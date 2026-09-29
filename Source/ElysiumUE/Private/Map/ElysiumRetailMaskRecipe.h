#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

// How a retail contents mask is asked of this world (0018 story 6). A retail trace names one mask;
// Unreal traces one channel. The mask's BRUSH bits pick the channel whose profiles were computed
// from the same bits (`ElysiumContentsSignature.h`); its ENTITY bits pick which kinds of entity
// body the trace may meet beside the world. Pure: no world, no state beyond the once-per-mask log.
struct FElysiumRetailMaskRecipe
{
	// The channel the world answer traces on.
	ECollisionChannel Channel = ECC_Pawn;
	// MONSTER `0x2000000`: character bodies are met (and listed, never folded into the world answer).
	bool bCharacters = false;
	// MOVEABLE `0x4000`: movers (doors, trains, func_movelinear) are met.
	bool bMovers = false;
	// Solid props are met. Also MONSTER: R2 § 2 -- `StandardFilterRules 0x101d3080` rejects every
	// entity that is not a solid brush model unless the mask carries MONSTER (or the entity is
	// `blocks_traces`), and the engine itself never tests MONSTER against an entity.
	bool bProps = false;
	// Mask 0: the trace asks nothing, so it is clear without being traced.
	bool bNothing = false;
};

namespace ElysiumRetailMask
{
	// The retail contents bits the recipe reads (Source `bspflags.h`, as VtMB ships them).
	inline constexpr int32 Opaque = 0x80;             // CONTENTS_OPAQUE
	inline constexpr int32 SightBrush = 0x800000;     // VtMB's second sight bit, in `0x804091`'s brush half
	inline constexpr int32 Moveable = 0x4000;         // CONTENTS_MOVEABLE
	inline constexpr int32 PlayerClip = 0x10000;      // CONTENTS_PLAYERCLIP
	inline constexpr int32 MonsterClip = 0x20000;     // CONTENTS_MONSTERCLIP
	inline constexpr int32 Monster = 0x2000000;       // CONTENTS_MONSTER

	// The `FMaskFilter` bits the world lane sets on primitives, so a query can drop a kind with
	// `FCollisionQueryParams::IgnoreMask` instead of walking actors (`FMaskFilter` has 6 usable bits).
	//   CharacterMaskBit: every character body -- set by lane A on the NPC body's capsule, and by
	//     lane B on the player pawn's hull.
	//   MoverMaskBit: every mover -- set by lane A on the mover brush component.
	//   PropMaskBit: every ENTITY prop's body (`prop_physics`, `prop_dynamic`, their box proxy) --
	//     met only under MONSTER -- unless the prop is `blocks_traces` (`PropBodyMaskBits`). Static
	//     (GAME_LUMP) props wear no bit: retail's engine never hands them to the entity filter
	//     (R2 § 2), so every mask meets them.
	//   NpcTransparentMaskBit: an entity prop authored `npc_transparent` -- dropped by a trace whose
	//     filter is `FVisible` (`QueryIgnoreMask`).
	inline constexpr uint8 CharacterMaskBit = 1;
	inline constexpr uint8 MoverMaskBit = 2;
	inline constexpr uint8 PropMaskBit = 4;
	inline constexpr uint8 NpcTransparentMaskBit = 8;

	// The mask bits one entity prop's body wears, from its two authored keyfields, chosen at spawn.
	//   `blocks_traces` (`m_bBlocksTraces +0xfd`): `StandardFilterRules 0x101d3080` admits the entity
	//     without MONSTER (`101d30f2`), so it wears no `PropMaskBit` and every mask meets it.
	//   `npc_transparent` (`m_bNPCTransparent +0xfc`): `CTraceFilterFVisible::ShouldHitEntity
	//     0x10107630` skips it; every other filter meets it.
	inline uint8 PropBodyMaskBits(bool bBlocksTraces, bool bNpcTransparent)
	{
		return static_cast<uint8>((bBlocksTraces ? 0 : PropMaskBit)
			| (bNpcTransparent ? NpcTransparentMaskBit : 0));
	}

