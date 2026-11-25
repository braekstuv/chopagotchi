// Chopagotchi - finding Chop's saved data in the game's script globals.
//
// The iFruit app used to sync Chop's happiness, trick level and collar into a
// small block of the game's saved script data. Layout of that block (from the
// decompiled chop / social_controller / main scripts, where it appears as
// Global_114963.f_20127 in Legacy 3889):
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
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Hud.h"
#include "Log.h"
#include "Natives.h"
#include "Settings.h"

static const int kDefaultBlock = 114963 + 20127; // Legacy 3889; only a first guess
static const int kOffHappiness = 254;
static const int kOffTraining = 257;
static const int kOffCollar = 258;
static const int kOffValid = 259;
static const int kOffUsedApp = 263;

// ---- script-global access ---------------------------------------------------
static uintptr_t g_okLo = 0, g_okHi = 0;   // last region known to be committed + writable
static uintptr_t g_badLo = 0, g_badHi = 0; // last region known not to be

static bool writableRegion(const MEMORY_BASIC_INFORMATION& mbi)
{
    return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
}

static bool accessible(const void* p)
{
    uintptr_t a = (uintptr_t)p;
    if (!a) return false;
    if (a >= g_okLo && a + 8 <= g_okHi) return true;
    if (a >= g_badLo && a < g_badHi) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
    if (!writableRegion(mbi)) {
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

// ---- locating the saved Chop data -------------------------------------------
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
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || !writableRegion(mbi)) return false;
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

// True in story mode once Chop's data is found and looks right.
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
