#include "ElysiumNpcSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumCharacterAssets.h"

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
	// The eye pass's own inputs and what they solve to. Every wrong answer the lid write-back
	// can give is a plausible-looking number, so the terms print separately rather than only the
	// result: at rest `upL.up` must be 1, `fwL.up` must be 0, and the lid must land exactly on the
	// neutral target, which is the hinge the two morph ramps meet at.
	void DumpEyeState(const UElysiumBipedAnimInstance* Inst)
	{
		if (Inst == nullptr || Inst->GetFacialRig() == nullptr)
		{
			return;
		}
		const FElysiumEyeInput& Eyes = Inst->GetEyeInput();
		const TArray<float>& Flex = Inst->GetFlexWeights();
		UE_LOG(LogElysiumNpc, Display, TEXT("%s: blink %.4f, lids %s"), *Inst->GetFacialRig()->Stem,
			Eyes.Blink, Eyes.bWriteLids ? TEXT("on") : TEXT("off"));
		for (int32 e = 0; e < 2; ++e)
		{
			const FElysiumEyeAim& A = Eyes.Eyes[e];
			if (!A.bValid)
			{
				UE_LOG(LogElysiumNpc, Display, TEXT("  eye[%d] no aim"), e);
				continue;
			}
			UE_LOG(LogElysiumNpc, Display,
				TEXT("  eye[%d] r=%.3f upL=(%.4f %.4f %.4f) fwL=(%.4f %.4f %.4f) up=(%.4f %.4f %.4f)"),
				e, A.Radius, A.UpLocal.X, A.UpLocal.Y, A.UpLocal.Z,
				A.ForwardLocal.X, A.ForwardLocal.Y, A.ForwardLocal.Z,
				A.AuthoredUp.X, A.AuthoredUp.Y, A.AuthoredUp.Z);
			UE_LOG(LogElysiumNpc, Display, TEXT("         upL.up=%.4f fwL.up=%.4f |upL|=%.4f |up|=%.4f"),
				FVector::DotProduct(A.UpLocal, A.AuthoredUp),
				FVector::DotProduct(A.ForwardLocal, A.AuthoredUp),
				A.UpLocal.Size(), A.AuthoredUp.Size());
			const auto Report = [&](const TCHAR* Label, const int32 (&Src)[3], const float (&Tgt)[3], int32 Lid)
			{
				float Sum = 0.f;
				FString Terms;
				for (int32 k = 0; k < 3; ++k)
				{
					const float W = Flex.IsValidIndex(Src[k]) ? Flex[Src[k]] : 0.f;
					const float Angle = FMath::Asin(FMath::Clamp(Tgt[k] / A.Radius, -1.f, 1.f));
					Sum += Angle * W;
					Terms += FString::Printf(TEXT("[fd%d t=%.3f w=%.3f a=%.4f] "), Src[k], Tgt[k], W, Angle);
				}
				UE_LOG(LogElysiumNpc, Display, TEXT("         %s %s sum=%.4f -> fd%d = %.4f"),
					Label, *Terms, Sum, Lid, Flex.IsValidIndex(Lid) ? Flex[Lid] : 0.f);
			};
			Report(TEXT("upper"), A.UpperFlexDesc, A.UpperTarget, A.UpperLidFlexDesc);
			Report(TEXT("lower"), A.LowerFlexDesc, A.LowerTarget, A.LowerLidFlexDesc);
		}
	}

	// What a face can be told to do, read straight off the sidecar. This is the answer when nothing
	// is standing yet — the whole controller vocabulary, grouped by the five shipped families, plus
	// the reconstructed eyelid hinges.
	void DumpFacialRigFromDisk(UGameInstance* GameInstance, const FString& Stem)
	{
		UElysiumAnimSubsystem* Anims = GameInstance
			? GameInstance->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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

TArray<UElysiumBipedAnimInstance*> UElysiumNpcSubsystem::FacialBodies(const FString& StemFilter) const
{
	TArray<UElysiumBipedAnimInstance*> Bodies;
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Bodies;
	}
	// Every rigged body in the world, however it was spawned: the map's `npc_*` entities and this
	// harness's preview bodies both stand on UElysiumBipedAnimInstance, and the rig knows its own stem,
	// so one iteration covers both without either side registering anywhere.
	for (TObjectIterator<UElysiumBipedAnimInstance> It; It; ++It)
	{
		UElysiumBipedAnimInstance* Inst = *It;
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

TArray<FString> UElysiumNpcSubsystem::AvailableBodyStems()
{
	return ElysiumCharacterAssets::BodyNames();
}

void UElysiumNpcSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UElysiumMapSubsystem* Maps = Collection.InitializeDependency<UElysiumMapSubsystem>())
	{
		MapEpochRetiredHandle = Maps->OnMapEpochRetired().AddUObject(
			this, &UElysiumNpcSubsystem::OnMapEpochRetired);
	}

	IConsoleManager& CM = IConsoleManager::Get();

	// elysium.npc.load [stem] [anim] -- stand the baked body for <stem> in front of the player,
	// playing <anim> (or the default idle its disposition selects). Defaults to the exported test
	// NPC. Scriptable echo of the Cog NPC window's Load button (F1-first: the window is primary).
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.load"),
		TEXT("elysium.npc.load [stem] [anim] -- stand the baked body for <stem> near the player (default: gangmember_male_2)"),
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
	// This is how a named sequence is checked before wiring it to anything (scripted_sequence
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
			UElysiumAnimSubsystem* Anims = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
				Set->Clips.Num(), UElysiumAnimSubsystem::TierName(Tier),
				Idles.Num() > 0 ? *Idles[0] : TEXT("<none>"));
			int32 Shown = 0;
			bool bCapped = false;
			// One row per (label, owner): several banks may declare one label, each under its own
			// activity, and a listing that showed the first alone would hide the others.
			Set->Clips.ForEachClip([&](const FString& Label, const FElysiumNpcClip& Clip)
			{
				if (bCapped)
				{
					return;
				}
				if (!Filter.IsEmpty()
					&& !Label.Contains(Filter, ESearchCase::IgnoreCase)
					&& !Clip.Activity.Contains(Filter, ESearchCase::IgnoreCase))
				{
					return;
				}
				if (++Shown > 60)
				{
					UE_LOG(LogElysiumNpc, Display, TEXT("  ... (narrow the filter)"));
					bCapped = true;
					return;
				}
				UE_LOG(LogElysiumNpc, Display, TEXT("  %-34s %-42s %-24s w=%-3d %.2fs"),
					*Label, *Clip.Owner,
					Clip.Activity.IsEmpty() ? TEXT("-") : *Clip.Activity,
					Clip.Weight, Clip.Seconds());
			});
		}),
		ECVF_Cheat));

	// elysium.npc.flex <controller> <value> [stem] -- write one flex controller on every live facial
	// body. Controllers are the whole input surface: everything below one is arithmetic
	// replayed from the rig, so this is the acceptance instrument until scene expression
	// tracks and lipsync start writing the same values. `blink 1` closes both pairs of lids
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
			for (UElysiumBipedAnimInstance* Inst : FacialBodies(Args.Num() > 2 ? Args[2] : FString()))
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
			const TArray<UElysiumBipedAnimInstance*> Bodies = FacialBodies(Args.Num() > 0 ? Args[0] : FString());
			for (UElysiumBipedAnimInstance* Inst : Bodies)
			{
				Inst->ResetFlexControllers();
			}
			UE_LOG(LogElysiumNpc, Display, TEXT("npc.flex_reset -> %d body(ies)"), Bodies.Num());
		}),
		ECVF_Cheat));

	// The read side of the same surface, and the way to find out what a face can be told to do.
	// With a live body it reports what the three layers currently resolve to; with none, a stem
	// argument reads the sidecar off disk, so the controller names are answerable before a map is.
	// The eye pass's own inputs and what they solve to. The lid write-back is a chain of
	// dots and an asin, and every wrong answer it can give is a plausible-looking number, so the
	// terms are printed separately rather than only the result.
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.eyes_dump"),
		TEXT("elysium.npc.eyes_dump [stem] -- per live rigged NPC, each eye's bone-local basis, the lid solve's terms, and the flexdesc weight it produces"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();
			for (const UElysiumBipedAnimInstance* Inst : FacialBodies(Filter))
			{
				DumpEyeState(Inst);
			}
		}),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.npc.flex_dump"),
		TEXT("elysium.npc.flex_dump [stem] -- report each live rigged NPC's controllers and its non-zero flexdesc/morph weights; with no live body, list a stem's rig off disk"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();
			const TArray<UElysiumBipedAnimInstance*> Bodies = FacialBodies(Filter);
			if (Bodies.IsEmpty())
			{
				DumpFacialRigFromDisk(GetGameInstance(), Filter);
				return;
			}
			for (const UElysiumBipedAnimInstance* Inst : Bodies)
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
				int32 FirstMoved = INDEX_NONE;
				for (int32 i = 0; i < Morphs.Num(); ++i)
				{
					if (!FMath::IsNearlyZero(Morphs[i]))
					{
						UE_LOG(LogElysiumNpc, Display, TEXT("  morph %-24s %.4f"),
							*Rig.Morphs[i].Name, Morphs[i]);
						FirstMoved = FirstMoved == INDEX_NONE ? i : FirstMoved;
					}
				}
				// The three hops below the rig, which a weight alone cannot tell apart: the rig's
				// answer, the curve the anim instance published, and the weight the component will
				// actually skin with. A face that computes correctly and does not move is one of the
				// last two being zero.
				if (const USkeletalMeshComponent* Comp = Inst->GetSkelMeshComponent())
				{
					int32 NonZero = 0;
					for (const float W : Comp->MorphTargetWeights)
					{
						NonZero += FMath::IsNearlyZero(W) ? 0 : 1;
					}
					UE_LOG(LogElysiumNpc, Display,
						TEXT("  comp  %d morph slot(s), %d non-zero, %d active"),
						Comp->MorphTargetWeights.Num(), NonZero, Comp->ActiveMorphTargets.Num());
					if (const USkeletalMesh* Asset = Comp->GetSkeletalMeshAsset())
					{
						UE_LOG(LogElysiumNpc, Display, TEXT("  comp  mesh carries %d morph target(s)"),
							Asset->GetMorphTargets().Num());
					}
				}
				if (FirstMoved != INDEX_NONE)
				{
					UE_LOG(LogElysiumNpc, Display, TEXT("  curve %-24s %.4f (anim instance)"),
						*Rig.Morphs[FirstMoved].Name,
						Inst->GetCurveValue(Rig.Morphs[FirstMoved].Curve));
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
			const TArray<FString> Stems = AvailableBodyStems();
			UE_LOG(LogElysiumNpc, Display, TEXT("%d NPC glb(s) under out/npc:"), Stems.Num());
			for (const FString& Stem : Stems)
			{
				UE_LOG(LogElysiumNpc, Display, TEXT("  %s"), *Stem);
			}
		}),
		ECVF_Cheat));
}

