#include "ElysiumEntityDebugSubsystem.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEventQueue.h"
#include "Debug/ElysiumGizmoColor.h"
#include "Debug/ElysiumGizmoLayer.h"
#include "ElysiumIOSink.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumVariant.h"

#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SceneTypes.h"                 // ESceneDepthPriorityGroup (gizmo occlusion vs x-ray)
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEnt, Log, All);

namespace
{
	const TCHAR* VariantTypeName(EElysiumVariantType T)
	{
		switch (T)
		{
		case EElysiumVariantType::Void:   return TEXT("Void");
		case EElysiumVariantType::Bool:   return TEXT("Bool");
		case EElysiumVariantType::Int:    return TEXT("Int");
		case EElysiumVariantType::Float:  return TEXT("Float");
		case EElysiumVariantType::String: return TEXT("String");
		case EElysiumVariantType::Vector: return TEXT("Vector");
		case EElysiumVariantType::Handle: return TEXT("Handle");
		default:                          return TEXT("?");
		}
	}

	// Chain-walk a class's own + inherited input names (derived shadows base), sorted for a stable
	// listing. Mirrors the Cog inspector's CollectInputs.
	void CollectChainInputs(const FElysiumClassDesc& Leaf, const FElysiumClassRegistry& Reg, TArray<FName>& Out)
	{
		TSet<FName> Seen;
		for (const FElysiumClassDesc* D = &Leaf; D; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			for (const TPair<FName, FElysiumInputThunk>& I : D->Inputs)
			{
				Seen.Add(I.Key);
			}
		}
		Out = Seen.Array();
		Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	}

	void CollectChainFields(const FElysiumClassDesc& Leaf, const FElysiumClassRegistry& Reg, TArray<FName>& Out)
	{
		TSet<FName> Seen;
		for (const FElysiumClassDesc* D = &Leaf; D; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			for (const TPair<FName, FElysiumFieldAccessor>& F : D->Fields)
			{
				Seen.Add(F.Key);
			}
		}
		Out = Seen.Array();
		Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	}

	// The world-space anchor + half-extent for an entity's overlays: the body's cooked bounds for a
	// brush entity, else a small box at the def origin for a point/logic entity.
	void EntityBounds(const FElysiumEntity& Ent, FVector& OutCenter, FVector& OutExtent)
	{
		if (Ent.Body)
		{
			const FBoxSphereBounds B = Ent.Body->Bounds;
			OutCenter = B.Origin;
			OutExtent = B.BoxExtent;
		}
		else
		{
			OutCenter = Ent.Def ? Ent.Def->Origin : FVector::ZeroVector;
			OutExtent = FVector(16.f);
		}
	}

	FColor StateColor(const FElysiumEntity& Ent)
	{
		if (Ent.IsDead())   { return FColor(160, 60, 60); }
		if (Ent.IsHidden()) { return FColor(200, 170, 60); }
		return FColor(80, 200, 120);
	}

	// The gizmo marker / beam endpoint / label anchor is ElysiumGizmoAnchor (ElysiumGizmoColor.h),
	// shared with the retained ISM layer and the click-pick.
	using ::ElysiumGizmoAnchor;

	// Coarse classname -> gizmo color lives in ElysiumGizmoColor.h (shared with the retained ISM
	// layer); ElysiumGizmoClassColor() is used below for the trigger hulls and gizmo labels.

	// Dim a color toward black (hidden/dormant entities read as inactive without changing the hue).
	FColor Dimmed(const FColor& C, float K)
	{
		return FColor(uint8(C.R * K), uint8(C.G * K), uint8(C.B * K), C.A);
	}
}

// The chokepoint tap the subsystem installs into each entity world. It is owned by the world (torn
// down with it), holds a weak back-pointer to the subsystem (which outlives the world), and forwards
// the two chokepoint notifications the ent_* overlay/break tooling needs.
class FElysiumDebugTapSink final : public IElysiumIOSink
{
public:
	FElysiumDebugTapSink(FElysiumEntityWorld& InWorld, UElysiumEntityDebugSubsystem* InSub)
		: World(InWorld), Sub(InSub) {}

	virtual void OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) override
	{
		if (UElysiumEntityDebugSubsystem* S = Sub.Get())
		{
			S->TapDelivered(World, Now, Target, Event);
		}
	}
	virtual void OnOutputFired(double Now, const FElysiumEntity& Source, const FElysiumOutputDef& Output) override
	{
		if (UElysiumEntityDebugSubsystem* S = Sub.Get())
		{
			S->TapOutput(World, Now, Source, Output);
		}
	}

private:
	FElysiumEntityWorld& World;
	TWeakObjectPtr<UElysiumEntityDebugSubsystem> Sub;
};

// ============================================================================================
// Subsystem lifecycle
// ============================================================================================

void UElysiumEntityDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if !UE_BUILD_SHIPPING
	GizmoLayer = MakePimpl<FElysiumGizmoLayer>();

	IConsoleManager& CM = IConsoleManager::Get();

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_fire"),
		TEXT("elysium.ent_fire [target|!picker] [Input] [param] [delay] — inject an input through the real event queue. "
		     "No target = entity under crosshair; no Input = list the target's inputs. Target matches targetname or classname."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleFire(Args, W); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_dump"),
		TEXT("elysium.ent_dump [target|!picker] — dump one entity's live state, fields, keyvalues, and outputs."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleDump(Args, W); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_info"),
		TEXT("elysium.ent_info <classname> — dump a class's chain-resolved inputs and fields (schema)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld*) { HandleInfo(Args); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_pause"),
		TEXT("elysium.ent_pause — toggle the event queue's pause (freezes I/O delivery and overlay fade)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&, UWorld* W) { HandlePause(W); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_step"),
		TEXT("elysium.ent_step [n] — pause the queue if running, then release n queued events (default 1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleStep(Args, W); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_break"),
		TEXT("elysium.ent_break [target|!picker] [Input] — pause the queue when a matching input is delivered. No args = clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleBreak(Args, W); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_text"),
		TEXT("elysium.ent_text [target|!picker|off] — toggle the overhead identity/state text overlay on matching entities."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleOverlay(Args, W, Overlay_Text, TEXT("text")); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_bbox"),
		TEXT("elysium.ent_bbox [target|!picker|off] — toggle the collision-bounds box overlay on matching entities."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleOverlay(Args, W, Overlay_BBox, TEXT("bbox")); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_messages"),
		TEXT("elysium.ent_messages [target|!picker|off] — toggle the fading I/O message overlay on matching entities."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld* W) { HandleOverlay(Args, W, Overlay_Messages, TEXT("messages")); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_clear"),
		TEXT("elysium.ent_clear — clear every ent_text / ent_bbox / ent_messages overlay."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&, UWorld*) { HandleClear(); }),
		ECVF_Cheat));

	// --- P2.4 world-viz verbs (thin echoes of the World Viz Cog window; they flip VizSettings) -------
	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.showtriggers"),
		TEXT("elysium.showtriggers [0|1] [state] — toggle wireframe trigger-body hulls (no arg = toggle). "
		     "'state' colors by enabled/dormant instead of by class."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld*) { HandleShowTriggers(Args); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_gizmos"),
		TEXT("elysium.ent_gizmos [off|visible|all] — entity origin gizmos: off, visible (walls occlude), "
		     "or all (x-ray). No arg cycles off->visible->all->off."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld*) { HandleGizmos(Args); }),
		ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(TEXT("elysium.ent_beams"),
		TEXT("elysium.ent_beams [0|1] — toggle fading caller->target arrows on each I/O delivery (no arg = toggle)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args, UWorld*) { HandleBeams(Args); }),
		ECVF_Cheat));
#endif // !UE_BUILD_SHIPPING
}

void UElysiumEntityDebugSubsystem::Deinitialize()
{
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();
	Super::Deinitialize();
}

bool UElysiumEntityDebugSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UElysiumEntityDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UElysiumEntityDebugSubsystem, STATGROUP_Tickables);
}

void UElysiumEntityDebugSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if !UE_BUILD_SHIPPING
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		// No live world (between maps): drop stale, per-epoch state so nothing lingers.
		if (HookedEpoch != 0)
		{
			ResetState();
			if (GizmoLayer) { GizmoLayer->Teardown(); }
			HookedEpoch = 0;
		}
		return;
	}

	// A fresh world (first load or a reload/travel) gets a fresh tap and a clean slate — the old
	// world's sink and our old handle-scoped state both died with it.
	if (EW->GetEpoch() != HookedEpoch)
	{
		ResetState();
		if (GizmoLayer) { GizmoLayer->Teardown(); }   // the ISM died with the old map actor
		EW->AddSink(MakeUnique<FElysiumDebugTapSink>(*EW, this));
		HookedEpoch = EW->GetEpoch();
	}

	// Drive the retained gizmo ISM layer: build it lazily the first time gizmos are switched on for
	// this epoch, then just apply the mode (a no-op when unchanged — no per-frame draw cost).
	if (GizmoLayer)
	{
		if (VizSettings.GizmoMode != EGizmoMode::Off && !GizmoLayer->IsBuilt())
		{
			GizmoLayer->Rebuild(*EW, EW->GetOwnerActor());
		}
		if (GizmoLayer->IsBuilt())
		{
			GizmoLayer->SetMode(VizSettings.GizmoMode);
			GizmoLayer->SetClassMask(VizSettings.GizmoClassMask);
		}
	}

	// The fade clock stops while the queue is paused, so ent_break freezes the message evidence.
	if (!EW->Queue().IsPaused())
	{
		FadeClock += DeltaTime;
	}

	RenderOverlays(*EW);
	RenderWorldViz(*EW);
#endif // !UE_BUILD_SHIPPING
}

// ============================================================================================
// Helpers
// ============================================================================================

FElysiumEntityWorld* UElysiumEntityDebugSubsystem::GetSubstrate() const
{
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	return Map ? Map->GetEntityWorld() : nullptr;
}

void UElysiumEntityDebugSubsystem::ResetState()
{
	OverlayBits.Empty();
	Messages.Empty();
	Beams.Empty();
	FadeClock = 0.0;
	bBreakArmed = false;
	BreakTarget.Empty();
	BreakHandle = FElysiumEntityHandle::Invalid();
	BreakInput = NAME_None;
	// VizSettings (the toggles themselves) survive a map reload — a dev leaves gizmos on across
	// travels — so it is deliberately not reset here; only the per-epoch beam captures are dropped.
}

