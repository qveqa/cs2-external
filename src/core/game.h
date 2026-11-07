// game.h - read-only match view: players, bones, view matrix
#pragma once

#include "memory.h"
#include "sdk.h"
#include "bone_scan_budget.h"
#include "mouse_settings.h"

#include <vector>

constexpr int kEspBoneCount   = 24;   // bones we pull per player (highest drawn index is 23)
constexpr int kHeadBoneIndex  = 7;    // cs2 skeleton (verified against the live build)

struct EspPlayer {
    Vec3 origin;                      // feet (scene node absolute origin)
    Vec3 bones[kEspBoneCount];        // raw bone translations (model space -> world)
    char name[128]   = {};            // controller player name (lowercase)
    char weapon[32]  = {};            // active weapon display name (lowercase)
    char weapon_icon[4] = {};         // icon-font glyph for the weapon (ascii letter)
    int  health       = 0;
    int  armor        = 0;            // m_ArmorValue, 0..100
    int  money        = 0;            // controller money services
    int  ping         = 0;            // controller ping
    int  ammo         = 0;            // active weapon m_iClip1
    int  ammo_max     = 0;            // full magazine for that weapon (0 = no bar)
    int  team         = 0;
    bool bones_valid  = false;
    bool spotted      = false;        // seen by the local player (resolved for the aim/trigger)
    uint32_t spotted_mask[2] = {};    // raw m_bSpottedByMask words (resolved in ReadFrame)
    uintptr_t controller = 0;         // stable identity for the aim's target lock
    uint32_t pawn_handle = 0;         // CHandle of the pawn (bomb carrier / spectator target)

    bool scoped     = false;          // m_bIsScoped
    bool defusing   = false;          // m_bIsDefusing
    bool has_bomb   = false;          // this pawn owns the C4 weapon
    bool has_kit    = false;          // defuse kit on the controller
    bool reloading  = false;          // active weapon m_bInReload
};

// a weapon lying on the ground (owner handle is invalid)
struct EspWeapon {
    Vec3 origin;
    char name[32] = {};               // lowercase display name
    char icon[4]  = {};               // icon-font glyph
};

// planted C4 state, resolved from dwPlantedC4 every frame
struct BombInfo {
    bool  planted         = false;
    int   site            = -1;       // 0 = a, 1 = b
    float remaining       = 0.0f;     // seconds until detonation
    bool  being_defused   = false;
    float defuse_remaining = 0.0f;    // seconds until the defuse completes
    bool  defuse_succeeds  = false;   // the defuse finishes before the blast
};

// one controller that currently observes somebody
struct PlayerObserver {
    uintptr_t controller   = 0;
    uint32_t  target       = 0;       // watched pawn handle
    int       team         = 0;
    char      name[128]    = {};      // lowercase
};

struct GameFrame {
    Matrix4x4              view;
    bool                   has_local  = false;   // local pawn alive (aim/quickstop need it)
    bool                   has_local_team = false;   // team known even while dead/spectating
    Vec3                   local_velocity;       // ground velocity (quickstop)
    float                  local_yaw = 0.0f;     // view yaw in degrees (quickstop key mapping)
    float                  local_sensitivity_scale = 1.0f; // current camera FOV input adjustment
    bool                   has_mouse_settings = false;
    GameMouseSettings      mouse_settings;
    int                    local_team = 0;
    bool                   local_on_ground = false;
    int                    local_weapon_index = 0;   // active weapon item id (per-weapon overrides)
    uint32_t               local_pawn_handle = 0;    // valid while alive
    uint32_t               local_observer_target = 0; // valid while spectating
    uint32_t               c4_owner_handle = 0;      // pawn carrying the bomb (0 = none)
    BombInfo               bomb;
    std::vector<EspPlayer> players;
    std::vector<PlayerObserver> observers;           // everyone watching someone
    std::vector<EspWeapon> dropped_weapons;
};

// what the reader currently sees: shown by the BLUE_WHALE_ESP_DEBUG build
struct ReaderStats {
    bool          attached          = false;
    unsigned long pid               = 0;
    int           entity_stride     = 0;
    int           bone_array_offset = -1;
    int           bone_pos_offset   = -1;
    int           players_found     = 0;
    int           bones_ok          = 0;
    int           local_slot        = 0;       // entity index of the local player
    bool          visible_mask      = false;   // "spotted by me" comes from the mask, not the flag
};

