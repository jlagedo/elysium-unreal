#pragma once

#include "CoreMinimal.h"
#include "ElysiumSurfaceParams.h"
#include "Materials/MaterialInstanceDynamic.h"

// Source's distance fog, as the Custom Primitive Data slots every world / 3D-skybox / prop
// primitive carries. This namespace is the whole contract between the material graph
// (pipeline/unreal/mat_fog.py), the bake (pipeline/unreal/bake_map.py, FOG_CPD_*) and AElysiumMapActor::ApplySceneFog.
//
// WHY IT IS PER-PRIMITIVE. VtMB fogs the world and the 3D-skybox miniature with two different
// linear fogs, pushed and popped around two separate renders (RE-A8/RE-A9). Ported as one scene
// the two share screen depth — over the exported set the placed miniature's bounds sit 0–4,868 cm
// from the world's own against world diagonals of 11,124–40,334 cm, and on 6 of the 8 maps with a
// miniature its geometry lies inside the world's box — so no distance-based engine mechanism
// (FogCutoffDistance, a LocalFogVolume, a second fog actor) can tell them apart, and a deferred
// fog pass offers nothing else. The term therefore lives in the material and is driven per
// primitive from here.
//
// The 2D backdrop is exempt game-wide (RE-A9: every sky face carries `$nofog 1`), so it neither
// carries these slots nor uses the material that reads them.
namespace ElysiumFog
{
	// float4 — linear RGB (the authored 0-255 colour / 255), then an unused A.
	inline constexpr int32 SlotColor = 0;
	// Distance from the camera at which the fog begins, cm.
	inline constexpr int32 SlotStart = 4;
	// 1 / (end - start). Zero means "this primitive is not fogged".
	inline constexpr int32 SlotInvRange = 5;

	inline constexpr int32 NumFloats = 6;

	// VtMB's authored colours are gamma-encoded and its own math decodes them with a plain 2.2
	// (RE-A5: a light's transfer is `(colour/255)^2.2 · …`), which is also what every other
	// authored colour in this pipeline becomes — an albedo texture is imported sRGB and sampled
	// linear. A fog colour is no different, so `.env` transports the authored value verbatim
	// (/255) and the consumer decodes.
	//
	// The magnitudes say the same thing: the map's own sky radiance is 0.0034–0.0066 and C4 puts
	// a typical lit surface near there, so an undecoded 17/255 = 0.067 would make the fog three
	// to thirteen times brighter than the world it hangs in. Decoded it is 0.0021 — a dark haze
	// just under the walls, which is what VtMB draws.
	//
	// What this does NOT close is the display transfer: a linear value still meets a filmic
	// tonemapper whose toe crushes the dark end (B4/D7 measured it at up to 9x on the sky). That
	// is one named calibration for the whole render, not a per-term fudge, so nothing here
	// compensates for it.
	inline FLinearColor DecodeColor(const FLinearColor& Authored)
	{
		return FLinearColor(FMath::Pow(FMath::Max(Authored.R, 0.f), 2.2f),
			FMath::Pow(FMath::Max(Authored.G, 0.f), 2.2f),
			FMath::Pow(FMath::Max(Authored.B, 0.f), 2.2f), 1.f);
	}

	// Pack one fog set into those slots. `Color` is the authored colour / 255, as `.env` carries it.
	//
	// A set that is off — or degenerate, `EndCm <= StartCm` — yields an inverse range of 0, which
	// is exactly what an unwritten slot reads as (the engine memzeroes the whole block for a
	// primitive that carries none). So "not fogged" and "never written" are the same state, and
	// the material term is neutral by construction rather than by a branch.
	inline void Pack(bool bEnabled, const FLinearColor& Color, float StartCm, float EndCm,
		TArray<float>& Out)
	{
		const FLinearColor Linear = DecodeColor(Color);
		Out.SetNumUninitialized(NumFloats);
		Out[SlotColor + 0] = Linear.R;
		Out[SlotColor + 1] = Linear.G;
		Out[SlotColor + 2] = Linear.B;
		Out[SlotColor + 3] = 1.f;
		Out[SlotStart] = StartCm;
		Out[SlotInvRange] = (bEnabled && EndCm > StartCm) ? 1.f / (EndCm - StartCm) : 0.f;
	}

	// R5.3's chosen home for the decal axis (`seam_map_material.md` -> "Decal fog and wetness
	// homes"): a `UDecalComponent` is a `USceneComponent`, not a `UPrimitiveComponent`, so it
	// carries no Custom Primitive Data of its own and `Pack` above does not apply to it. A decal
	// is only ever a world surface (`mat_fog.fog_from_params`'s own docstring), so it needs one
	// per-map value, never a per-primitive one -- set here as the three named instance parameters
	// `M_V2_Decal` declares (`ElysiumSurfaceParamsDecal`) on an MID created at decal-spawn time,
	// parented to the shared imported `MI_<unit>`. Never a per-map material package: the same
	// values `Pack` already computes for the mesh/prop CPD path, applied to one more instance.
	inline void ApplyToDecalMID(UMaterialInstanceDynamic* Mid, bool bEnabled,
		const FLinearColor& Color, float StartCm, float EndCm)
	{
		if (!Mid)
		{
			return;
		}
		TArray<float> Data;
		Pack(bEnabled, Color, StartCm, EndCm, Data);
		Mid->SetVectorParameterValue(ElysiumSurfaceParamsDecal::Vectors::FogColor,
			FLinearColor(Data[SlotColor + 0], Data[SlotColor + 1], Data[SlotColor + 2],
				Data[SlotColor + 3]));
		Mid->SetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogStart, Data[SlotStart]);
		Mid->SetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogInvRange,
			Data[SlotInvRange]);
	}
}
