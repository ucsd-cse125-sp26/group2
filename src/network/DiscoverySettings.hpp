/// @file DiscoverySettings.hpp
/// @brief Runtime host-controlled discovery advertisement settings.

#pragma once

/// @brief Host-managed discovery visibility flags.
struct DiscoverySettings
{
    bool advertiseGlobal = true; ///< True to publish this server to the global directory.
    bool advertiseLan = true;    ///< True to respond to LAN discovery requests.
};
