#include "ElysiumEntityWorld.h"

#include "ElysiumClipMovement.h"   // the melee stop's own recovered rule
#include "ElysiumComboChain.h"   // ElysiumCombo::In* — the FILE's own button bits the masks are in
#include "ElysiumPlayer.h"
#include "ElysiumUserCmd.h"   // EElysiumButton — the combat button field this file drains
#include "ElysiumViewState.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"   // EElysiumItemType — the weapon frame's melee/ranged split
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"

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
	// Begins before ends, reproducing retail's frame order: the engine fires a new contact's
	// StartTouch synchronously during the move, and defers a stale contact's EndTouch to that frame's
	// post-think untouch pass — both outputs reach the same event-queue drain, so with the queue's
	// FIFO tie-break the end edge is the last writer of any shared state (retail engine.dll relink →
	// `PhysicsMarkEntityAsTouched` StartTouch vs `PhysicsCheckForEntityUntouch` EndTouch;
	// `docs/vtmb/entity_io.md`). Within each phase the entity-index sort is a deterministic substitute
	// for retail's spatial (BSP-leaf) enumeration, which no map is known to depend on.
	Begins.Sort();
	Ends.Sort();
	for (int32 BrushIndex : Begins)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ true);
	}
	for (int32 BrushIndex : Ends)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ false);
	}
}

void FElysiumEntityWorld::QueuePlayerFeedEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingFeedEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::SetPlayerButtons(uint64 Buttons)
{
	if (!bActive || PlayerButtons == Buttons)
	{
		return;
	}
	// The COMBAT bits are what arms the think, not the whole field. The movement bits ride the same
	// field because direction-keyed attack selection reads them, and they change on almost every
	// frame a player walks — arming a deadline-driven think off those would make it a per-frame think
	// for as long as the player is moving, which is a different scheduler than the one this arms.
	constexpr uint64 CombatBits =
		static_cast<uint64>(EElysiumButton::Attack)
		| static_cast<uint64>(EElysiumButton::Attack2)
		| static_cast<uint64>(EElysiumButton::SecondaryAtk)
		| static_cast<uint64>(EElysiumButton::Reload);
	const bool bCombatChanged = ((PlayerButtons ^ Buttons) & CombatBits) != 0;
	PlayerButtons = Buttons;
	// Arm the think on a combat change. The player's think is deadline-driven off the stealth cadence,
	// so both edges would otherwise be answered up to a tenth of a second late — long enough for a
	// released block to still be blocking when a contact lands.
	if (bCombatChanged)
	{
		if (FElysiumPlayer* PlayerEnt = FindPlayer())
		{
			PlayerEnt->NextThink = static_cast<float>(NowSeconds());
		}
	}
}

