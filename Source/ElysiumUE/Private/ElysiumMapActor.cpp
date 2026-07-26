#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBakedTags.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumEnvironment.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSoundScheme.h"
#include "ElysiumLightRig.h"
#include "ElysiumMaterialFactory.h"
#include "ElysiumNpcVisual.h"
#include "ElysiumObjModel.h"
#include "ElysiumPropSkins.h"
#include "ElysiumRopes.h"
#include "ElysiumTextureCache.h"

#include "Engine/GameInstance.h"

#include "Animation/AnimSequence.h"
#include "CableComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Light.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

// Build the world collider from the pipeline's brush sidecars (.hulls convex + .dispcol trimesh)
// (1), or leave the map with no walkable surface (0, for debugging noclip flythroughs). Brush
// collision matches retail: it includes the invisible PLAYERCLIP volumes and drops collision on
// geometry the designer clipped off. Read at map load, so re-travel to apply a value.
static TAutoConsoleVariable<int32> CVarBrushCollision(
	TEXT("elysium.BrushCollision"), 1,
	TEXT("Build the .hulls/.dispcol world collider (1) or skip it (0). Applied at map load."),
	ECVF_Default);

// Build the map's overhead cables (1) or skip them (0), for A/B. Read at map load, so re-travel
// (elysium.reload) to toggle.
static TAutoConsoleVariable<int32> CVarRopes(
	TEXT("elysium.Ropes"), 1,
	TEXT("Build the map's cables from <map>.ropes (1) or skip (0). Applied at map load."),
	ECVF_Default);

// A debug multiplier on the 2D backdrop's texel, default 1 = parity (decisions.md 2026-07-26,
// D7). VtMB writes a sky texel to the framebuffer unscaled and unfogged — the whole material is
// `mul r0, t0, v0` against a modulation the engine forces to white (sky-ambience.md -> "K7 ...
// (settled)") — so any value but 1 is a stated divergence, not a calibration. A night-sky lift
// goes through the D3 post-process knobs, never through here. Live: re-applies to the backdrop
// as it changes, so an A/B needs no reload.
static TAutoConsoleVariable<float> CVarSkyBrightness(
	TEXT("elysium.SkyBrightness"), 1.f,
	TEXT("Debug multiplier on the sky backdrop texel. 1 = parity with VtMB's identity transfer."),
	ECVF_Default);

// The offline enhancement track's A/B (docs/asset-enhancement.md), off by default: prefer the
// super-resolved `tex_hi/` set over the faithful `tex/` decode wherever a map has one. The sky
// is its first consumer; the world/prop texture path joins it as that track lands. Faithful is
// the default everywhere, per the direction charter — this is an opt-in layer, not a
// replacement. Read at map load; elysium.reload to apply.
static TAutoConsoleVariable<int32> CVarEnhancedTextures(
	TEXT("elysium.EnhancedTextures"), 0,
	TEXT("Prefer the offline-enhanced tex_hi/ texture set (1) over the faithful decode (0). "
	     "Applied at map load."),
	ECVF_Default);