class GameReader {
public:
    bool attached() const { return memory_.attached(); }
    bool alive() const { return memory_.alive(); }
    bool Attach(const wchar_t* exe_name);   // attaches to the target and resolves client.dll
    void Detach();

    void ReadFrame(GameFrame& frame, bool scan_dropped_weapons = false, bool read_observers = false,
                   bool read_mouse_settings = false);

    // re-read the view matrix as late as possible: the camera is the fastest-changing
    // value in a frame, so projecting with a stale one makes the ESP trail the models
    // on sharp turns. keeps the last good matrix if the read fails.
    void RefreshView(GameFrame& frame);

    const ReaderStats& stats() const { return stats_; }

private:
    struct ScannedWeapon {
        Vec3 origin;
        int  item_index = 0;
    };

    // money changes slowly and is only shown in the menu: refreshed once a second
    struct MoneyCache {
        uintptr_t          controller = 0;
        int                money      = 0;
        unsigned long long next_ms    = 0;
    };

    // the active weapon entity is stable while the handle is unchanged
    struct WeaponCache {
        uintptr_t controller = 0;
        uint32_t  handle     = 0;
        uintptr_t weapon     = 0;
    };

    bool      ReadPlayer(uintptr_t controller, const uint8_t* ctrl, uintptr_t entity_list,
                         const uint8_t* slot_records, int stride, uintptr_t local_pawn,
                         const Matrix4x4& view, int slot, EspPlayer& out);
    void      ReadWeapon(uintptr_t pawn, uintptr_t entity_list, uintptr_t controller, int slot,
                         EspPlayer& out);
    void      ReadObserver(uintptr_t controller, const uint8_t* ctrl, uintptr_t entity_list,
                           const uint8_t* slot_records, int stride, uint32_t observer_pawn,
                           PlayerObserver& out) const;
    void      ReadBomb(GameFrame& frame) const;
    void      ScanDroppedWeaponsStep(uintptr_t entity_list);
    void      ResetWeaponScan();
    void      ResetCaches();
    uintptr_t ResolvePawn(uintptr_t entity_list, uint32_t handle) const;
    uintptr_t ResolveSlotEntity(uintptr_t entity_list, const uint8_t* slot_records, int stride,
                                uint32_t handle) const;
    int       ResolveEntityStride(uintptr_t entity_list);
    bool      ResolveBoneLayout(const uint8_t* scene_block, const Vec3& origin);
    bool      ReadBones(const uint8_t* scene_block, EspPlayer& out, int slot = 0);

    Memory    memory_;
    uintptr_t client_base_       = 0;
    int       entity_stride_     = 0;    // entity list slot stride (0x70 / 0x78), derived once
    int       bone_array_offset_ = -1;   // pointer to the bone array, inside CModelState
    int       bone_pos_offset_   = -1;   // 0x00 or 0x10 inside a 32-byte CTransform (derived once)
    BoneScanBudget bone_scan_budget_;
    MouseSettingsReader mouse_settings_reader_;
    int       empty_frames_      = 0;
    bool      spotted_mask_seen_ = false;   // the per-player mask proved alive at least once
    unsigned long long next_stride_try_ms_ = 0;
    ReaderStats stats_;
    MoneyCache  money_cache_[sdk::kMaxPlayerSlots + 1] = {};
    WeaponCache weapon_cache_[sdk::kMaxPlayerSlots + 1] = {};

    // dropped weapons are found by walking the entity list a chunk at a time; a full
    // pass spans several frames and the finished list is reused until the next pass
    std::vector<ScannedWeapon> scan_pending_;
    std::vector<EspWeapon>     dropped_weapons_;
    std::vector<uint8_t>       scan_chunk_records_;   // raw slot table of the chunk being scanned
    bool                       scan_active_       = false;
    bool                       scan_chunk_loaded_ = false;
    int                        scan_chunk_        = 0;
    int                        scan_chunk_count_  = 0;
    int                        scan_slot_         = 0;
    int                        scan_highest_slot_ = 0;
    unsigned long long         next_weapon_scan_ms_ = 0;
    unsigned long long         last_scan_tick_ms_   = 0;
};
