/// @file GlobalServerAdvertiser.hpp
/// @brief Server-side publisher for the global directory service.

#pragma once

#include "network/NetworkConfig.hpp"
#include "network/discovery/GlobalDiscoveryProtocol.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class GlobalServerAdvertiser
{
public:
    bool start(const GlobalDiscoveryConfig& cfg, Uint16 gamePort, std::function<int()> currentPlayersFn);
    void stop();
    [[nodiscard]] std::uint32_t serverId() const noexcept { return serverId_.load(std::memory_order_relaxed); }

private:
    void loop();
    bool sendRegistration(net::discovery::DirectoryMessage kind);
    void sendUdpHello();
    void pollPunchRequests();
    void sendPunchProbes(const net::discovery::UdpPunchPeer& peer);

    GlobalDiscoveryConfig cfg_;
    Uint16 gamePort_ = 0;
    std::function<int()> currentPlayersFn_;
    net::UdpEndpoint udpEndpoint_;
    std::thread thread_;
    std::atomic<bool> shouldStop_{false};
    std::atomic<std::uint32_t> serverId_{0};
};
