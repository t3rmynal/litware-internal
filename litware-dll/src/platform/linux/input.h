#pragma once

#ifdef __linux__

#include <cstdint>
#include <bitset>
#include <mutex>
#include <SDL2/SDL.h>

namespace linput {

class KeyState {
private:
    std::bitset<256> key_down;
    std::bitset<256> key_pressed;
    mutable std::mutex mutex;

public:
    void OnKeyDown(int vk) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!key_down[vk]) {
            key_down[vk] = 1;
            key_pressed[vk] = 1;
        }
    }

    void OnKeyUp(int vk) {
        std::lock_guard<std::mutex> lock(mutex);
        key_down[vk] = 0;
        key_pressed[vk] = 0;
    }

    bool IsKeyDown(int vk) {
        std::lock_guard<std::mutex> lock(mutex);
        return key_down[vk];
    }

    bool IsKeyPressed(int vk) {
        std::lock_guard<std::mutex> lock(mutex);
        bool result = key_pressed[vk];
        key_pressed[vk] = 0;  // One-time flag
        return result;
    }

    void ClearPressed() {
        std::lock_guard<std::mutex> lock(mutex);
        key_pressed.reset();
    }
};

static KeyState g_key_state;

// SDL Scancode to Win32 VK mapping
inline int SdlScancodeToVk(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_A: return 0x41; // VK_A
        case SDL_SCANCODE_B: return 0x42; // VK_B
        case SDL_SCANCODE_C: return 0x43; // VK_C
        case SDL_SCANCODE_D: return 0x44; // VK_D
        case SDL_SCANCODE_E: return 0x45; // VK_E
        case SDL_SCANCODE_F: return 0x46; // VK_F
        case SDL_SCANCODE_G: return 0x47; // VK_G
        case SDL_SCANCODE_H: return 0x48; // VK_H
        case SDL_SCANCODE_I: return 0x49; // VK_I
        case SDL_SCANCODE_J: return 0x4A; // VK_J
        case SDL_SCANCODE_K: return 0x4B; // VK_K
        case SDL_SCANCODE_L: return 0x4C; // VK_L
        case SDL_SCANCODE_M: return 0x4D; // VK_M
        case SDL_SCANCODE_N: return 0x4E; // VK_N
        case SDL_SCANCODE_O: return 0x4F; // VK_O
        case SDL_SCANCODE_P: return 0x50; // VK_P
        case SDL_SCANCODE_Q: return 0x51; // VK_Q
        case SDL_SCANCODE_R: return 0x52; // VK_R
        case SDL_SCANCODE_S: return 0x53; // VK_S
        case SDL_SCANCODE_T: return 0x54; // VK_T
        case SDL_SCANCODE_U: return 0x55; // VK_U
        case SDL_SCANCODE_V: return 0x56; // VK_V
        case SDL_SCANCODE_W: return 0x57; // VK_W
        case SDL_SCANCODE_X: return 0x58; // VK_X
        case SDL_SCANCODE_Y: return 0x59; // VK_Y
        case SDL_SCANCODE_Z: return 0x5A; // VK_Z

        case SDL_SCANCODE_0: return 0x30; // VK_0
        case SDL_SCANCODE_1: return 0x31; // VK_1
        case SDL_SCANCODE_2: return 0x32; // VK_2
        case SDL_SCANCODE_3: return 0x33; // VK_3
        case SDL_SCANCODE_4: return 0x34; // VK_4
        case SDL_SCANCODE_5: return 0x35; // VK_5
        case SDL_SCANCODE_6: return 0x36; // VK_6
        case SDL_SCANCODE_7: return 0x37; // VK_7
        case SDL_SCANCODE_8: return 0x38; // VK_8
        case SDL_SCANCODE_9: return 0x39; // VK_9

        case SDL_SCANCODE_F1:  return 0x70; // VK_F1
        case SDL_SCANCODE_F2:  return 0x71; // VK_F2
        case SDL_SCANCODE_F3:  return 0x72; // VK_F3
        case SDL_SCANCODE_F4:  return 0x73; // VK_F4
        case SDL_SCANCODE_F5:  return 0x74; // VK_F5
        case SDL_SCANCODE_F6:  return 0x75; // VK_F6
        case SDL_SCANCODE_F7:  return 0x76; // VK_F7
        case SDL_SCANCODE_F8:  return 0x77; // VK_F8
        case SDL_SCANCODE_F9:  return 0x78; // VK_F9
        case SDL_SCANCODE_F10: return 0x79; // VK_F10
        case SDL_SCANCODE_F11: return 0x7A; // VK_F11
        case SDL_SCANCODE_F12: return 0x7B; // VK_F12

        case SDL_SCANCODE_ESCAPE:     return 0x1B; // VK_ESCAPE
        case SDL_SCANCODE_SPACE:      return 0x20; // VK_SPACE
        case SDL_SCANCODE_RETURN:     return 0x0D; // VK_RETURN
        case SDL_SCANCODE_TAB:        return 0x09; // VK_TAB
        case SDL_SCANCODE_BACKSPACE:  return 0x08; // VK_BACK
        case SDL_SCANCODE_INSERT:     return 0x2D; // VK_INSERT
        case SDL_SCANCODE_DELETE:     return 0x2E; // VK_DELETE
        case SDL_SCANCODE_HOME:       return 0x24; // VK_HOME
        case SDL_SCANCODE_END:        return 0x23; // VK_END
        case SDL_SCANCODE_PAGEUP:     return 0x21; // VK_PRIOR
        case SDL_SCANCODE_PAGEDOWN:   return 0x22; // VK_NEXT

        case SDL_SCANCODE_UP:    return 0x26; // VK_UP
        case SDL_SCANCODE_DOWN:  return 0x28; // VK_DOWN
        case SDL_SCANCODE_LEFT:  return 0x25; // VK_LEFT
        case SDL_SCANCODE_RIGHT: return 0x27; // VK_RIGHT

        case SDL_SCANCODE_LSHIFT:   return 0xA0; // VK_LSHIFT
        case SDL_SCANCODE_RSHIFT:   return 0xA1; // VK_RSHIFT
        case SDL_SCANCODE_LCTRL:    return 0xA2; // VK_LCONTROL
        case SDL_SCANCODE_RCTRL:    return 0xA3; // VK_RCONTROL
        case SDL_SCANCODE_LALT:     return 0xA4; // VK_LMENU
        case SDL_SCANCODE_RALT:     return 0xA5; // VK_RMENU

        case SDL_SCANCODE_NUMPAD0: return 0x60; // VK_NUMPAD0
        case SDL_SCANCODE_NUMPAD1: return 0x61; // VK_NUMPAD1
        case SDL_SCANCODE_NUMPAD2: return 0x62; // VK_NUMPAD2
        case SDL_SCANCODE_NUMPAD3: return 0x63; // VK_NUMPAD3
        case SDL_SCANCODE_NUMPAD4: return 0x64; // VK_NUMPAD4
        case SDL_SCANCODE_NUMPAD5: return 0x65; // VK_NUMPAD5
        case SDL_SCANCODE_NUMPAD6: return 0x66; // VK_NUMPAD6
        case SDL_SCANCODE_NUMPAD7: return 0x67; // VK_NUMPAD7
        case SDL_SCANCODE_NUMPAD8: return 0x68; // VK_NUMPAD8
        case SDL_SCANCODE_NUMPAD9: return 0x69; // VK_NUMPAD9

        case SDL_SCANCODE_NUMPADMULTIPLY: return 0x6A; // VK_MULTIPLY
        case SDL_SCANCODE_NUMPADADD:      return 0x6B; // VK_ADD
        case SDL_SCANCODE_NUMPADSUBTRACT: return 0x6D; // VK_SUBTRACT
        case SDL_SCANCODE_NUMPADDECIMAL:  return 0x6E; // VK_DECIMAL
        case SDL_SCANCODE_NUMPADDIVIDE:   return 0x6F; // VK_DIVIDE

        case SDL_SCANCODE_CAPSLOCK:  return 0x14; // VK_CAPITAL
        case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90; // VK_NUMLOCK
        case SDL_SCANCODE_SCROLLLOCK: return 0x91; // VK_SCROLL

        default: return 0;
    }
}

