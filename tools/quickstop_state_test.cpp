#include <windows.h>
#include "func/misc/input.h"
#include <cstdio>
static bool send_ok=true, physically_down=false;
static int releases=0;
static bool TestSend(WORD, bool down, bool) { if(!send_ok) return false; releases += !down; return true; }
static bool TestPhysical(WORD,bool) { return physically_down; }
#define InputSendScan TestSend
#define InputPhysicalScanDown TestPhysical
#include "../src/func/misc/quickstop.cpp"
#undef InputSendScan
#undef InputPhysicalScanDown
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* text) { std::printf("%s %s\n",ok?"pass":"FAIL",text); failures+=!ok; };
    g_held[0]=true; send_ok=false;
    ReleaseAllKeys(true);
    check(g_held[0] && releases==0,"failed release retains ownership");
    send_ok=true; ReleaseAllKeys(true);
    check(!g_held[0] && releases==1,"release retries successfully");
    g_held[1]=true; send_ok=false; ReleaseUnownedKeys(true,nullptr);
    check(g_held[1],"failed unowned release retains ownership");
    send_ok=true; ReleaseUnownedKeys(true,nullptr);
    check(!g_held[1],"unowned release retries");
    g_held[2]=true; physically_down=true;
    const int before=releases; ReleaseAllKeys(true);
    check(!g_held[2] && releases==before,"physical key is handed back without key-up");
    physically_down=false; g_suspect_rounds[3]=3;
    send_ok=false; ResyncStuckKeys(100);
    check(g_suspect_rounds[3]==3,"failed resync keeps retry budget");
    send_ok=true; ResyncStuckKeys(100);
    check(g_suspect_rounds[3]==2,"successful resync consumes retry");
    return failures!=0;
}
