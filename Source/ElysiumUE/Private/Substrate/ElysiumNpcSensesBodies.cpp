#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
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

	// `m_bfNPCStateFlags & 0x40`, the bit slot 168 requires before it falls back to
	// `m_hLastEnemy`. `FElysiumNpcFlags::NpcStateFlagsForRetailState` gives it to retail states
	// `0xb` (HUNT) and `0xe` only, and neither has a port state — see the slot's own comment.
	constexpr uint8 GStateFlagLastEnemyFallback = 0x40;

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

// =================================================================================================
// Slots 364 / 365 — `FInAimCone`, `0x10326bd0` and `0x10326ae0`
// =================================================================================================

// =================================================================================================
// Slots 167 / 168 — `GetEnemy`, `0x101a67e0` and `0x102b5360`
// =================================================================================================

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
		return World != nullptr ? World->Resolve(BaseMemory.LastEnemy) : nullptr;
	}
	return Live;
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
		if (!HeardConditions.Has(Row.Heard))
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

// =================================================================================================
// The occlusion edge — `0x10270180`
// =================================================================================================

// =================================================================================================
// `SetDistLook` — `0x1026a2a0`
// =================================================================================================

