#pragma once

#include "CoreMinimal.h"

class UTextureCube;

// The `<map>.env` sidecar: the 2D skybox flag/name, the face-orientation convention, and the
// map's TWO fog sets — `worldspawn`'s (the world's) and `sky_camera`'s (the 3D-skybox pass's
// own). Distances are already centimetres and the colours linear 0..1. The runtime turns these
// into image-based sky ambient and height fog.
struct FElysiumEnvDef
{
	bool bSky = false;             // six sky_*.png faces were decoded (skybox 1)
	FString SkyName;               // e.g. "la" (informational; faces are named sky_<face>.png)
	int32 SkyConvention = 0;       // the sky-face orientation contract version (skyconv); 0 = absent

	// The WORLD's fog, off `worldspawn`. What the player stands in.
	bool bFog = false;
	FLinearColor FogColor = FLinearColor::Black;
	float FogStartCm = 0.f;
	float FogEndCm = 0.f;

	// The 3D-SKYBOX PASS's own fog, off `sky_camera` — a second, separately-scoped set, with its
	// distances already carried into world units (the pass renders at 1/scale, so the exporter
	// multiplies by `scale`). VtMB pushes this for the miniature's draw and pops it again; the
	// 2D backdrop is fogged by neither. Scoped onto the miniature alone as a per-primitive
	// material term — ElysiumFog.h says why nothing else can.
	bool bSkyFog = false;
	FLinearColor SkyFogColor = FLinearColor::Black;
	float SkyFogStartCm = 0.f;
	float SkyFogEndCm = 0.f;

	static bool Parse(const FString& EnvPath, FElysiumEnvDef& Out);
};

// The `<map>.sky` sidecar: where the 3D-skybox miniature goes. VtMB renders the miniature as a
// second pass through a scaled view; we place it as real geometry instead, under the inverse of
// that view transform — `world(v) = Scale · (v − OriginCm)`, uniform, no rotation. `Scale` is an
// integer keyvalue on `sky_camera` and reads 16 on all 43 maps that have one. Absent for the
// other 65, which leaves the identity (scale 1, origin zero) and no miniature to place.
struct FElysiumSkyDef
{
	bool bValid = false;
	FVector OriginCm = FVector::ZeroVector;   // the sky_camera's origin, Unreal cm
	float Scale = 1.f;

	// Carry a raw miniature-space point into world space.
	FVector ToWorld(const FVector& V) const { return (V - OriginCm) * Scale; }

	static bool Parse(const FString& SkyPath, FElysiumSkyDef& Out);
};

namespace ElysiumEnvironment
{
	// The sky-face orientation contract this build assembles cubes under, matched against the
	// `.env` sidecar's `skyconv`. Version 1: the exported `sky_<face>.png` are verbatim decodes
	// carrying VtMB's own canonical orientation — rt=+X, lf=-X, bk=+Y, ft=-Y, up=+Z, dn=-Z in
	// Source space, image row 0 the top of the face, no face rotated or mirrored
	// (docs/vtmb/sky-ambience.md -> "K1 ... (settled)"). Everything BuildSkyCube does to a face is
	// derived from that plus Unreal's own cube layout, so a sidecar written under a different
	// convention would silently draw wrong.
	inline constexpr int32 SkyConventionVersion = 1;

	// The six Source sky faces (tex/sky_{rt,lf,ft,bk,up,dn}.png) -> a transient UTextureCube in
	// Unreal slice order (+X,-X,+Y,-Y,+Z,-Z), each face bound to its slice and rotated into
	// Unreal's D3D-derived cube layout (the table is in the .cpp). It feeds the SkyLight IBL
	// *and* the visible M_Sky backdrop, so both the binding and the per-face rotation matter.
	// Null if any face is missing or the faces are not square and equal-sized.
	UTextureCube* BuildSkyCube(const FString& TexDir);

	// The same assembly over six faces named `<Prefix><face>.png` in Dir, which is how the
	// labelled RE-A2 probe set is named (`<skyname>rt.png`, …). BuildSkyCube is this with
	// Prefix = "sky_".
	//
	// `OutUpperMean`, when given, receives the cube's **solid-angle-weighted mean linear
	// radiance over the upper hemisphere** — the part that actually lights, since the SkyLight
	// runs with `bLowerHemisphereIsBlack`. It is what turns VtMB's `emit_skyambient` magnitude
	// into a SkyLight intensity: VtMB states the sky's radiance as one number, so scaling the
	// cube so its own average matches that number gives the sky VtMB's *level* while keeping
	// the cube's *direction* (C1). 0 when the cube could not be built.
	UTextureCube* BuildSkyCubeFrom(const FString& Dir, const FString& Prefix,
		float* OutUpperMean = nullptr);

	// True when all six `<Prefix><face>.png` exist in Dir — the test for "is there an enhanced
	// face set for this map", asked before BuildSkyCubeFrom so a partial set falls back to the
	// faithful one rather than failing the sky outright.
	bool HasSkyFaces(const FString& Dir, const FString& Prefix);

	// The two halves of the K1 x K2 face->slice transform, exposed so the automation suite can
	// check them against the conventions they were derived from rather than against themselves.
	// `Slice` is 0..5 = +X, -X, +Y, -Y, +Z, -Z.

	// The Source face suffix feeding a slice ("rt", "lf", "ft", "bk", "up", "dn").
	const TCHAR* SkySliceFace(int32 Slice);

	// Where slice texel (X, Y) of an N x N face reads from in that face's decoded image — the
	// rotation half. Both images are row-major top-down.
	void SkySliceSource(int32 Slice, int32 X, int32 Y, int32 N, int32& OutSrcX, int32& OutSrcY);
}
