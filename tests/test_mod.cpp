// Host-side tests for Chopagotchi.cpp and mod/. The mod's source is compiled in directly
// (so its static functions and state are reachable) against a fake Win32
// (tests/fake/windows.h) and the fake Script Hook V natives below, which play
// the game: Chop, the player, animations, the clock and the HUD.
//
// Build and run from the repo root:  sh tests/run.sh
#include <cmath>
#include <ctime>
#include <string>
#include <functional>
#include <set>
#include <string>
#include <vector>

static long long g_fakeWallTime = 1700000000;
static long long fakeTime(time_t* out)
{
    if (out) *out = (time_t)g_fakeWallTime;
    return g_fakeWallTime;
}
#define time(x) fakeTime(x)
#include "../Chopagotchi.cpp"
#undef time

// ---- test registry (checks come from check.h) --------------------------------------
#include "check.h"
struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& allTests() { static std::vector<TestCase> v; return v; }
struct TestReg { TestReg(const char* n, void (*f)()) { allTests().push_back({n, f}); } };
#define TEST(name) static void name(); static TestReg reg_##name(#name, name); static void name()
static const char* g_current = "";
#define EXPECT(cond) CHECK(cond)
#define EXPECT_NEAR(a, b, eps) do { ++g_checks; double a_ = (a), b_ = (b); if (fabs(a_ - b_) > (eps)) { ++g_failures; printf("FAIL %s:%d: [%s] %s = %g, expected %g\n", __FILE__, __LINE__, g_current, #a, a_, b_); } } while (0)

// ---- fake game ------------------------------------------------------------------
namespace game {
const int kPlayer = 101, kChop = 202, kGroup = 7;
bool online = false, paused = false, loading = false, hudOff = false;
float aspect = 16.0f / 9.0f, safeZone = 1.0f;                // screen shape and the game's safe-zone setting
bool chopExists = true, chopInGroup = true, chopDead = false;
int32_t walkStat = 0;
float playerPos[3] = {0, 0, 0};
int year = 2013, month = 8, day = 17, hour = 12, minute = 0; // month 0-11 as the game returns it
std::set<std::string> anims;                                 // "entity|anim"
std::vector<std::string> feed;                               // messages posted above the minimap
struct Drawn { std::string text; float x, y, scale; int a; };
std::vector<Drawn> drawn;                                    // text drawn on screen this frame
std::vector<uint64_t> globals(0x40000 + 0x1000, 0);
// Phone app
std::set<int> controls;                                      // controls just pressed this frame
std::set<int> disabled;                                      // controls the mod switched off
bool phoneUp = false, inVehicle = false;
float phoneScale = -1;                                       // last SET_MOBILE_PHONE_SCALE, -1 = never set
float chopOffset[3] = {0, 0, 0};                             // Chop's position relative to the player
std::vector<int> tasks;                                      // peds sent to the player (TASK_GO_TO_ENTITY)
std::vector<std::string> movieCalls;                         // what was sent to the phone's movie
std::string movieCall;
const char* playerModel = "player_one";                      // Franklin
int listSelection = 0;                                       // highlighted row of the phone's list
std::map<uint32_t, int> scriptThreads;                       // running game scripts, by name hash
std::vector<std::string> endedScripts, requestedScripts, releasedScripts;

void reset()
{
    online = paused = loading = hudOff = false;
    aspect = 16.0f / 9.0f;
    safeZone = 1.0f;
    chopExists = chopInGroup = true;
    chopDead = false;
    walkStat = 0;
    playerPos[0] = playerPos[1] = playerPos[2] = 0;
    year = 2013; month = 8; day = 17; hour = 12; minute = 0;
    anims.clear();
    feed.clear();
    drawn.clear();
    std::fill(globals.begin(), globals.end(), 0);
}

float asFloat(uint64_t v) { float f_; uint32_t b = (uint32_t)v; memcpy(&f_, &b, 4); return f_; }
uint64_t floatBits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }

// Puts a block that looks like Chop's saved data at index b.
void putBlock(int b, float happy = 10, int training = 0, int collar = 0, int valid = 1, int used = 0)
{
    for (int k = 0; k < 9; ++k) globals[b + kSigOffsets[k]] = kSigValues[k];
    globals[b + kOffHappiness] = floatBits(happy);
    globals[b + kOffTraining] = (uint32_t)training;
    globals[b + kOffCollar] = (uint32_t)collar;
    globals[b + kOffValid] = (uint32_t)valid;
    globals[b + kOffUsedApp] = (uint32_t)used;
}
float blockHappiness(int b) { return asFloat(globals[b + kOffHappiness]); }
} // namespace game

static uint64_t g_nHash = 0;
static std::vector<uint64_t> g_nArgs;
static uint64_t g_nResult[4];
static std::string g_pendingText;
static float g_textScale = 0;
static int g_textAlpha = 0;

extern "C" {
void shv_scriptRegister(HMODULE, void (*)()) {}
void shv_scriptUnregister(HMODULE) {}
void shv_scriptWait(DWORD) {}
void shv_nativeInit(uint64_t hash) { g_nHash = hash; g_nArgs.clear(); }
void shv_nativePush64(uint64_t v) { g_nArgs.push_back(v); }
uint64_t* shv_getGlobalPtr(int index)
{
    if (index < 0 || (size_t)index >= game::globals.size()) return nullptr;
    return &game::globals[index];
}
uint64_t* shv_nativeCall()
{
    using namespace game;
    auto arg = [](size_t i) { return i < g_nArgs.size() ? g_nArgs[i] : 0; };
    auto e = [&](size_t i) { return (int)(int32_t)(uint32_t)arg(i); };
    memset(g_nResult, 0, sizeof(g_nResult));
    uint64_t& r = g_nResult[0];
    switch (g_nHash) {
    case 0xD80958FC74E988A6ULL: r = kPlayer; break;                                            // PLAYER_PED_ID
    case 0x7239B21A38F536BAULL: r = e(0) == kPlayer || (e(0) == kChop && chopExists); break; // DOES_ENTITY_EXIST
    case 0x9F47B058362C84B5ULL: r = e(0) == kChop ? joaat("a_c_chop") : joaat(playerModel); break;
    case 0x5F9532F3B5CC2551ULL: r = e(0) == kChop && chopDead; break;                        // IS_ENTITY_DEAD
    case 0x1F0B79228E461EC9ULL:                                                              // IS_ENTITY_PLAYING_ANIM
        r = anims.count(std::to_string(e(0)) + "|" + (const char*)(uintptr_t)arg(2)) ? 1 : 0;
        break;
    case 0xF162E133B4E7A675ULL: r = kGroup; break;                                           // GET_PED_GROUP_INDEX
    case 0x9BB01E3834671191ULL: r = e(0) == kChop && chopInGroup && chopExists; break;       // IS_PED_GROUP_MEMBER
    case 0x51455483CF23ED97ULL: r = (e(1) == 0 && chopInGroup && chopExists) ? kChop : 0; break;
    case 0x8DE69FE35CA09A45ULL:                                                              // GET_GROUP_SIZE
        *(uint64_t*)(uintptr_t)arg(2) = (chopInGroup && chopExists) ? 1 : 0;
        break;
    case 0x767FBC2AC802EF3DULL:                                                              // STAT_GET_INT
        *(uint64_t*)(uintptr_t)arg(1) = (uint32_t)walkStat;
        r = 1;
        break;
    case 0x3FEF770D40960D5AULL:                                                              // GET_ENTITY_COORDS
        for (int i = 0; i < 3; ++i) g_nResult[i] = floatBits(playerPos[i] + (e(0) == kChop ? chopOffset[i] : 0));
        break;
    case 0x91AEF906BCA88877ULL: r = controls.count(e(1)); break;                             // IS_DISABLED_CONTROL_JUST_PRESSED
    case 0xFE99B66D079CF6BCULL: disabled.insert(e(1)); break;                                // DISABLE_CONTROL_ACTION
    case 0x2AFE52F782F25775ULL: r = e(0) == kPlayer && phoneUp; break;                       // IS_PED_RUNNING_MOBILE_PHONE_TASK
    case 0xCBDD322A73D6D932ULL: phoneScale = asFloat(arg(0)); break;                         // SET_MOBILE_PHONE_SCALE
    case 0x997ABD671D25CA0BULL: r = inVehicle; break;                                        // IS_PED_IN_ANY_VEHICLE
    case 0x6A071245EB0D1882ULL: tasks.push_back(e(0)); break;                                // TASK_GO_TO_ENTITY
    case 0x9DE624D2FC4B603FULL: r = online; break;
    case 0xB0034A223497FFCBULL: r = paused; break;
    case 0x10D0A8F259E93EC9ULL: r = loading; break;
    case 0xA86478C6958735C5ULL: r = hudOff; break;
    case 0xF1307EF624A80D87ULL: r = floatBits(aspect); break;                                // GET_ASPECT_RATIO
    case 0xBAF107B6BB2C97F0ULL: r = floatBits(safeZone); break;                              // GET_SAFE_ZONE_SIZE
    case 0x961777E64BDAF717ULL: r = (uint32_t)year; break;
    case 0xBBC72712E80257A1ULL: r = (uint32_t)month; break;
    case 0x3D10BC92A4DB1D35ULL: r = (uint32_t)day; break;
    case 0x25223CA6B4D20B7FULL: r = (uint32_t)hour; break;
    case 0x13D2B8ADD79640F2ULL: r = (uint32_t)minute; break;
    case 0x6C188BE134E074AAULL: g_pendingText = (const char*)(uintptr_t)arg(0); break;     // ADD_TEXT_COMPONENT...
    case 0x2ED7843F8F801023ULL: feed.push_back(g_pendingText); break;                         // feed post
    case 0x07C837F9A01C34C9ULL: g_textScale = asFloat(arg(0)); break;                         // SET_TEXT_SCALE
    case 0xBE6B23FFA53FB442ULL: g_textAlpha = e(3); break;                                    // SET_TEXT_COLOUR
    case 0xCD015E5BB0D96A57ULL:                                                               // END_TEXT_COMMAND_DISPLAY_TEXT
        drawn.push_back({g_pendingText, asFloat(arg(0)), asFloat(arg(1)), g_textScale, g_textAlpha});
        break;
    case 0x85F01B8D5B90570EULL: r = e(0) == 5; break;                                        // HAS_SCALEFORM_MOVIE_LOADED
    case 0xF6E48914C7A8694EULL:                                                              // BEGIN_SCALEFORM_MOVIE_METHOD
        movieCall = std::string((const char*)(uintptr_t)arg(1)) + "(" + std::to_string(e(0));
        break;
    case 0xC3D0841A0CC546A6ULL: movieCall += "," + std::to_string(e(0)); break;              // SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT
    case 0x362E2D3FE93A9959ULL: movieCall += ",'" + g_pendingText + "'"; break;              // END_TEXT_COMMAND_SCALEFORM_STRING
    case 0xC6796A8FFA375E53ULL: movieCalls.push_back(movieCall + ")"); break;                // END_SCALEFORM_MOVIE_METHOD
    case 0xC50AA39A577AF886ULL: movieCalls.push_back(movieCall + ")"); r = 77; break;        // END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE
    case 0x768FF8961BA904D6ULL: r = e(0) == 77; break;                                       // ..._RETURN_VALUE_READY
    case 0x2DE7EFA66B906036ULL: r = (uint32_t)listSelection; break;                          // ..._RETURN_VALUE_INT
    case 0x2C83A9DA6BFFC4F9ULL: r = (uint32_t)scriptThreads[(uint32_t)e(0)]; break;          // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH
    case 0x9DC711BC69C548DFULL:                                                              // TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME
        endedScripts.push_back((const char*)(uintptr_t)arg(0));
        scriptThreads[joaat((const char*)(uintptr_t)arg(0))] = 0;
        break;
    case 0x6EB5F71AA68F2E8EULL: requestedScripts.push_back((const char*)(uintptr_t)arg(0)); break; // REQUEST_SCRIPT
    case 0xC90D2DCACD56184CULL: releasedScripts.push_back((const char*)(uintptr_t)arg(0)); break;  // SET_SCRIPT_AS_NO_LONGER_NEEDED
    default: break;
    }
    return g_nResult;
}
}

