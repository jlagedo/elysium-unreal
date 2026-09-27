// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- `CAI_BaseNPC`'s bodies.
//
// Declarations are in `ElysiumNpcBaseConditions19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. Walked prose:
// `docs/vtmb/npc-ai/story8/Conditions19.md`.
//
// Owns (Conditions19's `rule` rows): 0x10270b20 CAI_BaseNPC::GatherEnemyConditions, 0x1026ec30
// CAI_BaseNPC::GatherConditions.
//
// Absent words, by the landed convention: the scope-trace push/pop (`g_ScopeTraceStack`), the
// `rdtsc` bracket into `DAT_109204a0/a4`, the VProf `EnterScope`/`ExitScope` pairs and the
// `ent_trace_conditions` hook `(*DAT_10924a6c)->vfunc1()` retail calls before a `SetCondition`
// (`ElysiumNpcConditions10.inl`: the ConVar object `0x10924a68`) -- none has an observable here.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// =================================================================================================
// Slot 433 -- `CAI_BaseNPC::GatherConditions` `0x1026ec30`, 987 bytes
// =================================================================================================

double FElysiumNpcBase::Conditions19Now() const
{
	return World != nullptr ? World->NowSeconds() : 0.0;
}

void FElysiumNpcBase::Conditions19FlushDelayedConditions(double Now)
{
	// `0x102cc760`. `list+0x40` is the count; the entries are (condition, float stamp) pairs.
	TArray<TPair<int32, double>>& List = Cognition.DelayedConditions;
	int32 Index = 0;
	int32 End = List.Num();
	while (Index < End)                                                  // 102cc760 `this < piVar1`
	{
		const double Stamp = List[Index].Value;
		// `(t < curtime) == (t == curtime)` skips: both false (t > curtime) or NaN.
		if (!(Stamp <= Now))
		{
			++Index;                                                     // piVar2 += 2
			continue;
		}
		Cognition.Conditions.SetOrdinal(List[Index].Key);                // SetCondition(param_1, *piVar2)
		// `0x102cc730`: count -= 1, and the LAST entry is copied into the vacated slot unless it is
		// that slot; the walk re-examines the same index.
		const int32 Last = End - 1;
		if (Index != Last)
		{
			List[Index] = List[Last];
		}
		List.RemoveAt(Last, 1, EAllowShrinking::No);
		--End;                                                           // piVar1 -= 2
	}
}

void FElysiumNpcBase::Conditions19PushDelayedCondition(int32 Cond, float Delay, double Now)
{
	// `0x102cc6c0`.
	TArray<TPair<int32, double>>& List = Cognition.DelayedConditions;
	if (List.Num() >= Cond19DelayedListCapacity)                         // `CMP [list+0x40],8 / JGE`
	{
		return;
	}
	const double Stamp = Now + static_cast<double>(Delay);
	for (TPair<int32, double>& Entry : List)
	{
		if (Entry.Key == Cond)
		{
			// `0x102cc590`: only an EARLIER stamp replaces the standing one.
			if (Stamp < Entry.Value)
			{
				Entry.Value = Stamp;
			}
			return;
		}
	}
	List.Add(TPair<int32, double>(Cond, Stamp));                        // `0x102cc560`, count += 1
}

bool FElysiumNpcBase::Conditions19ClientInPvs() const
{
	// SEAM (`0x101d1800`, `UTIL_FindClientInPVS`): the live player stands in for the PVS client.
	const FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;
	return Player != nullptr && !Player->IsInert();
}

