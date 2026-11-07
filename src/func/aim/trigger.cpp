// trigger.cpp - pixel-space triggerbot. bones give the hitbox centres; the pixel
// radius of each hitbox follows from the on-screen size of the player (head-to-pelvis
// span), so the check scales with distance the same way the model does.
#include "trigger.h"

#include "core/bind.h"

#include <imgui.h>
#include "core/projection.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr int kBoneHead    = 7;
constexpr int kBoneChest   = 23;
constexpr int kBoneStomach = 3;
constexpr int kBonePelvis  = 1;

constexpr float kHeadCenterUp = 4.0f;   // head bone -> skull centre, world units
constexpr float kHeadRadius   = 3.4f;   // hitbox half-widths, world units
constexpr float kChestRadius  = 5.0f;
constexpr float kStomachRadius = 4.4f;
constexpr float kPelvisRadius = 4.4f;

constexpr unsigned long long kClickHoldMs = 15;   // a click should look like one

BindRuntime        g_bind_rt;
bool               g_click_down = false;
unsigned long long g_release_ms = 0;
unsigned long long g_next_shot_ms = 0;


float WorldDistance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool SendClick(bool down) {
    INPUT input = {};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

}  // namespace

bool TriggerReleasePendingClick(bool force) {
    if (g_click_down && (force || GetTickCount64() >= g_release_ms)) {
        if (!SendClick(false)) return false;
        g_click_down = false;
        BindSuppressMouseEdges(false);
    }
    return !g_click_down;
}

void TriggerReset() {
    TriggerReleasePendingClick(true);
    g_bind_rt = BindRuntime{};
    g_next_shot_ms = 0;
}

bool ApplyTrigger(const GameFrame& frame, const TriggerSettings& trigger, HWND game_window,
                  const GameViewport& viewport) {
    TriggerReleasePendingClick();

    if (!trigger.enabled || !frame.has_local || !viewport.valid() ||
        !game_window || GetForegroundWindow() != game_window) {
        TriggerReset();
        return false;
    }
    if (!BindActive(trigger.bind, g_bind_rt)) return false;

    const ImVec2 crosshair(viewport.center_x(), viewport.center_y());

    bool on_target = false;

    for (const EspPlayer& player : frame.players) {
        if (!player.bones_valid) continue;
        if (!trigger.friendly_fire && frame.has_local_team && player.team == frame.local_team) continue;   // mates only on request
        if (trigger.visible_only && !player.spotted) continue;

        ImVec2 head_pt;
        ImVec2 pelvis_pt;
        if (!WorldToScreen(frame.view, player.bones[kBoneHead], viewport, head_pt) ||
            !WorldToScreen(frame.view, player.bones[kBonePelvis], viewport, pelvis_pt))
            continue;

        const float world_span = WorldDistance(player.bones[kBoneHead], player.bones[kBonePelvis]);
        if (world_span < 1.0f) continue;
        const float dx_span = head_pt.x - pelvis_pt.x;
        const float dy_span = head_pt.y - pelvis_pt.y;
        const float px_per_unit = std::sqrt(dx_span * dx_span + dy_span * dy_span) / world_span;

        auto inside = [&](const Vec3& world, float radius_units) {
            ImVec2 pt;
            if (!WorldToScreen(frame.view, world, viewport, pt)) return false;
            const float radius = radius_units * px_per_unit;
            const float dx = pt.x - crosshair.x;
            const float dy = pt.y - crosshair.y;
            return dx * dx + dy * dy <= radius * radius;
        };

        Vec3 head_center = player.bones[kBoneHead];
        head_center.z += kHeadCenterUp;

        if (trigger.target == 0) {
            on_target = inside(head_center, kHeadRadius);
        } else {
            on_target = inside(player.bones[kBoneChest], kChestRadius) ||
                        inside(player.bones[kBoneStomach], kStomachRadius) ||
                        inside(player.bones[kBonePelvis], kPelvisRadius);
        }

        if (on_target) break;
    }

    if (!on_target) return false;

    const unsigned long long now = GetTickCount64();
    if (g_click_down || now < g_next_shot_ms) return false;

    if (!SendClick(true)) return false;
    g_click_down = true;
    g_release_ms = now + kClickHoldMs;
    BindSuppressMouseEdges(true);   // our own press must not count as a user press

    const unsigned long long cooldown = trigger.delay_ms < 20 ? 20 : static_cast<unsigned long long>(trigger.delay_ms);
    g_next_shot_ms = now + cooldown;
    return true;
}
