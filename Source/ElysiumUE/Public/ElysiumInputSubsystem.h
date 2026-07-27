#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ElysiumInputScope.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UObject/WeakObjectPtr.h"

#include "ElysiumInputSubsystem.generated.h"

class APlayerController;
class IConsoleObject;
class UGameInstance;

// S6 — the one input-mode arbiter (roadmap 11.5, `runtime-architecture.md` §8.1). Mode, cursor and
// (at 10.6) mapping contexts come from the top of one priority stack, and nothing else in the
// project calls SetInputMode: the menu, the dialogue box, sign panels, cutscenes, chargen and Cog
// all push a scope while they are up and pop it when they go away.
//
// LocalPlayer-scoped, which is the application lifetime for input: the stack has to survive travel
// (a menu is up across the level load that New Game triggers) while the player controller it writes
// to does not. Every apply re-resolves the controller, and a fresh world re-applies the stack onto
// its new one.
//
// **Push/pop is handle-based, not last-in-first-out.** Screens close out of order — a conversation
// ends behind an open pause menu — and the point of the whole system is that whatever is left finds
// exactly the mode it pushed over.
UCLASS()
class UElysiumInputSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	// The local player's arbiter. Null before the first local player exists (early boot) and in a
	// headless world with no player — every caller handles that, the same null-service discipline
	// 11.2 established for the world services.
	static UElysiumInputSubsystem* Get(const UGameInstance* GameInstance);

	// Claim input. The returned handle is what releases it; a UIOnly claim also revokes any ImGui
	// capture Cog was holding when it opened (`ElysiumInput::RevokesDebugCapture`).
	FElysiumInputScopeHandle Push(FElysiumInputScope Scope);

	// Release a claim. Takes the handle by reference and clears it, so a caller cannot pop twice or
	// hold a stale id; a handle the stack no longer has is a no-op, not a mismatched pop.
	bool Pop(FElysiumInputScopeHandle& Handle);

	// Re-point a live scope's focus widget. The dialogue box rebuilds its whole tree every turn, so
	// the widget the scope focuses on re-apply has to follow it or a menu closing over a conversation
	// would hand focus to a dead widget.
	void SetFocusWidget(FElysiumInputScopeHandle Handle, TSharedPtr<SWidget> Widget);

	const FElysiumInputScopeStack& Stack() const { return Scopes; }
	FElysiumInputState State() const { return Scopes.Resolve(); }

	// Write the resolved state onto the current controller even if it has not changed. Used after a
	// travel, where the mode is per-controller state and the stack is not.
	void Reapply();

private:
	// Resolve the stack and write it to the controller, skipping a write that would change nothing
	// (re-applying UIOnly re-steals keyboard focus, which a screen behind a closing one would feel).
	void Apply();
	void ApplyToController(APlayerController* PC, const FElysiumInputState& NewState);

	APlayerController* ResolveController() const;

	void OnPostLoadMap(UWorld* LoadedWorld);

	// --- Cog ------------------------------------------------------------------------------------
	// Cog owns its own Slate capture and is not going to be taught about this stack (it is a
	// vendored plugin), so the arbiter *observes* it: a per-frame reconcile pushes a Debug scope
	// when ImGui takes input and pops it when it lets go. That is what puts the debug UI in the same
	// ordering as everything else — F1 over a conversation stands the conversation's UI-only mode
	// down for as long as ImGui has input, and closing it restores exactly what was there.
	bool ReconcileDebugCapture(float DeltaTime);
	// Take an ImGui capture away (the other half of RevokesDebugCapture) and drop the Debug scope.
	void RevokeDebugCapture();

	FElysiumInputScopeStack Scopes;

	// The scope standing for Cog's capture, while it holds one.
	FElysiumInputScopeHandle DebugScope;

	// What was last written, and to whom. A controller change (travel) forces the next write.
	FElysiumInputState Applied;
	bool bApplied = false;
	TWeakObjectPtr<APlayerController> AppliedTo;

	FTSTicker::FDelegateHandle DebugTicker;
	FDelegateHandle PostLoadMapHandle;
	TArray<IConsoleObject*> ConsoleObjects;
};
