#pragma once

#include "CoreMinimal.h"

class UTextureCube;

// The `<map>.env` sidecar: 2D skybox flag/name and Source sky_camera fog. Distances are
// already converted to centimetres by the exporter; the fog colour is linear 0..1. The runtime
// turns these into image-based sky ambient and height fog.
struct FElysiumEnvDef
{
	bool bSky = false;             // six sky_*.png faces were decoded (skybox 1)
	FString SkyName;               // e.g. "la" (informational; faces are named sky_<face>.png)
	bool bFog = false;             // sky_camera had fogenable
	FLinearColor FogColor = FLinearColor::Black;
	float FogStartCm = 0.f;
	float FogEndCm = 0.f;

	static bool Parse(const FString& EnvPath, FElysiumEnvDef& Out);
};

namespace ElysiumEnvironment
{
	// The six Source sky faces (tex/sky_{ft,bk,rt,lf,up,dn}.png) -> a transient UTextureCube
	// in Unreal face order (+X,-X,+Y,-Y,+Z,-Z). Used only as SkyLight IBL, so per-face
	// rotation is irrelevant (the capture integrates the whole cube). Null if any face is
	// missing or the faces are not square and equal-sized.
	UTextureCube* BuildSkyCube(const FString& TexDir);
}
