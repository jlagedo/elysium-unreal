#include "ElysiumMapActor.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEnvironment.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumLightRig.h"
#include "ElysiumMaterialFactory.h"
#include "ElysiumObjModel.h"
#include "ElysiumStaticMesh.h"

#include "Engine/GameInstance.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

// Use the pipeline's brush sidecars (.hulls convex + .dispcol trimesh) as the world collider (1),
// or fall back to the render-mesh trimesh (0). Brush collision matches retail: it includes the
// invisible PLAYERCLIP volumes and drops collision on geometry the designer clipped off. Read at
// map load, so re-travel to A/B a value.
static TAutoConsoleVariable<int32> CVarBrushCollision(
	TEXT("elysium.BrushCollision"), 1,
	TEXT("World collider: 1 = .hulls/.dispcol brush collision, 0 = render-mesh trimesh. Applied at map load."),
	ECVF_Default);

namespace
{
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

	// ---- mesh cook-cache -----------------------------------------------------
	// OBJ parse + tangent generation are cooked once into Saved/ElysiumCache/<stem>.emc
	// (validated against the OBJ's size+mtime); later loads read flat buffers instead.

	constexpr uint32 MeshCacheMagic = 0x31434D45;   // 'EMC1'

	struct FCookedSection
	{
		FString Mat;
		TArray<FVector> Verts;
		TArray<FVector2D> UVs;
		TArray<FVector> Normals;
		TArray<FVector> TanX;
		TArray<uint8> TanFlip;
		TArray<int32> Tris;

		void Serialize(FArchive& Ar)
		{
			Ar << Mat << Verts << UVs << Normals << TanX << TanFlip << Tris;
		}
	};

	FString MeshCachePath(const FString& ObjPath)
	{
		return FPaths::ProjectSavedDir() / TEXT("ElysiumCache") / FPaths::GetBaseFilename(ObjPath) + TEXT(".emc");
	}

	void SourceStamp(const FString& ObjPath, int64& OutSize, int64& OutTicks)
	{
		OutSize = IFileManager::Get().FileSize(*ObjPath);
		OutTicks = IFileManager::Get().GetTimeStamp(*ObjPath).GetTicks();
	}

	bool LoadMeshCache(const FString& ObjPath, TArray<FCookedSection>& OutSections, FString& OutMtlName)
	{
		TArray<uint8> Data;
		if (!FFileHelper::LoadFileToArray(Data, *MeshCachePath(ObjPath)))
		{
			return false;
		}
		FMemoryReader Ar(Data);
		uint32 Magic = 0;
		int64 SrcSize = 0, SrcTicks = 0, CurSize = 0, CurTicks = 0;
		Ar << Magic << SrcSize << SrcTicks;
		SourceStamp(ObjPath, CurSize, CurTicks);
		if (Magic != MeshCacheMagic || SrcSize != CurSize || SrcTicks != CurTicks)
		{
			return false;
		}
		int32 Count = 0;
		Ar << OutMtlName << Count;
		OutSections.SetNum(Count);
		for (FCookedSection& S : OutSections)
		{
			S.Serialize(Ar);
		}
		return !Ar.IsError();
	}

	void SaveMeshCache(const FString& ObjPath, TArray<FCookedSection>& Sections, const FString& MtlName)
	{
		TArray<uint8> Data;
		FMemoryWriter Ar(Data);
		uint32 Magic = MeshCacheMagic;
		int64 SrcSize = 0, SrcTicks = 0;
		SourceStamp(ObjPath, SrcSize, SrcTicks);
		int32 Count = Sections.Num();
		FString Mtl = MtlName;
		Ar << Magic << SrcSize << SrcTicks << Mtl << Count;
		for (FCookedSection& S : Sections)
		{
			S.Serialize(Ar);
		}
		FFileHelper::SaveArrayToFile(Data, *MeshCachePath(ObjPath));
	}
}

