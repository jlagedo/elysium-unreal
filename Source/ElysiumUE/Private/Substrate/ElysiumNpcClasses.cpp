// The `npc_*` family's registration site: one shared leaf per living-NPC classname, the
// `intersting_place` node, and the two `npc_maker` classnames.
//
// The classes themselves live one to a file beside this one — `ElysiumInterestingPlace`,
// `ElysiumScriptedCharacter`, `ElysiumNpc` (with the scene-owned player duplicate) and
// `ElysiumNpcMaker`. What remains here is the registry wiring: the factories, the input/field
// tables the class chain exposes, and the one static registrar that installs them.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumRulebook.h"
#include "Tests/ElysiumNpcTestHooks.h"

#include <type_traits>

DEFINE_LOG_CATEGORY(LogElysiumNpcEnt);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSignField / AddLogicField — file-unique name so all of them
	// can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddNpcField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
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
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddNpcField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
bool ElysiumNpcTestHooks::ApplyResolvedTemplate(FElysiumEntity& Entity,
	const FElysiumClanTemplate& Resolved)
{
	if (!Entity.Def || !Entity.Def->Classname.StartsWith(TEXT("npc_V"), ESearchCase::IgnoreCase)
		|| !Entity.AsCombatCharacter())
	{
		return false;
	}
	static_cast<FElysiumNpc&>(Entity).ApplyResolvedTemplate(Resolved, /*Table*/ nullptr);
	return true;
}
#endif

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeNpc()       { return MakeUnique<FElysiumNpc>(); }
static TUniquePtr<FElysiumEntity> MakeController(){ return MakeUnique<FElysiumPlayerControllerNpc>(); }
static TUniquePtr<FElysiumEntity> MakeNpcMaker()  { return MakeUnique<FElysiumNpcMaker>(); }
static TUniquePtr<FElysiumEntity> MakeInterestingPlace() { return MakeUnique<FElysiumInterestingPlace>(); }

