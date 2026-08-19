#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumProp.h"

struct FElysiumSignData;

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

	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return bLocked; }
	virtual void Use(const FElysiumEntityHandle& Activator) override { ButtonUse(Activator); }

	void InputLock() { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	void InputToggleLock() { bLocked = !bLocked; }
	void InputSetState(int32 ExternalState, const FElysiumEntityHandle& Activator);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	void ButtonUse(const FElysiumEntityHandle& Activator);
	void SetButtonState(int32 NewState, const FElysiumEntityHandle& Activator);
};

// VtMB CPropSwitch. The state changes immediately, while its authored transition clip owns the
// delayed OnActivate/OnDeactivate edge. Static or incomplete bodies complete synchronously so the
// gameplay wire never depends on whether presentation content was available.
class FElysiumPropSwitch final : public FElysiumProp
{
public:
	FString LinkedSwitchName;
	// SEAM (parsed, unread): `reset_state` lands here through the registered keyfield and nothing in
	// the module reads it.
	int32 ResetState = 0;
	bool bLocked = false;
	bool bActivated = false;
	bool bTransitioning = false;
	FElysiumEntityHandle TransitionActivator;

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
	void SetSwitchState(bool bNewActivated, const FElysiumEntityHandle& Activator, bool bMirrorLinked);
	void FinishTransition();
	void PlayIdle();
};

// CPropSign is a world prop and an explicit one-player session. The view is presentation only;
// this entity owns admission and produces both inherited use and sign-specific read edges.
class FElysiumPropSign final : public FElysiumProp
{
public:
	FString DefinitionFile;
	TSharedPtr<const FElysiumSignData> Panel;

	virtual bool IsUsable() const override { return true; }

	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason) override;
	virtual void OnDormancyChanged() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	bool EnsurePanel();
};