// ---- per-test setup -------------------------------------------------------------
static const char* kIni = "/tmp/chopagotchi-test/Chopagotchi.ini";
static const char* kState = "/tmp/chopagotchi-test/Chopagotchi.state.ini";

static void resetMod()
{
    fake::reset();
    game::reset();
    g_fakeWallTime = 1700000000;
    fake::addRegion(game::globals.data(), game::globals.size() * sizeof(uint64_t));
    snprintf(g_dir, sizeof(g_dir), "/tmp/chopagotchi-test/");
    if (system("mkdir -p /tmp/chopagotchi-test") != 0) {}
    hChop = joaat("a_c_chop");
    hWalkStat = joaat("sp_chop_walk_done");
    loadSettings(); // empty ini: every setting at its default
    g_okLo = g_okHi = g_badLo = g_badHi = 0;
    g_page0 = nullptr;
    g_page0Slots = 0;
    g_block = -1;
    g_scanAttempts = 0;
    g_warnedInsane = g_loggedAmbiguous = false;
    g_xp = 0;
    g_happy = 0;
    g_haveHappy = false;
    g_collar = -1;
    g_stateDirty = false;
    g_live = false;
    g_cd = cooldowns::State();
    g_collarProgress = collars::Progress();
    g_roll = [] { return 0.99f; };  // no rare finds unless a test wants one
    g_gameFrom = 0;
    g_gameTo = -1;
    g_gameAt = 0;
    g_happyPop = GainPopup();
    g_trainPop = GainPopup();
    g_chop = 0;
    g_chopWasDead = g_wasFetchDrop = g_wasPetting = false;
    for (auto& w : g_wasTrick) w = false;
    g_walkStat = -1;
    g_haveLastPos = false;
    g_walkSeconds = g_awaySeconds = 0;
    g_announced = false;
}

// Starts a session with Chop's data at the default location and the given happiness.
static void startWith(float happy, float xp = 0)
{
    game::putBlock(kDefaultBlock, happy);
    g_happy = happy;
    g_haveHappy = true;
    g_xp = xp;
    tick(0);
    game::feed.clear();
}

static bool feedHas(const char* part)
{
    for (auto& m : game::feed)
        if (m.find(part) != std::string::npos) return true;
    return false;
}

// =================================================================================
// Basics
// =================================================================================
TEST(joaat_matches_game_hashes)
{
    EXPECT(joaat("a_c_chop") == 0x14EC17EAu);
    EXPECT(joaat("A_C_CHOP") == joaat("a_c_chop")); // case-insensitive like the game's
}

TEST(mood_thresholds)
{
    EXPECT(mood(0) == 0);
    EXPECT(mood(33) == 0);
    EXPECT(mood(33.1f) == 1);
    EXPECT(mood(66) == 1);
    EXPECT(mood(66.1f) == 2);
    EXPECT(mood(100) == 2);
    EXPECT(strcmp(moodName(0), "unhappy") == 0);
    EXPECT(strcmp(moodName(1), "content") == 0);
    EXPECT(strcmp(moodName(2), "happy") == 0);
}

TEST(trick_levels_unlock_at_thresholds)
{
    EXPECT(levelForXp(0) == 0);
    EXPECT(levelForXp(19.9f) == 0);
    EXPECT(levelForXp(20) == 1);
    EXPECT(levelForXp(59) == 1);
    EXPECT(levelForXp(60) == 2);
    EXPECT(levelForXp(120) == 3);
    EXPECT(levelForXp(199) == 3);
    EXPECT(levelForXp(200) == 4);
    EXPECT(levelForXp(100000) == 4);
}

TEST(easing_helpers)
{
    EXPECT_NEAR(clamp01(-1), 0, 1e-6);
    EXPECT_NEAR(clamp01(2), 1, 1e-6);
    EXPECT_NEAR(easeOut(0), 0, 1e-6);
    EXPECT_NEAR(easeOut(1), 1, 1e-6);
    EXPECT(easeOut(0.5f) > 0.5f); // fast start, slow finish
    EXPECT_NEAR(since(100, 200), 0, 1e-6);
    EXPECT_NEAR(since(250, 200), 50, 1e-6);
}

// =================================================================================
// Happiness and training
// =================================================================================
TEST(happiness_gain_is_clamped_to_100)
{
    resetMod();
    startWith(98);
    addHappiness(5, "test");
    EXPECT_NEAR(g_happy, 100, 1e-4);
    EXPECT(g_stateDirty);
}

TEST(happiness_loss_is_clamped_to_0)
{
    resetMod();
    startWith(3);
    addHappiness(-25, "test");
    EXPECT_NEAR(g_happy, 0, 1e-4);
}

TEST(no_change_at_limits_is_a_no_op)
{
    resetMod();
    startWith(100);
    g_stateDirty = false;
    addHappiness(5, "test");
    addHappiness(0, "test");
    EXPECT(!g_stateDirty);
    EXPECT(!g_happyPop.shown);
}

TEST(mood_change_messages)
{
    resetMod();
    startWith(30);
    addHappiness(5, "test");
    EXPECT(feedHas("Chop is content now (35/100)"));
    game::feed.clear();
    addHappiness(1, "test");
    EXPECT(game::feed.empty()); // same mood, no message
    addHappiness(-10, "test");
    EXPECT(feedHas("getting unhappy"));
}

TEST(notifications_can_be_switched_off)
{
    resetMod();
    startWith(30);
    cfgNotify = 0;
    addHappiness(50, "test");
    EXPECT(game::feed.empty());
}

