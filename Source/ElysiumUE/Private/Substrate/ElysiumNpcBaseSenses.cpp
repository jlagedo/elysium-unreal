// `CAI_BaseNPC`'s bodies of the `Senses` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSenses.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcWitness.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_1049e0c8`, a DOUBLE (`10326cbf  FCOMP double ptr [0x1049e0c8]`) with exactly ONE reader
	// in the image — slot 364. 0.994 is a 6.28-degree half-angle: the aim cone is far narrower than
	// the view cone (`0.2`, `ElysiumNpcSense::DefaultViewConeDot`), which is what makes
	// `FInAimCone` a firing test and not a seeing test.
	constexpr double GAimConeDotFloor = 0.994;
	// `_DAT_104563b0` = 4096.0f, compared against a SQUARED distance — so the occlusion edge trips
	// once the enemy has moved 64 Source units from where it was when sight was lost.
	constexpr float GEnemyWentOccludedDistanceSqUnits = 4096.0f;
	// `_DAT_10454110` = 5.0f and `_DAT_1044eb0c` = 20.0f — `OnDoorBlocked`'s two retry windows. The
	// 5.0 cell is also the detected-attack window.
	constexpr float GDoorRetryShortSeconds = ElysiumNpcTunables::Five;
	constexpr float GDoorRetryLongSeconds = 20.0f;
	// `_DAT_104454c0` = 1.0f — the alternate-AI hit-info window `OnDoorBlocked` re-arms.
	constexpr float GDoorAlternateAiWindowSeconds = 1.0f;
	// `_DAT_10463584` = 15.0f — `SetSquadFocus`'s own expiry (`squad+0x74`); slot 616's fire-immune
	// window reads the same pooled cell.
	constexpr float GSquadFocusLifetimeSeconds = ElysiumNpcTunables::Fifteen;
	// `CBaseDoor+0x644`'s two tested bits. Their retail names are **unrecovered** — no corpus body
	// in layers 0–9 declares the word — so they are carried by value beside the body that reads
	// each. `0x10` skips the unreachable marking entirely; `0x40` shortens the retry to 5 s.
	constexpr uint32 GDoorFlagNoBlockRetry = 0x10u;
	constexpr uint32 GDoorFlagShortBlockRetry = 0x40u;
	// `m_nHintType == 0xd` — `CAI_Hint::IsViewable`'s one viewable type. The number is a literal in
	// the body and the hint-type vocabulary is not otherwise recovered, so it is carried as 13.
	constexpr int32 GViewableHintType = 13;
	// Slot 532's reason word for "the door blocked me". `npc-kernel/signatures.md` slot 532 lists
	// all four: 1 from `0x1027dd10` (fully open), 2 from here, 4 from `0x1027dfb0`, 8 from
	// `OnScheduleChange`.
	constexpr int32 GSlot532ReasonDoorBlocked = 2;
	// The two `m_eAlternateAI` modes `OnDoorBlocked` promotes to 3.
	constexpr int32 GAlternateAiModeFacing = 1;
	constexpr int32 GAlternateAiModeOpening = 2;
	constexpr int32 GAlternateAiModeBlocked = 3;
	// `DAT_109203f0` — the ONE shared `CAI_Enemies` every squad-disconnected NPC is handed instead
	// of its own. A retail GLOBAL, ported as one: a single store for the whole level, not one per
	// NPC. Nothing in this runtime writes into it; it exists so `GetEnemies()` answers a real
	// object on the disconnected arm rather than null, which is what retail answers.
	FElysiumNpcEnemyMemory GDisconnectedEnemies;
}

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 5) ---

FVector FElysiumNpcBase::EarPosition()
{
	// `0x100b4c00`, 20 bytes: `(**(code**)(*this + 0x304))(out); return out;` — a tail call to slot
	// 193, `EyePosition`, for the side effect of filling the out-vector, and the same pointer back.
	// The ear IS the eye on every class in the family: 82 classes fill slot 196 and every one of
	// them with this body.
	return EyePosition();
}

