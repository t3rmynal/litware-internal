#pragma once
#include <cstdint>

namespace third_person {

struct Params {
    bool enabled = false;
    float chase_distance = 120.f;
    float height_offset = 30.f;
};

void Tick(uintptr_t client, uintptr_t view_angles, const Params& p);

}
