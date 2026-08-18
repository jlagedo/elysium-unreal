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
		TEXT("move_yaw_wish"), TEXT("move_yaw_vel"), TEXT("move_yaw"),
		TEXT("act_code"), TEXT("act_route"), TEXT("act_outcome"), TEXT("act_asset"),
		TEXT("act_state"),
		TEXT("air_phase"), TEXT("act_gen"), TEXT("act_stride"), TEXT("act_fade"),
		TEXT("cam_boom"), TEXT("cam_damp"), TEXT("cam_pitch"), TEXT("cam_yaw"),
		TEXT("cam_clip"), TEXT("cam_third"),
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
		// The animation half (CCC4), also optional: the map actor is what publishes the player's
		// selection record, and a run with none records the classifier's columns as their defaults.
		AElysiumMapActor* Map = nullptr;
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
		R.Map = Sub ? Sub->GetCurrentMap() : nullptr;
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
	if (!ElysiumGym::Spawn(World, Spec, ElysiumGym::DefaultOrigin()))
	{
		UE_LOG(LogElysiumMove, Error, TEXT("could not stand the gym up"));
		return false;
	}

	// **The body the gym stands** (CCC7). Without one the speed authority has no tables and the whole
	// run measures the constants fallback — which would make the promoted baselines a recording of
	// the thing the rung replaced. It is the shipping `BuildPlayerVisual`, so the attachment, the
	// hull offset, the mover tick prerequisite and the cached stem are the game's own.
	//
	// A body that will not build is not fatal: the run continues on the fallback, which is what a
	// checkout with no character export gets, and the manifest records the empty stem so a recording
	// made without a body cannot be mistaken for one made with it.
	if (Body.Map)
	{
		FString Stem = TEXT("tremere_Male_Armor_0");
		FParse::Value(FCommandLine::Get(), TEXT("MoveBody="), Stem);
		if (Body.Map->BuildPlayerVisual(Stem, TEXT("Neutral"), 0) == nullptr)
		{
			UE_LOG(LogElysiumMove, Warning,
				TEXT("no player body for '%s' — recording the constants fallback instead"), *Stem);
		}
	}

	// The stage world freezes its body on arrival because a stage has no floor to stand on. The
	// harness is what just gave it one, so releasing the freeze is the harness's to do — the map
	// layer's rule is still right for every other caller.
	Body.Pawn->SetMovementFrozen(false);
	return true;
}

