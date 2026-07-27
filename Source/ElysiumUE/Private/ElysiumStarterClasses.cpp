// P1.6 — the starter entity classes: the smallest set that makes the substrate observable
// end-to-end on sp_tutorial_1. `logic_auto` ignites the map (OnMapLoad), `logic_relay` is the
// indirection layer a quarter of all wires pass through (OnTrigger), and `trigger_multiple`/
// `trigger_once` turn a brush body's begin/end overlap into OnStartTouch/OnEndTouch/OnTrigger.
//
// Each is a plain-C++ FElysiumEntity subclass (R1, no reflection) registered by a module-static
// FElysiumClassRegistrar; the class chain (R2) reaches the base Kill/ScriptHide/ScriptUnhide +
// keyfields with no per-class boilerplate. The classes are file-local — nothing outside the
// registry references them, so they need no header.

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTrigger, Log, All);

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
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
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
	// against the toucher's flags. The only toucher is still the player (a client), so the test
	// reduces to the ALLOW_CLIENTS bit: a client-allowing trigger fires, a physics-only (0x8, no
	// 0x1) trigger correctly ignores the player. The activator IS resolved now (11.4), so the
	// remaining bits (NPCs, physics objects) grow the test when those touchers exist.
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
// trigger_hurt (P4.5) — CTriggerHurt (2 on the tutorial). Deals `damage` to the player every
// 0.5 s while it stands in the volume, on the substrate clock (R4). Enable/Disable + StartDisabled
// come from CBaseTrigger; the overlap hooks track the player and drive the damage think.
// ============================================================================================

class FElysiumTriggerHurt final : public FElysiumTriggerBase
{
public:
	float Damage = 0.0f;      // damage — points per tick
	int32 DamageType = 0;     // damagetype — bitfield (logged; the WoD damage model lands later)

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || !PlayerPasses())
		{
			return;
		}
		bPlayerInside = true;
		LastActivator = Activator;
		HurtNow();
		NextThink = (World ? World->NowSeconds() : 0.0) + DamageIntervalSeconds;
	}

	virtual void OnTouchEnd(const FElysiumEntityHandle& /*Activator*/) override
	{
		bPlayerInside = false;
		NextThink = ELYSIUM_NEVER_THINK;
	}

	virtual void Think() override
	{
		if (!bPlayerInside || bDisabled || IsInert())
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		HurtNow();
		NextThink = (World ? World->NowSeconds() : 0.0) + DamageIntervalSeconds;
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Damage"), FString::Printf(TEXT("%.0f / %.2fs"), Damage, DamageIntervalSeconds));
		Out.Emplace(TEXT("Damage type"), FString::Printf(TEXT("0x%x"), DamageType));
		Out.Emplace(TEXT("Player inside"), bPlayerInside ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
	}

private:
	static constexpr double DamageIntervalSeconds = 0.5;   // Source trigger_hurt damage cadence

	void HurtNow()
	{
		// 11.4 — the damage receiver is the player *entity*: `health` is a CBaseEntity keyfield and
		// the combat character owns what running out of it means. The body still gets the hit (the
		// engine damage event a flinch/hit reaction will hang off, 4.9), but it is no longer where
		// the number lives.
		if (FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
		{
			Player->TakeDamage(Damage);
		}
		if (IElysiumEmbodiment* PlayerBody = World ? World->Embodiment() : nullptr)
		{
			PlayerBody->DamagePlayer(Damage);
		}
	}

	bool bPlayerInside = false;
	FElysiumEntityHandle LastActivator;
};

// ============================================================================================
// trigger_look (P4.5) — CTriggerLook (4 on the tutorial). Fires OnTrigger once the player, standing
// in the volume, looks at the `target` entity within `FieldOfView` (a forward-dot threshold) for a
// cumulative `LookTime` seconds. Self-contained (needs only the pawn camera + the target origin);
// fires once, then disables (the common Source case).
// ============================================================================================

class FElysiumTriggerLook final : public FElysiumTriggerBase
{
public:
	float LookTime = 0.5f;      // LookTime — required cumulative look seconds
	float FieldOfView = 0.9f;   // FieldOfView — min forward·dir dot (1 = dead-on, 0 = 90°)

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || !PlayerPasses())
		{
			return;
		}
		bPlayerInside = true;
		LastActivator = Activator;
		LookElapsed = 0.0f;
		LastThinkTime = World ? World->NowSeconds() : 0.0;
		NextThink = LastThinkTime;   // per-frame while inside
	}

	virtual void OnTouchEnd(const FElysiumEntityHandle& /*Activator*/) override
	{
		bPlayerInside = false;
		LookElapsed = 0.0f;
		NextThink = ELYSIUM_NEVER_THINK;
	}

	virtual void Think() override
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		const float Dt = (float)FMath::Max(0.0, Now - LastThinkTime);
		LastThinkTime = Now;

		if (!bPlayerInside || bDisabled || IsInert())
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		if (IsLookingAtTarget())
		{
			LookElapsed += Dt;
			if (LookElapsed >= LookTime)
			{
				static const FName OnTrigger(TEXT("OnTrigger"));
				FireOutput(OnTrigger, LastActivator);
				bDisabled = true;            // fire once
				NextThink = ELYSIUM_NEVER_THINK;
				return;
			}
		}
		else
		{
			LookElapsed = 0.0f;              // must be a continuous look (Source resets on look-away)
		}
		NextThink = Now;                     // keep polling each frame
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Target"), Target.IsEmpty() ? TEXT("(none)") : Target);
		Out.Emplace(TEXT("FieldOfView"), FString::Printf(TEXT("%.2f"), FieldOfView));
		Out.Emplace(TEXT("Look progress"), FString::Printf(TEXT("%.2f / %.2f s"), LookElapsed, LookTime));
		Out.Emplace(TEXT("Player inside"), bPlayerInside ? TEXT("yes") : TEXT("no"));
	}

