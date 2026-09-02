#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace ElysiumUI
{
	// A white texture carrying an alpha ramp — the shape of a gradient, with its colour left to the
	// brush tint. One strip serves a veil (W x 1, ramping left to right), a hairline (1 x H) or a
	// solid bar (1 x 1, Alpha = {255}). Screens build these instead of shipping gradient assets,
	// because a ramp authored in code is the one the layout constants were tuned against.
	// Returns null on failure; callers cache the result.
	//
	// This is the UI's one code-made texture. Every picture it draws is an imported asset
	// (`ElysiumUiArt.h`, R6.6); nothing decodes an image file at runtime any more.
	UTexture2D* MakeAlphaRamp(const TArray<uint8>& Alpha, int32 Width, int32 Height);
}
