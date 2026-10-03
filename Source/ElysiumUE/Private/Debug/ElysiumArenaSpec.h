#pragma once

#include "CoreMinimal.h"

// By value: `FStanding` is a member of the green-room harness, which is compiled in every
// configuration, so the bag of things one stood-up arena owns lives here beside the spec rather
// than in the Shipping-gated builder. The spec itself stays pure — nothing below reads a world.
#include "ElysiumEntityHandle.h"
#include "UObject/WeakObjectPtr.h"

class AActor;

// The combat arena's specification: a clean square room with one cover block and a ring of
// anchors, stated as plain values with no UObject and no engine gameplay type — the same
// pure-spec/engine-half split `ElysiumGymSpec.h` and `ElysiumGymBuilder.h` use. The engine half is
// `Debug/ElysiumArenaBuilder.h`, which turns these values into collision, navigation and anchors
// and makes no decisions of its own.
//
// **The arena is not the movement gym.** The gym is a bracket instrument: its lanes are ramps,
// risers, apertures and gaps placed at `<movement constant> + <offset>`, and a body fighting on one
// is measuring the wrong thing. This is the opposite geometry on purpose — one flat plate a fight
// can cross at any gait, four walls that keep a chase inside the navigable surface, and exactly one
// solid tall enough to break an eye line.
//
// **What the cover block is for.** `FElysiumNpcSenses` asks the engine one world term —
// `IElysiumEmbodiment::QueryLineOfSight` between two points — and derives cone, range, the 2 s
// cadence and the occlusion debounce from it. A solid taller than a standing eye is therefore the
// only thing in this runtime that makes an NPC actually lose the player: `ENEMY_OCCLUDED`,
// `LOST_ENEMY` and the enemy transaction's re-selection all hang off that one trace. The block's
// height is stated against eye height for that reason and is not a scenery choice.
//
// **What the anchors are, and are not.** They are `intersting_place` nodes — VtMB's ambient
// destination system, which `FElysiumNpc::ClaimAmbientSpot` selects among by rating and group and
// then stands the body at to play a stance. They are NOT cover nodes. Placing the face anchors in
// the block's own line-of-sight shadow is what makes the two systems coincide without either
// pretending to be the other.
//
// **The cover nodes are separate, and are the real thing.** `FNode` is an `info_node_cover_*` row:
// the builder adopts the node set as the entity world's AI network (a stage world adopts none of
// its own) and spawns one row per node through `SpawnRuntimeEntity`, which turns it into an
// `ai_hint` bound to its network node by `CNodeEnt::Spawn`'s counter
// (`ElysiumNodeEntity::SpawnNodeRow`). Those hints are what the Troika tactical cover search
// (`0x102b7110` → `FindHintByClassMask(8, 1, radius)`) walks, so a character that ends up at one
// was put there by retail's own query.

namespace ElysiumArena
{
	// Every dimension below is in **Source units**; the builder multiplies by `ElysiumMove::U` once,
	// at emission, exactly as the gym spec does.

	// Half the interior floor, wall face to centre. 512 units is ~13 m to a side of the middle:
	// far enough that a pistol engagement is not point-blank at spawn, close enough that a melee
	// NPC crosses it in a few seconds rather than making every test a walk.
	inline constexpr float RoomHalfExtent = 512.0f;
	inline constexpr float FloorThickness = 32.0f;
	inline constexpr float WallThickness  = 32.0f;
	// Taller than a jump can clear from the floor, so a chase cannot leave the navigable surface.
	inline constexpr float WallHeight     = 320.0f;

	// The one cover solid. Its height is the load-bearing number: a standing VtMB eye sits near 64
	// units, so 96 puts the top of the block clearly above it and a body behind the block is
	// genuinely untraceable rather than nearly so.
	inline constexpr float BlockHalfWidth = 96.0f;
	inline constexpr float BlockHeight    = 96.0f;

	// How far back from a block face an anchor stands.
	//
	// Small on purpose, and the number is load-bearing rather than cosmetic. An `AElysiumNpcBody`
	// capsule is 34 cm in radius, so ~61 cm of clearance puts a claimed body against the solid with
	// room to stand and nothing more — which is what makes the block occlude it from a wide arc.
	// A setback of a stride or more is a body standing NEAR cover, hidden from almost nowhere, and
	// `Elysium.Substrate.ArenaSpec` fails a `bAgainstCover` claim made at that distance.
	inline constexpr float FaceAnchorSetback = 24.0f;
	// How far in from a corner a corner anchor stands.
	inline constexpr float CornerAnchorInset = 128.0f;
	// How far in from a wall a spawn pad stands.
	inline constexpr float PadInset = 128.0f;

	// One solid. Axis-aligned: nothing in this room is a ramp, which is the whole difference from
	// the gym.
	struct FSolid
	{
		FName Tag;                                // the spawned component's name, so a trace hit reads back
		FVector Center = FVector::ZeroVector;     // arena-relative, cm
		FVector Extent = FVector::ZeroVector;     // half-extents, cm
	};

	// One `intersting_place` node. `Rating` is what `PickHighestRatedCandidate` arbitrates on, and
	// the face anchors carry the higher one so a claim prefers the block's shadow to a bare corner.
	struct FAnchor
	{
		FName Name;
		FVector FeetOrigin = FVector::ZeroVector;   // arena-relative, cm
		float Yaw = 0.0f;
		int32 Rating = 0;
		/**
		 * Whether this anchor stands AGAINST the cover block.
		 *
		 * Deliberately a statement about adjacency rather than about a sight line, because the block
		 * sits at the room's centre and "is the block between this anchor and the middle" is
		 * therefore true of every point in the room — a test that reads as meaningful and measures
		 * nothing. What makes cover cover is standing close enough to the solid that it subtends a
		 * wide arc: an anchor a body's width off a face is hidden from most of the room, and one in a
		 * corner is hidden from none of it.
		 *
		 * Reported so a panel can say which anchors are cover and which are only somewhere to stand.
		 * The `intersting_place` entity carries no such distinction, because VtMB's does not — this
		 * is the spec's claim about its own geometry, and `Elysium.Substrate.ArenaSpec` holds it.
		 */
		bool bAgainstCover = false;
	};

