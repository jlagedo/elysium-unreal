#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses10Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumWorldServices.h"

// Story 29d, family **Senses10** — the Troika-line bodies. The species line is in
// `ElysiumNpcSenses10_2.cpp`; the declarations and this family's standing facts are in
// `ElysiumNpcSenses10.inl`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. The three corrections this family's reading made to the checklist's
// one-line walks are marked **CORRECTION** at the arm they change.

namespace
{
	// Retail's `Disposition_t`.
	constexpr int32 GD_ER = 0;
	constexpr int32 GD_LI = 3;
	constexpr int32 GD_NU = 4;

	// `_DAT_10457f54` = **0.7**, the far-band fraction slot 594 compares against
	// (`docs/vtmb/computer-terminals.md`; `ElysiumNpcEntityChain2.cpp` carries the same cell).
	constexpr float GFarBandFraction = ElysiumNpcTunables::SevenTenths;

	// `_DAT_1044bef8` = **0.25**, the cowering/sleeping hearing scale in slot 467
	// (`docs/vtmb/computer-terminals.md` line 1186).
	constexpr float GCoweringHearingScale = ElysiumNpcTunables::Quarter;

	// `m_bfAINPCFlags & 0x20400` — `COWERING` (`0x400`) and `SLEEPING` (`0x20000`), the one gate that
	// lets slot 467's distance arm run at all (`102b3734`).
	constexpr uint32 GHearDistanceGateMask = 0x00020400u;

	// `_DAT_10452dc4` = **2.0**, `_DAT_104492a4` = **60.0**, `_DAT_104454d0` = **0.5**,
	// `_DAT_104454c0` = **1.0**, `_DAT_10449270` = **0.5**, `_DAT_10450568` = **360.0**,
	// `_DAT_1044eb0c` = **20.0**, `_DAT_104492dc` = **-1.0**.
	constexpr float GTwo = ElysiumNpcTunables::Two;
	constexpr float GGroundpointZLift = ElysiumNpcTunables::YawSpeedHumanoidCrouch;
	constexpr float GYawWrap = ElysiumNpcTunables::HeadAngleRunawayLimit;
	constexpr float GMingXiaoAimZBonus = ElysiumNpcTunables::Twenty;

}

// =================================================================================================
// Slot 404 / 405 — the two dispositions, read off the store the Troika body's tail reaches.
// =================================================================================================

int32 FElysiumNpc::IRelationTypeOf(const FElysiumEntity* Candidate) const
{
	// **This is now the one-line forward the `.inl` promised.** Slot 404 (`0x10299da0`) carries its
	// body as of family **Conditions10** — the two null arms, the INSANE forwarding, both boss arms
	// and the `CBaseCombatCharacter` tail — so this reads the slot instead of the store the tail
	// reaches, which is what every retail caller of `vtable +0x650` gets.
	return const_cast<FElysiumNpc*>(this)->IRelationType(const_cast<FElysiumEntity*>(Candidate));
}

