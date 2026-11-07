#include "core/game.h"
#include <cstdio>

int main() {
    GameReader reader;
    if (!reader.Attach(L"cs2.exe")) { std::puts("attach failed"); return 1; }
    GameFrame frame;
    for(int i=0;i<70 && !frame.has_mouse_settings;++i)reader.ReadFrame(frame,false,false,true);
    std::printf("automatic mouse: valid=%d sensitivity=%.6f yaw=%.6f pitch=%.6f\n",
        frame.has_mouse_settings,frame.mouse_settings.sensitivity,frame.mouse_settings.yaw,frame.mouse_settings.pitch);
    for (const auto& row : frame.view.m)
        std::printf("view %.3f %.3f %.3f %.3f\n", row[0], row[1], row[2], row[3]);
    const auto& stats = reader.stats();
    std::printf("stride=%x players=%zu bones=%d layout=%x/%x local=%d team=%d\n",
        stats.entity_stride, frame.players.size(), stats.bones_ok,
        stats.bone_array_offset, stats.bone_pos_offset, frame.has_local, frame.local_team);
    for (const auto& player : frame.players)
        std::printf("hp=%d origin=%.1f,%.1f,%.1f head=%.1f,%.1f,%.1f bones=%d\n",
            player.health, player.origin.x, player.origin.y, player.origin.z,
            player.bones[kHeadBoneIndex].x, player.bones[kHeadBoneIndex].y,
            player.bones[kHeadBoneIndex].z, player.bones_valid);
    std::printf("camera sensitivity scale=%.4f\n", frame.local_sensitivity_scale);
    Memory memory;
    if (memory.Attach(L"cs2.exe")) {
        const uintptr_t base=memory.module_base(L"client.dll");
        const uintptr_t pawn=memory.read_or<uintptr_t>(base+sdk::dwLocalPlayerPawn,0);
        if(pawn) std::printf("pawn mouse sensitivity=%.6f\n",memory.read_or<float>(pawn+sdk::C_BasePlayerPawn::m_flMouseSensitivity,-999));
    }
}
