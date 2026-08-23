#include "Visual/ElysiumBipedAnimInstance.h"

#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation

#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimNodes/AnimNode_BlendListByBool.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimationRuntime.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "BonePose.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumBipedGraph, Log, All);

// ================================================================================================
// FElysiumBipedAnimProxy
// ================================================================================================

void FElysiumBipedAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FElysiumBodyAnimProxy::Initialize(InAnimInstance);

	// Every player this proxy owns itself loops by default and is driven explicitly; no sync group,
	// because two clips of different lengths must not be forced onto a shared normalized time — and a
	// layer must never be phase-matched to the pose it rides over, which is what an aim layer and a
	// walk cycle have nothing to say to each other about.
	FAnimationInitializeContext Context(this);
	ClipPlayer.SetLoopAnimation(true);
	ClipPlayer.SetPlayRate(1.f);
	ClipPlayer.SetGroupName(NAME_None);
	ClipPlayer.SetGroupMethod(EAnimSyncMethod::DoNotSync);
	ClipPlayer.Initialize_AnyThread(Context);
	Playing = nullptr;
	bPlayingLoop = true;
	bClipNeedsReinit = false;
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumBipedAnimProxy::CacheBones()
{
	// The compiled graph, the composition stages and the garment first, through the shared base.
	FElysiumBodyAnimProxy::CacheBones();

	FAnimationCacheBonesContext Context(this);
	ClipPlayer.CacheBones_AnyThread(Context);
}

bool FElysiumBipedAnimProxy::Evaluate(FPoseContext& Output)
{
	// A standing cinematic clip IS the body pose, replacing the graph rather than blending with it —
	// a scene owns the body outright for its duration. The graph still advanced this frame, so
	// whatever it holds resumes the moment the clip is stopped.
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Evaluate_AnyThread(Output);
	}
	else
	{
		// The compiled graph. `FAnimInstanceProxy::Evaluate` returns false to mean "not handled, run
		// the graph", so calling it here would evaluate nothing and pose the reference pose.
		EvaluateAnimationNode(Output);
	}
	// VtMB's autolayers used to be accumulated here, between the body pose and the composition
	// stages. `CCC10` moved them into the compiled graph, where the mask is a property of the blend
	// node rather than of the pose feeding it — so they now arrive inside `EvaluateAnimationNode`
	// above, still under the composition tail, which is retail's own order either way.
	//
	// Then the composition stages and the face, over whatever produced the pose — the same tail every
	// body wears, which is what stops a body shipping frozen eyes and untwisted forearms.
	EvaluateTail(Output);
	return true;
}

void FElysiumBipedAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (bClipNeedsReinit)
	{
		bClipNeedsReinit = false;
		FAnimationInitializeContext InitContext(this);
		ClipPlayer.Initialize_AnyThread(InitContext);
		FAnimationCacheBonesContext BoneContext(this);
		ClipPlayer.CacheBones_AnyThread(BoneContext);
	}
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Update_AnyThread(InContext);
	}

	// The graph advances whether or not its pose is consumed. A scene that ends hands the body back
	// to a stack that has kept up with the world rather than to one frozen where the scene began.
	FElysiumBodyAnimProxy::UpdateAnimationNode(InContext);
}

// ================================================================================================
// The cinematic clip path
// ================================================================================================

bool FElysiumBipedAnimProxy::PlayDirect(UAnimSequence* Sequence, bool bLoop, bool bRestart,
	float PlayRate)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// The recovered restart rule: `RestartIdealActivity` clears the current activity before setting
	// the ideal, so a request on that route is never swallowed as an unchanged one. Every other route
	// keeps the early-out below, which is the complementary rule the held-crouch trace proves.
	if (Sequence == Playing && bLoop && bPlayingLoop && !bRestart)
	{
		// A repeated looping request must not visibly reset the clip. A body a prior Seek pinned at
		// rate 0 must not stay frozen forever because of it, though: SetPlayRate writes the node's
		// own member, read on every UpdateAssetPlayer, so this un-freezes without the restart the
		// early-out exists to prevent — no reinit, and the play position is preserved.
		ClipPlayer.SetPlayRate(PlayRate);
		return false;
	}
	ClipPlayer.SetSequence(Sequence);
	ClipPlayer.SetLoopAnimation(bLoop);
	// `m_flPlaybackRate`. The producer's, or the authored 1.0 nobody wrote over.
	ClipPlayer.SetPlayRate(PlayRate);
	ClipPlayer.SetStartPosition(0.f);
	bClipNeedsReinit = true;   // reset the play time on the worker
	Playing = Sequence;
	bPlayingLoop = bLoop;
	return true;
}

void FElysiumBipedAnimProxy::Seek(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	ClipPlayer.SetStartPosition(Position);
	ClipPlayer.SetPlayRate(0.f);
	bClipNeedsReinit = true;
}

float FElysiumBipedAnimProxy::GetClipPosition() const
{
	if (Playing == nullptr)
	{
		return -1.f;
	}
	return ClipPlayer.GetAccumulatedTime();
}

void FElysiumBipedAnimProxy::ResyncPosition(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	// A pending restart wins. The worker consumes bClipNeedsReinit in UpdateAnimationNode and
	// Initialize_AnyThread then sets the accumulator to the node's start position, so a resync
	// written in the same frame as a PlayDirect would be silently discarded. Bailing makes that
	// deterministic instead of dependent on which ran first; the next resync corrects it.
	if (bClipNeedsReinit)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	// SetAccumulatedTime writes the node's play time and nothing else — no play rate, no start
	// position, no reinit. That is the whole reason this is not Seek: the clip keeps running from
	// its corrected phase rather than freezing at it.
	//
	// This is a game-thread write to a field the worker advances. It is safe only because every
	// caller arrives through UElysiumBipedAnimInstance's GetProxyOnGameThread, which blocks on any
	// in-flight parallel evaluation. A future path reaching the proxy without that accessor turns
	// this into a silent data race on a float.
	ClipPlayer.SetAccumulatedTime(Position);
}

void FElysiumBipedAnimProxy::StopDirect()
{
	Playing = nullptr;
	ClipPlayer.SetSequence(nullptr);
}


// ================================================================================================
// UElysiumBipedAnimInstance
// ================================================================================================

FAnimNode_BlendStack* UElysiumBipedAnimInstance::FindLocomotionStack()
{
	// The compiled class's tag table, the same door `ApplyUpperBodyMask` and
	// `HasCompiledReactionBranch` use. Absent on the plain native class — the designed host for a
	// skeletal prop, which `ElysiumEntityBodies.cpp` installs deliberately — so a null table is a
	// state rather than an error.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	FAnimNode_BlendStack* Stack = Tags != nullptr
		? Tags->FindNodeByTag<FAnimNode_BlendStack>(
			FName(ElysiumAnimGraph::LocomotionStackTag), this)
		: nullptr;

	if (!bLocomotionStackResolved)
	{
		bLocomotionStackResolved = true;
		bHasLocomotionStack = Stack != nullptr;
		// A generated class that should carry the stack and does not IS a defect — a renamed tag or
		// a stale package — and it costs the body its whole base channel. Reported once per
		// instance: the lookup above still runs every update, and this must not become that.
		if (Stack == nullptr && HasCompiledGraph())
		{
			UE_LOG(LogElysiumBipedGraph, Warning,
				TEXT("[elysium] compiled graph '%s' carries no blend stack tagged '%s'; the base ")
				TEXT("channel poses nothing and publishes no phase"),
				*GetClass()->GetName(), ElysiumAnimGraph::LocomotionStackTag);
		}
	}
	return Stack;
}

bool UElysiumBipedAnimInstance::HasCompiledLocomotionStack() const
{
	if (!bLocomotionStackResolved)
	{
		const_cast<UElysiumBipedAnimInstance*>(this)->FindLocomotionStack();
	}
	return bHasLocomotionStack;
}

