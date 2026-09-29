// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- the species classes'
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Werewolf19's `rule` rows): 0x103cc450 CNPC_VWerewolf::UpdateConditionShouldBreakHint,
// 0x103d0ec0 CNPC_VWerewolf::FindBreakHint, 0x103d1200 CNPC_VWerewolf::FindEgressHint, 0x103d2070
// CNPC_VWerewolf::IsImperativeMoveHint, 0x103d3c20 CNPC_VWerewolf::FindTeleportHint, 0x103da0a0
// CNPC_VWerewolf::IsEnemyUnreachable, 0x103d2810 CNPC_VWerewolf::IsImperativeRandomMoveHint,
// 0x103d2a10 CNPC_VWerewolf::FindMoveHint, 0x103cc320
// CNPC_VWerewolf::UpdateConditionEnemyUnreachable, 0x103cf770
// CNPC_VWerewolf::CheckAllRandomMoveHints, 0x103d14f0 CNPC_VWerewolf::FindRandomMoveHint,
// 0x103cc5c0 CNPC_VWerewolf::UpdateConditionCanSpecialMove.
//
// Story 8, lane L12. Every body is ported arm by arm from the decompiled C and the packet
// (`families-19-29/Werewolf19-READING.md`); each arm carries the address of the instruction it came
// from. The walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Werewolf19".
//
// The retail helpers these bodies call are already ported on the class (`ElysiumNpcWerewolf.h`):
// `IsValidBreakHint` 0x103d8550, `GetHintGroundpoint` 0x103d6770, `GetHintTargetGroundpoint`
// 0x103d68d0, `GetHintEndpoint` 0x103d6650, `GetHintEndEntity` 0x103d6390, `IsValidMoveHint`
// 0x103d8060, `IsValidRandomMoveHint` 0x103d7dc0, `IsValidTeleportHint` 0x103d8300,
// `IsImperativeTeleportHint` 0x103d3360, `GetHintTeleportPriority` 0x103d3220, `SetMoveHint`
// 0x103d44e0, `ClearMoveHint` 0x103d4690, `SetTeleportHint` 0x103d45c0, `WerewolfShouldPursueEnemy`
// 0x103cf5f0, `FUN_103d1e50`,
// `FUN_103d9c90`, `WerewolfHasPath` 0x103d0db0 (the navigator seam), `CachedNearestNodeZone`
// 0x103d0ad0 (seam) and `GetNearestNodeToPlayer` 0x103d0bf0.
//
// The scope-trace frame every one of these opens (`g_ScopeTraceStack`, keyed on `m_iName` or
// `"NULL ENTITY"`) is a debugger aid with no reader; it is absent, as in every landed family.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcWerewolf.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleId.h"
#include "Substrate/ElysiumScheduleNumbers.h"

namespace NpcKernelWerewolf19Species
{
	// Retail's condition numbers. `EElysiumNpcCond` names `0x59` (`EnemyUnreachable`) and `0x77`
	// (`CanTeleport`); `0x78`, `0x79` and `0x7b` are the Werewolf-line registrar's
	// (`COND_VWEREWOLF_CAN_SPECIAL_MOVE`, `_ENEMY_REACHABLE`, `_SHOULD_BREAKHINT` in the overlay table
	// the dead debug overlay printed), above the base table and without enumerators, so they are cast.
	inline EElysiumNpcCond Werewolf19Cond(int32 RetailCondition)
	{
		return static_cast<EElysiumNpcCond>(RetailCondition);
	}
	constexpr int32 GWerewolf19CondEnemyUnreachable = 0x59;
	constexpr int32 GWerewolf19CondCanSpecialMove = 0x78;
	constexpr int32 GWerewolf19CondEnemyReachable = 0x79;
	constexpr int32 GWerewolf19CondShouldBreakHint = 0x7b;

	// `+0x66e8`'s bits, read off the listings. What each MEANS is the zone logic's (the dead debug overlay
	// printed the zone word, not this one); only the tests are recovered.
	constexpr uint32 GWerewolf19FlagBreakHintHeld = 0x2u;    // 0x103cc4c7 / 0x103d3e2e / 0x103d2c51
	constexpr uint32 GWerewolf19FlagBit4 = 0x4u;             // 0x103d2101 / 0x103d235b
	constexpr uint32 GWerewolf19FlagBit80 = 0x80u;           // 0x103d210a
	constexpr uint32 GWerewolf19FlagBit100 = 0x100u;         // 0x103d20f7 / 0x103d236b
	constexpr uint32 GWerewolf19FlagBit200 = 0x200u;         // 0x103d2158
	constexpr uint32 GWerewolf19FlagBit400 = 0x400u;         // 0x103d227d

	// `m_fEffects` bit `0x40`, `0x103cc6a4`'s gate (`TEST byte ptr [...],0x40`).
	constexpr uint32 GWerewolf19EffectBit40 = 0x40u;

	// The hint types these bodies test (`CAI_Hint::m_nHintType`, `+0x5dc`), the Werewolf's own
	// 15000..15018 band.
	constexpr int32 GWerewolf19HintBreakthroughF = 0x3a99;
	constexpr int32 GWerewolf19HintBreakthroughL = 0x3a9a;
	constexpr int32 GWerewolf19HintBreakthroughR = 0x3a9b;
	constexpr int32 GWerewolf19HintSqueezeFront = 0x3a9c;
	constexpr int32 GWerewolf19HintSqueezeLeft = 0x3a9d;
	constexpr int32 GWerewolf19HintSqueezeRight = 0x3a9e;
	constexpr int32 GWerewolf19HintJumpToPlatform = 0x3aa4;
	constexpr int32 GWerewolf19HintObsDoor = 0x3aa5;
	constexpr int32 GWerewolf19Hint3aa6 = 0x3aa6;
	constexpr int32 GWerewolf19Hint3aa7 = 0x3aa7;
	constexpr int32 GWerewolf19HintEgress = 0x3aa8;
	constexpr int32 GWerewolf19Hint3aaa = 0x3aaa;

	// The four Werewolf programs `0x103cc5c0` leaves alone, in the listing's compare order
	// (`0x103cc63f`, `0x103cc65d`, `0x103cc67b`, `0x103cc693`): 0x15f, 0x15b, 0x15a, 0x160, the
	// class-local ids `ElysiumScheduleNumbers.h` checks against the corpus.
	constexpr int32 GWerewolf19SchedulesThatHoldTheHint[4] = {
		ElysiumSched::SCHED_VWEREWOLF_DO_JUMP_HINT, ElysiumSched::SCHED_VWEREWOLF_DO_SPECIAL_MOVEMENT,
		ElysiumSched::SCHED_VWEREWOLF_RUN_TO_SPECIAL_MOVEMENT, ElysiumSched::SCHED_VWEREWOLF_DO_DEATH_HINT };

	// Retail constants, bound to the tunables table (story 8 wave 2).
	constexpr float GWerewolf19TeleportRetrySeconds = ElysiumNpcTunables::FifteenHundredths;   // `_DAT_104aaac4`
	constexpr float GWerewolf19RandomRetrySeconds = ElysiumNpcTunables::Quarter;             // `_DAT_1044bef8`
	constexpr float GWerewolf19MoveHintSeedDistance = ElysiumNpcTunables::WerewolfMoveHintSeedDistance; // `0x103d2bc6`
	constexpr float GWerewolf19RandomHintReach = ElysiumNpcTunables::FifteenHundred;         // `_DAT_10462b70`
	constexpr int32 GWerewolf19TeleportTryBudget = 9;          // `0x103d3e82` `CMP 10, JGE`
	constexpr int32 GWerewolf19MoveTryBudget = 0xe;            // `0x103d2ca6` `0xe < tries`
	constexpr int32 GWerewolf19RandomTryBudget = 4;            // `0x103d176f` `4 < tries`

	// The three `ConVar`s `FindTeleportHint` reads (`werewolf_force_teleport_in_time` `+0x28`,
	// `werewolf_teleport_full_path_check` and `werewolf_teleport_ignore_viewcone` `+0x2c`) are the
	// tunables table's `WerewolfForceTeleportInTime`, `WerewolfTeleportFullPathCheck` and
	// `WerewolfTeleportIgnoreViewcone` (story 8 wave 2), read at each use.

	// `vec3_origin`, `DAT_1070d1b0/b4/b8` (`staticinit_101370b0` zeroes it): the seed of the
	// searches' candidate points and the slot-617 extents.
	const FVector GWerewolf19Vec3Origin = FVector::ZeroVector;

