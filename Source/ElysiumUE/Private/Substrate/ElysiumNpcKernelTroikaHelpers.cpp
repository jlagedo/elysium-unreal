#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **TroikaHelpers** — the seams, the melee-quartet line table and the seventeen
// Troika-line slot bodies. The non-slot helpers (the hint lean, the tactical hint search, the
// ignore-collision triple, the target lead, `CAI_Motor`, `CAI_Navigator` and
// `CAI_StandoffBehavior`) are `Substrate/ElysiumNpcKernelTroikaHelpers2.cpp`. The declarations and
// this family's two standing facts are `Substrate/ElysiumNpcKernelTroikaHelpers.inl`; the walked
// prose is `docs/vtmb/npc-ai/social.md`, `conditions-and-states.md`, `schedule-kernel.md` and
// `shape.md`.
//
// Every threshold below was read out of the decompiled C of the body it names — never off 29c's
// one-line walk, which this family found wrong in four places (see the report). Where a `.rdata`
// cell's VALUE is not in the corpus the constant says UNRECOVERED and names the cell, so one edit
// closes it.

namespace
{
	// --- Retail `.rdata`, one line per constant ---------------------------------------------------

	// `0x40f00000` / `0x41700000` — slots 599 and 600 arm `m_flMeleeMustLeaveTimer` with
	// `RandomFloat(7.5, 15.0)`. **7.5, not 4.0**: `0x40f00000` is `(1 + 0.875) * 2^2`. 29c's walk
	// reads "random 4-15s" and the immediates say otherwise.
	constexpr float TroikaMeleeMustLeaveMin = 7.5f;
	constexpr float TroikaMeleeMustLeaveMax = 15.0f;

	// `0x40a00000` / `0x41200000` — slot 601 arms `m_flMeleeCanEnterTimer` with
	// `RandomFloat(5.0, 10.0)`.
	constexpr float TroikaMeleeCanEnterMin = 5.0f;
	constexpr float TroikaMeleeCanEnterMax = 10.0f;

	// `0x40000000` / `0x40200000` — slot 609's search deadline, `RandomFloat(2.0, 2.5)`.
	constexpr float TroikaShootAtHintSearchMin = 2.0f;
	constexpr float TroikaShootAtHintSearchMax = 2.5f;

	// Slot 609's default search radius when there is no active weapon: the literal `1024.0` the
	// body loads before it asks for one. With a weapon it is the weapon's `+0x8c0` max range.
	constexpr float TroikaShootAtHintDefaultRadiusUnits = 1024.0f;

	// Slot 609's hint type and search-flag byte, both literals in the body.
	constexpr int32 TroikaShootAtHintType = 8;
	constexpr uint8 TroikaShootAtHintSearchFlags = 0x10;

	// `_DAT_10463584` — slot 616's fire-immune window. **UNRECOVERED**: the cell lives past
	// `.data`'s raw size in the pinned image. `0.0` arms the timer at `curtime`, which is "already
	// expired" and therefore the arm that suppresses nothing; any positive value only widens it.
	constexpr float TroikaFireImmuneSeconds = 0.0f;

	// `_DAT_10454110` — `RecordDetectedAttack`'s cooldown stamp. **UNRECOVERED**, same reason.
	constexpr float TroikaDetectedAttackWindowSeconds = 0.0f;

	// The weapon capability bits slot 600 requires (`weapon->slot360() & 0x18000`).
	constexpr uint32 TroikaMeleeWeaponCapabilityBits = 0x18000u;

	// `m_bfNPCFrenziedFlags` bits with no name in the recovered vocabulary
	// (`ElysiumNpcFlags.h` names only `0x8`, `0x10`, `0x800` and `0x8000`). Carried by VALUE with
	// the body that reads each, which is all the evidence there is.
	constexpr uint32 TroikaFrenziedBitForcesMelee = 0x0002u;      // slots 599 and 602, the first gate
	constexpr uint32 TroikaFrenziedBitSkipsCoordinator = 0x1000u; // slot 599's coordinator bypass
	constexpr uint32 TroikaFrenziedBitBlocksOcclude = 0x0100u;    // slot 606's third gate

	// Slot 606's four RandomInt(0,99) thresholds that are NOT per-instance words: the `D_POSSESSED`
	// arm splits at 0x46 and the `FORCED_OCCLUDE` arm at 0x50.
	constexpr int32 TroikaOccludeRollPossessed = 0x46;
	constexpr int32 TroikaOccludeRollForced = 0x50;

	// Slot 606's answers, by retail number.
	constexpr int32 TroikaOccludeAnswerNone = 0;
	constexpr int32 TroikaOccludeAnswerUnreachable = 0xaa;
	constexpr int32 TroikaOccludeAnswerFrenzied = 0xb4;
	constexpr int32 TroikaOccludeAnswerShort = 0xb6;

	// Slot 607's four answers, by retail number.
	constexpr int32 TroikaFollowerAnswerNone = 0;
	constexpr int32 TroikaFollowerAnswerBackAway = 0x10c;
	constexpr int32 TroikaFollowerAnswerRunTo = 0x113;
	constexpr int32 TroikaFollowerAnswerWalkTo = 0x112;
	constexpr int32 TroikaFollowerAnswerClose = 0x115;

	// Slot 612's three network priorities.
	constexpr int32 TroikaDialogPriorityNone = 0x80;
	constexpr int32 TroikaDialogPriorityFinal = 0x280;
	constexpr int32 TroikaDialogPriorityQueued = 0x680;

	// Slot 610's four literal expression names, read out of `.rdata` at `s_Anger_105da6c0`,
	// `s_Anger_No_Deform_105da6ac`, `DAT_105da6d8` and `s_Fear_NoDeform_105da6c8`.
	const TCHAR* const TroikaExpressionAnger = TEXT("Anger");
	const TCHAR* const TroikaExpressionAngerNoDeform = TEXT("Anger_No_Deform");
	const TCHAR* const TroikaExpressionFear = TEXT("Fear");
	const TCHAR* const TroikaExpressionFearNoDeform = TEXT("Fear_NoDeform");

	// `0x3f000000` and `0x3f800000` — the two blend weights slot 610 writes to `+0x10b8`.
	constexpr float TroikaExpressionWeightAngerState = 0.5f;
	constexpr float TroikaExpressionWeightOther = 1.0f;

	// `DAT_10937cf2` — slot 334's global "a discipline is off cooldown" byte. A retail GLOBAL and
	// ported as one: one flag for the whole level, not one per NPC.
	bool GTroikaDisciplineReadyFlag = false;

	// The three global attack-coordinator pointers slot 608 walks, in retail's order:
	// `DAT_1090fbec`, `DAT_1090fbf0`, `DAT_1090fbf4`. Pointers in retail, INDICES here (29b's shape
	// map says `m_pAttackCoordinator` is "the index of the three global coordinators"). `0` is
	// retail's own null, which the walk skips, so the indices start at 1.
	constexpr int32 GTroikaAttackCoordinatorIndices[] = { 1, 2, 3 };

