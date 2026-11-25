// Chopagotchi - watching what the player does with Chop, and the hotkey actions.
#pragma once
#include <windows.h>
#include <cstring>
#include <ctime>

#include "../rules/Collars.h"
#include "../rules/Cooldowns.h"
#include "ChopData.h"
#include "Hud.h"
#include "Log.h"
#include "Natives.h"
#include "Progress.h"
#include "Settings.h"

static const char* kTricksDict = "creatures@rottweiler@tricks@";
static const char* kMoveDict = "creatures@rottweiler@move";

static uint32_t hChop, hWalkStat;  // set in scriptMain
static int g_chop = 0;
static bool g_chopWasDead = false;
static bool g_wasFetchDrop = false, g_wasPetting = false;
static bool g_wasTrick[3] = {false, false, false};
static int32_t g_walkStat = -1;
static float g_lastPos[3] = {0, 0, 0};
static bool g_haveLastPos = false;
static float g_walkSeconds = 0;  // time spent moving with Chop, towards the next minute
static float g_awaySeconds = 0;  // time spent without Chop, towards the next decay point
static bool g_announced = false;

static void findChop(int player)
{
    if (g_chop && entityExists(g_chop) && entityModel(g_chop) == hChop) return;
    g_chop = 0;
    g_chopWasDead = false;
    int group = pedGroup(player);
    int n = groupSize(group);
    if (n < 0 || n > 16) return;
    for (int i = 0; i < n; ++i) {
        int ped = groupMember(group, i);
        if (ped && entityExists(ped) && entityModel(ped) == hChop) {
            g_chop = ped;
            return;
        }
    }
}

// 0 at home, 1 with you, 2 hurt.
static int chopWhere()
{
    if (!g_chop || !entityExists(g_chop)) return 0;
    return entityDead(g_chop) ? 2 : 1;
}