	// Retail's `Vector` equality on all three components, the visited-point scans
	// (`0x103d3f40`..`0x103d3f5b` and siblings): three float `==` tests, an unordered compare fails.
	bool Werewolf19SamePoint(const FVector& A, const FVector& B)
	{
		return static_cast<float>(A.X) == static_cast<float>(B.X)
			&& static_cast<float>(A.Y) == static_cast<float>(B.Y)
			&& static_cast<float>(A.Z) == static_cast<float>(B.Z);
	}

	bool Werewolf19ListHas(const TArray<FVector>& Points, const FVector& Point)
	{
		for (const FVector& Seen : Points)
		{
			if (Werewolf19SamePoint(Seen, Point))
			{
				return true;
			}
		}
		return false;
	}

	// `FUN_101371d0` over a squared length: retail's float `sqrt`.
	float Werewolf19Distance(const FVector& A, const FVector& B)
	{
		const float Dx = static_cast<float>(A.X - B.X);
		const float Dy = static_cast<float>(A.Y - B.Y);
		const float Dz = static_cast<float>(A.Z - B.Z);
		return FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
	}

	// `0x102d1220`, `CAI_Hint`'s (type, name) test: `m_nHintType == type` AND the hint's `m_iName`
	// (`+0x26c`, empty when null) equals the literal — pointer-equal fast path, `_strnicmp` over
	// `len-1` when the literal ends in `*`, `_stricmp` otherwise. `FElysiumEntityWorld::NameMatches`
	// is exactly that matcher (`FUN_100f7770`). CORRECTION to the checklist: the name compared is
	// `+0x26c` `m_iName`, not `m_strGroup` (`vtmb_code 0x102d1220`).
	bool Werewolf19HintIs(const FElysiumNpcBase::FHintWords& Hint, int32 HintType, const TCHAR* Name)
	{
		return Hint.HintType == HintType && FElysiumEntityWorld::NameMatches(Hint.Name, Name);
	}

	// The pointer-equal / `*` / `_stricmp` ladder `0x103d2070` inlines on the hint's `m_iName`.
	bool Werewolf19HintNamed(const FElysiumNpcBase::FHintWords& Hint, const TCHAR* Name)
	{
		return FElysiumEntityWorld::NameMatches(Hint.Name, Name);
	}
}

// -------------------------------------------------------------------------------------------------
// The shared helpers
// -------------------------------------------------------------------------------------------------

const void* FElysiumNpcWerewolf::WerewolfScheduleOfType(int32 RawRetailId)
{
	// `0x102cc1f0`: slot 440 `TranslateSchedule` (`vtable +0x6e0`), slot 446 `GetScheduleOfType`
	// (`+0x6f8`); a null answer `DevMsg`s and asks slot 446 again for schedule 1.
	const int32 Translated = TranslateScheduleRetail(RawRetailId);          // 0x102cc1fd
	const void* Program = GetScheduleOfType(Translated);                    // 0x102cc20a slot 446
	if (Program == nullptr)                                                 // 0x102cc212
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("GetScheduleOfType(): No CASE for Schedule Type %d!"), Translated);   // 0x102cc21a
		Program = GetScheduleOfType(ElysiumSched::IDLE_STAND);              // 0x102cc227 / 0x102cc229
	}
	return Program;
}

FElysiumEntity* FElysiumNpcWerewolf::WerewolfSlot167Enemy() const
{
	// Slot 167 is the CONST overload; `FElysiumNpc` hides it behind its slot-168 override, so it is
	// reached through the base that declares it. Virtual dispatch lands on the most-derived body.
	return static_cast<const FElysiumNpcBase*>(this)->GetEnemy();
}

bool FElysiumNpcWerewolf::WerewolfSlot617(const FVector& PointUnits, bool bSkipViewCone, bool bUseHitbox)
{
	// `(**(code **)(*this + 0x9a4))(point, bSkipViewCone, bUseHitbox, DAT_1070d1b0..b8)`. The
	// Werewolf's slot 617 is `CNPC_VWerewolf::EnemyCouldSeeHull` (`0x103da230`), CENTIMETRES here.
	return EnemyCouldSeeHullWerewolf(PointUnits * ElysiumMove::U, bSkipViewCone, bUseHitbox,
		NpcKernelWerewolf19Species::GWerewolf19Vec3Origin);
}

bool FElysiumNpcWerewolf::WerewolfSearchStampedThisFrame() const
{
	const int32 Frame = EngineFrameNumber();
	return WerewolfMorphTimerB == static_cast<float>(Frame);
}

void FElysiumNpcWerewolf::WerewolfStampSearch(double Now)
{
	WerewolfMorphTimerB = static_cast<float>(EngineFrameNumber());          // +0x66d4 := frame
	WerewolfMorphTimerC = static_cast<float>(Now);                          // +0x66d8 := curtime
}

int32 FElysiumNpcWerewolf::WerewolfHintListHead() const
{
	const TArray<int32> List = GlobalHintList();
	return List.Num() > 0 ? List[0] : INDEX_NONE;
}

int32 FElysiumNpcWerewolf::WerewolfHintListNext(int32 HintNode) const
{
	const TArray<int32> List = GlobalHintList();
	const int32 At = List.Find(HintNode);
	return At != INDEX_NONE && At + 1 < List.Num() ? List[At + 1] : INDEX_NONE;
}

bool FElysiumNpcWerewolf::WerewolfClosestPlayerResolves() const
{
	return World != nullptr && Senses.Memory.ClosestPlayer.IsSet()
		&& World->Resolve(Senses.Memory.ClosestPlayer) != nullptr;
}

FVector FElysiumNpcWerewolf::WerewolfOriginUnits() const
{
	return Origin / ElysiumMove::U;
}

FElysiumNpcBase::FHintWords FElysiumNpcWerewolf::WerewolfHintAt(int32 HintNode) const
{
	FHintWords Words;
	HintWords(HintNode, Words);
	return Words;
}

// -------------------------------------------------------------------------------------------------
// 0x103cc450 CNPC_VWerewolf::UpdateConditionShouldBreakHint
// -------------------------------------------------------------------------------------------------

void FElysiumNpcWerewolf::UpdateConditionShouldBreakHint()
{
	// Absent: the scope-trace name pick (branches 0x103cc459 0x103cc463), a debugger aid with no
	// reader.
	using namespace NpcKernelWerewolf19Species;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	Cognition.Conditions.Clear(Werewolf19Cond(GWerewolf19CondShouldBreakHint)); // 0x103cc4c2
	if ((WerewolfHintFlags & GWerewolf19FlagBreakHintHeld) != GWerewolf19FlagBreakHintHeld) // 0x103cc4d2
	{
		WerewolfBreakHintNode = INDEX_NONE;                                 // 0x103cc4d4
	}
	if (WerewolfBreakHintNode == INDEX_NONE)                                // 0x103cc4e6
	{
		return;
	}
	const FHintWords Break = WerewolfHintAt(WerewolfBreakHintNode);
	if (!IsValidBreakHint(Break, Now))                                      // 0x103cc4f2 0x103cc4eb
	{
		return;
	}
	const FVector GroundUnits = GetHintGroundpoint(Break);                  // 0x103cc502
	if (!WerewolfHasPath(WerewolfOriginUnits(), GroundUnits))               // 0x103cc546
	{
		return;
	}
	// 0x103cc550: `(**(code **)(*DAT_10924a6c + 4))()` — slot 1 on `ent_trace_conditions`
	// (`0x10924a68`), its result discarded: a folded condition-trace gate, nothing to reproduce.
	Cognition.Conditions.Set(Werewolf19Cond(GWerewolf19CondShouldBreakHint));   // 0x103cc557
}

