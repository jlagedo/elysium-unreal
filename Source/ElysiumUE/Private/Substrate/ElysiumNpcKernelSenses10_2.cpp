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
// `ElysiumNpcKernelSenses10.cpp` route to.
//
// Every body is retail's, arm by arm, with the `0x10……` address of the arm beside it.

namespace
{
	constexpr int32 GSpeciesD_HT = 1;

	// The player sheet's level-script clan encoding: Brujah 2 … Ventrue 8, so Nosferatu is 5
	// (`ElysiumDlgClan::OffsetFromSheetClan`). `0x103ad0f0` compares the target's character template
	// against `Player_Nosferatu`, and a player's template IS its clan on this sheet.
	constexpr int32 GNosferatuClan = 5;

	// `_DAT_1044eb0c` = **20.0** Source units, Ming Xiao's aim-point Z bonus.
	constexpr float GMingXiaoAimZBonusUnits = 20.0f;

	// `_DAT_10450568` = **360.0**, the yaw wrap; `_DAT_104454c4` = **0.0**, its lower bound.
	constexpr float GYawWrapDegrees = 360.0f;

	// `_DAT_104492a4` = **60.0** Source units, the Z lift `InitializeHintData` applies before it
	// asks for a groundpoint.
	constexpr float GHintGroundpointLiftUnits = 60.0f;

	// `_DAT_10452dc4` = **2.0** seconds, the random-move arm's last-seen window.
	constexpr float GRandomMoveLastSeenWindow = 2.0f;

	// The Scurrying flee march's two scalars: `_DAT_104454d0` = **0.5** (the retry scale) and
	// `_DAT_104454c0` = **1.0** (the give-up floor and the "trace was blocked" fraction).
	constexpr float GFleeRetryScale = 0.5f;
	constexpr float GFleeDistanceFloor = 1.0f;

	// The jitter bands `0x103acba0` picks between: `0..-60` / `0..60` on the dominant axis and
	// `-80..80` on the other, with five attempts.
	constexpr float GFleeJitterMinor = 80.0f;
	constexpr float GFleeJitterMajor = 60.0f;
	constexpr int32 GFleeJitterAttempts = 5;

