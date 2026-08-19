#pragma once

#include "CoreMinimal.h"

// ================================================================================================
// 17. system/reaction.txt + reactions000.txt — the RPG/social reaction score and its modifiers
// ================================================================================================
//
// The third social domain (`docs/architecture/gameplay-systems-architecture.md` §5.5.7, K4): a
// score separate from the native combat-relationship table (`D_HT`/`D_FR`/`D_LI`/`D_NU`, read
// through `FElysiumNpc`'s relationship rows) and from `DispositionTable.txt`'s emotional/
// presentation state (`FElysiumDispositionTable`, above). Nothing here derives from, or is derived
// by, either of the other two — a band label like `Hatred` names a point on THIS scale, not a
// `D_HT` row (`docs/vtmb/npc-ai-reverse-engineering.md` → "Emotional disposition and social
// reaction are different domains").
//
// `reaction.txt`'s `General.Table` ("Reaction Ranges") is the seven-row band-boundary list, joined
// BY INDEX to `Strings.ReactionLevel`'s `Name<N>` labels — row N's boundary is band N's lower bound
// (`"0" "0"`, `"1" "20"`, … `"6" "9999"` -> Want To Kill, Hatred, Dislike, Neutral Reaction, Admire,
// Love, Obsession). The table's own `"Clamping" "1"` is read verbatim: the calculator in
// `Substrate/ElysiumReaction.h` clamps the running score into
// `[Bands[0].LowerBoundary, Bands.Last().LowerBoundary]` before resolving it, the natural reading
// of a range table that declares itself clamped.
//
// `reactions000.txt`'s `ReactionCategory -> ReactionGroup -> Reaction` nesting is the same shape
// `FElysiumTraitEffects` already reads for `traiteffects000.txt` (Category/Group/Effect). Each
// `Reaction` row carries an optional `Targets` scope (`Kindred(!Gangrel)`, `Kine()`, bare `ALL`, or
// no key at all), a `WhoModifies` token (every shipped row authors `Others`), and a `Modifier`
// string that is either a scalar op (`+20`, `-5`, `*2`) or a free-form expression naming the
// running score (`"Reaction + ((Reaction - 50)*2)"`, Dementation's Passion). The calculator in
// `Substrate/ElysiumReaction.{h,cpp}` is the sole consumer of both tables; this layer only parses
// what the two files carry.

// One `General.Table` row joined to its `Strings.ReactionLevel` label.
struct FElysiumReactionBand
{
	int32 LowerBoundary = 0;
	FString Label;

	bool IsValid() const { return !Label.IsEmpty(); }
};

struct FElysiumReactionBandTable
{
	TArray<FElysiumReactionBand> Bands;   // ascending, `General.Table`'s own row order (0..6)
	bool bClamping = false;               // `General.Table`'s `"Clamping"` key

	bool Load(FString& OutError);
	bool IsValid() const { return !Bands.IsEmpty(); }

	// The band whose LowerBoundary is the greatest one <= Score. Null only when the table itself is
	// empty — a loaded table always covers every Score at or above its first (0) boundary.
	const FElysiumReactionBand* Resolve(int32 Score) const;

	int32 MinBoundary() const { return Bands.IsEmpty() ? 0 : Bands[0].LowerBoundary; }
	int32 MaxBoundary() const { return Bands.IsEmpty() ? 0 : Bands.Last().LowerBoundary; }
};

// `Targets`'s scope token. `reaction.txt`'s own (fully commented-out) `General.CritterScopes`
// legend names the same three values.
enum class EElysiumReactionTargetScope : uint8
{
	Kindred,
	Kine,
	All,      // bare `ALL`, or the row authored no `Targets` key at all
};

// A parsed `Targets` string: `Kindred(!Gangrel)` -> {Kindred, ["Gangrel"]}; `Kine()` -> {Kine, []};
// bare `ALL` -> {All, []}.
struct FElysiumReactionTargets
{
	EElysiumReactionTargetScope Scope = EElysiumReactionTargetScope::All;
	TArray<FName> Exclusions;   // the `(!X,!Y)` names, e.g. `Kindred(!Gangrel)` -> ["Gangrel"]

	// False when the row authored no `Targets` key at all (`Presence-Awe`/`Presence-General` ship
	// this way). CHOSEN, NOT RECOVERED: read as unconditional — same as Scope::All — rather than as
	// "never applies", since a bonus that could never fire would make the authored row meaningless.
	// See `ElysiumReaction.cpp`.
	bool bAuthored = false;

