#include "ElysiumMapActor.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMaterialFactory.h"
#include "ElysiumObjModel.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

namespace
{
	// Source (inches, right-handed) -> Unreal (cm, left-handed): negate Y, scale 2.54.
	FVector SourceToUnreal(double SX, double SY, double SZ)
	{
		return FVector(SX, -SY, SZ) * 2.54;
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

	SkyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyMesh"));
	SkyMesh->SetupAttachment(SceneRoot);
	SkyMesh->bUseAsyncCooking = true;
	// Query-only, blocking the Visibility trace channel only: the debug crosshair pick can
	// identify skybox geometry, but the sky never obstructs the player.
	SkyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SkyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SkyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Placeholder lighting until the .lights rig lands (M1) — owned components, so they
	// unload with the map. A gentle "soft key + fill" rig for walking around: a soft
	// directional sun gives geometry some shape, and the sky light supplies a fixed
	// constant ambient so no surface ever falls fully black.
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
	WorldSurfaceCount = BuildMeshFromObj(FElysiumContentPaths::MapObj(MapName), WorldMesh, true);
	UE_LOG(LogElysium, Log, TEXT("world '%s': %d surfaces"), *MapName, WorldSurfaceCount);

	const FString SkyObj = FElysiumContentPaths::MapSkyObj(MapName);
	if (FPaths::FileExists(SkyObj))
	{
		SkySurfaceCount = BuildMeshFromObj(SkyObj, SkyMesh, true);
		ApplySkyTransform();
		UE_LOG(LogElysium, Log, TEXT("skybox: %d surfaces"), SkySurfaceCount);
	}

	SkyLight->RecaptureSky();

	if (ReadSpawn(PendingSpawnLoc, PendingSpawnYaw))
	{
		bSpawnPending = true;
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
				OriginUnreal = SourceToUnreal(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
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

bool AElysiumMapActor::ReadSpawn(FVector& OutLocation, float& OutYaw) const
{
	// Debug spawn override for the tutorial while iterating: a fixed Unreal pose (from the
	// F1 overlay's UE / look readout) instead of the map's info_player_start.
	static constexpr bool bDebugSpawn = true;
	if (bDebugSpawn && MapName == TEXT("sp_tutorial_1"))
	{
		OutLocation = FVector(-173.f, 340.f, 323.f);
		OutYaw = 26.f;
		return true;
	}

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
			Origin = SourceToUnreal(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
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
	// Source yaw is measured in the opposite sense once Y is negated.
	OutYaw = -YawSrc;
	return true;
}

void AElysiumMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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
