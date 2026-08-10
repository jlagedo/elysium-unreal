#include "ElysiumMovementComponent.h"

#include "ElysiumGameClock.h"
#include "ElysiumPawn.h"
#include "Debug/ElysiumConsole.h"
#include "Player/ElysiumCommandBus.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Engine/GameInstance.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"

// The timestep A/B. **0 is the faithful baseline** — VtMB has no tick, so its movement really is
// frame-rate dependent (`docs/vtmb/source_movement.md` → "Frame timing"). A non-zero value opts into
// frame-rate independence and is a recorded divergence, not the reference behaviour.
static TAutoConsoleVariable<float> CVarMoveFixedStep(
	TEXT("elysium.move.FixedStep"),
	0.0f,
	TEXT("Seconds per movement integration step. 0 = the frame's own delta (faithful: VtMB has no ")
	TEXT("tick and its air-accel/jump apex are frame-rate dependent). A non-zero value (e.g. 0.015) ")
	TEXT("makes movement frame-rate independent — a divergence, kept A/B-able."),
	ECVF_Default);

// How long the jump's push keeps being applied. 0 = use `rules.txt`'s `JumpHoldTime` (0.2). This
// is **ours, not a VtMB cvar** — it exists because `rules.txt` carries two candidate windows
// (`JumpHoldTime` 0.2 and the Feat-indexed `JumpDuration`, 0.11 at rank 1) and which one the
// engine uses is not transcribed. `docs/vtmb/source_movement.md` records that as unverified.
static TAutoConsoleVariable<float> CVarJumpHoldSeconds(
	TEXT("elysium.jump.HoldSeconds"),
	0.0f,
	TEXT("Seconds the jump's upward push is sustained while held. 0 = rules.txt JumpHoldTime."),
	ECVF_Default);

// The speed authority A/B (CCC7). VtMB's player speed is the animation's own per-direction cell
// speed; `speed_walk`/`speed_runbase` are registered-but-never-read in the retail build, which makes
// the constants the *port's* baseline rather than the game's.
static TAutoConsoleVariable<int32> CVarAnimSpeedAuthority(
	TEXT("elysium.move.AnimSpeedAuthority"),
	1,
	TEXT("1 = the body's speed comes from the animation's per-direction cell speeds (faithful). ")
	TEXT("0 = Troika's stated speed_walk/speed_runbase constants, which the retail build never ")
	TEXT("reads. The A/B for the whole speed seam, air and water included."),
	ECVF_Default);

// Whether a direction between two cells blends them or snaps to the nearer. Retail snaps, because
// its speed comes from a 3x3 digital key table and can never land between two cells; a stick can,
// and on the walk fan the cells differ by more than 2x, so snapping reads as the stride popping.
// A divergence, and the shipped default.
static TAutoConsoleVariable<int32> CVarGaitSpeedInterpolate(
	TEXT("elysium.move.GaitSpeedInterpolate"),
	1,
	TEXT("1 = interpolate the animation's cell speed across the move_yaw fan (a Feel divergence; ")
	TEXT("what an analog stick needs). 0 = snap to the nearest cell, which is retail's behaviour."),
	ECVF_Default);

UElysiumMovementComponent::UElysiumMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Step 4 of the frame, and it is deliberately EARLY: the map actor makes this a prerequisite of
	// its gameplay pass and a dependent of its pre-move pass, so the body moves on a freshly
	// advanced clock and before a single think or queued event runs. That is where retail moves it —
	// out of the `clc_move` drain, ahead of `GameFrame` entirely (RE21).
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UElysiumMovementComponent::IsAnimSpeedAuthorityEnabled()
{
	return CVarAnimSpeedAuthority.GetValueOnAnyThread() != 0;
}

bool UElysiumMovementComponent::IsGaitSpeedInterpolationEnabled()
{
	return CVarGaitSpeedInterpolate.GetValueOnAnyThread() != 0;
}

float UElysiumMovementComponent::GetMaxSpeed() const
{
	// **The ceiling, not the gait.** This is retail's `m_flMaxspeed`: the peak over every cell of
	// every gait table, published for `UNavMovementComponent`'s contract and for readouts, and read
	// by nothing inside the solve — which asks `WishSpeed()` for a direction instead. The peak is
	// >= every cell by construction, so a clamp against it can never fire.
	if (bNoclip)
	{
		return ElysiumMove::NoclipSpeed * (PendingCmd.IsDown(EElysiumButton::Speed) ? ElysiumMove::NoclipBoost : 1.0f);
	}
	if (!bOnGround)
	{
		return Tuning.JumpMaxSpeed;
	}
	if (IsAnimSpeedAuthorityEnabled() && GaitSpeeds.IsValid())
	{
		return GaitSpeeds.Peak();
	}
	return FMath::Max(ElysiumMove::WalkSpeed, ElysiumMove::RunSpeed);
}