void FElysiumNpcBase::Conditions19PerformSensing(double Now)
{
	// `0x1026e4f0`. `if (m_iIsOblivious < 1) CAI_Senses::PerformSensing(m_pSenses);`
	if (!IsOblivious())                                                  // 1026e55a / 1026e562 `+0x5bb4`
	{
		if (FElysiumNpc* const Troika = AsNpc())                         // 1026e56a m_pSenses
		{
			// `CAI_Senses::PerformSensing` `0x10310710`: the `m_bCanPerformSenses` gate, `Look` and
			// `Listen` -- `FElysiumNpcSenses::PerformSensing`, the port's `Tick` without its LOS
			// debounce (that is slot 481's, run by the caller after this).
			if (Senses.PerformSensing(*Troika, Now))
			{
				// `Look`'s tail, slot 469 `OnLooked`: the base body `0x1026a2c0` (`BaseOnLooked`,
				// the six-entry SEE clear and the classifier). The Troika half of slot 469
				// (`0x102b39a0`, `++m_iEnemySightings` under `NEW_ENEMY`) is already folded into
				// `TickSight`'s tail, so the base body is what remains to run here.
				BaseOnLooked();
				// `Listen`'s tail, slot 470 `OnListened`'s base body `0x1026a5e0`: `ClearConditions
				// (0x105c97b4, 10)` -- the HEAR family plus SMELL -- then the delayed-sound flush's
				// `SetCondition`s. The port's `TickHearing` builds the promoted set into
				// `HeardConditions` (`+0x5ca8`), so the flush is the OR of that set.
				static const EElysiumNpcCond HearTable[] = {
					EElysiumNpcCond::HearDanger, EElysiumNpcCond::HearThumper,
					EElysiumNpcCond::HearBugbait, EElysiumNpcCond::HearCombat,
					EElysiumNpcCond::HearWorld, EElysiumNpcCond::HearPlayer,
					EElysiumNpcCond::HearBulletImpact, EElysiumNpcCond::HearPhysicsDanger,
					EElysiumNpcCond::HearFlinch, EElysiumNpcCond::Smell,
				};
				for (const EElysiumNpcCond Cond : HearTable)
				{
					Cognition.Conditions.Clear(Cond);
				}
				Cognition.Conditions |= HeardConditions;
			}
		}
	}
	RemoveIgnoredConditions();                                           // 1026e573 slot 459 `+0x72c`
}

FElysiumEntity* FElysiumNpcBase::Conditions19WeaponFindUsable(const FVector& RangeUnits)
{
	(void)RangeUnits;
	++Conditions19WeaponFindUsableCalls;
	return nullptr;
}

bool FElysiumNpcBase::Conditions19BetterWeaponAvailable(double Now)
{
	// `0x1026fb40`.
	if ((CapabilitiesGet() & Cond19CapWeaponSearch) == 0)                // 1026fb48 slot 513 / 1026fb53
	{
		return false;
	}
	// `FCOMP [+0x5da0]` against curtime, `TEST AH,5 / JP`: only strictly-before proceeds.
	if (!(NextWeaponSearchTime < Now))                                   // 1026fb69
	{
		return false;
	}
	NextWeaponSearchTime = Now + Cond19WeaponSearchInterval;             // 1026fb76
	const bool bHasActiveWeapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		&& World->Resolve(Inventory.ActiveWeapon) != nullptr;             // 1026fb7c GetActiveWeapon
	if (bHasActiveWeapon)                                                // 1026fb83
	{
		return false;
	}
	// `(300, 300, 100)` -- `0x43960000`, `0x43960000`, `0x42c80000` stored at `1026fb89..1026fb9c`.
	const FVector Range(300.0, 300.0, ElysiumNpcTunables::Hundred);
	return Conditions19WeaponFindUsable(Range) != nullptr;               // 1026fba4 / 1026fbab
}

void FElysiumNpcBase::Conditions19CheckTarget(FElysiumEntity* TargetEntity)
{
	(void)TargetEntity;
	++Conditions19CheckTargetCalls;
}

