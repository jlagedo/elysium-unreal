#include "ElysiumEntityWorld.h"

#include "ElysiumPlayer.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumSkillClasses.h"

void FElysiumEntityWorld::RouteEntityTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	const uint64 TouchKey = (static_cast<uint64>(static_cast<uint32>(Brush.Index)) << 32)
		| static_cast<uint32>(Activator.Index);
	if (!bBegin)
	{
		// Collision is switched off as part of Hide/Kill, after the entity has become inert. Release
		// the physical pair before the liveness gate so a later Unhide while still intersecting can
		// produce a fresh begin edge.
		if (ActiveTouches.Remove(TouchKey) == 0)
		{
			return;
		}
	}
	// Engine overlap callbacks can arrive while procedural collision and the pawn placement are
	// still settling. Dormant begins are deliberately forgotten: activation reconciles final
	// containment after authoritative placement. Ends still release an already-retained pair above,
	// even while the gameplay gate is closed or the brush has become inert.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	FElysiumEntity* E = Resolve(Brush);
	if (!E || E->IsInert())
	{
		// A dormant/dead brush cannot be touched (R6). On the end edge this is also the retail
		// asymmetry: `~CBaseEntity` reaches PhysicsRemoveTouchedList, which notifies the OTHER side
		// of each link and frees it without PhysicsRemoveToucher — so a dying trigger never receives
		// its own EndTouch, and a self-removing trigger_once emits no final OnEndTouch to occupants
		// still inside it (entity_io.md). Kill() flips bDead before releasing contacts, which is
		// what routes the release here (`Elysium.Substrate.DyingTriggerEndTouch`).
		return;
	}

	// Begin/end are edges, not level-triggered calls. Engine movement normally supplies exactly
	// one of each, but a teleport reconciliation also asks which brushes contain the player after
	// the transform. Collapse that second observation here so an authored trigger never double-
	// fires; an end releases the pair so a later genuine re-entry remains an edge.
	if (bBegin)
	{
		if (!E->CanBeginTouch(Activator))
		{
			return;
		}
		if (ActiveTouches.Contains(TouchKey))
		{
			return;
		}
		ActiveTouches.Add(TouchKey);
	}

	if (bBegin)
	{
		++TouchBeginCount;
		E->OnTouchStart(Activator);
	}
	else
	{
		++TouchEndCount;
		E->OnTouchEnd(Activator);
	}
	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) touch %s %s"),
		NowSeconds(), bBegin ? TEXT("begin") : TEXT("end"), *E->DebugString());
}

void FElysiumEntityWorld::EndBrushTouches(const FElysiumEntityHandle& Brush)
{
	if (!Brush.IsSet() || Brush.Epoch != Epoch)
	{
		return;
	}
	TArray<int32> ActivatorIndices;
	for (uint64 Key : ActiveTouches)
	{
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (BrushIndex == Brush.Index)
		{
			ActivatorIndices.Add(static_cast<int32>(static_cast<uint32>(Key)));
		}
	}
	ActivatorIndices.Sort();
	for (int32 ActivatorIndex : ActivatorIndices)
	{
		RouteBrushTouch(Brush, FElysiumEntityHandle(ActivatorIndex, Epoch), /*bBegin*/ false);
	}
}

// --- Player interaction ----------------------------------------------------------------

namespace
{
	constexpr double GPromptFadeInSeconds = 0.10;
	constexpr double GPromptFadeOutSeconds = 0.15;
}

void FElysiumEntityWorld::QueuePlayerUseEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingUseEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::ReconcilePlayerTouches(TConstArrayView<FElysiumEntityHandle> CurrentBrushes)
{
	if (!bActive || !Player.IsSet() || !IsTriggerResolutionEnabled())
	{
		return;
	}

	TSet<int32> CurrentIndices;
	for (const FElysiumEntityHandle& Brush : CurrentBrushes)
	{
		if (FElysiumEntity* E = Resolve(Brush); E && E->CanBeginTouch(Player))
		{
			CurrentIndices.Add(Brush.Index);
		}
	}

	TArray<int32> Ends;
	for (uint64 Key : ActiveTouches)
	{
		const int32 ActivatorIndex = static_cast<int32>(static_cast<uint32>(Key));
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (ActivatorIndex == Player.Index && !CurrentIndices.Contains(BrushIndex))
		{
			Ends.Add(BrushIndex);
		}
	}
	TArray<int32> Begins = CurrentIndices.Array();
	Begins.RemoveAll([this](int32 BrushIndex)
	{
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(BrushIndex)) << 32)
			| static_cast<uint32>(Player.Index);
		return ActiveTouches.Contains(Key);
	});
	Ends.Sort();
	Begins.Sort();
	for (int32 BrushIndex : Ends)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ false);
	}
	for (int32 BrushIndex : Begins)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ true);
	}
}

