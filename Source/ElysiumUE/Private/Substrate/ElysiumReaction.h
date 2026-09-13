#pragma once

#include "CoreMinimal.h"

// The RPG/social reaction score — the third social domain (K4): a pure function over explicit
// inputs, consumed by dialogue and never by combat targeting or emotional disposition. No world, no
// entity, no `FElysiumCombatCharacter` — the caller (the eventual dialogue consumer) gathers every
// fact this needs and hands them in, the split `ElysiumDice::Roll` and `ElysiumFeats::FeatValue`
// already use for their own rulebook-plus-facts inputs.
//
// `Substrate/ElysiumRulebook.h`'s `FElysiumReactionBandTable` (`reaction.txt`) and
// `FElysiumReactionModifierTable` (`reactions000.txt`) are the two tables `Compute` reads; nothing
// here parses KeyValues or touches disk.

struct FElysiumReactionBandTable;
struct FElysiumReactionModifierTable;

namespace ElysiumReaction
{
	// The reacting NPC's own clan/kindred-or-kine classification — the fact a modifier row's
	// `Targets` scope (`Kindred(!Gangrel)`, `Kine()`, `ALL`) gates on.
	struct FReactorFacts
	{
		bool bIsKindred = false;
		bool bIsKine = false;
		FName Clan = NAME_None;   // meaningful only when bIsKindred; checked against a `(!X)` exclusion
	};

	// The subject's (the character being reacted TO — ordinarily the player) own history/Merit-Flaw
	// and active-Discipline facts. Every shipped `reactions000.txt` row authors `WhoModifies`
	// `"Others"`, which this runtime reads as "driven by the OTHER party's facts" — the reacting
	// NPC's own reaction changes because of something true about the subject, not about itself
	// (`docs/vtmb/npc-ai/social.md` → "Emotional disposition and social reaction are
	// different domains"). These six are every condition the shipped file names; nothing here
	// invents a seventh.
	struct FSubjectFacts
	{
		bool bHasMegalomaniac = false;          // "Reaction (Megalomaniac)" — doubles reaction toward ALL
		bool bIsCloseToTheBeast = false;        // "Reaction (Close to the Beast)" — Kindred(!Gangrel)/Kine penalty
		bool bHasOccultNut = false;             // "Reaction (Occult Nut)" — Kindred penalty
		bool bDementationPassionActive = false; // "Reaction (Dementation-Passion)" — Kindred, amplifies away from 50

		// The file ships two independent Presence bonuses, both `Targets`-less (unconditional), with
		// no stated exclusivity or stacking rule between them. CHOSEN, NOT RECOVERED: this runtime
		// applies each independently rather than inventing a rank hierarchy that collapses them into
		// one "active Presence rank" — a caller that wants only one active leaves the other's flag
		// false.
		bool bPresenceAweActive = false;        // "Reaction (Presence-Awe)" — flat +20
		bool bPresenceGeneralActive = false;    // "Reaction (Presence-General)" — flat +10
	};

	struct FComputeParams
	{
		int32 BaseScore = 0;
		FReactorFacts Reactor;
		FSubjectFacts Subject;
	};

	struct FComputeResult
	{
		int32 FinalScore = 0;
		FString BandLabel;
		int32 BandLowerBoundary = 0;
		// False only when `Bands` had no rows to resolve against (an unloaded/missing
		// `reaction.txt`) — a structured failure the caller decides how to surface. The rulebook
		// subsystem's `Reactions()` accessor already logs a load failure once; this does not log
		// again for the same cause.
		bool bResolved = false;
	};

	// Evaluates a `reactions000.txt` Modifier formula (`+ - * /`, parens, decimal numbers, and the
	// identifier `Reaction` substituted with `ReactionValue`). Returns false on a malformed
	// expression, leaving OutValue at ReactionValue unchanged.
	bool TryEvaluateFormula(const FString& Expression, double ReactionValue, double& OutValue);

	// Applies every applicable `Modifiers` row over `Params.BaseScore`, in file order (CHOSEN, NOT
	// RECOVERED — neither file states an application order across categories, groups, or rows within
	// a group; History runs before Discipline, and each in its own authored row order, because that
	// is the only order either file expresses), then clamps into
	// `[Bands.MinBoundary(), Bands.MaxBoundary()]` when `Bands.bClamping` (`reaction.txt`'s own
	// `"Clamping" "1"`) and resolves the clamped score to its band.
	FComputeResult Compute(const FComputeParams& Params, const FElysiumReactionBandTable& Bands,
		const FElysiumReactionModifierTable& Modifiers);
}
