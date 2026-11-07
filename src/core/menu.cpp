// menu.cpp - aim (aimbot / trigger) and esp (team / enemy) subtabs; every label is lowercase
#include "menu.h"

#include "core/bind.h"

#include <windows.h>

#include <imgui.h>

#include <cstdio>

namespace {

const char* const kHitboxes[]       = { "head", "stomach" };
const char* const kTriggerTargets[] = { "head", "body" };
const char* const kBoxStyles[]      = { "default", "corner" };
const char* const kHealthStyles[]   = { "default", "gradient" };
const char* const kSkeletonStyles[] = { "default", "smooth" };
const char* const kModeNames[]      = { "always on", "on hotkey", "toggle" };
const char* const kDroppedModes[]   = { "off", "text", "icon", "icon + text" };

constexpr float kBindBoxWidth = 52.0f;

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

// compact row: 13px square on the left, whole row clickable, tight spacing
bool SmallCheckbox(const char* label, bool* value) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 start      = ImGui::GetCursorScreenPos();
    const float  row_height = ImGui::GetTextLineHeight();
    const ImVec2 text_size  = ImGui::CalcTextSize(label);
    const float  box        = 13.0f;
    const float  gap        = 7.0f;

    ImGui::InvisibleButton(label, ImVec2(box + gap + text_size.x, row_height));
    const bool clicked = ImGui::IsItemClicked();
    if (clicked)
        *value = !*value;
    const bool hovered = ImGui::IsItemHovered();

    const ImVec2 box_min(start.x, start.y + (row_height - box) * 0.5f);
    const ImVec2 box_max(box_min.x + box, box_min.y + box);

    const ImU32 fill   = *value ? IM_COL32(61, 148, 181, 255) : IM_COL32(26, 29, 33, 255);
    const ImU32 border = hovered ? IM_COL32(150, 158, 168, 255) : IM_COL32(84, 92, 102, 255);

