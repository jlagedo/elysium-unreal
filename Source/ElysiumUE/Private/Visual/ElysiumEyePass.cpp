#include "Visual/ElysiumEyePass.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumEyeTuningConfig.h"
#include "Visual/ElysiumBodyAnimInstance.h"
#include "Visual/ElysiumEntityBodiesLog.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "AnimationRuntime.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

// An unambiguous visual check on the eye basis before the gaze cascade exists. Aiming every
// eye at a target that moves is the only way to tell a correct basis from one that merely looks
// plausible while parked on the record's authored resting aim.
static TAutoConsoleVariable<int32> CVarEyeTrackPlayer(
	TEXT("elysium.EyeTrackPlayer"), 0,
	TEXT("Aim every NPC's eyes at the player camera (1) instead of the eyeball record's authored resting aim (0, default)."),
	ECVF_Cheat);

bool FElysiumEyePass::SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget)
{
	if (Body == nullptr)
	{
		return false;
	}
	for (FElysiumEyeBinding& Binding : EyeBindings)
	{
		if (Binding.Comp.Get() == Body)
		{
			Binding.ViewTarget = WorldTarget;
			Binding.bHasViewTarget = true;
			return true;
		}
	}
	// Most of the cast authors no eyeball record, so this is the ordinary answer rather than an
	// error: the character still decides where it is looking, there is simply nothing to aim.
	return false;
}

bool FElysiumEyePass::GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
	FVector& OutForward) const
{
	if (Body == nullptr)
	{
		return false;
	}
	for (const FElysiumEyeBinding& Binding : EyeBindings)
	{
		if (Binding.Comp.Get() != Body || Binding.HeadBoneIndex == INDEX_NONE)
		{
			continue;
		}
		// Component-space bone transform lifted to world. Read here rather than in the substrate
		// because this is the settled post-move pose, and because a bone index only means anything
		// beside the component it was resolved against.
		const FTransform BoneToWorld =
			Body->GetBoneTransform(Binding.HeadBoneIndex, Body->GetComponentTransform());
		// `CalcLookData`'s position: the bind-time eye point carried by the live bone.
		OutPosition = Binding.bHasHeadLocalEye
			? BoneToWorld.TransformPosition(Binding.HeadLocalEye)
			: BoneToWorld.GetLocation();
		// The head bone's own X runs up the skull (a Bip01 bone points along its length), not out
		// of the face. Measuring the cone along it aimed every "straight ahead" at the crown, so the
		// autonomous scan rejected the player and the iris parked under the upper lid — blank eyes.
		// Retail's `CalcLookData`: the bind-time head-local forward rotated by the live bone. A
		// binding without one (no reference pose to read at build) falls back to the component's
		// facing, which is right until the head turns.
		OutForward = Binding.HeadLocalForward.IsNearlyZero()
			? Body->GetComponentTransform().GetRotation().GetForwardVector()
			: BoneToWorld.GetRotation().RotateVector(Binding.HeadLocalForward).GetSafeNormal();
		return true;
	}
	return false;
}

void FElysiumEyePass::UpdateDisposition(USkeletalMeshComponent* Body,
	const FString& Disposition, int32 DispositionLevel)
{
	if (FElysiumEyeBinding* Binding = EyeBindings.FindByPredicate(
		[Body](const FElysiumEyeBinding& Row) { return Row.Comp.Get() == Body; }))
	{
		Binding->Disposition = Disposition;
		Binding->DispositionLevel = FMath::Max(1, DispositionLevel);
		Binding->NextBlinkTime = 0.f;
	}
}

