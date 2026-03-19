#include "debug.h"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>

#include <mutex>
#include <vector>
#include <string>

static std::vector<std::string> g_logs;
static std::mutex g_logMutex;

const std::vector<std::string>& GetDebugLogs() {
    return g_logs;
}

void ClearDebugLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logs.clear();
}

// ???????????
void BootstrapLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logs.push_back(buf);
        if (g_logs.size() > 500) g_logs.erase(g_logs.begin());
    }

    char path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, path) == 0) return;
    char fullPath[MAX_PATH];
    snprintf(fullPath, sizeof(fullPath), "%slitware_dll.log", path);
    FILE* f = nullptr;
    if (fopen_s(&f, fullPath, "a") != 0 || !f) return;
    time_t t; time(&t);
    struct tm lt; if (localtime_s(&lt, &t) == 0)
        fprintf(f, "[%02d:%02d:%02d] ", lt.tm_hour, lt.tm_min, lt.tm_sec);
    fprintf(f, "%s\n", buf);
    fclose(f);
}

void DebugLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logs.push_back(buf);
        if (g_logs.size() > 500) g_logs.erase(g_logs.begin());
    }

#ifdef LITWARE_DEBUG
    char path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, path) == 0) return;

    char fullPath[MAX_PATH];
    snprintf(fullPath, sizeof(fullPath), "%slitware_dll.log", path);

    FILE* f = nullptr;
    if (fopen_s(&f, fullPath, "a") != 0 || !f) return;

    fprintf(f, "%s\n", buf);
    fclose(f);
#endif
}
