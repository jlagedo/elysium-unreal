#pragma once

#include "CoreMinimal.h"

class SWidget;

// S6 — the input scope stack (roadmap 11.5, `runtime-architecture.md` §8.1). Plain C++, no UObject
// reflection: the stack and its arbitration are the whole rule set, so they are asserted with no
// game instance, no world, no local player and no RHI — `Elysium.Substrate.InputScopes`.
// `UElysiumInputSubsystem` owns one of these and is the only thing that writes the engine's input
// mode, the cursor and (at 10.6) the mapping contexts.

// What the top scope asks the engine for. Mirrors the three FInputMode* shapes, kept as our own
// enum so the stack carries no engine type and the test tier needs no viewport.
enum class EElysiumInputMode : uint8
{
	GameOnly,    // FInputModeGameOnly     — the world has the mouse
	GameAndUI,   // FInputModeGameAndUI    — both, cursor over the world
	UIOnly,      // FInputModeUIOnly       — a screen has it; the controller sees nothing
};

namespace ElysiumInput
{
	inline const TCHAR* ModeName(EElysiumInputMode Mode)
	{
		switch (Mode)
		{
		case EElysiumInputMode::GameOnly:  return TEXT("GameOnly");
		case EElysiumInputMode::GameAndUI: return TEXT("GameAndUI");
		case EElysiumInputMode::UIOnly:    return TEXT("UIOnly");
		}
		return TEXT("?");
	}

	// The one priority table. Every push names a constant from here rather than an integer, so the
	// question "what happens when X opens over Y" has a single place to be answered.
	//
	// Debug is deliberately the top of the table, not the bottom: F1 is a developer asking for the
	// debug UI over whatever is on screen, and that request has to outrank a conversation or a menu
	// or it is useless in exactly the situations it is needed. What keeps it from eating a screen's
	// clicks is the other half of the rule — `RevokesDebugCapture` below — which takes an *inherited*
	// capture away the moment a screen comes up.
	namespace Priority
	{
		inline constexpr int32 Game      = 0;    // the empty stack: gameplay owns the mouse
		inline constexpr int32 Sign      = 10;   // game_sign / popup panel
		inline constexpr int32 Cinematic = 20;   // scripted camera + choreography (P12)
		inline constexpr int32 Chargen   = 30;   // the genesis screens (9.4)
		inline constexpr int32 Dialogue  = 40;   // the .dlg conversation box
		inline constexpr int32 Menu      = 50;   // main / pause / game-over screens
		inline constexpr int32 Debug     = 100;  // Cog's ImGui capture
	}
}

// Identity for a pushed scope. Zero is "nothing pushed", and an id is never reused within a stack,
// so popping a handle twice — or popping one the stack has already dropped — is a no-op rather than
// a mismatched pop that takes somebody else's scope down.
struct FElysiumInputScopeHandle
{
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
	void Reset() { Id = 0; }

	friend bool operator==(FElysiumInputScopeHandle A, FElysiumInputScopeHandle B) { return A.Id == B.Id; }
	friend bool operator!=(FElysiumInputScopeHandle A, FElysiumInputScopeHandle B) { return A.Id != B.Id; }
};

// One claim on input. A screen, a conversation, a cutscene or the debug UI pushes one while it is
// up and pops it when it goes away; the top of the stack decides what the engine is told.
struct FElysiumInputScope
{
	// "Menu", "Dialogue", "Sign", "Cinematic", "Chargen", "Debug". Diagnostic and lookup only —
	// arbitration is by priority, so two scopes may share a name.
	FName Name;

	// ElysiumInput::Priority::*.
	int32 Priority = ElysiumInput::Priority::Game;

	EElysiumInputMode Mode = EElysiumInputMode::GameOnly;
	bool bShowCursor = false;

	// The mapping contexts applied while this scope is top, by id. Nothing resolves them yet: the
	// Enhanced Input surface is 10.6's, and it reads them from here rather than adding a fourth
	// owner. `FElysiumInputScopeStack::Resolve` already reports the active set, so a context change
	// is observable before a single UInputMappingContext asset exists.
	TArray<FName> Contexts;

