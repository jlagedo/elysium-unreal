#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Squad** — the squad and follower surface of `order.md` layers 0–9.
//
// The squad join/leave/share/vacate set, the follower pair, and the `CNPC_VChangBros` /
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
	constexpr float GNpcKernelSquadFollowerOverlap = ElysiumNpcTunables::Ten;
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
	// The global `CAI_Hint` list walk both retail callers inline (`CNPC_VChangBros::SelectUnitedNode
	// 0x1036d100`, `StoreArenaCenter 0x1036e400`): from the head `DAT_10925450` along `+0x5d8`,
	// counting the nodes whose `m_nHintType` (`+0x5dc`) equals the type, answering the `Ordinal`-th.
	// The TYPE word is the only gate -- neither body reads `m_iDisabled`, the owner or the reuse
	// time, so a disabled or claimed hint counts. The cursor `DAT_10925454` is not touched. Falling
	// off the end answers NULL. The list is `FElysiumEntityWorld::HintList`, head first.
	if (World == nullptr)
	{
		return nullptr;
	}
	int32 Seen = 0;
	for (const int32 HintIndex : World->HintList())
	{
		FHintWords Words;
		if (!HintWords(HintIndex, Words) || Words.HintType != HintType)
		{
			continue;   // not a live hint (crash guard: retail holds the pointer) / another type
		}
		if (Seen == Ordinal)
		{
			return World->Entities().IsValidIndex(HintIndex) ? World->Entities()[HintIndex].Get() : nullptr;
		}
		++Seen;
	}
	return nullptr;
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
	// Slot 404, VIRTUAL (`vt+0x650`): the ally's OWN relation to the attacker. **RETAIL CORRECTION
	// (fold A2):** the port called the controller line's body `0x103a48b0` here for every ally, so an
	// ordinary NPC answered the stand-in's table (D_HT for any visible combat character, D_LI only for
	// its friend player) instead of its own.
	if (IRelationType(Attacker) == 3)
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
	// `SetFollowerBoss(const char*)` (`0x102c44e0`, Werewolf19, story 8 lane L12) resolves the name
	// through slot 559 into `m_hFollowerBoss` (`+0x647c`), refuses `this`, `Error`s on a squad member
	// and on acceptance resets the AI and sets `m_bfNPCFrenziedFlags |= 0x3008`.
	SetFollowerBoss(Name);                                              // 0x102c449b thunk 0x100061ae
	FollowerBossName = Name.IsEmpty() ? FString() : Name;               // 0x102c44a0..0x102c44a8 +0x6478
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


