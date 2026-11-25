// Tests for rules/RealPhone.h: finding the phone's app table, the Chopagotchi
// row inside the Checklist app, the icon on the home screen, taking the screen
// over when either is chosen and leaving the phone alone when anything looks wrong.
//
// FakePhone below plays the game's side: it fills the table the way
// cellphone_controller does and moves the phone through its states the way
// cellphone_flashhand does (numbers from the decompiled Enhanced 1158.13 scripts).
#include "rules/RealPhone.h"
#include "rules/PhoneScreens.h"
#include "check.h"

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace realphone;

struct FakePhone : Host {
    std::vector<uint64_t> g;
    int table, rowCount, anchor;
    unsigned long long now = 1000;
    int movie = 5;
    bool movieIsLoaded = true;
    std::vector<std::string> calls, sounds, logs;
    std::string call;
    std::set<int> down;

    // Enhanced 1158.13: table at 9509 with 37 rows, sound set label at 21655.
    explicit FakePhone(int table_ = 9509, int rows_ = 37, int anchor_ = 21655)
        : g(40000, 0), table(table_), rowCount(rows_), anchor(anchor_) {}

    int slots() override { return (int)g.size(); }
    uint64_t* slot(int i) override { return i >= 0 && i < (int)g.size() ? &g[i] : nullptr; }
    unsigned long long nowMs() override { return now; }
    bool movieLoaded(int handle) override { return movieIsLoaded && handle == movie; }
    void begin(int handle, const char* method) override { call = std::string(method) + "(" + (handle == movie ? "" : "WRONG MOVIE "); }
    void addInt(int v) override { sep(); call += std::to_string(v); }
    void addText(const char* t) override { sep(); call += std::string("'") + t + "'"; }
    void end() override { calls.push_back(call + ")"); }
    bool pressed(int control) override { return down.count(control) > 0; }
    void sound(const char* name, const char* set) override { sounds.push_back(std::string(name) + "@" + set); }
    void log(const char* text) override { logs.push_back(text); }
    void sep() { if (call.back() != '(') call += ","; }

    // Questions to the movie, and the game's scripts.
    int selection = 0;        // the highlighted row of the list on screen
    bool answers = true;      // false: the answer is not in yet
    int nextTicket = 7;
    std::set<int> tickets;    // asked and not picked up yet
    std::map<std::string, int> threads;
    std::vector<std::string> ended, requested, released;
    static std::string lower(const char* s)
    {
        std::string r(s);
        for (auto& c : r) c = (char)tolower(c);
        return r;
    }
    int endAsking() override
    {
        calls.push_back(call + ")");
        tickets.insert(nextTicket);
        return nextTicket++;
    }
    bool answerReady(int t) override { return answers && tickets.count(t) > 0; }
    int answerInt(int t) override
    {
        tickets.erase(t);
        return selection;
    }
    int scriptThreads(const char* s) override { return threads[lower(s)]; }
    void endScript(const char* s) override
    {
        ended.push_back(s);
        threads[lower(s)] = 0;
    }
    void requestScript(const char* s) override { requested.push_back(s); }
    void releaseScript(const char* s) override { released.push_back(s); }

    int32_t at(int i) { return (int32_t)(uint32_t)g[i]; }
    void put(int i, int32_t v) { g[i] = (uint64_t)(uint32_t)v; }
    void label(int i, const char* text)
    {
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "%s", text);
        memcpy(&g[i], buf, 32);
    }
    int rowAt(int r, int field) { return table + 1 + r * 15 + field; }
    int mapBase() { return table + 1 + rowCount * 15; }
    int usedBase() { return mapBase() + 1 + rowCount; }
    int pageSlot() { return usedBase() + 1 + rowCount; }
    int flags() { return table - 7; }
    int state() { return anchor + 12; }

    void app(int r, const char* name, int cell, const char* script, int icon, int page = 1)
    {
        label(rowAt(r, 0), name);
        put(rowAt(r, 4), cell);
        label(rowAt(r, 5), script);
        put(rowAt(r, 9), (int32_t)joaat(script));
        put(rowAt(r, 10), icon);
        put(rowAt(r, 11), page);
        put(rowAt(r, 12), 1);
    }
    // cellphone_controller: hide every row, then register the story-mode apps.
    void buildTable(bool trackify = false)
    {
        put(table, rowCount);
        put(mapBase(), rowCount);
        put(usedBase(), rowCount);
        put(pageSlot(), 1);
        for (int r = 0; r < rowCount; ++r) put(rowAt(r, 4), -99);
        app(7, "CELL_5", 0, "appEmail", 4);
        app(1, "CELL_1", 1, "appTextMessage", 2);
        app(4, "CELL_23", 2, "appChecklist", 39);
        app(20, "CELL_32", 3, "appSettings", 43);
        app(0, "CELL_0", 4, "appContacts", 5);
        app(10, "CELL_16", 5, "appSettings", 24);
        app(3, "CELL_7", 6, "appCamera", 1);
        app(2, "CELL_2", 7, "appInternet", 6);
        if (trackify) app(17, "CELL_28", 8, "appTrackify", 42);
        app(9, "CELL_13", -99, "appContacts", 13, 2);
    }
    // cellphone_flashhand drawing the home screen from the table.
    void drawHome()
    {
        for (int c = 0; c < 9; ++c) put(usedBase() + 1 + c, 0);
        for (int c = 0; c < 9; ++c)
            for (int r = 0; r < rowCount; ++r)
                if (at(rowAt(r, 11)) == 1 && at(rowAt(r, 4)) == c && !at(usedBase() + 1 + c)) {
                    put(mapBase() + 1 + c, r);
                    put(usedBase() + 1 + c, 1);
                }
    }
    void raisePhone(const char* who = "Franklin")
    {
        label(anchor, (std::string("Phone_SoundSet_") + who).c_str());
        put(anchor + 11, 1);     // owner
        put(anchor - 8, movie);  // the phone's movie
        put(anchor - 9, 4);      // Contacts highlighted
        put(state(), 4);
        drawHome();
    }
    void homeScreen() { put(state(), 6); }
    void putAway() { put(state(), 3); }
    // Select pressed on a home-screen cell.
    void pressCell(int cell)
    {
        put(anchor - 9, cell);
        put(anchor + 14, at(mapBase() + 1 + cell));
        put(state(), 7);
        put(flags(), at(flags()) | (1 << 23));
    }
    // The phone starting the Checklist script once it has loaded, which then
    // draws its two rows (cellphone_flashhand, appchecklist).
    void checklistStarts()
    {
        put(flags(), at(flags()) & ~(1 << 23));
        threads["appchecklist"] = 1;
    }
    // Back pressed on an app's first screen: the phone returns to the home screen by itself.
    void backToHome()
    {
        put(state(), 6);
        drawHome();
    }
    bool called(const std::string& c) const
    {
        for (auto& x : calls)
            if (x == c) return true;
        return false;
    }
    bool logged(const char* part) const
    {
        for (auto& x : logs)
            if (x.find(part) != std::string::npos) return true;
        return false;
    }
};

