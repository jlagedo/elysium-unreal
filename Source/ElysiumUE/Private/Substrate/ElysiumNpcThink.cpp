// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcThink.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body.
//
// Owns (Think19's `rule` rows): 0x10298070 CAI_BaseNPCTroika::UpdateCharacter, 0x10292de0
// CAI_BaseNPCTroika::NPCThink. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family Think19".
//
// This is the retail pass. The live loop still runs the port's `FElysiumNpc::Think`
// (`ElysiumNpc.cpp`); the story-8 wave-2 rewire points the entity think at slot 431 and deletes that
// twin (lane L13b's report lists every fragment).

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcThinkCadence.h"

namespace
{
	// `0x469c4000` -- the enemy triple's no-enemy value (`0x10293059`), 20000.0 Source units.
	constexpr float GThink19NoEnemyDistanceUnits = 20000.f;

	// Slot 517's three floats at `0x1029303d`..`0x10293048`: `PUSH 0x3f800000` (1.0),
	// `PUSH 0x3f4ccccd` (0.8), `PUSH 0x0` (0.0). Immediates, not table cells.
	constexpr float GThink19FaceEnemyDuration = 1.0f;
	constexpr float GThink19FaceEnemyRamp = 0.8f;
	constexpr float GThink19FaceEnemyTolerance = 0.f;

	// `ClearHintNode(5.0)` -- `PUSH 0x40a00000` at `0x10293149`, an immediate.
	constexpr float GThink19HintClearReuseSeconds = 5.0f;

	// The hint-upkeep conditions: `HasCondition(0x2e)` (`0x1029312f`), `HasCondition(0x48)`
	// (`0x1029313c`), `SetCondition(0x29)` (`0x10293160`). 0x29 has no enumerator (the lifecycle
	// family's `GHintDestroyedCondition` spells it the same way).
	constexpr int32 GThink19CondBlockedByFriend = 0x2e;
	constexpr int32 GThink19CondEnemyOccluded = 0x48;
	constexpr int32 GThink19CondHintInvalid = 0x29;

	// `m_bfNPCFrenziedFlags & 0x8000` (`0x102931cf`) -- no named mask in the base header.
	constexpr uint32 GThink19FrenziedDeathScream = 0x8000u;
	// `RandomInt(0, 0x63)` compared `>= 1` (`0x102931e7`..`0x102931f3`).
	constexpr int32 GThink19ScreamRollMax = 0x63;
	// The VSound play `0x101f5950(this, index, 2, 1.0, 1.25)` (`0x10293259`..`0x10293263`).
	constexpr int32 GThink19ScreamChannel = 2;
	constexpr float GThink19ScreamAttenuation = 1.25f;
	// The function-local static `0x109246c0` behind guard bit `0x10923dd7 & 1`: the concept index is
	// searched ONCE for the whole process, and `-1` is cached forever on a miss (`0x1029324b`).
	bool GThink19ScreamDeathSearched = false;
	int32 GThink19ScreamDeathIndex = INDEX_NONE;

	// `FVisible(player, 0x2804091, 0, 0)` -- `PUSH 0x2804091` at `0x102933df`.
	constexpr int32 GThink19DisappearTraceMask = 0x2804091;

	// The refused think's re-arm, `curtime + _DAT_104491b4` (0.1f, `0x10293419`) -- a float cell
	// the kernel table does not carry.
	constexpr float GThink19RefusedRearmSeconds = 0.1f;

	// Slot 464 `GetState() == 2` (`0x10298109`): retail 2 is COMBAT.
	constexpr EElysiumNpcState GThink19BossRegisterState = EElysiumNpcState::Combat;
	constexpr int8 GThink19BossRegistrySlots = 2;
}

FElysiumEntityHandle FElysiumNpc::Think19BossRegistry[2] = { FElysiumEntityHandle(), FElysiumEntityHandle() };
int8 FElysiumNpc::Think19BossRegistryCount = 0;

