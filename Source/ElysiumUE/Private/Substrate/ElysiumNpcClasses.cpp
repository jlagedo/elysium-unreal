// The `npc_*` family's registration site: the ordinary NPC classes, one per retail class, the
// `intersting_place` node, and the three `npc_maker` classnames.
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
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcBaseBoss.h"
#include "Substrate/ElysiumNpcBrujah.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcCameraSecurity.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcChangBrosBlade.h"
#include "Substrate/ElysiumNpcChangBrosClaw.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatPatrol.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcLasombra.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcPlaceholder.h"
#include "Substrate/ElysiumNpcPlayerController.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumAiScriptedSequence.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumNpcProneDialog.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcSabbatGunman.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampire.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWolfMorph.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumNpcKernelBindings.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerFleshpile.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
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

static TUniquePtr<FElysiumEntity> MakeInterestingPlace() { return MakeUnique<FElysiumInterestingPlace>(); }
static TUniquePtr<FElysiumEntity> MakeHint()      { return MakeUnique<FElysiumHint>(); }
static TUniquePtr<FElysiumEntity> MakeConversationPlace() { return MakeUnique<FElysiumConversationPlace>(); }

// `CAI_BaseNPC`'s own surface (story 5 step 5): its datamap's keyed fields and SAVE walk, on the
// base's abstract descriptor, so the Troika line and every class beneath it inherit them.
static void BuildNpcBaseClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddNpcBaseFields(D);
	ElysiumNpcKernelBindings::AddNpcBaseSaveFields(D);

	// The remaining map-fired gap is 8 `SetScriptedDiscipline` wires across the exported maps, all
	// of them aimed at an `npc_*` receiver.
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FElysiumNpcBase, SetScriptedDiscipline, "P13 — disciplines");
}

static void BuildNpcClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddNpcFields(D);
	// Retail's `SAVE`-only rows, under their retail member names (0019 story 2 pass B). No
	// keyvalue, input or Python attribute reaches any of them -- `ReadKeyField`'s gate is
	// `KEY|OUTPUT` and `AcceptInput`'s is `INPUT` -- so they are persistence and nothing else,
	// and the registry's save walk is what carries them.
	ElysiumNpcKernelBindings::AddNpcSaveFields(D);

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
	// carries 40 wires across three NPC families; all reach the one `CAI_BaseNPCTroika` input.
	D.Input(TEXT("TeleportToEntity"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputTeleportToEntity(Args); });
	// `CAI_BaseNPCTroika::InputDisableThink` `0x1029f2a0` -> `SetDisableAI` `0x1029f300`.
	D.Input(TEXT("DisableThink"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputDisableThink(Args); });
	// `CAI_BaseNPCTroika::InputTweakParam` (`0x1029ea40`) -> slot 585 `ProcessTweakParam`. Until
	// story 5 step 2 only the `npc_VCamera` stub row answered this name.
	D.Input(TEXT("TweakParam"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputTweakParam(Args); });

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
	// The stance pair (`m_CurrStance` +0x64c8, `m_flStanceTime` +0x64e4) and the acquisition
	// counter (`m_iEnemySightings` +0x60a8) were hand-written save rows until 0019 story 2 pass B;
	// `AddNpcSaveFields` now emits all three off the replay, with the two stance LATCHES beside
	// them that retail's datamap also carries. The restore-time clamp the stance index had here is
	// gone with them: `FElysiumStanceState::Current` addresses a three-slot array, and the
	// selector is what validates it, the same way retail's reader does.
	// `floatfreq` -> `m_iFloatSoundFrequency` (`CBaseCombatCharacter +0x10e8`) is the combat
	// character's own row (`AddCombatCharacterFields`, story 5 step 5).

	// --- Combat: the three authored loadout keyfields ---
	// `additionalequipment` (267 authored rows), `alternateequipment` (184) and `cantdropweapons`
	// (78). Save-flagged like the rest of the authored NPC tuning: a map may not rewrite them, but a
	// payload has to carry what the entity was authored with, because the resolved loadout is
	// derived from them on the first think. The resolution is `Substrate/ElysiumNpcLoadout.h`; what
	// each is read for (and which of the three is deliberately unread) is stated on the members.
	ElysiumAddClassField(D, TEXT("cantdropweapons"),     &FElysiumCombatCharacter::bCantDropWeapons,    EElysiumField::Save);
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

