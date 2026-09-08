#pragma once

// The recording world-services stub. Implements all five FElysiumWorldServices interfaces
// and writes one line per call into `Calls`, so a Substrate-tier test can assert what a map's logic
// *did* — stood this body, played that voice, faded the screen, asked to travel — with no RHI, no
// actors and no `$ELYSIUM_EXPORT_ROOT`.
//
// It lives in the module (like the tests themselves — the substrate carries no ELYSIUMUE_API
// exports, so a same-module test links its symbols directly) and compiles only where the automation
// framework does.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h" // EElysiumAnimBodyKind + BodyKindName (the recorded chain)
#include "ElysiumCameraSolve.h"   // FElysiumCameraShot (full type; ElysiumWorldServices.h only forward-declares it)
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"
#include "ElysiumMoveSolve.h"     // HullHalfWidth/StandHeight — the character box the sweep reaches
#include "ElysiumSurfaceSounds.h" // A2: FElysiumSurfaceSounds (held by value in the table below)
#include "ElysiumVariant.h"
#include "Substrate/ElysiumSignData.h"
#include "ElysiumWorldServices.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumDisposition.h"
#include "Visual/ElysiumBodyAnimInstance.h"   // the live clip-phase forward below

#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"

struct FElysiumRecordingNpcMotor final : IElysiumNpcMotor
{
	FElysiumNpcNavigationSample Navigation;
	virtual FElysiumNpcNavigationSample SampleNavigation() const override { return Navigation; }
	virtual void ClearNavigationGoal() override
	{
		Navigation.bActiveGoal = false;
		if (Navigation.Type != EElysiumNpcNavType::Jump && Navigation.Type != EElysiumNpcNavType::Climb) Stop();
		Record(TEXT("NpcMotor ClearNavigationGoal"));
	}
	virtual void SetNavigationType(EElysiumNpcNavType Type) override { Navigation.Type = Type; Record(TEXT("NpcMotor SetNavigationType")); }
	virtual void ResetSteering() override { Record(TEXT("NpcMotor ResetSteering")); }
	TArray<FString>* Calls = nullptr;
	FVector Feet = FVector::ZeroVector;
	FVector RequestedFeet = FVector::ZeroVector;
	float Yaw = 0.0f;
	float RequestedYaw = 0.0f;
	float RequestedSpeedCmPerSecond = 0.0f;
	// Which fan the in-flight request's speed came from, or unset for a caller-authored speed that
	// must never be re-derived from a changed fan (the equip-mid-leg fix).
	TOptional<EElysiumNpcGaitKind> RequestedGaitKind;
	bool bEnabled = true;
	bool bMoving = false;
	bool bFacing = false;
	bool bFrozen = false;
	bool bIgnoreCharacterCollision = false;
	FElysiumEntityHandle Owner;
	// A test drives arrival by flipping these: SampleStatus is what an in-flight move reports, and
	// clearing bAcceptMoves is how "this mark has no path" is expressed.
	bool bAcceptMoves = true;
	EElysiumNpcMoveStatus SampleStatus = EElysiumNpcMoveStatus::Moving;
	// The reachability query. The stub projects to the point it was handed, which is the
	// "open floor, nothing to correct" world every existing case already assumes; clearing
	// bProjectsToNavigable is how "there is no navmesh under that" is expressed, and setting
	// ProjectedOverride is how a projection that MOVED the point is expressed. Both branches matter:
	// the consumer's re-test only fires on the second.
	bool bProjectsToNavigable = true;
	TOptional<FVector> ProjectedOverride;
	// The body's own authored forward cell per gait. Zero is the default and means
	// "this body resolves no fan", which is how the caller's fallback to the stated constants is
	// exercised; setting one is how an authored travel speed is expressed.
	float AuthoredWalkSpeedCmPerSecond = 0.f;
	float AuthoredRunSpeedCmPerSecond = 0.f;
	float AuthoredSneakSpeedCmPerSecond = 0.f;
	// How much of the forward cell a fully-reversed direction commands. One means a flat fan, which
	// is the default so every existing case reads exactly the number it set above.
	float StrafeSpeedFraction = 1.0f;

