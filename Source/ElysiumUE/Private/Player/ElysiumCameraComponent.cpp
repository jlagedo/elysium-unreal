#include "ElysiumCameraComponent.h"

#include "Player/ElysiumCommandBus.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumPlayerBody.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCamera, Log, All);

namespace
{
	void MarkTemporalCameraCut(const UActorComponent* Component)
	{
		const APawn* Pawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
		const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
}

UElysiumCameraComponent::UElysiumCameraComponent()
{
	// The solve runs from CalcCamera, not from a tick (header): a component tick would run before the
	// pawn has moved and leave the boom a frame behind the body it hangs off.
	PrimaryComponentTick.bCanEverTick = false;

	// The view angles are the player's the whole time — the camera derives from them, it does not
	// take them over. That is true in third person too: `CAM_ApplyToView` overrides the *view's*
	// angles with the solved ones, it does not reparent anything.
	bUsePawnControlRotation = true;
}

void UElysiumCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterCommands();
}

void UElysiumCameraComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UnregisterCommands();
	Super::EndPlay(Reason);
}

// =====================================================================================
// The frame
// =====================================================================================

FRotator UElysiumCameraComponent::ViewRotation() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			return Controller->GetControlRotation();
		}
		return Pawn->GetActorRotation();
	}
	return GetComponentRotation();
}

void UElysiumCameraComponent::UpdateCamera(float DeltaSeconds)
{
	// Once per frame: a second CalcCamera in the same frame (a scene capture, a spectator) must read
	// the solved view, not blend again.
	if (LastSolvedFrame == GFrameCounter)
	{
		return;
	}
	LastSolvedFrame = GFrameCounter;

	const UWorld* World = GetWorld();
	// A held world holds the camera. The delta CalcCamera is handed is real time while paused, since
	// the controller keeps ticking; and while running it has already been scaled by the world's
	// dilation, which is why the weight driver is passed a time scale of 1 (11.1's rule for the clock,
	// applied to the same problem).
	const float Dt = (World && World->IsPaused()) ? 0.0f : FMath::Max(0.0f, DeltaSeconds);

	Cvars.LoadFrom([](const TCHAR* Name) { return ElysiumCommandBus::Console().GetCvar(Name); });

	ConsumeCamCommand();
	ReadOrbitInput(PendingCmd, Dt);

	// The scripted channel's weight is written before the third-person driver advances, because
	// `CAM_IsThirdPerson` reads it — a dialogue camera counts as third person, which is what gets the
	// player model drawn under it for free.
	Shots.Advance(Dt);
	Weights.Scripted = Shots.GetWeight();
	Weights.Advance(Dt, /*TimeScale*/ 1.0f);

	const FVector Eye = EyeLocation();
	const FRotator View = ViewRotation();

	if (Weights.Third > 0.0f)
	{
		SolveBoom(Eye, View, Dt);
	}
	else
	{
		// First person: the offset is the zero vector and the result is exactly the eye view. Mark the
		// camera for a re-seed so re-entering third person snaps its smoothing rather than swinging in
		// from wherever it was left (VtMB's `+0x4` flag).
		SolvedOffset = FVector::ZeroVector;
		SolvedAngles = View;
		bNeedsReseed = true;
	}

	SolveShot(Dt);
	SolveModelAlpha();
}

void UElysiumCameraComponent::ConsumeCamCommand()
{
	// `cam_command` is a one-shot mode request, and it is both a cvar and a bindable verb — reading it
	// here is what makes the two the same write.
	const FString Value = ElysiumCommandBus::Console().GetCvar(TEXT("cam_command"));
	const int32 Command = Value.IsEmpty() ? 0 : FCString::Atoi(*Value);
	if (Command == 1)
	{
		SetThirdPerson(true);
	}
	else if (Command == 2)
	{
		SetThirdPerson(false);
	}
}

