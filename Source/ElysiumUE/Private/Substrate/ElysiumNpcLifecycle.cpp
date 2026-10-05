#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumScriptedSequence.h"

// Story 29c-1, family **Lifecycle** — spawn, init, precache, save/restore, dormancy and the
// per-think clocks of `order.md` layers 0–9. 59 rows. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md`.
//
// Twelve rows fill Troika-line slots and are DEFINED here with the generated signature; the rest are
// species or variant bodies of slots a later story owns, and land under the named methods
// `ElysiumNpcLifecycle.inl` declares. That file carries the family's own reading notes.

namespace
{
	// `CAI_Hint::vfunc5`'s two retail numbers. The condition the hint's destructor raises on its
	// owner (`SetCondition(owner, 0x29)`, `0x10269a20`) is above nothing this port's registrar
	// names, so it is spelled as the number — family Conditions' convention for the Werewolf's
	// `0x7a`; and the reuse delay it passes to `ClearHintNode` is a literal `0.0`, not the 5.0 s
	// every other caller passes.
	constexpr int32 GHintDestroyedCondition = 0x29;
	constexpr float GHintDestroyedReuseDelay = 0.0f;

	// `m_fEffects` bits the `KeyValue` cascade ORs in (`0x1009e430`).
	constexpr int32 GEffectNoShadow = 0x20;
	constexpr int32 GEffectNoReceiveShadow = 0x80;

}

// -------------------------------------------------------------------------------------------------
// Slots 77 / 78 / 119 — dormancy, by species.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::HintDeletingDestructor()
{
	// 0x102d2f00 -> 0x102d3040. `this` is the node's OWNER — retail resolves it out of the hint's
	// `m_hOwner` and takes the `m_pNPC` (+0x98) off it; here the owner is the receiver, so the two
	// resolves are the caller's and what is left is the pair of writes, in retail's order.
	//
	//     SetCondition(owner, 0x29);                 // 0x10269a20, FIRST
	//     CAI_BaseNPCTroika::ClearHintNode(owner, 0.0);
	//
	// The condition is raised BEFORE the reference is dropped, and the reuse delay is ZERO: the node
	// is being destroyed, so there is nothing to hold a cooldown against. Both are retail's.
	Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(GHintDestroyedCondition));
	ClearScheduleHint(GHintDestroyedReuseDelay);

	// The rest of `0x102d3040` is the global hint-list unlink (`DAT_10925450`, cursor zeroed if it
	// sat on the node) and the storage teardown. The list is `FElysiumEntityWorld::HintList` (0018/8),
	// but no hint is ever destroyed at run time — `Kill` on a hint is slot 77's hide (`0x102d08c0`)
	// — so the unlink has no live caller and is not stood; nothing kernel-observable is in that half.
}

