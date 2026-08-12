// B6 — ordinary player-on-humanoid feeding: the command's acceptance, the minimal paired action,
// the authoritative blood transaction on the substrate clock, and the `OnFedUponBegin` /
// `OnFedUponEnd` outputs that reach authored map wires.
//
// `docs/vtmb/feeding.md` is the specification and owns every fact below; this file is the
// implementation of its "Recreation contract" items 1-9 minus the ones stated as out of scope
// there (seductive/rat/zombie modes, prayer, and all presentation).
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

#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFeed, Log, All);

// ================================================================================================
// The pure rules
// ================================================================================================

namespace ElysiumFeed
{
	bool OpposedAccepts(int32 AttackerBrawlRating, const FElysiumRollResult& VictimHackingRoll)
	{
		return AttackerBrawlRating > FMath::Max(VictimHackingRoll.Net, 0);
	}

	const TCHAR* PhaseClipLabel(EElysiumFeedPhase Phase, bool bAttacker)
	{
		switch (Phase)
		{
		case EElysiumFeedPhase::Engage:
			return bAttacker ? TEXT("feeding_attacker_shortvictim_front_engage")
			                 : TEXT("feeding_victim_shortattacker_front_engage");
		case EElysiumFeedPhase::Bite:
			return bAttacker ? TEXT("feeding_attacker_shortvictim_front_bite")
			                 : TEXT("feeding_victim_shortattacker_front_bite");
		case EElysiumFeedPhase::Loop:
			return bAttacker ? TEXT("feeding_attacker_shortvictim_front_feed_loop")
			                 : TEXT("feeding_victim_shortattacker_front_feed_loop");
		case EElysiumFeedPhase::Release:
			return bAttacker ? TEXT("feeding_attacker_shortvictim_front_feed_release")
			                 : TEXT("feeding_victim_shortattacker_front_feed_release");
		default:
			return TEXT("");
		}
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

// ================================================================================================
// The transaction, on the character chain
// ================================================================================================

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
	// OPEN — this runtime has no ACT_* state machine to ask (the NPC mind is
	// `gameplay-systems-architecture.md` §5.5.4 and lands after B6), so the stand-in is the
	// character's current disposition name, which is the one emotional-state value the substrate
	// does carry and which selects the standing set those activities belong to. When the mind
	// lands, this reads the activity instead and the four names move with it.
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
		UE_LOG(LogElysiumFeed, Log, TEXT("%s feed on %s %s"), *DebugString(),
			*Victim.DebugString(), LexToString(Verdict));
		return Verdict;
	}

	const double Now = World ? World->NowSeconds() : 0.0;
	StartFeedPair(Victim, Now);
	UE_LOG(LogElysiumFeed, Log, TEXT("%s feed on %s %s"), *DebugString(), *Victim.DebugString(),
		LexToString(Verdict));
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
	// One owner aligns the two actors. The attacker faces the victim; the victim's motor is frozen
	// through the existing `SetBodyFrozen` seam — the same switch a `scripted_sequence` beat borrows,
	// and deliberately not `SetEnabled`, which would also take the body off screen.
	const FVector ToVictim = Victim.Origin - Origin;
	if (!ToVictim.IsNearlyZero())
	{
		// `Angles.Y` is Source's yaw, which this runtime carries negated relative to Unreal's
		// (every motor call is `-Angles.Y`), so the write is negated once, here.
		FVector Facing = Angles;
		Facing.Y = -static_cast<float>(ToVictim.Rotation().Yaw);
		SetRuntimeAngles(Facing);
	}

	FeedState = FElysiumFeedState();
	FeedState.Peer = Victim.Handle;
	FeedState.bVictim = false;
	FeedState.bContinuation = true;   // +0x14a8, set on acceptance
	FeedState.Phase = EElysiumFeedPhase::Engage;
	FeedState.PhaseDeadline = static_cast<float>(Now) + ElysiumFeed::EngageSeconds;

	Victim.FeedState = FElysiumFeedState();
	Victim.FeedState.Peer = Handle;
	Victim.FeedState.bVictim = true;
	Victim.FeedState.Phase = EElysiumFeedPhase::Engage;
	Victim.SetBodyFrozen(true);
	Victim.FeedState.bFrozenByFeed = true;

	PlayFeedPhaseClips(EElysiumFeedPhase::Engage, Now);
	ScheduleFeedThink(Now);
}

void FElysiumCombatCharacter::EndFeedGrapple()
{
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
	}
}