void FElysiumEntityWorld::QueuePlayerFeedEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingFeedEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::UpdatePlayerFeed()
{
	if (PendingFeedEdges.IsEmpty())
	{
		return;
	}
	TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingFeedEdges);
	PendingFeedEdges.Reset();
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}

	for (const EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			// The low-level `-feed` edge only releases the command button. Feeding is a toggle-style
			// action: the first PRESS starts it and a later PRESS requests the paired release family.
			// Treating this edge as cancellation makes the patch's 0.1-second `vm_feed` tap abort
			// before the first blood pulse.
			continue;
		}
		// A second press while this player is the feeder requests the ordinary release transition.
		// The continuation latch belongs to the paired action; it is not the physical button's held
		// state. A victim-role player is not part of the ordinary slice and cannot cancel its attacker.
		if (PlayerEnt->IsFeedPaired())
		{
			if (!PlayerEnt->FeedState.bVictim && PlayerEnt->FeedState.bContinuation)
			{
				PlayerEnt->SetFeedContinuation(false);
				UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed stop requested"));
			}
			continue;
		}
		// One unpaired press is one `Replenish` request. A miss does not become a held retry.
		FElysiumEntityHandle Candidate = FElysiumEntityHandle::Invalid();
		if (IElysiumEmbodiment* Bodily = Embodiment())
		{
			Candidate = Bodily->QueryFeedTarget();
		}
		FElysiumEntity* TargetEnt = Resolve(Candidate);
		FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
		if (!Victim)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed missed: no live target"));
			UE_LOG(LogElysiumWorld, Verbose,
				TEXT("%s feed request missed: candidate #%d is not a live combat character"),
				*PlayerEnt->DebugString(), Candidate.IsSet() ? Candidate.Index : INDEX_NONE);
			continue;   // nothing in the hull, or what is there is not a character
		}
		PlayerEnt->AttemptFeed(*Victim);
	}
}

float FElysiumEntityWorld::InteractionPromptAlpha(double Now) const
{
	if (!InteractionPrompt.DisplayOwner.IsSet())
	{
		return 0.0f;
	}
	const double Duration = InteractionPrompt.bFadingIn
		? GPromptFadeInSeconds : GPromptFadeOutSeconds;
	const float Target = InteractionPrompt.bFadingIn ? 1.0f : 0.0f;
	const float T = Duration > 0.0
		? FMath::Clamp(static_cast<float>((Now - InteractionPrompt.TransitionTime) / Duration), 0.0f, 1.0f)
		: 1.0f;
	return FMath::Lerp(InteractionPrompt.StartAlpha, Target, T);
}

void FElysiumEntityWorld::TransitionUseFocus(const FElysiumUseCandidate* Candidate)
{
	const FElysiumEntityHandle Next = Candidate ? Candidate->Owner : FElysiumEntityHandle::Invalid();
	if (Next == FocusedUsable)
	{
		if (FElysiumEntity* Current = Resolve(FocusedUsable))
		{
			InteractionPrompt.Icon = Current->ResolveUseIcon(Player);
			InteractionPrompt.bLocked = Current->IsUseLocked();
		}
		if (Candidate)
		{
			FocusContext.AnchorPoint = Candidate->AnchorPoint;
			FocusContext.Selection = Candidate->Selection;
		}
		return;
	}

	const double Now = NowSeconds();
	const float CurrentAlpha = InteractionPromptAlpha(Now);
	if (FElysiumEntity* Old = Resolve(FocusedUsable))
	{
		Old->OnUseCursorLeave();
	}
	FocusedUsable = Next;
	FocusContext = FElysiumUseContext();
	FocusContext.Activator = Player;
	FocusContext.Owner = Next;
	FocusContext.TimeSeconds = Now;
	if (Candidate)
	{
		FocusContext.AnchorPoint = Candidate->AnchorPoint;
		FocusContext.Selection = Candidate->Selection;
	}

	if (FElysiumEntity* New = Resolve(FocusedUsable))
	{
		New->OnUseCursorEnter();
		InteractionPrompt.DisplayOwner = New->Handle;
		InteractionPrompt.Icon = New->ResolveUseIcon(Player);
		InteractionPrompt.bLocked = New->IsUseLocked();
		InteractionPrompt.bFadingIn = true;
		InteractionPrompt.StartAlpha = CurrentAlpha;
		InteractionPrompt.TransitionTime = Now;
	}
	else if (InteractionPrompt.DisplayOwner.IsSet())
	{
		InteractionPrompt.bFadingIn = false;
		InteractionPrompt.StartAlpha = CurrentAlpha;
		InteractionPrompt.TransitionTime = Now;
	}

	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) interaction-focus -> %s"),
		Now, FocusedUsable.IsSet() ? *DescribeHandle(FocusedUsable) : TEXT("<none>"));
}

