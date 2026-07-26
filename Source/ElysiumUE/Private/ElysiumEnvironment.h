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
	int32 SkyConvention = 0;       // the sky-face orientation contract version (skyconv); 0 = absent

	bool bFog = false;             // sky_camera had fogenable
	FLinearColor FogColor = FLinearColor::Black;
	float FogStartCm = 0.f;
	float FogEndCm = 0.f;

	static bool Parse(const FString& EnvPath, FElysiumEnvDef& Out);
};

namespace ElysiumEnvironment
{
	// The sky-face orientation contract this build assembles cubes under, matched against the
	// `.env` sidecar's `skyconv`. Version 1: the exported `sky_<face>.png` are verbatim decodes
	// carrying VtMB's own canonical orientation — rt=+X, lf=-X, bk=+Y, ft=-Y, up=+Z, dn=-Z in
	// Source space, image row 0 the top of the face, no face rotated or mirrored
	// (docs/sky-ambience.md -> "K1 ... (settled)"). Everything BuildSkyCube does to a face is
	// derived from that plus Unreal's own cube layout, so a sidecar written under a different
	// convention would silently draw wrong.
	inline constexpr int32 SkyConventionVersion = 1;

	// The six Source sky faces (tex/sky_{ft,bk,rt,lf,up,dn}.png) -> a transient UTextureCube
	// in Unreal face order (+X,-X,+Y,-Y,+Z,-Z). It feeds the SkyLight IBL *and* the visible
	// M_Sky backdrop, so both the face binding and the per-face rotation matter; the binding
	// is currently wrong (see the .cpp). Null if any face is missing or the faces are not
	// square and equal-sized.
	UTextureCube* BuildSkyCube(const FString& TexDir);
}
