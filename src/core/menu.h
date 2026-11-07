// menu.h - imgui menu rendering
#pragma once

#include "game.h"
#include "settings.h"

// applies the custom dark theme; call once after ImGui::CreateContext
void ApplyMenuStyle();

// draws the centered menu window; want_close is set when the user asks to hide it
void DrawMenu(Settings& settings, bool& want_close, int local_weapon_index,
              const GameMouseSettings* mouse_settings = nullptr);

// draggable "who watches me" window, shown next to the menu while it is open
void DrawSpectatorsWindow(const GameFrame& frame, const Settings& settings);

// abandons a bind capture (used when the menu closes with a capture still armed)
void CancelBindCapture();