namespace
{
	// `CBaseEntity::FVisible 0x100a6fa0` -- the body slot 201 ends at (`102b46b1`) once its own gates
	// have passed. `Probe` is `FVisible`'s fourth argument (`m_eEnemyOccludedCheck +0x5b98` at the
	// enemy call site, 0 at every other), and this is the one place it is read.
	//   1. `target->GetFlags() & 0x8000` (`FL_NOTARGET`) -> false (`100a7017`).
	//   2. the water gate on `+0x3e0`: looker not 3 and target 3, or looker 3 and target 0 -> false.
	//      The field is `FElysiumEntity::WaterLevel`; nothing in this runtime writes it yet, so both
	//      read 0 and the gate never closes.
	//   3. start = the looker's eye (slot 193), end = `ElysiumNpcSight::VisibleTargetOrigin` over the
	//      target's OBB (`RetailCollisionExtents`, SOURCE units, scaled here to the seam's cm).
	//      With no extents to read (the seam answers false) the end is the target's eye, probe 0's.
	//   4. `ElysiumNpcSight::Visible` -- the LINE under `CTraceFilterFVisible` and the verdict order.
	//   5. the blocker cell (`100a71ab`): on a block, `*ppBlocker = tr.m_pEnt` when the caller passed a
	//      cell -- `FVisible`'s THIRD argument (`CBaseEntity**`); the fourth is `Probe`. The generated
	//      slot takes that argument by value, so "a cell was passed" is its nullness and the cell is
	//      the NPC's own `LastFVisibleBlockerTarget` (`WriteFVisibleBlocker`). `CellOwner` is that
	//      NPC, null when no cell was passed. What stopped the ray is `ElysiumNpcSight::Visible`'s
	//      out-blocker over `TraceRetail`: the kept character, or Invalid for the static world (retail's
	//      `tr.m_pEnt` there is the world entity). The gates above write nothing, as retail's return
	//      before the trace.
	bool BaseFVisible(const FElysiumNpc& Looker, const FElysiumEntity& Target, int32 Mask, int32 Probe,
		FElysiumNpc* CellOwner = nullptr)
	{
		if (FElysiumNpcBase::HasNoTargetFlag(Target))                            // 100a7017
		{
			return false;
		}
		constexpr int32 WaterSubmerged = 3;
		if ((Looker.WaterLevel != WaterSubmerged && Target.WaterLevel == WaterSubmerged)
			|| (Looker.WaterLevel == WaterSubmerged && Target.WaterLevel == 0))
		{
			return false;
		}

		const IElysiumEmbodiment* Embodiment = Looker.World != nullptr ? Looker.World->Embodiment() : nullptr;
		if (Embodiment == nullptr)
		{
			return true;   // headless: no collision world reads as clear, as `QueryLineOfSight`'s default does
		}

		FVector TargetPointCm = Target.EyePosition();
		FVector MinsUnits = FVector::ZeroVector;
		FVector MaxsUnits = FVector::ZeroVector;
		if (FElysiumNpcBase::RetailCollisionExtents(Target, MinsUnits, MaxsUnits))
		{
			TargetPointCm = ElysiumNpcSight::VisibleTargetOrigin(Probe, Target.EyePosition(), Target.Origin,
				MinsUnits * ElysiumMove::U, MaxsUnits * ElysiumMove::U);
		}

		ElysiumNpcSight::FVisibleQuery Query;
		Query.EyeCm = Looker.EyePosition();
		Query.TargetCm = TargetPointCm;
		Query.Mask = Mask;
		Query.Looker = Looker.Handle;
		Query.Target = Target.Handle;
		Query.World = Looker.World;
		FElysiumEntityHandle Blocker = FElysiumEntityHandle::Invalid();
		if (ElysiumNpcSight::Visible(*Embodiment, Query, &Blocker))
		{
			return true;
		}
		if (CellOwner != nullptr)                                                  // 100a71ab
		{
			CellOwner->WriteFVisibleBlocker(Looker.World->Resolve(Blocker));
		}
		return false;
	}
}

// =================================================================================================
// Slot 201 `FVisible` — `CAI_BaseNPCTroika::FVisible` `0x102b4630`, 232 bytes.
// =================================================================================================

void FElysiumNpc::WriteFVisibleBlocker(const FElysiumEntity* SeenTarget)
{
	++FVisibleBlockerWrites;
	LastFVisibleBlockerTarget = SeenTarget != nullptr ? SeenTarget->Handle : FElysiumEntityHandle::Invalid();
}

bool FElysiumNpc::BaseEntityFVisible(const FElysiumEntity& SeenTarget, int32 Mask) const
{
	// The `FVisible` fourth argument is not on this declaration (`ElysiumNpcSenses10.inl`), so the
	// body proper is `BaseFVisible` above and this is its probe-0 form.
	return BaseFVisible(*this, SeenTarget, Mask, 0);
}

