// Chopagotchi on Franklin's real in-game phone.
//
// There are two ways in, chosen by Config::inChecklist.
//
// Inside the Checklist app (the default). The Checklist app's first screen is
// a two-row list, "Open Missions" and "Progress", drawn by the game's
// appChecklist script. A third row, "Chopagotchi", is added to that list right
// after the script draws it. The script knows nothing of a third row and would
// open "Progress" for it, so when Select is pressed the phone is asked which
// row is highlighted, the same question the script asks, and if it is ours the
// script is ended and the screen is ours. Back on our first screen has the
// phone start the Checklist app again, the way it starts any app.
//
// As an icon of its own. The phone's home screen is a table of script globals
// owned by the game's cellphone_controller script: one row per app with its
// name, cell, icon and the script to launch. A row of our own goes into a
// spare slot, so the game draws the icon itself, and the screen is taken over
// when the icon is pressed. The method is the one documented by the iFruit
// Jailbreak project.
//
// Either way the pages are drawn on the phone's own plain list screen (the one
// Checklist uses). Everything was checked against the decompiled Enhanced
// scripts (build 1158.13).
//
// The numbers of those globals change with every game update, so nothing here
// is hard-coded. The table is recognised by what the game keeps in it (row 0
// launches "appContacts", row 1 "appTextMessage", row 3 "appCamera", each
// stored next to its own hash) and the phone's state by the sound set label
// ("Phone_SoundSet_...") that sits a fixed distance before it. Every value is
// checked before anything is written; if a check fails the phone is left alone
// and the caller keeps using its own overlay.
//
// No game or Windows calls in here: the game is reached through Host, so the
// whole thing runs against a fake phone in tests/test_real_phone.cpp.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace realphone {

struct Host {
    virtual ~Host() {}
    virtual int slots() = 0;                                // script-global slots that may be read
    virtual uint64_t* slot(int index) = 0;                  // nullptr when out of reach
    virtual unsigned long long nowMs() = 0;
    virtual bool movieLoaded(int handle) = 0;               // HAS_SCALEFORM_MOVIE_LOADED
    virtual void begin(int handle, const char* method) = 0; // BEGIN_SCALEFORM_MOVIE_METHOD
    virtual void addInt(int v) = 0;
    virtual void addText(const char* literal) = 0;
    virtual void end() = 0;
    virtual bool pressed(int control) = 0;                  // just pressed, also when the phone has it disabled
    virtual void sound(const char* name, const char* set) = 0;
    virtual void log(const char* text) = 0;
    // A movie method that answers: begin() as usual, then endAsking() instead of
    // end(). The answer arrives some frames later.
    virtual int endAsking() = 0;                            // END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE
    virtual bool answerReady(int ticket) = 0;
    virtual int answerInt(int ticket) = 0;                  // also hands the ticket back to the game
    // The game's own scripts, by name.
    virtual int scriptThreads(const char* script) = 0;      // how many are running
    virtual void endScript(const char* script) = 0;
    virtual void requestScript(const char* script) = 0;     // have it loaded, so the phone can start it
    virtual void releaseScript(const char* script) = 0;
};