TEST(training_unlocks_tricks_one_message_each)
{
    resetMod();
    startWith(50, 19);
    addXp(1);
    EXPECT(feedHas("new trick: Sit (1/4)"));
    game::feed.clear();
    addXp(200); // jumps several levels at once
    EXPECT(feedHas("Paw (2/4)"));
    EXPECT(feedHas("Beg (3/4)"));
    EXPECT(feedHas("Pet (4/4)"));
    EXPECT(game::feed.size() == 3);
    addXp(-5);
    EXPECT_NEAR(g_xp, 220, 1e-4); // negative training is ignored
}

// =================================================================================
// Smooth fill of the game's own bar
// =================================================================================
TEST(game_bar_fills_smoothly_on_gain)
{
    resetMod();
    startWith(40);
    addHappiness(10, "test");
    EXPECT_NEAR(g_happy, 50, 1e-4);             // the real value changes at once
    EXPECT_NEAR(happinessForGame(), 40, 1e-3);  // what the game shows starts at the old value
    fake::tickMs += 250;
    float q = happinessForGame();
    fake::tickMs += 250;
    float h = happinessForGame();
    EXPECT(q > 40 && q < h && h < 50);
    fake::tickMs += 500;
    EXPECT_NEAR(happinessForGame(), 50, 1e-3);
    fake::tickMs += 5000;
    EXPECT_NEAR(happinessForGame(), 50, 1e-3);
}

TEST(game_bar_drops_at_once_on_loss)
{
    resetMod();
    startWith(40);
    addHappiness(-10, "test");
    EXPECT_NEAR(happinessForGame(), 30, 1e-3);
}

TEST(loss_during_fill_jumps_to_new_value)
{
    resetMod();
    startWith(40);
    addHappiness(10, "test");
    fake::tickMs += 300;
    addHappiness(-25, "test");
    EXPECT_NEAR(happinessForGame(), 25, 1e-3);
}

TEST(second_gain_continues_from_where_the_bar_is)
{
    resetMod();
    startWith(40);
    addHappiness(10, "test");
    fake::tickMs += 400;
    float mid = happinessForGame();
    addHappiness(5, "test");
    EXPECT_NEAR(happinessForGame(), mid, 1e-3); // no jump back or forward
    fake::tickMs += 1000;
    EXPECT_NEAR(happinessForGame(), 55, 1e-3);
}

TEST(applyToGame_writes_animated_value_and_progress)
{
    resetMod();
    startWith(40, 60);
    g_collar = 3;
    addHappiness(20, "test");
    applyToGame();
    EXPECT_NEAR(game::blockHappiness(kDefaultBlock), 40, 1e-3);
    fake::tickMs += 1000;
    applyToGame();
    EXPECT_NEAR(game::blockHappiness(kDefaultBlock), 60, 1e-3);
    EXPECT(game::globals[kDefaultBlock + kOffTraining] == 2);
    EXPECT(game::globals[kDefaultBlock + kOffValid] == 1);
    EXPECT(game::globals[kDefaultBlock + kOffCollar] == 3);
    EXPECT(game::globals[kDefaultBlock + kOffUsedApp] == 1);
}

TEST(applyToGame_leaves_collar_alone_when_unset)
{
    resetMod();
    startWith(40);
    game::globals[kDefaultBlock + kOffCollar] = 2;
    g_collar = -1;
    applyToGame();
    EXPECT(game::globals[kDefaultBlock + kOffCollar] == 2);
}

TEST(applyToGame_writes_nothing_when_not_live)
{
    resetMod();
    game::putBlock(kDefaultBlock, 10);
    g_happy = 80;
    g_haveHappy = true;
    applyToGame(); // never located
    EXPECT_NEAR(game::blockHappiness(kDefaultBlock), 10, 1e-4);
}

// =================================================================================
// "Chop Happiness +N" / "Chop Training +N" popups
// =================================================================================
static const game::Drawn* drawnStarting(const char* prefix)
{
    for (auto& d : game::drawn)
        if (d.text.rfind(prefix, 0) == 0) return &d;
    return nullptr;
}
static const game::Drawn* drawnPlus() { return drawnStarting("Chop Happiness +"); }
static const game::Drawn* drawnTrain() { return drawnStarting("Chop Training +"); }

TEST(popup_shows_gain_beside_the_bar)
{
    resetMod();
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    const game::Drawn* d = drawnPlus();
    EXPECT(d && d->text == "Chop Happiness +5");
    if (d) {
        EXPECT_NEAR(d->x, 0.95, 1e-4);
        EXPECT(d->y <= 0.90f && d->y > 0.85f);
        EXPECT(d->a > 200);
    }
}

TEST(popup_floats_up_and_fades_out)
{
    resetMod();
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 300;
    drawGain(fake::tickMs);
    const game::Drawn early = *drawnPlus();
    game::drawn.clear();
    fake::tickMs += 1500;
    drawGain(fake::tickMs);
    const game::Drawn* late = drawnPlus();
    EXPECT(late != nullptr);
    if (late) {
        EXPECT(late->y < early.y);
        EXPECT(late->a < early.a);
    }
    game::drawn.clear();
    fake::tickMs += 500; // past 2 s
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() == nullptr);
    EXPECT(!g_happyPop.shown);
}

TEST(popup_pops_in_bigger_then_settles)
{
    resetMod();
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 50;
    drawGain(fake::tickMs);
    float first = drawnPlus()->scale;
    game::drawn.clear();
    fake::tickMs += 500;
    drawGain(fake::tickMs);
    EXPECT(first > drawnPlus()->scale);
}

TEST(popup_adds_up_quick_gains)
{
    resetMod();
    startWith(40);
    addHappiness(3, "fetch");
    fake::tickMs += 800;
    addHappiness(2, "walk");
    fake::tickMs += 200; // past the 120 ms fade-in
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && drawnPlus()->text == "Chop Happiness +5");
    fake::tickMs += 2500;
    addHappiness(1, "trick");
    game::drawn.clear();
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && drawnPlus()->text == "Chop Happiness +1"); // a fresh popup after the old one is gone
}

TEST(popup_shows_actual_gain_when_capped)
{
    resetMod();
    startWith(98);
    addHappiness(5, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && drawnPlus()->text == "Chop Happiness +2");
}

TEST(popup_formats_small_gains_with_a_decimal)
{
    resetMod();
    startWith(40);
    addHappiness(0.5f, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && drawnPlus()->text == "Chop Happiness +0.5");
}

TEST(no_popup_for_losses)
{
    resetMod();
    startWith(40);
    addHappiness(-10, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() == nullptr);
}

TEST(popup_hidden_in_menus_and_when_switched_off)
{
    resetMod();
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 200;
    game::paused = true;
    drawGain(fake::tickMs);
    game::paused = false;
    game::loading = true;
    drawGain(fake::tickMs);
    game::loading = false;
    game::hudOff = true;
    drawGain(fake::tickMs);
    game::hudOff = false;
    cfgGainPopup = 0;
    drawGain(fake::tickMs);
    EXPECT(game::drawn.empty());
    cfgGainPopup = 1;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() != nullptr);
}

TEST(hud_area_on_16_9_is_the_whole_screen)
{
    float x, y;
    hudToScreen(0, 0, 16.0f / 9.0f, 1.0f, &x, &y);
    EXPECT_NEAR(x, 0, 1e-5);
    EXPECT_NEAR(y, 0, 1e-5);
    hudToScreen(1, 1, 16.0f / 9.0f, 1.0f, &x, &y);
    EXPECT_NEAR(x, 1, 1e-5);
    EXPECT_NEAR(y, 1, 1e-5);
}

TEST(hud_area_on_super_ultrawide_is_the_middle_half)
{
    float x, y;
    hudToScreen(0, 1, 32.0f / 9.0f, 1.0f, &x, &y);  // where the minimap sits
    EXPECT_NEAR(x, 0.25, 1e-5);
    hudToScreen(1, 1, 32.0f / 9.0f, 1.0f, &x, &y);
    EXPECT_NEAR(x, 0.75, 1e-5);
    hudToScreen(0, 0, 21.0f / 9.0f, 1.0f, &x, &y);  // 21:9
    EXPECT_NEAR(x, 0.5 - 0.5 * (16.0 / 21.0), 1e-5);
}

TEST(hud_area_shrinks_with_safe_zone)
{
    float x, y;
    hudToScreen(1, 1, 16.0f / 9.0f, 0.9f, &x, &y);
    EXPECT_NEAR(x, 0.95, 1e-5);
    EXPECT_NEAR(y, 0.95, 1e-5);
    hudToScreen(0.5f, 0.5f, 32.0f / 9.0f, 0.9f, &x, &y);
    EXPECT_NEAR(x, 0.5, 1e-5);
}

TEST(hud_area_uses_full_width_on_narrow_screens_and_survives_bad_values)
{
    float x, y;
    hudToScreen(1, 1, 4.0f / 3.0f, 1.0f, &x, &y);
    EXPECT_NEAR(x, 1, 1e-5);
    hudToScreen(1, 1, 0, 0, &x, &y);              // natives not ready: treat as 16:9, no safe zone
    EXPECT_NEAR(x, 1, 1e-5);
    hudToScreen(1, 1, NAN, 3.0f, &x, &y);
    EXPECT_NEAR(x, 1, 1e-5);
    EXPECT_NEAR(y, 1, 1e-5);
}

