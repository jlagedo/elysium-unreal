#include "ElysiumMovementComponent.h"

#include "ElysiumGameClock.h"
#include "ElysiumGroundSurface.h"   // the surfaceprop under the foot, off the ground trace
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


// How long the jump's push keeps being applied. 0 = use `rules.txt`'s `JumpHoldTime` (0.2). This
// is **ours, not a VtMB cvar** — it exists because `rules.txt` carries two candidate windows
// (`JumpHoldTime` 0.2 and the Feat-indexed `JumpDuration`, 0.11 at rank 1) and which one the
// engine uses is not transcribed. `docs/vtmb/source_movement.md` records that as unverified.
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

FElysiumWishSpeedInput UElysiumMovementComponent::BodySpeedInput() const
{
	FElysiumWishSpeedInput In;
	In.bNoclip = bNoclip;
	In.bOnGround = bOnGround;
	In.bDucked = bDucked || bDucking;
	In.bWalkKey = PendingCmd.IsDown(EElysiumButton::Speed);
	In.LastGroundedWishSpeed = LastGroundedWishSpeed;
	In.JumpMaxSpeed = Tuning.JumpMaxSpeed;
	In.NoclipSpeed = ElysiumMove::NoclipSpeed
		* (In.bWalkKey ? ElysiumMove::NoclipBoost : 1.0f);
	return In;
}

float UElysiumMovementComponent::GetMaxSpeed() const
{
	// **The ceiling, not the gait.** This is retail's `m_flMaxspeed`: the peak over every cell of
	// every gait table while grounded, `sv_jump_maxspeed` while the jump phase is live, published
	// for `UNavMovementComponent`'s contract and for readouts.
	//
	// It is also the cap the solve clamps against, at exactly one place — `SetupMove` bounds the
	// wish it substitutes from a playing sequence (`CheckParameters`). An authored lunge is the only
	// input that can exceed every gait cell: a player-driven wish IS a cell, so the clamp reaches
	// nothing else in the solve, which asks `WishSpeed()` for a direction instead.
	return ElysiumGait::MaxSpeedFrom(BodySpeedInput(), GaitSpeeds);
}

const FElysiumGaitSpeedTable& UElysiumMovementComponent::GaitTableForCommand() const
{
	return ElysiumGait::TableFor(GaitSpeeds, bDucked || bDucking,
		PendingCmd.IsDown(EElysiumButton::Speed));
}

float UElysiumMovementComponent::WishSpeed(const FVector& WishDir, float Scale) const
{
	if (bSubstitutedMove)
	{
		// The sequence's own speed, and it is not a gait: retail's `SetupMove` writes the authored
		// displacement over the frame straight into the command, so there is no table cell to read
		// and no `+speed` ladder to walk. It answers here rather than at each move function so the
		// walk, the air move and the water move all take it — which is the level retail substitutes
		// at.
		return static_cast<float>(SubstitutedWish.Size());
	}
	FElysiumWishSpeedInput In = BodySpeedInput();
	// The **commanded** direction, not the realized one: retail picks the cell from the keys being
	// held, before the move integrates. The pose parameter the graph steers on is a different angle
	// and is deliberately filtered; this one never is.
	In.WishYawDegrees = ElysiumLocomotion::RelativeYaw(
		static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(WishDir.Y, WishDir.X))),
		static_cast<float>(ViewFrame().Yaw));
	In.Scale = Scale;
	const float Commanded = ElysiumGait::WishSpeedFrom(In, GaitSpeeds);

	// **A command erased by tables nothing ever published.** A gait that resolved no fan commands
	// zero on purpose — that is retail's unwritten slot, and `UElysiumAnimSubsystem::ResolveGaitSpeeds`
	// has already named the body and the gait it could not answer for. A mover that was never handed
	// tables at all is a different thing and nobody else's to report: retail's player always wears a
	// model, so there is no faithful behaviour being reproduced here, just a body that cannot walk.
	// Latched, and only when a real grounded command is what got erased — a body standing still with
	// no tables is not failing at anything.
	if (!bReportedNoGaitAuthority && !bGaitSpeedsPublished && In.bOnGround && !In.bNoclip
		&& Scale > 0.0f)
	{
		bReportedNoGaitAuthority = true;
		UE_LOG(LogElysiumMovement, Warning,
			TEXT("'%s' commands no speed: nothing has published gait tables to this mover, so every ")
			TEXT("grounded command resolves to zero. The body's visual, and with it the speed ")
			TEXT("authority, was never built."),
			PawnOwner ? *PawnOwner->GetName() : TEXT("(no pawn)"));
	}
	return Commanded;
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
	bOnGround = false;
	// The surface goes with the ground contact, exactly as retail's reset clears the NPC's own
	// cache (`0x10273390` / `0x1027bf50`): a body arriving somewhere new is not standing on what it
	// left, and the next `CategorizePosition` is what says what it IS standing on.
	GroundSurface = FName();
	WaterLevel = EElysiumWaterLevel::None;
	// A teleport is not a landing: the fall the body was in belonged to where it left, and carrying
	// the speed across would make the arrival play a hard landing step it never fell for.
	FallVelocity = 0.0f;
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

	// The swing that owned the command goes with the carried motion. A body arriving somewhere new is
	// not mid-lunge, and the animation pass re-pushes the lock next frame if it really still is.
	AnimLock = FElysiumAnimMovementLock();
	bSubstitutedMove = false;
	SubstitutedWish = FVector::ZeroVector;

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