	// `0x102edae0`'s search radius.
	constexpr float GFleeNodeSearchUnits = 30000.0f;
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

bool FElysiumNpc::CameraSecurityFVisible(FElysiumEntity* Candidate)
{
	// `10369ff0`: resolve the linked `CSecCamera`, then answer 1 only when the candidate carries a
	// player record (`+0xa8`) AND the link is non-null AND `0x1020cd00` is true. Every other case is
	// 0, and the NPC's own eyes are never consulted — this REPLACES the Troika body `0x102b4630`.
	FElysiumEntity* Camera = ResolveSecCameraLink();
	const bool bPlayer = Candidate != nullptr && World != nullptr
		&& Candidate->Handle == World->PlayerHandle();
	return bPlayer && Camera != nullptr && SecCameraCanSee(Camera, Candidate);
}

bool FElysiumNpc::CameraSecurityFInViewCone(FElysiumEntity* Candidate)
{
	// `10369fb0`: the same shape with the camera's CONE test in place of its full sight test.
	FElysiumEntity* Camera = ResolveSecCameraLink();
	const bool bPlayer = Candidate != nullptr && World != nullptr
		&& Candidate->Handle == World->PlayerHandle();
	return bPlayer && Camera != nullptr && SecCameraInViewCone(Camera, Candidate);
}

// =================================================================================================
// `CNPC_VTzimisce` slot 201 `0x103ba290`, `CNPC_VZombie` slot 201 `0x103e0bc0`.
// =================================================================================================

bool FElysiumNpc::TzimisceFVisible(FElysiumEntity* Candidate, int32 Mask, FElysiumEntity* Blocker,
	int32 /*Arg4*/)
{
	// `103ba290`, 30 bytes: the Troika base with the FOURTH argument FORCED to `0`. The whole
	// override is that one clamp, and it is observable — every Tzimisce visibility test runs the
	// base with that word zeroed.
	return FVisible(Candidate, Mask, Blocker, 0);
}

bool FElysiumNpc::ZombieFVisible(FElysiumEntity* Candidate, int32 Mask, FElysiumEntity* Blocker,
	int32 /*Arg4*/)
{
	// `103e0bef`: arm one — the current enemy's own `+0x9c` compared against the QUERIED entity. On
	// this leaf `+0x9c` IS the entity (the self-downcast cache), so the compare is "the candidate is
	// my enemy". The answer is then NOT `0x10146a80` — discipline stat 8 at or above 1 AND the
	// entity's `+0x14dc` cloak byte, which is `FElysiumCombatCharacter::IsObfuscatedForSenses`.
	FElysiumEntity* Enemy = GetEnemy();
	const FElysiumCombatCharacter* EnemyCharacter = Enemy != nullptr
		? Enemy->AsCombatCharacter() : nullptr;
	if (EnemyCharacter != nullptr && Candidate != nullptr && Candidate->Handle == Enemy->Handle)
	{
		return !EnemyCharacter->IsObfuscatedForSenses();
	}
	// `103e0c2e`: arm two — the Troika base with the fourth argument forced to 0, as Tzimisce does.
	return FVisible(Candidate, Mask, Blocker, 0);
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
// `CNPC_VMingXiao::GetShootEnemyDir` `0x10395d00`, 144 bytes.
// =================================================================================================

FVector FElysiumNpc::MingXiaoGetShootEnemyDir(const FVector& ShootPositionCm, int32 /*A*/,
	int32 /*B*/)
{
	// `10395d1c`: identical to the base `0x10278900` except that the aim point's Z is raised by
	// `_DAT_1044eb0c` (**20.0** Source units) BEFORE the caller's shoot position is subtracted.
	FVector AimPointCm = ShootEnemyAimPoint(ShootPositionCm);
	AimPointCm.Z += static_cast<double>(GMingXiaoAimZBonusUnits) * ElysiumMove::U;
	// The delta is normalised in place through `0x1057966c` and the UNIT direction stored out, which
	// the decompiled C hides for the same stack-shift reason as the base body.
	return (AimPointCm - ShootPositionCm).GetSafeNormal();
}

// =================================================================================================
// `CNPC_VScurrying` — `0x103acac0` and `0x103acba0`.
// =================================================================================================

bool FElysiumNpc::IsNosferatuTemplate(const FElysiumEntity& SeenTarget)
{
	// `0x103ad0f0`: the target's character template compared against `Player_Nosferatu`. On this
	// sheet a player's template IS its clan (`ElysiumDlgClan::OffsetFromSheetClan`), and a
	// non-player target carries no `Player_*` template at all.
	const FElysiumCombatCharacter* Character = SeenTarget.AsCombatCharacter();
	return Character != nullptr && Character->Sheet.Clan() == GNosferatuClan
		&& SeenTarget.AsNpc() == nullptr;
}

bool FElysiumNpc::ScurryingMustDetectAdmits(const FElysiumEntity& SeenTarget) const
{
	// `0x103ad0a0`: true for any target that is NOT a player, and for a player only while
	// COND `0x5a` (`SEE_PLAYER`) or COND `0x6f` stands.
	if (World == nullptr || SeenTarget.Handle != World->PlayerHandle())
	{
		return true;
	}
	return Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x5a))
		|| Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x6f));
}

bool FElysiumNpc::ScurryingShouldDetect(const FElysiumEntity* SeenTarget) const
{
	// `103acac0`: a null target answers false.
	if (SeenTarget == nullptr)
	{
		return false;
	}
	// `103acad4`: the distance between the two slot-217 origins (the ROOT of the summed squares,
	// `0x10579660`) must be STRICTLY below `m_flDetectionDistance` (`+0x6690`).
	const float DistanceUnits = static_cast<float>(
		FVector::Dist(Origin, SeenTarget->Origin) / ElysiumMove::U);
	if (!(DistanceUnits < ScurryingDetectionDistanceUnits))
	{
		return false;
	}
	// `103acb1a`: `m_fIgnoreNosferatu` (`+0x6694`) rejects a `Player_Nosferatu` target.
	if (bScurryingIgnoreNosferatu && IsNosferatuTemplate(*SeenTarget))
	{
		return false;
	}
	// `103acb3c`: `m_fMustDetect` (`+0x6695`) rejects unless `0x103ad0a0` holds.
	if (bScurryingMustDetect && !ScurryingMustDetectAdmits(*SeenTarget))
	{
		return false;
	}
	// `103acb52`: passing all of them answers true.
	return true;
}

