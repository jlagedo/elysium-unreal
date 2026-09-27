#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSquadShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Squad** — the squad and follower surface of `order.md` layers 0–9.
//
// 74 rows: the 57 slot-546 `SquadSlotName` bodies (the Troika method, the deferred rows' table below, and
// each species' own id-space row in its override), the
// squad join/leave/share/vacate set, the follower pair, and the `CNPC_VChangBros` /
// `CNPC_VMingXiao` coordination helpers. The walked prose is `docs/vtmb/npc-ai/social.md`.
//
// THE STANDING FACT OF THIS FAMILY: there is no squad object here. `m_pSquad` (`+0x5da4`) is
// recorded ABSENT and `ConnectedSquad()` answers `nullptr`. Every body below is ported verbatim
// and then asks the seam declared in `ElysiumNpcSquad.inl`, which answers nothing and names
// the retail call it stands for. Nothing here invents a squad to make a body "work".

namespace
{

	// `_DAT_1044e664` — the follower-distance overlap, recovered by story 16a
	// (`docs/vtmb/npc-ai/social.md` § "`m_hFollowerBoss` — the follower controller").
	constexpr float GNpcKernelSquadFollowerOverlap = 10.0f;
}

// -------------------------------------------------------------------------------------------------
// The squad seam. Seven accessors, each answering nothing.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SquadMemberCount(const void* Squad) const
{
	// SEAM for `CAI_Squad::NumMembers` (`0x103160a0`, `squad+0x5c`).
	(void)Squad;
	return 0;
}

FElysiumEntity* FElysiumNpc::SquadMember(const void* Squad, int32 Index) const
{
	// SEAM for `CAI_Squad::GetMember(i)` (`0x103160c0`, the `m_hMembers[16]` array at `squad+0x1c`,
	// which answers NULL for every index when member 0 is disconnected).
	(void)Squad;
	(void)Index;
	return nullptr;
}

FElysiumEntity* FElysiumNpc::NthHintOfType(int32 HintType, int32 Ordinal) const
{
	// SEAM for the global `CAI_Hint` list walk (`DAT_10925450`, next `+0x5d8`, `m_nHintType
	// +0x5dc`). `BaseScheduleHost.HintNode` is a bare node index here; no store carries hint types yet.
	(void)HintType;
	(void)Ordinal;
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// Slot 546 `SquadSlotName` — the Troika method and the deferred rows' table.
// -------------------------------------------------------------------------------------------------
//
// Every row was read from the decompiled C, not from a summary. All 56 species bodies are
// byte-identical but for their id-space global:
//
//     iVar1 = SquadSlotLocalToGlobal(&DAT_<species>, slotEN);   // 0x102ea2d0
//     return IdToSymbol(&DAT_10936c74, iVar1);                  // 0x102ea020
//
// and the Troika line (`0x101a6c00`) is the same without the first line.
//
// THE ID SPACE EACH SPECIES ANSWERS IS EMPTY, and that is a recovered fact, not a gap:
//
//   * every species space is constructed with `isRoot = false` (`0x102ea090`), which leaves
//     `m_globalBase = -1`, `m_localBase = 9999` (the "empty" sentinel) and `m_localTop = -1`;
//   * `CAI_ClassScheduleIdSpace::Init` (`0x102ea0e0`) only rewrites those three when the space has
//     already been filled (`+0x0c != -1`), which at static-init time it has not, so `Init` binds
//     the namespace and the parent and leaves the range empty;
//   * the only way a range becomes non-empty is `ADD_CUSTOM_SQUADSLOT`, which registers the name
//     into the global namespace as well — and `vampire.dll` contains exactly TWO squad-slot name
//     strings, `SQUAD_SLOT_ATTACK1` and `SQUAD_SLOT_ATTACK2`, both referenced only by
//     `0x10316e80`, the seeder of the GLOBAL namespace. No class registers one;
//   * the chain ends at the root space `DAT_10920484` (global base 0, local base 0, local top -1),
//     which matches no id either.
//
// So `SquadSlotName(n)` answers `"<<null>>"` for every `n` on every one of the 56 species, and on
// the Troika line answers `SQUAD_SLOT_ATTACK1`/`2` for ids 1000000000/1000000001, `"<<null>>"` for
// -1 and null for anything else. This agrees with `docs/vtmb/npc-ai/social.md` § "Squads, decoded
// (2026-09-08)" — "Strategy slots ship dead … every class registers zero squadslots".

namespace
{
}

const FElysiumNpcBase::FSquadSlotSpecies* FElysiumNpc::SquadSlotSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(NpcKernelSquadShared::GNpcKernelSquadSlotSpecies);
	return NpcKernelSquadShared::GNpcKernelSquadSlotSpecies;
}

