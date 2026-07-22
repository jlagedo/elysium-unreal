#pragma once

#include "CoreMinimal.h"

class UTexture2D;

// Process-wide path -> UTexture2D cache for decoded map textures, plus a cache of
// 1x1 solid-colour fallbacks for materials with no albedo. PNGs are decoded from disk
// into transient textures; nullptr is cached for a missing/undecodable file so a broken
// path is probed only once. Cached textures are held by strong references so the GC
// never collects them out from under the material instances that sample them.
struct FElysiumTextureCache
{
	// Decode (or fetch cached) the texture at Dir/Rel. Returns nullptr on miss.
	static UTexture2D* LoadTex(const FString& Dir, const FString& Rel);

	// A 1x1 texture of a solid colour, cached per colour.
	static UTexture2D* SolidTex(const FLinearColor& Color);

	// Drop every strong reference (map unload): textures still bound by live material
	// instances survive until their owner dies; everything else becomes GC-collectable.
	static void FlushAll();
};
