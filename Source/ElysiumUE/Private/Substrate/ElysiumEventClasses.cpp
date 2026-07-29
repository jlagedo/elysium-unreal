// P4.9 — the two singleton event-bus entities every VtMB map carries: `events_player` (the
// player's event surface, targetnames pc_0 / pcevents / controller) and `events_world` (the
// world-policy + world-event surface, targetname `world`). They are the map's data bus: level
// scripts hang their world-event callbacks off `events_world`'s outputs, and the discipline /
// frenzy / morph outputs off `events_player`. Both are plain-C++ FElysiumEntity leaves (R1)
// registered by a module-static FElysiumClassRegistrar, inheriting the base
// Kill/ScriptHide/ScriptUnhide + keyfields (and OnUseBegin/OnUseEnd) through the class chain (R2).
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
// Scope: the input/field surface is complete and faithful. The *outputs* are fired by systems
// that do not exist yet (disciplines, frenzy, wolf morph, the cop/masquerade AI, the music state
// machine), so this task lands the bus itself — inputs deliver, policy state is recorded and
// inspectable, and every output wire resolves — not the systems that will drive it. Each input
// that fronts an unbuilt system records its state and logs; none of them silently no-op.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEvents, Log, All);

namespace
{
	// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only takes base
	// FElysiumEntity members). Mirrors AddLogicField / AddSubclassField / AddDoorSubclassField —
	// file-unique name so all of them can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddEventField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
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
			static_assert(sizeof(TMember) == 0, "AddEventField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	const TCHAR* OnOff(bool b) { return b ? TEXT("on") : TEXT("off"); }
}

// ============================================================================================
// events_player — CPlayerEvents. 12 inputs, 23 outputs, one keyfield (`enabled`). The tutorial
// carries three instances: `pc_0` (the map-load MakePlayerUnkillable target and the discipline
// output source), `pcevents` (Jack's OnDamaged -> ClearDialogCombatTimers), and `controller`
// (sp_observatory_1 fires ImmobilizePlayer at its own copy).
//
// EnableOutputs/DisableOutputs gate the whole output surface — modelled here as the real gate
// (FireEvent early-outs), so a disabled bus stays silent exactly as retail's does.
// ============================================================================================

class FElysiumPlayerEvents final : public FElysiumEntity
{
public:
	// `enabled` keyfield (m_bEnabled @0x450). Every exported instance sets it to 1.
	bool bEnabled = true;

	// Player-policy state these inputs latch. The systems that read them are later phases
	// (damage/RPG 9.4, NPC 8.5), so the bus records the intent and reports it in the inspector.
	bool bUnkillable       = false;   // MakePlayerUnkillable / MakePlayerKillable
	bool bImmobilized      = false;   // ImmobilizePlayer / MobilizePlayer
	int32 DisciplineClears = 0;       // RemoveDisciplines(+Now) call count
	int32 DialogTimerClears= 0;       // ClearDialogCombatTimers call count
	FString LastAwardExp;             // AwardExp <experience_table key>

	void InputEnableOutputs()  { bEnabled = true; }
	void InputDisableOutputs() { bEnabled = false; }

	// The latch is the damage system's gate, and since 11.4 the damage system is the player entity's
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

	void InputImmobilizePlayer() { bImmobilized = true;  Note(TEXT("ImmobilizePlayer")); }
	void InputMobilizePlayer()   { bImmobilized = false; Note(TEXT("MobilizePlayer")); }

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

	void InputClearDialogCombatTimers() { ++DialogTimerClears; }

	void InputAwardExp(const FElysiumInputArgs& A)
	{
		LastAwardExp = A.Param.ToString();
		// experience_table.txt lookup + XP award is 9.4; the key is recorded so the wire is traceable.
		UE_LOG(LogElysiumEvents, Log, TEXT("%s AwardExp '%s' (XP award is 9.4)"),
			*DebugString(), *LastAwardExp);
	}

	// The one output chokepoint: honours the EnableOutputs/DisableOutputs gate. The discipline /
	// frenzy / morph events that drive these live in later phases; this is the seam they call.
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

// ============================================================================================
// events_world — CWorldEvents. 10 inputs, 21 outputs (+ the base OnUseBegin/OnUseEnd). One
// instance per map, targetname `world`. Its inputs are world *policy* (safe area, cop grace,
// frenzy suppression, AI enable, wetness) — all of them read by systems that land later, so the
// faithful minimal implementation is to hold them accurately and expose them.
//
// The tutorial sets SetNoFrenzyArea 1 at OnMapLoad; worldspawn also carries `copwaitarea` and
// `nosferatu_tolerrant` keys that mirror two of these, so the entity is the runtime override.
// ============================================================================================

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
	float GlobalWetness     = 0.0f;   // FadeGlobalWetness (float target)
	bool  bCutsceneHidden   = false;  // Hide/UnhideCutsceneInterferingEntities

	void InputSetSafeArea(const FElysiumInputArgs& A)           { SafeArea = A.Param.ToInt(); }
	void InputSetCopWaitArea(const FElysiumInputArgs& A)        { bCopWaitArea = A.Param.ToInt() != 0; }
	void InputSetCopGrace(const FElysiumInputArgs& A)           { CopGrace = A.Param.ToFloat(); }
	void InputSetNosferatuTolerant(const FElysiumInputArgs& A)  { bNosferatuTolerant = A.Param.ToInt() != 0; }
	void InputSetNoFrenzyArea(const FElysiumInputArgs& A)       { bNoFrenzyArea = A.Param.ToInt() != 0; }
	void InputAIEnable(const FElysiumInputArgs& A)              { bAIEnabled = A.Param.ToInt() != 0; }
	void InputFadeGlobalWetness(const FElysiumInputArgs& A)     { GlobalWetness = A.Param.ToFloat(); }

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
		Out.Emplace(TEXT("Global wetness"),    FString::SanitizeFloat(GlobalWetness));
		Out.Emplace(TEXT("Cutscene hide"),     OnOff(bCutsceneHidden));
	}
};

// ============================================================================================
// Registration
// ============================================================================================

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
		AddEventField(D, TEXT("enabled"), &FElysiumPlayerEvents::bEnabled);
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
	});
