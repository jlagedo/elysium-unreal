#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"

#include "ElysiumAnimGraph.generated.h"

// The player graph's own vocabulary, and the rules that project a selection onto it (CCC5).
//
// Pure: a selection record in, a state and a duration out. No UObject, no asset, no world — which
// is what lets `Elysium.Substrate.AnimationGraph` assert the whole projection on the stack, and what
// keeps the graph itself free of any decision. The graph reads these answers; it does not compute
// them.

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

	// The duration baked into every transition in the authored graph, as its inertialization
	// request. It is a CEILING and never a value read off VtMB: the authored fade arrives at runtime
	// through `TransitionSeconds` below, and requests merge by taking the smaller, so the runtime
	// answer always wins. It exists so a frame whose request did not land blends visibly wrong
	// rather than hard-cutting invisibly.
	//
	// It is also the reason the graph asset encodes no game-derived timing, which
	// `Content/ElysiumAuthored/README.md` forbids outright.
	inline constexpr float TransitionCeilingSeconds = 0.5f;

	// Retail's own combine (`docs/vtmb/animation_and_movers.md` A.4c): the transition takes the
	// LARGER of the outgoing clip's authored fade and the incoming clip's, scored right on 80 of 80
	// recorded transitions where a current-only rule scores 20 of 26.
	//
	// **The one refusal is a property of the INCOMING clip.** `flags & 0x2` makes the duration <= 0,
	// which discards the whole fading set — an unconditional hard cut decided by the clip being
	// entered rather than by whatever is already running, and the most common authored transition
	// behaviour in the corpus at 2,642 of 5,836 sequences. It arrives here as `bSnap`.
	//
	// Retail's `flags & 0x400` flips the combine from `max` to `min`; no shipped sequence sets it,
	// so that branch is unreachable and is not reproduced.
	//
	// `Outgoing` is null for a body that has not played anything yet, which takes the incoming fade
	// alone rather than pretending the previous clip authored a zero.
	float TransitionSeconds(const FElysiumAnimationSelection* Outgoing,
		const FElysiumAnimationSelection& Incoming);

	// Which of the eight states realizes a selection.
	//
	// Everything outside the slice lands on `Idle`, which is a decision rather than a fallback: a
	// body that cannot say what it is doing should stand, and the record still names the activity it
	// asked for.
	EElysiumGraphState StateFor(const FElysiumAnimationSelection& Selection);

	// The same projection from the classifier's own code, which is what a test drives and what the
	// intent carries before a selection exists.
	EElysiumGraphState StateForActivity(EElysiumAnimActivityCode Code);

	// The state's name, spelled once. The authored graph's state nodes carry exactly these names, so
	// `Elysium.Substrate.AnimationGraph` can assert the asset against this list rather than against
	// a screenshot.
	const TCHAR* StateName(EElysiumGraphState State);

	// Whether a state plays a one-shot that has to end somehow. `Leap`, `Land` and `Crouch` are
	// non-looping in the authored data; the other five run until the request changes.
	bool IsOneShotState(EElysiumGraphState State);
}
