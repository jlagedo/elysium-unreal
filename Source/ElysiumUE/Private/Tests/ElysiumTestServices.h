#pragma once

// The recording world-services stub (11.2). Implements all five FElysiumWorldServices interfaces
// and writes one line per call into `Calls`, so a Substrate-tier test can assert what a map's logic
// *did* — stood this body, played that voice, faded the screen, asked to travel — with no RHI, no
// actors and no `$ELYSIUM_EXPORT_ROOT`. That is the tier the back-pointers used to make impossible.
//
// It lives in the module (like the tests themselves — the substrate carries no ELYSIUMUE_API
// exports, so a same-module test links its symbols directly) and compiles only where the automation
// framework does.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumSignData.h"
#include "ElysiumWorldServices.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

struct FElysiumRecordingNpcMotor final : IElysiumNpcMotor
{
	TArray<FString>* Calls = nullptr;
	FVector Feet = FVector::ZeroVector;
	FVector RequestedFeet = FVector::ZeroVector;
	float Yaw = 0.0f;
	float RequestedYaw = 0.0f;
	float RequestedSpeedCmPerSecond = 0.0f;
	bool bEnabled = true;
	bool bMoving = false;
	bool bFacing = false;
	bool bFrozen = false;
	bool bIgnoreCharacterCollision = false;
	// A test drives arrival by flipping these: SampleStatus is what an in-flight move reports, and
	// clearing bAcceptMoves is how "this mark has no path" is expressed.
	bool bAcceptMoves = true;
	EElysiumNpcMoveStatus SampleStatus = EElysiumNpcMoveStatus::Moving;

	void Record(const FString& Call) const
	{
		if (Calls)
		{
			Calls->Add(Call);
		}
	}

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false) override
	{
		RequestedFeet = FeetDestination;
		RequestedSpeedCmPerSecond = SpeedCmPerSecond;
		bMoving = bEnabled && bAcceptMoves;
		bFacing = false;
		Record(FString::Printf(TEXT("NpcMotor MoveTo %s radius=%.1f speed=%.1f partial=%d"),
			*FeetDestination.ToString(), AcceptanceRadiusCm, SpeedCmPerSecond,
			bAllowPartialPath ? 1 : 0));
		return bMoving;
	}
	virtual void Face(float YawDegrees) override
	{
		RequestedYaw = YawDegrees;
		bFacing = bEnabled;
		Record(FString::Printf(TEXT("NpcMotor Face yaw=%.1f"), YawDegrees));
	}
	virtual void Stop() override
	{
		bMoving = false;
		bFacing = false;
		Record(TEXT("NpcMotor Stop"));
	}
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) override
	{
		bMoving = false;
		bFacing = false;
		Feet = FeetOrigin;
		Yaw = YawDegrees;
		Record(FString::Printf(TEXT("NpcMotor Teleport %s yaw=%.1f"), *Feet.ToString(), Yaw));
	}
	virtual void SetEnabled(bool bInEnabled) override
	{
		bEnabled = bInEnabled;
		if (!bEnabled)
		{
			bMoving = false;
			bFacing = false;
		}
		Record(FString::Printf(TEXT("NpcMotor SetEnabled %d"), bEnabled ? 1 : 0));
	}
	virtual void SetFrozen(bool bInFrozen) override
	{
		bFrozen = bInFrozen;
		if (bFrozen)
		{
			bMoving = false;
			bFacing = false;
		}
		Record(FString::Printf(TEXT("NpcMotor SetFrozen %d"), bFrozen ? 1 : 0));
	}
	virtual void SetIgnoreCharacterCollision(bool bIgnore) override
	{
		bIgnoreCharacterCollision = bIgnore;
		Record(FString::Printf(TEXT("NpcMotor SetIgnoreCharacterCollision %d"), bIgnore ? 1 : 0));
	}
	// The stub has no movement component to derive one from, so it reports a body standing still at
	// the yaw it was placed at. What a substrate test asserts is the request contract, not motion.
	virtual FElysiumLocomotionSample SampleLocomotion() const override
	{
		FElysiumLocomotionSample Out;
		Out.FacingYaw = Yaw;
		Out.bOnGround = true;
		return Out;
	}
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override
	{
		OutFeetOrigin = Feet;
		OutYawDegrees = Yaw;
		if (bMoving)
		{
			return SampleStatus;
		}
		return bFacing ? EElysiumNpcMoveStatus::Moving : EElysiumNpcMoveStatus::Idle;
	}
};

