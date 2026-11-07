// aim.cpp - time-based angular aim correction with fractional mouse counts.
#include "aim.h"

#include "core/bind.h"
#include "func/aim/fov.h"
#include "mouse_math.h"
#include "humanizer.h"

#include <imgui.h>
#include "core/projection.h"

#include <cmath>
#include <cstdint>

namespace {

constexpr int kBoneHead    = 7;
constexpr int kBoneStomach = 3;

constexpr float kTauSharp  = 0.015f;    // s: time constant at smoothing 0.00
constexpr float kTauSmooth = 0.350f;    // s: time constant at smoothing 1.00
constexpr float kDeadzone  = 2.0f;      // px: closer than this the mouse is left alone
constexpr float kMinDt     = 0.0005f;   // s
constexpr float kMaxDt     = 0.050f;    // s


int HitboxBone(int hitbox) {
    return hitbox == 1 ? kBoneStomach : kBoneHead;
}

// per-hold state: the locked target and the sub-pixel remainder
struct AimState {
    AimHumanizer humanizer;
    float sensitivity = 0, yaw = 0, pitch = 0;
    uintptr_t locked_controller = 0;
    float     rest_x            = 0.0f;
    float     rest_y            = 0.0f;
    long long last_qpc          = 0;
};

AimState g_state;
BindRuntime g_bind_rt;

void ResetAim() {
    g_state.humanizer.Reset();
    g_state.locked_controller = 0;
    g_state.rest_x            = 0.0f;
    g_state.rest_y            = 0.0f;
}

// first-order gain: the fraction of the error to correct over dt with time constant tau
float Gain(float dt, float tau) {
    return 1.0f - std::exp(-dt / tau);
}

// measured frame delta (clamped against hitches): frame-rate independence lives here
float FrameDt() {
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);

    float dt = kMinDt;
    if (g_state.last_qpc != 0)
        dt = static_cast<float>(static_cast<double>(now.QuadPart - g_state.last_qpc) /
                                static_cast<double>(freq.QuadPart));
    g_state.last_qpc = now.QuadPart;

    if (dt < kMinDt) dt = kMinDt;
    if (dt > kMaxDt) dt = kMaxDt;
    return dt;
}

// fractional glide: the remainder carries over so slow smoothing still moves the mouse
bool MoveMouse(float dx, float dy, bool immediate = false) {
    if (immediate) {
        // Snap to the nearest count without integrating residuals: repeated reads
        // of a sub-count error must not produce a one-count oscillation.
        g_state.rest_x = dx;
        g_state.rest_y = dy;
    } else {
        g_state.rest_x += dx;
        g_state.rest_y += dy;
    }

    const int steps_x = immediate ? static_cast<int>(std::lround(g_state.rest_x)) : static_cast<int>(g_state.rest_x);
    const int steps_y = immediate ? static_cast<int>(std::lround(g_state.rest_y)) : static_cast<int>(g_state.rest_y);
    if (steps_x == 0 && steps_y == 0) return false;

    INPUT input = {};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx      = steps_x;
    input.mi.dy      = steps_y;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        // Retain only fractions: retrying whole stale movements would cause a burst.
        g_state.rest_x -= static_cast<float>(steps_x);
        g_state.rest_y -= static_cast<float>(steps_y);
        return false;
    }
    if(immediate) g_state.rest_x=g_state.rest_y=0;
    else {
        g_state.rest_x -= static_cast<float>(steps_x);
        g_state.rest_y -= static_cast<float>(steps_y);
    }
    return true;
}

}  // namespace

void AimReset() {
    g_state = AimState{};
    g_bind_rt = BindRuntime{};
}

