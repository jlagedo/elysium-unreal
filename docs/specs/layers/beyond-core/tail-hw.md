# Tail — Hollywood (`hw_*`)

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

201 open, partial or likely-open functions that only these maps reach (9 KB).
Their witnesses are records on their own maps (`stage: map:<map>`); a map not baked yet is baked first.

## The maps

| map | baked | classes only this group spawns |
|---|---|---|
| `hw_609_1` | yes | CItemGJumblesFlyer, COccultHealRate, CWeaponMelee_HengeyokaiFist, npc VAnimal |
| `hw_ash_sewer_1` | yes | — |
| `hw_asphole_1` | yes | — |
| `hw_cemetery_1` | yes | — |
| `hw_chateau_1` | yes | npc VGargoyle |
| `hw_chinese_1` | yes | CItemGGarysFilm, npc VGargoyle |
| `hw_hub_1` | yes | — |
| `hw_jewelry_1` | yes | — |
| `hw_luckystar_1` | yes | CItemGJunkyardBusinesscard |
| `hw_metalhead_1` | yes | — |
| `hw_netcafe_1` | yes | CItemGHorrorTape_2, CResearchBook_HG_COMP |
| `hw_redspot_1` | yes | — |
| `hw_sinbin_1` | yes | — |
| `hw_tawni_1` | yes | — |
| `hw_vesuvius_1` | yes | — |
| `hw_warrens_1` | yes | CItemGWarrClipboard, CItemGWarrLedger1 |
| `hw_warrens_2` | yes | — |
| `hw_warrens_2b` | yes | — |
| `hw_warrens_3` | yes | CItemGWarrLedger2 |
| `hw_warrens_4` | yes | CItemGWarrens4Passkey |
| `hw_warrens_5` | yes | CBlueBloodPack, CItemGBertramsCD, COccultPassiveDurations |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | effects_world | `0x10113d10` | CFunc_Dust::Spawn | 245 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | open |
| L0 entity | effects_world | `0x10113680` | CFunc_Dust::FUN_10113680 | 6 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x101136a0` | CFunc_Dust::FUN_101136a0 | 3 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x10113aa0` | CFunc_Dust::FUN_10113aa0 | 6 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x10113c10` | CFunc_Dust::FUN_10113c10 | 8 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x10113c60` | CFunc_DustMotes::vfunc5 | 30 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x10113e70` | CFunc_Dust::FUN_10113e70 | 202 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x10113cf0` | Global::FUN_10113cf0 | 11 | ch_lotus_1, hw_ash_sewer_1, hw_cemetery_1, hw_chinese_1, hw_warrens_3, la_expipe_1, la_museum_1, sm_oceanhouse_2, sm_warehouse_1, sp_giovanni_5 | likely open |
| L0 entity | effects_world | `0x100f5710` | CEnvShooter::vfunc82 | 6 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f5880` | CEnvShooter::vfunc241 | 271 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f59e0` | CEnvShooter::vfunc242 | 144 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f5ad0` | CEnvShooter::vfunc5 | 30 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f54c0` | Global::FUN_100f54c0 | 196 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f55d0` | Global::FUN_100f55d0 | 26 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100f5600` | Global::FUN_100f5600 | 13 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | likely open |
| L0 entity | effects_world | `0x100580e0` | CSteamJet::Spawn | 65 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | effects_world | `0x10058150` | CSteamJet::Use | 94 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | effects_world | `0x10057bf0` | CSteamJet::vfunc80 | 6 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x10057c10` | CSteamJet::vfunc81 | 3 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x10058050` | CSteamJet::vfunc82 | 6 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x10058240` | CSteamJet::vfunc5 | 30 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x1042b540` | CSteamJet::FUN_1042b540 | 6 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x1042b5c0` | CSteamJet::FUN_1042b5c0 | 131 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | effects_world | `0x1042b680` | CSteamJet::FUN_1042b680 | 20 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | entity_core | `0x100ae150` | CBaseEntity::Remove | 123 | hw_hub_1, hw_vesuvius_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | physics | `0x10158af0` | Global::FUN_10158af0 | 57 | ch_fishmarket_1, ch_temple_4, hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_ventruetower_3 | likely open |
| L0 entity | physics | `0x100f4730` | CGibShooter::Use | 35 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | open |
| L0 entity | physics | `0x100f47b0` | CGibShooter::Spawn | 242 | hw_ash_sewer_1, hw_chinese_1, la_ventruetower_1b, la_ventruetower_3, sm_warehouse_1, sp_observatory_2 | open |
| L0 entity | physics | `0x10213ae0` | CPropHaunted::Spawn | 78 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10213b70` | CPropHaunted::UpdateOnRemove | 179 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10213d90` | CPropHaunted::StartTouch | 40 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10213dd0` | CPropHaunted::VPhysicsShadowCollision | 97 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10213e60` | CPropHaunted::VPhysicsCollision | 97 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x102146d0` | CPropHaunted::InputFlingNow | 19 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10214700` | CPropHaunted::InputShakeAndFling | 8 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10214720` | CPropHaunted::InputStopShaking | 22 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10214750` | CPropHaunted::InputStartShaking | 22 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | open |
| L0 entity | physics | `0x10213040` | CPropHaunted::vfunc82 | 6 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10213a60` | CPropHaunted::vfunc5 | 87 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10213b50` | CPropHaunted::vfunc265 | 1 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214010` | CPropHaunted::vfunc266 | 68 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10213ef0` | Global::FUN_10213ef0 | 190 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214080` | Global::FUN_10214080 | 289 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214200` | Global::FUN_10214200 | 65 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214780` | Global::FUN_10214780 | 287 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x102148f0` | Global::FUN_102148f0 | 236 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214a20` | Global::FUN_10214a20 | 527 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | physics | `0x10214cc0` | Global::FUN_10214cc0 | 113 | hw_609_1, la_hospital_1, sm_oceanhouse_2, sp_giovanni_5 | likely open |
| L0 entity | triggers | `0x101c89c0` | CTriggerPush::InputSetTargetSpeed | 118 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | triggers | `0x101c8a60` | CTriggerPush::InputSetAcceleration | 35 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | triggers | `0x101c8aa0` | CTriggerPush::Spawn | 133 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | triggers | `0x101c8c40` | CTriggerPush::Touch | 902 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | triggers | `0x101c87c0` | CTriggerPush::vfunc82 | 6 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | triggers | `0x101c8910` | CTriggerPush::vfunc5 | 76 | ch_fulab_1, hw_chateau_1, hw_vesuvius_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, la_malkavian_4, sm_junkyard_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | triggers | `0x101cbd80` | CSmallHullTrigger::Spawn | 16 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2 | open |
| L0 entity | triggers | `0x101cbda0` | CSmallHullTrigger::StartTouch | 33 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2 | open |
| L0 entity | triggers | `0x101cbde0` | CSmallHullTrigger::EndTouch | 47 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2 | open |
| L0 entity | triggers | `0x101cbc60` | CSmallHullTrigger::vfunc82 | 6 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2 | likely open |
| L0 entity | triggers | `0x101cc4b0` | CSmallHullTrigger::vfunc5 | 76 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2 | likely open |
| L2 character | combat | `0x10322800` | CBaseCombatCharacter::SetOccultPowerFlag | 134 | hw_609_1, hw_warrens_5, la_chantry_1, la_malkavian_2, sm_oceanhouse_2, sp_giovanni_3 | open |
| L2 character | combat | `0x10338950` | CBaseCombatCharacter::IsBloodPoolFull | 336 | hw_warrens_5, la_malkavian_3 | open |
| L2 character | inventory_ui_items | `0x103f58f0` | CItemMWallet::vfunc80 | 6 | hw_luckystar_1, la_confession_1 | likely open |
| L2 character | inventory_ui_items | `0x103f5910` | CItemMWallet::vfunc81 | 3 | hw_luckystar_1, la_confession_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd8a0` | CItemGDrugsPerscriptionBottle::vfunc80 | 6 | hw_luckystar_1, la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd8c0` | CItemGDrugsPerscriptionBottle::vfunc81 | 3 | hw_luckystar_1, la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x10409b20` | CItemMWallet::vfunc5 | 30 | hw_luckystar_1, la_confession_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a240` | CItemGDrugsPerscriptionBottle::vfunc5 | 30 | hw_luckystar_1, la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x103fc4e0` | CBlueBloodPack::Deploy | 203 | hw_warrens_5 | open |
| L2 character | inventory_ui_items | `0x103f6f30` | CResearchBook_HG_COMP::vfunc80 | 6 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6f50` | CResearchBook_HG_COMP::vfunc81 | 3 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x103fc6b0` | CBlueBloodPack::vfunc80 | 6 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x103fc6d0` | CBlueBloodPack::vfunc81 | 3 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x103ffe40` | CItemGJumblesFlyer::vfunc80 | 6 | hw_609_1 | likely open |
| L2 character | inventory_ui_items | `0x103ffe60` | CItemGJumblesFlyer::vfunc81 | 3 | hw_609_1 | likely open |
| L2 character | inventory_ui_items | `0x10400650` | CItemGHorrorTape_2::vfunc80 | 6 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x10400670` | CItemGHorrorTape_2::vfunc81 | 3 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x10402bf0` | CItemGWarrens4Passkey::vfunc80 | 6 | hw_warrens_4 | likely open |
| L2 character | inventory_ui_items | `0x10402c10` | CItemGWarrens4Passkey::vfunc81 | 3 | hw_warrens_4 | likely open |
| L2 character | inventory_ui_items | `0x10404170` | CItemGWarrClipboard::vfunc80 | 6 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x10404190` | CItemGWarrClipboard::vfunc81 | 3 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x10404420` | CItemGWarrLedger1::vfunc80 | 6 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x10404440` | CItemGWarrLedger1::vfunc81 | 3 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x104046d0` | CItemGWarrLedger2::vfunc80 | 6 | hw_warrens_3 | likely open |
| L2 character | inventory_ui_items | `0x104046f0` | CItemGWarrLedger2::vfunc81 | 3 | hw_warrens_3 | likely open |
| L2 character | inventory_ui_items | `0x10404ee0` | CItemGJunkyardBusinesscard::vfunc80 | 6 | hw_luckystar_1 | likely open |
| L2 character | inventory_ui_items | `0x10404f00` | CItemGJunkyardBusinesscard::vfunc81 | 3 | hw_luckystar_1 | likely open |
| L2 character | inventory_ui_items | `0x10405f00` | CItemGGarysFilm::vfunc80 | 6 | hw_chinese_1 | likely open |
| L2 character | inventory_ui_items | `0x10405f20` | CItemGGarysFilm::vfunc81 | 3 | hw_chinese_1 | likely open |
| L2 character | inventory_ui_items | `0x10406710` | CItemGBertramsCD::vfunc80 | 6 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x10406730` | CItemGBertramsCD::vfunc81 | 3 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x10409c40` | CResearchBook_HG_COMP::vfunc5 | 30 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a120` | CBlueBloodPack::vfunc5 | 30 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x1040a4e0` | CItemGJumblesFlyer::vfunc5 | 30 | hw_609_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a570` | CItemGHorrorTape_2::vfunc5 | 30 | hw_netcafe_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a810` | CItemGWarrens4Passkey::vfunc5 | 30 | hw_warrens_4 | likely open |
| L2 character | inventory_ui_items | `0x1040a990` | CItemGWarrClipboard::vfunc5 | 30 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a9c0` | CItemGWarrLedger1::vfunc5 | 30 | hw_warrens_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a9f0` | CItemGWarrLedger2::vfunc5 | 30 | hw_warrens_3 | likely open |
| L2 character | inventory_ui_items | `0x1040aa80` | CItemGJunkyardBusinesscard::vfunc5 | 30 | hw_luckystar_1 | likely open |
| L2 character | inventory_ui_items | `0x1040aba0` | CItemGGarysFilm::vfunc5 | 30 | hw_chinese_1 | likely open |
| L2 character | inventory_ui_items | `0x1040ac30` | CItemGBertramsCD::vfunc5 | 30 | hw_warrens_5 | likely open |
| L2 character | inventory_ui_items | `0x103fc5f0` | Global::FUN_103fc5f0 | 57 | hw_warrens_5 | likely open |
| L2 character | rpg | `0x103f8fc0` | COccultRegen::FUN_103f8fc0 | 63 | hw_609_1, hw_warrens_5, la_chantry_1, la_malkavian_2, sp_giovanni_3 | likely open |
| L2 character | rpg | `0x103f9010` | COccultRegen::FUN_103f9010 | 53 | hw_609_1, hw_warrens_5, la_chantry_1, la_malkavian_2, sp_giovanni_3 | likely open |
| L2 character | rpg | `0x103fb7d0` | COccultPassiveDurations::vfunc80 | 6 | hw_warrens_5 | likely open |
| L2 character | rpg | `0x103fb7f0` | COccultPassiveDurations::vfunc81 | 3 | hw_warrens_5 | likely open |
| L2 character | rpg | `0x103fbad0` | COccultHealRate::vfunc80 | 6 | hw_609_1 | likely open |
| L2 character | rpg | `0x103fbaf0` | COccultHealRate::vfunc81 | 3 | hw_609_1 | likely open |
| L2 character | rpg | `0x1040a060` | COccultPassiveDurations::vfunc5 | 30 | hw_warrens_5 | likely open |
| L2 character | rpg | `0x1040a090` | COccultHealRate::vfunc5 | 30 | hw_609_1 | likely open |
| L2 character | weapons | `0x103e7c90` | CWeaponMelee_SeveredArm::Spawn | 17 | hw_hub_1, sm_basement_1 | open |
| L2 character | weapons | `0x10234f60` | CWeaponRanged_Pistol_Glock::vfunc80 | 6 | hw_warrens_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x10234f80` | CWeaponRanged_Pistol_Glock::vfunc81 | 3 | hw_warrens_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x10235130` | CWeaponRanged_Pistol_Glock::vfunc362 | 6 | hw_warrens_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x10235150` | CWeaponRanged_Pistol_Glock::vfunc363 | 6 | hw_warrens_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x1023a3b0` | CWeaponRanged_Pistol_Glock::vfunc5 | 30 | hw_warrens_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e7a10` | CWeaponMelee_SeveredArm::vfunc80 | 6 | hw_hub_1, sm_basement_1 | likely open |
| L2 character | weapons | `0x103e7a30` | CWeaponMelee_SeveredArm::vfunc81 | 3 | hw_hub_1, sm_basement_1 | likely open |
| L2 character | weapons | `0x103e7be0` | CWeaponMelee_SeveredArm::vfunc362 | 6 | hw_hub_1, sm_basement_1 | likely open |
| L2 character | weapons | `0x103e7c00` | CWeaponMelee_SeveredArm::vfunc363 | 6 | hw_hub_1, sm_basement_1 | likely open |
| L2 character | weapons | `0x103ece10` | CWeaponMelee_SeveredArm::vfunc5 | 30 | hw_hub_1, sm_basement_1 | likely open |
| L2 character | weapons | `0x103eba10` | CWeaponMelee_HengeyokaiFist::vfunc80 | 6 | hw_609_1 | likely open |
| L2 character | weapons | `0x103eba30` | CWeaponMelee_HengeyokaiFist::vfunc81 | 3 | hw_609_1 | likely open |
| L2 character | weapons | `0x103ebbe0` | CWeaponMelee_HengeyokaiFist::vfunc362 | 6 | hw_609_1 | likely open |
| L2 character | weapons | `0x103ebc00` | CWeaponMelee_HengeyokaiFist::vfunc363 | 6 | hw_609_1 | likely open |
| L2 character | weapons | `0x103ebc90` | CWeaponMelee_HengeyokaiFist::vfunc373 | 4 | hw_609_1 | likely open |
| L2 character | weapons | `0x103ed020` | CWeaponMelee_HengeyokaiFist::vfunc5 | 30 | hw_609_1 | likely open |
| L4 NPC | makers | `0x1034bdc0` | CNPCMaker_Fleshpile::vfunc82 | 6 | hw_609_1, hw_jewelry_1, hw_warrens_3, hw_warrens_4 | likely open |
| L4 NPC | makers | `0x1034bfb0` | CNPCMaker_Fleshpile::vfunc5 | 65 | hw_609_1, hw_jewelry_1, hw_warrens_3, hw_warrens_4 | likely open |
| L4 NPC | makers | `0x1034c260` | CNPCMaker_Fleshpile::vfunc130 | 68 | hw_609_1, hw_jewelry_1, hw_warrens_3, hw_warrens_4 | likely open |
| L4 NPC | npc_species | `0x103c32a0` | CNPC_VTzimisceRunner::IsMonster | 3 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | open |
| L4 NPC | npc_species | `0x103c28a0` | CNPC_VTzimisceRunner::vfunc82 | 6 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c2a60` | CNPC_VTzimisceRunner::vfunc546 | 29 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c3040` | CNPC_VTzimisceRunner::vfunc399 | 3 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c3080` | CNPC_VTzimisceRunner::vfunc325 | 3 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c30a0` | CNPC_VTzimisceRunner::vfunc324 | 3 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c30e0` | CNPC_VTzimisceRunner::vfunc580 | 6 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c3120` | CNPC_VTzimisceRunner::vfunc5 | 134 | hw_609_1, hw_jewelry_1, hw_netcafe_1, hw_warrens_1, hw_warrens_2, hw_warrens_3, hw_warrens_4, la_library_1 | likely open |
| L4 NPC | npc_species | `0x103c1520` | CNPC_VTzimisceHeadClaw::IsMonster | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | open |
| L4 NPC | npc_species | `0x103c2810` | CNPC_VTzimisceHeadClaw::Save | 53 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | open |
| L4 NPC | npc_species | `0x103c0ad0` | CNPC_VTzimisceHeadClaw::vfunc82 | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c0c90` | CNPC_VTzimisceHeadClaw::vfunc546 | 29 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c12c0` | CNPC_VTzimisceHeadClaw::vfunc325 | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c12e0` | CNPC_VTzimisceHeadClaw::vfunc324 | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c1300` | CNPC_VTzimisceHeadClaw::vfunc580 | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c1340` | CNPC_VTzimisceHeadClaw::vfunc5 | 134 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103c2860` | CNPC_VTzimisceHeadClaw::vfunc127 | 40 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1, la_bradbury_2 | likely open |
| L4 NPC | npc_species | `0x103b9020` | CNPC_VTzimisce::IsMonster | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | open |
| L4 NPC | npc_species | `0x103ba230` | CNPC_VTzimisce::Classify | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | open |
| L4 NPC | npc_species | `0x103bfa30` | CNPC_VTzimisce::VPhysicsShadowCollision | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | open |
| L4 NPC | npc_species | `0x103b9e30` | Global::FUN_103b9e30 | 143 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | open |
| L4 NPC | npc_species | `0x1035eb10` | CNPC_VAnimal::FUN_1035eb10 | 6 | hw_609_1, hw_warrens_1, sm_beachhouse_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x103b6b60` | CNPC_VTzimisce::vfunc82 | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b6d90` | CNPC_VTzimisce::vfunc325 | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b6db0` | CNPC_VTzimisce::vfunc324 | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b6e50` | CNPC_VTzimisce::vfunc580 | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b6e90` | CNPC_VTzimisce::vfunc5 | 225 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b70f0` | CNPC_VTzimisce::vfunc546 | 29 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9270` | CNPC_VTzimisce::vfunc422 | 26 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9360` | CNPC_VTzimisce::vfunc489 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b95e0` | CNPC_VTzimisce::vfunc492 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9600` | CNPC_VTzimisce::vfunc493 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9620` | CNPC_VTzimisce::vfunc494 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9640` | CNPC_VTzimisce::vfunc495 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9680` | CNPC_VTzimisce::vfunc497 | 1 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103b9ef0` | CNPC_VTzimisce::vfunc509 | 3 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103ba000` | CNPC_VTzimisce::vfunc473 | 6 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x103ba250` | CNPC_VTzimisce::vfunc533 | 33 | hw_warrens_2b, hw_warrens_3, hw_warrens_4, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x10378660` | CNPC_VGargoyle::Classify | 6 | hw_chateau_1, hw_chinese_1 | open |
| L4 NPC | npc_species | `0x10379470` | CNPC_VGargoyle::IsMonster | 3 | hw_chateau_1, hw_chinese_1 | open |
| L4 NPC | npc_species | `0x1037a610` | CNPC_VGargoyle::ReceivesImpactDamage | 5 | hw_chateau_1, hw_chinese_1 | open |
| L4 NPC | npc_species | `0x103a5c80` | CNPC_VSabbatLeader::IsMonster | 3 | hw_609_1, la_bradbury_3 | open |
| L4 NPC | npc_species | `0x103a6ef0` | CNPC_VSabbatLeader::Classify | 94 | hw_609_1, la_bradbury_3 | open |
| L4 NPC | npc_species | `0x103a7760` | CNPC_VSabbatLeader::Event_Killed | 100 | hw_609_1, la_bradbury_3 | open |
| L4 NPC | npc_species | `0x1035cf80` | CNPC_VAndreiBlood::Restore | 101 | hw_609_1, la_bradbury_1 | open |
| L4 NPC | npc_species | `0x103a6e80` | CNPC_VSabbatLeader::vfunc127 | 71 | hw_609_1, la_bradbury_3 | open |
| L4 NPC | npc_species | `0x1035c230` | CNPC_VAndreiBlood::vfunc580 | 6 | hw_609_1, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x1035c270` | CNPC_VAndreiBlood::vfunc5 | 30 | hw_609_1, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x1035c2a0` | CNPC_VAndreiBlood::vfunc82 | 6 | hw_609_1, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x1035c460` | CNPC_VAndreiBlood::vfunc546 | 29 | hw_609_1, la_bradbury_1 | likely open |
| L4 NPC | npc_species | `0x10377910` | CNPC_VGargoyle::vfunc82 | 6 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x10377b00` | CNPC_VGargoyle::vfunc378 | 5 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x10377b60` | CNPC_VGargoyle::vfunc5 | 43 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x10377cd0` | CNPC_VGargoyle::vfunc546 | 29 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x10378640` | CNPC_VGargoyle::vfunc473 | 6 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x10378b60` | CNPC_VGargoyle::vfunc461 | 15 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x1037a5f0` | CNPC_VGargoyle::vfunc359 | 8 | hw_chateau_1, hw_chinese_1 | likely open |
| L4 NPC | npc_species | `0x103a5b50` | CNPC_VSabbatLeader::vfunc82 | 6 | hw_609_1, la_bradbury_3 | likely open |
| L4 NPC | npc_species | `0x103a5ca0` | CNPC_VSabbatLeader::vfunc580 | 6 | hw_609_1, la_bradbury_3 | likely open |
| L4 NPC | npc_species | `0x103a5ce0` | CNPC_VSabbatLeader::vfunc5 | 43 | hw_609_1, la_bradbury_3 | likely open |
| L4 NPC | npc_species | `0x103a5e50` | CNPC_VSabbatLeader::vfunc546 | 29 | hw_609_1, la_bradbury_3 | likely open |
| L4 NPC | npc_species | `0x1035f560` | CNPC_VAnimal::Classify | 6 | hw_609_1 | open |
| L4 NPC | npc_species | `0x1035ec10` | CNPC_VAnimal::vfunc580 | 6 | hw_609_1 | likely open |
| L4 NPC | npc_species | `0x1035ec50` | CNPC_VAnimal::vfunc5 | 30 | hw_609_1 | likely open |
| L4 NPC | npc_species | `0x1035edb0` | CNPC_VAnimal::vfunc546 | 29 | hw_609_1 | likely open |
| L4 NPC | social | `0x101358e0` | CLogicNPCCondition::InputTest | 59 | hw_tawni_1, la_ventruetower_1b | open |
| L4 NPC | social | `0x10135510` | CLogicNPCCondition::vfunc82 | 6 | hw_tawni_1, la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10135650` | CLogicNPCCondition::vfunc113 | 152 | hw_tawni_1, la_ventruetower_1b | likely open |
| L4 NPC | social | `0x101361c0` | CLogicNPCCondition::vfunc5 | 54 | hw_tawni_1, la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10135720` | Global::FUN_10135720 | 213 | hw_tawni_1, la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10135840` | Global::FUN_10135840 | 124 | hw_tawni_1, la_ventruetower_1b | likely open |
