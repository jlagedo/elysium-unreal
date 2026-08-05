#include "ElysiumInputAssets.h"

const FElysiumInputActionDefinition* UElysiumInputActionSet::Find(FName Id) const
{
	return Actions.FindByPredicate(
		[Id](const FElysiumInputActionDefinition& Definition) { return Definition.Id == Id; });
}
