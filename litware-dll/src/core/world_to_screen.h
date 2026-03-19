#pragma once
#include <cstdint>
#include <cmath>
#include "entity.h"

// ???????????
// ???????????
inline bool WorldToScreen(const float* m, const Vec3& pos,
                          int screenW, int screenH,
                          float& outX, float& outY) {
    // ???????????
    float w = m[12] * pos.x + m[13] * pos.y + m[14] * pos.z + m[15];
    if (w < 0.001f)
        return false;

    float invW = 1.0f / w;
    float x = (m[0] * pos.x + m[1] * pos.y + m[2] * pos.z + m[3]) * invW;
    float y = (m[4] * pos.x + m[5] * pos.y + m[6] * pos.z + m[7]) * invW;

    outX = (screenW * 0.5f) + (x * screenW * 0.5f);
    outY = (screenH * 0.5f) - (y * screenH * 0.5f);

    return true;
}
