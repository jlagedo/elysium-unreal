// NPC presence and the first native-Unreal locomotion slice: the `npc_*` character leaf.
//
// The leaf here is the **dialogue half only**. Its place in VtMB's chain is CAI_BaseNPC under
// CBaseCombatCharacter under CBaseAnimating (`ElysiumPlayer.h`), and everything those two own
// — the sheet and its 25 inputs, the WillTalk latch, `default_disposition`, the skeletal body,
// playing a clip on it, following SetOrigin/SetModel, gating it on dormancy — arrives through the
// chain, shared with the player. `elysium.NpcBodies` is the one thing that stays here: it is this
// class's A/B, not the animating node's.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumWeaponClasses.h"

#include "HAL/IConsoleManager.h"

// A/B toggle for the B3 NPC skeletal bodies (mirrors elysium.BrushBodies). Read in the leaf's Spawn,
// so it takes effect on the next map load: 1 stands the models, 0 leaves the NPCs bodiless records
// (their I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarNpcBodies(
	TEXT("elysium.NpcBodies"),
	1,
	TEXT("Stand NPC glTF skeletal bodies at their origins at map load (1, default) or skip them (0)."),
	ECVF_Default);

bool FElysiumNpc::ResistsFeeding() const
{
	// `FastFood` is inherited through the resolved NPC template. It is the authored data switch
	// used by tutorial and ambient victims that must accept without the Brawl/Hacking check.
	return ElysiumFeed::ResistsByAuthoredPolicy(
		bFastFood, !FElysiumCombatCharacter::ResistsFeeding());
}

void FElysiumNpc::ApplyResolvedTemplate(const FElysiumClanTemplate& Resolved,
	const FElysiumStatTable* Table)
{
	bFastFood = Resolved.GeneralInt(TEXT("FastFood")) != 0;
	bHasKindredTemplate = true;
	bKindredTemplate = Resolved.GeneralInt(TEXT("Kindred")) != 0;

	// The authored damage filters, kept as the template states them. Nothing multiplies them
	// yet — the resolver only accumulates them onto the descriptor (`ElysiumDamage::Apply`
	// step 9 is an open join) — so an absent key stays absent rather than defaulting to 1.
	static const TCHAR* const FilterKeys[] =
	{
		TEXT("DamageFilterBashing"), TEXT("DamageFilterLethal"),
		TEXT("DamageFilterAggravated"), TEXT("DamageFilterFlame"),
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(FilterKeys); ++i)
	{
		const FString Authored = Resolved.GeneralStr(FilterKeys[i]);
		bHasDamageFilter[i] = !Authored.IsEmpty();
		DamageFilters[i] = bHasDamageFilter[i] ? FCString::Atof(*Authored) : 0.f;
	}
	Sheet.ApplyTemplate(Resolved, Table);
}

bool FElysiumNpc::IsKindred() const
{
	return bHasKindredTemplate ? bKindredTemplate : FElysiumCombatCharacter::IsKindred();
}

bool FElysiumNpc::GetTemplateDamageFilter(EElysiumDmgFamily Family, bool bFlame,
	float& OutFilter) const
{
	const int32 Index = bFlame ? 3 : static_cast<int32>(Family);
	if (Index < 0 || Index >= UE_ARRAY_COUNT(bHasDamageFilter) || !bHasDamageFilter[Index])
	{
		return false;
	}
	OutFilter = DamageFilters[Index];
	return true;
}

void FElysiumNpc::OnDamageCommitted(const FElysiumDmg& Dmg)
{
	const double Now = World ? World->NowSeconds() : 0.0;
	Senses.Memory.LastDamageAttacker = Dmg.Source;
	Senses.Memory.LastDamageTime = Now;
	Senses.Memory.LastDamageAmount = Dmg.CommittedDamage();
	// Step 5 of the recovered damage-to-AI transaction: the one-second accumulation window
	// `REPEATED_DAMAGE` is derived from. The window arithmetic is the conditions layer's rule.
	ElysiumNpcCond::AccumulateDamage(Senses.Memory, Dmg.CommittedDamage(), Now);
}

void FElysiumNpc::InputUseInteresting(const FElysiumInputArgs& Args)
{
	bUseInteresting = Args.Param.ToInt() != 0;
	if (!bUseInteresting)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	else if (!bPatrolActive)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
}

void FElysiumNpc::InputTeleportToEntity(const FElysiumInputArgs& Args)
{
	// CAI_BaseNPCTroika's FIELD_EHANDLE input performs its string-to-handle conversion when
	// the input is delivered: first live name match in entity-list order, including the retail
	// trailing-'*' prefix rule. It does not cache or retry a destination.
	if (!World)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s cannot resolve TeleportToEntity without an entity world"), *DebugString());
		return;
	}
	const FElysiumEntity* Destination = Args.Param.Type == EElysiumVariantType::Handle
		? World->Resolve(Args.Param.AsHandle)
		: World->FindByName(Args.Param.ToString());
	if (!Destination)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s TeleportToEntity destination '%s' resolved to no live entity"),
			*DebugString(), *Args.Param.ToString());
		return; // retail consumes the input as a no-op; there is no retry
	}

	// SetAbsOrigin, then SetAbsAngles, followed by the concrete NPC's due-think hook. The
	// authoritative writer crosses the existing embodiment seam without adding placement,
	// velocity, route, schedule, enemy, animation, or safe-location policy.
	SetRuntimeTransform(Destination->Origin, Destination->Angles);
	NextThink = static_cast<float>(World->NowSeconds());
}

void FElysiumNpc::SeedPlayerRelationship()
{
	const FElysiumEntityHandle Player = World ? World->PlayerHandle()
		: FElysiumEntityHandle::Invalid();
	if (!Player.IsSet() || PlayerReaction.IsEmpty() || Relationships.HasEntity(Player))
	{
		return;
	}
	TArray<FString> Tokens;
	PlayerReaction.ParseIntoArrayWS(Tokens);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	if (Tokens.Num() != 2 || !ElysiumRelationships::Parse(Tokens[0], Value))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s invalid player_reaction '%s'"),
			*DebugString(), *PlayerReaction);
		return;
	}
	Relationships.SetEntity(Player, Value, FCString::Atoi(*Tokens[1]));
}

void FElysiumNpc::InputSetRelationship(const FElysiumInputArgs& Args)
{
	TArray<FString> Tokens;
	Args.Param.ToString().ParseIntoArrayWS(Tokens);
	if (Tokens.IsEmpty() || Tokens.Num() % 3 != 0)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s SetRelationship expects target D_* priority triples, got '%s'"),
			*DebugString(), *Args.Param.ToString());
		return;
	}

	for (int32 Index = 0; Index < Tokens.Num(); Index += 3)
	{
		const FString& TargetSpec = Tokens[Index];
		EElysiumRelationship Value = EElysiumRelationship::Neutral;
		if (!ElysiumRelationships::Parse(Tokens[Index + 1], Value))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s SetRelationship unknown value '%s'"),
				*DebugString(), *Tokens[Index + 1]);
			continue;
		}
		const int32 Priority = FCString::Atoi(*Tokens[Index + 2]);
		bool bMatchedEntity = false;
		if (World && TargetSpec.Equals(TEXT("player"), ESearchCase::IgnoreCase))
		{
			bMatchedEntity = Relationships.SetEntity(World->PlayerHandle(), Value, Priority);
		}
		else if (World)
		{
			const bool bWildcard = TargetSpec.Contains(TEXT("*")) || TargetSpec.Contains(TEXT("?"));
			for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
			{
				if (!Candidate || Candidate->IsDead() || Candidate->TargetName.IsEmpty())
				{
					continue;
				}
				const bool bMatches = bWildcard
					? Candidate->TargetName.MatchesWildcard(TargetSpec, ESearchCase::IgnoreCase)
					: Candidate->TargetName.Equals(TargetSpec, ESearchCase::IgnoreCase);
				if (bMatches)
				{
					Relationships.SetEntity(Candidate->Handle, Value, Priority);
					bMatchedEntity = true;
				}
			}
		}
		if (!bMatchedEntity && !TargetSpec.Contains(TEXT("*")) && !TargetSpec.Contains(TEXT("?")))
		{
			Relationships.SetClass(TargetSpec, Value, Priority);
		}
	}
	Mind.RecordExternal(FString::Printf(TEXT("relationship table %d entity / %d class; combat consumer pending"),
		Relationships.NumEntityRules(), Relationships.NumClassRules()));
}

void FElysiumNpc::InputSetupPatrolType(const FElysiumInputArgs& Args)
{
	PatrolType = Args.Param.ToString();
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s SetupPatrolType(%s)"),
		*DebugString(), *PatrolType);
}

void FElysiumNpc::InputFollowPatrolPath(const FElysiumInputArgs& Args)
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	PatrolPath = Args.Param.ToString();
	PatrolIndex = 0;
	bPatrolActive = ResolvePatrolPoints();
	bMoveIssued = false;
	if (bPatrolActive && Mind.IsAdmitted() && Mind.Owner() == EElysiumBodyOwner::None
		&& !PatrolOwner.IsSet())
	{
		bPatrolActive = Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
			PatrolOwner, TEXT("FollowPatrolPath"));
	}
	if (bPatrolActive)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s FollowPatrolPath: %d/%d points (%s)"),
		*DebugString(), PatrolPoints.Num(), PatrolNames.Num(),
		bPatrolActive ? TEXT("armed") : TEXT("not armed"));
}

void FElysiumNpc::InputClearPatrolPath(const FElysiumInputArgs&)
{
	bPatrolActive = false;
	bMoveIssued = false;
	PatrolIndex = 0;
	PatrolPath.Reset();
	PatrolNames.Reset();
	PatrolPoints.Reset();
	if (PatrolOwner.IsSet())
	{
		if (Mind.Owner() == EElysiumBodyOwner::Patrol)
		{
			Mind.Release(PatrolOwner, TEXT("ClearPatrolPath"));
		}
		else
		{
			Mind.ForgetSuspended(EElysiumBodyOwner::Patrol, TEXT("ClearPatrolPath"));
		}
		PatrolOwner.Reset();
	}
	// A cutscene beat outranks the route inputs: clearing the route while a script owns the
	// body drops the route only, and leaves the beat's travel and its cycle running.
	if (ScriptPhase == EScriptPhase::None)
	{
		if (Motor)
		{
			Motor->Stop();
		}
		ResetAnimToIdle();
	}
	if (bUseInteresting)
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
}

const FElysiumEntity* FElysiumNpc::FindPatrolPoint(const FString& Name) const
{
	if (const FElysiumEntity* Named = World->FindByName(Name))
	{
		return Named;
	}
	for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
	{
		if (!Ent.IsValid() || Ent->Def == nullptr
			|| !Ent->Def->Classname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Ent->Def->Keys.FindRef(TEXT("Group")).Equals(Name, ESearchCase::IgnoreCase))
		{
			return Ent.Get();
		}
	}
	return nullptr;
}

bool FElysiumNpc::ResolvePatrolPoints()
{
	PatrolNames.Reset();
	PatrolPoints.Reset();
	PatrolPath.ParseIntoArrayWS(PatrolNames);
	if (!World)
	{
		return false;
	}
	for (const FString& Name : PatrolNames)
	{
		if (const FElysiumEntity* Point = FindPatrolPoint(Name))
		{
			PatrolPoints.Add(Point->Origin);
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s patrol point '%s' does not resolve"),
				*DebugString(), *Name);
		}
	}
	PatrolIndex = PatrolPoints.IsEmpty() ? 0 : PatrolIndex % PatrolPoints.Num();
	return !PatrolPoints.IsEmpty();
}

bool FElysiumNpc::IssuePatrolMove()
{
	if (!Motor || !bPatrolActive || PatrolPoints.IsEmpty())
	{
		return false;
	}
	PatrolIndex = FMath::Clamp(PatrolIndex, 0, PatrolPoints.Num() - 1);
	bMoveIssued = Motor->MoveTo(PatrolPoints[PatrolIndex], /*AcceptanceRadiusCm=*/20.0f,
		ElysiumNpcGait::WalkSpeed);
	if (bMoveIssued && !bWalkingAnimation)
	{
		bWalkingAnimation = StartWalkingAnimation();
	}
	return bMoveIssued;
}

bool FElysiumNpc::StartWalkingAnimation(bool bRunning)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment && Visual && Embodiment->PlayNpcActivity(Visual, ModelStem(),
		bRunning ? TEXT("ACT_RUN") : TEXT("ACT_WALK"),
		FMath::Max(0, Handle.Index), /*bLoop=*/true, nullptr))
	{
		return true;
	}
	// A few early manifests only carry the retail label. Keep them mobile while the animation
	// catalog remains strict for every model that does expose ACT_WALK.
	return PlayAnimClip(bRunning ? TEXT("run") : TEXT("walk"), /*bLoop=*/true);
}

bool FElysiumNpc::AcquireSequenceBody(const TCHAR* Reason)
{
	// A pushed scripted order is DROPPED rather than parked, for the same reason dialogue drops it:
	// the arbiter's one parked slot belongs to the patrol route, and `aiscripted_schedule` is a
	// one-shot push with no resume. A beat that takes this body ends whatever a director had asked
	// for, which is also the recovered precedence — a sequence claims the body outright.
	EndScriptedSchedule(Reason);
	// The combat claim leaves the same way, and for a load-bearing reason rather than tidiness: the
	// arbiter has ONE parked slot, and a schedule that took the body off a patrol route is already
	// holding the route in it. Parking the schedule on top would discard the route and strand the
	// mind owning a claim whose token this leaf has retired. Releasing first restores the route, and
	// the claim below then parks the thing that is actually resumable.
	ReleaseScheduleBody(Reason);
	return Mind.Acquire(EElysiumBodyOwner::Sequence, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
		SequenceOwner, Reason);
}

