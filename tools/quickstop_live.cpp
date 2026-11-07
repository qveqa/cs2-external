// quickstop_live.cpp - desktop check of the counter-strafe brake: drives ApplyQuickstop
// against a synthetic grounded frame and watches the injected scancodes through a
// low-level keyboard hook. verifies that the counter key is held while the velocity is
// above the threshold, released on the frame it drops, that the brake stays idle after
// a stop, that a reverse overshoot is not answered with the opposite key, that a player
// movement key ends the brake, that a non-movement key does not, and that disabling
// releases the keys. steals the foreground briefly; keys land in its own empty window.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/quickstop_live.cpp src/func/misc/quickstop.cpp \
//       src/func/misc/input.cpp -std=c++20 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
//       -DBLUE_WHALE_QUICKSTOP_TEST -I src -I dependencies \
//       -o %TEMP%/opencode/quickstop_live.exe -luser32 -lwinmm
#include <windows.h>
#include <timeapi.h>

#include <cstdio>

#include "core/game.h"
#include "core/settings.h"
#include "func/misc/input.h"
#include "func/misc/quickstop.h"

namespace {

struct KeyEvent {
    int       vk;
    int       scan;
    bool      down;
    ULONGLONG time;
};

KeyEvent g_journal[256];
int      g_count = 0;

LRESULT CALLBACK KeyProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (kb->flags & LLKHF_INJECTED) {
            int vk = static_cast<int>(kb->vkCode);
            if (vk == 0)
                vk = static_cast<int>(MapVirtualKeyW(kb->scanCode, MAPVK_VSC_TO_VK));
            if (g_count < 256) {
                g_journal[g_count].vk   = vk;
                g_journal[g_count].scan = static_cast<int>(kb->scanCode);
                g_journal[g_count].down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
                g_journal[g_count].time = GetTickCount64();
                ++g_count;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"quickstop_live_window";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"quickstop_live", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 360, 240,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

void ForceForeground(HWND hwnd) {
    for (int i = 0; i < 60; ++i) {
        if (GetForegroundWindow() == hwnd) return;

        const HWND  fg     = GetForegroundWindow();
        const DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        const DWORD my_tid = GetCurrentThreadId();

        if (fg_tid && fg_tid != my_tid) AttachThreadInput(fg_tid, my_tid, TRUE);
        ShowWindow(hwnd, SW_SHOW);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SwitchToThisWindow(hwnd, TRUE);   // also bypasses the foreground lock
        if (fg_tid && fg_tid != my_tid) AttachThreadInput(fg_tid, my_tid, FALSE);

        Sleep(30);
    }
}

void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

GameFrame MakeFrame(float vx, float vy, bool on_ground) {
    GameFrame frame;
    frame.has_local       = true;
    frame.local_on_ground = on_ground;
    frame.local_yaw       = 0.0f;
    frame.local_velocity  = Vec3{ vx, vy, 0.0f };
    return frame;
}

// steps the module for ms milliseconds; returns events seen
void Step(const QuickstopSettings& qs, HWND window, float vx, float vy, int ms) {
    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(ms);
    while (GetTickCount64() < until) {
        PumpMessages();
        if (GetForegroundWindow() != window) {
            ForceForeground(window);
            PumpMessages();
        }
        ApplyQuickstop(MakeFrame(vx, vy, true), qs, window);
        Sleep(2);
    }
    PumpMessages();
}

int CountKey(int vk, bool down) {
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_journal[i].vk == vk && g_journal[i].down == down) ++n;
    }
    return n;
}

int FirstIndex(int vk, bool down) {
    for (int i = 0; i < g_count; ++i) {
        if (g_journal[i].vk == vk && g_journal[i].down == down) return i;
    }
    return -1;
}

int FirstIndexAfter(int vk, bool down, ULONGLONG time) {
    for (int i = 0; i < g_count; ++i) {
        if (g_journal[i].vk == vk && g_journal[i].down == down && g_journal[i].time >= time) return i;
    }
    return -1;
}

int LastIndex(int vk) {
    for (int i = g_count - 1; i >= 0; --i) {
        if (g_journal[i].vk == vk) return i;
    }
    return -1;
}

// drops any brake and lets the retry gap of a finished episode expire
void Settle(const QuickstopSettings& qs, HWND window) {
    QuickstopReleaseKeys();
    Step(qs, window, 0.0f, 0.0f, 160);
    PumpMessages();
    g_count = 0;
}

}  // namespace