void UElysiumBipedAnimInstance::PublishSelection(const FElysiumAnimationSelection& Selection,
	const FElysiumResolvedAnimation& Assets)
{
	Pending = Selection;
	PendingBlendSpace = Assets.Space;
	PendingSequence = Assets.Sequence;
	// BuildNpcVisual (and every other clip stand) arms a looping one-shot on DefaultSlot so a
	// freshly stood body is not the bind pose. That slot sits ON TOP of the blend stack, so a walk
	// fan published underneath never reaches the frame — the body keeps playing idle while it
	// moves, and a publish that owns the base has to take the slot back.
	//
	// **Who owns the base is the record's arbitration verdict, decided nowhere else.** The driver
	// ranked its own publish against the base channel's standing claim on the priority table
	// (`docs/architecture/animation-architecture.md` §3.3 step 1) and wrote the answer; this obeys
	// it. A publish whose record yielded — an ambient stance holding against a standing body, a
	// scene holding against a travelling one — leaves the clip alone however the body moves, and a
	// publish that won ends it. A hand-built record defaults to owning the base, because a debug or
	// test stand IS a deliberate claim on the pose it publishes.
	if (Selection.bBasePoseOwned && (PendingSequence != nullptr || PendingBlendSpace != nullptr))
	{
		// Only when it actually takes a clip away. The condition above is true on every frame a
		// selection stays resolved, but StopOneShot nulls the montage, so a live one here is one
		// real pre-emption -- one line per clip killed, not per frame.
		if (ActiveSlotMontage != nullptr)
		{
			UE_LOG(LogElysiumBipedGraph, Verbose,
				TEXT("publish preempts one-shot '%s' (zero blend) for %s seq='%s' space='%s'"),
				*GetNameSafe(ActiveSlotMontage), ElysiumAnimGraph::StateName(Selection.GraphState),
				*GetNameSafe(PendingSequence), *GetNameSafe(PendingBlendSpace));
		}
		StopOneShot(0.f);
		StopClip();
		// LIFE5 — and the reaction branch with them. A publish that has WON the base pose owns the
		// base pose; leaving the branch holding a fan would leave the body reacting to a hit the
		// arbitration has already handed away, with the reaction's own assets pinned behind it.
		StopReaction();
	}
	// **An autolayer belongs to the host that owns the base pose, and to no other.** The binding is
	// declared per host sequence (`autolayers[label]`), so the layers that compose are the ones the
	// sequence ON SCREEN declares. A publish that yielded the base is not that sequence: its host is
	// not being posed, and installing its overlay anyway drives the bones the layer owns from a clip
	// nothing is playing. That is a masked overlay at full weight fighting the claim that won —
	// `katana_relaxed_move_layer` owns the weapon arm, so a swing claimed over a relaxed gait keeps
	// the carry pose in the hand while the rest of the body swings, and the same holds for every
	// `<weapon>_relaxed_move_layer` and `<weapon>_bobble_layer` over every claimed clip.
	//
	// Retail cannot reach the state at all: the attack REPLACES the base sequence, so the host is
	// the attack, and no `ACT_MELEE_ATTACK_<FAMILY>` clip declares an autolayer. The claim's own
	// layers ride its own publish; a yielded one contributes none.
	const bool bLayersOwned = Selection.bBasePoseOwned;
	PendingUpperBodySpace = bLayersOwned ? Assets.OverlaySpace : nullptr;
	PendingUpperBodySequence = bLayersOwned ? Assets.OverlaySequence : nullptr;
	PendingUpperBodyMaskName = bLayersOwned ? Assets.OverlayMaskName : NAME_None;
	PendingAdditiveSequence = bLayersOwned ? Assets.AdditiveSequence : nullptr;
	// 1.0 is retail's own per-layer weight: the autolayer dispatcher pushes a literal 1.0f and the
	// accumulator multiplies it by a per-bone mask that is binary. A manual driver overrides it
	// afterward through SetUpperBodyLayerWeight/SetAdditiveLayerWeight, which is why this only
	// resets the default on a NEW publish rather than every frame.
	PendingUpperBodyLayerWeight = (PendingUpperBodySpace != nullptr
		|| PendingUpperBodySequence != nullptr) ? 1.0f : 0.0f;
	PendingAdditiveLayerWeight = PendingAdditiveSequence != nullptr ? 1.0f : 0.0f;

	// --- the overlay slot, staged off the SAME record and gated by nothing -------------------------
	//
	// **`bBasePoseOwned` is deliberately not consulted here, and the asymmetry with the autolayers
	// above is the whole point.** An autolayer belongs to the host sequence that owns the base pose,
	// so a publish that yielded contributes none. Retail's `CBaseAnimatingOverlay` slot 0 belongs to
	// nothing of the kind: it is accumulated over whatever owns the base at the time, it survives the
	// base changing hands underneath it, and it dies with its own clip. Gating it would silence every
	// shot fired while a scene, a reaction or an ambient stance held the base.
	//
	// The weight is the record's ENVELOPED number — `FElysiumAnimationDriver::ArbitrateSlot` computed
	// it with `ElysiumAnimIntent::SlotWeightAt` over the claim's own age — never the `m_flWeightMax`
	// ceiling `PendingUpperBodyLayerWeight` above stands in with. A ceiling written here would compose
	// a reload's first frame at full strength and lose the ramp retail measures.
	//
	// Every field is cleared together when the record names no layer: `HasSlotLayer()` is a pointer
	// test, and a weight or a cycle left behind by a claim that has gone would describe a layer the
	// record has already stopped naming.
	PendingSlotSequence = Assets.SlotSequence;
	// The layers the slot clip itself declares, riding OVER the motion — its aim grid, that grid's
	// own mask, and its `_delta`. All three travel with the sequence and go down with it.
	PendingSlotSpace = Assets.SlotSequence != nullptr ? Assets.SlotSpace : nullptr;
	PendingSlotAimMaskName = Assets.SlotSequence != nullptr ? Assets.SlotAimMaskName : NAME_None;
	PendingSlotAdditive = Assets.SlotSequence != nullptr ? Assets.SlotAdditive : nullptr;
	PendingSlotMaskName = Assets.SlotSequence != nullptr ? Assets.SlotMaskName : NAME_None;
	PendingSlotWeight = Assets.SlotSequence != nullptr ? Selection.SlotWeight : 0.0f;
	PendingSlotCycle = Assets.SlotSequence != nullptr ? Selection.SlotCycle : 0.0f;

	// The layer's PHASE moves with the same record, off the record's own cycle rather than a second
	// clock: the claim's hold is what expires the layer, and a timeline walked against any other
	// number would fire the shot's commit id at a different instant from the one the pose reaches it.
	// It is read off `Selection` rather than `PendingSlotCycle` above so a frame whose asset has not
	// been resolved yet still advances the timeline of the clip the claim named.
	RefreshSlotPhase(Selection, Selection.SlotCycle);
}

// One line per (reason, bank|label, asset) for a refusal the arm seam can reach at autofire rates.
// The identity is the key rather than the claim's label alone because a bank's own spelling is what
// distinguishes two clips that share a vocabulary key, and the asset name is carried so a re-bake
// that changes which sequence answers is reported again rather than swallowed by the latch. The
// claim's label backs the key up for a hand-built stand that names no identity at all.
bool UElysiumBipedAnimInstance::ShouldReportSlotArmRefusalOnce(const TCHAR* Reason,
	const FElysiumClipIdentity& Identity, const FElysiumAnimationRequest& Claim,
	const UAnimSequence* Sequence)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%s|%s"), Reason, *Identity.OwnerStem,
		*Identity.Label, *Claim.Label, *GetNameSafe(Sequence));
	bool bAlready = false;
	ReportedSlotArmRefusals.Add(Key, &bAlready);
	return !bAlready;
}

bool UElysiumBipedAnimInstance::PlaySlotLayer(const FElysiumClipIdentity& Identity,
	UAnimSequence* Sequence, FName MaskName, const FElysiumAnimationRequest& Claim,
	UBlendSpace* AimSpace, FName AimMaskName, UAnimSequence* Additive)
{
	// **Every refusal below is latched on its own (reason, bank|label), because this seam is reached
	// once per TRIGGER PULL.** A fire layer the bake left unmasked is refused on every shot, and an
	// autofire weapon reaches that at its own rate — an unguarded line would restate one bake fault
	// dozens of times a second and bury everything else in the log. The fault does not change between
	// two plays of the same clip, so the first line carries the whole of it.
	if (Sequence == nullptr)
	{
		if (ShouldReportSlotArmRefusalOnce(TEXT("nosequence"), Identity, Claim, nullptr))
		{
			UE_LOG(LogElysiumBipedGraph, Warning,
				TEXT("[elysium] the overlay slot was armed for '%s' with no sequence, so nothing "
					 "composes"),
				*Claim.Label);
		}
		return false;
	}
	// A layer with no baked bone mask owns the WHOLE rig, which is never what a partial-body overlay
	// means: composed unmasked it would drag every bone the layer does not animate toward the layer's
	// own pose and lose the body's stance from the waist down. Refused rather than composed, the same
	// gate the layer lab and `ApplyUpperBodyMask` both apply.
	if (MaskName.IsNone())
	{
		if (ShouldReportSlotArmRefusalOnce(TEXT("nomask"), Identity, Claim, Sequence))
		{
			UE_LOG(LogElysiumBipedGraph, Warning,
				TEXT("[elysium] the overlay slot refuses '%s' ('%s'): it carries no baked bone mask, so "
					 "it would own the whole rig rather than composing over the base pose"),
				*Claim.Label, *GetNameSafe(Sequence));
		}
		return false;
	}

	// A claim that states no clip length states no phase either, and a layer with no phase is one
	// still frame of its clip held for as long as the claim stands. Refused rather than composed: the
	// length is the clip's own authored seconds over the producer's rate, so a claim without one names
	// a zero-length asset or was built by hand without the conversion `ClaimForSegment` performs.
	if (!(ElysiumAnimIntent::SlotPhaseLength(Claim) > 0.0f))
	{
		if (ShouldReportSlotArmRefusalOnce(TEXT("nolength"), Identity, Claim, Sequence))
		{
			UE_LOG(LogElysiumBipedGraph, Warning,
				TEXT("[elysium] the overlay slot refuses '%s' ('%s'): its claim carries no clip length, "
					 "so the layer has no phase to ride and would stand one frame at a fixed weight"),
				*Claim.Label, *GetNameSafe(Sequence));
		}
		return false;
	}

	PendingSlotSequence = Sequence;
	// The clip's own declared layers, resolved by the CALLER through the same subsystem helper the
	// driver's publish uses — so there is no frame where the motion stands without them, and no
	// frame where a previous clip's grid outlives its own sequence.
	PendingSlotSpace = AimSpace;
	PendingSlotAimMaskName = AimSpace != nullptr ? AimMaskName : NAME_None;
	PendingSlotAdditive = Additive;
	PendingSlotMaskName = MaskName;
	// **The envelope is `ElysiumAnimIntent`'s and is never re-derived here.** The arm frame is age
	// zero, which is full weight for an attack layer's snap and zero for the foot of a reload's ramp
	// — two answers one shared helper already gives. A literal 1.0 here would be the ceiling wearing
	// the envelope's name, and the driver's own publish next frame would disagree with it.
	PendingSlotWeight = ElysiumAnimIntent::SlotWeightAt(Claim, 0.0f);
	PendingSlotCycle = ElysiumAnimIntent::SlotCycle(Claim, 0.0f);

	// **The phase is armed HERE, not on the next update**, for the reason `ArmBasePhase` is: the
	// ranged transactions resolve a clip and immediately ask whether a polled channel is standing on
	// it, in the same statement pair. A layer whose phase appeared a frame later would answer for the
	// previous play, and the shot's commit would fall back to the `ContactEventCycle` estimate with
	// nothing but a Verbose line saying so.
	//
	// The LENGTH is the sequence's own authored seconds and the RATE is what the claim is riding it
	// at — `authored / SlotPhaseLength` is the claim's own playback rate read back out of the one
	// duration the envelope, the expiry and this phase all share, so `PlayRate / Length` is the
	// cycles-per-second a consumer sampling a window forward from the cycle needs.
	const float AuthoredSeconds = Sequence->GetPlayLength();
	const float PhaseSeconds = ElysiumAnimIntent::SlotPhaseLength(Claim);
	ArmSlotPhase(Identity, AuthoredSeconds, Claim.bLoop,
		PhaseSeconds > 0.0f ? AuthoredSeconds / PhaseSeconds : 1.0f, PendingSlotCycle);
	return true;
}

void UElysiumBipedAnimInstance::StopSlotLayer()
{
	PendingSlotSequence = nullptr;
	PendingSlotSpace = nullptr;
	PendingSlotAdditive = nullptr;
	PendingSlotAimMaskName = NAME_None;
	PendingSlotMaskName = NAME_None;
	PendingSlotWeight = 0.0f;
	PendingSlotCycle = 0.0f;
	// **The PROJECTED pins go down with the staging, or the call does not do what it says.** Staging
	// alone is only read by the next `NativeUpdateAnimation`, and the bodies this is called on are
	// exactly the ones that may not get another: a corpse frozen at its final pose, a body whose
	// driver was released wholesale. The pins are what the graph evaluates, so a weight left standing
	// is a shot still composing on a body whose producer ended it.
	RequestedSlotSequence = nullptr;
	RequestedSlotBlendSpace = nullptr;
	RequestedSlotAdditive = nullptr;
	SlotAimLayerWeight = 0.0f;
	SlotAdditiveWeight = 0.0f;
	RequestedSlotMaskName = NAME_None;
	SlotLayerWeight = 0.0f;
	SlotExplicitTime = 0.0f;
	SlotNormalizedTime = 0.0f;
	PosedSlotSequence = nullptr;
	// The published timeline goes with the pose. A phase left standing names a clip nothing composes,
	// and the event pass would keep walking its records against a frozen cycle.
	SlotArm = FElysiumArmedClip();
	PublishSlotPhase(0.0f);
	// The blend node's own mask is deliberately NOT written here. At weight zero it gates nothing,
	// and this runs from gameplay code rather than from the update pass, where a node write can race
	// the worker the last dispatch handed the graph to. The next projection gives it back.
}

