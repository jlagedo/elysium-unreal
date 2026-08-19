#include "Substrate/ElysiumProp.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Math/BoxSphereBounds.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogElysiumProp);

// A/B toggle for the dynamic-prop bodies (mirrors elysium.NpcBodies). Read in the leaf's Spawn, so it
// takes effect on the next map load: 1 stands the meshes, 0 leaves the props bodiless records (their
// I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarPropBodies(
	TEXT("elysium.PropBodies"),
	1,
	TEXT("Stand dynamic-prop static-mesh bodies at their placements at map load (1, default) or skip them (0)."),
	ECVF_Default);

bool ElysiumPropBodiesEnabled()
{
	return CVarPropBodies.GetValueOnGameThread() != 0;
}

void FElysiumProp::Spawn()
{
	BuildBody(/*bFromSetModel=*/false);
	StandRestPose();
	// CDynamicProp::Spawn (FUN_101905e0) arms the animate think only for a random animator.
	// Every other prop spawns thinking never; SetAnimation is what arms it later.
	if (bRandomAnimator && World)
	{
		NextRandAnim = World->NowSeconds() + DrawRandomAnimInterval();
		NextThink = static_cast<float>(NextRandAnim + AnimThinkInterval);
	}
}

bool FElysiumProp::PreloadAnimClip(const FString& ClipName)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	bool bLoops = false;
	return Embodiment && AnimatedVisual && !AnimatedStem.IsEmpty()
		&& Embodiment->FindAnimatedPropClip(AnimatedStem, ClipName, bLoops)
		&& Embodiment->PreloadAnimatedPropClips(AnimatedVisual, AnimatedStem) > 0;
}

void FElysiumProp::PreloadForActivation()
{
	// Unlike an NPC, a skeletal prop owns a compact, self-contained clip list. Load the whole
	// represented model now: SetAnimation is also invoked from Python, whose dynamic string cannot
	// be recovered by walking entity outputs alone.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		if (AnimatedVisual && !AnimatedStem.IsEmpty())
		{
			Embodiment->PreloadAnimatedPropClips(AnimatedVisual, AnimatedStem);
		}
	}
}

// CDynamicProp::Activate (FUN_101906c0). `LoopSequence` resolves HERE, not in Spawn: when it
// names a real sequence — including sequence index 0, the compare is against -1 — the prop arms
// a one-shot think at `curtime + RandomFloat(0.1, 0.99)`. That stagger is why three palm trees
// do not sway in lockstep. The think it arms (FUN_10190750) assigns the sequence, calls
// ResetSequenceInfo (which lifts the play rate off zero), fires OnAnimationBegun and hands over
// to the 10 Hz animate think.
void FElysiumProp::Activate()
{
	bLoopSequenceResolved = false;
	bLoopStartPending = false;
	if (!AnimatedVisual || !World || !MeaningfulSequence(LoopSequence))
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment || AnimatedStem.IsEmpty())
	{
		return;
	}
	// Resolution is by name here rather than by index: the runtime addresses clips by label.
	bool bLoops = false;
	if (!Embodiment->FindAnimatedPropClip(AnimatedStem, LoopSequence, bLoops))
	{
		return;
	}
	bLoopSequenceResolved = true;
	bLoopStartPending = true;
	// NowSeconds() at Activate equals the activation transaction's Now: the game clock only
	// advances from AElysiumMapActor::PreMoveTick, which is gated on RuntimePhase == Active and
	// so has not ticked yet. The stagger therefore survives the load gap intact. If that gate
	// ever moves, every prop's start collapses onto the first frame.
	NextThink = static_cast<float>(World->NowSeconds() + DrawLoopStartDelay());
}