const FElysiumGaitSpeedTable& UElysiumMovementComponent::GaitTableForCommand() const
{
	return ElysiumGait::TableFor(GaitSpeeds, bDucked || bDucking,
		PendingCmd.IsDown(EElysiumButton::Speed));
}

float UElysiumMovementComponent::WishSpeed(const FVector& WishDir, float Scale) const
{
	FElysiumWishSpeedInput In;
	In.bNoclip = bNoclip;
	In.bOnGround = bOnGround;
	In.bDucked = bDucked || bDucking;
	In.bWalkKey = PendingCmd.IsDown(EElysiumButton::Speed);
	In.bAuthority = IsAnimSpeedAuthorityEnabled();
	In.bInterpolate = IsGaitSpeedInterpolationEnabled();
	// The **commanded** direction, not the realized one: retail picks the cell from the keys being
	// held, before the move integrates. The pose parameter the graph steers on is a different angle
	// and is deliberately filtered; this one never is.
	In.WishYawDegrees = ElysiumLocomotion::RelativeYaw(
		static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(WishDir.Y, WishDir.X))),
		static_cast<float>(ViewFrame().Yaw));
	In.Scale = Scale;
	In.LastGroundedWishSpeed = LastGroundedWishSpeed;
	In.JumpMaxSpeed = Tuning.JumpMaxSpeed;
	In.NoclipSpeed = ElysiumMove::NoclipSpeed
		* (In.bWalkKey ? ElysiumMove::NoclipBoost : 1.0f);
	return ElysiumGait::WishSpeedFrom(In, GaitSpeeds);
}

void UElysiumMovementComponent::SetNoclip(bool bEnable)
{
	bNoclip = bEnable;
	Velocity = FVector::ZeroVector;
	if (!bNoclip)
	{
		// Re-ask the world where the floor is rather than trusting whatever was true before the fly.
		bOnGround = false;
	}
}

void UElysiumMovementComponent::ResetState()
{
	Velocity = FVector::ZeroVector;
	OldButtons = 0;
	JumpsTaken = 0;
	PrevCmd = FElysiumUserCmd();
	Stepper.Reset();
	bOnGround = false;
	WaterLevel = EElysiumWaterLevel::None;
	EndJumpHold();

	// The published body state goes with the carried motion: a teleported body must not still be
	// reporting the gait it left with.
	LastSample = FElysiumLocomotionSample();
	CapturedWish = FVector::ZeroVector;
	CapturedWishScale = 0.0f;
	// The gait tables survive a teleport — they belong to the body, not to where it is standing —
	// but the speed carried out of the last frame does not.
	LastGroundedWishSpeed = 0.0f;
	CommandedSpeed = 0.0f;

	// Stand up if we were crouched, so the hull the body arrives with is the standing one. The
	// retained request has to be cleared with it: a toggle survives a keypress by design, so a body
	// teleported while crouched would otherwise re-duck itself on the very next frame.
	bDuckRequested = false;
	if (bDucked || bDucking)
	{
		FinishUnDuck();
	}
	DuckTime = 0.0f;

	// The jump rules go back to the rulebook's. A harness override belongs to the course that set
	// it, and a body arriving somewhere new is arriving with the shipped tuning.
	Tuning.BaseJumpVelocity = ElysiumMove::BaseJumpVelocity;
	Tuning.JumpGravityMultiplier = ElysiumMove::JumpGravityMultiplier;
	Tuning.JumpHoldSeconds = ElysiumMove::JumpHoldSeconds;
	bJumpTuningLoaded = false;
}

bool UElysiumMovementComponent::SetJumpRuleOverride(const TCHAR* Key, float Value)
{
	if (!Tuning.SetJumpRule(Key, Value))
	{
		return false;
	}
	// The rulebook read is deferred to the first frame that moves; without latching it here it
	// would land on top of this and the override would last exactly zero frames.
	bJumpTuningLoaded = true;
	return true;
}

void UElysiumMovementComponent::SetFrozen(bool bInFrozen)
{
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
	}
}

FRotator UElysiumMovementComponent::ViewFrame() const
{
	const AController* C = PawnOwner ? PawnOwner->GetController() : nullptr;
	return C ? C->GetControlRotation()
		: (PawnOwner ? PawnOwner->GetActorRotation() : FRotator::ZeroRotator);
}

FVector UElysiumMovementComponent::WishDirection(const FElysiumUserCmd& Cmd, float& OutScale,
	bool bForcePitch)
{
	// Walking is on the ground plane; noclip flies along the aim and swimming aims where you look,
	// both pitch included.
	const FVector Wish = ElysiumMove::WishDirection(Cmd.Move, Cmd.Up, ViewFrame(),
		/*bIncludePitch*/ bNoclip || bForcePitch, OutScale);
	CapturedWish = Wish;
	CapturedWishScale = OutScale;
	return Wish;
}