void FElysiumNpcBase::GatherConditions()
{
	// `0x1026ec30`. Skeleton sites with no port line, by the absent-words convention: the
	// scope-trace name pick (branches 0x1026ec3a 0x1026ec44); the `CAI_BaseNPC::IdleSound` VProf
	// scope (calls 0x1026ed41 0x1026edd2, branches 0x1026edc0 0x1026edca 0x1026edda); the
	// `ent_trace_conditions` read before `SetCondition(0x67)` (call 0x1026ee34).
	const double Now = Conditions19Now();
	// `m_bConditionsGathered (+0x5ca4) = 1` -- the only field this body writes itself
	// (`1026eca9`). The shape map binds `+0x5ca4` to `Cognition.GatheredAt`, the port's stamp form of
	// the byte (non-negative = gathered this pass).
	Cognition.GatheredAt = Now;                                          // 1026eca9
	Conditions19FlushDelayedConditions(Now);                             // 1026ecc1 0x102cc760(+0x1a9c)

	const int32 State = NpcStateRetail();                                // 1026ecc6 +0x5cc0
	if (State == 0 || State == 7)                                        // 1026ecce / 1026ecd7
	{
		return;                                                          // -> 1026efb3 epilogue
	}

	if ((static_cast<uint32>(SpawnFlags) & Cond19SpawnflagSenseAlways) != 0   // 1026ece8
		|| Conditions19ClientInPvs()                                     // 1026ecf1 / 1026ecfb
		|| (NpcStateFlags() & 1u) != 0)                                  // 1026ecfd / 1026ed03
	{
		CheckOnGround();                                                 // 1026ed16
		if (ShouldPlayIdleSound())                                       // 1026ed1f slot 509 / 1026ed27
		{
			if ((NpcStateFlags() & 2u) != 0)                             // 1026ed47 / 1026ed51
			{
				IdleAgitatedSound();                                     // 1026ed57 slot 499
			}
			else if (NpcFlags.Has(EElysiumNpcFlag2::D_CALM))             // 1026ed5f..1026ed6f `& 0x10000`
			{
				UpsetSound();                                            // 1026ed75 slot 504
			}
			else if (Schedule.IsRunning()                                // 1026ed7d / 1026ed85 m_pSchedule
				&& GetLocalScheduleId(Schedule.Current)                  // 1026ed8f slot 447 (sched+0x1c)
					== ElysiumNpcCond::ComfortScheduleNumber)            // 1026ed9a `CMP EAX,0x12f`
			{
				ComfortSound();                                          // 1026eda0 slot 503
			}
			else
			{
				IdleSound();                                             // 1026edac slot 490
			}
		}
		Conditions19PerformSensing(Now);                                 // 1026ee04 0x1026e4f0
		if (World != nullptr)
		{
			EnemyMemory.Refresh(*World, Now);                            // 1026ee0d slot 541 / 1026ee15
		}
		// `CAI_BaseNPC::ChooseEnemy` (`0x10279dd0`, Misc19's row, landed by lane L11): every NPC,
		// base-only included.
		ElysiumNpcEnemy::ChooseEnemy(*this);                             // 1026ee1c
		if (Conditions19BetterWeaponAvailable(Now))                      // 1026ee23 / 1026ee2a
		{
			Cognition.Conditions.Set(Cond19BetterWeaponAvailable);       // 1026ee3b SetCondition(0x67)
		}
	}
	else
	{
		ClearSenseConditions();                                          // 1026ed09 slot 477
	}

	// Both paths.
	const FElysiumNpcBase* const ConstThis = this;
	if (ConstThis->GetEnemy() != nullptr)                                // 1026ee44 slot 167 / 1026ee4c
	{
		GatherEnemyConditions(ConstThis->GetEnemy());                    // 1026ee52 / 1026ee5b slot 481
	}

	// `m_hTargetEnt (+0x5ce4)`: `-1`, a serial mismatch or an empty entry skips; a dead entity is
	// still an entry, so the lookup is the dead-inclusive one.
	if (TargetEnt.IsSet() && World != nullptr)                           // 1026ee6a
	{
		const FElysiumEntity* const TargetEntity = ElysiumNpcCond::ResolveEnemyHandle(*World, TargetEnt);
		if (TargetEntity != nullptr)                                           // 1026ee8a / 1026ee8f
		{
			// The handle re-read for the argument (1026ee9a `-1`, 1026eeb1 serial): the same entity.
			Conditions19CheckTarget(const_cast<FElysiumEntity*>(TargetEntity));   // 1026eebc 0x10271d10
		}
	}

	if (FElysiumNpc* const Troika = AsNpc())                             // 1026eec1 +0x98 / 1026eec9
	{
		Troika->Conditions19TroikaGoalUpkeep();                          // 1026eed8..1026efa4
	}

	CheckAmmo();                                                         // 1026efad slot 565
}

