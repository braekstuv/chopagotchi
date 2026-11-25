// Chopagotchi - the mod's folder and its log file.
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>

static char g_dir[MAX_PATH] = "";  // the game folder, with a trailing backslash; set in DllMain

// A path to one of the mod's files (Chopagotchi.ini, .state.ini, .log) in the game folder.
struct ModPath {
    char s[MAX_PATH + 32];
    explicit ModPath(const char* name) { snprintf(s, sizeof(s), "%s%s", g_dir, name); }
    operator const char*() const { return s; }
};

static void logf(const char* fmt, ...)
{
    FILE* f = fopen(ModPath("Chopagotchi.log"), "a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
