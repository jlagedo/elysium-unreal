#include "Debug/ElysiumMoveRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumGymBuilder.h"
#include "Debug/ElysiumMoveCourses.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumGymSpec.h"
#include "ElysiumInputRouter.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerCameraManager.h"
#include "ElysiumPlayerController.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMove, Log, All);

namespace
{
	// The same settle the other harnesses take: the spawn pass is done by then, but collision cooks
	// over the first frames and a body dropped into a half-built scene falls through the floor.
	constexpr int32 MoveSettleFrames = 30;

	// And a second settle, per course, between seating the body and feeding it its first command.
	//
	// `ResetState` clears `bOnGround`, and `Duck` runs **before** `CategorizePosition` — so a course
	// whose first frame holds `+duck` gets the *airborne* duck, which moves the origin up 18 units
	// instead of leaving the feet planted. The body then lands two units high (inside
	// `CategorizePosition`'s own down-trace tolerance, so it never settles further) and walks into a
	// ceiling it would otherwise have fitted under. A handful of input-free frames is all it takes
	// for the body to be grounded before the course says anything.
	constexpr int32 CourseSettleFrames = 10;

	// The gym stands at the world origin: every recorded coordinate then reads small, and a row's
	// `py` divided by the lane pitch is the lane index by inspection.
	const FVector GymOrigin = FVector::ZeroVector;

	FString OutputDir()
	{
		return FPaths::Combine(FElysiumContentPaths::Root(), TEXT("_move"));
	}
}

bool FElysiumMoveRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumMove"));
}

bool FElysiumMoveRun::WantsGym()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MoveGym"));
}

FElysiumMoveRun::FElysiumMoveRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("MoveHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	StepSeconds = 1.0f / static_cast<float>(Hz);

	FParse::Value(FCommandLine::Get(), TEXT("MoveCourse="), CourseFilter);
	bGym = WantsGym();

	UE_LOG(LogElysiumMove, Log, TEXT("headless movement run armed: %s, %d Hz%s."),
		bGym ? TEXT("the generated gym") : TEXT("a sited map"), Hz,
		CourseFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", course '%s'"), *CourseFilter));

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumMoveRun::Tick));
}

FElysiumMoveRun::~FElysiumMoveRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

namespace
{
	// The move producer's frame channels, in column order. Every one resolves in
	// `ElysiumChannels::Defs()` or the recorder refuses to open — which is what stops a value
	// reaching disk with nothing that knows how to compare it.
	const TCHAR* const GMoveChannels[] =
	{
		TEXT("frame"), TEXT("seq"), TEXT("dt"),
		TEXT("px"), TEXT("py"), TEXT("pz"),
		TEXT("vx"), TEXT("vy"), TEXT("vz"), TEXT("speed2d"),
		TEXT("onground"), TEXT("ducked"), TEXT("ducking"), TEXT("canunduck"),
		TEXT("water"), TEXT("surffric"),
		TEXT("move_yaw_wish"), TEXT("move_yaw_vel"),
		TEXT("cam_boom"), TEXT("cam_damp"), TEXT("cam_pitch"), TEXT("cam_yaw"),
		TEXT("cam_clip"), TEXT("cam_third"),
		TEXT("mcam_boom"), TEXT("mcam_damp"), TEXT("mcam_pitch"), TEXT("mcam_yaw"),
		TEXT("mcam_clip"),
	};

	// One place that resolves the harness's actors, so a null anywhere reads the same.
	struct FBodyRefs
	{
		AElysiumPlayerController* PC = nullptr;
		AElysiumPawn* Pawn = nullptr;
		UElysiumMovementComponent* Move = nullptr;
		UElysiumInputRouter* Router = nullptr;
		// The camera half. Both are optional: a run whose view target is not a player body still
		// records movement, and the camera columns hold their last settled values.
		UElysiumCameraComponent* Camera = nullptr;
		AElysiumPlayerCameraManager* CameraManager = nullptr;
		explicit operator bool() const { return PC && Pawn && Move && Router; }
	};

