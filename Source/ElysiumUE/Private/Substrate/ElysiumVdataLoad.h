#pragma once

#include "CoreMinimal.h"

namespace ElysiumKeyValues { struct FKvNode; }

// The `vdata` loading helpers every rulebook section file shares. `ReadVdata`, `RootBlock` and
// `Index` are defined once module-wide in `ElysiumVdataLoad.cpp`.
namespace ElysiumVdata
{
	// Every loader opens its file the same way and fails with the same readable reason.
	bool ReadVdata(const TCHAR* Rel, TSharedPtr<ElysiumKeyValues::FKvNode>& OutRoot, FString& OutError);

	// The root block a file's whole content hangs off. Named because a wrong root is the one
	// failure that produces a silently empty table rather than an error.
	const ElysiumKeyValues::FKvNode* RootBlock(const TSharedPtr<ElysiumKeyValues::FKvNode>& Root,
		const TCHAR* Key, const TCHAR* Rel, FString& OutError);

	void Index(TMap<FString, int32>& Map, const FString& Key, int32 Value);

	// Trim the mix of spaces and tabs the tables pad with.
	inline FString Trim(const FString& S)
	{
		FString Out = S;
		Out.TrimStartAndEndInline();
		return Out;
	}
}
