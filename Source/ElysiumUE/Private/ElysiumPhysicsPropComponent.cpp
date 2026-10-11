#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumModelPhysicsInputs.h"
#include "PhysicsEngine/BodySetup.h"

UBodySetup* UElysiumPhysicsPropComponent::GetBodySetup()
{
	if (bUseSelectedBody)
	{
		const auto* Inputs = UElysiumModelPhysicsInputs::Find(GetStaticMesh());
		return Inputs ? Inputs->SelectedBody.Get() : nullptr;
	}
	return Super::GetBodySetup();
}
