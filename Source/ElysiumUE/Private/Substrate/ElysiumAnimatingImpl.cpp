// CBaseAnimating, the chain node that owns a skeletal body.
//
// The design is `docs/architecture/runtime-architecture.md` sections 5-6; the public declaration
// stays the chain header `Public/ElysiumPlayer.h`, and the class registration stays at the one
// registration site, `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumNpcVisual.h"

#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"

namespace
{
	// Which channels the event pass polls, and `EventCursors` is sized off this array so a channel
	// added here gets its own cursor with nothing else to change.
	//
	// **The overlay slot is polled beside the base pose, because the ranged families live there.**
	// Every ranged fire and the player's reload compose as retail's `CBaseAnimatingOverlay` slot 0 —
	// a masked partial-body layer over the base — and the shot clips are the ones that carry the
	// 3030-3044 commit ids. A pass that polled the base alone would walk the gait's timeline while
	// the shot's went unread, and `FElysiumWeapon::CommitArrivesFromAnimEvent` would answer false for
	// every player shot: the commit would silently fall back to the `ContactEventCycle` estimate with
	// nothing but a Verbose line to say so.
	//
	// The three remaining channels stay out: nothing publishes a phase for them, and a channel
	// nothing answers for costs one seam call per body per frame to learn nothing.
	constexpr EElysiumAnimChannel GPolledEventChannels[] = {
		EElysiumAnimChannel::Base, EElysiumAnimChannel::UpperBody };

	// A phase the seam answered TRUE for but that names no clip, or sits outside `[0,1)`, is a
	// producer defect: the dispatcher's whole rule is an interval over that number, so a bad one
	// silently mis-fires or drops a timeline. Warned once per spelling — a body republishes its
	// phase every frame, and a defect restated sixty times a second buries everything else.
	bool ShouldReportBadPhase(const FElysiumClipPhase& Phase)
	{
		static TSet<FString> Reported;
		const FString Key = FString::Printf(TEXT("%s|%s|%s"), *Phase.OwnerStem, *Phase.OwnerRoot, *Phase.Label);
		bool bAlready = false;
		Reported.Add(Key, &bAlready);
		return !bAlready;
	}
}

// --- FElysiumAnimating — CBaseAnimating ---

FString FElysiumAnimating::ModelStem() const
{
	return FPaths::GetBaseFilename(Model).ToLower();
}

void FElysiumAnimating::BuildBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def || Model.IsEmpty())
	{
		return;   // bare test world, no embodiment, or a bodiless character (npc_VCamera has no model)
	}

	// Source `angles` is [pitch yaw roll]; a standing character needs yaw only, and a baked body's
	// authored forward is its own component +X, so the placement is the reflected yaw and nothing
	// else (ElysiumSkeletalBasis).
	const FRotator Rot = ElysiumSkeletalBasis::FromSourceAngles(Angles);
	Visual = Embodiment->BuildNpcVisual(ModelStem(), Origin, Rot, Embodiment->BodyScaleFor(*Def),
		Disposition, IdleVariant());
	if (Visual)
	{
		World->RegisterNpcBody(Visual);
		Embodiment->UpdateNpcDisposition(Visual, Disposition, DispositionLevel);
		RefreshDispositionExpression();
		if (IsInert())
		{
			GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill)
		}
	}
}

bool FElysiumAnimating::PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds)
{
	// The band-less door, and it means the ambient band: one clip, one claim, and the claim goes when
	// the clip does. Everything a run does differently it states on its own segment record.
	FElysiumClipSegment Segment;
	Segment.ClipName = ClipName;
	Segment.bLoop = bLoop;
	return PlayAnimSegment(Segment, OutSeconds);
}

bool FElysiumAnimating::PlayAnimSegment(const FElysiumClipSegment& Segment, float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || !Segment.IsValid())
	{
		return false;
	}
	return Embodiment->PlayNpcClip(Visual, ModelStem(), Segment, OutSeconds);
}