void UElysiumCameraComponent::ReadOrbitInput(const FElysiumUserCmd& Cmd, float Dt)
{
	// Un-orbited axes follow their cvar live, so editing `cam_idealdist` or `cam_targetangle` moves
	// the camera at once; an axis the player has dollied or orbited keeps what they asked for until
	// `snapto` puts it back.
	if (!bDollied)      { RequestedDistance = Cvars.IdealDist; }
	if (!bPitchOrbited) { RequestedPitch = Cvars.TargetAngle; }
	if (!bYawOrbited)   { RequestedYaw = 0.0f; }

	const float DollyStep = Cvars.DollySpeed * Dt;
	const float OrbitStep = Cvars.OrbitSpeed * Dt;

	if (Cmd.IsDown(EElysiumButton::CamIn))  { RequestedDistance -= DollyStep; bDollied = true; }
	if (Cmd.IsDown(EElysiumButton::CamOut)) { RequestedDistance += DollyStep; bDollied = true; }

	// Source's pitch is down-positive, and so are `c_minpitch` / `c_maxpitch`: `+campitchup` raises the
	// camera above the eye line, which is a larger value here.
	if (Cmd.IsDown(EElysiumButton::CamPitchUp))   { RequestedPitch += OrbitStep; bPitchOrbited = true; }
	if (Cmd.IsDown(EElysiumButton::CamPitchDown)) { RequestedPitch -= OrbitStep; bPitchOrbited = true; }

	if (Cmd.IsDown(EElysiumButton::CamYawLeft))  { RequestedYaw -= OrbitStep; bYawOrbited = true; }
	if (Cmd.IsDown(EElysiumButton::CamYawRight)) { RequestedYaw += OrbitStep; bYawOrbited = true; }

	RequestedDistance = FMath::Clamp(RequestedDistance, Cvars.MinDistance, Cvars.MaxDistance);
	RequestedPitch = FMath::Clamp(RequestedPitch, Cvars.MinPitch, Cvars.MaxPitch);
	RequestedYaw = FMath::Clamp(RequestedYaw, Cvars.MinYaw, Cvars.MaxYaw);
}

void UElysiumCameraComponent::SolveBoom(const FVector& Eye, const FRotator& View, float Dt)
{
	// 1) The rate-limited approach on distance, yaw and pitch. Nothing eases — the clamp is the whole
	// smoothing, which is why the damper below is what the camera actually reads as.
	if (bNeedsReseed)
	{
		SolvedDistance = RequestedDistance;
		SolvedYaw = RequestedYaw;
		SolvedPitch = RequestedPitch;
	}
	else
	{
		SolvedDistance = ElysiumCam::Approach(SolvedDistance, RequestedDistance, Cvars.ApproachDistSpeed, Dt);
		SolvedYaw = ElysiumCam::ApproachAngle(SolvedYaw, RequestedYaw, Cvars.ApproachAngleSpeed, Dt);
		SolvedPitch = ElysiumCam::ApproachAngle(SolvedPitch, RequestedPitch, Cvars.ApproachAngleSpeed, Dt);
	}

	// 2) The boom direction. The camera looks *along* it, from behind and above the eye — Source's
	// pitch is down-positive, so a `cam_targetangle` of 15 (above the eye line) subtracts here
	// (`docs/project/rebuild-strategy.md` -> Coordinate conventions). The exact composition inside
	// `0x100fd350` is only partially recovered (`docs/vtmb/camera-view-modes.md` -> Not yet recovered); this is
	// the reading the observed cvar roles support.
	FRotator BoomRot;
	BoomRot.Pitch = FMath::Clamp(View.Pitch - SolvedPitch, -89.0f, 89.0f);
	BoomRot.Yaw = FRotator::NormalizeAxis(View.Yaw + Cvars.Yaw + SolvedYaw);
	BoomRot.Roll = 0.0f;

	const FVector BoomDir = BoomRot.Vector();
	float Allowed = SolvedDistance;

	// 3) Collision. VtMB traces a box hull; a sphere is the closer match to how the retract actually
	// reads in motion and is the substitution `docs/vtmb/camera-view-modes.md` records as a feel delta.
	bClipped = false;
	const UWorld* World = GetWorld();
	if (Cvars.bCollide && World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumCameraBoom), /*bTraceComplex*/ false, GetOwner());
		Params.AddIgnoredActor(GetOwner());
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Eye, Eye - BoomDir * SolvedDistance, FQuat::Identity,
				ECC_Camera, FCollisionShape::MakeSphere(Cvars.TraceRadius), Params))
		{
			// The allowed distance is the swept fraction, then a further pull-in on contact so the
			// near plane does not sit flush against the surface. Contact also forces a re-seed, which
			// is what makes the camera snap in against a wall and ease back out.
			Allowed = FMath::Max(0.0f, SolvedDistance * Hit.Time - Cvars.WallPullIn);
			bClipped = true;
		}
	}

	const FVector Target = Eye - BoomDir * Allowed;

	// 4) The spring damper — **two constants**, stiffer while wall-clipped than in open space. This is
	// the most characteristic part of the VtMB camera and the reason `USpringArmComponent` is not
	// enough; `cdamp_on 0` bypasses it for A/B.
	if (bNeedsReseed || !Cvars.bDampOn)
	{
		SpringPosition = Target;
	}
	else
	{
		const FVector Delta = Target - SpringPosition;
		const float Dist = Delta.Size();
		if (Dist >= Cvars.DampMaxDist || Dist <= Cvars.SpringLength)
		{
			// Past the clamp the camera never lags further; inside the rest length there is no force.
			SpringPosition = Target;
		}
		else
		{
			const float K = bClipped ? Cvars.HookesConstantWall : Cvars.HookesConstant;
			SpringPosition += Delta.GetSafeNormal() * (Dist - Cvars.SpringLength)
				* FMath::Clamp(K * Dt, 0.0f, 1.0f);
		}
	}
	bNeedsReseed = false;

	// 5) What the weight is applied to: the boom offset scales 0 -> full and the view angles
	// interpolate from the eye angles to the camera's own. At weight 0 the offset is the zero vector,
	// so the result is exactly the first-person view — there is no second camera anywhere.
	SolvedOffset = SpringPosition - Eye;
	SolvedAngles = BoomRot;
}