static const Config kCfg;

// Brings the phone up to its home screen with our icon on it.
static void toHome(FakePhone& f, Phone& p)
{
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, kCfg);
    f.homeScreen();
    p.frame(f, true, kCfg);
    f.calls.clear();
}

static Screen homeList(int selected = 0)
{
    Screen s;
    snprintf(s.header, sizeof(s.header), "Chopagotchi");
    s.add("Overview");
    s.add("Cooldowns");
    s.add("Collar");
    s.selected = selected;
    return s;
}

static void testHash()
{
    CHECK(joaat("a_c_chop") == 0x14EC17EAu);
    CHECK(joaat("appContacts") == joaat("APPCONTACTS"));
}

static void testFindsTableOnEnhanced()
{
    FakePhone f;
    f.buildTable();
    Layout L;
    CHECK(findTable(f, &L));
    // The indices the decompiled Enhanced 1158.13 scripts use.
    CHECK_EQ(L.table, 9509);
    CHECK_EQ(L.rows, 37);
    CHECK_EQ(L.map, 10065);
    CHECK_EQ(L.used, 10103);
    CHECK_EQ(L.page, 10141);
    CHECK_EQ(L.flags, 9502);
    f.raisePhone();
    L.anchor = findAnchor(f, L);
    CHECK_EQ(L.anchor, 21655);
    CHECK_EQ(L.handle(), 21647);
    CHECK_EQ(L.highlight(), 21646);
    CHECK_EQ(L.owner(), 21666);
    CHECK_EQ(L.state(), 21667);
    CHECK_EQ(L.pressedRow(), 21669);
}

static void testFindsTableOnOtherBuilds()
{
    // Legacy 3889: 36 rows, and the arrays after the table are one shorter.
    FakePhone f(9509, 36, 21638);
    f.buildTable();
    Layout L;
    CHECK(findTable(f, &L));
    CHECK_EQ(L.map, 10050);
    CHECK_EQ(L.used, 10087);
    CHECK_EQ(L.page, 10124);
    // A future build that moved everything.
    FakePhone g(9777, 38, 22001);
    g.buildTable();
    CHECK(findTable(g, &L));
    CHECK_EQ(L.table, 9777);
    CHECK_EQ(L.rows, 38);
    g.raisePhone();
    CHECK_EQ(findAnchor(g, L), 22001);
}

static void testRejectsLookalikes()
{
    Layout L;
    FakePhone empty;
    CHECK(!findTable(empty, &L));
    {   // script name without its hash
        FakePhone f;
        f.buildTable();
        f.put(f.rowAt(0, 9), 1234);
        CHECK(!findTable(f, &L));
    }
    {   // another script in row 1
        FakePhone f;
        f.buildTable();
        f.app(1, "CELL_1", 1, "appSomething", 2);
        CHECK(!findTable(f, &L));
    }
    {   // the arrays after the table are not the right length
        FakePhone f;
        f.buildTable();
        f.put(f.usedBase(), 9);
        CHECK(!findTable(f, &L));
    }
    {   // two sound set labels: do not guess
        FakePhone f;
        f.buildTable();
        f.raisePhone();
        f.label(f.anchor + 200, "Phone_SoundSet_Michael");
        CHECK(findTable(f, &L));
        CHECK_EQ(findAnchor(f, L), -1);
    }
}

