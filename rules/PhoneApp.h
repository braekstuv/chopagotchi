// Chopagotchi - the phone app's menu and the text it shows.
//
// Pure logic with no game or Windows calls, so it can be tested on any host
// (see tests/test_phone_app.cpp). Chopagotchi.cpp reads the keys, draws the
// screen and carries out the actions this returns.
#pragma once
#include <cstddef>
#include <cstdio>

#include "Cooldowns.h"

namespace phoneapp {

enum Page { Home, Overview, Timers, Collars, Settings };
enum Key { KeyApp, KeyUp, KeyDown, KeySelect, KeyBack };

enum ActionType {
    None,
    Opened,
    Closed,
    Whistle,
    SetCollar,  // value = collar 0..4
    ToggleMod,
    ToggleMessages,
    ToggleGainPopup,
};
struct Action {
    ActionType type = None;
    int value = 0;
};

static const int kCollars = 5;
// Home menu, top to bottom. Whistle is an action, the rest open a page.
enum HomeItem { ItemOverview, ItemTimers, ItemCollar, ItemWhistle, ItemSettings, kHomeItems };
static const char* const kHomeLabels[kHomeItems] = {"Overview", "Cooldowns", "Collar", "Whistle for Chop", "Settings"};
enum SettingItem { SettingMod, SettingMessages, SettingGainPopup, kSettingItems };
static const char* const kSettingLabels[kSettingItems] = {"Chopagotchi", "Messages", "Happiness popup"};

struct App {
    bool open = false;
    Page page = Home;
    int cursor = 0;  // highlighted row on Home, Collars and Settings
};

inline int rows(Page p)
{
    switch (p) {
    case Home: return kHomeItems;
    case Collars: return kCollars;
    case Settings: return kSettingItems;
    default: return 0;
    }
}

inline HomeItem homeItemFor(Page p)
{
    switch (p) {
    case Overview: return ItemOverview;
    case Timers: return ItemTimers;
    case Collars: return ItemCollar;
    default: return ItemSettings;
    }
}

// Applies one key press. currentCollar (0..4, or -1 if unknown) puts the
// cursor on the collar Chop wears when the Collar page opens.
inline Action press(App& app, Key key, int currentCollar)
{
    Action act;
    if (!app.open) {
        if (key == KeyApp) {
            app = App();
            app.open = true;
            act.type = Opened;
        }
        return act;
    }
    if (key == KeyApp || (key == KeyBack && app.page == Home)) {
        app.open = false;
        act.type = Closed;
        return act;
    }
    if (key == KeyBack) {
        app.cursor = homeItemFor(app.page);
        app.page = Home;
        return act;
    }
    int n = rows(app.page);
    if (key == KeyUp || key == KeyDown) {
        if (n > 0) app.cursor = (app.cursor + (key == KeyUp ? n - 1 : 1)) % n;
        return act;
    }
    // KeySelect
    switch (app.page) {
    case Home:
        switch (app.cursor) {
        case ItemOverview: app.page = Overview; break;
        case ItemTimers: app.page = Timers; break;
        case ItemCollar:
            app.page = Collars;
            app.cursor = (currentCollar >= 0 && currentCollar < kCollars) ? currentCollar : 0;
            return act;
        case ItemWhistle: act.type = Whistle; return act;
        case ItemSettings: app.page = Settings; break;
        }
        app.cursor = 0;
        break;
    case Collars:
        act.type = SetCollar;
        act.value = app.cursor;
        break;
    case Settings:
        act.type = app.cursor == SettingMod ? ToggleMod : (app.cursor == SettingMessages ? ToggleMessages : ToggleGainPopup);
        break;
    default:
        break;
    }
    return act;
}

// ---- where the app sits ----------------------------------------------------

// The game keeps its HUD (minimap, phone) in a 16:9 area centred on wider
// screens, shrunk by the safe-zone setting. Positions in the ini are inside
// that area (0,0 = its top left, 1,1 = its bottom right), so the app lands by
// the phone on 16:9, 21:9 and 32:9 alike.
struct HudArea {
    float left, top, width, height;  // screen fractions of the safe 16:9 area
    float squeeze;                   // horizontal sizes are multiplied by this (1 on 16:9)
};

inline HudArea hudArea(float aspect, float safeZone)
{
    const float wide = 16.0f / 9.0f;
    if (!(aspect > 0.5f && aspect < 10.0f)) aspect = wide;  // also catches NaN
    if (!(safeZone >= 0.5f && safeZone <= 1.0f)) safeZone = 1.0f;
    HudArea a;
    a.squeeze = aspect > wide ? wide / aspect : 1.0f;
    float margin = (1.0f - safeZone) / 2.0f;
    a.width = a.squeeze * safeZone;
    a.height = safeZone;
    a.left = 0.5f - a.squeeze / 2.0f + a.squeeze * margin;
    a.top = margin;
    return a;
}

// ---- what the screens show --------------------------------------------------

// Training: how many tricks are known, and progress towards the next one.
struct Training {
    int level = 0;          // tricks known, 0..4
    float fraction = 1.0f;  // 0..1 of the way from the previous trick to the next
    float next = 0;         // training needed for the next trick (0 when all known)
};

inline Training training(float xp, const float needed[4])
{
    Training t;
    while (t.level < 4 && xp >= needed[t.level]) ++t.level;
    if (t.level == 4) return t;
    float from = t.level > 0 ? needed[t.level - 1] : 0.0f;
    float span = needed[t.level] - from;
    t.next = needed[t.level];
    t.fraction = span > 0 ? (xp - from) / span : 1.0f;
    if (t.fraction < 0) t.fraction = 0;
    if (t.fraction > 1) t.fraction = 1;
    return t;
}

// "Sit, Paw" for the tricks known, or "None yet".
inline void trickList(int level, const char* const names[4], char* out, size_t n)
{
    if (n == 0) return;
    out[0] = 0;
    if (level <= 0) {
        std::snprintf(out, n, "None yet");
        return;
    }
    size_t used = 0;
    for (int i = 0; i < level && i < 4 && used < n; ++i)
        used += std::snprintf(out + used, n - used, i ? ", %s" : "%s", names[i]);
}

// Real seconds left as "Ready", "45s", "3m 05s" or "2h 10m".
inline void formatSeconds(long long left, char* out, size_t n)
{
    if (left <= 0) std::snprintf(out, n, "Ready");
    else if (left < 60) std::snprintf(out, n, "%llds", left);
    else if (left < 3600) std::snprintf(out, n, "%lldm %02llds", left / 60, left % 60);
    else std::snprintf(out, n, "%lldh %02lldm", left / 3600, (left % 3600) / 60);
}

// In-game minutes left as "Ready", "45m" or "5h 20m".
inline void formatGameMinutes(long long left, char* out, size_t n)
{
    if (left <= 0) std::snprintf(out, n, "Ready");
    else if (left < 60) std::snprintf(out, n, "%lldm", left);
    else std::snprintf(out, n, "%lldh %02lldm", left / 60, left % 60);
}

// In-game minutes until the next walk bonus, using the same rule that grants it.
inline long long walkMinutesLeft(long long lastWalk, long long now, long long cooldownMinutes)
{
    long long copy = lastWalk, left = 0;
    cooldowns::tryWalkBonus(copy, now, cooldownMinutes, &left);
    return left;
}

inline long long secondsLeft(long long readyAt, long long now) { return readyAt > now ? readyAt - now : 0; }

}  // namespace phoneapp
