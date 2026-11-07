// settings.h - menu state shared between overlay and ui modules
#pragma once

// how a bound key activates its function
enum BindMode {
    kBindAlwaysOn = 0,   // stays on, the key is ignored
    kBindOnHotkey = 1,   // active while the key is held
    kBindToggle   = 2,   // press to switch on, press again to switch off
};

// a key plus its activation mode. vk stays raw so this header needs no windows.h
// (0x01 = lmb, 0x02 = rmb, 0x05/0x06 = m4/m5, 0x0D = enter, ...)
struct KeyBind {
    int bind = 0;                 // virtual key code; 0 = unbound
    int mode = kBindOnHotkey;
};

struct AimSettings {
    bool    enabled      = false;   // master
    KeyBind bind{ 0x02, kBindOnHotkey };   // rmb held by default
    bool    visible_only  = false;  // only spotted targets
    bool    friendly_fire = false;  // also aim at teammates
    bool    draw_fov     = true;    // circle around the crosshair
    float   fov          = 5.0f;    // degrees
    float   smoothing    = 0.50f;   // 0.00 = very sharp, 1.00 = very smooth
    float   humanizer    = 0.0f;    // 0..100 percent; zero preserves unmodified correction
    float   sensitivity  = 1.25f;   // manual fallback when automatic reading is disabled
    bool    auto_sensitivity = true;
    float   mouse_yaw    = 0.022f;
    float   mouse_pitch  = 0.022f;
    int     hitbox       = 0;       // 0 = head, 1 = stomach
};

struct TriggerSettings {
    bool    enabled      = false;   // master
    KeyBind bind{ 0xA4, kBindOnHotkey };   // lalt held by default (0xA4 = left alt)
    bool    visible_only  = false;
    bool    friendly_fire = false;  // also fire on teammates
    int     delay_ms     = 60;      // minimum time between shots
    int     target       = 1;       // 0 = head, 1 = body
};

// box rendering style for one esp side (visibility is the separate `boxes` toggle)
enum BoxStyle {
    kBoxOff     = 0,   // legacy configs: read as default
    kBoxDefault = 1,   // full rectangle
    kBoxCorner  = 2,   // corner brackets only
};

// health bar fill
enum HealthBarStyle {
    kHealthSolid    = 0,   // one color picked from the health value
    kHealthGradient = 1,   // red -> green ramp, the fill shows its slice
};

// skeleton rendering style
enum SkeletonStyle {
    kSkeletonDefault = 0,   // straight 1px segments
    kSkeletonSmooth  = 1,   // joined polylines with rounded joints
};

// one side of the esp (team / enemy) with its own toggles and colors.
// by request: everything is on out of the box except the box itself
struct SideEsp {
    bool  enabled      = true;    // master
    bool  boxes        = false;   // box toggle; reveals the style selector
    int   box_style    = kBoxDefault;
    bool  name         = true;
    bool  health       = true;
    int   health_style = kHealthSolid;   // reveals the health bar type selector
    bool  ammo         = false;   // magazine bar, mirrored of the health bar
    bool  weapon_icon  = true;
    bool  weapon_text  = true;
    bool  skeleton     = true;
    int   skeleton_style = kSkeletonDefault;
    bool  visible_only = false;   // draw only spotted players
    bool  offscreen = false;
    float offscreen_size = 10.0f;
    float offscreen_margin = 32.0f;
    float offscreen_color[4] = {1.0f,0.65f,0.3f,1.0f};
    float box_color[4]      = { 1.00f, 1.00f, 1.00f, 1.00f };
    float skeleton_color[4] = { 0.90f, 0.90f, 0.90f, 1.00f };
};

// status tags drawn next to the box: one master switch plus the individual tags
struct FlagSettings {
    bool enabled  = false;
    bool scoped   = true;
    bool defusing = true;
    bool bomb     = true;
    bool kit      = true;
    bool reload   = true;
};

// how dropped weapons on the ground are labelled
enum DroppedWeaponMode {
    kDroppedOff     = 0,
    kDroppedText    = 1,
    kDroppedIcon    = 2,
    kDroppedBoth    = 3,
};

