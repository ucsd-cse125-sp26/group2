/// @file DiscoverySettings.hpp
/// @brief Runtime host-controlled discovery advertisement settings.

#pragma once

/// @brief Host-managed discovery visibility flags.
///
/// Used both as the client-to-server runtime update payload and as local UI
/// state for the host screen's unsaved-settings tracking.
struct DiscoverySettings
{
    bool advertiseGlobal = true; ///< True to publish this server to the global directory.
    bool advertiseLan = true;    ///< True to respond to LAN discovery requests.
};
