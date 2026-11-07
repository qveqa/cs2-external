// Read-only live game sampling + headless ESP draw-list generation; no game input.
#include "core/game.h"
#include "core/config.h"
#include "func/esp/esp.h"
#include "core/projection.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Clock=std::chrono::steady_clock;
static double Ms(Clock::time_point a,Clock::time_point b) {return std::chrono::duration<double,std::milli>(b-a).count();}
static void Summary(FILE* out,const char* name,std::vector<double> values) {
    if(values.empty())return;
    std::sort(values.begin(),values.end());
    std::fprintf(out,"%s ms: p50=%.4f p95=%.4f p99=%.4f max=%.4f\n",name,
        values[values.size()/2],values[(values.size()-1)*95/100],values[(values.size()-1)*99/100],values.back());
}
int main(int argc,char** argv) {
    const int seconds=argc>1?std::clamp(std::atoi(argv[1]),1,600):120;
    FILE* out=std::fopen("build/esp_soak_results.txt","w");if(!out)return 1;
    GameReader reader;
    if(!reader.Attach(L"cs2.exe")) {std::fprintf(out,"attach failed\n");std::fclose(out);return 1;}
    Settings settings;LoadSettings(settings,L".\\build\\settings.ini");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize={1920,1080};io.DeltaTime=1.0f/120;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    GameFrame frame;std::vector<double> reads,draws,totals;
    int empty=0,players_seen=0,bones_seen=0,drawn=0,spikes=0;long long vertices=0;
    const auto start=Clock::now();int next_report=10;
    while(Ms(start,Clock::now())<seconds*1000 && reader.alive()) {
        const auto a=Clock::now();
        reader.ReadFrame(frame,settings.other.dropped_weapons!=kDroppedOff,false);
        const auto b=Clock::now();
        ImGui::NewFrame();
        DrawEsp(frame,settings,{0,0,1920,1080},nullptr,14,nullptr,14);
        const int count=ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
        vertices+=count;drawn+=count>0;
        ImGui::EndFrame();
        const auto c=Clock::now();
        reads.push_back(Ms(a,b));draws.push_back(Ms(b,c));totals.push_back(Ms(a,c));
        spikes+=Ms(a,c)>5.0;empty+=frame.players.empty();players_seen+=static_cast<int>(frame.players.size());
        for(const auto& player:frame.players)bones_seen+=player.bones_valid;
        if(Ms(start,c)>=next_report*1000) {
            std::fprintf(out,"elapsed=%ds samples=%zu players=%d bones=%d stride=%X vertices=%d\n",
                next_report,reads.size(),reader.stats().players_found,reader.stats().bones_ok,reader.stats().entity_stride,count);
            std::fflush(out);next_report+=10;
        }
        Sleep(8);
    }
    std::fprintf(out,"samples=%zu empty_frames=%d player_samples=%d bone_samples=%d frames_with_geometry=%d vertices=%lld samples_over_5ms=%d\n",
        reads.size(),empty,players_seen,bones_seen,drawn,vertices,spikes);
    Summary(out,"reader",reads);Summary(out,"headless_draw",draws);Summary(out,"combined",totals);
    std::fprintf(out,"Scope: CPU reader + ImGui geometry at 1920x1080; no GPU presentation or CS2 FPS measurement.\n");
    std::fclose(out);ImGui::DestroyContext();return reads.empty();
}
