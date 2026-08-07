#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"   // FElysiumFlexWrite (passed by view)
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Visual/ElysiumAnimNodes.h"
// By value: the eye input is a member, so the rig's own header rather than a forward declaration.
#include "Visual/ElysiumFacialRig.h"

#include "ElysiumNpcAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;
struct FElysiumClothRig;
struct FElysiumCompositionRig;

// The NPC animation host (roadmap 8.5) — a native C++ anim instance, no Blueprint and no anim
// graph asset.
//
// It exists for one reason: VtMB's stance banks ship almost no authored transitions. Of the 21
// dispositions × 2 gendered banks, exactly one carries a `Stance_<D>_Trans_<a>_<b>` clip
// (`Stance_Neutral_Trans_1_2`, male only), so a stance change, a fidget, or a
// `scripted_sequence`'s clip cannot route through an authored blend the way the naming first
// suggests. Source blended between sequences itself, so a short engine crossfade reproduces the
// original's feel rather than inventing polish.
//
// Two sequence players and a lerp: a request starts a crossfade from whatever is currently
// playing to the new clip. Repeating the same looping stance is a no-op; a one-shot request or a
// loop-mode change restarts from frame zero, which scripted_sequence/choreo playback requires.
//
// A second, independent track rides over that pose: the face (roadmap 12.3). It is not a clip —
// VtMB authors no facial animation as keyframes — but a rig evaluated from flex controller values
// (`Visual/ElysiumFacialRig.h`), emitted as morph-target curves. The body and the face never
// interact: a crossfade between two stances does not touch the face, and a blink does not touch
// the pose.

USTRUCT()
struct FElysiumNpcAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FElysiumNpcAnimProxy() = default;
	explicit FElysiumNpcAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void CacheBones() override;
	// The cloth chains' game-thread pass, driven from the anim instance rather than through
	// `GetCustomNodes`: that registration is gathered during InitializeAnimation, before any rig
	// exists, and holds raw pointers into an array `SetClothRig` reallocates.
	void PreUpdateCloth(const UAnimInstance* Instance);
	// The node graph's own update — where the sequence players advance their play time. The
	// base-class `Update(float)` is NOT enough: a node that is never Update_AnyThread'd sits at
	// its start position forever and evaluates one frozen frame.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Start playing Sequence. FadeSeconds is the INCOMING clip's own authored fade, not the
	// transition duration — the transition takes the larger of it and the outgoing clip's, which
	// this owns. 0 is the refusal: it snaps AND flushes every clip still fading, which is retail's
	// `flags & 0x2` hard cut. Called from the game thread through UElysiumNpcAnimInstance, never
	// from the worker.
	void Request(UAnimSequence* Sequence, bool bLoop, float FadeSeconds);
	// Pin the current clip to an absolute authored time. Cinematic scenes call this every scene
	// frame, making the pose a function of scene time rather than accumulated animation delta. It
	// does NOT settle a crossfade — a scene seeks the clip it just started, so a Seek that forced the
	// blend to 1 would make every scene's opening pose snap.
	void Seek(float PositionSeconds);
	void Stop();

	// Where the incoming player's clip actually sits, in clip seconds. Negative when nothing is
	// playing — "cannot say" is a different answer from "at zero", and a caller measuring drift
	// must not read the second as the first.
	float GetClipPosition() const;
	// Re-phase a clip that is still PLAYING, without pinning it. Unlike Seek this touches neither
	// the play rate nor the start position nor the crossfade, so a clip mid-blend keeps blending
	// and a free-running clip keeps running — it only moves where it is running from.
	void ResyncPosition(float PositionSeconds);

	// --- blend grids ---------------------------------------------------------------------------
	//
	// Stand the body on a blend space instead of a clip. A VtMB sequence label does not always name
	// one animation: 275 of them name a grid of them, and the baked `BS_<label>` is that grid's mix.
	// While one is set it IS the body pose — `Request` clears it, so exactly one thing owns the body.
	//
	// False for a blend space whose samples are partial-body `*_layer` overlays. Those are a layer's
	// grid, not a base pose, and standing one would pull every bone outside its mask onto the
	// reference pose — the same defect an unmasked `*_layer` reaching the clip path causes, which is
	// what RequestLayer's gate refuses from the other side.
	bool RequestGrid(UBlendSpace* Space, bool bLoop);
	// Where on its axes the grid is sampled, in the pose parameters' own units (degrees). Axis 1 is
	// ignored by a one-dimensional grid. Cheap enough to write every frame — it moves the sample
	// point without touching the play position, so a body being steered keeps its stride.
	void SetGridPosition(float Axis0, float Axis1);
	void StopGrid();
	UBlendSpace* GetGrid() const { return GridPlayer.GetBlendSpace(); }

	// --- autolayers ----------------------------------------------------------------------------
	//
	// Start or re-weight one autolayer. Weight is retail's `layer_weight`, the scalar the
	// accumulator receives from its caller; 1 is the whole layer. Asking for a sequence already on
	// a layer only re-weights it, so a caller may drive the weight every frame without restarting
	// the clip.
	//
	// Which combine it goes through is the SEQUENCE's property, not the caller's — retail's one
	// accumulator branches on the clip's own flags. An additive `_delta` accumulates
	// post-multiplied over every bone; an ordinary `*_layer` is a complementary-weight blend gated
	// by the bones that clip owns. False when the sequence is neither, which is the gate that keeps
	// the second honest — see RequestLayer.
	bool RequestLayer(UAnimSequence* Sequence, bool bLoop, float Weight);
	// Drop one layer, or every layer. Dropping is immediate: an autolayer carries no fade of its
	// own, and the weight IS the ramp.
	void StopLayer(const UAnimSequence* Sequence);
	void StopAllLayers();
	int32 NumLayers() const;

	UAnimSequence* GetPlaying() const { return Playing; }
	bool IsPlayingLoop() const { return bPlayingLoop; }
	// True while any previous clip is still fading out. More than one may be, so this is "a
	// transition is in flight", not "the one crossfade is running".
	bool IsBlending() const { return Fading.Num() > 0; }
	int32 NumFadingClips() const { return Fading.Num(); }

	// The facial morph track (12.3): the rig's evaluated morph weights, published from the game
	// thread and emitted as morph-target anim curves over whatever pose the body produced. Two
	// parallel arrays in the rig's own morph order — a name list that changes only when the body's
	// model does, and weights that change whenever a flex controller is written.
	void SetFacialTrack(TArray<FName>&& InCurves);
	void SetFacialWeights(TArrayView<const float> InWeights);

	// VtMB's one composition stage (CAP7.2); null clears it.
	void SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	int32 NumAxisInterpRules() const { return AxisInterp.NumResolvedRules(); }

	// The garment spike's rig; null clears it. Independent of the composition stage — a model can
	// carry either, both or neither, and nearly every model carries none of this one.
	void SetClothRig(TSharedPtr<const FElysiumClothRig> InRig);
	int32 NumClothChains() const { return Cloth.NumChains(); }
	void SetClothTuning(const FElysiumClothTuning& InTuning) { Cloth.SetTuning(InTuning); }
	const FElysiumClothTuning& GetClothTuning() const { return Cloth.GetTuning(); }