FElysiumEntityHandle UElysiumEntityDebugSubsystem::PickUnderCrosshair(UWorld* World, FElysiumEntityWorld& EW) const
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return FElysiumEntityHandle::Invalid();
	}

	FVector Loc; FRotator Rot;
	PC->GetPlayerViewPoint(Loc, Rot);
	const FVector Dir = Rot.Vector();
	constexpr double TraceDist = 100000.0;
	const FVector End = Loc + Dir * TraceDist;

	// 1) Brush bodies: a multi-trace returns trigger overlaps and the first solid block in near->far
	// order, so the nearest UElysiumBrushComponent along the aim wins — solids and triggers alike,
	// and never through a wall (a world-collision block ends the trace).
	FCollisionQueryParams Params(FName(TEXT("ElysiumEntPicker")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(PC->GetPawn());
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Loc, End, ECC_Visibility, Params);
	for (const FHitResult& H : Hits)
	{
		if (const UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent()))
		{
			return B->GetOwningEntity();
		}
	}

	// 2) Bodiless logic entities: the one whose origin is nearest the aim ray (Source's picker gives
	// geometry-less ents an origin proxy). Capped so an off-screen ent is never silently selected.
	constexpr double MaxPerp = 200.0;
	double BestPerp = MaxPerp;
	FElysiumEntityHandle Best = FElysiumEntityHandle::Invalid();
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EW.Entities())
	{
		const FElysiumEntity* E = EntPtr.Get();
		if (!E || E->IsDead() || E->Body || !E->Def)
		{
			continue;   // brush ents are handled by the trace; skip dead
		}
		const FVector V = E->Def->Origin - Loc;
		const double T = FVector::DotProduct(V, Dir);
		if (T < 0.0 || T > TraceDist)
		{
			continue;   // behind the camera or past the trace
		}
		const double Perp = FVector::Dist(E->Def->Origin, Loc + Dir * T);
		if (Perp < BestPerp)
		{
			BestPerp = Perp;
			Best = E->Handle;
		}
	}
	return Best;
}

void UElysiumEntityDebugSubsystem::ResolveTargets(FElysiumEntityWorld& EW, UWorld* World,
	const FString& Target, TArray<FElysiumEntityHandle>& Out) const
{
	if (Target.IsEmpty() || Target.Equals(TEXT("!picker"), ESearchCase::IgnoreCase))
	{
		const FElysiumEntityHandle H = PickUnderCrosshair(World, EW);
		if (H.IsSet())
		{
			Out.Add(H);
		}
		return;
	}

	// Every live entity whose targetname or classname matches (case-folded), so a classname arg
	// fans out over the class — the console analogue of Source's targetname-else-classname resolve.
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EW.Entities())
	{
		const FElysiumEntity* E = EntPtr.Get();
		if (!E || E->IsDead() || !E->Def)
		{
			continue;
		}
		if (E->TargetName.Equals(Target, ESearchCase::IgnoreCase) ||
			E->Def->Classname.Equals(Target, ESearchCase::IgnoreCase))
		{
			Out.Add(E->Handle);
		}
	}
}

// ============================================================================================
// UI-facing controls (shared by the Cog Inspector and the ent_* verbs)
// ============================================================================================

FElysiumEntityHandle UElysiumEntityDebugSubsystem::PickSelection()
{
	FElysiumEntityWorld* EW = GetSubstrate();
	return EW ? PickUnderCrosshair(GetWorld(), *EW) : FElysiumEntityHandle::Invalid();
}

bool UElysiumEntityDebugSubsystem::IsOverlayOn(int32 EntityIndex, uint8 Bit) const
{
	return (OverlayBits.FindRef(EntityIndex) & Bit) != 0;
}

void UElysiumEntityDebugSubsystem::SetOverlay(int32 EntityIndex, uint8 Bit, bool bOn)
{
	uint8& Bits = OverlayBits.FindOrAdd(EntityIndex);
	if (bOn) { Bits |= Bit; } else { Bits &= ~Bit; }
	if (Bits == 0) { OverlayBits.Remove(EntityIndex); }
}

bool UElysiumEntityDebugSubsystem::IsBreakArmedOn(const FElysiumEntityHandle& Handle) const
{
	return bBreakArmed && BreakHandle.IsSet() && BreakHandle == Handle;
}

void UElysiumEntityDebugSubsystem::ArmBreakOn(const FElysiumEntityHandle& Handle)
{
	bBreakArmed = true;
	BreakHandle = Handle;
	BreakTarget.Empty();
	BreakInput = NAME_None;
}

void UElysiumEntityDebugSubsystem::ClearBreak()
{
	bBreakArmed = false;
	BreakHandle = FElysiumEntityHandle::Invalid();
	BreakTarget.Empty();
	BreakInput = NAME_None;
}

// ============================================================================================
// Verbs
// ============================================================================================