void FElysiumEntityWorld::EndActiveUse(EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet())
	{
		return;
	}
	const FActiveUse Ending = ActiveUse.GetValue();
	ActiveUse.Reset();
	if (FElysiumEntity* Entity = Resolve(Ending.Context.Owner))
	{
		Entity->EndPlayerUse(Ending.Context, Reason);
	}
	LastUseOutcome = (Reason == EElysiumUseEndReason::Released
		|| Reason == EElysiumUseEndReason::Completed)
		? EElysiumUseOutcome::Completed : EElysiumUseOutcome::Cancelled;
}

bool FElysiumEntityWorld::EndPlayerUseSession(const FElysiumEntityHandle& OwnerHandle,
	EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet() || (OwnerHandle.IsSet() && ActiveUse->Context.Owner != OwnerHandle))
	{
		return false;
	}
	EndActiveUse(Reason);
	return true;
}

FElysiumUseBeginResult FElysiumEntityWorld::BeginPlayerUseSession(
	const FElysiumEntityHandle& OwnerHandle, const FElysiumEntityHandle& Activator)
{
	if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
	{
		LastUseOutcome = EElysiumUseOutcome::Busy;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumUseContext Context;
	Context.Activator = Activator;
	Context.Owner = OwnerHandle;
	Context.TimeSeconds = NowSeconds();
	if (!Entity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("programmatic +use session failed: owner %s does not resolve"),
			*OwnerHandle.ToString());
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->CanPlayerFocus(Context))
	{
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->UseFilterName.IsEmpty())
	{
		FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
		if (!Filter)
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
				*Entity->DebugString(), *Entity->UseFilterName);
		}
		if (Filter && !Filter->PassesFilter(Activator))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
		}
	}
	const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(Context);
	if (Result.Outcome == EElysiumUseOutcome::SessionStarted
		&& Result.SessionKind != EElysiumUseSessionKind::None)
	{
		FActiveUse Session;
		Session.Context = Context;
		Session.Kind = Result.SessionKind;
		ActiveUse = Session;
	}
	LastUseOutcome = Result.Outcome;
	return Result;
}

void FElysiumEntityWorld::UpdatePlayerInteraction()
{
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		PendingUseEdges.Reset();
		TransitionUseFocus(nullptr);
		return;
	}

	if (ActiveUse.IsSet())
	{
		FElysiumEntity* ActiveEntity = Resolve(ActiveUse->Context.Owner);
		FElysiumEntity* ActiveUser = Resolve(ActiveUse->Context.Activator);
		if (!ActiveEntity || ActiveEntity->IsInert() || !ActiveUser || ActiveUser->IsInert())
		{
			EndActiveUse(EElysiumUseEndReason::TargetInvalid);
		}
	}

	FElysiumUseQueryResult Query;
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Query = Bodily->QueryPlayerUse(FocusedUsable);
	}

	const FElysiumUseCandidate* Selected = nullptr;
	for (const FElysiumUseCandidate& Candidate : Query.Candidates)
	{
		FElysiumEntity* Entity = Resolve(Candidate.Owner);
		FElysiumUseContext Context;
		Context.Activator = Player;
		Context.Owner = Candidate.Owner;
		Context.AnchorPoint = Candidate.AnchorPoint;
		Context.Selection = Candidate.Selection;
		Context.TimeSeconds = NowSeconds();
		if (Entity && Entity->CanPlayerFocus(Context))
		{
			Selected = &Candidate;
			break;
		}
		// An exact entity hit fails closed. Assistance must never jump through the object under
		// the reticle to something merely close to it.
		if (Candidate.Selection == EElysiumUseSelection::Exact)
		{
			break;
		}
	}
	TransitionUseFocus(Selected);
	LastUseOutcome = Selected ? EElysiumUseOutcome::Completed : Query.MissOutcome;

	const TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingUseEdges);
	PendingUseEdges.Reset();
	for (EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::WhileHeld)
			{
				EndActiveUse(EElysiumUseEndReason::Released);
			}
			continue;
		}

		if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
		{
			LastUseOutcome = EElysiumUseOutcome::Busy;
			continue;
		}
		FElysiumEntity* Entity = Resolve(FocusedUsable);
		if (!Entity || !Entity->CanPlayerFocus(FocusContext))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			continue;
		}

		if (!Entity->UseFilterName.IsEmpty())
		{
			FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
			// Retail treats an unresolved filter target as no filter. A resolved filter is the
			// authoritative gate and sees the player as its activator, exactly like trigger filters.
			if (!Filter)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
					*Entity->DebugString(), *Entity->UseFilterName);
			}
			if (Filter && !Filter->PassesFilter(Player))
			{
				LastUseOutcome = EElysiumUseOutcome::Unavailable;
				continue;
			}
		}
		FocusContext.TimeSeconds = NowSeconds();
		const bool bWasLocked = Entity->IsUseLocked();
		const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(FocusContext);
		LastUseOutcome = bWasLocked ? EElysiumUseOutcome::Locked : Result.Outcome;
		if (Result.Outcome == EElysiumUseOutcome::SessionStarted
			&& Result.SessionKind != EElysiumUseSessionKind::None)
		{
			FActiveUse Session;
			Session.Context = FocusContext;
			Session.Kind = Result.SessionKind;
			ActiveUse = Session;
		}
	}
}

