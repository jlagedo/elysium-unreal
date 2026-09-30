// `CAI_BaseNPC`'s bodies of the `Senses` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSenses.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumMover.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcWitness.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_104563b0` = 4096.0f, compared against a SQUARED distance — so the occlusion edge trips
	// once the enemy has moved 64 Source units from where it was when sight was lost.
	constexpr float GEnemyWentOccludedDistanceSqUnits = ElysiumNpcTunables::FourThousandNinetySix;
	// `_DAT_10454110` = 5.0f and `_DAT_1044eb0c` = 20.0f — `OnDoorBlocked`'s two retry windows. The
	// 5.0 cell is also the detected-attack window.
	constexpr float GDoorRetryShortSeconds = ElysiumNpcTunables::Five;
	constexpr float GDoorRetryLongSeconds = ElysiumNpcTunables::Twenty;
	// `_DAT_104454c0` = 1.0f — the alternate-AI hit-info window `OnDoorBlocked` re-arms.
	constexpr float GDoorAlternateAiWindowSeconds = ElysiumNpcTunables::One;
	// `_DAT_10463584` = 15.0f — `SetSquadFocus`'s own expiry (`squad+0x74`); slot 616's fire-immune
	// window reads the same pooled cell.
	constexpr float GSquadFocusLifetimeSeconds = ElysiumNpcTunables::Fifteen;
	// `CBaseDoor+0x644`'s two tested bits. Their retail names are **unrecovered** — no corpus body
	// in layers 0–9 declares the word — so they are carried by value beside the body that reads
	// each. `0x10` skips the unreachable marking entirely; `0x40` shortens the retry to 5 s.
	constexpr uint32 GDoorFlagNoBlockRetry = 0x10u;
	constexpr uint32 GDoorFlagShortBlockRetry = 0x40u;
	// `m_nHintType == 0xd` — `CAI_Hint::IsViewable`'s one viewable type. The number is a literal in
	// the body and the hint-type vocabulary is not otherwise recovered, so it is carried as 13.
	constexpr int32 GViewableHintType = 13;
	// Slot 532's reason word for "the door blocked me". `npc-kernel/signatures.md` slot 532 lists
	// all four: 1 from `0x1027dd10` (fully open), 2 from here, 4 from `0x1027dfb0`, 8 from
	// `OnScheduleChange`.
	constexpr int32 GSlot532ReasonDoorBlocked = 2;
	// The two `m_eAlternateAI` modes `OnDoorBlocked` promotes to 3.
	constexpr int32 GAlternateAiModeFacing = 1;
	constexpr int32 GAlternateAiModeOpening = 2;
	constexpr int32 GAlternateAiModeBlocked = 3;
	// `DAT_109203f0` — the ONE shared `CAI_Enemies` every squad-disconnected NPC is handed instead
	// of its own. A retail GLOBAL, ported as one: a single store for the whole level, not one per
	// NPC. Nothing in this runtime writes into it; it exists so `GetEnemies()` answers a real
	// object on the disconnected arm rather than null, which is what retail answers.
	FElysiumNpcEnemyMemory GDisconnectedEnemies;
}

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 5) ---

FElysiumEntity* FElysiumNpcBase::GetEnemy() const
{
	// `0x101a67e0`, 45 bytes: resolve `m_hEnemy` (`+0x5ce0`, `FElysiumNpcMemory::Enemy`) through
	// the global entity-handle table and answer the pointer, or 0 when the serial no longer
	// matches. 126 real callers — the most-called body of this family — and the port's own
	// `World->Resolve` is that table's exact contract: index plus serial, null on a stale handle.
	if (World == nullptr)
	{
		return nullptr;
	}
	return World->Resolve(BaseMemory.Enemy);
}

FElysiumEntity* FElysiumNpcBase::GetEnemyBaseLine() const
{
	// `0x10027020`. The corpus prints a DAMAGED banner for the C; the listing is two instructions,
	// `MOV EAX,[ECX]` / `JMP [EAX+0x29c]` — slot 167, the const overload, tail-called. No work.
	return GetEnemy();
}

