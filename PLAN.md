# Fix SEND/RECV bandwidth metrics on UDP-session transport

## Context

DebugUI's SEND and RECV rows are wrong when the client uses
`UdpSessionTransport` (`transportConfig_.useUdpSessions`). The HUD computes
bandwidth from `bytesSentWindow` and `bytesRecvWindow` in
`Client::updateStats`, but the UDP-session branch of `Client::networkLoop`
currently mirrors only RTT from `session_.stats()`.

The transport already owns monotonic byte counters:

- `UdpSessionTransport::Stats::bytesSent`
- `UdpSessionTransport::Stats::bytesRecv`

Those counters are updated when packets are sent/received inside
`UdpSessionTransport`, including relay envelope bytes. The client should use
those counters as the source of truth for UDP-session bandwidth instead of
trying to maintain per-call accounting in the client.

## Critique of the Current Draft

The original plan has the right core idea: sample the transport's monotonic
byte counters and feed deltas into the existing HUD windows. That avoids
touching every high-frequency `session_.send(...)` call and keeps relay/direct
wire accounting in one place.

However, it needs a few corrections before implementation:

- It says every UDP-session send call bypasses the client counters, but
  `Client::send()` already increments `stats.bytesSentTotal` and
  `bytesSentWindow` after sending on `ControlReliableOrdered`. Mirroring
  `session_.stats().bytesSent` without removing that manual increment will
  double-count chat/ready/control packets.
- The plan should explicitly reset the "last sampled" UDP-session byte
  snapshots when a new session starts. Default member initialization is fine
  for the first connection, but reconnects should not depend on object
  lifetime assumptions.
- `session_.stats()` returns a reference to transport-owned mutable state.
  In the current client, UDP-session sends can happen from the game thread
  while `networkLoop()` pumps and samples on the network thread. Keep the
  sampled values short-lived and copy only the needed fields before doing
  client-side bookkeeping.
- The final implementation should avoid mixing two accounting models on the
  UDP-session path. Either account at every `session_.send(...)` and receive
  event, or mirror transport counters. Mirroring transport counters is simpler
  and more complete.

## Phased Plan

### Phase 1: Confirm the Accounting Boundary

Goal: make the intended ownership clear before changing code.

- Treat `UdpSessionTransport::Stats::{bytesSent,bytesRecv}` as the only
  bandwidth source for `usingUdpSession_ == true`.
- Keep existing client-side accounting for the legacy TCP plus UDP-sidecar path
  (`usingUdpSession_ == false`), because that path does not use
  `UdpSessionTransport::Stats`.
- Do not add byte increments at individual UDP-session `session_.send(...)`
  call sites. The transport has better information about fragmentation,
  redundancy, direct-vs-relay routing, and actual send success.

Status: complete.

Findings:

- The legacy path owns its client-side accounting through direct writes to
  `stats.bytesSentTotal`, `bytesSentWindow`, `stats.bytesRecvTotal`, and
  `bytesRecvWindow` in `Client.cpp`.
- The legacy send writers are `Client::send()` for TCP-framed messages,
  the UDP-sidecar immediate-send paths in `sendPing()`,
  `sendInputSnapshot()`, `sendShotIntent()`, and the delayed outbound drain in
  `networkLoop()`.
- The legacy receive writer is `Client::poll()`, after `msgStream` strips the
  TCP length prefix.
- The UDP-session transport already records actual wire bytes in
  `UdpSessionTransport.cpp`: receives at datagram pump time, direct sends in
  `sendDirect()`, and relay sends in `sendViaRelay()`.
- The only current UDP-session client-side byte writer is the
  `usingUdpSession_` branch in `Client::send()`. Phase 2 should remove that
  manual write before Phase 4 mirrors transport byte deltas.

### Phase 2: Remove the UDP-Session Double-Count Hazard

Goal: make mirroring transport counters safe.

- In `Client::send()`, keep the current manual accounting for the legacy path.
- In the `usingUdpSession_` branch of `Client::send()`, send on
  `ControlReliableOrdered` and return the send result without updating
  `stats.bytesSentTotal` or `bytesSentWindow`.
- Leave `sendPing()`, `sendInputSnapshot()`, `sendShotIntent()`, and
  `sendVoiceFrame()` without manual UDP-session accounting.

Expected result: no UDP-session send path updates client bandwidth counters
directly; all UDP-session bytes can be mirrored from the transport once.

Status: complete.

Implementation note:

