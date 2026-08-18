#pragma once

#include "CoreMinimal.h"

#include "ElysiumGraphState.generated.h"

// The body graph's own state vocabulary — the eight names, spelled once (CCC5).
//
// It lives beside the animation records rather than with the graph's projection rules because the
// **selection record names the state it resolved to**, and a record that crosses the substrate
// boundary cannot carry a type the graph keeps to itself. The rules that decide *which* state a
// request lands in stay with the graph, in `Visual/ElysiumAnimGraph.h`.

// Eight states, deliberately NOT the classifier's thirteen activity codes.
//
// The projection between the two is where the graph's declared answers live: `ACT_LAND_CROUCH`
// resolves nothing on a validated player body, the relaxed gaits are what the classifier emits
// before translation, and swimming is reachable but outside the slice. A body asking for any of
// those still has to stand somewhere, and naming the eight states separately is what makes "where"
// a stated rule rather than an accident of an `if` chain inside the graph.
UENUM()
enum class EElysiumGraphState : uint8
{
	Idle,
	Walk,
	Run,
	Sneak,
	Crouch,
	Leap,
	Falling,
	Land,
};

namespace ElysiumAnimGraph
{
	// How many states there are, for a caller sizing an array by them.
	inline constexpr int32 NumGraphStates = 8;

	// The state's name, spelled once. The authored graph's state nodes carry exactly these names, so
	// `Elysium.Substrate.AnimationGraph` can assert the asset against this list rather than against
	// a screenshot.
	const TCHAR* StateName(EElysiumGraphState State);

	// Inverse of `StateName`. False when the string is not one of the eight.
	bool TryParseState(const FString& Name, EElysiumGraphState& OutState);
}
