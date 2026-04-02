#include "GameServer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

bool GameServer::init(uint16_t port)
{
    if (!UdpSocket::platformInit()) {
        SDL_Log("Server: socket platform init failed");
        return false;
    }
    if (!sock.open() || !sock.bind(port)) {
        SDL_Log("Server: cannot bind UDP port %u", port);
        return false;
    }
    sock.setNonBlocking(true);

    // Build the level (same world used by clients)
    world = level::build();

    SDL_Log("Server: listening on UDP port %u  (tick=%d Hz, snapshot=%d Hz)", port, k_serverTickHz, k_snapshotHz);

    for (uint8_t i = 0; i < k_maxPlayers; ++i)
        clients[i].id = i;

    return true;
}

void GameServer::shutdown()
{
    sock.close();
    UdpSocket::platformShutdown();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void GameServer::run()
{
    const Uint64 nsPerTick = static_cast<Uint64>(1e9 / k_serverTickHz);
    Uint64 nextTick = SDL_GetTicksNS();

    while (true) {
        receiveAll();

        Uint64 now = SDL_GetTicksNS();
        if (now >= nextTick) {
            tick(k_tickDt);
            nextTick += nsPerTick;
        } else {
            SDL_DelayNS(std::min(nextTick - now, (Uint64)500000)); // sleep up to 0.5 ms
        }
    }
}

// ---------------------------------------------------------------------------
// Single tick
// ---------------------------------------------------------------------------

void GameServer::tick(float dt)
{
    serverTick++;
    simTime += dt;

    processTick(dt);
    checkRespawns(dt);
    pushLagCompSnapshot();

    if (serverTick % k_snapshotEvery == 0)
        broadcastSnapshot();
}

// ---------------------------------------------------------------------------
// Physics + weapon processing for all connected clients
// ---------------------------------------------------------------------------

void GameServer::processTick(float dt)
{
    for (auto& cl : clients) {
        if (!cl.connected || cl.entity == entt::null)
            continue;

        auto& hp = registry.get<Health>(cl.entity);
        if (!hp.alive())
            continue;

        if (cl.inputCount > 0) {
            // Use the newest packet for continuous analog state (aim, movement).
            const PktInput& latest = cl.inputBuf[cl.inputCount - 1];

            auto& input = registry.get<InputState>(cl.entity);
            auto& cam = registry.get<CameraAngles>(cl.entity);

            input.moveDir.x = latest.moveRight;
            input.moveDir.y = latest.moveFwd;
            cam.yaw = latest.yaw;
            cam.pitch = latest.pitch;

            // OR held/pressed bits across ALL buffered packets so no event is
            // dropped when multiple packets arrive in the same server tick.
            bool anyFireHeld = false;
            bool anyAltFireHeld = false;
            bool anyJumpHeld = false;
            bool anyJumpPressed = false;
            bool anyKnife = false;
            for (int j = 0; j < cl.inputCount; ++j) {
                const Buttons& b = cl.inputBuf[j].buttons;
                if (b.fire())
                    anyFireHeld = true;
                if (b.altFire())
                    anyAltFireHeld = true;
                if (b.jumpHeld())
                    anyJumpHeld = true;
                if (b.jump())
                    anyJumpPressed = true;
                if (b.knife())
                    anyKnife = true;
            }

            // Server-side rising-edge detection for semi-auto weapons.
            // Client sends fireHeld (continuous); we compute the pressed edge here
            // so a single missed/late packet can't swallow an entire shot.
            bool firePressed = anyFireHeld && !cl.prevFireHeld;
            bool altFirePressed = anyAltFireHeld && !cl.prevAltFireHeld;
            cl.prevFireHeld = anyFireHeld;
            cl.prevAltFireHeld = anyAltFireHeld;

            input.jumpHeld = anyJumpHeld;
            input.jumpPressed = anyJumpPressed;
            input.crouchHeld = latest.buttons.crouch();
            input.glideHeld = anyJumpHeld;

            processWeapon(cl, latest, anyFireHeld, firePressed, anyAltFireHeld, altFirePressed, anyKnife, dt);
            cl.inputCount = 0; // consumed
        }
    }

    physicsUpdate(registry, world, physicsCfg, dt);
}

// ---------------------------------------------------------------------------
// Weapon / hitscan processing
// ---------------------------------------------------------------------------

void GameServer::processWeapon(ClientSlot& cl,
                               const PktInput& inp,
                               bool fireHeld,
                               bool firePressed,
                               bool altFireHeld,
                               bool /*altFirePressed*/,
                               bool knifePressed,
                               float dt)
{
    auto& ws = registry.get<WeaponState>(cl.entity);
    ws.active =
        static_cast<WeaponId>(std::clamp(static_cast<int>(ws.active), 0, static_cast<int>(WeaponId::Count) - 1));
    weaponUpdate(ws, dt);

    // altFire held with no primary fire = reload
    if (altFireHeld && !fireHeld)
        weaponTryReload(ws);

    WeaponId useWeapon = knifePressed ? WeaponId::Knife : ws.active;
    const auto& stats = k_weaponStats[static_cast<int>(useWeapon)];
    const bool isMelee = (stats.range <= 100.0f);

    bool didFire;
    if (isMelee) {
        // Melee: use held so swings feel responsive; cooldown limits rate.
        didFire = weaponTryFire(ws, fireHeld || knifePressed, fireHeld || knifePressed, dt);
    } else {
        // Ranged semi-auto: server-computed rising edge avoids the dropped-packet problem.
        // Automatic: held fires continuously, rate limited by cooldown.
        didFire = weaponTryFire(ws, fireHeld, firePressed, dt);
    }

    if (!didFire)
        return;

    // Build aim direction from client's yaw/pitch
    const auto& cam = registry.get<CameraAngles>(cl.entity);
    const auto& tf = registry.get<Transform>(cl.entity);
    const auto& ctrl = registry.get<PlayerController>(cl.entity);
    glm::vec3 eyePos = tf.position + glm::vec3(0.0f, ctrl.eyeHeight, 0.0f);

    float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
    float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
    glm::vec3 aimDir = glm::normalize(glm::vec3(-sy * cp, sp, cy * cp));

    // Build lag-compensated capsule list for all other players
    glm::vec3 rwPos[k_maxPlayers];
    bool rwAlive[k_maxPlayers];
    bool hasRewind = lagComp.rewind(inp.clientTick, rwPos, rwAlive);

    CapsuleInfo caps[k_maxPlayers];
    int capCount = 0;
    for (auto& other : clients) {
        if (!other.connected || other.entity == entt::null || other.id == cl.id)
            continue;
        auto& ohp = registry.get<Health>(other.entity);
        if (!ohp.alive())
            continue;

        glm::vec3 pos;
        bool alive = true;
        if (hasRewind) {
            pos = rwPos[other.id];
            alive = rwAlive[other.id];
        } else {
            pos = registry.get<Transform>(other.entity).position;
        }
        if (!alive)
            continue;

        caps[capCount++] = {
            .id = other.id,
            .center = pos + glm::vec3(0.0f, LagComp::k_capsuleYOffset, 0.0f),
            .radius = LagComp::k_capsuleRadius,
            .halfHeight = LagComp::k_capsuleHalfHeight,
        };
    }

    HitResult hit;
    if (stats.range <= 100.0f)
        hit = meleeAttack(eyePos, aimDir, stats, caps, capCount);
    else
        hit = hitscanFire(eyePos, aimDir, stats, world, caps, capCount);

    if (hit.hit && hit.victimId >= 0) {
        // Send hit confirm to shooter
        sendEvent(cl, EventType::HitConfirm, static_cast<uint8_t>(hit.victimId), static_cast<uint8_t>(stats.damage));

        // Apply damage to victim
        auto& vic = clients[hit.victimId];
        if (vic.connected && vic.entity != entt::null) {
            auto& vicHp = registry.get<Health>(vic.entity);
            int actual = vicHp.applyDamage(stats.damage);
            sendEvent(vic, EventType::Damaged, cl.id, static_cast<uint8_t>(actual));
            if (!vicHp.alive())
                killPlayer(vic, cl.id);
        }
    }
}

// ---------------------------------------------------------------------------
// Lag comp snapshot push
// ---------------------------------------------------------------------------

void GameServer::pushLagCompSnapshot()
{
    LagSnapshot::Entry entries[k_maxPlayers] = {};
    uint8_t cnt = 0;
    for (auto& cl : clients) {
        if (!cl.connected || cl.entity == entt::null)
            continue;
        const auto& tf = registry.get<Transform>(cl.entity);
        const auto& hp = registry.get<Health>(cl.entity);
        entries[cl.id] = {tf.position, hp.alive()};
        cnt++;
    }
    lagComp.push(serverTick, entries, cnt);
}

// ---------------------------------------------------------------------------
// Broadcast snapshot to all clients
// ---------------------------------------------------------------------------

void GameServer::broadcastSnapshot()
{
    PktSnapshot snap{};
    snap.serverTick = serverTick;
    snap.playerCount = 0;

    for (auto& cl : clients) {
        if (!cl.connected || cl.entity == entt::null)
            continue;
        auto& tf = registry.get<Transform>(cl.entity);
        auto& vel = registry.get<Velocity>(cl.entity);
        auto& ctrl = registry.get<PlayerController>(cl.entity);
        auto& cam = registry.get<CameraAngles>(cl.entity);
        auto& hp = registry.get<Health>(cl.entity);
        auto& ws = registry.get<WeaponState>(cl.entity);

        auto& ps = snap.players[snap.playerCount++];
        ps.id = cl.id;
        ps.flags = PlayerState::makeFlags(hp.alive(), ctrl.onGround, ctrl.onWall);
        ps.health = static_cast<uint8_t>(std::max(0, std::min(255, hp.current)));
        ps.weapon = static_cast<uint8_t>(ws.active);
        ps.ammo = static_cast<uint8_t>(std::max(0, std::min(255, ws.ammo)));
        ps.posX = tf.position.x;
        ps.posY = tf.position.y;
        ps.posZ = tf.position.z;
        ps.velX = vel.linear.x;
        ps.velY = vel.linear.y;
        ps.velZ = vel.linear.z;
        ps.yaw = cam.yaw;
        ps.pitch = cam.pitch;
    }

    int pktLen = static_cast<int>(sizeof(PacketHeader) + 8 + snap.playerCount * sizeof(PlayerState));

    for (auto& cl : clients) {
        if (!cl.connected)
            continue;
        cl.chan.send(&snap, pktLen, PacketType::Snapshot, 0xFF);
    }
}

// ---------------------------------------------------------------------------
// Receive all pending UDP packets
// ---------------------------------------------------------------------------

void GameServer::receiveAll()
{
    sockaddr_in from{};
    int n;
    while ((n = sock.recvFrom(buf, sizeof(buf), from)) > 0) {
        if (n < static_cast<int>(sizeof(PacketHeader)))
            continue;
        auto* hdr = reinterpret_cast<PacketHeader*>(buf);
        if (hdr->magic != k_magic)
            continue;
        handlePacket(hdr, n, from);
    }
}

// ---------------------------------------------------------------------------
// Packet dispatch
// ---------------------------------------------------------------------------

void GameServer::handlePacket(const PacketHeader* hdr, int len, const sockaddr_in& from)
{
    auto type = static_cast<PacketType>(hdr->type);

    if (type == PacketType::Connect) {
        if (len >= static_cast<int>(sizeof(PktConnect)))
            handleConnect(reinterpret_cast<const PktConnect*>(hdr), from);
        return;
    }

    ClientSlot* cl = findClient(from);
    if (!cl)
        return;

    switch (type) {
    case PacketType::Input:
        if (len >= static_cast<int>(sizeof(PktInput)))
            handleInput(*cl, reinterpret_cast<const PktInput*>(hdr));
        break;
    case PacketType::Disconnect:
        handleDisconnect(*cl);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Client connect / disconnect
// ---------------------------------------------------------------------------

void GameServer::handleConnect(const PktConnect* pkt, const sockaddr_in& from)
{
    // Already connected?
    ClientSlot* existing = findClient(from);
    if (existing) {
        // Re-send ack
        PktConnectAck ack{};
        ack.hdr.clientId = existing->id;
        ack.clientId = existing->id;
        ack.serverTick = serverTick;
        existing->chan.send(&ack, sizeof(ack), PacketType::ConnectAck, 0xFF);
        return;
    }

    uint8_t slot = nextFreeSlot();
    if (slot == 0xFF) {
        // Server full — send kick
        PacketHeader kick{};
        NetChannel tmp;
        tmp.init(&sock, &from);
        tmp.send(&kick, sizeof(kick), PacketType::Kick, 0xFF);
        return;
    }

    auto& cl = clients[slot];
    cl.connected = true;
    cl.addr = from;
    cl.chan.init(&sock, &from);
    std::memcpy(cl.name, pkt->name, sizeof(cl.name));
    cl.name[15] = '\0';
    cl.lastInputTick = 0;

    char addrStr[32];
    UdpSocket::addrToStr(from, addrStr, sizeof(addrStr));
    SDL_Log("Server: player %u '%s' connected from %s", slot, cl.name, addrStr);

    connectedCount++;

    spawnPlayer(cl);

    // Send ack
    PktConnectAck ack{};
    ack.hdr.clientId = slot;
    ack.clientId = slot;
    ack.serverTick = serverTick;
    cl.chan.send(&ack, sizeof(ack), PacketType::ConnectAck, 0xFF);
}

void GameServer::handleInput(ClientSlot& cl, const PktInput* pkt)
{
    if (pkt->clientTick <= cl.lastInputTick && cl.lastInputTick != 0)
        return; // stale

    // Buffer the input (we keep only the newest)
    if (cl.inputCount < ClientSlot::k_inputBuf) {
        cl.inputBuf[cl.inputCount++] = *pkt;
    } else {
        // Overwrite oldest
        std::memmove(&cl.inputBuf[0], &cl.inputBuf[1], sizeof(PktInput) * (ClientSlot::k_inputBuf - 1));
        cl.inputBuf[ClientSlot::k_inputBuf - 1] = *pkt;
    }
    cl.lastInputTick = pkt->clientTick;
}

void GameServer::handleDisconnect(ClientSlot& cl)
{
    SDL_Log("Server: player %u '%s' disconnected", cl.id, cl.name);
    if (cl.entity != entt::null) {
        registry.destroy(cl.entity);
        cl.entity = entt::null;
    }
    cl.connected = false;
    cl.inputCount = 0;
    connectedCount = connectedCount > 0 ? connectedCount - 1 : 0;
}

// ---------------------------------------------------------------------------
// Player spawn / kill / respawn
// ---------------------------------------------------------------------------

void GameServer::spawnPlayer(ClientSlot& cl)
{
    if (cl.entity != entt::null)
        registry.destroy(cl.entity);

    cl.entity = registry.create();
    glm::vec3 spawnPt = k_spawns[cl.id % k_maxPlayers];

    registry.emplace<Transform>(cl.entity, Transform{spawnPt});
    registry.emplace<Velocity>(cl.entity);
    registry.emplace<CameraAngles>(cl.entity);
    registry.emplace<InputState>(cl.entity);
    registry.emplace<PlayerController>(cl.entity);
    registry.emplace<WallRun>(cl.entity);
    registry.emplace<GrappleHook>(cl.entity);
    registry.emplace<Health>(cl.entity);
    registry.emplace<WeaponState>(cl.entity);

    cl.respawnTimer = -1.0f;
    SDL_Log("Server: spawned player %u at (%.0f,%.0f,%.0f)",
            cl.id,
            static_cast<double>(spawnPt.x),
            static_cast<double>(spawnPt.y),
            static_cast<double>(spawnPt.z));
}

void GameServer::killPlayer(ClientSlot& cl, int attackerId)
{
    auto& hp = registry.get<Health>(cl.entity);
    hp.current = 0;
    cl.deaths++;

    if (attackerId >= 0 && attackerId < k_maxPlayers)
        clients[attackerId].kills++;

    cl.respawnTimer = k_respawnDelay;
    sendEvent(cl, EventType::Killed, static_cast<uint8_t>(attackerId));
    SDL_Log("Server: player %u killed by %d  (K/D: %d/%d)", cl.id, attackerId, cl.kills, cl.deaths);
}

void GameServer::checkRespawns(float dt)
{
    for (auto& cl : clients) {
        if (!cl.connected || cl.respawnTimer < 0.0f)
            continue;
        cl.respawnTimer -= dt;
        if (cl.respawnTimer <= 0.0f) {
            spawnPlayer(cl);
            sendEvent(cl, EventType::Respawn);
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void GameServer::sendEvent(ClientSlot& cl, EventType evt, uint8_t p1, uint8_t p2)
{
    PktEvent pkt{};
    pkt.eventType = static_cast<uint8_t>(evt);
    pkt.param1 = p1;
    pkt.param2 = p2;
    cl.chan.send(&pkt, sizeof(pkt), PacketType::Event, 0xFF);
}

void GameServer::sendEventAll(EventType evt, uint8_t p1, uint8_t p2)
{
    for (auto& cl : clients) {
        if (cl.connected)
            sendEvent(cl, evt, p1, p2);
    }
}

ClientSlot* GameServer::findClient(const sockaddr_in& from)
{
    for (auto& cl : clients) {
        if (cl.connected && UdpSocket::addrEqual(cl.addr, from))
            return &cl;
    }
    return nullptr;
}

uint8_t GameServer::nextFreeSlot() const
{
    for (uint8_t i = 0; i < k_maxPlayers; ++i) {
        if (!clients[i].connected)
            return i;
    }
    return 0xFF;
}
