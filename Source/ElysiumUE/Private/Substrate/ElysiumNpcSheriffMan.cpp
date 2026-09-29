#include "Substrate/ElysiumNpcSheriffMan.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcCombat10_2Shared.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"
#include "Substrate/ElysiumNpcPositionsShared.h"
#include "Substrate/ElysiumNpcScheduleShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	const TCHAR* const GSheriffManFile = TEXT("NPC_VSheriffMan.cpp");
	// The two bits `CNPC_VSheriffMan::KillSheriff` raises on its active weapon.
	constexpr uint32 EffectNoDraw = 0x20u;        // m_fEffects |= 0x20
	constexpr uint32 SolidNotSolid = 0x4u;        // AddSolidFlags(4)
	// `CNPC_VSheriffMan::KillSheriff`'s two named entities.
	constexpr const TCHAR* SheriffRelayName = TEXT("logic_zap_player");
	constexpr const TCHAR* SheriffSelfName = TEXT("sheriff");
	constexpr float GMotorTailSheriffJumpRise = ElysiumNpcTunables::SheriffJumpRise;      // _DAT_104c614c
	constexpr float DegreesPerTurnRecip = ElysiumNpcTunables::InverseOneEighty; // _DAT_104c6d40, 1/180
	constexpr float SheriffLastTeleportFloor = ElysiumNpcTunables::SheriffLastTeleportFloor;    // DAT_104c6124
	// `CNPC_VSheriffMan::SelectTeleportNode` `0x103b0630`.
	constexpr float SheriffTeleportZScale = ElysiumNpcTunables::Hundred;   // a DIMENSIONLESS weight
	constexpr float SheriffScoreDistCap = ElysiumNpcTunables::SheriffScoreDistCap;        // DAT_104c6144
	constexpr float SheriffScoreYawWeight = ElysiumNpcTunables::FourTenths;         // _DAT_1044a2bc
	constexpr float SheriffScoreDistWeight = ElysiumNpcTunables::SixTenths;        // _DAT_104c6d3c
	constexpr int32 HintCenter = 0x4651;
	// `0x10651230`, the model name `NPCInit` `0x103ae6c0` and `Restore` both write.
	const TCHAR* const GSheriffModel = TEXT("models/character/monster/manbat/manbat.mdl");
}

// `CNPC_VSheriffMan`'s constructor `0x103ae3e0` writes the standing hull word at `0x103ae463`,
// after the `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs
// in the same order. It writes `+0x1568` ALONE: he stands on SHERIFF_HULL and routes on the human
// mesh the `CAI_BaseNPC` constructor left in `+0x156c`.
FElysiumNpcSheriffMan::FElysiumNpcSheriffMan()
{
	HullKind = 21;
}

// Slot 420: `0x103ae6c0`.
// `0x103ae6c0`
void FElysiumNpcSheriffMan::NPCInit()
{
	SheriffTeleportSwarm = FElysiumEntityHandle::Invalid();              // BEFORE the base
	bSheriffTeleporting = false;
	bSheriffDead = false;
	bSheriffActivated = false;
	// `m_vLastTeleportPosition = GetAbsOrigin()`. Carried in centimetres like the node pick that
	// overwrites it (`SelectTeleportNodeSheriff`) and the clearance test that reads it (`0x103b0c70`),
	// and like the Andrei's and the Chang brothers' same word (story 5 step 4r: this write alone was
	// in Source units, so an init-seeded cache sat 2.54x too close to the origin).
	SheriffLastTeleportPosition = Origin;
	SheriffLastTeleportTime = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);
	bSheriffLedgeHeightStored = false;                                   // +0x66e7
	VampireBossNPCInit();
	VampireBossMonsterModelName = TEXT("models/character/monster/manbat/manbat.mdl");
	VampireBossMonsterClassname = TEXT("npc_VSheriffMan");
	RecordHealthPercent();                                               // 103c6a00
	JumpGravity = SheriffManJumpGravity;
	NodeGraphHullIndex() = HullIndexSheriffMan;
}

// Slot 104 is gone (story 0019/6): the manbat model, the landblast and teleport emitters
// and `item_w_sheriff_sword` resolve at bake and load; the precache had no observable step or order.

// Slot 461: `0x103aeac0`, the selector tag 0x21 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcSheriffMan::SelectIdealStateRetail()
{
	return HumanSelectIdealState();
}

