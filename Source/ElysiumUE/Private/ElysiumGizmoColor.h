#pragma once

#include "CoreMinimal.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"

// A single representative world point for an entity (the gizmo marker, the beam endpoint, the
// label anchor, and the click-pick target): the brush body's world center if it has one, else the
// def origin (the point/logic entity's placement). One definition, because the P2.6 pick must
// agree exactly with where the gizmo is drawn — "what you see is what you click" only holds if
// both read the same anchor.
inline FVector ElysiumGizmoAnchor(const FElysiumEntity& Ent)
{
	if (Ent.Body)
	{
		return Ent.Body->Bounds.Origin;
	}
	return Ent.Def ? Ent.Def->Origin : FVector::ZeroVector;
}

// The gizmo marker's size: the /Engine/BasicShapes/Cube is 100 cm, drawn at GizmoScale, so the
// marker is a 28 cm axis-aligned cube centred on the anchor. The pick ray-tests exactly this box.
inline constexpr float ElysiumGizmoScale = 0.28f;
inline constexpr float ElysiumGizmoHalfExtent = 100.0f * ElysiumGizmoScale * 0.5f;

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