private:
	bool IsLookingAtTarget() const
	{
		const IElysiumEmbodiment* Player = World ? World->Embodiment() : nullptr;
		const FElysiumEntity* Tgt = World ? World->FindByName(Target) : nullptr;
		FVector ViewLoc; FRotator ViewRot;
		if (!Player || !Tgt || !Tgt->Def || !Player->GetPlayerViewPoint(ViewLoc, ViewRot))
		{
			return false;
		}
		const FVector ToTarget = (Tgt->Def->Origin - ViewLoc).GetSafeNormal();
		return FVector::DotProduct(ViewRot.Vector(), ToTarget) >= FieldOfView;
	}

	bool   bPlayerInside = false;
	float  LookElapsed = 0.0f;
	double LastThinkTime = 0.0;
	FElysiumEntityHandle LastActivator;
};

// ============================================================================================
// trigger_autosave (P4.5) — CTriggerAutosave (1 on the tutorial). A checkpoint volume: the player
// entering it triggers a save. Saves land in P10, so this logs the checkpoint and fires once
// (then disables) so it doesn't spam every frame the player lingers.
// ============================================================================================

class FElysiumTriggerAutosave final : public FElysiumTriggerBase
{
public:
	virtual void OnTouchStart(const FElysiumEntityHandle& /*Activator*/) override
	{
		if (bDisabled || IsInert() || !PlayerPasses())
		{
			return;
		}
		++TriggerCount;
		UE_LOG(LogElysiumTrigger, Log, TEXT("%s: autosave checkpoint reached (save deferred to P10)"),
			*DebugString());
		bDisabled = true;   // one-shot per arming; a ScriptUnhide/Enable re-arms it
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Triggered"), FString::Printf(TEXT("%d time(s)"), TriggerCount));
		Out.Emplace(TEXT("Armed"), bDisabled ? TEXT("no") : TEXT("yes"));
		Out.Emplace(TEXT("Save"), TEXT("deferred to P10"));
	}

private:
	int32 TriggerCount = 0;
};