// The two Lumen art-direction knobs D3 sanctions, as live cvars over the map's baked
// PostProcessVolume. **Negative = neutral**, which is the shipped state: the override is
// cleared rather than set to a nominal default, so "we are not touching this" and "we set it to
// what it would have been" stay distinguishable. A non-neutral value is a divergence and wants
// a dated per-map decision (decisions.md); these exist so C4/C5 can measure whether one is
// justified without a rebuild.
//
// Skylight Leaking is the sanctioned replacement for VtMB's load-bearing author fill — the
// soft lights it sprays where a real bounce would have come from, which Lumen cannot reproduce
// where there is nothing in the room to bounce off.
//
// The bounce-strength knob is **Lumen Diffuse Color Boost**, not Indirect Lighting Intensity.
// D3 named the latter, but on this render path it does nothing: it reaches the shaders as
// `View.PrecomputedIndirectLightingColorScale`, which scales *precomputed* indirect lighting
// only, and no shader under Lumen/ reads it — measured, an IndirectLightingIntensity of 3
// changes not one pixel here. `LumenDiffuseColorBoost` is Lumen's own control
// (`LumenDiffuseColorBoost.ush`: `pow(DiffuseLum, Boost)`, so **below 1 brightens** and 1 is
// neutral) and it raises the albedo the bounce sees, which is the term VtMB's look actually
// lives on.
//
// Ambient Cubemap is deliberately absent: a flat occlusion-ignoring term is the contrast-killer
// both Epic and the direction charter warn against.
static TAutoConsoleVariable<float> CVarSkylightLeaking(
	TEXT("elysium.SkylightLeaking"), -1.f,
	TEXT("Lumen skylight leaking on the map's PPV (0..1). Negative = neutral (no override)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarSkylightLeakingDistance(
	TEXT("elysium.SkylightLeakingDistance"), -1.f,
	TEXT("Distance (cm) over which skylight leaking reaches full strength. Negative = neutral."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarDiffuseColorBoost(
	TEXT("elysium.LumenDiffuseBoost"), -1.f,
	TEXT("Lumen diffuse colour boost on the map's PPV: pow(albedo, boost), so below 1 brightens "
	     "the bounce and 1 is neutral. Negative = neutral (no override)."),
	ECVF_Default);

// Assemble the sky cube from the labelled RE-A2 probe faces instead of the map's own (0/1).
// Each face states its suffix, the axis it belongs on, which way is up and which face each of
// its edges meets, so a wrong slice binding, a rotation, a mirror and a broken seam are four
// visibly different failures. It is the acceptance check on the B3 assembly, run against the
// same six images the shipped VtMB engine drew for RE-A2 — so both ends of the orientation
// chain are checked with one set of faces. Read at map load; elysium.reload to apply.
static TAutoConsoleVariable<int32> CVarSkyProbe(
	TEXT("elysium.SkyProbe"), 0,
	TEXT("Build the sky cube from the labelled tools/out/_skyprobe faces (1) or the map's own (0). "
	     "Applied at map load."),
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

AElysiumMapActor::AElysiumMapActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Convex world collision from .hulls: no render sections (never drawn), simple = convex.
	// One FKConvexElem per solid brush, so pawn capsule sweeps (which query simple collision)
	// hit the brushes and their invisible clip volumes. Cooked async (hundreds of synchronous
	// Chaos cooks stall the game thread); the spawn teleport waits for ground (see Tick).
	HullCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HullCollision"));
	HullCollision->SetupAttachment(SceneRoot);
	HullCollision->bUseComplexAsSimpleCollision = false;
	HullCollision->bUseAsyncCooking = true;
	HullCollision->SetCollisionProfileName(TEXT("BlockAll"));

	// Displacement terrain collision from .dispcol: one invisible complex-as-simple trimesh
	// section, so capsule sweeps hit the concave terrain the convex hulls don't represent.
	DispCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DispCollision"));
	DispCollision->SetupAttachment(SceneRoot);
	DispCollision->bUseComplexAsSimpleCollision = true;
	DispCollision->bUseAsyncCooking = true;
	DispCollision->SetCollisionProfileName(TEXT("BlockAll"));
	DispCollision->SetVisibility(false);

	// 2D skybox backdrop. Unlit and drawn on a huge box that surrounds the camera; the
	// sky material samples the cube by view direction, so it reads as infinitely far. No
	// collision, no shadows. Hidden until a map's sky faces build the cubemap.
	SkyDomeMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyDomeMesh"));
	SkyDomeMesh->SetupAttachment(SceneRoot);
	SkyDomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDomeMesh->SetCastShadow(false);
	SkyDomeMesh->SetVisibility(false);

	// The sun, sky light and height fog are actors in the baked level, adopted in LoadMap —
	// this actor owns no lighting components of its own.

	// Real-time light rig: adopts the baked level's light actors (built in LoadMap).
	LightRig = CreateDefaultSubobject<UElysiumLightRig>(TEXT("LightRig"));
	LightRig->SetupAttachment(SceneRoot);
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();

	// The backdrop multiplier is an A/B knob, so a console flip has to reach the sky already
	// built. Weak-bound: the callback drops out with the actor at map teardown.
	CVarSkyBrightness.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable*) { ApplySkyBrightness(); }));

	// The D3 knobs are for finding a value by eye against a live scene, so they apply as they
	// change rather than at load.
	const FConsoleVariableDelegate Knobs = FConsoleVariableDelegate::CreateWeakLambda(this,
		[this](IConsoleVariable*) { ApplyPostProcessKnobs(); });
	CVarSkylightLeaking.AsVariable()->SetOnChangedCallback(Knobs);
	CVarSkylightLeakingDistance.AsVariable()->SetOnChangedCallback(Knobs);
	CVarDiffuseColorBoost.AsVariable()->SetOnChangedCallback(Knobs);

	LoadMap();
}