FElysiumInteractionView FElysiumEntityWorld::GetInteractionView() const
{
	FElysiumInteractionView View;
	if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::Explicit)
	{
		return View;
	}
	View.PromptAlpha = InteractionPromptAlpha(NowSeconds());
	View.bVisible = InteractionPrompt.DisplayOwner.IsSet()
		&& View.PromptAlpha > KINDA_SMALL_NUMBER;
	View.bActionable = FocusedUsable.IsSet() && !ActiveUse.IsSet();
	View.Icon = InteractionPrompt.Icon;
	View.bLocked = InteractionPrompt.bLocked;
	return View;
}

namespace
{
	FElysiumItemContainer* FindOpenLootContainer(FElysiumEntityWorld& World)
	{
		const FElysiumEntityHandle Player = World.PlayerHandle();
		for (const TUniquePtr<FElysiumEntity>& Candidate : World.Entities())
		{
			FElysiumItemContainer* Container = Candidate ? Candidate->AsItemContainer() : nullptr;
			if (Container && !Container->IsDead() && Container->CurrentUser == Player)
			{
				return Container;
			}
		}
		return nullptr;
	}
}

bool FElysiumEntityWorld::BuildLootView(FElysiumLootView& Out) const
{
	Out = FElysiumLootView();
	FElysiumEntityWorld& Mutable = const_cast<FElysiumEntityWorld&>(*this);
	FElysiumItemContainer* Container = FindOpenLootContainer(Mutable);
	const FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		return false;
	}
	Container->BuildLootView(Out, *PlayerEntity);
	return true;
}

bool FElysiumEntityWorld::BuildTerminalView(FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	if (!ActiveUse.IsSet() || ActiveUse->Kind != EElysiumUseSessionKind::Explicit)
	{
		return false;
	}
	const FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	const FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	if (!Terminal || Terminal->CurrentUser != ActiveUse->Context.Activator)
	{
		return false;
	}
	Terminal->BuildView(Out);
	return Out.IsOpen();
}

bool FElysiumEntityWorld::SubmitTerminalCommand(const FElysiumEntityHandle& OwnerHandle,
	uint32 SessionSerial, const FString& Command)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != OwnerHandle
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->Submit(SessionSerial, Command);
}

bool FElysiumEntityWorld::SubmitActiveTerminalCommand(const FString& Command)
{
	if (!ActiveUse.IsSet())
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, Command);
}

bool FElysiumEntityWorld::PlayerBeginTerminalHack(const FElysiumEntityHandle& OwnerHandle,
	uint32 SessionSerial)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != OwnerHandle
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->BeginHack(SessionSerial);
}

bool FElysiumEntityWorld::PlayerLootTake(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot take failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->TakeToPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerLootGive(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot give failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->GiveFromPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerCloseLoot()
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	if (!Container)
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: no open container session"));
		return false;
	}
	if (!EndPlayerUseSession(Container->Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: %s did not own active +use"),
			*Container->DebugString());
		return false;
	}
	return true;
}