// -------------------------------------------------------------------------------------------------
// 0x103d0ec0 CNPC_VWerewolf::FindBreakHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::FindBreakHint()
{
	// Absent: the scope-trace name pick (branches 0x103d0eca 0x103d0ed4), a debugger aid with no
	// reader.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (WerewolfBreakHintNode != INDEX_NONE)                                // 0x103d0f37
	{
		const FHintWords Held = WerewolfHintAt(WerewolfBreakHintNode);
		if (IsValidBreakHint(Held, Now))                                    // 0x103d0f43 0x103d0f3c
		{
			const FVector GroundUnits = GetHintGroundpoint(Held);           // 0x103d0f53
			if (WerewolfHasPath(WerewolfOriginUnits(), GroundUnits))        // 0x103d0f97 0x103d0f5e 0x103d0f90
			{
				return true;                                                // 0x103d0fab
			}
		}
	}
	WerewolfStampSearch(Now);                                               // 0x103d0fba / 0x103d0fc8 0x103d0fb4
	int32 Node = WerewolfHintListHead();
	if (Node == INDEX_NONE)                                                 // 0x103d0fd6
	{
		return false;                                                       // 0x103d0fea
	}
	if (!WerewolfClosestPlayerResolves())                                   // 0x103d0ff4 / 0x103d100f / 0x103d1014
	{
		return false;                                                       // 0x103d1028
	}
	float BestDistance = MAX_FLT;                                           // 0x103d102d
	const FVector OriginUnits = WerewolfOriginUnits();                      // 0x103d1035
	FVector ReferenceUnits;
	FUN_103d9c90(ReferenceUnits);                                           // 0x103d1056
	int32 Best = INDEX_NONE;
	for (; Node != INDEX_NONE; Node = WerewolfHintListNext(Node))           // 0x103d105f / 0x103d1121
	{
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (!IsValidBreakHint(Candidate, Now))                              // 0x103d106f 0x103d1068
		{
			continue;
		}
		const FVector GroundUnits = GetHintGroundpoint(Candidate);          // 0x103d107d
		const float Distance =
			NpcKernelWerewolf19Species::Werewolf19Distance(ReferenceUnits, GroundUnits); // 0x103d10b4
		if (!(Distance < BestDistance))                                     // 0x103d10d0
		{
			continue;
		}
		if (!WerewolfHasPath(OriginUnits, GroundUnits))                     // 0x103d110d 0x103d1106
		{
			continue;
		}
		Best = Node;                                                        // 0x103d110f
		BestDistance = Distance;                                            // 0x103d1115
	}
	WerewolfBreakHintNode = Best;                                           // 0x103d1127
	return Best != INDEX_NONE;                                              // 0x103d1143
}

// -------------------------------------------------------------------------------------------------
// 0x103d1200 CNPC_VWerewolf::FindEgressHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::FindEgressHint()
{
	// Absent: the scope-trace name pick (branches 0x103d120a 0x103d1214), a debugger aid with no
	// reader.
	using namespace NpcKernelWerewolf19Species;
	if (MoveHintNode != INDEX_NONE)                                         // 0x103d1277
	{
		if (WerewolfSlot167Enemy() != nullptr)                              // 0x103d1281 / 0x103d1289
		{
			const FHintWords Move = WerewolfHintAt(MoveHintNode);
			const FVector GroundUnits = GetHintGroundpoint(Move);           // 0x103d129d
			if (WerewolfHasPath(WerewolfOriginUnits(), GroundUnits))        // 0x103d12e1 0x103d12a8 0x103d12da
			{
				// Slot 167 asked AGAIN for the enemy whose origin ends the second path (`0x103d12e7`).
				const FElysiumEntity* Enemy = WerewolfSlot167Enemy();       // 0x103d12e7
				const FVector EnemyUnits = Enemy->Origin / ElysiumMove::U;  // 0x103d12f1
				const FVector EndUnits = GetHintEndpoint(&Move) / ElysiumMove::U; // 0x103d1307
				if (WerewolfHasPath(EndUnits, EnemyUnits))                  // 0x103d133f 0x103d1338
				{
					return true;                                            // 0x103d1353
				}
			}
		}
		ClearMoveHint();                                                    // 0x103d1356
	}
	for (int32 Node = WerewolfHintListHead(); Node != INDEX_NONE;           // 0x103d1363
		Node = WerewolfHintListNext(Node))                                  // 0x103d142c
	{
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (Candidate.HintType != GWerewolf19HintEgress)                    // 0x103d1373
		{
			continue;
		}
		const FVector GroundUnits = GetHintGroundpoint(Candidate);          // 0x103d1381
		if (!WerewolfHasPath(WerewolfOriginUnits(), GroundUnits))           // 0x103d13c5 0x103d138c
		{
			continue;
		}
		const FVector EndUnits = GetHintEndpoint(&Candidate) / ElysiumMove::U;   // 0x103d13cf
		if (!WerewolfSlot617(EndUnits, false, false))                       // 0x103d1410 / 0x103d1418
		{
			break;
		}
		// Every hint whose endpoint the enemy could see is INSTALLED and the scan goes on, so the last
		// such hint on the list is the one left held — and the body still answers false (`0x103d1444`).
		SetMoveHint(Node, false);                                           // 0x103d141f
	}
	return false;                                                           // 0x103d1444
}

