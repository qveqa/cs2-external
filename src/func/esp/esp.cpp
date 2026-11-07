// esp.cpp - thin, outline-free esp: 1px box, 1px skeleton (or a smooth one), health bar + text
#include "esp.h"
#include "core/projection.h"
#include "offscreen.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// cs2 skeleton mapping, verified against the live build's bone dump (world-space joints):
//   0 root (feet)  1 pelvis  2..4 spine  6 neck  7 head  23 chest
//   8/12 clavicles  9,10,11 left arm  13,14,15 right arm
//   17,18,19 left leg  20,21,22 right leg
// every segment is length-checked before drawing, so a shifted index skips the line
// instead of painting garbage — if a limb lands wrong, this table is the single place to fix.
constexpr int kBonePairs[][2] = {
    { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 23 }, { 23, 6 }, { 6, 7 },   // pelvis -> spine -> chest -> neck -> head
    { 23, 8 }, { 8, 9 }, { 9, 10 }, { 10, 11 },                     // left arm: chest, clavicle, upper, forearm, hand
    { 23, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },                 // right arm
    { 1, 17 }, { 17, 18 }, { 18, 19 },                              // left leg: pelvis, hip, knee, foot
    { 1, 20 }, { 20, 21 }, { 21, 22 },                              // right leg
};

// the same joints as kBonePairs, but as limb chains, so smooth mode can round the
// corners where bones meet instead of stacking separate segments
constexpr int kSpineChain[]   = { 1, 2, 3, 4, 23, 6, 7 };
constexpr int kLeftArmChain[] = { 23, 8, 9, 10, 11 };
constexpr int kRightArmChain[] = { 23, 12, 13, 14, 15 };
constexpr int kLeftLegChain[] = { 1, 17, 18, 19 };
constexpr int kRightLegChain[] = { 1, 20, 21, 22 };

constexpr float kMaxSegmentLength = 30.0f;   // longest real segment is a thigh (~18); anything longer is bad data
constexpr float kLineThickness    = 1.0f;    // hairline, no outline
constexpr float kSmoothThickness  = 1.5f;    // smooth skeleton: a touch heavier so the arcs read
constexpr float kBoxWidthRatio    = 0.55f;
constexpr float kCornerArmRatio   = 0.25f;   // corner arm length relative to the shorter box side
constexpr float kSkullExtraUnits  = 8.0f;    // the head bone sits at the skull base
constexpr float kFeetExtraUnits   = 4.0f;    // boots dip below the entity origin while running/crouching
constexpr float kWeaponIconDrop   = 3.0f;    // the icon glyph rides a bit higher than the line box suggests
constexpr float kMeterGap         = 3.0f;    // bars sit this far outside the box
constexpr ImU32 kHealthLow        = IM_COL32(235, 65, 55, 255);    // gradient bar, empty end
constexpr ImU32 kHealthHigh       = IM_COL32(95, 225, 95, 255);    // gradient bar, full end
constexpr ImU32 kAmmoColor        = IM_COL32(150, 195, 240, 235);
constexpr ImU32 kMoneyColor       = IM_COL32(150, 230, 150, 235);
constexpr ImU32 kPingColor        = IM_COL32(200, 200, 200, 235);

float Clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}


float WorldDistance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

ImU32 HealthColor(int health) {
    const float t = Clamp01(static_cast<float>(health) / 100.0f);
    const float r = (t < 0.5f) ? 1.0f : 2.0f * (1.0f - t);
    const float g = (t < 0.5f) ? 2.0f * t : 1.0f;
    return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), 70, 255);
}

ImU32 MixColor(ImU32 from, ImU32 to, float t) {
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

ImVec2 TextExtent(ImFont* font, float size, const char* text) {
    return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text);
}

void DrawLabel(ImDrawList* draw, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text) {
    if (font)
        draw->AddText(font, size, pos, color, text);
    else
        draw->AddText(pos, color, text);
}