private:
	// The body half of Evaluate: the crossfade between the two sequence players.
	void EvaluateBody(FPoseContext& Output);
	// The autolayer half: VtMB's layers composed onto the body pose in LOCAL space — a `_delta`
	// accumulated post-multiplied, a `*_layer` blended under its bone mask. Runs between the body
	// and the composition stages, which is retail's own order — see the definition.
	void EvaluateLayers(FPoseContext& Output);
	// Read one slot's bone gate out of the sequence's own metadata and the skeleton's blend
	// profile. Game thread, at the request: the worker has no business resolving an asset.
	void ResolveLayerMask(int32 Layer, const UAnimSequence* Sequence,
		const class UElysiumAnimLayerMask* Mask);
	// `elysium.LayerDump`'s one-shot readout: what the layer pose actually contains, per bone, so
	// what the applier reads can be checked against the container's own numbers instead of against
	// a screenshot.
	void DumpLayerPose(int32 Layer, const FPoseContext& Pose);
	// The composition half: split inheritance, then axis interpolation, in component space over
	// whatever the body produced. Order is load-bearing.
	void EvaluateComposition(FPoseContext& Output);

	TArray<FName> FacialCurves;
	TArray<float> FacialWeights;

	// A plain member rather than a graph: this instance has no AnimGraph, so the post-process slot
	// is the tail of Evaluate.
	UPROPERTY(Transient) FAnimNode_ElysiumAxisInterp AxisInterp;
	// The garment spike, evaluated after it so the simulation sees the finished skeleton.
	UPROPERTY(Transient) FAnimNode_ElysiumCloth Cloth;

	// One clip still fading out. Retail keeps these in a CUtlVector with NO cap and evicts purely on
	// the weight reaching zero, so concurrency is however many clip changes land inside a fade
	// window. Four players is one live clip plus three fading, which covers everything the retail
	// captures ever showed (the deepest observed was four concurrent sequences); a fifth change
	// inside one fade window evicts the oldest, which is the weakest contribution by construction.
	struct FFadingClip
	{
		int32 Slot = INDEX_NONE;
		float Elapsed = 0.f;
		float Duration = 0.f;
	};

	static constexpr int32 MaxPlayers = 4;

	// Autolayers get their OWN players rather than a share of the ones above. The array above is
	// the crossfade's: `TakeFreeSlot` evicts from it by age whenever a clip change lands inside a
	// fade window, and an autolayer has no business being evicted by a stance change — its lifetime
	// is the weapon's, not the transition's. Two, because retail's autolayer pattern is one masked
	// `<weapon>_aim_layer` plus one unmasked `<weapon>_<action>_delta` and never more; a third
	// request replaces the weakest, which is the smallest contribution by construction.
	static constexpr int32 MaxLayers = 2;

	// Standalone (not _Standalone-suffixed by accident): the plain-C++ variant of the sequence
	// player whose setters actually write, unlike the Blueprint-bound FAnimNode_SequencePlayer
	// whose SetSequence is a no-op outside a compiled anim graph.
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone Players[MaxPlayers];
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone LayerPlayers[MaxLayers];

	// The body's other possible source: one blend grid, standing in for the whole crossfade rather
	// than beside it. One, because a grid IS the stance — there is no second grid to blend toward,
	// and a stance change away from one goes back through the sequence players. Standalone for the
	// same reason the sequence players are: the Blueprint-bound variant's setters are no-ops outside
	// a compiled anim graph.
	UPROPERTY(Transient) FAnimNode_BlendSpacePlayer_Standalone GridPlayer;
	bool bGridNeedsReinit = false;

	// Retail's `layer_weight` per layer, 0 for a slot carrying nothing. Not a fade: an autolayer
	// has no authored transition, and a caller that wants one ramps this itself.
	float LayerWeights[MaxLayers] = {};
	bool bLayerNeedsReinit[MaxLayers] = {};
	// Which combine this slot's sequence asks for, latched at the request rather than re-derived
	// per frame: `IsValidAdditive()` walks the sequence's additive settings, and the answer cannot
	// change while the slot holds it.
	bool bLayerAdditive[MaxLayers] = {};
	// The bones an ordinary layer owns, as retail's per-bone `weight`@0 gate: 1 inside the mask, 0
	// outside, indexed by SKELETON bone index. Empty for a layer that owns the whole rig and for
	// every additive, where the mask is already expressed by the additive identity. Resolved on the
	// game thread at the request, off the sequence's own `UElysiumAnimLayerMask` — the worker has
	// no business loading an asset, and the mask cannot change under a running layer.
	TArray<float> LayerMasks[MaxLayers];

	// A slot no live clip is using, evicting the oldest fade when every slot is busy.
	int32 TakeFreeSlot();
	// The blend factor toward a fading clip's pose. Retail: f = 1 - elapsed/duration, then
	// SimpleSpline. Zero once expired, which is also the eviction test.
	static float FadeWeight(const FFadingClip& Fade);

	// Which player holds the clip that is playing now.
	int32 Current = 0;
	// Newest first, oldest last — the order retail blends them in, so the weakest pull lands last.
	TArray<FFadingClip, TInlineAllocator<MaxPlayers>> Fading;

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Playing = nullptr;
	// The playing clip's own authored fade, kept so the next transition can take the max of the
	// pair without the caller having to know what is currently up.
	float CurrentFade = 0.2f;
	bool bPlayingLoop = true;
	bool bInitialized = false;
	// A player whose sequence changed needs Initialize_AnyThread to reset its time accumulator —
	// SetSequence alone leaves it wherever the previous clip had run to. Flagged on the game
	// thread, consumed on the worker where the contexts are valid.
	bool bNeedsReinit[MaxPlayers] = {};
};

