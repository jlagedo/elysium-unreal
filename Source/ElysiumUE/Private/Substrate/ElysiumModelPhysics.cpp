#include "Substrate/ElysiumModelPhysics.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "ElysiumPhysicsPropComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

FBodyInstance* ElysiumModelPhysics::Identity(UStaticMeshComponent* Component)
{
	FBodyInstance* Body = Component && !Component->IsBeingDestroyed() ? Component->GetBodyInstance() : nullptr;
	return Body && Body->IsValidBodyInstance() && Component->IsSimulatingPhysics() ? Body : nullptr;
}

bool ElysiumModelPhysics::IsActive(UStaticMeshComponent* Component)
{
	const FBodyInstance* Body = Identity(Component);
	return Body && Body->IsInstanceAwake();
}

void ElysiumModelPhysics::Destroy(UStaticMeshComponent* Component)
{
	if (Component)
	{
		Component->SetSimulatePhysics(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Collision changes can create a native static actor even for a non-simulating component.
		// Destroy last so failed creation/removal cannot leave that replacement behind.
		Component->DestroyPhysicsState();
	}
}

bool ElysiumModelPhysics::Create(UStaticMeshComponent* Component, FElysiumPhysicsSurfaceProps& Surfaces,
	float MassBias, float OverrideMass, bool bStartAsleep, FString& OutError)
{
	OutError.Reset();
	if (!Component) { OutError = TEXT("missing placed component"); return false; }
	Destroy(Component); // retire any old native identity before validating its replacement.
	const UStaticMesh* Mesh = Component->GetStaticMesh();
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
	auto* Proxy = Cast<UElysiumPhysicsPropComponent>(Component);
	const UBodySetup* Setup = Inputs ? Inputs->SelectedBody.Get() : nullptr;
	if (!Inputs || !Inputs->bSourceBody || !Setup || Setup->AggGeom.GetElementCount() == 0)
	{
		if (!Inputs) OutError = TEXT("model requires body-input reimport: uv run elysium import models");
		else if (!Inputs->bSourceBody) OutError = TEXT("model has no source VPhysics body");
		else OutError = TEXT("source body has missing selected native collision cook: reimport model");
		return false;
	}
	if (!Proxy) { OutError = TEXT("placed model requires native selected-body component"); return false; }
	Proxy->bUseSelectedBody = true;
	if (!FMath::IsFinite(Inputs->InertiaScale) || Inputs->InertiaScale == 0.f
		|| !FMath::IsFinite(Inputs->LinearDamping) || Inputs->LinearDamping < 0.f
		|| !FMath::IsFinite(Inputs->AngularDamping) || Inputs->AngularDamping < 0.f)
	{
		OutError = TEXT("authored inertia/damping input unsupported by Chaos adapter");
		return false;
	}
	int32 SurfaceIndex = Inputs->SurfaceProperty.IsEmpty() ? -1 : Surfaces.GetSurfaceIndex(Inputs->SurfaceProperty);
	if (SurfaceIndex < 0) SurfaceIndex = Surfaces.GetSurfaceIndex(TEXT("default")); // 26018ba0
	const auto* Surface = Surfaces.GetSurfaceData(SurfaceIndex);
	if (!Surface) { OutError = TEXT("source surface service has no default material"); return false; }
	auto* Material = NewObject<UElysiumPhysicalMaterial>(Component);
	Material->SourceName = Surfaces.GetSurfaceName(SurfaceIndex);
	Material->AssetId = TEXT("vtmb:surface-property:") + Material->SourceName;
	Material->Friction = Surface->Friction();
	Material->StaticFriction = Surface->Friction();
	Material->RawElasticity = Surface->Elasticity();
	Material->Restitution = Surface->Elasticity();
	Material->RawDensity = Surface->Density();
	Material->Density = Surface->Density() / 1000.f;
	Material->Thickness = Surface->Thickness();
	Component->SetPhysMaterialOverride(Material);
	// Direct authored inputs. Chaos integrates the selected hulls and owns the tensor/motion.
	float Mass = Inputs->MassKg;
	if (MassBias > 0.f) Mass = FMath::Clamp(Mass * MassBias, .5f, 1000000.f); // 10191510
	if (OverrideMass > 0.f) Mass = OverrideMass;
	if (!FMath::IsFinite(Mass) || Mass <= 0.f) { OutError = TEXT("non-positive authored mass unsupported by Chaos"); return false; }
	// 26018ba0 ->2602ea60: authored inertia multiplies the backend's principal moments.
	// Unreal's tensor scale is geometric (squared at fixed mass), so convert the input.
	Component->BodyInstance.InertiaTensorScale = FVector(FMath::Sqrt(Inputs->InertiaScale < 0.f ? 1.f : Inputs->InertiaScale));
	Component->BodyInstance.bStartAwake = !bStartAsleep;
	Component->SetLinearDamping(Inputs->LinearDamping);
	Component->SetAngularDamping(Inputs->AngularDamping);
	Component->SetMassOverrideInKg(NAME_None, Mass, true);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetSimulatePhysics(true);
	FBodyInstance* Body = Identity(Component);
	if (!Body) { Destroy(Component); OutError = TEXT("Chaos body creation failed"); return false; }
	if (Inputs->bMassCenter)
	{
		FTransform MassSpace = Body->GetMassSpaceLocal();
		MassSpace.SetTranslation(Inputs->MassCenterCm * Component->GetComponentScale());
		Body->SetMassSpaceLocal(MassSpace);
	}
	if (bStartAsleep) Body->PutInstanceToSleep();
	else Body->WakeInstance();
	return true;
}
