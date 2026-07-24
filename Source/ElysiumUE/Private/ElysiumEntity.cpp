#include "ElysiumEntity.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

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

void FElysiumEntity::OnDormancyChanged()
{
	// R6 — one reversible switch. Inert (hidden or dead) drops the body's collision so it cannot
	// be touched or traced; active restores its built solidity. Idempotent (SetDormant re-applies).
	if (Body)
	{
		Body->SetDormant(IsInert());
	}
	// P2.4 — the visual (colour/visibility) changed; let a retained gizmo layer dirty this one
	// instance on the event rather than polling every entity every frame. No-op in normal play.
	if (World)
	{
		World->NotifyVisualChanged(*this);
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