void UElysiumMovementComponent::StopBody()
{
	// `SetAbsVelocity(vec3_origin)` — all three components, including the vertical one. The tail of
	// a grounded swing carries no Z worth speaking of, but the recovered call takes the whole vector
	// and a horizontal-only stop would be a rule of our own.
	Velocity = FVector::ZeroVector;

	// `SetLocalVelocity(vec3_origin)` — the published record, brought into agreement with the
	// component. The rule is a free function so a test can assert it against a lunging sample with
	// no pawn, no world and no mover; what it clears and what it deliberately leaves standing is
	// stated where it is declared.
	ElysiumLocomotion::ClearMotion(LastSample);
}

void UElysiumMovementComponent::SetFrozen(bool bInFrozen)
{
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
		// The swing that owned the command goes with the carried motion, exactly as it does through
		// `ResetState`. Clearing only the derived `bSubstitutedMove` — which the frozen tick branch
		// does every frame — leaves the INPUT standing, so a body unfrozen after a swing ended would
		// resample a window frozen at the cycle it was locked at and assign one constant velocity
		// forever. The animation pass re-pushes the lock next frame if the body really is still
		// mid-swing.
		AnimLock = FElysiumAnimMovementLock();
		bSubstitutedMove = false;
		SubstitutedWish = FVector::ZeroVector;
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
	// The substituted command, when `SetupMove` refilled one. It goes through this member rather than
	// past it so the published sample reports the wish the solve actually used — retail writes
	// `cmdMoveMag` from the substituted values too, and a body lunging on an authored path that
	// reported no wish at all would read as standing still.
	if (bSubstitutedMove)
	{
		const FVector Wish = SubstitutedWish.GetSafeNormal();
		OutScale = Wish.IsNearlyZero() ? 0.0f : 1.0f;
		CapturedWish = Wish;
		CapturedWishScale = OutScale;
		return Wish;
	}

	// Walking is on the ground plane; noclip flies along the aim and swimming aims where you look,
	// both pitch included.
	const FVector Wish = ElysiumMove::WishDirection(Cmd.Move, Cmd.Up, ViewFrame(),
		/*bIncludePitch*/ bNoclip || bForcePitch, OutScale);
	CapturedWish = Wish;
	CapturedWishScale = OutScale;
	return Wish;
}