// -------------------------------------------------------------------------------------------------
// 0x103d2070 CNPC_VWerewolf::IsImperativeMoveHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::IsImperativeMoveHint(const FHintWords& Hint)
{
	// Absent: the scope-trace name pick (branches 0x103d207a 0x103d2084), a debugger aid with no
	// reader. Each `0x102d1220` (type, name) test below is `CALL 0x10006992`; each bare name ladder is
	// the inlined pointer-equal / `strlen` / `*` / `_stricmp` (`0x1043e780`) / `_strnicmp`
	// (`0x10432740`) test.
	using namespace NpcKernelWerewolf19Species;
	const uint32 GateFlags = WerewolfHintFlags;                                 // 0x103d20e3
	auto PathToHint = [this, &Hint]()
	{
		// `GetHintGroundpoint(hint)` then slot 220 `GetOrigin` then `HasPath(origin, ground)`.
		const FVector GroundUnits = GetHintGroundpoint(Hint);
		return WerewolfHasPath(WerewolfOriginUnits(), GroundUnits);
	};

	// Gate A: bit 0x100 set and bit 4 clear.
	bool bToGateB = false;
	if ((GateFlags & GWerewolf19FlagBit100) == GWerewolf19FlagBit100            // 0x103d20f7
		&& (GateFlags & GWerewolf19FlagBit4) != GWerewolf19FlagBit4)            // 0x103d2101
	{
		if ((GateFlags & GWerewolf19FlagBit80) == GWerewolf19FlagBit80 && FUN_103d1e50()) // 0x103d210a / 0x103d2115 0x103d210e
		{
			if (Hint.HintType == GWerewolf19Hint3aaa)                       // 0x103d2121
			{
				return true;                                                // 0x103d2135
			}
			bToGateB = true;   // `goto LAB_103d2148`: the 0x3aa6 test is skipped on this arm
		}
		if (!bToGateB && Hint.HintType == GWerewolf19Hint3aa6)              // 0x103d2138 / 0x103d2142
		{
			return true;                                                    // 0x103d2651
		}
	}

	// Gate B: bit 0x200, the wall_b_1 / door_b_5 set.
	if ((GateFlags & GWerewolf19FlagBit200) == GWerewolf19FlagBit200)           // 0x103d2158
	{
		if (Hint.Disabled != 0)                                             // 0x103d215e / 0x103d2166
		{
			return false;                                                   // 0x103d2666
		}
		if (Werewolf19HintIs(Hint, GWerewolf19HintBreakthroughR, TEXT("wall_b_1_breakthrough_r"))  // 0x103d217f 0x103d2178
			|| Werewolf19HintIs(Hint, GWerewolf19HintBreakthroughF, TEXT("wall_b_1_breakthrough_f")) // 0x103d2198 0x103d2191
			|| Werewolf19HintIs(Hint, GWerewolf19HintBreakthroughL, TEXT("wall_b_1_breakthrough_l")) // 0x103d21ad 0x103d21a6
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeRight, TEXT("wall_b_1_squeeze_right"))   // 0x103d21c2 0x103d21bb
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeFront, TEXT("wall_b_1_squeeze_front"))   // 0x103d21d7 0x103d21d0
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeLeft, TEXT("wall_b_1_squeeze_left"))     // 0x103d21ec 0x103d21e5
			|| Werewolf19HintIs(Hint, GWerewolf19HintBreakthroughF, TEXT("door_b_5_breakthrough"))   // 0x103d2201 0x103d21fa
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeFront, TEXT("door_b_5_squeeze_front")))  // 0x103d2216 0x103d220f
		{
			if (PathToHint())                                               // 0x103d225e / 0x103d2265 0x103d2220 0x103d222c
			{
				return true;                                                // 0x103d2651
			}
		}
	}

	// Gate C: bit 0x400, the tramdoor set.
	if ((GateFlags & GWerewolf19FlagBit400) == GWerewolf19FlagBit400)           // 0x103d227d
	{
		if (Hint.Disabled != 0)                                             // 0x103d2283 / 0x103d228b
		{
			return false;                                                   // 0x103d2666
		}
		if (Werewolf19HintIs(Hint, GWerewolf19HintBreakthroughF, TEXT("tramdoor_b_1_breakthrough_f"))  // 0x103d22a4 0x103d229d
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeFront, TEXT("tramdoor_b_1_squeeze_front"))   // 0x103d22b9 0x103d22b2
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeRight, TEXT("tramdoor_a_1_squeeze_right"))   // 0x103d22ce 0x103d22c7
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeFront, TEXT("tramdoor_a_1_squeeze_front"))   // 0x103d22e3 0x103d22dc
			|| Werewolf19HintIs(Hint, GWerewolf19HintSqueezeLeft, TEXT("tramdoor_a_1_squeeze_left")))    // 0x103d22f8 0x103d22f1
		{
			if (PathToHint())                                               // 0x103d2340 / 0x103d2347 0x103d2302 0x103d230e
			{
				return true;                                                // 0x103d2651
			}
		}
	}

	// Gate D: bit 4 SET and bit 0x100 CLEAR, else false.
	if ((GateFlags & GWerewolf19FlagBit4) != GWerewolf19FlagBit4                // 0x103d235b
		|| (GateFlags & GWerewolf19FlagBit100) == GWerewolf19FlagBit100)        // 0x103d236b
	{
		return false;                                                       // 0x103d2666
	}
	if (Hint.HintType != GWerewolf19Hint3aa7)                               // 0x103d237e
	{
		if (FUN_103d1e50())                                                 // 0x103d239d / 0x103d23aa
		{
			if (Hint.HintType == GWerewolf19HintObsDoor && PathToHint())    // 0x103d23b5 / 0x103d2404 0x103d23bf 0x103d23cb 0x103d23fd
			{
				return true;                                                // 0x103d2651
			}
			if (Hint.HintType != GWerewolf19HintJumpToPlatform)             // 0x103d2414
			{
				return false;                                               // 0x103d2666
			}
			if (!Werewolf19HintNamed(Hint, TEXT("jump_to_platform_hint_1"))   // 0x103d2422..0x103d2491 0x103d2429 0x103d243e 0x103d2456 0x103d245a 0x103d2467 0x103d2473 0x103d2482
				&& !Werewolf19HintNamed(Hint, TEXT("jump_to_platform_hint_2"))) // 0x103d249f..0x103d2600 0x103d24a6 0x103d24bb 0x103d24d6 0x103d24da 0x103d24e7 0x103d24f6
			{
				return false;                                               // 0x103d2666
			}
		}
		else
		{
			if (Hint.HintType != GWerewolf19HintSqueezeFront)               // 0x103d250e
			{
				return false;                                               // 0x103d2666
			}
			if (!Werewolf19HintNamed(Hint, TEXT("archway_a_5_squeeze_front"))  // 0x103d251c..0x103d258b 0x103d2523 0x103d2538 0x103d2550 0x103d2554 0x103d2561 0x103d256d 0x103d257c
				&& !Werewolf19HintNamed(Hint, TEXT("archway_b_6_squeeze_front")))  // 0x103d2595..0x103d2600 0x103d259c 0x103d25ad 0x103d25c5 0x103d25c9 0x103d25d6 0x103d25e2 0x103d25f1
			{
				return false;                                               // 0x103d2666
			}
		}
	}
	// The `0x3aa7` arm reaches here with no further test (`0x103d2380`).
	return PathToHint();                                                    // 0x103d2648 / 0x103d264f 0x103d2386 0x103d2392 0x(0x3aa7 0xarm) 0x103d260a 0x103d2616
}

