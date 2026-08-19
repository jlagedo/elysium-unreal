// 11.4 — CBaseAnimating, the chain node that owns a skeletal body (S3).
//
// Moved verbatim out of `ElysiumPlayerClasses.cpp` when the discipline runtime landed; the file
// granularity rule in `Source/ElysiumUE/CLAUDE.md` puts one primary class per `.cpp`. The design is
// `docs/architecture/runtime-architecture.md` sections 5-6; the public declaration stays the chain
// header `Public/ElysiumPlayer.h`, and the class registration stays at the one registration site,
// `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumNpcVisual.h"

#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"

// ============================================================================================
// FElysiumAnimating — CBaseAnimating
// ============================================================================================

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
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || ClipName.IsEmpty())
	{
		return false;
	}
	return Embodiment->PlayNpcClip(Visual, ModelStem(), ClipName, bLoop, OutSeconds);
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
