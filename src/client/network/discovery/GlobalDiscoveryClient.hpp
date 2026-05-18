/// @file GlobalDiscoveryClient.hpp
/// @brief Client-side global server browser and NAT-assist helper.

#pragma once

#include "network/NetworkConfig.hpp"
#include "network/discovery/GlobalDiscoveryProtocol.hpp"

#include <string>
#include <vector>

class GlobalDiscoveryClient
{
public:
    bool fetchServers(const GlobalDiscoveryConfig& cfg,
                      std::vector<net::discovery::ServerInfo>& outServers,
                      std::string& outError,
                      int timeoutMs = 1200);

    bool requestHolePunch(const GlobalDiscoveryConfig& cfg,
                          std::uint32_t serverId,
                          net::discovery::ServerInfo& outServer,
                          std::string& outError,
                          int timeoutMs);

    bool requestHolePunch(const GlobalDiscoveryConfig& cfg,
                          std::uint32_t serverId,
                          std::uint32_t clientNonce,
                          net::discovery::ServerInfo& outServer,
                          std::string& outError,
                          int timeoutMs);
};
