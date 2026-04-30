/// @file RegistryArchive.hpp
/// @brief Lightweight binary serialization archives for ECS component snapshots.
///
/// OutputArchive writes trivially-copyable components into a byte buffer.
/// InputArchive reads them back, advancing an internal offset.  Used by
/// RegistrySerialization to snapshot and restore the entt registry over
/// the network.

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

/// @brief Archive that serializes trivially-copyable values into a byte buffer.
class OutputArchive
{
public:
    std::vector<uint8_t> buffer;

    /// @brief Append @p value to the internal buffer as raw bytes.
    /// @tparam T A trivially-copyable type.
    /// @param value The value to serialize.
    template <typename T>
    void operator()(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Component type must be trivially copyable for serialization");

        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), p, p + sizeof(T));
    }
};

/// @brief Archive that deserializes trivially-copyable values from a byte buffer.
class InputArchive
{
public:
    /// @brief Construct an InputArchive over an existing data buffer.
    /// @param d Pointer to the beginning of the buffer.
    /// @param s Size of the buffer in bytes.
    InputArchive(const uint8_t* d, size_t s) : data(d), size(s) {}

    /// @brief Read the next value from the buffer and advance the offset.
    /// @tparam T A trivially-copyable type.
    /// @param value Output parameter populated with the deserialized value.
    template <typename T>
    void operator()(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Component type must be trivially copyable for deserialization");

        if (offset + sizeof(T) > size) {
            throw std::runtime_error("InputArchive: out of data");
        }

        std::memcpy(&value, data + offset, sizeof(T));
        offset += sizeof(T);
    }

private:
    const uint8_t* data;
    size_t size;
    size_t offset = 0;
};