// The ordinary NPC classes (story 5 step 2). Each classname registers the C++ class retail's
// factory builds for it (`kernel_factories.tsv`, replayed from the 74 factories); two retail
// classes carry two classnames each (`CNPC_VPedestrian`, `CNPC_ProneDialog`). Above the classnames
// stand the retail classes as abstract descriptors, chained as retail derives them, so a row a
// class declares is reached by exactly its descendants. `CAI_BaseNPC` stands under the combat
// character and `CAI_BaseNPCTroika` beneath it (story 5 step 5); the NPC surface `BuildNpcClass`
// registers is split the same way: `BuildNpcBaseClass` for the base's rows, `BuildNpcClass` for the
// Troika's.
//
// The census (`Substrate/ElysiumNpcKernelShape.cpp`) is the same tree read as data; each species
// class answers its own row (`FElysiumNpc::OwnRetailClass`).
template <typename T>
static TUniquePtr<FElysiumEntity> MakeNpcOf()
{
	return MakeUnique<T>();
}

// The introduced species' own datamap INPUTs (story 5 step 4). None has a ported handler yet, so
// each is an explicit pending seam on the class that declares it: a wire to it is a reported
// work-list row naming the retail handler, never an unknown-input diagnostic. The handlers are
// story-8 residue; the addresses are the corpus's names for them.
static void AddSpeciesPendingInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	const auto Is = [RetailClass](const TCHAR* Class) { return FCString::Strcmp(RetailClass, Class) == 0; };
	if (Is(TEXT("CNPC_VAndreiBlood")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VAndreiBlood", FElysiumNpc, TriggerCombat,
			"0019 story 8 — CNPC_VAndreiBlood::InputTriggerCombat 0x1035dd00");
	}
	else if (Is(TEXT("CNPC_VBach")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VBach", FElysiumNpc, GrenadeEnter,
			"0019 story 8 — CNPC_VBach::InputGrenadeEnter 0x10366120");
		ELYSIUM_PENDING_INPUT_ON("CNPC_VBach", FElysiumNpc, GrenadeExit,
			"0019 story 8 — CNPC_VBach::InputGrenadeExit 0x10366190");
		ELYSIUM_PENDING_INPUT_ON("CNPC_VBach", FElysiumNpc, SignalVulnerable,
			"0019 story 8 — CNPC_VBach::InputSignalVulnerable 0x103661d0");
	}
	else if (Is(TEXT("CNPC_VHengeyokai")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VHengeyokai", FElysiumNpc, StartTransformation,
			"0019 story 8 — CNPC_VHengeyokai::InputStartTransformation 0x10383170");
	}
	else if (Is(TEXT("CNPC_VLasombra")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VLasombra", FElysiumNpc, DisableCover,
			"0019 story 8 — CNPC_VLasombra::InputDisableCover 0x103893f0");
	}
	else if (Is(TEXT("CNPC_VManBat")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VManBat", FElysiumNpc, ManBatStun,
			"0019 story 8 — CNPC_VManBat::InputManBatStun 0x1038fa50");
		ELYSIUM_PENDING_INPUT_ON("CNPC_VManBat", FElysiumNpc, ManBatFlyBegin,
			"0019 story 8 — CNPC_VManBat::InputManBatFlyBegin 0x1038fa90");
	}
	else if (Is(TEXT("CNPC_VMingXiao")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VMingXiao", FElysiumNpc, StartTransformation,
			"0019 story 8 — CNPC_VMingXiao::InputStartTransformation 0x1039a700");
	}
	else if (Is(TEXT("CNPC_VSabbatLeader")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VSabbatLeader", FElysiumNpc, StartTransformation,
			"0019 story 8 — CNPC_VSabbatLeader::StartTransformation 0x103aa3b0");
	}
	else if (Is(TEXT("CNPC_VSheriffMan")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VSheriffMan", FElysiumNpc, StartAttacking,
			"0019 story 8 — CNPC_VSheriffMan::StartAttacking 0x103b1470");
		ELYSIUM_PENDING_INPUT_ON("CNPC_VSheriffMan", FElysiumNpc, StartTransformation,
			"0019 story 8 — CNPC_VSheriffMan::StartTransformation 0x103b15e0");
	}
	else if (Is(TEXT("CNPC_VVampireBoss")))
	{
		// `CNPC_VVampireBoss::InputTransformModel` (datamap INPUT), the protean swap's trigger.
		// Its only stand-in was the `npc_VVampireBoss` stub row this class supersedes.
		ELYSIUM_PENDING_INPUT_ON("CNPC_VVampireBoss", FElysiumNpc, TransformModel,
			"0019 story 8 — the vampire boss's protean swap");
	}
	else if (Is(TEXT("CNPC_VWerewolf")))
	{
		ELYSIUM_PENDING_INPUT_ON("CNPC_VWerewolf", FElysiumNpc, ToggleDoorState,
			"0019 story 8 — CNPC_VWerewolf::InputToggleDoorState 0x103d99a0");
	}
	else if (Is(TEXT("CNPC_VZombie")))
	{
		// `0x103e0e30` parses the name (`0x103e0cd0`) and calls `SetZombieAIType` (`0x103e0980`,
		// ported as `FElysiumNpc::FUN_103e0980`); the parser is not, so the wire stays pending.
		ELYSIUM_PENDING_INPUT_ON("CNPC_VZombie", FElysiumNpc, SetZombieAIType,
			"0019 story 8 — CNPC_VZombie::InputSetZombieAIType 0x103e0e30");
	}
}

