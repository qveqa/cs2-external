// esp.h - external esp rendering (boxes, skeleton, health)
#pragma once

#include <imgui.h>

#include "core/game.h"
#include "core/settings.h"
#include "core/viewport.h"

// draws onto the background draw list, clipped to the game's client area
void DrawEsp(const GameFrame& frame, const Settings& settings, const GameViewport& viewport,
             ImFont* text_font, float text_size, ImFont* icon_font, float icon_size);