// CDynamicPropAnimThink (FUN_10190850): drive the random animator and report a finished clip.
// Retail leaves the think disarmed once a non-looping sequence has finished and there is no
// random animator left to schedule — and because that disarm is permanent, the revert-to-
// `LoopSequence` branch at the top of the retail think is **unreachable in shipped data**
// (`m_bRandomAnimator` is zero on all 749 entities carrying the key). A finished one-shot holds
// its final frame; it does not return to the authored loop, and there is no rest-pose fallback.
void FElysiumProp::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// The Activate-phase one-shot (FUN_10190750), folded in as a flag rather than a second
	// think function: this think already owns the random animator's schedule, and a second
	// state would have to interleave with it.
	if (bLoopStartPending)
	{
		bLoopStartPending = false;
		if (StartLoopSequence())
		{
			static const FName OnAnimationBegun(TEXT("OnAnimationBegun"));
			FireOutput(OnAnimationBegun, Handle);
		}
		NextThink = static_cast<float>(Now + AnimThinkInterval);
		return;
	}

	if (bRandomAnimator && Now > NextRandAnim && PlayRandomAnimation())
	{
		static const FName OnAnimationBegun(TEXT("OnAnimationBegun"));
		FireOutput(OnAnimationBegun, Handle);
		NextRandAnim = Now + DrawRandomAnimInterval();
	}

	// After the two start branches, so a clip that started on this very think is not measured
	// against a stamp it has not run under yet; before the finish check, so the last correction
	// lands on the frame the clip completes rather than after the think has disarmed.
	ResyncAnimation(Now);

	if (SequenceFinished(Now))
	{
		static const FName OnAnimationDone(TEXT("OnAnimationDone"));
		FireOutput(OnAnimationDone, Handle);
		if (!bRandomAnimator)
		{
			return;   // nothing left to schedule; retail returns without re-arming
		}
		NextThink = static_cast<float>(NextRandAnim + AnimThinkInterval);
		return;
	}
	NextThink = static_cast<float>(Now + AnimThinkInterval);
}

UPrimitiveComponent* FElysiumProp::GetAttachBody() const
{
	return Visual ? static_cast<UPrimitiveComponent*>(Visual)
		: static_cast<UPrimitiveComponent*>(AnimatedVisual);
}

void FElysiumProp::Serialize(FElysiumSaveArchive& Ar)
{
	// m_flNextRandAnim is a retail SAVE field. AnimationEndTime is not carried: the replay below
	// restarts the clip, which is what re-derives it. `bRestPoseHeld` has to be, though — the
	// replay would otherwise start a resting prop's clip *running* instead of holding frame 0.
	Ar << CurrentAnimation << bAnimationLoop << bRestPoseHeld << NextRandAnim;
	if (Ar.IsLoading() && AnimatedVisual && !CurrentAnimation.IsEmpty())
	{
		const bool bWasHeld = bRestPoseHeld;
		PlayAnimation(CurrentAnimation, bAnimationLoop);   // clears bRestPoseHeld
		if (bWasHeld && World)
		{
			if (IElysiumEmbodiment* Embodiment = World->Embodiment())
			{
				Embodiment->SeekCinematicClip(AnimatedVisual, 0.0f);
				AnimationEndTime = 0.0;
				bRestPoseHeld = true;
			}
		}
	}
}

void FElysiumProp::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateVisual();
}

void FElysiumProp::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	// One basis for both representations (see BuildBody).
	const FQuat Rot(ElysiumSkeletalBasis::FromSourceAngles(Angles));
	if (Visual)
	{
		Visual->SetWorldLocationAndRotation(Origin, Rot);
	}
	if (AnimatedVisual)
	{
		AnimatedVisual->SetWorldLocationAndRotation(Origin, Rot);
	}
}

void FElysiumProp::OnRuntimeModelChanged()
{
	DestroyBody();
	BuildBody(/*bFromSetModel=*/true);
	StandRestPose();
	// A SetModel arrives long after the Activate phase, so there is no stagger to wait out —
	// if the authored loop resolves on the new model it starts now.
	bLoopSequenceResolved = MeaningfulSequence(LoopSequence) && StartLoopSequence();
}

void FElysiumProp::InputBreak(const FElysiumInputArgs& Args)
{
	if (bBroken)
	{
		return;
	}
	bBroken = true;
	GateVisual();
	static const FName OnBreak(TEXT("OnBreak"));
	FireOutput(OnBreak, Args.Activator);
	UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
}

void FElysiumProp::InputSkin(const FElysiumInputArgs& Args)
{
	SetSkin(Args.Param.ToInt());
}

void FElysiumProp::SetSkin(int32 Family)
{
	Skin = Family;
	ApplySkin();
}

void FElysiumProp::ApplySkin()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || VisualStem.IsEmpty())
	{
		return;
	}
	if (AnimatedVisual)
	{
		Embodiment->ApplyAnimatedPropSkin(AnimatedVisual, VisualStem, Skin);
	}
	else if (Visual)
	{
		Embodiment->ApplyPropSkin(Visual, VisualStem, Skin);
	}
}