void FElysiumNpc::ReleaseSequenceBody(const TCHAR* Reason)
{
	if (!SequenceOwner.IsSet())
	{
		return;
	}
	Mind.Release(SequenceOwner, Reason);
	SequenceOwner.Reset();
	if (Mind.Owner() == EElysiumBodyOwner::Patrol)
	{
		PatrolOwner = Mind.CurrentToken();
	}
}

bool FElysiumNpc::ClaimScriptMove()
{
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	if (!AcquireSequenceBody(TEXT("BeginScriptMove")))
	{
		return false;
	}
	bMoveIssued = false;
	bWalkingAnimation = false;
	return true;
}

void FElysiumNpc::ReleaseScriptMove(const TCHAR* Reason)
{
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (bScriptBodyHeld)
	{
		// The beat outlives its travel: `m_iszPlay` and the post-idle still play on this body,
		// so arrival hands the motor back without giving up the claim under them.
		return;
	}
	ReleaseSequenceBody(Reason);
}

bool FElysiumNpc::ClaimScriptBody(const TCHAR* Reason)
{
	bScriptBodyRequested = true;
	if (bScriptBodyHeld)
	{
		return true;
	}
	if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
	{
		// An interesting-place visit is this NPC's own executor; the beat takes the body off it.
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	bScriptBodyHeld = AcquireSequenceBody(Reason);
	// This leaf owns the arbiter, so it owns the one report of a claim it could not grant. The
	// two outcomes are not the same event: an unadmitted mind is an ordinary race this NPC
	// resolves itself on its next think, while an admitted mind that still refuses means another
	// owner holds a body a beat is entitled to.
	if (!bScriptBodyHeld && !Mind.IsAdmitted())
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("%s defers a scripted beat's body claim until admission has run"),
			*DebugString());
	}
	else if (!bScriptBodyHeld)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted beat the body: %s owns it. The beat runs on its queue "
				 "lock and the claim is retried when the arbitration allows it"),
			*DebugString(), LexToString(Mind.Owner()));
	}
	return bScriptBodyHeld;
}

void FElysiumNpc::ReleaseScriptBody(const TCHAR* Reason)
{
	bScriptBodyRequested = false;
	if (!bScriptBodyHeld)
	{
		return;
	}
	bScriptBodyHeld = false;
	if (bScriptMoveClaimed)
	{
		// Cancelled mid-travel: the motor is still on the same token and EndScriptMove, which
		// the beat runs next, is what gives it back.
		return;
	}
	ReleaseSequenceBody(Reason);
}

bool FElysiumNpc::IsFeedBusy() const
{
	return bInDialog || FElysiumCombatCharacter::IsFeedBusy();
}

void FElysiumNpc::Think()
{
	if (IsInert())
	{
		return;
	}
	// The activation barrier admits the mind on its first frozen-time think. Admission is a
	// no-op for body, motor and animation; executors may run only on a later think.
	if (Mind.Admit())
	{
		// Every admitted NPC gets a next think, not just the ones with an executor: a standing
		// character's stance machine is an executor too, and without this it would never run.
		NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.1);
		return;
	}
	// --- Cycle 6: the combat loadout -------------------------------------------------------------
	// The first ordinary think after admission, not `Spawn`: creating an item entity inside the
	// world's range-based spawn pass would invalidate the array being iterated, which is why
	// `FElysiumItemContainer` materialises its equip seeds from a think too.
	if (!bLoadoutResolved)
	{
		bLoadoutResolved = true;
		const ElysiumNpcLoadout::EResult Result = ElysiumNpcLoadout::Resolve(*this);
		Mind.RecordExternal(FString::Printf(TEXT("loadout: %s"),
			ElysiumNpcLoadout::ResultName(Result)));
	}
	// --- Cycle 7: a director that fired before this NPC's first think -----------------------------
	// The push was deferred whole (`BeginScriptedSchedule`), because admission establishes idle and
	// would have wiped a forced state applied ahead of it. Replaying it here is the first thing an
	// admitted NPC does, so the order is in force before any condition is gathered against it.
	if (ScriptedScheduleOrder.bPending && Mind.IsAdmitted())
	{
		const FElysiumScriptedScheduleOrder Pending = ScriptedScheduleOrder;
		ScriptedScheduleOrder.Reset();
		BeginScriptedSchedule(Pending, Pending.bHasForcedState, Pending.ForcedState);
	}
	// --- Cycle 4: condition gathering ------------------------------------------------------------
	// Senses run before any executor picks work, which is where the recovered pass puts them, and
	// are suppressed exactly where retail suppresses condition gathering: a scripted owner or an
	// in-flight scripted move is driving this body (`docs/vtmb/npc-ai-reverse-engineering.md`).
	// The inert/dead gate is the early return above.
	if (!ScriptOwner.IsSet() && ScriptPhase == EScriptPhase::None)
	{
		const double SenseNow = World ? World->NowSeconds() : 0.0;
		Senses.Tick(*this, SenseNow);
		// Cycle 5: the rest of the recovered decision pass, in `RunAI`'s own order — condition
		// gathering (which contains the enemy transaction), then ideal-state selection, then the
		// schedule work every executor below performs.
		ElysiumNpcEnemy::GatherConditions(*this, SenseNow);
		UpdateIdealState(SenseNow);
	}
	else
	{
		// Gathering is suppressed, so the previous pass's conditions are stale. Retail clears
		// transient conditions when a pass ends; a pass that never runs must not leave a schedule
		// interruptible by a stimulus nobody re-observed.
		Cognition.Conditions.Reset();
	}
	// B6 — a pair this NPC is part of owns the body outright: it advances the transaction from
	// the feeder's think and nothing else moves either actor while it runs.
	if (TickFeed(World ? World->NowSeconds() : 0.0))
	{
		return;
	}
	if (ScriptPhase != EScriptPhase::None)
	{
		// The owning beat advances the move; this think only watches for a beat that stopped
		// doing so (killed or hidden mid-travel) and releases the body rather than freezing it.
		const double Now = World ? World->NowSeconds() : 0.0;
		if (Now < ScriptWatchdogAt)
		{
			NextThink = static_cast<float>(ScriptWatchdogAt);
			return;
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s released an abandoned scripted move"),
			*DebugString());
		// Everything the beat holds leaves together, the queue lock included: a beat that stopped
		// advancing its own move will not run its teardown either, and half a claim would leave
		// this body suppressed and unowned for the rest of the map.
		ReleaseScriptBody(TEXT("abandoned scripted move"));
		EndScriptMove();
		ScriptOwner = FElysiumEntityHandle::Invalid();
		bScriptOwnerLocked = false;
		ResetAnimToIdle();
		return;
	}
	if (bInDialog)
	{
		// A per-line VCD owns the body while its sequence/gesture event is live. The ordinary
		// dialogue stance think must not replace that one-shot with a disposition idle.
		if (World && World->HasActiveDialogueBodyClip(Handle))
		{
			NextThink = static_cast<float>(World->NowSeconds() + 0.1);
			return;
		}
		// A character in conversation still runs its stance machine -- retail's Talking
		// threshold/chance pair exists precisely for this case. The selector settles it onto its
		// current idle rather than fidgeting through a line, so the reschedule is what keeps it
		// posed rather than what makes it move.
		ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
		return;
	}
	if (ScriptOwner.IsSet())
	{
		// A scripted owner — a `scripted_sequence` beat or a choreographed scene's cast — is
		// driving this body's pose. Script ownership suppresses the ordinary condition-gathering
		// path (`docs/vtmb/npc-ai-reverse-engineering.md`), so nothing below may select a
		// schedule whose idle would replace the clip the owner put on the body.
		const double Now = World ? World->NowSeconds() : 0.0;
		if (bScriptBodyRequested && !bScriptBodyHeld
			&& Mind.CanAcquire(EElysiumBodyOwner::Sequence))
		{
			// The beat asked before this NPC's admission think had run. Admission has happened
			// by now, so the claim it is entitled to lands here.
			bScriptBodyHeld = AcquireSequenceBody(TEXT("scripted beat claim after admission"));
		}
		NextThink = static_cast<float>(Now + 0.25);
		return;
	}
	// --- Cycle 7: schedule selection pre-empts an autonomous executor ---------------------------
	// A committed enemy or an authored director outranks this NPC's own patrol route and
	// interesting-place visit. Until this branch existed, `Think` reached the executor before
	// schedule selection ran at all, so a patrolling guard that acquired an enemy kept walking its
	// route and `SelectCombatSchedule` was never called for it.
	//
	// The change is deliberately in the ROUTING and not in either executor: the arbiter already
	// carries both hand-over shapes (patrol suspends and resumes, ambient owns a claimed place that
	// has to be given back), and the programs are the ones cycle 6 registered.
	const bool bScriptedPolicy = ScriptedScheduleOrder.IsSet() || ScriptedScheduleOwner.IsSet();
	if (bScriptedPolicy || Mind.State() == EElysiumNpcState::Combat)
	{
		if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
		{
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		}
		if (bPatrolActive && bMoveIssued && !ScheduleOwner.IsSet() && !ScriptedScheduleOwner.IsSet())
		{
			// The route's outstanding request stops once, on the hand-over. The TOKEN is not released
			// here: the claim that takes the body suspends it through the arbiter, which is what lets
			// the route resume at the same point when the program is done.
			if (Motor != nullptr)
			{
				Motor->Stop();
			}
			bMoveIssued = false;
			bWalkingAnimation = false;
		}
		ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
		return;
	}
	if (bPatrolActive && !PatrolOwner.IsSet())
	{
		if (!Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
			PatrolOwner, TEXT("patrol executor admission")))
		{
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.25);
			return;
		}
	}
	if (bPatrolActive && !PatrolPoints.IsEmpty())
	{
		ThinkPatrol();
	}
	else if (bUseInteresting)
	{
		ThinkAmbient();
	}
	else
	{
		// A standing NPC. Before this it fell off the end of Think() without touching NextThink,
		// which is why it was never asked again and held whatever pose it spawned in.
		ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
	}
}

EElysiumScheduleId FElysiumNpc::SelectIdleSchedule()
{
	// 1. Choreo scene or an active discipline. `m_bInChoreoScene` maps onto the scripted body
	//    owner we already issue; the discipline flag has no domain in this runtime yet, so only
	//    the choreo half is answerable -- and it answers the same schedule either way.
	if (Mind.Owner() == EElysiumBodyOwner::Sequence)
	{
		return EElysiumScheduleId::IdleDisposition;
	}

	// 2. The follower controller at virtual `+0x97c`. Refused: `EElysiumBodyOwner::Follower` is
	//    already rejected by the mind, and the three `follower_type` radii it compares against
	//    are unrecovered.
	//
	// 3. Patrol, and 4. `use_interesting`. Both keep their existing executors rather than being
	//    re-expressed as task programs -- they own the body through the mind's own token, which
	//    is the arbitration this selection order would otherwise duplicate.
	if (bPatrolActive || bUseInteresting)
	{
		return EElysiumScheduleId::None;
	}

	// 5. Alert lookaround. `m_iEnemySightings` counts committed-enemy acquisition episodes against
	//    the player, so an NPC that has never had one reads the flat 10% floor and a veteran of
	//    four or more reads the 30% cap. `no_alert_state` does NOT suppress this route: the
	//    recovered base `SelectIdealState` carries no such test.
	if (bAllowAlertLookaround)
	{
		const int32 Chance = ElysiumNpcCond::AlertLookaroundChance(EnemySightings);
		if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99) < Chance)
		{
			return EElysiumScheduleId::AlertLookAroundNi;
		}
	}

	// 6. Door obstruction (`CAI_BaseNPCTroika::SelectDoorObstructionSchedule`).
	if (const EElysiumScheduleId Door = SelectDoorObstructionSchedule();
		Door != EElysiumScheduleId::None)
	{
		return Door;
	}

	// 7. Return-to-initial, else the disposition stance. `m_bReturnToInitialPos` has no producer
	//    here, so this resolves to the stance -- which is also retail's own default.
	return EElysiumScheduleId::IdleDisposition;
}

EElysiumScheduleId FElysiumNpc::SelectSchedule()
{
	switch (Mind.State())
	{
	case EElysiumNpcState::Combat:  return SelectCombatSchedule();
	case EElysiumNpcState::Alert:   return SelectAlertSchedule();
	default:                        return SelectIdleSchedule();
	}
}

EElysiumScheduleId FElysiumNpc::SelectAlertSchedule()
{
	// The executors keep their bodies in alert exactly as they do in idle: a patrol or an ambient
	// place is owned through the mind's token, and re-expressing it as a task program here would
	// duplicate the arbitration.
	if (bPatrolActive || bUseInteresting)
	{
		return EElysiumScheduleId::None;
	}
	if (!Cognition.bReportedAlertRefusal)
	{
		Cognition.bReportedAlertRefusal = true;
		// The recovered alert branch's damage reactions, refused BY NAME rather than approximated.
		// Two of the three programs are now registered (`TAKE_COVER_FROM_ORIGIN` 0x19 and
		// `ALERT_SMALL_FLINCH` 0x07, both minimal and marked in
		// `Substrate/ElysiumNpcCombatSchedules.cpp`) and `ALERT_FACE` is not — but what is missing
		// here is the SELECTION, not the programs: the branch turns on "when the attack origin lies
		// within its recovered facing test", and the survey names that test without stating its
		// threshold (`docs/vtmb/combat-and-damage.md` -> "Incapacitation, feeding, grapple, and
		// death"). Choosing between cover and a flinch on an invented angle would be a behaviour.
		RecordScheduleEvent(TEXT("alert: the damage branch's recovered facing test has no decoded "
			"threshold — holding on the lookaround"));
	}
	// CHOSEN, NOT RECOVERED: the alert state's ordinary (undamaged) selection. Retail's case 3 body
	// is not decoded past the three damage reactions above, so the alert idle is taken to be the
	// lookaround — the one alert-named program the survey does decode, and the one the idle branch
	// already reaches on a chance roll. `m_bAllowAlertLookaround` deliberately does NOT gate it: the
	// recovered keyfield gates step 5 of the IDLE selector, not the alert state itself.
	return EElysiumScheduleId::AlertLookAroundNi;
}