void UElysiumBipedAnimInstance::StopSlotLayerOn(USkeletalMeshComponent* Body)
{
	if (UElysiumBipedAnimInstance* Graph = Body != nullptr
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr)
	{
		Graph->StopSlotLayer();
	}
}

void UElysiumBipedAnimInstance::ProjectUpperBodyLayer()
{
	RequestedUpperBodyBlendSpace = PendingUpperBodySpace;
	RequestedUpperBodySequence = PendingUpperBodySequence;
	RequestedUpperBodyMaskName = PendingUpperBodyMaskName;
	RequestedAdditiveSequence = PendingAdditiveSequence;
	AimYaw = Pending.AimYaw;
	AimPitch = Pending.AimPitch;
	UpperBodyLayerWeight = PendingUpperBodyLayerWeight;
	AdditiveLayerWeight = PendingAdditiveLayerWeight;

	// The layer lab's hand driver wins over the published record, and it has to: the player's own
	// driver republishes a selection every frame, so an override that merely wrote the pending record
	// would be overwritten before it was ever evaluated. This is what lets the owner judge a layer
	// over a MOVING host, which is the whole reason the lab survives this rung.
	if (bDebugUpperBody)
	{
		RequestedUpperBodyBlendSpace = DebugOverlaySpace;
		RequestedUpperBodySequence = DebugOverlaySequence;
		RequestedUpperBodyMaskName = DebugOverlayMaskName;
		RequestedAdditiveSequence = DebugAdditiveSequence;
		AimYaw = DebugAimYaw;
		AimPitch = DebugAimPitch;
		UpperBodyLayerWeight = (DebugOverlaySpace != nullptr || DebugOverlaySequence != nullptr)
			? DebugLayerWeight : 0.0f;
		AdditiveLayerWeight = DebugAdditiveSequence != nullptr ? DebugLayerWeight : 0.0f;
	}

	bUpperBodyHasBlendSpace = RequestedUpperBodyBlendSpace != nullptr;
	// The one pin-less write, and the only thing here that touches a node rather than a property.
	ApplyUpperBodyMask();
}

void UElysiumBipedAnimInstance::ProjectSlotLayer()
{
	// **The playhead is re-seated whenever the ASSET moves, and the cycle cannot answer that on its
	// own.** A re-fire of the same clip restarts through the claim — a new claim starts at age zero,
	// so its cycle is zero and the evaluator seats at the head, which is retail's own restart. What
	// the cycle cannot state is a publish that swaps in a DIFFERENT layer while still carrying the
	// previous claim's phase: seating a fresh clip a third of the way through its motion is a visible
	// jump into the middle of a reload.
	const bool bSequenceChanged = PendingSlotSequence != PosedSlotSequence;

	RequestedSlotSequence = PendingSlotSequence;
	RequestedSlotBlendSpace = PendingSlotSpace;
	RequestedSlotMaskName = PendingSlotMaskName;

	// A layer the record named but that carries no baked bone mask is refused rather than composed:
	// unmasked it would drag every bone it does not animate toward its own pose and lose the body's
	// stance from the waist down. The resolver already warns when it loads one, so this says so once
	// per instance rather than per frame — and takes the pose down, which the warning alone would not.
	if (RequestedSlotSequence != nullptr && RequestedSlotMaskName.IsNone())
	{
		if (!bReportedMasklessSlot)
		{
			bReportedMasklessSlot = true;
			UE_LOG(LogElysiumBipedGraph, Warning,
				TEXT("[elysium] the overlay slot refuses '%s' on %s: no baked bone mask, so composing it "
					 "would own the whole rig rather than the bones the layer animates"),
				*GetNameSafe(RequestedSlotSequence), *GetNameSafe(GetSkelMeshComponent()));
		}
		RequestedSlotSequence = nullptr;
		RequestedSlotBlendSpace = nullptr;
	}

	// The clip's declared layers stand only where the motion does — one clip, one trio, one fate.
	// Within the slot they ride at retail's hardcoded autolayer 1.0; the slot's own envelope is
	// applied once by the OUTER blend's weight, so a weight here would double-apply it.
	RequestedSlotAdditive = RequestedSlotSequence != nullptr ? PendingSlotAdditive.Get() : nullptr;
	if (RequestedSlotSequence == nullptr)
	{
		RequestedSlotBlendSpace = nullptr;
	}
	SlotAimLayerWeight = RequestedSlotBlendSpace != nullptr ? 1.0f : 0.0f;
	SlotAdditiveWeight = RequestedSlotAdditive != nullptr ? 1.0f : 0.0f;

	const float Length = RequestedSlotSequence != nullptr
		? RequestedSlotSequence->GetPlayLength() : 0.0f;
	SlotExplicitTime = ElysiumAnimGraph::SlotEvaluatorTime(PendingSlotCycle, Length,
		bSequenceChanged);
	// The same instant the seconds above name, as the fraction the grid's evaluator states time in.
	SlotNormalizedTime = Length > 0.0f ? FMath::Clamp(SlotExplicitTime / Length, 0.0f, 1.0f) : 0.0f;
	// The record's own enveloped number, gated only by there being a layer to weigh. Retail's slot
	// weight is `m_flWeight` after the blend envelope, and the record already carries exactly that.
	SlotLayerWeight = RequestedSlotSequence != nullptr
		? FMath::Clamp(PendingSlotWeight, 0.0f, ElysiumAnimIntent::SlotWeightMax) : 0.0f;
	PosedSlotSequence = RequestedSlotSequence;

	// The slot's own pin-less writes, one per masked node.
	ApplySlotMask();
	ApplySlotAimMask();
}

void UElysiumBipedAnimInstance::ApplySlotAimMask()
{
	// The name to write: the grid's own mask while a grid stands, null to give the mask back when it
	// goes. Same rules, latches and refusals as `ApplySlotMask` on the node behind it — a separate
	// applier because the two nodes carry two independent masks.
	const FName Wanted = RequestedSlotBlendSpace != nullptr ? PendingSlotAimMaskName : NAME_None;
	if (Wanted == AppliedSlotAimMaskName)
	{
		return;
	}

	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	FAnimNode_LayeredBoneBlend* Layer = Tags != nullptr
		? Tags->FindNodeByTag<FAnimNode_LayeredBoneBlend>(
			FName(ElysiumAnimGraph::SlotAimLayerTag), this)
		: nullptr;
	if (Layer == nullptr)
	{
		// No compiled graph, or one built before this node existed — a state, not an error.
		return;
	}
	if (Layer->BlendMode != ELayeredBoneBlendMode::BlendMask || !Layer->BlendPoses.IsValidIndex(0)
		|| !Layer->BlendMasks.IsValidIndex(0))
	{
		return;
	}

	UBlendProfile* Profile = nullptr;
	if (!Wanted.IsNone())
	{
		USkeleton* Skeleton = GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetSkeleton();
		Profile = Skeleton != nullptr ? Skeleton->GetBlendProfile(Wanted) : nullptr;
		if (Profile == nullptr || Profile->Mode != EBlendProfileMode::BlendMask)
		{
			if (ReportedSlotAimMaskName != Wanted)
			{
				ReportedSlotAimMaskName = Wanted;
				UE_LOG(LogElysiumBipedGraph, Warning,
					TEXT("[elysium] slot aim mask '%s' is absent from this body's skeleton or is not "
						 "a blend mask; the aim layer is left unmasked-refused"),
					*Wanted.ToString());
			}
			return;
		}
	}

	Layer->SetBlendMask(0, Profile);
	AppliedSlotAimMaskName = Wanted;
	ReportedSlotAimMaskName = NAME_None;
}

void UElysiumBipedAnimInstance::ApplySlotMask()
{
	if (RequestedSlotMaskName == AppliedSlotMaskName)
	{
		return;
	}

	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	FAnimNode_LayeredBoneBlend* Layer = Tags != nullptr
		? Tags->FindNodeByTag<FAnimNode_LayeredBoneBlend>(
			FName(ElysiumAnimGraph::SlotLayerTag), this)
		: nullptr;
	if (Layer == nullptr)
	{
		// No compiled graph, or a graph built before the slot's own blend existed. A state rather
		// than an error: the plain native class has no node to write to at all.
		return;
	}

	// `SetBlendMask` asserts all three, so they are tested rather than assumed — same as the
	// autolayer blend's applier, and for the same reason: a graph rebuilt with a different node shape
	// must not turn a mask write into a crash.
	if (Layer->BlendMode != ELayeredBoneBlendMode::BlendMask || !Layer->BlendPoses.IsValidIndex(0)
		|| !Layer->BlendMasks.IsValidIndex(0))
	{
		return;
	}

	UBlendProfile* Profile = nullptr;
	if (!RequestedSlotMaskName.IsNone())
	{
		// Against the PLAYING skeleton, which is why the mask travels as a name: a bank owns every
		// shipped layer, so the profile the node rebuilds its per-bone weights from routinely belongs
		// to a different asset than the clip does. Epic's own `ULayeredBoneBlendLibrary` resolves it
		// the same way.
		USkeleton* Skeleton = GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetSkeleton();
		Profile = Skeleton != nullptr
			? Skeleton->GetBlendProfile(RequestedSlotMaskName) : nullptr;
		if (Profile == nullptr || Profile->Mode != EBlendProfileMode::BlendMask)
		{
			// Said ONCE per requested name. Nothing was written, so the applied name cannot latch this
			// — and the request does not change frame to frame, so an unguarded line here repeats for
			// every frame the layer stands. The applied name is deliberately left alone: this frame's
			// skeleton may simply not be bound yet, and the resolve is retried until it answers.
			if (ReportedSlotMaskName != RequestedSlotMaskName)
			{
				ReportedSlotMaskName = RequestedSlotMaskName;
				UE_LOG(LogElysiumBipedGraph, Warning,
					TEXT("[elysium] overlay slot mask '%s' is absent from this body's skeleton or is not "
						"a blend mask; the layer is left unmasked-refused"),
					*RequestedSlotMaskName.ToString());
			}
			return;
		}
	}

	// Null is a legal write: it is how a body that stopped carrying a slot layer gives the mask back.
	Layer->SetBlendMask(0, Profile);
	AppliedSlotMaskName = RequestedSlotMaskName;
	// A name that resolved is no longer a name a refusal has been reported for, so the next one that
	// does not resolve is said out loud rather than swallowed by a stale latch.
	ReportedSlotMaskName = NAME_None;
}