void* FElysiumNpcBase::GetEnemies()
{
	// `0x10273e10`, 22 bytes:
	//
	//     return (m_iSquadDisconnected < 1) ? m_pMemory (+0x5d88) : DAT_109203f0;
	//
	// 43 real callers. The test is `< 1`, not `== 0`: `DisconnectFromSquad` increments and
	// `ReconnectToSquad` decrements, so a negative count reads as connected — and it is
	// `m_iSquadDisconnected` (`+0x5bb0`, `FElysiumNpcScheduleHost::SquadDisconnected`) that decides,
	// never `m_pSquad`.
	//
	// The shape map's own note on `+0x5d88` states the redirection this answers: "squad redirection
	// replaces the ownership, not the object". Until a squad store lands, `EnemyMemory` IS the
	// connected answer.
	if (BaseScheduleHost.SquadDisconnected < 1)
	{
		return &EnemyMemory;
	}
	return &GDisconnectedEnemies;
}

void FElysiumNpcBase::RemoveMemory()
{
	// `0x10273e40`, 39 bytes:
	//
	//     if (m_pSquad (+0x5da4) == NULL && m_pMemory (+0x5d88) != NULL) { dtor(m_pMemory); free(); }
	//
	// The gate is the SQUAD pointer, not a reference count: an NPC in a squad does not own the
	// store it was handed and must not free it. `+0x5da4` is ABSENT from the shape map — this
	// substrate stands no squad object — so `ConnectedSquad()` answers null on every NPC and the
	// free always runs, which is retail's answer for a squadless NPC.
	//
	// The port's `EnemyMemory` is a member by value, not a heap allocation, so "free" is "empty".
	// `FElysiumNpcEnemyMemory` has no `Clear`; `Refresh` with no world is not the same thing, so
	// the release is spelled as a default-assignment, which is exactly what the destructor left.
	if (ConnectedSquad() != nullptr)
	{
		return;
	}
	EnemyMemory = FElysiumNpcEnemyMemory();
}

bool FElysiumNpcBase::IsHintViewable(const FHintWords& Hint)
{
	// `0x102d1320`, 24 bytes:
	//
	//     if (m_iDisabled != 0) return m_iDisabled & 0xffffff00;
	//     return m_nHintType == 0xd;
	//
	// The first return is an `int` whose LOW byte — the `bool` the caller reads in AL — is always
	// zero, whatever `m_iDisabled` holds. So a disabled hint is never viewable and the masked word
	// is a compiler artefact of returning the same register, not a value anyone sees.
	if (Hint.Disabled != 0)
	{
		return false;
	}
	return Hint.HintType == GViewableHintType;
}

bool FElysiumNpcBase::NavigatorHasNodeGraph() const
{
	// `0x102ee6a0` is `CAI_Navigator::IsGoalActive`: `nav+0x30` is the `CAI_Path` the constructor
	// `0x102eca50` builds (`0x1030bec0`), not a network, and `path+0x24` its head waypoint (0018/7
	// correction; the declaration's "node graph" reading is wrong). The port's answer is the
	// navigator's own (`NavIsGoalActive`).
	return NavIsGoalActive();
}

const FElysiumEntity* FElysiumNpcBase::SquadFocus() const
{
	// `GetSquadFocus` (`0x103166b0`): `curtime < squad+0x74` AND `squad+0x70` resolves. **SEAM**:
	// `ConnectedSquad()` is null on every NPC here.
	return nullptr;
}

void FElysiumNpcBase::SetSquadFocus(const FElysiumEntity* Focus)
{
	// `SetSquadFocus` (`0x10316660`): `squad+0x70 = handle or -1` and
	// `squad+0x74 = curtime + _DAT_10463584` (**15.0**, read out of `.rdata`). **SEAM**: counted.
	(void)Focus;
	(void)GSquadFocusLifetimeSeconds;
	++SquadFocusWrites;
}

uint32 FElysiumNpcBase::DoorBlockFlags(const FElysiumEntity& Door)
{
	// `door+0x644` (`FElysiumDoorBase::NpcFailedFlags`), tested `& 0x10` and `& 0x40` by
	// `0x100f0ec0`, whose whole body is `(*(uint*)(this + 0x644) & mask) == mask`. A non-door
	// answers 0, "a plain door" (retail never hands this a non-door: every caller has one).
	const FElysiumDoorBase* DoorBase = const_cast<FElysiumEntity&>(Door).AsDoorBase();
	return DoorBase != nullptr ? DoorBase->NpcFailedFlags : 0u;
}

void FElysiumNpcBase::SetDoorNextTryTime(FElysiumEntity& Door, double At)
{
	// `0x100f0e30`: `if (at >= door+0x640) door+0x640 = at;` — a MAX write, spelled by retail as an
	// if/else whose refusal arm assigns the field to itself (`FElysiumDoorBase::RaiseNpcFailedTimer`).
	// The count stays for the suites that assert the call.
	++DoorNextTryWrites;
	if (FElysiumDoorBase* DoorBase = Door.AsDoorBase())
	{
		DoorBase->RaiseNpcFailedTimer(At);
	}
}