int32 FElysiumEntityWorld::PlayerSelectionStateMask() const
{
	// The direction correspondence, stated once. Nothing here is arithmetic on a bit index: the two
	// numberings agree on no bit at all, and the pairing is what each bit MEANS.
	struct FDirectionBit
	{
		EElysiumButton Ours;
		int32 Theirs;
	};
	static constexpr FDirectionBit Bits[] = {
		{ EElysiumButton::Jump,      ElysiumCombo::InJump },
		{ EElysiumButton::Forward,   ElysiumCombo::InForward },
		{ EElysiumButton::Back,      ElysiumCombo::InBack },
		{ EElysiumButton::Left,      ElysiumCombo::InLeft },
		{ EElysiumButton::Right,     ElysiumCombo::InRight },
		{ EElysiumButton::MoveLeft,  ElysiumCombo::InMoveLeft },
		{ EElysiumButton::MoveRight, ElysiumCombo::InMoveRight },
	};

	int32 Mask = 0;
	for (const FDirectionBit& Bit : Bits)
	{
		if ((PlayerButtons & static_cast<uint64>(Bit.Ours)) != 0)
		{
			Mask |= Bit.Theirs;
		}
	}
	return Mask;
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

void FElysiumEntityWorld::AdvanceMeleeSwings(float DeltaSeconds)
{
	// Same liveness gate as the weapon frame below, and for the same reason: a world that is not
	// running is not sweeping a swing through the air, it is not observing one at all.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	// The same walk the animation-event pass makes over the entity list, including the player: a
	// swing belongs to whoever is holding the weapon, and the player has no separate contact path.
	// Indexed rather than ranged, because a contact commits damage and a death can append to the
	// list mid-walk.
	for (int32 Index = 0; Index < EntityList.Num(); ++Index)
	{
		FElysiumEntity* Ent = EntityList[Index].Get();
		FElysiumCombatCharacter* Character = Ent ? Ent->AsCombatCharacter() : nullptr;
		if (Character == nullptr || Ent->IsInert())
		{
			continue;
		}
		FElysiumItem* Active = Character->Inventory.Active(*Character);
		if (FElysiumWeapon* Weapon = Active ? Active->AsWeapon() : nullptr)
		{
			Weapon->AdvanceSwingContact(DeltaSeconds);
		}
	}
}

void FElysiumEntityWorld::UpdatePlayerWeaponFrame()
{
	// The world-liveness gate stands AHEAD of the edge drain, because a world that is not running is
	// not refusing a press — it is not observing one. That is the same pairing a pause relies on
	// (`AElysiumMapActor::PostMoveTick` carries `bTickEvenWhenPaused = false`, and the controller
	// publishes no command while held), and switching trigger resolution off has to behave the same
	// way rather than consuming a held button until the player releases it.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	// The press edges, and they are computed FIRST — before every refusal below, including the ones
	// that return without doing anything. A press is spent by being observed, so a frame that
	// refuses it cannot bank it for the next one and a button held across a refused frame cannot
	// re-press itself.
	const uint64 Pressed = PlayerButtons & ~ConsumedPlayerButtons;
	ConsumedPlayerButtons = PlayerButtons;
	const uint64 Held = PlayerButtons;

	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}
	// RE-A6 — retail skips the whole live `PostThink` main body, and `ItemPostFrame` with it, while
	// `m_iPlayerLocked` is set or the player is not alive (`docs/vtmb/player-entity.md` § "Recovered
	// `PostThink` body"). This runtime publishes no such field; `IsMobile()` is the latch every other
	// producer already gates on for the states that raise it — a cutscene, a scripted beat, a
	// controller handover — so it stands in for it.
	if (!PlayerEnt->IsMobile() || PlayerEnt->HasReportedDeath())
	{
		return;
	}

	// Retail's `ItemPostFrame` gives a controlling use entity FIRST REFUSAL
	// (`docs/vtmb/player-entity.md` § "Recovered `PostThink` body"). An open sign panel is this
	// runtime's other controlling surface, and mapping the first refusal onto it is CHOSEN (C4):
	// every VtMB popup instructs "left-click to continue", so the primary press that dismisses one
	// must not also swing. The press is spent either way — `MinShowTime` refusing the dismissal is
	// not a reason to let the click through to the weapon.
	//
	// The refusal is the WHOLE frame's, not the dismissing press's. An open panel owns the primary
	// button for as long as it is up, so a press that arrives while it is up — a second click at the
	// panel, an autofire trigger still held — must not reach the weapon and fire behind it. The edges
	// were already drained above, so a button held across the panel's whole life produces exactly one
	// spent press and nothing re-presses itself when the panel closes.
	if (GetOpenSign().IsSet())
	{
		if ((Pressed & static_cast<uint64>(EElysiumButton::Attack)) != 0)
		{
			PlayerDismissSign();
		}
		return;
	}
	// The literal controlled-use refusal: a captured session owns the player's hands.
	if (ActiveUse.IsSet())
	{
		return;
	}

	FElysiumItem* Item = PlayerEnt->Inventory.Active(*PlayerEnt);
	FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
	if (!Weapon)
	{
		// An empty hand is an ordinary state, not a failure: the selector can legitimately hold a
		// non-weapon or nothing at all, and a click then does nothing.
		return;
	}

	EElysiumWeaponButton HeldMask = EElysiumWeaponButton::None;
	EElysiumWeaponButton PressedMask = EElysiumWeaponButton::None;
	const auto Fold = [](uint64 Bits, EElysiumWeaponButton& Out)
	{
		if ((Bits & static_cast<uint64>(EElysiumButton::Attack)) != 0)
		{
			Out |= EElysiumWeaponButton::Primary;
		}
		// Both secondary verbs reach the same route. `+wpn_secondaryatk` is a composite: its block
		// half is a standing classification on the player, and this is its ordinary secondary-fire
		// half (`docs/vtmb/controls.md` § "Attack, block and weapon commands").
		if ((Bits & (static_cast<uint64>(EElysiumButton::Attack2)
			| static_cast<uint64>(EElysiumButton::SecondaryAtk))) != 0)
		{
			Out |= EElysiumWeaponButton::Secondary;
		}
		if ((Bits & static_cast<uint64>(EElysiumButton::Reload)) != 0)
		{
			Out |= EElysiumWeaponButton::Reload;
		}
	};
	Fold(Held, HeldMask);
	Fold(Pressed, PressedMask);
	if (HeldMask == EElysiumWeaponButton::None && PressedMask == EElysiumWeaponButton::None)
	{
		return;
	}

	// The ranged victim. A firearm's shot takes an explicit handle rather than inventing a trace, and
	// the producer that supplies one is the embodiment's aim query.
	//
	// It runs only on a button this frame's record would take an attack route for, and only for a
	// mode that fires: a weapon merely being carried never traces. It is deliberately NOT gated on
	// the attack being ready as well — the deadline, the reload latch and the empty magazine are
	// `AttackIntent`'s to judge, and a second copy of any of them here is how the button route and
	// the aim route come to disagree. The cost of that choice is a held autofire trigger tracing
	// once per frame across its recovery gaps, which is a read-only query on a frame that is
	// already firing.
	//
	// Melee is excluded because it reserves its own opponent inside the swing, on the authored
	// sequence reach — a second acquisition here would reserve a different body than the one the
	// swing hits.
	//
	// An Invalid answer is ordinary and the transaction already tolerates it: acquisition is opponent
	// reservation, not a damage verdict, so an unaimed shot still animates and still spends its
	// ammunition.
	FElysiumEntityHandle AimTarget = FElysiumEntityHandle::Invalid();
	const FElysiumItemDef* WeaponRecord = Weapon->Data();
	const bool bMeleeRecord =
		WeaponRecord && WeaponRecord->Type == EElysiumItemType::WeaponMelee;
	if (!bMeleeRecord)
	{
		const bool bPrimary = Weapon->WantsPrimaryPress(HeldMask, PressedMask);
		if (bPrimary || Weapon->WantsSecondaryPress(PressedMask))
		{
			const FElysiumWeapon::EIntent Intent = bPrimary
				? FElysiumWeapon::EIntent::Primary : FElysiumWeapon::EIntent::Secondary;
			// The range is the answering mode's own authored `Range`; the weapon reports for itself
			// when the record authors none.
			const float RangeCm = Weapon->AimQueryRangeCm(Intent);
			if (IElysiumEmbodiment* Bodily = Embodiment(); Bodily != nullptr && RangeCm > 0.0f)
			{
				AimTarget = Bodily->QueryAimTarget(RangeCm);
			}
		}
	}
	const FElysiumWeapon::EVerdict Verdict =
		Weapon->ItemPostFrame(HeldMask, PressedMask, AimTarget);

	// **Logged on the EDGE, and never for `Idle`.** This frame runs on any held weapon button, not
	// only on a press, so a trigger held through an attack's recovery answers `Idle` — "no button
	// asked for anything" — on every frame of it. At frame rate that is hundreds of identical lines
	// a second, and it buries the melee timeline and everything else in the log. An unchanged verdict
	// says nothing the previous line did not, so only a change is reported; `Idle` is the resting
	// answer and is never worth a line of its own.
	const int32 VerdictKey = static_cast<int32>(Verdict);
	if (Verdict != FElysiumWeapon::EVerdict::Idle && VerdictKey != LastLoggedWeaponVerdict)
	{
		UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) player weapon frame %s -> %s"),
			NowSeconds(), *Weapon->DebugString(), FElysiumWeapon::VerdictName(Verdict));
	}
	LastLoggedWeaponVerdict = VerdictKey;
}

void FElysiumEntityWorld::UpdatePlayerMeleeMovementStop(const FElysiumIdealActivityState& State)
{
	if (!bActive)
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}
	// The same two guards the weapon frame takes, and they are the same retail ones: the whole live
	// `PostThink` body is skipped while `m_iPlayerLocked` is set or the player is not alive, and
	// this block sits inside it.
	if (!PlayerEnt->IsMobile() || PlayerEnt->HasReportedDeath())
	{
		return;
	}
	// Asked fresh every frame, against this frame's own cycle. The recovered block keeps no state
	// between frames and neither does this.
	if (!ElysiumClipMovement::StopsMeleeTailMotion(State, PlayerSelectionStateMask()))
	{
		return;
	}
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->StopPlayerBody();
	}
	// A headless world has no body to stop. An ordinary absence, and the same one every other
	// embodiment reader treats as one.
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
