#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **EntityChain**, part 2 — the nine `CBasePlayer` bodies that carry real
// arithmetic: autoaim (`GetAutoaimVector` + `AutoaimDeflection`), the held use entity, the
// camera-override crossfade, the closest-NPC cache and its sense value, the player-animation arm,
// and the `npc_VPlayerController` detach. Part 1 is `ElysiumNpcKernelEntityChain.cpp` and carries
// the file header, the constants and every seam these bodies ask.
//
// Split at ~1,100 lines per the story's own rule, on the seam between "the slots and the small
// getters" and "the six long player bodies".

namespace
{
	// The constants part 1 states in full; restated here because the two translation units do not
	// share an anonymous namespace and the module builds adaptive-unity, where a second definition
	// of the same name would collide. Unit-prefixed for that reason.
	constexpr float GChain2Zero = ElysiumNpcTunables::Zero;
	constexpr float GChain2One = ElysiumNpcTunables::One;
	constexpr float GChain2AutoaimNewWeight = 0.7f;   // _DAT_10457f54
	constexpr float GChain2ClosestNpcReset = 100000.0f;   // the 0x47c34ff3 immediate
	constexpr float GChain2AutoaimDistance = 16384.0f;    // the 0x46800000 immediate
	constexpr float GChain2AutoaimWrap = 360.0f;          // _DAT_10450568
	constexpr float GChain2AutoaimHalfWrap = ElysiumNpcTunables::OneEighty;   // negated, `_DAT_10462948`
	constexpr float GChain2AutoaimPitchClamp = 25.0f;
	constexpr float GChain2AutoaimYawClamp = 12.0f;
	constexpr int32 GChain2FlAimTarget = 0x10000;         // FL_AIMTARGET
	constexpr int32 GChain2ClosestNpcOverThreshold = 0x264;
	// `Disposition_t D_HT`, the answer `AutoaimDeflection` admits a candidate on (slot 404's `== 1`).
	constexpr int32 GChain2DispositionHate = 1;
}

// -------------------------------------------------------------------------------------------------
// Shared small arithmetic.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::ChainEntityFlags(const FElysiumEntity& Entity)
{
	// `CBaseEntity::GetFlags()`. `FElysiumEntity::Flags` is that word; no seam.
	return Entity.Flags;
}

bool FElysiumNpc::ChainTakesDamage(const FElysiumEntity& Entity)
{
	// `entity->m_takedamage != DAMAGE_NO` (+0x01fc). Family Damage carries the word as
	// `TakeDamageMode` ON THE NPC LEAF and nowhere else, because it is the only leaf 29c's shape
	// reached. An entity that is not an NPC therefore answers `DAMAGE_NO`, which is retail's own
	// default for a `CBaseEntity` nothing set — and it is what makes the autoaim trace fall through
	// to the entity walk for a world brush.
	const FElysiumNpc* Npc = Entity.AsNpc();
	return Npc != nullptr && Npc->TakeDamageMode != 0;
}

FVector FElysiumNpc::ChainAngleForward(const FRotator& Angles)
{
	// `AngleVectors(angles, &forward, 0, 0)` (`thunk_FUN_10139610`). Retail's `QAngle` is
	// `(pitch, yaw, roll)` in degrees and `FRotator` is the same triple in the same order, so the
	// conversion is the engine's own and nothing is respelt.
	return Angles.Vector();
}

float FElysiumNpc::ChainWrapDegrees(float Degrees)
{
	// The `(-180, 180]` normalisation both deflection angles go through:
	//     if (a > 180)  a -= 360;
	//     if (a < -180) a += 360;
	// Retail runs each test ONCE, not in a loop, so an angle outside `(-540, 540)` comes out still
	// outside. Reproduced rather than replaced with `FRotator::NormalizeAxis`, which loops.
	if (Degrees > GChain2AutoaimHalfWrap)
	{
		Degrees -= GChain2AutoaimWrap;
	}
	if (Degrees < -GChain2AutoaimHalfWrap)
	{
		Degrees += GChain2AutoaimWrap;
	}
	return Degrees;
}

// -------------------------------------------------------------------------------------------------
// Autoaim — `0x10176520` and `0x10176930`.
// -------------------------------------------------------------------------------------------------

