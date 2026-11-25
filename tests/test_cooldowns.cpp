// Tests for rules/Cooldowns.h: the once-per-in-game-day walk bonus, the
// fetch / trick / petting cooldowns, saving them, and loading an older save.
#include "rules/Cooldowns.h"
#include "check.h"

#include <map>
#include <string>

using namespace cooldowns;

static const long long kDay = 24 * 60;  // in-game minutes per in-game day (the default)

static void testCalendar()
{
    // Matches the well-known Unix epoch offset: 1970-01-01 is day 719468.
    CHECK_EQ(daysFromCivil(1970, 1, 1), 719468);
    CHECK_EQ(daysFromCivil(1970, 1, 2) - daysFromCivil(1970, 1, 1), 1);
    // Month and year ends, leap years.
    CHECK_EQ(daysFromCivil(2013, 3, 1) - daysFromCivil(2013, 2, 28), 1);   // not a leap year
    CHECK_EQ(daysFromCivil(2016, 3, 1) - daysFromCivil(2016, 2, 28), 2);   // leap year
    CHECK_EQ(daysFromCivil(2100, 3, 1) - daysFromCivil(2100, 2, 28), 1);   // century, not leap
    CHECK_EQ(daysFromCivil(2000, 3, 1) - daysFromCivil(2000, 2, 28), 2);   // 400-year leap
    CHECK_EQ(daysFromCivil(2014, 1, 1) - daysFromCivil(2013, 12, 31), 1);
    CHECK_EQ(daysFromCivil(2014, 1, 1) - daysFromCivil(2013, 1, 1), 365);
    CHECK_EQ(daysFromCivil(2017, 1, 1) - daysFromCivil(2016, 1, 1), 366);

    // gameMinutes takes the game's 0-based month.
    CHECK_EQ(gameMinutes(2013, 0, 1, 0, 1) - gameMinutes(2013, 0, 1, 0, 0), 1);
    CHECK_EQ(gameMinutes(2013, 0, 2, 0, 0) - gameMinutes(2013, 0, 1, 23, 59), 1);       // midnight
    CHECK_EQ(gameMinutes(2013, 2, 1, 0, 0) - gameMinutes(2013, 1, 28, 23, 0), 60);      // Feb -> Mar
    CHECK_EQ(gameMinutes(2014, 0, 1, 0, 0) - gameMinutes(2013, 11, 31, 23, 30), 30);    // New Year
    CHECK_EQ(gameMinutes(2013, 5, 15, 10, 0) - gameMinutes(2013, 5, 14, 10, 0), kDay);
}

static void testWalkBonus()
{
    long long t0 = gameMinutes(2013, 8, 17, 9, 0);
    long long last = -1, left = -7;

    // First walk ever is rewarded and recorded.
    CHECK(tryWalkBonus(last, t0, kDay, &left));
    CHECK_EQ(last, t0);
    CHECK_EQ(left, 0);

    // The exploit: dismiss Chop and walk again straight away.
    CHECK(!tryWalkBonus(last, t0, kDay, &left));
    CHECK_EQ(left, kDay);
    CHECK(!tryWalkBonus(last, t0 + 5, kDay, &left));
    CHECK_EQ(left, kDay - 5);
    CHECK_EQ(last, t0);  // a refused walk does not push the cooldown back

    // One minute short of a day is still refused; a full day is allowed.
    CHECK(!tryWalkBonus(last, t0 + kDay - 1, kDay, &left));
    CHECK_EQ(left, 1);
    CHECK(tryWalkBonus(last, t0 + kDay, kDay, &left));
    CHECK_EQ(last, t0 + kDay);

    // Counts across midnight: 9:00 one day to 8:59 the next is refused.
    long long late = gameMinutes(2013, 8, 30, 23, 0);
    last = late;
    CHECK(!tryWalkBonus(last, gameMinutes(2013, 9, 1, 22, 59), kDay, &left));  // Sep 30 -> Oct 1
    CHECK_EQ(left, 1);
    CHECK(tryWalkBonus(last, gameMinutes(2013, 9, 1, 23, 0), kDay, &left));

    // Sleeping that skips past the day counts as the day passing.
    last = t0;
    CHECK(tryWalkBonus(last, t0 + kDay + 12 * 60, kDay));

    // Without the optional out-parameter.
    last = t0;
    CHECK(!tryWalkBonus(last, t0 + 1, kDay));

    // A configured cooldown of 0 means every walk counts.
    last = t0;
    CHECK(tryWalkBonus(last, t0, 0));
    CHECK(tryWalkBonus(last, t0, 0));
}

static void testOlderSave()
{
    long long t0 = gameMinutes(2013, 8, 17, 9, 0);
    long long last = t0, left = 0;
    // Loading an older save puts the clock before the last walk: allowed, and
    // the new (earlier) time becomes the reference.
    long long older = t0 - 3 * kDay;
    CHECK(tryWalkBonus(last, older, kDay, &left));
    CHECK_EQ(last, older);
    // ...and the normal cooldown applies again from there.
    CHECK(!tryWalkBonus(last, older + 10, kDay, &left));
    CHECK_EQ(left, kDay - 10);
}

