// weapondump.cpp - live read-only probe: runs the real GameReader against the live
// game and prints what the per-weapon override logic depends on (local player alive,
// active weapon item index, plus one enemy's spotted signals). touches READ memory only.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/weapondump.cpp src/core/game.cpp src/core/memory.cpp \
//       -std=c++20 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -I src -I dependencies \
//       -o %TEMP%/opencode/weapondump.exe -luser32
#include <windows.h>

#include <cstdio>
#include <string>

#include "core/game.h"

int main(int argc, char** argv) {
    std::wstring exe = L"cs2.exe";
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        const int length = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0);
        if (length > 1) {
            exe.resize(static_cast<size_t>(length));
            MultiByteToWideChar(CP_ACP, 0, argv[1], -1, exe.data(), length);
            exe.pop_back();
        }
    }

    GameReader reader;
    if (!reader.Attach(exe.c_str())) {
        std::printf("attach failed (is %ls running?)\n", exe.c_str());
        return 1;
    }
    std::printf("attached to %ls, pid %lu\n", exe.c_str(), reader.stats().pid);

    for (int i = 0; i < 20; ++i) {
        GameFrame frame;
        reader.ReadFrame(frame);

        int first_enemy_spot = -1;
        unsigned mask0 = 0;
        unsigned mask1 = 0;
        for (const EspPlayer& p : frame.players) {
            if (frame.has_local_team && p.team == frame.local_team) continue;
            first_enemy_spot = p.spotted ? 1 : 0;
            mask0 = p.spotted_mask[0];
            mask1 = p.spotted_mask[1];
            break;
        }

        std::printf("[%2d] local=%d team=%d weapon=%3d ground=%d vel=%6.0f,%6.0f yaw=%6.1f"
                    " | players=%2d bones=%2d slot=%2d enemy_spotted=%2d mask=%08X %08X vis_mask=%d\n",
                    i, frame.has_local ? 1 : 0, frame.local_team, frame.local_weapon_index,
                    frame.local_on_ground ? 1 : 0,
                    frame.local_velocity.x, frame.local_velocity.y, frame.local_yaw,
                    reader.stats().players_found, reader.stats().bones_ok,
                    reader.stats().local_slot,
                    first_enemy_spot, mask0, mask1, reader.stats().visible_mask ? 1 : 0);

        Sleep(500);
    }

    return 0;
}
