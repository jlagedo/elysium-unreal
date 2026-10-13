#include "Substrate/ElysiumPhysProp.h"

#include "ElysiumCollisionChannels.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"
#include "Map/ElysiumRetailMaskRecipe.h"   // PropBodyMaskBits -- which retail filters meet an entity prop
#include "Substrate/ElysiumProp.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Substrate/ElysiumCollisionPairs.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicsPropComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

// Console gate for prop_physics simulation. 1 (default) simulates the body as a Chaos rigid body;
bool FElysiumPhysProp::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	//101914a0: actual cached combat/player downcasts, then the base animation pair service.
	if (Other && Other->AsCombatCharacter() && (SpawnFlags & 4)) return true;
	if (Other && Other->AsPlayer() && (EntityFlags2Word & 1)) return true;
	return World && World->CollisionPairs().ContainsEntities(Handle,Other ? Other->Handle : FElysiumEntityHandle::Invalid());
}

// 0 leaves it standing as a static, non-solid mesh (visual parity, like a prop_dynamic). Read in
// the leaf's Spawn — takes effect next load.
static TAutoConsoleVariable<int32> CVarPhysicsProps(
	TEXT("elysium.PhysicsProps"),
	1,
	TEXT("Simulate prop_physics bodies as Chaos rigid bodies (1, default) or stand them static/non-solid (0)."),
	ECVF_Default);

void FElysiumPhysProp::Spawn()
{
	//10191450 ->1018f340 ->1018df70 virtual105: the selected model index is
	//published before virtual223 enters10191510/100a5ea0's GetModelIndex read.
	if (World && !Model.IsEmpty()) SetModelIndex(World->ModelTableIndex(Model));
	BuildBody();
}

void FElysiumPhysProp::VPhysicsDestroyObject()
{
    //225 owns the current field only; SetObject clear/overwrite did not transfer producer ownership.
    FElysiumEntity::VPhysicsDestroyObject();
}

void FElysiumPhysProp::RetireModelBody()
{
	// 10158520/10045110: the adapter removes its saved recipe before native deletion;
	// generic 100b5040 also retires all registered references to the supplied actor.
	bRestoredNativePhysics = false;
	ElysiumModelPhysics::Destroy(Visual);
	VPhysicsSetObject(nullptr);
}

bool FElysiumPhysProp::CreateModelBody(FString& OutError)
{
	void* Object = VPhysicsInitNormal(6, 0, (SpawnFlags & 1) != 0, Visual, nullptr, &OutError);
	if (!Object) return false;
	//10191510: exact authored equality; this is independent of GetDamageType slot189.
	if (DamageType == 1 && !ElysiumModelPhysics::AddGameFlags(Object, 1))
	{ OutError = TEXT("created physics prop has no current game flag association"); RetireModelBody(); return false; }
	//10191510: caller writes flags, positive bias/clamp, then positive override after normal init.
	if (MassBias > 0.f && !ElysiumModelPhysics::SetMass(Object,
		FMath::Clamp(Visual->GetMass() * MassBias, .5f, 1000000.f)))
	{ OutError = TEXT("physics prop massbias has no supported current native mass"); RetireModelBody(); return false; }
	if (MassOverride > 0.f && !ElysiumModelPhysics::SetMass(VPhysicsGetObject(), MassOverride))
	{ OutError = TEXT("physics prop override_mass has no supported current native mass"); RetireModelBody(); return false; }
	return true;
}

bool FElysiumPhysProp::RestoreSavedPhysics(const FElysiumSavedPhysicsBody& State, FString& OutError)
{
	RetireModelBody(); // the port's spawned baseline is retired before Physics reconstruction.
	if (!Visual || IsDead() || bBroken || State.FieldName != TEXT("m_pPhysicsObject"))
	{ OutError = TEXT("saved physics prop has no live admitted field/component"); return false; }
	if (!ElysiumModelPhysics::Restore(Visual, World->PhysicsSurfaces(), State, OutError))
	{ bSimulating = false; return false; }
	ElysiumModelPhysics::SetGameData(ElysiumModelPhysics::ActorIdentity(Visual), *this);
	VPhysicsSetObject(ElysiumModelPhysics::ActorIdentity(Visual));
	bSimulating = true;
	bRestoredNativePhysics = true;
	return true;
}