static void testNothingWrittenBeforeThePhoneIsKnown()
{
    FakePhone f;
    Phone p;
    CHECK_EQ(p.frame(f, true, kCfg), None);  // no table yet
    CHECK(f.logged("not there yet"));
    f.buildTable();
    std::vector<uint64_t> before = f.g;
    f.now += 5000;
    CHECK_EQ(p.frame(f, true, kCfg), None);  // table found, phone never taken out
    CHECK(f.g == before);
    CHECK(!p.onPhone());
    CHECK(f.calls.empty());
}

static void testScanIsThrottled()
{
    FakePhone f;
    Phone p;
    p.frame(f, true, kCfg);
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, kCfg);  // same moment: no second scan yet
    CHECK(!p.onPhone());
    f.now += 2500;
    p.frame(f, true, kCfg);
    CHECK(p.onPhone());
}

static void testIconAppears()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, kCfg);
    CHECK(p.onPhone());
    CHECK_EQ(p.row(), 19);
    CHECK_EQ(p.cell(), 8);  // the one free cell in story mode
    CHECK_EQ(f.at(f.rowAt(19, 4)), 8);
    CHECK_EQ(f.at(f.rowAt(19, 10)), 14);
    CHECK_EQ(f.at(f.rowAt(19, 11)), 1);
    CHECK_EQ((uint32_t)f.at(f.rowAt(19, 9)), joaat("chopagotchi_app"));
    CHECK(std::string((const char*)&f.g[f.rowAt(19, 5)]) == "chopagotchi_app");
    CHECK(f.calls.empty());  // still rising: nothing drawn yet
    f.homeScreen();
    p.frame(f, true, kCfg);
    CHECK(f.called("SET_DATA_SLOT(1,8,14,0,'Chopagotchi',255)"));
    CHECK(f.called("DISPLAY_VIEW(1,4)"));  // the highlight stays on Contacts
    CHECK_EQ(f.at(f.mapBase() + 1 + 8), 19);
    CHECK_EQ(f.at(f.usedBase() + 1 + 8), 1);
    // Nothing more is sent while the home screen just sits there.
    f.calls.clear();
    p.frame(f, true, kCfg);
    p.frame(f, true, kCfg);
    CHECK(f.calls.empty());
    // The game's own rows were not touched.
    CHECK_EQ(f.at(f.rowAt(4, 4)), 2);
    CHECK_EQ(f.at(f.rowAt(10, 4)), 5);
}

static void testFullHomeScreenTakesChecklist()
{
    FakePhone f;
    Phone p;
    f.buildTable(true);  // Trackify on the ninth cell
    f.raisePhone();
    p.frame(f, true, kCfg);
    CHECK(p.onPhone());
    CHECK_EQ(p.cell(), 2);
    CHECK_EQ(f.at(f.rowAt(4, 4)), -99);   // Checklist hidden
    CHECK_EQ(f.at(f.rowAt(17, 4)), 8);    // Trackify stays
    CHECK(f.logged("Checklist"));
    // The game rebuilds its table: Checklist is back, and gives way again.
    f.buildTable(true);
    p.frame(f, true, kCfg);
    CHECK_EQ(p.cell(), 2);
    CHECK_EQ(f.at(f.rowAt(4, 4)), -99);
    CHECK_EQ(f.at(f.rowAt(19, 4)), 2);
}

static void testChosenCell()
{
    FakePhone f;
    Phone p;
    Config cfg;
    cfg.cell = 5;
    cfg.icon = 40;
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, cfg);
    CHECK_EQ(p.cell(), 5);
    CHECK_EQ(f.at(f.rowAt(10, 4)), -99);  // Settings gave way
    CHECK_EQ(f.at(f.rowAt(19, 10)), 40);
}

static void testRowIsRestoredAfterTheGameWipesIt()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.putAway();
    p.frame(f, true, kCfg);
    f.buildTable();  // loading or switching character
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);
    p.frame(f, true, kCfg);
    CHECK_EQ(f.at(f.rowAt(19, 4)), 8);
    // Phone comes out again: the game draws our icon itself, we put the name on it.
    f.raisePhone();
    p.frame(f, true, kCfg);
    f.homeScreen();
    p.frame(f, true, kCfg);
    CHECK(f.called("SET_DATA_SLOT(1,8,14,0,'Chopagotchi',255)"));
}