FElysiumNpc::FCineUnhideRecord FElysiumNpc::TroikaScriptUnhideTail()
{
	FCineUnhideRecord Record;

	// 1. Slot 614 `ResetThinkTimers`, dispatched right after `CBaseEntity::ScriptUnhide`
	//    (vtable `+0x998`). ALREADY CARRIED: `FElysiumNpc::OnDormancyChanged` runs it on the waking
	//    edge and cites this exact address. Not repeated, so the stamps are not written twice.

	// 2. The active weapon's own slot 78 (`weapon->vtable+0x138`) — retail UNHIDES the weapon, it
	//    does not holster it. `GetActiveWeapon()` is called twice in the listing, once as the null
	//    test and once for the dispatch.
	if (World != nullptr && Inventory.ActiveWeapon.IsSet())
	{
		if (FElysiumEntity* Weapon = World->Resolve(Inventory.ActiveWeapon))
		{
			Weapon->ScriptUnhide();
		}
	}

	// 3. The cine hand-back: when `m_bCineScriptHidden` (`+0x5d78`) stands and `m_hCine`
	//    (`+0x5d74`) resolves, six words of THIS body's state are written onto the cine at
	//    `+0x5f78`..`+0x5f8c`, in this order — slot 94 `GetMoveType`, slot 95 `GetMoveCollide`,
	//    slot 92 `GetSolid`, slot 211 `GetSolidFlags`, `m_fEffects`, `m_bfAINPCFlags`.
	//
	// 0x102c1ec0: +0x5d78 is a separate cine latch, never the base hidden bit.
	const bool bCineLatchStands = bCineScriptHidden;
	FElysiumEntity* Cine = (World && ScriptOwner.IsSet()) ? World->Resolve(ScriptOwner) : nullptr;
	if (bCineLatchStands && Cine != nullptr)
	{
		Record.bWroteToCine = true;
		Record.MoveType = GetMoveType();
		Record.MoveCollide = RetailMoveCollide; // 0x100aacd0 represented word while shared slot95 is unfilled
		Record.Solid = RetailSolidType; // 0x10027570 represented collision word
		Record.SolidFlags = static_cast<int32>(RetailSolidFlags); // 0x100274d0 represented collision flags
		Record.Effects = static_cast<int32>(EffectsWord); // 0x102c1ec0 sixth-word handback
		Record.NpcFlagWord = NpcFlags.RawWord1();
		if (FElysiumScriptedSequence* Director = ResolveCine())
		{
			Director->SavedMoveType = Record.MoveType;
			Director->SavedMoveCollide = Record.MoveCollide;
			Director->SavedSolid = Record.Solid;
			Director->SavedSolidFlags = Record.SolidFlags;
			Director->SavedEffects = Record.Effects;
			Director->SavedTroikaFlags = static_cast<int32>(Record.NpcFlagWord);
		}
	}
	// 4. `m_bCineScriptHidden = 0` on BOTH arms — the latch is cleared whether or not a cine took
	//    the record (`102c1fxx` and the tail).
	bCineScriptHidden = false;
	//
	// 0x102c1ec0: the pre-base four getters live in ScriptUnhide above.
	return Record;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 `Spawn` — the species bodies.
// -------------------------------------------------------------------------------------------------


bool FElysiumNpc::ConversationPlaceSpawnPrecachesLast()
{
	// 0x102dbc80: `CALL 0x10001f28` (its own field init) then `JMP [[this]+0x1a0]` — `+0x1a0` is
	// slot 104. The spawn's LAST act is its precache, which is the reverse of every other class.
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 104 `Precache` — the species bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ConversationPlacePrecache(const FString& SoundLoop, const FString& SoundOnce,
	TArray<FPrecacheRequest>& Out)
{
	// 0x102dbcb0. `m_iszSoundLoop` (`+0x0454`) is warned about when it is empty and is then NOT
	// precached; `m_iszSoundOnce` (`+0x0458`) is precached unconditionally and never warned. The
	// asymmetry is retail's and is the recovered fact.
	if (SoundLoop.IsEmpty())
	{
		FPrecacheRequest Warn;
		Warn.Name = SoundLoop;
		Warn.bWarnedInvalid = true;
		Out.Add(MoveTemp(Warn));
	}
	else
	{
		Out.Add(FPrecacheRequest{ SoundLoop, /*bModel=*/false, /*bWarnedInvalid=*/false });
	}
	Out.Add(FPrecacheRequest{ SoundOnce, /*bModel=*/false, /*bWarnedInvalid=*/false });
}

// -------------------------------------------------------------------------------------------------
// Slot 110 `KeyValue` — the AI-helper-entity cascade.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 113 `Activate`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::ClassifyIsNonZero() const
{
	// SEAM for slot 138 `Classify()`. Retail's gate is `Classify() != 0`; the classes that answer 0
	// are the inert helpers that share the `CAI_BaseNPC` vtable (`CAI_Hint`, `CAI_TestHull`,
	// `CScriptedTarget`), none of which this runtime stands on this leaf. Every entity that reaches
	// `FElysiumNpc::Activate` is a living NPC, so the gate passes.
	return true;
}

void FElysiumNpc::ApplyDefaultDispositionOnActivate()
{
	// 0x1028e310, the whole Troika `Activate` past `CBaseEntity::Activate`: when `Classify()` is
	// non-zero, apply `m_sDefaultDisposition` (`+0x6558`) — or the EMPTY string when the keyfield is
	// unset, which retail substitutes explicitly — through `SetDisposition(name, 1)`.
	//
	// The level is retail's literal `1`, which is what `default_disposition starts at level 1` on
	// `FElysiumAnimating::DispositionLevel` already records.
	if (!ClassifyIsNonZero())
	{
		return;
	}
	SetDisposition(Disposition, 1);
}

TArray<FElysiumEntityHandle> FElysiumNpc::ConversationPlaceActivate(const FString& PlacesName,
	bool bEnabled, TArray<FString>* OutRejected) const
{
	// 0x102dbde0. `m_iszInterestingPlaces` (`+0x0450`) names a TARGETNAME, and retail walks every
	// entity carrying it (`FindEntityByName` from the previous hit, not from the head) — so one name
	// admits many places. A hit is admitted when it RTTI-casts to the conversation interesting-place
	// type AND its `field[0x161]` is 1; everything else is `DevWarning`ed and dropped.
	//
	// `+0x04fc` (the admitted count) is zeroed FIRST, before the walk.
	TArray<FElysiumEntityHandle> Admitted;
	if (World == nullptr || PlacesName.IsEmpty())
	{
		// Retail substitutes the empty string for an unset name and looks THAT up, which finds
		// nothing; the arm below (`m_bEnabled`) still runs, which is why this does not return early
		// past it.
		(void)bEnabled;
		return Admitted;
	}
	FElysiumEntityWorld& MutableWorld = *const_cast<FElysiumEntityWorld*>(World);
	if (FElysiumEntity* Hit = MutableWorld.FindByName(PlacesName))
	{
		// The `___RTDynamicCast` to the conversation interesting-place type: this runtime recognises
		// an interesting place by its REGISTERED CLASSNAME, `intersting_place` (retail's own
		// misspelling), because entities here carry no RTTI. The `field[0x161] == 1` term beside it
		// is the leaf's own enabled byte, which `FElysiumInterestingPlace::bEnabled` carries.
		const bool bIsPlace =
			Hit->Def != nullptr && Hit->Def->Classname == TEXT("intersting_place");
		const FElysiumInterestingPlace* Place =
			bIsPlace ? static_cast<const FElysiumInterestingPlace*>(Hit) : nullptr;
		if (Place != nullptr && Place->bEnabled)
		{
			Admitted.Add(Hit->Handle);
		}
		else if (OutRejected != nullptr)
		{
			// `DevWarning("%s %s is not a valid target for ...")`.
			OutRejected->Add(Hit->DebugString());
		}
	}
	// The tail: `m_bEnabled` either re-arms the conversation think (`thunk_FUN_102dc3e0`) or turns
	// the think off outright (`ThinkSet(NULL, 0, NULL)`). SEAM — this runtime has no conversation
	// interesting place, so there is no think to arm; the admitted list is the whole answer.
	return Admitted;
}

// -------------------------------------------------------------------------------------------------
// Slots 127 / 130 — save and restore.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 175 `Touch`, slot 180 `UpdateOnRemove`.
// -------------------------------------------------------------------------------------------------


// -------------------------------------------------------------------------------------------------
// Slot 434 `PrescheduleThink` — `CNPC_VCamera`'s empty `0x10369100` is its class's override (story 5
// step 3). `CNPC_VSabbatLeader`'s (a scope-trace wrapper forwarding to `0x10385a30`) is
// verdicted dead (0019/1) and has no port body.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// The free functions and the unnamed tables.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::TestField_0x2830() const
{
	// `FUN_100e58b0`, arm order preserved: the byte at `+0x30e9` short-circuits TRUE, and only then
	// is the int at `+0x2830` compared against 0.
	if (bField_0x30e9)
	{
		return true;
	}
	return Field_0x2830 == 0;
}