void UElysiumMovementComponent::SetupMove(float DeltaTime)
{
	bSubstitutedMove = false;
	SubstitutedWish = FVector::ZeroVector;
	if (!AnimLock.bActive || DeltaTime <= 0.0f)
	{
		return;
	}

	// **The discard comes first and is unconditional.** Retail zeroes the three movement axes before
	// it asks the sequence for anything, so a locked body whose clip authors no displacement still
	// stops — the command is gone either way, and only the refill is conditional. The BUTTONS are
	// untouched: `+speed`, `+duck` and the attack bits are not movement axes, and retail leaves them
	// alone as well.
	PendingCmd.Move = FVector2D::ZeroVector;
	PendingCmd.Up = 0.0f;

	if (!AnimLock.HasPath())
	{
		// A clip that authors no `mstudiomovement_t` record at all. `Studio_AnimMovement` returns
		// false, nothing refills the command, and what holds the body in place is the ordinary
		// friction of a zero wish — not an assignment. That is a value the file states, not a gap.
		return;
	}

	// The window this step will advance through, clamped into the clip exactly as retail's is: the
	// lower bound cannot go below zero and the upper cannot run past the clip's end.
	const float CycleFrom = FMath::Max(0.0f, AnimLock.Cycle);
	const float CycleTo = FMath::Min(1.0f, CycleFrom + AnimLock.CycleRate * DeltaTime);
	FVector DeltaCm = FVector::ZeroVector;
	if (!ElysiumClipMovement::SampleDelta(*AnimLock.Path, AnimLock.FrameCount, CycleFrom, CycleTo,
		DeltaCm))
	{
		// `Studio_AnimMovement` said there is nothing to sample at all. The discarded command stays
		// unrefilled and the body is held by ordinary friction. A window that merely does not ADVANCE
		// is not this case: it answers true with a zero delta, which substitutes a stop.
		return;
	}

	// The delta is in the clip's own local frame — X forward, Y right, Z up, already Unreal-native —
	// so it is carried into the world by the same view frame the ordinary wish is built in. Retail
	// composes the identical basis out of `forwardmove`/`sidemove`, which is what its `x` and `-y`
	// assignments are: the negation there IS the reflection this export already spent.
	const FRotator Frame(0.0f, static_cast<float>(ViewFrame().Yaw), 0.0f);
	SubstitutedWish = Frame.RotateVector(DeltaCm) / DeltaTime;

	// **`CheckParameters`' clamp, and the substituted command is what it was written for.** Retail
	// bounds the refilled `forwardmove`/`sidemove`/`upmove` as one 3-vector against `m_flMaxspeed`
	// before any move function runs, and `WalkMove` bounds the same ceiling again in the wishvel
	// slots its direct-assignment fork reads — so BOTH of retail's clamps sit above the fork and the
	// assignment is capped. The 3-vector form is the stricter (it counts `up`, which `WalkMove`
	// zeroes first), and 56 shipped records author a non-zero `pos_z_cm`, so it is the one to run.
	//
	// The ceiling is re-read every step rather than latched when the lock armed: `PreThink` refills
	// the gait tables ahead of the lock predicate and is not gated by it — `FElysiumAnimationDriver::
	// Tick` refreshes its fan above the same guard — so the cap keeps tracking the live run peak for
	// the whole of a grounded swing — and `sv_jump_maxspeed` for an airborne
	// one, which `GetMaxSpeed` answers off the `bOnGround` the previous step settled, exactly where
	// retail's `PreThink` reads its jump phase.
	//
	// Only the substituted wish is clamped, because only it is in cm/s. An ordinary command's axes
	// are deflections and its speed enters later, off a gait cell the ceiling covers by
	// construction.
	//
	// **A ceiling of zero is not a ceiling.** `MaxSpeedFrom` answers zero for a grounded body whose
	// gait tables never published, and clamping to it would multiply the authored displacement by
	// nothing — deleting the whole lunge while `bSubstitutedMove` still claims the command. That is a
	// body whose visual failed to build, not a game rule, so the authored wish stands and the failure
	// is reported instead. `WishSpeed`'s own latch already names the body once; this names the swing
	// that was nearly eaten by it.
	const float Ceiling = GetMaxSpeed();
	const double Authored = SubstitutedWish.Size();
	if (!(Ceiling > 0.0f))
	{
		if (!bReportedNoClampCeiling)
		{
			bReportedNoClampCeiling = true;
			UE_LOG(LogElysiumMovement, Warning,
				TEXT("'%s' substitutes an authored move of %.1f cm/s against a speed ceiling of zero; ")
				TEXT("the ceiling is refused rather than applied, which would delete the move."),
				PawnOwner ? *PawnOwner->GetName() : TEXT("(no pawn)"), Authored);
		}
	}
	else if (ElysiumMove::ClampCommandSpeed(SubstitutedWish, Ceiling))
	{
		// Faithful — `docs/vtmb/source_movement.md` measures an authored displacement as a request
		// rather than a guarantee — but invisible, because the clip still plays at rate 1.0 while the
		// body covers less ground than it animates. Said once per body so a foot slide has a cause in
		// the log rather than only in the frame.
		if (!bReportedClampedLunge)
		{
			bReportedClampedLunge = true;
			UE_LOG(LogElysiumMovement, Log,
				TEXT("'%s' authored %.1f cm/s over this step and the live speed ceiling is %.1f, so the ")
				TEXT("move lands short of its authored distance while the clip plays at rate 1."),
				PawnOwner ? *PawnOwner->GetName() : TEXT("(no pawn)"), Authored, Ceiling);
		}
	}
	bSubstitutedMove = true;
}

