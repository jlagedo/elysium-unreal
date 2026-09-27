#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"   // story 29d: `RestoreExtendedHeader`'s `IRestore` is this archive
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

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

	// `CCineNPC::Spawn` (`0x101a6f10`): the two spawnflags it reads and the two compiled DOUBLES it
	// adds to `curtime` — the auto-remove think delay and the named cine's start-time offset. Both
	// stood at 0.0 as unrecovered until the cells were read (2026-09-21, held by the tunables table
	// since 0019/4).
	constexpr int32 GCineSpawnFlagAutoRemove = 0x10;
	constexpr int32 GCineSpawnFlagNotInterruptable = 0x20;
	constexpr double GCineAutoRemoveDelaySeconds = ElysiumNpcTunables::OneDouble;
	constexpr double GCineStartTimeOffsetSeconds = ElysiumNpcTunables::CineStartTimeOffset;

}

// -------------------------------------------------------------------------------------------------
// Slots 412/413/414 — the three think stamps this runtime already carries.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::LastUpdateThink() const
{
	// 0x101aa6d0 — the Troika's update-think stamp (+0x6254). The base body `0x101a6440` would
	// answer `CBaseEntity::GetLastThink(NULL)` instead.
	return static_cast<float>(ScheduleHost.LastUpdate);
}

float FElysiumNpc::LastNormalThink() const
{
	// 0x101aa6f0 — the normal channel (+0x6258).
	return static_cast<float>(ScheduleHost.LastNormal);
}

float FElysiumNpc::LastMoveThink() const
{
	// 0x101aa710 — the move channel (+0x625c).
	return static_cast<float>(ScheduleHost.LastMove);
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

	// SEAM: the rest of `0x102d3040` is the engine's global hint-list unlink and the node's own
	// storage teardown. This substrate carries hint nodes as rows, not as engine objects on a list,
	// so there is no list here and no kernel-observable state in that half.
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
	//    `+0x5d78` is its OWN byte (story 29e rebound it as `FElysiumNpc::bCineScriptHidden`, which
	//    `NPCInit` clears at `102735xx` and this body clears on both arms), distinct from
	//    `m_bScriptHidden` (`+0x0f4`) and from `m_fEffects`. Its SETTER — retail's `ScriptHide` —
	//    is not ported yet, so the latch would never stand; until it is, the condition is read from
	//    the entity's own hidden flag, which is this runtime's only live spelling of "hidden by a
	//    script". The beat (`FElysiumScriptedSequence`) carries no `+0x5f78` block, so the record is
	//    RETURNED rather than written.
	const bool bCineLatchStands = bHidden;
	FElysiumEntity* Cine = (World && ScriptOwner.IsSet()) ? World->Resolve(ScriptOwner) : nullptr;
	if (bCineLatchStands && Cine != nullptr)
	{
		Record.bWroteToCine = true;
		Record.MoveType = GetMoveType();
		Record.MoveCollide = GetMoveCollide();
		Record.Solid = GetSolid();
		Record.SolidFlags = GetSolidFlags();
		// `m_fEffects` and `m_bfAINPCFlags` both stay 0 here: this runtime carries no `m_fEffects`
		// word at all (rendering effects are the body's, not the entity's), and `FElysiumNpcFlags`
		// keeps its two raw words private — no ported reader wants the whole word, only named bits.
		// Recorded as the two words retail writes so the day a consumer exists it fills them.
		Record.Effects = 0;
		Record.NpcFlagWord = 0;
	}
	// 4. `m_bCineScriptHidden = 0` on BOTH arms — the latch is cleared whether or not a cine took
	//    the record (`102c1fxx` and the tail).
	bCineScriptHidden = false;
	//
	// **Unrecovered:** the four vtable dispatches ahead of `CBaseEntity::ScriptUnhide` in the
	// listing (the same slots 94/95/92/211, results discarded by the decompiler). They are the same
	// sequence in the same order as the recorded one, so either the compiler hoisted the reads or
	// retail restores them from the cine first; nothing in the corpus settles which, and this body
	// reproduces only the sequence whose destination is stated.
	return Record;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 `Spawn` — the species bodies.
// -------------------------------------------------------------------------------------------------

FElysiumNpc::FCineSpawnState FElysiumNpc::CineSpawn(int32 InSpawnFlags, bool bNamed, double Now)
{
	FCineSpawnState State;
	// 0x101a6f10, in retail's own order.
	State.Solid = 0;                 // SetSolid(SOLID_NONE)
	State.AddedSolidFlags = 4;       // AddSolidFlags(flags | 4) — FSOLID_NOT_SOLID
	State.bTargetable = false;       // m_bIsBCCTargetable = 0
	State.bAlive = false;            // m_bIsAlive = 0
	// The auto-remove think: an UNNAMED cine, or spawnflag 0x10 on a named one.
	if (!bNamed || (InSpawnFlags & GCineSpawnFlagAutoRemove) != 0)
	{
		State.bAutoRemoveThink = true;
		State.NextThink = Now + GCineAutoRemoveDelaySeconds;
		// Only a NAMED cine that armed the think also takes a start time.
		if (bNamed)
		{
			State.bHasStartTime = true;
			State.StartTime = Now + GCineStartTimeOffsetSeconds;
		}
	}
	// Spawnflag 0x20 CLEARS interruptable; its absence sets it.
	State.bInterruptable = (InSpawnFlags & GCineSpawnFlagNotInterruptable) == 0;
	State.SequenceStarted = 0;
	State.bNextCineSet = false;      // m_hNextCine = 0xffffffff
	State.AddedFlags2 = 0x10;        // AddFlag2(0x10)
	return State;
}

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

bool FElysiumNpc::CineTouch(FElysiumEntity* Other)
{
	// 0x101a75a0, shared by `CCineAI`, `CCineAISchedule` and `CCineNPC`: `return;`, parameter
	// ignored, no chain to the base. Nothing happens when something touches a cine actor, and
	// nothing else gets a chance to.
	(void)Other;
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 434 `PrescheduleThink` — `CNPC_VCamera`'s empty `0x10369100` is its class's override (story 5
// step 3). `CNPC_VSabbatLeader`'s `0x103a7650` forwards to `0x10385a30`, which has no port body yet.
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

uint64 FElysiumNpc::SearchTimerElapsedCycles()
{
	return NpcKernelLifecycleShared::GSearchTimerCycles;
}

