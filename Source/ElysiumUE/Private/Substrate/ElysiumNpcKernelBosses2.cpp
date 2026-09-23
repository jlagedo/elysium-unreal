#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Bosses**, second half — `CNPC_VMingXiao`'s tentacle rules and its pedestal
// pick. `Substrate/ElysiumNpcKernelBosses.cpp` carries the seams, `CNPC_VHengeyokai`'s pickup chain,
// `CNPC_VManBat`'s flight and carry chain and the `CNPC_VAndreiBlood` line's melee-slot bodies, and
// states the family's three standing facts; the declarations are
// `Substrate/ElysiumNpcKernelBosses.inl`. Split because the family passed ~1,500 lines, per the
// brief.
//
// The constants are re-declared here rather than exported, because a constant is only useful beside
// the body that cites its address, and under a distinct prefix because a unity build concatenates
// two anonymous namespaces into one.

namespace
{
	constexpr float Bosses2Zero = ElysiumNpcTunables::Zero;
	constexpr float Bosses2One = ElysiumNpcTunables::One;

	// `0x10398030`'s distance bands against `m_flClosestPlayerDistance` (+0x6264), SOURCE units.
	constexpr float MingXiaoNearBand = ElysiumNpcTunables::Hundred;
	constexpr float MingXiaoFarBand300 = 300.0f;      // _DAT_10462b84
	constexpr float MingXiaoFarBand200 = 200.0f;      // _DAT_104492b8
	constexpr float MingXiaoReachBand = 150.0f;       // _DAT_10457f60

	// `0x10398030`'s tentacle schedule ids, slots 0..3.
	constexpr int32 MingXiaoTentacleSchedules[4] = { 0x112a, 0x112b, 0x112c, 0x112d };

	// `0x10396dc0`'s two conditions and the two schedules they answer.
	constexpr int32 MingXiaoCondGrabA = 0x7b;
	constexpr int32 MingXiaoCondGrabB = 0x7c;
	constexpr int32 MingXiaoSchedGrabA = 0x15c;
	constexpr int32 MingXiaoSchedGrabB = 0x15d;

	// `0x103983d0`'s out-of-range answer — the same shared 20.0 cell as the pickup cone's upper edge.
	constexpr float MingXiaoThrowDefault = 20.0f;     // _DAT_1044eb0c

	// `0x103989b0`'s abeam test.
	constexpr float PedestalHeightTolerance = static_cast<float>(ElysiumNpcTunables::SixtyFourDouble);
	constexpr float PedestalForwardLo = -0.17f;       // _DAT_104bde64
	constexpr float PedestalForwardHi = ElysiumNpcTunables::Half;

	// `0x10398b20`'s search radius, its stationary tolerance and its name prefix.
	constexpr float PedestalSearchRadius = 257.0f;
	constexpr float PedestalStationaryTolerance = 0.1f;   // _DAT_104491b4
	constexpr const TCHAR* PedestalNamePrefix = TEXT("Pedestal");
	constexpr int32 PedestalNamePrefixLength = 8;         // retail's `__strnicmp(..., 8)`

	// `VectorNormalize` `0x10137220`: `1.0 / (FLT_EPSILON + length)`, so a zero vector normalizes to
	// zero rather than to NaN and a unit vector comes back a hair short. Both are observable, and the
	// first is what makes `PedestalTaskForSide` answer for a candidate standing on top of the boss.
	constexpr float Bosses2NormalizeEpsilon = ElysiumNpcTunables::FloatEpsilon;