bool FElysiumMoveRun::BeginCourse(int32 Index, ECoursePhase InPhase)
{
	Phase = InPhase;
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
		Feet = ElysiumGym::DefaultOrigin() + Lane->FeetOrigin;
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

	// The probe pass never opens the recorder. One `Open` per `Write` is the recorder's own
	// invariant, and a probe that opened would leave a half-course in it whose frame indices the
	// record pass would then continue from.
	if (Phase == ECoursePhase::Record)
	{
		FString Error;
		if (!Recorder.Open(GMoveChannels, Error))
		{
			UE_LOG(LogElysiumMove, Error, TEXT("%s"), *Error);
			return false;
		}
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
	ActResolvedFrames = 0;
	ActFallbackFrames = 0;
	ActCodesSeen = 0;
	AnimBanks.Reset();
	AnimSelections.Reset();

	if (Phase == ECoursePhase::Probe)
	{
		ProbeFrame = 0;
		bProbeWasOnGround = true;
		bProbeSawGroundLoss = false;
		ResolvedEventFrame = INDEX_NONE;
		ProbeEvent = Course.EventJump.IsSet()
			? Course.EventJump->Event : ElysiumMoveCourses::EBodyEvent::GroundLost;
	}

	// The stream is armed after the settle, not here — see `CourseSettleFrames`. The probe pass
	// expands with INDEX_NONE, which is the same call with no press placed — so both passes drive
	// streams of identical length carrying identical intent but for the one frame under test.
	PendingStream = ElysiumMoveCourses::Expand(Course, StepSeconds, ResolvedEventFrame);
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

	// The selection (CCC4), read off the record the post-move pass published rather than re-resolved,
	// for the same reason the sample above is read rather than re-derived: two derivations of one
	// answer are how a recording comes to disagree with what actually ran.
	//
	// In the gym these carry the classifier alone. There is no map, no player entity and no visual
	// there, so the outcome is `NoVocabulary` and the asset is none — which is the right split: the
	// gym brackets the classification `CCC7` will move, and the sited courses carry the resolution.
	static const FElysiumAnimationSelection EmptySelection;
	const FElysiumAnimationSelection& Sel = Body.Map
		? Body.Map->GetPlayerAnimSelection() : EmptySelection;
	// The classifier's own answer is the pre-translation activity, which is what the record keeps as
	// the logical request; the resolved one is only different once a translation row applied.
	const EElysiumAnimActivityCode Code = ElysiumAnimIntent::ActivityCode(Sel.RequestedActivity);
	Recorder.Set(TEXT("act_code"), static_cast<int32>(Code));
	Recorder.Set(TEXT("act_route"), static_cast<int32>(Sel.Route));
	Recorder.Set(TEXT("act_outcome"), static_cast<int32>(Sel.Outcome));
	Recorder.Set(TEXT("act_asset"), static_cast<int32>(Sel.AssetKind));
	// The state the record named, not one this recorder projected: the trace and the pose come off
	// the same field, so a run cannot record a state the graph never entered.
	Recorder.Set(TEXT("act_state"), static_cast<int32>(Sel.GraphState));
	Recorder.Set(TEXT("air_phase"), static_cast<int32>(Sel.AirPhase));
	Recorder.Set(TEXT("act_gen"), static_cast<int32>(Sel.Generation));
	// The pose parameter, off the same record. It comes from the driver rather than from the sample
	// above because the slew is a rate the driver owns; the sample carries its unfiltered input.
	Recorder.Set(TEXT("move_yaw"), Sel.MoveYaw);
	Recorder.Set(TEXT("act_stride"), Sel.GroundSpeedCmPerSecond * Inv);
	Recorder.Set(TEXT("act_fade"), Sel.FadeSeconds);

	ActCodesSeen |= (Code != EElysiumAnimActivityCode::Unknown)
		? (1u << static_cast<uint32>(Code)) : 0u;
	if (Sel.Outcome == EElysiumAnimOutcome::Resolved)
	{
		++ActResolvedFrames;
	}
	else
	{
		++ActFallbackFrames;
	}
	if (!Sel.OwnerStem.IsEmpty())
	{
		// A run course's manifest naming the PC-only bank IS the bank-ownership claim, in text, in the
		// run's own output.
		AnimBanks.Add(Sel.OwnerStem);
		AnimSelections.Add(FString::Printf(TEXT("%s=%s@%s:%s"), *Sel.ResolvedActivity,
			*Sel.SequenceLabel, *Sel.OwnerStem, *Sel.AnimationName));
	}

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
	JumpsTaken = Body.Move->GetJumpsTaken();

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

void FElysiumMoveRun::ProbeSample()
{
	const FBodyRefs Body = ResolveBody(Subsystem.Get());
	if (!Body)
	{
		return;
	}

	// One question per frame, and no recorder: which frame the ground state flipped on. The frame
	// index is the replayed command's, counted the same way `Expand` numbers them, so the answer is
	// directly the index the press is placed against.
	const bool bGround = Body.Move->IsOnGround();
	if (bGround != bProbeWasOnGround)
	{
		if (!bGround)
		{
			bProbeSawGroundLoss = true;
			if (ResolvedEventFrame == INDEX_NONE
				&& ProbeEvent == ElysiumMoveCourses::EBodyEvent::GroundLost)
			{
				ResolvedEventFrame = ProbeFrame;
			}
		}
		// The landing that matters is the one after the body actually left the lip — a course whose
		// settle ended airborne would otherwise resolve against its own first contact.
		else if (bProbeSawGroundLoss && ResolvedEventFrame == INDEX_NONE
			&& ProbeEvent == ElysiumMoveCourses::EBodyEvent::GroundGained)
		{
			ResolvedEventFrame = ProbeFrame;
		}
		bProbeWasOnGround = bGround;
	}
	++ProbeFrame;
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
		// What the body's own fans answered (CCC7). The constants block is metadata and is never
		// compared, so this is free — and it is what stops a recording made on the no-fan fallback
		// being mistaken for one made on the animation.
		const FElysiumGaitSpeeds& Fans = Body.Move->GetGaitSpeeds();
		Recorder.SetConstant(TEXT("GaitWalkForward"), Fans.Walk.Forward() * Inv);
		Recorder.SetConstant(TEXT("GaitRunForward"), Fans.Run.Forward() * Inv);
		Recorder.SetConstant(TEXT("GaitSneakForward"), Fans.Sneak.Forward() * Inv);
	}
	for (const TPair<const TCHAR*, float>& Override : Course.JumpOverrides)
	{
		Recorder.SetOverride(Override.Key, Override.Value);
	}

	// The selection's string identities (CCC4). Sorted and joined so the line is deterministic across
	// two runs of the same course; empty on the gym, which stands a body with no model behind it.
	{
		auto Joined = [](const TSet<FString>& Values)
		{
			TArray<FString> Sorted = Values.Array();
			Sorted.Sort();
			return FString::Join(Sorted, TEXT(" "));
		};
		Recorder.SetMeta(TEXT("anim_stem"), Body.Map ? Body.Map->GetPlayerAnimSelection().Stem
			: FString());
		Recorder.SetMeta(TEXT("anim_banks"), Joined(AnimBanks));
		Recorder.SetMeta(TEXT("anim_selections"), Joined(AnimSelections));
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
	Recorder.SetRun(TEXT("act_resolved"), ActResolvedFrames);
	Recorder.SetRun(TEXT("act_fallbacks"), ActFallbackFrames);
	Recorder.SetRun(TEXT("act_codes"), static_cast<int32>(ActCodesSeen));

	// Only the leniency courses carry these. A course that merely *holds* jump re-fires on landing at
	// a gait-dependent moment, so declaring the count universally would commit a number `CCC7` moves.
	if (Course.EventJump.IsSet())
	{
		Recorder.SetRun(TEXT("jumps_taken"), JumpsTaken);
		Recorder.SetRun(TEXT("event_frame"), ResolvedEventFrame);
		Recorder.SetOverride(TEXT("JumpFrameOffset"), Course.EventJump->FrameOffset);
	}

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
		if (Phase == ECoursePhase::Probe)
		{
			ProbeSample();
		}
		else
		{
			Sample();
		}
		return true;
	}

	const TArray<ElysiumMoveCourses::FCourse> Courses = CoursesFor(bGym, Body.Move->GetTuning());

	// A probe pass ends by re-running the same course for record, now that the event frame is known.
	// The body is re-seated and its state reset by `BeginCourse` exactly as for any other course, so
	// the record pass starts from the same place the probe did.
	if (CourseIndex >= 0 && Phase == ECoursePhase::Probe)
	{
		const FName Name = Courses.IsValidIndex(CourseIndex)
			? Courses[CourseIndex].Name : FName(TEXT("?"));
		if (ResolvedEventFrame == INDEX_NONE)
		{
			// Recorded as -1 rather than skipped: the sentinel rungs then disagree with their
			// committed baselines and the run fails, which is the geometry reporting itself broken.
			UE_LOG(LogElysiumMove, Error,
				TEXT("course '%s': the probe pass never saw the ground edge it times against; ")
				TEXT("recording with no press."), *Name.ToString());
		}
		else
		{
			UE_LOG(LogElysiumMove, Log, TEXT("course '%s': ground edge at probe frame %d"),
				*Name.ToString(), ResolvedEventFrame);
		}
		if (!BeginCourse(CourseIndex, ECoursePhase::Record))
		{
			UE_LOG(LogElysiumMove, Warning, TEXT("could not seat the body to record '%s'; exiting."),
				*Name.ToString());
			bDone = true;
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}

	// The stream ran out (or nothing has started): close the last course and open the next one that
	// passes the filter.
	if (CourseIndex >= 0)
	{
		FinishCourse();
	}

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
	// A course that times a press against a body event has to find the event first; everything else
	// goes straight to record, so every existing course's execution is unchanged.
	const ECoursePhase NextPhase = Courses[Next].EventJump.IsSet()
		? ECoursePhase::Probe : ECoursePhase::Record;
	if (!BeginCourse(CourseIndex, NextPhase))
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