TEST(popup_follows_hud_on_super_ultrawide)
{
    resetMod();
    game::aspect = 32.0f / 9.0f;
    game::safeZone = 0.95f;
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    const game::Drawn* d = drawnPlus();
    EXPECT(d != nullptr);
    if (d) {
        EXPECT(d->x > 0.5f && d->x < 0.75f);  // bottom right of the middle 16:9 HUD area, not the screen edge
        EXPECT_NEAR(d->x, 0.5 + 0.45 * 0.95 * 0.5, 1e-4);
    }
}

TEST(training_popup_shows_on_training_gain)
{
    resetMod();
    startWith(40);
    addXp(2);
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnTrain() && drawnTrain()->text == "Chop Training +2");
    EXPECT(drawnPlus() == nullptr); // training alone does not show happiness
}

TEST(training_popup_adds_up_and_expires)
{
    resetMod();
    startWith(40);
    addXp(1);
    fake::tickMs += 1000;
    addXp(1);
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnTrain() && drawnTrain()->text == "Chop Training +2");
    game::drawn.clear();
    fake::tickMs += 2000;
    drawGain(fake::tickMs);
    EXPECT(drawnTrain() == nullptr);
    EXPECT(!g_trainPop.shown);
}

TEST(no_training_popup_for_zero_or_negative)
{
    resetMod();
    startWith(40);
    addXp(0);
    addXp(-3);
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnTrain() == nullptr);
}

TEST(happiness_and_training_popups_never_overlap)
{
    resetMod();
    startWith(40);
    // Training first, happiness a second later: the worst case, as the
    // training line has floated up as far as it goes when happiness appears.
    addXp(2);
    fake::tickMs += 1000;
    addHappiness(3, "fetch");
    for (int ms = 0; ms < 2000; ms += 50) {
        fake::tickMs += 50;
        game::drawn.clear();
        drawGain(fake::tickMs);
        const game::Drawn* h = drawnPlus();
        const game::Drawn* tr = drawnTrain();
        if (h && tr) EXPECT(h->y - tr->y >= 0.03f); // training stays clearly above happiness
    }
}

TEST(fetch_shows_both_popups)
{
    resetMod();
    startWith(40);
    game::anims.insert(std::to_string(game::kChop) + "|fetch_drop");
    fake::tickMs += 250;
    g_fakeWallTime += 1;
    tick(0.25f);
    fake::tickMs += 200;
    game::drawn.clear();
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && drawnPlus()->text == "Chop Happiness +3");
    EXPECT(drawnTrain() && drawnTrain()->text == "Chop Training +2");
    if (drawnPlus() && drawnTrain()) {
        EXPECT(drawnTrain()->y < drawnPlus()->y);
        EXPECT_NEAR(drawnTrain()->x, drawnPlus()->x, 1e-5);
    }
}

TEST(popup_switch_hides_training_too)
{
    resetMod();
    startWith(40);
    addXp(2);
    cfgGainPopup = 0;
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnTrain() == nullptr);
    cfgGainPopup = 1;
}

TEST(popup_position_comes_from_ini)
{
    resetMod();
    fake::setIni(kIni, "Hud", "GainX", "0.5");
    fake::setIni(kIni, "Hud", "GainY", "0.25");
    loadSettings();
    startWith(40);
    addHappiness(5, "test");
    fake::tickMs += 200;
    drawGain(fake::tickMs);
    EXPECT(drawnPlus() && fabs(drawnPlus()->x - 0.5f) < 1e-4 && drawnPlus()->y <= 0.25f);
}

// =================================================================================
// Settings and saved progress
// =================================================================================
TEST(settings_defaults)
{
    resetMod();
    EXPECT(cfgEnabled == 1 && cfgKey == VK_F9 && cfgNotify == 1 && cfgGainPopup == 1);
    EXPECT_NEAR(gainWalkStart, 5, 1e-6);
    EXPECT_NEAR(gainFetch, 3, 1e-6);
    EXPECT_NEAR(gainPet, 5, 1e-6);
    EXPECT_NEAR(xpNeeded[0], 20, 1e-6);
    EXPECT_NEAR(xpNeeded[3], 200, 1e-6);
}

TEST(settings_are_read_and_clamped)
{
    resetMod();
    fake::setIni(kIni, "Happiness", "Pet", "12");
    fake::setIni(kIni, "Happiness", "Fetch", "500");   // above the 100 limit
    fake::setIni(kIni, "Happiness", "Trick", "-3");    // below 0
    fake::setIni(kIni, "Hud", "GainPopup", "0");
    fake::setIni(kIni, "Training", "NeededForPaw", "10"); // lower than Sit: raised to Sit's
    loadSettings();
    EXPECT_NEAR(gainPet, 12, 1e-6);
    EXPECT_NEAR(gainFetch, 100, 1e-6);
    EXPECT_NEAR(gainTrick, 0, 1e-6);
    EXPECT(cfgGainPopup == 0);
    EXPECT_NEAR(xpNeeded[1], 20, 1e-6);
    EXPECT(xpNeeded[2] >= xpNeeded[1] && xpNeeded[3] >= xpNeeded[2]);
}

TEST(iniFloat_falls_back_on_garbage)
{
    resetMod();
    fake::setIni(kIni, "X", "Nan", "nan");
    fake::setIni(kIni, "X", "Text", "abc"); // atof gives 0
    EXPECT_NEAR(iniFloat(kIni, "X", "Nan", 7, 0, 10), 7, 1e-6);
    EXPECT_NEAR(iniFloat(kIni, "X", "Text", 7, 0, 10), 0, 1e-6);
    EXPECT_NEAR(iniFloat(kIni, "X", "Missing", 7, 0, 10), 7, 1e-6);
}

TEST(state_round_trips)
{
    resetMod();
    g_xp = 61.5f;
    g_happy = 72.25f;
    g_collar = 2;
    g_stateDirty = true;
    saveState();
    EXPECT(!g_stateDirty);
    g_xp = 0;
    g_happy = 0;
    g_collar = -1;
    g_haveHappy = false;
    loadState();
    EXPECT_NEAR(g_xp, 61.5, 0.01);
    EXPECT_NEAR(g_happy, 72.25, 0.01);
    EXPECT(g_haveHappy);
    EXPECT(g_collar == 2);
}

TEST(state_not_written_when_clean)
{
    resetMod();
    g_stateDirty = false;
    saveState();
    EXPECT(fake::getIni(kState, "State", "Happiness") == nullptr);
}

TEST(bad_state_values_are_repaired)
{
    resetMod();
    fake::setIni(kState, "State", "TrainingXP", "-5");
    fake::setIni(kState, "State", "Happiness", "250");
    fake::setIni(kState, "State", "Collar", "9");
    loadState();
    EXPECT_NEAR(g_xp, 0, 1e-6);
    EXPECT_NEAR(g_happy, 100, 1e-6);
    EXPECT(g_collar == -1);
}

TEST(missing_state_means_take_happiness_from_game)
{
    resetMod();
    loadState();
    EXPECT(!g_haveHappy);
    game::putBlock(kDefaultBlock, 47);
    tick(0);
    EXPECT(g_haveHappy);
    EXPECT_NEAR(g_happy, 47, 1e-4);
}

// =================================================================================
// Finding Chop's saved data
// =================================================================================
TEST(finds_block_at_default_location)
{
    resetMod();
    game::putBlock(kDefaultBlock);
    EXPECT(locate());
    EXPECT(g_block == kDefaultBlock);
}

TEST(finds_block_that_moved)
{
    resetMod();
    game::putBlock(kDefaultBlock + 1234);
    EXPECT(locate());
    EXPECT(g_block == kDefaultBlock + 1234);
}

TEST(prefers_candidate_nearest_default)
{
    resetMod();
    game::putBlock(1000);
    game::putBlock(kDefaultBlock + 500);
    EXPECT(locate());
    EXPECT(g_block == kDefaultBlock + 500);
}

TEST(ignores_block_with_wrong_values)
{
    resetMod();
    game::putBlock(kDefaultBlock, 500); // happiness out of range
    EXPECT(!locate());
    game::putBlock(kDefaultBlock, 50, 9); // impossible trick level
    EXPECT(!locate());
}

TEST(no_block_found_in_empty_memory)
{
    resetMod();
    EXPECT(!locate());
    EXPECT(g_block == -1);
}

TEST(globals_not_ready_yet)
{
    resetMod();
    fake::regions.clear(); // VirtualQuery fails
    game::putBlock(kDefaultBlock);
    EXPECT(!locate());
}

TEST(configured_global_index_is_used)
{
    resetMod();
    game::putBlock(5000);
    game::putBlock(kDefaultBlock);
    cfgGlobalIndex = 5000;
    EXPECT(locate());
    EXPECT(g_block == 5000);
    cfgGlobalIndex = 0;
}

