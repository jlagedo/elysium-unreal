// Story 0019/8 (29e under the strict verdict), family **Boss19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBoss19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (Boss19's `rule` rows): 0x102b52a0 FUN_102b52a0, 0x102c51a0 DoPossession, 0x102c5310
// DoFrenzy, 0x103830e0 FUN_103830e0, 0x10395c70 FUN_10395c70, 0x1039e970 FUN_1039e970, 0x10397410
// FUN_10397410, 0x10397e90 FUN_10397e90, 0x10397f00 FUN_10397f00, 0x10395750 FUN_10395750.
//
// Story 8, lane L12. The three Troika-line rows (`0x102b52a0`, `0x102c51a0`, `0x102c5310`) are here;
// the species rows are on their classes (`ElysiumNpcMingXiao.cpp`, `ElysiumNpcMingXiaoTentacle.cpp`,
// `ElysiumNpcHengeyokai.cpp`, `ElysiumNpcBoss19Species.cpp`). Arms carry the instruction address
// they came from (`vtmb_asm`); walked prose in `docs/vtmb/npc-ai/story8/Boss19.md`. The
// `+0x1b3c`/`+0x1b40` ideal-state trace stamps are the mind's transition trace
// (`RequestIdealStateRetail`, retail source line carried).

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelBoss19
{
	// `m_afMemory &= 0xf7fc7fff` (`0x102b52b3`..`0x102b52c5`): clears `0x8000` HAD_ENEMY, `0x10000`
	// HAD_PLAYER, `0x20000` and `0x8000000`.
	constexpr uint32 GBoss19MemoryKeepMask = 0xf7fc7fffu;
	// `D_HT`, the disposition slot 404 answers for a hated entity.
	constexpr int32 GBoss19DispositionHate = 1;
	// `CBaseCombatCharacter::AddMiscFlag(0x800)` — the misc flag both disciplines raise when the
	// current enemy or the caster is hated.
	constexpr uint32 GBoss19MiscFlagHatedCaster = 0x800u;
	// `m_bfAINPCFlags2 |= 0x80840000` (D_POSSESSED | D_DISCONNECT_SQUAD | bit 31) and `0x80820000`
	// (D_INSANE | D_DISCONNECT_SQUAD | bit 31). Bit 31 is the word's unnamed bit (`ElysiumNpcFlags.h`).
	constexpr uint32 GBoss19PossessionFlags2 = 0x80840000u;
	constexpr uint32 GBoss19FrenzyFlags2 = 0x80820000u;
	// The whole `m_bfNPCFrenziedFlags` each discipline writes.
	constexpr uint32 GBoss19PossessionFrenziedWord = 0x3b1cu;
	constexpr uint32 GBoss19FrenzyFrenziedWord = 0x9fbdu;
	// The relationship line both disciplines apply when the caster is the player (`0x106014c8`).
	const TCHAR* const GBoss19PlayerLikeLine = TEXT("player D_LI 99");
	// `SetFollowerType("Combat")` (`0x105b4738`).
	const TCHAR* const GBoss19FollowerTypeCombat = TEXT("Combat");
	// The retail source file of the three stamps, and their lines.
	const TCHAR* const GBoss19TroikaFile = TEXT("E:\\Vampire\\main\\dlls\\AI_BaseNPCTroika.cpp");
	constexpr int32 GBoss19ResetAiStateReasonLine = 0x5626;
	constexpr int32 GBoss19ResetAiStateIdealLine = 0x562a;
	constexpr int32 GBoss19PossessionIdealLine = 0x6e42;
	constexpr int32 GBoss19FrenzyIdealLine = 0x6e7f;
	// `NPC_STATE_IDLE` and `NPC_STATE_HUNT` (retail numbers, `SetState` takes them raw).
	constexpr int32 GBoss19StateIdle = 1;
	constexpr int32 GBoss19StateHunt = 0xb;
	// `m_eInvestigateMode` / `m_eInvestigateModeCombat` = 6 under frenzy.
	constexpr int32 GBoss19FrenzyInvestigateMode = 6;

	bool Boss19IsPlayer(const FElysiumEntityWorld* World, const FElysiumEntity& Caster)
	{
		// `caster->m_pPlayer` (`+0xa8`) non-null — the entity IS the player. Family Squad's
		// `SetFollowerBossName` tests the same word the same way.
		return World != nullptr && Caster.Handle == World->PlayerHandle();
	}
}

