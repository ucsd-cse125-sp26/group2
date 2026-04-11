# Networking Architecture

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [MessageStream -- Length-Prefixed Framing Protocol](#2-messagestream----length-prefixed-framing-protocol)
3. [Client](#3-client)
4. [Server](#4-server)
5. [Packet Format](#5-packet-format)
6. [Event System](#6-event-system)
7. [Server Game Loop](#7-server-game-loop)
8. [Client Game Loop](#8-client-game-loop)
9. [Input Pipeline](#9-input-pipeline)
10. [Current Limitations and TODOs](#10-current-limitations-and-todos)
11. [Data Flow Diagram](#11-data-flow-diagram)
12. [Connection Lifecycle](#12-connection-lifecycle)

---

## 1. Architecture Overview

The project uses a **client-authoritative-input / server-authoritative-state** model over
**TCP** (stream sockets). The networking layer is built on top of **SDL3_net**
(`SDL_net.h` / `NET_*` functions), which provides cross-platform socket
abstraction with non-blocking I/O.

Key properties:

- **Transport:** TCP stream sockets -- reliable, ordered delivery. No UDP path
  exists yet.
- **Library:** SDL3_net (`NET_CreateServer`, `NET_CreateClient`,
  `NET_StreamSocket`, `NET_ReadFromStreamSocket`, `NET_WriteToStreamSocket`).
- **Topology:** Single dedicated server, multiple clients. The server binds to
  `127.0.0.1:9999` by default. Each client connects to that address.
- **Framing:** A custom `MessageStream` class adds length-prefixed message
  framing on top of the raw TCP byte stream.
- **Current data flow:** Client sends `InputSnapshot` structs to the server.
  The server deserialises them, runs physics, but does **not** yet broadcast
  state back to clients (see [Section 10](#10-current-limitations-and-todos)).

### Source files

| Role | Header | Implementation |
|------|--------|----------------|
| Framing layer | `src/network/MessageStream.hpp` | `src/network/MessageStream.cpp` |
| Client | `src/client/network/Client.hpp` | `src/client/network/Client.cpp` |
| Server | `src/server/network/Server.hpp` | `src/server/network/Server.cpp` |
| Server game loop | `src/server/game/ServerGame.hpp` | `src/server/game/ServerGame.cpp` |
| Event queue | `src/server/systems/EventQueue.hpp` | `src/server/systems/EventQueue.cpp` |
| Input receive | `src/server/systems/InputReceiveSystem.hpp` | (inline) |
| Broadcast (stub) | `src/server/systems/BroadcastSystem.hpp` | -- |
| Input send | `src/client/systems/InputSendSystem.hpp` | (inline) |
| Input sample | `src/client/systems/InputSampleSystem.hpp` | (inline) |
| Prediction (stub) | `src/client/systems/PredictionSystem.hpp` | -- |
| Reconciliation (stub) | `src/client/systems/ReconciliationSystem.hpp` | -- |
| Packet struct | `src/ecs/components/InputSnapshot.hpp` | -- |

---

## 2. MessageStream -- Length-Prefixed Framing Protocol

`MessageStream` (`src/network/MessageStream.hpp/cpp`) wraps a raw
`NET_StreamSocket*` and splits the continuous TCP byte stream into discrete
messages using a **4-byte length prefix** protocol.

### Wire format

```
+-------------------+-----------------------------+
| length (4 bytes)  |  payload (length bytes)     |
+-------------------+-----------------------------+
```

- **Length header:** 4 bytes, `Uint32`, written in **host byte order** (not
  network/big-endian -- see note below). Represents the number of payload bytes
  that follow.
- **Payload:** Arbitrary bytes of exactly `length` size.

> **Note on byte order:** The code does `std::memcpy(&len, recvBuf.data(),
> sizeof(Uint32))` without any `ntohl` conversion. This means the protocol
> currently assumes both endpoints share the same endianness (which is true for
> x86/x64 builds). A cross-architecture deployment would require adding
> byte-order conversion.

### Sending (`MessageStream::send`)

1. Check that `socket` is non-null.
2. Write the 4-byte `size` value to the socket.
3. Write the `size` bytes of payload data to the socket.
4. Return whether the payload write succeeded.

### Receiving (`MessageStream::poll`)

1. Read up to 4096 bytes from the socket into a stack buffer.
2. If `n < 0`, the socket has errored -- return `false`.
3. If `n > 0`, append the bytes to the persistent `recvBuf` accumulation
   buffer (`std::vector<Uint8>`).
4. Enter a **drain loop**: while `recvBuf` contains at least 4 bytes (the
   length header):
   - `memcpy` the length from the front of `recvBuf`.
   - If the buffer does not yet contain `4 + length` bytes, break (wait for
     more data on the next poll).
   - Otherwise, invoke the callback with a pointer to the payload and the
     length.
   - Erase the consumed `4 + length` bytes from the front of `recvBuf`.
5. Return `true`.

The callback signature is:
```cpp
std::function<void(const void* data, Uint32 size)>
```

This pattern allows processing multiple complete messages per poll call if
they arrived in the same TCP segment, while correctly handling partial
messages that span multiple reads.

---

## 3. Client

`Client` (`src/client/network/Client.hpp/cpp`) manages the TCP connection
from the game client to the server.

### Connection flow (`Client::init`)

1. **DNS resolution:** `NET_ResolveHostname(addr)` resolves the hostname or IP
   address string. `NET_WaitUntilResolved(serverAddr, -1)` blocks until
   resolution completes (timeout of `-1` = infinite wait).
2. **Socket creation:** `NET_CreateClient(serverAddr, port)` creates a TCP
   stream socket and begins connecting.
3. **Connection wait:** `NET_WaitUntilConnected(sock, -1)` blocks until the
   TCP handshake completes. On failure, the socket is destroyed and `false` is
   returned.
4. **Assignment:** The connected socket is stored in `msgStream.socket`.
5. A log line prints the resolved server address string.

Default connection target: `127.0.0.1:9999` (hardcoded in `Game::init`).

### Sending (`Client::send`)

```cpp
bool send(const void* data, int len);
```

Sends a length-prefixed message. The implementation manually writes the 4-byte
length followed by the payload bytes directly through SDL_net, bypassing
`MessageStream::send`. (This is functionally equivalent but duplicates the
framing logic.)

### Polling (`Client::poll`)

```cpp
bool poll();
```

Calls `msgStream.poll()` with a callback that logs received messages. The
callback currently **only logs** the data -- it does not deserialise or apply
any server state. This is a placeholder for future state reconciliation.

Returns `false` always (the return value is not used meaningfully yet).

### Shutdown (`Client::shutdown`)

Destroys the stream socket and unrefs the resolved address, nulling both
pointers.

---

## 4. Server

`Server` (`src/server/network/Server.hpp/cpp`) manages the listening socket,
accepts incoming client connections, and dispatches received messages into the
event queue.

### Binding (`Server::init`)

1. Resolve the bind address via `NET_ResolveHostname` + `NET_WaitUntilResolved`.
2. Create the server socket with `NET_CreateServer(netAddr, port)`.
3. Unreference the resolved address (the server socket retains its own copy).
4. Initialise a fresh `EventQueue`.
5. Reset `nextClientId` to 0.

Default bind: `127.0.0.1:9999`, set in `src/server/main/main.cpp`.

### Per-client Connection struct

```cpp
struct Connection {
    MessageStream msgStream;  // Framed message stream for this client
    uint8_t clientId;         // Unique identifier assigned on accept
};
```

Clients are stored in `std::vector<Connection> clients`. Each connection has
its own `MessageStream` with an independent `recvBuf`, so partial message
reassembly is per-client.

### Accepting clients (`Server::acceptClients`)

Called once per `poll()`. Accepts **at most one** new client per tick via
`NET_AcceptClient`. On success:

1. Assigns `nextClientId++` as the client ID.
2. Pushes a new `Connection` onto the `clients` vector.
3. Sets the new connection's `msgStream.socket` and `clientId`.

### Reading clients (`Server::readClients`)

Iterates over all connections. For each one, calls `msgStream.poll()` with a
callback that invokes `handleMessage`. If `poll()` returns `false` (socket
error / disconnect):

1. Logs "client dead".
2. Destroys the stream socket.
3. Erases the connection from the vector.

### Message dispatch (`Server::handleMessage`)

```cpp
void handleMessage(Connection& conn, const void* data, Uint32 len);
```

1. **Size validation:** If `len != sizeof(InputSnapshot)`, logs an error and
   returns. This is the only packet type currently accepted.
2. **Deserialisation:** Calls `systems::runInputReceive(data)` which
   reinterpret-casts the raw bytes to `InputSnapshot*` and copies fields into
   an `Event` struct.
3. **Client ID:** Sets `event.clientId = conn.clientId`.
4. **Enqueue:** Pushes the event into the `EventQueue`.
5. **Acknowledgement:** Sends the ASCII string `"Message received"` (16 bytes)
   back to the client.
6. **Logging:** Logs all input fields (forward/back/left/right/jump/crouch/yaw/
   pitch/roll).

### Event access

The `Server` exposes `isEmpty()` and `dequeueEvent()` so `ServerGame` can
drain the event queue each tick.

### Shutdown

Destroys the `NET_Server`, then destroys all client stream sockets and clears
the client vector.

---

## 5. Packet Format

### InputSnapshot (Client -> Server)

The only packet currently sent over the network is the `InputSnapshot` struct
(`src/ecs/components/InputSnapshot.hpp`), transmitted as a **raw binary struct**
with no serialisation or versioning.

```
Offset  Size    Type      Field
──────  ──────  ────────  ─────────────
0       4       uint32_t  tick           Physics tick number
4       1       bool      forward        W key
5       1       bool      back           S key
6       1       bool      left           A key
7       1       bool      right          D key
8       1       bool      jump           Space key
9       1       bool      crouch         Left Ctrl
10      1       bool      sprint         Left Shift
11      1       bool      grapple        E / Middle mouse
12      1       bool      shooting       Fire button
13      3       --        (padding)      Compiler alignment padding (to 4-byte boundary)
16      4       float     yaw            Horizontal look (radians)
20      4       float     pitch          Vertical look (radians, clamped [-89deg, +89deg])
24      4       float     roll           Reserved (always 0)
28      4       float     prevTickYaw    Yaw at start of last physics tick
32      4       float     prevTickPitch  Pitch at start of last physics tick
──────  ──────
Total   36 bytes (approximate -- actual size depends on compiler struct packing)
```

> **Note:** The exact byte layout depends on the compiler's struct packing and
> alignment rules. `sizeof(InputSnapshot)` is checked at runtime by the server
> (`len != sizeof(InputSnapshot)`). Both client and server must be compiled with
> the same compiler and settings for this to work. No padding pragma or
> `__attribute__((packed))` is applied.

### Server -> Client

The server currently sends the ASCII string `"Message received"` (16 bytes) as
an acknowledgement after processing each input packet. This is a debug
placeholder -- no game state is transmitted back yet.

---

## 6. Event System

The event system bridges the network layer and the ECS game simulation on the
server side.

### MovementIntent

```cpp
class MovementIntent {
    bool forward, back, left, right, jump, crouch;
    float yaw, pitch, roll;
};
```

A decoded movement command extracted from a client's `InputSnapshot`. Contains
the same movement fields but lives in the server's event domain, decoupled from
the network packet format.

### Event

```cpp
class Event {
    int clientId;                   // Originating client
    MovementIntent movementIntent;  // Decoded movement fields
    bool shootIntent;               // Firing state
};
```

A single gameplay event. Each received `InputSnapshot` packet produces exactly
one `Event`.

### EventQueue

```cpp
class EventQueue {
    std::queue<Event> events;
    bool isEmpty();
    void enqueue(Event event);
    Event dequeue();  // throws if empty
    int size();
};
```

A simple FIFO queue backed by `std::queue<Event>`. **Not thread-safe** -- the
current design runs networking and game logic on the same thread, so no mutex
is needed. `dequeue()` throws `std::runtime_error` if called on an empty queue.

### How messages become events

1. `Server::readClients()` polls each client's `MessageStream`.
2. Complete messages trigger `Server::handleMessage()`.
3. `handleMessage` validates the size, calls `systems::runInputReceive(data)`
   which casts the raw bytes to `InputSnapshot*` and copies fields into a new
   `Event`.
4. The client ID is set on the event.
5. The event is pushed into the `EventQueue`.
6. `ServerGame::tick()` drains the queue via `server.dequeueEvent()`.

### InputReceiveSystem

`systems::runInputReceive` (`src/server/systems/InputReceiveSystem.hpp`) is an
inline function that performs a `static_cast<const InputSnapshot*>` on the raw
data pointer and maps each field to the corresponding `Event` /
`MovementIntent` field. No validation beyond the size check in `handleMessage`.

---

## 7. Server Game Loop

`ServerGame` (`src/server/game/ServerGame.hpp/cpp`) owns the `Server`, the ECS
`Registry`, and the client-to-entity mapping.

### Initialisation (`ServerGame::init`)

1. Sets `tickRateHz` (default 128 Hz).
2. Clears client-entity mapping and registry.
3. Calls `server.init(addr, port)` to bind the listening socket.
4. Previously spawned a test entity at y=200 (now commented out -- entities are
   created dynamically via `initNewPlayer`).

### Main loop (`ServerGame::run`)

```
running = true
dt        = 1.0 / tickRateHz          (e.g. 1/128 = 0.0078125 s)
perfFreq  = SDL_GetPerformanceFrequency()
tickDur   = perfFreq / tickRateHz      (counter ticks per game tick)
nextTick  = SDL_GetPerformanceCounter()

while (running):
    server.poll()              // accept clients + read all messages
    nextTick += tickDur        // advance the tick deadline
    tick(dt, nextTick)         // drain events + run physics

    // Sleep + spin-wait until nextTick
    now = SDL_GetPerformanceCounter()
    if now < nextTick:
        sleepMs = ((nextTick - now) * 1000 / perfFreq) - 1
        if sleepMs > 0:
            SDL_Delay(sleepMs)           // coarse sleep
        while SDL_GetPerformanceCounter() < nextTick:
            pass                         // spin-wait remainder
```

The timing strategy is **sleep + spin-wait**: the server sleeps for the bulk of
the remaining time (minus 1 ms safety margin), then spin-waits for the
sub-millisecond remainder to hit the exact tick boundary. This provides
accurate 128 Hz ticking without burning a full CPU core.

### Tick (`ServerGame::tick`)

1. **Event drain:** Dequeues all pending events from the server's `EventQueue`
   and passes each to `eventHandler`. If event handling exceeds the tick time
   budget (`now >= nextTick`), the remaining events are dropped with a log
   warning (TODO: this overflow handling needs refinement).
2. **Physics:** Runs `systems::runMovement` and `systems::runCollision` with
   the fixed `dt` and `physics::testWorld()`.
3. Increments `tickCount`.

### Event handler (`ServerGame::eventHandler`)

1. Looks up the `entt::entity` for the event's `clientId` in `clientEntities`.
2. If not found or the entity is invalid, returns silently.
3. Gets or creates an `InputSnapshot` component on the entity.
4. Copies all `MovementIntent` fields and `shootIntent` into the component.

### Player spawning (`ServerGame::initNewPlayer`)

Creates a new entity with `InputSnapshot`, `Position` (at `(0, 200, 0)`),
`Velocity`, `CollisionShape`, and `PlayerState`. Maps the entity to the given
`clientId` in `clientEntities`.

> **Note:** `initNewPlayer` is defined but is not yet called automatically when
> a client connects. The connection-to-entity lifecycle is incomplete.

---

## 8. Client Game Loop

The client uses SDL3's application-callback API:

- `SDL_AppInit` -- creates and initialises the `Game` object.
- `SDL_AppEvent` -- forwards SDL events (keyboard, mouse, window).
- `SDL_AppIterate` -- called as fast as possible (or VSync-capped); drives the
  game loop.
- `SDL_AppQuit` -- shuts down and deletes the `Game`.

### Fixed-timestep accumulator pattern (`Game::iterate`)

```
Constants:
    k_physicsHz        = 128
    k_physicsDt        = 1/128 s
    k_maxTicksPerFrame = 8     (spiral-of-death guard)

Each iterate() call:
    1. Compute frameTime = (now - prevTime) / perfFreq
       Cap frameTime to 0.25 s to prevent spiral-of-death
       accumulator += frameTime

    2. Refresh FPS / physics rate stats every 0.5 s

    3. Input:
       - Mouse look (yaw/pitch): ALWAYS sampled every iterate()
         for smooth camera at any FPS
       - Movement keys (WASD/jump/crouch): sampled once per physics
         tick group when inputSyncedWithPhysics is true (default)
       - InputSendSystem: sends InputSnapshot to server every iterate()

    4. Physics (when accumulator >= k_physicsDt):
       - Sample movement keys once for the tick group (if synced)
       - While accumulator >= k_physicsDt and ticksThisFrame < 8:
           - Save PreviousPosition for interpolation
           - Run MovementSystem
           - Run CollisionSystem
           - Decrement accumulator by k_physicsDt
       - Poll server for responses (client.poll())

    5. Skip rendering if renderSeparateFromPhysics is false and
       no physics ran this frame

    6. Render:
       - Interpolate position: alpha = accumulator / k_physicsDt
       - renderPos = lerp(previousPos, currentPos, alpha)
       - Camera yaw/pitch use latest values directly (never interpolated)
       - Draw the scene
```

### Physics vs render separation

| Setting | Behaviour |
|---------|-----------|
| `renderSeparateFromPhysics = true` (default) | Render every `iterate()` call with position interpolation between last two physics ticks. FPS is uncapped (or VSync). |
| `renderSeparateFromPhysics = false` | Only render after a physics tick ran. Caps visual FPS to 128 Hz. |
| `inputSyncedWithPhysics = true` (default) | Movement keys sampled once per physics tick group (server-consistent). Mouse look still per-frame. |
| `inputSyncedWithPhysics = false` | Movement keys also sampled every iterate() call. |
| `limitFPSToMonitor = false` (default) | VSync off. |

---

## 9. Input Pipeline

The full path of a player input from key press to physics effect:

### Client side

1. **InputSampleSystem** (`src/client/systems/InputSampleSystem.hpp`)
   - `runMouseLook()`: Called every `iterate()`. Reads `SDL_GetRelativeMouseState`,
     accumulates yaw/pitch on the local player's `InputSnapshot`. Wraps yaw to
     [-pi, pi], clamps pitch to [-89deg, +89deg].
   - `runMovementKeys()`: Called once per physics tick group (or every iterate if
     `inputSyncedWithPhysics` is off). Reads `SDL_GetKeyboardState`, sets
     `forward/back/left/right/jump/crouch/sprint/grapple` booleans.

2. **InputSendSystem** (`src/client/systems/InputSendSystem.hpp`)
   - `runInputSend()`: Called every `iterate()`. Iterates entities with
     `<InputSnapshot, LocalPlayer>`, sends each snapshot to the server as
     `sizeof(InputSnapshot)` raw bytes via `Client::send`.

3. **Network transport:** `Client::send` writes a 4-byte length prefix +
   `sizeof(InputSnapshot)` payload bytes over the TCP socket.

### Server side

4. **Server::readClients()**: Polls each client's `MessageStream`. Complete
   messages are passed to `handleMessage`.

5. **Server::handleMessage()**: Validates size == `sizeof(InputSnapshot)`.
   Calls `systems::runInputReceive(data)` to produce an `Event`.

6. **InputReceiveSystem** (`src/server/systems/InputReceiveSystem.hpp`):
   `runInputReceive()` casts the raw data to `const InputSnapshot*` and copies
   movement fields + shoot intent into a new `Event` struct.

7. **EventQueue**: The event is enqueued with the originating `clientId`.

8. **ServerGame::tick()**: Drains the event queue. For each event, calls
   `eventHandler`.

9. **ServerGame::eventHandler()**: Looks up the player entity by `clientId`,
   writes the `MovementIntent` fields into the entity's `InputSnapshot`
   component.

10. **Physics**: `systems::runMovement` and `systems::runCollision` read the
    `InputSnapshot` component to compute velocity, position, and collisions.

### Summary

```
[Client]                              [Server]
SDL input  ->  InputSampleSystem      |
           ->  InputSendSystem -------+-> Server::readClients
                                      |   -> handleMessage
                                      |   -> InputReceiveSystem
                                      |   -> EventQueue
                                      |   -> eventHandler
                                      |   -> InputSnapshot component
                                      |   -> MovementSystem
                                      |   -> CollisionSystem
```

---

## 10. Current Limitations and TODOs

### Not yet implemented

| Component | File | Status |
|-----------|------|--------|
| **BroadcastSystem** | `src/server/systems/BroadcastSystem.hpp` | Empty stub. Contains only `// TODO: implement runBroadcast()`. No server-to-client state broadcast exists. |
| **PredictionSystem** | `src/client/systems/PredictionSystem.hpp` | Empty stub. Contains only `// TODO: implement runPrediction()`. Planned to apply local input immediately without waiting for server confirmation, storing snapshots in a ring buffer. |
| **ReconciliationSystem** | `src/client/systems/ReconciliationSystem.hpp` | Empty stub. Contains only `// TODO: implement runReconciliation()`. Planned to rewind and re-simulate when the server sends corrections. |

### Known issues and gaps

- **No state broadcast:** The server processes input and runs physics but never
  sends game state (positions, velocities, etc.) back to clients. Each client
  runs its own local simulation with no server correction. This is the most
  critical missing piece for multiplayer.

- **No player spawn on connect:** `ServerGame::initNewPlayer()` exists but is
  never called automatically when a client connects. There is no
  connect/disconnect event from the network layer to the game layer -- the
  server just starts receiving packets.

- **Client::send duplicates framing:** `Client::send` manually writes the
  4-byte length + payload instead of delegating to `MessageStream::send`. This
  works but duplicates logic.

- **Client::poll ignores data:** The poll callback only logs received data. No
  deserialisation or state application occurs.

- **No packet type discrimination:** The server only handles one packet type
  (`InputSnapshot`, validated by size). There is no message type header or
  protocol versioning. Adding more packet types will require a type
  discriminator.

- **Raw struct serialisation:** `InputSnapshot` is sent as a raw memory copy.
  This is fragile: any struct layout change, compiler difference, or
  endianness mismatch will break the protocol silently.

- **Host byte order for length prefix:** The `MessageStream` 4-byte length
  header is written in host byte order. Cross-platform deployment (e.g.
  x86 server + ARM client) would silently corrupt framing.

- **EventQueue not thread-safe:** The queue uses `std::queue` with no mutex.
  Safe only because networking and game logic run on the same thread.

- **Event overflow handling:** If event processing exceeds the tick time budget,
  remaining events are dropped. The TODO in `ServerGame::tick` notes this needs
  proper handling.

- **Single-read limit:** `MessageStream::poll` reads at most 4096 bytes per
  call. Under high throughput this may cause message processing to lag behind
  arrival rate.

- **No heartbeat or keep-alive:** Dead connections are only detected when a
  `read` fails. There is no periodic ping/pong to detect silent disconnects.

- **No reconnection:** If the client connection fails during `init`, the client
  exits. There is no retry logic or reconnection mechanism.

---

## 11. Data Flow Diagram

### Current (implemented)

```
+------------------+                          +-------------------+
|     CLIENT       |                          |      SERVER       |
|                  |                          |                   |
|  SDL Input       |                          |                   |
|    |             |                          |                   |
|    v             |                          |                   |
|  InputSample     |                          |                   |
|  System          |                          |                   |
|    |             |                          |                   |
|    v             |                          |                   |
|  InputSnapshot   |                          |                   |
|    |             |                          |                   |
|    v             |     TCP (port 9999)      |                   |
|  InputSend  -----+--- [4B len | payload] --+-> readClients()   |
|  System          |                          |    |              |
|                  |                          |    v              |
|                  |                          |  handleMessage()  |
|                  |                          |    |              |
|                  |                          |    v              |
|                  |                          |  InputReceive     |
|                  |                          |  System           |
|                  |                          |    |              |
|                  |                          |    v              |
|                  |                          |  EventQueue       |
|                  |                          |    |              |
|                  |                          |    v              |
|                  |                          |  eventHandler()   |
|                  |                          |    |              |
|                  |                          |    v              |
|  Local physics   |                          |  InputSnapshot    |
|  (independent)   |                          |  component        |
|    |             |                          |    |              |
|    v             |                          |    v              |
|  Movement +      |                          |  MovementSystem   |
|  Collision       |                          |  CollisionSystem  |
|    |             |                          |    |              |
|    v             |    "Message received"    |    v              |
|  client.poll() <-+--- [4B len | ASCII] ----+  (ack only)      |
|  (logs only)     |                          |                   |
+------------------+                          +-------------------+
```

### Planned (future)

```
+------------------+                          +-------------------+
|     CLIENT       |                          |      SERVER       |
|                  |                          |                   |
|  InputSample     |     InputSnapshot        |                   |
|  InputSend  -----+------------------------>|  InputReceive     |
|                  |                          |  EventQueue       |
|                  |                          |  Physics          |
|                  |                          |    |              |
|                  |     State Snapshot        |    v              |
|  Reconciliation  |<------------------------+  Broadcast        |
|  System          |                          |  System           |
|    |             |                          |                   |
|    v             |                          +-------------------+
|  Prediction      |
|  System          |
|    |             |
|    v             |
|  Render          |
+------------------+
```

---

## 12. Connection Lifecycle

### Current implementation

```
CLIENT                                    SERVER
  |                                         |
  |  NET_ResolveHostname("127.0.0.1")       |  NET_ResolveHostname("127.0.0.1")
  |  NET_WaitUntilResolved (blocking)       |  NET_WaitUntilResolved (blocking)
  |                                         |  NET_CreateServer(addr, 9999)
  |                                         |  <listening>
  |                                         |
  |  NET_CreateClient(addr, 9999)           |
  |  NET_WaitUntilConnected (blocking)      |
  |  <connected>                            |
  |                                         |  NET_AcceptClient() -> socket
  |                                         |  assign clientId = nextClientId++
  |                                         |  create Connection{socket, clientId}
  |                                         |
  | ---- InputSnapshot (every iterate) ---> |  readClients() -> handleMessage()
  |                                         |  enqueue Event
  | <--- "Message received" (16 bytes) ---- |  (ack)
  |                                         |
  | ---- InputSnapshot ------------------> |  ...
  | <--- "Message received" -------------- |  ...
  |                                         |
  |         ... (loop continues) ...        |
  |                                         |
  |  <socket error or disconnect>           |  poll() returns false
  |                                         |  log "client dead"
  |                                         |  destroy socket
  |                                         |  erase from clients vector
  |                                         |
  |  Client::shutdown()                     |  Server::shutdown()
  |  NET_DestroyStreamSocket                |  NET_DestroyServer
  |  NET_UnrefAddress                       |  destroy all client sockets
```

### Gaps in the lifecycle

- **No connect event:** When a client connects, the server assigns a client ID
  and adds it to the connection list, but there is no event dispatched to the
  game layer. `ServerGame::initNewPlayer()` is not called. The game only
  becomes aware of a client when it first sends an `InputSnapshot` -- but if
  there is no entity mapped to that `clientId`, the `eventHandler` silently
  drops the event.

- **No disconnect event:** When a client socket errors, the server removes the
  connection from `clients` but does not notify `ServerGame`. The player entity
  remains in the registry with its last known state.

- **No graceful disconnect:** There is no disconnect packet or handshake.
  Disconnection is detected only by TCP socket read failure.

- **No client-side disconnect handling:** `Client::poll()` does not check the
  return value of `msgStream.poll()`. If the server drops, the client has no
  mechanism to detect it or reconnect.

- **Client ID overflow:** `nextClientId` is `uint8_t`, so it wraps after 255
  connections. If client IDs are reused while old entities still exist in the
  registry, the mapping will collide.
