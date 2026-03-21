#include "third_person.h"
#include "../core/offsets.h"
#include <Windows.h>

namespace third_person {
namespace {

template<typename T>
static T Rd(uintptr_t addr) {
    if (!addr) return T{};
    __try { return *reinterpret_cast<const T*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return T{}; }
}

template<typename T>
static void Wr(uintptr_t addr, T val) {
    if (!addr) return;
    __try { *reinterpret_cast<T*>(addr) = val; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace

void Tick(uintptr_t client, uintptr_t view_angles, const Params& p) {
    if (!client) return;

    __try {
        uintptr_t input = Rd<uintptr_t>(client + offsets::client::dwCSGOInput);
        uintptr_t lp = Rd<uintptr_t>(client + offsets::client::dwLocalPlayerPawn);
        if (!lp || lp <= 0x10000) return;

        int life = Rd<uint8_t>(lp + offsets::base_entity::m_lifeState);
        bool allow_third = p.enabled && (life == 0);

        if (input && input > 0x10000 && input < 0x00007FFFFFFFFFFF) {
            // Toggle third person via CSGOInput flag
            Wr<uint8_t>(input + offsets::csgo_input::m_in_thirdperson, allow_third ? 1u : 0u);

            if (allow_third && view_angles) {
                // Sync camera angles with player view
                float pitch = Rd<float>(view_angles);
                float yaw = Rd<float>(view_angles + 4);
                Wr<float>(input + offsets::csgo_input::m_third_person_angles, pitch);
                Wr<float>(input + offsets::csgo_input::m_third_person_angles + 4, yaw);
                Wr<float>(input + offsets::csgo_input::m_third_person_angles + 8, 0.f);
            }
        }

        // Write chase distance via observer services (controls camera distance)
        __try {
            uintptr_t obs = Rd<uintptr_t>(lp + offsets::base_pawn::m_pObserverServices);
            if (obs && obs > 0x10000 && obs < 0x00007FFFFFFFFFFF) {
                if (allow_third) {
                    Wr<float>(obs + offsets::observer::m_flObserverChaseDistance, p.chase_distance);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace third_person