UCLASS(Transient)
class UElysiumNpcAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// The transition duration a clip gets when its own authored value is not known. VtMB stores one
	// per sequence in `mstudioseqdesc_t` at +0x264 and combines a pair as `max(outgoing, incoming)`;
	// across 5,836 shipped sequences 5,762 carry 0.2, with 0.3 on a handful of dialogue clips and
	// 0.5 on the lying-down and damaged stance idles. So this default IS the shipped value for
	// 98.7% of the vocabulary, and stays correct until the export carries the field per clip.
	static constexpr float DefaultBlendSeconds = 0.2f;

	// Play a clip, transitioning from whatever is current. Repeating an already-looping clip does
	// nothing; a repeated one-shot or a loop-mode change restarts it from frame zero. A FadeSeconds
	// of 0 is retail's `flags & 0x2` snap: no transition, and every clip still fading is dropped.
	void PlayClip(UAnimSequence* Sequence, bool bLoop = true, float BlendSeconds = DefaultBlendSeconds);
	void SeekClip(float PositionSeconds);
	void StopClip();

	// --- blend grids (ANM3) --------------------------------------------------------------------
	//
	// Stand the body on a baked blend grid — a `move_yaw` locomotion fan, an `aim_yaw`/`aim_pitch`
	// grid — instead of one clip. `SetGridPosition` then steers it in the pose parameters' own
	// degrees, and `PlayClip` takes the body back.
	//
	// False for a grid of partial-body `*_layer` overlays, which are a layer's and not a base pose.
	// Only the bake writes a `UBlendSpace` at all.
	bool PlayGrid(UBlendSpace* Space, bool bLoop = true);
	void SetGridPosition(float Axis0, float Axis1 = 0.f);
	void StopGrid();
	UBlendSpace* GetPlayingGrid() const { return Proxy.GetGrid(); }

	// --- autolayers ----------------------------------------------------------------------------
	//
	// Compose a VtMB autolayer over the body pose. Weight is retail's own layer scalar; re-asking
	// with a new weight re-weights the running layer rather than restarting it. Independent of
	// PlayClip in every way: a stance change underneath does not touch a layer, and a layer does
	// not transition.
	//
	// Either kind, decided from the sequence: a `_delta` accumulates as an additive over the whole
	// rig, a partial-body `*_layer` blends in under the bones it owns.
	//
	// False when the sequence is neither — a plain pose clip, or one built by glTFRuntime. Only the
	// `.eskm` bake writes a delta in the form Unreal hands back as a delta (`AdditiveAnimType`) and
	// only it carries the bone mask (`UElysiumAnimLayerMask`), so an unbaked layer is refused rather
	// than composed out of a pose that is really the reference pose for every bone it does not
	// touch.
	bool PlayLayer(UAnimSequence* Sequence, float Weight = 1.f, bool bLoop = true);
	void StopLayer(UAnimSequence* Sequence);
	void StopAllLayers();
	int32 GetActiveLayers() const;

	// Read and correct the phase of a clip that is still playing. This is the measurement half of
	// the substrate-clock phase lock: a caller compares the position against elapsed game time and
	// re-phases only when the two have parted company.
	float GetClipPosition() const;
	void ResyncClip(float PositionSeconds);

	UAnimSequence* GetPlayingClip() const { return Proxy.GetPlaying(); }

	// --- the facial flex track (roadmap 12.3) ------------------------------------------------
	//
	// Install the body's flex rig (null for a model with no facial sidecar, which is an ordinary
	// load — most animals, crowd bodies and every player body carry no flex data at all). Every
	// controller starts at rest, which puts every morph target at exactly zero weight.
	void SetFacialRig(TSharedPtr<const FElysiumFacialRig> InRig);
	const FElysiumFacialRig* GetFacialRig() const { return FacialRig.Get(); }

	// --- the two composition stages (roadmap CAP7.2) -----------------------------------------
	//
	// Install the body's composition rig — the `ProcType == 1` rule table. Null for a model that
	// declares none, which is an ordinary load: the body then poses under Unreal's own hierarchy
	// composition alone. The stage runs in the proxy's evaluate, after the graph has blended
	// locals and before skinning.
	//
	// There is no split-inheritance decision to make here any more. `UE_mdl_skeletal.py` resolves
	// `Flags & 0x2` into ordinary parent-relative rotations at export, so no clip that reaches
	// this instance carries VtMB's model-space value (repo-root `CLAUDE.md`, "Poses are baked
	// native").
	void SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	const FElysiumCompositionRig* GetCompositionRig() const { return CompositionRig.Get(); }
	// How many rules resolved against this body's actual skeleton — the number the debug surface
	// reports, and what distinguishes "no table" from "a table whose bones this skeleton lacks".
	int32 GetResolvedAxisInterpRules() const;

	// Install the garment spike's rig (`npc/cloth/<stem>.json`). Null for every model the spike did
	// not build, which is an ordinary load: the body then wears its faithful mesh and simulates
	// nothing. Independent of the composition rig above — a model may carry either, both or neither.
	void SetClothRig(TSharedPtr<const FElysiumClothRig> InRig);
	const FElysiumClothRig* GetClothRig() const { return ClothRig.Get(); }
	// The live edit sitting over that rig — what the green-room lab's sliders write, and what a
	// freshly installed rig resets to its own authored values. Same game-thread door as SetClothRig:
	// the proxy accessor blocks on any in-flight parallel evaluation first.
	void SetClothTuning(const FElysiumClothTuning& InTuning);
	FElysiumClothTuning GetClothTuning() const;
	// How many chains were built against this body's actual skeleton — what distinguishes "no rig"
	// from "a rig whose lattice bones this skeleton lacks", which is what wearing the faithful mesh
	// with the enhanced sidecar would look like.
	int32 GetResolvedClothChains() const;