bool ApplyAim(const GameFrame& frame, const AimSettings& aim, HWND game_window,
              const GameViewport& viewport) {
    if (!aim.enabled || !frame.has_local || !viewport.valid() ||
        !game_window || GetForegroundWindow() != game_window) {
        AimReset();
        return false;
    }

    const float dt = FrameDt();   // always tick the clock, even while the bind is up

    if(aim.auto_sensitivity && !frame.has_mouse_settings) {ResetAim();return false;}
    const float sensitivity=(aim.auto_sensitivity ? frame.mouse_settings.sensitivity : aim.sensitivity)*frame.local_sensitivity_scale;
    const float yaw=aim.auto_sensitivity ? frame.mouse_settings.yaw : aim.mouse_yaw;
    const float pitch=aim.auto_sensitivity ? frame.mouse_settings.pitch : aim.mouse_pitch;
    if(sensitivity!=g_state.sensitivity || yaw!=g_state.yaw || pitch!=g_state.pitch) {
        g_state.rest_x=g_state.rest_y=0;
        g_state.sensitivity=sensitivity;g_state.yaw=yaw;g_state.pitch=pitch;
    }

    if (!BindActive(aim.bind, g_bind_rt)) {
        ResetAim();
        return false;
    }

    const ImVec2 crosshair(viewport.center_x(), viewport.center_y());
    const float max_pixels  = AimFovRadiusPixels(aim.fov, frame.view, viewport);
    const float max_pixels2 = max_pixels * max_pixels;

    const int bone = HitboxBone(aim.hitbox);

    auto aim_point_of = [&](const EspPlayer& player, ImVec2& point, float& dist2) -> bool {
        if (!player.bones_valid) return false;
        if (aim.visible_only && !player.spotted) return false;
        if (!aim.friendly_fire && frame.has_local_team && player.team == frame.local_team) return false;   // mates only on request
        if (!WorldToScreen(frame.view, player.bones[bone], viewport, point)) return false;

        const float dx = point.x - crosshair.x;
        const float dy = point.y - crosshair.y;
        dist2 = dx * dx + dy * dy;
        return true;
    };

    // keep the locked target while it stays inside the fov; otherwise take the closest one
    ImVec2 target_point;
    float  target_dist2 = 0.0f;
    bool   found = false;

    for (const EspPlayer& player : frame.players) {
        if (g_state.locked_controller == 0 || player.controller != g_state.locked_controller) continue;
        if (aim_point_of(player, target_point, target_dist2) && target_dist2 <= max_pixels2) {
            found = true;
        }
        break;
    }

    if (!found) {
        g_state.humanizer.Reset();
        g_state.rest_x = g_state.rest_y = 0.0f;
        g_state.locked_controller = 0;

        float best = max_pixels2;
        for (const EspPlayer& player : frame.players) {
            ImVec2 point;
            float  dist2 = 0.0f;
            if (!aim_point_of(player, point, dist2)) continue;
            if (dist2 >= best) continue;

            best                      = dist2;
            target_point              = point;
            target_dist2              = dist2;
            g_state.locked_controller = player.controller;
            found                     = true;
        }
    }

    if (!found) { ResetAim(); return false; }

    const float dx = target_point.x - crosshair.x;
    const float dy = target_point.y - crosshair.y;
    float smooth = std::isfinite(aim.smoothing) ? aim.smoothing : 0.5f;
    if (smooth < 0.0f) smooth = 0.0f;
    if (smooth > 1.0f) smooth = 1.0f;
    const float human=std::isfinite(aim.humanizer)?std::clamp(aim.humanizer,0.0f,100.0f):0;
    const bool immediate = smooth == 0.0f && human == 0.0f;
    const float local_speed2 = frame.local_velocity.x * frame.local_velocity.x +
                               frame.local_velocity.y * frame.local_velocity.y;
    // A stationary deadzone while strafing makes the correction start and stop in bursts.
    if (!immediate && local_speed2 <= 25.0f && dx * dx + dy * dy < kDeadzone * kDeadzone) return false;

    // Zero bypasses the filter. Positive values retain time-based smoothing.
    const float tau  = kTauSharp * std::pow(kTauSmooth / kTauSharp, smooth);
    const float gain = immediate ? 1.0f : Gain(dt, tau);

    float counts_x = 0.0f, counts_y = 0.0f;
    if (!MouseCountsForError(frame.view, viewport, dx, dy, sensitivity,yaw,pitch, counts_x, counts_y)) {
        ResetAim();
        return false;
    }
    if(human>0) {
        float step_x=0,step_y=0;
        g_state.humanizer.Step(g_state.locked_controller,static_cast<uint32_t>(g_state.last_qpc),dt,human,
                              smooth==0?0:tau,std::hypot(dx,dy),counts_x,counts_y,step_x,step_y);
        return MoveMouse(step_x,step_y);
    }
    return MoveMouse(counts_x * gain, counts_y * gain, immediate);
}