static void testOpenAndDraw()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.pressCell(4);  // Contacts: not ours
    f.put(f.flags(), 0);
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(!p.open());
    f.backToHome();
    p.frame(f, true, kCfg);

    f.pressCell(8);
    CHECK(f.at(f.flags()) & (1 << 23));
    CHECK_EQ(p.frame(f, true, kCfg), Opened);
    CHECK(p.open());
    CHECK_EQ(f.at(f.flags()) & (1 << 23), 0);  // or the phone would wait for our script for ever
    f.calls.clear();
    p.show(f, homeList());
    std::vector<std::string> want = {
        "SET_DATA_SLOT_EMPTY(18)",
        "SET_DATA_SLOT(18,0,0,'Overview')",
        "SET_DATA_SLOT(18,1,0,'Cooldowns')",
        "SET_DATA_SLOT(18,2,0,'Collar')",
        "DISPLAY_VIEW(18,0)",
        "SET_HEADER('Chopagotchi')",
        "SET_SOFT_KEYS(2,1,2)",
        "SET_SOFT_KEYS(3,1,4)",
        "SET_SOFT_KEYS(1,0,1)",
    };
    CHECK(f.calls == want);
    // Same screen again: nothing is sent.
    f.calls.clear();
    p.show(f, homeList());
    CHECK(f.calls.empty());
    // Moving the highlight only re-shows the view.
    p.show(f, homeList(2));
    CHECK(f.calls == std::vector<std::string>{"DISPLAY_VIEW(18,2)"});
    // One row's text changing sends that row only.
    f.calls.clear();
    Screen s = homeList(2);
    snprintf(s.rows[1].text, sizeof(s.rows[1].text), "Cooldowns (2)");
    p.show(f, s);
    CHECK((f.calls == std::vector<std::string>{"SET_DATA_SLOT(18,1,0,'Cooldowns (2)')", "DISPLAY_VIEW(18,2)"}));
    CHECK_EQ(f.at(f.state()), 7);
}

static void testInput()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.down = {176};
    CHECK_EQ(p.frame(f, true, kCfg), None);  // keys mean nothing to us on the home screen
    f.down.clear();
    f.pressCell(8);
    p.frame(f, true, kCfg);
    p.show(f, homeList());
    f.down = {173};
    CHECK_EQ(p.frame(f, true, kCfg), Down);
    f.down = {172};
    CHECK_EQ(p.frame(f, true, kCfg), Up);
    f.down = {180};
    CHECK_EQ(p.frame(f, true, kCfg), Down);  // mouse wheel
    f.down = {176};
    CHECK_EQ(p.frame(f, true, kCfg), Select);
    CHECK(f.sounds.back() == "Menu_Accept@Phone_SoundSet_Franklin");
    // Back on the first screen is the phone's to handle.
    f.down = {177};
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(p.open());
    f.backToHome();
    f.down.clear();
    f.calls.clear();
    CHECK_EQ(p.frame(f, true, kCfg), Closed);
    CHECK(!p.open());
    // Back on the home screen the name is put on the icon again.
    CHECK(f.called("SET_DATA_SLOT(1,8,14,0,'Chopagotchi',255)"));
    CHECK(f.called("DISPLAY_VIEW(1,8)"));
}

static void testPagesBelowTheFirstScreen()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.pressCell(8);
    p.frame(f, true, kCfg);
    Screen page;
    snprintf(page.header, sizeof(page.header), "Overview");
    page.add("Mood: Happy (72/100)");
    page.nested = true;
    page.canSelect = false;
    f.calls.clear();
    p.show(f, page);
    CHECK_EQ(f.at(f.state()), 8);  // the phone now leaves Back to the app
    CHECK(f.called("SET_SOFT_KEYS(2,0,2)"));
    CHECK(f.called("SET_HEADER('Overview')"));
    f.down = {177};
    CHECK_EQ(p.frame(f, true, kCfg), Back);
    CHECK(f.sounds.back() == "Menu_Back@Phone_SoundSet_Franklin");
    // The first screen is drawn at once, but the phone is only told a frame
    // later, so it cannot read the same press as "leave the app".
    f.calls.clear();
    p.show(f, homeList());
    CHECK(f.called("SET_HEADER('Chopagotchi')"));
    CHECK_EQ(f.at(f.state()), 8);
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK_EQ(f.at(f.state()), 7);
    f.down.clear();
    p.show(f, homeList());
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(p.open());
}

static void testClosesWhenThePhoneGoesAway()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.pressCell(8);
    p.frame(f, true, kCfg);
    f.put(f.state(), 9);  // a call comes in
    CHECK_EQ(p.frame(f, true, kCfg), Closed);
    f.putAway();
    CHECK_EQ(p.frame(f, true, kCfg), None);
    // Opening again starts with a full redraw.
    f.raisePhone();
    p.frame(f, true, kCfg);
    f.homeScreen();
    p.frame(f, true, kCfg);
    f.pressCell(8);
    CHECK_EQ(p.frame(f, true, kCfg), Opened);
    f.calls.clear();
    p.show(f, homeList());
    CHECK(f.called("SET_DATA_SLOT_EMPTY(18)"));
}

static void testRetiresForOtherCharacters()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    CHECK_EQ(p.frame(f, false, kCfg), None);  // switched to Michael with the phone out
    CHECK(!p.onPhone());
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);
    CHECK(f.called("SET_DATA_SLOT(1,8)"));  // the cell is blanked
    CHECK_EQ(f.at(f.usedBase() + 1 + 8), 0);
    f.calls.clear();
    p.frame(f, false, kCfg);
    CHECK(f.calls.empty());
    // Back to Franklin: the icon returns.
    p.frame(f, true, kCfg);
    CHECK(p.onPhone());
    CHECK(f.called("SET_DATA_SLOT(1,8,14,0,'Chopagotchi',255)"));
}

