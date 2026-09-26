#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

// Story 29c-1, family **Senses** — the nine Troika-line slot bodies (86, 167, 168, 196, 364, 365,
// 470, 541, 543), the six species overrides of the perception virtuals, the two witness-record
// setters, the occlusion edge and the door-blocked notice. The declarations and this family's two
// standing facts are `Substrate/ElysiumNpcSensesBodies.inl`; `FElysiumNpcSenses`'s own two rows
// (`0x103105d0`'s reader and `0x1029c970`) live on that struct, in
// `Substrate/ElysiumNpcSenses.cpp`. The walked prose is `docs/vtmb/npc-ai/senses.md`.
//
// Every constant below was read out of retail `vampire.dll`'s `.rdata` at the cell the decompiled C
// names (image base `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`), the same
// method that pinned `_DAT_104454c4` in `docs/vtmb/npc-ai/conditions-and-states.md`. Where a cell
// is a DOUBLE the listing says so with `FCOMP double ptr`, and the comment repeats it — reading
// `_DAT_1049e0c8` as a float gives 2.18e-25 and a cone that admits everything.

namespace
{
	// --- Retail `.rdata`, one line per constant ---------------------------------------------------

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

	// `m_bfNPCStateFlags & 0x40`, the bit slot 168 requires before it falls back to
	// `m_hLastEnemy`. `FElysiumNpcFlags::NpcStateFlagsForRetailState` gives it to retail states
	// `0xb` (HUNT) and `0xe` only, and neither has a port state — see the slot's own comment.
	constexpr uint8 GStateFlagLastEnemyFallback = 0x40;

	// Slot 532's reason word for "the door blocked me". `npc-kernel/signatures.md` slot 532 lists
	// all four: 1 from `0x1027dd10` (fully open), 2 from here, 4 from `0x1027dfb0`, 8 from
	// `OnScheduleChange`.
	constexpr int32 GSlot532ReasonDoorBlocked = 2;

	// The two `m_eAlternateAI` modes `OnDoorBlocked` promotes to 3.
	constexpr int32 GAlternateAiModeFacing = 1;
	constexpr int32 GAlternateAiModeOpening = 2;
	constexpr int32 GAlternateAiModeBlocked = 3;

	constexpr uint32 GSecureStoreXorWrite = 0x0ae8746fu;

	// `0x1042fde0`, the hash `0x1028ea60` runs the level through before scrambling it.
	uint32 SecureHash(uint32 Value)
	{
		const uint32 Folded = Value ^ NpcKernelSensesShared::GSecureHashXor;
		return Folded
			^ (((((Folded & NpcKernelSensesShared::GSecureHashMaskA) ^ NpcKernelSensesShared::GSecureHashXorA) + NpcKernelSensesShared::GSecureHashAddA)
				^ NpcKernelSensesShared::GSecureHashXorB) & NpcKernelSensesShared::GSecureHashMaskB);
	}

}

// =================================================================================================
// Slot 196 — `EarPosition`, `0x100b4c00`
// =================================================================================================

FVector FElysiumNpc::EarPosition()
{
	// `0x100b4c00`, 20 bytes: `(**(code**)(*this + 0x304))(out); return out;` — a tail call to slot
	// 193, `EyePosition`, for the side effect of filling the out-vector, and the same pointer back.
	// The ear IS the eye on every class in the family: 82 classes fill slot 196 and every one of
	// them with this body.
	return EyePosition();
}

// =================================================================================================
// Slots 364 / 365 — `FInAimCone`, `0x10326bd0` and `0x10326ae0`
// =================================================================================================

bool FElysiumNpc::AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm,
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

bool FElysiumNpc::FInAimCone(const FVector& TargetCm)
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

bool FElysiumNpc::FInAimCone(FElysiumEntity* AimTarget)
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

// =================================================================================================
// Slots 167 / 168 — `GetEnemy`, `0x101a67e0` and `0x102b5360`
// =================================================================================================

FElysiumEntity* FElysiumNpc::GetEnemy() const
{
	// `0x101a67e0`, 45 bytes: resolve `m_hEnemy` (`+0x5ce0`, `FElysiumNpcMemory::Enemy`) through
	// the global entity-handle table and answer the pointer, or 0 when the serial no longer
	// matches. 126 real callers — the most-called body of this family — and the port's own
	// `World->Resolve` is that table's exact contract: index plus serial, null on a stale handle.
	if (World == nullptr)
	{
		return nullptr;
	}
	return World->Resolve(Senses.Memory.Enemy);
}

