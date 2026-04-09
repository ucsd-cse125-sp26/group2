#include "MessageStream.hpp"

bool MessageStream::send(const void* data, Uint32 size)
{
    if (!socket)
        return false;

    auto dataSize = static_cast<int>(size);
    NET_WriteToStreamSocket(socket, &size, sizeof(size));
    return NET_WriteToStreamSocket(socket, data, dataSize);
}

bool MessageStream::poll(const std::function<void(const void* data, Uint32 size)>& callback)
{
    if (!socket)
        return false;

    Uint8 buf[4096];
    int n = NET_ReadFromStreamSocket(socket, buf, sizeof(buf));

    if (n < 0) {
        return false;
    }

    if (n > 0) {
        recvBuf.insert(recvBuf.end(), buf, buf + n);
    }

    while (recvBuf.size() >= sizeof(Uint32)) {
        Uint32 len;
        std::memcpy(&len, recvBuf.data(), sizeof(Uint32));

        if (recvBuf.size() < sizeof(Uint32) + len)
            break; // need to wait for more data

        const Uint8* payload = recvBuf.data() + sizeof(Uint32);
        callback(payload, len);

        recvBuf.erase(recvBuf.begin(), recvBuf.begin() + sizeof(Uint32) + len);
    }

    return true;
}