void UElysiumBipedAnimInstance::ArmDebugUpperBodyOverlay(UAnimSequence* Sequence, UBlendSpace* Space,
	FName MaskName, float Weight)
{
	bDebugUpperBody = true;
	DebugOverlaySequence = Sequence;
	DebugOverlaySpace = Space;
	DebugOverlayMaskName = MaskName;
	DebugLayerWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UElysiumBipedAnimInstance::ArmDebugUpperBodyAdditive(UAnimSequence* Sequence, float Weight)
{
	bDebugUpperBody = true;
	DebugAdditiveSequence = Sequence;
	DebugLayerWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UElysiumBipedAnimInstance::SetDebugUpperBodyAim(float Yaw, float Pitch)
{
	DebugAimYaw = Yaw;
	DebugAimPitch = Pitch;
}

void UElysiumBipedAnimInstance::ClearDebugUpperBodyLayer()
{
	bDebugUpperBody = false;
	DebugOverlaySequence = nullptr;
	DebugOverlaySpace = nullptr;
	DebugAdditiveSequence = nullptr;
	DebugOverlayMaskName = NAME_None;
}

void UElysiumBipedAnimInstance::ApplyUpperBodyMask()
{
	if (RequestedUpperBodyMaskName == AppliedUpperBodyMaskName)
	{
		return;
	}

	// The compiled graph's tag table. Absent on the plain native class — a body with no generated
	// graph package has no node to write to, which is a state rather than an error.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	FAnimNode_LayeredBoneBlend* Layer = Tags != nullptr
		? Tags->FindNodeByTag<FAnimNode_LayeredBoneBlend>(
			FName(ElysiumAnimGraph::UpperBodyLayerTag), this)
		: nullptr;
	if (Layer == nullptr)
	{
		return;
	}

	// `SetBlendMask` asserts all three of these, so they are tested rather than assumed: a graph
	// rebuilt with a different node shape must not turn a mask write into a crash.
	if (Layer->BlendMode != ELayeredBoneBlendMode::BlendMask || !Layer->BlendPoses.IsValidIndex(0)
		|| !Layer->BlendMasks.IsValidIndex(0))
	{
		return;
	}

	// Resolved against the PLAYING skeleton, which is the whole reason the mask travels as a name:
	// the profile the node rebuilds its per-bone weights from has to belong to the skeleton those
	// bone indices are in. This is the same resolution Epic's own `ULayeredBoneBlendLibrary` performs.
	UBlendProfile* Profile = nullptr;
	if (!RequestedUpperBodyMaskName.IsNone())
	{
		USkeleton* Skeleton = GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetSkeleton();
		Profile = Skeleton != nullptr
			? Skeleton->GetBlendProfile(RequestedUpperBodyMaskName) : nullptr;
		if (Profile == nullptr || Profile->Mode != EBlendProfileMode::BlendMask)
		{
			// A named mask the body's own skeleton does not carry. Refused rather than composed
			// unmasked, which would pull the whole rig toward the layer instead of the bones it owns
			// — the same failure the retired accumulator's gate existed to prevent.
			//
			// Once per requested name, and against its own latch rather than the applied one: nothing
			// is written on this path, so the applied name never moves and an unguarded line repeats
			// for every frame the layer stands. Leaving the applied name alone is also what keeps the
			// resolve being retried while a proxy's skeleton is still binding.
			if (ReportedUpperBodyMaskName != RequestedUpperBodyMaskName)
			{
				ReportedUpperBodyMaskName = RequestedUpperBodyMaskName;
				UE_LOG(LogElysiumBipedGraph, Warning,
					TEXT("[elysium] upper-body mask '%s' is absent from this body's skeleton or is not a "
						"blend mask; the layer is left unmasked-refused"),
					*RequestedUpperBodyMaskName.ToString());
			}
			return;
		}
	}

	// Null is a legal write: it is how a body that stopped carrying a layer gives the mask back.
	Layer->SetBlendMask(0, Profile);
	AppliedUpperBodyMaskName = RequestedUpperBodyMaskName;
	// A name that resolved clears the refusal latch, so the next one that does not is reported.
	ReportedUpperBodyMaskName = NAME_None;
}

void UElysiumBipedAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// The garment's game-thread pass.
	Super::NativeUpdateAnimation(DeltaSeconds);

	// --- a request that resolved no asset holds the pose it had -----------------------------------
	//
	// **This is retail's behaviour, not a guard bolted on.** A failed selection never reaches
	// `ResetSequenceInfo`: `m_nSequence` keeps whatever it held and the body goes on playing it. The
	// controlled corpus records exactly one such request on a validated player body — a ducked
	// phase-8 landing asking for `ACT_LAND_CROUCH`, whose selection returns `-1` and for which no
	// clip was ever observed (`docs/vtmb/animation_and_movers.md`).
	//
	// Projecting it anyway is what produced a visible **reference pose**: the stack takes its clip
	// from the pin below, an unresolved request leaves that pin null, and a blend stack with no asset
	// evaluates to the skeleton's bind pose — a T-pose flash for as long as the landing lasts.
	//
	// The **record is untouched** and still names the miss, which is the whole reason a player miss
	// is a named one. Holding also leaves `OneShot` describing the pose that is actually on screen,
	// so the generation gate in the driver's owner reads stale and the latch falls back to its
	// timer — which is what ends the landing.
	// --- the upper-body layer, projected AHEAD of the hold branch ----------------------------------
	//
	// It rides beside the locomotion state rather than through it, so it does not wait on a gait
	// transition — and, for the same reason, a base pose that is being HELD must not freeze it. The
	// two are independent requests that happen to arrive on one record.
	ProjectUpperBodyLayer();

	// And the overlay slot beside it, ahead of the hold branch for the same reason — a body whose
	// landing resolved nothing is still holding a trigger down. It is projected AFTER the autolayers
	// because that is the order it composes in, and unlike them it is not gated on who owns the base.
	ProjectSlotLayer();

	// --- the reaction's phase clock, advanced AHEAD of the hold branch too (LIFE5) -----------------
	//
	// Same reason as the layer above: a base pose that is being HELD is a locomotion answer, and a
	// reaction is a separate request that must not be frozen by it. A held base with a running
	// reaction is the ordinary case for a body whose landing resolved nothing while something shot it.
	if (bReactionActive)
	{
		// Counted UP as well as down, because the two answer different questions: the countdown is
		// seeded from `ActiveSeconds` and says when to ask for the fade back, while the phase needs
		// where in the CLIP the branch is standing.
		ReactionElapsedSeconds += DeltaSeconds;
		// A HELD play is released by its producer, so it has no countdown to advance: the elapsed
		// clock above still runs (the phase has to keep walking a repeating clip), and the branch
		// stands until `StopReaction`.
		if (!bReactionHeld)
		{
			ReactionSecondsLeft -= DeltaSeconds;
			if (ReactionSecondsLeft <= 0.0f)
			{
				// The fan is left on its pins deliberately: the engine is still fading the branch OUT
				// over `ReactionBlendOutSeconds`, and clearing the asset here would evaluate the fade
				// against a null pose — the reference pose — for its whole length.
				bReactionActive = false;
				ReactionSecondsLeft = 0.0f;
			}
		}
	}

	// One barrier for every node read this update, taken here because the hold branch below returns
	// through it too. `GetProxyOnGameThread` blocks on an in-flight parallel evaluation; the tag
	// lookup beside it has none of its own, so the two are taken together and the node is threaded
	// down rather than re-resolved by each reader.
	FElysiumBipedAnimProxy& ProxyRef = GetProxyOnGameThread<FElysiumBipedAnimProxy>();
	FAnimNode_BlendStack* Stack = FindLocomotionStack();

	bHoldingPose = ElysiumAnimGraph::ShouldHoldPose(bHasApplied, PendingSequence != nullptr,
		PendingBlendSpace != nullptr);
	if (bHoldingPose)
	{
		// The phase clock advances on this path too, for the same reason the layer and the reaction
		// clock are projected ahead of the hold: a HELD base is a locomotion answer about the
		// SELECTION, and whatever the body is actually standing on — a montage one-shot, a scene
		// clip, a reaction, or the clip the stack is still playing — goes on running. Freezing it
		// here would hand the event pass a zero-delta frame and then fire the whole skipped interval
		// at once the moment the hold ended.
		RefreshBasePhase(ProxyRef, Stack);
		return;
	}

	// What the stack is standing on right now, captured BEFORE the projection overwrites its pins.
	// The loop half of it is the one thing the node cannot notice on its own.
	UAnimationAsset* const PreviousAsset = RequestedAsset;
	const bool bPreviousLooping = bRequestedLooping;

	// --- project the record onto what the graph reads --------------------------------------------
	// The state is READ off the record rather than derived here. The resolver projected it once, and
	// an instance that re-derived its own would be a second answer to the question the record exists
	// to settle — visible the day a readout and the pose disagree about where the body is standing.
	RequestedState = Pending.GraphState;
	// **One pin for both shapes.** A fan and a single clip are both `UAnimationAsset`s, and the stack
	// asks the asset which player to build — so the "exactly one of these two" the resolver answers
	// with collapses here rather than fanning out into a branch the graph has to carry. The blend
	// space wins the coalesce because the two are never both set; the pair is still staged separately
	// because `ShouldHoldPose` above asks about it.
	RequestedAsset = PendingBlendSpace != nullptr
		? static_cast<UAnimationAsset*>(PendingBlendSpace)
		: static_cast<UAnimationAsset*>(PendingSequence);
	// A held stance repeats its into-pose, which is retail's reselect-and-restart expressed as a
	// loop (see `bRequestedLooping`). Every other state takes the model's own bit unchanged, and the
	// record keeps the authored value either way.
	bRequestedLooping = ElysiumAnimGraph::ShouldRepeatClip(RequestedState, Pending.bLooping);
	GridAxis0 = Pending.AxisValue[0];
	GridAxis1 = Pending.AxisValue[1];
	// The steering pair in the shape the node takes it. The third component is unused: every grid
	// VtMB ships is one- or two-dimensional, and a blend space reads only as many axes as it has.
	RequestedBlendParameters = FVector(GridAxis0, GridAxis1, 0.0f);
	Speed = Pending.Speed;
	MoveYaw = Pending.MoveYaw;
	bHasBlendSpace = PendingBlendSpace != nullptr;

	// --- the loop bit the node cannot notice on its own -------------------------------------------
	//
	// `FAnimNode_BlendStack::ConditionalBlendTo` compares the requested asset against the one it is
	// playing and returns early when they match; `bLoop` is read only inside `BlendTo`, as an
	// argument to the player it constructs. So a loop flip on the SAME asset holds the pin and
	// changes nothing, with nothing logged — and `Crouch` is exactly that request, a non-looping
	// into-pose republished as a held stance. The forced re-blend is what makes the pin honest.
	//
	// LIFE5's repeated-identical-request restart is this door's second caller when it lands: a hit
	// that re-fires the same clip is the same "the asset did not change and it still has to blend
	// again" shape, and it goes through this predicate rather than growing one of its own.
	//
	// **An EMPTY stack is excluded, and that is a guard against the node rather than an
	// optimization.** `ConditionalBlendTo` consumes `bForceBlendNextUpdate` only on the branch where
	// a player is already standing; the empty-stack branch blends and leaves the flag SET, so the
	// following update forces a second blend of the same asset seeded at its head — a visible restart
	// and cross-fade of the gait. An empty stack needs no force in the first place: the asset pin is
	// non-null here, so the node blends onto it either way.
	if (ElysiumAnimGraph::NeedsForcedReblend(
			RequestedAsset != nullptr && RequestedAsset == PreviousAsset,
			bRequestedLooping != bPreviousLooping)
		&& Stack != nullptr && !Stack->AnimPlayers.IsEmpty())
	{
		Stack->ForceBlendNextUpdate();
	}

	// --- one blend per discrete request change ---------------------------------------------------
	if (!bHasApplied || Pending.Generation != Applied.Generation)
	{
		// The authored fade, combined as retail combines it. It reaches the blend stack on its
		// `BlendTime` pin, whole and capped by nothing: the node reads the pin at the instant it
		// pushes the new player, so this write and the asset write above land on the same request.
		//
		// A duration of 0 is a legal request rather than a refusal, so `flags & 0x2`'s hard cut falls
		// out of the same call instead of needing a branch of its own.
		//
		// **An outgoing record that posed nothing is not an outgoing operand.** `bHasApplied` alone
		// goes true on the first update of any body, asset or no asset, so a first publish that
		// resolved no clip would present a non-null descriptor while the stack beneath it is still
		// holding nothing — the skeleton's bind pose. The fade then blends the first REAL clip up out
		// of a T-pose for its whole duration, which is exactly the defect the null-outgoing refusal
		// exists to kill. The verdict is latched below at publish time, because
		// `FElysiumAnimationSelection::AssetKind` cannot answer it: the assets are resolved beside the
		// record and either can be absent while the other is not.
		const bool bFadeableOutgoing = bHasApplied && bAppliedPosedAnAsset;
		const float BlendSeconds = ElysiumAnimGraph::TransitionSeconds(
			bFadeableOutgoing ? &Applied : nullptr, Pending);
		RequestedBlendSeconds = BlendSeconds;

		// **Two named divergences from retail live on this transition**, both properties of the
		// engine node rather than choices, and both recorded here beside the faithful behaviour they
		// depart from (`docs/vtmb/animation_and_movers.md`). The third — retail's UNBOUNDED
		// concurrent-transition count against this node's required `MaxActiveBlends` — is a property
		// of the node's configuration rather than of a transition, and is named where that value is
		// written (`Editor/ElysiumAnimGraphLibrary.cpp`):
		//
		// 1. **nlerp for slerp.** `FAnimNode_BlendStack_Standalone::BlendWithPose` accumulates the
		//    incoming pose with `AccumulateWithShortestRotation` and normalizes afterward, which is a
		//    normalized linear blend. Retail's own transitioner interpolates the two quaternions
		//    spherically (`:2884`). The paths differ only in the middle of a fade and only by the
		//    chord-versus-arc error: measured over the corpus's transitions the median per-bone
		//    deviation is 1.6e-6 radians against slerp's own 3.5e-8 rounding floor — four orders of
		//    magnitude under a frame of authored motion, and invisible at any playback rate.
		//
		// 2. **Inverted tail nesting at depth 3 or more.** The stack seeds its accumulation
		//    oldest-player-first and blends forward; retail folds newest-previous-first (`:2924-2927`).
		//    With two players standing the two orders are identical, which is every ordinary
		//    transition. They part only when a third request lands while two are still fading — the
		//    capture saw up to four — and there the intermediate weights differ while both endpoints
		//    and the final settled pose do not.

		// The one place a blend is DECIDED, and the only place it is observable. Everything downstream
		// is a fade already in progress, which on screen is indistinguishable from an authored hard cut
		// or from a dropped blend — so the decision is recorded and logged here rather than sampled
		// afterward from a pose that can no longer say which of the three it is.
		//
		// One line per real transition, never per frame: this branch is gated on the generation
		// changing, so a body standing in one selection emits nothing however long it stands.
		//
		// The two operand strings are built inside the verbosity check rather than as arguments to
		// it. `UE_LOG` evaluates its arguments only when the category is active, but a local built
		// ahead of the macro allocates on every transition of every body whether anything is
		// listening or not.
		// "Nothing to fade FROM", which is what the report's field means and is wider than "nothing
		// has ever been published": a record that resolved no clip left the stack holding nothing,
		// and there is no more to fade out of that than out of a body that never published.
		const bool bNoOutgoingClip = !bFadeableOutgoing;
		// The state readout, which is all it is now: the graph carries no machine to leave, so this
		// decides nothing and is recorded because the trace and the Cog row still distinguish a
		// walk-to-run transition from a walk-to-walk republish.
		const bool bStateTransition = Pending.GraphState != Applied.GraphState;
		if (UE_LOG_ACTIVE(LogElysiumBipedGraph, Verbose))
		{
			const FString From = bNoOutgoingClip
				? FString(TEXT("(nothing)"))
				: FString::Printf(TEXT("%s '%s' [%s]"),
					Applied.ResolvedActivity.IsEmpty() ? TEXT("?") : *Applied.ResolvedActivity,
					*Applied.AnimationName,
					Applied.OwnerStem.IsEmpty() ? TEXT("?") : *Applied.OwnerStem);
			const FString To = FString::Printf(TEXT("%s '%s' [%s]"),
				Pending.ResolvedActivity.IsEmpty() ? TEXT("?") : *Pending.ResolvedActivity,
				*Pending.AnimationName,
				Pending.OwnerStem.IsEmpty() ? TEXT("?") : *Pending.OwnerStem);
			UE_LOG(LogElysiumBipedGraph, Verbose,
				TEXT("transition %s -> %s over %.3fs (%s%s)"),
				*From, *To, BlendSeconds,
				bStateTransition ? TEXT("state-transition") : TEXT("in-state"),
				// The two zero-second answers named apart, because they are different facts about the
				// same number: `flags & 0x2` is the incoming clip's authored hard cut, while an
				// outgoing record posing no clip has nothing to fade FROM
				// (`ElysiumAnimGraph::TransitionSeconds`).
				Pending.bSnap ? TEXT(", SNAP")
					: (bNoOutgoingClip ? TEXT(", nothing to fade from") : TEXT("")));
		}

		Blend = FElysiumBlendReport();
		Blend.Generation = Pending.Generation;
		Blend.RequestedSeconds = BlendSeconds;
		Blend.bSnap = Pending.bSnap;
		Blend.bFirstPublish = bNoOutgoingClip;
		Blend.bStateTransition = bStateTransition;
		Blend.FromAnimation = bNoOutgoingClip ? FString() : Applied.AnimationName;
		Blend.ToAnimation = Pending.AnimationName;
		Blend.StampSeconds = GetWorld() != nullptr
			? static_cast<double>(GetWorld()->GetTimeSeconds()) : -1.0;

		Applied = Pending;
		bHasApplied = true;
		// The verdict the NEXT transition's outgoing operand is gated on, latched where the assets
		// that answer it are in hand. What matters is whether the graph will actually be evaluating
		// something, not what the record claims it resolved.
		bAppliedPosedAnAsset = RequestedAsset != nullptr;
	}

	// --- read the graph back ---------------------------------------------------------------------
	OneShot = FElysiumOneShotReport();
	OneShot.Generation = Pending.Generation;
	// One-shots are single clips; a fan is a gait, and a `Cast` here is what keeps the length below
	// the SEQUENCE's rather than a blend space's normalized one.
	UAnimSequence* const OneShotClip = Cast<UAnimSequence>(RequestedAsset);
	// **The identity gate runs FIRST, before any clock is read, and it carries the whole guard.**
	// The stack's remaining time is its current player's length less its adjusted time, so a stack
	// that is empty — or that is still standing on the clip this request replaced — answers a small
	// number rather than an absurd one. Its failure direction is "already finished", which would
	// silently END a one-shot that has not started; `IsPlayableRemaining` cannot catch that, because
	// zero is also what a genuinely completed clip answers. Only asking the node whether it is
	// playing the asset that was requested can.
	//
	// The blend-in weight is checked for its own reason: a clip still fading up is not yet the pose
	// on screen, and the latch must not end a leap the body has barely entered.
	//
	// Anything that is not a sane finite duration still reports **nothing at all**, which leaves
	// `RemainingSeconds` at its "cannot say" −1 and routes the latch back to its fallback timer.
	if (Stack != nullptr
		&& OneShotClip != nullptr
		&& Stack->GetAnimAsset() == OneShotClip
		&& ElysiumAnimGraph::IsOneShotState(RequestedState)
		&& !Stack->AnimPlayers.IsEmpty()
		&& Stack->AnimPlayers[0].GetBlendInWeight() > 0.99f)
	{
		const float Length = Stack->GetCurrentAssetLength();
		const float Remaining = Length - Stack->GetCurrentAssetTimePlayRateAdjusted();
		if (ElysiumAnimGraph::IsPlayableRemaining(Remaining, OneShotClip->GetPlayLength()))
		{
			OneShot.bInOneShotState = true;
			OneShot.RemainingSeconds = Remaining;
			OneShot.bComplete = Remaining <= KINDA_SMALL_NUMBER;
		}
	}

	// --- the base channel's phase, last (LIFE5) ---------------------------------------------------
	//
	// After the publish, so the locomotion arm's identity comes off the record the stack is about to
	// pose rather than the one it just left. Every other arm was armed synchronously at its own play
	// seam; only the cycle is advanced here.
	RefreshBasePhase(ProxyRef, Stack);
}

bool UElysiumBipedAnimInstance::HasCompiledGraph() const
{
	// The class itself is the answer, and it is available before the first update: a generated
	// Animation Blueprint class carries the compiled graph, the plain native class carries none.
	// Asking the proxy for a root node would be the same question a frame later and only on the
	// worker's terms.
	//
	// **A cast of the class, not `IsChildOf` on it, and the difference is the whole answer.**
	// `UAnimBlueprintGeneratedClass` is the METACLASS of a generated graph class, not an ancestor of
	// it: `ABP_ElysiumBiped_C` derives from `UElysiumBipedAnimInstance`, and it is
	// `UAnimBlueprintGeneratedClass` that it is an instance OF. `IsChildOf` walks the superclass chain
	// and so answers false for every body that has a graph and every body that does not — a predicate
	// that is never true, on a path where false is the quiet fallback. `UClass::IsA` is private
	// precisely to stop the same confusion being written the other way round, so the cast is the door.
	return Cast<UAnimBlueprintGeneratedClass>(GetClass()) != nullptr;
}

bool UElysiumBipedAnimInstance::PlayOneShot(const FElysiumClipIdentity& Identity,
	UAnimSequence* Sequence, bool bLoop, float BlendInSeconds, float BlendOutSeconds, bool bRestart,
	float PlayRate)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// A non-positive rate is not a slow play, it is a stopped one, and nothing in the recovered
	// producer set can ask for it: `m_flPlaybackRate` is a product of two positive scalars. Refusing
	// it here would silence a caller bug, so it is named and the authored speed stands.
	if (!(PlayRate > 0.0f) || !FMath::IsFinite(PlayRate))
	{
		UE_LOG(LogElysiumBipedGraph, Warning,
			TEXT("[elysium] clip '%s' was handed playback rate %.3f, which is not a speed — playing "
				"it at the authored 1.0 instead"), *Identity.Label, PlayRate);
		PlayRate = 1.0f;
	}
	// No compiled graph means no slot node to play a montage into, so the clip player answers
	// instead. That is the body whose generated graph package is not on the mount — a named failure
	// rather than a silent one, and it still animates.
	//
	// It arms the phase too, through `PlayClip`'s own arm: the event dispatcher's rule is a property
	// of the clip, not of which host happens to be posing it, and a body on the fallback path fires
	// its footsteps like any other.
	if (!HasCompiledGraph())
	{
		PlayClip(Identity, Sequence, bLoop, bRestart, PlayRate);
		return true;
	}
	// The montage route needs no restart branch: `PlaySlotAnimationAsDynamicMontage` builds a fresh
	// montage on every call, so a repeated identical request always re-fires from frame one. That is
	// the restart helper's own shape, and it is what a repeated attack already relies on. `bRestart`
	// therefore reaches only the clip player above, which is the one path that holds a repeat.
	// A clip with nothing to blend FROM snaps in, which is retail's own rule: otherwise a body would
	// fade up out of the reference pose on map load, because the slot's source pose is a blend stack
	// that has been handed no asset yet.
	//
	// **The predicate is that source pose, not "is this the first montage".** The slot blends the
	// clip against whatever the stack holds, so a graph already posing a real asset has something to
	// blend from even when nothing has been on the slot before — and snapping onto a posed body is
	// the pop this rule exists to avoid. `bHasApplied` is NOT the fact to ask: it goes true on the
	// first update of any body, asset or no asset, because a record that resolved nothing is still a
	// record applied. The asset the graph is actually evaluating is.
	const bool bGraphPosesAnAsset = RequestedAsset != nullptr;
	const float BlendIn =
		(bGraphPosesAnAsset || Montage_IsPlaying(ActiveSlotMontage)) ? BlendInSeconds : 0.0f;
	// A loop count of 0 is infinite. The blend IN is the clip's own authored fade — the same number
	// the locomotion transition uses, so one authority serves both consumers.
	//
	// A LOOPING clip carries no blend out, and that is not a tidiness choice. The blend-out trigger
	// is armed relative to the montage's own length, so on a looping montage it fires at every pass
	// of the loop point and dips the slot's weight before the next pass restores it. The slot's
	// source pose is the blend stack underneath, which for a body that has been handed no
	// selection is the REFERENCE pose — so the dip shows as a single frame of the authored bind
	// pose. A clip that never ends has nothing to blend out to; only the one-shot does.
	const float BlendOut = bLoop ? 0.0f : BlendOutSeconds;
	// A loop count of 0 is NOT infinite here: the dynamic montage's length is
	// `LoopingCount * segment length`, and `Montage_Play` refuses a zero-length montage without
	// logging. A looping clip therefore asks for a segment long enough to outlast any lab or
	// gameplay hold; the montage is replaced, not resumed, on every new selection.
	constexpr int32 LoopingHoldCount = 1000000;
	// **The producer's rate reaches the montage.** Retail has one `m_flPlaybackRate` per play, so the
	// drawn speed, the montage position and therefore the cycle every consumer reads all scale off
	// this one number rather than the transaction keeping a private copy of it.
	ActiveSlotMontage = PlaySlotAnimationAsDynamicMontage(Sequence, FAnimSlotGroup::DefaultSlotName,
		BlendIn, BlendOut, PlayRate, /*LoopCount=*/ bLoop ? LoopingHoldCount : 1);
	if (ActiveSlotMontage == nullptr)
	{
		return false;
	}
	// Armed HERE rather than on the next update, and the difference is one frame of a commit. The
	// weapon transactions call `ResolveAndPlay` and `CommitArrivesFromAnimEvent` in the same
	// statement pair (`Substrate/ElysiumWeaponClasses.cpp`), so the phase has to name the new clip at
	// the instant the montage was accepted — a tick-only publish still names the clip it replaced.
	//
	// **The SEQUENCE's length, never the montage's.** `PlaySlotAnimationAsDynamicMontage` builds a
	// looping clip as `LoopingHoldCount` segments and reports that as its length; dividing a position
	// by it pins the cycle at ~0 forever, which fires the timeline's first record and nothing else.
	ArmBasePhase(EElysiumBasePhaseSource::Montage, Identity, Sequence->GetPlayLength(), bLoop,
		PlayRate);
	return true;
}