bool FElysiumNpcBase::AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm,
	const FVector& Aim)
{
	// Slot 364's arithmetic, with the three virtual reads lifted out. See `FInAimCone` below for
	// the listing this transcribes.
	FVector Delta = TargetCm - OriginCm;
	Delta.Z = 0.0;
	if (!Delta.Normalize())
	{
		// `VectorNormalize` of a zero vector leaves the components alone and answers length 0;
		// retail then dots zeros against 0.994, which refuses. Same answer.
		return false;
	}
	return FVector::DotProduct(Delta, Aim) > GAimConeDotFloor;
}

bool FElysiumNpcBase::FInAimCone(const FVector& TargetCm)
{
	// `0x10326bd0`, 283 bytes of which the scope-trace push/pop is 200. The computation, read off
	// the listing because the decompiler lost two of the three operands:
	//
	//     d = target - GetAbsOrigin()          (slot 217, vtable +0x364)
	//     d.z = 0                              (10326c83  MOV [ESP+0xc],0x0)   <-- BEFORE normalise
	//     VectorNormalize(&d)                  (PTR_thunk_FUN_10137220)
	//     return dot(d, EyeDirection2D()) > 0.994   (slot 372, vtable +0x5d0; FCOMP double)
	//
	// **The Z is zeroed before the normalise, not after**, so the test is planar in both operands
	// and a target directly overhead is at dot 0. 29c's walk reads it as a 3-D normalise; the
	// immediate at `10326c83` says otherwise, and the difference is the whole answer for a target
	// above or below the shooter.
	//
	// The dot's third term survives in the listing (`FLD [ESP+0x20]  FMUL [ESP+0x8]`) and is
	// `eyeDir.z * 0`, so it contributes nothing — retail computes it anyway and so does this.
	//
	// `GetAbsOrigin()` is slot 217; the generated virtual is a declared stub that answers null, and
	// `FElysiumEntity::Origin` is the word it would hand back — family **BaseHelpers** spells it
	// the same way at its own slot-217 read.
	//
	// **SEAM, named**: `EyeDirection2D()` (slot 372) forwards to `HeadDirection2D()` (slot 370),
	// which is still a GENERATED STUB answering the zero vector, so every call through this body
	// refuses today. The refusal is the stub's, not the cone's — `AimConeAdmits` above carries the
	// recovered rule and is what the suite drives.
	return AimConeAdmits(Origin, TargetCm, EyeDirection2D());
}

bool FElysiumNpcBase::FInAimCone(FElysiumEntity* AimTarget)
{
	// `0x10326ae0`, 181 bytes. Past the scope trace it is three dispatches and nothing else:
	//
	//     eye  = EyePosition()                         (this, slot 193, vtable +0x304)
	//     aim  = target->BodyTarget(eye, true, false)  (slot 197, vtable +0x314)
	//     return FInAimCone(aim)                       (this, slot 364, vtable +0x5b0)
	//
	// The two literal booleans are pushed at `10326b0c`/`10326b0e` before the eye call, which is
	// why the decompiler attached them to the wrong callee. `BodyTarget`'s answer, not the target's
	// origin, is what the cone is measured to — a prone or crouched body aims at a different point.
	if (AimTarget == nullptr)
	{
		// Retail dereferences `*param_1` for the vtable and would fault; every call site in the
		// closure has already null-checked. Refusing is this port's own guard and changes no
		// reachable arm.
		return false;
	}
	const FVector EyeCm = EyePosition();
	// Slot 197 is declared on `FElysiumNpc` only — this runtime stands the Troika line's
	// `BodyTarget` (`0x102789c0`) and no `CBaseEntity` tier below it. For a non-NPC target the base
	// `CBaseEntity::BodyTarget` is `WorldSpaceCenter()`, i.e. `GetAbsOrigin() + (mins+maxs)/2`, and
	// family Motor's `RetailCollisionExtents` — the only source for those extents — is a seam that
	// answers a zero box, so the centre reduces to the origin. That is a consequence of the extents
	// seam, not a value invented here.
	FElysiumNpc* TargetNpc = AimTarget->AsNpc();
	const FVector AimPointCm = TargetNpc != nullptr
		? TargetNpc->BodyTarget(EyeCm, true, false) : AimTarget->Origin;
	return FInAimCone(AimPointCm);
}

