// game.cpp - the read-only game view; PROCESS_VM_READ is the only access ever requested
#include "game.h"
#include "bone_validation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace sdk;   // dump offsets + schema names

namespace {

constexpr wchar_t kClientDll[] = L"client.dll";

// CModelState keeps its schema members from 0xA0 up; the runtime bone array pointer
// lives below that. We locate it by scanning and validating, so a patch that shuffles
// the runtime layout re-derives itself instead of breaking.
constexpr int kBoneScanStart = 0x08;
constexpr int kBoneScanEnd   = 0x98;
constexpr int kBoneScanStep  = 8;
constexpr int kTransformSize = 32;

constexpr int kItemDefOffset =
    C_EconEntity::m_AttributeManager + C_AttributeContainer::m_Item + C_EconItemView::m_iItemDefinitionIndex;

// one controller read carries team, name, money pointer, ping, pawn/observer handles, kit
constexpr int kCtrlBlockBase = C_BaseEntity::m_iTeamNum;
constexpr int kCtrlBlockSize = CCSPlayerController::m_bPawnHasDefuser + 1 - kCtrlBlockBase;
static_assert(kCtrlBlockSize > 0, "controller block");

// one pawn read carries the body pointer, vitals, flags and velocity
constexpr int kPawnNearBase = C_BaseEntity::m_CBodyComponent;
constexpr int kPawnNearSize = C_BaseEntity::m_vecVelocity + 12 - kPawnNearBase;
static_assert(C_BaseEntity::m_iHealth + 4 <= kPawnNearBase + kPawnNearSize, "health outside pawn block");
static_assert(C_BaseEntity::m_lifeState + 1 <= kPawnNearBase + kPawnNearSize, "life state outside pawn block");
static_assert(C_BaseEntity::m_iTeamNum + 1 <= kPawnNearBase + kPawnNearSize, "team outside pawn block");
static_assert(C_BaseEntity::m_fFlags + 4 <= kPawnNearBase + kPawnNearSize, "flags outside pawn block");

// one pawn read carries spotted state/mask, scoped/defusing flags and armor
constexpr int kPawnStatusBase = C_CSPlayerPawn::m_entitySpottedState;
constexpr int kPawnStatusSize = C_CSPlayerPawn::m_ArmorValue + 4 - kPawnStatusBase;
static_assert(EntitySpottedState_t::m_bSpottedByMask + 8 <= kPawnStatusSize, "spotted mask outside pawn block");
static_assert(C_CSPlayerPawn::m_bIsScoped + 1 <= kPawnStatusBase + kPawnStatusSize, "scoped outside pawn block");
static_assert(C_CSPlayerPawn::m_bIsDefusing + 1 <= kPawnStatusBase + kPawnStatusSize, "defusing outside pawn block");

// one weapon read carries the item id, clip and reload flag
constexpr int kWeaponBlockBase = kItemDefOffset;
constexpr int kWeaponBlockSize = std::max<int>(
    C_BasePlayerWeapon::m_iClip1 + 4, C_CSWeaponBase::m_bInReload + 1) - kWeaponBlockBase;
static_assert(C_BasePlayerWeapon::m_iClip1 >= kWeaponBlockBase, "clip outside weapon block");
static_assert(C_CSWeaponBase::m_bInReload >= kWeaponBlockBase, "reload outside weapon block");

// controller block fields (name, money pointer, ping, handles, kit) must sit inside it
static_assert(CBasePlayerController::m_iszPlayerName >= kCtrlBlockBase, "name outside controller block");
static_assert(CCSPlayerController::m_pInGameMoneyServices >= kCtrlBlockBase, "money outside controller block");
static_assert(CCSPlayerController::m_iPing >= kCtrlBlockBase, "ping outside controller block");
static_assert(CCSPlayerController::m_hPlayerPawn >= kCtrlBlockBase, "pawn handle outside controller block");
static_assert(CCSPlayerController::m_hObserverPawn >= kCtrlBlockBase, "observer handle outside controller block");

// scene node: origin plus the whole model-state window the bone scan probes
constexpr int kSceneBlockSize = CSkeletonInstance::m_modelState + kBoneScanEnd + 8;
static_assert(CGameSceneNode::m_vecAbsOrigin + 12 <= kSceneBlockSize, "origin outside scene block");
static_assert(kSceneBlockSize > 0, "scene block");

// an off-screen player draws nothing (box, skeleton and trigger all need a screen point),
// so their bone read can be skipped. feet and approximate head keep point-blank targets
int ProjectedVisibility(const Matrix4x4& view, const Vec3& p) {
    const float w = view.m[3][0] * p.x + view.m[3][1] * p.y + view.m[3][2] * p.z + view.m[3][3];
    if (w < 0.01f) return -1;
    const float x = view.m[0][0] * p.x + view.m[0][1] * p.y + view.m[0][2] * p.z + view.m[0][3];
    const float y = view.m[1][0] * p.x + view.m[1][1] * p.y + view.m[1][2] * p.z + view.m[1][3];
    const bool inside = std::fabs(x / w) <= 2.0f && std::fabs(y / w) <= 2.0f;
    return inside ? 1 : 0;
}

bool BonesWorthReading(const Matrix4x4& view, const Vec3& origin) {
    // an unreadable view matrix is all zeroes: never cull against it
    if (view.m[3][0] == 0.0f && view.m[3][1] == 0.0f && view.m[3][2] == 0.0f && view.m[3][3] == 0.0f)
        return true;
    const Vec3 head{ origin.x, origin.y, origin.z + 72.0f };
    return ProjectedVisibility(view, origin) == 1 || ProjectedVisibility(view, head) == 1;
}

// weapons only: knives sit on the ground after almost every death and would bury the map in labels
bool IsKnife(int item_index) {
    return item_index == 41 || item_index == 42 || item_index == 59 || item_index == 80 || item_index >= 500;
}

bool OriginLooksValid(const Vec3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) return false;
    if (std::fabs(p.x) > 20000.0f || std::fabs(p.y) > 20000.0f || std::fabs(p.z) > 10000.0f) return false;
    return true;
}