bool FElysiumNpc::NearestNavigatorNode(const FVector& /*PositionCm*/, float /*RadiusUnits*/,
	FVector& OutNodeCm) const
{
	// SEAM for `0x102edae0` over `0x103008f0` / `0x102ee9c0` — "the nearest navigator node within
	// `radius`". No AI network stands on this substrate, so the search fails, which is the arm that
	// still produces a destination (the march below).
	OutNodeCm = FVector::ZeroVector;
	return false;
}

bool FElysiumNpc::IsAreaClear(const FVector& /*PositionCm*/, int32 /*Mask*/) const
{
	// SEAM for `CAI_BaseNPCTroika::IsAreaClear(pos, 0x202400b, 0, 0)`. This runtime has no hull
	// sweep; answers true, which ADMITS the jittered point — retail's own answer for open ground.
	return true;
}

bool FElysiumNpc::ScurryingFindFleeDestination(const FVector& ThreatPosCm, float DistanceUnits,
	FVector* OutDestinationCm)
{
	// `103acbb0`: the navigator is asked for the nearest node to the THREAT within 30000 units.
	FVector NodeCm = FVector::ZeroVector;
	if (NearestNavigatorNode(ThreatPosCm, GFleeNodeSearchUnits, NodeCm))
	{
		// `103acc3c`: the JITTER. Whichever of the x or y deltas to the threat is LARGER picks the
		// axis; the SIGN of that delta picks a `0..-60` or `0..60` band on it and the other axis
		// gets `-80..80`.
		const double DeltaX = ThreatPosCm.X - NodeCm.X;
		const double DeltaY = ThreatPosCm.Y - NodeCm.Y;
		float MinX = 0.f;
		float MaxX = 0.f;
		float MinY = 0.f;
		float MaxY = 0.f;
		if (FMath::Abs(DeltaX) <= FMath::Abs(DeltaY))
		{
			// The Y delta dominates: Y takes the signed 0..±60 band, X takes -80..80.
			if (DeltaY <= 0.0)
			{
				MinY = 0.f;
				MaxY = -GFleeJitterMajor;
			}
			else
			{
				MinY = 0.f;
				MaxY = GFleeJitterMajor;
			}
			MinX = -GFleeJitterMinor;
			MaxX = GFleeJitterMinor;
		}
		else
		{
			if (DeltaX <= 0.0)
			{
				MinX = 0.f;
				MaxX = -GFleeJitterMajor;
			}
			else
			{
				MinX = 0.f;
				MaxX = GFleeJitterMajor;
			}
			MinY = -GFleeJitterMinor;
			MaxY = GFleeJitterMinor;
		}
		// `103accc8`: z is the node z plus slot 522 `StepHeight` times `_DAT_104454d0` (0.5); up to
		// FIVE random points are tried against `IsAreaClear`, and the LAST one tried is kept
		// whether or not it was accepted.
		FVector CandidateCm = NodeCm;
		for (int32 Attempt = 0; Attempt < GFleeJitterAttempts; ++Attempt)
		{
			const float ZCm = static_cast<float>(NodeCm.Z
				+ static_cast<double>(StepHeight()) * 0.5 * ElysiumMove::U);
			CandidateCm.X = NodeCm.X
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					FMath::Min(MinX, MaxX), FMath::Max(MinX, MaxX)) * ElysiumMove::U;
			CandidateCm.Y = NodeCm.Y
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					FMath::Min(MinY, MaxY), FMath::Max(MinY, MaxY)) * ElysiumMove::U;
			CandidateCm.Z = ZCm;
			if (IsAreaClear(CandidateCm, 0x202400b))
			{
				break;
			}
		}
		// `103acd34`: the accepted point is written out only when one was passed, and the body
		// answers 1 either way.
		if (OutDestinationCm != nullptr)
		{
			*OutDestinationCm = CandidateCm;
		}
		return true;
	}

	// `103acbd0`: the MARCH. Start at slot 217 `GetAbsOrigin` raised by `StepHeight` times
	// `_DAT_104454d0`; the direction is normalised AWAY from the threat.
	const FVector StartCm = Origin
		+ FVector(0.0, 0.0, static_cast<double>(StepHeight()) * 0.5 * ElysiumMove::U);
	FVector DirectionCm = (Origin - ThreatPosCm).GetSafeNormal();
	float Distance = DistanceUnits;
	for (;;)
	{
		const FVector EndCm = StartCm + DirectionCm * (static_cast<double>(Distance) * ElysiumMove::U);
		// `103acc02`: a hull trace with the collision bounds from `+0x1568` and mask `0x202400b`.
		FKernelHullTrace Trace;
		const bool bTraced = KernelHullTrace(StartCm / ElysiumMove::U, EndCm / ElysiumMove::U,
			HullMinsUnits(false), HullMaxsUnits(false), 0x202400b, Trace);
		// `103acc74`: the trace is BLOCKED while the fraction is below `_DAT_104454c0` (1.0) or
		// either solid flag is set. The seam reports a clear trace, which is the arm that returns
		// the endpoint.
		const bool bBlocked = bTraced && Trace.Fraction < GFleeDistanceFloor;
		if (!bBlocked)
		{
			if (OutDestinationCm != nullptr)
			{
				*OutDestinationCm = EndCm;
			}
			return true;
		}
		// `103acc8c`: scale BOTH the distance and the direction by `_DAT_104454d0` and retry.
		DirectionCm *= GFleeRetryScale;
		Distance *= GFleeRetryScale;
		// `103acd0c`: give up once the distance falls to `_DAT_104454c0` (1.0) or below.
		if (Distance <= GFleeDistanceFloor)
		{
			return false;
		}
	}
}