void AElysiumMapActor::LoadMap()
{
	const double Start = FPlatformTime::Seconds();

	// Per-phase timing for the Maps Cog window: stamp closes the running phase and opens the next.
	LoadPhases.Reset();
	double PhaseStart = Start;
	auto Phase = [this, &PhaseStart](const TCHAR* Name)
	{
		const double Now = FPlatformTime::Seconds();
		LoadPhases.Add({ Name, (Now - PhaseStart) * 1000.0 });
		PhaseStart = Now;
	};

	LoadedMap = MapName;

	// B7 — the 3D-skybox miniature's placement transform (`<map>.sky`), read first because three
	// later steps need it: the light rig scales a miniature source's reach by it, the `.ents`
	// parser carries sky-scope entities through it, and a miniature body takes its mesh scale
	// from it. The identity (scale 1) on the 65 maps with no `sky_camera`.
	SkyDef = FElysiumSkyDef();
	FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(MapName), SkyDef);

	// This map's texture dedup index. Must exist before the first material is built (the sky cube
	// below), and lives for the actor's lifetime so a runtime prop/NPC spawn reuses it.
	TextureCache = MakePimpl<FElysiumTextureCache>();

	// P1.7 — label the map actor and drop it in an Elysium Outliner folder, so the PIE World
	// Outliner reads as a live scene browser (debug-tooling.md Layer 0).
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Map:%s"), *MapName));
	SetFolderPath(TEXT("Elysium"));
#endif

	// The look is already here — this actor was spawned into the map's baked level. Take hold of
	// its actors so the runtime can address them, and hand the light rig its sources.
	const int32 Adopted = AdoptBakedLevel();
	Phase(TEXT("Adopt baked level"));

	// The walkable surface. Baked world geometry carries no gameplay collision, so the brush
	// sidecars are the only world collider: .hulls convex (which carries the invisible PLAYERCLIP
	// volumes and drops geometry the designer clipped off) plus the .dispcol displacement trimesh.
	bBrushCollision = CVarBrushCollision.GetValueOnGameThread() != 0 && LoadHulls();
	if (bBrushCollision)
	{
		LoadDispCol();
	}
	else
	{
		UE_LOG(LogElysium, Warning,
			TEXT("no brush collision for '%s' — the map has no walkable surface"), *MapName);
	}
	Phase(TEXT("Collision"));

	BuildRopes();
	Phase(TEXT("Ropes"));

	// Sky cubemap + backdrop, and the sky light's IBL off the same cube.
	ApplyEnvironment();
	ApplyPostProcessKnobs();
	Phase(TEXT("Environment"));

	UE_LOG(LogElysium, Log,
		TEXT("baked '%s': %d actors (%d world, %d sky, %d props, %d decals), %d lights, %d hulls, "
		     "ppv %s"),
		*MapName, Adopted, WorldActors.Num(), SkyActors.Num(), PropActors.Num(), DecalCount,
		WorldLightCount, HullCount, PostProcess ? TEXT("yes") : TEXT("MISSING"));

	if (ReadSpawn(PendingSpawnLoc, PendingSpawnYaw))
	{
		bSpawnPending = true;
	}

	// Track-B entity substrate (P1.4): parse `.ents`, build the live world, run the spawn pass.
	// Map-load ignition (OnMapLoad) is the logic_auto class's own first-think (P1.6), not a
	// separate pass. The world ticks from AElysiumMapActor::Tick.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			FElysiumEntityDefs EntDefs;
			if (FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(MapName), EntDefs,
				SkyDef.Scale, SkyDef.OriginCm))
			{
				EntityCount = EntDefs.Num();

				// P9 9.3 — import this map's `worldspawn.levelscript` module before anything can
				// evaluate against it. VtMB's own load order: the level script's top-level code
				// (constants like cCelerity, `from vamputil import *`, the On* defs) runs first,
				// then entities spawn and fire their field-6 payloads into that namespace.
				GameState->LoadLevelScript(EntDefs.LevelScriptModule());

				EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState);
				// The scheme manager must exist before the spawn pass: a start_enabled
				// ambient_soundscheme fades its scheme in from its own Spawn() (P6.3).
				SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();
				EntityWorld->Load(MoveTemp(EntDefs));
				BrushBodyCount = EntityWorld->NumBrushBodies();
			}
			else
			{
				UE_LOG(LogElysium, Log, TEXT("no %s.ents — entity world not built"), *MapName);
			}
		}
	}

	// P4.6 — a landmark transition places the player against the destination info_landmark instead of
	// info_player_start. Runs after the entity world is built (the landmark is one of its entities).
	ResolveLandmarkSpawn();

	Phase(TEXT("Entities"));

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("loaded %s in %.2fs"), *MapName, TotalMs / 1000.0);
}

