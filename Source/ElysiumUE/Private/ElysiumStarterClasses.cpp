// P1.6 — the starter entity classes: the smallest set that makes the substrate observable
// end-to-end on sp_tutorial_1. `logic_auto` ignites the map (OnMapLoad), `logic_relay` is the
// indirection layer a quarter of all wires pass through (OnTrigger), and `trigger_multiple`/
// `trigger_once` turn a brush body's begin/end overlap into OnStartTouch/OnEndTouch/OnTrigger.
//
// Each is a plain-C++ FElysiumEntity subclass (R1, no reflection) registered by a module-static
// FElysiumClassRegistrar; the class chain (R2) reaches the base Kill/ScriptHide/ScriptUnhide +
// keyfields with no per-class boilerplate. The classes are file-local — nothing outside the
// registry references them, so they need no header.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include <type_traits>

namespace
{
	// Register a field backed by a *subclass* member. FElysiumClassDesc::Field only takes members
	// of the base FElysiumEntity; leaf classes carry their own state (bDisabled, wait), so the
	// accessor static_casts the entity to TClass. Always valid: a class's field table is only ever
	// walked for entities of that class or a subclass (Construct applies keyvalues through the
	// entity's own chain, the P2 inspector reads an entity through its own chain).
	template <typename TClass, typename TMember>
	void AddSubclassField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			// ToInt (not ToBool): a "0" keyvalue must read false, not "non-empty -> true".
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddSubclassField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// logic_auto — map-load ignition (678 wires across all maps; 5 instances / 13 OnMapLoad rows on
// the tutorial). Its only job is to fire its OnMapLoad outputs once the map is up.
// ============================================================================================

class FElysiumLogicAuto final : public FElysiumEntity
{
public:
	virtual void Spawn() override
	{
		// Retail logic_auto fires OnMapLoad on the first server think after spawn — not mid-spawn,
		// so every target has finished spawning. Schedule a one-shot think at t=0: it is due on the
		// first world Tick, and RunThinks clears NextThink before Think() runs, so it fires once.
		NextThink = 0.0f;
	}

	virtual void Think() override
	{
		static const FName OnMapLoad(TEXT("OnMapLoad"));
		FireOutput(OnMapLoad, Handle);
		// Fire-once — no reschedule. (Stock Source SF 1 = remove-on-fire; not modelled: the one-shot
		// think already fires exactly once, and save/load reaping is a later concern.)
	}
};

// ============================================================================================
// logic_relay — the indirection layer (5,957 OnTrigger wires — a quarter of all game wires; 107
// instances on the tutorial). `Trigger` re-fires the entity's OnTrigger outputs, propagating the
// activator; Enable/Disable/Toggle gate it (a disabled relay swallows Trigger).
// ============================================================================================

class FElysiumLogicRelay final : public FElysiumEntity
{
public:
	bool bDisabled = false;   // StartDisabled keyvalue; flipped by Enable/Disable/Toggle
};

// ============================================================================================
// CBaseTrigger — the shared touch base for trigger_multiple/trigger_once (and the P4.5 trigger
// family). Enable/Disable/Toggle + the StartDisabled/wait keyfields live here; the overlap→output
// translation is the OnTouchStart/OnTouchEnd overrides. The classname never appears in `.ents` —
// it is purely the chain node the leaf triggers derive from (base CBaseEntity).
// ============================================================================================

class FElysiumTriggerBase : public FElysiumEntity
{
public:
	bool   bDisabled = false;                 // StartDisabled keyvalue
	float  Wait = 0.2f;                        // `wait` — min seconds between OnTrigger re-fires
	double LastTriggerTime = -1.0e18;          // last OnTrigger fire (game seconds); primed to always-due

	virtual bool IsOnce() const { return false; }

	// CBaseTrigger::PassesTriggerFilters (RE1, entity_io.md) reads the ALLOW_* spawnflag bits
	// against the toucher's flags. In P1.6 the only toucher is the player (a client) and its
	// activator is unresolved (the pawn is not an entity yet), so the test reduces to the
	// ALLOW_CLIENTS bit: a client-allowing trigger fires, a physics-only (0x8, no 0x1) trigger
	// correctly ignores the player. Grows the full flag/filter test when the pawn becomes an
	// entity (P4) and NPC touchers exist.
	bool PlayerPasses() const { return (SpawnFlags & 0x1) != 0; }   // 0x1 = ALLOW_CLIENTS

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || !PlayerPasses())
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
		if (Now - LastTriggerTime >= Wait)
		{
			LastTriggerTime = Now;
			FireOutput(OnTrigger, Activator);
		}