// =================================================================================================
// `CNPC_VWerewolf` — the hint bodies and `CheckStuck`.
// =================================================================================================

FVector FElysiumNpc::GetHintEndpointUnits(const FHintWords& Hint) const
{
	// SEAM for `CNPC_VWerewolf::GetHintEndpoint`: the hint's END entity's origin, resolved through
	// family Hints' `FindHintEndEntity` (`0x103d6520`). No hint store stands here, so the end entity
	// never resolves and this answers the hint's OWN origin — which is retail's answer for a hint
	// whose `target_name` names nothing.
	if (World != nullptr)
	{
		const int32 EndNode = FindHintEndEntity(Hint);
		FHintWords End;
		if (EndNode != INDEX_NONE && HintWords(EndNode, End))
		{
			return End.OriginCm / ElysiumMove::U;
		}
	}
	return Hint.OriginCm / ElysiumMove::U;
}

float FElysiumNpc::GetForwardYawForHint(const FHintWords& Hint) const
{
	// `103d728c`: the working direction is SEEDED with `vec3_invalid` (`DAT_10713de0`…`de8`) and
	// then overwritten outright by `endOrigin - forwardOrigin`; the seed never reaches the answer.
	const FVector EndUnits = GetHintEndpointUnits(Hint);
	FVector ForwardOriginUnits = Hint.OriginCm / ElysiumMove::U;
	const int32 ForwardHint = GetForwardHintForHint(Hint);
	FHintWords ForwardWords;
	if (ForwardHint != INDEX_NONE && HintWords(ForwardHint, ForwardWords))
	{
		ForwardOriginUnits = ForwardWords.OriginCm / ElysiumMove::U;
	}
	// `103d72cf`: the delta, normalised (`0x1057966c`) and converted to a yaw (`0x101d2c70`).
	const FVector Delta = (EndUnits - ForwardOriginUnits).GetSafeNormal();
	float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));

	// `103d730b`: the switch on the hint type at `+0x5dc`. `_DAT_10455050`, the adjust arms' 90
	// degrees, stood at a zero offset until the cell was read (2026-09-21); the two arms that
	// discard the computed yaw entirely were always exact.
	constexpr float GHintYawAdjustDegrees = ElysiumNpcTunables::WerewolfHintYawAdjust;
	switch (Hint.HintType)
	{
	case 0x3a9a:
	case 0x3a9d:
		Yaw += GHintYawAdjustDegrees;
		break;
	case 0x3a9b:
	case 0x3a9e:
		Yaw -= GHintYawAdjustDegrees;
		break;
	case 0x3aa3:
	case 0x3aa5:
		// `103d7328`: the computed yaw is discarded and the HINT's own slot-219 yaw is taken.
		Yaw = static_cast<float>(Hint.Angles.Y);
		break;
	default:
		break;
	}
	// `103d7347`: **read off the listing, because the decompiler lost the `float10` return storage
	// and both tails read alike.** It is a single-step WRAP, not a selection: above `360.0` subtract
	// `360.0`; otherwise, below `0.0` add `360.0`.
	if (Yaw > GYawWrapDegrees)
	{
		Yaw -= GYawWrapDegrees;
	}
	else if (Yaw < 0.f)
	{
		Yaw += GYawWrapDegrees;
	}
	return Yaw;
}