	void Record(const FString& Call) const
	{
		if (Calls)
		{
			Calls->Add(Call);
		}
	}

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false,
		TOptional<EElysiumNpcGaitKind> GaitKind = TOptional<EElysiumNpcGaitKind>()) override
	{
		RequestedFeet = FeetDestination;
		RequestedSpeedCmPerSecond = SpeedCmPerSecond;
		RequestedGaitKind = GaitKind;
		bMoving = bEnabled && bAcceptMoves;
		Navigation.bActiveGoal = bMoving;
		bFacing = false;
		const TCHAR* GaitKindName = TEXT("none");
		if (GaitKind.IsSet())
		{
			switch (*GaitKind)
			{
			case EElysiumNpcGaitKind::Run:   GaitKindName = TEXT("run");   break;
			case EElysiumNpcGaitKind::Sneak: GaitKindName = TEXT("sneak"); break;
			default:                         GaitKindName = TEXT("walk"); break;
			}
		}
		Record(FString::Printf(TEXT("NpcMotor MoveTo %s radius=%.1f speed=%.1f partial=%d gait=%s"),
			*FeetDestination.ToString(), AcceptanceRadiusCm, SpeedCmPerSecond,
			bAllowPartialPath ? 1 : 0, GaitKindName));
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
		Navigation.bActiveGoal = false;
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
	// The ballistic pair, MODELLED rather than recorded.
	//
	// A recorded-only stub cannot serve a chain: the terminator reads whether the body is grounded,
	// so a stub answering the same thing every think either ends the chain on its first look or
	// never ends it at all. The double therefore holds the two flags a case drives.
	//
	// `bCarriesBallistic` is the seam's own "this motor cannot carry a launched body" answer, which
	// a case sets false to prove the caller ends its chain instead of waiting forever.
	bool bCarriesBallistic = true;
	bool bLaunched = false;
	FVector LaunchedVelocityCmPerSecond = FVector::ZeroVector;
	// What the next `SampleBallistic` reports. A case moves these to walk a body through its flight:
	// airborne, then contacted with a normal, then grounded.
	FElysiumBallisticSample Ballistic;

	virtual bool Launch(const FVector& VelocityCmPerSecond) override
	{
		Record(FString::Printf(TEXT("NpcMotor Launch %s carries=%d"),
			*VelocityCmPerSecond.ToString(), bCarriesBallistic ? 1 : 0));
		if (!bCarriesBallistic)
		{
			return false;
		}
		bLaunched = true;
		LaunchedVelocityCmPerSecond = VelocityCmPerSecond;
		// The default flight a case starts in, so a launch that is not driven further still reads as
		// airborne rather than as a body that landed on the frame it left the ground.
		Ballistic.bGrounded = false;
		Ballistic.bFalling = true;
		Ballistic.VelocityCmPerSecond = VelocityCmPerSecond;
		return true;
	}

	virtual bool SampleBallistic(FElysiumBallisticSample& Out) const override
	{
		if (!bCarriesBallistic)
		{
			return false;   // the headless answer, and the one a chain has to end on
		}
		Out = Ballistic;
		return true;
	}

	// A1 (footsteps): the surfaceprop this body's last move step left cached — the double's stand-in
	// for `CAI_BaseNPC +0x5b90`. `NAME_None` by default, which is retail's own answer for a body
	// that has never travelled and the one that makes a step silent; a case that wants a footfall
	// names the surface it is standing on.
	FName GroundSurface;

	virtual FElysiumLocomotionSample SampleLocomotion() const override
	{
		FElysiumLocomotionSample Out;
		Out.FacingYaw = Yaw;
		Out.bOnGround = true;
		Out.GroundSurface = GroundSurface;
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
	// The stub carries one cell per gait rather than a fan, and `StrafeSpeedFraction` is how a test
	// says "this body's sideways cells are slower than its forward one" without building a table:
	// the fraction is applied by how far off forward the direction is, so a substrate test can
	// assert that a turning body is commanded a different number than a settled one.
	virtual float GaitSpeed(EElysiumNpcGaitKind Gait, float MoveYawDegrees) const override
	{
		float Forward = AuthoredWalkSpeedCmPerSecond;
		switch (Gait)
		{
		case EElysiumNpcGaitKind::Run:   Forward = AuthoredRunSpeedCmPerSecond;   break;
		case EElysiumNpcGaitKind::Sneak: Forward = AuthoredSneakSpeedCmPerSecond; break;
		default: break;
		}
		if (Forward <= 0.f || !FMath::IsFinite(MoveYawDegrees))
		{
			return Forward;   // no fan to read a direction out of: the caller falls back
		}
		const float Off = FMath::Abs(FRotator::NormalizeAxis(MoveYawDegrees)) / 180.0f;
		return Forward * FMath::Lerp(1.0f, StrafeSpeedFraction, FMath::Clamp(Off, 0.0f, 1.0f));
	}
	virtual bool ProjectToNavigable(const FVector& PointCm, FVector& OutProjectedCm) const override
	{
		const FVector Result = ProjectedOverride.Get(PointCm);
		Record(FString::Printf(TEXT("NpcMotor ProjectToNavigable %s -> %s"), *PointCm.ToString(),
			bProjectsToNavigable ? *Result.ToString() : TEXT("unprojectable")));
		if (!bProjectsToNavigable)
		{
			return false;   // OutProjectedCm stays untouched, as the interface promises
		}
		OutProjectedCm = Result;
		return true;
	}
};

struct FElysiumRecordingServices final
	: public IElysiumEmbodiment
	, public IElysiumAudio
	, public IElysiumTravel
	, public IElysiumPresenter
	, public IElysiumWeather
{
	// The runtime addresses a model by unit id (`vtmb:model:<dir>/<base>`) or by its source path
	// (`models/<dir>/<base>.mdl`). The double records and keys by the base name: it is what a
	// source `.mdl` stem was, and what the literals every test asserts against read like.
	static FString StemOf(const FString& Model)
	{
		FString Base = Model;
		Base.RemoveFromStart(TEXT("vtmb:model:"));
		int32 Slash = INDEX_NONE;
		if (Base.FindLastChar(TEXT('/'), Slash)) Base = Base.Mid(Slash + 1);
		Base.RemoveFromEnd(TEXT(".mdl"));
		return Base;
	}

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
	float NpcMakerGroundZ = 0.0f;
	bool bUseNpcMakerGroundZ = false;
	bool bNpcMakerVisible = false;
	bool bNpcMakerInViewCone = false;
	bool bNpcMakerOccupied = false;
	FElysiumCameraShot LastCameraShot;
	// The placed model's `$attachment` table, as a case's own values. Empty is the honest headless
	// answer -- a terminal with no attachments refuses its session with the named error.
	TMap<FName, FTransform> BodyAttachments;
	// The world bounds of the registered use body, for retail's held-use reach test. Off by default:
	// a headless case has no body, and the reach test degenerates to "always pin".
	//
	// This stands for the **use ANCHOR's** box, not a render bound: `AElysiumMapActor` answers the
	// same question from `FUseAnchorRecord::Component` (the `ELYSIUM_USE_CHANNEL` proxy), because
	// retail measures the reach, the `WorldSpaceCenter()` snap target and the pin's sweep stop
	// against one collision box (`slice-bc-decompiles.md` §5.1/§5.2). A case that sets this is
	// describing the box the pin would stop at.
	FBox UseBodyBounds = FBox(ForceInit);
	bool bHasUseBodyBounds = false;
	// Where the next `SweepPlayerHullToward` reports contact, and whether it moves the player there.
	// Off by default: a sweep that found nothing to hit leaves the pawn where it was, and a case that
	// is not about the pin must not have its player teleported to the origin. A pin case sets both,
	// and the double then MOVES its player the way the map actor's `SetActorLocation` does, so
	// `GetPlayerUseOrigin` reports the pinned point on the next frame.
	bool bPlayerSweepMoves = false;
	FVector PlayerSweepContact = FVector::ZeroVector;
	// What the next modern interaction query returns. Geometry-specific tests control the adapter;
	// substrate tests remain pure and exercise focus/session policy over these records.
	FElysiumUseQueryResult UseQuery;
	TMap<FElysiumEntityHandle, bool> UseAnchorEnabled;
	TMap<FElysiumEntityHandle, bool> TouchAnchorEnabled;
	// Damage accumulated by DamagePlayer, so a trigger_hurt cadence is assertable as a number.
	float DamageTaken = 0.f;
	// Opt-in because most tests intentionally exercise the supported headless/no-motor path.
	bool bProvideNpcMotor = false;
	// Opt-in activity resolution mirrors the real manifest path. Default false preserves the
	// supported old-export fallback exercised by most Substrate tests.
	bool bNpcActivitiesResolve = false;
	FString ResolvedNpcActivityLabel = TEXT("walk");
	FString ResolvedNpcActivityClip = TEXT("walk_0");
	// The bank the include DAG named, which a one-shot producer addresses the cell through.
	FString ResolvedNpcActivityOwner = TEXT("move_and_ranged");
	float ResolvedNpcGroundSpeedCmPerSecond = 0.f;
	// The selected row's own loop bit, which a producer ORs into its own request. False by default:
	// the ambient and schedule callers ask for one-shots, and that is the shape most cases assert.
	bool ResolvedNpcActivityLoops = false;
	// The authored forward cells every motor this service builds answers with. Zero,
	// the default, is a body whose export resolves no fan: its travel requests fall back to the
	// stated `ElysiumNpcGait` constants, which is the path most Substrate cases exercise.
	float NpcWalkSpeedCmPerSecond = 0.f;
	float NpcRunSpeedCmPerSecond = 0.f;
	TArray<TUniquePtr<FElysiumRecordingNpcMotor>> NpcMotors;
	FElysiumRecordingNpcMotor* LastNpcMotor() const
	{
		return NpcMotors.IsEmpty() ? nullptr : NpcMotors.Last().Get();
	}

	// Stand a terminal's glass in front of this double's player, so the recovered screen cone
	// (`ElysiumTerminalCone`) passes: `screen` at `ScreenCm` with `screen_axis` 100 cm along
	// `ForwardXY`, and the player's eye `DistanceCm` out in front of it — the plan-view cosine is
	// then exactly 1. Call it BEFORE `Load`, because `FElysiumTerminal::Spawn` reads the pair.
	void StandTerminalScreen(const FVector& ScreenCm = FVector(0.0f, 0.0f, 120.0f),
		const FVector& ForwardXY = FVector(-1.0f, 0.0f, 0.0f), float DistanceCm = 300.0f)
	{
		const FVector Forward = ForwardXY.GetSafeNormal();
		BodyAttachments.Add(FName(TEXT("screen")), FTransform(ScreenCm));
		BodyAttachments.Add(FName(TEXT("screen_axis")), FTransform(ScreenCm + Forward * 100.0f));
		bHasPlayer = true;
		PlayerLocation = ScreenCm + Forward * DistanceCm;
	}

	// IElysiumEmbodiment.
	virtual float BodyScaleFor(const FElysiumEntityDef& Def) const override { return Def.bSky ? 16.f : 1.f; }

	// A body the CASE stood, handed to whatever entity asks for one. Null by default, which is every
	// Substrate case: those want the bare component below. A Content case that needs the real pose
	// layer under the substrate's own pass — a graph-backed body on a baked mesh — sets this, and
	// the entity chain then drives that body instead of a stand-in it cannot animate.
	USkeletalMeshComponent* PrebuiltNpcVisual = nullptr;

	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildNpcVisual %s %s scale=%.2f disp=%s var=%d"),
			*StemOf(Stem), *Location.ToString(), UniformScale, *Disposition, IdleVariant));
		if (PrebuiltNpcVisual != nullptr)
		{
			return PrebuiltNpcVisual;
		}
		// A real component (transient, never registered — no RHI is touched) rather than null, so
		// the leaf classes take their body-carrying path: they register it for teardown, gate it on
		// dormancy, and route SetAnimation/SetDisposition through it.
		return NewComponent<USkeletalMeshComponent>();
	}
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FElysiumEntityHandle& EntityOwner, const FVector& FeetOrigin, float YawDegrees,
		const FString& Stem, int32 Variant) override
	{
		if (!bProvideNpcMotor || !Body)
		{
			return nullptr;
		}
		TUniquePtr<FElysiumRecordingNpcMotor> Motor = MakeUnique<FElysiumRecordingNpcMotor>();
		Motor->Calls = &Calls;
		Motor->Owner = EntityOwner;
		Motor->Feet = FeetOrigin;
		Motor->Yaw = YawDegrees;
		Motor->AuthoredWalkSpeedCmPerSecond = NpcWalkSpeedCmPerSecond;
		Motor->AuthoredRunSpeedCmPerSecond = NpcRunSpeedCmPerSecond;
		FElysiumRecordingNpcMotor* Result = Motor.Get();
		NpcMotors.Add(MoveTemp(Motor));
		Record(FString::Printf(TEXT("BuildNpcMotor %s yaw=%.1f stem=%s var=%d"),
			*FeetOrigin.ToString(), YawDegrees, *StemOf(Stem), Variant));
		return Result;
	}
	virtual void DestroyNpcMotor(IElysiumNpcMotor*) override
	{
		Record(TEXT("DestroyNpcMotor"));
	}
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 DispositionLevel, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("RefreshNpcIdle %s disp=%s level=%d var=%d"),
			*StemOf(Stem), *Disposition, DispositionLevel, IdleVariant));
		return Body != nullptr;
	}
	// The stance set a test hands the machine. Empty by default, which is the "this model carries no
	// stance clips" answer — a test that wants the machine to run fills `StanceClips` first, the same
	// way `ResolvedNpcActivityLabel` seeds the activity resolver.
	FElysiumStanceClips StanceClips;
	virtual bool ResolveStanceClips(const FString& Stem, const FString& AnimName,
		FElysiumStanceClips& OutClips) override
	{
		Record(FString::Printf(TEXT("ResolveStanceClips %s anim=%s"), *StemOf(Stem), *AnimName));
		OutClips = StanceClips;
		return OutClips.IsValid();
	}
	// The disposition row the machine is tuned by. Default-constructed is a row with an empty name,
	// which `IsValid()` rejects — a test that wants the machine to run seeds this the same way it
	// seeds `StanceClips`, so the literals it asserts against are visible in the test body.
	FElysiumDisposition DispositionRow;
	// Optional name+level rows for a test that needs to resolve a transition between two different
	// dispositions. The single-row fixture above remains the common-case fallback.
	TMap<FString, FElysiumDisposition> DispositionRows;
	virtual bool ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
		FElysiumDisposition& OutRow) override
	{
		Record(FString::Printf(TEXT("ResolveDisposition %s %d"), *Disposition, DispositionLevel));
		const FString Key = FString::Printf(TEXT("%s|%d"), *Disposition.ToLower(), DispositionLevel);
		const FElysiumDisposition* Named = DispositionRows.Find(Key);
		OutRow = Named != nullptr ? *Named : DispositionRow;
		return OutRow.IsValid();
	}

	// --- A2 (footsteps): the surface sound table ------------------------------------------
	// The rows a test authors for the surfaces its bodies stand on. An unlisted name answers false
	// — the headless answer stated on the interface, and retail's null `surfacedata_t`.
	TMap<FName, FElysiumSurfaceSounds> SurfaceSounds;
	virtual bool ResolveSurfaceSounds(FName Surface, FElysiumSurfaceSounds& Out) const override
	{
		const FElysiumSurfaceSounds* Row = SurfaceSounds.Find(Surface);
		Record(FString::Printf(TEXT("ResolveSurfaceSounds %s -> %s"), *Surface.ToString(),
			Row != nullptr ? TEXT("yes") : TEXT("no")));
		if (Row == nullptr)
		{
			return false;
		}
		Out = *Row;
		return true;
	}
	// `TASK_WAIT_PVS`'s answer, settable so a test drives both branches. True by default because
	// that is what a headless run means: the question has no renderer to answer it.
	bool bNpcBodyVisible = true;
	virtual bool IsNpcBodyVisible(USkeletalMeshComponent* Body) override
	{
		return bNpcBodyVisible;
	}
	// The band and the hold ride at the TAIL of the line, after the tokens every existing
	// case matches on: `Saw` is a prefix match, so a run's band is readable by a case that wants it
	// without moving the ground under one that does not.
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FElysiumClipSegment& Segment, float* OutSeconds) override
	{
		// `rate=`, `act=` and `ch=` ride at the very tail, after the band and the hold, for the reason
		// those two do: `Saw` is a prefix match, so a case that wants the forced ideal activity, the
		// playback rate or the channel can ask for it without moving the ground under one that does
		// not. `act=` is the FORCED IDEAL ACTIVITY the segment carries, and it is recorded because it
		// is the value the movement lock, the reselection guard and the air self-latch all read — a
		// producer that stopped stating it would otherwise still play its clip and look identical
		// here. `ch=` is which of retail's two mechanisms the producer asked for: a base pose that
		// REPLACES, or a `CBaseAnimatingOverlay` slot 0 layer that composes over it. A ranged fire on
		// the base channel collapses the body, and nothing else in this line would say so.
		Record(FString::Printf(TEXT("PlayNpcClip %s %s loop=%d band=%s%s rate=%.2f act=%s ch=%s"),
			*StemOf(Stem), *Segment.ClipName, Segment.bLoop ? 1 : 0,
			ElysiumAnimIntent::PriorityName(Segment.Priority),
			Segment.bHoldUntilReleased ? TEXT(" held=1") : TEXT(""), Segment.PlaybackRate,
			Segment.Activity.IsEmpty() ? TEXT("(none)") : *Segment.Activity,
			ElysiumAnimIntent::ChannelName(Segment.Channel)));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;   // a beat's OnEndSequence schedules off this
		}
		if (Body != nullptr && Segment.bHoldUntilReleased)
		{
			bNpcSegmentHeld = true;
		}
		return Body != nullptr;
	}

	// Whether this fixture's body is holding a montage-slot RUN claim, modelled rather than
	// only recorded: the whole point of the run bracket is that every stop path gives the claim back,
	// and a double that only logged the calls could not say whether one was left standing.
	bool bNpcSegmentHeld = false;
	virtual void ReleaseNpcSegment(USkeletalMeshComponent* Body) override
	{
		Record(FString::Printf(TEXT("ReleaseNpcSegment body=%d"), Body != nullptr ? 1 : 0));
		bNpcSegmentHeld = false;
	}
	virtual bool PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadNpcClip %s %s"), *StemOf(Stem), *ClipName));
		return Body != nullptr;
	}
	virtual bool PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadNpcClipForModel %s player=%d %s"),
			*StemOf(Stem), bPlayerMaterial ? 1 : 0, *ClipName));
		return !Stem.IsEmpty() && !ClipName.IsEmpty();
	}
	virtual bool ResolveNpcActivityClip(const FElysiumActivityClipRequest& Request,
		FElysiumActivityClip& Out) override
	{
		// The stem and the activity stay the first two fields — `Saw` is a prefix match — and the
		// chain stays the LAST token, so a reader that splits on `body=` takes it whole. The
		// classification the producer filled sits between them: it is what makes the class body, the
		// weapon ladder and the armed/alert branch reachable, and a request that dropped it would
		// resolve past all three with nothing to read.
		Record(FString::Printf(
			TEXT("ResolveNpcActivityClip %s %s var=%d class=%s weapon=%s state=%s hit=%.1f ")
			TEXT("buttons=%d body=%s"),
			*StemOf(Request.Stem), *Request.Activity, Request.Variant,
			Request.ActorClassname.IsEmpty() ? TEXT("-") : *Request.ActorClassname,
			Request.WeaponClassname.IsEmpty() ? TEXT("-") : *Request.WeaponClassname,
			LexToString(Request.ActorState), Request.HitYaw, Request.StateMask,
			ElysiumAnimIntent::BodyKindName(Request.BodyKind)));
		Out = FElysiumActivityClip();
		if (!bNpcActivitiesResolve)
		{
			return false;
		}
		Out.Label = ResolvedNpcActivityLabel;
		Out.AnimationName = ResolvedNpcActivityClip;
		Out.OwnerStem = ResolvedNpcActivityOwner;
		Out.GroundSpeedCmPerSecond = ResolvedNpcGroundSpeedCmPerSecond;
		Out.bLooping = ResolvedNpcActivityLoops;
		// The authored fade rides the resolved clip, so a producer that states no blend reads it here.
		// Carried by the double rather than left at the struct default: a seam field the stub never
		// writes is a field whose producer cannot be asserted at all.
		Out.FadeSeconds = ResolvedNpcActivityFadeSeconds;
		// The translated activity's acquisition distance, carried for the same reason as the fade: a
		// seam field the stub never writes is a field whose consumer cannot be asserted at all, and a
		// melee swing reads this one to decide who it reserves.
		Out.MaxReachCm = ResolvedNpcActivityMaxReachCm;
		// The fan half. The axis value is the request's own hit yaw rather than a fixture
		// constant: a producer that dropped it would answer every reaction at the fan's forward cell,
		// and a stub that invented an angle would hide exactly that.
		Out.bGrid = bResolvedNpcActivityIsGrid;
		Out.AxisValue = Request.HitYaw;
		Out.NextAnimationName = ResolvedNpcActivityNextClip;
		Out.AxisFraction = ResolvedNpcActivityFraction;
		return true;
	}
	// Whether the resolved label names a fan, and the pair it names when it does. Off by default,
	// because most Substrate cases stand a body whose activity resolves one animation.
	bool bResolvedNpcActivityIsGrid = false;
	FString ResolvedNpcActivityNextClip;
	float ResolvedNpcActivityFraction = 0.0f;
	// The fade the resolved clip reports. 0.2 is what 5,762 of the 5,836 shipped sequences carry.
	float ResolvedNpcActivityFadeSeconds = 0.2f;
	// The maximum reach the resolved clip's activity reports, in centimetres. Zero is the default
	// because it is the honest answer for a vocabulary that authors no reach column at all, and it is
	// what puts a swing on the stated stand-in distance — the path most Substrate cases exercise.
	float ResolvedNpcActivityMaxReachCm = 0.0f;
	// Whether a one-shot request is played, and the length it reports. Opt-in like every
	// other fixture flag: default false is the body that resolves no clip, which is what most
	// Substrate cases stand.
	bool bNpcOneShotsPlay = false;
	float OneShotSeconds = 1.0f;
	virtual bool PlayNpcOneShot(USkeletalMeshComponent* Body,
		const FElysiumOneShotClipRequest& Request, float* OutSeconds) override
	{
		// The three fields sit BEFORE `prio=`, which several suites already split on as the
		// line's tail: a reader that took the last token would otherwise start reading the axis. The
		// release condition is appended AFTER it for the same reason, in the other direction.
		Record(FString::Printf(
			TEXT("PlayNpcOneShot %s %s loop=%d in=%.2f out=%.2f route=%s grid=%d axis=%.1f prio=%s "
				"release=%s"),
			*Request.OwnerStem, *Request.AnimationName, Request.bLoop ? 1 : 0,
			Request.BlendInSeconds, Request.BlendOutSeconds,
			Request.Route == EElysiumOneShotRoute::Reaction ? TEXT("reaction") : TEXT("slot"),
			Request.bGrid ? 1 : 0, Request.AxisValue,
			ElysiumAnimIntent::PriorityName(Request.Priority),
			ElysiumAnimIntent::ReactionReleaseName(Request.Release)));
		if (OutSeconds != nullptr)
		{
			// A HELD claim has no duration, and the double says so rather than answering the fixture's
			// nominal clip length: a caller that scheduled off a held reaction's "length" would be
			// scheduling against a number the real seam does not produce.
			*OutSeconds = Request.Route == EElysiumOneShotRoute::Reaction
				&& Request.Release == EElysiumReactionRelease::Predicate
				? 0.0f : OneShotSeconds;
		}
		const bool bPlayed = bNpcOneShotsPlay && Body != nullptr;
		// The visibility tail, through the SAME named rule the real body factory branches on rather
		// than a copy of its condition — a hidden body must survive an involuntary reaction, and that
		// is only assertable headless if the double answers off the rule instead of mirroring it.
		if (bPlayed && ElysiumAnimIntent::OneShotForcesVisibility(Request.Route))
		{
			Body->SetVisibility(true, true);
		}
		// The held claim, MODELLED rather than only recorded — the producer polls it back, so a double
		// that always answered the same thing would prove nothing about the resume.
		if (bPlayed && Request.Route == EElysiumOneShotRoute::Reaction
			&& Request.Release == EElysiumReactionRelease::Predicate)
		{
			bNpcReactionHeld = true;
		}
		return bPlayed;
	}

	// The held reaction claim.
	//
	// The body-side half of a `Predicate` play, in the smallest form a Substrate case needs: whether
	// this fixture's body is holding one, and whether anything else stands on the base channel. The
	// real arbitration is the driver's (`Elysium.Substrate.Animation`); what is modelled here is only
	// the ANSWER a producer polls, so a case can put the fixture into each of the three states.
	bool bNpcReactionHeld = false;
	// Whether the base channel is free once the hold is gone. A case sets it false to stand in for a
	// still-playing preemptor — the blocked hit's own `ACT_BLOCK`, which takes the channel at the same
	// Reaction band and must not be cut short by a resume.
	bool bNpcReactionChannelFree = true;
	// What the body factory's own preemption hook does: drop the record without releasing a claim that
	// is already gone. The one door a case should use to simulate being outranked.
	void PreemptNpcReaction() { bNpcReactionHeld = false; }

	// The release half of a held reaction claim.
	virtual void ReleaseNpcReaction(USkeletalMeshComponent* Body) override
	{
		Record(FString::Printf(TEXT("ReleaseNpcReaction body=%d"), Body != nullptr ? 1 : 0));
		bNpcReactionHeld = false;
	}

	virtual EElysiumHeldReactionState QueryNpcReactionHold(
		USkeletalMeshComponent* Body) const override
	{
		const EElysiumHeldReactionState State = bNpcReactionHeld
			? EElysiumHeldReactionState::Held
			: (bNpcReactionChannelFree ? EElysiumHeldReactionState::Free
			                           : EElysiumHeldReactionState::Displaced);
		Record(FString::Printf(TEXT("QueryNpcReactionHold body=%d -> %s"),
			Body != nullptr ? 1 : 0, ElysiumAnimIntent::HeldReactionStateName(State)));
		return State;
	}

	// The death handoff.
	// Whether this fixture's bodies carry a physics asset. FALSE by default, and that default is the
	// shipped answer rather than a convenience: the character bake writes no physics asset, so every
	// death in the game today takes the frozen-final-pose arm. A case that wants the ragdoll arm has
	// to say so.
	bool bBodiesRagdoll = false;
	virtual void ReleaseBodyAnimClaims(USkeletalMeshComponent* Body) override
	{
		Record(FString::Printf(TEXT("ReleaseBodyAnimClaims body=%d"), Body != nullptr ? 1 : 0));
		// Death ends every claim at once, the run's included — a corpse holding a beat's segment claim
		// is exactly the leak the wholesale release exists to close.
		bNpcReactionHeld = false;
		bNpcSegmentHeld = false;
	}
	virtual bool StartBodyRagdoll(USkeletalMeshComponent* Body) override
	{
		const bool bStarted = bBodiesRagdoll && Body != nullptr;
		Record(FString::Printf(TEXT("StartBodyRagdoll -> %d"), bStarted ? 1 : 0));
		return bStarted;
	}
	virtual void HoldBodyFinalPose(USkeletalMeshComponent* Body) override
	{
		Record(FString::Printf(TEXT("HoldBodyFinalPose body=%d"), Body != nullptr ? 1 : 0));
	}

	// The sequence-event seam.
	//
	// The phase is a settable CURRENT record rather than a scripted sequence of them, matching every
	// other fixture in this file: a suite drives the pass frame by frame anyway, so mutating
	// `BodyClipPhase.Cycle` between calls says exactly what a scripted list would and lets a case
	// re-arm, seek or stop mid-run without rewriting the script. Off by default — the ordinary
	// Substrate body stands on nothing this seam can see.
	bool bBodyClipPhaseSet = false;
	FElysiumClipPhase BodyClipPhase;
	// The other half, for a Content case standing a REAL animation host: ask that host, exactly the
	// two lines `UElysiumEntityBodies::GetBodyClipPhase` performs. It is opt-in rather than automatic
	// because the fixture above is what every Substrate case drives, and a body whose host answers
	// for itself cannot be scripted frame by frame.
	bool bLiveClipPhase = false;
	// Deliberately not recorded: the world's event pass asks this of every bodied entity every
	// frame, and a line per body per frame would bury every call a suite is actually reading.
	virtual bool GetBodyClipPhase(USkeletalMeshComponent* Body, EElysiumAnimChannel Channel,
		FElysiumClipPhase& Out) override
	{
		Out = FElysiumClipPhase();
		if (Body == nullptr)
		{
			return false;
		}
		if (bLiveClipPhase)
		{
			const UElysiumBodyAnimInstance* Inst =
				Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance());
			return Inst != nullptr && Inst->GetClipPhase(Channel, Out);
		}
		if (!bBodyClipPhaseSet || BodyClipPhase.Channel != Channel)
		{
			// **The scripted record stands on ONE channel, and every other channel is empty.** A body
			// really does stand on its base pose and on the overlay slot at the same time — the two are
			// different clips composed together — so a double that answered the same record for every
			// channel would describe a body that cannot exist, and the event pass would walk one
			// timeline twice and fire every record on it once per polled channel. The record's own
			// `Channel` is the one it stands on, and it defaults to the base pose, which is what every
			// case that never states one means.
			return false;
		}
		Out = BodyClipPhase;
		return true;
	}
	// The timelines a fixture declares, keyed `<owner>|<label>` and matched case-insensitively the
	// way the real sidecar's own map is. Absent is the ordinary answer: most sequences declare none.
	TMap<FString, TArray<FElysiumAnimEvent>> NpcEventTimelines;
	static FString EventTimelineKey(const FString& OwnerStem, const FString& Label)
	{
		return FString::Printf(TEXT("%s|%s"), *OwnerStem.ToLower(), *Label.ToLower());
	}
	virtual const TArray<FElysiumAnimEvent>* GetNpcEventTimeline(const FString& OwnerStem,
		const FString& Label, const FString& OwnerRoot = FString()) override
	{
		return NpcEventTimelines.Find(EventTimelineKey(OwnerStem, Label)
			+ (OwnerRoot.IsEmpty()?FString():TEXT("|")+OwnerRoot.ToLower()));
	}

	// The ornament rows this fixture pretends `DA_OrnamentModels` carries, keyed by the
	// retail-formatted path exactly as the real catalogue is. A path that is not here is the
	// "mock, don't fail" case: the seam answers false and the slot stays empty, which is the state
	// retail's own `GetModelPtr`-null tail leaves behind.
	TSet<FString> OrnamentModels;
	// The path each body is currently wearing, so a test can assert the SLOT and not merely the
	// call trace. Absent means nothing is worn.
	TMap<FObjectKey, FString> WornOrnaments;
	FString WornOrnament(USkeletalMeshComponent* Body) const
	{
		const FString* Found = WornOrnaments.Find(FObjectKey(Body));
		return Found ? *Found : FString();
	}
	virtual bool AttachOrnamentModel(USkeletalMeshComponent* Body, const FString& RetailPath) override
	{
		Record(FString::Printf(TEXT("AttachOrnamentModel %s"), *RetailPath));
		// Retail removes the standing follow model FIRST and unconditionally, so a refused attach
		// still leaves the slot empty.
		WornOrnaments.Remove(FObjectKey(Body));
		if (!OrnamentModels.Contains(RetailPath))
		{
			return false;
		}
		WornOrnaments.Add(FObjectKey(Body), RetailPath);
		return true;
	}
	virtual void DetachOrnamentModel(USkeletalMeshComponent* Body) override
	{
		Record(TEXT("DetachOrnamentModel"));
		WornOrnaments.Remove(FObjectKey(Body));
	}

	// The label-route sibling's fixture, mirroring bNpcActivitiesResolve above: opt-in so most
	// Substrate tests keep exercising the supported "no motion for this exact clip" fallback.
	bool bNpcSequenceClipsResolve = false;
	FString ResolvedNpcSequenceAnimName = TEXT("walk_0");
	float ResolvedNpcSequenceGroundSpeedCmPerSecond = 0.f;
	virtual bool ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
		EElysiumAnimBodyKind BodyKind, FString& OutAnimName,
		float& OutGroundSpeedCmPerSecond) override
	{
		Record(FString::Printf(TEXT("ResolveNpcSequenceClip %s %s body=%s"), *StemOf(Stem), *ClipName,
			ElysiumAnimIntent::BodyKindName(BodyKind)));
		OutAnimName = bNpcSequenceClipsResolve ? ResolvedNpcSequenceAnimName : FString();
		OutGroundSpeedCmPerSecond = bNpcSequenceClipsResolve
			? ResolvedNpcSequenceGroundSpeedCmPerSecond : 0.f;
		return bNpcSequenceClipsResolve;
	}
	// The clip vocabulary a test seeds for HasNpcClip, keyed by lower-cased stem. Empty by default,
	// so an unseeded probe answers "not authored" -- the ordinary case for a cross-disposition
	// stance transition, which is the one caller this exists for.
	TMap<FString, TSet<FString>> KnownNpcClips;
	virtual bool HasNpcClip(const FString& Stem, const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("HasNpcClip %s %s"), *StemOf(Stem), *ClipName));
		const TSet<FString>* Known = KnownNpcClips.Find(StemOf(Stem).ToLower());
		return Known != nullptr && Known->Contains(ClipName);
	}
	// The blocked-reaction column a test authors, keyed by lower-cased clip label. Empty by default:
	// most shipped sequences name none, and the producer's own fallback is what an empty answer
	// selects. Keyed by label alone rather than by (stem, label) because a Substrate case stands one
	// vocabulary — the STEM still rides the recorded line, so a producer that keyed off the wrong
	// body is still visible.
	TMap<FString, FString> BlockedReactionByClip;
	virtual FString NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel) override
	{
		const FString* Found = BlockedReactionByClip.Find(ClipLabel.ToLower());
		Record(FString::Printf(TEXT("NpcClipBlockedReaction %s %s -> %s"), *StemOf(Stem), *ClipLabel,
			Found != nullptr ? **Found : TEXT("-")));
		return Found != nullptr ? *Found : FString();
	}
	// The `swings` column a test authors, keyed by lower-cased clip label for the same reason the
	// blocked reaction is: a Substrate case stands one vocabulary, and the stem still rides the
	// recorded line. Empty by default, which is what all but 574 shipped descriptors declare and
	// also what an export predating the column gives every clip — so an unseeded case exercises the
	// "no records, no contact" path without arranging anything.
	TMap<FString, TArray<FElysiumSwingRecord>> SwingsByClip;
	virtual const TArray<FElysiumSwingRecord>* NpcClipSwings(const FString& Stem,
		const FString& ClipLabel) override
	{
		const TArray<FElysiumSwingRecord>* Found = SwingsByClip.Find(ClipLabel.ToLower());
		Record(FString::Printf(TEXT("NpcClipSwings %s %s -> %d"), *StemOf(Stem), *ClipLabel,
			Found != nullptr ? Found->Num() : 0));
		return (Found != nullptr && !Found->IsEmpty()) ? Found : nullptr;
	}
	// The `combo` column a test authors, keyed by lower-cased clip label on the same terms as the
	// three columns above. Empty by default, which is what all but 208 shipped descriptors declare
	// and what an export predating the column gives every clip — so an unseeded case exercises the
	// "terminal attack, no hand-off" path without arranging anything.
	TMap<FString, FElysiumComboChain> ComboByClip;
	virtual const FElysiumComboChain* NpcClipCombo(const FString& Stem,
		const FString& ClipLabel) override
	{
		const FElysiumComboChain* Found = ComboByClip.Find(ClipLabel.ToLower());
		Record(FString::Printf(TEXT("NpcClipCombo %s %s -> %s"), *StemOf(Stem), *ClipLabel,
			Found != nullptr ? TEXT("stated") : TEXT("-")));
		return (Found != nullptr && Found->bStated) ? Found : nullptr;
	}
	// The owning bank a test declares per label, lower-cased. An unseeded label answers EMPTY, which
	// is `LookupSequence` returning -1 — the dangling-chain case — so this is opt-in rather than
	// defaulting to the fixture's own bank.
	TMap<FString, FString> ClipOwnerByLabel;
	virtual FString NpcClipOwner(const FString& Stem, const FString& ClipLabel) override
	{
		const FString* Found = ClipOwnerByLabel.Find(ClipLabel.ToLower());
		Record(FString::Printf(TEXT("NpcClipOwner %s %s -> %s"), *StemOf(Stem), *ClipLabel,
			Found != nullptr ? **Found : TEXT("-")));
		return Found != nullptr ? *Found : FString();
	}
	// The bone frames a test places, keyed by lower-cased bone name. A body whose bone is unseeded
	// answers false, which is the missing-bone guard's own case.
	TMap<FString, FTransform> BoneFrames;
	virtual bool GetBodyAttachment(const FElysiumEntityHandle& Owner, FName Attachment,
		FTransform& OutWorld) const override
	{
		const FTransform* Found = BodyAttachments.Find(Attachment);
		Record(FString::Printf(TEXT("GetBodyAttachment %s -> %s"), *Attachment.ToString(),
			Found ? *Found->GetLocation().ToCompactString() : TEXT("<none>")));
		if (!Found)
		{
			return false;
		}
		OutWorld = *Found;
		return true;
	}
	virtual bool GetUseBodyWorldBounds(const FElysiumEntityHandle& Owner, FBox& OutWorld) const override
	{
		if (!bHasUseBodyBounds)
		{
			return false;
		}
		OutWorld = UseBodyBounds;
		return true;
	}
	virtual bool GetBodyBoneTransform(USkeletalMeshComponent* Body, const FString& BoneName,
		FTransform& OutWorld) const override
	{
		OutWorld = FTransform::Identity;
		const FTransform* Found = BoneFrames.Find(BoneName.ToLower());
		Record(FString::Printf(TEXT("GetBodyBoneTransform %s -> %s"), *BoneName,
			Found != nullptr ? TEXT("placed") : TEXT("-")));
		if (Body == nullptr || Found == nullptr)
		{
			return false;
		}
		OutWorld = *Found;
		return true;
	}
	// What the swing sweep answers. `SwingContactSweeps` accumulates the segments it was asked
	// about, so a case can assert WHERE the walk swept as well as that it swept at all. Deliberately
	// not `Record`ed: the walk asks once per live record per sub-step, and a hundred lines a frame
	// would bury everything a suite reads.
	mutable TArray<FElysiumSwingSweep> SwingContactSweeps;

	// The standing answer, returned verbatim for every sub-step. It is what a case uses when the
	// question is the WALK — which records opened, how the hit-once spread behaved, whether a batch
	// ran at all — and geometry would only be scaffolding in the way.
	TArray<FElysiumEntityHandle> SwingContacts;

	// A body the sweep can actually reach, keyed by handle: the feet-anchored box a case places so
	// the swept segment has something to intersect. Non-empty switches this query from the standing
	// answer to a real geometric one.
	//
	// **Why the double sweeps rather than only recording.** The walk's sub-step batching decides
	// WHERE a limb is at each of `floor(span * 100)` instants, and a stub that answers the same list
	// however the segment moved cannot tell a correct interpolation from a stationary one — the
	// batching was provable in the arena and nowhere else. With a box in the way the substrate tier
	// asks the same question the map actor does.
	TMap<FElysiumEntityHandle, FBox> SwingBodies;

	// The 32x32x72-unit character box, in the same feet-anchored form the map actor builds for a
	// bodiless candidate: origin-half to origin+half+height, NOT centred on the origin.
	static FBox StandHullAt(const FVector& FeetOriginCm)
	{
		const FVector Half(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.0f);
		return FBox(FeetOriginCm - Half,
			FeetOriginCm + Half + FVector(0.0f, 0.0f, ElysiumMove::StandHeight));
	}
	void PlaceSwingBody(const FElysiumEntityHandle& Body, const FVector& FeetOriginCm)
	{
		SwingBodies.Add(Body, StandHullAt(FeetOriginCm));
	}

	virtual void QuerySwingContacts(const FElysiumSwingSweep& Sweep,
		TArray<FElysiumEntityHandle>& OutHits) const override
	{
		SwingContactSweeps.Add(Sweep);
		if (SwingBodies.IsEmpty())
		{
			OutHits = SwingContacts;
			return;
		}
		// The same four edges `AElysiumMapActor::QuerySwingContacts` bounds the swept patch with:
		// the segment where the sub-step started, where it ended, and the path each endpoint took
		// between. Testing the edges rather than solving the bilinear patch is the production
		// query's own choice, and the double has to make the same one or it would answer a question
		// the real seam does not.
		const FVector Edges[4][2] = {
			{ Sweep.PrevA, Sweep.PrevB },
			{ Sweep.CurA,  Sweep.CurB  },
			{ Sweep.PrevA, Sweep.CurA  },
			{ Sweep.PrevB, Sweep.CurB  },
		};
		OutHits.Reset();
		for (const TPair<FElysiumEntityHandle, FBox>& Body : SwingBodies)
		{
			if (Body.Key == Sweep.Attacker)
			{
				continue;   // a swing never reaches its own swinger, same as the real query
			}
			for (const FVector (&Edge)[2] : Edges)
			{
				if (FMath::LineBoxIntersection(Body.Value, Edge[0], Edge[1], Edge[1] - Edge[0]))
				{
					OutHits.Add(Body.Key);
					break;
				}
			}
		}
		// **No occlusion trace, and that is the stated divergence.** The real query then asks the
		// engine whether solid world stands between the limb and the body. A headless world has no
		// geometry to answer with, so this reports every geometric reach — which is the permissive
		// direction: a case can prove a contact landed, never that a wall stopped one.
	}
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayCinematicClip %s %s %s %s"), *StemOf(Stem), *AnimSetModel,
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
		Record(FString::Printf(TEXT("PreloadCinematicClip %s %s %s %s"), *StemOf(Stem),
			*AnimSetModel, *BoneRoot, *ClipName));
		return bCinematicClipsResolve && Body != nullptr;
	}
	virtual bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) override
	{
		Record(FString::Printf(TEXT("PreloadCinematicClipForModel %s player=%d %s %s %s"),
			*StemOf(Stem), bPlayerMaterial ? 1 : 0, *AnimSetModel, *BoneRoot, *ClipName));
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
	virtual void ReleaseCinematicClaim(USkeletalMeshComponent*) override { Record(TEXT("ReleaseCinematicClaim")); }

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
	virtual bool PlayAttachedEffect(USkeletalMeshComponent* Body, const FString& Definition,
		FName Attachment) override
	{
		Record(FString::Printf(TEXT("PlayAttachedEffect %s %s"),
			*Definition, *Attachment.ToString()));
		return Body != nullptr && !Definition.IsEmpty() && !Attachment.IsNone();
	}

	// The per-model phoneme filter this fake cast answers with. Per body, because the point of
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

	// The gaze seam, recorded rather than drawn. The head frame is test-controlled so a
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
		const FVector& Location, const FQuat& Rotation, float UniformScale,
		int32 PlacementToken = 0) override
	{
		// The rotation is recorded last so the existing prefix assertions keep matching.
		Record(FString::Printf(TEXT("BuildAnimatedPropVisual %s %s scale=%.2f rot=%s"),
			*StemOf(Stem), *Location.ToString(), UniformScale, *Rotation.Rotator().ToString()));
		LastAnimatedPropRotation = Rotation;
		USkeletalMeshComponent* Body = NewComponent<USkeletalMeshComponent>();
		AnimatedPropBodies.Add(StemOf(Stem), Body);
		return Body;
	}
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayAnimatedPropClip %s %s loop=%d"),
			*StemOf(Stem), *ClipName, bLoop ? 1 : 0));
		if (OutSeconds) { *OutSeconds = ClipSeconds; }
		return Body != nullptr;
	}
	virtual int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
		const FString& Stem) override
	{
		Record(FString::Printf(TEXT("PreloadAnimatedPropClips %s"), *StemOf(Stem)));
		return Body != nullptr ? 1 : 0;
	}
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent*, const FString& StaticStem,
		int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyAnimatedPropSkin %s family=%d"), *StemOf(StaticStem), Family));
	}
	virtual FString AnimatedPropRestClip(const FString& Stem,
		int32 PlacementToken = 0) const override
	{
		(void)PlacementToken;
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
			*StemOf(Stem), UniformScale, bSky ? 1 : 0));
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
	// Whether a cinematic anim set resolves. Default false — a scene must run its timeline and
	// outputs either way.
	bool bCinematicClipsResolve = false;
	// Opt-in so existing body-path tests continue to exercise the legacy-index fallback. The v7
	// closure tests enable this and receive a real composite with distinct visual/proxy components.
	bool bPlacedModelsResolve = false;
	virtual bool HasPlacedModelCatalogue() const override { return bPlacedModelsResolve; }
	virtual FElysiumPlacedModelBody BuildPlacedModelBody(
		const FElysiumPlacedModelRequest& Request) override
	{
		Record(FString::Printf(TEXT("BuildPlacedModelBody %s token=%d skin=%d physics=%d"),
			*Request.ModelPath, Request.PlacementToken, Request.Skin,
			static_cast<int32>(Request.Physics)));
		FElysiumPlacedModelBody Result;
		if (!bPlacedModelsResolve)
		{
			return Result;
		}
		Result.Stem = FPaths::GetBaseFilename(Request.ModelPath).ToLower();
		Result.Visual = NewComponent<USkeletalMeshComponent>();
		if (Request.Physics == EElysiumPlacedModelPhysics::None)
		{
			Result.Attach = Result.Visual;
		}
		else
		{
			Result.PhysicsProxy = NewComponent<UStaticMeshComponent>();
			Result.Attach = Result.PhysicsProxy;
		}
		return Result;
	}

	// The body each static prop stem was built onto — mirrors AnimatedPropBodies, so a solid/
	// disableshadows test can inspect the live component the ordinary Record() string can't carry
	// (collision-enabled state, cast-shadow flag, an attached box collision proxy).
	TMap<FString, UStaticMeshComponent*> PropBodies;

	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPropVisual %s %s scale=%.2f"), *StemOf(Stem), *Location.ToString(), UniformScale));
		LastPropRotation = Rotation;
		UStaticMeshComponent* Body = NewComponent<UStaticMeshComponent>();
		PropBodies.Add(StemOf(Stem), Body);
		return Body;
	}
	TMap<FString, EElysiumItemGroundModelState> ItemGroundModelStates;
	virtual EElysiumItemGroundModelState ItemGroundModelState(
		const FString& ModelPath) override
	{
		if (const EElysiumItemGroundModelState* State = ItemGroundModelStates.Find(ModelPath))
		{
			return *State;
		}
		return EElysiumItemGroundModelState::Geometry;
	}
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPhysPropVisual %s %s scale=%.2f"), *StemOf(Stem), *Location.ToString(), UniformScale));
		return NewComponent<UStaticMeshComponent>();
	}
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyPropSkin %s family=%d"), *StemOf(Stem), Family));
	}
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildPlayerVisual %s disp=%s var=%d"),
			*StemOf(Stem), *Disposition, IdleVariant));
		return Stem.IsEmpty() ? nullptr : NewComponent<USkeletalMeshComponent>();
	}
	virtual void ClearPlayerVisual() override
	{
		Record(TEXT("ClearPlayerVisual"));
	}
	virtual void SetPlayerBodyEntityHidden(bool bHidden) override
	{
		bPlayerBodyEntityHidden = bHidden;
		Record(FString::Printf(TEXT("SetPlayerBodyEntityHidden %d"), bHidden ? 1 : 0));
	}
	// The last entity-side gate the player pushed. The camera's half is not modelled here — it is a
	// pure function asserted in `Elysium.Substrate.CameraDraw` with no world at all.
	bool bPlayerBodyEntityHidden = false;

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
	virtual bool SweepPlayerHullToward(const FVector& TargetCm, FVector& OutContactCm) override
	{
		const FVector Contact = bPlayerSweepMoves ? PlayerSweepContact : PlayerLocation;
		Record(FString::Printf(TEXT("SweepPlayerHullToward %s -> %s"),
			*TargetCm.ToCompactString(), *Contact.ToCompactString()));
		if (!bHasPlayer)
		{
			return false;
		}
		OutContactCm = Contact;
		PlayerLocation = Contact;
		return true;
	}
	virtual bool SnapPlayerViewTo(const FVector& TargetCm) override
	{
		Record(FString::Printf(TEXT("SnapPlayerViewTo %s"), *TargetCm.ToCompactString()));
		if (!bHasPlayer)
		{
			return false;
		}
		const FVector Direction = TargetCm - PlayerLocation;
		if (Direction.IsNearlyZero())
		{
			return false;
		}
		PlayerRotation = Direction.Rotation();
		return true;
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
	virtual void UnregisterUseAnchor(const FElysiumEntityHandle& Owner) override
	{
		UseAnchorEnabled.Remove(Owner);
		Record(FString::Printf(TEXT("UnregisterUseAnchor %s"), *Owner.ToString()));
	}
	virtual void ClearUseAnchors() override
	{
		UseAnchorEnabled.Reset();
		Record(TEXT("ClearUseAnchors"));
	}
	virtual void RegisterTouchAnchor(UPrimitiveComponent*, const FElysiumEntityHandle& Owner) override
	{
		TouchAnchorEnabled.Add(Owner, true);
		Record(FString::Printf(TEXT("RegisterTouchAnchor %s"), *Owner.ToString()));
	}
	virtual void SetTouchAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) override
	{
		TouchAnchorEnabled.Add(Owner, bEnabled);
		Record(FString::Printf(TEXT("SetTouchAnchorEnabled %s %d"),
			*Owner.ToString(), bEnabled ? 1 : 0));
	}
	virtual void ClearTouchAnchors() override
	{
		TouchAnchorEnabled.Reset();
		Record(TEXT("ClearTouchAnchors"));
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
	// What the next ranged aim query finds. Geometry is the embodiment's, exactly as above; a
	// substrate case drives the transaction over whatever this answers. Invalid by default, which is
	// the ordinary headless answer and what every pre-existing case already sees.
	FElysiumEntityHandle AimTarget;
	virtual FElysiumEntityHandle QueryAimTarget(float MaxRangeCm) const override
	{
		// The range is recorded because it is the producer's own answer: a frame that queried at the
		// stand-in distance instead of the mode's authored `Range` is otherwise indistinguishable.
		Record(FString::Printf(TEXT("QueryAimTarget %.1f -> %s"),
			MaxRangeCm, *AimTarget.ToString()));
		return AimTarget;
	}
	// 11.15 — the two perception queries. Both default to the interface's stated headless answers,
	// so a case that does not care about perception keeps running exactly as it did: every segment
	// is clear and every point is fully lit.
	bool bLineOfSightClear = true;
	TFunction<bool(const FVector&, const FVector&)> LineOfSightQuery;
	float LightAtPoint = 1.0f;
	bool bLightQueryAvailable = true;
	bool bPlayerDucking = false;
	bool bPlayerStealthBoundsAvailable = true;
	FBox PlayerStealthBounds = FBox(FVector(-16,-16,0) * ElysiumMove::U, FVector(16,16,72) * ElysiumMove::U);
	FVector PlayerStealthCenter = PlayerStealthBounds.GetCenter();
	virtual bool IsLightQueryAvailable() const override { return bLightQueryAvailable; }
	virtual bool IsPlayerDucking() const override { return bPlayerDucking; }
	virtual bool SamplePlayerStealthBounds(FBox& OutBounds, FVector& OutCenter) const override
	{
		OutBounds = PlayerStealthBounds;
		OutCenter = PlayerStealthCenter;
		return bPlayerStealthBoundsAvailable;
	}
	virtual bool QueryLineOfSight(const FVector& FromCm, const FVector& ToCm) const override
	{
		if (LineOfSightQuery) return LineOfSightQuery(FromCm, ToCm);
		Record(FString::Printf(TEXT("QueryLineOfSight %s -> %s = %s"), *FromCm.ToString(),
			*ToCm.ToString(), bLineOfSightClear ? TEXT("clear") : TEXT("blocked")));
		return bLineOfSightClear;
	}
	virtual float QueryLightAtPoint(const FVector& PointCm) const override
	{
		Record(FString::Printf(TEXT("QueryLightAtPoint %s = %.2f"), *PointCm.ToString(),
			LightAtPoint));
		return LightAtPoint;
	}
	// SC8 — the geometry `FindBestShot`'s visibility predicate asks about, as a scriptable list
	// rather than a collision world. Each blocker is a world-space point with a radius: it stops the
	// swept hull when it lies within `RadiusCm` of the segment, and `Owner` is what makes the
	// subject-filter half assertable — a blocker whose owner is the entity the trace was told to
	// ignore does not block, exactly as retail's `CTraceFilterSimple(m_hSubject, 0)` does not.
	//
	// Empty is the ordinary answer and the headless one: no blocker, `TraceCameraHull` still reports
	// that it ran (the double IS the collision world for a Substrate case) and every candidate is
	// admitted.
	struct FCameraHullBlocker
	{
		FVector PointCm = FVector::ZeroVector;
		float RadiusCm = 32.0f;
		// Unset = world geometry, which nothing can filter out.
		FElysiumEntityHandle Owner;
	};
	TArray<FCameraHullBlocker> CameraHullBlockers;
	virtual bool TraceCameraHull(const FVector& FromCm, const FVector& ToCm,
		const FVector& HalfExtentCm, const FElysiumEntityHandle& IgnoreEntity,
		float& OutFraction, bool& OutStartSolid) const override
	{
		OutFraction = 1.0f;
		OutStartSolid = false;
		const FVector Segment = ToCm - FromCm;
		const double LengthSq = Segment.SizeSquared();
		for (const FCameraHullBlocker& Blocker : CameraHullBlockers)
		{
			if (Blocker.Owner.IsSet() && Blocker.Owner == IgnoreEntity)
			{
				continue;   // the subject filter
			}
			// The closest point on the segment, clamped to it, plus the hull's own half extent —
			// a swept box of that extent grazes anything within `Radius + extent` of the line.
			const double T = LengthSq > UE_DOUBLE_SMALL_NUMBER
				? FMath::Clamp(FVector::DotProduct(Blocker.PointCm - FromCm, Segment) / LengthSq,
					0.0, 1.0)
				: 0.0;
			const FVector Nearest = FromCm + Segment * T;
			const double Reach = Blocker.RadiusCm + HalfExtentCm.GetAbsMax();
			if (FVector::DistSquared(Nearest, Blocker.PointCm) > Reach * Reach)
			{
				continue;
			}
			if (T <= 0.0)
			{
				// The hull is already inside it — retail's `startsolid` / `allsolid` pair.
				OutStartSolid = true;
				OutFraction = 0.0f;
				break;
			}
			OutFraction = FMath::Min(OutFraction, static_cast<float>(T));
		}
		Record(FString::Printf(TEXT("TraceCameraHull %s -> %s ignore=%s = %.2f%s"),
			*FromCm.ToString(), *ToCm.ToString(), *IgnoreEntity.ToString(), OutFraction,
			OutStartSolid ? TEXT(" startsolid") : TEXT("")));
		return true;
	}
	// R6.2: the lightstyle table a `light` writes, kept so a test reads the pattern by style.
	TMap<int32, FString> LightStylePatterns;
	virtual void SetLightStylePattern(int32 Style, const FString& Pattern) override
	{
		Record(FString::Printf(TEXT("SetLightStylePattern %d %s"), Style, *Pattern));
		LightStylePatterns.Add(Style, Pattern);
	}
	virtual FString LightStylePattern(int32 Style) const override
	{
		const FString* Found = LightStylePatterns.Find(Style);
		return Found ? *Found : FString(TEXT("m"));
	}
	FElysiumDynamicLightSpec LastDynamicLight;
	virtual ULightComponent* BuildDynamicLight(const FElysiumDynamicLightSpec& Spec,
		USceneComponent* Parent) override
	{
		Record(FString::Printf(TEXT("BuildDynamicLight %s mag=%.1f reach=%.1f style=%d%s"),
			Spec.bSpot ? TEXT("spot") : TEXT("point"), Spec.Mag, Spec.RadiusCm, Spec.Style,
			Parent ? TEXT(" parented") : TEXT("")));
		LastDynamicLight = Spec;
		ULightComponent* Light = Spec.bSpot
			? static_cast<ULightComponent*>(NewComponent<USpotLightComponent>())
			: static_cast<ULightComponent*>(NewComponent<UPointLightComponent>());
		if (Parent)
		{
			Light->SetupAttachment(Parent);
		}
		return Light;
	}
	virtual void DestroyDynamicLight(ULightComponent* Light) override
	{
		Record(TEXT("DestroyDynamicLight"));
	}
	// R6.1: the sprite draw switch an `env_sprite` writes, by entity index, kept so a test reads
	// the last state published for each.
	TMap<int32, bool> BakedSpriteVisible;
	virtual void SetBakedSpriteVisible(int32 EntityIndex, bool bVisible) override
	{
		Record(FString::Printf(TEXT("SetBakedSpriteVisible %d %d"), EntityIndex, bVisible ? 1 : 0));
		BakedSpriteVisible.Add(EntityIndex, bVisible);
	}
	// 13.1 — the stealth eligibility predicate's one world term. Default false is the interface's
	// stated headless answer (the non-stealth fallback), so a case that does not care about stealth
	// keeps reading exactly the surface it did before.
	bool bPlayerSneaking = false;
	virtual bool IsPlayerSneaking() const override
	{
		Record(FString::Printf(TEXT("IsPlayerSneaking -> %s"),
			bPlayerSneaking ? TEXT("true") : TEXT("false")));
		return bPlayerSneaking;
	}
	// One term of the player's block predicate. Default TRUE, unlike `bPlayerSneaking` above: a
	// Substrate world's player stands on a floor it has no body to fall off, so "airborne" is the
	// case a test has to ask for rather than the one it inherits.
	bool bPlayerOnGround = true;
	virtual bool IsPlayerOnGround() const override
	{
		Record(FString::Printf(TEXT("IsPlayerOnGround -> %s"),
			bPlayerOnGround ? TEXT("true") : TEXT("false")));
		return bPlayerOnGround;
	}
	// ---- A1, footsteps ---------------------------------------------------------------------------
	// The player's whole published locomotion record. UNSET by default, which is the interface's
	// stated headless answer: a Substrate world runs no mover, and "no record" is a different fact
	// from a zeroed one — a zeroed sample reads as a body standing still on the ground with no
	// surface, and a step clock fed that would tick forever. A case that wants the clock to run
	// sets the sample it wants the clock to see.
	TOptional<FElysiumLocomotionSample> PlayerLocomotion;
	virtual bool SamplePlayerLocomotion(FElysiumLocomotionSample& Out) const override
	{
		if (!PlayerLocomotion.IsSet())
		{
			Record(TEXT("SamplePlayerLocomotion -> (none)"));
			return false;
		}
		Record(FString::Printf(TEXT("SamplePlayerLocomotion -> speed2d=%.1f ground=%d surface=%s"),
			PlayerLocomotion->Speed2D(), PlayerLocomotion->bOnGround ? 1 : 0,
			*PlayerLocomotion->GroundSurface.ToString()));
		Out = *PlayerLocomotion;
		return true;
	}
	// ----------------------------------------------------------------------------------------------

	// The player's published ideal activity, which the melee primary's airborne fork switches on.
	// Default EMPTY — a Substrate world runs no animation driver, so the honest answer is "nothing
	// published", and an empty activity forks nowhere. A test that wants the fork names the phase.
	FString PlayerBaseActivity;
	virtual FString GetPlayerBaseActivity() const override
	{
		Record(FString::Printf(TEXT("GetPlayerBaseActivity -> %s"),
			PlayerBaseActivity.IsEmpty() ? TEXT("(none)") : *PlayerBaseActivity));
		return PlayerBaseActivity;
	}
	// The melee stop. Counted as well as recorded, because the assertion that matters most
	// is that it fires on EVERY frame of the swing's tail: the recovered block carries no latch, so
	// the count is what tells a window apart from a one-shot.
	int32 StopPlayerBodyCount = 0;
	virtual void StopPlayerBody() override
	{
		++StopPlayerBodyCount;
		Record(TEXT("StopPlayerBody"));
	}
	// The death think's ground friction. A Substrate world runs no mover, so the double keeps the
	// carried speed itself and applies retail's own rule to it — which is what lets a case assert
	// the -20 units/frame bleed and the stop at zero with no body at all.
	float PlayerBodySpeedCm = 0.f;
	int32 BleedPlayerBodyVelocityCount = 0;
	virtual void BleedPlayerBodyVelocity(float StepCm) override
	{
		++BleedPlayerBodyVelocityCount;
		PlayerBodySpeedCm = PlayerBodySpeedCm - StepCm > 0.f ? PlayerBodySpeedCm - StepCm : 0.f;
		Record(FString::Printf(TEXT("BleedPlayerBodyVelocity step=%.2f -> %.2f"), StepCm,
			PlayerBodySpeedCm));
	}
	// `m_iFOV`, as the camera would be handed it. Unset is the port's "no producer has spoken";
	// death writes 0, which the camera latches to 60.
	TOptional<int32> PlayerFovOverride;
	virtual void SetPlayerFovOverride(int32 SourceFov) override
	{
		if (SourceFov < 0) { PlayerFovOverride.Reset(); } else { PlayerFovOverride = SourceFov; }
		Record(FString::Printf(TEXT("SetPlayerFovOverride %d"), SourceFov));
	}
	virtual float ResolveNpcMakerGroundZ(const FVector& Origin, float Depth) const override
	{
		const float Result = bUseNpcMakerGroundZ ? NpcMakerGroundZ : Origin.Z;
		Record(FString::Printf(TEXT("ResolveNpcMakerGroundZ origin=%s depth=%.2f -> %.2f"),
			*Origin.ToString(), Depth, Result));
		return Result;
	}
	virtual bool IsNpcMakerVisibleFromPlayer(const FVector& Origin) const override
	{
		Record(FString::Printf(TEXT("IsNpcMakerVisibleFromPlayer %s -> %s"), *Origin.ToString(),
			bNpcMakerVisible ? TEXT("true") : TEXT("false")));
		return bNpcMakerVisible;
	}
	virtual bool IsNpcMakerInPlayerViewCone(const FVector& Origin) const override
	{
		Record(FString::Printf(TEXT("IsNpcMakerInPlayerViewCone %s -> %s"), *Origin.ToString(),
			bNpcMakerInViewCone ? TEXT("true") : TEXT("false")));
		return bNpcMakerInViewCone;
	}
	virtual bool IsNpcMakerSpawnAreaOccupied(const FVector& Origin, float HalfExtent) const override
	{
		Record(FString::Printf(TEXT("IsNpcMakerSpawnAreaOccupied %s half=%.2f -> %s"),
			*Origin.ToString(), HalfExtent, bNpcMakerOccupied ? TEXT("true") : TEXT("false")));
		return bNpcMakerOccupied;
	}
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) override
	{
		Record(FString::Printf(TEXT("PushCameraShot %s"), *ShotFile));
		return ++NextCameraShotId;
	}
	// False stands for retail's `FUN_1006e130` failing to load the named block (`-1`), which makes
	// `FUN_10070470` remove its `camera_cinematic` and return NULL. The push is still recorded,
	// because the caller asking is the observable half.
	bool bNamedCameraShotResolves = true;
	virtual int32 PushCameraShotNamed(const FString& ShotFile, const FString& ShotName,
		const FElysiumEntityHandle& Subject, EElysiumShotExposure Exposure) override
	{
		Record(FString::Printf(TEXT("PushCameraShotNamed %s:%s exposure=%s"), *ShotFile, *ShotName,
			Exposure == EElysiumShotExposure::Clamped ? TEXT("clamped") : TEXT("scene")));
		return bNamedCameraShotResolves ? ++NextCameraShotId : 0;
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
	// The re-shot's `m_nClientResetFrame` re-stamp. Recorded rather than modelled: the double owns
	// no stack, and what a case asserts is that a shot start on a live handle asked for it.
	virtual bool RestartCameraShot(int32 ShotId) override
	{
		Record(FString::Printf(TEXT("RestartCameraShot %d"), ShotId));
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
	// A2 (footsteps): the body-sound seam, RECORDED and not mixed. The `(Owner, Channel)`
	// replacement is `AElysiumMapActor`'s own policy — it needs a live voice pool to stop a voice
	// in — so this double deliberately keeps both voices alive and lets a test read the two
	// requests exactly as the producer made them.
	virtual FElysiumAudioVoiceHandle PlayBodySound(const FElysiumEntityHandle& Owner,
		const FElysiumBodySound& Sound) override
	{
		Record(FString::Printf(TEXT("PlayBodySound %s vol=%.2f lvl=%d pitch=%.2f chan=%d"),
			*Sound.Rel, Sound.Volume, Sound.SoundLevelDb, Sound.Pitch,
			static_cast<int32>(Sound.Channel)));
		BodySounds.Add(Sound);
		BodySoundOwners.Add(Owner);
		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(Sound.Rel);
		Request.Gain = Sound.Volume;
		Request.Pitch = Sound.Pitch;
		return Submit(MoveTemp(Request));
	}
	// Every body sound a test's producers made, in order, with the owner each was made for. The
	// recorded line carries the same facts as text; these carry them as values, so a test asserts
	// a volume without parsing one out of a string.
	TArray<FElysiumBodySound> BodySounds;
	TArray<FElysiumEntityHandle> BodySoundOwners;
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
	// Test-only completion edge: remove the currently live handles without issuing a cancellation.
	// The entity world then observes exactly the same IsVoicePlaying true -> false transition as a
	// naturally completed device voice.
	void CompleteAllVoices()
	{
		LiveVoices.Reset();
		Requests.Reset();
	}
	int32 NumLiveVoices() const { return LiveVoices.Num(); }
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
	virtual void PostNotification(const FElysiumNotification& Notification) override
	{
		Notifications.Add(Notification);
		Record(FString::Printf(TEXT("PostNotification %s '%s' x%d"),
			ElysiumNotificationKindName(Notification.Kind), *Notification.Subject,
			Notification.Quantity));
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
	virtual void ApplyDust(const FElysiumDustState& Dust) override
	{
		DustStates.Add(Dust.Entity.Index, Dust);
		Record(FString::Printf(TEXT("ApplyDust #%d %s points=%d"), Dust.Entity.Index,
			Dust.bActive ? TEXT("on") : TEXT("off"), Dust.SpawnPointsCm.Num()));
	}
	virtual void ApplySteam(const FElysiumSteamState& Steam) override
	{
		SteamStates.Add(Steam.Entity.Index, Steam);
		Record(FString::Printf(TEXT("ApplySteam #%d %s"), Steam.Entity.Index,
			Steam.bActive ? TEXT("on") : TEXT("off")));
	}
	virtual void ApplyBeam(const FElysiumBeamState& Beam) override
	{
		BeamStates.Add(Beam.Entity.Index, Beam);
		Record(FString::Printf(TEXT("ApplyBeam #%d %s"), Beam.Entity.Index,
			Beam.bActive ? TEXT("on") : TEXT("off")));
	}

	FElysiumEntityHandle OpenSignOwner;
	FElysiumEntityHandle OpenDialogOwner;
	TArray<FElysiumNotification> Notifications;
	FString ActiveScheme;
	FElysiumWeatherTransition LastWetness;
	TMap<int32, FElysiumWeatherEmitterState> Emitters;
	TMap<int32, FElysiumDustState> DustStates;
	TMap<int32, FElysiumSteamState> SteamStates;
	TMap<int32, FElysiumBeamState> BeamStates;

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
// contract is a statement about
// relative order, and relative order is what a per-facility counter cannot express.
//
// Line grammar: `<kind> <detail>`, kind being the first whitespace-delimited token.
//
//   fire      relay1.OnTrigger -> counter1.Add
//   queue     counter1.Add(5) @1.200 +py act=#4 cal=#1
//   deliver   #3 counter1.Add(5) act=#4 cal=#1
//   python    G.Tut_Key = 1
//   no-target ghost.Add act=#4 cal=#1
//   no-input  #3 counter1.Nope
//   loop-guard 10000
//
// An entity reads as `#<index> <targetname-or-classname>`, so a fan-out over three same-named
// entities is distinguishable by index, which is what stable-entity-order assertions need.
//
// The three event-carrying kinds close with the dispatch provenance (`act=` activator, `cal=`
// caller, `#<null>` when unbound), because a chokepoint's transport and its provenance are one
// claim: an input that reaches a receiver with the wrong activator is as wrong as one that reaches
// it in the wrong order. It rides at the END of the line so every needle written against the
// leading `<target>.<Input>(<param>)` shape still matches.
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

	// The first line of `Kind` containing `Needle`, whole. What a provenance assertion reads: the
	// needle picks the record out of the stream, then the rest of the line carries the answer.
	// Empty when there is no such line — which fails a Contains assertion, as it should.
	FString FirstLine(const TCHAR* Kind, const FString& Needle) const
	{
		const TArray<FString> Kinds = OfKind(Kind);
		const int32 At = PositionOf(Kind, Needle);
		return Kinds.IsValidIndex(At) ? Kinds[At] : FString();
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
		Lines.Add(FString::Printf(TEXT("queue %s.%s(%s) @%.3f%s%s"),
			Event.Target.IsEmpty() ? TEXT("(python)") : *Event.Target,
			*Event.Input.ToString(), *Event.Param.ToString(), Event.FireTime,
			Event.PythonSrc.IsEmpty() ? TEXT("") : TEXT(" +py"), *Provenance(Event)));
	}
	virtual void OnDelivered(double, const FElysiumEntity& Target,
		const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("deliver #%d %s.%s(%s)%s"),
			Target.Handle.Index, *Name(Target), *Event.Input.ToString(), *Event.Param.ToString(),
			*Provenance(Event)));
	}
	virtual void OnUnknownTarget(double, const FElysiumIOEvent& Event) override
	{
		Lines.Add(FString::Printf(TEXT("no-target %s.%s%s"), *Event.Target,
			*Event.Input.ToString(), *Provenance(Event)));
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
	// ` act=#<idx> cal=#<idx>` — the dispatch provenance an event carries, in the handle's own
	// `#<index>` / `#<null>` spelling.
	static FString Provenance(const FElysiumIOEvent& Event)
	{
		return FString::Printf(TEXT(" act=%s cal=%s"),
			*Event.Activator.ToString(), *Event.Caller.ToString());
	}

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