void FElysiumNpc::Think19ResetBossRegistry()
{
	// `CWorld::vfunc113` `0x1023bc20`: `DAT_109247e0 = -1; DAT_10924fb8 = 0; DAT_109247e4 = -1`.
	Think19BossRegistry[0] = FElysiumEntityHandle::Invalid();
	Think19BossRegistryCount = 0;
	Think19BossRegistry[1] = FElysiumEntityHandle::Invalid();
}

int32 FElysiumNpc::Think19DebugTrackUnderGroundConVar()
{
	// `debug_track_under_ground`, shipped "0" (see the declaration).
	return 0;
}

void FElysiumNpc::Think19TrackUnderGround(float NormalInterval)
{
	// SEAM: `0x102bfe10` -- DevMsg-only (see the declaration). Its argument is unread (`RET 4`).
	(void)NormalInterval;
	++Think19TrackUnderGroundCalls;
}

void FElysiumNpc::Think19DebugDistanceOverlay()
{
	// SEAM: `0x1029bd40` -- a debug screen text under `debug_npc_map` (see the declaration).
	++Think19DebugDistanceOverlayCalls;
}

bool FElysiumNpc::Think19InPlayerPvs(const FElysiumEntity* Player) const
{
	// `0x101d1a90(player, this)`: the engine PVS test, which answers false for a null entity.
	if (Player == nullptr)
	{
		return false;
	}
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return true;   // headless: every point shares one PVS
	}
	return Embodiment->ArePointsInSamePvs(Player->Origin, Origin);
}

void FElysiumNpc::Think19CombatCharacterUpdateCharacter(float IntervalSeconds)
{
	// SEAM: `CBaseCombatCharacter::UpdateCharacter` `0x103246d0` (see the declaration).
	Think19LastUpdateCharacterInterval = IntervalSeconds;
	++Think19CombatUpdateCharacterCalls;
}

