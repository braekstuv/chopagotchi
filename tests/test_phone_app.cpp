// Tests for rules/PhoneApp.h: opening and navigating the phone app, the
// actions it asks for, and the text its screens show.
#include "rules/PhoneApp.h"
#include "check.h"

#include <cstring>

using namespace phoneapp;

static bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

static void testOpenClose()
{
    App app;
    // Only the app key opens it; other keys do nothing while it is closed.
    CHECK_EQ(press(app, KeySelect, 0).type, None);
    CHECK_EQ(press(app, KeyBack, 0).type, None);
    CHECK(!app.open);
    CHECK_EQ(press(app, KeyApp, 0).type, Opened);
    CHECK(app.open);
    CHECK_EQ(app.page, Home);
    CHECK_EQ(app.cursor, 0);
    // The app key closes it from any page; Back closes it only from Home.
    press(app, KeySelect, 0);  // Overview
    CHECK_EQ(app.page, Overview);
    CHECK_EQ(press(app, KeyApp, 0).type, Closed);
    CHECK(!app.open);
    press(app, KeyApp, 0);
    CHECK_EQ(press(app, KeyBack, 0).type, Closed);
    CHECK(!app.open);
    // Reopening starts fresh on Home.
    press(app, KeyApp, 0);
    press(app, KeyDown, 0);
    press(app, KeyApp, 0);
    press(app, KeyApp, 0);
    CHECK_EQ(app.cursor, 0);
}

static void testHomeNavigation()
{
    App app;
    press(app, KeyApp, 0);
    // Up from the top wraps to the bottom, down from the bottom to the top.
    press(app, KeyUp, 0);
    CHECK_EQ(app.cursor, ItemSettings);
    press(app, KeyDown, 0);
    CHECK_EQ(app.cursor, ItemOverview);

    const Page pages[] = {Overview, Timers, Collars};
    for (int i = 0; i < 3; ++i) {
        app.cursor = i;
        CHECK_EQ(press(app, KeySelect, 2).type, None);
        CHECK_EQ(app.page, pages[i]);
        // Back returns to Home with the item that opened the page highlighted.
        CHECK_EQ(press(app, KeyBack, 2).type, None);
        CHECK(app.open);
        CHECK_EQ(app.page, Home);
        CHECK_EQ(app.cursor, i);
    }
    app.cursor = ItemSettings;
    press(app, KeySelect, 0);
    CHECK_EQ(app.page, Settings);
    CHECK_EQ(app.cursor, 0);
    press(app, KeyBack, 0);
    CHECK_EQ(app.cursor, ItemSettings);

    // Whistle is an action and stays on Home.
    app.cursor = ItemWhistle;
    CHECK_EQ(press(app, KeySelect, 0).type, Whistle);
    CHECK_EQ(app.page, Home);
    CHECK_EQ(app.cursor, ItemWhistle);

    // Up/down on pages without rows change nothing.
    app.cursor = ItemOverview;
    press(app, KeySelect, 0);
    press(app, KeyDown, 0);
    CHECK_EQ(app.cursor, 0);
    CHECK_EQ(press(app, KeySelect, 0).type, None);
}

static void testCollars()
{
    App app;
    press(app, KeyApp, 0);
    app.cursor = ItemCollar;
    // The cursor starts on the collar Chop wears.
    press(app, KeySelect, 3);
    CHECK_EQ(app.page, Collars);
    CHECK_EQ(app.cursor, 3);
    press(app, KeyDown, 3);
    Action a = press(app, KeySelect, 3);
    CHECK_EQ(a.type, SetCollar);
    CHECK_EQ(a.value, 4);
    CHECK_EQ(app.page, Collars);  // stays so you can try another
    press(app, KeyDown, 4);
    CHECK_EQ(app.cursor, 0);      // wraps
    press(app, KeyUp, 4);
    CHECK_EQ(app.cursor, 4);

    // Unknown or out-of-range collar starts at the first one.
    press(app, KeyBack, 0);
    press(app, KeySelect, -1);
    CHECK_EQ(app.cursor, 0);
    press(app, KeyBack, 0);
    press(app, KeySelect, 9);
    CHECK_EQ(app.cursor, 0);
}

static void testSettings()
{
    App app;
    press(app, KeyApp, 0);
    app.cursor = ItemSettings;
    press(app, KeySelect, 0);
    CHECK_EQ(press(app, KeySelect, 0).type, ToggleMod);
    press(app, KeyDown, 0);
    CHECK_EQ(press(app, KeySelect, 0).type, ToggleMessages);
    press(app, KeyDown, 0);
    CHECK_EQ(press(app, KeySelect, 0).type, ToggleGainPopup);
    press(app, KeyDown, 0);
    CHECK_EQ(app.cursor, SettingMod);
}

