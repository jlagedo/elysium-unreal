#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPropSkins.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumBodies, Log, All);

// The NPC animation host. 1 = UElysiumNpcAnimInstance (two sequence players + a crossfade), 0 =
// Unreal's single-node instance, which cannot blend, so every clip change pops. Applied at map
// load, per body.
static TAutoConsoleVariable<int32> CVarNpcAnim(
	TEXT("elysium.NpcAnim"), 1,
	TEXT("NPC animation host: the crossfading Elysium anim instance (1) or single-node (0). Applied at map load."),
	ECVF_Default);

// A/B toggle for the prop skin pass (8.3/8.4). 1 applies alternate skin families; 0 leaves every
// prop on its authored materials, so a look change can be attributed. Read per apply, so it takes
// effect on the next Skin input without a reload.
static TAutoConsoleVariable<int32> CVarPropSkins(
	TEXT("elysium.PropSkins"), 1,
	TEXT("Apply alternate prop skin families (1, default) or keep every prop on skin 0 (0)."),
	ECVF_Default);

UElysiumEntityBodies::UElysiumEntityBodies()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAnimSequence* UElysiumEntityBodies::ResolveNpcClip(const FString& Stem, const FString& ClipName)
{
	if (Stem.IsEmpty() || ClipName.IsEmpty())
	{
		return nullptr;
	}
	// Keyed by stem AND clip: one UAnimSequence is bound to one skeleton, so the same bank clip
	// resolves separately per NPC model. A null entry is a remembered miss.
	const FString Key = Stem + TEXT("|") + ClipName;
	if (const TObjectPtr<UAnimSequence>* Cached = NpcAnimCache.Find(Key))
	{
		return Cached->Get();
	}

	UAnimSequence* Anim = nullptr;
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const TObjectPtr<USkeletalMesh>* Mesh = NpcMeshCache.Find(Stem);
	const TObjectPtr<UglTFRuntimeAsset>* Own = NpcAssetCache.Find(Stem);
	if (Anims != nullptr && Mesh != nullptr && *Mesh != nullptr)
	{
		FString Error;
		Anim = Anims->ResolveClip(Stem, ClipName, Mesh->Get(), Own ? Own->Get() : nullptr, Error);
		if (Anim == nullptr)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("npc '%s' clip '%s': %s"), *Stem, *ClipName, *Error);
		}
	}
	NpcAnimCache.Add(Key, Anim);
	return Anim;
}

bool UElysiumEntityBodies::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	UAnimSequence* Anim = Body ? ResolveNpcClip(Stem, ClipName) : nullptr;
	if (Anim == nullptr)
	{
		return false;
	}
	if (OutSeconds != nullptr)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);   // elysium.NpcAnim 0 — single-node A/B, no crossfade
	}
	return true;
}

bool UElysiumEntityBodies::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	if (Body == nullptr || Anims == nullptr)
	{
		return false;
	}
	EElysiumIdleTier Tier = EElysiumIdleTier::None;
	const FString Clip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// Cache-checked load: mesh per stem, so a shared model (three Sabbat share shovelhead) loads
	// once. A stem that failed once is not cached (Mesh stays null), so it retries — cheap, and a
	// genuinely missing glb is a one-line warning per NPC, not per frame.
	const TObjectPtr<USkeletalMesh>* Cached = NpcMeshCache.Find(Stem);
	USkeletalMesh* Mesh = Cached ? Cached->Get() : nullptr;
	if (Mesh == nullptr)
	{
		UglTFRuntimeAsset* Asset = nullptr;
		FString Error;
		Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, Error);
		if (Mesh == nullptr)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("BuildNpcVisual '%s': %s"), *Stem, *Error);
			return nullptr;
		}
		NpcMeshCache.Add(Stem, Mesh);
		NpcAssetCache.Add(Stem, Asset);   // the NPC's own clips are retargeted off this
	}

	// The standing idle. Two NPCs sharing a model can carry different dispositions and different
	// variants, so the pick is per (stem, disposition, variant) — but the resolved clip caches per
	// (stem, clip), so a crowd spread across three stance idles still resolves three sequences,
	// not one per NPC.
	FString IdleClip;
	if (UGameInstance* GI = Owner->GetGameInstance())
	{
		if (UElysiumNpcAnimSubsystem* Anims = GI->GetSubsystem<UElysiumNpcAnimSubsystem>())
		{
			EElysiumIdleTier Tier = EElysiumIdleTier::None;
			IdleClip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
			UE_LOG(LogElysiumBodies, Verbose, TEXT("npc '%s' idle: %s (%s, disposition '%s', variant %d)"),
				*Stem, IdleClip.IsEmpty() ? TEXT("<none>") : *IdleClip,
				UElysiumNpcAnimSubsystem::TierName(Tier),
				Disposition.IsEmpty() ? TEXT("<unset>") : *Disposition, IdleVariant);
		}
	}

	// Standard runtime-component recipe (mirrors BuildBrushBody): NewObject → attach → place →
	// RegisterComponent. The hulls-body path uses relative placement against the root at world origin;
	// NPC origins are the same Unreal-space verbatim values, so relative == world here.
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocation(Location);
	Comp->SetRelativeRotation(Rotation);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	// The animation host is installed before the first clip, so it owns the pose from frame one and
	// every later change (stance, gesture, scripted sequence) crossfades instead of popping.
	// `elysium.NpcAnim 0` drops back to the single-node instance for an A/B.
	if (CVarNpcAnim.GetValueOnGameThread() != 0)
	{
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	}
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // no AI, no physics body (B3)
	Owner->AddInstanceComponent(Comp);
	if (!IdleClip.IsEmpty())
	{
		PlayNpcClip(Comp, Stem, IdleClip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
	}
	return Comp;
}

