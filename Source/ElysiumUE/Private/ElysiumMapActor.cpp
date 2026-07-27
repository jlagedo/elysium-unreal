#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBakedTags.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumFog.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumEnvironment.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSoundScheme.h"
#include "ElysiumLightRig.h"
#include "ElysiumMaterialFactory.h"
#include "ElysiumNpcAnimInstance.h"
#include "ElysiumNpcAnimSubsystem.h"
#include "ElysiumNpcVisual.h"
#include "ElysiumObjModel.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPropSkins.h"
#include "ElysiumReflections.h"
#include "ElysiumRopes.h"
#include "ElysiumTextureCache.h"
#include "ElysiumUseIcons.h"

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
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
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
// The NPC animation host. 1 = UElysiumNpcAnimInstance (two sequence players + a crossfade), 0 =
// Unreal's single-node instance, which cannot blend, so every clip change pops. Applied at map
// load, per body.
static TAutoConsoleVariable<int32> CVarNpcAnim(
	TEXT("elysium.NpcAnim"), 1,
	TEXT("NPC animation host: the crossfading Elysium anim instance (1) or single-node (0). Applied at map load."),
	ECVF_Default);

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

// The $envmap reflection channel (roadmap 7.5), live over the baked materials.
//
// The bake authors MaterialInstanceConstants, and a constant has no runtime setter — so without
// ApplyMaterialOverrides standing a MID in front of each, every one of these is dead on the path
// that actually renders. The pass reads each knob's BAKED value off the parent instance and
// writes the adjusted one onto the MID, so applying twice is idempotent and a per-material value
// the bake computed (the grey-$envmaptint dim-down on SpecReflect, say) is never flattened by a
// global knob.
//
// EnvReflect SCALES the baked EnvStrength, so it cannot make a matte surface reflective: a
// surface with no $envmap has a baked strength of 0 and stays at 0 whatever the multiplier.
// The three level knobs pin a parameter across every reflective material and follow the
// "negative = neutral" convention the PPV knobs above use — the override is *not applied* rather
// than set to a nominal default, so "not touching this" and "set to what it would have been"
// stay distinguishable.
static TAutoConsoleVariable<int32> CVarMaterialOverrides(
	TEXT("elysium.MaterialOverrides"), 1,
	TEXT("Stand a dynamic instance in front of each baked material (1) so the elysium.* material "
	     "knobs reach it, or render the baked instances exactly as authored (0). Applied at map load."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRoughBase(
	TEXT("elysium.RoughBase"), -1.f,
	TEXT("Roughness of a NON-reflective surface (VtMB's world is Lambert, so 1). "
	     "Negative = neutral (keep the baked value)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRoughReflect(
	TEXT("elysium.RoughReflect"), -1.f,
	TEXT("Roughness a fully $envmapmask-ed texel reaches. Negative = neutral (keep the baked value)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarSpecBase(
	TEXT("elysium.SpecBase"), -1.f,
	TEXT("Specular level of a NON-reflective surface. 0 is the Lambert floor VtMB's material data "
	     "states; UE's own default is 0.5. Negative = neutral (keep the baked value)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarSpecReflect(
	TEXT("elysium.SpecReflect"), -1.f,
	TEXT("Specular level a fully $envmapmask-ed texel reaches. Pinning this overrides the "
	     "per-material grey-$envmaptint dim-down. Negative = neutral (keep the baked value)."),
	ECVF_Default);

// Source's distance fog, on (1) or off (0), for A/B. It is a per-primitive material term rather
// than the height fog actor because the world and the 3D-skybox miniature carry two different
// fogs and share screen depth — the reasoning and its measurement are in ElysiumFog.h. Live:
// ApplySceneFog re-stamps every primitive as it changes, so an A/B needs no reload. It does not
// reach the map's decals, whose fog is bound into their baked material instances (a
// UDecalComponent is a USceneComponent and carries no custom primitive data).
static TAutoConsoleVariable<int32> CVarFog(
	TEXT("elysium.Fog"), 1,
	TEXT("Apply the map's authored distance fog (1) or none (0). World and 3D-skybox miniature "
	     "take their own sets, from worldspawn and sky_camera."),
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

// ------------------------------------------------------------------------------------------
// S2 — the post-move tick function (runtime-architecture.md §3, step 7).
// ------------------------------------------------------------------------------------------

void FElysiumPostMoveTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->PostMoveTick(DeltaTime);
	}
}

FString FElysiumPostMoveTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::PostMoveTick]");
}

FName FElysiumPostMoveTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorPostMove/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorPostMove"));
}

AElysiumMapActor::AElysiumMapActor()
{
	// S2 — the frame order is declared with tick groups, not left to registration order. The
	// gameplay pass (clock, thinks, queue) runs before physics; the post-move pass runs after it
	// and after the pawn's move. Neither ticks while the game is held: pause is meant to stop the
	// world, and the presentation side is what keeps drawing (§4).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = false;

	PostMoveTickFunction.bCanEverTick = true;
	PostMoveTickFunction.bStartWithTickEnabled = true;
	PostMoveTickFunction.TickGroup = TG_PostPhysics;
	PostMoveTickFunction.bTickEvenWhenPaused = false;

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

void AElysiumMapActor::RegisterActorTickFunctions(bool bRegister)
{
	Super::RegisterActorTickFunctions(bRegister);

	if (bRegister)
	{
		if (PostMoveTickFunction.bCanEverTick)
		{
			PostMoveTickFunction.Target = this;
			PostMoveTickFunction.SetTickFunctionEnable(PostMoveTickFunction.bStartWithTickEnabled);
			PostMoveTickFunction.RegisterTickFunction(GetLevel());
			// The tick groups already separate the two passes; the prerequisite says so in the
			// graph as well, so the dependency survives anyone re-grouping either end.
			PostMoveTickFunction.AddPrerequisite(this, PrimaryActorTick);
		}
	}
	else if (PostMoveTickFunction.IsTickFunctionRegistered())
	{
		PostMoveTickFunction.UnRegisterTickFunction();
	}
}

void AElysiumMapActor::EnsureTickPrerequisites()
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Step 1 -> steps 2-4: the frame's input sample lands before the substrate runs.
	if (PrereqController.Get() != PC)
	{
		AddTickPrerequisiteActor(PC);
		PrereqController = PC;
	}

	// Steps 2-4 -> step 5: the pawn moves against the positions this frame's thinks produced.
	// A door's think issues its swept move here; moving the pawn first tunnels it on fast movers.
	UPawnMovementComponent* Move = PC->GetPawn() ? PC->GetPawn()->GetMovementComponent() : nullptr;
	if (Move && PrereqMovement.Get() != Move)
	{
		Move->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		PrereqMovement = Move;

		// 11.4 — tell the body which entity it embodies. Done here rather than at SpawnPlayer
		// because a fresh world has no pawn yet when the map builds, and this already runs each
		// gameplay tick until the pawn appears (and again if it is replaced).
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn()))
		{
			Body->SetPlayerEntity(EntityWorld ? EntityWorld->PlayerHandle() : FElysiumEntityHandle::Invalid());
		}
	}
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();

	// Engine pause and time dilation are per-world; the clock is not. Re-stamp them onto this
	// world so a hold or a time scale set before travel survives the map change (S1).
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().ApplyToWorld();
		}
	}
	EnsureTickPrerequisites();

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

	// The reflection channel is found by eye against a live scene too, so its knobs re-apply as
	// they change. ApplyMaterialOverrides re-reads each baked value before adjusting it, so
	// re-running it is idempotent rather than cumulative.
	const FConsoleVariableDelegate Reflect = FConsoleVariableDelegate::CreateWeakLambda(this,
		[this](IConsoleVariable*) { ApplyMaterialOverrides(); });
	CVarRoughBase.AsVariable()->SetOnChangedCallback(Reflect);
	CVarRoughReflect.AsVariable()->SetOnChangedCallback(Reflect);
	CVarSpecBase.AsVariable()->SetOnChangedCallback(Reflect);
	CVarSpecReflect.AsVariable()->SetOnChangedCallback(Reflect);
	if (IConsoleVariable* EnvReflectVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.EnvReflect")))
	{
		EnvReflectVar->SetOnChangedCallback(Reflect);
	}

	// Same for the fog A/B: it re-stamps the primitives already placed.
	CVarFog.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this,
			[this](IConsoleVariable*) { ApplySceneFog(); }));

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
	// Stand dynamic instances in front of the baked materials so the look-tuning cvars reach
	// them; without this a baked MaterialInstanceConstant has no runtime setter and every
	// elysium.* material knob is dead on the path that renders.
	MaterialOverrides.Reset();
	ApplyMaterialOverrides();
	Phase(TEXT("Adopt baked level"));

	// 8.6 — a menu backdrop builds the map in full, entity substrate included: the NPCs standing and
	// idling in frame *are* entities, so a look-only build has no one in it (owner call, see
	// `docs/decisions.md`). What a backdrop skips is only the player's placement — it seats no pawn.
	UElysiumMapSubsystem* MapSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	const bool bMenuBackdrop = MapSubsystem && MapSubsystem->IsMenuBackdrop();

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

	if (!bMenuBackdrop && ReadSpawn(PendingSpawnLoc, PendingSpawnYaw))
	{
		bSpawnPending = true;
	}

	if (bMenuBackdrop)
	{
		UE_LOG(LogElysium, Log, TEXT("menu backdrop '%s': full build, no player placement"), *MapName);
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

				// The scheme manager must exist before the spawn pass: a start_enabled
				// ambient_soundscheme fades its scheme in from its own Spawn() (P6.3), and it
				// reaches it through this actor's IElysiumAudio.
				SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();

				// 11.2 — hand the substrate its outbound seam. This actor is three of the four
				// services; IElysiumPresenter stays null until 11.8 gives it a real implementation.
				FElysiumWorldServices Services;
				Services.Embodiment = this;
				Services.Audio      = this;
				Services.Travel     = this;
				EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState, Services);
				EntityWorld->Load(MoveTemp(EntDefs));
				BrushBodyCount = EntityWorld->NumBrushBodies();

				// 11.4 (S3) — the player is an entity, created here because the map is where a
				// player exists at all: a backdrop seats no pawn, so it gets no player entity and
				// everything that looks for one handles its absence. Created after the spawn pass
				// and before the first tick, so `!player` resolves for the map's own logic_auto
				// ignition; it hydrates from the session record the previous map dehydrated into.
				if (!bMenuBackdrop)
				{
					EntityWorld->SpawnPlayer();
				}
			}
			else
			{
				UE_LOG(LogElysium, Log, TEXT("no %s.ents — entity world not built"), *MapName);
			}
		}
	}

	// P4.6 — a landmark transition places the player against the destination info_landmark instead of
	// info_player_start. Runs after the entity world is built (the landmark is one of its entities),
	// and not at all on a backdrop, which has no entity world and seats no player.
	if (!bMenuBackdrop)
	{
		ResolveLandmarkSpawn();
	}

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

