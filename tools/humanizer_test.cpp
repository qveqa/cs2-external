#include "func/aim/humanizer.h"
#include <cstdio>
int main() {
    int checks=0,failures=0;
    auto check=[&](bool ok) {++checks;failures+=!ok;};
    for(float percent:{0.0f,25.0f,50.0f,100.0f})for(float tau:{0.0f,0.015f,0.35f})
    for(int rate:{60,144,240,400}) {
        AimHumanizer h;
        float x=100,y=30;
        for(int step=0;step<100;++step) {
            float sx,sy;h.Step(42,1234,1.0f/rate,percent,tau,100,x,y,sx,sy);
            const float next=std::hypot(x-sx,y-sy);
            check(std::isfinite(sx)&&std::isfinite(sy)&&next<=std::hypot(x,y)+0.0001f);
            x-=sx;y-=sy;
        }
    }
    AimHumanizer h;float x,y;
    h.Step(42,1234,0.004f,0,0,100,100,50,x,y);
    check(x==100 && y==50);
    h.Step(42,1234,0.004f,100,0,0,100,0,x,y);
    check(y==0); // curvature vanishes at the target, not random target jitter
    AimHumanizer a,b;float ax,ay,bx,by;
    a.Step(42,7,0.004f,100,0,100,100,0,ax,ay);
    a.Step(99,8,0.004f,100,0,100,100,0,ax,ay);
    b.Step(99,8,0.004f,100,0,100,100,0,bx,by);
    check(ax==bx&&ay==by); // target change starts a fresh curve
    check(std::fabs(ay)>0.0001f);
    std::printf("humanizer: %d/%d passed\n",checks-failures,checks);
    return failures!=0;
}
