#include "Visual/ElysiumAnimationDriver.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumItemClasses.h"   // Inventory.Active() is read for its classname
#include "Substrate/ElysiumNpc.h"           // the mind's state, which the alert/relaxed branch reads

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
		return;
	}

	// A body with no mind is not a cast member and has no state to read; idle is what the recovered
	// tree answers for every state that is neither alert nor combat, so it is the honest default
	// rather than a placeholder.
	const FElysiumNpc* Npc = Char->AsNpc();
	ActorState = Npc != nullptr ? Npc->GetMind().State() : EElysiumNpcState::Idle;

	// The classname a map AUTHORS, which is the key both committed ledgers are joined to. The
	// registered descriptor answers for a character no def produced, which is the player's case.
	if (Char->Def != nullptr)
	{
		Adopt(ActorClassname, Char->Def->Classname);
	}
	else if (Char->Class != nullptr)
	{
		Adopt(ActorClassname, Char->Class->ClassName.ToString());
	}
	else
	{
		ActorClassname.Reset();
	}

	const FElysiumItem* Active = Char->Inventory.Active(*Char);
	if (Active != nullptr && Active->Def != nullptr)
	{
		Adopt(WeaponClassname, Active->Def->Classname);
	}
	else
	{
		WeaponClassname.Reset();
	}
}

FElysiumGaitSpeedRequest FElysiumAnimationDriver::BuildGaitKey() const
{
	// **The whole translation context, not a subset.** The tables are resolved through the same
	// chain the pose is: same source, same class body, same alert/relaxed branch.
	FElysiumGaitSpeedRequest Key;
	Key.Stem = Stem;
	Key.Source = Source;
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
	// scripted task. Passing the producer here is what keeps that a stated rule rather than a
	// coincidence of what the movers happen to report.
	const bool bCommandsJumps = Source == EElysiumAnimSource::Player;
	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Body, DeltaSeconds, Gait, OneShot,
		bCommandsJumps);

	FElysiumAnimationIntent Intent = ElysiumAnimIntent::BuildLocomotionIntent(Body, Latch, Gait,
		Source, Stem, Character, Variant);
	// The translation context, which the classifier has no business knowing: it answers what the body
	// did, and these answer which sequence set realizes it.
	Intent.ActorClassname = ActorClassname;
	Intent.WeaponClassname = WeaponClassname;
	Intent.ActorState = ActorState;
	Intent.FormTag = FormTag;

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