FElysiumEntity* FElysiumNpc::GetEnemy()
{
	// `0x102b5360`, the TROIKA line's mutable overload, 75 bytes:
	//
	//     e = slot167();                                   // vtable +0x29c
	//     if (e == NULL && (m_bfNPCStateFlags & 0x40))     // +0x5b64 bit 6
	//         return resolve(m_hLastEnemy);                // +0x1a94, or 0
	//     return e;
	//
	// **The fallback is unreachable in this runtime, and that is a recovered fact rather than a
	// gap.** `m_bfNPCStateFlags` is a pure function of `m_NPCState` (`0x1026e3e0`, the table in
	// `FElysiumNpcFlags::NpcStateFlagsForRetailState`), and bit 6 is set for retail states `0xb`
	// (HUNT) and `0xe` only. Neither has an `EElysiumNpcState`, so `NpcStateFlags()` cannot
	// answer 0x40 today. The arm is written anyway, reading the real word, so the day a hunt state
	// lands it is already correct.
	//
	// The BASE line (`0x10027020`, 18 further classes) is `GetEnemyBaseLine` below: a bare tail
	// jump to slot 167 with no last-enemy fallback at all.
	FElysiumEntity* Live = static_cast<const FElysiumNpc*>(this)->GetEnemy();
	if (Live == nullptr && (NpcStateFlags() & GStateFlagLastEnemyFallback) != 0)
	{
		return World != nullptr ? World->Resolve(Senses.Memory.LastEnemy) : nullptr;
	}
	return Live;
}

FElysiumEntity* FElysiumNpc::GetEnemyBaseLine() const
{
	// `0x10027020`. The corpus prints a DAMAGED banner for the C; the listing is two instructions,
	// `MOV EAX,[ECX]` / `JMP [EAX+0x29c]` — slot 167, the const overload, tail-called. No work.
	return GetEnemy();
}

// =================================================================================================
// Slots 541 / 543 — the `CAI_Enemies` store, `0x10273e10` and `0x10273e40`
// =================================================================================================

namespace
{
	// `DAT_109203f0` — the ONE shared `CAI_Enemies` every squad-disconnected NPC is handed instead
	// of its own. A retail GLOBAL, ported as one: a single store for the whole level, not one per
	// NPC. Nothing in this runtime writes into it; it exists so `GetEnemies()` answers a real
	// object on the disconnected arm rather than null, which is what retail answers.
	FElysiumNpcEnemyMemory GDisconnectedEnemies;
}

void* FElysiumNpc::GetEnemies()
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
	if (ScheduleHost.SquadDisconnected < 1)
	{
		return &EnemyMemory;
	}
	return &GDisconnectedEnemies;
}

void FElysiumNpc::RemoveMemory()
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

// =================================================================================================
// Slot 86 — `ShouldTransmit`, `0x102c0420`
// =================================================================================================

bool FElysiumNpc::ShouldTransmit(int32 Param1, void* Edict, void* Info, int32 Param4, int32 Param5)
{
	// `0x102c0420`, 54 bytes:
	//
	//     if (IsInDialog(this)) return true;                       // 0x102c1170
	//     return CBaseCombatCharacter::ShouldTransmit(...);        // the base, all five arguments
	//
	// `0x102c1170` is NAMED in the corpus — `IsInDialog` — and is four tests in order:
	// `+0x64c0` set, `+0x64ec` set, a live handle at `+0xfe8`, a live handle at `+0x6554`. So the
	// gate is "this NPC is mid-conversation", and its effect is to FORCE the NPC onto the network
	// whatever the base body would have said. It is not a hidden-from-network test, which is how
	// 29c's walk reads it, and the polarity matters: reading it the other way would cull the actor
	// the player is talking to.
	//
	// **SEAM**: this runtime has no networking and no `edict_t`. `FElysiumNpcDialogue`'s live
	// session is the port's answer for the four dialog words, and the base
	// `CBaseCombatCharacter::ShouldTransmit` — PVS plus the always-transmit flags — has no port
	// body either, so the fall-through answers TRUE: everything is transmitted in a single-player
	// substrate with no PVS culling, which is the arm that can never hide a live actor.
	(void)Param1;
	(void)Edict;
	(void)Info;
	(void)Param4;
	(void)Param5;
	if (HasLiveDialogPartner())
	{
		return true;
	}
	return true;
}

// =================================================================================================
// Slot 470 — `OnListened`, `0x102b39e0`
// =================================================================================================