// esp extras that are not tied to a player: ground weapons, the bomb, spectators
struct OtherEspSettings {
    int  dropped_weapons = kDroppedOff;
    bool bomb_info       = true;
    bool spectators      = true;
};

// per-weapon aim/trigger values: picked up whenever the local player holds that gun
struct WeaponOverride {
    bool  active           = false;   // a private set of values for this weapon
    float fov              = 5.0f;
    float smoothing        = 0.50f;
    int   hitbox           = 0;       // 0 = head, 1 = stomach
    int   trigger_delay_ms = 60;
    int   trigger_target   = 1;       // 0 = head, 1 = body
};

struct WeaponSlot {
    int         item_index;   // cs2 item definition id
    const char* name;
};

// every gun that can carry its own values
constexpr WeaponSlot kWeaponOverrideTable[] = {
    { 7,  "ak-47" }, { 8, "aug" }, { 10, "famas" }, { 13, "galil" }, { 16, "m4a4" }, { 39, "sg 553" }, { 60, "m4a1-s" },
    { 9,  "awp" }, { 11, "g3sg1" }, { 38, "scar-20" }, { 40, "ssg 08" },
    { 1,  "deagle" }, { 2, "dual berettas" }, { 3, "five-seven" }, { 4, "glock-18" }, { 30, "tec-9" },
    { 32, "p2000" }, { 36, "p250" }, { 61, "usp-s" }, { 63, "cz75-auto" }, { 64, "r8 revolver" },
    { 17, "mac-10" }, { 19, "p90" }, { 23, "mp5-sd" }, { 24, "ump-45" }, { 26, "pp-bizon" },
    { 33, "mp7" }, { 34, "mp9" },
    { 25, "xm1014" }, { 27, "mag-7" }, { 29, "sawed-off" }, { 35, "nova" },
    { 14, "m249" }, { 28, "negev" },
};
constexpr int kWeaponOverrideSlots = static_cast<int>(sizeof(kWeaponOverrideTable) / sizeof(kWeaponOverrideTable[0]));

// external auto-jump: a fresh space press on each landing while the player moves
struct BhopSettings {
    bool enabled = false;   // master
};

// external counter-strafe: engages by itself the moment the movement keys are
// released, there is no bind. stop_speed is the ground velocity (u/s) below which
// the player counts as stopped.
struct QuickstopSettings {
    bool enabled    = false;   // master
    int  stop_speed = 25;      // u/s
};

struct MiscSettings {
    bool screen_capture_bypass = true;    // hide the overlay from every screen capture
    BhopSettings      bhop;
    QuickstopSettings quickstop;
};

struct Settings {
    AimSettings     aim;
    TriggerSettings trigger;
    SideEsp         team;
    SideEsp         enemy;
    FlagSettings    flags;
    OtherEspSettings other;
    WeaponOverride  weapons[kWeaponOverrideSlots];
    MiscSettings    misc;
};

inline int WeaponOverrideSlot(int item_index) {
    if (item_index <= 0) return -1;
    for (int i = 0; i < kWeaponOverrideSlots; ++i) {
        if (kWeaponOverrideTable[i].item_index == item_index) return i;
    }
    return -1;
}

// the settings the aim actually runs with: the weapon override wins when it exists
inline AimSettings EffectiveAim(const Settings& s, int weapon_index) {
    AimSettings aim = s.aim;
    const int slot = WeaponOverrideSlot(weapon_index);
    if (slot >= 0 && s.weapons[slot].active) {
        aim.fov       = s.weapons[slot].fov;
        aim.smoothing = s.weapons[slot].smoothing;
        aim.hitbox    = s.weapons[slot].hitbox;
    }
    return aim;
}

inline TriggerSettings EffectiveTrigger(const Settings& s, int weapon_index) {
    TriggerSettings trigger = s.trigger;
    const int slot = WeaponOverrideSlot(weapon_index);
    if (slot >= 0 && s.weapons[slot].active) {
        trigger.delay_ms = s.weapons[slot].trigger_delay_ms;
        trigger.target   = s.weapons[slot].trigger_target;
    }
    return trigger;
}

inline bool EspActive(const Settings& s) {
    return s.team.enabled || s.enemy.enabled ||
           s.other.bomb_info || s.other.dropped_weapons != kDroppedOff;
}