int32 AElysiumMapActor::AdoptBakedLevel()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	WorldActors.Reset();
	SkyActors.Reset();
	PropActors.Reset();
	SkyLight = nullptr;
	HeightFog = nullptr;
	PostProcess = nullptr;
	DecalCount = 0;

	// One pass over the level. A light's `.lights` line index rides a second tag, so the rig can
	// bind each actor back to the source row it re-derives intensity and reach from.
	TArray<UElysiumLightRig::FAdoptedLight> Adopted;
	int32 Tagged = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor->Tags.Num() == 0)
		{
			continue;
		}
		if (Actor->ActorHasTag(ElysiumBakedTags::World))
		{
			WorldActors.Add(Cast<AStaticMeshActor>(Actor));
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Sky))
		{
			SkyActors.Add(Cast<AStaticMeshActor>(Actor));
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Prop))
		{
			PropActors.Add(Cast<AStaticMeshActor>(Actor));
		}
		else if (Actor->ActorHasTag(ElysiumBakedTags::Light))
		{
			if (ALight* Light = Cast<ALight>(Actor))
			{
				Adopted.Add({ Light->GetLightComponent(),
					ElysiumBakedTags::ParseSourceIndex(Actor->Tags) });
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
			++DecalCount;
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

	WorldLightCount = LightRig->Adopt(Adopted, FElysiumContentPaths::MapLights(MapName), SkyDef.Scale);

	if (Tagged == 0)
	{
		UE_LOG(LogElysium, Warning,
			TEXT("'%s' has no baked actors — this world is not a baked level (run: bake.bat %s)"),
			*MapName, *MapName);
	}
	return Tagged;
}

USkeletalMeshComponent* AElysiumMapActor::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale)
{
	USceneComponent* Root = GetRootComponent();
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// Cache-checked load: mesh + idle anim per stem, so a shared model (three Sabbat share shovelhead)
	// loads once. A stem that failed once is not re-cached (Mesh stays null), so it retries — cheap,
	// and a genuinely missing glb is a one-line warning per NPC, not per frame.
	const TObjectPtr<USkeletalMesh>* Cached = NpcMeshCache.Find(Stem);
	USkeletalMesh* Mesh = Cached ? Cached->Get() : nullptr;
	UAnimSequence* Idle = nullptr;
	if (Mesh == nullptr)
	{
		UglTFRuntimeAsset* Asset = nullptr;
		FString Error;
		Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, Error);
		if (Mesh == nullptr)
		{
			UE_LOG(LogElysium, Warning, TEXT("BuildNpcVisual '%s': %s"), *Stem, *Error);
			return nullptr;
		}
		FString AppliedAnim;
		Idle = ElysiumNpcVisual::LoadIdleAnim(Asset, Mesh, AppliedAnim);
		NpcMeshCache.Add(Stem, Mesh);
		NpcIdleCache.Add(Stem, Idle);   // may be null → reference pose; cached either way
	}
	else if (const TObjectPtr<UAnimSequence>* CachedIdle = NpcIdleCache.Find(Stem))
	{
		Idle = CachedIdle->Get();
	}

	// Standard runtime-component recipe (mirrors BuildBrushBody): NewObject → attach → place →
	// RegisterComponent. The hulls-body path uses relative placement against the root at world origin;
	// NPC origins are the same Unreal-space verbatim values, so relative == world here.
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocation(Location);
	Comp->SetRelativeRotation(Rotation);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // no AI, no physics body (B3)
	AddInstanceComponent(Comp);
	if (Idle != nullptr)
	{
		Comp->PlayAnimation(Idle, /*bLooping=*/true);
	}
	return Comp;
}

// The bake already produced every prop model as a real asset — Nanite, compressed textures, and the
// DDC-fitted Lumen cards a runtime-built mesh can never have — so an entity prop stands the same mesh
// the level's static props do. Cached per stem so a model placed by several entities resolves once; a
// stem that failed is not cached, so it retries.
UStaticMesh* AElysiumMapActor::ResolvePropMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = PropMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedPropMesh(MapName, Stem));
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysium, Warning, TEXT("prop '%s': no baked mesh (run: bake.bat %s props)"),
			*Stem, *MapName);
		return nullptr;
	}
	PropMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* AElysiumMapActor::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	USceneComponent* Root = GetRootComponent();
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
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
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
	AddInstanceComponent(Comp);
	return Comp;
}

// A/B toggle for the prop skin pass (8.3/8.4). 1 applies alternate skin families; 0 leaves every
// prop on its authored materials, so a look change can be attributed. Read per apply, so it takes
// effect on the next Skin input without a reload.
static TAutoConsoleVariable<int32> CVarPropSkins(
	TEXT("elysium.PropSkins"),
	1,
	TEXT("Apply alternate prop skin families (1, default) or keep every prop on skin 0 (0)."),
	ECVF_Default);

void AElysiumMapActor::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
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

UStaticMeshComponent* AElysiumMapActor::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	USceneComponent* Root = GetRootComponent();
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

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
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
	AddInstanceComponent(Comp);
	return Comp;
}

float AElysiumMapActor::BodyScaleFor(const FElysiumEntityDef& Def) const
{
	return Def.bSky ? SkyDef.Scale : 1.f;
}