// Slot 312: `0x10298070`, 574 bytes. The prologue's profiler-scope name (`this == NULL` ->
// "NULL ENTITY" `0x10298079`, a NULL classname -> "" `0x10298083`) is the engine's VPROF budget
// label; it reaches no state the bytecode observes.
void FElysiumNpc::UpdateCharacterRetail(float IntervalSeconds)
{
	if (bIsBossMonster)                                                     // 0x102980de / 0x102980e6
	{
		if (!bInBossRegistry                                                // 0x102980e8 / 0x102980f0
			&& Think19BossRegistryCount < GThink19BossRegistrySlots         // 0x102980f2 / 0x102980f9 (signed byte)
			&& GetState() == GThink19BossRegisterState)                     // 0x10298100 slot 464 / 0x10298109
		{
			const int8 Slot = Think19BossRegistryCount;                     // 0x1029810b
			Think19BossRegistryCount = static_cast<int8>(Slot + 1);         // 0x10298115
			Think19BossRegistry[Slot] = Handle;                             // 0x10298126 slot 1 / 0x1029812b
			bInBossRegistry = true;                                         // 0x1029812d
		}
		if (bIsBossMonster)                                                 // 0x10298134 / 0x1029813c
		{
			Think19CombatCharacterUpdateCharacter(IntervalSeconds);         // 0x1029829a 0x103246d0
			return;
		}
	}
	if (bInBossRegistry)                                                    // 0x10298142 / 0x1029814a
	{
		// The temporary `CUtlVector` (`0x102c6b30`, `0x1029815a`) of the survivors.
		TArray<FElysiumEntityHandle> Survivors;
		for (int32 Slot = 0; Slot < GThink19BossRegistrySlots; ++Slot)      // 0x1029817a..0x1029821f
		{
			// `0x100290c0` resolves the saved handle to its entity; a stale or dead one is dropped.
			FElysiumEntity* Entry = World != nullptr ? World->Resolve(Think19BossRegistry[Slot]) : nullptr;  // 0x1029817d
			if (Entry == nullptr)                                           // 0x10298184
			{
				continue;
			}
			if (Entry == this)                                              // 0x1029818f..0x102981b5 (serial check 0x102981ab)
			{
				continue;
			}
			// The entity's own `GetRefEHandle()` appended (`0x102981de`, `0x102981f2`/`0x102981fe`,
			// the store `0x1029820e` behind the element-pointer null test `0x1029820c`); `-1` when the second
			// resolve failed (`0x102981ba`..`0x102981da`, its serial check `0x102981d4`),
			// which cannot happen between two resolves on one thread.
			Survivors.Add(Entry->Handle);
		}
		// `DAT_109247e0 = 0; DAT_10924fb8 = count; DAT_109247e4 = 0` (`0x1029822b`..`0x10298239`).
		// Retail's raw word 0 has no port handle spelling; the unbound handle stands for it (the one
		// reader reads `[0, count)`).
		Think19BossRegistry[0] = FElysiumEntityHandle::Invalid();
		Think19BossRegistryCount = static_cast<int8>(Survivors.Num());
		Think19BossRegistry[1] = FElysiumEntityHandle::Invalid();
		if (Think19BossRegistryCount != 0)                                  // 0x10298240
		{
			for (int32 Slot = 0; Slot < GThink19BossRegistrySlots; ++Slot)  // 0x1029824d..0x1029825a
			{
				// BOTH slots are copied back from the vector. With one survivor, slot 1 reads past the
				// vector's count in retail (an over-read of the growth buffer); NAMED CRASH GUARD:
				// the port writes the unbound handle there.
				Think19BossRegistry[Slot] = Survivors.IsValidIndex(Slot)
					? Survivors[Slot] : FElysiumEntityHandle::Invalid();
			}
		}
		bInBossRegistry = false;                                            // 0x10298260
		// `0x102c6d40` + `Plat_Free` (`0x1029826f`..`0x1029828a`): the vector's release, skipped for an
		// external buffer (`0x10298283`, allocator count -1) or a null one (`0x10298287`).
	}
	Think19CombatCharacterUpdateCharacter(IntervalSeconds);                 // 0x1029829a 0x103246d0
}

void FElysiumNpc::Think19EnemyTriple()
{
	// Slot 167 `GetEnemy()` (the const slot; slot 168 is the Troika's other overload).
	FElysiumEntity* Enemy = static_cast<const FElysiumNpcBase*>(this)->GetEnemy();   // 0x10292f03
	// `enemy->m_lifeState (+0x200) != 0` -- slot 158 `IsAlive` is this runtime's reading of that word.
	if (Enemy == nullptr || !Enemy->IsAlive())                              // 0x10292f0d / 0x10292f1b
	{
		ScheduleHost.EnemyDistUnits = GThink19NoEnemyDistanceUnits;         // 0x1029305e
		ScheduleHost.EnemyHeightDiffUnits = GThink19NoEnemyDistanceUnits;   // 0x10293064
		ScheduleHost.EnemyLastKnownDistUnits = GThink19NoEnemyDistanceUnits;  // 0x1029306a
		return;
	}
	const FVector MyOrigin = Origin;                                        // 0x10292f25 slot 217
	const FVector EnemyOrigin = Enemy->Origin;                              // 0x10292f43 slot 217
	// Slot 541 `GetEnemies()` -> `0x102dfed0` the last-known position (cm here).
	const FVector LastKnown = Conditions19LastKnownPosition(Enemy);         // 0x10292f67 / 0x10292f6f
	ScheduleHost.EnemyDistUnits =
		static_cast<float>(FVector::Dist(MyOrigin, EnemyOrigin)) / ElysiumMove::U;   // 0x10292faa / 0x10292fb0
	ScheduleHost.EnemyHeightDiffUnits =
		static_cast<float>(FMath::Abs(MyOrigin.Z - EnemyOrigin.Z)) / ElysiumMove::U;  // 0x10292fbf FABS / 0x10292fc1
	ScheduleHost.EnemyLastKnownDistUnits =
		static_cast<float>(FVector::Dist(Origin, LastKnown)) / ElysiumMove::U;       // 0x10292fc9 / 0x10292ffd sqrt / 0x10293003
	// `debug_allow_move_facing` (`*0x10924f74`, `IsCommand() ? 0 : +0x2c`; the `IsCommand` call
	// `0x10293014`) and `MOVE_FACE_ENEMY`.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugAllowMoveFacing) != 0  // 0x10293019 / 0x10293025
		&& NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY))                  // 0x10293039
	{
		AddFacingTarget(Enemy, LastKnown, GThink19FaceEnemyDuration, GThink19FaceEnemyRamp,
			GThink19FaceEnemyTolerance);                                    // 0x10293051 slot 517
	}
}

