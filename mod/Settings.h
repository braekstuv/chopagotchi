// Chopagotchi - the settings in Chopagotchi.ini.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../rules/Collars.h"
#include "Log.h"

// [Settings]
static int cfgEnabled = 1;
static int cfgKey = VK_F9;
static int cfgNotify = 1;
// [Hud]: the "Chop Happiness +N" / "Chop Training +N" popups (mod/Hud.h)
static int cfgGainPopup = 1;
static float cfgGainX = 0.95f, cfgGainY = 0.90f;  // inside the HUD area, see hudToScreen
// [Advanced]
static int cfgGlobalIndex = 0;

// [Happiness] and [Training]
static float gainWalkStart = 5, gainWalkMinute = 2, gainFetch = 3, gainTrick = 1, gainPet = 5;
static float decayMinutesPerPoint = 10, lossChopKilled = 25;
static float xpWalkMinute = 1, xpFetch = 2, xpTrick = 1;
static float xpNeeded[4] = {20, 60, 120, 200};
// [Cooldowns] between rewards. The walk is in in-game hours (24 = one in-game day,
// 48 real minutes); the rest are in real minutes.
static float cdWalkGameHours = 24, cdFetchMinutes = 1, cdTrickMinutes = 5, cdPetMinutes = 10;
static long long minutesToSeconds(float minutes) { return (long long)(minutes * 60.0f); }
static long long walkCooldownGameMinutes() { return (long long)(cdWalkGameHours * 60.0f); }
// [Collars] (rules/Collars.h)
static collars::Config cfgCollars;

// [Phone]: the F9 overlay (mod/Overlay.h). Its defaults put it over the game's
// phone: measured on a 32:9 screen, the phone is centred at 0.925 of the HUD
// area's width and its top is at 0.60 of its height.
static int cfgPhone = 1, cfgPhoneHint = 1;
static float cfgPhoneX = 0.925f, cfgPhoneY = 0.807f, cfgPhoneScale = 1.0f;
// [Phone]: the app on the real phone (mod/InPhone.h)
static int cfgInPhone = 1, cfgInChecklist = 1, cfgPhoneIcon = 14, cfgPhoneCell = -1;

static float iniFloat(const char* ini, const char* section, const char* key, float def, float lo, float hi)
{
    char buf[32], defs[32];
    snprintf(defs, sizeof(defs), "%g", def);
    GetPrivateProfileStringA(section, key, defs, buf, sizeof(buf), ini);
    float v = (float)atof(buf);
    if (!std::isfinite(v)) v = def;
    return v < lo ? lo : (v > hi ? hi : v);
}