// The bake already produced every prop model as a real asset — Nanite, compressed textures, and the
// DDC-fitted Lumen cards a runtime-built mesh can never have — so an entity prop stands the same mesh
// the level's static props do. Cached per stem so a model placed by several entities resolves once; a
// stem that failed is not cached, so it retries.
UStaticMesh* UElysiumEntityBodies::ResolvePropMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = PropMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedPropMesh(MapName, Stem));
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("prop '%s': no baked mesh (run: bake.bat %s props)"),
			*Stem, *MapName);
		return nullptr;
	}
	PropMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	// Standard runtime-component recipe (mirrors BuildNpcVisual): NewObject → mesh → attach → place →
	// register. The map actor sits at the origin, so relative == world for these Unreal-space values.
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	// prop_dynamic is visual-only (8.3); prop_physics owns collision (8.4). The baked mesh carries
	// collision geometry for the props that do need it, so it is switched off here per component.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

void UElysiumEntityBodies::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
	if (!Mesh || CVarPropSkins.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!bPropSkinsLoaded)
	{
		bPropSkinsLoaded = true;
		PropSkins = LoadObject<UElysiumPropSkinSet>(
			nullptr, *FElysiumContentPaths::BakedPropSkins(MapName));
	}

	// Restore first, so a swap back to skin 0 -- or to a family this model does not carry, which
	// Source draws as the authored set -- undoes whatever the previous skin painted. Clearing the
	// overrides puts every slot back on the mesh's own material.
	Comp->EmptyOverrideMaterials();
	const FElysiumSkinFamily* Row = PropSkins ? PropSkins->Find(FName(*Stem), Family) : nullptr;
	if (!Row)
	{
		return;
	}

	for (const FElysiumSkinOverride& Override : Row->Overrides)
	{
		if (!Override.Material)
		{
			continue;
		}
		// Every prop body stands a baked mesh, whose slots the bake named safe_name(material) --
		// the same key the skin table is written with, so the slot name resolves directly.
		const int32 Slot = Mesh->GetMaterialIndex(Override.SlotName);
		if (Slot != INDEX_NONE)
		{
			Comp->SetMaterial(Slot, Override.Material);
		}
	}
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// The same baked asset every other prop stands: the bake gave a physics model VtMB's own
	// convex collision (props/<stem>.phys, one shape per `.phy` ledge) and its authored mass on
	// the body setup, under CTF_UseSimpleAndComplex — so one mesh serves both a simulating body
	// and a static placement of the same model, and the cache is shared with BuildPropVisual.
	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// Collide as a physics body (blocks the world's BlockAll hull colliders). Simulation, mass and
	// the elysium.PhysicsProps gate are the leaf's call — the body stands here inert until it decides.
	Comp->SetCollisionProfileName(TEXT("PhysicsActor"));
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}
