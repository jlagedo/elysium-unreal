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

	// A white texture carrying an alpha ramp — the shape of a gradient, with its colour left to the
	// brush tint. One strip serves a veil (W x 1, ramping left to right), a hairline (1 x H) or a
	// solid bar (1 x 1, Alpha = {255}). Screens build these instead of shipping gradient assets,
	// because a ramp authored in code is the one the layout constants were tuned against.
	// Returns null on failure; callers cache the result.
	UTexture2D* MakeAlphaRamp(const TArray<uint8>& Alpha, int32 Width, int32 Height);
}