void FElysiumEyePass::InstallEyes(USkeletalMeshComponent* Comp,
	const TSharedPtr<const FElysiumEyeSet>& Set, const FString& Disposition)
{
	if (Comp == nullptr || !Set.IsValid())
	{
		return;
	}
	USkeletalMesh* Mesh = Comp->GetSkeletalMeshAsset();
	UMaterialInterface* Master = ElysiumNpcVisual::EyeMaster(Mesh);
	if (Master == nullptr || Mesh == nullptr)
	{
		return;
	}

	FElysiumEyeBinding Binding;
	Binding.Comp = Comp;
	Binding.Set = Set;
	Binding.Disposition = Disposition;

	// The head bone the gaze cone and the fidget grid are measured in. Resolved once, by name,
	// against this component's own skeleton: the exporter appends a synthetic root on models with
	// more than one parent-less bone, so the `.mdl`'s bone ordering is not the USkeleton's and an
	// index carried across from the sidecar would aim off the wrong bone. The eye records name the
	// bone they hang from, which for every rigged character is the head, so take it from there
	// rather than hardcoding a string.
	for (const FElysiumEyeball& Candidate : Set->Eyeballs)
	{
		if (Candidate.Bone.IsNone())
		{
			continue;
		}
		const int32 Index = Comp->GetBoneIndex(Candidate.Bone);
		if (Index != INDEX_NONE)
		{
			Binding.HeadBoneIndex = Index;
			break;
		}
	}
	if (Binding.HeadBoneIndex != INDEX_NONE)
	{
		// `SetModel`'s `VectorIRotate(model forward, head bind matrix)`: the model's own facing
		// taken into the head bone's frame through the reference pose. Read once here because the
		// reference skeleton is the mesh's, and the animated bone the pass reads is this
		// component's, so both halves are resolved against the same skeleton.
		//
		// Retail's model forward is the constant `(0,-1,0)` in Source model space. The imported
		// mesh's reference pose is NOT guaranteed to face the component's +X — the V2 corpus keeps
		// the model's authored axes in the bind pose and carries the turn to +X in the animation's
		// root — so the constant cannot be hardcoded in component space. The eyeball record says
		// which model axis the face is on: its authored resting aim, rotated by the bind bone,
		// lands within the QC's few degrees of eye angle of the model's facing axis, and snapping
		// it to that axis recovers retail's constant for this model. It also recovers
		// `CNPC_VTzimisce`'s override (`0x103b9060`, a quarter turn of the stored vector) without
		// a class case, because that model's eyes author the axis its face is actually on.
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const FTransform BindComponent = FAnimationRuntime::GetComponentSpaceTransform(
			Ref, Ref.GetRefBonePose(), Binding.HeadBoneIndex);
		const FQuat BindRot = BindComponent.GetRotation();
		FVector ModelForward = FVector::ForwardVector;
		for (const FElysiumEyeball& Eye : Set->Eyeballs)
		{
			if (Eye.Forward.IsNearlyZero())
			{
				continue;
			}
			// The record stores the aim negated, as `BuildState` reads it.
			const FVector Aim = BindRot.RotateVector(-Eye.Forward);
			const int32 Axis = FMath::Abs(Aim.X) >= FMath::Abs(Aim.Y)
				? (FMath::Abs(Aim.X) >= FMath::Abs(Aim.Z) ? 0 : 2)
				: (FMath::Abs(Aim.Y) >= FMath::Abs(Aim.Z) ? 1 : 2);
			ModelForward = FVector::ZeroVector;
			ModelForward[Axis] = FMath::Sign(Aim[Axis]);
			break;
		}
		Binding.HeadLocalForward = BindRot.UnrotateVector(ModelForward).GetSafeNormal();
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("eyes '%s': head bone %d, model forward %s, head-local forward %s"),
			*Set->Stem, Binding.HeadBoneIndex, *ModelForward.ToCompactString(),
			*Binding.HeadLocalForward.ToCompactString());
		// `VectorITransform(m_vecViewOffset, bind)`: the standing view offset sits on the
		// component's own vertical through the feet, which is where the entity's origin is.
		Binding.HeadLocalEye =
			BindComponent.InverseTransformPosition(FVector(0.f, 0.f, ElysiumMove::StandViewZ));
		Binding.bHasHeadLocalEye = true;
	}

	const TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		// Two independent tests, because either alone can be defeated. The base-material test
		// survives any change to the plugin's slot naming; the name test says *which* eye.
		UMaterialInterface* Existing = Comp->GetMaterial(Slot);
		const bool bIsEye = Existing != nullptr && Existing->GetBaseMaterial() == Master;
		if (!bIsEye)
		{
			continue;
		}
		// Both slot spellings reduce to the material name the sidecar keys on, and the join is exact:
		// the baked slot is the container's own `Eyeball_r` against a sidecar that lowercases it, so a
		// suffix test misses by the separator it has no room for.
		const FString SlotName = Slots[Slot].MaterialSlotName.ToString();
		const FElysiumEyeball* Eye = Set->FindByMaterial(
			ElysiumEyes::MaterialNameFromSlot(Slots[Slot].MaterialSlotName));
		Binding.SlotJoins.Emplace(SlotName, Eye != nullptr ? Eye->Index : INDEX_NONE);
		if (Eye == nullptr)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("eyes '%s': slot %d ('%s') draws the eye master but matches no record"),
				*Set->Stem, Slot, *SlotName);
			continue;
		}

		UMaterialInstanceDynamic* Mid = Comp->CreateDynamicMaterialInstance(Slot);
		if (Mid == nullptr)
		{
			continue;
		}
		// The native material instance already carries the imported $iris texture.
		Mid->SetScalarParameterValue(TEXT("Vampire"), Eye->bVampire ? 1.f : 0.f);

		FElysiumEyeSlot Bound;
		Bound.Mid = Mid;
		Bound.EyeIndex = Eye->Index;
		// Resolved once, by name: a synthetic skeleton root makes the .mdl's own bone index wrong
		// on some models, and the reference-skeleton index is what GetBoneTransform takes.
		Bound.BoneIndex = Comp->GetBoneIndex(Eye->Bone);
		if (Bound.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("eyes '%s': bone '%s' is not on this skeleton"),
				*Set->Stem, *Eye->Bone.ToString());
			continue;
		}
		FVector Zero = FVector::ZeroVector;
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisU"), FLinearColor(Zero), Bound.ParamIrisU);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisV"), FLinearColor(Zero), Bound.ParamIrisV);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisOrigin"), FLinearColor(Zero), Bound.ParamIrisOrigin);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("NormalOrigin"), FLinearColor(Zero), Bound.ParamNormalOrigin);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("EyeUpN"), FLinearColor(Zero), Bound.ParamEyeUp);
		Binding.Slots.Add(Bound);
	}

	// Registered on the presence of eye SECTIONS, not of bound slots: a body whose sections joined no
	// record still has to be findable, because it is drawing the eye master's default iris and nothing
	// else about it says so. Such a binding is skipped by the pass below.
	if (!Binding.SlotJoins.IsEmpty())
	{
		EyeBindings.Add(MoveTemp(Binding));
	}
}