FElysiumEntity* FElysiumNpcBase::GetEnemy() const
{
	// `0x101a67e0`, 45 bytes: resolve `m_hEnemy` (`+0x5ce0`, `FElysiumNpcMemory::Enemy`) through
	// the global entity-handle table and answer the pointer, or 0 when the serial no longer
	// matches. 126 real callers — the most-called body of this family — and the port's own
	// `World->Resolve` is that table's exact contract: index plus serial, null on a stale handle.
	if (World == nullptr)
	{
		return nullptr;
	}
	return World->Resolve(BaseMemory.Enemy);
}

FElysiumEntity* FElysiumNpcBase::GetEnemyBaseLine() const
{
	// `0x10027020`. The corpus prints a DAMAGED banner for the C; the listing is two instructions,
	// `MOV EAX,[ECX]` / `JMP [EAX+0x29c]` — slot 167, the const overload, tail-called. No work.
	return GetEnemy();
}

void* FElysiumNpcBase::GetEnemies()
{
	// `0x10273e10`, 22 bytes:
	//
	//     return (m_iSquadDisconnected < 1) ? m_pMemory (+0x5d88) : DAT_109203f0;
	//
	// 43 real callers. The test is `< 1`, not `== 0`: `DisconnectFromSquad` increments and
	// `ReconnectToSquad` decrements, so a negative count reads as connected — and it is
	// `m_iSquadDisconnected` (`+0x5bb0`, `FElysiumNpcScheduleHost::SquadDisconnected`) that decides,
	// never `m_pSquad`.
	//
	// The shape map's own note on `+0x5d88` states the redirection this answers: "squad redirection
	// replaces the ownership, not the object". Until a squad store lands, `EnemyMemory` IS the
	// connected answer.
	if (BaseScheduleHost.SquadDisconnected < 1)
	{
		return &EnemyMemory;
	}
	return &GDisconnectedEnemies;
}

void FElysiumNpcBase::RemoveMemory()
{
	// `0x10273e40`, 39 bytes:
	//
	//     if (m_pSquad (+0x5da4) == NULL && m_pMemory (+0x5d88) != NULL) { dtor(m_pMemory); free(); }
	//
	// The gate is the SQUAD pointer, not a reference count: an NPC in a squad does not own the
	// store it was handed and must not free it. `+0x5da4` is ABSENT from the shape map — this
	// substrate stands no squad object — so `ConnectedSquad()` answers null on every NPC and the
	// free always runs, which is retail's answer for a squadless NPC.
	//
	// The port's `EnemyMemory` is a member by value, not a heap allocation, so "free" is "empty".
	// `FElysiumNpcEnemyMemory` has no `Clear`; `Refresh` with no world is not the same thing, so
	// the release is spelled as a default-assignment, which is exactly what the destructor left.
	if (ConnectedSquad() != nullptr)
	{
		return;
	}
	EnemyMemory = FElysiumNpcEnemyMemory();
}

bool FElysiumNpcBase::IsHintViewable(const FHintWords& Hint)
{
	// `0x102d1320`, 24 bytes:
	//
	//     if (m_iDisabled != 0) return m_iDisabled & 0xffffff00;
	//     return m_nHintType == 0xd;
	//
	// The first return is an `int` whose LOW byte — the `bool` the caller reads in AL — is always
	// zero, whatever `m_iDisabled` holds. So a disabled hint is never viewable and the masked word
	// is a compiler artefact of returning the same register, not a value anyone sees.
	if (Hint.Disabled != 0)
	{
		return false;
	}
	return Hint.HintType == GViewableHintType;
}

bool FElysiumNpcBase::NavigatorHasNodeGraph() const
{
	// `0x102ee6a0`: `nav->m_pNetwork (+0x30) != NULL && nav->m_pNetwork->m_pNodes (+0x24) != NULL`.
	// **SEAM**: this substrate has no AI network and no node list.
	return false;
}

