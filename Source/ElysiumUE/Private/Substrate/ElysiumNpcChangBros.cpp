#include "Substrate/ElysiumNpcChangBros.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"
#include "Substrate/ElysiumNpcPositionsShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `CNPC_VChangBros::TaskFail`'s write, `0x15d` (`1036d20a MOV dword ptr [ESI+0x5c54],0x15d`).
	constexpr int32 GCond10ChangBrosFailSchedule = 0x15d;
	constexpr float EnergyBallSpeed = 800.0f;       // DAT_104ada30
	// `_DAT_104ada34` — the segment-distance threshold `CheckJumpPathToHintNode` (`0x1036df50`)
	// tests against (`1036e04c FCOMP float ptr`), 100 SOURCE UNITS.
	constexpr float GHintsJumpPathClearanceUnits = ElysiumNpcTunables::ChangBrosJumpPathThreshold;
	// `_DAT_104ce8c0` — the squared-length floor `DistToSegment` (`0x103c6b70`) calls degenerate
	// before dividing by it: `103c6c02 FCOMP float ptr`, the float 1e-5 (in squared SOURCE units).
	// The arm answers `_DAT_104454c4`, the shared `0.0f`.
	constexpr float GHintsSegmentEpsilon = ElysiumNpcTunables::VampireBossSegmentLengthFloor;
	// The hint type `CNPC_VChangBros::StoreArenaCenter` walks the global hint list for.
	constexpr int32 GMiscArenaCenterHintType = 0x4651;
	constexpr float GChangJumpCooldown = 16.0f;     // _DAT_104ada00
	constexpr float GSuperJumpNearRise = 50.0f;     // _DAT_104ada1c
	constexpr float GSuperJumpFarRise = 150.0f;     // _DAT_104ada20
	constexpr float GSuperJumpSplit = 40.0f;        // _DAT_104ada3c
	constexpr int32 GActSuperJump = 0x7b;
	// `CNPC_VChangBros::GetSector` `0x1036e580`.
	constexpr float SectorZThreshold = 200.0f;     // _DAT_104ada6c
	constexpr float SectorOneDistSq = 270.0f;      // _DAT_104ada60 — compared to a SQUARED distance
	constexpr float SectorTwoRadius = 370.0f;      // _DAT_104ada64, squared by staticinit 0x1036e500
	constexpr float SectorThreeRadius = 525.0f;    // _DAT_104ada68, squared by staticinit 0x1036e550
	// `CNPC_VChangBros::GetTeleportPosition` `0x1036d270`.
	constexpr float ChangTeleportPositionMaxAge = 3.0f;   // _DAT_104ada04, seconds
	constexpr float ChangLastTeleportFloor = 100.0f;      // DAT_104ad9f4
	constexpr int32 HintChangTeleport = 18000;
	const TCHAR* const GChangEmitters[] = {
		TEXT("chang_teleport_in_emitter"),
		TEXT("chang_teleport_out_emitter"),
		TEXT("chang_powerup_emitter"),
		TEXT("chang_spine_emitter"),
		TEXT("chang_center_emitter"),
		TEXT("chang_blast_emitter"),
		TEXT("chang_ball_charge_emitter"),
	};
	const TCHAR* const GChangWeapons[] = {
		TEXT("item_w_chang_claw"),
		TEXT("item_w_chang_blade"),
		TEXT("item_w_chang_energy_ball"),
		TEXT("item_w_chang_ghost"),
	};
	// `_DAT_104ad9f8` = **0.1**, `CNPC_VChangBros`'s teleport health-loss threshold.
	constexpr float GChangTeleportHealthLoss = 0.1f;
	// `_DAT_104ada48` = **30.0** s and `_DAT_104ada4c` = **0.5**, the united-attack cooldown and the
	// health fraction each brother is tested against.
	constexpr double GChangUnitedCooldownSeconds = 30.0;
	constexpr float GChangUnitedHealthFraction = 0.5f;
}

const FElysiumNpcClass* FElysiumNpcChangBros::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x1036b050`.
void FElysiumNpcChangBros::NPCInit()
{
	ChangBrosNPCInit();
}

// Slot 104: `0x1036ae60`.
// 0x1036ae60 — and the Blade and Claw forms
void FElysiumNpcChangBros::Precache()
{
	// `CNPC_VChangBros::Precache` `0x1036ae60` — one body filling `CNPC_VChangBros#104`,
	// `CNPC_VChangBrosBlade#104` and `CNPC_VChangBrosClaw#104`, which the address key makes one arm.
	// Scope-trace frame, the Troika body, seven preload-1 emitters, four weapons.
	TroikaPrecache();
	for (const TCHAR* Emitter : GChangEmitters)
	{
		NpcKernelPrecache10Shared::Precache10Particle(*this, Emitter, /*Preload=*/1);
	}
	for (const TCHAR* Weapon : GChangWeapons)
	{
		NpcKernelPrecache10Shared::Precache10Other(*this, Weapon);
	}
}

// Slot 127: `0x1036b170`.
/** `CNPC_VChangBros::Restore` (`0x1036b170`), shared by `CNPC_VChangBros`, `CNPC_VChangBrosBlade`
 *  and `CNPC_VChangBrosClaw`. `CNPC_VVampireBoss::Restore` first and ITS answer is kept
 *  (`1036b1e3 MOV EDI,EAX` … `1036b210 MOV EAX,EDI`), then four writes in the listing's order:
 *    `m_fJumpGravity` (`+0x64b8`) = `_DAT_104ada44` (**2.3f**);
 *    `SetBodyEmitterName(0, "chang_powerup_emitter")`;
 *    `SetBodyEmitterName(1, "chang_powerup_emitter")`;
 *    `SetBodyEmitterName(2, "chang_spine_emitter")`.
 *  The gravity store is issued between the `FLD` and the first `SetBodyEmitterName` call
 *  (`1036b1ce FLD` / `1036b1db FSTP float ptr [ESI + 0x64b8]` / `1036b1e5 CALL`), so it lands
 *  first. */
