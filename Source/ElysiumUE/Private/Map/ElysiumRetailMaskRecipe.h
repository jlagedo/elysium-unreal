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

	// The two `FMaskFilter` bits the world lane sets on primitives, so a query can drop a kind with
	// `FCollisionQueryParams::IgnoreMask` instead of walking actors.
	//   CharacterMaskBit: every character body -- set by lane A on the NPC body's capsule, and by
	//     lane B on the player pawn's hull.
	//   MoverMaskBit: every mover -- set by lane A on the mover brush component.
	//   PropMaskBit: every ENTITY prop's body (`prop_physics`, `prop_dynamic`, their box proxy) --
	//     met only under MONSTER. Static (GAME_LUMP) props wear no bit: retail's engine never hands
	//     them to the entity filter (R2 § 2), so every mask meets them.
	inline constexpr uint8 CharacterMaskBit = 1;
	inline constexpr uint8 MoverMaskBit = 2;
	inline constexpr uint8 PropMaskBit = 4;

	// The recipe for one retail mask.
	//   channel: SIGHT when the mask carries OPAQUE `0x80` or `0x800000` and no MONSTERCLIP;
	//            PLAYER when it carries PLAYERCLIP `0x10000` and no MONSTERCLIP;
	//            else `ECC_Pawn` (MONSTERCLIP `0x20000`: `0x2400b`, `0x202400b`, `0x2000b`).
	//   bCharacters = bProps = MONSTER; bMovers = MOVEABLE; mask 0 = bNothing.
	// A mask outside the retail set R1/R2 name is logged once, then answered by the same rules.
	//
	// UNPORTED ARMS of `StandardFilterRules 0x101d3080` (R2 § 2). The recipe carries its MONSTER
	// gate (`101d30f2`) and its MOVEABLE gate; these arms are named here and built nowhere:
	//   - render mode: an entity with `m_nRenderMode != 0` is rejected unless the mask carries
	//     WINDOW `0x2` (`101d3112`), so a render-transparent prop or brush entity does not stop
	//     `0x2804091`. The port meets it.
	//   - solid flag `0x20` (`101d30b6`): an entity carrying it is rejected under every mask. The
	//     port does not model solid flags on props or brush entities.
	//   - `blocks_traces` (`m_bBlocksTraces +0xfd`, the keyfield): admits a non-brush entity even
	//     without MONSTER (`101d30f2`). The port reads no such keyfield; such an entity prop is met
	//     only under MONSTER.
	//   - `npc_transparent` (the keyfield; `SetNPCTransparent`): a prop or brush entity carrying it
	//     is skipped by `FVisible`'s NPC-transparency test. The port meets it.
	FElysiumRetailMaskRecipe Recipe(int32 RetailMask);

	// True for the masks the recipe was written against (0018 story 6, R1/R2).
	bool IsListed(int32 RetailMask);
}
