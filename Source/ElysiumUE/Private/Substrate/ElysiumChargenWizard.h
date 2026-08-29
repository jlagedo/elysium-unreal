#pragma once

#include "CoreMinimal.h"

// --- charcreatewizard.txt — the chargen personality quiz and its clan scoring ---
//
// The wizard is a `client.dll` panel, so nothing here is a layout: `Region`/`TextRegion` are read
// as authored *intent* (which answer sits above which, how much room the question wanted), never as
// a runtime coordinate system (`docs/project/remaster-direction.md` axis 1). What is load-bearing is the graph —
// which popup follows which, which answer increments which abstract trait, and how the tallies
// score each clan. The model: `docs/vtmb/game_runtime.md` → "Chargen — a personality quiz".

// A rectangle exactly as authored, in the file's own 1024x768 terms.
struct FElysiumWizRegion
{
	int32 X = 0, Y = 0, Width = 0, Height = 0;
	bool bAuthored = false;
};

// One `Trait_Prereq`. Either bound may be absent, and absent means unbounded — a popup that only
// authors `MaxVal` admits every tally at or below it, including a trait never yet incremented.
struct FElysiumWizPrereq
{
	FString Trait;
	int32 MinVal = MIN_int32;
	int32 MaxVal = MAX_int32;

	bool Admits(int32 Tally) const { return Tally >= MinVal && Tally <= MaxVal; }
};

// A button, and what clicking it does. At most three are shown; the rest of a block's Actions are
// `KeyLookup` alternates, selected by the wizard rather than drawn alongside.
struct FElysiumWizAction
{
	FString Text;
	FString Next;              // the InternalName of the popup to go to
	FString Trait;             // the abstract trait this answer increments, or empty
	FString CharTemplate;      // sets the player's clan/template outright
	bool bSetGenderMale = false;
	bool bSetGenderFemale = false;
	bool bIsCheckBox = false;         // only triggers if still checked when the popup closes
	bool bEndCharGenWiz = false;
	bool bProcessTraitChoices = false;
	// **Absent is not zero.** `"KeyLookup" "0"` is a real alternate distinct from a plain action, so
	// the unauthored state needs its own value.
	int32 KeyLookup = INDEX_NONE;
	TArray<FElysiumWizPrereq> Prereqs;
	FElysiumWizRegion Region;
};

struct FElysiumWizPopup
{
	FString Text;
	FString InternalName;      // NOT unique — popups share one and the wizard picks among them
	FString CharTemplate;
	FString BkgImage;
	bool bOrderPrereqs = false;
	bool bClanPrereqs = false;
	TArray<FElysiumWizPrereq> Prereqs;
	TArray<FElysiumWizAction> Actions;
	FElysiumWizRegion Region;
	FElysiumWizRegion TextRegion;

	bool IsValid() const { return !InternalName.IsEmpty(); }
};

// When a group hands off to the next one, and on what answered-count.
struct FElysiumWizNextSection
{
	FString Next;
	int32 MinCount = INDEX_NONE;
	int32 MaxCount = INDEX_NONE;
};

// One of the ten `*_Popups` blocks. Its `Defaults` is a popup-shaped block every member inherits
// from field by field — including the positional `Action` list, which concrete popups override by
// index rather than replace.
struct FElysiumWizGroup
{
	FString InternalName;
	FElysiumWizNextSection NextSection;
	FElysiumWizPopup Defaults;
	TArray<FElysiumWizPopup> Popups;
};

// How traits group for the ordering questions.
struct FElysiumWizCombination
{
	FString InternalName;
	TArray<FString> Traits;
};

struct FElysiumWizOrderingStep
{
	FString TraitCombination;
	FString Index;             // "Primary" | "Secondary" | "Tertiary"
};

struct FElysiumWizTraitOrdering
{
	FString TraitCombination;
	FString Index;
	FString Trait;
	TArray<FElysiumWizPrereq> Prereqs;
	TArray<FElysiumWizOrderingStep> Orderings;
};

// A clan's ranked traits. **A rank repeats** — Gangrel authors two `Primary`s — so each is a list.
struct FElysiumWizClanNode
{
	FString CharTemplate;
	TArray<FString> Primary;
	TArray<FString> Secondary;
	TArray<FString> Tertiary;

	// The rank this clan gives a trait: 0 Primary, 1 Secondary, 2 Tertiary, INDEX_NONE if unranked.
	int32 RankOf(const FString& Trait) const;
};

struct FElysiumWizard
{
	TArray<FString> Traits;                     // the 8 abstract traits, in file order
	TArray<FElysiumWizCombination> Combinations;
	TArray<FElysiumWizTraitOrdering> Orderings;
	TArray<FElysiumWizClanNode> ClanNodes;
	// `ConnectionScores`: [selection 0..2][clan rank 0..2]. The payoff for the player's n-th chosen
	// trait meeting a clan that ranks it n-th.
	int32 ConnectionScores[3][3] = {};
	TArray<FElysiumWizGroup> Groups;
	TArray<FString> GroupNames;                 // Strings.PopUpGroups, the authored group order

	bool Load(FString& OutError);
	bool IsValid() const { return !Groups.IsEmpty(); }

	const FElysiumWizGroup* Group(const FString& InternalName) const;
	// Every popup with this InternalName, across all groups — the set the wizard picks from.
	void PopupsNamed(const FString& InternalName, TArray<const FElysiumWizPopup*>& Out) const;
	const FElysiumWizClanNode* ClanNode(const FString& CharTemplate) const;

	int32 NumPopups() const;

private:
	TMap<FString, int32> GroupByName;
};