static void testRetireWhileAppIsOpen()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.pressCell(8);
    p.frame(f, true, kCfg);
    CHECK_EQ(p.frame(f, false, kCfg), Closed);
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);
}

static void testStopsWhenThePhoneStateMakesNoSense()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.put(f.state(), 77);
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(p.broken());
    CHECK(!p.onPhone());
    CHECK(f.logged("switched off"));
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);  // our row is hidden again
    std::vector<uint64_t> before = f.g;
    f.put(f.state(), 6);
    before[f.state()] = 6;
    f.calls.clear();
    p.frame(f, true, kCfg);
    CHECK(f.g == before);  // and nothing is touched after that
    CHECK(f.calls.empty());
}

static void testStopsWhenTheLoadingFlagIsElsewhere()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    f.pressCell(8);
    f.put(f.flags(), 0);  // the game raised some other flag, not the one we know
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(!p.open());
    CHECK(p.broken());
    CHECK_EQ(f.at(f.state()), 6);          // back on the home screen
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);   // icon gone from the table
    CHECK(f.called("SET_DATA_SLOT(1,8)")); // and from the screen
    CHECK(f.logged("loading flag"));
}

static void testNoSpareRow()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    for (int r : {19, 18, 22, 12, 6}) f.app(r, "CELL_15", -99, "appSomething", 17);
    f.raisePhone();
    p.frame(f, true, kCfg);
    CHECK(p.broken());
    CHECK(f.logged("no spare row"));
}

static void testStartsOverAfterLoadingASave()
{
    FakePhone f;
    Phone p;
    toHome(f, p);
    std::fill(f.g.begin(), f.g.end(), 0);  // globals reset
    f.now += 100;
    CHECK_EQ(p.frame(f, true, kCfg), None);
    CHECK(!p.onPhone());
    CHECK(!p.broken());
    f.buildTable();
    f.raisePhone();
    f.now += 3000;
    p.frame(f, true, kCfg);
    CHECK(p.onPhone());
}

static void testWaitsForTheMovie()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    f.raisePhone();
    f.movieIsLoaded = false;
    p.frame(f, true, kCfg);
    f.homeScreen();
    p.frame(f, true, kCfg);
    CHECK(f.calls.empty());
    f.movieIsLoaded = true;
    p.frame(f, true, kCfg);
    CHECK(f.called("SET_DATA_SLOT(1,8,14,0,'Chopagotchi',255)"));
}

// ---- inside the Checklist app --------------------------------------------------------
static Config listCfg()
{
    Config c;
    c.inChecklist = true;
    return c;
}
static const Config kList = listCfg();
static const std::vector<std::string> kRowAdded = {"SET_DATA_SLOT(18,2,0,'Chopagotchi')", "DISPLAY_VIEW(18,0)"};

// Phone out, Checklist pressed on the home screen, its script started and our
// row added to its menu.
static void toChecklist(FakePhone& f, Phone& p)
{
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, kList);
    f.homeScreen();
    p.frame(f, true, kList);
    f.pressCell(2);
    p.frame(f, true, kList);
    f.checklistStarts();
    for (int i = 0; i < 4; ++i) p.frame(f, true, kList);
    f.calls.clear();
}
// Select on our row, and the answer coming in.
static Event chooseOurRow(FakePhone& f, Phone& p)
{
    f.selection = 2;
    f.down = {176};
    p.frame(f, true, kList);
    f.down.clear();
    return p.frame(f, true, kList);
}

static void testRowIsAddedToChecklist()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    f.raisePhone();
    std::vector<uint64_t> before = f.g;
    p.frame(f, true, kList);
    CHECK(p.onPhone());
    f.homeScreen();
    before[f.state()] = 6;
    p.frame(f, true, kList);
    p.frame(f, true, kList);
    CHECK(f.g == before);      // no icon, no row in the table: nothing is written
    CHECK(f.calls.empty());
    CHECK_EQ(p.row(), -1);

    f.pressCell(2);            // Checklist
    p.frame(f, true, kList);
    p.frame(f, true, kList);
    CHECK(f.calls.empty());    // its script is still loading
    f.checklistStarts();
    p.frame(f, true, kList);
    CHECK(f.calls.empty());    // the script draws its two rows around now
    p.frame(f, true, kList);
    CHECK(f.calls == kRowAdded);
    p.frame(f, true, kList);   // and once more, in case the script drew after us
    CHECK_EQ((int)f.calls.size(), 4);
    f.calls.clear();
    for (int i = 0; i < 10; ++i) p.frame(f, true, kList);
    CHECK(f.calls.empty());    // then the list is left alone
    CHECK(f.logged("added to the Checklist"));
    CHECK(!p.open());
    CHECK(f.ended.empty());
}

static void testOtherAppsAreLeftAlone()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    f.raisePhone();
    p.frame(f, true, kList);
    f.homeScreen();
    p.frame(f, true, kList);
    f.pressCell(4);            // Contacts
    f.put(f.flags(), 0);
    f.threads["appcontacts"] = 1;
    f.down = {176};
    for (int i = 0; i < 6; ++i) CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(f.calls.empty());
    CHECK(f.ended.empty());
}