void FElysiumAnimating::ReleaseAnimSegment()
{
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr; Embodiment && Visual)
	{
		Embodiment->ReleaseNpcSegment(Visual);
	}
}

bool FElysiumAnimating::PreloadAnimClip(const FString& ClipName)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && !ClipName.IsEmpty()
		&& Embodiment->PreloadNpcClip(Visual, ModelStem(), ClipName);
}

bool FElysiumAnimating::PlayCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || AnimSetModel.IsEmpty() || ClipName.IsEmpty())
	{
		return false;
	}
	return Embodiment->PlayCinematicClip(Visual, ModelStem(), AnimSetModel, BoneRoot, ClipName,
		bLoop, OutSeconds);
}

bool FElysiumAnimating::PreloadCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
	const FString& ClipName)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && !AnimSetModel.IsEmpty() && !ClipName.IsEmpty()
		&& Embodiment->PreloadCinematicClip(
			Visual, ModelStem(), AnimSetModel, BoneRoot, ClipName);
}

bool FElysiumAnimating::SeekCinematicClip(float PositionSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && Embodiment->SeekCinematicClip(Visual, PositionSeconds);
}

void FElysiumAnimating::StopCinematicClip()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return;
	}
	// The scene's base-channel claim goes back FIRST, on every stop path — cancel and natural
	// finish both stop through here. The idle below submits its own ambient claim, and a scene
	// claim left standing would refuse it (and every claim after it) forever, parking the base
	// channel on a scene that no longer plays.
	Embodiment->ReleaseCinematicClaim(Visual);
	// Crossfade out of the cinematic pose; only tear the player down when there is no idle to go to.
	// Stopping first empties the animation host, which makes the idle behind it SNAP in from nothing
	// (the host has nothing to blend from, so it treats the idle as a first clip) and discards the
	// outgoing pose that the next scene's opening clip has to blend out of. Between two chained
	// scenes that is two hard pops a frame apart, which is what the theatre's courtroom hand-offs
	// read as. With no idle resolved, StopCinematicClip leaves the body in its reference pose, so it
	// stays the fallback rather than the first move.
	if (!ResetAnimToIdle())
	{
		Embodiment->StopCinematicClip(Visual);
	}
}

int32 FElysiumAnimating::SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
	TArray<FString>* OutMissing)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return INDEX_NONE;   // a bodiless or headless character has no face to move
	}
	return Embodiment->SetFlexControllers(Visual, Writes, OutMissing);
}

bool FElysiumAnimating::SetMouthOpen(float Open)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;   // a bodiless or headless character has no jaw to move
	}
	return Embodiment->SetMouthOpen(Visual, Open);
}

bool FElysiumAnimating::GetPhonemeFilter(float& OutMin, float& OutMax) const
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;   // a bodiless or headless character speaks with no filter to read
	}
	return Embodiment->GetPhonemeFilter(Visual, OutMin, OutMax);
}

bool FElysiumAnimating::ResetAnimToIdle()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;
	}
	return Embodiment->RefreshNpcIdle(
		Visual, ModelStem(), Disposition, DispositionLevel, IdleVariant());
}

bool FElysiumAnimating::HasLiveAnimEventDispatch(const FString& OwnerStem, const FString& Label) const
{
	FElysiumClipPhase Ignored;
	return GetLiveClipPhase(OwnerStem, Label, Ignored);
}

