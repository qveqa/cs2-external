// trigger.h - external triggerbot: taps the left mouse button when the crosshair
// rests on an enemy hitbox. read-only game data, the only output is a synthetic click.
#pragma once

#include <windows.h>

#include "core/game.h"
#include "core/settings.h"
#include "core/viewport.h"

// evaluates the crosshair against enemy hitboxes; returns true when a click was issued
bool ApplyTrigger(const GameFrame& frame, const TriggerSettings& trigger, HWND game_window,
                  const GameViewport& viewport);

// Tick every frame. force bypasses the pulse deadline; reset/shutdown must use it.
// A failed key-up preserves ownership and can be retried.
bool TriggerReleasePendingClick(bool force = false);
void TriggerReset();