    draw->AddRectFilled(box_min, box_max, fill, 2.0f);
    draw->AddRect(box_min, box_max, border, 2.0f);
    if (*value) {
        const ImVec2 p1(box_min.x + 3.0f, box_min.y + 6.5f);
        const ImVec2 p2(box_min.x + 5.4f, box_min.y + 9.0f);
        const ImVec2 p3(box_min.x + 10.0f, box_min.y + 3.5f);
        draw->AddLine(p1, p2, IM_COL32(255, 255, 255, 255), 1.7f);
        draw->AddLine(p2, p3, IM_COL32(255, 255, 255, 255), 1.7f);
    }
    draw->AddText(ImVec2(box_max.x + gap, start.y), ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
}

// thin rectangular slider: the fill runs from the left edge up to the value, no rounding
bool SmallSliderFloat(const char* label, float* value, float min, float max, const char* format) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(26, 29, 33, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(61, 148, 181, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(90, 190, 225, 255));

    ImGui::SetNextItemWidth(240.0f);
    const bool changed = ImGui::SliderFloat(label, value, min, max, format);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
    return changed;
}

bool SmallSliderInt(const char* label, int* value, int min, int max, const char* format) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(26, 29, 33, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(61, 148, 181, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(90, 190, 225, 255));

    ImGui::SetNextItemWidth(240.0f);
    const bool changed = ImGui::SliderInt(label, value, min, max, format);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
    return changed;
}

// ---- binds: a private-cheat style row: [x] label ........ [-]/[key] ----

// one capture at a time; 1 = aim, 2 = trigger
struct BindCapture {
    bool active    = false;
    int  slot      = 0;
    bool prev[256] = {};
};

BindCapture g_capture;

KeyBind* CaptureBind(Settings& s) {
    if (g_capture.slot == 1) return &s.aim.bind;
    if (g_capture.slot == 2) return &s.trigger.bind;
    return nullptr;
}

void BeginCapture(int slot) {
    g_capture.active = true;
    g_capture.slot   = slot;
    for (int vk = 0; vk < 256; ++vk)
        g_capture.prev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// grabs the first fresh key press; esc / del clear the bind
void UpdateBindCapture(Settings& s) {
    if (!g_capture.active) return;

    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;   // generic aliases
        const bool down  = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool fresh = down && !g_capture.prev[vk];
        g_capture.prev[vk] = down;
        if (!fresh) continue;

        if (KeyBind* bind = CaptureBind(s))
            bind->bind = (vk == VK_ESCAPE || vk == VK_DELETE) ? 0 : vk;
        g_capture.active = false;
        return;
    }
}

// right-aligned text on the current row; left click binds, right click picks the mode
void BindBox(KeyBind* bind, int slot) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - kBindBoxWidth);

    ImGui::PushID(slot);

    const ImVec2 start(ImGui::GetCursorScreenPos());
    const float  height = ImGui::GetTextLineHeight() + 3.0f;
    ImGui::InvisibleButton("bind", ImVec2(kBindBoxWidth, height));

    const bool capturing = g_capture.active && g_capture.slot == slot;
    const bool hovered   = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !g_capture.active)
        BeginCapture(slot);
    if (!capturing && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("bind_mode");

    char name[20] = {};
    if (capturing) {
        std::snprintf(name, sizeof(name), "[...]");
    } else if (bind->bind == 0) {
        std::snprintf(name, sizeof(name), "[-]");
    } else {
        char key[12] = {};
        BindKeyName(bind->bind, key, sizeof(key));
        std::snprintf(name, sizeof(name), "[%s]", key);
    }

    ImU32 color = IM_COL32(225, 228, 232, 255);   // bound
    if (bind->bind == 0 && !capturing)
        color = IM_COL32(150, 155, 163, 255);     // empty slot
    if (capturing)
        color = IM_COL32(95, 190, 225, 255);
    else if (hovered)
        color = IM_COL32(255, 255, 255, 255);

    const ImVec2 text_size = ImGui::CalcTextSize(name);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(start.x + (kBindBoxWidth - text_size.x) * 0.5f, start.y + (height - text_size.y) * 0.5f),
        color, name);

    if (hovered && !capturing)
        ImGui::SetTooltip("click: bind\nright click: activation (%s)", kModeNames[bind->mode]);

    if (ImGui::BeginPopup("bind_mode")) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(kModeNames[i], bind->mode == i))
                bind->mode = i;
        }
        ImGui::Separator();
        if (ImGui::Selectable("clear bind"))
            bind->bind = 0;
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

// checkbox and its bind box on the same row
void BindRow(const char* label, bool* value, KeyBind* bind, int slot) {
    SmallCheckbox(label, value);
    BindBox(bind, slot);
}

// per-weapon aim/trigger overrides, edited at the bottom of the aimbot tab
void DrawWeaponOverrideSection(Settings& s, int local_weapon_index);

void DrawAimbotSubTab(Settings& s, int local_weapon_index, const GameMouseSettings* mouse) {
    BindRow("enable aim", &s.aim.enabled, &s.aim.bind, 1);
    if (s.aim.enabled) {
        SmallCheckbox("spotted only", &s.aim.visible_only);
        SmallCheckbox("friendly fire", &s.aim.friendly_fire);
        SmallCheckbox("draw fov", &s.aim.draw_fov);

        SmallSliderFloat("fov", &s.aim.fov, 1.0f, 30.0f, "%.1f deg");
        SmallSliderFloat("smoothing", &s.aim.smoothing, 0.00f, 1.00f, "%.2f");
        SmallSliderFloat("humanizer", &s.aim.humanizer, 0.0f, 100.0f, "%.0f%%");
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Speed variation and light curvature. 0%% preserves the original aim.");
        SmallCheckbox("auto sensitivity", &s.aim.auto_sensitivity);
        if(s.aim.auto_sensitivity) {
            if(mouse) ImGui::TextDisabled("game sensitivity: %.3f",mouse->sensitivity);
            else ImGui::TextDisabled("Waiting for game mouse settings...");
        }
        if(!s.aim.auto_sensitivity)
            SmallSliderFloat("game sensitivity", &s.aim.sensitivity, 0.01f, 20.0f, "%.2f");
        ImGui::SetNextItemWidth(240.0f);
        ImGui::Combo("hitbox", &s.aim.hitbox, kHitboxes, IM_ARRAYSIZE(kHitboxes));
    }

    DrawWeaponOverrideSection(s, local_weapon_index);
}