void AElysiumMapActor::BuildRopes()
{
	RopeCount = 0;
	if (CVarRopes.GetValueOnGameThread() == 0)
	{
		return;
	}

	TArray<FElysiumRopeDef> Defs;
	if (!FElysiumRopes::Parse(FElysiumContentPaths::MapRopes(MapName), Defs) || Defs.Num() == 0)
	{
		return;
	}

	const FString Dir = FElysiumContentPaths::MapDir(MapName);

	// One MID per unique rope texture (every cable/cable rope shares one; the exporter names the PNG
	// after the material, so the albedo path identifies the material). Built through the same factory
	// the world/prop surfaces use, so the sidecar's matflags pick the master: `cable/chain`/`chainb`
	// are $alphatest over a texture that is ~47% cut out, and instancing them opaque fills the gaps
	// between the links in — a chain then reads as a solid tube with a chain painted on it. A "-" tex
	// (decode failed) yields a solid dark-cable fallback from the def's Kd colour.
	TMap<FString, UMaterialInstanceDynamic*> MidByTex;
	auto MidFor = [&](const FElysiumRopeDef& D) -> UMaterialInstanceDynamic*
	{
		if (UMaterialInstanceDynamic** Found = MidByTex.Find(D.Tex))
		{
			return *Found;
		}
		FElysiumMaterialDef MatDef;
		MatDef.Name = TEXT("rope");
		if (D.Tex != TEXT("-"))
		{
			MatDef.Albedo = D.Tex;
		}
		else
		{
			MatDef.Color = FLinearColor(0.05f, 0.05f, 0.05f);
		}
		if (D.Bump != TEXT("-"))
		{
			MatDef.Bump = D.Bump;
		}
		MatDef.bScissor = (D.MatFlags & FElysiumRopeDef::Masked) != 0;
		MatDef.bBlend = (D.MatFlags & FElysiumRopeDef::Translucent) != 0;
		// $envmap with no separate mask ($normalmapalphaenvmapmask) — the uniform-reflectivity path.
		MatDef.bEnvmap = (D.MatFlags & FElysiumRopeDef::Envmap) != 0;
		UMaterialInstanceDynamic* Mid = FElysiumMaterialFactory::Build(&MatDef, Dir, this, *TextureCache);
		MidByTex.Add(D.Tex, Mid);
		return Mid;
	};

	Ropes.Reserve(Defs.Num());
	for (const FElysiumRopeDef& D : Defs)
	{
		UCableComponent* Cable = NewObject<UCableComponent>(this);
		Cable->SetupAttachment(SceneRoot);
		// The map actor sits at the origin, so a component-relative location is the world point.
		// Start is fixed at A (the component's own location). The end needs care: EndLocation is
		// resolved against `AttachEndTo.GetComponent(GetOwner())`, and with AttachEndTo unset
		// FComponentReference does NOT return null — `ExtractComponent` falls back to the owner's
		// ROOT component (EngineTypes.cpp), i.e. SceneRoot, not the cable. (The stock CableActor
		// never notices because its cable IS the root.) SceneRoot sits at identity, so EndLocation
		// is in world space: it must be B itself. A `B - A` offset here reads as an absolute point
		// near the world origin and drags every cable's far end across the map into one spot.
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
	UE_LOG(LogElysium, Log, TEXT("ropes: %d cables"), RopeCount);
}

bool AElysiumMapActor::LoadHulls()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapHulls(MapName)))
	{
		return false;   // no .hulls sidecar: keep the render-mesh trimesh fallback
	}

	// Each line is one solid world brush as a flat, unordered point cloud in Unreal cm:
	// x y z x y z ...  (>= 4 verts). UE builds the convex hull from the points, so order is
	// irrelevant. The sidecar is pre-filtered at export to player-blocking contents
	// (SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP), so invisible clip brushes are in and passable
	// water/monsterclip is out.
	TArray<TArray<FVector>> Hulls;
	Hulls.Reserve(Lines.Num());
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 12 || Tok.Num() % 3 != 0)
		{
			continue;   // need >= 4 verts, whole (x,y,z) triples
		}
		TArray<FVector> Verts;
		Verts.Reserve(Tok.Num() / 3);
		for (int32 I = 0; I + 2 < Tok.Num(); I += 3)
		{
			Verts.Emplace(FCString::Atod(*Tok[I]), FCString::Atod(*Tok[I + 1]), FCString::Atod(*Tok[I + 2]));
		}
		Hulls.Add(MoveTemp(Verts));
	}
	if (Hulls.Num() == 0)
	{
		return false;
	}

	HullCount = Hulls.Num();
	// Set the whole convex set in one call: SetCollisionConvexMeshes replaces the elements and
	// cooks collision once (AddCollisionConvexMesh would re-cook per hull).
	HullCollision->SetCollisionConvexMeshes(MoveTemp(Hulls));
	UE_LOG(LogElysium, Log, TEXT("brush collision: %d convex hulls"), HullCount);
	return true;
}

