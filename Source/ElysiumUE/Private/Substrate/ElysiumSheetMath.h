#pragma once

#include "CoreMinimal.h"

#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumDisciplineTargetTables.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumStealthTables.h"

// The arithmetic over the character sheet's slots: the trait-effect layer a character carries
// (`m_tEffectList`) and the feat evaluator (`Feats::FeatValue`) that reads through it.
//
// The storage and the names are `Public/ElysiumSheetSlots.h`'s and the values are the rulebook's;
// this is the third piece — what the two produce together when something asks for a number.
//
// The VtMB facts: `docs/vtmb/game_runtime.md` section 3 ("Feats", "Trait effects"). The effect
// accumulator below is `CVTraitEffectQuery`'s, read off the image — its field layout, its per-
// operator rules and its finalize arithmetic — not a reconstruction.

struct FElysiumSheet;
class FElysiumCombatCharacter;

// The resolved effect layer — one character's `m_tEffectList`.

// A character's active `TraitEffectGroup`s, flattened into what a read needs: per-trait modifier
// rows, per-feat modifier rows, and the `Fx_*` code-side flags.
//
// The names come from the character's clan (`ClanEffect`) and its History (`Effect`). `Build`
// resolves them once; every read is a map lookup.
struct FElysiumSheetEffects
{
	// One applicable modifier. `Cost`/`BloodCost`/`Damage`/`Duration` are **payload** operators:
	// the engine's own accumulator switch breaks on all four without touching the query, so they
	// never move a trait's value. They are stored on the same row type and read by the system that
	// owns them — the buy path takes `Cost`, the discipline transactions take `BloodCost` and
	// `Duration`, the feed/heal payload takes `Damage`.
	struct FRow
	{
		EElysiumTraitOp Op = EElysiumTraitOp::Add;
		int32 Amount = 0;
		// The owning group's priority (`CVTraitEffectGroup_t+0xC`), which decides between two
		// effects competing for the same single-winner slot. No shipped group authors one, so every
		// group is equal-priority and the tie-break — the smaller amount wins — is what decides.
		int32 Priority = 0;
		// The trailing `%` of `"Duration 200%"`. Only a payload row carries it: the arithmetic
		// operators have their own `%` operator.
		bool bPercent = false;
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
		const FElysiumFeatTable* Feats, const FElysiumStatTable* Stats = nullptr,
		const FElysiumStrings* Strings = nullptr);
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
	// one the humanity-mod path reads (Toreador's gift and bane are the same flag).
	int32 Flag(const TCHAR* FxName) const;

	// The payload operators.
	// `Value` through every payload row of `Op` that targets this trait slot. Rows without a `%`
	// add; rows with one scale.
	//
	// **CHOSEN** — the payload operators never reach `CVTraitEffectQuery` (the engine's switch
	// breaks on them before the accumulator), so how the engine composes two groups authoring the
	// same payload on the same trait is not recovered. Stated here: additive rows sum first, then
	// each percentage row scales in authored order with integer truncation. No shipped character
	// can carry two payload rows of one operator on one trait — the Discipline `Duration` rows all
	// come from Histories and a character has exactly one — so the composition is unobservable in
	// the shipped corpus and is stated rather than left undefined.
	int32 ApplyPayload(EElysiumTraitOp Op, EElysiumTraitContainer Container, int32 Slot,
		int32 Value) const;

	// Whether any payload row of `Op` targets this slot — what a caller checks before reporting a
	// modified cost or duration as authored rather than defaulted.
	bool HasPayload(EElysiumTraitOp Op, EElysiumTraitContainer Container, int32 Slot) const;

	const TArray<FString>& ResolvedGroups() const { return Groups; }
	const TArray<FString>& UnresolvedGroups() const { return Unresolved; }
	int32 NumRows() const;

private:
	// container * 256 + slot, and the feat's own index. Both are dense small ints, and a map keeps
	// the empty case (most characters) free.
	TMap<int32, TArray<FRow>> TraitRows;
	TMap<int32, TArray<FRow>> FeatRows;
	// The payload rows, keyed `op * 65536 + container * 256 + slot` so one map serves all four.
	TMap<int32, TArray<FRow>> PayloadRows;
	TMap<FString, int32> Flags;          // lowercased Fx_ name -> summed amount

