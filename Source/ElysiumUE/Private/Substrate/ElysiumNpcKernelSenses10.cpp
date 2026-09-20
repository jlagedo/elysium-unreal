#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "ElysiumSessionSubsystem.h"

// Story 29d, family **Senses10** — the Troika-line bodies. The species line is in
// `ElysiumNpcKernelSenses10_2.cpp`; the declarations and this family's standing facts are in
// `ElysiumNpcKernelSenses10.inl`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. The three corrections this family's reading made to the checklist's
// one-line walks are marked **CORRECTION** at the arm they change.

namespace
{
	// Retail's `Disposition_t`.
	constexpr int32 GD_ER = 0;
	constexpr int32 GD_HT = 1;
	constexpr int32 GD_FR = 2;
	constexpr int32 GD_LI = 3;
	constexpr int32 GD_NU = 4;

	// `_DAT_10457f54` = **0.7**, the far-band fraction slot 594 compares against
	// (`docs/vtmb/computer-terminals.md`; `ElysiumNpcKernelEntityChain2.cpp` carries the same cell).
	constexpr float GFarBandFraction = 0.7f;

	// `_DAT_1044bef8` = **0.25**, the cowering/sleeping hearing scale in slot 467
	// (`docs/vtmb/computer-terminals.md` line 1186).
	constexpr float GCoweringHearingScale = 0.25f;

	// `m_bfAINPCFlags & 0x20400` — `COWERING` (`0x400`) and `SLEEPING` (`0x20000`), the one gate that
	// lets slot 467's distance arm run at all (`102b3734`).
	constexpr uint32 GHearDistanceGateMask = 0x00020400u;

	// `_DAT_10452dc4` = **2.0**, `_DAT_104492a4` = **60.0**, `_DAT_104454d0` = **0.5**,
	// `_DAT_104454c0` = **1.0**, `_DAT_10449270` = **0.5**, `_DAT_10450568` = **360.0**,
	// `_DAT_1044eb0c` = **20.0**, `_DAT_104492dc` = **-1.0**.
	constexpr float GTwo = 2.0f;
	constexpr float GGroundpointZLift = 60.0f;
	constexpr float GHalf = 0.5f;
	constexpr float GOne = 1.0f;
	constexpr float GYawWrap = 360.0f;
	constexpr float GMingXiaoAimZBonus = 20.0f;

	// The cop and hunter class statics. STATIC IN RETAIL — `DAT_1093ac3c` / `_DAT_1093aca8` are one
	// grudge every cop in the map shares, and `DAT_1093b650` / `_DAT_1093b658` are the hunter's.
	FElysiumEntityHandle GCopSuspect;
	double GCopSuspectExpiry = 0.0;
	FElysiumEntityHandle GHunterSuspect;
	double GHunterSuspectExpiry = 0.0;