static void BuildNpcClass(FElysiumClassDesc& D)
{
	// `WillTalk` and `SetAnimation` are not here: they belong to CBaseCombatCharacter and
	// CBaseAnimating, and the chain walk (R2) reaches them — which is the point of 11.4 giving the
	// NPC the same two ancestors VtMB gives it.
	D.Input(TEXT("UseInteresting"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputUseInteresting(Args); });
	D.Input(TEXT("StartPlayerDialogRemote"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialogRemote(Args); });
	D.Input(TEXT("StartPlayerDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialog(Args); });
	D.Input(TEXT("StartPlayerDialogUnforced"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialogUnforced(Args); });
	D.Input(TEXT("EndDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputEndDialog(Args); });
	D.Input(TEXT("SetupPatrolType"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetupPatrolType(Args); });
	D.Input(TEXT("FollowPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputFollowPatrolPath(Args); });
	D.Input(TEXT("ClearPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputClearPatrolPath(Args); });
	D.Input(TEXT("SetRelationship"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetRelationship(Args); });

	// The runtime folds the retail CAI_BaseNPC / CAI_BaseNPCTroika nodes into every registered
	// `npc_*` leaf; the registry's class walk still exposes their shared input surface.
	using FN = FElysiumNpc;
	// SetRelationship's store/writer is live above. Enemy assignment, senses and combat schedules
	// are intentionally absent from this talk/feed slice and remain visible in NPC diagnostics.
	// `TeleportToEntity` is CAI_BaseNPCTroika's recovered FIELD_EHANDLE input. The current corpus
	// carries 40 wires across three NPC families; all use the same shared leaf implementation here.
	D.Input(TEXT("TeleportToEntity"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputTeleportToEntity(Args); });

	// The remaining map-fired gap is 8 `SetScriptedDiscipline` wires across the exported maps, all
	// of them aimed at an `npc_*` receiver.
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, SetScriptedDiscipline, "P13 — disciplines");

	// `TakeDamage` — 4 map wires, plus the same name as a Character method the script surface
	// dispatches (K1: two bindings, one implementation). The wire's own datamap record is
	// unrecovered, so its argument's FIELD TYPE is a genuine unknown; what the corpus passes is a
	// number, and a number routes to the scalar fallback exactly as the script call does. A
	// parameter that is not numeric is refused and reported rather than turned into a plausible
	// default, because the marshalling contract is what is missing, not the receiver.
	D.Input(TEXT("TakeDamage"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumNpc& Npc = static_cast<FElysiumNpc&>(E);
			const float Amount = Args.Param.ToFloat();
			if (Amount <= 0.f)
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s TakeDamage '%s' is not a positive number — refused (the recovered "
						"datamap record does not name this input's field type)"),
					*Npc.DebugString(), *Args.Param.Describe());
				return;
			}
			Npc.TakeDamage(Amount);
		});

	AddNpcField(D, TEXT("use_interesting"), &FElysiumNpc::bUseInteresting);
	AddNpcField(D, TEXT("allow_alert_lookaround"), &FElysiumNpc::bAllowAlertLookaround);
	AddNpcField(D, TEXT("default_camera"), &FElysiumNpc::DefaultCamera, EElysiumField::Key);
	AddNpcField(D, TEXT("player_reaction"), &FElysiumNpc::PlayerReaction, EElysiumField::Key);
	AddNpcField(D, TEXT("stattemplate"),    &FElysiumNpc::StatTemplate);
	AddNpcField(D, TEXT("interesting_place_groups"), &FElysiumNpc::InterestingPlaceGroups);
	// times_talked: santamonica/chinatown/e3/demo read `npc.times_talked` to branch first-vs-repeat
	// dialogue. Register it read-only (engine-written, script-read) so the read resolves to a defined
	// value instead of raising AttributeError. B4's dialogue runner drives the count; it stays 0 until then.
	AddNpcField(D, TEXT("times_talked"), &FElysiumNpc::TimesTalked, EElysiumField::Save);

	// The stance pair, under retail's own datamap names. Save-only: they carry flag `0x2` there, so
	// they persist and no keyvalue or script writes them. Nothing in the recovered writer set resets
	// either on a schedule, state, dialogue or disposition change, which is why a character keeps its
	// stance across a conversation.
	//
	// The index is clamped on restore rather than trusted: it addresses a three-slot array, and a
	// payload written by another build must not be able to index past it.
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Save);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::Int(static_cast<const FElysiumNpc&>(E).Stance.Current); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{
			static_cast<FElysiumNpc&>(E).Stance.Current =
				FMath::Clamp(V.ToInt(), 0, ElysiumStance::Count - 1);
		};
		D.Fields.Add(FName(TEXT("m_CurrStance")), MoveTemp(Acc));
	}
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Save);
		Acc.Type = EElysiumVariantType::Float;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::Float(static_cast<const FElysiumNpc&>(E).Stance.LastChangeTime); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{ static_cast<FElysiumNpc&>(E).Stance.LastChangeTime = V.ToFloat(); };
		D.Fields.Add(FName(TEXT("m_flStanceTime")), MoveTemp(Acc));
	}
}

static void BuildInterestingPlaceClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputToggle(Args); });
	AddNpcField(D, TEXT("type"),              &FElysiumInterestingPlace::Type);
	AddNpcField(D, TEXT("enabled"),           &FElysiumInterestingPlace::bEnabled);
	AddNpcField(D, TEXT("max_npcs"),          &FElysiumInterestingPlace::MaxNpcs);
	AddNpcField(D, TEXT("group_id"),          &FElysiumInterestingPlace::GroupId);
	AddNpcField(D, TEXT("rating"),            &FElysiumInterestingPlace::Rating);
	AddNpcField(D, TEXT("testflags"),         &FElysiumInterestingPlace::TestFlags);
	AddNpcField(D, TEXT("match_orientation"), &FElysiumInterestingPlace::bMatchOrientation);
	AddNpcField(D, TEXT("min_time"),          &FElysiumInterestingPlace::MinTime);
	AddNpcField(D, TEXT("max_time"),          &FElysiumInterestingPlace::MaxTime);
}

