#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **Precache10** — slot 104 `Precache`: the `CAI_BaseNPC` base body, the
// `CAI_BaseNPCTroika` body that owns the slot, and its twenty-three species arms. 25 of the
// family's 28 rows; the three `CNPCMaker*` bodies are in `Substrate/ElysiumNpcMaker.cpp`, on the
// separate type this port models a maker as.
//
// Every constant below was read off the decompiled C and, where the decompiler folded an argument,
// off the listing. `ElysiumNpcKernelPrecache10.inl` carries the family's reading notes; the walked
// prose is `docs/vtmb/npc-ai/lifecycle.md`.
//
// **How a pointer table's order was read.** Eleven of these bodies walk a `.rdata` pointer table
// with a byte loop (`uVar = 0; ... uVar += 4; while (uVar < N)`), and the corpus names such a table
// only by its first entry's string. The order is recovered from the layout: MSVC emits string
// literals in REVERSE order of first appearance, so the entry declared first carries the HIGHEST
// address — `character/monster/gargoyle/stomp_1.wav` is `0x10639814` and `stomp_4` is `0x10639784`.
// The disassembly's own annotation of each indexed load confirms it on the four tables where Ghidra
// resolved two entries (`wingflap_1` then `wingflap_2`; `Foot_Step1` then `Foot_Step2`;
// `pain1` then `pain2`; `ThrowTaxi.mdl` then `supportb.mdl`), and the three tables the port already
// carries as vocalization data (`ElysiumNpcKernelSounds.cpp`, `ElysiumFootsteps.cpp`) agree.

namespace
{
	// --- The words `0x1027bb50` and `0x10298ad0` read -----------------------------------------
	//
	// `DAT_105399a0` is the one-character string `"0"` — the authored "none" sentinel this runtime
	// already spells `ElysiumNpcLoadout::IsNoneSentinel`, and the thing the two-byte `REPE CMPSB`
	// in both bodies tests an equipment keyfield against.
	const TCHAR* const GNoneSentinel = TEXT("0");
	// `s_models_error_error_mdl_105d90c4` — the Troika body's model fallback.
	const TCHAR* const GErrorModel = TEXT("models/error/error.mdl");
	// `s_item_w_unarmed_1055f6bc`, the 15-byte compare (14 characters plus the NUL) the Troika body
	// runs on `m_altEquipment` after the sentinel test.
	const TCHAR* const GUnarmedItem = TEXT("item_w_unarmed");
	// `DAT_10598a30` and `DAT_10548ed4`, the two extension patterns `0x101d0f10` globs with. Their
	// bytes are pinned by `FUN_101b1120`, which is SDK-2013's `CSoundEmitterSystem::EmitSound`
	// instruction for instruction: `Q_stristr(soundname, ".wav") || Q_stristr(soundname, ".mp3") ||
	// soundname[0] == '!'`.
	const TCHAR* const GExtWav = TEXT(".wav");
	const TCHAR* const GExtMp3 = TEXT(".mp3");
	// `s_Normal_105c89dc` — the attack-coordinator name slot 608 is dispatched with, last.
	const TCHAR* const GNormalCoordinator = TEXT("Normal");
	// `0x101d0f10`'s two reject literals. **UNRECOVERED**: `DAT_105a0410` is compared over three
	// characters and `DAT_105a040c` over one, and the corpus holds neither's bytes. Carried empty,
	// which makes `Q_strnicmp(dir, "", n)` non-zero for any non-empty directory and so refuses
	// nothing — and none of this family's six call sites passes a directory either could match.
	const TCHAR* const GDirectoryRejectPrefix3 = TEXT("");   // DAT_105a0410
	const TCHAR* const GDirectoryRejectPrefix1 = TEXT("");   // DAT_105a040c
	// `LEA ECX,[EBP + 0x6]` at `101d0fb9`/`101d0fc4` — every hit is precached under the directory
	// MINUS its first six characters, which is the literal `"sound/"` all three call sites open
	// with. Recorded on the op rather than applied, because the op names the directory retail was
	// handed.
	constexpr int32 GDirectorySoundPrefixLength = 6;

	// Slot 104's index, spelled once.
	constexpr int32 GPrecacheSlot = 104;

	// --- `CNPC_Crow` `0x10358ec0` --------------------------------------------------------------
	const TCHAR* const GCrowModel = TEXT("models/crow.mdl");

	// --- `CGeneric_NPC` `0x10359f70`, `CGeneric_NPC_bathack` `0x1035ade0`,
	//     `CGenericSabbat_NPC` `0x1035b5d0` ------------------------------------------------------
	//
	// Three classes, three SEPARATE `.rdata` copies of the same four wav names (`0x10629a18…`,
	// `0x10629d80…`, `0x1062a004…`) — family Sounds recorded the same duplication for the
	// vocalization hooks and the tables are repeated here for the same reason: a reader checking
	// one class against its `Precache` finds its own table.
	const TCHAR* const GMetropoliceAlert = TEXT("npc/metropolice/alert1.wav");
	const TCHAR* const GMetropoliceSurprise = TEXT("npc/metropolice/surprise1.wav");
	const TCHAR* const GMetropoliceDie = TEXT("npc/metropolice/die1.wav");
	const TCHAR* const GCitizenPain[] = {
		TEXT("npc/citizen/pain1.wav"),
		TEXT("npc/citizen/pain2.wav"),
		TEXT("npc/citizen/pain3.wav"),
		TEXT("npc/citizen/pain4.wav"),
	};
	// `0x10629c00`, and `CGenericSabbat_NPC`'s pointer `0x1062a000` resolves to the SAME string.
	const TCHAR* const GSabbatFemaleModel = TEXT("models/character/npc/sabbat/sabbat_female.mdl");
	const TCHAR* const GBathackModel = TEXT("models/bats.mdl");

	// --- `CNPC_VAndreiBlood` `0x1035cb90` ------------------------------------------------------
	//
	// The three emitters carry a HYPHEN before `Emitter`, not the underscore the checklist's walk
	// spells: `0x1062b04c`, `0x1062b02c`, `0x1062b010`, each also named by
	// `CNPC_VAndreiBlood::StartTask`.
	const TCHAR* const GAndreiTeleportOutSound = TEXT("Character/Boss/Andrei/TeleportOut.wav");
	const TCHAR* const GAndreiTeleportInSound = TEXT("Character/Boss/Andrei/TeleportIn.wav");
	const TCHAR* const GAndreiSummonSound = TEXT("Character/Boss/Andrei/Summon.wav");
	const TCHAR* const GAndreiTeleportOutEmitter = TEXT("Andrei_Teleport_Out-Emitter");
	const TCHAR* const GAndreiTeleportInEmitter = TEXT("Andrei_Teleport_In-Emitter");
	const TCHAR* const GAndreiSummonEmitter = TEXT("Andrei_Summon-Emitter");

	// --- `CNPC_VBach` `0x103637b0` -------------------------------------------------------------
	const TCHAR* const GBachWeapons[] = {
		TEXT("item_w_grenade_frag"),
		TEXT("item_w_katana"),
		TEXT("item_w_rem_m_700_bach"),
	};
	const TCHAR* const GBachSounds[] = {
		TEXT("Character/Boss/Bach/bach_grenade.wav"),
		TEXT("Character/Boss/Bach/bach_shield.wav"),
		TEXT("Character/Boss/Bach/bach_camp_warn.wav"),
		TEXT("Character/Boss/Bach/bach_holy_light.wav"),
		TEXT("Character/Boss/Bach/snipe_warn6.wav"),
	};

	// --- `CNPC_VChangBros` `0x1036ae60`, shared by `…Blade` and `…Claw` ------------------------
	const TCHAR* const GChangEmitters[] = {
		TEXT("chang_teleport_in_emitter"),
		TEXT("chang_teleport_out_emitter"),
		TEXT("chang_powerup_emitter"),
		TEXT("chang_spine_emitter"),
		TEXT("chang_center_emitter"),
		TEXT("chang_blast_emitter"),
		TEXT("chang_ball_charge_emitter"),
	};
	const TCHAR* const GChangWeapons[] = {
		TEXT("item_w_chang_claw"),
		TEXT("item_w_chang_blade"),
		TEXT("item_w_chang_energy_ball"),
		TEXT("item_w_chang_ghost"),
	};

