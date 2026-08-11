#include "Debug/ElysiumGreenRoomRun.h"

#include "Debug/ElysiumClothDebug.h"

#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerUISubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumSkeletalBasis.h"
#include "Debug/ElysiumScreenshot.h"
#if !UE_BUILD_SHIPPING
// The gym's engine half is debug-only, while its spec is not. Drive mode is the only thing here that
// needs the spawner, so the guard is on the one call rather than on the harness.
#include "Debug/ElysiumGymBuilder.h"
#endif
#include "Substrate/ElysiumCameraTrack.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGreenRoom, Log, All);

namespace
{
	constexpr int32 GreenRoomBootSettleFrames = 8;
	constexpr int32 PoseWarmupFrames = 4;

	FString VecJson(const FVector& V)
	{
		return FString::Printf(TEXT("[%.3f, %.3f, %.3f]"), V.X, V.Y, V.Z);
	}

	FString RotJson(const FRotator& R)
	{
		return FString::Printf(TEXT("[%.3f, %.3f, %.3f]"), R.Pitch, R.Yaw, R.Roll);
	}

	FString Vec2Json(const FVector2D& V)
	{
		return FString::Printf(TEXT("[%.3f, %.3f]"), V.X, V.Y);
	}

	bool IsFiniteVector(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}
}

bool FElysiumGreenRoomRun::IsRequested()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ElysiumGreenRoom")))
	{
		return false;
	}
	if (GUsingNullRHI)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("-ElysiumGreenRoom ignored: rendered skeletal validation requires a real RHI."));
		return false;
	}
	return true;
}

FElysiumGreenRoomRun::FElysiumGreenRoomRun(UElysiumMapSubsystem* InSubsystem, bool bForceLab)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomCase="), Selector);
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomSettle="), SettleFrames);
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomStem="), ReviewStem);
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomClip="), ReviewClip);
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomAnimSet="), ReviewAnimSet);
	FParse::Value(FCommandLine::Get(), TEXT("GreenRoomBoneRoot="), ReviewBoneRoot);
	bLive = FParse::Param(FCommandLine::Get(), TEXT("GreenRoomLive"));
	Selector = Selector.ToLower();
	// A drive request implies the lab — there is nothing to drive in a capture run — but it cannot be
	// acted on here: there is no map, no pawn and no floor yet.
	bDriveRequested = FParse::Param(FCommandLine::Get(), TEXT("GreenRoomDrive"));
	bLab = bForceLab || bDriveRequested || FParse::Param(FCommandLine::Get(), TEXT("GreenRoomLab"))
		|| Selector == TEXT("lab");
	if (bLab)
	{
		Selector = TEXT("lab");
		bLive = true;
	}
	SettleFrames = FMath::Max(2, SettleFrames);
	Fractions = { 0.0f, 0.25f, 0.5f, 0.75f, 0.99f };
	ReviewViewYaws = { 0.0f, 45.0f, 90.0f, 180.0f };

	UE_LOG(LogElysiumGreenRoom, Log, TEXT("green room armed: case=%s settle=%d%s"),
		*Selector, SettleFrames, bLab ? TEXT(" (interactive lab)") : TEXT(""));
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumGreenRoomRun::Tick));
}

FElysiumGreenRoomRun::~FElysiumGreenRoomRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	// The lab took the HUD off screen; it does not own it past its own lifetime. `elysium.gr` can
	// arm a lab inside an ordinary session, so leaving the HUD hidden would look like a HUD bug
	// long after the green room is gone.
	if (bLab)
	{
		ApplyLabHud(true);
	}
	DestroyDriveGym();
	DestroyBodies();
}

UWorld* FElysiumGreenRoomRun::GetWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	return GI ? GI->GetWorld() : nullptr;
}

AElysiumMapActor* FElysiumGreenRoomRun::GetMap() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	return Sub ? Sub->GetCurrentMap() : nullptr;
}

