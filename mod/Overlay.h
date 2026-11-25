// Chopagotchi - the F9 overlay: a phone-style screen drawn where Franklin's phone
// sits, for when the app on the real phone (mod/InPhone.h) cannot be used.
//
// The menu itself lives in rules/PhoneApp.h; this part reads the phone's own
// controls, draws the screen and hands what the menu asks for to phoneAct.
// While the real phone is out without the in-phone app, a small "Chopagotchi"
// tab above it shows the key to open this one.
#pragma once
#include <windows.h>
#include <cstdio>

#include "../rules/PhoneApp.h"
#include "../rules/PhoneScreens.h"
#include "AppActions.h"
#include "InPhone.h"
#include "Natives.h"
#include "Progress.h"
#include "Settings.h"

static phoneapp::App g_app;

static void phonePress(phoneapp::Key key) { phoneAct(phoneapp::press(g_app, key, currentCollar())); }

// The phone's own controls, so arrows / Enter / Backspace and the pad's d-pad,
// A and B all work. They are switched off for the game while the app is open,
// which also keeps the real phone from opening or reacting underneath.
static const int kPhoneControls[] = {27, 172, 173, 174, 175, 176, 177, 199, 200};
static void phoneInput()
{
    for (int c : kPhoneControls) call(0xFE99B66D079CF6BCULL, 0, u(c), 1);              // DISABLE_CONTROL_ACTION
    auto hit = [](int c) { return asBool(call(0x91AEF906BCA88877ULL, 0, u(c))); };      // IS_DISABLED_CONTROL_JUST_PRESSED
    if (hit(172) || hit(27)) phonePress(phoneapp::KeyUp);
    else if (hit(173)) phonePress(phoneapp::KeyDown);
    else if (hit(176)) phonePress(phoneapp::KeySelect);
    else if (hit(177) || hit(200)) phonePress(phoneapp::KeyBack);
}

static void keyName(char* out, int n)
{
    LONG sc = (LONG)MapVirtualKeyA((UINT)cfgKey, 0 /* MAPVK_VK_TO_VSC */);
    if (sc == 0 || GetKeyNameTextA(sc << 16, out, n) <= 0) snprintf(out, n, "key %d", cfgKey);
}

// ---- drawing ------------------------------------------------------------------
struct Rgb { int r, g, b; };
static const Rgb kWhite = {255, 255, 255}, kGreen = {114, 204, 114}, kGrey = {150, 150, 160};
static const Rgb kMoodColours[3] = {{224, 80, 70}, {240, 200, 80}, {114, 204, 114}};  // unhappy, content, happy

// Layout, in screen fractions. The screen is w x h, centred on (cx, cy).
struct PhoneLayout {
    float cx, cy;
    float s, w, h, left, top, pad, row;
};

static PhoneLayout phoneLayout()
{
    PhoneLayout L;
    phoneapp::HudArea hud = phoneapp::hudArea(aspectRatio(), safeZoneSize());
    L.s = cfgPhoneScale;
    L.cx = hud.left + cfgPhoneX * hud.width;
    L.cy = hud.top + cfgPhoneY * hud.height;
    L.w = 0.16f * L.s * hud.squeeze;
    L.h = 0.40f * L.s;
    L.left = L.cx - L.w / 2;
    L.top = L.cy - L.h / 2;
    L.pad = 0.009f * L.s * hud.squeeze;
    L.row = 0.032f * L.s;
    return L;
}

static void phRow(const PhoneLayout& L, float y, const char* label, const char* value, bool selected = false,
                  Rgb vc = kWhite)
{
    if (selected) drawRect(L.left + L.pad / 2, y, L.w - L.pad, L.row, 255, 255, 255, 46);
    float ty = y + L.row * 0.12f;
    drawText(label, L.left + L.pad * 1.4f, ty, 0.32f * L.s, 235, 235, 235, 255, AlignLeft);
    if (value) drawText(value, L.left + L.w - L.pad * 1.4f, ty, 0.32f * L.s, vc.r, vc.g, vc.b, 255, AlignRight);
}