bool FElysiumNpc::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `0x102b4630`. The one-shot static init at `102b4630` resolves the `Dominate_BrainWipe`
	// discipline id into `DAT_10924074` through `0x101e1590` / `0x101e1870` and is guarded by bit 0
	// of `DAT_10923e2c`. This runtime keys a discipline by NAME, so the id resolution has no
	// counterpart and the refusal arm below reads the name directly; the once-flag is unobservable.

	// `102b4655`: a null target answers false and does NOT write the blocker. Retail's asymmetry.
	if (SeenTarget == nullptr)
	{
		return false;
	}
	// `102b4660`: `npc_ignore_senses` — blocker written, false.
	if (ElysiumNpcSense::IgnoreSenses())
	{
		if (Blocker != nullptr)
		{
			WriteFVisibleBlocker(SeenTarget);
		}
		return false;
	}
	// `102b466e`: `npc_ignore_player` AND the target carrying `+0xa8` — blocker written, false.
	if (ElysiumNpcSense::IgnorePlayer() && NpcKernelSenses10Shared::IsPlayerRecord(*this, SeenTarget))
	{
		if (Blocker != nullptr)
		{
			WriteFVisibleBlocker(SeenTarget);
		}
		return false;
	}
	// `102b4688`: slot 594 (`vtable +0x948`) with the caller's mask and BOTH trailing arguments
	// forced to `0` — so the Troika line never hands slot 594 a blocker cell.
	if (!Slot594(SeenTarget, Mask, nullptr, 0))
	{
		return false;
	}
	// `102b469c`: `0x1033d2f0(this, DAT_10924074)` — this NPC carrying the `Dominate_BrainWipe`
	// status is blind, whatever the trace would say.
	if (HasDisciplineStatus(TEXT("Dominate_BrainWipe")))
	{
		return false;
	}
	// `102b46b1`: the base `CBaseEntity::FVisible` does the trace itself, all four arguments passed
	// through (`102b46fa..102b4708`), the blocker cell with them.
	return BaseFVisible(*this, *SeenTarget, Mask, Arg4, Blocker != nullptr ? this : nullptr);
}

// =================================================================================================
// Slot 594 — `CAI_BaseNPCTroika::FUN_102b4760` `0x102b4760`, 668 bytes.
// =================================================================================================

float FElysiumNpc::SeekDistInspectionCm() const
{
	return Senses.Perception.VisionDistanceCm;   // +0x63b8 m_flSeekDistInspection
}

float FElysiumNpc::TargetStealthVisionScalar(const FElysiumEntity& SeenTarget) const
{
	// The target's slot 28 (`vtable +0x70`). Only the player carries a stealth surface here.
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	return Player != nullptr && Player->Handle == SeenTarget.Handle ? Player->Stealth.VisionScalar : 1.f;
}

bool FElysiumNpc::Slot594(FElysiumEntity* SeenTarget, int32 /*Mask*/, FElysiumEntity* Blocker,
	int32 /*Arg4*/)
{
	// `102b4770`: `m_bSeenInOuterBand (+0x6081) = 0` FIRST, before anything is read.
	Senses.Memory.bPlayerInOuterBand = false;
	if (SeenTarget == nullptr)
	{
		// Retail dereferences the target unguarded (`102b477d` `MOV EDI,[ESP+0x48]`, then
		// `CALL [EDX+0x304]`). CRASH GUARD, named: no caller in this runtime can reach it, because
		// slot 201 has already refused a null target.
		return false;
	}

	const FVector MyEyeCm = EyePosition();                  // `102b4777` slot 0x304
	const FVector TargetEyeCm = SeenTarget->EyePosition();      // `102b478a` slot 0x304

	// `102b4790`: the range block runs only when `(m_NPCState != 2 || m_bEnemyWentOccluded)` AND
	// `m_flStealthVisionOverrideTime <= curtime`. Retail state 2 is COMBAT.
	//
	// **CORRECTION.** The port's `FElysiumNpcSenses::IsVisible` read the TEN-FAILURE DEBOUNCE
	// (`bEnemyOccluded`) here. `102b479b` reads `[ESI+0x5bc5]`, which the shape map binds to
	// `m_bEnemyWentOccluded` — the occlusion EDGE that `0x10270180` writes, a different word.
	const bool bCombatBypass = GetMind().State() == EElysiumNpcState::Combat
		&& !BaseMemory.bEnemyWentOccluded;
	const bool bOverrideActive = NpcKernelSenses10Shared::NowOf(*this) < Senses.Memory.StealthVisionOverrideUntil;
	if (!bCombatBypass && !bOverrideActive)
	{
		const float DistanceCm = static_cast<float>(FVector::Dist(MyEyeCm, TargetEyeCm));
		// `102b4808`: the target's slot 28 times `m_flSeekDistInspection`.
		const float LimitCm = TargetStealthVisionScalar(*SeenTarget) * SeekDistInspectionCm();
		// `102b4819`: `dist <= limit` continues; `dist > limit` refuses.
		if (DistanceCm > LimitCm)
		{
			// `102b4820`: the blocker is written only when one was passed. Retail's own arm.
			if (Blocker != nullptr)
			{
				WriteFVisibleBlocker(SeenTarget);
			}
			return false;
		}
		// `102b483e`: beyond `_DAT_10457f54` (0.7) of that same product the FAR byte is set, and the
		// body carries on. This is the one writer of `+0x6081`.
		if (DistanceCm > GFarBandFraction * LimitCm)
		{
			Senses.Memory.bPlayerInOuterBand = true;
		}
	}

	// `102b485e`: the concealment test, only when the target IS a combat character (`+0x9c`).
	if (const FElysiumCombatCharacter* Character = SeenTarget->AsCombatCharacter())
	{
		if (!CanPerceiveConcealment(*Character))
		{
			// `102b487a`: a +-2 debug box when `m_debugOverlays < 0` and the target is the player.
			// Visual only; this runtime draws through the debug logging layer instead.
			// `102b492a`: the blocker write, again only when one was passed.
			if (Blocker != nullptr)
			{
				WriteFVisibleBlocker(SeenTarget);
			}
			return false;
		}
	}
	// `102b49f2`: the success exit draws a +-3 box and answers true.
	return true;
}

