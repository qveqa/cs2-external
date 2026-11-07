// quickstop_test.cpp - offline check of the counter-strafe key mapping. pure math:
// every case feeds a ground velocity + view yaw and expects an exact w/a/s/d set.
// no windows, no input injection, nothing can disturb the desktop.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/quickstop_test.cpp src/func/misc/quickstop.cpp \
//       src/func/misc/input.cpp -std=c++20 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
//       -I src -I dependencies -o %TEMP%/opencode/quickstop_test.exe -luser32
#include <cstdio>

#include "func/misc/quickstop.h"

namespace {

struct Case {
    const char* name;
    float       vx;
    float       vy;
    float       yaw;
    float       stop_speed;
    bool        want[4];   // w, a, s, d
};

bool Same(const bool a[4], const bool b[4]) {
    for (int i = 0; i < 4; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

}  // namespace

int main() {
    const Case cases[] = {
        { "run forward            -> s",         250,    0,   0, 25, { false, false, true,  false } },
        { "run backward           -> w",        -250,    0,   0, 25, { true,  false, false, false } },
        { "strafe right           -> a",           0, -250,   0, 25, { false, true,  false, false } },
        { "strafe left            -> d",           0,  250,   0, 25, { false, false, false, true  } },
        { "facing north, fwd      -> s",           0,  250,  90, 25, { false, false, true,  false } },
        { "facing west, east run  -> w",         250,    0, 180, 25, { true,  false, false, false } },
        { "diagonal north-east    -> s + d",     180,  180,   0, 25, { false, false, true,  true  } },
        { "below stop speed       -> none",       20,    0,   0, 25, { false, false, false, false } },
        { "above stop speed       -> s",          30,    0,   0, 25, { false, false, true,  false } },
        { "below per-axis eps     -> none",      0.20f, 0,   0,  0, { false, false, false, false } },
        { "over per-axis eps      -> s",         0.30f, 0,   0,  0, { false, false, true,  false } },
        { "diagonal under eps     -> none",     0.20f, 0.20f, 0, 0, { false, false, false, false } },
    };

    const int total = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    int failed = 0;

    for (const Case& c : cases) {
        bool want[4] = {};
        QuickstopDecide(Vec3{ c.vx, c.vy, 0.0f }, c.yaw, c.stop_speed, want);

        const bool ok = Same(want, c.want);
        if (!ok) ++failed;

        std::printf("%-32s vel (%6.2f, %6.2f) yaw %5.0f stop %4.2f  ->  [%c%c%c%c]  %s\n",
                    c.name, c.vx, c.vy, c.yaw, c.stop_speed,
                    want[0] ? 'w' : '.', want[1] ? 'a' : '.', want[2] ? 's' : '.', want[3] ? 'd' : '.',
                    ok ? "pass" : "FAIL");
    }

    std::printf("%d/%d cases pass\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
