#pragma once

#include "CoreMinimal.h"
#include "ElysiumComboChain.generated.h"

// One melee attack sequence's authored combo-chain block — which direction key selects it, which
// attack it hands off to, and when the hand-off may be asked for
// (`docs/vtmb/combat-and-damage.md`).
//
// The sequence descriptor's last 44 bytes carry all seven fields and the export writes them onto the
// clip sidecar's `combo` column
// (`pipeline/src/elysium_pipeline/formats/mdl_skel.py` -> `read_combo_chain`). 208 of the install's
// 14,012 descriptors author at least one; every other sequence carries none, which is an authored
// absence and not a gap. Nothing in the block is a length or a direction, so it crosses the seam
// unconverted: a button mask is a mask, a sequence label is a name, and a fraction of a clip cycle
// has no units.
//
// It lives in `Public/` for the same reason `Public/ElysiumSwingRecord.h` does: it crosses the
// outbound service seam, where `IElysiumEmbodiment::NpcClipCombo` hands one sequence's block down to
// the substrate.

// The **Source-side** `IN_*` usercmd bits the authored masks are built from — the file's own bit
// numbering, which is NOT this runtime's `EElysiumButton`. The player's button field is translated
// into these before a mask is compared against it, because the mask is exported raw.
namespace ElysiumCombo
{
	inline constexpr int32 InJump      = 0x002;
	inline constexpr int32 InForward   = 0x008;
	inline constexpr int32 InBack      = 0x010;
	inline constexpr int32 InLeft      = 0x080;
	inline constexpr int32 InRight     = 0x100;
	inline constexpr int32 InMoveLeft  = 0x200;
	inline constexpr int32 InMoveRight = 0x400;

	// The bits the player selector reads off the current button field before comparing it with a
	// sequence's authored mask — retail's `+0x2088 & 0x79A`. Everything outside it (the attack bits,
	// `+use`, duck, the camera verbs) is invisible to attack selection.
	inline constexpr int32 SelectionMask =
		InJump | InForward | InBack | InLeft | InRight | InMoveLeft | InMoveRight;

	// `+0x2D4`'s "this sequence is not a candidate for direction-keyed selection" marker, on 13,898
	// descriptors. `0` is a STATED mask — the attack a neutral press selects — so the marker is the
	// only value tested; treating a falsy mask as unset would drop 36 authored selections.
	inline constexpr int32 MaskUnset = -1;

	// Which class of match a candidate sequence's mask makes against the player's current state. The
	// ORDER is the preference: an exact match wins, then the two partial classes, then the neutral
	// fallback. `None` is not a rank — it means the candidate is not selectable by state at all.
	enum class EStateMatch : uint8
	{
		None = 0,
		Exact,
		Directional,   // shares a held forward/back bit
		Strafe,        // shares a held strafe bit
		Neutral,       // the mask-0 attack, which any state falls back to
	};

	// The player selector's own comparison (`0x10160F90`): the authored mask against the current
	// button state reduced to `SelectionMask`. `State` is masked here as well as by the caller, so a
	// caller that hands the whole field over still gets the recovered comparison.
	inline EStateMatch RankStateMask(int32 ClipMask, int32 State)
	{
		if (ClipMask == MaskUnset || ClipMask < 0)
		{
			return EStateMatch::None;   // the 13,898 sequences that are not candidates
		}
		const int32 Held = State & SelectionMask;
		if (ClipMask == Held)
		{
			return EStateMatch::Exact;   // mask 0 against no direction held is this arm, not the fallback
		}
		if ((ClipMask & Held & (InForward | InBack)) != 0)
		{
			return EStateMatch::Directional;
		}
		if ((ClipMask & Held & (InMoveLeft | InMoveRight)) != 0)
		{
			return EStateMatch::Strafe;
		}
		if (ClipMask == 0)
		{
			return EStateMatch::Neutral;
		}
		return EStateMatch::None;
	}

	// The held selection bits by name — `FORWARD|MOVELEFT`, or `-` when the player is holding no
	// direction. Diagnostics only, and the reason it exists is that the state a swing selected on is
	// otherwise invisible: a directional attack that silently resolves at the neutral entry looks
	// exactly like an authored absence in the log, and telling those two apart is what the melee
	// timeline line needs in order to be evidence.
	inline FString DescribeStateMask(int32 State)
	{
		struct FNamedBit { int32 Bit; const TCHAR* Name; };
		static constexpr FNamedBit Named[] = {
			{ InForward,   TEXT("FORWARD")   },
			{ InBack,      TEXT("BACK")      },
			{ InMoveLeft,  TEXT("MOVELEFT")  },
			{ InMoveRight, TEXT("MOVERIGHT") },
			{ InLeft,      TEXT("LEFT")      },
			{ InRight,     TEXT("RIGHT")     },
			{ InJump,      TEXT("JUMP")      },
		};
		const int32 Held = State & SelectionMask;
		FString Out;
		for (const FNamedBit& Entry : Named)
		{
			if ((Held & Entry.Bit) != 0)
			{
				Out += Out.IsEmpty() ? Entry.Name : FString(TEXT("|")) + Entry.Name;
			}
		}
		return Out.IsEmpty() ? FString(TEXT("-")) : Out;
	}

	// Whether A is a better match than B, over the order the enum declares. `None` never wins.
	inline bool IsBetterStateMatch(EStateMatch A, EStateMatch B)
	{
		return A != EStateMatch::None && (B == EStateMatch::None || A < B);
	}

