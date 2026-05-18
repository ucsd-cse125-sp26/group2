#include "network/crypto/HmacSha256.hpp"
#include "network/transport/PacketHeader.hpp"
#include "network/transport/UdpSessionTransport.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>

namespace
{
bool waitForPayload(net::UdpSessionTransport& transport,
                    net::UdpSessionTransport::Event& out,
                    net::UdpSessionTransport::EventType type,
                    int timeoutMs)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeoutMs);
    while (SDL_GetTicks() <= deadline) {
        while (transport.pollEvent(out)) {
            if (out.type == type)
                return true;
        }
        SDL_Delay(2);
    }
    return false;
}

bool require(bool condition, const char* message)
{
    if (!condition)
        SDL_Log("udp_session_tests: %s", message);
    return condition;
}

bool digestMatchesHex(const net::crypto::Sha256Digest& digest, std::string_view hex)
{
    static constexpr char k_digits[] = "0123456789abcdef";
    if (hex.size() != digest.size() * 2)
        return false;
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (hex[i * 2] != k_digits[digest[i] >> 4] || hex[i * 2 + 1] != k_digits[digest[i] & 0x0f])
            return false;
    }
    return true;
}
} // namespace

int main()
{
    SDL_Init(0);
    NET_Init();

    {
        const char abc[] = "abc";
        const auto digest = net::crypto::sha256(abc, 3);
        if (!require(digestMatchesHex(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
                     "sha256 test vector mismatch"))
        {
            NET_Quit();
            SDL_Quit();
            return 1;
        }

        const std::array<std::uint8_t, 20> key = {
            0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
            0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        };
        const char msg[] = "Hi There";
        const auto mac = net::crypto::hmacSha256(key.data(), key.size(), msg, 8);
        if (!require(digestMatchesHex(mac, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
                     "hmac-sha256 test vector mismatch"))
        {
            NET_Quit();
            SDL_Quit();
            return 1;
        }
    }

    {
        net::PacketHeader hdr{};
        hdr.kind = static_cast<std::uint8_t>(net::PacketKind::Payload);
        hdr.connectionId = 0x1122334455667788ULL;
        hdr.sequence = 0x01020304u;
        hdr.ack = 0x05060708u;
        hdr.ackBits = 0xa0b0c0d0u;
        hdr.routeId = 7;
        hdr.channel = static_cast<std::uint8_t>(net::ChannelId::EventReliableOrdered);
        hdr.flags = net::k_flagFragmented;
        hdr.fragmentInfo = 0x0203u;
        hdr.fragmentGroup = 0x4444u;

        std::uint8_t bytes[sizeof(net::PacketHeader)]{};
        net::encodePacketHeader(hdr, bytes);
        net::PacketHeader decoded{};
        if (!require(net::decodePacketHeader(bytes, sizeof(bytes), decoded), "header decode failed") ||
            !require(decoded.connectionId == hdr.connectionId, "connection id mismatch") ||
            !require(decoded.sequence == hdr.sequence, "sequence mismatch") ||
            !require(decoded.ack == hdr.ack, "ack mismatch") ||
            !require(decoded.ackBits == hdr.ackBits, "ack bits mismatch") ||
            !require(decoded.routeId == hdr.routeId, "route id mismatch") ||
            !require(decoded.channel == hdr.channel, "channel mismatch") ||
            !require(decoded.fragmentInfo == hdr.fragmentInfo, "fragment info mismatch") ||
            !require(decoded.fragmentGroup == hdr.fragmentGroup, "fragment group mismatch"))
        {
            NET_Quit();
            SDL_Quit();
            return 1;
        }
    }

    constexpr Uint16 k_port = 19191;
    net::UdpSessionTransport server;
    if (!require(server.openServer("127.0.0.1", k_port), "server open failed")) {
        NET_Quit();
        SDL_Quit();
        return 1;
    }

    std::atomic<bool> pumpServer{true};
    std::thread pumpThread;
    auto finish = [&](int code) {
        pumpServer.store(false, std::memory_order_relaxed);
        if (pumpThread.joinable())
            pumpThread.join();
        server.close();
        NET_Quit();
        SDL_Quit();
        return code;
    };

    pumpThread = std::thread([&]() {
        while (pumpServer.load(std::memory_order_relaxed)) {
            server.pump();
            SDL_Delay(1);
        }
    });

    net::UdpSessionTransport client;
    if (!require(client.connectClient("127.0.0.1", k_port, 1500), "client connect failed") ||
        !require(client.clientConnectionId() != 0, "client connection id not assigned"))
    {
        client.close();
        return finish(1);
    }

    net::UdpSessionTransport::Event ev;
    if (!require(waitForPayload(server, ev, net::UdpSessionTransport::EventType::Connected, 1500),
                 "server did not emit connected event"))
    {
        client.close();
        return finish(1);
    }
    const std::uint64_t serverConn = ev.connectionId;
    if (!require(serverConn == client.clientConnectionId(), "client/server connection ids differ")) {
        client.close();
        return finish(1);
    }

    const char ping[] = "reliable-control";
    if (!require(client.send(client.clientConnectionId(),
                             net::ChannelId::ControlReliableOrdered,
                             ping,
                             static_cast<int>(sizeof(ping))),
                 "client reliable send failed") ||
        !require(waitForPayload(server, ev, net::UdpSessionTransport::EventType::Payload, 1500),
                 "server did not receive reliable payload") ||
        !require(ev.channel == net::ChannelId::ControlReliableOrdered, "server payload channel mismatch") ||
        !require(ev.payload.size() == sizeof(ping), "server payload size mismatch") ||
        !require(std::memcmp(ev.payload.data(), ping, sizeof(ping)) == 0, "server payload bytes mismatch"))
    {
        client.close();
        return finish(1);
    }

    const char pong[] = "event-reply";
    if (!require(server.send(serverConn, net::ChannelId::EventReliableOrdered, pong, static_cast<int>(sizeof(pong))),
                 "server reliable send failed") ||
        !require(waitForPayload(client, ev, net::UdpSessionTransport::EventType::Payload, 1500),
                 "client did not receive reliable payload") ||
        !require(ev.channel == net::ChannelId::EventReliableOrdered, "client payload channel mismatch") ||
        !require(ev.payload.size() == sizeof(pong), "client payload size mismatch") ||
        !require(std::memcmp(ev.payload.data(), pong, sizeof(pong)) == 0, "client payload bytes mismatch"))
    {
        client.close();
        return finish(1);
    }

    client.close();
    return finish(0);
}