struct FElysiumRecordingServices final
	: public IElysiumEmbodiment
	, public IElysiumAudio
	, public IElysiumTravel
	, public IElysiumPresenter
	, public IElysiumWeather
{
	// One line per service call, in the order they happened: "PlayVoice ambient/x.wav".
	// Mutable so the const interface methods can record too.
	mutable TArray<FString> Calls;

	// The bundle to hand FElysiumEntityWorld. All four members point at this object.
	FElysiumWorldServices Bundle()
	{
		FElysiumWorldServices S;
		S.Embodiment = this;
		S.Audio      = this;
		S.Travel     = this;
		S.Presenter  = this;
		S.Weather    = this;
		return S;
	}

	// Did any call start with this prefix, and how many. Prefix-matched so a test can assert the
	// verb ("PlayNpcClip") or the verb and its argument ("PlayNpcClip jack cower_idle").
	int32 Count(const FString& Prefix) const
	{
		int32 N = 0;
		for (const FString& C : Calls)
		{
			N += C.StartsWith(Prefix) ? 1 : 0;
		}
		return N;
	}
	bool Saw(const FString& Prefix) const { return Count(Prefix) > 0; }
	FString Log() const { return FString::Join(Calls, TEXT(" | ")); }

	// What the player's body reports. A test sets these to stand a player somewhere; left as-is,
	// bHasPlayer false is the "menu backdrop / headless" case every call site must survive.
	bool     bHasPlayer = false;
	FVector  PlayerLocation = FVector::ZeroVector;
	FRotator PlayerRotation = FRotator::ZeroRotator;
	FElysiumCameraShot LastCameraShot;
	// What the next modern interaction query returns. Geometry-specific tests control the adapter;
	// substrate tests remain pure and exercise focus/session policy over these records.
	FElysiumUseQueryResult UseQuery;
	TMap<FElysiumEntityHandle, bool> UseAnchorEnabled;
	// Damage accumulated by DamagePlayer, so a trigger_hurt cadence is assertable as a number.
	float DamageTaken = 0.f;
	// Opt-in because most tests intentionally exercise the supported headless/no-motor path.
	bool bProvideNpcMotor = false;
	// Opt-in activity resolution mirrors the real manifest path. Default false preserves the
	// supported old-export fallback exercised by most Substrate tests.
	bool bNpcActivitiesResolve = false;
	FString ResolvedNpcActivityLabel = TEXT("walk");
	FString ResolvedNpcActivityClip = TEXT("walk_0");
	float ResolvedNpcGroundSpeedCmPerSecond = 0.f;
	TArray<TUniquePtr<FElysiumRecordingNpcMotor>> NpcMotors;
	FElysiumRecordingNpcMotor* LastNpcMotor() const
	{
		return NpcMotors.IsEmpty() ? nullptr : NpcMotors.Last().Get();
	}

	// --- IElysiumEmbodiment ----------------------------------------------------------------
	virtual float BodyScaleFor(const FElysiumEntityDef& Def) const override { return Def.bSky ? 16.f : 1.f; }

	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildNpcVisual %s %s scale=%.2f disp=%s var=%d"),
			*Stem, *Location.ToString(), UniformScale, *Disposition, IdleVariant));
		// A real component (transient, never registered — no RHI is touched) rather than null, so
		// the leaf classes take their body-carrying path: they register it for teardown, gate it on
		// dormancy, and route SetAnimation/SetDisposition through it.
		return NewComponent<USkeletalMeshComponent>();
	}
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FVector& FeetOrigin, float YawDegrees, const FString& Stem, int32 Variant) override
	{
		if (!bProvideNpcMotor || !Body)
		{
			return nullptr;
		}
		TUniquePtr<FElysiumRecordingNpcMotor> Motor = MakeUnique<FElysiumRecordingNpcMotor>();
		Motor->Calls = &Calls;
		Motor->Feet = FeetOrigin;
		Motor->Yaw = YawDegrees;
		FElysiumRecordingNpcMotor* Result = Motor.Get();
		NpcMotors.Add(MoveTemp(Motor));
		Record(FString::Printf(TEXT("BuildNpcMotor %s yaw=%.1f stem=%s var=%d"),
			*FeetOrigin.ToString(), YawDegrees, *Stem, Variant));
		return Result;
	}
	virtual void DestroyNpcMotor(IElysiumNpcMotor*) override
	{
		Record(TEXT("DestroyNpcMotor"));
	}
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("RefreshNpcIdle %s disp=%s var=%d"), *Stem, *Disposition, IdleVariant));
		return Body != nullptr;
	}
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayNpcClip %s %s loop=%d"), *Stem, *ClipName, bLoop ? 1 : 0));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;   // a beat's OnEndSequence schedules off this
		}
		return Body != nullptr;
	}
	virtual bool PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadNpcClip %s %s"), *Stem, *ClipName));
		return Body != nullptr;
	}
	virtual bool PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadNpcClipForModel %s player=%d %s"),
			*Stem, bPlayerMaterial ? 1 : 0, *ClipName));
		return !Stem.IsEmpty() && !ClipName.IsEmpty();
	}
	virtual bool PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayNpcActivity %s %s var=%d loop=%d"), *Stem, *Activity,
			Variant, bLoop ? 1 : 0));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;
		}
		return bNpcActivitiesResolve && Body != nullptr;
	}
	virtual bool ResolveNpcActivityClip(const FString& Stem, const FString& Activity, int32 Variant,
		FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond) override
	{
		Record(FString::Printf(TEXT("ResolveNpcActivityClip %s %s var=%d"), *Stem, *Activity,
			Variant));
		OutLabel = bNpcActivitiesResolve ? ResolvedNpcActivityLabel : FString();
		OutAnimName = bNpcActivitiesResolve ? ResolvedNpcActivityClip : FString();
		OutGroundSpeedCmPerSecond = bNpcActivitiesResolve
			? ResolvedNpcGroundSpeedCmPerSecond : 0.f;
		return bNpcActivitiesResolve;
	}
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayCinematicClip %s %s %s %s"), *Stem, *AnimSetModel,
			*BoneRoot, *ClipName));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;
		}
		return bCinematicClipsResolve && Body != nullptr;
	}
	virtual bool PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadCinematicClip %s %s %s %s"), *Stem,
			*AnimSetModel, *BoneRoot, *ClipName));
		return bCinematicClipsResolve && Body != nullptr;
	}
	virtual bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadCinematicClipForModel %s player=%d %s %s %s"),
			*Stem, bPlayerMaterial ? 1 : 0, *AnimSetModel, *BoneRoot, *ClipName));
		return bCinematicClipsResolve && !Stem.IsEmpty();
	}
	virtual int32 FinishAnimationPreload() override
	{
		Record(TEXT("FinishAnimationPreload"));
		return 1;
	}
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override
	{
		Record(FString::Printf(TEXT("SeekCinematicClip %.3f"), PositionSeconds));
		return Body != nullptr;
	}
	virtual void StopCinematicClip(USkeletalMeshComponent*) override { Record(TEXT("StopCinematicClip")); }

	// Where each fake body's clip is "at". A test writes an entry to stage drift and reads it back
	// to see what a resync corrected it to. An ABSENT entry is the ordinary "this body has no anim
	// host" answer, which is what a prop with no anim instance gets — keyed on the component so
	// a test with two bodies can drift one without touching the other.
	TMap<const USkeletalMeshComponent*, float> ClipPositions;

	virtual bool GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const override
	{
		const float* Position = Body ? ClipPositions.Find(Body) : nullptr;
		if (!Position)
		{
			return false;
		}
		OutSeconds = *Position;
		return true;
	}
	virtual bool ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override
	{
		Record(FString::Printf(TEXT("ResyncCinematicClip %.3f"), PositionSeconds));
		if (Body)
		{
			ClipPositions.Add(Body, PositionSeconds);
		}
		return Body != nullptr;
	}

	// The controller names this fake face carries, lowercased. Empty is the default and stands for a
	// body with no facial rig — the majority of the exported cast, and the case every caller must
	// treat as an ordinary no-op.
	TSet<FString> FlexControllers;
	// The last value written per controller, so a test reads a composed expression back by name
	// instead of by morph weight.
	TMap<FString, float> FlexPose;
	float FlexValue(const FString& Name) const
	{
		const float* Found = FlexPose.Find(Name.ToLower());
		return Found != nullptr ? *Found : 0.f;
	}
	virtual int32 SetFlexControllers(USkeletalMeshComponent* Body,
		TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing) override
	{
		if (Body == nullptr || FlexControllers.IsEmpty())
		{
			return INDEX_NONE;
		}
		int32 Applied = 0;
		for (const FElysiumFlexWrite& Write : Writes)
		{
			const FString Key = Write.Name.ToLower();
			if (!FlexControllers.Contains(Key))
			{
				if (OutMissing != nullptr)
				{
					OutMissing->AddUnique(Write.Name);
				}
				continue;
			}
			FlexPose.Add(Key, Write.Value);
			++Applied;
		}
		Record(FString::Printf(TEXT("SetFlexControllers %d of %d"), Applied, Writes.Num()));
		return Applied;
	}

	// Whether this fake face carries an `mstudiomouth_t`. 199 of the 201 rigged models do, so the
	// default is yes — but only for a body that has a rig at all, which is what makes the unrigged
	// half of the cast a clean no-op rather than a silent write.
	bool bHasMouthRecord = true;
	// Per body, because a jaw is the one facial write a multi-actor scene has to keep apart: the
	// courtroom has seven of them and they do not speak at the same time.
	TMap<const USkeletalMeshComponent*, float> MouthOpenByBody;
	float MouthOpenOf(const FElysiumEntity* Entity) const
	{
		const USkeletalMeshComponent* Body = Entity ? Entity->GetSkeletalBody() : nullptr;
		const float* Found = Body ? MouthOpenByBody.Find(Body) : nullptr;
		return Found != nullptr ? *Found : 0.f;
	}
	virtual bool SetMouthOpen(USkeletalMeshComponent* Body, float Open) override
	{
		if (Body == nullptr || FlexControllers.IsEmpty() || !bHasMouthRecord)
		{
			return false;
		}
		MouthOpenByBody.Add(Body, Open);
		return true;
	}

	// 12.5 — the per-model phoneme filter this fake cast answers with. Per body, because the point of
	// the read is that two speakers in one scene can carry different pairs. A body with no entry falls
	// back to the shared pair, so a test that does not care sets nothing.
	float PhonemeFilterMin = 0.065f;
	float PhonemeFilterMax = 0.100f;
	TMap<const USkeletalMeshComponent*, TPair<float, float>> PhonemeFilterByBody;
	virtual bool GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
		float& OutMax) const override
	{
		if (Body == nullptr || FlexControllers.IsEmpty())
		{
			return false;   // no rig here, and the caller keeps its default
		}
		if (const TPair<float, float>* Found = PhonemeFilterByBody.Find(Body))
		{
			OutMin = Found->Key;
			OutMax = Found->Value;
			return true;
		}
		OutMin = PhonemeFilterMin;
		OutMax = PhonemeFilterMax;
		return true;
	}

	// 12.4 — the gaze seam, recorded rather than drawn. The head frame is test-controlled so a
	// cascade assertion can put a candidate inside or outside the ±30° cone on purpose.
	TMap<const USkeletalMeshComponent*, FVector> ViewTargetByBody;
	bool bHasHeadFrame = false;
	FVector HeadFramePosition = FVector::ZeroVector;
	FVector HeadFrameForward = FVector(1.f, 0.f, 0.f);

	virtual bool SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget) override
	{
		if (Body == nullptr)
		{
			return false;
		}
		ViewTargetByBody.Add(Body, WorldTarget);
		return true;
	}
	virtual bool GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
		FVector& OutForward) const override
	{
		if (Body == nullptr || !bHasHeadFrame)
		{
			return false;
		}
		OutPosition = HeadFramePosition;
		OutForward = HeadFrameForward;
		return true;
	}
	FVector ViewTargetOf(const FElysiumEntity* Entity) const
	{
		const USkeletalMeshComponent* Body = Entity ? Entity->GetSkeletalBody() : nullptr;
		const FVector* Found = Body ? ViewTargetByBody.Find(Body) : nullptr;
		return Found != nullptr ? *Found : FVector::ZeroVector;
	}
	// Test-controlled model path -> v4 animated-prop stem.
	TMap<FString, FString> AnimatedPropModels;
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const override
	{
		if (const FString* Stem = AnimatedPropModels.Find(ModelPath))
		{
			return *Stem;
		}
		return FString();
	}
	// Test-controlled prop stem -> its rest clip. An entry mapping to an empty string models a
	// model that bakes no clip, which is how the static-fallback path is exercised.
	TMap<FString, FString> AnimatedPropRestClips;
	// Test-controlled "<stem>|<clip>" -> the clip's STUDIO_LOOPING bit. A stem with a rest clip
	// resolves every clip name; this map only decides whether one loops.
	TMap<FString, bool> AnimatedPropClipLoops;

	// The rotation each build path was handed. Kept as a quaternion rather than asserted off the
	// record string: the two representations deliberately receive different bases, and a float
	// comparison with a tolerance is the honest test of that.
	FQuat LastAnimatedPropRotation = FQuat::Identity;
	FQuat LastPropRotation = FQuat::Identity;

	// The body each animated stem was built onto, so a test can address one prop's clip by name —
	// the records carry the stem but not the pointer, and ClipPositions is keyed on the component
	// because that is what the real seam receives.
	TMap<FString, USkeletalMeshComponent*> AnimatedPropBodies;

	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale) override
	{
		// The rotation is recorded last so the existing prefix assertions keep matching.
		Record(FString::Printf(TEXT("BuildAnimatedPropVisual %s %s scale=%.2f rot=%s"),
			*Stem, *Location.ToString(), UniformScale, *Rotation.Rotator().ToString()));
		LastAnimatedPropRotation = Rotation;
		USkeletalMeshComponent* Body = NewComponent<USkeletalMeshComponent>();
		AnimatedPropBodies.Add(Stem, Body);
		return Body;
	}
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayAnimatedPropClip %s %s loop=%d"),
			*Stem, *ClipName, bLoop ? 1 : 0));
		if (OutSeconds) { *OutSeconds = ClipSeconds; }
		return Body != nullptr;
	}
	virtual int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
		const FString& Stem) override
	{
		Record(FString::Printf(TEXT("PreloadAnimatedPropClips %s"), *Stem));
		return Body != nullptr ? 1 : 0;
	}
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent*, const FString& StaticStem,
		int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyAnimatedPropSkin %s family=%d"), *StaticStem, Family));
	}
	virtual FString AnimatedPropRestClip(const FString& Stem) const override
	{
		const FString* Found = AnimatedPropRestClips.Find(Stem);
		// Unregistered stems answer a rest clip so a test that only cares about SetAnimation does
		// not have to declare one; an explicit empty entry is the "bakes no clip" case.
		return Found != nullptr ? *Found : FString(TEXT("idle"));
	}
	virtual bool FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
		bool& bOutLoops) const override
	{
		const bool* Loops = AnimatedPropClipLoops.Find(Stem + TEXT("|") + ClipName);
		bOutLoops = Loops != nullptr && *Loops;
		return !AnimatedPropRestClip(Stem).IsEmpty() && !ClipName.IsEmpty();
	}
	virtual UStaticMeshComponent* BuildBrushVisual(const FString& Stem,
		USceneComponent* ParentBody, float UniformScale, bool bSky) override
	{
		Record(FString::Printf(TEXT("BuildBrushVisual %s scale=%.2f sky=%d"),
			*Stem, UniformScale, bSky ? 1 : 0));
		UStaticMeshComponent* Visual = NewComponent<UStaticMeshComponent>();
		if (ParentBody)
		{
			Visual->SetupAttachment(ParentBody);
			Visual->SetRelativeTransform(FTransform::Identity);
		}
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Visual;
	}

	// The authored length every stub clip reports. A scripted_sequence's OnEndSequence lands here.
	float ClipSeconds = 1.0f;
	// Whether a cinematic anim set resolves. Default false, which is the state of the world until
	// PL16's banks are exported — a scene must run its timeline and outputs either way.
	bool bCinematicClipsResolve = false;

	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPropVisual %s %s scale=%.2f"), *Stem, *Location.ToString(), UniformScale));
		LastPropRotation = Rotation;
		return NewComponent<UStaticMeshComponent>();
	}
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPhysPropVisual %s %s scale=%.2f"), *Stem, *Location.ToString(), UniformScale));
		return NewComponent<UStaticMeshComponent>();
	}
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyPropSkin %s family=%d"), *Stem, Family));
	}
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildPlayerVisual %s disp=%s var=%d"),
			*Stem, *Disposition, IdleVariant));
		return Stem.IsEmpty() ? nullptr : NewComponent<USkeletalMeshComponent>();
	}
	virtual void ClearPlayerVisual() override
	{
		Record(TEXT("ClearPlayerVisual"));
	}

	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override
	{
		if (!bHasPlayer)
		{
			return false;
		}
		OutLocation = PlayerLocation;
		OutRotation = PlayerRotation;
		return true;
	}
	virtual bool GetPlayerUseOrigin(FVector& OutLocation) const override
	{
		if (!bHasPlayer)
		{
			return false;
		}
		OutLocation = PlayerLocation;
		return true;
	}
	virtual bool GetPlayerFeetTransform(FVector& OutLocation, FRotator& OutRotation) const override
	{
		if (!bHasPlayer)
		{
			return false;
		}
		OutLocation = PlayerLocation;
		OutRotation = PlayerRotation;
		return true;
	}
	virtual bool GetPlayerCapsuleTransform(FVector& OutLocation, FRotator& OutRotation) const override
	{
		return GetPlayerFeetTransform(OutLocation, OutRotation); // headless services have no centred hull
	}
	virtual void TeleportPlayer(const FVector& FeetOrigin, const FRotator& ViewRotation) override
	{
		Record(FString::Printf(TEXT("TeleportPlayer %s rot=%s"),
			*FeetOrigin.ToString(), *ViewRotation.ToCompactString()));
		bHasPlayer = true;
		PlayerLocation = FeetOrigin;
		PlayerRotation = ViewRotation;
	}
	virtual void DamagePlayer(float Amount) override
	{
		Record(FString::Printf(TEXT("DamagePlayer %.1f"), Amount));
		DamageTaken += Amount;
	}
	virtual void RegisterUseAnchor(UPrimitiveComponent*, const FElysiumEntityHandle& Owner) override
	{
		UseAnchorEnabled.Add(Owner, true);
		Record(FString::Printf(TEXT("RegisterUseAnchor %s"), *Owner.ToString()));
	}
	virtual void SetUseAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) override
	{
		UseAnchorEnabled.Add(Owner, bEnabled);
		Record(FString::Printf(TEXT("SetUseAnchorEnabled %s %d"), *Owner.ToString(), bEnabled ? 1 : 0));
	}
	virtual void ClearUseAnchors() override
	{
		UseAnchorEnabled.Reset();
		Record(TEXT("ClearUseAnchors"));
	}
	virtual FElysiumUseQueryResult QueryPlayerUse(
		const FElysiumEntityHandle& CurrentFocus) const override
	{
		return UseQuery;
	}
	// What the next `+feed` acquisition finds. Geometry is the embodiment's; a substrate test drives
	// the acceptance policy and the transaction over whatever this answers.
	FElysiumEntityHandle FeedTarget;
	virtual FElysiumEntityHandle QueryFeedTarget() const override
	{
		Record(FString::Printf(TEXT("QueryFeedTarget -> %s"), *FeedTarget.ToString()));
		return FeedTarget;
	}
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) override
	{
		Record(FString::Printf(TEXT("PushCameraShot %s"), *ShotFile));
		return ++NextCameraShotId;
	}
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) override
	{
		Record(FString::Printf(TEXT("PushCameraShotValue %s"), *Shot.DebugName));
		LastCameraShot = Shot;
		return ++NextCameraShotId;
	}
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) override
	{
		Record(FString::Printf(TEXT("UpdateCameraShotValue %d %s"), ShotId, *Shot.DebugName));
		LastCameraShot = Shot;
		return ShotId > 0;
	}
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) override
	{
		Record(FString::Printf(TEXT("PopCameraShot %d blend=%.2f"), ShotId, BlendOutSeconds));
		return ShotId > 0;
	}

	// --- IElysiumAudio ---------------------------------------------------------------------
	virtual FElysiumVoiceHandle Submit(FElysiumAudioRequest Request) override
	{
		const FElysiumVoiceHandle H{ ++NextVoiceSlot, 1 };
		Record(FString::Printf(TEXT("Submit %s owner=%s epoch=%llu gain=%.2f loop=%d fade=%.2f"),
			*UElysiumAudioSubsystem::ResolveSourcePath(Request.Source), *Request.Owner.StableId,
			Request.Owner.MapEpoch, Request.Gain, Request.bLooping ? 1 : 0,
			Request.FadeInSeconds));
		LiveVoices.Add(H);
		Requests.Add(H, MoveTemp(Request));
		return H;
	}
	virtual void Prefetch(const FElysiumAudioSource& Source) override
	{
		Record(FString::Printf(TEXT("Prefetch %s"),
			*UElysiumAudioSubsystem::ResolveSourcePath(Source)));
	}
	virtual void PauseVoice(FElysiumVoiceHandle Handle, bool bPaused) override
	{
		Record(FString::Printf(TEXT("PauseVoice %u:%u %d"),
			Handle.Slot, Handle.Generation, bPaused ? 1 : 0));
	}
	virtual void SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds) override
	{
		Record(FString::Printf(TEXT("SeekVoice %u:%u %.3f"),
			Handle.Slot, Handle.Generation, MediaOffsetSeconds));
	}
	virtual void SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch) override
	{
		Record(FString::Printf(TEXT("SetVoicePitch %u:%u %.2f"),
			Handle.Slot, Handle.Generation, Pitch));
	}
	virtual void CancelAudioOwner(FElysiumAudioOwner Owner, float FadeSeconds = 0.f) override
	{
		Record(FString::Printf(TEXT("CancelAudioOwner %s epoch=%llu fade=%.2f"),
			*Owner.StableId, Owner.MapEpoch, FadeSeconds));
		TArray<FElysiumVoiceHandle> Remove;
		for (const TPair<FElysiumVoiceHandle, FElysiumAudioRequest>& Pair : Requests)
		{
			if (Pair.Value.Owner == Owner)
			{
				Remove.Add(Pair.Key);
			}
		}
		for (const FElysiumVoiceHandle H : Remove)
		{
			LiveVoices.Remove(H);
			Requests.Remove(H);
		}
	}
	virtual FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params) override
	{
		Record(FString::Printf(TEXT("PlayVoice %s vol=%.2f loop=%d offset=%.3f"), *Rel, Params.Volume,
			Params.bLooping ? 1 : 0, Params.StartTimeSeconds));
		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(Rel);
		Request.Gain = Params.Volume;
		Request.Pitch = Params.Pitch;
		Request.bLooping = Params.bLooping;
		Request.StartOffsetSeconds = Params.StartTimeSeconds;
		return Submit(MoveTemp(Request));
	}
	virtual void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("StopVoice %u:%u fade=%.2f"),
			Handle.Slot, Handle.Generation, FadeSeconds));
		LiveVoices.Remove(Handle);
		Requests.Remove(Handle);
	}
	virtual void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume) override
	{
		Record(FString::Printf(TEXT("SetVoiceVolume %u:%u %.2f"),
			Handle.Slot, Handle.Generation, Volume));
	}
	virtual bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const override
	{
		return LiveVoices.Contains(Handle);
	}
	virtual void FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("FadeInScheme %s fade=%.2f"), *SchemeRel, FadeSeconds));
		ActiveScheme = SchemeRel;
	}
	virtual void FadeOutScheme(const FString& SchemeRel, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("FadeOutScheme %s fade=%.2f"), *SchemeRel, FadeSeconds));
		if (ActiveScheme == SchemeRel)
		{
			ActiveScheme.Reset();
		}
	}
	virtual FString ActiveSchemeRel() const override { return ActiveScheme; }
	// What the recorded output path claims its lead is. A test sets it to stand in for a measured
	// device; left alone it is the same no-device fallback a headless world answers with.
	float OutputLead = ElysiumAudioLatency::FallbackLeadSeconds;
	virtual float OutputLeadSeconds() const override { return OutputLead; }

	// --- IElysiumTravel --------------------------------------------------------------------
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) override
	{
		Record(FString::Printf(TEXT("RequestLandmarkTravel %s@%s off=%s yaw=%.1f"),
			*Map, *Landmark, *Offset.ToString(), Yaw));
	}
	virtual void ChangeMap(const FString& Map) override
	{
		Record(FString::Printf(TEXT("ChangeMap %s"), *Map));
	}

	// --- IElysiumPresenter -----------------------------------------------------------------
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) override
	{
		Record(FString::Printf(TEXT("StartFade dur=%.2f hold=%.2f alpha=%.2f in=%d rev=%d"),
			Duration, HoldTime, MaxAlpha, bFadeIn ? 1 : 0, bAutoReverse ? 1 : 0));
	}
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) override
	{
		Record(FString::Printf(TEXT("OpenSign #%d fade=%.2f"), Owner.Index, FadeInSeconds));
		OpenSignOwner = Owner;
	}
	virtual void CloseSign() override
	{
		Record(TEXT("CloseSign"));
		OpenSignOwner = FElysiumEntityHandle::Invalid();
	}
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) override
	{
		Record(FString::Printf(TEXT("OpenDialog #%d"), Owner.Index));
		OpenDialogOwner = Owner;
	}
	virtual void CloseDialog() override
	{
		Record(TEXT("CloseDialog"));
		OpenDialogOwner = FElysiumEntityHandle::Invalid();
	}

	// --- IElysiumWeather -------------------------------------------------------------------
	virtual void ApplyWetness(const FElysiumWeatherTransition& Transition) override
	{
		LastWetness = Transition;
		Record(FString::Printf(TEXT("ApplyWetness %.3f -> %.3f"),
			Transition.CurrentWetness, Transition.TargetWetness));
	}
	virtual void ApplyEmitter(const FElysiumWeatherEmitterState& Emitter) override
	{
		Emitters.Add(Emitter.Entity.Index, Emitter);
		Record(FString::Printf(TEXT("ApplyEmitter #%d %s rate=%.3f"), Emitter.Entity.Index,
			Emitter.bActive ? TEXT("on") : TEXT("off"), Emitter.RateScale));
	}
	virtual void RemoveEmitter(const FElysiumEntityHandle& Entity) override
	{
		Emitters.Remove(Entity.Index);
		Record(FString::Printf(TEXT("RemoveEmitter #%d"), Entity.Index));
	}

	FElysiumEntityHandle OpenSignOwner;
	FElysiumEntityHandle OpenDialogOwner;
	FString ActiveScheme;
	FElysiumWeatherTransition LastWetness;
	TMap<int32, FElysiumWeatherEmitterState> Emitters;