- `Client::send()` now returns the UDP-session
  `ControlReliableOrdered` send result directly without touching
  `stats.bytesSentTotal` or `bytesSentWindow`.
- Legacy `Client::send()` accounting is unchanged.

### Phase 3: Add Per-Session Delta Sampling State

Goal: track transport counter deltas across network-loop iterations.

- Add two client members near the existing bandwidth window fields in
  `src/client/network/Client.hpp`:

  ```cpp
  uint64_t udpSessionLastBytesSent_ = 0;
  uint64_t udpSessionLastBytesRecv_ = 0;
  ```

- Reset both fields to `0` when starting a UDP-session connection in
  `Client::init()` before launching `networkThread_`.
- Optionally reset them in `Client::shutdown()` after `session_.close()` so the
  object's disconnected state is internally consistent.

Status: complete.

Implementation note:

- `Client` now stores `udpSessionLastBytesSent_` and
  `udpSessionLastBytesRecv_` beside the existing bandwidth window counters.
- Both snapshots reset after a UDP-session connection succeeds and again during
  shutdown after the transport is closed.

### Phase 4: Mirror Transport Deltas in the UDP-Session Network Loop

Goal: feed the existing HUD windows and totals from transport counters.

- In the `usingUdpSession_` branch of `Client::networkLoop()`, after
  `session_.pump()` and event polling, copy the relevant session stats:

  ```cpp
  const auto& sessionStats = session_.stats();
  const float rttMs = sessionStats.rttMs;
  const uint64_t bytesSent = sessionStats.bytesSent;
  const uint64_t bytesRecv = sessionStats.bytesRecv;
  ```

- Continue mirroring RTT as today.
- Under `stateMutex_`, compute byte deltas against the saved snapshots, advance
  the snapshots, and add the deltas to:

  - `bytesSentWindow`
  - `bytesRecvWindow`
  - `stats.bytesSentTotal`
  - `stats.bytesRecvTotal`

- Use monotonic-counter-safe delta logic. A normal session should only increase,
  but if the transport stats are ever reset, handle it by treating the current
  value as the new baseline instead of underflowing.

Example shape:

```cpp
const uint64_t sentDelta =
    bytesSent >= udpSessionLastBytesSent_ ? bytesSent - udpSessionLastBytesSent_ : 0;
const uint64_t recvDelta =
    bytesRecv >= udpSessionLastBytesRecv_ ? bytesRecv - udpSessionLastBytesRecv_ : 0;
udpSessionLastBytesSent_ = bytesSent;
udpSessionLastBytesRecv_ = bytesRecv;
```

Status: complete.

Implementation note:

- The UDP-session `networkLoop()` branch now copies RTT and byte totals out of
  `session_.stats()` after pumping and event delivery.
- It mirrors only monotonic byte deltas into `bytesSentWindow`,
  `bytesRecvWindow`, `stats.bytesSentTotal`, and `stats.bytesRecvTotal`.
- If a sampled transport total is lower than the previous baseline, the delta
  is treated as `0` and the baseline is advanced to the sampled value.

### Phase 5: Build and Runtime Verification

Goal: prove the HUD reports real bandwidth without regressing legacy transport.

- Build with the canonical command:

  ```bash
  just build-debug
  ```

- Run a server and the primary client:

  ```bash
  just server-debug
  just client-debug
  ```

- With UDP sessions enabled, open the debug HUD and confirm:

  - SEND is non-zero while inputs, pings, shots, chat, or voice packets are
    being sent.
  - RECV is non-zero once snapshots and server messages arrive.
  - `stats.bytesSentTotal` and `stats.bytesRecvTotal` increase steadily and
    roughly match the HUD rates over several one-second windows.
  - Control packets are not double-counted after the `Client::send()` cleanup.

- Sanity-check the legacy transport path (`usingUdpSession_ == false`) still
  reports bandwidth, since its accounting remains client-side.

Status: complete.

Verification performed:

- `just build-debug` passes after Phase 4.
- Manual HUD verification passed with a server/client run: SEND and RECV now
  display correctly on the UDP-session path.

Remaining optional sanity check:

- Toggle or configure the legacy transport path and confirm its SEND/RECV
  accounting is unchanged. The implementation does not alter the legacy
  accounting path.

## Critical Files

- `src/client/network/Client.cpp`
- `src/client/network/Client.hpp`
- `src/network/transport/UdpSessionTransport.hpp` and `.cpp` for reference only
