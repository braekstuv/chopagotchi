// Chopagotchi - a Script Hook V plugin for GTA V story mode.
//
// The iFruit app used to sync Chop's happiness, trick level and collar into a
// small block of the game's saved script data. With the app gone that block
// never changes, so Chop stays unhappy and knows no tricks. This plugin lets
// you earn those values in the game instead: walking Chop, playing fetch,
// practising tricks and petting him raise his happiness and teach him tricks,
// and his happiness fades again if you leave him at home.
//
// Layout of the saved block (from the decompiled chop / social_controller /
// main scripts, where it appears as Global_114963.f_20127 in Legacy 3889):
//   block + 0            int   = 3            (count of the car-app vehicle array)
//   block + 1            hash  = tailgater    (element 0, 43 slots each)
//   block + 44           hash  = buffalo2
//   block + 87           hash  = bodhi2
//   block + 130          int   = 3
//   block + 254          float happiness 0..100  (>66 happy, >33 content, else unhappy)
//   block + 257          int   training level 0..4 (sit, paw, beg, pet)
//   block + 258          int   collar 0..4
//   block + 259          bool  "app data valid" - must be 1 or Chop counts as unhappy
//   block + 263          bool  "used the app"   - must be 1 for the collar to apply
//
// The block's index moves between game builds, so it is located at run time by
// the array-length headers inside it rather than hard-coded. (The car hashes
// are not usable for that: the game clears or replaces them.)

#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "rules/Collars.h"
#include "rules/Cooldowns.h"
#include "rules/PhoneApp.h"
#include "rules/PhoneScreens.h"
#include "rules/RealPhone.h"

// ---- Script Hook V imports (mapped to the decorated export names in ScriptHookV.def) -------------------
extern "C" {
void shv_scriptRegister(HMODULE module, void (*scriptMain)());
void shv_scriptUnregister(HMODULE module);
void shv_scriptWait(DWORD ms);
void shv_nativeInit(uint64_t hash);
void shv_nativePush64(uint64_t value);
uint64_t* shv_nativeCall();
uint64_t* shv_getGlobalPtr(int index);
}

// ---- constants --------------------------------------------------------------
static const int kDefaultBlock = 114963 + 20127; // Legacy 3889; only a first guess
static const int kOffHappiness = 254;
static const int kOffTraining = 257;
static const int kOffCollar = 258;
static const int kOffValid = 259;
static const int kOffUsedApp = 263;

static const char* kVersion = "0.0.1";

static const char* kTricksDict = "creatures@rottweiler@tricks@";
static const char* kMoveDict = "creatures@rottweiler@move";
static const char* kTrickNames[4] = {"Sit", "Paw", "Beg", "Pet"};

static char g_dir[MAX_PATH] = "";

// ---- settings (Chopagotchi.ini) ---------------------------------------------
static int cfgEnabled = 1;
static int cfgKey = VK_F9;
static int cfgNotify = 1;
static int cfgBar = 1;
static int cfgGlobalIndex = 0;

static float gainWalkStart = 5, gainWalkMinute = 2, gainFetch = 3, gainTrick = 1, gainPet = 5;
static float decayMinutesPerPoint = 10, lossChopKilled = 25;
static float xpWalkMinute = 1, xpFetch = 2, xpTrick = 1;
static float xpNeeded[4] = {20, 60, 120, 200};
// Cooldowns between rewards. The walk is in in-game hours (24 = one in-game day,
// 48 real minutes); the rest are in real minutes.
static float cdWalkGameHours = 24, cdFetchMinutes = 1, cdTrickMinutes = 5, cdPetMinutes = 10;