// Slot 604: `0x103af960`, which replaces the Troika body wholesale; its argument is read by no arm.
// `CNPC_VSheriffMan::SelectScheduleMeleeCombat` `0x103af960`, its slot-604 override's body.

int32 FElysiumNpcSheriffMan::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)
	{
		if (!Slot599(0))
		{
			return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xe9 : 0x15a;
		}
	}
	else if (Slot602())
	{
		Slot601(Enemy);
		return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xe9 : 0xe7;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return 0x15a;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee)
			|| Conds.Has(EElysiumNpcCond::InterruptTime) || bHeightArmed))
	{
		Slot601(Enemy);
		return 0xe9;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack)
		&& !Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return 199;
	}
	return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xca : 0xcb;
}

// Slot 448: `0x103b0290`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VSheriffMan::TaskFail` (`0x103b0290`) — apart from the scope-trace bookkeeping the whole
 *  body is the chain to the base. The recovered fact is the ABSENCE of an arm, and it is ported so
 *  a test can prove the sheriff adds nothing. */
void FElysiumNpcSheriffMan::TaskFail(int32 Reason)
{
	// `CNPC_VSheriffMan::TaskFail` (`0x103b0290`), 100 bytes, of which the whole is the scope-trace
	// push/pop and the chain to the base. The recovered fact is the ABSENCE of an arm: once the
	// Troika body is ported, the sheriff needs only correct dispatch.
	(void)Reason;
	FElysiumNpc::TaskFail(Reason);
}

// Slot 605: `0x103afdb0`
/** `CNPC_VSheriffMan::SelectScheduleRangedCombat` (`0x103afdb0`), 984 bytes — three helpers offered
 *  separately rather than as one chained condition, an inlined dodge, and its own `0x15a` answer on
 *  an unreachable enemy. */
int32 FElysiumNpcSheriffMan::SelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// The same inlined dodge as Ming Xiao's — the first arm of `0x102b7f40` only.
	const auto InlineDodge = [this, &Conds]() -> bool
	{
		return SelectWeightedSequenceForActivity(NpcKernelCombat10_2Shared::GDodgeActivity) != 0
			&& !Conds.Has(EElysiumNpcCond::StopBackup)
			&& NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GDodgeRollThreshold;
	};

	// `103afe1a`: arm 1.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GSheriffManFile, 0x2b1));
		return 0xe3;
	}
	// `103afe43`: arm 2.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this)
		&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GSheriffManFile, 0x2cb));
		return 0xe3;
	}
	// `103afea2`: arm 3.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GSheriffManFile, 0x2d0));
		return 0xb8;
	}
	// `103afed4`: the three helpers, offered SEPARATELY (three `if`s, not one chained condition).
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `103aff2a`: the SPLIT. Neither COND `0x5f` nor COND `0x8` → the sheriff's OWN branch, which
	// has no slot-606 arm at all.
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		// `103aff56`: `COND_SEE_ENEMY` and not `0x48` and not `0x60` → DECLINE.
		if (Conds.Has(EElysiumNpcCond::SeeEnemy) && !Conds.Has(EElysiumNpcCond::EnemyOccluded)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			return 0;
		}
		// `103affa6`: `COND_ENEMY_UNREACHABLE` → 0x15a, else 0xe8.
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x15a"), GSheriffManFile, 0x314));
			return 0x15a;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x31a));
		return 0xe8;
	}
	// `103b0026`: the dodge/spacing branch.
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (InlineDodge())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GSheriffManFile, 0x2ed));
			return 0xef;
		}
		if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GMeleeSwitchRollThreshold
			&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x2f1));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GSheriffManFile, 0x2f5));
		return 0xf0;
	}
	if (InlineDodge())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GSheriffManFile, 0x2fc));
		return 0xb8;
	}
	if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x300));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GSheriffManFile, 0x304));
	return 0xb9;
}

// Slot 440: `0x103b0320`.
// `0x103b0320`
// `0x103b0320`, `CNPC_VSheriffMan::TranslateSchedule`, the body of `FElysiumNpcSheriffMan::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcSheriffMan::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 337: `0x103ae840`.
int32 FElysiumNpcSheriffMan::GetUsedHullBits()
{
	// A bare `return 0x200000`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x200000;
}