void AElysiumMapActor::LoadDispCol()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapDispCol(MapName)))
	{
		return;   // no .dispcol: map has no displacements
	}

	// Each line is one collision triangle: 9 Unreal-cm floats = A,B,C. Concave terrain, so it
	// becomes one complex-as-simple trimesh section. Winding is irrelevant (Chaos trimesh is
	// two-sided). The section stays invisible (component visibility off).
	TArray<FVector> Verts;
	TArray<int32> Tris;
	Verts.Reserve(Lines.Num() * 3);
	Tris.Reserve(Lines.Num() * 3);
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() != 9)
		{
			continue;
		}
		const int32 Base = Verts.Num();
		for (int32 V = 0; V < 3; ++V)
		{
			Verts.Emplace(FCString::Atod(*Tok[V * 3]), FCString::Atod(*Tok[V * 3 + 1]), FCString::Atod(*Tok[V * 3 + 2]));
		}
		Tris.Add(Base);
		Tris.Add(Base + 1);
		Tris.Add(Base + 2);
	}
	if (Tris.Num() == 0)
	{
		return;
	}

	DispTriCount = Tris.Num() / 3;
	const TArray<FVector> NoNormals;
	const TArray<FVector2D> NoUVs;
	const TArray<FLinearColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	DispCollision->CreateMeshSection_LinearColor(0, Verts, Tris, NoNormals, NoUVs, NoColors, NoTangents, true);
	UE_LOG(LogElysium, Log, TEXT("displacement collision: %d triangles"), DispTriCount);
}

void AElysiumMapActor::ToggleProps()
{
	bPropsVisible = !bPropsVisible;
	for (const TObjectPtr<AStaticMeshActor>& Prop : PropActors)
	{
		Prop->SetActorHiddenInGame(!bPropsVisible);
	}
}

bool AElysiumMapActor::ArePropsVisible() const
{
	return bPropsVisible;
}

void AElysiumMapActor::ToggleSkybox()
{
	// Both halves of the sky read as one thing to the eye, so they toggle together: the baked
	// 3D-skybox miniature and the backdrop dome that stands in for the 2D sky behind it.
	bSkyVisible = !bSkyVisible;
	for (const TObjectPtr<AStaticMeshActor>& Sky : SkyActors)
	{
		Sky->SetActorHiddenInGame(!bSkyVisible);
	}
	if (SkyDomeMesh)
	{
		SkyDomeMesh->SetVisibility(bSkyVisible && SkyDomeMesh->GetNumSections() > 0);
	}
}

bool AElysiumMapActor::IsSkyboxVisible() const
{
	return bSkyVisible;
}

void AElysiumMapActor::ToggleLights()
{
	if (LightRig)
	{
		LightRig->SetLightsVisible(!LightRig->AreLightsVisible());
	}
}

bool AElysiumMapActor::AreLightsVisible() const
{
	return LightRig && LightRig->AreLightsVisible();
}

void AElysiumMapActor::ApplyEnvironment()
{
	FElysiumEnvDef Env;
	const bool bHaveEnv = FElysiumEnvDef::Parse(FElysiumContentPaths::MapEnv(MapName), Env);
	if (!bHaveEnv || !Env.bSky)
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
		UE_LOG(LogElysium, Warning,
			TEXT("sky '%s': .env states orientation convention %d, this build assembles %d"),
			*Env.SkyName, Env.SkyConvention, ElysiumEnvironment::SkyConventionVersion);
	}

	// Faces come from one of three sets: the labelled probe (B1), the enhanced set (B5), or the
	// faithful decode. The enhanced set is opt-in and per-map, so a map without one silently
	// keeps the faithful faces rather than losing its sky.
	const bool bProbe = CVarSkyProbe.GetValueOnGameThread() != 0 && !Env.SkyName.IsEmpty();
	const FString TexHi = FElysiumContentPaths::MapTexHiDir(MapName);
	const bool bEnhanced = !bProbe && CVarEnhancedTextures.GetValueOnGameThread() != 0
		&& ElysiumEnvironment::HasSkyFaces(TexHi, TEXT("sky_"));

	float CubeUpperMean = 0.f;
	UTextureCube* Cube = ElysiumEnvironment::BuildSkyCubeFrom(
		bProbe    ? FElysiumContentPaths::SkyProbeDir() :
		bEnhanced ? TexHi
		          : FElysiumContentPaths::MapTexDir(MapName),
		bProbe ? Env.SkyName : FString(TEXT("sky_")), &CubeUpperMean);
	if (Cube == nullptr)
	{
		if (bProbe)
		{
			UE_LOG(LogElysium, Warning,
				TEXT("elysium.SkyProbe: no labelled '%s' face set under %s — run tools/sky_probe.py"),
				*Env.SkyName, *FElysiumContentPaths::SkyProbeDir());
		}
		return;
	}

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
	if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/VtMB/Materials/M_Sky.M_Sky")))
	{
		SkyMid = UMaterialInstanceDynamic::Create(Master, this);
		UMaterialInstanceDynamic* Mid = SkyMid;
		Mid->SetTextureParameterValue(TEXT("SkyCube"), Cube);
		ApplySkyBrightness();

		TArray<FVector> Verts, Normals;
		TArray<int32> Tris;
		TArray<FVector2D> UVs;
		BuildSkyBox(SkyDomeHalfExtentCm, Verts, Tris, Normals, UVs);
		SkyDomeMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, {}, {}, false);
		SkyDomeMesh->SetMaterial(0, Mid);
		SkyDomeMesh->SetVisibleInRayTracing(false);
		SkyDomeMesh->SetVisibility(bSkyVisible);
		UE_LOG(LogElysium, Log,
			TEXT("sky '%s': %s faces, cube upper-hemisphere mean %.5f, skyambient %.5f -> "
			     "SkyLight intensity %.3f"),
			*Env.SkyName,
			bProbe ? TEXT("labelled probe") : bEnhanced ? TEXT("enhanced") : TEXT("faithful"),
			CubeUpperMean, LightRig ? LightRig->SkyAmbientMag : 0.f,
			SkyAmbientIntensity(CubeUpperMean));
	}
}

