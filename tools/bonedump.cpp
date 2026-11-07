// bonedump.cpp - one-shot read-only skeleton dump used to calibrate bone indices.
// Attaches to the running game, resolves a pawn, locates its bone array and writes
// a plain-text report (positions relative to the pawn origin) to bonedump.txt.
#include "core/memory.h"
#include "core/sdk.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sdk;

namespace {

constexpr int kTransformSize  = 32;
constexpr int kBoneScanStart  = 0x08;
constexpr int kBoneScanEnd    = 0x98;
constexpr int kBoneMax        = 96;

bool QuatLooksValid(const uint8_t* transform, int pos_offset) {
    const int quat_offset = (pos_offset == 0x10) ? 0x00 : 0x10;
    float q[4];
    std::memcpy(q, transform + quat_offset, sizeof(q));
    const float length_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return length_sq > 0.5f && length_sq < 1.6f;
}

Vec3 ExtractPosition(const uint8_t* transform, int pos_offset) {
    Vec3 position;
    std::memcpy(&position, transform + pos_offset, sizeof(float) * 3);
    return position;
}

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

uintptr_t ResolveStride(Memory& mem, uintptr_t entity_list, uintptr_t client_base, uintptr_t client_end) {
    int best_stride = 0, best_hits = 0;
    for (const int stride : {0x70, 0x78}) {
        int hits = 0;
        for (int slot = 1; slot <= 16; ++slot) {
            const uintptr_t chunk = mem.read_or<uintptr_t>(
                entity_list + kEntityChunkOffset + 8 * (slot >> kEntityChunkShift), 0);
            if (!chunk) continue;
            const uintptr_t entity = mem.read_or<uintptr_t>(chunk + stride * (slot & kEntitySlotMask), 0);
            if (entity < 0x10000) continue;
            const uintptr_t vtable = mem.read_or<uintptr_t>(entity, 0);
            if (vtable >= client_base + 0x1000 && vtable < client_end && (vtable & 0x7) == 0) ++hits;
        }
        if (hits > best_hits) {
            best_hits   = hits;
            best_stride = stride;
        }
    }
    return best_hits > 0 ? best_stride : 0;
}

uintptr_t ResolvePawn(Memory& mem, uintptr_t entity_list, int stride, uint32_t handle) {
    if (!handle || handle == 0xFFFFFFFF) return 0;
    const int index = static_cast<int>(handle & 0x7FFF);
    const uintptr_t chunk = mem.read_or<uintptr_t>(
        entity_list + kEntityChunkOffset + 8 * (index >> kEntityChunkShift), 0);
    if (!chunk) return 0;
    return mem.read_or<uintptr_t>(chunk + stride * (index & kEntitySlotMask), 0);
}

}  // namespace