// The species whose own slot 77 / 78 body replaces the base entity's `ScriptHide` / `ScriptUnhide`
// input. `FElysiumEntity::ScriptHide/ScriptUnhide` are not virtual, so the input (and the Python
// `ent.ScriptHide()` that reaches it through the same registry row) is routed to the species body
// here, on the class descriptor that declares it, as the hint's are (`ElysiumHint.cpp`).
static void AddSpeciesSlotInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	if (FCString::Strcmp(RetailClass, TEXT("CNPC_VGhoulCroucher")) == 0)
	{
		// Slot 77 `0x1037c1c0`.
		D.Input(TEXT("ScriptHide"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumNpcGhoulCroucher&>(E).GhoulCroucherScriptHide(); });
		// Slot 78 `0x1037c2f0`: `CAI_BaseNPCTroika::ScriptUnhide` DIRECT (`0x1037c359` -> `0x102c1ec0`,
		// unported: the entity base it ends in stands for it, as for slot 77), then the particle's.
		D.Input(TEXT("ScriptUnhide"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{
				E.ScriptUnhide();
				static_cast<FElysiumNpcGhoulCroucher&>(E).GhoulCroucherScriptUnhideTail();
			});
	}
}

struct FElysiumNpcRetailClassRow
{
	const TCHAR* RetailClass;
	const TCHAR* RetailBase;
};