// -------------------------------------------------------------------------------------------------
// 0x102b52a0 ResetAiState
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ResetAiState(bool bReapplyRelationships, bool bSetIdleIdeal)
{
	using namespace NpcKernelBoss19;
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());      // 0x102b52a5 0x10279a50
	BaseMemory.LastEnemy = FElysiumEntityHandle::Invalid();                 // 0x102b52ae 0x10279b70 +0x1a94
	BaseScheduleHost.MemoryBits &= GBoss19MemoryKeepMask;                   // 0x102b52c5 +0x5d8c
	if (bReapplyRelationships)                                              // 0x102b52cb
	{
		// `0x10273760`: `InputSetRelationship(this, m_RelationshipString ?: "", 0)`.
		const FString Line = AuthoredRelationshipString();
		if (!Line.IsEmpty())
		{
			// The port's `InputSetRelationship` warns on an empty line, which retail's parser passes
			// silently; an empty line changes nothing on either side, so it is not dispatched.
			FElysiumInputArgs Args;
			Args.Param = FElysiumVariant::String(Line);
			InputSetRelationship(Args);                                     // 0x102b52cf
		}
	}
	// `UTIL_VarArgs("%s(%d) :", __FILE__, 0x5626)` (`0x101d3730`), handed to the enemy store's
	// clear-all (`0x102dfc10`) on slot 541 `GetEnemies()`.
	const FString Reason = FString::Printf(TEXT("%s(%d) :"), GBoss19TroikaFile,
		GBoss19ResetAiStateReasonLine);                                     // 0x102b52e3
	ClearEnemyMemoryStore(Reason);                                          // 0x102b52f0 / 0x102b52f8
	if (bSetIdleIdeal)                                                      // 0x102b5303
	{
		// `m_IdealNPCState = NPC_STATE_IDLE` DIRECTLY, with the trace stamp — not `SetState`, so no
		// `OnStateChange` runs and `m_NPCState` is left alone.
		RequestIdealStateRetail(GBoss19StateIdle, GBoss19ResetAiStateIdealLine);   // 0x102b5305..0x102b5319
	}
}

FString FElysiumNpc::AuthoredRelationshipString() const
{
	// SEAM; see the declaration. `FString` keys hash and compare case-insensitively, as the def's
	// keyvalue names are authored in any case.
	if (Def == nullptr)
	{
		return FString();
	}
	const FString* Line = Def->Keys.Find(TEXT("Relationship"));
	return Line != nullptr ? *Line : FString();
}

void FElysiumNpc::ClearEnemyMemoryStore(const FString& Reason)
{
	// SEAM; see the declaration. `FreeKnowledgeDuration` is `CAI_Memory`'s own tuning word, not a
	// record, and the clear-all leaves it.
	EnemyStoreClearedRecords = EnemyMemory.Num();
	EnemyStoreClearReason = Reason;
	const double FreeKnowledgeDuration = EnemyMemory.FreeKnowledgeDuration;
	EnemyMemory = FElysiumNpcEnemyMemory();
	EnemyMemory.FreeKnowledgeDuration = FreeKnowledgeDuration;
}