void FElysiumNpc::Think19HintUpkeep()
{
	if (BaseScheduleHost.HintNode == INDEX_NONE)                            // 0x102930bc / 0x102930c4
	{
		OccludedDelay = OccludedDelayNormal;                                // 0x1029316b / 0x10293171
		return;
	}
	OccludedDelay = OccludedDelayCover;                                     // 0x102930ca / 0x102930d1
	bool bClearHint = false;
	if (!FValidateHintTypeNode(BaseScheduleHost.HintNode))                  // 0x102930db slot 566 / 0x102930e3
	{
		bClearHint = true;
	}
	else if (!bStayEntrenched)                                              // 0x102930e5 / 0x102930ed
	{
		// `m_hHintCoverObject` (+0x6448), NULL when `-1` or stale (`0x102930fc`..`0x1029311f`,
		// the serial check `0x10293119`).
		const FElysiumEntity* Cover =
			World != nullptr ? World->Resolve(ScheduleHost.HintCoverObject) : nullptr;
		// Compared with slot 167. RETAIL DEFECT reproduced: NO cover object and NO enemy compare
		// EQUAL, so a hint with neither still reaches the two condition tests.
		if (Cover == static_cast<const FElysiumNpcBase*>(this)->GetEnemy())     // 0x10293125 / 0x1029312d
		{
			bClearHint = Cognition.Conditions.HasOrdinal(GThink19CondBlockedByFriend)   // 0x10293133 / 0x1029313a
				|| Cognition.Conditions.HasOrdinal(GThink19CondEnemyOccluded);           // 0x10293140 / 0x10293147
		}
	}
	if (!bClearHint)
	{
		return;
	}
	ClearScheduleHint(GThink19HintClearReuseSeconds);                       // 0x10293150 0x10295ab0
	// `(*0x10924a6c)->vfunc1()` -- `ent_trace_conditions`' IsCommand, read and discarded
	// (`0x1029315d`); the trace it gates is debug output.
	(void)ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::EntTraceConditions);
	Cognition.Conditions.SetOrdinal(GThink19CondHintInvalid);               // 0x10293164 0x10269a20
}

void FElysiumNpc::Think19ShootAtHint()
{
	if (ScheduleHost.ShootAtHintNode == 0)                                  // 0x10293177 / 0x1029317f
	{
		return;
	}
	if (!FValidateHintTypeNode(ScheduleHost.ShootAtHintNode))              // 0x10293186 slot 566 / 0x1029318e
	{
		ShootTargetOverride = FElysiumEntityHandle::Invalid();              // 0x102931b5
		ScheduleHost.ShootAtHintNode = 0;                                   // 0x102931bf
		return;
	}
	// Re-read (`0x10293190`/`0x10293198`); slot 1 on the hint is its own `GetRefEHandle()`.
	const FElysiumEntity* Hint = nullptr;
	if (World != nullptr && World->Entities().IsValidIndex(ScheduleHost.ShootAtHintNode))
	{
		Hint = World->Entities()[ScheduleHost.ShootAtHintNode].Get();
	}
	ShootTargetOverride = Hint != nullptr
		? Hint->Handle                                                      // 0x1029319c / 0x102931a1
		: FElysiumEntityHandle::Invalid();                                  // 0x102931a9
}