	FBodyRefs ResolveBody(UElysiumMapSubsystem* Sub)
	{
		FBodyRefs R;
		const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		R.PC = World ? Cast<AElysiumPlayerController>(World->GetFirstPlayerController()) : nullptr;
		R.Pawn = R.PC ? Cast<AElysiumPawn>(R.PC->GetPawn()) : nullptr;
		R.Move = R.Pawn
			? Cast<UElysiumMovementComponent>(R.Pawn->GetMovementComponent()) : nullptr;
		R.Router = R.PC ? R.PC->GetInputRouter() : nullptr;
		R.Camera = R.Pawn ? R.Pawn->GetCameraComponent() : nullptr;
		R.CameraManager = R.PC
			? Cast<AElysiumPlayerCameraManager>(R.PC->PlayerCameraManager) : nullptr;
		return R;
	}

	// The courses this run drives, rebuilt each time so a gym course reads the live tuning.
	TArray<ElysiumMoveCourses::FCourse> CoursesFor(bool bGym, const FElysiumMoveTuning& T)
	{
		if (!bGym)
		{
			return TArray<ElysiumMoveCourses::FCourse>(ElysiumMoveCourses::Sited());
		}
		return ElysiumMoveCourses::Gym(ElysiumGym::Build(T));
	}
}

bool FElysiumMoveRun::BuildGym()
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	const FBodyRefs Body = ResolveBody(Sub);
	if (!World || !Body)
	{
		return false;
	}

	const ElysiumGym::FSpec Spec = ElysiumGym::Build(Body.Move->GetTuning());
	if (!ElysiumGym::Spawn(World, Spec, GymOrigin))
	{
		UE_LOG(LogElysiumMove, Error, TEXT("could not stand the gym up"));
		return false;
	}

	// The stage world freezes its body on arrival because a stage has no floor to stand on. The
	// harness is what just gave it one, so releasing the freeze is the harness's to do — the map
	// layer's rule is still right for every other caller.
	Body.Pawn->SetMovementFrozen(false);
	return true;
}

