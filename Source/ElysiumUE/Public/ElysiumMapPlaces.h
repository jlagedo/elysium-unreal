#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumMapPlaces.generated.h"

// One retail `CAI_Node` (`0xa8` bytes, constructed by `0x102fc5d0`) as the bake stages it from the
// map's AIN (0018 story 4). Only the words the node READS are here -- what the AIN holds and
// `GetPosition` (`0x102fb0d0`) consults. The node's run-time words (`+0x9c` the cooldown, `+0xa0`
// the attached hint) are the place set's, not the asset's: `FElysiumPlaceSet`.
//
// Unreal-native: the origin is centimetres and the yaw is the reflected Source yaw (the same
// Y-negating projection every entity origin takes), so the row can be placed as it is.
USTRUCT()
struct FElysiumPlaceRow
{
	GENERATED_BODY()

	// The node's network index, `+0x04`. The row's own position in `UElysiumMapPlaces::Rows`, kept
	// so a row read out of context still says which node it is.
	UPROPERTY(VisibleAnywhere, Category = "Place") int32 NetworkIndex = INDEX_NONE;
	// `+0x70`, the node type: 2 ground, 3 air, 4 climb (`CNodeEnt::Spawn` `0x102d7b50..0x102d7b62`).
	UPROPERTY(VisibleAnywhere, Category = "Place") int32 Type = 0;
	// `+0x74`, the node info bits. `GetPosition` reads `4` / `8` / `0x10` on a climb node.
	UPROPERTY(VisibleAnywhere, Category = "Place") int32 Flags = 0;
	// `+0x08..+0x10`, the raw node origin with no hull offset.
	UPROPERTY(VisibleAnywhere, Category = "Place") FVector OriginCm = FVector::ZeroVector;
	// `+0x6c`, the node yaw -- Unreal-native degrees (the negated Source yaw).
	UPROPERTY(VisibleAnywhere, Category = "Place") float YawDeg = 0.0f;
	// `+0x14 + 4 * hull`, one per retail hull (`ElysiumRetailHulls::Count`), centimetres. Retail's
	// row carries all 22 because `GetPosition` may be asked for any hull.
	UPROPERTY(VisibleAnywhere, Category = "Place") float ZOffsetCm[22] = {};
	// The authored `nodeid` (`m_nWCNodeID`, `CNodeEnt +0x454`) of the row this node was built from,
	// or -1. Provenance only: nothing at run time keys on it.
	UPROPERTY(VisibleAnywhere, Category = "Place") int32 WcId = INDEX_NONE;
	// The BSP entity index of the hint `CNodeEnt::Spawn` pairs with this node, or -1. What the
	// runtime's own counter walk must reproduce (`Elysium.Content.Places.*`).
	UPROPERTY(VisibleAnywhere, Category = "Place") int32 HintBspIndex = INDEX_NONE;
};

// One per-hull wander cap. `CapUnits` is SOURCE units, as the pick that reads it measures.
USTRUCT()
struct FElysiumPlaceWanderCap
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Wander") int32 Hull = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category = "Wander") float CapUnits = 0.0f;
	// The hull had no link of its own in this graph and took the human hull's figure.
	UPROPERTY(VisibleAnywhere, Category = "Wander") bool bFromHuman = false;
};

// A hint-making node row the counter ran past `NumNodes`: retail's `DAT_106c994c` arm.
USTRUCT()
struct FElysiumPlaceOutOfRange
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Pairing") int32 BspIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category = "Pairing") int32 Counter = INDEX_NONE;
};

// This map's AI-network nodes as cooked content (0018 story 4): the place set's rows, the pairing
// the bake computed with `CNodeEnt::Spawn`'s counter rule, the crosswalk pairs, the per-hull wander
// caps and the map's `UsedHullBits`. One asset per map, beside `DA_<map>_Entities`
// (`FElysiumContentPaths::BakedMapPlaces`). The runtime copies it into `FElysiumPlaceSet` at map
// activation and never reads it again.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMapPlaces : public UDataAsset
{
	GENERATED_BODY()
public:
	// The payload's `version`. The one `AuthorJson` reads.
	static constexpr int32 SupportedPayloadVersion = 1;

	UPROPERTY(VisibleAnywhere, Category = "Map") FString MapName;
	UPROPERTY(VisibleAnywhere, Category = "Map") int32 PayloadVersion = 0;
	// The network's node count (`*DAT_1093407c`). Equal to `Rows.Num()` in any asset that authored.
	UPROPERTY(VisibleAnywhere, Category = "Map") int32 NumNodes = 0;
	// The OR of `1 << hull` over every hull a link of this graph declares.
	UPROPERTY(VisibleAnywhere, Category = "Map") int32 UsedHullBits = 0;
	UPROPERTY(VisibleAnywhere, Category = "Map") TArray<FElysiumPlaceRow> Rows;
	// Node index pairs whose two ends both pair to a hint of type 11000 (`info_node_crosswalk`),
	// lower index first: the links `CAI_Hint::InputWalk` / `InputDontWalk` (`0x102f97c0`) write the
	// signal nibble of. The place set carries their state (0018 story 7).
	UPROPERTY(VisibleAnywhere, Category = "Map") TArray<FIntPoint> CrosswalkPairs;
	// One per pair, parallel: the link's hull-0 motion word (`link+0x0c`). A pair without the ground
	// bit (1) is jump-only -- the hub's 260-263 and 259-261 carry 2 -- and no pedestrian route takes
	// it (`0x102ff960`), so the pedestrian splice lays only walkable pairs.
	UPROPERTY(VisibleAnywhere, Category = "Map") TArray<int32> CrosswalkPairMotions;
	UPROPERTY(VisibleAnywhere, Category = "Map") TArray<FElysiumPlaceWanderCap> WanderCaps;

	// The bake's pairing report: how many node rows advanced the counter, the hint-making rows it
	// ran past, and the standalone rows (`info_hint` and the kick/shoot trio) that never advance it.
	UPROPERTY(VisibleAnywhere, Category = "Pairing") int32 PairingNodeRows = 0;
	UPROPERTY(VisibleAnywhere, Category = "Pairing") TArray<FElysiumPlaceOutOfRange> PairingOutOfRange;
	UPROPERTY(VisibleAnywhere, Category = "Pairing") TArray<int32> PairingStandalone;

	// Every row indexed by its own network index, 22 offsets each, and a count that agrees.
	bool IsValidPlaces() const;

#if WITH_EDITOR
	// Author the asset from the staged payload (`pipeline/unreal/bake_places.py`). False, with the
	// reason logged, on a payload of the wrong version or shape; the asset is then left as it was.
	UFUNCTION(BlueprintCallable, Category="Elysium|Import")
	bool AuthorJson(const FString& Json);
#endif
};