	// The two cover hint types the arena authors. `CAI_Hint::Spawn` (`0x102d0b60`) gives both the
	// class word 1 that the cover search's mask admits, and fills their `target_dist_*`,
	// `target_angle_range` and `hint_rating` (3, then 2.5 through `NPC_Cover_Distance_Scalar`) from
	// its per-type rows — none of which the arena authors.
	inline constexpr int32 HintTypeCoverLow = 101;       // `info_node_cover_low`, 0x65
	inline constexpr int32 HintTypeCoverCorner = 10200;  // `info_node_cover_corner`, 0x27d8

	// How far off the block's south face the player stands for `gr_los on` — the mirror of the
	// `behind_cover` pad on the player's side, ~2 m back, so the block breaks the line from the
	// north pad.
	inline constexpr float PlayerCoverSetback = 80.0f;

	// The cover scenario's seats (centimetres, arena-relative; the pad they are measured against is
	// `far_ne`). The open seat's X, and how far past the block centre (beyond `BlockHalfWidth`) the
	// occluded seat stands on the far_ne -> centre line.
	inline constexpr float CoverSeatXCm = -300.0f;
	inline constexpr float CoverBehindSetbackCm = 150.0f;

	// The targetname the cover scenario's gunman carries. Stated here so the room's teardown and
	// the console verb that stages it name the same character.
	inline constexpr const TCHAR* CoverGunmanName = TEXT("arena_gunman");

	// One AI-network cover node: a row of the adopted network AND the hint-making node entity
	// spawned on it. `YawDeg` is Unreal-native, as `FElysiumPlaceRow::YawDeg` is; the spawned row's
	// `angles` key carries the negated (Source) yaw.
	struct FNode
	{
		FString Name;                               // the hint's targetname
		FVector FeetCm = FVector::ZeroVector;       // arena-relative, cm
		float YawDeg = 0.0f;
		int32 HintType = 0;
		int32 GroupId = 1;                          // `group_id`, folded to one group bit at Spawn
	};

	// A named place to put a spawned character. Feet-anchored, like a gym lane.
	struct FPad
	{
		FName Name;
		FVector FeetOrigin = FVector::ZeroVector;
		float Yaw = 0.0f;
	};

	struct FSpec
	{
		TArray<FSolid> Solids;
		TArray<FAnchor> Anchors;
		TArray<FPad> Pads;
		// The cover nodes, in network order: a node's index here IS its network index.
		TArray<FNode> Nodes;

		// Where the driven body starts: against one wall, facing the block down the room's axis.
		FVector PlayerFeet = FVector::ZeroVector;
		float PlayerYaw = 0.0f;
		// Where `gr_los on` puts it: behind the block from the north pad, facing it.
		FVector PlayerCoverFeet = FVector::ZeroVector;
		float PlayerCoverYaw = 0.0f;

		// The `gr_scenario cover` geometry (brief S4). The gunman stands on pad `far_ne` and these are
		// the player's two seats against it, both inside `npc_perception 3`'s ~1118 cm vision distance
		// for the open seat: `CoverSeatFeet` is on the line y = far_ne.y, which clears the block, and
		// `CoverBehindFeet` is past the block's centre on the far_ne -> centre line, so the block is
		// between the two. Yaws are Unreal-native and face the gunman.
		FVector CoverSeatFeet = FVector::ZeroVector;
		float CoverSeatYaw = 0.0f;
		FVector CoverBehindFeet = FVector::ZeroVector;
		float CoverBehindYaw = 0.0f;

		const FPad* FindPad(const FName& Name) const;
		const FAnchor* FindAnchor(const FName& Name) const;
		const FNode* FindNode(const FString& Name) const;
		// Every solid's arena-relative bounds — what the navigation volume is sized from and what
		// the camera frames.
		FBox Bounds() const;
		// The walkable plate's top surface, arena-relative. The Z every feet-anchored point sits on.
		float FloorZ() const;
	};

	// The whole room. Unlike the gym's, this derivation reads no tuning: the arena tests nothing
	// about the mover, so its dimensions are stated rather than generated.
	FSpec Build();

	// Everything one stood-up arena owns, so a caller tears the whole thing down by handing this
	// back to `Teardown`. Declared beside the spec rather than in the builder because the green-room
	// harness holds one by value and is compiled in Shipping, where the builder is not.
	struct FStanding
	{
		// The actor owning every solid.
		TWeakObjectPtr<AActor> Solids;
		// The runtime navigation-bounds volume built over them.
		TWeakObjectPtr<AActor> NavigationBounds;
		// The `intersting_place` entities, in spec order. Runtime entities in the substrate, so
		// these are handles rather than actors.
		TArray<FElysiumEntityHandle> Anchors;
		// The cover nodes' `ai_hint` entities, in spec order, as the room first stood them. A
		// console re-stand replaces them by name (`ElysiumArena::StandCoverNetwork`).
		TArray<FElysiumEntityHandle> Nodes;

		bool IsValid() const { return Solids.IsValid(); }
	};

	// Where an arena stands, for every harness that stands one. The world origin, for the same
	// reason the gym uses it: every recorded coordinate then reads small.
	inline FVector DefaultOrigin() { return FVector::ZeroVector; }
}