	// --- `CNPC_VGargoyle` `0x10378470` ---------------------------------------------------------
	//
	// Nine gib models, precached in DESCENDING `.rdata` address order (`0x1063a808` down to
	// `0x1063a550`) — which is the order the body pushes them, not an artifact.
	const TCHAR* const GGargoyleGibModels[] = {
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/garg_gibbs.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_head.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_foot.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_hand.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_torso.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_pelvis.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_foot.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_hand.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_torso.mdl"),
	};
	const TCHAR* const GGargoyleStomps[] = {   // 0x10639480, to 0x10 — four
		TEXT("character/monster/gargoyle/stomp_1.wav"),
		TEXT("character/monster/gargoyle/stomp_2.wav"),
		TEXT("character/monster/gargoyle/stomp_3.wav"),
		TEXT("character/monster/gargoyle/stomp_4.wav"),
	};
	const TCHAR* const GGargoyleExerts[] = {   // 0x10639490, to 0xc — three
		TEXT("character/monster/gargoyle/exert_heavy_1.wav"),
		TEXT("character/monster/gargoyle/exert_heavy_2.wav"),
		TEXT("character/monster/gargoyle/exert_heavy_3.wav"),
	};
	const TCHAR* const GGargoyleRoar = TEXT("character/monster/gargoyle/roar2.wav");
	const TCHAR* const GGargoyleWeapon = TEXT("item_w_gargoyle_fist");

	// --- `CNPC_VGhoulCroucher` `0x1037b1a0` ----------------------------------------------------
	const TCHAR* const GGhoulCroucherWeapon = TEXT("item_w_claws_ghoul");
	// `0x1063b088` then `0x1063b028`, preload 0 — the two models that make the male/female split in
	// its `SetModel` sibling (`0x1037b1f0`) reachable.
	const TCHAR* const GGhoulCroucherModels[] = {
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker/stalker.mdl"),
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker_Female/stalker_female.mdl"),
	};

	// --- `CNPC_VHengeyokai` `0x1037f960` -------------------------------------------------------
	const TCHAR* const GHengeyokaiStomps[] = {   // 0x1063bd94, to 0x10
		TEXT("character/monster/hengeyokai/stomp_1.wav"),
		TEXT("character/monster/hengeyokai/stomp_2.wav"),
		TEXT("character/monster/hengeyokai/stomp_3.wav"),
		TEXT("character/monster/hengeyokai/stomp_4.wav"),
	};
	const TCHAR* const GHengeyokaiExerts[] = {   // 0x1063bda4, to 0xc
		TEXT("character/monster/hengeyokai/exert_heavy_1.wav"),
		TEXT("character/monster/hengeyokai/exert_heavy_2.wav"),
		TEXT("character/monster/hengeyokai/exert_heavy_3.wav"),
	};
	const TCHAR* const GHengeyokaiModel =
		TEXT("models/character/monster/Hengeyokai/hengeyokai.mdl");
	const TCHAR* const GHengeyokaiFreezeEmitter = TEXT("Hengeyokai_freeze_emitter");
	const TCHAR* const GHengeyokaiWeapon = TEXT("item_w_hengeyokai_fist");

	// --- `CNPC_VManBat` `0x1038aec0` -----------------------------------------------------------
	//
	// The model table at `0x10640ce0` is FOUR entries and the corpus pins only two of the four
	// strings. Entries 2 and 3 are recorded by table index, the convention family Lifecycle used
	// for `PTR_s_weapons_ar2_ar2_fire1_wav_106244c0`'s unnamed tail.
	const TCHAR* const GManBatThrowModels[] = {
		TEXT("models/character/monster/manbat/Throw_Objects/ThrowTaxi.mdl"),
		TEXT("models/character/monster/manbat/Throw_Objects/supportb.mdl"),
		TEXT("PTR_0x10640ce0[2]"),   // unrecovered
		TEXT("PTR_0x10640ce0[3]"),   // unrecovered
	};
	const TCHAR* const GManBatEmitters[] = {
		TEXT("Manbat_screechcone_emitter"),
		TEXT("Manbat_player_emitter"),
		TEXT("HUD_Manbat_emitter"),
		TEXT("Manbat_blast_player"),   // the one of the four with no `_emitter` suffix
	};
	const TCHAR* const GManBatWingflaps[] = {   // 0x10640d10, to 0xc
		TEXT("character/male/sheriff_manbat/wingflap_1.wav"),
		TEXT("character/male/sheriff_manbat/wingflap_2.wav"),
		TEXT("character/male/sheriff_manbat/wingflap_3.wav"),
	};
	const TCHAR* const GManBatExerts[] = {      // 0x10640d1c, to 0xc
		TEXT("character/male/sheriff_manbat/exert_heavy_1.wav"),
		TEXT("character/male/sheriff_manbat/exert_heavy_2.wav"),
		TEXT("character/male/sheriff_manbat/exert_heavy_3.wav"),
	};
	const TCHAR* const GManBatFlyBys[] = {      // 0x10640d28, to 0xc
		TEXT("character/male/sheriff_manbat/fly_by_1.wav"),
		TEXT("character/male/sheriff_manbat/fly_by_2.wav"),
		TEXT("character/male/sheriff_manbat/fly_by_3.wav"),
	};
	const TCHAR* const GManBatScreech = TEXT("character/male/sheriff_manbat/screech.wav");
	const TCHAR* const GManBatFall = TEXT("character/male/sheriff_manbat/fall.wav");
	const TCHAR* const GManBatWeapon = TEXT("item_w_manbat_claw");
	// `0x10642adc` — precached TWICE in a row by BOTH `CNPC_VManBat` and `CNPC_VSheriffMan`, from
	// the same `.rdata` cell. A retail duplicate, kept.
	const TCHAR* const GSheriffTeleportEmitter = TEXT("sheriff_teleport_emitter");

	// --- `CNPC_VMingXiao` `0x10392660` ---------------------------------------------------------
	const TCHAR* const GMingXiaoEmitters[] = {
		TEXT("Ming_xiao_slimetrail_emitter"),
		TEXT("Ming_xiao_slimetrail_emitter2"),
		TEXT("Ming_xiao_tentacle_damage_emitter"),
		TEXT("Ming_xiao_tentacle_burst_emitter"),
		TEXT("Ming_xiao_death_emitter"),
		TEXT("Ming_xiao_death_emitter2"),
		TEXT("Ming_xiao_death_proxy_emitter"),
		TEXT("Ming_xiao_death_proxy_emitter2"),
		TEXT("Ming_xiao_vomit_emitter"),
		TEXT("Ming_xiao_transform_emitter"),
		TEXT("Ming_xiao_transform_emitter2"),
	};
	// `PTR_..._1064339c`, one entry: the directory name carries a SPACE, not an underscore.
	const TCHAR* const GMingXiaoMoveSound = TEXT("character/monster/ming xiao/movement.wav");
	const TCHAR* const GMingXiaoWeapons[] = {
		TEXT("item_w_mingxiao_melee"),
		TEXT("item_w_mingxiao_tentacle"),
		TEXT("item_w_mingxiao_spit"),
	};

	// --- `CNPC_VMingXiaoTentacle` `0x1039c220` -------------------------------------------------
	const TCHAR* const GTentacleFallbackModel =
		TEXT("models/character/monster/mingxiao/mingxiao_tentacle/mingxiao_tentacle.mdl");
	// `0x1064a2f0` is pushed TWICE — `m_iModeIndexTentacleToGrub` and `m_iModeIndexGrub` are two
	// indices of ONE model, which is retail's own duplicate call and is kept.
	const TCHAR* const GTentacleBabyModel =
		TEXT("models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl");
	const TCHAR* const GTentacleTransformModel =
		TEXT("models/character/monster/MingXiao/MingXiao_transformation.mdl");
	const TCHAR* const GTentacleEmitters[] = {
		TEXT("Ming_xiao_tentacle_transform_emitter"),
		TEXT("Ming_xiao_baby_transform_emitter"),
		TEXT("Ming_xiao_baby_death_emitter"),
	};
	// `0x106477c8` then `0x106477cc`, both pinned by the listing's own annotation.
	const TCHAR* const GTentacleSounds[] = {
		TEXT("character/monster/ming xiao/tentacle_hit_ground.wav"),
		TEXT("character/monster/ming xiao/tentacle_flopping_loop.wav"),
	};

	// --- `CNPC_VNewscaster` `0x103a03e0` -------------------------------------------------------
	const TCHAR* const GNewscasterSoundDir = TEXT("sound/character/conversations/news/tv");

