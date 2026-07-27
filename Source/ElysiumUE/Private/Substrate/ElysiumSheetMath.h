#pragma once

#include "CoreMinimal.h"

#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumRulebook.h"

// The arithmetic over the character sheet's slots: the trait-effect layer a character carries
// (`m_tEffectList`) and the feat evaluator (`Feats::FeatValue`) that reads through it.
//
// The storage and the names are `Public/ElysiumSheetSlots.h`'s and the values are the rulebook's;
// this is the third piece — what the two produce together when something asks for a number.
//
// The VtMB facts: `docs/game_runtime.md` section 3 ("Feats", "Trait effects"). The effect
// accumulator below is `CVTraitEffectQuery`'s, read off the image — its field layout, its per-
// operator rules and its finalize arithmetic — not a reconstruction.

struct FElysiumSheet;

// ================================================================================================
// The resolved effect layer — one character's `m_tEffectList`
// ================================================================================================

// A character's active `TraitEffectGroup`s, flattened into what a read needs: per-trait modifier
// rows, per-feat modifier rows, and the `Fx_*` code-side flags.
//
// The names come from the character's clan (`ClanEffect`), its History (`Effect`) and, later, its
// items and its frenzy state. `Build` resolves them once; every read is a map lookup.
struct FElysiumSheetEffects
{
	// One applicable modifier. `Cost`/`BloodCost`/`Damage`/`Duration` carry payloads no sheet read
	// consumes — the buy path and the discipline/heal systems read them where they live — so they
	// are counted and skipped rather than stored as arithmetic.
	struct FRow
	{
		EElysiumTraitOp Op = EElysiumTraitOp::Add;
		int32 Amount = 0;
		// The owning group's priority (`CVTraitEffectGroup_t+0xC`), which decides between two
		// effects competing for the same single-winner slot. No shipped group authors one, so every
		// group is equal-priority and the tie-break — the smaller amount wins — is what decides.
		int32 Priority = 0;
	};

	// `CVTraitEffectQuery` — the accumulator `ApplyEffects` fills, one per read. Its defaults are
	// the ones the engine's initializer writes (`FUN_101f6910`): the four single-winner slots start
	// unclaimed at priority -1, the bounds at +/-32000, the percentage at 100.
	struct FQuery
	{
		int32 Value = 0;        // the input value; an op `Value` REPLACES it outright
		int32 Add = 0;          // every `+`/`-` sums here
		int32 Mul = 1,  MulPri = -1;
		int32 Div = 1,  DivPri = -1;
		int32 Max = 32000,  MaxPri = -1;
		int32 Min = -32000, MinPri = -1;
		int32 Percent = 100;    // each `%` effect adds `100 - amount`

		void Accumulate(const FRow& Row);
		// `(Value + Add) * Mul / Div`, then the percentage when it is not 100, then clamped to
		// [Min, Max] — the engine's own order, read off `FUN_101f69a0`.
		int32 Finalize() const;
	};

	// Resolve `GroupNames` against the loaded effect table. Unknown names are recorded in
	// `Unresolved` rather than dropped, so a stale clan/history key is visible to `elysium.sheet`.
	// The feat table is what lets an effect target a **feat** — seven shipped traits do
	// (`Inspection`, `Seduction`, the three combat feats, `Intrusion`, `Hacking`); with no table
	// those rows are recorded as unresolved rather than mistaken for a stat.
	void Build(const FElysiumTraitEffects& Table, TArrayView<const FString> GroupNames,
		const FElysiumFeatTable* Feats);
	void Reset();
	bool IsEmpty() const { return TraitRows.IsEmpty() && FeatRows.IsEmpty() && Flags.IsEmpty(); }

	// value -> the whole accumulator, finalized. The `Max`/`Min` operators clamp INSIDE this, and
	// the stat's own authored bounds are then run through the same walk (`BoundsFor`), which is how
	// one `Max 4` effect both caps the value and lowers the ceiling.
	int32 ApplyToTrait(EElysiumTraitContainer Container, int32 Slot, int32 Value) const;
	int32 ApplyToFeat(int32 FeatIndex, int32 Value) const;

	// The stat's authored bound as this character sees it: the resolved number run through the same
	// per-slot effect walk the value takes. So a `+1` on a stat raises its ceiling by 1 too, and a
	// `Max 4` lowers it to 4 — which is exactly what the engine's two bound accessors do.
	int32 ApplyToBound(EElysiumTraitContainer Container, int32 Slot, int32 Bound) const;

	// An `Fx_*` flag's summed amount; 0 when no group sets it. `Fx_Humanity_Mods_Doubled` is the
	// one 9.4c reads (Toreador's gift and bane are the same flag).
	int32 Flag(const TCHAR* FxName) const;

	const TArray<FString>& ResolvedGroups() const { return Groups; }
	const TArray<FString>& UnresolvedGroups() const { return Unresolved; }
	int32 NumRows() const;

private:
	// container * 256 + slot, and the feat's own index. Both are dense small ints, and a map keeps
	// the empty case (most characters) free.
	TMap<int32, TArray<FRow>> TraitRows;
	TMap<int32, TArray<FRow>> FeatRows;
	TMap<FString, int32> Flags;          // lowercased Fx_ name -> summed amount

	TArray<FString> Groups;
	TArray<FString> Unresolved;
	int32 SkippedRows = 0;               // payload operators no sheet read consumes
};

// ================================================================================================
// The feat evaluator
// ================================================================================================

namespace ElysiumFeats
{
	// `Feats::FeatValue` — the sum of the feat's variable-length `Base%d` list, each entry the
	// CURRENT trait value through its own `/`-or-`*` modifier, the nine attributes floored at 1,
	// then a feat-level trait-effect pass, then clamped to `[0, MaxValue]`.
	int32 FeatValue(const FElysiumFeat& Feat, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects);

	// What `CalcFeat("<name>")` resolves: a feat first, then — our own extension — a trait, whose
	// current value is returned. `bOutIsFeat` says which path answered; the return is 0 with
	// `bOutResolved` false when neither owns the name.
	int32 Calc(const FElysiumFeatTable& Feats, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects, const FString& Name,
		bool& bOutResolved, bool& bOutIsFeat);
}

// ================================================================================================
// Shared sheet predicates
// ================================================================================================

namespace ElysiumXp
{
	// `CVPlayer::AwardExperience`'s bonus step: above 2 XP (a raw value over 299) the character's
	// `Experience_Modifier` is added, floored at 100 — the file's own "the value without extra
	// experience points".
	int32 WithModifier(int32 RawValue, int32 ExperienceModifier);

	// `CVPlayer::AddExperience`'s banking step: the raw value accumulates untouched into
	// `Lifetime`, and the /100 **keeps its remainder** — so the sub-100 residue is state that
	// persists across awards. Returns the whole XP points banked by this award.
	int32 Bank(int32 RawValue, float& InOutRemainder, float& InOutLifetime);
}

namespace ElysiumSheetRules
{
	// One `IncPredependency` expression — `"BloodPool > 0"`, `"Health < Max_Health"`. Each side is
	// a trait name (read as its current value) or a literal. An expression that does not parse
	// reads TRUE: a gate we cannot read must not silently refuse a raise.
	bool EvalPredependency(const FString& Expr, const FElysiumSheet& Sheet);
}
