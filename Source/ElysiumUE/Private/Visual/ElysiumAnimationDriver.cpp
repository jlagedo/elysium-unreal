#include "Visual/ElysiumAnimationDriver.h"

#include "ElysiumClipMovement.h"            // the animation-driven predicate and its refuse set
#include "ElysiumEntityWorld.h"             // the clock the combat-stance window is read against
#include "ElysiumPlayer.h"
#include "Visual/ElysiumActionTables.h"     // the committed player gait ladder (LIFE4, Option A)

void FElysiumAnimationDriver::AddReferencedObjects(FReferenceCollector& Collector)
{
	Assets.AddReferencedObjects(Collector);
}

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnimDriver, Log, All);

namespace
{
	// Drop everything the RESOLVER owns about the overlay slot.
	//
	// Every field here is written by `UElysiumAnimSubsystem::ResolveSlotLayer` and by nothing else, so
	// a frame that did not re-enter it must not leave them describing a layer other than the one the
	// record names. `FElysiumResolvedAnimation::HasSlotLayer` is a POINTER test: a sequence left behind
	// by a claim that has gone answers it true under a record that says nothing is layering, and the
	// graph would keep composing the previous shot over the base pose.
	void ClearSlotAssets(FElysiumResolvedAnimation& Assets)
	{
		// **Every field the resolver writes, not just the pair the pointer test reads.** `SlotSpace`,
		// `SlotAimMaskName` and `SlotAdditive` come out of `ResolveSlotDeclaredAssets` on the same
		// pass and are stale by the same argument; leaving them makes `HasSlotLayer()` answer true off
		// a grid under a record that names no layer, and the graph is saved from composing it only by
		// `PublishSelection` gating all three on the sequence.
		Assets.SlotSequence = nullptr;
		Assets.SlotSpace = nullptr;
		Assets.SlotAdditive = nullptr;
		Assets.SlotMaskName = NAME_None;
		Assets.SlotAimMaskName = NAME_None;
	}
}

void FElysiumAnimationDriver::Reset()
{
	Latch = FElysiumJumpLatch();
	Selection = FElysiumAnimationSelection();
	Assets = FElysiumResolvedAnimation();
	// The forced ideal and the cycle it was read against go with the record they describe: a teleport
	// or a map epoch is not a swing continuing, and a lock carried across one would discard the
	// player's command for a clip nothing is playing.
	BaseClipCycle = FElysiumBaseClipCycle();
	IdealActivity = FElysiumIdealActivityState();
	bAnimationDriven = false;
	bMovementLocked = false;
	// The published pair goes together: a producer left holding last epoch's sample beside this
	// epoch's empty record would trace a body that moved through a frame nothing classified.
	Sample = FElysiumLocomotionSample();
	LastActivity.Reset();
	LastStem.Reset();
	LastActorClassname.Reset();
	LastWeaponClassname.Reset();
	LastRoute = EElysiumAnimRoute::Activity;
	bResolvedOnce = false;
	// The slot key goes with the rest of the discrete state: the claims below are dropped by this same
	// reset, so a remembered handle would report the layer as unchanged and never resolve the next one.
	LastSlotHandle = 0;
	// A teleport is not a turn: slewing the stride across one would rotate the body's gait over a
	// quarter second of standing somewhere else entirely.
	MoveYawFilter = FElysiumMoveYawFilter();

	// The tables go too. A reset is a teleport or a map epoch, and a driver reused across a model
	// swap would otherwise steer the mover with the previous body's authored speeds — which is the
	// one failure mode of pushing rather than pulling, and it costs one re-resolve to close.
	GaitKey = FElysiumGaitSpeedRequest();
	GaitSpeeds = FElysiumGaitSpeeds();
	GaitGeneration = 0;
	// And the reference built from them, for the same reason: a threshold left behind by the previous
	// body would classify the next one against a walk it does not author.
	Gait = FElysiumGaitReference();

	// The claims go with everything else: a reset is a teleport or a map epoch, and a claim held
	// across one would let last epoch's scene hold a body it no longer owns. The serial is NOT
	// reset, so a handle issued before the reset releases nothing rather than a stranger's claim.
	for (FElysiumAnimRequestSlot& Slot : Requests)
	{
		Slot = FElysiumAnimRequestSlot();
	}
}

