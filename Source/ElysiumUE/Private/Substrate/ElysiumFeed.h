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
// first-pulse blood-gain modifier at +0x14a4, heartbeat, particles and audio remain absent rather
// than approximated. The capture-backed ordinary presentation slice (paired height variants,
// camera lease, victim meter and release tail) uses these same pure rules.

struct FElysiumRollResult;
struct FElysiumUserCmd;
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

	// While the paired action owns the player body, keep the second Feed press but remove look,
	// movement, combat, use and panel intent. This filters the one command snapshot rather than
	// polling keys or creating a feed-specific input owner.
	FElysiumUserCmd GatePairedUserCmd(const FElysiumUserCmd& Cmd);

	// The feat names, spelled once. `Hacking` is literal retail behaviour on the victim side and is
	// not a rename candidate.
	inline const TCHAR* AttackerFeat() { return TEXT("Close_Combat_Brawl"); }
	inline const TCHAR* VictimFeat()   { return TEXT("Hacking"); }

	// --- The ordinary paired-action catalog ---------------------------------------------------
	enum class EPartnerHeight : uint8
	{
		Shorter,
		Taller,
	};

	enum class ESide : uint8
	{
		Front,
		Back,
	};

	struct FClipPair
	{
		FString Attacker;
		FString Victim;

		bool IsComplete() const { return !Attacker.IsEmpty() && !Victim.IsEmpty(); }
	};

	// The source pair has no equal-height cell. A tie takes the historical short-victim cell on the
	// attacker and therefore the complementary tall-attacker cell on the victim. The important rule
	// is that the two roles are always opposites; the old implementation selected short for both.
	inline EPartnerHeight VictimHeightFor(float AttackerHeightCm, float VictimHeightCm)
	{
		return VictimHeightCm > AttackerHeightCm ? EPartnerHeight::Taller : EPartnerHeight::Shorter;
	}

	FClipPair ResolveClipPair(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight,
		ESide Side = ESide::Front);

	// Character SoundScheme activities resolve to these patch-first audio mirror paths. Kept pure so
	// sex/role selection can be asserted without an audio device.
	FString AudioPath(bool bVictim, bool bMale, const TCHAR* Phase);

	// --- The animation-event bridge, driven from decoded clip metadata ------------------------
	// The bake carries no MDL animation events (see the header comment in `ElysiumFeed.cpp`), so
	// the state machine raises 4007/4006 itself at the authored cycles `feeding.md` § "Representative
	// clip timing" decoded for the ordinary front variants. Seconds, at the authored 30 fps. A body
	// that resolves its own clip length overrides these; a headless world uses them as-is, which is
	// what keeps the transaction independent of a rendered body.
	inline constexpr float EngageSeconds  = 16.0f / 30.0f;   // 0.533
	inline constexpr float BiteSeconds    = 19.0f / 30.0f;   // 0.633
	inline constexpr float LoopSeconds    = 61.0f / 30.0f;   // 2.033
	inline constexpr float ShortVictimReleaseSeconds = 73.0f / 30.0f;   // 2.433
	inline constexpr float TallVictimReleaseSeconds  = 68.0f / 30.0f;   // 2.267

	// Where each boundary event sits inside its own clip, as a 0..1 cycle.
	inline constexpr float BiteEventCycle    = 0.0f;        // 4007 on the bite clip
	inline constexpr float ShortVictimReleaseEventCycle = 0.305556f;
	inline constexpr float TallVictimReleaseEventCycle  = 0.328358f;

	float PhaseSeconds(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight);
	float EventCycle(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight);

	// The three event ids `CBaseCombatCharacter::HandleAnimEvent` gives distinct jobs.
	inline constexpr int32 EventFeedBegin      = 4007;
	inline constexpr int32 EventFeedTeardown   = 4006;
	inline constexpr int32 EventFeedEmitter    = 5116;   // presentation only
}
