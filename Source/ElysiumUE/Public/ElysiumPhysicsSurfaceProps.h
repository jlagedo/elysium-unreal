#pragma once

#include "CoreMinimal.h"

/** Authored surface values. Chaos owns the physical mechanism; these fields retain
 * material, movement and sound rules without exposing an IVP memory layout. */
struct ELYSIUMUE_API FElysiumPhysicsSurfaceParameters
{
	float Friction = 0.f, Elasticity = 0.f, Density = 0.f, Thickness = 0.f, Dampening = 0.f;
	float AudioReflectivity = 0.f, AudioHardnessFactor = 0.f, AudioRoughnessFactor = 0.f;
	float ScrapeRoughThreshold = 0.f, ImpactHardThreshold = 0.f, AudioHardMinVelocity = 0.f;
	float MaxSpeedFactor = 0.f, JumpFactor = 0.f;
	uint16 StepLeft = 0, StepRight = 0, Impact = 0, ImpactSoft = 0, ImpactHard = 0, Scrape = 0;
	uint16 ScrapeSmooth = 0, ScrapeRough = 0, BulletImpact = 0, Rolling = 0;
	// Weapon metal,wood,blade,fist,bullet,claw; outcome norm,soak,crit.
	uint16 WeaponImpact[6][3] = {};
	uint16 GameMaterial = 0;
	uint8 Climbable = 0;
};

/** One named authored surface. The name/index identity belongs to the parser service. */
struct ELYSIUMUE_API FElysiumPhysicsSurfaceData
{
	uint16 NameSymbol = 0xffff;
	FElysiumPhysicsSurfaceParameters Values;
	float Friction() const { return Values.Friction; }
	float Elasticity() const { return Values.Elasticity; }
	float Density() const { return Values.Density; }
	float Thickness() const { return Values.Thickness; }
	float Dampening() const { return Values.Dampening; }
};

/** Surface authored-data parser and material lookup. Returned rows borrow
 * table storage: a later parse may relocate them, as retail's vector does. The process
 * service survives level/world teardown. No baked UPhysicalMaterial clamps are read. */
class ELYSIUMUE_API FElysiumPhysicsSurfaceProps final
{
public:
	FElysiumPhysicsSurfaceProps();
	FElysiumPhysicsSurfaceProps(const FElysiumPhysicsSurfaceProps&) = delete;
	FElysiumPhysicsSurfaceProps& operator=(const FElysiumPhysicsSurfaceProps&) = delete;
	int32 ParseSurfaceData(const FString& Filename, const FString& Text);
	int32 GetSurfaceIndex(const FString& Name) const;
	const FElysiumPhysicsSurfaceData* GetSurfaceData(int32 Index) const;
	FString GetSurfaceName(int32 Index) const;
	FString GetString(uint16 Symbol) const;
	int32 SurfaceCount() const { return Surfaces.Num(); }
	const TArray<FString>& Diagnostics() const { return Messages; }
	bool LoadDeployed(FString& OutError);
	static FElysiumPhysicsSurfaceProps& ProcessService();

private:
	uint16 Intern(const FString& Text);
	void Apply(FElysiumPhysicsSurfaceData& Row, const FString& Key, const FString& Value);
	TArray<FString> Symbols;
	TArray<uint16> ParsedFiles;
	TArray<FElysiumPhysicsSurfaceData> Surfaces;
	TArray<FString> Messages;
	bool bCreatedShadow = false;
	int32 ShadowIndex = 0;
};