bool FElysiumMoveRun::BeginCourse(int32 Index)
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FBodyRefs Body = ResolveBody(Sub);
	if (!Body)
	{
		return false;
	}

	const TArray<ElysiumMoveCourses::FCourse> Courses = CoursesFor(bGym, Body.Move->GetTuning());
	if (!Courses.IsValidIndex(Index))
	{
		return false;
	}
	const ElysiumMoveCourses::FCourse& Course = Courses[Index];

	// Where the body starts. A sited course carries its own point; a gym course reads its lane's,
	// because the geometry is derived from tuning that is read live and a copied coordinate would
	// be a coordinate that can go stale.
	FVector Feet = Course.StartFeet;
	float Yaw = Course.StartYaw;
	if (Course.Host == ElysiumMoveCourses::EHost::GymStage)
	{
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(Body.Move->GetTuning());
		const ElysiumGym::FLane* Lane = Spec.FindLane(Course.GymLane);
		if (!Lane)
		{
			UE_LOG(LogElysiumMove, Warning, TEXT("course '%s' names no gym lane"),
				*Course.Name.ToString());
			return false;
		}
		Feet = GymOrigin + Lane->FeetOrigin;
		Yaw = Lane->Yaw;
	}

	// Seat the body and aim it. The start is absolute and the mover's state is cleared, so a course
	// inherits neither the position nor the *motion* of the one before it — without the reset the
	// duck course opened at 203 u/s carried over from the strafe course's last airborne frame.
	// `ResetState` also drops the previous course's jump overrides, which is why they are applied
	// after it and not before.
	Body.Move->ResetState();
	Body.Pawn->SetActorLocation(
		ElysiumGym::SeatOrigin(Feet, Body.Pawn->GetBodyHalfHeight()),
		/*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	Body.PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));

	if (Body.Camera)
	{
		// The damper re-seeds per course for the same reason the mover's state is cleared: the body
		// is teleported to the next start, and without this the opening frames record the spring
		// flying in from wherever the previous course left it. Third person itself is armed once for
		// the whole run, in `Tick`.
		Body.Camera->RequestReseed();
	}

	for (const TPair<const TCHAR*, float>& Override : Course.JumpOverrides)
	{
		if (!Body.Move->SetJumpRuleOverride(Override.Key, Override.Value))
		{
			UE_LOG(LogElysiumMove, Warning, TEXT("course '%s': '%s' is not a jump rule"),
				*Course.Name.ToString(), Override.Key);
		}
	}

	FString Error;
	if (!Recorder.Open(GMoveChannels, Error))
	{
		UE_LOG(LogElysiumMove, Error, TEXT("%s"), *Error);
		return false;
	}

	StartFeet = Feet;
	StartForward = FRotator(0.0f, Yaw, 0.0f).Vector();
	PeakSpeed2D = 0.0;
	PeakApexUnits = 0.0;
	AdvanceMax = 0.0;
	TopStand = 0.0;
	ReachMax = 0.0;
	bEndedDucked = false;
	// Apex is measured per airborne span, from the takeoff height (see Sample).
	TakeoffZ = 0.0;
	LastGroundedZ = 0.0;
	bHaveDatum = false;
	GroundTransitions = 0;
	bWasOnGround = true;
	CamThirdMax = 0.0;

	// The stream is armed after the settle, not here — see `CourseSettleFrames`.
	PendingStream = ElysiumMoveCourses::Expand(Course, StepSeconds);
	CourseSettleRemaining = CourseSettleFrames;

	UE_LOG(LogElysiumMove, Log, TEXT("course '%s' begins at %s yaw %.1f"),
		*Course.Name.ToString(), *Feet.ToCompactString(), Yaw);
	return true;
}

void FElysiumMoveRun::Sample()
{
	const FBodyRefs Body = ResolveBody(Subsystem.Get());
	if (!Body)
	{
		return;
	}

	const FVector P = Body.Pawn->GetActorLocation();
	const FVector V = Body.Move->Velocity;
	const double Speed2D = FVector2D(V.X, V.Y).Size();
	const bool bGround = Body.Move->IsOnGround();

	// Positions and velocities are emitted in **Source units**, not cm, so a row reads directly
	// against `docs/vtmb/source_movement.md`'s numbers (and against a retail demo dump, if one is
	// ever taken).
	const double Inv = 1.0 / ElysiumMove::U;

	Recorder.BeginFrame();
	Recorder.Set(TEXT("frame"), Recorder.FrameCount());
	Recorder.Set(TEXT("seq"), static_cast<int32>(Body.Router->CurrentCmd().Seq));
	Recorder.Set(TEXT("dt"), StepSeconds);
	Recorder.Set(TEXT("px"), P.X * Inv);
	Recorder.Set(TEXT("py"), P.Y * Inv);
	Recorder.Set(TEXT("pz"), P.Z * Inv);
	Recorder.Set(TEXT("vx"), V.X * Inv);
	Recorder.Set(TEXT("vy"), V.Y * Inv);
	Recorder.Set(TEXT("vz"), V.Z * Inv);
	Recorder.Set(TEXT("speed2d"), Speed2D * Inv);
	Recorder.Set(TEXT("onground"), bGround);
	Recorder.Set(TEXT("ducked"), Body.Move->IsDucked());
	Recorder.Set(TEXT("ducking"), Body.Move->IsDucking());
	Recorder.Set(TEXT("canunduck"), Body.Move->CanUnduck());
	Recorder.Set(TEXT("water"), static_cast<int32>(Body.Move->GetWaterLevel()));
	Recorder.Set(TEXT("surffric"), Body.Move->GetSurfaceFriction());

	// The body sample (CCC1), read rather than re-derived: the mover published it at its tick tail,
	// so these are the yaws of the frame that was actually integrated.
	const FElysiumLocomotionSample& Locomotion = Body.Move->GetLocomotionSample();
	Recorder.Set(TEXT("move_yaw_wish"), Locomotion.MoveYawWish);
	Recorder.Set(TEXT("move_yaw_vel"), Locomotion.MoveYawVelocity);

	// The camera (CCC2), read off the manager's settled sample. This runs from the core ticker,
	// which the engine ticks *after* the world — so the sample published inside this frame's view
	// update is the one that was rendered, and no lag compensation is needed. The frame stamp is
	// still checked, because a frame whose view never updated (a paused world, an unpossessed body)
	// would otherwise be indistinguishable from a genuinely still camera.
	const FElysiumCameraSample Cam = Body.CameraManager
		? Body.CameraManager->GetCameraSample() : FElysiumCameraSample();
	const bool bCamFresh = Cam.Frame == GFrameCounter;
	Recorder.Set(TEXT("cam_boom"), (bCamFresh ? Cam.BoomLength : 0.0f) * Inv);
	Recorder.Set(TEXT("cam_damp"), (bCamFresh ? Cam.DamperDistance : 0.0f) * Inv);
	Recorder.Set(TEXT("cam_pitch"), bCamFresh ? Cam.BoomPitch : 0.0f);
	Recorder.Set(TEXT("cam_yaw"), bCamFresh ? Cam.BoomYaw : 0.0f);
	Recorder.Set(TEXT("cam_clip"), bCamFresh && Cam.bClipped);
	Recorder.Set(TEXT("cam_third"), bCamFresh ? Cam.ThirdWeight : 0.0f);
	Recorder.Set(TEXT("mcam_boom"), (bCamFresh ? Cam.ModernBoomLength : 0.0f) * Inv);
	Recorder.Set(TEXT("mcam_damp"), (bCamFresh ? Cam.ModernDamperDistance : 0.0f) * Inv);
	Recorder.Set(TEXT("mcam_pitch"), bCamFresh ? Cam.ModernBoomPitch : 0.0f);
	Recorder.Set(TEXT("mcam_yaw"), bCamFresh ? Cam.ModernBoomYaw : 0.0f);
	Recorder.Set(TEXT("mcam_clip"), bCamFresh && Cam.bModernClipped);

	FString Error;
	if (!Recorder.EndFrame(Error))
	{
		UE_LOG(LogElysiumMove, Error, TEXT("%s"), *Error);
		return;
	}

	PeakSpeed2D = FMath::Max(PeakSpeed2D, Speed2D * Inv);
	if (bCamFresh)
	{
		CamThirdMax = FMath::Max(CamThirdMax, static_cast<double>(Cam.ThirdWeight));
	}

	// --- The run channels, which are what a committed baseline actually compares ---------------
	// Each is written to **saturate**: how far the body got before something stopped it, and how
	// high it stood or reached. A body either climbs a riser or is stopped by it, and either answer
	// is the same at any gait — which is what no per-frame trace can be, because *when* a body
	// arrives moves with its speed even when *whether* it arrives does not.
	const double FeetZ = P.Z - Body.Pawn->GetBodyHalfHeight();
	const double RiseUnits = (FeetZ - StartFeet.Z) * Inv;
	ReachMax = FMath::Max(ReachMax, RiseUnits);
	if (bGround)
	{
		TopStand = FMath::Max(TopStand, RiseUnits);
	}
	const double Advance = FVector::DotProduct(P - StartFeet, StartForward) * Inv;
	AdvanceMax = FMath::Max(AdvanceMax, Advance);
	bEndedDucked = Body.Move->IsDucked();

	// The apex is measured from the height the body LEFT THE GROUND at, per airborne span — not
	// from one datum for the whole course. Against a single datum a running jump taken from higher
	// ground reads as a higher jump, which makes the number a property of the level rather than of
	// the movement, and it stops being comparable across frame rates (the body is somewhere
	// slightly different when it jumps).
	// The datum is the LAST GROUNDED sample, not the first airborne one. Sampling happens after the
	// move, so by the first frame that reports airborne the body has already risen a full `v0*dt` —
	// taking that as the takeoff height understates the apex by an amount that shrinks with the
	// frame time, which reads exactly like frame-rate dependence and is really just the ruler.
	if (bGround != bWasOnGround)
	{
		++GroundTransitions;
		if (!bGround)
		{
			TakeoffZ = LastGroundedZ;   // ground -> air
			bHaveDatum = true;
		}
		bWasOnGround = bGround;
	}
	if (bGround)
	{
		LastGroundedZ = P.Z;
	}
	else if (bHaveDatum)
	{
		PeakApexUnits = FMath::Max(PeakApexUnits, (P.Z - TakeoffZ) * Inv);
	}
}

void FElysiumMoveRun::FinishCourse()
{
	const FBodyRefs Body = ResolveBody(Subsystem.Get());
	if (!Body || Recorder.FrameCount() == 0)
	{
		return;
	}
	const TArray<ElysiumMoveCourses::FCourse> Courses = CoursesFor(bGym, Body.Move->GetTuning());
	if (!Courses.IsValidIndex(CourseIndex))
	{
		return;
	}
	const ElysiumMoveCourses::FCourse& Course = Courses[CourseIndex];
	const FString Host = bGym ? TEXT("gym") : FString(Course.Map ? Course.Map : TEXT("map"));

	Recorder.SetMeta(TEXT("harness"), TEXT("move"));
	Recorder.SetMeta(TEXT("host"), Host);
	Recorder.SetMeta(TEXT("course"), Course.Name.ToString());
	// Whether this recording may be promoted to a committed baseline yet. A course whose answer
	// moves with the speed authority is validated but never compared until `CCC7` settles it.
	Recorder.SetMeta(TEXT("baseline"), Course.bDeferBaseline ? TEXT("deferred") : TEXT("committed"));
	Recorder.SetMetaNumber(TEXT("hz"), Hz);
	Recorder.SetMetaNumber(TEXT("fixedStep"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.move.FixedStep"))
			? IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.move.FixedStep"))->GetFloat()
			: 0.0f);

	// What the geometry and the motion were derived from. A bracket that turns red is a bracket
	// whose constant moved, and this is what says which one without anyone having to guess.
	{
		const FElysiumMoveTuning& T = Body.Move->GetTuning();
		const float Inv = 1.0f / ElysiumMove::U;
		Recorder.SetConstant(TEXT("StepSize"), T.StepSize * Inv);
		Recorder.SetConstant(TEXT("JumpBoost"), T.JumpBoost);          // already Source units
		Recorder.SetConstant(TEXT("JumpBoostScale"), ElysiumMove::JumpBoostScale);
		Recorder.SetConstant(TEXT("StandableZ"), ElysiumMove::StandableZ);
		Recorder.SetConstant(TEXT("StandHeight"), ElysiumMove::StandHeight * Inv);
		Recorder.SetConstant(TEXT("DuckHeight"), ElysiumMove::DuckHeight * Inv);
		Recorder.SetConstant(TEXT("HullHalfWidth"), ElysiumMove::HullHalfWidth * Inv);
		Recorder.SetConstant(TEXT("BaseJumpVelocity"), T.BaseJumpVelocity * Inv);
		Recorder.SetConstant(TEXT("JumpHoldTime"), T.JumpHoldSeconds);
		Recorder.SetConstant(TEXT("JumpGravityMultiplier"), T.JumpGravityMultiplier);
		Recorder.SetConstant(TEXT("Gravity"), T.Gravity * Inv);
		Recorder.SetConstant(TEXT("Friction"), T.Friction);
		Recorder.SetConstant(TEXT("StopSpeed"), T.StopSpeed * Inv);
		Recorder.SetConstant(TEXT("Accelerate"), T.Accelerate);
		Recorder.SetConstant(TEXT("WalkSpeed"), ElysiumMove::WalkSpeed * Inv);
		Recorder.SetConstant(TEXT("RunSpeed"), ElysiumMove::RunSpeed * Inv);
	}
	for (const TPair<const TCHAR*, float>& Override : Course.JumpOverrides)
	{
		Recorder.SetOverride(Override.Key, Override.Value);
	}

	Recorder.SetRun(TEXT("frames"), Recorder.FrameCount());
	Recorder.SetRun(TEXT("advance_max"), AdvanceMax);
	Recorder.SetRun(TEXT("top_stand"), TopStand);
	Recorder.SetRun(TEXT("reach_max"), ReachMax);
	Recorder.SetRun(TEXT("peak_rise"), PeakApexUnits);
	Recorder.SetRun(TEXT("ground_transitions"), GroundTransitions);
	Recorder.SetRun(TEXT("ended_ducked"), bEndedDucked);
	Recorder.SetRun(TEXT("peak_speed2d"), PeakSpeed2D);
	Recorder.SetRun(TEXT("cam_third_max"), CamThirdMax);

	const FString Stem = FString::Printf(TEXT("%s.%s.%dhz"), *Host, *Course.Name.ToString(), Hz);
	FString Error;
	if (!Recorder.Write(OutputDir(), Stem, Error))
	{
		UE_LOG(LogElysiumMove, Error, TEXT("%s"), *Error);
		return;
	}

	UE_LOG(LogElysiumMove, Log,
		TEXT("course '%s': %d frames, advance %.1f u, stood %.2f u, reached %.2f u, ")
		TEXT("peak 2D %.1f u/s, %d ground transitions -> %s"),
		*Course.Name.ToString(), Recorder.FrameCount(), AdvanceMax, TopStand, ReachMax,
		PeakSpeed2D, GroundTransitions, *Stem);
}

bool FElysiumMoveRun::Tick(float /*DeltaSeconds*/)
{
	if (bDone)
	{
		return false;
	}

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;
	const FBodyRefs Body = ResolveBody(Sub);

	if (!(Body.PC && MapActor && MapActor->IsSpawnDone()))
	{
		return true;
	}
	if (++FrameInPhase < MoveSettleFrames)
	{
		return true;
	}
	if (!Body)
	{
		UE_LOG(LogElysiumMove, Warning, TEXT("no body to drive; exiting."));
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}

	// The run records the camera, so the camera has to be doing something: in first person the boom
	// is the zero vector and every channel would be a plausible-looking zero. Third person is armed
	// once for the whole run — the latch persists across courses — and the run then **waits on the
	// weight rather than on a frame count**. The ramp is a fixed 0.5 s at 2.0/s, so a fixed number
	// of settle frames would land at 1.0 at 60 Hz and at 0.5 at 120, and the first course would
	// record the tail of a transition at one rate and a settled boom at the other.
	if (Body.Camera)
	{
		if (!Body.Camera->IsThirdPerson())
		{
			Body.Camera->SetThirdPerson(true);
			return true;
		}
		if (Body.Camera->ThirdPersonWeight() < 1.0f)
		{
			return true;
		}
	}

	if (bGym && !bGymBuilt)
	{
		bGymBuilt = true;
		if (!BuildGym())
		{
			bDone = true;
			FPlatformMisc::RequestExit(false);
			return false;
		}
		// Give the freshly registered bodies a frame to enter the scene before seating anything on
		// them.
		return true;
	}

	// Let a freshly seated body find the floor before it is told to do anything. The datum for
	// every run channel is taken here, once the body has stopped moving, so a course measures from
	// where it actually stands rather than from where it was placed.
	if (CourseSettleRemaining > 0)
	{
		if (--CourseSettleRemaining == 0)
		{
			StartFeet.Z = Body.Pawn->GetActorLocation().Z - Body.Pawn->GetBodyHalfHeight();
			Body.Router->StartReplay(MoveTemp(PendingStream));
		}
		return true;
	}

	// A course is in flight for as long as the router is still feeding its stream.
	if (CourseIndex >= 0 && Body.Router->IsReplaying())
	{
		Sample();
		return true;
	}

	// The stream ran out (or nothing has started): close the last course and open the next one that
	// passes the filter.
	if (CourseIndex >= 0)
	{
		FinishCourse();
	}

	const TArray<ElysiumMoveCourses::FCourse> Courses = CoursesFor(bGym, Body.Move->GetTuning());
	int32 Next = CourseIndex + 1;
	while (Courses.IsValidIndex(Next) &&
		!CourseFilter.IsEmpty() && CourseFilter != Courses[Next].Name.ToString())
	{
		++Next;
	}

	if (!Courses.IsValidIndex(Next))
	{
		UE_LOG(LogElysiumMove, Log, TEXT("movement run complete; exiting."));
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}

	CourseIndex = Next;
	if (!BeginCourse(CourseIndex))
	{
		UE_LOG(LogElysiumMove, Warning, TEXT("could not seat the body for course '%s'; exiting."),
			*Courses[CourseIndex].Name.ToString());
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
