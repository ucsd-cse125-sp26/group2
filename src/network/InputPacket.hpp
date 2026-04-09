#pragma once

#include <cstdint>

struct InputPacket
{
    uint8_t type; // 0x01
    bool forward, back, left, right, jump, crouch;
    float yaw, pitch, roll;
};