void UElysiumCameraComponent::SolveShot(float Dt)
{
	const FElysiumCameraShot* Top = Shots.Top();
	const int32 TopId = Shots.TopId();
	if (!Top)
	{
		bShotSeeded = false;
		LastTopShotId = 0;
		return;
	}
	if (TopId != LastTopShotId)
	{
		// The deciding shot changed — a push, or a pop that revealed the one underneath. Either way it
		// cuts: the weight ramp is what blends, the shot itself does not chase in from its predecessor.
		LastTopShotId = TopId;
		bShotSeeded = false;
	}

	FVector TargetPos = Top->Origin;
	FRotator TargetRot = Top->bUseLookAt
		? (Top->LookAt - TargetPos).Rotation()
		: Top->Rotation;
	TargetRot.Roll = Top->Roll;

	// A shot arriving is a *blend*, not a chase: the weight ramp is what carries the view from the
	// player's camera to the shot, so the shot itself starts where it was authored. `MoveSpeed` and
	// `MaxTurnRate` are the file's own limits on the shot **tracking** a moving target afterwards,
	// which is what the shipped how-to says they are for.
	if (!bShotSeeded)
	{
		ShotPosition = TargetPos;
		ShotRotation = TargetRot;
		bShotSeeded = true;
	}
	else
	{
		ShotPosition = Top->MoveSpeed > 0.0f
			? FMath::VInterpConstantTo(ShotPosition, TargetPos, Dt, Top->MoveSpeed)
			: TargetPos;
		ShotRotation.Pitch = ElysiumCam::ApproachAngle(ShotRotation.Pitch, TargetRot.Pitch, Top->MaxTurnRate.X, Dt);
		ShotRotation.Yaw   = ElysiumCam::ApproachAngle(ShotRotation.Yaw,   TargetRot.Yaw,   Top->MaxTurnRate.Y, Dt);
		ShotRotation.Roll  = ElysiumCam::ApproachAngle(ShotRotation.Roll,  TargetRot.Roll,  Top->MaxTurnRate.Z, Dt);
	}
}

void UElysiumCameraComponent::SolveModelAlpha()
{
	PlayerModelAlpha = ElysiumCam::SolveModelAlpha(SolvedOffset, Weights, Cvars);
}
// =====================================================================================
// The apply point (`CAM_ApplyToView`, 0x100ffb00)
// =====================================================================================

void UElysiumCameraComponent::ApplyToView(FMinimalViewInfo& View) const
{
	// The strafe bank goes on FIRST, so the third-person blend below lerps it away along with
	// everything else. VtMB's own gate is binary — it adds a literal 0.0 in third person — but the
	// mode here is a weight rather than a flag, so the bank is scaled by the first-person share.
	// At E = 0 and E = 1 that is exactly the original; in between it fades instead of popping.
	if (const AActor* Owner = GetOwner())
	{
		const float Roll = ElysiumCam::SolveViewRoll(Owner->GetVelocity(), View.Rotation,
			Cvars.RollAngle, Cvars.RollSpeed);
		View.Rotation.Roll += Roll * (1.0f - Weights.ThirdBlend());
	}

	const float E = Weights.ThirdBlend();
	if (E > 0.0f)
	{
		View.Location += SolvedOffset * E;
		View.Rotation = FMath::Lerp(View.Rotation, SolvedAngles, E);
	}

	const float S = Shots.GetWeight();
	const FElysiumCameraShot* Top = Shots.Top();
	if (S > 0.0f)
	{
		// VtMB camera tracks author exact edits and deliberate dollies, but no camera-motion blur.
		// UE's default blur turns even the small post-cut dollies into a radial smear and makes a
		// zero-time edit read as a scroll. Keep gameplay post-processing intact and suppress only the
		// scripted camera channel while it has visible weight.
		View.PostProcessSettings.bOverride_MotionBlurAmount = true;
		View.PostProcessSettings.MotionBlurAmount = 0.0f;
	}
	if (S > 0.0f && bShotSeeded)
	{
		// The scripted camera is applied on top: origin, look-at, roll and FOV all lerp by its own
		// timed weight, which is what lets a cutscene cut on the beat it was authored for.
		View.Location = FMath::Lerp(View.Location, ShotPosition, S);
		View.Rotation = FMath::Lerp(View.Rotation, ShotRotation, S);
		if (Top && Top->FieldOfView > 0.0f)
		{
			View.FOV = FMath::Lerp(View.FOV, Top->FieldOfView, S);
		}
	}
}

