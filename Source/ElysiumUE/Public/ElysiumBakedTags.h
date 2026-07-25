#pragma once

#include "CoreMinimal.h"

// The actor tags tools/bake_map.py stamps on the baked level's actors, and the only contract
// between the offline bake and AElysiumMapActor::AdoptBakedLevel. Tags (not Outliner folder
// paths) carry this because folders are editor-only metadata and vanish in a -game build, while
// AActor::Tags is plain serialized runtime data.
//
// Keep these in sync with the TAG_* constants in tools/bake_map.py.
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
	// The sky light and the height fog, adopted so the Lights Cog window can tune ambience live.
	inline const FName SkyLight(TEXT("elysium.skylight"));
	inline const FName Fog(TEXT("elysium.fog"));
	// One projected decal.
	inline const FName Decal(TEXT("elysium.decal"));

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
