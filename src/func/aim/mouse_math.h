#pragma once
#include "core/sdk.h"
#include "core/viewport.h"
#include <cmath>

// Convert a camera-space screen error to world yaw/pitch, then mouse counts.
// sensitivity/yaw/pitch describe the game's effective input scale.
inline bool MouseCountsForError(const Matrix4x4& view, const GameViewport& vp,
                               float dx, float dy, float sensitivity, float yaw, float pitch,
                               float& counts_x, float& counts_y) {
    if (!vp.valid() || !std::isfinite(sensitivity) || sensitivity <= 0.0f ||
        !std::isfinite(yaw) || !std::isfinite(pitch) ||
        std::fabs(yaw) < 1e-6f || std::fabs(pitch) < 1e-6f ||
        !std::isfinite(dx) || !std::isfinite(dy)) return false;
    const auto length = [&](int row) {
        return std::hypot(view.m[row][0], view.m[row][1], view.m[row][2]);
    };
    const float forward_length = length(3);
    const float right_length = length(0);
    const float up_length = length(1);
    if (!std::isfinite(forward_length) || !std::isfinite(right_length) || !std::isfinite(up_length) ||
        forward_length < 1e-6f || right_length < 1e-6f || up_length < 1e-6f) return false;
    float forward[3], target[3];
    const float x = 2.0f * dx / vp.width * forward_length / right_length;
    const float y = -2.0f * dy / vp.height * forward_length / up_length;
    for (int i = 0; i < 3; ++i) {
        forward[i] = view.m[3][i] / forward_length;
        target[i] = forward[i] + x * view.m[0][i] / right_length + y * view.m[1][i] / up_length;
    }
    constexpr float pi = 3.14159265358979323846f;
    const float yaw_error = std::remainder(std::atan2(target[1], target[0]) -
                                           std::atan2(forward[1], forward[0]), 2.0f*pi);
    const float pitch_error = std::atan2(forward[2], std::hypot(forward[0], forward[1])) -
                              std::atan2(target[2], std::hypot(target[0], target[1]));
    counts_x = -yaw_error * (180.0f/pi) / (sensitivity*yaw);
    counts_y = pitch_error * (180.0f/pi) / (sensitivity*pitch);
    return std::isfinite(counts_x) && std::isfinite(counts_y);
}