// =================================================================================================
// Slot 467 `QueryHearSound` — `CAI_BaseNPCTroika::QueryHearSound` `0x102b35b0`, 605 bytes.
// =================================================================================================

bool FElysiumNpc::SoundOwnerInDeafZone(const FElysiumEntity* Owner) const
{
	// `CStealthKillRules::InDeafZone(&DAT_1072c540, owner->m_pPlayer, this)`.
	if (World == nullptr || !NpcKernelSenses10Shared::IsPlayerRecord(*this, Owner))
	{
		return false;
	}
	UElysiumSessionSubsystem* State = World->GetGameState();
	UElysiumRulebookSubsystem* Rules = State != nullptr ? State->Rulebook() : nullptr;
	FElysiumPlayer* Player = World->FindPlayer();
	return Rules != nullptr && Player != nullptr
		&& Rules->StealthKillRules().InDeafZone(*Player, *this);
}

bool FElysiumNpc::QueryHearSound(void* SoundPtr)
{
	const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(SoundPtr);
	// `102b35bd`: a null `CSound*` answers false.
	if (Sound == nullptr)
	{
		return false;
	}
	// `102b35cd`: `npc_ignore_senses` (`DAT_10924fba`).
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	FElysiumEntity* Owner = World != nullptr && Sound->Source.IsSet()
		? World->Resolve(Sound->Source) : nullptr;
	// `102b35e2`: `npc_ignore_player` (`DAT_10924fb9`) AND the owner resolving to a player record.
	if (ElysiumNpcSense::IgnorePlayer() && Owner != nullptr && NpcKernelSenses10Shared::IsPlayerRecord(*this, Owner))
	{
		return false;
	}
	// `102b361b`: `(m_bfNPCFrenziedFlags & 0x800) == 0x800` and the owner resolving to
	// `m_hFriendPlayer`. THE ARM THE PORT WAS MISSING — `ElysiumNpcSenses.cpp` recorded it as "16c
	// work" and the seam answered not-a-friend. `FriendPlayer` is a real word (`+0x60ac`) and the
	// frenzied word is a real word; both ship at their retail defaults, so the arm is live.
	if (HasFrenzied(FElysiumNpcBase::FrenziedFriendPlayer) && FriendPlayer.IsSet()
		&& Sound->Source == FriendPlayer)
	{
		return false;
	}
	// `102b3655`: the owner IS me.
	if (Sound->Source == Handle)
	{
		return false;
	}
	// `102b368d`: a resolvable owner whose `+0x9c` combat character refuses `0x10146b20(cc, this)`.
	// Note retail's shape: an owner that exists but is NOT a combat character refuses OUTRIGHT
	// (`102b36bb` `MOV ECX,[EAX+0x9c]`, `102b36c3` `JZ` straight to the refusal), so a sound made by
	// a door whose owner handle is live is not heard. A sound with NO owner handle skips the whole
	// block, which is how world sounds get through.
	if (Owner != nullptr)
	{
		const FElysiumCombatCharacter* Character = Owner->AsCombatCharacter();
		if (Character == nullptr || !CanPerceiveConcealment(*Character))
		{
			return false;
		}
	}
	// `102b36e1`: sound type 4 (the player family) with the owner carrying a player record and the
	// stealth rules reporting a deaf zone.
	if (Sound->TypeMask == ElysiumGameSounds::Player && SoundOwnerInDeafZone(Owner))
	{
		return false;
	}
	// `102b3734`: the distance test runs ONLY for a cowering or sleeping body
	// (`m_bfAINPCFlags & 0x20400`). Everything else has already answered true by falling through —
	// the Listen-level cull (`CanHearSound` `0x1030f7b0`) is where the ordinary radius test lives,
	// and `FElysiumNpcSenses::TickHearing` is that cull.
	// `GHearDistanceGateMask` IS `COWERING | SLEEPING`; the port's flag vocabulary names both bits.
	static_assert(GHearDistanceGateMask
		== (static_cast<uint32>(EElysiumNpcFlag::COWERING)
			| static_cast<uint32>(EElysiumNpcFlag::SLEEPING)),
		"0x20400 is COWERING | SLEEPING");
	if (NpcFlags.Has(EElysiumNpcFlag::COWERING) || NpcFlags.Has(EElysiumNpcFlag::SLEEPING))
	{
		// `102b3753`: from the EAR position (slot 0x310), not the eye.
		const float DistanceCm = static_cast<float>(FVector::Dist(EarPosition(), Sound->Position));
		// `102b378f`: slot 0x770 `HearingSensitivity` times the sound's INT volume times
		// `_DAT_1044bef8` (0.25). The port's radius is already `volume * U`, so the scale applies to
		// it directly.
		float LimitCm = HearingSensitivity() * Sound->UnadjustedRadiusCm * GCoweringHearingScale;
		// `102b37dd`: `CBaseEntity::AdjustSoundDistForStealth(owner, sound, &limit)` — for a TYPE 4
		// sound whose owner carries a player record, subtract the owner's slot-30 stealth reduction
		// and clamp at zero. `FElysiumGameSoundEvent` carries that reduction already resolved.
		if (Owner != nullptr && Sound->TypeMask == ElysiumGameSounds::Player
			&& NpcKernelSenses10Shared::IsPlayerRecord(*this, Owner))
		{
			LimitCm = FMath::Max(0.f, LimitCm - Sound->StealthHearingReductionCm);
		}
		// `102b37e4`: `dist <= limit` hears; `dist > limit` refuses.
		if (DistanceCm > LimitCm)
		{
			return false;
		}
	}
	// `102b3801`.
	return true;
}