void FElysiumPhysProp::Kill()
{
    FElysiumEntity::Kill();
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
		ElysiumModelPhysics::Wake(Visual);
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
	// Hidden shape eligibility does not stop native particle dynamics or imply sleep.
	Out.Emplace(TEXT("Simulating"), ElysiumModelPhysics::Identity(Visual) ? TEXT("yes") : TEXT("no"));
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
    bSimulating = !Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0 &&
        CreateModelBody(BodyError);
    if (!bSimulating)
    {
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (!Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0)
		{
			SetSolid(0);
			SetMoveType(0, 0); //10191510: every failed normal creation, not just absent .phy.
			//The caller's final Relink100b3b00 is profiling only.
		}
        const auto* Inputs = UElysiumModelPhysicsInputs::Find(Visual->GetStaticMesh());
        if (Inputs && !Inputs->bSourceBody)
        {
            UE_LOG(LogElysiumProp, Log, TEXT("%s source model has no VPhysics body"), *DebugString());
        }
        else if (!Def->bSky && CVarPhysicsProps.GetValueOnGameThread() != 0)
        {
            UE_LOG(LogElysiumProp, Warning, TEXT("%s Chaos adapter unavailable: %s"), *DebugString(), *BodyError);
        }
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
	// Bare field clear/overwrite leaves a detached former producer alive. Art gating does not own225.
	if (void* NativeActor = ElysiumModelPhysics::ActorIdentity(Visual); NativeActor && NativeActor != VPhysicsGetObject()) return;
	if (bBroken)
	{
		const bool bResumeSimulation = bSimulating;
		RetireModelBody();
		bSimulating = bResumeSimulation; // preserve existing break retirement; not ScriptHide.
	}
	else if (ElysiumModelPhysics::Identity(Visual))
	{
		// Final entity reconnect follows the Physics block. Keep its reconstructed solved state.
		if (bRestoredNativePhysics && World && World->IsApplyingSnapshot()) return;
		// 100a8710/100a8990 notify current object#25 through physical words. Gate
		// query/contact eligibility without225, creation, velocity, gravity or sleep writes.
		ElysiumModelPhysics::GateStaticModelCollision(Visual, !bDown);
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
	NativeEndpoint1 = ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Body1));
	NativeEndpoint2 = ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Body2));
	World->RegisterConstraintBody(Constraint);
	bSavedConstraintPresent = true;
	UE_LOG(LogElysiumProp, Verbose, TEXT("%s hinge: %s <-> %s"), *DebugString(),
		Body1 ? TEXT("attach1") : TEXT("world"), Body2 ? TEXT("attach2") : TEXT("world"));
}

void FElysiumPhysHinge::InputTurnOff(const FElysiumInputArgs&)
{
	if (Constraint)
	{
		Constraint->BreakConstraint();
	}
	bSavedConstraintEnabled = false;
	//1002af90 preserves the constraint, clears endpoint GameFlags2, then removes the pair.
	for (const auto End : { NativeEndpoint1, NativeEndpoint2 })
		if (ElysiumModelPhysics::IsCurrentOutput(End))
		{
			uint16 NativeFlags = 0;
			if (ElysiumModelPhysics::GetGameFlags(End, NativeFlags))
				ElysiumModelPhysics::SetGameFlags(End, NativeFlags & ~2u);
		}
	PublishPair(false);
}

void FElysiumPhysHinge::Precache()
{
	//1002b170: the inherited slot precaches only an authored nonnull sound name.
	if (!BreakSound.IsEmpty())
		if (auto* Audio = World ? World->Audio() : nullptr)
			Audio->Prefetch(FElysiumAudioSource::Path(BreakSound));
}