void FElysiumNpc::Think19ScreamDeathRoll()
{
	if (!HasFrenzied(GThink19FrenziedDeathScream))                          // 0x102931c9 / 0x102931db
	{
		return;
	}
	// `RandomInt(0, 99)` on the engine random stream (`*0x1070b244` slot 2).
	if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, GThink19ScreamRollMax) >= 1)  // 0x102931ed / 0x102931f3
	{
		return;
	}
	if (!GThink19ScreamDeathSearched)                                       // 0x102931f5 / 0x102931fc
	{
		GThink19ScreamDeathSearched = true;                                 // 0x1029320c
		// The `__strcmpi` walk of the VSound concept table (`0x10293214`..`0x10293249`):
		// an empty table skips it (`0x10293212` JLE, -1); a NULL name compares as "" (`0x10293224`);
		// `__strcmpi` `0x10293231`, and a match (`0x1029323b` JZ -> `0x1029338d`) stores that concept's id.
		GThink19ScreamDeathIndex = VSoundConceptId(TEXT("Scream_Death"));   // 0x1029324e
	}
	SpeakVSound(TEXT("Scream_Death"), GThink19ScreamDeathIndex, GThink19ScreamChannel,
		ElysiumNpcTunables::One, GThink19ScreamAttenuation);                // 0x1029326c 0x101f5950
}

void FElysiumNpc::Think19NormalSet1(double Now)
{
	// `m_flPlayerDist = SetClosestPlayer()` -- the body writes `+0x6264` and `m_hClosestPlayer`.
	Senses.SetClosestPlayer(*this, Now);                                    // 0x10292ef4 / 0x10292efd
	Think19EnemyTriple();                                                   // 0x10292f03..0x1029306a
	// Slot 221 `GetAngles()` -> `AngleVectors` into `m_vecForward` (+0x6290) and `m_vecRight`
	// (+0x629c), up NULL.
	StartTaskAngleVectors(RetailGetAnglesDegrees(), &Forward, &Right);      // 0x10293084 / 0x1029308b
	Senses.SetPlayerLos(*this, Now);                                        // 0x10293095 0x10291610
	CacheInterruptConditionsForMaintenance(Now);                            // 0x1029309c 0x1026a0f0
	if (NpcFlags.Has(EElysiumNpcFlag::ANIM_MOVEMENT))                       // 0x102930a1 / 0x102930b3
	{
		AutoMovement();                                                     // 0x102930b7 0x10280a50
	}
	Think19HintUpkeep();                                                    // 0x102930bc..0x10293171
	Think19ShootAtHint();                                                   // 0x10293177..0x102931bf
	Think19ScreamDeathRoll();                                               // 0x102931c9..0x1029326c
}