int32 FElysiumNpcChangBros::Restore(void* Archive)
{
	// `CNPC_VChangBros::Restore` `0x1036b170`, shared by `CNPC_VChangBros`, `CNPC_VChangBrosBlade`
	// and `CNPC_VChangBrosClaw`.
	const int32 Result = VampireBossRestore(Archive);   // `1036b1c9`, and `EDI` keeps its answer
	// `1036b1ce FLD float ptr [0x104ada44]` / `1036b1db FSTP float ptr [ESI + 0x64b8]` — the store
	// is issued before the first emitter call.
	JumpGravity = ChangBrosJumpGravity;                 // +0x64b8, 2.3f
	SetBodyEmitterName(0, ChangPowerupEmitterName());   // `1036b1d9 PUSH 0x0`
	SetBodyEmitterName(1, ChangPowerupEmitterName());   // `1036b1ef PUSH 0x1`
	SetBodyEmitterName(2, ChangSpineEmitterName());     // `1036b1fd PUSH 0x2`
	return Result;                                      // `1036b210 MOV EAX,EDI`
}

// Slot 461: `0x1036b500`, the selector tag 0xa and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcChangBros::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0xa;
	return HumanSelectIdealState();
}

// Slot 604: `0x1036d800`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcChangBros::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatChangLine(true);
}

// Slot 448: `0x1036d1d0`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VChangBros::TaskFail` (`0x1036d1d0`), shared by `CNPC_VChangBrosBlade` and
 *  `CNPC_VChangBrosClaw` — the same 12..15 gate, but the write is `m_failSchedule` (`+0x5c54`) =
 *  `0x15d`. */
void FElysiumNpcChangBros::TaskFail(int32 Reason)
{
	// `CNPC_VChangBros::TaskFail` (`0x1036d1d0`), shared by `CNPC_VChangBrosBlade` and
	// `CNPC_VChangBrosClaw`. The same 12..15 gate as the AsianVampire arm, but the write is
	// `m_failSchedule` (`+0x5c54`) `= 0x15d` rather than the path-blocked flag.
	if (Reason >= NpcKernelConditions10Shared::GCond10PathFailFirst && Reason <= NpcKernelConditions10Shared::GCond10PathFailLast)
	{
		Schedule.FailScheduleOverride = GCond10ChangBrosFailSchedule;
	}
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x1036b460`.
// `0x1036b460`
// `0x1036b460`, `CNPC_VChangBros::TranslateSchedule`, the body of `FElysiumNpcChangBros::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcChangBros::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return ScheduleNumber == 0x17 ? 0x15d : TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 566: `0x1036c6a0`, a replacement that does not chain.
bool FElysiumNpcChangBros::FValidateHintType(void* Hint)
{
	// The whole body is `return 1;`: the hint is never read. `CNPC_VChangBrosBlade` and
	// `CNPC_VChangBrosClaw` fill the slot with this same body and inherit this override.
	(void)Hint;
	return true;
}

// Slot 546: `0x1036a3f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBros::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093aa70`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VChangBros"), TEXT("0x1036a3f0"), TEXT("0x1093aa70") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcChangBros::KillCenterEmitter()
{
	// The same pair on the single `m_hCenterEmitter` (+0x66f4), and it too leaves the handle alone.
	KillNamedEmitter(ChangCenterEmitter);
}

FElysiumEntityHandle FElysiumNpcChangBros::SpawnEnergyBall()
{
	// The muzzle attachment's basis: slot 219 answers the angles and `AngleVectors` the triple. No
	// attachment table reaches the kernel here, so this NPC's own facing stands in and the
	// substitution is named rather than hidden.
	const float YawDegrees = static_cast<float>(Angles.Y);
	const float Rad = FMath::DegreesToRadians(YawDegrees);
	const FVector FwdAxis(FMath::Cos(Rad), -FMath::Sin(Rad), 0.0);
	const FVector RightAxis(-FMath::Sin(Rad), -FMath::Cos(Rad), 0.0);
	const FVector UpAxis(0.0, 0.0, 1.0);

	const FVector Spawn = EnergyBallSpawnPoint(Origin / ElysiumMove::U, FwdAxis, RightAxis, UpAxis);
	const FElysiumEntityHandle Ball =
		CreateNamedEntity(TEXT("item_w_chang_energy_ball"), Spawn);

	// The fire is gated on BOTH the create succeeding and `m_hClosestPlayer` (+0x628c) resolving:
	// `(**(code **)(*piVar4 + 0x5d0))(this, DAT_104ada30, m_hClosestPlayer)` with the speed 800.0.
	if (Ball.IsSet() && Senses.Memory.ClosestPlayer.IsSet())
	{
		(void)EnergyBallSpeed;
	}
	return Ball;
}

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 4) ---

