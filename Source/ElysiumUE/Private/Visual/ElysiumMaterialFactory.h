#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

// The runtime's one material shape on a converted map: a dynamic child of the `MI_` the material lane
// imported for a `vtmb:material:` unit. It builds nothing — no master selection, no texture load,
// no feature switch: every VMT-derived value is the instance's own, wetness arrives through the
// `MPC_ElysiumEnvironment` write and scene fog through custom primitive data. The MID exists so
// that a runtime bind, when one is ruled, has a per-map-actor home that dies with the map; today
// nothing writes one, and the Substrate tier pins that the child carries no override of its own.
struct FElysiumMaterialFactory
{
	// A `UMaterialInstanceDynamic` parented to `Imported`, outered to `Outer`, with no parameter
	// override. Null in, null out.
	static UMaterialInstanceDynamic* Create(UMaterialInterface* Imported, UObject* Outer);
};