static void BuildNpcMakerClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Spawn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputSpawn(Args); });
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputToggle(Args); });

	using FM = FElysiumNpcMaker;
	AddNpcField(D, TEXT("NPCType"),           &FM::NpcType);
	AddNpcField(D, TEXT("MaxNPCCount"),       &FM::RemainingTotal);
	AddNpcField(D, TEXT("SpawnFrequency"),    &FM::SpawnFrequency);
	AddNpcField(D, TEXT("m_cLiveChildren"),   &FM::LiveChildren, EElysiumField::Save);
	AddNpcField(D, TEXT("MaxLiveChildren"),   &FM::MaxLiveChildren);
	AddNpcField(D, TEXT("m_flGround"),        &FM::CachedGroundZ, EElysiumField::Save);
	AddNpcField(D, TEXT("NPCTargetname"),     &FM::ChildTargetName);
	AddNpcField(D, TEXT("Flag_StartDisabled"),&FM::bDisabled);
	AddNpcField(D, TEXT("Flag_NPCClip"),      &FM::bNpcClip);
	AddNpcField(D, TEXT("Flag_Fade"),         &FM::bFade);
	AddNpcField(D, TEXT("Flag_InfChild"),     &FM::bInfinite);
	AddNpcField(D, TEXT("Flag_NoDrop"),       &FM::bNoDrop);
	AddNpcField(D, TEXT("Flag_ViewCone"),     &FM::bViewCone);
	AddNpcField(D, TEXT("MinPCDistance"),      &FM::MinPcDistance);
}

// One shared leaf per living-NPC classname (a class-for-class registration, so the registry's exact
// case-folded Find resolves each). npc_VCamera is a camera control entity with no model — left as an
// inert record for now. The two maker classnames share the maker leaf.
struct FElysiumNpcRegistrar
{
	FElysiumNpcRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		BuildInterestingPlaceClass(Reg.Register(TEXT("intersting_place"),
			ElysiumBaseClassName(), &MakeInterestingPlace));

		static const TCHAR* const NpcClasses[] = {
			TEXT("npc_VVampire"), TEXT("npc_VPedestrian"), TEXT("npc_VHumanCombatant"),
			TEXT("npc_VRat"), TEXT("npc_VDialogPedestrian"), TEXT("npc_VCop"),
			TEXT("npc_VTaxiDriver"), TEXT("npc_VHuman"), TEXT("npc_VHunter"),
			TEXT("npc_VTzimisceRunner"), TEXT("npc_VNewscaster"), TEXT("npc_VAnimal"),
			TEXT("npc_VSabbatLeader"), TEXT("npc_VAndreiBlood"),
		};
		for (const TCHAR* Name : NpcClasses)
		{
			// CAI_BaseNPC's place in VtMB's chain: under CBaseCombatCharacter, which is under
			// CBaseAnimating (11.4). The sheet, the counters and the body all arrive through it.
			BuildNpcClass(Reg.Register(FName(Name), ElysiumCombatCharacterClassName(), &MakeNpc));
		}

		// Created only at runtime by events_player.CreateControllerNPC. It intentionally does not
		// receive FElysiumNpc's dialogue surface or any AI behavior.
		Reg.Register(TEXT("npc_VPlayerController"), ElysiumCombatCharacterClassName(), &MakeController);

		static const TCHAR* const MakerClasses[] = { TEXT("npc_maker"), TEXT("npc_maker_fleshpile") };
		for (const TCHAR* Name : MakerClasses)
		{
			BuildNpcMakerClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeNpcMaker));
		}
	}
};

static FElysiumNpcRegistrar GElysiumNpcRegistrar;