	// Where keyboard focus goes under UIOnly. A screen that does not take focus never sees Escape
	// (the menu) or its number keys (the dialogue box), because focus otherwise stays on the game
	// viewport widget. Held as a plain shared pointer: SWidget is not a UObject, and keeping the
	// stack free of engine types is what lets it run in the Substrate tier.
	TSharedPtr<SWidget> FocusWidget;

	// Assigned by Push. Ignored on the way in.
	FElysiumInputScopeHandle Handle;
};

// What the stack resolves to — the whole of what the engine is told, in one comparable value, so
// the subsystem can skip a redundant write (re-applying UIOnly re-steals focus every time).
struct FElysiumInputState
{
	FName Name;                       // the deciding scope, NAME_None for the empty stack
	EElysiumInputMode Mode = EElysiumInputMode::GameOnly;
	bool bShowCursor = false;
	TArray<FName> Contexts;
	TSharedPtr<SWidget> FocusWidget;

	bool operator==(const FElysiumInputState& Other) const
	{
		return Name == Other.Name && SameEngineState(Other);
	}
	bool operator!=(const FElysiumInputState& Other) const { return !(*this == Other); }

	// Everything the engine is actually told, minus which scope said it. What the subsystem compares
	// before writing: a sign opening over a running game decides the same mode the game already had,
	// and re-issuing FInputMode* for a change the engine cannot see is pure churn.
	bool SameEngineState(const FElysiumInputState& Other) const
	{
		return Mode == Other.Mode
			&& bShowCursor == Other.bShowCursor
			&& Contexts == Other.Contexts
			&& FocusWidget == Other.FocusWidget;
	}

	FString Describe() const;
};

// The priority stack itself. Push/pop is handle-based rather than strictly last-in-first-out,
// because screens genuinely close out of order: a conversation ends behind an open pause menu, and
// the menu must still find exactly the mode it pushed over.
struct FElysiumInputScopeStack
{
	// Returns the scope's handle. The scope keeps its place in push order; only Resolve() cares
	// about priority.
	FElysiumInputScopeHandle Push(FElysiumInputScope Scope);

	// Drops the scope wherever it sits. False when the handle is stale or was never pushed.
	bool Pop(FElysiumInputScopeHandle Handle);

	// Drops every scope carrying the name; returns how many went. The Debug scope's escape hatch.
	int32 PopByName(FName Name);

	void Reset() { Entries.Reset(); }

	int32 Num() const { return Entries.Num(); }
	bool IsEmpty() const { return Entries.Num() == 0; }
	bool Contains(FElysiumInputScopeHandle Handle) const { return Find(Handle) != nullptr; }

	const FElysiumInputScope* Find(FElysiumInputScopeHandle Handle) const;
	FElysiumInputScope* Find(FElysiumInputScopeHandle Handle);

	// The deciding scope: highest priority, and on a tie the one pushed last — so two screens at
	// the same priority behave like an ordinary modal stack. Null for an empty stack.
	const FElysiumInputScope* Top() const;

	// The top scope's whole claim, or the gameplay default when nothing is pushed. That default is
	// the identity of the system: an empty stack is the game holding the mouse, which is also what
	// "the stack is balanced" means after every screen has closed.
	FElysiumInputState Resolve() const;

	// Bottom-to-top, one line per scope. The `elysium.inputscopes` dump and the test's failure text.
	FString Describe() const;

	// Every scope, in push order.
	const TArray<FElysiumInputScope>& All() const { return Entries; }

private:
	TArray<FElysiumInputScope> Entries;
	int32 NextId = 1;
};

namespace ElysiumInput
{
	// The one rule that keeps the debug UI from eating a screen's clicks: a scope that takes the
	// mouse away from the world entirely — a menu, a conversation, chargen — revokes any ImGui
	// capture that was already up when it opened. Cog's capture is a Slate-layer catcher widget, so
	// an inherited one swallows the click before Slate reaches the screen underneath and the screen
	// is simply dead (`ui-architecture.md` §1); no input mode can arbitrate that, only revocation.
	//
	// Deliberately *at push time only*. Pressing F1 afterwards is a developer asking for the debug UI
	// over that screen and gets it — which is the front end's whole workflow, since a menu is up
	// there permanently.
	inline bool RevokesDebugCapture(const FElysiumInputScope& Incoming)
	{
		return Incoming.Mode == EElysiumInputMode::UIOnly;
	}
}