void DrawCornerBox(ImDrawList* draw, const ImVec2& box_min, const ImVec2& box_max, ImU32 color, float thickness) {
    const float width  = box_max.x - box_min.x;
    const float height = box_max.y - box_min.y;
    const float arm    = (width < height ? width : height) * kCornerArmRatio;
    if (arm < 1.0f) return;

    const ImVec2 top_right(box_max.x, box_min.y);
    const ImVec2 bottom_left(box_min.x, box_max.y);

    draw->AddLine(box_min, ImVec2(box_min.x + arm, box_min.y), color, thickness);
    draw->AddLine(box_min, ImVec2(box_min.x, box_min.y + arm), color, thickness);

    draw->AddLine(top_right, ImVec2(top_right.x - arm, top_right.y), color, thickness);
    draw->AddLine(top_right, ImVec2(top_right.x, top_right.y + arm), color, thickness);

    draw->AddLine(bottom_left, ImVec2(bottom_left.x + arm, bottom_left.y), color, thickness);
    draw->AddLine(bottom_left, ImVec2(bottom_left.x, bottom_left.y - arm), color, thickness);

    draw->AddLine(box_max, ImVec2(box_max.x - arm, box_max.y), color, thickness);
    draw->AddLine(box_max, ImVec2(box_max.x, box_max.y - arm), color, thickness);
}

// rounds every interior joint with a quadratic arc as wide as the geometry allows, then
// draws the limb as one polyline with round caps: the radius is half of the shorter
// neighbouring bone, so adjacent arcs meet exactly at the segment midpoint and no straight
// stub is left between joints
void AddSmoothChain(ImDrawList* draw, const ImVec2* points, int count, ImU32 color, float thickness) {
    if (count < 2) return;

    ImVec2 path[128];
    int length = 0;
    path[length++] = points[0];

    for (int i = 1; i + 1 < count; ++i) {
        const ImVec2& from  = points[i - 1];
        const ImVec2& joint = points[i];
        const ImVec2& to    = points[i + 1];

        const float in_x  = joint.x - from.x;
        const float in_y  = joint.y - from.y;
        const float out_x = to.x - joint.x;
        const float out_y = to.y - joint.y;
        const float in_len  = std::sqrt(in_x * in_x + in_y * in_y);
        const float out_len = std::sqrt(out_x * out_x + out_y * out_y);
        if (in_len < 0.001f || out_len < 0.001f) {
            path[length++] = joint;
            continue;
        }

        const float radius = std::min(in_len, out_len) * 0.5f;
        const ImVec2 start(joint.x - in_x / in_len * radius, joint.y - in_y / in_len * radius);
        const ImVec2 end(joint.x + out_x / out_len * radius, joint.y + out_y / out_len * radius);

        path[length++] = start;
        constexpr int kArcSteps = 12;
        for (int step = 1; step <= kArcSteps; ++step) {
            const float t = static_cast<float>(step) / kArcSteps;
            const float u = 1.0f - t;
            path[length++] = ImVec2(u * u * start.x + 2.0f * u * t * joint.x + t * t * end.x,
                                    u * u * start.y + 2.0f * u * t * joint.y + t * t * end.y);
        }
    }

    path[length++] = points[count - 1];
    draw->AddPolyline(path, length, color, ImDrawFlags_None, thickness);

    const float cap = thickness * 0.5f;
    draw->AddCircleFilled(points[0], cap, color, 6);
    draw->AddCircleFilled(points[count - 1], cap, color, 6);
}