	FVector Bosses2Normalize(const FVector& V)
	{
		const float Scale = Bosses2One / (Bosses2NormalizeEpsilon + static_cast<float>(V.Size()));
		return V * Scale;
	}
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VMingXiao` — the tentacle rules.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::IsTentacleConnected(int32 TentacleId) const
{
	// `0x10398000`: `(m_iSeveredTentacleMask & (1 << (n & 0x1f))) == 0`.
	return (MingXiaoSeveredTentacleMask & (1u << (static_cast<uint32>(TentacleId) & 0x1fu))) == 0u;
}

bool FElysiumNpc::IsMingXiaoProxy() const
{
	// `0x10398870`: `m_iTentacleID != -1`.
	return MingXiaoTentacleId != INDEX_NONE;
}

int32 FElysiumNpc::FUN_10396dc0() const
{
	// `0x10396dc0`, the whole body:
	//     if (m_hThrowObject resolves to a live entity
	//         && 2 < m_eThrowableObjectMode && m_eThrowableObjectMode < 5) {
	//         if (HasCondition(0x7b)) { <selector trace>; return 0x15c; }
	//         if (HasCondition(0x7c)) { <selector trace>; return 0x15d; }
	//     }
	//     return 0;
	// The band is STRICT on both sides — modes 3 and 4 only. The selector trace writes retail's
	// `__FILE__`/`__LINE__` into `+0x1b30`/`+0x1b34`, which the shape map records as ABSENT here.
	if (World == nullptr || World->Resolve(MingXiaoThrowObject) == nullptr)
	{
		return 0;
	}
	if (!(2 < MingXiaoThrowableObjectMode && MingXiaoThrowableObjectMode < 5))
	{
		return 0;
	}
	if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(MingXiaoCondGrabA)))
	{
		return MingXiaoSchedGrabA;
	}
	if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(MingXiaoCondGrabB)))
	{
		return MingXiaoSchedGrabB;
	}
	return 0;
}

void FElysiumNpc::SeverTentacle(int32 TentacleId)
{
	// SEAM for `0x10397930`, which is no family's row. The two writes this substrate can make are
	// made (`m_rhProxies[id]` and `m_rhSeveredTentacles[id]`, family Squad's members); the hit
	// points, the attack and regrow timers, `m_rbProxyRegistered` and the bodygroup set are records.
	LastSeveredTentacle = TentacleId;
	if (TentacleId >= 0 && TentacleId < 6)
	{
		SeveredTentacles[TentacleId] = FElysiumEntityHandle::Invalid();
		Proxies[TentacleId] = FElysiumEntityHandle::Invalid();
		// `m_rflAttackTimers[id] = curtime + _DAT_1044e664` — that cell lives past `.data`'s raw
		// size and is **unrecovered**, so the stamp is left alone rather than guessed.
	}
}

void FElysiumNpc::FUN_10397a50(const FElysiumEntity* Tentacle,
	TFunctionRef<float(int32)> TuningField)
{
	// `0x10397a50`:
	//     if (param_1 == NULL) return;
	//     f = Tuning[100] + Tuning[0x68] * (6 - m_iConnectedTentacleCount);
	//     if (f <= 0.0) f = 0.0;                                       // _DAT_104454c4
	//     m_flProxyReadyTimer = f + curtime;                           // +0x66a4
	//     if (Resolve(m_rhProxies[param_1->m_iTentacleID]) == param_1)  // +0x668c indexed by +0x6674
	//         SeverTentacle(param_1->m_iTentacleID);                   // 0x10397930
	// The clamp is `<=`, so an exactly-zero blend also takes the floor — the same value either way,
	// and reproduced as written.
	if (Tentacle == nullptr)
	{
		return;
	}
	float Blend = TuningField(100)
		+ TuningField(0x68) * static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	if (Blend <= Bosses2Zero)
	{
		Blend = Bosses2Zero;
	}
	MingXiaoProxyReadyTimer =
		static_cast<double>(Blend) + (World != nullptr ? World->NowSeconds() : 0.0);

	// Retail reads `param_1 + 0x6674` — the TENTACLE's own `m_iTentacleID`, not this NPC's. This
	// runtime stands one leaf, so the tentacle entity's id is read off it as an NPC when it is one.
	const FElysiumNpc* TentacleNpc = const_cast<FElysiumEntity*>(Tentacle)->AsNpc();
	if (TentacleNpc == nullptr || World == nullptr)
	{
		return;
	}
	const int32 Id = TentacleNpc->MingXiaoTentacleId;
	if (Id < 0 || Id >= 6)
	{
		return;
	}
	if (World->Resolve(Proxies[Id]) == Tentacle)
	{
		SeverTentacle(Id);
	}
}

float FElysiumNpc::FUN_10397f70(TFunctionRef<float(int32)> TuningField) const
{
	// `0x10397f70`:
	//     if (m_iTentacleID != -1) return Tuning[0x20];                 // 0x10398870
	//     f = Tuning[0x6c] + Tuning[0x70] * (6 - m_iConnectedTentacleCount);
	//     if (f <= 0.0) f = 0.0;
	//     return f;
	if (IsMingXiaoProxy())
	{
		return TuningField(0x20);
	}
	const float Blend = TuningField(0x6c)
		+ TuningField(0x70) * static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	return Blend <= Bosses2Zero ? Bosses2Zero : Blend;
}

bool FElysiumNpc::ChooseMeleeAttackSequenceSeam() const
{
	// SEAM for `0x10398030`'s `param_2` tail. Slot 331's Troika body (`0x10347180`) is story 29d's
	// and `m_hMeleeWeapon`'s owner chain has no counterpart here. False is retail's refusal.
	return false;
}

bool FElysiumNpc::FUN_10398030(int32 Slot, bool bTestMelee, int32& OutSchedule)
{
	// `0x10398030`, arm for arm and in retail's order:
	//
	//     if (!IsTentacleConnected(param_1)) return false;              // 0x10398000
	//     if (curtime < m_rflAttackTimers[param_1]) return false;       // +0x66c4 + param_1*4
	//     sched = -1;
	//     switch (param_1) {
	//       case 0: case 1:
	//         if (m_bBlockedByFriend) return false;                     // 0x1039ab10
	//         if (dist < 100.0)  return false;                          // _DAT_10450564
	//         if (dist >= 300.0) return false;                          // _DAT_10462b84
	//         sched = 0x112a + param_1;  break;
	//       case 2: case 3:
	//         if (m_bBlockedByFriend) return false;
	//         if (dist < 100.0)  return false;
	//         if (dist >= 200.0) return false;                          // _DAT_104492b8
	//         sched = 0x112c + (param_1 - 2);  break;
	//       case 4: case 5:
	//         if (m_bBlockedByFriend) return false;
	//         if (dist < 150.0) return false;                           // _DAT_10457f60
	//         if (!m_hThrowObject resolves live) return false;          // +0x6718
	//         if (m_eThrowingTentacle != param_1) return false;         // +0x671c
	//         break;                                                    // sched STAYS -1
	//     }
	//     if (param_2 && sched != -1) { <the melee tail>; }
	//     return true;
	//
	// Two facts worth stating because they look like slips and are not. Slots 4 and 5 leave the
	// schedule at -1, so the `param_2` tail is SKIPPED for them however `param_2` was passed. And
	// `param_1` outside 0..5 falls through the switch with the schedule still -1 and answers TRUE,
	// having passed only the mask and timer gates.
	OutSchedule = INDEX_NONE;
	if (!IsTentacleConnected(Slot))
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Slot >= 0 && Slot < 6 && Now < MingXiaoAttackTimers[Slot])
	{
		return false;
	}
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	switch (Slot)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	{
		if (BlockedByFriend())
		{
			return false;
		}
		if (DistUnits < MingXiaoNearBand)
		{
			return false;
		}
		const float Ceiling = (Slot < 2) ? MingXiaoFarBand300 : MingXiaoFarBand200;
		if (DistUnits >= Ceiling)
		{
			return false;
		}
		OutSchedule = MingXiaoTentacleSchedules[Slot];
		break;
	}
	case 4:
	case 5:
		if (BlockedByFriend())
		{
			return false;
		}
		if (DistUnits < MingXiaoReachBand)
		{
			return false;
		}
		if (World == nullptr || World->Resolve(MingXiaoThrowObject) == nullptr)
		{
			return false;
		}
		if (MingXiaoThrowingTentacle != Slot)
		{
			return false;
		}
		break;
	default:
		break;
	}
	if (bTestMelee && OutSchedule != INDEX_NONE)
	{
		if (!ChooseMeleeAttackSequenceSeam())
		{
			return false;
		}
	}
	return true;
}

float FElysiumNpc::FUN_103983d0(int32 Selector, TFunctionRef<float(int32)> TuningField) const
{
	// `0x103983d0`, recovered from the LISTING: the decompiled C lost the jump table at
	// `0x10398598` and the ST0 return, and read the selector as a return-storage pointer.
	//
	//     switch (selector) {
	//       case 0: case 1:
	//         if (dist > 200.0) <a dead Tuning fetch whose result is discarded>;
	//         scale = dist <= 150.0 ? Tuning[0x38] : Tuning[0x34];
	//         v = Tuning[0x74] + Tuning[0x78] * (6 - count);
	//         if (v <= 0) v = 0;
	//         return v * scale;
	//       case 2: case 3:
	//         scale = dist <= 150.0 ? Tuning[0x40] : Tuning[0x3c];
	//         v = Tuning[0x74] + Tuning[0x78] * (6 - count);
	//         if (v <= 0) v = 0;
	//         return v * scale;
	//       case 4: case 5:
	//         if (dist <= 200.0) { a = Tuning[0x84]; b = Tuning[0x88]; }
	//         else               { a = Tuning[0x7c]; b = Tuning[0x80]; }
	//         v = a + b * (6 - count);
	//         return v <= 0 ? 0 : v;                                   // NO second factor
	//       default: return 20.0;                                      // _DAT_1044eb0c
	//     }
	//
	// Slots 0/1 and 2/3 share the `0x74`/`0x78` blend and differ only in which pair of scale cells
	// the distance picks; slots 4/5 use a different pair entirely and skip the multiply. The dead
	// fetch on slots 0/1 is retail's and is not reproduced: its result is discarded before the next
	// instruction and nothing observes the call.
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	const float Count = static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	switch (Selector)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	{
		const int32 NearCell = (Selector < 2) ? 0x38 : 0x40;
		const int32 FarCell = (Selector < 2) ? 0x34 : 0x3c;
		const float Scale = (DistUnits <= MingXiaoReachBand) ? TuningField(NearCell)
			: TuningField(FarCell);
		float Blend = TuningField(0x74) + TuningField(0x78) * Count;
		if (Blend <= Bosses2Zero)
		{
			Blend = Bosses2Zero;
		}
		return Blend * Scale;
	}
	case 4:
	case 5:
	{
		const int32 BaseCell = (DistUnits <= MingXiaoFarBand200) ? 0x84 : 0x7c;
		const int32 StepCell = (DistUnits <= MingXiaoFarBand200) ? 0x88 : 0x80;
		const float Blend = TuningField(BaseCell) + TuningField(StepCell) * Count;
		return Blend <= Bosses2Zero ? Bosses2Zero : Blend;
	}
	default:
		return MingXiaoThrowDefault;
	}
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VMingXiao`'s pedestal pick — `0x103989b0`, `0x10398b20`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::PedestalTaskForSide(const FVector& DeltaUnits, const FVector& Forward,
	const FVector& Right, bool bTentacle4Connected, bool bTentacle5Connected, int32& OutTask)
{
	// `0x103989b0`, read from the listing because the decompiler dropped the `VectorNormalize` call
	// and turned three `FCOM`s into status-word arithmetic:
	//
	//     d = param_1 - GetAbsOrigin();
	//     if (fabs(d.z) > 64.0) return false;                    // _DAT_1049ae28, a double
	//     d.z = 0;  VectorNormalize(&d);                         // 0x10137220
	//     f = d.x * m_vecForward.x + d.y * m_vecForward.y;
	//     if (f < -0.17) return false;                           // _DAT_104bde64
	//     if (f >  0.5)  return false;                           // _DAT_104454d0
	//     r = d.x * m_vecRight.x + d.y * m_vecRight.y;
	//     if (r <= 0.0) { if (TentacleConnected(5)) { *out = 5; return true; } }
	//     else          { if (TentacleConnected(4)) { *out = 4; return true; } }
	//     return false;
	//
	// The height gate is inclusive at 64 and the forward band inclusive at both edges; `r == 0`
	// takes the tentacle-5 arm. The dot is against a NORMALIZED 2-D direction, so the band is an
	// angle and not a distance — which is what makes `[-0.17, 0.5]` an abeam wedge.
	if (FMath::Abs(DeltaUnits.Z) > PedestalHeightTolerance)
	{
		return false;
	}
	FVector Flat(DeltaUnits.X, DeltaUnits.Y, 0.0);
	Flat = Bosses2Normalize(Flat);
	const float ForwardDot = static_cast<float>(Flat.X * Forward.X + Flat.Y * Forward.Y);
	if (ForwardDot < PedestalForwardLo)
	{
		return false;
	}
	if (ForwardDot > PedestalForwardHi)
	{
		return false;
	}
	const float RightDot = static_cast<float>(Flat.X * Right.X + Flat.Y * Right.Y);
	if (RightDot <= Bosses2Zero)
	{
		if (bTentacle5Connected)
		{
			OutTask = 5;
			return true;
		}
		return false;
	}
	if (bTentacle4Connected)
	{
		OutTask = 4;
		return true;
	}
	return false;
}