FVector FElysiumNpc::GetHintTargetGroundpoint(const FHintWords& Hint) const
{
	// `103d6939`: the linear scan of `m_HintData` (`+0x6714`, count `+0x6720`, stride `0x48`)
	// comparing the ENTITY POINTER at element `+0x04`. The hit answers the Vector at element
	// `+0x14` — the TARGET groundpoint, where family Hints' twin answers `+0x08`.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode != INDEX_NONE && Row.HintNode == Hint.HintIndex)
		{
			return Row.GroundpointUnits;
		}
	}
	// `103d698c`: the miss `DevWarning`s and falls back to `GetGroundpoint(GetHintEndpoint(hint))`,
	// so a miss STILL answers a point.
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s: 0x103d68d0 Werewolf did not find the hint target groundpoint"), *DebugString());
	return GetGroundpoint(GetHintEndpointUnits(Hint));
}

bool FElysiumNpc::IsGroundpointExponentValid(const FVector& PointUnits)
{
	// `103d7a1e`: `(bits & 0x7f800000) == 0x7f800000` on each component — an infinity or a NaN.
	// NOTE `FLT_MAX` (`0x7f7fffff`, which IS `vec3_invalid`) PASSES this test and is stored.
	auto ExponentSaturated = [](double Component)
	{
		const float Value = static_cast<float>(Component);
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return (Bits & 0x7f800000u) == 0x7f800000u;
	};
	return !ExponentSaturated(PointUnits.X) && !ExponentSaturated(PointUnits.Y)
		&& !ExponentSaturated(PointUnits.Z);
}

FElysiumNpc::FWerewolfHintGroundpoint FElysiumNpc::InitializeHintDataRow(
	const FHintWords& Hint) const
{
	// `103d7800`..`103d7ae4`, one hint's row. `CHintData_WW::Init`, the hint at `+4`, the end-entity
	// handle at `+0` (`0xffffffff` when absent), the forward yaw written back through the hint's own
	// angles (slot `0x104`), `GetGroundpoint(GetAbsOrigin + _DAT_104492a4)` into `+8` and
	// `GetGroundpoint(GetHintEndpoint + _DAT_104492a4)` into `+0x14`, each validated against the
	// `0x7f800000` exponent mask and on failure falling back to the RAW origin / RAW endpoint.
	FWerewolfHintGroundpoint Row;
	// Element `+0x04` is the hint ENTITY (`this_01[1] = this_00`), not its network node.
	Row.HintNode = Hint.HintIndex;
	// The end-entity handle at element `+0x00`, which this comment already named and the row did not
	// carry until story 29d's family Hints10 added the word for `GetHintEndEntity` (`0x103d6390`) to
	// read back. `FindHintEndEntity` (`0x103d6520`) is what retail resolves it with.
	Row.CachedEndEntity = FindHintEndEntity(Hint);

	const FVector OwnUnits = Hint.OriginCm / ElysiumMove::U
		+ FVector(0.0, 0.0, GHintGroundpointLiftUnits);
	const FVector OwnGround = GetGroundpoint(OwnUnits);
	const FVector TargetUnits = GetHintEndpointUnits(Hint) + FVector(0.0, 0.0, GHintGroundpointLiftUnits);
	const FVector TargetGround = GetGroundpoint(TargetUnits);

	// The TARGET groundpoint is what this family's `GetHintTargetGroundpoint` reads back, so it is
	// what the row carries; family Hints' own `GetHintGroundpoint` reads `+8`, whose value is
	// `OwnGround` and is recomputed there.
	if (!IsGroundpointExponentValid(OwnGround))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103d7710 %s has invalid ground point"),
			*DebugString(), *Hint.Name);
	}
	if (IsGroundpointExponentValid(TargetGround))
	{
		Row.GroundpointUnits = TargetGround;
	}
	else
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s: 0x103d7710 %s has invalid target ground point"), *DebugString(), *Hint.Name);
		Row.GroundpointUnits = GetHintEndpointUnits(Hint);
	}
	return Row;
}