// A small grey line of text across the screen.
static void phSmall(const PhoneLayout& L, float y, const char* text, float scale, Rgb c)
{
    drawText(text, L.left + L.pad * 1.4f, y, scale * L.s, c.r, c.g, c.b, 255, AlignLeft);
}

static void phBar(const PhoneLayout& L, float y, float frac, Rgb c)
{
    float x = L.left + L.pad * 1.4f, w = L.w - L.pad * 2.8f, h = 0.009f * L.s;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    drawRect(x, y, w, h, 255, 255, 255, 40);
    if (frac > 0) drawRect(x, y, w * frac, h, c.r, c.g, c.b, 255);
}

static void drawPhoneHint()
{
    PhoneLayout L = phoneLayout();
    char key[32], text[64];
    keyName(key, sizeof(key));
    snprintf(text, sizeof(text), "Chopagotchi  [%s]", key);
    // Just above the phone's top edge (the app's top) and about the phone's width.
    float y = L.top - 0.034f * L.s, w = L.w * 0.85f;
    drawRect(L.cx - w / 2, y, w, 0.028f * L.s, 232, 120, 40, 220);
    drawText(text, L.cx, y + 0.002f * L.s, 0.32f * L.s, 255, 255, 255, 255);
}

static void drawPhoneApp(ULONGLONG now)
{
    using namespace phoneapp;
    PhoneLayout L = phoneLayout();
    // Body, header and footer.
    drawRect(L.left - 0.004f * L.s, L.top - 0.007f * L.s, L.w + 0.008f * L.s, L.h + 0.014f * L.s, 10, 10, 12, 245);
    drawRect(L.left, L.top, L.w, L.h, 28, 28, 34, 250);
    float headH = 0.05f * L.s;
    drawRect(L.left, L.top, L.w, headH, 232, 120, 40, 255);
    drawText(kPageTitles[g_app.page], L.cx, L.top + 0.006f * L.s, 0.55f * L.s, 255, 255, 255, 255, AlignCentre, 4);

    phonescreens::Status st = chopStatus();
    Rgb mc = kMoodColours[st.mood];
    Training tr = training(st.xp, st.needed);
    char v[64];
    float y = L.top + headH + L.pad;

    switch (g_app.page) {
    case Home: {
        if (st.have) {
            snprintf(v, sizeof(v), "%d/100", (int)st.happy);
            char mood1[24];
            snprintf(mood1, sizeof(mood1), "Chop is %s", moodName(st.mood));
            phRow(L, y, mood1, v, false, mc);
            phBar(L, y + L.row, st.happy / 100.0f, mc);
        } else {
            phRow(L, y, "Looking for Chop...", nullptr);
        }
        y += L.row * 1.8f;
        for (int i = 0; i < kHomeItems; ++i, y += L.row)
            phRow(L, y, kHomeLabels[i], nullptr, g_app.cursor == i);
        break;
    }
    case Overview: {
        if (!st.have) { phRow(L, y, "Chop's data isn't loaded yet.", nullptr); break; }
        snprintf(v, sizeof(v), "%s  %d/100", moodName(st.mood), (int)st.happy);
        v[0] = (char)(v[0] - 'a' + 'A');
        phRow(L, y, "Mood", v, false, mc);
        phBar(L, y + L.row, st.happy / 100.0f, mc);
        y += L.row * 1.6f;
        snprintf(v, sizeof(v), "%d", (int)st.xp);
        phRow(L, y, "Training", v);
        y += L.row;
        snprintf(v, sizeof(v), "%d of 4", tr.level);
        phRow(L, y, "Tricks", v);
        y += L.row;
        trickList(tr.level, kTrickNames, v, sizeof(v));
        phSmall(L, y + L.row * 0.1f, v, 0.28f, {170, 170, 180});
        y += L.row;
        if (tr.level < 4) {
            snprintf(v, sizeof(v), "%s at %d", kTrickNames[tr.level], (int)tr.next);
            phRow(L, y, "Next trick", v);
            phBar(L, y + L.row, tr.fraction, {93, 182, 229});
            y += L.row * 1.6f;
        } else {
            phRow(L, y, "Next trick", "All learned");
            y += L.row;
        }
        phRow(L, y, "Chop", st.chop == 2 ? "Hurt" : (st.chop == 1 ? "With you" : "At home"));
        break;
    }
    case Timers: {
        if (st.have) formatGameMinutes(st.walkMinutesLeft, v, sizeof(v));
        else snprintf(v, sizeof(v), "-");
        phRow(L, y, "Walk bonus", v, false, v[0] == 'R' ? Rgb{114, 255, 114} : kWhite);
        y += L.row;
        for (int i = 0; i < kTimers; ++i, y += L.row) {
            bool locked = tr.level < kTimerNeedLevel[i];
            if (locked) snprintf(v, sizeof(v), "Not learned");
            else formatSeconds(st.secondsLeft[i], v, sizeof(v));
            bool readyNow = !locked && v[0] == 'R';
            phRow(L, y, kTimerNames[i], v, false, readyNow ? kGreen : (locked ? Rgb{150, 150, 150} : kWhite));
        }
        phSmall(L, y + L.row * 0.3f, "Walk bonus is in game time", 0.26f, kGrey);
        break;
    }
    case Collars: {
        for (int i = 0; i < kCollars; ++i, y += L.row) {
            snprintf(v, sizeof(v), "Collar %d", i + 1);
            if (i == st.collar) phRow(L, y, v, "Wearing", g_app.cursor == i, kGreen);
            else if (st.collarLocked[i]) phRow(L, y, v, "Locked", g_app.cursor == i, kGrey);  // not said here what earns it
            else phRow(L, y, v, nullptr, g_app.cursor == i);
        }
        break;
    }
    case Settings: {
        for (int i = 0; i < kSettingItems; ++i, y += L.row)
            phRow(L, y, kSettingLabels[i], st.on[i] ? "On" : "Off", g_app.cursor == i,
                  st.on[i] ? kGreen : kMoodColours[0]);
        break;
    }
    }

    float footY = L.top + L.h - 0.03f * L.s;
    if (now < g_appNoteUntil) {
        float ny = footY - L.row * 1.1f;
        drawRect(L.left + L.pad / 2, ny, L.w - L.pad, L.row, 240, 200, 80, 60);
        drawText(g_appNote, L.cx, ny + L.row * 0.15f, 0.28f * L.s, 255, 235, 170, 255);
    }
    drawRect(L.left, footY, L.w, 0.03f * L.s, 18, 18, 22, 255);
    bool list = rows(g_app.page) > 0;
    const char* foot = g_app.page == Home ? "Enter: open    Backspace: close"
                       : list             ? "Enter: choose    Backspace: back"
                                          : "Backspace: back";
    drawText(foot, L.cx, footY + 0.005f * L.s, 0.26f * L.s, 160, 160, 170, 255);
}

