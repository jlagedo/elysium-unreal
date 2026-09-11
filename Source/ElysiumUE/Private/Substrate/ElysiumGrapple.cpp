#include "Substrate/ElysiumGrapple.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumStealthKillRules.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace
{
	FString RoleActivity(bool bVictim, bool bPartnerMale, int32 Position)
	{
		// TranslateBaseGrappleActivity 0x10328380: base+1+male+2*role+4*position.
		// GetGrappleSize 0x103282e0 is IsMale(), not measured mesh height.
		return FString::Printf(TEXT("ACT_SNEAKATTACK_SUCCESS_%s_%s%s_%s"),
			bVictim ? TEXT("VICTIM") : TEXT("ATTACKER"),
			bPartnerMale ? TEXT("TALL") : TEXT("SHORT"),
			bVictim ? TEXT("ATTACKER") : TEXT("VICTIM"), Position ? TEXT("BACK") : TEXT("FRONT"));
	}

	bool ResolvePair(FElysiumPlayer& Attacker, FElysiumNpc& Victim, int32 Hint,
		FElysiumStealthPairClips& Out)
	{
		IElysiumEmbodiment* Bodies = Attacker.World ? Attacker.World->Embodiment() : nullptr;
		if (!Bodies) return false;
		FElysiumActivityClipRequest Request;
		Attacker.FillActivityClipRequest(Request);
		Request.Source = EElysiumAnimSource::Interaction;
		Request.BodyKind = EElysiumAnimBodyKind::Player;
		Request.StateMask = INDEX_NONE;
		Request.bAllowFallbackLadder = false;
		Request.bWeaponTranslationOnly = true;
		Request.Select = EElysiumAnimSelect::Heaviest;
		// 0x10328b9e and 0x10328bc3 both dispatch on ATTACKER. Only the vocabulary changes.
		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			Out.Position = (Hint + Attempt) % 2;
			Out.VictimActivity = RoleActivity(true, Attacker.Sheet.IsMale(), Out.Position);
			Out.AttackerActivity = RoleActivity(false, Victim.Sheet.IsMale(), Out.Position);
			Request.Stem = Victim.ModelStem();
			Request.Activity = Out.VictimActivity;
			if (!Bodies->ResolveNpcActivityClip(Request, Out.Victim)) continue;
			Request.Stem = Attacker.ModelStem();
			Request.Activity = Out.AttackerActivity;
			if (Bodies->ResolveNpcActivityClip(Request, Out.Attacker)) return true;
		}
		Out.Position = INDEX_NONE;
		return false;
	}
}

bool FElysiumPlayer::CanStartStealthKill(FElysiumNpc& Victim, float MaxDistanceUnits,
	int32 PositionHint, FElysiumStealthPairClips* OutClips)
{
	if (!World || Victim.World != World || Victim.IsInert()) return false;
	IElysiumEmbodiment* Bodies = World->Embodiment();
	if (!Bodies || (Bodies->IsPlayerDucking() && !Bodies->CanStandForGrapple(Origin, Handle)))
		return false;
	// CanStartGrappleAttack 0x103285a0. Player/NPC acceptance virtuals return true;
	// an existing matching victim succeeds before distance/position tests.
	if (Victim.IsGrappling())
		return Victim.ResolveGrapplePartner() == this && Grapple.Type == EElysiumGrappleType::StealthKill;
	if (IsGrappling())
	{
		if (Grapple.Role != EElysiumGrappleRole::Attacker) return false;
		if (FeedState.IsPaired()) BreakFeed();
		else LeaveGrapplePair();
	}
	if (MaxDistanceUnits > 0.f && FVector::DistSquared2D(Origin, Victim.Origin)
		> FMath::Square(MaxDistanceUnits * ElysiumMove::U)) return false;
	FElysiumStealthPairClips Pair;
	if (!ResolvePair(*this, Victim, PositionHint, Pair)) return false;
	if (OutClips) *OutClips = MoveTemp(Pair);
	return true;
}

bool FElysiumPlayer::TryStealthKill()
{
	UElysiumSessionSubsystem* State = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Book = State ? State->Rulebook() : nullptr;
	return Book && TryStealthKill(Book->StealthKillRules());
}

bool FElysiumPlayer::TryStealthKill(const FElysiumStealthKillRules& Rules)
{
	FElysiumNpc* Victim = Rules.FindVictim(*this);
	if (!Victim) return false;
	// 0x10167370 writes both handles before the attempt, including its failure arm.
	MeleeOpponent = Victim->Handle;
	LastOpponent = Victim->Handle;
	return StartStealthKill(*Victim, Rules.DistanceMaxUnits);
}