// ============================================================================================
// info_landmark (P4.6) — CBaseLandmark (FUN_100b7590). A bodiless anchor point shared by name
// between two maps: a trigger_changelevel measures the player's offset from the SOURCE map's
// landmark, and the DESTINATION map re-adds that offset to its own same-named landmark to place
// the player (level_transitions.md path 2). The runtime placement lives in the map subsystem +
// AElysiumMapActor::ResolveLandmarkSpawn; this leaf exists so the landmark is a first-class entity
// (not an inert record) with inspectable state and its own OnEnterMapHere output (fired by the map
// actor when the player enters here — e.g. pawnshop's newgame/haven landmarks silence Radio2).
// ============================================================================================

class FElysiumInfoLandmark final : public FElysiumEntity
{
public:
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Origin"), (Def ? Def->Origin : FVector::ZeroVector).ToString());
		Out.Emplace(TEXT("Facing"), FString::Printf(TEXT("yaw %.0f"), -Angles.Y));
	}
};

// ============================================================================================
// trigger_changelevel (P4.6) — CChangeLevel (FUN_101c71f0). A brush trigger over CBaseTrigger that
// carries a `map` (destination) + `landmark` (the shared info_landmark name). When the player is in
// the volume, TouchChangeLevel (FUN_101c7890) fires the transition; VtMB defers the actual swap to
// end-of-frame, so we request a deferred landmark travel through the map subsystem (the swap can't
// run inside this touch — it destroys this very entity world). The player's offset from the SOURCE
// landmark and their view yaw are captured here and re-applied against the destination landmark. The
// scripted path (level-script `ChangeMap(delay, landmark, trigger)` -> the ChangeLevel input) forces
// the same transition without a touch. OnChangeLevel (field-5 Python on some triggers, e.g.
// werewolfBloodHavenExit()) fires just before the swap.
// ============================================================================================

class FElysiumChangeLevel final : public FElysiumTriggerBase
{
public:
	FString DestMap;        // `map` — the destination map name (an exported folder under tools/out)
	FString LandmarkName;   // `landmark` — the info_landmark shared with the destination map

	// SF_CHANGELEVEL_NOTOUCH (stock Source `0x0002`): the transition fires only via a scripted input,
	// never on player touch — the tutorial's changelevels carry this (spawnflags 2) and are activated
	// by the level scripts' ChangeMap. A changelevel WITHOUT the bit (e.g. pawnshop's togenesis) fires
	// on walk-in. NB: trigger_changelevel does NOT use CBaseTrigger's ALLOW_CLIENTS (0x1) convention —
	// its own Touch fires for the player directly — so PlayerPasses() is bypassed here.
	static constexpr int32 SF_NOTOUCH = 0x0002;

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override
	{
		if (bDisabled || IsInert() || (SpawnFlags & SF_NOTOUCH) != 0)
		{
			return;
		}
		DoChangeLevel();
	}

	// The scripted / forced entry (ChangeMap's ChangeLevel input): transition regardless of whether
	// the player is stood in the volume. The offset is still landmark-relative, so a remote fire lands
	// the player correctly at the destination.
	void ForceChangeLevel() { DoChangeLevel(); }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Destination"), FString::Printf(TEXT("%s @ %s"),
			DestMap.IsEmpty() ? TEXT("(none)") : *DestMap,
			LandmarkName.IsEmpty() ? TEXT("(none)") : *LandmarkName));
		Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
		Out.Emplace(TEXT("Trigger"), (SpawnFlags & SF_NOTOUCH) ? TEXT("scripted (NOTOUCH)") : TEXT("player touch"));
		Out.Emplace(TEXT("State"), bChanging ? TEXT("changing") : TEXT("armed"));
	}