// a wrong dwViewMatrix (build mismatch) yields a matrix that is finite but useless;
// reject it instead of projecting every player into garbage
bool ViewLooksUsable(const Matrix4x4& view) {
    for (const auto& row : view.m)
        for (float value : row)
            if (!std::isfinite(value)) return false;
    // an all-zero bottom row means the matrix was never written (or read failed)
    return view.m[3][0] != 0.0f || view.m[3][1] != 0.0f ||
           view.m[3][2] != 0.0f || view.m[3][3] != 0.0f;
}

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
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// display strings are lowercase by request; multi-byte utf-8 (cyrillic names) passes through
void ToLowerAscii(char* text) {
    for (; *text; ++text) {
        if (*text >= 'A' && *text <= 'Z')
            *text = static_cast<char>(*text - 'A' + 'a');
    }
}

struct WeaponName {
    int         index;
    const char* name;
    char        icon;    // icon-font glyph, from the font's own mapping
    int         clip;    // full magazine, 0 = nothing to meter
};

// cs2 item definition ids -> short lowercase names + icon glyphs
// (letters follow the icon font's published mapping: W = ak-47, Z = awp, etc.)
constexpr WeaponName kWeaponNames[] = {
    { 1, "deagle", 'A', 7 }, { 2, "dual berettas", 'B', 30 }, { 3, "five-seven", 'C', 20 },
    { 4, "glock-18", 'D', 20 }, { 7, "ak-47", 'W', 30 }, { 8, "aug", 'U', 30 }, { 9, "awp", 'Z', 10 },
    { 10, "famas", 'R', 25 }, { 11, "g3sg1", 'X', 20 }, { 13, "galil", 'Q', 35 }, { 14, "m249", 'g', 100 },
    { 16, "m4a4", 'S', 30 }, { 17, "mac-10", 'K', 30 }, { 19, "p90", 'P', 50 }, { 23, "mp5-sd", 'x', 30 },
    { 24, "ump-45", 'L', 25 }, { 25, "xm1014", 'b', 7 }, { 26, "pp-bizon", 'M', 64 },
    { 27, "mag-7", 'd', 5 }, { 28, "negev", 'f', 150 }, { 29, "sawed-off", 'c', 7 },
    { 30, "tec-9", 'H', 18 }, { 31, "zeus", 'h', 1 }, { 32, "p2000", 'E', 13 }, { 33, "mp7", 'N', 30 },
    { 34, "mp9", 'O', 30 }, { 35, "nova", 'e', 8 }, { 36, "p250", 'F', 13 }, { 38, "scar-20", 'Y', 20 },
    { 39, "sg 553", 'V', 30 }, { 40, "ssg 08", 'a', 10 },
    { 41, "knife", '[', 0 }, { 42, "knife", '[', 0 }, { 59, "knife", '[', 0 }, { 80, "knife", '[', 0 },
    { 43, "flash", 'i', 1 }, { 44, "he grenade", 'j', 1 }, { 45, "smoke", 'k', 1 }, { 46, "molotov", 'l', 1 },
    { 47, "decoy", 'm', 1 }, { 48, "incendiary", 'n', 1 }, { 49, "c4", 'o', 1 },
    { 60, "m4a1-s", 'T', 20 }, { 61, "usp-s", 'G', 12 }, { 63, "cz75-auto", 'I', 12 }, { 64, "r8 revolver", 'J', 8 },
    { 500, "bayonet", '[', 0 }, { 503, "classic", '[', 0 }, { 505, "flip", '[', 0 }, { 506, "gut", '[', 0 },
    { 507, "karambit", '[', 0 }, { 508, "m9 bayonet", '[', 0 }, { 509, "huntsman", '[', 0 },
    { 512, "falchion", '[', 0 }, { 514, "bowie", '[', 0 }, { 515, "butterfly", '[', 0 },
    { 516, "shadow daggers", '9', 0 }, { 517, "paracord", '[', 0 }, { 518, "survival", '[', 0 },
    { 519, "ursus", '[', 0 }, { 520, "navaja", '[', 0 }, { 521, "nomad", '[', 0 }, { 522, "stiletto", '[', 0 },
    { 523, "talon", '[', 0 }, { 525, "skeleton", '[', 0 }, { 526, "kukri", '[', 0 },
};

const WeaponName* WeaponOf(int index) {
    for (const WeaponName& entry : kWeaponNames)
        if (entry.index == index) return &entry;
    return nullptr;
}

}  // namespace

bool GameReader::Attach(const wchar_t* exe_name) {
    if (memory_.attached()) return true;
    if (!memory_.Attach(exe_name)) return false;

    client_base_ = memory_.module_base(kClientDll);
    if (!client_base_) {
        memory_.Detach();
        return false;
    }

    bone_array_offset_ = -1;   // re-derive against the new process
    bone_pos_offset_   = -1;
    bone_scan_budget_  = BoneScanBudget{};
    mouse_settings_reader_.Reset();
    entity_stride_     = 0;
    empty_frames_      = 0;
    ResetCaches();
    return true;
}

void GameReader::Detach() {
    memory_.Detach();
    client_base_ = 0;
    ResetWeaponScan();
    ResetCaches();
}

void GameReader::ResetCaches() {
    for (MoneyCache& cache : money_cache_) cache = MoneyCache{};
    for (WeaponCache& cache : weapon_cache_) cache = WeaponCache{};
}

uintptr_t GameReader::ResolvePawn(uintptr_t entity_list, uint32_t handle) const {
    if (handle == 0 || handle == 0xFFFFFFFF) return 0;

    const int index = static_cast<int>(handle & 0x7FFF);
    const uintptr_t chunk = memory_.read_or<uintptr_t>(
        entity_list + sdk::kEntityChunkOffset + 8 * (index >> sdk::kEntityChunkShift), 0);
    if (!chunk) return 0;

    return memory_.read_or<uintptr_t>(
        chunk + entity_stride_ * (index & sdk::kEntitySlotMask), 0);
}