void AElysiumMapActor::ApplyMaterialOverrides()
{
	if (CVarMaterialOverrides.GetValueOnGameThread() == 0)
	{
		return;
	}

	// elysium.EnvReflect is registered by ElysiumMaterialFactory.cpp (it also binds it onto the
	// rope MIDs, which are built at runtime and not baked), so it is reached by name rather than
	// duplicated here — one cvar, both consumers.
	static IConsoleVariable* EnvReflectVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.EnvReflect"));
	const float EnvReflect = EnvReflectVar ? FMath::Max(0.f, EnvReflectVar->GetFloat()) : 1.f;
	const float RoughBase = CVarRoughBase.GetValueOnGameThread();
	const float RoughReflect = CVarRoughReflect.GetValueOnGameThread();
	const float SpecBase = CVarSpecBase.GetValueOnGameThread();
	const float SpecReflect = CVarSpecReflect.GetValueOnGameThread();

	// Nothing to override until a knob is actually turned, and standing a dynamic instance in
	// front of a baked one is not free: a runtime SetMaterial drops the primitive's built texture
	// streaming data, and the textures fall back to a low mip until the streamer catches up (a
	// visibly blurry world). So the shipped path creates no MIDs at all and renders the baked
	// instances exactly as authored; they appear only for an A/B session, where a transient mip
	// settle is an acceptable price for a live knob.
	const bool bWanted = !FMath::IsNearlyEqual(EnvReflect, 1.f)
		|| RoughBase >= 0.f || RoughReflect >= 0.f || SpecBase >= 0.f || SpecReflect >= 0.f;
	if (!bWanted && MaterialOverrides.Num() == 0)
	{
		return;
	}

	// Build the MID set once per map. Keyed by the baked material, so a material shared by many
	// components yields one MID — the same one-MID-per-material shape the runtime-built path had,
	// and the reason this is ~700 objects on the tutorial rather than ~1,400 slot-wise.
	if (MaterialOverrides.Num() == 0)
	{
		auto Cover = [this](const TArray<TObjectPtr<AStaticMeshActor>>& Actors)
		{
			for (const TObjectPtr<AStaticMeshActor>& Actor : Actors)
			{
				UStaticMeshComponent* Comp = Actor ? Actor->GetStaticMeshComponent() : nullptr;
				if (Comp == nullptr)
				{
					continue;
				}
				const int32 Num = Comp->GetNumMaterials();
				for (int32 Slot = 0; Slot < Num; ++Slot)
				{
					UMaterialInterface* Baked = Comp->GetMaterial(Slot);
					// Already covered (a material shared across two of the buckets), or nothing
					// bound at all.
					if (Baked == nullptr || Baked->IsA<UMaterialInstanceDynamic>())
					{
						continue;
					}
					TObjectPtr<UMaterialInstanceDynamic>& Mid = MaterialOverrides.FindOrAdd(Baked);
					if (Mid == nullptr)
					{
						Mid = UMaterialInstanceDynamic::Create(Baked, this);
					}
					if (Mid != nullptr)
					{
						Comp->SetMaterial(Slot, Mid);
					}
				}
			}
		};
		Cover(WorldActors);
		Cover(SkyActors);
		Cover(PropActors);
	}

	for (const TPair<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>>& Pair
		: MaterialOverrides)
	{
		UMaterialInterface* Baked = Pair.Key;
		UMaterialInstanceDynamic* Mid = Pair.Value;
		if (Baked == nullptr || Mid == nullptr)
		{
			continue;
		}

		// Every parameter is read off the BAKED instance and written whole, so the pass is
		// idempotent and a knob returned to neutral restores the authored value exactly —
		// rather than leaving the last pinned one behind, which would make an A/B one-way.
		auto Pin = [Baked, Mid](const FName& Param, float Knob)
		{
			float Value = 0.f;
			if (!Baked->GetScalarParameterValue(Param, Value))
			{
				return;   // this master has no such parameter (M_Additive is unlit)
			}
			Mid->SetScalarParameterValue(Param, Knob >= 0.f ? Knob : Value);
		};

		// The Lambert base is the whole world's, not just the reflective set's — it is what
		// decides whether a non-$envmap surface takes any Lumen specular at all.
		Pin(ElysiumReflections::Params::RoughBase, RoughBase);
		Pin(ElysiumReflections::Params::SpecBase, SpecBase);
		// The reflective end. Inert on a matte surface, whose lerp alpha is 0 there.
		Pin(ElysiumReflections::Params::RoughReflect, RoughReflect);
		Pin(ElysiumReflections::Params::SpecReflect, SpecReflect);

		// EnvStrength is SCALED rather than pinned, because it carries no single right value:
		// it is the per-material reflection reach. Reading the baked value each time is what
		// keeps repeated calls from compounding the multiplier. Scaling is also what keeps a
		// matte surface matte — its baked strength is 0, so no multiplier gives it a reflection.
		float BakedEnvStrength = 0.f;
		if (Baked->GetScalarParameterValue(ElysiumReflections::Params::EnvStrength, BakedEnvStrength)
			&& BakedEnvStrength > 0.f)
		{
			Mid->SetScalarParameterValue(ElysiumReflections::Params::EnvStrength,
				BakedEnvStrength * EnvReflect);
		}
	}
}

