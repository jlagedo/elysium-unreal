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
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumConversationPlace.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelBindings.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumRulebook.h"
#include "Tests/ElysiumNpcTestHooks.h"

DEFINE_LOG_CATEGORY(LogElysiumNpcEnt);

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
static TUniquePtr<FElysiumEntity> MakeHint()      { return MakeUnique<FElysiumHint>(); }
static TUniquePtr<FElysiumEntity> MakeConversationPlace() { return MakeUnique<FElysiumConversationPlace>(); }

static void BuildNpcClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddNpcFields(D);

	// `WillTalk` and `SetAnimation` are not here: they belong to CBaseCombatCharacter and
	// CBaseAnimating, and the chain walk (R2) reaches them — the NPC shares those two ancestors
	// with the player, as VtMB's chain does.
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

	// The two policy-level schedule commands. Both take a native schedule NAME
	// (`docs/vtmb/npc-ai/authored-control.md` -> "Direct schedule changes") and both land on one
	// handler, which distinguishes them by `Args.Input` in every diagnostic.
	//
	// Corpus provenance, because the two names are not evidenced the same way and the difference
	// matters for what a coverage report should expect. `ChangeSchedule` has five immediate script
	// sites, all of them on an NPC receiver (`vamputil` names `SCHED_VDOG_SNARL` and
	// `SCHED_VDOG_MADEFRIEND`; `hollywood` twice names the literal `-`). `StartSchedule` has eight
	// map wires and two script sites and EVERY one of them aims at an `aiscripted_schedule` entity,
	// never at an NPC — that entity's own input is registered in
	// `Substrate/ElysiumAiScriptedSchedule.cpp`. It is registered here as well because the recovered
	// material names both as CAI_BaseNPC-level commands and a receiver may distinguish identical
	// spellings (K1); nothing in the current corpus reaches this one.
	D.Input(TEXT("ChangeSchedule"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputNamedSchedule(Args); });
	D.Input(TEXT("StartSchedule"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputNamedSchedule(Args); });

	// The runtime folds the retail CAI_BaseNPC / CAI_BaseNPCTroika nodes into every registered
	// `npc_*` leaf; the registry's class walk still exposes their shared input surface.
	using FN = FElysiumNpc;
	// SetRelationship's store/writer is live above. Enemy assignment, senses and combat schedules
	// are intentionally absent from this talk/feed slice and remain visible in NPC diagnostics.
	// `TeleportToEntity` is CAI_BaseNPCTroika's recovered FIELD_EHANDLE input. The current corpus
	// carries 40 wires across three NPC families; all use the same shared leaf implementation here.
	D.Input(TEXT("TeleportToEntity"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputTeleportToEntity(Args); });
	// `CAI_BaseNPCTroika::InputDisableThink` `0x1029f2a0` -> `SetDisableAI` `0x1029f300`.
	D.Input(TEXT("DisableThink"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputDisableThink(Args); });

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

	ElysiumAddClassField(D, TEXT("stattemplate"),    &FElysiumNpc::StatTemplate);
	// `interesting_place_groups` -> `m_sInterestingPlaceGroups +0x62d8`, parsed into
	// `m_iInterestingPlaceGroups +0x62dc` on the write, as `0x10298910` is.
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::String;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::String(static_cast<const FElysiumNpc&>(E).InterestingPlaceGroups); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{ static_cast<FElysiumNpc&>(E).SetInterestingPlaceGroups(V.ToString()); };
		D.Fields.Add(FName(TEXT("interesting_place_groups")), MoveTemp(Acc));
	}
	// `hint_groups` -> `m_sHintGroups +0x62e0`, and its parse into `m_iHintGroups +0x62e4`. The
	// pair is one field and not two, because retail parses on the write (`0x102989e0` off the
	// KeyValue) rather than on the first read; an NPC whose key never arrives keeps the all-ones
	// default and admits every hint group, which is retail's answer for an unset list.
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Key | EElysiumField::Save);
		Acc.Type = EElysiumVariantType::String;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::String(static_cast<const FElysiumNpc&>(E).ScheduleHost.HintGroups); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{ static_cast<FElysiumNpc&>(E).SetHintGroups(V.ToString()); };
		D.Fields.Add(FName(TEXT("hint_groups")), MoveTemp(Acc));
	}
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

	// --- Cognition: the acquisition counter behind the lookaround chance ---
	// `m_iEnemySightings` (+0x60a8) is engine-written, never authored — the same Save-only posture
	// the two stance members take. It is saved because the chance it feeds is a per-character
	// history: a guard who has fought the player before looks around more often.
	ElysiumAddClassField(D, TEXT("m_iEnemySightings"), &FElysiumNpc::EnemySightings, EElysiumField::Save);
	// `floatfreq` -> `m_iFloatSoundFrequency` (`CBaseCombatCharacter +0x10e8`, `fieldType 0`): the
	// authored "the float sound plays 1 time in X" frequency both halves of slot 510 roll against
	// (`Substrate/ElysiumNpcKernelSounds.cpp`). 0 and 8 disable the hook.
	ElysiumAddClassField(D, TEXT("floatfreq"), &FElysiumNpc::FloatSoundFrequency, EElysiumField::Save);

	// --- Combat: the three authored loadout keyfields ---
	// `additionalequipment` (267 authored rows), `alternateequipment` (184) and `cantdropweapons`
	// (78). Save-flagged like the rest of the authored NPC tuning: a map may not rewrite them, but a
	// payload has to carry what the entity was authored with, because the resolved loadout is
	// derived from them on the first think. The resolution is `Substrate/ElysiumNpcLoadout.h`; what
	// each is read for (and which of the three is deliberately unread) is stated on the members.
	ElysiumAddClassField(D, TEXT("cantdropweapons"),     &FElysiumNpc::bCantDropWeapons,    EElysiumField::Save);
}