static void testTimedRewards()
{
    const long long now = 1'795'000'000;  // Unix seconds
    // Defaults: fetch 1 min, each trick 5 min, petting 10 min.
    struct { long long cooldown; } cases[] = {{60}, {5 * 60}, {10 * 60}};
    for (auto c : cases) {
        long long readyAt = 0;
        CHECK(tryTimedReward(readyAt, now, c.cooldown));        // first one counts
        CHECK_EQ(readyAt, now + c.cooldown);
        CHECK(!tryTimedReward(readyAt, now, c.cooldown));       // spamming does not
        CHECK(!tryTimedReward(readyAt, now + c.cooldown - 1, c.cooldown));
        CHECK_EQ(readyAt, now + c.cooldown);                    // refused tries do not extend it
        CHECK(tryTimedReward(readyAt, now + c.cooldown, c.cooldown));
        CHECK_EQ(readyAt, now + 2 * c.cooldown);
    }

    // Each trick has its own cooldown: sit does not block paw.
    State st;
    CHECK(tryTimedReward(st.trickReadyAt[0], now, 300));
    CHECK(tryTimedReward(st.trickReadyAt[1], now, 300));
    CHECK(tryTimedReward(st.trickReadyAt[2], now, 300));
    CHECK(!tryTimedReward(st.trickReadyAt[0], now + 1, 300));
    // ...and fetch, tricks and petting do not block each other.
    CHECK(tryTimedReward(st.fetchReadyAt, now, 60));
    CHECK(tryTimedReward(st.petReadyAt, now, 600));

    // Cooldown 0 = no limit.
    long long r = 0;
    CHECK(tryTimedReward(r, now, 0));
    CHECK(tryTimedReward(r, now, 0));
}

// An in-memory stand-in for the state file.
struct Store {
    std::map<std::string, std::string> kv;
    auto getter() const
    {
        return [this](const char* k, const std::string& def) {
            auto it = kv.find(k);
            return it == kv.end() ? def : it->second;
        };
    }
    auto setter()
    {
        return [this](const char* k, const std::string& v) { kv[k] = v; };
    }
};