bool FElysiumNpc::FUN_103989b0(const FVector& TargetOriginUnits, int32& OutTask) const
{
	// The member form. `m_vecForward` (+0x6290) and `m_vecRight` (+0x629c) are retail's cached
	// basis; NOTHING in this runtime writes them, so both are the zero vector, every dot is 0.0,
	// the forward band admits it (0 is inside `[-0.17, 0.5]`) and the right test takes its `<= 0`
	// arm — tentacle 5 for every candidate inside the height gate. That is the seam's answer, not
	// retail's, and it is stated rather than hidden.
	const FVector DeltaUnits = TargetOriginUnits - Origin / ElysiumMove::U;
	return PedestalTaskForSide(DeltaUnits, Forward, Right, IsTentacleConnected(4),
		IsTentacleConnected(5), OutTask);
}

void FElysiumNpc::MingXiaoPedestalAimPoint(const FVector& PedestalOriginUnits, int32 Task,
	FVector& OutAimPointUnits, FVector& OutForward) const
{
	// SEAM for `0x10398890`:
	//     r  = m_vecRight   * _DAT_1049ae40;
	//     p  = pedestal     + m_vecForward * _DAT_10463584;
	//     out = (task == 4) ? p + r : p - r;
	//     outForward = m_vecForward;
	// Both cells live past `.data`'s raw size and are **unrecovered**; with them reading 0 the aim
	// point is the pedestal's own origin, which is the seam's answer. The SIGN split on task 4 and
	// the forward copy are the recovered half and are ported.
	OutAimPointUnits = PedestalOriginUnits;
	(void)Task;
	OutForward = Forward;
}

