#include "ElysiumInputSubsystem.h"

#include "ElysiumInputAssets.h"
#include "ElysiumInputRouter.h"
#include "ElysiumPlayerController.h"

#include "CommonInputSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputMappingContext.h"

#include "CogCommon.h"
#if ENABLE_COG
#include "CogSubsystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumInput, Log, All);

void UElysiumInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCommonInputSubsystem>();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>())
	{
		if (UCommonInputSubsystem* CommonInput =
			LocalPlayer->GetSubsystem<UCommonInputSubsystem>())
		{
			InputMethodChangedHandle = CommonInput->OnInputMethodChangedNative.AddUObject(
				this, &UElysiumInputSubsystem::OnInputMethodChanged);
		}
	}

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.inputscopes"),
		TEXT("Dump the input scope stack, bottom to top (* marks the scope deciding mode/cursor)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogElysiumInput, Display, TEXT("input scopes:\n%s-> %s"),
				*Scopes.Describe(), *Scopes.Resolve().Describe());
		}),
		ECVF_Default));

#if ENABLE_COG
	// Cog is vendored and knows nothing about the stack, so the arbiter watches it instead. Every
	// frame, not on a hook: F1 is a shortcut inside Cog's own input handling and there is no event
	// to bind. The reconcile is two pointer hops and a bool compare when nothing has changed.
	DebugTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
		{
			return ReconcileDebugCapture(DeltaTime);
		}));
#endif
}

void UElysiumInputSubsystem::Deinitialize()
{
	ApplyMappingContexts({});
	if (InputMethodChangedHandle.IsValid())
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>())
		{
			if (UCommonInputSubsystem* CommonInput =
				LocalPlayer->GetSubsystem<UCommonInputSubsystem>())
			{
				CommonInput->OnInputMethodChangedNative.Remove(InputMethodChangedHandle);
			}
		}
		InputMethodChangedHandle.Reset();
	}
	if (DebugTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebugTicker);
		DebugTicker.Reset();
	}
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();
	Scopes.Reset();
	DebugScope.Reset();
	ContextAssets.Reset();
	MissingContextNames.Reset();

	Super::Deinitialize();
}

UElysiumInputSubsystem* UElysiumInputSubsystem::Get(const UGameInstance* GameInstance)
{
	ULocalPlayer* LocalPlayer = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UElysiumInputSubsystem>() : nullptr;
}

FElysiumInputScopeHandle UElysiumInputSubsystem::Push(FElysiumInputScope Scope)
{
	// A screen taking the mouse away from the world takes it back from the debug UI too. This has to
	// happen before the push, because revoking pops the Debug scope and the two would otherwise race
	// for the top on the same frame.
	if (ElysiumInput::RevokesDebugCapture(Scope))
	{
		RevokeDebugCapture();
	}

	const FElysiumInputScopeHandle Handle = Scopes.Push(MoveTemp(Scope));
	Apply();
	return Handle;
}

bool UElysiumInputSubsystem::Pop(FElysiumInputScopeHandle& Handle)
{
	const bool bPopped = Scopes.Pop(Handle);
	Handle.Reset();
	if (bPopped)
	{
		Apply();
	}
	return bPopped;
}

void UElysiumInputSubsystem::SetFocusWidget(FElysiumInputScopeHandle Handle, TSharedPtr<SWidget> Widget)
{
	FElysiumInputScope* Scope = Scopes.Find(Handle);
	if (!Scope || Scope->FocusWidget == Widget)
	{
		return;
	}
	Scope->FocusWidget = MoveTemp(Widget);
	Apply();
}

void UElysiumInputSubsystem::Reapply()
{
	// Mapping contexts are stored on the current UEnhancedPlayerInput. A replacement controller
	// gets a fresh one even though the LocalPlayer subsystem and our logical scope stack survive,
	// so make the next Apply add every wanted Elysium context to that new owner. The generated
	// contexts use Untracked registration; adding one already present simply refreshes its entry.
	AppliedContextNames.Reset();
	bApplied = false;
	Apply();
}