void DrawPlayer(ImDrawList* draw, const GameFrame& frame, const SideEsp& side, const FlagSettings& flags,
                const EspPlayer& player, const GameViewport& viewport, ImU32 box_color, ImU32 skeleton_color,
                ImFont* text_font, float text_size, ImFont* icon_font, float icon_size) {
    ImVec2 feet;
    ImVec2 head;
    Vec3 head_world = player.bones_valid ? player.bones[kHeadBoneIndex]
                                       : Vec3{player.origin.x, player.origin.y, player.origin.z + 64.0f};
    if (!WorldToScreen(frame.view, player.origin, viewport, feet)) return;
    if (!WorldToScreen(frame.view, head_world, viewport, head)) return;

    const float world_height = head_world.z - player.origin.z;
    if (world_height < 5.0f || world_height > 120.0f) return;   // sanity: rejects garbage bone data

    const float pixels_per_unit = (feet.y - head.y) / world_height;
    if (pixels_per_unit <= 0.0f) return;

    // the head bone is at the skull base: stretch the box up to the crown, and let the
    // bottom dip below the origin — boots leave the origin plane while running/crouching
    const float box_top    = head.y - kSkullExtraUnits * pixels_per_unit;
    const float box_bottom = feet.y + kFeetExtraUnits * pixels_per_unit;
    const float box_height = box_bottom - box_top;
    if (box_height < 10.0f || box_height > 2000.0f) return;

    const float width = box_height * kBoxWidthRatio;
    const ImVec2 box_min(head.x - width * 0.5f, box_top);
    const ImVec2 box_max(head.x + width * 0.5f, box_bottom);

    if (box_max.x < viewport.x || box_min.x > viewport.x + viewport.width ||
        box_max.y < viewport.y || box_min.y > viewport.y + viewport.height)
        return;   // fully outside the game's client area

    if (side.boxes) {
        if (side.box_style == kBoxCorner)
            DrawCornerBox(draw, box_min, box_max, box_color, kLineThickness);
        else
            draw->AddRect(box_min, box_max, box_color, 0.0f, 0, kLineThickness);
    }

    if (side.skeleton && player.bones_valid) {
        ImVec2 points[kEspBoneCount];
        bool visible[kEspBoneCount] = {};
        for (int bone = 0; bone < kEspBoneCount; ++bone)
            visible[bone] = WorldToScreen(frame.view, player.bones[bone], viewport, points[bone]);

        const float max_length_sq = kMaxSegmentLength * kMaxSegmentLength;

        if (side.skeleton_style == kSkeletonSmooth) {
            auto draw_chain = [&](const int* chain, int count) {
                ImVec2 run[kEspBoneCount];
                int length = 0;

                auto flush = [&]() {
                    if (length >= 2)
                        AddSmoothChain(draw, run, length, skeleton_color, kSmoothThickness);
                    length = 0;
                };

                for (int i = 0; i < count; ++i) {
                    const int bone = chain[i];
                    if (!visible[bone]) {
                        flush();
                        continue;
                    }
                    if (length > 0 &&
                        WorldDistance(player.bones[chain[i - 1]], player.bones[bone]) > max_length_sq)
                        flush();
                    run[length++] = points[bone];
                }
                flush();
            };

            draw_chain(kSpineChain, IM_ARRAYSIZE(kSpineChain));
            draw_chain(kLeftArmChain, IM_ARRAYSIZE(kLeftArmChain));
            draw_chain(kRightArmChain, IM_ARRAYSIZE(kRightArmChain));
            draw_chain(kLeftLegChain, IM_ARRAYSIZE(kLeftLegChain));
            draw_chain(kRightLegChain, IM_ARRAYSIZE(kRightLegChain));
        } else {
            for (const auto& pair : kBonePairs) {
                const int a = pair[0];
                const int b = pair[1];
                if (!visible[a] || !visible[b]) continue;
                if (WorldDistance(player.bones[a], player.bones[b]) > max_length_sq) continue;
                draw->AddLine(points[a], points[b], skeleton_color, kLineThickness);
            }
        }
    }

    if (side.health) {
        const float bar_x = box_min.x - 6.0f;
        const float bar_w = 3.0f;
        const ImVec2 bar_min(bar_x, box_min.y);
        const ImVec2 bar_max(bar_x + bar_w, box_max.y);

        draw->AddRectFilled(bar_min, bar_max, IM_COL32(0, 0, 0, 120), 0.0f);
        const float fraction = Clamp01(static_cast<float>(player.health) / 100.0f);
        const float fill_top = bar_max.y - (bar_max.y - bar_min.y) * fraction;

        if (side.health_style == kHealthGradient) {
            // the bar is a fixed red -> green ramp; the fill exposes just its slice
            const float span = std::max(bar_max.y - bar_min.y, 1.0f);
            const ImU32 top = MixColor(kHealthLow, kHealthHigh, (bar_max.y - fill_top) / span);
            draw->AddRectFilledMultiColor(ImVec2(bar_min.x, fill_top), bar_max,
                                          top, top, kHealthLow, kHealthLow);
        } else {
            draw->AddRectFilled(ImVec2(bar_min.x, fill_top), bar_max, HealthColor(player.health), 0.0f);
        }

        // the number rides the top of the filled part; a healthy 100 is left as a bare bar
        if (player.health < 100) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", player.health);
            const ImVec2 text_extent = text_font ? text_font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, text)
                                                 : ImGui::CalcTextSize(text);
            const ImVec2 text_pos(bar_min.x - 4.0f - text_extent.x, fill_top - text_extent.y * 0.5f);
            if (text_font)
                draw->AddText(text_font, text_size, text_pos, IM_COL32(255, 255, 255, 240), text);
            else
                draw->AddText(text_pos, IM_COL32(255, 255, 255, 240), text);
        }
    }

    // magazine bar across the bottom edge: fills left to right
    float under_box_y = box_max.y;
    if (side.ammo && player.ammo_max > 0) {
        const ImVec2 bar_min(box_min.x, box_max.y + kMeterGap);
        const ImVec2 bar_max(box_max.x, bar_min.y + 3.0f);

        draw->AddRectFilled(bar_min, bar_max, IM_COL32(0, 0, 0, 120), 0.0f);
        if (player.ammo > 0) {
            const float fraction = Clamp01(static_cast<float>(player.ammo) / static_cast<float>(player.ammo_max));
            draw->AddRectFilled(bar_min, ImVec2(bar_min.x + (bar_max.x - bar_min.x) * fraction, bar_max.y),
                                kAmmoColor, 0.0f);
        }
        under_box_y = bar_max.y;
    }

    // info panel beside the box top: money, armor, ping
    {
        const float x = box_max.x + 4.0f;
        const float line_height = TextExtent(text_font, text_size, "0").y + 1.0f;

        char money[12];
        char armor[8];
        char ping[8];
        std::snprintf(money, sizeof(money), "%d", player.money);
        std::snprintf(armor, sizeof(armor), "%d", player.armor);
        std::snprintf(ping, sizeof(ping), "%d", player.ping);

        DrawLabel(draw, text_font, text_size, ImVec2(x, box_min.y), kMoneyColor, money);
        DrawLabel(draw, text_font, text_size, ImVec2(x, box_min.y + line_height), kMoneyColor, armor);
        DrawLabel(draw, text_font, text_size, ImVec2(x, box_min.y + 2.0f * line_height), kPingColor, ping);
    }

    // the name sits on top of the box
    if (side.name && player.name[0] != '\0') {
        const ImVec2 text_extent = text_font ? text_font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, player.name)
                                             : ImGui::CalcTextSize(player.name);
        const ImVec2 text_pos(box_min.x + (box_max.x - box_min.x - text_extent.x) * 0.5f,
                              box_min.y - text_extent.y - 2.0f);
        if (text_font)
            draw->AddText(text_font, text_size, text_pos, IM_COL32(235, 235, 235, 230), player.name);
        else
            draw->AddText(text_pos, IM_COL32(235, 235, 235, 230), player.name);
    }

    // under the box: icon first, then the text, centred as one row; the flags row
    // follows right below it
    float under_y = under_box_y + 2.0f;
    const bool show_icon = side.weapon_icon && icon_font && player.weapon_icon[0] != '\0';
    const bool show_text = side.weapon_text && player.weapon[0] != '\0';
    if (show_icon || show_text) {
        const float row_y = under_y;
        const ImVec2 text_extent = show_text ? TextExtent(text_font, text_size, player.weapon) : ImVec2(0.0f, 0.0f);
        const ImVec2 icon_extent = show_icon
            ? icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, player.weapon_icon)
            : ImVec2(0.0f, 0.0f);

        const float gap = (show_icon && show_text) ? 4.0f : 0.0f;
        float x = box_min.x + (box_max.x - box_min.x - (icon_extent.x + gap + text_extent.x)) * 0.5f;

        if (show_icon) {
            draw->AddText(icon_font, icon_size,
                          ImVec2(x, row_y + (text_extent.y - icon_extent.y) * 0.5f + kWeaponIconDrop),
                          IM_COL32(235, 235, 235, 230), player.weapon_icon);
            x += icon_extent.x + gap;
        }
        if (show_text)
            DrawLabel(draw, text_font, text_size, ImVec2(x, row_y), IM_COL32(235, 235, 235, 230), player.weapon);

        under_y = row_y + std::max(text_extent.y, icon_extent.y) + 1.0f;
    }

    if (flags.enabled) {
        char tags[96] = {};
        auto add_tag = [&](const char* tag) {
            if (tags[0] != '\0')
                std::strncat(tags, " ", sizeof(tags) - std::strlen(tags) - 1);
            std::strncat(tags, tag, sizeof(tags) - std::strlen(tags) - 1);
        };
        if (flags.scoped && player.scoped)     add_tag("scoped");
        if (flags.defusing && player.defusing) add_tag("defusing");
        if (flags.bomb && player.has_bomb)     add_tag("bomb");
        if (flags.kit && player.has_kit)       add_tag("kit");
        if (flags.reload && player.reloading)  add_tag("reload");

        if (tags[0] != '\0') {
            const ImVec2 extent = TextExtent(text_font, text_size, tags);
            const ImVec2 pos(box_min.x + (box_max.x - box_min.x - extent.x) * 0.5f, under_y);
            DrawLabel(draw, text_font, text_size, pos, IM_COL32(255, 205, 130, 235), tags);
        }
    }
}

