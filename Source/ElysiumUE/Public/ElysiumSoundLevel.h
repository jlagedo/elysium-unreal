#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundAttenuation.h"   // FSoundAttenuationSettings — returned by value

// Source's SOUND LEVEL, in dB, and the distance law it buys — the engine half of the footstep
// subsystem (`docs/architecture/footstep-architecture.md` §4.6, contract 8). The recovery is
// `docs/vtmb/footsteps.md` §3, "The engine's distance law".
//
// **This is not `sound_volume_table.txt`.** `FElysiumSoundLevel` in
// `Substrate/ElysiumSoundVolumeTable.h` is VtMB's AI-HEARING level (an index into an authored
// radius table that NPC senses read). What lives here is the ENGINE's `soundlevel_t`: the dB number
// `IEngineSound::EmitSound` takes and turns into a falloff. A step raises both, and they are
// unrelated numbers.
//
// Three functions from two DLLs, and they use DIFFERENT reference dB:
//
//   * `vampire.dll 0x10228350` turns an AUTHORED DISTANCE into a level:
//     `level = (int)(20*log10(dist / 36) + 40)` when `dist > 0`, else `0`. Read from the listing:
//     `FMUL [0x1047aa18]` (1/36), `FLDLG2`/`FYL2X` (log10), `FMUL [0x104704a8]` (20),
//     `FADD [0x10462950]` (40), `JMP __ftol` (truncation toward zero, not rounding).
//   * `engine.dll 0x20119fe0` (`DIST_MULT_TO_SNDLVL`, inlined at `0x2011a19e`) relates a level to
//     the channel's `dist_mult` through `snd_refdb` = **60**, not 40:
//     `dist_mult(L) = 10^((60 - L)/20) / 36`, i.e. the REFERENCE DISTANCE of a level is
//     `D_ref(L) = 36 * 10^((L - 60)/20)`.
//   * `engine.dll 0x2011a0b0` (`SND_GetGain`) is the gain itself — see `Gain` below.
//
// Putting the first two together is the identity the whole model rests on:
// `d_authored / D_ref(L) == 10` exactly. **The authored `NormalFootfallDist` / `HeavyFootfallDist`
// is the distance at which the step has fallen 20 dB**, not the distance at which it dies. A
// 300-unit walk step is level 58, reference distance 28.6 units, audible (gain >= `snd_gain_min`
// 0.01) out to 2860.
namespace ElysiumSoundLevel
{
	// ---------------------------------------------------------------------------------------
	// THE CONSTANT BLOCK. Every number the model is built from, with the object it was read from.
	//
	// A3: recovered in wave 1 (`docs/vtmb/footsteps.md` §3.1) and no longer standing in for
	// A3: anything. The one term of `SND_GetGain` this file deliberately does NOT apply is
	// A3: `snd_foliage_db_loss` — see `FoliageDbLossPer1200Units` below, which is the single named
	// A3: divergence in the model.
	// ---------------------------------------------------------------------------------------

	// `snd_refdist`, engine.dll ConVar `0x21307378`, in Source units.
	inline constexpr float RefDistUnits = 36.0f;
	// `snd_refdb`, ConVar `0x21315bd8`. The dB a sound at `snd_refdist` is defined to be.
	inline constexpr float RefDb = 60.0f;
	// `snd_gain` / `snd_gain_max` / `snd_gain_min`, ConVars `0x213109d8` / `0x21310658` /
	// `0x21310470`. The floor is 0.01, which is -40 dB and where a sound stops mattering.
	inline constexpr float SndGain = 1.0f;
	inline constexpr float SndGainMax = 1.0f;
	inline constexpr float SndGainMin = 0.01f;
	// `SND_GetGain`'s near-field clamp: `relative` is floored at 0.1 (`0x201736e0`), so the raw
	// gain never exceeds 10 before compression.
	inline constexpr float NearFieldRelativeFloor = 0.1f;
	// The soft compressor above `0.5` (`0x20173680`):
	// `gain = snd_gain_max * (1 - 1/(gain^e * 2 * 2^e))`, with
	// `e = L > 90 ? 2.5 - (L - 90) * 1.7 / 50 : 2.5` (`0x20188080` 2.5, `0x20188084` 1.7,
	// `0x20188090` 90, `0x201734ec` 50). It is continuous at 0.5 and asymptotic to 1.
	inline constexpr float CompressionKnee = 0.5f;
	inline constexpr float CompressionExponent = 2.5f;
	inline constexpr int32 CompressionLevelKnee = 90;
	inline constexpr float CompressionExponentSlope = 1.7f;
	inline constexpr float CompressionLevelSpan = 50.0f;
	// Below the floor a short linear knee takes the gain to zero at twice the audible range, then
	// pins it at 0.001 (`0x3a83126f`). The port culls at the floor instead — see `MakeAttenuation`.
	inline constexpr float SilentGain = 0.001f;
	// `snd_foliage_db_loss`, ConVar `0x213108b0`: 4 dB of extra loss per 1200 units
	// (`0x20188098`), folded into `relative` in this build's `SND_GetGain`.
	//
	// **The one named divergence.** It is NOT applied here. Stock Source gates the same term behind
	// a foliage trace and the port has no such trace; A3's own reach table is stated "ignoring
	// foliage loss" and §3.6 asks for a falloff at `D_ref * 100`, which is that table. Applying it
	// would shorten a level-58 step's reach from 2860 to about 1570 units. Named so the day the
	// gate is recovered the change is one branch here.
	inline constexpr float FoliageDbLossPer1200Units = 4.0f;