	// --- `CNPC_VSabbatLeader` `0x103a6ab0` -----------------------------------------------------
	const TCHAR* const GSabbatLeaderModels[] = {
		TEXT("models/character/monster/Andrei/andrei.mdl"),
		TEXT("models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl"),
	};
	const TCHAR* const GAndreiTransformedSteps[] = {   // 0x1064c480, to 0x1c — seven
		TEXT("character/monster/andrei_transformed/step1.wav"),
		TEXT("character/monster/andrei_transformed/step2.wav"),
		TEXT("character/monster/andrei_transformed/step3.wav"),
		TEXT("character/monster/andrei_transformed/step4.wav"),
		TEXT("character/monster/andrei_transformed/step5.wav"),
		TEXT("character/monster/andrei_transformed/step6.wav"),
		TEXT("character/monster/andrei_transformed/step7.wav"),
	};
	const TCHAR* const GAndreiTransformedExerts[] = {  // 0x1064c49c, to 0xc — three
		TEXT("character/monster/andrei_transformed/exert_heavy_1.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_2.wav"),
		TEXT("character/monster/andrei_transformed/exert_heavy_3.wav"),
	};
	// The seven singles, in the body's own push order (`0x1064ea40` down to `0x1064e8b0`).
	const TCHAR* const GAndreiTransformedSingles[] = {
		TEXT("Character/Monster/Andrei_Transformed/ambient_run.wav"),
		TEXT("Character/Monster/Andrei_Transformed/Leap_Down_Attack_1.wav"),
		TEXT("Character/Monster/Andrei_Transformed/dive_in_splash.wav"),
		TEXT("Character/Monster/Andrei_Transformed/dive_out_splash.wav"),
		TEXT("Character/Monster/Andrei_Transformed/splash_warning.wav"),
		TEXT("Character/Monster/Andrei_Transformed/jump_retreat.wav"),
		TEXT("Character/Monster/Andrei_Transformed/roar_1.wav"),
	};
	const TCHAR* const GAndreiPowerupEmitter = TEXT("Andrei_powerup_emitter");
	const TCHAR* const GAndreiBlastEmitter = TEXT("Andrei_blast_emitter");
	const TCHAR* const GSabbatLeaderWeapon = TEXT("item_w_sabbatleader_attack");

	// --- `CNPC_VSheriffMan` `0x103ae540` -------------------------------------------------------
	const TCHAR* const GSheriffModel = TEXT("models/character/monster/manbat/manbat.mdl");
	const TCHAR* const GSheriffLandblastEmitter = TEXT("sheriff_landblast_emitter");
	const TCHAR* const GSheriffWeapon = TEXT("item_w_sheriff_sword");

	// --- `CNPC_VTest` `0x103b41e0` -------------------------------------------------------------
	//
	// Twelve sounds and then the base, LAST. Family Sounds already carries eight of these names as
	// vocalization data (`ElysiumNpcKernelSounds.cpp`); `knockout1` is precached and never spoken.
	const TCHAR* const GTestDeath = TEXT("character/npc/test/death1.wav");
	const TCHAR* const GTestAlert = TEXT("character/npc/test/alert1.wav");
	const TCHAR* const GTestIdle = TEXT("character/npc/test/idle1.wav");
	const TCHAR* const GTestPains[] = {   // 0x10652194, to 0x10
		TEXT("character/npc/test/pain1.wav"),
		TEXT("character/npc/test/pain2.wav"),
		TEXT("character/npc/test/pain3.wav"),
		TEXT("character/npc/test/pain4.wav"),
	};
	const TCHAR* const GTestFear = TEXT("character/npc/test/fear1.wav");
	const TCHAR* const GTestLostEnemy = TEXT("character/npc/test/lostenemy1.wav");
	const TCHAR* const GTestFoundEnemy = TEXT("character/npc/test/foundenemy1.wav");
	const TCHAR* const GTestSurprise = TEXT("character/npc/test/surprise1.wav");
	const TCHAR* const GTestKnockout = TEXT("character/npc/test/knockout1.wav");

	// --- `CNPC_VTzimisce` `0x103b8fa0` ---------------------------------------------------------
	const TCHAR* const GSpiderchickFootsteps[] = {   // 0x106530fc, to 0x18 — six
		TEXT("character/monster/spiderchick/spi_footstep_indiv_1.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_2.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_3.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_4.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_5.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_6.wav"),
	};
	const TCHAR* const GSpiderchickSwishes[] = {     // 0x10653114, to 0xc — three
		TEXT("character/monster/spiderchick/spi_attack_swish_1.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_2.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_3.wav"),
	};
	const TCHAR* const GTzimisceWeapon = TEXT("item_w_tzimisce_melee");

	// --- `CNPC_VTzimisceHeadClaw` `0x103c1400` -------------------------------------------------
	const TCHAR* const GTzim2Emitters[] = {
		TEXT("Tzim2_powerup_emitter"),
		TEXT("Tzim2_blast_emitter"),
		TEXT("Tzim2_player_emitter"),
		TEXT("HUD_Tzim2_emitter"),
	};
	// Three CONTIGUOUS tables at `0x1065ca68` / `0x1065ca70` / `0x1065ca78`: the fat guy's four
	// footsteps split 2+2, then his three exerts.
	const TCHAR* const GFatGuyFootStepsA[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step1.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step2.wav"),
	};
	const TCHAR* const GFatGuyFootStepsB[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step3.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step4.wav"),
	};
	const TCHAR* const GFatGuyExerts[] = {
		TEXT("character/monster/TC_FatGuy/Exert_Heavy_1.wav"),
		TEXT("character/monster/TC_FatGuy/Exert_Heavy_2.wav"),
		TEXT("character/monster/TC_FatGuy/Exert_Heavy_3.wav"),
	};
	const TCHAR* const GFatGuySlugHit = TEXT("Character/Monster/TC_FatGuy/Sluge_Hit.wav");
	const TCHAR* const GFatGuySlugAffected =
		TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav");
	const TCHAR* const GTzim2Weapons[] = {
		TEXT("item_w_tzimisce2_claw"),
		TEXT("item_w_tzimisce2_head"),
	};

	// --- `CNPC_VTzimisceRunner` `0x103c31e0` ---------------------------------------------------
	//
	// Four contiguous tables at `0x1065d680` / `…688` / `…690` / `…6a0`. The port's footstep table
	// (`ElysiumFootsteps.cpp`) already carries the same 2+2 left/right split and the same four
	// breaths.
	const TCHAR* const GRunnerStepsA[] = {
		TEXT("character/monster/TC_Runner/foot_steps_1.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_2.wav"),
	};
	const TCHAR* const GRunnerStepsB[] = {
		TEXT("character/monster/TC_Runner/foot_steps_3.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_4.wav"),
	};
	const TCHAR* const GRunnerBreaths[] = {
		TEXT("character/monster/TC_Runner/Breath1.wav"),
		TEXT("character/monster/TC_Runner/Breath2.wav"),
		TEXT("character/monster/TC_Runner/Breath3.wav"),
		TEXT("character/monster/TC_Runner/Breath4.wav"),
	};
	const TCHAR* const GRunnerExerts[] = {
		TEXT("character/monster/TC_Runner/Exert_Heavy_1.wav"),
		TEXT("character/monster/TC_Runner/Exert_Heavy_2.wav"),
		TEXT("character/monster/TC_Runner/Exert_Heavy_3.wav"),
	};
	const TCHAR* const GRunnerWeapon = TEXT("item_w_tzimisce3_claw");

	// --- `CNPC_VWerewolf` `0x103cb2a0` ---------------------------------------------------------
	const TCHAR* const GWerewolfSoundDir = TEXT("sound/Character/Monster/Werewolf");
	const TCHAR* const GObservatorySoundDir = TEXT("sound/Area/Special/Observatory");
	const TCHAR* const GWerewolfSoundGroup = TEXT("Werewolf");
	// `m_iVSoundTableIdx` takes the literal 2 before the group row is looked up.
	constexpr int32 GWerewolfVSoundTableIndex = 2;
	// `0x1065f4d0`, four entries — and the listing annotates them `character/monster/TC_FatGuy/
	// Foot_Step1.wav` and `…Foot_Step2.wav`. The WEREWOLF precaches the Tzimisce fat guy's
	// footsteps: its own steps come through the sound GROUP it binds three lines earlier, and this
	// table is a retail copy-paste that is reproduced rather than corrected.
	const TCHAR* const GWerewolfFootsteps[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step1.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step2.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step3.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step4.wav"),
	};
	const TCHAR* const GWerewolfWeapon = TEXT("item_w_werewolf_attacks");
	// `dev/ww_tele_out.wav` and `dev/ww_tele_in.wav` — a SLASH, not the underscore the checklist's
	// walk spells, and the same two names `TeleportOut`/`TeleportIn` play.
	const TCHAR* const GWerewolfTeleOut = TEXT("dev/ww_tele_out.wav");
	const TCHAR* const GWerewolfTeleIn = TEXT("dev/ww_tele_in.wav");

	// --- `CNPC_VZombie` `0x103df120` -----------------------------------------------------------
	const TCHAR* const GZombieEmitters[] = {
		TEXT("zombie_headshot_death_emitter"),
		TEXT("zombie_headshot_dmg_emitter"),
	};
	const TCHAR* const GZombieWeapon = TEXT("item_w_zombie_fists");

	// --- The four request shapes, spelled once --------------------------------------------------
	//
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.

	void Precache10Model(FElysiumNpc& Npc, const TCHAR* Name, int32 Preload)
	{
		FElysiumNpc::FPrecacheOp Op;
		Op.Channel = FElysiumNpc::EPrecacheChannel::Model;
		Op.Name = Name;
		Op.Flag = Preload;
		Npc.IssuePrecache(Op);
	}