bool FElysiumEyePass::DescribeEyes(const USkeletalMeshComponent* Comp,
	FElysiumEyeReadout& Out) const
{
	Out = FElysiumEyeReadout();
	if (Comp == nullptr)
	{
		return false;
	}
	const FElysiumEyeBinding* Binding = EyeBindings.FindByPredicate(
		[Comp](const FElysiumEyeBinding& B) { return B.Comp.Get() == Comp; });
	if (Binding == nullptr)
	{
		return false;
	}
	Out.bHasSet = Binding->Set.IsValid();
	Out.RecordCount = Out.bHasSet ? Binding->Set->Eyeballs.Num() : 0;
	Out.EyeSlotCount = Binding->SlotJoins.Num();
	Out.BoundCount = Binding->Slots.Num();
	Out.Slots = Binding->SlotJoins;
	Out.Blink = Binding->LastBlink;
	Out.bAiming = Binding->bLastAiming;
	return true;
}

void FElysiumEyePass::TickEyes(const UObject* Context, float)
{
	// Everything here reads this frame's settled component-space pose, which is why it runs in the
	// post-move pass rather than in the component's own tick: at TG_PrePhysics the transforms are
	// last frame's, and GetProxyOnGameThread would flush a live parallel evaluation.
	// Until a gaze cascade supplies a target, an eye sits on the record's own authored resting
	// aim — the state `bEyeMove` off produces, which is a real retail configuration but is NOT
	// guaranteed to point out of the face: it is whatever the model's QC authored.
	//
	// `elysium.EyeTrackPlayer` overrides that with the player's camera, which is the cheapest
	// unambiguous check that the basis math is right: if the irises converge on the camera as it
	// moves, the record, the import transform, the solve and the plane parameters are all correct.
	if (EyeBindings.IsEmpty())
	{
		return;
	}
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// The blink cadence is content, and it is per disposition: most rows sit at 2.5/6.0 s, `Anger`
	// blinks slowly and `Error` — the row a character falls to when its own disposition does not
	// resolve — blinks fast enough to read as a tell.
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UElysiumRulebookSubsystem* Rules = GI
		? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;

	// The green room's override, consumed once for the whole pass. `bBlinkNow` is an edge, so it is
	// cleared here rather than per body: one press is one blink on everything bound, not one per body.
	FElysiumEyeDebug& Debug = EyeDebugState;
	const bool bBlinkNow = Debug.bBlinkNow;
	Debug.bBlinkNow = false;

	// `elysium.EyeTrackPlayer` is the debug override, and it outranks the gaze cascade on purpose:
	// it is the cheapest unambiguous check that the basis math is right, and it has to keep working
	// when the cascade is the thing under suspicion. The green room's Camera mode aims at the same
	// place, so the two resolve one camera between them.
	const bool bTrackPlayer = CVarEyeTrackPlayer.GetValueOnGameThread() != 0;
	const bool bWantCamera = bTrackPlayer || Debug.Gaze == FElysiumEyeDebug::EGaze::Camera;
	FVector TrackWorld = FVector::ZeroVector;
	bool bHaveCamera = false;
	if (bWantCamera)
	{
		if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(Context, 0))
		{
			TrackWorld = Cam->GetCameraLocation();
			bHaveCamera = true;
		}
	}

	for (int32 i = EyeBindings.Num() - 1; i >= 0; --i)
	{
		FElysiumEyeBinding& Binding = EyeBindings[i];
		USkeletalMeshComponent* Comp = Binding.Comp.Get();
		if (Comp == nullptr || !Binding.Set.IsValid())
		{
			EyeBindings.RemoveAtSwap(i);
			continue;
		}
		// A binding whose sections joined no record is a diagnostic entry: there is no MID to write
		// and no aim to solve, and writing its blink would move lids the eye pass does not own.
		if (Binding.Slots.IsEmpty())
		{
			continue;
		}
		// Retail runs the eye pass per *drawn* model, so skipping an unseen body is faithful as
		// well as cheap.
		if (!Comp->WasRecentlyRendered(0.2f))
		{
			continue;
		}
		// Blink: schedule, then evaluate the envelope. The interval is content — retail reads it
		// from `vdata/system/dispositiontable.txt`, which the disposition table already carries.
		float BlinkMin = 2.5f;
		float BlinkMax = 6.f;
		if (Rules != nullptr)
		{
			if (const FElysiumDisposition* Row = Rules->Dispositions().Resolve(
				Binding.Disposition, Binding.DispositionLevel))
			{
				BlinkMin = Row->MinBlinkInterval;
				BlinkMax = Row->MaxBlinkInterval;
			}
		}
		FElysiumEyeInput EyeInput;
		if (bBlinkNow)
		{
			Binding.BlinkEndsAt = Now + ElysiumEyes::BlinkSeconds;
		}
		else if (Debug.bHoldBlink)
		{
			// Held open, and the schedule is held with it: releasing the hold should not fire every
			// blink the window was open for.
			Binding.BlinkEndsAt = 0.f;
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		else if (Binding.NextBlinkTime <= 0.f)
		{
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		else if (Now >= Binding.NextBlinkTime)
		{
			Binding.BlinkEndsAt = Now + ElysiumEyes::BlinkSeconds;
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		EyeInput.Blink = ElysiumEyes::BlinkWeight(Binding.BlinkEndsAt - Now);

		// Where this body is looking, in priority order: the green room's override, then the cvar,
		// then the gaze the substrate pushed for this character, then nothing. The override outranks
		// the cvar for the same reason the cvar outranks the cascade: it is the hand on the control,
		// and it has to win over whatever a session was left set to.
		//
		// `bEyeMove` is assigned on every branch INCLUDING the last, and the last is what makes the
		// fallback true. It defaults on, so leaving it alone with no gaze point does not rest the eye —
		// it aims at the target a zero vector names, which is the world origin. That reads as a whole
		// cast staring at one arbitrary point in the map and at nothing on a stage built far from it.
		// The Green Room's live nudge composes ON TOP of the corpus-wide baseline (R4.5,
		// `UElysiumEyeTuningConfig`, `/ElysiumAuthored/Eyes/DA_EyeTuning`, `ElysiumEyes::ComposeTuning`)
		// — an untouched debug state and an untouched (or absent) asset both leave every eye exactly
		// where it was before this asset existed.
		FElysiumEyeTuning Tuning = ElysiumEyes::ComposeTuning(Debug.Tuning, UElysiumEyeTuningConfig::Load());
		FVector GazeWorld = FVector::ZeroVector;
		bool bHaveGaze = false;
		if (Debug.Gaze == FElysiumEyeDebug::EGaze::Rest)
		{
			bHaveGaze = false;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Point)
		{
			GazeWorld = Debug.Target;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Camera && bHaveCamera)
		{
			GazeWorld = TrackWorld;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Off && bTrackPlayer && bHaveCamera)
		{
			GazeWorld = TrackWorld;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Off && Binding.bHasViewTarget)
		{
			GazeWorld = Binding.ViewTarget;
			bHaveGaze = true;
		}
		// The authored resting aim is what `bEyeMove` off selects — a real retail configuration, and
		// the one a body with no gaze source sits in.
		Tuning.bEyeMove = bHaveGaze;
		Binding.LastBlink = EyeInput.Blink;
		Binding.bLastAiming = bHaveGaze;

		for (const FElysiumEyeSlot& Slot : Binding.Slots)
		{
			UMaterialInstanceDynamic* Mid = Slot.Mid.Get();
			const FElysiumEyeball* Eye = Binding.Set->Find(Slot.EyeIndex);
			if (Mid == nullptr || Eye == nullptr)
			{
				continue;
			}
			// Component space throughout: the material measures its planes from the component
			// origin, and it keeps the arithmetic away from large world coordinates.
			const FTransform BoneToComponent = Comp->GetBoneTransform(Slot.BoneIndex, FTransform::Identity);
			const FVector Target = Tuning.bEyeMove
				? Comp->GetComponentTransform().InverseTransformPosition(GazeWorld)
				: FVector::ZeroVector;
			FElysiumEyeState State;
			ElysiumEyes::BuildState(*Eye, BoneToComponent, Target, Tuning, State);
			if (!State.bValid)
			{
				continue;
			}
			Mid->SetVectorParameterByIndex(Slot.ParamIrisU, FLinearColor(State.IrisU));
			Mid->SetVectorParameterByIndex(Slot.ParamIrisV, FLinearColor(State.IrisV));
			Mid->SetVectorParameterByIndex(Slot.ParamIrisOrigin, FLinearColor(State.Org));
			Mid->SetVectorParameterByIndex(Slot.ParamNormalOrigin, FLinearColor(State.NormalOrg));
			Mid->SetVectorParameterByIndex(Slot.ParamEyeUp, FLinearColor(State.AuthoredUp));

			// The lid half of the same pass. Carried in the eye bone's own space, with the record's
			// lid fields beside it, so the flex rig needs no eye state of its own.
			if (Slot.EyeIndex >= 0 && Slot.EyeIndex < 2)
			{
				EyeInput.Eyes[Slot.EyeIndex].FromRecord(*Eye, State);
			}
		}

		// One write per body per frame, which is what re-evaluates the face. A body with no flex
		// rig answers false and keeps its aiming irises — the player-body case.
		if (UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()))
		{
			Inst->SetEyeInput(EyeInput);
		}
	}
}
