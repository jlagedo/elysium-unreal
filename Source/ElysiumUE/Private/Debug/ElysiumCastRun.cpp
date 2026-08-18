#include "Debug/ElysiumCastRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaBuilder.h"
#include "Debug/ElysiumArenaCast.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGraphState.h"
#include "ElysiumGymSpec.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumNpc.h"
#include "Visual/ElysiumNpcBody.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
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

	// How long an order is given to actually take the body. `FollowPatrolPath` arms synchronously,
	// but the mind claim behind a scripted beat can arrive a think later, so the check is a window
	// rather than an assertion on the next line.
	constexpr int32 CastArmWaitFrames = 120;

	// How long Recast is given to finish building over the host. The build is asynchronous and a
	// character let in before it answers cannot path at all, so waiting forever would record a course
	// of a body standing still; this fails loudly instead.
	constexpr int32 CastNavigationWaitFrames = 600;

	// --- The no-slide predicate's own numbers, Source units ------------------------------------
	// How far the realized speed may sit from the cell being played before the body is sliding. It is
	// tight on purpose: the mover is commanded with that exact cell every frame, so anything above
	// rounding means the two numbers came from different places, which is the whole defect.
	constexpr double NoSlideEpsilon = 1.0;
	// How near forward a frame has to be travelling before the cell it plays is required to be the
	// body's own forward cell. Anything wider is a strafe, and a strafe cell is a different number by
	// design — but the window is narrower than "not a strafe" for an arithmetic reason: the fan's
	// cells sit 45 degrees apart and `SpeedAt` blends between the two the angle falls between, so a
	// body ten degrees off forward is legitimately a fifth of the way toward a cell that can be
	// twenty u/s slower. Two degrees keeps that blend inside the epsilon, which is what makes a
	// failure here mean the wrong fan rather than the right one read off-axis.
	constexpr double ForwardWindowDegrees = 2.0;
	// The fewest at-speed frames a course that declares a gait has to produce, and the fewest of
	// those that have to be travelling forward. A body that never reached its commanded speed proved
	// nothing, and a predicate evaluated over no frames is the vacuous pass this replaces.
	constexpr int32 MinAtSpeedFrames = 30;
	constexpr int32 MinForwardFrames = 10;

	// What a course names its own entities. A generated prefix rather than a bare name so a course's
	// props cannot collide with anything a stage world — or a real map — already carries.
	const TCHAR* const CastWalkerName = TEXT("cast_walker");
	const TCHAR* const CastRoutePrefix = TEXT("cast_route_");
	const TCHAR* const CastMarkName = TEXT("cast_mark");

	// The file a failed run leaves in its own output directory. Its name is shared with
	// `channel_diff`, which refuses a directory carrying one.
	const TCHAR* const ElysiumCastRefusalFile = TEXT("run.failed");

	// The arena's default body. Named rather than "whatever the mount lists first": the first baked
	// stem is alphabetical and can be an animal, a gib or a prop rig, and a locomotion recording made
	// on one of those is a recording of the fallback. This is a shipped humanoid that authors the
	// three locomotion fans, and it is the same body the sited courses stand.
	const TCHAR* const CastDefaultBody = TEXT("regular_cop");

	// Which of the body's own fans a graph state is a state of. Only the three gaits have one; every
	// other state plays a cell whose speed the record already carries, and asking a fan for it would
	// be inventing a number.
	bool GaitKindForState(EElysiumGraphState State, EElysiumNpcGaitKind& OutKind)
	{
		switch (State)
		{
		case EElysiumGraphState::Walk:  OutKind = EElysiumNpcGaitKind::Walk;  return true;
		case EElysiumGraphState::Run:   OutKind = EElysiumNpcGaitKind::Run;   return true;
		case EElysiumGraphState::Sneak: OutKind = EElysiumNpcGaitKind::Sneak; return true;
		default: return false;
		}
	}

	// The map's own patrol node, by the token a level script spells: its targetname first, then the
	// `Group` key, which is what all 34 of Santa Monica's nodes actually carry.
	const FElysiumEntity* FindPatrolNode(const FElysiumEntityWorld& World, const FString& Name)
	{
		for (const TUniquePtr<FElysiumEntity>& Ent : World.Entities())
		{
			if (!Ent.IsValid() || Ent->Def == nullptr)
			{
				continue;
			}
			if (!Ent->Def->Classname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Ent->Def->TargetName.Equals(Name, ESearchCase::IgnoreCase)
				|| Ent->Def->Keys.FindRef(TEXT("Group")).Equals(Name, ESearchCase::IgnoreCase))
			{
				return Ent.Get();
			}
		}
		return nullptr;
	}
}

bool FElysiumCastRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumCast"));
}