// player pawn handles index the first chunk, whose slot table the frame already holds
uintptr_t GameReader::ResolveSlotEntity(uintptr_t entity_list, const uint8_t* slot_records, int stride,
                                        uint32_t handle) const {
    if (handle == 0 || handle == 0xFFFFFFFF) return 0;

    const int index = static_cast<int>(handle & 0x7FFF);
    if (slot_records && stride > 0 && index >= 0 && index <= sdk::kMaxPlayerSlots) {
        uintptr_t entity = 0;
        std::memcpy(&entity, slot_records + static_cast<size_t>(stride) * index, sizeof(entity));
        return entity;
    }
    return ResolvePawn(entity_list, handle);
}

int GameReader::ResolveEntityStride(uintptr_t entity_list) {
    const uintptr_t client_end = memory_.module_end(kClientDll);
    if (!client_end) return 0;

    // slot records sit 0x70 (recent builds) or 0x78 bytes apart;
    // a valid record holds an entity whose vtable lives inside client.dll
    int best_stride = 0;
    int best_hits   = 0;

    for (const int stride : {0x70, 0x78}) {
        int hits = 0;
        for (int slot = 1; slot <= 16; ++slot) {
            const uintptr_t chunk = memory_.read_or<uintptr_t>(
                entity_list + sdk::kEntityChunkOffset + 8 * (slot >> sdk::kEntityChunkShift), 0);
            if (!chunk) continue;

            const uintptr_t entity = memory_.read_or<uintptr_t>(
                chunk + stride * (slot & sdk::kEntitySlotMask), 0);
            if (entity < 0x10000) continue;

            const uintptr_t vtable = memory_.read_or<uintptr_t>(entity, 0);
            if (vtable >= client_base_ + 0x1000 && vtable < client_end && (vtable & 0x7) == 0)
                ++hits;
        }
        if (hits > best_hits) {
            best_hits   = hits;
            best_stride = stride;
        }
    }

    return best_hits > 0 ? best_stride : 0;
}

bool GameReader::ResolveBoneLayout(const uint8_t* scene_block, const Vec3& origin) {
    for (int offset = kBoneScanStart; offset <= kBoneScanEnd; offset += kBoneScanStep) {
        uintptr_t candidate = 0;
        std::memcpy(&candidate, scene_block + CSkeletonInstance::m_modelState + offset, sizeof(candidate));
        if (candidate < 0x10000) continue;

        uint8_t transforms[kEspBoneCount * kTransformSize];
        if (!memory_.read_bytes(candidate, transforms, sizeof(transforms))) continue;

        // the transform stores a quaternion and a position: try both orderings
        for (const int pos_offset : {0x10, 0x00}) {
            if (!QuatLooksValid(transforms, pos_offset)) continue;
            if (!QuatLooksValid(transforms + kTransformSize, pos_offset)) continue;

            const Vec3 first  = ExtractPosition(transforms, pos_offset);
            const Vec3 second = ExtractPosition(transforms + kTransformSize, pos_offset);
            if (Distance(first, origin) > 200.0f) continue;    // bones hug the entity origin
            if (Distance(first, second) > 60.0f) continue;     // neighbours are close together

            Vec3 pose[kEspBoneCount];
            bool valid = true;
            for (int bone = 0; bone < kEspBoneCount; ++bone) {
                const auto* transform = transforms + bone * kTransformSize;
                pose[bone] = ExtractPosition(transform, pos_offset);
                if (!QuatLooksValid(transform, pos_offset)) valid = false;
            }
            if (!valid || !BonePoseLooksUsable(pose, kEspBoneCount, kHeadBoneIndex, origin)) continue;

            bone_array_offset_ = offset;
            bone_pos_offset_   = pos_offset;
            return true;
        }
    }
    return false;
}

bool GameReader::ReadBones(const uint8_t* scene_block, EspPlayer& out, int slot) {
    out.bones_valid = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt != 0 || bone_array_offset_ < 0 || bone_pos_offset_ < 0) {
            if (!bone_scan_budget_.Acquire(GetTickCount64(), slot)) return false;
            if (!ResolveBoneLayout(scene_block, out.origin)) return false;
        }

        uintptr_t bone_array = 0;
        std::memcpy(&bone_array, scene_block + CSkeletonInstance::m_modelState + bone_array_offset_,
                    sizeof(bone_array));
        if (bone_array < 0x10000) {
            continue;
        }

        uint8_t transforms[kEspBoneCount * kTransformSize];
        if (!memory_.read_bytes(bone_array, transforms, sizeof(transforms))) {
            continue;
        }

        bool all_valid = true;
        for (int bone = 0; bone < kEspBoneCount; ++bone) {
            const uint8_t* transform = transforms + bone * kTransformSize;
            out.bones[bone] = ExtractPosition(transform, bone_pos_offset_);
            if (!QuatLooksValid(transform, bone_pos_offset_)) all_valid = false;
        }
        if (!all_valid || !BonePoseLooksUsable(out.bones, kEspBoneCount, kHeadBoneIndex, out.origin)) {
            // One dormant/unreadable pawn does not invalidate a layout shared by all players.
            continue;
        }

        out.bones_valid = true;
        return true;
    }
    out.bones_valid = false;
    return false;
}