uint32 FElysiumAnimationDriver::SubmitRequest(const FElysiumAnimationRequest& Request)
{
	const int32 Channel = static_cast<int32>(Request.Channel);
	if (Channel < 0 || Channel >= ElysiumAnimIntent::NumChannels)
	{
		UE_LOG(LogElysiumAnimDriver, Warning,
			TEXT("'%s' refused an animation request on channel %d ('%s' from %s): no such channel"),
			*Stem, Channel, *Request.Label, ElysiumAnimIntent::SourceName(Request.Source));
		return 0;
	}
	FElysiumAnimRequestSlot& Slot = Requests[Channel];
	if (Slot.bActive && Request.Priority < Slot.Request.Priority)
	{
		// An ordinary arbitration answer, not a failure: the slot holds the higher claim, and the
		// refused producer's clip simply does not own the channel.
		UE_LOG(LogElysiumAnimDriver, Verbose,
			TEXT("'%s' %s claim '%s' (%s) refused: the channel is held by %s '%s' (%s)"),
			*Stem, ElysiumAnimIntent::SourceName(Request.Source), *Request.Label,
			ElysiumAnimIntent::PriorityName(Request.Priority),
			ElysiumAnimIntent::SourceName(Slot.Request.Source), *Slot.Request.Label,
			ElysiumAnimIntent::PriorityName(Slot.Request.Priority));
		return 0;
	}
	Slot.Request = Request;
	Slot.AgeSeconds = 0.0f;
	Slot.bActive = true;
	// Zero means "no claim", so the serial skips it on wrap.
	if (++RequestSerial == 0)
	{
		++RequestSerial;
	}
	Slot.Handle = RequestSerial;
	return Slot.Handle;
}

bool FElysiumAnimationDriver::ReleaseRequest(uint32 Handle)
{
	if (Handle == 0)
	{
		return false;
	}
	for (FElysiumAnimRequestSlot& Slot : Requests)
	{
		if (Slot.bActive && Slot.Handle == Handle)
		{
			Slot = FElysiumAnimRequestSlot();
			return true;
		}
	}
	// Already expired, outranked or replaced — the ordinary end of a claim whose owner came back
	// late, and not a failure.
	return false;
}

int32 FElysiumAnimationDriver::ReleaseAllRequests(bool* OutDroppedSlotLayer)
{
	if (OutDroppedSlotLayer != nullptr)
	{
		*OutDroppedSlotLayer = false;
	}
	int32 Released = 0;
	for (FElysiumAnimRequestSlot& Slot : Requests)
	{
		if (Slot.bActive)
		{
			// **The overlay slot's channel is reported, not just counted.** Its POSE lives on an anim
			// instance this struct deliberately cannot see, and the claim going back is not what takes
			// a layer off the body — the next publish is, and a body being released wholesale is a
			// body that may never publish again. The caller holding the mesh is the one that can end
			// it, and this is the only thing that tells it there is something to end.
			if (Slot.Request.Channel == EElysiumAnimChannel::UpperBody
				&& OutDroppedSlotLayer != nullptr)
			{
				*OutDroppedSlotLayer = true;
			}
			UE_LOG(LogElysiumAnimDriver, Verbose,
				TEXT("'%s' drops %s claim '%s' (%s) on %s: the character's claims are being released "
					 "wholesale"),
				*Stem, ElysiumAnimIntent::SourceName(Slot.Request.Source), *Slot.Request.Label,
				ElysiumAnimIntent::PriorityName(Slot.Request.Priority),
				ElysiumAnimIntent::ChannelName(Slot.Request.Channel));
			++Released;
		}
		Slot = FElysiumAnimRequestSlot();
	}
	return Released;
}

const FElysiumAnimationRequest* FElysiumAnimationDriver::ActiveRequest(
	EElysiumAnimChannel Channel) const
{
	const int32 Index = static_cast<int32>(Channel);
	if (Index < 0 || Index >= ElysiumAnimIntent::NumChannels || !Requests[Index].bActive)
	{
		return nullptr;
	}
	return &Requests[Index].Request;
}

FElysiumIdealActivityState FElysiumAnimationDriver::ForcedIdealActivity() const
{
	FElysiumIdealActivityState State;
	// **The BASE channel explicitly, and no other.** Retail's `ForcePreTranslatedSequenceAndActivity`
	// writes the ideal activity together with the base sequence; its overlay slots write no ideal
	// activity at all. A layer claim states an activity too — `ACT_RELOAD_LAYER` names the family its
	// blend envelope comes from — and reading that here would hand the movement lock, the reselection
	// guard and the airborne-attack self-latch an activity the body is not performing: the player
	// would be frozen in place for the length of every reload.
	if (const FElysiumAnimationRequest* Claim = ActiveRequest(EElysiumAnimChannel::Base))
	{
		State.Activity = Claim->Activity;
	}
	// The cycle half comes from the pose layer through the owner's push. A body with no pose layer
	// reports no sequence, which fails every row's own guard — so a headless body is never
	// animation-driven, however its claims are labelled.
	State.bHasSequence = BaseClipCycle.bPlaying;
	State.Cycle = BaseClipCycle.Cycle;
	State.HoldCycle = BaseClipCycle.HoldCycle;
	return State;
}