		if (IsOnce())
		{
			// trigger_once removes itself after the first successful touch (CTriggerOnce). Kill drops
			// the body's collision (R6), so no further begin/end overlap routes here — and OnEndTouch
			// never fires, matching retail (the volume is gone before the player leaves it).
			Kill();
		}
	}

	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || !PlayerPasses())
		{
			return;
		}
		static const FName OnEndTouch(TEXT("OnEndTouch"));
		FireOutput(OnEndTouch, Activator);
	}
};

class FElysiumTriggerMultiple final : public FElysiumTriggerBase {};

class FElysiumTriggerOnce final : public FElysiumTriggerBase
{
public:
	virtual bool IsOnce() const override { return true; }
};

// ============================================================================================
// Registration
// ============================================================================================

static TUniquePtr<FElysiumEntity> MakeLogicAuto()       { return MakeUnique<FElysiumLogicAuto>(); }
static TUniquePtr<FElysiumEntity> MakeLogicRelay()      { return MakeUnique<FElysiumLogicRelay>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerBase()     { return MakeUnique<FElysiumTriggerBase>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerMultiple() { return MakeUnique<FElysiumTriggerMultiple>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerOnce()     { return MakeUnique<FElysiumTriggerOnce>(); }

// The Enable/Disable/Toggle input set shared by CBaseTrigger. Free function (not a captured
// lambda) so the module-static registrars can reference it without static-init ordering hazards.
static void BuildCBaseTrigger(FElysiumClassDesc& D)
{
	D.Input(TEXT("Enable"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumTriggerBase&>(E).bDisabled = false; });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumTriggerBase&>(E).bDisabled = true; });
	D.Input(TEXT("Toggle"),  [](FElysiumEntity& E, const FElysiumInputArgs&)
	{
		FElysiumTriggerBase& T = static_cast<FElysiumTriggerBase&>(E);
		T.bDisabled = !T.bDisabled;
	});
	AddSubclassField(D, TEXT("StartDisabled"), &FElysiumTriggerBase::bDisabled);
	AddSubclassField(D, TEXT("wait"),          &FElysiumTriggerBase::Wait);
}

static FElysiumClassRegistrar GRegLogicAuto(
	TEXT("logic_auto"), ElysiumBaseClassName(), &MakeLogicAuto,
	[](FElysiumClassDesc& /*D*/) { /* ignition lives in Spawn()/Think(); no extra inputs/fields */ });

static FElysiumClassRegistrar GRegLogicRelay(
	TEXT("logic_relay"), ElysiumBaseClassName(), &MakeLogicRelay,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Trigger"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumLogicRelay& R = static_cast<FElysiumLogicRelay&>(E);
			if (R.bDisabled)
			{
				return;
			}
			static const FName OnTrigger(TEXT("OnTrigger"));
			R.FireOutput(OnTrigger, Args.Activator);   // propagate the incoming activator (Source relays pass it through)
		});
		D.Input(TEXT("Enable"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLogicRelay&>(E).bDisabled = false; });
		D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLogicRelay&>(E).bDisabled = true; });
		D.Input(TEXT("Toggle"),  [](FElysiumEntity& E, const FElysiumInputArgs&)
		{
			FElysiumLogicRelay& R = static_cast<FElysiumLogicRelay&>(E);
			R.bDisabled = !R.bDisabled;
		});
		AddSubclassField(D, TEXT("StartDisabled"), &FElysiumLogicRelay::bDisabled);
		// (Stock Source SF 1 = remove-on-fire / SF 2 = allow-fast-retrigger are unconfirmed for
		// VtMB — buttons diverge from stock — so they are not modelled; per-output `times` already
		// caps re-fires. Revisit with an RE pass if a relay over-fires on the tutorial.)
	});

static FElysiumClassRegistrar GRegCBaseTrigger(
	FName(TEXT("CBaseTrigger")), ElysiumBaseClassName(), &MakeTriggerBase, &BuildCBaseTrigger);

static FElysiumClassRegistrar GRegTriggerMultiple(
	TEXT("trigger_multiple"), FName(TEXT("CBaseTrigger")), &MakeTriggerMultiple,
	[](FElysiumClassDesc& /*D*/) { /* inherits everything from CBaseTrigger via the chain */ });

static FElysiumClassRegistrar GRegTriggerOnce(
	TEXT("trigger_once"), FName(TEXT("CBaseTrigger")), &MakeTriggerOnce,
	[](FElysiumClassDesc& /*D*/) { /* inherits everything from CBaseTrigger via the chain */ });
