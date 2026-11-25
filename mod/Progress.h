// Chopagotchi - Chop's progress (Chopagotchi.state.ini) and putting it into the game.
//
// The mod is the keeper of these values. On an account that ever used iFruit
// the game keeps copying the app's stale numbers over the saved block, so
// whatever is earned is held here and written back every frame.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "../rules/Collars.h"
#include "../rules/Cooldowns.h"
#include "../rules/PhoneApp.h"
#include "ChopData.h"
#include "Hud.h"
#include "Log.h"
#include "Settings.h"

static const char* kTrickNames[4] = {"Sit", "Paw", "Beg", "Pet"};

static float g_xp = 0;
static float g_happy = 0;
static bool g_haveHappy = false; // false until read from the state file or the game
static int g_collar = -1;        // -1 = leave the game's collar alone
static bool g_stateDirty = false;
static bool g_live = false;      // true while story mode is running and the block is known
// Reward cooldowns, kept in the state file so reloading or restarting does not reset them.
static cooldowns::State g_cd;
// Which collars Chop has earned and how far along the rest are (rules/Collars.h).
static collars::Progress g_collarProgress;

// Random number in [0, 1) for the rare collar find; tests replace it.
// rand() starts every game session from the same seed, and its first number
// (41, so 0.0041 here) is under the default 2% chance: unseeded, the "rare"
// collar would turn up in the first minute of the first walk, every time.
static float randomRoll()
{
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        srand((unsigned)time(nullptr) ^ ((unsigned)GetTickCount64() * 2654435761u));
        rand();  // the first number after seeding still follows the seed closely
    }
    return (float)(rand() % 10000) / 10000.0f;
}
static float (*g_roll)() = randomRoll;

static void loadState()
{
    ModPath path("Chopagotchi.state.ini");
    // get(key, default) for the rules' load functions, reading one section.
    auto reader = [&](const char* section) {
        return [&path, section](const char* key, const std::string& def) {
            char v[32];
            GetPrivateProfileStringA(section, key, def.c_str(), v, sizeof(v), path);
            return std::string(v);
        };
    };
    auto state = reader("State");
    g_xp = (float)atof(state("TrainingXP", "0").c_str());
    if (!std::isfinite(g_xp) || g_xp < 0) g_xp = 0;
    std::string happy = state("Happiness", "");
    if (!happy.empty()) {
        g_happy = (float)atof(happy.c_str());
        if (!std::isfinite(g_happy) || g_happy < 0) g_happy = 0;
        if (g_happy > 100) g_happy = 100;
        g_haveHappy = true;
    }
    g_collar = (int)GetPrivateProfileIntA("State", "Collar", -1, path);
    if (g_collar < -1 || g_collar > 4) g_collar = -1;
    g_collarProgress = collars::load(reader("Collars"), g_collar);
    g_cd = cooldowns::load(reader("Cooldowns"));
}

static void saveState()
{
    if (!g_stateDirty) return;
    g_stateDirty = false;
    ModPath path("Chopagotchi.state.ini");
    // set(key, value) for the rules' save functions, writing one section.
    auto writer = [&](const char* section) {
        return [&path, section](const char* key, const std::string& v) {
            WritePrivateProfileStringA(section, key, v.c_str(), path);
        };
    };
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", g_xp);
    WritePrivateProfileStringA("State", "TrainingXP", buf, path);
    snprintf(buf, sizeof(buf), "%.2f", g_happy);
    WritePrivateProfileStringA("State", "Happiness", buf, path);
    snprintf(buf, sizeof(buf), "%d", g_collar);
    WritePrivateProfileStringA("State", "Collar", buf, path);
    cooldowns::save(g_cd, writer("Cooldowns"));
    collars::save(g_collarProgress, writer("Collars"));
}

// ---- the earning rules ------------------------------------------------------
static int mood(float happy) { return happy > 66.0f ? 2 : (happy > 33.0f ? 1 : 0); }
static const char* moodName(int m) { return m == 2 ? "happy" : (m == 1 ? "content" : "unhappy"); }

static int levelForXp(float xp) { return phoneapp::training(xp, xpNeeded).level; }

// Puts the mod's values into the game's block. A handful of memory writes,
// cheap enough to do every frame.
// Gains are fed to the game over about a second, so its own happiness bar
// (bottom right, near Chop) fills up smoothly instead of jumping.
static const ULONGLONG kGameFillMs = 1000;
static float g_gameFrom = 0, g_gameTo = -1;
static ULONGLONG g_gameAt = 0;

static float happinessForGame()
{
    if (g_gameTo != g_happy) return g_happy;
    float t = since(GetTickCount64(), g_gameAt) / kGameFillMs;
    if (t >= 1.0f) return g_happy;
    return g_gameFrom + (g_gameTo - g_gameFrom) * easeOut(t);
}

static void applyToGame()
{
    if (!g_live || !g_haveHappy || !signatureAt(g_block)) return;
    g_page0[g_block + kOffHappiness] = (uint32_t)f(happinessForGame());
    g_page0[g_block + kOffTraining] = (uint32_t)levelForXp(g_xp);
    g_page0[g_block + kOffValid] = 1;
    if (g_collar >= 0) {
        g_page0[g_block + kOffCollar] = (uint32_t)g_collar;
        g_page0[g_block + kOffUsedApp] = 1;
    }
}

static void addHappiness(float delta, const char* reason)
{
    if (delta == 0) return;
    float before = g_happy;
    float after = before + delta;
    if (after < 0) after = 0;
    if (after > 100) after = 100;
    if (after == before) return;
    if (after > before) {
        g_gameFrom = happinessForGame();
        g_gameTo = after;
        g_gameAt = GetTickCount64();
    }
    g_happy = after;
    g_stateDirty = true;
    if (fabsf(delta) >= 1.0f) logf("%s: happiness %.1f -> %.1f", reason, before, after);
    if (after > before) gainPopup(g_happyPop, after - before);
    int mb = mood(before), ma = mood(after);
    if (ma > mb) notify("Chop is %s now (%d/100).", moodName(ma), (int)after);
    else if (ma < mb) notify("Chop is getting %s (%d/100). Take him out.", moodName(ma), (int)after);
}

static void addXp(float delta)
{
    if (delta <= 0) return;
    int level = levelForXp(g_xp);
    g_xp += delta;
    g_stateDirty = true;
    gainPopup(g_trainPop, delta);
    int earned = levelForXp(g_xp);
    while (level < earned) {
        ++level;
        notify("Chop learned a new trick: %s (%d/4).", kTrickNames[level - 1], level);
    }
}

// Announces the collars in `unlocked` (a bit per collar) that were just earned.
static void collarsEarned(int unlocked)
{
    if (!unlocked) return;
    g_stateDirty = true;
    for (int i = 0; i < collars::kCount; ++i) {
        if (!(unlocked & (1 << i))) continue;
        char msg[96];
        collars::unlockMessage(cfgCollars, i, msg, sizeof(msg));
        logf("%s", msg);
        notify("%s Pick it in the app.", msg);
    }
}

// The collar Chop wears: the mod's choice, else the game's, else -1.
// dataReady is ready(), for callers that have already asked.
static int currentCollar(bool dataReady)
{
    if (g_collar >= 0) return g_collar;
    int32_t c = -1;
    if (dataReady && readInt(g_block + kOffCollar, &c) && c >= 0 && c < collars::kCount) return c;
    return -1;
}
static int currentCollar() { return g_collar >= 0 ? g_collar : currentCollar(ready()); }