FVector FElysiumNpc::GetAutoaimVector(const FVector& ShootPosition)
{
	// 0x10176520 — `CBasePlayer::GetAutoaimVector`.
	//
	// ARM ONE, autoaim off (`thunk_FUN_101764d0()` answers 0), and it is the whole body:
	//     AngleVectors(m_Local.m_vecPunchAngle + pl.v_angle) -> forward
	// Note what it does NOT include: `m_vecAutoAim`. With autoaim off the retained deflection is
	// ignored rather than cleared, so it survives across a toggle.
	//
	// ARM TWO, autoaim on:
	//   1. `Weapon_ShootPosition(GetOrigin())` — slot 389 off slot 220.
	//   2. `m_vecAutoAim` is ZEROED, before the deflection is computed, so the deflection's own
	//      angle sum sees a zero autoaim term.
	//   3. `AutoaimDeflection(shootPos, 16384.0, delta)`.
	//   4. `!AllowAutoTargetCrosshair()` clears `m_fOnTarget` — and only that; a true answer leaves
	//      whatever the deflection raised.
	//   5. the two returned angles are wrapped into `(-180, 180]` and clamped to ±25 (pitch) and
	//      ±12 (yaw).
	//   6. `DAT_1070ba3c == 1` scales them by `_DAT_10450a9c`; otherwise they are BLENDED with the
	//      retained `m_vecAutoAim` as `old * _DAT_10451ab8 + new * _DAT_10457f54`.
	//   7. `AngleVectors(punch + v_angle + m_vecAutoAim)` -> forward.
	//
	// **Unrecovered:** `DAT_1070ba3c` (the branch selector — the SDK's `m_fOldTargetTime + 2.0 <
	// curtime` sticky-aim test sits here, so this is the "always use non-sticky autoaim" latch that
	// shipped hard-coded to 1), `_DAT_10450a9c` (the scale, `0.9` in the SDK) and `_DAT_10451ab8`
	// (the old-sample weight, `0.6` in the SDK). Only `_DAT_10457f54 = 0.7` is settled from the
	// corpus, and it does NOT pair with the SDK's `0.4`, so Troika retuned the blend and the two
	// unknown weights are left as seams rather than guessed from the SDK.
	const FRotator Punch = LocalPunchAngle;
	const FRotator View = EyeAngle;

	if (!GameRulesAutoAimEnabled())
	{
		return ChainAngleForward(Punch + View);
	}

	AutoAim = FRotator::ZeroRotator;
	const FRotator Deflection = AutoaimDeflection(ShootPosition, GChain2AutoaimDistance,
		AutoaimDelta());

	if (!GameRulesAllowAutoTargetCrosshair())
	{
		bOnTarget = false;
	}

	float Pitch = ChainWrapDegrees(Deflection.Pitch);
	float Yaw = ChainWrapDegrees(Deflection.Yaw);
	Pitch = FMath::Clamp(Pitch, -GChain2AutoaimPitchClamp, GChain2AutoaimPitchClamp);
	Yaw = FMath::Clamp(Yaw, -GChain2AutoaimYawClamp, GChain2AutoaimYawClamp);

	float Scale = 0.f;
	float OldWeight = 0.f;
	if (AutoaimBlendWeights(Scale, OldWeight))
	{
		AutoAim = FRotator(Pitch * Scale, Yaw * Scale, Deflection.Roll * Scale);
	}
	else
	{
		AutoAim = FRotator(
			AutoAim.Pitch * OldWeight + Pitch * GChain2AutoaimNewWeight,
			AutoAim.Yaw * OldWeight + Yaw * GChain2AutoaimNewWeight,
			AutoAim.Roll * OldWeight + Deflection.Roll * GChain2AutoaimNewWeight);
	}

	return ChainAngleForward(Punch + View + AutoAim);
}

float FElysiumNpc::AutoaimDelta() const
{
	// **SEAM** for the `flDelta` `GetAutoaimVector` passes on: it arrives in the caller's own frame
	// and the decompilation cannot attribute it (`unaff_retaddr`). The SDK's callers pass
	// `AUTOAIM_2DEGREES` = `cos(2°)` ≈ 0.96815. **Unrecovered** here, and answering 0 makes the
	// deflection's alignment screen accept any candidate in front of the player rather than a narrow
	// cone — which is the LOOSEST reading, deliberately, because a tight one invented from the SDK
	// would silently drop candidates retail admits.
	return GChain2Zero;
}

bool FElysiumNpc::AutoaimBlendWeights(float& OutScale, float& OutOldWeight) const
{
	// **SEAM** for `DAT_1070ba3c`, `_DAT_10450a9c` and `_DAT_10451ab8` — see `GetAutoaimVector`.
	// Answering TRUE with a scale of 0 is the shipped branch (`DAT_1070ba3c == 1`, the "always
	// non-sticky" latch) with an unrecovered scale, so the retained autoaim is written as zero and
	// `GetAutoaimVector` answers the unassisted direction. Nothing is invented.
	OutScale = GChain2Zero;
	OutOldWeight = GChain2Zero;
	return true;
}