AElysiumMapActor::AElysiumMapActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	WorldMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WorldMesh"));
	WorldMesh->SetupAttachment(SceneRoot);
	WorldMesh->bUseComplexAsSimpleCollision = true;
	// Chaos trimesh cooking is the dominant load cost (hundreds of sections cooked
	// synchronously stall the game thread for ~10s); async cooking moves it to task
	// threads. The spawn teleport waits for ground collision (see Tick).
	WorldMesh->bUseAsyncCooking = true;
	WorldMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Convex world collision from .hulls: no render sections (never drawn), simple = convex.
	// One FKConvexElem per solid brush, so pawn capsule sweeps (which query simple collision)
	// hit the brushes and their invisible clip volumes. Cooked async like WorldMesh.
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

	SkyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyMesh"));
	SkyMesh->SetupAttachment(SceneRoot);
	SkyMesh->bUseAsyncCooking = true;
	// Query-only, blocking the Visibility trace channel only: the debug crosshair pick can
	// identify skybox geometry, but the sky never obstructs the player.
	SkyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SkyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SkyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 2D skybox backdrop. Unlit and drawn on a huge box that surrounds the camera; the
	// sky material samples the cube by view direction, so it reads as infinitely far. No
	// collision, no shadows. Hidden until a map's sky faces build the cubemap.
	SkyDomeMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyDomeMesh"));
	SkyDomeMesh->SetupAttachment(SceneRoot);
	SkyDomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDomeMesh->SetCastShadow(false);
	SkyDomeMesh->SetVisibility(false);

	// Fallback sun for maps with no .lights sidecar — owned components, so they unload
	// with the map. When the LightRig builds real per-source lights (the usual case),
	// LoadMap switches this off: a fixed directional sun over interior geometry is exactly
	// the flat look the per-source rig replaces.
	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetRelativeRotation(FRotator(-46.f, -45.f, 0.f));
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetIntensity(3.f);
	SunLight->SetLightColor(FLinearColor(1.0f, 0.98f, 0.92f));

	// SLS_SpecifiedCubemap with no cubemap resolves to a flat constant ambient of the
	// light colour — a fixed fill that does not depend on any scene capture (the sky
	// isn't rendered yet at load time, so SLS_CapturedScene would capture black). Lower
	// hemisphere lit too, so undersides and floor-facing faces also read.
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = SLS_SpecifiedCubemap;
	SkyLight->Cubemap = nullptr;
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->LightColor = FLinearColor(0.55f, 0.58f, 0.65f).ToFColor(true);
	SkyLight->SetIntensity(1.5f);

	// Per-map colour grade from the .cube LUT. Unbound so it grades the whole view
	// regardless of where the camera is; unloads with the map.
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;

	// Source sky_camera fog. Approximated with height fog (Unreal has no linear depth-fog
	// component); hidden until a map's .env turns fog on. Unloads with the map.
	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetVisibility(false);

	// Real-time light rig: one Unreal light per WORLDLIGHTS source (built in LoadMap).
	LightRig = CreateDefaultSubobject<UElysiumLightRig>(TEXT("LightRig"));
	LightRig->SetupAttachment(SceneRoot);
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();
	LoadMap();
}

void AElysiumMapActor::LoadMap()
{
	const double Start = FPlatformTime::Seconds();

	LoadedMap = MapName;

	// P1.7 — label the map actor and drop it in an Elysium Outliner folder, so the PIE World
	// Outliner reads as a live scene browser (debug-tooling.md Layer 0).
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Map:%s"), *MapName));
	SetFolderPath(TEXT("Elysium"));
#endif

	// Prefer real brush collision (.hulls convex + .dispcol trimesh) — it carries the invisible
	// clip volumes and matches retail walk behaviour. When it loads, the world render mesh is
	// built without collision. Falls back to the render-mesh trimesh when the sidecar is missing
	// or elysium.BrushCollision is 0.
	bBrushCollision = CVarBrushCollision.GetValueOnGameThread() != 0 && LoadHulls();
	if (bBrushCollision)
	{
		LoadDispCol();
	}
	WorldSurfaceCount = BuildMeshFromObj(FElysiumContentPaths::MapObj(MapName), WorldMesh, !bBrushCollision);
	UE_LOG(LogElysium, Log, TEXT("world '%s': %d surfaces (collision: %s)"),
		*MapName, WorldSurfaceCount, bBrushCollision ? TEXT("brush") : TEXT("trimesh"));

	const FString SkyObj = FElysiumContentPaths::MapSkyObj(MapName);
	if (FPaths::FileExists(SkyObj))
	{
		SkySurfaceCount = BuildMeshFromObj(SkyObj, SkyMesh, true);
		ApplySkyTransform();
		UE_LOG(LogElysium, Log, TEXT("skybox: %d surfaces"), SkySurfaceCount);
	}

	LoadProps();

	ApplyEnvironment();

	// Real-time lighting: one Unreal light per WORLDLIGHTS source. When the rig has real
	// lights it owns the scene, so the flat fallback sun is switched off and the SkyLight
	// drops to a dim ambient fill (tinted by the map's skyambient where present) that only
	// keeps deep shadows off pure black — the per-source lights do the actual lighting.
	WorldLightCount = LightRig->Build(FElysiumContentPaths::MapLights(MapName));
	if (WorldLightCount > 0)
	{
		SunLight->SetVisibility(false);
		SkyLight->LightColor = LightRig->SkyAmbient.ToFColor(true);
		SkyLight->SetIntensity(LightRig->bHasSkyAmbient ? 0.6f : 0.4f);
	}

	SkyLight->RecaptureSky();

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
			if (FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(MapName), EntDefs))
			{
				EntityCount = EntDefs.Num();
				EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState);
				EntityWorld->Load(MoveTemp(EntDefs));
				BrushBodyCount = EntityWorld->NumBrushBodies();
			}
			else
			{
				UE_LOG(LogElysium, Log, TEXT("no %s.ents — entity world not built"), *MapName);
			}
		}
	}

	UE_LOG(LogElysium, Log, TEXT("loaded %s in %.2fs"), *MapName, FPlatformTime::Seconds() - Start);
}

