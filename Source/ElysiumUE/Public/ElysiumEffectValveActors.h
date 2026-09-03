#pragma once

#include "CoreMinimal.h"
#include "ElysiumEffectActor.h"
#include "ElysiumEffectValveActors.generated.h"

struct FElysiumDustState;
struct FElysiumSteamState;
struct FElysiumBeamState;

// R7.3 (`docs/architecture/effects-architecture.md` §5.6): the three Valve classes' actors, one per
// `dustmotes[]` / `steam[]` / `beams[]` row, each on its own authored family system and driven by
// its leaf's published state through `AElysiumMapActor`. Fields are the staged row's.

/** One `dustmotes[]` row (`func_dustmotes`) on `NS_ElysiumDust` (§5.6). */
UCLASS()
class ELYSIUMUE_API AElysiumDustActor : public AElysiumEffectActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Model = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SpawnRate = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FLinearColor Color = FLinearColor::White;   // rgb + alpha
	UPROPERTY(EditAnywhere, Category = "Elysium") float SpeedMaxCmS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SizeMinCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SizeMaxCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeMinS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeMaxS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float DistMaxCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bFrozen = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bStartDisabled = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Sprite;   // vtmb:material:particle/sparkles

	void Drive(const FElysiumDustState& Dust);

protected:
	virtual const TCHAR* DefaultSystemPath() const override;
	virtual void WriteParameters() override;

private:
	TArray<FVector> SpawnPointsLocal;
};

/** One `steam[]` row (`env_steam`) on `NS_ElysiumSteam` (§5.6). */
UCLASS()
class ELYSIUMUE_API AElysiumSteamActor : public AElysiumEffectActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 Type = 0;   // 0 normal, 1 heatwave
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bInitialState = true;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SpreadSpeedCmS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float SpeedCmS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float StartSizeCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float EndSizeCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Rate = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float JetLengthCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifetimeS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FLinearColor Color = FLinearColor::White;   // rendercolor + renderamt

	void Drive(const FElysiumSteamState& Steam);

protected:
	virtual const TCHAR* DefaultSystemPath() const override;
	virtual void WriteParameters() override;
};

/** One `beams[]` row (`env_beam`) on `NS_ElysiumBeam` (§5.6). */
UCLASS()
class ELYSIUMUE_API AElysiumBeamActor : public AElysiumEffectActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Start;    // LightningStart targetname
	UPROPERTY(EditAnywhere, Category = "Elysium") FString End;      // LightningEnd targetname
	UPROPERTY(EditAnywhere, Category = "Elysium") float WidthCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float EndWidthCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float NoiseAmplitudeCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FString Texture;  // vtmb:material:sprites/beama
	UPROPERTY(EditAnywhere, Category = "Elysium") float TextureScroll = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float RadiusCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float LifeS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float StrikeTimeS = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Damage = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") FLinearColor Color = FLinearColor::White;
	// `SpawnFlags` is the base row's (Source's beam bits: 1 start on, 4 random strike, 128/256 shade).
	// Provenance, inert in VtMB and here.
	UPROPERTY(EditAnywhere, Category = "Elysium") FString ImpactParticle;
	UPROPERTY(EditAnywhere, Category = "Elysium") bool bFacesPlayer = false;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Framerate = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") float Framestart = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elysium") int32 RenderFx = 0;

	void Drive(const FElysiumBeamState& Beam);

protected:
	virtual const TCHAR* DefaultSystemPath() const override;
	virtual void WriteParameters() override;

private:
	FVector StartCm = FVector::ZeroVector;
	FVector EndCm = FVector::ZeroVector;
	bool bHaveEndpoints = false;
};
