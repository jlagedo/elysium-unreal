#include "ElysiumEntity.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumLineService.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEntityBase, Log, All);

void FElysiumEntity::Construct(const FElysiumEntityDef& InDef, FElysiumEntityHandle InHandle, const FElysiumClassDesc& InClass)
{
	Def = &InDef;
	Handle = InHandle;
	Class = &InClass;
	TargetName = InDef.TargetName;
	Origin = InDef.Origin;   // the live copy; the def's is immutable (SetOrigin moves this one)

	// Apply the raw keyvalues through the class chain field table. Only mapped base/leaf
	// fields are copied onto members; unmapped keys stay on the def (property-bag reads land
	// with the script host later). Spawn-time application ignores bKeyable — the write-gate is
	// for runtime Python/I/O, not the map's own keyvalues.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (const TPair<FString, FString>& KV : InDef.Keys)
	{
		if (const FElysiumFieldAccessor* Acc = Reg.FindField(InClass, FName(*KV.Key)))
		{
			if (Acc->Set)
			{
				Acc->Set(*this, FElysiumVariant::String(KV.Value));
			}
		}
	}

	// Seed the per-output `times` counters from the def (the world counts them down as it fires).
	OutputTimesRemaining.Reserve(InDef.Outputs.Num());
	for (const FElysiumOutputDef& O : InDef.Outputs)
	{
		OutputTimesRemaining.Add(O.Times);
	}

	// start_hidden — born fully OFF. No prior think to save; the body build skips
	// collision + draw while bHidden.
	if (InDef.bStartHidden)
	{
		bHidden = true;
		NextThink = ELYSIUM_NEVER_THINK;
	}
}

void FElysiumEntity::ScriptHide()
{
	// CBaseEntity::ScriptHide (entity_io.md): early-out if already hidden; save the prior
	// think; next-think = never; go non-solid + undrawn (the body, via OnDormancyChanged).
	if (bHidden)
	{
		return;
	}
	bHidden = true;
	SavedNextThink = NextThink;
	NextThink = ELYSIUM_NEVER_THINK;
	OnDormancyChanged();
}

void FElysiumEntity::ScriptUnhide()
{
	// The exact inverse: restore the saved think and clear the hidden flag; the body restores
	// its prior solidity + draw.
	if (!bHidden)
	{
		return;
	}
	bHidden = false;
	NextThink = SavedNextThink;
	OnDormancyChanged();
}

void FElysiumEntity::Kill()
{
	// Terminal: mark dead and go inert immediately (a killed-but-not-yet-reaped entity must
	// not touch, trace, or think). The slot removal + handle invalidation is the world's job;
	// this only flips the entity's own state.
	if (bDead)
	{
		return;
	}
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::RemovedAlive);
	if (World)
	{
		if (IElysiumAudio* Audio = World->Audio())
		{
			FElysiumAudioOwner AudioOwner;
			AudioOwner.Kind = EElysiumAudioOwnerKind::MapEntity;
			AudioOwner.StableId =
				FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
			Audio->CancelAudioOwner(MoveTemp(AudioOwner));
		}
		if (World->Lines())
		{
			World->Lines()->CancelSession(
				FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index));
			World->Lines()->CancelDialogue(Handle);
		}
	}
	bDead = true;
	NextThink = ELYSIUM_NEVER_THINK;
	if (!bHidden)
	{
		OnDormancyChanged();   // drop the body's collision + draw (no-op with no body); notifies below
	}
	else if (World)
	{
		// Already hidden (OnDormancyChanged is skipped), but the visual state still changed
		// hidden -> dead, so a retained visualizer must still be told.
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::NotifyOwnerOfTermination(EElysiumOwnedEntityTermination Reason)
{
	if (bOwnerTerminationNotified)
	{
		return;
	}
	bOwnerTerminationNotified = true; // latch before the callback: owner outputs may re-enter us
	if (!World || !OwnerEntity.IsSet())
	{
		return;
	}
	if (FElysiumEntity* Owner = World->Resolve(OwnerEntity))
	{
		Owner->OnOwnedEntityTerminated(*this, Reason);
	}
}

void FElysiumEntity::PlayDialogFile(const FString& AuthoredPath)
{
	if (!World || bFakeSilence || AuthoredPath.IsEmpty() || !World->Lines())
	{
		return;
	}
	FElysiumEntity* SoundOwner = this;
	if (!SoundOverrideEntityName.IsEmpty())
	{
		if (FElysiumEntity* Override = World->FindByName(SoundOverrideEntityName))
		{
			SoundOwner = Override;
		}
	}
	const FString Session =
		FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index);
	World->Lines()->PlayDirect(Session, AuthoredPath, SoundOwner->Origin,
		SoundOwner->GetSkeletalBody(), EElysiumAudioCategory::Auto);
}