void FElysiumGreenRoomRun::ResolveCases()
{
	const TArray<FCase> Opening =
	{
		{ TEXT("player"), TEXT("brujah_male_armor_0"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips1.mdl"), TEXT("Bip01"), true },
		{ TEXT("sire"), TEXT("brujah_female_armor_3"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips1.mdl"), TEXT("Bip02"), false },
		{ TEXT("vampire1"), TEXT("malkavian_male_armor_0"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips1.mdl"), TEXT("Bip03"), false },
		{ TEXT("vampire2"), TEXT("toreador_male_armor_0"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips1.mdl"), TEXT("Bip04"), false },
		{ TEXT("sheriff"), TEXT("sheriff"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips2.mdl"), TEXT("Bip05"), false },
	};
	const TArray<FCase> OpeningProps =
	{
		{ TEXT("wineglass_1"), TEXT("cin_wineglass"), TEXT("wineglass_1"),
			TEXT("models/cinematic/santa_monica/haven/cin_wineglass.mdl"), false, true, false },
		{ TEXT("wineglass_2"), TEXT("cin_wineglass"), TEXT("wineglass_2"),
			TEXT("models/cinematic/santa_monica/haven/cin_wineglass.mdl"), false, true, false },
		{ TEXT("stake_idle01"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("idle01"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, true },
		{ TEXT("pc_pre"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("pc_pre"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		{ TEXT("pc_post"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("pc_post"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		{ TEXT("pc_post_female"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("pc_post_female"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		{ TEXT("sire_fly"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("sire_fly"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		{ TEXT("sire_pre"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("sire_pre"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		{ TEXT("sire_post"), TEXT("cinematic_santa_monica_haven_cin_stake"), TEXT("sire_post"),
			TEXT("models/cinematic/santa_monica/haven/cin_stake.mdl"), false, true, false },
		// The courtroom half. `scene` is the clip the `courtroom_scene_relay` wire plays on all
		// three, and `idle01`/`idle` is the sequence each rests on; both are covered because the
		// rest pose and the scripted one-shot resolve through different rules.
		{ TEXT("sword_idle01"), TEXT("cin_sheriff_sword"), TEXT("idle01"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl"), false, true, true },
		{ TEXT("sword_scene"), TEXT("cin_sheriff_sword"), TEXT("scene"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl"), false, true, false },
		{ TEXT("courtroom_stake_idle01"), TEXT("cinematic_santa_monica_courtroom_cin_stake"),
			TEXT("idle01"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_stake.mdl"), false, true, true },
		{ TEXT("courtroom_stake_scene"), TEXT("cinematic_santa_monica_courtroom_cin_stake"),
			TEXT("scene"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_stake.mdl"), false, true, false },
		{ TEXT("cigar_idle"), TEXT("cin_cigar"), TEXT("idle"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_cigar.mdl"), false, true, true },
		{ TEXT("cigar_scene"), TEXT("cin_cigar"), TEXT("scene"),
			TEXT("models/cinematic/santa_monica/courtroom/cin_cigar.mdl"), false, true, false },
	};
	const TArray<FCase> Courtroom =
	{
		// A deliberately small authored-space oracle. Vampire4 is one of the seated audience actors
		// reported facing away from the stage; LaCroix supplies the intended focus side without
		// requiring the opening scripts, cameras, or the rest of the cast.
		{ TEXT("vampire4_seated"), TEXT("ventrue_female_armor_1"),
			TEXT("models/cinematic/santa_monica/courtroom/courtroom_bip5.mdl"),
			TEXT("Bip01"), false },
		{ TEXT("prince"), TEXT("lacroix"),
			TEXT("models/cinematic/santa_monica/courtroom/courtroom_bip2.mdl"),
			TEXT("Bip01"), false },
	};

	ActiveCases.Reset();
	if (bLab)
	{
		// The lab resolves no case at all. It builds the room and waits: which body stands on it is
		// the window's decision, taken and re-taken while the run is alive, and pre-seeding one from
		// the command line would only mean answering that question twice.
		bReview = true;   // the review stage dressing — no backdrop wall, so an orbit stays clean
		return;
	}
	bTheatreCamera = Selector == TEXT("embrace");
	bCourtroom = Selector == TEXT("courtroom");
	bReview = Selector == TEXT("review");
	bEnsemble = Selector == TEXT("opening") || bTheatreCamera || bCourtroom;
	if (bReview)
	{
		if (ReviewStem.IsEmpty() || ReviewClip.IsEmpty())
		{
			UE_LOG(LogElysiumGreenRoom, Warning,
				TEXT("review needs -GreenRoomStem=<stem> and -GreenRoomClip=<clip>"));
			bAnyFailure = true;
			return;
		}
		const bool bCinematic = !ReviewAnimSet.IsEmpty() && !ReviewBoneRoot.IsEmpty();
		ActiveCases.Add({
			ReviewStem, ReviewStem, ReviewAnimSet, ReviewBoneRoot,
			false, false, false, ReviewClip, !bCinematic
		});
		return;
	}
	if (bCourtroom)
	{
		ActiveCases = Courtroom;
		return;
	}
	if (Selector == TEXT("props"))
	{
		ActiveCases = OpeningProps;
		return;
	}
	if (bEnsemble || Selector == TEXT("all"))
	{
		ActiveCases = Opening;
		return;
	}
	if (Selector == TEXT("placeholder"))
	{
		// Diagnostic only: this is the authored temporary body before chooseSire() replaces it. Its
		// loud rainbow torso is source content and must never be confused with the final sire.
		ActiveCases.Add({ TEXT("placeholder"), TEXT("doppleganger_male"),
			TEXT("models/cinematic/santa_monica/haven/embrace_bips1.mdl"), TEXT("Bip02"), false });
		return;
	}
	for (const FCase& Candidate : Opening)
	{
		if (Candidate.Label == Selector)
		{
			ActiveCases.Add(Candidate);
			return;
		}
	}
	for (const FCase& Candidate : OpeningProps)
	{
		if (Candidate.Label == Selector)
		{
			ActiveCases.Add(Candidate);
			return;
		}
	}
	UE_LOG(LogElysiumGreenRoom, Warning,
		TEXT("unknown GreenRoomCase '%s' (player|sire|vampire1|vampire2|sheriff|opening|")
		TEXT("embrace|courtroom|props|all|placeholder|prop clip)"),
		*Selector);
	bAnyFailure = true;
}

bool FElysiumGreenRoomRun::CreateStage()
{
	if (bTheatreCamera)
	{
		return PrepareTheatreCase();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor)
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("GreenRoomRoot"));
	Actor->AddInstanceComponent(Root);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	StageActor = Actor;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("green room could not load Engine cube"));
		return false;
	}
	auto MakePanel = [Actor, Root, Cube](const TCHAR* Name)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Actor, Name);
		Actor->AddInstanceComponent(Comp);
		Comp->SetupAttachment(Root);
		Comp->SetStaticMesh(Cube);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->RegisterComponent();
		return Comp;
	};
	Floor = MakePanel(TEXT("GreenRoomFloor"));
	Wall = MakePanel(TEXT("GreenRoomWall"));
	if (UStaticMeshComponent* WallComp = Wall.Get())
	{
		// A backdrop is useful for deterministic single-view captures, but it occludes the
		// rear half of a human review orbit. Review uses the floor and neutral sky instead.
		WallComp->SetVisibility(!bReview);
	}

	auto MakeLight = [Actor, Root](const TCHAR* Name, float Intensity)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Actor, Name);
		Actor->AddInstanceComponent(Light);
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(Intensity);
		Light->SetAttenuationRadius(1800.0f);
		Light->SetCastShadows(true);
		Light->RegisterComponent();
		return Light;
	};
	KeyLight = MakeLight(TEXT("GreenRoomKey"), 80000.0f);
	FillLight = MakeLight(TEXT("GreenRoomFill"), 30000.0f);
	if (FillLight.IsValid())
	{
		FillLight->SetLightColor(FLinearColor(0.30f, 0.45f, 1.0f));
	}

	// In a stage world nothing else lights anything — no map sky light, no fog, no environment — and
	// two point lights against a void read far harsher than the same two standing inside a map. The
	// stage carries its own ambient there. Only there: inside a real map the map's environment is the
	// baseline every existing capture was taken against, and a second sky light would move it.
	const UElysiumMapSubsystem* Maps = Subsystem.Get();
	if (Maps && Maps->IsStageWorld())
	{
		USkyLightComponent* Sky = NewObject<USkyLightComponent>(Actor, TEXT("GreenRoomAmbient"));
		Actor->AddInstanceComponent(Sky);
		Sky->SetupAttachment(Root);
		Sky->SetMobility(EComponentMobility::Movable);
		Sky->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		Sky->Cubemap = ElysiumEnvironment::BuildConstantCube(FLinearColor(0.55f, 0.58f, 0.68f));
		Sky->SetIntensity(1.0f);
		Sky->RegisterComponent();
		Sky->RecaptureSky();
		Ambient = Sky;
	}
	return true;
}

bool FElysiumGreenRoomRun::PrepareTheatreCase()
{
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	FElysiumEntity* Scene = World ? World->FindByName(TEXT("embrace_o_matic")) : nullptr;
	FElysiumEntity* PositionRoot = World ? World->FindByName(TEXT("embrace_camera")) : nullptr;
	FElysiumEntity* TargetRoot = World ? World->FindByName(TEXT("embrace_target")) : nullptr;
	if (!Scene || !PositionRoot || !TargetRoot)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("embrace room requires embrace_o_matic, embrace_camera, and embrace_target"));
		return false;
	}

	TheatreSceneOrigin = Scene->Origin;
	TheatreSceneAngles = Scene->Angles;
	TheatrePositionOwner = PositionRoot->Handle;
	TheatreTargetOwner = TargetRoot->Handle;
	World->SelectTrackCameraRole(false, TheatrePositionOwner);
	World->SelectTrackCameraRole(true, TheatreTargetOwner);

	FElysiumEntityDefs Defs;
	if (!FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(TEXT("sp_theatre")), Defs))
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("embrace room could not parse sp_theatre.ents"));
		return false;
	}
	TMap<FString, const FElysiumEntityDef*> Named;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (!Def.TargetName.IsEmpty())
		{
			Named.FindOrAdd(Def.TargetName.ToLower(), &Def);
		}
	}

	auto FloatKey = [](const FElysiumEntityDef& Def, const TCHAR* Name, float Default)
	{
		const FString* Value = Def.Keys.Find(Name);
		return Value && !Value->IsEmpty() ? FCString::Atof(**Value) : Default;
	};
	auto BoolKey = [](const FElysiumEntityDef& Def, const TCHAR* Name, bool Default)
	{
		const FString* Value = Def.Keys.Find(Name);
		return Value && !Value->IsEmpty() ? FCString::Atoi(**Value) != 0 : Default;
	};
	auto VectorKey = [](const FElysiumEntityDef& Def, const TCHAR* Name)
	{
		const FString* Value = Def.Keys.Find(Name);
		if (!Value)
		{
			return FVector::ZeroVector;
		}
		TArray<FString> Parts;
		Value->ParseIntoArrayWS(Parts);
		return Parts.Num() >= 3
			? FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]))
			: FVector::ZeroVector;
	};
	auto BuildPath = [&Named, &FloatKey, &BoolKey, &VectorKey](
		const TCHAR* RootName, ElysiumCameraTrack::FPath& Out,
		TArray<const FElysiumEntityDef*>& OutDefs)
	{
		Out = ElysiumCameraTrack::FPath();
		OutDefs.Reset();
		const FElysiumEntityDef* const* Root = Named.Find(FString(RootName).ToLower());
		const FElysiumEntityDef* Cursor = Root ? *Root : nullptr;
		TSet<FString> Seen;
		for (int32 Guard = 0; Cursor && Guard < 1024; ++Guard)
		{
			const FString Current = Cursor->TargetName.ToLower();
			if (Seen.Contains(Current))
			{
				return false;
			}
			Seen.Add(Current);

			ElysiumCameraTrack::FPoint Point;
			Point.Position = Cursor->Origin;
			Point.SourceAngles = VectorKey(*Cursor, TEXT("angles"));
			Point.Roll = FloatKey(*Cursor, TEXT("Roll"), 0.0f);
			Point.FocalLength = FloatKey(*Cursor, TEXT("FocalLength"), 0.0f);
			Point.bTimeControl = BoolKey(*Cursor, TEXT("TimeControl"), false);
			Point.MoveSpeed = FloatKey(*Cursor, TEXT("MoveSpeed"), 64.0f);
			Point.MoveTime = FloatKey(*Cursor, TEXT("MoveTime"), 0.0f);
			Point.Pause = FloatKey(*Cursor, TEXT("Pause"), 0.0f);
			Point.RateIn = FloatKey(*Cursor, TEXT("RateIn"), 1.0f);
			Point.RateOut = FloatKey(*Cursor, TEXT("RateOut"), 1.0f);
			Point.bCorner = BoolKey(*Cursor, TEXT("Corner"), false);
			Out.Points.Add(Point);
			OutDefs.Add(Cursor);

			const FString Next = Cursor->Keys.FindRef(TEXT("NextKey"));
			if (Next.IsEmpty())
			{
				break;
			}
			const FElysiumEntityDef* const* Found = Named.Find(Next.ToLower());
			if (!Found)
			{
				return false;
			}
			Cursor = *Found;
		}
		Out.RebuildTimes();
		return Out.Points.Num() > 0;
	};

	TheatrePositionPath = MakeUnique<ElysiumCameraTrack::FPath>();
	TheatreTargetPath = MakeUnique<ElysiumCameraTrack::FPath>();
	TArray<const FElysiumEntityDef*> PositionDefs;
	TArray<const FElysiumEntityDef*> TargetDefs;
	if (!BuildPath(TEXT("embrace_camera"), *TheatrePositionPath, PositionDefs)
		|| !BuildPath(TEXT("embrace_target"), *TheatreTargetPath, TargetDefs))
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("embrace room could not build both camera paths"));
		return false;
	}
	TheatreDuration = FMath::Max(TheatrePositionPath->EndTime, TheatreTargetPath->EndTime);
	if (TheatreDuration <= 0.0f
		|| !FMath::IsNearlyEqual(TheatrePositionPath->EndTime, TheatreTargetPath->EndTime, 0.001f))
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("embrace camera clocks disagree: position %.3f target %.3f"),
			TheatrePositionPath->EndTime, TheatreTargetPath->EndTime);
		return false;
	}

	TheatreFades.Reset();
	for (int32 Index = 0; Index < PositionDefs.Num(); ++Index)
	{
		const FElysiumEntityDef& Point = *PositionDefs[Index];
		for (const FElysiumOutputDef& Output : Point.Outputs)
		{
			const bool bReached = Output.Name.Equals(TEXT("OnReachedKeyframe"), ESearchCase::IgnoreCase);
			const bool bLeaving = Output.Name.Equals(TEXT("OnLeavingKeyframe"), ESearchCase::IgnoreCase);
			if ((!bReached && !bLeaving)
				|| !Output.Input.Equals(TEXT("Fade"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FElysiumEntityDef* const* Found = Named.Find(Output.Target.ToLower());
			const FElysiumEntityDef* Fade = Found ? *Found : nullptr;
			if (!Fade || !Fade->Classname.Equals(TEXT("env_fade"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FFadeWindow Window;
			Window.StartTime = (bReached
				? TheatrePositionPath->Arrivals[Index]
				: TheatrePositionPath->Departures[Index]) + FMath::Max(0.0f, Output.Delay);
			Window.Duration = FMath::Max(0.0f, FloatKey(*Fade, TEXT("duration"), 0.0f));
			Window.HoldTime = FMath::Max(0.0f, FloatKey(*Fade, TEXT("holdtime"), 0.0f));
			Window.Opacity = FMath::Clamp(FloatKey(*Fade, TEXT("renderamt"), 255.0f) / 255.0f, 0.0f, 1.0f);
			Window.bAutoReverse = (FMath::RoundToInt(FloatKey(*Fade, TEXT("spawnflags"), 0.0f)) & 8) != 0;
			TheatreFades.Add(Window);
		}
	}
	TheatreFades.Sort([](const FFadeWindow& A, const FFadeWindow& B)
	{
		return A.StartTime < B.StartTime;
	});

	// Sample every authored edit from both independently timed streams. Positive moves get a
	// midpoint; true zero-time cuts get frames immediately before and after the boundary.
	TArray<float> Times;
	auto AddTime = [&Times, this](float Time)
	{
		const float Clamped = FMath::Clamp(Time, 0.0f, TheatreDuration);
		if (!Times.ContainsByPredicate([Clamped](float Existing)
		{
			return FMath::IsNearlyEqual(Existing, Clamped, 0.002f);
		}))
		{
			Times.Add(Clamped);
		}
	};
	auto AddPathEdits = [&AddTime](const ElysiumCameraTrack::FPath& Path)
	{
		for (int32 Index = 0; Index + 1 < Path.Points.Num(); ++Index)
		{
			const float Departure = Path.Departures[Index];
			const float Arrival = Path.Arrivals[Index + 1];
			if (ElysiumCameraTrack::IsHardCut(Path.Points[Index]))
			{
				AddTime(Arrival - 0.01f);
				AddTime(Arrival + 0.01f);
			}
			else
			{
				AddTime(Departure + (Arrival - Departure) * 0.5f);
			}
		}
	};
	AddTime(0.0f);
	AddPathEdits(*TheatrePositionPath);
	AddPathEdits(*TheatreTargetPath);
	for (const FFadeWindow& Window : TheatreFades)
	{
		AddTime(Window.StartTime - 0.01f);
		AddTime(Window.StartTime + 0.01f);
		AddTime(Window.StartTime + Window.Duration + Window.HoldTime - 0.01f);
		AddTime(Window.StartTime + Window.Duration + Window.HoldTime + Window.Duration + 0.01f);
	}
	AddTime(TheatreDuration - 0.01f);
	Times.Sort();
	Fractions.Reset();
	for (const float Time : Times)
	{
		Fractions.Add(Time / TheatreDuration);
	}
	UE_LOG(LogElysiumGreenRoom, Log,
		TEXT("embrace room ready: origin=%s yaw=%.1f camera=%.3fs samples=%d fades=%d"),
		*TheatreSceneOrigin.ToCompactString(), TheatreSceneAngles.Y, TheatreDuration,
		Fractions.Num(), TheatreFades.Num());
	return true;
}

void FElysiumGreenRoomRun::DestroyBodies()
{
	AElysiumMapActor* Map = GetMap();
	for (FBodyEntry& Entry : Bodies)
	{
		if (Entry.Case.bPlayerSurface && bPlayerSurfaceActive)
		{
			continue;
		}
		if (USkeletalMeshComponent* Body = Entry.Body.Get())
		{
			Body->DestroyComponent();
		}
	}
	if (bPlayerSurfaceActive && Map)
	{
		Map->ClearPlayerVisual();
	}
	bPlayerSurfaceActive = false;
	Bodies.Reset();
}

bool FElysiumGreenRoomRun::BuildBodies()
{
	DestroyBodies();
	AElysiumMapActor* Map = GetMap();
	if (!Map || ActiveCases.IsEmpty())
	{
		return false;
	}

	const int32 Begin = bEnsemble ? 0 : CaseIndex;
	const int32 End = bEnsemble ? ActiveCases.Num() : CaseIndex + 1;
	for (int32 Index = Begin; Index < End; ++Index)
	{
		const FCase& Case = ActiveCases[Index];
		USkeletalMeshComponent* Body = nullptr;
		if (Case.bAnimatedProp)
		{
			const FString ResolvedStem = Map->AnimatedPropStemForModel(Case.BoneRoot);
			if (!ResolvedStem.Equals(Case.MeshStem, ESearchCase::CaseSensitive))
			{
				UE_LOG(LogElysiumGreenRoom, Warning,
					TEXT("%s: model %s resolved animated stem '%s', expected '%s'"),
					*Case.Label, *Case.BoneRoot, *ResolvedStem, *Case.MeshStem);
				bAnyFailure = true;
				continue;
			}
			Body = Map->BuildAnimatedPropVisual(ResolvedStem, BodyOrigin(),
				FQuat(BodyRotation(/*bAnimatedProp=*/true)), 1.0f);
			if (Body)
			{
				Map->ApplyAnimatedPropSkin(Body, FPaths::GetBaseFilename(Case.BoneRoot).ToLower(), 0);
			}
		}
		else if (Case.bPlayerSurface && !bTheatreCamera)
		{
			Body = Map->BuildPlayerVisual(Case.MeshStem, TEXT("Neutral"), 0);
			bPlayerSurfaceActive = Body != nullptr;
			if (Body && Map->GetRootComponent())
			{
				Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				Body->AttachToComponent(Map->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			}
		}
		else
		{
			Body = Map->BuildNpcVisual(Case.MeshStem, BodyOrigin(),
				BodyRotation(/*bAnimatedProp=*/false), 1.0f, TEXT("Neutral"), 0);
		}
		if (!Body)
		{
			UE_LOG(LogElysiumGreenRoom, Warning, TEXT("%s: failed to build %s"),
				*Case.Label, *Case.MeshStem);
			bAnyFailure = true;
			continue;
		}
		Body->SetWorldLocationAndRotation(BodyOrigin(), BodyRotation(Case.bAnimatedProp));
		Body->SetVisibility(true, true);
		Body->SetComponentTickEnabled(true);

		float Duration = 0.0f;
		const FString ClipName = Case.ClipName.IsEmpty()
			? TEXT("entire_scene") : Case.ClipName;
		const bool bPlayed = Case.bAnimatedProp
			? Map->PlayAnimatedPropClip(Body, Case.MeshStem, Case.AnimSetModel,
				Case.bLoop, &Duration)
			: Case.bResolvedClip
				? Map->PlayNpcClip(Body, Case.MeshStem, ClipName, Case.bLoop, &Duration)
				: Map->PlayCinematicClip(Body, Case.MeshStem, Case.AnimSetModel, Case.BoneRoot,
					ClipName, Case.bLoop, &Duration);
		if (!bPlayed)
		{
			if (Case.bAnimatedProp)
			{
				UE_LOG(LogElysiumGreenRoom, Warning,
					TEXT("%s: failed to play animated prop %s/%s"),
					*Case.Label, *Case.MeshStem, *Case.AnimSetModel);
			}
			else if (Case.bResolvedClip)
			{
				UE_LOG(LogElysiumGreenRoom, Warning,
					TEXT("%s: failed to resolve clip %s on %s"),
					*Case.Label, *ClipName, *Case.MeshStem);
			}
			else
			{
				UE_LOG(LogElysiumGreenRoom, Warning,
					TEXT("%s: failed to bind cinematic %s/%s %s"),
					*Case.Label, *Case.MeshStem, *Case.AnimSetModel, *ClipName);
			}
			Body->DestroyComponent();
			bAnyFailure = true;
			continue;
		}
		Bodies.Add({ Case, Body, Duration });
		if (Case.bAnimatedProp)
		{
			UE_LOG(LogElysiumGreenRoom, Log, TEXT("%s: %s clip %s loop=%d (%.3fs)"),
				*Case.Label, *Case.MeshStem, *Case.AnimSetModel, Case.bLoop ? 1 : 0, Duration);
		}
		else if (Case.bResolvedClip)
		{
			UE_LOG(LogElysiumGreenRoom, Log, TEXT("%s: %s clip %s (%.3fs)"),
				*Case.Label, *Case.MeshStem, *ClipName, Duration);
		}
		else
		{
			UE_LOG(LogElysiumGreenRoom, Log, TEXT("%s: %s <- %s/%s %s (%.3fs)"),
				*Case.Label, *Case.MeshStem, *Case.AnimSetModel, *Case.BoneRoot,
				*ClipName, Duration);
		}
	}
	return Bodies.Num() == End - Begin;
}

void FElysiumGreenRoomRun::SeekPose()
{
	AElysiumMapActor* Map = GetMap();
	if (!Map || !Fractions.IsValidIndex(FractionIndex))
	{
		return;
	}
	const float SceneTime = bTheatreCamera
		? CurrentSceneTime()
		: Fractions[FractionIndex];
	for (FBodyEntry& Entry : Bodies)
	{
		if (USkeletalMeshComponent* Body = Entry.Body.Get())
		{
			Body->SetWorldLocationAndRotation(BodyOrigin(),
				BodyRotation(Entry.Case.bAnimatedProp));
			const float ClipTime = bTheatreCamera
				? FMath::Min(Entry.Duration, SceneTime)
				: Entry.Duration * SceneTime;
			Map->SeekCinematicClip(Body, ClipTime);
		}
	}
	FrameInPhase = 0;
}

bool FElysiumGreenRoomRun::PrepareFrame()
{
	CurrentMetrics.Reset();
	CurrentCamera = FCameraMetric();
	if (bTheatreCamera)
	{
		PublishTheatreCamera();
		if (!CurrentCamera.bValid)
		{
			return false;
		}
	}
	FBox Combined(ForceInit);
	const FVector BaseOrigin = BodyOrigin();
	const FVector IndividualCenter = BaseOrigin + FVector(0.0f, 0.0f, 105.0f);

	for (FBodyEntry& Entry : Bodies)
	{
		USkeletalMeshComponent* Body = Entry.Body.Get();
		if (!Body || !Body->GetSkeletalMeshAsset())
		{
			bAnyFailure = true;
			continue;
		}
		// Per entry, not once for the pass: the measurements below are un-rotated back into the
		// scene's own frame, and a prop body and a character body do not stand on the same basis.
		const FRotator BaseRotation = BodyRotation(Entry.Case.bAnimatedProp);
		Body->RefreshBoneTransforms();
		Body->UpdateBounds();

		FBodyMetric Metric;
		Metric.Label = Entry.Case.Label;
		Metric.MeshStem = Entry.Case.MeshStem;
		Metric.TimeSeconds = bTheatreCamera
			? FMath::Min(Entry.Duration, CurrentSceneTime())
			: Entry.Duration * Fractions[FractionIndex];
		Metric.AuthoredBoundsCenter = Body->Bounds.Origin - BaseOrigin;
		Metric.BoundsExtent = Body->Bounds.BoxExtent;
		const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
		if (Ref.GetNum() > 0)
		{
			const FTransform RootWorld = Body->GetSocketTransform(Ref.GetBoneName(0), RTS_World);
			Metric.AuthoredRoot = RootWorld.GetLocation() - BaseOrigin;
			Metric.AuthoredRootRotation = RootWorld.Rotator();
			const FName HeadName(TEXT("Bip01 Head"));
			if (Ref.FindBoneIndex(HeadName) != INDEX_NONE)
			{
				const FTransform HeadWorld = Body->GetSocketTransform(HeadName, RTS_World);
				Metric.AuthoredHead = BaseRotation.UnrotateVector(
					HeadWorld.GetLocation() - BaseOrigin);
				// VtMB's eye attachments use the head bone's local +Z as their forward axis. Record
				// the final runtime pose after glTF import and component placement, not a decoder
				// estimate, so a basis error at either layer is observable.
				Metric.AuthoredHeadForward = BaseRotation.UnrotateVector(
					HeadWorld.GetUnitAxis(EAxis::Z)).GetSafeNormal();
			}
		}
		if (bTheatreCamera)
		{
			static const FName KeyBoneNames[] =
			{
				TEXT("Bip01 Pelvis"), TEXT("Bip01 Spine2"), TEXT("Bip01 Neck"),
				TEXT("Bip01 Head"), TEXT("Bip01 L Hand"), TEXT("Bip01 R Hand"),
				TEXT("Bip01 L Foot"), TEXT("Bip01 R Foot")
			};
			const float TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(
				FMath::Clamp(CurrentCamera.FieldOfView, 1.0f, 179.0f) * 0.5f));
			const float TanHalfVertical = TanHalfHorizontal / (16.0f / 9.0f);
			for (const FName BoneName : KeyBoneNames)
			{
				const int32 BoneIndex = Ref.FindBoneIndex(BoneName);
				if (BoneIndex == INDEX_NONE)
				{
					UE_LOG(LogElysiumGreenRoom, Warning, TEXT("%s/%s is missing key bone %s"),
						*CurrentLabel(), *Entry.Case.Label, *BoneName.ToString());
					bAnyFailure = true;
					continue;
				}
				FBoneMetric Bone;
				Bone.Name = BoneName.ToString();
				const FVector WorldLocation = Body->GetBoneLocation(BoneName);
				Bone.AuthoredLocation = BaseRotation.UnrotateVector(WorldLocation - BaseOrigin);
				const FVector View = CurrentCamera.Rotation.UnrotateVector(
					WorldLocation - CurrentCamera.Position);
				Bone.ViewDepth = View.X;
				if (View.X > KINDA_SMALL_NUMBER)
				{
					Bone.NormalizedView.X = View.Y / (View.X * TanHalfHorizontal);
					Bone.NormalizedView.Y = View.Z / (View.X * TanHalfVertical);
					Bone.bInFrame = FMath::Abs(Bone.NormalizedView.X) <= 1.0f
						&& FMath::Abs(Bone.NormalizedView.Y) <= 1.0f;
				}
				Metric.VisibleKeyBones += Bone.bInFrame ? 1 : 0;
				Metric.KeyBones.Add(MoveTemp(Bone));
			}
		}
		if (!IsFiniteVector(Metric.AuthoredRoot)
			|| !IsFiniteVector(Metric.AuthoredBoundsCenter)
			|| !IsFiniteVector(Metric.BoundsExtent)
			|| Metric.BoundsExtent.SizeSquared() <= KINDA_SMALL_NUMBER
			|| Metric.BoundsExtent.GetAbsMax() > 2000.0f
			|| Metric.AuthoredRoot.GetAbsMax() > 5000.0f)
		{
			UE_LOG(LogElysiumGreenRoom, Warning,
				TEXT("%s/%s has invalid or exploded geometry at %.3fs: root=%s center=%s extent=%s"),
				*CurrentLabel(), *Entry.Case.Label, Metric.TimeSeconds,
				*Metric.AuthoredRoot.ToCompactString(), *Metric.AuthoredBoundsCenter.ToCompactString(),
				*Metric.BoundsExtent.ToCompactString());
			bAnyFailure = true;
		}
		if (bTheatreCamera)
		{
			Metric.AuthoredBoundsCenter = BaseRotation.UnrotateVector(Metric.AuthoredBoundsCenter);
			Metric.AuthoredRoot = BaseRotation.UnrotateVector(Metric.AuthoredRoot);
			Metric.AuthoredRootRotation = (
				BaseRotation.Quaternion().Inverse() * Metric.AuthoredRootRotation.Quaternion()).Rotator();
		}

		if (!bEnsemble)
		{
			Metric.CenteringOffset = IndividualCenter - Body->Bounds.Origin;
			Body->AddWorldOffset(Metric.CenteringOffset);
			Body->UpdateBounds();
		}
		Combined += Body->Bounds.GetBox();
		CurrentMetrics.Add(Metric);
	}
	if (bCourtroom)
	{
		FBodyMetric* Audience = CurrentMetrics.FindByPredicate([](const FBodyMetric& Metric)
		{
			return Metric.Label == TEXT("vampire4_seated");
		});
		const FBodyMetric* Prince = CurrentMetrics.FindByPredicate([](const FBodyMetric& Metric)
		{
			return Metric.Label == TEXT("prince");
		});
		if (Audience && Prince)
		{
			FVector ToFocus = Prince->AuthoredHead - Audience->AuthoredHead;
			ToFocus.Z = 0.0f;
			FVector Forward = Audience->AuthoredHeadForward;
			Forward.Z = 0.0f;
			if (ToFocus.Normalize() && Forward.Normalize())
			{
				Audience->FacingDotToFocus = FVector::DotProduct(Forward, ToFocus);
				Audience->bHasFacingFocus = true;
				UE_LOG(LogElysiumGreenRoom, Log,
					TEXT("courtroom %.3fs: Vampire4 head-forward=%s to-Prince=%s dot=%.3f"),
					Audience->TimeSeconds, *Forward.ToCompactString(),
					*ToFocus.ToCompactString(), Audience->FacingDotToFocus);
			}
		}
	}
	if (!Combined.IsValid)
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("%s: no valid rendered bounds"), *CurrentLabel());
		bAnyFailure = true;
		return false;
	}
	if (!bTheatreCamera)
	{
		UpdateStage(Combined);
		PublishCamera(Combined);
	}
	return !bAnyFailure;
}

void FElysiumGreenRoomRun::UpdateStage(const FBox& Bounds)
{
	const FVector Center = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();
	if (UStaticMeshComponent* WallComp = Wall.Get())
	{
		const float WallSide = bCourtroom ? 1.0f : -1.0f;
		WallComp->SetWorldLocation(Center + FVector(WallSide * (Extent.X + 180.0f), 0.0f, 0.0f));
		WallComp->SetWorldScale3D(FVector(0.1f,
			FMath::Max(4.0f, (Extent.Y + 220.0f) / 50.0f),
			FMath::Max(4.0f, (Extent.Z + 180.0f) / 50.0f)));
	}
	if (UStaticMeshComponent* FloorComp = Floor.Get())
	{
		FloorComp->SetWorldLocation(FVector(Center.X, Center.Y, Bounds.Min.Z - 8.0f));
		FloorComp->SetWorldScale3D(FVector(
			FMath::Max(6.0f, (Extent.X + 450.0f) / 50.0f),
			FMath::Max(6.0f, (Extent.Y + 250.0f) / 50.0f), 0.1f));
	}
	if (UPointLightComponent* Light = KeyLight.Get())
	{
		Light->SetWorldLocation(Center + FVector(260.0f, -220.0f, 320.0f));
	}
	if (UPointLightComponent* Light = FillLight.Get())
	{
		Light->SetWorldLocation(Center + (bReview
			? FVector(-260.0f, 220.0f, 220.0f)
			: FVector(80.0f, 300.0f, 120.0f)));
	}
}

FBox FElysiumGreenRoomRun::EmptyStageBounds() const
{
	const FVector Centre = StageOrigin + FVector(0.0f, 0.0f, 90.0f);
	return FBox(Centre - FVector(40.0f, 40.0f, 90.0f), Centre + FVector(40.0f, 40.0f, 90.0f));
}

void FElysiumGreenRoomRun::FrameLabCamera(const FBox& Bounds)
{
	AElysiumMapActor* Map = GetMap();
	if (!Map || !Bounds.IsValid)
	{
		return;
	}
	const FVector Extent = Bounds.GetExtent();
	const FVector Focus(Bounds.GetCenter().X, Bounds.GetCenter().Y,
		FMath::Lerp(Bounds.Min.Z, Bounds.Max.Z, FMath::Clamp(LabViewState.LookHeight, 0.0f, 1.0f)));
	// The same fit the capture path uses, then scaled: the orbit starts framed the way a contact
	// sheet would have framed it, so the two are comparable by eye.
	const float Fit = FMath::Max(220.0f, Extent.Z / 0.34f + Extent.X + 50.0f);
	const float Distance = Fit * FMath::Clamp(LabViewState.DistanceScale, 0.15f, 6.0f);
	const FVector Offset = FRotator(FMath::Clamp(LabViewState.OrbitPitch, -85.0f, 85.0f),
		LabViewState.OrbitYaw, 0.0f).RotateVector(FVector(Distance, 0.0f, 0.0f));
	CameraLocation = Focus + Offset;
	CameraRotation = (Focus - CameraLocation).Rotation();

	FElysiumCameraShot Shot;
	Shot.Origin = CameraLocation;
	Shot.LookAt = Focus;
	Shot.bUseLookAt = true;
	Shot.FieldOfView = 60.0f;
	Shot.BlendSeconds = 0.0f;
	Shot.MaxTurnRate = FVector::ZeroVector;
	Shot.DebugName = TEXT("green_room_lab");
	if (CameraShotId == 0)
	{
		CameraShotId = Map->PushCameraShotValue(Shot);
	}
	else
	{
		Map->UpdateCameraShotValue(CameraShotId, Shot);
	}
}

void FElysiumGreenRoomRun::PublishCamera(const FBox& Bounds)
{
	AElysiumMapActor* Map = GetMap();
	if (!Map)
	{
		return;
	}
	const FVector Center = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();
	const bool bSmallProp = ActiveCases.IsValidIndex(CaseIndex)
		&& ActiveCases[CaseIndex].bAnimatedProp;
	const float Distance = bReview
		? FMath::Max(220.0f, Extent.Z / 0.34f + Extent.X + 50.0f)
		: FMath::Max(bSmallProp ? 25.0f : 280.0f,
			FMath::Max(Extent.Y / 0.50f, Extent.Z / 0.28f) + Extent.X
				+ (bSmallProp ? 12.0f : 100.0f));
	// Courtroom_bip5 places its seated audience on the positive-X side looking toward LaCroix.
	// Put the oracle camera on LaCroix's side so the expected result is Vampire4's face, not the
	// back view produced by the generic +X green-room camera.
	const float ReviewYaw = bReview && ReviewViewYaws.IsValidIndex(ViewIndex)
		? ReviewViewYaws[ViewIndex] : 0.0f;
	const FVector CameraOffset = FRotator(0.0f, ReviewYaw, 0.0f)
		.RotateVector(FVector(bCourtroom ? -Distance : Distance, 0.0f, 0.0f));
	CameraLocation = Center + CameraOffset;
	CameraRotation = (Center - CameraLocation).Rotation();

	FElysiumCameraShot Shot;
	Shot.Origin = CameraLocation;
	Shot.LookAt = Center;
	Shot.bUseLookAt = true;
	Shot.FieldOfView = 60.0f;
	Shot.BlendSeconds = 0.0f;
	Shot.MaxTurnRate = FVector::ZeroVector;
	Shot.DebugName = TEXT("green_room");
	if (CameraShotId == 0)
	{
		CameraShotId = Map->PushCameraShotValue(Shot);
	}
	else
	{
		Map->UpdateCameraShotValue(CameraShotId, Shot);
	}
}

void FElysiumGreenRoomRun::PublishTheatreCamera()
{
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (!World || !TheatrePositionPath || !TheatreTargetPath)
	{
		bAnyFailure = true;
		return;
	}
	ElysiumCameraTrack::FSample Position;
	ElysiumCameraTrack::FSample Target;
	const float SceneTime = CurrentSceneTime();
	if (!TheatrePositionPath->Sample(SceneTime, Position)
		|| !TheatreTargetPath->Sample(SceneTime, Target))
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("embrace camera sample failed at %.3fs"), SceneTime);
		bAnyFailure = true;
		return;
	}

	// Mirror the authored output order, then let the position publish compose the complete value
	// shot. This is the production world seam, including independent stream ownership and cuts.
	World->PublishTrackCamera(true, TheatreTargetOwner, Target.Position, Target.Rotation,
		Target.Roll, Target.FieldOfView, 0.0f);
	World->PublishTrackCamera(false, TheatrePositionOwner, Position.Position, Position.Rotation,
		Position.Roll, Position.FieldOfView, 0.0f);

	CurrentCamera.bValid = true;
	CurrentCamera.SceneTime = SceneTime;
	CurrentCamera.PositionSegment = Position.Segment;
	CurrentCamera.TargetSegment = Target.Segment;
	CurrentCamera.Position = Position.Position;
	CurrentCamera.Target = Target.Position;
	CurrentCamera.Rotation = (Target.Position - Position.Position).Rotation();
	CurrentCamera.Rotation.Roll = Position.Roll;
	CurrentCamera.Roll = Position.Roll;
	CurrentCamera.FieldOfView = Position.FieldOfView;
	CurrentCamera.FadeAlpha = TheatreFadeAlpha(SceneTime);
	CameraLocation = Position.Position;
	CameraRotation = CurrentCamera.Rotation;
}

float FElysiumGreenRoomRun::CurrentSceneTime() const
{
	return bTheatreCamera && Fractions.IsValidIndex(FractionIndex)
		? Fractions[FractionIndex] * TheatreDuration
		: 0.0f;
}

float FElysiumGreenRoomRun::TheatreFadeAlpha(float SceneTime) const
{
	float Alpha = 0.0f;
	for (const FFadeWindow& Window : TheatreFades)
	{
		const float Local = SceneTime - Window.StartTime;
		if (Local < 0.0f)
		{
			continue;
		}
		float WindowAlpha = Window.Opacity;
		if (Window.Duration > KINDA_SMALL_NUMBER && Local < Window.Duration)
		{
			WindowAlpha *= Local / Window.Duration;
		}
		else if (Local <= Window.Duration + Window.HoldTime)
		{
			WindowAlpha = Window.Opacity;
		}
		else if (Window.bAutoReverse
			&& Local < 2.0f * Window.Duration + Window.HoldTime)
		{
			WindowAlpha *= 1.0f - (Local - Window.Duration - Window.HoldTime)
				/ FMath::Max(Window.Duration, KINDA_SMALL_NUMBER);
		}
		else
		{
			WindowAlpha = Window.bAutoReverse ? 0.0f : Window.Opacity;
		}
		Alpha = FMath::Max(Alpha, FMath::Clamp(WindowAlpha, 0.0f, 1.0f));
	}
	return Alpha;
}

FVector FElysiumGreenRoomRun::BodyOrigin() const
{
	return bTheatreCamera ? TheatreSceneOrigin : StageOrigin;
}

FRotator FElysiumGreenRoomRun::BodyRotation(bool bAnimatedProp) const
{
	if (!bTheatreCamera)
	{
		return FRotator::ZeroRotator;   // the stage stands its cast on the world axes
	}
	// One anchor, two bases: the scene's understudies come off the baked mount and the props are
	// loaded through glTFRuntime, so the same authored yaw resolves differently for each.
	return bAnimatedProp
		? ElysiumSkeletalBasis::GlbFromSourceAngles(TheatreSceneAngles)
		: ElysiumSkeletalBasis::FromSourceAngles(TheatreSceneAngles);
}

void FElysiumGreenRoomRun::PinCameraAndPlayerSurface()
{
	// **Drive mode refuses this structurally, not merely by not calling it.** Both pins fight the
	// shipping path: the control rotation is where the mouse look lands, and the model alpha is
	// solved by the camera manager from the fade band every frame. Pinning either would mean the
	// acceptance was watching the lab rather than the game.
	if (IsDriving())
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn)
	{
		if (IElysiumPlayerBody* PlayerBody = Cast<IElysiumPlayerBody>(Pawn))
		{
			PlayerBody->ApplyPlayerModelAlpha(1.0f);
		}
	}
	if (PC)
	{
		PC->SetControlRotation(CameraRotation);
		if (bTheatreCamera && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetManualCameraFade(
				CurrentCamera.FadeAlpha, FLinearColor::Black, false);
		}
	}
}

FString FElysiumGreenRoomRun::CurrentLabel() const
{
	if (bTheatreCamera)
	{
		return TEXT("embrace");
	}
	if (bEnsemble)
	{
		return Selector;
	}
	return ActiveCases.IsValidIndex(CaseIndex) ? ActiveCases[CaseIndex].Label : Selector;
}

FString FElysiumGreenRoomRun::OutputDirectory() const
{
	return FElysiumContentPaths::Root() / TEXT("_greenroom")
		/ (bReview ? TEXT("review") : Selector);
}

void FElysiumGreenRoomRun::BeginCapture()
{
	const float Fraction = Fractions[FractionIndex];
	const FString Label = CurrentLabel();
	const float ViewYaw = bReview && ReviewViewYaws.IsValidIndex(ViewIndex)
		? ReviewViewYaws[ViewIndex] : 0.0f;
	const int32 TimeCode = bTheatreCamera
		? FMath::RoundToInt(CurrentSceneTime() * 100.0f)
		: FMath::RoundToInt(Fraction * 100.0f);
	const FString Path = OutputDirectory() / (bTheatreCamera
		? FString::Printf(TEXT("%s_s%05d.png"), *Label, TimeCode)
		: bReview
			? FString::Printf(TEXT("review_t%03d_v%03d.png"), TimeCode,
				FMath::RoundToInt(ViewYaw))
			: FString::Printf(TEXT("%s_t%03d.png"), *Label, TimeCode));
	const TArray<FBodyMetric> Metrics = CurrentMetrics;
	const FCameraMetric Camera = CurrentCamera;
	bAwaitingCapture = true;

	const bool bRequested = ElysiumScreenshot::Request(
		[this, Path, Label, Fraction, ViewYaw, Metrics, Camera](
			int32 Width, int32 Height, const TArray<FColor>& Bitmap)
		{
			FShot Shot;
			Shot.Label = Label;
			Shot.File = Path;
			Shot.Fraction = Fraction;
			Shot.ViewYaw = ViewYaw;
			Shot.Width = Width;
			Shot.Height = Height;
			Shot.Bodies = Metrics;
			Shot.Camera = Camera;
			Shot.bOk = Width > 0 && Height > 0
				&& ElysiumScreenshot::SavePng(Width, Height, Bitmap, Path);
			Shots.Add(MoveTemp(Shot));
			bAnyFailure |= !Shots.Last().bOk;
			if (Shots.Last().bOk)
			{
				UE_LOG(LogElysiumGreenRoom, Log, TEXT("%s %3.0f%%: captured (%dx%d)"),
					*Label, Fraction * 100.0f, Width, Height);
			}
			else
			{
				UE_LOG(LogElysiumGreenRoom, Warning, TEXT("%s %3.0f%%: capture FAILED (%dx%d)"),
					*Label, Fraction * 100.0f, Width, Height);
			}
			bAwaitingCapture = false;
		});
	if (!bRequested)
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("no viewport available for green-room capture"));
		bAnyFailure = true;
		bAwaitingCapture = false;
	}
}

void FElysiumGreenRoomRun::Advance()
{
	if (bReview && ++ViewIndex < ReviewViewYaws.Num())
	{
		FrameInPhase = 0;
		Phase = EPhase::PoseWarmup;
		return;
	}
	ViewIndex = 0;
	if (++FractionIndex < Fractions.Num())
	{
		SeekPose();
		Phase = EPhase::PoseWarmup;
		return;
	}
	FractionIndex = 0;
	if (!bEnsemble && ++CaseIndex < ActiveCases.Num())
	{
		if (!BuildBodies())
		{
			bAnyFailure = true;
		}
		SeekPose();
		Phase = EPhase::PoseWarmup;
		return;
	}
	Finish();
	Phase = EPhase::Done;
}

void FElysiumGreenRoomRun::Finish()
{
	if (bTheatreCamera)
	{
		if (AElysiumMapActor* Map = GetMap())
		{
			if (FElysiumEntityWorld* World = Map->GetEntityWorld())
			{
				World->RestoreTrackCamera(false, TheatrePositionOwner, 0.0f);
				World->RestoreTrackCamera(true, TheatreTargetOwner, 0.0f);
			}
		}
	}
	if (bTheatreCamera)
	{
		for (const FCase& Case : ActiveCases)
		{
			const bool bFramed = Shots.ContainsByPredicate([&Case](const FShot& Shot)
			{
				return Shot.bOk && Shot.Camera.bValid && Shot.Camera.FadeAlpha < 0.8f
					&& Shot.Bodies.ContainsByPredicate([&Case](const FBodyMetric& Body)
					{
						return Body.Label == Case.Label && Body.VisibleKeyBones > 0;
					});
			});
			if (!bFramed)
			{
				UE_LOG(LogElysiumGreenRoom, Warning,
					TEXT("embrace framing never shows %s outside an opaque fade"), *Case.Label);
				bAnyFailure = true;
			}
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, false);
			}
		}
	}
	IFileManager::Get().MakeDirectory(*OutputDirectory(), true);
	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"selector\": \"%s\",\n"), *Selector);
	if (bReview)
	{
		Json += FString::Printf(TEXT("  \"review_stem\": \"%s\",\n"), *ReviewStem);
		Json += FString::Printf(TEXT("  \"review_clip\": \"%s\",\n"), *ReviewClip);
		Json += FString::Printf(TEXT("  \"review_anim_set\": \"%s\",\n"), *ReviewAnimSet);
		Json += FString::Printf(TEXT("  \"review_bone_root\": \"%s\",\n"), *ReviewBoneRoot);
	}
	Json += FString::Printf(TEXT("  \"ensemble\": %s,\n"), bEnsemble ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"ok\": %s,\n"), bAnyFailure ? TEXT("false") : TEXT("true"));
	Json += TEXT("  \"shots\": [\n");
	for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
	{
		const FShot& Shot = Shots[ShotIndex];
		Json += TEXT("    {\n");
		Json += FString::Printf(
			TEXT("      \"label\": \"%s\", \"fraction\": %.3f, \"view_yaw\": %.1f,\n"),
			*Shot.Label, Shot.Fraction, Shot.ViewYaw);
		Json += FString::Printf(TEXT("      \"file\": \"%s\", \"width\": %d, \"height\": %d, \"ok\": %s,\n"),
			*FPaths::GetCleanFilename(Shot.File), Shot.Width, Shot.Height, Shot.bOk ? TEXT("true") : TEXT("false"));
		Json += TEXT("      \"bodies\": [\n");
		for (int32 BodyIndex = 0; BodyIndex < Shot.Bodies.Num(); ++BodyIndex)
		{
			const FBodyMetric& Body = Shot.Bodies[BodyIndex];
			Json += TEXT("        {");
			Json += FString::Printf(TEXT("\"label\": \"%s\", \"stem\": \"%s\", \"time\": %.3f, "),
				*Body.Label, *Body.MeshStem, Body.TimeSeconds);
			Json += FString::Printf(TEXT("\"authored_root\": %s, \"authored_bounds_center\": %s, "),
				*VecJson(Body.AuthoredRoot), *VecJson(Body.AuthoredBoundsCenter));
			Json += FString::Printf(
				TEXT("\"authored_root_rotation\": %s, \"authored_head\": %s, ")
				TEXT("\"authored_head_forward\": %s, "),
				*RotJson(Body.AuthoredRootRotation), *VecJson(Body.AuthoredHead),
				*VecJson(Body.AuthoredHeadForward));
			if (Body.bHasFacingFocus)
			{
				Json += FString::Printf(TEXT("\"facing_dot_to_focus\": %.3f, "),
					Body.FacingDotToFocus);
			}
			Json += FString::Printf(TEXT("\"bounds_extent\": %s, \"centering_offset\": %s, "),
				*VecJson(Body.BoundsExtent), *VecJson(Body.CenteringOffset));
			Json += FString::Printf(TEXT("\"visible_key_bones\": %d, \"key_bones\": ["),
				Body.VisibleKeyBones);
			for (int32 BoneIndex = 0; BoneIndex < Body.KeyBones.Num(); ++BoneIndex)
			{
				const FBoneMetric& Bone = Body.KeyBones[BoneIndex];
				Json += FString::Printf(
					TEXT("{\"name\": \"%s\", \"authored_location\": %s, ")
					TEXT("\"view_depth\": %.3f, \"normalized_view\": %s, \"in_frame\": %s}"),
					*Bone.Name, *VecJson(Bone.AuthoredLocation), Bone.ViewDepth,
					*Vec2Json(Bone.NormalizedView), Bone.bInFrame ? TEXT("true") : TEXT("false"));
				Json += BoneIndex + 1 < Body.KeyBones.Num() ? TEXT(", ") : TEXT("");
			}
			Json += TEXT("]}");
			Json += BodyIndex + 1 < Shot.Bodies.Num() ? TEXT(",\n") : TEXT("\n");
		}
		if (Shot.Camera.bValid)
		{
			Json += TEXT("      ],\n");
			Json += FString::Printf(
				TEXT("      \"camera\": {\"scene_time\": %.3f, ")
				TEXT("\"position_segment\": %d, \"target_segment\": %d, ")
				TEXT("\"position\": %s, \"target\": %s, ")
				TEXT("\"rotation\": %s, \"roll\": %.3f, \"fov\": %.3f, ")
				TEXT("\"fade_alpha\": %.3f}\n"),
				Shot.Camera.SceneTime, Shot.Camera.PositionSegment, Shot.Camera.TargetSegment,
				*VecJson(Shot.Camera.Position), *VecJson(Shot.Camera.Target),
				*RotJson(Shot.Camera.Rotation), Shot.Camera.Roll, Shot.Camera.FieldOfView,
				Shot.Camera.FadeAlpha);
		}
		else
		{
			Json += TEXT("      ]\n");
		}
		Json += ShotIndex + 1 < Shots.Num() ? TEXT("    },\n") : TEXT("    }\n");
	}
	Json += TEXT("  ]\n");
	Json += TEXT("}\n");
	const FString Manifest = OutputDirectory() / TEXT("manifest.json");
	if (!FFileHelper::SaveStringToFile(Json, *Manifest))
	{
		bAnyFailure = true;
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("failed to write %s"), *Manifest);
	}
	else
	{
		UE_LOG(LogElysiumGreenRoom, Log, TEXT("wrote %s (%d shots, ok=%s)"),
			*Manifest, Shots.Num(), bAnyFailure ? TEXT("false") : TEXT("true"));
	}
	if (bLive)
	{
		// Holding the window open is not enough on its own. Capturing pins each body with
		// `SeekCinematicClip`, which is an absolute-time scrub at zero play rate — so a body left
		// as the captures found it stands frozen at the last fraction. Re-issue an ordinary looping
		// play so the clip actually runs, which is the entire point of watching it.
		AElysiumMapActor* Map = GetMap();
		int32 Playing = 0;
		for (FBodyEntry& Entry : Bodies)
		{
			USkeletalMeshComponent* Body = Entry.Body.Get();
			if (!Map || !Body)
			{
				continue;
			}
			float Duration = 0.0f;
			const FString ClipName = Entry.Case.ClipName.IsEmpty()
				? TEXT("entire_scene") : Entry.Case.ClipName;
			const bool bPlayed = Entry.Case.bAnimatedProp
				? Map->PlayAnimatedPropClip(Body, Entry.Case.MeshStem, Entry.Case.AnimSetModel,
					/*bLoop=*/true, &Duration)
				: Entry.Case.bResolvedClip
					? Map->PlayNpcClip(Body, Entry.Case.MeshStem, ClipName,
						/*bLoop=*/true, &Duration)
					: Map->PlayCinematicClip(Body, Entry.Case.MeshStem, Entry.Case.AnimSetModel,
						Entry.Case.BoneRoot, ClipName, /*bLoop=*/true, &Duration);
			Playing += bPlayed ? 1 : 0;
		}
		UE_LOG(LogElysiumGreenRoom, Log,
			TEXT("live: captures written, %d/%d bodies looping — close the window when done"),
			Playing, Bodies.Num());
		return;
	}
	FPlatformMisc::RequestExitWithStatus(false, bAnyFailure ? 1 : 0);
}

USkeletalMeshComponent* FElysiumGreenRoomRun::LabBody() const
{
	// While driving, the body on the stage IS the player's — so the cloth tab, the source readout and
	// the overlays all follow it without knowing which mode they are in.
	if (IsDriving())
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const IElysiumPlayerBody* Body = PC ? Cast<IElysiumPlayerBody>(PC->GetPawn()) : nullptr;
		return Body ? Body->GetPlayerVisual() : nullptr;
	}
	return Bodies.IsEmpty() ? nullptr : Bodies[0].Body.Get();
}

float FElysiumGreenRoomRun::LabDuration() const
{
	return Bodies.IsEmpty() ? 0.0f : Bodies[0].Duration;
}

void FElysiumGreenRoomRun::LabSetTime(float Seconds)
{
	const float Duration = LabDuration();
	LabClipTime = Duration > KINDA_SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.0f, Seconds), Duration) : 0.0f;
}

bool FElysiumGreenRoomRun::LabSetBody(const FString& Stem, const FString& Clip, FString& OutError)
{
	AElysiumMapActor* Map = GetMap();
	if (!IsLabReady() || !Map)
	{
		OutError = TEXT("the green room stage is not ready yet");
		return false;
	}
	if (Stem.IsEmpty())
	{
		OutError = TEXT("no model named");
		return false;
	}

	// While driving, "stand this model up" means "put it on the pawn". One door for both modes, so
	// the model list keeps working and no clip is named — the graph picks that.
	if (IsDriving())
	{
		DestroyBodies();
		return LabSetDriveBody(Stem, OutError);
	}

	// Resolve the clip before anything is destroyed, so a typo costs nothing: the body that is
	// already standing there stays standing.
	FString ResolvedClip = Clip;
	const UGameInstance* GI = Subsystem.IsValid() ? Subsystem->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (ResolvedClip.IsEmpty())
	{
		// An empty clip is a complete request: ask the model's own idle policy, the same chain a
		// map's NPCs resolve through, and report back which clip that was.
		EElysiumIdleTier Tier = EElysiumIdleTier::None;
		if (Anims != nullptr)
		{
			ResolvedClip = Anims->PickIdleClip(Stem, TEXT("Neutral"), Tier);
		}
		if (ResolvedClip.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s resolves no idle clip — name one"), *Stem);
			return false;
		}
	}

	DestroyBodies();
	USkeletalMeshComponent* Body = Map->BuildNpcVisual(Stem, BodyOrigin(),
		BodyRotation(/*bAnimatedProp=*/false), 1.0f, TEXT("Neutral"), 0);
	if (!Body)
	{
		OutError = FString::Printf(TEXT("could not build %s — is npc/%s.glb exported?"),
			*Stem, *Stem);
		return false;
	}
	Body->SetWorldLocationAndRotation(BodyOrigin(), BodyRotation(/*bAnimatedProp=*/false));
	Body->SetVisibility(true, true);
	Body->SetComponentTickEnabled(true);

	float Duration = 0.0f;
	if (!Map->PlayNpcClip(Body, Stem, ResolvedClip, /*bLoop=*/true, &Duration))
	{
		Body->DestroyComponent();
		OutError = FString::Printf(TEXT("%s has no clip '%s'"), *Stem, *ResolvedClip);
		return false;
	}

	FCase Case;
	Case.Label = Stem;
	Case.MeshStem = Stem;
	Case.ClipName = ResolvedClip;
	Case.bResolvedClip = true;
	Case.bLoop = true;
	Bodies.Add({ Case, Body, Duration });
	ReviewStem = Stem;
	ReviewClip = ResolvedClip;
	// A layer belongs to the body it was composed onto, and that body has just been destroyed. So
	// does a grid, and it is additionally what the clip above just replaced.
	ReviewLayer.Reset();
	ReviewLayers.Reset();
	ReviewGrid = FElysiumResolvedGrid();
	LabClipTime = 0.0f;
	UE_LOG(LogElysiumGreenRoom, Log, TEXT("lab: %s clip %s (%.3fs)"), *Stem, *ResolvedClip, Duration);
	return true;
}

// --- drive mode (CCC6) ---------------------------------------------------------------------------

namespace
{
	// Everything drive mode needs off the possessed body, resolved together so a half-built session
	// is one refusal rather than five null checks scattered through the mode change.
	struct FDriveRefs
	{
		APlayerController* PC = nullptr;
		APawn* Pawn = nullptr;
		IElysiumPlayerBody* Body = nullptr;
		UElysiumMovementComponent* Move = nullptr;

		explicit operator bool() const { return PC && Pawn && Body && Move; }
	};

	FDriveRefs ResolveDriveBody(UWorld* World)
	{
		FDriveRefs Refs;
		Refs.PC = World ? World->GetFirstPlayerController() : nullptr;
		Refs.Pawn = Refs.PC ? Refs.PC->GetPawn() : nullptr;
		Refs.Body = Cast<IElysiumPlayerBody>(Refs.Pawn);
		// The faithful mover specifically: the gym's brackets are its constants, so a body without
		// one cannot supply the tuning the gym is built from.
		Refs.Move = Refs.Pawn ? Refs.Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr;
		return Refs;
	}
}

bool FElysiumGreenRoomRun::BuildDriveGym(FString& OutError)
{
	DestroyDriveGym();
	UWorld* World = GetWorld();
	const FDriveRefs Refs = ResolveDriveBody(World);
	if (!World || !Refs)
	{
		OutError = TEXT("no player body to build a floor for");
		return false;
	}

	// Built from the mover's live tuning, never from a cached spec: the geometry IS the constants,
	// so a gym standing under a body whose tuning has since moved is measuring nothing.
	GymSpec = ElysiumGym::Build(Refs.Move->GetTuning());
#if !UE_BUILD_SHIPPING
	GymActor = ElysiumGym::Spawn(World, GymSpec, ElysiumGym::DefaultOrigin(), bGymMeshes);
#endif
	if (!GymActor.IsValid())
	{
		GymSpec = ElysiumGym::FSpec();
		OutError = TEXT("could not stand the gym up");
		return false;
	}
	UE_LOG(LogElysiumGreenRoom, Log, TEXT("drive: gym standing — %d lanes, %d solids%s"),
		GymSpec.Lanes.Num(), GymSpec.Placements.Num(), bGymMeshes ? TEXT("") : TEXT(" (hidden)"));
	return true;
}

void FElysiumGreenRoomRun::DestroyDriveGym()
{
	if (AActor* Actor = GymActor.Get())
	{
		Actor->Destroy();
	}
	GymActor.Reset();
	GymSpec = ElysiumGym::FSpec();
}

bool FElysiumGreenRoomRun::LabSeatOnLane(const FName& Lane, FString& OutError)
{
	if (!IsDriving())
	{
		OutError = TEXT("not driving");
		return false;
	}
	const FDriveRefs Refs = ResolveDriveBody(GetWorld());
	if (!Refs)
	{
		OutError = TEXT("no player body to seat");
		return false;
	}
	const ElysiumGym::FLane* Found = GymSpec.FindLane(Lane);
	if (Found == nullptr)
	{
		OutError = FString::Printf(TEXT("the gym has no lane '%s'"), *Lane.ToString());
		return false;
	}

	// The movement harness's own seating, minus the recorder. `ResetState` first, so the body
	// inherits neither the position nor the *motion* of wherever it was; `SeatOrigin` is the one
	// conversion between the spec's feet and the pawn's centre.
	Refs.Move->ResetState();
	Refs.Pawn->SetActorLocation(
		ElysiumGym::SeatOrigin(ElysiumGym::DefaultOrigin() + Found->FeetOrigin,
			Refs.Body->GetBodyHalfHeight()),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Refs.PC->SetControlRotation(FRotator(0.0f, Found->Yaw, 0.0f));
	// The boom would otherwise ease across the whole teleport, which reads as the camera falling
	// behind a body that did not move.
	if (UElysiumCameraComponent* Camera = Refs.Body->GetCameraComponent())
	{
		Camera->RequestReseed();
	}
	SeatLane = Lane;
	return true;
}

bool FElysiumGreenRoomRun::LabSetDriveBody(const FString& Stem, FString& OutError)
{
	AElysiumMapActor* Map = GetMap();
	if (!Map)
	{
		OutError = TEXT("no map actor");
		return false;
	}
	if (Stem.IsEmpty())
	{
		OutError = TEXT("no model named");
		return false;
	}

	// **The attachment this makes is the rung.** `BuildPlayerVisual` parents the visual to the pawn,
	// offsets it by the hull's half height, applies the facing basis and installs the mover tick
	// prerequisite — and caches the stem, which is the one thing whose absence leaves
	// `TickPlayerAnimation` idle in a stage world. The capture path's detach-and-re-parent is what
	// this rung exists not to do.
	USkeletalMeshComponent* Visual = Map->BuildPlayerVisual(Stem, TEXT("Neutral"), 0);
	if (!Visual)
	{
		OutError = FString::Printf(TEXT("could not build %s — is npc/%s.glb exported?"), *Stem, *Stem);
		return false;
	}
	// The same flag the capture path sets: it means "this run owns the map's player visual", and it
	// is what makes the ordinary teardown clear it.
	bPlayerSurfaceActive = true;
	ReviewStem = Stem;
	UE_LOG(LogElysiumGreenRoom, Log, TEXT("drive: %s standing on the pawn"), *Stem);
	return true;
}

bool FElysiumGreenRoomRun::LabSetGymVisible(bool bVisible, FString& OutError)
{
	bGymMeshes = bVisible;
	if (!IsDriving())
	{
		return true;
	}
	// The solids are spawned with their meshes or without them, so this is a rebuild rather than a
	// visibility write. Seating survives it: the lane is a coordinate, not a reference.
	if (!BuildDriveGym(OutError))
	{
		return false;
	}
	return LabSeatOnLane(SeatLane, OutError);
}

bool FElysiumGreenRoomRun::LabSetMode(ELabMode NewMode, FString& OutError)
{
	AElysiumMapActor* Map = GetMap();
	if (!IsLabReady() || !Map)
	{
		OutError = TEXT("the green room stage is not ready yet");
		return false;
	}
	if (Mode == NewMode)
	{
		return true;
	}

	UWorld* World = GetWorld();
	const FDriveRefs Refs = ResolveDriveBody(World);
	if (!Refs)
	{
		OutError = TEXT("this session has no player body with the faithful mover on it");
		return false;
	}
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	if (NewMode == ELabMode::Drive && Sub && !Sub->IsStageWorld())
	{
		// A real map already has a floor, a spawn point and its own player visual. Driving there is a
		// reasonable thing to want and a different question from this one; refusing says so rather
		// than standing a second floor through the middle of a level.
		OutError = TEXT("drive mode is the stage world's — relaunch without -ElysiumMap");
		return false;
	}

	// Whatever was standing belongs to the mode that is leaving.
	DestroyBodies();

	if (NewMode == ELabMode::Drive)
	{
		// The orbit is a camera shot on the player's own stack, so leaving it pushed would mean the
		// "real camera" the rung is about is still being overridden. Popping it hands the view back
		// to the manager and the rig.
		if (CameraShotId != 0)
		{
			Map->PopCameraShot(CameraShotId);
			CameraShotId = 0;
		}
		// The review stage's floor plate is a collision-free prop at the stage origin. Left visible it
		// intersects nothing and means nothing, but it reads as a surface — which is the one confusion
		// standing on a real floor exists to remove.
		if (UStaticMeshComponent* FloorComp = Floor.Get())
		{
			FloorComp->SetVisibility(false);
		}

		Mode = ELabMode::Drive;
		if (!BuildDriveGym(OutError))
		{
			Mode = ELabMode::Review;
			return false;
		}
		// The stage world froze this body on arrival because it had no floor. It has one now, and
		// releasing the freeze belongs to whoever supplied it.
		Refs.Body->SetMovementFrozen(false);
		if (!LabSeatOnLane(SeatLane, OutError))
		{
			return false;
		}
		// Third person or there is nothing to look at: the fade band takes the model to zero alpha at
		// the first-person eye. The choice is the player's own persistent one, set here rather than
		// pinned per frame, so the operator can still toggle it and watch what that does.
		if (UElysiumCameraComponent* Camera = Refs.Body->GetCameraComponent())
		{
			Camera->SetThirdPerson(true);
			Camera->RequestReseed();
		}
		// A stemless drive is a complete request: the mover, the gym and the camera are worth
		// watching on their own, and inventing a default PC body would be inventing content.
		if (!ReviewStem.IsEmpty())
		{
			FString BodyError;
			if (!LabSetDriveBody(ReviewStem, BodyError))
			{
				UE_LOG(LogElysiumGreenRoom, Warning, TEXT("drive: %s"), *BodyError);
			}
		}
		UE_LOG(LogElysiumGreenRoom, Log,
			TEXT("drive: the shipping path has the body — F1 hands the keyboard to the window"));
		return true;
	}

	// Back to review: put the stage back the way it frames a model, and put the body back where a
	// stage world seats it.
	Mode = ELabMode::Review;
	DestroyDriveGym();
	Refs.Body->SetMovementFrozen(true);
	Refs.Move->ResetState();
	Refs.Pawn->SetActorLocation(FVector(0.0f, 0.0f, 100.0f), /*bSweep=*/false, nullptr,
		ETeleportType::TeleportPhysics);
	if (UStaticMeshComponent* FloorComp = Floor.Get())
	{
		FloorComp->SetVisibility(true);
	}
	// The next lab tick reframes the empty stage and pushes a fresh orbit shot.
	return true;
}

void FElysiumGreenRoomRun::TickDrive(float DeltaSeconds)
{
	ApplyLabHud(LabViewState.bShowHud);

	// The stage's key and fill travel with the body, which is the only reason the stage still exists
	// in this mode: a gym is unlit geometry in an empty level, and a body walking out of two
	// 18-metre lights would walk into the dark.
	const FDriveRefs Refs = ResolveDriveBody(GetWorld());
	if (Refs)
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Refs.Pawn->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
		UpdateStage(FBox(Origin - Extent, Origin + Extent));
	}

	// Nothing else. No camera shot, no control rotation, no clip seek, no placement: every one of
	// those is the shipping path's, and writing one here would be writing over the thing the
	// acceptance is watching.
	DrawLabOverlays();
}

bool FElysiumGreenRoomRun::LabRestand(FString& OutError)
{
	if (ReviewStem.IsEmpty())
	{
		OutError = TEXT("nothing is standing on the stage");
		return false;
	}
	AElysiumMapActor* Map = GetMap();
	UElysiumEntityBodies* BodyFactory = Map != nullptr ? Map->GetBodies() : nullptr;
	if (BodyFactory != nullptr)
	{
		// Before the rebuild, not after: LabSetBody resolves the mesh through the same cache this
		// clears, so clearing afterwards would leave the body it just built on the old path and
		// only take effect on the restand after this one.
		BodyFactory->ForgetNpcVisuals();
	}
	return LabSetBody(ReviewStem, ReviewClip, OutError);
}

bool FElysiumGreenRoomRun::LabSetLayer(const FString& Clip, float Weight, FString& OutError)
{
	AElysiumMapActor* Map = GetMap();
	USkeletalMeshComponent* Body = LabBody();
	if (!IsLabReady() || Map == nullptr || Body == nullptr)
	{
		OutError = TEXT("nothing is standing on the stage");
		return false;
	}
	if (Clip.IsEmpty())
	{
		OutError = TEXT("no layer named");
		return false;
	}
	UElysiumEntityBodies* BodyFactory = Map->GetBodies();
	if (BodyFactory == nullptr || !BodyFactory->PlayNpcLayer(Body, ReviewStem, Clip, Weight))
	{
		// The two refusals worth telling apart, because only one of them is about the clip. A layer
		// needs both halves of the combine off the asset itself — an additive base and a bone mask —
		// so a plain pose clip is declined rather than composed out of reference-pose bones.
		OutError = FString::Printf(
			TEXT("%s cannot layer '%s' — is it a `_delta` or a masked `_layer` clip, and is the ")
			TEXT("stem exported? (`uv run elysium export characters`, then Restand)"),
			*ReviewStem, *Clip);
		return false;
	}
	ReviewLayer = Clip;
	ReviewLayers.AddUnique(Clip);
	UE_LOG(LogElysiumGreenRoom, Log, TEXT("lab: %s layer %s at %.2f"), *ReviewStem, *Clip, Weight);
	return true;
}

bool FElysiumGreenRoomRun::LabSetGrid(const FString& Label, FString& OutError)
{
	AElysiumMapActor* Map = GetMap();
	USkeletalMeshComponent* Body = LabBody();
	if (!IsLabReady() || Map == nullptr || Body == nullptr)
	{
		OutError = TEXT("nothing is standing on the stage");
		return false;
	}
	UElysiumEntityBodies* BodyFactory = Map->GetBodies();
	FElysiumResolvedGrid Grid;
	if (BodyFactory == nullptr || !BodyFactory->PlayNpcGrid(Body, ReviewStem, Label, Grid))
	{
		// Three refusals share this message because the operator's next move is the same for all
		// three, and the fourth — an aim grid — is the one worth naming apart, so the clip list marks
		// those rather than leaving them to fail here.
		OutError = FString::Printf(
			TEXT("%s cannot stand '%s' as a grid — does the label name one, is the body baked ")
			TEXT("(re-export, then Restand)?"),
			*ReviewStem, *Label);
		return false;
	}

	ReviewGrid = Grid;
	ReviewClip = Label;
	// Mid-range on every axis, which is the resting value of every parameter VtMB declares: `move_yaw`
	// 0 is straight ahead on a -180..180 fan, and `aim_yaw` 0 is level. So a grid stands at the pose
	// a body nothing has steered would hold.
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		ReviewGridAt[Axis] = Axis < Grid.Axes
			? 0.5f * (Grid.AxisMin[Axis] + Grid.AxisMax[Axis]) : 0.f;
	}
	BodyFactory->SetNpcGridPosition(Body, ReviewGridAt[0], ReviewGridAt[1]);
	UE_LOG(LogElysiumGreenRoom, Log, TEXT("lab: %s grid %s (%d axis) at %.1f, %.1f"),
		*ReviewStem, *Label, Grid.Axes, ReviewGridAt[0], ReviewGridAt[1]);
	return true;
}

void FElysiumGreenRoomRun::LabSetGridPosition(float Axis0, float Axis1)
{
	ReviewGridAt[0] = Axis0;
	ReviewGridAt[1] = Axis1;
	AElysiumMapActor* Map = GetMap();
	if (UElysiumEntityBodies* BodyFactory = Map != nullptr ? Map->GetBodies() : nullptr)
	{
		BodyFactory->SetNpcGridPosition(LabBody(), Axis0, Axis1);
	}
}

void FElysiumGreenRoomRun::LabClearGrid()
{
	AElysiumMapActor* Map = GetMap();
	if (UElysiumEntityBodies* BodyFactory = Map != nullptr ? Map->GetBodies() : nullptr)
	{
		BodyFactory->StopNpcGrid(LabBody());
	}
	ReviewGrid = FElysiumResolvedGrid();
	// Back to a clip, because a body with neither a grid nor a clip stands in its reference pose and
	// reads as a broken model rather than a cleared control.
	FString Ignored;
	LabSetBody(ReviewStem, FString(), Ignored);
}

void FElysiumGreenRoomRun::LabClearLayers()
{
	AElysiumMapActor* Map = GetMap();
	UElysiumEntityBodies* BodyFactory = Map != nullptr ? Map->GetBodies() : nullptr;
	if (BodyFactory != nullptr)
	{
		BodyFactory->StopNpcLayers(LabBody());
	}
	ReviewLayer.Reset();
	ReviewLayers.Reset();
}

void FElysiumGreenRoomRun::ApplyLabHud(bool bShow) const
{
	if (UElysiumPlayerUISubsystem* UI = UElysiumPlayerUISubsystem::Get(GetWorld()))
	{
		UI->SetHUDSurfaceVisible(bShow);
	}
}

void FElysiumGreenRoomRun::TickLab(float DeltaSeconds)
{
	if (IsDriving())
	{
		TickDrive(DeltaSeconds);
		return;
	}

	ApplyLabHud(LabViewState.bShowHud);

	AElysiumMapActor* Map = GetMap();
	USkeletalMeshComponent* Body = LabBody();
	if (!Map || !Body || !Body->GetSkeletalMeshAsset())
	{
		// Nothing standing: hold the empty stage in frame. Every frame, not once on the edge — the
		// window can clear the body, and on a stage world there is nothing else in the level to look
		// at, so a camera left where the last body was would be pointing at void.
		UpdateStage(EmptyStageBounds());
		FrameLabCamera(EmptyStageBounds());
		PinCameraAndPlayerSurface();
		return;
	}

	// The clip is driven by absolute time rather than left to play itself. `Seek` pins the player at
	// a position and a zero play rate, so one mechanism gives pause, scrub and speed together — and
	// the simulation still integrates over real frame time underneath, which is why a paused body
	// keeps settling instead of freezing mid-swing.
	const float Duration = LabDuration();
	if (!LabViewState.bPaused && Duration > KINDA_SMALL_NUMBER)
	{
		LabClipTime = FMath::Fmod(LabClipTime + DeltaSeconds * LabViewState.Speed + Duration, Duration);
	}
	Body->SetWorldLocationAndRotation(BodyOrigin(), BodyRotation(/*bAnimatedProp=*/false));
	Map->SeekCinematicClip(Body, LabClipTime);

	Body->UpdateBounds();
	const FBox Bounds = Body->Bounds.GetBox();
	if (Bounds.IsValid)
	{
		UpdateStage(Bounds);
		FrameLabCamera(Bounds);
	}

	PinCameraAndPlayerSurface();
	DrawLabOverlays();
}

void FElysiumGreenRoomRun::DrawLabOverlays() const
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* Body = LabBody();
	if (!World || !Body
		|| (!LabViewState.bDrawLattice && !LabViewState.bDrawColliders
			&& !LabViewState.bDrawSkeleton))
	{
		return;
	}

	// The garment, if this body wears one. Its collision comes from the generated physics asset
	// rather than from the cloth collection, so the bones that own a collider are read from there
	// and that is also what the skeleton view highlights.
	const UChaosClothComponent* Garment = nullptr;
	for (const USceneComponent* Child : Body->GetAttachChildren())
	{
		if (const UChaosClothComponent* Candidate = Cast<UChaosClothComponent>(Child))
		{
			Garment = Candidate;
			break;
		}
	}
	const UChaosClothAsset* Asset = Garment
		? Cast<UChaosClothAsset>(Garment->GetAsset()) : nullptr;
	const UPhysicsAsset* Physics = Asset ? Asset->GetPhysicsAsset() : nullptr;

	TSet<FName> Driving;
	if (Physics != nullptr)
	{
		for (const USkeletalBodySetup* Setup : Physics->SkeletalBodySetups)
		{
			if (Setup != nullptr)
			{
				Driving.Add(Setup->BoneName);
			}
		}
	}

	// One frame's lifetime: this runs every tick, and a persistent line would stack until the
	// overlay is a solid block.
	if (LabViewState.bDrawSkeleton)
	{
		const USkinnedAsset* Skinned = Body->GetSkinnedAsset();
		const FReferenceSkeleton* Ref = Skinned ? &Skinned->GetRefSkeleton() : nullptr;
		for (int32 Index = 0; Ref != nullptr && Index < Ref->GetNum(); ++Index)
		{
			const FName Name = Ref->GetBoneName(Index);
			// The bones a collider hangs off, labelled. Every other bone is a joint and a line:
			// naming ninety of them costs a DrawDebugString apiece and buries the few being judged.
			const bool bDrives = Driving.Contains(Name);
			const FColor Colour = bDrives ? FColor(255, 90, 120) : FColor(150, 230, 160);
			const FVector Here = Body->GetBoneTransform(Index).GetLocation();
			DrawDebugPoint(World, Here, bDrives ? 8.0f : 5.0f, Colour, false, -1.0f, SDPG_Foreground);

			const int32 Parent = Ref->GetParentIndex(Index);
			if (Parent != INDEX_NONE)
			{
				DrawDebugLine(World, Body->GetBoneTransform(Parent).GetLocation(), Here,
					FColor(90, 190, 110), false, -1.0f, SDPG_Foreground, 0.4f);
			}
			if (bDrives)
			{
				DrawDebugString(World, Here + FVector(0, 0, 4.f), Name.ToString(), nullptr,
					Colour, 0.f, /*bShadow*/ true, /*Scale*/ 0.9f);
			}
		}
	}

	// What the garment actually occupies. A cloth that has collapsed onto its anchors and one that
	// is simulating read identically in a lit viewport at rest; the bounds separate them.
	if (Garment != nullptr && LabViewState.bDrawLattice)
	{
		const FBoxSphereBounds Bounds = Garment->Bounds;
		DrawDebugBox(World, Bounds.Origin, Bounds.BoxExtent, FColor(255, 190, 90),
			false, -1.0f, SDPG_Foreground, 0.6f);
	}

	// The authored capsules and spheres the garment is solved against, on their own bones. This is
	// what a hem passing through a leg is diagnosed with: a collider sitting off its limb reads at
	// a glance against the labelled bone beside it.
	if (Physics != nullptr && LabViewState.bDrawColliders)
	{
		for (const USkeletalBodySetup* Setup : Physics->SkeletalBodySetups)
		{
			if (Setup == nullptr)
			{
				continue;
			}
			const FTransform Frame = Body->GetSocketTransform(Setup->BoneName, RTS_World);
			for (const FKSphereElem& Sphere : Setup->AggGeom.SphereElems)
			{
				DrawDebugSphere(World, Frame.TransformPosition(Sphere.Center), Sphere.Radius, 16,
					FColor(255, 90, 120), false, -1.0f, SDPG_Foreground, 0.6f);
			}
			for (const FKSphylElem& Sphyl : Setup->AggGeom.SphylElems)
			{
				DrawDebugCapsule(World, Frame.TransformPosition(Sphyl.Center),
					Sphyl.Length * 0.5f + Sphyl.Radius, Sphyl.Radius,
					Frame.GetRotation() * Sphyl.Rotation.Quaternion(),
					FColor(255, 130, 90), false, -1.0f, SDPG_Foreground, 0.6f);
			}
		}
	}
}

bool FElysiumGreenRoomRun::Tick(float DeltaSeconds)
{
	AElysiumMapActor* Map = GetMap();
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	switch (Phase)
	{
	case EPhase::WaitReady:
		if (bLab)
		{
			if (!Map || !Map->IsSpawnDone() || !PC || !Pawn
				|| ++FrameInPhase < GreenRoomBootSettleFrames)
			{
				break;
			}
			ResolveCases();
			if (!CreateStage())
			{
				UE_LOG(LogElysiumGreenRoom, Warning, TEXT("lab: could not build the stage"));
				bAnyFailure = true;
				Phase = EPhase::Done;
				return false;
			}
			// Build the empty room *and look at it*. On a stage world that is the whole picture: the
			// pawn is seated at the origin with the stage 500 m away and an empty level between them,
			// so a camera nobody has framed yet renders black until the first body stands up.
			UpdateStage(EmptyStageBounds());
			FrameLabCamera(EmptyStageBounds());
			Phase = EPhase::Lab;
			// Arm Chaos's debug drawing for the whole lab session. It is off at every launch and
			// gates every cloth overlay, so leaving it to be discovered means the garment panel's
			// checkboxes do nothing the first time anyone ticks one. Nothing is drawn until an
			// overlay is actually selected, so arming it costs a bool.
			ElysiumClothDebug::SetDrawEnabled(true);
			UE_LOG(LogElysiumGreenRoom, Log,
				TEXT("lab: stage ready — F1, or `elysium.gr`, opens the window that drives it"));
			// `gr <model> [clip]` names a body up front, and `--drive` says which mode stands it. A
			// failure here is reported and nothing else: the stage is up, and the window can ask
			// again.
			if (bDriveRequested)
			{
				FString Error;
				if (!LabSetMode(ELabMode::Drive, Error))
				{
					UE_LOG(LogElysiumGreenRoom, Warning, TEXT("lab: %s"), *Error);
				}
			}
			else if (!ReviewStem.IsEmpty())
			{
				FString Error;
				if (!LabSetBody(ReviewStem, ReviewClip, Error))
				{
					UE_LOG(LogElysiumGreenRoom, Warning, TEXT("lab: %s"), *Error);
				}
			}
			break;
		}
		if (Map && Map->IsSpawnDone() && PC && Pawn && ++FrameInPhase >= GreenRoomBootSettleFrames)
		{
			ResolveCases();
			if (ActiveCases.IsEmpty() || !CreateStage() || !BuildBodies())
			{
				bAnyFailure = true;
				Finish();
				Phase = EPhase::Done;
				return false;
			}
			SeekPose();
			Phase = EPhase::PoseWarmup;
		}
		break;

	case EPhase::PoseWarmup:
		PinCameraAndPlayerSurface();
		if (++FrameInPhase >= PoseWarmupFrames)
		{
			if (!PrepareFrame())
			{
				Finish();
				Phase = EPhase::Done;
				return false;
			}
			FrameInPhase = 0;
			Phase = EPhase::Settle;
		}
		break;

	case EPhase::Settle:
		PinCameraAndPlayerSurface();
		if (++FrameInPhase >= SettleFrames)
		{
			BeginCapture();
			FrameInPhase = 0;
			Phase = EPhase::Await;
		}
		break;

	case EPhase::Await:
		PinCameraAndPlayerSurface();
		if (!bAwaitingCapture)
		{
			Advance();
			if (Phase == EPhase::Done)
			{
				return false;
			}
		}
		break;

	case EPhase::Lab:
		TickLab(DeltaSeconds);
		break;

	case EPhase::Done:
	default:
		return false;
	}
	return true;
}
