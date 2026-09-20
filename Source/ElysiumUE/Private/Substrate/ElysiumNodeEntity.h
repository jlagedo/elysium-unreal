#pragma once

#include "CoreMinimal.h"

struct FElysiumEntityDef;

// `CNodeEnt` — every `info_node*` authoring entity and `info_hint` — never lives past its own
// spawn in retail. `CNodeEnt::Spawn` (`0x102d78d0`) either builds a `CAI_Hint` out of the row's raw
// keyvalue block (`FUN_102d2f30`, classname `ai_hint`) or makes none, and then removes the
// authoring entity on every branch. This is the def-level half of that lifecycle: which rows
// become a hint, and the classname the live entity carries. The graph-node half (the network node,
// its id, the removal of rows that make no hint) is 0018 story 4's.
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
}
