// bhop_live.cpp - desktop check of the hold-space auto-jump: drives ApplyBhop
// against synthetic ground/air frames and watches the injected space events.
// verifies that holding space starts the jump pulses, the logical release before
// each press, airborne and retry behavior, and that releasing/disabling stops it.
// steals the foreground briefly; keys land in its own empty window.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/bhop_live.cpp src/func/misc/bhop.cpp \
//       src/func/misc/input.cpp -std=c++20 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
//       -DBLUE_WHALE_BHOP_TEST -I src -I dependencies \
//       -o %TEMP%/opencode/bhop_live.exe -luser32 -lwinmm
#include <windows.h>
#include <timeapi.h>

#include <cstdio>

#include "core/game.h"
#include "core/settings.h"
#include "func/misc/bhop.h"
#include "func/misc/input.h"

namespace {

int g_down = 0;
int g_up = 0;

LRESULT CALLBACK JournalProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if ((kb->flags & LLKHF_INJECTED) != 0 && kb->scanCode == 0x39) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) ++g_down;
            if (wp == WM_KEYUP || wp == WM_SYSKEYUP) ++g_up;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void StealForeground(HWND hwnd) {
    const HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD our_thread = GetCurrentThreadId();

    if (foreground_thread && foreground_thread != our_thread)
        AttachThreadInput(foreground_thread, our_thread, TRUE);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    if (foreground_thread && foreground_thread != our_thread)
        AttachThreadInput(foreground_thread, our_thread, FALSE);
}

void ResetCounts() {
    g_down = 0;
    g_up = 0;
}

int Check(const char* label, int want_down, int want_up) {
    const bool ok = g_down == want_down && g_up == want_up;
    std::printf("%-42s %s (down %d, up %d)\n", label, ok ? "pass" : "FAIL", g_down, g_up);
    return ok ? 0 : 1;
}

void HoldSpace() {
    InputTestSetPhysicalScan(0x39, false, true);
}

void ReleasePhysicalSpace() {
    InputTestSetPhysicalScan(0x39, false, false);
}

int Fail(const wchar_t* window_class) {
    UnregisterClassW(window_class, GetModuleHandleW(nullptr));
    timeEndPeriod(1);
    return 1;
}

}  // namespace

int main() {
    timeBeginPeriod(1);

    constexpr wchar_t kWindowClass[] = L"bhop_live_check";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"bhop check", WS_OVERLAPPEDWINDOW,
                                100, 100, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    StealForeground(hwnd);
    Sleep(150);

    if (GetForegroundWindow() != hwnd) {
        std::printf("foreground check FAILED\n");
        DestroyWindow(hwnd);
        return Fail(kWindowClass);
    }

    HHOOK journal = SetWindowsHookExW(WH_KEYBOARD_LL, JournalProc, GetModuleHandleW(nullptr), 0);
    if (!journal) {
        std::printf("journal hook FAILED\n");
        DestroyWindow(hwnd);
        return Fail(kWindowClass);
    }

    // install the shared hook before simulating the physical key
    if (!InputEnsureKeyboardHook()) {
        std::printf("input hook FAILED\n");
        DestroyWindow(hwnd);
        return Fail(kWindowClass);
    }

    GameFrame frame;
    frame.has_local = true;
    frame.local_on_ground = true;

    BhopSettings bhop;
    bhop.enabled = true;
    int failed = 0;

    // holding space starts with a logical release, not a jump
    HoldSpace();
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("hold starts with logical release", 0, 1);

    // the fresh press follows after the short release gap
    Sleep(20);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("fresh press after the release", 1, 0);

    // the pulse ends, but the retry timer is still running
    Sleep(25);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("pulse release", 0, 1);

    // airborne: the logical key stays released against auto-repeat
    frame.local_on_ground = false;
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("airborne keeps key released", 0, 1);

    Sleep(30);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("airborne periodic release", 0, 1);

    // releasing physical space stops bhop and gives the key back
    frame.local_on_ground = true;
    ReleasePhysicalSpace();
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("releasing space stops bhop", 0, 1);

    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("released space stays inactive", 0, 0);

    // grounded retry after a missed jump
    HoldSpace();
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: hold releases logical key", 0, 1);

    Sleep(20);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: first press", 1, 0);

    Sleep(25);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: first pulse release", 0, 1);

    Sleep(130);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: release before second press", 0, 1);

    Sleep(20);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: second press", 1, 0);

    Sleep(25);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: second pulse release", 0, 1);

    ReleasePhysicalSpace();
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("retry: final release", 0, 1);

    // holding space in the air and landing starts the first jump
    HoldSpace();
    frame.local_on_ground = false;
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("air activation releases key", 0, 1);

    Sleep(30);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("air activation periodic release", 0, 1);

    frame.local_on_ground = true;
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("landing releases before press", 0, 1);

    Sleep(20);
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("landing press", 1, 0);

    ReleasePhysicalSpace();
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("landing final release", 0, 1);

    // disabling while space is held must release the synthetic key
    HoldSpace();
    frame.local_on_ground = true;
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("disable: activation", 0, 1);

    bhop.enabled = false;
    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("disable releases held key", 0, 1);

    ResetCounts();
    ApplyBhop(frame, bhop, hwnd);
    failed += Check("disable stays inactive", 0, 0);

    ReleasePhysicalSpace();
    ApplyBhop(frame, bhop, hwnd);

    BhopShutdown();
    InputShutdown();
    UnhookWindowsHookEx(journal);

    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClass, wc.hInstance);
    timeEndPeriod(1);

    std::printf(failed == 0 ? "all bhop checks pass\n" : "%d bhop checks FAILED\n", failed);
    return failed == 0 ? 0 : 1;
}
