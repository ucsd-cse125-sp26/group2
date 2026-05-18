#include "network/ChatProtocol.hpp"
#include "network/VoiceProtocol.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
void testChatRoundTrip()
{
    const auto clientWire = net::chat::encodeClientText(7, "  hello\x01  world  ");
    const auto client = net::chat::decodeClientText(clientWire);
    assert(client);
    assert(client->clientSeq == 7);
    assert(client->message == "hello world");

    const auto serverWire = net::chat::encodeServerText(ClientId{3}, 42, client->message);
    const auto server = net::chat::decodeServerText(serverWire);
    assert(server);
    assert(server->sender.value == 3);
    assert(server->serverSeq == 42);
    assert(server->message == "hello world");
}

void testChatRejectsInvalidAndOversized()
{
    const std::string badUtf8 = std::string("bad") + static_cast<char>(0xc0) + static_cast<char>(0xaf);
    assert(net::chat::encodeClientText(1, badUtf8).empty());

    std::string huge(400, 'x');
    const auto wire = net::chat::encodeClientText(2, huge);
    const auto chat = net::chat::decodeClientText(wire);
    assert(chat);
    assert(chat->message.size() == net::chat::k_maxChatBytes);

    std::vector<std::uint8_t> malformed = wire;
    malformed.back() = 0xff;
    assert(!net::chat::decodeClientText(malformed));
}

void testVoiceRoundTrip()
{
    std::vector<std::uint8_t> opus{1, 2, 3, 4, 5};
    const auto clientWire = net::voice::encodeClientFrame(91, net::voice::k_frameMs, opus);
    const auto client = net::voice::decodeClientFrame(clientWire);
    assert(client);
    assert(client->sequence == 91);
    assert(client->frameMs == net::voice::k_frameMs);
    assert(client->opus == opus);

    const auto serverWire =
        net::voice::encodeServerFrame(ClientId{12}, client->sequence, client->frameMs, client->opus);
    const auto server = net::voice::decodeServerFrame(serverWire);
    assert(server);
    assert(server->speaker.value == 12);
    assert(server->sequence == 91);
    assert(server->opus == opus);
}

void testVoiceBounds()
{
    std::vector<std::uint8_t> empty;
    assert(net::voice::encodeClientFrame(1, net::voice::k_frameMs, empty).empty());

    std::vector<std::uint8_t> tooLarge(static_cast<std::size_t>(net::voice::k_maxOpusBytes) + 1u, 0x42);
    assert(net::voice::encodeClientFrame(1, net::voice::k_frameMs, tooLarge).empty());

    std::vector<std::uint8_t> opus{0x33};
    assert(net::voice::encodeClientFrame(1, 15, opus).empty());
}
} // namespace

int main()
{
    testChatRoundTrip();
    testChatRejectsInvalidAndOversized();
    testVoiceRoundTrip();
    testVoiceBounds();
    return 0;
}