void FElysiumNpc::OnListened()
{
	// `0x102b39e0`, 740 bytes and the largest body of this family. Structure, in retail's order:
	//
	//   1. chain `CAI_BaseNPC::OnListened` (`0x1026a5e0`) — clear `m_HeardConditions` (+0x5ca8),
	//      walk the sense list mapping each raw `CSound` type to its condition (1 -> 0x6d COMBAT,
	//      2 -> 0x6e WORLD, 4 -> 0x6f PLAYER, 8 -> 0x6a DANGER, 0x10 -> 0x70 BULLET_IMPACT,
	//      0x100 -> 0x6b, 0x200 -> 0x6c, 0x400 -> 0x71 PHYSICS_DANGER, 0x800 -> 0x72 FLINCH), queue
	//      it on `m_DelayedSoundConditionList` with slot 471's `GetReactionDelay()` (or
	//      `RandomFloat(0, 0.5)` for FLINCH), promote the expired ones, and fire `OnHearWorld`,
	//      `OnHearPlayer` and `OnHearCombat`.
	//   2. six category snapshots, each gated on its `m_HeardConditions` bit and each writing the
	//      record `CAI_Senses::GetClosestSound` answers for that raw type.
	//   3. the FLINCH snapshot, gated the same way through `0x102c66b0` on bit 0x72.
	//   4. `HEAR_COMBAT` and `HEAR_BULLET_IMPACT` — read off `m_Conditions` (+0x5c5c) this time,
	//      not `m_HeardConditions` — each feeding its record's resolved OWNER into `0x1028e8b0`
	//      with strength `1.0`, which is the stealth-vision override extension.
	//
	// **This runtime folds step 1 and `CAI_Senses::Listen` together into
	// `FElysiumNpcSenses::TickHearing`**, because the port consumes the game-sound bus directly and
	// has no `CSound` list to build first. `TickHearing` therefore also performs steps 2–4 inline,
	// and it is what the sense pass runs. This body is the recovered slot, written over the same
	// substrate so the two agree: calling it after a `TickHearing` re-runs steps 2–4 and is
	// idempotent (the same closest sound, and `ExtendVisionOverride` is a `Max`). It is NOT wired
	// into the sense pass, because a second producer of the same writes is what this port does not
	// do; the day `TickHearing` is split into Listen and OnListened, this is the OnListened half.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// Steps 2 and 3, in retail's own order — which is NOT the priority order `CommitBestSound`
	// ranks by, and not the arm order the investigate sweep walks. Each row is
	// (heard bit, raw type, record).
	struct FSnapshot
	{
		EElysiumNpcCond Heard;
		uint32 TypeMask;
		FElysiumGameSoundEvent* Record;
	};
	FElysiumNpcMemory& Memory = Senses.Memory;
	const FSnapshot Snapshots[] = {
		{ EElysiumNpcCond::HearWorld,         ElysiumGameSounds::World,        &Memory.LastSoundWorld },
		{ EElysiumNpcCond::HearPhysicsDanger, ElysiumGameSounds::PhysicsDanger,
			&Memory.LastSoundPhysicsDanger },
		{ EElysiumNpcCond::HearDanger,        ElysiumGameSounds::Danger,       &Memory.LastSoundDanger },
		{ EElysiumNpcCond::HearPlayer,        ElysiumGameSounds::Player,       &Memory.LastSoundPlayer },
		{ EElysiumNpcCond::HearBulletImpact,  ElysiumGameSounds::BulletImpact,
			&Memory.LastSoundBulletImpact },
		{ EElysiumNpcCond::HearCombat,        ElysiumGameSounds::Combat,       &Memory.LastSoundCombat },
		{ EElysiumNpcCond::HearFlinch,        ElysiumGameSounds::Flinch,       &Memory.LastSoundFlinch },
	};
	for (const FSnapshot& Row : Snapshots)
	{
		if (!Senses.HeardConditions.Has(Row.Heard))
		{
			continue;
		}
		if (const FElysiumGameSoundEvent* Closest = Senses.ClosestSound(*this, Row.TypeMask))
		{
			*Row.Record = *Closest;
		}
	}

	// Step 4. The gate is `m_Conditions` (+0x5c5c), the PROMOTED set, which is why a sound still
	// inside its reaction delay snapshots its record above but does not extend the override here.
	// `thunk_FUN_100290c0(PTR_DAT_10566458, &record)` is the handle resolve, and `0x1028e8b0` is
	// `FElysiumNpcSenses::ExtendVisionOverride` — already ported, with its three-arm owner test.
	if (Cognition.Conditions.Has(EElysiumNpcCond::HearCombat))
	{
		Senses.ExtendVisionOverride(*this, Memory.LastSoundCombat.Source, Now, 1.0);
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::HearBulletImpact))
	{
		Senses.ExtendVisionOverride(*this, Memory.LastSoundBulletImpact.Source, Now, 1.0);
	}
}

