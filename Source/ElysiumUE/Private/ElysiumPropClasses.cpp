// 8.3 — Dynamic props: the `prop_dynamic` (+ `prop_dynamic_ornament`) static-mesh leaf.
//
// Closes the "bodiless prop" gap (roadmap 9.3): a prop_dynamic record that parsed as an inert entity
// now stands its decoded static `.mdl` (out/<map>/props/<stem>.obj via AElysiumMapActor::BuildPropVisual,
// the shared prop decode 8.1 exports) at its placement, and gains per-instance addressability — the base
// ScriptHide/ScriptUnhide dormancy, the 9.3 SetOrigin/SetAngles/SetModel writers (body-follow), and the
// prop_dynamic inputs. `Break` hides the body and fires OnBreak. `Skin`/`SetAnimation` are logged stubs:
// the prop decode is LOD0 static geometry, skin 0 only — no alternate skin families or skeleton are
// exported — so faithfully they can only record the request (roadmap 8.3 / decisions.md).
//
// Deliberately out of scope: prop_physics Chaos bodies + constraints (8.4), the interactive
// prop_button/prop_sign/prop_switch/… `+use` family (4.10/8.8), and collision — a prop stands non-solid
// here (visual parity first; entity_visuals R2 "collision optional for visuals").

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"

#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumProp, Log, All);

// A/B toggle for the dynamic-prop bodies (mirrors elysium.NpcBodies). Read in the leaf's Spawn, so it
// takes effect on the next map load: 1 stands the meshes, 0 leaves the props bodiless records (their
// I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarPropBodies(
	TEXT("elysium.PropBodies"),
	1,
	TEXT("Stand dynamic-prop static-mesh bodies at their placements at map load (1, default) or skip them (0)."),
	ECVF_Default);

// A/B toggle for prop_physics simulation (8.4). 1 (default) simulates the body as a Chaos rigid
// body; 0 leaves it standing as a static, non-solid mesh (visual parity, like a prop_dynamic), so
// a map can be compared with and without physics. Read in the leaf's Spawn — takes effect next load.
static TAutoConsoleVariable<int32> CVarPhysicsProps(
	TEXT("elysium.PhysicsProps"),
	1,
	TEXT("Simulate prop_physics bodies as Chaos rigid bodies (1, default) or stand them static/non-solid (0)."),
	ECVF_Default);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddNpcField — file-unique name so all of them can land in one
	// unity blob. Only the int case this leaf needs.
	template <typename TClass, typename TMember>
	void AddPropField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		static_assert(std::is_same_v<TMember, int32>, "AddPropField: only int32 members are used here");
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumProp — the AI-free static-mesh leaf shared by prop_dynamic / prop_dynamic_ornament. It
// stands a decoded prop mesh at its placement and exposes the dynamic-prop inputs over it.
// ============================================================================================

class FElysiumProp final : public FElysiumEntity
{
public:
	// The standing static-mesh body, or null (elysium.PropBodies 0, a decode that failed, or a record
	// with no model_mesh annotation). Owned by the map actor; the world tears it down. This leaf only
	// gates its visibility / moves it.
	UStaticMeshComponent* Visual = nullptr;
	bool  bBroken = false;   // Break fired — the body is hidden and stays down
	int32 Skin = 0;          // `skin` keyfield / Skin input (recorded; no alternate skins exported)

	virtual void Spawn() override
	{
		// Keyfields (model/angles/skin) are already applied. Stand the body from the export annotation.
		BuildBody(/*bFromSetModel=*/false);
	}

