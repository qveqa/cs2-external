#pragma once
#include "memory.h"
#include <offsets/interfaces.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

struct GameMouseSettings {
    float sensitivity=0, yaw=0, pitch=0;
};

// Read-only convar discovery. Work is bounded to 64 records per call; addresses
// are cached and only three small records are read once discovery is complete.
class MouseSettingsReader {
public:
    void Reset() { *this=MouseSettingsReader{}; }
    bool Read(Memory& memory,GameMouseSettings& out) {
        const auto now=GetTickCount64();
        if (!variables_[0] || !variables_[1] || !variables_[2]) {
            if(now<retry_ms_)return false;
            if(!table_) {
                const auto base=memory.module_base(L"tier0.dll");
                if(!base) {retry_ms_=now+2000;return false;}
                const auto object=base+cs2_dumper::interfaces::tier0_dll::VEngineCvar007;
                const auto vtable=memory.read_or<uintptr_t>(object,0);
                if(vtable<base || vtable>=memory.module_end(L"tier0.dll")) {retry_ms_=now+2000;return false;}
                // Current CUtlLinkedList layout, verified against this build.
                table_=memory.read_or<uintptr_t>(object+0x50,0);
                count_=memory.read_or<uint16_t>(object+0x5E,0);
                if(table_<0x10000 || count_<1 || count_>16384) {
                    Reset();retry_ms_=now+2000;return false;
                }
            }
            const int records=std::min(64,count_-cursor_);
            unsigned char entries[64*16]{};
            if(!memory.read_bytes(table_+cursor_*16,entries,records*16)) {
                Reset();retry_ms_=now+2000;return false;
            }
            const char* names[]={"sensitivity","m_yaw","m_pitch"};
            for(int i=0;i<records;++i) {
                uintptr_t variable=0;std::memcpy(&variable,entries+i*16,8);
                unsigned char header[0x30]{};
                if(variable<0x10000 || !memory.read_bytes(variable,header,sizeof(header)))continue;
                uint16_t type=0;std::memcpy(&type,header+0x28,2);
                if(type!=7)continue; // float32
                uintptr_t nameptr=0;std::memcpy(&nameptr,header,8);
                char name[64]{};
                if(!memory.read_bytes(nameptr,name,sizeof(name)-1))continue;
                for(int n=0;n<3;++n)if(!std::strcmp(name,names[n])) {
                    variables_[n]=variable;name_pointers_[n]=nameptr;
                }
            }
            cursor_+=records;
            if(!variables_[0] || !variables_[1] || !variables_[2]) {
                if(cursor_>=count_) {Reset();retry_ms_=now+2000;}
                return false;
            }
        }
        float values[3]{};
        for(int i=0;i<3;++i) {
            unsigned char record[0x60]{};
            if(!memory.read_bytes(variables_[i],record,sizeof(record))) {
                Reset();retry_ms_=now+2000;return false;
            }
            uintptr_t nameptr=0;uint16_t type=0;
            std::memcpy(&nameptr,record,8);std::memcpy(&type,record+0x28,2);
            if(nameptr!=name_pointers_[i] || type!=7) {Reset();retry_ms_=now+2000;return false;}
            std::memcpy(&values[i],record+0x58,4);
            if(!std::isfinite(values[i]))return false;
        }
        if(values[0]<0.001f || values[0]>1000.0f || std::fabs(values[1])<0.00001f ||
           std::fabs(values[1])>1 || std::fabs(values[2])<0.00001f || std::fabs(values[2])>1)return false;
        out={values[0],values[1],values[2]};return true;
    }
private:
    uintptr_t table_=0,variables_[3]{},name_pointers_[3]{};
    int count_=0,cursor_=0;
    unsigned long long retry_ms_=0;
};