float AElysiumMapActor::SkyAmbientIntensity(float CubeUpperMean) const
{
	// C1/C2 (D2, decisions.md 2026-07-26): the SkyLight actor stays on every map, and its level
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
		UE_LOG(LogElysium, Warning,
			TEXT("sky: type-5 magnitude %.5f but the cube's upper hemisphere is black — no IBL level"),
			Mag);
		return 0.f;
	}
	return Mag / CubeUpperMean;
}

void AElysiumMapActor::ApplyPostProcessKnobs()
{
	if (!PostProcess)
	{
		return;
	}
	FPostProcessSettings& S = PostProcess->Settings;

	const float Leak = CVarSkylightLeaking.GetValueOnGameThread();
	S.bOverride_LumenSkylightLeaking = Leak >= 0.f;
	if (Leak >= 0.f)
	{
		S.LumenSkylightLeaking = Leak;
	}

	const float LeakDist = CVarSkylightLeakingDistance.GetValueOnGameThread();
	S.bOverride_LumenFullSkylightLeakingDistance = LeakDist >= 0.f;
	if (LeakDist >= 0.f)
	{
		S.LumenFullSkylightLeakingDistance = LeakDist;
	}

	const float Boost = CVarDiffuseColorBoost.GetValueOnGameThread();
	S.bOverride_LumenDiffuseColorBoost = Boost >= 0.f;
	if (Boost >= 0.f)
	{
		S.LumenDiffuseColorBoost = Boost;
	}
}

void AElysiumMapActor::ApplySkyBrightness()
{
	if (SkyMid)
	{
		SkyMid->SetScalarParameterValue(TEXT("Brightness"), CVarSkyBrightness.GetValueOnGameThread());
	}
}

bool AElysiumMapActor::ReadSpawn(FVector& OutLocation, float& OutYaw) const
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapSpawn(MapName)))
	{
		return false;
	}

	bool bHasOrigin = false;
	float YawSrc = 0.f;
	FVector Origin = FVector::ZeroVector;

	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() == 4 && Tok[0] == TEXT("origin"))
		{
			Origin = FVector(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
			bHasOrigin = true;
		}
		else if (Tok.Num() == 2 && Tok[0] == TEXT("yaw"))
		{
			YawSrc = FCString::Atof(*Tok[1]);
		}
	}

	if (!bHasOrigin)
	{
		return false;
	}

	// Lift off the floor so the pawn's collision capsule clears the ground on spawn.
	OutLocation = Origin + FVector(0.f, 0.f, 100.f);
	// .spawn already carries Unreal-space yaw (UE_bsp_to_scene negates it at export).
	OutYaw = YawSrc;
	return true;
}

