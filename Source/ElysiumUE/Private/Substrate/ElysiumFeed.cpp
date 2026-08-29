// Ordinary player-on-humanoid feeding: the command's acceptance, the minimal paired action,
// the authoritative blood transaction on the substrate clock, and the `OnFedUponBegin` /
// `OnFedUponEnd` outputs that reach authored map wires.
//
// `docs/vtmb/feeding.md` is the specification and owns every fact below; this file is the
// implementation of its "Recreation contract" items 1-9 plus the capture-backed ordinary camera,
// meter, audio, heartbeat, event-5116 particle and release-tail presentation contract.
// Seductive/rat/zombie modes and prayer remain explicitly out of scope.
//
// THE ANIMATION-EVENT BRIDGE IS A SCHEDULER, NOT A NOTIFY LISTENER.
// VtMB marks the transaction's boundaries with model-authored animation events (4007 at the bite,
// 4006 on the release). Those events are decoded offline —
// `pipeline/src/elysium_pipeline/formats/mdl_skel.py` reads `mstudioevent_t` and every `Seq` carries
// its event rows — but nothing writes them past that point: the `.eskm` container has no event
// section, the per-clip runtime manifest interns exactly
// `owner/activity/weight/flags/frames/fps/fade`, the character bake places no `AnimNotify` on the
// generated `UAnimSequence`s, and the module contains no `UAnimNotify` subclass and no notify
// subscriber at all. So there is no notify to listen to. The boundaries are therefore scheduled by
// this state machine against the substrate clock, from the clip cycles `feeding.md` decoded, and
// raised through `OnFeedAnimEvent` — the seam a real notify path replaces by calling it instead.
// That also satisfies K10: with no body at all the state machine alone carries engage -> bite ->
// loop -> release, so headless correctness never depends on something being rendered.

#include "Substrate/ElysiumFeed.h"

#include "ElysiumCameraSolve.h"
#include "ElysiumCameraService.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumUserCmd.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDiceTables.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLaw.h"        // pulse / interrupt law producers
#include "Substrate/ElysiumNpc.h"        // victim's law observation windows
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"

#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFeed, Log, All);

// --- The pure rules ---

namespace ElysiumFeed
{
	bool OpposedAccepts(int32 AttackerBrawlRating, const FElysiumRollResult& VictimHackingRoll)
	{
		return AttackerBrawlRating > FMath::Max(VictimHackingRoll.Net, 0);
	}

	namespace
	{
		const TCHAR* PhaseSuffix(EElysiumFeedPhase Phase)
		{
			switch (Phase)
			{
			case EElysiumFeedPhase::Engage:  return TEXT("engage");
			case EElysiumFeedPhase::Bite:    return TEXT("bite");
			case EElysiumFeedPhase::Loop:    return TEXT("feed_loop");
			case EElysiumFeedPhase::Release: return TEXT("feed_release");
			default:                         return TEXT("");
			}
		}
	}

	FElysiumUserCmd GatePairedUserCmd(const FElysiumUserCmd& Cmd)
	{
		FElysiumUserCmd Out = Cmd;
		Out.Move = FVector2D::ZeroVector;
		Out.Up = 0.0f;
		Out.LookDelta = FVector2D::ZeroVector;
		Out.Buttons &= static_cast<uint64>(EElysiumButton::Feed);
		return Out;
	}

	FClipPair ResolveClipPair(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight, ESide Side)
	{
		FClipPair Out;
		const TCHAR* Suffix = PhaseSuffix(Phase);
		if (!Suffix[0])
		{
			return Out;
		}
		const TCHAR* SideName = Side == ESide::Front ? TEXT("front") : TEXT("back");
		const bool bVictimTaller = VictimHeight == EPartnerHeight::Taller;
		Out.Attacker = FString::Printf(TEXT("feeding_attacker_%s_%s_%s"),
			bVictimTaller ? TEXT("tallvictim") : TEXT("shortvictim"), SideName, Suffix);
		Out.Victim = FString::Printf(TEXT("feeding_victim_%s_%s_%s"),
			bVictimTaller ? TEXT("shortattacker") : TEXT("tallattacker"), SideName, Suffix);
		return Out;
	}

	FString AudioPath(bool bVictim, bool bMale, const TCHAR* Phase)
	{
		return FString::Printf(TEXT("Character/%s/%s_%s.wav"),
			bMale ? TEXT("Male") : TEXT("Female"),
			bVictim ? TEXT("fed_upon") : TEXT("feed_on"), Phase);
	}

	float PhaseSeconds(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight)
	{
		switch (Phase)
		{
		case EElysiumFeedPhase::Engage:  return EngageSeconds;
		case EElysiumFeedPhase::Bite:    return BiteSeconds;
		case EElysiumFeedPhase::Loop:    return LoopSeconds;
		case EElysiumFeedPhase::Release:
			return VictimHeight == EPartnerHeight::Taller
				? TallVictimReleaseSeconds : ShortVictimReleaseSeconds;
		default:                         return 0.0f;
		}
	}

	float EventCycle(EElysiumFeedPhase Phase, EPartnerHeight VictimHeight)
	{
		if (Phase == EElysiumFeedPhase::Bite)
		{
			return BiteEventCycle;
		}
		if (Phase == EElysiumFeedPhase::Release)
		{
			return VictimHeight == EPartnerHeight::Taller
				? TallVictimReleaseEventCycle : ShortVictimReleaseEventCycle;
		}
		return 0.0f;
	}

}

const TCHAR* LexToString(EElysiumFeedVerdict Verdict)
{
	switch (Verdict)
	{
	case EElysiumFeedVerdict::AcceptedAutomaticState: return TEXT("accepted (automatic state)");
	case EElysiumFeedVerdict::AcceptedNotResisting:   return TEXT("accepted (does not resist)");
	case EElysiumFeedVerdict::AcceptedOpposedCheck:   return TEXT("accepted (opposed check)");
	case EElysiumFeedVerdict::RefusedOpposedCheck:    return TEXT("refused (opposed check)");
	case EElysiumFeedVerdict::RefusedBusy:            return TEXT("refused (busy)");
	case EElysiumFeedVerdict::RefusedInvalidTarget:   return TEXT("refused (invalid target)");
	}
	return TEXT("?");
}

// --- The transaction, on the character chain ---

