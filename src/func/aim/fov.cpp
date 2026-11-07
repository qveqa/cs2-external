// fov.cpp - derive the horizontal fov from the game's world-to-clip matrix.
// The projection already contains the current aspect ratio and weapon zoom, so the
// crosshair circle stays correct without reading any extra game fields.
#include "fov.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Vec3d {
    double x, y, z;
};

bool Invert(const Matrix4x4& in, Matrix4x4& out) {
    double a[4][8] = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col)
            a[row][col] = in.m[row][col];
        a[row][row + 4] = 1.0;
    }

    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row) {
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                pivot = row;
        }
        if (std::fabs(a[pivot][col]) < 1e-9) return false;

        if (pivot != col) {
            for (int k = 0; k < 8; ++k) {
                const double tmp = a[col][k];
                a[col][k] = a[pivot][k];
                a[pivot][k] = tmp;
            }
        }

        const double scale = 1.0 / a[col][col];
        for (int k = 0; k < 8; ++k)
            a[col][k] *= scale;

        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            if (factor == 0.0) continue;
            for (int k = 0; k < 8; ++k)
                a[row][k] -= factor * a[col][k];
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col)
            out.m[row][col] = static_cast<float>(a[row][col + 4]);
    }
    return true;
}

bool TransformPoint(const Matrix4x4& inv, double x, double y, double z, Vec3d& out) {
    const double rx = inv.m[0][0] * x + inv.m[0][1] * y + inv.m[0][2] * z + inv.m[0][3];
    const double ry = inv.m[1][0] * x + inv.m[1][1] * y + inv.m[1][2] * z + inv.m[1][3];
    const double rz = inv.m[2][0] * x + inv.m[2][1] * y + inv.m[2][2] * z + inv.m[2][3];
    const double rw = inv.m[3][0] * x + inv.m[3][1] * y + inv.m[3][2] * z + inv.m[3][3];

    if (!std::isfinite(rw) || std::fabs(rw) < 1e-9) return false;

    out = { rx / rw, ry / rw, rz / rw };
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool Direction(const Matrix4x4& inv, double ndc_x, double z0, double z1, Vec3d& out) {
    Vec3d a, b;
    if (!TransformPoint(inv, ndc_x, 0.0, z0, a)) return false;
    if (!TransformPoint(inv, ndc_x, 0.0, z1, b)) return false;

    out = { b.x - a.x, b.y - a.y, b.z - a.z };
    const double length = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
    if (!std::isfinite(length) || length < 1e-9) return false;

    out.x /= length;
    out.y /= length;
    out.z /= length;
    return true;
}

double AngleBetween(const Vec3d& a, const Vec3d& b) {
    double dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot > 1.0) dot = 1.0;
    if (dot < -1.0) dot = -1.0;
    return std::acos(dot);
}

}  // namespace

float HorizontalFovDegrees(const Matrix4x4& view) {
    Matrix4x4 inv;
    if (!Invert(view, inv)) return 0.0f;

    // different depth conventions map some ndc z values to infinity; try a few
    // finite pairs before giving up
    constexpr double kDepthPairs[][2] = {
        { 0.0,  0.5 },
        { -0.5, 0.0 },
        { 0.5,  1.0 },
        { -1.0, -0.5 },
    };

    for (const auto& depths : kDepthPairs) {
        Vec3d center, edge;
        if (!Direction(inv, 0.0, depths[0], depths[1], center)) continue;
        if (!Direction(inv, 1.0, depths[0], depths[1], edge)) continue;

        const double half_fov = AngleBetween(center, edge);
        if (!std::isfinite(half_fov) || half_fov < 0.01) continue;

        const double degrees = half_fov * 2.0 * 180.0 / kPi;
        if (degrees > 0.5 && degrees < 175.0)
            return static_cast<float>(degrees);
    }

    return 0.0f;
}

float AimFovRadiusPixels(float fov_deg, const Matrix4x4& view, const GameViewport& viewport) {
    if (!viewport.valid()) return 0.0f;

    float horizontal = HorizontalFovDegrees(view);
    if (horizontal < 0.5f || horizontal > 175.0f)
        horizontal = 90.0f;   // fallback when the matrix is not available yet

    const double half_rad = static_cast<double>(horizontal) * kPi / 360.0;
    const double fov_rad  = static_cast<double>(fov_deg) * kPi / 180.0;
    const double tangent  = std::tan(half_rad);
    if (tangent < 1e-6) return 0.0f;

    return static_cast<float>(std::tan(fov_rad) / tangent * (viewport.width * 0.5));
}
