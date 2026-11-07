// config.h - settings.ini persistence next to the exe
#pragma once

#include <string>

#include "settings.h"

// missing keys keep the built-in defaults; loaded values are clamped to the ui ranges
void LoadSettings(Settings& settings, const std::wstring& path);

// writes every key; does nothing for an empty path
void SaveSettings(const Settings& settings, const std::wstring& path);