	// Mirror the whole-entity dormancy switch onto the body (R6): a ScriptHidden/dead prop is undrawn.
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateVisual();
	}

	// SetOrigin/SetAngles: the body is Movable, so follow it. Authored placement is a full 3-axis quat
	// (the exporter's model_quat); a runtime re-face carries no pre-converted form, so it collapses to
	// yaw-only, negated by the Source->Unreal Y reflection — matching the NPC leaf (cosmetic in practice).
	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Visual)
		{
			Visual->SetRelativeLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)));
		}
	}

	// SetModel: tear the old body down and rebuild from the new model at the same placement.
	virtual void OnRuntimeModelChanged() override
	{
		if (Visual)
		{
			Visual->DestroyComponent();
			Visual = nullptr;
		}
		BuildBody(/*bFromSetModel=*/true);
	}

	// Break: hide the prop and fire OnBreak (Source spawns gibs here; 8.3 has no gib system, so the
	// body simply goes down). Idempotent.
	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (bBroken)
		{
			return;
		}
		bBroken = true;
		GateVisual();
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
	}

	// Skin: faithfully selects an alternate skin family. The prop decode is skin 0 only (no alternate
	// texture sets exported), so this records the request and logs (roadmap 8.3 deferral).
	void InputSkin(const FElysiumInputArgs& Args)
	{
		Skin = Args.Param.ToInt();
		UE_LOG(LogElysiumProp, Log, TEXT("%s Skin %d — no alternate skin families exported (8.3 stub)"),
			*DebugString(), Skin);
	}

	// SetAnimation: prop_dynamic can be skeletal in Source; the decode here is LOD0 static geometry (no
	// skeleton), so animation is a no-op stub (roadmap 8.3 deferral — skeletal props are a follow-up).
	void InputSetAnimation(const FElysiumInputArgs& Args)
	{
		UE_LOG(LogElysiumProp, Log, TEXT("%s SetAnimation '%s' — prop is static geometry (8.3 stub)"),
			*DebugString(), *Args.Param.ToString());
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("static mesh") : TEXT("(none)"));
		Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Skin"), FString::FromInt(Skin));
	}

private:
	// Stand (or restand) the body. At spawn the exporter's model_mesh annotation names the decoded OBJ
	// stem exactly (a plain basename may not reproduce the sanitised stem) and model_quat the placement
	// rotation; a runtime SetModel has neither, so it derives the stem from the model path's basename
	// (matching the NPC SetModel path) and keeps the current yaw.
	void BuildBody(bool bFromSetModel)
	{
		if (CVarPropBodies.GetValueOnGameThread() == 0 || !World || !Def)
		{
			return;   // gated off, no world, or bare test world (no map actor to build on)
		}
		AElysiumMapActor* Map = Cast<AElysiumMapActor>(World->GetOwnerActor());
		if (!Map)
		{
			return;
		}

		FString Stem;
		FVector Loc;
		FQuat   Rot;
		if (bFromSetModel)
		{
			if (Model.IsEmpty())
			{
				return;   // now modelless
			}
			Stem = FPaths::GetBaseFilename(Model).ToLower();
			Loc  = Origin;
			Rot  = FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
		}
		else
		{
			if (Def->ModelMesh.IsEmpty())
			{
				return;   // no decoded prop mesh (bodiless record / decode failed) — stay a data-only entity
			}
			Stem = Def->ModelMesh;
			Loc  = Def->Origin;
			Rot  = Def->ModelQuat;
		}

		Visual = Map->BuildPropVisual(Stem, Loc, Rot);
		if (Visual)
		{
			World->RegisterPropBody(Visual);
			if (IsInert() || bBroken)
			{
				GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill) or already broken
			}
		}
	}

	void GateVisual()
	{
		if (Visual)
		{
			Visual->SetVisibility(!IsInert() && !bBroken);
		}
	}
};

// ============================================================================================
// FElysiumPhysProp — the `prop_physics` leaf: a Chaos rigid body. Stands the same decoded mesh as
// a dynamic prop but cooked with convex collision (the 8.4 `.hulls` decomposition) and simulating.
// Faithful I/O surface (RE'd from CPhysicsProp/CBreakableProp datamaps, decisions.md 2026-07-24):
// `Wake` wakes the body; `Break` hides it + fires OnBreak (no gib system — the OnBreakLevel1..8 chain
// stays undriven); `Skin`/`SetSkin`/`FadeToSkin`/`SetSkinFadeTime` are skin stubs (skin 0 only
// exported, as 8.3). VtMB's prop has **no** EnableMotion/DisableMotion/Sleep — those don't exist here.
// ============================================================================================

class FElysiumPhysProp final : public FElysiumEntity
{
public:
	UStaticMeshComponent* Visual = nullptr;   // the simulating body, or null (gated off / decode failed)
	bool  bBroken = false;
	int32 Skin = 0;
	bool  bSimulating = false;                // elysium.PhysicsProps decided sim on at spawn

	virtual void Spawn() override
	{
		BuildBody();
	}

