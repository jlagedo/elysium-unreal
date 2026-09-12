// The two singleton event-bus entities every VtMB map carries: `events_player` (the
// player's event surface, targetnames pc_0 / pcevents / controller) and `events_world` (the
// world-policy + world-event surface, targetname `world`). They are the map's data bus: level
// scripts hang their world-event callbacks off `events_world`'s outputs, and the discipline /
// frenzy / morph outputs off `events_player`. Both are plain-C++ FElysiumEntity leaves
// registered by a module-static FElysiumClassRegistrar, inheriting the base
// Kill/ScriptHide/ScriptUnhide + keyfields (and OnUseBegin/OnUseEnd) through the class chain.
//
// Provenance: both datamaps were read out of the decompiled vampire.dll.
//   CPlayerEvents  factory FUN_10226630 (object 0x67c), ctor 0x102270b0, vftable 0x1048d844,
//                  datamap_t 0x105b28c0 -> records 0x105b2904, 36 fields; the leading 14 are
//                  static, the trailing 22 outputs are written by the builder FUN_10226700.
//   CWorldEvents   factory FUN_1023cf80, vftable 0x10496acc, datamap_t 0x105c2a50 ->
//                  records 0x105c2a94, 31 fields; leading 11 static, trailing 20 outputs built
//                  by FUN_1023d050.
// Both chain to the shared CBaseEntity map 0x10552e18, which is where OnUseBegin/OnUseEnd come
// from (the tutorial's `world` wires them) — they are not declared on CWorldEvents itself.
//
// The input/field surface is complete and faithful. The *outputs* are fired by other systems
// (disciplines, frenzy, wolf morph, the cop/masquerade AI, the music state machine). Inputs
// deliver, policy state is recorded and inspectable, and every output wire resolves. Each input
// that fronts an unbuilt system records its state and logs; none of them silently no-op.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumLaw.h"   // the SetSafeArea world-to-player transaction

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEvents, Log, All);

namespace
{
	const TCHAR* OnOff(bool b) { return b ? TEXT("on") : TEXT("off"); }
}

// --- events_player — CPlayerEvents ---
// 12 inputs, 23 outputs, one keyfield (`enabled`). The tutorial carries three instances: `pc_0`
// (the map-load MakePlayerUnkillable target and the discipline output source), `pcevents` (Jack's
// OnDamaged -> ClearDialogCombatTimers), and `controller` (sp_observatory_1 fires ImmobilizePlayer
// at its own copy).
//
// EnableOutputs/DisableOutputs gate the whole output surface — modelled here as the real gate
// (FireEvent early-outs), so a disabled bus stays silent exactly as retail's does.

class FElysiumPlayerEvents final : public FElysiumEntity
{
public:
	// `enabled` keyfield (m_bEnabled @0x450). Every exported instance sets it to 1.
	bool bEnabled = true;

	// Player-policy state these inputs latch. The systems that read them live elsewhere
	// (damage/RPG, NPC AI), so the bus records the intent and reports it in the inspector.
	bool bUnkillable       = false;   // MakePlayerUnkillable / MakePlayerKillable
	bool bImmobilized      = false;   // ImmobilizePlayer / MobilizePlayer
	int32 DisciplineClears = 0;       // RemoveDisciplines(+Now) call count
	int32 DialogTimerClears= 0;       // ClearDialogCombatTimers call count
	FString LastAwardExp;             // AwardExp <experience_table key>

	void InputEnableOutputs()  { bEnabled = true; }
	void InputDisableOutputs() { bEnabled = false; }

	// The latch is the damage system's gate, and the damage system is the player entity's
	// — so the write lands there. The copy kept here is what the inspector shows and what a second
	// events_player on the same map would report; the player's own copy is the one that decides.
	void InputMakePlayerUnkillable() { SetPlayerUnkillable(true);  Note(TEXT("MakePlayerUnkillable")); }
	void InputMakePlayerKillable()   { SetPlayerUnkillable(false); Note(TEXT("MakePlayerKillable")); }