void UElysiumMovementComponent::PublishLocomotionSample(bool bSolved)
{
	// The same expression the wish was framed in, or the sample's facing and its `move_yaw` would be
	// two different frames wearing one name.
	LastSample.FacingYaw = static_cast<float>(ViewFrame().Yaw);

	// Into the facing frame. A yaw-only rotation, so the vertical component passes through unchanged.
	const FVector Planar = FRotator(0.0f, -LastSample.FacingYaw, 0.0f).RotateVector(Velocity);
	LastSample.LocalVelocity = FVector(Planar.X, Planar.Y, Velocity.Z);

	LastSample.MoveYawVelocity = LastSample.Speed2D() > UE_KINDA_SMALL_NUMBER
		? ElysiumLocomotion::RelativeYaw(
			static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X))),
			LastSample.FacingYaw)
		: 0.0f;
	// Seeded unfiltered, for the same reason as the cast's: the slew is a rate and belongs to the
	// once-a-frame driver, not to a sample a readout may take twice.
	LastSample.MoveYawPose = LastSample.MoveYawVelocity;

	// The wish the solve actually used, captured in `WishDirection` rather than re-derived here.
	// `bSolved` is false on the frozen path, where no move function ran: a body nailed to the floor
	// commanded nothing that took effect, and reporting last frame's wish would animate it walking.
	//
	// Noclip and swimming carry pitch, so the wish is projected before its yaw is taken — a wish
	// pointing straight up has no horizontal direction to report.
	const double WishPlanar = bSolved ? FVector2D(CapturedWish.X, CapturedWish.Y).Size() : 0.0;
	if (WishPlanar > UE_KINDA_SMALL_NUMBER)
	{
		LastSample.MoveYawWish = ElysiumLocomotion::RelativeYaw(
			static_cast<float>(FMath::RadiansToDegrees(
				FMath::Atan2(CapturedWish.Y, CapturedWish.X))),
			LastSample.FacingYaw);
		LastSample.WishScale = CapturedWishScale;
	}
	else
	{
		// No commanded direction. The yaw is a placeholder and the zero scale is what says so — a
		// consumer that cannot tell them apart reads "walk forward" out of a body standing still.
		LastSample.MoveYawWish = 0.0f;
		LastSample.WishScale = 0.0f;
	}

	// What the solve was commanded, for the classifier's `cmdMoveMag` term. It rides the same
	// `bSolved` gate as the wish, and for the same reason: a frozen body commanded nothing that took
	// effect, and reporting last frame's would have it break into a run standing still.
	LastSample.CommandedSpeed = bSolved ? CommandedSpeed : 0.0f;

	// The body's own answer, not `CategorizePosition`'s raw one: a noclipping body is flying, and a
	// graph asking whether it is grounded wants that to be false.
	LastSample.bOnGround = IsMovingOnGround();
	LastSample.Water = WaterLevel;
	LastSample.Stance = ElysiumLocomotion::StanceFrom(bDucked, bDucking);
	LastSample.JumpHoldRemaining = JumpHoldRemaining;
}

void UElysiumMovementComponent::CategorizePosition()
{
	if (!UpdatedComponent)
	{
		bOnGround = false;
		return;
	}
	// Rising: never on the ground. Source's own gate, and what keeps a jump from re-grounding on
	// the frame it leaves — including every frame of the held push, which holds `v.z` at the full
	// launch speed and would otherwise re-ground the moment the body brushed a surface.
	if (Velocity.Z > Tuning.BaseJumpVelocity * 0.5f)
	{
		bOnGround = false;
		return;
	}

	// A 2u down-trace. VtMB has no StayOnGround, so descending a step briefly leaves the ground and
	// this is what re-detects the floor.
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, 2.0f * ElysiumMove::U + ElysiumMove::DistEpsilon);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumCategorizePosition), /*bTraceComplex*/ false, PawnOwner);
	Params.AddIgnoredActor(PawnOwner);
	const bool bHit = UpdatedPrimitive && GetWorld()->SweepSingleByChannel(Hit, Start, End,
		UpdatedComponent->GetComponentQuat(), UpdatedPrimitive->GetCollisionObjectType(),
		UpdatedPrimitive->GetCollisionShape(), Params);

	bOnGround = bHit && Hit.ImpactNormal.Z >= ElysiumMove::StandableZ;
	if (bOnGround)
	{
		// Landing ends the jump: the reduced gravity and the push window belong to the jump, and
		// leaving either armed would carry it into the next one.
		EndJumpHold();
	}
	// 1.0 is the retail value, not a placeholder: VtMB scales the surface's friction by 1.25 and
	// clamps to 1.0, and every world surface resolves to the `default` prop at 0.8
	// (`docs/vtmb/source_movement.md` § surfaceFriction is 1.0 on every world surface).
	SurfaceFriction = 1.0f;
	if (bOnGround && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}
}

