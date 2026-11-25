// Chopagotchi - Script Hook V imports and the game natives the mod calls.
//
// Every native goes through call(hash, args...). Arguments are pushed as
// 64-bit values: use u() for ints, f() for floats and str() for strings.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../rules/Cooldowns.h"
#include "../rules/Joaat.h"

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

template <class... Args> static uint64_t* call(uint64_t hash, Args... args)
{
    shv_nativeInit(hash);
    (shv_nativePush64((uint64_t)args), ...);
    return shv_nativeCall();
}

static int32_t asInt(uint64_t* r) { return r ? (int32_t)(uint32_t)*r : 0; }
static bool asBool(uint64_t* r) { return asInt(r) != 0; }
static float asFloat(uint64_t* r) { float v = 0; if (r) memcpy(&v, r, 4); return v; }
static uint64_t str(const char* s) { return (uint64_t)(uintptr_t)s; }
static uint64_t u(int32_t v) { return (uint64_t)(uint32_t)v; }
static uint64_t f(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static uint64_t ptr(void* p) { return (uint64_t)(uintptr_t)p; }

// ---- peds and entities --------------------------------------------------------
static int playerPed() { return asInt(call(0xD80958FC74E988A6ULL)); }                         // PLAYER_PED_ID
static bool entityExists(int e) { return asBool(call(0x7239B21A38F536BAULL, u(e))); }         // DOES_ENTITY_EXIST
static uint32_t entityModel(int e) { return (uint32_t)asInt(call(0x9F47B058362C84B5ULL, u(e))); } // GET_ENTITY_MODEL
static bool entityDead(int e) { return asBool(call(0x5F9532F3B5CC2551ULL, u(e), 0)); }        // IS_ENTITY_DEAD
static bool playingAnim(int e, const char* dict, const char* name)                             // IS_ENTITY_PLAYING_ANIM
{
    return asBool(call(0x1F0B79228E461EC9ULL, u(e), str(dict), str(name), 3));
}
static int pedGroup(int ped) { return asInt(call(0xF162E133B4E7A675ULL, u(ped))); }           // GET_PED_GROUP_INDEX
static bool inGroup(int ped, int group) { return asBool(call(0x9BB01E3834671191ULL, u(ped), u(group))); } // IS_PED_GROUP_MEMBER
static int groupMember(int group, int n) { return asInt(call(0x51455483CF23ED97ULL, u(group), u(n))); }   // GET_PED_AS_GROUP_MEMBER
static int groupSize(int group)                                                                // GET_GROUP_SIZE
{
    uint64_t leader = 0, count = 0;
    call(0x8DE69FE35CA09A45ULL, u(group), ptr(&leader), ptr(&count));
    return (int32_t)(uint32_t)count;
}
static bool inAnyVehicle(int ped) { return asBool(call(0x997ABD671D25CA0BULL, u(ped), 0)); }  // IS_PED_IN_ANY_VEHICLE
static void entityCoords(int e, float* xyz)                                                    // GET_ENTITY_COORDS
{
    uint64_t* r = call(0x3FEF770D40960D5AULL, u(e), 0);
    for (int i = 0; i < 3; ++i) xyz[i] = r ? asFloat(&r[i]) : 0.0f;
}
static float distance(const float* a, const float* b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
// TASK_GO_TO_ENTITY(ped, target, no time limit, stop `stopAt` m away, at `speed`, 2.0, 0)
static void goToEntity(int ped, int target, float stopAt, float speed)
{
    call(0x6A071245EB0D1882ULL, u(ped), u(target), u(-1), f(stopAt), f(speed), f(2.0f), 0);
}

static bool statInt(uint32_t hash, int32_t* out)                                               // STAT_GET_INT
{
    uint64_t v = 0;
    bool ok = asBool(call(0x767FBC2AC802EF3DULL, hash, ptr(&v), u(-1)));
    *out = (int32_t)(uint32_t)v;
    return ok;
}

// ---- game state -----------------------------------------------------------------
static bool onlineSession() { return asBool(call(0x9DE624D2FC4B603FULL)); }                   // NETWORK_IS_SESSION_STARTED
static bool pauseMenu() { return asBool(call(0xB0034A223497FFCBULL)); }                       // IS_PAUSE_MENU_ACTIVE
static bool loadingScreen() { return asBool(call(0x10D0A8F259E93EC9ULL)); }                   // GET_IS_LOADING_SCREEN_ACTIVE
static bool hudHidden() { return asBool(call(0xA86478C6958735C5ULL)); }                       // IS_HUD_HIDDEN
static float aspectRatio() { return asFloat(call(0xF1307EF624A80D87ULL, 0)); }                // GET_ASPECT_RATIO
static float safeZoneSize() { return asFloat(call(0xBAF107B6BB2C97F0ULL)); }                  // GET_SAFE_ZONE_SIZE

// The in-game date and time as minutes since 1 Jan 0000, so cooldowns measured
// in in-game time survive midnight, month ends and sleeping.
static long long gameMinutes()
{
    long long y = asInt(call(0x961777E64BDAF717ULL));                                       // GET_CLOCK_YEAR
    long long m = asInt(call(0xBBC72712E80257A1ULL));                                       // GET_CLOCK_MONTH (0-11)
    long long d = asInt(call(0x3D10BC92A4DB1D35ULL));                                       // GET_CLOCK_DAY_OF_MONTH
    long long h = asInt(call(0x25223CA6B4D20B7FULL));                                       // GET_CLOCK_HOURS
    long long mi = asInt(call(0x13D2B8ADD79640F2ULL));                                      // GET_CLOCK_MINUTES
    return cooldowns::gameMinutes(y, m, d, h, mi);
}

// ---- the phone ------------------------------------------------------------------
// True while the player has the phone out in his hand.
static bool phoneOnScreen() { return asBool(call(0x2AFE52F782F25775ULL, u(playerPed()))); }  // IS_PED_RUNNING_MOBILE_PHONE_TASK
static void setPhoneScale(float s) { call(0xCBDD322A73D6D932ULL, f(s)); }                     // SET_MOBILE_PHONE_SCALE

// ---- screen output --------------------------------------------------------------
// A message above the minimap.
static void postFeed(const char* text)
{
    call(0x202709F4C58A0424ULL, str("STRING"));  // BEGIN_TEXT_COMMAND_THEFEED_POST
    call(0x6C188BE134E074AAULL, str(text));      // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    call(0x2ED7843F8F801023ULL, 0, 1);           // END_TEXT_COMMAND_THEFEED_POST_TICKER
}

// DRAW_RECT, given by its top-left corner and size in screen fractions.
static void drawRect(float x, float y, float w, float h, int r, int g, int b, int a)
{
    call(0x3A618A217E5154F0ULL, f(x + w / 2), f(y + h / 2), f(w), f(h), u(r), u(g), u(b), u(a), 0);
}

enum Align { AlignCentre = 0, AlignLeft = 1, AlignRight = 2 };  // SET_TEXT_JUSTIFICATION values

// One line of text: centred on x, left from x, or right up to x.
static void drawText(const char* s, float x, float y, float scale, int r, int g, int b, int a, Align align = AlignCentre,
                     int font = 0, bool outline = false)
{
    call(0x66E0276CC5F6B9DAULL, u(font));                          // SET_TEXT_FONT
    call(0x07C837F9A01C34C9ULL, f(scale), f(scale));               // SET_TEXT_SCALE
    call(0xBE6B23FFA53FB442ULL, u(r), u(g), u(b), u(a));           // SET_TEXT_COLOUR
    if (outline) call(0x2513DFB0FB8400FEULL);                      // SET_TEXT_OUTLINE
    call(0x4E096588B13FFECAULL, u(align));                         // SET_TEXT_JUSTIFICATION
    if (align == AlignRight) call(0x63145D9C883A1A70ULL, f(0.0f), f(x)); // SET_TEXT_WRAP
    call(0x25FBB336DF1804CBULL, str("STRING"));                    // BEGIN_TEXT_COMMAND_DISPLAY_TEXT
    call(0x6C188BE134E074AAULL, str(s));                           // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    call(0xCD015E5BB0D96A57ULL, f(x), f(y), 0);                    // END_TEXT_COMMAND_DISPLAY_TEXT
}
