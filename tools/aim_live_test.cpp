// Real SendInput delivery into this test's own window. No game input or clicks.
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
static bool running=true, physical_move=false;
static long long total_x=0,total_y=0;
static int sent=0,received=0,send_failures=0;
static UINT ObserveSend(UINT n,LPINPUT data,int size) {
    const UINT result=SendInput(n,data,size);
    if(result==n) {
        for(UINT i=0;i<n;++i) if(data[i].type==INPUT_MOUSE) {
            total_x+=data[i].mi.dx; total_y+=data[i].mi.dy; ++sent;
        }
    } else ++send_failures;
    return result;
}
#define SendInput ObserveSend
#include "../src/func/aim/aim.cpp"
#undef SendInput

static LRESULT CALLBACK MouseHook(int code,WPARAM wp,LPARAM lp) {
    if(code==HC_ACTION && wp==WM_MOUSEMOVE) {
        const auto* event=reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if(event->flags & LLMHF_INJECTED) ++received;
        else physical_move=true;
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE || (msg==WM_KEYDOWN && wp==VK_ESCAPE)) {running=false;return 0;}
    if(msg==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);
        const wchar_t text[]=L"Bluewhale input test\nReal mouse movement; no clicks.\nMove your mouse or press Esc to stop.\nThis window closes automatically after testing.";
        RECT rect{24,24,650,280};DrawTextW(dc,text,-1,&rect,DT_LEFT);
        EndPaint(hwnd,&paint);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void Pump() {
    MSG message;
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
}
static double Seconds() {
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    return static_cast<double>(now.QuadPart)/freq.QuadPart;
}
static GameFrame Frame(float angle_x,float angle_y,float focal=1.0f) {
    constexpr float rad=0.0174532925199433f;
    GameFrame frame;frame.has_local=true;
    frame.view.m[0][1]=-focal;frame.view.m[1][2]=focal*16/9;
    frame.view.m[2][0]=1;frame.view.m[2][3]=-1;frame.view.m[3][0]=1;
    EspPlayer player;player.controller=123;player.bones_valid=true;
    player.bones[7]={1000,-1000*std::tan(angle_x*rad),-1000*std::tan(angle_y*rad)};
    frame.players.push_back(player);return frame;
}
int main() {
    FILE* log=std::fopen("build/aim_live_results.txt","w");if(!log)return 1;
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=L"BluewhaleAimTest";
    cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"Bluewhale mouse test",WS_OVERLAPPEDWINDOW,
                              100,100,700,360,nullptr,nullptr,instance,nullptr);
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
    const double wait_start=Seconds();
    while(running && GetForegroundWindow()!=window && Seconds()-wait_start<60) {Pump();Sleep(10);}
    if(GetForegroundWindow()!=window) {std::fprintf(log,"ABORT: test window never received foreground\n");std::fclose(log);DestroyWindow(window);return 2;}
    HHOOK hook=SetWindowsHookExW(WH_MOUSE_LL,MouseHook,instance,0);
    if(!hook) {std::fprintf(log,"ABORT: mouse hook failed\n");std::fclose(log);DestroyWindow(window);return 2;}
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    int passed=0,failed=0;
    for(int rate:{60,144,240})
    for(float smooth:{0.0f,0.5f,1.0f})
    for(float focal:{1.0f,2.0f})
    for(float direction:{-1.0f,1.0f}) {
        if(!running || physical_move || GetForegroundWindow()!=window) break;
        AimReset();total_x=total_y=0;
        AimSettings settings;settings.enabled=true;settings.bind.mode=kBindAlwaysOn;
        settings.smoothing=smooth;settings.sensitivity=1.25f;settings.auto_sensitivity=false;settings.fov=20;
        const double start=Seconds();float final_error=999,largest_step=0;bool monotone=true;
        float previous=999;
        int calls=0;
        while(running && !physical_move && GetForegroundWindow()==window && Seconds()-start<2.5) {
            Pump();
            const float angle_x=direction*5.0f-static_cast<float>(total_x)*1.25f*0.022f;
            const float angle_y=direction*2.0f-static_cast<float>(total_y)*1.25f*0.022f;
            auto frame=Frame(angle_x,angle_y,focal);
            ImVec2 point;WorldToScreen(frame.view,frame.players[0].bones[7],{0,0,1920,1080},point);
            final_error=std::hypot(point.x-960,point.y-540);
            if(final_error>previous+1.0f)monotone=false;previous=final_error;
            const auto before=total_x;
            ApplyAim(frame,settings,window,{0,0,1920,1080});++calls;
            largest_step=std::max(largest_step,static_cast<float>(std::llabs(total_x-before)));
            if(final_error<2.5f) break;
            LARGE_INTEGER due{};due.QuadPart=-10000000LL/rate;
            if(timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)) WaitForSingleObject(timer,100);
            else Sleep(static_cast<DWORD>(1000/rate));
        }
        Pump();
        const bool ok=running && !physical_move && GetForegroundWindow()==window && final_error<2.5f && monotone;
        passed+=ok;failed+=!ok;
        std::fprintf(log,"%s rate=%dHz smooth=%.2f focal=%.1f direction=%+.0f error=%.3fpx time=%.3fs calls=%d max_step=%.0fcounts\n",
            ok?"PASS":"FAIL",rate,smooth,focal,direction,final_error,Seconds()-start,calls,largest_step);
        std::fflush(log);
    }
    AimReset();Pump();
    std::fprintf(log,"summary passed=%d failed=%d sends=%d hook_events=%d send_failures=%d physical_move=%d\n",
                  passed,failed,sent,received,send_failures,physical_move);
    if(timer)CloseHandle(timer);
    UnhookWindowsHookEx(hook);DestroyWindow(window);std::fclose(log);
    return failed || passed!=36 || received==0 || send_failures;
}