TArray<const TCHAR*> FElysiumCastRun::DeclaredChannels()
{
	const TArrayView<const TCHAR* const> Shared = ElysiumLocomotionTrace::Channels();
	TArray<const TCHAR*> Names(Shared.GetData(), Shared.Num());
	// The one column only a commanded body can write. Without it a row cannot tell a slide from a
	// body still accelerating into its order, or one the path follower is holding under it.
	Names.Add(TEXT("act_cmd"));
	return Names;
}

FElysiumCastRun::FElysiumCastRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("CastHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	StepSeconds = 1.0f / static_cast<float>(Hz);

	FParse::Value(FCommandLine::Get(), TEXT("CastCourse="), CourseFilter);
	FParse::Value(FCommandLine::Get(), TEXT("CastBody="), BodyStem);
	FParse::Value(FCommandLine::Get(), TEXT("ElysiumMap="), MapName);

	// The host decides the courses, and a sited host that authors none is a run with nothing to do
	// rather than a run that silently records the arena instead.
	Courses = MapName.IsEmpty()
		? ElysiumCastCourses::Arena() : ElysiumCastCourses::Sited(MapName);

	UE_LOG(LogElysiumCast, Log, TEXT("headless cast run armed: %d Hz on %s, %d course(s)%s%s."), Hz,
		MapName.IsEmpty() ? TEXT("the arena") : *MapName, Courses.Num(),
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
	// The cast goes before the room does. A sited host has no room to tear down and its map outlives
	// the run, so a character left thinking on it would be one this harness spawned and never took
	// back.
	if (FElysiumEntityWorld* Entities = GetEntityWorld())
	{
		for (const FElysiumEntityHandle& Handle : CourseProps)
		{
			if (FElysiumEntity* Prop = Entities->Resolve(Handle))
			{
				Prop->Kill();
			}
		}
		if (FElysiumEntity* Walker = Entities->Resolve(CastEntity))
		{
			Walker->Kill();
		}
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

FString FElysiumCastRun::OutputDir() const
{
	const FString Root = FPaths::Combine(FElysiumContentPaths::Root(), TEXT("_cast"));
	// A sited host keeps its own directory, and therefore its own baseline. Sharing one would make
	// every arena-only run report the map's courses missing, and every sited run report the arena's.
	return MapName.IsEmpty() ? Root : FPaths::Combine(Root, MapName);
}

bool FElysiumCastRun::ClearOutputDir()
{
	const FString Dir = OutputDir();
	IFileManager& Files = IFileManager::Get();
	if (!Files.DirectoryExists(*Dir))
	{
		return true;
	}
	// The recordings only, never the tree: `baseline/` lives directly under this directory, and a
	// wholesale delete would take the thing the run is about to be compared against. The refusal
	// marker goes with them — this run has not failed yet, and a previous run's marker left in place
	// would fail it for something that is over.
	TArray<FString> Stale;
	Files.FindFiles(Stale, *(Dir / TEXT("*.csv")), /*Files*/ true, /*Directories*/ false);
	Files.FindFiles(Stale, *(Dir / TEXT("*.channels.json")), true, false);
	if (Files.FileExists(*(Dir / ElysiumCastRefusalFile)))
	{
		Stale.Add(ElysiumCastRefusalFile);
	}
	for (const FString& Name : Stale)
	{
		if (!Files.Delete(*(Dir / Name), /*RequireExists*/ true, /*EvenReadOnly*/ true))
		{
			UE_LOG(LogElysiumCast, Error,
				TEXT("could not delete the stale recording '%s' — a run that cannot clear its own ")
				TEXT("output leaves the comparator reading a previous run as this one"), *Name);
			return false;
		}
	}
	if (Stale.Num() > 0)
	{
		UE_LOG(LogElysiumCast, Log, TEXT("cleared %d stale recording file(s) from %s"),
			Stale.Num(), *Dir);
	}
	return true;
}

void FElysiumCastRun::Fail(const TCHAR* Reason)
{
	UE_LOG(LogElysiumCast, Error, TEXT("%s; exiting."), Reason);
	bDone = true;

	// **The refusal, on disk, where the comparator reads it.** A headless editor's process status is
	// not a reliable carrier — a clean exit posts its return code to a message loop this run does not
	// pump — and every failure here is a course that recorded nothing or a claim a recording did not
	// hold, which is exactly the shape of a green run that proved nothing. The marker is written into
	// the run's own output directory, which was cleared at run start, so its presence means THIS run
	// failed and the comparator refuses the whole directory rather than judging the courses that did
	// finish.
	const FString Marker = OutputDir() / ElysiumCastRefusalFile;
	const FString Body = FString::Printf(TEXT("%s") LINE_TERMINATOR, Reason);
	// Forced UTF-8: the default encoding is auto-detected per string, so a reason carrying a dash or
	// a quote silently becomes UTF-16 and the comparator reads a byte order mark instead of a
	// sentence.
	if (!FFileHelper::SaveStringToFile(Body, *Marker,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogElysiumCast, Error,
			TEXT("could not write the refusal marker to '%s' — this run's failure will only be ")
			TEXT("readable in the log"), *Marker);
	}
	FPlatformMisc::RequestExitWithStatus(/*Force*/ false, /*ReturnCode*/ 1);
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
	if (FElysiumEntity* Standing = Entities->Resolve(CastEntity))
	{
		Standing->Kill();
	}
	CastEntity = FElysiumEntityHandle::Invalid();

	// --- Where the course begins ----------------------------------------------------------------
	// An arena course is written against the room's own extent; a sited course begins on the first
	// node of the map's own route, facing the second, because those are the only coordinates in it
	// the map itself authored.
	const bool bSited = !Course.RouteNames.IsEmpty();
	FVector Start = ElysiumArena::DefaultOrigin() + Course.StartFeet;
	float StartYaw = Course.StartYaw;
	if (bSited)
	{
		const FElysiumEntity* First = FindPatrolNode(*Entities, Course.RouteNames[0]);
		const FElysiumEntity* Second = Course.RouteNames.Num() > 1
			? FindPatrolNode(*Entities, Course.RouteNames[1]) : nullptr;
		if (First == nullptr)
		{
			UE_LOG(LogElysiumCast, Error,
				TEXT("course '%s': %s authors no patrol node '%s' — the route this course names is ")
				TEXT("not in the exported map"),
				*Course.Name.ToString(), *MapName, *Course.RouteNames[0]);
			return false;
		}
		Start = First->Origin;
		StartYaw = Second != nullptr
			? static_cast<float>((Second->Origin - First->Origin).Rotation().Yaw) : StartYaw;
	}

	// --- Stand the character up -----------------------------------------------------------------
	// Before the props: a `scripted_sequence` names its NPC by targetname, and a marker standing up
	// ahead of the body it names reports it missing before the course has begun.
	ElysiumArenaCast::FSpawnRequest Request;
	Request.Model = Course.Model.IsEmpty() ? BodyStem : Course.Model;
	Request.TargetName = CastWalkerName;
	Request.Origin = Start;
	Request.Yaw = StartYaw;
	if (!Course.Classname.IsEmpty())
	{
		Request.Classname = Course.Classname;
	}
	Request.StatTemplate = Course.StatTemplate;
	Request.Weapon = Course.Weapon;
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
	// A sited course dresses nothing: its route is already standing in the map.
	if (!bSited && Course.Order == ElysiumCastCourses::EOrder::Patrol)
	{
		for (int32 Point = 0; Point < Course.Route.Num(); ++Point)
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("info_node_patrol_point");
			Def.TargetName = FString::Printf(TEXT("%s%d"), CastRoutePrefix, Point + 1);
			Def.Origin = ElysiumArena::DefaultOrigin() + Course.Route[Point];
			CourseProps.Add(Entities->SpawnRuntimeEntity(MoveTemp(Def)));
		}
	}
	else if (Course.Order == ElysiumCastCourses::EOrder::Scripted && !Course.Route.IsEmpty())
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("scripted_sequence");
		Def.TargetName = CastMarkName;
		Def.Origin = ElysiumArena::DefaultOrigin() + Course.Route[0];
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
	NoSlide.Reset();
	Previous = FPublishedCell();
	LoggedGeneration = 0;
	StartFeet = Start;
	StartForward = FRotator(0.0f, StartYaw, 0.0f).Vector();
	AdvanceMax = 0.0;
	SpawnSettleRemaining = CastSpawnSettleFrames;
	ArmWaitRemaining = 0;
	RecordRemaining = FMath::Max(1, FMath::RoundToInt(Course.RecordSeconds / StepSeconds));

	UE_LOG(LogElysiumCast, Log, TEXT("course '%s' begins at %s yaw %.1f, %d frames, body '%s'%s"),
		*Course.Name.ToString(), *Request.Origin.ToCompactString(), StartYaw, RecordRemaining,
		*Request.Model,
		Course.Weapon.IsEmpty() ? TEXT(", hands empty")
			: *FString::Printf(TEXT(", holding %s"), *Course.Weapon));
	return true;
}

bool FElysiumCastRun::ArmCourse()
{
	FElysiumEntityWorld* Entities = GetEntityWorld();
	if (!Courses.IsValidIndex(CourseIndex) || !Entities)
	{
		return false;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[CourseIndex];

	// Through the entity's own input table, which is the door a level script uses. Nothing here
	// touches the motor: what is being recorded is the whole chain from the order to the pose.
	switch (Course.Order)
	{
	case ElysiumCastCourses::EOrder::Patrol:
	{
		FString Route;
		if (!Course.RouteNames.IsEmpty())
		{
			Route = FString::Join(Course.RouteNames, TEXT(" "));
			if (!Course.PatrolType.IsEmpty())
			{
				// The level script's own first call. It is stored by the leaf and selects nothing
				// today; firing it is what makes this the route the map authors rather than a route
				// shaped like it.
				Entities->AcceptInput(CastEntity, FName(TEXT("SetupPatrolType")),
					FElysiumVariant::String(Course.PatrolType), CastEntity, CastEntity);
			}
		}
		else
		{
			for (int32 Point = 0; Point < Course.Route.Num(); ++Point)
			{
				Route += FString::Printf(TEXT("%s%s%d"), Route.IsEmpty() ? TEXT("") : TEXT(" "),
					CastRoutePrefix, Point + 1);
			}
		}
		Entities->AcceptInput(CastEntity, FName(TEXT("FollowPatrolPath")),
			FElysiumVariant::String(Route), CastEntity, CastEntity);
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': FollowPatrolPath(\"%s\")"),
			*Course.Name.ToString(), *Route);
		return true;
	}
	case ElysiumCastCourses::EOrder::Scripted:
	{
		const FElysiumEntityHandle Mark = CourseProps.IsEmpty()
			? FElysiumEntityHandle::Invalid() : CourseProps[0];
		if (!Mark.IsSet())
		{
			UE_LOG(LogElysiumCast, Error, TEXT("course '%s': no marker to begin"),
				*Course.Name.ToString());
			return false;
		}
		Entities->AcceptInput(Mark, FName(TEXT("BeginSequence")), FElysiumVariant::Void(),
			CastEntity, CastEntity);
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': BeginSequence (m_fMoveTo %d)"),
			*Course.Name.ToString(), Course.MoveTo);
		return true;
	}
	default:
		// Stand: the body is the whole course. Saying so keeps the log honest about a run with no
		// order in it rather than leaving a silent gap where every other course logs one.
		UE_LOG(LogElysiumCast, Log, TEXT("course '%s': no order — the body stands"),
			*Course.Name.ToString());
		return true;
	}
}

bool FElysiumCastRun::IsCourseArmed() const
{
	if (!Courses.IsValidIndex(CourseIndex))
	{
		return false;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[CourseIndex];
	if (Course.Order == ElysiumCastCourses::EOrder::Stand)
	{
		return true;
	}
	FElysiumEntityWorld* Entities = GetEntityWorld();
	FElysiumEntity* Entity = Entities ? Entities->Resolve(CastEntity) : nullptr;
	const FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
	if (Npc == nullptr)
	{
		return false;
	}
	// The mind's own account of who is driving the body. `FollowPatrolPath` and `BeginSequence` both
	// answer by acquiring it, and both return quietly when they cannot — which is the failure this
	// window exists to catch, because the recording that follows is a body standing still while its
	// record says a course ran.
	const EElysiumBodyOwner Owner = Npc->GetMind().Owner();
	return Course.Order == ElysiumCastCourses::EOrder::Patrol
		? Owner == EElysiumBodyOwner::Patrol
		: Owner == EElysiumBodyOwner::Sequence;
}

bool FElysiumCastRun::Sample()
{
	const AElysiumNpcBody* Body = FindBody();
	if (!Body)
	{
		// A body that has gone is a course that cannot be measured. It used to be a warning and a
		// short recording, and a short recording that still reaches disk is one the comparator reads
		// as this run.
		Fail(TEXT("the character lost its engine body mid-course"));
		return false;
	}

	const FVector P = Body->GetActorLocation();
	const FVector V = Body->GetVelocity();
	// The driver's own published pair, both halves of it. `SampleLocomotion()` recomputes from live
	// component state, so calling it here would write a sample the record beside it never saw — and
	// the writer's contract is the published sample and the published selection and nothing else.
	const FElysiumLocomotionSample& Locomotion = Body->GetAnimSample();
	const FElysiumAnimationSelection& Selection = Body->GetAnimSelection();

	const double Inv = 1.0 / ElysiumMove::U;
	const double Speed = Locomotion.Speed2D() * Inv;
	const double Stride = Selection.GroundSpeedCmPerSecond * Inv;
	const double Commanded = Locomotion.CommandedSpeed * Inv;

	Recorder.BeginFrame();
	// The same writer the player's harness calls, over the same two published records.
	ElysiumLocomotionTrace::Frame(Recorder, StepSeconds, P, V, Locomotion, Selection);
	Recorder.Set(TEXT("act_cmd"), Commanded);
	Totals.Observe(Locomotion, Selection);

	FString Error;
	if (!Recorder.EndFrame(Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("%s"), *Error);
		Fail(TEXT("a frame could not be recorded"));
		return false;
	}

	// --- The no-slide predicate -----------------------------------------------------------------
	// It is asked of the row against the cell that STEERED the row, which is the one the previous
	// animation pass published: this frame's velocity was realized under the command that pass left
	// on the mover. Comparing a row's speed against the cell it publishes for the next frame is
	// measuring the pass order, not the resolver.
	const double StillCut = FElysiumGaitReference().StillSpeed() * Inv;
	if (Speed > StillCut)
	{
		++NoSlide.MovingFrames;
	}
	// At speed: moving, under a live order, and AT what that order commands — two-sided, because a
	// body still accelerating into its cell and one the path follower is holding under it are both
	// honestly slower than their command, and neither is a claim about what is being played.
	if (Previous.bValid && Speed > StillCut && Commanded > StillCut
		&& FMath::Abs(Speed - Commanded) <= NoSlideEpsilon)
	{
		++NoSlide.AtSpeedFrames;

		// The claim itself, in two links that together say "the body travels at the speed of the
		// cell it is playing". The first is where the rung's defect lives: the motor is commanded off
		// its requested gait kind and the record publishes off its projected graph state, so the two
		// really can name different cells.
		const double Off = FMath::Abs(Commanded - Previous.Stride);
		NoSlide.WorstCommand = FMath::Max(NoSlide.WorstCommand, Off);
		if (Off > NoSlideEpsilon)
		{
			++NoSlide.CommandMismatches;
		}
		const double Slide = FMath::Abs(Speed - Previous.Stride);
		NoSlide.WorstSlide = FMath::Max(NoSlide.WorstSlide, Slide);
		if (Slide > NoSlideEpsilon)
		{
			++NoSlide.SlideFrames;
			if (NoSlide.FirstBadFrame == INDEX_NONE)
			{
				NoSlide.FirstBadFrame = Recorder.FrameCount() - 1;
				NoSlide.FirstBadSpeed = Speed;
				NoSlide.FirstBadStride = Previous.Stride;
				NoSlide.FirstBadCommanded = Commanded;
			}
		}

		// And the cell itself, where the direction makes the question answerable: travelling forward,
		// the cell that steered the row has to be the body's own authored forward cell for the state
		// it published it under.
		EElysiumNpcGaitKind Kind = EElysiumNpcGaitKind::Walk;
		if (FMath::Abs(Previous.MoveYaw) <= ForwardWindowDegrees
			&& GaitKindForState(Previous.State, Kind))
		{
			const double Forward = Body->GaitSpeed(Kind, 0.0f) * Inv;
			if (Forward > 0.0)
			{
				++NoSlide.ForwardFrames;
				const double CellOff = FMath::Abs(Previous.Stride - Forward);
				NoSlide.WorstForward = FMath::Max(NoSlide.WorstForward, CellOff);
				if (CellOff > NoSlideEpsilon)
				{
					++NoSlide.ForwardMismatches;
				}
			}
		}
	}
	Previous.bValid = true;
	Previous.Stride = Stride;
	Previous.MoveYaw = Selection.MoveYaw;
	Previous.State = Selection.GraphState;

	// --- The selection record, once per request -------------------------------------------------
	if (Selection.Generation != LoggedGeneration)
	{
		LoggedGeneration = Selection.Generation;
		// The key the record was resolved under is logged beside the record. The authored loadout on
		// the spawn keyfield is not evidence that the body is holding anything — what the ladder
		// walks is the classname of the weapon the driver read off the inventory, and that is what
		// this prints.
		FString ActorClass, ActiveWeapon;
		EElysiumNpcState ActorState = EElysiumNpcState::Idle;
		Body->GetAnimTranslationContext(ActorClass, ActiveWeapon, ActorState);
		UE_LOG(LogElysiumCast, Log,
			TEXT("selection @%d gen %u: %s [%s / %s / state %d] -> %s = %s@%s:%s | state %s ")
			TEXT("outcome %s | stride %.2f u/s cmd %.2f u/s speed %.2f u/s move_yaw %.1f deg"),
			Recorder.FrameCount() - 1, Selection.Generation,
			*Selection.RequestedActivity,
			ActorClass.IsEmpty() ? TEXT("(no class)") : *ActorClass,
			ActiveWeapon.IsEmpty() ? TEXT("empty hands") : *ActiveWeapon,
			static_cast<int32>(ActorState),
			*Selection.ResolvedActivity,
			Selection.SequenceLabel.IsEmpty() ? TEXT("(none)") : *Selection.SequenceLabel,
			Selection.OwnerStem.IsEmpty() ? TEXT("(none)") : *Selection.OwnerStem,
			Selection.AnimationName.IsEmpty() ? TEXT("(none)") : *Selection.AnimationName,
			ElysiumAnimGraph::StateName(Selection.GraphState),
			ElysiumAnimIntent::OutcomeName(Selection.Outcome),
			Stride, Commanded, Speed, Selection.MoveYaw);
	}

	const double Advance = FVector::DotProduct(P - StartFeet, StartForward) / ElysiumMove::U;
	AdvanceMax = FMath::Max(AdvanceMax, Advance);
	return true;
}

bool FElysiumCastRun::FinishCourse()
{
	if (!Courses.IsValidIndex(CourseIndex))
	{
		return false;
	}
	const ElysiumCastCourses::FCourse& Course = Courses[CourseIndex];
	if (Recorder.FrameCount() == 0)
	{
		UE_LOG(LogElysiumCast, Error,
			TEXT("course '%s' recorded no frames — there is nothing to write and nothing to check"),
			*Course.Name.ToString());
		return false;
	}

	Recorder.SetMeta(TEXT("harness"), TEXT("cast"));
	Recorder.SetMeta(TEXT("host"), MapName.IsEmpty() ? TEXT("arena") : *MapName);
	Recorder.SetMeta(TEXT("course"), Course.Name.ToString());
	// Nothing here is deferred: what a cast course measures is the resolver and the travel order,
	// and both are what this rung settled.
	Recorder.SetMeta(TEXT("baseline"), TEXT("committed"));
	Recorder.SetMetaNumber(TEXT("hz"), Hz);
	// Which body stood the course, under which classname, holding what. A recording made on another
	// body — or on the same one with empty hands — is a different recording, and the manifest is
	// where that has to be readable; the comparator refuses a baseline pair that disagrees.
	Recorder.SetMeta(TEXT("cast_body"), Course.Model.IsEmpty() ? BodyStem : Course.Model);
	Recorder.SetMeta(TEXT("cast_weapon"), Course.Weapon);
	Recorder.SetMeta(TEXT("cast_class"), Course.Classname);
	// And what the driver was ACTUALLY keyed on, which is the authored loadout only once the
	// inventory has equipped it. The pair is deliberate: `cast_weapon` is what the course asked for
	// and `cast_held` is what the ladder walked, and a course whose second is empty proved nothing
	// about the armed branch however the first reads.
	{
		FString ActorClass, ActiveWeapon;
		EElysiumNpcState ActorState = EElysiumNpcState::Idle;
		if (const AElysiumNpcBody* Keyed = FindBody())
		{
			Keyed->GetAnimTranslationContext(ActorClass, ActiveWeapon, ActorState);
		}
		Recorder.SetMeta(TEXT("cast_held"), ActiveWeapon);
		Recorder.SetMeta(TEXT("cast_keyed_class"), ActorClass);
		Recorder.SetMetaNumber(TEXT("cast_state"), static_cast<int32>(ActorState));
	}

	// The body's own authored fans (CCC7), the same three the movement harness records: a course run
	// on the no-fan fallback must not be mistaken for one run on the animation.
	if (const AElysiumNpcBody* Body = FindBody())
	{
		const float Inv = 1.0f / ElysiumMove::U;
		// The forward cell of each fan, which is what the constant's name claims — the direction is
		// left at zero deliberately, so the manifest keeps stating the same number whatever the body
		// happened to be facing when the course ended.
		Recorder.SetConstant(TEXT("GaitWalkForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Walk, 0.0f) * Inv);
		Recorder.SetConstant(TEXT("GaitRunForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Run, 0.0f) * Inv);
		Recorder.SetConstant(TEXT("GaitSneakForward"),
			Body->GaitSpeed(EElysiumNpcGaitKind::Sneak, 0.0f) * Inv);
	}
	// The cut every "is it moving" question in the predicate is asked against, and the bar it was
	// judged at, so a reader of the manifest is reading the same numbers the run judged itself with.
	Recorder.SetConstant(TEXT("StillSpeed"),
		FElysiumGaitReference().StillSpeed() / ElysiumMove::U);
	Recorder.SetConstant(TEXT("NoSlideEpsilon"), NoSlideEpsilon);

	Totals.Write(Recorder);
	Recorder.SetRun(TEXT("frames"), Recorder.FrameCount());
	Recorder.SetRun(TEXT("advance_max"), AdvanceMax);
	Recorder.SetRun(TEXT("slide_frames"),
		NoSlide.SlideFrames + NoSlide.ForwardMismatches + NoSlide.CommandMismatches);
	// The denominators ride as metadata rather than as channels: how many frames a body spends at its
	// commanded speed moves with the navigation solve, and a value with no stable comparison rule is
	// exactly what the registry refuses to accept as a channel.
	Recorder.SetMetaNumber(TEXT("noslide_moving"), NoSlide.MovingFrames);
	Recorder.SetMetaNumber(TEXT("noslide_at_speed"), NoSlide.AtSpeedFrames);
	Recorder.SetMetaNumber(TEXT("noslide_forward"), NoSlide.ForwardFrames);
	Recorder.SetMetaNumber(TEXT("noslide_worst"), NoSlide.WorstSlide);
	Recorder.SetMetaNumber(TEXT("noslide_worst_forward"), NoSlide.WorstForward);
	Recorder.SetMetaNumber(TEXT("noslide_worst_command"), NoSlide.WorstCommand);

	const FString Stem = FString::Printf(TEXT("%s.%s.%dhz"),
		MapName.IsEmpty() ? TEXT("arena") : *MapName, *Course.Name.ToString(), Hz);
	FString Error;
	if (!Recorder.Write(OutputDir(), Stem, Error))
	{
		UE_LOG(LogElysiumCast, Error, TEXT("%s"), *Error);
		return false;
	}

	UE_LOG(LogElysiumCast, Log,
		TEXT("course '%s': %d frames, advanced %.1f u, peak 2D %.1f u/s, %d resolved / %d fallback, ")
		TEXT("%d of %d moving frames at the commanded speed (worst slide %.2f, worst command ")
		TEXT("%.2f, worst cell %.2f u/s) -> %s"),
		*Course.Name.ToString(), Recorder.FrameCount(), AdvanceMax, Totals.PeakSpeed2D,
		Totals.ResolvedFrames, Totals.FallbackFrames, NoSlide.AtSpeedFrames, NoSlide.MovingFrames,
		NoSlide.WorstSlide, NoSlide.WorstCommand, NoSlide.WorstForward, *Stem);

	// --- The claim ------------------------------------------------------------------------------
	bool bHeld = true;
	auto Refuse = [&bHeld, &Course](const FString& Line)
	{
		bHeld = false;
		UE_LOG(LogElysiumCast, Error, TEXT("course '%s': %s"), *Course.Name.ToString(), *Line);
	};

	const double StillCut = FElysiumGaitReference().StillSpeed() / ElysiumMove::U;
	if (Course.bExpectStill)
	{
		if (Totals.PeakSpeed2D > StillCut)
		{
			Refuse(FString::Printf(
				TEXT("the body was asked for nothing and travelled at %.2f u/s, past the %.2f u/s ")
				TEXT("standstill"), Totals.PeakSpeed2D, StillCut));
		}
	}
	else if (Course.ExpectGait != EElysiumAnimActivityCode::Unknown)
	{
		if (Totals.PeakSpeed2D <= StillCut)
		{
			Refuse(FString::Printf(
				TEXT("the body never left the %.2f u/s standstill — peak was %.2f u/s, so every ")
				TEXT("frame of this course is an idle rather than the gait it is a course of"),
				StillCut, Totals.PeakSpeed2D));
		}
		const uint32 Wanted = 1u << static_cast<uint32>(Course.ExpectGait);
		if ((Totals.CodesSeen & Wanted) == 0)
		{
			Refuse(FString::Printf(TEXT("no frame reached %s; the codes seen were 0x%x"),
				ElysiumAnimIntent::ActivityName(Course.ExpectGait), Totals.CodesSeen));
		}
		if (NoSlide.AtSpeedFrames < MinAtSpeedFrames)
		{
			Refuse(FString::Printf(
				TEXT("only %d frame(s) reached the speed the motor commanded, under the %d this ")
				TEXT("predicate needs to say anything at all"),
				NoSlide.AtSpeedFrames, MinAtSpeedFrames));
		}
		if (NoSlide.ForwardFrames < MinForwardFrames)
		{
			Refuse(FString::Printf(
				TEXT("only %d at-speed frame(s) travelled forward, under the %d it takes to say the ")
				TEXT("cell is this body's own authored one rather than some other number"),
				NoSlide.ForwardFrames, MinForwardFrames));
		}
	}
	if (NoSlide.CommandMismatches > 0)
	{
		Refuse(FString::Printf(
			TEXT("%d of %d at-speed frame(s) were commanded a speed that is not the cell the record ")
			TEXT("published for them (worst %.2f u/s) — the motor's gait kind and the record's graph ")
			TEXT("state named different cells"),
			NoSlide.CommandMismatches, NoSlide.AtSpeedFrames, NoSlide.WorstCommand));
	}
	if (NoSlide.SlideFrames > 0)
	{
		Refuse(FString::Printf(
			TEXT("%d of %d at-speed frame(s) slid: the body travelled at a speed the cell steering ")
			TEXT("it does not author (worst %.2f u/s). First at frame %d: speed %.2f, cell %.2f, ")
			TEXT("commanded %.2f u/s"),
			NoSlide.SlideFrames, NoSlide.AtSpeedFrames, NoSlide.WorstSlide, NoSlide.FirstBadFrame,
			NoSlide.FirstBadSpeed, NoSlide.FirstBadStride, NoSlide.FirstBadCommanded));
	}
	if (NoSlide.ForwardMismatches > 0)
	{
		Refuse(FString::Printf(
			TEXT("%d of %d forward-travelling frame(s) played a cell that is not this body's own ")
			TEXT("authored forward cell (worst %.2f u/s off)"),
			NoSlide.ForwardMismatches, NoSlide.ForwardFrames, NoSlide.WorstForward));
	}
	return bHeld;
}

void FElysiumCastRun::NextCourse()
{
	int32 Next = CourseIndex + 1;
	while (Courses.IsValidIndex(Next)
		&& !CourseFilter.IsEmpty() && CourseFilter != Courses[Next].Name.ToString())
	{
		++Next;
	}
	if (!Courses.IsValidIndex(Next))
	{
		if (CoursesRun == 0)
		{
			// A run that opened nothing is a run that proved nothing, and exiting clean is how it
			// reads as a pass. The two ways to get here are a filter that names no course and a host
			// that authors none.
			const FString Reason = CourseFilter.IsEmpty()
				? FString(TEXT("no course ran: this host authors none"))
				: FString::Printf(
					TEXT("no course ran: '%s' names none of the %d this host authors"),
					*CourseFilter, Courses.Num());
			Fail(*Reason);
			return;
		}
		UE_LOG(LogElysiumCast, Log, TEXT("cast run complete: %d course(s); exiting."), CoursesRun);
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

	if (!bOutputCleared)
	{
		if (!ClearOutputDir())
		{
			Fail(TEXT("the run could not clear its own output directory"));
			return false;
		}
		bOutputCleared = true;
		return true;
	}

	// The arena is stood only on the arena host. A sited run has the map's own floor and the map's
	// own navigation, and building a room into one would be measuring the harness.
	if (MapName.IsEmpty() && !bArenaStood)
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
			Fail(TEXT("Recast never finished building over this host"));
			return false;
		}
		return true;
	}

	// The body every arena course stands. A named humanoid rather than whatever the mount lists
	// first: the first baked stem is alphabetical, and a locomotion recording made on an animal or a
	// prop rig is a recording of the fallback rather than of the resolver.
	if (BodyStem.IsEmpty())
	{
		const TArray<FString> Stems = ElysiumArenaCast::BodyStems();
		if (Stems.IsEmpty())
		{
			Fail(TEXT("the mount carries no baked character body — export the cast before recording it"));
			return false;
		}
		if (!Stems.Contains(CastDefaultBody))
		{
			const FString Reason = FString::Printf(
				TEXT("the mount carries no '%s' — the default cast body is a named locomoting ")
				TEXT("humanoid, and standing a different one silently would record another body; ")
				TEXT("pass -CastBody=<stem> to choose deliberately"), CastDefaultBody);
			Fail(*Reason);
			return false;
		}
		BodyStem = CastDefaultBody;
		UE_LOG(LogElysiumCast, Log, TEXT("no -CastBody given; standing '%s' of %d baked bodies"),
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
			if (!ArmCourse())
			{
				Fail(TEXT("a course's travel order could not be delivered"));
				return false;
			}
			ArmWaitRemaining = CastArmWaitFrames;
		}
		return true;
	}

	// And the order has to have taken the body before the window it is recorded over opens.
	if (ArmWaitRemaining > 0)
	{
		if (IsCourseArmed())
		{
			ArmWaitRemaining = 0;
			++CoursesRun;
		}
		else if (--ArmWaitRemaining == 0)
		{
			const FString Reason = FString::Printf(
				TEXT("course '%s': the order returned without taking the body — the whole recording ")
				TEXT("would be a body standing still while its record said a course ran"),
				*Courses[CourseIndex].Name.ToString());
			Fail(*Reason);
			return false;
		}
		else
		{
			return true;
		}
	}

	if (RecordRemaining > 0)
	{
		if (!Sample())
		{
			return false;
		}
		if (--RecordRemaining == 0)
		{
			if (!FinishCourse())
			{
				Fail(TEXT("a cast course did not hold its own claim"));
				return false;
			}
			NextCourse();
		}
		return !bDone;
	}

	NextCourse();
	return !bDone;
}

#endif // !UE_BUILD_SHIPPING