static void testChoosingOurRowOpensTheApp()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    f.selection = 2;
    f.answers = false;
    f.down = {176};
    CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(f.calls == std::vector<std::string>{"GET_CURRENT_SELECTION()"});
    f.down.clear();
    CHECK_EQ(p.frame(f, true, kList), None);  // no answer yet
    CHECK(f.ended.empty());
    f.put(f.state(), 8);                      // meanwhile the script opened "Progress" for it
    f.answers = true;
    CHECK_EQ(p.frame(f, true, kList), Opened);
    CHECK(p.open());
    CHECK(f.ended == std::vector<std::string>{"appChecklist"});
    CHECK(f.tickets.empty());
    CHECK_EQ(f.at(f.state()), 8);
    f.calls.clear();
    p.show(f, homeList());
    std::vector<std::string> want = {
        "SET_DATA_SLOT_EMPTY(18)",
        "SET_DATA_SLOT(18,0,0,'Overview')",
        "SET_DATA_SLOT(18,1,0,'Cooldowns')",
        "SET_DATA_SLOT(18,2,0,'Collar')",
        "DISPLAY_VIEW(18,0)",
        "SET_HEADER('Chopagotchi')",
        "SET_SOFT_KEYS(2,1,2)",
        "SET_SOFT_KEYS(3,1,4)",
        "SET_SOFT_KEYS(1,0,1)",
    };
    CHECK(f.calls == want);
    CHECK_EQ(f.at(f.state()), 8);  // our first screen is a page of Checklist: Back is ours
    // The answer can also be there before the script has moved.
    FakePhone g;
    Phone q;
    toChecklist(g, q);
    CHECK_EQ(chooseOurRow(g, q), Opened);
    CHECK_EQ(g.at(g.state()), 8);
    CHECK_EQ(g.at(g.rowAt(19, 4)), -99);  // still no row of ours in the app table
}

static void testChecklistKeepsItsOwnRows()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    f.selection = 1;           // Progress
    f.down = {176};
    p.frame(f, true, kList);
    f.down.clear();
    CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(f.ended.empty());
    CHECK(f.tickets.empty());
    f.put(f.state(), 8);       // the script shows its Progress page
    f.calls.clear();
    f.down = {176};
    for (int i = 0; i < 5; ++i) CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(f.calls.empty());    // nothing of ours happens on Checklist's pages
    f.down.clear();
    f.put(f.state(), 7);       // Back: the script draws its two rows again
    p.frame(f, true, kList);
    p.frame(f, true, kList);
    CHECK(f.calls == kRowAdded);
}

static void testInputInsideChecklist()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    f.down = {173};
    CHECK_EQ(p.frame(f, true, kList), None);  // moving through Checklist's menu is the phone's business
    CHECK(f.calls.empty());
    CHECK_EQ(chooseOurRow(f, p), Opened);
    p.show(f, homeList());
    f.down = {173};
    CHECK_EQ(p.frame(f, true, kList), Down);
    f.down = {172};
    CHECK_EQ(p.frame(f, true, kList), Up);
    f.down = {181};
    CHECK_EQ(p.frame(f, true, kList), Up);    // mouse wheel
    f.down = {176};
    CHECK_EQ(p.frame(f, true, kList), Select);
    CHECK(f.sounds.back() == "Menu_Accept@Phone_SoundSet_Franklin");
    // A page below our first screen: Back comes to us and the phone stays put.
    Screen page;
    snprintf(page.header, sizeof(page.header), "Overview");
    page.add("Mood: Happy (72/100)");
    page.nested = true;
    page.canSelect = false;
    p.show(f, page);
    f.down = {177};
    CHECK_EQ(p.frame(f, true, kList), Back);
    CHECK(p.open());
    CHECK_EQ(f.at(f.state()), 8);
    CHECK(f.requested.empty());
}

static void testBackReturnsToChecklist()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    CHECK_EQ(chooseOurRow(f, p), Opened);
    p.show(f, homeList());
    f.down = {177};
    CHECK_EQ(p.frame(f, true, kList), Closed);
    CHECK(!p.open());
    CHECK(f.sounds.back() == "Menu_Back@Phone_SoundSet_Franklin");
    // The phone is where it is right after Checklist's icon was pressed.
    CHECK_EQ(f.at(f.state()), 7);
    CHECK(f.at(f.flags()) & (1 << 23));
    CHECK(f.requested == std::vector<std::string>{"appChecklist"});
    f.down.clear();
    f.calls.clear();
    p.frame(f, true, kList);
    CHECK(f.released.empty());   // still loading
    f.checklistStarts();
    p.frame(f, true, kList);
    CHECK(f.released == std::vector<std::string>{"appChecklist"});
    p.frame(f, true, kList);
    // Our row is added again, and this time it is the highlighted one.
    std::vector<std::string> want = {"SET_DATA_SLOT(18,2,0,'Chopagotchi')", "DISPLAY_VIEW(18,2)"};
    CHECK(f.calls == want);
    p.frame(f, true, kList);
    p.frame(f, true, kList);
    // And it opens again from there.
    CHECK_EQ(chooseOurRow(f, p), Opened);
    f.calls.clear();
    p.show(f, homeList());
    CHECK(f.called("SET_DATA_SLOT_EMPTY(18)"));
    // The highlight is only moved to our row on the way back from our app.
    f.put(f.state(), 6);
    CHECK_EQ(p.frame(f, true, kList), Closed);
    f.pressCell(2);
    p.frame(f, true, kList);
    f.checklistStarts();
    f.calls.clear();
    p.frame(f, true, kList);
    p.frame(f, true, kList);
    CHECK(f.calls == kRowAdded);
}

