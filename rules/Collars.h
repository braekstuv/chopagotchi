// Chopagotchi - earning Chop's collars.
//
// The first (green) collar is his from the start. The other four are earned:
//   Collar 2  achievement: take Chop for a number of walks (the caller counts
//             one per in-game day, the walks that earn the walk bonus)
//   Collar 3  achievement: play a number of games of fetch
//   Collar 4  bond streak: keep him happy through several in-game days of play
//             in a row (play time, so sleeping the days away does not count)
//   Collar 5  rare find:   Chop sometimes digs it up while you walk together
// Pure logic with no game or Windows calls (tested in tests/test_collars.cpp).
// Chopagotchi.cpp feeds it events and keeps the progress in the state file.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace collars {

static const int kCount = 5;
enum Goal { Free, Walks, Fetches, BondStreak, RareFind };
static const Goal kGoal[kCount] = {Free, Walks, Fetches, BondStreak, RareFind};
// An in-game day at the game's normal pace: 48 minutes of play.
static const float kDaySeconds = 48 * 60;

// Thresholds, from [Collars] in Chopagotchi.ini.
struct Config {
    bool locked = true;           // false = every collar available, as before
    int walks = 10;               // walks for collar 2
    int fetches = 50;             // balls brought back for collar 3
    float bondHappiness = 80;     // happiness to stay at or above...
    int bondDays = 3;             // ...through this many in-game days of play in a row (collar 4)
    float findChance = 2;         // percent chance per minute walking together (collar 5)
};

// Progress, kept in the state file.
struct Progress {
    int unlocked = 1;             // bit per collar; the first is always set
    int walks = 0;
    int fetches = 0;
    int bondStreak = 0;           // in-game days of play in a row at or above bondHappiness
    float bondSeconds = 0;        // play time at or above it towards the next day
};

inline bool isUnlocked(const Progress& p, const Config& c, int collar)
{
    if (collar < 0 || collar >= kCount) return false;
    return !c.locked || collar == 0 || (p.unlocked & (1 << collar)) != 0;
}

// Unlocks every collar whose goal is met. Returns a bit per newly unlocked collar.
inline int settle(Progress& p, const Config& c)
{
    int before = p.unlocked;
    p.unlocked |= 1;
    if (p.walks >= c.walks) p.unlocked |= 1 << 1;
    if (p.fetches >= c.fetches) p.unlocked |= 1 << 2;
    if (p.bondStreak >= c.bondDays) p.unlocked |= 1 << 3;
    return p.unlocked & ~before & ~1;
}

inline int onWalk(Progress& p, const Config& c) { ++p.walks; return settle(p, c); }
inline int onFetch(Progress& p, const Config& c) { ++p.fetches; return settle(p, c); }

// Called regularly with the play time that has passed (seconds) and Chop's
// happiness. Only time actually played counts, a day for every kDaySeconds of
// it: the game's clock is not looked at, so sleeping, a time skip or loading
// another save neither adds a day nor breaks the streak. Dropping below the
// line starts over.
inline int onHappiness(Progress& p, const Config& c, float seconds, float happy)
{
    if (isUnlocked(p, c, 3)) return settle(p, c);  // earned (or nothing to earn): stop counting
    if (happy < c.bondHappiness) {
        p.bondStreak = 0;
        p.bondSeconds = 0;
    } else if (seconds > 0) {
        p.bondSeconds += seconds;
        while (p.bondSeconds >= kDaySeconds) {
            p.bondSeconds -= kDaySeconds;
            ++p.bondStreak;
        }
    }
    return settle(p, c);
}

// One minute walking together. roll is a random number in [0, 1).
inline int onWalkMinute(Progress& p, const Config& c, float roll)
{
    int got = settle(p, c);
    if (!isUnlocked(p, c, 4) && roll * 100.0f < c.findChance) {
        p.unlocked |= 1 << 4;
        got |= 1 << 4;
    }
    return got;
}

// The next collar after `current` that can be worn (wraps around).
inline int next(const Progress& p, const Config& c, int current)
{
    for (int i = 1; i <= kCount; ++i) {
        int k = ((current < 0 ? 0 : current) + i) % kCount;
        if (isUnlocked(p, c, k)) return k;
    }
    return 0;
}

// "Locked: 10 walks (3/10)" and so on; empty when the collar can be worn.
// For the log: the app itself only says that a collar is locked.
inline void hint(const Progress& p, const Config& c, int collar, char* out, size_t n)
{
    if (n == 0) return;
    out[0] = 0;
    if (isUnlocked(p, c, collar)) return;
    auto shown = [](int have, int need) { return have > need ? need : have; };
    switch (kGoal[collar]) {
    case Walks: std::snprintf(out, n, "Locked: %d walks (%d/%d)", c.walks, shown(p.walks, c.walks), c.walks); break;
    case Fetches: std::snprintf(out, n, "Locked: %d fetches (%d/%d)", c.fetches, shown(p.fetches, c.fetches), c.fetches); break;
    case BondStreak:
        std::snprintf(out, n, "Locked: %d days at %d+ (%d/%d)", c.bondDays, (int)c.bondHappiness,
                      shown(p.bondStreak, c.bondDays), c.bondDays);
        break;
    case RareFind: std::snprintf(out, n, "Locked: dig it up on a walk"); break;
    default: break;
    }
}

// The note shown when a collar is unlocked.
inline void unlockMessage(const Config& c, int collar, char* out, size_t n)
{
    switch (kGoal[collar]) {
    case Walks: std::snprintf(out, n, "New collar for Chop: Collar %d, for %d walks together!", collar + 1, c.walks); break;
    case Fetches: std::snprintf(out, n, "New collar for Chop: Collar %d, for %d fetches!", collar + 1, c.fetches); break;
    case BondStreak:
        std::snprintf(out, n, "New collar for Chop: Collar %d, for %d happy days in a row!", collar + 1, c.bondDays);
        break;
    case RareFind: std::snprintf(out, n, "Chop dug something up on the walk: Collar %d!", collar + 1); break;
    default: std::snprintf(out, n, "New collar for Chop: Collar %d", collar + 1); break;
    }
}

// Reads Progress through get(key, default). `worn` is the collar saved before
// collars had to be earned: a player who already wore one keeps it.
template <class Get> Progress load(Get get, int worn)
{
    auto num = [&](const char* key, long long def) {
        std::string s = get(key, std::to_string(def));
        char* end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        return (end == s.c_str()) ? def : v;
    };
    Progress p;
    long long mask = num("Unlocked", -1);
    if (mask < 0) mask = 1 | (worn > 0 && worn < kCount ? 1 << worn : 0);
    p.unlocked = (int)(mask & ((1 << kCount) - 1)) | 1;
    auto count = [&](const char* key) { long long v = num(key, 0); return (int)(v < 0 ? 0 : (v > 1000000 ? 1000000 : v)); };
    p.walks = count("Walks");
    p.fetches = count("Fetches");
    p.bondStreak = count("BondStreak");
    long long secs = num("BondSeconds", 0);
    p.bondSeconds = (float)(secs < 0 ? 0 : (secs >= (long long)kDaySeconds ? (long long)kDaySeconds - 1 : secs));
    return p;
}

template <class Set> void save(const Progress& p, Set set)
{
    set("Unlocked", std::to_string(p.unlocked));
    set("Walks", std::to_string(p.walks));
    set("Fetches", std::to_string(p.fetches));
    set("BondStreak", std::to_string(p.bondStreak));
    set("BondSeconds", std::to_string((long long)p.bondSeconds));
}

}  // namespace collars
