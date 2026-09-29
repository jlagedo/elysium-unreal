// `CAI_BaseNPC`'s bodies of the `Misc` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMisc.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `m_bfAINPCFlags2` (+0x14bc) bit `0x1000` — `TEST AH,0x10` at `0x1028a213`. See the note in
	// `OkToDisturb` below: this IS a reader of `MADE_OBLIVIOUS`, which `ElysiumNpcFlags.h` records
	// as having none.
	constexpr EElysiumNpcFlag2 GMiscObliviousBit = EElysiumNpcFlag2::MADE_OBLIVIOUS;
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 5) ---

// -------------------------------------------------------------------------------------------------
// Slot 513 `CapabilitiesGet` — `0x1026db30`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::CapabilitiesGet() const
{
	// `0x1026db30`. Strip the scope-trace push/pop that brackets the body — retail's named debug
	// stack, which this runtime logs through its own channels — and the whole of it is:
	//     uint caps = m_afCapability;                             // +0x5cec
	//     if (GetActiveWeapon()) caps |= GetActiveWeapon()->vtable[+0x5a0]();   // slot 360
	//     return caps;
	//
	// Retail calls `GetActiveWeapon` TWICE, once for the null test and once for the dispatch; the
	// second call cannot answer differently, so one resolve carries both here.
	//
	// `ActiveWeaponCapabilityWord()` is family **Motor**'s seam for slot 360 (`0x1014f930`) and
	// answers 0 — there is no capability word on `FElysiumWeapon` — so today this is
	// `m_afCapability` verbatim, which is exactly what family Motor's own comment at `0x10278c60`
	// already assumed. Asking the seam rather than assuming it is what makes that assumption true.
	int32 Capabilities = CapabilityWord;
	const FElysiumEntity* Weapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	if (Weapon != nullptr)
	{
		Capabilities |= static_cast<int32>(ActiveWeaponCapabilityWord());
	}
	return Capabilities;
}

bool FElysiumNpcBase::OkToDisturb() const
{
	// `0x1028a190`, walked off the LISTING because the decompilation is damaged (`Could not recover
	// jumptable at 0x1028a21d`). The listing is linear and the whole body is:
	//
	//     1028a195  CALL [EAX+0x740]        ; slot 464 GetState()
	//     1028a19b  CMP EAX,0x4             ; NPC_STATE_SCRIPT
	//     1028a1a0  MOV EAX,[ESI+0x5d74]    ; m_hCine
	//     ... resolve it, and if it resolves:
	//     1028a1fa  CALL 0x1000c6da         ; -> 0x101a8930 CCineNPC::CanInterrupt, ON THE CINE
	//     1028a201  JZ  0x1028a223          ; false -> return false
	//     1028a203  MOV AL,[ESI+0x5bc4]     ; m_bInChoreoScene -> non-zero returns false
	//     1028a20d  MOV EAX,[ESI+0x14bc]
	//     1028a213  TEST AH,0x10            ; m_bfAINPCFlags2 & 0x1000 -> set returns false
	//     1028a21d  JMP [EDX+0x278]         ; tail-call slot 158 IsAlive()
	//
	// **`+0x14bc & 0x1000` is `MADE_OBLIVIOUS`, and this is a reader of it.** `ElysiumNpcFlags.h`
	// records that bit as having "ZERO readers anywhere in retail"; that is wrong, and this address
	// is the counter-example. The header is another story's file and is left alone; the correction
	// is recorded here and in `docs/vtmb/npc-ai/conditions-and-states.md`.
	//
	// The cine arm asks `CanInterrupt` (`0x101a8930`) ON THE CINE — the director's own
	// `m_interruptable` and its target's `IsAlive` (story 5 fold A3). A live owner that is not a
	// director (the port's choreographed scene also stands in `ScriptOwner`) has no such body; it
	// refuses, which is what the arm answered before the fold.
	//
	// Retail name unrecovered.
	if (Mind.State() == EElysiumNpcState::Scripted && ScriptOwner.IsSet()
		&& World != nullptr && World->Resolve(ScriptOwner) != nullptr)
	{
		const FElysiumScriptedSequence* Cine = ResolveCine();
		if (Cine == nullptr || !Cine->CanInterrupt())
		{
			return false;
		}
	}
	if (bInChoreoScene)
	{
		return false;
	}
	if (NpcFlags.Has(GMiscObliviousBit))
	{
		return false;
	}
	return const_cast<FElysiumNpcBase*>(this)->IsAlive();
}
