#include "Visual/ElysiumAnimationDriver.h"

#include "ElysiumEntityWorld.h"             // the clock the combat-stance window is read against
#include "ElysiumPlayer.h"
#include "Visual/ElysiumActionTables.h"     // the committed player gait ladder (LIFE4, Option A)

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnimDriver, Log, All);

void FElysiumAnimationDriver::Reset()
{
	Latch = FElysiumJumpLatch();
	Selection = FElysiumAnimationSelection();
	Assets = FElysiumResolvedAnimation();
	// The published pair goes together: a producer left holding last epoch's sample beside this
	// epoch's empty record would trace a body that moved through a frame nothing classified.
	Sample = FElysiumLocomotionSample();
	LastActivity.Reset();
	LastStem.Reset();
	LastActorClassname.Reset();
	LastWeaponClassname.Reset();
	LastRoute = EElysiumAnimRoute::Activity;
	bResolvedOnce = false;
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

	// The pose parameter is a rate, and this is the one place per body per frame — a producer's
	// sample is a getter a readout may take twice, so it seeds the value and cannot advance it.
	FElysiumLocomotionSample Body = InSample;
	Body.MoveYawPose = ElysiumLocomotion::AdvanceMoveYaw(MoveYawFilter, InSample.MoveYawVelocity,
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
	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Body, DeltaSeconds, Gait, OneShot,
		bCommandsJumps);

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
	}

	// The body key, resolved ahead of the request so a body that changed model this frame steers by
	// its new speeds rather than by one frame of its old ones.
	RefreshGaitSpeeds(Anims);

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

	if (!bChanged && Selection.Generation == Generation)
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
		return;
	}

	Anims->ResolveAnimation(Intent, Mesh, Selection, Assets);
	// The resolver answers with the one cell it selected; the stride the body will actually travel
	// at is the table's reading at this direction, which is the same number the mover commands.
	Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
	// After the resolve, which rewrote the whole record: the verdict is step 1's and rides every
	// publish, so it is restated onto whatever the resolver wrote.
	ArbitrateBase();
}
