// config.cpp - settings.ini read/write via the win32 profile api
#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string>

namespace {

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback) {
    return ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

float ReadFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key, float fallback) {
    wchar_t buffer[64] = {};
    GetPrivateProfileStringW(section, key, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (buffer[0] == L'\0') return fallback;

    wchar_t* end = nullptr;
    const double value = std::wcstod(buffer, &end);
    if (end == buffer || !std::isfinite(value)) return fallback;
    return static_cast<float>(value);
}

void WriteValue(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value) {
    WriteValue(path, section, key, std::to_wstring(value));
}

void WriteBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool value) {
    WriteInt(path, section, key, value ? 1 : 0);
}

void WriteFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key, float value) {
    WriteValue(path, section, key, std::to_wstring(value));
}

void ReadColor(const std::wstring& path, const wchar_t* section, const wchar_t* key, float rgba[4]) {
    wchar_t buffer[160] = {};
    GetPrivateProfileStringW(section, key, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (buffer[0] == L'\0') return;

    float values[4] = {};
    if (std::swscanf(buffer, L"%f %f %f %f", &values[0], &values[1], &values[2], &values[3]) != 4)
        return;
    for (int i = 0; i < 4; ++i)
        rgba[i] = std::clamp(values[i], 0.0f, 1.0f);
}

void WriteColor(const std::wstring& path, const wchar_t* section, const wchar_t* key, const float rgba[4]) {
    std::wstring text;
    for (int i = 0; i < 4; ++i) {
        if (i != 0) text += L' ';
        text += std::to_wstring(rgba[i]);
    }
    WriteValue(path, section, key, text);
}

void LoadSide(const std::wstring& path, const wchar_t* section, SideEsp& side) {
    side.enabled      = ReadBool(path, section, L"enabled", side.enabled);
    side.boxes        = ReadBool(path, section, L"boxes", side.boxes);
    side.box_style    = std::clamp(ReadInt(path, section, L"box_style", side.box_style),
                                   static_cast<int>(kBoxDefault), static_cast<int>(kBoxCorner));
    side.name         = ReadBool(path, section, L"name", side.name);
    side.health       = ReadBool(path, section, L"health", side.health);
    side.health_style = std::clamp(ReadInt(path, section, L"health_style", side.health_style),
                                   static_cast<int>(kHealthSolid), static_cast<int>(kHealthGradient));
    side.ammo         = ReadBool(path, section, L"ammo", side.ammo);
    side.weapon_icon  = ReadBool(path, section, L"weapon_icon", side.weapon_icon);
    side.weapon_text  = ReadBool(path, section, L"weapon_text", side.weapon_text);
    side.skeleton     = ReadBool(path, section, L"skeleton", side.skeleton);
    side.skeleton_style = std::clamp(ReadInt(path, section, L"skeleton_style", side.skeleton_style), 0, 1);
    side.visible_only = ReadBool(path, section, L"visible_only", side.visible_only);
    side.offscreen = ReadBool(path,section,L"offscreen",side.offscreen);
    side.offscreen_size = std::clamp(ReadFloat(path,section,L"offscreen_size",side.offscreen_size),6.0f,20.0f);
    side.offscreen_margin = std::clamp(ReadFloat(path,section,L"offscreen_margin",side.offscreen_margin),16.0f,160.0f);
    ReadColor(path,section,L"offscreen_color",side.offscreen_color);
    ReadColor(path, section, L"box_color", side.box_color);
    ReadColor(path, section, L"skeleton_color", side.skeleton_color);
}

void SaveSide(const std::wstring& path, const wchar_t* section, const SideEsp& side) {
    WriteBool(path, section, L"enabled", side.enabled);
    WriteBool(path, section, L"boxes", side.boxes);
    WriteInt(path, section, L"box_style", side.box_style);
    WriteBool(path, section, L"name", side.name);
    WriteBool(path, section, L"health", side.health);
    WriteInt(path, section, L"health_style", side.health_style);
    WriteBool(path, section, L"ammo", side.ammo);
    WriteBool(path, section, L"weapon_icon", side.weapon_icon);
    WriteBool(path, section, L"weapon_text", side.weapon_text);
    WriteBool(path, section, L"skeleton", side.skeleton);
    WriteInt(path, section, L"skeleton_style", side.skeleton_style);
    WriteBool(path, section, L"visible_only", side.visible_only);
    WriteBool(path,section,L"offscreen",side.offscreen);
    WriteFloat(path,section,L"offscreen_size",side.offscreen_size);
    WriteFloat(path,section,L"offscreen_margin",side.offscreen_margin);
    WriteColor(path,section,L"offscreen_color",side.offscreen_color);
    WriteColor(path, section, L"box_color", side.box_color);
    WriteColor(path, section, L"skeleton_color", side.skeleton_color);
}

void LoadBind(const std::wstring& path, const wchar_t* section, KeyBind& bind) {
    bind.bind = std::clamp(ReadInt(path, section, L"bind", bind.bind), 0, 255);
    bind.mode = std::clamp(ReadInt(path, section, L"mode", bind.mode), 0, 2);
}

void SaveBind(const std::wstring& path, const wchar_t* section, const KeyBind& bind) {
    WriteInt(path, section, L"bind", bind.bind);
    WriteInt(path, section, L"mode", bind.mode);
}

}  // namespace