void UElysiumEntityDebugSubsystem::HandleFire(const TArray<FString>& Args, UWorld* World)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_fire: no live entity world (load a map first)"));
		return;
	}

	const FString Target = Args.Num() >= 1 ? Args[0] : FString();
	TArray<FElysiumEntityHandle> Handles;
	ResolveTargets(*EW, World, Target, Handles);
	if (Handles.Num() == 0)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_fire: no live entity matches '%s'"),
			Target.IsEmpty() ? TEXT("(crosshair)") : *Target);
		return;
	}

	// No input given: list the resolved target's chain-resolved inputs (discovery, no fire). Entities
	// of one class share an input table, so one listing (off the first match) covers the whole fan-out.
	if (Args.Num() < 2)
	{
		const FElysiumEntity* E = EW->Resolve(Handles[0]);
		if (E && E->Class)
		{
			TArray<FName> Inputs;
			CollectChainInputs(*E->Class, FElysiumClassRegistry::Get(), Inputs);
			FString List;
			for (const FName& N : Inputs) { List += (List.IsEmpty() ? TEXT("") : TEXT(", ")); List += N.ToString(); }
			UE_LOG(LogElysiumEnt, Display, TEXT("ent_fire: %s inputs: %s%s"),
				*E->DebugString(), List.IsEmpty() ? TEXT("(none)") : *List,
				Handles.Num() > 1 ? *FString::Printf(TEXT("  (%d entities matched)"), Handles.Num()) : TEXT(""));
		}
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_fire: re-run with an input to fire it."));
		return;
	}

	const FName Input(*Args[1]);
	const FString Param = Args.Num() >= 3 ? Args[2] : FString();
	const double Delay = Args.Num() >= 4 ? FCString::Atod(*Args[3]) : 0.0;

	// Fire through the real queue (chokepoint 2), one delivery per resolved entity via "!self" +
	// Caller = its handle, so it reaches exactly that record regardless of shared targetnames.
	for (const FElysiumEntityHandle& H : Handles)
	{
		EW->EnqueueInput(TEXT("!self"), Input, FElysiumVariant::String(Param), Delay,
			FElysiumEntityHandle::Invalid(), H);
	}
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_fire: queued %s(%s) on %d entit%s (+%.2fs)"),
		*Input.ToString(), *Param, Handles.Num(), Handles.Num() == 1 ? TEXT("y") : TEXT("ies"), Delay);
}

void UElysiumEntityDebugSubsystem::HandleDump(const TArray<FString>& Args, UWorld* World)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_dump: no live entity world (load a map first)"));
		return;
	}

	const FString Target = Args.Num() >= 1 ? Args[0] : FString();
	TArray<FElysiumEntityHandle> Handles;
	ResolveTargets(*EW, World, Target, Handles);
	if (Handles.Num() == 0)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_dump: no live entity matches '%s'"),
			Target.IsEmpty() ? TEXT("(crosshair)") : *Target);
		return;
	}

	const FElysiumEntity* E = EW->Resolve(Handles[0]);
	if (!E || !E->Def)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_dump: entity is dead or stale"));
		return;
	}
	if (Handles.Num() > 1)
	{
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_dump: '%s' matched %d entities; dumping the first"),
			*Target, Handles.Num());
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	UE_LOG(LogElysiumEnt, Display, TEXT("== ent_dump %s =="), *E->DebugString());
	// The LIVE origin, not the def's. An entity a `scripted_sequence` placed on its mark, or a script
	// moved with SetOrigin, is somewhere else entirely — printing the def would report the spawn point
	// and quietly contradict where the body is standing. The def's is shown alongside once it differs.
	const bool bMoved = !E->Origin.Equals(E->Def->Origin, 0.01);
	UE_LOG(LogElysiumEnt, Display, TEXT("  state: %s | body: %s | next-think: %s | origin: %s%s"),
		E->IsDead() ? TEXT("dead") : E->IsHidden() ? TEXT("hidden") : TEXT("live"),
		E->Body ? TEXT("brush") : TEXT("(none)"),
		E->NextThink == ELYSIUM_NEVER_THINK ? TEXT("never") : *FString::Printf(TEXT("%.2fs"), E->NextThink),
		*E->Origin.ToString(),
		bMoved ? *FString::Printf(TEXT(" (spawned at %s)"), *E->Def->Origin.ToString()) : TEXT(""));

	if (E->Class)
	{
		TArray<FName> Fields;
		CollectChainFields(*E->Class, Reg, Fields);
		UE_LOG(LogElysiumEnt, Display, TEXT("  fields (%d):"), Fields.Num());
		for (const FName& N : Fields)
		{
			if (const FElysiumFieldAccessor* Acc = Reg.FindField(*E->Class, N))
			{
				UE_LOG(LogElysiumEnt, Display, TEXT("    %-16s = %s"), *N.ToString(), *Acc->Get(*E).ToString());
			}
		}
	}

	TArray<FString> Keys;
	E->Def->Keys.GetKeys(Keys);
	Keys.Sort();
	UE_LOG(LogElysiumEnt, Display, TEXT("  keyvalues (%d):"), Keys.Num());
	for (const FString& K : Keys)
	{
		UE_LOG(LogElysiumEnt, Display, TEXT("    %-16s = %s"), *K, *E->Def->Keys[K]);
	}

	const TArray<FElysiumOutputDef>& Outputs = E->Def->Outputs;
	UE_LOG(LogElysiumEnt, Display, TEXT("  outputs (%d):"), Outputs.Num());
	for (int32 i = 0; i < Outputs.Num(); ++i)
	{
		const FElysiumOutputDef& O = Outputs[i];
		const int32 Remaining = E->OutputTimesRemaining.IsValidIndex(i) ? E->OutputTimesRemaining[i] : O.Times;
		UE_LOG(LogElysiumEnt, Display, TEXT("    %s -> %s.%s(%s) delay %.2f times %s%s"),
			*O.Name, O.Target.IsEmpty() ? TEXT("(python)") : *O.Target, *O.Input, *O.Param, O.Delay,
			O.Times < 0 ? TEXT("inf") : *FString::Printf(TEXT("%d/%d"), Remaining, O.Times),
			O.Python.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [py: %s]"), *O.Python));
	}
}

