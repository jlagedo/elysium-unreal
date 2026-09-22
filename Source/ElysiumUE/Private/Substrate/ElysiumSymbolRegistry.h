// The three run-time name tables the schedule parser resolves operands against, as one type.
//
// `Activity:` (`0x1025d760` over the registry `DAT_1090fbe0`), `Model:` (`0x1030d3d0` over the
// symbol table `DAT_10936b74`) and `SOUND:` (`0x1030d400` over the `{id, name}` array
// `DAT_1073dc3c` / `DAT_1073dc40`) are the same mechanism three times: a flat name -> id table that
// something else fills before the first text is parsed, and a resolver that looks a name up in it.
// One type serves all three; each instance names the retail field it stands for.
//
// **Stated divergence.** Retail Errors on a miss and the calling init body then abandons the rest
// of that class's texts. Here the three tables are filled by systems this port has not built (the
// activity list, the precache path, the VSound table), so retail's behaviour would delete hundreds
// of real programs over a name this runtime cannot yet know. These registries therefore INTERN
// instead of failing: an unknown name takes the next id and is tallied once. The tally is the seam
// -- it says exactly which names had no source -- and the parse survives.
//
// Only `Activity:` has a consumer: the `TASK_SET_ACTIVITY` arm maps the stored id back to its name
// for `PlayActivity`, which is how an authored activity leaves the two-word task record without
// being lost. `Model:` and `SOUND:` are read by no ported task, and a pass-E test asserts it.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"

/**
 * A flat name -> id table that never fails a lookup.
 *
 * Ids are dense from zero and stable within a session, which is what lets a task record carry an
 * id rather than a string. They are NOT retail's ids and are not saved: the save CRC hashes the
 * task id and the raw operand word, and an activity operand's word is one of these.
 */
struct FElysiumSymbolRegistry
{
	FElysiumSymbolRegistry() = default;

	/** `InRetailField` is the retail global this table stands for, carried for the seam report. */
	explicit FElysiumSymbolRegistry(const TCHAR* InRetailField)
		: RetailField(InRetailField)
	{
	}

	/** The id for `Name`, minting one if this is the first time it has been seen.
	 *
	 *  A name minted rather than found is counted in `NumInterned` -- the count of operands whose
	 *  retail source does not exist in this runtime yet. */
	int32 Intern(const FString& Name);

	/** The id registered under `Name`, or `INDEX_NONE`. This is the arm that would be retail's
	 *  resolver; nothing calls it during a parse, because a parse interns. */
	int32 Find(const FString& Name) const;

	/** The name an id was minted for, or null. `TASK_SET_ACTIVITY`'s reverse lookup. */
	const FString* NameOf(int32 Id) const;

	int32 Num() const { return Names.Num(); }

	/** How many of the registered names had no source and were minted by a parse. */
	int32 InternedCount() const { return NumInterned; }

	void Reset();

	/** The retail global whose contents this stands in for. */
	const TCHAR* RetailField = TEXT("");

private:
	/** Folded name -> id. Folded because every resolver compares with `strcmpi`. */
	TMap<FString, int32> Ids;

	/** Id -> the name as first authored, for the reverse lookup and the report. */
	TArray<FString> Names;

	int32 NumInterned = 0;
};