	double NowOf(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	bool IsPlayerRecord(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		// `+0x00a8 m_pPlayer`, `CBaseEntity`'s self-downcast cache: non-null on exactly the player.
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
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

int32 FElysiumNpc::IRelationPriorityOf(const FElysiumEntity* Candidate) const
{
	// `IRelationPriority` (`0x10333700`) — the raw integer, with `FElysiumRelationships`' own
	// recovered defaults (5 for a live actor with no row, 0 for a null target).
	//
	// **This one does NOT become a forward, and the reason is a slot boundary rather than a gap.**
	// Story 29d's family Conditions10 owns slot **404**, not slot 405: `0x10333700` is a LAYER 0 row
	// of story 29c's band whose overlay target is still the generated stub, so forwarding would tell
	// every caller that every entity has priority 0 — which is the same error the `.inl` above
	// records for slot 404 before it landed. The store this reads IS what `0x10333700` reads.
	if (Candidate == nullptr)
	{
		return 0;
	}
	const FString Classname = Candidate->Def ? Candidate->Def->Classname : FString();
	return Relationships.ResolvePriority(Candidate->Handle, Classname);
}

// =================================================================================================
// Slot 201 `FVisible` — `CAI_BaseNPCTroika::FVisible` `0x102b4630`, 232 bytes.
// =================================================================================================

void FElysiumNpc::WriteFVisibleBlocker(const FElysiumEntity* SeenTarget)
{
	++FVisibleBlockerWrites;
	LastFVisibleBlockerTarget = SeenTarget != nullptr ? SeenTarget->Handle : FElysiumEntityHandle::Invalid();
}

bool FElysiumNpc::BaseEntityFVisible(const FElysiumEntity& SeenTarget, int32 /*Mask*/) const
{
	// `CBaseEntity::FVisible` — the eye-to-eye segment slot 201 ends at. The mask is the caller's;
	// this runtime's embodiment answers one world term and takes no mask.
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	return Embodiment == nullptr
		|| Embodiment->QueryLineOfSight(EyePosition(), SeenTarget.EyePosition());
}

bool FElysiumNpc::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `0x102b4630`. The one-shot static init at `102b4630` resolves the `Dominate_BrainWipe`
	// discipline id into `DAT_10924074` through `0x101e1590` / `0x101e1870` and is guarded by bit 0
	// of `DAT_10923e2c`. This runtime keys a discipline by NAME, so the id resolution has no
	// counterpart and the refusal arm below reads the name directly; the once-flag is unobservable.

	// The species arms of slot 201. `CNPC_VCameraSecurity` REPLACES the body outright;
	// `CNPC_VTzimisce` and `CNPC_VZombie` wrap it. `CNPC_VWerewolf` (`0x103cb810`) and `CNPC_VYukie`
	// (`0x103ddaf0`) are story 29c-1's family Senses and answer through the same table.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 201);
	if (SlotBody != nullptr && SpeciesDispatchingSlot != 201)
	{
		if (FCString::Strcmp(SlotBody, TEXT("0x10369ff0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 201);
			return CameraSecurityFVisible(SeenTarget);
		}
		if (FCString::Strcmp(SlotBody, TEXT("0x103ba290")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 201);
			return TzimisceFVisible(SeenTarget, Mask, Blocker, Arg4);
		}
		if (FCString::Strcmp(SlotBody, TEXT("0x103e0bc0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 201);
			return ZombieFVisible(SeenTarget, Mask, Blocker, Arg4);
		}
		if (FCString::Strcmp(SlotBody, TEXT("0x103cb810")) == 0)
		{
			// `CNPC_VWerewolf#201`, story 29c-1's `WerewolfFVisible`. Dispatched, not re-ported.
			FSpeciesDispatchScope Scope(*this, 201);
			FElysiumEntityHandle Unused;
			return WerewolfFVisible(SeenTarget, &Unused);
		}
		if (FCString::Strcmp(SlotBody, TEXT("0x103ddaf0")) == 0)
		{
			// `CNPC_VYukie#201`, story 29c-1's `YukieFVisible`, which chains slot 594 below.
			FSpeciesDispatchScope Scope(*this, 201);
			FElysiumEntityHandle Unused;
			return YukieFVisible(SeenTarget, &Unused);
		}
	}

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
	if (ElysiumNpcSense::IgnorePlayer() && IsPlayerRecord(*this, SeenTarget))
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
	// `102b46b1`: the base `CBaseEntity::FVisible` does the trace itself.
	return BaseEntityFVisible(*SeenTarget, Mask);
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

bool FElysiumNpc::IsBccTargetable(const FElysiumEntity& Candidate)
{
	// `m_bIsBCCTargetable` (`+0x1480`) — no longer a seam. Story 29e's family Lifecycle19 landed the
	// byte itself: `CAI_BaseNPCTroika::NPCInit` (`1029a4a2`) sets it when and only when
	// `m_statTemplate` is a non-empty string, and five species bodies clear it afterwards (Camera,
	// Placeholder, Newscaster, PlayerController; the payphone sets it). 2055 of the 2060 `npc_*`
	// entities in the shipped maps author a `stattemplate`, and the five that do not are
	// `npc_VNewscaster`, whose own `NPCInit` (`0x103a0420`) clears the byte anyway — so the gate
	// costs nothing that retail keeps and refuses exactly what retail refuses.
	//
	// An entity that is NOT an NPC has no such byte: retail's gate is on `+0x9c`'s combat character
	// and every caller has already established that, so a non-NPC combat character passes.
	const FElysiumNpc* const CandidateNpc = Candidate.AsNpc();
	return CandidateNpc == nullptr || CandidateNpc->bIsBccTargetable;
}

bool FElysiumNpc::HasNoTargetFlag(const FElysiumEntity& /*Candidate*/)
{
	return false;  // SEAM: `GetFlags() & 0x8000` (FL_NOTARGET) — the admitting arm.
}

bool FElysiumNpc::InnateWeaponLosTrace(const FVector& StartCm, const FVector& EndCm,
	FElysiumEntity*& OutBlocker) const
{
	OutBlocker = nullptr;
	// SEAM: mask `0x46004003` with the self filter `0x101d3190(this, 0)`. Family Motor's hull trace
	// is the nearest seam this substrate has and it reports no hit, which is a clear trace.
	FKernelHullTrace Trace;
	const FVector StartUnits = StartCm / ElysiumMove::U;
	const FVector EndUnits = EndCm / ElysiumMove::U;
	if (KernelHullTrace(StartUnits, EndUnits, FVector::ZeroVector, FVector::ZeroVector,
		0x46004003, Trace))
	{
		if (World != nullptr && Trace.HitEntity.IsSet())
		{
			OutBlocker = World->Resolve(Trace.HitEntity);
		}
		return Trace.Fraction >= 1.f;
	}
	return true;
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
		&& !Senses.Memory.bEnemyWentOccluded;
	const bool bOverrideActive = NowOf(*this) < Senses.Memory.StealthVisionOverrideUntil;
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
	if (World == nullptr || !IsPlayerRecord(*this, Owner))
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
	if (ElysiumNpcSense::IgnorePlayer() && Owner != nullptr && IsPlayerRecord(*this, Owner))
	{
		return false;
	}
	// `102b361b`: `(m_bfNPCFrenziedFlags & 0x800) == 0x800` and the owner resolving to
	// `m_hFriendPlayer`. THE ARM THE PORT WAS MISSING — `ElysiumNpcSenses.cpp` recorded it as "16c
	// work" and the seam answered not-a-friend. `FriendPlayer` is a real word (`+0x60ac`) and the
	// frenzied word is a real word; both ship at their retail defaults, so the arm is live.
	if (NpcFlags.HasFrenzied(FElysiumNpcFlags::FrenziedFriendPlayer) && FriendPlayer.IsSet()
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
			&& IsPlayerRecord(*this, Owner))
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
	// The one species arm, `CNPC_VCameraSecurity#468` (`0x1036a030`), is story 29c-1's
	// `CameraSecurityQuerySeeEntity` and REPLACES this body: the camera sees the player and nothing
	// else.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 468);
	if (SlotBody != nullptr && SpeciesDispatchingSlot != 468
		&& FCString::Strcmp(SlotBody, TEXT("0x1036a030")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 468);
		return Candidate != nullptr && CameraSecurityQuerySeeEntity(*Candidate);
	}

	// `102b38b0`: `npc_ignore_senses`, or `npc_ignore_player` with a non-null candidate carrying a
	// player record. Note the null check sits INSIDE the second arm only.
	if (ElysiumNpcSense::IgnoreSenses())
	{
		return false;
	}
	if (ElysiumNpcSense::IgnorePlayer() && Candidate != nullptr && IsPlayerRecord(*this, Candidate))
	{
		return false;
	}
	// `102b38f1`: the frenzy-friend veto. THE ARM THE PORT WAS MISSING.
	if (NpcFlags.HasFrenzied(FElysiumNpcFlags::FrenziedFriendPlayer) && Candidate != nullptr
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
	if (IsPlayerRecord(*this, Candidate))
	{
		return true;
	}
	// `102b393c`: slot 404 `IRelationType`, true for D_HT and D_FR alone. Every other disposition —
	// including retail's `default:` — answers false.
	const int32 Relation = IRelationTypeOf(Candidate);
	return Relation == GD_HT || Relation == GD_FR;
}

// =================================================================================================
// Slot 469 `OnLooked` — `CAI_BaseNPCTroika::OnLooked` `0x102b39a0`, and the base body beneath it.
// =================================================================================================

void FElysiumNpc::BaseOnLooked()
{
	// `CAI_BaseNPC::OnLooked` (`0x1026a2c0`). The body is `ElysiumNpcCond::GatherSight`, which
	// carries every arm in retail's order: the six-entry `ClearCondition` table at `0x105c979c`, the
	// skip entity resolved off `m_bfAINPCFlags2 & 0x400000`, `SEE_PLAYER` with the `0x1017ff40`
	// per-relation stamp, the `relation != D_NU` gate, `SEE_ENEMY` for the committed enemy, the
	// D_CALM divert and the three `IRelationPriority` thresholds, and the slot-544
	// `UpdateEnemyMemory` write.
	if (World == nullptr)
	{
		return;
	}
	ElysiumNpcCond::GatherSight(*this, World->NowSeconds(), Cognition.Conditions);
}

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
	// The two species arms, `CNPC_VCop` (`0x10371ae0`) and `CNPC_VHunter` (`0x103887d0`). Both run
	// this body unconditionally after their own stamp, through a DIRECT call in retail — which is
	// what `FSpeciesDispatchScope` is.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 472);
	if (SlotBody != nullptr && SpeciesDispatchingSlot != 472)
	{
		if (FCString::Strcmp(SlotBody, TEXT("0x10371ae0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 472);
			CopOnSeeEntity(Seen);
			return;
		}
		if (FCString::Strcmp(SlotBody, TEXT("0x103887d0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 472);
			HunterOnSeeEntity(Seen);
			return;
		}
	}

	FElysiumNpcMemory& Memory = Senses.Memory;
	const double Now = NowOf(*this);

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
		const FElysiumPlayer* Player = IsPlayerRecord(*this, Seen) && World != nullptr
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

bool FElysiumNpc::BestEnemyCandidateVisible(FElysiumEntity* Candidate)
{
	// `10274585` / `10274686`: `CAI_Senses::DidSeeEntity(m_pSenses, cand)` (`0x1030fb10`) — THIS
	// Look pass's accepted set — OR slot 201 `FVisible(cand, 0x2804091, 0, 0)`.
	if (Candidate == nullptr)
	{
		return false;
	}
	if (Senses.Sighted().Contains(Candidate->Handle))
	{
		return true;
	}
	return FVisible(Candidate, 0x2804091, nullptr, 0);
}

int32 FElysiumNpc::BestEnemyDistanceKey(const FElysiumEntity& Candidate) const
{
	// `1027452b`..`10274552`: the three slot-217 deltas, squared and summed, then `__ftol`
	// (`0x10431320`, 39 bytes, no callees — there is NO root). SOURCE units squared.
	const double UnitsSquared = FVector::DistSquared(Origin, Candidate.Origin)
		/ (static_cast<double>(ElysiumMove::U) * static_cast<double>(ElysiumMove::U));
	return static_cast<int32>(FMath::TruncToInt64(UnitsSquared));
}

FElysiumEntity* FElysiumNpc::BestEnemy()
{
	// The one species arm, `CNPC_VFrenzyShadow#478` (`0x103766d0`), REPLACES this body.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 478);
	if (SlotBody != nullptr && SpeciesDispatchingSlot != 478
		&& FCString::Strcmp(SlotBody, TEXT("0x103766d0")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 478);
		return FrenzyShadowBestEnemy();
	}

	if (World == nullptr)
	{
		return nullptr;
	}
	// `102743c0`: the four incumbent words, at retail's seeds.
	FBestEnemyState State;

	// `102743eb`: `GetEnemies()->+0xc` is the memory list head; `+0x38` is the next link and `+0x24`
	// the record's handle. An empty list answers null immediately.
	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		FElysiumEntity* Candidate = World->Resolve(Record.Handle);
		// `1027440e`: an unresolvable or null handle is skipped.
		if (Candidate == nullptr)
		{
			continue;
		}
		// `10274442`: `GetFlags()` bit `0x8000` — `FL_NOTARGET`, read as the SIGN of `(flags >> 8)`
		// (`TEST AH,AH / JS`). THE FIRST GATE THE PORT WAS MISSING. `FElysiumEntity` carries no
		// `FL_NOTARGET` word, so this reads false — retail's own "targetable" answer, the admitting
		// one, and the retail flag it stands for is named here.
		if (HasNoTargetFlag(*Candidate))
		{
			continue;
		}
		// `10274451`: an entity whose `+0x9c` combat character is live but whose `m_bIsBCCTargetable`
		// (`+0x1480`) is CLEAR is rejected; an entity with NO combat character passes this gate.
		// THE SECOND GATE THE PORT WAS MISSING. `m_bIsBCCTargetable` has no port word and no
		// recovered clearer, so it reads true — again the admitting arm.
		const FElysiumCombatCharacter* Character = Candidate->AsCombatCharacter();
		if (Character != nullptr && !IsBccTargetable(*Candidate))
		{
			continue;
		}
		// `10274469`: never self.
		if (Candidate->Handle == Handle)
		{
			continue;
		}
		// `10274475`: slot 158 `IsAlive` on the CANDIDATE.
		if (Candidate->IsInert())
		{
			continue;
		}
		// `10274483`: slot 404 `IRelationType`, D_HT or D_FR alone. Retail dispatches it TWICE when
		// the first answer is not 1; the query is pure, so one call is the same observation.
		const int32 Relation = IRelationTypeOf(Candidate);
		if (Relation != GD_HT && Relation != GD_FR)
		{
			continue;
		}
		// `102744a7`: `HasEludedMe` (`0x102e0210`) on the same `GetEnemies()` list.
		if (EnemyMemory.IsEluded(Candidate->Handle))
		{
			continue;
		}
		// `102744c1`: slot 530 `IsUnreachable`.
		const bool bCandidateUnreachable = IsUnreachable(Candidate);

		if (!State.bUnreachable)
		{
			// `102744de`: the incumbent is REACHABLE. An unreachable candidate is dropped outright —
			// it can never displace a reachable incumbent, whatever its priority or distance.
			if (bCandidateUnreachable)
			{
				continue;
			}
		}
		else if (!bCandidateUnreachable)
		{
			// `1027456a`: the incumbent is unreachable (or is the seed) and the candidate is
			// reachable — the OUTRIGHT WIN, with no priority and no distance comparison at all.
			// `10274572`: but slot 479 `IsValidEnemy` must pass first. THE THIRD GATE THE PORT WAS
			// MISSING, and retail requires it here as well as on the replacement paths.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			State.bVisible = BestEnemyCandidateVisible(Candidate);   // 10274585
			State.Priority = IRelationPriorityOf(Candidate);         // 102745b6
			State.Distance = BestEnemyDistanceKey(*Candidate);       // 102745c9
			State.bUnreachable = false;                              // 1027460b
			State.Best = Candidate;                                  // 10274708
			continue;
		}

		// `102744e6`: the two matching reachability classes fall here.
		const int32 Priority = IRelationPriorityOf(Candidate);
		if (Priority > State.Priority)
		{
			// `102744fe`: slot 479 again. A candidate that fails it is DROPPED, not demoted to the
			// equal-priority comparison.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			State.Priority = Priority;                          // 10274517
			State.Distance = BestEnemyDistanceKey(*Candidate);  // 1027455d
			State.bUnreachable = bCandidateUnreachable;         // 10274561
			State.Best = Candidate;                             // 10274708
			// `bVisible` is deliberately NOT touched: the next equal-priority candidate observes the
			// PREVIOUS incumbent's visibility byte. A retail selection quirk, kept.
			continue;
		}
		// `10274615`: only an EQUAL priority carries on; a lower one is dropped.
		if (Priority != State.Priority)
		{
			continue;
		}
		const int32 Distance = BestEnemyDistanceKey(*Candidate);    // 10274627
		const bool bCloser = Distance < State.Distance;             // 1027466b SETL
		// `1027467a`: not closer AND the incumbent is visible — dropped before the candidate's own
		// visibility is even computed.
		if (!bCloser && State.bVisible)
		{
			continue;
		}
		const bool bCandidateVisible = BestEnemyCandidateVisible(Candidate);   // 10274686
		// `102746b4`: closer replaces when the candidate is visible OR the incumbent is not;
		// `102746ca`: farther replaces only when the incumbent is unseen AND the candidate is seen.
		const bool bDisplaces = bCloser
			? (bCandidateVisible || !State.bVisible)
			: (!State.bVisible && bCandidateVisible);
		if (!bDisplaces)
		{
			continue;
		}
		// `102746d6`: slot 479 once more.
		if (!IsValidEnemy(Candidate))
		{
			continue;
		}
		State.Distance = Distance;                          // 102746eb
		State.bVisible = bCandidateVisible;                 // 102746f2
		State.Priority = IRelationPriorityOf(Candidate);    // 102746f6
		State.bUnreachable = bCandidateUnreachable;         // 10274704
		State.Best = Candidate;                             // 10274708
	}
	return State.Best;
}

// =================================================================================================
// Slot 544 `UpdateEnemyMemory` — `CAI_BaseNPC::FUN_102709c0` `0x102709c0`, 175 bytes.
// =================================================================================================

uint32 FElysiumNpc::SquadWord() const
{
	// `+0x5da4`. SEAM: no squad object stands here, so the word is retail's own "no squad" value.
	return 0;
}

bool FElysiumNpc::UpdateCaiMemory(FElysiumEntity* Enemy, const FVector& PositionCm)
{
	// `CAI_Memory::UpdateMemory` (`0x102df700`) with the node array at `m_pNavigator+0x2c`, the
	// enemy, the position and the enemy's velocity. The node array is the AI network and does not
	// exist here, so the record's two node ids stay `INDEX_NONE`.
	const bool bFirstRecord = Enemy != nullptr && EnemyMemory.Find(Enemy->Handle) == nullptr;
	const double Now = NowOf(*this);
	if (Enemy == nullptr)
	{
		EnemyMemory.UpdatePositionOnly(PositionCm, Now);
		return false;
	}
	EnemyMemory.UpdateAtPosition(*this, Enemy->Handle, PositionCm, Now);
	return bFirstRecord;
}

bool FElysiumNpc::UpdateEnemyMemory(FElysiumEntity* Enemy, const FVector& PositionCm,
	FElysiumEntity* /*Informer*/)
{
	// `102709c0`: `GetEnemies()` null answers TRUE at once and writes nothing. This runtime's store
	// is a member and is never null; the arm is named so the recovered answer is on record.
	if (GetEnemies() == nullptr)
	{
		return true;
	}
	if (Enemy != nullptr)
	{
		// `102709df`: the SQUADMATE gate. **CORRECTION to the pack-01 row, confirmed here**: the
		// enemy's NPC sub-object is `param_1[0x25]` = `+0x94`, not `+0xa8`; `m_iSquadDisconnected`
		// is `+0x5bb0`; and `+0x5d34` is `m_pNavigator`, not a squad word.
		const FElysiumNpc* EnemyNpc = Enemy->AsNpc();
		if (EnemyNpc != nullptr && SquadDisconnected < 1 && SquadWord() != 0)
		{
			// `102709fd`: the enemy's squad word is `0` when its own `m_iSquadDisconnected` is above
			// zero, and its `+0x5da4` otherwise.
			const uint32 EnemySquad = EnemyNpc->SquadDisconnected > 0 ? 0u : EnemyNpc->SquadWord();
			// `10270a15`: equal squads AND the enemy still connected answers FALSE — squadmates
			// never enter each other's memory.
			if (EnemySquad == SquadWord() && EnemyNpc->SquadDisconnected < 1)
			{
				return false;
			}
		}
		// `10270a2b`: `IsEluded` (`0x102e0210`) on the same list fires slot 494 `FoundEnemySound`.
		if (EnemyMemory.IsEluded(Enemy->Handle))
		{
			FoundEnemySound();
		}
	}
	// `10270a55`: the unconditional forward, whose answer is this slot's answer.
	return UpdateCaiMemory(Enemy, PositionCm);
}

// =================================================================================================
// Slot 402 `Event_Gibbed` — `CAI_BaseNPC::FUN_102658f0` `0x102658f0`, 96 bytes.
// =================================================================================================

void FElysiumNpc::CreateSecondaryDiscParticles()
{
	++SecondaryDiscParticleBursts;
}

void FElysiumNpc::UtilRemoveSelf()
{
	++UtilRemoveCalls;
	// `UTIL_Remove(this)` (`0x101cd940`). This substrate's removal is the world's, and a body that
	// removes itself from inside a slot would invalidate the caller's `this`; the call is recorded
	// and the entity is marked dead, which is the observable half.
	bDead = true;
}

bool FElysiumNpc::Event_Gibbed()
{
	// `102658f0`: slot 394 `CorpseGib` FIRST, and its answer is this body's answer on EVERY path.
	const bool bGibbed = CorpseGib();
	if (!bGibbed)
	{
		// `10265904`: slot 395 `CorpseFade` and `return 0` — note the return is the literal `0`,
		// which here equals the first gate's own answer.
		CorpseFade();
		return false;
	}
	// `10265915`: slot 398 `HasExplosiveGibs` decides the rest.
	if (!HasExplosiveGibs())
	{
		// `10265922`: the remove arm is the ONE path that does not run `CorpseFade`.
		UtilRemoveSelf();
		return bGibbed;
	}
	// `1026593a`: the disc particles are tied to the SECOND gate alone.
	CreateSecondaryDiscParticles();
	ScriptHide();    // slot 77, vtable +0x134
	CorpseFade();    // slot 395
	return bGibbed;
}

// =================================================================================================
// Slot 223 `CreateVPhysics` — `CAI_BaseNPC::FUN_10273720` `0x10273720`, 36 bytes.
// =================================================================================================

float FElysiumNpc::ModelMassKeyvalue() const
{
	return 0.f;   // SEAM: no studio header on this substrate; retail's at-or-below-zero arm.
}

bool FElysiumNpc::IsFemaleBody() const
{
	// The sheet's `Gender` slot, which is the same authored word retail's shadow-mass arm reads.
	return !Sheet.IsMale();
}

void FElysiumNpc::BuildVPhysicsShadow()
{
	// `0x10272f40`. Arm 1: slot 94 `GetMoveType()` answering `7` refuses outright.
	if (GetMoveType() == 7)
	{
		return;
	}
	// `VPhysicsInitShadow(true, false, NULL)` after destroying any existing object.
	VPhysicsShadow.bBuilt = true;
	const float AuthoredMass = ModelMassKeyvalue();
	VPhysicsShadow.MassKg = AuthoredMass > 0.f ? AuthoredMass : (IsFemaleBody() ? 65.f : 90.f);
	// The damping: the summed hull extents times `_DAT_10449270` (0.5), squared.
	const FVector Extents = HullMaxsUnits(false) - HullMinsUnits(false);
	const float Summed = (Extents.X + Extents.Y + Extents.Z) * GHalf;
	VPhysicsShadow.Damping = Summed * Summed;
	bHasPhysicsObject = true;
}

bool FElysiumNpc::CreateVPhysics()
{
	// `10273720`: BOTH slot 158 `IsAlive` and a null physics object are required before the shadow
	// is built, and the slot answers TRUE unconditionally — including when nothing was created.
	if (IsAlive() && !bHasPhysicsObject)
	{
		BuildVPhysicsShadow();
	}
	return true;
}

// =================================================================================================
// Slot 538 `AimGun` — `CAI_BaseNPC::FUN_1026b4f0` `0x1026b4f0`, 108 bytes.
// =================================================================================================

void FElysiumNpc::AimGun()
{
	// `1026b4f0`: the whole body is gated on slot 167 `GetEnemy()` being non-null, and does nothing
	// without an enemy. No member is written here; every write happens inside the slots it calls.
	if (GetEnemy() == nullptr)
	{
		return;
	}
	const FVector OriginCm = Origin;                                    // slot 217 +0x364
	const FVector ShootPositionCm = Weapon_ShootPosition(OriginCm);     // slot 389 +0x614
	// slot 574 +0x8f8, with BOTH trailing arguments 0.
	const FVector Direction = GetShootEnemyDir(ShootPositionCm, 0, 0);
	SetAim(Direction);                                                  // slot 539 +0x86c
}

// =================================================================================================
// Slot 574 `GetShootEnemyDir` — `CAI_BaseNPC::FUN_10278900` `0x10278900`, 130 bytes.
// =================================================================================================

FVector FElysiumNpc::ShootEnemyAimPoint(const FVector& ShootPositionCm)
{
	// `0x10278650`, arm 1 (`10278654`): `m_hShootTargetOverride` (`+0x5ba8`) live answers that
	// entity's slot-217 origin, whole.
	if (World != nullptr && ShootTargetOverride.IsSet())
	{
		if (const FElysiumEntity* Override = World->Resolve(ShootTargetOverride))
		{
			return Override->Origin;
		}
	}
	FElysiumEntity* Enemy = GetEnemy();
	if (Enemy == nullptr)
	{
		// `102786af`: no enemy — the body's own forward through `AngleVectors` (`0x10139610`) off
		// slot `+0x374`. SEAM: this runtime's `+0x374` accessor is the entity's angles and the
		// vector build is family Geometry's; the aim point is this body's own eye position, which
		// is where retail's degenerate arm lands.
		return EyePosition();
	}
	// `102786e2`: the enemy-memory LKP (`0x102dfed0`) plus the enemy's `BodyTarget(shootPos)` minus
	// its slot-217 origin.
	FVector LastKnownCm = Enemy->Origin;
	if (const FElysiumNpcEnemyMemoryRecord* Record = EnemyMemory.Find(Enemy->Handle))
	{
		LastKnownCm = Record->LastPosition;
	}
	const FVector BodyTargetCm = Enemy->EyePosition();
	// `1027879f`: `+_DAT_104994e0` is added to Z when the enemy's stat `0x0b` reads `5`. SEAM:
	// `_DAT_104994e0` is UNRECOVERED and the `CVStatList_t` join by retail list TYPE does not exist
	// on this sheet, so the stat reads not-5 and the bonus is not applied.
	(void)ShootPositionCm;
	return LastKnownCm + (BodyTargetCm - Enemy->Origin);
}

FVector FElysiumNpc::GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B)
{
	// The one species arm, `CNPC_VMingXiao#574` (`0x10395d00`).
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 574);
	if (SlotBody != nullptr && SpeciesDispatchingSlot != 574
		&& FCString::Strcmp(SlotBody, TEXT("0x10395d00")) == 0)
	{
		FSpeciesDispatchScope Scope(*this, 574);
		return MingXiaoGetShootEnemyDir(ShootPositionCm, A, B);
	}

	// `10278918`: the aim point, then the caller's shoot position subtracted.
	const FVector Delta = ShootEnemyAimPoint(ShootPositionCm) - ShootPositionCm;
	// `10278959`: `VectorNormalize` IN PLACE, and the three floats stored out afterwards. The
	// decompiled C shows the raw delta because it misses the `POP ESI` at `10278969` that shifts the
	// stack reads; the listing stores `[ESP+0x4]`, `[ESP+0x4]` and `[ESP+0x8]` AFTER the pop, which
	// are the normalised components. So the slot answers a UNIT direction.
	return Delta.GetSafeNormal();
}

// =================================================================================================
// Slot 562 `WeaponLOSCondition` — `CAI_BaseNPC::FUN_1026fbe0` `0x1026fbe0`, 198 bytes.
// =================================================================================================

bool FElysiumNpc::PlayerInLineOfFire(const FVector& OwnerPosCm, const FVector& TargetPosCm) const
{
	// `0x10266b10`. `0x10137220` (`1057966c`) is `VectorNormalize`, which ANSWERS THE LENGTH — that
	// is where both distance terms come from, and why they are unsquared.
	if (World == nullptr)
	{
		return false;
	}
	FVector ToTarget = TargetPosCm - OwnerPosCm;
	const float TargetDistance = static_cast<float>(ToTarget.Size());
	// CRASH GUARD, named: retail divides by the length unguarded. A degenerate delta normalises to
	// the zero vector here, whose dot is 0 and which therefore fails the 0.92 test.
	ToTarget = ToTarget.GetSafeNormal();

	// `10266b52`: the walk is `1 .. gpGlobals->maxClients`. This runtime stands one player.
	const FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr)
	{
		return false;
	}
	FVector ToPlayer = Player->EyePosition() - OwnerPosCm;   // slot 0x300 WorldSpaceCenter
	const float PlayerDistance = static_cast<float>(ToPlayer.Size());
	ToPlayer = ToPlayer.GetSafeNormal();
	// `10266bd4`: `dot > 0.92` AND `targetDist > playerDist`, both STRICT (the `<` and `==` bits are
	// tested together and both must be clear).
	const float Dot = static_cast<float>(FVector::DotProduct(ToTarget, ToPlayer));
	return Dot > 0.92f && TargetDistance > PlayerDistance;
}

bool FElysiumNpc::WeaponLOSCondition(const FVector& OwnerPosCm, const FVector& TargetPosCm,
	bool bSetConditions)
{
	bool bAnswer = false;
	// `1026fbf1`: `GetActiveWeapon()`.
	if (Inventory.ActiveWeapon.IsSet())
	{
		// `1026fc76`: the weapon's own slot `+0x5b0`. SEAM: this runtime stands no
		// `CBaseCombatWeapon` vtable; the weapon's answer is ADMITTING (`true`), so the 0.92 test
		// below is what a weapon-carrying body is decided by.
		bAnswer = true;
	}
	else
	{
		// `1026fc00`: capabilities (slot 513) without bit `0x20000` answer 0, and only when
		// `bSetConditions` do they touch the ConVar object and raise COND `0x42`.
		const int32 Capabilities = CapabilitiesGet();
		if ((Capabilities & 0x20000) == 0)
		{
			if (bSetConditions)
			{
				Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x42));
			}
			bAnswer = false;
		}
		else
		{
			// `1026fc3c`: slot 573 `InnateWeaponLOSCondition`.
			bAnswer = InnateWeaponLOSCondition(OwnerPosCm, TargetPosCm, bSetConditions);
		}
	}
	// `1026fc8b`: capabilities are re-read, and bit `0x10000000` adds the friendly-fire test — which
	// OVERRIDES a weapon that said yes.
	if ((CapabilitiesGet() & 0x10000000) != 0)
	{
		if (PlayerInLineOfFire(OwnerPosCm, TargetPosCm))
		{
			if (bSetConditions)
			{
				Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(100));
			}
			return false;
		}
	}
	return bAnswer;
}

// =================================================================================================
// Slot 573 `InnateWeaponLOSCondition` — `CAI_BaseNPC::FUN_1026fcf0` `0x1026fcf0`, 405 bytes.
// =================================================================================================

bool FElysiumNpc::InnateWeaponLOSCondition(const FVector& OwnerPosCm, const FVector& TargetPosCm,
	bool bSetConditions)
{
	// `1026fd01`: the ray starts at the caller's position plus `m_vecViewOffset` (`+0x0184`) and
	// ends at the caller's target; `0x1004f7a0` builds it; the trace uses mask `0x46004003` with the
	// self filter `0x101d3190(this, 0)`.
	const FVector StartCm = OwnerPosCm + (EyePosition() - Origin);
	FElysiumEntity* Blocker = nullptr;
	const bool bClear = InnateWeaponLosTrace(StartCm, TargetPosCm, Blocker);

	// `1026fdbd`: `fraction == _DAT_10449280` (a DOUBLE 1.0) answers TRUE — nothing in the way.
	if (bClear)
	{
		return true;
	}
	// `1026fddb`: the hit entity being slot 167 `GetEnemy()` answers TRUE.
	if (Blocker != nullptr && Blocker == GetEnemy())
	{
		return true;
	}
	// `1026fdfa`: a hit entity that is null, or whose `+0x9c` combat character is null.
	// **CORRECTION to the checklist's walk**: the walk calls `+0x9c` "the `+0x27` word", and it
	// reports both condition raises as gated on "trace bool bytes". The listing reads
	// `[ESP+0xb8]` at `1026fe25` and `1026fe51`, which — with `SUB ESP,0xa4` and two pushes — is the
	// THIRD ARGUMENT, `bSetConditions`. There is no trace byte in either arm.
	if (Blocker == nullptr || Blocker->AsCombatCharacter() == nullptr)
	{
		if (bSetConditions)
		{
			Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x66));
			// `1026fe73`: `0x10270aa0(this, blocker)` records the blocker into `+0x5d90`
			// `m_hEnemyOccluder`.
			Senses.Memory.EnemyOccluder = Blocker != nullptr
				? Blocker->Handle : FElysiumEntityHandle::Invalid();
		}
		return false;
	}
	// `1026fe08`: slot 404 `IRelationType` equal to D_HT answers TRUE — shooting a hated blocker is
	// fine. Note `1026fe19`'s `MOV AL,AL`: the answer is the relation's own low byte, which is 1.
	if (IRelationTypeOf(Blocker) == GD_HT)
	{
		return true;
	}
	// `1026fe25`: otherwise COND `0x63` under `bSetConditions`, and false either way.
	if (bSetConditions)
	{
		Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x63));
	}
	return false;
}

// =================================================================================================
// Slot 445 `StartTaskOverlay` — `CAI_BaseNPC::FUN_10288710` `0x10288710`, 78 bytes.
// =================================================================================================

void FElysiumNpc::DisableMoveAndShootOverlay()
{
	// `0x102e8250`: `overlay+0x18 = FLT_MAX` (`0x7f7fffff`).
	MoveAndShootOverlay.NextShotTime = MAX_flt;
	++MoveAndShootOverlay.Disables;
}

void FElysiumNpc::ArmMoveAndShootOverlay(float PauseMin, float PauseMax)
{
	// `0x102e8270`: re-derive the shot counts from the active weapon's data (`+0x3a4`/`+0x3a8`),
	// store the pause pair at `overlay+0x24`/`+0x28` and re-arm `overlay+0x18 = curtime +
	// overlay+0x2c`. The same body falls back to `0x102e8250` when the NPC is in state 4, has no
	// weapon, or lacks either the `0x11` or the `0x15` activity sequence.
	//
	// SEAM: this runtime has no activity-sequence table, so the sequence half of that fallback is
	// never satisfiable; the two halves it CAN answer — state 4 and "no weapon" — are run, and the
	// sequence term is named rather than guessed.
	const bool bStateFour = GetMind().State() == EElysiumNpcState::Scripted;   // retail state 4
	if (bStateFour || !Inventory.ActiveWeapon.IsSet())
	{
		DisableMoveAndShootOverlay();
		return;
	}
	MoveAndShootOverlay.PauseMin = PauseMin;
	MoveAndShootOverlay.PauseMax = PauseMax;
	MoveAndShootOverlay.NextShotTime = static_cast<float>(NowOf(*this));
	++MoveAndShootOverlay.Arms;
}

void FElysiumNpc::StartTaskOverlay()
{
	// `10288710`: gated entirely on slot 529 `IsCurTaskContinuousMove` — a false answer leaves the
	// overlay untouched.
	if (!IsCurTaskContinuousMove())
	{
		return;
	}
	// `10288721`: slot 575 `ShouldMoveAndShoot` false disables the overlay and returns.
	if (!ShouldMoveAndShoot())
	{
		DisableMoveAndShootOverlay();
		return;
	}
	// `1028873e`: slot 419 `UpdateBurstShootPause` runs FIRST, and only then the arm.
	UpdateBurstShootPause();
	ArmMoveAndShootOverlay(BurstShootPauseMin, BurstShootPauseMax);
}

// =================================================================================================
// `0x1026ab50` — the shrunk-hull head probe, and the hull seams under it.
// =================================================================================================

FVector FElysiumNpc::HullMinsUnits(bool bSmall) const
{
	// `CAI_Navigator`'s hull table: `0x102d6100` normal mins against `0x102d6140` small mins.
	// `bSmall` picks the TABLE, on this NPC's own hull — it is not a different hull id. The
	// earlier reading passed hull 1 for "small", which answers correctly for a human only by
	// coincidence (HUMAN_PATHING_HULL's full box is the same (-8,-8,0)..(8,8,72) as HUMAN_HULL's
	// small one) and wrongly for every other species. The fallback to the entity's own collision
	// bounds stays for a hull id the table does not carry.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	const EElysiumHullExtents Which =
		bSmall ? EElysiumHullExtents::Small : EElysiumHullExtents::Full;
	if (RetailHullExtents(HullKind, Which, MinsUnits, MaxsUnits))
	{
		return MinsUnits;
	}
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	return MinsUnits;
}

FVector FElysiumNpc::HullMaxsUnits(bool bSmall) const
{
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	const EElysiumHullExtents Which =           // 0x102d6120 / 0x102d6160
		bSmall ? EElysiumHullExtents::Small : EElysiumHullExtents::Full;
	if (RetailHullExtents(HullKind, Which, MinsUnits, MaxsUnits))
	{
		return MaxsUnits;
	}
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	return MaxsUnits;
}

void FElysiumNpc::RestoreNormalHull()
{
	// `0x10273070`: restore the normal hull from the navigator, clear `+0x5f2d`, and re-run
	// `0x10272f40` when `+0x36c` stands.
	bIsUsingSmallHull = false;
	if (bHasPhysicsObject)
	{
		BuildVPhysicsShadow();
	}
}

void FElysiumNpc::HeadProbe()
{
	// `1026ab5a`: the WHOLE body runs only when the shrunk-hull latch `+0x5f2d` AND `+0x5f2c` are
	// both set. Neither has a port producer — the hull swap they record is `CAI_Navigator`'s — so
	// today this is always false and the probe does nothing, which is retail's own answer for a body
	// whose hull was never shrunk.
	if (!bIsUsingSmallHull || !bWantsLargeHull)
	{
		return;
	}
	// `1026ab72`: the box is centred on `GetAbsOrigin` raised by `_DAT_104454c0` (1.0), the hull
	// extents are halved by `_DAT_104454d0` (0.5) and scaled by `_DAT_104492dc` (-1.0), and the two
	// box flag bytes come from the zero-delta test against `_DAT_104454c4` (0.0) and the
	// `_DAT_104492e0` extent tests (`_DAT_104492e0` is UNRECOVERED).
	const FVector MinsUnits = HullMinsUnits(false);
	const FVector MaxsUnits = HullMaxsUnits(false);
	const FVector HalfExtentsUnits = (MaxsUnits - MinsUnits) * GHalf;
	const FVector CentreUnits = (MinsUnits + MaxsUnits) * GHalf;
	const FVector ProbeCentreCm = Origin
		+ FVector(0.0, 0.0, static_cast<double>(GOne) * ElysiumMove::U)
		+ CentreUnits * ElysiumMove::U;
	(void)HalfExtentsUnits;
	// `1026ac66`: the trace uses mask `0x200400b` with the self filter `0x101d3190(this, 0)`, and is
	// optionally drawn under a cvar. SEAM: this runtime has no hull sweep; a clean trace is the
	// admitting answer and is what the restore below reads.
	FElysiumEntity* Blocker = nullptr;
	const bool bClean = InnateWeaponLosTrace(Origin, ProbeCentreCm, Blocker);
	// `1026ac9c`: a clean trace (start-solid byte clear AND fraction equal to `_DAT_10449280` = 1.0)
	// calls `0x10273070`.
	if (bClean)
	{
		RestoreNormalHull();
	}
}

// =================================================================================================
// The cop and hunter class statics — `DAT_1093ac3c` / `_DAT_1093aca8` and their hunter twins.
// =================================================================================================

FElysiumEntityHandle FElysiumNpc::CopSuspectHandle() { return GCopSuspect; }
double FElysiumNpc::CopSuspectExpiry() { return GCopSuspectExpiry; }
FElysiumEntityHandle FElysiumNpc::HunterSuspectHandle() { return GHunterSuspect; }
double FElysiumNpc::HunterSuspectExpiry() { return GHunterSuspectExpiry; }

void FElysiumNpc::ResetSpeciesSuspectGlobals()
{
	GCopSuspect = FElysiumEntityHandle::Invalid();
	GCopSuspectExpiry = 0.0;
	GHunterSuspect = FElysiumEntityHandle::Invalid();
	GHunterSuspectExpiry = 0.0;
}

void FElysiumNpc::StampCopSuspect(FElysiumEntity* Seen)
{
	// `0x10370560`: `_DAT_1093aca8 = curtime + _DAT_104492a8` and `DAT_1093ac3c = seen->handle`,
	// but ONLY when the seen entity carries a non-null `+0xa8` player record.
	if (!IsPlayerRecord(*this, Seen))
	{
		return;
	}
	GCopSuspectExpiry = NowOf(*this) + SpeciesSuspectWindowSeconds;
	GCopSuspect = Seen->Handle;
}

void FElysiumNpc::StampHunterSuspect(FElysiumEntity* Seen)
{
	// `0x10387fd0`: the same pair over the hunter's own globals, with NO `+0xa8` guard — the one
	// difference between the two 54-byte twins.
	if (Seen == nullptr)
	{
		return;
	}
	GHunterSuspectExpiry = NowOf(*this) + SpeciesSuspectWindowSeconds;
	GHunterSuspect = Seen->Handle;
}

void FElysiumNpc::CopOnSeeEntity(FElysiumEntity* Seen)
{
	// `10371ae0`: when `+0x6081` is CLEAR and slot 404 answers D_HT, stamp; then the Troika body
	// runs UNCONDITIONALLY.
	if (!Senses.Memory.bPlayerInOuterBand && IRelationTypeOf(Seen) == GD_HT)
	{
		StampCopSuspect(Seen);
	}
	OnSeeEntity(Seen);
}

void FElysiumNpc::HunterOnSeeEntity(FElysiumEntity* Seen)
{
	// `103887d0`: the twin.
	if (!Senses.Memory.bPlayerInOuterBand && IRelationTypeOf(Seen) == GD_HT)
	{
		StampHunterSuspect(Seen);
	}
	OnSeeEntity(Seen);
}