// Slot 566: `0x103af810`, a replacement that does not chain.
bool FElysiumNpcSheriffMan::FValidateHintType(void* Hint)
{
	// The whole body is `return 1;`: the hint is never read.
	(void)Hint;
	return true;
}

/** `CNPC_VSheriffMan::vfunc127`'s load-side half (story 0019/6;
 *  `docs/vtmb/npc-ai/shape.md` § "The three species `Restore` bodies"). Retail calls
 *  `CNPC_VVampireBoss::Restore` and then writes, in order: `m_pMonsterModelName`
 *  (`+0x6680`) = the manbat model (`0x10651230`), `m_pszMonsterClassname` (`+0x6694`) =
 *  `"npc_VSheriffMan"`, `m_fJumpGravity` (`+0x64b8`) = `_DAT_104c6148` (**2.0f**, `103ae811 FLD
 *  dword`), and last the process-wide `DAT_109340d8` (the node-graph hull index) = `0x15` — the same
 *  data `NPCInit` `0x103ae6c0` writes. The record half is the generated SAVE walk. */
void FElysiumNpcSheriffMan::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	VampireBossPostRestoreResets();                                      // CNPC_VVampireBoss::Restore
	VampireBossMonsterModelName = GSheriffModel;                         // +0x6680
	VampireBossMonsterClassname = TEXT("npc_VSheriffMan");              // +0x6694
	JumpGravity = SheriffManJumpGravity;                                 // +0x64b8, 2.0f
	NodeGraphHullIndex() = HullIndexSheriffMan;                          // DAT_109340d8 := 0x15
	FElysiumNpcVampire::OnPostRestore(InWorld);
}

// --- Moved from `ElysiumNpcCombat10_2.cpp` (story 5 step 4) ---

// --- `CNPC_VSheriffMan::SelectScheduleRangedCombat` `0x103afdb0`, 984 bytes ----------------------

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