bool UElysiumCameraComponent::ConsumeTemporalCameraCutRequest()
{
	const bool bPending = bTemporalCameraCutPending;
	bTemporalCameraCutPending = false;
	return bPending;
}

bool UElysiumCameraComponent::CalcCameraFor(UElysiumCameraComponent* Camera, float DeltaSeconds,
	FMinimalViewInfo& Out)
{
	if (!Camera || !Camera->IsActive())
	{
		return false;
	}
	// `GetCameraView` FIRST: it fills FOV, the post-process settings and the first-person-rendering
	// fields, and skipping it is the documented cause of first-person rendering silently not applying.
	Camera->GetCameraView(DeltaSeconds, Out);
	Camera->UpdateCamera(DeltaSeconds);
	Camera->ApplyToView(Out);
	if (Camera->ConsumeTemporalCameraCutRequest())
	{
		// A camera cut resets temporal histories and has zero camera velocity by definition. The base
		// camera component can still supply its pre-cut transform through PreviousViewTransform, so
		// override it with the newly applied authored view before the viewport builds this frame.
		Out.PreviousViewTransform = FTransform(Out.Rotation, Out.Location);
		MarkTemporalCameraCut(Camera);
		UE_LOG(LogElysiumCamera, Log, TEXT("applied temporal camera cut at %s"),
			*Out.Location.ToCompactString());
	}
	return true;
}

// =====================================================================================
// The mode
// =====================================================================================

void UElysiumCameraComponent::SetThirdPerson(bool bThird)
{
	// The minimal pair: set the latch, clear the one-shot request. No weapon arbitration and no
	// holster check — only `togglecamera` runs those, and both need weapons (4.9).
	Weights.bUserThird = bThird;
	ElysiumCommandBus::Console().SetCvar(TEXT("cam_command"), TEXT("0"));
}

void UElysiumCameraComponent::ToggleCamera()
{
	SetThirdPerson(!Weights.bUserThird);
	UE_LOG(LogElysiumCamera, Verbose, TEXT("togglecamera -> %s"),
		Weights.bUserThird ? TEXT("third") : TEXT("first"));
}

// =====================================================================================
// The scripted-shot channel
// =====================================================================================

int32 UElysiumCameraComponent::PushShot(const FElysiumCameraShot& Shot)
{
	FElysiumCameraShot StableShot = Shot;
	StableShot.bCameraCut = false; // an instruction for this frame, never persistent shot state
	const int32 Id = Shots.Push(StableShot);
	if (Shot.bCameraCut || Shot.BlendSeconds <= KINDA_SMALL_NUMBER)
	{
		bTemporalCameraCutPending = true;
	}
	UE_LOG(LogElysiumCamera, Verbose, TEXT("camera shot #%d '%s' pushed"), Id, *Shot.DebugName);
	return Id;
}

bool UElysiumCameraComponent::UpdateShot(int32 Id, const FElysiumCameraShot& Shot)
{
	FElysiumCameraShot StableShot = Shot;
	StableShot.bCameraCut = false;
	if (!Shots.Update(Id, StableShot))
	{
		return false;
	}
	if (Shot.bCameraCut)
	{
		bTemporalCameraCutPending = true;
		UE_LOG(LogElysiumCamera, Log, TEXT("camera shot #%d queued temporal cut"), Id);
	}
	return true;
}

bool UElysiumCameraComponent::PopShot(int32 Id, float BlendOutSeconds)
{
	const bool bWasTop = Shots.TopId() == Id;
	const FElysiumCameraShot* Existing = Shots.Find(Id);
	const float EffectiveBlend = BlendOutSeconds >= 0.0f
		? BlendOutSeconds
		: (Existing ? Existing->BlendSeconds : 0.0f);
	if (!Shots.Pop(Id, BlendOutSeconds))
	{
		return false;
	}
	if (bWasTop && EffectiveBlend <= KINDA_SMALL_NUMBER)
	{
		bTemporalCameraCutPending = true;
	}
	UE_LOG(LogElysiumCamera, Verbose, TEXT("camera shot #%d popped"), Id);
	return true;
}