void DrawTriggerSubTab(Settings& s) {
    BindRow("enable trigger", &s.trigger.enabled, &s.trigger.bind, 2);
    if (!s.trigger.enabled)
        return;

    SmallCheckbox("spotted only", &s.trigger.visible_only);
    SmallCheckbox("friendly fire", &s.trigger.friendly_fire);

    SmallSliderInt("delay ms", &s.trigger.delay_ms, 20, 250, "%d");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("target", &s.trigger.target, kTriggerTargets, IM_ARRAYSIZE(kTriggerTargets));
}

// shared per-weapon selector: shows what the local player holds and returns the
// weapon selected for editing (one selection for the whole menu)
int DrawWeaponSelector(Settings& s, int local_weapon_index, const char* combo_label) {
    static int  selected = 0;
    static char labels[kWeaponOverrideSlots][40];

    const int held_slot = WeaponOverrideSlot(local_weapon_index);
    if (local_weapon_index > 0 && held_slot >= 0) {
        ImGui::TextDisabled("now: %s - %s", kWeaponOverrideTable[held_slot].name,
                            s.weapons[held_slot].active ? "override" : "global");
    } else {
        ImGui::TextDisabled("now: %s", local_weapon_index > 0 ? "another weapon" : "unknown");
    }

    for (int i = 0; i < kWeaponOverrideSlots; ++i)
        std::snprintf(labels[i], sizeof(labels[i]), "%s%s", kWeaponOverrideTable[i].name,
                      s.weapons[i].active ? " *" : "");

    const char* items[kWeaponOverrideSlots];
    for (int i = 0; i < kWeaponOverrideSlots; ++i)
        items[i] = labels[i];

    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo(combo_label, &selected, items, kWeaponOverrideSlots);
    return selected;
}

// per-weapon aim/trigger overrides, edited at the bottom of the aimbot tab
void DrawWeaponOverrideSection(Settings& s, int local_weapon_index) {
    ImGui::Spacing();
    ImGui::PushID("weapon_override");

    const int selected = DrawWeaponSelector(s, local_weapon_index, "override for");
    WeaponOverride& ov = s.weapons[selected];

    if (SmallCheckbox("use override", &ov.active) && ov.active) {
        // seed from the working global values so tuning starts from the baseline
        ov.fov              = s.aim.fov;
        ov.smoothing        = s.aim.smoothing;
        ov.hitbox           = s.aim.hitbox;
        ov.trigger_delay_ms = s.trigger.delay_ms;
        ov.trigger_target   = s.trigger.target;
    }

    if (!ov.active) {
        ImGui::TextDisabled("no override for %s", kWeaponOverrideTable[selected].name);
        ImGui::PopID();
        return;
    }

    ImGui::TextDisabled("aim");
    SmallSliderFloat("fov", &ov.fov, 1.0f, 30.0f, "%.1f deg");
    SmallSliderFloat("smoothing", &ov.smoothing, 0.00f, 1.00f, "%.2f");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("hitbox", &ov.hitbox, kHitboxes, IM_ARRAYSIZE(kHitboxes));

    ImGui::TextDisabled("trigger");
    SmallSliderInt("delay ms", &ov.trigger_delay_ms, 20, 250, "%d");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("target", &ov.trigger_target, kTriggerTargets, IM_ARRAYSIZE(kTriggerTargets));

    ImGui::PopID();
}