int32 AElysiumMapActor::BuildMeshFromObj(const FString& ObjPath, UProceduralMeshComponent* Mesh, bool bCollision)
{
	const FString Dir = FPaths::GetPath(ObjPath);
	TArray<FCookedSection> Sections;
	FString MtlName;
	TMap<FString, FElysiumMaterialDef> Materials;

	if (LoadMeshCache(ObjPath, Sections, MtlName))
	{
		FElysiumObjModel::ParseMtl(Dir / MtlName, Materials);
	}
	else
	{
		FElysiumObjModel Model;
		if (!FElysiumObjModel::Parse(ObjPath, Model))
		{
			UE_LOG(LogElysium, Warning, TEXT("failed to parse %s"), *ObjPath);
			return 0;
		}
		MtlName = Model.MtlName;
		Materials = MoveTemp(Model.Materials);

		for (const TPair<FString, TArray<int32>>& Group : Model.Groups)
		{
			const TArray<int32>& GlobalIdx = Group.Value;
			if (GlobalIdx.Num() == 0)
			{
				continue;
			}

			// Remap the group's global vertex indices into a compact per-section buffer.
			FCookedSection S;
			S.Mat = Group.Key;
			TMap<int32, int32> Remap;
			S.Tris.Reserve(GlobalIdx.Num());
			for (int32 GI : GlobalIdx)
			{
				int32 Local;
				if (const int32* Existing = Remap.Find(GI))
				{
					Local = *Existing;
				}
				else
				{
					Local = S.Verts.Num();
					Remap.Add(GI, Local);
					S.Verts.Add(Model.Positions.IsValidIndex(GI) ? Model.Positions[GI] : FVector::ZeroVector);
					S.UVs.Add(Model.Uvs.IsValidIndex(GI) ? Model.Uvs[GI] : FVector2D::ZeroVector);
				}
				S.Tris.Add(Local);
			}

			TArray<FProcMeshTangent> Tangents;
			UKismetProceduralMeshLibrary::CalculateTangentsForMesh(S.Verts, S.Tris, S.UVs, S.Normals, Tangents);
			S.TanX.Reserve(Tangents.Num());
			S.TanFlip.Reserve(Tangents.Num());
			for (const FProcMeshTangent& T : Tangents)
			{
				S.TanX.Add(FVector(T.TangentX));
				S.TanFlip.Add(T.bFlipTangentY ? 1 : 0);
			}
			Sections.Add(MoveTemp(S));
		}
		SaveMeshCache(ObjPath, Sections, MtlName);
	}

	int32 Section = 0;
	const TArray<FLinearColor> NoColors;
	for (FCookedSection& S : Sections)
	{
		TArray<FProcMeshTangent> Tangents;
		Tangents.Reserve(S.TanX.Num());
		for (int32 I = 0; I < S.TanX.Num(); ++I)
		{
			Tangents.Emplace(S.TanX[I], S.TanFlip.IsValidIndex(I) && S.TanFlip[I] != 0);
		}

		Mesh->CreateMeshSection_LinearColor(Section, S.Verts, S.Tris, S.Normals, S.UVs, NoColors, Tangents, bCollision);

		const FElysiumMaterialDef* Def = Materials.Find(S.Mat);
		if (UMaterialInstanceDynamic* Mid = FElysiumMaterialFactory::Build(Def, Dir, this))
		{
			Mesh->SetMaterial(Section, Mid);
		}
		++Section;
	}
	return Section;
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

void AElysiumMapActor::LoadProps()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapProps(MapName)))
	{
		return;   // no .props sidecar: map has no static props
	}
	const FString PropsDir = FElysiumContentPaths::MapPropsDir(MapName);

	// Group instances by model so each unique mesh is built once. `safename ox oy oz
	// qx qy qz qw solid` — all Unreal-space (UE_bsp_to_scene emits it verbatim).
	struct FInst { FTransform Xform; bool bSolid; };
	TMap<FString, TArray<FInst>> ByModel;
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 9)
		{
			continue;
		}
		const FVector Loc(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
		const FQuat Rot(FCString::Atod(*Tok[4]), FCString::Atod(*Tok[5]), FCString::Atod(*Tok[6]), FCString::Atod(*Tok[7]));
		const bool bSolid = FCString::Atoi(*Tok[8]) != 0;
		ByModel.FindOrAdd(Tok[0]).Add({ FTransform(Rot, Loc), bSolid });
	}

	for (const TPair<FString, TArray<FInst>>& Entry : ByModel)
	{
		FElysiumObjModel Model;
		if (!FElysiumObjModel::Parse(PropsDir / (Entry.Key + TEXT(".obj")), Model))
		{
			UE_LOG(LogElysium, Warning, TEXT("prop model parse failed: %s"), *Entry.Key);
			continue;
		}

		const bool bAnySolid = Entry.Value.ContainsByPredicate([](const FInst& I) { return I.bSolid; });
		UStaticMesh* Mesh = FElysiumStaticMeshBuilder::Build(Model, Model.Dir, bAnySolid, this);
		if (!Mesh)
		{
			continue;
		}
		PropMeshes.Add(Mesh);
		++PropModelCount;

		// One ISM per solidity bucket (shared mesh; only the component's collision differs):
		// solid props block, non-solid props are visual-only. Most models are all-or-nothing,
		// so this is usually a single component per model.
		UInstancedStaticMeshComponent* Buckets[2] = { nullptr, nullptr };
		for (const FInst& I : Entry.Value)
		{
			UInstancedStaticMeshComponent*& ISM = Buckets[I.bSolid ? 1 : 0];
			if (!ISM)
			{
				FName IsmName = NAME_None;
#if WITH_EDITOR
				IsmName = ElysiumEditorObjectName(FString::Printf(TEXT("Props_%s_%s"),
					*Entry.Key, I.bSolid ? TEXT("solid") : TEXT("nonsolid")));
#endif
				ISM = NewObject<UInstancedStaticMeshComponent>(this, IsmName);
				ISM->SetupAttachment(SceneRoot);
				ISM->SetMobility(EComponentMobility::Movable);
				ISM->SetStaticMesh(Mesh);
				if (I.bSolid)
				{
					ISM->SetCollisionProfileName(TEXT("BlockAll"));
				}
				else
				{
					ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				ISM->RegisterComponent();
				PropComponents.Add(ISM);
			}
			// Transforms are map-space; the map actor sits at the origin, so component-local
			// (the AddInstance default) equals world.
			ISM->AddInstance(I.Xform);
			++PropInstanceCount;
		}
	}

	UE_LOG(LogElysium, Log, TEXT("props: %d instances / %d models"), PropInstanceCount, PropModelCount);
}