void FElysiumProp::InputSetAnimation(const FElysiumInputArgs& Args)
{
	const FString Clip = Args.Param.ToString();
	// InputSetAnimation does not force one shot: ResetSequenceInfo derives m_bSequenceLoops
	// from the model's own STUDIO_LOOPING bit, so the clip decides. Every clip the exported
	// maps name here is non-looping; a script naming a looping one gets a loop, as in retail.
	bool bLoops = false;
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops);
	}
	if (!PlayAnimation(Clip, bLoops))
	{
		UE_LOG(LogElysiumProp, Warning, TEXT("%s SetAnimation '%s' did not resolve on %s"),
			*DebugString(), *Clip, AnimatedStem.IsEmpty() ? TEXT("static representation") : *AnimatedStem);
		return;
	}
	// InputSetAnimation (FUN_10190a00) re-arms the animate think, which is what carries the clip
	// to its end and fires OnAnimationDone before disarming again.
	if (World)
	{
		NextThink = static_cast<float>(World->NowSeconds() + AnimThinkInterval);
	}
}

void FElysiumProp::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), AnimatedVisual ? TEXT("skeletal animated prop")
		: (Visual ? TEXT("static mesh") : TEXT("(none)")));
	Out.Emplace(TEXT("Animation"), CurrentAnimation.IsEmpty() ? TEXT("(none)")
		: (bRestPoseHeld ? CurrentAnimation + TEXT(" (rest pose, held at frame 0)")
			: CurrentAnimation));
	Out.Emplace(TEXT("Animation loop"), bAnimationLoop ? TEXT("yes") : TEXT("no"));
	// Where the clip SHOULD be by the substrate clock, and how hard the think has had to work to
	// keep it there. A misplaced prop reading `0 resync(s)` is in phase and wrongly anchored —
	// a different fault from the one this row exists to catch.
	if (!CurrentAnimation.IsEmpty() && !bRestPoseHeld)
	{
		Out.Emplace(TEXT("Clip phase"), FString::Printf(TEXT("%.3fs, %d resync(s), last drift %.3fs"),
			World ? World->NowSeconds() - AnimStartTime : 0.0, ResyncCount, LastResyncDrift));
	}
	// A prop animated in place is culled on its bounds, not on where its bones drew it, so a
	// prop that vanishes mid-scene is asking whether the bind pose was widened to the clip's
	// reach. Equal numbers mean it was not — the index carried no radius for this model.
	if (const USkeletalMesh* PropMesh = AnimatedVisual ? AnimatedVisual->GetSkeletalMeshAsset() : nullptr)
	{
		Out.Emplace(TEXT("Bounds radius"), FString::Printf(TEXT("%.0f cm bind -> %.0f cm drawn"),
			PropMesh->GetImportedBounds().SphereRadius, PropMesh->GetBounds().SphereRadius));
	}
	Out.Emplace(TEXT("Loop sequence"), LoopSequence.IsEmpty() ? TEXT("(none)")
		: (bLoopSequenceResolved ? LoopSequence : LoopSequence + TEXT(" (unresolved)")));
	if (bRandomAnimator)
	{
		Out.Emplace(TEXT("Random animator"),
			FString::Printf(TEXT("%.2f-%.2fs (not implemented)"), MinAnimTime, MaxAnimTime));
	}
	// Authored but ignored — `demo_sequence` is a Hammer/FGD field with no engine keyfield behind
	// it, so surface it rather than let the divergence disappear silently.
	const FString Demo = Def ? Def->Keys.FindRef(TEXT("demo_sequence")) : FString();
	if (MeaningfulSequence(Demo))
	{
		Out.Emplace(TEXT("demo_sequence"), Demo + TEXT(" (editor-only, ignored)"));
	}
	Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Skin"), FString::FromInt(Skin));
}

bool FElysiumProp::MeaningfulSequence(const FString& Sequence)
{
	return !Sequence.IsEmpty()
		&& !Sequence.Equals(TEXT("none"), ESearchCase::IgnoreCase)
		&& !Sequence.Equals(TEXT("null"), ESearchCase::IgnoreCase)
		&& Sequence != TEXT("0");
}

