#include "interfaces.h"
#include "offsets.h"
#include "../platform/compat.h"
#include "../platform/linux/proc_maps.h"

namespace interfaces {
    uintptr_t client = 0;
    uintptr_t engine = 0;
    uintptr_t entityList = 0;
    uintptr_t localPlayerController = 0;
    uintptr_t localPlayerPawn = 0;
    uintptr_t csgoInput = 0;

    bool Initialize() {
        client = (uintptr_t)GetModuleHandleA("client.dll");
        engine = (uintptr_t)GetModuleHandleA("engine2.dll");
        
        if (!client || !engine) return false;
        
        entityList = client + offsets::client::dwEntityList;
        localPlayerController = client + offsets::client::dwLocalPlayerController;
        localPlayerPawn = client + offsets::client::dwLocalPlayerPawn;
        csgoInput = client + offsets::client::dwCSGOInput;
        
        return true;
    }
}