// pawn -> weapon services -> active weapon handle -> weapon entity -> item definition id
void GameReader::ReadWeapon(uintptr_t pawn, uintptr_t entity_list, uintptr_t controller, int slot,
                            EspPlayer& out) {
    out.weapon[0] = '\0';
    out.weapon_icon[0] = '\0';
    out.reloading = false;
    out.ammo      = 0;
    out.ammo_max  = 0;

    const uintptr_t services = memory_.read_or<uintptr_t>(pawn + C_BasePlayerPawn::m_pWeaponServices, 0);
    if (!services) return;

    const uint32_t handle = memory_.read_or<uint32_t>(services + CPlayer_WeaponServices::m_hActiveWeapon, 0);
    if (handle == 0 || handle == 0xFFFFFFFF) return;

    // the weapon entity stays put while the handle does: resolve it only on a swap
    WeaponCache& cache = weapon_cache_[slot];
    if (cache.controller != controller || cache.handle != handle || cache.weapon == 0) {
        cache.controller = controller;
        cache.handle     = handle;
        cache.weapon     = ResolvePawn(entity_list, handle);
    }
    if (!cache.weapon) return;

    uint8_t block[kWeaponBlockSize] = {};
    if (!memory_.read_bytes(cache.weapon + kWeaponBlockBase, block, sizeof(block))) {
        cache.weapon = 0;   // dropped/recycled: resolve again next frame
        return;
    }

    uint16_t index = 0;
    std::memcpy(&index, block, sizeof(index));

    const WeaponName* entry = WeaponOf(index);
    if (!entry) return;

    std::snprintf(out.weapon, sizeof(out.weapon), "%s", entry->name);
    out.weapon_icon[0] = entry->icon;
    out.weapon_icon[1] = '\0';
    out.reloading = block[C_CSWeaponBase::m_bInReload - kWeaponBlockBase] != 0;

    out.ammo_max = entry->clip;
    int32_t clip = 0;
    std::memcpy(&clip, block + (C_BasePlayerWeapon::m_iClip1 - kWeaponBlockBase), sizeof(clip));
    out.ammo = std::clamp(clip, 0, std::max(out.ammo_max, 0));
}

// dead controller -> observer pawn -> observer services -> who it watches
void GameReader::ReadObserver(uintptr_t controller, const uint8_t* ctrl, uintptr_t entity_list,
                              const uint8_t* slot_records, int stride, uint32_t observer_pawn,
                              PlayerObserver& out) const {
    out = PlayerObserver{};
    out.controller = controller;

    out.team = ctrl[C_BaseEntity::m_iTeamNum - kCtrlBlockBase];
    constexpr int kNameField = CBasePlayerController::m_iszPlayerName - kCtrlBlockBase;
    static_assert(kNameField + 128 <= kCtrlBlockSize, "name outside controller block");
    std::memcpy(out.name, ctrl + kNameField, sizeof(out.name));
    out.name[sizeof(out.name) - 1] = '\0';
    ToLowerAscii(out.name);

    const uintptr_t pawn = ResolveSlotEntity(entity_list, slot_records, stride, observer_pawn);
    if (!pawn) return;

    const uintptr_t services = memory_.read_or<uintptr_t>(pawn + C_BasePlayerPawn::m_pObserverServices, 0);
    if (!services) return;

    // 4 = in-eye, 5 = chase; roaming free-camera spectators watch nobody
    const uint8_t mode = memory_.read_or<uint8_t>(services + CPlayer_ObserverServices::m_iObserverMode, 0);
    if (mode != 4 && mode != 5) return;

    const uint32_t target = memory_.read_or<uint32_t>(services + CPlayer_ObserverServices::m_hObserverTarget, 0);
    if (target == 0 || target == 0xFFFFFFFF) return;

    out.target = target;
}

void GameReader::ReadBomb(GameFrame& frame) const {
    // carried bomb: the c4 weapon entity knows its owner
    const uintptr_t weapon_c4 = memory_.read_or<uintptr_t>(client_base_ + sdk::dwWeaponC4, 0);
    if (weapon_c4) {
        const uint32_t owner = memory_.read_or<uint32_t>(weapon_c4 + C_BaseEntity::m_hOwnerEntity, 0);
        if (owner != 0 && owner != 0xFFFFFFFF)
            frame.c4_owner_handle = owner;
    }

    const uintptr_t planted = memory_.read_or<uintptr_t>(client_base_ + sdk::dwPlantedC4, 0);
    if (!planted) return;

    // ticking .. defused lives inside 0x55 bytes: one read covers the whole state
    uint8_t state[0x58] = {};
    if (!memory_.read_bytes(planted + C_PlantedC4::m_bBombTicking, state, sizeof(state))) return;

    auto field = [&](std::ptrdiff_t offset) { return state + (offset - C_PlantedC4::m_bBombTicking); };

    if (field(C_PlantedC4::m_bBombTicking)[0] == 0) return;
    if (field(C_PlantedC4::m_bHasExploded)[0] != 0) return;
    if (field(C_PlantedC4::m_bBombDefused)[0] != 0) return;

    int32_t site = -1;
    std::memcpy(&site, field(C_PlantedC4::m_nBombSite), sizeof(site));

    float blow  = 0.0f;
    float timer = 0.0f;
    std::memcpy(&blow, field(C_PlantedC4::m_flC4Blow), sizeof(blow));
    std::memcpy(&timer, field(C_PlantedC4::m_flTimerLength), sizeof(timer));
    if (!std::isfinite(blow) || !std::isfinite(timer) || timer <= 0.0f) return;

    // the c4 entity is networked every tick: its simulation time tracks the server clock
    const float now = memory_.read_or<float>(planted + C_BaseEntity::m_flSimulationTime, 0.0f);
    if (!std::isfinite(now) || now <= 0.0f) return;

    frame.bomb.planted   = true;
    frame.bomb.site      = (site == 0 || site == 1) ? site : -1;
    frame.bomb.remaining = std::clamp(blow - now, 0.0f, timer);

    frame.bomb.being_defused = field(C_PlantedC4::m_bBeingDefused)[0] != 0;
    if (frame.bomb.being_defused) {
        float countdown = 0.0f;
        std::memcpy(&countdown, field(C_PlantedC4::m_flDefuseCountDown), sizeof(countdown));
        if (std::isfinite(countdown) && countdown > 0.0f) {
            frame.bomb.defuse_remaining = std::clamp(countdown - now, 0.0f, timer);
            frame.bomb.defuse_succeeds  = countdown < blow;
        }
    }
}

