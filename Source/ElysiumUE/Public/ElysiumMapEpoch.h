#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

// The map epoch as a value. One epoch spans one AElysiumMapActor's
// life: minted when the actor enters play, retired when it leaves. Anything an application-lifetime
// object holds ON BEHALF OF a map is keyed by it, so the boundary that frees it is a broadcast
// rather than a hand-maintained list of collaborators.
//
// Kept engine-neutral so the mint and the stale-retire rule are testable without a world, the same
// reason FElysiumMapRuntimePrerequisites is a plain struct.
struct FElysiumMapEpoch
{
	// Mint the next epoch and make it current. Never returns 0, so 0 is always "no live epoch"
	// and a default-constructed handle matches nothing.
	uint64 Begin()
	{
		CurrentEpoch = NextEpoch++;
		return CurrentEpoch;
	}

	uint64 Current() const { return CurrentEpoch; }

	// A retire is honoured only for the live epoch. Two worlds briefly overlap during hard travel
	// (the outgoing actor's EndPlay can run after the incoming epoch is minted), so a late retire
	// carrying the older number must not free the new map's state.
	bool ShouldRetire(uint64 Epoch) const { return Epoch != 0 && Epoch == CurrentEpoch; }

	// Idempotent: the second retire of the same epoch is a no-op, because the first cleared it.
	void Retire(uint64 Epoch)
	{
		if (ShouldRetire(Epoch))
		{
			CurrentEpoch = 0;
		}
	}

private:
	uint64 CurrentEpoch = 0;
	uint64 NextEpoch = 1;
};

// Broadcast by UElysiumMapSubsystem, the only owner of map lifecycle. Subscribers bind in their
// own Initialize and unbind in Deinitialize; ordering between
// them is deliberately undefined, because nothing retired at this boundary depends on anything else
// retired at it.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumMapEpochBegin, uint64);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumMapEpochRetired, uint64);
