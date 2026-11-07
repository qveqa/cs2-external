#include "core/memory.h"
#include <offsets/interfaces.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#define private public
#include "core/mouse_settings.h"
#undef private
#include <cstdio>
#include <cwchar>
int main() {
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    Memory memory;if(!memory.Attach(std::wcsrchr(exe,L'\\')+1))return 1;
    unsigned char vars[3][0x60]{},entries[128*16]{};
    char names[3][64]={"sensitivity","m_yaw","m_pitch"};
    const float values[]={1.25f,0.022f,0.022f};
    for(int i=0;i<3;++i) {
        const uintptr_t name=reinterpret_cast<uintptr_t>(names[i]),var=reinterpret_cast<uintptr_t>(vars[i]);
        const uint16_t type=7;
        std::memcpy(vars[i],&name,8);std::memcpy(vars[i]+0x28,&type,2);std::memcpy(vars[i]+0x58,values+i,4);
        std::memcpy(entries+(70+i*10)*16,&var,8);
    }
    MouseSettingsReader reader;reader.table_=reinterpret_cast<uintptr_t>(entries);reader.count_=128;
    GameMouseSettings out;
    int failures=0;
    auto check=[&](bool ok,const char* text) {std::printf("%s %s\n",ok?"pass":"FAIL",text);failures+=!ok;};
    check(!reader.Read(memory,out) && reader.cursor_==64,"discovery limited to 64 records per frame");
    check(reader.Read(memory,out) && out.sensitivity==1.25f,"discovers all mouse convars by name and type");
    const float changed=2.75f;std::memcpy(vars[0]+0x58,&changed,4);
    check(reader.Read(memory,out) && out.sensitivity==changed,"runtime sensitivity changes immediately visible");
    const float inverted=-0.022f;std::memcpy(vars[2]+0x58,&inverted,4);
    check(reader.Read(memory,out) && out.pitch==inverted,"inverted pitch supported");
    const float invalid=0;std::memcpy(vars[0]+0x58,&invalid,4);
    check(!reader.Read(memory,out),"invalid sensitivity rejected");
    std::memcpy(vars[0]+0x58,&changed,4);
    const uint16_t badtype=3;std::memcpy(vars[0]+0x28,&badtype,2);
    check(!reader.Read(memory,out) && !reader.variables_[0],"changed record invalidates cache");
    return failures!=0;
}