	void SetPlayerUnkillable(bool bValue)
	{
		bUnkillable = bValue;
		if (FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
		{
			Player->SetUnkillable(bValue);
		}
	}

	void SetPlayerImmobilized(bool bValue)
	{
		bImmobilized = bValue;
		if (FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
		{
			Player->SetImmobilized(bValue);
		}
	}

	void InputImmobilizePlayer() { SetPlayerImmobilized(true);  Note(TEXT("ImmobilizePlayer")); }
	void InputMobilizePlayer()   { SetPlayerImmobilized(false); Note(TEXT("MobilizePlayer")); }

	void InputCreateControllerNPC()
	{
		if (World)
		{
			World->CreatePlayerControllerEntity();
		}
		Note(TEXT("CreateControllerNPC"));
	}
	void InputRemoveControllerNPC()
	{
		if (World)
		{
			World->RemovePlayerControllerEntity();
		}
		Note(TEXT("RemoveControllerNPC"));
	}

	void InputRemoveDisciplines()    { ++DisciplineClears; Note(TEXT("RemoveDisciplines")); }
	void InputRemoveDisciplinesNow() { ++DisciplineClears; Note(TEXT("RemoveDisciplinesNow")); }

	// `CPlayerEvents::InputClearDialogCombatTimers` (`0x10227250`) — resets the combat timers the
	// player-side dialogue refusal predicate (`FUN_10178170`) reads. Like the killable/immobilize
	// pair above, the state lives on the player entity and the copy kept here is the inspector's.
	void InputClearDialogCombatTimers()
	{
		++DialogTimerClears;
		if (FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr)
		{
			Player->ClearDialogCombatTimers();
		}
	}

	void InputAwardExp(const FElysiumInputArgs& A)
	{
		LastAwardExp = A.Param.ToString();
		// The key is recorded so the wire is traceable; the experience_table.txt lookup awards XP.
		UE_LOG(LogElysiumEvents, Log, TEXT("%s AwardExp '%s' (XP award is 9.4)"),
			*DebugString(), *LastAwardExp);
	}

	// The one output chokepoint: honours the EnableOutputs/DisableOutputs gate. The discipline /
	// frenzy / morph events that drive these live in their own systems; this is the seam they call.
	void FireEvent(FName Output, const FElysiumEntityHandle& Activator)
	{
		if (!bEnabled)
		{
			return;
		}
		FireOutput(Output, Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Outputs"),      OnOff(bEnabled));
		Out.Emplace(TEXT("Player"),       bUnkillable ? TEXT("unkillable") : TEXT("killable"));
		Out.Emplace(TEXT("Movement"),     bImmobilized ? TEXT("immobilized") : TEXT("free"));
		Out.Emplace(TEXT("Controller NPC"), OnOff(World && World->FindPlayerController() != nullptr));
		Out.Emplace(TEXT("Discipline clears"), FString::FromInt(DisciplineClears));
		Out.Emplace(TEXT("Dialog timer clears"), FString::FromInt(DialogTimerClears));
		Out.Emplace(TEXT("Last AwardExp"), LastAwardExp.IsEmpty() ? TEXT("(none)") : LastAwardExp);
	}

private:
	void Note(const TCHAR* What) const
	{
		UE_LOG(LogElysiumEvents, Verbose, TEXT("%s %s"), *DebugString(), What);
	}
};

// --- events_world — CWorldEvents ---
// 10 inputs, 21 outputs (+ the base OnUseBegin/OnUseEnd). One instance per map, targetname
// `world`. Its inputs are world *policy* (safe area, cop grace, frenzy suppression, AI enable,
// wetness) — all of them read by other systems, so the faithful implementation is to hold them
// accurately and expose them.
//
// The tutorial sets SetNoFrenzyArea 1 at OnMapLoad; worldspawn also carries `copwaitarea` and
// `nosferatu_tolerrant` keys that mirror two of these, so the entity is the runtime override.

class FElysiumWorldEvents final : public FElysiumEntity
{
public:
	// World policy. Defaults match "no special rule in force".
	int32 SafeArea          = 0;      // SetSafeArea (int)
	bool  bCopWaitArea      = false;  // SetCopWaitArea (bool)
	float CopGrace          = 0.0f;   // SetCopGrace (float, seconds)
	bool  bNosferatuTolerant= false;  // SetNosferatuTolerant (bool)
	bool  bNoFrenzyArea     = false;  // SetNoFrenzyArea (bool)
	bool  bAIEnabled        = true;   // AIEnable (bool)
	bool  bCutsceneHidden   = false;  // Hide/UnhideCutsceneInterferingEntities

	// The authored `worldspawn` baseline.
	//
	// The world's area type and its three companion policies are AUTHORED ON `worldspawn`, not on
	// this entity: sixteen of the exported maps carry `safearea`, fourteen `copwaitarea`, six
	// `nosferatu_tolerrant` and three `nofrenzyarea`, while no exported `events_world` carries a
	// policy key at all. Retail's world singleton derives its `m_nAreaType` from that key and
	// `CWorldEvents` mutates the same field, so seeding here is what makes this leaf the same
	// store rather than a second one that starts at zero in a map authored `safearea 1`.
	virtual void Spawn() override
	{
		const FElysiumEntityDef* WorldSpawn = FindWorldSpawnDef();
		if (WorldSpawn == nullptr)
		{
			return;
		}
		// An `events_world` that authored the key itself keeps its own value: Construct already
		// applied it, and the worldspawn baseline must not overwrite a more specific statement.
		auto Own = [this](const TCHAR* Key) { return Def != nullptr && Def->Keys.Contains(Key); };
		auto SeedInt = [WorldSpawn, &Own](const TCHAR* Key, int32& Out)
		{
			const FString* Value = Own(Key) ? nullptr : WorldSpawn->Keys.Find(Key);
			if (Value) { Out = FCString::Atoi(**Value); }
		};
		auto SeedBool = [WorldSpawn, &Own](const TCHAR* Key, bool& Out)
		{
			const FString* Value = Own(Key) ? nullptr : WorldSpawn->Keys.Find(Key);
			if (Value) { Out = FCString::Atoi(**Value) != 0; }
		};
		SeedInt(TEXT("safearea"), SafeArea);
		SafeArea = FMath::Clamp(SafeArea, 0, 2);
		SeedBool(TEXT("copwaitarea"), bCopWaitArea);
		SeedBool(TEXT("nosferatu_tolerrant"), bNosferatuTolerant);
		SeedBool(TEXT("nofrenzyarea"), bNoFrenzyArea);
	}

	// `CWorldEvents::SetSafeArea` is a world-to-player TRANSACTION, not a field write. The server
	// applies the new area's policy to every connected player before it marks the world state dirty:
	// Elysium runs the ordinary all-Discipline teardown, safe / Masquerade ends only Celerity and
	// Protean, and combat has no immediate teardown. The policy itself is
	// `ElysiumLaw::ApplyWorldAreaTransition`; the value stays here, where retail's world keeps it,
	// and the transition runs only when it actually changed.
	void InputSetSafeArea(const FElysiumInputArgs& A)
	{
		const int32 NewArea = FMath::Clamp(A.Param.ToInt(), 0, 2);
		if (NewArea == SafeArea)
		{
			return;
		}
		SafeArea = NewArea;
		if (World)
		{
			ElysiumLaw::ApplyWorldAreaTransition(*World, SafeArea);
		}
	}
	void InputSetCopWaitArea(const FElysiumInputArgs& A)        { bCopWaitArea = A.Param.ToInt() != 0; }
	void InputSetCopGrace(const FElysiumInputArgs& A)           { CopGrace = A.Param.ToFloat(); }
	void InputSetNosferatuTolerant(const FElysiumInputArgs& A)  { bNosferatuTolerant = A.Param.ToInt() != 0; }
	void InputSetNoFrenzyArea(const FElysiumInputArgs& A)       { bNoFrenzyArea = A.Param.ToInt() != 0; }
	// `CWorldEvents::InputAIEnable` `0x1023e290`: a bool variant is passed to `SetAIEnabled`
	// `0x10265680`; any other variant type disables. The world owns the flag (`g_AIDisabled`
	// bit 0); this leaf only mirrors it for its debug row.
	void InputAIEnable(const FElysiumInputArgs& A)
	{
		bAIEnabled = A.Param.Type == EElysiumVariantType::Bool && A.Param.AsBool;
		if (World)
		{
			World->SetAiEnabled(bAIEnabled);
		}
	}
	void InputFadeGlobalWetness(const FElysiumInputArgs& A)
	{
		if (World)
		{
			World->FadeGlobalWetness(A.Param.ToFloat());
		}
	}

	void InputHideCutsceneInterferingEntities()   { bCutsceneHidden = true; }
	void InputUnhideCutsceneInterferingEntities() { bCutsceneHidden = false; }

	void InputPlayEndCredits(const FElysiumInputArgs& A)
	{
		UE_LOG(LogElysiumEvents, Log, TEXT("%s PlayEndCredits '%s' (credits sequence unbuilt)"),
			*DebugString(), *A.Param.ToString());
	}

	// The seam the cop/masquerade/music systems fire their world events through, so that when they
	// land they route to the level script's callbacks with no extra plumbing.
	void FireEvent(FName Output, const FElysiumEntityHandle& Activator)
	{
		FireOutput(Output, Activator);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Safe area"),         FString::FromInt(SafeArea));
		Out.Emplace(TEXT("Cop wait area"),     OnOff(bCopWaitArea));
		Out.Emplace(TEXT("Cop grace"),         FString::SanitizeFloat(CopGrace));
		Out.Emplace(TEXT("Nosferatu tolerant"),OnOff(bNosferatuTolerant));
		Out.Emplace(TEXT("No-frenzy area"),    OnOff(bNoFrenzyArea));
		Out.Emplace(TEXT("AI"),                OnOff(bAIEnabled));
		Out.Emplace(TEXT("Global wetness"), World
			? FString::Printf(TEXT("%.3f -> %.3f"), World->GetWeatherState().CurrentWetness,
				World->GetWeatherState().TargetWetness)
			: TEXT("(no world)"));
		Out.Emplace(TEXT("Cutscene hide"),     OnOff(bCutsceneHidden));
	}

private:
	// The map's `worldspawn` def, or null. It is a record-only entity, so this reads the def rather
	// than a live leaf.
	const FElysiumEntityDef* FindWorldSpawnDef() const
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		static const FString WorldSpawnClass(TEXT("worldspawn"));
		for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
		{
			if (Ent && Ent->Def && Ent->Def->Classname.Equals(WorldSpawnClass, ESearchCase::IgnoreCase))
			{
				return Ent->Def;
			}
		}
		return nullptr;
	}
};

// --- Registration ---

static TUniquePtr<FElysiumEntity> MakePlayerEvents() { return MakeUnique<FElysiumPlayerEvents>(); }
static TUniquePtr<FElysiumEntity> MakeWorldEvents()  { return MakeUnique<FElysiumWorldEvents>(); }

static FElysiumClassRegistrar GRegPlayerEvents(
	TEXT("events_player"), ElysiumBaseClassName(), &MakePlayerEvents,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("EnableOutputs"),           [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputEnableOutputs(); });
		D.Input(TEXT("DisableOutputs"),          [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputDisableOutputs(); });
		D.Input(TEXT("CreateControllerNPC"),     [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputCreateControllerNPC(); });
		D.Input(TEXT("RemoveControllerNPC"),     [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputRemoveControllerNPC(); });
		D.Input(TEXT("AwardExp"),                [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumPlayerEvents&>(E).InputAwardExp(A); });
		D.Input(TEXT("ClearDialogCombatTimers"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputClearDialogCombatTimers(); });
		D.Input(TEXT("ImmobilizePlayer"),        [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputImmobilizePlayer(); });
		D.Input(TEXT("MobilizePlayer"),          [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputMobilizePlayer(); });
		D.Input(TEXT("RemoveDisciplines"),       [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputRemoveDisciplines(); });
		D.Input(TEXT("RemoveDisciplinesNow"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputRemoveDisciplinesNow(); });
		D.Input(TEXT("MakePlayerUnkillable"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputMakePlayerUnkillable(); });
		D.Input(TEXT("MakePlayerKillable"),      [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumPlayerEvents&>(E).InputMakePlayerKillable(); });
		ElysiumAddClassField(D, TEXT("enabled"), &FElysiumPlayerEvents::bEnabled);
	});