// Retail's m_bSequenceFinished (+0x65c) for the cases the think cares about: a looping clip never
// reports finished, and a one-shot is finished once its authored length has elapsed.
bool FElysiumProp::SequenceFinished(double Now) const
{
	return !bAnimationLoop && AnimationEndTime > 0.0 && Now >= AnimationEndTime;
}

// The gap until the next random draw. Cosmetic, so it deliberately does not come from a named
// save-carried ElysiumRng stream.
double FElysiumProp::DrawRandomAnimInterval() const
{
	return FMath::FRandRange(FMath::Min(MinAnimTime, MaxAnimTime),
		FMath::Max(MinAnimTime, MaxAnimTime));
}

// CDynamicProp::Activate's `RandomFloat(0.1, 0.99)` start stagger. `FMath` for the same reason
// the interval above uses it: the draw is sub-second, fires once per map load, and no save can
// observe it — a restore re-runs Activate and re-draws.
double FElysiumProp::DrawLoopStartDelay()
{
	return FMath::FRandRange(0.1f, 0.99f);
}

// Retail re-picks here with the *same* call the spawn path uses — SelectWeightedSequence
// (ACT_IDLE, -1), FUN_1008dc40 — not an arbitrary sequence. `RandomAnimation` is 0 on all 749
// entities carrying it across the exported corpus, so this is unreachable in shipped data; it
// is written out rather than stubbed because the seam it needs now exists, and no exported prop
// model carries more than one ACT_IDLE clip, so the pick cannot actually vary.
bool FElysiumProp::PlayRandomAnimation()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || AnimatedStem.IsEmpty())
	{
		return false;
	}
	const FString Clip = Embodiment->AnimatedPropRestClip(AnimatedStem, Handle.Index);
	bool bLoops = false;
	Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops);
	return PlayAnimation(Clip, bLoops);
}

void FElysiumProp::DestroyBody()
{
	if (Visual) { Visual->DestroyComponent(); Visual = nullptr; }
	if (AnimatedVisual) { AnimatedVisual->DestroyComponent(); AnimatedVisual = nullptr; }
	// CollisionProxy is AnimatedVisual's parent on the catalogue path (BuildPlacedModelBody
	// attaches the drawn mesh to it), so it survives the child's destruction and must be torn
	// down explicitly rather than assumed gone.
	if (CollisionProxy) { CollisionProxy->DestroyComponent(); CollisionProxy = nullptr; }
	if (BoxCollisionProxy) { BoxCollisionProxy->DestroyComponent(); BoxCollisionProxy = nullptr; }
	SolidValue = 0;
	AnimatedStem.Reset();
	VisualStem.Reset();
	CurrentAnimation.Reset();
	bAnimationLoop = false;
	bLoopSequenceResolved = false;
	bRestPoseHeld = false;
	bLoopStartPending = false;
	AnimationEndTime = 0.0;
	AnimStartTime = 0.0;
	AnimLengthSeconds = 0.0;
	ResyncCount = 0;
	LastResyncDrift = 0.0;
}