EElysiumScheduleId FElysiumNpc::SelectCombatSchedule()
{
	// An NPC running the patrol or interesting-place executor DOES reach this function: `Think`
	// routes a Combat state to schedule selection ahead of either executor, the patrol route is
	// suspended through the arbiter and resumes when the program ends, and an interesting-place visit
	// gives its claimed place back. The hand-over lives in the routing, not here.
	const double Now = World ? World->NowSeconds() : 0.0;

	// The recovered split: a weapon reporting `0x18000` enters the melee selector at `0x10385e40`,
	// every other weapon the ranged one at `0x10386560`. An NPC the item catalogue could not arm
	// takes the melee branch with bare-hands defaults, and its attack tasks then fail by name — the
	// marked unarmed path, which is a visible refusal rather than an NPC that mimes a fight.
	const ElysiumNpcCond::ECapability Capability = ElysiumNpcCond::WeaponCapability(*this);
	const EElysiumScheduleId Chosen = Capability == ElysiumNpcCond::ECapability::Ranged
		? ElysiumNpcCombat::SelectRangedSchedule(*this, Now)
		: ElysiumNpcCombat::SelectMeleeSchedule(*this, Now);
	if (Chosen != EElysiumScheduleId::None)
	{
		return Chosen;
	}

	// --- The composition rule -------------------------------------------------------------------
	// "A selector returning zero falls through to `CAI_BaseNPCTroika::SelectSchedule`, so the weapon
	// policy composes with damage, door, fear and base state reactions rather than replacing them."
	// What follows is that base branch, in the same order the idle selector runs it.
	if (const EElysiumScheduleId Door = SelectDoorObstructionSchedule();
		Door != EElysiumScheduleId::None)
	{
		return Door;
	}
	// "The base idle/combat selectors may choose `SMALL_FLINCH` (0x14)."
	if (Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage)
		|| Cognition.Conditions.Has(EElysiumNpcCond::LightDamage))
	{
		return EElysiumScheduleId::SmallFlinch;
	}
	// SEAM (comment only): the base branch's fear reaction. The `COWER`/`FLEE` families are 24
	// schedules whose contents the survey does not decode, and `SEE_FEAR` alone does not say which.
	return EElysiumScheduleId::IdleDisposition;
}

void FElysiumNpc::UpdateIdealState(double Now)
{
	if (!Mind.IsAdmitted())
	{
		return;
	}
	const EElysiumNpcState Current = Mind.State();
	if (Current == EElysiumNpcState::Scripted || Current == EElysiumNpcState::Dead)
	{
		// A scripted owner or a dead body owns the state outright; the ideal-state pass does not
		// compete with either.
		return;
	}

	ElysiumNpcCond::FIdealStateInput In;
	In.Current = Current;
	In.bNoAlertState = bNoAlertState;
	In.bHasEnemy = Senses.Memory.Enemy.IsSet();
	bool bCombatWithoutEnemy = false;
	const EElysiumNpcState Ideal =
		ElysiumNpcCond::SelectIdealState(In, Cognition.Conditions, bCombatWithoutEnemy);

	if (bCombatWithoutEnemy && !Cognition.bWarnedCombatWithoutEnemy)
	{
		// The recovered emission, verbatim, and once: it is a state-machine invariant failing, not
		// a per-think event.
		Cognition.bWarnedCombatWithoutEnemy = true;
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Combat state with no enemy"), *DebugString());
	}
	if (Ideal == Current)
	{
		return;
	}
	Mind.RequestState(Ideal, TEXT("SelectIdealState"));
	// An authored director outranks the state change it may itself have caused. `forcestate 3` puts
	// an NPC in combat with no enemy, whose recovered fallback is a drop back to alert on the very
	// next pass — and discarding the program here would cancel the walk the same director pushed
	// half a think earlier. The pushed program ends where every other program ends: on its own
	// completion or failure, in `ThinkStanceOrIdle`.
	if (ScriptedScheduleOwner.IsSet() || ScriptedScheduleOrder.IsSet())
	{
		return;
	}
	// A state change reselects. The running program was chosen by the state that has just been left
	// — an idle stance under an NPC that just acquired an enemy — so it ends here rather than
	// finishing on behalf of a state that no longer holds.
	Schedule.Clear();
	// The discarded program's movement claim goes with it. Releasing after `RequestState` is
	// deliberate: the arbiter's own state refresh runs on the release, and it must see the state
	// this pass decided rather than the one the program was chosen under.
	ReleaseScheduleBody(TEXT("ideal state changed"));
	(void)Now;
}

void FElysiumNpc::ThinkStanceOrIdle(double Now)
{
	double Delay = 0.25;
	if (Schedule.IsRunning()
		&& ElysiumSchedule::Tick(Schedule, *this, Now, Delay, &Cognition.Conditions))
	{
		NextThink = static_cast<float>(Now + Delay);
		return;
	}

	// The program ended — completed, failed through to nothing, or was interrupted. Whatever
	// movement it claimed goes back BEFORE the next selection runs: an idle program picked while
	// this NPC still held the body would decline its own first task against itself.
	ReleaseScheduleBody(TEXT("schedule ended"));

	// A pushed scripted order lives exactly as long as the program it started. Arrival, a refused
	// route and a failed leg all land here, and this is what returns the NPC to ordinary selection
	// and hands a suspended patrol route back. The forced state deliberately does NOT come back with
	// it: `forcestate` was a state push, not a hold.
	if (ScriptedScheduleOrder.IsSet() || ScriptedScheduleOwner.IsSet())
	{
		EndScriptedSchedule(TEXT("scripted schedule ended"));
		// Hand back to `Think`'s routing rather than selecting from inside the branch the director
		// sent this NPC down: a suspended patrol route has just been restored, and resuming it is
		// that executor's turn, not schedule selection's.
		NextThink = static_cast<float>(Now + 0.05);
		return;
	}

	const EElysiumScheduleId Next = SelectSchedule();
	if (Next == EElysiumScheduleId::None || !ElysiumSchedule::Start(Schedule, Next, *this))
	{
		// No idle schedule applies -- an executor owns this body, or this model carries no
		// stance set at all. Either way it is re-asked on a slow cadence rather than dropped.
		NextThink = static_cast<float>(Now + 1.0);
		return;
	}
	if (!ElysiumSchedule::Tick(Schedule, *this, Now, Delay))
	{
		// The schedule ended inside its first think -- a body with no stance machine takes this
		// path, because `TASK_SPECIAL_IDLE_ACTIVITY` fails for it.
		NextThink = static_cast<float>(Now + 1.0);
		return;
	}
	NextThink = static_cast<float>(Now + Delay);
}

void FElysiumNpc::ThinkPatrol()
{
	if (!bPatrolActive || PatrolPoints.IsEmpty())
	{
		return;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	if (!Motor)
	{
		NextThink = static_cast<float>(Now + 0.25);
		return;
	}

	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
	// CharacterMovement is the physical authority while a patrol is active. Write its feet/yaw
	// straight into the entity rather than calling SetRuntimeOrigin, which would teleport it back.
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}

	if (Status == EElysiumNpcMoveStatus::Reached)
	{
		PatrolIndex = (PatrolIndex + 1) % PatrolPoints.Num();
		bMoveIssued = false;
	}
	else if (Status == EElysiumNpcMoveStatus::Failed
		|| Status == EElysiumNpcMoveStatus::Unavailable)
	{
		bMoveIssued = false; // preserve the point and retry; never silently skip authored route data
	}

	if (!bMoveIssued)
	{
		IssuePatrolMove();
	}
	NextThink = static_cast<float>(Now + (bMoveIssued ? 0.05 : 0.25));
}

FElysiumInterestingPlace* FElysiumNpc::CurrentAmbientSpot() const
{
	if (!World || CurrentSpotIndex == INDEX_NONE)
	{
		return nullptr;
	}
	FElysiumEntity* Entity = World->Resolve(
		FElysiumEntityHandle(CurrentSpotIndex, World->GetEpoch()));
	return Entity && Entity->Def
		&& Entity->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
		? static_cast<FElysiumInterestingPlace*>(Entity) : nullptr;
}

const FElysiumInterestingPlaceType* FElysiumNpc::AmbientType(const FElysiumInterestingPlace* Spot) const
{
	const FElysiumInterestingPlaceTable* Table = ElysiumInterestingPlaces::Types();
	return Table && Spot ? Table->Find(Spot->Type) : nullptr;
}

FElysiumInterestingPlace* FElysiumNpc::ClaimAmbientSpot()
{
	if (!World || !Def || !ElysiumInterestingPlaces::Types())
	{
		return nullptr;
	}

	// PickRandomInterestingPlace admits nodes within 10,000 Source units, but distance does not
	// rank them. It walks rating 5 -> 0, stops at the first populated tier, and chooses uniformly
	// within that tier. The NPC schedule stream makes the choice replayable across save/load.
	constexpr double FindRadiusCm = 10000.0 * ElysiumMove::U;
	constexpr double FindRadiusSqCm = FindRadiusCm * FindRadiusCm;
	TArray<FElysiumInterestingPlace*> Candidates;
	TArray<int32> CandidateRatings;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate || !Candidate->Def
			|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(Candidate.Get());
		const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
		if (!Spot->IsAvailable() || FailedSpotIndices.Contains(Spot->Handle.Index)
			|| !AcceptsAmbientGroup(Spot->GroupId)
			|| !TypeRow || TypeRow->Activities.IsEmpty()
			|| !TypeRow->Accepts(Def->Classname, StatTemplate))
		{
			continue;
		}
		if (FVector::DistSquared(Origin, Spot->Origin) > FindRadiusSqCm)
		{
			continue;
		}
		Candidates.Add(Spot);
		CandidateRatings.Add(Spot->Rating);
	}

	const int32 PickedIndex = ElysiumInterestingPlaces::PickHighestRatedCandidate(
		CandidateRatings, ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
	FElysiumInterestingPlace* Picked = Candidates.IsValidIndex(PickedIndex)
		? Candidates[PickedIndex] : nullptr;
	if (Picked && Picked->Claim(Handle))
	{
		if (!Mind.Acquire(EElysiumBodyOwner::Ambient, /*bSuspendCurrent=*/false,
			AmbientOwner, TEXT("interesting-place claim")))
		{
			Picked->Release(Handle);
			return nullptr;
		}
		CurrentSpotIndex = Picked->Handle.Index;
		return Picked;
	}
	return nullptr;
}

bool FElysiumNpc::AcceptsAmbientGroup(int32 GroupId)
{
	if (!bAmbientGroupsParsed)
	{
		bAmbientGroupsParsed = true;
		TArray<FString> Tokens;
		InterestingPlaceGroups.ParseIntoArrayWS(Tokens);
		for (const FString& Token : Tokens)
		{
			AmbientGroups.Add(FCString::Atoi(*Token));
		}
	}
	return AmbientGroups.IsEmpty() || AmbientGroups.Contains(GroupId);
}

bool FElysiumNpc::PlayAmbientActivity(const TArray<FElysiumWeightedName>& Choices, bool bLoop,
	double Now, double& OutEnd)
{
	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!TypeRow || !Embodiment || !Visual)
	{
		return false;
	}
	const uint32 Seed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
		static_cast<uint32>(AmbientActivityCycle++));
	const FString Activity = TypeRow->PickActivity(Choices, Seed);
	float Seconds = 0.0f;
	if (Activity.IsEmpty() || !Embodiment->PlayNpcActivity(Visual, ModelStem(), Activity,
		AmbientActivityCycle, bLoop, &Seconds))
	{
		return false;
	}
	OutEnd = Now + FMath::Max(0.25f, Seconds);
	return true;
}

bool FElysiumNpc::EnsureStanceResolved()
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"),
		*ModelStem(), *Disposition, DispositionLevel);
	if (StanceResolvedFor == Key)
	{
		return !bStanceUnavailable;
	}
	StanceResolvedFor = Key;
	bStanceUnavailable = true;
	StanceClips = FElysiumStanceClips();
	StanceTuning = FElysiumDisposition();

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr
		|| !Embodiment->ResolveDisposition(Disposition, DispositionLevel, StanceTuning)
		|| !Embodiment->ResolveStanceClips(ModelStem(), StanceTuning.AnimName, StanceClips))
	{
		return false;
	}
	bStanceUnavailable = false;
	return true;
}

