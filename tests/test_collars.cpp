// Tests for rules/Collars.h: which collars are earned how, the hints the app
// shows for locked ones, and keeping progress in the state file.
#include "rules/Collars.h"
#include "check.h"

#include <cstring>
#include <map>
#include <string>

using namespace collars;

static bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

static void testStart()
{
    Progress p;
    Config c;
    CHECK(isUnlocked(p, c, 0));
    for (int i = 1; i < kCount; ++i) CHECK(!isUnlocked(p, c, i));
    CHECK(!isUnlocked(p, c, -1) && !isUnlocked(p, c, 5));
    CHECK_EQ(next(p, c, 0), 0);  // nothing else to switch to
    c.locked = false;
    for (int i = 0; i < kCount; ++i) CHECK(isUnlocked(p, c, i));
    CHECK_EQ(next(p, c, 4), 0);
    CHECK_EQ(next(p, c, 1), 2);
}

static void testAchievements()
{
    Progress p;
    Config c;
    for (int i = 0; i < 9; ++i) CHECK_EQ(onWalk(p, c), 0);
    CHECK_EQ(onWalk(p, c), 1 << 1);  // the tenth walk
    CHECK_EQ(onWalk(p, c), 0);       // only announced once
    CHECK(isUnlocked(p, c, 1));
    for (int i = 0; i < 49; ++i) CHECK_EQ(onFetch(p, c), 0);
    CHECK_EQ(onFetch(p, c), 1 << 2);
    CHECK_EQ(next(p, c, 0), 1);
    CHECK_EQ(next(p, c, 1), 2);
    CHECK_EQ(next(p, c, 2), 0);

    // Lowering a threshold in the ini unlocks on the next event.
    Progress q;
    q.walks = 4;
    Config easy;
    easy.walks = 3;
    CHECK_EQ(settle(q, easy), 1 << 1);
}

static void testBondStreak()
{
    Config c;  // 80+ through 3 in-game days of play
    const float day = kDaySeconds;
    CHECK_EQ((int)day, 48 * 60);
    Progress p;
    CHECK_EQ(onHappiness(p, c, day - 1, 85), 0);
    CHECK_EQ(p.bondStreak, 0);
    CHECK_EQ(onHappiness(p, c, 1, 90), 0);  // a day's worth of play done
    CHECK_EQ(p.bondStreak, 1);
    CHECK_EQ(onHappiness(p, c, day, 80), 0);
    CHECK_EQ(p.bondStreak, 2);
    char h[64];
    hint(p, c, 3, h, sizeof(h));
    CHECK(same(h, "Locked: 3 days at 80+ (2/3)"));
    CHECK_EQ(onHappiness(p, c, day - 0.25f, 95), 0);
    CHECK_EQ(onHappiness(p, c, 0.25f, 95), 1 << 3);  // three days
    CHECK(isUnlocked(p, c, 3));
    // Earned: nothing more is counted, and a dip later takes nothing away.
    Progress after = p;
    CHECK_EQ(onHappiness(p, c, day, 10), 0);
    CHECK_EQ(p.bondStreak, after.bondStreak);
    CHECK(isUnlocked(p, c, 3));

    // Play time is all that counts: checks with no time between them (the
    // game's clock jumping ahead while Franklin sleeps) add nothing.
    Progress z;
    for (int i = 0; i < 1000; ++i) onHappiness(z, c, 0, 100);
    CHECK_EQ(z.bondStreak, 0);
    CHECK_EQ((int)z.bondSeconds, 0);

    // A dip below the line starts over, days and all.
    Progress d;
    onHappiness(d, c, day, 90);
    onHappiness(d, c, 600, 90);
    CHECK_EQ(d.bondStreak, 1);
    CHECK_EQ((int)d.bondSeconds, 600);
    onHappiness(d, c, 0.25f, 79);
    CHECK_EQ(d.bondStreak, 0);
    CHECK_EQ((int)d.bondSeconds, 0);
    onHappiness(d, c, day, 90);
    CHECK_EQ(d.bondStreak, 1);

    // Time below the line is not counted.
    Progress l;
    onHappiness(l, c, day, 50);
    CHECK_EQ(l.bondStreak, 0);

    // Many small steps add up the same (the mod checks four times a second).
    Progress m;
    for (int i = 0; i < 48 * 60 * 4 - 1; ++i) onHappiness(m, c, 0.25f, 90);
    CHECK_EQ(m.bondStreak, 0);
    onHappiness(m, c, 0.25f, 90);
    CHECK_EQ(m.bondStreak, 1);

    // With collars not locked there is nothing to count.
    Config open;
    open.locked = false;
    Progress o;
    onHappiness(o, open, day, 90);
    CHECK_EQ(o.bondStreak, 0);
}