void AElysiumMapActor::ToggleProps()
{
	bPropsVisible = !bPropsVisible;
	for (UInstancedStaticMeshComponent* ISM : PropComponents)
	{
		if (ISM)
		{
			ISM->SetVisibility(bPropsVisible);
		}
	}
}

bool AElysiumMapActor::ArePropsVisible() const
{
	return bPropsVisible;
}

void AElysiumMapActor::ToggleSkybox()
{
	if (SkyMesh)
	{
		SkyMesh->SetVisibility(!SkyMesh->IsVisible());
	}
}

bool AElysiumMapActor::IsSkyboxVisible() const
{
	return SkyMesh && SkyMesh->IsVisible();
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

void AElysiumMapActor::ApplySkyTransform()
{
	// The 3D skybox is a miniature authored at 1/scale in a corner of the map; sky_camera
	// origin maps to world (0,0,0). Blow it up: world(v) = scale * (v - origin). Verts are
	// already in Unreal space, so the component gets uniform scale and translation -scale*origin.
	float Scale = 16.f;
	FVector OriginUnreal = FVector::ZeroVector;

	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapSky(MapName)))
	{
		for (const FString& Line : Lines)
		{
			TArray<FString> Tok;
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			if (Tok.Num() == 4 && Tok[0] == TEXT("origin"))
			{
				OriginUnreal = FVector(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
			}
			else if (Tok.Num() == 2 && Tok[0] == TEXT("scale"))
			{
				Scale = FCString::Atof(*Tok[1]);
			}
		}
	}

	SkyMesh->SetRelativeScale3D(FVector(Scale));
	SkyMesh->SetRelativeLocation(-Scale * OriginUnreal);
}

