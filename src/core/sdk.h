// sdk.h - dumper offsets/schemas plus the entity list structure constants
#pragma once

#include <cstdint>

#include <offsets/client_dll.hpp>
#include <offsets/offsets.hpp>

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Matrix4x4 {
    float m[4][4] = {};
};

namespace sdk {

using namespace cs2_dumper::offsets::client_dll;
using namespace cs2_dumper::schemas::client_dll;

// entity list access: pointer table of 512-slot chunks.
// list structure constants (not schema): stable across patches; adjust if the layout changes
constexpr int kEntityChunkShift  = 9;
constexpr int kEntityChunkOffset = 0x10;
constexpr int kEntitySlotStride  = 0x78;
constexpr int kEntitySlotMask    = 0x1FF;
constexpr int kMaxPlayerSlots    = 64;

// LIFE_ALIVE
constexpr uint8_t kLifeAlive = 0;
constexpr uint32_t kFlagOnGround = 1;   // m_fFlags bit 0 (FL_ONGROUND)

}  // namespace sdk