private:
	bool bChanging = false;   // latched once the transition is requested (ignore further touches)

	void DoChangeLevel()
	{
		if (bChanging || IsInert())
		{
			return;
		}
		if (DestMap.IsEmpty())
		{
			UE_LOG(LogElysiumTrigger, Warning, TEXT("%s: trigger_changelevel with no map key"), *DebugString());
			return;
		}

		// OnChangeLevel wires fire first (the field-5 Python exit hooks: werewolfBloodHavenExit(), …).
		static const FName OnChangeLevel(TEXT("OnChangeLevel"));
		FireOutput(OnChangeLevel, Handle);

		// Capture the player's offset from THIS map's landmark + their view yaw. The destination map
		// re-adds the offset to its same-named landmark (translation only; the player keeps their yaw).
		FVector Offset = FVector::ZeroVector;
		float   Yaw = 0.0f;
		// 11.4 — the player's position is the player entity's, sampled from its body at the top of
		// this frame like every other entity's origin. `angles` is Source-space, so the Unreal yaw
		// the destination re-applies is the negated one.
		if (const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
		{
			const FVector PlayerLoc = Player->Origin;
			Yaw = -Player->Angles.Y;
			if (const FElysiumEntity* Src = World->FindLandmark(LandmarkName))
			{
				if (Src->Def)
				{
					Offset = PlayerLoc - Src->Def->Origin;
				}
			}
			else if (!LandmarkName.IsEmpty())
			{
				UE_LOG(LogElysiumTrigger, Warning,
					TEXT("%s: source landmark '%s' not found — player placed at destination landmark"),
					*DebugString(), *LandmarkName);
			}
		}

		if (IElysiumTravel* Maps = World ? World->Travel() : nullptr)
		{
			Maps->RequestLandmarkTravel(DestMap, LandmarkName, Offset, Yaw);
			bChanging = true;
		}
	}
};

// ============================================================================================
// logic_pythoncheck (P5 5.4) — a Python expression gate (51 game-wide). Its `python_script`
// keyvalue is an expression (e.g. `G.Story_State < 110`); the `Test` input evaluates it and fires
// OnTrue when the result is truthy, OnFalse otherwise — the standard VtMB branch node
// (FUN_10135290, entity_io.md / python_bridge.md). Evaluation runs through the world's script host
// (EvalCondition), so error-to-false (a raise / an unresolved name) reads OnFalse, and disabling
// live eval (`elysium.script.live 0`) makes every gate fail closed — matching retail's Py_eval_input
// path. The incoming activator is propagated onto the fired branch (Source I/O convention).
// ============================================================================================

class FElysiumPythonCheck final : public FElysiumEntity
{
public:
	FString PythonScript;                     // `python_script` — the gating expression

	// Test's last outcome, for the Cog inspector's Live state (not a keyfield).
	bool bLastResult = false;
	bool bEverTested = false;

	void RunTest(const FElysiumEntityHandle& Activator)
	{
		static const FName OnTrue(TEXT("OnTrue"));
		static const FName OnFalse(TEXT("OnFalse"));

		const FElysiumVariant R = World ? World->EvalCondition(PythonScript, Handle, Activator)
			: FElysiumVariant::Void();
		bLastResult = R.ToBool();   // Void (no host / error-to-false) -> false -> OnFalse
		bEverTested = true;
		FireOutput(bLastResult ? OnTrue : OnFalse, Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("python_script"), PythonScript.IsEmpty() ? TEXT("(none)") : PythonScript);
		Out.Emplace(TEXT("last Test"),
			bEverTested ? (bLastResult ? TEXT("OnTrue") : TEXT("OnFalse")) : TEXT("(not tested yet)"));
	}
};

// ============================================================================================
// Registration
// ============================================================================================

