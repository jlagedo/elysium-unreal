#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"   // ElysiumLightStyle::StampUnstyled writes slot 6
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
	// `M_V2_Decal` declares (`ElysiumSurfaceParamsDecal`) on an MID `UElysiumDecalSubsystem` owns
	// (R7.2 ruling 4): created at MAP LOAD for every baked `elysium.decal` component it adopts,
	// and inside `Lay` for a runtime stain, parented to the unit's projector twin
	// `MI_<unit>_Decal` -- a bake cannot save an MID into a level, so there is nowhere earlier for
	// it to live. Never a per-map material package: the same values `Pack` already computes for
	// the mesh/prop CPD path, applied to one more instance.
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

// R7.4 (`water-architecture.md` ruling M, owner decision 4): VtMB's per-face LIGHTSTYLE, as the one
// Custom Primitive Data slot past the fog block above.
//
// VtMB modulates a face's LIGHTMAP page by its style's pattern -- the pier's 34 `objects/surf` foam
// cards carry style 1, 21 of them style 32 as well (G6) -- and the port has no lightmaps at all:
// Lumen replaces them project-wide (owner decision 4). So the style survives as a BRIGHTNESS the
// lit base colour and the emissive are multiplied by, driven from the same clock that already
// animates a styled light (`UElysiumLightRig`'s `StyleTime` / `StylePatterns`), written onto the
// styled chunk's primitive rather than onto a light.
//
// It is one shared block: `ElysiumFog` owns floats 0-5 on every world / sky / prop primitive and
// this owns float 6, so a writer of either must never resize the array from the front.
//
// THE NEUTRAL VALUE HAS TO BE WRITTEN, ON EVERY PRIMITIVE, BY WHOEVER MAKES IT. A parameter with
// `bUseCustomPrimitiveData` compiles to `Compiler->CustomPrimitiveData(index, MCT_Float)` and
// nothing else (`UE_5.8 MaterialExpressions.cpp:8427`): the material's own default value is the
// editor preview and is never a runtime fallback, and an unwritten slot reads 0
// (`FPrimitiveUniformShaderParametersBuilder::CustomPrimitiveData` copies `Data.Num()` floats into
// a zeroed array). The fog block underneath survives that convention because its unwritten 0 means
// "not fogged"; a BRIGHTNESS of 0 is black. So slot 6 is stamped 1.0 by the bake
// (`bake_map.set_fog`, on every component it places) and by the runtime
// (`ElysiumLightStyle::StampUnstyled`, at each runtime component's construction), and only then
// does `UElysiumLightRig`'s clock overwrite it on the styled ones.
namespace ElysiumLightStyle
{
	// Immediately after ElysiumFog's six.
	inline constexpr int32 SlotBrightness = ElysiumFog::NumFloats;
	inline constexpr int32 NumFloats = SlotBrightness + 1;
	static_assert(SlotBrightness == 6, "the bake and the material graph read slot 6 by number");

	// What a face with no style, and a component the clock never reaches, must read.
	inline constexpr float Unstyled = 1.f;

	// The scalar parameter the Lit / LitTranslucent / Water / TwoTexture masters declare over slot
	// 6 (`make_v2_materials.py`, mirrored in `ElysiumSurfaceParams.h`). Custom primitive data is
	// authored as a named parameter with `use_custom_primitive_data` set, exactly as the fog
	// triple is, so the name is part of the contract even though no material INSTANCE overrides it.
	inline const FName ParameterName(TEXT("LightStyleBrightness"));

	// Write the neutral brightness onto one runtime-created primitive. Call it wherever a component
	// that can bind a V2 master is built (`UElysiumEntityBodies`'s brush / prop / phys-prop / NPC
	// bodies, the wield sockets, the character stage) -- the bake's own components come out of
	// `set_fog` already stamped. Idempotent, and it never touches slots 0-5, so it composes with
	// `ApplySceneFog` in either order.
	inline void StampUnstyled(UPrimitiveComponent* Component)
	{
		if (Component != nullptr)
		{
			Component->SetCustomPrimitiveDataFloat(SlotBrightness, Unstyled);
		}
	}

	// The style a BRUSH-ENTITY mesh animates on, read off its material slot names (R7.4 G6).
	//
	// A world chunk carries its style in the chunk actor's own `elysium.style=<n>` tag, because the
	// bake places that actor. A brush entity's mesh is never placed: the runtime builds one
	// component per body when the entity world embodies it, so the fact has to travel on the ASSET.
	// It already does -- the bake names each section's slot `safe_name(<group key>)` and a styled
	// group's key ends in `#style<n>` (`map_geometry.section_key`), which folds to `_style<n>`. The
	// Python twin of this rule is `asset_names.brush_slot_style`; `bake_map.py` also fails the
	// bake loudly if an unstyled group key ever folds to the same shape.
	//
	// One primitive, one slot, so one style: a mesh whose sections disagree, or that mixes styled
	// and unstyled sections, animates on none (0) rather than dragging unstyled geometry with it.
	inline int32 StyleFromSlotNames(TConstArrayView<FName> SlotNames)
	{
		if (SlotNames.IsEmpty())
		{
			return 0;
		}
		int32 Agreed = 0;
		for (const FName& SlotName : SlotNames)
		{
			const FString Text = SlotName.ToString();
			int32 Marker = INDEX_NONE;
			if (!Text.FindLastChar(TEXT('_'), Marker))
			{
				return 0;
			}
			const FString Tail = Text.Mid(Marker + 1);
			if (!Tail.StartsWith(TEXT("style"), ESearchCase::CaseSensitive))
			{
				return 0;
			}
			const FString Digits = Tail.Mid(5);
			if (Digits.IsEmpty() || !Digits.IsNumeric())
			{
				return 0;
			}
			const int32 Style = FCString::Atoi(*Digits);
			if (Style < 1 || (Agreed != 0 && Style != Agreed))
			{
				return 0;
			}
			Agreed = Style;
		}
		return Agreed;
	}
}
