// Chopagotchi - the app on Franklin's real phone.
//
// Where the game allows it, Chopagotchi is on the phone itself: a row in the
// menu of the phone's Checklist app (or, with InChecklist=0, an icon of its own
// on the home screen) that opens the app's pages on the phone's plain list
// screen. rules/RealPhone.h finds the phone's data and does the work;
// rules/PhoneScreens.h turns the pages into rows. If the phone's data cannot be
// found or does not check out, the phone is left alone and the F9 overlay
// (mod/Overlay.h) is the way in.
#pragma once
#include "../rules/Joaat.h"
#include "../rules/PhoneApp.h"
#include "../rules/PhoneScreens.h"
#include "../rules/RealPhone.h"
#include "AppActions.h"
#include "ChopData.h"
#include "Log.h"
#include "Natives.h"
#include "Settings.h"

static realphone::Phone g_real;
static phoneapp::App g_realApp;
static int g_realScroll = 0;

// The game, as rules/RealPhone.h sees it.
struct RealHost : realphone::Host {
    RealHost() { mapPage0(); }
    int slots() override { return g_page0 ? g_page0Slots : 0; }
    uint64_t* slot(int index) override { return g_page0 && index >= 0 && index < g_page0Slots ? g_page0 + index : nullptr; }
    unsigned long long nowMs() override { return GetTickCount64(); }
    bool movieLoaded(int handle) override { return asBool(call(0x85F01B8D5B90570EULL, u(handle))); } // HAS_SCALEFORM_MOVIE_LOADED
    void begin(int handle, const char* method) override { call(0xF6E48914C7A8694EULL, u(handle), str(method)); } // BEGIN_SCALEFORM_MOVIE_METHOD
    void addInt(int v) override { call(0xC3D0841A0CC546A6ULL, u(v)); }                        // SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT
    void addText(const char* text) override
    {
        call(0x80338406F3475E55ULL, str("STRING"));                                           // BEGIN_TEXT_COMMAND_SCALEFORM_STRING
        call(0x6C188BE134E074AAULL, str(text));                                               // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
        call(0x362E2D3FE93A9959ULL);                                                          // END_TEXT_COMMAND_SCALEFORM_STRING
    }
    void end() override { call(0xC6796A8FFA375E53ULL); }                                      // END_SCALEFORM_MOVIE_METHOD
    bool pressed(int control) override
    {
        return asBool(call(0x580417101DDB492FULL, 2, u(control))) ||                          // IS_CONTROL_JUST_PRESSED
               asBool(call(0x91AEF906BCA88877ULL, 2, u(control)));                            // IS_DISABLED_CONTROL_JUST_PRESSED
    }
    void sound(const char* name, const char* set) override
    {
        if (set && set[0]) call(0x67C540AA08E4A6F5ULL, u(-1), str(name), str(set), 1);        // PLAY_SOUND_FRONTEND
    }
    void log(const char* text) override { logf("%s", text); }
    int endAsking() override { return asInt(call(0xC50AA39A577AF886ULL)); }                   // END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE
    bool answerReady(int ticket) override { return asBool(call(0x768FF8961BA904D6ULL, u(ticket))); } // IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY
    int answerInt(int ticket) override { return asInt(call(0x2DE7EFA66B906036ULL, u(ticket))); }     // GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT
    int scriptThreads(const char* script) override
    {
        return asInt(call(0x2C83A9DA6BFFC4F9ULL, u((int32_t)joaat(script))));                // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH
    }
    void endScript(const char* script) override { call(0x9DC711BC69C548DFULL, str(script)); }     // TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME
    void requestScript(const char* script) override { call(0x6EB5F71AA68F2E8EULL, str(script)); } // REQUEST_SCRIPT
    void releaseScript(const char* script) override { call(0xC90D2DCACD56184CULL, str(script)); } // SET_SCRIPT_AS_NO_LONGER_NEEDED
};

static void realPhoneKey(phoneapp::Key key)
{
    using namespace phoneapp;
    if (rows(g_realApp.page) == 0 && (key == KeyUp || key == KeyDown)) {
        g_realScroll += key == KeyUp ? -1 : 1;  // an information page: just scroll
        return;
    }
    Page before = g_realApp.page;
    Action act = press(g_realApp, key, currentCollar());
    g_realApp.open = true;  // only the phone itself closes this one
    if (g_realApp.page != before) g_realScroll = 0;
    // What the overlay shows as a note at the bottom goes above the minimap here.
    g_appNoteToFeed = true;
    phoneAct(act);
    g_appNoteToFeed = false;
}

// Called every frame, before the overlay.
static void realPhoneFrame()
{
    // Franklin's phone only: Chop is his dog.
    static const uint32_t kFranklin = joaat("player_one");
    bool want = cfgPhone && cfgInPhone && !onlineSession() && entityModel(playerPed()) == kFranklin;
    realphone::Config cfg;
    cfg.icon = cfgPhoneIcon;
    cfg.cell = cfgPhoneCell;
    cfg.inChecklist = cfgInChecklist != 0;
    RealHost host;
    switch (g_real.frame(host, want, cfg)) {
    case realphone::Opened:
        g_realApp = phoneapp::App();
        g_realApp.open = true;
        g_realScroll = 0;
        logf("In-phone app opened");
        break;
    case realphone::Closed: g_realApp.open = false; break;
    case realphone::Up: realPhoneKey(phoneapp::KeyUp); break;
    case realphone::Down: realPhoneKey(phoneapp::KeyDown); break;
    case realphone::Select: realPhoneKey(phoneapp::KeySelect); break;
    case realphone::Back: realPhoneKey(phoneapp::KeyBack); break;
    default: break;
    }
    if (!g_real.open()) return;
    realphone::Screen screen;
    phonescreens::build(g_realApp, chopStatus(), g_realScroll, screen);
    if (!screen.canSelect) g_realScroll = screen.selected;
    g_real.show(host, screen);
}