void FElysiumCombatCharacter::PlayFeedPhaseClips(EElysiumFeedPhase Phase, double Now)
{
	const bool bLoop = (Phase == EElysiumFeedPhase::Loop);
	auto Play = [this, Phase, bLoop](FElysiumCombatCharacter& Who, bool bAttacker)
	{
		const FString Label = ElysiumFeed::PhaseClipLabel(Phase, bAttacker);
		if (Label.IsEmpty() || !Who.GetSkeletalBody())
		{
			return;   // no body: the transaction runs anyway (K10)
		}
		if (!Who.PlayAnimClip(Label, bLoop))
		{
			// Best effort by contract: a model whose bank does not answer this cell must not break
			// the transaction. Retail would end the pair here; that would make headless correctness
			// depend on the export, which K10 forbids.
			UE_LOG(LogElysiumFeed, Log, TEXT("%s has no feed clip '%s'"), *Who.DebugString(), *Label);
		}
	};
	Play(*this, /*bAttacker*/ true);
	if (FElysiumCombatCharacter* Victim = ResolveFeedPeer())
	{
		Play(*Victim, /*bAttacker*/ false);
		// Re-assert the hold. Bodies are disposable presentation and are rebuilt from the def, so a
		// restore mid-feed comes back with a live, unfrozen motor under a victim whose logical state
		// says it is held; the motor's switch is idempotent, so the cheapest correct place to close
		// that is wherever the pair next touches both bodies.
		if (Victim->FeedState.bFrozenByFeed)
		{
			Victim->SetBodyFrozen(true);
		}
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
		FeedInterrupt();
		break;
	case ElysiumFeed::EventFeedEmitter:
		// 5116 starts the mouth-attached `force_feeding_emitter` effect. Presentation only: no
		// blood, no health, no timing. Left as a named no-op so the id is accounted for.
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

	UE_LOG(LogElysiumFeed, Log,
		TEXT("%s FeedBegin on %s: blood %d, first pulse in %.2fs"),
		*DebugString(), *Victim.DebugString(), B, FeedState.Interval);
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
	const int32 Before = BloodPoolValue();
	AddBlood(+1);
	const ElysiumFeed::FPulseEffects Pulse =
		ElysiumFeed::PulseEffects(/*bBloodPoolIncremented*/ BloodPoolValue() > Before);
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

	// The accelerating cadence, and AT MOST ONE pulse per update — never a catch-up loop.
	FeedState.Interval = ElysiumFeed::NextInterval(FeedState.Interval);
	FeedState.NextPulse = static_cast<float>(Now) + FeedState.Interval;
	return true;
}

void FElysiumCombatCharacter::FeedInterrupt()
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

	// Clear the continuation latch. The cant-break and frenzy-grapple latches retail also clears
	// here belong to systems B6 does not build (the grapple router and frenzy); they are named
	// rather than faked. Feed sound and the feed camera are presentation and stop nowhere yet.
	FeedState.bContinuation = false;

	FElysiumEntity* TargetEnt = World ? World->Resolve(FeedState.Target) : nullptr;
	FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
	if (Victim)
	{
		// 1. stop feeder/victim loop and heartbeat presentation — presentation, not built.
		// 2. read the victim's remaining BloodPool.
		const int32 Remaining = Victim->BloodPoolValue();
		if (Remaining < 1)
		{
			// 3. below one selects the native death/incapacitation outcome.
			// OPEN — the full mortal / Kindred / unkillable outcome matrix, its humanity and
			// Masquerade consequences and the frenzy interaction are unresolved (`feeding.md` §
			// "Open verification gaps"). Only death-vs-survive is taken here; `OnKilled` is the same
			// door the damage path uses, so `OnDeath` fires exactly once either way.
			Victim->OnKilled();
		}
		else
		{
			// 4. otherwise the victim returns to its non-depleted post-feed path.
			Victim->EndFeedVictimRole();
		}
		// 5. the victim feed-end callback. Same activator/caller identity as FeedBegin: the feeder
		//    activates, the victim is the firing entity.
		Victim->FireOutput(GOnFedUponEnd, Handle);
		UE_LOG(LogElysiumFeed, Log, TEXT("%s FeedInterrupt on %s: %d stolen, %d left"),
			*DebugString(), *Victim->DebugString(), FeedState.BloodStolen, Remaining);
	}

	// 6. clear the target handle and the current interval.
	FeedState.Target = FElysiumEntityHandle::Invalid();
	FeedState.Interval = 0.0f;
	FeedState.NextPulse = 0.0f;

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
	PlayFeedPhaseClips(EElysiumFeedPhase::Release, Now);
	FeedState.PhaseDeadline = static_cast<float>(Now)
		+ ElysiumFeed::ReleaseSeconds * ElysiumFeed::ReleaseEventCycle;
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
		PlayFeedPhaseClips(EElysiumFeedPhase::Bite, Now);
		FeedState.PhaseDeadline = static_cast<float>(Now)
			+ ElysiumFeed::BiteSeconds * (1.0f - ElysiumFeed::BiteEventCycle);
		OnFeedAnimEvent(ElysiumFeed::EventFeedBegin);
		if (!FeedState.IsTransacting())
		{
			// FeedBegin refused: no partial transaction is left standing.
			EndFeedGrapple();
		}
		break;

	case EElysiumFeedPhase::Bite:
		FeedState.Phase = EElysiumFeedPhase::Loop;
		PlayFeedPhaseClips(EElysiumFeedPhase::Loop, Now);
		FeedState.PhaseDeadline = static_cast<float>(Now) + ElysiumFeed::LoopSeconds;
		break;

	case EElysiumFeedPhase::Loop:
		// The loop repeats for as long as the release predicate stays false; `ShouldReleaseFeed`
		// owns that decision and `TickFeed` asks it every update, not only here.
		FeedState.PhaseDeadline = static_cast<float>(Now) + ElysiumFeed::LoopSeconds;
		break;

	case EElysiumFeedPhase::Release:
		// Event 4006 lands part-way through the release clip; the rest of that clip is presentation
		// and nothing waits on it.
		OnFeedAnimEvent(ElysiumFeed::EventFeedTeardown);
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
