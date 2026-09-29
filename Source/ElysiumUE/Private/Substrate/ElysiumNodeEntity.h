#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

struct FElysiumEntityDef;
class FElysiumPlaceSet;

// `CNodeEnt` — every `info_node*` authoring entity and `info_hint` — never lives past its own
// spawn in retail. `CNodeEnt::Spawn` (`0x102d78d0`) either builds a `CAI_Hint` out of the row's raw
// keyvalue block (`FUN_102d2f30`, classname `ai_hint`) or makes none, and then removes the
// authoring entity on every branch (`0x1000e255` -> `0x101cd970`). This is that lifecycle at the
// def level: which rows become a hint, the classname the live entity carries, the network node id
// the hint takes from the node-row counter (0018 story 4, `SpawnNodeRow`), and the retirement of
// every row that makes no hint.
namespace ElysiumNodeEntity
{
	// The classname `FUN_102d2f30` creates the hint under (`s_ai_hint_1060a388`).
	extern const TCHAR* const HintClassname;

	// Is this classname one `CNodeEnt` spawns? Every `info_node*` classname the binary names except
	// `info_node_link` (`CAI_DynamicLink`), plus `info_hint`. Case-insensitive, as the entity
	// factory's lookup is.
	bool IsNodeClassname(const FString& Classname);

	// `FUN_102d7d30`, the first call `CNodeEnt::Spawn` makes: the hint type a node's CLASSNAME
	// forces, whatever `hinttype` it authored. A classname the table does not name keeps the
	// authored value (`info_node_werewolf_hint` keeps an authored 15000..15018 and is forced to 0
	// otherwise).
	int32 ClassHintType(const FString& Classname, int32 AuthoredHintType);

	// `CNodeEnt::Spawn`'s decision, over a def's classname and raw keys. The standalone set
	// (`info_hint`, `info_node_kick_over`, `info_node_kick_at`, `info_node_shoot_at`) makes a hint
	// iff its (class-forced) type is non-zero; every other node iff the type is non-zero OR it
	// authored a `Group`. `info_node_tzimisce` is `info_node` by then.
	bool MakesHint(const FString& Classname, const TMap<FString, FString>& Keys);

	// Apply the replacement to one def: a row that makes a hint becomes classname `ai_hint`, with
	// the authored classname kept on `SourceClassname`. Any other row is left as it is. Idempotent:
	// an `ai_hint` def is not a node classname. Answers whether the def was replaced.
	bool ApplyHintReplacement(FElysiumEntityDef& Def);

	// Is this one of `CNodeEnt::Spawn`'s four standalone classnames (`info_hint`,
	// `info_node_kick_over`, `info_node_kick_at`, `info_node_shoot_at`), which never take a node?
	bool IsStandaloneClassname(const FString& Classname);

	// Which arm of `CNodeEnt::Spawn` a def took.
	enum class ESpawnArm : uint8
	{
		NotNode,             // not a `CNodeEnt` classname; the def is untouched
		StandaloneHint,      // `0x102d7ba5`: a standalone row with a type -- a hint, node id -1
		StandaloneNoType,    // `0x102d7bde`: "WARNING: Hint node with no hint type!" and nothing
		NodeHint,            // `0x102d79b8`: a node row that made a hint, node id = the counter
		NodeNoHint,          // `0x102d79e2` with no hint: the counter still advances
	};

	struct FSpawnResult
	{
		ESpawnArm Arm = ESpawnArm::NotNode;
		// The hint's `m_nNodeID` (`+0x5e4`, written by `FUN_102d2f30` at `0x102d2fce`): the counter
		// for `NodeHint`, -1 for every other arm.
		int32 NodeId = INDEX_NONE;
		// The authoring row does not live: every arm but `NotNode` and the two hint arms. A hint
		// arm's def lives on AS the hint (`ApplyHintReplacement`), which is this runtime's
		// equivalent of retail's create-hint-then-remove-the-row.
		bool bRetired = false;
	};

	// `CNodeEnt::Spawn` (`0x102d78d0`) over one def, on the loaded-network arm, in BSP spawn order:
	// the hint decision (`MakesHint`), the def rewrite (`ApplyHintReplacement`), and for every
	// non-standalone node row the counter step (`FElysiumPlaceSet::SpawnNodeRow`), which attaches
	// `Hint` -- the handle the hint entity will be built at -- to its node when the counter is in
	// range and counts it out when it is not. A standalone row never touches the counter.
	FSpawnResult SpawnNodeRow(FElysiumEntityDef& Def, FElysiumPlaceSet& Places, const FElysiumEntityHandle& Hint);
}