void GameReader::ResetWeaponScan() {
    scan_pending_.clear();
    dropped_weapons_.clear();
    scan_chunk_records_.clear();
    scan_active_         = false;
    scan_chunk_loaded_   = false;
    scan_chunk_          = 0;
    scan_chunk_count_    = 0;
    scan_slot_           = 0;
    scan_highest_slot_   = 0;
    next_weapon_scan_ms_ = 0;
    last_scan_tick_ms_   = 0;
}

// walks the entity list a slice at a time: every timer tick a bounded number of entity
// pointers is inspected, so a full pass never stalls the overlay
void GameReader::ScanDroppedWeaponsStep(uintptr_t entity_list) {
    const unsigned long long tick = GetTickCount64();
    if (tick == last_scan_tick_ms_) return;
    last_scan_tick_ms_ = tick;

    constexpr int kChunkSlots = 1 << sdk::kEntityChunkShift;

    if (!scan_active_) {
        if (tick < next_weapon_scan_ms_) return;

        const int32_t highest = memory_.read_or<int32_t>(
            entity_list + sdk::dwGameEntitySystem_highestEntityIndex, -1);
        if (highest < 1) {
            next_weapon_scan_ms_ = tick + 2000;   // list not resolved yet: retry later
            return;
        }

        scan_highest_slot_ = std::min<int>(highest, kChunkSlots * 32 - 1);
        scan_chunk_count_  = (scan_highest_slot_ >> sdk::kEntityChunkShift) + 1;
        scan_chunk_        = 0;
        scan_slot_         = 0;
        scan_chunk_loaded_ = false;
        scan_pending_.clear();
        scan_active_       = true;
    }

    auto finish_pass = [&]() {
        scan_active_ = false;
        scan_chunk_records_.clear();
        dropped_weapons_.clear();
        dropped_weapons_.reserve(scan_pending_.size());

        for (const ScannedWeapon& found : scan_pending_) {
            const WeaponName* entry = WeaponOf(found.item_index);
            if (!entry) continue;

            EspWeapon weapon;
            weapon.origin = found.origin;
            std::snprintf(weapon.name, sizeof(weapon.name), "%s", entry->name);
            weapon.icon[0] = entry->icon;
            dropped_weapons_.push_back(weapon);
        }

        scan_pending_.clear();
        next_weapon_scan_ms_ = GetTickCount64() + 2000;
    };

    int budget = 192;   // entity pointers inspected per slice
    while (budget > 0 && scan_active_) {
        if (!scan_chunk_loaded_) {
            scan_chunk_records_.assign(static_cast<size_t>(kChunkSlots) * entity_stride_, 0);
            const uintptr_t chunk = memory_.read_or<uintptr_t>(
                entity_list + sdk::kEntityChunkOffset + 8 * scan_chunk_, 0);

            if (!chunk || !memory_.read_bytes(chunk, scan_chunk_records_.data(), scan_chunk_records_.size())) {
                ++scan_chunk_;
                scan_slot_ = 0;
                if (scan_chunk_ >= scan_chunk_count_) finish_pass();
                continue;
            }

            scan_chunk_loaded_ = true;
            scan_slot_         = 0;
        }

        const int chunk_base = scan_chunk_ << sdk::kEntityChunkShift;

        while (budget > 0 && scan_slot_ < kChunkSlots && chunk_base + scan_slot_ <= scan_highest_slot_) {
            uintptr_t entity = 0;
            std::memcpy(&entity, scan_chunk_records_.data() + static_cast<size_t>(entity_stride_) * scan_slot_,
                        sizeof(entity));
            ++scan_slot_;
            --budget;
            if (entity < 0x10000) continue;

            const uint16_t item_index = memory_.read_or<uint16_t>(entity + kItemDefOffset, 0);
            const WeaponName* entry = WeaponOf(item_index);
            if (!entry || IsKnife(item_index) || item_index == 49) continue;   // 49 = the c4 itself

            // carried weapons belong to a pawn; only ground weapons have no owner
            if (memory_.read_or<uint32_t>(entity + C_BaseEntity::m_hOwnerEntity, 0xFFFFFFFF) != 0xFFFFFFFF)
                continue;

            const uintptr_t body = memory_.read_or<uintptr_t>(entity + C_BaseEntity::m_CBodyComponent, 0);
            if (!body) continue;
            const uintptr_t scene = memory_.read_or<uintptr_t>(body + CBodyComponent::m_pSceneNode, 0);
            if (!scene) continue;

            const Vec3 origin = memory_.read_or<Vec3>(scene + CGameSceneNode::m_vecAbsOrigin, Vec3{});
            if (!OriginLooksValid(origin)) continue;

            scan_pending_.push_back({ origin, item_index });
        }

        if (scan_slot_ >= kChunkSlots || chunk_base + scan_slot_ > scan_highest_slot_) {
            ++scan_chunk_;
            scan_chunk_loaded_ = false;
            scan_slot_         = 0;
            if (scan_chunk_ >= scan_chunk_count_) finish_pass();
        }
    }
}

