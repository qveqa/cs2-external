// quickstop.h
#pragma once

#include <windows.h>

#include "core/game.h"
#include "core/settings.h"

// Selects counter-strafe keys from horizontal world-space velocity and view yaw.
// Output order: W, A, S, D. This function does not inject input.
void QuickstopDecide(const Vec3& velocity, float yaw_deg, float stop_speed, bool want[4]);

// Evaluates one update. Holds the counter keys while the speed is above the threshold
// and releases them on the frame it drops below. Returns true while keys are injected.
// Call regularly while the feature is active so velocity changes are observed.
bool ApplyQuickstop(const GameFrame& frame, const QuickstopSettings& quickstop, HWND game_window);

// Releases movement keys injected by quickstop and drops the current brake. Call
// before the game loses the foreground, and whenever the feature is disabled or its
// frame data is invalid. Never blocks on a physical key: the keyboard hook tells those
// apart.
void QuickstopReleaseKeys();

// Releases injected keys; call once on shutdown. The shared keyboard hook is
// removed separately with InputShutdown().
void QuickstopShutdown();

#ifdef BLUE_WHALE_QUICKSTOP_TEST
// test seam: pretend the player physically presses/releases w/a/s/d (0..3)
void QuickstopTestSetPhysicalKey(int index, bool down);
#endif
