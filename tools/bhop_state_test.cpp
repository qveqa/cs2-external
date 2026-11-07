#include <windows.h>
#include "func/misc/input.h"
#include <cstdio>
static bool send_ok=false;
static bool TestSend(WORD,bool,bool) { return send_ok; }
#define InputSendScan TestSend
#include "../src/func/misc/bhop.cpp"
#undef InputSendScan
int main() {
    g_active=true; g_space_down=true;
    BhopRelease();
    const bool retained=g_active && g_space_down;
    send_ok=true; BhopRelease();
    const bool cleared=!g_active && !g_space_down;
    std::printf("bhop release failure/retry: %s\n",retained&&cleared?"pass":"FAIL");
    return !(retained&&cleared);
}
