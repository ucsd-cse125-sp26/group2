/// @file main.cpp
/// @brief Multi-bot load-test launcher.
///
/// Usage:
///     ./clientbot              # 1 bot, defaults to config.toml host:port
///     ./clientbot 10           # 10 bots
///     ./clientbot 50 host:port # 50 bots, override host:port
///
/// On Ctrl+C (SIGINT) or SIGTERM, all bots are signalled to stop and the
/// process waits for them to finish their current tick before exiting.
///
/// All bots run in a single process — no fork, no GPU. One std::thread per
/// bot, sharing the SDL_net global state. Each bot owns its own TCP socket
/// + Registry; nothing else is shared.

#include "Bot.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/MapConfig.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/physics/WorldData.hpp"
#include "network/NetworkConfig.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

/// @brief Process-wide stop flag; flipped by SIGINT/SIGTERM handler.
///
/// std::atomic with default sequential consistency is safe to set from a
/// signal handler on Linux for std::atomic_bool / lock-free types.
std::atomic<bool> g_stopFlag{false};

extern "C" void onSignal(int /*sig*/)
{
    // Async-signal-safe: just set the flag. All cleanup happens on the
    // main thread after the flag is observed.
    g_stopFlag.store(true, std::memory_order_relaxed);
}

void installSignalHandlers()
{
    // `= {}` value-initialises every field. Same effect as `sa{};` but
    // formats identically under clang-format-18 and clang-format-22 —
    // the v18 layout-disagreement with the brace-init-only form was the
    // sole reason this file failed the PR CI's clang-format-18 gate.
    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART; let blocking calls return EINTR if any
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

/// @brief Parse "host:port" into separate fields.
/// @return False if the format is malformed.
bool parseHostPort(const std::string& spec, std::string& host, Uint16& port)
{
    const auto colon = spec.find(':');
    if (colon == std::string::npos)
        return false;
    host = spec.substr(0, colon);
    const auto portStr = spec.substr(colon + 1);
    char* end = nullptr;
    const long p = std::strtol(portStr.c_str(), &end, 10);
    if (end == portStr.c_str() || *end != '\0' || p <= 0 || p > 65535)
        return false;
    port = static_cast<Uint16>(p);
    return true;
}

void printUsage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [<numBots>] [<host>:<port>]\n"
                 "  numBots      Number of bots to launch concurrently (default: 1)\n"
                 "  host:port    Override the server address from config.toml\n"
                 "\n"
                 "All bots are killed on Ctrl+C (SIGINT) or SIGTERM.\n"
                 "\n"
                 "Fleet RTT aggregator:\n"
                 "  GROUP2_BOT_FLEET_RTT=1     Log fleet-wide RTT p50/p99/max once per second.\n"
                 "  GROUP2_BOT_FLEET_RTT_CSV=path  Also write CSV rows to `path`.\n",
                 argv0);
}

/// PR-1 (server-perf): once-per-second fleet RTT aggregator.
///
/// Walks every connected bot, samples its smoothed RTT from
/// `Bot::getCurrentRttMs()`, computes p50/p99/max, and emits one
/// log line. Optional CSV output keyed off `GROUP2_BOT_FLEET_RTT_CSV`.
///
/// We sample on the main thread via a sleep loop instead of spinning
/// up a dedicated std::thread because the main thread is otherwise
/// idle (just waiting on the stop flag) and the sampling cost is
/// dominated by the SDL_Log syscall, not the per-bot read.
void runFleetRttAggregator(const std::vector<std::unique_ptr<Bot>>& bots, const std::atomic<bool>& stopFlag)
{
    const char* csvPath = std::getenv("GROUP2_BOT_FLEET_RTT_CSV");
    std::FILE* csv = nullptr;
    if (csvPath != nullptr && csvPath[0] != '\0') {
        csv = std::fopen(csvPath, "w");
        if (csv != nullptr) {
            std::fprintf(csv, "t_unix_ms,bot_count,bot_ready,p50_ms,p99_ms,max_ms,mean_ms\n");
        } else {
            SDL_Log("[fleet] WARNING: cannot open CSV at '%s'", csvPath);
        }
    }

    std::vector<float> rtts;
    rtts.reserve(bots.size());

    while (!stopFlag.load(std::memory_order_relaxed)) {
        // Sleep in short slices so SIGINT-triggered stop is observed
        // within ~100 ms instead of waiting out a full 1-second slumber.
        // Without this, the loadtest harness's `kill -INT` would block
        // for up to a second per bot's outstanding sleep.
        for (int i = 0; i < 10 && !stopFlag.load(std::memory_order_relaxed); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stopFlag.load(std::memory_order_relaxed))
            break;

        rtts.clear();
        for (const auto& bot : bots) {
            if (!bot->isReady())
                continue;
            const float r = bot->getCurrentRttMs();
            if (r > 0.0f && r < 10'000.0f) // filter pre-PONG sentinel + obvious outliers
                rtts.push_back(r);
        }

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

        if (rtts.empty()) {
            SDL_Log("[fleet rtt] N=%zu of %zu (warming up — waiting for first PONGs)", rtts.size(), bots.size());
            if (csv != nullptr) {
                std::fprintf(csv, "%lld,%zu,0,0,0,0,0\n", static_cast<long long>(ts), bots.size());
                std::fflush(csv);
            }
            continue;
        }

        std::sort(rtts.begin(), rtts.end());
        const std::size_t n = rtts.size();
        const float p50 = rtts[n / 2];
        const float p99 = rtts[std::min(n - 1, static_cast<std::size_t>(static_cast<double>(n) * 0.99))];
        const float max = rtts.back();
        float sum = 0.0f;
        for (float r : rtts)
            sum += r;
        const float mean = sum / static_cast<float>(n);

        SDL_Log("[fleet rtt] N=%zu of %zu  p50=%.2fms p99=%.2fms max=%.2fms mean=%.2fms",
                n,
                bots.size(),
                static_cast<double>(p50),
                static_cast<double>(p99),
                static_cast<double>(max),
                static_cast<double>(mean));

        if (csv != nullptr) {
            std::fprintf(csv,
                         "%lld,%zu,%zu,%.4f,%.4f,%.4f,%.4f\n",
                         static_cast<long long>(ts),
                         bots.size(),
                         n,
                         static_cast<double>(p50),
                         static_cast<double>(p99),
                         static_cast<double>(max),
                         static_cast<double>(mean));
            std::fflush(csv);
        }
    }

    if (csv != nullptr)
        std::fclose(csv);
}

} // namespace