void FElysiumNpcSheriffMan::HideAndUnsolidifyWeapon(const FElysiumEntityHandle& Weapon)
{
	// SEAM for `m_fEffects |= 0x20` (EF_NODRAW), `AddSolidFlags(4)` (FSOLID_NOT_SOLID) and
	// `CBaseEntity::Relink` on the weapon. No `FElysiumEntity` word stands for either mask.
	FHideAndUnsolidifyCall Call;
	Call.Entity = Weapon;
	Call.EffectBits = EffectNoDraw;
	Call.SolidBits = SolidNotSolid;
	HideAndUnsolidifyCalls.Add(Call);
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcSheriffMan::KillSheriff()
{
	// 1. BOTH named entities are looked up and BOTH must exist before the relay fires. The
	//    `sheriff` lookup's result is never used for anything else — it is a presence test, and
	//    reproducing it is the point: a map missing `sheriff` does not zap the player.
	FElysiumEntity* Relay = World != nullptr ? World->FindByName(FString(SheriffRelayName)) : nullptr;
	FElysiumEntity* Self = World != nullptr ? World->FindByName(FString(SheriffSelfName)) : nullptr;
	if (Relay != nullptr && Self != nullptr)
	{
		// `CLogicRelay::InputTrigger` with a default `inputdata_t` (activator -1, value 0).
		static const FName TriggerInput(TEXT("Trigger"));
		World->EnqueueInput(FString(SheriffRelayName), TriggerInput, FElysiumVariant(), 0.0,
			Handle, Handle);
	}

	// 2. The weapon half is UNCONDITIONAL on the relay half — it runs even when the relay or the
	//    `sheriff` entity is missing. `m_fEffects |= 0x20` (EF_NODRAW) then `AddSolidFlags(4)`
	//    (FSOLID_NOT_SOLID) and `Relink()`: the sword goes invisible and non-solid, it is NOT
	//    removed, so a corpse still nominally holds it.
	FElysiumEntity* Weapon = (World != nullptr && Inventory.ActiveWeapon.IsSet())
		? World->Resolve(Inventory.ActiveWeapon) : nullptr;
	if (Weapon != nullptr)
	{
		HideAndUnsolidifyWeapon(Weapon->Handle);
	}
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

void FElysiumNpcSheriffMan::SheriffManSetupJump(float Enabled)
{
	// `CNPC_VSheriffMan::SetupJump` `0x103b1300`, rise `_DAT_104c614c` = 400.0.
	SetupJumpRise(Enabled, GMotorTailSheriffJumpRise);
}

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcSheriffMan::SelectCenterNodeRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm)
{
	// The whole body past the closest-player gate: type `0x4651`, the 3-D distance from the node to
	// the PLAYER (rooted, not squared — `PTR_thunk_FUN_101371d0` is `sqrt`), strictly nearest wins,
	// ties keep the FIRST because the compare is `<`.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintCenter)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, Nodes[Index].OriginCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcSheriffMan::SelectCenterNode() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;   // retail's `m_hClosestPlayer` handle-validity gate
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectCenterNodeRule(Nodes, Player->Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

int32 FElysiumNpcSheriffMan::SelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
	const FVector& MeasureFromCm)
{
	// Type `0x4653`, the 3-D distance from the node to `MeasureFromCm`, nearest wins. Retail's
	// `char` argument selects the reference: `'\0'` reads the PLAYER's `GetAbsOrigin` (through the
	// resolved handle's vtable) and anything else reads its OWN — and the closest-player gate stands
	// in front of both, so a Sheriff with no player answers null even when measuring from himself.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != NpcKernelPositionsShared::HintLedge)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(MeasureFromCm, Nodes[Index].OriginCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcSheriffMan::SelectLedgeNode(bool bMeasureFromSelf) const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectLedgeNodeRule(Nodes, bMeasureFromSelf ? Origin : Player->Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

FElysiumNpc::FTeleportNodePick FElysiumNpcSheriffMan::SelectTeleportNodeSheriffRule(
	TArrayView<const FHintWords> Nodes, const FVector& PlayerCm, float PlayerYaw,
	TFunctionRef<bool(const FVector&, float)> Clear)
{
	// Types 17000, `0x4653` and `0x4652`. The distance is NOT euclidean: the Z component is
	// multiplied by `_DAT_10450564 = 100.0` before the root, so a node one unit above the player
	// reads as a hundred away and the Sheriff will not teleport off his own floor.
	//
	//     d  = player - node;  dz *= 100;  dist = sqrt(dx*dx + dy*dy + dz*dz)
	//     if (dist < DAT_104c6124) skip;                    // 100.0 units
	//     if (!PositionClearForTeleport(node, DAT_104c6124)) skip;
	//     yaw   = AngleDiff( player.yaw, VectorAngles(dx, dy, 0).y );
	//     score = |yaw| * (1/180) * 0.4  +  (min(dist, 1000) / 1000) * 0.6
	//
	// Both terms are normalized into `[0,1]` before the weights, which is why the two weights sum
	// to one. The clearance the gate uses is the SAME constant as the distance floor.
	FTeleportNodePick Pick;
	Pick.Score = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != NpcKernelPositionsShared::HintTeleport17000 && Type != NpcKernelPositionsShared::HintLedge && Type != NpcKernelPositionsShared::HintJumpbase)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const double Dx = PlayerCm.X - NodeCm.X;
		const double Dy = PlayerCm.Y - NodeCm.Y;
		const double Dz = (PlayerCm.Z - NodeCm.Z) * SheriffTeleportZScale;
		float Distance = static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
		if (Distance < SheriffLastTeleportFloor * NpcKernelPositionsShared::U)
		{
			continue;
		}
		if (!Clear(NodeCm, SheriffLastTeleportFloor * NpcKernelPositionsShared::U))
		{
			continue;
		}
		const float YawDelta = NpcKernelPositionsShared::RetailAngleDiff(PlayerYaw, NpcKernelPositionsShared::FlatYaw(PlayerCm, NodeCm));
		if (Distance > SheriffScoreDistCap * NpcKernelPositionsShared::U)
		{
			Distance = SheriffScoreDistCap * NpcKernelPositionsShared::U;
		}
		const float Score = FMath::Abs(YawDelta) * DegreesPerTurnRecip * SheriffScoreYawWeight
			+ (Distance / (SheriffScoreDistCap * NpcKernelPositionsShared::U)) * SheriffScoreDistWeight;
		if (Score < Pick.Score)
		{
			Pick.Score = Score;
			Pick.Index = Index;
		}
	}
	return Pick;
}

int32 FElysiumNpcSheriffMan::SelectTeleportNodeSheriff()
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const FTeleportNodePick Pick = SelectTeleportNodeSheriffRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y),
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportSheriff(PositionCm, ClearanceCm);
		});
	if (Pick.Index == INDEX_NONE)
	{
		// **Named divergence.** Retail dereferences the winning node UNCONDITIONALLY —
		// `(**(code **)(*local_2c + 0x364))()` with `local_2c` still null — so a Sheriff on a map
		// with no reachable teleport node faults. That is a shipped crash and not a behaviour a
		// caller can observe; the port leaves the cache untouched and answers null instead.
		return INDEX_NONE;
	}
	SheriffLastTeleportPosition = Nodes[Pick.Index].OriginCm;
	SheriffLastTeleportTime = World != nullptr ? World->NowSeconds() : 0.0;
	return NodeIds[Pick.Index];
}

