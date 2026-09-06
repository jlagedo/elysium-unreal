#include "ElysiumSkyProvenance.h"
#include "Visual/ElysiumMapVisuals.h"

#include "ElysiumBakedTags.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumDetailPropActor.h"
#include "ElysiumEffectActor.h"
#include "ElysiumEffectFamilies.h"
#include "ElysiumSpriteActor.h"
#include "ElysiumSpriteComponent.h"
#include "ElysiumFog.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMapTransportSettings.h"
#include "ElysiumReflections.h"
#include "ElysiumWaterVolumes.h"
#include "Visual/ElysiumLightRig.h"
#include "Visual/ElysiumMaterialFactory.h"
#include "Visual/ElysiumRopes.h"

#include "CableComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumVisuals, Log, All);

// Build the map's overhead cables (1) or skip them (0), for A/B. Read at map load, so re-travel
// (elysium.reload) to toggle.
static TAutoConsoleVariable<int32> CVarRopes(
	TEXT("elysium.Ropes"), 1,
	TEXT("Build the map's cables from <map>.ropes (1) or skip (0). Applied at map load."),
	ECVF_Default);

// A debug multiplier on the 2D backdrop's texel, default 1 = parity (D7).
// VtMB writes a sky texel to the framebuffer unscaled and unfogged — the whole material is
// `mul r0, t0, v0` against a modulation the engine forces to white (sky-ambience.md -> "K7 ...
// (settled)") — so any value but 1 is a stated divergence, not a calibration. A night-sky lift
// goes through the D3 post-process knobs, never through here. Live: re-applies to the backdrop
// as it changes, so an A/B needs no reload.
static TAutoConsoleVariable<float> CVarSkyBrightness(
	TEXT("elysium.SkyBrightness"), 1.f,
	TEXT("Debug multiplier on the sky backdrop texel. 1 = parity with VtMB's identity transfer."),
	ECVF_Default);

// Source's distance fog, on (1) or off (0), for A/B. It is a per-primitive material term rather
// than the height fog actor because the world and the 3D-skybox miniature carry two different
// fogs and share screen depth — the reasoning and its measurement are in ElysiumFog.h. Live:
// ApplySceneFog re-stamps every primitive as it changes, so an A/B needs no reload. A decal takes
// the same set through UElysiumDecalSubsystem (R7.2 ruling 4) rather than a custom-data slot: a
// UDecalComponent is a USceneComponent and carries no custom primitive data, so its fog lives on a
// load-time MID the subsystem owns.
static TAutoConsoleVariable<int32> CVarFog(
	TEXT("elysium.Fog"), 1,
	TEXT("Apply the map's authored distance fog (1) or none (0). World and 3D-skybox miniature "
	     "take their own sets, from worldspawn and sky_camera."),
	ECVF_Default);

namespace
{
	// Half-extent of the backdrop box, centred on the map actor. It only has to enclose every
	// map (the largest is a few hundred metres) plus the 3D-skybox miniature blown up 16x.
	constexpr float SkyDomeHalfExtentCm = 500000.f;

	// Where the world's height fog stops. RE-A9 is categorical that VtMB's 2D backdrop is never
	// fogged — every sky face in the game carries `$nofog 1`, which the shader turns into
	// FogMode(0) — but our backdrop is ordinary opaque geometry, and Unreal's deferred fog pass
	// fogs by depth alone, so without this the fog inscatters straight into the sky (measured on
	// sm_hub_1: the sky's displayed mean goes 16.6 -> 29.1). Epic's own documented use for the
	// cutoff is exactly this. It sits far outside anything real — the world and the miniature
	// both live within a few hundred metres — and far inside the dome, so no camera position
	// inside the map can put the two on the wrong sides of it.
	constexpr float FogCutoffCm = SkyDomeHalfExtentCm * 0.5f;

	// A cube of half-extent H centred on the origin, as PMC section arrays. The sky material
	// is two-sided and samples by view direction, so winding, normals, and UVs are unused —
	// the box just has to surround the camera. ~5 km keeps the tutorial map well inside it.
	void BuildSkyBox(float H, TArray<FVector>& Verts, TArray<int32>& Tris,
		TArray<FVector>& Normals, TArray<FVector2D>& UVs)
	{
		Verts = {
			{-H, -H, -H}, {H, -H, -H}, {H, H, -H}, {-H, H, -H},
			{-H, -H,  H}, {H, -H,  H}, {H, H,  H}, {-H, H,  H} };
		const int32 Quads[6][4] = {
			{0, 1, 2, 3}, {7, 6, 5, 4}, {4, 5, 1, 0}, {3, 2, 6, 7}, {1, 5, 6, 2}, {4, 0, 3, 7} };
		for (const int32(&Q)[4] : Quads)
		{
			Tris.Append({ Q[0], Q[1], Q[2], Q[0], Q[2], Q[3] });
		}
		Normals.Init(FVector::UpVector, Verts.Num());
		UVs.Init(FVector2D::ZeroVector, Verts.Num());
	}
}

UElysiumMapVisuals::UElysiumMapVisuals()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UElysiumMapVisuals::BeginPlay()
{
	Super::BeginPlay();

	// The real-time light rig, standing before the map builds so the Lights Cog window has
	// something to bind to from frame one.
	if (AActor* Owner = GetOwner())
	{
		LightRig = NewObject<UElysiumLightRig>(Owner, TEXT("LightRig"));
		LightRig->SetupAttachment(this);
		LightRig->RegisterComponent();
	}

	// The backdrop multiplier is an A/B knob, so a console flip has to reach the sky already
	// built. Weak-bound: the callback drops out with the component at map teardown.
	CVarSkyBrightness.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable*) { ApplySkyBrightness(); }));

	// Same for the fog A/B: it re-stamps the primitives already placed.
	CVarFog.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable*) { ApplySceneFog(); }));
}

