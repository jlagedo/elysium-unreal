#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElysiumEffectActor.generated.h"

class UElysiumEffectFamilies;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USkeletalMeshComponent;
class UTexture2D;
struct FElysiumWeatherEmitterState;

// R7.3 (`docs/architecture/effects-architecture.md` §5, `docs/architecture/seam_map_map.md` ->
// "Import — effects (R7.3)"): the staged particle tree as the bake writes it onto the placed
// actor, field for field. A ramp is a keyframe list in normalized age: `[[t, lo, hi], ...]`, a
// scalar one keyframe `(0, v, v)`, `lo != hi` rolled per particle at spawn.

USTRUCT()
struct FElysiumRampKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") float T = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Lo = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Hi = 0.f;
};

// A leaf's sprite (or its DUDV `normal`): the texture lane's `T_` asset and the half-extent
// aspect `(0.5 * w / max, 0.5 * h / max)` the size ramp multiplies.
USTRUCT()
struct FElysiumParticleSprite
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") FString Id;
	UPROPERTY(EditAnywhere, Category = "Elysium") TSoftObjectPtr<UTexture2D> Texture;
	UPROPERTY(EditAnywhere, Category = "Elysium") FIntPoint SizePx = FIntPoint(0, 0);
	UPROPERTY(EditAnywhere, Category = "Elysium") FVector2D Aspect = FVector2D(0.5, 0.5);

	bool IsSet() const { return !Texture.IsNull(); }
};

USTRUCT()
struct FElysiumParticleDecal
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") FString Id;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Texture;
	UPROPERTY(EditAnywhere, Category = "Elysium") float AngleSpread = 0.f;
};

// `collide {}`: the four scalars (defaults 1, 1, 0, 1), `self`, the child nodes spawned at the
// impact, and the decal set (the spawn is R7.2's runtime-stain seam; carried here).
USTRUCT()
struct FElysiumParticleCollide
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") float Bounce = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Friction = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Gravity = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Drag = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bSelfCollide = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bNested = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<int32> Spawn;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumParticleDecal> Decals;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bHasVdecal = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 VdecalFirst = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 VdecalLast = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") float VdecalAngleSpread = 0.f;
};

// The reaching `spawn {}` block: how the parent emits this node. Ramps run at the spawning
// particle's age (or the root's clock).
USTRUCT()
struct FElysiumParticleSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Rate;       // /s
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Burst;      // count
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bDistance = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> RadiusCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ThetaDeg;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> PhiDeg;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> XCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> YCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ZCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ElevationCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> RotationDeg;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Width;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Height;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Size;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Red;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Green;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Blue;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Color;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Mask;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Refract;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Timescale = 1.f;
};

// One `particleTrees{}.nodes[]` row. `Index` 0 is the root; parents precede children.
USTRUCT()
struct FElysiumParticleNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Index = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Id;        // vtmb:particle:<key>
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Name;
	UPROPERTY(EditAnywhere, Category = "Elysium") FName Kind;        // root | spawn | leaf | both
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bDraws = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bSpawns = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Parent = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Elysium") FName Via;         // spawn | collide
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 BlockIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Depth = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bResolved = true;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Fps = 30.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeS = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeMinS = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeMaxS = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bLoop = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> SizeCm;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Width;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Height;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> RotationDeg;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Red;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Green;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Blue;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Color;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Mask;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> Refract;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> RadiusSpeedCmS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ThetaSpeedDegS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> PhiSpeedDegS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> XSpeedCmS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> YSpeedCmS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ZSpeedCmS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ElevationSpeedCmS;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumRampKey> ParentSpeed;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bHasSpawn = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumParticleSpawn Spawn;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bMoveAlign = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bFlat = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bSortFront = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bNoZTest = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bLighting = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bPrecipitation = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") float DepthOffsetCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bSurfaceColorOptout = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumParticleSprite Sprite;
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumParticleSprite Normal;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bHasCollide = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumParticleCollide Collide;
};

USTRUCT()
struct FElysiumParticleTree
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") FString Root;      // vtmb:particle:<key>
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Name;      // as the entity spelled it
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FElysiumParticleNode> Nodes;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 LeafCount = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Depth = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 MaxKeyframes = 0;
};

// What the map actor resolved for one attach (§5.7): the parent's body and skeletal mesh, the
// socket, and the world location a tree attach reconstructs. Everything optional -- an origin
// attach carries nothing.
struct FElysiumEffectAttachment
{
	USceneComponent* ParentBody = nullptr;
	USkeletalMeshComponent* Skeletal = nullptr;
	FName Socket;
	bool bHasWorldLocation = false;
	FVector WorldLocationCm = FVector::ZeroVector;
};

/**
 * One `effects[]` row in the baked level (R7.3): an `env_particle` / `func_particle` standing on
 * the slotted floor `NS_ElysiumParticle` or its family override. The bake writes the row's fields
 * and the tree, tags it `elysium.effect` + `elysium.ent=<index>`; the runtime buckets it once
 * (`UElysiumMapVisuals::AdoptBakedLevel`) and the leaf's `ApplyEmitter` drives it by entity
 * index through `AElysiumMapActor`. Every field name is the staged row's.
 */