void UElysiumMovementComponent::PublishLocomotionSample(bool bSolved)
{
	// The same expression the wish was framed in, or the sample's facing and its `move_yaw` would be
	// two different frames wearing one name.
	LastSample.FacingYaw = static_cast<float>(ViewFrame().Yaw);
	// The other half of the same frame, and the only consumer of the view's vertical: the movement
	// solve is planar, so this is carried for the pose parameter rather than used here.
	LastSample.ViewPitch = static_cast<float>(ViewFrame().Pitch);

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
	// The surfaceprop `CategorizePosition` just read off the ground trace, unfiltered by the noclip
	// answer above: `bOnGround` is what says whether the body is standing, and a mover with no floor
	// already carries `NAME_None` here.
	LastSample.GroundSurface = GroundSurface;
	LastSample.Water = WaterLevel;
	LastSample.Stance = ElysiumLocomotion::StanceFrom(bDucked, bDucking);
	LastSample.JumpHoldRemaining = JumpHoldRemaining;
	// Always false, and faithfully so: VtMB's `PlayerMove` movetype switch has no ladder arm and no
	// map places a ladder entity (`docs/vtmb/source_movement.md` → "Ladders: VtMB has none"), so
	// retail's own `UpdateStepSound` ladder branch is unreachable as well. Written explicitly rather
	// than left to the sample's default, so the line that a climbing modernization would change is
	// where the reader looks for it.
	LastSample.bOnLadder = false;

	// **The landing signal, on the one frame it exists.** `CheckFalling` (`0x10125db0`) is entered
	// with a ground entity and reads `m_flFallVelocity`; the field is then cleared, which is what
	// makes the landing a single event rather than a state. Published off the RAW ground answer
	// (`bOnGround`) and not `IsMovingOnGround()`, because a noclipping body has no landing to
	// report and its raw answer is already false.
	LastSample.FallSpeedAtLanding = bOnGround ? FMath::Max(0.0f, FallVelocity) : 0.0f;
	if (bOnGround)
	{
		FallVelocity = 0.0f;   // `0x10126226`: cleared on any move that has a ground entity
	}
}

