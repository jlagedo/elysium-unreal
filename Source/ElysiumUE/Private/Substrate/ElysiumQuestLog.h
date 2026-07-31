#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumRulebook.h"

// What happens around a quest state change: resolving the catalogue, deciding whether the awards
// fire, and reconciling the journal row. The awards themselves are the session's — this layer only
// says what is owed, so the whole decision is plain C++ over a catalogue and an array, testable with
// no world and no subsystem. Same split `ElysiumSheetMath` makes for the sheet.
//
// The VtMB facts: `docs/vtmb/game_runtime.md` section 3 ("Quests"), read off `CVPlayer::SetQuest`
// (`1017CC20`) and the two `QuestJournal` halves it calls. Every rule below is the engine's.

namespace ElysiumQuestLog
{
	// What a state change asks the session to do. `AwardMoney` and `Event` are authored in ZERO
	// shipped rows — they are schema with no data behind them, not dead code.
	struct FOutcome
	{
		bool bResolved = false;   // the title named a real quest AND the state exists on it
		bool bChanged  = false;   // the journal moved — false means nothing at all is owed
		bool bBotched  = false;   // refused: the state we are leaving is Type `botch`

		FString AwardXpKey;       // an experience_table KEY, never a number
		int32   AwardMoney = 0;
		FString Event;            // script data, handed to the installed host

		FString Title;            // the catalogue's spelling
		FString DisplayName;      // the journal heading, for the log line and the verb
		FString Type;             // success | failure | incomplete
		int32   Order = 0;        // the row's display order after the change
	};

	// Resolve `Title` at `NewState`, reconcile `Journal`, and report what is owed.
	//
	// Mutates ONLY `Journal`; the caller owns the quest map and performs every award. The rules,
	// all VtMB's:
	//   * An unknown title, or a state the quest does not have, does NOTHING — no row, no award.
	//     (The caller still stores the raw value: default-0-on-miss is the contract 732 call sites
	//     rely on, and our map is the authoritative store where VtMB's catalogue record is.)
	//   * `NewState` is the 1-based ORDINAL of the completion state in file order.
	//   * A row already at `NewState` reports `bChanged == false`: a repeat set awards nothing.
	//   * Any other change awards — INCLUDING a move backwards — unless the state being left is
	//     Type `botch`, which the engine refuses outright.
	//   * `Order` is assigned once, on first assignment, as max(order) + 1 over the journal.
	//   * The row is replaced in place, never appended twice; a title matches case-insensitively.
	FOutcome Apply(const FElysiumQuestTables& Tables, TArray<FElysiumAssignedQuest>& Journal,
		const FString& Title, int32 NewState);

	// The row for a title, or null. Case-insensitive, as the engine's `Q_strnicmp` match is.
	const FElysiumAssignedQuest* FindRow(const TArray<FElysiumAssignedQuest>& Journal,
		const FString& Title);

	// `Type` -> the engine's enum (incomplete 1, success 2, failure 3, botch 4). Resolved by
	// SUBSTRING in that order, with anything unrecognised reading as incomplete — the engine uses
	// `Q_stristr`, not equality, and defaults the key to the literal "incomplete".
	enum class EType : uint8 { Incomplete = 1, Success = 2, Failure = 3, Botch = 4 };
	EType ParseType(const FString& Raw);
}
