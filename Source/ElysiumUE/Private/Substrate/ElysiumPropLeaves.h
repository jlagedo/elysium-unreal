#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumProp.h"

struct FElysiumSignData;
class UElysiumEntityQueryBox;

// VtMB CPropButton (vampire.dll 0x10214d60; datamap builder 0x10214e30).
// State values are skin indices 0..max_states. Use fires OnPressed before cycling the state;
// SetState's external value is one-based and maps back to a zero-based skin/output index.
class FElysiumPropButton final : public FElysiumProp
{
public:
	bool bLocked = false;
	int32 CurrentState = 0;
	int32 MaxStates = 0;

	virtual void Spawn() override;
	~FElysiumPropButton() override;
	void ReleaseForRemoval() override;
	void OnRuntimeCollisionChanged() override;
	void OnPostRestore(FElysiumEntityWorld& InWorld) override;
	UElysiumEntityQueryBox* QueryBody() const { return StudioQuery; }
	UPrimitiveComponent* GetCollisionQueryBody() const override;

	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return bLocked; }
	virtual void Use(const FElysiumEntityHandle& Activator) override { ButtonUse(Activator); }

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	void InputToggleLock() { bLocked = !bLocked; }
	void InputSetState(int32 ExternalState, const FElysiumEntityHandle& Activator);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	bool UsesStudioQueryObb() const override { return true; }
	void OnRuntimeModelChanged() override;
	void OnRuntimeModelAdmitted() override;
	void OnRuntimeTransformChanged() override;
	void OnDormancyChanged() override;
	void BuildStudioQuery(bool bUseSavedBounds);
	void RetireStudioQuery();
	void GateStudioQuery();
	UElysiumEntityQueryBox* StudioQuery = nullptr;
	bool bSpawnQueryPending = true;
	bool bSavedQueryBounds = false;
	void ButtonUse(const FElysiumEntityHandle& Activator);
	void SetButtonState(int32 NewState, const FElysiumEntityHandle& Activator);
};

// CPropSwitch1020d850/1020dd70: owned studio query OBB and server sequence state.
class FElysiumPropSwitch final : public FElysiumProp
{
public:
	virtual ~FElysiumPropSwitch() override;
	void ReleaseForRemoval() override;
	bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	bool ReplayActivationAfterRestore() const override { return true; }
	void Activate() override;
	void OnPostRestore(FElysiumEntityWorld& InWorld) override;
	void OnRuntimeCollisionChanged() override;
	UPrimitiveComponent* GetAttachBody() const override;
	UElysiumEntityQueryBox* QueryBody() const { return StudioQuery; }
	UPrimitiveComponent* GetCollisionQueryBody() const override;
	int32 SequenceIndex = 0; //m_nSequence, including missing-idle index0 fallback
	float SequenceCycle = 0.f;
	float SequenceCycleRate = 0.f; //+650 cached by ResetSequenceInfo; negative-index helper retains it
	float SequenceAnimTime = 0.f, SequencePrevAnimTime = 0.f;
	bool bSequenceFinished = false;
	float SequencePlaybackRate() const { return AnimationPlaybackRate; }
	void SetSequencePlaybackRate(float Rate);
	FString LinkedSwitchName;
	//1020e100 reads reset_state only for a true level-transition restore bool.
	int32 ResetState = 0;
	bool bLocked = false;
	bool bActivated = false;
	bool bTransitioning = false;
	FElysiumEntityHandle TransitionActivator;

	// The `___RTDynamicCast(ent, CBaseEntity, CPropSwitch 0x105a6404)` the VSound group seam makes.
	virtual FElysiumPropSwitch* AsPropSwitch() override { return this; }

	virtual void Spawn() override;

	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return bLocked; }
	virtual void Use(const FElysiumEntityHandle& Activator) override;

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	void InputToggle(const FElysiumEntityHandle& Activator) { Use(Activator); }
	void InputActivate(const FElysiumEntityHandle& Activator);
	void InputDeactivate(const FElysiumEntityHandle& Activator);

	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	bool UsesStudioQueryObb() const override { return true; }
	void OnRuntimeModelChanged() override;
	void OnRuntimeModelAdmitted() override;
	void OnRuntimeTransformChanged() override;
	void OnDormancyChanged() override;
	void BuildStudioQuery(bool bUseSavedBounds);
	void RetireStudioQuery();
	void GateStudioQuery();
	UElysiumEntityQueryBox* StudioQuery = nullptr;
	bool bSpawnQueryPending = true;
	bool bSavedQueryBounds = false;
	FElysiumEntityHandle LinkedSwitch;
	int32 SwitchSequences[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	void SetSwitchState(bool bNewActivated, const FElysiumEntityHandle& Activator, bool bMirrorLinked);
	void PlayIdle();
	void PlaySwitchSequence(int32 Index, const FString& Label);
	void AdvanceSwitchSequence();
};

// CPropSign is a world prop and an explicit one-player session. The view is presentation only;
// this entity owns admission and produces both inherited use and sign-specific read edges.
class FElysiumPropSign final : public FElysiumProp
{
public:
	~FElysiumPropSign() override;
	void Spawn() override;
	void ReleaseForRemoval() override;
	void OnRuntimeCollisionChanged() override;
	void OnPostRestore(FElysiumEntityWorld& InWorld) override;
	UPrimitiveComponent* GetCollisionQueryBody() const override;
	UElysiumEntityQueryBox* QueryBody() const { return StudioQuery; }
	FString DefinitionFile;
	TSharedPtr<const FElysiumSignData> Panel;

	virtual bool IsUsable() const override { return true; }

	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason) override;
	virtual void OnDormancyChanged() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	bool UsesStudioQueryObb() const override { return true; }
	void OnRuntimeModelChanged() override;
	void OnRuntimeModelAdmitted() override;
	void OnRuntimeTransformChanged() override;
	void BuildStudioQuery(bool bUseSavedBounds);
	void RetireStudioQuery();
	void GateStudioQuery();
	UElysiumEntityQueryBox* StudioQuery = nullptr;
	bool bSpawnQueryPending = true;
	bool bSavedQueryBounds = false;
	bool EnsurePanel();
};
