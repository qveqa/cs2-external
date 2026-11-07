#pragma once
#include "sdk.h"
#include <cmath>

inline bool BonePoseLooksUsable(const Vec3* bones, int count, int head, const Vec3& origin) {
    if (!bones || count < 2 || head < 0 || head >= count) return false;
    for (int i = 0; i < count; ++i) {
        const float x = bones[i].x - origin.x;
        const float y = bones[i].y - origin.y;
        const float z = bones[i].z - origin.z;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            x*x + y*y + z*z > 200.0f*200.0f) return false;
    }
    const float height = bones[head].z - origin.z;
    if (height < 20.0f || height > 100.0f) return false;
    const float x = bones[0].x - bones[1].x;
    const float y = bones[0].y - bones[1].y;
    const float z = bones[0].z - bones[1].z;
    return x*x + y*y + z*z <= 60.0f*60.0f;
}