void FElysiumAnimationDriver::AdvanceRequests(float DeltaSeconds)
{
	for (FElysiumAnimRequestSlot& Slot : Requests)
	{
		if (!Slot.bActive)
		{
			continue;
		}
		// Every claim ages — an unexpiring one too, because its age is what the verdict surface
		// shows to tell a scene mid-performance from a claim whose owner leaked it.
		Slot.AgeSeconds += DeltaSeconds;
		if (Slot.Request.HoldSeconds > 0.0f && Slot.AgeSeconds >= Slot.Request.HoldSeconds)
		{
			// The one-shot ran its length; the channel goes back to whoever is underneath.
			Slot = FElysiumAnimRequestSlot();
		}
	}
}

void FElysiumAnimationDriver::ArbitrateBase()
{
	// **The base slot by name, and only it.** The priority table ranks claims against the locomotion
	// publish, and a publish can only take back a channel it is capable of posing — the base pose.
	// A layer on another channel is not competing for that pose at all, so ranking it here would let
	// a travelling body consume a reload layer it was never displacing.
	FElysiumAnimRequestSlot& Slot = Requests[static_cast<int32>(EElysiumAnimChannel::Base)];
	const EElysiumAnimPriority Locomotion =
		ElysiumAnimIntent::LocomotionPriority(Selection.GraphState);
	// Ties keep the holder: a claim is not churned by a publisher it merely equals.
	if (Slot.bActive && Slot.Request.Priority >= Locomotion)
	{
		Selection.bBasePoseOwned = false;
		Selection.BaseHold = FString::Printf(TEXT("%s '%s' (%s)"),
			ElysiumAnimIntent::SourceName(Slot.Request.Source), *Slot.Request.Label,
			ElysiumAnimIntent::PriorityName(Slot.Request.Priority));
		Selection.BaseHoldSeconds = Slot.AgeSeconds;
		return;
	}
	if (Slot.bActive)
	{
		// Outranked, which consumes the claim: the publish that won is about to end its clip, and a
		// claim left standing would report a holder whose pose is no longer on screen.
		UE_LOG(LogElysiumAnimDriver, Verbose,
			TEXT("'%s' locomotion (%s) takes the base pose from %s '%s' (%s)"),
			*Stem, ElysiumAnimIntent::PriorityName(Locomotion),
			ElysiumAnimIntent::SourceName(Slot.Request.Source), *Slot.Request.Label,
			ElysiumAnimIntent::PriorityName(Slot.Request.Priority));
		Slot = FElysiumAnimRequestSlot();
	}
	Selection.bBasePoseOwned = true;
	Selection.BaseHold.Reset();
	Selection.BaseHoldSeconds = 0.0f;
}

void FElysiumAnimationDriver::ArbitrateSlot()
{
	const FElysiumAnimRequestSlot& Slot =
		Requests[static_cast<int32>(EElysiumAnimChannel::UpperBody)];
	if (!Slot.bActive)
	{
		// No layer, so the record says so outright rather than leaving the last shot's line standing
		// under a body that has stopped firing. `SlotOwnerStem` goes with the rest: it names the bank
		// a layer came out of, and a bank with no layer in it identifies nothing. The assets go too,
		// because the record and the graph have to be unable to disagree: a sequence left standing
		// answers `HasSlotLayer()` true beside a record that has already stopped naming a layer.
		Selection.SlotLabel.Reset();
		Selection.SlotOwnerStem.Reset();
		Selection.SlotWeight = 0.0f;
		Selection.SlotCycle = 0.0f;
		ClearSlotAssets(Assets);
		return;
	}
	// The claim's own identity, published rather than compared: nothing was arbitrated here.
	Selection.SlotLabel = Slot.Request.Label;
	// **The bank and the assets are republished only where they still describe THIS claim.** The
	// resolver is the only thing that can name either, and `Tick` has exit paths that publish the
	// record without re-entering it — an animation-driven frame, a refused reselection. `LastSlotHandle`
	// is that resolver's own key, so a claim it has not answered for yet is stated as a named layer
	// with no bank and no asset behind it, rather than as this claim wearing the previous one's: a
	// record naming clip B over bank A while the graph holds clip A is a layer that reads correct and
	// composes the wrong motion.
	if (Slot.Handle != LastSlotHandle)
	{
		Selection.SlotOwnerStem.Reset();
		ClearSlotAssets(Assets);
	}
	// The same duration `AdvanceRequests` expires the claim on, read as a phase. One clock: the
	// layer cannot be weighed against a length that is not the one it ends on.
	Selection.SlotCycle = ElysiumAnimIntent::SlotCycle(Slot.Request, Slot.AgeSeconds);
	// The ENVELOPED weight over that phase, capped at retail's `m_flWeightMax`, rather than the
	// ceiling itself: the readout's job is to separate a layer composing at full strength from one
	// stuck at zero, and the ceiling answers the same number for both. The per-bone mask that gates it
	// belongs to the composition, not to this line.
	Selection.SlotWeight = ElysiumAnimIntent::SlotWeightAt(Slot.Request, Slot.AgeSeconds);
}