int main() {
    timeBeginPeriod(1);

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc, GetModuleHandleW(nullptr), 0);
    if (!hook) {
        std::printf("no keyboard hook, aborting\n");
        return 1;
    }

    HWND window = MakeWindow();
    if (!window) {
        std::printf("no window, aborting\n");
        UnhookWindowsHookEx(hook);
        return 1;
    }

    ForceForeground(window);
    PumpMessages();
    if (GetForegroundWindow() != window) {
        std::printf("could not take the foreground, aborting\n");
        UnhookWindowsHookEx(hook);
        return 1;
    }

    int failed = 0;

    QuickstopSettings qs;
    qs.enabled = true;
    qs.stop_speed = 25;

    // hook self-test: our own scancode injection must appear in the journal
    {
        Settle(qs, window);

        Step(qs, window, 250.0f, 0.0f, 80);   // sliding forward: s must go down
        const bool saw_down = CountKey('S', true) >= 1;
        const bool saw_scan = g_count > 0 && g_journal[0].scan != 0;
        std::printf("hook sees injected scancodes:  %s\n", (saw_down && saw_scan) ? "pass" : "FAIL");
        if (!(saw_down && saw_scan)) ++failed;
    }

    // the counter key is held for as long as the velocity is above the threshold and
    // released on the frame it drops; a single press, not a series of taps
    {
        Settle(qs, window);

        Step(qs, window, 250.0f, 0.0f, 120);   // constant slide: one continuous hold
        const int downs_while_moving = CountKey('S', true);
        const bool holding = LastIndex('S') >= 0 && g_journal[LastIndex('S')].down;

        const ULONGLONG t_stop = GetTickCount64();
        Step(qs, window, 0.0f, 0.0f, 160);      // stopped: the brake must let go
        const int ups = CountKey('S', false);
        const int up_i = FirstIndex('S', false);
        const bool released = up_i >= 0 && g_journal[up_i].time >= t_stop &&
                              g_journal[up_i].time - t_stop <= 40;
        const int downs_after_stop = CountKey('S', true) - downs_while_moving;

        const int down_i = FirstIndex('S', true);
        const ULONGLONG hold_ms = (down_i >= 0 && up_i > down_i)
                                      ? g_journal[up_i].time - g_journal[down_i].time : 0;

        const bool hold_ok = hold_ms >= 90 && hold_ms <= 240;
        const bool idle_ok = downs_after_stop == 0;

        std::printf("hold %llu ms, %d down / %d up, %d after stop: %s\n",
                    hold_ms, downs_while_moving, ups, downs_after_stop,
                    (holding && released && hold_ok && idle_ok) ? "pass" : "FAIL");
        if (!(holding && released && hold_ok && idle_ok)) ++failed;
    }

    // braking past zero must not answer with the opposite key: the reverse velocity is
    // the brake's own overshoot, not a new direction from the player
    {
        Settle(qs, window);

        Step(qs, window, 250.0f, 0.0f, 80);     // catch the s hold
        const ULONGLONG t_reverse = GetTickCount64();
        Step(qs, window, -40.0f, 0.0f, 100);    // still inside the retry gap
        const int s_up = FirstIndexAfter('S', false, t_reverse);
        const int w_down = FirstIndex('W', true);

        const bool stopped = s_up >= 0;
        const bool no_reverse = w_down < 0;

        std::printf("overshoot ends the brake, no w:  %s\n",
                    (stopped && no_reverse) ? "pass" : "FAIL");
        if (!(stopped && no_reverse)) ++failed;
    }

    // a non-movement key (space) must not interrupt the brake
    {
        Settle(qs, window);

        InputTestSetPhysicalScan(0x39, false, true);   // space, not a movement key
        Step(qs, window, 250.0f, 0.0f, 100);
        const bool holding = LastIndex('S') >= 0 && g_journal[LastIndex('S')].down;
        InputTestSetPhysicalScan(0x39, false, false);

        std::printf("space does not halt the brake:  %s\n", holding ? "pass" : "FAIL");
        if (!holding) ++failed;
    }

    // a key the player holds must end the brake at once and keep new ones from starting
    {
        Settle(qs, window);

        Step(qs, window, 250.0f, 0.0f, 80);            // catch the s hold
        const int before = CountKey('S', true);
        const ULONGLONG t_key = GetTickCount64();

        // keybd_event would be seen as injected by the module's own hook; the test
        // seam marks a real physical press instead
        QuickstopTestSetPhysicalKey(0, true);          // the player starts steering (w)
        Step(qs, window, 250.0f, 0.0f, 150);           // keeps "sliding", but w is held
        const int after = CountKey('S', true);

        const int up_i = FirstIndexAfter('S', false, t_key);
        const ULONGLONG release_delay = (up_i >= 0) ? g_journal[up_i].time - t_key : 0xFFFF;

        QuickstopTestSetPhysicalKey(0, false);

        const bool halted   = after == before;         // no further presses while w is held
        const bool released = release_delay <= 40;     // the active hold let go quickly
        std::printf("player key halts the brake (let go within %llu ms): %s\n",
                    release_delay, (halted && released) ? "pass" : "FAIL");
        if (!(halted && released)) ++failed;
    }

    // disabling mid-hold must release the keys
    {
        Settle(qs, window);

        Step(qs, window, 250.0f, 0.0f, 60);            // catch it holding s
        const int downs = CountKey('S', true);

        qs.enabled = false;
        Step(qs, window, 250.0f, 0.0f, 120);
        const int ups = CountKey('S', false);

        const bool ok = downs >= 1 && ups >= downs;
        std::printf("disable mid-hold releases all keys (%d down / %d up): %s\n",
                    downs, ups, ok ? "pass" : "FAIL");
        if (!ok) ++failed;
    }

    std::printf(failed == 0 ? "all quickstop checks pass\n" : "%d quickstop checks FAILED\n", failed);

    QuickstopShutdown();
    InputShutdown();
    UnhookWindowsHookEx(hook);
    timeEndPeriod(1);
    return failed == 0 ? 0 : 1;
}
