// Chopagotchi - what the app shows on Franklin's real phone.
//
// The phone's own list screen takes rows of one line of text, so each page of
// the app (see PhoneApp.h) is turned into such rows here: plain text and no
// icons, like the lists in the phone's Checklist app. Pure text building, tested in tests/test_real_phone.cpp.
#pragma once
#include <cstdio>

#include "PhoneApp.h"
#include "RealPhone.h"

namespace phonescreens {

// Everything the pages show, gathered by Chopagotchi.cpp each frame.
struct Status {
    bool have = false;                 // Chop's data is loaded
    int mood = 0;                      // 0 unhappy, 1 content, 2 happy
    float happy = 0, xp = 0;
    float needed[4] = {0, 0, 0, 0};    // training needed for each trick
    const char* const* trickNames = nullptr;
    int chop = 0;                      // 0 at home, 1 with you, 2 hurt
    int collar = -1;                   // 0..4, -1 unknown
    bool collarLocked[phoneapp::kCollars] = {};   // not earned yet
    long long walkMinutesLeft = 0;     // in-game minutes
    long long secondsLeft[5] = {0, 0, 0, 0, 0};  // fetch, sit, paw, beg, petting
    bool on[phoneapp::kSettingItems] = {false, false, false};
};

static const char* const kTitles[] = {"Chopagotchi", "Overview", "Cooldowns", "Collar", "Settings"};
static const char* const kMoods[] = {"Unhappy", "Content", "Happy"};

// `scroll` is the highlighted row on the two pages that only show information.
inline void build(const phoneapp::App& app, const Status& st, int scroll, realphone::Screen& s)
{
    using namespace phoneapp;
    s = realphone::Screen();
    snprintf(s.header, sizeof(s.header), "%s", kTitles[app.page]);
    s.nested = app.page != Home;
    s.canSelect = rows(app.page) > 0;
    char v[48], line[64];
    Training tr = training(st.xp, st.needed);

    switch (app.page) {
    case Home:
        for (int i = 0; i < kHomeItems; ++i) s.add(kHomeLabels[i]);
        break;
    case Overview:
        if (!st.have) {
            s.add("Looking for Chop...");
            break;
        }
        snprintf(line, sizeof(line), "Mood: %s (%d/100)", kMoods[st.mood < 0 ? 0 : (st.mood > 2 ? 2 : st.mood)], (int)st.happy);
        s.add(line);
        snprintf(line, sizeof(line), "Training: %d", (int)st.xp);
        s.add(line);
        snprintf(line, sizeof(line), "Tricks: %d of 4", tr.level);
        s.add(line);
        if (tr.level > 0 && st.trickNames) {
            trickList(tr.level, st.trickNames, v, sizeof(v));
            snprintf(line, sizeof(line), "Knows: %s", v);
            s.add(line);
        }
        if (tr.level < 4 && st.trickNames) snprintf(line, sizeof(line), "Next: %s at %d", st.trickNames[tr.level], (int)tr.next);
        else snprintf(line, sizeof(line), "Next: all learned");
        s.add(line);
        s.add(st.chop == 2 ? "Chop: hurt" : (st.chop == 1 ? "Chop: with you" : "Chop: at home"));
        break;
    case Timers: {
        if (st.have) formatGameMinutes(st.walkMinutesLeft, v, sizeof(v));
        else snprintf(v, sizeof(v), "-");
        snprintf(line, sizeof(line), "Walk: %s", v);
        s.add(line);
        static const char* const names[5] = {"Fetch", "Sit", "Paw", "Beg", "Petting"};
        static const int needLevel[5] = {0, 1, 2, 3, 0};
        for (int i = 0; i < 5; ++i) {
            if (tr.level < needLevel[i]) snprintf(v, sizeof(v), "Not learned");
            else formatSeconds(st.secondsLeft[i], v, sizeof(v));
            snprintf(line, sizeof(line), "%s: %s", names[i], v);
            s.add(line);
        }
        break;
    }
    case Collars:
        for (int i = 0; i < kCollars; ++i) {
            const char* form = i == st.collar ? "Collar %d (wearing)" : st.collarLocked[i] ? "Collar %d - Locked" : "Collar %d";
            snprintf(line, sizeof(line), form, i + 1);
            s.add(line);
        }
        break;
    case Settings:
        for (int i = 0; i < kSettingItems; ++i) {
            snprintf(line, sizeof(line), "%s: %s", kSettingLabels[i], st.on[i] ? "On" : "Off");
            s.add(line);
        }
        break;
    }
    int sel = s.canSelect ? app.cursor : scroll;
    if (sel >= s.count) sel = s.count - 1;
    if (sel < 0) sel = 0;
    s.selected = sel;
}

} // namespace phonescreens