void UElysiumEntityDebugSubsystem::HandleInfo(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("usage: elysium.ent_info <classname>"));
		return;
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Desc = Reg.Find(FName(*Args[0]));
	if (!Desc)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_info: '%s' is not a registered class (would spawn as an inert record)"), *Args[0]);
		return;
	}

	// The base chain, leaf first.
	FString Chain;
	for (const FElysiumClassDesc* D = Desc; D; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
	{
		Chain += (Chain.IsEmpty() ? TEXT("") : TEXT(" -> "));
		Chain += D->ClassName.ToString();
	}

	TArray<FName> Inputs;
	CollectChainInputs(*Desc, Reg, Inputs);
	TArray<FName> Fields;
	CollectChainFields(*Desc, Reg, Fields);

	UE_LOG(LogElysiumEnt, Display, TEXT("== ent_info %s =="), *Args[0]);
	UE_LOG(LogElysiumEnt, Display, TEXT("  chain: %s"), *Chain);

	FString InputList;
	for (const FName& N : Inputs) { InputList += (InputList.IsEmpty() ? TEXT("") : TEXT(", ")); InputList += N.ToString(); }
	UE_LOG(LogElysiumEnt, Display, TEXT("  inputs (%d): %s"), Inputs.Num(), InputList.IsEmpty() ? TEXT("(none)") : *InputList);

	UE_LOG(LogElysiumEnt, Display, TEXT("  fields (%d):"), Fields.Num());
	for (const FName& N : Fields)
	{
		if (const FElysiumFieldAccessor* Acc = Reg.FindField(*Desc, N))
		{
			UE_LOG(LogElysiumEnt, Display, TEXT("    %-16s %s%s"), *N.ToString(),
				VariantTypeName(Acc->Type), Acc->bKeyable ? TEXT(" (keyable)") : TEXT(""));
		}
	}
	UE_LOG(LogElysiumEnt, Display, TEXT("  note: outputs are per-entity .ents data, not a class schema — use ent_dump <name>."));
}

void UElysiumEntityDebugSubsystem::HandlePause(UWorld* /*World*/)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_pause: no live entity world"));
		return;
	}
	FElysiumEventQueue& Q = EW->Queue();
	if (Q.IsPaused())
	{
		Q.Resume();
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_pause: queue resumed"));
	}
	else
	{
		Q.Pause();
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_pause: queue paused (%d pending)"), Q.Num());
	}
}

void UElysiumEntityDebugSubsystem::HandleStep(const TArray<FString>& Args, UWorld* /*World*/)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_step: no live entity world"));
		return;
	}
	FElysiumEventQueue& Q = EW->Queue();
	if (!Q.IsPaused())
	{
		Q.Pause();   // stepping only makes sense while paused; arm the pause first
	}
	const int32 N = Args.Num() >= 1 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 1;
	Q.RequestSteps(N);
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_step: stepping %d event(s) (%d pending, %d armed; still paused)"),
		N, Q.Num(), Q.StepsPending());
}

void UElysiumEntityDebugSubsystem::HandleBreak(const TArray<FString>& Args, UWorld* World)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_break: no live entity world"));
		return;
	}

	// No args: clear an armed breakpoint, else arm on the entity under the crosshair (any input).
	if (Args.Num() == 0)
	{
		if (bBreakArmed)
		{
			bBreakArmed = false;
			BreakTarget.Empty();
			BreakHandle = FElysiumEntityHandle::Invalid();
			BreakInput = NAME_None;
			UE_LOG(LogElysiumEnt, Display, TEXT("ent_break: cleared"));
			return;
		}
		const FElysiumEntityHandle H = PickUnderCrosshair(World, *EW);
		const FElysiumEntity* E = EW->Resolve(H);
		if (!E)
		{
			UE_LOG(LogElysiumEnt, Warning, TEXT("ent_break: nothing under the crosshair to break on"));
			return;
		}
		bBreakArmed = true;
		BreakHandle = H;
		BreakTarget.Empty();
		BreakInput = NAME_None;
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_break: armed on %s (any input)"), *E->DebugString());
		return;
	}

	bBreakArmed = true;
	BreakHandle = FElysiumEntityHandle::Invalid();
	BreakTarget = Args[0];
	BreakInput = Args.Num() >= 2 ? FName(*Args[1]) : NAME_None;
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_break: armed on '%s' input '%s'"),
		*BreakTarget, BreakInput.IsNone() ? TEXT("(any)") : *BreakInput.ToString());
}

void UElysiumEntityDebugSubsystem::HandleOverlay(const TArray<FString>& Args, UWorld* World, uint8 Bit, const TCHAR* Name)
{
	FElysiumEntityWorld* EW = GetSubstrate();
	if (!EW)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_%s: no live entity world"), Name);
		return;
	}

	// "off"/"clear"/"none": drop this overlay bit from every entity.
	if (Args.Num() >= 1 && (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase) ||
		Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("none"), ESearchCase::IgnoreCase)))
	{
		for (auto It = OverlayBits.CreateIterator(); It; ++It)
		{
			It.Value() &= ~Bit;
			if (It.Value() == 0) { It.RemoveCurrent(); }
		}
		UE_LOG(LogElysiumEnt, Display, TEXT("ent_%s: cleared"), Name);
		return;
	}

	const FString Target = Args.Num() >= 1 ? Args[0] : FString();
	TArray<FElysiumEntityHandle> Handles;
	ResolveTargets(*EW, World, Target, Handles);
	if (Handles.Num() == 0)
	{
		UE_LOG(LogElysiumEnt, Warning, TEXT("ent_%s: no live entity matches '%s'"),
			Name, Target.IsEmpty() ? TEXT("(crosshair)") : *Target);
		return;
	}

	int32 On = 0, Off = 0;
	for (const FElysiumEntityHandle& H : Handles)
	{
		uint8& Bits = OverlayBits.FindOrAdd(H.Index);
		Bits ^= Bit;   // toggle per entity
		if (Bits & Bit) { ++On; } else { ++Off; }
		if (Bits == 0) { OverlayBits.Remove(H.Index); }
	}
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_%s: %d on, %d off"), Name, On, Off);
}