// -------------------------------------------------------------------------------------------------
// The squad bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetSquad(const FString& NewSquadName)
{
	// 0x1029a930 `CAI_BaseNPCTroika::SetSquad`, the `SQUAD` tweak param's move:
	//
	//   1. already squadded  -> RemoveFromSquad(m_pSquad, this); m_pEnemies = NULL
	//      not squadded      -> delete the private AI_Enemies (0x102e0730 + operator delete)
	//   2. squad = FindCreateSquad(this, name[0] ? name : NULL)
	//      hit  -> m_pSquad = squad; m_pEnemies = squad + 8   (the squad's embedded AI_Enemies)
	//      miss -> m_pSquad = NULL;  m_pEnemies = new AI_Enemies (0x102e06f0)
	//   3. if (m_iSquadDisconnected > 0 && m_pSquad) LeaveSquad(this)   -- `0x10316700` is RET 4,
	//      an empty stub, so this arm has no effect in retail either.
	void* Squad = const_cast<void*>(ConnectedSquad());
	if (Squad != nullptr)
	{
		RemoveFromSquad(Squad);
	}
	// Step 2. The seam never finds and never creates, so the enemy memory keeps its private
	// ownership, which is what the "miss" arm does anyway (`new AI_Enemies`).
	//
	// `SetSquad` does NOT write `m_SquadName` (`+0x5da8`) — the keyfield and the squad object are
	// separate words in retail and this body only moves the object.
	void* NewSquad = FindOrCreateSquad(NewSquadName, /*bFindOnly=*/false);
	RepointEnemyMemoryToSquad(NewSquad);
	if (BaseScheduleHost.SquadDisconnected > 0 && ConnectedSquad() != nullptr)
	{
		// `LeaveSquad` `0x10316700` is `RET 4`.
	}
}

void FElysiumNpc::AlertNearbyAlly(FElysiumEntity* Attacker)
{
	// 0x102bf5d0. The caller walks the NPCs near a victim; `this` is the ally being told and
	// `Attacker` is who did it. Arm for arm:
	//
	//   attacker != NULL
	//   && m_NPCState (+0x5cc0) is 1 (IDLE), 3 (COMBAT) or 0xb (the Troika hunt state)
	//   && attacker->m_pCombatCharacter (+0x9c) != NULL
	//   && IRelationType(attacker) (slot 404, vt+0x650) != 3 (D_LI)
	//   && ( dist2(attacker->GetAbsOrigin(), GetAbsOrigin()) < _DAT_1049aea0
	//        || (FVisible(attacker, 0x2804091, 0, 0) && FInViewCone(attacker)) )
	//   -> 0x102bf560: unless m_bIgnoreDetectedAttack (+0x65f5), record the attacker in
	//      m_hDetectedAttacker (+0x65c0) and set m_flDetectedAttackExpireTime (+0x65c4) to
	//      curtime + _DAT_10454110.
	//
	// 29c's one-line walk read `vt+0x650` as a not-dead test; it is slot 404 `IRelationType`, and
	// the constant compared is `D_LI`. Corrected here and in `social.md`.
	if (Attacker == nullptr || World == nullptr)
	{
		return;
	}
	const EElysiumNpcState State = Mind.State();
	// Retail's third admitted state is the custom `0xb`, which this runtime's state set does not
	// carry (`EElysiumNpcState` stops at `Dead`); UNRECOVERED here, so only 1 and 3 are tested.
	if (State != EElysiumNpcState::Idle && State != EElysiumNpcState::Combat)
	{
		return;
	}
	// `+0x9c m_pCombatCharacter` is CBaseEntity's self-downcast cache: non-null exactly for a
	// `CBaseCombatCharacter`.
	if (Attacker->AsCombatCharacter() == nullptr)
	{
		return;
	}
	if (SpeciesIRelationType(Attacker) == 3)
	{
		return;
	}
	// `_DAT_1049aea0` is the squared radius. Its literal is not in the corpus; the same chain is
	// recovered in `docs/vtmb/combat-and-damage.md` as 150 Source units, which is the constant
	// `ElysiumNpcCond::MeleeNoticeAcceptanceUnits` already carries.
	const double RadiusCm =
		static_cast<double>(ElysiumNpcCond::MeleeNoticeAcceptanceUnits) * ElysiumMove::U;
	const double Dist2 = FVector::DistSquared(Origin, Attacker->Origin);
	const double Now = World->NowSeconds();
	const bool bNear = Dist2 < RadiusCm * RadiusCm;
	if (!bNear
		&& !(FElysiumNpcSenses::IsVisible(*this, *Attacker, Now)
			&& FElysiumNpcSenses::IsInViewCone(*this, *Attacker)))
	{
		return;
	}
	// 0x102bf560, a row of its own; its two writes land on the words the shape map binds here.
	if (!bIgnoreDetectedAttack)
	{
		Senses.Memory.DetectedAttackAttacker = Attacker->Handle;
		// Retail stores an EXPIRY, `curtime + _DAT_10454110`; this runtime stores the stamp and
		// compares it against `ElysiumNpcCond::DetectedAttackRetentionSeconds`, the same 5.0.
		Senses.Memory.DetectedAttackTime = Now;
	}
}

