// bind.cpp - activation of a bound key; polling based, the frame loop calls it
#include "bind.h"

#include <windows.h>

#include <cstdio>

namespace {

bool g_suppress_mouse_edges = false;

bool KeyDown(int vk) {
    return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool IsMouseButton(int vk) {
    return vk >= VK_LBUTTON && vk <= VK_XBUTTON2;   // includes the X buttons
}

}  // namespace

void BindSuppressMouseEdges(bool suppress) {
    g_suppress_mouse_edges = suppress;
}

bool BindActive(const KeyBind& bind, BindRuntime& runtime) {
    // the key or the mode just changed: forget the latch and ignore a key that is
    // already held, so a capture press cannot double as an activation
    if (bind.bind != runtime.last_vk || bind.mode != runtime.last_mode) {
        runtime.last_vk   = bind.bind;
        runtime.last_mode = bind.mode;
        runtime.toggled   = false;
        runtime.prev_down = KeyDown(bind.bind);
    }

    if (bind.mode == kBindAlwaysOn) {
        runtime.toggled = false;
        return true;
    }

    if (bind.bind == 0) {          // unbound: nothing can activate it
        runtime.toggled = false;
        return false;
    }

    const bool down = KeyDown(bind.bind);

    if (bind.mode == kBindToggle) {
        const bool edge = down && !runtime.prev_down && !(g_suppress_mouse_edges && IsMouseButton(bind.bind));
        if (edge)
            runtime.toggled = !runtime.toggled;
    } else {
        runtime.toggled = false;   // latch only exists in toggle mode
    }
    runtime.prev_down = down;

    return bind.mode == kBindToggle ? runtime.toggled : down;
}

const char* BindKeyName(int vk, char* buf, size_t size) {
    if (vk == 0) {
        std::snprintf(buf, size, "[-]");
        return buf;
    }

    switch (vk) {
    case VK_LBUTTON:  std::snprintf(buf, size, "m1");     return buf;
    case VK_RBUTTON:  std::snprintf(buf, size, "m2");     return buf;
    case VK_MBUTTON:  std::snprintf(buf, size, "m3");     return buf;
    case VK_XBUTTON1: std::snprintf(buf, size, "m4");     return buf;
    case VK_XBUTTON2: std::snprintf(buf, size, "m5");     return buf;
    case VK_LMENU:    std::snprintf(buf, size, "lalt");   return buf;
    case VK_RMENU:    std::snprintf(buf, size, "ralt");   return buf;
    case VK_LSHIFT:   std::snprintf(buf, size, "lshift"); return buf;
    case VK_RSHIFT:   std::snprintf(buf, size, "rshift"); return buf;
    case VK_LCONTROL: std::snprintf(buf, size, "lctrl");  return buf;
    case VK_RCONTROL: std::snprintf(buf, size, "rctrl");  return buf;
    case VK_SPACE:    std::snprintf(buf, size, "space");  return buf;
    case VK_TAB:      std::snprintf(buf, size, "tab");    return buf;
    case VK_CAPITAL:  std::snprintf(buf, size, "caps");   return buf;
    case VK_RETURN:   std::snprintf(buf, size, "enter");  return buf;
    case VK_BACK:     std::snprintf(buf, size, "bksp");   return buf;
    case VK_DELETE:   std::snprintf(buf, size, "del");    return buf;
    case VK_INSERT:   std::snprintf(buf, size, "ins");    return buf;
    case VK_HOME:     std::snprintf(buf, size, "home");   return buf;
    case VK_END:      std::snprintf(buf, size, "end");    return buf;
    case VK_PRIOR:    std::snprintf(buf, size, "pgup");   return buf;
    case VK_NEXT:     std::snprintf(buf, size, "pgdn");   return buf;
    case VK_UP:       std::snprintf(buf, size, "up");     return buf;
    case VK_DOWN:     std::snprintf(buf, size, "down");   return buf;
    case VK_LEFT:     std::snprintf(buf, size, "left");   return buf;
    case VK_RIGHT:    std::snprintf(buf, size, "right");  return buf;
    case VK_OEM_1:    std::snprintf(buf, size, ";");      return buf;
    case VK_OEM_2:    std::snprintf(buf, size, "/");      return buf;
    case VK_OEM_3:    std::snprintf(buf, size, "`");      return buf;
    case VK_OEM_4:    std::snprintf(buf, size, "[");      return buf;
    case VK_OEM_5:    std::snprintf(buf, size, "\\");     return buf;
    case VK_OEM_6:    std::snprintf(buf, size, "]");      return buf;
    case VK_OEM_7:    std::snprintf(buf, size, "'");      return buf;
    case VK_OEM_PLUS: std::snprintf(buf, size, "=");      return buf;
    case VK_OEM_MINUS:std::snprintf(buf, size, "-");      return buf;
    case VK_OEM_COMMA:std::snprintf(buf, size, ",");      return buf;
    case VK_OEM_PERIOD: std::snprintf(buf, size, ".");    return buf;
    default: break;
    }

    if (vk >= 'A' && vk <= 'Z') {
        std::snprintf(buf, size, "%c", static_cast<char>(vk - 'A' + 'a'));
        return buf;
    }
    if (vk >= '0' && vk <= '9') {
        std::snprintf(buf, size, "%c", static_cast<char>(vk));
        return buf;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        std::snprintf(buf, size, "f%d", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        std::snprintf(buf, size, "num%d", vk - VK_NUMPAD0);
        return buf;
    }

    std::snprintf(buf, size, "vk%02x", vk);
    return buf;
}