bool FElysiumNpc::Think19NormalSet2(double Now, float NormalInterval)
{
	ResolveStandingOnHead(NormalInterval);                                  // 0x1029330c 0x102bf820
	if (!NpcFlags.Has(EElysiumNpcFlag2::DONT_FALL_TO_GROUND))               // 0x10293311..0x1029331c
	{
		// `0x102bfdf0(interval)` -- an empty `RET 4` in the image (0x10293321).
	}
	if (Think19DebugTrackUnderGroundConVar() != 0)                          // 0x1029332e IsCommand / 0x10293333 / 0x1029333f
	{
		Think19TrackUnderGround(NormalInterval);                            // 0x10293344 0x102bfe10
	}
	// `0x102bf310`: slot 345 `SetPoseParameter("move_yaw", m_flDesiredMoveYaw (+0x63ec), 0)`.
	SetPoseParameter(TEXT("move_yaw"), ScheduleHost.DesiredMoveYaw, false); // 0x1029334c
	if (NpcFlags.Has(EElysiumNpcFlag2::DISAPPEAR))                          // 0x10293351 / 0x1029335b
	{
		// `m_hClosestPlayer` (+0x628c), NULL when `-1` or stale -- resolved separately for each test
		// (PVS: `0x1029336a` -1 / `0x10293387` serial; FVisible: `0x102933b6` -1 / `0x102933d3` serial).
		FElysiumEntity* Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x10293361..0x1029339d
		if (!Think19InPlayerPvs(Player)                                     // 0x102933a1 / 0x102933ab
			|| !FElysiumEntity::FVisible(Player, GThink19DisappearTraceMask, nullptr, 0))  // 0x102933e7 DIRECT 0x100a6fa0 / 0x102933ee
		{
			// `UTIL_Remove(this)` -- deferred in retail, so the think CONTINUES below.
			Kill();                                                         // 0x102933f1 0x101cd940
		}
	}
	if (!Think19AiConsoleGate())                                            // 0x102933fb / 0x10293402
	{
		if (!Think19NodeGraphBuilt())                                       // 0x10293408 / 0x1029340f
		{
			NextThink = static_cast<float>(Now) + GThink19RefusedRearmSeconds;  // 0x10293411..0x1029341f
		}
		return false;                                                       // 0x10293447 -> 0x102937c6
	}
	// The move clock's due test: `0x1029349e CALL 0x10004a34`, a thunk (`JMP 0x102906e0`) to a body
	// that is `MOV AL,0x1 / RET` and nothing else, called DIRECT (no slot, no species override).
	// Retail computes nothing here; the constant IS the port, not a seam.
	const bool bMoveDue = true;                                             // 0x1029349e / 0x102934a3
	const bool bAiDue = IsAiThinkDue();                                     // 0x102934a9 0x10290700
	const bool bReduced = !bAiDue;                                          // 0x10293566 / 0x10293568 SETZ
	// `RunAlternateAI(bReduced)` (`0x1028fd80`), then slot 432 with the same byte on a FALSE answer.
	if (!RunAlternateAI(bReduced))                                          // 0x1029356e / 0x10293575
	{
		RunAI(bReduced);                                                    // 0x1029357c slot 432
	}
	// `PerformMovement(PostRun(), !bMoveDue)`: `PostRun` runs `RunAnimation` and the anim events and
	// answers the interval (`0x1029358d FSTP`).
	const float Interval = PostRun();                                       // 0x10293584 0x1026c7c0
	PerformMovement(Interval, bMoveDue ? 0 : 1);                            // 0x10293597 SETZ / 0x1029359e 0x1026c120
	const double Frame = World != nullptr ? World->FrameSeconds() : ElysiumWorldClock::DefaultFrameSeconds;
	ElysiumNpcThink::CalcNextMoveThink(ScheduleHost, Now);                  // 0x10293632 0x10290fc0
	ElysiumNpcThink::CalcNextAiThink(ScheduleHost, ElysiumNpcThink::GatherInputs(*this), Now, Frame);  // 0x1029363e 0x10291230
	return true;
}

void FElysiumNpc::Think19Tail(double Now, bool bUpdateDue, float UpdateInterval)
{
	if (bUpdateDue)                                                         // 0x1029364a / 0x10293650
	{
		UpdateCharacterRetail(UpdateInterval);                              // 0x1029365b slot 312
		if (bIsTalking && !Dialogue.IsTalking(*this, Now))                  // 0x10293669 / 0x1029366d / 0x10293674
		{
			FinishTalking();                                                // 0x10293678 0x102c0ca0
		}
	}
	const double Frame = World != nullptr ? World->FrameSeconds() : ElysiumWorldClock::DefaultFrameSeconds;
	ElysiumNpcThink::CalcNextUpdateThink(ScheduleHost, ElysiumNpcThink::GatherInputs(*this), Now, Frame);  // 0x10293684 0x10290720
	ElysiumNpcThink::CalcNextNormalThink(ScheduleHost, ElysiumNpcThink::GatherInputs(*this), Now, Frame);  // 0x10293690 0x10290b60
	// `FCOMPP` of update (ST0) against normal: update <= normal, or unordered, picks the update stamp.
	NextThink = static_cast<float>(ScheduleHost.NextNormal < ScheduleHost.NextUpdate
		? ScheduleHost.NextNormal : ScheduleHost.NextUpdate);               // 0x102936a1..0x102936c0
	if (bJumping)                                                           // 0x102936ba / 0x102936c8
	{
		NextThink = static_cast<float>(Now) + ElysiumNpcTunables::Hundredth;  // 0x102936cf..0x102936d8
	}
	Think19DebugDistanceOverlay();                                          // 0x102936e0 0x1029bd40
}

