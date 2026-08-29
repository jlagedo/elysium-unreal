#include "Substrate/ElysiumPhysProp.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumProp.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

// Console gate for prop_physics simulation. 1 (default) simulates the body as a Chaos rigid body;
// 0 leaves it standing as a static, non-solid mesh (visual parity, like a prop_dynamic). Read in
// the leaf's Spawn — takes effect next load.
static TAutoConsoleVariable<int32> CVarPhysicsProps(
	TEXT("elysium.PhysicsProps"),
	1,
	TEXT("Simulate prop_physics bodies as Chaos rigid bodies (1, default) or stand them static/non-solid (0)."),
	ECVF_Default);

namespace
{
	const UBodySetup* PropBodySetup(const UStaticMeshComponent* Comp)
	{
		const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
		return Mesh ? Mesh->GetBodySetup() : nullptr;
	}

	// Does this body's mesh carry simple collision a Chaos rigid body can simulate against? The
	// bake writes one convex shape per `.phy` ledge, so an empty AggGeom means the model shipped
	// no VPhysics collision model at all.
	bool HasSimpleCollision(const UStaticMeshComponent* Comp)
	{
		const UBodySetup* Body = PropBodySetup(Comp);
		return Body && Body->AggGeom.GetElementCount() > 0;
	}

	// The model's authored `.phy` mass in kg, which the bake stored on the mesh's body setup, or
	// 0 when the model carries none. It has to be re-applied to the *component*: UBodySetup's
	// mass override is read off the owning primitive's own FBodyInstance whenever there is one
	// (UBodySetup::CalculateMass), and a component built at runtime never seeds that from the
	// asset — leaving Chaos to compute mass from hull volume instead (a 3 kg bin came out 48 kg).
	float AuthoredMassKg(const UStaticMeshComponent* Comp)
	{
		const UBodySetup* Body = PropBodySetup(Comp);
		return Body && Body->DefaultInstance.bOverrideMass
			? Body->DefaultInstance.GetMassOverride() : 0.0f;
	}
}

void FElysiumPhysProp::Spawn()
{
	BuildBody();
}

UPrimitiveComponent* FElysiumPhysProp::GetAttachBody() const
{
	return Visual;
}

void FElysiumPhysProp::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateBody();
}

void FElysiumPhysProp::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Visual)
	{
		Visual->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)), /*bSweep=*/false);
	}
}

void FElysiumPhysProp::OnRuntimeModelChanged()
{
	if (Visual)
	{
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	if (PosedVisual)
	{
		PosedVisual->DestroyComponent();
		PosedVisual = nullptr;
	}
	BuildBody();
}

void FElysiumPhysProp::InputWake(const FElysiumInputArgs&)
{
	if (Visual && bSimulating)
	{
		Visual->WakeAllRigidBodies();
	}
}

void FElysiumPhysProp::InputBreak(const FElysiumInputArgs& Args)
{
	if (bBroken)
	{
		return;
	}
	bBroken = true;
	GateBody();
	static const FName OnBreak(TEXT("OnBreak"));
	FireOutput(OnBreak, Args.Activator);
	UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
}

void FElysiumPhysProp::InputSkin(const FElysiumInputArgs& Args)
{
	SetSkin(Args.Param.ToInt());
}

void FElysiumPhysProp::SetSkin(int32 Family)
{
	Skin = Family;
	ApplySkin();
}

void FElysiumPhysProp::ApplySkin()
{
	if (!World || !Def)
	{
		return;
	}
	if (IElysiumEmbodiment* Embodiment = World->Embodiment())
	{
		if (PosedVisual)
		{
			Embodiment->ApplyAnimatedPropSkin(PosedVisual, Def->ModelMesh, Skin);
		}
		else if (Visual)
		{
			Embodiment->ApplyPropSkin(Visual, Def->ModelMesh, Skin);
		}
	}
}

void FElysiumPhysProp::InputFadeToSkin(const FElysiumInputArgs& Args)
{
	SetSkin(Args.Param.ToInt());
}

void FElysiumPhysProp::InputSetSkinFadeTime(const FElysiumInputArgs& Args)
{
	SkinFadeTime = Args.Param.ToFloat();
}

void FElysiumPhysProp::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), Visual ? TEXT("physics mesh") : TEXT("(none)"));
	Out.Emplace(TEXT("Simulating"), bSimulating && !bBroken && !IsInert() ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
	// The mass Chaos is actually using, so the model's authored `.phy` mass can be checked
	// against the body rather than inferred from the asset.
	Out.Emplace(TEXT("Mass"), Visual
		? FString::Printf(TEXT("%.2f kg"), Visual->GetMass()) : TEXT("(none)"));
}

