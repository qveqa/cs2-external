// bind.h - key bind activation shared by the aim and the trigger:
// always on / held ("on hotkey") / latched ("toggle"), plus key naming for the menu.
#pragma once

#include <cstddef>

#include "settings.h"

// runtime state of one bind: the toggle latch, the edge memory and the last config
struct BindRuntime {
    bool toggled   = false;
    bool prev_down = false;
    int  last_vk   = -1;   // re-seeds the edge memory whenever the key/mode changes
    int  last_mode = -1;
};

// evaluate once per frame; owns the edge detection and the toggle latch
bool BindActive(const KeyBind& bind, BindRuntime& runtime);

// the triggerbot holds a synthetic mouse button: its own injected press must not be
// counted as a fresh press of a mouse bind (otherwise one shot could latch a toggle)
void BindSuppressMouseEdges(bool suppress);

// human-readable name of the virtual key for the menu box ("m4", "x", "lalt");
// writes into buf (never null-terminates beyond size) and returns it
const char* BindKeyName(int vk, char* buf, size_t size);