protected:
	// The cloth chains' game-thread pass. Runs here rather than through the proxy's node
	// registration, which is gathered before any rig is installed (`FAnimNode_ElysiumCloth`).
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

public:

	// Flex controllers are the only writable facial state; the flexdesc weights and morph weights
	// below them are arithmetic, re-derived on every write. Names are the rig's own (`blink`,
	// `jaw_drop`, `right_lid_droop`), matched case-insensitively; false when this rig has no such
	// controller. 12.1's scene expression tracks and 12.5's lipsync both land here.
	bool SetFlexController(const FString& Name, float Value);
	bool SetFlexControllerByIndex(int32 Index, float Value);
	// A whole set of named writes, evaluated once rather than once per controller — an expression row
	// touches up to 44 of them at a time. Purely additive: a controller the set does not name keeps
	// its value. Returns the number that landed, INDEX_NONE when there is no rig here; names this rig
	// does not carry are appended to OutMissing rather than dropped.
	int32 SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing = nullptr);
	// Back to rest — every controller at zero, the jaw closed, every morph target off.
	void ResetFlexControllers();

	// --- the amplitude jaw (roadmap 12.5) ----------------------------------------------------
	//
	// The face's second input, and deliberately not a controller: `mstudiomouth_t` names a FLEXDESC,
	// so this write lands *downstream* of the rule layer that every controller feeds. 0 is a closed
	// mouth and 1 the line's own peak. False when this body carries no rig or no mouth record — the
	// majority of the cast — which is an ordinary no-op. Precedence against the rules, and why the
	// value alone moves nothing on the shipped models: `Visual/ElysiumFacialRig.h`.
	bool SetMouthOpen(float Open);
	float GetMouthOpen() const { return MouthOpen; }
	bool HasMouth() const;

	// --- the eyes (12.4) -----------------------------------------------------------------------
	//
	// The face's third input. Written once per frame by `UElysiumEntityBodies::TickEyes`, which is
	// the only thing that has both the settled bone transforms the aim is built from and the gaze
	// target it is aimed at. Everything below it is arithmetic replayed from the rig, so this
	// write is what re-evaluates the face — the same contract every controller write has.
	//
	// False when this body carries no rig, which is an ordinary no-op: a model with eyeballs and no
	// flex data still aims its irises, it just has no flexdesc for the lids to land on.
	bool SetEyeInput(const FElysiumEyeInput& Eyes);
	const FElysiumEyeInput& GetEyeInput() const { return EyeInput; }

	// Read-back for the debug surface: the normalized controller inputs, the flexdesc weights the
	// rules produced from them, and the ramped weight each morph target is driven at.
	const TArray<float>& GetFlexControllerValues() const { return ControllerValues; }
	const TArray<float>& GetFlexWeights() const { return FlexWeights; }
	const TArray<float>& GetMorphWeights() const { return MorphWeights; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy*) override {}

private:
	// Run the three layers and publish the result to the proxy. Called on every controller write
	// rather than every frame: nothing below a controller changes on its own.
	void EvaluateFacial();

	UPROPERTY(Transient) FElysiumNpcAnimProxy Proxy;

	TSharedPtr<const FElysiumFacialRig> FacialRig;
	TSharedPtr<const FElysiumCompositionRig> CompositionRig;
	TSharedPtr<const FElysiumClothRig> ClothRig;
	float MouthOpen = 0.f;
	FElysiumEyeInput EyeInput;
	TArray<float> ControllerValues;
	TArray<float> FlexWeights;
	TArray<float> MorphWeights;

	friend struct FElysiumNpcAnimProxy;
};
