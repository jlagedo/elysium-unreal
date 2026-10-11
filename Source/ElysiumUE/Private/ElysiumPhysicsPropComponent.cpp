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

void UElysiumPhysicsPropComponent::OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(Flags, Teleport);
	// UE OnSyncBodies publishes dirty native transforms with SkipPhysicsUpdate, including
	// the final move of a particle that fell asleep. A game-side setter is not solve output.
	if (EnumHasAnyFlags(Flags, EUpdateTransformFlags::SkipPhysicsUpdate))
		ElysiumModelPhysics::RecordNativeOutput(this);
}

void UElysiumPhysicsPropComponent::NativeWake(UPrimitiveComponent*, FName)
{
	ElysiumModelPhysics::RecordNativeActivity(this, true);
}

void UElysiumPhysicsPropComponent::NativeSleep(UPrimitiveComponent*, FName)
{
	ElysiumModelPhysics::RecordNativeActivity(this, false);
}