TEST(block_lost_is_found_again)
{
    resetMod();
    game::putBlock(kDefaultBlock);
    EXPECT(locate());
    game::globals[kDefaultBlock] = 0; // signature broken, e.g. new save loaded elsewhere
    game::putBlock(kDefaultBlock + 40);
    EXPECT(locate());
    EXPECT(g_block == kDefaultBlock + 40);
}

TEST(not_ready_online)
{
    resetMod();
    game::putBlock(kDefaultBlock);
    game::online = true;
    EXPECT(!ready());
}

TEST(insane_data_warns_once)
{
    resetMod();
    game::putBlock(kDefaultBlock);
    cfgGlobalIndex = kDefaultBlock; // forced index skips the sanity filter of the scan
    game::globals[kDefaultBlock + kOffValid] = 7;
    EXPECT(!ready());
    EXPECT(!ready());
    int warnings = 0;
    for (auto& m : game::feed)
        if (m.find("unexpected data") != std::string::npos) ++warnings;
    EXPECT(warnings == 1);
    cfgGlobalIndex = 0;
}

// =================================================================================
// Watching what the player does (tick)
// =================================================================================
static void step(float seconds, float moveMetres = 0)
{
    fake::tickMs += (ULONGLONG)(seconds * 1000);
    g_fakeWallTime += (long long)seconds;
    game::playerPos[0] += moveMetres;
    tick(seconds);
}

TEST(first_tick_announces_and_applies)
{
    resetMod();
    game::putBlock(kDefaultBlock, 10);
    g_happy = 70;
    g_haveHappy = true;
    tick(0);
    EXPECT(g_live);
    EXPECT(feedHas("Chop is happy (70/100)"));
    EXPECT_NEAR(game::blockHappiness(kDefaultBlock), 70, 1e-4);
    game::feed.clear();
    tick(0);
    EXPECT(game::feed.empty()); // only once
}

TEST(petting_gives_happiness_once_per_pet)
{
    resetMod();
    startWith(40);
    game::anims.insert(std::to_string(game::kPlayer) + "|petting_franklin");
    step(0.25f);
    EXPECT_NEAR(g_happy, 45, 1e-4);
    step(0.25f); // still the same pet animation
    EXPECT_NEAR(g_happy, 45, 1e-4);
    EXPECT(g_happyPop.shown);
}

TEST(fetch_gives_happiness_and_training)
{
    resetMod();
    startWith(40);
    game::anims.insert(std::to_string(game::kChop) + "|fetch_drop");
    step(0.25f);
    EXPECT_NEAR(g_happy, 43, 1e-4);
    EXPECT_NEAR(g_xp, 2, 1e-4);
}

TEST(trick_gives_happiness_and_training)
{
    resetMod();
    startWith(40);
    game::anims.insert(std::to_string(game::kChop) + "|sit_loop");
    step(0.25f);
    EXPECT_NEAR(g_happy, 41, 1e-4);
    EXPECT_NEAR(g_xp, 1, 1e-4);
}

TEST(walking_together_earns_per_minute)
{
    resetMod();
    startWith(40);
    step(0.25f, 0); // first position
    for (int i = 0; i < 240; ++i) step(0.25f, 1.0f); // 60 s at 4 m/s
    EXPECT_NEAR(g_happy, 42, 1e-3);
    EXPECT_NEAR(g_xp, 1, 1e-3);
}

TEST(standing_still_with_chop_earns_nothing)
{
    resetMod();
    startWith(40);
    for (int i = 0; i < 300; ++i) step(0.25f, 0);
    EXPECT_NEAR(g_happy, 40, 1e-3);
}

TEST(chop_getting_killed_costs_happiness_once)
{
    resetMod();
    startWith(60);
    step(0.25f);
    game::chopDead = true;
    step(0.25f);
    EXPECT_NEAR(g_happy, 35, 1e-4);
    step(0.25f);
    EXPECT_NEAR(g_happy, 35, 1e-4);
}

TEST(time_apart_slowly_lowers_happiness)
{
    resetMod();
    startWith(60);
    game::chopExists = false;
    for (int i = 0; i < 10 * 60 * 4; ++i) step(0.25f);
    EXPECT_NEAR(g_happy, 59, 1e-3);
}

TEST(walk_start_bonus_from_game_counter)
{
    resetMod();
    game::walkStat = 3;
    startWith(40); // the first read only remembers the counter
    step(0.25f);
    EXPECT_NEAR(g_happy, 40, 1e-4);
    game::walkStat = 4;
    step(0.25f);
    EXPECT_NEAR(g_happy, 45, 1e-4);
}

TEST(nothing_happens_without_chops_data)
{
    resetMod();
    g_happy = 40;
    g_haveHappy = true;
    game::anims.insert(std::to_string(game::kPlayer) + "|petting_franklin");
    tick(0.25f);
    EXPECT(!g_live);
    EXPECT_NEAR(g_happy, 40, 1e-4);
}

// =================================================================================
// Keys and loading
// =================================================================================
TEST(status_message)
{
    resetMod();
    startWith(50, 30);
    showStatus();
    EXPECT(feedHas("Chop: content (50/100). Tricks 1/4, next: Paw at 30/60 training."));
    game::feed.clear();
    g_xp = 500;
    showStatus();
    EXPECT(feedHas("Knows all 4 tricks"));
}

TEST(status_without_data)
{
    resetMod();
    showStatus();
    EXPECT(feedHas("data not found"));
}

TEST(collar_cycles_through_five)
{
    resetMod();
    startWith(50);
    g_collarProgress.unlocked = 31;
    game::globals[kDefaultBlock + kOffCollar] = 3;
    cycleCollar();
    EXPECT(g_collar == 4);
    cycleCollar();
    EXPECT(g_collar == 0);
    EXPECT(game::globals[kDefaultBlock + kOffCollar] == 0);
}

TEST(key_only_counts_when_game_focused)
{
    resetMod();
    fake::keys[VK_F9] = true;
    EXPECT(down(VK_F9) && gameFocused());
    fake::focused = false;
    EXPECT(!gameFocused());
}

TEST(dll_main_sets_folder_and_clears_log)
{
    resetMod();
    // A Windows-style folder under /tmp, so the log DllMain writes lands there.
    fake::modulePath = "/tmp/chopagotchi-test/GTAV\\Chopagotchi.asi";
    char saved[MAX_PATH];
    snprintf(saved, sizeof(saved), "%s", g_dir);
    DllMain(nullptr, DLL_PROCESS_ATTACH, nullptr);
    EXPECT(strcmp(g_dir, "/tmp/chopagotchi-test/GTAV\\") == 0);
    EXPECT(!fake::deletedFiles.empty() && fake::deletedFiles[0] == "/tmp/chopagotchi-test/GTAV\\Chopagotchi.log");
    snprintf(g_dir, sizeof(g_dir), "%s", saved);
}

// =================================================================================
// Phone app
// =================================================================================
static void resetPhone()
{
    g_app = phoneapp::App();
    g_appNote[0] = 0;
    g_appNoteUntil = 0;
    loadSettings();
    game::controls.clear();
    game::phoneUp = false;
    game::inVehicle = false;
    game::phoneScale = -1;
    g_phoneShrunk = false;
    game::aspect = 16.0f / 9.0f;
    game::safeZone = 1.0f;
    game::chopOffset[0] = game::chopOffset[1] = game::chopOffset[2] = 0;
    game::tasks.clear();
}

static bool drawnHas(const char* part)
{
    for (auto& d : game::drawn)
        if (d.text.find(part) != std::string::npos) return true;
    return false;
}

// Presses one of the phone's controls for a frame and draws the next frame.
static void phoneKey(int control)
{
    game::controls = {control};
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    game::controls.clear();
    game::drawn.clear();
    phoneFrame(fake::tickMs);
}
static const int kUp = 172, kDown = 173, kSelect = 176, kBack = 177;

static void openPhone()
{
    phonePress(phoneapp::KeyApp);
    game::drawn.clear();
    phoneFrame(fake::tickMs);
}

TEST(phone_app_opens_on_home_with_mood)
{
    resetMod();
    resetPhone();
    startWith(72);
    openPhone();
    EXPECT(g_app.open);
    EXPECT(drawnHas("Chopagotchi"));
    EXPECT(drawnHas("Chop is happy"));
    EXPECT(drawnHas("72/100"));
    for (const char* item : {"Overview", "Cooldowns", "Collar", "Whistle for Chop", "Settings"}) EXPECT(drawnHas(item));
    phonePress(phoneapp::KeyApp);  // the key again closes it
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(!g_app.open);
    EXPECT(game::drawn.empty());
}

TEST(phone_app_backspace_closes_from_home)
{
    resetMod();
    resetPhone();
    startWith(50);
    openPhone();
    phoneKey(kBack);
    EXPECT(!g_app.open);
}

TEST(phone_app_switches_off_game_controls_while_open)
{
    resetMod();
    resetPhone();
    startWith(50);
    game::disabled.clear();
    phoneFrame(fake::tickMs);
    EXPECT(game::disabled.empty());  // closed: the game keeps its controls
    openPhone();
    for (int c : {27, 172, 173, 176, 177}) EXPECT(game::disabled.count(c));
}

