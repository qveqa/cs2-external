// fov.h - angular fov -> screen pixels without assuming a fixed game fov
#pragma once

#include "core/sdk.h"
#include "core/viewport.h"

// full horizontal fov encoded by the world-to-clip matrix; 0 when it cannot be derived
float HorizontalFovDegrees(const Matrix4x4& view);

// radius in overlay pixels for an angular offset from the crosshair
float AimFovRadiusPixels(float fov_deg, const Matrix4x4& view, const GameViewport& viewport);
