#include <windows.h>
#include <cstdio>
static bool send_ok = true;
static unsigned long long now_ms = 100;
static HWND foreground = reinterpret_cast<HWND>(1);
static int downs = 0, ups = 0;
static UINT TestSendInput(UINT, LPINPUT input, int) {
    if (!send_ok) return 0;
    if (input->mi.dwFlags == MOUSEEVENTF_LEFTDOWN) ++downs; else ++ups;
    return 1;
}
static ULONGLONG TestTick() { return now_ms; }
static HWND TestForeground() { return foreground; }
#define SendInput TestSendInput
#define GetTickCount64 TestTick
#define GetForegroundWindow TestForeground
#include "../src/func/aim/trigger.cpp"
#undef SendInput
#undef GetTickCount64
#undef GetForegroundWindow

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* text) { std::printf("%s %s\n", ok ? "pass" : "FAIL", text); failures += !ok; };
    GameFrame frame; frame.has_local = true;
    frame.view.m[0][1] = -1; frame.view.m[1][2] = 1;
    frame.view.m[3][0] = 1;
    EspPlayer player; player.bones_valid = true;
    player.bones[7] = {1000, 0, -4}; player.bones[1] = {1000, 0, -30};
    frame.players.push_back(player);
    TriggerSettings settings; settings.enabled = true; settings.target = 0;
    settings.bind.mode = kBindAlwaysOn;
    const GameViewport vp{0,0,1920,1080};
    send_ok = false;
    check(!ApplyTrigger(frame, settings, foreground, vp) && !g_click_down, "failed down does not claim ownership");
    send_ok = true;
    check(ApplyTrigger(frame, settings, foreground, vp) && downs == 1, "successful down owns button");
    check(!TriggerReleasePendingClick() && ups == 0, "normal release waits for pulse deadline");
    send_ok = false;
    check(!TriggerReleasePendingClick(true) && g_click_down, "failed forced up retains ownership");
    send_ok = true;
    check(TriggerReleasePendingClick(true) && ups == 1 && !g_click_down, "forced release bypasses deadline");
    TriggerReset();
    check(ApplyTrigger(frame, settings, foreground, vp), "can click after reset");
    foreground = nullptr;
    check(!ApplyTrigger(frame, settings, reinterpret_cast<HWND>(1), vp) && !g_click_down && ups == 2,
          "focus loss releases immediately");
    foreground=reinterpret_cast<HWND>(1);TriggerReset();
    frame.players[0].bones_valid=false;
    check(!ApplyTrigger(frame,settings,foreground,vp),"invalid bones do not trigger click");
    frame.players[0].bones_valid=true;settings.visible_only=true;
    check(!ApplyTrigger(frame,settings,foreground,vp),"unspotted target does not trigger click");
    frame.players[0].spotted=true;
    check(ApplyTrigger(frame,settings,foreground,vp),"spotted eligible target triggers click");
    TriggerReset();settings.visible_only=false;
    frame.has_local_team=true;frame.local_team=2;frame.players[0].team=2;
    check(!ApplyTrigger(frame,settings,foreground,vp),"friendly player does not trigger click");
    settings.friendly_fire=true;
    check(ApplyTrigger(frame,settings,foreground,vp),"friendly-fire override permits click");
    settings.enabled=false;
    check(!ApplyTrigger(frame,settings,foreground,vp) && !g_click_down,"disabling trigger releases its click");
    return failures != 0;
}