// -------------------------------------------------------------------------------------------------
// 0x103d3c20 CNPC_VWerewolf::FindTeleportHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::FindTeleportHint()
{
	// Absent: the scope-trace name pick (branches 0x103d3c30 0x103d3c3a), a debugger aid with no
	// reader. The two local `CUtlVector`s are the port's `TArray`s: `0x1000f52e` insert, `0x10006d16`
	// grow, `0x10009d1d` element copy, `0x1000e3ea` RemoveAll, `0x100156e5` purge, `[0x109f3624]`
	// free.
	using namespace NpcKernelWerewolf19Species;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (WerewolfSearchStampedThisFrame())                                   // 0x103d3cad 0x103d3ca1 0x(frame 0xcount)
	{
		return false;                                                       // 0x103d3cc5
	}
	if (static_cast<float>(Now) < WerewolfMorphTimerC + GWerewolf19TeleportRetrySeconds) // 0x103d3ce2
	{
		return false;                                                       // 0x103d3cfa
	}
	if (TeleportHintNode != INDEX_NONE)                                     // 0x103d3d01
	{
		return true;                                                        // 0x103d3d19
	}
	WerewolfStampSearch(Now);                                               // 0x103d3d28 / 0x103d3d36 0x103d3d22
	const int32 Head = WerewolfHintListHead();
	if (Head == INDEX_NONE)                                                 // 0x103d3d44
	{
		return false;                                                       // 0x103d3d5c
	}
	if (!WerewolfClosestPlayerResolves())                                   // 0x103d3d6a / 0x103d3d71
	{
		return false;                                                       // 0x103d3d89
	}
	// The cursor `+0x66ac`: resume at its `+0x5d8` successor, else at the head.
	const bool bCursorSet = WerewolfWord66ac != 0 && WerewolfWord66ac != INDEX_NONE;
	const int32 CursorNode = bCursorSet ? WerewolfWord66ac : INDEX_NONE;
	int32 Node = bCursorSet ? WerewolfHintListNext(CursorNode) : INDEX_NONE;   // 0x103d3d92
	if (Node == INDEX_NONE)                                                 // 0x103d3d9c
	{
		Node = Head;
	}

	int32 Best = INDEX_NONE;                                                // 0x103d3da7 piStack_78
	int32 BestPriority = 0;                                                 // iStack_68
	float BestDistance = MAX_FLT;                                           // fStack_70
	FVector ReferenceUnits;
	FUN_103d9c90(ReferenceUnits);                                           // 0x103d3db7
	FVector GroundUnits = GWerewolf19Vec3Origin;                            // fStack_4c.. = DAT_1070d1b0
	FVector TargetUnits = GWerewolf19Vec3Origin;                            // fStack_64.. = DAT_1070d1b0
	int32 Tries = 0;                                                        // iStack_7c
	bool bAccepted = false;                                                 // cStack_7d
	TArray<FVector> Rejected;                                               // iStack_28 (the visited list)
	TArray<FVector> AcceptedPoints;                                         // aiStack_14
	if ((WerewolfHintFlags & GWerewolf19FlagBreakHintHeld) == GWerewolf19FlagBreakHintHeld   // 0x103d3e2e
		&& WerewolfBreakHintNode != INDEX_NONE)                             // 0x103d3e38
	{
		ReferenceUnits = GetHintGroundpoint(WerewolfHintAt(WerewolfBreakHintNode));   // 0x103d3e42
	}

	// The walk. It stops on returning to the cursor, or at the list's end when there is none. The
	// visit bound below is a NAMED CRASH GUARD: retail spins forever when `+0x66ac` holds a hint that
	// has left the list (the wrap never meets it) and no candidate spends the try budget; a faithful
	// walk visits at most `List.Num()` nodes, so the bound never cuts a retail-reachable walk short.
	const int32 VisitBound = GlobalHintList().Num() + 1;
	int32 Visits = 0;
	int32 Winner = INDEX_NONE;
	do
	{
		if (++Visits > VisitBound)
		{
			Winner = Best;
			break;
		}
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (IsImperativeTeleportHint(Candidate))                            // 0x103d3e5e / 0x103d3e65
		{
			Winner = Node;
			break;
		}
		bool bPushTarget = false;
		if (!IsValidTeleportHint(&Candidate, Now))                          // 0x103d3e6e / 0x103d3e75
		{
			// RETAIL DEFECT, reproduced: an invalid hint re-tests the flag the LAST valid candidate left
			// (`cStack_7d`) and, when it is clear, appends the STALE target point — the previous
			// candidate's, or `vec3_origin` before any — to the reject list (`0x103d4178`).
			bPushTarget = !bAccepted;                                       // 0x103d417a
		}
		else
		{
			if (Tries > GWerewolf19TeleportTryBudget)                       // 0x103d3e82
			{
				if (Best == INDEX_NONE)                                     // 0x103d42a7
				{
					WerewolfWord66ac = Node;                                // 0x103d42b3 +0x66ac = cursor
					WerewolfTeleportGiveUpTries = Tries;
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("Werewolf giving up after %d teleport searches!"), Tries);   // 0x103d42b9
					return false;                                           // 0x103d42ca 0xteardown 0x103d42d5 0x103d42de 0x103d42f2 0x103d42fb 0x103d4304 0x103d4315
				}
				Winner = Best;
				break;
			}
			GroundUnits = GetHintGroundpoint(Candidate);                    // 0x103d3e90
			GetHintEndEntity(Candidate);                                    // 0x103d3eac, answer discarded
			TargetUnits = GetHintTargetGroundpoint(Candidate);              // 0x103d3eb9
			const float Distance = Werewolf19Distance(TargetUnits, ReferenceUnits);   // 0x103d3f04
			bAccepted = false;                                              // 0x103d3f11
			const int32 Priority = GetHintTeleportPriority(Candidate.HintType);  // 0x103d3f1a
			if (Werewolf19ListHas(Rejected, TargetUnits))                   // 0x103d3f2b..0x103d3f6a 0x103d3f4e 0x103d3f63
			{
				// A rejected point is pushed AGAIN (`0x103d4180`), so the list grows with repeats.
				bPushTarget = true;
			}
			else if (Distance < BestDistance && BestPriority < Priority)    // 0x103d3f7f / 0x103d3f8f
			{
				if (Werewolf19ListHas(AcceptedPoints, TargetUnits))         // 0x103d3fa0..0x103d3fdf 0x103d3fb5 0x103d3fc3 0x103d3fd0 0x103d3fd8
				{
					bAccepted = true;                                       // 0x103d3fe1
				}
				// `werewolf_force_teleport_in_time` plus `m_flTimeTeleportedOut` (`+0x66f0`) already past.
				if (ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::WerewolfForceTeleportInTime) + static_cast<float>(WerewolfTimeTeleportedOut)   // 0x103d3fef 0x(IsCommand)
					< static_cast<float>(Now))                              // 0x103d3ff4 / 0x103d401a
				{
					bAccepted = true;                                       // 0x103d401c
				}
				else if (!bAccepted)                                        // 0x103d4029
				{
					if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfTeleportFullPathCheck) != 0)              // 0x103d403c / 0x103d4048 0x103d4037 0x(IsCommand)
					{
						++Tries;                                            // 0x103d4052
						bAccepted = WerewolfHasPath(TargetUnits, ReferenceUnits);   // 0x103d4087
					}
					else
					{
						// `0x103d0e70`: both the nearest node to the player (`0x103d0bf0`) and the
						// hint's own node (`0x102d3e60`) resolve with a non-zero `+0x94` zone. No node
						// graph stands here (`CachedNearestNodeZone` is its seam), so this answers
						// retail's "no node" false. Unreached as shipped (`full_path_check` "1").
						bAccepted = false;                                  // 0x103d4091
					}
					if (!bAccepted)                                         // 0x103d409e
					{
						bPushTarget = true;
					}
					else
					{
						AcceptedPoints.Add(TargetUnits);                    // 0x103d40b4..0x103d40ed 0x103d40c3 0x103d40d4
					}
				}
				if (bAccepted)
				{
					++Tries;                                                // 0x103d40f4
					const bool bSkipViewCone = ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfTeleportIgnoreViewcone) != 0;  // 0x103d40fb / 0x103d4100
					if (!WerewolfSlot617(GroundUnits, bSkipViewCone, false))   // 0x103d4152 / 0x103d415c
					{
						Best = Node;                                        // 0x103d415e
						BestDistance = Distance;
						BestPriority = Priority;
					}
					bPushTarget = !bAccepted;                               // 0x103d4178 (always false here)
				}
			}
			else
			{
				bPushTarget = true;                                         // 0x103d4180
			}
		}
		if (bPushTarget)
		{
			Rejected.Add(TargetUnits);                                      // 0x103d418b..0x103d41ca 0x103d4195 0x103d41b5
		}
		Node = WerewolfHintListNext(Node);                                  // 0x103d41d4
		if (Node == INDEX_NONE && bCursorSet)                               // 0x103d41dc / 0x103d41e4
		{
			Node = Head;
		}
		Winner = Best;
	}
	while (Node != CursorNode);                                             // 0x103d41f2

	WerewolfWord66ac = Winner == INDEX_NONE ? 0 : Winner;                   // 0x103d4203 +0x66ac
	if (Winner != INDEX_NONE)                                               // 0x103d4209
	{
		SetTeleportHint(Winner);                                            // 0x103d4213
	}
	return Winner != INDEX_NONE;                                            // 0x103d4224 0xteardown 0x103d4236 0x103d424a 0x103d4257 0x103d426b 0x103d426f 0x103d4272
}

// -------------------------------------------------------------------------------------------------
// 0x103da0a0 CNPC_VWerewolf::IsEnemyUnreachable
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::IsEnemyUnreachable()
{
	// Absent: the scope-trace name pick (branches 0x103da0a9 0x103da0b3), a debugger aid with no
	// reader.
	const int32 Frame = EngineFrameNumber();   // 0x103da116 0x(frame 0xcount)
	if (Frame != INDEX_NONE && WerewolfMorphTimerA == static_cast<float>(Frame))   // 0x103da122
	{
		return bWerewolfTaskFailed;                                         // 0x103da1be
	}
	WerewolfMorphTimerA = static_cast<float>(Frame);                        // 0x103da136 +0x66a4
	FElysiumEntity* Enemy = WerewolfSlot167Enemy();                         // 0x103da140
	if (Enemy == nullptr || IsUnreachable(Enemy))                           // 0x103da148 / 0x103da157 0x103da14f 0x(slot 0x530)
	{
		bWerewolfTaskFailed = true;                                         // 0x103da159 +0x66a1
	}
	// Never cleared on a frame that finds the enemy fine: only a path clears the latch.
	if (bWerewolfTaskFailed)                                                // 0x103da168
	{
		FVector ChaseUnits;
		FUN_103d9c90(ChaseUnits);                                           // 0x103da171
		if (WerewolfHasPath(WerewolfOriginUnits(), ChaseUnits))             // 0x103da1ae / 0x103da1b5 0x103da17c
		{
			bWerewolfTaskFailed = false;                                    // 0x103da1b7
		}
	}
	return bWerewolfTaskFailed;                                             // 0x103da1be
}

// -------------------------------------------------------------------------------------------------
// 0x103d2810 CNPC_VWerewolf::IsImperativeRandomMoveHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::IsImperativeRandomMoveHint(const FHintWords& Hint)
{
	// Absent: the scope-trace name pick (branches 0x103d281a 0x103d2824), a debugger aid with no
	// reader.
	using namespace NpcKernelWerewolf19Species;
	if (Hint.HintType == GWerewolf19HintEgress)                             // 0x103d288e
	{
		// `DAT_1093d574` `werewolf_pursuit_distance`: its float, or `_DAT_104454c4` (0.0) when slot 1
		// reports it a command (`0x103d289c` / `0x103d28a1`).
		const float Limit = WerewolfPursuePlayerDistLimitUnits();
		const float PlayerDistUnits = Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U;   // +0x6264
		if (Limit < PlayerDistUnits)                                        // 0x103d28bf
		{
			const FVector EndUnits = GetHintEndpoint(&Hint) / ElysiumMove::U;    // 0x103d28cd
			if (!WerewolfSlot617(EndUnits, true, false))                    // 0x103d290e / 0x103d2916
			{
				const FVector GroundUnits = GetHintGroundpoint(Hint);       // 0x103d2920
				if (WerewolfHasPath(WerewolfOriginUnits(), GroundUnits))    // 0x103d295d / 0x103d2964 0x103d292b 0x(slot 0x220)
				{
					return true;                                            // 0x103d2978
				}
			}
		}
	}
	return IsImperativeMoveHint(Hint);                                      // 0x103d297e
}

