#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumModelPhysics.h"

class UPhysicsConstraintComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

// The `prop_physics` leaf: a Chaos rigid body. Stands the same decoded mesh as a dynamic prop but
// cooked with convex collision (the `.hulls` decomposition) and simulating. I/O surface (RE'd from
// CPhysicsProp/CBreakableProp datamaps): `Wake` wakes the body; `Break` hides it + fires OnBreak
// (no gib system — the OnBreakLevel1..8 chain stays undriven); `Skin`/`SetSkin`/`FadeToSkin`/
// `SetSkinFadeTime` are skin stubs (skin 0 only exported). VtMB's prop has **no**
// EnableMotion/DisableMotion/Sleep.

class FElysiumPhysProp final : public FElysiumEntity
{
public:
	UStaticMeshComponent* Visual = nullptr;   // the simulating body, or null (gated off / decode failed)
	USkeletalMeshComponent* PosedVisual = nullptr; // collision-free authored resting pose
	bool  bBroken = false;
	bool bNpcKickable = false; // CPhysicsProp +0x788, npc_kickable; TaskFail consumes this authored permission.
	float MassBias = 0.f; // +0x77c, SAVE|KEY massbias; positive multiplier precedes override.
	float MassOverride = 0.f; // +0x780, SAVE|KEY override_mass; nonpositive leaves model mass.
	int32 DamageType = 0; //+0x784, SAVE|KEY Damagetype;10191510 tests exactly1 on fresh native creation
	int32 Skin = 0;
	float SkinFadeTime = 0.0f;                // m_flSkinCrossfadeTime — stored, unread (skins snap)
	bool  bSimulating = false;                // elysium.PhysicsProps decided sim on at spawn

	virtual void Spawn() override;
	bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	ElysiumCollisionRules::ENativeVeto NativeCollisionVetoPolicy() const override { return ElysiumCollisionRules::ENativeVeto::PhysicsProp; }
	virtual void Kill() override;
	virtual void VPhysicsDestroyObject() override;
	void OnPhysicsActorRetired() override { bSimulating = false; bRestoredNativePhysics = false; }
	bool RestoreSavedPhysics(const FElysiumSavedPhysicsBody& State, FString& OutError) override;

	// A constraint (phys_hinge) attaches to the simulating body, not the brush body.
	virtual UPrimitiveComponent* GetAttachBody() const override;

	virtual void OnDormancyChanged() override;

	// SetOrigin/SetAngles from a script teleport the body (no sweep — a simulating body is moved
	// directly, then physics resumes from the new pose).
	virtual void OnRuntimeTransformChanged() override;

	virtual void OnRuntimeModelChanged() override;

	void InputWake(const FElysiumInputArgs&);

	// Drop the prop (hide + no collision + stop simulating) and fire OnBreak. Idempotent. Source
	// spawns gibs and fires the OnBreakLevel* chain here; no gib system exists, so the body simply
	// goes down (matches the prop_dynamic Break).
	void InputBreak(const FElysiumInputArgs& Args);

	// Snap to an alternate skin family. VtMB's `skin` input is a direct field write
	// (`FElysiumProp::InputSkin`).
	void InputSkin(const FElysiumInputArgs& Args);

	void SetSkin(int32 Family);
	void ApplySkin();

	// CBaseAnimating::FadeToSkin (vampire.dll @1008d6d0) sets m_nSkinCrossfade to the old skin,
	// m_nSkin to the new one, and leaves the blend to the client — all three fields are networked
	// SendProps and the server writes no start time. This snaps instead: no exported map fires this
	// input (18 skin wires across the 16 exported maps are all `Skin`, which snaps in VtMB too), so
	// the crossfade is engine code no map data reaches.
	void InputFadeToSkin(const FElysiumInputArgs& Args);

	// CBaseAnimating::SetSkinFadeTime (@1008d5f0) clamps to a floor and stores
	// m_flSkinCrossfadeTime, which only the crossfade reads. Stored for fidelity of the field, but
	// nothing consumes it while skin changes snap.
	void InputSetSkinFadeTime(const FElysiumInputArgs& Args);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	bool bRestoredNativePhysics = false;
	void BuildBody();
	void RetireModelBody();
	bool CreateModelBody(FString& OutError);

	// ScriptHide gates art/query/contact eligibility while retaining native dynamics.
	// Broken retains its separate existing native-retirement branch.
	void GateBody();
	// 0019/6: the retail filter bits this entity prop's body wears (`ElysiumRetailMask::PropBodyMaskBits`).
	uint8 PropMaskBits() const;
};

// The `phys_hinge` leaf: a Chaos hinge constraint (one free rotational DOF) between attach1's body
// and attach2's (or the world). Bodiless. RE'd from CPhysHinge/CPhysConstraint. Fields
// attach1/attach2/forcelimit/torquelimit/hingefriction/hingeaxis; inputs TurnOn/TurnOff/Break;
// output OnBreak. The axis is the exporter's pre-converted Def->HingeAxis.

class FElysiumPhysHinge final : public FElysiumEntity
{
public:
	UPhysicsConstraintComponent* Constraint = nullptr;
	// CPhysConstraint +45c, FIELD_SOUNDNAME SAVE|KEY (breaksound).
	FString BreakSound;
	virtual void Precache() override;

	// Second-phase init (both attached bodies must already exist — see FElysiumEntity::PostSpawn).
	virtual void PostSpawn() override;
	virtual void Activate() override;
	virtual void OnPostRestore(FElysiumEntityWorld&) override;
	virtual void Serialize(FElysiumSaveArchive&) override;
	virtual bool RestoreSavedPhysics(const FElysiumSavedPhysicsBody&, FString&) override;
	virtual ~FElysiumPhysHinge() override;
	virtual void ReleaseForRemoval() override;
	bool CaptureConstraintPhysics(TArray<FElysiumSavedPhysicsBody>& Rows) const;
	bool ValidateRestoredConstraintEndpoints(FString& Error) const;
	virtual bool ReplayActivationAfterRestore() const override { return true; }

	//1002af70 only republishes the existing native owner; TurnOff retains it disabled.
	void InputTurnOff(const FElysiumInputArgs&);
	void InputTurnOn(const FElysiumInputArgs&) { Activate(); }

	void InputBreak(const FElysiumInputArgs& Args);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	FElysiumEntityHandle Endpoint1, Endpoint2;
	ElysiumModelPhysics::FNativeOutput NativeEndpoint1, NativeEndpoint2;
	ElysiumModelPhysics::FNativeOutput AdditionalEndpoint1, AdditionalEndpoint2;
	int32 EndpointFieldOwner1 = INDEX_NONE, EndpointFieldOwner2 = INDEX_NONE;
	FString EndpointField1, EndpointField2;
	FTransform SavedConstraintFrame;
	void SavedEndpointReference(const ElysiumModelPhysics::FNativeOutput&, int32&, FString&) const;
	ElysiumModelPhysics::FNativeOutput ResolveEndpointReference(int32, const FString&) const;
	bool bSavedConstraintPresent = false;
	bool bSavedConstraintEnabled = true;
	bool bConstraintBroken = false;
	void PublishPair(bool bAdd);
	UPrimitiveComponent* ResolveBody(const TCHAR* Key);
	void ConfigureAsHinge();

	// Bind the actual registered saved native endpoints after all Physics records exist.
	void ReinitConstraint();
};