TEST(phone_app_overview_shows_progress)
{
    resetMod();
    resetPhone();
    startWith(50, 90);  // content, Sit + Paw, halfway to Beg
    openPhone();
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Overview);
    EXPECT(drawnHas("Content  50/100"));
    EXPECT(drawnHas("2 of 4"));
    EXPECT(drawnHas("Sit, Paw"));
    EXPECT(drawnHas("Beg at 120"));
    EXPECT(drawnHas("With you"));
    game::chopExists = false;
    tick(0.25f);
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(drawnHas("At home"));
    phoneKey(kBack);
    EXPECT(g_app.open && g_app.page == phoneapp::Home);
}

TEST(phone_app_cooldowns_show_time_left)
{
    resetMod();
    resetPhone();
    startWith(50, 25);  // knows Sit only
    g_cd.fetchReadyAt = g_fakeWallTime + 185;
    g_cd.lastWalkGameMinute = gameMinutes() - 60;
    openPhone();
    phoneKey(kDown);
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Timers);
    EXPECT(drawnHas("3m 05s"));      // fetch
    EXPECT(drawnHas("23h 00m"));     // walk bonus, in-game time
    EXPECT(drawnHas("Ready"));       // Sit, petting
    EXPECT(drawnHas("Not learned")); // Paw, Beg
}

TEST(phone_app_sets_collar)
{
    resetMod();
    resetPhone();
    startWith(50);
    g_collarProgress.unlocked = 31;
    game::globals[kDefaultBlock + kOffCollar] = 2;
    openPhone();
    phoneKey(kDown);
    phoneKey(kDown);
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Collars);
    EXPECT(g_app.cursor == 2);  // starts on the collar he wears
    phoneKey(kDown);
    phoneKey(kSelect);
    EXPECT(g_collar == 3);
    EXPECT(g_stateDirty);
    EXPECT(game::globals[kDefaultBlock + kOffCollar] == 3);
    EXPECT(game::globals[kDefaultBlock + kOffUsedApp] == 1);
    EXPECT(drawnHas("Wearing"));
}

TEST(phone_app_settings_toggle_and_save)
{
    resetMod();
    resetPhone();
    startWith(50);
    openPhone();
    phoneKey(kUp);  // wraps to Settings
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Settings);
    phoneKey(kDown);
    phoneKey(kSelect);  // Messages off
    EXPECT(cfgNotify == 0);
    EXPECT(fake::getIni(kIni, "Settings", "Notifications") && std::string(fake::getIni(kIni, "Settings", "Notifications")) == "0");
    phoneKey(kDown);
    phoneKey(kSelect);  // happiness popup off
    EXPECT(cfgGainPopup == 0);
    EXPECT(std::string(fake::getIni(kIni, "Hud", "GainPopup")) == "0");
    phoneKey(kDown);
    phoneKey(kSelect);  // mod off, and the app still works to switch it back on
    EXPECT(cfgEnabled == 0);
    phoneKey(kSelect);
    EXPECT(cfgEnabled == 1);
    EXPECT(std::string(fake::getIni(kIni, "Settings", "Enabled")) == "1");
}

static void whistle()
{
    g_app.open = true;
    g_app.page = phoneapp::Home;
    g_app.cursor = phoneapp::ItemWhistle;
    phoneKey(kSelect);
}

TEST(phone_app_whistle)
{
    resetMod();
    resetPhone();
    startWith(50);
    game::chopOffset[0] = 30;
    whistle();
    EXPECT(game::tasks.size() == 1 && game::tasks[0] == game::kChop);
    EXPECT(drawnHas("Chop comes running"));

    game::tasks.clear();
    game::chopOffset[0] = 1;
    whistle();
    EXPECT(game::tasks.empty());
    EXPECT(drawnHas("right beside you"));

    game::chopOffset[0] = 500;
    whistle();
    EXPECT(game::tasks.empty());
    EXPECT(drawnHas("too far"));

    game::chopOffset[0] = 30;
    game::inVehicle = true;
    whistle();
    EXPECT(game::tasks.empty());
    EXPECT(drawnHas("car"));
    game::inVehicle = false;

    game::chopExists = false;
    tick(0.25f);
    whistle();
    EXPECT(game::tasks.empty());
    EXPECT(drawnHas("isn't with you"));

    // The note fades after a few seconds.
    fake::tickMs += 4000;
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(!drawnHas("isn't with you"));
}

TEST(phone_app_hint_shows_over_real_phone)
{
    resetMod();
    resetPhone();
    startWith(50);
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(!drawnHas("Chopagotchi"));
    game::phoneUp = true;
    phoneFrame(fake::tickMs);
    EXPECT(drawnHas("Chopagotchi  [F9]"));
    fake::setIni(kIni, "Phone", "Hint", "0");
    loadSettings();
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(game::drawn.empty());
}

static float drawnX(const char* part)
{
    for (auto& d : game::drawn)
        if (d.text.find(part) != std::string::npos) return d.x;
    return -1;
}

TEST(phone_app_sits_by_the_phone_on_any_screen)
{
    resetMod();
    resetPhone();
    startWith(50);
    game::phoneUp = true;
    auto hintX = []() { game::drawn.clear(); phoneFrame(fake::tickMs); return drawnX("Chopagotchi  [F9]"); };
    EXPECT_NEAR(hintX(), 0.925, 1e-4);                // 16:9
    for (auto& d : game::drawn)                       // just above the phone's top (0.607)
        if (d.text == "Chopagotchi  [F9]") EXPECT_NEAR(d.y, 0.607 - 0.034 + 0.002, 1e-3);
    game::aspect = 32.0f / 9.0f;                      // super ultrawide: HUD in the middle half
    EXPECT_NEAR(hintX(), 0.25 + 0.925 * 0.5, 1e-4);
    game::aspect = 21.0f / 9.0f;
    EXPECT_NEAR(hintX(), 0.5 - (16.0 / 21) / 2 + 0.925 * (16.0 / 21), 1e-4);
    game::aspect = 16.0f / 9.0f;
    game::safeZone = 0.9f;                            // smaller safe zone pulls it inwards
    EXPECT_NEAR(hintX(), 0.05 + 0.925 * 0.9, 1e-4);
    // The app itself follows the same anchor.
    game::aspect = 32.0f / 9.0f;
    game::safeZone = 1.0f;
    game::phoneUp = false;
    openPhone();
    EXPECT_NEAR(drawnX("Chopagotchi"), 0.7125, 1e-4);
}

TEST(phone_app_replaces_the_real_phone)
{
    resetMod();
    resetPhone();
    startWith(50);
    openPhone();                       // phone not out: leave it alone
    EXPECT(game::phoneScale == -1);
    phonePress(phoneapp::KeyApp);
    phoneFrame(fake::tickMs);
    EXPECT(game::phoneScale == -1);

    game::phoneUp = true;              // phone out: the app hides it while open
    openPhone();
    EXPECT(game::phoneScale == 0);
    EXPECT(drawnHas("Overview"));
    EXPECT(!drawnHas("[F9]"));         // no hint tab over the app
    game::phoneScale = 500;            // the game resets it: hidden again next frame
    phoneFrame(fake::tickMs);
    EXPECT(game::phoneScale == 0);
    phoneKey(kBack);                   // closing brings the phone back
    EXPECT(!g_app.open);
    EXPECT(game::phoneScale == 500);
    game::phoneScale = -1;
    phoneFrame(fake::tickMs);
    EXPECT(game::phoneScale == -1);    // and stops touching it
}

TEST(phone_app_hidden_while_paused)
{
    resetMod();
    resetPhone();
    startWith(50);
    openPhone();
    game::paused = true;
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Home);  // input ignored
    EXPECT(game::drawn.empty());
    game::paused = false;
    phoneFrame(fake::tickMs);
    EXPECT(drawnHas("Chopagotchi"));
}

TEST(phone_app_can_be_switched_off)
{
    resetMod();
    fake::setIni(kIni, "Phone", "App", "0");
    resetPhone();
    startWith(50);
    phonePress(phoneapp::KeyApp);
    game::drawn.clear();
    game::phoneUp = true;
    phoneFrame(fake::tickMs);
    EXPECT(game::drawn.empty());
}