bool FElysiumAnimating::GetLiveClipPhase(const FString& OwnerStem, const FString& Label,
	FElysiumClipPhase& Out) const
{
	Out = FElysiumClipPhase();

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || OwnerStem.IsEmpty() || Label.IsEmpty())
	{
		return false;
	}
	// The same channel list the pass walks, asked the same way, and matched on the same identity the
	// cursor uses — a phase for SOME clip proves nothing about the clip the caller named. The
	// comparison is case-insensitive because `ElysiumAnimEvents::Advance` compares the cursor's own
	// (owner, label) that way, and content spells a label however it likes.
	//
	// `PlayId` is deliberately not part of this test: the caller is asking whether the clip it just
	// started is on a channel the pass polls, and the play it is asking about is the one standing
	// there now.
	for (const EElysiumAnimChannel Channel : GPolledEventChannels)
	{
		FElysiumClipPhase Phase;
		if (Embodiment->GetBodyClipPhase(Visual, Channel, Phase)
			&& Phase.OwnerStem.Equals(OwnerStem, ESearchCase::IgnoreCase)
			&& Phase.Label.Equals(Label, ESearchCase::IgnoreCase))
		{
			Phase.Channel = Channel;
			Out = Phase;
			return true;
		}
	}
	return false;
}

void FElysiumAnimating::AdvanceAnimEvents()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return;   // a bodiless character, a headless world, or bodies off — nothing is playing
	}

	// Reused across channels and across bodies within the frame: the array holds borrowed pointers
	// into the timeline the seam handed back, and `Advance` resets it before every walk.
	TArray<const FElysiumAnimEvent*> Fired;

	constexpr int32 NumPolled = static_cast<int32>(UE_ARRAY_COUNT(GPolledEventChannels));
	if (EventCursors.Num() != NumPolled)
	{
		EventCursors.SetNum(NumPolled);
	}

	for (int32 Slot = 0; Slot < NumPolled; ++Slot)
	{
		FElysiumAnimEventCursor& Cursor = EventCursors[Slot];

		FElysiumClipPhase Phase;
		if (!Embodiment->GetBodyClipPhase(Visual, GPolledEventChannels[Slot], Phase))
		{
			// This channel is playing nothing. The cursor forgets where it was, so the next clip to
			// arm here cannot inherit a position from a different timeline.
			Cursor.Reset();
			continue;
		}
		Phase.Channel = GPolledEventChannels[Slot];

		// `1.0` is legal and is the terminal position of a finished one-shot, which has nowhere to
		// wrap to; anything outside `[0,1]` is not a phase at all. A non-finite cycle is tested
		// explicitly because it satisfies neither comparison — a length-zero clip divided into a
		// position is the way one arrives — and would otherwise pass this gate unremarked.
		if (!Phase.IsValid() || !FMath::IsFinite(Phase.Cycle)
			|| Phase.Cycle < 0.0f || Phase.Cycle > 1.0f)
		{
			// The seam said a clip is playing and then described one that cannot be walked. The pass
			// runs on with what it was given — `Advance` clamps and a nameless clip fires nothing —
			// but the defect is named here, where the body it came from can be identified.
			if (ShouldReportBadPhase(Phase))
			{
				UE_LOG(LogElysiumPlayer, Warning,
					TEXT("anim events on '%s': the pose layer reported clip '%s'@'%s' at cycle %.4f, "
					     "which is not a normalized [0,1) phase — its timeline cannot be walked "
					     "faithfully"),
					*ModelStem(), *Phase.Label, *Phase.OwnerStem, Phase.Cycle);
			}
		}

		const TArray<FElysiumAnimEvent>* Timeline =
			Embodiment->GetNpcEventTimeline(Phase.OwnerStem, Phase.Label, Phase.OwnerRoot);
		ElysiumAnimEvents::Advance(Timeline, Phase, Cursor, Fired);

		for (const FElysiumAnimEvent* Record : Fired)
		{
			// The server band, exactly as `DispatchAnimEvents` applies it: an id at or above the
			// ceiling is never offered to a handler at all.
			const bool bAboveBand = Record->Event >= ElysiumAnimEvents::ServerDispatchCeiling;
			if (!bAboveBand && HandleAnimEvent(*Record))
			{
				continue;   // claimed and acted on; the handler owns its own observability
			}
			// Unclaimed. The census IS the report for this — an id with no handler is work that has
			// not landed, not a fault, and a line per occurrence would fire every footstep of every
			// walking body. One Verbose line the first time each (id, owner, label) is seen.
			if (ElysiumAnimEventCensus::Record(*Record, Phase.OwnerStem, Phase.Label, bAboveBand))
			{
				UE_LOG(LogElysiumPlayer, Verbose,
					TEXT("anim event %d on '%s'@'%s' is unclaimed%s%s"),
					Record->Event, *Phase.Label, *Phase.OwnerStem,
					bAboveBand ? TEXT(" (above the server dispatch band)") : TEXT(""),
					Record->Options.IsEmpty()
						? TEXT("") : *FString::Printf(TEXT(", options '%s'"), *Record->Options));
			}
		}
	}
}