bool FElysiumNpc::SetDisposition(const FString& NewDisposition, int32 NewLevel)
{
	FElysiumDisposition OldRow;
	FElysiumDisposition NewRow;
	bool bChanged = false;
	if (!CommitDisposition(NewDisposition, NewLevel, bChanged, &OldRow, &NewRow))
	{
		return false;
	}
	if (!bChanged)
	{
		return true;
	}

	StanceResolvedFor.Reset();
	bool bPlayedTransition = false;
	const EElysiumBodyOwner Owner = Mind.Owner();
	if (Visual && !FElysiumCombatCharacter::IsFeedBusy()
		&& (Owner == EElysiumBodyOwner::None || Owner == EElysiumBodyOwner::Dialogue))
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment)
		{
			const FString OldAnim = !OldRow.AnimName.IsEmpty() ? OldRow.AnimName : OldRow.Name;
			const FString NewAnim = !NewRow.AnimName.IsEmpty() ? NewRow.AnimName : NewRow.Name;
			const int32 StanceNumber = FMath::Clamp(
				Stance.Current, 0, ElysiumStance::Count - 1) + 1;
			auto TryTransition = [&](int32 Number)
			{
				if (OldAnim.IsEmpty() || NewAnim.IsEmpty())
				{
					return false;
				}
				const FString Clip = FString::Printf(TEXT("Stance_Trans_%s_%d_%s_%d"),
					*OldAnim, Number, *NewAnim, Number);
				// This cross-disposition transition is authored per model, and most bodies carry
				// none: probe the vocabulary first, so an absent clip is a quiet negative query
				// result rather than PlayNpcClip's logged miss.
				if (!Embodiment->HasNpcClip(ModelStem(), Clip))
				{
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("%s disposition transition '%s' not authored; not taken"),
						*DebugString(), *Clip);
					return false;
				}
				float Seconds = 0.f;
				if (!Embodiment->PlayNpcClip(Visual, ModelStem(), Clip,
					/*bLoop=*/false, &Seconds))
				{
					return false;
				}
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("%s disposition transition '%s' taken"), *DebugString(), *Clip);
				Mind.RecordExternal(FString::Printf(TEXT("disposition %s L%d -> %s L%d via %s"),
					*OldRow.Name, OldRow.Level, *NewRow.Name, NewRow.Level, *Clip));
				if (World)
				{
					NextThink = static_cast<float>(World->NowSeconds() + FMath::Max(0.05f, Seconds));
				}
				return true;
			};
			bPlayedTransition = TryTransition(StanceNumber)
				|| (StanceNumber != 1 && TryTransition(1));
		}
	}
	if (!bPlayedTransition)
	{
		ResetAnimToIdle();
		if (World)
		{
			NextThink = static_cast<float>(World->NowSeconds());
		}
	}
	return true;
}

float FElysiumNpc::RunSpecialIdleActivity(double Now)
{
	// The task writes a clip onto the body, so it runs only while this NPC's own idle owns it.
	// `SCHED_TROIKA_IDLE_DISPOSITION` is still the faithful selection for a choreo-scene NPC --
	// the guard belongs here, at the one step that would overwrite what the owner is playing.
	// The admitted set matches the disposition-stance transition's: nobody, or a conversation.
	const EElysiumBodyOwner BodyOwner = Mind.Owner();
	if (BodyOwner != EElysiumBodyOwner::None && BodyOwner != EElysiumBodyOwner::Dialogue)
	{
		// Failing the task is how a runner declines: the schedule ends through its (absent) fail
		// schedule and the caller re-asks on the slow cadence rather than spinning at zero delay.
		Mind.RecordExternal(FString::Printf(
			TEXT("TASK_SPECIAL_IDLE_ACTIVITY declined: %s owns the body"), LexToString(BodyOwner)));
		return -1.f;
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr || !EnsureStanceResolved())
	{
		return -1.f;
	}
	// `bTalking` is the file's own distinction: a line playing on this character, not a dialogue
	// being open. We have only the session latch until the per-line driver lands, and the two
	// agree on the branch that matters -- a character in dialogue holds its stance either way.
	const int32 Before = Stance.Current;
	const FElysiumStanceChoice Choice = ElysiumStance::Select(StanceClips, StanceTuning, Stance,
		/*bTalking=*/IsDispositionTalking(), Now,
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
	float Seconds = 0.f;
	if (!Choice.IsSet()
		|| !Embodiment->PlayNpcClip(Visual, ModelStem(), Choice.Clip, Choice.bLoop, &Seconds))
	{
		return -1.f;
	}
	// Only a change is traced. An idle re-settling on the same stance is the common case by far,
	// and recording it would push everything else out of a 16-row window.
	if (Choice.bChangedStance)
	{
		Mind.RecordExternal(FString::Printf(TEXT("stance %d -> %d via %s"),
			Before, Stance.Current, *Choice.Clip));
	}
	return Seconds;
}

bool FElysiumNpc::IsBodyVisible() const
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	// No embodiment is a headless run, where the question has no renderer to answer it. The
	// service's own default says visible for the same reason.
	return Embodiment == nullptr || Embodiment->IsNpcBodyVisible(Visual);
}

float FElysiumNpc::PlayActivity(const FString& Activity)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return -1.f;
	}
	float Seconds = 0.f;
	if (!Embodiment->PlayNpcActivity(Visual, ModelStem(), Activity, ScheduleActivityCycle++,
		/*bLoop=*/false, &Seconds))
	{
		return -1.f;
	}
	return Seconds;
}

float FElysiumNpc::RandomSeconds(float Max)
{
	return Max > 0.f
		? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.f, Max) : 0.f;
}

void FElysiumNpc::RecordScheduleEvent(const FString& Row)
{
	Mind.RecordExternal(Row);
}

bool FElysiumNpc::FaceSavePosition()
{
	if (Motor == nullptr)
	{
		return false;
	}
	const FVector ToSource = SavePosition - Origin;
	if (ToSource.IsNearlyZero())
	{
		return false;
	}
	Motor->Face(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToSource.Y, ToSource.X))));
	return true;
}

bool FElysiumNpc::StepAwayFromSavePosition(float DistanceCm)
{
	// A retreat is body movement, so it claims the body first. The near-door family reaches this
	// from the idle branch and the combat family from the fight; both are schedules moving an NPC,
	// which is exactly what the `Schedule` owner names.
	if (Motor != nullptr && !AcquireScheduleBody(TEXT("TASK_MOVE_AWAY_PATH")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_MOVE_AWAY_PATH refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	// The rule itself is `ElysiumSchedule::StepAwayFromSavePosition` — extrapolate, project
	// through the motor, re-test the projection against the retreat rule. This leaf supplies the
	// two positions and turns the outcome into the task's pass/fail, so a refusal reaches the
	// schedule's fail path already named rather than as a bare false.
	FVector Destination = FVector::ZeroVector;
	const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
		Motor, Origin, SavePosition, DistanceCm, Destination);
	if (Result != ElysiumSchedule::ERetreat::Moving)
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_MOVE_AWAY_PATH refused: %s"),
			ElysiumSchedule::RetreatResultName(Result)));
		return false;
	}
	bMoveIssued = true;
	return true;
}

// ================================================================================================
// The combat task bodies (cycle 6)
// ================================================================================================

bool FElysiumNpc::AcquireScheduleBody(const TCHAR* Reason)
{
	if (ScheduleOwner.IsSet() && Mind.Owner() == EElysiumBodyOwner::Schedule
		&& Mind.Generation() == ScheduleOwner.Generation)
	{
		return true;
	}
	// A token from a claim that was displaced (a scripted beat took the body mid-chase) is retired
	// here rather than carried: the arbiter would refuse a release against it anyway.
	ScheduleOwner.Reset();
	// A patrol route is SUSPENDED rather than taken: the arbiter parks it, the release below restores
	// it, and the route continues from the point it reached. An interesting-place visit is not
	// parkable in that sense — it owns a claimed place — so `Think`'s hand-over finishes it first.
	const bool bParkPatrol = Mind.Owner() == EElysiumBodyOwner::Patrol;
	return Mind.Acquire(EElysiumBodyOwner::Schedule, bParkPatrol, ScheduleOwner, Reason);
}

void FElysiumNpc::ReleaseScheduleBody(const TCHAR* Reason)
{
	if (!ScheduleOwner.IsSet())
	{
		return;
	}
	const bool bLive = Mind.Owner() == EElysiumBodyOwner::Schedule
		&& Mind.Generation() == ScheduleOwner.Generation;
	if (bLive)
	{
		// Whatever the program had the body doing stops with the claim. A schedule that ended
		// mid-path must not leave an outstanding move running under whatever selects next.
		if (Motor != nullptr)
		{
			Motor->Stop();
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
		Mind.Release(ScheduleOwner, Reason);
	}
	ScheduleOwner.Reset();
	if (Mind.Owner() == EElysiumBodyOwner::Patrol)
	{
		// A suspended route came back with a fresh generation. The leaf's own token has to be
		// re-stamped or the patrol executor would hold one the arbiter no longer honours.
		PatrolOwner = Mind.CurrentToken();
	}
}

bool FElysiumNpc::AcquireScriptedScheduleBody(const TCHAR* Reason)
{
	if (ScriptedScheduleOwner.IsSet() && Mind.Owner() == EElysiumBodyOwner::ScriptedSchedule
		&& Mind.Generation() == ScriptedScheduleOwner.Generation)
	{
		return true;
	}
	ScriptedScheduleOwner.Reset();
	const bool bParkPatrol = Mind.Owner() == EElysiumBodyOwner::Patrol;
	return Mind.Acquire(EElysiumBodyOwner::ScriptedSchedule, bParkPatrol, ScriptedScheduleOwner,
		Reason);
}

void FElysiumNpc::ReleaseScriptedScheduleBody(const TCHAR* Reason)
{
	if (!ScriptedScheduleOwner.IsSet())
	{
		return;
	}
	const bool bLive = Mind.Owner() == EElysiumBodyOwner::ScriptedSchedule
		&& Mind.Generation() == ScriptedScheduleOwner.Generation;
	if (bLive)
	{
		if (Motor != nullptr)
		{
			Motor->Stop();
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
		Mind.Release(ScriptedScheduleOwner, Reason);
	}
	ScriptedScheduleOwner.Reset();
	if (Mind.Owner() == EElysiumBodyOwner::Patrol)
	{
		PatrolOwner = Mind.CurrentToken();
	}
}

// ================================================================================================
// The authored director (cycle 7)
// ================================================================================================

bool FElysiumNpc::BeginScriptedSchedule(const FElysiumScriptedScheduleOrder& Order,
	bool bHasForcedState, EElysiumNpcState ForcedState)
{
	using EMode = ElysiumAiScriptedSchedule::EMode;

	if (IsInert())
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted schedule: the NPC is dead or hidden"), *DebugString());
		return false;
	}
	if (!Mind.IsAdmitted())
	{
		// DEFERRED, not refused, and the whole push waits rather than half of it. `sm_medical_1`
		// wires `guard_to_nurse` off an `npc_maker`'s `OnSpawnNPC`, so a director genuinely reaches
		// an NPC that has never thought — and admission establishes idle on that first think, so a
		// forced state applied ahead of it would be wiped by the barrier it was racing. The order
		// carries its own forced state for exactly this window and `Think` replays it once admission
		// has run, the same shape a scripted beat's deferred body claim already takes.
		ScriptedScheduleOrder = Order;
		ScriptedScheduleOrder.bHasForcedState = bHasForcedState;
		ScriptedScheduleOrder.ForcedState = ForcedState;
		ScriptedScheduleOrder.bPending = true;
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return true;
	}

	// The policy, first and unconditionally. It is a STATE push and not a hold: nothing here parks
	// the state to restore later, because the recovered entity has no end and no release — it fires
	// once and the NPC carries what it was given.
	if (bHasForcedState)
	{
		Mind.RequestState(ForcedState, TEXT("aiscripted_schedule forcestate"));
	}

	if (static_cast<EMode>(Order.Mode) == EMode::AssignEnemy)
	{
		// Mode 3, recovered: "assigns the goal entity as enemy, copies its target position, and
		// injects native condition 0x54". The assignment goes through the ordinary `SetEnemy`
		// transaction rather than writing the handle, so the last-enemy transfer, the LOS-episode
		// reset and everything else an acquisition means all happen exactly once and in one place.
		const FElysiumEntity* Goal = World ? World->Resolve(Order.Goal) : nullptr;
		ElysiumNpcEnemy::SetEnemy(*this, Order.Goal);
		if (Goal != nullptr)
		{
			// "copies its target position". CHOSEN, NOT RECOVERED — WHICH slot. The recovered
			// sentence names a target position and this chain carries exactly one recovered position
			// member, `m_vSavePosition` (+0x5dd0), which is also what the retreat and cover tasks
			// read and what the combat selector stamps with an enemy origin for the same purpose.
			SavePosition = Goal->Origin;
		}
		// The injected condition, by its recovered number: `NEW_ENEMY` is 0x54.
		Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		// The running program was chosen by an NPC that did not have this enemy. It ends here rather
		// than finishing on behalf of a decision the director has just overruled.
		Schedule.Clear();
		ReleaseScheduleBody(TEXT("aiscripted_schedule assigned an enemy"));
		RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode 3: enemy := %s"),
			World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return true;
	}

	const EElysiumScheduleId Program = ElysiumAiScriptedSchedule::ProgramFor(Order.Mode);
	if (Program == EElysiumScheduleId::None)
	{
		// A forced state with no movement mode is an ordinary authored row: two corpus rows push a
		// state alone. The push above already happened, so there is nothing left to refuse.
		RecordScheduleEvent(TEXT("aiscripted_schedule: forced state only, no movement mode"));
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		return bHasForcedState;
	}

	// An interesting-place visit gives its claimed place back before the director takes the body —
	// ambient owns a place rather than a resumable route, so parking it would strand the claim.
	if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}
	// An ordinary combat claim gives way to the director. Releasing before the new claim keeps the
	// arbiter's parked-owner slot holding the PATROL route rather than the schedule that displaced it.
	Schedule.Clear();
	ReleaseScheduleBody(TEXT("aiscripted_schedule took the body"));

	ScriptedScheduleOrder = Order;
	ScriptedScheduleOrder.bPending = false;   // this IS the replay; nothing is waiting any more
	if (!AcquireScriptedScheduleBody(TEXT("aiscripted_schedule")))
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s refused a scripted schedule the body: %s owns it"),
			*DebugString(), LexToString(Mind.Owner()));
		ScriptedScheduleOrder.Reset();
		return false;
	}
	if (!ElysiumSchedule::Start(Schedule, Program, *this))
	{
		EndScriptedSchedule(TEXT("scripted program would not start"));
		return false;
	}
	RecordScheduleEvent(FString::Printf(TEXT("aiscripted_schedule mode %d (%s, %s) goal %s"),
		Order.Mode, ElysiumAiScriptedSchedule::ModeName(Order.Mode),
		Order.bRun ? TEXT("run") : TEXT("walk"),
		World ? *World->DescribeHandle(Order.Goal) : TEXT("(no world)")));
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	return true;
}