UCLASS()
class ELYSIUMUE_API AElysiumEffectActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumEffectActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The root: the one Niagara component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium")
	TObjectPtr<UNiagaraComponent> Niagara;

	// --- The row (`seam_map_map.md` -> "effects[]") ---
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 EntityIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Elysium") FName Classname;          // env_particle | func_particle
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Root;             // vtmb:particle:<key>
	UPROPERTY(EditAnywhere, Category = "Elysium") FString RootName;         // as the entity spelled it
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 AttachType = 0;     // the 19-value enum; func_particle 15
	UPROPERTY(EditAnywhere, Category = "Elysium") FString ParentName;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString AttachBone;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 AttachPoint = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bActiveAtSpawn = true;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bStartHidden = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SpawnBoundsCm = 1300.48f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float RampScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float RampTime = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FBox BoundsCm = FBox(ForceInit);   // func_particle: the brush AABB (world)
	UPROPERTY(EditAnywhere, Category = "Elysium") float VolumeScale = 1.f;           // func_particle: clamp(vol x 2^-21, 0.01, 100)
	// Provenance only (the row carries them; the class reads none of the three).
	UPROPERTY(EditAnywhere, Category = "Elysium") FVector AnglesDeg = FVector::ZeroVector;   // key `angles` as authored
	UPROPERTY(EditAnywhere, Category = "Elysium") FString TargetName;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 SpawnFlags = 0;
	UPROPERTY(EditAnywhere, Category = "Elysium") FElysiumParticleTree Tree;
	UPROPERTY(EditAnywhere, Category = "Elysium") TSoftObjectPtr<UNiagaraSystem> FamilySystem;
	// The five scale fields (no keyfield; code producers only).
	UPROPERTY(EditAnywhere, Category = "Elysium") FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SizeScale = 1.f;

	// --- Runtime behaviour (§5.2) ---
	// Restart: the system resets and activates. The caller re-attaches first (the parent may only
	// now exist).
	void TurnOn();
	// Let finish: spawning stops, live particles run out on their own timeline.
	void TurnOff();
	// Remove now: deactivate immediately and hide.
	void Kill();
	// The one rate float: `User.RateScale = RateScale x VolumeScale`.
	void SetRate(float InRateScale);
	float GetRate() const { return CurrentRate; }
	// Re-attach with a new mode (§5.7). `Attachment` is what the map actor resolved.
	void SetAttachType(int32 Mode, const FElysiumEffectAttachment& Attachment);
	void SetTint(const FLinearColor& InTint);
	void SetSizeScale(float InSizeScale);
	// The project fog set (`ElysiumFog.h`), world or sky by the actor's marker: packed into
	// `User.FogColor` / `User.FogStart` / `User.FogInvRange`, which the renderer's material binding
	// hands the sprite material -- `ElysiumFog::ApplyToDecalMID`'s pattern on a Niagara material.
	void ApplyFog(bool bEnabled, const FLinearColor& Color, float StartCm, float EndCm);

	// Drive from the leaf's published state: `TurnOnSerial` rising while active restarts,
	// `bActive` falling lets finish, `bDead` kills, `RateScale` reaches the one rate float every
	// publish. `Attachment` is consulted only when the actor (re)attaches -- the first activation
	// and an attach-type change -- and may be null for an origin attach.
	void Drive(const FElysiumWeatherEmitterState& Emitter, const FElysiumEffectAttachment* Attachment);

	// §5.5: pick the family override (or the floor) and bind the system. Called at adopt so a
	// data-asset edit needs no re-bake; `Families` may be null (the floor).
	void ResolveFamily(const UElysiumEffectFamilies* Families);
	// Bind the system and write every user parameter. Idempotent; BeginPlay calls it.
	void EnsureSystem();
	bool IsOn() const { return bOn; }
	bool IsKilled() const { return bKilled; }

protected:
	// The asset this actor plays when no family matched.
	virtual const TCHAR* DefaultSystemPath() const;
	// Write this actor's row onto the bound system. The base writes the tree (§5.3).
	virtual void WriteParameters();

	// Warn once per key for this actor; the effects on a hub would otherwise spam per publish.
	void WarnOnce(const FString& Key, const FString& Message);
	UNiagaraSystem* LoadSystem(const TCHAR* Path);
	UMaterialInterface* LoadMaterial(const TCHAR* Path);

	bool bSystemBound = false;
	bool bOn = false;
	bool bKilled = false;
	float CurrentRate = 0.f;

private:
	void WriteTree();
	void WriteFamilyPins();
	void WriteSpawnBoxFromBounds(const FBox& WorldBox);
	void AttachNow(int32 Mode, const FElysiumEffectAttachment& Attachment);

	// The family match the runtime resolved (its pins), or empty for the floor.
	TMap<FName, FName> FamilyPins;
	bool bFamilyResolved = false;
	// The last emitter publish the actor was driven by, for the rising / falling edges.
	bool bHaveDriven = false;
	bool bLastActive = false;
	uint32 LastTurnOnSerial = 0;
	int32 LastAttachType = 0;
	// Mode 9: the parent primitive whose bounds refresh the spawn box each tick.
	TWeakObjectPtr<UPrimitiveComponent> BoundsParent;
	TSet<FString> Reported;
};
