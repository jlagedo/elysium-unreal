#include "Substrate/ElysiumStealthTrigger.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumStealthTrigger, Log, All);

namespace
{
	// The toucher's combat-character half, or null. This — not the base filter's verdict — is what
	// guards the specialized body.
	FElysiumCombatCharacter* ResolveToucher(FElysiumEntityWorld* World,
		const FElysiumEntityHandle& Activator)
	{
		FElysiumEntity* Entity = World ? World->Resolve(Activator) : nullptr;
		return Entity ? Entity->AsCombatCharacter() : nullptr;
	}
}

bool FElysiumStealthModTrigger::CanBeginTouch(const FElysiumEntityHandle& Activator) const
{
	if (bDisabled || IsInert())
	{
		// The base's own solidity test, and the only half of its admission that survives here: a
		// disabled volume has no trigger to link against, so there is no contact to credit.
		return false;
	}
	return ResolveToucher(World, Activator) != nullptr;
}

void FElysiumStealthModTrigger::OnTouchStart(const FElysiumEntityHandle& Activator)
{
	// The base callback runs FIRST and owns its own admission: `OnStartTouch`/`OnTrigger`, the
	// `wait` gate, the spawnflag and filter tests. The three authored tutorial volumes wire no
	// outputs at all, so for them this call is inert — and the contribution below still happens,
	// which is exactly the recovered order.
	FElysiumTriggerBase::OnTouchStart(Activator);

	FElysiumCombatCharacter* Toucher = ResolveToucher(World, Activator);
	if (Toucher == nullptr)
	{
		return;   // an ordinary non-character overlap; nothing failed
	}
	if (Leases.Contains(Activator))
	{
		// A second begin for a contact already credited. The touch-link system supplies one pair
		// per contact, so this is a re-link (an Enable beneath a standing character) rather than a
		// new occupancy — crediting it again would leave a contribution the matching single end
		// cannot take back.
		return;
	}
	Leases.Add(Activator);
	Toucher->StealthModRaw += StealthMod;
	UE_LOG(LogElysiumStealthTrigger, Verbose,
		TEXT("%s +%d stealth modifier onto %s (raw now %d, %d lease(s))"),
		*DebugString(), StealthMod, *Toucher->DebugString(), Toucher->StealthModRaw,
		Leases.Num());
}

void FElysiumStealthModTrigger::OnTouchEnd(const FElysiumEntityHandle& Activator)
{
	FElysiumTriggerBase::OnTouchEnd(Activator);

	if (Leases.Remove(Activator) == 0)
	{
		return;   // never credited: an end for a contact this volume does not hold
	}
	FElysiumCombatCharacter* Toucher = ResolveToucher(World, Activator);
	if (Toucher == nullptr)
	{
		// The lease was released either way: the character it named is gone, and holding the row
		// would make a re-entering entity at the same index inherit it.
		return;
	}
	// Subtract this volume's OWN contribution, never a clamp of the total. Leaving one of several
	// overlaps therefore restores exactly the remaining contribution.
	Toucher->StealthModRaw -= StealthMod;
	UE_LOG(LogElysiumStealthTrigger, Verbose,
		TEXT("%s -%d stealth modifier off %s (raw now %d, %d lease(s))"),
		*DebugString(), StealthMod, *Toucher->DebugString(), Toucher->StealthModRaw,
		Leases.Num());
}

void FElysiumStealthModTrigger::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumTriggerBase::Serialize(Ar);
	// The leases ride with the trigger because the aggregate they produced rides with the
	// characters. Saving one without the other is what would make a save taken inside a volume
	// restore with a contribution nothing can ever take back — and the leases are the half that
	// says which contributions are still owed.
	Ar << Leases;
	if (Ar.IsLoading())
	{
		// A saved handle carries a dead epoch by design (the archive drops it), so every lease goes
		// through the one rebase path. A lease whose occupant no longer resolves is dropped rather
		// than left pointing at whichever entity now holds its index — releasing a contribution to
		// a stranger is the one outcome worse than losing it.
		for (int32 i = Leases.Num() - 1; i >= 0; --i)
		{
			Leases[i] = World ? World->RebaseSavedHandle(Leases[i]) : FElysiumEntityHandle::Invalid();
			if (!Leases[i].IsSet())
			{
				Leases.RemoveAt(i);
			}
		}
	}
}

void FElysiumStealthModTrigger::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("stealth_modifier"), FString::FromInt(StealthMod));
	Out.Emplace(TEXT("Leases"), FString::FromInt(Leases.Num()));
	for (const FElysiumEntityHandle& Lease : Leases)
	{
		Out.Emplace(TEXT("  occupant"), Lease.ToString());
	}
}



namespace
{
	static TUniquePtr<FElysiumEntity> MakeStealthModTrigger()
	{
		return MakeUnique<FElysiumStealthModTrigger>();
	}

}

// A `CBaseTrigger` leaf: Enable/Disable/Toggle, `StartDisabled`, `filtername` and `wait` all arrive
// through the chain, and the six authored volumes in the corpus (three in `sp_tutorial_1`, three in
// `sp_theatre`, every one of them `stealth_modifier 2`) need nothing else.
static FElysiumClassRegistrar GRegTriggerStealthMod(
	TEXT("trigger_stealth_mod"), FName(TEXT("CBaseTrigger")), &MakeStealthModTrigger,
	[](FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("stealth_modifier"),
			&FElysiumStealthModTrigger::StealthMod);
	});
