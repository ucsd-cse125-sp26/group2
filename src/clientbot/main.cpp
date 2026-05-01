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
#include "network/NetworkConfig.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
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
    struct sigaction sa{};
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
                 "All bots are killed on Ctrl+C (SIGINT) or SIGTERM.\n",
                 argv0);
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

    // ── Wait for shutdown signal ─────────────────────────────────────────
    while (!g_stopFlag.load(std::memory_order_relaxed)) {
        SDL_Delay(100);
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