bool UElysiumBipedAnimInstance::HasCompiledReactionBranch() const
{
	if (bReactionBranchResolved)
	{
		return bHasReactionBranch;
	}
	// The compiled class's tag table, the same door `ApplyUpperBodyMask` uses. It is compiled state,
	// so one lookup answers for the instance's whole life — and a class with no table (the plain
	// native host) answers false without another lookup every time something is hit.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	bHasReactionBranch = Tags != nullptr
		&& Tags->FindNodeByTag<FAnimNode_BlendListByBool>(
			FName(ElysiumAnimGraph::ReactionBranchTag),
			const_cast<UElysiumBipedAnimInstance*>(this)) != nullptr;
	bReactionBranchResolved = true;
	return bHasReactionBranch;
}

bool UElysiumBipedAnimInstance::PlayReaction(const FElysiumReactionPlay& Play)
{
	if (!Play.IsValid())
	{
		UE_LOG(LogElysiumBipedGraph, Warning,
			TEXT("[elysium] reaction refused: space='%s' sequence='%s' length %.3fs release=%s "
				"(in %.2f out %.2f) -- exactly one asset and a positive length are required, and an "
				"envelope play needs a positive fade pair"),
			*GetNameSafe(Play.Space), *GetNameSafe(Play.Sequence), Play.LengthSeconds,
			ElysiumAnimIntent::ReactionReleaseName(Play.Release), Play.BlendInSeconds,
			Play.BlendOutSeconds);
		return false;
	}
	if (!HasCompiledReactionBranch())
	{
		// A real case rather than a guard: a generated graph package built before the branch existed
		// loads, compiles and poses. The caller collapses to a single clip on this answer, so it is
		// reported once here and acted on there.
		UE_LOG(LogElysiumBipedGraph, Warning,
			TEXT("[elysium] '%s' carries no reaction branch tagged '%s'; the reaction cannot be "
				"played on this class"),
			*GetClass()->GetName(), ElysiumAnimGraph::ReactionBranchTag);
		return false;
	}

	// The blend IN takes the same predicate `PlayOneShot` does, and for the same reason: the branch's
	// false pose is the locomotion pose, so a graph that has been handed no asset is blending up out
	// of the reference pose. A body with nothing to blend FROM snaps in.
	const bool bGraphPosesAnAsset = RequestedAsset != nullptr;
	ReactionBlendInSeconds = bGraphPosesAnAsset ? FMath::Max(Play.BlendInSeconds, 0.0f) : 0.0f;
	ReactionBlendOutSeconds = FMath::Max(Play.BlendOutSeconds, 0.0f);

	// The assets, republished whole. **This is also the retrigger**: a sequence player whose
	// `Sequence` pin changes restarts from the top, and so does the blend space player on a new
	// `BlendSpace`, so a second hit on a body already reacting plays its own clip from frame one.
	RequestedReactionBlendSpace = Play.Space;
	RequestedReactionSequence = Play.Sequence;
	bReactionHasBlendSpace = Play.Space != nullptr;
	ReactionAxis0 = Play.AxisValue;

	// **A retrigger onto the SAME fan continues its phase**, because the pin did not change and a
	// blend space player only restarts on a new asset. That is a named residual rather than a
	// defect: it costs a second hit from the same direction its own wind-up, and the alternatives
	// (a per-play generation on the pin, or a child reset that hard-cuts the base on release) both
	// buy it with a worse failure.
	// The play owns the arithmetic, because the channel claim reads the other half of it: the claim's
	// `HoldSeconds` is `TotalSeconds`, this is `ActiveSeconds`, and one expression is what stops a
	// claim expiring while the branch is still fading. What each release condition answers, and why,
	// is on `ActiveSeconds` itself.
	bReactionHeld = Play.IsHeld();
	bReactionLoops = Play.bLoop;
	ReactionSecondsLeft = bReactionHeld ? 0.0f : Play.ActiveSeconds();
	ReactionElapsedSeconds = 0.0f;
	bReactionActive = true;
	// The branch owns the base pose while it stands, so it owns the base channel's phase with it.
	// The length is the CLIP's (a fan's engine-blended length at the sampled parameter), not the
	// branch's own hold — `ActiveSeconds` is a different span for every release condition, and a cycle
	// divided by any of them would name the wrong frame.
	ArmBasePhase(EElysiumBasePhaseSource::Reaction,
		FElysiumClipIdentity(Play.OwnerStem, Play.Label), Play.LengthSeconds, Play.bLoop);

	UE_LOG(LogElysiumBipedGraph, Verbose,
		TEXT("reaction %s '%s' at %.1f (%.3fs, in %.2f out %.2f, release=%s, active for %.3fs)"),
		Play.Space != nullptr ? TEXT("fan") : TEXT("clip"),
		Play.Space != nullptr ? *GetNameSafe(Play.Space) : *GetNameSafe(Play.Sequence),
		Play.AxisValue, Play.LengthSeconds, ReactionBlendInSeconds, ReactionBlendOutSeconds,
		ElysiumAnimIntent::ReactionReleaseName(Play.Release), ReactionSecondsLeft);
	return true;
}

