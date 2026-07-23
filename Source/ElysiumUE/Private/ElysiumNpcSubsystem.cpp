#include "ElysiumNpcSubsystem.h"

#include "ElysiumContentPaths.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpc, Log, All);

TArray<FString> UElysiumNpcSubsystem::AvailableGlbStems()
{
	TArray<FString> Stems;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(FElysiumContentPaths::NpcDir() / TEXT("*.glb")), true, false);
	for (const FString& File : Files)
	{
		Stems.Add(FPaths::GetBaseFilename(File));
	}
	Stems.Sort();
	return Stems;
}

void UElysiumNpcSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IConsoleManager& CM = IConsoleManager::Get();

	// elysium.npc.load [stem] [anim] -- load out/npc/<stem>.glb via glTFRuntime and spawn it in
	// front of the player, playing <anim> (or the first animation). Defaults to the exported test
	// NPC. Scriptable echo of the Cog NPC window's Load button (F1-first: the window is primary).
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.load"),
		TEXT("elysium.npc.load [stem] [anim] -- load out/npc/<stem>.glb (mesh+skeleton+anim) via glTFRuntime and spawn it near the player (default: gangmember_male_2)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Stem = Args.Num() > 0 ? Args[0] : TEXT("gangmember_male_2");
			const FString AnimName = Args.Num() > 1 ? Args[1] : FString();
			FString Error;
			if (LoadTestNpc(Stem, AnimName, Error) == nullptr)
			{
				UE_LOG(LogElysiumNpc, Warning, TEXT("npc.load failed: %s (%s)"), *Stem, *Error);
			}
		}),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.clear"),
		TEXT("elysium.npc.clear -- destroy every test NPC spawned by elysium.npc.load"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ClearNpcs(); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.list"),
		TEXT("elysium.npc.list -- list the .glb NPC assets under out/npc"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			const TArray<FString> Stems = AvailableGlbStems();
			UE_LOG(LogElysiumNpc, Display, TEXT("%d NPC glb(s) under out/npc:"), Stems.Num());
			for (const FString& Stem : Stems)
			{
				UE_LOG(LogElysiumNpc, Display, TEXT("  %s"), *Stem);
			}
		}),
		ECVF_Cheat));
}

void UElysiumNpcSubsystem::Deinitialize()
{
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();

	ClearNpcs();

	Super::Deinitialize();
}

AActor* UElysiumNpcSubsystem::LoadTestNpc(const FString& Stem, const FString& AnimName, FString& OutError)
{
	OutError.Reset();

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		OutError = TEXT("no world");
		return nullptr;
	}

	const FString FullPath = FElysiumContentPaths::NpcGlb(Stem);
	if (!FPaths::FileExists(FullPath))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *FullPath);
		return nullptr;
	}

	const double StartSeconds = FPlatformTime::Seconds();

	// glTF is self-describing (Y-up, metres, right-handed); glTFRuntime's default config converts it
	// to UE space (Z-up, cm, left-handed) -- SceneScale 100 (m->cm), TransformBaseType::Default, and
	// bAllowExternalFiles so the sibling tex/*.png resolve relative to the .glb. So the standard glb
	// mdl_gltf.py writes loads 1:1 with no UE_-style pre-conversion (the raw-OBJ path's convention
	// applies only to dumb container formats, not a self-describing one the loader can reorient).
	FglTFRuntimeConfig Config;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FullPath, false, Config);
	if (Asset == nullptr)
	{
		OutError = TEXT("glTFRuntime could not parse the .glb");
		return nullptr;
	}

	FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;
	USkeletalMesh* Mesh = Asset->LoadSkeletalMesh(0, 0, SkeletalMeshConfig);
	if (Mesh == nullptr)
	{
		OutError = TEXT("LoadSkeletalMesh(mesh 0, skin 0) returned null");
		return nullptr;
	}

	// Animation: by name when asked, else the first clip. Void on failure just leaves the mesh in its
	// ref pose (still a valid spike result -- the mesh + skeleton loaded).
	const TArray<FString> AnimNames = Asset->GetAnimationsNames(true);
	FglTFRuntimeSkeletalAnimationConfig AnimConfig;
	UAnimSequence* Anim = nullptr;
	FString AppliedAnim;
	if (!AnimName.IsEmpty())
	{
		Anim = Asset->LoadSkeletalAnimationByName(Mesh, AnimName, AnimConfig, /*bCaseSensitive=*/false);
		if (Anim != nullptr)
		{
			AppliedAnim = AnimName;
		}
		else
		{
			UE_LOG(LogElysiumNpc, Warning, TEXT("npc.load: anim '%s' not in %s (have: %s)"),
				*AnimName, *Stem, *FString::Join(AnimNames, TEXT(", ")));
		}
	}
	else if (Asset->GetNumAnimations() > 0)
	{
		Anim = Asset->LoadSkeletalAnimation(Mesh, 0, AnimConfig);
		AppliedAnim = AnimNames.Num() > 0 ? AnimNames[0] : TEXT("anim0");
	}

	// Spawn a plain actor with a skeletal-mesh component root, in front of the player.
	FRotator Rotation;
	const FVector Location = ComputeSpawnLocation(Rotation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, SpawnParams);
	if (Actor == nullptr)
	{
		OutError = TEXT("SpawnActor failed");
		return nullptr;
	}
#if WITH_EDITOR
	Actor->SetActorLabel(FString::Printf(TEXT("NPC_%s"), *Stem));
#endif

	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetSkeletalMeshAsset(Mesh);
	Actor->SetRootComponent(Component);
	Component->RegisterComponent();
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Anim != nullptr)
	{
		Component->PlayAnimation(Anim, /*bLooping=*/true);
	}

	const double LoadMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

	PruneDead();

	FElysiumLoadedNpc Record;
	Record.Actor = Actor;
	Record.Mesh = Mesh;
	Record.Anim = Anim;
	Record.Stem = Stem;
	Record.AnimName = AppliedAnim;
	Record.NumBones = Mesh->GetRefSkeleton().GetNum();
	Record.NumAnims = Asset->GetNumAnimations();
	Record.AnimNames = AnimNames;
	Record.Location = Location;
	Record.LoadMilliseconds = LoadMs;
	Loaded.Add(Record);

	UE_LOG(LogElysiumNpc, Display,
		TEXT("npc.load %s: %d bones, %d anims, clip '%s' -> spawned at %s (%.1f ms)"),
		*Stem, Record.NumBones, Record.NumAnims, *Record.AnimName, *Location.ToCompactString(), LoadMs);

	return Actor;
}

void UElysiumNpcSubsystem::ClearNpcs()
{
	for (FElysiumLoadedNpc& Record : Loaded)
	{
		if (IsValid(Record.Actor))
		{
			Record.Actor->Destroy();
		}
	}
	Loaded.Empty();
}

void UElysiumNpcSubsystem::PruneDead()
{
	Loaded.RemoveAll([](const FElysiumLoadedNpc& Record) { return !IsValid(Record.Actor); });
}

FVector UElysiumNpcSubsystem::ComputeSpawnLocation(FRotator& OutRotation) const
{
	OutRotation = FRotator::ZeroRotator;

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (Pawn == nullptr)
	{
		return FVector::ZeroVector;
	}

	FVector Forward = Pawn->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();

	// Player-feet (capsule centre less a nominal half-height) + a short forward offset, so the NPC
	// stands on the floor roughly where the player is looking.
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.f, 0.f, 88.f);
	const FVector Location = Feet + Forward * 220.f;

	OutRotation = FRotator(0.f, (Pawn->GetActorLocation() - Location).Rotation().Yaw, 0.f);
	return Location;
}