bool FElysiumAnimating::SetDispositionName(const FString& NewDisposition)
{
	return SetDisposition(NewDisposition, 1);
}

bool FElysiumAnimating::CommitDisposition(const FString& NewDisposition, int32 NewLevel,
	bool& bOutChanged, FElysiumDisposition* OutOld, FElysiumDisposition* OutNew)

{
	bOutChanged = false;
	if (NewDisposition.IsEmpty())
	{
		return false;
	}
	FElysiumDisposition OldRow;
	FElysiumDisposition NewRow;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	const bool bOldResolved = Embodiment
		&& Embodiment->ResolveDisposition(Disposition, DispositionLevel, OldRow);
	const bool bNewResolved = Embodiment
		&& Embodiment->ResolveDisposition(NewDisposition, FMath::Max(1, NewLevel), NewRow);
	if (OutOld)
	{
		*OutOld = bOldResolved ? OldRow : FElysiumDisposition();
	}
	if (OutNew)
	{
		*OutNew = bNewResolved ? NewRow : FElysiumDisposition();
	}

	const FString ResolvedName = bNewResolved ? NewRow.Name : NewDisposition;
	const int32 ResolvedLevel = bNewResolved ? NewRow.Level : FMath::Max(1, NewLevel);
	bOutChanged = !Disposition.Equals(ResolvedName, ESearchCase::IgnoreCase)
		|| DispositionLevel != ResolvedLevel;
	if (!bOutChanged)
	{
		return true;
	}
	Disposition = ResolvedName;
	DispositionLevel = ResolvedLevel;
	if (Embodiment && Visual)
	{
		Embodiment->UpdateNpcDisposition(Visual, Disposition, DispositionLevel);
	}
	RefreshDispositionExpression();
	return true;
}

bool FElysiumAnimating::SetDisposition(const FString& NewDisposition, int32 NewLevel)
{
	bool bChanged = false;
	if (!CommitDisposition(NewDisposition, NewLevel, bChanged))
	{
		return false;
	}
	if (bChanged)
	{
		ResetAnimToIdle();
	}
	return true;
}

void FElysiumAnimating::SetDispositionTalking(bool bTalking)
{
	if (bDispositionTalking == bTalking)
	{
		return;
	}
	bDispositionTalking = bTalking;
	RefreshDispositionExpression();
}

void FElysiumAnimating::AccumulateDispositionFacialPose(TMap<FString, float>& InOutPose) const
{
	for (const TPair<FString, float>& Key : DispositionFacialPose)
	{
		InOutPose.Add(Key.Key, Key.Value);
	}
}