// =================================================================================================
// `CAI_Hint#163` — `IsViewable`, `0x102d1320`
// =================================================================================================

bool FElysiumNpc::IsHintViewable(const FHintWords& Hint)
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

// =================================================================================================
// The `CNPC_VWerewolf` / `CNPC_VYukie` stealth-gate trio — `0x103cb810`, `0x103ddaa0`, `0x103ddaf0`
// =================================================================================================

bool FElysiumNpc::SpeciesStealthSenseGate(const FElysiumEntity* Candidate) const
{
	// The three bodies open with the same three tests, in this order:
	//
	//     if (candidate == NULL)                           -> fail
	//     if (DAT_10924fba != 0)                           -> fail   (npc_ignore_senses)
	//     if (DAT_10924fb9 != 0 && candidate->m_pPlayer)   -> fail   (npc_ignore_player)
	//
	// Both bytes are the ConCommand-toggled debug globals `ElysiumNpcSense::IgnoreSenses` and
	// `IgnorePlayer` already stand over. Everything past this point differs per body.
	if (Candidate == nullptr)
	{
		return false;
	}
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	const bool bCandidateIsPlayer = World != nullptr && Candidate->Handle == World->PlayerHandle();
	if (ElysiumNpcSense::IgnorePlayer() && bCandidateIsPlayer)
	{
		return false;
	}
	return true;
}

// =================================================================================================
// The two witness-record setters — `0x1028ea60` and `0x1028eb30`
// =================================================================================================

uint32 FElysiumNpc::EncodeWitnessedLevel(uint32 Level)
{
	// `0x1028ea60`'s scramble over `0x1042fde0`'s hash:
	//     h = hash(level);
	//     stored = (((h & 0x068d8635) ^ 0x0ae8746f) + 0x0ffa91d8) & 0x197279ca ^ h ^ 0xa641cacd;
	const uint32 Hashed = SecureHash(Level);
	return ((((Hashed & NpcKernelSensesShared::GSecureStoreMask) ^ GSecureStoreXorWrite) + NpcKernelSensesShared::GSecureStoreAdd)
		& NpcKernelSensesShared::GSecureStoreMask2) ^ Hashed ^ NpcKernelSensesShared::GSecureStoreXorTail;
}

void FElysiumNpc::RecordCriminalWitness(int32 Level, const FVector& AtCm,
	const FElysiumEntity* Offender)
{
	// `0x1028ea60`, 154 bytes, in the listing's own order:
	//
	//     +0x6360 = <uninitialised stack byte>                       (1028ea72 MOV DL,[ESP+0x8])
	//     +0x6364 = scramble(hash(level))                            (the CSecureType payload)
	//     +0x6361 = <uninitialised stack byte>                       (1028eaa9 MOV CL,[ESP+0x5])
	//     +0x6380..+0x6388 = position
	//     +0x638c = offender->GetRefEHandle(), or 0xffffffff
	//
	// **The two bytes are a retail defect.** `SUB ESP,0x8` allocates them and nothing writes them
	// before they are read; `[ESP+0x8]` and `[ESP+0x5]` are inside that allocation. An
	// indeterminate read has no reproduction, so this runtime writes 0 and says so at the members.
	//
	// The level lands in the port's `FElysiumNpcWitnessChannel::Level` as a PLAIN number; the
	// scramble is kept as `EncodeWitnessedLevel` beside it so the recovered constants are in the
	// tree and the round trip is checkable. Nothing a shipped program runs can observe the
	// obfuscation.
	CriminalWitnessByte6360 = 0;
	CriminalWitnessByte6361 = 0;
	FElysiumNpcWitnessChannel& Channel =
		Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
	Channel.Level = Level;
	Channel.Location = AtCm;
	Channel.Offender = Offender != nullptr ? Offender->Handle : FElysiumEntityHandle();
}

void FElysiumNpc::RecordSupernaturalWitness(int32 Level, const FVector& AtCm,
	const FElysiumEntity* Offender, bool bFleeOnly)
{
	// `0x1028eb30`, 102 bytes:
	//
	//     +0x6368 = level                          (PLAIN — the supernatural level is not secured)
	//     +0x6374..+0x637c = position
	//     if (offender) { +0x6390 = handle; } else { +0x6390 = 0xffffffff; }
	//     +0x6394 = fleeOnly                       (on BOTH arms)
	//
	// The asymmetry with the criminal setter is real and is what the shape map already records:
	// `+0x635c` is a `custom` datamap type and `+0x6368` is a plain `int`.
	FElysiumNpcWitnessChannel& Channel =
		Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural);
	Channel.Level = Level;
	Channel.Location = AtCm;
	Channel.Offender = Offender != nullptr ? Offender->Handle : FElysiumEntityHandle();
	Witness.bSupernaturalFleeOnly = bFleeOnly;
}

