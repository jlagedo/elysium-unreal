#include "Substrate/ElysiumActivityTrigger.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumPlayerLog.h"

bool FElysiumActivityTrigger::CanBeginTouch(const FElysiumEntityHandle& Activator) const
{
	if (bDisabled || IsInert())
	{
		// The base's solidity test, and the only half of its admission the recovered `Touch` shares:
		// a disabled volume has no trigger to link against.
		return false;
	}
	return World != nullptr && Activator == World->PlayerHandle();
}

void FElysiumActivityTrigger::OnTouchStart(const FElysiumEntityHandle& Activator)
{
	// The base callback runs FIRST and owns its own admission — `OnStartTouch`, the `wait`-gated
	// `OnTrigger` and the spawnflag/filter tests. The two authored `OnTrigger` rows in `sm_diner_1`
	// are invalid in retail (the class declares no such output) and are the reason the base's fire
	// is harmless here: `FireOutput` matches authored rows, and those two resolve against the base's
	// own `OnTrigger`, which this class does inherit. That is a faithfulness gap the corpus survey
	// already records; it is not introduced here.
	FElysiumTriggerBase::OnTouchStart(Activator);

	if (!CanBeginTouch(Activator))
	{
		return;
	}
	bPlayerInside = true;
	// Retail's `Touch` runs on the collision tick that follows the link, so the first assertion
	// happens immediately rather than one refresh interval later.
	RefreshLevels();
	NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + RefreshIntervalSeconds);
}

void FElysiumActivityTrigger::Think()
{
	if (bTouchSuppressed)
	{
		// The base's `wait == -1` self-removal. This class inherits the base's `OnStartTouch` path,
		// so it can also inherit that latch; the refresh below must not swallow the removal.
		FElysiumTriggerBase::Think();
		return;
	}
	if (!bPlayerInside || bDisabled || IsInert())
	{
		NextThink = ELYSIUM_NEVER_THINK;
		return;
	}
	RefreshLevels();
	NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + RefreshIntervalSeconds);
}

void FElysiumActivityTrigger::RefreshLevels()
{
	FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
	if (Player == nullptr)
	{
		return;   // no player to author against: an ordinary absence in a headless world
	}
	// `-1` is unset. Only an authored value at or above zero is refreshed — and a value of zero IS
	// authored, so a volume may deliberately clear a channel on contact.
	if (SupernaturalLevel >= 0)
	{
		ElysiumLaw::SetSupernaturalLevel(*Player, SupernaturalLevel, ElysiumLaw::DeriveDuration);
	}
	if (CriminalLevel >= 0)
	{
		ElysiumLaw::SetCriminalLevel(*Player, CriminalLevel, ElysiumLaw::DeriveDuration);
	}
	if (InvestigateLevel >= 0)
	{
		ElysiumLaw::SetInvestigateLevel(*Player, InvestigateLevel);
	}
	++Refreshes;
}

void FElysiumActivityTrigger::OnTouchEnd(const FElysiumEntityHandle& Activator)
{
	FElysiumTriggerBase::OnTouchEnd(Activator);

	if (World == nullptr || Activator != World->PlayerHandle())
	{
		return;
	}
	bPlayerInside = false;
	NextThink = ELYSIUM_NEVER_THINK;

	FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr)
	{
		return;
	}
	// Exact-match release: clear a channel only while its current level is still the one THIS volume
	// installed. An overlapping volume that raised it, or a Discipline cast that did, keeps it.
	const bool bReleaseTimed = (SpawnFlags & ReleaseSpawnFlag) != 0;
	if (bReleaseTimed && SupernaturalLevel >= 0 && Player->Law.Supernatural == SupernaturalLevel)
	{
		ElysiumLaw::SetSupernaturalLevel(*Player, 0);
	}
	if (bReleaseTimed && CriminalLevel >= 0 && Player->Law.Criminal == CriminalLevel)
	{
		ElysiumLaw::SetCriminalLevel(*Player, 0);
	}
	// Investigate uses the same exact-match test and clears WITHOUT requiring the `0x20` bit.
	if (InvestigateLevel >= 0 && Player->Law.Investigate == InvestigateLevel)
	{
		ElysiumLaw::SetInvestigateLevel(*Player, 0);
	}
}

void FElysiumActivityTrigger::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumTriggerBase::Serialize(Ar);
	// Occupancy is real simulation state here, unlike on an enter-only trigger: it is what re-arms
	// the refresh think, and a save taken standing inside a `restricted_section` has to resume
	// re-asserting its level rather than waiting for an exit the player never makes.
	Ar << bPlayerInside;
	Ar << Refreshes;
}

void FElysiumActivityTrigger::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	auto Channel = [](int32 Value)
	{
		return (Value < 0) ? FString(TEXT("(unset)")) : FString::FromInt(Value);
	};
	Out.Emplace(TEXT("supernatural_level"), Channel(SupernaturalLevel));
	Out.Emplace(TEXT("criminal_level"),     Channel(CriminalLevel));
	Out.Emplace(TEXT("investigate_level"),  Channel(InvestigateLevel));
	Out.Emplace(TEXT("Releases on exit"),
		((SpawnFlags & ReleaseSpawnFlag) != 0) ? TEXT("yes (spawnflag 0x20)") : TEXT("investigate only"));
	Out.Emplace(TEXT("Occupied"), bPlayerInside ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Refreshes"), FString::FromInt(Refreshes));
}

// --- Registration ---

namespace
{
	TUniquePtr<FElysiumEntity> MakeActivityTrigger()
	{
		return MakeUnique<FElysiumActivityTrigger>();
	}

}

// A `CBaseTrigger` leaf: `Enable`/`Disable`/`Toggle`, `StartDisabled`, `filtername` and `wait` all
// arrive through the chain, and the three authored keys are its own. It has no leaf `OnTrigger`,
// output object, think, stack or reference count of its own — the two `OnTrigger` rows in
// `sm_diner_1` are an authored defect retail discards at keyvalue parse time.
static FElysiumClassRegistrar GRegTriggerPlayerActivityLevel(
	TEXT("trigger_player_activity_level"), FName(TEXT("CBaseTrigger")), &MakeActivityTrigger,
	[](FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("supernatural_level"),
			&FElysiumActivityTrigger::SupernaturalLevel);
		ElysiumAddClassField(D, TEXT("criminal_level"),
			&FElysiumActivityTrigger::CriminalLevel);
		ElysiumAddClassField(D, TEXT("investigate_level"),
			&FElysiumActivityTrigger::InvestigateLevel);
	});