void FElysiumPhysHinge::InputBreak(const FElysiumInputArgs& Args)
{
	InputTurnOff(Args); //1002a960 also removes the spawnflag1 relation.
	bConstraintBroken = true;
	//1002a960 raw EmitSound: channel6, volume1, attenuation1.25, pitch100.
	if (!BreakSound.IsEmpty())
		if (auto* Audio = World ? World->Audio() : nullptr)
		{
			FElysiumBodySound Sound;
			Sound.Rel = BreakSound; Sound.Channel = EElysiumSoundChannel::Static;
			Sound.Volume = 1.f; Sound.SoundLevelDb = 66; Sound.Pitch = 1.f;
			Audio->PlayBodySound(Handle, Sound);
		}
	static const FName OnBreak(TEXT("OnBreak"));
	FireOutput(OnBreak, Handle); //retail self/self, independent of the input activator
	QueueRemove(); //1002a960 ->101cd940, distinct from TurnOff
}

void FElysiumPhysHinge::PublishPair(bool bAdd)
{
	if (!World || !Constraint || bConstraintBroken || (SpawnFlags & 1) == 0) return;
	if (!ElysiumModelPhysics::IsCurrentOutput(NativeEndpoint1)
		|| !ElysiumModelPhysics::IsCurrentOutput(NativeEndpoint2)) return;
	if (bAdd) World->CollisionPairs().AddTranslated(NativeEndpoint1.Actor, NativeEndpoint2.Actor);
	else World->CollisionPairs().RemoveTranslated(NativeEndpoint1.Actor, NativeEndpoint2.Actor);
}

void FElysiumPhysHinge::Activate()
{
	FElysiumEntity::Activate();
	//1002b420 ->1002b4c0 existing-native arm: publish, then owner association. No enable write.
	PublishPair(true);
}

void FElysiumPhysHinge::Serialize(FElysiumSaveArchive& Ar)
{
	if (Ar.IsSaving())
	{
		bSavedConstraintPresent = Constraint != nullptr;
		bSavedConstraintEnabled = Constraint && !Constraint->IsBroken();
		if (Constraint) SavedConstraintFrame = Constraint->GetComponentTransform();
		SavedEndpointReference(NativeEndpoint1, EndpointFieldOwner1, EndpointField1);
		SavedEndpointReference(NativeEndpoint2, EndpointFieldOwner2, EndpointField2);
	}
	Ar << Endpoint1 << Endpoint2 << bSavedConstraintPresent << bSavedConstraintEnabled << bConstraintBroken;
	Ar << EndpointFieldOwner1 << EndpointField1 << EndpointFieldOwner2 << EndpointField2;
	Ar << SavedConstraintFrame;
	if (Ar.IsLoading() && World)
	{ Endpoint1 = World->RestoreHandle(Endpoint1); Endpoint2 = World->RestoreHandle(Endpoint2); }
}

void FElysiumPhysHinge::OnPostRestore(FElysiumEntityWorld&)
{
	// Native owner field reconstructed after all Physics fields, before forward Activate publishes.
	if (!bSavedConstraintPresent || bConstraintBroken)
	{
		if (Constraint) { Constraint->DestroyComponent(); Constraint = nullptr; }
		return;
	}
	NativeEndpoint1 = ResolveEndpointReference(EndpointFieldOwner1, EndpointField1);
	NativeEndpoint2 = ResolveEndpointReference(EndpointFieldOwner2, EndpointField2);
	if (Constraint) Constraint->SetWorldTransform(SavedConstraintFrame);
	ReinitConstraint();
	if (Constraint && !bSavedConstraintEnabled) Constraint->BreakConstraint();
}

