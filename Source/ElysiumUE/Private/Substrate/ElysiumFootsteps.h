#pragma once

#include "Containers/ArrayView.h"      // the species wav pools, held as views over static arrays
#include "CoreMinimal.h"
#include "ElysiumFootstepTuning.h"     // FElysiumFootstepTuning, LogElysiumFootsteps
#include "ElysiumLocomotionSample.h"   // EElysiumWaterLevel — the clock's water arm switches on it
#include "Math/RandomStream.h"         // the coin flip's stream, taken by reference

// The footstep subsystem's RULE MODULE (`docs/architecture/footstep-architecture.md` §4.3): pure
// functions over value types, one per retail function, with every recovered constant named beside
// the address it was read from. Nothing here touches a `UObject`, an entity or a service, so the
// whole of VtMB's stepping arithmetic is asserted with no world
// (`Elysium.Substrate.Footsteps.*`).
//
// The recovery is `docs/vtmb/footsteps.md`; addresses are `vampire.dll` (image base `0x10000000`).
//
// TWO producers live above this file and neither knows about the other:
//   * the NPC animation-event arm (`FElysiumNpc::HandleAnimEvent` -> `0x1026d460`), and
//   * the player's millisecond step clock (`FElysiumPlayer::TickStepClock` -> `0x1011e940`),
// which is why the shared vocabulary is here rather than on either of them.

struct FElysiumClanTemplate;
struct FElysiumSurfaceSounds;
class FElysiumEntityWorld;

// Which body of `HandleAnimEvent` a SPECIES runs instead of `0x1026d460`
// (`docs/vtmb/footsteps.md` §1.7). Every one of retail's overrides returns without calling the
// shared function, so a species row is a replacement and never a modifier.
//
// At global scope rather than inside the namespace because it is a type the NPC leaf holds, the
// same place `EElysiumStance` and `EElysiumWaterLevel` sit.
enum class EElysiumFootstepPolicy : uint8
{
	// No override: the shared `0x1026d460` chain runs. Every classname this port registers except
	// `npc_VTzimisceRunner`.
	Default,
	// The override swallows the id and makes no sound at all — `CNPC_VMingXiao 0x10392a70`.
	Silent,
	// `UTIL_ScreenShake` plus a fixed wav pool: `CNPC_VHengeyokai 0x1037fb60` in shark form and
	// `CNPC_VTzimisceHeadClaw 0x103c1540`.
	Shake,
	// A fixed wav pool and nothing else — `CNPC_VTzimisceRunner 0x103c32c0`.
	CustomWav,
};

// One species row. The pools are fixed wavs: not one of these overrides reads a `surfacedata_t`, a
// template or a cvar (`docs/vtmb/footsteps.md` §1.7, closing line).
struct FElysiumFootstepSpecies
{
	// The MAP classname the row is keyed by. Retail keys on the C++ class; this port stands ONE leaf
	// for every `npc_V*` name, so the registered classname is what distinguishes them — the same
	// test `FElysiumNpc::BypassesKnockbackEligibility` makes for retail's slot-400 bypass.
	const TCHAR* Classname = nullptr;
	// The retail class and the `HandleAnimEvent` the row was read out of, for the diagnostic.
	const TCHAR* RetailClass = nullptr;
	const TCHAR* RetailAddress = nullptr;

	EElysiumFootstepPolicy Policy = EElysiumFootstepPolicy::Default;

	// Whether the override also claims 2052/2053. Only the Hengeyokai does; the two Tzimisce rows
	// claim 2050/2051 and let the run pair fall through to the base handler, which is explicit in
	// `CNPC_VTzimisceRunner::HandleAnimEvent`'s asm (`JMP 0x100146e1` for everything else).
	bool bClaimsRun = false;

	// `UTIL_ScreenShake(amplitude, frequency, duration, radius)` in retail's own units; the radius is
	// Source units. Zero amplitude means the row raises no shake.
	float ShakeAmplitude = 0.0f;
	float ShakeFrequency = 0.0f;
	float ShakeDurationSeconds = 0.0f;
	float ShakeRadiusUnits = 0.0f;