bool FElysiumProp::PlayAnimation(const FString& Clip, bool bLoop)
{
	if (!MeaningfulSequence(Clip) || !AnimatedVisual || !World)
	{
		return false;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	float Seconds = 0.0f;
	if (!Embodiment || !Embodiment->PlayAnimatedPropClip(
		AnimatedVisual, AnimatedStem, Clip, bLoop, &Seconds))
	{
		return false;
	}
	CurrentAnimation = Clip;
	bAnimationLoop = bLoop;
	bRestPoseHeld = false;   // playing anything releases the held pose
	// Frame 0 is now, which is retail stamping m_flAnimTime at the first StudioFrameAdvance.
	// The length is kept for loops as well, so the phase wraps at the source; AnimationEndTime
	// deliberately stays 0 for a loop, since a looping clip never reports finished.
	AnimStartTime = World->NowSeconds();
	AnimLengthSeconds = Seconds > 0.0f ? static_cast<double>(Seconds) : 0.0;
	AnimationEndTime = (!bLoop && Seconds > 0.0f) ? AnimStartTime + Seconds : 0.0;
	return true;
}

// Retail's cycle is not accumulated — StudioFrameAdvance (FUN_1008f120) recomputes it from
// `curtime - m_flAnimTime` on every call and leaves the stamp at curtime, so the interval
// telescopes and the total advance is exactly elapsed game time no matter how many calls there
// were or how long each frame was. That is why a prop thinking at 10 Hz stays in lockstep with a
// choreo actor seeked every frame.
//
// Unreal's sequence player accumulates instead, off the engine's animation delta. With the
// frame filters unified (FElysiumTimeControl::ApplyToWorld) the two agree frame for frame, so
// this corrects only the residue — a clip that missed ticks while hidden, float accumulation, a
// body that started a frame early. It is also the measurement: if a prop is visibly wrong with
// ResyncCount at zero, its phase is right and its clip origin is not.
void FElysiumProp::ResyncAnimation(double Now)
{
	if (!AnimatedVisual || bRestPoseHeld || CurrentAnimation.IsEmpty() || !World)
	{
		return;   // a held pose is rate 0 by design and must never be re-phased
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	float Position = 0.0f;
	if (!Embodiment || !Embodiment->GetCinematicClipPosition(AnimatedVisual, Position))
	{
		return;   // no anim host, or nothing playing — an ordinary answer
	}

	const bool bWraps = bAnimationLoop && AnimLengthSeconds > 0.0;
	double Phase = Now - AnimStartTime;
	if (bWraps)
	{
		Phase = FMath::Fmod(Phase, AnimLengthSeconds);
	}

	// Circular distance on a loop: at length 4, a position of 3.99 and a phase of 0.01 are
	// 0.02 apart across the seam, not 3.98. A plain difference would resync every think there.
	double Drift = Phase - static_cast<double>(Position);
	if (bWraps)
	{
		Drift = FMath::Fmod(Drift + 1.5 * AnimLengthSeconds, AnimLengthSeconds)
			- 0.5 * AnimLengthSeconds;
	}
	if (FMath::Abs(Drift) <= AnimResyncTolerance)
	{
		return;
	}
	if (Embodiment->ResyncCinematicClip(AnimatedVisual, static_cast<float>(Phase)))
	{
		++ResyncCount;
		LastResyncDrift = Drift;
	}
}

// CBaseProp::Spawn (FUN_1018df70). A prop at rest is a **held pose, not a playing clip**: the
// cycle and the playback rate are both left at zero, and only ResetSequenceInfo — which nothing
// calls for a prop with no `LoopSequence` and no scripted SetAnimation — ever lifts the rate.
// The sequence is SelectWeightedSequence(ACT_IDLE, -1) falling back to sequence index 0, which
// is the branch that fires for the theatre's cinematic props: they tag `ACT_VM_IDLE`, not
// `ACT_IDLE`. `demo_sequence` names the same clip but is not an engine keyfield, so it stays
// unread — the activity/index rule reaches the same pose by the faithful route.
bool FElysiumProp::StandRestPose()
{
	bRestPoseHeld = false;
	if (!AnimatedVisual || !World)
	{
		return false;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return false;
	}
	// bLoop MUST stay false. The anim proxy's Request early-outs on
	// (Sequence == Playing && bLoop && bPlayingLoop) *without* restoring the play rate, so a
	// hold created as a loop would latch a later request out of ever un-freezing — and
	// `palmtree`'s rest clip and its `LoopSequence` are the same clip, so that path is live.
	if (!PlayAnimation(Embodiment->AnimatedPropRestClip(AnimatedStem, Handle.Index), /*bLoop*/ false))
	{
		return false;
	}
	Embodiment->SeekCinematicClip(AnimatedVisual, 0.0f);   // start position 0, play rate 0
	AnimatedVisual->TickAnimation(0.0f, false);
	AnimatedVisual->RefreshBoneTransforms();
	AnimatedVisual->SetComponentTickEnabled(false);
	AnimationEndTime = 0.0;   // a held pose never finishes, so it must not fire OnAnimationDone
	bRestPoseHeld = true;
	return true;
}

// The Activate-phase loop start: assign the authored sequence and let it run.
bool FElysiumProp::StartLoopSequence()
{
	bool bLoops = false;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment)
	{
		Embodiment->FindAnimatedPropClip(AnimatedStem, LoopSequence, bLoops);
	}
	return PlayAnimation(LoopSequence, bLoops);
}

void FElysiumProp::BuildBody(bool bFromSetModel)
{
	if (!ElysiumPropBodiesEnabled() || !World || !Def || Model.IsEmpty())
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;
	}

	// solid (SolidType_t) and disableshadows are CDynamicProp-scoped CBaseEntity
	// keyfields, read once here rather than through the class-field table: neither is mutated by
	// a runtime input, so there is nothing for a live field to back. "" -> Atoi -> 0 matches
	// SOLID_NONE's already-correct no-collision default when the key is absent
	// (docs/vtmb/phy_vphysics.md).
	const int32 Solid = FCString::Atoi(*Def->Keys.FindRef(TEXT("solid")));
	const bool bDisableShadows = FCString::Atoi(*Def->Keys.FindRef(TEXT("disableshadows"))) != 0;
	SolidValue = Solid;

	// Both representations share one rotation. A prop's skeletal body is baked from its `.eskm`
	// in the repo's canonical Source->Unreal frame, exactly like its static mesh, so
	// `model_quat` — the placement of the exporter's Unreal-native OBJ — is the whole answer for
	// either, pitch and roll included (`ElysiumSkeletalBasis`).
	FVector Loc;
	FQuat StaticRot, SkeletalRot;
	if (bFromSetModel)
	{
		// `Def->ModelQuat` belongs to the model this one replaced, so it cannot be reused; both
		// representations fall back to the yaw-only runtime derivation.
		VisualStem = FPaths::GetBaseFilename(Model).ToLower();
		Loc = Origin;
		StaticRot = FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles));
		SkeletalRot = StaticRot;
	}
	else
	{
		VisualStem = Def->ModelMesh;
		Loc = Def->Origin;
		StaticRot = Def->ModelQuat;
		// A model that decoded no static geometry carries no `model_quat`, so falling back to
		// the identity default would silently drop the placement.
		SkeletalRot = Def->ModelMesh.IsEmpty()
			? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
			: Def->ModelQuat;
	}

	AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
	// An indexed model that bakes no playable clip is not an animated representation — it is a
	// bind-pose skeleton standing where the baked static mesh should be. RestClip is empty
	// exactly when the entry carries no clips, so it is the same test.
	if (!AnimatedStem.IsEmpty()
		&& Embodiment->AnimatedPropRestClip(AnimatedStem, Handle.Index).IsEmpty())
	{
		UE_LOG(LogElysiumProp, Verbose,
			TEXT("%s: '%s' is indexed as animated but bakes no clip; standing the static mesh"),
			*DebugString(), *AnimatedStem);
		AnimatedStem.Reset();
	}
	if (Embodiment->HasPlacedModelCatalogue())
	{
		FElysiumPlacedModelRequest Request;
		Request.ModelPath = Model;
		Request.StaticStem = VisualStem;
		Request.Location = Loc;
		Request.Rotation = SkeletalRot;
		Request.UniformScale = Embodiment->BodyScaleFor(*Def);
		Request.PlacementToken = Handle.Index;
		Request.Skin = Skin;
		// VPhysicsInitStatic: solid 0 -> no collision, solid 2 -> a box (built separately below,
		// no existing builder bakes one), any other nonzero value -> the model's `.phy` hulls,
		// which CollisionProxy already builds (the same PhysicsActor-profile, non-simulating body
		// prop_physics's own static case uses).
		Request.Physics = (Solid != 0 && Solid != 2)
			? EElysiumPlacedModelPhysics::CollisionProxy
			: EElysiumPlacedModelPhysics::None;
		const FElysiumPlacedModelBody PlacedBody = Embodiment->BuildPlacedModelBody(Request);
		AnimatedVisual = PlacedBody.Visual;
		CollisionProxy = PlacedBody.PhysicsProxy;
		if (AnimatedVisual)
		{
			World->RegisterNpcBody(AnimatedVisual);
			if (IsUsable() && Def && !Def->bSky)
			{
				World->RegisterUseAnchor(AnimatedVisual, Handle);
			}
			AnimatedVisual->SetCastShadow(!bDisableShadows);
			if (Solid == 2)
			{
				BuildBoxCollisionProxy(AnimatedVisual);
			}
		}
	}
	else if (!AnimatedStem.IsEmpty())
	{
		// v3-v6 developer exports predate complete placed-model coverage. Preserve their former
		// animated-prop path; v7 never reaches this branch and therefore cannot display a fallback.
		AnimatedVisual = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Loc, SkeletalRot,
			Embodiment->BodyScaleFor(*Def), Handle.Index);
		if (AnimatedVisual)
		{
			World->RegisterNpcBody(AnimatedVisual);
			if (IsUsable() && Def && !Def->bSky)
			{
				World->RegisterUseAnchor(AnimatedVisual, Handle);
			}
		}
	}
	else if (!VisualStem.IsEmpty())
	{
		Visual = Embodiment->BuildPropVisual(VisualStem, Loc, StaticRot, Embodiment->BodyScaleFor(*Def));
		if (Visual)
		{
			World->RegisterPropBody(Visual,
				IsUsable() && Def && !Def->bSky ? Handle : FElysiumEntityHandle::Invalid());
			Visual->SetCastShadow(!bDisableShadows);
			// Set explicitly rather than trusting BuildPropVisual's own NoCollision default: solid
			// is this leaf's rule, not the builder's, and a self-determined result is what the
			// solid 0/absent case (and BoxCollisionProxy's solid 2 case, which leaves Visual itself
			// non-colliding) both need.
			if (Solid != 0 && Solid != 2)
			{
				Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
			}
			else
			{
				Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (Solid == 2)
				{
					BuildBoxCollisionProxy(Visual);
				}
			}
		}
	}

	if (Visual || AnimatedVisual)
	{
		if (Skin != 0) { ApplySkin(); }
		if (IsInert() || bBroken) { GateVisual(); }
	}
}