	// The other arms of `StandardFilterRules 0x101d3080` (R2 § 2). The recipe carries its MONSTER
	// gate (`101d30f2`) and its MOVEABLE gate; the rest, in retail's order:
	//   - solid flag `0x20` (`101d30b6`): an entity carrying it is rejected under every mask.
	//     CARRIED for a hit that names its entity (`EntityArmsReject`, applied per hit by
	//     `AElysiumMapActor::TraceRetail`), over `FElysiumEntity::RetailSolidFlags`.
	//   - `blocks_traces` (`m_bBlocksTraces +0xfd`, the keyfield): admits a non-brush entity even
	//     without MONSTER (`101d30f2`). AUTHORED: `"blocks_traces":"1"` on 21 `prop_dynamic` rows in
	//     ch_temple_1, sm_beachhouse_1, sm_hub_1 and sm_pier_1 (grep of every map's entity sidecar,
	//     `$ELYSIUM_WORK_ROOT/exports_v2/_sidecars/*/*.ents`, 108 maps, 2026-09-29). CARRIED (0019/6)
	//     for entity props as a per-body mask-bit choice at spawn (`PropBodyMaskBits`: such a prop
	//     wears no `PropMaskBit`), read from the authored keyfield by `ElysiumProp.cpp`,
	//     `ElysiumPhysProp.cpp` and `Visual/ElysiumEntityBodiesProps.cpp`. Spawn-time only: a
	//     runtime write of the field does not re-choose the bit (UNRECOVERED whether any script does).
	//   - render mode: an entity with `m_nRenderMode != 0` is rejected unless the mask carries
	//     WINDOW `0x2` (`101d3112`), so a render-transparent brush entity does not stop `0x2804091`.
	//     CARRIED like `0x20`, reading the entity record's authored `rendermode` keyfield (the
	//     runtime word `FElysiumEntity::RenderMode` is not bound from it).
	//   - `npc_transparent` (the keyfield; `SetNPCTransparent`): an entity carrying it is skipped by
	//     `FVisible`'s filter (`CTraceFilterFVisible::ShouldHitEntity 0x10107630`). AUTHORED on 8096
	//     rows (4429 `prop_dynamic`, 1312 `prop_physics`, NPCs and makers; same grep). Carried for
	//     characters (`ElysiumNpcSight::Visible`). CARRIED for entity props (0019/6): the prop wears
	//     `NpcTransparentMaskBit` from its authored keyfield at spawn, and a trace whose
	//     `FElysiumRetailTrace::Filter` is `FVisible` drops it (`QueryIgnoreMask`); every other
	//     filter meets it. Spawn-time only, like `blocks_traces` (a runtime `SetNPCTransparent` on a
	//     prop does not re-choose the bit).
	// The two carried arms reach only bodies whose hit names its entity: brush entities (their
	// component carries the handle). An entity prop's hit answers the static world's handle.
	inline constexpr int32 Window = 0x2;               // CONTENTS_WINDOW
	inline constexpr uint32 SolidFlagFilterRejected = 0x20;   // name unrecovered (`101d30b6`)

	// `StandardFilterRules`' two entity arms the port carries, in retail's order. True = the filter
	// refuses this entity under `RetailMask`, and the trace goes on past it.
	inline bool EntityArmsReject(int32 RetailMask, int32 RenderMode, uint32 SolidFlags)
	{
		if ((SolidFlags & SolidFlagFilterRejected) != 0)            // 101d30b6
		{
			return true;
		}
		return RenderMode != 0 && (RetailMask & Window) == 0;       // 101d3112
	}

	// How many entities one trace may pass over before the answer stands. Retail's filter has no
	// bound; the port re-traces once per refused entity, and this caps a pathological stack.
	inline constexpr int32 MaxRefusedEntityPasses = 8;

	// The per-hit loop. `DoTrace(Request, Out)` is one world trace honouring `Request.Ignore` by
	// handle; `Rejects(Out.HitEntity)` is the arms over the hit entity's words. A refused entity is
	// appended to a copy of the request's ignore list and the trace is asked again; the result is the
	// first answer whose hit the arms admit. Only the ignore list changes between passes.
	template <typename TRequest, typename TResult, typename TTrace, typename TRejects>
	bool TraceSkippingRefusedEntities(const TRequest& Request, TResult& Out, TTrace&& DoTrace,
		TRejects&& Rejects)
	{
		if (!DoTrace(Request, Out))
		{
			return false;
		}
		if (!Out.HitEntity.IsSet() || !Rejects(Out.HitEntity))
		{
			return true;
		}
		TRequest Next = Request;
		for (int32 Pass = 0; Pass < MaxRefusedEntityPasses && Out.HitEntity.IsSet()
			&& Rejects(Out.HitEntity); ++Pass)
		{
			Next.Ignore.Add(Out.HitEntity);
			if (!DoTrace(Next, Out))
			{
				return false;
			}
		}
		return true;
	}

	// The query's `IgnoreMask` for one recipe and filter. Characters never (the kernel folds them);
	// movers only under MOVEABLE; entity props only under MONSTER (a `blocks_traces` prop wears no
	// `PropMaskBit`, so it is met regardless); an `npc_transparent` prop is dropped when the filter is
	// `CTraceFilterFVisible` (`ShouldHitEntity 0x10107630`) -- `bFVisibleFilter`.
	inline uint8 QueryIgnoreMask(const FElysiumRetailMaskRecipe& Recipe, bool bFVisibleFilter)
	{
		return static_cast<uint8>(CharacterMaskBit
			| (Recipe.bMovers ? 0 : MoverMaskBit)
			| (Recipe.bProps ? 0 : PropMaskBit)
			| (bFVisibleFilter ? NpcTransparentMaskBit : 0));
	}

	// The recipe for one retail mask.
	//   channel: SIGHT when the mask carries OPAQUE `0x80` or `0x800000` and no MONSTERCLIP;
	//            PLAYER when it carries PLAYERCLIP `0x10000` and no MONSTERCLIP;
	//            else `ECC_Pawn` (MONSTERCLIP `0x20000`: `0x2400b`, `0x202400b`, `0x2000b`).
	//   bCharacters = bProps = MONSTER; bMovers = MOVEABLE; mask 0 = bNothing.
	// A mask outside the retail set R1/R2 name is logged once, then answered by the same rules.
	FElysiumRetailMaskRecipe Recipe(int32 RetailMask);

	// True for the masks the recipe was written against (0018 story 6, R1/R2).
	bool IsListed(int32 RetailMask);
}