// =====================================================================================
// The verbs
// =====================================================================================

void UElysiumCameraComponent::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	Bindings.Add(Registry.Bind(TEXT("togglecamera"), [this](const FElysiumCommandCall&) { ToggleCamera(); }));
	Bindings.Add(Registry.Bind(TEXT("thirdperson"), [this](const FElysiumCommandCall&) { SetThirdPerson(true); }));
	Bindings.Add(Registry.Bind(TEXT("firstperson"), [this](const FElysiumCommandCall&) { SetThirdPerson(false); }));

	// `cam_command` as a verb writes the same cvar the console does; `CAM_Think` consumes it either
	// way, which is the point of it being a channel rather than a call.
	Bindings.Add(Registry.Bind(TEXT("cam_command"), [](const FElysiumCommandCall& Call)
	{
		ElysiumCommandBus::Console().SetCvar(TEXT("cam_command"),
			Call.Args.IsEmpty() ? TEXT("0") : *Call.Args);
	}));

	// `snapto` puts the orbit back on its cvars and re-seeds the smoothing, so the camera arrives at
	// its ideal instead of easing there. (`cam_snapto` is registered but its role is unverified.)
	Bindings.Add(Registry.Bind(TEXT("snapto"), [this](const FElysiumCommandCall&)
	{
		bDollied = bPitchOrbited = bYawOrbited = false;
		RequestReseed();
	}));

	// `centerview` / `force_centerview` recentre pitch. The view belongs to the controller, so this
	// writes there — the camera derives from it and follows on its own.
	const auto CenterView = [this](const FElysiumCommandCall&)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (AController* Controller = Pawn ? Pawn->GetController() : nullptr)
		{
			FRotator Rot = Controller->GetControlRotation();
			Rot.Pitch = 0.0f;
			Rot.Roll = 0.0f;
			Controller->SetControlRotation(Rot);
		}
	};
	Bindings.Add(Registry.Bind(TEXT("centerview"), CenterView));
	Bindings.Add(Registry.Bind(TEXT("force_centerview"), CenterView));

	Bindings.RemoveAll([](const FElysiumCommandBinding& B) { return !B.IsValid(); });
}

void UElysiumCameraComponent::UnregisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();
	for (FElysiumCommandBinding& Binding : Bindings)
	{
		Registry.Unbind(Binding);
	}
	Bindings.Reset();
}

FString UElysiumCameraComponent::Describe() const
{
	return FString::Printf(
		TEXT("camera: %s (driver %s)\n")
		TEXT("  weights   third %.3f (eased %.3f)  scripted %.3f  feed %.3f  secondary %.3f\n")
		TEXT("  latches   user %d  forced-third %d  forced-first %d  feed %d\n")
		TEXT("  boom      requested %.1f cm / yaw %.1f / pitch %.1f -> solved %.1f / %.1f / %.1f%s\n")
		TEXT("  applied   offset %s  length %.1f cm  model alpha %.2f\n")
		TEXT("  shots     %s"),
		Weights.IsThirdPerson() ? TEXT("third person") : TEXT("first person"),
		Weights.Driver(),
		Weights.Third, Weights.ThirdBlend(), Weights.Scripted, Weights.Feed, Weights.Secondary,
		Weights.bUserThird ? 1 : 0, Weights.bForcedThird ? 1 : 0,
		Weights.bForcedFirst ? 1 : 0, Weights.bFeed ? 1 : 0,
		RequestedDistance, RequestedYaw, RequestedPitch,
		SolvedDistance, SolvedYaw, SolvedPitch, bClipped ? TEXT("  [wall-clipped]") : TEXT(""),
		*SolvedOffset.ToCompactString(), BoomLength(), PlayerModelAlpha,
		*Shots.Describe());
}

// =====================================================================================
// Dev verb
// =====================================================================================

static FAutoConsoleCommandWithWorld GElysiumCameraDump(
	TEXT("elysium.camera"),
	TEXT("Dump the player camera: the four VtMB weights, which latch is driving, the solved boom and "
	     "the scripted-shot stack."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UElysiumCameraComponent* Camera = Pawn
			? Pawn->FindComponentByClass<UElysiumCameraComponent>() : nullptr;
		if (!Camera)
		{
			UE_LOG(LogElysiumCamera, Warning, TEXT("no player camera in this world"));
			return;
		}
		UE_LOG(LogElysiumCamera, Display, TEXT("%s"), *Camera->Describe());
	}));