	void Precache10Sound(FElysiumNpc& Npc, const TCHAR* Name)
	{
		FElysiumNpc::FPrecacheOp Op;
		Op.Channel = FElysiumNpc::EPrecacheChannel::Sound;
		Op.Name = Name;
		Npc.IssuePrecache(Op);
	}

	void Precache10SoundTable(FElysiumNpc& Npc, const TCHAR* const* Names, int32 Count)
	{
		// The byte loop, verbatim: `uVar = 0; do { precache(table[uVar/4]); uVar += 4; } while
		// (uVar < N)`. A do/while, so a table walked to zero bytes would still precache its first
		// entry — no body here walks one, and the bound is always a positive multiple of four.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Precache10Sound(Npc, Names[Index]);
		}
	}

	void Precache10Particle(FElysiumNpc& Npc, const TCHAR* Name, int32 Preload)
	{
		FElysiumNpc::FPrecacheOp Op;
		Op.Channel = FElysiumNpc::EPrecacheChannel::Particle;
		Op.Name = Name;
		Op.Flag = Preload;
		Npc.IssuePrecache(Op);
	}

	void Precache10Other(FElysiumNpc& Npc, const FString& Classname)
	{
		FElysiumNpc::FPrecacheOp Op;
		Op.Channel = FElysiumNpc::EPrecacheChannel::Other;
		Op.Name = Classname;
		Npc.IssuePrecache(Op);
	}
}

// -------------------------------------------------------------------------------------------------
// The seam.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::IssuePrecache(const FPrecacheOp& Op)
{
	// The record IS the recovered half; the acquisition is the seam. See the `.inl`.
	PrecacheLog.Add(Op);
}

int32 FElysiumNpc::VSoundGroupRowFor(const TCHAR* GroupName)
{
	// SEAM for `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, 0)`. Nothing in this runtime parses
	// a VSound concept list (family Sounds10), so the table is empty and this takes retail's own
	// count-zero miss.
	(void)GroupName;
	return INDEX_NONE;
}

