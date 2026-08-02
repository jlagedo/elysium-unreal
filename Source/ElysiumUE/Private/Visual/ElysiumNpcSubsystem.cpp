#include "ElysiumNpcSubsystem.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcAnimInstance.h"
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
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpc, Log, All);

namespace
{
	// What a face can be told to do, read straight off the sidecar. This is the answer when nothing
	// is standing yet — the whole controller vocabulary, grouped by the five shipped families, plus
	// the reconstructed eyelid hinges.
	void DumpFacialRigFromDisk(UGameInstance* GameInstance, const FString& Stem)
	{
		UElysiumNpcAnimSubsystem* Anims = GameInstance
			? GameInstance->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
		if (Anims == nullptr || Stem.IsEmpty())
		{
			UE_LOG(LogElysiumNpc, Warning,
				TEXT("npc.flex_dump: no live rigged NPC; name a stem to read its rig off disk"));
			return;
		}
		const TSharedPtr<const FElysiumFacialRig> Rig = Anims->GetFacialRig(Stem);
		if (!Rig.IsValid())
		{
			UE_LOG(LogElysiumNpc, Warning, TEXT("npc.flex_dump: '%s' carries no facial flex rig"), *Stem);
			return;
		}
		UE_LOG(LogElysiumNpc, Display, TEXT("%s: %d controllers, %d rules, %d morphs, %d lid(s)"),
			*Rig->Stem, Rig->Controllers.Num(), Rig->Rules.Num(), Rig->Morphs.Num(), Rig->Lids.Num());
		for (const FElysiumFlexController& Controller : Rig->Controllers)
		{
			UE_LOG(LogElysiumNpc, Display, TEXT("  ctl   %-24s %-10s [%.2f..%.2f]"),
				*Controller.Name, *Controller.Type, Controller.Min, Controller.Max);
		}
		for (const FElysiumFlexLid& Lid : Rig->Lids)
		{
			UE_LOG(LogElysiumNpc, Display,
				TEXT("  lid   %-24s lowered %.3f  neutral %.3f  raised %.3f"),
				*Rig->FlexDescs[Lid.FlexDesc], Lid.LoweredAngle, Lid.NeutralAngle, Lid.RaisedAngle);
		}
	}
}