int main(int argc, char* argv[])
{
    // ── Parse args ────────────────────────────────────────────────────────
    int numBots = 1;
    std::string hostOverride;
    Uint16 portOverride = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }

        // First non-flag arg: bot count. Second: host:port.
        char* end = nullptr;
        const long n = std::strtol(arg.c_str(), &end, 10);
        if (*end == '\0') {
            if (n < 1 || n > 10000) {
                std::fprintf(stderr, "Error: numBots out of range (1..10000): %ld\n", n);
                return 1;
            }
            numBots = static_cast<int>(n);
        } else if (parseHostPort(arg, hostOverride, portOverride)) {
            // ok
        } else {
            std::fprintf(stderr, "Error: cannot parse argument '%s'\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    // ── SDL + SDL_net init ────────────────────────────────────────────────
    SDL_Init(0); // no video / audio subsystems
    if (!NET_Init()) {
        SDL_Log("NET_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // ── Resolve target host/port ──────────────────────────────────────────
    //
    // Priority: explicit CLI override > config.toml > built-in default. We
    // re-use the existing NetworkConfig loader so the bot speaks the same
    // address as a real client by default.
    const char* base = SDL_GetBasePath();
    const std::string cfgPath = std::string(base ? base : "") + "config.toml";
    const NetworkConfig cfg = loadNetworkConfig(cfgPath.c_str());

    const std::string host = hostOverride.empty() ? cfg.serverNetwork.host : hostOverride;
    const Uint16 port = portOverride != 0 ? portOverride : cfg.serverNetwork.port;

    SDL_Log("[clientbot] launching %d bot(s) → %s:%u", numBots, host.c_str(), port);

    // ── PR-23: load map collision once for all bots ──────────────────────
    //
    // Client-side prediction needs world geometry — `runMovement` +
    // `runCollision` raycast against `physics::activeWorld()`.  This is
    // a Meyer's-singleton, so loading once here populates it for every
    // bot in the process.
    //
    // PR-30 (post-merge): use the same `gamemap::loadConfiguredMap`
    // helper the client and server now share (introduced in main's
    // `ecs/MapConfig.hpp` refactor).  Prediction parity needs the bot
    // to extract IDENTICAL collision primitives — going through the
    // shared helper makes that automatic; any future change to map-
    // load options propagates here unchanged.
    //
    // CRITICAL lifetime: `WorldGeometry` holds `std::span`s into the
    // vectors inside `MapCollisionData`, NOT copies.  `setActiveWorld`
    // stashes those spans verbatim in the singleton, so `mapCollision`
    // MUST outlive every `physics::activeWorld()` access — which
    // means it has to live for the whole process.  Hoist into `main`
    // scope (not into a `{}` block) so it survives until process exit.
    physics::MapCollisionData mapCollision;
    {
        gamemap::loadConfiguredMap(mapCollision, "clientbot");
        // PR-30: V-HACD gated on `gamemap::k_useVhacd`.  See the comment
        // in `MapConfig.hpp` for the rationale.
        const std::string assetsDir = std::string(base ? base : "") + "assets/";
        for (const AssetDefinition& def : kPropAssets) {
            const bool decompose = def.decomposeCollision && gamemap::k_useVhacd;
            physics::loadPropCollision(
                assetsDir + def.filename, mapCollision, def.loadTranslation, def.loadScale, decompose);
        }
        physics::setActiveWorld(mapCollision.geometry());
    }

    // Optional latency simulator hook for testing Phase 6 lag-comp under
    // bot-only load. Each bot applies `GROUP2_BOT_LATENCY_MS` ms of
    // simulated round-trip delay on its UDP path. Defaults to 0 (off).
    int botLatencyMs = 0;
    if (const char* envLat = std::getenv("GROUP2_BOT_LATENCY_MS")) {
        char* end = nullptr;
        const long n = std::strtol(envLat, &end, 10);
        if (*end == '\0' && n >= 0 && n <= 200) {
            botLatencyMs = static_cast<int>(n);
            SDL_Log("[clientbot] applying simulated %d ms RTT to all bots (GROUP2_BOT_LATENCY_MS)", botLatencyMs);
        } else {
            SDL_Log("[clientbot] ignoring invalid GROUP2_BOT_LATENCY_MS='%s' (need 0..200)", envLat);
        }
    }

    // Same for simulated packet loss. Each bot drops both directions
    // independently at this rate. Defaults to 0 (off).
    int botLossPct = 0;
    if (const char* envLoss = std::getenv("GROUP2_BOT_LOSS_PCT")) {
        char* end = nullptr;
        const long n = std::strtol(envLoss, &end, 10);
        if (*end == '\0' && n >= 0 && n <= 100) {
            botLossPct = static_cast<int>(n);
            SDL_Log("[clientbot] applying simulated %d %% UDP loss to all bots (GROUP2_BOT_LOSS_PCT)", botLossPct);
        } else {
            SDL_Log("[clientbot] ignoring invalid GROUP2_BOT_LOSS_PCT='%s' (need 0..100)", envLoss);
        }
    }

    installSignalHandlers();

    // ── Spawn bots ────────────────────────────────────────────────────────
    //
    // We create-and-init each bot serially before starting any threads.
    // That way handshake failures are reported deterministically and we
    // don't leak partially-running bot threads if connection setup fails
    // halfway through.
    //
    // Bot is non-movable / non-copyable (owns a std::thread + TCP socket
    // via Client) so we hold them via unique_ptr in a vector. A pointer
    // indirection per bot is cheap and gives stable references.
    std::vector<std::unique_ptr<Bot>> bots;
    bots.reserve(static_cast<size_t>(numBots));

    int connected = 0;
    for (int i = 0; i < numBots; ++i) {
        auto bot = std::make_unique<Bot>();
        if (!bot->init(host, port, i)) {
            // Don't abort — partial fleet is still a useful load test signal
            // ("first 47 of 50 connected, then refused") and we still want
            // to clean up the ones that did connect.
            continue;
        }
        if (botLatencyMs > 0)
            bot->setSimulatedLatencyMs(botLatencyMs);
        if (botLossPct > 0)
            bot->setSimulatedLossPercent(botLossPct);
        bots.push_back(std::move(bot));
        ++connected;

        if (g_stopFlag.load(std::memory_order_relaxed)) {
            SDL_Log("[clientbot] stop flag observed during connect phase; aborting startup");
            break;
        }
    }

    SDL_Log("[clientbot] %d/%d bots connected; starting tick loops", connected, numBots);

    for (auto& bot : bots) {
        bot->start(g_stopFlag);
    }

    // ── Wait for shutdown signal (with optional fleet aggregator) ────────
    //
    // PR-1 (server-perf): when GROUP2_BOT_FLEET_RTT=1, the main thread
    // doubles as the aggregator. Otherwise it just sleeps on the stop
    // flag the way it always did.
    const char* fleetEnv = std::getenv("GROUP2_BOT_FLEET_RTT");
    const bool fleetOn = (fleetEnv != nullptr) && fleetEnv[0] != '\0' && fleetEnv[0] != '0';
    if (fleetOn) {
        SDL_Log("[clientbot] fleet RTT aggregator enabled (1 Hz)");
        runFleetRttAggregator(bots, g_stopFlag);
    } else {
        while (!g_stopFlag.load(std::memory_order_relaxed)) {
            SDL_Delay(100);
        }
    }

    SDL_Log("[clientbot] shutdown signalled; waiting for %zu bots to finish", bots.size());

    // ── Join all bot threads ──────────────────────────────────────────────
    //
    // Each bot's runLoop checks stopFlag at the top of every iteration and
    // also during the spin-wait phase, so worst-case wait per bot is one
    // tick (~7.8 ms at 128 Hz). With 100 bots joining sequentially we may
    // wait up to ~800 ms total — fine for a load-test tool.
    for (auto& bot : bots) {
        bot->join();
    }

    bots.clear(); // explicit cleanup before NET/SDL teardown

    NET_Quit();
    SDL_Quit();
    SDL_Log("[clientbot] exit");
    return 0;
}