FString FElysiumNpc::CharTemplateModelName() const
{
	// SEAM for `FUN_10207e60`: `GetCharTemplate(this)` (`0x10207c40`) then the template's `+0x78`
	// (`0x101d4f20`). `+0x78` has no recovered column name and this runtime's template records
	// expose none, so this answers the empty string — which is also what retail precaches when the
	// template pointer is null (`DAT_106b8540`).
	return FString();
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPC::Precache` — `0x1027bb50`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::BasePrecache()
{
	// Step 1. `m_spawnEquipment` (`+0x5dec`, this runtime's `AdditionalEquipment`) is precached when
	// the pointer is non-null AND the string is not the two-byte literal `"0"` (`DAT_105399a0`). A
	// null pointer reads as the empty string (`DAT_106b8540`) and an empty string is NOT `"0"`, so
	// retail's outer null test is what stops an unset keyfield — and this runtime, which carries the
	// keyfield as an `FString`, spells the same pair of tests as "non-empty and not the sentinel".
	if (!AdditionalEquipment.IsEmpty() && AdditionalEquipment != GNoneSentinel)
	{
		Precache10Other(*this, AdditionalEquipment);
	}

	// Step 2. Slot 452 `LoadedSchedules` (vtable `+0x710`).
	if (!LoadedSchedules())
	{
		// `DevMsg` then `UTIL_Remove(this)` (`thunk_FUN_101cd940`) — and RETURN, so the base
		// `CBaseCombatCharacter::Precache` never runs. That is the whole point of the arm: an NPC
		// whose schedule text failed to parse is removed rather than half-precached.
		//
		// UNREACHABLE IN THIS RUNTIME TODAY: `FElysiumNpc::LoadedSchedules` answers true for every
		// class by design (`ElysiumNpcKernelSchedule.cpp:297` — nothing here parses schedule text,
		// so no class flag can be cleared). Ported anyway, and exercised through the same slot.
		//
		// The format is `s_ERROR__Rejecting_spawn_of__s_as_e_105cd21c` verbatim, including the
		// apostrophe and the trailing period the checklist's walk drops.
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("ERROR: Rejecting spawn of %s as error in NPC's schedules."), *DebugString());
		Kill();
		return;
	}

	// Step 3. `CBaseCombatCharacter::Precache` (`0x10011324` -> `CBaseCombatCharacter::PrecacheOnce`
	// `0x1033f750`). SEAM, and deliberately not a row of this family: that body is the once-guarded
	// GLOBAL block every character shares — the discipline emitters (`D_Potence_Emitter`,
	// `D_AuspexCast_Emitter`, `D_ObfuscateIn_Emitter`, …), the damage-effect emitters
	// (`DMGFX_body_fire_emitter`, `DMGFX_hud_shock_emitter`, …) and `HUD_targeting_emitter`. It is
	// `CBaseCombatCharacter`'s story, not the NPC kernel's, it is precached once per map rather than
	// per NPC, and this runtime's discipline and damage visuals are Unreal assets the map epoch
	// already holds. Nothing is recorded for it, because recording a per-map block on a per-NPC log
	// would misstate what retail does.
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPCTroika::Precache` — `0x10298ad0`, slot 104.
// -------------------------------------------------------------------------------------------------

FString FElysiumNpc::DialogueSoundDirectory(const FString& Dialog)
{
	// `Q_snprintf(buf, 0x104, "sound/character/%s", m_iDialog)` — `s_sound_character__s_105622e4`,
	// spelled at the call site because `FString::Printf` requires a literal format.
	FString Buffer = FString::Printf(TEXT("sound/character/%s"), *Dialog);

	// `buf[strlen(buf) - 4] = 0`, read off the listing at `10298bf8`..`10298c04`:
	//
	//     LEA EDI,[ESP+0x1c]      ; EDI = buf
	//     OR ECX,-1 / XOR EAX,EAX / REPNE SCASB / NOT ECX / DEC ECX     ; ECX = strlen(buf)
	//     LEA EDX,[ESP+0x1c] / SUB EDX,0x4                              ; EDX = buf - 4
	//     MOV byte ptr [ECX + EDX*1],AL                                 ; buf[strlen - 4] = 0
	//
	// **FOUR** characters, not the five the checklist's walk claims. For `m_iDialog` = `"foo.dlg"`
	// the directory becomes `"sound/character/foo"` — the extension and the dot come off, which is
	// what makes the glob land on the character's own conversation directory.
	//
	// DIVERGENCE, named: a dialog name shorter than four characters makes `strlen(buf) - 4` index
	// BEFORE the buffer, and retail writes a NUL over its own stack. This port clamps at zero and
	// answers the empty directory, which `PrecacheDirectory` then refuses on its own empty-name arm.
	// The prefix is 16 characters long, so `strlen(buf)` is at least 16 for any dialog name at all
	// and the clamp is unreachable for every authored `dialogname` — it exists so a fixture cannot
	// corrupt the heap, not to change an answer.
	const int32 Chop = Buffer.Len() - 4;
	Buffer = Chop > 0 ? Buffer.Left(Chop) : FString();

	// `Q_strnlwr(buf, strlen(buf))`, recomputed AFTER the chop.
	return Buffer.ToLower();
}

void FElysiumNpc::PrecacheDirectory(const FString& Directory, const TCHAR* Extension,
	bool bStarPrefix, int32 Flag)
{
	// `0x101d0f10`, arm by arm.
	//
	// A null directory answers 0 (`TEST EBP,EBP / JZ`), and so does an EMPTY one (`REPNE SCASB /
	// NOT ECX / DEC ECX / JNZ` — the `DEC` sets ZF when the length is zero).
	if (Directory.IsEmpty())
	{
		return;
	}
	// The two reject prefixes. Both literals are unrecovered and carried empty, so neither matches;
	// see the `.inl`.
	if (*GDirectoryRejectPrefix3 != TEXT('\0')
		&& Directory.Left(3).Equals(GDirectoryRejectPrefix3, ESearchCase::IgnoreCase))
	{
		return;
	}
	if (*GDirectoryRejectPrefix1 != TEXT('\0')
		&& Directory.Left(1).Equals(GDirectoryRejectPrefix1, ESearchCase::IgnoreCase))
	{
		return;
	}
	// SEAM: `FindFirst("<dir>/*<ext>")` / `FindNext`, precaching every non-directory hit as
	// `"*%s/%s"` (or `"%s/%s"`) of `dir + 6` and the hit's name, with `Flag` as the precache flag.
	// This runtime enumerates no `sound/` tree at kernel level — the bake resolves a soundscript by
	// name and the corpus is not walked per NPC — so the walk finds nothing and the whole REQUEST is
	// recorded instead: the directory, the extension and both flags, which is every input retail's
	// own answer depends on.
	(void)GDirectorySoundPrefixLength;
	FPrecacheOp Op;
	Op.Channel = EPrecacheChannel::Directory;
	Op.Name = Directory;
	Op.Extension = Extension;
	Op.bStarPrefix = bStarPrefix;
	Op.Flag = Flag;
	IssuePrecache(Op);
}

void FElysiumNpc::TroikaPrecache()
{
	// `0x10298ad0`, in retail's order.
	//
	// The model keyfield is read through slot 9 `GetModelName` (vtable `+0x24`) three times to ask
	// one question: "is it unset or empty". This runtime carries it as `FElysiumEntity::Model`
	// (`ElysiumNpcKernelShapeMap` binds the same word), and slot 9 / slot 212 are still generated
	// `CBaseEntity` stubs that no story has verdicted — calling them would tally a stub and lose the
	// write, so the word is read and written directly with the slots named here.
	if (Model.IsEmpty())
	{
		// `SetModelName(-(uint)(s_models_error_error_mdl[0] != 0) & 0x105d90c4)` — vtable `+0x350`,
		// slot 212. The `-(x != 0) &` idiom yields a NULL pointer for an empty literal; the literal
		// is not empty, so the fallback is always the string.
		Model = GErrorModel;
	}

	// `PrecacheModel(model, 0)` and the returned index to slot 10 `SetModelIndex` (vtable `+0x28`).
	// The index is the engine's; this runtime has no model-index space, so nothing is stored and the
	// slot is named rather than dispatched — slot 10 is a generated `CBaseEntity` stub too.
	Precache10Model(*this, *Model, /*Preload=*/0);

	// `m_altEquipment` (`+0x1a98`, this runtime's `AlternateEquipment`): precached unless it is the
	// sentinel `"0"` or the literal `"item_w_unarmed"`. TWO exclusions here where the base body has
	// one, and the unarmed exclusion is exact (a 15-byte compare, the string plus its NUL).
	if (!AlternateEquipment.IsEmpty()
		&& AlternateEquipment != GNoneSentinel
		&& AlternateEquipment != GUnarmedItem)
	{
		Precache10Other(*this, AlternateEquipment);
	}

	// `CAI_BaseNPC::thunk_FUN_1027bb50(this)` — a DIRECT call with no argument (`MOV ECX,EBX /
	// CALL 0x1000eaf7`; the decompiler's `param_1` is the dead `EAX` of the previous call).
	BasePrecache();

	// `m_iDialog` (`+0x0128`, this runtime's `DialogName`): the conversation directory, globbed
	// twice — `.wav` first, then `.mp3`. Both with `bStarPrefix` SET and the precache flag 0, which
	// is the opposite pair from `CNPC_VWerewolf`'s two calls to the same function.
	const FString Dialog = DialogName;
	if (!Dialog.IsEmpty())
	{
		const FString Directory = DialogueSoundDirectory(Dialog);
		PrecacheDirectory(Directory, GExtWav, /*bStarPrefix=*/true, /*Flag=*/0);
		PrecacheDirectory(Directory, GExtMp3, /*bStarPrefix=*/true, /*Flag=*/0);
	}

	// `*(int*)(this + 0x64e8) = thunk_FUN_100ec640(this)` — `CDispositionTable::PrecacheModel`
	// (`0x100ec640`) on the singleton at `0x10924980`. Retail walks its rows for this entity's model
	// index, and on a miss builds a new row: per stance, three `stance_%s_idle_%d`, three
	// `stance_%s_fidget_%d` and three-by-three `stance_%s_trans_%d_%d` sequence lookups, a missing
	// idle falling back to idle 1 and a missing fidget or transition to the matching idle. The row
	// index is the answer, and `+0x64e8` is `m_iDispositionModelIndex`.
	//
	// This runtime's CDispositionTable is `FElysiumNpc::EnsureStanceResolved`, and the shape map
	// binds `+0x64e8` to its cache key (`StanceResolvedFor`, "the port keys the row by name and
	// level, not by a table row index"). So the write IS that call: the stance clips and the
	// disposition tuning for this body's model are resolved here, at precache, exactly where retail
	// builds them. Headless (no embodiment) it answers false and leaves the key set, which is
	// retail's own "the model has no sequences" outcome.
	EnsureStanceResolved();

	// `(**(code **)(*(int *)this + 0x980))("Normal")` — vtable `+0x980` is slot 608,
	// `SetAttackCoordinator(const char*)` (family TroikaHelpers' `Slot608`). EVERY Troika NPC is
	// bound to the coordinator named `"Normal"` at precache, and the bind is what makes the
	// coordinator's five entry points reachable at all.
	Slot608(GNormalCoordinator);
}

// -------------------------------------------------------------------------------------------------
// Slot 104 `Precache` — the species prologue, then the Troika body.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Precache()
{
	// The vtable, spelled as a table lookup: an override of slot 104 replaces this body outright,
	// and the arms that want the Troika body call back into this function under
	// `FSpeciesDispatchScope`, which is retail's non-virtual thunk.
	//
	// NOTHING IN THIS RUNTIME CALLS `Precache()` YET, and that is deliberate rather than an
	// oversight: this substrate acquires assets for the whole map epoch before any NPC stands
	// (`FElysiumMapActor::PreparePropAndWieldModels`), so wiring a per-entity precache into
	// `Spawn` would ADD an event retail's order does not have here. The body is ported whole and is
	// driven by `Elysium.Substrate.NpcKernelPrecache10.*`; the caller lands with the asset path.
	if (PrecacheSpecies())
	{
		return;
	}
	TroikaPrecache();
}

namespace
{
	// One slot-104 override row and the port body that carries it. Keyed on the RETAIL ADDRESS, so
	// the three Chang forms and the two camera forms are one arm apiece.
	struct FPrecache10Arm
	{
		const TCHAR* Address = nullptr;
		// The retail class the body is named after, for the report and for the coverage test.
		const TCHAR* RetailClass = nullptr;
		// Null for the three `CNPCMaker*` bodies: they land on `FElysiumNpcMaker::Precache` and can
		// never run on an `FElysiumNpc`, so the arm CLAIMS the slot (the Troika body must not run
		// for a class whose body is not the Troika body) and does nothing.
		void (FElysiumNpc::*Body)() = nullptr;
	};
}

bool FElysiumNpc::PrecacheSpecies()
{
	static const FPrecache10Arm Arms[] =
	{
		// The twenty-three species arms of story 29d, family Precache10.
		{ TEXT("0x10358ec0"), TEXT("CNPC_Crow"),            &FElysiumNpc::CrowPrecache },
		{ TEXT("0x10359f70"), TEXT("CGeneric_NPC"),         &FElysiumNpc::GenericNpcTroikaPrecache },
		{ TEXT("0x1035ade0"), TEXT("CGeneric_NPC_bathack"), &FElysiumNpc::GenericNpcBathackPrecache },
		{ TEXT("0x1035b5d0"), TEXT("CGenericSabbat_NPC"),   &FElysiumNpc::GenericSabbatNpcPrecache },
		{ TEXT("0x1035cb90"), TEXT("CNPC_VAndreiBlood"),    &FElysiumNpc::AndreiBloodPrecache },
		{ TEXT("0x10360bc0"), TEXT("CNPC_VAsianVampire"),   &FElysiumNpc::AsianVampirePrecache },
		{ TEXT("0x103637b0"), TEXT("CNPC_VBach"),           &FElysiumNpc::BachPrecache },
		{ TEXT("0x1036ae60"), TEXT("CNPC_VChangBros"),      &FElysiumNpc::ChangBrosPrecache },
		{ TEXT("0x10378470"), TEXT("CNPC_VGargoyle"),       &FElysiumNpc::GargoylePrecache },
		{ TEXT("0x1037b1a0"), TEXT("CNPC_VGhoulCroucher"),  &FElysiumNpc::GhoulCroucherPrecache },
		{ TEXT("0x1037f960"), TEXT("CNPC_VHengeyokai"),     &FElysiumNpc::HengeyokaiPrecache },
		{ TEXT("0x1038aec0"), TEXT("CNPC_VManBat"),         &FElysiumNpc::ManBatPrecache },
		{ TEXT("0x10392660"), TEXT("CNPC_VMingXiao"),       &FElysiumNpc::MingXiaoPrecache },
		{ TEXT("0x1039c220"), TEXT("CNPC_VMingXiaoTentacle"), &FElysiumNpc::MingXiaoTentaclePrecache },
		{ TEXT("0x103a03e0"), TEXT("CNPC_VNewscaster"),     &FElysiumNpc::NewscasterPrecache },
		{ TEXT("0x103a6ab0"), TEXT("CNPC_VSabbatLeader"),   &FElysiumNpc::SabbatLeaderPrecache },
		{ TEXT("0x103ae540"), TEXT("CNPC_VSheriffMan"),     &FElysiumNpc::SheriffManPrecache },
		{ TEXT("0x103b41e0"), TEXT("CNPC_VTest"),           &FElysiumNpc::TestNpcPrecache },
		{ TEXT("0x103b8fa0"), TEXT("CNPC_VTzimisce"),       &FElysiumNpc::TzimiscePrecache },
		{ TEXT("0x103c1400"), TEXT("CNPC_VTzimisceHeadClaw"), &FElysiumNpc::TzimisceHeadClawPrecache },
		{ TEXT("0x103c31e0"), TEXT("CNPC_VTzimisceRunner"), &FElysiumNpc::TzimisceRunnerPrecache },
		{ TEXT("0x103cb2a0"), TEXT("CNPC_VWerewolf"),       &FElysiumNpc::WerewolfPrecache },
		{ TEXT("0x103df120"), TEXT("CNPC_VZombie"),         &FElysiumNpc::ZombiePrecache },

		// Two slot-104 overrides story 29c-1 already ported as PURE bodies in family Lifecycle and
		// left unwired, because slot 104 itself was a generated stub then. They are wired here — the
		// arms are theirs and are cited, not re-recovered.
		{ TEXT("0x1034aa40"), TEXT("CGenericNPC"),          &FElysiumNpc::GenericNpcLinePrecache },
		{ TEXT("0x103689c0"), TEXT("CNPC_VCamera"),         &FElysiumNpc::CameraPrecache },

		// The three maker bodies. `FElysiumNpcMaker::Precache` carries them; see the struct.
		{ TEXT("0x1034b160"), TEXT("CNPCMaker"),            nullptr },
		{ TEXT("0x1034c180"), TEXT("CNPCMaker_Fleshpile"),  nullptr },
		{ TEXT("0x1034cde0"), TEXT("CNPCMaker_Zombie"),     nullptr },
	};

	// Retail's non-virtual thunk: while slot 104's species body runs, slot 104's dispatcher answers
	// "no species body" so the arm's own chain call reaches the Troika body directly.
	if (SpeciesDispatchingSlot == GPrecacheSlot)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GPrecacheSlot);
	if (Override == nullptr)
	{
		// `RetailClass()` is null for a classname the census claims nothing for — `npc_VCop`'s own
		// recovered answer — and a class with no slot-104 row inherits the Troika body, which is
		// exactly what returning false runs.
		return false;
	}
	for (const FPrecache10Arm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		if (Arm.Body == nullptr)
		{
			return true;
		}
		const FSpeciesDispatchScope Scope(*this, GPrecacheSlot);
		(this->*Arm.Body)();
		return true;
	}
	// Unreachable: `Elysium.Substrate.NpcKernelPrecache10.ArmCoverage` asserts the table above
	// carries every slot-104 override row the census holds, which is what stops a class this file
	// does not know from silently taking the Troika body.
	return false;
}