	// Whether a reacting character with these facts falls inside this scope.
	bool Matches(bool bReactorIsKindred, bool bReactorIsKine, FName ReactorClan) const;
};

// Parses a `Targets` string. Public because it is the piece `Elysium.Substrate.Reaction` drives
// directly, the rule `FElysiumTraitEffects::ParseModifier` established.
void ElysiumParseReactionTargets(const FString& Raw, FElysiumReactionTargets& Out);

// What decides whether a modifier applies — the character/history/Discipline fact its owning
// `ReactionGroup`'s `InternalName` names. The six below are every group `reactions000.txt` ships;
// an InternalName outside this set parses as `Unrecognized` and the row is carried-but-inert.
enum class EElysiumReactionCondition : uint8
{
	Megalomaniac,          // "Reaction (Megalomaniac)"
	CloseToTheBeast,       // "Reaction (Close to the Beast)"
	OccultNut,             // "Reaction (Occult Nut)"
	DementationPassion,    // "Reaction (Dementation-Passion)"
	PresenceAwe,           // "Reaction (Presence-Awe)"
	PresenceGeneral,       // "Reaction (Presence-General)"
	Unrecognized,
};

EElysiumReactionCondition ElysiumParseReactionCondition(const FString& GroupInternalName);

// The parsed shape of a `Modifier` string.
enum class EElysiumReactionModifierKind : uint8
{
	Add,            // leading `+`/`-` and a plain number: `"+20"`, `"-5"`
	Multiply,       // leading `*` and a plain number: `"*2"`
	Formula,        // a free-form expression naming `Reaction`: `"Reaction + ((Reaction - 50)*2)"`
	Unrecognized,   // carried-but-inert — no shipped row takes this today
};

// Parses a `Modifier` string. Public for the same reason `ElysiumParseReactionTargets` is.
void ElysiumParseReactionModifierExpr(const FString& Raw, EElysiumReactionModifierKind& OutKind,
	double& OutScalar, FString& OutFormula);

// One `reactions000.txt` `Reaction` row.
struct FElysiumReactionModifier
{
	FString CategoryInternalName;   // the owning `ReactionCategory`'s `InternalName` ("History", "Discipline")
	FString GroupInternalName;      // the owning `ReactionGroup`'s `InternalName`
	EElysiumReactionCondition Condition = EElysiumReactionCondition::Unrecognized;

	FElysiumReactionTargets Targets;
	FString WhoModifies;             // raw token; every shipped row authors `Others`

	FString RawModifier;             // exactly as authored, for diagnosis
	EElysiumReactionModifierKind Kind = EElysiumReactionModifierKind::Unrecognized;
	double ScalarValue = 0.0;        // Add: the signed delta. Multiply: the factor.
	FString FormulaExpression;       // Kind == Formula: the raw expression text, evaluated by
	                                  // `ElysiumReaction::TryEvaluateFormula`

	// Recognized and applicable in principle: a named condition, a parseable Modifier shape, and the
	// one shipped `WhoModifies` value. `ElysiumReaction::Compute` skips a row this returns false for;
	// `FElysiumReactionModifierTable::Load` logs the inert set once.
	bool IsRecognized() const;
};

struct FElysiumReactionModifierTable
{
	TArray<FElysiumReactionModifier> Modifiers;   // file order: category, then group, then row

	// Diagnostic only — `GroupInternalName` plus a short reason, one entry per row `IsRecognized()`
	// returns false for. `Load` fills it; the subsystem's `Reactions()` accessor logs it once on the
	// first load.
	TArray<FString> InertModifiers;

	bool Load(FString& OutError);
	bool IsValid() const { return !Modifiers.IsEmpty(); }
	int32 Num() const { return Modifiers.Num(); }
};

// The pair, bundled together — `ElysiumReaction::Compute` needs both. One lazy `Reactions()`
// accessor exposes them, the shape `SoundVolumes()` established for a single-file table and
// `TraitEffects()` established for a two-file one (`traiteffect.txt` + `traiteffects000.txt`).
struct FElysiumReactionCatalogue
{
	FElysiumReactionBandTable Bands;          // reaction.txt
	FElysiumReactionModifierTable Modifiers;  // reactions000.txt

	// Bands are load-bearing — there is nothing to resolve a score to without them. Modifiers are
	// supplementary: an unmodified base score still resolves to a band. The catalogue counts as
	// loaded once Bands alone loads, mirroring how `FElysiumSoundVolumeTable` fails open on a
	// missing file.
	bool Load(FString& OutError);
	bool IsValid() const { return Bands.IsValid(); }
};