static const FElysiumNpcRetailClassRow GNpcRetailClasses[] =
{
	// `FElysiumNpcTestHull`: no factory builds it by classname; retail builds it by code, and so does
	// a test here (`FElysiumEntityDef::InternalFactory`), against this abstract descriptor.
	{ TEXT("CAI_TestHull"), TEXT("CAI_BaseNPC") },
	// The script directors (story 5 fold A3): `CCineNPC` under `CAI_BaseNPC`, its two twins under it.
	{ TEXT("CCineAI"), TEXT("CCineNPC") },
	{ TEXT("CCineAISchedule"), TEXT("CCineNPC") },
	{ TEXT("CCineNPC"), TEXT("CAI_BaseNPC") },
	// The makers (story 5 fold A4): `CNPCMaker` IS a Troika NPC; its two variants derive from it.
	{ TEXT("CNPCMaker"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPCMaker_Fleshpile"), TEXT("CNPCMaker") },
	{ TEXT("CNPCMaker_Zombie"), TEXT("CNPCMaker") },
	{ TEXT("CNPC_ProneDialog"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VAndreiBlood"), TEXT("CNPC_VVampireBoss") },
	{ TEXT("CNPC_VAnimal"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VAsianVampire"), TEXT("CNPC_VVampireBoss") },
	{ TEXT("CNPC_VBach"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VBaseBoss"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VBrujah"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VCamera"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VCameraSecurity"), TEXT("CNPC_VCamera") },
	{ TEXT("CNPC_VChangBros"), TEXT("CNPC_VVampireBoss") },
	{ TEXT("CNPC_VChangBrosBlade"), TEXT("CNPC_VChangBros") },
	{ TEXT("CNPC_VChangBrosClaw"), TEXT("CNPC_VChangBros") },
	{ TEXT("CNPC_VCop"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VDog"), TEXT("CNPC_VAnimal") },
	{ TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VPlayerController") },
	{ TEXT("CNPC_VGargoyle"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VGhoulCroucher"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VGuard1"), TEXT("CNPC_VHuman") },
	{ TEXT("CNPC_VHengeyokai"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VHuman"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VHumanCombatant"), TEXT("CNPC_VHuman") },
	{ TEXT("CNPC_VHumanCombatPatrol"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VHunter"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VLasombra"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VManBat"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VMingXiao"), TEXT("CNPC_VBaseBoss") },
	{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VNewscaster"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VPedestrian"), TEXT("CNPC_VHuman") },
	{ TEXT("CNPC_VPlaceholder"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VPlayerController"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VRat"), TEXT("CNPC_VScurrying") },
	{ TEXT("CNPC_VSabbatGunman"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VSabbatLeader"), TEXT("CNPC_VVampireBoss") },
	{ TEXT("CNPC_VScurrying"), TEXT("CNPC_VAnimal") },
	{ TEXT("CNPC_VSheriffMan"), TEXT("CNPC_VVampireBoss") },
	{ TEXT("CNPC_VTaxiDriver"), TEXT("CNPC_VHuman") },
	{ TEXT("CNPC_VTzimisce"), TEXT("CNPC_VBaseBoss") },
	{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("CNPC_VBaseBoss") },
	{ TEXT("CNPC_VTzimisceRunner"), TEXT("CNPC_VBaseBoss") },
	{ TEXT("CNPC_VVampire"), TEXT("CNPC_VHuman") },
	{ TEXT("CNPC_VVampireBoss"), TEXT("CNPC_VVampire") },
	{ TEXT("CNPC_VWerewolf"), TEXT("CNPC_VBaseBoss") },
	{ TEXT("CNPC_VWolfMorph"), TEXT("CNPC_VPlayerController") },
	{ TEXT("CNPC_VYukie"), TEXT("CNPC_VHumanCombatant") },
	{ TEXT("CNPC_VZombie"), TEXT("CNPC_VAnimal") },
	{ TEXT("CPayphone"), TEXT("CAI_BaseNPCTroika") },
};

struct FElysiumNpcClassnameRow
{
	const TCHAR* Classname;
	const TCHAR* RetailClass;
	FElysiumEntityFactory Factory;
};

static const FElysiumNpcClassnameRow GNpcClassnames[] =
{
	// The script directors (story 5 fold A3), each classname its own class: `0x101a96b0`
	// (`aiscripted_schedule`), `0x1000886e` (`aiscripted_sequence`), and `scripted_sequence`'s.
	{ TEXT("aiscripted_schedule"), TEXT("CCineAISchedule"), &MakeNpcOf<FElysiumAiScriptedSchedule> },
	{ TEXT("aiscripted_sequence"), TEXT("CCineAI"), &MakeNpcOf<FElysiumAiScriptedSequence> },
	// The makers (story 5 fold A4), each classname its own class.
	{ TEXT("npc_maker"), TEXT("CNPCMaker"), &MakeNpcOf<FElysiumNpcMaker> },
	{ TEXT("npc_maker_fleshpile"), TEXT("CNPCMaker_Fleshpile"), &MakeNpcOf<FElysiumNpcMakerFleshpile> },
	{ TEXT("npc_maker_zombie"), TEXT("CNPCMaker_Zombie"), &MakeNpcOf<FElysiumNpcMakerZombie> },
	{ TEXT("npc_payphone"), TEXT("CPayphone"), &MakeNpcOf<FElysiumNpcPayphone> },
	{ TEXT("npc_VAndreiBlood"), TEXT("CNPC_VAndreiBlood"), &MakeNpcOf<FElysiumNpcAndreiBlood> },
	{ TEXT("npc_VAnimal"), TEXT("CNPC_VAnimal"), &MakeNpcOf<FElysiumNpcAnimal> },
	{ TEXT("npc_VAsianVampire"), TEXT("CNPC_VAsianVampire"), &MakeNpcOf<FElysiumNpcAsianVampire> },
	{ TEXT("npc_VBach"), TEXT("CNPC_VBach"), &MakeNpcOf<FElysiumNpcBach> },
	{ TEXT("npc_VBrujah"), TEXT("CNPC_VBrujah"), &MakeNpcOf<FElysiumNpcBrujah> },
	{ TEXT("npc_VCamera"), TEXT("CNPC_VCamera"), &MakeNpcOf<FElysiumNpcCamera> },
	{ TEXT("npc_VCameraSecurity"), TEXT("CNPC_VCameraSecurity"),
		&MakeNpcOf<FElysiumNpcCameraSecurity> },
	{ TEXT("npc_VChangBros"), TEXT("CNPC_VChangBros"), &MakeNpcOf<FElysiumNpcChangBros> },
	{ TEXT("npc_VChangBrosBlade"), TEXT("CNPC_VChangBrosBlade"),
		&MakeNpcOf<FElysiumNpcChangBrosBlade> },
	{ TEXT("npc_VChangBrosClaw"), TEXT("CNPC_VChangBrosClaw"), &MakeNpcOf<FElysiumNpcChangBrosClaw> },
	{ TEXT("npc_VCop"), TEXT("CNPC_VCop"), &MakeNpcOf<FElysiumNpcCop> },
	{ TEXT("npc_VDialogPedestrian"), TEXT("CNPC_VPedestrian"), &MakeNpcOf<FElysiumNpcPedestrian> },
	{ TEXT("npc_VDog"), TEXT("CNPC_VDog"), &MakeNpcOf<FElysiumNpcDog> },
	{ TEXT("npc_VFrenzyShadow"), TEXT("CNPC_VFrenzyShadow"), &MakeNpcOf<FElysiumNpcFrenzyShadow> },
	{ TEXT("npc_VGargoyle"), TEXT("CNPC_VGargoyle"), &MakeNpcOf<FElysiumNpcGargoyle> },
	{ TEXT("npc_VGhoulCroucher"), TEXT("CNPC_VGhoulCroucher"), &MakeNpcOf<FElysiumNpcGhoulCroucher> },
	{ TEXT("npc_VGuard1"), TEXT("CNPC_VGuard1"), &MakeNpcOf<FElysiumNpcGuard1> },
	{ TEXT("npc_VHengeyokai"), TEXT("CNPC_VHengeyokai"), &MakeNpcOf<FElysiumNpcHengeyokai> },
	{ TEXT("npc_VHuman"), TEXT("CNPC_VHuman"), &MakeNpcOf<FElysiumNpcHuman> },
	{ TEXT("npc_VHumanCombatant"), TEXT("CNPC_VHumanCombatant"),
		&MakeNpcOf<FElysiumNpcHumanCombatant> },
	{ TEXT("npc_VHumanCombatPatrol"), TEXT("CNPC_VHumanCombatPatrol"),
		&MakeNpcOf<FElysiumNpcHumanCombatPatrol> },
	{ TEXT("npc_VHunter"), TEXT("CNPC_VHunter"), &MakeNpcOf<FElysiumNpcHunter> },
	{ TEXT("npc_VLasombra"), TEXT("CNPC_VLasombra"), &MakeNpcOf<FElysiumNpcLasombra> },
	{ TEXT("npc_VManBat"), TEXT("CNPC_VManBat"), &MakeNpcOf<FElysiumNpcManBat> },
	{ TEXT("npc_VMercurio"), TEXT("CNPC_ProneDialog"), &MakeNpcOf<FElysiumNpcProneDialog> },
	{ TEXT("npc_VMingXiao"), TEXT("CNPC_VMingXiao"), &MakeNpcOf<FElysiumNpcMingXiao> },
	{ TEXT("npc_VMingXiaoTentacle"), TEXT("CNPC_VMingXiaoTentacle"),
		&MakeNpcOf<FElysiumNpcMingXiaoTentacle> },
	{ TEXT("npc_VNewscaster"), TEXT("CNPC_VNewscaster"), &MakeNpcOf<FElysiumNpcNewscaster> },
	{ TEXT("npc_VPedestrian"), TEXT("CNPC_VPedestrian"), &MakeNpcOf<FElysiumNpcPedestrian> },
	{ TEXT("npc_VPlaceholder"), TEXT("CNPC_VPlaceholder"), &MakeNpcOf<FElysiumNpcPlaceholder> },
	// The player's scene stand-in: `events_player.CreateControllerNPC` builds it through this factory
	// (`FElysiumEntityWorld::CreatePlayerControllerEntity`, retail `GetControllerNPC` `0x10161a70`).
	{ TEXT("npc_VPlayerController"), TEXT("CNPC_VPlayerController"),
		&MakeNpcOf<FElysiumNpcPlayerController> },
	{ TEXT("npc_VProneDialog"), TEXT("CNPC_ProneDialog"), &MakeNpcOf<FElysiumNpcProneDialog> },
	{ TEXT("npc_VRat"), TEXT("CNPC_VRat"), &MakeNpcOf<FElysiumNpcRat> },
	{ TEXT("npc_VSabbatGunman"), TEXT("CNPC_VSabbatGunman"), &MakeNpcOf<FElysiumNpcSabbatGunman> },
	{ TEXT("npc_VSabbatLeader"), TEXT("CNPC_VSabbatLeader"), &MakeNpcOf<FElysiumNpcSabbatLeader> },
	{ TEXT("npc_VScurrying"), TEXT("CNPC_VScurrying"), &MakeNpcOf<FElysiumNpcScurrying> },
	{ TEXT("npc_VSheriffMan"), TEXT("CNPC_VSheriffMan"), &MakeNpcOf<FElysiumNpcSheriffMan> },
	{ TEXT("npc_VTaxiDriver"), TEXT("CNPC_VTaxiDriver"), &MakeNpcOf<FElysiumNpcTaxiDriver> },
	{ TEXT("npc_VTzimisce"), TEXT("CNPC_VTzimisce"), &MakeNpcOf<FElysiumNpcTzimisce> },
	{ TEXT("npc_VTzimisceHeadClaw"), TEXT("CNPC_VTzimisceHeadClaw"),
		&MakeNpcOf<FElysiumNpcTzimisceHeadClaw> },
	{ TEXT("npc_VTzimisceRunner"), TEXT("CNPC_VTzimisceRunner"),
		&MakeNpcOf<FElysiumNpcTzimisceRunner> },
	{ TEXT("npc_VVampire"), TEXT("CNPC_VVampire"), &MakeNpcOf<FElysiumNpcVampire> },
	{ TEXT("npc_VVampireBoss"), TEXT("CNPC_VVampireBoss"), &MakeNpcOf<FElysiumNpcVampireBoss> },
	{ TEXT("npc_VWerewolf"), TEXT("CNPC_VWerewolf"), &MakeNpcOf<FElysiumNpcWerewolf> },
	{ TEXT("npc_VWolfMorph"), TEXT("CNPC_VWolfMorph"), &MakeNpcOf<FElysiumNpcWolfMorph> },
	{ TEXT("npc_VYukie"), TEXT("CNPC_VYukie"), &MakeNpcOf<FElysiumNpcYukie> },
	{ TEXT("npc_VZombie"), TEXT("CNPC_VZombie"), &MakeNpcOf<FElysiumNpcZombie> },
	{ TEXT("scripted_sequence"), TEXT("CCineNPC"), &MakeNpcOf<FElysiumScriptedSequence> },
};

struct FElysiumNpcRegistrar
{
	FElysiumNpcRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		BuildInterestingPlaceClass(Reg.Register(TEXT("intersting_place"),
			ElysiumBaseClassName(), &MakeInterestingPlace));

		// CAI_BaseNPC's place in VtMB's chain: under CBaseCombatCharacter, which is under
		// CBaseAnimating. The sheet, the counters and the body all arrive through it; the Troika
		// line stands beneath it, as retail's `CAI_BaseNPCTroika` derives from `CAI_BaseNPC`.
		BuildNpcBaseClass(Reg.RegisterAbstract(TEXT("CAI_BaseNPC"), ElysiumCombatCharacterClassName()));
		BuildNpcClass(Reg.RegisterAbstract(TEXT("CAI_BaseNPCTroika"), TEXT("CAI_BaseNPC")));
		for (const FElysiumNpcRetailClassRow& Row : GNpcRetailClasses)
		{
			FElysiumClassDesc& D = Reg.RegisterAbstract(FName(Row.RetailClass), FName(Row.RetailBase));
			// The class's own datamap rows (story 5 step 4): keyed fields and the SAVE walk, on
			// this descriptor alone, so a descendant inherits them and a sibling never sees them.
			ElysiumNpcKernelBindings::AddSpeciesFields(D, Row.RetailClass);
			AddSpeciesPendingInputs(D, Row.RetailClass);
			AddSpeciesSlotInputs(D, Row.RetailClass);
			// The directors' datamap inputs (`CCineNPC` `0x10593628`, `CCineAISchedule` `0x10593c9c`).
			FElysiumScriptedSequence::AddInputs(D, Row.RetailClass);
			FElysiumAiScriptedSchedule::AddInputs(D, Row.RetailClass);
			// The makers' datamap inputs (`CNPCMaker` `0x10624718`).
			FElysiumNpcMaker::AddInputs(D, Row.RetailClass);
		}
		// `CPayphone` (`.?AVCPayphone@@` `0x10587930`) is a `CAI_BaseNPCTroika` subclass — the class
		// `CBasePlayer::StartPlayerDialog` `0x10178280` RTTI-casts its partner to before it decides
		// whether to create a camera at all (SC9/RC6). The four shipped phones author `dialogname`,
		// `default_camera` and `WillTalk`, the opener takes their dialogue body session, and the arm
		// it runs instead of the camera is `StartGrappleAttack(this, npc, 5)`, whose state lives on
		// the combat character. Its own `EnterGrappleState` override (`CPayphone::vfunc379`
		// `0x101aade0`) is `FElysiumNpcPayphone::EnterGrappleState` (`ElysiumNpcMisc19Species.cpp`).
		//
		// A classname registered here supersedes its `ElysiumStubClasses.cpp` row, whichever
		// registers first (`RegisterStub` answers null once an implementation owns the name).
		for (const FElysiumNpcClassnameRow& Row : GNpcClassnames)
		{
			Reg.Register(FName(Row.Classname), FName(Row.RetailClass), Row.Factory);
		}

		// `ai_hint` — the live `CAI_Hint` every hint-making `info_node*` row becomes
		// (`ElysiumNodeEntity::ApplyHintReplacement`).
		FElysiumHint::BuildClass(Reg.Register(FElysiumHint::ClassName(), ElysiumBaseClassName(),
			&MakeHint));
		FElysiumConversationPlace::BuildClass(Reg.Register(TEXT("intersting_place_conversation"),
			ElysiumBaseClassName(), &MakeConversationPlace));
	}
};

static FElysiumNpcRegistrar GElysiumNpcRegistrar;
