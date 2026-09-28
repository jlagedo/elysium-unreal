// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcWerewolf19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Werewolf19's `rule` rows): 0x102c44e0 SetFollowerBoss, 0x103cac20 FUN_103cac20, 0x102c4430
// FUN_102c4430, 0x10397380 FUN_10397380.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 8, lane L12. Arms carry the instruction address they came from (`vtmb_asm`); walked prose in
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Werewolf19".

// -------------------------------------------------------------------------------------------------
// 0x102c44e0 SetFollowerBoss
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::SetFollowerBoss(const FString& BossName)
{
	// Slot 559 `FindNamedEntity` (`vtable +0x8bc`), then slot 1 `GetRefEHandle` on the find.
	FElysiumEntity* Found = FindNamedEntity(*BossName);                    // 0x102c44ec
	if (Found != nullptr)                                                  // 0x102c44f4
	{
		FollowerBoss = Found->Handle;                                      // 0x102c44fa / 0x102c44ff +0x647c
	}
	else
	{
		FollowerBoss = FElysiumEntityHandle::Invalid();                    // 0x102c4507
	}
	// The handle is validated through the handle table (serial, then entity pointer), and then
	// resolved AGAIN for the self test — one resolve here, since the two answer the same entity.
	const FElysiumEntity* Boss = World != nullptr && FollowerBoss.IsSet()  // 0x102c451a
		? World->Resolve(FollowerBoss) : nullptr;                          // 0x102c4540 / 0x102c4549
	if (Boss == nullptr || Boss == this)                                   // 0x102c4558..0x102c4579 0x102c456f 0x(handle 0xserial)
	{
		FollowerBoss = FElysiumEntityHandle::Invalid();                    // 0x102c45d7
		return false;                                                      // 0x102c45e1
	}
	// `m_iSquadDisconnected < 1 && m_pSquad`: a connected squad member cannot follow. This runtime
	// stands no squad object (`ConnectedSquad()` answers null), so the arm is unreached; it is kept
	// whole. Retail's `[0x109f366c]` is tier0's `Error`, which is fatal there; here it is logged at
	// Error severity and the refusal returns (NAMED DIVERGENCE: the port does not terminate).
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)   // 0x102c4583 / 0x102c458d
	{
		FollowerBoss = FElysiumEntityHandle::Invalid();                    // 0x102c4591
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("%s - Followers can not be in squads.  This functionality not implemented."),
			*DebugString());                                               // 0x102c459b / 0x102c45a6
		return false;                                                      // 0x102c45af
	}
	ResetAiState(false, false);                                            // 0x102c45b5..0x102c45bb 0x102b52a0
	SetFrenziedWord(FrenziedWord | 0x3008u);                               // 0x102c45c6 / 0x102c45cb +0x5b84
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102c4430 SetFollowerBossName(const char*)
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetFollowerBossName(const FString& BossName)
{
	SetFollowerBoss(BossName);                                             // 0x102c4439 0x102c44e0
	// `m_sFollowerBoss = name[0] ? name : NULL` — the empty string normalised to NULL, which is what
	// makes `StartNPC` substitute the empty literal on the next start.
	FollowerBossName = BossName.IsEmpty() ? FString() : BossName;          // 0x102c443e..0x102c4447 +0x6478
}

// -------------------------------------------------------------------------------------------------
// 0x10397380 ShareEnemyWithAlly
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ShareEnemyWithAlly(FElysiumNpc* Ally)
{
	FElysiumEntity* Enemy = static_cast<const FElysiumNpcBase*>(this)->GetEnemy();   // 0x10397386 slot 167
	if (Enemy == nullptr)                                                  // 0x10397390
	{
		return;
	}
	if (Ally == nullptr)
	{
		// NAMED CRASH GUARD: retail dereferences the argument without a test (`0x1039739e`); its
		// one caller (`0x10397410`) passes the tentacle it has just created and null-checked.
		return;
	}
	// `CBaseCombatCharacter::AddEntityRelationship(ally, enemy, D_HT, 5)` — on the ALLY.
	// `0x10005849` -> `0x10332ca0`, which overwrites at any priority (not `SetEntity`, which refuses a
	// lower one).
	Ally->Relationships.AddEntityRelationship(Enemy->Handle, EElysiumRelationship::Hate, 5);   // 0x1039739e
	Ally->Slot596(Enemy);                                                  // 0x103973a8 slot 596
	// 0x103973b6: slot 1 on `ent_trace_conditions`, result discarded.
	Ally->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);             // 0x103973bd COND 0x54
	// THIS NPC's squad, not the ally's: `m_iSquadDisconnected (+0x5bb0) < 1` and `m_pSquad (+0x5da4)`.
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)   // 0x103973cb / 0x103973d5
	{
		// `SquadNewEnemy(m_pSquad, enemy)` (`0x103161a0`, `0x103973d8`). No squad object stands here,
		// so this arm is unreached, and `SquadNewEnemy` has no port body to call.
	}
}
