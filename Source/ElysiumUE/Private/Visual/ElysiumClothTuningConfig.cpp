#include "ElysiumClothTuningConfig.h"

#include "ElysiumContentPaths.h"

#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCloth, Log, All);

// The values the resolver walks, as flag/member/name triples.
//
// A macro rather than eighteen hand-written members in each of two functions: the overlay and the
// completeness report differ only in what they do per value, and spelling the list out twice is how
// one of them silently loses a key when a nineteenth is added.
//
// `Material` is not in the list. It selects which material layer to overlay rather than being one
// of that layer's values, so it is read off the garment entry directly and reported with the stem
// in the message.
#define ELYSIUM_CLOTH_TUNING_VALUES(Visit) \
	Visit(bOverrideDensity, Density, TEXT("density")) \
	Visit(bOverrideEdgeStiffness, EdgeStiffness, TEXT("edge_stiffness")) \
	Visit(bOverrideBendStiffness, BendStiffness, TEXT("bend_stiffness")) \
	Visit(bOverrideAreaStiffness, AreaStiffness, TEXT("area_stiffness")) \
	Visit(bOverrideDamping, Damping, TEXT("damping")) \
	Visit(bOverrideLocalDamping, LocalDamping, TEXT("local_damping")) \
	Visit(bOverrideCollisionThickness, CollisionThickness, TEXT("collision_thickness")) \
	Visit(bOverrideFriction, Friction, TEXT("friction")) \
	Visit(bOverrideUseCCD, bUseCCD, TEXT("use_ccd")) \
	Visit(bOverrideGravityMultiplier, GravityMultiplier, TEXT("gravity_multiplier")) \
	Visit(bOverrideLinearVelocityScale, LinearVelocityScale, TEXT("linear_velocity_scale")) \
	Visit(bOverrideAngularVelocityScale, AngularVelocityScale, TEXT("angular_velocity_scale")) \
	Visit(bOverrideFictitiousAngularScale, FictitiousAngularScale, \
		TEXT("fictitious_angular_scale")) \
	Visit(bOverrideTetherStiffness, TetherStiffness, TEXT("tether_stiffness")) \
	Visit(bOverrideTetherScale, TetherScale, TEXT("tether_scale")) \
	Visit(bOverrideLeashReachFraction, LeashReachFraction, TEXT("leash_reach_fraction")) \
	Visit(bOverrideLeashMinCm, LeashMinCm, TEXT("leash_min_cm")) \
	Visit(bOverrideLeashMaxCm, LeashMaxCm, TEXT("leash_max_cm"))

void FElysiumClothTuningLayer::OverlayOnto(FElysiumClothTuningLayer& Target) const
{
#define ELYSIUM_CLOTH_OVERLAY_ONE(Flag, Value, Key) \
	if (Flag) \
	{ \
		Target.Flag = true; \
		Target.Value = Value; \
	}
	ELYSIUM_CLOTH_TUNING_VALUES(ELYSIUM_CLOTH_OVERLAY_ONE)
#undef ELYSIUM_CLOTH_OVERLAY_ONE

	if (bOverrideMaterial)
	{
		Target.bOverrideMaterial = true;
		Target.Material = Material;
	}
}

void FElysiumClothTuningLayer::ReportUnsetValues(TArray<FString>& OutErrors) const
{
#define ELYSIUM_CLOTH_REPORT_ONE(Flag, Value, Key) \
	if (!Flag) \
	{ \
		OutErrors.Add(FString::Printf(TEXT("%s declares no '%s'"), \
			*FElysiumContentPaths::AuthoredClothTuning(), Key)); \
	}
	ELYSIUM_CLOTH_TUNING_VALUES(ELYSIUM_CLOTH_REPORT_ONE)
#undef ELYSIUM_CLOTH_REPORT_ONE
}

#undef ELYSIUM_CLOTH_TUNING_VALUES

const UElysiumClothTuningConfig* UElysiumClothTuningConfig::Load()
{
	// Rooted rather than re-resolved, the same shape `UElysiumWieldTable::Load` uses: the table is
	// authored content with no map epoch of its own, and nothing else holds a reference to it.
	static bool bResolved = false;
	static UElysiumClothTuningConfig* Cached = nullptr;
	if (!bResolved)
	{
		bResolved = true;
		const FString Path = FElysiumContentPaths::AuthoredClothTuning();
		Cached = LoadObject<UElysiumClothTuningConfig>(nullptr, *Path);
		if (Cached != nullptr)
		{
			Cached->AddToRoot();
		}
		else
		{
			UE_LOG(LogElysiumCloth, Error,
				TEXT("the authored cloth tuning asset '%s' is not on the mount -- no garment can be "
				     "built, because there is no compiled fallback for what a garment is made of."),
				*Path);
		}
	}
	return Cached;
}

FElysiumClothTuningLayer UElysiumClothTuningConfig::ResolveGarment(const FString& Stem,
	int32 Definition, FName& OutMaterial, TArray<FString>& OutErrors) const
{
	// Every flag starts false, so "nothing ever set this" stays distinguishable from "a layer set
	// it to the number the struct happens to initialise to".
	FElysiumClothTuningLayer Resolved;
	Defaults.OverlayOnto(Resolved);

	const FElysiumClothGarmentDefinitions* const Model = Garments.Find(FName(*Stem));
	const FElysiumClothTuningLayer* const Entry =
		Model != nullptr && Model->Definitions.IsValidIndex(Definition)
			? &Model->Definitions[Definition]
			: nullptr;

	FName Material = Entry != nullptr && Entry->bOverrideMaterial ? Entry->Material : NAME_None;
	if (Material.IsNone())
	{
		Material = FallbackMaterial;
		OutErrors.Add(FString::Printf(
			TEXT("%s names no garment '%s' definition %d; fell back to '%s'"),
			*FElysiumContentPaths::AuthoredClothTuning(), *Stem, Definition, *Material.ToString()));
	}

	if (const FElysiumClothTuningLayer* const Declared = Materials.Find(Material))
	{
		Declared->OverlayOnto(Resolved);
	}
	else
	{
		OutErrors.Add(FString::Printf(TEXT("%s declares no material '%s'"),
			*FElysiumContentPaths::AuthoredClothTuning(), *Material.ToString()));
	}

	if (Entry != nullptr)
	{
		Entry->OverlayOnto(Resolved);
	}

	Resolved.ReportUnsetValues(OutErrors);

	OutMaterial = Material;
	return Resolved;
}