int32 UElysiumMapVisuals::AdoptBakedLevel(const FString& MapName, const FElysiumSkyDef& SkyDef)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	WorldActors.Reset();
	SkyActors.Reset();
	PropActors.Reset();
	DetailActors.Reset();
	SpriteActors.Reset();
	SpritesByEntity.Reset();
	EffectActors.Reset();
	EffectsByEntity.Reset();
	WaterVolumes = nullptr;
	WaterVolumeCount = 0;
	LightStylePrimitiveCount = 0;
	EffectCount = 0;
	EffectSkyCount = 0;
	SpriteCount = 0;
	SpriteGlowCount = 0;
	SpriteSkyCount = 0;
	DetailSkyComponentCount = 0;
	RuntimeWorldBrushes.Reset();
	RuntimeSkyBrushes.Reset();
	SkyLight = nullptr;
	HeightFog = nullptr;
	PostProcess = nullptr;
	BakedSkyDomeActor = nullptr;
	DecalCount = 0;
	// Filled by the walk below and handed to UElysiumDecalSubsystem once, at its end.
	TArray<UDecalComponent*> BakedDecals;

	// One pass over the level. A light's `.lights` line index rides a second tag, so the rig can
	// bind each actor back to the source row it re-derives intensity and reach from.
	TArray<UElysiumLightRig::FAdoptedLight> Adopted;
	// R7.4 (G6, owner decision 4): the chunks whose FACES carry a VtMB lightstyle. The bake splits
	// them out by (material, style) and tags the chunk `elysium.style=n` -- on the component where
	// it tagged the section, on the actor otherwise, and both spellings are read here so neither
	// half of the bake contract can go quietly unread. The rig's style clock then writes their
	// brightness into custom primitive data slot 6 every tick.
	TArray<UElysiumLightRig::FStyledPrimitive> Styled;
	auto CollectStyled = [&Styled](AActor* Actor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Component : Primitives)
		{
			if (Component == nullptr)
			{
				continue;
			}
			int32 Style = ElysiumBakedTags::ParseLightStyle(Component->ComponentTags);
			if (Style == 0)
			{
				Style = ElysiumBakedTags::ParseLightStyle(Actor->Tags);
			}
			if (Style > 0)
			{
				Styled.Add({ Component, Style });
			}
		}
	};
	int32 Tagged = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor->Tags.Num() == 0)
		{
			continue;
		}
		// The class tags first (R6.7): a detail or sprite actor inside the 3D-skybox miniature also
		// carries `elysium.sky` as its scope marker, and the static-mesh sky bucket must never
		// see it (`ElysiumBakedTags.h`).
		if (Actor->ActorHasTag(ElysiumBakedTags::Detail))
		{
			if (AElysiumDetailPropActor* DetailActor = Cast<AElysiumDetailPropActor>(Actor))
			{
				DetailActors.Add(DetailActor);
				DetailSkyComponentCount += ElysiumBakedTags::InMiniature(Actor->Tags) ? 1 : 0;
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Sprite))
		{
			// R6.1: bucketed by the entity index the tag carries, so an `env_sprite` input
			// reaches its billboard by the one number both halves share.
			if (AElysiumSpriteActor* SpriteActor = Cast<AElysiumSpriteActor>(Actor))
			{
				SpriteActors.Add(SpriteActor);
				const int32 EntityIndex = ElysiumBakedTags::ParseEntityIndex(Actor->Tags);
				if (EntityIndex != INDEX_NONE)
				{
					SpritesByEntity.Add(EntityIndex, SpriteActor);
				}
				if (SpriteActor->Sprite && SpriteActor->Sprite->IsGlow())
				{
					++SpriteGlowCount;
				}
				SpriteSkyCount += ElysiumBakedTags::InMiniature(Actor->Tags) ? 1 : 0;
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Effect))
		{
			// R7.3: bucketed by the entity index its tag carries, the same number the leaf's
			// `ApplyEmitter` / `ApplyDust` / `ApplySteam` / `ApplyBeam` publishes arrive by.
			if (AElysiumEffectActor* Effect = Cast<AElysiumEffectActor>(Actor))
			{
				EffectActors.Add(Effect);
				const int32 EntityIndex = ElysiumBakedTags::ParseEntityIndex(Actor->Tags);
				if (EntityIndex != INDEX_NONE)
				{
					EffectsByEntity.Add(EntityIndex, Effect);
				}
				EffectSkyCount += ElysiumBakedTags::InMiniature(Actor->Tags) ? 1 : 0;
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::World))
		{
			WorldActors.Add(Cast<AStaticMeshActor>(Actor));
			CollectStyled(Actor);
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Sky))
		{
			SkyActors.Add(Cast<AStaticMeshActor>(Actor));
			CollectStyled(Actor);
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Prop))
		{
			PropActors.Add(Cast<AStaticMeshActor>(Actor));
			CollectStyled(Actor);
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Light))
		{
			if (ALight* Light = Cast<ALight>(Actor))
			{
				// R5.6: the type and style tags are only ever written by the V2 bake; on a
				// legacy-lane actor they read as their defaults and `Adopt` ignores them anyway.
				Adopted.Add({ Light->GetLightComponent(),
					ElysiumBakedTags::ParseSourceIndex(Actor->Tags),
					ElysiumBakedTags::ParseLightType(Actor->Tags),
					ElysiumBakedTags::ParseLightStyle(Actor->Tags) });
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::SkyLight))
		{
			if (ASkyLight* Sky = Cast<ASkyLight>(Actor))
			{
				SkyLight = Sky->GetLightComponent();
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Fog))
		{
			if (AExponentialHeightFog* Fog = Cast<AExponentialHeightFog>(Actor))
			{
				HeightFog = Fog->GetComponent();
				// The world's fog, and only the world's: the 2D backdrop is exempt game-wide
				// (sky-ambience B8 / RE-A9), and a deferred fog pass has no other way to say so.
				HeightFog->SetFogCutoffDistance(FogCutoffCm);
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::PostProcess))
		{
			PostProcess = Cast<APostProcessVolume>(Actor);
			if (PostProcess)
			{
				// The runtime owns how the volume applies, the same way it owns every light's
				// values: the bake only places the actor. Unbound and full weight, so the knobs
				// reach the camera wherever it is — a bounded volume would silently do nothing
				// outside its brush, which is indistinguishable from a knob that does not work.
				PostProcess->bEnabled = true;
				PostProcess->bUnbound = true;
				PostProcess->BlendWeight = 1.f;
			}
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Decal))
		{
			// R7.2 ruling 4: this walk is the only walk. The decal subsystem is the single owner
			// of every decal in the world, so the components go to it here and nothing else ever
			// iterates the level looking for them again.
			TInlineComponentArray<UDecalComponent*> Decals(Actor);
			BakedDecals.Append(Decals);
			++DecalCount;
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::SkyDome))
		{
			BakedSkyDomeActor = Cast<AStaticMeshActor>(Actor);
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Water))
		{
			// R7.1: one actor carrying every `water.volumes[]` row, so this is the whole adoption —
			// the actor registers its own post-process volume at BeginPlay and answers the map
			// actor's three point queries per frame.
			WaterVolumes = Cast<AElysiumWaterVolumes>(Actor);
		}
		else
		{
			continue;
		}
		++Tagged;
	}

	// A Cast that failed leaves a null slot; drop those rather than null-check at every use.
	WorldActors.RemoveAll([](const TObjectPtr<AStaticMeshActor>& A) { return A == nullptr; });
	SkyActors.RemoveAll([](const TObjectPtr<AStaticMeshActor>& A) { return A == nullptr; });
	PropActors.RemoveAll([](const TObjectPtr<AStaticMeshActor>& A) { return A == nullptr; });
	DetailActors.RemoveAll([](const TObjectPtr<AElysiumDetailPropActor>& A) { return A == nullptr; });

	WorldSurfaceCount = WorldActors.Num();
	SkySurfaceCount = SkyActors.Num();
	PropInstanceCount = PropActors.Num();
	// Unique models behind those instances, so the stat still reads as it did on the runtime path.
	TSet<const UStaticMesh*> Models;
	for (const TObjectPtr<AStaticMeshActor>& Prop : PropActors)
	{
		if (const UStaticMeshComponent* Comp = Prop->GetStaticMeshComponent())
		{
			Models.Add(Comp->GetStaticMesh());
		}
	}
	PropModelCount = Models.Num();

	// R6.3: a detail actor is one model's whole placement set; the instance count is the
	// component's own, written by the bake, so the stat reads the lump's record count back.
	DetailInstanceCount = 0;
	TSet<const UStaticMesh*> DetailModels;
	for (const TObjectPtr<AElysiumDetailPropActor>& Detail : DetailActors)
	{
		if (const UInstancedStaticMeshComponent* Instances = Detail->Instances)
		{
			DetailInstanceCount += Instances->GetInstanceCount();
			DetailModels.Add(Instances->GetStaticMesh());
		}
	}
	DetailModelCount = DetailModels.Num();
	SpriteCount = SpriteActors.Num();
	EffectCount = EffectActors.Num();
	WaterVolumeCount = WaterVolumes ? WaterVolumes->Volumes.Num() : 0;

	// R7.3 (§5.5): the family match is re-resolved at adopt so a data-asset edit needs no re-bake.
	if (EffectActors.Num() > 0)
	{
		if (!EffectFamilies)
		{
			EffectFamilies = LoadObject<UElysiumEffectFamilies>(nullptr, ElysiumEffectAssets::Families);
		}
		for (const TObjectPtr<AElysiumEffectActor>& Effect : EffectActors)
		{
			Effect->ResolveFamily(EffectFamilies);
		}
	}

	// R7.2 ruling 4: hand the baked decals to their one owner. A bake cannot save a
	// UMaterialInstanceDynamic into a level, so the fog term `M_V2_Decal` declares only exists
	// once the subsystem parents an MID to each bound instance -- and `ApplySceneFog` (run right
	// after, from ApplyEnvironment) is what puts this map's numbers on them.
	if (UElysiumDecalSubsystem* Decals = World->GetSubsystem<UElysiumDecalSubsystem>())
	{
		Decals->AdoptBaked(BakedDecals);
	}

	if (LightRig)
	{
		// R5.6: a converted map's actors already carry every derived value, so the rig snapshots
		// them (`AdoptBaked`); every other map re-derives from `<map>.lights` exactly as before.
		WorldLightCount = ElysiumMapTransport::IsMapOnV2Models(MapName)
			? LightRig->AdoptBaked(Adopted, MapName)
			: LightRig->Adopt(Adopted, FElysiumContentPaths::MapLights(MapName), SkyDef.Scale);
		// R7.4: the styled chunks ride the same clock the styled lights do, so they are handed over
		// beside them -- one walk, one adopt.
		LightStylePrimitiveCount = LightRig->AdoptStyledPrimitives(Styled);
	}

	if (Tagged == 0)
	{
		UE_LOG(LogElysiumVisuals, Warning,
			TEXT("'%s' has no baked actors — this world is not a baked level (run: uv run elysium export map %s --force)"),
			*MapName, *MapName);
	}
	// The PPV is the one adopted actor whose absence is silent — the D3 knobs simply stop
	// working — so its presence is stated rather than inferred.
	// R7.4 (contract 3): the styled-chunk count rides the same line. A map whose bake wrote
	// `elysium.style=<n>` chunks and whose runtime adopted none is the one failure mode of that
	// contract that renders as "nothing happened" rather than as an error.
	UE_LOG(LogElysiumVisuals, Log,
		TEXT("adopted '%s': %d tagged actors, %d styled chunks, ppv %s"),
		*MapName, Tagged, LightStylePrimitiveCount, PostProcess ? TEXT("yes") : TEXT("MISSING"));
	return Tagged;
}

