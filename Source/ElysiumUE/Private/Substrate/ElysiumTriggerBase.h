#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

// The shared touch base every trigger leaf derives from. The registration site —
// `BuildCBaseTrigger` and the `CBaseTrigger` registrar — stays in `ElysiumStarterClasses.cpp`.

// CBaseTrigger — the shared touch base for trigger_multiple/trigger_once (and the trigger family).
// Enable/Disable/Toggle + the StartDisabled/wait keyfields live here; the overlap→output
// translation is the OnTouchStart/OnTouchEnd overrides. The classname never appears in `.ents` —
// it is purely the chain node the leaf triggers derive from (base CBaseEntity).

class FElysiumTriggerBase : public FElysiumEntity
{
public:
	bool   bDisabled = false;                 // StartDisabled keyvalue
	FString FilterName;                        // filtername — resolved once during late Activate
	float  Wait = 0.2f;                        // `wait` — min seconds between OnTrigger re-fires
	double LastTriggerTime = -1.0e18;          // last OnTrigger fire (game seconds); primed to always-due
	// wait == -1 (entity_io.md: the value CTriggerOnce::Spawn forces): after the one accepted
	// activation, ActivateMultiTrigger nulls the touch handler and schedules SUB_Remove at
	// curtime + 0.1. This latches that "touch handler nulled, removal pending" state.
	bool   bTouchSuppressed = false;

	// CBaseTrigger::PassesTriggerFilters (RE1, entity_io.md) reads the ALLOW_* spawnflag bits
	// against the toucher's flags. This leaf currently implements the ALLOW_CLIENTS consumer: the
	// player fires a client trigger, while a native NPC body retains its own entity handle and is
	// rejected instead of masquerading as !player. ALLOW_NPCS and physics grow this test when those
	// trigger consumers are implemented.
	bool PlayerPasses(const FElysiumEntityHandle& Activator) const
	{
		return World && Activator == World->PlayerHandle() && (SpawnFlags & 0x1) != 0;
	} // 0x1 = ALLOW_CLIENTS
	virtual void Activate() override
	{
		FilterHandle = FElysiumEntityHandle::Invalid();
		if (World && !FilterName.IsEmpty())
		{
			if (const FElysiumEntity* Filter = World->FindByName(FilterName))
			{
				FilterHandle = Filter->Handle;
			}
		}
	}
	virtual bool CanBeginTouch(const FElysiumEntityHandle& Activator) const override
	{
		if (bDisabled || IsInert() || !PlayerPasses(Activator))
		{
			return false;
		}
		const FElysiumEntity* Filter = World ? World->Resolve(FilterHandle) : nullptr;
		return !Filter || Filter->PassesFilter(Activator);
	}
	virtual bool IsBrushBodyEnabled() const override
	{
		return !bDisabled && !IsInert();
	}
	void SetDisabled(bool bInDisabled)
	{
		if (bDisabled == bInDisabled)
		{
			return;
		}
		bDisabled = bInDisabled;
		// SetDormant(false) explicitly refreshes overlaps, so enabling beneath an already-contained
		// player produces the same late StartTouch that retail's FSOLID_TRIGGER re-add does.
		RefreshBrushBodyState();
	}

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override
	{
		// entity_io.md "OnStartTouch still fires inside the wait == -1 removal window": what
		// ActivateMultiTrigger nulls is `m_pfnTouch`, and CBaseEntity::Touch is its only consumer —
		// so nulling it gates the ACTIVATION half alone. CBaseTrigger::StartTouch carries no wait
		// or removal test of its own, and for the ~0.1 s before SUB_Remove runs the volume is still
		// solid with a clean deletion flag. A genuine re-entry inside that window therefore still
		// produces OnStartTouch and still produces no OnTrigger, which is why bTouchSuppressed gates
		// the activation block below and not this admission test.
		//
		// The disabled/inert half of that test is retail's solidity, not an extra rule: a disabled
		// trigger has no FSOLID_TRIGGER to link against, which here is the dormant brush body that
		// already turned the begin away in CanBeginTouch.
		if (bDisabled || IsInert() || !PlayerPasses(Activator))
		{
			return;
		}
		static const FName OnStartTouch(TEXT("OnStartTouch"));
		static const FName OnTrigger(TEXT("OnTrigger"));

		// OnStartTouch fires on every touch begin; OnTrigger is rate-limited by `wait` (Source's
		// multi-manager gate). FireOutput no-ops on outputs the entity did not wire, so firing both
		// is safe regardless of which rows this instance carries.
		FireOutput(OnStartTouch, Activator);

		const double Now = World ? World->NowSeconds() : 0.0;
		if (!bTouchSuppressed && Now - LastTriggerTime >= Wait)
		{
			LastTriggerTime = Now;
			FireOutput(OnTrigger, Activator);

			if (Wait == -1.0f)
			{
				// entity_io.md "trigger_multiple / trigger_once edge and re-arm order": wait == -1
				// (CTriggerOnce::Spawn's forced value) routes ActivateMultiTrigger into
				// SetTouch(NULL); SetThink(SUB_Remove); next think at curtime + 0.1. Already-queued
				// delayed output rows the OnTrigger fire above just enqueued stay valid queue
				// entries — Kill() (called from Think(), below) never touches the event queue.
				bTouchSuppressed = true;
				NextThink = Now + 0.1;
			}
		}
	}

	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || !PlayerPasses(Activator))
		{
			return;
		}
		static const FName OnEndTouch(TEXT("OnEndTouch"));
		FireOutput(OnEndTouch, Activator);
	}

	virtual void Think() override
	{
		// The self-removal scheduled by the wait == -1 route above. A subclass with its own Think()
		// (trigger_hurt, trigger_look) never reaches this OnTouchStart, so it never arms this.
		if (bTouchSuppressed)
		{
			Kill();
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		// LastTriggerTime is an absolute game-clock time (World->NowSeconds()); a restore resets the
		// clock to the saved ClockNow before any entity's NextThink/wait gate is read back, so the
		// absolute value still means what it meant when written — the same
		// reasoning that lets NextThink itself ride the generic entity record unconverted.
		Ar << LastTriggerTime;
		Ar << bTouchSuppressed;
	}

	// The enable latch every trigger leaf shares, readable by the map-slice tests through the one
	// debug seam (`Tests/ElysiumEntityDebugStateTestHelpers.h`). A leaf that publishes its own rows
	// overrides and may skip this.
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Disabled"), bDisabled ? TEXT("yes") : TEXT("no"));
	}

private:
	FElysiumEntityHandle FilterHandle;
};