// -------------------------------------------------------------------------------------------------
// 0x103d2a10 CNPC_VWerewolf::FindMoveHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::FindMoveHint()
{
	// Absent: the scope-trace name pick (branches 0x103d2a20 0x103d2a2a), a debugger aid with no
	// reader. The local `CUtlVector`s are `TArray`s, as in `FindTeleportHint`.
	using namespace NpcKernelWerewolf19Species;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (WerewolfSearchStampedThisFrame())                                   // 0x103d2a99 0x103d2a8d 0x(frame 0xcount)
	{
		return false;                                                       // 0x103d2ab1
	}
	if (static_cast<float>(Now) < WerewolfMorphTimerC + ElysiumNpcTunables::Half)   // 0x103d2ace `_DAT_104454d0`
	{
		return false;                                                       // 0x103d2ae6
	}
	if (MoveHintNode != INDEX_NONE)                                         // 0x103d2aed
	{
		return true;                                                        // 0x103d2b05
	}
	if (!WerewolfShouldPursueEnemy())                                       // 0x103d2b08 / 0x103d2b0f
	{
		return false;                                                       // 0x103d2b27
	}
	WerewolfStampSearch(Now);                                               // 0x103d2b3d / 0x103d2b4b 0x103d2b37
	const int32 Head = WerewolfHintListHead();
	if (Head == INDEX_NONE)                                                 // 0x103d2b59
	{
		return false;                                                       // 0x103d2b71
	}
	if (!WerewolfClosestPlayerResolves())                                   // 0x103d2b7f / 0x103d2b86
	{
		return false;                                                       // 0x103d2b9e
	}
	const bool bCursorSet = WerewolfMoveHintSearchStart != 0 && WerewolfMoveHintSearchStart != INDEX_NONE;
	const int32 CursorNode = bCursorSet ? WerewolfMoveHintSearchStart : INDEX_NONE;
	int32 Node = bCursorSet ? WerewolfHintListNext(CursorNode) : INDEX_NONE;    // 0x103d2ba7
	if (Node == INDEX_NONE)                                                 // 0x103d2bb1
	{
		Node = Head;
	}
	int32 Best = INDEX_NONE;                                                // piStack_60
	int32 Tries = 0;                                                        // iStack_78
	float BestDistance = GWerewolf19MoveHintSeedDistance;                   // 0x103d2bc6 fStack_28
	const FVector OriginUnits = WerewolfOriginUnits();                      // 0x103d2bce
	FVector ReferenceUnits;
	FUN_103d9c90(ReferenceUnits);                                           // 0x103d2bef
	TArray<FVector> Rejected;                                               // iStack_74
	TArray<FVector> AcceptedPoints;                                         // aiStack_5c
	if ((WerewolfHintFlags & GWerewolf19FlagBreakHintHeld) == GWerewolf19FlagBreakHintHeld   // 0x103d2c51
		&& WerewolfBreakHintNode != INDEX_NONE)                             // 0x103d2c5b
	{
		ReferenceUnits = GetHintGroundpoint(WerewolfHintAt(WerewolfBreakHintNode));   // 0x103d2c68
	}

	const int32 VisitBound = GlobalHintList().Num() + 1;   // the named crash guard, as FindTeleportHint
	int32 Visits = 0;
	int32 Winner = INDEX_NONE;
	do
	{
		if (++Visits > VisitBound)
		{
			Winner = Best;
			break;
		}
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (IsImperativeMoveHint(Candidate))                                // 0x103d2c84 / 0x103d2c8b
		{
			Winner = Node;
			break;
		}
		if (IsValidMoveHint(Candidate, Now))                                // 0x103d2c94 / 0x103d2c9b
		{
			if (Tries > GWerewolf19MoveTryBudget)                           // 0x103d2ca6
			{
				if (Best == INDEX_NONE)                                     // 0x103d3011 / 0x103d3015
				{
					WerewolfMoveHintSearchStart = Node;                     // 0x103d3023 +0x66b8 = cursor
					return false;                                           // 0x103d3029 0xteardown 0x103d3034 0x103d303d 0x103d304e 0x103d3057 0x103d3060 0x103d3071
				}
				Winner = Best;
				break;
			}
			const int32 Type = Candidate.HintType;                          // 0x103d2cac
			GetHintEndEntity(Candidate);                                    // 0x103d2cb5, answer discarded
			const FVector GroundUnits = GetHintGroundpoint(Candidate);      // 0x103d2cc2
			const FVector TargetUnits = GetHintTargetGroundpoint(Candidate);   // 0x103d2ce6
			const float Distance = Werewolf19Distance(OriginUnits, GroundUnits);   // 0x103d2d31
			bool bPushTarget = true;                                        // LAB_103d2ed1 unless a win
			if (Distance < BestDistance || Type == GWerewolf19HintObsDoor)  // 0x103d2d47 / 0x103d2d4f
			{
				if (Werewolf19ListHas(Rejected, TargetUnits))               // 0x103d2d5d..0x103d2d9c 0x103d2d72 0x103d2d80 0x103d2d8d 0x103d2d95
				{
					bPushTarget = true;                                     // pushed again, 0x103d2ed1
				}
				else if (Werewolf19ListHas(AcceptedPoints, TargetUnits))    // 0x103d2daa..0x103d2de9 0x103d2dbf 0x103d2dcd 0x103d2dda 0x103d2de2
				{
					// An accepted point wins without a path test (`LAB_103d2ebf`).
					BestDistance = Distance;                                // 0x103d2ebf
					Best = Node;                                            // 0x103d2ec3
					bPushTarget = false;
				}
				else
				{
					++Tries;                                                // 0x103d2dfb
					if (WerewolfHasPath(TargetUnits, ReferenceUnits))       // 0x103d2e2f / 0x103d2e36
					{
						++Tries;                                            // 0x103d2e47
						if (WerewolfHasPath(OriginUnits, GroundUnits))      // 0x103d2e79 / 0x103d2e80
						{
							AcceptedPoints.Add(TargetUnits);                // 0x103d2e8c..0x103d2eb9 0x103d2e98 0x103d2ea9
							BestDistance = Distance;                        // 0x103d2ebf
							Best = Node;                                    // 0x103d2ec3
							bPushTarget = false;
						}
					}
				}
			}
			if (bPushTarget)
			{
				Rejected.Add(TargetUnits);                                  // 0x103d2ed1..0x103d2f18 0x103d2edc 0x103d2ee6 0x103d2f03
			}
		}
		// An invalid hint appends nothing here, unlike `FindTeleportHint` (`0x103d2c9b` -> `0x103d2f20`).
		Node = WerewolfHintListNext(Node);                                  // 0x103d2f20
		if (Node == INDEX_NONE && bCursorSet)                               // 0x103d2f28 / 0x103d2f30
		{
			int32 Zone = 0;
			const bool bNearestNode = CachedNearestNodeZone(Zone);          // 0x103d2f34 `0x103d0ad0`
			Node = Head;
			if (bNearestNode)                                               // 0x103d2f3b
			{
				WerewolfHintNodeCacheA = Zone;                              // 0x103d2f43 +0x6708 = node+0x94
			}
		}
		Winner = Best;
	}
	while (Node != CursorNode);                                             // 0x103d2f55

	WerewolfMoveHintSearchStart = Winner == INDEX_NONE ? 0 : Winner;        // 0x103d2f61 +0x66b8
	const bool bFound = Winner != INDEX_NONE;                               // 0x103d2f67
	if (bFound)
	{
		SetMoveHint(Winner, false);                                         // 0x103d2f72
	}
	return bFound;                                                          // 0x103d2f83 0xteardown 0x103d2f92 0x103d2fae 0x103d2fb2 0x103d2fb5 0x103d2fca 0x103d2fdd 0x103d2fe1 0x103d2fe4
}

// -------------------------------------------------------------------------------------------------
// 0x103cc320 CNPC_VWerewolf::UpdateConditionEnemyUnreachable
// -------------------------------------------------------------------------------------------------