void FElysiumNpc::EndScriptedSchedule(const TCHAR* Reason)
{
	// `bPending` is tested beside the other two because a state-only push carries mode 0, which is
	// not `IsSet()` — a deferred one still has to be droppable by death, dormancy and a beat.
	if (!ScriptedScheduleOrder.IsSet() && !ScriptedScheduleOrder.bPending
		&& !ScriptedScheduleOwner.IsSet())
	{
		return;
	}
	ScriptedScheduleOrder.Reset();
	if (ElysiumAiScriptedSchedule::IsScriptedProgram(Schedule.Current))
	{
		// The program and the order are one thing. A scripted program left running with no order
		// behind it would fail its next leg by name for a reason no reader could act on.
		Schedule.Clear();
	}
	ReleaseScriptedScheduleBody(Reason);
}

bool FElysiumNpc::GetPathToScriptedGoal()
{
	if (!ScriptedScheduleOrder.IsSet())
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: no scripted order is in force"));
		return false;
	}
	if (!ScriptedScheduleOrder.Route.IsValidIndex(ScriptedScheduleOrder.Leg))
	{
		// The route ran out, which is how a follow-path program ends: the transfer back to itself
		// re-enters here, this fails, and the NPC returns to ordinary selection. It is not an error.
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL: the scripted route is complete"));
		return false;
	}
	if (Motor == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: this NPC has no motor"));
		return false;
	}
	if (!AcquireScriptedScheduleBody(TEXT("TASK_GET_PATH_TO_GOAL")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_GET_PATH_TO_GOAL refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	const FVector Destination = ScriptedScheduleOrder.Route[ScriptedScheduleOrder.Leg++];
	bMoveIssued = Motor->MoveTo(Destination, ElysiumNpcGait::ScriptAcceptanceCm,
		ScriptedScheduleOrder.bRun ? ElysiumNpcGait::RunSpeed : ElysiumNpcGait::WalkSpeed);
	if (!bMoveIssued)
	{
		// The recovered route-failure report, and the recovered switch that silences it: "spawn flag
		// 0x800 suppresses the route-failure warning". Latched per pushed order either way — a body
		// that could not take this leg will not take the next one.
		if (!ScriptedScheduleOrder.bSuppressRouteWarning && !ScriptedScheduleOrder.bWarnedRoute)
		{
			ScriptedScheduleOrder.bWarnedRoute = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s could not take the route an aiscripted_schedule pushed (goal %s): the "
					 "program fails and the NPC returns to ordinary selection"),
				*DebugString(),
				World ? *World->DescribeHandle(ScriptedScheduleOrder.Goal) : TEXT("(no world)"));
		}
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_GOAL refused: the body would not take the route"));
		return false;
	}
	bWalkingAnimation = StartWalkingAnimation(ScriptedScheduleOrder.bRun);
	return true;
}

void FElysiumNpc::InputNamedSchedule(const FElysiumInputArgs& Args)
{
	const FString Requested = Args.Param.ToString().TrimStartAndEnd();
	const FString InputName = Args.Input.IsNone()
		? FString(TEXT("ChangeSchedule")) : Args.Input.ToString();
	if (IsInert())
	{
		return;
	}
	if (Requested.IsEmpty())
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s %s was fired with no schedule name — refused"), *DebugString(), *InputName);
		return;
	}
	EElysiumScheduleId Id = EElysiumScheduleId::None;
	if (!ElysiumScheduleIdFromName(Requested, Id))
	{
		// The gap is the NAMED program, not the input. The corpus's five `ChangeSchedule` sites ask
		// for `SCHED_VDOG_SNARL`, `SCHED_VDOG_MADEFRIEND` and the literal `-`, and this runtime
		// registers none of the three; the reported surface is keyed on the NAME, so `elysium.stubs`
		// reads back exactly which native schedules the shipped scripts want, one row each.
		ElysiumStub::Fired(TEXT("schedule"),
			FString::Printf(TEXT("CAI_BaseNPC.%s(%s)"), *InputName, *Requested),
			DebugString(), ElysiumStub::DescribeInput(Args),
			TEXT("no registered program carries that name"));
		RecordScheduleEvent(FString::Printf(TEXT("%s refused: '%s' is not a registered program"),
			*InputName, *Requested));
		return;
	}
	// A named schedule starts through the ordinary kernel: interrupts, fail schedules, motor work and
	// activity translation are all whatever the named program declares. Nothing about being named by
	// a script changes how it runs, which is the whole recovered point of these two commands.
	Schedule.Clear();
	ReleaseScheduleBody(*InputName);
	if (!ElysiumSchedule::Start(Schedule, Id, *this))
	{
		return;   // `Start` reports the refusal by name through the runner's own trace
	}
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumNpc::StopMoving()
{
	if (Motor != nullptr)
	{
		Motor->Stop();
	}
	bMoveIssued = false;
	bWalkingAnimation = false;
}

bool FElysiumNpc::GetPathToEnemy(float ToleranceUnits)
{
	const FElysiumEntity* Enemy = World
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr || Enemy->IsInert())
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: no live committed enemy"));
		return false;
	}
	if (Motor == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: this NPC has no motor"));
		return false;
	}
	if (!AcquireScheduleBody(TEXT("TASK_GET_PATH_TO_ENEMY")))
	{
		Mind.RecordExternal(FString::Printf(
			TEXT("TASK_GET_PATH_TO_ENEMY refused: %s owns the body"), LexToString(Mind.Owner())));
		return false;
	}
	// The operand is the schedule's own tolerance, in Source units. A program that never ran
	// `TASK_SET_TOLERANCE_DISTANCE` leaves it negative, and the motor's own arrival radius decides.
	const float ToleranceCm = ToleranceUnits > 0.f
		? static_cast<float>(ToleranceUnits * ElysiumMove::U)
		: ElysiumNpcGait::ScriptAcceptanceCm;
	// The enemy's FEET: an entity's origin is its feet in this runtime, which is what the patrol
	// executor already hands the same verb.
	bMoveIssued = Motor->MoveTo(Enemy->Origin, ToleranceCm, ElysiumNpcGait::RunSpeed);
	if (!bMoveIssued)
	{
		Mind.RecordExternal(TEXT("TASK_GET_PATH_TO_ENEMY refused: the body would not take the path"));
	}
	return bMoveIssued;
}

void FElysiumNpc::RunPath()
{
	// Locomotion, not permission: a bank with no run clip still travels. The miss is recorded so a
	// body walking a chase in its idle pose is diagnosable rather than invisible.
	bWalkingAnimation = StartWalkingAnimation(/*bRunning=*/true);
	if (!bWalkingAnimation)
	{
		Mind.RecordExternal(TEXT("TASK_RUN_PATH: no run locomotion resolved for this body"));
	}
}

EElysiumMoveWatch FElysiumNpc::WaitForMovement()
{
	if (Motor == nullptr)
	{
		return EElysiumMoveWatch::Failed;
	}
	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
	// The motor is the physical authority while it holds a request, exactly as in the patrol
	// executor: its feet/yaw are written straight into the entity rather than through
	// SetRuntimeOrigin, which would teleport the body back.
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
	switch (Status)
	{
	case EElysiumNpcMoveStatus::Reached:
		bMoveIssued = false;
		return EElysiumMoveWatch::Arrived;
	case EElysiumNpcMoveStatus::Failed:
	case EElysiumNpcMoveStatus::Unavailable:
		bMoveIssued = false;
		Mind.RecordExternal(TEXT("TASK_WAIT_FOR_MOVEMENT: the body gave up its path"));
		return EElysiumMoveWatch::Failed;
	case EElysiumNpcMoveStatus::Idle:
		// No outstanding request. The step that should have issued one already failed its own task,
		// so arriving here means the body is standing where it was told to be.
		return EElysiumMoveWatch::Arrived;
	default:
		return EElysiumMoveWatch::Moving;
	}
}

bool FElysiumNpc::FaceEnemy()
{
	const FElysiumEntity* Enemy = World
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr || Enemy->IsInert())
	{
		Mind.RecordExternal(TEXT("TASK_FACE_ENEMY refused: no live committed enemy"));
		return false;
	}
	if (Motor == nullptr)
	{
		// A bodiless NPC turns by writing its own yaw. The task is about where the character is
		// pointed, and the character exists whether or not a capsule was built for it.
		const FVector ToEnemy = Enemy->Origin - Origin;
		if (ToEnemy.IsNearlyZero())
		{
			return false;
		}
		Angles.Y = -static_cast<float>(
			FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Y, ToEnemy.X)));
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}
		return true;
	}
	if (!AcquireScheduleBody(TEXT("TASK_FACE_ENEMY")))
	{
		Mind.RecordExternal(FString::Printf(TEXT("TASK_FACE_ENEMY refused: %s owns the body"),
			LexToString(Mind.Owner())));
		return false;
	}
	const FVector ToEnemy = Enemy->Origin - Origin;
	if (ToEnemy.IsNearlyZero())
	{
		return false;
	}
	Motor->Face(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Y, ToEnemy.X))));
	return true;
}

bool FElysiumNpc::AnnounceAttack(float Param)
{
	FElysiumEntity* Enemy = World ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_ANNOUNCE_ATTACK refused: no live committed enemy"));
		return false;
	}
	const double Now = World->NowSeconds();
	// The notice is an NPC-side record. A player victim has no such memory — its reaction is the
	// player's own input — so announcing at the player is an ordinary negative, not a failure.
	if (FElysiumNpc* Victim = Enemy->AsNpc())
	{
		const bool bAccepted = ElysiumNpcCond::NoticeMeleeAttack(*Victim, Handle, Origin, Now);
		Mind.RecordExternal(FString::Printf(TEXT("TASK_ANNOUNCE_ATTACK %g -> %s %s"),
			Param, *World->DescribeHandle(Victim->Handle),
			bAccepted ? TEXT("accepted") : TEXT("out of notice range")));
	}
	return true;
}

namespace
{
	// The active weapon controller, or null. Shared by the two attack tasks so a missing weapon
	// fails both the same way.
	FElysiumWeapon* ElysiumNpcActiveWeapon(FElysiumNpc& Npc)
	{
		FElysiumItem* Item = Npc.Inventory.Active(Npc);
		return Item != nullptr ? Item->AsWeapon() : nullptr;
	}
}

bool FElysiumNpc::MeleeAttack1()
{
	FElysiumWeapon* Weapon = ElysiumNpcActiveWeapon(*this);
	if (Weapon == nullptr)
	{
		// The marked unarmed path: an NPC the catalogue could not arm fails its terminal attack task
		// by name rather than dealing damage out of nothing.
		Mind.RecordExternal(TEXT("TASK_MELEE_ATTACK1 failed: no active weapon"));
		return false;
	}
	// The transaction is the weapon's (`docs/vtmb/combat-and-damage.md`): the task presses, and the
	// controller acquires its own opponent, stages the swing and schedules the commit.
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary);
	Mind.RecordExternal(FString::Printf(TEXT("TASK_MELEE_ATTACK1 -> %s"),
		FElysiumWeapon::VerdictName(Verdict)));
	return Verdict == FElysiumWeapon::EVerdict::Accepted;
}

bool FElysiumNpc::RangeAttack1()
{
	FElysiumWeapon* Weapon = ElysiumNpcActiveWeapon(*this);
	if (Weapon == nullptr)
	{
		Mind.RecordExternal(TEXT("TASK_RANGE_ATTACK1 failed: no active weapon"));
		return false;
	}
	// The ranged transaction takes an explicit victim rather than tracing for one: the shot's world
	// trace and spread cone are a producer that joins with the perception cycle, and the committed
	// enemy IS this NPC's answer to it.
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary, Senses.Memory.Enemy);
	Mind.RecordExternal(FString::Printf(TEXT("TASK_RANGE_ATTACK1 -> %s"),
		FElysiumWeapon::VerdictName(Verdict)));
	return Verdict == FElysiumWeapon::EVerdict::Accepted;
}

void FElysiumNpc::RememberFact(float What)
{
	// Traced and otherwise inert — the memory-bit table the operand indexes is not decoded
	// (`EElysiumTask::Remember`).
	Mind.RecordExternal(FString::Printf(TEXT("TASK_REMEMBER %g (no consumer)"), What));
}