	// The pool the LEFT id draws from and the pool the RIGHT id draws from. A species with ONE flat
	// pool (the Hengeyokai's `RandomInt(0,3)` over four stomps) points both at the same array, which
	// is the honest spelling of "the foot is not read".
	TArrayView<const TCHAR* const> LeftWavs;
	TArrayView<const TCHAR* const> RightWavs;
	// A SECOND sound the same call emits, on the same channel — the Tzimisce vfunc's
	// `TC_Runner/Breath{1..4}` (`RandomInt(0,3)`). Empty on every other row.
	TArrayView<const TCHAR* const> ExtraWavs;
};

namespace ElysiumFootsteps
{

// --- NPC (0x1026d460) ---------------------------------------------------------------------------
//
// Owned by B1. Do not edit this region from the player lane.
//
// `CAI_BaseNPC::HandleAnimEvent 0x10274e30` -> `0x1026d460(mode)`. The chain, in retail's order
// (`docs/vtmb/footsteps.md` §1.2): the three-way player gate, the template/cvar source, the cached
// surface, the level, the coin flip, the emit. Everything here is one of those steps; the
// sequencing is `FElysiumNpc::NpcStep`.

// The four ids, and the mode each carries. Shared with the player half, which SWALLOWS all four
// (`PlayerSwallows` below): the left/right in the name is discarded by the NPC handler — the mode
// is the only thing carried and the foot is re-chosen by a coin flip (§1.1).
inline constexpr int32 EventWalkLeft  = 2050;   // 0x802 — "normal", `0x1026d460(this, 0)`
inline constexpr int32 EventWalkRight = 2051;   // 0x803 — "normal"
inline constexpr int32 EventRunLeft   = 2052;   // 0x804 — "heavy",  `0x1026d460(this, 1)`
inline constexpr int32 EventRunRight  = 2053;   // 0x805 — "heavy"

inline bool IsFootstepEvent(int32 Event)
{
	return Event >= EventWalkLeft && Event <= EventRunRight;
}
// `bHeavy` is `0x1026d460`'s `param_1`: 0 for the walk pair, 1 for the run pair.
inline bool IsHeavyFootstep(int32 Event)
{
	return Event == EventRunLeft || Event == EventRunRight;
}
// True for the LEFT id of either pair (2050, 2052). Retail's shared handler throws this away; only
// the species overrides read it, because their two fixed pools are selected by the flag
// `CNPC_VTzimisceRunner::HandleAnimEvent` passes to `+0x9ac` (1 for 2050, 0 for 2051).
inline bool IsLeftFootstep(int32 Event)
{
	return Event == EventWalkLeft || Event == EventRunLeft;
}

// What one NPC footfall is emitted at. `Volume` reaches the emit UNMODIFIED — there is no duck
// scale, no distance scale and no material scale anywhere on the NPC path, verified at
// `1026d66e`/`1026d68d`. `DistanceUnits` is in SOURCE units, because that is what the cvars and the
// `npctemplate*.txt` keys author and what `ElysiumSoundLevel::FromDistanceUnits` expects.
struct FStepSource
{
	float Volume = 0.0f;
	float DistanceUnits = 0.0f;
};

// Step 3 of `0x1026d460` (`1026d4aa`...`1026d58a`): where the volume and the distance come from.
//
// `footstep_npc_use_templates` (`0x109203f8`, default 1) selects the source. Non-zero reads the four
// `General` keys off the NPC's RESOLVED `npctemplate*.txt` record — `NormalFootfallVol` `+0x24`,
// `NormalFootfallDist` `+0x28`, `HeavyFootfallVol` `+0x2c`, `HeavyFootfallDist` `+0x30` — with the
// loader's own defaults (`0x101d3f10`: 0.45 / 256 / 0.85 / 512) for a block that authors none of
// them, because the loader writes those defaults into the record itself. Zero takes the cvar pair
// instead (`footstep_normal_vol/dist` 0.5/256, `footstep_heavy_vol/dist` 0.85/512).
//
// **A null template on the template path answers the loader defaults**, not the cvars: an NPC with
// no `stattemplate` has no record to read, and the loader's defaults are what a record would have
// carried. The cvar pair is reachable only through the cvar itself, which is what `use_templates`
// means.
FStepSource NpcSource(const FElysiumClanTemplate* Template, bool bHeavy,
	const FElysiumFootstepTuning& Tuning);

// Step 1 of `0x1026d460` (`1026d467`...`1026d499`): the three predicates evaluated on the LOCAL
// PLAYER, any one of which aborts the step before the template lookup.
//
// The gate is GLOBAL, not per-NPC: while a cutscene camera or a conversation is up, no NPC in the
// level makes a footfall.
//
//   `0x10014371` -> `0x1017d630`  player `+0x19c0`  the scripted camera VIEW entity
//   `0x10014ad3`                  player `+0x19cc`  the camera TARGET
//   `0x1000306c`                  player `+0x19b4` with `+0x1ec4 > 0`  a dialogue / CONTROL entity
//                                                                     owns the player
//
// **NAMED DIVERGENCE — the control-entity arm is only partly reproducible.** `+0x19b4` is the entity
// currently driving the player's input, and a conversation is only one of the things that takes it:
// the terminal, keypad and hacking sessions set the SAME field through the same setter
// (`0x1017cef0`, whose callers are the conversation opener and those session openers). This world
// exposes the open conversation and both camera roles; it has no session state for a terminal or a
// keypad until 13.4 builds one, so an NPC walking past a player at a computer is audible here and
// silent in retail. The day that state exists this predicate gains one clause and nothing else
// changes.
bool NpcStepsMuted(const FElysiumEntityWorld& World);

// Step 9 of `0x1026d460` (`1026d626`...`1026d666`): the coin flip, and the name behind it.
//
// `RandomInt(0, 1)` — non-zero takes `stepleft` (`surfacedata_t +0x2c`), zero takes `stepright`
// (`+0x2e`). **The foot the event named is ignored**; the walk/run split is the volume split, not
// the foot split.
//
// Retail's two indices name ONE soundscript each and the engine picks among that script's repeats;
// the port's baked table has already flattened those repeats into a pool with its duplicates
// preserved (`FElysiumSurfaceSounds`), so the variation pick happens here, on the same stream. That
// second draw is the only part of this function retail does inside the sound engine rather than in
// the game DLL — named, because it consumes a draw from `EElysiumRngStream::Footsteps` that retail's
// own stream would not have consumed.
//
// **Null is a legal answer for a resolved surface**: a `surfacedata_t` whose chosen side names no
// script is retail's own silent step (`if (!name || !*name) return;` at `1026d666`), so a
// half-authored surface steps on one foot and not on the other.
const FString* PickNpcWav(const FElysiumSurfaceSounds& Sounds, FRandomStream& Stream);

// --- Species overrides (`docs/vtmb/footsteps.md` §1.7) -------------------------------------------
//
// Retail's overrides, as DATA. Three of the classes have no leaf in this port at all
// (`npc_VMingXiao`, `npc_VHengeyokai` and `npc_VTzimisceHeadClaw` are not registered classnames),
// and `CNPC_VSabbatLeader`'s step is task-driven rather than event-driven so it is not a row here at
// all; `npc_VTzimisceRunner` IS a registered classname and this table is live for it.

// The volume and the sound level every species row emits at. Retail passes `EmitSound` the pair
// (volume 1.0, attenuation 0.8) on both vfuncs (`0x103817f0`, `0x103c4160`). 0.8 is Source's
// `ATTN_NORM`, and the level it stands for is the exact inverse of `0x1026d5e1`'s own `20/(L-50)`:
// `L = 50 + 20/0.8 = 75`. So a species step is `SNDLVL_NORM`, not a computed distance — these rows
// read no `NormalFootfallDist` at all.
inline constexpr float SpeciesVolume = 1.0f;
inline constexpr int32 SpeciesSoundLevelDb = 75;
// `pitch 100` at both vfuncs, which is this seam's 1.0 (`FElysiumBodySound::Pitch` is a multiplier,
// not Source's percentage).
inline constexpr float SpeciesPitch = 1.0f;

// The row for a classname, or null for "no override — run `0x1026d460`". Case-folded, because a map
// authors its classname however it likes and the class registry resolves them case-insensitively.
const FElysiumFootstepSpecies* SpeciesFor(const FString& Classname);

// Whether this row's override claims that id. A row that does not claim it never ran in retail
// either: `CNPC_VTzimisceRunner`'s `JMP 0x100146e1` hands 2052/2053 straight to the base.
bool SpeciesClaims(const FElysiumFootstepSpecies& Row, int32 EventId);

// The wav this row plays for that id, or null when the row plays none (the `Silent` policy, and any
// row whose selected pool is empty). Draws once from the stream, which is retail's own `RandomInt`
// inside the vfunc.
const TCHAR* PickSpeciesWav(const FElysiumFootstepSpecies& Row, int32 EventId,
	FRandomStream& Stream);
// The SECOND sound the same call makes (`TC_Runner/Breath{1..4}`), or null.
const TCHAR* PickSpeciesExtraWav(const FElysiumFootstepSpecies& Row, FRandomStream& Stream);

// The screenshake a `Shake` row raises, REPORTED and not performed.
//
// TODO(footsteps): the screenshake seam belongs to the 2100/2101 werewolf group
// (`werewolf_footstep_shakes 0x103d8ba0`), which is where `UTIL_ScreenShake` is worth building once
// for every producer. Until then a `Shake` row plays its wavs and says out loud that the camera did
// not move. Warns ONCE per row, process-wide, for the reason the anim-event census exists: a shaking
// footfall fires two or three times a second.
void ReportUnimplementedShake(const FElysiumFootstepSpecies& Row);

// Test-only: forget which rows have already warned, so a case can assert the once-per-row rule
// without depending on what ran before it. Nothing in the game calls this.
void ResetUnimplementedShakeReports();


// --- Player (0x1011e940 / 0x1011e430 / 0x10125db0) ----------------------------------------------
//
// `CGameMovement`'s step clock, its emit and the landing, plus `CBasePlayer::UpdatePlayerSound`'s
// hearing stimulus. Every number below is `docs/vtmb/footsteps.md` §2 and §5.2–§5.5.
//
// **Units.** Speeds and radii are SOURCE UNITS per second / Source units, because every retail
// constant here is authored in them; the clock is MILLISECONDS, because `m_flStepSoundTime` is
// (`ReduceTimers 0x1011f520` subtracts `frametime * 1000`). The one conversion to centimetres
// happens at the producer edge through `ElysiumMove::U`, like every other recovered distance.

// Which pool the step draws from. Retail picks a `surfacedata_t*` rather than a pool id — the
// movement's cached one for dry ground, or `physprops->GetSurfaceData(GetSurfaceIndex("ladder"
// | "water" | "wade"))` for the other three (`0x10572554` / `0x10572544` / `0x1057254c`) — so the
// enumerator IS the surfaceprop name the producer resolves, and `LadderSurface()` and friends
// below spell it.
enum class EStepPool : uint8
{
	Dry,      // the movement's cached `m_pSurfaceData` (`CGameMovement+0x9c`)
	Ladder,   // surfaceprop `ladder`
	Water,    // surfaceprop `water`  — classified water level 1
	Wade,     // surfaceprop `wade`   — classified water level >= 2
};

// The surfaceprop names the three non-dry arms resolve, exactly as retail spells them.
const FName& LadderSurface();
const FName& WaterSurface();
const FName& WadeSurface();

// --- §5.2, the clock's constants ----------------------------------------------------------------

// The two speed bands (`0x1011ea30`..`0x1011ea58`). `VelWalk` is carried for the record: the arm
// that reads it (`0x1011eabb`, "3-D speed < velwalk AND the clock is non-zero") is DEAD, because
// `ReduceTimers` clamps the clock at 0 and the clock is therefore always exactly 0 by the time the
// test runs. Nothing in the port may gate on it.
inline constexpr float kDuckedVelWalk = 60.0f;
inline constexpr float kDuckedVelRun  = 80.0f;
inline constexpr float kDuckedFlDuck  = 100.0f;
inline constexpr float kNormalVelWalk = 120.0f;
inline constexpr float kNormalVelRun  = 220.0f;
inline constexpr float kNormalFlDuck  = 0.0f;

// The intervals, milliseconds. `flduck` (0 or 100) is added to whichever one the arm wrote
// (`0x1011ec9c`, `FLD [ESP+0x1c]`) — **not** `velwalk`, which is what the decompiled C at this
// address mislabels it as.
inline constexpr float kIntervalDryWalkMs   = 400.0f;   // `0x1011ebe9`: (walking ? 100 : 0) + 300
inline constexpr float kIntervalDryRunMs    = 300.0f;
inline constexpr float kIntervalSlowMs      = 800.0f;   // `0x44480000`
inline constexpr float kSlowSpeed2DUnits    = 100.0f;   // `0x10450564`, tested with `<=`
inline constexpr float kIntervalLadderMs    = 350.0f;   // `0x43af0000`
inline constexpr float kIntervalWaterWalkMs = 400.0f;   // `0x1011ebc2`
inline constexpr float kIntervalWaterRunMs  = 300.0f;
inline constexpr float kIntervalWadeMs      = 600.0f;   // `0x44160000`

// The wade branch's module-global counter (`0x1070b898`, `0x1011eb42`): phase 0 returns BEFORE
// playing and before the clock is re-armed, the other three play. One wading step in four is
// silent, and the cycle is silent, sound, sound, sound.
inline constexpr int32 kWadePhases = 4;

// The fixed volumes the three wet/ladder arms write, ahead of the common tail below.
inline constexpr float kVolLadder = 0.35f;   // `0x3eb33333`
inline constexpr float kVolWater  = 1.0f;
inline constexpr float kVolWade   = 1.0f;

// The dry volumes, by `surfacedata_t`'s `gamematerial` letter (`+0x70`, cached on the movement at
// `CGameMovement+0xa4`). A jump table over `'D'`..`'V'` (`0x1011ed90`) in which only two letters
// are special; everything else — including a surface that declares no letter at all — is default.
inline constexpr float kVolDirtWalk    = 0.25f;   // `0x10449260`
inline constexpr float kVolDirtRun     = 0.55f;   // `0x10462928`
inline constexpr float kVolVentWalk    = 0.40f;   // `0x1045a3f0`
inline constexpr float kVolVentRun     = 0.70f;   // `0x104492d0`
inline constexpr float kVolDefaultWalk = 0.20f;   // `0x10449198`
inline constexpr float kVolDefaultRun  = 0.50f;   // `0x10449270`

// The common tail (`0x1011ec9c` onward), applied to EVERY arm and not just the dry one.
inline constexpr float kVolDuckScale = 0.35f;   // `0x10462918`
inline constexpr float kVolClamp     = 1.0f;    // `0x10449280`

// --- §5.3, the emit -----------------------------------------------------------------------------
inline constexpr int32 kPlayerSoundLevelDb = 75;   // `0x1011e50b`
inline constexpr int32 kPitchBase          = 95;   // `0x1011e502`
inline constexpr int32 kPitchJitter        = 10;   // `RandomInt(0, 10)`, inclusive — 95..105

// --- §5.4, the landing --------------------------------------------------------------------------

// `max( sqrt(2*sv_jump_boost*sv_gravity) + BaseJumpVelocity*M*(1 + 0.75*H), fall_threshold )`,
// recomputed every `CheckFalling` (`0x10125e04`). With the shipped `sv_jump_boost` 25,
// `sv_gravity` 800, `rules.txt Jumping/BaseJumpVelocity` 185 and the feat-1 scalars M = 1.0,
// H = 0.2 s it is 200 + 185 + 27.75. The shape is deliberate: **you can always land your own jump
// without a hard landing.**
float MaxSafeFallSpeedUnits(float JumpBoost, float Gravity, float BaseJumpVelocity,
	float VerticalScalar, float JumpDurationSeconds, float FallThreshold);
inline constexpr float kMaxSafeFallSpeed = 412.75f;

// `sqrt(2 * sv_gravity * SafeFallDist)` with `SafeFallDist` 240 (`rules+0x3ec`, `0x101e7db0`).
inline constexpr float kSafeFallSpeed = 619.6773f;
// `sqrt(2 * sv_gravity * SupernaturalFallDist)` with `SupernaturalFallDist` 500 (`rules+0x3f0`).
// Read and DISCARDED at `0x10125ffd` — carried so the port's fatal arm is provably the same one.
inline constexpr float kSupernaturalFallSpeed = 894.4272f;

inline constexpr float kFloatingFallReduction = 173.0f;   // `0x104629e4`
inline constexpr float kFallPunchThreshold    = 200.0f;   // `0x104492b8`
inline constexpr float kLandVolSoft  = 0.25f;   // `0x3e800000`
inline constexpr float kLandVolMid   = 0.5f;    // `0x3f000000`
inline constexpr float kLandVolLow   = 0.65f;   // `0x3f266666`
inline constexpr float kLandVolHard  = 0.85f;   // `0x3f59999a`
inline constexpr float kLandVolFatal = 1.0f;    // `0x3f800000`

// --- §5.5, the hearing stimulus -----------------------------------------------------------------
inline constexpr float kPlayerRunSpeedUnits = 128.0f;   // `MiscData PLAYER_RUN_SPEED`, table `+0xfc`
inline constexpr float kHearingDecayUnitsPerSecond = 250.0f;   // `0x104704b8`
// The shipped `sound_volume_table.txt` answers (§2.6). The LIVE path resolves the same rows through
// `FElysiumEntityWorld::GameSoundRadiusUnits`, so a re-authored table wins; these are the recovery,
// and the oracle a headless case compares the table against.
inline constexpr float kHearingRadiusSneak    = 180.0f;   // LEVEL_1
inline constexpr float kHearingRadiusWalk     = 240.0f;   // LEVEL_2
inline constexpr float kHearingRadiusRun      = 240.0f;   // LEVEL_2
inline constexpr float kHearingRadiusJump     = 240.0f;   // LEVEL_2
inline constexpr float kHearingRadiusLandSoft = 180.0f;   // LEVEL_1
inline constexpr float kHearingRadiusLandHard = 240.0f;   // LEVEL_2

// --- The clock ----------------------------------------------------------------------------------

// What one pass of `UpdateStepSound` is handed. Every field is something only a body that moved
// can know, which is why the whole of it arrives from `IElysiumEmbodiment::SamplePlayerLocomotion`
// rather than being asked for term by term.
struct FStepClockIn
{
	float Speed2D = 0.0f;   // Source units/s, horizontal
	float Speed3D = 0.0f;   // Source units/s, including the vertical
	// `FL_DUCKING` — the hull is the SMALL one, which is `FElysiumLocomotionSample`'s `Ducked` and
	// `Rising` stances. Both a band selector and the `x0.35` tail.
	bool bDucked = false;
	// `GetMoveType() == 10` (`MOVETYPE_LADDER`). **Never true, in retail either**: VtMB's
	// `PlayerMove` movetype switch has no ladder arm and no map places a ladder entity
	// (`docs/vtmb/source_movement.md` → "Ladders: VtMB has none"), so this whole arm is dead code
	// in the shipped game. Reproduced anyway, because it is one of the four the switch spells and
	// a port that dropped it would be reading a different function.
	bool bOnLadder = false;
	bool bOnGround = false;
	EElysiumWaterLevel Water = EElysiumWaterLevel::None;
	// `surfacedata_t`'s `gamematerial`, the letter `0x1011ed90`'s jump table switches on. Empty is
	// the ordinary case — 37 of the 63 shipped entries declare none anywhere in their `base` chain
	// — and takes the default pair.
	FString GameMaterial;
	float PcVol = 0.5f;              // `footstep_pc_vol`
	bool bServerFootsteps = true;    // `sv_footsteps`
};

// What the pass decided.
struct FStepClockOut
{
	// The value written back onto `m_flStepSoundTime`, `flduck` already added.
	float NextIntervalMs = 0.0f;
	// The final volume: the arm's own, times the duck scale, times `footstep_pc_vol`, clamped at 1.
	float Volume = 0.0f;
	EStepPool Pool = EStepPool::Dry;
	// The wade branch's phase-0 pass: no sound, and **the clock is deliberately not re-armed**, so
	// the next move retries immediately.
	bool bSilentPhase = false;
};

// `ReduceTimers` (`0x1011f520`) and `UpdateStepSound` (`0x1011e940`) as one call, because the port
// has one caller and splitting them would let a producer forget the decrement.
//
// `StepSoundMs` is `m_flStepSoundTime` (`player+0x2328`), decremented by `DtMs` and **clamped at
// zero** — the clamp that kills the `velwalk` arm — then re-armed by whichever arm fires.
// `WadePhase` is the module-global counter at `0x1070b898`, held by the caller so a save can carry
// it. Returns true when a step should be played, in which case `Out` is filled.
//
// The early returns it reproduces, in retail's order (§2.2): the clock, `sv_footsteps`, "no ladder
// and no ground and no water", and "2-D speed <= 0". `FL_FROZEN | FL_ATCONTROLS` and
// `MOVETYPE_NOCLIP` are the caller's, because the sample already answers them: a frozen body
// publishes a standing-still record and a noclipping one publishes `bOnGround == false` with no
// water and no ladder, which is early return #5.
bool AdvanceStepClock(float& StepSoundMs, float DtMs, int32& WadePhase, const FStepClockIn& In,
	FStepClockOut& Out);

// The dry arm's volume, tail included: the `gamematerial` pair, then `x0.35` while ducked, then
// `x footstep_pc_vol`, then clamped at 1 (`0x1011ec9c`..`0x10449280`). Split out from the clock
// because it is the one piece a producer may want to ask about on its own, and because the three
// wet arms run the same tail over a constant.
float PlayerDryVolume(const FString& GameMaterial, bool bWalking, bool bDucked, float PcVol);

// The forced landing step's volume (`CheckFalling 0x10125db0`, §2.4). Retail's own ladder, arms in
// its order — and two of them are counter-intuitive and reproduced as shipped:
//
//  * the `0.5` / `0.65` pair is INVERTED (the faster fall is the quieter one), and
//  * with the shipped rules both are unreachable on ordinary ground, because the hard band starts
//    at `kMaxSafeFallSpeed` 412.75 and `kSafeFallSpeed * 0.5` is 309.8. The only way in is the
//    `IsFloating` reduction, which is why it is a real write.
//
// `FallSpeedUnits` is IN/OUT: `player->m_flFallVelocity -= 173` is a write to the field, not a
// local (`0x104629e4`). Returns 0 when no step is forced.
float LandingStepVolume(float& FallSpeedUnits, bool bInWater, bool bGroundIsFloating,
	float MaxSafeFallSpeed = kMaxSafeFallSpeed, float SafeFallSpeed = kSafeFallSpeed);

// --- The hearing stimulus -----------------------------------------------------------------------

// What `CBasePlayer::UpdatePlayerSound` (`0x1016b480`) reads, per think, off the player.
struct FHearingIn
{
	// `m_nButtons & IN_JUMP`. The port's stand-in is the sample's jump push window
	// (`JumpHoldRemaining > 0`): VtMB's jump is a HELD push, so the window is open exactly while
	// the button is doing work. It closes earlier than retail's button bit does, and the radius
	// decay covers the difference (§2 port notes).
	bool bJumpHeld = false;
	bool bOnGround = false;
	// Player animation state `+0x1db4` == 8, and == 10 or 11. **The enum is unrecovered**
	// (§4, "Unrecovered"): the port maps them off the landing the same frame's `CheckFalling`
	// classified — the soft band (`fallVel < kMaxSafeFallSpeed`) is 8, the hard band is 10/11 —
	// which is the split the two `sound_volume_table.txt` rows are named after.
	bool bLandSoft = false;
	bool bLandHard = false;
	float Speed3D = 0.0f;   // `|m_vecAbsVelocity|`, Source units/s
	bool bDucked = false;   // `FL_DUCKING` — **ducking is the whole of "sneaking"** on this path
	float RunSpeedUnits = kPlayerRunSpeedUnits;
};

// The category the stimulus carries this think, or `NAME_None` for silence — which is retail's
// volume 0 and not an error. Retail's priority order exactly (§2.5): jump, then the two landings,
// then the three locomotion rows; airborne and not jumping is silent, and so is standing still.
// The water level is never consulted, so wading emits footsteps and swimming emits nothing.
FName HearingCategory(const FHearingIn& In);

// The shipped `sound_volume_table.txt` radius for one of the six player categories, Source units.
// 0 for a name that is not one of them (including `NAME_None`).
float HearingRadiusUnits(FName Category);

// The reserved slot's volume law (`0x1016b642`..): it **rises instantly** and **falls at 250 units
// per second**, so a sprint heard once keeps ringing for about a second of AI polling.
float DecayHearingRadiusUnits(float CurrentUnits, float TargetUnits, float DeltaSeconds);

// `CBasePlayer::HandleAnimEvent` swallows 2050-2053: the player's own clips carry the same footfall
// records the cast's do, and the player steps off the millisecond clock instead.
bool PlayerSwallows(int32 EventId);

} // namespace ElysiumFootsteps