	// A constraint (phys_hinge) attaches to the simulating body, not the brush body.
	virtual UPrimitiveComponent* GetAttachBody() const override { return Visual; }

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateBody();
	}

	// SetOrigin/SetAngles from a script teleport the body (no sweep — a simulating body is moved
	// directly, then physics resumes from the new pose).
	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Visual)
		{
			Visual->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)), /*bSweep=*/false);
		}
	}

	virtual void OnRuntimeModelChanged() override
	{
		if (Visual)
		{
			Visual->DestroyComponent();
			Visual = nullptr;
		}
		BuildBody();
	}

	void InputWake(const FElysiumInputArgs&)
	{
		if (Visual && bSimulating)
		{
			Visual->WakeAllRigidBodies();
		}
	}

	// Break: drop the prop (hide + no collision + stop simulating) and fire OnBreak. Idempotent.
	// Source spawns gibs and fires the OnBreakLevel* chain here; no gib system exists, so the body
	// simply goes down (matches the 8.3 prop_dynamic Break).
	void InputBreak(const FElysiumInputArgs& Args)
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

	void InputSkin(const FElysiumInputArgs& Args)
	{
		Skin = Args.Param.ToInt();
		UE_LOG(LogElysiumProp, Log, TEXT("%s Skin %d — no alternate skin families exported (8.4 stub)"),
			*DebugString(), Skin);
	}

	void InputSkinStub(const FElysiumInputArgs& Args, const TCHAR* Which)
	{
		UE_LOG(LogElysiumProp, Log, TEXT("%s %s — skin-only decode, no alternate skins (8.4 stub)"),
			*DebugString(), Which);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("physics mesh") : TEXT("(none)"));
		Out.Emplace(TEXT("Simulating"), bSimulating && !bBroken && !IsInert() ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
	}

private:
	void BuildBody()
	{
		if (CVarPropBodies.GetValueOnGameThread() == 0 || !World || !Def || Def->ModelMesh.IsEmpty())
		{
			return;   // gated off, bare test world, or a record with no decoded prop mesh
		}
		AElysiumMapActor* Map = Cast<AElysiumMapActor>(World->GetOwnerActor());
		if (!Map)
		{
			return;
		}
		Visual = Map->BuildPhysPropVisual(Def->ModelMesh, Def->Origin, Def->ModelQuat);
		if (!Visual)
		{
			return;
		}
		World->RegisterPropBody(Visual);

		// Simulate (or stand static under the A/B toggle). override_mass > 0 overrides the density-
		// computed mass; -1 (the common case) keeps the computed mass.
		bSimulating = CVarPhysicsProps.GetValueOnGameThread() != 0;
		if (bSimulating)
		{
			const float OverrideMass = FCString::Atof(*Def->Keys.FindRef(TEXT("override_mass")));
			if (OverrideMass > 0.0f)
			{
				Visual->SetMassOverrideInKg(NAME_None, OverrideMass, true);
			}
			Visual->SetSimulatePhysics(true);
		}
		else
		{
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // static, non-solid parity
		}

		if (IsInert() || bBroken)
		{
			GateBody();   // born hidden (start_hidden / a Spawn()-time Kill)
		}
	}

	// Hidden/broken → undrawn, non-colliding, not simulating. Live → restore draw + (if enabled)
	// simulation. Mirrors the whole-entity dormancy switch onto the physics body.
	void GateBody()
	{
		if (!Visual)
		{
			return;
		}
		const bool bDown = IsInert() || bBroken;
		Visual->SetVisibility(!bDown);
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
};

// ============================================================================================
// FElysiumPhysHinge — the `phys_hinge` leaf: a Chaos hinge constraint (one free rotational DOF)
// between attach1's body and attach2's (or the world). Bodiless. RE'd from CPhysHinge/CPhysConstraint
// (decisions.md 2026-07-24): fields attach1/attach2/forcelimit/torquelimit/hingefriction/hingeaxis;
// inputs TurnOn/TurnOff/Break; output OnBreak. The axis is the exporter's pre-converted Def->HingeAxis.
// ============================================================================================

class FElysiumPhysHinge final : public FElysiumEntity
{
public:
	UPhysicsConstraintComponent* Constraint = nullptr;