void UElysiumMovementComponent::CategorizePosition()
{
	if (!UpdatedComponent)
	{
		bOnGround = false;
		GroundSurface = FName();
		return;
	}
	// Rising: never on the ground. Source's own gate, and what keeps a jump from re-grounding on
	// the frame it leaves — including every frame of the held push, which holds `v.z` at the full
	// launch speed and would otherwise re-ground the moment the body brushed a surface.
	if (Velocity.Z > Tuning.BaseJumpVelocity * 0.5f)
	{
		bOnGround = false;
		GroundSurface = FName();
		return;
	}

	// A 2u down-trace. VtMB has no StayOnGround, so descending a step briefly leaves the ground and
	// this is what re-detects the floor.
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, 2.0f * ElysiumMove::U + ElysiumMove::DistEpsilon);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumCategorizePosition), /*bTraceComplex*/ false, PawnOwner);
	Params.AddIgnoredActor(PawnOwner);
	// What the floor is MADE OF, asked of the same query that asks whether there is one — which is
	// where retail asks it: `CGameMovement::CategorizePosition` caches the ground trace's
	// `surfacedata_t` on the mover, and `UpdateStepSound 0x1011e940` switches the dry step volume on
	// its `gamematerial` letter (`switch((char)mover[0x29])`). Free on a query that was running
	// anyway, and it never changes what the sweep decides.
	Params.bReturnPhysicalMaterial = true;
	const bool bHit = UpdatedPrimitive && GetWorld()->SweepSingleByChannel(Hit, Start, End,
		UpdatedComponent->GetComponentQuat(), UpdatedPrimitive->GetCollisionObjectType(),
		UpdatedPrimitive->GetCollisionShape(), Params);

	bOnGround = bHit && Hit.ImpactNormal.Z >= ElysiumMove::StandableZ;
	// The port's stand-in for `m_pSurfaceData`, published on the sample rather than kept here
	// (`ElysiumGroundSurface`, which owns the "no floor is NAME_None, an unmaterialed floor is
	// `default`" pair and the render-geometry probe this runtime needs because the bake split the
	// collision brush from the drawn one). A body that is not standing on anything carries no
	// surface, which is retail's null `surfacedata_t` and a silent step.
	GroundSurface = bOnGround
		? ElysiumGroundSurface::AtFloorHit(GetWorld(), Hit, PawnOwner) : FName();
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
	// **The planes bumped into since the body last actually moved.** Clipping against each one as it
	// arrives is not enough: in a doorway's interior corner the projection that clears wall A drives
	// straight back into wall B, so bump three re-clips against A and the four bumps are spent
	// without resolving anything. Source keeps the set and asks for a direction that clears all of
	// them at once, falling back to the crease the two share — which is what turns sticking on a jamb
	// into sliding through it.
	TArray<FVector, TInlineAllocator<ElysiumMove::MaxClipPlanes>> Planes;
	// The velocity this bump sequence started with. Re-baselined whenever the body covers ground,
	// because a plane set only describes the corner the body is currently wedged in.
	FVector Original = Velocity;
	const FVector Primal = Velocity;

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
		if (Hit.Time > 0.0f)
		{
			// Ground was covered, so whatever corner the previous planes described is behind us.
			Planes.Reset();
			Original = Velocity;
		}

		Remaining *= (1.0f - Hit.Time);

		if (Planes.Num() >= ElysiumMove::MaxClipPlanes)
		{
			Velocity = FVector::ZeroVector;
			return;
		}
		Planes.Add(Hit.Normal);

		if (!ElysiumMove::ResolveClipPlanes(Planes, Original, Primal, Velocity))
		{
			// Wedged: the resolver already zeroed the velocity, and a remaining slide would only
			// carry the body into the geometry that wedged it.
			return;
		}

		// What is left of the MOVE takes the same direction the velocity just resolved to, so the
		// next sweep travels along the crease rather than back into the plane that stopped it.
		const float RemainingLength = static_cast<float>(Remaining.Size());
		Remaining = Velocity.GetSafeNormal() * RemainingLength;
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
	CommandedSpeed = Commanded;
	if (!bSubstitutedMove)
	{
		// What an airborne body keeps commanding, because retail's tables stop refreshing for the
		// jump. Written from the grounded solve rather than from `CategorizePosition`, so it is the
		// speed that was actually integrated and not the one the state says should have been.
		//
		// **A substituted command is deliberately excluded.** It is a sequence's authored
		// displacement clamped to `m_flMaxspeed`, not a gait cell, and retail's carried value is the
		// gait ladder's own answer. Letting a swing write it would have a body that left the ground
		// mid-lunge keep commanding the lunge's speed through the whole of `AirMove`.
		LastGroundedWishSpeed = Commanded;
	}
	if (bSubstitutedMove)
	{
		// **`CMoveData+0xD0`.** A refilled command is a displacement the sequence already authored,
		// not a target to accelerate toward: `WalkMove` assigns the wish to the velocity outright and
		// skips `Accelerate` entirely, which is what makes a lunge cover its authored distance rather
		// than a friction-and-acceleration approximation of it. Everything downstream is unchanged —
		// the move is swept, clipped and stepped exactly as any other.
		Velocity = WishDir * Commanded;
	}
	else
	{
		ElysiumMove::ApplyAccelerate(Velocity, WishDir, Commanded, Tuning.Accelerate,
			SurfaceFriction, DeltaTime);
	}
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
	// hits anything. Testing at the destination rather than sweeping from here is what stops a
	// stand-up pushing the body through a ceiling — and, in the air, through the floor.
	//
	// **The offset is two terms, because retail's origin is the feet and ours is the box centre.**
	// `CanUnduck` (`vampire.dll 0x101265d0`) moves the *feet* by ground state alone — nothing on the
	// ground, `-Grow` airborne, where the airborne case is the fixed-centre resize `SetHullHeight`
	// performs. Re-expressing that against a centre origin adds the half-height the hull would gain,
	// which is a fact about the **hull**, not about the ground:
	//
	//     ducked + ground `+Grow` | ducked + air `0` | standing + ground `0` | standing + air `-Grow`
	//
	// Collapsing the two into `bOnGround ? Grow : 0` asks a *standing* body for 45.72 cm of headroom
	// above its own head, which almost nothing has — and this function is a recorded channel
	// (`Debug/ElysiumMoveRun.cpp`), so it answers on every frame rather than only when a stand-up is
	// pending.
	const float Grow = (ElysiumMove::StandHeight - ElysiumMove::DuckHeight) * 0.5f;
	const FVector Centre = UpdatedComponent->GetComponentLocation()
		+ FVector(0.0f, 0.0f, (bDucked ? Grow : 0.0f) - (bOnGround ? 0.0f : Grow));

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

	// **The press edge decides; the release does nothing.** This is the action layer, and it is the
	// only place the crouch is retained: the command says whether the key is down this frame
	// ("a key press never selects an animation asset"), the classifier reads the realized stance
	// off the body sample, and neither of them
	// holds a crouch between frames. `m_nOldButtons` keeps tracking the button's true level, because
	// that is what makes the edge detectable at all — the same latch `CheckJumpButton` uses.
	//
	// **Both edges are keyed on the HULL, not on the retained request** (`CGameMovement::Duck`,
	// `vampire.dll 0x10126fd0`): `press && !FL_DUCKING` starts — or restarts — the lowering ramp, and
	// `press && FL_DUCKING && CanUnduck()` starts the stand-up. A press with no headroom is neither,
	// so it is swallowed rather than queued, and the body leaves the vent still crouched
	// (`docs/vtmb/source_movement.md` → "Ducking"). Keying the unduck on the request going false
	// instead lets a release mid-lowering enter the stand-up with a standing hull, which retail
	// routes back into the duck ramp and cannot reach at all.
	const bool bPressEdge = bDuckDown && !(OldButtons & DuckBit);
	const bool bDuckEdge = bPressEdge && !bDucked;
	const bool bUnduckEdge = bPressEdge && bDucked && CanUnduck();
	if (bDuckEdge)
	{
		bDuckRequested = true;
	}
	else if (bUnduckEdge)
	{
		bDuckRequested = false;
	}
	OldButtons = bDuckDown ? (OldButtons | DuckBit) : (OldButtons & ~DuckBit);

	// The request can only fall on an unduck edge, which needs the hull to be ducked — so while a
	// lowering ramp is in flight it stays true and the ramp runs to completion, which is exactly the
	// routing retail performs on the flags.
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
		// Arming and advancing are separate: the edge arms, and every later frame advances whatever
		// is in flight. A re-press mid-ramp therefore **restarts** the lowering rather than
		// continuing it, which is what retail's unconditional `m_flDucktime = 1000` does.
		if (bDuckEdge)
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
			// Armed by the press edge alone, which is what makes a re-press during a rise restart it.
			if (bUnduckEdge)
			{
				bDucking = true;
				DuckTime = ElysiumMove::GameMovementDuckTime;
			}

			// **Headroom is tested before anything moves, including the eye.** Retail returns from
			// the refusal without touching the view offset; ramping first walks the camera up through
			// the very ceiling the stand-up is being refused for — 28 units above the head on a
			// ducked hull — and leaves it there for as long as the body stays under it.
			if (CanUnduck())
			{
				const float Elapsed = (ElysiumMove::GameMovementDuckTime - DuckTime) * 0.001f;
				if (Elapsed >= ElysiumMove::TimeToUnduck || bInAir)
				{
					FinishUnDuck();
				}
				else if (AElysiumPawn* P = Cast<AElysiumPawn>(PawnOwner))
				{
					P->SetEyeHeight(FMath::Lerp(ElysiumMove::DuckViewZ, ElysiumMove::StandViewZ,
						Elapsed / ElysiumMove::TimeToUnduck));
				}
			}
			else
			{
				// Still under something. Retail re-arms the timer on every blocked frame, so the rise
				// starts from the beginning once the ceiling clears rather than snapping to standing.
				DuckTime = ElysiumMove::GameMovementDuckTime;
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

	// Retail rebinds `frametime` to the user command's OWN timing for the duration of the move:
	// CPlayerMove::RunCommand runs on the command's interval, not on the server frame's.
	// The router builds one command per frame, so this is the identity — it stops being the
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
		// No step ran, so no command was substituted this frame. Left standing, the flag would have a
		// readout reporting a lunge on a body nailed to the floor.
		bSubstitutedMove = false;
		SubstitutedWish = FVector::ZeroVector;
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

	// One step, at exactly the delta the command carries (RE21). VtMB has no tick and no
	// accumulator, so its movement really is frame-rate dependent — `AirAccelerate`'s `addspeed`
	// clamp stops binding above ~117 fps, and the full-step gravity puts the jump apex at
	// `JumpBoost - 100*dt` (`docs/vtmb/source_movement.md` → "Frame timing").
	PlayerMove(DeltaTime);

	PrevCmd = PendingCmd;
	UpdateComponentVelocity();

	// The tick tail. After the move is the only point where the state is settled, so no
	// consumer can read a half-integrated frame; after `UpdateComponentVelocity` so the sample and
	// `GetVelocity()` agree by construction.
	//
	// One path deliberately does not reach here and **holds the previous sample** rather than
	// publishing a settled one: the `ShouldSkipUpdate` / `DeltaTime <= 0` guard above is a paused or
	// dormant frame, and zeroing there would snap a run cycle to idle and back on resume.
	PublishLocomotionSample(/*bSolved*/ true);
}