static void testTraining()
{
    const float needed[4] = {20, 60, 120, 200};
    Training t = training(0, needed);
    CHECK_EQ(t.level, 0);
    CHECK_EQ(t.next, 20);
    CHECK(t.fraction == 0.0f);
    t = training(10, needed);
    CHECK(t.fraction == 0.5f);
    t = training(20, needed);  // exactly at a threshold: trick learned
    CHECK_EQ(t.level, 1);
    CHECK_EQ(t.next, 60);
    CHECK(t.fraction == 0.0f);
    t = training(90, needed);  // halfway from Paw (60) to Beg (120)
    CHECK_EQ(t.level, 2);
    CHECK(t.fraction == 0.5f);
    t = training(500, needed);
    CHECK_EQ(t.level, 4);
    CHECK_EQ(t.next, 0);
    CHECK(t.fraction == 1.0f);
    // Two tricks at the same threshold: no divide by zero.
    const float flat[4] = {0, 0, 50, 50};
    t = training(0, flat);
    CHECK_EQ(t.level, 2);
    CHECK(t.fraction == 0.0f);
}

static void testTrickList()
{
    const char* const names[4] = {"Sit", "Paw", "Beg", "Pet"};
    char buf[64];
    trickList(0, names, buf, sizeof(buf));
    CHECK(same(buf, "None yet"));
    trickList(1, names, buf, sizeof(buf));
    CHECK(same(buf, "Sit"));
    trickList(3, names, buf, sizeof(buf));
    CHECK(same(buf, "Sit, Paw, Beg"));
    trickList(4, names, buf, sizeof(buf));
    CHECK(same(buf, "Sit, Paw, Beg, Pet"));
    char tiny[6];  // truncates safely
    trickList(4, names, tiny, sizeof(tiny));
    CHECK(std::strlen(tiny) < sizeof(tiny));
}

static void testFormatting()
{
    char buf[32];
    formatSeconds(0, buf, sizeof(buf));
    CHECK(same(buf, "Ready"));
    formatSeconds(-5, buf, sizeof(buf));
    CHECK(same(buf, "Ready"));
    formatSeconds(45, buf, sizeof(buf));
    CHECK(same(buf, "45s"));
    formatSeconds(185, buf, sizeof(buf));
    CHECK(same(buf, "3m 05s"));
    formatSeconds(2 * 3600 + 10 * 60 + 59, buf, sizeof(buf));
    CHECK(same(buf, "2h 10m"));

    formatGameMinutes(0, buf, sizeof(buf));
    CHECK(same(buf, "Ready"));
    formatGameMinutes(45, buf, sizeof(buf));
    CHECK(same(buf, "45m"));
    formatGameMinutes(5 * 60 + 7, buf, sizeof(buf));
    CHECK(same(buf, "5h 07m"));
}

static bool near(float a, float b) { return a - b < 1e-5f && b - a < 1e-5f; }

static void testHudArea()
{
    HudArea a = hudArea(16.0f / 9.0f, 1.0f);
    CHECK(near(a.left, 0) && near(a.top, 0) && near(a.width, 1) && near(a.height, 1) && near(a.squeeze, 1));
    a = hudArea(32.0f / 9.0f, 1.0f);  // 16:9 area in the middle half
    CHECK(near(a.left, 0.25f) && near(a.width, 0.5f) && near(a.squeeze, 0.5f) && near(a.height, 1));
    a = hudArea(16.0f / 9.0f, 0.9f);  // 5% margin each side
    CHECK(near(a.left, 0.05f) && near(a.top, 0.05f) && near(a.width, 0.9f) && near(a.height, 0.9f));
    a = hudArea(32.0f / 9.0f, 0.9f);
    CHECK(near(a.left, 0.275f) && near(a.width, 0.45f));
    a = hudArea(4.0f / 3.0f, 1.0f);   // narrower than 16:9: full width
    CHECK(near(a.left, 0) && near(a.width, 1) && near(a.squeeze, 1));
    // Bad values from the game fall back to 16:9 and no margin.
    a = hudArea(0, 0);
    CHECK(near(a.left, 0) && near(a.width, 1) && near(a.top, 0));
    a = hudArea(0.0f / 0.0f, 2.0f);
    CHECK(near(a.width, 1) && near(a.height, 1));
}

static void testTimers()
{
    const long long day = 24 * 60;
    CHECK_EQ(walkMinutesLeft(-1, 1000, day), 0);          // never walked
    CHECK_EQ(walkMinutesLeft(1000, 1000, day), day);      // just walked
    CHECK_EQ(walkMinutesLeft(1000, 1000 + 60, day), day - 60);
    CHECK_EQ(walkMinutesLeft(1000, 1000 + day, day), 0);  // a day later
    CHECK_EQ(walkMinutesLeft(1000, 500, day), 0);         // older save loaded
    CHECK_EQ(secondsLeft(100, 40), 60);
    CHECK_EQ(secondsLeft(100, 100), 0);
    CHECK_EQ(secondsLeft(0, 1700000000LL), 0);
}

int main()
{
    testOpenClose();
    testHomeNavigation();
    testCollars();
    testSettings();
    testTraining();
    testTrickList();
    testFormatting();
    testTimers();
    testHudArea();
    return finish("phone app");
}