// ground weapons: a label hovering just above each weapon's world origin
void DrawDroppedWeapons(ImDrawList* draw, const GameFrame& frame, const GameViewport& viewport, int mode,
                        ImFont* text_font, float text_size, ImFont* icon_font, float icon_size) {
    if (mode == kDroppedOff) return;

    const bool want_icon = mode == kDroppedIcon || mode == kDroppedBoth;
    const bool want_text = mode == kDroppedText || mode == kDroppedBoth;

    for (const EspWeapon& weapon : frame.dropped_weapons) {
        ImVec2 pos;
        if (!WorldToScreen(frame.view, weapon.origin, viewport, pos)) continue;
        if (pos.x < viewport.x || pos.x > viewport.x + viewport.width ||
            pos.y < viewport.y || pos.y > viewport.y + viewport.height)
            continue;

        const bool show_icon = want_icon && icon_font && weapon.icon[0] != '\0';
        const bool show_text = want_text && weapon.name[0] != '\0';
        if (!show_icon && !show_text) continue;

        const ImVec2 text_extent = show_text ? TextExtent(text_font, text_size, weapon.name) : ImVec2(0.0f, 0.0f);
        const ImVec2 icon_extent = show_icon
            ? icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, weapon.icon)
            : ImVec2(0.0f, 0.0f);

        const float gap = (show_icon && show_text) ? 4.0f : 0.0f;
        const float row_height = std::max(text_extent.y, icon_extent.y);
        const float y = pos.y - row_height - 2.0f;
        float x = pos.x - (icon_extent.x + gap + text_extent.x) * 0.5f;

        if (show_icon) {
            draw->AddText(icon_font, icon_size,
                          ImVec2(x, y + (text_extent.y - icon_extent.y) * 0.5f + kWeaponIconDrop),
                          IM_COL32(225, 225, 225, 215), weapon.icon);
            x += icon_extent.x + gap;
        }
        if (show_text)
            DrawLabel(draw, text_font, text_size, ImVec2(x, y), IM_COL32(225, 225, 225, 215), weapon.name);
    }
}

