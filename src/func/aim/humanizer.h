#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Small, time-based changes in speed and lateral curvature; no random target jitter.
struct AimHumanizer {
    uintptr_t target=0;
    float elapsed=0,phase=0,frequency=0;
    void Reset() { *this=AimHumanizer{}; }
    void Step(uintptr_t controller,uint32_t seed,float dt,float percent,float base_tau,
              float screen_error,float x,float y,float& out_x,float& out_y) {
        const float amount=std::isfinite(percent)?std::clamp(percent,0.0f,100.0f)/100.0f:0;
        if(controller!=target) {
            target=controller;elapsed=0;
            uint32_t bits=seed^static_cast<uint32_t>(controller)^0x9E3779B9u;
            bits^=bits<<13;bits^=bits>>17;bits^=bits<<5;
            phase=(bits&65535)*(6.28318530718f/65536.0f);
            frequency=2.0f+((bits>>16)&255)*(2.0f/255.0f);
        }
        const float safe_dt=std::isfinite(dt)?std::clamp(dt,0.0f,0.05f):0;
        elapsed+=safe_dt;
        const float speed=1.0f+0.18f*amount*std::sin(phase+elapsed*frequency);
        const float tau=std::max(base_tau,0.0f)+0.060f*amount;
        const float gain=tau>0? -std::expm1(-safe_dt*speed/tau):1.0f;
        const float fade=std::clamp(screen_error/60.0f,0.0f,1.0f);
        const float bend=0.12f*amount*fade*std::sin(phase+elapsed*frequency*1.37f);
        out_x=gain*(x-y*bend);
        out_y=gain*(y+x*bend);
    }
};
