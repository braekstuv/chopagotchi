// Fake <windows.h> for host-side tests. Only what Chopagotchi.cpp uses, with
// test hooks (all in namespace fake) to drive the clock, ini files, keys and
// the memory regions VirtualQuery reports.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

typedef void* HMODULE;
typedef void* HWND;
typedef void* LPVOID;
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned long long ULONGLONG;
typedef long LONG;
typedef unsigned int UINT;
typedef uintptr_t SIZE_T;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_F9 0x78
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100

struct SYSTEMTIME { unsigned short wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};

namespace fake {
struct Region { uintptr_t lo, hi; DWORD state, protect; };
inline ULONGLONG tickMs = 1000000;
inline std::map<std::string, std::map<std::string, std::string>> iniFiles; // "file|section" -> key -> value
inline std::vector<Region> regions;
inline std::map<int, bool> keys;
inline bool focused = true;
inline std::string modulePath = "C:\\Games\\GTAV\\Chopagotchi.asi";
inline std::vector<std::string> deletedFiles;

inline std::string iniKey(const char* file, const char* section) { return std::string(file) + "|" + section; }
inline void setIni(const char* file, const char* section, const char* key, const char* value)
{
    iniFiles[iniKey(file, section)][key] = value;
}
inline const char* getIni(const char* file, const char* section, const char* key)
{
    auto s = iniFiles.find(iniKey(file, section));
    if (s == iniFiles.end()) return nullptr;
    auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : k->second.c_str();
}
inline void addRegion(const void* p, size_t bytes, DWORD protect = PAGE_READWRITE, DWORD state = MEM_COMMIT)
{
    regions.push_back({(uintptr_t)p, (uintptr_t)p + bytes, state, protect});
}
inline void reset()
{
    tickMs = 1000000;
    iniFiles.clear();
    regions.clear();
    keys.clear();
    focused = true;
    deletedFiles.clear();
}
} // namespace fake

inline ULONGLONG GetTickCount64() { return fake::tickMs; }
inline void GetLocalTime(SYSTEMTIME* t) { memset(t, 0, sizeof(*t)); }

inline DWORD GetPrivateProfileStringA(const char* section, const char* key, const char* def, char* out, DWORD n,
                                      const char* file)
{
    const char* v = fake::getIni(file, section, key);
    snprintf(out, n, "%s", v ? v : (def ? def : ""));
    return (DWORD)strlen(out);
}
inline unsigned GetPrivateProfileIntA(const char* section, const char* key, int def, const char* file)
{
    const char* v = fake::getIni(file, section, key);
    return v ? (unsigned)atoi(v) : (unsigned)def;
}
inline BOOL WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file)
{
    fake::setIni(file, section, key, value);
    return TRUE;
}

inline SIZE_T VirtualQuery(const void* p, MEMORY_BASIC_INFORMATION* mbi, SIZE_T)
{
    uintptr_t a = (uintptr_t)p;
    for (auto& r : fake::regions) {
        if (a >= r.lo && a < r.hi) {
            memset(mbi, 0, sizeof(*mbi));
            mbi->BaseAddress = (void*)r.lo;
            mbi->RegionSize = r.hi - r.lo;
            mbi->State = r.state;
            mbi->Protect = r.protect;
            return sizeof(*mbi);
        }
    }
    return 0;
}

inline HWND GetForegroundWindow() { return fake::focused ? (HWND)0x1234 : nullptr; }
inline DWORD GetCurrentProcessId() { return 42; }
inline DWORD GetWindowThreadProcessId(HWND, DWORD* pid) { *pid = 42; return 1; }
inline short GetAsyncKeyState(int vk) { return fake::keys[vk] ? (short)0x8000 : 0; }
inline DWORD GetModuleFileNameA(HMODULE, char* out, DWORD n)
{
    snprintf(out, n, "%s", fake::modulePath.c_str());
    return (DWORD)strlen(out);
}
inline BOOL DeleteFileA(const char* path) { fake::deletedFiles.push_back(path); return TRUE; }
inline UINT MapVirtualKeyA(UINT vk, UINT) { return vk == VK_F9 ? 0x43 : 0; }
inline int GetKeyNameTextA(LONG, char* out, int n) { return snprintf(out, n, "F9"); }