FRotator FElysiumNpc::AutoaimDeflection(const FVector& Src, float Distance, float Delta)
{
	// 0x10176930 — `CBasePlayer::AutoaimDeflection`, the aim-assist target search.
	//
	// STEP 0. `thunk_FUN_101764d0()` again: autoaim off clears `m_fOnTarget` and answers the global
	//         zero vector `DAT_1070d9d0..d8` through the hidden return pointer. This is the same
	//         gate `GetAutoaimVector` opened with, asked a second time.
	// STEP 1. The look direction is `AngleVectors(punch + v_angle + m_vecAutoAim)` — the SAME sum
	//         `GetAutoaimVector` ends on, which is why the caller zeroes `m_vecAutoAim` first.
	//         `m_fOnTarget` is cleared here too, before anything is found.
	// STEP 2. Trace the ray `Src -> Src + forward * Distance`. If it hit something that takes
	//         damage, the search is OVER at zero deflection: the water-level pair
	//         (`my waterlevel != 3 && his == 3`, or `mine == 3 && his == 0`) is the one thing that
	//         lets it fall through to the walk, and `FL_AIMTARGET` on the hit raises `m_fOnTarget`.
	// STEP 3. Otherwise walk the live entity list. Each candidate is screened, in retail's order,
	//         on: the edict's free byte, "not the entity at `this+0x2e0`" (my own edict), the
	//         rules' per-candidate admission, a resolvable base entity, `FL_AIMTARGET`, slot 158
	//         `IsAlive`, the same water-level pair, and then
	//         `IRelationType(candidate) == D_HT || candidate->m_pPlayer != 0 || rules override`.
	// STEP 4. The survivor's `BodyTarget` (slot 197, `+0x314`) gives the aim point; the candidate is
	//         admitted only when `dot(toTarget, look) >= 0` and its scored deflection is at or below
	//         the running best, which starts at `Delta`. A second trace confirms line of sight to
	//         the winner.
	// STEP 5. The winner's offset from the current aim, minus the caller's own eye/bone offsets, is
	//         the answer; `FL_AIMTARGET` on the winner raises `m_fOnTarget`. No winner answers the
	//         zero deflection.
	//
	// **Unrecovered:** the SCORE. Retail's is
	// `(sqrt(|d|^2)/fStack_8 * _DAT_10449198 + _DAT_10449280) * (|dot_up| + |dot_right| *
	// _DAT_10449270)` and three of those four constants are shared `.rdata` words with no value in
	// the corpus, while `fStack_8` arrives in the caller's frame. The SHAPE — a distance term times
	// an off-axis term, smaller is better — is recovered and reproduced; the weights are not, so the
	// port scores on the off-axis term alone and says so.
	bOnTarget = false;
	if (!GameRulesAutoAimEnabled())
	{
		return FRotator::ZeroRotator;
	}

	const FRotator AimAngles = LocalPunchAngle + EyeAngle + AutoAim;
	const FVector Look = ChainAngleForward(AimAngles);

	if (FElysiumEntity* Hit = TraceAimRay(Src, Look, Distance))
	{
		if (ChainTakesDamage(*Hit) && !AutoaimWaterLevelBlocks(*Hit))
		{
			if ((ChainEntityFlags(*Hit) & GChain2FlAimTarget) != 0)
			{
				bOnTarget = true;
			}
			return FRotator::ZeroRotator;
		}
	}

	TArray<FElysiumEntity*> Candidates;
	ChainEntityList(Candidates);

	FElysiumEntity* Best = nullptr;
	FVector BestPoint = FVector::ZeroVector;
	float BestScore = Delta;
	for (FElysiumEntity* Candidate : Candidates)
	{
		if (Candidate == nullptr || Candidate == this)
		{
			continue;
		}
		if (!GameRulesAllowsAutoAimAt(*Candidate))
		{
			// Retail's `(*DAT_1070ba0c + 0x80)(this, edict)` is a REQUIRED gate, not an override:
			// a candidate the rules do not admit never reaches the rest of the screen.
			continue;
		}
		if ((ChainEntityFlags(*Candidate) & GChain2FlAimTarget) == 0)
		{
			continue;
		}
		FElysiumNpc* CandidateNpc = Candidate->AsNpc();
		if (CandidateNpc == nullptr || !CandidateNpc->IsAlive())
		{
			continue;
		}
		if (AutoaimWaterLevelBlocks(*Candidate))
		{
			continue;
		}
		const bool bHostile = IRelationType(Candidate) == GChain2DispositionHate;
		const bool bIsPlayer = Candidate->AsCombatCharacter() != nullptr
			&& World != nullptr && World->FindPlayer() == Candidate->AsCombatCharacter();
		if (!bHostile && !bIsPlayer)
		{
			continue;
		}
		// Slot 197 `BodyTarget` on the CANDIDATE (`+0x314`), not on me: retail asks each candidate
		// where it wants to be shot.
		const FVector Point = CandidateNpc->BodyTarget(Src, true, false);
		const FVector ToTarget = Point - Src;
		if (FVector::DotProduct(ToTarget, Look) < GChain2Zero)
		{
			continue;
		}
		// The recovered SHAPE of the score: the off-axis magnitude, smaller is better. The three
		// weight constants are unrecovered (see above), so the distance term is omitted rather than
		// weighted with an invented number.
		const FVector Offaxis = ToTarget.GetSafeNormal() - Look;
		const float Score = FMath::Abs(Offaxis.Y) + FMath::Abs(Offaxis.Z);
		if (Score <= BestScore)
		{
			if (TraceAimRay(Src, ToTarget.GetSafeNormal(), Distance) == nullptr)
			{
				// Retail admits on `fStack_80 == _DAT_10449280` (the second trace hit nothing) OR
				// the trace's own entity being the candidate. The seam's null trace is the first.
				BestScore = Score;
				Best = Candidate;
				BestPoint = Point;
			}
		}
	}

	if (Best == nullptr)
	{
		return FRotator::ZeroRotator;
	}
	if ((ChainEntityFlags(*Best) & GChain2FlAimTarget) != 0)
	{
		bOnTarget = true;
	}
	return (BestPoint - Src).Rotation() - AimAngles;
}

