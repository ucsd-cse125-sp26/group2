/// @file LocalAddress.cpp
/// @brief Platform helpers for selecting a displayable local LAN IPv4 address.

#include "LocalAddress.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace local_address
{

std::string firstLanIPv4()
{
#if defined(_WIN32)
    return "127.0.0.1";
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0 || interfaces == nullptr) {
        return "127.0.0.1";
    }

    std::string result = "127.0.0.1";
    for (ifaddrs* iface = interfaces; iface != nullptr; iface = iface->ifa_next) {
        if (iface->ifa_addr == nullptr || iface->ifa_addr->sa_family != AF_INET)
            continue;

        const unsigned int flags = iface->ifa_flags;
        if ((flags & IFF_UP) == 0 || (flags & IFF_LOOPBACK) != 0)
            continue;

        char address[INET_ADDRSTRLEN] = {};
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(iface->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) != nullptr) {
            result = address;
            break;
        }
    }

    freeifaddrs(interfaces);
    return result;
#endif
}

} // namespace local_address