	// Second-phase init (both attached bodies must already exist — see FElysiumEntity::PostSpawn).
	virtual void PostSpawn() override
	{
		if (!World || !Def)
		{
			return;
		}
		AElysiumMapActor* Map = Cast<AElysiumMapActor>(World->GetOwnerActor());
		USceneComponent* Root = Map ? Map->GetRootComponent() : nullptr;
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

		Constraint = NewObject<UPhysicsConstraintComponent>(Map);
		Constraint->SetupAttachment(Root);
		// The constraint's local +X is the twist axis; orient the frame so it lies along the hinge axis.
		// The map actor sits at world origin, so relative == world for these Unreal-space values.
		Constraint->SetRelativeLocationAndRotation(Def->Origin, FRotationMatrix::MakeFromX(Axis).ToQuat());
		Constraint->RegisterComponent();
		Map->AddInstanceComponent(Constraint);

		ConfigureAsHinge();
		Constraint->SetConstrainedComponents(Body1, NAME_None, Body2, NAME_None);
		World->RegisterConstraintBody(Constraint);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s hinge: %s <-> %s"), *DebugString(),
			Body1 ? TEXT("attach1") : TEXT("world"), Body2 ? TEXT("attach2") : TEXT("world"));
	}

	// TurnOff/TurnOn enable-disable the constraint; Break is permanent + fires OnBreak.
	void InputTurnOff(const FElysiumInputArgs&) { if (Constraint) { Constraint->BreakConstraint(); } }
	void InputTurnOn(const FElysiumInputArgs&)  { ReinitConstraint(); }

	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (Constraint)
		{
			Constraint->BreakConstraint();
		}
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("attach1"), Def ? Def->Keys.FindRef(TEXT("attach1")) : FString());
		Out.Emplace(TEXT("attach2"), Def ? Def->Keys.FindRef(TEXT("attach2")) : FString());
		Out.Emplace(TEXT("Axis"), Def ? Def->HingeAxis.ToString() : FString());
		Out.Emplace(TEXT("Constraint"), Constraint ? TEXT("live") : TEXT("(none)"));
	}

private:
	UPrimitiveComponent* ResolveBody(const TCHAR* Key)
	{
		const FString Name = Def->Keys.FindRef(Key);
		if (Name.IsEmpty())
		{
			return nullptr;   // empty attach → the world frame
		}
		FElysiumEntity* Found = World->FindByName(Name);
		return Found ? Found->GetAttachBody() : nullptr;
	}

	void ConfigureAsHinge()
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

	// Re-wire a TurnOff'd (broken) constraint from the stored attach targets.
	void ReinitConstraint()
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
};

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeProp() { return MakeUnique<FElysiumProp>(); }
static TUniquePtr<FElysiumEntity> MakePhysProp() { return MakeUnique<FElysiumPhysProp>(); }
static TUniquePtr<FElysiumEntity> MakePhysHinge() { return MakeUnique<FElysiumPhysHinge>(); }

static void BuildPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSetAnimation(Args); });

	// `skin` keyfield / script `.skin`: registered read/write so the initial skin applies and a script
	// read/write resolves to a real field instead of the instance __dict__ (closes the 9.3 `.skin` note).
	AddPropField(D, TEXT("skin"), &FElysiumProp::Skin);
}

// prop_physics (8.4): the RE'd CPhysicsProp/CBreakableProp input surface. Wake + Break are real;
// the skin inputs are stubs (skin 0 only exported); no EnableMotion/DisableMotion/Sleep exist in VtMB.
static void BuildPhysPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Wake"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputWake(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("FadeToSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkinStub(Args, TEXT("FadeToSkin")); });
	D.Input(TEXT("SetSkinFadeTime"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkinStub(Args, TEXT("SetSkinFadeTime")); });

	AddPropField(D, TEXT("skin"), &FElysiumPhysProp::Skin);
}

// phys_hinge (8.4): the RE'd CPhysHinge/CPhysConstraint input surface.
static void BuildPhysHingeClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOn(Args); });
	D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOff(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputBreak(Args); });
}

// One shared leaf per classname (class-for-class registration, so the registry's exact case-folded
// Find resolves each). The +use prop_* family (4.10/8.8) is not here.
struct FElysiumPropRegistrar
{
	FElysiumPropRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		static const TCHAR* const PropClasses[] = {
			TEXT("prop_dynamic"), TEXT("prop_dynamic_ornament"),
		};
		for (const TCHAR* Name : PropClasses)
		{
			BuildPropClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeProp));
		}
		BuildPhysPropClass(Reg.Register(FName(TEXT("prop_physics")), ElysiumBaseClassName(), &MakePhysProp));
		BuildPhysHingeClass(Reg.Register(FName(TEXT("phys_hinge")), ElysiumBaseClassName(), &MakePhysHinge));
	}
};

static FElysiumPropRegistrar GElysiumPropRegistrar;
