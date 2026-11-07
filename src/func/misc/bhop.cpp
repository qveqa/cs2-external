// bhop.cpp - hold-space auto-jump driven by the local player's ground state.
//
// The game is still only read: the landing is detected through m_fFlags and the
// jump is a synthetic space press sent with SendInput. Holding physical space is
// the activator. Because the game would otherwise see space as permanently held,
// bhop releases the logical key while airborne and presses it again on landing;
// that release/press pair is what gives CS2 a fresh jump input each time.
#include "bhop.h"

#include "input.h"

#include <chrono>

namespace {

using Clock = std::chrono::steady_clock;

constexpr WORD kSpaceScan = 0x39;

// long enough for a game frame at 60 Hz to sample the press; the key-up is
// independent of the jump, so holding it a few milliseconds longer is harmless
constexpr auto kSpacePulse = std::chrono::milliseconds(16);

// the game needs to see the release before the next press; one 240 Hz frame
constexpr auto kPressGap = std::chrono::milliseconds(10);

// while airborne, keep clearing the logical key so keyboard auto-repeat cannot
// leave space held when the player lands
constexpr auto kAirReleaseGap = std::chrono::milliseconds(25);

// a missed jump is retried, but not on every frame while still grounded
constexpr unsigned long long kRetryMs = 100;

bool g_active = false;           // physical space is held and bhop owns the jumps
bool g_space_down = false;       // we injected a down that has no up yet
bool g_press_pending = false;    // release was sent; the fresh press comes next
Clock::time_point g_press_at;
Clock::time_point g_space_release;
Clock::time_point g_next_air_release;
unsigned long long g_next_attempt_ms = 0;

bool SpacePhysicallyDown() {
    return InputPhysicalScanDown(kSpaceScan, false);
}

void ResetState() {
    g_active = false;
    g_space_down = false;
    g_press_pending = false;
    g_next_attempt_ms = 0;
}

}  // namespace

void BhopRelease() {
    if (g_active || g_space_down || g_press_pending) {
        // also covers the held physical key: the synthetic up is what returns
        // the game's logical space state to "released" before we stop
        if (!InputSendScan(kSpaceScan, false, false)) {
            InputSuppressSpaceRepeats(false);
            return; // retain ownership so the next frame retries release
        }
    }
    InputSuppressSpaceRepeats(false);
    ResetState();
}

void BhopShutdown() {
    BhopRelease();
}

bool ApplyBhop(const GameFrame& frame, const BhopSettings& bhop, HWND game_window) {
    const bool game_focused = game_window != nullptr && GetForegroundWindow() == game_window;

    if (!bhop.enabled || !game_focused || !frame.has_local) {
        BhopRelease();
        return false;
    }

    // without the hook physical and injected space cannot be told apart
    if (!InputEnsureKeyboardHook()) {
        BhopRelease();
        return false;
    }

    // space itself is the activator: releasing it stops bhop
    if (!SpacePhysicallyDown()) {
        BhopRelease();
        return false;
    }

    const auto clock_now = Clock::now();
    const unsigned long long now = GetTickCount64();

    if (!g_active) {
        g_active = true;
        g_space_down = false;
        g_press_pending = false;
        g_next_attempt_ms = 0;
        g_next_air_release = clock_now;

        // the held key must not auto-repeat between our own press pulses
        InputSuppressSpaceRepeats(true);
    }

    if (g_space_down && clock_now >= g_space_release) {
        if (!InputSendScan(kSpaceScan, false, false)) {
            return true;   // keep ownership and retry the key-up next frame
        }
        g_space_down = false;
    }

    if (frame.local_on_ground) {
        g_next_air_release = clock_now;

        if (g_press_pending) {
            if (clock_now >= g_press_at) {
                if (InputSendScan(kSpaceScan, true, false)) {
                    g_press_pending = false;
                    g_space_down = true;
                    g_space_release = clock_now + kSpacePulse;
                    g_next_attempt_ms = now + kRetryMs;
                    return true;
                }
            }
            return g_space_down;
        }

        if (!g_space_down && now >= g_next_attempt_ms) {
            // always release first: the physical key is still down and a plain
            // down event would not be a new jump for the game
            if (!InputSendScan(kSpaceScan, false, false)) {
                return false;
            }
            g_press_pending = true;
            g_press_at = clock_now + kPressGap;
        }
        return g_space_down;
    }

    // in the air: cancel a scheduled press and keep the logical key released
    g_press_pending = false;
    if (clock_now >= g_next_air_release) {
        InputSendScan(kSpaceScan, false, false);
        g_next_air_release = clock_now + kAirReleaseGap;
    }
    return g_space_down;
}