bool FElysiumNpc::AutoaimWaterLevelBlocks(const FElysiumEntity& Candidate) const
{
	// The water-level pair both autoaim bodies screen on, spelled once. Retail's two arms are
	// `m_nWaterLevel == 3` on one side and `candidate->m_nWaterLevel == 0` / `== 3` on the other,
	// which together mean: you cannot autoaim from dry land at something fully submerged, and you
	// cannot autoaim from under water at something fully dry.
	const int32 Mine = WaterLevel;
	const int32 Theirs = Candidate.WaterLevel;
	if (Mine == 3)
	{
		return Theirs == 0;
	}
	return Theirs == 3;
}

// -------------------------------------------------------------------------------------------------
// The held use entity — `0x1017c6d0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ClearUseEntity()
{
	// 0x1017c6d0 — `CBasePlayer::ClearUseEntity`, and the shape is worth reading twice because the
	// guard and the clear are not the same guard:
	//
	//     if (m_hUseEntity != 0 && the one-shot at +0x1ec0 is set) {
	//         resolve m_hUseEntity through the engine's PEntityOfEntIndex (+0x98) -> edict ->
	//             networkable (+0x40) -> GetBaseEntity (+0x10);
	//         if (that resolved) {
	//             the one-shot is CLEARED;
	//             AcceptInput(<name>, this, this)  — slot 118 (+0x1d8);
	//         }
	//     }
	//     m_hUseEntity = 0;                       // OUTSIDE every guard, on every path
	//
	// So a release with the one-shot already down still drops the held entity, silently. And the
	// engine resolve is performed TWICE in the decompilation — once for the guard and once for the
	// networkable hop — with the second one able to answer differently.
	//
	// **Unrecovered:** the input's NAME. `DAT_10555f7c` has 13 referrers, among them
	// `datamap_CBaseEntity_builder`, `CBaseDoor::Use` and `CBasePlayer::PlayerUse`, which makes it an
	// input `CBaseEntity` itself registers on every class; the string's bytes are not in the corpus.
	// The port fires nothing rather than guessing `"Use"`, and records the refusal.
	if (UseEntityIndex != 0 && bUseEntityNotify)
	{
		FElysiumEntity* Held = EntityOfEdict(reinterpret_cast<const void*>(
			static_cast<UPTRINT>(static_cast<uint32>(UseEntityIndex))));
		if (Held != nullptr)
		{
			bUseEntityNotify = false;
			UnrecoveredChainCalls.Add(TEXT("0x1017c6d0 AcceptInput(DAT_10555f7c)"));
		}
	}
	UseEntityIndex = 0;
}

