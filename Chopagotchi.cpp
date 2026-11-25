// Chopagotchi - a Script Hook V plugin for GTA V story mode.
//
// The iFruit app used to sync Chop's happiness, trick level and collar into a
// small block of the game's saved script data. With the app gone that block
// never changes, so Chop stays unhappy and knows no tricks. This plugin lets
// you earn those values in the game instead: walking Chop, playing fetch,
// practising tricks and petting him raise his happiness and teach him tricks,
// and his happiness fades again if you leave him at home.
//
// The mod is built as one translation unit: this file includes the parts below
// in order, so tests/test_mod.cpp can compile it whole against a fake game.
//   mod/Natives.h     Script Hook V imports and the game natives used
//   mod/Log.h         the game folder and Chopagotchi.log
//   mod/Settings.h    Chopagotchi.ini
//   mod/Hud.h         messages above the minimap, happiness / training popups
//   mod/ChopData.h    finding Chop's saved data in the script globals
//   mod/Progress.h    Chop's progress, Chopagotchi.state.ini, writing it to the game
//   mod/Gameplay.h    watching walks, fetch, tricks and petting; hotkey actions
//   mod/AppActions.h  what both apps show and do
//   mod/InPhone.h     the app on Franklin's real phone
//   mod/Overlay.h     the F9 overlay app
// Game-free rules (cooldowns, collars, the app's menu, the real phone's data)
// live in rules/ and have tests of their own.

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "mod/Gameplay.h"
#include "mod/Hud.h"
#include "mod/Log.h"
#include "mod/Overlay.h"
#include "mod/Progress.h"
#include "mod/Settings.h"

static const char* kVersion = "0.0.1";

// ---- input ------------------------------------------------------------------
static bool gameFocused()
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return false;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---- script -----------------------------------------------------------------
static void scriptMain()
{
    hChop = joaat("a_c_chop");
    hWalkStat = joaat("sp_chop_walk_done");
    loadSettings();
    loadState();
    logf("Progress so far: training %.1f, happiness %s%.1f, collar %d", g_xp, g_haveHappy ? "" : "(none yet) ", g_happy, g_collar);

    ULONGLONG last = GetTickCount64();
    ULONGLONG nextTick = last + 5000;
    ULONGLONG nextSave = last + 15000;
    ULONGLONG nextKeyPoll = 0;
    bool keyWasDown = false;

    for (;;) {
        ULONGLONG now = GetTickCount64();

        if (now >= nextTick) {
            float dt = (float)(now - last) / 1000.0f;
            last = now;
            nextTick = now + (g_block >= 0 ? 250 : 5000);
            // Paused or loading time does not count for or against Chop.
            if (dt > 2.0f) {
                dt = 0;
                g_haveLastPos = false;
            }
            if (cfgEnabled && !pauseMenu() && !loadingScreen())
                tick(dt);
            else
                g_haveLastPos = false;
        }

        if (now >= nextSave) {
            nextSave = now + 15000;
            saveState();
        }

        if (cfgEnabled) {
            applyToGame();
            drawGain(now);
        }
        phoneFrame(now);

        if (now >= nextKeyPoll) {
            nextKeyPoll = now + 50;
            bool keyDown = down(cfgKey) && gameFocused();
            if (keyDown && !keyWasDown) {
                if (down(VK_SHIFT)) {
                    cycleCollar();
                } else if (down(VK_CONTROL)) {
                    cfgEnabled = !cfgEnabled;
                    notify(cfgEnabled ? "Chopagotchi: on" : "Chopagotchi: off");
                } else if (cfgPhone) {
                    appKeyPressed();
                } else {
                    showStatus();
                }
            }
            keyWasDown = keyDown;
        }

        shv_scriptWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            char* slash = strrchr(path, '\\');
            if (slash) {
                slash[1] = 0;
                snprintf(g_dir, sizeof(g_dir), "%s", path);
            }
        }
        DeleteFileA(ModPath("Chopagotchi.log"));
        logf("Chopagotchi v%s loaded", kVersion);
        shv_scriptRegister(module, scriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        shv_scriptUnregister(module);
    }
    return TRUE;
}