void FElysiumPhysHinge::SavedEndpointReference(const ElysiumModelPhysics::FNativeOutput& Output, int32& FieldOwner, FString& Field) const
{
	FieldOwner = INDEX_NONE; Field.Empty();
	if (!World || !ElysiumModelPhysics::IsCurrentOutput(Output)) return;
	auto* ActualOwner = ElysiumModelPhysics::GameDataFor(Output, *World);
	if (auto* Owner = ActualOwner ? World->Resolve(ActualOwner->Handle) : nullptr;
		Owner && Owner->ShouldSavePhysics() && Owner->VPhysicsGetObject() == Output.Actor)
	{ FieldOwner = Owner->Handle.Index; Field = TEXT("m_pPhysicsObject"); return; }
	// Registered current fields are reconstructed by Physics first; preserve their actual identity.
	for (const auto& Entity : World->Entities())
		if (Entity && !Entity->IsDead() && Entity->ShouldSavePhysics() && Entity->VPhysicsGetObject() == Output.Actor)
		{ FieldOwner = Entity->Handle.Index; Field = TEXT("m_pPhysicsObject"); return; }
	// A former producer can be owned only by a constraint CUSTOM record. Canonicalize shared refs.
	for (const auto& Entity : World->Entities())
	{
		if (!Entity || Entity->IsDead() || !Entity->Def || Entity->Def->Classname != TEXT("phys_hinge")) continue;
		const auto* Hinge = static_cast<const FElysiumPhysHinge*>(Entity.Get());
		if (Hinge->NativeEndpoint1.Actor == Output.Actor && Hinge->NativeEndpoint1.Incarnation == Output.Incarnation)
		{ FieldOwner = Entity->Handle.Index; Field = TEXT("m_pConstraint.endpoint1"); return; }
		if (Hinge->NativeEndpoint2.Actor == Output.Actor && Hinge->NativeEndpoint2.Incarnation == Output.Incarnation)
		{ FieldOwner = Entity->Handle.Index; Field = TEXT("m_pConstraint.endpoint2"); return; }
	}
}

bool FElysiumPhysHinge::CaptureConstraintPhysics(TArray<FElysiumSavedPhysicsBody>& Rows) const
{
	for (const auto Output : { NativeEndpoint1, NativeEndpoint2 })
	{
		int32 FieldOwner; FString Field; SavedEndpointReference(Output, FieldOwner, Field);
		if (FieldOwner != Handle.Index || !Field.StartsWith(TEXT("m_pConstraint."))) continue;
		if (Rows.ContainsByPredicate([&](const auto& Row) { return Row.EntityIndex == FieldOwner && Row.FieldName == Field; })) continue;
		FElysiumSavedPhysicsBody State;
		if (!ElysiumModelPhysics::Capture(Output.Actor, State)) return false;
		State.EntityIndex = FieldOwner; State.FieldName = Field;
		const auto* Owner = ElysiumModelPhysics::GameDataFor(Output, *World);
		State.GameDataEntityIndex = Owner ? Owner->Handle.Index : INDEX_NONE;
		Rows.Add(MoveTemp(State));
	}
	return true;
}

ElysiumModelPhysics::FNativeOutput FElysiumPhysHinge::ResolveEndpointReference(int32 FieldOwner, const FString& Field) const
{
	auto* Entity = World ? World->Resolve(FElysiumEntityHandle(FieldOwner, World->GetEpoch())) : nullptr;
	if (!Entity) return {};
	if (Field == TEXT("m_pPhysicsObject")) return ElysiumModelPhysics::CaptureIdentity(Entity->VPhysicsGetObject());
	if (!Entity->Def || Entity->Def->Classname != TEXT("phys_hinge")) return {};
	const auto* Hinge = static_cast<const FElysiumPhysHinge*>(Entity);
	if (Field == TEXT("m_pConstraint.endpoint1")) return Hinge->AdditionalEndpoint1;
	if (Field == TEXT("m_pConstraint.endpoint2")) return Hinge->AdditionalEndpoint2;
	return {};
}

bool FElysiumPhysHinge::ValidateRestoredConstraintEndpoints(FString& Error) const
{
	if (!bSavedConstraintPresent || bConstraintBroken) return true;
	for (const auto& Reference : { TPair<int32,FString>(EndpointFieldOwner1,EndpointField1), {EndpointFieldOwner2,EndpointField2} })
	{
		if (Reference.Key == INDEX_NONE && Reference.Value.IsEmpty()) continue; //world frame
		if (Reference.Key < 0 || !ElysiumModelPhysics::IsCurrentOutput(ResolveEndpointReference(Reference.Key, Reference.Value)))
		{ Error = FString::Printf(TEXT("constraint endpoint %d.%s has no reconstructed native output"),Reference.Key,*Reference.Value); return false; }
	}
	return true;
}