int32 FElysiumNpc::SpeciesIRelationType(const FElysiumEntity* Candidate) const
{
	// 0x103a48b0, slot 404's body for CNPC_VFrenzyShadow, CNPC_VPlayerController and
	// CNPC_VWolfMorph:
	//
	//   target == NULL                                   -> 0   D_ER
	//   target == m_hFriendPlayer (+0x60ac, resolved)    -> 3   D_LI
	//   target->m_pCombatCharacter (+0x9c) != NULL
	//     && cc->m_bIsBCCTargetable (+0x1480)
	//     && !cc->m_bScriptHidden   (+0x00f4, 0x100b5190) -> 1  D_HT
	//   otherwise                                        -> 4   D_NU
	if (Candidate == nullptr)
	{
		return 0;
	}
	if (World != nullptr && FriendPlayer.IsSet() && World->Resolve(FriendPlayer) == Candidate)
	{
		return 3;
	}
	const FElysiumCombatCharacter* Combatant = Candidate->AsCombatCharacter();
	if (Combatant != nullptr)
	{
		// `m_bIsBCCTargetable` has no port field and no recovered clearer, so it reads true — the
		// same reading `ElysiumNpcConditions.cpp` already takes for the `+0x1480` term.
		const bool bTargetable = true;
		if (bTargetable && !Candidate->IsHidden())
		{
			return 1;
		}
	}
	return 4;
}

// -------------------------------------------------------------------------------------------------
// The follower pair.
// -------------------------------------------------------------------------------------------------

// slot 293 0x102c5470 `CBaseEntity* GetFollowerBoss()`
FElysiumEntity* FElysiumNpc::GetFollowerBoss()
{
	// 0x102c5470: resolve `m_hFollowerBoss` (`+0x647c`) through the handle table and answer
	// `boss + 0x9c`, the cached `CBaseCombatCharacter*` — so a live boss that is not a combat
	// character answers NULL, exactly as a dead handle does.
	if (World == nullptr || !FollowerBoss.IsSet())
	{
		return nullptr;
	}
	FElysiumEntity* Boss = World->Resolve(FollowerBoss);
	return Boss != nullptr ? Boss->AsCombatCharacter() : nullptr;
}

void FElysiumNpc::SetFollowerBossName(const FElysiumEntity* Boss)
{
	// 0x102c4470:
	//   name = boss->m_pPlayer (+0xa8) ? "!player" : (boss->m_iName (+0x26c) ?: "")
	//   SetFollowerBoss(name)                       -- 0x102c44e0, a row of its own
	//   m_sFollowerBoss (+0x6478) = name[0] ? name : NULL
	//
	// Retail dereferences `boss` without a null test; a null argument faults there and is refused
	// here (NAMED DIVERGENCE: a crash is not a behaviour the port reproduces).
	if (Boss == nullptr)
	{
		return;
	}
	const bool bIsPlayer = World != nullptr && Boss->Handle == World->PlayerHandle();
	const FString Name = bIsPlayer ? FString(TEXT("!player")) : Boss->TargetName;
	// `SetFollowerBoss(const char*)` (`0x102c44e0`) resolves the name through slot 559, refuses
	// `this`, `Error`s on a squad member and sets `m_bfNPCFrenziedFlags |= 0x3008`. It is a row of
	// its own and is NOT ported here; `m_hFollowerBoss` (`+0x647c`) therefore stays unwritten and
	// `GetFollowerBoss()` keeps answering nothing.
	FollowerBossName = Name.IsEmpty() ? FString() : Name;
}

