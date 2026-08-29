#pragma once

#include "CoreMinimal.h"

// Identity is a generation-checked handle; targetnames are non-unique.
//
// Index is the entity's position in the map's parsed `.ents` def array: stable across
// runs, never reused within a single map load (killed entities are marked dead, not
// recycled). Epoch is the entity world's teardown generation — every handle minted for
// a map load carries that load's epoch, and a map teardown bumps the world epoch so all
// outstanding handles go stale at once.
//
// A handle is only *resolved* by FElysiumEntityWorld: resolution returns null when
// the index is unset, the epoch does not match the live world, or the entity is dead —
// that is the "falsy when dead/stale" contract VtMB scripts rely on (`if(ent):` after a
// delete). This struct is the pure value; it carries no world pointer, so IsSet() here is
// only the structural check (a handle that was actually bound to some entity). Everything
// beyond that is the world's job.
struct FElysiumEntityHandle
{
	int32 Index = INDEX_NONE;
	uint32 Epoch = 0;

	FElysiumEntityHandle() = default;
	FElysiumEntityHandle(int32 InIndex, uint32 InEpoch)
		: Index(InIndex), Epoch(InEpoch) {}

	// The canonical unbound handle (never pointed at any entity).
	static FElysiumEntityHandle Invalid() { return FElysiumEntityHandle(); }

	// Structurally bound to some entity index. NOT a liveness check — a handle can be
	// set yet stale (epoch mismatch) or dead; only FElysiumEntityWorld::Resolve knows.
	bool IsSet() const { return Index != INDEX_NONE; }

	bool operator==(const FElysiumEntityHandle& Other) const
	{
		return Index == Other.Index && Epoch == Other.Epoch;
	}
	bool operator!=(const FElysiumEntityHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FElysiumEntityHandle& H)
	{
		return HashCombine(::GetTypeHash(H.Index), ::GetTypeHash(H.Epoch));
	}

	// `#<index>` — the index half of the canonical debug string `#<idx> <name>(<class>)`
	// (the world fills in name/class when it can resolve the handle).
	FString ToString() const
	{
		return IsSet() ? FString::Printf(TEXT("#%d"), Index) : TEXT("#<null>");
	}
};