static void testRareFind()
{
    Config c;  // 2% a minute
    Progress p;
    CHECK_EQ(onWalkMinute(p, c, 0.5f), 0);
    CHECK_EQ(onWalkMinute(p, c, 0.02f), 0);
    CHECK_EQ(onWalkMinute(p, c, 0.019f), 1 << 4);
    CHECK(isUnlocked(p, c, 4));
    CHECK_EQ(onWalkMinute(p, c, 0.0f), 0);
    Config never;
    never.findChance = 0;
    Progress q;
    CHECK_EQ(onWalkMinute(q, never, 0.0f), 0);
}

static void testHints()
{
    Progress p;
    Config c;
    p.walks = 3;
    p.fetches = 12;
    char h[64];
    hint(p, c, 0, h, sizeof(h));
    CHECK(same(h, ""));
    hint(p, c, 1, h, sizeof(h));
    CHECK(same(h, "Locked: 10 walks (3/10)"));
    hint(p, c, 2, h, sizeof(h));
    CHECK(same(h, "Locked: 50 fetches (12/50)"));
    hint(p, c, 3, h, sizeof(h));
    CHECK(same(h, "Locked: 3 days at 80+ (0/3)"));
    hint(p, c, 4, h, sizeof(h));
    CHECK(same(h, "Locked: dig it up on a walk"));
    c.locked = false;
    hint(p, c, 4, h, sizeof(h));
    CHECK(same(h, ""));

    char m[96];
    unlockMessage(c, 1, m, sizeof(m));
    CHECK(same(m, "New collar for Chop: Collar 2, for 10 walks together!"));
    unlockMessage(c, 4, m, sizeof(m));
    CHECK(same(m, "Chop dug something up on the walk: Collar 5!"));
}

struct Store {
    std::map<std::string, std::string> kv;
    auto getter()
    {
        return [this](const char* k, const std::string& def) { auto it = kv.find(k); return it == kv.end() ? def : it->second; };
    }
    auto setter()
    {
        return [this](const char* k, const std::string& v) { kv[k] = v; };
    }
};

static void testPersistence()
{
    Progress p;
    p.unlocked = 1 | 4 | 16;
    p.walks = 7;
    p.fetches = 33;
    p.bondStreak = 2;
    p.bondSeconds = 1234.5f;
    Store s;
    save(p, s.setter());
    Progress b = load(s.getter(), 0);
    CHECK_EQ(b.unlocked, p.unlocked);
    CHECK_EQ(b.walks, 7);
    CHECK_EQ(b.fetches, 33);
    CHECK_EQ(b.bondStreak, 2);
    CHECK_EQ((int)b.bondSeconds, 1234);

    // A state file from before collars were earned: the collar he wore stays his.
    Store old;
    CHECK_EQ(load(old.getter(), 3).unlocked, 1 | 8);
    CHECK_EQ(load(old.getter(), -1).unlocked, 1);
    // Once saved, the worn collar no longer matters.
    Store saved;
    saved.kv["Unlocked"] = "1";
    CHECK_EQ(load(saved.getter(), 3).unlocked, 1);

    // Damaged values.
    Store bad;
    bad.kv["Unlocked"] = "999";
    bad.kv["Walks"] = "-4";
    bad.kv["Fetches"] = "lots";
    bad.kv["BondSeconds"] = "-9";
    Progress d = load(bad.getter(), 0);
    CHECK_EQ(d.unlocked, 999 & 31);
    CHECK_EQ(d.walks, 0);
    CHECK_EQ(d.fetches, 0);
    CHECK_EQ((int)d.bondSeconds, 0);
    bad.kv["BondSeconds"] = "99999999";
    CHECK(load(bad.getter(), 0).bondSeconds < kDaySeconds);  // never more than a day in the making
    bad.kv["Unlocked"] = "0";
    CHECK_EQ(load(bad.getter(), 0).unlocked, 1);  // the first collar can't be lost
}

int main()
{
    testStart();
    testAchievements();
    testBondStreak();
    testRareFind();
    testHints();
    testPersistence();
    return finish("collars");
}
