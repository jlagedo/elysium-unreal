// The two effects "params" classes, registered inert: `params_particle` is a precache stub in VtMB -- the discipline auras are created by name
// from the discipline record walker, never by looking this entity up -- and `params_explosion` is
// the recipe holder `point_explosion` reads by targetname, joined at the stage. Neither takes an
// input; both spawn as plain records under a real class so their rows stop reporting as
// unresolved and nothing else happens.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"

static TUniquePtr<FElysiumEntity> MakeInertEffectParams() { return MakeUnique<FElysiumEntity>(); }

static FElysiumClassRegistrar GRegParamsParticle(
	TEXT("params_particle"), ElysiumBaseClassName(), &MakeInertEffectParams,
	[](FElysiumClassDesc&) {});

static FElysiumClassRegistrar GRegParamsExplosion(
	TEXT("params_explosion"), ElysiumBaseClassName(), &MakeInertEffectParams,
	[](FElysiumClassDesc&) {});