UAnimSequence* AElysiumMapActor::ResolveNpcClip(const FString& Stem, const FString& ClipName)
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
	UGameInstance* GI = GetGameInstance();
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const TObjectPtr<USkeletalMesh>* Mesh = NpcMeshCache.Find(Stem);
	const TObjectPtr<UglTFRuntimeAsset>* Own = NpcAssetCache.Find(Stem);
	if (Anims != nullptr && Mesh != nullptr && *Mesh != nullptr)
	{
		FString Error;
		Anim = Anims->ResolveClip(Stem, ClipName, Mesh->Get(), Own ? Own->Get() : nullptr, Error);
		if (Anim == nullptr)
		{
			UE_LOG(LogElysium, Warning, TEXT("npc '%s' clip '%s': %s"), *Stem, *ClipName, *Error);
		}
	}
	NpcAnimCache.Add(Key, Anim);
	return Anim;
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
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

bool AElysiumMapActor::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	if (Body == nullptr || Anims == nullptr)
	{
		return false;
	}
	EElysiumIdleTier Tier = EElysiumIdleTier::None;
	const FString Clip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
}

USkeletalMeshComponent* AElysiumMapActor::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant)
{
	USceneComponent* Root = GetRootComponent();
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
			UE_LOG(LogElysium, Warning, TEXT("BuildNpcVisual '%s': %s"), *Stem, *Error);
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
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumNpcAnimSubsystem* Anims = GI->GetSubsystem<UElysiumNpcAnimSubsystem>())
		{
			EElysiumIdleTier Tier = EElysiumIdleTier::None;
			IdleClip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
			UE_LOG(LogElysium, Verbose, TEXT("npc '%s' idle: %s (%s, disposition '%s', variant %d)"),
				*Stem, IdleClip.IsEmpty() ? TEXT("<none>") : *IdleClip,
				UElysiumNpcAnimSubsystem::TierName(Tier),
				Disposition.IsEmpty() ? TEXT("<unset>") : *Disposition, IdleVariant);
		}
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
	AddInstanceComponent(Comp);
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

