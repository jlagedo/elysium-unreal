#pragma once

#include "CoreMinimal.h"

// Editor-only World Outliner affordance (`docs/architecture/debug-tooling.md` Layer 0). Every runtime spawn
// path (the map actor, brush bodies, the light rig's lights, the prop ISMs) names its object from
// a readable label, so the PIE Outliner reads as a live scene browser. `SetActorLabel`/`SetFolderPath`
// are the actor-level hooks; component labels are just the object name shown under the actor.
//
// `ElysiumEditorObjectName` folds a readable label into an FName-safe object name to pass to
// NewObject. Only compiled in editor builds — call sites gate the whole label build behind
// `#if WITH_EDITOR`, so Shipping keeps auto-generated names and pays nothing.
#if WITH_EDITOR
inline FName ElysiumEditorObjectName(const FString& Label)
{
	FString Safe;
	Safe.Reserve(Label.Len());
	for (const TCHAR C : Label)
	{
		Safe.AppendChar((FChar::IsAlnum(C) || C == TEXT('_')) ? C : TEXT('_'));
	}
	return Safe.IsEmpty() ? NAME_None : FName(*Safe);
}
#endif