void LoadSettings(Settings& settings, const std::wstring& path) {
    if (path.empty()) return;

    LoadBind(path, L"aim", settings.aim.bind);
    settings.aim.enabled      = ReadBool(path, L"aim", L"enabled", settings.aim.enabled);
    settings.aim.visible_only = ReadBool(path, L"aim", L"visible_only", settings.aim.visible_only);
    settings.aim.friendly_fire = ReadBool(path, L"aim", L"friendly_fire", settings.aim.friendly_fire);
    settings.aim.draw_fov     = ReadBool(path, L"aim", L"draw_fov", settings.aim.draw_fov);
    settings.aim.fov          = std::clamp(ReadFloat(path, L"aim", L"fov", settings.aim.fov), 1.0f, 30.0f);
    settings.aim.smoothing    = std::clamp(ReadFloat(path, L"aim", L"smoothing", settings.aim.smoothing), 0.0f, 1.0f);
    settings.aim.humanizer = std::clamp(ReadFloat(path,L"aim",L"humanizer",settings.aim.humanizer),0.0f,100.0f);
    settings.aim.hitbox       = std::clamp(ReadInt(path, L"aim", L"hitbox", settings.aim.hitbox), 0, 1);
    settings.aim.sensitivity  = std::clamp(ReadFloat(path, L"aim", L"sensitivity", settings.aim.sensitivity), 0.01f, 20.0f);
    settings.aim.auto_sensitivity = ReadBool(path,L"aim",L"auto_sensitivity",settings.aim.auto_sensitivity);
    settings.aim.mouse_yaw    = std::clamp(ReadFloat(path, L"aim", L"mouse_yaw", settings.aim.mouse_yaw), 0.001f, 1.0f);
    settings.aim.mouse_pitch  = std::clamp(ReadFloat(path, L"aim", L"mouse_pitch", settings.aim.mouse_pitch), -1.0f, 1.0f);
    if (std::fabs(settings.aim.mouse_pitch) < 0.001f) settings.aim.mouse_pitch = 0.022f;

    LoadBind(path, L"trigger", settings.trigger.bind);
    settings.trigger.enabled       = ReadBool(path, L"trigger", L"enabled", settings.trigger.enabled);
    settings.trigger.visible_only  = ReadBool(path, L"trigger", L"visible_only", settings.trigger.visible_only);
    settings.trigger.friendly_fire = ReadBool(path, L"trigger", L"friendly_fire", settings.trigger.friendly_fire);
    settings.trigger.delay_ms      = std::clamp(ReadInt(path, L"trigger", L"delay_ms", settings.trigger.delay_ms), 20, 250);
    settings.trigger.target        = std::clamp(ReadInt(path, L"trigger", L"target", settings.trigger.target), 0, 1);

    LoadSide(path, L"esp.team", settings.team);
    LoadSide(path, L"esp.enemy", settings.enemy);

    settings.flags.enabled  = ReadBool(path, L"esp.flags", L"enabled", settings.flags.enabled);
    settings.flags.scoped   = ReadBool(path, L"esp.flags", L"scoped", settings.flags.scoped);
    settings.flags.defusing = ReadBool(path, L"esp.flags", L"defusing", settings.flags.defusing);
    settings.flags.bomb     = ReadBool(path, L"esp.flags", L"bomb", settings.flags.bomb);
    settings.flags.kit      = ReadBool(path, L"esp.flags", L"kit", settings.flags.kit);
    settings.flags.reload   = ReadBool(path, L"esp.flags", L"reload", settings.flags.reload);

    settings.other.dropped_weapons = std::clamp(
        ReadInt(path, L"esp.other", L"dropped_weapons", settings.other.dropped_weapons), 0, 3);
    settings.other.bomb_info  = ReadBool(path, L"esp.other", L"bomb_info", settings.other.bomb_info);
    settings.other.spectators = ReadBool(path, L"esp.other", L"spectators", settings.other.spectators);

    for (int i = 0; i < kWeaponOverrideSlots; ++i) {
        const std::wstring section = L"weapon." + std::to_wstring(kWeaponOverrideTable[i].item_index);
        WeaponOverride& ov = settings.weapons[i];
        ov.active           = ReadBool(path, section.c_str(), L"active", ov.active);
        ov.fov              = std::clamp(ReadFloat(path, section.c_str(), L"fov", ov.fov), 1.0f, 30.0f);
        ov.smoothing        = std::clamp(ReadFloat(path, section.c_str(), L"smoothing", ov.smoothing), 0.0f, 1.0f);
        ov.hitbox           = std::clamp(ReadInt(path, section.c_str(), L"hitbox", ov.hitbox), 0, 1);
        ov.trigger_delay_ms = std::clamp(ReadInt(path, section.c_str(), L"trigger_delay_ms", ov.trigger_delay_ms), 20, 250);
        ov.trigger_target   = std::clamp(ReadInt(path, section.c_str(), L"trigger_target", ov.trigger_target), 0, 1);
    }

    settings.misc.screen_capture_bypass = ReadBool(path, L"misc", L"screen_capture_bypass",
                                                   settings.misc.screen_capture_bypass);
    settings.misc.bhop.enabled         = ReadBool(path, L"misc", L"bhop_enabled",
                                                  settings.misc.bhop.enabled);
    settings.misc.quickstop.enabled    = ReadBool(path, L"misc", L"quickstop_enabled",
                                                  settings.misc.quickstop.enabled);
    settings.misc.quickstop.stop_speed = std::clamp(ReadInt(path, L"misc", L"quickstop_stop_speed",
                                                            settings.misc.quickstop.stop_speed), 0, 60);
}