// planted c4: site + countdown at the top of the game area, with the defuse race below
void DrawBombInfo(ImDrawList* draw, const GameFrame& frame, const GameViewport& viewport,
                  ImFont* text_font, float text_size) {
    if (!frame.bomb.planted) return;

    const char site = frame.bomb.site == 0 ? 'a' : (frame.bomb.site == 1 ? 'b' : '?');

    char line[96];
    std::snprintf(line, sizeof(line), "bomb - site %c - %.1fs", site, frame.bomb.remaining);

    char defuse_line[96] = {};
    if (frame.bomb.being_defused) {
        std::snprintf(defuse_line, sizeof(defuse_line), "defusing - %.1fs%s", frame.bomb.defuse_remaining,
                      frame.bomb.defuse_succeeds ? "" : " (too late)");
    }

    const ImVec2 line_extent     = TextExtent(text_font, text_size, line);
    const ImVec2 defuse_extent   = defuse_line[0] ? TextExtent(text_font, text_size, defuse_line) : ImVec2(0.0f, 0.0f);

    const float pad  = 7.0f;
    const float gap  = defuse_line[0] ? 2.0f : 0.0f;
    const float width  = std::max(line_extent.x, defuse_extent.x) + pad * 2.0f;
    const float height = line_extent.y + gap + defuse_extent.y + pad * 2.0f;

    const ImVec2 top_left(viewport.center_x() - width * 0.5f, viewport.y + 36.0f);
    const ImVec2 bottom_right(top_left.x + width, top_left.y + height);

    draw->AddRectFilled(top_left, bottom_right, IM_COL32(8, 10, 12, 190), 4.0f);
    draw->AddRect(top_left, bottom_right, IM_COL32(61, 148, 181, 200), 4.0f);

    const ImU32 bomb_color = frame.bomb.remaining <= 10.0f ? IM_COL32(255, 105, 95, 255)
                                                           : IM_COL32(235, 235, 235, 240);
    DrawLabel(draw, text_font, text_size, ImVec2(top_left.x + pad, top_left.y + pad), bomb_color, line);

    if (defuse_line[0]) {
        const ImU32 color = frame.bomb.defuse_succeeds ? IM_COL32(150, 220, 150, 255)
                                                       : IM_COL32(255, 105, 95, 255);
        DrawLabel(draw, text_font, text_size,
                  ImVec2(top_left.x + pad, top_left.y + pad + line_extent.y + gap), color, defuse_line);
    }
}

}  // namespace

