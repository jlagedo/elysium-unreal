#pragma once

#include "CoreMinimal.h"

struct FElysiumMaterialDef;
struct FElysiumTextureCache;
class UMaterialInstanceDynamic;

// Builds a material instance for one OBJ surface. The OBJ material's blend flags pick one of the
// four hand-authored world masters (M_World_Opaque / _Masked / _Translucent / M_Additive); the
// surface's textures bind that master's named parameters (Albedo, Emissive, BumpMap, EnvMask,
// BaseTex2) with the feature scalars switched on only where a channel is present. Surfaces with no
// albedo get a 1x1 solid fallback (Kd colour). Used by both the world mesh and the prop ISMs.
struct FElysiumMaterialFactory
{
	// Cache is the owning map's texture dedup index (all textures bound here belong to that map).
	static UMaterialInstanceDynamic* Build(const FElysiumMaterialDef* Def, const FString& Dir,
		UObject* Outer, FElysiumTextureCache& Cache);

	// A dynamic instance of the deferred-decal master M_Decal for one projected decal (7.2),
	// with the decal's albedo bound to the Albedo parameter (RGB -> BaseColor, A -> Opacity) and,
	// when the decal carries a `map_Ke` mask, the Emissive parameter + EmissiveScale switched on.
	// Set on a UDecalComponent via SetDecalMaterial.
	static UMaterialInstanceDynamic* BuildDecal(const FElysiumMaterialDef* Def, const FString& Dir,
		UObject* Outer, FElysiumTextureCache& Cache);
};
