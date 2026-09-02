#pragma once

#include "CoreMinimal.h"

// Source's sprite glow rule (R6.1, `docs/architecture/seam_map_map.md` -> "Sprites (R6.1)"),
// restated as pure functions so the proxy applies exactly what `Elysium.Substrate.SpriteGlow`
// pins. Every constant is `client.dll`'s `GlowBlend` (`100c24a0`): the defaults below are the
// `.rdata` doubles and the two cvar default strings, and `FParams::FromSettings` reads the
// Sprites page, which ships them unchanged.
namespace ElysiumSpriteGlow
{
	constexpr float CmPerInch = 2.54f;
	// `kRenderGlow` (screen-constant size, distance brightness, the visibility fade) and
	// `kRenderWorldGlow` (the same brightness and fade at the authored world size).
	constexpr int32 ModeGlow = 3;
	constexpr int32 ModeWorldGlow = 9;
	// `kRenderFxNoDissipation`: GlowBlend returns the smoothed visibility alone -- no distance
	// brightness and no screen-constant scaling (`100c30e9`: the whole block is skipped).
	constexpr int32 FxNoDissipation = 14;

	struct FParams
	{
		float Falloff = 19000.0f;
		float MinBrightness = 0.05f;
		float SizePerDistance = 1.0f / 200.0f;
		float FadeInSeconds = 0.2f;
		float FadeOutSeconds = 0.1f;
		float QueryFootprintPerDistance = 3.0f / 128.0f;
		int32 QueryGrid = 4;

		// The Sprites settings page's values (`UElysiumSpriteSettings`), the CDO.
		static FParams FromSettings();
	};

	inline bool IsGlowMode(int32 RenderMode)
	{
		return RenderMode == ModeGlow || RenderMode == ModeWorldGlow;
	}

	// The world width (or height) of one sprite for one view, in cm: a rendermode-3 corona
	// without NoDissipation is `size x dist x SizePerDistance` (screen-constant, the actor's
	// scale is not a factor; `100c3197`: WorldGlow keeps its size, `100c30e9`: so does fx 14);
	// everything else is `size x 2.54 x ActorScale` (a miniature card scales with the miniature).
	inline float WorldSizeCm(float SizeInches, int32 RenderMode, int32 RenderFx, float DistCm,
		float ActorScale, const FParams& P)
	{
		if (RenderMode == ModeGlow && RenderFx != FxNoDissipation)
		{
			return SizeInches * DistCm * P.SizePerDistance;
		}
		return SizeInches * CmPerInch * ActorScale;
	}

	// The distance term of a glow's blend: `clamp(Falloff / dist_in^2, MinBrightness, 1)`, 1 under
	// NoDissipation, and 1 for every non-glow mode (which VtMB never routes through GlowBlend).
	inline float Brightness(float DistCm, int32 RenderMode, int32 RenderFx, const FParams& P)
	{
		if (!IsGlowMode(RenderMode) || RenderFx == FxNoDissipation)
		{
			return 1.0f;
		}
		const float DistIn = DistCm / CmPerInch;
		if (DistIn <= UE_KINDA_SMALL_NUMBER)
		{
			return 1.0f;
		}
		return FMath::Clamp(P.Falloff / (DistIn * DistIn), FMath::Min(P.MinBrightness, 1.0f), 1.0f);
	}

	// One step of the visibility smoothing: the fraction rises at `1 / FadeInSeconds` and falls
	// at `1 / FadeOutSeconds` per second, never past the target. A zero time snaps.
	inline float Smooth(float Current, float Target, float DeltaSeconds, const FParams& P)
	{
		Target = FMath::Clamp(Target, 0.0f, 1.0f);
		if (Target < Current)
		{
			const float Step = P.FadeOutSeconds > UE_KINDA_SMALL_NUMBER ? DeltaSeconds / P.FadeOutSeconds : 1.0f;
			return FMath::Max(Current - Step, Target);
		}
		const float Step = P.FadeInSeconds > UE_KINDA_SMALL_NUMBER ? DeltaSeconds / P.FadeInSeconds : 1.0f;
		return FMath::Min(Current + Step, Target);
	}

	// The half-size, in cm, of the occlusion sample square at the sprite origin for one view:
	// VtMB's query quad scaled with the distance, floored at 1 cm so a near sprite still has a box.
	inline float QueryHalfSizeCm(float DistCm, const FParams& P)
	{
		return FMath::Max(DistCm * P.QueryFootprintPerDistance, 1.0f);
	}
}