EElysiumScheduleId FElysiumNpc::SelectDoorObstructionSchedule()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// The source order is retail's own. A blocked-door handle outlives its usefulness, so an
	// expired one is cleared rather than reused.
	const FElysiumEntity* Source = nullptr;
	if (BlockedDoor.IsSet() && World)
	{
		if (Now < BlockedDoorExpiresAt)
		{
			Source = World->Resolve(BlockedDoor);
		}
		else
		{
			BlockedDoor = FElysiumEntityHandle::Invalid();
		}
	}
	if (Source == nullptr && bCondHitByDoor && CondHitByDoor.IsSet() && World)
	{
		Source = World->Resolve(CondHitByDoor);
	}
	if (Source == nullptr)
	{
		return EElysiumScheduleId::None;
	}

	// With a source chosen, retail asks the hint machinery for cover and takes it when the claim
	// is medium, low or corner cover. We carry no hint-node reader yet, so this falls through to
	// the distance test -- the authored `info_node_cover_med/low/corner` entities the branch
	// needs are exported but unread.
	SavePosition = Source->Origin;

	// 256 units, compared squared, in Source units -- the same 2.54 cm/inch the whole runtime
	// reads verbatim.
	constexpr double ThresholdCm = 256.0 * 2.54;
	const bool bNear = FVector::DistSquared(Origin, SavePosition) <= ThresholdCm * ThresholdCm;
	// The recovered table branches on `GetEnemy`: the enemy rows are `SCHED_TROIKA_BACK_AWAY_FROM_DOOR`
	// (0x90) and `_WAIT` (0x94), the no-enemy rows their `_NE` variants (0x91 / 0x96). Only the two
	// `_NE` programs are registered here, so an NPC that HAS an enemy is a divergence rather than a
	// branch -- and it is recorded by name instead of taken quietly.
	if (Senses.Memory.Enemy.IsSet())
	{
		RecordScheduleEvent(TEXT("door obstruction with an enemy: SCHED_TROIKA_BACK_AWAY_FROM_DOOR "
			"(0x90) / _WAIT (0x94) are not registered — taking the _NE variant"));
	}
	return bNear ? EElysiumScheduleId::BackAwayFromDoorNe
		: EElysiumScheduleId::BackAwayFromDoorWaitNe;
}

void FElysiumNpc::BeginAmbientUse(FElysiumInterestingPlace& Spot, double Now)
{
	bAmbientArrived = true;
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (Motor)
	{
		Motor->Stop();
		if (Spot.bMatchOrientation)
		{
			Motor->Teleport(Spot.Origin, -Spot.Angles.Y);
			Origin = Spot.Origin;
			Angles.Y = Spot.Angles.Y;
		}
	}
	Spot.Arrived(Handle);

	const uint32 StaySeed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
		static_cast<uint32>(FMath::Max(0, Spot.Handle.Index)));
	const float Unit = static_cast<float>(StaySeed) / static_cast<float>(MAX_uint32);
	const float Lo = FMath::Max(0.0f, FMath::Min(Spot.MinTime, Spot.MaxTime));
	const float Hi = FMath::Max(Lo, FMath::Max(Spot.MinTime, Spot.MaxTime));
	AmbientLeaveAt = Now + FMath::Lerp(Lo, Hi, Unit);

	const FElysiumInterestingPlaceType* TypeRow = AmbientType(&Spot);
	if (TypeRow && PlayAmbientActivity(TypeRow->IntoActivities, /*bLoop=*/false,
		Now, AmbientNextActivityAt))
	{
		AmbientPhase = EAmbientPhase::Into;
	}
	else
	{
		AmbientPhase = EAmbientPhase::Dwelling;
		AmbientNextActivityAt = Now;
	}
}

void FElysiumNpc::BeginAmbientLeave(double Now)
{
	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
	if (TypeRow && PlayAmbientActivity(TypeRow->OutOfActivities, /*bLoop=*/false,
		Now, AmbientNextActivityAt))
	{
		AmbientPhase = EAmbientPhase::Out;
		return;
	}
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
}

void FElysiumNpc::FinishAmbientUse(bool bFireLeft)
{
	// This is the single exit for an ambient claim, including UseInteresting(0), a disabled spot,
	// dialogue, dormancy and a patrol taking ownership. Cancel a request before forgetting it so
	// the native controller cannot keep walking an entity the substrate now considers idle.
	if (AmbientPhase == EAmbientPhase::Moving && bMoveIssued && Motor)
	{
		Motor->Stop();
	}
	if (FElysiumInterestingPlace* Spot = CurrentAmbientSpot())
	{
		if (bFireLeft)
		{
			Spot->Left(Handle);
		}
		Spot->Release(Handle);
	}
	CurrentSpotIndex = INDEX_NONE;
	AmbientPhase = EAmbientPhase::None;
	bAmbientArrived = false;
	bMoveIssued = false;
	bWalkingAnimation = false;
	if (AmbientOwner.IsSet())
	{
		Mind.Release(AmbientOwner, TEXT("interesting-place release"));
		AmbientOwner.Reset();
	}
	if (ScriptPhase == EScriptPhase::None)
	{
		ResetAnimToIdle();   // a script that owns the body owns its pose too
	}
}

void FElysiumNpc::ThinkAmbient()
{
	const double Now = World ? World->NowSeconds() : 0.0;
	if (!Motor)
	{
		NextThink = static_cast<float>(Now + 0.5);
		return;
	}

	FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
	if (AmbientPhase == EAmbientPhase::None || !Spot)
	{
		Spot = ClaimAmbientSpot();
		if (!Spot)
		{
			if (!FailedSpotIndices.IsEmpty())
			{
				FailedSpotIndices.Reset();
			}
			NextThink = static_cast<float>(Now + 1.0);
			return;
		}
		AmbientPhase = EAmbientPhase::Moving;
		bMoveIssued = Motor->MoveTo(Spot->Origin, 24.0f, 254.0f);
		if (bMoveIssued)
		{
			bWalkingAnimation = StartWalkingAnimation();
		}
		else
		{
			FailedSpotIndices.Add(Spot->Handle.Index);
			FinishAmbientUse(/*bFireLeft=*/false);
		}
		NextThink = static_cast<float>(Now + 0.25);
		return;
	}

	if (!Spot->IsEnabledFor(Handle))
	{
		if (!bAmbientArrived)
		{
			FinishAmbientUse(/*bFireLeft=*/false);
			NextThink = static_cast<float>(Now + 0.5);
			return;
		}
		if (AmbientPhase != EAmbientPhase::Out)
		{
			BeginAmbientLeave(Now);
			NextThink = static_cast<float>(Now + 0.1);
			return;
		}
		// An already-started out activity is allowed to finish below even though Disable made
		// the place unavailable to new claimants.
	}

	if (AmbientPhase == EAmbientPhase::Moving)
	{
		FVector Feet = Origin;
		float Yaw = -Angles.Y;
		const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
		Origin = Feet;
		Angles.Y = -Yaw;
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}
		if (Status == EElysiumNpcMoveStatus::Reached)
		{
			BeginAmbientUse(*Spot, Now);
		}
		else if (Status == EElysiumNpcMoveStatus::Failed
			|| Status == EElysiumNpcMoveStatus::Unavailable)
		{
			FailedSpotIndices.Add(Spot->Handle.Index);
			FinishAmbientUse(/*bFireLeft=*/false);
		}
		NextThink = static_cast<float>(Now + 0.05);
		return;
	}

	if (AmbientPhase == EAmbientPhase::Out && Now >= AmbientNextActivityAt)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		NextThink = static_cast<float>(Now + 0.5);
		return;
	}
	if (AmbientPhase == EAmbientPhase::Into && Now >= AmbientNextActivityAt)
	{
		AmbientPhase = EAmbientPhase::Dwelling;
		AmbientNextActivityAt = Now;
	}
	if (AmbientPhase == EAmbientPhase::Dwelling)
	{
		if (Now >= AmbientLeaveAt)
		{
			BeginAmbientLeave(Now);
		}
		else if (Now >= AmbientNextActivityAt)
		{
			const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
			if (!TypeRow || !PlayAmbientActivity(TypeRow->Activities, /*bLoop=*/false,
				Now, AmbientNextActivityAt))
			{
				ResetAnimToIdle();
				AmbientNextActivityAt = FMath::Min(AmbientLeaveAt, Now + 2.0);
			}
		}
	}
	NextThink = static_cast<float>(Now + 0.1);
}

FElysiumBodyOwnerToken FElysiumNpc::BeginDialogueBodySession()
{
	if (IsInert())
	{
		return FElysiumBodyOwnerToken();
	}
	if (DialogueBodyOwner.IsSet())
	{
		return DialogueBodyOwner;
	}
	// Dialogue is the authored interruption of CCineNPC ownership. Cancel through the sequence
	// itself before asking the mind for Dialogue so travel, action, collision flags and the
	// sequence's delayed completion all leave through the one CancelSequence teardown. This is
	// what prevents an older action deadline from resetting a newer dialogue line to idle.
	if (ScriptOwner.IsSet() && World)
	{
		const FElysiumEntityHandle PreviousOwner = ScriptOwner;
		if (FElysiumEntity* Owner = World->Resolve(PreviousOwner))
		{
			if (Owner->CancelScriptedSequenceForDialogue(Handle) && ScriptOwner.IsSet())
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s dialogue cancelled scripted owner %s but the body claim remained"),
					*DebugString(), *PreviousOwner.ToString());
				return FElysiumBodyOwnerToken();
			}
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s cleared stale scripted owner %s while opening dialogue"),
				*DebugString(), *PreviousOwner.ToString());
			// Nothing is left to run the beat's own teardown, so its body claim is dropped here
			// too -- otherwise the arbiter would still read Sequence and refuse the dialogue.
			ReleaseScriptBody(TEXT("stale scripted owner cleared for dialogue"));
			EndScriptMove();
			ScriptOwner = FElysiumEntityHandle::Invalid();
			bScriptOwnerLocked = false;
		}
	}
	// A pushed scripted order is DROPPED rather than parked. The arbiter's one parked slot is spoken
	// for by the patrol route, and `aiscripted_schedule` has no resume: it is a one-shot push with no
	// end and no release, so a conversation ends the order it interrupted. The combat claim leaves
	// for the same one-slot reason it does at `AcquireSequenceBody`.
	EndScriptedSchedule(TEXT("dialogue opened"));
	ReleaseScheduleBody(TEXT("dialogue opened"));
	if (!Mind.Acquire(EElysiumBodyOwner::Dialogue, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
		DialogueBodyOwner, TEXT("dialogue open")))
	{
		return FElysiumBodyOwnerToken();
	}
	if (Motor && bPatrolActive)
	{
		Motor->Stop();
		bMoveIssued = false;
	}
	bInDialog = true;
	return DialogueBodyOwner;
}

void FElysiumNpc::EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent)
{
	if (DialogueBodyOwner.IsSet() && Token.Owner == DialogueBodyOwner.Owner
		&& Token.Generation == DialogueBodyOwner.Generation)
	{
		Mind.Release(DialogueBodyOwner, bSilent ? TEXT("dialogue silent close")
			: TEXT("dialogue normal close"));
		DialogueBodyOwner.Reset();
		if (Mind.Owner() == EElysiumBodyOwner::Patrol)
		{
			PatrolOwner = Mind.CurrentToken();
		}
	}
	if (bSilent)
	{
		bInDialog = false;
	}
	if (!bInDialog && (bPatrolActive || bUseInteresting))
	{
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
}

void FElysiumNpc::BeginDialog(EElysiumDialogOpenerKind Opener, int32 RawFlags,
	const FElysiumInputArgs& Args)
{
	if (IsInert() || bInDialog)
	{
		return;
	}
	// BeginDialogueBodySession performs the scripted-sequence cancellation and body transfer.
	// Keep that ownership transition in one place so direct World::OpenDialog uses the same path.
	FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	if (Motor && bPatrolActive)
	{
		Motor->Stop();
		bMoveIssued = false;
	}
	if (!BeginDialogueBodySession().IsSet())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s refused dialogue body ownership"),
			*DebugString());
		return;
	}
	DialogOpener = Opener;
	DialogFlags = RawFlags;
	DecodedDialogFlags = 0;
	if (DialogFlags != 0)
	{
		ElysiumStub::Fired(TEXT("field"), TEXT("CAI_BaseNPC.DialogOpenerInteger"),
			DebugString(), FString::Printf(TEXT("raw=%d"), DialogFlags),
			TEXT("RE46 — recover NPC+0x5bac before decoding flags"));
	}
	static const FName OnDialogBegin(TEXT("OnDialogBegin"));
	FireOutput(OnDialogBegin, Args.Activator);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s(raw=%d decoded=%d)"), *DebugString(),
		ElysiumDialogueCamera::LexToString(Opener), DialogFlags, DecodedDialogFlags);

	OpenConversation(Args.Activator, Opener);
}

void FElysiumNpc::InputStartPlayerDialog(const FElysiumInputArgs& Args)
{
	if (IsInert() || bInDialog)
	{
		return;
	}
	// The pinned ordinary handler stores this integer at NPC+0x5bac. Its bits are not yet named.
	BeginDialog(EElysiumDialogOpenerKind::Forced, Args.Param.ToInt(), Args);
}

void FElysiumNpc::InputStartPlayerDialogRemote(const FElysiumInputArgs& Args)
{
	if (IsInert() || bInDialog)
	{
		return;
	}
	// The pinned Remote handler never reads the input variant; authored `256` is intentionally
	// discarded before the common primitive sees it.
	BeginDialog(EElysiumDialogOpenerKind::Remote, 0, Args);
}

void FElysiumNpc::InputStartPlayerDialogUnforced(const FElysiumInputArgs& Args)
{
	if (IsInert() || bInDialog)
	{
		return;
	}
	// The extra player-refusal predicate is not yet recovered. Keep the unresolved gate visible;
	// no placement, acceptance, or flag meaning is inferred from it.
	ElysiumStub::Fired(TEXT("input"), TEXT("CAI_BaseNPC.StartPlayerDialogUnforcedGate"),
		DebugString(), ElysiumStub::DescribeInput(Args),
		TEXT("RE46 — recover the player-side unforced refusal predicate"));
	BeginDialog(EElysiumDialogOpenerKind::Unforced, Args.Param.ToInt(), Args);
}

