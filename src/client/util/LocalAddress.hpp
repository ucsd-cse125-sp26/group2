/// @file LocalAddress.hpp
/// @brief Helpers for displaying local network addresses in client UI.

#pragma once

#include <string>

namespace local_address
{

/// @brief Return the first active non-loopback LAN IPv4 address, or 127.0.0.1 if none is found.
std::string firstLanIPv4();

} // namespace local_address