void FElysiumEntity::SetSoundOverrideEnt(const FString& EntityName)
{
	SoundOverrideEntityName = EntityName.TrimStartAndEnd();
}

void FElysiumEntity::SetFakeSilence(bool bEnabled)
{
	bFakeSilence = bEnabled;
	if (bEnabled && World && World->Lines())
	{
		World->Lines()->CancelSession(
			FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index));
		World->Lines()->CancelDialogue(Handle);
	}
}

UPrimitiveComponent* FElysiumEntity::GetAttachBody() const
{
	return Body ? static_cast<UPrimitiveComponent*>(Body)
		: GenericModelBody ? static_cast<UPrimitiveComponent*>(GenericModelBody) : GenericStaticModelBody;
}

USceneComponent* FElysiumEntity::GetAttachChild() const
{
	return GetAttachBody();
}

void FElysiumEntity::EnsurePlacedModelBody()
{
	if (GetAttachBody() || !World || !Def || Model.IsEmpty() || Def->ModelMesh.IsEmpty())
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment || !Embodiment->HasPlacedModelCatalogue())
	{
		return;
	}
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = Def->ModelMesh;
	Request.Location = Origin;
	Request.Rotation = Def->ModelQuat;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	const auto Placed = Embodiment->BuildPlacedModelBody(Request);
	GenericModelBody = Cast<USkeletalMeshComponent>(Placed.Visual);
	GenericStaticModelBody = GenericModelBody ? nullptr : Placed.Visual;
	if (GenericStaticModelBody)
	{
		World->RegisterPropBody(GenericStaticModelBody);
		GenericStaticModelBody->SetVisibility(!IsInert(), true);
	}
	if (GenericModelBody)
	{
		World->RegisterNpcBody(GenericModelBody);
		GenericModelBody->SetVisibility(!IsInert(), true);
	}
}

void FElysiumEntity::PostSpawn()
{
	ResolveParentAttachment(false);
}

bool FElysiumEntity::ResolveParentAttachment(bool bWarnIfPending)
{
	MoveParent = FElysiumEntityHandle::Invalid();
	if (ParentName.IsEmpty() || !World)
	{
		return true;
	}
	FElysiumEntity* Parent = World->FindByName(ParentName);
	if (!Parent)
	{
		if (bWarnIfPending)
		{
			UE_LOG(LogElysiumEntityBase, Warning,
				TEXT("%s cannot resolve parent entity '%s'"), *DebugString(), *ParentName);
		}
		return false;
	}
	if (Parent == this)
	{
		UE_LOG(LogElysiumEntityBase, Warning,
			TEXT("%s cannot parent itself through '%s'"), *DebugString(), *ParentName);
		return false;
	}
	MoveParent = Parent->Handle;

	USceneComponent* ChildBody = GetAttachChild();
	if (!ChildBody)
	{
		return true; // valid logical parenting between entities that need no scene component
	}
	UPrimitiveComponent* ParentBody = Parent->GetAttachBody();
	if (!ParentBody)
	{
		if (bWarnIfPending)
		{
			UE_LOG(LogElysiumEntityBase, Warning,
				TEXT("%s resolved parent '%s', but its attachment body is unavailable"),
				*DebugString(), *ParentName);
		}
		return false;
	}
	if (ChildBody->GetAttachParent() == ParentBody)
	{
		return true;
	}
	const FTransform ParentWorld = ParentBody->GetComponentTransform();
	if (!ChildBody->AttachToComponent(ParentBody, FAttachmentTransformRules::KeepWorldTransform))
	{
		if (bWarnIfPending)
		{
			UE_LOG(LogElysiumEntityBase, Warning, TEXT("%s failed to attach to parent '%s'"),
				*DebugString(), *ParentName);
		}
		return false;
	}
	OnParentAttached(ParentWorld);
	return true;
}