	TArray<FString> Groups;
	TArray<FString> Unresolved;
	int32 SkippedRows = 0;               // rows no operator family claims
};

// The feat evaluator.

namespace ElysiumFeats
{
	// `feats.txt`'s second `Feat` block, and therefore feat id 1. The recovered CharacterData
	// category-1 walk special-cases exactly this id: it adds `CBaseCombatCharacter::
	// GetStealthModifier()` to the summed rating (`docs/vtmb/stealth.md` -> "Player target-surface
	// update", step 2). The index is asserted against the real table by
	// `Elysium.Content.Stealth`, so a patched `feats.txt` that reordered the list is caught rather
	// than silently moving the bonus onto Lockpicking.
	inline constexpr int32 SneakingFeatIndex = 1;

	// `Feats::FeatValue` — the sum of the feat's variable-length `Base%d` list, each entry the
	// CURRENT trait value through its own `/`-or-`*` modifier, the nine attributes floored at 1,
	// then the per-feat code term, then a feat-level trait-effect pass, then clamped to
	// `[0, MaxValue]`.
	//
	// `Owner` is the character the sheet belongs to, or null. It exists for the code term above and
	// nothing else: with no owner the Sneaking feat reads its authored bases alone, which is what a
	// character carrying no `trigger_stealth_mod` contribution would read anyway.
	int32 FeatValue(const FElysiumFeat& Feat, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects, const FElysiumCombatCharacter* Owner = nullptr);

	// What `CalcFeat("<name>")` resolves: a feat first, then — our own extension — a trait, whose
	// current value is returned. `bOutIsFeat` says which path answered; the return is 0 with
	// `bOutResolved` false when neither owns the name.
	int32 Calc(const FElysiumFeatTable& Feats, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects, const FString& Name,
		bool& bOutResolved, bool& bOutIsFeat, const FElysiumCombatCharacter* Owner = nullptr);
}

// Shared sheet predicates.

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

	// The headless table binding.
	//
	// Every sheet reader ordinarily reaches its tables through
	// `UElysiumGameStateSubsystem::Rulebook()`, which is a GameInstance subsystem — so a bare
	// substrate world (K10: "every domain runs headless") has no rulebook at all, and the clan
	// banes, the feat evaluator and the Discipline blocks all read as absent.
	//
	// This is the one seam that closes that: a Substrate-tier test binds fabricated tables and every
	// reader that finds no subsystem falls back to them. It is the same shape
	// `FElysiumGameSoundBus::SetVolumeTable` already has ("the mutable overload exists for the two
	// callers that own the bus's configuration — this world, and a Substrate-tier test binding a
	// fabricated table") and `ElysiumItems::Install` has for the item catalogue.
	//
	// **The subsystem always wins.** A bound table is a fallback, never an override, so nothing
	// bound here can change what a real run reads. A null member is "not bound".
	struct FBoundTables
	{
		const FElysiumStatTable*         Stats = nullptr;
		const FElysiumTraitEffects*      TraitEffects = nullptr;
		const FElysiumFeatTable*         Feats = nullptr;
		const FElysiumClanTable*         Clans = nullptr;
		const FElysiumDisciplineTargets* DisciplineTargets = nullptr;
		const FElysiumStealthTables*     Stealth = nullptr;
		// `rules.txt`. The combat transactions read their opposed-roll difficulties and their melee
		// reaction margins out of it, and they are plain-C++ leaves holding no session pointer for
		// exactly the reason the rest of this bundle exists: with no subsystem in reach a weapon
		// context otherwise gathers nothing, the defender defends with zero and no margin can be
		// classified at all.
		const FElysiumRules*             Rules = nullptr;
	};

	// Bind (or, with a default-constructed value, unbind) the fallback tables. Process-wide,
	// because the readers are plain-C++ leaves that hold no session pointer — the same reason
	// `ElysiumRng`'s streams are module-static.
	void BindTables(const FBoundTables& Tables);
	const FBoundTables& BoundTables();
}