void SaveSettings(const Settings& settings, const std::wstring& path) {
    if (path.empty()) return;

    SaveBind(path, L"aim", settings.aim.bind);
    WriteBool(path, L"aim", L"enabled", settings.aim.enabled);
    WriteBool(path, L"aim", L"visible_only", settings.aim.visible_only);
    WriteBool(path, L"aim", L"friendly_fire", settings.aim.friendly_fire);
    WriteBool(path, L"aim", L"draw_fov", settings.aim.draw_fov);
    WriteFloat(path, L"aim", L"fov", settings.aim.fov);
    WriteFloat(path, L"aim", L"smoothing", settings.aim.smoothing);
    WriteFloat(path,L"aim",L"humanizer",settings.aim.humanizer);
    WriteInt(path, L"aim", L"hitbox", settings.aim.hitbox);
    WriteFloat(path, L"aim", L"sensitivity", settings.aim.sensitivity);
    WriteBool(path,L"aim",L"auto_sensitivity",settings.aim.auto_sensitivity);
    WriteFloat(path, L"aim", L"mouse_yaw", settings.aim.mouse_yaw);
    WriteFloat(path, L"aim", L"mouse_pitch", settings.aim.mouse_pitch);

    SaveBind(path, L"trigger", settings.trigger.bind);
    WriteBool(path, L"trigger", L"enabled", settings.trigger.enabled);
    WriteBool(path, L"trigger", L"visible_only", settings.trigger.visible_only);
    WriteBool(path, L"trigger", L"friendly_fire", settings.trigger.friendly_fire);
    WriteInt(path, L"trigger", L"delay_ms", settings.trigger.delay_ms);
    WriteInt(path, L"trigger", L"target", settings.trigger.target);

    SaveSide(path, L"esp.team", settings.team);
    SaveSide(path, L"esp.enemy", settings.enemy);

    WriteBool(path, L"esp.flags", L"enabled", settings.flags.enabled);
    WriteBool(path, L"esp.flags", L"scoped", settings.flags.scoped);
    WriteBool(path, L"esp.flags", L"defusing", settings.flags.defusing);
    WriteBool(path, L"esp.flags", L"bomb", settings.flags.bomb);
    WriteBool(path, L"esp.flags", L"kit", settings.flags.kit);
    WriteBool(path, L"esp.flags", L"reload", settings.flags.reload);

    WriteInt(path, L"esp.other", L"dropped_weapons", settings.other.dropped_weapons);
    WriteBool(path, L"esp.other", L"bomb_info", settings.other.bomb_info);
    WriteBool(path, L"esp.other", L"spectators", settings.other.spectators);

    for (int i = 0; i < kWeaponOverrideSlots; ++i) {
        const std::wstring section = L"weapon." + std::to_wstring(kWeaponOverrideTable[i].item_index);
        const WeaponOverride& ov = settings.weapons[i];
        WriteBool(path, section.c_str(), L"active", ov.active);
        WriteFloat(path, section.c_str(), L"fov", ov.fov);
        WriteFloat(path, section.c_str(), L"smoothing", ov.smoothing);
        WriteInt(path, section.c_str(), L"hitbox", ov.hitbox);
        WriteInt(path, section.c_str(), L"trigger_delay_ms", ov.trigger_delay_ms);
        WriteInt(path, section.c_str(), L"trigger_target", ov.trigger_target);
    }

    WriteBool(path, L"misc", L"screen_capture_bypass", settings.misc.screen_capture_bypass);
    WriteBool(path, L"misc", L"bhop_enabled", settings.misc.bhop.enabled);
    WriteBool(path, L"misc", L"quickstop_enabled", settings.misc.quickstop.enabled);
    WriteInt(path, L"misc", L"quickstop_stop_speed", settings.misc.quickstop.stop_speed);
}