void FElysiumAnimationDriver::ResolveSlotClaim(UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh)
{
	const FElysiumAnimRequestSlot& Slot =
		Requests[static_cast<int32>(EElysiumAnimChannel::UpperBody)];
	const uint32 Handle = Slot.bActive ? Slot.Handle : 0;
	if (Handle == LastSlotHandle)
	{
		// Already answered for. The bank and the asset standing are this claim's own, so re-loading
		// them per frame would repeat the vocabulary walk and the mount read for every frame of a
		// swing — and `ArbitrateSlot` below leaves them alone precisely because this key matches.
		return;
	}
	// The resolver's own key moves here, which is what stops `ArbitrateSlot` clearing the answer this
	// call is about to write.
	LastSlotHandle = Handle;
	// Whatever the previous claim left behind goes first: the fields below are the resolver's, and a
	// claim it has not answered for must never be described by another claim's bank or asset.
	Selection.SlotOwnerStem.Reset();
	ClearSlotAssets(Assets);
	if (Handle == 0 || Anims == nullptr)
	{
		// No claim, or no game instance to read a catalog from — the same rung the base resolve's own
		// catalog-less exit takes, and for the same reason. `ArbitrateSlot` still publishes the
		// claim's label, so a layer that could not be looked up reads as a named miss.
		return;
	}
	Anims->ResolveSlotLayer(Slot.Request, Stem, Mesh, Selection, Assets);
}

void FElysiumAnimationDriver::SetTranslationContext(const FElysiumCombatCharacter* Char)
{
	// Written only on change. This runs once per body per frame while the answer moves only on an
	// equip, a possession or a model swap, and the discrete key below already treats a moved value
	// as a re-resolve.
	auto Adopt = [](FString& Field, const FString& Value)
	{
		if (!Field.Equals(Value, ESearchCase::CaseSensitive))
		{
			Field = Value;
		}
	};

	if (Char == nullptr)
	{
		ActorClassname.Reset();
		WeaponClassname.Reset();
		ActorState = EElysiumNpcState::Idle;
		bCombatStance = false;
		return;
	}

	// The combat-stance clock, read beside the weapon because the ladder's `CombatReady`/`Relaxed`
	// predicates consume the two together. A character outside a world has no clock and so no
	// stance — the ordinary gym case, not a failure.
	bCombatStance = Char->World != nullptr
		&& Char->IsInCombatStance(Char->World->NowSeconds());

	// **One read of the classification, asked of the character that owns it.** The per-frame publish
	// and a producer's activity resolve have to select through the same class body, the same weapon
	// ladder and the same alert/relaxed branch, and a second copy of those reads here is exactly how
	// the two come to pose different walks for one body.
	FElysiumActivityClipRequest Context;
	Char->FillActivityClipRequest(Context);
	ActorState = Context.ActorState;
	if (Context.ActorClassname.IsEmpty())
	{
		ActorClassname.Reset();
	}
	else
	{
		Adopt(ActorClassname, Context.ActorClassname);
	}
	if (Context.WeaponClassname.IsEmpty())
	{
		WeaponClassname.Reset();
	}
	else
	{
		Adopt(WeaponClassname, Context.WeaponClassname);
	}
}

FElysiumGaitSpeedRequest FElysiumAnimationDriver::BuildGaitKey() const
{
	// **The whole translation context, not a subset.** The tables are resolved through the same
	// chain the pose is: same body kind, same class body, same alert/relaxed branch.
	FElysiumGaitSpeedRequest Key;
	Key.Stem = Stem;
	Key.Source = Source;
	Key.BodyKind = BodyKind;
	Key.ActorClassname = ActorClassname;
	Key.WeaponClassname = WeaponClassname;
	Key.FormTag = FormTag;
	Key.ActorState = ActorState;
	Key.Variant = Variant;
	Key.SpeedScale = SpeedScale;
	return Key;
}