	// Which arm of the busy predicate an attack activity takes while its clip runs. The three arms
	// are retail's own, one per activity family (`docs/vtmb/combat-and-damage.md`).
	enum class EBusyArm : uint8
	{
		// Not a busy family at all: nothing about the playing clip refuses a press.
		None = 0,
		// The ordinary attack — busy while the clip's cycle is below its authored `w_hold`.
		Hold,
		// The block family — busy purely on the clock, until the next-attack deadline.
		Clock,
		// The air, `2COMBO` and heavy families — busy for the whole clip.
		WholeClip,
	};

	// The activity's arm, by name. Exact matches on the three attack families, because
	// `ACT_MELEE_ATTACK` and `ACT_MELEE_ATTACK_2COMBO` take different arms and a prefix test would
	// collapse them.
	inline EBusyArm BusyArmFor(const FString& Activity)
	{
		if (Activity.Equals(TEXT("ACT_MELEE_ATTACK"), ESearchCase::IgnoreCase))
		{
			return EBusyArm::Hold;
		}
		if (Activity.Equals(TEXT("ACT_MELEE_ATTACK_2COMBO"), ESearchCase::IgnoreCase)
			|| Activity.Equals(TEXT("ACT_MELEE_ATTACK_HEAVY"), ESearchCase::IgnoreCase)
			|| Activity.Equals(TEXT("ACT_MELEE_AIR_ATTACK"), ESearchCase::IgnoreCase))
		{
			return EBusyArm::WholeClip;
		}
		// `ACT_PREBLOCK`, `ACT_BLOCK`, `ACT_BLOCK_HEAVY` — and the weapon-translated spellings of all
		// three, which is why this one is a prefix.
		if (Activity.StartsWith(TEXT("ACT_BLOCK"), ESearchCase::IgnoreCase)
			|| Activity.StartsWith(TEXT("ACT_PREBLOCK"), ESearchCase::IgnoreCase))
		{
			return EBusyArm::Clock;
		}
		return EBusyArm::None;
	}

	// The busy predicate itself. `Cycle` is the playing clip's own normalized position, `HoldCycle`
	// the value that clip's descriptor states (1.0 where it states no combo block at all), and the
	// two times are the substrate clock and the weapon's next-attack deadline.
	//
	// **An unauthored `w_hold` is 1.0, and that makes the `Hold` arm degenerate into `WholeClip`.**
	// The retail predicate reads `+0x2F8` with no substitution and no clamp, so the file's own value
	// is the whole rule; 10,597 of the 14,012 shipped descriptors state exactly 1.00 and the 126
	// stating 0.91 are the directional attacks. With 1.0 in hand `Cycle < HoldCycle` and the melee
	// rows' outer `Cycle < 1.0` guard are the same condition, so `ACT_MELEE_ATTACK` degrades cleanly
	// to what the three unconditional arms already do.
	inline bool IsBusy(const FString& Activity, float Cycle, float HoldCycle, double Now,
		double NextAttackTime)
	{
		switch (BusyArmFor(Activity))
		{
		case EBusyArm::Hold:      return Cycle < HoldCycle;
		case EBusyArm::Clock:     return Now < NextAttackTime;
		case EBusyArm::WholeClip: return Cycle < 1.0f;
		default:                  return false;
		}
	}
}

USTRUCT()
struct ELYSIUMUE_API FElysiumComboChain
{
	GENERATED_BODY()

	// `+0x2D4` — the button-state mask direction-keyed attack selection matches, exported RAW in the
	// file's own `IN_*` bits. `MaskUnset` on every sequence that is not a selection candidate.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Mask = ElysiumCombo::MaskUnset;

	// `+0x2DC` — the DODGE activity this sequence answers with (`ACT_DODGE_DUCK` on all 12 that state
	// one). Empty where the descriptor names none.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Dodge;

	// `+0x2E8` / `+0x2EC` — the successor SEQUENCE LABELS this attack hands off to, matched
	// case-insensitively against the body's own vocabulary. `ChainAlt` is the flying-knockback wall
	// branch, on 28 `meleeshared_onehand` descriptors. Both are carried verbatim, including the
	// shipped links that name a sequence their own model never defines: that string is the whole
	// evidence of the authoring bug, and repairing it is not this runtime's business.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Chain;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString ChainAlt;

	// `+0x2F0` / `+0x2F4` / `+0x2F8` — the hand-off window in clip cycles, and the cycle the busy hold
	// is released at. Carried verbatim and in no assumed order: `w_hold` sits BELOW `w_close` on four
	// shipped descriptors, so a consumer deriving one from the other disagrees with the file.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float WindowOpen = 0.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float WindowClose = 1.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float HoldCycle = 1.0f;

	// Whether the sidecar stated this block at all. The exporter writes the record whole or not at
	// all, so this is the same "no column" absence a clip with no swing records has.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	bool bStated = false;

	// Whether this sequence is a candidate for direction-keyed selection. `0` is a stated mask.
	bool HasStateMask() const { return bStated && Mask != ElysiumCombo::MaskUnset && Mask >= 0; }

	// Whether a press may hand this attack off to a successor at all. Every terminal attack and every
	// `2COMBO` clip answers false, and a press on one is ignored rather than queued.
	bool HasChain() const { return bStated && !Chain.IsEmpty(); }

	// The hand-off window, CLOSED at both ends: a press exactly on either bound commits.
	bool IsWindowOpen(float Cycle) const
	{
		return bStated && Cycle >= WindowOpen && Cycle <= WindowClose;
	}
};