FElysiumEntity* FElysiumNpc::FindNearestPedestal(float RadiusUnits, int32& OutTask)
{
	// `0x10398b20`'s search, behind the cvar gate:
	//     best = 257.0;  winner = NULL;  me = GetAbsOrigin();
	//     while ((e = FindEntityInSphere(prev, 6, me, best)) != NULL) {
	//         if (BossBlacklistHolds(e)) continue;                        // 0x10366400
	//         if (strnicmp(e->m_iName, "Pedestal", 8) != 0) continue;
	//         e->GetVelocity(&v, &av);                                     // slot 199
	//         if (fabs(v.x) > 0.1 || fabs(v.y) > 0.1 || fabs(v.z) >= 0.1) continue;
	//         if (!PedestalTask(e->GetAbsOrigin(), &task)) continue;       // 0x103989b0
	//         best = length(me - e->GetAbsOrigin());  winner = e;          // the radius SHRINKS
	//     }
	//
	// Three facts reproduced verbatim. The sphere radius is re-read each iteration, so every
	// accepted winner tightens the search. The Z tolerance is STRICT (`< 0.1`) while X and Y are
	// inclusive (`<= 0.1`) — retail spells the third compare differently from the first two. And the
	// name test is an eight-character case-insensitive PREFIX, so `Pedestal_03` matches.
	FElysiumEntity* Winner = nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	float Best = RadiusUnits;
	const FVector MeUnits = Origin / ElysiumMove::U;
	for (const TUniquePtr<FElysiumEntity>& Owned : World->Entities())
	{
		FElysiumEntity* Candidate = Owned.Get();
		if (Candidate == nullptr || Candidate == this || Candidate->IsInert())
		{
			continue;
		}
		const FVector CandidateUnits = Candidate->Origin / ElysiumMove::U;
		if ((CandidateUnits - MeUnits).SizeSquared() >= static_cast<double>(Best) * Best)
		{
			continue;
		}
		if (BossBlacklistHolds(Candidate))
		{
			continue;
		}
		if (FCString::Strnicmp(*Candidate->TargetName, PedestalNamePrefix, PedestalNamePrefixLength)
			!= 0)
		{
			continue;
		}
		const FVector V = EntityVelocityUnits(*Candidate);
		if (FMath::Abs(V.X) > PedestalStationaryTolerance
			|| FMath::Abs(V.Y) > PedestalStationaryTolerance
			|| !(FMath::Abs(V.Z) < PedestalStationaryTolerance))
		{
			continue;
		}
		int32 Task = OutTask;
		if (!FUN_103989b0(CandidateUnits, Task))
		{
			continue;
		}
		OutTask = Task;
		Best = static_cast<float>((MeUnits - CandidateUnits).Size());
		Winner = Candidate;
	}
	return Winner;
}

