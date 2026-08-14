#include "ElysiumInputScope.h"

#include "ElysiumUserCmd.h"

bool ElysiumInput::AllowsGameplayCommands(const FElysiumInputState& State)
{
	return State.Contexts.Contains(PlayerKeyboardMouseContext())
		|| State.Contexts.Contains(PlayerGamepadContext());
}

FElysiumUserCmd ElysiumInput::GateGameplayCommand(
	const FElysiumInputState& State, const FElysiumUserCmd& Command)
{
	if (AllowsGameplayCommands(State))
	{
		return Command;
	}

	FElysiumUserCmd Gated = Command;
	Gated.Move = FVector2D::ZeroVector;
	Gated.Up = 0.0f;
	Gated.LookDelta = FVector2D::ZeroVector;
	Gated.Buttons = 0;
	return Gated;
}

FString FElysiumInputState::Describe() const
{
	FString Out = FString::Printf(TEXT("%s mode=%s cursor=%s"),
		Name.IsNone() ? TEXT("<game>") : *Name.ToString(),
		ElysiumInput::ModeName(Mode),
		ElysiumInput::CursorPolicyName(CursorPolicy));
	if (Contexts.Num() > 0)
	{
		Out += TEXT(" ctx=");
		for (int32 i = 0; i < Contexts.Num(); ++i)
		{
			Out += (i ? TEXT(",") : TEXT("")) + Contexts[i].ToString();
		}
	}
	if (FocusWidget.IsValid())
	{
		Out += TEXT(" focus");
	}
	return Out;
}

FElysiumInputScopeHandle FElysiumInputScopeStack::Push(FElysiumInputScope Scope)
{
	Scope.Handle.Id = NextId++;
	Entries.Add(MoveTemp(Scope));
	return Entries.Last().Handle;
}

bool FElysiumInputScopeStack::Pop(FElysiumInputScopeHandle Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}
	const int32 Index = Entries.IndexOfByPredicate(
		[Handle](const FElysiumInputScope& Scope) { return Scope.Handle == Handle; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	// RemoveAt, not RemoveAtSwap: push order is the tie-break Top() reads, so the order of the
	// entries that stay has to survive a pop from the middle.
	Entries.RemoveAt(Index);
	return true;
}

int32 FElysiumInputScopeStack::PopByName(FName Name)
{
	return Entries.RemoveAll([Name](const FElysiumInputScope& Scope) { return Scope.Name == Name; });
}

const FElysiumInputScope* FElysiumInputScopeStack::Find(FElysiumInputScopeHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return nullptr;
	}
	return Entries.FindByPredicate(
		[Handle](const FElysiumInputScope& Scope) { return Scope.Handle == Handle; });
}

FElysiumInputScope* FElysiumInputScopeStack::Find(FElysiumInputScopeHandle Handle)
{
	return const_cast<FElysiumInputScope*>(static_cast<const FElysiumInputScopeStack*>(this)->Find(Handle));
}

const FElysiumInputScope* FElysiumInputScopeStack::Top() const
{
	const FElysiumInputScope* Best = nullptr;
	for (const FElysiumInputScope& Scope : Entries)
	{
		// >= so a later push at the same priority wins: same-priority screens stack like modals.
		if (!Best || Scope.Priority >= Best->Priority)
		{
			Best = &Scope;
		}
	}
	return Best;
}

FElysiumInputState FElysiumInputScopeStack::Resolve() const
{
	FElysiumInputState State;
	// Gameplay is the identity of the stack. A pushed scope replaces this set explicitly: signs
	// and Cog retain it, while screens and cinematics leave their context list empty.
	ElysiumInput::AddPlayerContexts(State.Contexts);
	if (const FElysiumInputScope* Scope = Top())
	{
		State.Name = Scope->Name;
		State.Mode = Scope->Mode;
		State.CursorPolicy = Scope->CursorPolicy;
		State.Contexts = Scope->Contexts;
		State.FocusWidget = Scope->FocusWidget;
	}
	return State;
}

FString FElysiumInputScopeStack::Describe() const
{
	if (Entries.Num() == 0)
	{
		return TEXT("(empty — the game has the mouse)");
	}
	const FElysiumInputScope* TopScope = Top();
	FString Out;
	for (const FElysiumInputScope& Scope : Entries)
	{
		Out += FString::Printf(TEXT("%s#%d %s prio=%d mode=%s cursor=%s\n"),
			(&Scope == TopScope) ? TEXT("* ") : TEXT("  "),
			Scope.Handle.Id,
			*Scope.Name.ToString(),
			Scope.Priority,
			ElysiumInput::ModeName(Scope.Mode),
			ElysiumInput::CursorPolicyName(Scope.CursorPolicy));
	}
	return Out;
}
