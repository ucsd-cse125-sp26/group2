#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "ecs/Components.hpp"
#include "game/Physics.hpp"
#include "game/Weapons.hpp"
#include "game/World.hpp"
#include "net/NetChannel.hpp"
#include "net/Protocol.hpp"
#include "net/Socket.hpp"
#include "renderer/GpuTypes.hpp"
#include "renderer/HUD.hpp"
#include "renderer/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr float k_mouseSensitivity = 0.0015f;
constexpr float k_fovDeg           = 100.0f;
constexpr float k_nearPlane        = 4.0f;
constexpr float k_farPlane         = 16000.0f;
constexpr glm::vec3 k_spawnPos     = {0.0f, 36.0f, 0.0f};

// ---------------------------------------------------------------------------
// Remote-player interpolation state (one per server slot)
// ---------------------------------------------------------------------------

struct RemotePlayer
{
    bool active    = false;
    glm::vec3 posA = {0.0f, 0.0f, 0.0f}; // older snapshot
    glm::vec3 posB = {0.0f, 0.0f, 0.0f}; // newer snapshot
    float yawA     = 0.0f;
    float yawB     = 0.0f;
    float lerpT    = 1.0f; // 0 = A, 1 = B
    float lerpDt   = 1.0f / k_snapshotHz;
    uint8_t health = 100;
    bool alive     = true;
};

// ---------------------------------------------------------------------------
// Client networking state
// ---------------------------------------------------------------------------

struct ClientNet
{
    UdpSocket sock;
    NetChannel chan;
    uint8_t myId        = 0xFF; // assigned by server
    bool connected      = false;
    uint32_t clientTick = 0;
    float sendTimer     = 0.0f;