void UElysiumMovementComponent::TryPlayerMove(const FVector& Delta)
{
	FVector Remaining = Delta;
	// Four bumps is Source's own iteration count; past that the move is abandoned rather than
	// resolved, which is what stops a wedged player from tunnelling.
	for (int32 Bump = 0; Bump < 4 && !Remaining.IsNearlyZero(); ++Bump)
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Remaining, UpdatedComponent->GetComponentQuat(), /*bSweep*/ true, Hit);
		if (!Hit.IsValidBlockingHit())
		{
			return;
		}
		// Source's own ClipVelocity, overbounce 1.0, applied to both the velocity and what is left
		// of the move. The returned blocked bits are what a two-plane crease case would need.
		FVector Clipped;
		ElysiumMove::ClipVelocity(Velocity, Hit.Normal, Clipped);
		Velocity = Clipped;

		ElysiumMove::ClipVelocity(Remaining * (1.0f - Hit.Time), Hit.Normal, Clipped);
		Remaining = Clipped;
	}
}

void UElysiumMovementComponent::WalkMove(float DeltaTime)
{
	if (!UpdatedComponent)
	{
		return;
	}

	// Ground acceleration belongs to WalkMove, not to its caller — `FullWalkMove` runs friction and
	// then hands over. The vertical component is dropped first, so a walk is planar.
	float Scale = 0.0f;
	const FVector WishDir = WishDirection(PendingCmd, Scale);
	const float Commanded = WishSpeed(WishDir, Scale);
	// What an airborne body keeps commanding, because retail's tables stop refreshing for the jump.
	// Written from the grounded solve rather than from `CategorizePosition`, so it is the speed that
	// was actually integrated and not the one the state says should have been.
	LastGroundedWishSpeed = Commanded;
	CommandedSpeed = Commanded;
	ElysiumMove::ApplyAccelerate(Velocity, WishDir, Commanded, Tuning.Accelerate,
		SurfaceFriction, DeltaTime);
	Velocity.Z = 0.0f;

	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector StartVelocity = Velocity;
	const FVector Delta = Velocity * DeltaTime;

	// Attempt 1 — the flat move.
	TryPlayerMove(Delta);
	const FVector FlatPos = UpdatedComponent->GetComponentLocation();
	const FVector FlatVel = Velocity;

	// Completed? That is the move. (Cheap test: it went where it was asked to.)
	if (FVector::DistSquared2D(FlatPos, Start + Delta) < FMath::Square(ElysiumMove::DistEpsilon))
	{
		return;
	}
	if (!bOnGround)
	{
		return;   // StepMove is a ground behaviour
	}

	// Attempt 2 — raised. Reset, trace up a step, slide forward, trace back down.
	Velocity = StartVelocity;
	UpdatedComponent->SetWorldLocation(Start, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);

	const FVector Up(0.0f, 0.0f, ElysiumMove::StepSize + ElysiumMove::DistEpsilon);
	FHitResult UpHit;
	SafeMoveUpdatedComponent(Up, UpdatedComponent->GetComponentQuat(), true, UpHit);
	TryPlayerMove(FVector(Delta.X, Delta.Y, 0.0f));
	FHitResult DownHit;
	SafeMoveUpdatedComponent(-Up, UpdatedComponent->GetComponentQuat(), true, DownHit);

	const bool bStandable = DownHit.IsValidBlockingHit() && DownHit.ImpactNormal.Z >= ElysiumMove::StandableZ;
	const FVector StepPos = UpdatedComponent->GetComponentLocation();

	// Keep whichever attempt covered more ground horizontally.
	const float FlatDist = FVector::DistSquared2D(FlatPos, Start);
	const float StepDist = FVector::DistSquared2D(StepPos, Start);
	if (!bStandable || FlatDist >= StepDist)
	{
		UpdatedComponent->SetWorldLocation(FlatPos, false, nullptr, ETeleportType::TeleportPhysics);
		Velocity = FlatVel;
		return;
	}
	// The raised attempt won; its Z velocity still comes from the flat one.
	Velocity.Z = FlatVel.Z;
}