void UElysiumInputSubsystem::Apply()
{
	APlayerController* PC = ResolveController();
	if (!PC)
	{
		// No controller yet (early boot, or a world between travels). The stack is the truth; the
		// next controller gets it through PlayerControllerChanged. Drop the remembered state with
		// it — it holds a reference to a widget belonging to the world that is going away.
		Applied = FElysiumInputState();
		AppliedTo = nullptr;
		bApplied = false;
		return;
	}

	const FElysiumInputState NewState = Scopes.Resolve();
	const bool bSameController = bApplied && AppliedTo.Get() == PC;
	if (!bSameController || !Applied.SameEngineState(NewState))
	{
		ApplyToController(PC, NewState);
		UE_LOG(LogElysiumInput, Verbose, TEXT("input scope -> %s (%d on the stack)"),
			*NewState.Describe(), Scopes.Num());
	}

	Applied = NewState;
	AppliedTo = PC;
	bApplied = true;
}

void UElysiumInputSubsystem::ApplyToController(APlayerController* PC, const FElysiumInputState& NewState)
{
	ApplyMappingContexts(NewState.Contexts);

	switch (NewState.Mode)
	{
	case EElysiumInputMode::UIOnly:
	{
		FInputModeUIOnly Mode;
		// DoNotLock: locking the cursor to the viewport is a mouselook affordance, and a screen with
		// the mouse is exactly when the player may want to leave the window.
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (NewState.FocusWidget.IsValid())
		{
			Mode.SetWidgetToFocus(NewState.FocusWidget);
		}
		PC->SetInputMode(Mode);
		break;
	}
	case EElysiumInputMode::GameAndUI:
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		if (NewState.FocusWidget.IsValid())
		{
			Mode.SetWidgetToFocus(NewState.FocusWidget);
		}
		PC->SetInputMode(Mode);
		break;
	}
	case EElysiumInputMode::GameOnly:
	default:
		PC->SetInputMode(FInputModeGameOnly());
		break;
	}

	PC->SetShowMouseCursor(ResolveCursorVisible(NewState.CursorPolicy));

	// A key held across a scope change must not bleed into what comes next: walking into a
	// conversation with W down has to stop walking. UI-only mode may prevent another controller
	// sample, so the router both clears its latches and replaces the body's retained command.
	if (const AElysiumPlayerController* Elysium = Cast<AElysiumPlayerController>(PC))
	{
		if (UElysiumInputRouter* Router = Elysium->GetInputRouter())
		{
			Router->ApplyScopeState(NewState);
		}
	}
}

void UElysiumInputSubsystem::ApplyMappingContexts(const TArray<FName>& ContextNames)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	UEnhancedInputLocalPlayerSubsystem* Enhanced =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Enhanced)
	{
		return;
	}

	const TSet<FName> Wanted(ContextNames);
	const FModifyContextOptions Options;
	for (const FName AppliedName : AppliedContextNames.Difference(Wanted))
	{
		if (UInputMappingContext* Context = ResolveMappingContext(AppliedName))
		{
			Enhanced->RemoveMappingContext(Context, Options);
		}
		AppliedContextNames.Remove(AppliedName);
	}
	// Records what actually went in, not what was asked for. A context whose asset has not been
	// generated resolves null and adds nothing; remembering it as applied would make every later
	// Difference() come out empty and retire the retry, leaving the pad dead for the rest of the
	// session behind one early log line. A session that never opens a UI-only screen — the headless
	// harnesses, or any boot straight into a map — never gets another chance otherwise.
	for (const FName WantedName : Wanted.Difference(AppliedContextNames))
	{
		if (UInputMappingContext* Context = ResolveMappingContext(WantedName))
		{
			Enhanced->AddMappingContext(Context, /*Priority*/ 0, Options);
			AppliedContextNames.Add(WantedName);
		}
	}
}