bool FElysiumPlayer::StartStealthKill(FElysiumNpc& Victim, float MaxDistanceUnits)
{
	if (Grapple.bOwnsStealthAction) return true;
	const float Yaw = (Victim.Origin - Origin).Rotation().Yaw;
	const int32 Hint = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, -float(Victim.Angles.Y))) <= 90.f;
	FElysiumStealthPairClips Pair;
	if (!CanStartStealthKill(Victim, MaxDistanceUnits, Hint, &Pair)) return false;
	if (Pair.Position == INDEX_NONE && !ResolvePair(*this, Victim, Hint, Pair)) return false;
	IElysiumEmbodiment* Bodies = World->Embodiment();
	FVector Root;
	FVector UnusedAttackerRoot;
	if (!Bodies->SampleGrappleRoot(GetSkeletalBody(), Pair.Attacker.OwnerStem, Pair.Attacker.AnimationName, UnusedAttackerRoot)
		|| !Bodies->SampleGrappleRoot(Victim.GetSkeletalBody(), Pair.Victim.OwnerStem, Pair.Victim.AnimationName, Root))
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("stealth pair %s -> %s cannot load/sample '%s' / '%s'"),
			*DebugString(), *Victim.DebugString(), *Pair.Attacker.Label, *Pair.Victim.Label);
		return false;
	}
	if (!EnterGrapplePair(Victim, EElysiumGrappleType::StealthKill, Pair.Position)) return false;
	// PlayerTryStealthKill commits after the observer snapshot may have been
	// published for the same pre-commit frame. The committed action owns the HUD
	// answer until teardown, so discard that earlier searching/detected sample.
	Observer.Reset();
	PendingObserver.Reset();
	Grapple.bOwnsStealthAction = Victim.Grapple.bOwnsStealthAction = true;
	// 0x10329228: victim stays at its origin; attacker subtracts the victim's cycle-zero
	// Bip01 x/y rotated by ATTACKER yaw. Position 0 turns the victim by 180 degrees.
	Root.Z = 0;
	SetRuntimeOrigin(Victim.Origin - FRotator(0, Yaw, 0).RotateVector(Root));
	FVector Facing = Angles;
	Facing.Y = -Yaw;
	SetRuntimeAngles(Facing);
	Facing = Victim.Angles;
	Facing.Y = Pair.Position ? -Yaw
		: float(uint32(int32((-Yaw + 180.f) * 182.04444885f)) & 0xffff) * 0.0054931640625f;
	Victim.SetRuntimeAngles(Facing);
	Victim.SetBodyFrozen(true);
	FeedState.BloodStolen = 0;
	auto Play = [](FElysiumCombatCharacter& Character, const FElysiumActivityClip& Clip,
		const FString& Activity)
	{
		FElysiumClipSegment Segment(Clip.Label, false);
		Segment.OwnerStem = Clip.OwnerStem;
		Segment.AnimationName = Clip.AnimationName;
		Segment.Source = EElysiumAnimSource::Interaction;
		Segment.Priority = EElysiumAnimPriority::Scripted;
		Segment.Activity = Activity;
		Segment.bHoldUntilReleased = true;
		Segment.bHoldFinalPose = true;
		Character.Grapple.ClipLabel = Clip.Label;
		Character.Grapple.ClipOwner = Clip.OwnerStem;
		return Character.PlayAnimSegment(Segment, &Character.Grapple.ClipSeconds);
	};
	if (!Play(*this, Pair.Attacker, Pair.AttackerActivity) || !Play(Victim, Pair.Victim, Pair.VictimActivity))
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("stealth pair %s -> %s could not start both clips"),
			*DebugString(), *Victim.DebugString());
		LeaveGrapplePair();
		return false;
	}
	NextThink = float(World->NowSeconds());
	if (FElysiumItem* Item = Inventory.Active(*this))
		if (FElysiumWeapon* Weapon = Item->AsWeapon()) Weapon->PlayStealthKillSound();
	return true;
}

void FElysiumPlayer::TickStealthKill()
{
	if (!Grapple.bOwnsStealthAction || Grapple.Role != EElysiumGrappleRole::Attacker) return;
	FElysiumCombatCharacter* Victim = ResolveGrapplePartner();
	if (!Victim || !IsAlive() || Victim->HasReportedDeath() || Victim->IsInert())
	{
		LeaveGrapplePair();
		return;
	}
	FElysiumClipPhase Phase;
	IElysiumEmbodiment* Bodies = World ? World->Embodiment() : nullptr;
	if (!Bodies || !Bodies->GetBodyClipPhase(GetSkeletalBody(), EElysiumAnimChannel::Base, Phase))
		return; // no finished sequence was reported; never invent a completion deadline.
	if (Phase.Label != Grapple.ClipLabel || (!Grapple.ClipOwner.IsEmpty() && Phase.OwnerStem != Grapple.ClipOwner))
	{
		if (!Grapple.ClipPlayId) return; // the host publishes its first phase on its next animation update.
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s stealth action lost its attacker clip"), *DebugString());
		LeaveGrapplePair();
		return;
	}
	if (!Grapple.ClipPlayId) Grapple.ClipPlayId = Phase.PlayId;
	if (Grapple.ClipPlayId != Phase.PlayId) { LeaveGrapplePair(); return; }
	// The victim follows the attacker's normalized cycle, even when authored lengths differ.
	if (!Bodies->SyncGrappleClip(Victim->GetSkeletalBody(), Phase.Cycle * Victim->Grapple.ClipSeconds,
		Phase.Cycle >= 1.f))
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s stealth action lost its victim playback"), *DebugString());
		LeaveGrapplePair();
		return;
	}
	// 0x10165d90 reads m_bSequenceFinished. Victim playback never commits the result.
	if (!Phase.bLooping && Phase.Cycle >= 1.f)
	{
		Victim->CommitStealthDeath(Handle);
		LeaveGrapplePair();
		ResetAnimToIdle();
	}
}

void FElysiumCombatCharacter::CommitStealthDeath(const FElysiumEntityHandle& Attacker)
{
	if (HasReportedDeath()) return;
	using EC = EElysiumTraitContainer;
	const int32 Ceiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, Ceiling);
	DeathAttacker = Attacker;
	bStealthDeathCommitted = true;
	RecomputeSheet();
	OnKilled(); // virtual Event_Killed +0x240; NPC Event_Dying +0x64c is empty (0x1032bdf0).
}
