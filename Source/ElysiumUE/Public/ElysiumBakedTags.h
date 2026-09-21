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
	// The 3D-skybox miniature (sky_camera) geometry, already scaled/offset by the bake -- and
	// the miniature's scope marker for every class placed inside it: a sky chunk and a sky prop carry
	// it INSTEAD of a class tag (both AStaticMeshActors, one bucket), a Detail or Sprite actor
	// inside the miniature carries it BESIDE its class tag. AdoptBakedLevel therefore buckets by
	// the class tags first and reads InMiniature() second.
	inline const FName Sky(TEXT("elysium.sky"));
	// One GAME_LUMP static prop.
	inline const FName Prop(TEXT("elysium.prop"));
	// One GAME_LUMP detail model's instanced component (R6.3): an
	// AElysiumDetailPropActor carrying every `dprp` record of one model in lump order. Carries a
	// second tag, DetailModel(stem), naming the corpus mesh it draws.
	inline const FName Detail(TEXT("elysium.detail"));
	// One `env_sprite` billboard (R6.1): an AElysiumSpriteActor
	// carrying every value the bake wrote. Carries a second tag, EntityIndex(i), naming the
	// entity's lump ordinal so the leaf's visibility writes find it.
	inline const FName Sprite(TEXT("elysium.sprite"));
	// One effects entity (R7.3): an AElysiumEffectActor (or its
	// Dust / Steam / Beam subclass) carrying its staged row. Carries a second tag, EntityIndex(i),
	// so the leaf's `ApplyEmitter` / `ApplyDust` / `ApplySteam` / `ApplyBeam` writes find it, and
	// -- inside the miniature -- `Sky` beside it, like a sprite.
	inline const FName Effect(TEXT("elysium.effect"));
	// One WORLDLIGHTS source. Carries a second tag, Source(i), naming its lump-15 ordinal so the
	// light rig can bind it back to the calibration rows keyed by it.
	inline const FName Light(TEXT("elysium.light"));
	// The sky light and the height fog, adopted so `UElysiumMapVisuals` can drive their values.
	inline const FName SkyLight(TEXT("elysium.skylight"));
	inline const FName Fog(TEXT("elysium.fog"));
	// The baked 2D-sky backdrop dome (R5.2) — a StaticMeshActor,
	// distinct from `Sky` (the 3D-skybox miniature) so it does not fall into the miniature's
	// sky-fog stamping bucket.
	inline const FName SkyDome(TEXT("elysium.skydome"));
	// One projected decal.
	inline const FName Decal(TEXT("elysium.decal"));
	// One reflection capture at a `cubemaps[]` sample (R5.5). Carries a
	// second tag, Source(i), naming its lump-42 row. Its image lives in the level's MapBuildData,
	// uploaded at registration; the runtime adopts nothing from it.
	inline const FName Capture(TEXT("elysium.capture"));
	// The map's water volumes (R7.1): one AElysiumWaterVolumes carrying
	// every `water.volumes[]` row the stage emitted -- the CONTENTS_WATER brush hulls, their surface
	// plane and the volume's authored fog. One actor per map, not one per volume, because the runtime
	// asks "which volume is this point in" against all of them at once.
	inline const FName Water(TEXT("elysium.water"));
	// The map's unbound PostProcessVolume — where a per-map Lumen art-direction value lives
	// (D3). It ships neutral: nothing overridden, so it changes no pixel until an owner call
	// puts a number on it.
	inline const FName PostProcess(TEXT("elysium.ppv"));
	// AIN jump links (`AElysiumNavJumpLink`, `pipeline/unreal/bake_jump_links.py`). The engine's
	// navigation system consumes these actors; the runtime adopts nothing from the tag.
	inline const FName NavJump(TEXT("elysium.nav-jump"));

	// One overhead cable segment (0018 story 21-3): an `AElysiumRopeActor` carrying the eight facts
	// `UElysiumMapVisuals::BuildRopes` builds its `UCableComponent` from. Carries a second tag,
	// SourceIndex(i), naming its row in the staged `ropes` block -- the order the cables are built
	// in, and what `bake_verify.rope_errors` matches an actor to its row by.
	inline const FName Rope(TEXT("elysium.rope"));

	// The BSP-authored AI infrastructure families (0018 story 2), one `AElysiumInfraActor` subclass
	// each, every actor also tagged with `EntityIndex(i)` — its row in the map's entity table. The
	// map adopts them before the entity world is built and rebuilds each def at its own index.
	inline const FName InfraHint(TEXT("elysium.infra.hint"));
	inline const FName InfraPlace(TEXT("elysium.infra.place"));
	inline const FName InfraConversation(TEXT("elysium.infra.conversation"));
	inline const FName InfraMaker(TEXT("elysium.infra.maker"));
	inline const FName InfraNpc(TEXT("elysium.infra.npc"));
	// The one `AElysiumInfraIndex` a baked level carries: the declared set the adoption checks.
	inline const FName InfraIndex(TEXT("elysium.infra.index"));

	// The `.lights` line index tag carried alongside Light, e.g. "elysium.src=137".
	inline FName SourceIndex(int32 Index)
	{
		return FName(*FString::Printf(TEXT("elysium.src=%d"), Index));
	}

	// R5.6: the two facts a baked light carries because the runtime rig
	// still needs them after every derived value is baked -- the VtMB light type (0 texlight,
	// 1 point, 2 spot, 3 sun) for the viewer's readout and the non-spot batch toggle, and the
	// lightstyle index the rig animates per frame. Nothing else rides along: magnitude, radius and
	// cosines are inputs to a derivation a converted map no longer performs at load.
	inline FName LightType(int32 Type)
	{
		return FName(*FString::Printf(TEXT("elysium.type=%d"), Type));
	}
	inline FName LightStyle(int32 Style)
	{
		return FName(*FString::Printf(TEXT("elysium.style=%d"), Style));
	}

	// The entity lump ordinal a sprite actor stands for (R6.1), e.g. "elysium.ent=309" -- the
	// `FElysiumEntityHandle::Index` of the `env_sprite` whose inputs drive it.
	inline FName EntityIndex(int32 Index)
	{
		return FName(*FString::Printf(TEXT("elysium.ent=%d"), Index));
	}

	// The R1 corpus stem a detail actor draws, e.g. "elysium.model=models_scenery_plants_grass_grassa".
	inline FName DetailModel(const FString& Stem)
	{
		return FName(*FString::Printf(TEXT("elysium.model=%s"), *Stem));
	}

	// R6.7: whether a Detail or Sprite actor's tags put it inside the 3D-skybox miniature -- the
	// fact ApplySceneFog stamps by (the `sky_camera`'s set, not `worldspawn`'s) and ToggleSkybox
	// hides by.
	inline bool InMiniature(const TArray<FName>& Tags)
	{
		return Tags.Contains(Sky);
	}

	// The text after `Prefix` on the first tag carrying it, or empty if no tag does.
	inline FString ParseTagText(const TArray<FName>& Tags, const FString& Prefix)
	{
		for (const FName& Tag : Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return Text.RightChop(Prefix.Len());
			}
		}
		return FString();
	}

	// The stem a Detail actor carries (R6.3), or empty if it has no model tag.
	inline FString ParseDetailModel(const TArray<FName>& Tags)
	{
		return ParseTagText(Tags, TEXT("elysium.model="));
	}

	// The integer after `Prefix` on the first tag carrying it, or `Default` if no tag does.
	inline int32 ParseTagInt(const TArray<FName>& Tags, const FString& Prefix, int32 Default)
	{
		for (const FName& Tag : Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return FCString::Atoi(*Text.RightChop(Prefix.Len()));
			}
		}
		return Default;
	}

	// The line index a Light actor carries, or INDEX_NONE if it has no source tag.
	inline int32 ParseSourceIndex(const TArray<FName>& Tags)
	{
		return ParseTagInt(Tags, TEXT("elysium.src="), INDEX_NONE);
	}
	// The entity index a Sprite actor carries (R6.1), or INDEX_NONE if it has no entity tag.
	inline int32 ParseEntityIndex(const TArray<FName>& Tags)
	{
		return ParseTagInt(Tags, TEXT("elysium.ent="), INDEX_NONE);
	}
	// The VtMB light type a baked light carries (R5.6), or `Default` (a legacy-lane actor has none).
	inline int32 ParseLightType(const TArray<FName>& Tags, int32 Default = 1)
	{
		return ParseTagInt(Tags, TEXT("elysium.type="), Default);
	}
	// The lightstyle index a baked light carries (R5.6); 0, unanimated, when it has none.
	inline int32 ParseLightStyle(const TArray<FName>& Tags)
	{
		return ParseTagInt(Tags, TEXT("elysium.style="), 0);
	}
}
