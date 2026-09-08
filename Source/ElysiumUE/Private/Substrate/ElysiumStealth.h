#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

class FElysiumCombatCharacter;
class FElysiumEntity;
class FElysiumEntityWorld;
class FElysiumPlayer;
struct FElysiumStealthTables;

// The rules over the player's stealth target surface
// (`docs/vtmb/stealth.md`).
//
// The storage is `FElysiumStealthSurface` on `FElysiumPlayer` (retail's `+0x1c6c..+0x1c8c`); the
// authored numbers are `FElysiumStealthTables` in the rulebook (K9). This is the third piece: what
// the two produce together when the player think reaches its 0.1 s deadline.
//
// `Recompute` is the whole recovered rule as a PURE function over explicit inputs — no world, no
// services, no clock — so every threshold, the equality rule, the torch override, the fallback arm
// and the normalization are assertable with nothing built. `TickPlayerSurface` is the thin wiring
// half that samples one body point through the embodiment and hands the result to it.
//
// Nothing here decides whether an observer can see anything. §5.5.3 consumes the committed surface;
// this only produces it.

namespace ElysiumStealth
{
	// `m_flNextStealthUpdate` advances by this after every due pass, whether or not the pass
	// committed. One body point is refreshed per pass, so a full feet/centre/head cycle is ~0.3 s
	// and that lag is part of the transaction.
	inline constexpr double UpdateIntervalSeconds = 0.1;

	// `(feet + centre + head) * 0.083325` — the recovered raw-aggregate constant, applied verbatim.
	inline constexpr float RawLightScale = 0.083325f;

	// The `m_flLightOnMe` value the non-stealth fallback writes. It is a sentinel, not a light
	// level: nothing indexes a table with it, and it is how a reader tells "not in the eligible
	// stealth state" from "standing in the dark".
	inline constexpr float InactiveLightSentinel = -4.0f;

	// CHOSEN, NOT RECOVERED — the configured world light minimum/maximum. Retail clamps the raw
	// aggregate to a configured pair and renormalizes over the remaining range; the pair's authored
	// source is not recovered, and retail's per-point sample is a lightmap value on a scale
	// `1 / 0.083325 = 12` implies is [0, 4] per point. This runtime's own seam contract
	// (`IElysiumEmbodiment::QueryLightAtPoint`) is normalized 0..1 per point instead, so the
	// configured maximum is set to what three fully lit samples produce through the recovered
	// constant. The arithmetic is therefore the recovered one and only the pair is ours; replace
	// the pair, not the formula, when the authored values are recovered.
	inline constexpr float DefaultWorldLightMin = 0.f;
	inline constexpr float DefaultWorldLightMax = 3.f * RawLightScale;

	// The vertical body column the three points are taken on, as fractions of feet -> eye. The two
	// outer weights are recovered (`0.125` / `0.875`). CHOSEN, NOT RECOVERED: the middle one — the
	// recovery names only the pair, and the midpoint is the one place a point called "centre" can
	// sit. Replace the number, not the column, if the third weight is recovered.
	inline constexpr float FeetSampleWeight = 0.125f;
	inline constexpr float CentreSampleWeight = 0.5f;
	inline constexpr float HeadSampleWeight = 0.875f;

	// The active usable weapon that forces normalized light to 1.0 — you cannot hide behind a lit
	// torch. Compared against the item entity's own registered classname.
	inline const TCHAR* TorchWeaponClass = TEXT("item_w_torch");

	// SEAM: retail's `debug_stealth_light` replaces the normalized aggregate with `value * 0.1` when
	// enabled. It is a retail developer console variable, and this project's debug surface is Cog
	// rather than a cvar — but the surface has no Cog window of its own yet, and adding one to carry
	// a single override is not wired here. Nothing here reads an override; the recompute is a
	// pure function over explicit inputs, so the override lands as one more input on
	// `FRecomputeInputs` whenever a stealth Cog tab exists to drive it.

	// How long a pending observer offer defends its distance before the next pass may replace it
	// with a further one. CHOSEN: twice `ElysiumNpcSense::PlayerLosCadenceSeconds`, so an observer
	// that stopped offering has missed two of its own sight passes before it is let go — which is
	// the shortest window that cannot drop an observer mid-cadence.
	inline constexpr double ObserverStaleSeconds = 4.0;

