// Synthetic good/bad poses in this test process; never attaches to the game.
#include "core/memory.h"
#define private public
#include "core/game.h"
#undef private
#include <cstdio>
#include <cstring>
#include <cwchar>

int main() {
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH);
    GameReader reader;
    if (!reader.memory_.Attach(std::wcsrchr(exe,L'\\')+1)) return 1;
    alignas(8) uint8_t good[24*32]{}, bad[24*32]{};
    for (int bone=0; bone<24; ++bone) {
        Vec3 pos{100,100,bone==7?164.0f:130.0f};
        const float quat[4]={0,0,0,1};
        std::memcpy(good+bone*32,&pos,sizeof(pos));
        std::memcpy(good+bone*32+16,quat,sizeof(quat));
    }
    std::memcpy(bad,good,sizeof(good));
    const Vec3 invalid_head{100,100,100};
    std::memcpy(bad+7*32,&invalid_head,sizeof(invalid_head));
    uint8_t good_scene[0x300]{}, bad_scene[0x300]{};
    const uintptr_t good_ptr=reinterpret_cast<uintptr_t>(good), bad_ptr=reinterpret_cast<uintptr_t>(bad);
    const auto model=sdk::CSkeletonInstance::m_modelState;
    std::memcpy(good_scene+model+0x80,&good_ptr,sizeof(good_ptr));
    for (int off=8;off<=0x98;off+=8) std::memcpy(bad_scene+model+off,&bad_ptr,sizeof(bad_ptr));
    EspPlayer valid,invalid; valid.origin=invalid.origin={100,100,100};
    LARGE_INTEGER start,end,freq; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    int good_count=0,bad_count=0;
    for(int frame=0;frame<5000;++frame) {
        reader.bone_scan_budget_.BeginFrame();
        good_count += reader.ReadBones(good_scene,valid);
        bad_count += reader.ReadBones(bad_scene,invalid);
    }
    QueryPerformanceCounter(&end);
    std::printf("5000 good/bad pose pairs: %.3f ms; good=%d bad=%d\n",
        (end.QuadPart-start.QuadPart)*1000.0/freq.QuadPart,good_count,bad_count);
    return good_count!=5000 || bad_count!=0;
}
