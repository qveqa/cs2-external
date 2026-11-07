// Deterministic aim regression tests; no hooks, windows or real input.
#include <windows.h>
#include <cstdio>
static bool send_ok = true;
static int sent_x = 0;
static HWND foreground = reinterpret_cast<HWND>(1);
static UINT TestSendInput(UINT, LPINPUT input, int) {
    if (!send_ok) return 0;
    sent_x += input->mi.dx;
    return 1;
}
static HWND TestForeground() { return foreground; }
#define SendInput TestSendInput
#define GetForegroundWindow TestForeground
#include "../src/func/aim/aim.cpp"
#undef SendInput
#undef GetForegroundWindow
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* text) { std::printf("%s %s\n",ok?"pass":"FAIL",text); failures += !ok; };
    AimReset();
    check(!MoveMouse(0.4f,0) && sent_x==0,"fraction retained");
    check(MoveMouse(0.8f,0) && sent_x==1,"fraction accumulates");
    send_ok=false;
    check(!MoveMouse(10,0) && sent_x==1 && std::fabs(g_state.rest_x)<1,"failed move reports failure without queued burst");
    g_state.locked_controller=123; g_state.rest_x=0.7f; g_state.last_qpc=123;
    g_bind_rt.toggled=true;
    AimReset();
    check(g_state.locked_controller==0 && g_state.rest_x==0 && g_state.last_qpc==0 && !g_bind_rt.toggled,"reset clears target, fractions, clock and bind");
    GameFrame frame; frame.has_local=true;
    frame.view.m[0][1]=-1; frame.view.m[1][2]=16.0f/9;
    frame.view.m[2][0]=1; frame.view.m[2][3]=-1; frame.view.m[3][0]=1;
    EspPlayer target; target.controller=456; target.bones_valid=true; target.bones[7]={1000,-100,0};
    frame.players.push_back(target);
    AimSettings settings; settings.enabled=true; settings.bind.mode=kBindAlwaysOn; settings.fov=30;
    settings.auto_sensitivity=false;
    send_ok=true;
    check(ApplyAim(frame,settings,foreground,{0,0,1920,1080}) && sent_x>1,"full aim path sends angular correction");
    foreground=nullptr;
    check(!ApplyAim(frame,settings,reinterpret_cast<HWND>(1),{0,0,1920,1080}) &&
          g_state.locked_controller==0 && g_state.rest_x==0 && g_state.last_qpc==0,"focus loss resets full aim path");
    foreground=reinterpret_cast<HWND>(1);
    const GameViewport vp{0,0,1920,1080};
    settings.visible_only=true;AimReset();
    check(!ApplyAim(frame,settings,foreground,vp) && !g_state.locked_controller,"unspotted target filtered");
    frame.players[0].spotted=true;
    ApplyAim(frame,settings,foreground,vp);
    check(g_state.locked_controller==456,"spotted target accepted");
    settings.visible_only=false;frame.has_local_team=true;frame.local_team=2;frame.players[0].team=2;AimReset();
    check(!ApplyAim(frame,settings,foreground,vp) && !g_state.locked_controller,"friendly player filtered");
    settings.friendly_fire=true;ApplyAim(frame,settings,foreground,vp);
    check(g_state.locked_controller==456,"friendly-fire override accepted");
    settings.friendly_fire=false;frame.players[0].team=3;settings.fov=1;AimReset();
    check(!ApplyAim(frame,settings,foreground,vp) && !g_state.locked_controller,"outside FOV rejected");
    settings.fov=30;frame.players[0].bones_valid=false;AimReset();
    check(!ApplyAim(frame,settings,foreground,vp),"invalid bones rejected by aim");
    frame.players[0].bones_valid=true;
    EspPlayer nearer=frame.players[0];nearer.controller=789;nearer.bones[7].y=-50;
    frame.players.push_back(nearer);AimReset();ApplyAim(frame,settings,foreground,vp);
    check(g_state.locked_controller==789,"nearest eligible target selected");
    frame.players[0].bones[7].y=-10;ApplyAim(frame,settings,foreground,vp);
    check(g_state.locked_controller==789,"existing valid lock preserved");
    frame.players.pop_back();ApplyAim(frame,settings,foreground,vp);
    check(g_state.locked_controller==456,"missing lock replaced by available target");
    settings.enabled=false;ApplyAim(frame,settings,foreground,vp);
    check(!g_state.locked_controller && g_state.rest_x==0 && !g_bind_rt.toggled,"disabled aim clears runtime state");
    // Independent feedback model: counts rotate a camera, projection uses tan(angle).
    for (float sensitivity : {0.5f,1.25f,3.0f})
    for (float focal : {0.75f,1.0f,2.0f}) {
        Matrix4x4 view{}; view.m[0][1]=-focal; view.m[1][2]=focal*16/9; view.m[3][0]=1;
        GameViewport vp{0,0,1920,1080};
        float angle=5.0f, previous=angle;
        bool monotone=true;
        for(int i=0;i<200;++i) {
            const float dx=std::tan(angle*0.01745329252f)*vp.width*0.5f*focal;
            float x,y;
            if(!MouseCountsForError(view,vp,dx,0,sensitivity,0.022f,0.022f,x,y)) { monotone=false; break; }
            angle -= x*0.1f*sensitivity*0.022f;
            monotone = monotone && angle>=-0.0001f && angle<=previous;
            previous=angle;
        }
        check(monotone && std::fabs(angle)<0.001f,"angular feedback converges across sensitivity and zoom projections");
    }
    return failures!=0;
}