void AElysiumMapActor::ResolveLandmarkSpawn()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps || !EntityWorld)
	{
		return;
	}

	FString Landmark; FVector Offset; float Yaw; bool bHasYaw;
	if (!Maps->ConsumeLandmarkSpawn(Landmark, Offset, Yaw, bHasYaw))
	{
		return;   // not a landmark transition — keep the info_player_start placement (ReadSpawn)
	}

	FElysiumEntity* Lm = EntityWorld->FindLandmark(Landmark);
	if (!Lm || !Lm->Def)
	{
		// Both maps must carry an info_landmark of the same name; a missing one is a data error.
		// Fall back to info_player_start rather than dumping the player at the origin (matches the
		// decompiled "can't find landmark" warning path).
		UE_LOG(LogElysium, Warning,
			TEXT("landmark '%s' not found in %s — spawning at info_player_start"), *Landmark, *MapName);
		return;
	}

	// new pos = destination landmark origin + the offset captured at the source landmark. The offset
	// already carries the player's capsule-centre height above the landmark, so a real transition
	// needs no extra lift; a direct/console landmark entry (offset zero) seats the capsule centre by
	// lifting off the landmark's feet origin, and faces the landmark's own angles.
	PendingSpawnLoc = Lm->Def->Origin + Offset;
	if (bHasYaw)
	{
		PendingSpawnYaw = Yaw;   // preserve the player's view yaw across the transition
	}
	else
	{
		PendingSpawnLoc.Z += 100.0f;   // lift the capsule off the landmark feet (as ReadSpawn does)
		PendingSpawnYaw = -Lm->Angles.Y;   // face the landmark's angles (Source yaw negated to Unreal)
	}
	bSpawnPending = true;
	EntryLandmark = Landmark;

	// Fire the landmark's OnEnterMapHere (e.g. pawnshop's newgame/haven -> Radio2.Deactivate). The
	// spawn pass is complete, so every wire target exists; FireOutput queues it on the event queue,
	// serviced on the first tick (after any logic_auto OnMapLoad, matching the map-enter ordering).
	static const FName OnEnterMapHere(TEXT("OnEnterMapHere"));
	Lm->FireOutput(OnEnterMapHere, Lm->Handle);

	UE_LOG(LogElysium, Log, TEXT("landmark spawn: %s @ %s -> %s (yaw %.0f)"),
		*MapName, *Landmark, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The audio subsystem is GameInstance-scoped and outlives this map actor, but every voice it
	// holds is map-scoped (ambient_generic + the scheme bed/music/random one-shots). Stop them all
	// on unload so nothing bleeds into the next map. StopAllVoices also covers the scheme voices, so
	// the scheme manager only needs to drop its (now-dead) handles.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
		{
			if (SchemeManager)
			{
				SchemeManager->StopAll(Audio);
			}
			Audio->StopAllVoices();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AElysiumMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// R4 — advance the single game clock once per frame and drive the entity world think-first
	// (retail order: due thinks, then the event queue). Runs every frame, independent of the
	// spawn-hold below, so the map-load I/O chains service immediately.
	if (EntityWorld)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
			{
				GameState->GameClock().Advance(DeltaSeconds);
				EntityWorld->Tick(GameState->GameClock().GetNow());
				// P4.2 — the minimal +use look-cursor: re-pick the aimed usable each frame and fire
				// OnIn/OnOut on the transitions. Runs after the world tick so the outputs it fires
				// enqueue against the same `now`. The E key drives PlayerUse (AElysiumPawn).
				EntityWorld->UpdateUseCursor();

				// P6.3 — reap finished voices, then advance the SoundScheme manager (random-one-shot
				// scheduler + music crossfade) with the listener position for polar placement/attenuation.
				if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
				{
					Audio->TickAudio(DeltaSeconds);
					if (SchemeManager)
					{
						FVector ListenerLoc = GetActorLocation();
						if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
						{
							FVector VLoc; FRotator VRot;
							PC->GetPlayerViewPoint(VLoc, VRot);
							ListenerLoc = VLoc;
						}
						SchemeManager->Tick(Audio, ListenerLoc, DeltaSeconds);
					}
				}
			}
		}
	}

	if (!bSpawnPending || bSpawnDone)
	{
		return;
	}
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Place the pawn immediately, but hold it frozen (no gravity) until the async
	// collision cook produces ground under the spawn point — otherwise it falls
	// through the not-yet-cooked floor. A timeout releases it regardless.
	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!bSpawnPlaced)
	{
		Pawn->SetActorLocation(PendingSpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
		if (Char)
		{
			Char->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
		}
		bSpawnPlaced = true;
	}

	SpawnHoldSeconds += DeltaSeconds;
	FHitResult Hit;
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit,
		PendingSpawnLoc, PendingSpawnLoc - FVector(0.f, 0.f, 100000.f), ECC_Pawn);
	if (bGround || SpawnHoldSeconds > 8.f)
	{
		if (Char && Char->GetCharacterMovement()->MovementMode == MOVE_None)
		{
			Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		UE_LOG(LogElysium, Log, TEXT("spawn released after %.2fs (%s)"),
			SpawnHoldSeconds, bGround ? TEXT("ground ready") : TEXT("timeout"));
		bSpawnDone = true;
	}
}
