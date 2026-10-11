#include "Substrate/ElysiumPhysProp.h"

#include "ElysiumCollisionChannels.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"
#include "Map/ElysiumRetailMaskRecipe.h"   // PropBodyMaskBits -- which retail filters meet an entity prop
#include "Substrate/ElysiumProp.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "ElysiumModelPhysicsInputs.h"

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

void FElysiumPhysProp::Spawn()
{
	BuildBody();
}

void FElysiumPhysProp::VPhysicsDestroyObject()
{
    RetireModelBody();
    bSimulating = false;
}

void FElysiumPhysProp::RetireModelBody()
{
	// 10158520/10045110: the adapter removes its saved recipe before native deletion;
	// generic 100b5040's shadow membership remains a separate unfinished contract.
	bRestoredNativePhysics = false;
	ElysiumModelPhysics::Destroy(Visual);
	VPhysicsSetObject(nullptr);
}

bool FElysiumPhysProp::CreateModelBody(FString& OutError)
{
	RetireModelBody();
	if (!ElysiumModelPhysics::Create(Visual, World->PhysicsSurfaces(),
		MassBias, MassOverride, (SpawnFlags & 1) != 0, OutError)) return false;
	// 0x100a5ea0 / 0x100a6490: publish only the successfully created actual body.
	VPhysicsSetObject(ElysiumModelPhysics::ActorIdentity(Visual));
	return true;
}

bool FElysiumPhysProp::RestoreSavedPhysics(const FElysiumSavedPhysicsBody& State, FString& OutError)
{
	RetireModelBody(); // the port's spawned baseline is retired before Physics reconstruction.
	if (!Visual || IsInert() || bBroken || State.FieldName != TEXT("m_pPhysicsObject"))
	{ OutError = TEXT("saved physics prop has no live admitted field/component"); return false; }
	if (!ElysiumModelPhysics::Restore(Visual, World->PhysicsSurfaces(), State, OutError))
	{ bSimulating = false; return false; }
	VPhysicsSetObject(ElysiumModelPhysics::ActorIdentity(Visual));
	bSimulating = true;
	bRestoredNativePhysics = true;
	return true;
}

void FElysiumPhysProp::Kill()
{
    FElysiumEntity::Kill();
    VPhysicsDestroyObject();
}

UPrimitiveComponent* FElysiumPhysProp::GetAttachBody() const
{
	return Visual; // scene attachment anchor; native physics identity is queried separately.
}

void FElysiumPhysProp::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateBody();
}

void FElysiumPhysProp::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Visual && !(bRestoredNativePhysics && World && World->IsApplyingSnapshot()))
	{
		Visual->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)), /*bSweep=*/false);
	}
}

void FElysiumPhysProp::OnRuntimeModelChanged()
{
	VPhysicsDestroyObject();
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
			Embodiment->ApplyAnimatedPropSkin(PosedVisual, Model, Skin);
		}
		else if (Visual)
		{
			Embodiment->ApplyPropSkin(Visual, Model, Skin);
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
	Out.Emplace(TEXT("Body"), ElysiumModelPhysics::Identity(Visual) ? TEXT("Chaos body") : TEXT("(none)"));
	Out.Emplace(TEXT("Active"), ElysiumModelPhysics::IsActive(Visual) ? TEXT("yes") : TEXT("no"));
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
	Request.Location = Origin;
	Request.Rotation = ElysiumPlayerView::ToUnreal(Angles).Quaternion();
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	Request.Skin = Skin;
	Request.Physics = EElysiumPlacedModelPhysics::SimulatedProxy;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody PlacedBody = Embodiment->BuildPlacedModelBody(Request);
		Visual = PlacedBody.PhysicsProxy;
		PosedVisual = Cast<USkeletalMeshComponent>(PlacedBody.Visual);
	}
	else
	{
		Visual = Embodiment->BuildPhysPropVisual(Model, Request.Location, Request.Rotation,
			Embodiment->BodyScaleFor(*Def));
	}
	if (!Visual)
	{
		return;
	}
	// 0019/6: an entity prop's retail filter bits, from its two authored keyfields, on whichever
	// builder stood the body (the catalogue's physics proxy wears none of its own). The mask
	// filter survives every later profile change.
	Visual->SetMaskFilterOnBodyInstance(PropMaskBits());
	World->RegisterPropBody(Visual);
	if (PosedVisual)
	{
		World->RegisterNpcBody(PosedVisual);
	}
	if (Skin != 0)
	{
		ApplySkin();
	}

	// 1002e3a0/10191510: source body presence is distinct from a cooked visual fallback box.
    FString BodyError;
    if (!Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0)
    {
        SetSolid(6);
        SetSolidFlags(0); // 100a5ea0 writes these before calling the model creator.
    }
    bSimulating = !Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0 &&
        CreateModelBody(BodyError);
    if (!bSimulating)
    {
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        const auto* Inputs = UElysiumModelPhysicsInputs::Find(Visual->GetStaticMesh());
        if (Inputs && !Inputs->bSourceBody)
        {
            SetSolid(0);
            SetMoveType(0, 0); // 10191510's source-absent outcome, not a host bake failure.
            UE_LOG(LogElysiumProp, Log, TEXT("%s source model has no VPhysics body"), *DebugString());
        }
        else if (!Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0)
        {
            UE_LOG(LogElysiumProp, Warning, TEXT("%s Chaos adapter unavailable: %s"), *DebugString(), *BodyError);
        }
    }
    else
    {
        SetMoveType(7, 0); // VPhysicsInitNormal 100a5ea0.
    }

	if (IsInert() || bBroken)
	{
		GateBody();   // born hidden (start_hidden / a Spawn()-time Kill)
	}
}

uint8 FElysiumPhysProp::PropMaskBits() const
{
	// `blocks_traces` (`m_bBlocksTraces +0xfd`): admitted without MONSTER (`101d30f2`);
	// `npc_transparent` (`m_bNPCTransparent +0xfc`): passed by `FVisible` (`0x10107630`). Authored
	// keyfields, read like `override_mass`; spawn-time only.
	const auto Flag = [this](const TCHAR* Key)
	{
		return Def != nullptr && FCString::Atoi(*Def->Keys.FindRef(Key)) != 0;
	};
	return ElysiumRetailMask::PropBodyMaskBits(Flag(TEXT("blocks_traces")), Flag(TEXT("npc_transparent")));
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
		RetireModelBody();
	}
	else if (bSimulating)
	{
		// Final entity reconnect follows the Physics block. Keep its reconstructed solved state.
		if (bRestoredNativePhysics && World && World->IsApplyingSnapshot()) return;
		RetireModelBody(); // the profile/collision changes below may recreate a native actor.
		Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
		// 0018 story 6: the profile reset the body's responses. A moving prop is still an entity
		// prop to `StandardFilterRules 0x101d3080` (R2 section 2): met only under MONSTER
		// (`PropMaskBit`, unless `blocks_traces`), and then it blocks `FVisible`'s `0x2804091`
		// (unless `npc_transparent`), which `PhysicsActor` ignores -- so the sight Block is set
		// again on this body alone.
		Visual->SetMaskFilterOnBodyInstance(PropMaskBits());
		Visual->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
		FString BodyError;
		bSimulating = CreateModelBody(BodyError);
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
