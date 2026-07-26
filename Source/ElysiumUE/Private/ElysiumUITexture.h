#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace ElysiumUI
{
	// Decode a PNG off disk into a transient BGRA texture. The UI's offline-decoded sheets are read
	// 1:1 — the PL3 use-icon atlas, the PL5c sign backgrounds, the PL8 title lockup and HUD art —
	// unlike game-content textures, which go through FElysiumTextureCache (mips, DDS, streaming).
	// Returns null on any failure; callers cache the result, including the failure.
	UTexture2D* LoadPngTexture(const FString& PngPath);
}
