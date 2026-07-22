#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElysiumMapActor.generated.h"

class UDirectionalLightComponent;
class UElysiumLightRig;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMesh;

// One loaded VtMB map, built at runtime from the shared Python-pipeline intermediates
// (no imported .uasset content): the exported OBJ as one procedural-mesh section per
// material with its albedo texture, the 3D skybox, placeholder lighting, and the player
// spawn. Everything map-scoped is a component of (or outer'd to) this actor, so destroying
// it unloads the map. Spawned only by UElysiumMapSubsystem (deferred, MapName set before
// FinishSpawning); BeginPlay builds the map.
UCLASS()
class AElysiumMapActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumMapActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Map name under FElysiumContentPaths::Root() (a folder holding <MapName>.obj).
	UPROPERTY(EditAnywhere, Category = "Elysium")
	FString MapName = TEXT("sp_tutorial_1");

	// Show/hide the 3D skybox mesh (bound to the T key by the pawn).
	void ToggleSkybox();
	bool IsSkyboxVisible() const;

	// Show/hide the real-time light rig (bound to the L key / elysium.lights).
	void ToggleLights();
	bool AreLightsVisible() const;

	// Show/hide the static-prop instances (elysium.props).
	void ToggleProps();
	bool ArePropsVisible() const;

	// Live stats for the debug overlay, filled by LoadMap.
	FString LoadedMap;
	int32 WorldSurfaceCount = 0;
	int32 SkySurfaceCount = 0;
	int32 WorldLightCount = 0;
	int32 PropInstanceCount = 0;
	int32 PropModelCount = 0;

private:
	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> WorldMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyMesh;
	// The 2D six-face skybox backdrop: a large inward box sampling the sky cubemap through
	// M_Sky. Distinct from SkyMesh (the 3D skybox miniature geometry).
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyDomeMesh;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> SunLight;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UElysiumLightRig> LightRig;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	// Static props: one ISM per (unique model, solidity) bucket, over runtime-built meshes.
	// Both arrays keep the objects alive for the map's lifetime (freed on map unload).
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> PropComponents;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> PropMeshes;
	bool bPropsVisible = true;

	void LoadMap();
	void LoadProps();
	int32 BuildMeshFromObj(const FString& ObjPath, UProceduralMeshComponent* Mesh, bool bCollision);
	void ApplySkyTransform();
	// Per-map colour grade (.cube), sky IBL ambient + height fog (.env). Sets the SkyLight
	// cubemap but leaves RecaptureSky to the caller.
	void ApplyEnvironment();
	bool ReadSpawn(FVector& OutLocation, float& OutYaw) const;

	// The player pawn may not exist yet in BeginPlay, so the teleport is deferred to Tick;
	// the pawn is then held frozen until the async collision cook yields ground beneath it.
	bool bSpawnPending = false;
	bool bSpawnPlaced = false;
	bool bSpawnDone = false;
	float SpawnHoldSeconds = 0.f;
	FVector PendingSpawnLoc = FVector::ZeroVector;
	float PendingSpawnYaw = 0.f;
};
