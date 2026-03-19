// мост между dll и electron меню
// простой tcp сервер — json строки разделённые \n
// ws протокол не нужен — оба конца под нашим контролем

#include "electron_bridge.h"
#include "resource.h"
#include "../debug.h"
#include <WinSock2.h>
#include <Windows.h>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

static const unsigned short kPort = 37373;

static ElectronBridgeApplyFn s_apply = nullptr;
static std::atomic<bool> s_stop{ false };
static std::thread s_thread;
static SOCKET s_listenSocket = INVALID_SOCKET;
static SOCKET s_clientSocket = INVALID_SOCKET;
static std::atomic<int>  s_pendingMenuOpen{ -1 };
static std::atomic<int>  s_menuOpenState{ 0 };
static std::atomic<bool> s_hasNotif{ false };
static char              s_notifBuf[512]{};
static DWORD             s_electronPid = 0;
static std::atomic<int>  s_pendingVisibility{ -1 };

static void ParseJsonKeyValue(const char* json, size_t /*len*/, std::string& keyOut, std::string& valOut) {
    keyOut.clear(); valOut.clear();
    const char* keyStart = strstr(json, "\"key\":");
    if (!keyStart) return;
    keyStart += 6;
    while (*keyStart == ' ') keyStart++;
    if (*keyStart != '"') return;
    keyStart++;
    const char* keyEnd = strchr(keyStart, '"');
    if (!keyEnd) return;
    keyOut.assign(keyStart, keyEnd - keyStart);
    const char* valStart = strstr(keyEnd, "\"value\":");
    if (!valStart) return;
    valStart += 8;
    while (*valStart == ' ') valStart++;
    if (*valStart == '"') {
        valStart++;
        const char* valEnd = strchr(valStart, '"');
        if (valEnd) valOut.assign(valStart, valEnd - valStart);
    } else {
        const char* valEnd = valStart;
        while (*valEnd && *valEnd != ',' && *valEnd != '}' && *valEnd != ' ') valEnd++;
        valOut.assign(valStart, valEnd - valStart);
    }
}

// шлём json строку + \n клиенту
static void SendLine(SOCKET client, const char* msg, int msglen) {
    send(client, msg, msglen, 0);
    send(client, "\n", 1, 0);
}

static void ServerThread() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == INVALID_SOCKET) { WSACleanup(); return; }

    BOOL reuseAddr = TRUE;
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof(reuseAddr));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kPort);

    if (bind(s_listenSocket, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(s_listenSocket, 1) != 0) {
        BootstrapLog("[bridge] bind failed err=%d", WSAGetLastError());
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }
    BootstrapLog("[bridge] tcp server on port %d", kPort);

    while (!s_stop) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(s_listenSocket, &r);
        timeval tv = { 1, 0 };
        if (select((int)(s_listenSocket + 1), &r, nullptr, nullptr, &tv) <= 0 || !FD_ISSET(s_listenSocket, &r))
            continue;

        SOCKET client = accept(s_listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        BootstrapLog("[bridge] electron connected");
        s_clientSocket = client;

        // шлём начальное состояние
        {
            char msg[64];
            int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"menu_open\",\"value\":%s}", s_menuOpenState.load() ? "true" : "false");
            SendLine(client, msg, msglen);
            // cs2 в фокусе при инъекте — показываем overlay
            msglen = snprintf(msg, sizeof(msg), "{\"key\":\"overlay_visible\",\"value\":true}");
            SendLine(client, msg, msglen);
            if (s_hasNotif.load()) {
                char notifMsg[640];
                msglen = snprintf(notifMsg, sizeof(notifMsg), "{\"key\":\"notification\",\"value\":\"%s\"}", s_notifBuf);
                SendLine(client, notifMsg, msglen);
                s_hasNotif.store(false);
            }
        }

        // буфер для чтения строки от electron
        char lineBuf[4096];
        int lineLen = 0;

        while (!s_stop) {
            // шлём pending исходящие
            int pending = s_pendingMenuOpen.exchange(-1);
            if (pending >= 0) {
                char msg[64];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"menu_open\",\"value\":%s}", pending ? "true" : "false");
                SendLine(client, msg, msglen);
            }
            if (s_hasNotif.exchange(false)) {
                char msg[640];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"notification\",\"value\":\"%s\"}", s_notifBuf);
                SendLine(client, msg, msglen);
            }
            int pendingVis = s_pendingVisibility.exchange(-1);
            if (pendingVis >= 0) {
                char msg[64];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"overlay_visible\",\"value\":%s}", pendingVis ? "true" : "false");
                SendLine(client, msg, msglen);
            }

            // ждём входящие данные от electron (10ms таймаут)
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(client, &rf);
            timeval tv2 = { 0, 10000 };
            if (select((int)(client + 1), &rf, nullptr, nullptr, &tv2) <= 0 || !FD_ISSET(client, &rf))
                continue;

            // читаем по одному байту накапливая строку до \n
            char c;
            int r = recv(client, &c, 1, 0);
            if (r <= 0) break;
            if (c == '\n' || c == '\r') {
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    if (s_apply) {
                        std::string key, val;
                        ParseJsonKeyValue(lineBuf, (size_t)lineLen, key, val);
                        if (!key.empty()) s_apply(key.c_str(), val.c_str());
                    }
                    lineLen = 0;
                }
            } else if (lineLen < (int)sizeof(lineBuf) - 1) {
                lineBuf[lineLen++] = c;
            }
        }

        s_clientSocket = INVALID_SOCKET;
        closesocket(client);
        BootstrapLog("[bridge] electron disconnected");
    }

    if (s_listenSocket != INVALID_SOCKET) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