// -------------------------------------------------------------------------------------------------
// The camera-override crossfade — `0x1017d900` and `0x1017d680`.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::CameraOverrideFadeFraction()
{
	// 0x1017d900. The gate is `m_flCameraOverrideFadeMarkTime > 0` **AND** at least one of
	// `m_hCameraViewEntity` (+0x19c0) and `m_hCameraTargetEntity` (+0x19cc) resolving to a live
	// entity — either one is enough, which is what lets a shot that has only a target keep fading.
	//
	// Inside, on `now = (*DAT_1070b22c + 0x1dc)(0)` (the engine's own clock, not curtime):
	//   duration == 0  -> 1.0, immediately: a zero-length fade is already finished.
	//   duration > 0   -> clamp((now - mark) / duration, 0, 1).
	//   duration < 0   -> clamp(1 + (now - mark) / duration, 0, 1) — the count-DOWN arm, which
	//                     starts at 1 and falls; and its UNDERFLOW (below 0) does not clamp, it
	//                     falls through to the reset.
	//
	// The reset arm — taken by the gate's refusal and by that underflow — zeroes `+0x19b8`,
	// `+0x19bc`, `+0x19e4` and both handles, and answers 0. It is a side effect of a QUERY, which is
	// why `0x1017d680` calls this before it resolves its handle: reading the fraction is what tears
	// the finished fade down.
	const bool bViewLive = World != nullptr && CameraViewEntity.IsSet()
		&& World->Resolve(CameraViewEntity) != nullptr;
	const bool bTargetLive = World != nullptr && CameraTargetEntity.IsSet()
		&& World->Resolve(CameraTargetEntity) != nullptr;

	if (CameraOverrideFadeMarkTime > GChain2Zero && (bViewLive || bTargetLive))
	{
		const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
		if (CameraOverrideFadeDuration == GChain2Zero)
		{
			return GChain2One;
		}
		if (CameraOverrideFadeDuration > GChain2Zero)
		{
			const float Fraction = (Now - CameraOverrideFadeMarkTime) / CameraOverrideFadeDuration;
			if (Fraction > GChain2One)
			{
				return GChain2One;
			}
			if (Fraction >= GChain2Zero)
			{
				return Fraction;
			}
			return GChain2Zero;
		}
		// Duration < 0 — the count-down arm.
		const float Fraction = GChain2One
			+ (Now - CameraOverrideFadeMarkTime) / CameraOverrideFadeDuration;
		if (Fraction > GChain2One)
		{
			return GChain2One;
		}
		if (Fraction >= GChain2Zero)
		{
			return Fraction;
		}
		// Below zero on the count-down arm falls THROUGH to the reset, unlike the count-up arm.
	}

	CameraOverrideFadeMarkTime = 0.f;
	CameraOverrideFadeDuration = 0.f;
	CameraViewEntity = FElysiumEntityHandle::Invalid();
	CameraTargetEntity = FElysiumEntityHandle::Invalid();
	Field_0x19e4 = 0;
	return GChain2Zero;
}

FElysiumEntity* FElysiumNpc::ResolveCameraTargetEntity()
{
	// 0x1017d680 — run the crossfade query FIRST (for the teardown side effect above), then resolve
	// `m_hCameraTargetEntity` (+0x19cc) and answer the entity or 0. The sibling `0x1017d630` is the
	// same body over `m_hCameraViewEntity` (+0x19c0) and is not this family's row.
	//
	// The ORDER matters and is the point: after a finished fade, the query has already cleared the
	// handle, so this answers null on the very call that observes the fade ending.
	CameraOverrideFadeFraction();
	if (World == nullptr || !CameraTargetEntity.IsSet())
	{
		return nullptr;
	}
	return World->Resolve(CameraTargetEntity);
}

