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

bool UElysiumPhysicsPropComponent::ShouldCreatePhysicsState() const
{
	// An analytic RecipeBody owns its actual geometry. The invisible mesh carrier's render
	// compilation is not a physics input (game Load can encounter an asynchronously compiling Cube).
	return HasIndependentPhysicsInputs() ? UPrimitiveComponent::ShouldCreatePhysicsState() : Super::ShouldCreatePhysicsState();
}

bool UElysiumPhysicsPropComponent::HasIndependentPhysicsInputs() const
{
	if (RecipeBody) return true;
	if (!bUseSelectedBody || IsVisible()) return false;
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(GetStaticMesh());
	return Inputs && Inputs->bSourceBody && Inputs->SelectedBody && Inputs->SelectedBody->AggGeom.GetElementCount() > 0;
}

#if WITH_EDITOR
void UElysiumPhysicsPropComponent::PreStaticMeshCompilation()
{
	if (!HasIndependentPhysicsInputs() || IsVisible()) Super::PreStaticMeshCompilation();
}

void UElysiumPhysicsPropComponent::PostStaticMeshCompilation()
{
	if (HasIndependentPhysicsInputs() && !IsVisible())
	{
		// This render-only carrier has no geometry to rebuild. Super would recreate the
		// already published selected/analytic actor, retiring its recipe/controller/current aliases.
		return;
	}
	Super::PostStaticMeshCompilation();
}
#endif

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