// -------------------------------------------------------------------------------------------------
// The species arms, in retail address order.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::CrowPrecache()
{
	// `CNPC_Crow::Precache` `0x10358ec0` — 24 bytes, and the ONLY arm in the band that chains the
	// base FIRST and then precaches. No model keyfield is read and no fallback is set, so a crow's
	// model is hard-coded and a map cannot override it.
	//
	// The chain is `CAI_BaseNPC::Precache` `0x1027bb50` — the BASE, not the Troika body: `CNPC_Crow`
	// is a `CAI_BaseNPC` in the census, so the Troika half (the model fallback, the alt equipment,
	// the dialogue directory, the disposition row and the coordinator bind) never runs for one.
	BasePrecache();
	Precache10Model(*this, GCrowModel, /*Preload=*/0);
}

void FElysiumNpc::GenericNpcTroikaPrecache()
{
	// `CGeneric_NPC::Precache` `0x10359f70` — the Troika-line `CGeneric_NPC` (`npc_generic`), a
	// different class from `CGenericNPC` whose `0x1034aa40` family Lifecycle carries.
	//
	// Model keyfield first: unset or empty falls back to the SABBAT FEMALE model, which is the
	// recovered oddity of this class.
	if (Model.IsEmpty())
	{
		Model = GSabbatFemaleModel;   // slot 212 `SetModelName`, `0x10629c00`
	}
	Precache10Model(*this, *Model, /*Preload=*/0);

	// Its own copies of the four metropolice/citizen names, in table order.
	Precache10Sound(*this, GMetropoliceAlert);
	Precache10Sound(*this, GMetropoliceSurprise);
	Precache10Sound(*this, GMetropoliceDie);
	Precache10SoundTable(*this, GCitizenPain, UE_ARRAY_COUNT(GCitizenPain));

	// And ONLY THEN the Troika body — this arm chains LAST, unlike every other Troika-line arm here.
	Precache();
}

void FElysiumNpc::GenericNpcBathackPrecache()
{
	// `CGeneric_NPC_bathack::Precache` `0x1035ade0` — `models/bats.mdl` hard-coded, its own private
	// copy of the same four sound names, and `CAI_BaseNPC::Precache` LAST. No model keyfield and no
	// fallback: this class is a `CAI_BaseNPC` in the census, so the base is what it chains.
	//
	// The decompiled C shows the surprise1 call with ONE argument; the LISTING (`1035ae13`) shows
	// `PUSH 0x0` before every one of the four. There is no stack quirk in this body.
	Precache10Model(*this, GBathackModel, /*Preload=*/0);
	Precache10Sound(*this, GMetropoliceAlert);
	Precache10Sound(*this, GMetropoliceSurprise);
	Precache10Sound(*this, GMetropoliceDie);
	Precache10SoundTable(*this, GCitizenPain, UE_ARRAY_COUNT(GCitizenPain));
	BasePrecache();
}

void FElysiumNpc::GenericSabbatNpcPrecache()
{
	// `CGenericSabbat_NPC::Precache` `0x1035b5d0`.
	//
	// `thunk_FUN_10207e60(this)` FIRST: resolve this entity's char template (`0x10207c40`), read the
	// template's `+0x78` (`0x101d4f20`) and `PrecacheModel` it with preload 0. The name is a seam
	// that answers the empty string — which is also retail's answer for a null template pointer.
	Precache10Model(*this, *CharTemplateModelName(), /*Preload=*/0);

	if (Model.IsEmpty())
	{
		Model = GSabbatFemaleModel;   // `PTR_..._1062a000`, the same string `CGeneric_NPC` falls to
	}
	Precache10Model(*this, *Model, /*Preload=*/0);

	Precache10Sound(*this, GMetropoliceAlert);
	Precache10Sound(*this, GMetropoliceSurprise);
	Precache10Sound(*this, GMetropoliceDie);
	Precache10SoundTable(*this, GCitizenPain, UE_ARRAY_COUNT(GCitizenPain));

	// `CAI_BaseNPC::Precache` LAST — a `CAI_BaseNPC` class, like the bathack.
	BasePrecache();
}

void FElysiumNpc::AndreiBloodPrecache()
{
	// `CNPC_VAndreiBlood::Precache` `0x1035cb90` — the Troika body FIRST, then three sounds and
	// three preload-1 emitters. The sound-before-emitter split and the preload flag are the data.
	Precache();
	Precache10Sound(*this, GAndreiTeleportOutSound);
	Precache10Sound(*this, GAndreiTeleportInSound);
	Precache10Sound(*this, GAndreiSummonSound);
	Precache10Particle(*this, GAndreiTeleportOutEmitter, /*Preload=*/1);
	Precache10Particle(*this, GAndreiTeleportInEmitter, /*Preload=*/1);
	Precache10Particle(*this, GAndreiSummonEmitter, /*Preload=*/1);
}

void FElysiumNpc::AsianVampirePrecache()
{
	// `CNPC_VAsianVampire::Precache` `0x10360bc0` — a scope-trace frame naming
	// `"CNPC_VAsianVampire::Precache"` with two empty operands, the Troika body, exactly one
	// `UTIL_PrecacheOther`, and the frame popped. That single weapon is the whole species payload.
	//
	// The scope-trace frame (`g_ScopeTraceStack`) is retail's VPROF-style profiling stack. It has no
	// port and nothing the kernel reads depends on it; the four arms here that push one say so and
	// carry no code for it.
	Precache();
	Precache10Other(*this, TEXT("item_w_avamp_blade"));
}

void FElysiumNpc::BachPrecache()
{
	// `CNPC_VBach::Precache` `0x103637b0` — the Troika body, then THREE weapons and then FIVE
	// sounds. The weapons-before-sounds order is this arm's fact.
	Precache();
	for (const TCHAR* Weapon : GBachWeapons)
	{
		Precache10Other(*this, Weapon);
	}
	for (const TCHAR* Sound : GBachSounds)
	{
		Precache10Sound(*this, Sound);
	}
}