static TUniquePtr<FElysiumEntity> MakeLogicAuto()       { return MakeUnique<FElysiumLogicAuto>(); }
static TUniquePtr<FElysiumEntity> MakeLogicRelay()      { return MakeUnique<FElysiumLogicRelay>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerBase()     { return MakeUnique<FElysiumTriggerBase>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerMultiple() { return MakeUnique<FElysiumTriggerMultiple>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerOnce()     { return MakeUnique<FElysiumTriggerOnce>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerHurt()     { return MakeUnique<FElysiumTriggerHurt>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerLook()     { return MakeUnique<FElysiumTriggerLook>(); }
static TUniquePtr<FElysiumEntity> MakeTriggerAutosave() { return MakeUnique<FElysiumTriggerAutosave>(); }
static TUniquePtr<FElysiumEntity> MakeInfoLandmark()    { return MakeUnique<FElysiumInfoLandmark>(); }
static TUniquePtr<FElysiumEntity> MakeChangeLevel()     { return MakeUnique<FElysiumChangeLevel>(); }
static TUniquePtr<FElysiumEntity> MakePythonCheck()     { return MakeUnique<FElysiumPythonCheck>(); }

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

// The P4.5 trigger family — CBaseTrigger leaves (Enable/Disable/Toggle + StartDisabled inherited).
static FElysiumClassRegistrar GRegTriggerHurt(
	TEXT("trigger_hurt"), FName(TEXT("CBaseTrigger")), &MakeTriggerHurt,
	[](FElysiumClassDesc& D)
	{
		AddSubclassField(D, TEXT("damage"),     &FElysiumTriggerHurt::Damage);
		AddSubclassField(D, TEXT("damagetype"), &FElysiumTriggerHurt::DamageType);
	});

static FElysiumClassRegistrar GRegTriggerLook(
	TEXT("trigger_look"), FName(TEXT("CBaseTrigger")), &MakeTriggerLook,
	[](FElysiumClassDesc& D)
	{
		AddSubclassField(D, TEXT("LookTime"),    &FElysiumTriggerLook::LookTime);
		AddSubclassField(D, TEXT("FieldOfView"), &FElysiumTriggerLook::FieldOfView);
	});

static FElysiumClassRegistrar GRegTriggerAutosave(
	TEXT("trigger_autosave"), FName(TEXT("CBaseTrigger")), &MakeTriggerAutosave,
	[](FElysiumClassDesc& /*D*/) { /* inherits the CBaseTrigger inputs/fields via the chain */ });

// info_landmark — a plain base-class leaf (no inputs; the transition math reads its origin/angles).
static FElysiumClassRegistrar GRegInfoLandmark(
	TEXT("info_landmark"), ElysiumBaseClassName(), &MakeInfoLandmark,
	[](FElysiumClassDesc& /*D*/) { /* bodiless anchor — placement + OnEnterMapHere driven by the map actor */ });

// trigger_changelevel — CBaseTrigger leaf (Enable/Disable/Toggle inherited via the chain). `map` +
// `landmark` are its own keyfields; ChangeLevel is the scripted/forced transition input.
static FElysiumClassRegistrar GRegChangeLevel(
	TEXT("trigger_changelevel"), FName(TEXT("CBaseTrigger")), &MakeChangeLevel,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("ChangeLevel"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{
			static_cast<FElysiumChangeLevel&>(E).ForceChangeLevel();
		});
		AddSubclassField(D, TEXT("map"),      &FElysiumChangeLevel::DestMap);
		AddSubclassField(D, TEXT("landmark"), &FElysiumChangeLevel::LandmarkName);
	});

static FElysiumClassRegistrar GRegPythonCheck(
	TEXT("logic_pythoncheck"), ElysiumBaseClassName(), &MakePythonCheck,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Test"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			static_cast<FElysiumPythonCheck&>(E).RunTest(Args.Activator);
		});
		AddSubclassField(D, TEXT("python_script"), &FElysiumPythonCheck::PythonScript);
	});
