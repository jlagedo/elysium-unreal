// `CAI_BaseNPC`'s bodies of the `Dialogue` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDialogue.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- Moved from `ElysiumNpcDialogueBodies.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NavigatorPathType() const
{
	// **SEAM** for `0x102ee620`, which is `return path->+0x30` on `m_pNavigator` (`+0x5d34`) — the
	// navigator's current path TYPE. The shape map routes `+0x5d34` to
	// `FElysiumScriptedCharacter::Motor`, "the one motor seam this chain stands beside the body",
	// and that motor carries no path object at all.
	//
	// Answers `-1`, which is neither the crosswalk type `8` nor any other retail type, so both
	// bodies below take their own "not a pedestrian path" arm rather than being refused.
	return NavigatorPathTypeWord;
}

void FElysiumNpcBase::OnUseBegin(FElysiumEntity* Activator)
{
	// slot 39, 0x100a4fe0 — `CBaseEntity`'s use-begin hook, 59 bytes, shared by 82 classes in the
	// kernel family (`CAISound` is simply the class the corpus attributes the address to). Two
	// statements:
	//
	//     FireOutput(m_OnUseBegin (+0x5c), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = param_1 ? param_1->GetRefEHandle() : -1;
	//
	// The output fires FIRST and UNCONDITIONALLY — a null activator still fires it, with a null
	// activator — and the handle is written after. `OnUseBegin` is a `CBaseEntity` datamap keyfield
	// (`vtmb_fields CAISound` names `+0x5c` `m_OnUseBegin`, key `OnUseBegin`), so every `npc_*` row
	// in a shipped map may wire it.
	static const FName GOnUseBegin(TEXT("OnUseBegin"));
	FireOutput(GOnUseBegin, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = Activator != nullptr ? Activator->Handle : FElysiumEntityHandle::Invalid();
}

void FElysiumNpcBase::OnUseEnd(FElysiumEntity* Activator)
{
	// slot 42, 0x100a5030 — the other half, 33 bytes:
	//
	//     FireOutput(m_OnUseEnd (+0x74), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = -1;
	//
	// The clear is UNCONDITIONAL — it does not consult the activator at all, so ending a use someone
	// else began still clears the cache. That asymmetry with slot 39 is retail's.
	static const FName GOnUseEnd(TEXT("OnUseEnd"));
	FireOutput(GOnUseEnd, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = FElysiumEntityHandle::Invalid();
}
