// Chopagotchi - what both apps (the F9 overlay and the one on the real phone)
// show and do. The menu itself is rules/PhoneApp.h; this gathers Chop's status
// for the pages and carries out the actions the menu returns.
#pragma once
#include <cstdio>
#include <ctime>

#include "../rules/Collars.h"
#include "../rules/PhoneApp.h"
#include "../rules/PhoneScreens.h"
#include "ChopData.h"
#include "Gameplay.h"
#include "Log.h"
#include "Natives.h"
#include "Progress.h"
#include "Settings.h"

// A short note from an action ("Chop comes running!"). The overlay shows it at
// the bottom of its screen for a few seconds; while g_appNoteToFeed is set (the
// app on the real phone) it goes above the minimap instead.
static char g_appNote[96] = "";
static ULONGLONG g_appNoteUntil = 0;
static bool g_appNoteToFeed = false;

static void appNote(const char* text)
{
    snprintf(g_appNote, sizeof(g_appNote), "%s", text);
    logf("Phone: %s", text);
    if (g_appNoteToFeed) postFeed(g_appNote);
    else g_appNoteUntil = GetTickCount64() + 3500;
}

// Everything the app's pages show.
static phonescreens::Status chopStatus()
{
    phonescreens::Status st;
    st.have = ready() && g_haveHappy;
    st.happy = g_happy;
    st.mood = mood(st.happy);
    st.xp = g_xp;
    for (int i = 0; i < 4; ++i) st.needed[i] = xpNeeded[i];
    st.trickNames = kTrickNames;
    st.chop = chopWhere();
    st.collar = currentCollar();
    for (int i = 0; i < collars::kCount && i < phoneapp::kCollars; ++i)
        st.collarLocked[i] = !collars::isUnlocked(g_collarProgress, cfgCollars, i);
    st.walkMinutesLeft = phoneapp::walkMinutesLeft(g_cd.lastWalkGameMinute, gameMinutes(), walkCooldownGameMinutes());
    long long wall = (long long)time(nullptr);
    const long long readyAt[phoneapp::kTimers] = {g_cd.fetchReadyAt, g_cd.trickReadyAt[0], g_cd.trickReadyAt[1],
                                                  g_cd.trickReadyAt[2], g_cd.petReadyAt};
    for (int i = 0; i < phoneapp::kTimers; ++i) st.secondsLeft[i] = phoneapp::secondsLeft(readyAt[i], wall);
    st.on[phoneapp::SettingMod] = cfgEnabled != 0;
    st.on[phoneapp::SettingMessages] = cfgNotify != 0;
    st.on[phoneapp::SettingGainPopup] = cfgGainPopup != 0;
    return st;
}

static void phoneWhistle()
{
    if (!ready()) { appNote("Chop's data isn't loaded yet."); return; }
    int player = playerPed();
    if (!g_chop || !entityExists(g_chop)) { appNote("Chop isn't with you. Pick him up at home."); return; }
    if (entityDead(g_chop)) { appNote("Chop is hurt."); return; }
    if (inAnyVehicle(player)) { appNote("Get out of the car first."); return; }
    float a[3], b[3];
    entityCoords(player, a);
    entityCoords(g_chop, b);
    float dist = distance(a, b);
    if (dist < 4.0f) { appNote("Chop is right beside you."); return; }
    if (dist > 120.0f) { appNote("Chop is too far away to hear you."); return; }
    goToEntity(g_chop, player, 2.0f, 3.0f);  // stop 2 m away, sprinting
    appNote("You whistle. Chop comes running!");
}

static void phoneAct(const phoneapp::Action& act)
{
    switch (act.type) {
    case phoneapp::Whistle: phoneWhistle(); break;
    case phoneapp::SetCollar:
        if (!ready()) { appNote("Chop's data isn't loaded yet."); break; }
        if (!collars::isUnlocked(g_collarProgress, cfgCollars, act.value)) {
            // The app only says "locked"; how far along he is goes to the log.
            char h[64], note[48];
            collars::hint(g_collarProgress, cfgCollars, act.value, h, sizeof(h));
            logf("Collar %d not set. %s", act.value + 1, h);
            snprintf(note, sizeof(note), "Collar %d is locked.", act.value + 1);
            appNote(note);
            break;
        }
        g_collar = act.value;
        g_stateDirty = true;
        applyToGame();
        logf("Collar set to %d", g_collar);
        break;
    case phoneapp::ToggleMod:
        cfgEnabled = !cfgEnabled;
        saveSetting("Settings", "Enabled", cfgEnabled);
        logf("Chopagotchi %s from the phone", cfgEnabled ? "on" : "off");
        break;
    case phoneapp::ToggleMessages:
        cfgNotify = !cfgNotify;
        saveSetting("Settings", "Notifications", cfgNotify);
        break;
    case phoneapp::ToggleGainPopup:
        cfgGainPopup = !cfgGainPopup;
        saveSetting("Hud", "GainPopup", cfgGainPopup);
        break;
    default: break;
    }
}