bool FElysiumAnimationDriver::RefreshGaitSpeeds(UElysiumAnimSubsystem* Anims)
{
	if (Anims == nullptr)
	{
		return false;
	}
	const FElysiumGaitSpeedRequest Key = BuildGaitKey();
	if (GaitGeneration != 0 && Key == GaitKey)
	{
		return false;
	}

	GaitKey = Key;
	Anims->ResolveGaitSpeeds(Key, GaitSpeeds);
	++GaitGeneration;
	// The classifier's reference comes off the same tables the movers command from, so the walk/run
	// threshold and the commanded speed cannot come from two different numbers. It is rebuilt here
	// rather than beside each producer's push, because there is one set of tables and both the player
	// and the cast read the threshold off it.
	Gait = ElysiumAnimIntent::GaitFrom(GaitSpeeds);
	return true;
}

FString FElysiumAnimationDriver::SelectPlayerGroundActivity(
	const FElysiumLocomotionSample& InSample) const
{
	using namespace ElysiumActionTables;

	// The branch's reach mirrors `Classify`: water and the committed air phases answer before the
	// grounded ladder does, and a moving landing falls through to the gait exactly as retail's
	// phase 8 does. Those arms live in the compact codes and the latch, not in the ladder.
	if (InSample.Water >= EElysiumWaterLevel::Waist)
	{
		return FString();
	}
	const bool bMoving = InSample.Speed2D() > Gait.StillSpeed();
	if (Latch.Phase == EElysiumAirPhase::Leap || Latch.Phase == EElysiumAirPhase::Falling
		|| (Latch.Phase == EElysiumAirPhase::Landing && !bMoving))
	{
		return FString();
	}

	// The live state the committed rows read. `CombatReady` is the recovered gate on `ACT_AIM` —
	// armed past `item_w_unarmed`, in stance, not morphed. `Relaxed` is NOT its negation: retail's
	// relaxed gaits take an active weapon out of stance, so an unarmed or a morphed body in stance
	// satisfies neither and keeps the plain gait. Morph is Protean's and constant false until that
	// discipline exists.
	const bool bArmed = !WeaponClassname.IsEmpty()
		&& !WeaponClassname.Equals(TEXT("item_w_unarmed"), ESearchCase::IgnoreCase);
	const bool bMorphed = false;
	auto State = [&](EPlayerPredicate Predicate, int32 /*Operand*/) -> bool
	{
		switch (Predicate)
		{
		case EPlayerPredicate::Always:
			return true;
		case EPlayerPredicate::Ducking:
			return InSample.Stance != EElysiumStance::Standing;
		case EPlayerPredicate::BelowMoveThreshold:
			return !bMoving;
		case EPlayerPredicate::AboveGaitThreshold:
			// The latch already holds retail's own disjunction — realized-or-commanded speed
			// against the body's walk cell plus one unit — so the ladder and the classifier
			// cannot come to split the same frame two ways.
			return Latch.bLastGaitWasRun;
		case EPlayerPredicate::CombatReady:
			return bArmed && bCombatStance && !bMorphed;
		case EPlayerPredicate::Relaxed:
			return bArmed && !bCombatStance;
		default:
			// The gait ladder draws on the six predicates above and nothing else; the rest of
			// the vocabulary belongs to the compact-code arms, which stay with the latch.
			return false;
		}
	};

	const FPlayerRule* Rule = SelectRule(PlayerGaitLadder(), State);
	if (Rule == nullptr || Rule->Activity == nullptr)
	{
		// The ladder's last row is unconditional, so this is a malformed regeneration rather
		// than a state. Logged once per body, because a broken table would otherwise warn every
		// frame; the classifier's answer stands.
		if (!bWarnedNoGroundActivityRow)
		{
			bWarnedNoGroundActivityRow = true;
			UE_LOG(LogElysiumAnimDriver, Warning,
				TEXT("'%s': the player gait ladder answered no activity row — the classifier's ")
				TEXT("answer stands"), *Stem);
		}
		return FString();
	}
	return FString(Rule->Activity);
}

float FElysiumAnimationDriver::GaitSpeedForSelection(float MoveYawDegrees) const
{
	// Off the projected graph state, which is step 6's single answer over the LOGICAL request — not
	// off the translated name, whose vocabulary is the weapon and class ladders rather than the
	// slice's own codes.
	const FElysiumGaitSpeedTable* Table = nullptr;
	switch (Selection.GraphState)
	{
	case EElysiumGraphState::Walk:  Table = &GaitSpeeds.Walk;  break;
	case EElysiumGraphState::Run:   Table = &GaitSpeeds.Run;   break;
	case EElysiumGraphState::Sneak: Table = &GaitSpeeds.Sneak; break;
	default: break;
	}
	if (Table != nullptr && Table->IsValid())
	{
		return Table->SpeedAt(MoveYawDegrees);
	}
	// Not a gait, or a body with no fan: the resolved cell's own authored speed, which is what the
	// record already carries and is zero for a label naming one clip.
	return Selection.GroundSpeedCmPerSecond;
}