void UElysiumMapVisuals::AuditMaterials(const FString& MapName) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The engine's own stand-in for "no material". A null slot is swapped for it at draw time with
	// nothing logged, so the two spellings of the same bake defect have to be reported together —
	// and an instance whose root material is the fallback is the same defect one level down.
	const UMaterial* const Fallback = UMaterial::GetDefaultMaterial(MD_Surface);

	// Keyed by the drawn asset: an unbound slot belongs to the mesh the bake wrote, not to the
	// hundred components that place it. Slots are unioned across instances because a runtime
	// SetMaterial override is per component, so two placements of one mesh can disagree.
	struct FMeshAudit
	{
		int32 Instances = 0;
		TMap<int32, FString> BadSlots;
	};
	TMap<FString, FMeshAudit> Meshes;
	int32 ComponentsWalked = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UMeshComponent*> Components(*It);
		for (const UMeshComponent* Component : Components)
		{
			// A hidden component draws nothing, so its bindings are not a look defect: the walkable
			// surface's displacement collider and a physics prop's invisible proxy both stand on
			// material-less or neutral geometry by design.
			if (Component == nullptr || !Component->IsVisible())
			{
				continue;
			}

			// A component drawing an asset is reported by that asset's path; one that builds its
			// geometry at runtime (a brush, a rope) has no asset to name, so it stands for itself.
			const UObject* Asset = nullptr;
			if (const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
			{
				Asset = StaticMesh->GetStaticMesh();
			}
			else if (const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component))
			{
				Asset = Skinned->GetSkinnedAsset();
			}
			const FString Key = Asset ? Asset->GetPathName() : Component->GetPathName();

			++ComponentsWalked;
			FMeshAudit& Audit = Meshes.FindOrAdd(Key);
			++Audit.Instances;

			const TArray<FName> SlotNames = Component->GetMaterialSlotNames();
			const int32 SlotCount = Component->GetNumMaterials();
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				const UMaterialInterface* Bound = Component->GetMaterial(Slot);
				const bool bDefaultBound = Bound != nullptr
					&& (Bound == Fallback || Bound->GetMaterial_Concurrent() == Fallback);
				if (Bound != nullptr && !bDefaultBound)
				{
					continue;
				}
				const FName SlotName = SlotNames.IsValidIndex(Slot) ? SlotNames[Slot] : NAME_None;
				Audit.BadSlots.Add(Slot, FString::Printf(TEXT("%d:%s=%s"), Slot,
					SlotName.IsNone() ? TEXT("<unnamed>") : *SlotName.ToString(),
					bDefaultBound ? TEXT("default") : TEXT("null")));
			}
		}
	}

	// Sorted so two runs over the same map produce the same list to diff.
	TArray<FString> Offenders;
	for (const TPair<FString, FMeshAudit>& Pair : Meshes)
	{
		if (Pair.Value.BadSlots.Num() > 0)
		{
			Offenders.Add(Pair.Key);
		}
	}
	Offenders.Sort();

	for (const FString& Key : Offenders)
	{
		const FMeshAudit& Audit = Meshes[Key];
		TArray<int32> Slots;
		Audit.BadSlots.GetKeys(Slots);
		Slots.Sort();
		TArray<FString> Labels;
		Labels.Reserve(Slots.Num());
		for (const int32 Slot : Slots)
		{
			Labels.Add(Audit.BadSlots[Slot]);
		}
		UE_LOG(LogElysiumVisuals, Warning,
			TEXT("material audit %s: '%s' has %d unbound slot(s) [%s] across %d placed instance(s)"),
			*MapName, *Key, Labels.Num(), *FString::Join(Labels, TEXT(", ")), Audit.Instances);
	}

	// Always stated, so a clean map says the audit ran rather than saying nothing at all.
	const FString Summary = FString::Printf(
		TEXT("material audit %s: %d mesh assets audited over %d components, %d with unbound or default-bound slots"),
		*MapName, Meshes.Num(), ComponentsWalked, Offenders.Num());
	if (Offenders.Num() > 0)
	{
		UE_LOG(LogElysiumVisuals, Warning, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogElysiumVisuals, Log, TEXT("%s"), *Summary);
	}
}