static void BuildInterestingPlaceClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddInterestingPlaceFields(D);

	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputToggle(Args); });
	// Not in the place's datamap, so not generated; the row stays by hand.
	ElysiumAddClassField(D, TEXT("testflags"),         &FElysiumInterestingPlace::TestFlags);
}

static void BuildNpcMakerClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddNpcMakerFields(D);

	D.Input(TEXT("Spawn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputSpawn(Args); });
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputToggle(Args); });
	// The maker is a Troika NPC in retail; its `DisableThink` is inherited onto every child.
	D.Input(TEXT("DisableThink"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisableThink(Args); });

	// The datamap's two save-only rows (no external name, so nothing to generate): the live-child
	// counter the spawner owns and its cached ground height.
	using FM = FElysiumNpcMaker;
	ElysiumAddClassField(D, TEXT("m_cLiveChildren"),   &FM::LiveChildren, EElysiumField::Save);
	ElysiumAddClassField(D, TEXT("m_flGround"),        &FM::CachedGroundZ, EElysiumField::Save);
}

// One shared leaf per living-NPC classname (a class-for-class registration, so the registry's exact
// case-folded Find resolves each). npc_VCamera is a camera control entity with no model — left as an
// inert record for now. The two maker classnames share the maker leaf.
//
// This is the registration of the classnames a map may spawn. The other half of "species are data"
// — which retail class each classname is, what it derives from, which vtable slots it overrides and
// with which body — is the class registry in `Substrate/ElysiumNpcKernelShape.cpp`
// (`ElysiumNpcKernelShape::Classes()` and `::Overrides()`, 77 classes and 2,344 override rows,
// generated from the kernel ledger). Neither is a subclass and neither ever becomes one.
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
			// `CPayphone` (`.?AVCPayphone@@` `0x10587930`) is a `CAI_BaseNPCTroika` subclass — the
			// class `CBasePlayer::StartPlayerDialog` `0x10178280` RTTI-casts its partner to before it
			// decides whether to create a camera at all (SC9/RC6). It is an NPC leaf here for the same
			// reason it is one in retail: the four shipped phones author `dialogname`,
			// `default_camera` and `WillTalk`, the opener takes their dialogue body session, and the
			// arm it runs instead of the camera is `StartGrappleAttack(this, npc, 5)`, whose state
			// lives on the combat character. Registering it retires the `npc_payphone` stub row
			// (`RegisterStub` answers null once an implementation owns the name). Its own
			// `EnterGrappleState` override (`CPayphone::vfunc379` `0x101aade0`, the receiver
			// animation) is not built.
			TEXT("npc_payphone"),
		};
		for (const TCHAR* Name : NpcClasses)
		{
			// CAI_BaseNPC's place in VtMB's chain: under CBaseCombatCharacter, which is under
			// CBaseAnimating. The sheet, the counters and the body all arrive through it.
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
		// `CNPCMaker_Zombie` (0018 story 2): the shared maker leaf plus the zombie's own three rows.
		// Its think is a seam (`FElysiumNpcMaker::Think`).
		FElysiumClassDesc& ZombieMaker = Reg.Register(TEXT("npc_maker_zombie"), ElysiumBaseClassName(),
			&MakeNpcMaker);
		BuildNpcMakerClass(ZombieMaker);
		ElysiumNpcKernelBindings::AddNpcMakerZombieFields(ZombieMaker);

		// `ai_hint` — the live `CAI_Hint` every hint-making `info_node*` row becomes
		// (`ElysiumNodeEntity::ApplyHintReplacement`).
		FElysiumHint::BuildClass(Reg.Register(FElysiumHint::ClassName(), ElysiumBaseClassName(),
			&MakeHint));
		FElysiumConversationPlace::BuildClass(Reg.Register(TEXT("intersting_place_conversation"),
			ElysiumBaseClassName(), &MakeConversationPlace));
	}
};

static FElysiumNpcRegistrar GElysiumNpcRegistrar;