void FElysiumPhysProp::BuildBody()
{
	if (!ElysiumPropBodiesEnabled() || !World || !Def || Def->ModelMesh.IsEmpty())
	{
		return;   // gated off, bare test world, or a record with no decoded prop mesh
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;
	}
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = Def->ModelMesh;
	Request.Location = Def->Origin;
	Request.Rotation = Def->ModelQuat;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	Request.Skin = Skin;
	Request.Physics = EElysiumPlacedModelPhysics::SimulatedProxy;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody PlacedBody = Embodiment->BuildPlacedModelBody(Request);
		Visual = PlacedBody.PhysicsProxy;
		PosedVisual = PlacedBody.Visual;
	}
	else
	{
		Visual = Embodiment->BuildPhysPropVisual(Def->ModelMesh, Def->Origin, Def->ModelQuat,
			Embodiment->BodyScaleFor(*Def));
	}
	if (!Visual)
	{
		return;
	}
	World->RegisterPropBody(Visual);
	if (PosedVisual)
	{
		World->RegisterNpcBody(PosedVisual);
	}
	if (Skin != 0)
	{
		Embodiment->ApplyPropSkin(Visual, Def->ModelMesh, Skin);   // authored on an alternate family
	}

	// A model with no collision model cannot simulate, and VtMB does not remove the entity over
	// it: CPhysicsProp::CreateVPhysics (vampire.dll @10191510) warns, drops the prop to
	// SOLID_NONE + MOVETYPE_NONE and returns true, leaving it standing as inert scenery. The
	// baked mesh carries that fact — no `.phy` meant no simple collision shapes — so read it off
	// the asset rather than tracking a second flag.
	if (!HasSimpleCollision(Visual))
	{
		UE_LOG(LogElysiumProp, Log,
			TEXT("%s model '%s' has no collision model — standing inert (VtMB parity)"),
			*DebugString(), *Def->ModelMesh);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		bSimulating = false;
	}
	// Simulate (or stand static under the console gate). Mass is the model's authored `.phy` value
	// (baked onto the mesh) unless the entity's own override_mass > 0, which outranks it —
	// Source's precedence. override_mass is -1 on every prop_physics in the exported maps, so
	// the authored mass is what nearly all of them weigh.
	else
	{
		bSimulating = CVarPhysicsProps.GetValueOnGameThread() != 0;
		if (bSimulating)
		{
			float Mass = FCString::Atof(*Def->Keys.FindRef(TEXT("override_mass")));
			if (Mass <= 0.0f)
			{
				Mass = AuthoredMassKg(Visual);
			}
			if (Mass > 0.0f)
			{
				Visual->SetMassOverrideInKg(NAME_None, Mass, true);
			}
			Visual->SetSimulatePhysics(true);
		}
		else
		{
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // static, non-solid parity
		}
	}

	if (IsInert() || bBroken)
	{
		GateBody();   // born hidden (start_hidden / a Spawn()-time Kill)
	}
}