void DrawEsp(const GameFrame& frame, const Settings& settings, const GameViewport& viewport,
             ImFont* text_font, float text_size, ImFont* icon_font, float icon_size) {
    if (!viewport.valid()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(viewport.x, viewport.y),
                       ImVec2(viewport.x + viewport.width, viewport.y + viewport.height), true);

    for (const EspPlayer& player : frame.players) {
        const bool is_team = frame.has_local_team && player.team == frame.local_team;
        const SideEsp& side = is_team ? settings.team : settings.enemy;
        if (!side.enabled) continue;
        if (side.visible_only && !player.spotted) continue;
        if(side.offscreen) {
            OffscreenTriangle arrow;
            const Vec3 center{player.origin.x,player.origin.y,player.origin.z+36.0f};
            if(OffscreenGeometry(frame.view,center,viewport,side.offscreen_margin,side.offscreen_size,arrow)) {
                const float* c=side.offscreen_color;
                draw->AddTriangleFilled(arrow.tip,arrow.left,arrow.right,
                    ImGui::GetColorU32(ImVec4(c[0],c[1],c[2],c[3])));
            }
        }

        const float* box_rgba = side.box_color;
        const float* skeleton_rgba = side.skeleton_color;
        const ImU32 box_color = ImGui::GetColorU32(ImVec4(box_rgba[0], box_rgba[1], box_rgba[2], box_rgba[3]));
        const ImU32 skeleton_color = ImGui::GetColorU32(ImVec4(skeleton_rgba[0], skeleton_rgba[1], skeleton_rgba[2], skeleton_rgba[3]));

        DrawPlayer(draw, frame, side, settings.flags, player, viewport, box_color, skeleton_color, text_font,
                   text_size, icon_font, icon_size);
    }

    DrawDroppedWeapons(draw, frame, viewport, settings.other.dropped_weapons, text_font, text_size,
                       icon_font, icon_size);

    if (settings.other.bomb_info)
        DrawBombInfo(draw, frame, viewport, text_font, text_size);

    draw->PopClipRect();
}