// dt: seconds played since the last tick.
static void tick(float dt)
{
    g_live = false;
    if (!ready()) return;
    if (!g_haveHappy) {
        // First run: start from whatever the game has.
        readFloat(g_block + kOffHappiness, &g_happy);
        g_haveHappy = true;
        g_stateDirty = true;
    }
    g_live = true;
    applyToGame();

    if (!g_announced) {
        g_announced = true;
        notify("Chopagotchi: Chop is %s (%d/100), tricks %d/4. Walk him, play fetch and pet him.",
               moodName(mood(g_happy)), (int)g_happy, levelForXp(g_xp));
    }

    int player = playerPed();
    if (!player || !entityExists(player)) return;
    long long wall = (long long)time(nullptr);  // real time, so cooldowns carry across restarts
    const long long fetchCd = minutesToSeconds(cdFetchMinutes), trickCd = minutesToSeconds(cdTrickMinutes),
                    petCd = minutesToSeconds(cdPetMinutes);
    {
        // Values no cooldown could have set (damaged file, clock moved back) would lock a reward.
        cooldowns::State before = g_cd;
        if (cooldowns::heal(g_cd, wall, fetchCd, trickCd, petCd)) {
            logf("Reset bad cooldowns at %lld: fetch %lld, tricks %lld %lld %lld, pet %lld", wall, before.fetchReadyAt,
                 before.trickReadyAt[0], before.trickReadyAt[1], before.trickReadyAt[2], before.petReadyAt);
            g_stateDirty = true;
        }
    }

    // A new walk: the game counts these itself.
    int32_t walks = 0;
    if (statInt(hWalkStat, &walks)) {
        if (g_walkStat >= 0 && walks > g_walkStat) {
            // Once per in-game day, so dismissing Chop and walking him again earns nothing.
            long long left = 0;
            if (cooldowns::tryWalkBonus(g_cd.lastWalkGameMinute, gameMinutes(), walkCooldownGameMinutes(), &left)) {
                g_stateDirty = true;
                addHappiness(gainWalkStart, "Walk started");
                // Only these walks count towards the collar, or calling and
                // dismissing Chop ten times in a row would earn it.
                collarsEarned(collars::onWalk(g_collarProgress, cfgCollars));
            } else {
                logf("Walk started, no reward: next in %lld in-game minutes", left);
                notify("Chop already had his walk today. Next walk bonus in %lldh %02lldm (game time).", left / 60,
                       left % 60);
            }
        }
        g_walkStat = walks;
    }

    findChop(player);

    // Bond streak: play time with Chop happy enough. dt is time played (paused
    // and loading time never gets here), so sleeping through the days does not count.
    {
        collars::Progress before = g_collarProgress;
        collarsEarned(collars::onHappiness(g_collarProgress, cfgCollars, dt, g_happy));
        // Saved when a day is done or lost, and once a minute in between.
        if (before.bondStreak != g_collarProgress.bondStreak ||
            (int)(before.bondSeconds / 60) != (int)(g_collarProgress.bondSeconds / 60))
            g_stateDirty = true;
    }

    float pos[3];
    entityCoords(player, pos);
    float moved = g_haveLastPos ? distance(pos, g_lastPos) : 0;
    memcpy(g_lastPos, pos, sizeof(pos));
    g_haveLastPos = true;

    bool together = false;
    if (g_chop) {
        bool dead = entityDead(g_chop);
        if (dead && !g_chopWasDead) addHappiness(-lossChopKilled, "Chop was hurt");
        g_chopWasDead = dead;

        if (!dead) {
            // Out together and actually going somewhere (on foot or driving).
            if (inGroup(g_chop, pedGroup(player)) && moved > 0.8f * dt && moved < 100.0f) {
                together = true;
                g_walkSeconds += dt;
                if (g_walkSeconds >= 60.0f) {
                    g_walkSeconds -= 60.0f;
                    addHappiness(gainWalkMinute, "Walking together");
                    addXp(xpWalkMinute);
                    collarsEarned(collars::onWalkMinute(g_collarProgress, cfgCollars, g_roll()));
                }
            }

            bool drop = playingAnim(g_chop, kMoveDict, "fetch_drop");
            if (drop && !g_wasFetchDrop) {
                // Every ball counts towards the collar, cooldown or not.
                g_stateDirty = true;
                if (cooldowns::tryTimedReward(g_cd.fetchReadyAt, wall, fetchCd)) {
                    addHappiness(gainFetch, "Fetch");
                    addXp(xpFetch);
                }
                collarsEarned(collars::onFetch(g_collarProgress, cfgCollars));
            }
            g_wasFetchDrop = drop;
            if (drop) together = true;

            static const char* trickAnims[3] = {"sit_loop", "paw_right_loop", "beg_loop"};
            for (int i = 0; i < 3; ++i) {
                bool on = playingAnim(g_chop, kTricksDict, trickAnims[i]);
                if (on && !g_wasTrick[i] && cooldowns::tryTimedReward(g_cd.trickReadyAt[i], wall, trickCd)) {
                    g_stateDirty = true;
                    addHappiness(gainTrick, "Trick");
                    addXp(xpTrick);
                }
                g_wasTrick[i] = on;
                if (on) together = true;
            }
        }
    }

    bool petting = playingAnim(player, kTricksDict, "petting_franklin");
    if (petting && !g_wasPetting && cooldowns::tryTimedReward(g_cd.petReadyAt, wall, petCd)) {
        g_stateDirty = true;
        addHappiness(gainPet, "Petting");
    }
    g_wasPetting = petting;
    if (petting) together = true;

    // Left alone, Chop slowly gets unhappy again.
    if (!together && decayMinutesPerPoint > 0) {
        g_awaySeconds += dt;
        float per = decayMinutesPerPoint * 60.0f;
        if (g_awaySeconds >= per) {
            g_awaySeconds -= per;
            addHappiness(-1.0f, "Time apart");
        }
    }
}

// F9 with [Phone] App=0: a one-line status message.
static void showStatus()
{
    if (!ready()) {
        notify("Chopagotchi: Chop's data not found. See Chopagotchi.log.");
        return;
    }
    int level = levelForXp(g_xp);
    if (level < 4)
        notify("Chop: %s (%d/100). Tricks %d/4, next: %s at %d/%d training.", moodName(mood(g_happy)), (int)g_happy,
               level, kTrickNames[level], (int)g_xp, (int)xpNeeded[level]);
    else
        notify("Chop: %s (%d/100). Knows all 4 tricks.", moodName(mood(g_happy)), (int)g_happy);
}

// Shift+F9: the next collar Chop has earned.
static void cycleCollar()
{
    if (!ready()) return;
    int32_t collar = g_collar;
    if (collar < 0) readInt(g_block + kOffCollar, &collar);
    int next = collars::next(g_collarProgress, cfgCollars, collar);
    if (next == collar) {
        notify("Chop has no other collar yet.");
        return;
    }
    g_collar = next;
    g_stateDirty = true;
    applyToGame();
    notify("Chop collar: %d", g_collar + 1);
}