void UElysiumEntityDebugSubsystem::HandleClear()
{
	OverlayBits.Empty();
	Messages.Empty();
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_clear: all overlays cleared"));
}

// --- P2.4 world-viz verbs — the scriptable echo of the World Viz Cog window's controls -------------

void UElysiumEntityDebugSubsystem::HandleShowTriggers(const TArray<FString>& Args)
{
	// Optional first arg: explicit 0/1 (else toggle). Optional "state" (any position): color by state.
	bool bWantState = false;
	TOptional<bool> Explicit;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("state"), ESearchCase::IgnoreCase) || A.Equals(TEXT("bystate"), ESearchCase::IgnoreCase))
		{
			bWantState = true;
		}
		else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase))  { Explicit = true; }
		else if (A == TEXT("0") || A.Equals(TEXT("off"), ESearchCase::IgnoreCase)) { Explicit = false; }
	}
	VizSettings.bShowTriggers = Explicit.IsSet() ? Explicit.GetValue() : !VizSettings.bShowTriggers;
	if (bWantState) { VizSettings.bTriggerColorByState = true; }
	UE_LOG(LogElysiumEnt, Display, TEXT("showtriggers: %s (color by %s)"),
		VizSettings.bShowTriggers ? TEXT("on") : TEXT("off"),
		VizSettings.bTriggerColorByState ? TEXT("state") : TEXT("class"));
}

void UElysiumEntityDebugSubsystem::HandleGizmos(const TArray<FString>& Args)
{
	if (Args.Num() >= 1)
	{
		const FString& M = Args[0];
		if (M.Equals(TEXT("off"), ESearchCase::IgnoreCase))          { VizSettings.GizmoMode = EGizmoMode::Off; }
		else if (M.Equals(TEXT("visible"), ESearchCase::IgnoreCase)) { VizSettings.GizmoMode = EGizmoMode::Visible; }
		else if (M.Equals(TEXT("all"), ESearchCase::IgnoreCase))     { VizSettings.GizmoMode = EGizmoMode::All; }
		else
		{
			UE_LOG(LogElysiumEnt, Warning, TEXT("ent_gizmos: expected off|visible|all"));
			return;
		}
	}
	else
	{
		// Cycle off -> visible -> all -> off.
		switch (VizSettings.GizmoMode)
		{
		case EGizmoMode::Off:     VizSettings.GizmoMode = EGizmoMode::Visible; break;
		case EGizmoMode::Visible: VizSettings.GizmoMode = EGizmoMode::All;     break;
		default:                  VizSettings.GizmoMode = EGizmoMode::Off;      break;
		}
	}
	const TCHAR* Name = VizSettings.GizmoMode == EGizmoMode::Off ? TEXT("off")
		: VizSettings.GizmoMode == EGizmoMode::Visible ? TEXT("visible") : TEXT("all");
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_gizmos: %s"), Name);
}

void UElysiumEntityDebugSubsystem::HandleBeams(const TArray<FString>& Args)
{
	if (Args.Num() >= 1)
	{
		VizSettings.bShowBeams = (Args[0] == TEXT("1") || Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase));
	}
	else
	{
		VizSettings.bShowBeams = !VizSettings.bShowBeams;
	}
	UE_LOG(LogElysiumEnt, Display, TEXT("ent_beams: %s"), VizSettings.bShowBeams ? TEXT("on") : TEXT("off"));
}

// ============================================================================================
// Chokepoint tap (ent_break + ent_messages capture)
// ============================================================================================