bool FElysiumPhysHinge::RestoreSavedPhysics(const FElysiumSavedPhysicsBody& State, FString& Error)
{
	if (State.FieldName == TEXT("m_pPhysicsObject")) return FElysiumEntity::RestoreSavedPhysics(State, Error);
	if (!World || !World->GetOwnerActor() || !State.bHasObject || State.bRebuildFromEntity
		|| (State.FieldName != TEXT("m_pConstraint.endpoint1") && State.FieldName != TEXT("m_pConstraint.endpoint2")))
	{ Error = TEXT("invalid registered constraint native endpoint"); return false; }
	auto* NativeOwner = World->GetOwnerActor();
	auto* Component = NewObject<UElysiumPhysicsPropComponent>(NativeOwner);
	Component->RegisterComponent(); NativeOwner->AddInstanceComponent(Component); Component->SetVisibility(false);
	if (!ElysiumModelPhysics::Restore(Component, World->PhysicsSurfaces(), State, Error))
	{ Component->DestroyComponent(); return false; }
	const auto Output = ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Component));
	auto* Owner = World->Resolve(FElysiumEntityHandle(State.GameDataEntityIndex, World->GetEpoch()));
	if (Owner)
		ElysiumModelPhysics::SetGameData(Output.Actor, *Owner);
	else if (State.GameDataEntityIndex != INDEX_NONE)
	{ ElysiumModelPhysics::Destroy(Component); Component->DestroyComponent(); Error = TEXT("restored constraint native endpoint has no live GameData owner"); return false; }
	World->AdoptRestoredConstraintEndpoint(Component, Owner ? Owner->Handle : FElysiumEntityHandle::Invalid());
	if (State.FieldName == TEXT("m_pConstraint.endpoint1")) AdditionalEndpoint1 = Output;
	else AdditionalEndpoint2 = Output;
	return true;
}

FElysiumPhysHinge::~FElysiumPhysHinge()
{
	if (Constraint) Constraint->DestroyComponent();
}

void FElysiumPhysHinge::ReleaseForRemoval()
{
	if (Constraint) { Constraint->DestroyComponent(); Constraint = nullptr; }
	FElysiumEntity::ReleaseForRemoval();
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
	if (FCString::Strcmp(Key, TEXT("attach1")) == 0) Endpoint1 = Found ? Found->Handle : FElysiumEntityHandle::Invalid();
	else Endpoint2 = Found ? Found->Handle : FElysiumEntityHandle::Invalid();
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
		auto* Outer = World ? World->GetOwnerActor() : nullptr;
		if (!Outer || !Def || !Outer->GetRootComponent()) return;
		Constraint = NewObject<UPhysicsConstraintComponent>(Outer);
		Constraint->SetupAttachment(Outer->GetRootComponent());
		Constraint->SetWorldTransform(SavedConstraintFrame);
		Constraint->RegisterComponent(); Outer->AddInstanceComponent(Constraint);
		World->RegisterConstraintBody(Constraint);
	}
	UPrimitiveComponent* Body1 = ElysiumModelPhysics::IsCurrentOutput(NativeEndpoint1) ? ElysiumModelPhysics::ComponentFor(NativeEndpoint1.Actor) : nullptr;
	UPrimitiveComponent* Body2 = ElysiumModelPhysics::IsCurrentOutput(NativeEndpoint2) ? ElysiumModelPhysics::ComponentFor(NativeEndpoint2.Actor) : nullptr;
	ConfigureAsHinge();
	Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
	NativeEndpoint1 = ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Body1));
	NativeEndpoint2 = ElysiumModelPhysics::CaptureIdentity(ElysiumModelPhysics::ActorIdentity(Body2));
}