	// `NPC_STATE`, as this image numbers it (`ElysiumNpc.cpp`'s own mapping, repeated here because
	// it is file-static there). Retail's 8 (FLEE), 10, 11 (HUNT) and 14 have no port state, so the
	// arms of slot 610 that name them are unreachable from this enum and say so.
	int32 TroikaRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		}
		return 0;
	}
}

// -------------------------------------------------------------------------------------------------
// The attack-coordinator seams — `0x1025db50`, `0x1025db70`, `0x1025dca0`, `0x1025de90`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::MeleeCoordinatorHasRoom() const
{
	// `0x1025db50`: `coord[4] < coord[0]` — the live melee count against the coordinator's cap.
	// **SEAM**: `m_pAttackCoordinator` (+0x65e8) is an index of three globals with no object behind
	// it. False is "no room", which is the arm slot 602 reads as "leave melee".
	return false;
}

bool FElysiumNpc::MeleeCoordinatorAdmits599() const
{
	// `0x1025db70(m_pAttackCoordinator, this)` — slot 599's admission test. **SEAM**, false.
	return false;
}

bool FElysiumNpc::MeleeCoordinatorAdmits600() const
{
	// `0x1025dca0(m_pAttackCoordinator, this, false)` — slot 600's, with retail's literal third
	// argument. **SEAM**, false.
	return false;
}

bool FElysiumNpc::MeleeCoordinatorHoldsMe() const
{
	// `0x1025de90(m_pAttackCoordinator, this)` — a linear scan of the coordinator's handle array
	// that answers TRUE when this NPC is NOT in it (and true for a null argument). **SEAM**: an
	// absent coordinator holds nobody, so the honest answer is "not held" — false here, and slot
	// 602 negates it into retail's `true`.
	return false;
}

float FElysiumNpc::MeleeRangeUnits()
{
	// `DAT_10924a1c`, read as `IsCommand() ? _DAT_104454c4 (0.0) : +0x28`. **UNRECOVERED** name and
	// default — the object lives in uninitialised `.data` and no corpus function constructs it.
	// Family **Schedule** answers `0.0` for the same global (`GScheduleMeleeRangeUnits`); this
	// answers the same number so the two melee readers cannot drift.
	return 0.0f;
}

float FElysiumNpc::MeleeHeightDiffLimitUnits()
{
	// `_DAT_10451acc`. **UNRECOVERED**; family Schedule records the same cell and the same `0.0`,
	// which makes an enemy at or below this NPC's own height "level".
	return 0.0f;
}

const FElysiumEntity* FElysiumNpc::RedirectDetectedAttacker(const FElysiumEntity* Candidate) const
{
	// `0x102707d0`: an entity whose `+0x98` combat-character pointer answers `3` at vtable `+0x228`
	// is replaced by whatever its vtable `+0x184` answers; everything else passes through. Neither
	// word exists on `FElysiumEntity`. **SEAM**: the pass-through, which is retail's own fall-through
	// for every entity that is not a type-3.
	return Candidate;
}

int32 FElysiumNpc::DisciplineTableFind(int32 DisciplineId, int32 Level) const
{
	// `thunk_FUN_101e1250(&DAT_10739a4c, id, level)`. **SEAM**: no global discipline table on this
	// substrate. `INDEX_NONE` is retail's own `0xffffffff` miss, which slot 334 answers true on.
	(void)DisciplineId;
	(void)Level;
	return INDEX_NONE;
}

float FElysiumNpc::DisciplineTableCooldown(int32 RowIndex) const
{
	// `thunk_FUN_101e11c0(&DAT_10739a4c, row)` then the float at record `+0x2c`. **SEAM**, `0.0`.
	(void)RowIndex;
	return 0.0f;
}

double FElysiumNpc::DisciplineTimer(int32 RowIndex) const
{
	// `m_fDisciplineTimers[row]` (+0x146c). Below the shape map's band and with no producer in this
	// runtime. **SEAM**, `0.0` — every discipline reads as never cast.
	(void)RowIndex;
	return 0.0;
}

bool FElysiumNpc::DisciplineReadyFlag()
{
	return GTroikaDisciplineReadyFlag;
}

int32 FElysiumNpc::LookupExpressionIndex(const TCHAR* ExpressionName) const
{
	// `CBaseCombatCharacter::LookupExpressionIndex(name)`. **SEAM**: this runtime NAMES expressions
	// (family **Conditions**' `DefExpression` +0x10b4 and 29b's `NoDeformExpression` +0x64d0 are
	// both `FString`), so there is no index to answer. `INDEX_NONE` — and slot 610 stores the names
	// the lookup was asked for, which is the whole of what the index stands for here.
	(void)ExpressionName;
	return INDEX_NONE;
}

bool FElysiumNpc::DispositionExpressionRow(FString& OutExpression, FString& OutNoDeformExpression,
	float& OutBlendWeight) const
{
	// `0x100ec360` / `0x100ec2e0` / `0x100ec3d0` against `&DAT_10924980`, keyed on
	// `m_nCurrDisposition` (+0x64d4). **SEAM**: family **Anim** records the same table as
	// unreachable from the kernel. False leaves all three untouched.
	(void)OutExpression;
	(void)OutNoDeformExpression;
	(void)OutBlendWeight;
	return false;
}

FString FElysiumNpc::AttackCoordinatorNameOf(int32 CoordinatorIndex)
{
	// `thunk_FUN_1025e120(coordinator)` — the coordinator's name. **SEAM**: no coordinator object.
	// The empty string never matches a non-empty argument, so slot 608 refuses every candidate and
	// leaves `m_pAttackCoordinator` alone.
	(void)CoordinatorIndex;
	return FString();
}

const int32* FElysiumNpc::AttackCoordinatorIndices(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GTroikaAttackCoordinatorIndices);
	return GTroikaAttackCoordinatorIndices;
}

bool FElysiumNpc::EntityWord0x200(const FElysiumEntity& Entity)
{
	// `entity->+0x200`, slot 56's first gate. **UNRECOVERED**: the corpus holds no other reader
	// that pins the word. False is retail's own early return.
	(void)Entity;
	return false;
}

// -------------------------------------------------------------------------------------------------
// The melee quartet's species line — slots 599, 600, 601, 602.
// -------------------------------------------------------------------------------------------------

const TCHAR* FElysiumNpc::MeleeSlotBody(int32 Slot, EMeleeSlotLine Line)
{
	switch (Slot)
	{
	case 599: return Line == EMeleeSlotLine::AndreiBlood ? TEXT("0x10385ab0") : TEXT("0x102b5650");
	case 600: return Line == EMeleeSlotLine::AndreiBlood ? TEXT("0x10385c30") : TEXT("0x102b57c0");
	case 601: return Line == EMeleeSlotLine::AndreiBlood ? TEXT("0x10385cf0") : TEXT("0x102b5880");
	case 602: return Line == EMeleeSlotLine::AndreiBlood ? TEXT("0x10385d70") : TEXT("0x102b5900");
	default: return TEXT("");
	}
}