// label on the left, a small swatch on the right that opens a compact picker
// (no rgb/hex input rows, no current/original preview)
void ColorRow(const char* label, float color[4]) {
    ImGui::TextUnformatted(label);

    const float size = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - size);

    ImGui::PushID(label);
    if (ImGui::ColorButton("##swatch", ImVec4(color[0], color[1], color[2], color[3]),
                           ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(size, size)))
        ImGui::OpenPopup("picker");

    if (ImGui::BeginPopup("picker")) {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::ColorPicker4("##picker", color,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_AlphaBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawSideEsp(SideEsp& side, const char* enable_label) {
    SmallCheckbox(enable_label, &side.enabled);
    if (!side.enabled)
        return;

    ImGui::SetNextItemWidth(240.0f);
    SmallCheckbox("boxes", &side.boxes);    if (side.boxes) {
        int style = side.box_style - kBoxDefault;
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::Combo("box style", &style, kBoxStyles, IM_ARRAYSIZE(kBoxStyles)))
            side.box_style = style + kBoxDefault;
    }
    SmallCheckbox("name", &side.name);
    SmallCheckbox("health", &side.health);
    if (side.health) {
        ImGui::SetNextItemWidth(240.0f);
        ImGui::Combo("health bar type", &side.health_style, kHealthStyles, IM_ARRAYSIZE(kHealthStyles));
    }
    SmallCheckbox("ammo", &side.ammo);
    SmallCheckbox("weapon icon", &side.weapon_icon);
    SmallCheckbox("weapon text", &side.weapon_text);
    SmallCheckbox("skeleton", &side.skeleton);
    if (side.skeleton) {
        ImGui::SetNextItemWidth(240.0f);
        ImGui::Combo("skeleton style", &side.skeleton_style, kSkeletonStyles, IM_ARRAYSIZE(kSkeletonStyles));
    }
    SmallCheckbox("spotted only", &side.visible_only);
    SmallCheckbox("offscreen", &side.offscreen);
    if(side.offscreen) {
        SmallSliderFloat("arrow size",&side.offscreen_size,6.0f,20.0f,"%.0f px");
        SmallSliderFloat("edge margin",&side.offscreen_margin,16.0f,160.0f,"%.0f px");
        ColorRow("arrow color",side.offscreen_color);
    }

    ImGui::Spacing();
    ColorRow("box color", side.box_color);
    ColorRow("skeleton color", side.skeleton_color);
}

void DrawFlagsSubTab(Settings& s) {
    SmallCheckbox("enable flags", &s.flags.enabled);
    if (!s.flags.enabled)
        return;

    SmallCheckbox("scoped", &s.flags.scoped);
    SmallCheckbox("defusing", &s.flags.defusing);
    SmallCheckbox("bomb", &s.flags.bomb);
    SmallCheckbox("kit", &s.flags.kit);
    SmallCheckbox("reload", &s.flags.reload);
}

void DrawOtherSubTab(Settings& s) {
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("dropped weapons", &s.other.dropped_weapons, kDroppedModes, IM_ARRAYSIZE(kDroppedModes));
    SmallCheckbox("bomb info", &s.other.bomb_info);
    SmallCheckbox("spectators", &s.other.spectators);
}

void DrawMiscTab(Settings& s) {
    SmallCheckbox("screen capture bypass", &s.misc.screen_capture_bypass);

    ImGui::Spacing();
    SmallCheckbox("enable bhop (hold space)", &s.misc.bhop.enabled);

    ImGui::Spacing();
    SmallCheckbox("enable quickstop", &s.misc.quickstop.enabled);
    if (s.misc.quickstop.enabled)
        SmallSliderInt("stop speed", &s.misc.quickstop.stop_speed, 0, 60, "%d u/s");
}

}  // namespace