void FElysiumNpc::InitializeHintData()
{
	// `103d77e8`: the whole body runs only while the count `+0x6720` is ZERO — a one-shot build.
	if (WerewolfHintGroundpoints.Num() != 0)
	{
		return;
	}
	// `103d77fa`: the walk is the GLOBAL hint list from `DAT_10925450` through each hint's
	// `+0x5d8` next link (`this_00[1].m_vecViewOffset[1]`), every hint of every type.
	//
	// Not ported here, and recorded as the two live-hint effects this body has: `103d7888` writes the
	// forward yaw back through the hint's own angles (slot `0x104`, `SetLocalAngles`), and `103d7a70`
	// counts the entities named by the hint's `target_name`, `DevWarning`ing when it is not exactly
	// one. Both belong to the Werewolf's program (0002), not to the hint store.
	int32 Count = 0;
	for (const int32 Node : GlobalHintList())
	{
		FHintWords Hint;
		if (!HintWords(Node, Hint) || !Hint.bValid)
		{
			continue;
		}
		WerewolfHintGroundpoints.Add(InitializeHintDataRow(Hint));
		++Count;
	}
	// `103d7b0a`: `DevMsg("There are %d WW hints on this map")`.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103d7710 there are %d WW hints on this map"),
		*DebugString(), Count);
}

bool FElysiumNpc::CachedNearestNodeZone(int32& OutZone) const
{
	// SEAM for `0x103d0ad0` — the cached nearest node, whose `+0x94` is the zone the random-move arm
	// compares against `m_iRandomMoveHintNodeZone`. No node graph; answers false, which is retail's
	// own empty-cache arm and refuses the hint.
	OutZone = 0;
	return false;
}

bool FElysiumNpc::ValidateHintTypeForWords(const FHintWords& Hint) const
{
	// Slot 566 (`vtable +0x8d8`) over the words already in hand — see the declaration for why the
	// node-index entry point cannot serve these two bodies.
	const FElysiumNpcClass* Cls = RetailClass();
	const FHintTypeSpecies* Row = HintTypeSpeciesOf(Cls != nullptr ? Cls->Name : nullptr);
	if (FValidateHintTypeSpecies(Row, Hint.HintType))
	{
		return true;
	}
	if (HintTypeSpeciesFallsThroughToBase(Row))
	{
		// `CNPC_VBach` (`0x10365800`) alone; slot 566's Troika body is still a generated stub.
		return const_cast<FElysiumNpc*>(this)->FValidateHintType(nullptr);
	}
	return false;
}

