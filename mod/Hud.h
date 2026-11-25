// Chopagotchi - messages above the minimap and the happiness / training popups.
//
// When Chop's happiness goes up, the game's own bar (bottom right) fills smoothly
// (see applyToGame in mod/Progress.h) and a green "Chop Happiness +N" pops up
// beside it, floats up and fades out; training gains show "Chop Training +N"
// just above. Their position is [Hud] GainX / GainY in Chopagotchi.ini.
#pragma once
#include <cstdarg>
#include <cstdio>

#include "../rules/PhoneApp.h"
#include "Log.h"
#include "Natives.h"
#include "Settings.h"

// Logged, and shown above the minimap unless messages are switched off.
static void notify(const char* fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    logf("%s", text);
    if (cfgNotify) postFeed(text);
}

// The game keeps its HUD (minimap, bars) inside a 16:9 area in the middle of
// wider screens, shrunk by the safe-zone setting. hx/hy are positions inside
// that area (0..1); the result is a full-screen position for drawing.
static void hudToScreen(float hx, float hy, float aspect, float safeZone, float* sx, float* sy)
{
    phoneapp::HudArea hud = phoneapp::hudArea(aspect, safeZone);
    *sx = hud.left + hx * hud.width;
    *sy = hud.top + hy * hud.height;
}

static float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
static float easeOut(float t) { t = 1.0f - clamp01(t); return 1.0f - t * t * t; }
static float since(ULONGLONG now, ULONGLONG at) { return now > at ? (float)(now - at) : 0.0f; }

static const ULONGLONG kGainShowMs = 2000;
static const float kGainRise = 0.02f;      // how far a popup floats up
static const float kGainRowGap = 0.045f;   // training sits this far above happiness, so they never overlap

// One floating "Chop <what> +N" line. Gains that land while it is still up add up.
struct GainPopup {
    float total = 0;
    ULONGLONG at = 0;
    bool shown = false;
};
static GainPopup g_happyPop, g_trainPop;

static void gainPopup(GainPopup& p, float delta)
{
    ULONGLONG now = GetTickCount64();
    bool stillUp = p.shown && since(now, p.at) < kGainShowMs;
    p.total = stillUp ? p.total + delta : delta;
    p.at = now;
    p.shown = true;
}

static void drawPopup(GainPopup& p, const char* label, float rowOffset, int r, int g, int b, ULONGLONG now)
{
    if (!p.shown) return;
    float age = since(now, p.at);
    if (age >= kGainShowMs) { p.shown = false; return; }
    float t = age / kGainShowMs;
    float pop = age < 180.0f ? 0.56f - 0.14f * (age / 180.0f) : 0.42f;  // pops in big, settles
    float alpha = clamp01(age / 120.0f) * (1.0f - clamp01((t - 0.55f) / 0.45f));
    int a = (int)(255 * alpha);
    if (a <= 0) return;
    char buf[48];
    if (p.total >= 1.0f) snprintf(buf, sizeof(buf), "%s +%d", label, (int)lroundf(p.total));
    else snprintf(buf, sizeof(buf), "%s +%.1f", label, p.total);
    float sx, sy;
    hudToScreen(cfgGainX, cfgGainY, aspectRatio(), safeZoneSize(), &sx, &sy);
    drawText(buf, sx, sy - rowOffset - kGainRise * easeOut(t), pop, r, g, b, a > 255 ? 255 : a, AlignCentre, 4, true);
}

static void drawGain(ULONGLONG now)
{
    if (!cfgGainPopup || (!g_happyPop.shown && !g_trainPop.shown)) return;
    if (pauseMenu() || loadingScreen() || hudHidden()) {
        // Still let them expire while hidden.
        if (since(now, g_happyPop.at) >= kGainShowMs) g_happyPop.shown = false;
        if (since(now, g_trainPop.at) >= kGainShowMs) g_trainPop.shown = false;
        return;
    }
    drawPopup(g_happyPop, "Chop Happiness", 0, 120, 230, 120, now);
    drawPopup(g_trainPop, "Chop Training", kGainRowGap, 110, 190, 255, now);
}