void AElysiumMapActor::ApplyEnvironment()
{
	// Colour grade (.cube): a per-map 3D LUT into the unbound post-process. Independent of
	// the rest of .env, so it applies even when a map has no sky/fog.
	// TEMP: disabled for a test — see the raw (un-graded) scene colour.
	// if (UTexture2D* Lut = ElysiumEnvironment::BuildColorGradeLUT(FElysiumContentPaths::MapGrade(MapName)))
	// {
	// 	FPostProcessSettings& PP = PostProcess->Settings;
	// 	PP.bOverride_ColorGradingLUT = true;
	// 	PP.ColorGradingLUT = Lut;
	// 	PP.bOverride_ColorGradingIntensity = true;
	// 	PP.ColorGradingIntensity = 1.f;
	// 	UE_LOG(LogElysium, Log, TEXT("colour grade LUT applied"));
	// }

	FElysiumEnvDef Env;
	if (!FElysiumEnvDef::Parse(FElysiumContentPaths::MapEnv(MapName), Env))
	{
		return;   // no .env: keep the placeholder ambient, no fog
	}

	// The six sky faces build one cubemap for the visible backdrop (the M_Sky dome). The
	// SkyLight keeps its flat placeholder ambient for now: these 2D skyboxes are near-black
	// night skies that integrate to almost nothing, so using the cube as the sole ambient
	// would leave the (still unlit — no LightRig yet) world invisible. Cube IBL moves onto
	// the SkyLight once the .lights rig provides the real scene lighting.
	if (Env.bSky)
	{
		if (UTextureCube* Cube = ElysiumEnvironment::BuildSkyCube(FElysiumContentPaths::MapTexDir(MapName)))
		{
			if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/VtMB/Materials/M_Sky.M_Sky")))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Master, this);
				Mid->SetTextureParameterValue(TEXT("SkyCube"), Cube);
				Mid->SetScalarParameterValue(TEXT("Brightness"), 4.f);

				TArray<FVector> Verts, Normals;
				TArray<int32> Tris;
				TArray<FVector2D> UVs;
				BuildSkyBox(500000.f, Verts, Tris, Normals, UVs);
				SkyDomeMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, {}, {}, false);
				SkyDomeMesh->SetMaterial(0, Mid);
				SkyDomeMesh->SetVisibility(true);
				UE_LOG(LogElysium, Log, TEXT("sky dome applied (%s)"), *Env.SkyName);
			}
		}
	}

	// Height fog approximates Source's linear depth fog (start/end in metres -> cm). A small
	// falloff keeps it near height-independent; density is tuned so the far distance reads as
	// mostly fogged. Off maps (fog 0, e.g. the tutorial) leave the component hidden.
	HeightFog->SetVisibility(Env.bFog);
	if (Env.bFog)
	{
		const float StartCm = Env.FogStartCm;
		const float EndCm = FMath::Max(Env.FogEndCm, StartCm + 1.f);
		HeightFog->SetFogInscatteringColor(Env.FogColor);
		HeightFog->SetStartDistance(StartCm);
		HeightFog->SetFogHeightFalloff(0.02f);
		// exp fog factor 1-e^{-density*dist}; density for ~0.95 opacity by the far plane.
		HeightFog->SetFogDensity(FMath::Clamp(3.f / EndCm, 0.0001f, 0.05f));
		UE_LOG(LogElysium, Log, TEXT("fog on: %.0f-%.0f cm"), StartCm, EndCm);
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