void UElysiumMapVisuals::BuildRopes(const FString& MapName)
{
	RopeCount = 0;
	AActor* Owner = GetOwner();
	if (Owner == nullptr || CVarRopes.GetValueOnGameThread() == 0)
	{
		return;
	}

	TArray<FElysiumRopeDef> Defs;
	if (!FElysiumRopes::Parse(FElysiumContentPaths::MapRopes(MapName), Defs) || Defs.Num() == 0)
	{
		return;
	}

	// One MID per distinct material id (every cable/cable rope shares one): the `MI_` the material
	// lane imported for the unit, resolved by the R5.4 naming rule (`FElysiumContentPaths::
	// BakedMaterial`) and wrapped once through `FElysiumMaterialFactory::Create`. The instance
	// carries the texture, the normal map and the shader mode — `cable/chain`/`chainb` are
	// `$alphatest` over a texture ~47% cut out, and the `MI_`'s own Masked blend is what keeps the
	// gaps between the links open. An id whose asset does not load is a warning naming the path
	// and a cable left on the engine default: never a quiet fallback to another master.
	TMap<FString, UMaterialInstanceDynamic*> MidById;
	int32 Unresolved = 0;
	auto MidFor = [&](const FElysiumRopeDef& D) -> UMaterialInstanceDynamic*
	{
		if (UMaterialInstanceDynamic** Found = MidById.Find(D.MaterialId))
		{
			return *Found;
		}
		const FString AssetPath = FElysiumContentPaths::BakedMaterial(D.MaterialId);
		UMaterialInterface* Imported = AssetPath.IsEmpty()
			? nullptr
			: LoadObject<UMaterialInterface>(nullptr, *AssetPath);
		if (Imported == nullptr)
		{
			UE_LOG(LogElysiumVisuals, Warning,
				TEXT("ropes: material '%s' resolves to '%s', which did not load (run: uv run elysium import materials)"),
				*D.MaterialId, *AssetPath);
			++Unresolved;
		}
		UMaterialInstanceDynamic* Mid = FElysiumMaterialFactory::Create(Imported, this);
		MidById.Add(D.MaterialId, Mid);
		return Mid;
	};

	Ropes.Reserve(Defs.Num());
	for (const FElysiumRopeDef& D : Defs)
	{
		UCableComponent* Cable = NewObject<UCableComponent>(Owner);
		Cable->SetupAttachment(this);
		// The map actor sits at the origin, so a component-relative location is the world point.
		// Start is fixed at A (the component's own location). The end needs care: EndLocation is
		// resolved against `AttachEndTo.GetComponent(GetOwner())`, and with AttachEndTo unset
		// FComponentReference does NOT return null — `ExtractComponent` falls back to the owner's
		// ROOT component (EngineTypes.cpp), i.e. the map actor's SceneRoot, not the cable. (The stock
		// CableActor never notices because its cable IS the root.) SceneRoot sits at identity, so
		// EndLocation is in world space: it must be B itself. A `B - A` offset here reads as an
		// absolute point near the world origin and drags every cable's far end into one spot.
		Cable->SetRelativeLocation(D.A);
		Cable->EndLocation = D.B;
		Cable->bAttachStart = true;
		// `Dangling` clears ROPE_LOCK_END_POINT, so the far end swings free instead of being
		// pinned to B.
		Cable->bAttachEnd = (D.Flags & FElysiumRopeDef::Dangling) == 0;
		// The sidecar already carries the RE'd rest length, so it goes in unmodified: the surplus
		// over the straight A→B distance is the whole sag. VtMB's `RecomputeSprings` subtracts a
		// flat 100 units from it, which at VtMB's authored slack (0..100 game-wide) usually puts
		// the rest length at or *below* the span — those cables hang taut, and the solver simply
		// stretches them along the chord.
		Cable->CableLength = D.RestCm;
		Cable->CableWidth = FMath::Max(0.1f, D.WidthCm);
		Cable->NumSides = 4;                                           // thin round tube
		// VtMB simulates `Nodes` points, so `Nodes - 1` spans. The Type-2 case (Nodes == 2) is
		// load-bearing: one span between two locked points is a straight line and cannot sag,
		// which is how the taut steel cables and hanging-lamp chains are meant to read.
		Cable->NumSegments = FMath::Max(1, D.Nodes - 1);
		// Enough relaxation passes that the shape settles on its catenary instead of hanging
		// somewhere short of it — the sag then follows from the authored slack alone.
		Cable->SolverIterations = 8;
		// Tile the strand texture along the cable so it does not stretch: repeats scale with length
		// (metres) times the authored TextureScale. Measure the drawn strand, not the rest length —
		// a taut cable is drawn along the chord, which is longer.
		const float Dist = static_cast<float>(FVector::Dist(D.A, D.B));
		Cable->TileMaterial = FMath::Max(0.1f, (FMath::Max(Dist, D.RestCm) / 100.f) * D.TexScale);
		if (UMaterialInstanceDynamic* Mid = MidFor(D))
		{
			Cable->SetMaterial(0, Mid);
		}
		Cable->RegisterComponent();
		Ropes.Add(Cable);
	}
	RopeCount = Ropes.Num();
	UE_LOG(LogElysiumVisuals, Log, TEXT("ropes: %d cables over %d material(s), %d unresolved"),
		RopeCount, MidById.Num(), Unresolved);
}