// ---- every frame ------------------------------------------------------------------
// The game draws its phone on top of anything a mod draws, so when the app is
// opened with the phone out, the phone is shrunk to nothing until the app closes.
// The app then takes its place; closing the app brings the phone back as it was.
static const float kPhoneScale = 500.0f;  // the scale the game gives its phone
static bool g_phoneShrunk = false;

// The app key (F9). While the in-phone app is up the key does nothing, so the
// two apps never fight over the same controls.
static void appKeyPressed()
{
    if (!g_real.open()) phonePress(phoneapp::KeyApp);
}

// Both apps, every frame.
static void phoneFrame(ULONGLONG now)
{
    realPhoneFrame();
    if (!cfgPhone) return;
    if (g_app.open && (g_phoneShrunk || phoneOnScreen())) {
        setPhoneScale(0.0f);  // every frame: the game resets it when the phone moves
        g_phoneShrunk = true;
    } else if (!g_app.open && g_phoneShrunk) {
        setPhoneScale(kPhoneScale);
        g_phoneShrunk = false;
    }
    bool hidden = pauseMenu() || loadingScreen() || onlineSession();
    if (g_app.open) {
        if (hidden) return;
        phoneInput();
        if (g_app.open) drawPhoneApp(now);
    } else if (cfgPhoneHint && !hidden && !g_real.onPhone() && phoneOnScreen()) {
        drawPhoneHint();
    }
}