void UElysiumMovementComponent::ReduceTimers(float DeltaTime)
{
	// `m_flDucktime` counts down in **milliseconds**, which is why the step is scaled by 1000.
	if (DuckTime > 0.0f)
	{
		DuckTime = FMath::Max(0.0f, DuckTime - DeltaTime * 1000.0f);
	}

	// The jump-hold window is in seconds. When it closes, full gravity comes back — the reduced
	// gravity belongs to the jump, not to being airborne.
	if (JumpHoldRemaining > 0.0f)
	{
		JumpHoldRemaining -= DeltaTime;
		if (JumpHoldRemaining <= 0.0f)
		{
			EndJumpHold();
		}
	}
}

bool UElysiumMovementComponent::CanUnduck() const
{
	if (!UpdatedComponent || !UpdatedPrimitive || !GetWorld())
	{
		return true;
	}

	// VtMB tests the **standing** hull at the origin the stand-up would land on, and refuses if it
	// hits anything. On the ground the feet stay planted, so the centre rises by the half-height
	// gained; airborne the centre does not move at all (`SetHullHeight`). Testing at the
	// destination rather than sweeping from here is what stops a stand-up pushing the body through
	// a ceiling — and, in the air, through the floor.
	const float Grow = (ElysiumMove::StandHeight - ElysiumMove::DuckHeight) * 0.5f;
	const FVector Centre = UpdatedComponent->GetComponentLocation()
		+ FVector(0.0f, 0.0f, bOnGround ? Grow : 0.0f);

	const FCollisionShape Standing = FCollisionShape::MakeBox(FVector(
		ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, ElysiumMove::StandHeight * 0.5f));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumCanUnduck), /*bTraceComplex*/ false, PawnOwner);
	Params.AddIgnoredActor(PawnOwner);
	return !GetWorld()->OverlapBlockingTestByChannel(Centre, UpdatedComponent->GetComponentQuat(),
		UpdatedPrimitive->GetCollisionObjectType(), Standing, Params);
}

void UElysiumMovementComponent::Duck()
{
	AElysiumPawn* Pawn = Cast<AElysiumPawn>(PawnOwner);
	if (!Pawn)
	{
		return;
	}

	const uint64 DuckBit = static_cast<uint64>(EElysiumButton::Duck);
	const bool bDuckDown = PendingCmd.IsDown(EElysiumButton::Duck);

	// **The press edge toggles; the release does nothing.** This is the action layer, and it is the
	// only place the crouch is retained: the command says whether the key is down this frame
	// (`docs/architecture/animation-architecture.md` § 3 — "a key press never selects an animation
	// asset"), the classifier reads the realized stance off the body sample, and neither of them
	// holds a crouch between frames. `m_nOldButtons` keeps tracking the button's true level, because
	// that is what makes the edge detectable at all — the same latch `CheckJumpButton` uses.
	const bool bPressEdge = bDuckDown && !(OldButtons & DuckBit);
	const bool bRequestRose = bPressEdge && !bDuckRequested;
	if (bPressEdge)
	{
		bDuckRequested = !bDuckRequested;
	}
	OldButtons = bDuckDown ? (OldButtons | DuckBit) : (OldButtons & ~DuckBit);

	const bool bWantsDuck = bDuckRequested;

	// **Airborne, the transition does not ramp — it completes on the spot.** `Duck` only takes the
	// `SetDuckedEyeOffset` lerp branch when `GetGroundEntity()` is non-null; every other path falls
	// straight through to `Finish(Un)Duck`. That is what makes the crouch-jump exist: the hull
	// shrinks on the frame the button goes down, mid-flight, lifting the feet 18 units. Ramping
	// instead would put the shrink 0.4s after the press — longer than the whole 0.5s jump — so a
	// step between 25 and 43 units becomes unclimbable (`docs/vtmb/source_movement.md` → "Ducking").
	const bool bInAir = !bOnGround;

	if (bWantsDuck)
	{
		// The ramp starts on the edge of the **request**, not of the button — under a toggle the key
		// is up for almost the whole crouch, so keying this on the button would start the lowering
		// ramp and then never advance it.
		if (bRequestRose && !bDucked)
		{
			bDucking = true;
			DuckTime = ElysiumMove::GameMovementDuckTime;
		}

		if (bDucking && !bDucked)
		{
			const float Elapsed = (ElysiumMove::GameMovementDuckTime - DuckTime) * 0.001f;
			if (Elapsed >= ElysiumMove::TimeToDuck || bInAir)
			{
				FinishDuck();
			}
			else if (AElysiumPawn* P = Cast<AElysiumPawn>(PawnOwner))
			{
				// Only the eye moves during the ramp; the hull stays standing until the finish.
				P->SetEyeHeight(FMath::Lerp(ElysiumMove::StandViewZ, ElysiumMove::DuckViewZ,
					Elapsed / ElysiumMove::TimeToDuck));
			}
		}
	}
	else
	{
		if (bDucked || bDucking)
		{
			if (!bDucking)
			{
				// The request went false: start the unduck ramp.
				bDucking = true;
				DuckTime = ElysiumMove::GameMovementDuckTime;
			}

			const float Elapsed = (ElysiumMove::GameMovementDuckTime - DuckTime) * 0.001f;
			// The unduck is gated on headroom as well as on time — a stand-up under a low ceiling
			// simply keeps waiting rather than pushing the body through it.
			if (Elapsed >= ElysiumMove::TimeToUnduck || bInAir)
			{
				if (CanUnduck())
				{
					FinishUnDuck();
				}
			}
			else if (AElysiumPawn* P = Cast<AElysiumPawn>(PawnOwner))
			{
				P->SetEyeHeight(FMath::Lerp(ElysiumMove::DuckViewZ, ElysiumMove::StandViewZ,
					Elapsed / ElysiumMove::TimeToUnduck));
			}
		}
	}
}