void UElysiumMapVisuals::ApplyEnvironment(const FElysiumEnvDef& Env, const FString& MapName)
{
	// `Env` already resolved (asset or sidecar) by the caller
	// (`ElysiumMapEnvironmentSource::Load`), which logs which source answered.
	EnvDef = Env;

	// Before anything sky-shaped: the fog belongs to every primitive in the level, including on
	// the 65 maps with no sky_camera and the ones whose faces did not decode.
	ApplySceneFog();

	if (ElysiumMapTransport::IsMapOnV2Models(MapName))
	{
		// R5.2: this map's SkyLight (`SLS_SpecifiedCubemap`, the real baked cube, the real
		// `emit_skyambient`-joined intensity) and its backdrop dome are already standing —
		// `pipeline/unreal/bake_map.py::_place_sky` binds the texture-lane cube and stored mean,
		// the same assets this function resolves below for maps still on the legacy path.
		// Nothing here would improve on that; re-running it would
		// silently fight the baked asset the next time something calls `RecaptureSky`.
		UE_LOG(LogElysiumVisuals, Log, TEXT("sky '%s': baked (MapsOnV2Models) — runtime assembly skipped"),
			*Env.SkyName);
		return;
	}

	if (!Env.bSky)
	{
		// No sky faces to build a cube from — but the SkyLight's *level* is still data (D2), and
		// leaving it on the bake's placeholder is exactly the cubemap-less constant fill this
		// policy exists to remove. A map with no sky pair gets zero; one that somehow has a pair
		// but no faces has no cube to scale, which SkyAmbientIntensity reports as zero too.
		if (SkyLight)
		{
			SkyLight->SetIntensity(SkyAmbientIntensity(0.f));
			SkyLight->SetMobility(EComponentMobility::Movable);
			SkyLight->RecaptureSky();
		}
		return;
	}

	// The faces are read verbatim under the export-side orientation contract; a sidecar written
	// under a different one would assemble into a silently wrong cube.
	if (Env.SkyConvention != ElysiumEnvironment::SkyConventionVersion)
	{
		UE_LOG(LogElysiumVisuals, Warning,
			TEXT("sky '%s': .env states orientation convention %d, this build assembles %d"),
			*Env.SkyName, Env.SkyConvention, ElysiumEnvironment::SkyConventionVersion);
	}

	// D1: the texture lane conserves the GLB faces/mips and stores the sky mean.
	const FString CubePath = FElysiumContentPaths::BakedUnit(
		TEXT("vtmb:texture:skybox/") + Env.SkyName.ToLower(), TEXT("TC"), TEXT("Sky"));
	UTextureCube* Cube = LoadObject<UTextureCube>(nullptr, *CubePath);
	const UElysiumSkyProvenance* Provenance = UElysiumSkyProvenance::Find(Cube);
	if (!Provenance || Provenance->SkyName != Env.SkyName.ToLower()
		|| !FMath::IsFinite(Provenance->UpperHemisphereMean) || Provenance->UpperHemisphereMean < 0.0)
	{
		UE_LOG(LogElysiumVisuals, Warning, TEXT("sky '%s': missing/invalid texture-lane composite %s"),
			*Env.SkyName, *CubePath);
		return;
	}
	const float CubeUpperMean = static_cast<float>(Provenance->UpperHemisphereMean);

	// The cube does two jobs. As the SkyLight's IBL source it is what gives Lumen real sky
	// occlusion: an interior stops receiving ambient because it cannot see the sky, instead of
	// being washed by a constant fill through solid walls (the cubemap-less SLS_SpecifiedCubemap
	// this replaced). VtMB night skies integrate to nearly nothing, which is the point — the
	// .lights rig and Lumen's bounce off the baked surface cache carry the room now.
	if (SkyLight)
	{
		SkyLight->SourceType = SLS_SpecifiedCubemap;
		SkyLight->Cubemap = Cube;
		// A sky that lit the undersides of the world would defeat the occlusion above.
		SkyLight->bLowerHemisphereIsBlack = true;
		SkyLight->SetMobility(EComponentMobility::Movable);
		SkyLight->SetIntensity(SkyAmbientIntensity(CubeUpperMean));
		SkyLight->RecaptureSky();
	}

	// And as the visible backdrop: a huge two-sided box around the camera sampling the cube by
	// view direction, so it reads as infinitely far. Excluded from ray tracing — a mesh that
	// encloses the whole scene is the canonical Lumen hardware-ray-tracing overlap cost.
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}
	if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		*FElysiumContentPaths::Material(TEXT("M_Sky"))))
	{
		SkyMid = UMaterialInstanceDynamic::Create(Master, this);
		UMaterialInstanceDynamic* Mid = SkyMid;
		Mid->SetTextureParameterValue(TEXT("SkyCube"), Cube);
		ApplySkyBrightness();

		// The backdrop mesh is built here rather than with the component: a map with no sky
		// (65 of them) has no cube to sample and gets no dome at all.
		if (SkyDomeMesh == nullptr)
		{
			SkyDomeMesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("SkyDomeMesh"));
			SkyDomeMesh->SetupAttachment(this);
			SkyDomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SkyDomeMesh->SetCastShadow(false);
			SkyDomeMesh->RegisterComponent();
		}

		TArray<FVector> Verts, Normals;
		TArray<int32> Tris;
		TArray<FVector2D> UVs;
		BuildSkyBox(SkyDomeHalfExtentCm, Verts, Tris, Normals, UVs);
		SkyDomeMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, {}, {}, false);
		SkyDomeMesh->SetMaterial(0, Mid);
		SkyDomeMesh->SetVisibleInRayTracing(false);
		SkyDomeMesh->SetVisibility(bSkyVisible);
		UE_LOG(LogElysiumVisuals, Log,
			TEXT("sky '%s': faithful faces, cube upper-hemisphere mean %.5f, skyambient %.5f -> "
			     "SkyLight intensity %.3f"),
			*Env.SkyName,
			CubeUpperMean, LightRig ? LightRig->SkyAmbientMag : 0.f,
			SkyAmbientIntensity(CubeUpperMean));
	}
}

