#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UTextureCube;

// The `<map>.env` sidecar: 2D skybox flag/name and Source sky_camera fog. Distances are
// already converted to metres by the exporter; the fog colour is linear 0..1. The runtime
// turns these into a per-map colour grade, image-based sky ambient, and height fog.
struct FElysiumEnvDef
{
	bool bSky = false;             // six sky_*.png faces were decoded (skybox 1)
	FString SkyName;               // e.g. "la" (informational; faces are named sky_<face>.png)
	bool bFog = false;             // sky_camera had fogenable
	FLinearColor FogColor = FLinearColor::Black;
	float FogStartMeters = 0.f;
	float FogEndMeters = 0.f;

	static bool Parse(const FString& EnvPath, FElysiumEnvDef& Out);
};

namespace ElysiumEnvironment
{
	// Adobe `.cube` 3D LUT -> a transient UTexture2D in Unreal's neutral colour-grading LUT
	// layout (16^3 unwrapped to 256x16, blue across tiles, red=x, green=y, sRGB off). The
	// source LUT (any LUT_3D_SIZE) is trilinearly resampled to 16^3. Returns null on a
	// missing or malformed file. Assign to FPostProcessSettings::ColorGradingLUT.
	UTexture2D* BuildColorGradeLUT(const FString& CubePath);

	// The six Source sky faces (tex/sky_{ft,bk,rt,lf,up,dn}.png) -> a transient UTextureCube
	// in Unreal face order (+X,-X,+Y,-Y,+Z,-Z). Used only as SkyLight IBL, so per-face
	// rotation is irrelevant (the capture integrates the whole cube). Null if any face is
	// missing or the faces are not square and equal-sized.
	UTextureCube* BuildSkyCube(const FString& TexDir);
}
