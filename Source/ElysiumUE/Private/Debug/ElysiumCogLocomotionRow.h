#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"

struct FElysiumAnimationSelection;
struct FElysiumLocomotionSample;

// One row of the shared body record (CCC1/CCC4), drawn by one function for every producer.
//
// **The contract is the point.** `FElysiumLocomotionSample` has two producers — the player's mover
// publishes its row at its own tick tail, an NPC motor's is pulled — and the claim the contract
// makes is that these are the same record. Two panels that happen to look alike would let that claim
// rot silently; one function over one struct means the day the player's row needs a column an NPC's
// does not is the day the contract has already split, and it shows here.
//
// It lives outside any one window because a third reader arrived: the green room's drive mode watches
// the same row while a human walks the body around the gym.
namespace ElysiumCogLocomotion
{
	// The seventeen headers the row fills, in order. A caller opens a 17-column table and calls
	// this; the count is not a caller's to know.
	inline constexpr int32 NumColumns = 17;
	void SetupColumns();

	// `Sel` is null-tolerant: a body can publish a sample before it has ever resolved anything.
	void Row(const char* Producer, const char* Name, const FElysiumLocomotionSample& Sample,
		const FElysiumAnimationSelection* Sel);
}

#endif // ENABLE_COG