void UElysiumBipedAnimInstance::StopReaction()
{
	// The assets stay on their pins for the fade the engine is about to run — the same reason the
	// phase clock's own expiry leaves them. They are replaced by the next `PlayReaction`.
	bReactionActive = false;
	bReactionHeld = false;
	bReactionLoops = false;
	ReactionSecondsLeft = 0.0f;
	ReactionElapsedSeconds = 0.0f;
	// Only this producer's arm. A montage or a scene clip standing under the reaction goes on
	// playing and takes the channel back on the next publish, with its own clock where it left it.
	DisarmBasePhase(EElysiumBasePhaseSource::Reaction);
}

void UElysiumBipedAnimInstance::StopOneShot(float BlendSeconds)
{
	if (!HasCompiledGraph())
	{
		StopClip();
		return;
	}
	if (ActiveSlotMontage != nullptr)
	{
		Montage_Stop(BlendSeconds, ActiveSlotMontage);
		ActiveSlotMontage = nullptr;
		// The engine may still be fading the montage out, but the caller has ENDED this play: retail
		// stops dispatching a sequence's events the moment it is deselected, and firing a footstep
		// out of a clip nobody is asking for any more is the same defect as firing one twice. Only
		// THIS producer's arm — a reaction or a scene clip on the same body is a different clock and
		// keeps it.
		DisarmBasePhase(EElysiumBasePhaseSource::Montage);
	}
}

