#include "client/voice/VoiceJitterBuffer.hpp"

#include <cstdlib>
#include <vector>

namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

void testReordersFrames()
{
    VoiceJitterBuffer jitter(2, 8);
    const std::vector<std::uint8_t> a{1};
    const std::vector<std::uint8_t> b{2};
    jitter.push(11, 20, b);
    require(!jitter.pop());
    jitter.push(10, 20, a);
    auto first = jitter.pop();
    require(first.has_value());
    require(first->sequence == 10);
    auto second = jitter.pop();
    require(second.has_value());
    require(second->sequence == 11);
}

void testDropsDuplicatesAndOldFrames()
{
    VoiceJitterBuffer jitter(1, 4);
    const std::vector<std::uint8_t> payload{9};
    jitter.push(4, 20, payload);
    jitter.push(4, 20, payload);
    require(jitter.size() == 1);
    auto first = jitter.pop();
    require(first && first->sequence == 4);
    jitter.push(4, 20, payload);
    require(jitter.size() == 0);
}

void testReportsLossWhenGapPersists()
{
    VoiceJitterBuffer jitter(1, 6);
    const std::vector<std::uint8_t> payload{1};
    jitter.push(1, 20, payload);
    auto first = jitter.pop();
    require(first && first->sequence == 1);
    jitter.push(4, 20, payload);
    jitter.push(5, 20, payload);
    jitter.push(6, 20, payload);
    auto lost = jitter.pop();
    require(lost && lost->lost && lost->sequence == 2);
}
} // namespace

int main()
{
    testReordersFrames();
    testDropsDuplicatesAndOldFrames();
    testReportsLossWhenGapPersists();
    return 0;
}