void FElysiumNpc::InputEndDialog(const FElysiumInputArgs& Args)
{
	if (!bInDialog)
	{
		return;
	}
	if (DialogueBodyOwner.IsSet())
	{
		EndDialogueBodySession(DialogueBodyOwner, /*bSilent=*/false);
	}
	bInDialog = false;
	++TimesTalked;
	static const FName OnDialogEnd(TEXT("OnDialogEnd"));
	FireOutput(OnDialogEnd, Args.Activator);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s EndDialog (times_talked=%d)"), *DebugString(), TimesTalked);
	// Unconditional: leaving a conversation releases the stance machine's talking branch, so a
	// standing character has a decision to make on the very next think.
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

bool FElysiumNpc::OpenConversation(const FElysiumEntityHandle& Activator, EElysiumDialogOpenerKind Opener)
{
	if (!World || !Def)
	{
		return false;
	}
	const FString DialogName = Def->Keys.FindRef(TEXT("dialogname"));
	if (DialogName.IsEmpty())
	{
		return false;   // an NPC with no dialogue file — nothing to open
	}

	const FString Path = FElysiumContentPaths::DlgFromDialogname(DialogName);
	TSharedRef<FElysiumDlgFile> DlgFile = MakeShared<FElysiumDlgFile>();
	FString Err;
	if (!FElysiumDlgFile::LoadFile(Path, DlgFile.Get(), &Err))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s dialog load failed: %s"), *DebugString(), *Err);
		return false;
	}

	// Player gender + clan drive text selection: VtMB shows col-2 for a female PC, and the col-12
	// Malkavian variant for a Malkavian PC. Clan is the 2..8 sheet encoding (Malkavian = 4).
	const UElysiumGameStateSubsystem* GameState = World->GetGameState();
	const bool bMale = GameState ? GameState->PlayerSheet().IsMale() : true;
	const bool bMalk = GameState
		&& GameState->PlayerSheet().Clan() == FElysiumSheet::ClanFromName(TEXT("Malkavian"));
	const FElysiumEntityHandle Self = Handle;
	FElysiumEntityWorld* W = World;

	// Field-4 conditions eval, field-4(NPC)/field-5 actions exec — both through the installed host
	// (EvalCondition also execs statements), so they land in the same `G` the level script reads and
	// obey the same live/off switch and eval log as field-6. dlgexpr -> Python via the normalizer.
	auto Cond = [W, Self, Activator](const FString& Raw) -> bool
	{
		return W->EvalCondition(ElysiumDlgExpr::ConditionToPython(Raw), Self, Activator).ToBool();
	};
	auto Act = [W, Self, Activator](const FString& Raw)
	{
		W->EvalCondition(ElysiumDlgExpr::ActionToPython(Raw), Self, Activator);
	};
	const FString DialogUseScript = UseScript;
	auto StartFallback = [W, Self, Activator, DialogUseScript]() -> TOptional<int32>
	{
		if (DialogUseScript.IsEmpty())
		{
			return TOptional<int32>();   // no usescript: retail's default is line 1
		}
		const FElysiumVariant Result = W->EvalCondition(DialogUseScript, Self, Activator);
		// CallPyDialogFunc accepts only a Python int; every other result (including an error/None)
		// returns 0 and lets CDialog::Acquire apply its first-stored-line fallback.
		return Result.IsInt() ? Result.ToInt() : 0;
	};

	TSharedRef<FElysiumDlgConversation> Conv =
		MakeShared<FElysiumDlgConversation>(DlgFile, bMale, bMalk, MoveTemp(Cond), MoveTemp(Act),
			MoveTemp(StartFallback));
	Conv->Start();
	World->OpenDialog(Self, Conv, Opener, DialogFlags, DefaultCamera, DialogueBodyOwner);
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s opened dialogue '%s' (%d rows)"),
		*DebugString(), *DialogName, DlgFile->Lines.Num());
	return true;
}

void FElysiumNpc::SeedSheet()
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	const FElysiumStatTable* Table = GameState ? GameState->Stats() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	FElysiumClanTemplate Resolved;
	const bool bResolvedTemplate = !StatTemplate.IsEmpty()
		&& Rules && Rules->Clans().Resolve(StatTemplate, Resolved);
	bFastFood = false;
	bHasKindredTemplate = false;
	bKindredTemplate = false;
	for (bool& bHas : bHasDamageFilter) { bHas = false; }
	if (!Table)
	{
		return;   // no rulebook: the sheet stays zeroed and the damage path stays fail-closed
	}
	Sheet.SeedFrom(*Table);

	if (!StatTemplate.IsEmpty())
	{
		if (bResolvedTemplate)
		{
			ApplyResolvedTemplate(Resolved, Table);
			// A template names its clan's `TraitEffectGroup`; that group is where the clan's
			// gifts and banes live, for an NPC exactly as for the player.
			const FString ClanEffect = Resolved.GeneralStr(TEXT("ClanEffect"));
			if (!ClanEffect.IsEmpty())
			{
				Effects.AddUnique(ClanEffect);
			}
			RebuildEffects();
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s stattemplate '%s' resolves to nothing"),
				*DebugString(), *StatTemplate);
		}
	}
	SyncHealthFromSheet();
}

void FElysiumNpc::Spawn()
{
	SeedSheet();
	// Keyfields (model/angles/use_interesting/stattemplate) are already applied. Stand the body:
	// out/npc/<stem>.glb, playing the standing idle `default_disposition` selects, spread across
	// the three VtMB authors per disposition and seeded from this entity's own index — a cop that
	// stood with its arms crossed must still be doing so after a reload. All of that is
	// FElysiumAnimating's; the cvar is this leaf's A/B.
	if (CVarNpcBodies.GetValueOnGameThread() == 0)
	{
		return;   // gated off: a bodiless record whose I/O still resolves
	}
	BuildBody();
	BuildMotor();
	// Spawn constructs presentation only. Activate arms the first deterministic admission think;
	// no autonomous decision, controller wake or activity write occurs in this phase.
}

