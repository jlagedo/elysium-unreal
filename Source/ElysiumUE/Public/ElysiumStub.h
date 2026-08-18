#pragma once

#include "CoreMinimal.h"

struct FElysiumVariant;
struct FElysiumInputArgs;

// The one place the runtime says "this surface has no implementation yet".
//
// Every unimplemented surface reports here rather than logging its own shape: a stubbed native, an
// input that registers but does nothing, a classname the registry has no leaf for, a sheet
// attribute no compiled slot owns. One dialect means the gaps are one greppable line and one
// tally instead of a dozen spellings scattered across the layers. The line is deliberately loud —
// it is a work list, not a diagnostic:
//
//   Stub fired!!!!! [input] env_shake.StartShake | on 'earthquake'
//     | params: param="" activator=#41 caller=#38 | owner: screen shake
//
// `elysium.stubs` reads the tally back and `elysium.stubs clear` resets it. `elysium.StubWarn`
// sets the volume: 2 (default) warns on every fire, 1 warns once per surface, 0 tallies silently.
//
// Game-thread only, like the rest of the substrate — the tally takes no lock.
namespace ElysiumStub
{
	// `Kind` is the surface category, short and lowercase: input, native, method, field, class.
	// `Surface` identifies it uniquely and is what the tally is keyed on, so it carries the class
	// and member name but never an instance name or an argument value. `Receiver` is the entity the
	// call landed on (empty when the surface is not per-entity), `Params` the marshalled arguments,
	// and `Owner` what would implement it.
	void Fired(const TCHAR* Kind, const FString& Surface, const FString& Receiver,
		const FString& Params, const FString& Owner);

	// The two argument shapes the call sites have. `DescribeInput` spells an I/O input's whole
	// dispatch context (param plus provenance); `DescribeArgs` a script call's argument list.
	FString DescribeInput(const FElysiumInputArgs& Args);
	FString DescribeArgs(TArrayView<const FElysiumVariant> Args);

	// One tallied surface. `Count` is fires since load or since the last `elysium.stubs clear`.
	struct FTally
	{
		FString Kind;
		FString Surface;
		FString Owner;
		int32 Count = 0;
	};

	// Every tallied surface, most-fired first.
	void CollectTally(TArray<FTally>& Out);
	void ClearTally();
}
