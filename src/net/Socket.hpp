#pragma once
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Cross-platform UDP socket wrapper
//   Windows: Winsock2  |  Linux/macOS: POSIX sockets
// ---------------------------------------------------------------------------

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockFd = SOCKET;
static constexpr SockFd k_badSock = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SockFd = int;
static constexpr SockFd k_badSock = -1;
#endif

class UdpSocket
{
public:
    // One-time platform init (call before creating any socket).
    static bool platformInit();
    static void platformShutdown();

    bool open();              // create UDP socket
    bool bind(uint16_t port); // bind to local port (server side)
    void setNonBlocking(bool nb);
    void close();
    bool isOpen() const { return fd != k_badSock; }

    // Returns bytes sent, -1 on error.
    int sendTo(const void* buf, int len, const sockaddr_in& addr) const;

    // Returns bytes received, 0 if would-block, -1 on real error.
    // addr is filled with sender's address.
    int recvFrom(void* buf, int maxLen, sockaddr_in& addr) const;

    // Build a sockaddr_in from an IP string ("127.0.0.1") and port.
    static sockaddr_in makeAddr(const char* ip, uint16_t port);

    // Format addr as "x.x.x.x:port" into buf (at least 24 chars).
    static void addrToStr(const sockaddr_in& addr, char* buf, int bufLen);

    // Compare two addresses for equality.
    static bool addrEqual(const sockaddr_in& a, const sockaddr_in& b);

private:
    SockFd fd = k_badSock;
};