void UElysiumEntityDebugSubsystem::TapDelivered(FElysiumEntityWorld& World, double /*Now*/,
	const FElysiumEntity& Target, const FElysiumIOEvent& Event)
{
	// ent_break: pause the queue the moment a matching input lands. The current event is already
	// delivered; the service loop checks IsPaused() before the next event, so it halts there.
	if (bBreakArmed && (BreakInput.IsNone() || BreakInput == Event.Input))
	{
		const bool bMatch = BreakHandle.IsSet()
			? (Target.Handle == BreakHandle)
			: (Target.TargetName.Equals(BreakTarget, ESearchCase::IgnoreCase) ||
			   (Target.Def && Target.Def->Classname.Equals(BreakTarget, ESearchCase::IgnoreCase)));
		if (bMatch && !World.Queue().IsPaused())
		{
			World.Queue().Pause();
			UE_LOG(LogElysiumEnt, Display, TEXT("ent_break: hit %s.%s — queue paused"),
				*Target.DebugString(), *Event.Input.ToString());
		}
	}

#if ENABLE_DRAW_DEBUG
	// ent_messages: record the delivered input against the receiving entity for its overlay.
	if (OverlayBits.FindRef(Target.Handle.Index) & Overlay_Messages)
	{
		FOverlayMessage M;
		M.Time = FadeClock;
		M.EntityIndex = Target.Handle.Index;
		M.Text = FString::Printf(TEXT("< %s(%s)  from %s"),
			*Event.Input.ToString(), *Event.Param.ToString(), *World.DescribeHandle(Event.Caller));
		Messages.Add(MoveTemp(M));
	}

	// ent_beams: a fading arrow for this delivery. Normally caller->target (the firing entity to the
	// receiver). When the caller is the receiver itself (self-delivery — every hand-fire from ent_fire
	// / the Inspector is this) or unresolvable, fall back to an arrow from the camera to the target, so
	// a hand-fired test is still visible even on a map with no entity->entity wiring.
	if (VizSettings.bShowBeams)
	{
		const FVector To = ElysiumGizmoAnchor(Target);
		const FElysiumEntity* Caller = World.Resolve(Event.Caller);
		FVector From;
		bool bHaveFrom = false;
		if (Caller && !ElysiumGizmoAnchor(*Caller).Equals(To, 1.0))
		{
			From = ElysiumGizmoAnchor(*Caller);   // real entity -> entity output
			bHaveFrom = true;
		}
		else if (UWorld* W = GetWorld())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				FRotator ViewRot;
				PC->GetPlayerViewPoint(From, ViewRot);   // self / hand-fire: camera -> target
				bHaveFrom = true;
			}
		}
		if (bHaveFrom && !From.Equals(To, 1.0))
		{
			FBeam B;
			B.From = From;
			B.To   = To;
			B.Time = FadeClock;
			Beams.Add(B);
			// Bound the ring so a busy map can't grow it without limit between prunes.
			constexpr int32 MaxBeams = 256;
			if (Beams.Num() > MaxBeams)
			{
				Beams.RemoveAt(0, Beams.Num() - MaxBeams, EAllowShrinking::No);
			}
		}
	}
#endif
}

void UElysiumEntityDebugSubsystem::TapOutput(FElysiumEntityWorld& /*World*/, double /*Now*/,
	const FElysiumEntity& Source, const FElysiumOutputDef& Output)
{
#if ENABLE_DRAW_DEBUG
	// ent_messages: record the fired output against the firing entity for its overlay.
	if (OverlayBits.FindRef(Source.Handle.Index) & Overlay_Messages)
	{
		FOverlayMessage M;
		M.Time = FadeClock;
		M.EntityIndex = Source.Handle.Index;
		M.Text = FString::Printf(TEXT("> %s -> %s.%s"), *Output.Name,
			Output.Target.IsEmpty() ? TEXT("(python)") : *Output.Target, *Output.Input);
		Messages.Add(MoveTemp(M));
	}
#endif
}

// ============================================================================================
// Overlay rendering
// ============================================================================================

void UElysiumEntityDebugSubsystem::RenderOverlays(FElysiumEntityWorld& EW)
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World || OverlayBits.Num() == 0)
	{
		// Still prune the message ring so it can't grow unbounded once messages stop mattering.
		constexpr double FadeSeconds = 10.0;
		Messages.RemoveAll([&](const FOverlayMessage& M) { return FadeClock - M.Time > FadeSeconds; });
		return;
	}

	constexpr double FadeSeconds = 10.0;
	constexpr float LineStepZ = 14.0f;     // world-cm vertical gap between stacked text lines
	Messages.RemoveAll([&](const FOverlayMessage& M) { return FadeClock - M.Time > FadeSeconds; });

	const TArray<TUniquePtr<FElysiumEntity>>& Entities = EW.Entities();
	for (const TPair<int32, uint8>& Pair : OverlayBits)
	{
		if (!Entities.IsValidIndex(Pair.Key))
		{
			continue;
		}
		const FElysiumEntity* E = Entities[Pair.Key].Get();
		if (!E || !E->Def)
		{
			continue;
		}
		const uint8 Bits = Pair.Value;
		const FColor Color = StateColor(*E);

		FVector Center, Extent;
		EntityBounds(*E, Center, Extent);
		const FVector Top = Center + FVector(0, 0, Extent.Z + 8.f);

		if (Bits & Overlay_BBox)
		{
			DrawDebugBox(World, Center, Extent, Color, /*bPersistent*/ false, /*Life*/ -1.f, /*Depth*/ 0, /*Thickness*/ 1.5f);
		}

		float TextZ = Top.Z;
		if (Bits & Overlay_Text)
		{
			const FString Head = FString::Printf(TEXT("%s  [%s]"), *E->DebugString(),
				E->IsDead() ? TEXT("dead") : E->IsHidden() ? TEXT("hidden") : TEXT("live"));
			DrawDebugString(World, FVector(Center.X, Center.Y, TextZ), Head, nullptr, Color, 0.f, /*bShadow*/ true);
			TextZ += LineStepZ;
		}

		if (Bits & Overlay_Messages)
		{
			for (const FOverlayMessage& M : Messages)
			{
				if (M.EntityIndex != Pair.Key)
				{
					continue;
				}
				// Fade brightness toward black over the window (freezes while paused via FadeClock).
				const double Age = FMath::Clamp(FadeClock - M.Time, 0.0, FadeSeconds);
				const float K = 1.0f - float(Age / FadeSeconds);
				const FColor Faded(uint8(200 * K + 30), uint8(200 * K + 30), uint8(120 * K + 30));
				DrawDebugString(World, FVector(Center.X, Center.Y, TextZ), M.Text, nullptr, Faded, 0.f, /*bShadow*/ true);
				TextZ += LineStepZ;
			}
		}
	}
#endif // ENABLE_DRAW_DEBUG
}

// ============================================================================================
// P2.4 world visualization — map-wide layers (entity gizmos, trigger hulls, I/O beams)
// ============================================================================================