void FElysiumNpc::ChangBrosPrecache()
{
	// `CNPC_VChangBros::Precache` `0x1036ae60` — one body filling `CNPC_VChangBros#104`,
	// `CNPC_VChangBrosBlade#104` and `CNPC_VChangBrosClaw#104`, which the address key makes one arm.
	// Scope-trace frame, the Troika body, seven preload-1 emitters, four weapons.
	Precache();
	for (const TCHAR* Emitter : GChangEmitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/1);
	}
	for (const TCHAR* Weapon : GChangWeapons)
	{
		Precache10Other(*this, Weapon);
	}
}

void FElysiumNpc::GargoylePrecache()
{
	// `CNPC_VGargoyle::Precache` `0x10378470` — the Troika body, nine gib models with preload 1 in
	// descending `.rdata` order, the 0x10-byte stomp table, the 0xc-byte exert table, one roar and
	// the fist.
	Precache();
	for (const TCHAR* GibModel : GGargoyleGibModels)
	{
		Precache10Model(*this, GibModel, /*Preload=*/1);
	}
	Precache10SoundTable(*this, GGargoyleStomps, UE_ARRAY_COUNT(GGargoyleStomps));
	Precache10SoundTable(*this, GGargoyleExerts, UE_ARRAY_COUNT(GGargoyleExerts));
	Precache10Sound(*this, GGargoyleRoar);
	Precache10Other(*this, GGargoyleWeapon);
}

void FElysiumNpc::GhoulCroucherPrecache()
{
	// `CNPC_VGhoulCroucher::Precache` `0x1037b1a0` — 55 bytes, the shortest arm in the band: the
	// Troika body, the claws, and the two Malkavian-mansion stalker models with preload 0. Those two
	// models are what makes the male/female split in its `SetModel` sibling (`0x1037b1f0`)
	// reachable.
	Precache();
	Precache10Other(*this, GGhoulCroucherWeapon);
	for (const TCHAR* StalkerModel : GGhoulCroucherModels)
	{
		Precache10Model(*this, StalkerModel, /*Preload=*/0);
	}
}

void FElysiumNpc::HengeyokaiPrecache()
{
	// `CNPC_VHengeyokai::Precache` `0x1037f960` — the Troika body, the 0x10-byte stomp table, the
	// 0xc-byte exert table, its model with preload 0, the freeze emitter with preload **0** rather
	// than the 1 Andrei, Chang and the ManBat use, and the fist.
	Precache();
	Precache10SoundTable(*this, GHengeyokaiStomps, UE_ARRAY_COUNT(GHengeyokaiStomps));
	Precache10SoundTable(*this, GHengeyokaiExerts, UE_ARRAY_COUNT(GHengeyokaiExerts));
	Precache10Model(*this, GHengeyokaiModel, /*Preload=*/0);
	Precache10Particle(*this, GHengeyokaiFreezeEmitter, /*Preload=*/0);
	Precache10Other(*this, GHengeyokaiWeapon);
}

void FElysiumNpc::ManBatPrecache()
{
	// `CNPC_VManBat::Precache` `0x1038aec0` — 280 bytes, the longest arm here: the Troika body, the
	// four-entry throw-object model table with preload 0, four preload-1 emitters (the exact four
	// the screech-cone body `0x1038e9c0` spawns), three 0xc-byte sound tables, two singles,
	// `sheriff_teleport_emitter` TWICE in a row — a retail duplicate that is kept — and the claw.
	Precache();
	for (const TCHAR* ThrowModel : GManBatThrowModels)
	{
		Precache10Model(*this, ThrowModel, /*Preload=*/0);
	}
	for (const TCHAR* Emitter : GManBatEmitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/1);
	}
	Precache10SoundTable(*this, GManBatWingflaps, UE_ARRAY_COUNT(GManBatWingflaps));
	Precache10SoundTable(*this, GManBatExerts, UE_ARRAY_COUNT(GManBatExerts));
	Precache10SoundTable(*this, GManBatFlyBys, UE_ARRAY_COUNT(GManBatFlyBys));
	Precache10Sound(*this, GManBatScreech);
	Precache10Sound(*this, GManBatFall);
	Precache10Particle(*this, GSheriffTeleportEmitter, /*Preload=*/1);
	Precache10Particle(*this, GSheriffTeleportEmitter, /*Preload=*/1);
	Precache10Other(*this, GManBatWeapon);
}

