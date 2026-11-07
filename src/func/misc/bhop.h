// bhop.h - hold-space auto-jump: while physical space is held, bhop releases
// and re-presses it on every landing. Assumes jump is bound to space.
// The game is only read; jump input is synthesized through SendInput.
#pragma once

#include <windows.h>

#include "core/game.h"
#include "core/settings.h"

// Evaluates one update. Returns true while bhop holds its synthetic space.
// Call every frame while the game owns the foreground and the menu is closed.
// Holding physical space activates it; releasing space stops it.
bool ApplyBhop(const GameFrame& frame, const BhopSettings& bhop, HWND game_window);

// Releases the synthetic space. Safe when nothing is held.
void BhopRelease();

// Releases the synthetic space and drops the retry timer; call once on shutdown.
void BhopShutdown();
