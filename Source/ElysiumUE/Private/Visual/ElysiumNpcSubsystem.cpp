#include "ElysiumNpcSubsystem.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "glTFRuntimeAsset.h"
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

	// elysium.npc.play <clip> [index] -- re-apply a clip to an already-spawned test NPC, resolving
	// it through the manifest so any of its ~1,540 resolved clips plays, not just its own glb's.
	// This is how a named sequence is checked before wiring it to anything (8.5: scripted_sequence
	// `m_iszPlay`, the `SetAnimation` input, the `SetGesture` Character method).
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.play"),
		TEXT("elysium.npc.play <clip> [index] -- play a clip on a spawned test NPC (default: the last one); resolves shared animation banks"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumNpc, Warning, TEXT("npc.play: needs a clip name"));
				return;
			}
			FString Error;
			const int32 Index = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : Loaded.Num() - 1;
			if (!PlayClipOn(Index, Args[0], Error))
			{
				UE_LOG(LogElysiumNpc, Warning, TEXT("npc.play failed: %s"), *Error);
			}
		}),
		ECVF_Cheat));

	// elysium.npc.clips <stem> [filter] -- what a model can actually play, and where each clip
	// lives. The filter matches the label or the activity, so `elysium.npc.clips regular_cop
	// ACT_IDLE` lists exactly the engine's idle set with its weights.
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.clips"),
		TEXT("elysium.npc.clips <stem> [filter] -- list a model's resolved clips (label, owning glb, activity, weight); filter matches label or activity"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			UElysiumNpcAnimSubsystem* Anims = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
			const FString Stem = Args.Num() > 0 ? Args[0] : TEXT("gangmember_male_2");
			const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
			if (Set == nullptr)
			{
				UE_LOG(LogElysiumNpc, Warning, TEXT("npc.clips: no vocabulary for '%s'"), *Stem);
				return;
			}
			const FString Filter = Args.Num() > 1 ? Args[1] : FString();
			EElysiumIdleTier Tier = EElysiumIdleTier::None;
			const TArray<FString> Idles = Anims->IdleCandidates(Stem, FString(), Tier);
			UE_LOG(LogElysiumNpc, Display, TEXT("%s: %d clips; idle tier=%s, best='%s'"), *Stem,
				Set->Clips.Num(), UElysiumNpcAnimSubsystem::TierName(Tier),
				Idles.Num() > 0 ? *Idles[0] : TEXT("<none>"));
			int32 Shown = 0;
			for (const TPair<FString, FElysiumNpcClip>& Pair : Set->Clips)
			{
				if (!Filter.IsEmpty()
					&& !Pair.Key.Contains(Filter, ESearchCase::IgnoreCase)
					&& !Pair.Value.Activity.Contains(Filter, ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (++Shown > 60)
				{
					UE_LOG(LogElysiumNpc, Display, TEXT("  ... (narrow the filter)"));
					break;
				}
				UE_LOG(LogElysiumNpc, Display, TEXT("  %-34s %-42s %-24s w=%-3d %.2fs"),
					*Pair.Key, *Pair.Value.Owner,
					Pair.Value.Activity.IsEmpty() ? TEXT("-") : *Pair.Value.Activity,
					Pair.Value.Weight, Pair.Value.Seconds());
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

	const double StartSeconds = FPlatformTime::Seconds();

	// Load the mesh through the shared glTF path (ElysiumNpcVisual) -- the same loader the game NPC
	// bodies (B3) use. The parsed asset comes back so this harness can still audition any clip.
	UglTFRuntimeAsset* Asset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, OutError);
	if (Mesh == nullptr)
	{
		return nullptr;   // OutError set by LoadMesh
	}

	// Animation. A named clip resolves through the manifest, so the harness reaches the whole
	// resolved vocabulary (~1,540 clips per NPC) and not just the handful baked into this glb --
	// which is the point: an NPC's own clips are mostly dialogue, and idle/locomotion/combat live
	// in shared banks. With no name, the same default-idle policy the game uses picks one, so what
	// the harness stands matches what a map stands. Failure leaves the mesh in its ref pose.
	const TArray<FString> AnimNames = Asset->GetAnimationsNames(true);
	UAnimSequence* Anim = nullptr;
	FString AppliedAnim;
	UElysiumNpcAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;

	FString Want = AnimName;
	if (Want.IsEmpty() && Anims != nullptr)
	{
		EElysiumIdleTier Tier = EElysiumIdleTier::None;
		Want = Anims->PickIdleClip(Stem, FString(), Tier);
		if (!Want.IsEmpty())
		{
			UE_LOG(LogElysiumNpc, Display, TEXT("npc.load: default idle for %s is '%s' (%s)"),
				*Stem, *Want, UElysiumNpcAnimSubsystem::TierName(Tier));
		}
	}
	if (!Want.IsEmpty())
	{
		FString AnimError;
		if (Anims != nullptr)
		{
			Anim = Anims->ResolveClip(Stem, Want, Mesh, Asset, AnimError);
		}
		if (Anim == nullptr)
		{
			// No manifest (NPC export not run) -- fall back to this glb's own clips.
			Anim = ElysiumNpcVisual::RetargetClip(Asset, Mesh, Want, AnimError);
		}
		if (Anim != nullptr)
		{
			AppliedAnim = Want;
		}
		else
		{
			UE_LOG(LogElysiumNpc, Warning, TEXT("npc.load: clip '%s' on %s: %s (own glb has: %s)"),
				*Want, *Stem, *AnimError, *FString::Join(AnimNames, TEXT(", ")));
		}
	}
	else if (Asset->GetNumAnimations() > 0)
	{
		FglTFRuntimeSkeletalAnimationConfig AnimConfig;
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
	Record.Asset = Asset;
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

bool UElysiumNpcSubsystem::PlayClipOn(int32 Index, const FString& ClipName, FString& OutError)
{
	OutError.Reset();
	PruneDead();
	if (!Loaded.IsValidIndex(Index))
	{
		OutError = FString::Printf(TEXT("no test NPC at index %d (%d loaded)"), Index, Loaded.Num());
		return false;
	}
	FElysiumLoadedNpc& Record = Loaded[Index];
	USkeletalMeshComponent* Component = IsValid(Record.Actor)
		? Record.Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (Component == nullptr || Record.Mesh == nullptr)
	{
		OutError = TEXT("that test NPC has no live skeletal body");
		return false;
	}

	UElysiumNpcAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	UAnimSequence* Anim = nullptr;
	if (Anims != nullptr)
	{
		// Resolves the owning glb through the manifest — the NPC's own for a dialogue clip, a
		// shared bank otherwise, with the bank parsed once per session.
		Anim = Anims->ResolveClip(Record.Stem, ClipName, Record.Mesh, Record.Asset, OutError);
	}
	if (Anim == nullptr)
	{
		OutError = FString::Printf(TEXT("'%s' on %s: %s"), *ClipName, *Record.Stem,
			OutError.IsEmpty() ? TEXT("no animation subsystem") : *OutError);
		return false;
	}
	Component->PlayAnimation(Anim, /*bLooping=*/true);
	Record.Anim = Anim;
	Record.AnimName = ClipName;
	UE_LOG(LogElysiumNpc, Display, TEXT("npc.play: %s -> %s"), *Record.Stem, *ClipName);
	return true;
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
