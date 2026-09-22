// `CAI_GlobalIdSpace` -- one of the four flat name -> global-id tables every class registers into.
//
// Retail keeps two words: the symbol table, and a next-free counter seeded at 1,000,000,000.
// `0x102ea070` answers the counter and `0x102e9fe0` inserts a name and raises it to `id + 1`, so a
// space that takes the counter as its own global base at `Init` begins exactly where the previous
// space ended. That is the whole mechanism by which two classes that both register a local `0x156`
// end up with different global ids -- and `0x156` really does name six different schedules in six
// different tables.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Substrate/ElysiumScheduleId.h"

/**
 * One global namespace: every name registered into it, and the next global id it will hand out.
 *
 * Lookup is case-insensitive, because every name compare in the schedule parser and in
 * `CAI_GlobalIdSpace` is `strcmpi`.
 */
struct FElysiumIdNamespace
{
	/** What this namespace is for, in retail's own vocabulary: `schedule`, `task`, `condition` or
	 *  `squadslot`. Carried for diagnostics only; nothing dispatches on it. */
	FString Category;

	/** `0x102e9fe0`: insert `Name -> GlobalId` and raise the counter to `GlobalId + 1`.
	 *  A `-1` id inserts nothing, as retail's guard does. */
	void Insert(const FString& Name, int32 GlobalId);

	/** `0x102ea050`: the global id registered under a name, or `INDEX_NONE`.
	 *
	 *  The answer is the STORED id, verbatim -- there is no arithmetic on the way out. A caller
	 *  that wants a class-local number translates afterwards, through that class's space. */
	int32 Find(const FString& Name) const;

	/** `0x102ea070`: the next global id this namespace will hand out. A space takes this as its
	 *  own global base when it initialises. */
	int32 NextFree() const { return NextGlobalId; }

	/** Back to the empty, freshly seeded state. */
	void Reset();

	int32 Num() const { return Symbols.Num(); }

	/** Every registered name, for the census and for a debug readout. */
	const TMap<FString, int32>& Rows() const { return Symbols; }

private:
	/** Folded name -> global id. Folded because retail compares with `strcmpi`. */
	TMap<FString, int32> Symbols;

	/** The high-water mark, seeded at `ElysiumScheduleId::GlobalBase`. */
	int32 NextGlobalId = ElysiumScheduleId::GlobalBase;
};