float UElysiumMapVisuals::SkyAmbientIntensity(float CubeUpperMean) const
{
	// C1/C2 (D2, docs/vtmb/sky-ambience.md): the SkyLight actor stays on every map, and its level
	// is DATA, not a constant. VtMB states the sky's own radiance once per map, as the type-5
	// `emit_skyambient` row — the colour its light cache returns for a sky-hitting bounce ray
	// (RE-A3) — and VRAD divides no falloff out of a `light_environment`, so that number is a
	// lump-8 luxel value / 255 (RE-A5). It is authored on only **25 of 108 maps**; the other 83
	// carry no `light_environment` at all, and 41 of those still draw sky through `toolsskybox`,
	// so "shows sky" and "is lit by sky" are genuinely independent. Two of the 25 author a
	// **zero**. So the policy has exactly three cases and no fallback:
	//
	//   no pair (83 maps)  -> 0. The rig and Lumen's bounce carry the room; a sky that shows but
	//                         was never authored to light must not light.
	//   pair, magnitude 0  -> 0. An authored zero is a reading, not a missing one.
	//   pair, magnitude m  -> scale the cube so its own average radiance IS m.
	//
	// That last line is the whole of C1. VtMB's sky is one number and ours is an image; dividing
	// the cube's solid-angle-weighted upper-hemisphere mean out and multiplying VtMB's number in
	// gives the sky VtMB's **level** while keeping the cube's **direction** — the modernization
	// (a real IBL with real occlusion) sits entirely in the distribution, and the magnitude stays
	// the game's own. No free gain, which is what makes C4's residual a measurement.
	const float Mag = LightRig ? LightRig->SkyAmbientMag : 0.f;
	if (!LightRig || !LightRig->bHasSkyAmbient || Mag <= 0.f)
	{
		return 0.f;
	}
	if (CubeUpperMean <= KINDA_SMALL_NUMBER)
	{
		// A cube that integrates to nothing (an all-black `dn`-like set) cannot be scaled to a
		// target radiance — the ratio diverges. Say so rather than ship an infinity.
		UE_LOG(LogElysiumVisuals, Warning,
			TEXT("sky: type-5 magnitude %.5f but the cube's upper hemisphere is black — no IBL level"),
			Mag);
		return 0.f;
	}
	return Mag / CubeUpperMean;
}