static void testChecklistDoesNotStartAgain()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    CHECK_EQ(chooseOurRow(f, p), Opened);
    p.show(f, homeList());
    f.down = {177};
    CHECK_EQ(p.frame(f, true, kList), Closed);
    f.down.clear();
    f.now += 1000;
    p.frame(f, true, kList);
    CHECK(f.at(f.flags()) & (1 << 23));
    f.now += 2500;
    p.frame(f, true, kList);
    CHECK_EQ(f.at(f.flags()) & (1 << 23), 0);  // or Back would do nothing for ever
    CHECK(f.released == std::vector<std::string>{"appChecklist"});
    CHECK(f.logged("did not start again"));
    CHECK_EQ(f.at(f.state()), 7);
    CHECK(!p.broken());
}

static void testPhoneMovesOnWhileOurPageIsUp()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    CHECK_EQ(chooseOurRow(f, p), Opened);
    f.put(f.state(), 9);  // a call comes in
    CHECK_EQ(p.frame(f, true, kList), Closed);
    CHECK_EQ(f.at(f.state()), 9);
    CHECK(f.requested.empty());
    // Switching character with our page up: the phone must not be left on a
    // page nobody answers Back on.
    FakePhone g;
    Phone q;
    toChecklist(g, q);
    CHECK_EQ(chooseOurRow(g, q), Opened);
    CHECK_EQ(q.frame(g, false, kList), Closed);
    CHECK_EQ(g.at(g.state()), 7);
    CHECK(!q.onPhone());
}

static void testAnswerThatNeverComes()
{
    FakePhone f;
    Phone p;
    toChecklist(f, p);
    f.selection = 2;
    f.answers = false;
    f.down = {176};
    p.frame(f, true, kList);
    f.down.clear();
    f.now += 2000;
    CHECK_EQ(p.frame(f, true, kList), None);
    f.answers = true;          // too late: Checklist has long moved on
    CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(!p.open());
    CHECK(f.ended.empty());
    CHECK(f.tickets.empty());  // but the answer was still picked up
    // The phone going away while we wait drops the question too.
    FakePhone g;
    Phone q;
    toChecklist(g, q);
    g.selection = 2;
    g.answers = false;
    g.down = {176};
    q.frame(g, true, kList);
    g.down.clear();
    g.putAway();
    q.frame(g, true, kList);
    g.answers = true;
    CHECK_EQ(q.frame(g, true, kList), None);
    CHECK(!q.open());
    CHECK(g.ended.empty());
}

static void testChecklistRowNeedsTheChecklistApp()
{
    FakePhone f;
    Phone p;
    f.buildTable();
    f.app(4, "CELL_23", 2, "appSomethingElse", 39);  // a build where row 4 is another app
    f.raisePhone();
    p.frame(f, true, kList);
    f.homeScreen();
    p.frame(f, true, kList);
    f.pressCell(2);
    f.checklistStarts();
    f.down = {176};
    for (int i = 0; i < 6; ++i) CHECK_EQ(p.frame(f, true, kList), None);
    CHECK(f.calls.empty());
    CHECK(f.ended.empty());
}

static void testSwitchingFromIconToChecklist()
{
    FakePhone f;
    Phone p;
    toHome(f, p);              // with the icon
    CHECK_EQ(f.at(f.rowAt(19, 4)), 8);
    p.frame(f, true, kList);
    CHECK_EQ(f.at(f.rowAt(19, 4)), -99);    // the icon is taken away
    CHECK(f.called("SET_DATA_SLOT(1,8)"));
    CHECK(p.onPhone());
}

// ---- the pages as rows (rules/PhoneScreens.h) ---------------------------------------
static const char* const kNames[4] = {"Sit", "Paw", "Beg", "Pet"};

static phonescreens::Status status()
{
    phonescreens::Status st;
    st.have = true;
    st.mood = 2;
    st.happy = 72.6f;
    st.xp = 65;
    st.needed[0] = 20; st.needed[1] = 60; st.needed[2] = 120; st.needed[3] = 200;
    st.trickNames = kNames;
    st.chop = 1;
    st.collar = 2;
    st.walkMinutesLeft = 320;
    st.secondsLeft[0] = 45;
    st.secondsLeft[1] = 185;
    st.on[0] = true;
    st.on[2] = true;
    return st;
}

static bool row(const Screen& s, int i, const char* text) { return i < s.count && strcmp(s.rows[i].text, text) == 0; }

