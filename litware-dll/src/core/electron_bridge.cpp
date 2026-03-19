// мост между dll и electron меню
// общение через websocket на порту 37373

#include "electron_bridge.h"
#include "resource.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <wincrypt.h>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

static const unsigned short kPort = 37373;
static const char kMagic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC11B07";

static ElectronBridgeApplyFn s_apply = nullptr;
static std::atomic<bool> s_stop{ false };
static std::thread s_thread;
static SOCKET s_listenSocket = INVALID_SOCKET;
static SOCKET s_clientSocket = INVALID_SOCKET;
static std::atomic<int>  s_pendingMenuOpen{ -1 };   // ожидает отправки
static std::atomic<int>  s_menuOpenState{ 0 };     // текущее состояние меню
static std::atomic<bool> s_hasNotif{ false };       // есть ли уведомление для отправки
static char              s_notifBuf[512]{};         // текст уведомления
static DWORD             s_electronPid = 0;         // pid запущенного electron
static std::atomic<int>  s_pendingVisibility{ -1 }; // overlay_visible ожидает отправки

static bool Base64Encode(const unsigned char* in, DWORD inLen, char* out, DWORD outLen) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DWORD i = 0, j = 0;
    for (; i + 2 < inLen; i += 3, j += 4) {
        if (j + 4 >= outLen) return false;
        out[j + 0] = tab[(in[i] >> 2) & 0x3F];
        out[j + 1] = tab[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
        out[j + 2] = tab[((in[i + 1] & 0x0F) << 2) | (in[i + 2] >> 6)];
        out[j + 3] = tab[in[i + 2] & 0x3F];
    }
    if (i < inLen) {
        if (j + 4 >= outLen) return false;
        out[j++] = tab[(in[i] >> 2) & 0x3F];
        if (i + 1 < inLen) {
            out[j++] = tab[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
            out[j++] = tab[((in[i + 1] & 0x0F) << 2)];
        } else {
            out[j++] = tab[((in[i] & 0x03) << 4)];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
    return true;
}

static bool ComputeWsAccept(const char* key, char* acceptOut, DWORD acceptLen) {
    std::string input = key;
    input += kMagic;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return false;
    bool ok = false;
    if (CryptCreateHash(prov, CALG_SHA1, 0, 0, &hHash)) {
        if (CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.size(), 0)) {
            BYTE hash[20];
            DWORD hashLen = 20;
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
                ok = Base64Encode(hash, 20, acceptOut, acceptLen);
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(prov, 0);
    return ok;
}

static bool ExtractKeyFromRequest(const char* req, size_t reqLen, std::string& keyOut) {
    const char* p = strstr(req, "Sec-WebSocket-Key:");
    if (!p || (size_t)(p - req) > reqLen) return false;
    p += 18;
    while (*p == ' ' || *p == '\t') p++;
    const char* end = strstr(p, "\r\n");
    if (!end || (size_t)(end - req) > reqLen) return false;
    keyOut.assign(p, end - p);
    return true;
}

static void ParseJsonKeyValue(const char* json, size_t /*len*/, std::string& keyOut, std::string& valOut) {
    keyOut.clear();
    valOut.clear();
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

// отправляем строку клиенту как websocket text frame
static void WsSendStr(SOCKET client, const char* msg, int msglen) {
    unsigned char frame[8];
    size_t flen = 2;
    frame[0] = 0x81; // fin + text opcode
    if (msglen <= 125) {
        frame[1] = (unsigned char)msglen;
    } else {
        frame[1] = 126;
        frame[2] = (unsigned char)(msglen >> 8);
        frame[3] = (unsigned char)(msglen & 0xFF);
        flen = 4;
    }
    // шлём header и payload отдельно чтобы не аллоцировать лишний буфер
    send(client, (char*)frame, (int)flen, 0);
    send(client, msg, msglen, 0);
}

static void ServerThread() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == INVALID_SOCKET) { WSACleanup(); return; }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kPort);

    if (bind(s_listenSocket, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(s_listenSocket, 1) != 0) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    while (!s_stop) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(s_listenSocket, &r);
        timeval tv = { 1, 0 };
        if (select((int)(s_listenSocket + 1), &r, nullptr, nullptr, &tv) <= 0 || !FD_ISSET(s_listenSocket, &r))
            continue;

        SOCKET client = accept(s_listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        char buf[4096];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { closesocket(client); continue; }
        buf[n] = '\0';

        std::string wsKey;
        if (!ExtractKeyFromRequest(buf, (size_t)n, wsKey)) {
            closesocket(client);
            continue;
        }
        char acceptKey[128];
        if (!ComputeWsAccept(wsKey.c_str(), acceptKey, sizeof(acceptKey))) {
            closesocket(client);
            continue;
        }
        char resp[512];
        int rlen = snprintf(resp, sizeof(resp),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", acceptKey);
        send(client, resp, rlen, 0);
        s_clientSocket = client;

        // отправляем состояние при подключении
        {
            int state = s_menuOpenState.load();
            char msg[64];
            int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"menu_open\",\"value\":%s}", state ? "true" : "false");
            WsSendStr(client, msg, msglen);
        }

        while (!s_stop) {
            // шлём menu_open если был запрос
            int pending = s_pendingMenuOpen.exchange(-1);
            if (pending >= 0) {
                char msg[64];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"menu_open\",\"value\":%s}", pending ? "true" : "false");
                WsSendStr(client, msg, msglen);
            }
            // шлём уведомление если есть
            if (s_hasNotif.exchange(false)) {
                char msg[640];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"notification\",\"value\":\"%s\"}", s_notifBuf);
                WsSendStr(client, msg, msglen);
            }
            // шлём overlay_visible если изменилась видимость (alt-tab)
            int pendingVis = s_pendingVisibility.exchange(-1);
            if (pendingVis >= 0) {
                char msg[64];
                int msglen = snprintf(msg, sizeof(msg), "{\"key\":\"overlay_visible\",\"value\":%s}", pendingVis ? "true" : "false");
                WsSendStr(client, msg, msglen);
            }
            fd_set r;
            FD_ZERO(&r);
            FD_SET(client, &r);
            timeval tv = { 0, 50000 };
            if (select((int)(client + 1), &r, nullptr, nullptr, &tv) <= 0 || !FD_ISSET(client, &r))
                continue;
            char hdr[14];
            int hr = recv(client, hdr, 2, 0);
            if (hr <= 0) break;
            unsigned char opcode = (unsigned char)hdr[0] & 0x0F;
            unsigned long long payloadLen = (unsigned char)hdr[1] & 0x7F;
            if ((hdr[1] & 0x80) == 0) break; // клиент не замаскировал - обрываем
            if (payloadLen == 126) {
                if (recv(client, hdr, 2, 0) != 2) break;
                payloadLen = ((unsigned char)hdr[0] << 8) | (unsigned char)hdr[1];
            } else if (payloadLen == 127) {
                if (recv(client, hdr, 8, 0) != 8) break;
                payloadLen = 0;
                for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | (unsigned char)hdr[i];
            }
            if (payloadLen > sizeof(buf) - 1) { closesocket(client); goto next_client; }
            unsigned char mask[4];
            if (recv(client, (char*)mask, 4, 0) != 4) break;
            if (recv(client, buf, (int)payloadLen, 0) != (int)payloadLen) break;
            for (size_t i = 0; i < (size_t)payloadLen; i++)
                buf[i] ^= mask[i & 3];
            buf[payloadLen] = '\0';

            if (opcode == 1 && s_apply) {
                std::string key, val;
                ParseJsonKeyValue(buf, (size_t)payloadLen, key, val);
                if (!key.empty())
                    s_apply(key.c_str(), val.c_str());
            }
            if (opcode == 8) break; // close frame
        }
        s_clientSocket = INVALID_SOCKET;
        closesocket(client);
    next_client:;
    }
    if (s_listenSocket != INVALID_SOCKET) {
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

void ElectronBridge_Start(ElectronBridgeApplyFn apply_fn) {
    if (!apply_fn) return;
    s_apply = apply_fn;
    s_stop = false;
    s_pendingMenuOpen = -1;
    s_menuOpenState = 0;
    s_thread = std::thread(ServerThread);
}

void ElectronBridge_SendMenuOpen(bool open) {
    s_menuOpenState = open ? 1 : 0;
    s_pendingMenuOpen = open ? 1 : 0;
}

void ElectronBridge_SendNotification(const char* text) {
    if (!text) return;
    // копируем текст до установки флага (memory order acquire/release через atomic)
    strncpy_s(s_notifBuf, sizeof(s_notifBuf), text, _TRUNCATE);
    s_hasNotif.store(true);
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
        s_electronPid = pi.dwProcessId;  // сохраняем pid чтобы потом найти hwnd
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// колбэк для EnumWindows — ищем главное окно electron по pid и классу
struct FindWndData { DWORD pid; HWND hwnd; };
static BOOL CALLBACK FindElectronWnd(HWND hwnd, LPARAM lp) {
    auto* d = reinterpret_cast<FindWndData*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != d->pid) return TRUE;
    // electron использует класс Chrome_WidgetWin_1 для всех своих окон
    char cls[64];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "Chrome_WidgetWin_1") != 0) return TRUE;
    // нет родителя — значит это top-level окно (не дочернее)
    if (GetParent(hwnd) != nullptr) return TRUE;
    d->hwnd = hwnd;
    return FALSE; // нашли — останавливаемся
}

// поднимает electron окно поверх всех — вызывать из cs2 процесса (у нас есть foreground доступ)
void ElectronBridge_BringToFront() {
    if (s_electronPid == 0) return;
    FindWndData d{ s_electronPid, nullptr };
    EnumWindows(FindElectronWnd, reinterpret_cast<LPARAM>(&d));
    if (!d.hwnd) return;
    // явно показываем — окно могло быть hidden через win.hide()
    ShowWindow(d.hwnd, SW_SHOW);
    // HWND_TOPMOST + SWP_SHOWWINDOW — гарантированно поверх cs2
    SetWindowPos(d.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    // SetForegroundWindow работает т.к. мы внутри cs2 который сейчас foreground
    SetForegroundWindow(d.hwnd);
}

static bool LaunchFromResource(HMODULE hMod) {
    HRSRC hRes = FindResource(hMod, MAKEINTRESOURCE(IDR_MENU_EXE), RT_RCDATA);
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
    if (!ok || written != size) {
        DeleteFileA(exePath);
        return false;
    }
    LaunchProcess(exePath);
    return true;
}

void ElectronBridge_LaunchMenu(void) {
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&ElectronBridge_LaunchMenu, &hMod) || !hMod)
        return;

    if (LaunchFromResource(hMod)) return;

    char dllPath[MAX_PATH];
    if (!GetModuleFileNameA(hMod, dllPath, MAX_PATH)) return;
    char* slash = strrchr(dllPath, '\\');
    if (!slash) return;
    *slash = '\0';

    char exePath[MAX_PATH];
    snprintf(exePath, sizeof(exePath), "%s\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) {
        LaunchProcess(exePath);
        return;
    }
    // пробуем ../../../electron-menu/dist (dll в bin/Release/)
    snprintf(exePath, sizeof(exePath), "%s\\..\\..\\..\\electron-menu\\dist\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) {
        LaunchProcess(exePath);
        return;
    }
    // запасной вариант - один уровень выше
    snprintf(exePath, sizeof(exePath), "%s\\..\\electron-menu\\dist\\litware-menu.exe", dllPath);
    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES)
        LaunchProcess(exePath);
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
