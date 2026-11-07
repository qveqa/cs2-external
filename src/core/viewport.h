// viewport.h - the part of the overlay that shows the game's client area
#pragma once

struct GameViewport {
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    bool valid() const { return width > 0.0f && height > 0.0f; }
    float center_x() const { return x + width * 0.5f; }
    float center_y() const { return y + height * 0.5f; }
};