static void testScreens()
{
    phoneapp::App app;
    app.open = true;
    Screen s;
    phonescreens::Status st = status();

    phonescreens::build(app, st, 0, s);
    CHECK(strcmp(s.header, "Chopagotchi") == 0);
    CHECK_EQ(s.count, 5);
    CHECK(row(s, 0, "Overview") && row(s, 3, "Whistle for Chop") && row(s, 4, "Settings"));
    for (int i = 0; i < s.count; ++i) CHECK_EQ(s.rows[i].icon, 0);  // plain rows, like the Checklist app
    CHECK(!s.nested);
    CHECK(s.canSelect);
    app.cursor = 3;
    phonescreens::build(app, st, 0, s);
    CHECK_EQ(s.selected, 3);

    app.page = phoneapp::Overview;
    app.cursor = 0;
    phonescreens::build(app, st, 1, s);
    CHECK(strcmp(s.header, "Overview") == 0);
    CHECK(s.nested);
    CHECK(!s.canSelect);
    CHECK_EQ(s.count, 6);
    CHECK(row(s, 0, "Mood: Happy (72/100)"));
    CHECK(row(s, 1, "Training: 65"));
    CHECK(row(s, 2, "Tricks: 2 of 4"));
    CHECK(row(s, 3, "Knows: Sit, Paw"));
    CHECK(row(s, 4, "Next: Beg at 120"));
    CHECK(row(s, 5, "Chop: with you"));
    CHECK_EQ(s.selected, 1);  // information pages scroll with their own cursor
    phonescreens::build(app, st, 99, s);
    CHECK_EQ(s.selected, 5);  // and stop at the last row
    phonescreens::build(app, st, -3, s);
    CHECK_EQ(s.selected, 0);

    st.xp = 0;
    st.chop = 0;
    st.mood = 0;
    st.happy = 5;
    phonescreens::build(app, st, 0, s);
    CHECK_EQ(s.count, 5);  // no "Knows" line before the first trick
    CHECK(row(s, 0, "Mood: Unhappy (5/100)"));
    CHECK(row(s, 3, "Next: Sit at 20"));
    CHECK(row(s, 4, "Chop: at home"));
    st.xp = 500;
    st.chop = 2;
    phonescreens::build(app, st, 0, s);
    CHECK(row(s, 3, "Knows: Sit, Paw, Beg, Pet"));
    CHECK(row(s, 4, "Next: all learned"));
    CHECK(row(s, 5, "Chop: hurt"));
    st.have = false;
    phonescreens::build(app, st, 0, s);
    CHECK_EQ(s.count, 1);
    CHECK(row(s, 0, "Looking for Chop..."));

    st = status();
    app.page = phoneapp::Timers;
    phonescreens::build(app, st, 0, s);
    CHECK(strcmp(s.header, "Cooldowns") == 0);
    CHECK_EQ(s.count, 6);
    CHECK(row(s, 0, "Walk: 5h 20m"));
    CHECK(row(s, 1, "Fetch: 45s"));
    CHECK(row(s, 2, "Sit: 3m 05s"));
    CHECK(row(s, 3, "Paw: Ready"));
    CHECK(row(s, 4, "Beg: Not learned"));
    CHECK(row(s, 5, "Petting: Ready"));

    app.page = phoneapp::Collars;
    app.cursor = 4;
    phonescreens::build(app, st, 0, s);
    CHECK_EQ(s.count, 5);
    CHECK(row(s, 0, "Collar 1") && row(s, 2, "Collar 3 (wearing)"));
    CHECK_EQ(s.selected, 4);
    CHECK(s.canSelect);

    app.page = phoneapp::Settings;
    app.cursor = 1;
    phonescreens::build(app, st, 0, s);
    CHECK_EQ(s.count, 3);
    CHECK(row(s, 0, "Chopagotchi: On") && row(s, 1, "Messages: Off") && row(s, 2, "Happiness popup: On"));
    CHECK_EQ(s.selected, 1);
}

int main()
{
    testHash();
    testFindsTableOnEnhanced();
    testFindsTableOnOtherBuilds();
    testRejectsLookalikes();
    testNothingWrittenBeforeThePhoneIsKnown();
    testScanIsThrottled();
    testIconAppears();
    testFullHomeScreenTakesChecklist();
    testChosenCell();
    testRowIsRestoredAfterTheGameWipesIt();
    testOpenAndDraw();
    testInput();
    testPagesBelowTheFirstScreen();
    testClosesWhenThePhoneGoesAway();
    testRetiresForOtherCharacters();
    testRetireWhileAppIsOpen();
    testStopsWhenThePhoneStateMakesNoSense();
    testStopsWhenTheLoadingFlagIsElsewhere();
    testNoSpareRow();
    testStartsOverAfterLoadingASave();
    testWaitsForTheMovie();
    testRowIsAddedToChecklist();
    testOtherAppsAreLeftAlone();
    testChoosingOurRowOpensTheApp();
    testChecklistKeepsItsOwnRows();
    testInputInsideChecklist();
    testBackReturnsToChecklist();
    testChecklistDoesNotStartAgain();
    testPhoneMovesOnWhileOurPageIsUp();
    testAnswerThatNeverComes();
    testChecklistRowNeedsTheChecklistApp();
    testSwitchingFromIconToChecklist();
    testScreens();
    return finish("real phone");
}
