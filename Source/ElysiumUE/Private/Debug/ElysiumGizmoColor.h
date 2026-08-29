#pragma once

#include "CoreMinimal.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"

// A single representative world point for an entity (the gizmo marker, the beam endpoint, the
// label anchor, and the click-pick target): the brush body's world center if it has one, else the
// def origin (the point/logic entity's placement). One definition, because the click-pick must
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

// Coarse classname -> debug category. Read-at-a-glance keying over 100+ classnames without a
// per-class table. One enum drives the
// three things that have to agree: the gizmo colour, the World Viz class filter, and its labels.
enum class EElysiumGizmoClass : uint8
{
	Trigger,   // trigger*
	Light,     // light*, env_sprite
	Sound,     // *sound*, *ambient*
	Logic,     // logic*, math*, *relay*
	Prop,      // *prop*, *model*
	Other,
	Count
};

// One bit per category, for FVizSettings::GizmoClassMask. All bits set = show everything.
inline constexpr uint8 ElysiumGizmoClassBit(EElysiumGizmoClass C) { return uint8(1u << uint8(C)); }
inline constexpr uint8 ElysiumGizmoClassMaskAll = uint8((1u << uint8(EElysiumGizmoClass::Count)) - 1u);

inline EElysiumGizmoClass ElysiumGizmoClassOf(const FString& Cls)
{
	const FString L = Cls.ToLower();
	if (L.StartsWith(TEXT("trigger")))                                 { return EElysiumGizmoClass::Trigger; }
	if (L.StartsWith(TEXT("light")) || L.Contains(TEXT("env_sprite"))) { return EElysiumGizmoClass::Light; }
	if (L.Contains(TEXT("sound")) || L.Contains(TEXT("ambient")))      { return EElysiumGizmoClass::Sound; }
	if (L.StartsWith(TEXT("logic")) || L.StartsWith(TEXT("math")) || L.Contains(TEXT("relay")))
	{
		return EElysiumGizmoClass::Logic;
	}
	if (L.Contains(TEXT("prop")) || L.Contains(TEXT("model")))         { return EElysiumGizmoClass::Prop; }
	return EElysiumGizmoClass::Other;
}

// Triggers red, lights/sprites yellow, sound/ambient cyan, logic/math/relay magenta, props/models
// green, everything else grey. Shared by the retained gizmo ISM layer, the show-triggers
// overlay, and the World Viz filter's swatches.
inline FColor ElysiumGizmoClassColor(EElysiumGizmoClass C)
{
	switch (C)
	{
	case EElysiumGizmoClass::Trigger: return FColor(255,  90,  76);
	case EElysiumGizmoClass::Light:   return FColor(255, 230,  76);
	case EElysiumGizmoClass::Sound:   return FColor(102, 217, 255);
	case EElysiumGizmoClass::Logic:   return FColor(255, 102, 255);
	case EElysiumGizmoClass::Prop:    return FColor(102, 255, 128);
	default:                          return FColor(191, 191, 199);
	}
}

inline FColor ElysiumGizmoClassColor(const FString& Cls)
{
	return ElysiumGizmoClassColor(ElysiumGizmoClassOf(Cls));
}

// The filter's row labels — the classname patterns each category matches.
inline const TCHAR* ElysiumGizmoClassLabel(EElysiumGizmoClass C)
{
	switch (C)
	{
	case EElysiumGizmoClass::Trigger: return TEXT("trigger*");
	case EElysiumGizmoClass::Light:   return TEXT("light* / env_sprite");
	case EElysiumGizmoClass::Sound:   return TEXT("*sound* / *ambient*");
	case EElysiumGizmoClass::Logic:   return TEXT("logic* / math* / relay");
	case EElysiumGizmoClass::Prop:    return TEXT("*prop* / *model*");
	default:                          return TEXT("(other)");
	}
}

// Whether a classname passes a GizmoClassMask.
inline bool ElysiumGizmoClassVisible(const FString& Cls, uint8 Mask)
{
	return (Mask & ElysiumGizmoClassBit(ElysiumGizmoClassOf(Cls))) != 0;
}
