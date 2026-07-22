#pragma once

#include "CoreMinimal.h"

struct FElysiumMaterialDef;
class UMaterialInstanceDynamic;

// Builds a material instance for one OBJ surface: a dynamic instance of the hand-authored
// master material M_VtMB_World with the surface's albedo texture bound. The master's
// texture parameter name is discovered by reflection, so the factory does not depend on a
// hard-coded parameter name. Surfaces with no albedo get a 1x1 solid fallback (Kd colour).
struct FElysiumMaterialFactory
{
	static UMaterialInstanceDynamic* Build(const FElysiumMaterialDef* Def, const FString& Dir, UObject* Outer);
};
