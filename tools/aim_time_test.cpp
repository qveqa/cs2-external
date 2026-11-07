// End-to-end ApplyAim against an independent angular camera and deterministic time.
#include <windows.h>
#include <cstdio>
#include <cmath>
static LONGLONG now_us=1000000;
static long counts=0,counts_y=0;
static UINT TestSend(UINT n,LPINPUT data,int) {counts+=data->mi.dx;counts_y+=data->mi.dy;return n;}
static BOOL TestCounter(LARGE_INTEGER* out) {out->QuadPart=now_us;return TRUE;}
static BOOL TestFrequency(LARGE_INTEGER* out) {out->QuadPart=1000000;return TRUE;}
static HWND TestForeground() {return reinterpret_cast<HWND>(1);}
#define SendInput TestSend
#define QueryPerformanceCounter TestCounter
#define QueryPerformanceFrequency TestFrequency
#define GetForegroundWindow TestForeground
#include "../src/func/aim/aim.cpp"
#undef SendInput
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#undef GetForegroundWindow

static GameFrame MakeFrame(float angle,float sensitivity) {
    GameFrame f;f.has_local=true;f.has_mouse_settings=true;f.mouse_settings={sensitivity,0.022f,0.022f};
    f.view.m[0][1]=-1;f.view.m[1][2]=16.0f/9;f.view.m[2][0]=1;f.view.m[2][3]=-1;f.view.m[3][0]=1;
    EspPlayer p;p.controller=42;p.bones_valid=true;p.bones[7]={1000,-1000*std::tan(angle*0.01745329252f),0};
    f.players.push_back(p);return f;
}
static float Run(int hz,float smooth,float sensitivity,int duration_us,float scale) {
    AimReset();counts=0;now_us=1000000;FrameDt();
    AimSettings s;s.enabled=true;s.bind.mode=kBindAlwaysOn;s.fov=20;s.smoothing=smooth;
    s.sensitivity=99; // automatic mode must ignore this deliberately incorrect manual setting
    int elapsed=0;
    while(elapsed<duration_us) {
        const int step=std::min(1000000/hz,duration_us-elapsed);elapsed+=step;now_us+=step;
        const float angle=5-counts*sensitivity*scale*0.022f;
        auto frame=MakeFrame(angle,sensitivity);frame.local_sensitivity_scale=scale;
        ApplyAim(frame,s,TestForeground(),{0,0,1920,1080});
    }
    return 5-counts*sensitivity*scale*0.022f;
}
int main() {
    int cases=0,failures=0;
    for(float smooth:{0.0f,0.5f,1.0f})for(int duration:{50000,100000,200000}) {
        float minimum=999,maximum=-999;
        for(float scale:{0.25f,0.5f,1.0f})for(float sens:{0.5f,1.25f,3.0f})for(int hz:{60,144,240,400}) {
            const float error=Run(hz,smooth,sens,duration,scale);
            minimum=std::min(minimum,error);maximum=std::max(maximum,error);++cases;
            if(!std::isfinite(error) || error< -0.5f*sens*scale*0.022f-0.001f || error>5)++failures;
        }
        const float spread=maximum-minimum;
        const bool ok=spread<0.13f;failures+=!ok;
        std::printf("%s smoothing=%.1f elapsed=%dms spread=%.5f degrees across rates/sensitivities\n",
            ok?"pass":"FAIL",smooth,duration/1000,spread);
    }
    AimReset();counts=0;AimSettings s;s.enabled=true;s.bind.mode=kBindAlwaysOn;s.fov=20;
    s.smoothing=0;
    auto snap=MakeFrame(5,1.25f);
    ApplyAim(snap,s,TestForeground(),{0,0,1920,1080});
    const float residual=5-counts*1.25f*0.022f;
    const bool immediate=std::fabs(residual)<=0.5f*1.25f*0.022f+0.0001f;
    failures+=!immediate;
    std::printf("%s zero smoothing corrects full error in first step\n",immediate?"pass":"FAIL");
    const long snapped_counts=counts;
    for(int i=0;i<100;++i)ApplyAim(MakeFrame(residual,1.25f),s,TestForeground(),{0,0,1920,1080});
    const bool stable=counts==snapped_counts;failures+=!stable;
    std::printf("%s sub-count residual does not oscillate\n",stable?"pass":"FAIL");
    AimReset();counts=0;
    ApplyAim(MakeFrame(0.05f,1.25f),s,TestForeground(),{0,0,1920,1080});
    const bool tiny=counts!=0;failures+=!tiny;
    std::printf("%s zero smoothing bypasses stationary pixel deadzone\n",tiny?"pass":"FAIL");
    AimReset();counts=0;bool tracking=true;
    for(int i=0;i<100;++i) {
        const float target=5+3*std::sin(i*0.15f);
        const float error=target-counts*1.25f*0.022f;
        auto moving=MakeFrame(error,1.25f);moving.local_velocity={250,0,0};
        now_us+=4167;ApplyAim(moving,s,TestForeground(),{0,0,1920,1080});
        tracking=tracking && std::fabs(target-counts*1.25f*0.022f)<=0.5f*1.25f*0.022f+0.0001f;
    }
    failures+=!tracking;
    std::printf("%s zero smoothing follows 100 moving-target samples in one step\n",tracking?"pass":"FAIL");
    AimReset();counts=counts_y=0;s.humanizer=100;
    int curved_steps=0;
    for(int i=0;i<1000;++i) {
        const float error=5-counts*1.25f*0.022f;
        auto human=MakeFrame(error,1.25f);
        human.players[0].bones[7].z=1000*std::tan(counts_y*1.25f*0.022f*0.01745329252f);
        now_us+=4167;ApplyAim(human,s,TestForeground(),{0,0,1920,1080});
        curved_steps+=counts_y!=0;
    }
    const bool humanized=std::fabs(5-counts*1.25f*0.022f)<0.13f &&
                         std::fabs(counts_y*1.25f*0.022f)<0.13f && curved_steps>0;
    failures+=!humanized;
    std::printf("%s humanizer bends trajectory and converges back to target\n",humanized?"pass":"FAIL");
    s.humanizer=0;
    AimReset();counts=0;
    auto frame=MakeFrame(5,1.25f);frame.has_mouse_settings=false;
    const bool paused=!ApplyAim(frame,s,TestForeground(),{0,0,1920,1080}) && counts==0;
    failures+=!paused;
    std::printf("%s unavailable automatic settings pause aim\n",paused?"pass":"FAIL");
    // No catch-up burst after a multi-second hitch: measured dt is limited to 50 ms.
    AimReset();now_us=1000000;FrameDt();now_us+=5000000;
    const float dt=FrameDt();const bool bounded=dt<=0.05f && Gain(dt,0.015f)<1;
    failures+=!bounded;
    std::printf("%s hitch dt bounded to %.3fs\n",bounded?"pass":"FAIL",dt);
    std::printf("angular end-to-end cases=%d failures=%d\n",cases,failures);
    return failures!=0;
}
