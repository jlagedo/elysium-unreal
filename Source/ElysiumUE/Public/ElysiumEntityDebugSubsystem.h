#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEntityDebugSubsystem.generated.h"

class FElysiumEntity;
class FElysiumEntityWorld;
struct FElysiumIOEvent;
struct FElysiumOutputDef;
class IConsoleObject;

// P2.3 (debug-tooling.md Layer 2) — the Source-style `ent_*` verb set, on the Track-B chokepoints.
// A world subsystem (one per game/PIE world, surviving map-actor travels) that:
//   * registers the `elysium.ent_*` console verbs and drives them off the live entity world
//     (reached through the map subsystem's current map actor),
//   * ticks to render the per-entity overlay bitmask (ent_text / ent_bbox / ent_messages), and
//   * installs one debug sink into each new entity world to tap the two chokepoints for the
//     ent_messages I/O overlay and the ent_break breakpoint.
//
// Everything here is dev tooling: registration + tick work compile out of Shipping (#if
// !UE_BUILD_SHIPPING), and the in-world draws compile out wherever ENABLE_DRAW_DEBUG is off.
UCLASS()
class UElysiumEntityDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem ---------------------------------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- UTickableWorldSubsystem (FTickableGameObject) ----------------------------------
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// --- Chokepoint tap (called by the installed sink) ----------------------------------
	// An input was delivered to Target: evaluate the ent_break breakpoint and capture an
	// ent_messages line for the overlay.
	void TapDelivered(FElysiumEntityWorld& World, double Now, const FElysiumEntity& Target,
		const FElysiumIOEvent& Event);
	// Source fired a named output: capture an ent_messages line for the overlay.
	void TapOutput(FElysiumEntityWorld& World, double Now, const FElysiumEntity& Source,
		const FElysiumOutputDef& Output);

	// Per-entity overlay bits (ent_text / ent_bbox / ent_messages), keyed by entity index. Public so
	// the Cog Entity Inspector can flip the same overlays the console verbs toggle.
	enum EOverlay : uint8
	{
		Overlay_Text     = 1 << 0,
		Overlay_BBox     = 1 << 1,
		Overlay_Messages = 1 << 2,
	};

	// --- UI-facing controls (the Cog Inspector calls these; the ent_* verbs share the state) -----
	// The entity under the crosshair (resolves world + substrate internally); Invalid if none close.
	FElysiumEntityHandle PickSelection();
	bool IsOverlayOn(int32 EntityIndex, uint8 Bit) const;
	void SetOverlay(int32 EntityIndex, uint8 Bit, bool bOn);
	// ent_break, handle-scoped: armed on exactly this entity (any input)?  arm / clear it.
	bool IsBreakArmedOn(const FElysiumEntityHandle& Handle) const;
	void ArmBreakOn(const FElysiumEntityHandle& Handle);
	void ClearBreak();

private:
	// One captured I/O line for the ent_messages overlay. `Time` is in fade-clock seconds (which
	// freeze while the event queue is paused, so the evidence stays put on a breakpoint).
	struct FOverlayMessage
	{
		double Time = 0.0;
		int32 EntityIndex = INDEX_NONE;
		FString Text;
	};

	// --- Verb handlers ------------------------------------------------------------------
	void HandleFire(const TArray<FString>& Args, UWorld* World);
	void HandleDump(const TArray<FString>& Args, UWorld* World);
	void HandleInfo(const TArray<FString>& Args);
	void HandlePause(UWorld* World);
	void HandleStep(const TArray<FString>& Args, UWorld* World);
	void HandleBreak(const TArray<FString>& Args, UWorld* World);
	void HandleOverlay(const TArray<FString>& Args, UWorld* World, uint8 Bit, const TCHAR* Name);
	void HandleClear();

	// --- Helpers ------------------------------------------------------------------------
	FElysiumEntityWorld* GetSubstrate() const;
	// Resolve a target argument to live entity handles: "!picker" / empty = the entity under the
	// crosshair; otherwise every live entity whose targetname or classname matches (case-folded).
	void ResolveTargets(FElysiumEntityWorld& EW, UWorld* World, const FString& Target,
		TArray<FElysiumEntityHandle>& Out) const;
	// The entity under the crosshair: a brush body hit (solids block, triggers overlap), else the
	// bodiless logic entity whose origin is nearest the aim ray. Invalid if nothing is close.
	FElysiumEntityHandle PickUnderCrosshair(UWorld* World, FElysiumEntityWorld& EW) const;
	// Drop overlay/break/message state (on world change — handles are per-epoch).
	void ResetState();
	void RenderOverlays(FElysiumEntityWorld& EW);

	TArray<IConsoleObject*> ConsoleObjects;

	// Overlay + breakpoint state, valid only for the currently hooked world epoch.
	TMap<int32, uint8> OverlayBits;
	TArray<FOverlayMessage> Messages;
	double FadeClock = 0.0;                 // advances only while the queue is not paused
	uint32 HookedEpoch = 0;                 // world epoch whose chokepoints we've tapped (0 = none)

	// ent_break: pause the queue when a matching input is delivered. Handle-scoped when armed via
	// the picker (BreakHandle set); otherwise matched by targetname-or-classname string.
	bool bBreakArmed = false;
	FString BreakTarget;
	FElysiumEntityHandle BreakHandle;
	FName BreakInput = NAME_None;           // NAME_None = any input
};
