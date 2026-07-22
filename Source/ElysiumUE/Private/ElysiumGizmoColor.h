#pragma once

#include "CoreMinimal.h"

// Coarse classname -> debug color, ported from the Godot viewer (WorldLoader.EntColor): triggers
// red, lights/sprites yellow, sound/ambient cyan, logic/math/relay magenta, props/models green,
// everything else grey. Read-at-a-glance keying over 100+ classnames without a per-class table.
// Shared by the retained gizmo ISM layer (P2.4) and the show-triggers overlay.
inline FColor ElysiumGizmoClassColor(const FString& Cls)
{
	const FString L = Cls.ToLower();
	if (L.StartsWith(TEXT("trigger")))                                 { return FColor(255,  90,  76); }
	if (L.StartsWith(TEXT("light")) || L.Contains(TEXT("env_sprite"))) { return FColor(255, 230,  76); }
	if (L.Contains(TEXT("sound")) || L.Contains(TEXT("ambient")))      { return FColor(102, 217, 255); }
	if (L.StartsWith(TEXT("logic")) || L.StartsWith(TEXT("math")) || L.Contains(TEXT("relay")))
	{
		return FColor(255, 102, 255);
	}
	if (L.Contains(TEXT("prop")) || L.Contains(TEXT("model")))         { return FColor(102, 255, 128); }
	return FColor(191, 191, 199);
}