FElysiumNpc::EMeleeSlotLine FElysiumNpc::MeleeSlotLine(int32 Slot) const
{
	// Read off the CENSUS rather than a hand-typed class list, so the answer is checkable against
	// `docs/vtmb/npc-kernel/slots.md` by construction and cannot drift from it.
	//
	// A classname NO census class claims answers `Troika`: `RetailClass()` is null and the
	// Troika-line body is what a class with no override runs. `npc_VCop` is the recovered example
	// — the census lists no classname for `CNPC_VCop`, so a spawned cop takes this arm. That is the
	// recovered answer and not a bug.
	const FElysiumNpcClass* Cls = RetailClass();
	if (Cls == nullptr)
	{
		return EMeleeSlotLine::Troika;
	}
	const FString SlotBody(ElysiumNpcKernelClass::BodyOf(Cls, Slot));
	if (SlotBody == MeleeSlotBody(Slot, EMeleeSlotLine::Troika))
	{
		return EMeleeSlotLine::Troika;
	}
	if (SlotBody == MeleeSlotBody(Slot, EMeleeSlotLine::AndreiBlood))
	{
		return EMeleeSlotLine::AndreiBlood;
	}
	// Six classes replace each of the four slots outright (`CNPC_VFrenzyShadow`, `CNPC_VGargoyle`,
	// `CNPC_VHengeyokai`, `CNPC_VMingXiao`, `CNPC_VMingXiaoTentacle`, `CNPC_VTzimisceHeadClaw`,
	// `CNPC_VTzimisceRunner`, `CNPC_VYukie`). Their bodies are other families' rows and are NOT
	// guessed at here: this leaf runs the Troika line for them and the caller can see it did.
	return EMeleeSlotLine::Species;
}