// ================================================================================================
// The cinematic clip path and the autolayers, over the proxy
//
// Every one of these goes through GetProxyOnGameThread, which blocks on any in-flight parallel
// evaluation — that block is the whole reason the writes below cannot race the worker.
// ================================================================================================

void UElysiumBipedAnimInstance::PlayClip(const FElysiumClipIdentity& Identity,
	UAnimSequence* Sequence, bool bLoop, bool bRestart, float PlayRate)
{
	if (Sequence == nullptr)
	{
		return;
	}
	// A standing clip IS the body pose and the graph's output is not consumed at all while it holds,
	// so a reaction left active behind it would be an invisible branch pinning its own assets and
	// counting down a phase nothing is showing. The scene replaces the graph; it takes the branch too.
	StopReaction();
	const bool bStarted = GetProxyOnGameThread<FElysiumBipedAnimProxy>().PlayDirect(Sequence, bLoop,
		bRestart, PlayRate);
	// **The arm follows the pose.** `PlayDirect` holds a repeated identical looping clip rather than
	// resetting it, and arming a new `PlayId` over a clip that did not restart would re-fire its whole
	// timeline against a cycle nothing moved — a footstep per re-request on a body that never took a
	// step. A held request keeps its standing arm, which is what leaves the dispatcher's cursor where
	// the clip actually is.
	//
	// A scene clip is pinned to scene time and can be seeked backwards; the dispatcher's own rule
	// already answers a backwards phase on a one-shot by re-anchoring and firing nothing, which is
	// exactly what a seek should do (`Substrate/ElysiumAnimEvents.cpp`).
	if (bStarted)
	{
		ArmBasePhase(EElysiumBasePhaseSource::Clip, Identity, Sequence->GetPlayLength(), bLoop,
			PlayRate);
	}
}

void UElysiumBipedAnimInstance::SeekClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().Seek(PositionSeconds);
}

void UElysiumBipedAnimInstance::StopClip()
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().StopDirect();
	DisarmBasePhase(EElysiumBasePhaseSource::Clip);
}

UAnimSequence* UElysiumBipedAnimInstance::GetPlayingClip() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetPlaying();
}

float UElysiumBipedAnimInstance::GetClipPosition() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetClipPosition();
}

void UElysiumBipedAnimInstance::ResyncClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().ResyncPosition(PositionSeconds);
}

// ================================================================================================
// The base channel's phase clock (LIFE5)
//
// One PUBLISHED timeline per body, because retail has exactly one: `DispatchAnimEvents` stores the
// last checked cycle on the animating object at `+0x658`, and nothing advances a layer's own cycle
// server-side (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch").
//
// Four ARMED clocks behind it, because the four producers run concurrently — a reaction replaces
// the locomotion pose without stopping the montage under it, and that montage rides a blend stack
// which never stopped either. Precedence decides which one is the timeline; it never decides which
// ones exist. A single shared record would let the newest arm erase a clip that is still playing,
// and the displaced one could never take the channel back.
// ================================================================================================

void UElysiumBipedAnimInstance::ArmBasePhase(EElysiumBasePhaseSource Source,
	const FElysiumClipIdentity& Identity, float LengthSeconds, bool bLoop, float PlayRate)
{
	if (!Identity.IsValid())
	{
		// A clip nobody named. Ordinary rather than a failure — a preview stand, a green-room grid
		// and a lab clip all arm one — and it disarms only this producer, so whatever is still
		// playing underneath keeps the channel.
		DisarmBasePhase(Source);
		return;
	}

	FElysiumArmedClip& Arm = Armed(Source);
	Arm = FElysiumArmedClip();
	Arm.Identity = Identity;
	Arm.LengthSeconds = FMath::Max(0.0f, LengthSeconds);
	Arm.PlayRate = PlayRate;
	Arm.bLooping = bLoop;
	// The restart discriminator, and the only thing that can say a repeated attack is a new play:
	// the owner, the label and the phase are all identical between two plays of one clip
	// (`Substrate/ElysiumAnimEvents.cpp`).
	Arm.PlayId = ++NextPlayId;
	// Zero, because a play seam STARTED this clip — the one producer that did not is the blend
	// stack, which arms itself in `RefreshLocomotionArm` and anchors where it finds its clip running.
	Arm.AnchorCycle = 0.0f;

	// Published at the instant the seam accepted the clip, not on the next update. The weapon
	// transactions call `ResolveAndPlay` and `CommitArrivesFromAnimEvent` in the same statement pair
	// (`Substrate/ElysiumWeaponClasses.cpp`), so a phase that only appeared a frame later would name
	// the clip this one replaced. The clocks are NOT read here: the node this arm names has not run
	// yet, and the clip player in particular still reports the previous clip's position.
	PublishBasePhase(/*bReadClocks=*/false);
}

void UElysiumBipedAnimInstance::DisarmBasePhase(EElysiumBasePhaseSource Source)
{
	Armed(Source) = FElysiumArmedClip();
	PublishBasePhase(/*bReadClocks=*/false);
}

EElysiumBasePhaseSource UElysiumBipedAnimInstance::LiveBaseSource(FElysiumBipedAnimProxy& InProxy,
	const FAnimNode_BlendStack* Stack) const
{
	// The order the pose itself composes: a cinematic clip replaces the graph outright, a reaction
	// replaces the locomotion pose, the DefaultSlot montage rides over the blend stack, and the
	// stack is what is left underneath.
	if (InProxy.GetPlaying() != nullptr)
	{
		return EElysiumBasePhaseSource::Clip;
	}
	if (bReactionActive)
	{
		return EElysiumBasePhaseSource::Reaction;
	}
	if (ActiveSlotMontage != nullptr && Montage_IsPlaying(ActiveSlotMontage))
	{
		return EElysiumBasePhaseSource::Montage;
	}
	// **The identity question comes first, and the LENGTH second.** A stack standing on the clip a
	// newer request has already replaced answers a perfectly plausible length off the wrong asset,
	// and `GetCurrentAssetLength` is zero on an empty stack — which is also a legal-looking cycle of
	// zero if it were divided into. Both are refused before the arm is considered live.
	if (Stack != nullptr && RequestedAsset != nullptr && Stack->GetAnimAsset() == RequestedAsset
		&& Stack->GetCurrentAssetLength() > 0.0f)
	{
		return EElysiumBasePhaseSource::Locomotion;
	}
	return EElysiumBasePhaseSource::None;
}

float UElysiumBipedAnimInstance::LiveBaseCycle(EElysiumBasePhaseSource Source,
	FElysiumBipedAnimProxy& InProxy, const FAnimNode_BlendStack* Stack) const
{
	const FElysiumArmedClip& Arm = Armed(Source);
	// A degenerate length answers with the anchor rather than a division: the cursor then sees a
	// zero-delta frame and fires nothing, which is the honest answer for a clock that cannot say.
	if (Arm.LengthSeconds <= 0.0f)
	{
		return Arm.AnchorCycle;
	}

	switch (Source)
	{
	case EElysiumBasePhaseSource::Clip:
	{
		// **A pending restart is another "cannot say".** `PlayDirect` swaps the sequence on the game
		// thread and leaves the time accumulator alone until the worker reinitializes the node, so a
		// clip that replaced one already running reports the OLD clip's position for a frame. Divided
		// by the new clip's length that is a phase for a play nobody made — and a fresh arm anchors
		// at zero, so the cursor would fire everything below it in one burst. The anchor answers
		// instead, which is a zero-delta frame: the next update has the real position.
		//
		// Negative is the player's own "cannot say", and it is a different answer from "at zero".
		const float Position = InProxy.HasPendingClipRestart() ? -1.0f : InProxy.GetClipPosition();
		if (Position < 0.0f)
		{
			return Arm.AnchorCycle;
		}
		return Arm.bLooping
			? FMath::Frac(Position / Arm.LengthSeconds)
			: FMath::Clamp(Position / Arm.LengthSeconds, 0.0f, 1.0f);
	}
	case EElysiumBasePhaseSource::Reaction:
		// A struck reaction never loops, so its phase saturates at 1 — the terminal position a
		// finished one-shot has, and the one place a phase is legally not below 1. A HELD reaction
		// repeats, so its phase wraps like any other looping clip and its timeline fires once a pass.
		return bReactionLoops
			? FMath::Frac(ReactionElapsedSeconds / Arm.LengthSeconds)
			: FMath::Clamp(ReactionElapsedSeconds / Arm.LengthSeconds, 0.0f, 1.0f);
	case EElysiumBasePhaseSource::Montage:
	{
		// The montage's position runs `0 .. LoopCount * clip length`, so the CLIP's phase is the
		// fraction of one segment. A one-shot's position saturates at its own length while the
		// blend-out runs, and `Frac` of exactly 1 is 0 — which would read as a wrap and fire the
		// whole timeline again on a clip that has ended. It clamps instead, and 1 is terminal.
		const float Position = Montage_GetPosition(ActiveSlotMontage);
		return Arm.bLooping
			? FMath::Frac(Position / Arm.LengthSeconds)
			: FMath::Clamp(Position / Arm.LengthSeconds, 0.0f, 1.0f);
	}
	case EElysiumBasePhaseSource::Locomotion:
	{
		// The identity gate again, and before the clock rather than after it: the stack's time and
		// length both come off `AnimPlayers[0]`, so a stack still standing on the clip this request
		// replaced answers a phase for the wrong timeline — plausible, monotonic and wrong. The
		// anchor is the honest reply, and it is a zero-delta frame the cursor fires nothing on.
		if (Stack == nullptr || RequestedAsset == nullptr
			|| Stack->GetAnimAsset() != RequestedAsset)
		{
			return Arm.AnchorCycle;
		}
		const float Length = Stack->GetCurrentAssetLength();
		if (Length <= 0.0f)
		{
			return Arm.AnchorCycle;
		}
		return FMath::Clamp(Stack->GetCurrentAssetTimePlayRateAdjusted() / Length, 0.0f, 1.0f);
	}
	default:
		return Arm.AnchorCycle;
	}
}