float FElysiumNpcChangBros::GetFacingTimeToTeleport() const
{
	// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`, 138 bytes:
	//     if (m_iSquadDisconnected < 1 && m_pSquad != NULL && NumSquadMembers(m_pSquad) > 1)
	//         return 21.0f;                                  // _DAT_104ada0c
	//     return 7.0f;                                       // _DAT_104ada08
	// The Chang brothers wait three times as long before teleporting while the other one is still
	// standing. `ConnectedSquad()` is already this runtime's `m_iSquadDisconnected < 1 ? m_pSquad :
	// NULL` (+0x5bb0, +0x5da4) and answers nothing, because no squad object exists here — so the
	// long arm is unreachable and the answer is the lone-brother 7 seconds.
	if (ConnectedSquad() != nullptr)
	{
		// `thunk_FUN_103160a0(m_pSquad) > 1` — the squad's member count. **SEAM**, unreachable
		// today.
		return 21.0f;
	}
	return 7.0f;
}

void FElysiumNpcChangBros::UpdateFacingTimer()
{
	// `CNPC_VChangBros::UpdateFacingTimer` `0x1036d600`, 392 bytes. Three nested gates; passing ALL
	// of them leaves `m_fFacingTime` alone, and anything else resets it to now — so the timer
	// measures how long the player has been standing close, level and looking this way.
	//
	//     Vector d = GetAbsOrigin() - player->GetAbsOrigin();
	//     if (|d.z| < 50.0)                                          // _DAT_104ada10
	//     {
	//         Vector flat( d.x, d.y, 0 );  float len = VectorNormalize( flat );
	//         if (len < 150.0 && 1e-05 < len)                        // _DAT_104ada14, _DAT_104ad9fc
	//         {
	//             QAngle a;  VectorAngles( flat, a );
	//             if (|AngleDiff( player->GetAbsAngles().y, a.y )| < 70.0) return;   // _DAT_104ada18
	//         }
	//     }
	//     m_fFacingTime = gpGlobals->curtime;
	//
	// The distances are Source units and this world is centimetres, hence `ElysiumMove::U`. The yaw
	// term compares the PLAYER's yaw against the yaw of the brother-minus-player delta, so it asks
	// whether the player is pointed at the brother — not the other way round.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	const bool bPlayerValid = Player != nullptr && !Player->IsInert()
		&& Player->Handle == Senses.Memory.ClosestPlayer;
	if (bPlayerValid)
	{
		const FVector Delta = Origin - Player->Origin;
		if (FMath::Abs(Delta.Z) < 50.0 * ElysiumMove::U)
		{
			FVector Flat(Delta.X, Delta.Y, 0.0);
			const float Length = static_cast<float>(Flat.Size());
			Flat = Flat.GetSafeNormal();
			if (Length < 150.0f * ElysiumMove::U && 1e-05f * ElysiumMove::U < Length)
			{
				const FVector FlatAngles = NpcKernelFacingShared::FacingRetailVectorAngles(Flat);
				const float YawDelta = NpcKernelFacingShared::FacingRetailAngleDiff(static_cast<float>(Player->Angles.Y),
					static_cast<float>(FlatAngles.Y));
				if (FMath::Abs(YawDelta) < 70.0f)
				{
					return;
				}
			}
		}
	}
	FacingTime = World != nullptr ? World->NowSeconds() : 0.0;
}

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 4) ---

int32 FElysiumNpcChangBros::JumpPathSector(const FVector& PositionCm) const
{
	// SEAM for `CNPC_VChangBros::GetSector(pos)`. The Chang fight partitions its arena into numbered
	// sectors; nothing on this substrate carries that partition.
	//
	// It answers 4 — the value `CheckJumpPathToHintNode` tests for, and the one that CLOSES its
	// gate. Both callers of `GetSector` in this band read it the same way: sector 4 refuses.
	// Refusing a jump that cannot be validated is the conservative side, and it agrees with family
	// Motor's `ChangBrosSector`, which answers 4 for `CheckForJumpAttack` for the same reason.
	(void)PositionCm;
	return 4;
}

float FElysiumNpcChangBros::DistToSegment(const FVector& A, const FVector& B, const FVector& P)
{
	// `CNPC_VVampireBoss::DistToSegment` `0x103c6b70`, arm for arm:
	//
	//     d = B - A;  lenSq = |d|^2;
	//     if (lenSq < _DAT_104ce8c0) return _DAT_104454c4;      // 0.0f, NOT |P-A|
	//     ap = P - A;  t = dot(ap, d) / lenSq;
	//     if (t > 0.0f) { distSq = (t < 1.0f) ? |P - (A + d*t)|^2 : |P - B|^2; }
	//     else          { distSq = |ap|^2; }
	//     return sqrt(distSq);
	//
	// THE DEGENERATE ARM IS `0.0`. A zero-length segment answers "no distance at all", not the
	// distance to the point — which for `CheckJumpPathToHintNode`, whose test is `dist < threshold`,
	// means a hint sitting exactly on the NPC always BLOCKS the jump. Story 29c-1's first pass here
	// wrote `|P-A|` and was wrong; family Positions found the divergence.
	//
	// Ghidra spells the sign test `(t < 0.0) == (t == 0.0)`, which is true only when both are false,
	// i.e. `t > 0`. `t == 0` therefore takes the `|ap|` arm — the same value the projection arm
	// would give at `t == 0`, so the split is not observable, but it is retail's split and is kept.
	//
	// 3D throughout: `CheckJumpPathToHintNode` zeroes the Z of the two ENDPOINTS before calling and
	// leaves the player's Z alone, so the Z difference does reach the answer.
	const FVector D = B - A;
	const float LenSq = static_cast<float>(D.SizeSquared());
	if (LenSq < GHintsSegmentEpsilon)
	{
		return NpcKernelHintsShared::GHintsZero;
	}
	const FVector AP = P - A;
	const float T = static_cast<float>(FVector::DotProduct(AP, D)) / LenSq;
	float DistSq;
	if (T > NpcKernelHintsShared::GHintsZero)
	{
		DistSq = T < 1.0f
			? static_cast<float>((P - (A + D * T)).SizeSquared())
			: static_cast<float>((P - B).SizeSquared());
	}
	else
	{
		DistSq = static_cast<float>(AP.SizeSquared());
	}
	return FMath::Sqrt(DistSq);
}