private:
	void Record(FString&& Line) const { Calls.Add(MoveTemp(Line)); }

	// Transient components handed back by the Build* calls, kept rooted for the test's lifetime so
	// GC cannot reclaim one while an entity still holds it.
	template <typename T>
	T* NewComponent()
	{
		T* Comp = NewObject<T>(GetTransientPackage());
		Spawned.Emplace(Comp);
		return Comp;
	}
	TArray<TStrongObjectPtr<UActorComponent>> Spawned;

	uint32 NextVoiceSlot = 0;
	int32 NextCameraShotId = 0;
	TSet<FElysiumVoiceHandle> LiveVoices;
	TMap<FElysiumVoiceHandle, FElysiumAudioRequest> Requests;
};

// The ordered I/O recorder. Every sink tap writes ONE formatted line into ONE array, so the whole
// causality stream of a headless world — what was fired, what entered the queue, what came back
// out, in what order — is a single sequence a test can assert positions inside. The event-order
// contract (`docs/architecture/gameplay-systems-architecture.md` §2.5.1) is a statement about
// relative order, and relative order is what a per-facility counter cannot express.
//
// Line grammar: `<kind> <detail>`, kind being the first whitespace-delimited token.
//
//   fire      relay1.OnTrigger -> counter1.Add
//   queue     counter1.Add(5) @1.200 +py
//   deliver   #3 counter1.Add(5)
//   python    G.Tut_Key = 1
//   no-target ghost.Add
//   no-input  #3 counter1.Nope
//   loop-guard 10000
//
// An entity reads as `#<index> <targetname-or-classname>`, so a fan-out over three same-named
// entities is distinguishable by index, which is what stable-entity-order assertions need.
class FElysiumOrderedIOSink final : public IElysiumIOSink
{
public:
	// Every recorded event, in occurrence order.
	TArray<FString> Lines;