bool GameReader::ReadPlayer(uintptr_t controller, const uint8_t* ctrl, uintptr_t entity_list,
                            const uint8_t* slot_records, int stride, uintptr_t local_pawn,
                            const Matrix4x4& view, int slot, EspPlayer& out) {
    out.controller = controller;

    constexpr int kPawnField  = CCSPlayerController::m_hPlayerPawn - kCtrlBlockBase;
    constexpr int kAliveField = CCSPlayerController::m_bPawnIsAlive - kCtrlBlockBase;
    constexpr int kKitField   = CCSPlayerController::m_bPawnHasDefuser - kCtrlBlockBase;
    constexpr int kNameField  = CBasePlayerController::m_iszPlayerName - kCtrlBlockBase;
    constexpr int kMoneyField = CCSPlayerController::m_pInGameMoneyServices - kCtrlBlockBase;
    constexpr int kPingField  = CCSPlayerController::m_iPing - kCtrlBlockBase;
    static_assert(kNameField + 128 <= kCtrlBlockSize, "name outside controller block");
    static_assert(kMoneyField + 8 <= kCtrlBlockSize, "money pointer outside controller block");
    static_assert(kPingField + 4 <= kCtrlBlockSize, "ping outside controller block");

    uint32_t pawn_handle = 0;
    std::memcpy(&pawn_handle, ctrl + kPawnField, sizeof(pawn_handle));
    out.pawn_handle = pawn_handle;
    out.has_kit     = ctrl[kKitField] != 0;
    if (ctrl[kAliveField] == 0) return false;

    const uintptr_t pawn = ResolveSlotEntity(entity_list, slot_records, stride, pawn_handle);
    if (!pawn || pawn == local_pawn) return false;

    // body component, vitals, flags and velocity: one read
    uint8_t pawn_near[kPawnNearSize] = {};
    if (!memory_.read_bytes(pawn + kPawnNearBase, pawn_near, sizeof(pawn_near))) return false;

    int32_t health = 0;
    std::memcpy(&health, pawn_near + (C_BaseEntity::m_iHealth - kPawnNearBase), sizeof(health));
    const uint8_t life_state = pawn_near[C_BaseEntity::m_lifeState - kPawnNearBase];
    if (health <= 0 || health > 100 || life_state != sdk::kLifeAlive) return false;

    out.health = health;
    out.team   = pawn_near[C_BaseEntity::m_iTeamNum - kPawnNearBase];

    // spotted state/mask, scoped/defusing, armor: one read
    uint8_t status[kPawnStatusSize] = {};
    if (memory_.read_bytes(pawn + kPawnStatusBase, status, sizeof(status))) {
        constexpr int kSpottedField = C_CSPlayerPawn::m_entitySpottedState - kPawnStatusBase;
        out.spotted = status[kSpottedField + EntitySpottedState_t::m_bSpotted] != 0;
        std::memcpy(out.spotted_mask,
                    status + kSpottedField + EntitySpottedState_t::m_bSpottedByMask,
                    sizeof(out.spotted_mask));
        out.scoped   = status[C_CSPlayerPawn::m_bIsScoped - kPawnStatusBase] != 0;
        out.defusing = status[C_CSPlayerPawn::m_bIsDefusing - kPawnStatusBase] != 0;

        int32_t armor = 0;
        std::memcpy(&armor, status + (C_CSPlayerPawn::m_ArmorValue - kPawnStatusBase), sizeof(armor));
        out.armor = std::clamp(armor, 0, 100);
    }

    // name and ping ride the controller block
    std::memcpy(out.name, ctrl + kNameField, sizeof(out.name));
    out.name[sizeof(out.name) - 1] = '\0';
    ToLowerAscii(out.name);

    uint32_t ping = 0;
    std::memcpy(&ping, ctrl + kPingField, sizeof(ping));
    out.ping = ping > 999 ? 0 : static_cast<int>(ping);   // an unset/garbage ping reads as zero

    // money changes slowly and only the menu shows it: refresh once a second
    uintptr_t money_services = 0;
    std::memcpy(&money_services, ctrl + kMoneyField, sizeof(money_services));
    MoneyCache& money_cache = money_cache_[slot];
    const unsigned long long now = GetTickCount64();
    if (money_cache.controller != controller) {
        money_cache.controller = controller;
        money_cache.money      = 0;
        money_cache.next_ms    = 0;
    }
    if (money_cache.next_ms == 0 || now >= money_cache.next_ms) {
        money_cache.money = money_services
            ? memory_.read_or<int32_t>(money_services + CCSPlayerController_InGameMoneyServices::m_iAccount, 0)
            : 0;
        money_cache.next_ms = now + 1000;
    }
    out.money = money_cache.money;

    ReadWeapon(pawn, entity_list, controller, slot, out);

    uintptr_t body = 0;
    std::memcpy(&body, pawn_near, sizeof(body));   // m_CBodyComponent sits at the block start
    if (!body) return false;

    const uintptr_t scene = memory_.read_or<uintptr_t>(body + CBodyComponent::m_pSceneNode, 0);
    if (!scene) return false;

    // origin and the whole model-state pointer window: one read, then bones resolve locally
    uint8_t scene_block[kSceneBlockSize] = {};
    if (!memory_.read_bytes(scene, scene_block, sizeof(scene_block))) return false;

    std::memcpy(&out.origin, scene_block + CGameSceneNode::m_vecAbsOrigin, sizeof(out.origin));
    if (out.origin.x == 0.0f && out.origin.y == 0.0f && out.origin.z == 0.0f) return false;

    // off-screen players draw nothing: their bone read is the one thing worth skipping
    out.bones_valid = BonesWorthReading(view, out.origin) && ReadBones(scene_block, out, slot);

    if (out.bones_valid) {
        // sanity: head must sit above the feet by a human amount
        const float height = out.bones[kHeadBoneIndex].z - out.origin.z;
        if (height < 20.0f || height > 100.0f)
            out.bones_valid = false;
    }

    return true;
}

void GameReader::RefreshView(GameFrame& frame) {
    if (!attached() || !client_base_) return;

    Matrix4x4 view{};
    if (!memory_.read_bytes(client_base_ + sdk::dwViewMatrix, &view, sizeof(view)))
        return;
    if (!ViewLooksUsable(view))
        return;   // keep the last good matrix (or the early cull view) on a bad read
    frame.view = view;
}