// ============================================================================================
// The world services (11.2) — the substrate's engine side. Everything here is a forward: the
// player's pawn, the GI-scoped audio subsystem, this map's scheme manager, the map subsystem.
// Nothing under FElysiumEntityWorld knows any of those exist.
// ============================================================================================

APawn* AElysiumMapActor::ResolvePlayerPawn() const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool AElysiumMapActor::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(OutLocation, OutRotation);
	}
	else
	{
		// No controller (a detached/possessed-later pawn): the body's own transform is the best
		// available eye, which is what the trigger_look path has always fallen back to.
		OutLocation = Pawn->GetActorLocation();
		OutRotation = Pawn->GetActorRotation();
	}
	return true;
}

bool AElysiumMapActor::GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutLocation = Pawn->GetActorLocation();
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutYaw = PC ? (float)PC->GetControlRotation().Yaw : (float)Pawn->GetActorRotation().Yaw;
	return true;
}

void AElysiumMapActor::TeleportPlayer(const FVector& FeetOrigin, float Yaw)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return;
	}
	// Source places the entity's absorigin (feet); both Unreal bodies are centred, so lift by the
	// body's half-height to seat the player on the destination rather than in the floor. This is
	// the body's own geometry, which is why the compensation lives here and not in point_teleport,
	// and why the number comes from the body rather than from an assumed shape.
	FVector Dest = FeetOrigin;
	if (const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
	{
		Dest.Z += Body->GetBodyHalfHeight();
	}
	Pawn->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	}
}

