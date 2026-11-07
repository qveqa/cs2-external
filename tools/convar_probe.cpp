#include "core/mouse_settings.h"
#include <cstdio>
#include <chrono>
int main() {
    Memory memory;if(!memory.Attach(L"cs2.exe"))return 1;
    MouseSettingsReader reader;GameMouseSettings values;
    double maximum_ms=0;int calls=0;bool found=false;
    for(;calls<100 && !found;++calls) {
        const auto start=std::chrono::steady_clock::now();
        found=reader.Read(memory,values);
        maximum_ms=std::max(maximum_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    std::printf("found=%d discovery_calls=%d max_discovery_call=%.3fms sensitivity=%.6f yaw=%.6f pitch=%.6f\n",found,calls,maximum_ms,values.sensitivity,values.yaw,values.pitch);
    const auto start=std::chrono::steady_clock::now();int valid=0;
    for(int i=0;i<1000;++i)valid+=reader.Read(memory,values);
    std::printf("steady cached reads: valid=%d/1000 total=%.3fms\n",valid,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    return !found || valid!=1000;
}