TArray<UElysiumNpcAnimInstance*> UElysiumNpcSubsystem::FacialBodies(const FString& StemFilter) const
{
	TArray<UElysiumNpcAnimInstance*> Bodies;
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Bodies;
	}
	// Every rigged body in the world, however it was spawned: the map's `npc_*` entities and this
	// harness's preview bodies both stand on UElysiumNpcAnimInstance, and the rig knows its own stem,
	// so one iteration covers both without either side registering anywhere.
	for (TObjectIterator<UElysiumNpcAnimInstance> It; It; ++It)
	{
		UElysiumNpcAnimInstance* Inst = *It;
		const FElysiumFacialRig* Rig = IsValid(Inst) ? Inst->GetFacialRig() : nullptr;
		if (Rig == nullptr || Inst->GetWorld() != World)
		{
			continue;
		}
		if (StemFilter.IsEmpty() || Rig->Stem.Contains(StemFilter, ESearchCase::IgnoreCase))
		{
			Bodies.Add(Inst);
		}
	}
	return Bodies;
}

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

	// elysium.npc.flex <controller> <value> [stem] -- write one flex controller on every live facial
	// body (12.3). Controllers are the whole input surface: everything below one is arithmetic
	// replayed from the rig, so this is the acceptance instrument until 12.1's scene expression
	// tracks and 12.5's lipsync start writing the same values. `blink 1` closes both pairs of lids
	// through the four eyelid rules; `right_lid_droop 1` moves one lid partway, which is the RPN
	// layer showing itself rather than a 1:1 passthrough.
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.flex"),
		TEXT("elysium.npc.flex <controller> <value> [stem] -- set a facial flex controller (e.g. blink 1) on every live rigged NPC, or only those whose model stem contains <stem>"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogElysiumNpc, Warning, TEXT("npc.flex: needs <controller> <value>"));
				return;
			}
			const FString Controller = Args[0];
			const float Value = FCString::Atof(*Args[1]);
			int32 Written = 0, Missing = 0;
			for (UElysiumNpcAnimInstance* Inst : FacialBodies(Args.Num() > 2 ? Args[2] : FString()))
			{
				(Inst->SetFlexController(Controller, Value) ? Written : Missing)++;
			}
			UE_LOG(LogElysiumNpc, Display, TEXT("npc.flex %s=%.3f -> %d body(ies)%s"),
				*Controller, Value, Written,
				Missing > 0 ? *FString::Printf(TEXT("; %d rig(s) carry no such controller"), Missing)
					: TEXT(""));
		}),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.flex_reset"),
		TEXT("elysium.npc.flex_reset [stem] -- put every facial flex controller back to rest, which leaves every morph target at zero"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const TArray<UElysiumNpcAnimInstance*> Bodies = FacialBodies(Args.Num() > 0 ? Args[0] : FString());
			for (UElysiumNpcAnimInstance* Inst : Bodies)
			{
				Inst->ResetFlexControllers();
			}
			UE_LOG(LogElysiumNpc, Display, TEXT("npc.flex_reset -> %d body(ies)"), Bodies.Num());
		}),
		ECVF_Cheat));

	// The read side of the same surface, and the way to find out what a face can be told to do.
	// With a live body it reports what the three layers currently resolve to; with none, a stem
	// argument reads the sidecar off disk, so the controller names are answerable before a map is.
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.flex_dump"),
		TEXT("elysium.npc.flex_dump [stem] -- report each live rigged NPC's controllers and its non-zero flexdesc/morph weights; with no live body, list a stem's rig off disk"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();
			const TArray<UElysiumNpcAnimInstance*> Bodies = FacialBodies(Filter);
			if (Bodies.IsEmpty())
			{
				DumpFacialRigFromDisk(GetGameInstance(), Filter);
				return;
			}
			for (const UElysiumNpcAnimInstance* Inst : Bodies)
			{
				const FElysiumFacialRig& Rig = *Inst->GetFacialRig();
				const TArray<float>& Controllers = Inst->GetFlexControllerValues();
				const TArray<float>& Flexes = Inst->GetFlexWeights();
				const TArray<float>& Morphs = Inst->GetMorphWeights();
				UE_LOG(LogElysiumNpc, Display,
					TEXT("%s: %d controllers, %d rules, %d morphs, %d lid(s)"), *Rig.Stem,
					Rig.Controllers.Num(), Rig.Rules.Num(), Rig.Morphs.Num(), Rig.Lids.Num());
				for (int32 i = 0; i < Controllers.Num(); ++i)
				{
					if (Controllers[i] != 0.f)
					{
						UE_LOG(LogElysiumNpc, Display, TEXT("  ctl   %-24s %.3f"),
							*Rig.Controllers[i].Name, Controllers[i]);
					}
				}
				for (int32 i = 0; i < Flexes.Num(); ++i)
				{
					if (!FMath::IsNearlyZero(Flexes[i]))
					{
						UE_LOG(LogElysiumNpc, Display, TEXT("  flex  %-24s %.4f"),
							*Rig.FlexDescs[i], Flexes[i]);
					}
				}
				for (int32 i = 0; i < Morphs.Num(); ++i)
				{
					if (!FMath::IsNearlyZero(Morphs[i]))
					{
						UE_LOG(LogElysiumNpc, Display, TEXT("  morph %-24s %.4f"),
							*Rig.Morphs[i].Name, Morphs[i]);
					}
				}
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
	// The same animation host the map's own bodies stand on, installed before the component
	// registers so it owns the pose from frame one. The preview body carries no A/B against the
	// single-node instance because a single-node instance has no facial track at all: the face is
	// the reason this harness needs the real host.
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	Actor->SetRootComponent(Component);
	Component->RegisterComponent();
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Component->GetAnimInstance()))
	{
		Inst->SetFacialRig(Anims ? Anims->GetFacialRig(Stem) : nullptr);
		Inst->SetCompositionRig(Anims ? Anims->GetCompositionRig(Stem) : nullptr);
		if (Anim != nullptr)
		{
			Inst->PlayClip(Anim, /*bLoop=*/true);
		}
	}
	else if (Anim != nullptr)
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Component->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, /*bLoop=*/true);
	}
	else
	{
		Component->PlayAnimation(Anim, /*bLooping=*/true);
	}
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