void UElysiumMapVisuals::ApplySkyBrightness()
{
	if (SkyMid)
	{
		SkyMid->SetScalarParameterValue(TEXT("Brightness"), CVarSkyBrightness.GetValueOnGameThread());
	}
}

void UElysiumMapVisuals::ApplySceneFog()
{
	// The bake already stamped these, so the level looks right the moment it is opened. The
	// runtime re-derives them anyway, the same way it re-derives every light's intensity: the
	// numbers belong to `<map>.env`, and a sidecar re-export must not need a re-bake to take.
	const bool bOn = CVarFog.GetValueOnGameThread() != 0;

	TArray<float> WorldData, SkyData;
	ElysiumFog::Pack(bOn && EnvDef.bFog, EnvDef.FogColor, EnvDef.FogStartCm, EnvDef.FogEndCm,
		WorldData);
	// The miniature's distances arrive already multiplied by the 3D-skybox scale, because the
	// pass renders at 1/scale — a skybox-space distance is `scale` times as far in the world.
	ElysiumFog::Pack(bOn && EnvDef.bSkyFog, EnvDef.SkyFogColor, EnvDef.SkyFogStartCm,
		EnvDef.SkyFogEndCm, SkyData);

	auto Stamp = [](const TArray<TObjectPtr<AStaticMeshActor>>& Actors, const TArray<float>& Data)
	{
		int32 Count = 0;
		for (const TObjectPtr<AStaticMeshActor>& Actor : Actors)
		{
			if (UStaticMeshComponent* Comp = Actor ? Actor->GetStaticMeshComponent() : nullptr)
			{
				Comp->SetCustomPrimitiveDataFloatArray(ElysiumFog::SlotColor, Data);
				++Count;
			}
		}
		return Count;
	};

	const int32 WorldStamped = Stamp(WorldActors, WorldData) + Stamp(PropActors, WorldData);
	const int32 SkyStamped = Stamp(SkyActors, SkyData);
	auto StampComponents = [](const TArray<TObjectPtr<UStaticMeshComponent>>& Components,
		const TArray<float>& Data)
	{
		int32 Count = 0;
		for (UStaticMeshComponent* Comp : Components)
		{
			if (Comp)
			{
				Comp->SetCustomPrimitiveDataFloatArray(ElysiumFog::SlotColor, Data);
				++Count;
			}
		}
		return Count;
	};
	const int32 RuntimeWorldStamped = StampComponents(RuntimeWorldBrushes, WorldData);
	const int32 RuntimeSkyStamped = StampComponents(RuntimeSkyBrushes, SkyData);
	// R6.7: a detail component takes the set its scope marker names -- the `sky_camera`'s
	// inside the miniature, `worldspawn`'s everywhere else -- so a `.env` re-export or the
	// elysium.Fog A/B reaches the instanced grass exactly as it reaches the chunks and props.
	int32 DetailWorldStamped = 0;
	int32 DetailSkyStamped = 0;
	for (const TObjectPtr<AElysiumDetailPropActor>& Detail : DetailActors)
	{
		if (UInstancedStaticMeshComponent* Instances = Detail ? Detail->Instances.Get() : nullptr)
		{
			const bool bSky = ElysiumBakedTags::InMiniature(Detail->Tags);
			Instances->SetCustomPrimitiveDataFloatArray(ElysiumFog::SlotColor, bSky ? SkyData : WorldData);
			(bSky ? DetailSkyStamped : DetailWorldStamped) += 1;
		}
	}

	// R7.3: an effect takes its set by the same marker, through the system's fog pins
	// (`AElysiumEffectActor::ApplyFog`) rather than primitive data -- a Niagara renderer's
	// material is reached through its parameter binding, not a custom-data slot.
	int32 EffectWorldStamped = 0;
	int32 EffectSkyStamped = 0;
	for (const TObjectPtr<AElysiumEffectActor>& Effect : EffectActors)
	{
		if (!Effect)
		{
			continue;
		}
		const bool bSky = ElysiumBakedTags::InMiniature(Effect->Tags);
		Effect->ApplyFog(bOn && (bSky ? EnvDef.bSkyFog : EnvDef.bFog),
			bSky ? EnvDef.SkyFogColor : EnvDef.FogColor,
			bSky ? EnvDef.SkyFogStartCm : EnvDef.FogStartCm,
			bSky ? EnvDef.SkyFogEndCm : EnvDef.FogEndCm);
		(bSky ? EffectSkyStamped : EffectWorldStamped) += 1;
	}

	// R7.2 ruling 4: the map's decals, adopted and laid alike, through their one owner. A decal is
	// only ever a world surface (`mat_fog.fog_from_params`), so it takes worldspawn's set -- never
	// the miniature's -- and it takes it as three named instance parameters on an MID, because a
	// UDecalComponent carries no custom primitive data.
	int32 DecalsStamped = 0;
	if (UWorld* World = GetWorld())
	{
		if (UElysiumDecalSubsystem* Decals = World->GetSubsystem<UElysiumDecalSubsystem>())
		{
			DecalsStamped = Decals->ApplyFog(bOn && EnvDef.bFog, EnvDef.FogColor, EnvDef.FogStartCm,
				EnvDef.FogEndCm);
		}
	}

	auto Describe = [](const TArray<float>& Data)
	{
		return Data[ElysiumFog::SlotInvRange] > 0.f
			? FString::Printf(TEXT("%.0f->%.0fcm"), Data[ElysiumFog::SlotStart],
				Data[ElysiumFog::SlotStart] + 1.f / Data[ElysiumFog::SlotInvRange])
			: FString(TEXT("off"));
	};
	UE_LOG(LogElysiumVisuals, Log,
		TEXT("fog: world %s on %d primitives, 3D skybox %s on %d, %d decal MID(s)"),
		*Describe(WorldData), WorldStamped + RuntimeWorldStamped + DetailWorldStamped + EffectWorldStamped,
		*Describe(SkyData), SkyStamped + RuntimeSkyStamped + DetailSkyStamped + EffectSkyStamped,
		DecalsStamped);
}

