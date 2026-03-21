#pragma once
#include <cstdint>

namespace third_person {

/// Per-frame third-person camera: CSGOInput flag, chase distance via observer services,
/// and heading angles on the local pawn. Offsets live in core/offsets.h.
struct Params {
    bool enabled = false;
    float chase_distance = 120.f;
    float height_offset = 30.f;
};

/// \param client Base of client.dll (0 = no-op).
/// \param view_angles Absolute address of view angles (pitch at +0, yaw at +4); 0 skips angle writes.
void Tick(uintptr_t client, uintptr_t view_angles, const Params& p);

}