// solid 2 (SOLID_BBOX): retail derives a static box straight from the model's mins/maxs
// (PhysModelCreateBox) rather than parsing a `.phy`. Neither builder bakes a box shape onto the
// mesh asset, so this is the one solid value with no existing collision to switch on — attach a
// sibling box sized to the mesh's own local bounds instead, the same AABB-in-model-space retail
// derives.
void FElysiumProp::BuildBoxCollisionProxy(UPrimitiveComponent* Mesh)
{
	AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	const FBoxSphereBounds LocalBounds = Mesh->CalcBounds(FTransform::Identity);
	UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
	Box->SetBoxExtent(LocalBounds.BoxExtent);
	Box->SetCollisionProfileName(TEXT("PhysicsActor"));
	Box->SetupAttachment(Mesh);
	Box->SetRelativeLocation(LocalBounds.Origin);
	Box->RegisterComponent();
	Owner->AddInstanceComponent(Box);
	BoxCollisionProxy = Box;
}

void FElysiumProp::GateVisual()
{
	const bool bShown = !IsInert() && !bBroken;
	if (World && IsUsable())
	{
		World->SetUseAnchorEnabled(Handle, bShown);
	}
	if (Visual)
	{
		Visual->SetVisibility(bShown);
		// Only the fallback path's solid-driven Visual owns collision worth regating; solid 0
		// left it NoCollision at BuildBody and must stay that way rather than being turned solid
		// here.
		if (SolidValue != 0 && SolidValue != 2)
		{
			Visual->SetCollisionProfileName(bShown ? TEXT("PhysicsActor") : TEXT("NoCollision"));
		}
	}
	if (AnimatedVisual)
	{
		const bool bWasTicking = AnimatedVisual->IsComponentTickEnabled();
		AnimatedVisual->SetVisibility(bShown);
		AnimatedVisual->SetComponentTickEnabled(bShown && !bRestPoseHeld);
		// A hidden body does not tick, so its clip stops advancing while the game clock does
		// not. On the way back the 10 Hz think would correct it within 0.1 s; doing it on the
		// edge makes the prop right on its FIRST visible frame instead of a tenth of a second
		// into being looked at. ResyncAnimation owns the rest of the preconditions — BuildBody
		// reaches here before any clip exists, and a held rest pose must stay held.
		if (bShown && !bWasTicking && World)
		{
			ResyncAnimation(World->NowSeconds());
		}
	}
	// The catalogue path's static solid body and the solid==2 box proxy (either path) gate the
	// same way FElysiumPhysProp::GateBody gates its own static case: NoCollision when down,
	// restored PhysicsActor collision when live again. Neither ever simulates, so there is no
	// SetSimulatePhysics call to mirror.
	if (CollisionProxy)
	{
		CollisionProxy->SetCollisionEnabled(bShown ? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	}
	if (BoxCollisionProxy)
	{
		BoxCollisionProxy->SetCollisionEnabled(bShown ? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	}
}
