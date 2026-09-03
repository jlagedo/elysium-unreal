#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumEffectActor.h"   // FElysiumParticleTree -- the value both assets carry
#include "ElysiumEffectFamilies.generated.h"

class UNiagaraSystem;

// R7.3 (`docs/architecture/effects-architecture.md` §5.5, §5.9): the two data assets beside the
// effect actor -- the family overrides and the shared producer trees -- and the asset paths the
// runtime binds by.

// §5.5: one family override. Match a root name, else any leaf sprite stem; the matched actor
// plays `System` instead of the floor, writing the tree fields `Pins` names.
USTRUCT()
struct FElysiumEffectFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium") FName Family;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FString> RootNames;
	UPROPERTY(EditAnywhere, Category = "Elysium") TArray<FString> SpriteNames;
	UPROPERTY(EditAnywhere, Category = "Elysium") TSoftObjectPtr<UNiagaraSystem> System;
	// Tree field -> user parameter on `System` (`Rate` -> `User.FireRate`, ...). The field names
	// are the ramp slot table's (`Size`, `Rate`, `Burst`, `SpawnRadius`, ...) plus `Lifetime`,
	// `LifetimeMin`, `LifetimeMax`, read off the tree's first drawing leaf; a ramp pins as the
	// midpoint of its first keyframe.
	UPROPERTY(EditAnywhere, Category = "Elysium") TMap<FName, FName> Pins;
};

// `/Game/ElysiumAuthored/VFX/DA_EffectFamilies`. Editor-tuned; ships empty.
UCLASS()
class ELYSIUMUE_API UElysiumEffectFamilies : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium")
	TArray<FElysiumEffectFamily> Families;

	// The family whose RootNames carries `RootName`, else whose SpriteNames carries any drawing
	// leaf's sprite stem; null when none.
	const FElysiumEffectFamily* Match(const FString& RootName, const FElysiumParticleTree& Tree) const;
};

// `/ElysiumBaked/Particles/DA_ElysiumParticleTrees` (§5.9): the trees a code producer names,
// keyed by root id (`vtmb:particle:<key>`), built by the same `importers.effects` builder.
UCLASS()
class ELYSIUMUE_API UElysiumParticleTrees : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Elysium")
	TMap<FString, FElysiumParticleTree> Trees;
};

// The asset paths the runtime binds by (the editor-authoring lane creates the assets).
namespace ElysiumEffectAssets
{
	inline const TCHAR* FloorSystem = TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumParticle.NS_ElysiumParticle");
	inline const TCHAR* DustSystem  = TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumDust.NS_ElysiumDust");
	inline const TCHAR* SteamSystem = TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumSteam.NS_ElysiumSteam");
	inline const TCHAR* BeamSystem  = TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumBeam.NS_ElysiumBeam");
	inline const TCHAR* Families    = TEXT("/Game/ElysiumAuthored/VFX/DA_EffectFamilies.DA_EffectFamilies");
	inline const TCHAR* EffectType  = TEXT("/Game/ElysiumAuthored/VFX/ET_ElysiumEffects.ET_ElysiumEffects");
	inline const TCHAR* Trees       = TEXT("/ElysiumBaked/Particles/DA_ElysiumParticleTrees.DA_ElysiumParticleTrees");
	// §5.4: the material lane's children. The floor, the lit child, the depth-test-off child,
	// the DUDV child.
	inline const TCHAR* MaterialFloor   = TEXT("/ElysiumBaked/Materials/particles/MI_Particle.MI_Particle");
	inline const TCHAR* MaterialLit     = TEXT("/ElysiumBaked/Materials/particles/MI_ParticleLit.MI_ParticleLit");
	inline const TCHAR* MaterialNoZ     = TEXT("/ElysiumBaked/Materials/particles/MI_ParticleNoZ.MI_ParticleNoZ");
	inline const TCHAR* MaterialRefract = TEXT("/ElysiumBaked/Materials/particles/MI_ParticleRefract.MI_ParticleRefract");
	// The beam's sprite through the material lane (`vtmb:material:sprites/beama`).
	inline const TCHAR* BeamMaterial    = TEXT("/ElysiumBaked/Materials/sprites/MI_beama.MI_beama");

	// The slotted floor's shape: twenty identical leaf slots, 37 ramps each. A ramp travels as a
	// lookup table of RampSamples (lo, hi, 0) entries over normalized age -- the floor reads it with
	// one interpolated array fetch -- except Burst, whose block carries its raw keyframes (t, lo, hi)
	// in the first RampKeyframes entries and the rate ramp's maximum in the block's last entry.
	inline constexpr int32 MaxLeafSlots = 20;
	// The slot layout is fixed: slots 0..RootSlots-1 take the root-spawned leaves (the parent with
	// the most children first), and each child slot samples one fixed parent slot. A particle
	// reader only resolves as an emitter-level binding (the runtime copies every user DI onto the
	// component, where the reader cannot see the system), so the parent is a property of the slot,
	// not of the instance: three child slots under slot 0, three under slot 1, one under each of
	// slots 2..7. The staged corpus (R7.3) has at most eight roots, depth one, three children.
	inline constexpr int32 RootSlots = 8;
	inline constexpr int32 ChildSlotParent[MaxLeafSlots] =
		{ -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 1 };
	inline constexpr int32 RampCount = 37;
	inline constexpr int32 RampSamples = 32;
	inline constexpr int32 RampKeyframes = 5;
	inline constexpr int32 RampEntries = RampCount * RampSamples;
	inline constexpr int32 BurstRamp = 19;
	inline constexpr int32 RateRamp = 18;
}
