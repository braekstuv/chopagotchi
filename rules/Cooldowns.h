// Chopagotchi - reward cooldown rules.
//
// Pure logic with no game or Windows calls, so it can be tested on any host
// (see tests/). mod/Gameplay.h supplies the clocks, mod/Progress.h the state file.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace cooldowns {

// Days since 1 Jan 0000 for a proleptic Gregorian date (month 1-12).
inline long long daysFromCivil(long long y, long long m, long long d)
{
    if (m <= 2) { y -= 1; m += 12; }  // March-based year puts the leap day last
    return 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d - 1;
}

// The in-game clock as an absolute minute count. month0 is 0-11, as the game
// gives it. Keeps counting across midnight, month and year ends.
inline long long gameMinutes(long long year, long long month0, long long day, long long hour, long long minute)
{
    return (daysFromCivil(year, month0 + 1, day) * 24 + hour) * 60 + minute;
}

// Walk bonus: allowed when none was given yet, when the cooldown has passed,
// or when the clock is behind the last one (an older save was loaded; refusing
// would lock the bonus until the game caught up). On success records `now` in
// lastWalk; otherwise leaves it and puts the minutes remaining in *left.
inline bool tryWalkBonus(long long& lastWalk, long long now, long long cooldownMinutes, long long* left = nullptr)
{
    if (lastWalk < 0 || now < lastWalk || now - lastWalk >= cooldownMinutes) {
        lastWalk = now;
        if (left) *left = 0;
        return true;
    }
    if (left) *left = cooldownMinutes - (now - lastWalk);
    return false;
}

// Real-time reward (fetch, a trick, petting): allowed once `now` (seconds)
// reaches readyAt, which then moves cooldownSeconds ahead. A readyAt further
// ahead than one cooldown cannot have been set by us at this time (damaged
// file, the clock was moved back, or the cooldown was shortened in the ini),
// so it is ignored instead of locking the reward.
inline bool tryTimedReward(long long& readyAt, long long now, long long cooldownSeconds)
{
    if (cooldownSeconds < 0) cooldownSeconds = 0;
    if (now < readyAt && readyAt - now <= cooldownSeconds) return false;
    readyAt = now + cooldownSeconds;
    return true;
}

// Everything that has to survive a reload or restart.
struct State {
    long long lastWalkGameMinute = -1;  // -1 = never
    long long fetchReadyAt = 0;         // Unix seconds
    long long trickReadyAt[3] = {0, 0, 0};
    long long petReadyAt = 0;
};

// Resets any real-time stamp that is negative or further ahead than its
// cooldown (fetch, each trick, petting, in seconds) to 0 = ready. Returns how
// many were reset, so the caller can log that something went wrong.
inline int heal(State& st, long long now, long long fetchSeconds, long long trickSeconds, long long petSeconds)
{
    int fixed = 0;
    auto fix = [&](long long& v, long long cd) {
        if (cd < 0) cd = 0;
        if (v < 0 || v - now > cd) { v = 0; ++fixed; }
    };
    fix(st.fetchReadyAt, fetchSeconds);
    for (long long& t : st.trickReadyAt) fix(t, trickSeconds);
    fix(st.petReadyAt, petSeconds);
    return fixed;
}

static const long long kMinStamp = 1000000000;   // Sep 2001
static const long long kMaxStamp = 9999999999;   // Nov 2286

// Reads State from an INI-style store. get(key, default) returns the stored
// string or the default; missing or unreadable values fall back to defaults.
template <class Get> State load(Get get)
{
    auto num = [&](const char* key, long long def) {
        std::string s = get(key, std::to_string(def));
        char* end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        return (end == s.c_str()) ? def : v;
    };
    // Unix seconds outside 2001..2286 cannot be ours: treat as "ready".
    auto stamp = [&](const char* key) {
        long long v = num(key, 0);
        return (v < kMinStamp || v > kMaxStamp) ? 0 : v;
    };
    State st;
    st.lastWalkGameMinute = num("LastWalkGameMinute", -1);
    if (st.lastWalkGameMinute < -1) st.lastWalkGameMinute = -1;
    st.fetchReadyAt = stamp("FetchReadyAt");
    for (int i = 0; i < 3; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "Trick%dReadyAt", i + 1);
        st.trickReadyAt[i] = stamp(key);
    }
    st.petReadyAt = stamp("PetReadyAt");
    return st;
}

// Writes State through set(key, value).
template <class Set> void save(const State& st, Set set)
{
    set("LastWalkGameMinute", std::to_string(st.lastWalkGameMinute));
    set("FetchReadyAt", std::to_string(st.fetchReadyAt));
    for (int i = 0; i < 3; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "Trick%dReadyAt", i + 1);
        set(key, std::to_string(st.trickReadyAt[i]));
    }
    set("PetReadyAt", std::to_string(st.petReadyAt));
}

}  // namespace cooldowns