namespace
{
	// The rulebook this character reads its rules out of, or null in a bare world. (The same helper
	// `ElysiumPlayerClasses.cpp` keeps for its own file; both are file-local by design — the two
	// translation units must not grow a shared back door to the subsystem.)
	UElysiumRulebookSubsystem* FeedRulebook(const FElysiumCombatCharacter& Char)
	{
		UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
		return GameState ? GameState->Rulebook() : nullptr;
	}

	// The four automatic-acceptance activity states, as the disposition names this runtime can
	// observe. See `FElysiumCombatCharacter::IsFeedAutoAcceptState`.
	bool IsAutoAcceptDispositionName(const FString& Disposition)
	{
		static const TCHAR* const Names[] = { TEXT("mesmerized"), TEXT("disoriented"),
			TEXT("lost"), TEXT("cower") };
		for (const TCHAR* Name : Names)
		{
			if (Disposition.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	const FName GOnFedUponBegin(TEXT("OnFedUponBegin"));
	const FName GOnFedUponEnd(TEXT("OnFedUponEnd"));

	float RenderedFeedHeightCm(const FElysiumCombatCharacter& Character)
	{
		const USkeletalMeshComponent* Body = Character.GetSkeletalBody();
		if (!Body || !FMath::IsFinite(Body->Bounds.BoxExtent.Z))
		{
			return 0.0f;
		}
		return FMath::Max(0.0f, Body->Bounds.BoxExtent.Z * 2.0f);
	}
}

int32 FElysiumCombatCharacter::BloodPoolValue() const
{
	return Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool);
}

// --- Acceptance ---------------------------------------------------------------------------------

bool FElysiumCombatCharacter::IsFeedAutoAcceptState() const
{
	// Retail tests the target's current activity: `ACT_DISPOSITION_MESMERIZED`, `ACT_DISORIENTED`,
	// `ACT_LOST` and `ACT_COWER` all accept without a roll.
	//
	// OPEN — this runtime has no ACT_* state machine to ask, so the stand-in is the character's
	// current disposition name: the one emotional-state value the substrate carries, and the one
	// that selects the standing set those activities belong to.
	return IsAutoAcceptDispositionName(Disposition);
}

bool FElysiumCombatCharacter::IsFeedBusy() const
{
	// The one owner every character on this chain can have. `FElysiumNpc` adds its dialogue session;
	// the player leaf adds nothing, because the player is never the victim of an ordinary feed.
	return ScriptOwner.IsSet();
}

bool FElysiumCombatCharacter::ResistsFeeding() const
{
	// OPEN — retail's `ResistsFeeding` is a virtual on the target and its body is not recovered.
	// What IS recovered is the data surface's `Fx_No_Resist_Feeding` trait-effect flag, whose name
	// states exactly this predicate's negative, so it is the one authored input honoured here.
	// Everything else resists and goes to the opposed check.
	const FElysiumSheetEffects* Layer = SheetEffects();
	return ElysiumFeed::ResistsByAuthoredPolicy(
		/*bFastFood*/ false, Layer && Layer->Flag(TEXT("Fx_No_Resist_Feeding")) > 0);
}

EElysiumFeedVerdict FElysiumCombatCharacter::EvaluateFeedAcceptance(FElysiumCombatCharacter& Victim)
{
	if (&Victim == this || Victim.IsInert() || IsInert())
	{
		return EElysiumFeedVerdict::RefusedInvalidTarget;
	}
	// `Replenish` refuses to start another request while the player already has a paired peer, and
	// a victim already in a pair is not a candidate either.
	if (FeedState.IsPaired() || Victim.FeedState.IsPaired())
	{
		return EElysiumFeedVerdict::RefusedBusy;
	}
	// A victim a conversation or a `scripted_sequence` already owns is not available to be grappled:
	// both are body-ownership statements the substrate already carries (K7).
	if (Victim.IsFeedBusy() || IsFeedBusy())
	{
		return EElysiumFeedVerdict::RefusedBusy;
	}

	// The policy order is retail's and the four verdicts stay distinguishable.
	if (Victim.IsFeedAutoAcceptState())
	{
		return EElysiumFeedVerdict::AcceptedAutomaticState;
	}
	if (!Victim.ResistsFeeding())
	{
		return EElysiumFeedVerdict::AcceptedNotResisting;
	}

	// The asymmetric check: the attacker's rating is compared, never rolled (K5). The victim rolls
	// `Hacking` — literal retail behaviour, not a rename candidate — at the human target number 6,
	// and its net is floored at zero.
	const int32 AttackerRating = CalcFeat(ElysiumFeed::AttackerFeat());
	const int32 VictimPool = Victim.CalcFeat(ElysiumFeed::VictimFeat());

	const FElysiumDiceTable* Weighting = &FElysiumDiceTable::Uniform();
	if (UElysiumRulebookSubsystem* Rules = FeedRulebook(Victim))
	{
		if (const FElysiumFeat* Feat = Rules->Feats().Find(ElysiumFeed::VictimFeat()))
		{
			// The victim is the roller, so the NPC weighting column applies to an NPC victim. All 23
			// shipped feats name `Normal` on both sides; the indirection is kept because a modded
			// `DiceRolls.txt` may not.
			Weighting = &Rules->Dice().ForFeat(*Feat, /*bNpc*/ Victim.Handle != (World ? World->PlayerHandle() : FElysiumEntityHandle::Invalid()));
		}
	}
	const FElysiumRollResult Roll = ElysiumDice::Roll(VictimPool, ElysiumFeed::OpposedDifficulty,
		*Weighting);

	UE_LOG(LogElysiumFeed, Verbose,
		TEXT("%s feed check vs %s: Brawl rating %d vs Hacking %s"),
		*DebugString(), *Victim.DebugString(), AttackerRating, *Roll.Describe());

	if (ElysiumFeed::OpposedAccepts(AttackerRating, Roll))
	{
		return EElysiumFeedVerdict::AcceptedOpposedCheck;
	}
	// SEAM — a separate stealth predicate can still authorise the feed after the opposed comparison
	// fails (`feeding.md` § "Target acquisition and acceptance", item 4). Its identity is OPEN and
	// stealth itself (`gameplay-systems-architecture.md` §5.9) has not landed, so the override is
	// declared here and unimplemented rather than approximated.
	return EElysiumFeedVerdict::RefusedOpposedCheck;
}

EElysiumFeedVerdict FElysiumCombatCharacter::AttemptFeed(FElysiumCombatCharacter& Victim)
{
	const EElysiumFeedVerdict Verdict = EvaluateFeedAcceptance(Victim);
	if (!ElysiumFeedAccepted(Verdict))
	{
		// Retail plays the registered `ACT_FEEDING_ENGAGE_FAILURE` path rather than creating a
		// partial transaction. The clip exists in the bake as `feeding_failure`; playing it is
		// presentation, so only the refusal is recorded here.
		UE_LOG(LogElysiumFeed, Display, TEXT("INFO - Feed refused: %s (%s)"),
			*Victim.DebugString(), LexToString(Verdict));
		return Verdict;
	}

	const double Now = World ? World->NowSeconds() : 0.0;
	StartFeedPair(Victim, Now);
	UE_LOG(LogElysiumFeed, Display, TEXT("INFO - Feed engaged: %s (%s)"),
		*Victim.DebugString(), LexToString(Verdict));
	return Verdict;
}

// --- The paired action --------------------------------------------------------------------------

FElysiumCombatCharacter* FElysiumCombatCharacter::ResolveFeedPeer() const
{
	FElysiumEntity* Ent = World ? World->Resolve(FeedState.Peer) : nullptr;
	return Ent ? Ent->AsCombatCharacter() : nullptr;
}

void FElysiumCombatCharacter::StartFeedPair(FElysiumCombatCharacter& Victim, double Now)
{
	// One owner aligns the two actors. Both halves face into the ordinary front/short pair before the
	// victim's motor is frozen through the existing `SetBodyFrozen` seam — the same switch a
	// `scripted_sequence` beat borrows, and deliberately not `SetEnabled`, which would also take the
	// body off screen. Their origins stay untouched: no unrecovered grapple placement rule is added.
	const FVector ToVictim = Victim.Origin - Origin;
	if (!ToVictim.IsNearlyZero())
	{
		// `Angles.Y` is Source's yaw, which this runtime carries negated relative to Unreal's
		// (every motor call is `-Angles.Y`), so the write is negated once, here.
		FVector Facing = Angles;
		Facing.Y = -static_cast<float>(ToVictim.Rotation().Yaw);
		SetRuntimeAngles(Facing);

		FVector VictimFacing = Victim.Angles;
		VictimFacing.Y = -static_cast<float>((-ToVictim).Rotation().Yaw);
		Victim.SetRuntimeAngles(VictimFacing);
	}

	FeedState = FElysiumFeedState();
	FeedState.Peer = Victim.Handle;
	FeedState.bVictim = false;
	FeedState.bContinuation = true;   // +0x14a8, set on acceptance
	FeedState.Phase = EElysiumFeedPhase::Engage;
	bFeedCameraAcquireFailed = false;

	Victim.FeedState = FElysiumFeedState();
	Victim.FeedState.Peer = Handle;
	Victim.FeedState.bVictim = true;
	Victim.FeedState.Phase = EElysiumFeedPhase::Engage;
	Victim.SetBodyFrozen(true);
	Victim.FeedState.bFrozenByFeed = true;
	PlayFeedStartAudio(Victim);

	const float EngageDuration = PlayFeedPhaseClips(EElysiumFeedPhase::Engage, Now);
	FeedState.PhaseDeadline = static_cast<float>(Now) + EngageDuration;
	Victim.FeedState.PhaseDeadline = FeedState.PhaseDeadline;
	EnsureFeedCamera();
	ScheduleFeedThink(Now);
}

void FElysiumCombatCharacter::EndFeedGrapple()
{
	ReleaseFeedCamera();
	if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
	{
		Peer->EndFeedVictimRole();
		Peer->FeedState.Peer = FElysiumEntityHandle::Invalid();
		Peer->FeedState.Phase = EElysiumFeedPhase::None;
		Peer->FeedState.PhaseDeadline = 0.0f;
	}
	EndFeedVictimRole();
	FeedState.Peer = FElysiumEntityHandle::Invalid();
	FeedState.bVictim = false;
	FeedState.Phase = EElysiumFeedPhase::None;
	FeedState.PhaseDeadline = 0.0f;
}

void FElysiumCombatCharacter::EndFeedVictimRole()
{
	if (FeedState.bFrozenByFeed)
	{
		SetBodyFrozen(false);
		FeedState.bFrozenByFeed = false;
		// Hand the body back to its own behaviour: the standing idle its disposition selects. A
		// bodiless character answers false and nothing happens, which is the ordinary headless case.
		ResetAnimToIdle();
		// RunThinks clears NextThink before entering Think(). A victim whose due NPC think was
		// consumed while the pair owned its body therefore has no ambient/stance appointment left.
		// Re-arm the ordinary scheduler at release; the leaf mind chooses its next owner and cadence.
		if (!IsInert())
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}
}

uint8 FElysiumCombatCharacter::FeedVictimHeightCell() const
{
	const FElysiumCombatCharacter* Victim = ResolveFeedPeer();
	const float AttackerHeight = RenderedFeedHeightCm(*this);
	const float VictimHeight = Victim ? RenderedFeedHeightCm(*Victim) : 0.0f;
	if (AttackerHeight <= KINDA_SMALL_NUMBER || VictimHeight <= KINDA_SMALL_NUMBER)
	{
		return static_cast<uint8>(ElysiumFeed::EPartnerHeight::Shorter);
	}
	return static_cast<uint8>(ElysiumFeed::VictimHeightFor(AttackerHeight, VictimHeight));
}

float FElysiumCombatCharacter::PlayFeedPhaseClips(EElysiumFeedPhase Phase, double Now)
{
	(void)Now;
	const ElysiumFeed::EPartnerHeight Height =
		static_cast<ElysiumFeed::EPartnerHeight>(FeedVictimHeightCell());
	const float MetadataSeconds = ElysiumFeed::PhaseSeconds(Phase, Height);
	const ElysiumFeed::FClipPair Pair = ElysiumFeed::ResolveClipPair(
		Phase, Height, ElysiumFeed::ESide::Front);
	FElysiumCombatCharacter* Victim = ResolveFeedPeer();
	if (!Pair.IsComplete() || !Victim)
	{
		UE_LOG(LogElysiumFeed, Warning,
			TEXT("%s cannot resolve feed phase %d: paired victim or activity cell unavailable"),
			*DebugString(), static_cast<int32>(Phase));
		return MetadataSeconds;
	}

	USkeletalMeshComponent* AttackerBody = GetSkeletalBody();
	USkeletalMeshComponent* VictimBody = Victim->GetSkeletalBody();
	if (!AttackerBody && !VictimBody)
	{
		return MetadataSeconds;   // supported headless path (K10)
	}
	if (!AttackerBody || !VictimBody)
	{
		UE_LOG(LogElysiumFeed, Warning,
			TEXT("feed pair %s -> %s has only one rendered body; refusing one-sided phase '%s'/'%s'"),
			*DebugString(), *Victim->DebugString(), *Pair.Attacker, *Pair.Victim);
		return MetadataSeconds;
	}

	// Resolve both halves before changing either body. This is the visible-action equivalent of
	// retail's initial paired-answer guard: a missing cell falls back to the deterministic headless
	// transaction and never leaves one actor performing against an idle partner.
	const bool bAttackerReady = PreloadAnimClip(Pair.Attacker);
	const bool bVictimReady = Victim->PreloadAnimClip(Pair.Victim);
	if (!bAttackerReady || !bVictimReady)
	{
		UE_LOG(LogElysiumFeed, Warning,
			TEXT("feed pair %s -> %s missing complementary clips '%s'/'%s' (attacker=%d victim=%d)"),
			*DebugString(), *Victim->DebugString(), *Pair.Attacker, *Pair.Victim,
			bAttackerReady ? 1 : 0, bVictimReady ? 1 : 0);
		return MetadataSeconds;
	}

	const bool bLoop = Phase == EElysiumFeedPhase::Loop;
	float AttackerSeconds = MetadataSeconds;
	float VictimSeconds = MetadataSeconds;
	const bool bAttackerPlayed = PlayAnimClip(Pair.Attacker, bLoop, &AttackerSeconds);
	const bool bVictimPlayed = Victim->PlayAnimClip(Pair.Victim, bLoop, &VictimSeconds);
	if (!bAttackerPlayed || !bVictimPlayed)
	{
		UE_LOG(LogElysiumFeed, Warning,
			TEXT("feed pair %s -> %s failed to play preloaded clips '%s'/'%s' (attacker=%d victim=%d)"),
			*DebugString(), *Victim->DebugString(), *Pair.Attacker, *Pair.Victim,
			bAttackerPlayed ? 1 : 0, bVictimPlayed ? 1 : 0);
		return MetadataSeconds;
	}
	if (FMath::Abs(AttackerSeconds - VictimSeconds) > (1.0f / 30.0f))
	{
		UE_LOG(LogElysiumFeed, Warning,
			TEXT("feed pair %s -> %s clip duration mismatch '%s'=%.3fs '%s'=%.3fs; attacker timing wins"),
			*DebugString(), *Victim->DebugString(), *Pair.Attacker, AttackerSeconds,
			*Pair.Victim, VictimSeconds);
	}

	// Re-assert the hold after a restore rebuilt a live motor under saved paired state.
	if (Victim->FeedState.bFrozenByFeed)
	{
		Victim->SetBodyFrozen(true);
	}
	return AttackerSeconds > KINDA_SMALL_NUMBER ? AttackerSeconds : MetadataSeconds;
}

void FElysiumCombatCharacter::EnsureFeedCamera()
{
	if (FeedState.bVictim || !FeedState.IsPaired() || FeedState.Phase == EElysiumFeedPhase::ReleaseTail)
	{
		return;
	}
	if (bFeedCameraAcquireFailed)
	{
		return;   // one failed capability acquisition, one warning for this feed
	}
	IElysiumCameraService* Service = World ? World->Camera() : nullptr;
	if (!Service)
	{
		return;   // supported headless path
	}
	if (FeedCameraHandle.IsSet() && Service->IsCameraLive(FeedCameraHandle))
	{
		return;
	}
	FeedCameraHandle.Reset();

	FElysiumCameraRequest Request;
	Request.Kind = EElysiumCameraRequestKind::Feed;
	Request.Owner = FString::Printf(TEXT("Feed:%s"), *Handle.ToString());
	Request.DebugName = FString::Printf(TEXT("Feed:%s"), *DebugString());
	Request.Priority = 450;
	Request.bOverridePose = false; // the recovered feed weight owns the faithful baseline
	Request.BlendInSeconds = 1.0f;
	Request.BlendOutSeconds = 1.0f;
	Request.Control = EElysiumCameraControlPolicy::Locked;
	Request.bShowHud = true;
	Request.bDrawViewmodel = false;
	// The feed weight is a term in `CAM_IsThirdPerson`, so the body becomes draw-eligible on its own
	// as the weight rises and fades in with the boom. Nothing here has to ask for it.
	Request.Fallback = EElysiumCameraFallback::PlayerView;
	Request.SelectedProfile = TEXT("OrdinaryFeedWeight");
	FeedCameraHandle = Service->AcquireCamera(Request);
	if (!FeedCameraHandle.IsSet())
	{
		bFeedCameraAcquireFailed = true;
		UE_LOG(LogElysiumFeed, Warning, TEXT("%s failed to acquire feed camera request"),
			*DebugString());
	}
}

void FElysiumCombatCharacter::ReleaseFeedCamera()
{
	if (!FeedCameraHandle.IsSet())
	{
		return;
	}
	IElysiumCameraService* Service = World ? World->Camera() : nullptr;
	if (Service && Service->IsCameraLive(FeedCameraHandle)
		&& !Service->ReleaseCamera(FeedCameraHandle))
	{
		UE_LOG(LogElysiumFeed, Warning, TEXT("%s failed to release feed camera slot %d"),
			*DebugString(), FeedCameraHandle.Slot);
	}
	FeedCameraHandle.Reset();
}

FElysiumVoiceHandle FElysiumCombatCharacter::SubmitFeedCue(
	const TCHAR* Cue, bool bLooping, bool bHeartbeat)
{
	IElysiumAudio* Audio = World ? World->Audio() : nullptr;
	if (!Audio)
	{
		return FElysiumVoiceHandle::Invalid();   // supported headless path
	}
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(bHeartbeat
		? FString(TEXT("Interface/heartbeat_loop.wav"))
		: ElysiumFeed::AudioPath(/*bVictim*/ FeedState.bVictim, Sheet.IsMale(), Cue));
	Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
	Request.Owner.StableId = FString::Printf(TEXT("feed.%s.%d"),
		FeedState.bVictim ? TEXT("victim") : TEXT("attacker"), Handle.Index);
	Request.Category = EElysiumAudioCategory::Sfx;
	Request.Placement.bSpatialized = true;
	Request.Placement.AttachTo = GetAttachBody();
	Request.Placement.Location = Origin;
	Request.Gain = 1.0f;
	Request.Pitch = 1.0f;
	Request.bLooping = bLooping;
	// GrappleSound's legacy attenuation 0.8 reaches zero at 1000/0.8 Source units. Unreal owns the
	// falloff mechanism; this is the recovered audible radius stated in its native centimetres.
	Request.AttenuationRadiusCm = (1000.0f / 0.8f) * ElysiumCam::U;
	const FElysiumVoiceHandle Voice = Audio->Submit(MoveTemp(Request));
	if (!Voice.IsValid() && !bFeedAudioAcquireFailed)
	{
		bFeedAudioAcquireFailed = true;
		UE_LOG(LogElysiumFeed, Warning, TEXT("%s failed to submit feed audio cue '%s'"),
			*DebugString(), bHeartbeat ? TEXT("Interface/heartbeat_loop.wav") : Cue);
	}
	return Voice;
}

void FElysiumCombatCharacter::PlayFeedStartAudio(FElysiumCombatCharacter& Victim)
{
	bFeedAudioAcquireFailed = false;
	Victim.bFeedAudioAcquireFailed = false;
	SubmitFeedCue(TEXT("start"), /*bLooping*/ false);
	Victim.SubmitFeedCue(TEXT("start"), /*bLooping*/ false);
}

void FElysiumCombatCharacter::PlayFeedLoopAudio(FElysiumCombatCharacter& Victim)
{
	if (bFeedAudioAcquireFailed || Victim.bFeedAudioAcquireFailed)
	{
		return;
	}
	StopFeedLoopAudio();
	Victim.StopFeedLoopAudio();
	FeedLoopVoice = SubmitFeedCue(TEXT("loop"), /*bLooping*/ true);
	Victim.FeedLoopVoice = Victim.SubmitFeedCue(TEXT("loop"), /*bLooping*/ true);
	Victim.FeedHeartbeatVoice = Victim.SubmitFeedCue(
		TEXT("heartbeat"), /*bLooping*/ true, /*bHeartbeat*/ true);
}

void FElysiumCombatCharacter::StopFeedLoopAudio()
{
	IElysiumAudio* Audio = World ? World->Audio() : nullptr;
	if (Audio)
	{
		if (FeedLoopVoice.IsValid())
		{
			Audio->StopVoice(FeedLoopVoice, 0.0f);
		}
		if (FeedHeartbeatVoice.IsValid())
		{
			Audio->StopVoice(FeedHeartbeatVoice, 0.0f);
		}
	}
	FeedLoopVoice = FElysiumVoiceHandle::Invalid();
	FeedHeartbeatVoice = FElysiumVoiceHandle::Invalid();
}

void FElysiumCombatCharacter::PlayFeedEndAudio(FElysiumCombatCharacter* Victim)
{
	StopFeedLoopAudio();
	if (Victim)
	{
		Victim->StopFeedLoopAudio();
	}
	SubmitFeedCue(TEXT("end"), /*bLooping*/ false);
	if (Victim)
	{
		Victim->SubmitFeedCue(TEXT("end"), /*bLooping*/ false);
	}
}

// --- The authoritative transaction ---------------------------------------------------------------

void FElysiumCombatCharacter::OnFeedAnimEvent(int32 EventId)
{
	switch (EventId)
	{
	case ElysiumFeed::EventFeedBegin:
		if (FElysiumCombatCharacter* Victim = ResolveFeedPeer())
		{
			FeedBegin(*Victim);
		}
		break;
	case ElysiumFeed::EventFeedTeardown:
		CompleteFeedTransaction(/*bKeepReleaseTail*/ true);
		break;
	case ElysiumFeed::EventFeedEmitter:
		// Only the attacker clips author 5116. It starts an independent one-shot on every loop
		// occurrence: no blood, health or transaction timing, and no handle for teardown to stop.
		if (!FeedState.bVictim)
		{
			USkeletalMeshComponent* SkeletalBody = GetSkeletalBody();
			if (!SkeletalBody)
			{
				break;   // supported headless path
			}
			IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
			if (!Embodiment)
			{
				UE_LOG(LogElysiumFeed, Warning,
					TEXT("%s cannot emit force_feeding_emitter: no embodiment service"),
					*DebugString());
				break;
			}
			if (!Embodiment->PlayAttachedEffect(
				SkeletalBody, TEXT("force_feeding_emitter"), FName(TEXT("mouth"))))
			{
				// The embodiment owns and diagnoses the concrete missing socket, generated asset or
				// spawn failure. The effect is presentation-only, so the transaction keeps running.
			}
		}
		break;
	default:
		break;
	}
}

bool FElysiumCombatCharacter::FeedBegin(FElysiumCombatCharacter& Victim)
{
	if (Victim.IsInert() || &Victim == this)
	{
		return false;   // refuses a null / invalid target
	}
	if (FeedState.IsTransacting())
	{
		return false;   // refuses a second active feed
	}
	if (FeedState.Peer != Victim.Handle)
	{
		return false;   // refuses an attacker that is not paired with this victim
	}

	const double Now = World ? World->NowSeconds() : 0.0;
	// Let B be the victim's current BloodPool integer at THIS instant. It seeds the whole cadence,
	// so it is read once and never re-read.
	const int32 B = Victim.BloodPoolValue();

	FeedState.BloodStolen = 0;
	FeedState.Target = Victim.Handle;
	FeedState.StartTime = static_cast<float>(Now);
	FeedState.Interval = ElysiumFeed::InitialInterval(B);
	FeedState.NextPulse = static_cast<float>(Now) + FeedState.Interval;

	// The victim's feed-begin callback. It fires the victim's own `OnFedUponBegin` with the FEEDER
	// as activator and the victim as caller — the identity the tutorial's maker wires resolve
	// against, and the one this runtime picks. OPEN: retail's exact caller/activator identity for
	// the feed callbacks needs the controlled tutorial trace (`feeding.md` § "Open verification
	// gaps"). Chosen this way because it matches every other kind-2 producer on the chain —
	// `OnDeath` fires from the entity that died, with the killer as activator.
	Victim.FireOutput(GOnFedUponBegin, Handle);
	PlayFeedLoopAudio(Victim);

	UE_LOG(LogElysiumFeed, Display,
		TEXT("INFO - Feed started: %s (blood=%d, next=%.2fs)"),
		*Victim.DebugString(), B, FeedState.Interval);
	return true;
}

int32 FElysiumCombatCharacter::FeedHealAmount() const
{
	// OPEN — `feeding.md` records only that the amount is "victim/rules-derived" and the retail
	// formula behind `Feed`'s `HealthHeal()` call is not recovered. The closest RECOVERED rule is
	// `VampHeal_Info.VampFeedingHeal_Info`'s `BloodToHealthRatio` (10), whose block name is the
	// feeding heal itself: one blood point's worth of healing per transferred point. Closing the RE
	// is one edit here.
	int32 Ratio = 10;
	if (UElysiumRulebookSubsystem* Rules = FeedRulebook(*this))
	{
		Ratio = Rules->Rules().Int(TEXT("VampHeal_Info.VampFeedingHeal_Info"),
			TEXT("BloodToHealthRatio"), Ratio);
	}
	return FMath::Max(0, Ratio);
}

bool FElysiumCombatCharacter::Feed(double Now)
{
	if (!FeedState.IsTransacting())
	{
		return false;
	}
	// The release family has the fangs off the neck, so nothing transfers across it. The deadline
	// keeps its value — teardown clears it — but no pulse is performed.
	if (FeedState.Phase == EElysiumFeedPhase::Release)
	{
		return false;
	}
	if (static_cast<float>(Now) < FeedState.NextPulse)
	{
		return false;
	}
	FElysiumEntity* TargetEnt = World ? World->Resolve(FeedState.Target) : nullptr;
	FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
	if (!Victim)
	{
		FeedInterrupt();   // invalid feed target detected by the common grapple lifecycle
		return false;
	}

	// The baseline unit transaction, in retail's order.
	// 1. Try IncBloodPool() on the feeder.
	const int32 PlayerBefore = BloodPoolValue();
	const int32 VictimBefore = Victim->BloodPoolValue();
	AddBlood(+1);
	const ElysiumFeed::FPulseEffects Pulse =
		ElysiumFeed::PulseEffects(/*bBloodPoolIncremented*/ BloodPoolValue() > PlayerBefore);
	// 2. m_iBloodStolen counts ONLY when that increment succeeded.
	if (Pulse.bCountStolen)
	{
		++FeedState.BloodStolen;
	}
	// 3. The feed-heal is still evaluated in the normal branch when the feeder's pool is full — the
	//    successful-blood counter is not. Healing must not spend blood: the increment above already
	//    happened, so this is HealthHeal and not BloodHeal.
	if (Pulse.bHeal)
	{
		HealDamage(FeedHealAmount());
	}
	// 4. DecBloodPool(false) once on the victim. The immediate-resolution flag is false, so the
	//    death/incapacitation decision is deferred to teardown.
	if (Pulse.bDrainVictim)
	{
		Victim->AddBlood(-1);
	}
	const FString PulseLog = ElysiumFeed::PulseLogLine(
		PlayerBefore, BloodPoolValue(), VictimBefore, Victim->BloodPoolValue(), FeedState.BloodStolen);
	UE_LOG(LogElysiumFeed, Display, TEXT("%s"), *PulseLog);

	// `PLAYER_AGGRESSIVE_FEED`, from its real producer. The pulse is the transaction's own beat, so
	// the stimulus rides it rather than the grapple's beginning: a feed that is interrupted after
	// one pulse made one noise, and a long drain keeps making them.
	if (World != nullptr)
	{
		World->EmitGameSound(Origin, ElysiumGameSounds::Feed(),
			/*RadiusCm, table-resolved*/ -1.f, Handle,
			ElysiumStealth::HearingReductionCmFor(this));
	}

	// The accepted ordinary feed pulse is also a player-law producer (`docs/vtmb/feeding.md`).
	// For a player feeder it raises supernatural activity to 2 and criminal activity to 3, each
	// for an EXPLICIT two seconds — the one recovered case where a caller names its own duration
	// instead of passing the derive sentinel, which is why the pulse's wanted level dies two
	// seconds after the fangs come off rather than lasting a level's worth of seconds.
	//
	// It does not mutate Masquerade and does not spawn police: whether an NPC witnesses the feed
	// is the condition lane's question, and only the admitted incident reaches those consumers.
	if (FElysiumPlayer* PlayerFeeder = (World && World->FindPlayer() == this)
		? World->FindPlayer() : nullptr)
	{
		ElysiumLaw::SetSupernaturalLevel(*PlayerFeeder, 2, ElysiumLaw::FeedActivitySeconds);
		ElysiumLaw::SetCriminalLevel(*PlayerFeeder, 3, ElysiumLaw::FeedActivitySeconds);
		// The pulse also opens the victim NPC's criminal and supernatural observation windows for
		// three seconds (`docs/vtmb/feeding.md`), so that NPC's ordinary condition-gathering pass
		// can compare the new player act counts with its authored `pl_*` thresholds.
		//
		// On the VICTIM, and only when the victim is an ordinary NPC: the windows are per-NPC
		// state and the recovered caller names the fed-upon character.
		if (FElysiumNpc* VictimNpc = Victim->AsNpc())
		{
			ElysiumNpcWitness::OpenFeedWindows(*VictimNpc, Now);
		}
	}

	// The accelerating cadence, and AT MOST ONE pulse per update — never a catch-up loop.
	FeedState.Interval = ElysiumFeed::NextInterval(FeedState.Interval);
	FeedState.NextPulse = static_cast<float>(Now) + FeedState.Interval;
	return true;
}

void FElysiumCombatCharacter::FeedInterrupt()
{
	// Interrupted-feed law write (`docs/vtmb/feeding.md`): the path opens the same victim windows
	// and raises only criminal activity 1 for two seconds — one criminal level, no supernatural
	// at all, and the same explicit two seconds the pulse uses.
	//
	// Guarded on a LIVE transaction and on the feeder half: `CompleteFeedTransaction` is
	// idempotent and is also reached on the victim, so without this guard a repeated teardown
	// would count a second incident for an interruption that already happened.
	if (FeedState.IsPaired() && !FeedState.bVictim && !FeedState.bInterrupting)
	{
		if (FElysiumPlayer* PlayerFeeder = (World && World->FindPlayer() == this)
			? World->FindPlayer() : nullptr)
		{
			ElysiumLaw::SetCriminalLevel(*PlayerFeeder, 1, ElysiumLaw::FeedActivitySeconds);
			// Both channels again, not just the criminal one the activity write raises: the
			// recovered sentence says "the same victim windows", and the windows are an
			// observation grant rather than a mirror of what was raised.
			FElysiumCombatCharacter* Peer = ResolveFeedPeer();
			FElysiumNpc* VictimNpc = Peer ? Peer->AsNpc() : nullptr;
			if (VictimNpc != nullptr)
			{
				ElysiumNpcWitness::OpenFeedWindows(*VictimNpc,
					World ? World->NowSeconds() : 0.0);
			}
		}
	}
	CompleteFeedTransaction(/*bKeepReleaseTail*/ false);
}

void FElysiumCombatCharacter::CompleteFeedTransaction(bool bKeepReleaseTail)
{
	if (FeedState.bInterrupting)
	{
		return;   // +0x14a9, the re-entry guard
	}
	if (!FeedState.IsPaired())
	{
		return;   // idempotent: a second teardown performs nothing and fires nothing
	}
	FeedState.bInterrupting = true;
	const bool bManualStopRequested = !FeedState.bContinuation;
	FElysiumCombatCharacter* PairedVictim = FeedState.bVictim ? this : ResolveFeedPeer();
	PlayFeedEndAudio(PairedVictim);
	const float ReleaseEventDeadline = FeedState.PhaseDeadline;
	const ElysiumFeed::EPartnerHeight Height =
		static_cast<ElysiumFeed::EPartnerHeight>(FeedVictimHeightCell());

	// Clear the continuation latch. The cant-break and frenzy-grapple latches retail also clears
	// here belong to the grapple router and frenzy, which this ordinary-feed path does not build;
	// they are named rather than faked.
	FeedState.bContinuation = false;

	FElysiumEntity* TargetEnt = World ? World->Resolve(FeedState.Target) : nullptr;
	FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
	bool bSurvivingReleaseTail = false;
	if (Victim)
	{
		// 1. feeder/victim loops and the victim heartbeat were stopped above.
		// 2. read the victim's remaining BloodPool.
		const int32 Remaining = Victim->BloodPoolValue();
		const bool bDepleted = Remaining < 1;
		// A normal event-4006 exit keeps both bodies claimed until the authored release clip finishes.
		// Damage, invalidation and depleted-victim outcomes still tear down immediately.
		bSurvivingReleaseTail = bKeepReleaseTail
			&& FeedState.Phase == EElysiumFeedPhase::Release && !bDepleted;
		// 4. the victim feed-end callback. Same activator/caller identity as FeedBegin: the feeder
		//    activates, the victim is the firing entity.
		//
		// The native death outcome completes after this callback. Our narrow outcome collapses that
		// lifecycle into immediate OnKilled(), so enqueue the callback first: equal-time FIFO then
		// keeps the later OnDeath consequence terminal. The tutorial authors both results on the
		// victim; reversing them would let its success assignment overwrite its death assignment.
		Victim->FireOutput(GOnFedUponEnd, Handle);
		if (bDepleted)
		{
			// 5. below one selects the native death/incapacitation outcome.
			// OPEN — the full mortal / Kindred / unkillable outcome matrix, its humanity and
			// Masquerade consequences and the frenzy interaction are unresolved (`feeding.md` §
			// "Open verification gaps"). Only death-vs-survive is taken here; `OnKilled` is the same
			// door the damage path uses, so `OnDeath` fires exactly once either way.
			Victim->OnKilled();
		}
		if (bDepleted)
		{
			UE_LOG(LogElysiumFeed, Display,
				TEXT("INFO - Feed ended: %s depleted and killed"), *Victim->DebugString());
		}
		else if (bManualStopRequested)
		{
			UE_LOG(LogElysiumFeed, Display, TEXT("INFO - Feed stopped: %s (blood=%d)"),
				*Victim->DebugString(), Remaining);
		}
		else
		{
			UE_LOG(LogElysiumFeed, Display, TEXT("INFO - Feed interrupted: %s"),
				*Victim->DebugString());
		}
	}
	else
	{
		UE_LOG(LogElysiumFeed, Display, TEXT("INFO - Feed interrupted: %s"),
			PairedVictim ? *PairedVictim->DebugString() : TEXT("target unavailable"));
	}

	// 6. clear the target handle and the current interval.
	FeedState.Target = FElysiumEntityHandle::Invalid();
	FeedState.Interval = 0.0f;
	FeedState.NextPulse = 0.0f;
	ReleaseFeedCamera();

	if (bSurvivingReleaseTail)
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		const float ReleaseSeconds = ElysiumFeed::PhaseSeconds(EElysiumFeedPhase::Release, Height);
		const float EventCycle = ElysiumFeed::EventCycle(EElysiumFeedPhase::Release, Height);
		FeedState.Phase = EElysiumFeedPhase::ReleaseTail;
		// Anchor the tail to the authored event deadline, not to the possibly late update that observed
		// it. A hitch after 4006 must consume pose time rather than extending the release animation.
		FeedState.PhaseDeadline = ReleaseEventDeadline
			+ ReleaseSeconds * FMath::Clamp(1.0f - EventCycle, 0.0f, 1.0f);
		if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
		{
			Peer->FeedState.Phase = EElysiumFeedPhase::ReleaseTail;
			Peer->FeedState.PhaseDeadline = FeedState.PhaseDeadline;
		}
		FeedState.bInterrupting = false;
		ScheduleFeedThink(Now);
		return;
	}

	EndFeedGrapple();
	FeedState.bInterrupting = false;
}

void FElysiumCombatCharacter::BreakFeed()
{
	if (!FeedState.IsPaired())
	{
		return;
	}
	if (FeedState.bVictim)
	{
		if (FElysiumCombatCharacter* Attacker = ResolveFeedPeer())
		{
			Attacker->FeedInterrupt();
			return;
		}
		// The attacker is gone: release this body on its own rather than staying frozen forever.
		EndFeedVictimRole();
		FeedState = FElysiumFeedState();
		return;
	}
	FeedInterrupt();
}

// --- The state machine ---------------------------------------------------------------------------

void FElysiumCombatCharacter::ScheduleFeedThink(double Now)
{
	if (FeedState.Phase == EElysiumFeedPhase::None)
	{
		return;
	}
	float Next = FeedState.PhaseDeadline;
	if (FeedState.IsTransacting())
	{
		Next = FMath::Min(Next, FeedState.NextPulse);
	}
	NextThink = FMath::Max(Next, static_cast<float>(Now));
}

bool FElysiumCombatCharacter::ShouldReleaseFeed() const
{
	// The gameplay cancel gesture is a second Feed press. It clears the recovered continuation latch
	// at +0x14a8; button-up is inert and does not call `FeedInterrupt` (`feeding.md` § "Command and
	// initial request"). A cleared latch, or a victim with nothing left to take, selects the release
	// family, and teardown still arrives through event 4006 rather than directly from input. This is
	// evaluated every update so a drained victim releases at once instead of being drained past empty
	// for the rest of a cycle.
	if (!FeedState.bContinuation)
	{
		return true;
	}
	const FElysiumEntity* TargetEnt = World ? World->Resolve(FeedState.Target) : nullptr;
	const FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
	return !Victim || Victim->BloodPoolValue() < 1;
}

void FElysiumCombatCharacter::EnterFeedRelease(double Now)
{
	FeedState.Phase = EElysiumFeedPhase::Release;
	const ElysiumFeed::EPartnerHeight Height =
		static_cast<ElysiumFeed::EPartnerHeight>(FeedVictimHeightCell());
	const float ReleaseDuration = PlayFeedPhaseClips(EElysiumFeedPhase::Release, Now);
	FeedState.PhaseDeadline = static_cast<float>(Now)
		+ ReleaseDuration * ElysiumFeed::EventCycle(EElysiumFeedPhase::Release, Height);
	if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
	{
		Peer->FeedState.Phase = EElysiumFeedPhase::Release;
		Peer->FeedState.PhaseDeadline = FeedState.PhaseDeadline;
	}
	ScheduleFeedThink(Now);
}

void FElysiumCombatCharacter::AdvanceFeedPhase(double Now)
{
	switch (FeedState.Phase)
	{
	case EElysiumFeedPhase::Engage:
		// The bite clip carries event 4007 at cycle 0.0, so the transaction opens the moment the
		// clip starts.
		FeedState.Phase = EElysiumFeedPhase::Bite;
		{
			const ElysiumFeed::EPartnerHeight Height =
				static_cast<ElysiumFeed::EPartnerHeight>(FeedVictimHeightCell());
			const float BiteDuration = PlayFeedPhaseClips(EElysiumFeedPhase::Bite, Now);
			FeedState.PhaseDeadline = static_cast<float>(Now)
				+ BiteDuration * (1.0f - ElysiumFeed::EventCycle(EElysiumFeedPhase::Bite, Height));
			if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
			{
				Peer->FeedState.Phase = EElysiumFeedPhase::Bite;
				Peer->FeedState.PhaseDeadline = FeedState.PhaseDeadline;
			}
		}
		OnFeedAnimEvent(ElysiumFeed::EventFeedBegin);
		if (!FeedState.IsTransacting())
		{
			// FeedBegin refused: no partial transaction is left standing.
			EndFeedGrapple();
		}
		break;

	case EElysiumFeedPhase::Bite:
		FeedState.Phase = EElysiumFeedPhase::Loop;
		FeedState.PhaseDeadline = static_cast<float>(Now)
			+ PlayFeedPhaseClips(EElysiumFeedPhase::Loop, Now);
		if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
		{
			Peer->FeedState.Phase = EElysiumFeedPhase::Loop;
			Peer->FeedState.PhaseDeadline = FeedState.PhaseDeadline;
		}
		OnFeedAnimEvent(ElysiumFeed::EventFeedEmitter);
		break;

	case EElysiumFeedPhase::Loop:
		// The loop repeats for as long as the release predicate stays false; `ShouldReleaseFeed`
		// owns that decision and `TickFeed` asks it every update, not only here.
		FeedState.PhaseDeadline = static_cast<float>(Now)
			+ ElysiumFeed::PhaseSeconds(EElysiumFeedPhase::Loop,
				static_cast<ElysiumFeed::EPartnerHeight>(FeedVictimHeightCell()));
		if (FElysiumCombatCharacter* Peer = ResolveFeedPeer())
		{
			Peer->FeedState.PhaseDeadline = FeedState.PhaseDeadline;
		}
		OnFeedAnimEvent(ElysiumFeed::EventFeedEmitter);
		break;

	case EElysiumFeedPhase::Release:
		// Event 4006 lands part-way through the release clip. It ends gameplay and camera ownership;
		// CompleteFeedTransaction retains the body pair for the remaining authored pose.
		OnFeedAnimEvent(ElysiumFeed::EventFeedTeardown);
		break;

	case EElysiumFeedPhase::ReleaseTail:
		EndFeedGrapple();
		break;

	default:
		break;
	}
}

bool FElysiumCombatCharacter::TickFeed(double Now)
{
	if (FeedState.Phase == EElysiumFeedPhase::None)
	{
		return false;
	}
	// The victim half is driven by its attacker; it never advances the pair itself.
	if (FeedState.bVictim)
	{
		if (!ResolveFeedPeer())
		{
			BreakFeed();
		}
		return FeedState.Phase != EElysiumFeedPhase::None;
	}
	EnsureFeedCamera();
	if (FeedState.Phase == EElysiumFeedPhase::Loop && !FeedLoopVoice.IsValid()
		&& !bFeedAudioAcquireFailed)
	{
		if (FElysiumCombatCharacter* Victim = ResolveFeedPeer())
		{
			PlayFeedLoopAudio(*Victim);
		}
	}
	// The pulse comes first and independently of the clip cycle: a loop may repeat without a pulse
	// on its boundary, and a pulse may land part-way through a loop.
	Feed(Now);
	if (FeedState.Phase == EElysiumFeedPhase::Loop && ShouldReleaseFeed())
	{
		EnterFeedRelease(Now);
	}
	else if (FeedState.Phase != EElysiumFeedPhase::None
		&& static_cast<float>(Now) >= FeedState.PhaseDeadline)
	{
		AdvanceFeedPhase(Now);
	}
	ScheduleFeedThink(Now);
	return FeedState.Phase != EElysiumFeedPhase::None;
}