void FElysiumEntity::OnDormancyChanged()
{
	if (GenericStaticModelBody) GenericStaticModelBody->SetVisibility(!IsInert(), true);
	// One reversible switch. Inert (hidden or dead) drops the body's collision so it cannot
	// be touched or traced; active restores its built solidity. Idempotent (SetDormant re-applies).
	RefreshBrushBodyState();
	if (GenericModelBody)
	{
		GenericModelBody->SetVisibility(!IsInert(), true);
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
	// The visual (colour/visibility) changed; let a retained gizmo layer dirty this one
	// instance on the event rather than polling every entity every frame. No-op in normal play.
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::RefreshBrushBodyState()
{
	const bool bEnabled = IsBrushBodyEnabled();
	if (!bEnabled && World)
	{
		World->EndBrushTouches(Handle);
	}
	if (Body)
	{
		Body->SetDormant(!bEnabled);
	}
}

void FElysiumEntity::SetRuntimeOrigin(const FVector& NewOrigin)
{
	Origin = NewOrigin;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeAngles(const FVector& NewAngles)
{
	Angles = NewAngles;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeTransform(const FVector& NewOrigin, const FVector& NewAngles)
{
	Origin = NewOrigin;
	Angles = NewAngles;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeModel(const FString& NewModel)
{
	Model = NewModel;
	OnRuntimeModelChanged();
}

void FElysiumEntity::OnRuntimeTransformChanged()
{
	if (GenericStaticModelBody) GenericStaticModelBody->SetWorldLocationAndRotation(
		Origin, FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
	// A brush body is cooked static at build (BuildBrushBody never sets it Movable), and scripts only
	// SetOrigin/SetAngles point entities (props/items/NPCs) in practice — so the base does not move the
	// body. The authoritative Origin/Angles fields are already updated; a leaf with a movable body
	// overrides this to follow. Tell a retained visualizer the transform changed either way.
	if (GenericModelBody)
	{
		GenericModelBody->SetWorldLocationAndRotation(
			Origin, FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
	}
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::OnRuntimeModelChanged()
{
	if (GenericStaticModelBody)
	{
		GenericStaticModelBody->DestroyComponent();
		GenericStaticModelBody = nullptr;
	}
	if (GenericModelBody)
	{
		GenericModelBody->DestroyComponent();
		GenericModelBody = nullptr;
	}
	EnsurePlacedModelBody();
}

void FElysiumEntity::FireOutput(FName Output, const FElysiumEntityHandle& Activator)
{
	if (World)
	{
		World->FireOutput(*this, Output, Activator, FElysiumVariant::Void());
	}
}

void FElysiumEntity::FireOutput(FName Output, const FElysiumEntityHandle& Activator, const FElysiumVariant& Value)
{
	if (World)
	{
		World->FireOutput(*this, Output, Activator, Value);
	}
}

FString FElysiumEntity::DebugString() const
{
	const FString Name = TargetName.IsEmpty() ? TEXT("<noname>") : TargetName;
	const FString Cls = Def ? Def->Classname : (Class ? Class->ClassName.ToString() : TEXT("<?>"));
	return FString::Printf(TEXT("#%d %s(%s)"), Handle.Index, *Name, *Cls);
}