// =================================================================================================
// Slot 481 -- `CAI_BaseNPC::GatherEnemyConditions` `0x10270b20`, 2807 bytes
// =================================================================================================

float FElysiumNpcBase::Conditions19EnemyDistanceUnits(const FElysiumEntity& Enemy) const
{
	// `0x10270890`.
	const FVector Delta = Enemy.GetAbsOrigin() - GetAbsOrigin();        // 1027089a / 102708aa slot 217
	const FBox EnemyBox = ElysiumCameraShots::SurroundingBounds(Enemy);  // 102708f6
	const FBox MyBox = ElysiumCameraShots::SurroundingBounds(*this);     // 1027090f
	double Vertical = 0.0;
	if (!(EnemyBox.Min.Z < MyBox.Max.Z))                                 // 10270912..1027092e
	{
		Vertical = EnemyBox.Min.Z - MyBox.Max.Z;
	}
	else if (!(EnemyBox.Max.Z > MyBox.Min.Z))                            // 10270949 (greater or NaN -> 0)
	{
		Vertical = EnemyBox.Max.Z - MyBox.Min.Z;                         // 1027093f
	}
	const double Cm = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y + Vertical * Vertical);   // 1027096d
	return static_cast<float>(Cm / ElysiumMove::U);
}

double FElysiumNpcBase::Conditions19LastTimeSeen(const FElysiumEntity* Enemy) const
{
	// `0x102e0150`.
	if (Enemy == nullptr)
	{
		return 0.0;
	}
	if (const FElysiumNpcEnemyMemoryRecord* const Record = EnemyMemory.Find(Enemy->Handle))
	{
		return Record->LastSeenTime;
	}
	const FElysiumNpcEnemyMemoryRecord* PositionOnly = nullptr;
	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		if (Record.bPositionOnly)                                        // record `+0x34 == 1`
		{
			PositionOnly = &Record;
		}
	}
	if (PositionOnly != nullptr)
	{
		return PositionOnly->LastSeenTime;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Asking LastTimeSeen for enemy that's not in my memory!!"),
		*DebugString());                                                 // `1060e2f8`
	return 0.0;
}

FVector FElysiumNpcBase::Conditions19LastKnownPosition(const FElysiumEntity* Enemy) const
{
	// `0x102dfed0`.
	if (Enemy != nullptr)
	{
		if (const FElysiumNpcEnemyMemoryRecord* const Record = EnemyMemory.Find(Enemy->Handle))
		{
			return Record->LastPosition;                                 // record `+0xc`
		}
	}
	const FElysiumNpcEnemyMemoryRecord* PositionOnly = nullptr;
	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		if (Record.bPositionOnly)
		{
			PositionOnly = &Record;
		}
	}
	const FString EnemyName = Enemy != nullptr ? Enemy->DebugString() : FString(TEXT("NULL"));
	if (PositionOnly != nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s Asking LastKnownPosition for enemy (%s) that's not in my memory (using danger pos)!!"),
			*DebugString(), *EnemyName);                                 // `1060e248`
		return PositionOnly->LastPosition;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s Asking LastKnownPosition for enemy (%s) that's not in my memory!!"),
		*DebugString(), *EnemyName);                                     // `1060e1f8`
	return FVector::ZeroVector;                                          // `vec3_origin`
}

void FElysiumNpcBase::Conditions19UpdateEnemyPos()
{
	++Conditions19UpdateEnemyPosCalls;
}

bool FElysiumNpcBase::Conditions19NavNotOnNetwork() const
{
	return false;
}

bool FElysiumNpcBase::Conditions19RayReaches(const FVector& FromCm, const FVector& ToCm) const
{
	const IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	return Embodiment == nullptr || Embodiment->QueryLineOfSight(FromCm, ToCm);
}

