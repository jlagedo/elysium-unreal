#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestTables.h"

// The journal as a screen reads it: the player's rows joined to the catalogue, split into the three
// columns the quest log draws, for one hub. A pure function over a catalogue and an array — no world,
// no subsystem, no disk — so every rule below is testable the way `ElysiumQuestLog`'s are.
//
// This is the read side of the same data `ElysiumQuestLog` writes. The console verb and the screen
// both go through it, which is what stops them describing the journal differently.

namespace ElysiumQuestView
{
	// One row, resolved. `bResolved` false means the catalogue has no quest at the row's address:
	// the row still appears, carrying its own title and no description, because the journal is the
	// truth and the catalogue only decorates it. That is what `elysium.quest` already does with "?".
	struct FEntry
	{
		FString Title;          // the catalogue's spelling, as stored on the row
		FString DisplayName;    // the heading; falls back to Title when unresolved
		FString Description;    // the CURRENT completion state's text; empty when unresolved
		ElysiumQuestLog::EType Type = ElysiumQuestLog::EType::Incomplete;
		// The authored `Type` spelling, verbatim, empty when unresolved. `Type` above is what it
		// PARSES to; this is what the file says, which is what a debug read wants to see.
		FString RawType;

		int32 Table = INDEX_NONE;   // owning quest table = the hub
		int32 State = 0;            // 1-based ordinal in file order
		int32 Order = 0;            // assignment order, max+1 at first assignment

		bool bUnread = false;
		bool bResolved = false;
	};

	// One hub's worth, already sorted and split.
	struct FView
	{
		// Newest assignment first. `Order` is assigned once as max+1, so it IS the order the player
		// took the work on — which is what makes numbering the entries meaningful rather than decorative.
		TArray<FEntry> Active;
		TArray<FEntry> Completed;
		TArray<FEntry> Failed;

		// Active count per table, for the tab row. A `main` row counts in every hub because it is
		// shown in every hub, so the counts describe the tabs rather than the journal.
		int32 HubActive[FElysiumQuestTables::NumTables] = {};

		int32 Num() const { return Active.Num() + Completed.Num() + Failed.Num(); }
	};

	// Join one journal row to the catalogue. This is the seam the screen and the `elysium.quest` verb
	// share: they present differently (columns per hub vs one flat list) but must agree on what a row
	// SAYS — its heading, its current description, its type, and whether it resolved at all.
	FEntry ResolveRow(const FElysiumQuestTables& Tables, const FElysiumAssignedQuest& Row);

	// Build the view for `Hub`. Rules, all of them VtMB's except where marked:
	//   * Rows on `FElysiumQuestTables::MainTable` appear under EVERY hub — `main` is not a place, so
	//     it has no tab, and its rows would otherwise be unreachable.
	//   * A row whose table is unknown (INDEX_NONE) also appears under every hub, for the same reason:
	//     a row nothing can display is worse than a row displayed twice.
	//   * The column is `ElysiumQuestLog::ParseType` of the current state's `Type`. `botch` — which no
	//     shipped row authors — reads as still-open and lands in Active.
	//   * An unresolvable row is Active, since a quest with no readable state is not finished.
	FView Build(const FElysiumQuestTables& Tables, const TArray<FElysiumAssignedQuest>& Journal, int32 Hub);

	// Which hub to open on when the record carries no selection yet (`m_iCurrQuestLogArea` is
	// INDEX_NONE). The hub with the most open work, ties broken by tab order, Santa Monica when the
	// journal is empty. **Ours** — VtMB persists the selection and we have no capture of its initial
	// value, so this only ever applies to a character who has never opened the screen.
	int32 DefaultHub(const FElysiumQuestTables& Tables, const TArray<FElysiumAssignedQuest>& Journal);
}
