/// @file FragmentReassembler.hpp
/// @brief Per-connection assembly buffer for fragmented UDP packets.
///
/// Stage 3d-4: snapshots above the MTU floor are split into multiple
/// datagrams by `UdpEndpoint::sendFragmented`. The receiver pairs them
/// by `(sequence, fragmentInfo.count)` until a complete set has arrived,
/// then hands the assembled bytes back. UDP can reorder so fragments
/// land in any order; missing fragments mean the whole logical message
/// is lost — the caller drops the in-progress set when a newer
/// `sequence` shows up. No retransmit (drop-stale is the contract).
///
/// **Scope**: this implementation tracks ONE in-progress reassembly per
/// connection — the most-recent sequence we've started. If a newer
/// sequence arrives before the current is complete, the current is
/// abandoned. This is the simplest viable design and matches the
/// "newer snapshot supersedes older" semantic the snapshot stream
/// already has at the application layer. Multi-set tracking (e.g. for
/// multiple concurrent unreliable channels) is a future enhancement.

#pragma once

#include "PacketHeader.hpp"

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <vector>

namespace net
{

class FragmentReassembler
{
public:
    /// @brief Maximum fragments per logical message — matches the
    /// 8-bit fragment-count field in the wire header.
    static constexpr int k_maxFragments = 256;

    /// @brief Result of `addFragment`.
    enum class Result
    {
        /// @brief Fragment accepted into in-progress set, but more
        /// fragments are still needed before the message is complete.
        InProgress,
        /// @brief Fragment was the final missing piece — `assembled`
        /// is populated with the full logical message.
        Complete,
        /// @brief Fragment was older than the in-progress set and got
        /// dropped (drop-stale semantics).
        Stale,
        /// @brief Header was malformed (bad fragment index/count).
        Malformed,
    };

    /// @brief Feed one received fragment into the reassembler.
    ///
    /// @param hdr        Received PacketHeader. `flags.0` must be set
    ///                   (caller pre-checks); `fragmentInfo` carries
    ///                   `(index << 8) | count`.
    /// @param payload    Fragment payload bytes (already stripped of
    ///                   the 16-byte PacketHeader by UdpEndpoint).
    /// @param payloadLen Length of the fragment's payload (≤
    ///                   k_maxPayloadBytes).
    /// @param assembled  Output: filled with the full reassembled
    ///                   logical-message bytes when result == Complete.
    /// @return See Result enum.
    Result addFragment(const PacketHeader& hdr, const uint8_t* payload, int payloadLen, std::vector<uint8_t>& assembled)
    {
        const uint8_t fragIdx = static_cast<uint8_t>(hdr.fragmentInfo >> 8);
        const uint8_t fragCount = static_cast<uint8_t>(hdr.fragmentInfo & 0xFF);

        if (fragCount == 0 || fragIdx >= fragCount)
            return Result::Malformed;

        // Older sequence than what we're currently reassembling? Drop.
        // Glenn-Fiedler "more recent" comparison handles 16-bit wrap.
        if (active_ && seqMoreRecent(active_->sequence, hdr.sequence))
            return Result::Stale;

        // Newer sequence (or first fragment ever)? Reset the in-progress set.
        if (!active_ || hdr.sequence != active_->sequence) {
            active_.emplace();
            active_->sequence = hdr.sequence;
            active_->fragmentCount = fragCount;
            active_->received.assign(fragCount, std::vector<uint8_t>{});
            active_->haveCount = 0;
        }

        // Set must be self-consistent. If a fragment claims a different
        // count than the in-progress set, the sender's sequence/count
        // got mismatched somewhere. Reset and try again with this new
        // count.
        if (active_->fragmentCount != fragCount) {
            active_.emplace();
            active_->sequence = hdr.sequence;
            active_->fragmentCount = fragCount;
            active_->received.assign(fragCount, std::vector<uint8_t>{});
            active_->haveCount = 0;
        }

        auto& slot = active_->received[fragIdx];
        if (!slot.empty())
            return Result::InProgress; // duplicate; ignore

        slot.assign(payload, payload + payloadLen);
        ++active_->haveCount;

        if (active_->haveCount < active_->fragmentCount)
            return Result::InProgress;

        // Complete. Concatenate fragments in index order and hand back.
        size_t total = 0;
        for (const auto& s : active_->received)
            total += s.size();
        assembled.clear();
        assembled.reserve(total);
        for (const auto& s : active_->received)
            assembled.insert(assembled.end(), s.begin(), s.end());

        active_.reset();
        return Result::Complete;
    }

    /// @brief Drop any in-progress reassembly. Used on disconnect /
    /// connection reset.
    void reset() noexcept { active_.reset(); }

private:
    /// @brief Glenn-Fiedler "is s2 more recent than s1?" with 16-bit
    /// wrap. Used for stale-fragment detection.
    static bool seqMoreRecent(uint16_t s1, uint16_t s2) noexcept
    {
        constexpr uint16_t k_half = 32768u;
        return ((s1 > s2) && (s1 - s2 > k_half)) || ((s2 > s1) && (s2 - s1 < k_half));
    }

    struct ActiveSet
    {
        uint16_t sequence = 0;
        uint8_t fragmentCount = 0;
        uint8_t haveCount = 0;
        std::vector<std::vector<uint8_t>> received;
    };

    // Single in-progress set per reassembler. Multi-channel reassembly
    // would need a small map; not needed at current scope.
    struct OptionalSet
    {
        bool hasValue = false;
        ActiveSet value;

        OptionalSet& emplace()
        {
            value = ActiveSet{};
            hasValue = true;
            return *this;
        }
        void reset() noexcept { hasValue = false; }
        ActiveSet* operator->() { return &value; }
        const ActiveSet* operator->() const { return &value; }
        ActiveSet& operator*() { return value; }
        const ActiveSet& operator*() const { return value; }
        explicit operator bool() const noexcept { return hasValue; }
    };
    OptionalSet active_;
};

} // namespace net