bool FElysiumNpc::IsValidRandomMoveHint(const FHintWords& Hint, double Now)
{
	// `103d7dfa`: false unless all four of — the hint is non-null, `0x102d14c0` says it is not
	// taken, slot 566 `FValidateHintType` passes, and the type `+0x5dc` is not `0x3aa9`.
	if (!Hint.bValid)
	{
		return false;
	}
	const bool bOwnerAlive = Hint.HintOwner.IsSet() && World != nullptr
		&& World->Resolve(Hint.HintOwner) != nullptr;
	if (IsHintUnusable(Hint, Now, bOwnerAlive))
	{
		return false;
	}
	if (!ValidateHintTypeForWords(Hint))
	{
		return false;
	}
	if (Hint.HintType == 0x3aa9)
	{
		return false;
	}
	// `103d7e4a`: `0x3aa8` is true only when slot 617 `EnemyCouldSeeHull` at the hint ENDPOINT
	// answers FALSE.
	if (Hint.HintType == 0x3aa8)
	{
		return !WerewolfHintTrace(GetHintEndpointUnits(Hint) * ElysiumMove::U);
	}
	// `103d7ea8`: `0x3a9a`, `0x3a9b` and `0x3a99` are true when `curtime - m_flLastSeenByPlayerTime`
	// is at or below `0` OR below `_DAT_10452dc4` (**2.0**), and otherwise only when the cached
	// nearest node's `+0x94` equals `m_iRandomMoveHintNodeZone` (`+0x670c`).
	if (Hint.HintType == 0x3a9a || Hint.HintType == 0x3a9b || Hint.HintType == 0x3a99)
	{
		const float Elapsed = static_cast<float>(Now - WerewolfLastSeenTime);
		if (Elapsed < 0.f || Elapsed < GRandomMoveLastSeenWindow)
		{
			return true;
		}
		int32 Zone = 0;
		return CachedNearestNodeZone(Zone) && Zone == RandomMoveHintNodeZone;
	}
	// `103d7f4c`: `0x3aa7`, `0x3aa5`, `15000` and `0x3aa3` are ALWAYS false.
	if (Hint.HintType == 0x3aa7 || Hint.HintType == 0x3aa5 || Hint.HintType == 15000
		|| Hint.HintType == 0x3aa3)
	{
		return false;
	}
	// `103d7f8c`: every other type is true only when the per-NPC cooldown list `0x10366400` holds no
	// live entry — a call that also evicts the expired row by swapping in the last.
	FElysiumEntity* HintEntity = World != nullptr && Hint.HintOwner.IsSet()
		? World->Resolve(Hint.HintOwner) : nullptr;
	return !FUN_10366400(HintEntity);
}

bool FElysiumNpc::IsValidMoveHint(const FHintWords& Hint, double Now)
{
	// `103d8113`: false on a null hint, on `0x102d14c0` reporting the hint taken, and on slot 566
	// `FValidateHintType` failing.
	if (!Hint.bValid)
	{
		return false;
	}
	const bool bOwnerAlive = Hint.HintOwner.IsSet() && World != nullptr
		&& World->Resolve(Hint.HintOwner) != nullptr;
	if (IsHintUnusable(Hint, Now, bOwnerAlive))
	{
		return false;
	}
	if (!ValidateHintTypeForWords(Hint))
	{
		return false;
	}
	// `103d8179`: types `0x3aa3` and `0x3aa9` are false. NOTE the difference from the random-move
	// twin, which refuses `0x3aa9` but treats `0x3aa3` through its always-false group.
	if (Hint.HintType == 0x3aa3 || Hint.HintType == 0x3aa9)
	{
		return false;
	}
	// `103d81a5`: `0x3aa8` needs `HasCondition(0x77)` SET **and** slot 617 to answer false.
	if (Hint.HintType == 0x3aa8)
	{
		if (!Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x77)))
		{
			return false;
		}
		return !WerewolfHintTrace(GetHintEndpointUnits(Hint) * ElysiumMove::U);
	}
	// `103d820c`: `0x3aa5` needs `m_DoorState` (`+0x6680`) to be exactly `2`.
	if (Hint.HintType == 0x3aa5)
	{
		return WerewolfDoorState == 2;
	}
	// `103d8225`: every other type needs the hint's end entity to have a CLEAR byte at `+0xf4` and
	// the cooldown list to answer false. `0x100b5190`'s whole body is `return *(byte*)(ent + 0xf4)`;
	// **`+0xf4`'s retail name is UNRECOVERED** — it is `CBaseEntity`'s, and `CanHearSound`
	// (`0x1030f7b0`) reads it beside `m_bIsBCCTargetable` as a refusal. It answers 0 here, the
	// admitting value.
	const int32 EndNode = FindHintEndEntity(Hint);
	FHintWords End;
	if (EndNode != INDEX_NONE && HintWords(EndNode, End))
	{
		// The byte's seam: 0, so the arm never refuses on it.
	}
	FElysiumEntity* HintEntity = World != nullptr && Hint.HintOwner.IsSet()
		? World->Resolve(Hint.HintOwner) : nullptr;
	return !FUN_10366400(HintEntity);
}