// =================================================================================
// The app on Franklin's real phone
// =================================================================================
// The fake game keeps a phone the way the Enhanced 1158.13 scripts lay it out:
// the app table at 9509 (37 rows) and the phone's state after the sound set
// label at 21655. rules/RealPhone.h has its own, more thorough tests in
// tests/test_real_phone.cpp; these check that the mod is wired to it.
namespace realfake {
const int kTable = 9509, kRows = 37, kAnchor = 21655, kMovie = 5;
int rowAt(int r, int field) { return kTable + 1 + r * 15 + field; }
int mapAt(int cell) { return kTable + 1 + kRows * 15 + 1 + cell; }
int usedAt(int cell) { return mapAt(0) + kRows + 1 + cell; }
void label(int i, const char* text)
{
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%s", text);
    memcpy(&game::globals[i], buf, 32);
}
void app(int r, int cell, const char* script)
{
    game::globals[rowAt(r, 4)] = (uint32_t)cell;
    label(rowAt(r, 5), script);
    game::globals[rowAt(r, 9)] = joaat(script);
    game::globals[rowAt(r, 11)] = 1;
}
void state(int s) { game::globals[kAnchor + 12] = (uint32_t)s; }
// Story mode, phone out on its home screen.
void phoneOut()
{
    game::globals[kTable] = kRows;
    game::globals[mapAt(0) - 1] = kRows;
    game::globals[usedAt(0) - 1] = kRows;
    game::globals[usedAt(0) + kRows] = 1;  // page
    for (int r = 0; r < kRows; ++r) game::globals[rowAt(r, 4)] = (uint32_t)-99;
    app(0, 4, "appContacts");
    app(1, 1, "appTextMessage");
    app(3, 6, "appCamera");
    app(4, 2, "appChecklist");
    app(7, 0, "appEmail");
    app(20, 3, "appSettings");
    app(10, 5, "appSettings");
    app(2, 7, "appInternet");
    label(kAnchor, "Phone_SoundSet_Franklin");
    game::globals[kAnchor + 11] = 1;
    game::globals[kAnchor - 8] = kMovie;
    game::globals[kAnchor - 9] = 4;
    state(6);
}
void pressIcon()
{
    game::globals[kAnchor + 14] = (uint32_t)g_real.row();
    game::globals[kTable - 7] |= 1u << 23;
    state(7);
}
bool movieHas(const char* part)
{
    for (auto& c : game::movieCalls)
        if (c.find(part) != std::string::npos) return true;
    return false;
}
void key(int control)
{
    game::controls = {control};
    phoneFrame(fake::tickMs);
    game::controls.clear();
}
// Select on the Checklist icon, and its script up and running.
void openChecklist()
{
    game::globals[kAnchor + 14] = 4;
    game::scriptThreads[joaat("appchecklist")] = 1;
    state(7);
}
// The tests further down go in through the icon on the home screen unless
// they ask for the row inside the Checklist app, which is the default.
void reset(bool icon = true)
{
    if (icon) fake::setIni(kIni, "Phone", "InChecklist", "0");
    game::listSelection = 0;
    game::scriptThreads.clear();
    game::endedScripts.clear();
    game::requestedScripts.clear();
    game::releasedScripts.clear();
    g_real = realphone::Phone();
    g_realApp = phoneapp::App();
    g_realScroll = 0;
    g_phoneShrunk = false;
    g_app = phoneapp::App();
    g_appNote[0] = 0;
    g_appNoteUntil = 0;
    game::movieCalls.clear();
    game::controls.clear();
    game::phoneUp = false;
    game::playerModel = "player_one";
    loadSettings();
}
} // namespace realfake

TEST(real_phone_row_inside_checklist)
{
    resetMod();
    realfake::reset(false);
    startWith(72, 65);
    realfake::phoneOut();
    game::phoneUp = true;
    game::drawn.clear();
    std::vector<uint64_t> before = game::globals;
    phoneFrame(fake::tickMs);
    EXPECT(cfgInChecklist == 1);       // the default
    EXPECT(g_real.onPhone());
    EXPECT(!drawnHas("[F9]"));
    EXPECT(game::globals == before);   // no icon: nothing is written to the phone's data
    EXPECT(game::movieCalls.empty());

    realfake::openChecklist();
    for (int i = 0; i < 4; ++i) phoneFrame(fake::tickMs);
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,18,2,0,'Chopagotchi')"));
    EXPECT(!g_real.open());
    game::listSelection = 2;           // our row
    realfake::key(176);
    EXPECT(realfake::movieHas("GET_CURRENT_SELECTION(5)"));
    phoneFrame(fake::tickMs);
    EXPECT(g_real.open());
    EXPECT(game::endedScripts == std::vector<std::string>{"appChecklist"});
    EXPECT(game::globals[realfake::kAnchor + 12] == 8);
    EXPECT(realfake::movieHas("SET_HEADER(5,'Chopagotchi')"));
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,18,0,0,'Overview')"));
    EXPECT(!g_app.open);

    realfake::key(176);                // Select: Overview
    EXPECT(g_realApp.page == phoneapp::Overview);
    EXPECT(realfake::movieHas("'Mood: Happy (72/100)'"));
    realfake::key(177);                // Back: our first screen
    EXPECT(g_realApp.page == phoneapp::Home);
    EXPECT(g_real.open());
    realfake::key(177);                // Back again: the Checklist app
    EXPECT(!g_real.open());
    EXPECT(!g_realApp.open);
    EXPECT(game::globals[realfake::kAnchor + 12] == 7);
    EXPECT((game::globals[realfake::kTable - 7] & (1u << 23)) != 0);
    EXPECT(game::requestedScripts == std::vector<std::string>{"appChecklist"});
}

TEST(real_phone_icon_and_pages)
{
    resetMod();
    realfake::reset();
    startWith(72, 65);
    phoneFrame(fake::tickMs);
    EXPECT(!g_real.onPhone());         // no phone data yet: nothing happens
    EXPECT(game::movieCalls.empty());

    realfake::phoneOut();
    fake::tickMs += 3000;
    phoneFrame(fake::tickMs);
    EXPECT(g_real.onPhone());
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,1,8,14,0,'Chopagotchi',255)"));
    EXPECT(game::globals[realfake::mapAt(8)] == (uint64_t)g_real.row());

    realfake::pressIcon();
    phoneFrame(fake::tickMs);
    EXPECT(g_real.open());
    EXPECT((game::globals[realfake::kTable - 7] & (1u << 23)) == 0);
    EXPECT(realfake::movieHas("SET_HEADER(5,'Chopagotchi')"));
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,18,0,0,'Overview')"));
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,18,3,0,'Whistle for Chop')"));
    EXPECT(!g_app.open);               // the F9 app stays out of it

    realfake::key(176);                // Select: Overview
    EXPECT(g_realApp.page == phoneapp::Overview);
    EXPECT(realfake::movieHas("SET_HEADER(5,'Overview')"));
    EXPECT(realfake::movieHas("'Mood: Happy (72/100)'"));
    EXPECT(realfake::movieHas("'Knows: Sit, Paw'"));
    EXPECT(game::globals[realfake::kAnchor + 12] == 8);
    realfake::key(173);                // Down scrolls the information
    EXPECT(g_realScroll == 1);
    EXPECT(realfake::movieHas("DISPLAY_VIEW(5,18,1)"));
    realfake::key(177);                // Back: the app's first screen
    EXPECT(g_realApp.page == phoneapp::Home);
    EXPECT(g_real.open());
    phoneFrame(fake::tickMs);
    EXPECT(game::globals[realfake::kAnchor + 12] == 7);
}

TEST(real_phone_actions)
{
    resetMod();
    realfake::reset();
    startWith(50);
    g_collarProgress.unlocked = 31;
    realfake::phoneOut();
    phoneFrame(fake::tickMs);
    realfake::pressIcon();
    phoneFrame(fake::tickMs);

    realfake::key(173);
    realfake::key(173);
    realfake::key(176);                // Collar
    EXPECT(g_realApp.page == phoneapp::Collars);
    realfake::key(173);
    realfake::key(176);                // second collar
    EXPECT(g_collar == 1);
    EXPECT(realfake::movieHas("'Collar 2 (wearing)'"));
    realfake::key(177);
    phoneFrame(fake::tickMs);

    game::feed.clear();
    game::chopOffset[0] = 30;
    realfake::key(173);                // Whistle for Chop
    realfake::key(176);
    EXPECT(game::tasks.size() == 1);
    EXPECT(feedHas("Chop comes running"));
    EXPECT(g_real.open());

    realfake::key(173);
    realfake::key(176);                // Settings
    EXPECT(g_realApp.page == phoneapp::Settings);
    realfake::key(173);
    realfake::key(176);                // Messages off
    EXPECT(cfgNotify == 0);
    EXPECT(realfake::movieHas("'Messages: Off'"));
    game::chopOffset[0] = 0;
    game::tasks.clear();
}

TEST(real_phone_closes_with_the_phone_and_hides_the_hint)
{
    resetMod();
    realfake::reset();
    startWith(50);
    realfake::phoneOut();
    game::phoneUp = true;
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(!drawnHas("[F9]"));         // the icon is on the phone: no hint tab needed
    realfake::pressIcon();
    phoneFrame(fake::tickMs);
    EXPECT(g_real.open());
    appKeyPressed();               // F9 while the in-phone app is up does nothing
    EXPECT(!g_app.open);
    realfake::state(6);                // Back on the first screen: the phone goes home
    phoneFrame(fake::tickMs);
    EXPECT(!g_real.open());
    EXPECT(!g_realApp.open);
    appKeyPressed();               // and F9 opens its own app again
    EXPECT(g_app.open);
}

