#include "ElysiumMovementComponent.h"

#include "ElysiumGameClock.h"
#include "ElysiumPawn.h"
#include "Debug/ElysiumConsole.h"
#include "Player/ElysiumCommandBus.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Engine/GameInstance.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

// The timestep A/B. **0 is the faithful baseline** — VtMB has no tick, so its movement really is
// frame-rate dependent (`docs/source_movement.md` → "Frame timing"). A non-zero value opts into
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
// engine uses is not transcribed. `docs/source_movement.md` records that as unverified.
static TAutoConsoleVariable<float> CVarJumpHoldSeconds(
	TEXT("elysium.jump.HoldSeconds"),
	0.0f,
	TEXT("Seconds the jump's upward push is sustained while held. 0 = rules.txt JumpHoldTime."),
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

float UElysiumMovementComponent::GetMaxSpeed() const
{
	if (bNoclip)
	{
		return ElysiumMove::NoclipSpeed * (PendingCmd.IsDown(EElysiumButton::Speed) ? ElysiumMove::NoclipBoost : 1.0f);
	}
	if (!bOnGround)
	{
		return Tuning.JumpMaxSpeed;
	}
	// `+speed` selects the *slow* gait: the run is the default, holding the key walks.
	const float Base = PendingCmd.IsDown(EElysiumButton::Speed)
		? ElysiumMove::WalkSpeed : ElysiumMove::RunSpeed;
	// Source's duck speed is a third of the gait — `sv_sneakscale` 2.3 divides it in the retail
	// PreThink, but that path is animation-driven (`source_movement.md` § "Player speed is
	// animation-driven"), so this is the one movement number still standing on Source's own default
	// rather than a read-out VtMB value. Marked so it is not mistaken for RE'd.
	return (bDucked || bDucking) ? Base / 3.0f : Base;
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
	PrevCmd = FElysiumUserCmd();
	Stepper.Reset();
	bOnGround = false;
	WaterLevel = EElysiumWaterLevel::None;
	EndJumpHold();

	// Stand up if we were crouched, so the hull the body arrives with is the standing one.
	if (bDucked || bDucking)
	{
		FinishUnDuck();
	}
	DuckTime = 0.0f;
}

void UElysiumMovementComponent::SetFrozen(bool bInFrozen)
{
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
	}
}

FVector UElysiumMovementComponent::WishDirection(const FElysiumUserCmd& Cmd, float& OutScale) const
{
	const AController* C = PawnOwner ? PawnOwner->GetController() : nullptr;
	const FRotator ViewRot = C ? C->GetControlRotation() : (PawnOwner ? PawnOwner->GetActorRotation() : FRotator::ZeroRotator);

	// Walking is on the ground plane; noclip flies along the aim, pitch included.
	return ElysiumMove::WishDirection(Cmd.Move, Cmd.Up, ViewRot, /*bIncludePitch*/ bNoclip, OutScale);
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
	// (`source_movement.md` § surfaceFriction is 1.0 on every world surface).
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
	ElysiumMove::ApplyAccelerate(Velocity, WishDir, GetMaxSpeed() * Scale, Tuning.Accelerate,
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

	const bool bWantsDuck = PendingCmd.IsDown(EElysiumButton::Duck);
	const uint64 DuckBit = static_cast<uint64>(EElysiumButton::Duck);

	// **Airborne, the transition does not ramp — it completes on the spot.** `Duck` only takes the
	// `SetDuckedEyeOffset` lerp branch when `GetGroundEntity()` is non-null; every other path falls
	// straight through to `Finish(Un)Duck`. That is what makes the crouch-jump exist: the hull
	// shrinks on the frame the button goes down, mid-flight, lifting the feet 18 units. Ramping
	// instead would put the shrink 0.4s after the press — longer than the whole 0.5s jump — so a
	// step between 25 and 43 units becomes unclimbable (`docs/source_movement.md` → "Ducking").
	const bool bInAir = !bOnGround;

	if (bWantsDuck)
	{
		// The press edge, latched into OldButtons the same way the jump is — so a held duck does
		// not restart the transition every step.
		if (!(OldButtons & DuckBit))
		{
			OldButtons |= DuckBit;
			if (!bDucked)
			{
				bDucking = true;
				DuckTime = ElysiumMove::GameMovementDuckTime;
			}
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
		OldButtons &= ~DuckBit;

		if (bDucked || bDucking)
		{
			if (!bDucking)
			{
				// The release edge: start the unduck ramp.
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
	Velocity = WishDir * GetMaxSpeed() * Scale;
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
	DeltaTime = static_cast<float>(ElysiumFrame::ClampFrameDelta(DeltaTime));

	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
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
}

void UElysiumMovementComponent::PlayerMove(float DeltaTime)
{
	if (bNoclip)
	{
		NoclipMove(DeltaTime);
		return;
	}

	// PlayerMove's own order (`source_movement.md` → "Where each move function lives"): the timers
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
	const float WishSpeed = GetMaxSpeed() * Scale;

	ElysiumMove::ApplyAirAccelerate(Velocity, WishDir, WishSpeed, Tuning.AirAccel,
		Tuning.AirSpeedCap, SurfaceFriction, DeltaTime);

	// The air move slides against geometry too — it just never runs the step attempt.
	TryPlayerMove(Velocity * DeltaTime);
}

void UElysiumMovementComponent::WaterMove(float DeltaTime)
{
	// Formula-faithful and unexercised: nothing sets WaterLevel, because no exported map places a
	// water brush (`source_movement.md` → "Water"). It is here so the state machine is Source's
	// shape rather than a subset, and so the day a water map exports this is wiring, not a port.
	const AController* C = PawnOwner ? PawnOwner->GetController() : nullptr;
	const FRotator ViewRot = C ? C->GetControlRotation()
		: (PawnOwner ? PawnOwner->GetActorRotation() : FRotator::ZeroRotator);

	float Scale = 0.0f;
	// Swimming aims where you look, pitch included.
	FVector WishDir = ElysiumMove::WishDirection(PendingCmd.Move, PendingCmd.Up, ViewRot,
		/*bIncludePitch*/ true, Scale);
	float WishSpeed = GetMaxSpeed() * Scale;

	if (Scale <= 0.0f && PendingCmd.Buttons == 0)
	{
		// Idle in water sinks. VtMB's rate is 40, not HL2's 60.
		WishDir = -FVector::UpVector;
		WishSpeed = ElysiumMove::WaterSinkSpeed;
	}
	WishSpeed *= ElysiumMove::WaterSpeedScale;

	ElysiumMove::ApplyWaterFriction(Velocity, Tuning.Friction, SurfaceFriction, DeltaTime);
	ElysiumMove::ApplyAccelerate(Velocity, WishDir, WishSpeed, Tuning.Accelerate,
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