// =================================================================================================
// Slot 468 `QuerySeeEntity` — `CAI_BaseNPCTroika::QuerySeeEntity` `0x102b38b0`, 159 bytes.
// =================================================================================================

bool FElysiumNpc::QuerySeeEntity(FElysiumEntity* Candidate)
{
	// `102b38b0`: `npc_ignore_senses`, or `npc_ignore_player` with a non-null candidate carrying a
	// player record. Note the null check sits INSIDE the second arm only.
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	if (ElysiumNpcSense::IgnorePlayer() && Candidate != nullptr && NpcKernelSenses10Shared::IsPlayerRecord(*this, Candidate))
	{
		return false;
	}
	// `102b38f1`: the frenzy-friend veto. THE ARM THE PORT WAS MISSING.
	if (HasFrenzied(FElysiumNpcBase::FrenziedFriendPlayer) && Candidate != nullptr
		&& FriendPlayer.IsSet() && Candidate->Handle == FriendPlayer)
	{
		return false;
	}
	// `102b3930`: ANY candidate carrying a player record answers true, before any relationship test.
	// Retail dereferences the candidate here without a null check; CRASH GUARD, named.
	if (Candidate == nullptr)
	{
		return false;
	}
	if (NpcKernelSenses10Shared::IsPlayerRecord(*this, Candidate))
	{
		return true;
	}
	// `102b393c`: slot 404 `IRelationType`, true for D_HT and D_FR alone. Every other disposition —
	// including retail's `default:` — answers false.
	const int32 Relation = IRelationTypeOf(Candidate);
	return Relation == NpcKernelSenses10Shared::GD_HT || Relation == NpcKernelSenses10Shared::GD_FR;
}