void FElysiumNpcWerewolf::UpdateConditionEnemyUnreachable()
{
	// Absent: the scope-trace name pick (branches 0x103cc325 0x103cc32f), a debugger aid with no
	// reader.
	using namespace NpcKernelWerewolf19Species;
	if (!IsEnemyUnreachable())                                              // 0x103cc389 / 0x103cc390
	{
		Cognition.Conditions.Clear(Werewolf19Cond(GWerewolf19CondEnemyUnreachable));   // 0x103cc3b5
		// 0x103cc3c2: slot 1 on `ent_trace_conditions`, result discarded.
		Cognition.Conditions.Set(Werewolf19Cond(GWerewolf19CondEnemyReachable));       // 0x103cc3c9
	}
	else
	{
		// 0x103cc39a: slot 1 on `ent_trace_conditions`, result discarded.
		Cognition.Conditions.Set(Werewolf19Cond(GWerewolf19CondEnemyUnreachable));     // 0x103cc3a1
		Cognition.Conditions.Clear(Werewolf19Cond(GWerewolf19CondEnemyReachable));     // 0x103cc3aa
	}
	// The once-per-episode edge: the previous pass's latch still clear AND 0x59 now set.
	if (WerewolfSnapWordA == 0                                              // 0x103cc3d6
		&& Cognition.Conditions.Has(Werewolf19Cond(GWerewolf19CondEnemyUnreachable)))  // 0x103cc3dc / 0x103cc3e3
	{
		if (!CheckAllMoveHints())                                           // 0x103cc3e7 / 0x103cc3ee
		{
			bWerewolfPlayFrustration = true;                                // 0x103cc3f0 +0x66a9
		}
	}
	WerewolfSnapWordA =
		Cognition.Conditions.Has(Werewolf19Cond(GWerewolf19CondEnemyUnreachable)) ? 1 : 0;   // 0x103cc3fb / 0x103cc400 +0x66a8
}

// -------------------------------------------------------------------------------------------------
// 0x103cf770 CNPC_VWerewolf::CheckAllRandomMoveHints
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::CheckAllRandomMoveHints()
{
	// Absent: the scope-trace name pick (branches 0x103cf77d 0x103cf787), a debugger aid with no
	// reader. The visited list is a `TArray`, as in `FindTeleportHint`.
	using namespace NpcKernelWerewolf19Species;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// `fStack_38..30`, the point appended for a node that does not win. Retail leaves it
	// UNINITIALISED unless the held-hint arm below wrote it; the port seeds `vec3_origin` (NAMED
	// DIVERGENCE: an uninitialised stack read is not reproducible).
	FVector TargetUnits = GWerewolf19Vec3Origin;
	if (MoveHintNode != INDEX_NONE)                                         // 0x103cf7ea
	{
		const FVector OriginUnits = WerewolfOriginUnits();                  // 0x103cf7f0
		TargetUnits = GetHintTargetGroundpoint(WerewolfHintAt(MoveHintNode));   // 0x103cf80c
		if (WerewolfHasPath(OriginUnits, TargetUnits))                      // 0x103cf839 / 0x103cf840
		{
			return true;                                                    // 0x103cf855
		}
		ClearMoveHint();                                                    // 0x103cf858
	}
	WerewolfMorphTimerB = static_cast<float>(EngineFrameNumber());          // 0x103cf874 +0x66d4 0x103cf86e
	WerewolfMoveHintSearchStart = 0;                                        // 0x103cf885 +0x66b8 = NULL
	WerewolfMorphTimerC = static_cast<float>(Now);                          // 0x103cf88b +0x66d8
	const FVector OriginUnits = WerewolfOriginUnits();                      // 0x103cf899
	TArray<FVector> Visited;                                                // iStack_14
	for (int32 Node = WerewolfHintListHead(); ; Node = WerewolfHintListNext(Node))   // 0x103cfa3a
	{
		if (Node == INDEX_NONE)                                             // 0x103cf8c9
		{
			return false;                                                   // 0x103cfa48 0xteardown 0x103cfa53 0x103cfa5c 0x103cfa70 0x103cfa74 0x103cfa77
		}
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (IsImperativeRandomMoveHint(Candidate))                          // 0x103cf8d2 / 0x103cf8db
		{
			ClearMoveHint();                                                // 0x103cfa9c
			SetMoveHint(Node, true);                                        // 0x103cfaa6
			return true;                                                    // 0x103cfab4 0xteardown 0x103cfabf 0x103cfac8 0x103cfad9
		}
		if (IsValidRandomMoveHint(Candidate, Now))                          // 0x103cf8e2 / 0x103cf8e9
		{
			TargetUnits = GetHintTargetGroundpoint(Candidate);              // 0x103cf8f7
			if (!Werewolf19ListHas(Visited, TargetUnits))                   // 0x103cf918..0x103cf957 0x103cf92d 0x103cf93b 0x103cf948 0x103cf950
			{
				// The distance is to the hint's OWN origin (slot 217 on the hint, `0x103cf961`).
				const FVector HintUnits = Candidate.OriginCm / ElysiumMove::U;
				const float Distance = Werewolf19Distance(OriginUnits, HintUnits);   // 0x103cf995
				if (Distance < GWerewolf19RandomHintReach)                  // 0x103cf9a9
				{
					const FVector GroundUnits = GetHintGroundpoint(Candidate);   // 0x103cf9b3
					if (WerewolfHasPath(OriginUnits, GroundUnits))          // 0x103cf9e8 / 0x103cf9ef
					{
						ClearMoveHint();                                    // 0x103cfaf4
						SetMoveHint(Node, true);                            // 0x103cfafe
						return true;                                        // 0x103cfb0c 0xteardown 0x103cfb17 0x103cfb20 0x103cfb31
					}
				}
			}
		}
		// Every node that does not win appends the target point — for an INVALID node that is the
		// stale point the previous node left (retail defect, reproduced).
		Visited.Add(TargetUnits);                                           // 0x103cf9f5..0x103cfa32 0x103cf9ff 0x103cfa0b 0x103cfa1c
	}
}