void UElysiumMapVisuals::RegisterRuntimeBrush(UStaticMeshComponent* Comp, bool bSky)
{
	if (!Comp)
	{
		return;
	}
	(bSky ? RuntimeSkyBrushes : RuntimeWorldBrushes).Add(Comp);
	// R7.4 (G6): a brush entity's own faces can carry a lightstyle -- `sm_pier_1`'s 17
	// `objects/surf` foam bodies are the census's motivating case, and they are the only
	// lightstyle-bearing water geometry in the corpus. They are never placed by the bake, so they
	// carry no `elysium.style=` actor tag; the style rides the mesh's slot names instead and is
	// handed to the same clock the baked chunks ride (`ElysiumLightStyle::StyleFromSlotNames`).
	if (LightRig != nullptr)
	{
		if (const UStaticMesh* Mesh = Comp->GetStaticMesh())
		{
			TArray<FName> SlotNames;
			SlotNames.Reserve(Mesh->GetStaticMaterials().Num());
			for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
			{
				SlotNames.Add(Slot.MaterialSlotName);
			}
			const int32 Style = ElysiumLightStyle::StyleFromSlotNames(SlotNames);
			if (Style > 0 && LightRig->AddStyledPrimitive(Comp, Style))
			{
				Comp->ComponentTags.AddUnique(ElysiumBakedTags::LightStyle(Style));
				++LightStylePrimitiveCount;
				// The adoption line above is written before any entity is embodied, so this is
				// the only place the brush half of the contract is observable at all.
				UE_LOG(LogElysiumVisuals, Log,
					TEXT("styled brush body: '%s' animates on style %d (%d styled primitives)"),
					*Mesh->GetName(), Style, LightStylePrimitiveCount);
			}
		}
	}
	ApplySceneFog();
}

AElysiumEffectActor* UElysiumMapVisuals::FindEffectActor(int32 EntityIndex) const
{
	const TWeakObjectPtr<AElysiumEffectActor>* Found = EffectsByEntity.Find(EntityIndex);
	return Found ? Found->Get() : nullptr;
}

bool UElysiumMapVisuals::SetSpriteVisible(int32 EntityIndex, bool bShown)
{
	const TWeakObjectPtr<AElysiumSpriteActor>* Found = SpritesByEntity.Find(EntityIndex);
	AElysiumSpriteActor* Actor = Found ? Found->Get() : nullptr;
	if (Actor == nullptr)
	{
		return false;
	}
	Actor->SetActorHiddenInGame(!bShown);
	return true;
}

void UElysiumMapVisuals::ToggleProps()
{
	bPropsVisible = !bPropsVisible;
	for (const TObjectPtr<AStaticMeshActor>& Prop : PropActors)
	{
		Prop->SetActorHiddenInGame(!bPropsVisible);
	}
	// The detail props are props to the eye, so the one toggle covers both placement families.
	for (const TObjectPtr<AElysiumDetailPropActor>& Detail : DetailActors)
	{
		Detail->SetActorHiddenInGame(!bPropsVisible);
	}
}

void UElysiumMapVisuals::ToggleSkybox()
{
	// Both halves of the sky read as one thing to the eye, so they toggle together: the baked
	// 3D-skybox miniature and the backdrop dome that stands in for the 2D sky behind it.
	bSkyVisible = !bSkyVisible;
	for (const TObjectPtr<AStaticMeshActor>& Sky : SkyActors)
	{
		Sky->SetActorHiddenInGame(!bSkyVisible);
	}
	// R6.7: the miniature's detail components and sprites are the miniature too.
	for (const TObjectPtr<AElysiumDetailPropActor>& Detail : DetailActors)
	{
		if (Detail && ElysiumBakedTags::InMiniature(Detail->Tags))
		{
			Detail->SetActorHiddenInGame(!bSkyVisible);
		}
	}
	for (const TObjectPtr<AElysiumSpriteActor>& SpriteActor : SpriteActors)
	{
		if (SpriteActor && ElysiumBakedTags::InMiniature(SpriteActor->Tags))
		{
			SpriteActor->SetActorHiddenInGame(!bSkyVisible);
		}
	}
	for (const TObjectPtr<AElysiumEffectActor>& Effect : EffectActors)
	{
		if (Effect && ElysiumBakedTags::InMiniature(Effect->Tags) && !Effect->IsKilled())
		{
			Effect->SetActorHiddenInGame(!bSkyVisible);
		}
	}
	if (SkyDomeMesh)
	{
		SkyDomeMesh->SetVisibility(bSkyVisible && SkyDomeMesh->GetNumSections() > 0);
	}
	if (BakedSkyDomeActor)
	{
		BakedSkyDomeActor->SetActorHiddenInGame(!bSkyVisible);
	}
}

void UElysiumMapVisuals::ToggleLights()
{
	if (LightRig)
	{
		LightRig->SetLightsVisible(!LightRig->AreLightsVisible());
	}
}

bool UElysiumMapVisuals::AreLightsVisible() const
{
	return LightRig && LightRig->AreLightsVisible();
}

// The 3D-skybox A/B is a **dev** verb, not a player one, so it lives on plane 1: an `elysium.*`
// console command, reached by a chord (Ctrl+T) rather than by a bare key — `t` is `toggleuiside` in
// VtMB's default set (`docs/architecture/input-architecture.md` § "Reserved keys").
static FAutoConsoleCommandWithWorld GElysiumToggleSky(
	TEXT("elysium.togglesky"),
	TEXT("Show/hide the 3D skybox miniature and the backdrop dome together."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
		AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
		if (UElysiumMapVisuals* Visuals = Map ? Map->GetVisuals() : nullptr)
		{
			Visuals->ToggleSkybox();
		}
	}));
