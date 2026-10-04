// CBaseAnimating, the chain node that owns a skeletal body.
//
// The public declaration is `Public/ElysiumAnimating.h`, and the class registration stays at the
// one registration site, `ElysiumPlayerClasses.cpp`.

#include "ElysiumAnimating.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumCharacterModel.h"
#include "Visual/ElysiumExpressionPreparation.h"

#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"

namespace
{
	// The channels `GetLiveClipPhase` asks the pose layer about: the base pose and retail's
	// `CBaseAnimatingOverlay` slot 0, the masked partial-body layer every ranged fire and the
	// player's reload compose on (the shot clips carry the 3030-3044 commit ids). The world-tick
	// event poll that walked these two is deleted (spec 0002 V4a, K3): events are dispatched by
	// `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` from each entity's own think.
	constexpr EElysiumAnimChannel GPolledEventChannels[] = {
		EElysiumAnimChannel::Base, EElysiumAnimChannel::UpperBody };
}

// --- FElysiumAnimating — CBaseAnimating ---

FString FElysiumAnimating::ModelStem() const
{
	return ElysiumCharacterModel::IdFromSource(Model);
}

void FElysiumAnimating::BuildBody()
{
	if (!PrepareCharacterVisual()) return;
	InstallPreparedCharacterVisual();
	RestoreModelChildren();
}

bool FElysiumAnimating::PrepareCharacterVisual()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def || Model.IsEmpty()) return false;
	FString Error;
	const auto Result = Embodiment->RequestCharacterModel(Handle, ModelStem(), ++CharacterVisualGeneration, Error);
	if (Result == EElysiumCharacterModelAdmission::Rejected)
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s: character model admission refused: %s"), *DebugString(), *Error);
	return Result == EElysiumCharacterModelAdmission::Ready;
}

void FElysiumAnimating::InvalidateCharacterVisualRequest()
{
	++CharacterVisualGeneration;
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		Embodiment->CancelCharacterModel(Handle);
}

void FElysiumAnimating::CompletePreparedCharacterVisual(uint64 Generation, const FString& ModelId)
{
	if (!World || IsDead() || World->GetEpoch() != Handle.Epoch || World->Resolve(Handle) != this
		|| Generation != CharacterVisualGeneration || ModelStem() != ModelId || Visual) return;
	InstallPreparedCharacterVisual();
	if (!Visual)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s: admitted native model %s did not construct a visual"), *DebugString(), *ModelId);
		return;
	}
	RestoreModelChildren();
	OnPreparedVisualAttached();
	RefreshPreparedExpressions();
}

void FElysiumAnimating::InstallPreparedCharacterVisual()
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
		// A character body that answers the class verb is also a +use target, exactly as a usable
		// prop's body is (`FElysiumProp`'s animated branch registers the same anchor). Without this
		// the `ElysiumUse` trace never hits a standing character, so an NPC carrying a `dialogname`
		// could never be focused and `+use` could never open its conversation (D3). Inert bodies are
		// registered disabled by `RegisterUseAnchor` itself and re-enabled by the life-state hook.
		if (IsUsable() && !Def->bSky)
		{
			World->RegisterUseAnchor(Visual, Handle);
		}
		Embodiment->UpdateNpcDisposition(Visual, Disposition, DispositionLevel);
		RefreshDispositionExpression();
		if (IsInert() || !IsTransmitted())
		{
			// Born hidden (start_hidden / a Spawn()-time Kill), or born undrawn: a body admitted
			// after `EF_NODRAW` was raised (the controller's asynchronous model) must not show.
			GateVisual();
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
	// Matched on the clip's identity -- a phase for SOME clip proves nothing about the clip the
	// caller named. The comparison is case-insensitive because content spells a label however it
	// likes.
	//
	// `PlayId` is deliberately not part of this test: the caller is asking whether the clip it just
	// started is on one of the two channels, and the play it is asking about is the one standing
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
		FString ExpressionDiagnostic;
		const TSharedPtr<const FElysiumExpressionTable> Table =
			ElysiumExpressions::LoadPreparedModelSelection(*this, TEXT("expressions"), ExpressionDiagnostic);
		if (!ExpressionDiagnostic.IsEmpty())
		{
			UE_LOG(LogElysiumPlayer, Warning, TEXT("%s: disposition expression: %s"),
				*DebugString(), *ExpressionDiagnostic);
		}
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
	InvalidateCharacterVisualRequest();
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
	if (Visual)
	{
		for (USceneComponent* Child : Visual->GetAttachChildren())
		{
			if (Child)
			{
				PendingModelChildren.Add({ Child, Child->GetRelativeTransform(), Child->GetAttachSocketName() });
			}
		}
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	BuildBody();
	// Admission discovers and loads the complete native closure. Do not restart a
	// synchronous map-wide preload from a gameplay input or its async completion.
}

void FElysiumAnimating::RestoreModelChildren()
{
	if (Visual)
	{
		for (const FCarriedModelChild& Child : PendingModelChildren)
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
		PendingModelChildren.Reset();
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
		// Dormancy stops the body; `EF_NODRAW` only stops it being drawn. A live untransmitted body
		// still ticks and still evaluates its pose off screen (the controller's pose is what the pawn
		// draws, `IElysiumEmbodiment::SetPlayerBodyPoseSource`).
		const bool bDrawn = bShown && IsTransmitted();
		// The pose layer reveals a body when it commits a clip; the tag withholds that reveal from a
		// body the entity says is never drawn, so a scripted beat cannot put the stand-in on screen.
		// Keyed on the effects word alone: an inert untransmitted body must not be revealed either.
		if (!IsTransmitted())
		{
			Visual->ComponentTags.AddUnique(ElysiumNpcVisual::UntransmittedBodyTag());
		}
		else
		{
			Visual->ComponentTags.Remove(ElysiumNpcVisual::UntransmittedBodyTag());
		}
		Visual->SetVisibility(bDrawn);
		Visual->SetComponentTickEnabled(bShown);   // pause the idle clip while hidden
		if (bShown && !bDrawn)
		{
			Visual->VisibilityBasedAnimTickOption =
				EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		ElysiumNpcVisual::GateLeaderCloth(Visual, bDrawn);
	}
}