    static constexpr float k_inputSendRate = 1.0f / 60.0f; // 60 Hz input
};

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct AppState
{
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu = nullptr;
    Renderer renderer;
    HUD hud;
    HudState hudState;
    World world;
    PhysicsConfig physicsCfg;
    entt::registry registry;
    entt::entity player = entt::null;

    bool mouseCaptured  = false;
    bool prevJump       = false;
    bool prevFire       = false;
    bool prevGrapple    = false;
    uint32_t frameCount = 0;
    Uint64 lastTick     = 0;

    // Networking (optional — nullptr = offline mode)
    ClientNet* net = nullptr;

    // Remote players (indexed by server slot 0-3)
    RemotePlayer remotes[4];

    // Client-side snapshot of own state (from server) for reconciliation
    bool hasServerState  = false;
    float serverPosError = 0.0f;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static glm::mat4 buildViewMatrix(const Transform& tf, const PlayerController& ctrl, const CameraAngles& cam)
{
    glm::vec3 eyePos = tf.position + glm::vec3(0.0f, ctrl.eyeHeight, 0.0f);
    float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
    float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
    glm::vec3 forward = {-sy * cp, sp, cy * cp};
    return glm::lookAt(eyePos, eyePos + forward, glm::vec3(0, 1, 0));
}

// Build a PktInput from the current player's input + physics state
static PktInput buildInputPacket(AppState* s, float /*dt*/)
{
    const auto& inp = s->registry.get<InputState>(s->player);
    const auto& cam = s->registry.get<CameraAngles>(s->player);

    PktInput pkt{};
    pkt.clientTick = s->net->clientTick;
    pkt.moveFwd    = inp.moveDir.y;
    pkt.moveRight  = inp.moveDir.x;
    pkt.yaw        = cam.yaw;
    pkt.pitch      = cam.pitch;
    pkt.buttons    = Buttons::build(inp.jumpPressed,
                                    inp.jumpHeld,
                                    inp.crouchHeld,
                                    inp.firePressed,
                                    inp.altFirePressed,
                                    inp.knifePressed,
                                    inp.grapplePressed);
    return pkt;
}

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    SDL_SetAppMetadata("Titandoom", "0.1.0", "com.titandoom");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* s   = new AppState();
    *appstate = s;

    // ---- GPU device ----
    s->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!s->gpu) {
        SDL_Log("GPU: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(s->gpu));

    // ---- Window ----
    constexpr int k_winW = 1280, k_winH = 720;
    s->window = SDL_CreateWindow("Titandoom", k_winW, k_winH, 0);
    if (!s->window) {
        SDL_Log("Window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowResizable(s->window, false);
    SDL_SetWindowMinimumSize(s->window, k_winW, k_winH);
    SDL_SetWindowMaximumSize(s->window, k_winW, k_winH);

    if (!SDL_ClaimWindowForGPUDevice(s->gpu, s->window)) {
        SDL_Log("ClaimWindow: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SyncWindow(s->window);

    // ---- World ----
    s->world = level::build();

    // ---- Renderer + HUD ----
    if (!s->renderer.init(s->gpu, s->window, s->world))
        return SDL_APP_FAILURE;
    if (!s->hud.init(s->gpu, s->window))
        SDL_Log("HUD: init failed (non-fatal, HUD disabled)");

    // ---- ECS: local player ----
    s->player = s->registry.create();
    s->registry.emplace<Transform>(s->player, Transform{k_spawnPos});
    s->registry.emplace<Velocity>(s->player);
    s->registry.emplace<CameraAngles>(s->player);
    s->registry.emplace<InputState>(s->player);
    s->registry.emplace<PlayerController>(s->player);
    s->registry.emplace<WallRun>(s->player);
    s->registry.emplace<GrappleHook>(s->player);
    s->registry.emplace<Health>(s->player);
    s->registry.emplace<WeaponState>(s->player);

    // ---- Optional: connect to server ----
    // Usage: titandoom <server-ip> [port]
    const char* serverIp = (argc >= 2) ? argv[1] : nullptr;
    if (serverIp) {
        uint16_t port = (argc >= 3) ? static_cast<uint16_t>(std::atoi(argv[2])) : k_serverPort;
        if (!UdpSocket::platformInit()) {
            SDL_Log("Client: socket init failed — offline mode");
        } else {
            s->net = new ClientNet();
            if (!s->net->sock.open()) {
                SDL_Log("Client: cannot open socket — offline mode");
                delete s->net;
                s->net = nullptr;
            } else {
                s->net->sock.setNonBlocking(true);
                sockaddr_in serverAddr = UdpSocket::makeAddr(serverIp, port);
                s->net->chan.init(&s->net->sock, &serverAddr);

                // Send connect request
                PktConnect conn{};
                std::strncpy(conn.name, "Player", sizeof(conn.name) - 1);
                s->net->chan.send(&conn, sizeof(conn), PacketType::Connect, 0xFF);
                SDL_Log("Client: connecting to %s:%u …", serverIp, port);
            }
        }
    }

    // ---- Capture mouse ----
    SDL_SetWindowRelativeMouseMode(s->window, true);
    s->mouseCaptured = true;

    s->lastTick = SDL_GetTicksNS();

    SDL_Log("Controls:");
    SDL_Log("  WASD = move   |  Mouse = look");
    SDL_Log("  Space = jump / bhop / walljump / wallrun");
    SDL_Log("  Shift-Space (in air, near wall) = wall run");
    SDL_Log("  E = grapple hook  |  LMB = fire  |  R = reload  |  F = knife");
    SDL_Log("  Esc = toggle mouse capture");
    SDL_Log("  Server IP as first argument for multiplayer");

    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* s = static_cast<AppState*>(appstate);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE) {
            s->mouseCaptured = !s->mouseCaptured;
            SDL_SetWindowRelativeMouseMode(s->window, s->mouseCaptured);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!s->mouseCaptured) {
            s->mouseCaptured = true;
            SDL_SetWindowRelativeMouseMode(s->window, true);
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (s->mouseCaptured) {
            auto& cam = s->registry.get<CameraAngles>(s->player);
            cam.yaw += event->motion.xrel * k_mouseSensitivity;
            cam.pitch = std::clamp(
                cam.pitch - event->motion.yrel * k_mouseSensitivity, -glm::radians(89.0f), glm::radians(89.0f));
        }
        break;

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* s = static_cast<AppState*>(appstate);

    // ---- Delta time ----
    Uint64 now  = SDL_GetTicksNS();
    float dt    = static_cast<float>(now - s->lastTick) * 1e-9f;
    dt          = std::min(dt, 0.033f);
    s->lastTick = now;

    // ---- Build input snapshot ----
    if (s->mouseCaptured) {
        auto& inp        = s->registry.get<InputState>(s->player);
        const bool* keys = SDL_GetKeyboardState(nullptr);

        float mx       = static_cast<float>((keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0));
        float mz       = static_cast<float>((keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0));
        float len      = std::sqrt(mx * mx + mz * mz);
        inp.moveDir    = (len > 0.001f) ? glm::vec2(mx / len, mz / len) : glm::vec2(0.0f);
        inp.mouseDelta = {0.0f, 0.0f};

        bool curJump    = keys[SDL_SCANCODE_SPACE] != 0;
        inp.jumpHeld    = curJump;
        inp.jumpPressed = curJump && !s->prevJump;
        s->prevJump     = curJump;
        inp.glideHeld   = curJump;
        inp.crouchHeld  = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];

        bool curFire       = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
        inp.firePressed    = curFire && !s->prevFire;
        inp.fireHeld       = curFire;
        s->prevFire        = curFire;
        inp.altFireHeld    = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) != 0;
        inp.altFirePressed = inp.altFireHeld;
        inp.knifePressed   = keys[SDL_SCANCODE_F] != 0;
        inp.reloadPressed  = keys[SDL_SCANCODE_R] != 0;

        bool curGrapple    = keys[SDL_SCANCODE_E] != 0;
        inp.grapplePressed = curGrapple && !s->prevGrapple;
        inp.grappleHeld    = curGrapple;
        s->prevGrapple     = curGrapple;
    }

    // ---- Local physics tick (client prediction) ----
    physicsUpdate(s->registry, s->world, s->physicsCfg, dt);

    // ---- Weapon tick (client-side for ammo display + visual feedback) ----
    {
        auto& ws  = s->registry.get<WeaponState>(s->player);
        auto& inp = s->registry.get<InputState>(s->player);
        weaponUpdate(ws, dt);
        bool shotFired = weaponTryFire(ws, inp.fireHeld, inp.firePressed, dt);
        if (inp.reloadPressed)
            weaponTryReload(ws);

        // Visual shot feedback: muzzle flash + client-side hitscan impact marker
        if (shotFired) {
            s->hudState.muzzleFlashTimer = 0.07f;

            const auto& tf   = s->registry.get<Transform>(s->player);
            const auto& ctrl = s->registry.get<PlayerController>(s->player);
            const auto& cam  = s->registry.get<CameraAngles>(s->player);

            glm::vec3 eyePos = tf.position + glm::vec3(0.0f, ctrl.eyeHeight, 0.0f);
            float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
            float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
            glm::vec3 aimDir = glm::normalize(glm::vec3(-sy * cp, sp, cy * cp));

            // Collect remote player capsules for client-side hit preview
            const float k_capsR = 20.0f, k_capsHH = 50.0f;
            CapsuleInfo others[4];
            int otherCount = 0;
            int myId       = s->net ? s->net->myId : 0;
            for (int i = 0; i < 4 && otherCount < 4; ++i) {
                if (i == myId)
                    continue;
                const auto& rp = s->remotes[i];
                if (!rp.active || !rp.alive)
                    continue;
                glm::vec3 pos        = glm::mix(rp.posA, rp.posB, rp.lerpT);
                others[otherCount++] = {i, pos + glm::vec3(0, k_capsHH, 0), k_capsR, k_capsHH};
            }

            const auto& stats = k_weaponStats[static_cast<int>(ws.active)];
            if (stats.range > 0.0f) {
                HitResult hr = hitscanFire(eyePos, aimDir, stats, s->world, others, otherCount);
                if (hr.hit)
                    s->renderer.addImpact(hr.point, hr.normal);
            }
        }

        // Decay muzzle flash
        if (s->hudState.muzzleFlashTimer > 0.0f)
            s->hudState.muzzleFlashTimer = std::max(0.0f, s->hudState.muzzleFlashTimer - dt);

        // Update HUD state from weapon
        s->hudState.ammo      = ws.ammo;
        s->hudState.reserve   = ws.reserve;
        s->hudState.reloading = ws.reloading;
        s->hudState.reloadFrac =
            ws.reloading ? (1.0f - ws.reload / k_weaponStats[static_cast<int>(ws.active)].reloadTime) : 0.0f;
    }

    // ---- Update HUD state from player ----
    {
        auto& hp              = s->registry.get<Health>(s->player);
        s->hudState.health    = hp.current;
        s->hudState.maxHealth = hp.max;
        s->hudState.armor     = hp.armor;

        auto* gh                  = s->registry.try_get<GrappleHook>(s->player);
        s->hudState.grappleActive = gh && gh->state != GrappleHook::State::Idle;

        if (s->hudState.hitMarkerTimer > 0.0f)
            s->hudState.hitMarkerTimer -= dt;
    }

    // ---- Networking ----
    if (s->net) {
        // Receive packets
        uint8_t buf[k_maxPacketBytes];
        sockaddr_in from{};
        int n;
        while ((n = s->net->sock.recvFrom(buf, sizeof(buf), from)) > 0) {
            if (n < static_cast<int>(sizeof(PacketHeader)))
                continue;
            auto* hdr = reinterpret_cast<PacketHeader*>(buf);
            if (hdr->magic != k_magic)
                continue;

            auto type = static_cast<PacketType>(hdr->type);

            if (type == PacketType::ConnectAck && n >= static_cast<int>(sizeof(PktConnectAck))) {
                auto* ack         = reinterpret_cast<PktConnectAck*>(buf);
                s->net->myId      = ack->clientId;
                s->net->connected = true;
                SDL_Log("Client: connected! Assigned slot %u (server tick %u)", ack->clientId, ack->serverTick);
            } else if (type == PacketType::Snapshot && n >= static_cast<int>(sizeof(PacketHeader) + 8)) {
                auto* snap = reinterpret_cast<PktSnapshot*>(buf);
                for (int i = 0; i < snap->playerCount; ++i) {
                    const auto& ps = snap->players[i];
                    if (ps.id >= 4)
                        continue;

                    if (ps.id == s->net->myId) {
                        // Reconcile own position
                        auto& tf = s->registry.get<Transform>(s->player);
                        glm::vec3 serverPos{ps.posX, ps.posY, ps.posZ};
                        float err = glm::length(serverPos - tf.position);
                        if (err > 64.0f) {
                            // Large error: hard correction
                            tf.position = serverPos;
                        }
                        auto& hp   = s->registry.get<Health>(s->player);
                        hp.current = ps.health;
                    } else {
                        // Update remote player interpolation buffer
                        auto& rp  = s->remotes[ps.id];
                        rp.posA   = rp.posB;
                        rp.yawA   = rp.yawB;
                        rp.posB   = {ps.posX, ps.posY, ps.posZ};
                        rp.yawB   = ps.yaw;
                        rp.lerpT  = 0.0f;
                        rp.lerpDt = 1.0f / k_snapshotHz;
                        rp.health = ps.health;
                        rp.alive  = ps.alive();
                        rp.active = true;
                    }
                }
            } else if (type == PacketType::Event && n >= static_cast<int>(sizeof(PktEvent))) {
                auto* evt    = reinterpret_cast<PktEvent*>(buf);
                auto evtType = static_cast<EventType>(evt->eventType);
                if (evtType == EventType::HitConfirm) {
                    s->hudState.hitMarkerTimer = 0.2f;
                    s->hudState.killedSomeone  = false;
                } else if (evtType == EventType::Killed) {
                    s->hudState.hitMarkerTimer = 0.5f;
                    s->hudState.killedSomeone  = true;
                } else if (evtType == EventType::Respawn) {
                    auto& tf    = s->registry.get<Transform>(s->player);
                    tf.position = k_spawnPos;
                    auto& vel   = s->registry.get<Velocity>(s->player);
                    vel.linear  = {0.0f, 0.0f, 0.0f};
                    auto& hp    = s->registry.get<Health>(s->player);
                    hp.current  = hp.max;
                }
            } else if (type == PacketType::Kick) {
                SDL_Log("Client: kicked by server (full or banned)");
                s->net->connected = false;
            }
        }

        // Advance interpolation
        for (auto& rp : s->remotes) {
            if (!rp.active)
                continue;
            rp.lerpT = std::min(rp.lerpT + dt / rp.lerpDt, 1.0f);
        }

        // Send input to server
        s->net->clientTick++;
        s->net->sendTimer += dt;
        if (s->net->connected && s->net->sendTimer >= ClientNet::k_inputSendRate) {
            s->net->sendTimer = 0.0f;
            PktInput pkt      = buildInputPacket(s, dt);
            s->net->chan.send(&pkt, sizeof(pkt), PacketType::Input, s->net->myId);
        } else if (!s->net->connected) {
            // Retry connect every 0.5s
            s->net->sendTimer += dt;
            if (s->net->sendTimer > 0.5f) {
                s->net->sendTimer = 0.0f;
                PktConnect conn{};
                std::strncpy(conn.name, "Player", sizeof(conn.name) - 1);
                s->net->chan.send(&conn, sizeof(conn), PacketType::Connect, 0xFF);
            }
        }
    }

    // ---- Debug title ----
    {
        const auto& vel  = s->registry.get<Velocity>(s->player);
        const auto& ctrl = s->registry.get<PlayerController>(s->player);
        auto* wr         = s->registry.try_get<WallRun>(s->player);
        auto* gh         = s->registry.try_get<GrappleHook>(s->player);
        float hspd       = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
        char title[192];
        std::snprintf(title,
                      sizeof(title),
                      "Titandoom | %.0f qu/s | %s%s%s%s%s%s",
                      static_cast<double>(hspd),
                      ctrl.onGround ? "GND " : "AIR ",
                      ctrl.onWall ? "WALL " : "",
                      ctrl.onSurf ? "SURF " : "",
                      ctrl.gliding ? "GLIDE " : "",
                      (wr && wr->active) ? "WRUN " : "",
                      (gh && gh->state == GrappleHook::State::Attached) ? "GRAPPLE " : "");
        SDL_SetWindowTitle(s->window, title);
    }

    // ---- Render ----
    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(s->gpu);
    if (!cmdbuf)
        return SDL_APP_CONTINUE;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 swW = 0, swH = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, s->window, &swapchain, &swW, &swH)) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        return SDL_APP_CONTINUE;
    }

    if (swapchain && swW > 0 && swH > 0) {
        s->frameCount++;
        s->renderer.onResize(swW, swH);

        const auto& tf   = s->registry.get<Transform>(s->player);
        const auto& ctrl = s->registry.get<PlayerController>(s->player);
        const auto& cam  = s->registry.get<CameraAngles>(s->player);

        glm::mat4 view = buildViewMatrix(tf, ctrl, cam);
        glm::mat4 proj = glm::perspective(
            glm::radians(k_fovDeg), static_cast<float>(swW) / static_cast<float>(swH), k_nearPlane, k_farPlane);

        SceneUniforms uniforms;
        uniforms.viewProj = proj * view;
        uniforms.model    = glm::mat4(1.0f);

        // Collect remote player positions for rendering
        glm::vec3 positions[4] = {};
        float yaws[4]          = {};
        bool alive[4]          = {};

        // Local player slot
        int myId = s->net ? s->net->myId : 0;
        if (myId < 4) {
            positions[myId] = tf.position;
            yaws[myId]      = cam.yaw;
            alive[myId]     = true;
        }

        // Remote players (interpolated)
        for (int i = 0; i < 4; ++i) {
            if (i == myId)
                continue;
            const auto& rp = s->remotes[i];
            if (!rp.active || !rp.alive)
                continue;
            positions[i] = glm::mix(rp.posA, rp.posB, rp.lerpT);
            yaws[i]      = rp.yawA + (rp.yawB - rp.yawA) * rp.lerpT;
            alive[i]     = true;
        }

        // Only skip own model in first-person (myId = 0 in offline mode)
        int skipSelf = (s->net && s->net->connected) ? myId : 0;

        // Age impact markers before the draw (so upload sees current lifetimes)
        s->renderer.tickImpacts(dt);

        s->renderer.drawScene(cmdbuf, swapchain, uniforms, positions, yaws, alive, skipSelf);

        // HUD overlay
        s->hud.draw(cmdbuf, swapchain, swW, swH, s->hudState);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* s = static_cast<AppState*>(appstate);
    if (!s)
        return;

    // Graceful disconnect
    if (s->net && s->net->connected) {
        PacketHeader disc{};
        disc.magic    = k_magic;
        disc.clientId = s->net->myId;
        disc.type     = static_cast<uint8_t>(PacketType::Disconnect);
        s->net->chan.send(&disc, sizeof(disc), PacketType::Disconnect, s->net->myId);
    }
    if (s->net) {
        s->net->sock.close();
        UdpSocket::platformShutdown();
        delete s->net;
    }

    s->hud.destroy();
    s->renderer.destroy();
    if (s->gpu && s->window)
        SDL_ReleaseWindowFromGPUDevice(s->gpu, s->window);
    SDL_DestroyWindow(s->window);
    SDL_DestroyGPUDevice(s->gpu);
    delete s;
    SDL_Quit();
}
