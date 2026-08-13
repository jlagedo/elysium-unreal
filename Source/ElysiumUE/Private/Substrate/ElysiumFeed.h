#pragma once

#include "CoreMinimal.h"

// B6 — the pure rules half of feeding, beside `ElysiumDice` and `ElysiumSheetMath`.
//
// Everything here is a function of numbers: no world, no clock, no entity. The transaction that
// consumes it lives on `FElysiumCombatCharacter` (`Public/ElysiumPlayer.h`) because that is where
// retail puts it. The behaviour these constants reproduce is canonical in `docs/vtmb/feeding.md`;
// the check boundary is `docs/vtmb/skills-and-checks.md`.
//
// Scope: ordinary player-on-humanoid feeding, paired mode 0. Seductive (mode 2), rat (mode 6) and
// zombie (mode 8) feeding, prayer, the trait-effect branches (`FX_Increased_Rat_Feed`,
// `Fx_Feed_Bonus_Opp_Gender`, `Fx_Feed_Bonus_Tramps`, `Fx_Cannot_Rat_Feed`), the signed
// first-pulse blood-gain modifier at +0x14a4, and every presentation layer (camera, feed bar,
// heartbeat, particles, audio) are deliberately absent rather than approximated.

struct FElysiumRollResult;
enum class EElysiumFeedPhase : uint8;   // Public/ElysiumPlayer.h

namespace ElysiumFeed
{
	// --- The accelerating server-timer cadence ------------------------------------------------
	// The pinned binary stores both as doubles; a `m_fl*` field carries them, like `NextThink`.
	inline constexpr float MinInterval  = 0.30f;
	inline constexpr float IntervalStep = 0.15f;

	// `initial interval = 0.30 + (B + 1) * 0.15`, where B is the victim's BloodPool integer at the
	// instant FeedBegin runs. A fuller victim therefore starts slower and accelerates for longer.
	inline float InitialInterval(int32 VictimBloodPool)
	{
		return MinInterval + static_cast<float>(VictimBloodPool + 1) * IntervalStep;
	}

	// `if (current > 0.30) current -= 0.15`. The epsilon is float hygiene over repeated
	// subtraction, not a rule: retail compares the raw doubles.
	inline float NextInterval(float Current)
	{
		return (Current > MinInterval + UE_KINDA_SMALL_NUMBER) ? (Current - IntervalStep) : Current;
	}

	// --- The opposed check (`skills-and-checks.md` § "Feeding is deliberately asymmetric") -----
	// The victim rolls Hacking against the human target number 6; the attacker does not roll at all.
	inline constexpr int32 OpposedDifficulty = 6;

	// `attacker Close_Combat_Brawl RATING > max(victim Hacking net, 0)`. Strictly greater, and the
	// net is floored at zero so a botched defence cannot help the attacker past a zero rating.
	bool OpposedAccepts(int32 AttackerBrawlRating, const FElysiumRollResult& VictimHackingRoll);

	// The two authored ways a humanoid skips that opposed check. `FastFood` is inherited from the
	// NPC template's General block; `Fx_No_Resist_Feeding` is the live trait-effect flag. Keeping
	// their OR here lets content tests prove the real BluebloodFastfood row through the same policy
	// the NPC leaf calls, without teaching a bare entity world to load the rulebook.
	inline bool ResistsByAuthoredPolicy(bool bFastFood, bool bNoResistEffect)
	{
		return !bFastFood && !bNoResistEffect;
	}

	// --- The baseline unit transaction ---------------------------------------------------------
	// What one pulse does, given only whether the feeder's blood-pool increment landed. The rule
	// worth isolating is the asymmetry: healing is still evaluated in the normal branch when the
	// feeder's pool is full, and the successful-blood counter is not. The victim is always drained,
	// because `DecBloodPool(false)` is called once regardless.
	//
	// It is a separate function because the feeder's pool ceiling is a rulebook clamp, so a world
	// with no rulebook (the Substrate tier, and any headless run) cannot produce a full pool — this
	// is where that branch stays assertable.
	struct FPulseEffects
	{
		bool bCountStolen = false;
		bool bHeal = true;
		bool bDrainVictim = true;
	};
	inline FPulseEffects PulseEffects(bool bBloodPoolIncremented)
	{
		FPulseEffects Out;
		Out.bCountStolen = bBloodPoolIncremented;
		return Out;
	}
	inline FString PulseLogLine(int32 PlayerBefore, int32 PlayerAfter,
		int32 VictimBefore, int32 VictimAfter, int32 BloodStolen)
	{
		return FString::Printf(
			TEXT("INFO - Feed pulse: player=%d->%d, victim=%d->%d, stolen=%d"),
			PlayerBefore, PlayerAfter, VictimBefore, VictimAfter, BloodStolen);
	}

	// The feat names, spelled once. `Hacking` is literal retail behaviour on the victim side and is
	// not a rename candidate.
	inline const TCHAR* AttackerFeat() { return TEXT("Close_Combat_Brawl"); }
	inline const TCHAR* VictimFeat()   { return TEXT("Hacking"); }

	// --- The animation-event bridge, driven from decoded clip metadata ------------------------
	// The bake carries no MDL animation events (see the header comment in `ElysiumFeed.cpp`), so
	// the state machine raises 4007/4006 itself at the authored cycles `feeding.md` § "Representative
	// clip timing" decoded for the ordinary attacker/short-victim/front variant. Seconds, at the
	// authored 30 fps. A body that resolves its own clip length overrides these; a headless world
	// uses them as-is, which is what keeps the transaction independent of a rendered body.
	inline constexpr float EngageSeconds  = 16.0f / 30.0f;   // 0.533
	inline constexpr float BiteSeconds    = 19.0f / 30.0f;   // 0.633
	inline constexpr float LoopSeconds    = 61.0f / 30.0f;   // 2.033
	inline constexpr float ReleaseSeconds = 73.0f / 30.0f;   // 2.433

	// Where each boundary event sits inside its own clip, as a 0..1 cycle.
	inline constexpr float BiteEventCycle    = 0.0f;        // 4007 on the bite clip
	inline constexpr float ReleaseEventCycle = 0.305556f;   // 4006 on the feed-release clip

	// The three event ids `CBaseCombatCharacter::HandleAnimEvent` gives distinct jobs.
	inline constexpr int32 EventFeedBegin      = 4007;
	inline constexpr int32 EventFeedTeardown   = 4006;
	inline constexpr int32 EventFeedEmitter    = 5116;   // presentation only

	// The clip label each half plays for a phase, in the NPC clip vocabulary the export already
	// carries (`feeding_attacker_shortvictim_front_*` / `feeding_victim_shortattacker_front_*`).
	//
	// The short-victim/front variant is pinned. Retail's paired translator expands the base
	// activity into attacker/victim x short/tall-partner x front/back; resolving that is the
	// grapple router's job and is out of B6's scope, so the pair always performs the front,
	// short-partner cell. A missing clip is logged and the transaction continues.
	const TCHAR* PhaseClipLabel(EElysiumFeedPhase Phase, bool bAttacker);
}