static void testPersistence()
{
    // Missing file: nothing on cooldown.
    Store empty;
    State fresh = load(empty.getter());
    CHECK_EQ(fresh.lastWalkGameMinute, -1);
    CHECK_EQ(fresh.fetchReadyAt, 0);
    CHECK_EQ(fresh.petReadyAt, 0);
    for (int i = 0; i < 3; ++i) CHECK_EQ(fresh.trickReadyAt[i], 0);

    // Round trip keeps every value, including ones past 32 bits.
    State st;
    st.lastWalkGameMinute = gameMinutes(2013, 8, 17, 9, 0);
    st.fetchReadyAt = 1'795'000'060;
    st.trickReadyAt[0] = 1'795'000'300;
    st.trickReadyAt[1] = 1'795'000'301;
    st.trickReadyAt[2] = 1'795'000'302;
    st.petReadyAt = 1'795'000'600;
    Store file;
    save(st, file.setter());
    CHECK_EQ(file.kv.size(), 6u);
    CHECK(file.kv["LastWalkGameMinute"] == std::to_string(st.lastWalkGameMinute));
    CHECK(file.kv.count("Trick1ReadyAt") && file.kv.count("Trick2ReadyAt") && file.kv.count("Trick3ReadyAt"));
    State back = load(file.getter());
    CHECK_EQ(back.lastWalkGameMinute, st.lastWalkGameMinute);
    CHECK_EQ(back.fetchReadyAt, st.fetchReadyAt);
    CHECK_EQ(back.petReadyAt, st.petReadyAt);
    for (int i = 0; i < 3; ++i) CHECK_EQ(back.trickReadyAt[i], st.trickReadyAt[i]);

    // After a reload the walk is still on cooldown: the exploit does not
    // come back by quitting and restarting.
    long long left = 0;
    CHECK(!tryWalkBonus(back.lastWalkGameMinute, st.lastWalkGameMinute + 60, kDay, &left));
    CHECK_EQ(left, kDay - 60);
    CHECK(!tryTimedReward(back.petReadyAt, 1'795'000'000, 600));

    // Hand-edited or damaged values fall back to "not on cooldown".
    Store bad;
    bad.kv["LastWalkGameMinute"] = "banana";
    bad.kv["FetchReadyAt"] = "";
    bad.kv["PetReadyAt"] = "x12";
    bad.kv["Trick2ReadyAt"] = "42";
    State b = load(bad.getter());
    CHECK_EQ(b.lastWalkGameMinute, -1);
    CHECK_EQ(b.fetchReadyAt, 0);
    CHECK_EQ(b.petReadyAt, 0);
    CHECK_EQ(b.trickReadyAt[1], 0);  // 1970: not a stamp of ours
    Store neg;
    neg.kv["LastWalkGameMinute"] = "-50";
    CHECK_EQ(load(neg.getter()).lastWalkGameMinute, -1);
}

// Bad values in the state file, or a clock moved back, must not lock a reward.
static void testSelfHealing()
{
    const long long now = 1'795'642'000;  // 25 Nov 2026

    // A readyAt further ahead than one cooldown is ignored and replaced.
    long long r = now + 10 * 60 + 1;
    CHECK(tryTimedReward(r, now, 10 * 60));
    CHECK_EQ(r, now + 10 * 60);
    r = 9'000'000'000'000LL;  // far future
    CHECK(tryTimedReward(r, now, 300));
    CHECK_EQ(r, now + 300);
    // Exactly one cooldown ahead is a genuine cooldown and still refused.
    r = now + 300;
    CHECK(!tryTimedReward(r, now, 300));
    // Shortening a cooldown in the ini takes effect at once.
    r = now + 600;  // set under a 10 min cooldown
    CHECK(tryTimedReward(r, now, 60));
    // A negative cooldown from a bad ini acts like 0.
    r = 0;
    CHECK(tryTimedReward(r, now, -5));
    CHECK(tryTimedReward(r, now, -5));
    // Negative and zero stamps are simply "ready".
    r = -123456;
    CHECK(tryTimedReward(r, now, 60));

    // Loading: stamps that cannot be Unix seconds of ours become 0.
    Store bad;
    bad.kv["FetchReadyAt"] = "1795642060";            // fine, kept
    bad.kv["Trick1ReadyAt"] = "-4294967296";          // negative
    bad.kv["Trick2ReadyAt"] = "4294967295";           // year 2106: in range, kept, healed on use
    bad.kv["Trick3ReadyAt"] = "99999999999999999999"; // overflows long long
    bad.kv["PetReadyAt"] = "123";                     // 1970
    State b = load(bad.getter());
    CHECK_EQ(b.fetchReadyAt, 1795642060);
    CHECK_EQ(b.trickReadyAt[0], 0);
    CHECK_EQ(b.trickReadyAt[1], 4294967295LL);
    CHECK_EQ(b.trickReadyAt[2], 0);
    CHECK_EQ(b.petReadyAt, 0);
    // Every one of them gives a reward right away now.
    CHECK(!tryTimedReward(b.fetchReadyAt, now, 60));  // genuine 60 s cooldown left
    CHECK(tryTimedReward(b.trickReadyAt[0], now, 300));
    CHECK(tryTimedReward(b.trickReadyAt[1], now, 300));
    CHECK(tryTimedReward(b.trickReadyAt[2], now, 300));
    CHECK(tryTimedReward(b.petReadyAt, now, 600));

    // heal(): the values found in a real state file are all reset, valid ones kept.
    State g;
    g.fetchReadyAt = now + 30;
    g.trickReadyAt[0] = -3889165104099819521LL;
    g.trickReadyAt[1] = -8029759185026482178LL;
    g.trickReadyAt[2] = now + 200;
    g.petReadyAt = 5613736935518419406LL;
    CHECK_EQ(heal(g, now, 60, 300, 600), 3);
    CHECK_EQ(g.fetchReadyAt, now + 30);
    CHECK_EQ(g.trickReadyAt[0], 0);
    CHECK_EQ(g.trickReadyAt[1], 0);
    CHECK_EQ(g.trickReadyAt[2], now + 200);
    CHECK_EQ(g.petReadyAt, 0);
    CHECK_EQ(heal(g, now, 60, 300, 600), 0);  // nothing left to fix
    // Loading those exact lines gives a usable state too.
    Store real;
    real.kv["LastWalkGameMinute"] = "1056779292";
    real.kv["FetchReadyAt"] = "1795642341";
    real.kv["Trick1ReadyAt"] = "-3889165104099819521";
    real.kv["Trick2ReadyAt"] = "-8029759185026482178";
    real.kv["Trick3ReadyAt"] = "-8554542495910309034";
    real.kv["PetReadyAt"] = "5613736935518419406";
    State rs = load(real.getter());
    CHECK_EQ(rs.lastWalkGameMinute, 1056779292);
    CHECK_EQ(rs.fetchReadyAt, 1795642341);
    for (int i = 0; i < 3; ++i) CHECK_EQ(rs.trickReadyAt[i], 0);
    CHECK_EQ(rs.petReadyAt, 0);
    CHECK(tryTimedReward(rs.petReadyAt, now, 600));

    // A garbage walk time far ahead acts like an older save: allowed.
    long long last = 9'000'000'000'000LL;
    CHECK(tryWalkBonus(last, gameMinutes(2013, 8, 17, 9, 0), kDay));
}

int main()
{
    testSelfHealing();
    testCalendar();
    testWalkBonus();
    testOlderSave();
    testTimedRewards();
    testPersistence();
    return finish("cooldowns");
}
