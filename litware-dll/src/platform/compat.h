#pragma once

#ifdef _WIN32
    // Windows — используй оригинальные заголовки
    #include <Windows.h>
#else
    // Linux — POSIX и стандартная библиотека C++
    #include <cstdint>
    #include <cstring>
    #include <unistd.h>
    #include <time.h>
    #include <dlfcn.h>

    // === Win32 Types ===
    typedef unsigned char  BYTE;
    typedef unsigned short WORD;
    typedef unsigned int   DWORD;
    typedef unsigned long  ULONG;
    typedef unsigned long long UINT64;
    typedef unsigned int   UINT;
    typedef int            BOOL;
    typedef void*          LPVOID;
    typedef void*          HANDLE;
    typedef unsigned short wchar_t;
    typedef signed char    CHAR;
    typedef char*          PCHAR;
    typedef unsigned char* PUCHAR;

    typedef short          SHORT;
    typedef int            INT;
    typedef int            INT32;
    typedef unsigned int   UINT32;
    typedef long long      INT64;

    #define FALSE          0
    #define TRUE           1
    #define NULL           nullptr
    #define VOID           void

    #define MAX_PATH       4096

    // === Function Declarations ===
    #define _declspec(x)

    // === Calling Convention (Linux x86-64 uses SystemV, calling convention decorators are no-ops) ===
    #define __fastcall
    #define __stdcall
    #define __cdecl
    #define APIENTRY

    // === Sleep replacement ===
    inline void Sleep(DWORD dwMilliseconds) {
        usleep(dwMilliseconds * 1000);
    }

    // === GetTickCount64 replacement ===
    inline UINT64 GetTickCount64() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (UINT64)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
    }

    // === GetTickCount replacement (32-bit, subject to overflow) ===
    inline DWORD GetTickCount() {
        return (DWORD)(GetTickCount64() & 0xFFFFFFFFULL);
    }

    // === Virtual Key Constants ===
    #define VK_LBUTTON     0x01
    #define VK_RBUTTON     0x02
    #define VK_CANCEL      0x03
    #define VK_MBUTTON     0x04
    #define VK_XBUTTON1    0x05
    #define VK_XBUTTON2    0x06

    #define VK_BACK        0x08
    #define VK_TAB         0x09
    #define VK_CLEAR       0x0C
    #define VK_RETURN      0x0D
    #define VK_SHIFT       0x10
    #define VK_CONTROL     0x11
    #define VK_MENU        0x12
    #define VK_PAUSE       0x13
    #define VK_CAPITAL     0x14

    #define VK_ESCAPE      0x1B
    #define VK_SPACE       0x20
    #define VK_PRIOR       0x21
    #define VK_NEXT        0x22
    #define VK_END         0x23
    #define VK_HOME        0x24
    #define VK_LEFT        0x25
    #define VK_UP          0x26
    #define VK_RIGHT       0x27
    #define VK_DOWN        0x28

    // Number keys (0-9)
    #define VK_0           0x30
    #define VK_1           0x31
    #define VK_2           0x32
    #define VK_3           0x33
    #define VK_4           0x34
    #define VK_5           0x35
    #define VK_6           0x36
    #define VK_7           0x37
    #define VK_8           0x38
    #define VK_9           0x39

    // Letter keys (A-Z)
    #define VK_A           0x41
    #define VK_B           0x42
    #define VK_C           0x43
    #define VK_D           0x44
    #define VK_E           0x45
    #define VK_F           0x46
    #define VK_G           0x47
    #define VK_H           0x48
    #define VK_I           0x49
    #define VK_J           0x4A
    #define VK_K           0x4B
    #define VK_L           0x4C
    #define VK_M           0x4D
    #define VK_N           0x4E
    #define VK_O           0x4F
    #define VK_P           0x50
    #define VK_Q           0x51
    #define VK_R           0x52
    #define VK_S           0x53
    #define VK_T           0x54
    #define VK_U           0x55
    #define VK_V           0x56
    #define VK_W           0x57
    #define VK_X           0x58
    #define VK_Y           0x59
    #define VK_Z           0x5A

    #define VK_LWIN        0x5B
    #define VK_RWIN        0x5C
    #define VK_APPS        0x5D

    #define VK_NUMPAD0     0x60
    #define VK_NUMPAD1     0x61
    #define VK_NUMPAD2     0x62
    #define VK_NUMPAD3     0x63
    #define VK_NUMPAD4     0x64
    #define VK_NUMPAD5     0x65
    #define VK_NUMPAD6     0x66
    #define VK_NUMPAD7     0x67
    #define VK_NUMPAD8     0x68
    #define VK_NUMPAD9     0x69

    #define VK_MULTIPLY    0x6A
    #define VK_ADD         0x6B
    #define VK_SEPARATOR   0x6C
    #define VK_SUBTRACT    0x6D
    #define VK_DECIMAL     0x6E
    #define VK_DIVIDE      0x6F

    // Function keys (F1-F12)
    #define VK_F1          0x70
    #define VK_F2          0x71
    #define VK_F3          0x72
    #define VK_F4          0x73
    #define VK_F5          0x74
    #define VK_F6          0x75
    #define VK_F7          0x76
    #define VK_F8          0x77
    #define VK_F9          0x78
    #define VK_F10         0x79
    #define VK_F11         0x7A
    #define VK_F12         0x7B

    #define VK_NUMLOCK     0x90
    #define VK_SCROLL      0x91

    #define VK_LSHIFT      0xA0
    #define VK_RSHIFT      0xA1
    #define VK_LCONTROL    0xA2
    #define VK_RCONTROL    0xA3
    #define VK_LMENU       0xA4
    #define VK_RMENU       0xA5

    #define VK_INSERT      0x2D
    #define VK_DELETE      0x2E

    // === Structured Exception Handling (lossy translation to C++ try/catch) ===
    // Note: on Linux, SIGSEGV is a signal, not a C++ exception, so catch(...) won't catch it
    // For critical reads, use sigsetjmp/siglongjmp in production
    #define __try          try
    #define __except(x)    catch(...)
    #define EXCEPTION_EXECUTE_HANDLER 1

    // === Memory Protection Constants ===
    // (used in bypass.cpp VirtualProtect replacement: mprotect)
    #define PAGE_NOACCESS           0
    #define PAGE_READONLY           1
    #define PAGE_READWRITE          2
    #define PAGE_EXECUTE            4
    #define PAGE_EXECUTE_READ       5
    #define PAGE_EXECUTE_READWRITE  6
    #define PAGE_EXECUTE_WRITECOPY  7

    // === COM/HRESULT ===
    typedef long HRESULT;
    #define S_OK            0L
    #define S_FALSE         1L
    #define E_FAIL          0x80004005L
    #define E_OUTOFMEMORY   0x8007000EL

    // === Math ===
    #define __assume(x)    __builtin_unreachable()

    // === Misc ===
    #define FARPROC         void*

#endif // _WIN32