// ---- logging ----------------------------------------------------------------
static void logf(const char* fmt, ...)
{
    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%sChopagotchi.log", g_dir);
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- natives ----------------------------------------------------------------
static uint64_t* call0(uint64_t h) { shv_nativeInit(h); return shv_nativeCall(); }
static uint64_t* call1(uint64_t h, uint64_t a) { shv_nativeInit(h); shv_nativePush64(a); return shv_nativeCall(); }
static uint64_t* call2(uint64_t h, uint64_t a, uint64_t b) { shv_nativeInit(h); shv_nativePush64(a); shv_nativePush64(b); return shv_nativeCall(); }
static uint64_t* call3(uint64_t h, uint64_t a, uint64_t b, uint64_t c) { shv_nativeInit(h); shv_nativePush64(a); shv_nativePush64(b); shv_nativePush64(c); return shv_nativeCall(); }
static uint64_t* call4(uint64_t h, uint64_t a, uint64_t b, uint64_t c, uint64_t d) { shv_nativeInit(h); shv_nativePush64(a); shv_nativePush64(b); shv_nativePush64(c); shv_nativePush64(d); return shv_nativeCall(); }

static int32_t asInt(uint64_t* r) { return r ? (int32_t)(uint32_t)*r : 0; }
static bool asBool(uint64_t* r) { return asInt(r) != 0; }
static uint64_t str(const char* s) { return (uint64_t)(uintptr_t)s; }
static uint64_t u(int32_t v) { return (uint64_t)(uint32_t)v; }

static int playerPed() { return asInt(call0(0xD80958FC74E988A6ULL)); }                       // PLAYER_PED_ID
static bool entityExists(int e) { return asBool(call1(0x7239B21A38F536BAULL, u(e))); }       // DOES_ENTITY_EXIST
static uint32_t entityModel(int e) { return (uint32_t)asInt(call1(0x9F47B058362C84B5ULL, u(e))); } // GET_ENTITY_MODEL
static bool entityDead(int e) { return asBool(call2(0x5F9532F3B5CC2551ULL, u(e), 0)); }      // IS_ENTITY_DEAD
static bool playingAnim(int e, const char* dict, const char* name)                           // IS_ENTITY_PLAYING_ANIM
{
    return asBool(call4(0x1F0B79228E461EC9ULL, u(e), str(dict), str(name), 3));
}
static int pedGroup(int ped) { return asInt(call1(0xF162E133B4E7A675ULL, u(ped))); }         // GET_PED_GROUP_INDEX
static bool inGroup(int ped, int group) { return asBool(call2(0x9BB01E3834671191ULL, u(ped), u(group))); } // IS_PED_GROUP_MEMBER
static int groupMember(int group, int n) { return asInt(call2(0x51455483CF23ED97ULL, u(group), u(n))); }   // GET_PED_AS_GROUP_MEMBER
static int groupSize(int group)                                                              // GET_GROUP_SIZE
{
    uint64_t leader = 0, count = 0;
    call3(0x8DE69FE35CA09A45ULL, u(group), (uint64_t)(uintptr_t)&leader, (uint64_t)(uintptr_t)&count);
    return (int32_t)(uint32_t)count;
}
static bool statInt(uint32_t hash, int32_t* out)                                             // STAT_GET_INT
{
    uint64_t v = 0;
    bool ok = asBool(call3(0x767FBC2AC802EF3DULL, hash, (uint64_t)(uintptr_t)&v, u(-1)));
    *out = (int32_t)(uint32_t)v;
    return ok;
}
static void entityCoords(int e, float* xyz)                                                  // GET_ENTITY_COORDS
{
    uint64_t* r = call2(0x3FEF770D40960D5AULL, u(e), 0);
    for (int i = 0; i < 3; ++i) memcpy(&xyz[i], r ? (void*)&r[i] : (void*)"\0\0\0\0", 4);
}
static bool onlineSession() { return asBool(call0(0x9DE624D2FC4B603FULL)); }                 // NETWORK_IS_SESSION_STARTED
static bool pauseMenu() { return asBool(call0(0xB0034A223497FFCBULL)); }                     // IS_PAUSE_MENU_ACTIVE
static bool loadingScreen() { return asBool(call0(0x10D0A8F259E93EC9ULL)); }                 // GET_IS_LOADING_SCREEN_ACTIVE

// The in-game date and time as minutes since 1 Jan 0000, so cooldowns measured
// in in-game time survive midnight, month ends and sleeping.
static long long gameMinutes()
{
    long long y = asInt(call0(0x961777E64BDAF717ULL));                                    // GET_CLOCK_YEAR
    long long m = asInt(call0(0xBBC72712E80257A1ULL));                                    // GET_CLOCK_MONTH (0-11)
    long long d = asInt(call0(0x3D10BC92A4DB1D35ULL));                                    // GET_CLOCK_DAY_OF_MONTH
    long long h = asInt(call0(0x25223CA6B4D20B7FULL));                                    // GET_CLOCK_HOURS
    long long mi = asInt(call0(0x13D2B8ADD79640F2ULL));                                   // GET_CLOCK_MINUTES
    return cooldowns::gameMinutes(y, m, d, h, mi);
}

static void notify(const char* fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    logf("%s", text);
    if (!cfgNotify) return;
    call1(0x202709F4C58A0424ULL, str("STRING")); // BEGIN_TEXT_COMMAND_THEFEED_POST
    call1(0x6C188BE134E074AAULL, str(text));     // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    call2(0x2ED7843F8F801023ULL, 0, 1);          // END_TEXT_COMMAND_THEFEED_POST_TICKER
}

static int mood(float happy) { return happy > 66.0f ? 2 : (happy > 33.0f ? 1 : 0); }
static const char* moodName(int m) { return m == 2 ? "happy" : (m == 1 ? "content" : "unhappy"); }

// ---- happiness gain popup -------------------------------------------------------
// When Chop's happiness goes up, the game's own bar (bottom right) fills smoothly
// (see applyToGame) and a green "+N" pops up beside it, floats up and fades out.
// Its position is set in Chopagotchi.ini ([Hud] GainX / GainY, screen 0..1).
static uint64_t f(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static bool hudHidden() { return asBool(call0(0xA86478C6958735C5ULL)); }                     // IS_HUD_HIDDEN
static float asFloatResult(uint64_t* r) { float v = 0; if (r) memcpy(&v, r, 4); return v; }
static float aspectRatio() { return asFloatResult(call1(0xF1307EF624A80D87ULL, 0)); }           // GET_ASPECT_RATIO
static float safeZoneSize() { return asFloatResult(call0(0xBAF107B6BB2C97F0ULL)); }             // GET_SAFE_ZONE_SIZE

// The game keeps its HUD (minimap, bars) inside a 16:9 area in the middle of
// wider screens, shrunk by the safe-zone setting. hx/hy are positions inside
// that area (0..1); the result is a full-screen position for drawing.
static void hudToScreen(float hx, float hy, float aspect, float safeZone, float* sx, float* sy)
{
    if (!(aspect > 0.1f) || !std::isfinite(aspect)) aspect = 16.0f / 9.0f;
    if (!(safeZone > 0.5f) || safeZone > 1.0f) safeZone = 1.0f;
    float hudW = (16.0f / 9.0f) / aspect;
    if (hudW > 1.0f) hudW = 1.0f;  // narrower than 16:9 (4:3, 16:10): the HUD uses the full width
    *sx = 0.5f + (hx - 0.5f) * safeZone * hudW;
    *sy = 0.5f + (hy - 0.5f) * safeZone;
}

static void drawText(const char* s, float x, float y, float scale, int r, int g, int b, int a)  // centred on x
{
    if (a <= 0) return;
    if (a > 255) a = 255;
    call1(0x66E0276CC5F6B9DAULL, 4);                       // SET_TEXT_FONT (condensed)
    call2(0x07C837F9A01C34C9ULL, f(scale), f(scale));      // SET_TEXT_SCALE
    call4(0xBE6B23FFA53FB442ULL, u(r), u(g), u(b), u(a));  // SET_TEXT_COLOUR
    call0(0x2513DFB0FB8400FEULL);                          // SET_TEXT_OUTLINE
    call1(0xC02F4DBFB51D988BULL, 1);                       // SET_TEXT_CENTRE
    call1(0x25FBB336DF1804CBULL, str("STRING"));           // BEGIN_TEXT_COMMAND_DISPLAY_TEXT
    call1(0x6C188BE134E074AAULL, str(s));                  // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    call3(0xCD015E5BB0D96A57ULL, f(x), f(y), 0);           // END_TEXT_COMMAND_DISPLAY_TEXT
}

static float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
static float easeOut(float t) { t = 1.0f - clamp01(t); return 1.0f - t * t * t; }
static float since(ULONGLONG now, ULONGLONG at) { return now > at ? (float)(now - at) : 0.0f; }

static const ULONGLONG kGainShowMs = 2000;
static const float kGainRise = 0.02f;      // how far a popup floats up
static const float kGainRowGap = 0.045f;   // training sits this far above happiness, so they never overlap
static float cfgGainX = 0.95f, cfgGainY = 0.90f;  // inside the HUD area, see hudToScreen

// One floating "Chop <what> +N" line. Gains that land while it is still up add up.
struct GainPopup {
    float total = 0;
    ULONGLONG at = 0;
    bool shown = false;
};
static GainPopup g_happyPop, g_trainPop;

static void gainPopup(GainPopup& p, float delta)
{
    ULONGLONG now = GetTickCount64();
    bool stillUp = p.shown && since(now, p.at) < kGainShowMs;
    p.total = stillUp ? p.total + delta : delta;
    p.at = now;
    p.shown = true;
}

static void drawPopup(GainPopup& p, const char* label, float rowOffset, int r, int g, int b, ULONGLONG now)
{
    if (!p.shown) return;
    float age = since(now, p.at);
    if (age >= kGainShowMs) { p.shown = false; return; }
    float t = age / kGainShowMs;
    float pop = age < 180.0f ? 0.56f - 0.14f * (age / 180.0f) : 0.42f;  // pops in big, settles
    float alpha = clamp01(age / 120.0f) * (1.0f - clamp01((t - 0.55f) / 0.45f));
    char buf[48];
    if (p.total >= 1.0f) snprintf(buf, sizeof(buf), "%s +%d", label, (int)lroundf(p.total));
    else snprintf(buf, sizeof(buf), "%s +%.1f", label, p.total);
    float sx, sy;
    hudToScreen(cfgGainX, cfgGainY, aspectRatio(), safeZoneSize(), &sx, &sy);
    drawText(buf, sx, sy - rowOffset - kGainRise * easeOut(t), pop, r, g, b, (int)(255 * alpha));
}

static void drawGain(ULONGLONG now)
{
    if (!cfgBar || (!g_happyPop.shown && !g_trainPop.shown)) return;
    if (pauseMenu() || loadingScreen() || hudHidden()) {
        // Still let them expire while hidden.
        if (since(now, g_happyPop.at) >= kGainShowMs) g_happyPop.shown = false;
        if (since(now, g_trainPop.at) >= kGainShowMs) g_trainPop.shown = false;
        return;
    }
    drawPopup(g_happyPop, "Chop Happiness", 0, 120, 230, 120, now);
    drawPopup(g_trainPop, "Chop Training", kGainRowGap, 110, 190, 255, now);
}

// ---- script-global access ---------------------------------------------------
static uintptr_t g_okLo = 0, g_okHi = 0;   // last region known to be committed + writable
static uintptr_t g_badLo = 0, g_badHi = 0; // last region known not to be

static bool accessible(const void* p)
{
    uintptr_t a = (uintptr_t)p;
    if (!a) return false;
    if (a >= g_okLo && a + 8 <= g_okHi) return true;
    if (a >= g_badLo && a < g_badHi) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
    bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
              (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
    if (!ok) {
        g_badLo = lo;
        g_badHi = hi;
        return false;
    }
    if (a + 8 > hi) return false;
    g_okLo = lo;
    g_okHi = hi;
    return true;
}

static uint64_t* slot(int index)
{
    if (index < 0) return nullptr;
    uint64_t* p = shv_getGlobalPtr(index);
    return accessible(p) ? p : nullptr;
}

static bool readInt(int index, int32_t* out)
{
    uint64_t* p = slot(index);
    if (!p) return false;
    *out = (int32_t)(uint32_t)*p;
    return true;
}

static bool readFloat(int index, float* out)
{
    int32_t bits;
    if (!readInt(index, &bits)) return false;
    memcpy(out, &bits, 4);
    return true;
}

static void writeInt(int index, int32_t v)
{
    uint64_t* p = slot(index);
    if (p) *p = (uint64_t)(uint32_t)v;
}

static uint32_t joaat(const char* s)
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

// ---- locating the saved Chop data -------------------------------------------
static uint32_t hChop, hWalkStat;

// The block is recognised by the array-length headers the game's compiler
// stores inside it. These never change, whatever the save contains:
//   +0 = 3 (cars), +34/+77/+120 = 5 (one list per car), +130 = 3,
//   +273/+277/+281/+285 = 3 (four small per-character arrays).
static const int kSigOffsets[9] = {0, 34, 77, 120, 130, 273, 277, 281, 285};
static const uint32_t kSigValues[9] = {3, 5, 5, 5, 3, 3, 3, 3, 3};
static const int kBlockSize = 292;
static const int kPageSlots = 0x40000; // script globals come in pages of this many slots

static uint64_t* g_page0 = nullptr; // start of the first page of script globals
static int g_page0Slots = 0;        // how many slots of it are safe to read

static bool mapPage0()
{
    uint64_t* p = shv_getGlobalPtr(0);
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) return false;
    uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    size_t slots = (end - (uintptr_t)p) / 8;
    g_page0 = p;
    g_page0Slots = slots > (size_t)kPageSlots ? kPageSlots : (int)slots;
    return g_page0Slots > kBlockSize;
}

static int sigMatches(int b)
{
    int n = 0;
    for (int k = 0; k < 9; ++k)
        if ((uint32_t)g_page0[b + kSigOffsets[k]] == kSigValues[k]) ++n;
    return n;
}

static bool signatureAt(int b)
{
    if (!g_page0 || b < 0 || b + kBlockSize > g_page0Slots) return false;
    return sigMatches(b) == 9;
}

// The values must look like Chop data before anything is written.
static bool chopDataSane(int b)
{
    float happy;
    int32_t training, collar, valid, used;
    if (!readFloat(b + kOffHappiness, &happy)) return false;
    if (!readInt(b + kOffTraining, &training)) return false;
    if (!readInt(b + kOffCollar, &collar)) return false;
    if (!readInt(b + kOffValid, &valid)) return false;
    if (!readInt(b + kOffUsedApp, &used)) return false;
    if (!std::isfinite(happy) || happy < 0.0f || happy > 100.0f) return false;
    if (training < 0 || training > 4) return false;
    if (collar < 0 || collar > 8) return false;
    if (valid != 0 && valid != 1) return false;
    if (used != 0 && used != 1) return false;
    return true;
}

static void logBlock(const char* label, int b)
{
    float happy = 0;
    int32_t car0 = 0, car1 = 0, car2 = 0, training = 0, collar = 0, valid = 0, used = 0;
    readInt(b + 1, &car0);
    readInt(b + 44, &car1);
    readInt(b + 87, &car2);
    readFloat(b + kOffHappiness, &happy);
    readInt(b + kOffTraining, &training);
    readInt(b + kOffCollar, &collar);
    readInt(b + kOffValid, &valid);
    readInt(b + kOffUsedApp, &used);
    logf("  %s %d: cars 0x%08X 0x%08X 0x%08X, happiness %g, tricks %d, collar %d, valid %d, usedApp %d", label, b,
         (uint32_t)car0, (uint32_t)car1, (uint32_t)car2, happy, training, collar, valid, used);
}

static int g_block = -1;
static int g_scanAttempts = 0;
static bool g_warnedInsane = false;
static bool g_loggedAmbiguous = false;

// Written to the log when the block cannot be found, so the layout of an
// unknown game build can be worked out from the log alone.
static void logDiagnostics()
{
    logf("Diagnostics: first page of globals at %p, %d slots readable.", (void*)g_page0, g_page0Slots);
    if (!g_page0) return;
    if (kDefaultBlock + kBlockSize <= g_page0Slots) {
        logf("  default location %d matches %d of 9 markers", kDefaultBlock, sigMatches(kDefaultBlock));
        logBlock("default", kDefaultBlock);
    }
    int shown = 0;
    for (int b = 0; b + kBlockSize <= g_page0Slots && shown < 15; ++b) {
        int m = sigMatches(b);
        if (m >= 6) {
            logf("  near miss: %d of 9 markers", m);
            logBlock("candidate", b);
            ++shown;
        }
    }
    logf("Diagnostics: %d near miss(es).", shown);
}

static bool locate()
{
    if (g_block >= 0 && signatureAt(g_block)) return true;
    g_block = -1;
    if (!mapPage0()) {
        if (++g_scanAttempts == 1) logf("Script globals are not available yet.");
        return false;
    }

    if (cfgGlobalIndex > 0) {
        if (cfgGlobalIndex + kBlockSize <= g_page0Slots) g_block = cfgGlobalIndex;
    } else {
        // One pass over the page (a few hundred KB of memory). Prefer a match
        // whose Chop values look right; take the one nearest the default.
        int best = -1, count = 0;
        long bestDist = 0;
        for (int b = 0; b + kBlockSize <= g_page0Slots; ++b) {
            if ((uint32_t)g_page0[b] != 3 || sigMatches(b) != 9) continue;
            if (!chopDataSane(b)) continue;
            ++count;
            long dist = labs((long)b - kDefaultBlock);
            if (best < 0 || dist < bestDist) {
                best = b;
                bestDist = dist;
            }
        }
        if (count > 1 && !g_loggedAmbiguous) {
            g_loggedAmbiguous = true;
            logf("%d places look like Chop's data; using the one nearest the expected location.", count);
        }
        g_block = best;
    }
    ++g_scanAttempts;
    if (g_block >= 0) {
        logf("Saved Chop data found on attempt %d: block index %d (expected %d, offset %+d)", g_scanAttempts, g_block,
             kDefaultBlock, g_block - kDefaultBlock);
        logBlock("found", g_block);
        return true;
    }
    if (g_scanAttempts == 1 || g_scanAttempts == 6 || g_scanAttempts % 60 == 0) {
        logf("Saved Chop data not found yet (attempt %d).", g_scanAttempts);
        if (g_scanAttempts <= 60) logDiagnostics();
    }
    return false;
}

static bool ready()
{
    if (onlineSession()) return false;
    if (!locate()) return false;
    if (!chopDataSane(g_block)) {
        if (!g_warnedInsane) {
            g_warnedInsane = true;
            logf("Block %d matched the signature but the Chop values look wrong. Nothing written.", g_block);
            notify("Chopagotchi: unexpected data, nothing changed. See Chopagotchi.log.");
        }
        return false;
    }
    return true;
}

// ---- Chop's progress (Chopagotchi.state.ini) --------------------------------
// The mod is the keeper of these values. On an account that ever used iFruit
// the game keeps copying the app's stale numbers over the saved block, so
// whatever is earned is held here and written back every frame.
static float g_xp = 0;
static float g_happy = 0;
static bool g_haveHappy = false; // false until read from the state file or the game
static int g_collar = -1;        // -1 = leave the game's collar alone
static bool g_stateDirty = false;
static bool g_live = false;      // true while story mode is running and the block is known
// Reward cooldowns, kept in the state file so reloading or restarting does not reset them.
static cooldowns::State g_cd;
// Which collars Chop has earned and how far along the rest are (rules/Collars.h).
static collars::Config cfgCollars;
static collars::Progress g_collarProgress;
// Random number in [0, 1) for the rare collar find; tests replace it.
// rand() starts every game session from the same seed, and its first number
// (41, so 0.0041 here) is under the default 2% chance: unseeded, the "rare"
// collar would turn up in the first minute of the first walk, every time.
static float randomRoll()
{
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        srand((unsigned)time(nullptr) ^ ((unsigned)GetTickCount64() * 2654435761u));
        rand();  // the first number after seeding still follows the seed closely
    }
    return (float)(rand() % 10000) / 10000.0f;
}
static float (*g_roll)() = randomRoll;

static void statePath(char* out, size_t n) { snprintf(out, n, "%sChopagotchi.state.ini", g_dir); }

static void loadState()
{
    char path[MAX_PATH + 32], buf[32];
    statePath(path, sizeof(path));
    GetPrivateProfileStringA("State", "TrainingXP", "0", buf, sizeof(buf), path);
    g_xp = (float)atof(buf);
    if (!std::isfinite(g_xp) || g_xp < 0) g_xp = 0;
    GetPrivateProfileStringA("State", "Happiness", "", buf, sizeof(buf), path);
    if (buf[0]) {
        g_happy = (float)atof(buf);
        if (!std::isfinite(g_happy) || g_happy < 0) g_happy = 0;
        if (g_happy > 100) g_happy = 100;
        g_haveHappy = true;
    }
    g_collar = (int)GetPrivateProfileIntA("State", "Collar", -1, path);
    if (g_collar < -1 || g_collar > 4) g_collar = -1;
    g_collarProgress = collars::load([&](const char* key, const std::string& def) {
        char v[32];
        GetPrivateProfileStringA("Collars", key, def.c_str(), v, sizeof(v), path);
        return std::string(v);
    }, g_collar);
    g_cd = cooldowns::load([&](const char* key, const std::string& def) {
        char v[32];
        GetPrivateProfileStringA("Cooldowns", key, def.c_str(), v, sizeof(v), path);
        return std::string(v);
    });
}

static void saveState()
{
    if (!g_stateDirty) return;
    g_stateDirty = false;
    char path[MAX_PATH + 32], buf[32];
    statePath(path, sizeof(path));
    snprintf(buf, sizeof(buf), "%.2f", g_xp);
    WritePrivateProfileStringA("State", "TrainingXP", buf, path);
    snprintf(buf, sizeof(buf), "%.2f", g_happy);
    WritePrivateProfileStringA("State", "Happiness", buf, path);
    snprintf(buf, sizeof(buf), "%d", g_collar);
    WritePrivateProfileStringA("State", "Collar", buf, path);
    cooldowns::save(g_cd, [&](const char* key, const std::string& v) {
        WritePrivateProfileStringA("Cooldowns", key, v.c_str(), path);
    });
    collars::save(g_collarProgress, [&](const char* key, const std::string& v) {
        WritePrivateProfileStringA("Collars", key, v.c_str(), path);
    });
}

// ---- the earning rules ------------------------------------------------------

static float happiness() { return g_happy; }

static int levelForXp(float xp)
{
    int level = 0;
    while (level < 4 && xp >= xpNeeded[level]) ++level;
    return level;
}

// Puts the mod's values into the game's block. A handful of memory writes,
// cheap enough to do every frame.
// Gains are fed to the game over about a second, so its own happiness bar
// (bottom right, near Chop) fills up smoothly instead of jumping.
static const ULONGLONG kGameFillMs = 1000;
static float g_gameFrom = 0, g_gameTo = -1;
static ULONGLONG g_gameAt = 0;

static float happinessForGame()
{
    if (g_gameTo != g_happy) return g_happy;
    float t = since(GetTickCount64(), g_gameAt) / kGameFillMs;
    if (t >= 1.0f) return g_happy;
    return g_gameFrom + (g_gameTo - g_gameFrom) * easeOut(t);
}

static void applyToGame()
{
    if (!g_live || !g_haveHappy || !signatureAt(g_block)) return;
    uint32_t bits;
    float shownHappy = happinessForGame();
    memcpy(&bits, &shownHappy, 4);
    g_page0[g_block + kOffHappiness] = bits;
    g_page0[g_block + kOffTraining] = (uint32_t)levelForXp(g_xp);
    g_page0[g_block + kOffValid] = 1;
    if (g_collar >= 0) {
        g_page0[g_block + kOffCollar] = (uint32_t)g_collar;
        g_page0[g_block + kOffUsedApp] = 1;
    }
}

static void addHappiness(float delta, const char* reason)
{
    if (delta == 0) return;
    float before = g_happy;
    float after = before + delta;
    if (after < 0) after = 0;
    if (after > 100) after = 100;
    if (after == before) return;
    if (after > before) {
        g_gameFrom = happinessForGame();
        g_gameTo = after;
        g_gameAt = GetTickCount64();
    }
    g_happy = after;
    g_stateDirty = true;
    if (fabsf(delta) >= 1.0f) logf("%s: happiness %.1f -> %.1f", reason, before, after);
    if (after > before) gainPopup(g_happyPop, after - before);
    int mb = mood(before), ma = mood(after);
    if (ma > mb) notify("Chop is %s now (%d/100).", moodName(ma), (int)after);
    else if (ma < mb) notify("Chop is getting %s (%d/100). Take him out.", moodName(ma), (int)after);
}

static void addXp(float delta)
{
    if (delta <= 0) return;
    int level = levelForXp(g_xp);
    g_xp += delta;
    g_stateDirty = true;
    gainPopup(g_trainPop, delta);
    int earned = levelForXp(g_xp);
    while (level < earned) {
        ++level;
        notify("Chop learned a new trick: %s (%d/4).", kTrickNames[level - 1], level);
    }
}

// Announces the collars in `unlocked` (a bit per collar) that were just earned.
static void collarsEarned(int unlocked)
{
    if (!unlocked) return;
    g_stateDirty = true;
    for (int i = 0; i < collars::kCount; ++i) {
        if (!(unlocked & (1 << i))) continue;
        char msg[96];
        collars::unlockMessage(cfgCollars, i, msg, sizeof(msg));
        logf("%s", msg);
        notify("%s Pick it in the app.", msg);
    }
}

// ---- watching what the player does with Chop --------------------------------
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

static void tick(float dt, ULONGLONG now)
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
        int level = levelForXp(g_xp);
        notify("Chopagotchi: Chop is %s (%d/100), tricks %d/4. Walk him, play fetch and pet him.",
               moodName(mood(happiness())), (int)happiness(), level);
    }

    int player = playerPed();
    if (!player || !entityExists(player)) return;
    (void)now;
    long long wall = (long long)time(nullptr);  // real time, so cooldowns carry across restarts
    {
        // Values no cooldown could have set (damaged file, clock moved back) would lock a reward.
        cooldowns::State before = g_cd;
        if (cooldowns::heal(g_cd, wall, (long long)(cdFetchMinutes * 60.0f), (long long)(cdTrickMinutes * 60.0f),
                            (long long)(cdPetMinutes * 60.0f))) {
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
            if (cooldowns::tryWalkBonus(g_cd.lastWalkGameMinute, gameMinutes(),
                                        (long long)(cdWalkGameHours * 60.0f), &left)) {
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
        collarsEarned(collars::onHappiness(g_collarProgress, cfgCollars, dt, happiness()));
        // Saved when a day is done or lost, and once a minute in between.
        if (before.bondStreak != g_collarProgress.bondStreak ||
            (int)(before.bondSeconds / 60) != (int)(g_collarProgress.bondSeconds / 60))
            g_stateDirty = true;
    }

    float pos[3];
    entityCoords(player, pos);
    float moved = 0;
    if (g_haveLastPos) {
        float dx = pos[0] - g_lastPos[0], dy = pos[1] - g_lastPos[1], dz = pos[2] - g_lastPos[2];
        moved = sqrtf(dx * dx + dy * dy + dz * dz);
    }
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
            if (drop && !g_wasFetchDrop &&
                cooldowns::tryTimedReward(g_cd.fetchReadyAt, wall, (long long)(cdFetchMinutes * 60.0f))) {
                g_stateDirty = true;
                addHappiness(gainFetch, "Fetch");
                addXp(xpFetch);
            }
            if (drop && !g_wasFetchDrop) {
                g_stateDirty = true;
                collarsEarned(collars::onFetch(g_collarProgress, cfgCollars));
            }
            g_wasFetchDrop = drop;
            if (drop) together = true;

            static const char* trickAnims[3] = {"sit_loop", "paw_right_loop", "beg_loop"};
            for (int i = 0; i < 3; ++i) {
                bool on = playingAnim(g_chop, kTricksDict, trickAnims[i]);
                if (on && !g_wasTrick[i] &&
                    cooldowns::tryTimedReward(g_cd.trickReadyAt[i], wall, (long long)(cdTrickMinutes * 60.0f))) {
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
    if (petting && !g_wasPetting &&
        cooldowns::tryTimedReward(g_cd.petReadyAt, wall, (long long)(cdPetMinutes * 60.0f))) {
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

static void showStatus()
{
    if (!ready()) {
        notify("Chopagotchi: Chop's data not found. See Chopagotchi.log.");
        return;
    }
    int level = levelForXp(g_xp);
    float h = happiness();
    if (level < 4)
        notify("Chop: %s (%d/100). Tricks %d/4, next: %s at %d/%d training.", moodName(mood(h)), (int)h, level,
               kTrickNames[level], (int)g_xp, (int)xpNeeded[level]);
    else
        notify("Chop: %s (%d/100). Knows all 4 tricks.", moodName(mood(h)), (int)h);
}

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

// ---- input ------------------------------------------------------------------
static bool gameFocused()
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return false;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---- settings file ----------------------------------------------------------
static float iniFloat(const char* ini, const char* section, const char* key, float def, float lo, float hi)
{
    char buf[32], defs[32];
    snprintf(defs, sizeof(defs), "%g", def);
    GetPrivateProfileStringA(section, key, defs, buf, sizeof(buf), ini);
    float v = (float)atof(buf);
    if (!std::isfinite(v)) v = def;
    return v < lo ? lo : (v > hi ? hi : v);
}

static void loadSettings()
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%sChopagotchi.ini", g_dir);
    cfgEnabled = GetPrivateProfileIntA("Settings", "Enabled", 1, ini);
    cfgKey = GetPrivateProfileIntA("Settings", "Key", VK_F9, ini);
    cfgNotify = GetPrivateProfileIntA("Settings", "Notifications", 1, ini);
    cfgBar = GetPrivateProfileIntA("Hud", "GainPopup", 1, ini);
    cfgGainX = iniFloat(ini, "Hud", "GainX", 0.95f, 0, 1);
    cfgGainY = iniFloat(ini, "Hud", "GainY", 0.90f, 0, 1);
    cfgGlobalIndex = GetPrivateProfileIntA("Advanced", "GlobalIndex", 0, ini);

    gainWalkStart = iniFloat(ini, "Happiness", "WalkStart", 5, 0, 100);
    gainWalkMinute = iniFloat(ini, "Happiness", "WalkPerMinute", 2, 0, 100);
    gainFetch = iniFloat(ini, "Happiness", "Fetch", 3, 0, 100);
    gainTrick = iniFloat(ini, "Happiness", "Trick", 1, 0, 100);
    gainPet = iniFloat(ini, "Happiness", "Pet", 5, 0, 100);
    decayMinutesPerPoint = iniFloat(ini, "Happiness", "MinutesApartPerPointLost", 10, 0, 100000);
    lossChopKilled = iniFloat(ini, "Happiness", "LostWhenChopIsKilled", 25, 0, 100);

    xpWalkMinute = iniFloat(ini, "Training", "WalkPerMinute", 1, 0, 1000);
    xpFetch = iniFloat(ini, "Training", "Fetch", 2, 0, 1000);
    xpTrick = iniFloat(ini, "Training", "Trick", 1, 0, 1000);
    xpNeeded[0] = iniFloat(ini, "Training", "NeededForSit", 20, 0, 1000000);
    xpNeeded[1] = iniFloat(ini, "Training", "NeededForPaw", 60, 0, 1000000);
    xpNeeded[2] = iniFloat(ini, "Training", "NeededForBeg", 120, 0, 1000000);
    xpNeeded[3] = iniFloat(ini, "Training", "NeededForPet", 200, 0, 1000000);
    cdWalkGameHours = iniFloat(ini, "Cooldowns", "WalkGameHours", 24, 0, 24 * 365);
    cdFetchMinutes = iniFloat(ini, "Cooldowns", "FetchMinutes", 1, 0, 60 * 24 * 7);
    cdTrickMinutes = iniFloat(ini, "Cooldowns", "TrickMinutes", 5, 0, 60 * 24 * 7);
    cdPetMinutes = iniFloat(ini, "Cooldowns", "PetMinutes", 10, 0, 60 * 24 * 7);
    for (int i = 1; i < 4; ++i)
        if (xpNeeded[i] < xpNeeded[i - 1]) xpNeeded[i] = xpNeeded[i - 1];
    cfgCollars.locked = GetPrivateProfileIntA("Collars", "Locked", 1, ini) != 0;
    cfgCollars.walks = (int)iniFloat(ini, "Collars", "Walks", 10, 1, 100000);
    cfgCollars.fetches = (int)iniFloat(ini, "Collars", "Fetches", 50, 1, 100000);
    cfgCollars.bondHappiness = iniFloat(ini, "Collars", "BondHappiness", 80, 0, 100);
    cfgCollars.bondDays = (int)iniFloat(ini, "Collars", "BondDays", 3, 1, 365);
    cfgCollars.findChance = iniFloat(ini, "Collars", "FindChance", 2, 0, 100);

    logf("Settings: enabled %d, key 0x%X, gains walk %g+%g/min fetch %g trick %g pet %g, decay 1 per %g min, tricks at %g/%g/%g/%g",
         cfgEnabled, cfgKey, gainWalkStart, gainWalkMinute, gainFetch, gainTrick, gainPet, decayMinutesPerPoint,
         xpNeeded[0], xpNeeded[1], xpNeeded[2], xpNeeded[3]);
}

// ---- the Chopagotchi phone app ----------------------------------------------
// A phone-style screen drawn where Franklin's phone sits, opened with the key
// (F9). The menu itself lives in rules/PhoneApp.h; this part reads the phone's
// own controls, draws the screen and carries out what the menu asks for. The
// game's phone has no way to take a new app, so while the real phone is out a
// small "Chopagotchi" tab above it shows the key to open this one.
static int cfgPhone = 1, cfgPhoneHint = 1;
// Defaults put the app over the game's phone: measured on a 32:9 screen, the phone
// is centred at 0.925 of the HUD area's width and its top is at 0.60 of its height.
static float cfgPhoneX = 0.925f, cfgPhoneY = 0.807f, cfgPhoneScale = 1.0f;
static phoneapp::App g_app;
static char g_appNote[96] = "";
static ULONGLONG g_appNoteUntil = 0;

static void loadPhoneSettings()
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%sChopagotchi.ini", g_dir);
    cfgPhone = GetPrivateProfileIntA("Phone", "App", 1, ini);
    cfgPhoneHint = GetPrivateProfileIntA("Phone", "Hint", 1, ini);
    cfgPhoneX = iniFloat(ini, "Phone", "X", 0.925f, 0, 1);
    cfgPhoneY = iniFloat(ini, "Phone", "Y", 0.807f, 0, 1);
    cfgPhoneScale = iniFloat(ini, "Phone", "Scale", 1.0f, 0.5f, 2.0f);
}

static void saveSetting(const char* section, const char* key, int v)
{
    char ini[MAX_PATH + 32], buf[16];
    snprintf(ini, sizeof(ini), "%sChopagotchi.ini", g_dir);
    snprintf(buf, sizeof(buf), "%d", v);
    WritePrivateProfileStringA(section, key, buf, ini);
}

static void appNote(const char* text)
{
    snprintf(g_appNote, sizeof(g_appNote), "%s", text);
    g_appNoteUntil = GetTickCount64() + 3500;
    logf("Phone: %s", text);
}

// True while Franklin has the phone out in his hand.
static bool phoneOnScreen() { return asBool(call1(0x2AFE52F782F25775ULL, u(playerPed()))); } // IS_PED_RUNNING_MOBILE_PHONE_TASK

static void phRect(float x, float y, float w, float h, int r, int g, int b, int a)             // DRAW_RECT, top-left + size
{
    shv_nativeInit(0x3A618A217E5154F0ULL);
    shv_nativePush64(f(x + w / 2));
    shv_nativePush64(f(y + h / 2));
    shv_nativePush64(f(w));
    shv_nativePush64(f(h));
    shv_nativePush64(u(r));
    shv_nativePush64(u(g));
    shv_nativePush64(u(b));
    shv_nativePush64(u(a));
    shv_nativePush64(0);
    shv_nativeCall();
}

// align: 0 centre on x, 1 left from x, 2 right up to x.
static void phText(const char* s, float x, float y, float scale, int r, int g, int b, int a, int align, int font = 0)
{
    call1(0x66E0276CC5F6B9DAULL, u(font));                       // SET_TEXT_FONT
    call2(0x07C837F9A01C34C9ULL, f(scale), f(scale));            // SET_TEXT_SCALE
    call4(0xBE6B23FFA53FB442ULL, u(r), u(g), u(b), u(a));        // SET_TEXT_COLOUR
    call1(0x4E096588B13FFECAULL, u(align));                      // SET_TEXT_JUSTIFICATION
    if (align == 2) call2(0x63145D9C883A1A70ULL, f(0.0f), f(x)); // SET_TEXT_WRAP
    call1(0x25FBB336DF1804CBULL, str("STRING"));                 // BEGIN_TEXT_COMMAND_DISPLAY_TEXT
    call1(0x6C188BE134E074AAULL, str(s));                        // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    call3(0xCD015E5BB0D96A57ULL, f(x), f(y), 0);                 // END_TEXT_COMMAND_DISPLAY_TEXT
}

static void phMoodColour(int m, int* r, int* g, int* b)
{
    if (m == 2) { *r = 114; *g = 204; *b = 114; }
    else if (m == 1) { *r = 240; *g = 200; *b = 80; }
    else { *r = 224; *g = 80; *b = 70; }
}

static int currentCollar()
{
    if (g_collar >= 0) return g_collar;
    int32_t c = -1;
    if (ready() && readInt(g_block + kOffCollar, &c) && c >= 0 && c < phoneapp::kCollars) return c;
    return -1;
}

static void phoneWhistle()
{
    if (!ready()) { appNote("Chop's data isn't loaded yet."); return; }
    int player = playerPed();
    if (!g_chop || !entityExists(g_chop)) { appNote("Chop isn't with you. Pick him up at home."); return; }
    if (entityDead(g_chop)) { appNote("Chop is hurt."); return; }
    if (asBool(call2(0x997ABD671D25CA0BULL, u(player), 0))) {                               // IS_PED_IN_ANY_VEHICLE
        appNote("Get out of the car first.");
        return;
    }
    float a[3], b[3];
    entityCoords(player, a);
    entityCoords(g_chop, b);
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 4.0f) { appNote("Chop is right beside you."); return; }
    if (dist > 120.0f) { appNote("Chop is too far away to hear you."); return; }
    // TASK_GO_TO_ENTITY(chop, player, no time limit, stop 2 m away, sprint, 2.0, 0)
    shv_nativeInit(0x6A071245EB0D1882ULL);
    shv_nativePush64(u(g_chop));
    shv_nativePush64(u(player));
    shv_nativePush64(u(-1));
    shv_nativePush64(f(2.0f));
    shv_nativePush64(f(3.0f));
    shv_nativePush64(f(2.0f));
    shv_nativePush64(0);
    shv_nativeCall();
    appNote("You whistle. Chop comes running!");
}

static void phoneAct(const phoneapp::Action& act)
{
    switch (act.type) {
    case phoneapp::Whistle: phoneWhistle(); break;
    case phoneapp::SetCollar:
        if (!ready()) { appNote("Chop's data isn't loaded yet."); break; }
        if (!collars::isUnlocked(g_collarProgress, cfgCollars, act.value)) {
            // The app only says "locked"; how far along he is goes to the log.
            char h[64], note[48];
            collars::hint(g_collarProgress, cfgCollars, act.value, h, sizeof(h));
            logf("Collar %d not set. %s", act.value + 1, h);
            snprintf(note, sizeof(note), "Collar %d is locked.", act.value + 1);
            appNote(note);
            break;
        }
        g_collar = act.value;
        g_stateDirty = true;
        applyToGame();
        logf("Collar set to %d", g_collar);
        break;
    case phoneapp::ToggleMod:
        cfgEnabled = !cfgEnabled;
        saveSetting("Settings", "Enabled", cfgEnabled);
        logf("Chopagotchi %s from the phone", cfgEnabled ? "on" : "off");
        break;
    case phoneapp::ToggleMessages:
        cfgNotify = !cfgNotify;
        saveSetting("Settings", "Notifications", cfgNotify);
        break;
    case phoneapp::ToggleGainPopup:
        cfgBar = !cfgBar;
        saveSetting("Hud", "GainPopup", cfgBar);
        break;
    default: break;
    }
}

static void phonePress(phoneapp::Key key)
{
    phoneAct(phoneapp::press(g_app, key, currentCollar()));
}

// The phone's own controls, so arrows / Enter / Backspace and the pad's d-pad,
// A and B all work. They are switched off for the game while the app is open,
// which also keeps the real phone from opening or reacting underneath.
static const int kPhoneControls[] = {27, 172, 173, 174, 175, 176, 177, 199, 200};
static void phoneInput()
{
    for (int c : kPhoneControls) call3(0xFE99B66D079CF6BCULL, 0, u(c), 1);              // DISABLE_CONTROL_ACTION
    auto hit = [](int c) { return asBool(call2(0x91AEF906BCA88877ULL, 0, u(c))); };      // IS_DISABLED_CONTROL_JUST_PRESSED
    if (hit(172) || hit(27)) phonePress(phoneapp::KeyUp);
    else if (hit(173)) phonePress(phoneapp::KeyDown);
    else if (hit(176)) phonePress(phoneapp::KeySelect);
    else if (hit(177) || hit(200)) phonePress(phoneapp::KeyBack);
}

static void keyName(char* out, int n)
{
    LONG sc = (LONG)MapVirtualKeyA((UINT)cfgKey, 0 /* MAPVK_VK_TO_VSC */);
    if (sc == 0 || GetKeyNameTextA(sc << 16, out, n) <= 0) snprintf(out, n, "key %d", cfgKey);
}

// Layout, in screen fractions. The screen is W x H, centred on (X, Y).
struct PhoneLayout {
    float cx, cy;
    float s, w, h, left, top, pad, row;
    float x(float fx) const { return left + w * fx; }
};

static PhoneLayout phoneLayout()
{
    PhoneLayout L;
    phoneapp::HudArea hud = phoneapp::hudArea(aspectRatio(), safeZoneSize());
    L.s = cfgPhoneScale;
    L.cx = hud.left + cfgPhoneX * hud.width;
    L.cy = hud.top + cfgPhoneY * hud.height;
    L.w = 0.16f * L.s * hud.squeeze;
    L.h = 0.40f * L.s;
    L.left = L.cx - L.w / 2;
    L.top = L.cy - L.h / 2;
    L.pad = 0.009f * L.s * hud.squeeze;
    L.row = 0.032f * L.s;
    return L;
}

static void phRow(const PhoneLayout& L, float y, const char* label, const char* value, bool selected = false,
                  int vr = 255, int vg = 255, int vb = 255)
{
    if (selected) phRect(L.left + L.pad / 2, y, L.w - L.pad, L.row, 255, 255, 255, 46);
    float ty = y + L.row * 0.12f;
    phText(label, L.left + L.pad * 1.4f, ty, 0.32f * L.s, 235, 235, 235, 255, 1);
    if (value) phText(value, L.left + L.w - L.pad * 1.4f, ty, 0.32f * L.s, vr, vg, vb, 255, 2);
}

static void phBar(const PhoneLayout& L, float y, float frac, int r, int g, int b)
{
    float x = L.left + L.pad * 1.4f, w = L.w - L.pad * 2.8f, h = 0.009f * L.s;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    phRect(x, y, w, h, 255, 255, 255, 40);
    if (frac > 0) phRect(x, y, w * frac, h, r, g, b, 255);
}

static void drawPhoneHint()
{
    PhoneLayout L = phoneLayout();
    char key[32], text[64];
    keyName(key, sizeof(key));
    snprintf(text, sizeof(text), "Chopagotchi  [%s]", key);
    // Just above the phone's top edge (the app's top) and about the phone's width.
    float y = L.top - 0.034f * L.s, w = L.w * 0.85f;
    phRect(L.cx - w / 2, y, w, 0.028f * L.s, 232, 120, 40, 220);
    phText(text, L.cx, y + 0.002f * L.s, 0.32f * L.s, 255, 255, 255, 255, 0);
}

static void drawPhoneApp(ULONGLONG now)
{
    using namespace phoneapp;
    PhoneLayout L = phoneLayout();
    // Body, header and footer.
    phRect(L.left - 0.004f * L.s, L.top - 0.007f * L.s, L.w + 0.008f * L.s, L.h + 0.014f * L.s, 10, 10, 12, 245);
    phRect(L.left, L.top, L.w, L.h, 28, 28, 34, 250);
    float headH = 0.05f * L.s;
    phRect(L.left, L.top, L.w, headH, 232, 120, 40, 255);
    static const char* titles[] = {"Chopagotchi", "Overview", "Cooldowns", "Collar", "Settings"};
    phText(titles[g_app.page], L.cx, L.top + 0.006f * L.s, 0.55f * L.s, 255, 255, 255, 255, 0, 4);

    bool have = ready() && g_haveHappy;
    float h = happiness();
    int m = mood(h), mr, mg, mb;
    phMoodColour(m, &mr, &mg, &mb);
    Training tr = training(g_xp, xpNeeded);
    char v[64];
    float y = L.top + headH + L.pad;

    switch (g_app.page) {
    case Home: {
        if (have) {
            snprintf(v, sizeof(v), "%d/100", (int)h);
            char mood1[24];
            snprintf(mood1, sizeof(mood1), "Chop is %s", moodName(m));
            phRow(L, y, mood1, v, false, mr, mg, mb);
            phBar(L, y + L.row, h / 100.0f, mr, mg, mb);
        } else {
            phRow(L, y, "Looking for Chop...", nullptr);
        }
        y += L.row * 1.8f;
        for (int i = 0; i < kHomeItems; ++i, y += L.row)
            phRow(L, y, kHomeLabels[i], nullptr, g_app.cursor == i);
        break;
    }
    case Overview: {
        if (!have) { phRow(L, y, "Chop's data isn't loaded yet.", nullptr); break; }
        snprintf(v, sizeof(v), "%s  %d/100", moodName(m), (int)h);
        v[0] = (char)(v[0] - 'a' + 'A');
        phRow(L, y, "Mood", v, false, mr, mg, mb);
        phBar(L, y + L.row, h / 100.0f, mr, mg, mb);
        y += L.row * 1.6f;
        snprintf(v, sizeof(v), "%d", (int)g_xp);
        phRow(L, y, "Training", v);
        y += L.row;
        snprintf(v, sizeof(v), "%d of 4", tr.level);
        phRow(L, y, "Tricks", v);
        y += L.row;
        trickList(tr.level, kTrickNames, v, sizeof(v));
        phText(v, L.left + L.pad * 1.4f, y + L.row * 0.1f, 0.28f * L.s, 170, 170, 180, 255, 1);
        y += L.row;
        if (tr.level < 4) {
            snprintf(v, sizeof(v), "%s at %d", kTrickNames[tr.level], (int)tr.next);
            phRow(L, y, "Next trick", v);
            phBar(L, y + L.row, tr.fraction, 93, 182, 229);
            y += L.row * 1.6f;
        } else {
            phRow(L, y, "Next trick", "All learned");
            y += L.row;
        }
        bool with = g_chop && entityExists(g_chop);
        bool hurt = with && entityDead(g_chop);
        phRow(L, y, "Chop", hurt ? "Hurt" : (with ? "With you" : "At home"));
        break;
    }
    case Timers: {
        long long wall = (long long)time(nullptr);
        if (have) formatGameMinutes(walkMinutesLeft(g_cd.lastWalkGameMinute, gameMinutes(),
                                                     (long long)(cdWalkGameHours * 60.0f)), v, sizeof(v));
        else snprintf(v, sizeof(v), "-");
        phRow(L, y, "Walk bonus", v, false, v[0] == 'R' ? 114 : 255, 255, v[0] == 'R' ? 114 : 255);
        y += L.row;
        struct { const char* name; long long readyAt; int needLevel; } rows[] = {
            {"Fetch", g_cd.fetchReadyAt, 0},
            {"Sit", g_cd.trickReadyAt[0], 1},
            {"Paw", g_cd.trickReadyAt[1], 2},
            {"Beg", g_cd.trickReadyAt[2], 3},
            {"Petting", g_cd.petReadyAt, 0},
        };
        for (auto& r : rows) {
            bool locked = tr.level < r.needLevel;
            if (locked) snprintf(v, sizeof(v), "Not learned");
            else formatSeconds(secondsLeft(r.readyAt, wall), v, sizeof(v));
            bool readyNow = !locked && v[0] == 'R';
            int c = locked ? 150 : 255;
            phRow(L, y, r.name, v, false, readyNow ? 114 : c, readyNow ? 204 : c, readyNow ? 114 : c);
            y += L.row;
        }
        phText("Walk bonus is in game time", L.left + L.pad * 1.4f, y + L.row * 0.3f, 0.26f * L.s, 150, 150, 160, 255, 1);
        break;
    }
    case Collars: {
        int cur = currentCollar();
        for (int i = 0; i < kCollars; ++i, y += L.row) {
            snprintf(v, sizeof(v), "Collar %d", i + 1);
            if (i == cur)
                phRow(L, y, v, "Wearing", g_app.cursor == i, 114, 204, 114);
            else if (!collars::isUnlocked(g_collarProgress, cfgCollars, i))
                phRow(L, y, v, "Locked", g_app.cursor == i, 150, 150, 160);  // not said here what earns it
            else
                phRow(L, y, v, nullptr, g_app.cursor == i);
        }
        break;
    }
    case Settings: {
        const int values[kSettingItems] = {cfgEnabled, cfgNotify, cfgBar};
        for (int i = 0; i < kSettingItems; ++i, y += L.row)
            phRow(L, y, kSettingLabels[i], values[i] ? "On" : "Off", g_app.cursor == i,
                  values[i] ? 114 : 224, values[i] ? 204 : 80, values[i] ? 114 : 70);
        break;
    }
    }

    float footY = L.top + L.h - 0.03f * L.s;
    if (now < g_appNoteUntil) {
        float ny = footY - L.row * 1.1f;
        phRect(L.left + L.pad / 2, ny, L.w - L.pad, L.row, 240, 200, 80, 60);
        phText(g_appNote, L.cx, ny + L.row * 0.15f, 0.28f * L.s, 255, 235, 170, 255, 0);
    }
    phRect(L.left, footY, L.w, 0.03f * L.s, 18, 18, 22, 255);
    bool list = rows(g_app.page) > 0;
    const char* foot = g_app.page == Home ? "Enter: open    Backspace: close"
                       : list             ? "Enter: choose    Backspace: back"
                                          : "Backspace: back";
    phText(foot, L.cx, footY + 0.005f * L.s, 0.26f * L.s, 160, 160, 170, 255, 0);
}

// Called every frame.
// The game draws its phone on top of anything a mod draws, so when the app is
// opened with the phone out, the phone is shrunk to nothing until the app closes.
// The app then takes its place; closing the app brings the phone back as it was.
static const float kPhoneScale = 500.0f;  // the scale the game gives its phone
static bool g_phoneShrunk = false;

static void setPhoneScale(float s) { call1(0xCBDD322A73D6D932ULL, f(s)); }                  // SET_MOBILE_PHONE_SCALE

// ---- the app on Franklin's real phone ---------------------------------------
// Where the game allows it, Chopagotchi is also on the phone itself: a row in
// the menu of the phone's Checklist app (or, with InChecklist=0, an icon of its
// own on the home screen) that opens the same pages on the phone's plain list
// screen. rules/RealPhone.h finds the phone's data and does the work;
// rules/PhoneScreens.h turns the pages into rows. If the phone's data cannot
// be found or does not check out, the phone is left alone and the F9 app above
// is the way in.
static int cfgInPhone = 1, cfgInChecklist = 1, cfgPhoneIcon = 14, cfgPhoneCell = -1;
static bool g_realSettingsLoaded = false;
static realphone::Phone g_real;
static phoneapp::App g_realApp;
static int g_realScroll = 0;

static void loadRealPhoneSettings()
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%sChopagotchi.ini", g_dir);
    cfgInPhone = GetPrivateProfileIntA("Phone", "InPhone", 1, ini);
    cfgInChecklist = GetPrivateProfileIntA("Phone", "InChecklist", 1, ini);
    cfgPhoneIcon = GetPrivateProfileIntA("Phone", "Icon", 14, ini);
    cfgPhoneCell = GetPrivateProfileIntA("Phone", "Cell", -1, ini);
    // The home screen only has artwork for the icons the game itself puts there.
    static const int icons[] = {1, 2, 4, 5, 6, 12, 14, 17, 24, 27, 39, 40, 42, 43, 54, 57, 58, 59, 62};
    bool known = false;
    for (int i : icons) known = known || i == cfgPhoneIcon;
    if (!known) cfgPhoneIcon = 14;
    if (cfgPhoneCell < -1 || cfgPhoneCell > 8) cfgPhoneCell = -1;
    g_realSettingsLoaded = true;
    if (!cfgInPhone) logf("In-phone app: off");
    else if (cfgInChecklist) logf("In-phone app: on, as a row inside the Checklist app");
    else logf("In-phone app: on, as an icon (icon %d, cell %d)", cfgPhoneIcon, cfgPhoneCell);
}

struct RealHost : realphone::Host {
    RealHost() { mapPage0(); }
    int slots() override { return g_page0 ? g_page0Slots : 0; }
    uint64_t* slot(int index) override { return g_page0 && index >= 0 && index < g_page0Slots ? g_page0 + index : nullptr; }
    unsigned long long nowMs() override { return GetTickCount64(); }
    bool movieLoaded(int handle) override { return asBool(call1(0x85F01B8D5B90570EULL, u(handle))); } // HAS_SCALEFORM_MOVIE_LOADED
    void begin(int handle, const char* method) override { call2(0xF6E48914C7A8694EULL, u(handle), str(method)); } // BEGIN_SCALEFORM_MOVIE_METHOD
    void addInt(int v) override { call1(0xC3D0841A0CC546A6ULL, u(v)); }                        // SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT
    void addText(const char* text) override
    {
        call1(0x80338406F3475E55ULL, str("STRING"));                                           // BEGIN_TEXT_COMMAND_SCALEFORM_STRING
        call1(0x6C188BE134E074AAULL, str(text));                                               // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
        call0(0x362E2D3FE93A9959ULL);                                                          // END_TEXT_COMMAND_SCALEFORM_STRING
    }
    void end() override { call0(0xC6796A8FFA375E53ULL); }                                      // END_SCALEFORM_MOVIE_METHOD
    bool pressed(int control) override
    {
        return asBool(call2(0x580417101DDB492FULL, 2, u(control))) ||                          // IS_CONTROL_JUST_PRESSED
               asBool(call2(0x91AEF906BCA88877ULL, 2, u(control)));                            // IS_DISABLED_CONTROL_JUST_PRESSED
    }
    void sound(const char* name, const char* set) override
    {
        if (set && set[0]) call4(0x67C540AA08E4A6F5ULL, u(-1), str(name), str(set), 1);        // PLAY_SOUND_FRONTEND
    }
    void log(const char* text) override { logf("%s", text); }
    int endAsking() override { return asInt(call0(0xC50AA39A577AF886ULL)); }                   // END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE
    bool answerReady(int ticket) override { return asBool(call1(0x768FF8961BA904D6ULL, u(ticket))); } // IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY
    int answerInt(int ticket) override { return asInt(call1(0x2DE7EFA66B906036ULL, u(ticket))); }     // GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT
    int scriptThreads(const char* script) override
    {
        return asInt(call1(0x2C83A9DA6BFFC4F9ULL, u((int32_t)realphone::joaat(script))));      // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH
    }
    void endScript(const char* script) override { call1(0x9DC711BC69C548DFULL, str(script)); }     // TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME
    void requestScript(const char* script) override { call1(0x6EB5F71AA68F2E8EULL, str(script)); } // REQUEST_SCRIPT
    void releaseScript(const char* script) override { call1(0xC90D2DCACD56184CULL, str(script)); } // SET_SCRIPT_AS_NO_LONGER_NEEDED
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
    // What the F9 app shows as a note at the bottom goes above the minimap here.
    g_appNoteUntil = 0;
    phoneAct(act);
    if (g_appNoteUntil != 0) {
        call1(0x202709F4C58A0424ULL, str("STRING")); // BEGIN_TEXT_COMMAND_THEFEED_POST
        call1(0x6C188BE134E074AAULL, str(g_appNote)); // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
        call2(0x2ED7843F8F801023ULL, 0, 1);          // END_TEXT_COMMAND_THEFEED_POST_TICKER
        g_appNoteUntil = 0;
    }
}

static void realPhoneStatus(phonescreens::Status& st)
{
    st.have = ready() && g_haveHappy;
    st.happy = happiness();
    st.mood = mood(st.happy);
    st.xp = g_xp;
    for (int i = 0; i < 4; ++i) st.needed[i] = xpNeeded[i];
    st.trickNames = kTrickNames;
    bool with = g_chop && entityExists(g_chop);
    st.chop = with ? (entityDead(g_chop) ? 2 : 1) : 0;
    st.collar = currentCollar();
    for (int i = 0; i < collars::kCount && i < phoneapp::kCollars; ++i)
        st.collarLocked[i] = !collars::isUnlocked(g_collarProgress, cfgCollars, i);
    st.walkMinutesLeft = phoneapp::walkMinutesLeft(g_cd.lastWalkGameMinute, gameMinutes(), (long long)(cdWalkGameHours * 60.0f));
    long long wall = (long long)time(nullptr);
    const long long readyAt[5] = {g_cd.fetchReadyAt, g_cd.trickReadyAt[0], g_cd.trickReadyAt[1], g_cd.trickReadyAt[2], g_cd.petReadyAt};
    for (int i = 0; i < 5; ++i) st.secondsLeft[i] = phoneapp::secondsLeft(readyAt[i], wall);
    st.on[phoneapp::SettingMod] = cfgEnabled != 0;
    st.on[phoneapp::SettingMessages] = cfgNotify != 0;
    st.on[phoneapp::SettingGainPopup] = cfgBar != 0;
}

// The app key (F9). While the in-phone app is up the key does nothing, so the
// two apps never fight over the same controls.
static void appKeyPressed()
{
    if (!g_real.open()) phonePress(phoneapp::KeyApp);
}

// Called every frame, before the F9 app.
static void realPhoneFrame()
{
    if (!g_realSettingsLoaded) loadRealPhoneSettings();
    // Franklin's phone only: Chop is his dog.
    bool want = cfgPhone && cfgInPhone && !onlineSession() && entityModel(playerPed()) == joaat("player_one");
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
    phonescreens::Status st;
    realPhoneStatus(st);
    realphone::Screen screen;
    phonescreens::build(g_realApp, st, g_realScroll, screen);
    if (!screen.canSelect) g_realScroll = screen.selected;
    g_real.show(host, screen);
}

static void phoneFrame(ULONGLONG now)
{
    realPhoneFrame();
    if (!cfgPhone) return;
    if (g_app.open && (g_phoneShrunk || phoneOnScreen())) {
        setPhoneScale(0.0f);  // every frame: the game resets it when the phone moves
        g_phoneShrunk = true;
    } else if (!g_app.open && g_phoneShrunk) {
        setPhoneScale(kPhoneScale);
        g_phoneShrunk = false;
    }
    bool hidden = pauseMenu() || loadingScreen() || onlineSession();
    if (g_app.open) {
        if (hidden) return;
        phoneInput();
        if (g_app.open) drawPhoneApp(now);
    } else if (cfgPhoneHint && !hidden && !g_real.onPhone() && phoneOnScreen()) {
        drawPhoneHint();
    }
}

// ---- script -----------------------------------------------------------------
static void scriptMain()
{
    hChop = joaat("a_c_chop");
    hWalkStat = joaat("sp_chop_walk_done");
    loadSettings();
    loadPhoneSettings();
    loadState();
    logf("Progress so far: training %.1f, happiness %s%.1f, collar %d", g_xp, g_haveHappy ? "" : "(none yet) ", g_happy, g_collar);

    ULONGLONG last = GetTickCount64();
    ULONGLONG nextTick = last + 5000;
    ULONGLONG nextSave = last + 15000;
    ULONGLONG nextKeyPoll = 0;
    bool keyWasDown = false;

    for (;;) {
        ULONGLONG now = GetTickCount64();

        if (now >= nextTick) {
            float dt = (float)(now - last) / 1000.0f;
            last = now;
            nextTick = now + (g_block >= 0 ? 250 : 5000);
            // Paused or loading time does not count for or against Chop.
            if (dt > 2.0f) {
                dt = 0;
                g_haveLastPos = false;
            }
            if (cfgEnabled && !pauseMenu() && !loadingScreen())
                tick(dt, now);
            else
                g_haveLastPos = false;
        }

        if (now >= nextSave) {
            nextSave = now + 15000;
            saveState();
        }

        if (cfgEnabled) applyToGame();
        if (cfgEnabled) drawGain(now);
        phoneFrame(now);

        if (now < nextKeyPoll) {
            shv_scriptWait(0);
            continue;
        }
        nextKeyPoll = now + 50;
        bool keyDown = down(cfgKey) && gameFocused();
        if (keyDown && !keyWasDown) {
            if (down(VK_SHIFT)) {
                cycleCollar();
            } else if (down(VK_CONTROL)) {
                cfgEnabled = !cfgEnabled;
                notify(cfgEnabled ? "Chopagotchi: on" : "Chopagotchi: off");
            } else if (cfgPhone) {
                appKeyPressed();
            } else {
                showStatus();
            }
        }
        keyWasDown = keyDown;

        shv_scriptWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            char* slash = strrchr(path, '\\');
            if (slash) {
                slash[1] = 0;
                snprintf(g_dir, sizeof(g_dir), "%s", path);
            }
        }
        char log[MAX_PATH + 32];
        snprintf(log, sizeof(log), "%sChopagotchi.log", g_dir);
        DeleteFileA(log);
        logf("Chopagotchi v%s loaded", kVersion);
        shv_scriptRegister(module, scriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        shv_scriptUnregister(module);
    }
    return TRUE;
}
