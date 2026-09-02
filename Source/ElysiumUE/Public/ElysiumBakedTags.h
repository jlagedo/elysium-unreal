#pragma once

#include "CoreMinimal.h"

// The actor tags pipeline/unreal/bake_map.py stamps on the baked level's actors, and the only contract
// between the offline bake and AElysiumMapActor::AdoptBakedLevel. Tags (not Outliner folder
// paths) carry this because folders are editor-only metadata and vanish in a -game build, while
// AActor::Tags is plain serialized runtime data.
//
// Keep these in sync with the TAG_* constants in pipeline/unreal/bake_map.py.
namespace ElysiumBakedTags
{
	// One spatial cell of the world's baked geometry.
	inline const FName World(TEXT("elysium.world"));
	// The 3D-skybox miniature (sky_camera) geometry, already scaled/offset by the bake.
	inline const FName Sky(TEXT("elysium.sky"));
	// One GAME_LUMP static prop.
	inline const FName Prop(TEXT("elysium.prop"));
	// One WORLDLIGHTS source. Carries a second tag, Source(i), naming its `.lights` line so the
	// light rig can bind it back to the raw source data it re-derives intensity and reach from.
	inline const FName Light(TEXT("elysium.light"));
	// The sky light and the height fog, adopted so `UElysiumMapVisuals` can drive their values.
	inline const FName SkyLight(TEXT("elysium.skylight"));
	inline const FName Fog(TEXT("elysium.fog"));
	// The baked 2D-sky backdrop dome (R5.2, `MapsOnV2Models` maps only) — a StaticMeshActor,
	// distinct from `Sky` (the 3D-skybox miniature) so it does not fall into the miniature's
	// sky-fog stamping bucket.
	inline const FName SkyDome(TEXT("elysium.skydome"));
	// One projected decal.
	inline const FName Decal(TEXT("elysium.decal"));
	// One reflection capture at a `cubemaps[]` sample (R5.5, `MapsOnV2Models` maps only). Carries a
	// second tag, Source(i), naming its lump-42 row. Its image lives in the level's MapBuildData,
	// uploaded at registration; the runtime adopts nothing from it.
	inline const FName Capture(TEXT("elysium.capture"));
	// The map's unbound PostProcessVolume — where a per-map Lumen art-direction value lives
	// (D3). It ships neutral: nothing overridden, so it changes no pixel until an owner call
	// puts a number on it.
	inline const FName PostProcess(TEXT("elysium.ppv"));

	// The `.lights` line index tag carried alongside Light, e.g. "elysium.src=137".
	inline FName SourceIndex(int32 Index)
	{
		return FName(*FString::Printf(TEXT("elysium.src=%d"), Index));
	}

	// The line index a Light actor carries, or INDEX_NONE if it has no source tag.
	inline int32 ParseSourceIndex(const TArray<FName>& Tags)
	{
		static const FString Prefix(TEXT("elysium.src="));
		for (const FName& Tag : Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return FCString::Atoi(*Text.RightChop(Prefix.Len()));
			}
		}
		return INDEX_NONE;
	}
}