int main() {
    FILE* out = std::fopen("bonedump.txt", "w");
    if (!out) return 1;

    Memory mem;
    if (!mem.Attach(L"cs2.exe")) {
        std::fprintf(out, "attach failed\n");
        std::fclose(out);
        return 1;
    }

    const uintptr_t client_base = mem.module_base(L"client.dll");
    const uintptr_t client_end  = mem.module_end(L"client.dll");
    std::fprintf(out, "pid=%lu client=0x%llX end=0x%llX\n",
                 mem.pid(), (unsigned long long)client_base, (unsigned long long)client_end);
    if (!client_base) {
        std::fclose(out);
        return 1;
    }

    const uintptr_t entity_list = mem.read_or<uintptr_t>(client_base + dwEntityList, 0);
    const uintptr_t stride      = ResolveStride(mem, entity_list, client_base, client_end);
    const uintptr_t local_pawn  = mem.read_or<uintptr_t>(client_base + dwLocalPlayerPawn, 0);
    const uintptr_t local_ctrl  = mem.read_or<uintptr_t>(client_base + dwLocalPlayerController, 0);
    std::fprintf(out, "entity_list=0x%llX stride=0x%llX local_pawn=0x%llX local_ctrl=0x%llX\n",
                 (unsigned long long)entity_list, (unsigned long long)stride,
                 (unsigned long long)local_pawn, (unsigned long long)local_ctrl);
    if (!entity_list || !stride) {
        std::fclose(out);
        return 1;
    }

    // prefer a living pawn; fall back to the local one even if dead
    uintptr_t pawn = local_pawn;
    uintptr_t controller = local_ctrl;
    const uintptr_t chunk = mem.read_or<uintptr_t>(entity_list + kEntityChunkOffset, 0);
    if (chunk) {
        bool local_alive = false;
        if (pawn) {
            const int health = mem.read_or<int32_t>(pawn + C_BaseEntity::m_iHealth, 0);
            const uint8_t life = mem.read_or<uint8_t>(pawn + C_BaseEntity::m_lifeState, 1);
            local_alive = health > 0 && health <= 100 && life == kLifeAlive;
        }
        if (!pawn || !local_alive) {
            for (int slot = 1; slot <= kMaxPlayerSlots; ++slot) {
                const uintptr_t ctrl = mem.read_or<uintptr_t>(chunk + stride * slot, 0);
                if (!ctrl || ctrl == local_ctrl) continue;
                const uint32_t handle = mem.read_or<uint32_t>(ctrl + CCSPlayerController::m_hPlayerPawn, 0);
                const uintptr_t candidate = ResolvePawn(mem, entity_list, stride, handle);
                if (!candidate) continue;
                const int health = mem.read_or<int32_t>(candidate + C_BaseEntity::m_iHealth, 0);
                const uint8_t life = mem.read_or<uint8_t>(candidate + C_BaseEntity::m_lifeState, 1);
                if (health > 0 && health <= 100 && life == kLifeAlive) {
                    pawn = candidate;
                    controller = ctrl;
                    break;
                }
            }
        }
    }

    std::fprintf(out, "target pawn=0x%llX controller=0x%llX\n",
                 (unsigned long long)pawn, (unsigned long long)controller);
    if (!pawn) {
        std::fclose(out);
        return 1;
    }

    if (controller) {
        char name[64] = {};
        if (mem.read_bytes(controller + CBasePlayerController::m_iszPlayerName, name, sizeof(name) - 1))
            std::fprintf(out, "player=%s\n", name);
    }

    const uintptr_t body  = mem.read_or<uintptr_t>(pawn + C_BaseEntity::m_CBodyComponent, 0);
    const uintptr_t scene = body ? mem.read_or<uintptr_t>(body + CBodyComponent::m_pSceneNode, 0) : 0;
    if (!scene) {
        std::fprintf(out, "scene node not resolved (body=0x%llX)\n", (unsigned long long)body);
        std::fclose(out);
        return 1;
    }

    const uintptr_t model_state = scene + CSkeletonInstance::m_modelState;
    const Vec3 origin = mem.read_or<Vec3>(scene + CGameSceneNode::m_vecAbsOrigin, Vec3{});
    std::fprintf(out, "body=0x%llX scene=0x%llX model_state=0x%llX\n",
                 (unsigned long long)body, (unsigned long long)scene, (unsigned long long)model_state);
    std::fprintf(out, "origin=(%.2f, %.2f, %.2f)\n", origin.x, origin.y, origin.z);

    // dump every pointer-looking field around the model state, with a bone-array guess per field
    std::fprintf(out, "--- fields around model_state (0x140) ---\n");
    uintptr_t best_ptr   = 0;
    int       best_score = -1;
    int       best_pos   = -1;
    int       best_off   = -1;
    for (int off = -0x40; off <= 0x120; off += 8) {
        const uintptr_t value = mem.read_or<uintptr_t>(model_state + off, 0);
        std::fprintf(out, "  %+04X: 0x%016llX", off, (unsigned long long)value);
        if (value > 0x10000 && value < 0x00007FFFFFFFFFFFULL) {
            uint8_t transforms[32 * kTransformSize] = {};
            if (mem.read_bytes(value, transforms, sizeof(transforms))) {
                for (const int pos_offset : {0x10, 0x00}) {
                    int quats = 0;
                    for (int bone = 0; bone < 32; ++bone)
                        if (QuatLooksValid(transforms + bone * kTransformSize, pos_offset)) ++quats;
                    float max_gap = 0.0f;
                    for (int bone = 1; bone < 32; ++bone) {
                        const Vec3 a = ExtractPosition(transforms + (bone - 1) * kTransformSize, pos_offset);
                        const Vec3 b = ExtractPosition(transforms + bone * kTransformSize, pos_offset);
                        const float gap = Distance(a, b);
                        if (gap > max_gap) max_gap = gap;
                    }
                    const Vec3 b0 = ExtractPosition(transforms, pos_offset);
                    const float dist0  = Distance(b0, origin);
                    const Vec3 b1 = ExtractPosition(transforms + kTransformSize, pos_offset);
                    const float dist01 = Distance(b0, b1);

                    std::fprintf(out, "\n         pos=%d quats=%2d/32 maxgap=%7.1f dist0=%7.1f dist01=%7.1f",
                                 pos_offset, quats, max_gap, dist0, dist01);

                    // candidate quality: mostly-valid quats and a compact chain
                    const int score = quats * 100 - static_cast<int>(max_gap);
                    if (quats >= 24 && dist0 < 300.0f && score > best_score) {
                        best_score = score;
                        best_ptr   = value;
                        best_pos   = pos_offset;
                        best_off   = off;
                    }
                }
            }
        }
        std::fprintf(out, "\n");
    }

    const int found_offset = best_off;
    const int found_pos    = best_pos;
    uintptr_t bone_array   = best_ptr;

    std::fprintf(out, "bone_array=0x%llX offset=0x%X pos=0x%X\n",
                 (unsigned long long)bone_array, found_offset, found_pos);
    if (!bone_array) {
        std::fclose(out);
        return 1;
    }

    // read the skeleton in blocks: stop when memory no longer maps
    std::vector<uint8_t> all(kBoneMax * kTransformSize);
    int bones_read = 0;
    for (int base = 0; base + 16 <= kBoneMax; base += 16) {
        if (!mem.read_bytes(bone_array + base * kTransformSize, all.data() + base * kTransformSize, 16 * kTransformSize))
            break;
        bones_read = base + 16;
    }

    std::fprintf(out, "bones_read=%d\n", bones_read);
    std::fprintf(out, "idx      x        y        z     |  dx       dy       dz    | horiz  dist\n");
    for (int bone = 0; bone < bones_read; ++bone) {
        const Vec3 p = ExtractPosition(all.data() + bone * kTransformSize, found_pos);
        const float dx = p.x - origin.x;
        const float dy = p.y - origin.y;
        const float dz = p.z - origin.z;
        const float horiz = std::sqrt(dx * dx + dy * dy);
        const float dist = std::sqrt(horiz * horiz + dz * dz);
        std::fprintf(out, "%3d  %8.2f %8.2f %8.2f  | %8.2f %8.2f %8.2f | %6.2f %6.2f\n",
                     bone, p.x, p.y, p.z, dx, dy, dz, horiz, dist);
    }

    std::fclose(out);
    return 0;
}
