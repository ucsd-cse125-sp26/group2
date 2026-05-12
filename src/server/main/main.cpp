/// @file main.cpp
/// @brief Server application entry point.

#include "DeveloperConfig.hpp"
#include "game/ServerGame.hpp"
#include "network/DiscoveryServer.hpp"
#include "network/NetworkConfig.hpp"
#include "network/Server.hpp"
#include "perf/Parallel.hpp"
#include "perf/Profiler.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

/// CSV file (when `GROUP2_SERVER_PROFILE_CSV=path` is set). One row
/// per scope per aggregator window plus a synthetic `__tick__` row
/// for the per-tick wall-clock summary. Held open for the process
/// lifetime; flushed after every write so `tail -f` sees fresh data.
std::FILE* csvFile = nullptr;

/// Returns true on the first call and false thereafter. Used to gate
/// the CSV header row to a single emit.
bool firstCall()
{
    static bool first = true;
    if (first) {
        first = false;
        return true;
    }
    return false;
}

/// Format and emit one snapshot to SDL_Log + (optionally) the CSV
/// file. Called on the aggregator thread once per second.
void emitSnapshot(const ::group2::perf::Snapshot& snap)
{
    using ::group2::perf::Snapshot;

    // ── Sort scopes by p99 desc (top of log line is the worst). ──
    // We index into `snap.scopes[0..scopeNum)` via a small index array
    // — keeps the snapshot itself read-only.
    std::array<std::size_t, ::group2::perf::k_maxScopes> idx{};
    for (std::size_t i = 0; i < snap.scopeNum; ++i)
        idx[i] = i;
    std::sort(idx.begin(),
              idx.begin() + static_cast<std::ptrdiff_t>(snap.scopeNum),
              [&snap](std::size_t a, std::size_t b) { return snap.scopes[a].p99Ns > snap.scopes[b].p99Ns; });

    // ── Single-line log: top-N scopes. Truncate to keep one log
    //    line manageable; full data goes to the CSV.
    // Local constants are bare camelBack per .clang-tidy.
    constexpr int topN = 8; // NOLINT(readability-magic-numbers) — log line width budget
    char buf[1024];
    int off = 0;
    off += std::snprintf(buf + off,
                         sizeof(buf) - static_cast<size_t>(off),
                         "[perf clients=%u tickN=%" PRIu64 " tickP99=%.2fms tickMax=%.2fms net=%.1fKB/s in=%.1fKB/s "
                         "snaps/s=%" PRIu64 " backlog=%u]",
                         snap.clientCount,
                         snap.tickCount,
                         static_cast<double>(snap.tickP99Ns) / 1e6,
                         static_cast<double>(snap.tickMaxNs) / 1e6,
                         static_cast<double>(snap.bytesSent) / 1024.0,
                         static_cast<double>(snap.bytesRecv) / 1024.0,
                         snap.snapshotsSent,
                         snap.peakBacklog);

    const int top = static_cast<int>(std::min<std::size_t>(snap.scopeNum, static_cast<std::size_t>(topN)));
    for (int k = 0; k < top && off < static_cast<int>(sizeof(buf)) - 1; ++k) {
        const auto& s = snap.scopes[idx[static_cast<std::size_t>(k)]];
        if (s.count == 0)
            continue;
        off += std::snprintf(buf + off,
                             sizeof(buf) - static_cast<size_t>(off),
                             "  %s p50=%.2f p99=%.2f max=%.2f n=%" PRIu64 "",
                             s.name,
                             static_cast<double>(s.p50Ns) / 1e6,
                             static_cast<double>(s.p99Ns) / 1e6,
                             static_cast<double>(s.maxNs) / 1e6,
                             s.count);
    }
    SDL_Log("%s", buf);

    // ── CSV (optional). ────────────────────────────────────────────
    if (csvFile != nullptr) {
        if (firstCall()) {
            std::fprintf(csvFile, "t_unix_ms,system,count,min_ns,p50_ns,p99_ns,max_ns,mean_ns,clients\n");
        }
        // Per-tick row (synthetic system "__tick__").
        std::fprintf(csvFile,
                     "%" PRIu64 ",__tick__,%" PRIu64 ",0,0,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u\n",
                     snap.windowEndMs,
                     snap.tickCount,
                     snap.tickP99Ns,
                     snap.tickMaxNs,
                     (snap.tickCount == 0) ? 0 : (snap.tickSumNs / snap.tickCount),
                     snap.clientCount);
        // Per-scope rows.
        for (std::size_t i = 0; i < snap.scopeNum; ++i) {
            const auto& s = snap.scopes[i];
            if (s.count == 0)
                continue;
            std::fprintf(csvFile,
                         "%" PRIu64 ",%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u\n",
                         snap.windowEndMs,
                         s.name,
                         s.count,
                         s.minNs,
                         s.p50Ns,
                         s.p99Ns,
                         s.maxNs,
                         s.meanNs,
                         snap.clientCount);
        }
        // Network row (synthetic system "__net__").
        std::fprintf(csvFile,
                     "%" PRIu64 ",__net__,%" PRIu64 ",0,0,%" PRIu64 ",%u,%" PRIu64 ",%u\n",
                     snap.windowEndMs,
                     snap.snapshotsSent,
                     snap.bytesSent,
                     snap.peakBacklog,
                     snap.bytesRecv,
                     snap.clientCount);
        std::fflush(csvFile);
    }
}