// Slot 431: `0x10292de0`, 2552 bytes; 44 classes fill the slot with it.
// The engine profiler's bookkeeping has no port counterpart (it writes only `g_VProfCurrentProfile`
// and the budget-label stack, which nothing the bytecode reads observes; Unreal's stats stand in):
// - budget labels (`this == NULL` / NULL classname -> "NULL ENTITY" / ""): `0x10292df0`, `0x10292dfa`,
//   Set1 `0x10292ea5`, Set2 `0x102932b8`;
// - `VPROF_BUDGET("CAI_BaseNPC_NPCThink")` enter `0x10292e86`;
// - the refused path's scope exit: `0x1029343d`, `0x1029345a`, `0x10293465`, `0x10293470`, `0x10293491`;
// - the move scope's enter (label by move-due `0x102934b7`): `0x102934cc`, `0x102934d6`, `0x102934e4`,
//   `0x102934ee`, `0x10293512`, `0x10293548`, `0x1029354d`, `0x10293552`, `0x10293556`;
// - its exit: `0x102935b1`, `0x102935bb`, `0x102935ca`, `0x102935d1`, `0x102935d8`, `0x102935f8`,
//   `0x10293603`;
// - the tail's scope exit: `0x102936f3`, `0x102936fd`, `0x10293710`, `0x1029371d`, `0x10293783`,
//   `0x10293788`, `0x1029378d`, `0x10293790`, `0x1029379e`.
void FElysiumNpc::NPCThink()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	TroikaNPCThinkDebugPre();                                               // 0x10292e4d 0x10292500
	// `m_bfAINPCFlags2 &= 0x7ffffffb`: SCHEDULE_CHANGED (0x4) AND bit 31, before the disable test.
	NpcFlags.Clear(EElysiumNpcFlag2::SCHEDULE_CHANGED);                     // 0x10292e5e
	NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);        // 0x10292e66
	if (bDisableAi)                                                         // 0x10292e58 / 0x10292e6c
	{
		return;   // `m_flNextThink` untouched
	}
	// VProf "CAI_BaseNPC_NPCThink" and the three scope-trace frames ("Set1", "Set2"): absent.
	const bool bNormalDue = IsNormalThinkDue();                             // 0x10292e8e 0x102906c0
	if (bNormalDue)                                                         // 0x10292e97
	{
		Think19NormalSet1(Now);                                             // 0x10292e9d..0x10293271
	}
	const float UpdateInterval = static_cast<float>(Now - ScheduleHost.LastUpdate);  // 0x10293285 / 0x10293288
	const bool bUpdateDue = IsUpdateThinkDue();                             // 0x10293292 0x102906a0
	const float NormalInterval = static_cast<float>(Now - ScheduleHost.LastNormal);  // 0x10293297 / 0x1029329a
	if (bNormalDue)                                                         // 0x102932aa
	{
		if (!Think19NormalSet2(Now, NormalInterval))                        // 0x102932b0..0x10293643
		{
			return;
		}
	}
	Think19Tail(Now, bUpdateDue, UpdateInterval);                           // 0x1029364a..0x102936e0
}
