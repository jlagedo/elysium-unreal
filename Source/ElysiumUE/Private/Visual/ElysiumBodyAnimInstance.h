#pragma once

#include "CoreMinimal.h"
// By value: the phase seam below answers one `FElysiumClipPhase` and is keyed by channel.
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntity.h"   // FElysiumFlexWrite (passed by view)
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Visual/ElysiumAnimNodes.h"
#include "Visual/ElysiumHairDynamicsData.h"
// By value: the eye input is a member, so the rig's own header rather than a forward declaration.
#include "Visual/ElysiumFacialRig.h"

#include "ElysiumBodyAnimInstance.generated.h"

class UAnimSequence;
struct FElysiumCompositionRig;
struct FElysiumBodyAnimProxy;

// A stable node for the engine's native custom-node registry. The simulated nodes themselves live
// in a replaceable array, so registering their addresses would make reconfiguration unsafe; this
// bridge stays put and forwards each reset to whichever nodes the proxy currently owns.
struct FElysiumHairDynamicsResetNode final : FAnimNode_Base
{
	void SetOwner(FElysiumBodyAnimProxy* InOwner) { Owner = InOwner; }
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;

private:
	FElysiumBodyAnimProxy* Owner = nullptr;
};

// Everything a VtMB body wears over whatever produced its pose — and nothing about how the pose
// was produced.
//
// The tail is VtMB's one composition stage, the garment simulation, and the face's morph-target
// curves, and it belongs to a body rather than to a pose source: a body that carried it on only
// some of its paths would ship frozen eyes and untwisted forearms, and neither the log nor the
// Content Browser preview can show that. It stays split from `UElysiumBipedAnimInstance` because
// the two answer different questions — this one is what a body wears, that one is where its pose
// comes from — and the facial and composition rigs are installed by callers that have no business
// knowing which graph is running.

USTRUCT()
struct FElysiumBodyAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FElysiumBodyAnimProxy() { HairDynamicsResetNode.SetOwner(this); }
	explicit FElysiumBodyAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
	{
		HairDynamicsResetNode.SetOwner(this);
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	// Walks a compiled graph's nodes if there is one, then resolves the tail's bone references —
	// both stages resolve their indices here, once, and never by name per evaluation. The base call
	// is free on the native path: it is guarded on `RootNode`, which is null when there is no graph.
	// A derived proxy that owns nodes of its own caches them here too, through `Super::CacheBones`.
	virtual void CacheBones() override;

	// VtMB's one composition stage; null clears it.
	void SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	int32 NumAxisInterpRules() const { return AxisInterp.NumResolvedRules(); }
	void SetHairDynamics(const TArray<FElysiumHairDynamicsChainConfig>& InChains,
		const TArray<FElysiumHairDynamicsBodyConfig>& InBodies,
		const FReferenceSkeleton& ReferenceSkeleton);
	int32 NumHairDynamicsChains() const { return InstalledChainCount; }
	int32 NumHairDynamicsBodies() const { return InstalledBodyCount; }
#if WITH_DEV_AUTOMATION_TESTS
	void ClearHairDynamicResetRequestsForTest();
	bool HairDynamicsRequestedResetForTest(ETeleportType TeleportType) const;
#endif

	// The facial morph track: the rig's evaluated morph weights, published from the game
	// thread and emitted as morph-target anim curves over whatever pose the body produced. Two
	// parallel arrays in the rig's own morph order — a name list that changes only when the body's
	// model does, and weights that change whenever a flex controller is written.
	void SetFacialTrack(TArray<FName>&& InCurves);
	void SetFacialWeights(TArrayView<const float> InWeights);

protected:
	// The tail, over whatever produced the pose. Order is load-bearing: the composition stages
	// first, then the face's curves over whatever the body ended up in.
	void EvaluateTail(FPoseContext& Output);

private:
	// Split inheritance, then axis interpolation, in component space over whatever the body
	// produced. Order is load-bearing — see the definition.
	void EvaluateComposition(FPoseContext& Output);
	void ResetHairDynamics(ETeleportType TeleportType);
	friend struct FElysiumHairDynamicsResetNode;

	TArray<FName> FacialCurves;
	TArray<float> FacialWeights;

	// Plain members rather than graph nodes: the post-process slot is the tail of Evaluate, so both
	// are driven through `ResolveBones`/`CacheBones` + `Apply` rather than through pose links.
	UPROPERTY(Transient) FAnimNode_ElysiumAxisInterp AxisInterp;
	UPROPERTY(Transient) TArray<FAnimNode_ElysiumHairDynamics> HairDynamics;
	FElysiumHairDynamicsResetNode HairDynamicsResetNode;
	int32 InstalledChainCount = 0;
	int32 InstalledBodyCount = 0;
	bool bHairNeedsInitialize = false;

};