const FElysiumEntity* FElysiumNpcBase::SquadFocus() const
{
	// `GetSquadFocus` (`0x103166b0`): `curtime < squad+0x74` AND `squad+0x70` resolves. **SEAM**:
	// `ConnectedSquad()` is null on every NPC here.
	return nullptr;
}

void FElysiumNpcBase::SetSquadFocus(const FElysiumEntity* Focus)
{
	// `SetSquadFocus` (`0x10316660`): `squad+0x70 = handle or -1` and
	// `squad+0x74 = curtime + _DAT_10463584` (**15.0**, read out of `.rdata`). **SEAM**: counted.
	(void)Focus;
	(void)GSquadFocusLifetimeSeconds;
	++SquadFocusWrites;
}

uint32 FElysiumNpcBase::DoorBlockFlags(const FElysiumEntity& Door)
{
	// `door+0x644`, tested `& 0x10` and `& 0x40` by `0x100f0ec0`, whose whole body is
	// `(*(uint*)(this + 0x644) & mask) == mask`. **SEAM**: no port word; 0 is "a plain door", which
	// takes the long-retry arm.
	(void)Door;
	return 0u;
}

void FElysiumNpcBase::SetDoorNextTryTime(FElysiumEntity& Door, double At)
{
	// `0x100f0e30`: `if (at >= door+0x640) door+0x640 = at;` — a MAX write, spelled by retail as an
	// if/else whose refusal arm assigns the field to itself. **SEAM**: counted.
	(void)Door;
	(void)At;
	++DoorNextTryWrites;
}

