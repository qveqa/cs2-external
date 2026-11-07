// config_test.cpp - offline round-trip check of settings.ini persistence.
// writes a fully populated Settings to %TEMP%, reads it back and compares field by
// field; also verifies that out-of-range values are clamped on load. no game, no windows
// except the profile api.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/config_test.cpp src/core/config.cpp -std=c++20 -O2 \
//       -DNOMINMAX -DWIN32_LEAN_AND_MEAN -I src -o %TEMP%/opencode/config_test.exe
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "core/config.h"

namespace {

int g_failed = 0;

bool Close(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void Check(bool condition, const char* name) {
    if (!condition) ++g_failed;
    std::printf("%-34s %s\n", name, condition ? "pass" : "FAIL");
}

bool SameSide(const SideEsp& a, const SideEsp& b) {
    for (int i = 0; i < 4; ++i) {
        if (!Close(a.box_color[i], b.box_color[i])) return false;
        if (!Close(a.skeleton_color[i], b.skeleton_color[i])) return false;
    }
    return a.enabled == b.enabled && a.box_style == b.box_style && a.name == b.name &&
           a.health == b.health && a.weapon_icon == b.weapon_icon && a.weapon_text == b.weapon_text &&
           a.skeleton == b.skeleton && a.skeleton_style == b.skeleton_style &&
           a.visible_only == b.visible_only;
}

std::wstring TempFile(const wchar_t* name) {
    wchar_t directory[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    const std::wstring path = std::wstring(directory) + name;
    DeleteFileW(path.c_str());
    return path;
}

Settings MakeCustom() {
    Settings s;

    s.aim.enabled       = true;
    s.aim.bind          = KeyBind{ 'G', kBindToggle };
    s.aim.visible_only  = true;
    s.aim.friendly_fire = true;
    s.aim.draw_fov      = false;
    s.aim.fov           = 12.5f;
    s.aim.smoothing     = 0.25f;
    s.aim.humanizer     = 37.0f;
    s.aim.hitbox        = 1;
    s.aim.sensitivity   = 1.75f;
    s.aim.auto_sensitivity = false;
    s.aim.mouse_yaw     = 0.0165f;
    s.aim.mouse_pitch   = -0.022f;

    s.trigger.enabled       = true;
    s.trigger.bind          = KeyBind{ 'H', kBindAlwaysOn };
    s.trigger.visible_only  = true;
    s.trigger.friendly_fire = true;
    s.trigger.delay_ms      = 35;
    s.trigger.target        = 0;

    s.team.enabled      = false;
    s.team.box_style    = kBoxCorner;
    s.team.name         = false;
    s.team.health       = false;
    s.team.weapon_icon  = false;
    s.team.weapon_text  = false;
    s.team.skeleton     = false;
    s.team.skeleton_style = kSkeletonSmooth;
    s.team.visible_only = true;
    s.team.offscreen=true;s.team.offscreen_size=14;s.team.offscreen_margin=48;
    s.team.offscreen_color[0]=0.2f;
    s.team.box_color[0] = 0.10f; s.team.box_color[1] = 0.20f;
    s.team.box_color[2] = 0.30f; s.team.box_color[3] = 0.40f;
    s.team.skeleton_color[0] = 0.50f; s.team.skeleton_color[1] = 0.60f;
    s.team.skeleton_color[2] = 0.70f; s.team.skeleton_color[3] = 0.80f;

    s.enemy.box_style    = kBoxDefault;
    s.enemy.health       = false;
    s.enemy.visible_only = true;

    s.flags.enabled  = true;
    s.flags.scoped   = false;
    s.flags.defusing = false;
    s.flags.bomb     = false;
    s.flags.kit      = false;
    s.flags.reload   = false;

    s.other.dropped_weapons = kDroppedIcon;
    s.other.bomb_info       = false;
    s.other.spectators      = false;

    s.weapons[2].active           = true;
    s.weapons[2].fov              = 7.5f;
    s.weapons[2].smoothing        = 0.75f;
    s.weapons[2].hitbox           = 1;
    s.weapons[2].trigger_delay_ms = 45;
    s.weapons[2].trigger_target   = 0;

    s.misc.screen_capture_bypass    = false;
    s.misc.quickstop.enabled        = true;
    s.misc.quickstop.stop_speed     = 40;

    return s;
}

}  // namespace

int main() {
    const std::wstring path = TempFile(L"bluewhale_config_test.ini");

    const Settings written = MakeCustom();
    SaveSettings(written, path);

    Settings loaded;
    LoadSettings(loaded, path);

    Check(loaded.aim.enabled && loaded.aim.bind.bind == 'G' && loaded.aim.bind.mode == kBindToggle,
          "aim master + bind");
    Check(loaded.aim.visible_only && loaded.aim.friendly_fire && !loaded.aim.draw_fov,
          "aim flags");
    Check(Close(loaded.aim.fov, 12.5f) && Close(loaded.aim.smoothing, 0.25f) && loaded.aim.hitbox == 1,
          "aim values");
    Check(Close(loaded.aim.humanizer,37.0f),"humanizer percent round-trip");
    Check(loaded.team.offscreen && Close(loaded.team.offscreen_size,14) &&
          Close(loaded.team.offscreen_margin,48) && Close(loaded.team.offscreen_color[0],0.2f),"offscreen settings round-trip");
    Check(Close(loaded.aim.sensitivity, 1.75f) && Close(loaded.aim.mouse_yaw, 0.0165f) &&
          Close(loaded.aim.mouse_pitch, -0.022f) && !loaded.aim.auto_sensitivity, "mouse scale round-trip");

    Check(loaded.trigger.enabled && loaded.trigger.bind.bind == 'H' &&
          loaded.trigger.bind.mode == kBindAlwaysOn,
          "trigger master + bind");
    Check(loaded.trigger.visible_only && loaded.trigger.friendly_fire &&
          loaded.trigger.delay_ms == 35 && loaded.trigger.target == 0,
          "trigger values");

    Check(SameSide(loaded.team, written.team), "team esp side");
    Check(SameSide(loaded.enemy, written.enemy), "enemy esp side");
    Check(loaded.team.skeleton_style == kSkeletonSmooth && loaded.enemy.skeleton_style == kSkeletonDefault,
          "skeleton style");

    Check(loaded.flags.enabled && !loaded.flags.scoped && !loaded.flags.defusing &&
          !loaded.flags.bomb && !loaded.flags.kit && !loaded.flags.reload,
          "flag settings");
    Check(loaded.other.dropped_weapons == kDroppedIcon && !loaded.other.bomb_info &&
          !loaded.other.spectators,
          "other esp settings");

    Check(loaded.weapons[2].active && Close(loaded.weapons[2].fov, 7.5f) &&
          Close(loaded.weapons[2].smoothing, 0.75f) && loaded.weapons[2].hitbox == 1 &&
          loaded.weapons[2].trigger_delay_ms == 45 && loaded.weapons[2].trigger_target == 0,
          "weapon override");
    Check(!loaded.weapons[0].active, "untouched weapon slot");

    Check(!loaded.misc.screen_capture_bypass && loaded.misc.quickstop.enabled &&
          loaded.misc.quickstop.stop_speed == 40,
          "misc settings");

    DeleteFileW(path.c_str());

    // out-of-range values must be clamped to the ui/engine ranges
    const std::wstring clamp_path = TempFile(L"bluewhale_config_test_clamp.ini");
    Settings wild;
    wild.aim.bind.bind         = 999;
    wild.aim.bind.mode         = 7;
    wild.aim.fov               = 100.0f;
    wild.aim.smoothing         = -2.0f;
    wild.aim.humanizer=300;wild.team.offscreen_size=100;wild.team.offscreen_margin=-100;
    wild.aim.sensitivity       = 0.0f;
    wild.aim.mouse_yaw         = 0.0f;
    wild.aim.mouse_pitch       = 0.0f;
    wild.trigger.delay_ms      = 1;
    wild.team.box_style        = 9;
    wild.team.skeleton_style   = 9;
    wild.other.dropped_weapons = 99;
    wild.weapons[0].fov        = 0.0f;
    wild.misc.quickstop.stop_speed = 1000;
    SaveSettings(wild, clamp_path);

    Settings clamped;
    LoadSettings(clamped, clamp_path);
    DeleteFileW(clamp_path.c_str());

    Check(clamped.aim.bind.bind == 255 && clamped.aim.bind.mode == 2, "bind/mode clamped");
    Check(Close(clamped.aim.fov, 30.0f) && Close(clamped.aim.smoothing, 0.0f), "aim values clamped");
    Check(Close(clamped.aim.humanizer,100) && Close(clamped.team.offscreen_size,20) &&
          Close(clamped.team.offscreen_margin,16),"humanizer/offscreen values clamped");
    Check(Close(clamped.aim.sensitivity, 0.01f) && Close(clamped.aim.mouse_yaw, 0.001f) &&
          Close(clamped.aim.mouse_pitch, 0.022f), "invalid mouse scales repaired");
    Check(clamped.trigger.delay_ms == 20, "trigger delay clamped");
    Check(clamped.team.box_style == 2, "box style clamped");
    Check(clamped.team.skeleton_style == kSkeletonSmooth, "skeleton style clamped");
    Check(clamped.other.dropped_weapons == kDroppedBoth, "dropped mode clamped");
    Check(Close(clamped.weapons[0].fov, 1.0f), "override fov clamped");
    Check(clamped.misc.quickstop.stop_speed == 60, "stop speed clamped");

    // a missing file keeps the built-in defaults
    Settings untouched;
    LoadSettings(untouched, L"Z:\\does-not-exist\\bluewhale_settings.ini");
    Check(untouched.aim.enabled == Settings{}.aim.enabled &&
          untouched.enemy.box_style == Settings{}.enemy.box_style,
          "missing file keeps defaults");
    Check(untouched.other.dropped_weapons == kDroppedOff && untouched.other.bomb_info &&
          untouched.other.spectators && !untouched.flags.enabled,
          "missing file keeps esp extras");

    std::printf("%s\n", g_failed == 0 ? "all config cases pass" : "config cases FAILED");
    return g_failed == 0 ? 0 : 1;
}