// =================================================================================================
// Slot 469 `OnLooked` — `CAI_BaseNPCTroika::OnLooked` `0x102b39a0`, and the base body beneath it.
// =================================================================================================

void FElysiumNpc::OnLooked(int32)
{
	// `0x102b39a0`, 36 bytes, two statements: the base body FIRST, then one increment if
	// `COND_NEW_ENEMY` (`0x54`) still stands after it.
	BaseOnLooked();
	if (Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy))
	{
		++EnemySightings;   // m_iEnemySightings +0x60a8
	}
}

// =================================================================================================
// Slot 472 `OnSeeEntity` — `CAI_BaseNPCTroika::FUN_102b3e00` `0x102b3e00`, 485 bytes.
// =================================================================================================

void FElysiumNpc::OnSeeEntity(FElysiumEntity* Seen)
{
	FElysiumNpcMemory& Memory = Senses.Memory;
	const double Now = NpcKernelSenses10Shared::NowOf(*this);

	// `102b3e0c`: the outer gate, in retail's order — `m_bfAINPCFlags2 & 0x4000000` CLEAR
	// (`NO_UNKNOWN_VISION`), the far byte `+0x6081` SET, and `0x102b3270(entity, false)` true.
	// `0x102b3270` is `ShouldInvestigate` — `ElysiumNpcCond::ShouldInvestigate`.
	const bool bOuterGate = !NpcFlags.Has(EElysiumNpcFlag2::NO_UNKNOWN_VISION)
		&& Memory.bPlayerInOuterBand
		&& Seen != nullptr && ElysiumNpcCond::ShouldInvestigate(*this, *Seen, false);
	if (bOuterGate)
	{
		// `102b3e3c`: and only when the entity carries a player record AND `0x101671a0` admits it.
		// `0x101671a0` is the stealth-posture test — retail's "is this player sneaking".
		const FElysiumPlayer* Player = NpcKernelSenses10Shared::IsPlayerRecord(*this, Seen) && World != nullptr
			? World->FindPlayer() : nullptr;
		if (Player != nullptr && Player->IsInStealthPosture())
		{
			// `102b3e5a`: the ConVar object `DAT_10924a6c` slot 1 is touched, then
			// `0x10269a20(this, 1)` sets COND 1. Both are retail bookkeeping the port's condition
			// set carries; COND 1 is the schedule-changed family and is not this body's answer.
			// `102b3e6b`: `m_hBestSeeUnknown` ALREADY resolving to this entity RETURNS with nothing
			// written — not even the fall-through tail's flag clear.
			if (Memory.BestSeeUnknown == Seen->Handle)
			{
				return;
			}
			// `102b3e8c`: `m_hBestSeeUnknown = entity` and `m_iEnemySightings += 1`.
			Memory.BestSeeUnknown = Seen->Handle;
			++EnemySightings;
			// `102b3ea5`: `m_hLastSeeUnknown` resolving to the SAME entity is the repeat arm.
			if (Memory.LastSeeUnknown == Seen->Handle)
			{
				++Memory.SeeUnknownRepeatSightings;
				// `102b3ecb`: `m_bfAINPCFlags &= 0xfebfffff` — IGNORE_UNKNOWN (0x400000) and
				// MADE_INITIAL_RESPONSE (0x1000000). NOT LOOKED_AT_UNKNOWN: this arm returns before
				// the tail that clears it.
				NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
				NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
				return;
			}
			// `102b3ee6`: **the order the port had wrong.** `m_OnUnknownVisionPlayer` (`+0x5fa4`)
			// fires FIRST, with the entity as activator, and only then are the five words written.
			// The port fired it LAST, after all of them — an observable order difference, which is
			// why this row is `rule` and not `present`.
			FireOutput(FName(TEXT("OnUnknownVisionPlayer")), Seen->Handle);
			// `102b3f0b`: `m_hLastSeeUnknown = m_hBestSeeUnknown`.
			Memory.LastSeeUnknown = Memory.BestSeeUnknown;
			// `102b3f1e`: `m_vecLastSeeUnknownPos` from the RESOLVED handle's slot-217 origin — so
			// it is the entity `m_hLastSeeUnknown` now names, which is this one.
			Memory.LastSeeUnknownPosition = Seen->Origin;
			// `102b3f4d`: the counter and the two timers.
			Memory.SeeUnknownRepeatSightings = 0;
			Memory.SeeUnknownRunTimer = Now
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(10.f, 20.f);
			Memory.SeeUnknownStartTimer = Now
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(5.f, 10.f);
			// `102b3fd4`: the shared tail.
			NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
			NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
			NpcFlags.Clear(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
			return;
		}
		// `102b3f9c`: the refused-inside-the-gate path ORs `0x800000` into `m_bfAINPCFlags`.
		// `0x800000` is `FINISHED_IGNORE_UNKNOWN`'s neighbour in the port's word table; the shape
		// map names it `ATTACK_UNKNOWN`.
		NpcFlags.Set(EElysiumNpcFlag::ATTACK_UNKNOWN);
	}
	// `102b3fa5`: and then, on EVERY path that reached here, `m_hBestSeeUnknown` is released only
	// when it resolves to THIS entity — otherwise the body returns with nothing written at all.
	if (Seen == nullptr || Memory.BestSeeUnknown != Seen->Handle)
	{
		return;
	}
	Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
	// `102b3fd4`: the shared tail, `m_bfAINPCFlags &= 0xfe9fffff`.
	NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
	NpcFlags.Clear(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
	NpcFlags.Clear(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
}

// =================================================================================================
// Slot 478 `BestEnemy` — `CAI_BaseNPC::BestEnemy` `0x102743c0`, 884 bytes.
// =================================================================================================

// =================================================================================================
// Slot 544 `UpdateEnemyMemory` — `CAI_BaseNPC::FUN_102709c0` `0x102709c0`, 175 bytes.
// =================================================================================================

// =================================================================================================
// Slot 402 `Event_Gibbed` — `CAI_BaseNPC::FUN_102658f0` `0x102658f0`, 96 bytes.
// =================================================================================================

// =================================================================================================
// Slot 223 `CreateVPhysics` — `CAI_BaseNPC::FUN_10273720` `0x10273720`, 36 bytes.
// =================================================================================================

// =================================================================================================
// Slot 538 `AimGun` — `CAI_BaseNPC::FUN_1026b4f0` `0x1026b4f0`, 108 bytes.
// =================================================================================================

// =================================================================================================
// Slot 574 `GetShootEnemyDir` — `CAI_BaseNPC::FUN_10278900` `0x10278900`, 130 bytes.
// =================================================================================================

// =================================================================================================
// Slot 562 `WeaponLOSCondition` — `CAI_BaseNPC::FUN_1026fbe0` `0x1026fbe0`, 198 bytes.
// =================================================================================================

// =================================================================================================
// Slot 573 `InnateWeaponLOSCondition` — `CAI_BaseNPC::FUN_1026fcf0` `0x1026fcf0`, 405 bytes.
// =================================================================================================

// =================================================================================================
// Slot 445 `StartTaskOverlay` — `CAI_BaseNPC::FUN_10288710` `0x10288710`, 78 bytes.
// =================================================================================================

// =================================================================================================
// `0x1026ab50` — the shrunk-hull head probe, and the hull seams under it.
// =================================================================================================

// =================================================================================================
// The cop and hunter class statics — `DAT_1093ac3c` / `_DAT_1093aca8` and their hunter twins.
// =================================================================================================

void FElysiumNpc::ResetSpeciesSuspectGlobals()
{
	NpcKernelSenses10Shared::GCopSuspect = FElysiumEntityHandle::Invalid();
	NpcKernelSenses10Shared::GCopSuspectExpiry = 0.0;
	NpcKernelSenses10Shared::GHunterSuspect = FElysiumEntityHandle::Invalid();
	NpcKernelSenses10Shared::GHunterSuspectExpiry = 0.0;
}
