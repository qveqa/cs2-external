// aim.h - external aim assist (read-only data, relative mouse injection only)
#pragma once

#include <windows.h>

#include "core/game.h"
#include "core/settings.h"
#include "core/viewport.h"

// picks the target nearest to the crosshair inside the fov and glides the mouse
// towards it while the bind is active; the menu must be closed and the game focused.
// returns true when a mouse move was issued.
bool ApplyAim(const GameFrame& frame, const AimSettings& aim, HWND game_window,
              const GameViewport& viewport);
void AimReset();