UInputMappingContext* UElysiumInputSubsystem::ResolveMappingContext(FName ContextName)
{
	if (TObjectPtr<UInputMappingContext>* Found = ContextAssets.Find(ContextName))
	{
		return Found->Get();
	}

	const TCHAR* Path = nullptr;
	if (ContextName == ElysiumInput::PlayerKeyboardMouseContext())
	{
		Path = ElysiumInputAssets::KeyboardMouseContextPath;
	}
	else if (ContextName == ElysiumInput::PlayerGamepadContext())
	{
		Path = ElysiumInputAssets::GamepadContextPath;
	}
	if (!Path)
	{
		if (!MissingContextNames.Contains(ContextName))
		{
			UE_LOG(LogElysiumInput, Error, TEXT("input scope names unknown mapping context '%s'"),
				*ContextName.ToString());
			MissingContextNames.Add(ContextName);
		}
		return nullptr;
	}

	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path);
	if (Context)
	{
		ContextAssets.Add(ContextName, Context);
		return Context;
	}
	if (!MissingContextNames.Contains(ContextName))
	{
		UE_LOG(LogElysiumInput, Error,
			TEXT("mapping context '%s' is missing; run `uv run elysium export bundle policy` (expected %s)"),
			*ContextName.ToString(), Path);
		MissingContextNames.Add(ContextName);
	}
	return nullptr;
}

APlayerController* UElysiumInputSubsystem::ResolveController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	return LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
}

void UElysiumInputSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// The input mode is per-controller state and the stack is not, so a travel would otherwise drop
	// whatever is up: the menu survives the level load New Game triggers, and the fresh controller
	// has to be told the menu owns the mouse.
	Reapply();
}

void UElysiumInputSubsystem::OnInputMethodChanged(ECommonInputType InputType)
{
	// Auto cursor policy is resolved at apply time, so invalidate the remembered engine state even
	// though the logical scope itself has not changed. Reapplying UIOnly also restores its current
	// action focus when the player puts the mouse down and returns to a pad.
	bApplied = false;
	Apply();
}

bool UElysiumInputSubsystem::ResolveCursorVisible(EElysiumCursorPolicy Policy) const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	const UCommonInputSubsystem* CommonInput = LocalPlayer
		? LocalPlayer->GetSubsystem<UCommonInputSubsystem>() : nullptr;
	return ElysiumInput::ResolveCursorVisible(Policy,
		!CommonInput || CommonInput->GetCurrentInputType() == ECommonInputType::MouseAndKeyboard);
}

// Cog.

bool UElysiumInputSubsystem::ReconcileDebugCapture(float /*DeltaTime*/)
{
#if ENABLE_COG
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	UCogSubsystem* Cog = World ? World->GetSubsystem<UCogSubsystem>() : nullptr;
	const bool bCogHasInput = Cog && Cog->GetContext().GetEnableInput();

	if (bCogHasInput && !DebugScope.IsValid())
	{
		FElysiumInputScope Scope;
		Scope.Name = TEXT("Debug");
		Scope.Priority = ElysiumInput::Priority::Debug;
		// Cog drives Slate capture itself (its ImGui catcher widget takes the click, and the local
		// patch in CogImguiContext restores mouselook on release), so what this scope contributes is
		// the *ordering*: at the top it stands a screen's UI-only mode down for as long as ImGui has
		// input, and popping it restores exactly what was underneath. The cursor claim matches what
		// Cog does to the controller anyway, so the two never disagree.
		Scope.Mode = EElysiumInputMode::GameOnly;
		Scope.CursorPolicy = EElysiumCursorPolicy::Always;
		ElysiumInput::AddPlayerContexts(Scope.Contexts);
		DebugScope = Scopes.Push(MoveTemp(Scope));
		Apply();
	}
	else if (!bCogHasInput && DebugScope.IsValid())
	{
		Scopes.Pop(DebugScope);
		DebugScope.Reset();
		Apply();
	}
#endif
	return true;
}

void UElysiumInputSubsystem::RevokeDebugCapture()
{
#if ENABLE_COG
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	if (UCogSubsystem* Cog = World ? World->GetSubsystem<UCogSubsystem>() : nullptr)
	{
		// Only when it is already enabled. SetEnableInput dereferences the ImGui context
		// (ClearInputMouse), and Cog creates that lazily on its first tick — calling it at boot
		// crashed outright. `bEnableInput` defaults false and only becomes true through a call that
		// already required a live context, so "true" is a safe proxy for "initialised".
		if (Cog->GetContext().GetEnableInput())
		{
			Cog->GetContext().SetEnableInput(false);
		}
	}
#endif
	if (DebugScope.IsValid())
	{
		Scopes.Pop(DebugScope);
		DebugScope.Reset();
	}
}