// -------------------------------------------------------------------------------------------------
// 0x102c51a0 DoPossession
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DoPossession(FElysiumEntity* Caster)
{
	using namespace NpcKernelBoss19;
	if (Caster == nullptr)                                                  // 0x102c51aa
	{
		return;
	}
	if (static_cast<const FElysiumNpcBase*>(this)->GetEnemy() != nullptr)  // 0x102c51b2 / 0x102c51ba slot 167
	{
		// Slot 167 asked again for the argument (`0x102c51c1`), then slot 404 `IRelationType`.
		if (IRelationType(static_cast<const FElysiumNpcBase*>(this)->GetEnemy())
			== GBoss19DispositionHate)                                      // 0x102c51ca / 0x102c51d4
		{
			ElysiumMiscFlags::Set(MiscFlags, GBoss19MiscFlagHatedCaster);   // 0x102c51dd
		}
	}
	if (IRelationType(Caster) == GBoss19DispositionHate)                    // 0x102c51e7 / 0x102c51f0
	{
		ElysiumMiscFlags::Set(MiscFlags, GBoss19MiscFlagHatedCaster);       // 0x102c51f9
	}
	DisconnectFromSquad();                                                  // 0x102c5200 0x1026d050
	ResetAiState(true, true);                                               // 0x102c520b 0x102b52a0
	GiveBaseFightingItems();                                                // 0x102c5214 slot 304
	NpcFlags.SetRawWord2Bits(GBoss19PossessionFlags2);                      // 0x102c5226 +0x14bc
	if (Boss19IsPlayer(World, *Caster))                                     // 0x102c5234
	{
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(GBoss19PlayerLikeLine);
		InputSetRelationship(Args);                                         // 0x102c523f 0x10273790
	}
	SetFollowerBossName(static_cast<const FElysiumEntity*>(Caster));        // 0x102c5247 0x102c4470
	SetFollowerType(GBoss19FollowerTypeCombat);                             // 0x102c5253 0x102c4640
	RequestIdealStateRetail(GBoss19StateIdle, GBoss19PossessionIdealLine);  // 0x102c525c..0x102c5270
	SetState(GBoss19StateIdle);                                             // 0x102c527a 0x1026e340
	SetTarget(Caster->Handle);                                              // 0x102c5282 0x10279cc0
	FriendPlayer = Caster->Handle;                                          // 0x102c528b / 0x102c5292 +0x60ac
	ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);         // 0x102c529a slot 614
	SetFrenziedWord(GBoss19PossessionFrenziedWord);                         // 0x102c52a4 +0x5b84
	AcquireNearestHatedTarget();                                            // 0x102c52ae slot 595
}

// -------------------------------------------------------------------------------------------------
// 0x102c5310 DoFrenzy
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DoFrenzy(FElysiumEntity* Caster)
{
	using namespace NpcKernelBoss19;
	if (Caster == nullptr)                                                  // 0x102c531a
	{
		return;
	}
	if (static_cast<const FElysiumNpcBase*>(this)->GetEnemy() != nullptr)  // 0x102c5322 / 0x102c532a slot 167
	{
		if (IRelationType(static_cast<const FElysiumNpcBase*>(this)->GetEnemy())
			== GBoss19DispositionHate)                                      // 0x102c5331 / 0x102c533a / 0x102c5344
		{
			ElysiumMiscFlags::Set(MiscFlags, GBoss19MiscFlagHatedCaster);   // 0x102c534d
		}
	}
	if (IRelationType(Caster) == GBoss19DispositionHate)                    // 0x102c5357 / 0x102c5360
	{
		ElysiumMiscFlags::Set(MiscFlags, GBoss19MiscFlagHatedCaster);       // 0x102c5369
	}
	DisconnectFromSquad();                                                  // 0x102c5370 0x1026d050
	ResetAiState(true, true);                                               // 0x102c537b 0x102b52a0
	// Unlike `DoPossession`, the hated-target pick runs HERE, before the loadout.
	AcquireNearestHatedTarget();                                            // 0x102c5384 slot 595
	GiveBaseFightingItems();                                                // 0x102c538e slot 304
	NpcFlags.SetRawWord2Bits(GBoss19FrenzyFlags2);                          // 0x102c53a0 +0x14bc
	if (Boss19IsPlayer(World, *Caster))                                     // 0x102c53ae
	{
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(GBoss19PlayerLikeLine);
		InputSetRelationship(Args);                                         // 0x102c53b9 0x10273790
	}
	RequestIdealStateRetail(GBoss19StateHunt, GBoss19FrenzyIdealLine);      // 0x102c53c2..0x102c53d6
	SetState(GBoss19StateHunt);                                             // 0x102c53e0 0x1026e340
	InvestigateMode = GBoss19FrenzyInvestigateMode;                         // 0x102c53ec +0x6338
	InvestigateModeCombat = GBoss19FrenzyInvestigateMode;                   // 0x102c53f2 +0x633c
	FriendPlayer = Caster->Handle;                                          // 0x102c53fa / 0x102c53ff +0x60ac
	SetFrenziedWord(GBoss19FrenzyFrenziedWord);                             // 0x102c5405 +0x5b84
	// No slot 614 here: a frenzy waits for the existing think cadence (retail's own asymmetry).
}
