#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelDamage2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Damage**, second half — the per-species death, throw and emitter bodies.
// The Troika-line slot bodies, the family seams and its four standing facts are
// `Substrate/ElysiumNpcKernelDamage.cpp` and `Substrate/ElysiumNpcKernelDamage.inl`; the walked
// prose for everything below is `docs/vtmb/npc-ai/lifecycle.md`.
//
// Split at ~1,500 lines, as family Bosses once split off its second half. The constants below
// are the ones this half uses, re-stated rather than shared because neither file owns the other's
// anonymous namespace; every one carries the `.rdata` address it was read from.

namespace
{
	constexpr float EnergyBallForward = 50.0f;      // _DAT_104ada24
	constexpr float EnergyBallRight = 40.0f;        // _DAT_104ada28
	constexpr float EnergyBallUp = -10.0f;          // _DAT_104ada2c

}

// =================================================================================================
// `0x1036dd20` — `CNPC_VChangBros::SpawnEnergyBall`.
// =================================================================================================

FVector FElysiumNpc::EnergyBallSpawnPoint(const FVector& OriginUnits, const FVector& FwdAxis,
	const FVector& RightAxis, const FVector& UpAxis)
{
	// The listing's three accumulations, each a dot of one basis row with the same constant triple:
	//     p.x = origin.x + fwd.x*50 + up.x*40 + right.x*(-10)
	//     p.y = origin.y + fwd.y*50 + up.y*40 + right.y*(-10)
	//     p.z = origin.z + fwd.z*50 + up.z*40 + right.z*(-10)
	// `_DAT_104ada24` = 50.0, `_DAT_104ada28` = 40.0, `_DAT_104ada2c` = -10.0, all read out of
	// `.rdata`. `AngleVectors` (`0x10139610`) fills forward/right/up from the muzzle attachment's
	// angles (slot 219, `+0x36c`).
	return OriginUnits + FwdAxis * EnergyBallForward + UpAxis * EnergyBallRight
		+ RightAxis * EnergyBallUp;
}

// =================================================================================================
// The `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` line's four damage-and-death slot arms.
// =================================================================================================

const FElysiumNpc::FTookLifeSpecies* FElysiumNpc::TookLifeSpeciesRows(int32& OutCount)
{
	// `vtmb_slot 300`: three classes share `0x103a4950`.
	static const FTookLifeSpecies Rows[] = {
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x103a4950") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103a4950") },
		{ TEXT("CNPC_VWolfMorph"), TEXT("0x103a4950") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FTookLifeSpecies* FElysiumNpc::TookLifeSpeciesOf(const TCHAR* InRetailClass)
{
	int32 Count = 0;
	const FTookLifeSpecies* Rows = TookLifeSpeciesRows(Count);
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

int32 FElysiumNpc::OnTakeDamageSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376ae0, thirty-two bytes: forward the packet to the controller object's slot 0x238 and
	// ALWAYS answer 0 — a `CNPC_VFrenzyShadow` never reports damage taken, whatever the object does.
	(void)Info;
	(void)HasPlayerControllerObject();
	return 0;
}

int32 FElysiumNpc::OnTakeDamage_AliveSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376b10, thirty-six bytes: forward through the controller object's own `+0x9c` to slot
	// 0x618 and ALWAYS answer 0. Note it is the SUB-object's slot, one indirection deeper than
	// `OnTakeDamage`'s.
	(void)Info;
	(void)HasPlayerControllerObject();
	return 0;
}

void FElysiumNpc::Event_KilledSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376b50 is THREE BYTES — an empty body that ignores its parameter. `CNPC_VFrenzyShadow`'s
	// death handling is fully suppressed versus `CAI_BaseNPC::Event_Killed`: no ideal-state change,
	// no corpse, no outputs. Reproduced as written.
	(void)Info;
}

void FElysiumNpc::Event_TookLifeSpecies(const FElysiumEntity* Victim)
{
	// 0x103a4950. Guarded on BOTH the controller object (`+0x184`) and its AI component (`+0xa8`);
	// only inside both does it build the victim's debug name (`GetDebugName`, which itself tolerates
	// a null victim) and dispatch `thunk_FUN_1017e150(component, 4, -1.0, source)`.
	if (!HasPlayerControllerObject())
	{
		return;
	}
	FControllerAiEvent Event;
	Event.EventType = 4;
	Event.Priority = -1.0f;
	Event.Source = TookLifeEventSource();
	Event.VictimName = Victim != nullptr ? Victim->DebugString() : FString();
	ControllerAiEvents.Add(Event);
}