bool FElysiumNpcChangBros::CheckJumpPathToHintNode(const FHintWords& Hint) const
{
	// `CNPC_VChangBros::CheckJumpPathToHintNode` (`0x1036df50`).
	//
	// The segment is (my origin, the hint's origin) with the Z of BOTH endpoints forced to 0 — the
	// decompiler shows `fStack_4 = 0.0` overwriting the origin's Z it had just read, and
	// `uStack_10 = 0` doing the same to the hint end.
	if (!Hint.bValid)
	{
		return false;
	}
	// `m_hClosestPlayer` (`+0x628c`), the sense pass's cache — `FElysiumNpcMemory::ClosestPlayer`.
	// Dead or unset and retail falls straight to the blocked return without asking anything else.
	const FElysiumNpcMemory& Mem = Senses.Memory;
	FElysiumEntity* ClosestPlayer = Mem.ClosestPlayer.IsSet() && World
		? World->Resolve(Mem.ClosestPlayer) : nullptr;
	if (ClosestPlayer == nullptr || ClosestPlayer->IsInert())
	{
		return false;
	}
	// Retail's numbers are SOURCE units, so the segment and both probes are taken in them.
	FVector From = Origin / ElysiumMove::U;
	From.Z = 0.0;
	FVector To = Hint.OriginCm / ElysiumMove::U;
	To.Z = 0.0;

	if (DistToSegment(From, To, ClosestPlayer->Origin / ElysiumMove::U)
		< GHintsJumpPathClearanceUnits)
	{
		// The player stands on the jump line. Blocked.
		return false;
	}
	// Only when the player is clear does retail ask about the other brother, and a brother ON the
	// line blocks. `GetOtherBrother` is family Squad's ported body (`0x1036e2f0`).
	if (const FElysiumNpc* Brother = GetOtherBrother())
	{
		if (DistToSegment(From, To, Brother->Origin / ElysiumMove::U) < GHintsJumpPathClearanceUnits)
		{
			return false;
		}
	}
	// Both endpoints must be outside sector 4. Retail tests MY sector first and only asks about the
	// hint's when mine is not 4, so a body already in sector 4 is blocked without a second query.
	if (JumpPathSector(Origin) == 4)
	{
		return false;
	}
	return JumpPathSector(Hint.OriginCm) != 4;
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

void FElysiumNpcChangBros::ChangBrosNPCInit()
{
	VampireBossNPCInit();
	ChangLastTeleportPosition = FVector::ZeroVector;                     // +0x66bc CORRECTION
	ChangLastTeleportTime = 0.0;                                         // +0x66c8
	LastJumpTime = 0.0;                                                  // +0x66cc
	FacingTime = 0.0;                                                    // +0x66d0
	bChangCenterStored = false;                                          // +0x66e8
	ChangLastUnitedAttackTime = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);                 // +0x66ec CURTIME
	JumpGravity = ChangBrosJumpGravity;
	SetBodyEmitterName(0, TEXT("chang_powerup_emitter"));
	SetBodyEmitterName(1, TEXT("chang_powerup_emitter"));
	SetBodyEmitterName(2, TEXT("chang_spine_emitter"));
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

void FElysiumNpcChangBros::StoreArenaCenter()
{
	// `CNPC_VChangBros::StoreArenaCenter` `0x1036e400` (name recovered by `NameFromStrings` at
	// `0x106313a0`), scope trace stripped:
	//     node = DAT_10925450;                                   // the global CAI_Hint list head
	//     if (!node) return;
	//     while (node->m_nHintType (+0x5dc) != 0x4651) {
	//         node = node->next (+0x5d8);
	//         if (!node) return;                                 // no write at all
	//     }
	//     pos = node->vtable[+0x364]();                          // GetAbsOrigin
	//     m_vArenaCenter = pos;                                  // +0x66dc, three floats
	//     m_bCenterStored = 1;                                   // +0x66e8
	//
	// Both writes are inside the found arm: a map with no `0x4651` hint leaves `m_bCenterStored`
	// false and the centre at whatever it was, which is what the Chang fight's own guard reads.
	//
	// The walk is exactly family **Squad**'s `NthHintOfType(type, 0)` seam — the global hint list,
	// the same next link and the same type word — so that accessor is asked rather than a second
	// walk stood beside it. It answers null (no hint store carries hint types here), so this takes
	// retail's "fell off the end" arm and writes nothing.
	const FElysiumEntity* Node = NthHintOfType(GMiscArenaCenterHintType, 0);
	if (Node == nullptr)
	{
		return;
	}
	ChangArenaCenter = Node->Origin;
	bChangCenterStored = true;
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

int32 FElysiumNpcChangBros::ChangBrosSector(const FVector& PositionUnits) const
{
	// `CNPC_VChangBros::GetSector(pos)`. **SEAM**: this substrate has no sector partition. It
	// answers 4, which is the value `CheckForJumpAttack` tests for and which CLOSES its gate — the
	// jump attack is refused rather than allowed on a guess.
	(void)PositionUnits;
	return 4;
}

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

bool FElysiumNpcChangBros::CheckForJumpAttack()
{
	// `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`, arm for arm:
	//
	//     if (m_ChangType != 0) return false;
	//     CBaseEntity* p = m_hClosestPlayer;  if (!p) return false;
	//     if (GetSector(p->GetAbsOrigin()) == 4) return false;
	//     if (GetSector(GetAbsOrigin())  == 4) return false;
	//     if (curtime - m_fLastJumpTime < 16.0) return false;             // _DAT_104ada00
	//     if (m_iSquadDisconnected > 0 || !m_pSquad) return true;
	//     for (i = 0; i < squad->NumMembers(); ++i) {
	//         CNPC_VChangBros* b = dynamic_cast<…>(squad->GetMember(i));
	//         if (b && b != this && curtime - b->m_fLastJumpTime < 16.0) return false;
	//     }
	//     return true;
	//
	// Note the shape of the sector test: BOTH the player and this NPC must be out of sector 4, and
	// the second read is of this NPC's own origin, not the player's.
	if (ChangType != 0)
	{
		return false;
	}
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	if (ChangBrosSector(NpcKernelMotor2Shared::MotorTailSourceOf(Player->Origin)) == 4)
	{
		return false;
	}
	if (ChangBrosSector(NpcKernelMotor2Shared::MotorTailSourceOf(Origin)) == 4)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now - LastJumpTime < GChangJumpCooldown)
	{
		return false;
	}
	// The squad walk. `ConnectedSquad()` is the Squad family's seam and answers null, so a
	// disconnected or squadless brother is the arm every ChangBros takes today — which is the arm
	// that ALLOWS the jump, not the one that refuses it.
	if (BaseScheduleHost.SquadDisconnected > 0 || ConnectedSquad() == nullptr)
	{
		return true;
	}
	const int32 Count = SquadMemberCount(ConnectedSquad());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FElysiumEntity* Member = SquadMember(ConnectedSquad(), Index);
		if (Member == nullptr)
		{
			continue;
		}
		FElysiumNpc* Brother = Member->AsNpc();
		// `___RTDynamicCast(member, 0, CNPC_VChangBros)` — a sibling of the same retail class only.
		if (Brother == nullptr || Brother == this || !Brother->IsRetailClass(TEXT("CNPC_VChangBros")))
		{
			continue;
		}
		const FElysiumNpcChangBros* const Chang = Brother->AsSpecies<FElysiumNpcChangBros>();
		if (Chang == nullptr)
		{
			continue;
		}
		if (Now - Chang->LastJumpTime < GChangJumpCooldown)
		{
			return false;
		}
	}
	return true;
}