void FElysiumNpcBase::HitByDoor(FElysiumEntity& Door)
{
	// `0x1027dfb0`, in its order.
	//   1. slot 158 `IsAlive` (a null door is the caller's; this takes a reference).
	if (!IsAlive())
	{
		return;
	}
	//   `(*DAT_10924a6c)->vtable+4()` -- the `ent_trace_conditions` debug read, answer discarded.
	//   2. `SetCondition(0x34)` and `m_hCondHitByDoor (+0x5d2c) = door`.
	Cognition.Conditions.Set(EElysiumNpcCond::HitByDoor);
	CondHitByDoor = Door.Handle;
	// (`FElysiumNpc::bCondHitByDoor`, the door selector's port-only latch in `ElysiumNpc.cpp`, has
	// no writer and no retail word; it is left to that selector's owner rather than latched here
	// with nothing to clear it.)
	//   3. `debug_hit_by_mode`'s gate (`DAT_1092038c`): at the shipped "1" the alternate arm,
	//      slot 532(4) (`+0x850`), is the whole rest of the body.
	if (!HitByDoorGate())
	{
		Slot532(4);
		return;
	}
	//   At 0:
	//   The door this NPC is opening goes to the door-blocked notice (5 / 20 s and the link mark).
	const FElysiumEntity* Opening = World != nullptr ? World->Resolve(OpeningDoor) : nullptr;
	if (Opening == &Door)
	{
		OnDoorBlocked(Door);                                          // 0x1027de00
		return;
	}
	//   Any other door: a flat `curtime + 5.0` (`_DAT_10454110`) MAX-stamp, the squad's focus, and
	//   `m_hBlockedDoor (+0x5d28)`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	SetDoorNextTryTime(Door, Now + static_cast<double>(GDoorRetryShortSeconds));   // 0x100f0e30
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		SetSquadFocus(&Door);                                         // unconditional here, unlike 0x1027de00
	}
	BlockedDoor = Door.Handle;
}

void FElysiumNpcBase::OnDoorBlocked(FElysiumEntity& Door)
{
	// `0x1027de00`, 336 bytes. **Not `SetEnemy`.** Five callers (0018/7 findings, correction 2):
	// `0x10298840` (the alternate-AI door transaction, when `0x100eec70` refuses the NPC/door pair),
	// `0x1027dfb0` (the hit-by-door handler, when the door that hit is the door being opened),
	// `0x102f06e0` (the look-ahead's refused door), `0x102ff960` (a stale door link still stale) and
	// `0x100f1340` (`StartBlocked`, the activator's NPC). It writes `m_hBlockedDoor` (`+0x5d28`).
	// `npc-kernel/signatures.md` slot 532 names the reason word.
	//
	//   1. `if (!IsAlive() || door == NULL) return;`            (slot 158, vtable +0x278)
	if (!IsAlive())
	{
		return;
	}
	//   2. `if (door == resolve(m_hOpeningDoor)) slot532(2);`   — the door I was opening is the one
	//      blocking me, so end the alternate-AI door wait with reason 2.
	const FElysiumEntity* Opening = World != nullptr ? World->Resolve(OpeningDoor) : nullptr;
	if (Opening == &Door)
	{
		Slot532(GSlot532ReasonDoorBlocked);
	}
	//   3. `if (!(door+0x644 & 0x10))` — the retry block, skipped entirely on that flag.
	const uint32 DoorFlags = DoorBlockFlags(Door);
	if ((DoorFlags & GDoorFlagNoBlockRetry) != GDoorFlagNoBlockRetry)
	{
		//   4. `& 0x40` picks 5.0 s (`_DAT_10454110`) over 20.0 s (`_DAT_1044eb0c`), for BOTH the
		//      link mark and the door's own stamp. The link mark (`0x102f1fa0(nav, secs, door)`) is
		//      gated on `0x102ee6a0` (`IsGoalActive`) and the stamp is not.
		const bool bShort = (DoorFlags & GDoorFlagShortBlockRetry) == GDoorFlagShortBlockRetry;
		const float Seconds = bShort ? GDoorRetryShortSeconds : GDoorRetryLongSeconds;
		if (NavigatorHasNodeGraph())
		{
			++NavigatorUnreachableMarks;
			NavMarkLinkStale(static_cast<double>(Seconds), &Door);
		}
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		SetDoorNextTryTime(Door, Now + static_cast<double>(Seconds));
	}
	//   5. `if (m_iSquadDisconnected < 1 && m_pSquad) { if (GetSquadFocus() != door)
	//      SetSquadFocus(door); }` — keep the squad looking at the obstruction, but only change it
	//      when it is not already the door. The squad object is a seam, so `ConnectedSquad()` is
	//      null and the whole arm is skipped, which is retail's answer for a squadless NPC.
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		if (SquadFocus() != &Door)
		{
			SetSquadFocus(&Door);
		}
	}
	//   6. `m_hBlockedDoor = door->GetRefEHandle();`
	BlockedDoor = Door.Handle;
	//   7. `pTroika = this+0x98; if (pTroika && (pTroika->m_eAlternateAI == 1 || == 2)) {
	//      m_eAlternateAI = 3; m_flAlternateAIExpireTimer = curtime + 1.0; }`
	//
	//      `+0x98` is `m_pBaseNPCTroika` (`npc-kernel/layout.md`), CBaseEntity's SELF-downcast
	//      cache — non-null exactly when this entity IS a `CAI_BaseNPCTroika`: `AsNpc()`. A
	//      base-only NPC takes the null arm and writes nothing.
	FElysiumNpc* const Troika = AsNpc();
	if (Troika != nullptr && (Troika->AlternateAi == GAlternateAiModeFacing
		|| Troika->AlternateAi == GAlternateAiModeOpening))
	{
		Troika->AlternateAi = GAlternateAiModeBlocked;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		Troika->AlternateAiExpireTime = Now + static_cast<double>(GDoorAlternateAiWindowSeconds);
	}
}

