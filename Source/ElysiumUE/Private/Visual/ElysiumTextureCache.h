#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;

// Per-map decoded-texture dedup index: one UTexture2D per unique (path, sRGB) key, shared by
// every material instance the map builds, so a texture referenced by many surfaces decodes and
// uploads once. A null entry records a decode miss so a broken path is probed only once.
//
// Owned by the AElysiumMapActor for the map it built (a plain member, like the entity world), so
// the whole set is released when that actor is torn down on map unload — under OpenLevel hard
// travel the engine tears down the world and GC reclaims the textures, no manual flush. Nothing
// is shared across maps because only one map is ever resident at a time.
struct FElysiumTextureCache
{
	// Decode (or fetch cached) the texture at Dir/Rel. Returns nullptr on miss. bSRGB=false
	// loads linear data (normal maps, reflectivity masks) so the bytes aren't gamma-decoded;
	// it is part of the cache key, so the same path can be fetched both ways.
	UTexture2D* LoadTex(const FString& Dir, const FString& Rel, bool bSRGB = true);

	// A 1x1 texture of a solid colour, cached per colour.
	UTexture2D* SolidTex(const FLinearColor& Color);

private:
	// Strong references keep decoded textures alive for this map's lifetime; the maps (and their
	// refs) drop when the owning actor is destroyed on unload.
	TMap<FString, TStrongObjectPtr<UTexture2D>> TexCache;
	TMap<FString, TStrongObjectPtr<UTexture2D>> SolidCache;
};