void FElysiumAnimationDriver::Tick(float DeltaSeconds, const FElysiumLocomotionSample& InSample,
	UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh,
	EElysiumOneShotState OneShot)
{
	// The claims age first: a one-shot whose length has run out must not hold this frame's verdict.
	AdvanceRequests(DeltaSeconds);

	// **The gait fan is refreshed before the lock predicate is built, and is not gated by it.**
	// Retail fills its speed tables in `PreThink` ahead of the animation-driven flag it later
	// rebuilds, so the ceiling the mover clamps a substituted command against keeps tracking the live
	// run peak for the whole of a swing. Nothing here is a SELECTION — the fan is a property of the
	// body, resolved from its stem, weapon and state — so it belongs above the guard while every
	// classify/ladder/resolve step stays below it. It also seeds `Gait`, which both the jump latch
	// and the grounded ladder read this frame.
	RefreshGaitSpeeds(Anims);

	// **Retail's `CBasePlayer::SetAnimation` router opens here.** It asks the animation-driven
	// predicate and, when it holds, no-ops for animation SELECTION entirely: the ordinary player
	// selector has zero direct callers, and its one call site is on this router's not-busy branch.
	// So the gait ladder below is not overridden during a swing — it never runs, and the swing's
	// forced ideal activity stands because nothing wrote over it.
	//
	// The predicate is rebuilt from the claim and the cycle every frame, never latched, which is what
	// lets a reaction that overwrites the ideal activity mid-swing release the lock instantly.
	//
	// The router gates ORDINARY per-frame reselection and nothing else: retail's apply path has 14
	// other callers that bypass it — knockback, the block reactions, two script paths — so a reaction
	// or a scene still interrupts a swing. That asymmetry is already the priority table's
	// (`Scripted` loses to `Reaction`), and this guard leaves it alone.
	IdealActivity = ForcedIdealActivity();
	// The two arms of the same rebuild. `bAnimationDriven` is `vt+0x670` — what gates reselection
	// below and the mover's jump refusal; `bMovementLocked` is `vt+0x674`, the same predicate plus
	// `ACT_LAND_HARD`, and it is what discards the movement command. Both are `CBasePlayer`
	// capabilities, so both gate on the body.
	const bool bPlayerBody = BodyKind == EElysiumAnimBodyKind::Player;
	bAnimationDriven = bPlayerBody && ElysiumClipMovement::IsAnimationDriven(IdealActivity);
	bMovementLocked = bPlayerBody && ElysiumClipMovement::IsMovementLocked(IdealActivity);

	// The pose parameter is a rate, and this is the one place per body per frame — a producer's
	// sample is a getter a readout may take twice, so it seeds the value and cannot advance it.
	FElysiumLocomotionSample Body = InSample;
	// The three pose writes belong to the selector, so an animation-driven body does not perform
	// them: `move_yaw` holds where the swing began rather than slewing toward a wish the substituted
	// command no longer carries.
	Body.MoveYawPose = bAnimationDriven
		? MoveYawFilter.Value
		: ElysiumLocomotion::AdvanceMoveYaw(MoveYawFilter, InSample.MoveYawVelocity,
			InSample.Speed2D(), DeltaSeconds);
	// Kept, so the record and the frame it describes are one published pair. Every reader — the
	// graph, the trace, Cog — takes both from here rather than asking the body again.
	Sample = Body;

	// Only the player chain commands jumps, so only the player chain has air phases to latch. A cast
	// body's mover reports itself airborne for reasons that are never a jump — a mode it has not been
	// given yet, a lift, a frame mid-teleport — and retail answers none of them with an activity: its
	// NPC surface has no ground poll at all, and the air activities that exist are requested by a
	// scripted task. It is a `CBasePlayer` capability, so it reads the BODY: a scene beat driving the
	// player pawn still commands jumps, and a player-sourced request on a cast body never does.
	const bool bCommandsJumps = BodyKind == EElysiumAnimBodyKind::Player;
	// The air phases are the movement state machine's rather than the selector's — retail keeps its
	// jump phase in a player field the movement code writes — so the latch advances on both branches.
	// A body that leaves the ground mid-swing has left the ground.
	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Body, DeltaSeconds, Gait, OneShot,
		bCommandsJumps);

	if (bAnimationDriven)
	{
		// Phase 1 of the swing: the selection pass does not run at all. The record keeps naming the
		// clip and the assets it last resolved — the graph is posing the swing's own claim over them
		// — and the ideal activity is the forced one, which is what every reader of "what is this
		// body doing" takes while the claim stands.
		Selection.RequestedActivity = IdealActivity.Activity;
		Selection.AirPhase = Latch.Phase;
		// **The slot is answered even though the selection pass is not.** It composes over whatever
		// owns the base pose rather than choosing one, so it depends on no rung of the pass being
		// skipped here — and a shot fired mid-swing is exactly the case this exit is taken on.
		ResolveSlotClaim(Anims, Mesh);
		ArbitrateBase();
		ArbitrateSlot();
		return;
	}

	FElysiumAnimationIntent Intent = ElysiumAnimIntent::BuildLocomotionIntent(Body, Latch, Gait,
		Source, BodyKind, Stem, Character, Variant);
	// The translation context, which the classifier has no business knowing: it answers what the body
	// did, and these answer which sequence set realizes it.
	Intent.ActorClassname = ActorClassname;
	Intent.WeaponClassname = WeaponClassname;
	Intent.ActorState = ActorState;
	Intent.FormTag = FormTag;

	// LIFE4, Option A — the player's grounded stand/gait comes off the committed retail ladder,
	// walked live with the combat-stance query. `ACT_AIM` for a combat-ready armed stand, the
	// plain gaits in stance, the relaxed ones out of it; translation renames per weapon
	// downstream, so no Combat special case exists anywhere in the chain. Empty means the
	// grounded branch does not decide this frame and `Classify`'s answer stands (water, air).
	//
	// The ladder is `CBasePlayer`'s own committed rows, so it gates on the body rather than on who
	// asked — the same fork the translation and the fallback ladder take.
	if (BodyKind == EElysiumAnimBodyKind::Player)
	{
		const FString GroundActivity = SelectPlayerGroundActivity(Body);
		if (!GroundActivity.IsEmpty())
		{
			Intent.Activity = GroundActivity;
		}

		// **Phase 2 of the swing, between `w_hold` and the end of the clip.** The lock has released
		// and the selector runs again, so what the swing's tail survives on is the recovered refuse
		// set: an unfinished `ACT_MELEE_ATTACK` refuses `ACT_IDLE` and `ACT_AIM` — the selector
		// returns without applying — and applies anything else.
		//
		// So a body standing still plays its swing out, and a body that is MOVING answers a gait,
		// which is applied and cuts the last ~9% of the clip. That is a deliberate recovery cancel.
		if (ElysiumClipMovement::RefusesReselection(IdealActivity, Intent.Activity))
		{
			Selection.RequestedActivity = IdealActivity.Activity;
			Selection.AirPhase = Latch.Phase;
			// The same reason as the animation-driven exit above: the refusal is the BASE channel's,
			// and the layer standing over it was never part of what was refused.
			ResolveSlotClaim(Anims, Mesh);
			ArbitrateBase();
			ArbitrateSlot();
			return;
		}
		if (IdealActivity.bHasSequence
			&& ElysiumClipMovement::AnimDrivenArmFor(IdealActivity.Activity)
				!= EElysiumAnimDrivenArm::None)
		{
			// Applied. Retail's apply writes the new ideal activity over the swing's, which is what
			// ends the attack sequence; here the equivalent is giving the base channel back, so this
			// frame's locomotion publish takes the pose from the swing's claim. Without it the claim
			// outranks the publish and the cancel would exist in the classification only.
			//
			// Scoped to the families this rung owns. Retail's apply overwrites ANY ideal activity a
			// reselection is not refused for, but the claim table is not retail's single variable —
			// releasing a scene's or a scripted beat's claim because the gait ladder answered would
			// hand a body back to its locomotion mid-performance. Those producers state no forced
			// activity at all today, so the two rules agree; the gate is what keeps them agreeing
			// when the block, knockback and vomit rows land.
			//
			// Through the one release door, and reported at the same `Verbose` level `ArbitrateBase`
			// reports the identical transition at: this is a claim ending because something outranked
			// it in practice, and a consume that logs nothing is invisible beside one that does.
			const FElysiumAnimRequestSlot& Base =
				Requests[static_cast<int32>(EElysiumAnimChannel::Base)];
			if (Base.bActive)
			{
				UE_LOG(LogElysiumAnimDriver, Verbose,
					TEXT("'%s' reselection to '%s' takes the base pose from %s '%s' (%s)"),
					*Stem, *Intent.Activity, ElysiumAnimIntent::SourceName(Base.Request.Source),
					*Base.Request.Label, ElysiumAnimIntent::PriorityName(Base.Request.Priority));
				ReleaseRequest(Base.Handle);
			}
		}
	}

	// The discrete key. Everything else about the intent is continuous and does not re-select
	// anything: a body that turned, sped up or strafed is playing the same request.
	const bool bChanged = !bResolvedOnce
		|| !Intent.Activity.Equals(LastActivity, ESearchCase::IgnoreCase)
		|| !Intent.Stem.Equals(LastStem, ESearchCase::IgnoreCase)
		// The class body a request translates through is chosen by the actor's classname, and the
		// owner can only push it once its entity resolves — a first tick taken before that resolves
		// against no class body at all, and without this the answer is kept for the life of the
		// request.
		|| !Intent.ActorClassname.Equals(LastActorClassname, ESearchCase::IgnoreCase)
		|| !Intent.WeaponClassname.Equals(LastWeaponClassname, ESearchCase::IgnoreCase)
		// A body going alert changes which sequence set the SAME request resolves against, so the
		// state belongs in the discrete key beside the weapon rather than in the continuous half.
		|| Intent.ActorState != LastActorState
		|| Intent.Route != LastRoute;

	// The overlay slot's own key, ranked beside the base's rather than folded into it. A layer is
	// armed and re-armed without the body's request moving at all — a player standing still empties a
	// magazine into a wall — so the slot has to be able to force a resolve by itself. The HANDLE and
	// not the label: a second shot of the same layer is a new play whose asset has to be re-read and
	// whose rate comes off a new claim, and the label alone cannot tell it from the first.
	const FElysiumAnimRequestSlot& SlotRequest =
		Requests[static_cast<int32>(EElysiumAnimChannel::UpperBody)];
	const uint32 SlotHandle = SlotRequest.bActive ? SlotRequest.Handle : 0;
	const bool bSlotChanged = SlotHandle != LastSlotHandle;
	LastSlotHandle = SlotHandle;

	if (bChanged)
	{
		// The generation advances on the request, not on the frame, so a completed one-shot cannot
		// cancel the request that replaced it.
		++Generation;
		LastActivity = Intent.Activity;
		LastStem = Intent.Stem;
		LastActorClassname = Intent.ActorClassname;
		LastWeaponClassname = Intent.WeaponClassname;
		LastActorState = Intent.ActorState;
		LastRoute = Intent.Route;
		bResolvedOnce = true;
	}
	Intent.Generation = Generation;

	// **A moved slot re-resolves without advancing `Generation`.** The resolve is deterministic on the
	// base key, so re-running it republishes the same base selection; what it is being re-entered for
	// is the layer, which only that call can load. Bumping the generation instead would tell every
	// reader downstream that the body's own request changed, and the base pose would be restated as a
	// fresh transition once per trigger pull.
	if (!bChanged && !bSlotChanged && Selection.Generation == Generation)
	{
		// Step 6 alone: the graph's continuous parameters, rewritten in place. The label, the owning
		// bank and the resolved assets stay exactly as they were, which is what stops the weighted
		// pick and the asset load repeating every tick.
		Selection.MoveYaw = Intent.Body.MoveYaw();
		Selection.Speed = Intent.Body.Speed2D();
		// The stride, refreshed with the direction. Left alone it reports the speed of whichever way
		// the body happened to be going when the activity last changed, so a body that turned while
		// walking would keep quoting its old cell — and `act_stride` is this rung's readback.
		Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
		Selection.AimYaw = Intent.AimYaw;
		Selection.AimPitch = Intent.AimPitch;
		// The latch can move without the activity moving — a landing body that was already walking
		// leaves `Landing` for `Grounded` and keeps its gait — so the phase is continuous state here.
		Selection.AirPhase = Intent.AirPhase;
		for (int32 Axis = 0; Axis < Selection.Axes; ++Axis)
		{
			Selection.AxisValue[Axis] =
				ElysiumAnimResolve::PoseFrom(Intent).Get(Selection.AxisName[Axis]);
		}
		// The verdict is continuous state too: a claim can expire, be released or be outranked on a
		// frame whose discrete request never moved — a body that starts walking under an ambient
		// stance changes the answer without changing what it asked for.
		ArbitrateBase();
		ArbitrateSlot();
		return;
	}

	if (Anims == nullptr)
	{
		// No game instance to read a catalog from. The classifier still ran, so the record says what
		// was asked for and why nothing answered — and the stride is published on this path too,
		// because the tables are the body's and a resolve is not what produced them. A body holding
		// fans it resolved earlier still travels at them; one that never resolved any falls through
		// to the cell speed, which here is zero.
		ElysiumAnimResolve::Resolve(Intent, FElysiumAnimationCatalog(), Selection);
		Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
		Assets = FElysiumResolvedAnimation();
		ArbitrateBase();
		ArbitrateSlot();
		return;
	}

	Anims->ResolveAnimation(Intent, Mesh, Selection, Assets,
		SlotRequest.bActive ? &SlotRequest.Request : nullptr);
	// The resolver answers with the one cell it selected; the stride the body will actually travel
	// at is the table's reading at this direction, which is the same number the mover commands.
	Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
	// After the resolve, which rewrote the whole record: the verdict is step 1's and rides every
	// publish, so it is restated onto whatever the resolver wrote.
	ArbitrateBase();
	ArbitrateSlot();
}