void FElysiumPhysProp::GateBody()
{
	if (!Visual)
	{
		return;
	}
	const bool bDown = IsInert() || bBroken;
	// A composite physics prop draws only the authored skeletal pose. The static body remains an
	// invisible Chaos/collision proxy even when the entity wakes again.
	Visual->SetVisibility(PosedVisual == nullptr && !bDown);
	if (PosedVisual)
	{
		PosedVisual->SetVisibility(!bDown);
	}
	if (bDown)
	{
		Visual->SetSimulatePhysics(false);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else if (bSimulating)
	{
		Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
		Visual->SetSimulatePhysics(true);
	}
}

void FElysiumPhysHinge::PostSpawn()
{
	if (!World || !Def)
	{
		return;
	}
	// The constraint is a component like every other body: outer'd to the world's owner actor
	// (the actor is the component outer, not a service).
	AActor* Outer = World->GetOwnerActor();
	USceneComponent* Root = Outer ? Outer->GetRootComponent() : nullptr;
	if (!Root)
	{
		return;
	}

	UPrimitiveComponent* Body1 = ResolveBody(TEXT("attach1"));
	UPrimitiveComponent* Body2 = ResolveBody(TEXT("attach2"));
	if (!Body1 && !Body2)
	{
		UE_LOG(LogElysiumProp, Log, TEXT("%s: no attach body resolved — constraint skipped"), *DebugString());
		return;
	}

	FVector Axis = Def->HingeAxis;
	if (!Axis.Normalize())
	{
		Axis = FVector::UpVector;   // degenerate / absent axis → world Z
	}

	Constraint = NewObject<UPhysicsConstraintComponent>(Outer);
	Constraint->SetupAttachment(Root);
	// The constraint's local +X is the twist axis; orient the frame so it lies along the hinge axis.
	// The map actor sits at world origin, so relative == world for these Unreal-space values.
	Constraint->SetRelativeLocationAndRotation(Def->Origin, FRotationMatrix::MakeFromX(Axis).ToQuat());
	Constraint->RegisterComponent();
	Outer->AddInstanceComponent(Constraint);

	ConfigureAsHinge();
	Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
	World->RegisterConstraintBody(Constraint);
	UE_LOG(LogElysiumProp, Verbose, TEXT("%s hinge: %s <-> %s"), *DebugString(),
		Body1 ? TEXT("attach1") : TEXT("world"), Body2 ? TEXT("attach2") : TEXT("world"));
}

void FElysiumPhysHinge::InputTurnOff(const FElysiumInputArgs&)
{
	if (Constraint)
	{
		Constraint->BreakConstraint();
	}
}

void FElysiumPhysHinge::InputBreak(const FElysiumInputArgs& Args)
{
	if (Constraint)
	{
		Constraint->BreakConstraint();
	}
	static const FName OnBreak(TEXT("OnBreak"));
	FireOutput(OnBreak, Args.Activator);
}

void FElysiumPhysHinge::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("attach1"), Def ? Def->Keys.FindRef(TEXT("attach1")) : FString());
	Out.Emplace(TEXT("attach2"), Def ? Def->Keys.FindRef(TEXT("attach2")) : FString());
	Out.Emplace(TEXT("Axis"), Def ? Def->HingeAxis.ToString() : FString());
	Out.Emplace(TEXT("Constraint"), Constraint ? TEXT("live") : TEXT("(none)"));
}

UPrimitiveComponent* FElysiumPhysHinge::ResolveBody(const TCHAR* Key)
{
	const FString Name = Def->Keys.FindRef(Key);
	if (Name.IsEmpty())
	{
		return nullptr;   // empty attach → the world frame
	}
	FElysiumEntity* Found = World->FindByName(Name);
	return Found ? Found->GetAttachBody() : nullptr;
}

void FElysiumPhysHinge::ConfigureAsHinge()
{
	// One free rotational DOF about the twist axis; everything else locked.
	Constraint->SetLinearXLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearYLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearZLimit(LCM_Locked, 0.0f);
	Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
	Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
	Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.0f);

	// forcelimit/torquelimit = 0 → unbreakable (Source semantics); > 0 → break threshold.
	const float ForceLimit = FCString::Atof(*Def->Keys.FindRef(TEXT("forcelimit")));
	const float TorqueLimit = FCString::Atof(*Def->Keys.FindRef(TEXT("torquelimit")));
	if (ForceLimit > 0.0f)
	{
		Constraint->SetLinearBreakable(true, ForceLimit);
	}
	if (TorqueLimit > 0.0f)
	{
		Constraint->SetAngularBreakable(true, TorqueLimit);
	}

	// hingefriction → resist rotation via a zero-velocity twist drive damped by the friction.
	const float HingeFric = FCString::Atof(*Def->Keys.FindRef(TEXT("hingefriction")));
	if (HingeFric > 0.0f)
	{
		Constraint->SetAngularVelocityDriveTwistAndSwing(true, false);
		Constraint->SetAngularDriveParams(0.0f, HingeFric, 0.0f);
	}
}

void FElysiumPhysHinge::ReinitConstraint()
{
	if (!Constraint)
	{
		return;
	}
	UPrimitiveComponent* Body1 = ResolveBody(TEXT("attach1"));
	UPrimitiveComponent* Body2 = ResolveBody(TEXT("attach2"));
	ConfigureAsHinge();
	Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
}