void FElysiumNpc::WerewolfCheckStuck()
{
	// `103cb9a4`: the whole body is gated on slot `0x28c`. `0x28c / 4` is slot 163 `IsViewable` on
	// the hint line; on the NPC line it is the "may this body be probed" gate, and this runtime's
	// nearest answer is `IsAlive`.
	if (!IsAlive())
	{
		return;
	}
	// `103cb9bc`: probe 1 is a hull trace from `GetAbsOrigin` to origin + `_DAT_10452dc4` (2.0) in
	// Z with mask `0x202400b`, using the `m_eHull` normal mins/maxs and the navigator's filter,
	// inside a `CVProfile` "CAI_MoveProbe::TraceHull" scope.
	const FVector StartUnits = Origin / ElysiumMove::U;
	const FVector EndUnits = StartUnits + FVector(0.0, 0.0, 2.0);
	FKernelHullTrace Probe;
	const bool bTraced = KernelHullTrace(StartUnits, EndUnits, HullMinsUnits(false),
		HullMaxsUnits(false), 0x202400b, Probe);
	// `103cba9c`: the CLEAR branch (`cStack_55 == 0`, the start-solid byte) re-probes through the
	// navigator with the SMALL hull when `+0x5f2d` is set and the third extent scaled by
	// `_DAT_10449154` (0.45, `ElysiumNpcTunables::WerewolfStuckHullScale`), and only when THAT
	// probe reports blocked does it
	// `DevWarning "attempting alt unstuck..."` and run slot `0x360`.
	const bool bStartSolid = bTraced && Probe.Fraction < 1.f;
	if (!bStartSolid)
	{
		// SEAM: the navigator re-probe (`0x102a99e0`) does not exist here and reports clear, so the
		// alt-unstuck arm is not taken — retail's own not-stuck answer.
		SetHullSizeSmall(true);
		return;
	}
	// `103cbb44`: the BLOCKED branch — up to three escalating world traces (`0x1006dec0` with the
	// `0x101d3190(this, 7)` filter). The first reports "Werewolf stuck!"; the second reports
	// "Werewolf unstuck..." and runs slot `0x360`; the third either teleports out
	// (`0x10269aa0(this, 0x77)` succeeding → "Werewolf teleported out from stuck", `TeleportOut` and
	// slot `0x700` with "Werewolf stuck") or reports "Werewolf STUCK!!" and does nothing.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf stuck!"), *DebugString());
	FKernelHullTrace Second;
	const bool bSecondBlocked = KernelHullTrace(StartUnits, EndUnits, HullMinsUnits(false),
		HullMaxsUnits(false), 0x202400b, Second) && Second.Fraction < 1.f;
	if (!bSecondBlocked)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf unstuck..."),
			*DebugString());
		SetHullSizeSmall(true);
		return;
	}
	FKernelHullTrace Third;
	const bool bThirdBlocked = KernelHullTrace(StartUnits, EndUnits, HullMinsUnits(false),
		HullMaxsUnits(false), 0x202400b, Third) && Third.Fraction < 1.f;
	if (!bThirdBlocked)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf unstuck..."),
			*DebugString());
		SetHullSizeSmall(true);
		return;
	}
	if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x77)))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s: 0x103cb920 Werewolf teleported out from stuck"), *DebugString());
		++WerewolfTeleportOutCalls;
		// `103cbf07`: the teleport arm is the ONE exit that does NOT run `SetHullSizeSmall(1)`.
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf STUCK!!"), *DebugString());
	SetHullSizeSmall(true);
}