void UElysiumMovementComponent::FinishDuck()
{
	if (AElysiumPawn* Pawn = Cast<AElysiumPawn>(PawnOwner))
	{
		Pawn->SetHullHeight(ElysiumMove::DuckHeight, ElysiumMove::DuckViewZ, /*bAnchorFeet*/ bOnGround);
	}
	bDucked = true;
	bDucking = false;
	DuckTime = 0.0f;
}

void UElysiumMovementComponent::FinishUnDuck()
{
	if (AElysiumPawn* Pawn = Cast<AElysiumPawn>(PawnOwner))
	{
		Pawn->SetHullHeight(ElysiumMove::StandHeight, ElysiumMove::StandViewZ, /*bAnchorFeet*/ bOnGround);
	}
	bDucked = false;
	bDucking = false;
	DuckTime = 0.0f;
}

void UElysiumMovementComponent::NoclipMove(float DeltaTime)
{
	float Scale = 0.0f;
	const FVector WishDir = WishDirection(PendingCmd, Scale);
	CommandedSpeed = WishSpeed(WishDir, Scale);
	Velocity = WishDir * CommandedSpeed;
	// No sweep: the point of noclip is to pass through geometry, and the pawn has already dropped
	// its collision.
	MoveUpdatedComponent(Velocity * DeltaTime, UpdatedComponent->GetComponentQuat(), /*bSweep*/ false);
}

void UElysiumMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ShouldSkipUpdate(DeltaTime) || !PawnOwner || !UpdatedComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	// Retail rebinds `frametime` to the user command's OWN timing for the duration of the move
	// (RE21): CPlayerMove::RunCommand runs on the command's interval, not on the server frame's.
	// The router builds one command per frame, so today this is the identity — it stops being the
	// identity the moment a frame carries more or fewer than one command (a replayed command
	// stream, a hitch clamp, a fixed step), and the mover must follow the command either way.
	if (PendingCmd.DeltaSeconds > 0.0f)
	{
		DeltaTime = PendingCmd.DeltaSeconds;
	}
	// The scale comes from the world settings, which is the number the engine's own filter already
	// applied to this frame — not the substrate clock, which the Player layer does not reach.
	const AWorldSettings* FrameSettings = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr;
	DeltaTime = static_cast<float>(ElysiumFrame::ClampFrameDelta(
		DeltaTime, FrameSettings ? FrameSettings->GetEffectiveTimeDilation() : 1.0));

	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
		// A frozen body is settled, not unknown: it publishes a standing-still sample rather than
		// holding the last moving one, which would animate a spawn hold as a walk. No move function
		// ran, so the wish is reported as absent rather than as whatever the last live frame asked.
		PublishLocomotionSample(/*bSolved*/ false);
		PrevCmd = PendingCmd;
		return;
	}

	// Re-read the `sv_*` surface once per frame, so a live `elysium.cmd sv_gravity 400` takes effect
	// the way the original's would. An unset name keeps VtMB's own compiled-in default.
	Tuning.LoadFrom([](const TCHAR* Name) { return ElysiumCommandBus::Console().GetCvar(Name); });

	// The jump's own numbers come from `vdata/system/rules.txt`, not from a cvar — read once, since
	// the rulebook is not hot-reloaded. Absent keys keep the shipped defaults.
	if (!bJumpTuningLoaded)
	{
		bJumpTuningLoaded = true;
		if (const UWorld* W = GetWorld())
		{
			if (UElysiumRulebookSubsystem* Rulebook = UGameInstance::GetSubsystem<UElysiumRulebookSubsystem>(W->GetGameInstance()))
			{
				const FElysiumRules& Rules = Rulebook->Rules();
				Tuning.LoadJumpFrom([&Rules](const TCHAR* Key, float& Out) -> bool
				{
					if (!Rules.Has(TEXT("Jumping"), Key))
					{
						return false;
					}
					Out = Rules.Flt(TEXT("Jumping"), Key);
					return true;
				});
			}
		}
	}

	// The command stays authoritative for HOW MUCH time to integrate (RE21); the stepper decides
	// only how that time is chopped up. Total integrated time is the same in both modes, up to the
	// carried remainder.
	Stepper.FixedStep = FMath::Max(0.0f, CVarMoveFixedStep.GetValueOnGameThread());

	float StepSeconds = 0.0f;
	const int32 Steps = Stepper.BeginFrame(DeltaTime, StepSeconds);

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		PlayerMove(StepSeconds);
	}

	PrevCmd = PendingCmd;
	UpdateComponentVelocity();

	// The tick tail (CCC1). After the last substep is the only point where the state is settled, so
	// no consumer can read a half-integrated frame; after `UpdateComponentVelocity` so the sample and
	// `GetVelocity()` agree by construction.
	//
	// Two paths deliberately do not reach here, and both **hold the previous sample** rather than
	// publishing a settled one. The `ShouldSkipUpdate` / `DeltaTime <= 0` guard above is a paused or
	// dormant frame — zeroing there would snap a run cycle to idle and back on resume. And
	// `Steps == 0` happens under a non-zero `elysium.move.FixedStep` whenever the accumulator has not
	// reached a step: nothing integrated, so last frame's answer is still the true one.
	if (Steps > 0)
	{
		PublishLocomotionSample(/*bSolved*/ true);
	}
}