// -------------------------------------------------------------------------------------------------
// The closest-NPC cache — `0x101828b0` and `0x10182a90`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::UpdateClosestNpc(FElysiumEntity* Candidate, float Distance)
{
	// 0x101828b0 — the private cache behind `AddLookTarget`, over `m_hClosestNPC` (+0x1cc0),
	// `m_flClosestNPCDist` (+0x1cc4) and `m_iClosestNPCSense` (+0x1cc8).
	//
	// STEP 1. Resolve the cached handle. If it does not resolve, RESET the triple to
	//         (invalid, 100000.0, 0) — `0x47c34ff3` — and carry on with a null cached pointer.
	// STEP 2. Three arms:
	//   (a) the cached entity IS the candidate: if slot 158 says it is alive, refresh the DISTANCE
	//       and recompute the sense; if it is dead, reset the triple. Note the asymmetry — this arm
	//       accepts ANY distance, including a larger one, because the nearest NPC staying nearest is
	//       not a comparison.
	//   (b) the cached entity is a DIFFERENT entity that is now dead: reset the triple, then fall
	//       into (c).
	//   (c) the acceptance ladder, and every rung is required:
	//         Distance strictly LESS than the cached distance;
	//         the candidate is alive (slot 158);
	//         `candidate->+0x19c & 0x40` is CLEAR (an unrecovered per-entity refusal bit);
	//         `GetModelPtr()` is non-null;
	//         `IRelationType(me) == D_HT` — the candidate hates ME, not the other way round;
	//         `candidate->GetFollowerBoss() != me` (slot 293, `+0x494`) — my own follower is never
	//           my nearest threat;
	//         `thunk_FUN_100b5190(candidate)` is FALSE.
	//       Only then is the handle stored, the distance stored and the sense recomputed.
	//
	// **Unrecovered:** the `0x40` bit at `candidate+0x19c` and the `0x100b5190` predicate. Both are
	// seams below and both answer the ADMITTING value, so the ladder is as permissive as it can
	// honestly be rather than silently refusing everything.
	FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr || Candidate == nullptr)
	{
		return;
	}

	FElysiumEntity* Cached = World != nullptr && Player->Observer.Observer.IsSet()
		? World->Resolve(Player->Observer.Observer) : nullptr;
	if (Cached == nullptr)
	{
		Player->Observer.Observer = FElysiumEntityHandle::Invalid();
		Player->Observer.DistanceCm = GChain2ClosestNpcReset;
		Player->Observer.Meter = 0.f;
	}

	FElysiumNpc* CandidateNpc = Candidate->AsNpc();
	const bool bCandidateAlive = CandidateNpc != nullptr && CandidateNpc->IsAlive();

	if (Cached == Candidate)
	{
		if (bCandidateAlive)
		{
			Player->Observer.DistanceCm = Distance;
			Player->Observer.Meter = static_cast<float>(ClosestNpcSense());
			return;
		}
		Player->Observer.Observer = FElysiumEntityHandle::Invalid();
		Player->Observer.DistanceCm = GChain2ClosestNpcReset;
		Player->Observer.Meter = 0.f;
		return;
	}

	if (Cached != nullptr)
	{
		FElysiumNpc* CachedNpc = Cached->AsNpc();
		if (CachedNpc == nullptr || !CachedNpc->IsAlive())
		{
			Player->Observer.Observer = FElysiumEntityHandle::Invalid();
			Player->Observer.DistanceCm = GChain2ClosestNpcReset;
			Player->Observer.Meter = 0.f;
		}
	}

	if (!(Distance < Player->Observer.DistanceCm))
	{
		return;
	}
	if (!bCandidateAlive)
	{
		return;
	}
	if (ClosestNpcCandidateBitSet(*Candidate))
	{
		return;
	}
	if (!HasStudioModel(*Candidate))
	{
		return;
	}
	if (CandidateNpc->IRelationType(this) != GChain2DispositionHate)
	{
		return;
	}
	if (CandidateNpc->GetFollowerBoss() == this)
	{
		return;
	}
	if (ClosestNpcCandidateRefused(*Candidate))
	{
		return;
	}
	Player->Observer.Observer = Candidate->Handle;
	Player->Observer.DistanceCm = Distance;
	Player->Observer.Meter = static_cast<float>(ClosestNpcSense());
}

bool FElysiumNpc::ClosestNpcCandidateBitSet(const FElysiumEntity& Candidate) const
{
	// **SEAM** for `candidate + 0x19c & 0x40`, the per-entity bit whose SET state refuses a
	// candidate. Neither `CBaseEntity`'s datamap nor `layout.md` names `+0x019c`, so the bit's
	// concern is **unrecovered**. False is the admitting arm.
	(void)Candidate;
	return false;
}