	// `vampire.dll 0x10228350`'s own `+40`, which is NOT the engine's `snd_refdb`. It exists only
	// inside `FromDistanceUnits`; every other function here is on the engine's 60 dB scale.
	inline constexpr float AuthoredFloorDb = 40.0f;
	// `20*log10` — the pressure (not power) decade, so gain halves every distance doubling.
	inline constexpr float DbPerDecade = 20.0f;

	// `0x1026d460`'s inline attenuation: a level at or below this takes the fixed value below
	// instead of the hyperbola (`iVar3 < 0x33` is `level <= 50`).
	inline constexpr int32 AttenuationPivotDb = 50;
	inline constexpr int32 AttenuationNumerator = 20;   // the `0x14` in `0x14 / (level - 0x32)`
	inline constexpr float AttenuationFloor = 4.0f;     // `_DAT_10449148`, a double

	// `CGameMovement::PlayStepSound` (`1011e430`) emits every player step at 75 (`0x4b`); an NPC's
	// level is computed per step from its template/cvar distance.
	inline constexpr int32 PlayerStepLevelDb = 75;

	// `0x10228350` verbatim. `DistUnits` is in SOURCE UNITS, because that is what the cvars and the
	// `npctemplate*.txt` keys author. Non-positive distance answers 0, which is retail's
	// `SNDLVL_NONE` — `dist_mult == 0`, a sound that does not attenuate at all.
	int32 FromDistanceUnits(float DistUnits);

	// `0x1026d460`'s inline attenuation, integer division included:
	// `level > 50 ? 20/(level - 50) : 4.0`.
	//
	// **Inert in VtMB.** It is consumed only by `CPASAttenuationFilter` (`0x1019d4b0`), a
	// server-side recipient cull at `2000 / attenuation` units whose loop is skipped entirely when
	// `maxClients == 1` (`docs/vtmb/footsteps.md` §3.5). It shapes nothing the player hears and is
	// reproduced so the recovered values stay assertable — `MakeAttenuation` is built from the
	// SOUND LEVEL, never from this.
	float SourceAttenuation(int32 LevelDb);

	// `D_ref(L) = 36 * 10^((L - 60)/20)` — the range at which `dist * dist_mult == 1`. Inside it
	// the gain is compressed towards 1; outside it the gain is `D_ref / d`.
	float ReferenceDistanceUnits(int32 LevelDb);
	float ReferenceDistanceCm(int32 LevelDb);

	// `D_ref(L) * 10` — the -20 dB point, and the identity that makes the two DLLs agree: this is
	// the distance `FromDistanceUnits` was handed. The designer-visible number.
	float AuthoredDistanceUnits(int32 LevelDb);

	// `D_ref(L) * 100` — where the gain reaches `snd_gain_min` (0.01, -40 dB). Retail's own knee
	// carries it to zero over the next `D_ref * 100`; the port culls here instead.
	float AudibleRangeUnits(int32 LevelDb);
	float AudibleRangeCm(int32 LevelDb);

	// `SND_GetGain` (`engine.dll 0x2011a0b0`) as a pure function of the level and the distance in
	// SOURCE UNITS, with the foliage term omitted (see `FoliageDbLossPer1200Units`). This is the
	// curve `MakeAttenuation` samples, exposed so it can be asserted directly.
	float Gain(int32 LevelDb, float DistUnits);

	// The attenuation settings a body sound of this level plays with: `Gain` sampled onto an
	// `EAttenuationDistanceModel::Custom` curve out to `AudibleRangeCm`.
	//
	// Custom rather than `::Inverse` because Unreal's `Inverse` pins its own reference distance at
	// `0.02 * FalloffDistance` and cannot hold a reference distance and a cull distance
	// independently; and rather than a sphere extent plus `::Inverse` because the near field is not
	// flat — `SND_GetGain` compresses it towards 1 through a knee at gain 0.5, which only a sampled
	// curve reproduces. The result is -6 dB per doubling beyond `2 * D_ref` exactly, retail's own
	// near-field shape inside it, and silence at the recovered floor.
	FSoundAttenuationSettings MakeAttenuation(int32 LevelDb);
}