void FElysiumNpcBase::GatherEnemyConditions(FElysiumEntity* Enemy)
{
	// `0x10270b20`. The VProf scopes (`CAI_BaseNPC_GatherEnemyConditions`, `_Outputs`, `_SeeEnemy`)
	// and the RDTSC / vtune tail are absent. Every "GetEnemy()" below is SLOT 167 (`+0x29c`), the
	// const accessor; the checks run on the PARAMETER where retail uses it.
	//
	// Skeleton sites with no port line, by the absent-words convention:
	// - VProf enter/exit/sub-node and the RDTSC/vtune tail: calls 0x10270b40 0x10270bdb 0x10270c85
	//   0x10270d38 0x10270e1c 0x10270eb2 0x10270f2f 0x10270f41 0x102710ee 0x1027110e 0x102715d4;
	//   branches 0x10270c71 0x10270c79 0x10270c8d 0x10270e0a 0x10270e14 0x10270e24 0x10270e9c
	//   0x10270ea6 0x10270eba 0x10270f09 0x10270f13 0x10270f21 0x102710c7 0x102710d1 0x102710e0
	//   0x102710e7 0x10271119 0x1027152a 0x10271534 0x10271547 0x10271552 0x102715c6 0x102715cb
	//   0x102715d0 0x102715e2.
	// - the `ent_trace_conditions` read `(*DAT_10924a6c)->vfunc1()` before each SetCondition: calls
	//   0x10270bf5 0x10270cc2 0x10270cf4 0x10270e70 0x10271085 0x102710ad 0x102711b7 0x10271233.
	// - the eluded ray's debug overlay under `0x10738964` (`NDebugOverlay::Line`): branches
	//   0x102714b6 0x102714c2, calls 0x102714b1 0x102714e4.
	if (Enemy == nullptr)
	{
		return;   // crash guard: retail dereferences the parameter at `0x10270e5c`
	}
	const FElysiumNpcBase* const ConstThis = this;
	FElysiumNpcConditions& C = Cognition.Conditions;
	const double Now = Conditions19Now();

	C.Clear(Cond19EnemyFacingMe);                                        // 10270b4a 0x56
	C.Clear(Cond19BehindEnemy);                                          // 10270b53 0x57
	C.Clear(EElysiumNpcCond::HaveEnemyLos);                              // 10270b5c 0x4a
	C.Clear(EElysiumNpcCond::EnemyOccluded);                             // 10270b65 0x48 (0x46 is NOT cleared)
	// `0x10270aa0(this, NULL)`: `m_hEnemyOccluder (+0x5d90) = -1`; the blocker cell is zeroed.
	BaseMemory.EnemyOccluder = FElysiumEntityHandle::Invalid();          // 10270b6a / 10270b71

	// Slot 201 on THIS with the parameter, mask `0x2804091`, the blocker cell and the counter's
	// current value. The port's slot takes the blocker by value, so no blocker comes back (SEAM,
	// `ElysiumNpcSenses10.inl`), and the at-limit arm below records none.
	if (FVisible(Enemy, Cond19EnemyVisibleMask, nullptr, BaseMemory.EnemyOccludedCheck))   // 10270b76..10270b93 / 10270b9b
	{
		BaseMemory.EnemyOccludedCheck = 0;                               // 10270bb1
	}
	else if (BaseMemory.EnemyOccludedCheck < Cond19EnemyOccludedLimit)   // 10270ba6 signed JGE
	{
		++BaseMemory.EnemyOccludedCheck;                                 // 10270ba8 (saturates at 10)
	}

	const bool bEnemyIsPlayer = World != nullptr && ConstThis->GetEnemy() != nullptr
		&& ConstThis->GetEnemy()->Handle == World->PlayerHandle();       // `[enemy+0xa8] m_pPlayer`: 10270d67 / 10270c25 slot 167
	if (BaseMemory.EnemyOccludedCheck < Cond19EnemyOccludedLimit)        // 10270bc3 JL -> 10270cba
	{
		C.Set(EElysiumNpcCond::HaveEnemyLos);                            // 10270cc9 0x4a
		if (FInViewCone(Enemy)                                           // 10270cd3 slot 363 / 10270cdb
			&& QuerySeeEntity(Enemy))                                    // 10270ce2 slot 468 / 10270cea
		{
			C.Set(EElysiumNpcCond::SeeEnemy);                            // 10270cfb 0x46
			UpdateEnemyWentOccluded(ConstThis->GetEnemy(), false);       // 10270d06 slot 167 / 10270d0f 0x10270180(GetEnemy(), 0)
		}
		// Both refusals above land here too: the found outputs are gated on the memory bit only.
		if ((BaseScheduleHost.MemoryBits & Cond19MemoryEnemyInSight) == 0)   // 10270d14 / 10270d1e
		{
			const FElysiumEntity* const Found = ConstThis->GetEnemy();   // 10270d46
			const FElysiumEntityHandle Value = Found != nullptr
				? Found->Handle : FElysiumEntityHandle::Invalid();      // 10270d4e / 10270d54 / 10270d5f
			static const FName OnFoundPlayer(TEXT("OnFoundPlayer"));
			static const FName OnFoundEnemy(TEXT("OnFoundEnemy"));
			if (bEnemyIsPlayer)                                          // 10270d6d / 10270d75
			{
				// 10270d80 variant from the EHANDLE, 10270d94 set (type 0xc), 10270da3 copy, 10270daa fire
				FireOutput(OnFoundPlayer, Handle, FElysiumVariant::Handle(Value));   // 10270d77..10270daa +0x5e9c
			}
			// 10270db8 / 10270dcd variant from the EHANDLE, 10270de1 set, 10270df0 copy, 10270df7 fire
			FireOutput(OnFoundEnemy, Handle, FElysiumVariant::Handle(Value));        // 10270daf..10270df7 +0x5e54
		}
		BaseScheduleHost.MemoryBits |= Cond19MemoryEnemyInSight;         // 10270e4c, on EVERY pass below the limit
	}
	else
	{
		// `0x10270aa0(this, blocker)`: the blocker's handle, which the port's slot cannot return.
		BaseMemory.EnemyOccluder = FElysiumEntityHandle::Invalid();      // 10270be1..10270be8
		C.Set(EElysiumNpcCond::EnemyOccluded);                           // 10270bfc 0x48
		UpdateEnemyWentOccluded(ConstThis->GetEnemy(), true);            // 10270c07 slot 167 / 10270c10 0x10270180(GetEnemy(), 1)
		if ((BaseScheduleHost.MemoryBits & Cond19MemoryEnemyInSight) != 0)   // 10270c15 / 10270c1f
		{
			static const FName OnLostPlayerLos(TEXT("OnLostPlayerLOS"));
			static const FName OnLostEnemyLos(TEXT("OnLostEnemyLOS"));
			if (bEnemyIsPlayer)                                          // 10270c2b / 10270c36
			{
				FireOutput(OnLostPlayerLos, Handle);                     // 10270c3e +0x5eb4
			}
			FireOutput(OnLostEnemyLos, Handle);                          // 10270c4c +0x5e6c
		}
		BaseScheduleHost.MemoryBits &= ~Cond19MemoryEnemyInSight;        // 10270c51..10270c5d, unconditional
	}

	if (!Enemy->IsAlive())                                               // 10270e5a slot 158 / 10270e62
	{
		C.Set(EElysiumNpcCond::EnemyDead);                               // 10270e77 0x58
		C.Clear(EElysiumNpcCond::SeeEnemy);                              // 10270e80
		C.Clear(EElysiumNpcCond::EnemyOccluded);                         // 10270e89
		return;                                                          // 10270eec
	}

	const float Distance = Conditions19EnemyDistanceUnits(*Enemy);       // 10270ef2 0x10270890

	if (C.Has(EElysiumNpcCond::SeeEnemy))                                // 10270f4e / 10270f52 / 10270f59
	{
		if (BaseMemory.EnemyOccludedCheck == 0)                          // 10270f5f / 10270f67
		{
			// Slot 198's velocity against `vec3_origin`: NaN or any non-zero component leads.
			const FVector& EnemyVelocity = Enemy->Velocity;                   // 10270f71
			if (EnemyVelocity.X == 0.0 && EnemyVelocity.Y == 0.0 && EnemyVelocity.Z == 0.0)   // 10270f77..10270fa6 (10270f86 / 10270f96 JP)
			{
				// Third argument `&enemy->m_vecVelocity (+0x3d4)` (`1027104b LEA EDX,[EDI+0x3d4]`); the
				// port's slot takes an informer entity there, which the base body does not read -- the
				// landed convention (`ElysiumNpcFrenzyShadow.cpp` slot 431) passes none.
				UpdateEnemyMemory(Enemy, Enemy->GetAbsOrigin(), nullptr);   // 10271047..1027105e slot 544 (10271054 slot 217)
			}
			else
			{
				const float Lead = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
					.FRandRange(Cond19EnemyLeadMin, ElysiumNpcTunables::Zero);   // 10270fac..10270fc2 (10270fbb RandomFloat)
				// 10270fc6 slot 198 again, 10270ff0 slot 217
				const FVector Position = Enemy->GetAbsOrigin() - EnemyVelocity * static_cast<double>(Lead);   // 10270fcc..10271030
				UpdateEnemyMemory(Enemy, Position, nullptr);             // 10271034..1027103f slot 544 (+0x3d4 as above)
			}
		}
		FElysiumCombatCharacter* const EnemyCharacter = Enemy->AsCombatCharacter();   // 10271064 [enemy+0x9c]
		if (EnemyCharacter != nullptr && EnemyCharacter->FInViewCone(static_cast<FElysiumEntity*>(this)))   // 1027106c / 10271073 slot 363 / 1027107b
		{
			C.Set(Cond19EnemyFacingMe);                                  // 1027108c 0x56
			C.Clear(Cond19BehindEnemy);                                  // 10271095 0x57
		}
		else
		{
			C.Clear(Cond19EnemyFacingMe);                                // 102710a0
			C.Set(Cond19BehindEnemy);                                    // 102710b4
		}
	}

	// `m_flDistTooFar (+0x5de4)`, raised to the active weapon's `m_fMaxRange1 (+0x8c0)` while a
	// weapon is held and SEE_ENEMY stands (`Max1 < DistTooFar ? DistTooFar : Max1`, `1027117a`).
	float Limit = DistTooFar;                                            // 10271141 / 10271149
	const bool bHasActiveWeapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		&& World->Resolve(Inventory.ActiveWeapon) != nullptr;            // 1027114d GetActiveWeapon
	if (bHasActiveWeapon && C.Has(EElysiumNpcCond::SeeEnemy))            // 10271154 / 1027115a / 10271161
	{
		float MaxRange1 = 0.f;
		const FElysiumNpc* const Troika = AsNpc();
		// 1027118f GetActiveWeapon again for the read.
		// SEAM: `+0x8c0` is only carried as `ActiveWeaponMaxRangeUnits`, which answers false (no
		// weapon record carries a range); unanswered, the limit stays `m_flDistTooFar`.
		if (Troika != nullptr && Troika->ActiveWeaponMaxRangeUnits(MaxRange1))   // 1027116f / 10271174
		{
			// `FLD max1 / FCOMP limit / TEST AH,5 / JP`: only an ordered `max1 >= limit` takes max1.
			Limit = MaxRange1 >= DistTooFar ? MaxRange1 : DistTooFar;    // 1027117a / 10271183
		}
	}
	if (Distance < Limit)                                                // 102711a0..102711ad (NaN clears)
	{
		C.Clear(EElysiumNpcCond::EnemyTooFar);                           // 102711c9 0x55
	}
	else
	{
		C.Set(EElysiumNpcCond::EnemyTooFar);                             // 102711be 0x55
	}

	if (FCanCheckAttacks())                                              // 102711d2 slot 564 / 102711da
	{
		GatherAttackConditions(ConstThis->GetEnemy(), Distance);         // 102711dc..102711ee slot 561 (102711e5 slot 167)
	}
	else
	{
		ClearAttackConditions();                                         // 102711fa slot 560
	}

	Conditions19UpdateEnemyPos();                                        // 10271202 0x10271900

	if (!Conditions19NavNotOnNetwork()                                   // 10271207..10271212 [[+0x5d34]+0x34]
		&& IsUnreachable(ConstThis->GetEnemy()))                         // 10271218 slot 167 / 10271221 slot 530 / 10271229
	{
		C.Set(EElysiumNpcCond::EnemyUnreachable);                        // 1027123a 0x59
	}

	// --- The eluded tail --------------------------------------------------------------------------
	// `curtime - LastTimeSeen(GetEnemy()) > 8.0` (`AND 0x4100 / JNZ`: at or under 8, or NaN, skips).
	// 1027124b slot 167, 10271256 slot 541, 1027125e `0x102e0150`
	if (!(Now - Conditions19LastTimeSeen(ConstThis->GetEnemy()) > Cond19ElusionSeconds))   // 1027123f..10271272
	{
		return;
	}
	// `m_hEnemy (+0x5ce0)` resolved (10271281 `-1`, 1027129e serial), 102712ab slot 541, 102712b3 `0x102e0210`
	if (EnemyMemory.IsEluded(BaseMemory.Enemy))                          // 10271278..102712ba (m_hEnemy +0x5ce0)
	{
		return;
	}
	if (C.Has(EElysiumNpcCond::SeeEnemy))                                // 102712c0..102712cb (102712c4)
	{
		return;
	}
	const FVector LastKnownCm = Conditions19LastKnownPosition(ConstThis->GetEnemy());   // 102712d1..102712ed 0x102dfed0 (102712d5 slot 167, 102712e5 slot 541)
	if (FElysiumNpc* const Troika = AsNpc())                             // 102712f2 [this+0x98] / 102712fa
	{
		if (Troika->NpcFlags.Has(EElysiumNpcFlag::DONE_EXTRAPOLATING))   // 102712fc..10271310 flags +0x14b8 & 0x8000
		{
			Troika->NpcFlags.Clear(EElysiumNpcFlag::DONE_EXTRAPOLATING); // 10271316 / 10271319
			if (const FElysiumEntity* const Eluding = ConstThis->GetEnemy())
			{
				EnemyMemory.MarkEluded(Eluding->Handle);                 // 1027131f..10271399 0x102dfd90 (10271323 slot 167, 1027132e slot 541)
			}
		}
	}
	else
	{
		const FVector& MyOrigin = GetAbsOrigin();                        // 1027133a slot 217
		const double DxUnits = (LastKnownCm.X - MyOrigin.X) / ElysiumMove::U;
		const double DyUnits = (LastKnownCm.Y - MyOrigin.Y) / ElysiumMove::U;
		// `FCOMP 48.0 / TEST AH,5 / JP` skips only an ordered `>= 48`; a NaN distance proceeds.
		if (!(FMath::Sqrt(DxUnits * DxUnits + DyUnits * DyUnits) >= Cond19ElusionRadiusUnits)   // 10271340..10271373 (1027135f sqrt)
			&& !C.Has(EElysiumNpcCond::SeeEnemy))                        // 10271375..10271380 (10271379)
		{
			if (const FElysiumEntity* const Eluding = ConstThis->GetEnemy())
			{
				EnemyMemory.MarkEluded(Eluding->Handle);                 // 10271382..10271399 0x102dfd90 (10271386 slot 167, 10271391 slot 541)
			}
		}
	}
	// Either mark falls through to the ray.
	if (!C.Has(EElysiumNpcCond::SeeEnemy)                                // 1027139e..102713a9 (102713a2)
		&& C.Has(EElysiumNpcCond::EnemyUnreachable))                     // 102713af..102713ba (102713b3)
	{
		// `Ray_t::Init` (`m_IsSwept` from the delta's length, 10271414), `CTraceFilterSimple`
		// 10271485, `TraceRay` 102714a6.
		if (!Conditions19RayReaches(EyePosition(), LastKnownCm))   // 102713c9..102714fe (fraction != 1.0)
		{
			if (const FElysiumEntity* const Eluding = ConstThis->GetEnemy())
			{
				EnemyMemory.MarkEluded(Eluding->Handle);                 // 10271500..10271517 0x102dfd90 (10271504 slot 167, 1027150f slot 541)
			}
		}
	}
}