void FElysiumNpcChangBros::SetupSuperJump(float Enabled)
{
	// `CNPC_VChangBros::SetupSuperJump` `0x1036e160`:
	//
	//     if (param_1 == 0.0) return;
	//     m_vJumpOrigin = GetAbsOrigin();
	//     m_vJumpTarget = m_pHintNode->GetAbsOrigin();
	//     float top = m_pHintNode->GetAbsOrigin().z;
	//     if (top < GetAbsOrigin().z) top = GetAbsOrigin().z;      // the HIGHER of the two
	//     float dz = top - GetAbsOrigin().z;                       // >= 0 by construction
	//     m_fJumpHeight = (ABS(dz) < 40.0 ? 50.0 : 150.0) + dz;    // _DAT_104ada3c/1c/20
	//     ClearCondition(0x7b);                                    // 0x10269b50
	//     thunk_FUN_102c4e80(this);
	//
	// The ledger's one-line walk reads `thunk_FUN_10269b50` as "plays activity 0x7b"; it is
	// `ClearCondition`, and 0x7b is a species-registered condition number, not an activity.
	if (Enabled == 0.0f)
	{
		return;
	}
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	FVector HintUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(BaseScheduleHost.HintNode, HintUnits))
	{
		// **SEAM**: the hint store carries no origins, so retail's two reads of
		// `m_pHintNode->GetAbsOrigin()` cannot be made. Retail would crash on a null hint here — it
		// dereferences `m_pHintNode` without a check — so the port refuses instead, and says so.
		return;
	}
	JumpOrigin = SelfUnits;
	JumpTarget = HintUnits;
	float Top = static_cast<float>(HintUnits.Z);
	if (Top < static_cast<float>(SelfUnits.Z))
	{
		Top = static_cast<float>(SelfUnits.Z);
	}
	const float DeltaZ = Top - static_cast<float>(SelfUnits.Z);
	const float Rise = FMath::Abs(DeltaZ) < GSuperJumpSplit ? GSuperJumpNearRise : GSuperJumpFarRise;
	JumpHeight = Rise + DeltaZ;
	Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(GActSuperJump));
	CommitSetupJump();
}

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcChangBros::SelectTeleportNodeChangRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, TFunctionRef<bool(const FVector&, float)> Clear,
	TFunctionRef<int32(const FVector&)> Sector)
{
	// Types 18000, `0x4653` and `0x4652`; the plain 3-D distance to the player; the clearance gate
	// at `DAT_104ad9f4 = 100.0`. TWO bests are kept — the nearest of all candidates, and the nearest
	// whose sector matches the PLAYER's (retail takes the reference sector from
	// `GetSector(player->GetAbsOrigin())`, not from its own origin) — and the sector-matched one
	// REPLACES the plain one when it exists. A brother teleports to the player's half of the arena
	// when he can and to the nearest node when he cannot.
	int32 BestPlain = INDEX_NONE;
	int32 BestSector = INDEX_NONE;
	float BestPlainDistance = TNumericLimits<float>::Max();
	float BestSectorDistance = TNumericLimits<float>::Max();
	const int32 PlayerSector = Sector(PlayerCm);
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != HintChangTeleport && Type != NpcKernelPositionsShared::HintLedge && Type != NpcKernelPositionsShared::HintJumpbase)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, NodeCm));
		if (Distance < ChangLastTeleportFloor * NpcKernelPositionsShared::U)
		{
			continue;
		}
		if (!Clear(NodeCm, ChangLastTeleportFloor * NpcKernelPositionsShared::U))
		{
			continue;
		}
		if (Distance < BestPlainDistance)
		{
			BestPlainDistance = Distance;
			BestPlain = Index;
		}
		if (PlayerSector == Sector(NodeCm) && Distance < BestSectorDistance)
		{
			BestSectorDistance = Distance;
			BestSector = Index;
		}
	}
	return BestSector != INDEX_NONE ? BestSector : BestPlain;
}