void UElysiumMovementComponent::PlayerMove(float DeltaTime)
{
	if (bNoclip)
	{
		NoclipMove(DeltaTime);
		return;
	}

	// PlayerMove's own order (`docs/vtmb/source_movement.md` → "Where each move function lives"): the timers
	// and the duck run first, so the ground trace below sees the hull this step will move with.
	ReduceTimers(DeltaTime);
	Duck();
	CategorizePosition();

	FullWalkMove(DeltaTime);
}

void UElysiumMovementComponent::FullWalkMove(float DeltaTime)
{
	const bool bInWater = WaterLevel >= EElysiumWaterLevel::Waist;

	// The FIRST half of the interval's gravity.
	if (!bInWater)
	{
		ElysiumMove::StartGravity(Velocity, Tuning.Gravity * GravityScale, DeltaTime);
	}

	if (bInWater)
	{
		WaterMove(DeltaTime);
		CategorizePosition();
	}
	else
	{
		CheckJumpButton();

		if (bOnGround)
		{
			Velocity.Z = 0.0f;
			ElysiumMove::ApplyFriction(Velocity, Tuning.Friction, Tuning.StopSpeed,
				SurfaceFriction, DeltaTime);
		}
		ElysiumMove::CheckVelocity(Velocity, Tuning.MaxVelocity);

		if (bOnGround)
		{
			WalkMove(DeltaTime);
		}
		else
		{
			AirMove(DeltaTime);
		}
		CategorizePosition();
	}

	ElysiumMove::CheckVelocity(Velocity, Tuning.MaxVelocity);

	// The SECOND half. Splitting it is what puts the jump apex at a flat `sv_jump_boost` rather
	// than `sv_jump_boost - 100*dt`.
	if (!bInWater)
	{
		ElysiumMove::FinishGravity(Velocity, Tuning.Gravity * GravityScale, DeltaTime);
	}

	if (bOnGround)
	{
		Velocity.Z = 0.0f;
	}
}

void UElysiumMovementComponent::AirMove(float DeltaTime)
{
	float Scale = 0.0f;
	const FVector WishDir = WishDirection(PendingCmd, Scale);
	const float AirWishSpeed = WishSpeed(WishDir, Scale);
	CommandedSpeed = AirWishSpeed;

	ElysiumMove::ApplyAirAccelerate(Velocity, WishDir, AirWishSpeed, Tuning.AirAccel,
		Tuning.AirSpeedCap, SurfaceFriction, DeltaTime);

	// The air move slides against geometry too — it just never runs the step attempt.
	TryPlayerMove(Velocity * DeltaTime);
}

