#include "Debug/ElysiumCastRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaBuilder.h"
#include "Debug/ElysiumArenaCast.h"
#include "Debug/ElysiumCastCourses.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGymSpec.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumVariant.h"
#include "Visual/ElysiumNpcBody.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCast, Log, All);

namespace
{
	// The same settle the other harnesses take before they touch anything.
	constexpr int32 CastSettleFrames = 30;

	// Between standing a character up and giving it an order. The entity's Spawn builds the visual
	// and the motor, CharacterMovement drops the capsule onto the plate, and the driver resolves its
	// first selection — a body ordered to travel before any of that reports a refusal instead.
	constexpr int32 CastSpawnSettleFrames = 45;

	// How long Recast is given to finish building over the arena. The build is asynchronous and a
	// character let in before it answers cannot path at all, so waiting forever would record a course
	// of a body standing still; this fails loudly instead.
	constexpr int32 CastNavigationWaitFrames = 600;

	// What a course names its own entities. A generated prefix rather than a bare name so a course's
	// props cannot collide with anything a stage world already carries.
	const TCHAR* const CastWalkerName = TEXT("cast_walker");
	const TCHAR* const CastRoutePrefix = TEXT("cast_route_");
	const TCHAR* const CastMarkName = TEXT("cast_mark");

	FString CastOutputDir()
	{
		return FPaths::Combine(FElysiumContentPaths::Root(), TEXT("_cast"));
	}
}

bool FElysiumCastRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumCast"));
}

TArray<const TCHAR*> FElysiumCastRun::DeclaredChannels()
{
	const TArrayView<const TCHAR* const> Shared = ElysiumLocomotionTrace::Channels();
	return TArray<const TCHAR*>(Shared.GetData(), Shared.Num());
}

FElysiumCastRun::FElysiumCastRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("CastHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	StepSeconds = 1.0f / static_cast<float>(Hz);

	FParse::Value(FCommandLine::Get(), TEXT("CastCourse="), CourseFilter);
	FParse::Value(FCommandLine::Get(), TEXT("CastBody="), BodyStem);

	UE_LOG(LogElysiumCast, Log, TEXT("headless cast run armed: %d Hz%s%s."), Hz,
		CourseFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", course '%s'"), *CourseFilter),
		BodyStem.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", body '%s'"), *BodyStem));

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumCastRun::Tick));
}

FElysiumCastRun::~FElysiumCastRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	ElysiumArena::Teardown(GetEntityWorld(), ArenaStanding);
}

UWorld* FElysiumCastRun::GetWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	return GI ? GI->GetWorld() : nullptr;
}

FElysiumEntityWorld* FElysiumCastRun::GetEntityWorld() const
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	AElysiumMapActor* Map = Sub ? Sub->GetCurrentMap() : nullptr;
	return Map ? Map->GetEntityWorld() : nullptr;
}

AElysiumNpcBody* FElysiumCastRun::FindBody() const
{
	UWorld* World = GetWorld();
	if (!World || !CastEntity.IsSet())
	{
		return nullptr;
	}
	// By the entity it embodies rather than by spawn order: the map actor owns every body in the
	// world, and a course that read the wrong one would record a stranger.
	for (TActorIterator<AElysiumNpcBody> It(World); It; ++It)
	{
		if (It->GetOwningEntity() == CastEntity)
		{
			return *It;
		}
	}
	return nullptr;
}

void FElysiumCastRun::Fail(const TCHAR* Reason)
{
	UE_LOG(LogElysiumCast, Error, TEXT("%s; exiting."), Reason);
	bDone = true;
	FPlatformMisc::RequestExit(false);
}