void AElysiumMapActor::DamagePlayer(float Amount)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (Pawn && Amount > 0.f)
	{
		// This actor is the damage causer: every entity body is one of its components, so it is
		// what the old per-call `Body->GetOwner()` resolved to anyway.
		UGameplayStatics::ApplyDamage(Pawn, Amount, nullptr, const_cast<AElysiumMapActor*>(this),
			UDamageType::StaticClass());
	}
}

FElysiumEntityHandle AElysiumMapActor::TraceUseCursor(const FVector& Start, const FVector& End) const
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return FElysiumEntityHandle::Invalid();
	}

	// A single blocking trace naturally handles occlusion: a wall (or any solid) closer than the
	// button ends the ray. The dedicated +use channel (ELYSIUM_USE_CHANNEL, default-Block) keeps
	// world + solid bodies as occluders while staying isolated from ECC_Visibility;
	// func_button/func_door bodies are Solid (BlockAll), so they block it.
	FCollisionQueryParams Params(FName(TEXT("ElysiumUseCursor")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult H;
	if (W->LineTraceSingleByChannel(H, Start, End, ELYSIUM_USE_CHANNEL, Params))
	{
		if (const UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent()))
		{
			return B->GetOwningEntity();
		}
	}
	return FElysiumEntityHandle::Invalid();
}

FElysiumAudioVoiceHandle AElysiumMapActor::PlayVoice(const FString& Rel, const FElysiumPlayParams& Params)
{
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->PlayVoice(Rel, Params) : FElysiumAudioVoiceHandle::Invalid();
}

void AElysiumMapActor::StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->StopVoice(Handle, FadeSeconds);
	}
}

void AElysiumMapActor::SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetVoiceVolume(Handle, Volume);
	}
}

bool AElysiumMapActor::IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const
{
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio && Audio->IsVoicePlaying(Handle);
}

void AElysiumMapActor::FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds)
{
	if (SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), SchemeRel, Anchor, FadeSeconds);
	}
}

void AElysiumMapActor::FadeOutScheme(const FString& SchemeRel, float FadeSeconds)
{
	if (SchemeManager)
	{
		SchemeManager->FadeOutScheme(GetAudioSubsystem(), SchemeRel, FadeSeconds);
	}
}

FString AElysiumMapActor::ActiveSchemeRel() const
{
	return SchemeManager ? SchemeManager->ActiveSchemeRel() : FString();
}