inline uint32_t joaat(const char* s)
{
    uint32_t h = 0;
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c += 32;
        h += (uint8_t)c;
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

// ---- the game's layout -------------------------------------------------------
static const int kRowSize = 15;       // slots per app row
static const int kOffName = 0;        // text label (16 bytes): the app name's text key
static const int kOffCell = 4;        // home-screen cell 0..8, -99 = not shown
static const int kOffScript = 5;      // text label (16 bytes): script to launch
static const int kOffScriptHash = 9;
static const int kOffIcon = 10;
static const int kOffPage = 11;       // home-screen page, 1 or 2
static const int kCells = 9;
static const int kHidden = -99;
static const int kFlagsBeforeTable = 7;    // the phone's flag word sits 7 slots before the table
static const int kLoadingBit = 23;         // "the pressed app's script is still loading"
static const int kAnchorSearchFrom = 10000, kAnchorSearchTo = 15000; // slots after the table

// Phone states (cellphone_flashhand).
static const int kStateAway = 3, kStateHome = 6, kStateApp = 7, kStateAppNested = 8, kStateMax = 10;
// Screens of the phone's movie.
static const int kViewHome = 1, kViewList = 18; // 18: the plain text list the Checklist app uses
// The phone's own controls.
static const int kCtlUp = 172, kCtlDown = 173, kCtlSelect = 176, kCtlBack = 177, kCtlWheelDown = 180, kCtlWheelUp = 181;

// Spare rows: no game script ever registers an app in these. (Row 8 is spare
// too, but the phone ignores a press on it.)
static const int kSpareRows[] = {19, 18, 22, 12, 6};
// The Checklist app's row: the app our list row lives in, and the one icon that
// makes room for ours when the home screen is full.
static const int kChecklistRow = 4;
static const char* const kChecklistScript = "appChecklist";
// The Checklist app's first screen has rows 0 and 1; ours comes after them.
static const int kOurListRow = 2;
// The Checklist script draws its screen within a frame of starting; our row is
// sent on the two frames after we first see it, so it lands after that.
static const int kAddRowFrom = 2, kAddRowTo = 3;
static const unsigned long long kAnswerMs = 1500;   // how long to wait for "which row is highlighted"
static const unsigned long long kRestartMs = 3000;  // how long the phone gets to start Checklist again
// What our row says it launches. Nothing by this name exists, so nothing
// starts, and the press is ours to handle.
static const char* const kDecoyScript = "chopagotchi_app";
static const char* const kNameKey = "CHOPAGOTCHI";
static const char* const kAnchorPrefix = "Phone_SoundSet_";

struct Layout {
    int table = -1;  // the table's length header; row r starts at table + 1 + 15 r
    int rows = 0;
    int map = -1;    // length header of "cell -> row shown there"
    int used = -1;   // length header of "cell taken"
    int page = -1;   // the home-screen page last drawn
    int flags = -1;
    int anchor = -1; // the sound set label; the phone's state follows it
    int row(int r, int field) const { return table + 1 + r * kRowSize + field; }
    int mapAt(int cell) const { return map + 1 + cell; }
    int usedAt(int cell) const { return used + 1 + cell; }
    int handle() const { return anchor - 8; }     // the phone's movie
    int highlight() const { return anchor - 9; }  // highlighted home-screen cell
    int owner() const { return anchor + 11; }     // whose phone: 0..3
    int state() const { return anchor + 12; }
    int pressedRow() const { return anchor + 14; } // row of the icon just pressed
};

// ---- reading and writing slots ---------------------------------------------------
inline bool get(Host& h, int index, int32_t* out)
{
    uint64_t* p = index >= 0 ? h.slot(index) : nullptr;
    if (!p) return false;
    *out = (int32_t)(uint32_t)*p;
    return true;
}
inline int32_t getOr(Host& h, int index, int32_t fallback)
{
    int32_t v;
    return get(h, index, &v) ? v : fallback;
}
inline bool set(Host& h, int index, int32_t v)
{
    uint64_t* p = index >= 0 ? h.slot(index) : nullptr;
    if (!p) return false;
    *p = (uint64_t)(uint32_t)v;
    return true;
}
// Text labels are plain bytes laid over consecutive slots.
inline bool getLabel(Host& h, int index, int slotCount, char* out, size_t n)
{
    char buf[64];
    if (slotCount < 1 || slotCount > 8 || n == 0) return false;
    for (int i = 0; i < slotCount; ++i) {
        uint64_t* p = h.slot(index + i);
        if (!p) return false;
        memcpy(buf + i * 8, p, 8);
    }
    size_t len = 0;
    while (len < (size_t)slotCount * 8 && buf[len]) ++len;
    if (len >= n) len = n - 1;
    memcpy(out, buf, len);
    out[len] = 0;
    return true;
}
inline bool setLabel16(Host& h, int index, const char* text)
{
    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%s", text);
    uint64_t* a = h.slot(index);
    uint64_t* b = h.slot(index + 1);
    if (!a || !b) return false;
    memcpy(a, buf, 8);
    memcpy(b, buf + 8, 8);
    return true;
}
inline bool sameNoCase(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return *a == *b;
}

// ---- finding the table -------------------------------------------------------------
inline bool rowLaunches(Host& h, const Layout& L, int r, const char* script)
{
    char name[24];
    int32_t hash;
    if (!getLabel(h, L.row(r, kOffScript), 2, name, sizeof(name))) return false;
    if (!get(h, L.row(r, kOffScriptHash), &hash)) return false;
    return sameNoCase(name, script) && (uint32_t)hash == joaat(script);
}

// True when the slots at `table` are the phone's app table as the game fills it.
inline bool tableAt(Host& h, int table, Layout* out)
{
    int32_t n, v;
    if (table < kFlagsBeforeTable || !get(h, table, &n) || n < 30 || n > 64) return false;
    Layout L;
    L.table = table;
    L.rows = n;
    L.map = table + 1 + n * kRowSize;
    L.used = L.map + 1 + n;
    L.page = L.used + 1 + n;
    L.flags = table - kFlagsBeforeTable;
    if (L.page >= h.slots()) return false;
    if (!rowLaunches(h, L, 0, "appContacts")) return false;
    if (!rowLaunches(h, L, 1, "appTextMessage")) return false;
    if (!rowLaunches(h, L, 3, "appCamera")) return false;
    // The two per-cell arrays that follow are as long as the table.
    if (!get(h, L.map, &v) || v != n) return false;
    if (!get(h, L.used, &v) || v != n) return false;
    if (!get(h, L.page, &v) || v < 0 || v > 2) return false;
    if (out) *out = L;
    return true;
}

inline bool findTable(Host& h, Layout* out)
{
    const uint32_t contacts = joaat("appContacts");
    int n = h.slots();
    for (int b = kFlagsBeforeTable; b + 1 + 30 * kRowSize < n; ++b) {
        uint64_t* p = h.slot(b);
        if (!p) continue;
        uint32_t len = (uint32_t)*p;
        if (len < 30 || len > 64) continue;
        uint64_t* hp = h.slot(b + 1 + kOffScriptHash);
        if (!hp || (uint32_t)*hp != contacts) continue;
        if (tableAt(h, b, out)) return true;
    }
    return false;
}

inline bool anchorAt(Host& h, int index)
{
    char s[24];
    return getLabel(h, index, 2, s, sizeof(s)) && strncmp(s, kAnchorPrefix, strlen(kAnchorPrefix)) == 0;
}

// The sound set label; -1 when it is not there (yet) or there are several.
inline int findAnchor(Host& h, const Layout& L)
{
    int found = -1;
    int to = L.table + kAnchorSearchTo;
    if (to > h.slots() - 16) to = h.slots() - 16;
    for (int s = L.table + kAnchorSearchFrom; s < to; ++s) {
        uint64_t* p = h.slot(s);
        if (!p || memcmp(p, "Phone_So", 8) != 0 || !anchorAt(h, s)) continue;
        if (found >= 0) return -1;
        found = s;
    }
    return found;
}

// ---- what the app shows ------------------------------------------------------------
static const int kMaxRows = 12;
struct Row {
    char text[64];
    int icon; // 0 = none
};
struct Screen {
    char header[32] = "";
    Row rows[kMaxRows];
    int count = 0;
    int selected = 0;
    bool nested = false;    // a page below the app's first screen: Back returns to it
    bool canSelect = true;  // show the Select soft key
    void add(const char* text, int icon = 0)
    {
        if (count >= kMaxRows) return;
        snprintf(rows[count].text, sizeof(rows[count].text), "%s", text);
        rows[count].icon = icon;
        ++count;
    }
};

struct Config {
    const char* name = "Chopagotchi";
    int icon = 14;  // must be an icon the home screen has artwork for
    int cell = -1;  // -1 = a free cell, or the Checklist app's when there is none; 0..8 = this cell, whatever is there
    bool inChecklist = false; // true: no icon, a row inside the Checklist app instead
};

enum Event { None, Opened, Closed, Up, Down, Select, Back };

class Phone {
public:
    // Call every frame. `want` is false when the icon should not be there
    // (another character, online, switched off).
    Event frame(Host& h, bool want, const Config& cfg)
    {
        if (broken_) return None;
        collect(h);
        listReady_ = false;
        if (!want) {
            retire(h);
            return close(h);
        }
        if (!locate(h)) return close(h);

        int32_t state = getOr(h, L_.state(), -1);
        int32_t owner = getOr(h, L_.owner(), -1);
        if (state < 0 || state > kStateMax) {
            char msg[160];
            snprintf(msg, sizeof(msg), "In-phone app switched off: the phone's state reads %d, which makes no sense.", (int)state);
            fail(h, msg);
            return close(h);
        }
        if (owner < 0 || owner > 3) return close(h); // nobody's phone right now
        int handle = getOr(h, L_.handle(), 0);
        bool movie = state > kStateAway && handle > 0 && h.movieLoaded(handle);

        if (cfg.inChecklist) {
            retire(h); // an icon from before the setting changed
            listReady_ = true;
            return listFrame(h, state, handle, movie, cfg);
        }
        if (viaList_) return close(h);

        bool changed = keepRow(h, cfg);
        if (broken_) return close(h);
        if (state == kStateHome) {
            bool arrived = lastState_ != kStateHome;
            if (movie && getOr(h, L_.page, 1) <= 1) {
                if (cell_ < 0) {
                    releaseCell(h, handle);
                } else if (arrived || changed || drawnCell_ != cell_ || getOr(h, L_.mapAt(cell_), -1) != row_) {
                    paintCell(h, handle, cfg);
                }
            }
        }
        lastState_ = state;

        if (!open_) {
            if (state == kStateApp && cell_ >= 0 && getOr(h, L_.pressedRow(), -1) == row_) {
                // The phone has just asked for our (non-existent) script and
                // raised its "loading" flag. If the flag is not where we think
                // it is we could never lower it, so step back to the home
                // screen and stay out of the phone from here on.
                if (!(getOr(h, L_.flags, 0) & (1 << kLoadingBit))) {
                    set(h, L_.state(), kStateHome);
                    fail(h, "In-phone app switched off: the phone's loading flag is not where it was expected.");
                    return None;
                }
                open_ = true;
                toTop_ = false;
                shown_ = false;
                clearLoading(h);
                return Opened;
            }
            return None;
        }
        if (state != kStateApp && state != kStateAppNested) return close(h);
        clearLoading(h);
        if (toTop_) {
            // One frame after Back was pressed on a page, so the phone does not
            // take that same press as "leave the app".
            toTop_ = false;
            if (state == kStateAppNested) set(h, L_.state(), kStateApp);
            return None;
        }
        char soundSet[32];
        if (!getLabel(h, L_.anchor, 4, soundSet, sizeof(soundSet))) soundSet[0] = 0;
        if (h.pressed(kCtlUp) || h.pressed(kCtlWheelUp)) {
            h.sound("Menu_Navigate", soundSet);
            return Up;
        }
        if (h.pressed(kCtlDown) || h.pressed(kCtlWheelDown)) {
            h.sound("Menu_Navigate", soundSet);
            return Down;
        }
        if (h.pressed(kCtlSelect)) {
            h.sound("Menu_Accept", soundSet);
            return Select;
        }
        // On the app's first screen Back belongs to the phone: it returns to
        // the home screen by itself and we see the state change.
        if (state == kStateAppNested && h.pressed(kCtlBack)) {
            h.sound("Menu_Back", soundSet);
            return Back;
        }
        return None;
    }

    // Draws the app's screen; call every frame while open(). Only what
    // changed since the last call is sent to the phone.
    void show(Host& h, const Screen& s)
    {
        if (!open_ || broken_ || L_.anchor < 0) return;
        int32_t state = getOr(h, L_.state(), -1);
        if (viaList_) {
            // Inside Checklist every screen of ours is a page below its menu.
            if (state != kStateAppNested) return;
        } else {
            if (state != kStateApp && state != kStateAppNested) return;
            if (s.nested && state == kStateApp) set(h, L_.state(), kStateAppNested);
            if (!s.nested && state == kStateAppNested) toTop_ = true;
        }

        int handle = getOr(h, L_.handle(), 0);
        if (handle <= 0 || !h.movieLoaded(handle)) return;
        bool all = !shown_ || s.count != last_.count || strcmp(s.header, last_.header) != 0 || s.canSelect != last_.canSelect;
        bool any = all || s.selected != last_.selected;
        if (all) {
            h.begin(handle, "SET_DATA_SLOT_EMPTY");
            h.addInt(kViewList);
            h.end();
        }
        for (int i = 0; i < s.count; ++i) {
            if (!all && s.rows[i].icon == last_.rows[i].icon && strcmp(s.rows[i].text, last_.rows[i].text) == 0) continue;
            h.begin(handle, "SET_DATA_SLOT");
            h.addInt(kViewList);
            h.addInt(i);
            h.addInt(s.rows[i].icon);
            h.addText(s.rows[i].text);
            h.end();
            any = true;
        }
        if (any) {
            h.begin(handle, "DISPLAY_VIEW");
            h.addInt(kViewList);
            h.addInt(s.count > 0 ? s.selected : 0);
            h.end();
        }
        if (all) {
            h.begin(handle, "SET_HEADER");
            h.addText(s.header);
            h.end();
            softKey(h, handle, 2, s.canSelect, 2); // Select
            softKey(h, handle, 3, true, 4);        // Back
            softKey(h, handle, 1, false, 1);
        }
        last_ = s;
        shown_ = true;
    }

    bool open() const { return open_; }
    // True while the phone has a way in (the icon, or the row inside
    // Checklist), so the caller can drop its own hint.
    bool onPhone() const { return !broken_ && ((wrote_ && cell_ >= 0) || listReady_); }
    bool broken() const { return broken_; }
    const Layout& layout() const { return L_; }
    int row() const { return row_; }
    int cell() const { return cell_; }

private:
    Layout L_;
    bool broken_ = false; // a check failed: leave the phone alone until the game restarts
    unsigned long long nextScan_ = 0;
    bool saidNoTable_ = false, saidFull_ = false;
    int row_ = -1;        // our row in the table
    int cell_ = -1;       // the home-screen cell it sits on, -1 = none free
    bool wrote_ = false;
    int drawnCell_ = -1;  // cell last painted with our name
    int lastState_ = -1;
    bool open_ = false, toTop_ = false, shown_ = false;
    Screen last_;
    // Inside the Checklist app.
    bool listReady_ = false;  // the phone is known, so the row will be there
    bool viaList_ = false;    // open_, and reached through the Checklist row
    int menuFrames_ = 0;      // frames the Checklist app's first screen has been up
    bool saidAdded_ = false;
    bool asking_ = false;     // Select was pressed there: waiting to hear which row
    bool ticketOut_ = false;  // an answer that still has to be picked up
    int ticket_ = 0;
    unsigned long long askedAt_ = 0;
    bool restarting_ = false; // the phone was asked to start Checklist again
    unsigned long long restartAt_ = 0;
    bool cameBack_ = false;   // ... so highlight our row when it does

    Event close(Host& h)
    {
        asking_ = false;
        menuFrames_ = 0;
        if (!open_) return None;
        open_ = false;
        toTop_ = false;
        if (viaList_) {
            viaList_ = false;
            // The Checklist script was ended, so nothing answers Back on a
            // page any more. Step up to where the phone itself handles Back.
            if (L_.anchor >= 0 && anchorAt(h, L_.anchor) && getOr(h, L_.state(), -1) == kStateAppNested)
                set(h, L_.state(), kStateApp);
        }
        return Closed;
    }

    // An answer nobody waits for any more is still picked up, so the game gets
    // its ticket back.
    void collect(Host& h)
    {
        if (!ticketOut_ || asking_) return;
        if (h.answerReady(ticket_)) {
            h.answerInt(ticket_);
            ticketOut_ = false;
        } else if (h.nowMs() - askedAt_ > 4 * kAnswerMs) {
            ticketOut_ = false;
        }
    }

    // ---- inside the Checklist app ---------------------------------------------
    Event listFrame(Host& h, int32_t state, int handle, bool movie, const Config& cfg)
    {
        int32_t flags = getOr(h, L_.flags, 0);
        bool loading = (flags & (1 << kLoadingBit)) != 0;
        bool inChecklist = getOr(h, L_.pressedRow(), -1) == kChecklistRow;
        unsigned long long now = h.nowMs();

        if (restarting_) {
            if (!loading || state != kStateApp || !inChecklist) {
                // Started (or the phone went elsewhere): our hold on the script can go.
                h.releaseScript(kChecklistScript);
                restarting_ = false;
            } else if (now - restartAt_ > kRestartMs) {
                // While the phone waits for a script it ignores Back. Do not leave it like that.
                set(h, L_.flags, flags & ~(1 << kLoadingBit));
                loading = false;
                h.releaseScript(kChecklistScript);
                restarting_ = false;
                h.log("In-phone app: the Checklist app did not start again. Back returns to the home screen.");
            }
        }
        if (state != kStateApp || !inChecklist) cameBack_ = false;

        if (open_) {
            // Ours as long as the phone stays on the page we put it on.
            if (state != kStateAppNested || !inChecklist) return close(h);
            char soundSet[32];
            if (!getLabel(h, L_.anchor, 4, soundSet, sizeof(soundSet))) soundSet[0] = 0;
            if (h.pressed(kCtlUp) || h.pressed(kCtlWheelUp)) {
                h.sound("Menu_Navigate", soundSet);
                return Up;
            }
            if (h.pressed(kCtlDown) || h.pressed(kCtlWheelDown)) {
                h.sound("Menu_Navigate", soundSet);
                return Down;
            }
            if (h.pressed(kCtlSelect)) {
                h.sound("Menu_Accept", soundSet);
                return Select;
            }
            if (h.pressed(kCtlBack)) {
                h.sound("Menu_Back", soundSet);
                if (shown_ && last_.nested) return Back;
                backToChecklist(h, flags, now);
                return close(h);
            }
            return None;
        }

        if (asking_) {
            bool there = movie && inChecklist && (state == kStateApp || state == kStateAppNested);
            if (!there || now - askedAt_ > kAnswerMs) {
                asking_ = false;
                return None;
            }
            if (!h.answerReady(ticket_)) return None;
            int row = h.answerInt(ticket_);
            ticketOut_ = false;
            asking_ = false;
            if (row != kOurListRow) return None; // one of Checklist's own rows
            // Ours. The Checklist script is about to open "Progress" for it
            // (or just has): end it and take the screen.
            h.endScript(kChecklistScript);
            set(h, L_.state(), kStateAppNested);
            open_ = true;
            viaList_ = true;
            shown_ = false;
            toTop_ = false;
            menuFrames_ = 0;
            return Opened;
        }

        // The Checklist app's first screen: the app is up, its script runs and
        // the phone is on the app's top level (its own pages are one level down).
        bool menu = movie && state == kStateApp && inChecklist && !loading &&
                    rowLaunches(h, L_, kChecklistRow, kChecklistScript) && h.scriptThreads(kChecklistScript) > 0;
        if (!menu) {
            menuFrames_ = 0;
            return None;
        }
        if (menuFrames_ <= kAddRowTo) ++menuFrames_;
        if (menuFrames_ >= kAddRowFrom && menuFrames_ <= kAddRowTo) {
            addRow(h, handle, cfg);
            if (menuFrames_ == kAddRowTo) cameBack_ = false;
        } else if (menuFrames_ > kAddRowTo && !ticketOut_ && h.pressed(kCtlSelect)) {
            h.begin(handle, "GET_CURRENT_SELECTION");
            ticket_ = h.endAsking();
            ticketOut_ = true;
            asking_ = true;
            askedAt_ = now;
        }
        return None;
    }

    void addRow(Host& h, int handle, const Config& cfg)
    {
        h.begin(handle, "SET_DATA_SLOT");
        h.addInt(kViewList);
        h.addInt(kOurListRow);
        h.addInt(0);
        h.addText(cfg.name);
        h.end();
        h.begin(handle, "DISPLAY_VIEW");
        h.addInt(kViewList);
        h.addInt(cameBack_ ? kOurListRow : 0);
        h.end();
        if (!saidAdded_) {
            saidAdded_ = true;
            h.log("In-phone app: added to the Checklist app's menu.");
        }
    }

    // Back on our first screen: have the phone start the Checklist app again,
    // exactly as when its icon is pressed (state "in app", script requested,
    // "loading" flag up). The flag also keeps the phone from reading this same
    // Back press as "leave the app".
    void backToChecklist(Host& h, int32_t flags, unsigned long long now)
    {
        set(h, L_.state(), kStateApp);
        if (!rowLaunches(h, L_, kChecklistRow, kChecklistScript)) return;
        h.requestScript(kChecklistScript);
        set(h, L_.flags, flags | (1 << kLoadingBit));
        restarting_ = true;
        restartAt_ = now;
        cameBack_ = true;
    }

    void fail(Host& h, const char* why)
    {
        h.log(why);
        retire(h);
        broken_ = true;
    }

    bool locate(Host& h)
    {
        if (L_.table >= 0 && !tableAt(h, L_.table, nullptr)) {
            // A save was loaded or the game moved on: start over.
            L_ = Layout();
            row_ = cell_ = drawnCell_ = -1;
            wrote_ = false;
            lastState_ = -1;
            restarting_ = false;
        }
        if (L_.table < 0) {
            unsigned long long now = h.nowMs();
            if (now < nextScan_) return false;
            nextScan_ = now + 2000;
            Layout found;
            if (!findTable(h, &found)) {
                if (!saidNoTable_) {
                    saidNoTable_ = true;
                    h.log("In-phone app: the phone's app table is not there yet.");
                }
                return false;
            }
            L_ = found;
            char msg[200];
            snprintf(msg, sizeof(msg), "In-phone app: app table at %d (%d rows), cells at %d/%d, page at %d, flags at %d.",
                     L_.table, L_.rows, L_.map, L_.used, L_.page, L_.flags);
            h.log(msg);
        }
        if (L_.anchor >= 0 && !anchorAt(h, L_.anchor)) L_.anchor = -1;
        if (L_.anchor < 0) {
            // The label is filled in the first time the phone comes out.
            int a = findAnchor(h, L_);
            if (a < 0) return false;
            L_.anchor = a;
            char msg[200];
            snprintf(msg, sizeof(msg), "In-phone app: phone state at %d (sound set label at %d, %+d from the table).",
                     L_.state(), a, a - L_.table);
            h.log(msg);
        }
        return true;
    }

    bool rowIsOurs(Host& h, int r) { return (uint32_t)getOr(h, L_.row(r, kOffScriptHash), 0) == joaat(kDecoyScript); }

    // The first cell of page 1 no other app sits on; our current one if it is still free.
    int freeCell(Host& h)
    {
        bool taken[kCells] = {false};
        for (int r = 0; r < L_.rows; ++r) {
            if (r == row_) continue;
            if (getOr(h, L_.row(r, kOffPage), 0) != 1) continue;
            int32_t c = getOr(h, L_.row(r, kOffCell), kHidden);
            if (c >= 0 && c < kCells) taken[c] = true;
        }
        if (cell_ >= 0 && !taken[cell_]) return cell_;
        for (int c = 0; c < kCells; ++c)
            if (!taken[c]) return c;
        return -1;
    }

    // Keeps our row in the table: the game wipes it whenever it rebuilds the
    // table (loading, switching character, opening an app by itself).
    // Returns true when the row had to be written.
    bool keepRow(Host& h, const Config& cfg)
    {
        if (row_ < 0) {
            for (int r : kSpareRows) {
                if (r >= L_.rows) continue;
                if (getOr(h, L_.row(r, kOffScriptHash), -1) == 0 || rowIsOurs(h, r)) {
                    row_ = r;
                    break;
                }
            }
            if (row_ < 0) {
                fail(h, "In-phone app switched off: no spare row in the phone's app table.");
                return false;
            }
        }
        int cell;
        if (cfg.cell >= 0 && cfg.cell < kCells) {
            cell = cfg.cell;
            // Asked for by name: the game's app on that cell gives way.
            for (int r = 0; r < L_.rows; ++r)
                if (r != row_ && getOr(h, L_.row(r, kOffPage), 0) == 1 && getOr(h, L_.row(r, kOffCell), kHidden) == cell)
                    set(h, L_.row(r, kOffCell), kHidden);
        } else {
            cell = freeCell(h);
            if (cell < 0) {
                // A full home screen: the Checklist app gives up its cell.
                int32_t c = getOr(h, L_.row(kChecklistRow, kOffCell), kHidden);
                if (rowLaunches(h, L_, kChecklistRow, "appChecklist") && getOr(h, L_.row(kChecklistRow, kOffPage), 0) == 1 &&
                    c >= 0 && c < kCells) {
                    set(h, L_.row(kChecklistRow, kOffCell), kHidden);
                    cell = c;
                    if (!saidFull_) h.log("In-phone app: the home screen is full, so the icon takes the Checklist app's place.");
                } else if (!saidFull_) {
                    h.log("In-phone app: every cell of the phone's home screen is taken, so there is no icon for now.");
                }
                saidFull_ = true;
            }
        }
        cell_ = cell;
        int want = cell >= 0 ? cell : kHidden;
        bool intact = wrote_ && rowIsOurs(h, row_) && getOr(h, L_.row(row_, kOffCell), 0) == want &&
                      getOr(h, L_.row(row_, kOffPage), 0) == 1 && getOr(h, L_.row(row_, kOffIcon), 0) == cfg.icon;
        if (intact) return false;
        setLabel16(h, L_.row(row_, kOffName), kNameKey);
        setLabel16(h, L_.row(row_, kOffScript), kDecoyScript);
        set(h, L_.row(row_, kOffScriptHash), (int32_t)joaat(kDecoyScript));
        set(h, L_.row(row_, kOffIcon), cfg.icon);
        set(h, L_.row(row_, kOffPage), 1);
        set(h, L_.row(row_, 12), 1);
        set(h, L_.row(row_, 13), 0);
        set(h, L_.row(row_, 14), 0);
        set(h, L_.row(row_, kOffCell), want); // last: this is what makes the row show
        if (!wrote_) {
            char msg[120];
            snprintf(msg, sizeof(msg), "In-phone app: icon registered in row %d, home-screen cell %d.", row_, cell);
            h.log(msg);
        }
        wrote_ = true;
        return true;
    }

    // The game draws our icon itself whenever it rebuilds the home screen, but
    // with the row's text key as its name, and not at all if the row was
    // written after the rebuild. So the cell is sent to the movie again, the
    // way the game sends it, with the name as plain text.
    void paintCell(Host& h, int handle, const Config& cfg)
    {
        if (drawnCell_ >= 0 && drawnCell_ != cell_) releaseCell(h, handle);
        h.begin(handle, "SET_DATA_SLOT");
        h.addInt(kViewHome);
        h.addInt(cell_);
        h.addInt(cfg.icon);
        h.addInt(0); // no badge
        h.addText(cfg.name);
        h.addInt(255);
        h.end();
        set(h, L_.mapAt(cell_), row_);
        set(h, L_.usedAt(cell_), 1);
        drawnCell_ = cell_;
        redrawHome(h, handle);
    }

    void redrawHome(Host& h, int handle)
    {
        int32_t hi = getOr(h, L_.highlight(), 0);
        if (hi < 0 || hi >= kCells) hi = 0;
        h.begin(handle, "DISPLAY_VIEW");
        h.addInt(kViewHome);
        h.addInt(hi);
        h.end();
    }

    // Takes our icon off the cell it was drawn on, if that cell is still ours.
    void releaseCell(Host& h, int handle)
    {
        int c = drawnCell_;
        drawnCell_ = -1;
        if (c < 0 || getOr(h, L_.mapAt(c), -1) != row_) return;
        h.begin(handle, "SET_DATA_SLOT");
        h.addInt(kViewHome);
        h.addInt(c);
        h.end();
        set(h, L_.mapAt(c), 0);
        set(h, L_.usedAt(c), 0);
        redrawHome(h, handle);
    }

    // Hides the icon again (another character's phone, online, switched off).
    void retire(Host& h)
    {
        if (L_.table < 0 || row_ < 0 || !tableAt(h, L_.table, nullptr)) return;
        if (wrote_ && rowIsOurs(h, row_)) set(h, L_.row(row_, kOffCell), kHidden);
        wrote_ = false;
        int shownOn = drawnCell_ >= 0 ? drawnCell_ : cell_;
        cell_ = -1;
        if (shownOn < 0 || L_.anchor < 0 || !anchorAt(h, L_.anchor)) return;
        if (getOr(h, L_.mapAt(shownOn), -1) != row_) {
            drawnCell_ = -1;
            return;
        }
        drawnCell_ = shownOn; // still on screen: take it off once the home screen is up
        int handle = getOr(h, L_.handle(), 0);
        if (getOr(h, L_.state(), -1) == kStateHome && handle > 0 && h.movieLoaded(handle)) releaseCell(h, handle);
    }

    void clearLoading(Host& h)
    {
        int32_t f;
        if (get(h, L_.flags, &f) && (f & (1 << kLoadingBit))) set(h, L_.flags, f & ~(1 << kLoadingBit));
    }

    void softKey(Host& h, int handle, int button, bool visible, int icon)
    {
        h.begin(handle, "SET_SOFT_KEYS");
        h.addInt(button);
        h.addInt(visible ? 1 : 0);
        h.addInt(icon);
        h.end();
    }
};

} // namespace realphone
