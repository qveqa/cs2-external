#include "core/projection.h"
#include "core/bone_validation.h"
#include "func/aim/mouse_math.h"
#include <cstdio>
#include <initializer_list>
#include <limits>

int main() {
    int failures = 0, cases = 0;
    auto check = [&](bool ok) { ++cases; failures += !ok; };
    for (float sensitivity : {0.5f, 1.25f, 3.0f})
    for (float width : {1280.0f, 1920.0f, 2560.0f})
    for (float focal : {0.75f, 1.0f, 2.0f}) {
        GameViewport vp{10,20,width,width*9/16};
        Matrix4x4 view{};
        view.m[0][1] = -focal; view.m[1][2] = focal*16/9; view.m[3][0] = 1;
        constexpr float radians = 0.0174532925199433f;
        const float angle = 5*radians;
        ImVec2 point;
        check(WorldToScreen(view, {1000, -1000*std::tan(angle), 0}, vp, point));
        float x, y;
        check(MouseCountsForError(view,vp,point.x-vp.center_x(),point.y-vp.center_y(),sensitivity,0.022f,0.022f,x,y));
        check(std::fabs(x*sensitivity*0.022f-5.0f)<0.001f && std::fabs(y)<0.001f);
        check(MouseCountsForError(view,vp,0,100,sensitivity,0.022f,0.022f,x,y) && y>0);
    }
    Matrix4x4 view{}; view.m[3][0]=1;
    for (float yaw_degrees : {-179.0f,179.0f})
    for (float pitch_degrees : {-70.0f,0.0f,70.0f}) {
        constexpr float r=0.0174532925199433f;
        const float cy=std::cos(yaw_degrees*r), sy=std::sin(yaw_degrees*r);
        const float cp=std::cos(pitch_degrees*r), sp=std::sin(pitch_degrees*r);
        Matrix4x4 rotated{};
        rotated.m[0][0]=sy; rotated.m[0][1]=-cy;
        rotated.m[1][0]=sp*cy*16/9; rotated.m[1][1]=sp*sy*16/9; rotated.m[1][2]=cp*16/9;
        rotated.m[3][0]=cp*cy; rotated.m[3][1]=cp*sy; rotated.m[3][2]=-sp;
        const float target_yaw=(yaw_degrees-2)*r, target_pitch=(pitch_degrees+1)*r;
        Vec3 target{1000*std::cos(target_pitch)*std::cos(target_yaw),
                    1000*std::cos(target_pitch)*std::sin(target_yaw),-1000*std::sin(target_pitch)};
        ImVec2 projected; float cx,cy_count;
        const GameViewport vp{0,0,1920,1080};
        check(WorldToScreen(rotated,target,vp,projected) &&
              MouseCountsForError(rotated,vp,projected.x-960,projected.y-540,1.25f,0.022f,0.022f,cx,cy_count) &&
              std::fabs(cx*1.25f*0.022f-2)<0.001f && std::fabs(cy_count*1.25f*0.022f-1)<0.001f);
    }
    ImVec2 point;
    check(!WorldToScreen(view, {-1,0,0}, {0,0,1920,1080}, point));
    view.m[0][0]=std::numeric_limits<float>::quiet_NaN();
    check(!WorldToScreen(view, {1,0,0}, {0,0,1920,1080}, point));
    float x,y;
    check(!MouseCountsForError(view,{0,0,1920,1080},10,0,1.25f,0.022f,0.022f,x,y));
    Vec3 bones[24]{}; for (auto& bone:bones) bone={100,100,130};
    bones[7].z=164;
    check(BonePoseLooksUsable(bones,24,7,{100,100,100}));
    bones[7].z=100;
    check(!BonePoseLooksUsable(bones,24,7,{100,100,100}));
    bones[7].z=164; bones[23].x=std::numeric_limits<float>::infinity();
    check(!BonePoseLooksUsable(bones,24,7,{100,100,100}));
    std::printf("geometry: %d/%d passed\n",cases-failures,cases);
    return failures != 0;
}