void UElysiumNpcSubsystem::OnMapEpochRetired(uint64 Epoch)
{
	// Each record's UPROPERTY mesh/anim/asset would otherwise keep the outgoing map's whole preview
	// body resident for the session, long after its actor was torn down with the world.
	ClearNpcs();
}

void UElysiumNpcSubsystem::Deinitialize()
{
	if (MapEpochRetiredHandle.IsValid())
	{
		if (UElysiumMapSubsystem* Maps = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr)
		{
			Maps->OnMapEpochRetired().Remove(MapEpochRetiredHandle);
		}
		MapEpochRetiredHandle.Reset();
	}
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

	// One body off the baked mount -- the same one the game NPC bodies stand.
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
	if (Mesh == nullptr)
	{
		OutError = FString::Printf(
			TEXT("'%s' is not on the baked mount -- run: uv run elysium export characters"), *Stem);
		return nullptr;
	}

	// Animation. A named clip resolves through the manifest, so the harness reaches the whole
	// resolved vocabulary (~1,540 clips per NPC) and not just the handful the body's own model
	// carries -- which is the point: an NPC's own clips are mostly dialogue, and idle/locomotion/
	// combat live in shared banks. With no name, the same default-idle policy the game uses picks
	// one, so what the harness stands matches what a map stands. Failure leaves the mesh in its
	// ref pose.
	UAnimSequence* Anim = nullptr;
	FString AppliedAnim;
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;

	FString Want = AnimName;
	if (Want.IsEmpty() && Anims != nullptr)
	{
		EElysiumIdleTier Tier = EElysiumIdleTier::None;
		Want = Anims->PickIdleClip(Stem, FString(), Tier);
		if (!Want.IsEmpty())
		{
			UE_LOG(LogElysiumNpc, Display, TEXT("npc.load: default idle for %s is '%s' (%s)"),
				*Stem, *Want, UElysiumAnimSubsystem::TierName(Tier));
		}
	}
	if (!Want.IsEmpty())
	{
		FString AnimError;
		if (Anims != nullptr)
		{
			Anim = Anims->ResolveClip(Stem, Want, Mesh, AnimError);
		}
		if (Anim != nullptr)
		{
			AppliedAnim = Want;
		}
		else
		{
			UE_LOG(LogElysiumNpc, Warning, TEXT("npc.load: clip '%s' on %s: %s"),
				*Want, *Stem, *AnimError);
		}
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
	// The NATIVE host rather than the graph, installed before the component registers so it owns the
	// pose from frame one: a preview body stands one named clip and nothing publishes a selection for
	// it, so the clip player is the whole path it needs. The preview body carries no A/B against the
	// single-node instance because a single-node instance has no facial track at all: the face is
	// the reason this harness needs the real host.
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
	Actor->SetRootComponent(Component);
	Component->RegisterComponent();
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Component->GetAnimInstance()))
	{
		Inst->SetFacialRig(Anims ? Anims->GetFacialRig(Stem, Mesh) : nullptr);
		Inst->SetCompositionRig(Anims ? Anims->GetCompositionRig(Stem, Mesh) : nullptr);
		if (Anim != nullptr)
		{
			// No identity: a preview body stands one named clip and publishes no phase, so the event
			// pass sees a channel standing on nothing. Deliberate — the green room is not a game
			// body, and firing a footstep out of a Content Browser stand would be a fault.
			Inst->PlayClip(FElysiumClipIdentity(), Anim, /*bLoop=*/true);
		}
	}
	else if (Anim != nullptr)
	{
		Component->PlayAnimation(Anim, /*bLooping=*/true);
	}
	ElysiumNpcVisual::InstallHairDynamics(Component, Stem);

	const double LoadMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

	PruneDead();

	// The clips this body OWNS, off the resolved vocabulary rather than off a parsed glb: the
	// model's own dialogue, and the only one whose names are
	// short enough to be a row of buttons. Everything else it can play belongs to a shared bank.
	TArray<FString> OwnClips;
	if (const FElysiumNpcClipSet* Set = Anims != nullptr ? Anims->GetClipSet(Stem) : nullptr)
	{
		Set->Clips.ForEachClip([&OwnClips, &Stem](const FString& Label, const FElysiumNpcClip& Clip)
		{
			if (Clip.IsOwnedBy(Stem))
			{
				OwnClips.AddUnique(Label);
			}
		});
		OwnClips.Sort([](const FString& A, const FString& B) { return A < B; });
	}

	FElysiumLoadedNpc Record;
	Record.Actor = Actor;
	Record.Mesh = Mesh;
	Record.Anim = Anim;
	Record.Stem = Stem;
	Record.AnimName = AppliedAnim;
	Record.NumBones = Mesh->GetRefSkeleton().GetNum();
	Record.NumAnims = OwnClips.Num();
	Record.AnimNames = OwnClips;
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

	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	UAnimSequence* Anim = nullptr;
	if (Anims != nullptr)
	{
		// Resolves the owning stem through the manifest — the NPC's own for a dialogue clip, a
		// shared bank otherwise — and loads that owner's baked sequence off the mount.
		Anim = Anims->ResolveClip(Record.Stem, ClipName, Record.Mesh, OutError);
	}
	if (Anim == nullptr)
	{
		OutError = FString::Printf(TEXT("'%s' on %s: %s"), *ClipName, *Record.Stem,
			OutError.IsEmpty() ? TEXT("no animation subsystem") : *OutError);
		return false;
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Component->GetAnimInstance()))
	{
		// No identity, for the same reason the stand above carries none: `npc.play` is a preview
		// verb, and a preview body publishes no clip phase.
		Inst->PlayClip(FElysiumClipIdentity(), Anim, /*bLoop=*/true);
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
