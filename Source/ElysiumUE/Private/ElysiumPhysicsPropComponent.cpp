#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumModelPhysicsInputs.h"
#include "PhysicsEngine/BodySetup.h"
#include "Substrate/ElysiumModelPhysics.h"

UBodySetup* UElysiumPhysicsPropComponent::GetBodySetup()
{
	if (RecipeBody) return RecipeBody;
	if (bUseSelectedBody)
	{
		const auto* Inputs = UElysiumModelPhysicsInputs::Find(GetStaticMesh());
		return Inputs ? Inputs->SelectedBody.Get() : nullptr;
	}
	return Super::GetBodySetup();
}

void UElysiumPhysicsPropComponent::OnDestroyPhysicsState()
{
	ElysiumModelPhysics::RetireNative(this);
	Super::OnDestroyPhysicsState();
}
