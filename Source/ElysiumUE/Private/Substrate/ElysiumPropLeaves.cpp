#include "Substrate/ElysiumPropLeaves.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumSignData.h"

void FElysiumPropButton::Spawn()
{
	FElysiumProp::Spawn();
	MaxStates = FMath::Clamp(MaxStates, 0, 7);
	CurrentState = FMath::Clamp(CurrentState, 0, MaxStates);
	SetSkin(CurrentState);
}

void FElysiumPropButton::InputSetState(int32 ExternalState, const FElysiumEntityHandle& Activator)
{
	const int32 LastSettable = FMath::Max(0, MaxStates - 1);
	SetButtonState(FMath::Clamp(ExternalState - 1, 0, LastSettable), Activator);
}

void FElysiumPropButton::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Button state"), FString::Printf(TEXT("%d / %d"), CurrentState, MaxStates));
}

void FElysiumPropButton::ButtonUse(const FElysiumEntityHandle& Activator)
{
	if (IsInert())
	{
		return;
	}
	if (bLocked)
	{
		static const FName OnPressedLocked(TEXT("OnPressedLocked"));
		FireOutput(OnPressedLocked, Activator);
		return;
	}

	static const FName OnPressed(TEXT("OnPressed"));
	FireOutput(OnPressed, Activator);
	SetButtonState(CurrentState >= MaxStates ? 0 : CurrentState + 1, Activator);
}

void FElysiumPropButton::SetButtonState(int32 NewState, const FElysiumEntityHandle& Activator)
{
	CurrentState = FMath::Clamp(NewState, 0, MaxStates);
	const FName Output(*FString::Printf(TEXT("OnSetState%d"), CurrentState + 1));
	FireOutput(Output, Activator);
	SetSkin(CurrentState);
}



void FElysiumPropSwitch::Spawn()
{
	FElysiumProp::Spawn();
	bActivated = (SpawnFlags & 0x2000) != 0;
	bLocked = (SpawnFlags & 0x4000) != 0;
	SetSkin(bActivated ? 1 : 0);
	PlayIdle();
}

void FElysiumPropSwitch::Use(const FElysiumEntityHandle& Activator)
{
	if (IsInert())
	{
		return;
	}
	if (bLocked)
	{
		static const FName OnLockedUse(TEXT("OnLockedUse"));
		FireOutput(OnLockedUse, Activator);
		return;
	}
	static const FName OnUse(TEXT("OnUse"));
	FireOutput(OnUse, Activator);
	SetSwitchState(!bActivated, Activator, /*bMirrorLinked=*/true);
}

void FElysiumPropSwitch::InputActivate(const FElysiumEntityHandle& Activator)
{
	SetSwitchState(true, Activator, /*bMirrorLinked=*/true);
}

void FElysiumPropSwitch::InputDeactivate(const FElysiumEntityHandle& Activator)
{
	SetSwitchState(false, Activator, /*bMirrorLinked=*/true);
}

void FElysiumPropSwitch::Think()
{
	if (!bTransitioning)
	{
		FElysiumProp::Think();
		return;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	if (AnimationEndTime > Now)
	{
		NextThink = static_cast<float>(FMath::Min(AnimationEndTime, Now + 0.1));
		return;
	}
	FinishTransition();
}

void FElysiumPropSwitch::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumProp::Serialize(Ar);
	Ar << bLocked << bActivated << bTransitioning << TransitionActivator;
	if (Ar.IsLoading())
	{
		SetSkin(bActivated ? 1 : 0);
		if (bTransitioning && World)
		{
			NextThink = static_cast<float>(World->NowSeconds() + 0.1);
		}
		else
		{
			PlayIdle();
		}
	}
}

void FElysiumPropSwitch::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Switch"), bActivated ? TEXT("activated") : TEXT("deactivated"));
	Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Linked switch"), LinkedSwitchName.IsEmpty() ? TEXT("(none)") : LinkedSwitchName);
	Out.Emplace(TEXT("Transition"), bTransitioning ? TEXT("playing") : TEXT("idle"));
}