// -------------------------------------------------------------------------------------------------
// Slot 54 — `CAI_BaseNPCTroika::FUN_102b50b0` `0x102b50b0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot54(FElysiumEntity* Other)
{
	// `0x102b50b0`, arm by arm:
	//     if (other != NULL) {
	//         if (other == GetEnemy() && m_pSchedule != NULL)          // vtable +0x29c, +0x5c38
	//             if (!ConditionInterruptsCurrentSchedule(COND_LOST_ENEMY 0x47))  // 0x10269c70
	//                 return false;
	//     }
	//     return true;
	//
	// `0x10269c70` is NOT `HasCondition`: it asks whether the RUNNING PROGRAM'S MASK lists the
	// condition, and answers false outright with no program installed. The port already carries it
	// as `ElysiumSchedule::MaskHasCondition`, which is called rather than re-stated.
	if (Other != nullptr && World != nullptr)
	{
		const FElysiumEntity* Enemy = World->Resolve(Senses.Memory.Enemy);
		if (Enemy == Other && Schedule.Current != ElysiumScheduleId::None)
		{
			if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::LostEnemy))
			{
				return false;
			}
		}
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 56 — `CAI_BaseNPCTroika::FUN_102b5120` `0x102b5120`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot56(FElysiumEntity* Other, FVector, FVector, const TCHAR*)
{
	// `0x102b5120`. The three trailing arguments are read by nothing in the body, exactly as here.
	//     if (other == NULL) return;
	//     if (other->+0x200 == 0) return;
	//     if (other == m_hLastEnemy resolved) SetLastEnemy(NULL);   // 0x10279b70 -> +0x1a94 = -1
	if (Other == nullptr || World == nullptr)
	{
		return;
	}
	if (!EntityWord0x200(*Other))
	{
		return;
	}
	if (World->Resolve(Senses.Memory.LastEnemy) == Other)
	{
		Senses.Memory.LastEnemy = FElysiumEntityHandle();
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 322 — `CAI_BaseNPCTroika::FUN_102a0910` `0x102a0910`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot322(FElysiumEntity* Attacker)
{
	// `0x102a0910`, the whole body, in order:
	//     thunk_FUN_102bf5d0(this, attacker);      // family Squad's `AlertNearbyAlly`
	//     this->vtable[+0x960](attacker);          // slot 600
	//
	// `+0x960 / 4` is 600, so the attacker-side melee provocation ASKS THE MELEE COORDINATOR
	// through the same slot the schedule kernel does. Both halves are calls, not copies.
	AlertNearbyAlly(Attacker);
	Slot600(Attacker);
}

// -------------------------------------------------------------------------------------------------
// Slot 334 — `CAI_BaseNPC::FUN_10330020` `0x10330020`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot334(int32 DisciplineId, int32 Level)
{
	// `0x10330020`, arm by arm:
	//     if (m_iCurFrenzyCount > 0) return false;          // +0x0ec0, the low byte of the count
	//     DAT_10937cf2 = 0;
	//     row = DisciplineTableFind(&DAT_10739a4c, id, level);      // 0x101e1250
	//     if (row != -1) {
	//         elapsed  = curtime - m_fDisciplineTimers[row];        // +0x146c
	//         cooldown = record[+0x2c];
	//         if (cooldown > elapsed) { DAT_10937cf2 = 1; return false; }
	//     }
	//     return true;
	//
	// **29c's walk has the flag and the answer inverted.** The listing sets the global on the arm
	// that is STILL COOLING and answers false there; the elapsed-cooldown arm answers true and
	// leaves the global clear. The first arm's answer is `m_iCurFrenzyCount & 0xffffff00`, whose low
	// byte is zero — false — and not the count.
	if (CurFrenzyCount > 0)
	{
		return false;
	}
	GTroikaDisciplineReadyFlag = false;
	const int32 Row = DisciplineTableFind(DisciplineId, Level);
	if (Row != INDEX_NONE)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		const double Elapsed = Now - DisciplineTimer(Row);
		if (static_cast<double>(DisciplineTableCooldown(Row)) > Elapsed)
		{
			GTroikaDisciplineReadyFlag = true;
			return false;
		}
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 597 — `CAI_BaseNPCTroika::FUN_102b4fb0` `0x102b4fb0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot597(FElysiumEntity* Other, int32 Priority)
{
	// `0x102b4fb0`, the whole body:
	//     CBaseCombatCharacter::AddEntityRelationship(this, other, 1, priority);
	//
	// The literal `1` is Source's `Disposition_t` `D_HT`, which this runtime spells
	// `EElysiumRelationship::Hate`. It is the constant that makes this more than a bare forward:
	// slot 597 is "hate this entity at the caller's priority" and nothing else.
	//
	// Story 29d, family **SpeciesMisc10**: `CNPC_VCop#597` (`0x10372cc0`) ADDS the `m_hPursuitPlayer`
	// latch and the `"Player D_HT 10"` relationship write IN FRONT of this body, which then runs
	// unchanged — so the prologue goes here and nothing below it moves.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 597);
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x10372cc0")) == 0)
	{
		CopSlot597Prologue(Other);
	}
	if (Other == nullptr)
	{
		return;
	}
	Relationships.SetEntity(Other->Handle, EElysiumRelationship::Hate, Priority);
}

// -------------------------------------------------------------------------------------------------
// Slot 599 — `CAI_BaseNPCTroika::FUN_102b5650` `0x102b5650` / `CNPC_VAndreiBlood::FUN_10385ab0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot599(int32)
{
	// **The vtable dispatch first.** Four classes replace this slot outright — `CNPC_VFrenzyShadow`
	// `0x10376b70`, `CNPC_VGargoyle` `0x10379ef0`, `CNPC_VTzimisceHeadClaw` `0x103c19e0` and
	// `CNPC_VTzimisceRunner` `0x103c3960`, family **Species**' rows — and `MeleeSlotLine(599)` names
	// them. A `RetailClass()` the census does not claim falls through to this body, which is
	// `npc_VCop`'s recovered answer and not a hole.
	//
	// The argument: `signatures.md` types slot 599 `bool vfunc599(int)` because THIS body reads it
	// with no instruction, but the runner's copy casts it to a `CBaseEntity*` and caches
	// `m_hPotentialEnemy` from it. Every recovered dispatch site pushes `GetEnemy()` —
	// `0x102b6c30` `CALL [EAX+0x29c]` / `MOV EDI,EAX` / `PUSH EDI` / `CALL [EDX+0x95c]`, and
	// `0x103c4430` `CALL [EAX+0x29c]` / `PUSH EAX` / `CALL [EDX+0x95c]` — so this NPC's own enemy IS
	// retail's argument and is what the species arm is handed. `+0x29c` is slot **167**, the CONST
	// overload (`0x101a67e0`, a plain `m_hEnemy` resolve), not the Troika line's mutable 168 with
	// its last-enemy fallback — so the const one is the one called here.
	bool SpeciesAnswer = false;
	if (SpeciesSlot599(static_cast<const FElysiumNpc*>(this)->GetEnemy(), SpeciesAnswer))
	{
		return SpeciesAnswer;
	}

	// `0x102b5650`. The `CNPC_VAndreiBlood`-line copy `0x10385ab0` is BYTE-IDENTICAL (family Bosses
	// read it and this family re-read it), so one arm carries both lines and `MeleeSlotLine(599)`
	// is asserted rather than branched on. The argument is read by nothing in the body.
	//
	//     if ((m_bfNPCFrenziedFlags & 2) == 2 || GetFollowerBoss()) { m_bInMelee = 1; return true; }
	//     if (curtime < m_flMeleeCanEnterTimer) { m_bInMelee = 0; return false; }
	//     bool bRangeOk = true;
	//     if (2 * MeleeRange < m_flEnemyDist && HasUsableRangedWeapon()) bRangeOk = false;
	//     if ((m_flEnemyHeightDiff <= _DAT_10451acc || !HasCondition(ENEMY_UNREACHABLE 0x59))
	//         && bRangeOk
	//         && ((m_bfNPCFrenziedFlags & 0x1000) == 0x1000 || Coordinator599(coord, this)))
	//     {
	//         m_bInMelee = 1;
	//         m_flMeleeMustLeaveTimer = curtime + RandomFloat(7.5, 15.0);
	//         (*DAT_10924edc)->vfunc1();
	//         return true;
	//     }
	//     m_bInMelee = 0;
	//     return false;
	if (NpcFlags.HasFrenzied(TroikaFrenziedBitForcesMelee) || GetFollowerBoss() != nullptr)
	{
		bInMelee = true;
		return true;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < MeleeCanEnterTimer)
	{
		bInMelee = false;
		return false;
	}

	// The range term. Retail's `fVar4 + fVar4` is the melee range DOUBLED, and the whole arm only
	// closes the gate when the NPC also has a usable ranged weapon to fall back on — slot 308,
	// still a generated stub answering false, so the gate is open today.
	bool bRangeOk = true;
	if (2.0f * MeleeRangeUnits() < ScheduleHost.EnemyDistUnits && HasUsableRangedWeapon())
	{
		bRangeOk = false;
	}

	// The height term. Retail's flag word is `less || equal`, i.e. `heightDiff <= limit`, and the
	// `ENEMY_UNREACHABLE` test is only reached when the enemy is ABOVE the limit.
	const bool bHeightOk = ScheduleHost.EnemyHeightDiffUnits <= MeleeHeightDiffLimitUnits()
		|| !Cognition.Conditions.Has(EElysiumNpcCond::EnemyUnreachable);

	const bool bCoordinatorOk = NpcFlags.HasFrenzied(TroikaFrenziedBitSkipsCoordinator)
		|| MeleeCoordinatorAdmits599();

	if (bHeightOk && bRangeOk && bCoordinatorOk)
	{
		bInMelee = true;
		MeleeMustLeaveTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(TroikaMeleeMustLeaveMin, TroikaMeleeMustLeaveMax));
		// The global melee event, `(*DAT_10924edc)->vfunc1()`. Family **Bosses** already counts this
		// exact global through `MeleeEventFires`; the same counter is incremented here rather than a
		// second one stood beside it. Retail fires it on BOTH entering (599, 600) and leaving (601),
		// so it is one event object and not two.
		++MeleeEventFires;
		return true;
	}
	bInMelee = false;
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 600 — `FUN_102b57c0` `0x102b57c0` / `FUN_10385c30`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot600(FElysiumEntity* Enemy)
{
	// The vtable dispatch first: five classes replace this slot (family **Species**' `0x10376ba0`,
	// `0x10379f20`, `0x103c1a60`, `0x103c39e0` and `CNPC_VYukie`'s `0x103dd900`, which is not a
	// melee-entry body at all). The argument is retail's own and is passed straight through — the
	// runner's copy caches `m_hPotentialEnemy` from it.
	bool SpeciesAnswer = false;
	if (SpeciesSlot600(Enemy, SpeciesAnswer))
	{
		return SpeciesAnswer;
	}

	// `0x102b57c0`. `0x10385c30` is BYTE-IDENTICAL, verified against the decompiled C of both, so
	// one arm carries both lines. The argument is read by nothing in the body.
	//
	//     flags = GetActiveWeapon() ? weapon->slot360() : 0;          // +0x5a0
	//     if ((flags & 0x18000) != 0 && m_bInMelee == 0) {
	//         if (Coordinator600(coord, this, false)) {
	//             m_bInMelee = 1;
	//             m_flMeleeMustLeaveTimer = curtime + RandomFloat(7.5, 15.0);
	//             (*DAT_10924edc)->vfunc1();
	//             return true;
	//         }
	//         m_bInMelee = 0;
	//     }
	//     return false;
	//
	// An NPC ALREADY in melee falls straight out with false and writes nothing — the `m_bInMelee`
	// test is on the way IN, not a re-entry guard around the write.
	const FElysiumEntity* Weapon = (World != nullptr && Inventory.ActiveWeapon.IsSet())
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	// `ActiveWeaponCapabilityWord()` is family **Motor**'s read of the same weapon vtable `+0x5a0`
	// (slot 360, retail body `0x1014f930`); it is a SEAM answering 0, so the gate is closed today.
	const uint32 Capability = Weapon != nullptr ? ActiveWeaponCapabilityWord() : 0u;
	if ((Capability & TroikaMeleeWeaponCapabilityBits) != 0 && !bInMelee)
	{
		if (MeleeCoordinatorAdmits600())
		{
			bInMelee = true;
			const double Now = World != nullptr ? World->NowSeconds() : 0.0;
			MeleeMustLeaveTimer = Now + static_cast<double>(
				ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
					.FRandRange(TroikaMeleeMustLeaveMin, TroikaMeleeMustLeaveMax));
			++MeleeEventFires;
			return true;
		}
		bInMelee = false;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 601 — `FUN_102b5880` `0x102b5880` / `FUN_10385cf0` (family Bosses').
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot601(FElysiumEntity* Enemy)
{
	// The vtable dispatch first: `CNPC_VTzimisceHeadClaw` `0x103c1ad0` and `CNPC_VTzimisceRunner`
	// `0x103c3a70` replace this slot (family **Species**), both dropping the ranged-weapon test and
	// the release's null guard. Retail's argument at every recovered site is `GetEnemy()`
	// (`0x102b6c30` pushes the same EDI into `+0x964` as into `+0x95c`); it arrives here already.
	if (SpeciesSlot601(Enemy))
	{
		return;
	}

	// `0x102b5880`, the Troika line, in retail's order and with the argument ignored:
	//     (*DAT_10924edc)->vfunc1();                                   // the global melee event
	//     m_bInMelee = 0;
	//     if (HasUsableRangedWeapon())                                 // slot 308 (+0x4d0)
	//         m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0, 10.0);
	//     if (m_pAttackCoordinator != 0) ReleaseMeleeSlot(coord, this);  // 0x1025ddd0, GUARDED
	//
	// **The ONE difference from the `CNPC_VAndreiBlood` line (`0x10385cf0`) is the null guard on
	// the last line.** Family Bosses' note that its copy "fires the event FIRST" is not a
	// difference — the Troika body fires it first too; this family re-read both bodies and the
	// guard is the whole of it. `FUN_10385cf0` is Bosses' method and is CALLED here rather than
	// restated, so the two lines cannot drift.
	if (MeleeSlotLine(601) == EMeleeSlotLine::AndreiBlood)
	{
		FUN_10385cf0();
		return;
	}
	++MeleeEventFires;
	bInMelee = false;
	if (HasUsableRangedWeapon())
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeCanEnterTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(TroikaMeleeCanEnterMin, TroikaMeleeCanEnterMax));
	}
	if (AttackCoordinator != 0)
	{
		++MeleeCoordinatorReleases;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 602 — `FUN_102b5900` `0x102b5900` / `FUN_10385d70`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot602()
{
	// The vtable dispatch first: `CNPC_VTzimisceHeadClaw` `0x103c1b10` and `CNPC_VTzimisceRunner`
	// `0x103c3ab0` (family **Species**) keep only the far arm of the body below.
	bool SpeciesAnswer = false;
	if (SpeciesSlot602(SpeciesAnswer))
	{
		return SpeciesAnswer;
	}

	// `0x102b5900`, the Troika line:
	//     if ((m_bfNPCFrenziedFlags & 2) == 2) return false;
	//     if (GetFollowerBoss()) return false;
	//     if (m_pAttackCoordinator == 0) return false;              // <-- TROIKA ONLY
	//     if (!HasUsableRangedWeapon()) {                           // slot 308 (+0x4d0)
	//         if (2 * MeleeRange <= m_flEnemyDist && !CoordinatorHasRoom(coord)) return true;
	//     } else if (m_flMeleeMustLeaveTimer <= curtime) {
	//         return true;
	//     }
	//     return CoordinatorDoesNotHoldMe(coord, this);             // 0x1025de90
	//
	// **`0x10385d70`, the `CNPC_VAndreiBlood` line (40 classes), DROPS the third line** and nothing
	// else. Family Bosses flagged the divergence and left the slot undefined; this family read both
	// bodies and confirms it: the two are otherwise instruction-for-instruction the same.
	//
	// ONE method carries both, per the story's species rule — this is one behaviour with a
	// per-species arm, not two behaviours — and the arm is a CENSUS lookup (`MeleeSlotLine`) so it
	// is checkable against `slots.md`.
	//
	// **The divergence is NOT observable in retail, and the reachability argument is why.** Every
	// one of the coordinator's five entry points dereferences its `this` immediately —
	// `0x1025db50` reads `coord[4]` and `coord[0]`, `0x1025db70` / `0x1025dca0` / `0x1025de90` read
	// `coord+0x04` and `coord+0x10` — so a null `m_pAttackCoordinator` FAULTS in all of them. The
	// only producers of `m_bInMelee` are slots 599 and 600, and both reach the coordinator before
	// they set it, except slot 599's first arm — whose two conditions (`m_bfNPCFrenziedFlags & 2`
	// and a live `GetFollowerBoss()`) are exactly the two gates this body refuses on. So a body that
	// reaches the third line at all already has a coordinator, and the Troika line's test is dead
	// defensive code that the `CNPC_VAndreiBlood` copy simply did not carry over.
	//
	// **NAMED DIVERGENCE**: this substrate has no coordinator object, so `m_pAttackCoordinator` is
	// 0 on every NPC — a state retail cannot be in without having already crashed. The guard is
	// therefore applied on BOTH lines here. A crash is not a behaviour the port reproduces (family
	// **Squad**'s `SetFollowerBossName` took the same refusal for the same reason), and answering
	// the seams' "an empty coordinator holds nobody" on a state retail cannot reach would hand
	// family **Schedule**'s six melee arms a leave-melee decision that no shipped program ever saw.
	// The recovered difference itself is recorded, and `MeleeSlotLine(602)` is what states it.
	//
	// Retail's `(a < b) != (a == b)` on the distance is the FPU flag pair for `a <= b`.
	if (NpcFlags.HasFrenzied(TroikaFrenziedBitForcesMelee))
	{
		return false;
	}
	if (GetFollowerBoss() != nullptr)
	{
		return false;
	}
	if (AttackCoordinator == 0)
	{
		// The Troika line's own third test; on the `CNPC_VAndreiBlood` line it is the named
		// divergence above, and `MeleeSlotLine(602)` says which of the two this NPC took.
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!HasUsableRangedWeapon())
	{
		if (2.0f * MeleeRangeUnits() <= ScheduleHost.EnemyDistUnits && !MeleeCoordinatorHasRoom())
		{
			return true;
		}
	}
	else if (MeleeMustLeaveTimer <= Now)
	{
		return true;
	}
	return !MeleeCoordinatorHoldsMe();
}

// -------------------------------------------------------------------------------------------------
// Slot 606 — `FUN_102b8320` `0x102b8320`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::Slot606(int32 Arg)
{
	// The vtable dispatch first: `CNPC_VBach` `0x10364280` (family **Species**) wraps this body in an
	// arm-then-fire hysteresis around `COND_ENEMY_OCCLUDED` and DELEGATES here on its second pass —
	// `thunk_FUN_102b8320`, a direct call, which `SpeciesSlot606`'s dispatch scope reproduces.
	int32 SpeciesAnswer = 0;
	if (SpeciesSlot606(Arg, SpeciesAnswer))
	{
		return SpeciesAnswer;
	}

	// `0x102b8320`, in retail's order. Each fail arm stamps the selector trace at
	// `+0x1b30`/`+0x1b34`; the shape map calls that pair ABSENT and families Anim, Bosses and Damage
	// all record the same, so the line numbers are named in the comments and not stored.
	//
	//     if (!HasCondition(ENEMY_OCCLUDED 0x48))          return 0;
	//     if ( HasCondition(ENEMY_UNREACHABLE 0x59))       return 0xaa;   // line 0x5e1b
	//     if (m_bfNPCFrenziedFlags & 0x100)                return 0xb4;   // line 0x5e20
	//     if (m_bfAINPCFlags2 & D_POSSESSED 0x40000)
	//         return RandomInt(0,99) < 0x46 ? 0xb6 : 0xb4;                // lines 0x5e28 / 0x5e2c
	//     if (m_bfAINPCFlags & FORCED_OCCLUDE 0x10000000) {
	//         m_bfAINPCFlags &= ~FORCED_OCCLUDE;
	//         return RandomInt(0,99) < 0x50 ? 0xb6 : 0xb4;                // lines 0x5e37 / 0x5e3b
	//     }
	//     roll = RandomInt(0,99);
	//     <the two threshold tables, selected by COND_SQUAD_SEE_ENEMY 0x31>
	//
	// The argument is read by nothing in the body. The four thresholds are the authored per-NPC
	// words `m_iPercentOccludedWait` / `Cover` / `Walk` / `Flank` (`+0x6420`..`+0x642c`);
	// `m_iPercentOccludedChase` (`+0x6430`) is NOT read by this body, which is why its last bucket
	// is a constant on both tables.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return TroikaOccludeAnswerNone;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		return TroikaOccludeAnswerUnreachable;
	}
	if (NpcFlags.HasFrenzied(TroikaFrenziedBitBlocksOcclude))
	{
		return TroikaOccludeAnswerFrenzied;
	}
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	if (NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED))
	{
		return Rng.RandRange(0, 99) < TroikaOccludeRollPossessed ? TroikaOccludeAnswerShort
															: TroikaOccludeAnswerFrenzied;
	}
	if (NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE))
	{
		NpcFlags.Clear(EElysiumNpcFlag::FORCED_OCCLUDE);
		return Rng.RandRange(0, 99) < TroikaOccludeRollForced ? TroikaOccludeAnswerShort
															   : TroikaOccludeAnswerFrenzied;
	}
	const int32 Roll = Rng.RandRange(0, 99);
	if (Cognition.Conditions.Has(EElysiumNpcCond::SquadSeeEnemy))
	{
		// lines 0x5e46 / 0x5e4a / 0x5e4e / 0x5e52 / 0x5e56. The last two answers are the SAME
		// number, which is retail's and is reproduced rather than folded.
		if (Roll < PercentOccludedWait)  { return 0xab; }
		if (Roll < PercentOccludedCover) { return 0xb0; }
		if (Roll < PercentOccludedWalk)  { return 0xb3; }
		if (Roll < PercentOccludedFlank) { return 0xb2; }
		return 0xb2;
	}
	// lines 0x5e65 / 0x5e69 / 0x5e6d / 0x5e71 / 0x5e75.
	if (Roll < PercentOccludedWait)  { return TroikaOccludeAnswerUnreachable; }
	if (Roll < PercentOccludedCover) { return 0xaf; }
	if (Roll < PercentOccludedWalk)  { return 0xb5; }
	if (Roll < PercentOccludedFlank) { return TroikaOccludeAnswerShort; }
	return TroikaOccludeAnswerFrenzied;
}

// -------------------------------------------------------------------------------------------------
// Slot 607 — `FUN_102b93c0` `0x102b93c0`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::Slot607()
{
	// `0x102b93c0`, the follower-distance ladder. Distances are SOURCE units, as the three
	// `m_flFollowerDistance*` words (`+0x6484` back away, `+0x6488` walk to, `+0x648c` run to) are;
	// this runtime's origins are centimetres, so the delta is converted once.
	//
	//     boss = m_hFollowerBoss;                                    // +0x647c
	//     if (!boss resolves) return 0;
	//     d2 = |boss->GetAbsOrigin() - GetAbsOrigin()|^2;
	//     if (d2 < BackAway^2) {
	//         if (FacingTargetsEnabled()) AddFacingTarget(boss, bossOrigin, 1.0, 1.0, 0);  // +0x814
	//         m_vSavePosition = bossOrigin;                          // +0x5dd0
	//         return 0x10c;                                          // line 0x6090
	//     }
	//     if (RunTo^2  < d2) { SetTarget(boss); return 0x113; }      // line 0x6097
	//     if (WalkTo^2 < d2) { SetTarget(boss); return 0x112; }      // line 0x609d
	//     SetTarget(boss); return 0x115;                             // line 0x60a2
	//
	// Note the ORDER of the three thresholds: back-away first, then RUN, then WALK. The ladder
	// reads outward-in on the near side and inward-out on the far side, which is retail's.
	if (World == nullptr || !FollowerBoss.IsSet())
	{
		return TroikaFollowerAnswerNone;
	}
	FElysiumEntity* Boss = World->Resolve(FollowerBoss);
	if (Boss == nullptr)
	{
		return TroikaFollowerAnswerNone;
	}
	const FVector BossOriginUnits = Boss->Origin / ElysiumMove::U;
	const FVector MyOriginUnits = Origin / ElysiumMove::U;
	const double DistSq = FVector::DistSquared(BossOriginUnits, MyOriginUnits);

	if (DistSq < static_cast<double>(FollowerDistanceBackAway) * FollowerDistanceBackAway)
	{
		// `this->vtable[+0x814](boss, savePos, 1.0, 1.0, 0)` — slot 517, the queued facing target.
		// Gated on the same cvar (`DAT_10924f74`) family **Facing** carries as `FacingTargetsEnabled`,
		// read the same way (`!IsCommand() && +0x2c != 0`); its seam answers false, so the request
		// is not made, which is retail's own arm for a cleared cvar.
		if (FacingTargetsEnabled())
		{
			FFacingTargetRequest Request;
			// `MotorSlot` records which of `CAI_Motor`'s three queued-facing overloads was reached;
			// this call comes in through the NPC's own slot 517 rather than through one of them, so
			// it is left at 0 and named here instead of being attributed to an overload.
			Request.MotorSlot = 0;
			Request.Target = Boss->Handle;
			Request.Position = BossOriginUnits;
			Request.Duration = 1.0f;
			Request.Ramp = 1.0f;
			Request.Tolerance = 0.f;
			MotorAddFacingTarget(Request);
		}
		// `m_vSavePosition` is CENTIMETRES in this runtime — `FaceSavePosition` and
		// `StepAwayFromSavePosition` both compare it against `Origin` directly — so the boss's own
		// origin lands rather than the unit form the distance ladder above is measured in.
		SavePosition = Boss->Origin;
		return TroikaFollowerAnswerBackAway;
	}
	if (static_cast<double>(FollowerDistanceRunTo) * FollowerDistanceRunTo < DistSq)
	{
		SetTarget(FollowerBoss);
		return TroikaFollowerAnswerRunTo;
	}
	if (static_cast<double>(FollowerDistanceWalkTo) * FollowerDistanceWalkTo < DistSq)
	{
		SetTarget(FollowerBoss);
		return TroikaFollowerAnswerWalkTo;
	}
	SetTarget(FollowerBoss);
	return TroikaFollowerAnswerClose;
}

// -------------------------------------------------------------------------------------------------
// Slot 608 — `FUN_102c48b0` `0x102c48b0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot608(const TCHAR* Name)
{
	// `0x102c48b0`. The body is an inlined two-bytes-at-a-time `strcmp` run against up to three
	// global coordinator pointers in order, and the first match wins:
	//     if (name == NULL || name[0] == 0) return false;
	//     for (g in { DAT_1090fbec, DAT_1090fbf0, DAT_1090fbf4 }) {
	//         if (g == 0) continue;
	//         if (strcmp(name, CoordinatorName(g)) == 0) {
	//             m_pAttackCoordinator = g;                                     // +0x65e8
	//             const char* n = CoordinatorName(g);
	//             m_sAttackCoordinatorName = n[0] ? n : NULL;                   // +0x65ec
	//             return true;
	//         }
	//     }
	//     return false;
	//
	// The `n[0] ? n : NULL` on the name write is retail's: a coordinator with an EMPTY name is
	// matched by an empty argument — which the first guard already refused — so in practice the
	// stored name is never empty, and the conditional is reproduced rather than dropped.
	if (Name == nullptr || Name[0] == TEXT('\0'))
	{
		return false;
	}
	int32 Count = 0;
	const int32* Indices = AttackCoordinatorIndices(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Coordinator = Indices[Index];
		if (Coordinator == 0)
		{
			continue;
		}
		const FString Candidate = AttackCoordinatorNameOf(Coordinator);
		if (Candidate == Name)
		{
			AttackCoordinator = Coordinator;
			AttackCoordinatorName = Candidate.IsEmpty() ? FString() : Candidate;
			return true;
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 609 — `FUN_102b6b50` `0x102b6b50`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::FindShootAtHintNode(bool bForce)
{
	// `0x102b6b50`, arm by arm:
	//     if (m_pShootAtHint != NULL && FValidateHintType(m_pShootAtHint))     // +0x6444, slot 566
	//         return m_pShootAtHint;
	//     if (!bForce && curtime < m_flNextShootAtHintSearchTime) return NULL; // +0x6440
	//     m_flNextShootAtHintSearchTime = curtime + RandomFloat(2.0, 2.5);
	//     radius = GetActiveWeapon() ? weapon->+0x8c0 : 1024.0;
	//     return FindHintOfType(this, 8, 0x10, radius, NULL, NULL);            // 0x102d2980
	//
	// The draw is taken BEFORE the search and on every call that gets past the wait, whether or not
	// the search finds anything — so a miss still costs a full 2.0-2.5 s cooldown.
	if (ScheduleHost.ShootAtHintNode != 0 && FValidateHintTypeForSpecies(ScheduleHost.ShootAtHintNode))
	{
		return ScheduleHost.ShootAtHintNode;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!bForce && Now < ScheduleHost.NextShootAtHintSearchTime)
	{
		return INDEX_NONE;
	}
	ScheduleHost.NextShootAtHintSearchTime = Now + static_cast<double>(
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(TroikaShootAtHintSearchMin, TroikaShootAtHintSearchMax));

	// The weapon's `+0x8c0` max range. **SEAM**: `FElysiumWeapon` stands no such word (family Motor
	// records the same gap for `+0x5a0` beside it), so the default 1024.0 is what a body with an
	// active weapon gets too, and that is stated rather than hidden.
	const float RadiusUnits = TroikaShootAtHintDefaultRadiusUnits;

	// `0x102d2980` is the six-argument form of the hint search family **Hints** carries as
	// `FindHintNear` (`0x102d1af0`) over the SAME absent global hint list. It is called rather than
	// a second seam stood beside it; the two out-parameters retail passes are both NULL here.
	return FindHintNear(TroikaShootAtHintType, TroikaShootAtHintSearchFlags, RadiusUnits);
}

void* FElysiumNpc::Slot609(bool bForce)
{
	// The vtable dispatch first, and slot 609's species arm is a GATE rather than a replacement:
	// `CNPC_VBach` `0x103661f0`, `CNPC_VBatSwarm` `0x10367740` and `CNPC_VSheriffSwarm` `0x103b26f0`
	// (family **Species**, three byte-identical bodies) admit only retail `m_NPCState` 4 or 0xc and
	// otherwise ZERO `m_pShootAtHintNode` and answer NULL. An admitted body tail-calls the base
	// (`thunk_FUN_102b6b50`), which is the search below — so the dispatcher hands back "may the base
	// run" and the refusal is the only arm that returns early.
	bool bRunBase = false;
	if (SpeciesSlot609(bForce, bRunBase) && !bRunBase)
	{
		return nullptr;
	}

	// Retail's `CAI_Hint* vfunc609(bool)`. Hints are BARE INDICES in this runtime (family Hints'
	// standing fact), so there is no object to answer with: the body is `FindShootAtHintNode`,
	// which answers the index, and the slot answers null. Nothing is lost — every recovered caller
	// stores the result into a hint-node word, which is an index here.
	FindShootAtHintNode(bForce);
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// Slot 610 — `FUN_102adfe0` `0x102adfe0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot610(EElysiumNpcState NewState)
{
	// `0x102adfe0`, the whole switch on `NPC_STATE`:
	//     case 2:                 Anger / Anger_No_Deform, weight 1.0
	//     case 3, 0xb, 0xe:       Anger / Anger_No_Deform, weight 0.5
	//     case 8, 10:             Fear  / Fear_NoDeform,   weight 1.0
	//     default:                the disposition table's own row, its own weight
	//
	// **29c's walk folds case 2 into the 3/0xb/0xe arm.** The listing does not: case 2 falls through
	// to the SHARED tail, which writes weight `0x3f800000` (1.0), while 3/0xb/0xe writes
	// `0x3f000000` (0.5) and returns before it. Both use the same two names; the weight is the
	// difference, and it is reproduced.
	//
	// The writes are `m_idxDefExpression` (`+0x10b4`, family **Conditions**' `DefExpression`),
	// `m_idxNoDeformExpression` (`+0x64d0`, 29b's `NoDeformExpression`) and the blend weight
	// (`+0x10b8`). Both index words are NAMES in this runtime, so the resolve is asked (the seam) and
	// the NAME is what lands.
	//
	// Retail states 8 (FLEE), 10, 0xb (HUNT) and 0xe have no `EElysiumNpcState`, so their arms are
	// unreachable from this enum today. They are written out rather than dropped: the day the mind
	// carries those states the arms are already here.
	const int32 RetailState = TroikaRetailNpcState(NewState);
	switch (RetailState)
	{
	case 2:
		DefExpression = TroikaExpressionAnger;
		NoDeformExpression = TroikaExpressionAngerNoDeform;
		ExpressionBlendWeight = TroikaExpressionWeightOther;
		LookupExpressionIndex(TroikaExpressionAnger);
		LookupExpressionIndex(TroikaExpressionAngerNoDeform);
		return;
	case 3:
	case 0xb:
	case 0xe:
		DefExpression = TroikaExpressionAnger;
		NoDeformExpression = TroikaExpressionAngerNoDeform;
		ExpressionBlendWeight = TroikaExpressionWeightAngerState;
		LookupExpressionIndex(TroikaExpressionAnger);
		LookupExpressionIndex(TroikaExpressionAngerNoDeform);
		return;
	case 8:
	case 10:
		DefExpression = TroikaExpressionFear;
		NoDeformExpression = TroikaExpressionFearNoDeform;
		ExpressionBlendWeight = TroikaExpressionWeightOther;
		LookupExpressionIndex(TroikaExpressionFear);
		LookupExpressionIndex(TroikaExpressionFearNoDeform);
		return;
	default:
		break;
	}
	// The default arm: three reads of the disposition table keyed on `m_nCurrDisposition`
	// (`+0x64d4`). The seam answers false, and retail's own answer for a table it cannot key is the
	// table's DEFAULT row — which this substrate does not carry either, so the three words are left
	// exactly as they were. Stated, not guessed.
	FString Expression;
	FString NoDeform;
	float Weight = 0.f;
	if (DispositionExpressionRow(Expression, NoDeform, Weight))
	{
		DefExpression = Expression;
		NoDeformExpression = NoDeform;
		ExpressionBlendWeight = Weight;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 611 — `FUN_102c12a0` `0x102c12a0`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::Slot611()
{
	// `0x102c12a0` — the disposition stance selector, and **the port already carries it**.
	//
	//     rec = DispositionRecord(this, &delay, &changeChance, &altChance);   // 0x100ecee0
	//     if (m_bIsTalking) { seq = rec.A[stance]; m_bFidget = 0; }           // +0x64c0
	//     else if (m_bFidget || m_bTransition) { seq = rec.A[stance]; m_bFidget = 0; }
	//     else if (rec.A[stance] != rec.B[stance] && RandomInt(1,100) < altChance) {
	//         seq = rec.B[stance]; m_bFidget = 1;                             // and NOT settled
	//     } else if (delay < curtime - m_flStanceTime && RandomInt(1,100) < changeChance) {
	//         seq = ChangeStance(); m_bFidget = 0; m_bTransition = 1; return seq;
	//     } else { seq = rec.A[stance]; m_bFidget = 0; }
	//     m_bTransition = 0;
	//     return seq == -1 ? m_nSequence : seq;                               // +0x6f0
	//
	// `ElysiumStance::Select` (`Public/ElysiumStanceTypes.h`, `Substrate/ElysiumStance.cpp`) IS that
	// body arm for arm: the talking settle, the latch settle, the `Fidget[n] != Idle[n]` availability
	// test (retail's `rec.A[n] != rec.B[n]`), the fidget roll, the `Now - LastChangeTime > threshold`
	// floor and the change roll, in that order — over NAMES rather than sequence indices, which is
	// why `m_CurrStance` (+0x64c8), `m_flStanceTime` (+0x64e4) and the two latches (+0x64e0/+0x64e1)
	// are the same four words. So this slot CALLS it rather than standing a second selector beside
	// it, and the decision, the rolls and the latch writes are the landed ones.
	//
	// What cannot be answered is the RETURN: retail answers a studio sequence index and this runtime
	// answers clip NAMES. Retail's own fallback for an unresolved sequence is `m_nSequence`
	// (`+0x6f0`, family **Anim**'s `SequenceNumber`), and that is what lands — it is retail's value
	// for exactly the case this runtime is always in.
	if (!EnsureStanceResolved())
	{
		// No stance set on this body at all: retail's table lookup would have taken the default row.
		// This runtime's monsters and one-off models idle off `ACT_IDLE` instead, which the callers
		// already handle, and the answer is the playing sequence.
		return SequenceNumber;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	ElysiumStance::Select(StanceClips, StanceTuning, Stance, IsTalking(Now), Now,
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
	return SequenceNumber;
}

// -------------------------------------------------------------------------------------------------
// Slot 612 — `FUN_102c04b0` `0x102c04b0`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::Slot612()
{
	// `0x102c04b0`, the whole body:
	//     if (!m_hDialogPartner resolves)  return 0x80;               // +0x0fe8
	//     if (m_bDialogQueIsFinal)         return 0x280;              // +0x654c
	//     return 0x680;
	//
	// The retail name is **unrecovered**; the three answers are network priorities (0x80 / 0x280 /
	// 0x680), so a body in a conversation is raised and a body with a QUEUED — not final — line is
	// raised further still. `HasLiveDialogPartner` is family **Anim**'s read of the same `+0x0fe8`
	// resolve and is called rather than restated.
	if (!HasLiveDialogPartner())
	{
		return TroikaDialogPriorityNone;
	}
	return Dialogue.bDialogQueIsFinal ? TroikaDialogPriorityFinal : TroikaDialogPriorityQueued;
}

// -------------------------------------------------------------------------------------------------
// Slot 616 — `FUN_102ad110` `0x102ad110`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot616()
{
	// `0x102ad110`, the whole body:
	//     ClearCondition(COND_ON_FIRE 0x30);                          // 0x10269b50
	//     m_flNextBurnTime = curtime + _DAT_10463584;                 // +0x65bc
	//
	// The companion setter to slot 615 `CanBeSetOnFire` (`0x102ad0c0`, family **Damage**'s row):
	// this is what a body runs when the fire goes out, and the stamp is the immunity window before
	// it can catch again. `_DAT_10463584` is **UNRECOVERED** — see `TroikaFireImmuneSeconds`.
	Cognition.Conditions.Clear(EElysiumNpcCond::OnFire);
	NextBurnTime = (World != nullptr ? World->NowSeconds() : 0.0)
		+ static_cast<double>(TroikaFireImmuneSeconds);
}