void FElysiumNpc::Activate()
{
	SeedPlayerRelationship();
	// Cycle 4: `InitPerceptionDistances` runs once, on authored data that is already applied, and
	// the hearing cursor starts at the live head so an NPC never hears the map's own load.
	Senses.ResolveTuning(*this);
	Senses.StartSoundCursorAtHead(*this);
	Mind.ArmAdmission();
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumNpc::OnRuntimeModelChanged()
{
	if (CVarNpcBodies.GetValueOnGameThread() == 0)
	{
		return;
	}
	// The swap destroys the motor the beat is steering, so release the hold first — the beat
	// reads Unsupported next tick and finishes on the placement fallback.
	EndScriptMove();
	DestroyMotor();
	FElysiumAnimating::OnRuntimeModelChanged();
	BuildMotor();
}

void FElysiumNpc::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	if (IsInert())
	{
		if (DialogueBodyOwner.IsSet())
		{
			if (World && World->GetOpenDialogOwner() == Handle)
			{
				World->CloseDialog(/*bSilent=*/true);
			}
			else
			{
				EndDialogueBodySession(DialogueBodyOwner, /*bSilent=*/true);
			}
		}
		EndScriptMove();
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		EndScriptedSchedule(bDead ? TEXT("death") : TEXT("dormancy"));
		ReleaseScheduleBody(bDead ? TEXT("death") : TEXT("dormancy"));
		Schedule.Clear();
		CombatSelector.Reset();
		Mind.Invalidate(bDead ? TEXT("death") : TEXT("dormancy"), bDead);
		PatrolOwner.Reset();
		AmbientOwner.Reset();
		SequenceOwner.Reset();
		ScheduleOwner.Reset();
		ScriptedScheduleOwner.Reset();
		ScriptedScheduleOrder.Reset();
		DialogueBodyOwner.Reset();
		// The beat's own ReleaseNpc still runs; it must find nothing left to give back rather
		// than releasing a token this invalidation already retired.
		bScriptBodyRequested = false;
		bScriptBodyHeld = false;
	}
	else
	{
		// Waking, not going dormant: every NPC gets a think back, standing ones included.
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
	if (Motor)
	{
		Motor->SetEnabled(!IsInert());
	}
}

void FElysiumNpc::Serialize(FElysiumSaveArchive& Ar)
{
	Ar << PatrolType;
	Ar << PatrolPath;
	Ar << PatrolIndex;
	uint8 Active = bPatrolActive ? 1 : 0;
	Ar << Active;
	if (Ar.IsLoading())
	{
		// A restored body stands where the payload puts it, so no in-flight travel survives
		// the load. The beat that owned it re-issues its own move (FElysiumScriptedSequence).
		EndScriptMove();
	}
	if (Ar.IsLoading() && Ar.AtEnd())
	{
		bPatrolActive = Active != 0 && ResolvePatrolPoints();
		bMoveIssued = false;
		bWalkingAnimation = false;
		if (bPatrolActive)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		return; // compatibility with snapshots written before ambient-place state existed
	}
	uint8 SavedAmbientPhase = static_cast<uint8>(AmbientPhase);
	Ar << SavedAmbientPhase;
	Ar << CurrentSpotIndex;
	Ar << AmbientLeaveAt;
	Ar << AmbientNextActivityAt;
	Ar << AmbientActivityCycle;
	uint8 SavedAmbientArrived = bAmbientArrived ? 1 : 0;
	Ar << SavedAmbientArrived;
	if (Ar.IsLoading())
	{
		bPatrolActive = Active != 0 && ResolvePatrolPoints();
		bMoveIssued = false;
		bWalkingAnimation = false;
		AmbientPhase = static_cast<EAmbientPhase>(SavedAmbientPhase);
		bAmbientArrived = SavedAmbientArrived != 0;
		if (bPatrolActive)
		{
			CurrentSpotIndex = INDEX_NONE;
			AmbientPhase = EAmbientPhase::None;
			bAmbientArrived = false;
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		if (!bPatrolActive && AmbientPhase != EAmbientPhase::None)
		{
			FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
			if (!Spot || !Spot->Claim(Handle))
			{
				CurrentSpotIndex = INDEX_NONE;
				AmbientPhase = EAmbientPhase::None;
				bAmbientArrived = false;
			}
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	// Version 13 appends the maker relationship after the pre-existing NPC leaf. Older saves
	// deliberately restore legacy runtime NPCs unowned rather than guessing a maker association.
	if (Ar.Version() >= FElysiumSaveVersion::NpcMaker)
	{
		Ar << OwnerEntity;
		uint8 OwnerNotified = bOwnerTerminationNotified ? 1 : 0;
		Ar << OwnerNotified;
		if (Ar.IsLoading())
		{
			OwnerEntity = World
				? World->RebaseSavedHandle(OwnerEntity) : FElysiumEntityHandle::Invalid();
			bOwnerTerminationNotified = OwnerNotified != 0;
		}
	}

	if (Ar.Version() >= FElysiumSaveVersion::NpcMind)
	{
		uint8 SavedState = static_cast<uint8>(Mind.State());
		EElysiumBodyOwner ResumableOwner = Mind.Owner();
		if (!FElysiumNpcMind::IsResumableOwner(ResumableOwner))
		{
			ResumableOwner = EElysiumBodyOwner::None;
		}
		uint8 SavedOwner = static_cast<uint8>(ResumableOwner);
		Ar << SavedState;
		Ar << SavedOwner;
		if (Ar.IsLoading())
		{
			const EElysiumNpcState State = static_cast<EElysiumNpcState>(SavedState);
			EElysiumBodyOwner Owner = static_cast<EElysiumBodyOwner>(SavedOwner);
			if (!FElysiumNpcMind::IsSupportedState(State)
				|| !FElysiumNpcMind::IsResumableOwner(Owner))
			{
				Owner = EElysiumBodyOwner::None;
			}
			if (Owner == EElysiumBodyOwner::Patrol && !bPatrolActive)
			{
				Owner = EElysiumBodyOwner::None;
			}
			if (Owner == EElysiumBodyOwner::Ambient && AmbientPhase == EAmbientPhase::None)
			{
				Owner = EElysiumBodyOwner::None;
			}
			Mind.Restore(State, Owner);
			PatrolOwner = Owner == EElysiumBodyOwner::Patrol
				? Mind.CurrentToken() : FElysiumBodyOwnerToken();
			AmbientOwner = Owner == EElysiumBodyOwner::Ambient
				? Mind.CurrentToken() : FElysiumBodyOwnerToken();
			// A restore never resumes `Schedule` ownership either: the program restarts from its
			// first task below, and its first movement task takes the claim again.
			ScheduleOwner.Reset();
			// Nor `ScriptedSchedule`. The order behind it is a live goal handle and a route resolved
			// out of the previous map epoch, so it is session state (the reasoning is on
			// `FElysiumScriptedScheduleOrder`); what the push durably changed is the mind state
			// restored just above and, for mode 3, the committed enemy the senses block carries.
			ScriptedScheduleOwner.Reset();
			ScriptedScheduleOrder.Reset();
			CombatSelector.Reset();
			// A restore never resumes `Sequence` ownership, so any token from before the load is
			// retired with it. The request survives: whichever order the two entities restore in,
			// a beat that re-stamps its queue lock has its claim taken again on the next think.
			SequenceOwner.Reset();
			bScriptBodyHeld = false;
		}
	}

	// The schedule's IDENTITY is saved; its task position is not, and that is deliberate. A task
	// holds a playing clip, a pending motor move or a wall-clock deadline, and none of those
	// survive a load -- so resuming at task 3 would hold a pose nothing is playing. Restarting
	// the same program preserves the intent (an NPC mid-lookaround resumes looking around rather
	// than dropping to its stance) without pretending the state under it survived.
	if (Ar.Version() >= FElysiumSaveVersion::NpcSchedule)
	{
		uint8 SavedSchedule = static_cast<uint8>(Schedule.Current);
		Ar << SavedSchedule;
		if (Ar.IsLoading())
		{
			Schedule.Clear();
			const EElysiumScheduleId Restored = static_cast<EElysiumScheduleId>(SavedSchedule);
			if (ElysiumAiScriptedSchedule::IsScriptedProgram(Restored))
			{
				// A scripted director's program is not restartable without the order that pushed it,
				// and that order is not save state. `SaveBlockReason` refuses a save while the
				// `ScriptedSchedule` owner holds the body, so a payload can only carry this program
				// from the narrow window between the push and its first movement claim. Restarting it
				// would fail its first task by name on the next think; refusing it here says so once,
				// and the NPC selects normally instead.
				RecordScheduleEvent(FString::Printf(
					TEXT("restore refused %s: the pushed order it needs is not save state"),
					ElysiumScheduleName(Restored)));
			}
			else if (Restored != EElysiumScheduleId::None && ElysiumScheduleFor(Restored) != nullptr)
			{
				ElysiumSchedule::Start(Schedule, Restored, *this);
			}
			// NextThink is deliberately NOT touched here. The base record serializes it, so a
			// restored NPC already carries the cadence it was saved on -- rewriting it to "now"
			// would discard saved state and make the payload fail its own round trip.
		}
	}

	if (Ar.Version() >= FElysiumSaveVersion::NpcSocial)
	{
		Relationships.Serialize(Ar);
		if (Ar.IsLoading() && World)
		{
			Relationships.Rebase(*World);
		}
	}

	// Cycle 4. The memory is what survives losing sight, so it is what a save has to carry; the
	// resolved perception pair is not saved because it is derived from the keyfields the field
	// walk already restored.
	if (Ar.Version() >= FElysiumSaveVersion::NpcSenses)
	{
		Senses.Serialize(Ar, *this);
	}

	// Cycle 6. The loadout latch, and only the latch: the weapon it granted is a real runtime entity
	// the snapshot already carries with its own owner field, so re-running the resolution on a
	// restore would hand a restored NPC a second gun. A payload that predates this restores the
	// latch CLEAR, which is correct for it — an older payload was written by a build that granted
	// nothing, so the loadout has genuinely not run for that NPC.
	if (Ar.Version() >= FElysiumSaveVersion::NpcCombat)
	{
		uint8 LoadoutResolved = bLoadoutResolved ? 1 : 0;
		Ar << LoadoutResolved;
		if (Ar.IsLoading())
		{
			bLoadoutResolved = LoadoutResolved != 0;
		}
	}
	else if (Ar.IsLoading())
	{
		bLoadoutResolved = false;
	}

	if (Ar.IsLoading())
	{
		// Conditions are not saved (`ElysiumNpcConditions.h`): they are rebuilt from the memory
		// above on the first think after the load. What has to be stamped is the pass CLOCK — the
		// edge every stimulus producer measures against. Leaving it at -1 would make an hour-old
		// remembered gunshot look new and promote a restored NPC to alert on the strength of it.
		Cognition.Conditions.Reset();
		Cognition.GatheredAt = World ? World->NowSeconds() : 0.0;
	}
}

const TCHAR* FElysiumNpc::SaveBlockReason() const
{
	switch (Mind.Owner())
	{
	case EElysiumBodyOwner::Dialogue:          return TEXT("a conversation is open");
	case EElysiumBodyOwner::Sequence:          return TEXT("a scripted sequence is active");
	case EElysiumBodyOwner::ScriptedSchedule:  return TEXT("a scripted schedule is active");
	case EElysiumBodyOwner::Follower:          return TEXT("a follower session is active");
	default:                                   return nullptr;
	}
}

void FElysiumNpc::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
		? TEXT("(all)") : InterestingPlaceGroups);
	Out.Emplace(TEXT("In dialog"), bInDialog
		? FString::Printf(TEXT("YES (%s raw=%d decoded=%d)"),
			ElysiumDialogueCamera::LexToString(DialogOpener), DialogFlags, DecodedDialogFlags)
		: TEXT("no"));
	Out.Emplace(TEXT("default_camera"), DefaultCamera.IsEmpty() ? TEXT("(none)") : DefaultCamera);
	Out.Emplace(TEXT("Times talked"), FString::FromInt(TimesTalked));
	const FElysiumEntityHandle Player = World ? World->PlayerHandle()
		: FElysiumEntityHandle::Invalid();
	Out.Emplace(TEXT("Disposition"), FString::Printf(TEXT("%s L%d%s"), *Disposition,
		DispositionLevel, IsDispositionTalking() ? TEXT(" talking") : TEXT("")));
	Out.Emplace(TEXT("Relationship to player"), FString::Printf(
		TEXT("%s priority %d (table %d entity / %d class)"),
		ElysiumRelationships::LexToString(Relationships.Resolve(Player, TEXT("player"))),
		Relationships.ResolvePriority(Player, TEXT("player")),
		Relationships.NumEntityRules(), Relationships.NumClassRules()));
	if (!StatTemplate.IsEmpty())
	{
		Out.Emplace(TEXT("Stat template"), StatTemplate);
	}
	Out.Emplace(TEXT("Fast food"), bFastFood ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal (standing)") : TEXT("(none)"));
	Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
	const TCHAR* Admission = Mind.Admission() == FElysiumNpcMind::EAdmission::Spawned
		? TEXT("spawned") : (Mind.Admission() == FElysiumNpcMind::EAdmission::Armed
			? TEXT("armed") : TEXT("admitted"));
	Out.Emplace(TEXT("Mind"), FString::Printf(TEXT("%s current=%s ideal=%s"), Admission,
		LexToString(Mind.State()), LexToString(Mind.IdealState())));
	Out.Emplace(TEXT("Body owner"), FString::Printf(TEXT("%s gen=%u parked=%s"),
		LexToString(Mind.Owner()), Mind.Generation(), LexToString(Mind.SuspendedOwner())));
	Out.Emplace(TEXT("Mind transition"), Mind.LastTransition().IsEmpty()
		? TEXT("(none)") : Mind.LastTransition());
	Out.Emplace(TEXT("Scripted schedule"), ScriptedScheduleOrder.IsSet()
		? FString::Printf(TEXT("mode %d (%s, %s) leg %d/%d goal %s"), ScriptedScheduleOrder.Mode,
			ElysiumAiScriptedSchedule::ModeName(ScriptedScheduleOrder.Mode),
			ScriptedScheduleOrder.bRun ? TEXT("run") : TEXT("walk"),
			ScriptedScheduleOrder.Leg, ScriptedScheduleOrder.Route.Num(),
			World ? *World->DescribeHandle(ScriptedScheduleOrder.Goal) : TEXT("(no world)"))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Patrol"), bPatrolActive
		? FString::Printf(TEXT("point %d/%d: %s"), PatrolIndex + 1, PatrolPoints.Num(), *PatrolPath)
		: TEXT("inactive"));
	Out.Emplace(TEXT("Ambient place"), CurrentSpotIndex == INDEX_NONE
		? TEXT("searching")
		: FString::Printf(TEXT("#%d phase=%d"), CurrentSpotIndex,
			static_cast<int32>(AmbientPhase)));
	Out.Emplace(TEXT("Scripted move"), ScriptPhase == EScriptPhase::None
		? TEXT("(free)")
		: FString::Printf(TEXT("%s to %s, %.0fcm out"),
			ScriptPhase == EScriptPhase::Travel ? TEXT("travelling") : TEXT("facing"),
			*ScriptMark.ToString(), FVector::Dist2D(Origin, ScriptMark)));

	// Cycle 4 — the sensory transaction.
	Out.Emplace(TEXT("Perception"), FString::Printf(
		TEXT("npc_perception %d, vision %.0fcm, hearing %.2fx%s"), AuthoredPerception,
		Senses.Perception.VisionDistanceCm, Senses.Perception.HearingScalar,
		Senses.Perception.bUsedFallback ? TEXT(" (table fallback)") : TEXT("")));
	const FElysiumNpcMemory& Mem = Senses.Memory;
	Out.Emplace(TEXT("Closest player"), Mem.ClosestPlayer.IsSet()
		? FString::Printf(TEXT("%.0fcm, %s%s%s"), Mem.ClosestPlayerDistanceCm,
			Mem.bPlayerLos ? TEXT("SEEN") : TEXT("unseen"),
			Mem.bPlayerInCone ? TEXT(", in cone") : TEXT(", out of cone"),
			Mem.bPlayerInOuterBand ? TEXT(", outer band") : TEXT(""))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Enemy"), Mem.Enemy.IsSet()
		? FString::Printf(TEXT("%s (%s, %d failed LOS checks%s)"), *Mem.Enemy.ToString(),
			Mem.bEnemyOccluded ? TEXT("OCCLUDED") : TEXT("has LOS"), Mem.EnemyLosFailures,
			Mem.bEnemyEluded ? TEXT(", ELUDED") : TEXT(""))
		: TEXT("(none)"));
	Out.Emplace(TEXT("Last enemy"), Mem.LastEnemy.IsSet()
		? Mem.LastEnemy.ToString() : FString(TEXT("(none)")));
	Out.Emplace(TEXT("Enemy sightings"), FString::FromInt(EnemySightings));
	Out.Emplace(TEXT("Last heard"), Mem.LastHeardTime < 0.0
		? TEXT("(nothing)")
		: FString::Printf(TEXT("%s at %s, t=%.2f"), *Mem.LastHeardCategory,
			*Mem.LastHeardPosition.ToString(), Mem.LastHeardTime));
	Out.Emplace(TEXT("Last damage"), Mem.LastDamageTime < 0.0
		? TEXT("(none)")
		: FString::Printf(TEXT("%d from %s at t=%.2f (window sum %d)"), Mem.LastDamageAmount,
			*Mem.LastDamageAttacker.ToString(), Mem.LastDamageTime,
			Mem.RepeatedDamageAccumulated));

	// Cycle 5 — the decision pass.
	Out.Emplace(TEXT("Conditions"), FString::Printf(TEXT("%s (gathered t=%.2f)"),
		*Cognition.Conditions.Describe(), Cognition.GatheredAt));
	Out.Emplace(TEXT("no_alert_state"), bNoAlertState ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Schedule"), Schedule.IsRunning()
		? FString::Printf(TEXT("%s (0x%x) task %d"), ElysiumScheduleName(Schedule.Current),
			ElysiumScheduleNumber(Schedule.Current), Schedule.TaskIndex)
		: TEXT("(none)"));

	// Cycle 6 — the loadout and the combat policy it selects.
	const FElysiumItem* Active = Inventory.Active(*this);
	const ElysiumNpcCond::ECapability Capability = ElysiumNpcCond::WeaponCapability(*this);
	Out.Emplace(TEXT("Weapon"), FString::Printf(TEXT("%s — capability %s (0x%x)"),
		Active != nullptr ? *Active->ClassName() : TEXT("(none)"),
		ElysiumNpcCond::CapabilityName(Capability),
		ElysiumNpcCond::CapabilityBits(Capability)));
	Out.Emplace(TEXT("Loadout"), FString::Printf(TEXT("%s authored '%s'%s%s"),
		bLoadoutResolved ? TEXT("resolved,") : TEXT("PENDING,"),
		AdditionalEquipment.IsEmpty() ? TEXT("(none)") : *AdditionalEquipment,
		AlternateEquipment.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", alternate '%s' (unread)"),
			*AlternateEquipment),
		bCantDropWeapons ? TEXT(", cantdropweapons") : TEXT("")));
	Out.Emplace(TEXT("Detected attack"), Mem.DetectedAttackTime < 0.0
		? TEXT("(none)")
		: FString::Printf(TEXT("%s at t=%.2f"), *Mem.DetectedAttackAttacker.ToString(),
			Mem.DetectedAttackTime));
}

// ============================================================================================
// npc_VPlayerController
// ============================================================================================

void FElysiumPlayerControllerNpc::Spawn()
{
	if (CVarNpcBodies.GetValueOnGameThread() != 0)
	{
		BuildBody();
		BuildControllerMotor();
	}
}

void FElysiumPlayerControllerNpc::OnRuntimeModelChanged()
{
	if (CVarNpcBodies.GetValueOnGameThread() != 0)
	{
		EndScriptMove();
		DestroyMotor();
		FElysiumAnimating::OnRuntimeModelChanged();
		BuildControllerMotor();
	}
}

void FElysiumPlayerControllerNpc::SetIgnoreCharacterCollision(bool)
{
	// The duplicate navigates against the world but never becomes a second solid character.
	if (Motor)
	{
		Motor->SetIgnoreCharacterCollision(true);
	}
}

void FElysiumPlayerControllerNpc::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	if (IsInert())
	{
		EndScriptMove();
	}
	if (bDead)
	{
		DestroyMotor();
	}
	else if (Motor)
	{
		Motor->SetEnabled(!IsInert());
	}
}

void FElysiumPlayerControllerNpc::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Role"), TEXT("player controller (non-AI, non-solid)"));
	Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
	Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal") : TEXT("(none)"));
	Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
}

void FElysiumPlayerControllerNpc::BuildControllerMotor()
{
	BuildMotor();
	SetIgnoreCharacterCollision(true);
}