void ElectronBridge_Start(ElectronBridgeApplyFn apply_fn) {
    s_apply = apply_fn;
    if (s_thread.joinable()) return;
    s_stop = false;
    s_pendingMenuOpen = -1;
    s_menuOpenState = 0;
    s_thread = std::thread(ServerThread);
}

void ElectronBridge_SetApply(ElectronBridgeApplyFn apply_fn) {
    s_apply = apply_fn;
}

void ElectronBridge_SendMenuOpen(bool open) {
    s_menuOpenState = open ? 1 : 0;
    s_pendingMenuOpen = open ? 1 : 0;
}

void ElectronBridge_SendNotification(const char* text) {
    if (!text) return;
    strncpy_s(s_notifBuf, sizeof(s_notifBuf), text, _TRUNCATE);
    s_hasNotif.store(true);
}

bool ElectronBridge_IsMenuFocused(void) {
    if (s_electronPid == 0) return false;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == s_electronPid;
}

void ElectronBridge_SendVisibility(bool visible) {
    s_pendingVisibility = visible ? 1 : 0;
}

static void LaunchProcess(const char* path) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "\"%s\"", path);
    if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        s_electronPid = pi.dwProcessId;
        BootstrapLog("[bridge] electron started pid=%lu", s_electronPid);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        BootstrapLog("[bridge] CreateProcess failed err=%lu path=%s", GetLastError(), path);
    }
}

struct FindWndData { DWORD pid; HWND hwnd; };
static BOOL CALLBACK FindElectronWnd(HWND hwnd, LPARAM lp) {
    auto* d = reinterpret_cast<FindWndData*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != d->pid) return TRUE;
    char cls[64];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "Chrome_WidgetWin_1") != 0) return TRUE;
    if (GetParent(hwnd) != nullptr) return TRUE;
    d->hwnd = hwnd;
    return FALSE;
}

void ElectronBridge_BringToFront() {
    if (s_electronPid == 0) return;
    FindWndData d{ s_electronPid, nullptr };
    EnumWindows(FindElectronWnd, reinterpret_cast<LPARAM>(&d));
    if (!d.hwnd) return;
    ShowWindow(d.hwnd, SW_SHOW);
    SetWindowPos(d.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(d.hwnd);
}

static bool LaunchFromResource(HMODULE hMod) {
    // ищем IDR_MENU_EXE в ресурсах dll
    HRSRC hRes = FindResource(hMod, MAKEINTRESOURCE(1), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hGlob = LoadResource(hMod, hRes);
    if (!hGlob) return false;
    void* pData = LockResource(hGlob);
    if (!pData) return false;
    DWORD size = SizeofResource(hMod, hRes);
    if (size == 0) return false;

    char tmpPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpPath) == 0) return false;
    char exePath[MAX_PATH];
    snprintf(exePath, sizeof(exePath), "%slitware-menu.exe", tmpPath);

    HANDLE hFile = CreateFileA(exePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    BOOL ok = WriteFile(hFile, pData, size, &written, nullptr);
    CloseHandle(hFile);
    if (!ok || written != size) { DeleteFileA(exePath); return false; }
    LaunchProcess(exePath);
    return true;
}

void ElectronBridge_LaunchMenu(void) {
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&ElectronBridge_LaunchMenu, &hMod) || !hMod)
        return;

    if (LaunchFromResource(hMod)) { BootstrapLog("[bridge] electron from resources"); return; }

    char dllPath[MAX_PATH];
    if (!GetModuleFileNameA(hMod, dllPath, MAX_PATH)) return;
    char* slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';

    char exePath[MAX_PATH];
    snprintf(exePath, sizeof(exePath), "%s\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) { LaunchProcess(exePath); return; }

    snprintf(exePath, sizeof(exePath), "%s\\..\\..\\..\\electron-menu\\dist\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) { LaunchProcess(exePath); return; }

    snprintf(exePath, sizeof(exePath), "%s\\..\\electron-menu\\dist\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) { LaunchProcess(exePath); return; }

    BootstrapLog("[bridge] electron exe not found! dllPath=%s", dllPath);
}

void ElectronBridge_Stop(void) {
    s_stop = true;
    s_clientSocket = INVALID_SOCKET;
    if (s_listenSocket != INVALID_SOCKET) {
        SOCKET tmp = s_listenSocket;
        s_listenSocket = INVALID_SOCKET;
        closesocket(tmp);
    }
    if (s_thread.joinable())
        s_thread.join();
    s_apply = nullptr;
}

void ElectronBridge_CloseMenu(void) {
    if (s_electronPid == 0) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, s_electronPid);
    if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    s_electronPid = 0;
}