void UElysiumMovementComponent::PlayerMove(float DeltaTime)
{
	if (bNoclip)
	{
		NoclipMove(DeltaTime);
		return;
	}

	// `CGameMovement::PlayerMove`'s own first act, before anything can move the body: while the
	// player has no ground entity, remember how fast it is going DOWN. This is the whole of
	// `m_flFallVelocity`'s maintenance — the value `CheckFalling` (`0x10125db0`) reads at the far
	// end of the step to choose the landing step's volume — and `bOnGround` here is still the
	// previous move's answer, exactly as retail's `GetGroundEntity()` is at this point.
	if (!bOnGround)
	{
		FallVelocity = static_cast<float>(-Velocity.Z);
	}

	// `SetupMove` runs ahead of the whole step, which is where retail runs it: it rewrites the
	// command every move function below reads, so substituting inside one of them would leave the
	// other two commanding what the player asked for while the sequence drove the third.
	SetupMove(DeltaTime);

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
	if (AnimLock.bRefusesJump)
	{
		// **`vt+0x670`, which is NOT the bit the movement substitution reads.** The movement lock is
		// `vt+0x674` — the same predicate OR `m_IdealActivity == ACT_LAND_HARD` — so during a hard
		// landing retail drives the body from the clip and still permits the jump. Every implemented
		// row is a melee one, and none of them is `ACT_LAND_HARD`, so the two flags agree on every
		// melee frame; they are read apart so the landing and knockback families do not inherit a
		// refusal retail does not have.
		//
		// The press is NOT consumed. Retail's busy bail in `CheckJumpButton` jumps to a plain `ret`
		// and skips the `m_nOldButtons |= IN_JUMP` the water-jump bail runs, so a held jump key fires
		// the instant the lock releases — which is why this returns without touching `OldButtons`.
		//
		// Deliberately only the press edge, below the held-push branch above: the air attack is
		// animation-driven for its whole clip, and refusing the push would cut the jump the attack is
		// being swung from.
		return;
	}

	// The press edge.
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

	// The leniency courses' whole measurement: a press that reached this line became a jump,
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