void CancelBindCapture() {
    g_capture.active = false;
}

void ApplyMenuStyle() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 accent(0.24f, 0.58f, 0.71f, 1.00f);

    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 4.0f;
    style.TabRounding      = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.WindowPadding    = ImVec2(12.0f, 12.0f);
    style.FramePadding     = ImVec2(8.0f, 4.0f);
    style.ItemSpacing      = ImVec2(8.0f, 3.0f);

    style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.05f, 0.06f, 0.07f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]        = WithAlpha(accent, 0.45f);
    style.Colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.23f, 0.26f, 0.70f);
    style.Colors[ImGuiCol_SliderGrab]           = accent;
    style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.35f, 0.75f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_Header]               = WithAlpha(accent, 0.40f);
    style.Colors[ImGuiCol_HeaderHovered]        = WithAlpha(accent, 0.65f);
    style.Colors[ImGuiCol_HeaderActive]         = accent;
    style.Colors[ImGuiCol_Button]               = WithAlpha(accent, 0.35f);
    style.Colors[ImGuiCol_ButtonHovered]        = WithAlpha(accent, 0.60f);
    style.Colors[ImGuiCol_ButtonActive]         = accent;
    style.Colors[ImGuiCol_TabHovered]           = WithAlpha(accent, 0.65f);
    style.Colors[ImGuiCol_TabSelected]          = WithAlpha(accent, 0.45f);
    style.Colors[ImGuiCol_TabSelectedOverline]  = accent;
}

void DrawMenu(Settings& settings, bool& want_close, int local_weapon_index, const GameMouseSettings* mouse) {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 420.0f), ImGuiCond_FirstUseEver);

    ImGui::Begin("blue whale // external   [pgdn]",
                 nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    const bool was_capturing = g_capture.active;
    UpdateBindCapture(settings);

    // esc cancels a capture; it closes the menu only when no capture was armed
    if (!was_capturing && !g_capture.active && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        want_close = true;

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("aim")) {
            if (ImGui::BeginTabBar("aim_sub")) {
                if (ImGui::BeginTabItem("aimbot")) {
                    DrawAimbotSubTab(settings, local_weapon_index, mouse);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("trigger")) {
                    DrawTriggerSubTab(settings);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("esp")) {
            if (ImGui::BeginTabBar("esp_sub")) {
                if (ImGui::BeginTabItem("team esp")) {
                    DrawSideEsp(settings.team, "enable team esp");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("enemy esp")) {
                    DrawSideEsp(settings.enemy, "enable enemy esp");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("flags")) {
                    DrawFlagsSubTab(settings);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("other")) {
                    DrawOtherSubTab(settings);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("misc")) {
            DrawMiscTab(settings);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void DrawSpectatorsWindow(const GameFrame& frame, const Settings& settings) {
    if (!settings.other.spectators) return;

    // alive: whoever watches my pawn; dead: whoever watches the same player as my camera
    const uint32_t target = frame.has_local ? frame.local_pawn_handle : frame.local_observer_target;
    if (!target) return;

    int count = 0;
    for (const PlayerObserver& observer : frame.observers) {
        if (observer.target == target) ++count;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16.0f, 72.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_FirstUseEver);

    ImGui::Begin("spectators", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav);

    ImGui::TextDisabled("%d watching", count);

    const ImVec4 ct_color(0.55f, 0.72f, 0.95f, 1.00f);   // blue
    const ImVec4 t_color(0.86f, 0.76f, 0.52f, 1.00f);    // sand

    for (const PlayerObserver& observer : frame.observers) {
        if (observer.target != target) continue;

        const ImVec4 color = observer.team == 3 ? ct_color
                           : observer.team == 2 ? t_color
                           : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TextColored(color, "%s", observer.name[0] ? observer.name : "?");
    }

    ImGui::End();
}