void UElysiumMovementComponent::WaterMove(float DeltaTime)
{
	// Formula-faithful and unexercised: nothing sets WaterLevel, because no exported map places a
	// water brush (`docs/vtmb/source_movement.md` → "Water"). It is here so the state machine is Source's
	// shape rather than a subset, and so the day a water map exports this is wiring, not a port.
	float Scale = 0.0f;
	// Swimming aims where you look, pitch included — which is why it goes through the member with
	// the pitch forced rather than calling the free function: the wish the published sample reports
	// has to be the one the solve used, and a planar re-derivation would disagree here and nowhere
	// else.
	FVector WishDir = WishDirection(PendingCmd, Scale, /*bForcePitch*/ true);
	float SwimSpeed = WishSpeed(WishDir, Scale);

	if (Scale <= 0.0f && PendingCmd.Buttons == 0)
	{
		// Idle in water sinks. VtMB's rate is 40, not HL2's 60. Substituted after the capture above:
		// the sink is a water-locomotion state, not something the player asked for, and reporting it
		// as the wish would read an idle swimmer as pressing down.
		WishDir = -FVector::UpVector;
		SwimSpeed = ElysiumMove::WaterSinkSpeed;
	}
	SwimSpeed *= ElysiumMove::WaterSpeedScale;
	CommandedSpeed = SwimSpeed;

	ElysiumMove::ApplyWaterFriction(Velocity, Tuning.Friction, SurfaceFriction, DeltaTime);
	ElysiumMove::ApplyAccelerate(Velocity, WishDir, SwimSpeed, Tuning.Accelerate,
		SurfaceFriction, DeltaTime);

	TryPlayerMove(Velocity * DeltaTime);
}

void UElysiumMovementComponent::CheckJumpButton()
{
	// Source's `m_nOldButtons` rule, not a press edge against the previous *command*. It is the
	// faithful one (a held jump does not pogo), and it is also what makes sub-stepping correct: the
	// latch is consumed on the first step, so one press cannot fire a jump per sub-step.
	if (!PendingCmd.IsDown(EElysiumButton::Jump))
	{
		OldButtons &= ~static_cast<uint64>(EElysiumButton::Jump);
		// Releasing ends the push early. This is the whole reason a tap and a hold give different
		// heights, and why the mousewheel — which cannot be held — only ever hops.
		EndJumpHold();
		return;
	}
	if (OldButtons & static_cast<uint64>(EElysiumButton::Jump))
	{
		// Held. VtMB's jump is a **constant upward push for a window**, not a single impulse, so
		// the velocity is re-asserted every frame the window is still open rather than being left
		// to gravity. `ReduceTimers` closes the window and restores full gravity.
		if (JumpHoldRemaining > 0.0f)
		{
			Velocity.Z = FMath::Max(Velocity.Z, Tuning.BaseJumpVelocity);
		}
		return;
	}
	if (!bOnGround)
	{
		return;
	}

	// --- The press edge -------------------------------------------------------------------------
	// `sv_jump_boost` first: an instant **origin** pop of 25 inches, scaled by 0.99 and by how far
	// the hull actually gets, so it can never seat the body inside a ceiling.
	ApplyJumpBoost();

	// **Additive, not an overwrite** (`vampire.dll` 0x101226b0: `v.z = impulse * groundFactor + v.z`).
	// `StartGravity` has already taken half the step's gravity off `v.z`, and adding on top of that
	// is what leaves the launch at the half-step the leapfrog integration wants. The ground factor
	// is the surface's own jump scale, 1.0 for the `default` prop every world surface resolves to.
	Velocity.Z += Tuning.BaseJumpVelocity;

	// The jump runs under reduced gravity for its whole duration — VtMB sets the player's own
	// gravity scale (`m_flGravity`, `player+0x3ec`), which `Start`/`FinishGravity` multiply by.
	GravityScale = Tuning.JumpGravityMultiplier;
	// ...and the push keeps being applied while the button is held, which is why tapping the
	// mousewheel hops and holding space clears a crate.
	const float HoldOverride = CVarJumpHoldSeconds.GetValueOnGameThread();
	JumpHoldRemaining = HoldOverride > 0.0f ? HoldOverride : Tuning.JumpHoldSeconds;

	bOnGround = false;
	OldButtons |= static_cast<uint64>(EElysiumButton::Jump);

	// The leniency courses' whole measurement (CCC3): a press that reached this line became a jump,
	// and one that hit the `!bOnGround` bail above did not. Counted here rather than inferred from a
	// trace because the two are indistinguishable in the recorded position of a body that was
	// falling anyway.
	++JumpsTaken;
}

void UElysiumMovementComponent::ApplyJumpBoost()
{
	const float Pop = Tuning.JumpBoost * ElysiumMove::U * ElysiumMove::JumpBoostScale;
	if (Pop <= 0.0f || !UpdatedComponent)
	{
		return;
	}
	// Sweep rather than teleport: the original scales the pop by its own `TracePlayerBBox`
	// fraction, and a swept move is the same thing expressed with the engine's tracer.
	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector(0.0f, 0.0f, Pop), UpdatedComponent->GetComponentQuat(),
		/*bSweep*/ true, Hit);
}

void UElysiumMovementComponent::EndJumpHold()
{
	JumpHoldRemaining = 0.0f;
	GravityScale = 1.0f;
}