void FElysiumNpcBase::OnDoorBlocked(FElysiumEntity& Door)
{
	// `0x1027de00`, 336 bytes. **Not `SetEnemy`.** Its two callers are `0x10298840` (the
	// alternate-AI door transaction, when `0x100eec70` refuses the NPC/door pair) and `0x1027dfb0`
	// (the hit-by-door handler, when the door that hit is the door being opened), and it writes
	// `m_hBlockedDoor` (`+0x5d28`). `npc-kernel/signatures.md` slot 532 names the reason word.
	//
	//   1. `if (!IsAlive() || door == NULL) return;`            (slot 158, vtable +0x278)
	if (!IsAlive())
	{
		return;
	}
	//   2. `if (door == resolve(m_hOpeningDoor)) slot532(2);`   — the door I was opening is the one
	//      blocking me, so end the alternate-AI door wait with reason 2.
	const FElysiumEntity* Opening = World != nullptr ? World->Resolve(OpeningDoor) : nullptr;
	if (Opening == &Door)
	{
		Slot532(GSlot532ReasonDoorBlocked);
	}
	//   3. `if (!(door+0x644 & 0x10))` — the retry block, skipped entirely on that flag.
	const uint32 DoorFlags = DoorBlockFlags(Door);
	if ((DoorFlags & GDoorFlagNoBlockRetry) != GDoorFlagNoBlockRetry)
	{
		//   4. `& 0x40` picks 5.0 s (`_DAT_10454110`) over 20.0 s (`_DAT_1044eb0c`), for BOTH the
		//      navigator mark and the door's own stamp. The navigator mark is gated on
		//      `0x102ee6a0` and the stamp is not.
		const bool bShort = (DoorFlags & GDoorFlagShortBlockRetry) == GDoorFlagShortBlockRetry;
		const float Seconds = bShort ? GDoorRetryShortSeconds : GDoorRetryLongSeconds;
		if (NavigatorHasNodeGraph())
		{
			++NavigatorUnreachableMarks;
		}
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		SetDoorNextTryTime(Door, Now + static_cast<double>(Seconds));
	}
	//   5. `if (m_iSquadDisconnected < 1 && m_pSquad) { if (GetSquadFocus() != door)
	//      SetSquadFocus(door); }` — keep the squad looking at the obstruction, but only change it
	//      when it is not already the door. The squad object is a seam, so `ConnectedSquad()` is
	//      null and the whole arm is skipped, which is retail's answer for a squadless NPC.
	if (BaseScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
	{
		if (SquadFocus() != &Door)
		{
			SetSquadFocus(&Door);
		}
	}
	//   6. `m_hBlockedDoor = door->GetRefEHandle();`
	BlockedDoor = Door.Handle;
	//   7. `pTroika = this+0x98; if (pTroika && (pTroika->m_eAlternateAI == 1 || == 2)) {
	//      m_eAlternateAI = 3; m_flAlternateAIExpireTimer = curtime + 1.0; }`
	//
	//      `+0x98` is `m_pBaseNPCTroika` (`npc-kernel/layout.md`), CBaseEntity's SELF-downcast
	//      cache — non-null exactly when this entity IS a `CAI_BaseNPCTroika`: `AsNpc()`. A
	//      base-only NPC takes the null arm and writes nothing.
	FElysiumNpc* const Troika = AsNpc();
	if (Troika != nullptr && (Troika->AlternateAi == GAlternateAiModeFacing
		|| Troika->AlternateAi == GAlternateAiModeOpening))
	{
		Troika->AlternateAi = GAlternateAiModeBlocked;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		Troika->AlternateAiExpireTime = Now + static_cast<double>(GDoorAlternateAiWindowSeconds);
	}
}

void FElysiumNpcBase::UpdateEnemyWentOccluded(const FElysiumEntity* Enemy, bool bHaveLos)
{
	// `0x10270180`, 205 bytes and three arms, none of which falls through to another:
	//
	//   A. `enemy == NULL` — copy the world-origin global `DAT_1070d1b0..b8` into
	//      `m_vecEnemyWentOccluded` (+0x5bc8) and store the LOS BYTE ITSELF into
	//      `m_bEnemyWentOccluded` (+0x5bc5). Retail writes `param_2` here, not 0 — a null enemy
	//      with LOS true leaves the flag SET, which is the one arm that can raise it without a
	//      distance test.
	//   B. `!bHaveLos` — snapshot the enemy's CURRENT `GetAbsOrigin()` (slot 217, vtable +0x364)
	//      into `+0x5bc8` and CLEAR the flag. This is "I have just lost sight; remember where it
	//      was".
	//   C. `bHaveLos` and the flag is already clear — if the enemy has moved more than
	//      `_DAT_104563b0` (**4096.0**, a SQUARED distance, so 64 Source units) from that snapshot,
	//      SET the flag. Nothing else is written, and a flag already set is never re-tested.
	//
	// Arm A's `DAT_1070d1b0` is `vec3_origin` — the engine's shared zero vector, which `0x10270180`
	// reads as three dwords rather than constructing.
	if (Enemy == nullptr)
	{
		BaseMemory.EnemyWentOccludedPosition = FVector::ZeroVector;
		BaseMemory.bEnemyWentOccluded = bHaveLos;
		return;
	}
	if (!bHaveLos)
	{
		BaseMemory.EnemyWentOccludedPosition = Enemy->Origin;
		BaseMemory.bEnemyWentOccluded = false;
		return;
	}
	if (BaseMemory.bEnemyWentOccluded)
	{
		return;
	}
	const float LimitCm = GEnemyWentOccludedDistanceSqUnits * ElysiumMove::U * ElysiumMove::U;
	if (FVector::DistSquared(BaseMemory.EnemyWentOccludedPosition, Enemy->Origin) > LimitCm)
	{
		BaseMemory.bEnemyWentOccluded = true;
	}
}

void FElysiumNpcBase::SetDistLook(float LookDistCm)
{
	// `0x1026a2a0`, 16 bytes: `*(m_pSenses (+0x5cdc) + 0x10) = param_1;`
	//
	// `CAI_Senses+0x10` is `m_LookDist` — `vtmb_fields CAI_Senses` declares exactly two fields and
	// this is one of them. 29c filed the inner word as unrecovered because it read the offset
	// rather than the class's own field list; there is no seam here at all, only a member that had
	// not been declared. The object is reached through `SensesObject()`, the `m_pSenses` word; a
	// base-only NPC has none yet (fold 9), and the write lands nowhere.
	if (FElysiumNpcSenses* const SensesPtr = SensesObject())
	{
		SensesPtr->LookDistCm = LookDistCm;
	}
}