// -------------------------------------------------------------------------------------------------
// 0x103d14f0 CNPC_VWerewolf::FindRandomMoveHint
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcWerewolf::FindRandomMoveHint()
{
	// Absent: the scope-trace name pick (branches 0x103d1500 0x103d150a), a debugger aid with no
	// reader. The local `CUtlVector`s are `TArray`s, as in `FindTeleportHint`.
	using namespace NpcKernelWerewolf19Species;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (WerewolfSearchStampedThisFrame())                                   // 0x103d1579 0x103d156d 0x(frame 0xcount)
	{
		return false;                                                       // 0x103d1591
	}
	if (static_cast<float>(Now) < WerewolfMorphTimerC + GWerewolf19RandomRetrySeconds)   // 0x103d15ae
	{
		return false;                                                       // 0x103d15c6
	}
	if (MoveHintNode != INDEX_NONE)                                         // 0x103d15cd
	{
		return true;                                                        // 0x103d15e5
	}
	WerewolfStampSearch(Now);                                               // 0x103d15fb / 0x103d1609 0x103d15f5
	const int32 Head = WerewolfHintListHead();
	if (Head == INDEX_NONE)                                                 // 0x103d1616
	{
		return false;                                                       // 0x103d162e
	}
	if (!WerewolfClosestPlayerResolves())                                   // 0x103d163c / 0x103d1643
	{
		return false;                                                       // 0x103d165b
	}
	const bool bCursorSet = WerewolfMoveHintSearchStart != 0 && WerewolfMoveHintSearchStart != INDEX_NONE;
	const int32 CursorNode = bCursorSet ? WerewolfMoveHintSearchStart : INDEX_NONE;
	int32 Node = bCursorSet ? WerewolfHintListNext(CursorNode) : INDEX_NONE;    // 0x103d1664
	if (Node == INDEX_NONE)                                                 // 0x103d166e
	{
		Node = Head;
	}
	int32 Best = INDEX_NONE;                                                // piStack_78
	int32 Tries = 0;                                                        // aiStack_74[10]
	float BestDistance = MAX_FLT;                                           // fStack_38
	const FVector OriginUnits = WerewolfOriginUnits();                      // 0x103d168f
	FVector ReferenceUnits;
	FUN_103d9c90(ReferenceUnits);                                           // 0x103d16b3
	FVector TargetUnits = GWerewolf19Vec3Origin;                            // fStack_84.. = DAT_1070d1b0
	bool bAccepted = false;                                                 // bVar1
	TArray<FVector> Rejected;                                               // aiStack_74[0..4]
	TArray<FVector> AcceptedPoints;                                         // aiStack_74[5..9]
	if ((WerewolfHintFlags & GWerewolf19FlagBreakHintHeld) == GWerewolf19FlagBreakHintHeld   // 0x103d171a
		&& WerewolfBreakHintNode != INDEX_NONE)                             // 0x103d1724
	{
		ReferenceUnits = GetHintGroundpoint(WerewolfHintAt(WerewolfBreakHintNode));   // 0x103d1731
	}

	const int32 VisitBound = GlobalHintList().Num() + 1;   // the named crash guard, as FindTeleportHint
	int32 Visits = 0;
	int32 Winner = INDEX_NONE;
	do
	{
		if (++Visits > VisitBound)
		{
			Winner = Best;
			break;
		}
		const FHintWords Candidate = WerewolfHintAt(Node);
		if (IsImperativeRandomMoveHint(Candidate))                          // 0x103d174d / 0x103d1754
		{
			Winner = Node;
			break;
		}
		bool bPushTarget = false;
		if (!IsValidRandomMoveHint(Candidate, Now))                         // 0x103d175d / 0x103d1764
		{
			// RETAIL DEFECT, reproduced (as `FindTeleportHint`): an invalid node appends the stale
			// point unless the last valid candidate won (`bVar1`, `0x103d194f`).
			bPushTarget = !bAccepted;
		}
		else
		{
			if (Tries > GWerewolf19RandomTryBudget)                         // 0x103d176f
			{
				if (Best == INDEX_NONE)                                     // 0x103d1a9c
				{
					WerewolfMoveHintSearchStart = Node;                     // 0x103d1aaa +0x66b8 = cursor
					return false;                                           // 0x103d1ab0 0xteardown 0x103d1abb 0x103d1ac4 0x103d1ad5 0x103d1ade 0x103d1ae7 0x103d1af8
				}
				Winner = Best;
				break;
			}
			bAccepted = false;                                              // 0x103d1780
			const FVector GroundUnits = GetHintGroundpoint(Candidate);      // 0x103d1785
			TargetUnits = GetHintTargetGroundpoint(Candidate);              // 0x103d17a9
			const float Distance = Werewolf19Distance(TargetUnits, ReferenceUnits);   // 0x103d17f4
			bPushTarget = true;                                             // LAB_103d1955 unless a win
			if (Distance < BestDistance)                                    // 0x103d180e
			{
				if (Werewolf19ListHas(Rejected, TargetUnits))               // 0x103d1818..0x103d1857 0x103d182d 0x103d183b 0x103d1848 0x103d1850
				{
					bPushTarget = true;                                     // pushed again, 0x103d1955
				}
				else if (Werewolf19ListHas(AcceptedPoints, TargetUnits))    // 0x103d1865..0x103d18a4 0x103d187a 0x103d1888 0x103d1895 0x103d189d
				{
					bAccepted = true;                                       // 0x103d1936
					BestDistance = Distance;
					Best = Node;
					bPushTarget = false;
				}
				else
				{
					++Tries;                                                // 0x103d18b5
					if (WerewolfHasPath(OriginUnits, GroundUnits))          // 0x103d18f0 / 0x103d18f7
					{
						AcceptedPoints.Add(TargetUnits);                    // 0x103d1903..0x103d1920 0x103d190f
						bAccepted = true;                                   // 0x103d1936
						BestDistance = Distance;
						Best = Node;
						bPushTarget = false;
					}
				}
			}
		}
		if (bPushTarget)
		{
			Rejected.Add(TargetUnits);                                      // 0x103d1955..0x103d1978 0x103d195b 0x103d1967
		}
		Node = WerewolfHintListNext(Node);                                  // 0x103d198e
		if (Node == INDEX_NONE && bCursorSet)                               // 0x103d1996 / 0x103d199e
		{
			int32 Zone = 0;
			if (CachedNearestNodeZone(Zone))                                // 0x103d19a2 / 0x103d19a9
			{
				RandomMoveHintNodeZone = Zone;                              // 0x103d19b1 +0x670c
			}
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("FindRandomMoveHint() looped through the entire list"));   // 0x103d19bc
			Node = Head;                                                    // 0x103d19c2
		}
		Winner = Best;
	}
	while (Node != CursorNode);                                             // 0x103d19d1

	WerewolfMoveHintSearchStart = Winner == INDEX_NONE ? 0 : Winner;        // 0x103d19dd +0x66b8
	const bool bFound = Winner != INDEX_NONE;                               // 0x103d19e3
	if (bFound)
	{
		SetMoveHint(Winner, true);                                          // 0x103d19ef
	}
	return bFound;                                                          // 0x103d1a00 0xteardown 0x103d1a0b 0x103d1a14 0x103d1a30 0x103d1a34 0x103d1a37 0x103d1a48 0x103d1a51 0x103d1a64 0x103d1a68 0x103d1a6b
}

// -------------------------------------------------------------------------------------------------
// 0x103cc5c0 CNPC_VWerewolf::UpdateConditionCanSpecialMove
// -------------------------------------------------------------------------------------------------

void FElysiumNpcWerewolf::UpdateConditionCanSpecialMove()
{
	// Absent: the scope-trace name pick (branches 0x103cc5c9 0x103cc5d3), a debugger aid with no
	// reader.
	using namespace NpcKernelWerewolf19Species;
	// Gate 1: the installed program is one of the four hint programs -> do nothing at all. Each
	// compare re-reads `m_pSchedule` and stops at a null one (`0x103cc636` / `0x103cc64e` / …).
	for (const int32 HoldId : GWerewolf19SchedulesThatHoldTheHint)          // 0x103cc63f / 0x103cc65d / 0x103cc67b / 0x103cc693
	{
		if (Schedule.Current == ElysiumScheduleId::None)                    // 0x103cc636 / 0x103cc64e / 0x103cc66c / 0x103cc68a
		{
			break;
		}
		// `m_pSchedule` is a program pointer; the port's word is the program's global id, which
		// `ElysiumScheduleFor` turns back into the program.
		if (static_cast<const void*>(ElysiumScheduleFor(Schedule.Current))
			== WerewolfScheduleOfType(HoldId))                              // 0x103cc646 / 0x103cc664 / 0x103cc682 / 0x103cc69e
		{
			return;                                                         // 0x103cc7eb
		}
	}
	// Gate 2: `m_fEffects & 0x40`.
	if ((EffectsWord & GWerewolf19EffectBit40) == GWerewolf19EffectBit40)   // 0x103cc6a4 / 0x103cc6af
	{
		return;                                                             // 0x103cc7eb
	}
	if (MoveHintNode != INDEX_NONE                                          // 0x103cc6bd
		&& WerewolfHintAt(MoveHintNode).HintType == GWerewolf19HintObsDoor) // 0x103cc6c9
	{
		if (!FUN_103d1e50())                                                // 0x103cc6cd / 0x103cc6d4
		{
			ClearMoveHint();                                                // 0x103cc6d8
		}
	}
	if (MoveHintNode != INDEX_NONE)                                         // 0x103cc6e5
	{
		const FHintWords Move = WerewolfHintAt(MoveHintNode);
		if (IsImperativeMoveHint(Move))                                     // 0x103cc6ee / 0x103cc6f5
		{
			// 0x103cc6ff: slot 1 on `ent_trace_conditions`, result discarded.
			Cognition.Conditions.Set(Werewolf19Cond(GWerewolf19CondCanSpecialMove));  // 0x103cc706
			return;                                                         // 0x103cc717
		}
		const FVector TargetUnits = GetHintTargetGroundpoint(Move);         // 0x103cc726
		FVector ChaseUnits;
		FUN_103d9c90(ChaseUnits);                                           // 0x103cc732
		if (WerewolfHasPath(TargetUnits, ChaseUnits))                       // 0x103cc76b / 0x103cc772
		{
			// 0x103cc77c: slot 1 on `ent_trace_conditions`, result discarded.
			Cognition.Conditions.Set(Werewolf19Cond(GWerewolf19CondCanSpecialMove));  // 0x103cc783
			return;                                                         // 0x103cc794
		}
		ClearMoveHint();                                                    // 0x103cc797
	}
	if (!Cognition.Conditions.Has(Werewolf19Cond(GWerewolf19CondEnemyUnreachable)))   // 0x103cc7a0 / 0x103cc7a7
	{
		ClearMoveHint();                                                    // 0x103cc7e6
		return;                                                             // 0x103cc7f7
	}
	if (MoveHintNode != INDEX_NONE)                                         // 0x103cc7b1
	{
		return;                                                             // 0x103cc7eb
	}
	if (WerewolfShouldPursueEnemy())                                        // 0x103cc7b5 / 0x103cc7be
	{
		FindMoveHint();                                                     // 0x103cc7c0
		return;                                                             // 0x103cc7d1
	}
	FindRandomMoveHint();                                                   // 0x103cc7d2
}