void AElysiumMapActor::RequestLandmarkTravel(const FString& Map, const FString& Landmark,
	const FVector& Offset, float Yaw)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->RequestLandmarkTravel(Map, Landmark, Offset, Yaw);
	}
}

void AElysiumMapActor::ChangeMap(const FString& Map)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->Travel(Map);
	}
}

UElysiumAudioSubsystem* AElysiumMapActor::GetAudioSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumAudioSubsystem>() : nullptr;
}

UElysiumMapSubsystem* AElysiumMapActor::GetMapSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
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

// The 3D-skybox A/B is a **dev** verb, not a player one, so it lives on plane 1: an `elysium.*`
// console command, reached by a chord (Ctrl+T) rather than by a bare key — `t` is `toggleuiside` in
// VtMB's default set (`docs/input-architecture.md` § "Reserved keys").
static FAutoConsoleCommandWithWorld GElysiumToggleSky(
	TEXT("elysium.togglesky"),
	TEXT("Show/hide the 3D skybox miniature and the backdrop dome together."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
		if (AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr)
		{
			Map->ToggleSkybox();
		}
	}));

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
	EnvDef = Env;

	// Before anything sky-shaped: the fog belongs to every primitive in the level, including on
	// the 65 maps with no sky_camera and the ones whose faces did not decode.
	ApplySceneFog();

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

void AElysiumMapActor::ApplySceneFog()
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

	auto Describe = [](const TArray<float>& Data)
	{
		return Data[ElysiumFog::SlotInvRange] > 0.f
			? FString::Printf(TEXT("%.0f->%.0fcm"), Data[ElysiumFog::SlotStart],
				Data[ElysiumFog::SlotStart] + 1.f / Data[ElysiumFog::SlotInvRange])
			: FString(TEXT("off"));
	};
	UE_LOG(LogElysium, Log, TEXT("fog: world %s on %d primitives, 3D skybox %s on %d"),
		*Describe(WorldData), WorldStamped, *Describe(SkyData), SkyStamped);
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

	// The controller and the pawn appear after this actor does, so keep looking until the frame
	// order is fully declared (a menu backdrop map never seats a pawn, and that is fine).
	EnsureTickPrerequisites();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Step 2 — the only place `Now` moves (S1). DeltaSeconds is already dilated by the
			// engine, and the clock applies no factor of its own, so a time scale is applied once.
			GameState->TimeControl().AdvanceFrame(DeltaSeconds);

			// Steps 3-4 — the substrate, think-first (retail order: Physics_RunThinkFunctions,
			// then CEventQueue::ServiceEvents). Runs every frame, independent of the spawn-hold
			// below, so the map-load I/O chains service immediately.
			if (EntityWorld)
			{
				EntityWorld->Tick(GameState->GameClock().GetNow());
			}

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
	IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	if (!bSpawnPlaced)
	{
		Pawn->SetActorLocation(PendingSpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
		if (Body)
		{
			Body->SetMovementFrozen(true);
		}
		bSpawnPlaced = true;
	}

	SpawnHoldSeconds += DeltaSeconds;
	FHitResult Hit;
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit,
		PendingSpawnLoc, PendingSpawnLoc - FVector(0.f, 0.f, 100000.f), ECC_Pawn);
	if (bGround || SpawnHoldSeconds > 8.f)
	{
		if (Body)
		{
			Body->SetMovementFrozen(false);
		}
		UE_LOG(LogElysium, Log, TEXT("spawn released after %.2fs (%s)"),
			SpawnHoldSeconds, bGround ? TEXT("ground ready") : TEXT("timeout"));
		bSpawnDone = true;
	}
}

void AElysiumMapActor::PostMoveTick(float DeltaSeconds)
{
	// Step 7 — everything here reads the frame's final positions.
	//
	// P4.2 — the minimal +use look-cursor: re-pick the aimed usable and fire OnIn/OnOut on the
	// transitions. It traces, so it belongs after physics: before the pawn's move it would pick
	// against last frame's geometry, which reads as a door you cannot use until you stop walking.
	// Its outputs enqueue against the same `now` the gameplay pass advanced to, so they service on
	// the next frame's queue pass exactly like any other zero-delay wire.
	if (EntityWorld)
	{
		EntityWorld->UpdateUseCursor();
	}

	// The tail of a released frame: a dev step spends one here, and the last one re-holds the world.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().EndFrame();
		}
	}
}