	// The Sneaking rating is capped at 10 before the table lookup, independently of the feat's own
	// `MaxValue`: the matrices author eleven columns and a patched `feats.txt` raising the cap must
	// not read off the end of a row.
	inline constexpr int32 MaxStealthRow = 10;

	// The pure rule.

	struct FRecomputeInputs
	{
		// Step 1. False returns without manufacturing replacement values — the surface keeps what it
		// last committed. Nothing in this runtime sets it false: the light seam always answers (a
		// world with no rig reports full light by contract), so it exists for the recovered arm and
		// for the test that drives it.
		bool bLightServiceAvailable = true;

		// Step 3. The eligibility predicate's answer. False installs the fallback arm below; it does
		// NOT skip the pass.
		bool bEligible = false;

		// Step 4. `item_w_torch` is the active usable weapon.
		bool bTorchEquipped = false;

		// Step 2. The Sneaking feat, already resolved through the ordinary sheet path (which is
		// where the clamped `trigger_stealth_mod` aggregate enters) and already capped at 10.
		int32 Sneaking = 0;

		// The RETAINED triplet, with this pass's one refreshed sample already written into it.
		float Samples[3] = { 1.f, 1.f, 1.f };

		float WorldLightMin = DefaultWorldLightMin;
		float WorldLightMax = DefaultWorldLightMax;
	};

	struct FRecomputeResult
	{
		// False only on the unavailable-service arm: the caller leaves the surface alone and does
		// not bump its generation.
		bool bCommitted = false;
		bool bEligible = false;
		float LightOnMe = InactiveLightSentinel;
		float VisionScalar = 1.f;
		float ConeScalar = 1.f;
		float HearingReductionCm = 0.f;   // the table's Source units, converted once here
		int32 LightRow = 0;
		int32 StealthRow = 0;
	};

	// `(feet + centre + head) * 0.083325`, clamped into [Min, Max], shifted down by Min and divided
	// by the remaining range. A degenerate range answers 1.0 — full light — rather than dividing by
	// zero and handing the player a free pass out of a configuration error.
	float NormalizeBodyLight(float Feet, float Centre, float Head, float Min, float Max);

	// The descending-threshold walk. At or above 1.0 is `Light0`; below that, advance while the
	// value is less than or equal to the current threshold, so EQUALITY ENTERS THE NEXT DARKER ROW.
	int32 SelectLightRow(const FElysiumStealthTables& Tables, float Normalized);

	FRecomputeResult Recompute(const FElysiumStealthTables& Tables, const FRecomputeInputs& In);

	// The wiring half.

	// One player think's worth of stealth work: the 0.1 s cadence gate, the one rotating
	// `QueryLightAtPoint`, the recompute, and the observer snapshot commit. Safe on a player with no
	// world, no embodiment and no rulebook.
	void TickPlayerSurface(FElysiumPlayer& Player, double Now);

	// Promote the pending observer offer into the published snapshot, dropping an incumbent whose
	// observer has gone stale, inert or invalid. Runs from the player think — AFTER gameplay has
	// committed — and bumps the snapshot's generation only when the published values change.
	void CommitObserverSnapshot(FElysiumPlayer& Player, double Now);

	// The Sneaking feat as the recompute consumes it: resolved through `ElysiumFeats::Calc` (which
	// adds this character's clamped `trigger_stealth_mod` aggregate) and capped at 10. Answers 0
	// with no rulebook and no bound fallback tables, which is the `Stealth0` column.
	int32 ResolveSneaking(const FElysiumCombatCharacter& Character);

	// The tables this world reads, or the neutral fallback. Never null.
	const FElysiumStealthTables& TablesFor(const FElysiumEntityWorld* World);

	// The producer-side hearing reduction.
	// `CBaseEntity::AdjustSoundDistForStealth`: the SOURCE's own `m_flStealthHearingDist`, which
	// `FElysiumEntityWorld::EmitGameSound` subtracts from the radius at insertion.
	//
	// CHOSEN, NOT RECOVERED: retail scopes this to "eligible type-4 sound insertion", and the
	// eligibility predicate behind that phrase is not recovered. Every game sound whose source
	// exposes the surface is treated as eligible here, which is the literal half of the recovered
	// sentence ("when the source exposes the combat-character/player stealth surface"). Only the
	// player carries a surface, so no other character's stimulus changes either way.
	float HearingReductionCmFor(const FElysiumEntity* Source);
	float HearingReductionCmFor(const FElysiumEntityWorld* World,
		const FElysiumEntityHandle& Source);
}
