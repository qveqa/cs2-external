// quickstop.cpp
// Counter-strafe braking driven by the physical keyboard.
//
// The overlay reads the local player's ground velocity every frame. As soon as no
// movement key is physically held, the keys opposing the current velocity go down and
// stay down until the speed falls under the stop threshold; the release happens on the
// frame that threshold is crossed. Velocity is the feedback, so there are no blind
// timed taps.
//
// A low-level keyboard hook in our own process tells real presses apart from the
// injections this file sends. A key the player grabs mid-brake becomes his own and is
// never released by us. An axis that reverses (our own over-brake) ends the episode and
// a short retry gap keeps the brake clear of its reverse overshoot. Key-ups sent while
// the game was not in the foreground may never have reached it, so they are repeated
// once the game has focus again.
//
// Physical scancodes are used end to end, so the keys land on the w/a/s/d positions the
// game binds regardless of the keyboard layout.
#include "quickstop.h"

#include "input.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kAxisEpsilon = 0.25f;

// a brake that never crosses the threshold is dropped and retried after the gap
constexpr unsigned long long kMaxHoldMs = 350;
constexpr unsigned long long kRetryMs   = 120;

constexpr unsigned long long kResyncGapMs = 25;
constexpr int kResyncRounds = 3;

// physical key positions in the w/a/s/d cluster: the same positions the game binds
constexpr WORD kMovementScans[4] = { 0x11, 0x1E, 0x1F, 0x20 };

struct Wants {
    bool keys[4] = {};   // w, a, s, d
    int  forward = 0;    // +1 when the velocity points forward, -1 backward, 0 idle
    int  right   = 0;
};

bool g_held[4] = {};            // we injected a key-down that has no matching up yet
bool g_owned[4] = {};           // the player physically holds a key we injected
int  g_suspect_rounds[4] = {};  // the up may not have reached the game; resend later

bool g_braking = false;
int  g_brake_side[2] = {};                // velocity side we brake (forward, right)
unsigned long long g_brake_started_ms = 0;
unsigned long long g_next_engage_ms = 0;  // retry gap after an episode ended
unsigned long long g_next_resync_ms = 0;

HWND g_game_window = nullptr;

bool PhysicalScanDown(int key) {
    return InputPhysicalScanDown(kMovementScans[key], false);
}

bool InjectKey(int key, bool down) {
    return InputSendScan(kMovementScans[key], down, false);
}

bool AnyHeld() {
    for (bool held : g_held) {
        if (held) {
            return true;
        }
    }
    return false;
}

bool AnyWanted(const Wants& want) {
    for (bool key : want.keys) {
        if (key) {
            return true;
        }
    }
    return false;
}

Wants Decide(const Vec3& velocity, float yaw_deg, float stop_speed) {
    Wants out;

    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
        !std::isfinite(yaw_deg) || !std::isfinite(stop_speed)) {
        return out;
    }

    const float speed = std::hypot(velocity.x, velocity.y);
    const float threshold = stop_speed > 0.0f ? stop_speed : 0.0f;
    if (speed <= threshold) {
        return out;
    }

    // Project world-space velocity into the view-relative forward/right axes.
    // This assumes the game's yaw convention matches Source-style coordinates.
    const float yaw = yaw_deg * kPi / 180.0f;
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    const float forward_speed = velocity.x * cosine + velocity.y * sine;
    const float right_speed = velocity.x * sine - velocity.y * cosine;

    if (forward_speed > kAxisEpsilon) {
        out.keys[2] = true;  // moving forward: counter with s
        out.forward = 1;
    } else if (forward_speed < -kAxisEpsilon) {
        out.keys[0] = true;  // moving backward: counter with w
        out.forward = -1;
    }

    if (right_speed > kAxisEpsilon) {
        out.keys[1] = true;  // moving right: counter with a
        out.right = 1;
    } else if (right_speed < -kAxisEpsilon) {
        out.keys[3] = true;  // moving left: counter with d
        out.right = -1;
    }

    return out;
}

// The velocity crossed to the other side of an axis we were braking: that is our own
// counter input working, not a new direction from the player.
bool Reversed(const Wants& want) {
    if (g_brake_side[0] > 0 && want.keys[0]) return true;  // was braking forward, w now
    if (g_brake_side[0] < 0 && want.keys[2]) return true;
    if (g_brake_side[1] > 0 && want.keys[3]) return true;
    if (g_brake_side[1] < 0 && want.keys[1]) return true;
    return false;
}

void ResetEpisode() {
    g_braking = false;
    g_brake_side[0] = 0;
    g_brake_side[1] = 0;
    g_brake_started_ms = 0;
}

// Key-ups only land while the game owns the foreground. A failed or unfocused attempt
// is flagged and repeated by ResyncStuckKeys.
void ReleaseAllKeys(bool focused) {
    for (int i = 0; i < 4; ++i) {
        if (!g_held[i]) {
            g_owned[i] = false;
            continue;
        }

        if (PhysicalScanDown(i)) {
            g_held[i] = false;
            g_owned[i] = false;
            continue; // physical key-up now belongs to the player
        }
        const bool sent = InjectKey(i, false);
        if (!sent) {
            g_suspect_rounds[i] = kResyncRounds;
            continue;
        }
        g_held[i] = false;
        g_owned[i] = false;
        if (!sent || !focused) {
            g_suspect_rounds[i] = kResyncRounds;
        }
    }
}

