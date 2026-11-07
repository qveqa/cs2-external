// override_test.cpp - offline check of the per-weapon override resolution. pure
// settings math, no game, no input: a weapon with an active override must pick up
// its own values, everything else must fall back to the globals.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/override_test.cpp -std=c++20 -O2 -I src \
//       -o %TEMP%/opencode/override_test.exe
#include <cstdio>

#include "core/settings.h"

namespace {

bool Near(float a, float b) {
    const float d = a - b;
    return d > -0.001f && d < 0.001f;
}

}  // namespace

int main() {
    Settings s;
    s.aim.fov = 5.0f;
    s.aim.smoothing = 0.50f;
    s.aim.hitbox = 0;
    s.trigger.delay_ms = 60;
    s.trigger.target = 1;

    // ak-47 gets its own values, awp does not
    const int ak = WeaponOverrideSlot(7);
    if (ak < 0) {
        std::printf("ak-47 has no override slot  FAIL\n");
        return 1;
    }
    s.weapons[ak].active           = true;
    s.weapons[ak].fov              = 2.0f;
    s.weapons[ak].smoothing        = 0.10f;
    s.weapons[ak].hitbox           = 1;
    s.weapons[ak].trigger_delay_ms = 200;
    s.weapons[ak].trigger_target   = 0;

    const AimSettings ak_aim = EffectiveAim(s, 7);
    const AimSettings awp_aim = EffectiveAim(s, 9);
    const TriggerSettings ak_trig = EffectiveTrigger(s, 7);
    const TriggerSettings awp_trig = EffectiveTrigger(s, 9);

    int failed = 0;

    const bool ak_ok = Near(ak_aim.fov, 2.0f) && Near(ak_aim.smoothing, 0.10f) && ak_aim.hitbox == 1 &&
                       ak_trig.delay_ms == 200 && ak_trig.target == 0;
    const bool awp_ok = Near(awp_aim.fov, 5.0f) && Near(awp_aim.smoothing, 0.50f) && awp_aim.hitbox == 0 &&
                        awp_trig.delay_ms == 60 && awp_trig.target == 1;

    std::printf("ak-47 override  fov %.1f  smoothing %.2f  hitbox %d  delay %d  target %d  %s\n",
                ak_aim.fov, ak_aim.smoothing, ak_aim.hitbox, ak_trig.delay_ms, ak_trig.target,
                ak_ok ? "pass" : "FAIL");
    std::printf("awp fallback    fov %.1f  smoothing %.2f  hitbox %d  delay %d  target %d  %s\n",
                awp_aim.fov, awp_aim.smoothing, awp_aim.hitbox, awp_trig.delay_ms, awp_trig.target,
                awp_ok ? "pass" : "FAIL");
    if (!ak_ok) ++failed;
    if (!awp_ok) ++failed;

    // an inactive override must not leak
    s.weapons[ak].active = false;
    const AimSettings off_aim = EffectiveAim(s, 7);
    const bool off_ok = Near(off_aim.fov, 5.0f) && Near(off_aim.smoothing, 0.50f);
    std::printf("disabled override falls back                    %s\n", off_ok ? "pass" : "FAIL");
    if (!off_ok) ++failed;

    std::printf(failed == 0 ? "override resolution ok\n" : "%d checks FAILED\n", failed);
    return failed == 0 ? 0 : 1;
}
