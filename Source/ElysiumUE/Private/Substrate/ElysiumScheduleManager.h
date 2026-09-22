// `CAI_ScheduleManager` -- the one store every compiled schedule lands in, and the two lookups.
//
// Retail's is a singly linked list at `DAT_10936b68`: `0x1030c5b0` allocates the `0x48`-byte node
// and links it at the HEAD, `0x1030f350` finds by name (`strcmpi` on `+0x40`) and `0x1030f300`
// finds by the global id at `+0x1c`. The list is global, not per class: one namespace of names
// across every space, which is exactly why a schedule id is global and a task id is class-local.
//
// This port keeps an array in authored order and two indices beside it, and the indices answer
// NEWEST first so a duplicate name resolves the way a head-linked list resolves it. That matters
// once: retail's parser refuses a name declared twice inside ONE text, but nothing stops two
// classes from registering the same name, and then the last one loaded is the one every lookup
// finds.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Substrate/ElysiumScheduleText.h"

/**
 * Every compiled schedule in the session.
 *
 * Nothing here parses. `FElysiumScheduleCorpus` feeds it what `ElysiumScheduleText::Parse`
 * returned, text by text, in the order the owning classes load.
 */
class FElysiumScheduleManager
{
public:
	/** Link one program in. Takes it whole: a program is a task array and two 256-bit masks, and
	 *  copying it per lookup is not something the store should invite. */
	void Add(FElysiumScheduleProgram&& Program);

	/** Everything one text parsed into, in authored order. Convenience over `Add`. */
	void AddAll(FElysiumScheduleParseResult&& Result);

	/** `0x1030f350`. Case-insensitive, newest first. */
	const FElysiumScheduleProgram* FindByName(const FString& Name) const;

	/** `0x1030f300`, keyed on the GLOBAL schedule id. Newest first. */
	const FElysiumScheduleProgram* FindById(int32 GlobalId) const;

	const TArray<FElysiumScheduleProgram>& Programs() const { return Store; }

	int32 Num() const { return Store.Num(); }

	/** How many programs carry the same name as an earlier one -- the count that says whether the
	 *  newest-first rule is load-bearing in the shipped corpus or merely correct. */
	int32 ShadowedCount() const { return NumShadowed; }

	void Reset();

private:
	TArray<FElysiumScheduleProgram> Store;

	/** Folded name -> the index of the program a lookup answers. */
	TMap<FString, int32> ByName;

	/** Global id -> the index of the program a lookup answers. Ids below zero are not indexed:
	 *  a program whose name did not resolve has no id and cannot be found by one. */
	TMap<int32, int32> ById;

	int32 NumShadowed = 0;
};