int32 FElysiumNpcChangBros::SelectTeleportNodeChang()
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
	const int32 Pick = SelectTeleportNodeChangRule(Nodes, Player->Origin,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportChang(PositionCm, ClearanceCm);
		},
		[this](const FVector& PositionCm) { return GetSector(PositionCm); });
	if (Pick == INDEX_NONE)
	{
		// The same named divergence: retail dereferences the null winner.
		return INDEX_NONE;
	}
	ChangLastTeleportPosition = Nodes[Pick].OriginCm;
	ChangLastTeleportTime = World != nullptr ? World->NowSeconds() : 0.0;
	return NodeIds[Pick];
}

bool FElysiumNpcChangBros::PositionClearForTeleportChang(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x1036d350`, the whole body:
	//     if (|GetAbsOrigin() - pos| < clearance)        return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)   return false;   // DAT_104ad9f4
	//     if (ConnectedSquad()) {
	//         if (thunk_FUN_10315a80(squad, pos, clearance)) return false;
	//         for (i = 0; i < NumSquadMembers(squad); ++i) {
	//             CNPC_VChangBros* m = dynamic_cast<CNPC_VChangBros*>( SquadMember(squad, i) );
	//             if (m && m != this && m->GetTeleportPosition(&p) && |p - pos| < clearance)
	//                 return false;
	//         }
	//     }
	//     return true;
	// The member count is re-read every iteration, which is how retail leaves it. The two brothers
	// therefore never claim the same spot inside the cooldown `GetTeleportPosition` enforces.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(ChangLastTeleportPosition, PositionCm))
		< ChangLastTeleportFloor * NpcKernelPositionsShared::U)
	{
		return false;
	}
	if (ConnectedSquad() != nullptr)
	{
		if (SquadPositionTaken(PositionCm, ClearanceCm))
		{
			return false;
		}
		TArray<FElysiumNpc*> Members;
		SquadMembers(Members);
		for (FElysiumNpc* Member : Members)
		{
			if (Member == nullptr || Member == this)
			{
				continue;
			}
			// `___RTDynamicCast(member, 0, …, CNPC_VChangBros, 0)` — a brother, not any squadmate.
			if (!Member->IsRetailClass(TEXT("CNPC_VChangBros")))
			{
				continue;
			}
			FElysiumNpcChangBros* const Chang = Member->AsSpecies<FElysiumNpcChangBros>();
			if (Chang == nullptr)
			{
				continue;
			}
			FVector Claimed;
			if (Chang->GetTeleportPosition(Claimed)
				&& static_cast<float>(FVector::Dist(Claimed, PositionCm)) < ClearanceCm)
			{
				return false;
			}
		}
	}
	return true;
}

int32 FElysiumNpcChangBros::GetSector(const FVector& PositionCm) const
{
	// `0x1036e580`, read from the listing because the decompiler turned both FPU compares into
	// `(a < b) != (a == b)`:
	//
	//     if (!m_bCenterStored) return 0;                              // +0x66e8
	//     float d2 = (p.x - c.x)^2 + (p.y - c.y)^2;                    // XY ONLY
	//     if (p.z - c.z >= 200.0)          return d2 <= 525^2 ? 3 : 4; // _DAT_104ada6c, _DAT_104ada68
	//     if (d2 <= 270.0)                 return 1;                   // _DAT_104ada60
	//     if (d2 <= 370^2)                 return 2;                   // _DAT_104ada64
	//     return 4;
	//
	// The sector-1 threshold is the odd one and is reproduced as it stands: 270 is compared against
	// a SQUARED distance while 370 and 525 are squared first by their own static initializers
	// (`0x1036e500` and `0x1036e550`). Sector 1 is therefore a circle of radius ~16.4 units around
	// the arena centre, not 270 — a shipped inconsistency the two bodies that read the sector
	// (`SelectTeleportNode` and `IsUnreachable`) were tuned against.
	if (!bChangCenterStored)
	{
		return 0;
	}
	const double Dx = PositionCm.X - ChangArenaCenter.X;
	const double Dy = PositionCm.Y - ChangArenaCenter.Y;
	const float DistSq = static_cast<float>(Dx * Dx + Dy * Dy);
	if (PositionCm.Z - ChangArenaCenter.Z >= SectorZThreshold * NpcKernelPositionsShared::U)
	{
		if (DistSq <= SectorThreeRadius * SectorThreeRadius * NpcKernelPositionsShared::U * NpcKernelPositionsShared::U)
		{
			return 3;
		}
		return 4;
	}
	if (DistSq <= SectorOneDistSq * NpcKernelPositionsShared::U * NpcKernelPositionsShared::U)
	{
		return 1;
	}
	if (DistSq <= SectorTwoRadius * SectorTwoRadius * NpcKernelPositionsShared::U * NpcKernelPositionsShared::U)
	{
		return 2;
	}
	return 4;
}

bool FElysiumNpcChangBros::SectorIsInPit(int32 Sector)
{
	// `0x1036b6b0`: `return 0 < sector && sector < 3;` — sectors 1 and 2 are the pit, and the retail
	// `ScopeTrace` push/pop around it carries no gameplay data. Sector 0 ("no centre stored") and
	// sectors 3 and 4 are not the pit, which is what makes `IsUnreachable`'s pit-versus-3 pair
	// meaningful.
	return 0 < Sector && Sector < 3;
}

bool FElysiumNpcChangBros::GetTeleportPosition(FVector& OutPositionCm) const
{
	// `0x1036d270`:
	//     if (3.0 < gpGlobals->curtime - m_fLastTeleportTime) return false;   // _DAT_104ada04
	//     *out = m_vLastTeleportPosition;
	//     return true;
	// A claim expires three seconds after it was made, which is the window inside which the other
	// brother's `PositionClearForTeleport` will refuse the same spot.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (ChangTeleportPositionMaxAge < static_cast<float>(Now - ChangLastTeleportTime))
	{
		return false;
	}
	OutPositionCm = ChangLastTeleportPosition;
	return true;
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 4) ---

bool FElysiumNpcChangBros::IsUnreachableChang(FElysiumEntity* Unreachable)
{
	// `CNPC_VChangBros::IsUnreachable` `0x1036e6f0`, slot 530 for the three Chang classes:
	//
	//     CBaseCombatCharacter* c = dynamic_cast<CBaseCombatCharacter*>(pTarget);
	//     if (c) {
	//         int mine = GetSector( GetAbsOrigin() );
	//         int his  = GetSector( c->GetAbsOrigin() );
	//         if (his == 3 && SectorIsInPit(mine)) return true;
	//         if (SectorIsInPit(his) && mine == 3) return true;
	//     }
	//     return CAI_BaseNPC::IsUnreachable( pTarget );
	//
	// The rule is symmetric and it is about the ARENA, not about pathing: one of the two is in the
	// pit (sector 1 or 2) and the other is on the ledge (sector 3), so neither can walk to the
	// other. A target that is not a combat character skips it entirely and takes the base cache.
	const FElysiumCombatCharacter* AsCharacter =
		Unreachable != nullptr ? Unreachable->AsCombatCharacter() : nullptr;
	if (AsCharacter != nullptr)
	{
		const int32 MySector = GetSector(Origin);
		const int32 HisSector = GetSector(Unreachable->Origin);
		if (HisSector == 3 && SectorIsInPit(MySector))
		{
			return true;
		}
		if (SectorIsInPit(HisSector) && MySector == 3)
		{
			return true;
		}
	}
	return IsUnreachable(Unreachable);
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// `CNPC_VChangBros` — `0x1036c7f0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcChangBros::FUN_1036c7f0(int32 InChangType)
{
	// `0x1036c7f0`, thirteen bytes: `m_ChangType (+0x66b8) = param_1`.
	//
	// A plain setter over family **Squad**'s `ChangType`, which `CNPC_VChangBros::SelectUnitedNode`
	// (`0x1036d100`) later reads to pick the twin's node. Note the OFFSET COLLISION this family's
	// standing facts warn about: `+0x66b8` is `CNPC_VAndreiBlood::m_iActiveRunnerCount` two bodies
	// up this file, `CNPC_VManBat::m_bHasPlayedFlyBySound` on family Sounds' and
	// `CNPC_VAsianVampire::m_vLastJumpPosition` on family Motor's. Four species, one offset, four
	// members — writing through Squad's is what keeps the Chang reading in one place.
	//
	// Retail name unrecovered; `SetChangType` is the concern, not a recovered symbol.
	ChangType = InChangType;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

const TCHAR* FElysiumNpcChangBros::ChangPowerupEmitterName()
{
	return TEXT("chang_powerup_emitter");   // `0x10630e54`
}

const TCHAR* FElysiumNpcChangBros::ChangSpineEmitterName()
{
	return TEXT("chang_spine_emitter");     // `0x10630e3c`
}

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

bool FElysiumNpcChangBros::CheckForTeleport()
{
	// `1036cae5`: `m_ChangType != 1`. Type 1 answers false without reading anything else.
	if (ChangType == 1)
	{
		return false;
	}
	// `1036caee`: `0x103c6a20 >= _DAT_104ad9f8` (0.1). The percent RISES with damage (standing fact
	// one), so this is "lost a tenth of the bar since the mark".
	if (HealthPercentLostSinceRecord() >= GChangTeleportHealthLoss)
	{
		return true;
	}
	// `1036cb17`: the brother is resolved AFTER the health test, and the type is re-tested as 0 —
	// type 2 reaches here and is refused.
	const FElysiumNpcChangBros* Brother = GetOtherBrother();
	if (Brother == nullptr || ChangType != 0)
	{
		return false;
	}
	// `1036cb45`: `curtime - m_fFacingTime > GetFacingTimeToTeleport()`, strictly greater (the
	// decompiler's `a < b != (a == b)` idiom).
	const double Elapsed = NpcKernelSpeciesMisc10Shared::SpeciesMisc10Now(*this) - FacingTime;
	return Elapsed > static_cast<double>(GetFacingTimeToTeleport());
}

bool FElysiumNpcChangBros::CheckForUnited()
{
	// `1036cc05`: no other brother answers false at once.
	const FElysiumNpcChangBros* Brother = GetOtherBrother();
	if (Brother == nullptr)
	{
		return false;
	}
	// `1036cc18`: `m_fLastUnitedAttackTime + _DAT_104ada48 (30.0) < curtime`, strictly.
	if (!(ChangLastUnitedAttackTime + GChangUnitedCooldownSeconds < NpcKernelSpeciesMisc10Shared::SpeciesMisc10Now(*this)))
	{
		return false;
	}
	// `1036cc4c`: true UNLESS BOTH percents are below `_DAT_104ada4c` (0.5). The percent is
	// wounds/cap, so "below 0.5" is the HEALTHY half and two healthy brothers refuse the united
	// attack — the short-circuit means one hurt brother is enough to allow it.
	if (GetCurrHealthPercent() < GChangUnitedHealthFraction
		&& Brother->GetCurrHealthPercent() < GChangUnitedHealthFraction)
	{
		return false;
	}
	return true;
}

int32 FElysiumNpcChangBros::ChangBrosSelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
	const FVector& MeasureFromCm)
{
	// `1036cfe8`: the incumbent is seeded `-FLT_MAX` and replaced on a STRICTLY GREATER distance, so
	// the FARTHEST reachable ledge wins — the opposite comparison from `CNPC_VSheriffMan`'s
	// `0x103b0ab0`, which family Positions' `SelectLedgeNodeRule` carries.
	int32 Best = INDEX_NONE;
	float BestDistance = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		// `1036cffb`: `node->+0x5dc == 0x4653`, the ledge type.
		if (Nodes[Index].HintType != 0x4653)
		{
			continue;
		}
		// `1036d02b`: the full 3-D distance between the node's `GetAbsOrigin` and MY own. The
		// `CheckJumpPathToHintNode` gate at `1036d005` is applied by the caller below.
		const float Distance = static_cast<float>(FVector::Dist(MeasureFromCm,
			Nodes[Index].OriginCm));
		if (BestDistance < Distance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcChangBros::ChangBrosSelectLedgeNode() const
{
	// `1036cfd8`: the global hint list `DAT_10925450`, walked through `+0x5d8`. Unlike the Sheriff's
	// twin there is NO closest-player gate in front of this one.
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	// `1036d005`: `CheckJumpPathToHintNode(this, node)` gates every candidate, in list order. It is
	// applied here rather than inside the rule because family Hints' `JumpPathSector` is a SEAM
	// answering 4, which closes the gate for every node — retail's own refusal for a brother already
	// in sector 4 — and the farthest-wins comparison has to stay readable under it.
	TArray<FHintWords> Accepted;
	TArray<int32> AcceptedIds;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (CheckJumpPathToHintNode(Nodes[Index]))
		{
			Accepted.Add(Nodes[Index]);
			AcceptedIds.Add(NodeIds[Index]);
		}
	}
	const int32 Pick = ChangBrosSelectLedgeNodeRule(Accepted, Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : AcceptedIds[Pick];
}

// --- Moved from `ElysiumNpcSquad.cpp` (story 5 step 4) ---

FElysiumNpcChangBros* FElysiumNpcChangBros::GetOtherBrother() const
{
	// 0x1036e2f0 `CNPC_VChangBros::GetOtherBrother`: with `m_iSquadDisconnected < 1` and a live
	// `m_pSquad`, walk `0..NumMembers()` re-reading `NumMembers()` every iteration, `RTDynamicCast`
	// each member to `CNPC_VChangBros` and answer the first one that is not me. Else 0.
	if (BaseScheduleHost.SquadDisconnected >= 1)
	{
		return nullptr;
	}
	const void* Squad = ConnectedSquad();
	if (Squad == nullptr)
	{
		return nullptr;
	}
	for (int32 Index = 0; Index < SquadMemberCount(Squad); ++Index)
	{
		FElysiumEntity* Member = SquadMember(Squad, Index);
		if (Member == nullptr)
		{
			continue;
		}
		FElysiumNpc* Brother = Member->AsNpc();
		// The RTTI cast, as the chain walk this runtime dispatches species by.
		if (Brother != nullptr && Brother != this
			&& Brother->IsRetailClass(TEXT("CNPC_VChangBros")))
		{
			// The typed view of the brother the cast admitted, for his own words.
			if (FElysiumNpcChangBros* const Typed = Brother->AsSpecies<FElysiumNpcChangBros>())
			{
				return Typed;
			}
		}
	}
	return nullptr;
}

bool FElysiumNpcChangBros::ReadyForUnited() const
{
	// 0x1036e820 `CNPC_VChangBros::ReadyForUnited`: `GetCurSchedule()` (`0x1028a150`) and its
	// schedule id (`CAI_Schedule+0x00`) against 0x15a or 0x15b, false when there is no schedule.
	//
	// The port's schedule set does not carry the two ChangBros `UNITED` programs yet — the
	// comparison is retail's and nothing in `int32` numbers 0x15a/0x15b — so this
	// answers false until they are registered. Not a stub: the two numbers ARE the rule.
	if (!Schedule.IsRunning())
	{
		return false;
	}
	const int32 Number =
		IdSpace(EElysiumIdCategory::Schedule)->GlobalToLocal(Schedule.Current);
	return Number == 0x15a || Number == 0x15b;
}

FElysiumEntity* FElysiumNpcChangBros::SelectUnitedNode() const
{
	// 0x1036d100 `CNPC_VChangBros::SelectUnitedNode`: walk the global hint list from
	// `DAT_10925450` following `+0x5d8`, count the nodes whose `m_nHintType` (`+0x5dc`) is 18000,
	// and return the FIRST for `m_ChangType == 0`, the SECOND for `m_ChangType == 1` (the arm is
	// `type == 1 && count > 0`, so it is the match after the first). Any other `m_ChangType` walks
	// the whole list and answers 0.
	if (ChangType == 0)
	{
		return NthHintOfType(18000, 0);
	}
	if (ChangType == 1)
	{
		return NthHintOfType(18000, 1);
	}
	return nullptr;
}

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

