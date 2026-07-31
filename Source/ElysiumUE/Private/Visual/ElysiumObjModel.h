#pragma once

#include "CoreMinimal.h"

// One OBJ material: the fields the runtime binds onto a material instance. The blend flags
// (bScissor/bBlend/bAdditive) pick which world master the factory instances; the texture
// channels bind its named parameters. All channel paths are install-relative (e.g.
// "tex/brick_building1c.png"), resolved against the model's Dir by the texture cache.
struct FElysiumMaterialDef
{
	FString Name;
	FString Albedo;
	FString Emissive;        // map_Ke  -> $selfillum emission mask (RGB x alpha, "*_ke.png")
	FString Bump;            // bumpmap -> $bumpmap tangent-space normal map
	FString EnvMask;         // envmapmask -> $envmap reflectivity mask (empty = uniform when bEnvmap)
	FString BaseTex2;        // basetex2 -> WorldVertexTransition second albedo
	bool bScissor = false;   // illum 4    -> $alphatest (masked master)
	bool bBlend = false;     // blend 1    -> $translucent (translucent master)
	bool bAdditive = false;  // additive 1 -> $additive glow overlay (additive master, unlit)
	bool bEnvmap = false;    // envmap     -> $envmap reflective (Lumen roughness path)
	// envtint -> $envmaptint. Multiplies the reflection in VtMB's own shader, so it is the
	// reflection's colour and strength in one constant. White when unauthored.
	FLinearColor EnvTint = FLinearColor::White;
	FLinearColor Color = FLinearColor(0.6f, 0.6f, 0.65f);   // Kd fallback when no albedo

	// $envmaptint splits two ways, and the population is bimodal rather than a continuum
	// (docs/vtmb/reflections.md): grey is a reflection-strength dim-down, chromatic names a metal.
	// Translucent/additive surfaces are excluded -- the chromatic tints there are coloured
	// glass, which stays dielectric. Metalness is read off VtMB's authoring, never inferred.
	static constexpr float ChromaticSpread = 0.02f;

	bool IsChromatic() const
	{
		if (!bEnvmap || bBlend || bAdditive)
		{
			return false;
		}
		const float Lo = FMath::Min3(EnvTint.R, EnvTint.G, EnvTint.B);
		const float Hi = FMath::Max3(EnvTint.R, EnvTint.G, EnvTint.B);
		return (Hi - Lo) >= ChromaticSpread;
	}

	// Rec.709 luma of the tint, the grey half's reflection-strength scale. 1.0 when unauthored.
	float TintLuma() const
	{
		return 0.2126f * EnvTint.R + 0.7152f * EnvTint.G + 0.0722f * EnvTint.B;
	}
};

// Parses an exported OBJ + its MTL into raw mesh data: positions, UVs, per-material
// triangle-index groups, and material definitions. The exporter writes Godot-space
// vertices in metres; Parse converts them to Unreal space (centimetres) and reverses
// triangle winding so faces stay front-facing in Unreal's left-handed frame.
//
//   Godot (gx,gy,gz) metres  ->  Unreal (gx, gz, gy) * 100 cm
//
// UVs are already V-down (Source convention, matching Unreal) and pass through unchanged.
struct FElysiumObjModel
{
	TArray<FVector> Positions;                 // Unreal space, cm
	TArray<FVector2D> Uvs;
	TMap<FString, TArray<int32>> Groups;       // material name -> flat triangle indices (Unreal winding)
	TMap<FString, FElysiumMaterialDef> Materials;
	FString Dir;

	static bool Parse(const FString& ObjPath, FElysiumObjModel& Out);

	// Parse just an MTL into material defs (used by the mesh cook-cache path, which
	// skips the OBJ but still needs materials).
	static void ParseMtl(const FString& Path, TMap<FString, FElysiumMaterialDef>& Mats);

	// The line-oriented core of ParseMtl (file already read), so tests can drive it in memory.
	static void ParseMtlLines(const TArray<FString>& Lines, TMap<FString, FElysiumMaterialDef>& Mats);

	// The mtllib filename referenced by the OBJ (set by Parse).
	FString MtlName;
};