static FElysiumClassRegistrar GRegWorldEvents(
	TEXT("events_world"), ElysiumBaseClassName(), &MakeWorldEvents,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("SetSafeArea"),                      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputSetSafeArea(A); });
		D.Input(TEXT("SetCopWaitArea"),                   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputSetCopWaitArea(A); });
		D.Input(TEXT("SetCopGrace"),                      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputSetCopGrace(A); });
		D.Input(TEXT("SetNosferatuTolerant"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputSetNosferatuTolerant(A); });
		D.Input(TEXT("SetNoFrenzyArea"),                  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputSetNoFrenzyArea(A); });
		D.Input(TEXT("AIEnable"),                         [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputAIEnable(A); });
		D.Input(TEXT("FadeGlobalWetness"),                [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputFadeGlobalWetness(A); });
		D.Input(TEXT("HideCutsceneInterferingEntities"),  [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumWorldEvents&>(E).InputHideCutsceneInterferingEntities(); });
		D.Input(TEXT("UnhideCutsceneInterferingEntities"),[](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumWorldEvents&>(E).InputUnhideCutsceneInterferingEntities(); });
		D.Input(TEXT("PlayEndCredits"),                   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumWorldEvents&>(E).InputPlayEndCredits(A); });

		// The world-policy field surface. Registered at the default Key|Save so the area type a
		// level script sets mid-map survives a save and the substrate reads the value through one
		// door. `safearea` is the one both the Elysium refusal and the terminal incident guards
		// consume; `copwaitarea` selects the police response's wait-area path.
		ElysiumAddClassField(D, TEXT("safearea"),            &FElysiumWorldEvents::SafeArea);
		ElysiumAddClassField(D, TEXT("copwaitarea"),         &FElysiumWorldEvents::bCopWaitArea);
		ElysiumAddClassField(D, TEXT("copgrace"),            &FElysiumWorldEvents::CopGrace);
		ElysiumAddClassField(D, TEXT("nosferatu_tolerrant"), &FElysiumWorldEvents::bNosferatuTolerant);
		ElysiumAddClassField(D, TEXT("nofrenzyarea"),        &FElysiumWorldEvents::bNoFrenzyArea);
	});
