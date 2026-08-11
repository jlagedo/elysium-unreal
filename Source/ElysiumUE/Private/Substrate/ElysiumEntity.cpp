#include "ElysiumEntity.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumLineService.h"

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

	// Apply the raw keyvalues through the class chain field table (R2). Only mapped base/leaf
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

	// start_hidden — born fully OFF (R6). No prior think to save; the body build (P1.5) skips
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
	// not touch, trace, or think). The slot removal + handle invalidation is the world's job
	// in P1.4; this only flips the entity's own state.
	if (bDead)
	{
		return;
	}
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
		OnDormancyChanged();   // drop the body's collision + draw (no-op until P1.5); notifies below
	}
	else if (World)
	{
		// Already hidden (OnDormancyChanged is skipped), but the visual state still changed
		// hidden -> dead, so a retained visualizer must still be told.
		World->NotifyVisualChanged(*this);
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
	return Body;   // a brush entity's body; null for point/logic ents (a physics prop overrides this)
}

void FElysiumEntity::PostSpawn()
{
	MoveParent = FElysiumEntityHandle::Invalid();
	if (ParentName.IsEmpty() || !World)
	{
		return;
	}
	FElysiumEntity* Parent = World->FindByName(ParentName);
	UPrimitiveComponent* ChildBody = GetAttachBody();
	UPrimitiveComponent* ParentBody = Parent ? Parent->GetAttachBody() : nullptr;
	if (!Parent || Parent == this || !ChildBody || !ParentBody)
	{
		UE_LOG(LogElysiumEntityBase, Warning, TEXT("%s cannot attach to parent '%s'"),
			*DebugString(), *ParentName);
		return;
	}
	const FTransform ParentWorld = ParentBody->GetComponentTransform();
	if (!ChildBody->AttachToComponent(ParentBody, FAttachmentTransformRules::KeepWorldTransform))
	{
		UE_LOG(LogElysiumEntityBase, Warning, TEXT("%s failed to attach to parent '%s'"),
			*DebugString(), *ParentName);
		return;
	}
	MoveParent = Parent->Handle;
	OnParentAttached(ParentWorld);
}

void FElysiumEntity::OnDormancyChanged()
{
	// R6 — one reversible switch. Inert (hidden or dead) drops the body's collision so it cannot
	// be touched or traced; active restores its built solidity. Idempotent (SetDormant re-applies).
	RefreshBrushBodyState();
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
	// P2.4 — the visual (colour/visibility) changed; let a retained gizmo layer dirty this one
	// instance on the event rather than polling every entity every frame. No-op in normal play.
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::RefreshBrushBodyState()
{
	if (Body)
	{
		Body->SetDormant(!IsBrushBodyEnabled());
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
	// A brush body is cooked static at build (BuildBrushBody never sets it Movable), and scripts only
	// SetOrigin/SetAngles point entities (props/items/NPCs) in practice — so the base does not move the
	// body. The authoritative Origin/Angles fields are already updated; a leaf with a movable body
	// overrides this to follow. Tell a retained visualizer the transform changed either way.
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
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
