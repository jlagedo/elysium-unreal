#pragma once

#include "CoreMinimal.h"

// Single source of truth for the extracted-content root. Runtime code resolves every
// map/asset path through here. Today it points at the shared Python pipeline output
// (E:/dev/elysium/tools/out) that the Godot viewer also consumes; relocating content
// to a packaged folder later is a one-line change.
struct FElysiumContentPaths
{
	static FString Root() { return TEXT("E:/dev/elysium/tools/out"); }

	static FString MapDir(const FString& Map) { return Root() / Map; }
	static FString MapObj(const FString& Map) { return MapDir(Map) / (Map + TEXT(".obj")); }
	static FString MapSkyObj(const FString& Map) { return MapDir(Map) / (Map + TEXT("_sky.obj")); }
	static FString MapSpawn(const FString& Map) { return MapDir(Map) / (Map + TEXT(".spawn")); }
	static FString MapSky(const FString& Map) { return MapDir(Map) / (Map + TEXT(".sky")); }
};