static void loadSettings()
{
    ModPath ini("Chopagotchi.ini");
    cfgEnabled = GetPrivateProfileIntA("Settings", "Enabled", 1, ini);
    cfgKey = GetPrivateProfileIntA("Settings", "Key", VK_F9, ini);
    cfgNotify = GetPrivateProfileIntA("Settings", "Notifications", 1, ini);
    cfgGainPopup = GetPrivateProfileIntA("Hud", "GainPopup", 1, ini);
    cfgGainX = iniFloat(ini, "Hud", "GainX", 0.95f, 0, 1);
    cfgGainY = iniFloat(ini, "Hud", "GainY", 0.90f, 0, 1);
    cfgGlobalIndex = GetPrivateProfileIntA("Advanced", "GlobalIndex", 0, ini);

    gainWalkStart = iniFloat(ini, "Happiness", "WalkStart", 5, 0, 100);
    gainWalkMinute = iniFloat(ini, "Happiness", "WalkPerMinute", 2, 0, 100);
    gainFetch = iniFloat(ini, "Happiness", "Fetch", 3, 0, 100);
    gainTrick = iniFloat(ini, "Happiness", "Trick", 1, 0, 100);
    gainPet = iniFloat(ini, "Happiness", "Pet", 5, 0, 100);
    decayMinutesPerPoint = iniFloat(ini, "Happiness", "MinutesApartPerPointLost", 10, 0, 100000);
    lossChopKilled = iniFloat(ini, "Happiness", "LostWhenChopIsKilled", 25, 0, 100);

    xpWalkMinute = iniFloat(ini, "Training", "WalkPerMinute", 1, 0, 1000);
    xpFetch = iniFloat(ini, "Training", "Fetch", 2, 0, 1000);
    xpTrick = iniFloat(ini, "Training", "Trick", 1, 0, 1000);
    xpNeeded[0] = iniFloat(ini, "Training", "NeededForSit", 20, 0, 1000000);
    xpNeeded[1] = iniFloat(ini, "Training", "NeededForPaw", 60, 0, 1000000);
    xpNeeded[2] = iniFloat(ini, "Training", "NeededForBeg", 120, 0, 1000000);
    xpNeeded[3] = iniFloat(ini, "Training", "NeededForPet", 200, 0, 1000000);
    for (int i = 1; i < 4; ++i)
        if (xpNeeded[i] < xpNeeded[i - 1]) xpNeeded[i] = xpNeeded[i - 1];

    cdWalkGameHours = iniFloat(ini, "Cooldowns", "WalkGameHours", 24, 0, 24 * 365);
    cdFetchMinutes = iniFloat(ini, "Cooldowns", "FetchMinutes", 1, 0, 60 * 24 * 7);
    cdTrickMinutes = iniFloat(ini, "Cooldowns", "TrickMinutes", 5, 0, 60 * 24 * 7);
    cdPetMinutes = iniFloat(ini, "Cooldowns", "PetMinutes", 10, 0, 60 * 24 * 7);

    cfgCollars.locked = GetPrivateProfileIntA("Collars", "Locked", 1, ini) != 0;
    cfgCollars.walks = (int)iniFloat(ini, "Collars", "Walks", 10, 1, 100000);
    cfgCollars.fetches = (int)iniFloat(ini, "Collars", "Fetches", 50, 1, 100000);
    cfgCollars.bondHappiness = iniFloat(ini, "Collars", "BondHappiness", 80, 0, 100);
    cfgCollars.bondDays = (int)iniFloat(ini, "Collars", "BondDays", 3, 1, 365);
    cfgCollars.findChance = iniFloat(ini, "Collars", "FindChance", 2, 0, 100);

    cfgPhone = GetPrivateProfileIntA("Phone", "App", 1, ini);
    cfgPhoneHint = GetPrivateProfileIntA("Phone", "Hint", 1, ini);
    cfgPhoneX = iniFloat(ini, "Phone", "X", 0.925f, 0, 1);
    cfgPhoneY = iniFloat(ini, "Phone", "Y", 0.807f, 0, 1);
    cfgPhoneScale = iniFloat(ini, "Phone", "Scale", 1.0f, 0.5f, 2.0f);

    cfgInPhone = GetPrivateProfileIntA("Phone", "InPhone", 1, ini);
    cfgInChecklist = GetPrivateProfileIntA("Phone", "InChecklist", 1, ini);
    cfgPhoneIcon = GetPrivateProfileIntA("Phone", "Icon", 14, ini);
    cfgPhoneCell = GetPrivateProfileIntA("Phone", "Cell", -1, ini);
    // The home screen only has artwork for the icons the game itself puts there.
    static const int icons[] = {1, 2, 4, 5, 6, 12, 14, 17, 24, 27, 39, 40, 42, 43, 54, 57, 58, 59, 62};
    bool known = false;
    for (int i : icons) known = known || i == cfgPhoneIcon;
    if (!known) cfgPhoneIcon = 14;
    if (cfgPhoneCell < -1 || cfgPhoneCell > 8) cfgPhoneCell = -1;

    logf("Settings: enabled %d, key 0x%X, gains walk %g+%g/min fetch %g trick %g pet %g, decay 1 per %g min, tricks at %g/%g/%g/%g",
         cfgEnabled, cfgKey, gainWalkStart, gainWalkMinute, gainFetch, gainTrick, gainPet, decayMinutesPerPoint,
         xpNeeded[0], xpNeeded[1], xpNeeded[2], xpNeeded[3]);
    if (!cfgInPhone) logf("In-phone app: off");
    else if (cfgInChecklist) logf("In-phone app: on, as a row inside the Checklist app");
    else logf("In-phone app: on, as an icon (icon %d, cell %d)", cfgPhoneIcon, cfgPhoneCell);
}

// Writes one setting back, for the switches on the app's Settings page.
static void saveSetting(const char* section, const char* key, int v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    WritePrivateProfileStringA(section, key, buf, ModPath("Chopagotchi.ini"));
}
