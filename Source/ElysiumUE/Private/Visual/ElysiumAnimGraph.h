#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumGraphState.h"

// The rules that project a selection onto the player graph's state vocabulary (CCC5).
//
// Pure: a selection record in, a state and a duration out. No UObject, no asset, no world — which
// is what lets `Elysium.Substrate.AnimationGraph` assert the whole projection on the stack, and what
// keeps the graph itself free of any decision. The graph reads these answers; it does not compute
// them.
//
// The eight states themselves are `ElysiumGraphState.h`: the selection record names the state it
// resolved to, so the vocabulary crosses the substrate boundary with the record while these rules
// stay here.

namespace ElysiumAnimGraph
{
	// The duration baked into every transition in the authored graph, as its inertialization
	// request. It is a CEILING and never a value read off VtMB: the authored fade arrives at runtime
	// through `TransitionSeconds` below, and requests merge by taking the smaller, so the runtime
	// answer always wins. It exists so a frame whose request did not land blends visibly wrong
	// rather than hard-cutting invisibly.
	//
	// It is also the reason the graph asset encodes no game-derived timing, which
	// `Content/ElysiumAuthored/README.md` forbids outright.
	inline constexpr float TransitionCeilingSeconds = 0.5f;

	// The graph tag on the upper-body `FAnimNode_LayeredBoneBlend` (CCC10), read by both the
	// generator that stamps it and the native instance that looks the node up, so the two cannot
	// drift into two spellings of one node.
	//
	// **The mask is not a pin, and that is the engine's design rather than an omission.**
	// `FAnimNode_LayeredBoneBlend::BlendMasks` is edit-time state with no pin of its own, so a mask
	// that changes per selection is supplied at runtime through `SetBlendMask` — which is exactly
	// what Epic's own `ULayeredBoneBlendLibrary::SetBlendMask` does, resolving the mask BY NAME
	// against the playing skeleton. `FAnimSubsystem_Tag` is how native code reaches a tagged node.
	//
	// A **null** mask on a template Animation Blueprint is legal by construction:
	// `UAnimGraphNode_LayeredBoneBlend::ValidateAnimNodeDuringCompilation` raises its null-mask error
	// only when `!bIsTemplate`, and this graph is a template. That is what keeps the tracked graph
	// text free of any reference to a generated, game-derived profile asset.
	inline constexpr const TCHAR* UpperBodyLayerTag = TEXT("ElysiumUpperBodyLayer");

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

	// Which of the eight states realizes an activity. **The one projection**: the resolver runs it
	// once and the answer rides on the record as `FElysiumAnimationSelection::GraphState`, so the
	// anim instance, the Cog row, the trace and the MCP surface all read the same state rather than
	// each deriving one.
	//
	// Everything outside the slice lands on `Idle`, which is a decision rather than a fallback: a
	// body that cannot say what it is doing should stand, and the record still names the activity it
	// asked for.
	EElysiumGraphState StateForActivity(EElysiumAnimActivityCode Code);

	// The ACT_* the debug/grid stand path publishes so the record's state reads as this one. Walk is
	// the default stand because it has no one-shot completion contract.
	const TCHAR* ActivityForState(EElysiumGraphState State);

	// Whether this state evaluates a blend space. Every locomotion state carries the
	// sequence-or-blend-space pair, so a grid-shaped selection is playable in all eight.
	bool StateCanPlayBlendSpace(EElysiumGraphState State);

	// Whether a state can play a selection of this shape at all — the activity-to-state coverage
	// rule in one predicate, so a slice acceptance can walk every request it can emit against it
	// instead of spot-checking the grid half.
	//
	// `None` is playable by construction: there is nothing to play, and the graph answers a miss by
	// holding the pose it has. `Layer` never is — a masked overlay rides the layered blend and is
	// never a base pose, which is the same rule that refuses an additive from the base channel.
	bool StateCanPlay(EElysiumGraphState State, EElysiumAnimAssetKind Kind);

	// If the selection is a blend space whose target state cannot play one, refuse it in place and
	// name the miss (label, state, asset form). Returns true when it refused.
	bool RefuseUnplayableGrid(FElysiumAnimationSelection& Selection);

	// Whether a state plays a one-shot that has to end somehow. `Leap`, `Land` and `Crouch` are
	// non-looping in the authored data; the other five run until the request changes.
	bool IsOneShotState(EElysiumGraphState State);

	// Whether the graph should repeat the clip a state is playing, given the model's own loop bit.
	//
	// It is the authored bit for every state but one. A **held stance** repeats its non-looping
	// into-pose, because that is what retail does by a different mechanism: `StudioFrameAdvance`
	// clamps the finished cycle, the next unchanged request marks the selection dirty, and
	// `ResetSequenceInfo` replays the same clip (`docs/vtmb/animation_and_movers.md`). Freezing on
	// the terminal frame instead is recorded as **not faithful**, so this is a defect fix.
	//
	// `Leap` and `Land` are one-shots that END, and must not be caught by this: a looping landing
	// never reports complete, and the latch would hold the body in phase 8 forever.
	bool ShouldRepeatClip(EElysiumGraphState State, bool bAuthoredLooping);

	// =============================================================================================
	// Three decisions that each cost a visible defect once. They are pure so they are asserted with
	// no world and no body — every one of them failed in a way that compiled, exported and ran green.
	// =============================================================================================

	// Whether a `GetRelevantAnimTimeRemaining` answer describes the clip that was asked for.
	//
	// **The engine's failure value is `MAX_flt`, not zero.** Both
	// `FAnimNode_StateMachine::GetRelevantAnimTimeRemaining` and `FAnimInstanceProxy`'s wrapper
	// return it when no relevant asset player or no state machine is found. Read as a duration that
	// is "a very long time left", it reports a clip as still playing forever — which parks whatever
	// consumes it. Bounding against the clip's own length is what separates an answer from a
	// refusal, because no honest remaining time exceeds the sequence it belongs to.
	bool IsPlayableRemaining(float RemainingSeconds, float ClipLengthSeconds);

	// Whether a request that resolved no asset should hold the pose it already has.
	//
	// Retail's answer, not a guard: a failed selection never reaches `ResetSequenceInfo`, so
	// `m_nSequence` keeps what it held and the body goes on playing it. Projecting the miss instead
	// leaves an asset pin null, and a sequence player with no asset evaluates to the skeleton's bind
	// pose — a visible T-pose. A body that has published nothing yet has no pose to hold, so it
	// cannot take this path.
	bool ShouldHoldPose(bool bHasAppliedOnce, bool bHasSequence, bool bHasBlendSpace);

	// The one-shot answer a caller should feed the jump latch, from what it can see.
	//
	// The two collapses that matter: a request resolving **no asset** is a finished one-shot rather
	// than an unanswerable one — there is no clip to wait for, and treating it as unknown parks the
	// body in its previous pose for the whole fallback window, which reads as floating after a
	// ducked landing. And a report whose generation has moved on describes a clip that is no longer
	// playing, so it must answer `Unknown` rather than end the request that replaced it.
	EElysiumOneShotState OneShotStateFor(bool bHasAsset, bool bGenerationMatches,
		bool bInOneShotState, bool bComplete);
}
