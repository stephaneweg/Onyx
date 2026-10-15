//
// parts.h -- what a ship is made of (a hull, two wings, an engine, its paint) and what that makes of it (its
// stats), the pilots, the speed classes, and the gadgets: their effect, who gets them how often, their names.
// docs/cosmozoom/DESIGN.md sections 4 to 6. The names are English; the game shows them through TR () (lang/fr.txt).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_PARTS_H
#define CZ_PARTS_H

#include "math3d.h"

#ifndef TRN
#define TRN(s) (s)			// (uikit/lang.h: a word given to TR () where it is drawn)
#endif

namespace cz {

// ---- the parts: each gives stat points (1..5) ------------------------------------------------------------------------
struct Part { const char *name, *model, *look; int speed, accel, handling, weight; const char *perk; };

enum { NHULL = 6, NWING = 4, NENGINE = 5, NPAINT = 10, NPATTERN = 7 };

// (the user's concept sheet, 2026-10-15: docs/cosmozoom/DESIGN.md section 4)
static const Part HULLS[NHULL] = {
	{ TRN ("Comet Chaser"), "hull_comet", TRN ("The all-rounder"),     3, 3, 3, 3, "" },
	{ TRN ("Nova Streak"), "hull_needle", TRN ("The speedster"),       5, 2, 3, 2, "" },
	{ TRN ("Star Drifter"), "hull_whale", TRN ("The heavy one"),       4, 2, 2, 5, "" },
	{ TRN ("Manta Ray"), "hull_manta", TRN ("The slalom expert"),   2, 3, 5, 1, "" },
	{ TRN ("Plasma Runner"), "hull_pod", TRN ("The quick starter"),   3, 5, 2, 2, "" },
	{ TRN ("Ringmaster"), "hull_cube", TRN ("Tough and steady"),    4, 3, 1, 5, "" },
};
static const Part WINGS[NWING] = {
	{ TRN ("Stubby Fins"), "wing_stubby", "", 0, 1, -1, 0, "" },
	{ TRN ("Ray Wings"), "wing_ray", "", 1, -1, 0, 0, TRN ("Jumps fly further") },
	{ TRN ("Delta Wings"), "wing_solar", "", -1, 0, 1, 0, "" },
	{ TRN ("Swept Fins"), "wing_swept", "", 0, 0, 0, 0, TRN ("Drifts charge faster") },
};
static const Part ENGINES[NENGINE] = {
	{ TRN ("Ion Jet"), "engine_ion", "", 0, 0, 0, 0, "" },
	{ TRN ("Side Pods"), "engine_pods", "", -1, 1, 0, 0, "" },
	{ TRN ("Truss Nacelles"), "engine_twin", "", 0, 0, 1, -1, "" },
	{ TRN ("Plasma Pods"), "engine_star", "", 1, 0, -1, 0, TRN ("Boosts last longer") },
	{ TRN ("Ring Drive"), "engine_ring", "", 0, -1, 1, 1, "" },
};
// the swatches: magenta, orange, yellow, lime, cyan, teal, blue, purple, red, white
static const unsigned PAINTS[NPAINT] = { 0xFF2EB8, 0xFF7A1A, 0xFFDD1A, 0x9BEA1E, 0x1EC8F0, 0x14B8A6, 0x2F6BFF, 0x8B4DFF, 0xF2304B, 0xF4F4F8 };
static const char *const PAINT_NAMES[NPAINT] = { TRN ("Magenta"), TRN ("Orange"), TRN ("Yellow"), TRN ("Lime"), TRN ("Cyan"), TRN ("Teal"), TRN ("Blue"), TRN ("Purple"), TRN ("Red"), TRN ("White") };
static const char *const PATTERNS[NPATTERN] = { TRN ("Plain"), TRN ("Stripe"), TRN ("Two-tone"), TRN ("Racing"), TRN ("Checker"), TRN ("Rainbow"), TRN ("Split") };

struct Build { int hull, wing, engine, paint, paint2, pattern; };

// ---- the speed classes -------------------------------------------------------------------------------------------------
enum { NCLASS = 4, CLASS_MIRROR = 3 };
struct SpeedClass { const char *name; float top; float aiSkill; float rubber; };
static const SpeedClass CLASSES[NCLASS] = {
	{ TRN ("Orbit"),  30, 0.86f, 0.03f },
	{ TRN ("Hyper"),  40, 0.93f, 0.06f },
	{ TRN ("Warp"),   50, 0.98f, 0.06f },
	{ TRN ("Mirror"), 50, 0.98f, 0.06f },
};

// ---- the playing numbers from the parts (DESIGN.md 4.5) ----------------------------------------------------------------
struct Stats
{
	int speed, accelPts, handling, weightPts;	// 1..5 (the hangar's bars)
	float topMul;					// x the class's top speed
	float accel;					// m/s² (scaled by the class: see Race)
	float turn;					// rad/s
	float weight;					// the bump's mass
	float toTop;					// seconds from 0 to the top speed
	bool fastCharge, longBoost, longJump;		// the perks
};

static inline int clampi (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline Stats statsOf (const Build &b)
{
	const Part &H = HULLS[clampi (b.hull, 0, NHULL - 1)], &W = WINGS[clampi (b.wing, 0, NWING - 1)], &E = ENGINES[clampi (b.engine, 0, NENGINE - 1)];
	Stats s;
	s.speed = clampi (H.speed + W.speed + E.speed, 1, 5);
	s.accelPts = clampi (H.accel + W.accel + E.accel, 1, 5);
	s.handling = clampi (H.handling + W.handling + E.handling, 1, 5);
	s.weightPts = clampi (H.weight + W.weight + E.weight, 1, 5);
	static const float TOP[5] = { 0.94f, 0.97f, 1.00f, 1.03f, 1.06f }, TOTOP[5] = { 4.2f, 3.6f, 3.0f, 2.5f, 2.0f };
	static const float TURN[5] = { 70, 80, 90, 100, 110 }, WEIGHT[5] = { 0.7f, 0.85f, 1.0f, 1.2f, 1.4f };
	s.topMul = TOP[s.speed - 1];
	s.toTop = TOTOP[s.accelPts - 1];
	s.turn = TURN[s.handling - 1] * PI / 180 * 1.25f;
	s.weight = WEIGHT[s.weightPts - 1];
	s.accel = 0;
	s.fastCharge = b.wing == 3;
	s.longJump = b.wing == 1;
	s.longBoost = b.engine == 3;
	(void) 0;
	return s;
}

// ---- the gadgets (DESIGN.md 6) -------------------------------------------------------------------------------------------
enum { FX_BOOST, FX_SHIELD, FX_SHOT, FX_DROP, FX_WELL, FX_BEAM, FX_EMP, FX_NEBULA, FX_WARP };
enum
{
	IT_NONE, IT_BOOST, IT_TRIPLE, IT_SHIELD, IT_BUBBLE, IT_GOO, IT_WELL, IT_BEAM, IT_EMP, IT_NEBULA, IT_WARP, IT_COUNT
};

struct ItemInfo
{
	const char *name;		// (English; TR in the game)
	const char *help;		// one line (the manual, the hangar's tips)
	int effect, count;		// FX_*, charges
	int odds[5];			// % by place: 1st, 2nd-3rd, 4th-5th, 6th-7th, 8th (DESIGN.md 6.2)
	unsigned color;			// its icon's colour
	const char *model;		// what is drawn on the track ("": by code)
};

static const ItemInfo ITEMS[IT_COUNT] = {
	{ "", "", FX_BOOST, 0, { 0, 0, 0, 0, 0 }, 0, "" },
	{ TRN ("Hyper Boost"), TRN ("A burst of speed"),                                   FX_BOOST,  1, { 15, 20, 20, 10, 5 },  0xFF8C2B, "" },
	{ TRN ("Triple Hyper"), TRN ("Three bursts of speed, one by one"),                  FX_BOOST,  3, { 0, 5, 15, 20, 15 },   0xFF5FA2, "" },
	{ TRN ("Shield Dome"), TRN ("Blocks one hit"),                                     FX_SHIELD, 1, { 30, 15, 10, 5, 0 },   0x4CC9F0, "" },
	{ TRN ("Plasma Bubble"), TRN ("Fired ahead (or back): the ship it touches floats in a bubble"), FX_SHOT, 1, { 20, 20, 15, 10, 5 }, 0xFF5FA2, "bubble" },
	{ TRN ("Ion Goo"), TRN ("A sticky puddle dropped behind you"),                 FX_DROP,   1, { 30, 15, 10, 5, 0 },   0x3DDC97, "goo" },
	{ TRN ("Gravity Well"), TRN ("A little vortex that pulls the ships in"),            FX_WELL,   1, { 5, 10, 10, 10, 5 },   0x8B5CF6, "spring" },
	{ TRN ("Tractor Beam"), TRN ("Pulls you along behind the ship ahead"),              FX_BEAM,   1, { 0, 5, 10, 15, 15 },   0xFFD23F, "" },
	{ TRN ("EMP Burst"), TRN ("Static for the ships behind you: their gadgets are jammed"), FX_EMP, 1, { 0, 5, 5, 5, 0 },     0xC8A8FF, "" },
	{ TRN ("Ion Storm"), TRN ("A storm cloud flies to the leader and slows them down"), FX_NEBULA, 1, { 0, 0, 5, 10, 15 }, 0x8B5CF6, "nebula" },
	{ TRN ("Warp Star"), TRN ("A warp field: very fast, and nothing stops you"),   FX_WARP,   1, { 0, 0, 0, 10, 40 },    0xFFD23F, "warpstar" },
};

// ---- the pilots: eight racers in closed helmets, one a team (DESIGN.md 5) ------------------------------------------
struct Pilot { const char *name, *team, *teamFr; unsigned color, trim; const char *model; Build build; const char *number; };
enum { NPILOT = 8 };
static const Pilot PILOTS[NPILOT] = {
	{ "Kai Moreno",  "Ember",    "Braise",     0xF2384B, 0xF4F4F8, "pilot_pepper",   { 1, 3, 0, 8, 9, 1 }, "07" },
	{ "Lumi Sato",   "Zenith",   "Zénith", 0xF2F6FF, 0x22E6FF, "pilot_bruno",    { 0, 0, 1, 9, 4, 3 }, "01" },
	{ "Aya Okafor",  "Volt",     "Volt",       0xFFD21F, 0x1E2A6A, "pilot_bolt",     { 4, 0, 3, 2, 6, 2 }, "22" },
	{ "Nils Berg",   "Glacier",  "Glacier",    0x3DE0FF, 0xF4F4F8, "pilot_zib",      { 3, 2, 0, 4, 9, 1 }, "14" },
	{ "Zara Haddad", "Orchid",   "Orchidée", 0xFF2E9A, 0xF4F4F8, "pilot_beep", { 0, 3, 1, 0, 9, 3 }, "09" },
	{ "Teo Rossi",   "Verde",    "Verde",      0x7CF06A, 0x1E2A6A, "pilot_bloop",    { 2, 3, 2, 3, 6, 2 }, "33" },
	{ "Mika Lind",   "Nebula",   "Nébuleuse", 0x7A5CFF, 0xFF5FA2, "pilot_squiggle", { 3, 1, 0, 7, 0, 1 }, "18" },
	{ "Ravi Kapoor", "Sunburst", "Soleil",     0xFF8A1F, 0xFFD21F, "pilot_kiki",     { 5, 3, 4, 1, 2, 1 }, "42" },
};

// The points of a Grand Prix race, by place
static const int RACE_POINTS[8] = { 10, 8, 6, 5, 4, 3, 2, 1 };

} // namespace cz

#endif
