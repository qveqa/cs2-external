#include "func/esp/offscreen.h"
#include <cstdio>
#include <limits>
int main() {
    int checks=0,failures=0;
    auto check=[&](bool ok) {++checks;failures+=!ok;};
    Matrix4x4 view{};view.m[0][1]=-1;view.m[1][2]=16.0f/9;view.m[3][0]=1;
    for(GameViewport vp: {GameViewport{0,0,1920,1080},GameViewport{1920,-200,1280,1024},GameViewport{0,0,64,64}}) {
        OffscreenTriangle arrow;
        check(!OffscreenGeometry(view,{1000,0,0},vp,32,10,arrow));
        for(Vec3 world: {Vec3{1000,-3000,0},Vec3{1000,3000,0},Vec3{-1000,0,0},
                        Vec3{-1000,-1000,0},Vec3{1000,0,3000},Vec3{1000,0,-3000}}) {
            check(OffscreenGeometry(view,world,vp,160,20,arrow));
            for(const auto& p: {arrow.tip,arrow.left,arrow.right})
                check(std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=vp.x&&p.x<=vp.x+vp.width&&p.y>=vp.y&&p.y<=vp.y+vp.height);
        }
        check(OffscreenGeometry(view,{-1000,0,0},vp,32,10,arrow)&&arrow.tip.y>vp.center_y());
        check(OffscreenGeometry(view,{1000,-3000,0},vp,32,10,arrow)&&arrow.tip.x>vp.center_x());
        check(OffscreenGeometry(view,{1000,3000,0},vp,32,10,arrow)&&arrow.tip.x<vp.center_x());
    }
    OffscreenTriangle arrow;
    check(!OffscreenGeometry(Matrix4x4{}, {1,1,1},{0,0,1920,1080},32,10,arrow));
    check(!OffscreenGeometry(view,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,1920,1080},32,10,arrow));
    std::printf("offscreen: %d/%d passed\n",checks-failures,checks);
    return failures!=0;
}
