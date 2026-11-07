#pragma once
#include "sdk.h"
#include "viewport.h"
#include <imgui.h>
#include <cmath>

inline bool WorldToScreen(const Matrix4x4& view, const Vec3& world,
                          const GameViewport& viewport, ImVec2& out) {
    if (!viewport.valid()) return false;
    const float w = view.m[3][0]*world.x + view.m[3][1]*world.y + view.m[3][2]*world.z + view.m[3][3];
    const float x = view.m[0][0]*world.x + view.m[0][1]*world.y + view.m[0][2]*world.z + view.m[0][3];
    const float y = view.m[1][0]*world.x + view.m[1][1]*world.y + view.m[1][2]*world.z + view.m[1][3];
    if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || w < 0.01f) return false;
    const ImVec2 point(viewport.center_x() + viewport.width * 0.5f * x/w,
                       viewport.center_y() - viewport.height * 0.5f * y/w);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    out = point;
    return true;
}