void UElysiumEntityDebugSubsystem::RenderWorldViz(FElysiumEntityWorld& EW)
{
#if ENABLE_DRAW_DEBUG
	// Beams captured while ent_beams was off should not linger; drop them so re-enabling starts clean.
	if (!VizSettings.bShowBeams)
	{
		Beams.Reset();
	}

	const bool bAnything = VizSettings.GizmoMode != EGizmoMode::Off || VizSettings.bShowTriggers ||
		VizSettings.bShowBeams || Beams.Num() > 0;
	UWorld* World = GetWorld();
	if (!World || !bAnything)
	{
		return;
	}

	// Camera vantage for the distance culls (gizmos would otherwise draw ~1,200 boxes + strings a
	// frame). No camera (headless) → draw everything; the counts are debug-only.
	FVector CamLoc = FVector::ZeroVector;
	bool bHaveCam = false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		bHaveCam = true;
	}

	const TArray<TUniquePtr<FElysiumEntity>>& Entities = EW.Entities();

	// --- Entity gizmo labels ------------------------------------------------------------------------
	// The gizmo *boxes* are the retained ISM layer (FElysiumGizmoLayer) — built once, updated only on
	// entity events, zero per-frame draw cost. Only the labels stay immediate-mode (DrawDebugString
	// has no instanced equivalent), so they are culled hard by distance to keep the string count low.
	if (VizSettings.GizmoMode != EGizmoMode::Off && VizSettings.bGizmoLabels)
	{
		// Labels only within 6 m — you read the name of what you walk up to, and the string count
		// stays tiny (DrawDebugString has no instanced form). Fixed, not tunable.
		const float LabelDistSq = FMath::Square(600.f);
		for (const TUniquePtr<FElysiumEntity>& EntPtr : Entities)
		{
			const FElysiumEntity* E = EntPtr.Get();
			if (!E || E->IsDead() || !E->Def)
			{
				continue;
			}
			if (!ElysiumGizmoClassVisible(E->Def->Classname, VizSettings.GizmoClassMask))
			{
				continue;   // the class filter hides the label with the box it belongs to
			}
			const FVector Anchor = ElysiumGizmoAnchor(*E);
			if (bHaveCam && FVector::DistSquared(Anchor, CamLoc) > LabelDistSq)
			{
				continue;
			}
			FColor Color = ElysiumGizmoClassColor(E->Def->Classname);
			if (E->IsHidden())
			{
				Color = Dimmed(Color, 0.45f);
			}
			const FString Label = E->TargetName.IsEmpty()
				? FString::Printf(TEXT("(%s)"), *E->Def->Classname) : E->TargetName;
			DrawDebugString(World, Anchor + FVector(0, 0, 18.f), Label, nullptr, Color, 0.f, /*bShadow*/ true, /*Scale*/ 1.0f);
		}
	}

	// --- Show triggers: the wireframe convex-hull AABBs of every trigger brush entity ---------------
	// (VtMB triggers are axis-aligned box brushes, so the per-hull AABB is the exact volume.) Colored
	// by class (the gizmo palette) or by enabled/dormant state.
	if (VizSettings.bShowTriggers)
	{
		for (const TUniquePtr<FElysiumEntity>& EntPtr : Entities)
		{
			const FElysiumEntity* E = EntPtr.Get();
			if (!E || E->IsDead() || !E->Def || !E->Def->IsBrush())
			{
				continue;
			}
			const bool bTrigger = (E->Body && E->Body->GetSolidity() == EElysiumBrushSolidity::Trigger)
				|| E->Def->Classname.StartsWith(TEXT("trigger"));
			if (!bTrigger)
			{
				continue;
			}

			const FColor Color = VizSettings.bTriggerColorByState
				? (E->IsInert() ? FColor(150, 60, 60) : FColor(80, 220, 120))
				: ElysiumGizmoClassColor(E->Def->Classname);

			// The body's world transform (handles any placement) or the def origin for a bodiless record.
			const FTransform Xform = E->Body ? E->Body->GetComponentTransform() : FTransform(E->Def->Origin);
			for (const FElysiumConvexHull& Hull : E->Def->Hulls)
			{
				if (Hull.Vertices.Num() < 4)
				{
					continue;
				}
				FBox Box(ForceInit);
				for (const FVector& V : Hull.Vertices)
				{
					Box += Xform.TransformPosition(V);
				}
				DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), Color, /*bPersistent*/ false,
					/*Life*/ -1.f, uint8(SDPG_World), /*Thickness*/ 1.5f);
			}
		}
	}

	// --- I/O beams: fade + draw the captured caller->target arrows (foreground, follow through walls) -
	if (Beams.Num() > 0)
	{
		const double Window = FMath::Max(0.5, double(VizSettings.BeamSeconds));
		Beams.RemoveAll([&](const FBeam& B) { return FadeClock - B.Time > Window; });
		for (const FBeam& B : Beams)
		{
			const double Age = FMath::Clamp(FadeClock - B.Time, 0.0, Window);
			const float K = 1.0f - float(Age / Window);
			const FColor Color(uint8(70 * K + 40), uint8(210 * K + 30), uint8(255 * K), 255);
			DrawDebugDirectionalArrow(World, B.From, B.To, /*ArrowSize*/ 40.f, Color,
				/*bPersistent*/ false, /*Life*/ -1.f, uint8(SDPG_Foreground), /*Thickness*/ 2.f);
		}
	}
#endif // ENABLE_DRAW_DEBUG
}