void FElysiumNpc::MingXiaoPrecache()
{
	// `CNPC_VMingXiao::Precache` `0x10392660` — the Troika body, ELEVEN emitters all with preload
	// **0**, one move sound, three weapons.
	Precache();
	for (const TCHAR* Emitter : GMingXiaoEmitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	Precache10Sound(*this, GMingXiaoMoveSound);
	for (const TCHAR* Weapon : GMingXiaoWeapons)
	{
		Precache10Other(*this, Weapon);
	}
}

void FElysiumNpc::MingXiaoTentaclePrecache()
{
	// `CNPC_VMingXiaoTentacle::Precache` `0x1039c220` — the model fallback runs BEFORE the chain,
	// which no other arm does, and three `PrecacheModel` indices are STORED.
	if (Model.IsEmpty())
	{
		Model = GTentacleFallbackModel;   // slot 212, `0x1064a340`
	}
	Precache();

	// The engine returns a model index from each call. This runtime has no model-index space, so the
	// three words take the index of the request in `PrecacheLog` — a stable, distinct number per
	// call with the same identity semantics the later mode-change bodies need ("is the current model
	// the grub or the proxy"), and NOT a claim about retail's numbering. The first two calls push
	// the SAME string (`0x1064a2f0`), so retail's own two indices are equal; that is reproduced by
	// recording the equality explicitly rather than by the log position.
	Precache10Model(*this, GTentacleBabyModel, /*Preload=*/0);
	ModeIndexTentacleToGrub = PrecacheLog.Num() - 1;
	Precache10Model(*this, GTentacleBabyModel, /*Preload=*/0);
	ModeIndexGrub = ModeIndexTentacleToGrub;   // retail: the same model, so the same index
	Precache10Model(*this, GTentacleTransformModel, /*Preload=*/0);
	ModeIndexGrubToProxy = PrecacheLog.Num() - 1;

	for (const TCHAR* Emitter : GTentacleEmitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	for (const TCHAR* Sound : GTentacleSounds)
	{
		Precache10Sound(*this, Sound);
	}
}

void FElysiumNpc::NewscasterPrecache()
{
	// `CNPC_VNewscaster::Precache` `0x103a03e0` — the Troika body, then the news/tv conversation
	// directory globbed twice: `.mp3` FIRST and `.wav` second, the reverse of the Troika body's own
	// pair. Both calls pass `bStarPrefix` CLEAR and the precache flag 0 (`PUSH 0x0 / PUSH 0x0`),
	// where the Troika body passes 1 and 0 — three call sites of one function, three argument pairs.
	Precache();
	PrecacheDirectory(GNewscasterSoundDir, GExtMp3, /*bStarPrefix=*/false, /*Flag=*/0);
	PrecacheDirectory(GNewscasterSoundDir, GExtWav, /*bStarPrefix=*/false, /*Flag=*/0);
}

void FElysiumNpc::SabbatLeaderPrecache()
{
	// `CNPC_VSabbatLeader::Precache` `0x103a6ab0` — scope-trace frame, the Troika body, two models
	// with preload 1, the seven-entry step table, the three-entry exert table, seven singles, two
	// preload-1 emitters, and the attack weapon.
	Precache();
	for (const TCHAR* AndreiModel : GSabbatLeaderModels)
	{
		Precache10Model(*this, AndreiModel, /*Preload=*/1);
	}
	Precache10SoundTable(*this, GAndreiTransformedSteps, UE_ARRAY_COUNT(GAndreiTransformedSteps));
	Precache10SoundTable(*this, GAndreiTransformedExerts, UE_ARRAY_COUNT(GAndreiTransformedExerts));
	for (const TCHAR* Single : GAndreiTransformedSingles)
	{
		Precache10Sound(*this, Single);
	}
	Precache10Particle(*this, GAndreiPowerupEmitter, /*Preload=*/1);
	Precache10Particle(*this, GAndreiBlastEmitter, /*Preload=*/1);
	Precache10Other(*this, GSabbatLeaderWeapon);
}

void FElysiumNpc::SheriffManPrecache()
{
	// `CNPC_VSheriffMan::Precache` `0x103ae540` — scope-trace frame, the Troika body, the manbat
	// model with preload 1, the landblast emitter once and `sheriff_teleport_emitter` TWICE from the
	// identical `.rdata` cell `0x10642adc` (the same retail duplicate the ManBat arm keeps), and the
	// sword.
	Precache();
	Precache10Model(*this, GSheriffModel, /*Preload=*/1);
	Precache10Particle(*this, GSheriffLandblastEmitter, /*Preload=*/1);
	Precache10Particle(*this, GSheriffTeleportEmitter, /*Preload=*/1);
	Precache10Particle(*this, GSheriffTeleportEmitter, /*Preload=*/1);
	Precache10Other(*this, GSheriffWeapon);
}

void FElysiumNpc::TestNpcPrecache()
{
	// `CNPC_VTest::Precache` `0x103b41e0` — twelve sounds and only THEN the Troika body. Base-last,
	// like `CNPC_VTzimisce` and unlike its siblings.
	//
	// The decompiled C shows the surprise1 call with ONE argument; the LISTING (`103b427f`) shows
	// `PUSH 0x0` before all twelve. The checklist's "retail stack quirk" is a decompiler artifact
	// and is NOT reproduced.
	Precache10Sound(*this, GTestDeath);
	Precache10Sound(*this, GTestAlert);
	Precache10Sound(*this, GTestIdle);
	Precache10SoundTable(*this, GTestPains, UE_ARRAY_COUNT(GTestPains));
	Precache10Sound(*this, GTestFear);
	Precache10Sound(*this, GTestLostEnemy);
	Precache10Sound(*this, GTestFoundEnemy);
	Precache10Sound(*this, GTestSurprise);
	Precache10Sound(*this, GTestKnockout);
	Precache();
}

void FElysiumNpc::TzimiscePrecache()
{
	// `CNPC_VTzimisce::Precache` `0x103b8fa0` — the six-entry spiderchick footstep table, the
	// three-entry swish table, the melee weapon, and only then the Troika body. Base-last.
	// Oracle: `docs/vtmb/footsteps.md`.
	Precache10SoundTable(*this, GSpiderchickFootsteps, UE_ARRAY_COUNT(GSpiderchickFootsteps));
	Precache10SoundTable(*this, GSpiderchickSwishes, UE_ARRAY_COUNT(GSpiderchickSwishes));
	Precache10Other(*this, GTzimisceWeapon);
	Precache();
}

void FElysiumNpc::TzimisceHeadClawPrecache()
{
	// `CNPC_VTzimisceHeadClaw::Precache` `0x103c1400` — the Troika body, the four preload-1 emitters
	// the slot-332 grab body later spawns by name, the fat guy's footsteps as TWO tables of two,
	// his three exerts, the two Sluge singles, and two weapons.
	Precache();
	for (const TCHAR* Emitter : GTzim2Emitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/1);
	}
	Precache10SoundTable(*this, GFatGuyFootStepsA, UE_ARRAY_COUNT(GFatGuyFootStepsA));
	Precache10SoundTable(*this, GFatGuyFootStepsB, UE_ARRAY_COUNT(GFatGuyFootStepsB));
	Precache10SoundTable(*this, GFatGuyExerts, UE_ARRAY_COUNT(GFatGuyExerts));
	Precache10Sound(*this, GFatGuySlugHit);
	Precache10Sound(*this, GFatGuySlugAffected);
	for (const TCHAR* Weapon : GTzim2Weapons)
	{
		Precache10Other(*this, Weapon);
	}
}

void FElysiumNpc::TzimisceRunnerPrecache()
{
	// `CNPC_VTzimisceRunner::Precache` `0x103c31e0` — the Troika body, then four tables (2, 2, 4, 3)
	// and the claw. No field writes.
	Precache();
	Precache10SoundTable(*this, GRunnerStepsA, UE_ARRAY_COUNT(GRunnerStepsA));
	Precache10SoundTable(*this, GRunnerStepsB, UE_ARRAY_COUNT(GRunnerStepsB));
	Precache10SoundTable(*this, GRunnerBreaths, UE_ARRAY_COUNT(GRunnerBreaths));
	Precache10SoundTable(*this, GRunnerExerts, UE_ARRAY_COUNT(GRunnerExerts));
	Precache10Other(*this, GRunnerWeapon);
}

void FElysiumNpc::WerewolfPrecache()
{
	// `CNPC_VWerewolf::Precache` `0x103cb2a0` — a scope-trace frame carrying `m_iName` (`+0x26c`,
	// `"NULL ENTITY"` when `this` is null, which C++ cannot reach), the Troika body, TWO directory
	// globs, THREE state writes, the footstep table, the attacks weapon and two singles.
	Precache();

	// Both globs pass `.wav` only, `bStarPrefix` CLEAR and the precache flag **1** (`PUSH 0x1 /
	// PUSH 0x0` — the reverse of the Troika body's pair). The observatory directory is precached by
	// the werewolf because the Observatory fight is where one stands.
	PrecacheDirectory(GWerewolfSoundDir, GExtWav, /*bStarPrefix=*/false, /*Flag=*/1);
	PrecacheDirectory(GObservatorySoundDir, GExtWav, /*bStarPrefix=*/false, /*Flag=*/1);

	// The sound-group binding, in retail's write order: the table index FIRST, then the group name,
	// then the row the name resolves to. The same triple `CNPC_VZombie::SetModel` makes.
	VSoundTableIndex = GWerewolfVSoundTableIndex;             // +0x00bc := 2
	VSoundGroupName = GWerewolfSoundGroup;                    // +0x00c0 := "Werewolf"
	VSoundGroupRow = VSoundGroupRowFor(*VSoundGroupName);     // +0x00b4 := 0x101f55a0(...)

	Precache10SoundTable(*this, GWerewolfFootsteps, UE_ARRAY_COUNT(GWerewolfFootsteps));
	Precache10Other(*this, GWerewolfWeapon);
	Precache10Sound(*this, GWerewolfTeleOut);
	Precache10Sound(*this, GWerewolfTeleIn);
}

void FElysiumNpc::ZombiePrecache()
{
	// `CNPC_VZombie::Precache` `0x103df120` — the Troika body, the two headshot emitters with
	// preload **0**, and the fists. The two emitters are the assets the zombie head-damage arm
	// (`CNPC_VZombie::OnTakeDamage` `0x103e06d0`) names.
	Precache();
	for (const TCHAR* Emitter : GZombieEmitters)
	{
		Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	Precache10Other(*this, GZombieWeapon);
}

// -------------------------------------------------------------------------------------------------
// The two arms story 29c-1 ported and left unwired.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::GenericNpcLinePrecache()
{
	// `CGenericNPC::Precache` `0x1034aa40`. The BODY is family Lifecycle's
	// `GenericNpcPrecache(model, out)` — the three-entry `PTR_s_weapons_ar2_ar2_fire1_wav_106244c0`
	// table, then this entity's own model — and it is called rather than re-recovered here. What
	// this arm adds is the wiring: slot 104 was a generated stub when that body landed, so nothing
	// ran it.
	//
	// `CGenericNPC` is a `CAI_BaseNPC` line class and the body chains NOTHING: no base call at all,
	// which is why this arm ends where it ends.
	TArray<FPrecacheRequest> Requests;
	GenericNpcPrecache(Model, Requests);
	for (const FPrecacheRequest& Request : Requests)
	{
		FPrecacheOp Op;
		Op.Channel = Request.bModel ? EPrecacheChannel::Model : EPrecacheChannel::Sound;
		Op.Name = Request.Name;
		IssuePrecache(Op);
	}
}

void FElysiumNpc::CameraPrecache()
{
	// `CNPC_VCamera::Precache` `0x103689c0`, shared with `CNPC_VCameraSecurity`. The model fallback
	// is family Lifecycle's `CameraPrecacheModel` (`models/null.mdl` when the keyfield is unset or
	// empty) and is called rather than re-recovered; the rest of the body is the TAIL that family
	// explicitly left for "a later story", which is this one.
	Model = CameraPrecacheModel(Model);
	Precache10Model(*this, *Model, /*Preload=*/0);

	// The slot-452 reject arm, byte for byte the one `0x1027bb50` runs — except that retail reaches
	// `Msg` here and `DevMsg` there, the same format string `0x105cd21c`. Unreachable for the same
	// reason: `LoadedSchedules` answers true for every class in this runtime.
	if (!LoadedSchedules())
	{
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("ERROR: Rejecting spawn of %s as error in NPC's schedules."), *DebugString());
		Kill();
		return;
	}

	// `m_iInterestingPlaceGroups = 0` (`+0x62dc`) — the camera clears its interesting-place group
	// mask at precache, so `AcceptsAmbientGroup` answers false for every place and a camera never
	// claims one. The authored STRING beside it is left alone: retail's write is to the parsed int,
	// and `0x10298910` has already run off the keyvalue by now.
	InterestingPlaceGroupMask = 0;

	// SEAM, restated from family Lifecycle: the AI-node link-table integrity check
	// (`0x102f9970` / `0x102f9920` / `0x102f9950`) and its five-line `"is being spawned after links
	// have been..."` `DevMsg`. This substrate stands no AI node graph and no link table, so there is
	// nothing to check and nothing the kernel reads changes.
}
