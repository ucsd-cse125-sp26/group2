#include "Socket.hpp"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Platform init/shutdown
// ---------------------------------------------------------------------------

bool UdpSocket::platformInit()
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void UdpSocket::platformShutdown()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Socket lifecycle
// ---------------------------------------------------------------------------

bool UdpSocket::open()
{
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == k_badSock) {
        std::perror("UdpSocket::open");
        return false;
    }

    // Allow address reuse so we can restart the server quickly.
    int yes = 1;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    return true;
}

bool UdpSocket::bind(uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("UdpSocket::bind");
        return false;
    }
    return true;
}

void UdpSocket::setNonBlocking(bool nb)
{
#ifdef _WIN32
    u_long mode = nb ? 1 : 0;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (nb)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
#endif
}

void UdpSocket::close()
{
    if (fd == k_badSock)
        return;
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
    fd = k_badSock;
}

// ---------------------------------------------------------------------------
// Send / receive
// ---------------------------------------------------------------------------

int UdpSocket::sendTo(const void* buf, int len, const sockaddr_in& addr) const
{
    return static_cast<int>(
        sendto(fd, static_cast<const char*>(buf), len, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)));
}

int UdpSocket::recvFrom(void* buf, int maxLen, sockaddr_in& addr) const
{
#ifdef _WIN32
    int fromLen = sizeof(addr);
#else
    socklen_t fromLen = sizeof(addr);
#endif
    int n = static_cast<int>(
        recvfrom(fd, static_cast<char*>(buf), maxLen, 0, reinterpret_cast<sockaddr*>(&addr), &fromLen));
    if (n < 0) {
#ifdef _WIN32
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK)
            return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
#endif
        return -1;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Address helpers
// ---------------------------------------------------------------------------

sockaddr_in UdpSocket::makeAddr(const char* ip, uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    InetPtonA(AF_INET, ip, &addr.sin_addr);
#else
    inet_pton(AF_INET, ip, &addr.sin_addr);
#endif
    return addr;
}

void UdpSocket::addrToStr(const sockaddr_in& addr, char* buf, int bufLen)
{
    char ip[INET_ADDRSTRLEN] = "?";
#ifdef _WIN32
    InetNtopA(AF_INET, &addr.sin_addr, ip, sizeof(ip));
#else
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
#endif
    std::snprintf(buf, bufLen, "%s:%u", ip, ntohs(addr.sin_port));
}

bool UdpSocket::addrEqual(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}
