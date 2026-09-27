#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **Senses10** — the SPECIES line: the `SpeciesSenses10` rows plus the species
// arms of `FVisible`, `FInViewCone`, `BestEnemy` and `GetShootEnemyDir` that the dispatchers in
// `ElysiumNpcSenses10.cpp` route to.
//
// Every body is retail's, arm by arm, with the `0x10……` address of the arm beside it.

namespace
{
	constexpr int32 GSpeciesD_HT = 1;

}

// =================================================================================================
// `CNPC_VCameraSecurity` — slot 201 `0x10369ff0` and slot 363 `0x10369fb0`.
// =================================================================================================

bool FElysiumNpc::SecCameraCanSee(const FElysiumEntity* Camera, const FElysiumEntity* SeenTarget) const
{
	// SEAM for `CSecCamera::CanSee` (`0x1020cd00`): the enabled byte `+0x7d8`, then `0x1020cd30`'s
	// 2-D distance against the far radius `+0x794` and the near radius `+0x790`, then the cone test
	// `0x1020cc60`, then a `0x4091` trace whose fraction must equal `_DAT_10449280` (**1.0**).
	// The camera entity on this substrate carries none of those five words, so the ENABLED byte
	// reads clear — retail's switched-off camera, which is the refusing arm and the one that leaves
	// a security NPC blind rather than omniscient.
	(void)Camera;
	(void)SeenTarget;
	return false;
}

bool FElysiumNpc::SecCameraInViewCone(const FElysiumEntity* Camera,
	const FElysiumEntity* SeenTarget) const
{
	// SEAM for `CSecCamera::InViewCone` (`0x1020cc60`) alone: re-check the argument's `+0xa8`, take
	// the camera's forward from its angles (camera slot `+0x374` through `0x10139610`), the
	// normalised direction from the camera's `GetAbsOrigin` to the argument's `WorldSpaceCenter`
	// (`0x101d1120`), and require the dot STRICTLY greater than the camera's cosine `+0x798`. The
	// cosine word does not exist here; answers false, as above.
	(void)Camera;
	(void)SeenTarget;
	return false;
}

// =================================================================================================
// `CNPC_VFrenzyShadow::BestEnemy` `0x103766d0`, 793 bytes.
// =================================================================================================

FElysiumEntity* FElysiumNpc::FrenzyShadowBestEnemy()
{
	if (World == nullptr)
	{
		return nullptr;
	}
	// `103766d8`: the friend player's `+0xa8` record, resolved ONCE and handed to every relation
	// query below — so a frenzy shadow asks what its candidates think of its FRIEND, not of itself.
	FElysiumEntity* FriendRecord = FriendPlayer.IsSet() ? World->Resolve(FriendPlayer) : nullptr;
	const bool bFriendIsPlayer = FriendRecord != nullptr
		&& FriendRecord->Handle == World->PlayerHandle();
	FElysiumEntity* const FriendArgument = bFriendIsPlayer ? FriendRecord : nullptr;

	auto CandidateHatesFriend = [this, FriendArgument](FElysiumEntity* Candidate)
	{
		// `10376769` / `10376885`: `cand->IRelationType(friendRecord)`. The CANDIDATE's table is
		// asked, not this NPC's, which is why it goes through the candidate's own NPC leaf when it
		// has one and answers D_ER otherwise — retail's null-`this` arm.
		const FElysiumNpc* CandidateNpc = Candidate != nullptr ? Candidate->AsNpc() : nullptr;
		return CandidateNpc != nullptr && CandidateNpc->IRelationTypeOf(FriendArgument) == GSpeciesD_HT;
	};
	auto PassesFilter = [this](FElysiumEntity* Candidate)
	{
		// `1037673d`..`1037675c` and `1037681d`..`10376846`, the filter both halves share:
		// a live `+0x9c`, `GetFlags` bit `0x8000` clear, `m_bIsBCCTargetable` set and slot 158 alive.
		return Candidate != nullptr && Candidate->AsCombatCharacter() != nullptr
			&& !HasNoTargetFlag(*Candidate) && IsBccTargetable(*Candidate) && !Candidate->IsInert();
	};

	// `1037671a`: the STICKY arm, and it runs only while more than ONE hostile was counted last
	// pass.
	if (FrenzyShadowHostileEnemyCount > 1)
	{
		FElysiumEntity* Current = GetEnemy();
		if (PassesFilter(Current) && CandidateHatesFriend(Current)
			&& !EnemyMemory.IsEluded(Current->Handle)      // 10376777 IsEluded
			&& !IsUnreachable(Current))                    // 1037678f slot 530
		{
			return Current;                                // 1037679a — unchanged
		}
	}

	// `103767a2`: the full rescan. NOTE the seeds — best null, bestDist `0x10000000`, bestScore
	// **0** (not `-1000`, which is the base body's priority seed) — and the hostile count is zeroed
	// here, so it is rebuilt by this pass.
	FElysiumEntity* Best = nullptr;
	int32 BestDistance = 0x10000000;
	int32 BestScore = 0;
	FrenzyShadowHostileEnemyCount = 0;

	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		FElysiumEntity* Candidate = World->Resolve(Record.Handle);
		if (!PassesFilter(Candidate))
		{
			continue;
		}
		if (EnemyMemory.IsEluded(Candidate->Handle))       // 10376859
		{
			continue;
		}
		// `1037686d`: the SCORE, built from four independent terms rather than compared as a
		// lexicographic key — which is the whole difference from the base body.
		int32 Score = 0;
		if (!IsUnreachable(Candidate))                      // slot 530
		{
			Score += 0x40000000;
		}
		if (CandidateHatesFriend(Candidate))                // 10376885
		{
			Score += 0x20000000;
			++FrenzyShadowHostileEnemyCount;                // 1037689d
		}
		if (BestEnemyCandidateVisible(Candidate))           // 103768a3 DidSeeEntity || FVisible
		{
			Score += 0x10000000;
		}
		Score += IRelationPriorityOf(Candidate) * 0x1000000;   // 103768d6, slot 405 shifted 24

		if (Score > BestScore)
		{
			// `103768e9`: slot 479 `IsValidEnemy` before the win.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			BestDistance = BestEnemyDistanceKey(*Candidate);
			BestScore = Score;
			Best = Candidate;
			continue;
		}
		if (Score != BestScore)
		{
			continue;                                        // 10376944 JNZ — only EQUAL carries on
		}
		const int32 Distance = BestEnemyDistanceKey(*Candidate);
		// `10376992`: STRICTLY closer, and then `IsValidEnemy`.
		if (Distance < BestDistance && IsValidEnemy(Candidate))
		{
			BestDistance = Distance;
			BestScore = Score;
			Best = Candidate;
		}
	}
	// `103769cb`: on exit, a winner that differs from the current `GetEnemy()` clears
	// `m_bFailedGrapple`. Retail compares the entity against the `+0x9c` it kept, which on this leaf
	// is the same pointer, so the compare is a plain entity compare.
	if (Best != GetEnemy())
	{
		bFrenzyShadowFailedGrapple = false;
	}
	return Best;
}

// =================================================================================================
// `CNPC_VScurrying` — `0x103acac0` and `0x103acba0`.
// =================================================================================================

bool FElysiumNpc::IsAreaClear(const FVector& /*PositionCm*/, int32 /*Mask*/) const
{
	// SEAM for `CAI_BaseNPCTroika::IsAreaClear(pos, 0x202400b, 0, 0)`. This runtime has no hull
	// sweep; answers true, which ADMITS the jittered point — retail's own answer for open ground.
	return true;
}