// release the taps but leave alone any key the player is physically holding
void ReleaseUnownedKeys(bool focused, const Wants* wanted) {
    for (int i = 0; i < 4; ++i) {
        if (!g_held[i] || g_owned[i] || PhysicalScanDown(i)) {
            continue;
        }
        if (wanted && wanted->keys[i]) {
            continue;
        }

        const bool sent = InjectKey(i, false);
        if (!sent) {
            g_suspect_rounds[i] = kResyncRounds;
            continue;
        }
        g_held[i] = false;
        if (!sent || !focused) {
            g_suspect_rounds[i] = kResyncRounds;
        }
    }
}

// key-ups that were sent while the game was not foreground may never have reached
// it; once a suspect key is physically up, a key-up is harmless and unsticks it
void ResyncStuckKeys(unsigned long long now) {
    if (now < g_next_resync_ms) {
        return;
    }

    bool sent = false;
    for (int i = 0; i < 4; ++i) {
        if (g_suspect_rounds[i] <= 0 || g_held[i] || PhysicalScanDown(i)) {
            continue;
        }

        if (!InjectKey(i, false)) continue;
        --g_suspect_rounds[i];
        sent = true;
    }

    if (sent) {
        g_next_resync_ms = now + kResyncGapMs;
    }
}

void EndEpisode(bool focused, unsigned long long now, unsigned long long retry_ms) {
    ReleaseAllKeys(focused);
    ResetEpisode();
    g_next_engage_ms = now + retry_ms;
}

}  // namespace

void QuickstopDecide(const Vec3& velocity, float yaw_deg, float stop_speed, bool want[4]) {
    if (want == nullptr) {
        return;
    }

    const Wants decision = Decide(velocity, yaw_deg, stop_speed);
    for (int i = 0; i < 4; ++i) {
        want[i] = decision.keys[i];
    }
}

void QuickstopReleaseKeys() {
    const bool focused = g_game_window && GetForegroundWindow() == g_game_window;
    ReleaseAllKeys(focused);
    ResetEpisode();
}

void QuickstopShutdown() {
    QuickstopReleaseKeys();
}

bool ApplyQuickstop(const GameFrame& frame, const QuickstopSettings& quickstop, HWND game_window) {
    const unsigned long long now = GetTickCount64();
    g_game_window = game_window;

    // a key stranded by a focus switch must be flushed even when the local player is
    // dead, in the air, or the feature was just switched off: otherwise it stays down
    // in the game and eats the player's movement until they tap that key again
    const bool game_focused = game_window != nullptr && GetForegroundWindow() == game_window;
    if (game_focused && InputKeyboardHookInstalled()) {
        ResyncStuckKeys(now);
    }

    if (!quickstop.enabled) {
        QuickstopReleaseKeys();
        return false;
    }

    // without the hook there is no way to tell physical keys from injected ones;
    // staying out of the input is safer than risking a stuck movement key
    if (!InputEnsureKeyboardHook()) {
        QuickstopReleaseKeys();
        return false;
    }

    if (!game_focused || !frame.has_local || !frame.local_on_ground) {
        QuickstopReleaseKeys();
        return false;
    }

    // any movement key under the player's finger means he is driving: hand over the
    // keys he grabbed and stay out of his way
    bool physical_movement = false;
    for (int i = 0; i < 4; ++i) {
        physical_movement = physical_movement || PhysicalScanDown(i);
    }
    if (physical_movement) {
        for (int i = 0; i < 4; ++i) {
            if (g_held[i] && PhysicalScanDown(i)) {
                g_owned[i] = true;
            }
        }
        ReleaseUnownedKeys(game_focused, nullptr);
        ResetEpisode();
        return false;
    }

    // a key the player took over and then let go: his own key-up already released it
    for (int i = 0; i < 4; ++i) {
        if (g_owned[i]) {
            g_owned[i] = false;
            g_held[i] = false;
        }
    }

    const Wants want = Decide(frame.local_velocity, frame.local_yaw,
                              static_cast<float>(quickstop.stop_speed));

    if (g_braking) {
        if (g_brake_side[0] == 0) g_brake_side[0] = want.forward;
        if (g_brake_side[1] == 0) g_brake_side[1] = want.right;

        // stopped, or braked past zero: drop the episode and let friction settle the
        // reverse overshoot before the next one may start
        if (Reversed(want) || !AnyWanted(want)) {
            EndEpisode(game_focused, now, kRetryMs);
            return false;
        }

        // the counter keys are not bringing the speed down (wrong bind, no effect):
        // release instead of holding a movement key indefinitely
        if (now - g_brake_started_ms > kMaxHoldMs) {
            EndEpisode(game_focused, now, kRetryMs);
            return false;
        }

        ReleaseUnownedKeys(game_focused, &want);

        for (int i = 0; i < 4; ++i) {
            if (!want.keys[i] || g_held[i]) {
                continue;
            }

            if (!InjectKey(i, true)) {
                EndEpisode(game_focused, now, kRetryMs);
                return false;
            }
            g_held[i] = true;
            g_owned[i] = false;
        }

        return AnyHeld();
    }

    if (!AnyWanted(want) || now < g_next_engage_ms) {
        return false;
    }

    g_braking = true;
    g_brake_started_ms = now;
    g_brake_side[0] = want.forward;
    g_brake_side[1] = want.right;

    for (int i = 0; i < 4; ++i) {
        if (!want.keys[i]) {
            continue;
        }

        if (!InjectKey(i, true)) {
            EndEpisode(game_focused, now, kRetryMs);
            return false;
        }
        g_held[i] = true;
        g_owned[i] = false;
    }

    return AnyHeld();
}

#ifdef BLUE_WHALE_QUICKSTOP_TEST
void QuickstopTestSetPhysicalKey(int index, bool down) {
    if (index < 0 || index >= 4) {
        return;
    }
    InputTestSetPhysicalScan(kMovementScans[index], false, down);
}
#endif
