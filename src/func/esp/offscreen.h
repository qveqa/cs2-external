#pragma once
#include "core/projection.h"
#include <algorithm>
#include <cmath>

struct OffscreenTriangle { ImVec2 tip,left,right; };
inline bool OffscreenGeometry(const Matrix4x4& view,const Vec3& world,const GameViewport& vp,
                              float margin,float size,OffscreenTriangle& out) {
    if(!vp.valid() || vp.width<64 || vp.height<64)return false;
    const float x=view.m[0][0]*world.x+view.m[0][1]*world.y+view.m[0][2]*world.z+view.m[0][3];
    const float y=view.m[1][0]*world.x+view.m[1][1]*world.y+view.m[1][2]*world.z+view.m[1][3];
    const float w=view.m[3][0]*world.x+view.m[3][1]*world.y+view.m[3][2]*world.z+view.m[3][3];
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w))return false;
    if(w>0.01f && std::fabs(x)<=w && std::fabs(y)<=w)return false;
    const float sx=std::hypot(view.m[0][0],view.m[0][1],view.m[0][2]);
    const float sy=std::hypot(view.m[1][0],view.m[1][1],view.m[1][2]);
    const float sw=std::hypot(view.m[3][0],view.m[3][1],view.m[3][2]);
    if(!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(sw)||sx<1e-6f||sy<1e-6f||sw<1e-6f)return false;
    float dx=x/sx,dy=-y/sy;
    // Behind the camera: preserve left/right and move the indicator to the lower edge.
    if(w<=0.01f)dy=std::max(std::fabs(dy),std::fabs(w)/sw);
    float length=std::hypot(dx,dy);
    if(length<1e-5f) {dx=0;dy=w<=0.01f?1.0f:-1.0f;length=1;}
    dx/=length;dy/=length;
    const float max_size=std::min(20.0f,std::min(vp.width,vp.height)/6.0f);
    size=std::clamp(std::isfinite(size)?size:10.0f,6.0f,max_size);
    const float max_margin=std::min(vp.width,vp.height)*0.5f-size-1;
    margin=std::clamp(std::isfinite(margin)?margin:size+10,size+2,max_margin);
    const float half_w=vp.width*0.5f-margin,half_h=vp.height*0.5f-margin;
    const float t=std::min(std::fabs(dx)>1e-6f?half_w/std::fabs(dx):1e30f,
                           std::fabs(dy)>1e-6f?half_h/std::fabs(dy):1e30f);
    out.tip={vp.center_x()+dx*t,vp.center_y()+dy*t};
    const ImVec2 base{out.tip.x-dx*size*1.6f,out.tip.y-dy*size*1.6f};
    out.left={base.x-dy*size*0.65f,base.y+dx*size*0.65f};
    out.right={base.x+dy*size*0.65f,base.y-dx*size*0.65f};
    return std::isfinite(out.tip.x)&&std::isfinite(out.tip.y);
}
