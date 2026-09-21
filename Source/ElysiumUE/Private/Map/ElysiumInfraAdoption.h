#pragma once

#include "CoreMinimal.h"

class AElysiumInfraActor;
class AElysiumInfraIndex;
struct FElysiumEntityDefs;

// 0018 story 2: the baked AI infrastructure actors become the source of their entity defs.
//
// Runs once per map load, after the entity table is loaded (`DA_<map>_Entities`) and before
// anything reads it — the level script, the model preload walk,
// `FElysiumEntityWorld::Load`. Each actor rewrites the def at its OWN BSP index, in place: the
// table is never appended to, filtered or renumbered, because the index is the entity's handle
// and its save key.
namespace ElysiumInfraAdoption
{
	struct FInput
	{
		TArray<const AElysiumInfraActor*> Actors;
		TArray<const AElysiumInfraIndex*> Indices;
		// Actors that carried an infrastructure family tag but are no infrastructure class.
		int32 StrayCount = 0;
	};

	struct FResult
	{
		bool bActive = false;     // the level declared a baked set (carried an index actor)
		int32 Replaced = 0;       // defs rewritten from an actor
		int32 Moved = 0;          // of those, actors moved off their table origin (editor edits)
	};

	// Validate the whole declared set first — one index actor, every declared index in range,
	// adopted by exactly one actor of the declared family whose authored classname is the def's —
	// and only then rewrite. An actor moved in the editor moves its entity (`ApplyToDef`). A level
	// with no index actor leaves `Defs` untouched and answers true. On any refusal `Defs` is
	// untouched, `OutError` names every problem, and the answer is false.
	bool Apply(const FInput& Input, FElysiumEntityDefs& Defs, FResult& OutResult, FString& OutError);
}