void FElysiumAnimating::RefreshDispositionExpression()
{
	TMap<FString, float> Next;
	FElysiumDisposition Row;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment && Visual
		&& Embodiment->ResolveDisposition(Disposition, DispositionLevel, Row))
	{
		const FString Expression = bDispositionTalking && !Row.TalkingExpression.IsEmpty()
			? Row.TalkingExpression : Row.DefaultExpression;
		const TSharedPtr<const FElysiumExpressionTable> Table =
			ElysiumExpressions::Load(ModelStem(), TEXT("expressions"));
		const int32 Index = Table.IsValid() ? Table->FindRow(Expression) : INDEX_NONE;
		if (Table.IsValid() && Table->Rows.IsValidIndex(Index))
		{
			const FElysiumExpressionRow& ExpressionRow = Table->Rows[Index];
			for (int32 Key = 0; Key < Table->Keys.Num(); ++Key)
			{
				const float Influence = FMath::Clamp(
					ExpressionRow.Weights[Key] * Row.ExpressionIntensity, 0.f, 1.f);
				if (Influence > 0.f)
				{
					Next.Add(Table->Keys[Key], ExpressionRow.Values[Key] * Influence);
				}
			}
		}
	}

	TArray<FElysiumFlexWrite> Writes;
	for (const TPair<FString, float>& Key : Next)
	{
		Writes.Add({ Key.Key, Key.Value });
	}
	for (const TPair<FString, float>& Key : DispositionFacialPose)
	{
		if (!Next.Contains(Key.Key))
		{
			Writes.Add({ Key.Key, 0.f });
		}
	}
	if (!Writes.IsEmpty())
	{
		SetFlexControllers(Writes, nullptr);
	}
	DispositionFacialPose = MoveTemp(Next);

	// The body policy above is the emotional/presentation transaction. Relationship and RPG
	// reaction remain independent stores; no value is derived from this one.
}

void FElysiumAnimating::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Visual)
	{
		Visual->SetRelativeLocation(Origin);
		Visual->SetRelativeRotation(ElysiumSkeletalBasis::FromSourceAngles(Angles));
	}
}

void FElysiumAnimating::OnRuntimeModelChanged()
{
	if (!World)
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;   // bare test world — the logical Model field is still updated
	}
	// A model swap replaces the body, it does not remove it, so anything parented to this character
	// (PostSpawn attaches a child to GetAttachBody() = Visual) has to survive onto the new one.
	// Nothing tracks an entity's children, so read them off the component before it is destroyed and
	// carry their offsets across — a `parentname` child keeps its relative pose, not its world pose.
	// The socket travels with the child: a bone-attached env_particle re-parented to the bare root
	// would silently stop tracking the bone the first time the level script re-models the character,
	// which is exactly when the cinematic emitters are live.
	struct FCarriedChild
	{
		TWeakObjectPtr<USceneComponent> Component;
		FTransform RelativeTransform;
		FName Socket;
	};
	TArray<FCarriedChild> Carried;
	if (Visual)
	{
		for (USceneComponent* Child : Visual->GetAttachChildren())
		{
			if (Child)
			{
				Carried.Add({ Child, Child->GetRelativeTransform(), Child->GetAttachSocketName() });
			}
		}
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	BuildBody();
	if (Visual)
	{
		for (const FCarriedChild& Child : Carried)
		{
			if (USceneComponent* Live = Child.Component.Get())
			{
				const bool bKeepSocket = Child.Socket != NAME_None
					&& Visual->DoesSocketExist(Child.Socket);
				Live->AttachToComponent(Visual, FAttachmentTransformRules::KeepRelativeTransform,
					bKeepSocket ? Child.Socket : NAME_None);
				Live->SetRelativeTransform(Child.RelativeTransform);
			}
		}

		// Runtime animation objects are bound to the old body's transient USkeleton. Re-walk the
		// dormant plan against this replacement immediately so compression for a Python recast can
		// overlap the authored lead to its scene; InputStart owns the completion barrier.
		if (World->IsActive())
		{
			World->RefreshAnimationPreload();
		}
	}
}

void FElysiumAnimating::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateVisual();
}

void FElysiumAnimating::GateVisual()
{
	if (Visual)
	{
		const bool bShown = !IsInert();
		Visual->SetVisibility(bShown);
		Visual->SetComponentTickEnabled(bShown);   // pause the idle clip while hidden
		ElysiumNpcVisual::GateLeaderCloth(Visual, bShown);
	}
}