int32 FElysiumNpc::ClosestNpcSense() const
{
	// 0x10182a90 — the sense value the cache stores beside the NPC.
	//
	//   scalar = thunk_FUN_101e9000(0x10739d08)              // an int, unrecovered cvar
	//   npc    = resolve(m_hClosestNPC); refuse to 0 if dead
	//   a      = thunk_FUN_1029c970(npc)                     // the NPC's own perception term
	//   b      = npc->+0x628c resolves ? fold(a, me) : a     // folded through the NPC's own target
	//   c      = thunk_FUN_1029ca30(b)
	//   if (m_flClosestNPCDist < c)   return 0x264;          // OVER the threshold: a fixed code
	//   if (m_flClosestNPCDist <= a)  return (int)(scalar * b * _DAT_10450aa4)
	//                                      | (m_flClosestNPCDist < that) << 8;
	//   return 0;
	//
	// `0x264` is a MAGIC ANSWER, not a number the caller does arithmetic with: the middle arm packs
	// a truncated float in the low byte and a boolean in bit 8, and `0x264` collides with neither
	// encoding by accident — it is bit 9 plus 0x64.
	//
	// SEAM: the three perception folds are `ClosestNpcPerception`, which answers the port's
	// COMMITTED observation meter; the scalar is `ClosestNpcSenseScalar`, which answers 0 and so
	// zeroes the middle arm's product. With a zero product the packed answer is 0 unless the stored
	// distance is negative, which it never is.
	const FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr || !Player->Observer.Observer.IsSet() || World == nullptr)
	{
		return 0;
	}
	const FElysiumEntity* Npc = World->Resolve(Player->Observer.Observer);
	if (Npc == nullptr)
	{
		return 0;
	}
	const float A = ClosestNpcPerception(*Npc);
	const float C = A;   // the two folds are one seam here; retail's `c` derives from `a`
	if (Player->Observer.DistanceCm < C)
	{
		return GChain2ClosestNpcOverThreshold;
	}
	if (Player->Observer.DistanceCm <= A)
	{
		const float Scaled = static_cast<float>(ClosestNpcSenseScalar()) * A;
		const int32 Packed = static_cast<int32>(Scaled);
		return Packed | ((Player->Observer.DistanceCm < Scaled) ? (1 << 8) : 0);
	}
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The player-animation arm — `0x10182c40`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetPlayerAnim(const TCHAR* Name, const TCHAR* Sound)
{
	// 0x10182c40 — arm a player animation for the length of a spoken line.
	//
	// STEP 1. A hand-rolled `strcpy` of `Name` into the GLOBAL buffer `DAT_10724ef0`. One buffer for
	//         the whole process — a second call overwrites the first caller's name in place, which a
	//         program can observe.
	// STEP 2. `+0x1ca4 = (DAT_10724ef0 != '\0') ? &DAT_10724ef0 : 0` — the pointer is the buffer's
	//         address when the COPIED name is non-empty, and literal 0 when it is not. So an empty
	//         name still wipes the buffer and then disarms the pointer.
	// STEP 3. `+0x1ca8 = curtime - _DAT_10449270`.
	// STEP 4. If `m_hDialogPartner` (+0x0fe8) resolves live, that partner carries an NPC
	//         (`partner+0x98 != 0`) and `Sound` is a non-empty string, ADD the sound's length
	//         (`(*DAT_1070b248 + 0x30)(sound)`); otherwise add `_DAT_10471720`, the fallback.
	// STEP 5. `+0x1cac |= 1`.
	//
	// Both constants are DOUBLES (`10182c76 FSUB double ptr [0x10449270]`, 0.5, and
	// `10182d07 FADD double ptr [0x10471720]`, 0.6). The subtraction is `FST`ored to `+0x1ca8` as a
	// float while the x87 keeps the unrounded value: the sound arm adds the duration to the STORED
	// float (`10182cff FADD float ptr [ESI + 0x1ca8]`), the fallback arm adds 0.6 to the unrounded
	// one. Both then `FSTP float`.
	PlayerAnimNameBuffer = Name != nullptr ? FString(Name) : FString();
	bPlayerAnimNameSet = !PlayerAnimNameBuffer.IsEmpty();

	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	const double LeadIn = static_cast<double>(Now) - PlayerAnimLeadIn();
	float End = static_cast<float>(LeadIn);

	// `m_hDialogPartner` (+0x0fe8) resolving live is family Anim's `HasLiveDialogPartner()`, which
	// is the reading `ElysiumNpcKernelSounds.cpp` already made for the same word and is reused
	// rather than re-derived. Retail's SECOND test on the partner — `partner+0x98 != 0`, "it carries
	// a `CAI_BaseNPCTroika`" — cannot be made separately here, because the port carries the partner
	// as the open dialogue SESSION and not as an entity handle the kernel can hop off; the two tests
	// collapse into one and that is stated rather than silently dropped.
	bool bUsedSound = false;
	if (HasLiveDialogPartner() && Sound != nullptr && *Sound != TEXT('\0'))
	{
		End += SoundDurationOf(Sound);
		bUsedSound = true;
	}
	if (!bUsedSound)
	{
		End = static_cast<float>(LeadIn + PlayerAnimFallbackDuration());
	}

	PlayerAnimEndTime = End;
	PlayerAnimFlags |= 1;
}