void FElysiumNpc::SetFollowerType(const FString& NewFollowerType)
{
	// 0x102c4640: `0x102c4680(type)`, then `m_sFollowerType (+0x6480) = type[0] ? type : NULL`.
	//
	// 0x102c4680, the whole of it:
	//   0x101e8c90(&DAT_10739d08, type, &backAway +0x6484, &walkTo +0x6488, &runTo +0x648c)
	//       -- the `Npc_Follower_Info` row of Rules.txt (`docs/vtmb/npc-ai/social.md`)
	//   if (walkTo < backAway + 10.0) { DevMsg x4; walkTo = backAway + 10.0 }
	//   if (runTo  < walkTo   + 10.0) { DevMsg x4; runTo  = walkTo   + 10.0 }
	//
	// SEAM: this runtime has no `Npc_Follower_Info` loader, so the row read answers nothing and the
	// three distances keep whatever value they already carry. The CLAMP is the recovered rule and
	// runs either way — it is what keeps the three follower bands from overlapping.
	const float Overlap = GNpcKernelSquadFollowerOverlap;
	if (FollowerDistanceWalkTo < FollowerDistanceBackAway + Overlap)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("WARNING: FollowerDistanceBackAway (%f) and FollowerDistanceWalkTo (%f) overlap; ")
			TEXT("changing FollowerDistanceWalkTo to %f. NPC is using Follower Type '%s'"),
			FollowerDistanceBackAway, FollowerDistanceWalkTo,
			FollowerDistanceBackAway + Overlap, *NewFollowerType);
		FollowerDistanceWalkTo = FollowerDistanceBackAway + Overlap;
	}
	if (FollowerDistanceRunTo < FollowerDistanceWalkTo + Overlap)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("WARNING: FollowerDistanceWalkTo (%f) and FollowerDistanceRunTo (%f) overlap; ")
			TEXT("changing FollowerDistanceRunTo to %f. NPC is using Follower Type '%s'"),
			FollowerDistanceWalkTo, FollowerDistanceRunTo,
			FollowerDistanceWalkTo + Overlap, *NewFollowerType);
		FollowerDistanceRunTo = FollowerDistanceWalkTo + Overlap;
	}
	FollowerType = NewFollowerType.IsEmpty() ? FString() : NewFollowerType;
}

FElysiumEntity* FElysiumNpc::ResolveNamedMaster() const
{
	// 0x101a8130, disassembled because the decompiled C folds the RTTI call:
	//
	//   name = *(char**)(this + 0x5f5c); if (!name) return 0;
	//   ent  = gEntList.FindEntityByName(NULL, name, 0, 0);   -- 0x100f7770 over DAT_106eb5d8
	//   if (!ent || !ent->m_pBaseNPC (+0x94)) return 0;
	//   return RTDynamicCast(ent, 0, CBaseEntity_typeinfo 0x10538764, 0x105947c8, 0);
	//
	// LARGELY UNRECOVERED, and stated rather than papered over:
	//   * the key word. `+0x5f5c` is the second `COutputEvent` of the NPC's eight-output block in
	//     this runtime's shape (`ElysiumNpcKernelShapeMap.cpp`, `_IMPLICIT`), so no port member
	//     holds a name there and the read is a local empty key;
	//   * the target type. RTTI type descriptor `0x105947c8` has exactly ONE referrer in the whole
	//     image — this body — so the corpus does not name the class the cast admits;
	//   * the caller. The corpus records no call site, so the receiver class is unconfirmed.
	//
	// What IS recovered and ported: the name gate, the by-name entity lookup and the `+0x94`
	// `m_pBaseNPC` gate (the entity must be an NPC). The final type gate has no recovered answer,
	// so this refuses rather than admitting an NPC retail might reject.
	const FString NamedMasterKey;  // +0x5f5c, UNRECOVERED — see above
	if (NamedMasterKey.IsEmpty() || World == nullptr)
	{
		return nullptr;
	}
	FElysiumEntity* Found = World->FindByName(NamedMasterKey);
	if (Found == nullptr || Found->AsNpcBase() == nullptr)   // `piVar1[0x25]`, `+0x94 m_pBaseNPC`
	{
		return nullptr;
	}
	return nullptr;  // UNRECOVERED: the RTTI class `0x105947c8` admits
}