bool FElysiumCastRun::StandArena()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	Arena = ElysiumArena::Build();
	FString Error;
	// **No entity world**, deliberately: the anchors are `intersting_place` nodes, and one standing
	// in this room would let an ambient claim take a body off its course. The floor and the Recast
	// graph are all a travelling body needs.
	if (!ElysiumArena::Stand(World, /*EntityWorld*/ nullptr, Arena, ElysiumArena::DefaultOrigin(),
		/*bWithMeshes*/ false, ArenaStanding, Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("could not stand the arena up: %s"), *Error);
		return false;
	}
	// The stage world seats its pawn wherever the shell's player start is, which is the middle of the
	// room this just built around it. Move it to the arena's own player mark: a frozen body standing
	// inside the cover block is in the way of every trace a character takes, and it is in shot of
	// nothing that would explain why. It stays frozen — the pawn is not what this run records.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			const IElysiumPlayerBody* Body = Cast<const IElysiumPlayerBody>(Pawn);
			Pawn->SetActorLocation(
				ElysiumGym::SeatOrigin(ElysiumArena::DefaultOrigin() + Arena.PlayerFeet,
					Body ? Body->GetBodyHalfHeight() : 0.0f),
				/*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.0f, Arena.PlayerYaw, 0.0f));
		}
	}

	UE_LOG(LogElysiumCast, Log, TEXT("arena stood: %d solids, navigation building"),
		Arena.Solids.Num());
	return true;
}

bool FElysiumCastRun::BeginCourse(int32 Index)
{
	const TArray<ElysiumCastCourses::FCourse> Courses = ElysiumCastCourses::All();
	if (!Courses.IsValidIndex(Index))
	{
		return false;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[Index];

	FElysiumEntityWorld* Entities = GetEntityWorld();
	if (!Entities)
	{
		return false;
	}

	// The previous course's room, gone. A killed character takes its body, its mind and its motor
	// with it, which is the ordinary terminal path rather than a harness shortcut.
	for (const FElysiumEntityHandle& Handle : CourseProps)
	{
		if (FElysiumEntity* Prop = Entities->Resolve(Handle))
		{
			Prop->Kill();
		}
	}
	CourseProps.Reset();
	if (FElysiumEntity* Previous = Entities->Resolve(CastEntity))
	{
		Previous->Kill();
	}
	CastEntity = FElysiumEntityHandle::Invalid();

	// --- Stand the character up -----------------------------------------------------------------
	// Before the props: a `scripted_sequence` names its NPC by targetname, and a marker standing up
	// ahead of the body it names reports it missing before the course has begun.
	const FVector Origin = ElysiumArena::DefaultOrigin();
	ElysiumArenaCast::FSpawnRequest Request;
	Request.Model = BodyStem;
	Request.TargetName = CastWalkerName;
	Request.Origin = Origin + Course.StartFeet;
	Request.Yaw = Course.StartYaw;
	// Neutral at the authored priority, and no lookaround: this run measures locomotion, and a
	// character that acquires the frozen pawn across the room or turns to inspect a noise is a
	// character running a different course every time.
	Request.PlayerReaction = EElysiumRelationship::Neutral;
	Request.bAllowAlertLookaround = false;

	FString Error;
	CastEntity = ElysiumArenaCast::Spawn(*Entities, Request, Error);
	if (!CastEntity.IsSet())
	{
		UE_LOG(LogElysiumCast, Error, TEXT("course '%s': %s"), *Course.Name.ToString(), *Error);
		return false;
	}

	// --- Dress the room -------------------------------------------------------------------------
	if (Course.Order == ElysiumCastCourses::EOrder::Patrol)
	{
		for (int32 Point = 0; Point < Course.Route.Num(); ++Point)
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("info_node_patrol_point");
			Def.TargetName = FString::Printf(TEXT("%s%d"), CastRoutePrefix, Point + 1);
			Def.Origin = Origin + Course.Route[Point];
			CourseProps.Add(Entities->SpawnRuntimeEntity(MoveTemp(Def)));
		}
	}
	else if (Course.Order == ElysiumCastCourses::EOrder::Scripted && !Course.Route.IsEmpty())
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("scripted_sequence");
		Def.TargetName = CastMarkName;
		Def.Origin = Origin + Course.Route[0];
		Def.Keys.Add(TEXT("angles"), FString::Printf(TEXT("0 %.1f 0"), Course.StartYaw));
		Def.Keys.Add(TEXT("m_iszEntity"), CastWalkerName);
		Def.Keys.Add(TEXT("m_fMoveTo"), FString::FromInt(Course.MoveTo));
		CourseProps.Add(Entities->SpawnRuntimeEntity(MoveTemp(Def)));
	}
	for (const FElysiumEntityHandle& Handle : CourseProps)
	{
		if (!Handle.IsSet())
		{
			UE_LOG(LogElysiumCast, Error, TEXT("course '%s': a course entity would not spawn"),
				*Course.Name.ToString());
			return false;
		}
	}

	const TArray<const TCHAR*> Columns = DeclaredChannels();
	if (!Recorder.Open(Columns, Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("%s"), *Error);
		return false;
	}

	Totals.Reset();
	StartFeet = Origin + Course.StartFeet;
	StartForward = FRotator(0.0f, Course.StartYaw, 0.0f).Vector();
	AdvanceMax = 0.0;
	SpawnSettleRemaining = CastSpawnSettleFrames;
	RecordRemaining = FMath::Max(1, FMath::RoundToInt(Course.RecordSeconds / StepSeconds));
	bWarnedMissingBody = false;

	UE_LOG(LogElysiumCast, Log, TEXT("course '%s' begins at %s yaw %.1f, %d frames"),
		*Course.Name.ToString(), *Request.Origin.ToCompactString(), Course.StartYaw, RecordRemaining);
	return true;
}

void FElysiumCastRun::ArmCourse()
{
	const TArray<ElysiumCastCourses::FCourse> Courses = ElysiumCastCourses::All();
	FElysiumEntityWorld* Entities = GetEntityWorld();
	if (!Courses.IsValidIndex(CourseIndex) || !Entities)
	{
		return;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[CourseIndex];

	// Through the entity's own input table, which is the door a level script uses. Nothing here
	// touches the motor: what is being recorded is the whole chain from the order to the pose.
	switch (Course.Order)
	{
	case ElysiumCastCourses::EOrder::Patrol:
	{
		FString Route;
		for (int32 Point = 0; Point < Course.Route.Num(); ++Point)
		{
			Route += FString::Printf(TEXT("%s%s%d"), Route.IsEmpty() ? TEXT("") : TEXT(" "),
				CastRoutePrefix, Point + 1);
		}
		Entities->AcceptInput(CastEntity, FName(TEXT("FollowPatrolPath")),
			FElysiumVariant::String(Route), CastEntity, CastEntity);
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': FollowPatrolPath(\"%s\")"),
			*Course.Name.ToString(), *Route);
		break;
	}
	case ElysiumCastCourses::EOrder::Scripted:
	{
		const FElysiumEntityHandle Mark = CourseProps.IsEmpty()
			? FElysiumEntityHandle::Invalid() : CourseProps[0];
		if (!Mark.IsSet())
		{
			UE_LOG(LogElysiumCast, Error, TEXT("course '%s': no marker to begin"),
				*Course.Name.ToString());
			break;
		}
		Entities->AcceptInput(Mark, FName(TEXT("BeginSequence")), FElysiumVariant::Void(),
			CastEntity, CastEntity);
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': BeginSequence (m_fMoveTo %d)"),
			*Course.Name.ToString(), Course.MoveTo);
		break;
	}
	default:
		// Stand: the body is the whole course. Saying so keeps the log honest about a run with no
		// order in it rather than leaving a silent gap where every other course logs one.
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': no order — the body stands"),
			*Course.Name.ToString());
		break;
	}
}

void FElysiumCastRun::Sample()
{
	const AElysiumNpcBody* Body = FindBody();
	if (!Body)
	{
		// Nothing is written rather than a plausible zero: a frame with no body is a frame with no
		// measurement, and the course's short frame count is what will say so. Once per course — the
		// body does not come back, and a row of this per frame is noise over the reason.
		if (!bWarnedMissingBody)
		{
			bWarnedMissingBody = true;
			UE_LOG(LogElysiumCast, Warning,
				TEXT("the character lost its engine body mid-course; the rest of it is unrecorded"));
		}
		return;
	}

	const FVector P = Body->GetActorLocation();
	const FVector V = Body->GetVelocity();
	const FElysiumLocomotionSample Locomotion = Body->SampleLocomotion();
	const FElysiumAnimationSelection& Selection = Body->GetAnimSelection();

	Recorder.BeginFrame();
	// The same writer the player's harness calls, over the same two published records.
	ElysiumLocomotionTrace::Frame(Recorder, StepSeconds, P, V, Locomotion, Selection);
	Totals.Observe(Locomotion, Selection);

	FString Error;
	if (!Recorder.EndFrame(Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("%s"), *Error);
		return;
	}

	const double Advance = FVector::DotProduct(P - StartFeet, StartForward) / ElysiumMove::U;
	AdvanceMax = FMath::Max(AdvanceMax, Advance);
}

void FElysiumCastRun::FinishCourse()
{
	const TArray<ElysiumCastCourses::FCourse> Courses = ElysiumCastCourses::All();
	if (!Courses.IsValidIndex(CourseIndex) || Recorder.FrameCount() == 0)
	{
		return;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[CourseIndex];

	Recorder.SetMeta(TEXT("harness"), TEXT("cast"));
	Recorder.SetMeta(TEXT("host"), TEXT("arena"));
	Recorder.SetMeta(TEXT("course"), Course.Name.ToString());
	// Nothing here is deferred: what a cast course measures is the resolver and the travel order,
	// and both are what this rung settled.
	Recorder.SetMeta(TEXT("baseline"), TEXT("committed"));
	Recorder.SetMetaNumber(TEXT("hz"), Hz);
	// Which body stood the course. A recording made on another one is a different recording, and the
	// manifest is where that has to be readable.
	Recorder.SetMeta(TEXT("cast_body"), BodyStem);

	// The body's own authored fans (CCC7), the same three the movement harness records: a course run
	// on the no-fan fallback must not be mistaken for one run on the animation.
	if (const AElysiumNpcBody* Body = FindBody())
	{
		const float Inv = 1.0f / ElysiumMove::U;
		Recorder.SetConstant(TEXT("GaitWalkForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Walk) * Inv);
		Recorder.SetConstant(TEXT("GaitRunForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Run) * Inv);
		Recorder.SetConstant(TEXT("GaitSneakForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Sneak) * Inv);
	}

	Totals.Write(Recorder);
	Recorder.SetRun(TEXT("frames"), Recorder.FrameCount());
	Recorder.SetRun(TEXT("advance_max"), AdvanceMax);

	const FString Stem = FString::Printf(TEXT("arena.%s.%dhz"), *Course.Name.ToString(), Hz);
	FString Error;
	if (!Recorder.Write(CastOutputDir(), Stem, Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("%s"), *Error);
		return;
	}

	UE_LOG(LogElysiumCast, Log,
		TEXT("course '%s': %d frames, advanced %.1f u, peak 2D %.1f u/s, %d resolved / %d fallback ")
		TEXT("-> %s"),
		*Course.Name.ToString(), Recorder.FrameCount(), AdvanceMax, Totals.PeakSpeed2D,
		Totals.ResolvedFrames, Totals.FallbackFrames, *Stem);
}

void FElysiumCastRun::NextCourse()
{
	const TArray<ElysiumCastCourses::FCourse> Courses = ElysiumCastCourses::All();
	int32 Next = CourseIndex + 1;
	while (Courses.IsValidIndex(Next)
		&& !CourseFilter.IsEmpty() && CourseFilter != Courses[Next].Name.ToString())
	{
		++Next;
	}
	if (!Courses.IsValidIndex(Next))
	{
		UE_LOG(LogElysiumCast, Log, TEXT("cast run complete; exiting."));
		bDone = true;
		FPlatformMisc::RequestExit(false);
		return;
	}
	CourseIndex = Next;
	if (!BeginCourse(CourseIndex))
	{
		Fail(TEXT("could not begin a cast course"));
	}
}

bool FElysiumCastRun::Tick(float /*DeltaSeconds*/)
{
	if (bDone)
	{
		return false;
	}

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	AElysiumMapActor* Map = Sub ? Sub->GetCurrentMap() : nullptr;
	if (!Map || !Map->IsSpawnDone())
	{
		return true;
	}
	if (++FrameInPhase < CastSettleFrames)
	{
		return true;
	}

	UWorld* World = GetWorld();
	FElysiumEntityWorld* Entities = GetEntityWorld();
	if (!World || !Entities)
	{
		Fail(TEXT("no entity world to stand a cast in"));
		return false;
	}
	if (!Entities->IsActive())
	{
		// A dormant world defers Activate, so a character spawned into one never thinks. Waiting is
		// right for a frame or two; the settle above is what bounds it.
		return true;
	}

	if (!bArenaStood)
	{
		if (!StandArena())
		{
			Fail(TEXT("the cast has no floor to walk on"));
			return false;
		}
		bArenaStood = true;
		return true;
	}
	if (!ElysiumArena::IsNavigationReady(World))
	{
		if (++NavigationWaitFrames > CastNavigationWaitFrames)
		{
			Fail(TEXT("Recast never finished building over the arena"));
			return false;
		}
		return true;
	}

	// The body every course stands. Resolved once the mount is readable, and named in the log
	// either way: a run that picked its own body must say which one it picked.
	if (BodyStem.IsEmpty())
	{
		const TArray<FString> Stems = ElysiumArenaCast::BodyStems();
		if (Stems.IsEmpty())
		{
			Fail(TEXT("the mount carries no baked character body — export the cast before recording it"));
			return false;
		}
		BodyStem = Stems[0];
		UE_LOG(LogElysiumCast, Log,
			TEXT("no -CastBody given; standing '%s', the first of %d baked bodies"),
			*BodyStem, Stems.Num());
	}

	if (CourseIndex < 0)
	{
		NextCourse();
		return !bDone;
	}

	// A freshly spawned character settles before it is given anything to do.
	if (SpawnSettleRemaining > 0)
	{
		if (--SpawnSettleRemaining == 0)
		{
			if (FindBody() == nullptr)
			{
				Fail(TEXT("the character stood up with no engine body — nothing can be recorded"));
				return false;
			}
			ArmCourse();
		}
		return true;
	}

	if (RecordRemaining > 0)
	{
		Sample();
		if (--RecordRemaining == 0)
		{
			FinishCourse();
			NextCourse();
		}
		return !bDone;
	}

	NextCourse();
	return !bDone;
}

#endif // !UE_BUILD_SHIPPING