TEST(real_phone_only_for_franklin_and_can_be_switched_off)
{
    resetMod();
    realfake::reset();
    startWith(50);
    realfake::phoneOut();
    game::playerModel = "player_zero"; // Michael
    phoneFrame(fake::tickMs);
    EXPECT(!g_real.onPhone());
    EXPECT(game::movieCalls.empty());
    game::phoneUp = true;
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(drawnHas("[F9]"));          // the fallback hint is back
    game::playerModel = "player_one";
    phoneFrame(fake::tickMs);
    EXPECT(g_real.onPhone());

    fake::setIni(kIni, "Phone", "InPhone", "0");
    loadSettings();
    phoneFrame(fake::tickMs);
    EXPECT(!g_real.onPhone());
    EXPECT(game::globals[realfake::rowAt(g_real.row(), 4)] == (uint32_t)-99);
    fake::setIni(kIni, "Phone", "InPhone", "1");
    fake::setIni(kIni, "Phone", "Icon", "40");
    fake::setIni(kIni, "Phone", "Cell", "0");
    loadSettings();
    game::movieCalls.clear();
    phoneFrame(fake::tickMs);
    EXPECT(realfake::movieHas("SET_DATA_SLOT(5,1,0,40,0,'Chopagotchi',255)"));
    fake::setIni(kIni, "Phone", "Icon", "999");  // no such icon: the default is used
    loadSettings();
    EXPECT(cfgPhoneIcon == 14);
}

TEST(real_phone_left_alone_when_the_data_does_not_check_out)
{
    resetMod();
    realfake::reset();
    startWith(50);
    realfake::phoneOut();
    game::globals[realfake::rowAt(1, 9)] = 1;   // not the table the game builds
    std::vector<uint64_t> before = game::globals;
    game::phoneUp = true;
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(game::globals == before);
    EXPECT(game::movieCalls.empty());
    EXPECT(drawnHas("[F9]"));
}

// =================================================================================
// Earning collars
// =================================================================================
TEST(collars_start_with_only_the_first)
{
    resetMod();
    startWith(50);
    game::globals[kDefaultBlock + kOffCollar] = 0;
    cycleCollar();
    EXPECT(g_collar == -1);  // nothing else to wear yet
    EXPECT(feedHas("no other collar yet"));
    EXPECT(!feedHas("walks"));  // what earns the next one is not given away
}

TEST(collars_all_available_when_locking_is_off)
{
    resetMod();
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Collars", "Locked", "0");
    loadSettings();
    startWith(50);
    game::globals[kDefaultBlock + kOffCollar] = 3;
    cycleCollar();
    EXPECT(g_collar == 4);
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Collars", "Locked", "1");
}

TEST(collar_two_for_walks)
{
    resetMod();
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Collars", "Walks", "2");
    loadSettings();
    game::walkStat = 0;
    startWith(40);
    game::walkStat = 1;
    step(0.25f);
    EXPECT(g_collarProgress.walks == 1);
    EXPECT(!feedHas("New collar"));
    game::walkStat = 2;  // a second walk the same day: no bonus, and it does not count
    step(0.25f);
    EXPECT(g_collarProgress.walks == 1);
    for (int i = 3; i < 12; ++i) {  // nor does calling and dismissing him over and over
        game::walkStat = i;
        step(0.25f);
    }
    EXPECT(g_collarProgress.walks == 1);
    EXPECT(!feedHas("New collar"));
    game::day += 1;      // the next in-game day
    game::walkStat = 12;
    step(0.25f);
    EXPECT(g_collarProgress.walks == 2);
    EXPECT(feedHas("New collar for Chop: Collar 2, for 2 walks together!"));
    cycleCollar();
    EXPECT(g_collar == 1);
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Collars", "Walks", "10");
}

TEST(collar_three_for_fetches_even_on_cooldown)
{
    resetMod();
    startWith(40);
    std::string ball = std::to_string(game::kChop) + "|fetch_drop";
    for (int i = 0; i < 50; ++i) {
        game::anims.insert(ball);
        step(0.25f);
        game::anims.erase(ball);
        step(0.25f);
    }
    EXPECT(g_collarProgress.fetches == 50);
    EXPECT(collars::isUnlocked(g_collarProgress, cfgCollars, 2));
    EXPECT(feedHas("Collar 3, for 50 fetches"));
}

TEST(collar_four_for_happy_days_of_play_in_a_row)
{
    resetMod();
    startWith(90);
    game::chopExists = false;
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Happiness", "MinutesApartPerPointLost", "0");
    loadSettings();
    // Sleeping the days away: the game's clock moves on, no time is played.
    for (int d = 0; d < 6; ++d) {
        game::day = 17 + d;
        step(0.25f);
    }
    EXPECT(g_collarProgress.bondStreak == 0);
    EXPECT(!feedHas("New collar"));
    // Three in-game days of actual play (48 minutes each) with him happy.
    for (int i = 0; i < 3 * 48 * 60 - 60; ++i) step(1.0f);
    EXPECT(g_collarProgress.bondStreak == 2);
    EXPECT(!feedHas("New collar"));
    for (int i = 0; i < 60; ++i) step(1.0f);
    EXPECT(g_collarProgress.bondStreak == 3);
    EXPECT(feedHas("Collar 4, for 3 happy days in a row"));
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Happiness", "MinutesApartPerPointLost", "10");
}

TEST(collar_four_progress_is_saved_and_lost_on_a_dip)
{
    resetMod();
    startWith(90);
    game::chopExists = false;
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Happiness", "MinutesApartPerPointLost", "0");
    loadSettings();
    for (int i = 0; i < 50 * 60; ++i) step(1.0f);  // one day and two minutes
    EXPECT(g_collarProgress.bondStreak == 1);
    saveState();
    EXPECT(std::string(fake::getIni(kState, "Collars", "BondStreak")) == "1");
    EXPECT(atoi(fake::getIni(kState, "Collars", "BondSeconds")) >= 100);
    g_collarProgress = collars::Progress();
    loadState();
    EXPECT(g_collarProgress.bondStreak == 1);
    EXPECT(g_collarProgress.bondSeconds >= 100);
    g_happy = 70;                                  // he drops below 80
    step(1.0f);
    EXPECT(g_collarProgress.bondStreak == 0);
    EXPECT(g_collarProgress.bondSeconds == 0);
    fake::setIni("/tmp/chopagotchi-test/Chopagotchi.ini", "Happiness", "MinutesApartPerPointLost", "10");
}

TEST(collar_five_dug_up_on_a_walk)
{
    resetMod();
    startWith(40);
    g_roll = [] { return 0.001f; };
    step(0.25f, 0);
    for (int i = 0; i < 240; ++i) step(0.25f, 1.0f);  // one minute walking together
    EXPECT(collars::isUnlocked(g_collarProgress, cfgCollars, 4));
    EXPECT(feedHas("Chop dug something up on the walk: Collar 5!"));
}

TEST(collar_progress_kept_in_state_file)
{
    resetMod();
    g_collarProgress.walks = 4;
    g_collarProgress.unlocked = 1 | 4;
    g_stateDirty = true;
    saveState();
    EXPECT(std::string(fake::getIni(kState, "Collars", "Walks")) == "4");
    g_collarProgress = collars::Progress();
    loadState();
    EXPECT(g_collarProgress.walks == 4);
    EXPECT(g_collarProgress.unlocked == 5);
}

TEST(phone_app_shows_locked_collars_and_refuses_them)
{
    resetMod();
    resetPhone();
    startWith(50);
    g_collarProgress.walks = 3;
    game::globals[kDefaultBlock + kOffCollar] = 0;
    openPhone();
    phoneKey(kDown);
    phoneKey(kDown);
    phoneKey(kSelect);
    EXPECT(g_app.page == phoneapp::Collars);
    game::drawn.clear();
    phoneFrame(fake::tickMs);
    EXPECT(drawnHas("Locked"));
    EXPECT(!drawnHas("walks") && !drawnHas("fetches") && !drawnHas("days") && !drawnHas("dig"));
    phoneKey(kDown);
    phoneKey(kSelect);
    EXPECT(g_collar == -1);
    EXPECT(std::string(g_appNote) == "Collar 2 is locked.");
}

TEST(real_phone_shows_locked_collars)
{
    resetMod();
    realfake::reset();
    startWith(50);
    realfake::phoneOut();
    phoneFrame(fake::tickMs);
    realfake::pressIcon();
    phoneFrame(fake::tickMs);
    realfake::key(173);
    realfake::key(173);
    realfake::key(176);  // Collar
    phoneFrame(fake::tickMs);
    EXPECT(realfake::movieHas("'Collar 2 - Locked'"));
    EXPECT(realfake::movieHas("'Collar 5 - Locked'"));
    EXPECT(!realfake::movieHas("walks") && !realfake::movieHas("dig"));
}

int main()
{
    for (auto& t : allTests()) {
        g_current = t.name;
        int before = g_failures;
        t.fn();
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    return finish("test_mod");
}
