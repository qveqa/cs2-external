// fov_test.cpp - offline check of the matrix-derived fov. builds synthetic
// world-to-clip matrices with a known horizontal fov, rotates/translates the view and
// verifies that fov.cpp recovers the same angle and radius. pure math, no game.
//
// build (from the repo root):
//   C:/w64devkit/bin/g++.exe tools/fov_test.cpp src/func/aim/fov.cpp -std=c++20 -O2 \
//       -I src -I dependencies -o %TEMP%/opencode/fov_test.exe
#include <cmath>
#include <cstdio>

#include "func/aim/fov.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

float ToRadians(float degrees) { return degrees * kPi / 180.0f; }

Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 out;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k)
                out.m[row][col] += a.m[row][k] * b.m[k][col];
        }
    }
    return out;
}

// directx-style left-handed perspective: w = z_view, x_ndc = x_view / z_view * scale
Matrix4x4 Perspective(float horizontal_fov_deg, float aspect) {
    const float x_scale = 1.0f / std::tan(ToRadians(horizontal_fov_deg) * 0.5f);
    const float y_scale = x_scale * aspect;   // keeps the projection sane
    constexpr float near_plane = 0.1f;
    constexpr float far_plane  = 5000.0f;

    Matrix4x4 p;
    p.m[0][0] = x_scale;
    p.m[1][1] = y_scale;
    p.m[2][2] = far_plane / (far_plane - near_plane);
    p.m[2][3] = -near_plane * far_plane / (far_plane - near_plane);
    p.m[3][2] = 1.0f;
    return p;
}

Matrix4x4 ViewTransform() {
    const float angle = ToRadians(37.0f);
    const float c     = std::cos(angle);
    const float s     = std::sin(angle);

    // rotate around z, then translate: enough to catch a transpose/order mistake
    Matrix4x4 v;
    v.m[0][0] = c;
    v.m[0][1] = -s;
    v.m[1][0] = s;
    v.m[1][1] = c;
    v.m[2][2] = 1.0f;
    v.m[0][3] = 123.0f;
    v.m[1][3] = -45.0f;
    v.m[2][3] = 67.0f;
    v.m[3][3] = 1.0f;
    return v;
}

bool Close(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

int g_failed = 0;

void CheckFov(float fov_deg, float aspect) {
    const Matrix4x4 view = Multiply(Perspective(fov_deg, aspect), ViewTransform());
    const float     got  = HorizontalFovDegrees(view);

    const bool ok = Close(got, fov_deg, 0.05f);
    if (!ok) ++g_failed;

    std::printf("fov %6.2f  aspect %.2f  ->  %6.2f  %s\n",
                fov_deg, aspect, got, ok ? "pass" : "FAIL");
}

void CheckRadius(float fov_deg, float viewport_width, float expected) {
    const Matrix4x4 view = Multiply(Perspective(90.0f, 16.0f / 9.0f), ViewTransform());

    GameViewport viewport;
    viewport.width  = viewport_width;
    viewport.height = 900.0f;

    const float got = AimFovRadiusPixels(fov_deg, view, viewport);
    const bool ok = Close(got, expected, 0.5f);
    if (!ok) ++g_failed;

    std::printf("radius at %.1f deg, width %.0f  ->  %.2f (want %.2f)  %s\n",
                fov_deg, viewport_width, got, expected, ok ? "pass" : "FAIL");
}

}  // namespace

int main() {
    CheckFov(90.0f, 16.0f / 9.0f);
    CheckFov(60.0f, 21.0f / 9.0f);
    CheckFov(106.26f, 4.0f / 3.0f);
    CheckFov(40.0f, 16.0f / 10.0f);

    // 90 deg horizontal: a 45 deg offset reaches the screen edge
    CheckRadius(45.0f, 1600.0f, 800.0f);
    // 30 deg on a 60 deg fov also reaches the edge; 90 deg fov halves it
    CheckRadius(30.0f, 1600.0f, 800.0f * std::tan(ToRadians(30.0f)) / std::tan(ToRadians(45.0f)));

    Matrix4x4 zero;
    const bool zero_ok = HorizontalFovDegrees(zero) == 0.0f;
    if (!zero_ok) ++g_failed;
    std::printf("zero matrix -> fallback angle  %s\n", zero_ok ? "pass" : "FAIL");

    // an unusable matrix falls back to the documented 90 deg approximation
    GameViewport viewport;
    viewport.width  = 1600.0f;
    viewport.height = 900.0f;
    const float fallback = AimFovRadiusPixels(45.0f, zero, viewport);
    const bool fallback_ok = Close(fallback, 800.0f, 0.5f);
    if (!fallback_ok) ++g_failed;
    std::printf("zero matrix -> 90 deg fallback radius %.2f  %s\n",
                fallback, fallback_ok ? "pass" : "FAIL");

    std::printf("%s\n", g_failed == 0 ? "all fov cases pass" : "fov cases FAILED");
    return g_failed == 0 ? 0 : 1;
}