void UElysiumBipedAnimInstance::PublishBasePhase(bool bReadClocks)
{
	// One proxy fetch for the whole publish, and the stack resolved inside the window it opens.
	// `GetProxyOnGameThread` is a barrier against an in-flight parallel evaluation; the tag-table
	// lookup has none of its own, so it belongs after the barrier and its answer is threaded down
	// rather than re-asked by each reader.
	FElysiumBipedAnimProxy& ProxyRef = GetProxyOnGameThread<FElysiumBipedAnimProxy>();
	PublishBasePhase(ProxyRef, FindLocomotionStack(), bReadClocks);
}

void UElysiumBipedAnimInstance::PublishBasePhase(FElysiumBipedAnimProxy& InProxy,
	const FAnimNode_BlendStack* Stack, bool bReadClocks)
{
	const EElysiumBasePhaseSource Live = LiveBaseSource(InProxy, Stack);
	const FElysiumArmedClip& Arm = Armed(Live);
	if (Live == EElysiumBasePhaseSource::None || !Arm.IsArmed())
	{
		// Nothing is posing this body, or what is posing it named no clip — a preview stand, a lab
		// grid, a scene clip armed with no identity. There is no timeline this channel can address,
		// so it publishes nothing rather than walking one clip's records against another's cycle.
		BasePhase = FElysiumClipPhase();
		PhaseSource = EElysiumBasePhaseSource::None;
		return;
	}

	BasePhase = FElysiumClipPhase();
	BasePhase.OwnerStem = Arm.Identity.OwnerStem;
	BasePhase.Label = Arm.Identity.Label;
	BasePhase.Length = Arm.LengthSeconds;
	BasePhase.PlayRate = Arm.PlayRate;
	BasePhase.bLooping = Arm.bLooping;
	BasePhase.PlayId = Arm.PlayId;
	// Where the dispatcher last left THIS arm, carried as it stood before this frame's advance. A
	// cursor meeting the play for the first time resumes from it, which is what makes a clip that
	// was displaced and came back walk the interval it really passed through instead of replaying
	// its whole timeline from zero.
	BasePhase.AnchorCycle = Arm.AnchorCycle;
	BasePhase.Channel = EElysiumAnimChannel::Base;
	BasePhase.Cycle = bReadClocks ? LiveBaseCycle(Live, InProxy, Stack) : Arm.AnchorCycle;
	PhaseSource = Live;
	// Advance the arm's own anchor to what was just published. It therefore FREEZES the moment a
	// higher arm takes the channel — which is the whole point: the frozen value is where this
	// clip's timeline was last dispatched from.
	Armed(Live).AnchorCycle = BasePhase.Cycle;
}

void UElysiumBipedAnimInstance::RefreshLocomotionArm(const FAnimNode_BlendStack* Stack)
{
	FElysiumArmedClip& Arm = Armed(EElysiumBasePhaseSource::Locomotion);
	// The identity gate, then the length — the same order and for the same reason as
	// `LiveBaseSource`: a stack still standing on the clip a newer request replaced would arm this
	// record with the wrong asset's length and clock the wrong timeline against it.
	const bool bStackPlaysRequest = Stack != nullptr && RequestedAsset != nullptr
		&& Stack->GetAnimAsset() == RequestedAsset;
	const float Length = bStackPlaysRequest ? Stack->GetCurrentAssetLength() : 0.0f;
	// The identity is the APPLIED record's, because that record is what the stack is posing. The
	// LABEL and never the animation name: a fan is one sequence descriptor carrying N animations, and
	// its event timeline is filed under the sequence label like every other clip's — 141 of the
	// shipped labels are a grid and a timeline at once, and all of them are keyed that way.
	const FElysiumClipIdentity Identity(Applied.OwnerStem, Applied.SequenceLabel);
	if (Length <= 0.0f || !Identity.IsValid())
	{
		Arm = FElysiumArmedClip();
		return;
	}

	const bool bSameClip = Arm.IsArmed()
		&& Arm.Identity.OwnerStem.Equals(Identity.OwnerStem, ESearchCase::IgnoreCase)
		&& Arm.Identity.Label.Equals(Identity.Label, ESearchCase::IgnoreCase);
	if (!bSameClip)
	{
		Arm.Identity = Identity;
		Arm.PlayId = ++NextPlayId;
		// **It anchors where it is FOUND, never at zero.** Nothing starts the stack's clip in a way
		// this class can observe: by the time a record naming it is applied the node has already been
		// advancing it, and the transition into it is a blend nothing here clocks. Anchoring at zero
		// would fire every record below the current fraction in one burst — on every one-shot that
		// ends over a moving body, and on every re-arm that lands mid-clip. 217 of the shipped
		// locomotion and idle labels carry a timeline, so that burst is not a corner.
		Arm.AnchorCycle = FMath::Clamp(
			Stack->GetCurrentAssetTimePlayRateAdjusted() / Length, 0.0f, 1.0f);
	}
	// **A generation bump that keeps the same clip is NOT a new play** — an equip, a holster, an
	// alert-state change mid-walk all republish the record without changing what is playing, and
	// retail reselects the same sequence without resetting its cycle. Bumping the id here would
	// restart the timeline and re-fire every footstep behind it.
	Arm.LengthSeconds = Length;
	Arm.bLooping = bRequestedLooping;
}

void UElysiumBipedAnimInstance::RefreshBasePhase(FElysiumBipedAnimProxy& InProxy,
	const FAnimNode_BlendStack* Stack)
{
	// **Freshness, and it differs by arm.** `UAnimInstance::UpdateAnimation` services montages before
	// it calls `NativeUpdateAnimation`, so the montage's position and the reaction's own accumulator
	// are THIS frame's. The clip player and the blend stack are graph-side: both advance on the
	// worker, so both report the position last frame's update settled. Every one of them is monotonic
	// within a play, so the dispatcher's half-open interval rule fires each record exactly once
	// either way — a frame of lag moves WHEN a footstep lands by one frame, it does not drop or
	// double one.

	// The locomotion arm maintains itself, live or not: it is a per-frame projection rather than a
	// discrete play, so no seam ever calls for it — and an arm that only existed while it was on top
	// could not resume when the montage over it ended.
	RefreshLocomotionArm(Stack);
	PublishBasePhase(InProxy, Stack, /*bReadClocks=*/true);
}

// ================================================================================================
// The overlay slot's phase clock
//
// The slot's clip is not clocked by any node: `FAnimNode_SequenceEvaluator` is PINNED, and the
// number that moves it is the claim's own phase (`ElysiumAnimGraph::SlotEvaluatorTime` over
// `SlotCycle`). So this record is read off the claim rather than off the graph, and it is the same
// one clock the envelope and the expiry ride — a timeline walked against any other number would
// dispatch a shot's commit id at an instant the pose never reaches.
// ================================================================================================

void UElysiumBipedAnimInstance::ArmSlotPhase(const FElysiumClipIdentity& Identity,
	float LengthSeconds, bool bLoop, float PlayRate, float Cycle)
{
	if (!Identity.IsValid())
	{
		// A layer nobody named. Ordinary rather than a failure — the layer lab and a hand-built stand
		// both arm one — and it means this channel carries no timeline to walk, not that the pose is
		// wrong.
		SlotArm = FElysiumArmedClip();
		PublishSlotPhase(0.0f);
		return;
	}

	SlotArm = FElysiumArmedClip();
	SlotArm.Identity = Identity;
	SlotArm.LengthSeconds = FMath::Max(0.0f, LengthSeconds);
	SlotArm.PlayRate = PlayRate;
	SlotArm.bLooping = bLoop;
	// **Every arm is a genuine (re)start, and the id is what says so.** A claim reaching this seam is
	// a claim the driver has just granted, so two shots of one weapon are two plays of one clip:
	// owner, label and phase are identical between them, and only this tells the cursor that the
	// second is a new play rather than a lap.
	SlotArm.PlayId = ++NextPlayId;
	SlotArm.AnchorCycle = Cycle;
	PublishSlotPhase(Cycle);
}

void UElysiumBipedAnimInstance::RefreshSlotPhase(const FElysiumAnimationSelection& Selection,
	float Cycle)
{
	// The record has stopped naming a layer, so the claim behind this arm has expired or been given
	// back. The timeline goes with it rather than freezing at its last cycle.
	//
	// A record naming a DIFFERENT label than the arm is the same answer: this instance can only
	// address the timeline of a layer it armed itself, and a claim that never reached `PlaySlotLayer`
	// is one whose asset failed to load or carried no bone mask — both already reported by
	// `UElysiumAnimSubsystem::ResolveSlotLayer` and by the arm seam, where the cause is known.
	if (Selection.SlotLabel.IsEmpty() || !SlotArm.IsArmed()
		|| !SlotArm.Identity.Label.Equals(Selection.SlotLabel, ESearchCase::IgnoreCase))
	{
		SlotArm = FElysiumArmedClip();
		PublishSlotPhase(0.0f);
		return;
	}
	PublishSlotPhase(Cycle);
}

void UElysiumBipedAnimInstance::PublishSlotPhase(float Cycle)
{
	if (!SlotArm.IsArmed())
	{
		SlotPhase = FElysiumClipPhase();
		return;
	}

	SlotPhase = FElysiumClipPhase();
	SlotPhase.OwnerStem = SlotArm.Identity.OwnerStem;
	SlotPhase.Label = SlotArm.Identity.Label;
	SlotPhase.Length = SlotArm.LengthSeconds;
	SlotPhase.PlayRate = SlotArm.PlayRate;
	SlotPhase.bLooping = SlotArm.bLooping;
	SlotPhase.PlayId = SlotArm.PlayId;
	// Where the dispatcher last left this layer, carried as it stood before this frame's advance —
	// the same half-open interval rule the base channel walks, so a record between the two cycles
	// fires exactly once.
	SlotPhase.AnchorCycle = SlotArm.AnchorCycle;
	SlotPhase.Channel = EElysiumAnimChannel::UpperBody;
	SlotPhase.Cycle = Cycle;
	SlotArm.AnchorCycle = SlotPhase.Cycle;
}

bool UElysiumBipedAnimInstance::GetClipPhase(EElysiumAnimChannel Channel,
	FElysiumClipPhase& Out) const
{
	Out = FElysiumClipPhase();
	// Two channels, two records, and the same contract on both: a channel standing on nothing answers
	// false and leaves the caller a default-constructed record, which is an absence rather than a
	// fault. Every other channel is that absence permanently — nothing publishes a phase for it.
	switch (Channel)
	{
	case EElysiumAnimChannel::Base:
		if (!BasePhase.IsValid())
		{
			return false;
		}
		Out = BasePhase;
		return true;
	case EElysiumAnimChannel::UpperBody:
		if (!SlotPhase.IsValid())
		{
			return false;
		}
		Out = SlotPhase;
		return true;
	default:
		return false;
	}
}

