#pragma once

#include "CoreMinimal.h"

// One OBJ material: the fields the runtime binds onto a material instance. M0 uses
// only Albedo (+ the alpha/flag fields, parsed and carried for M1). The named texture
// channels are install-relative paths (e.g. "tex/brick_building1c.png") resolved
// against the model's Dir by the texture cache.
struct FElysiumMaterialDef
{
	FString Name;
	FString Albedo;
	FString Emissive;        // map_Ke -> $selfillum emission mask (RGB x alpha, "*_ke.png")
	bool bScissor = false;   // illum 4  -> alpha-tested
	bool bBlend = false;     // blend 1  -> $translucent
	FLinearColor Color = FLinearColor(0.6f, 0.6f, 0.65f);   // Kd fallback when no albedo
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

	// The mtllib filename referenced by the OBJ (set by Parse).
	FString MtlName;
};