UCLASS(Transient, Abstract)
class UElysiumBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// The transition duration a clip gets when its own authored value is not known. VtMB stores one
	// per sequence in `mstudioseqdesc_t` at +0x264 and combines a pair as `max(outgoing, incoming)`;
	// across 5,836 shipped sequences 5,762 carry 0.2, with 0.3 on a handful of dialogue clips and
	// 0.5 on the lying-down and damaged stance idles. So this default IS the shipped value for
	// 98.7% of the vocabulary.
	static constexpr float DefaultBlendSeconds = 0.2f;

	// The one-shot seam.
	//
	// Play one clip over whatever owns the base pose. A graph-backed body plays it on a montage slot
	// and a body with no compiled graph on its own clip player, and a caller holding an
	// `IElysiumEmbodiment` body has no way to know which it has, so the question is answered here
	// rather than at every call site.
	//
	// False when this instance cannot play the clip at all, which is an ordinary answer: a body
	// whose visual has no vocabulary is not an error.
	//
	// The two fades are stated apart because retail states them apart — the flinch fades in over 0.1
	// and out over 0.3 — and because `PlaySlotAnimationAsDynamicMontage` takes them separately.
	//
	// **`Identity` is what the event dispatcher walks, and only the caller knows it.** The
	// sequence is named after the animation the bake wrote; the timeline is keyed by the vocabulary
	// label the caller resolved it through. An empty identity is legal and means the clip carries no
	// timeline to walk — see `FElysiumClipIdentity`.
	//
	// `bRestart` carries the recovered restart rule (`Visual/ElysiumActionTables.h` →
	// `RouteRestartsIdenticalRequest`): retail's `RestartIdealActivity` clears the current activity
	// before setting the ideal, so a request equal to what is already playing re-fires instead of
	// being swallowed. It defaults to **false** because that is what the ordinary ideal route does,
	// and a producer that cannot name its route must not assume a restart — the held-crouch trace in
	// `docs/vtmb/animation_and_movers.md` is the proof of the complementary rule. A host whose only
	// route already restarts (the dynamic slot montage) ignores it.
	//
	// `PlayRate` is `m_flPlaybackRate`, and it defaults to **1.0** for the same reason `bRestart`
	// defaults to false: retail's `ResetSequenceInfo` resets the rate before anything writes it, so a
	// producer that names no rate means the authored speed. It is one number for the whole play — the
	// drawn speed, the cycle advance and anything derived from either.
	virtual bool PlayOneShot(const FElysiumClipIdentity& Identity, UAnimSequence* Sequence,
		bool bLoop, float BlendInSeconds, float BlendOutSeconds, bool bRestart = false,
		float PlayRate = 1.0f)
	{
		return false;
	}
	virtual void StopOneShot(float BlendSeconds) {}

	// The phase seam.
	//
	// Where one channel of this body stands on its clip, as the normalized cycle VtMB's own event
	// dispatcher compares (`docs/vtmb/animation_and_movers.md` → "Sequence events and native
	// dispatch"). Only the host knows: a montage, a graph state and a blended fan each carry their
	// own position, and none of them is readable from outside.
	//
	// False here is the ordinary answer for a host with no phase clock — this base has none, and a
	// body playing nothing on that channel has no phase either. `Out` is cleared either way, so a
	// negative answer can never be read as a stale record.
	virtual bool GetClipPhase(EElysiumAnimChannel Channel, FElysiumClipPhase& Out) const
	{
		Out = FElysiumClipPhase();
		return false;
	}

	// The facial flex track.
	//
	// Install the body's flex rig (null for a model with no facial sidecar, which is an ordinary
	// load — most animals, crowd bodies and every player body carry no flex data at all). Every
	// controller starts at rest, which puts every morph target at exactly zero weight.
	void SetFacialRig(TSharedPtr<const FElysiumFacialRig> InRig);
	const FElysiumFacialRig* GetFacialRig() const { return FacialRig.Get(); }

	// Flex controllers are the only writable facial state; the flexdesc weights and morph weights
	// below them are arithmetic, re-derived on every write. Names are the rig's own (`blink`,
	// `jaw_drop`, `right_lid_droop`), matched case-insensitively; false when this rig has no such
	// controller. Scene expression tracks and lipsync both land here.
	bool SetFlexController(const FString& Name, float Value);
	bool SetFlexControllerByIndex(int32 Index, float Value);
	// A whole set of named writes, evaluated once rather than once per controller — an expression row
	// touches up to 44 of them at a time. Purely additive: a controller the set does not name keeps
	// its value. Returns the number that landed, INDEX_NONE when there is no rig here; names this rig
	// does not carry are appended to OutMissing rather than dropped.
	int32 SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing = nullptr);
	// Back to rest — every controller at zero, the jaw closed, every morph target off.
	void ResetFlexControllers();

	// The amplitude jaw.
	//
	// The face's second input, and deliberately not a controller: `mstudiomouth_t` names a FLEXDESC,
	// so this write lands *downstream* of the rule layer that every controller feeds. 0 is a closed
	// mouth and 1 the line's own peak. False when this body carries no rig or no mouth record — the
	// majority of the cast — which is an ordinary no-op. Precedence against the rules, and why the
	// value alone moves nothing on the shipped models: `Visual/ElysiumFacialRig.h`.
	bool SetMouthOpen(float Open);
	float GetMouthOpen() const { return MouthOpen; }
	bool HasMouth() const;

	// The eyes.
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

	// The two composition stages.
	//
	// Install the body's composition rig — the `ProcType == 1` rule table. Null for a model that
	// declares none, which is an ordinary load: the body then poses under Unreal's own hierarchy
	// composition alone. The stage runs in the proxy's evaluate, after the pose has been produced
	// and before skinning.
	void SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	const FElysiumCompositionRig* GetCompositionRig() const { return CompositionRig.Get(); }
	// How many rules resolved against this body's actual skeleton — the number the debug surface
	// reports, and what distinguishes "no table" from "a table whose bones this skeleton lacks".
	int32 GetResolvedAxisInterpRules() const;

	// Install the generated mesh's stock-AnimDynamics recipes. Empty chains are the ordinary
	// answer outside the two-character hair proof; empty bodies are the ordinary answer on a
	// model with no one-bone breast row.
	void SetHairDynamics(const TArray<FElysiumHairDynamicsChainConfig>& InChains,
		const TArray<FElysiumHairDynamicsBodyConfig>& InBodies,
		const FReferenceSkeleton& ReferenceSkeleton);
	int32 GetHairDynamicsChainCount() const;
	int32 GetHairDynamicsBodyCount() const;

	// Read-back for the debug surface: the normalized controller inputs, the flexdesc weights the
	// rules produced from them, and the ramped weight each morph target is driven at.
	const TArray<float>& GetFlexControllerValues() const { return ControllerValues; }
	const TArray<float>& GetFlexWeights() const { return FlexWeights; }
	const TArray<float>& GetMorphWeights() const { return MorphWeights; }

private:
	// Run the three layers and publish the result to the proxy. Called on every controller write
	// rather than every frame: nothing below a controller changes on its own.
	void EvaluateFacial();

	TSharedPtr<const FElysiumFacialRig> FacialRig;
	TSharedPtr<const FElysiumCompositionRig> CompositionRig;
	float MouthOpen = 0.f;
	FElysiumEyeInput EyeInput;
	TArray<float> ControllerValues;
	TArray<float> FlexWeights;
	TArray<float> MorphWeights;
};
