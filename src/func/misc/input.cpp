// input.cpp - one keyboard hook and one scan-code sender for the misc features
#include "input.h"

namespace {

bool g_scan_down[512] = {};      // physical keys currently held (scancode | extended bit)

bool g_suppress_space_repeats = false;

HHOOK g_keyboard_hook = nullptr;

int ScanIndex(WORD scan, bool extended) {
    return static_cast<int>(scan & 0xFF) | (extended ? 0x100 : 0);
}

void SetPhysicalScan(WORD scan, bool extended, bool down) {
    g_scan_down[ScanIndex(scan, extended)] = down;
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if ((kb->flags & LLKHF_INJECTED) == 0) {
            const WORD scan = static_cast<WORD>(kb->scanCode);
            const bool extended = (kb->flags & LLKHF_EXTENDED) != 0;
            const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;

            // the first physical down passes; repeats while bhop owns space do not
            if (g_suppress_space_repeats && scan == 0x39 && !extended && down &&
                g_scan_down[ScanIndex(scan, extended)]) {
                return 1;
            }
            SetPhysicalScan(scan, extended, down);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// keys already held when the hook goes in: seed them so the first action does
// not fire into a held keyboard. a stale seed clears on that key's own key-up.
void SeedPhysicalState() {
    const HWND foreground = GetForegroundWindow();
    const DWORD thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const HKL layout = GetKeyboardLayout(thread);

    for (int vk = 0x01; vk < 0xFF; ++vk) {
        if ((GetAsyncKeyState(vk) & 0x8000) == 0) {
            continue;
        }

        const UINT scan = MapVirtualKeyExW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC, layout);
        if (scan == 0 || scan > 0xFF) {
            continue;
        }
        SetPhysicalScan(static_cast<WORD>(scan), false, true);
    }
}

}  // namespace

bool InputEnsureKeyboardHook() {
    if (g_keyboard_hook) {
        return true;
    }

    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    if (!g_keyboard_hook) {
        return false;
    }

    SeedPhysicalState();
    return true;
}

bool InputKeyboardHookInstalled() {
    return g_keyboard_hook != nullptr;
}

void InputShutdown() {
    if (g_keyboard_hook) {
        UnhookWindowsHookEx(g_keyboard_hook);
        g_keyboard_hook = nullptr;
    }

    for (bool& down : g_scan_down) {
        down = false;
    }
    g_suppress_space_repeats = false;
}

bool InputPhysicalScanDown(WORD scan, bool extended) {
    return g_scan_down[ScanIndex(scan, extended)];
}

bool InputSendScan(WORD scan, bool down, bool extended) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!down) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return SendInput(1, &input, sizeof(input)) == 1;
}

void InputSuppressSpaceRepeats(bool enabled) {
    g_suppress_space_repeats = enabled;
}

#if defined(BLUE_WHALE_QUICKSTOP_TEST) || defined(BLUE_WHALE_BHOP_TEST)
void InputTestSetPhysicalScan(WORD scan, bool extended, bool down) {
    SetPhysicalScan(scan, extended, down);
}
#endif