// SDL Button to Win32 VK mapping
inline int SdlButtonToVk(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return 0x01; // VK_LBUTTON
        case SDL_BUTTON_MIDDLE: return 0x04; // VK_MBUTTON
        case SDL_BUTTON_RIGHT:  return 0x02; // VK_RBUTTON
        case SDL_BUTTON_X1:     return 0x05; // VK_XBUTTON1
        case SDL_BUTTON_X2:     return 0x06; // VK_XBUTTON2
        default: return 0;
    }
}

// Process SDL event and update key state
inline void OnSdlEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_KEYDOWN:
            if (!ev.key.repeat) {
                int vk = SdlScancodeToVk(ev.key.keysym.scancode);
                if (vk) g_key_state.OnKeyDown(vk);
            }
            break;

        case SDL_KEYUP: {
            int vk = SdlScancodeToVk(ev.key.keysym.scancode);
            if (vk) g_key_state.OnKeyUp(vk);
            break;
        }

        case SDL_MOUSEBUTTONDOWN: {
            int vk = SdlButtonToVk(ev.button.button);
            if (vk) g_key_state.OnKeyDown(vk);
            break;
        }

        case SDL_MOUSEBUTTONUP: {
            int vk = SdlButtonToVk(ev.button.button);
            if (vk) g_key_state.OnKeyUp(vk);
            break;
        }

        default:
            break;
    }
}

// Get key state (matches Windows GetAsyncKeyState return value)
// Returns 0x8000 if key is down, 0 if up
inline int GetAsyncKeyState(int vk) {
    return g_key_state.IsKeyDown(vk) ? 0x8000 : 0;
}

// Check if key was pressed (one-time per poll)
inline bool IsKeyPressed(int vk) {
    return g_key_state.IsKeyPressed(vk);
}

} // namespace linput

#endif // __linux__