// =================================================================================================
// `OnDoorBlocked` — `0x1027de00`, and its four seams
// =================================================================================================

bool FElysiumNpc::NavigatorHasNodeGraph() const
{
	// `0x102ee6a0`: `nav->m_pNetwork (+0x30) != NULL && nav->m_pNetwork->m_pNodes (+0x24) != NULL`.
	// **SEAM**: this substrate has no AI network and no node list.
	return false;
}

const FElysiumEntity* FElysiumNpc::SquadFocus() const
{
	// `GetSquadFocus` (`0x103166b0`): `curtime < squad+0x74` AND `squad+0x70` resolves. **SEAM**:
	// `ConnectedSquad()` is null on every NPC here.
	return nullptr;
}

void FElysiumNpc::SetSquadFocus(const FElysiumEntity* Focus)
{
	// `SetSquadFocus` (`0x10316660`): `squad+0x70 = handle or -1` and
	// `squad+0x74 = curtime + _DAT_10463584` (**15.0**, read out of `.rdata`). **SEAM**: counted.
	(void)Focus;
	(void)GSquadFocusLifetimeSeconds;
	++SquadFocusWrites;
}

uint32 FElysiumNpc::DoorBlockFlags(const FElysiumEntity& Door)
{
	// `door+0x644`, tested `& 0x10` and `& 0x40` by `0x100f0ec0`, whose whole body is
	// `(*(uint*)(this + 0x644) & mask) == mask`. **SEAM**: no port word; 0 is "a plain door", which
	// takes the long-retry arm.
	(void)Door;
	return 0u;
}

void FElysiumNpc::SetDoorNextTryTime(FElysiumEntity& Door, double At)
{
	// `0x100f0e30`: `if (at >= door+0x640) door+0x640 = at;` — a MAX write, spelled by retail as an
	// if/else whose refusal arm assigns the field to itself. **SEAM**: counted.
	(void)Door;
	(void)At;
	++DoorNextTryWrites;
}

void FElysiumNpc::OnDoorBlocked(FElysiumEntity& Door)
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
	if (ScheduleHost.SquadDisconnected < 1 && ConnectedSquad() != nullptr)
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
	//      cache — non-null exactly when this entity IS a `CAI_BaseNPCTroika`. This runtime stands
	//      one Troika leaf for every classname, so the test is always true here and the null arm is
	//      unreachable rather than unported.
	if (AlternateAi == GAlternateAiModeFacing || AlternateAi == GAlternateAiModeOpening)
	{
		AlternateAi = GAlternateAiModeBlocked;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		AlternateAiExpireTime = Now + static_cast<double>(GDoorAlternateAiWindowSeconds);
	}
}

// =================================================================================================
// The occlusion edge — `0x10270180`
// =================================================================================================

void FElysiumNpc::UpdateEnemyWentOccluded(const FElysiumEntity* Enemy, bool bHaveLos)
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
	FElysiumNpcMemory& Memory = Senses.Memory;
	if (Enemy == nullptr)
	{
		Memory.EnemyWentOccludedPosition = FVector::ZeroVector;
		Memory.bEnemyWentOccluded = bHaveLos;
		return;
	}
	if (!bHaveLos)
	{
		Memory.EnemyWentOccludedPosition = Enemy->Origin;
		Memory.bEnemyWentOccluded = false;
		return;
	}
	if (Memory.bEnemyWentOccluded)
	{
		return;
	}
	const float LimitCm = GEnemyWentOccludedDistanceSqUnits * ElysiumMove::U * ElysiumMove::U;
	if (FVector::DistSquared(Memory.EnemyWentOccludedPosition, Enemy->Origin) > LimitCm)
	{
		Memory.bEnemyWentOccluded = true;
	}
}

// =================================================================================================
// `SetDistLook` — `0x1026a2a0`
// =================================================================================================

void FElysiumNpc::SetDistLook(float LookDistCm)
{
	// `0x1026a2a0`, 16 bytes: `*(m_pSenses (+0x5cdc) + 0x10) = param_1;`
	//
	// `CAI_Senses+0x10` is `m_LookDist` — `vtmb_fields CAI_Senses` declares exactly two fields and
	// this is one of them. 29c filed the inner word as unrecovered because it read the offset
	// rather than the class's own field list; there is no seam here at all, only a member that had
	// not been declared.
	Senses.LookDistCm = LookDistCm;
}