FElysiumEntity* FElysiumNpc::FUN_10398b20(int32& InOutTask, FVector& OutAimPointUnits,
	FVector& OutForward)
{
	// `0x10398b20`'s head, in retail's order:
	//     if (m_flClosestPlayerDistance < 150.0) return NULL;             // _DAT_10457f60
	//     if (!TentacleConnected(4) && !TentacleConnected(5)) return NULL;
	//     if (cvar(DAT_1093ba8c) == 0) return NULL;
	//     winner = <the search>;
	//     if (winner) { AimPoint(winner->GetAbsOrigin(), *param_1, &pos, &fwd); return winner; }
	//     return NULL;
	// Note the aim call re-reads `*param_1` — the task the search wrote through the same pointer.
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	if (DistUnits < MingXiaoReachBand)
	{
		return nullptr;
	}
	if (!IsTentacleConnected(4) && !IsTentacleConnected(5))
	{
		return nullptr;
	}
	if (MingXiaoPedestalCvar() == 0)
	{
		return nullptr;
	}
	FElysiumEntity* Winner = FindNearestPedestal(PedestalSearchRadius, InOutTask);
	if (Winner == nullptr)
	{
		return nullptr;
	}
	MingXiaoPedestalAimPoint(Winner->Origin / ElysiumMove::U, InOutTask, OutAimPointUnits,
		OutForward);
	return Winner;
}