void FElysiumNpcBase::UpdateEnemyWentOccluded(const FElysiumEntity* Enemy, bool bHaveLos)
{
	// `0x10270180`, 205 bytes and three arms, none of which falls through to another:
	//
	//   A. `enemy == NULL` — copy the world-origin global `DAT_1070d1b0..b8` into
	//      `m_vecEnemyWentOccluded` (+0x5bc8) and store the LOS BYTE ITSELF into
	//      `m_bEnemyWentOccluded` (+0x5bc5). Retail writes `param_2` here, not 0 — a null enemy
	//      with LOS true leaves the flag SET, which is the one arm that can raise it without a
	//      distance test.
	//   B. `!bHaveLos` — snapshot the enemy's CURRENT `GetAbsOrigin()` (slot 217, vtable +0x364)
	//      into `+0x5bc8` and CLEAR the flag. This is "I have just lost sight; remember where it
	//      was".
	//   C. `bHaveLos` and the flag is already clear — if the enemy has moved more than
	//      `_DAT_104563b0` (**4096.0**, a SQUARED distance, so 64 Source units) from that snapshot,
	//      SET the flag. Nothing else is written, and a flag already set is never re-tested.
	//
	// Arm A's `DAT_1070d1b0` is `vec3_origin` — the engine's shared zero vector, which `0x10270180`
	// reads as three dwords rather than constructing.
	if (Enemy == nullptr)
	{
		BaseMemory.EnemyWentOccludedPosition = FVector::ZeroVector;
		BaseMemory.bEnemyWentOccluded = bHaveLos;
		return;
	}
	if (!bHaveLos)
	{
		BaseMemory.EnemyWentOccludedPosition = Enemy->Origin;
		BaseMemory.bEnemyWentOccluded = false;
		return;
	}
	if (BaseMemory.bEnemyWentOccluded)
	{
		return;
	}
	const float LimitCm = GEnemyWentOccludedDistanceSqUnits * ElysiumMove::U * ElysiumMove::U;
	if (FVector::DistSquared(BaseMemory.EnemyWentOccludedPosition, Enemy->Origin) > LimitCm)
	{
		BaseMemory.bEnemyWentOccluded = true;
	}
}

void FElysiumNpcBase::SetDistLook(float LookDistCm)
{
	// `0x1026a2a0`, 16 bytes: `*(m_pSenses (+0x5cdc) + 0x10) = param_1;`
	//
	// `CAI_Senses+0x10` is `m_LookDist` — `vtmb_fields CAI_Senses` declares exactly two fields and
	// this is one of them. 29c filed the inner word as unrecovered because it read the offset
	// rather than the class's own field list; there is no seam here at all, only a member that had
	// not been declared. The object is reached through `SensesObject()`, the `m_pSenses` word, which
	// every NPC-base instance carries (story 5 fold A3).
	if (FElysiumNpcSenses* const SensesPtr = SensesObject())
	{
		SensesPtr->LookDistCm = LookDistCm;
	}
}
