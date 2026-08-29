#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumGraphState.h"

// The rules that project a selection onto the player graph's state vocabulary.
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
	// The graph tag on the locomotion `FAnimNode_BlendStack`, read by the generator that stamps
	// it and by the native instance that looks the node up, so the two cannot drift into two
	// spellings of one node.
	//
	// The stack IS the transitioner: one node holds the base channel, every request arrives on its
	// four exposed pins, and the fade duration is the authored value on the `BlendTime` pin rather
	// than a duration baked into a graph edge. The graph asset therefore encodes no game-derived
	// timing at all, which `Content/ElysiumAuthored/README.md` forbids outright.
	inline constexpr const TCHAR* LocomotionStackTag = TEXT("ElysiumLocomotionStack");
	// The sync group the locomotion stack leads and the base channel's `_delta` player follows,
	// which is how a host's autolayer is evaluated at the host's own cycle.
	inline constexpr const TCHAR* BaseSyncGroup = TEXT("ElysiumBase");

	// The graph tag on the upper-body `FAnimNode_LayeredBoneBlend`, read by both the
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

	// The graph tag on the overlay SLOT's own `FAnimNode_LayeredBoneBlend` — retail's
	// `CBaseAnimatingOverlay` slot 0, the masked partial-body layer every ranged fire, reload and
	// dry-fire composes through.
	//
	// **A SECOND layered blend, not the one above.** Retail accumulates its slots AFTER the host
	// sequence's own model-declared autolayers, so the two are ordered rather than alternatives: the
	// autolayer blend composes what the base clip DECLARES, and this one composes what a PRODUCER
	// armed over whatever came out of that. Reusing one node would make a body's carry-pose layer and
	// its fire layer fight for one weight and one mask, and only one of them could ever be on screen.
	//
	// Its mask reaches the node the same way the autolayer blend's does — `SetBlendMask` by name
	// against the playing skeleton, through `UElysiumBipedAnimInstance::ApplySlotMask` — and for the
	// same reason: `BlendMasks` is edit-time state with no pin, and a `UBlendProfile` belongs to one
	// skeleton while a bank owns the layer.
	//
	// **Four of them, chained, composed in slot index order** — retail's own accumulate order, where
	// each layer moves the running pose toward itself by its own weight. Chained rather than folded
	// into one node's four blend poses because the composition is SEQUENTIAL: two layers whose masks
	// overlap (an aim layer and an attack layer both reach the upper body) compose differently when
	// accumulated one after another than when blended simultaneously against one base.
	//
	// The layer each one composes is posed by an `FAnimNode_SequenceEvaluator` pinned to an EXPLICIT
	// time, not a sequence player. A player keeps its own clock, so it would drift from the layer that
	// ends it, it could not be restarted on a re-fire of the same clip, and it publishes no phase.
	// Retail's slot cycle is explicit, so the evaluator is the faithful shape as well as the
	// controllable one — and it needs no tag of its own, because both of its inputs arrive on pins.
	inline FName SlotLayerTag(int32 SlotIndex)
	{
		static_assert(ElysiumOverlay::NumSlots == 4, "the tag table is sized by the slot count");
		static const FName Tags[ElysiumOverlay::NumSlots] = {
			FName(TEXT("ElysiumSlotLayer0")), FName(TEXT("ElysiumSlotLayer1")),
			FName(TEXT("ElysiumSlotLayer2")), FName(TEXT("ElysiumSlotLayer3")) };
		return Tags[FMath::Clamp(SlotIndex, 0, ElysiumOverlay::NumSlots - 1)];
	}
	// The aim grid the SLOT's own clip declares, composed over the shot motion inside that slot's own
	// branch — retail's recursive autolayer rule for the overlay sequence, once per slot.
	inline FName SlotAimLayerTag(int32 SlotIndex)
	{
		static_assert(ElysiumOverlay::NumSlots == 4, "the tag table is sized by the slot count");
		static const FName Tags[ElysiumOverlay::NumSlots] = {
			FName(TEXT("ElysiumSlotAimLayer0")), FName(TEXT("ElysiumSlotAimLayer1")),
			FName(TEXT("ElysiumSlotAimLayer2")), FName(TEXT("ElysiumSlotAimLayer3")) };
		return Tags[FMath::Clamp(SlotIndex, 0, ElysiumOverlay::NumSlots - 1)];
	}

	// The graph tag on retail's per-body bank bone-remap (`vampire.dll FUN_100c67b0`,
	// `FAnimNode_ElysiumBankRemap`), read by both the generator that stamps it and the native
	// instance that pushes the resolved table into it, so the two cannot drift into two spellings
	// of one node.
	//
	// **One tag names the base channel's closure, four more name the overlay slots'** — the same
	// split the mask tags above make, and for the same underlying reason: each closure can be
	// decoded against a DIFFERENT owning bank, so each gets its own node fed its own resolved
	// table rather than one node fighting over which owner it corrects for. `SlotIndex ==
	// INDEX_NONE` asks for the base channel's tag; any other value clamps into the four slots.
	//
	// The node sits after its closure's own pose has composed — the base channel's trio (host,
	// autolayer, `_delta`) or one slot's own — and before anything downstream reads bone
	// translations, which is retail's own order: the closure decodes and composes in the owning
	// bank's space, and the correction is the last thing that happens to it.
	inline FName BankRemapTag(int32 SlotIndex)
	{
		if (SlotIndex == INDEX_NONE)
		{
			static const FName Base(TEXT("ElysiumBaseBankRemap"));
			return Base;
		}
		static_assert(ElysiumOverlay::NumSlots == 4, "the tag table is sized by the slot count");
		static const FName Tags[ElysiumOverlay::NumSlots] = {
			FName(TEXT("ElysiumSlotBankRemap0")), FName(TEXT("ElysiumSlotBankRemap1")),
			FName(TEXT("ElysiumSlotBankRemap2")), FName(TEXT("ElysiumSlotBankRemap3")) };
		return Tags[FMath::Clamp(SlotIndex, 0, ElysiumOverlay::NumSlots - 1)];
	}

	// The graph tag on the reaction branch's own `FAnimNode_BlendListByBool`, read by the
	// generator that stamps it and by anything that has to find the node on a compiled class, so the
	// two cannot drift into two spellings of one node.
	//
	// The branch sits between the one-shot slot and the upper-body layer: `bReactionActive` picks
	// either the reaction pose (a directional hit fan or a single clip) or the locomotion pose
	// underneath it. Its two per-pose blend times ARE the asymmetric fade — `FAnimNode_BlendListBase`
	// takes the newly-active child's own time, so entering the reaction uses the in-fade and
	// returning to the base uses the out-fade.
	inline constexpr const TCHAR* ReactionBranchTag = TEXT("ElysiumReactionBranch");

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
	// **A null `Outgoing` is the second refusal, not a missing operand.** Retail's first gate is
	// `if (!out || !in || (in->flags & 0x2)) return 0.0f;` (`FUN_1008de30` at `0x1008de4d`, recorded
	// in `docs/vtmb/animation_and_movers.md` A.4c), so an absent outgoing descriptor is ranked WITH
	// the hard cut and answers zero — which is why the two are one condition here.
	//
	// It is also the only answer that poses correctly. A body that has published nothing has an
	// un-entered state machine, whose un-published pin evaluates to the skeleton's bind pose; a
	// non-zero fade therefore cross-fades the first real clip up out of a T-pose for the whole
	// duration on map load.
	//
	// **Whether an outgoing record is an operand at all is the CALLER's question, and it is stricter
	// than "is the pointer null".** A first publish whose record resolved no clip is still a publish,
	// so the generation after it would otherwise hand this function a non-null descriptor while the
	// machine underneath is still posing the bind pose — the same T-pose fade through the front door.
	// `UElysiumBipedAnimInstance` therefore latches the applied record's own posed-an-asset verdict at
	// publish time (`bAppliedPosedAnAsset`) and passes null here when it is false, which is the same
	// predicate `PlayOneShot`'s `bGraphPosesAnAsset` applies on the montage path.
	// `FElysiumAnimationSelection::AssetKind` cannot answer it: the assets are resolved beside the
	// record and either can be absent while the other is not.
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

	// Three decisions that each cost a visible defect once. They are pure so they are asserted with
	// no world and no body — every one of them failed in a way that compiled, exported and ran green.

	// Whether a remaining-time answer describes the clip that was asked for.
	//
	// **The bound is two-sided, because a producer can fail in either direction.** The blend stack's
	// remaining time is its current player's own length less its adjusted time, so an empty stack —
	// or one whose asset is not the one that was requested — answers ZERO: the clip reads as
	// *finished*. The opposite failure is `MAX_flt`, which reads as *never finishing* and parks
	// whatever waits on it. Neither is an answer, and this predicate rejects both: no honest
	// remaining time exceeds the sequence it belongs to, and none is negative.
	//
	// A zero is indistinguishable from a genuinely completed clip, so the bound alone cannot catch the
	// stack's failure direction. **The identity gate is what does, and it has to run first** — the
	// caller asks whether the stack's own `GetAnimAsset()` is the asset it requested BEFORE reading
	// any clock off it. Bounding an answer that was never about the right clip only makes it plausible.
	bool IsPlayableRemaining(float RemainingSeconds, float ClipLengthSeconds);

	// Whether a request has to force the blend stack to re-blend even though the asset did not change.
	//
	// `FAnimNode_BlendStack::ConditionalBlendTo` compares the requested asset against the playing one
	// and returns without doing anything when they match — and `bLoop` is consumed only inside
	// `BlendTo`, as an argument to the player it constructs. So a loop bit that flips on the SAME
	// asset is silently ignored: the pin holds the new value, the player goes on with the old one, and
	// nothing logs. `Crouch` is exactly that case (a non-looping into-pose republished as a held
	// stance, `ShouldRepeatClip` above), so the predicate is what keeps the `bLoop` pin honest.
	bool NeedsForcedReblend(bool bSameAsset, bool bLoopChanged);

	// Whether a request that resolved no asset should hold the pose it already has.
	//
	// Retail's answer, not a guard: a failed selection never reaches `ResetSequenceInfo`, so
	// `m_nSequence` keeps what it held and the body goes on playing it. Projecting the miss instead
	// leaves an asset pin null, and a sequence player with no asset evaluates to the skeleton's bind
	// pose — a visible T-pose. A body that has published nothing yet has no pose to hold, so it
	// cannot take this path.
	bool ShouldHoldPose(bool bHasAppliedOnce, bool bHasSequence, bool bHasBlendSpace);

	// Where an overlay slot's `FAnimNode_SequenceEvaluator` stands this frame, in the layer clip's
	// own seconds.
	//
	// **The phase is the LAYER's, projected, never an accumulator the graph advances.** The record's
	// cycle is `FElysiumOverlayLayer::Cycle`, the same number the layer's own weight and its end are
	// read from, so pinning the evaluator to it is what keeps the layer from finishing early or
	// lingering past the layer it belongs to — the two cannot be read off different clocks because
	// there is only one.
	//
	// `bSequenceChanged` re-seats the playhead at the head, and it is not redundant with a cycle that
	// happens to be zero: a publish can hand over a NEW layer asset while still carrying the previous
	// claim's cycle, and seating a fresh clip mid-motion is a visible jump into the middle of a
	// reload. A re-fire of the SAME clip restarts through the cycle instead, because a new claim
	// starts at age zero — which is retail's own restart.
	//
	// A clip with no length has no playhead, so it answers zero rather than a NaN.
	float SlotEvaluatorTime(float Cycle, float ClipLengthSeconds, bool bSequenceChanged);

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
