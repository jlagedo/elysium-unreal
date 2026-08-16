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
// then stands the body at to play a stance. They are NOT cover nodes. Retail's cover selection
// reads `info_node_cover_*`, an entity this runtime does not carry, and `SCHED_TAKE_COVER_FROM_ORIGIN`
// is registered with nothing selecting it (`Substrate/ElysiumNpcCombatSchedules.cpp`). An NPC that
// ends up behind the block is there because an ambient claim or a path put it there, not because a
// cover query chose it. Placing the face anchors in the block's own line-of-sight shadow is what
// makes the two systems coincide without either pretending to be the other.

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

		// Where the driven body starts: against one wall, facing the block down the room's axis.
		FVector PlayerFeet = FVector::ZeroVector;
		float PlayerYaw = 0.0f;

		const FPad* FindPad(const FName& Name) const;
		const FAnchor* FindAnchor(const FName& Name) const;
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

		bool IsValid() const { return Solids.IsValid(); }
	};

	// Where an arena stands, for every harness that stands one. The world origin, for the same
	// reason the gym uses it: every recorded coordinate then reads small.
	inline FVector DefaultOrigin() { return FVector::ZeroVector; }
}