void FElysiumPropSwitch::SetSwitchState(bool bNewActivated, const FElysiumEntityHandle& Activator, bool bMirrorLinked)
{
	if (bNewActivated == bActivated)
	{
		return;
	}
	bActivated = bNewActivated;
	TransitionActivator = Activator;
	bTransitioning = true;
	AnimationEndTime = 0.0;
	FElysiumInputArgs Args;
	Args.Activator = Activator;
	Args.Param = FElysiumVariant::String(bActivated ? TEXT("activate") : TEXT("deactivate"));
	InputSetAnimation(Args);

	if (bMirrorLinked && World && !LinkedSwitchName.IsEmpty())
	{
		FElysiumEntity* Linked = World->FindByName(LinkedSwitchName);
		if (Linked && Linked != this && Linked->Def
			&& Linked->Def->Classname.Equals(TEXT("prop_switch"), ESearchCase::IgnoreCase))
		{
			static_cast<FElysiumPropSwitch*>(Linked)->SetSwitchState(
				bActivated, Activator, /*bMirrorLinked=*/false);
		}
		else
		{
			UE_LOG(LogElysiumProp, Warning,
				TEXT("%s linkedswitch '%s' did not resolve to another prop_switch"),
				*DebugString(), *LinkedSwitchName);
		}
	}

	const double Now = World ? World->NowSeconds() : 0.0;
	if (AnimationEndTime <= Now)
	{
		FinishTransition();
	}
	else
	{
		NextThink = static_cast<float>(FMath::Min(AnimationEndTime, Now + 0.1));
	}
}

void FElysiumPropSwitch::FinishTransition()
{
	if (!bTransitioning)
	{
		return;
	}
	bTransitioning = false;
	SetSkin(bActivated ? 1 : 0);
	FireOutput(bActivated ? FName(TEXT("OnActivate")) : FName(TEXT("OnDeactivate")),
		TransitionActivator);
	PlayIdle();
}

void FElysiumPropSwitch::PlayIdle()
{
	FElysiumInputArgs Args;
	Args.Activator = Handle;
	Args.Param = FElysiumVariant::String(bActivated ? TEXT("idle_on") : TEXT("idle_off"));
	InputSetAnimation(Args);
}



FElysiumUseBeginResult FElysiumPropSign::BeginPlayerUse(const FElysiumUseContext& Context)
{
	if (!World || Context.Activator != World->PlayerHandle() || IsInert())
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	const FElysiumEntityHandle Existing = World->GetOpenSign();
	if (Existing.IsSet() && Existing != Handle)
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	if (!EnsurePanel())
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	World->OpenSign(Handle, Panel, 0.0f);
	static const FName OnUseBegin(TEXT("OnUseBegin"));
	static const FName OnReadBegin(TEXT("OnReadBegin"));
	FireOutput(OnUseBegin, Context.Activator);
	FireOutput(OnReadBegin, Context.Activator);
	return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
}

void FElysiumPropSign::EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason)
{
	if (World && World->GetOpenSign() == Handle)
	{
		World->CloseSign(/*bSilent=*/true);
	}
	static const FName OnUseEnd(TEXT("OnUseEnd"));
	static const FName OnReadEnd(TEXT("OnReadEnd"));
	FireOutput(OnUseEnd, Context.Activator);
	FireOutput(OnReadEnd, Context.Activator);
}

void FElysiumPropSign::OnDormancyChanged()
{
	FElysiumProp::OnDormancyChanged();
	if (IsInert() && World && World->GetOpenSign() == Handle)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
	}
}

void FElysiumPropSign::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Definition"), DefinitionFile.IsEmpty() ? TEXT("(none)") : DefinitionFile);
	Out.Emplace(TEXT("On screen"), World && World->GetOpenSign() == Handle ? TEXT("YES") : TEXT("no"));
}

bool FElysiumPropSign::EnsurePanel()
{
	if (Panel.IsValid())
	{
		return true;
	}
	if (!World || DefinitionFile.IsEmpty())
	{
		UE_LOG(LogElysiumProp, Warning,
			TEXT("%s cannot open prop_sign: %s"), *DebugString(),
			!World ? TEXT("entity world is unavailable") : TEXT("definition_file is empty"));
		return false;
	}
	TSharedPtr<FElysiumSignData> Parsed = MakeShared<FElysiumSignData>();
	if (!FElysiumSignData::Load(DefinitionFile, *Parsed, World))
	{
		UE_LOG(LogElysiumProp, Warning, TEXT("%s: could not load prop_sign data '%s'"),
			*DebugString(), *DefinitionFile);
		return false;
	}
	Panel = Parsed;
	return true;
}
