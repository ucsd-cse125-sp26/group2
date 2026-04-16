#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

class OutputArchive
{
public:
    std::vector<uint8_t> buffer;

    template <typename T>
    void operator()(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Component type must be trivially copyable for serialization");

        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), p, p + sizeof(T));
    }
};

class InputArchive
{
public:
    InputArchive(const uint8_t* d, size_t s) : data(d), size(s) {}

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