void openCsvIfRequested()
{
    const char* path = std::getenv("GROUP2_SERVER_PROFILE_CSV");
    if (path == nullptr || path[0] == '\0')
        return;
    csvFile = std::fopen(path, "w");
    if (csvFile == nullptr) {
        SDL_Log("[perf] WARNING: cannot open CSV at '%s' (errno=%d)", path, errno);
        return;
    }
    SDL_Log("[perf] CSV output → %s", path);
}

void closeCsv()
{
    if (csvFile != nullptr) {
        std::fclose(csvFile);
        csvFile = nullptr;
    }
}

} // namespace

/// @brief Server entry point -- initialises SDL/NET, runs the game loop, and cleans up.
int main()
{
    SDL_Init(0);
    NET_Init();

    // PR-1: profiler. `initFromEnv()` reads `GROUP2_SERVER_PROFILE` and
    // sets the runtime-enabled flag. The aggregator is started
    // unconditionally so toggling the env var at runtime (via SIGUSR or
    // a future config-reload path) doesn't require restarting the
    // thread; it sleeps cheaply when sampling is off.
    ::group2::perf::initFromEnv();
    ::group2::perf::initParallelFromEnv();
    openCsvIfRequested();
    ::group2::perf::startAggregator(emitSnapshot);

    const char* base = SDL_GetBasePath();
    std::string cfgPath = std::string(base ? base : "") + "config.toml";
    const NetworkConfig cfg = loadNetworkConfig(cfgPath.c_str());
    const DeveloperConfig developerCfg = loadDeveloperConfig(cfgPath.c_str());
    const NetworkAddress& serverNet = cfg.serverNetwork;

    Server server;
    if (!server.init(serverNet.host.c_str(), serverNet.port, cfg.transport, cfg.discovery)) {
        ::group2::perf::stopAggregator();
        closeCsv();
        NET_Quit();
        SDL_Quit();
        return 1;
    }

    // start server discovery system
    // TODO: random port
    DiscoveryServer discoveryServer;
    const DiscoveryServer::ServerInfo serverInfo{
        .serverName = "Test Server",
        .gamePort = serverNet.port,
        .currentPlayers = 0,
    };
    discoveryServer.start(9998, serverInfo);

    ServerGame game;
    if (!game.init(server, /*tickRateHz*/ 128, cfg.serverRep.snapshotHz, developerCfg.skipLobby)) {
        server.shutdown();
        ::group2::perf::stopAggregator();
        closeCsv();
        discoveryServer.stop();
        NET_Quit();
        SDL_Quit();
        return 1;
    }

    game.run();
    game.shutdown();
    server.shutdown();

    ::group2::perf::stopAggregator();
    closeCsv();

    discoveryServer.stop();

    NET_Quit();
    SDL_Quit();
    return 0;
}