	void Reset() { Lines.Reset(); }
	FString Log() const { return FString::Join(Lines, TEXT(" | ")); }

	// The lines of one kind, kind token included, in occurrence order.
	TArray<FString> OfKind(const TCHAR* Kind) const
	{
		TArray<FString> Out;
		const FString Prefix = FString(Kind) + TEXT(" ");
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				Out.Add(Line);
			}
		}
		return Out;
	}
	FString Sequence(const TCHAR* Kind) const { return FString::Join(OfKind(Kind), TEXT(" | ")); }

	// Position of the first line of `Kind` containing `Needle`, counted within that kind's own
	// lines (so a `deliver` position is comparable against another `deliver`). INDEX_NONE if absent.
	int32 PositionOf(const TCHAR* Kind, const FString& Needle) const
	{
		const TArray<FString> Kinds = OfKind(Kind);
		for (int32 i = 0; i < Kinds.Num(); ++i)
		{
			if (Kinds[i].Contains(Needle))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
	int32 CountOf(const TCHAR* Kind, const FString& Needle) const
	{
		int32 N = 0;
		for (const FString& Line : OfKind(Kind))
		{
			N += Line.Contains(Needle) ? 1 : 0;
		}
		return N;
	}
	bool Saw(const TCHAR* Kind, const FString& Needle) const
	{
		return PositionOf(Kind, Needle) != INDEX_NONE;
	}

	// Every needle appears among this kind's lines, and in the order given. The ordering assertion
	// the whole determinism contract is written in.
	bool AppearsInOrder(const TCHAR* Kind, const TArray<FString>& Needles) const
	{
		int32 Last = INDEX_NONE;
		for (const FString& Needle : Needles)
		{
			const int32 At = PositionOf(Kind, Needle);
			if (At == INDEX_NONE || At <= Last)
			{
				return false;
			}
			Last = At;
		}
		return true;
	}

	// --- IElysiumIOSink --------------------------------------------------------------------
	virtual void OnOutputFired(double, const FElysiumEntity& Source,
		const FElysiumOutputDef& Output) override
	{
		Lines.Add(FString::Printf(TEXT("fire %s.%s -> %s.%s"),
			*Name(Source), *Output.Name,
			Output.Target.IsEmpty() ? TEXT("(python)") : *Output.Target, *Output.Input));
	}
	virtual void OnQueued(double, const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("queue %s.%s(%s) @%.3f%s"),
			Event.Target.IsEmpty() ? TEXT("(python)") : *Event.Target,
			*Event.Input.ToString(), *Event.Param.ToString(), Event.FireTime,
			Event.PythonSrc.IsEmpty() ? TEXT("") : TEXT(" +py")));
	}
	virtual void OnDelivered(double, const FElysiumEntity& Target,
		const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("deliver #%d %s.%s(%s)"),
			Target.Handle.Index, *Name(Target), *Event.Input.ToString(), *Event.Param.ToString()));
	}
	virtual void OnUnknownTarget(double, const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("no-target %s.%s"), *Event.Target, *Event.Input.ToString()));
	}
	virtual void OnUnknownInput(double, const FElysiumEntity& Target,
		const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("no-input #%d %s.%s"),
			Target.Handle.Index, *Name(Target), *Event.Input.ToString()));
	}
	virtual void OnPython(double, const FElysiumIOEvent& Event, const FElysiumVariant&) override
	{
		Lines.Add(FString::Printf(TEXT("python %s"), *Event.PythonSrc));
	}
	virtual void OnLoopGuard(double, int32 Delivered) override
	{
		Lines.Add(FString::Printf(TEXT("loop-guard %d"), Delivered));
	}

private:
	// A nameless entity still has to be distinguishable, so it reads as its classname.
	static FString Name(const FElysiumEntity& Entity)
	{
		if (!Entity.TargetName.IsEmpty())
		{
			return Entity.TargetName;
		}
		return Entity.Def ? Entity.Def->Classname : FString(TEXT("?"));
	}
};

#endif   // WITH_DEV_AUTOMATION_TESTS