double FElysiumNpc::PlayerAnimLeadIn() const
{
	// `_DAT_10449270`, the pooled double 0.5, subtracted from curtime.
	return ElysiumNpcTunables::HalfDouble;
}

double FElysiumNpc::PlayerAnimFallbackDuration() const
{
	// `_DAT_10471720`, the double 0.6: how long a whisper with no sound stays up.
	return ElysiumNpcTunables::WhisperNoSoundFallback;
}

// -------------------------------------------------------------------------------------------------
// The controller-NPC detach — `0x101618e0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ReleaseControllerNpc(bool bCopyAnimation, bool bCopyVelocity)
{
	// 0x101618e0 — hand the body back from an `npc_VPlayerController`.
	//
	// GATE: `m_hControllerNPC` (+0x1db0) must resolve to a live entity. Nothing happens otherwise —
	// not even the handle clear, which is INSIDE the gate.
	//
	// `bCopyAnimation` additionally requires the controller to carry an NPC (`controller+0x98`),
	// and then copies, in retail's order:
	//   +0x6f0 m_nSequence, +0x174 m_flAnimTime, +0x6f8 m_flCycle, +0x6f4 m_flPlaybackRate,
	//   0xc0 bytes from +0x734 — the WHOLE four-record gesture-layer table, and
	//   0x54 bytes from +0x7f4 — the WHOLE three-record flinch table.
	// Both blocks are `rep movsd` bulk copies, so every field travels, including the ones no body
	// on this side ever writes.
	//
	// `bCopyVelocity` transfers the controller's absolute linear and angular velocity: slot 219
	// (`+0x36c`) for the angular source, then slot 64 (`+0x100`) on ME, then — only when the
	// controller's `m_iEFlags` bit 0xc is set — `CalcAbsoluteVelocity` on the controller to bring
	// its cached value up to date, then `SetAbsVelocity(controller->+0x3bc)` and slot 62 (`+0xf8`)
	// with slot 217's (`+0x364`) answer.
	//
	// Then, UNCONDITIONALLY and outside both flags:
	//   ThinkSet(controller, 0x101c0b10, 0.0, nullptr);
	//   controller->m_flNextThink = curtime + _DAT_1044e658;
	//   m_hControllerNPC = -1;
	//
	// **Unrecovered:** what `0x101c0b10` does. The think is armed through the port's own
	// `NextThink`, at curtime plus `_DAT_1044e658` (the double 0.01).
	if (World == nullptr || !ControllerNpc.IsSet())
	{
		return;
	}
	FElysiumEntity* Controller = World->Resolve(ControllerNpc);
	if (Controller == nullptr)
	{
		return;
	}
	FElysiumNpc* ControllerNpcLeaf = Controller->AsNpc();

	if (bCopyAnimation && ControllerNpcLeaf != nullptr)
	{
		SequenceNumber = ControllerNpcLeaf->SequenceNumber;        // +0x6f0
		AnimTime = ControllerNpcLeaf->AnimTime;                    // +0x174
		SequenceCycle = ControllerNpcLeaf->SequenceCycle;          // +0x6f8
		SequencePlaybackRate = ControllerNpcLeaf->SequencePlaybackRate;   // +0x6f4
		for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
		{
			AnimOverlay[Index] = ControllerNpcLeaf->AnimOverlay[Index];   // +0x734, 0xc0 bytes
		}
		for (int32 Index = 0; Index < NumFlinchRecords; ++Index)
		{
			Flinch[Index] = ControllerNpcLeaf->Flinch[Index];       // +0x7f4, 0x54 bytes
		}
	}

	if (bCopyVelocity)
	{
		// Retail reads the controller's CACHED absolute velocity at `controller+0x3bc` and
		// recomputes it first only when the dirty bit is set. The port's `Velocity` /
		// `AngularVelocity` are never cached-and-dirty, so the recompute has nothing to do and the
		// transfer is the two reads.
		Velocity = Controller->Velocity;
		AngularVelocity = Controller->AngularVelocity;
	}

	// The one-shot think on the CONTROLLER — retail arms it on the entity it is letting go of, not
	// on itself.
	Controller->NextThink = (World != nullptr ? World->NowSeconds() : 0.0)
		+ ControllerReleaseThinkDelay();
	ControllerNpc = FElysiumEntityHandle::Invalid();
}

double FElysiumNpc::ControllerReleaseThinkDelay() const
{
	// `_DAT_1044e658`, the double 0.01 the released controller's one-shot think is armed at.
	return ElysiumNpcTunables::HundredthDouble;
}