bool FElysiumNpcSheriffMan::PositionClearForTeleportSheriff(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x103b0c70`, the whole body:
	//     if (|GetAbsOrigin() - pos| < clearance)       return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)  return false;   // DAT_104c6124
	//     if (player resolves && |player - pos| < clearance) return false;
	//     return true;
	// All three are 3-D, unlike the Asian vampire's flat pair.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(SheriffLastTeleportPosition, PositionCm))
		< SheriffLastTeleportFloor * NpcKernelPositionsShared::U)
	{
		return false;
	}
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		if (static_cast<float>(FVector::Dist(Player->Origin, PositionCm)) < ClearanceCm)
		{
			return false;
		}
	}
	return true;
}

void FElysiumNpcSheriffMan::CacheFloorHeights()
{
	// `0x103b1510`. Two nested gates and three writes, and the nesting is the point: the LEDGE
	// height is only cached when a centre node was found first, so a map that authors ledges but no
	// centre leaves `m_bLedgeStored` false and `CategorizeHeight` comparing against a zero.
	//
	//     node = SelectCenterNode();       if (!node) return;
	//     m_flCenterZ = node->GetAbsOrigin().z;                 // +0x66e8
	//     node = SelectLedgeNode('\0');    if (!node) return;   // measured from the PLAYER
	//     m_bLedgeStored = 1;                                   // +0x66e7
	//     m_flLedgeZ = node->GetAbsOrigin().z;                  // +0x66ec
	const int32 Center = SelectCenterNode();
	if (Center == INDEX_NONE)
	{
		return;
	}
	FHintWords CenterWords;
	if (HintWords(Center, CenterWords) && CenterWords.bValid)
	{
		SheriffCenterFloorZ = static_cast<float>(CenterWords.OriginCm.Z);
	}
	const int32 Ledge = SelectLedgeNode(/*bMeasureFromSelf*/ false);
	if (Ledge == INDEX_NONE)
	{
		return;
	}
	FHintWords LedgeWords;
	if (HintWords(Ledge, LedgeWords) && LedgeWords.bValid)
	{
		bSheriffLedgeHeightStored = true;
		SheriffLedgeFloorZ = static_cast<float>(LedgeWords.OriginCm.Z);
	}
}

void FElysiumNpcSheriffMan::CategorizeHeight(float Zcm, int32& OutCategory) const
{
	// `0x103b1790`, the whole body: `*out = |z - m_flCenterZ| < |z - m_flLedgeZ| ? 0 : 1;`
	// Strictly nearer wins for the centre; a tie goes to the LEDGE. No gate on
	// `m_bLedgeStored` — the byte `CacheFloorHeights` sets has no reader here, which is why a map
	// with no ledge node categorises everything against a zero height.
	OutCategory = FMath::Abs(Zcm - SheriffCenterFloorZ) < FMath::Abs(Zcm - SheriffLedgeFloorZ)
		? 0 : 1;
}

void FElysiumNpcSheriffMan::CategorizeHeights(int32& OutPlayer, int32& OutSelf) const
{
	// `0x103b1680`: both out-params are zeroed FIRST, then the closest player is resolved and both
	// are filled — the player's height, then this body's. A Sheriff with no player therefore
	// answers `0, 0`, which is the centre category for both, not an untouched pair.
	OutPlayer = 0;
	OutSelf = 0;
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	CategorizeHeight(static_cast<float>(Player->Origin.Z), OutPlayer);
	CategorizeHeight(static_cast<float>(Origin.Z), OutSelf);
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate.cpp` (story 5 step 4) ---