void GameReader::ReadFrame(GameFrame& frame, bool scan_dropped_weapons, bool read_observers, bool read_mouse_settings) {
    bone_scan_budget_.BeginFrame();
    frame.players.clear();
    frame.observers.clear();
    frame.dropped_weapons.clear();
    frame.has_local = false;
    frame.has_local_team = false;
    frame.local_velocity = Vec3{};
    frame.local_yaw = 0.0f;
    frame.local_sensitivity_scale = 1.0f;
    frame.has_mouse_settings = false;
    frame.mouse_settings = {};
    frame.local_team = 0;
    frame.local_on_ground = false;
    frame.local_weapon_index = 0;
    frame.local_pawn_handle = 0;
    frame.local_observer_target = 0;
    frame.c4_owner_handle = 0;
    frame.bomb = BombInfo{};

    stats_.attached          = attached();
    stats_.pid               = memory_.pid();
    stats_.entity_stride     = entity_stride_;
    stats_.bone_array_offset = bone_array_offset_;
    stats_.bone_pos_offset   = bone_pos_offset_;
    stats_.players_found     = 0;
    stats_.bones_ok          = 0;
    stats_.visible_mask      = spotted_mask_seen_;

    if (!attached() || !client_base_) return;

    if(read_mouse_settings)
        frame.has_mouse_settings = mouse_settings_reader_.Read(memory_,frame.mouse_settings);

    const uintptr_t entity_list = memory_.read_or<uintptr_t>(client_base_ + sdk::dwEntityList, 0);
    if (!entity_list) return;

    if (entity_stride_ == 0) {
        const unsigned long long now = GetTickCount64();
        if (now >= next_stride_try_ms_) {
            next_stride_try_ms_ = now + 2000;
            entity_stride_ = ResolveEntityStride(entity_list);
            stats_.entity_stride = entity_stride_;
        }
        if (entity_stride_ == 0) return;   // list layout not resolved yet
    }

    // the camera turns faster than everything else in the frame. read it once now for
    // the coarse off-screen cull, then refresh it after all entity reads (below) so
    // projection never pairs a fresh camera with old positions. the coarse cull uses
    // a +-2 NDC margin, so a few milliseconds of staleness here costs nothing.
    Matrix4x4 cull_view{};
    if (!memory_.read_bytes(client_base_ + sdk::dwViewMatrix, &cull_view, sizeof(cull_view)))
        cull_view = Matrix4x4{};
    frame.view = cull_view;   // standalone callers still get a usable matrix

    // the first chunk holds 512 records and the slot position equals the entity index,
    // so players live at records 1..64 - read that whole window in one go. the buffer
    // is sized for the widest known stride and the runtime one is clamped to it, so a
    // future layout change can never overrun the stack buffer
    const uintptr_t chunk = memory_.read_or<uintptr_t>(entity_list + sdk::kEntityChunkOffset, 0);
    if (!chunk) return;

    constexpr size_t kMaxSlotStride = 0x78;
    static_assert(sdk::kEntitySlotStride == 0x78, "widen kMaxSlotStride together with the dump");
    uint8_t slot_records[kMaxSlotStride * (sdk::kMaxPlayerSlots + 1)] = {};
    const size_t records_size = std::min<size_t>(static_cast<size_t>(entity_stride_), kMaxSlotStride) *
                                (sdk::kMaxPlayerSlots + 1);
    if (!memory_.read_bytes(chunk, slot_records, records_size)) return;

    const uintptr_t local_pawn = memory_.read_or<uintptr_t>(client_base_ + sdk::dwLocalPlayerPawn, 0);
    const uintptr_t local_controller = memory_.read_or<uintptr_t>(client_base_ + sdk::dwLocalPlayerController, 0);

    // the controller outlives the pawn, so the team stays known while dead/spectating;
    // its whole block (team, name, money, ping, handles, kit) arrives in one read
    uint8_t local_ctrl[kCtrlBlockSize] = {};
    const bool local_ctrl_ok = local_controller &&
        memory_.read_bytes(local_controller + kCtrlBlockBase, local_ctrl, sizeof(local_ctrl));

    int local_team = 0;
    if (local_ctrl_ok)
        local_team = local_ctrl[C_BaseEntity::m_iTeamNum - kCtrlBlockBase];
    if (local_team == 0 && local_pawn)
        local_team = memory_.read_or<uint8_t>(local_pawn + C_BaseEntity::m_iTeamNum, 0);
    if (local_team > 0) {
        frame.local_team     = local_team;
        frame.has_local_team = true;
    }

    constexpr int kPawnField = CCSPlayerController::m_hPlayerPawn - kCtrlBlockBase;
    constexpr int kObsField  = CCSPlayerController::m_hObserverPawn - kCtrlBlockBase;
    if (local_ctrl_ok)
        std::memcpy(&frame.local_pawn_handle, local_ctrl + kPawnField, sizeof(frame.local_pawn_handle));

    // alive local pawn: aim/trigger/quickstop only run while it exists
    if (local_pawn) {
        uint8_t local_near[kPawnNearSize] = {};
        if (memory_.read_bytes(local_pawn + kPawnNearBase, local_near, sizeof(local_near))) {
            int32_t health = 0;
            std::memcpy(&health, local_near + (C_BaseEntity::m_iHealth - kPawnNearBase), sizeof(health));
            const uint8_t life = local_near[C_BaseEntity::m_lifeState - kPawnNearBase];
            if (health > 0 && health <= 100 && life == sdk::kLifeAlive) {
                frame.has_local = true;
                const float input_scale = memory_.read_or<float>(
                    local_pawn + C_BasePlayerPawn::m_flFOVSensitivityAdjust, 1.0f);
                if (std::isfinite(input_scale) && input_scale >= 0.01f && input_scale <= 10.0f)
                    frame.local_sensitivity_scale = input_scale;

                // quickstop inputs: ground velocity, on-ground flag and the view yaw
                // (m_angEyeAngles is a QAngle: x = pitch, y = yaw, z = roll)
                std::memcpy(&frame.local_velocity,
                            local_near + (C_BaseEntity::m_vecVelocity - kPawnNearBase),
                            sizeof(frame.local_velocity));

                uint32_t flags = 0;
                std::memcpy(&flags, local_near + (C_BaseEntity::m_fFlags - kPawnNearBase), sizeof(flags));
                frame.local_on_ground = (flags & sdk::kFlagOnGround) != 0;
                frame.local_yaw       = memory_.read_or<Vec3>(local_pawn + C_CSPlayerPawn::m_angEyeAngles,
                                                              Vec3{}).y;

                // active weapon of the local player (same chain the esp uses for everyone else)
                const uintptr_t services = memory_.read_or<uintptr_t>(
                    local_pawn + C_BasePlayerPawn::m_pWeaponServices, 0);
                if (services) {
                    const uint32_t weapon_handle = memory_.read_or<uint32_t>(
                        services + CPlayer_WeaponServices::m_hActiveWeapon, 0);
                    if (weapon_handle != 0 && weapon_handle != 0xFFFFFFFF) {
                        const uintptr_t weapon = ResolvePawn(entity_list, weapon_handle);
                        if (weapon)
                            frame.local_weapon_index = memory_.read_or<uint16_t>(
                                weapon + C_EconEntity::m_AttributeManager + C_AttributeContainer::m_Item +
                                C_EconItemView::m_iItemDefinitionIndex, 0);
                    }
                }
            }
        }
    }

    // while dead/spectating, remember who the local observer camera is following
    if (local_ctrl_ok && !frame.has_local) {
        uint32_t observer_handle = 0;
        std::memcpy(&observer_handle, local_ctrl + kObsField, sizeof(observer_handle));

        const uintptr_t observer = ResolveSlotEntity(entity_list, slot_records, entity_stride_, observer_handle);
        if (observer) {
            const uintptr_t services = memory_.read_or<uintptr_t>(
                observer + C_BasePlayerPawn::m_pObserverServices, 0);
            if (services) {
                const uint8_t mode = memory_.read_or<uint8_t>(
                    services + CPlayer_ObserverServices::m_iObserverMode, 0);
                if (mode == 4 || mode == 5) {
                    const uint32_t target = memory_.read_or<uint32_t>(
                        services + CPlayer_ObserverServices::m_hObserverTarget, 0);
                    if (target != 0 && target != 0xFFFFFFFF)
                        frame.local_observer_target = target;
                }
            }
        }
    }

    ReadBomb(frame);

    int local_slot = 0;

    for (int slot = 1; slot <= sdk::kMaxPlayerSlots; ++slot) {
        uintptr_t controller = 0;
        std::memcpy(&controller, slot_records + static_cast<size_t>(entity_stride_) * slot, sizeof(controller));
        if (!controller) continue;
        if (controller == local_controller) local_slot = slot;

        // controller block: team, name, money pointer, ping, pawn/observer handles, kit
        uint8_t ctrl[kCtrlBlockSize] = {};
        if (!memory_.read_bytes(controller + kCtrlBlockBase, ctrl, sizeof(ctrl))) continue;

        constexpr int kAliveField = CCSPlayerController::m_bPawnIsAlive - kCtrlBlockBase;
        if (ctrl[kAliveField] != 0) {
            EspPlayer player;
            if (ReadPlayer(controller, ctrl, entity_list, slot_records, entity_stride_, local_pawn,
                           cull_view, slot, player))
                frame.players.push_back(player);
            continue;
        }

        // dead/spectating controller: collect it when it watches somebody. the local
        // controller is skipped - the window lists everyone but me. the list feeds the
        // menu's spectator window, so it is not collected while that window is closed
        if (controller == local_controller || !read_observers) continue;

        uint32_t observer_handle = 0;
        std::memcpy(&observer_handle, ctrl + kObsField, sizeof(observer_handle));
        if (!observer_handle) continue;

        PlayerObserver observer;
        ReadObserver(controller, ctrl, entity_list, slot_records, entity_stride_, observer_handle, observer);
        if (observer.target != 0)
            frame.observers.push_back(observer);
    }

    // the c4 carrier (bind the flag only while the bomb is still carried)
    if (!frame.bomb.planted && frame.c4_owner_handle != 0) {
        for (EspPlayer& player : frame.players) {
            if (player.pawn_handle == frame.c4_owner_handle)
                player.has_bomb = true;
        }
    }

    // "spotted by me" is the union of both signals: the team-wide flag (what the aim
    // used before the mask existed) and, once the mask proved alive, our own bit.
    // the union flips visible no later than either signal alone.
    if (local_slot >= 1 && local_slot < sdk::kMaxPlayerSlots) {
        const int word = local_slot >> 5;                // slots 1..63 -> words 0..1
        const uint32_t bit = 1u << (local_slot & 31);

        if (!spotted_mask_seen_) {
            for (const EspPlayer& player : frame.players) {
                if (player.spotted_mask[word] & bit) {
                    spotted_mask_seen_ = true;
                    break;
                }
            }
        }

        if (spotted_mask_seen_) {
            for (EspPlayer& player : frame.players) {
                if (player.spotted_mask[word] & bit)
                    player.spotted = true;
            }
        }
    }
    stats_.visible_mask = spotted_mask_seen_;
    stats_.local_slot   = local_slot;

    stats_.players_found = static_cast<int>(frame.players.size());
    stats_.bone_array_offset = bone_array_offset_;
    stats_.bone_pos_offset = bone_pos_offset_;
    for (const EspPlayer& player : frame.players) {
        if (player.bones_valid) ++stats_.bones_ok;
    }

    if (scan_dropped_weapons) {
        ScanDroppedWeaponsStep(entity_list);
        frame.dropped_weapons = dropped_weapons_;
    } else if (scan_active_ || scan_chunk_loaded_ || !dropped_weapons_.empty() || !scan_pending_.empty()) {
        ResetWeaponScan();
    }

    // the layout can move mid-session (map change / patch): rebuild the list view
    if (frame.players.empty()) {
        if (++empty_frames_ > 600) {
            empty_frames_ = 0;
            entity_stride_ = 0;
        }
    } else {
        empty_frames_ = 0;
    }

    // last read of the frame: every position is already captured, so this camera is
    // the closest match to what the game is about to show. aim/trigger consume it
    // from here; the overlay refreshes once more immediately before drawing.
    RefreshView(frame);
}
