# Tail — Downtown LA (`la_*`)

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

404 open, partial or likely-open functions that only these maps reach (30 KB).
Their witnesses are records on their own maps (`stage: map:<map>`); a map not baked yet is baked first.

## The maps

| map | baked | classes only this group spawns |
|---|---|---|
| `la_abandoned_building_1` | yes | — |
| `la_bradbury_1` | yes | CAI_ChangeTarget, CWeaponRanged_FlameThrower |
| `la_bradbury_2` | yes | CWeaponMelee_OccultBlade, CWeaponRanged_FlameThrower |
| `la_bradbury_3` | yes | CWeaponMelee_OccultBlade, CWeaponMelee_Torch |
| `la_chantry_1` | yes | CItemGGargoyleBook, COccultThaumDamage, CWeaponMelee_GargoyleFist |
| `la_confession_1` | yes | — |
| `la_crackhouse_1` | yes | — |
| `la_dane_1` | yes | CEnvFloatingCamera, CItemGEDanePrintReport, CItemGEDaneReport |
| `la_empire_1` | yes | — |
| `la_empire_2` | yes | CResearchBook_MG_BRAWL, CResearchBook_MG_FIN |
| `la_empire_3` | yes | CItemGBrotherhoodFlyer, CResearchBook_MG_BRAWL, CResearchBook_MG_FIN |
| `la_expipe_1` | yes | — |
| `la_hospital_1` | yes | CItemGMilligansBuisnesscard |
| `la_hub_1` | yes | CGameUI, CWeaponMelee_WerewolfAttacks, npc VScurrying |
| `la_library_1` | yes | CWeaponMelee_Claws_Protean5 |
| `la_malkavian_1` | yes | — |
| `la_malkavian_2` | yes | CFilterFeat, COccultFrenzy, CResearchBook_LG_DODGE, CWeaponMelee_ManBatClaw, CWeaponMelee_Tzimisce3Claw |
| `la_malkavian_3` | yes | CBloodPack, CElderVitaePack |
| `la_malkavian_3b` | yes | — |
| `la_malkavian_4` | yes | CTriggerImpact |
| `la_malkavian_5` | yes | — |
| `la_museum_1` | yes | CItemGPishaFetish, CResearchBook_MG_SEC, npc VCameraSecurity, npc VHumanCombatPatrol |
| `la_parkinggarage_1` | yes | CItemGLarryBriefcase |
| `la_plaguebearer_sewer_1` | yes | CItemGBrotherhoodFlyer, CPhysConstraintSystem, CPhysicsAnimlink |
| `la_skyline_1` | **no** | CItemGGarysPhoto, CItemGHannahsApptBook, CResearchBook_MG_MELEE, CWeaponMelee_ZombieFists |
| `la_ventruetower_1` | **no** | — |
| `la_ventruetower_1b` | **no** | CLogicSquadCondition |
| `la_ventruetower_2` | yes | — |
| `la_ventruetower_3` | yes | CWeaponMelee_Sheriff_Sword, npc VManBat, npc VSheriffMan |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | audio | `0x1022c220` | CPropRadio::Spawn | 502 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | open |
| L0 entity | audio | `0x1022c4a0` | CPropRadio::Activate | 344 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | open |
| L0 entity | audio | `0x1022c680` | CPropRadio::Use | 150 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | open |
| L0 entity | audio | `0x1022c9e0` | CPropRadio::LoadRadioData | 552 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | open |
| L0 entity | audio | `0x1022bba0` | CPropRadio::vfunc82 | 6 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | likely open |
| L0 entity | audio | `0x1022bca0` | CPropRadio::vfunc5 | 134 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | likely open |
| L0 entity | audio | `0x1022c660` | CPropRadio::vfunc117 | 6 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | likely open |
| L0 entity | audio | `0x1022d210` | Global::FUN_1022d210 | 58 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | likely open |
| L0 entity | audio | `0x1022d410` | Global::FUN_1022d410 | 103 | ch_ramen_1, hw_luckystar_1, hw_redspot_1, la_chantry_1, la_hub_1, la_skyline_1, sm_bailbonds_1, sm_pawnshop_1, sp_taxiride | likely open |
| L0 entity | effects_world | `0x10056a90` | CDynamicLight::Spawn | 175 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | open |
| L0 entity | effects_world | `0x10056460` | CDynamicLight::vfunc82 | 6 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | likely open |
| L0 entity | effects_world | `0x10056560` | CDynamicLight::vfunc80 | 6 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | likely open |
| L0 entity | effects_world | `0x10056580` | CDynamicLight::vfunc81 | 3 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | likely open |
| L0 entity | effects_world | `0x100568a0` | CDynamicLight::vfunc110 | 270 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | likely open |
| L0 entity | effects_world | `0x10056b70` | CDynamicLight::vfunc5 | 30 | ch_dragon_1, ch_lotus_1, hw_warrens_3, la_skyline_1, la_ventruetower_1, la_ventruetower_1b, la_ventruetower_2, sm_oceanhouse_1, sm_warehouse_1 | likely open |
| L0 entity | effects_world | `0x100badb0` | CBeam::SetModel | 83 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | open |
| L0 entity | effects_world | `0x100bae30` | CBeam::Spawn | 151 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | open |
| L0 entity | effects_world | `0x100fec40` | CEnvBeam::Spawn | 314 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | open |
| L0 entity | effects_world | `0x100fef30` | CEnvBeam::InputStrikeOnce | 8 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | open |
| L0 entity | effects_world | `0x100ba370` | CBeam::FUN_100ba370 | 6 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100ba390` | CBeam::FUN_100ba390 | 3 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bac90` | CBeam::FUN_100bac90 | 32 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bacc0` | CBeam::FUN_100bacc0 | 144 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bc130` | CBeam::FUN_100bc130 | 147 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100feb50` | CEnvBeam::vfunc82 | 6 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100fec10` | CEnvBeam::vfunc5 | 30 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100fee60` | CEnvBeam::vfunc113 | 46 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100ba1b0` | Global::FUN_100ba1b0 | 172 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bafc0` | Global::FUN_100bafc0 | 69 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bb030` | Global::FUN_100bb030 | 86 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bb270` | Global::FUN_100bb270 | 366 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bb440` | Global::FUN_100bb440 | 451 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bbbf0` | Global::FUN_100bbbf0 | 387 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bbdf0` | Global::FUN_100bbdf0 | 86 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100bbe70` | Global::FUN_100bbe70 | 81 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100ff1b0` | Global::FUN_100ff1b0 | 258 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x100ff310` | Global::FUN_100ff310 | 1991 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x10100b60` | Global::FUN_10100b60 | 543 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L0 entity | effects_world | `0x103e1740` | Global::FUN_103e1740 | 28 | la_bradbury_1, la_bradbury_2 | likely open |
| L0 entity | entity_core | `0x103e17b0` | Global::FUN_103e17b0 | 294 | la_bradbury_1, la_bradbury_2 | likely open |
| L0 entity | entity_core | `0x103e1960` | Global::FUN_103e1960 | 54 | la_bradbury_1, la_bradbury_2 | likely open |
| L0 entity | entity_core | `0x103e19b0` | Global::FUN_103e19b0 | 387 | la_bradbury_1, la_bradbury_2 | likely open |
| L0 entity | entity_core | `0x103e1bb0` | Global::FUN_103e1bb0 | 51 | la_bradbury_1, la_bradbury_2 | likely open |
| L0 entity | entity_io | `0x10134590` | CLogicCaseToggle::vfunc82 | 6 | la_bradbury_2, la_hub_1, la_library_1, sp_tutorial_1 | likely open |
| L0 entity | entity_io | `0x10136040` | CLogicCaseToggle::vfunc5 | 71 | la_bradbury_2, la_hub_1, la_library_1, sp_tutorial_1 | likely open |
| L0 entity | entity_io | `0x10108360` | CFilterFeat::vfunc241 | 250 | la_malkavian_2 | likely open |
| L0 entity | entity_io | `0x101084a0` | CFilterFeat::vfunc82 | 6 | la_malkavian_2 | likely open |
| L0 entity | entity_io | `0x10108710` | CFilterFeat::vfunc5 | 54 | la_malkavian_2 | likely open |
| L0 entity | physics | `0x1002c190` | CPhysFixed::vfunc241 | 275 | ch_fishmarket_1, la_abandoned_building_1, la_bradbury_3, la_hospital_1, la_parkinggarage_1, la_plaguebearer_sewer_1, la_skyline_1, sm_beachhouse_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | physics | `0x1002cb70` | CPhysFixed::vfunc5 | 30 | ch_fishmarket_1, la_abandoned_building_1, la_bradbury_3, la_hospital_1, la_parkinggarage_1, la_plaguebearer_sewer_1, la_skyline_1, sm_beachhouse_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | physics | `0x10192ab0` | CPhysicsPropContested::Spawn | 57 | la_abandoned_building_1, sm_hub_1 | open |
| L0 entity | physics | `0x101929d0` | CPhysicsPropContested::vfunc82 | 6 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x10192e60` | CPhysicsPropContested::vfunc39 | 235 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x10192f90` | CPhysicsPropContested::vfunc41 | 222 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x101930b0` | CPhysicsPropContested::vfunc32 | 20 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x10193130` | CPhysicsPropContested::vfunc35 | 18 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x10194510` | CPhysicsPropContested::vfunc5 | 65 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x10192b50` | Global::FUN_10192b50 | 615 | la_abandoned_building_1, sm_hub_1 | likely open |
| L0 entity | physics | `0x1002a170` | CPhysConstraintSystem::Spawn | 22 | la_plaguebearer_sewer_1 | open |
| L0 entity | physics | `0x1014f000` | CPhysicsAnimlink::Spawn | 402 | la_plaguebearer_sewer_1 | open |
| L0 entity | physics | `0x1002a090` | CPhysConstraintSystem::vfunc82 | 6 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1002cab0` | CPhysConstraintSystem::vfunc5 | 30 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1014edb0` | CPhysicsAnimlink::vfunc82 | 6 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1014ef60` | CPhysicsAnimlink::vfunc117 | 9 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1014ef80` | CPhysicsAnimlink::vfunc5 | 30 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1014eba0` | Global::FUN_1014eba0 | 18 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | physics | `0x1014efb0` | Global::FUN_1014efb0 | 63 | la_plaguebearer_sewer_1 | likely open |
| L0 entity | triggers | `0x101c92c0` | CTriggerTeleport::Touch | 831 | hw_warrens_2, la_bradbury_2, la_chantry_1, la_confession_1, la_malkavian_2, la_museum_1, la_skyline_1, sm_medical_1, sm_oceanhouse_2 | open |
| L0 entity | triggers | `0x101c9150` | CTriggerTeleport::vfunc82 | 6 | hw_warrens_2, la_bradbury_2, la_chantry_1, la_confession_1, la_malkavian_2, la_museum_1, la_skyline_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | triggers | `0x101c9230` | CTriggerTeleport::vfunc5 | 76 | hw_warrens_2, la_bradbury_2, la_chantry_1, la_confession_1, la_malkavian_2, la_museum_1, la_skyline_1, sm_medical_1, sm_oceanhouse_2 | likely open |
| L0 entity | triggers | `0x10231350` | CTriggerElectricBugaloo::Spawn | 16 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | open |
| L0 entity | triggers | `0x10231370` | CTriggerElectricBugaloo::StartTouch | 82 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | open |
| L0 entity | triggers | `0x102313f0` | CTriggerElectricBugaloo::EndTouch | 108 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | open |
| L0 entity | triggers | `0x102316d0` | CTriggerElectricBugaloo::Touch | 133 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | open |
| L0 entity | triggers | `0x10231200` | CTriggerElectricBugaloo::vfunc82 | 6 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x102312e0` | CTriggerElectricBugaloo::vfunc5 | 76 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10231480` | CTriggerElectricBugaloo::vfunc33 | 6 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x102314a0` | CTriggerElectricBugaloo::vfunc35 | 83 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10231520` | CTriggerElectricBugaloo::vfunc32 | 209 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10231640` | CTriggerElectricBugaloo::vfunc39 | 49 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10231690` | CTriggerElectricBugaloo::vfunc42 | 37 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10231790` | Global::FUN_10231790 | 66 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L0 entity | triggers | `0x10216d40` | CTriggerInventory::Spawn | 16 | la_ventruetower_2, sm_hub_1, sp_tutorial_1 | open |
| L0 entity | triggers | `0x10216d60` | CTriggerInventory::StartTouch | 119 | la_ventruetower_2, sm_hub_1, sp_tutorial_1 | open |
| L0 entity | triggers | `0x10216bc0` | CTriggerInventory::vfunc82 | 6 | la_ventruetower_2, sm_hub_1, sp_tutorial_1 | likely open |
| L0 entity | triggers | `0x10217000` | CTriggerInventory::vfunc5 | 87 | la_ventruetower_2, sm_hub_1, sp_tutorial_1 | likely open |
| L0 entity | triggers | `0x102110c0` | CTriggerBombSite::Spawn | 16 | la_ventruetower_2, sm_warehouse_1 | open |
| L0 entity | triggers | `0x102110e0` | CTriggerBombSite::StartTouch | 82 | la_ventruetower_2, sm_warehouse_1 | open |
| L0 entity | triggers | `0x10211160` | CTriggerBombSite::EndTouch | 108 | la_ventruetower_2, sm_warehouse_1 | open |
| L0 entity | triggers | `0x10210f80` | CTriggerBombSite::vfunc82 | 6 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x102111f0` | CTriggerBombSite::vfunc33 | 6 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10211210` | CTriggerBombSite::vfunc35 | 80 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10211280` | CTriggerBombSite::vfunc32 | 209 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x102113a0` | CTriggerBombSite::vfunc36 | 3 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x102113c0` | CTriggerBombSite::vfunc39 | 49 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10211410` | CTriggerBombSite::vfunc42 | 70 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10211480` | CTriggerBombSite::vfunc41 | 149 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10216f70` | CTriggerBombSite::vfunc5 | 98 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10211550` | Global::FUN_10211550 | 277 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x102116c0` | Global::FUN_102116c0 | 325 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x103f6800` | Global::FUN_103f6800 | 427 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x103f6ab0` | Global::FUN_103f6ab0 | 42 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x101cb210` | CTriggerImpact::Spawn | 12 | la_malkavian_4 | open |
| L0 entity | triggers | `0x101cb250` | CTriggerImpact::InputImpact | 178 | la_malkavian_4 | open |
| L0 entity | triggers | `0x101cb340` | CTriggerImpact::StartTouch | 406 | la_malkavian_4 | open |
| L0 entity | triggers | `0x101cb550` | CTriggerImpact::InputSetMagnitude | 35 | la_malkavian_4 | open |
| L0 entity | triggers | `0x101cb0d0` | CTriggerImpact::vfunc82 | 6 | la_malkavian_4 | likely open |
| L0 entity | triggers | `0x101cc2c0` | CTriggerImpact::vfunc5 | 98 | la_malkavian_4 | likely open |
| L1 animation | animation | `0x1008d0f0` | CBaseAnimating::SetGroundSpeedScalar | 126 | ch_temple_4, hw_warrens_4, la_bradbury_1, la_bradbury_2, la_crackhouse_1, sm_hub_2, sp_giovanni_2a, sp_giovanni_2b | open |
| L2 character | combat | `0x10338b00` | CBaseCombatCharacter::FillBloodPool | 321 | la_malkavian_3 | open |
| L2 character | inventory_ui_items | `0x103f5bf0` | CItemMMoneyClip::vfunc80 | 6 | ch_fishmarket_1, ch_shrekhub, hw_redspot_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103f5c10` | CItemMMoneyClip::vfunc81 | 3 | ch_fishmarket_1, ch_shrekhub, hw_redspot_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x10409b50` | CItemMMoneyClip::vfunc5 | 30 | ch_fishmarket_1, ch_shrekhub, hw_redspot_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103fcde0` | CItemGCarStereo::vfunc80 | 6 | ch_zhaos_1, la_abandoned_building_1, la_crackhouse_1, la_parkinggarage_1, la_skyline_1, sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103fce00` | CItemGCarStereo::vfunc81 | 3 | ch_zhaos_1, la_abandoned_building_1, la_crackhouse_1, la_parkinggarage_1, la_skyline_1, sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd5f0` | CItemGDrugsMorphineBottle::vfunc80 | 6 | ch_zhaos_1, hw_netcafe_1, la_hospital_1, la_malkavian_3, la_malkavian_3b, sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd610` | CItemGDrugsMorphineBottle::vfunc81 | 3 | ch_zhaos_1, hw_netcafe_1, la_hospital_1, la_malkavian_3, la_malkavian_3b, sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a180` | CItemGCarStereo::vfunc5 | 30 | ch_zhaos_1, la_abandoned_building_1, la_crackhouse_1, la_parkinggarage_1, la_skyline_1, sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a210` | CItemGDrugsMorphineBottle::vfunc5 | 30 | ch_zhaos_1, hw_netcafe_1, la_hospital_1, la_malkavian_3, la_malkavian_3b, sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe610` | CItemGRingGold::vfunc80 | 6 | ch_lotus_1, hw_luckystar_1, la_empire_1, la_malkavian_3, sm_asylum_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe630` | CItemGRingGold::vfunc81 | 3 | ch_lotus_1, hw_luckystar_1, la_empire_1, la_malkavian_3, sm_asylum_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a330` | CItemGRingGold::vfunc5 | 30 | ch_lotus_1, hw_luckystar_1, la_empire_1, la_malkavian_3, sm_asylum_1 | likely open |
| L2 character | inventory_ui_items | `0x103f7530` | CResearchBook_MG_FIN::vfunc80 | 6 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x103f7550` | CResearchBook_MG_FIN::vfunc81 | 3 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x103f7b30` | CResearchBook_MG_BRAWL::vfunc80 | 6 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x103f7b50` | CResearchBook_MG_BRAWL::vfunc81 | 3 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x10402130` | CItemGBrotherhoodFlyer::vfunc80 | 6 | la_empire_3, la_plaguebearer_sewer_1 | likely open |
| L2 character | inventory_ui_items | `0x10402150` | CItemGBrotherhoodFlyer::vfunc81 | 3 | la_empire_3, la_plaguebearer_sewer_1 | likely open |
| L2 character | inventory_ui_items | `0x10409ca0` | CResearchBook_MG_FIN::vfunc5 | 30 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x10409d00` | CResearchBook_MG_BRAWL::vfunc5 | 30 | la_empire_2, la_empire_3 | likely open |
| L2 character | inventory_ui_items | `0x1040a750` | CItemGBrotherhoodFlyer::vfunc5 | 30 | la_empire_3, la_plaguebearer_sewer_1 | likely open |
| L2 character | inventory_ui_items | `0x103fc060` | CBloodPack::Deploy | 236 | la_malkavian_3 | open |
| L2 character | inventory_ui_items | `0x103fc940` | CElderVitaePack::Deploy | 161 | la_malkavian_3 | open |
| L2 character | inventory_ui_items | `0x103f7230` | CResearchBook_MG_SEC::vfunc80 | 6 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x103f7250` | CResearchBook_MG_SEC::vfunc81 | 3 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x103f7e30` | CResearchBook_LG_DODGE::vfunc80 | 6 | la_malkavian_2 | likely open |
| L2 character | inventory_ui_items | `0x103f7e50` | CResearchBook_LG_DODGE::vfunc81 | 3 | la_malkavian_2 | likely open |
| L2 character | inventory_ui_items | `0x103f8430` | CResearchBook_MG_MELEE::vfunc80 | 6 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8450` | CResearchBook_MG_MELEE::vfunc81 | 3 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x103fc250` | CBloodPack::vfunc80 | 6 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x103fc270` | CBloodPack::vfunc81 | 3 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x103fcae0` | CElderVitaePack::vfunc80 | 6 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x103fcb00` | CElderVitaePack::vfunc81 | 3 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x104000f0` | CItemGLarryBriefcase::vfunc80 | 6 | la_parkinggarage_1 | likely open |
| L2 character | inventory_ui_items | `0x10400110` | CItemGLarryBriefcase::vfunc81 | 3 | la_parkinggarage_1 | likely open |
| L2 character | inventory_ui_items | `0x10401110` | CItemGPishaFetish::vfunc80 | 6 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x10401130` | CItemGPishaFetish::vfunc81 | 3 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x104013c0` | CItemGEDanePrintReport::vfunc80 | 6 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x104013e0` | CItemGEDanePrintReport::vfunc81 | 3 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x10401670` | CItemGEDaneReport::vfunc80 | 6 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x10401690` | CItemGEDaneReport::vfunc81 | 3 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x104023e0` | CItemGMilligansBuisnesscard::vfunc80 | 6 | la_hospital_1 | likely open |
| L2 character | inventory_ui_items | `0x10402400` | CItemGMilligansBuisnesscard::vfunc81 | 3 | la_hospital_1 | likely open |
| L2 character | inventory_ui_items | `0x10404c30` | CItemGHannahsApptBook::vfunc80 | 6 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x10404c50` | CItemGHannahsApptBook::vfunc81 | 3 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x104061b0` | CItemGGarysPhoto::vfunc80 | 6 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x104061d0` | CItemGGarysPhoto::vfunc81 | 3 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x10406c70` | CItemGGargoyleBook::vfunc80 | 6 | la_chantry_1 | likely open |
| L2 character | inventory_ui_items | `0x10406c90` | CItemGGargoyleBook::vfunc81 | 3 | la_chantry_1 | likely open |
| L2 character | inventory_ui_items | `0x10409c70` | CResearchBook_MG_SEC::vfunc5 | 30 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x10409d30` | CResearchBook_LG_DODGE::vfunc5 | 30 | la_malkavian_2 | likely open |
| L2 character | inventory_ui_items | `0x10409d90` | CResearchBook_MG_MELEE::vfunc5 | 30 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a0f0` | CBloodPack::vfunc5 | 30 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x1040a150` | CElderVitaePack::vfunc5 | 30 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x1040a510` | CItemGLarryBriefcase::vfunc5 | 30 | la_parkinggarage_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a630` | CItemGPishaFetish::vfunc5 | 30 | la_museum_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a660` | CItemGEDanePrintReport::vfunc5 | 30 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a690` | CItemGEDaneReport::vfunc5 | 30 | la_dane_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a780` | CItemGMilligansBuisnesscard::vfunc5 | 30 | la_hospital_1 | likely open |
| L2 character | inventory_ui_items | `0x1040aa50` | CItemGHannahsApptBook::vfunc5 | 30 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x1040abd0` | CItemGGarysPhoto::vfunc5 | 30 | la_skyline_1 | likely open |
| L2 character | inventory_ui_items | `0x1040ac90` | CItemGGargoyleBook::vfunc5 | 30 | la_chantry_1 | likely open |
| L2 character | inventory_ui_items | `0x103fc190` | Global::FUN_103fc190 | 57 | la_malkavian_3 | likely open |
| L2 character | inventory_ui_items | `0x103fca20` | Global::FUN_103fca20 | 57 | la_malkavian_3 | likely open |
| L2 character | rpg | `0x1022dc30` | Global::FUN_1022dc30 | 493 | ch_glaze_1, hw_asphole_1, la_confession_1, la_empire_1, sm_asylum_1, sp_endsequences_a | likely open |
| L2 character | rpg | `0x100bbef0` | Global::FUN_100bbef0 | 317 | ch_fulab_1, la_malkavian_2, la_museum_1, sp_soc_1, sp_soc_2 | likely open |
| L2 character | rpg | `0x103f99d0` | COccultFrenzy::vfunc80 | 6 | la_malkavian_2 | likely open |
| L2 character | rpg | `0x103f99f0` | COccultFrenzy::vfunc81 | 3 | la_malkavian_2 | likely open |
| L2 character | rpg | `0x103fb4d0` | COccultThaumDamage::vfunc80 | 6 | la_chantry_1 | likely open |
| L2 character | rpg | `0x103fb4f0` | COccultThaumDamage::vfunc81 | 3 | la_chantry_1 | likely open |
| L2 character | rpg | `0x10409e80` | COccultFrenzy::vfunc5 | 30 | la_malkavian_2 | likely open |
| L2 character | rpg | `0x1040a030` | COccultThaumDamage::vfunc5 | 30 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103e4430` | CWeaponMelee_Torch::FUN_103e4430 | 6 | ch_fulab_1, hw_hub_1, la_bradbury_2, la_bradbury_3, la_ventruetower_3, sm_basement_1, sm_hub_1, sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x102395c0` | CWeaponRanged::Deploy | 134 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | open |
| L2 character | weapons | `0x102397b0` | CWeaponRanged::Kick | 321 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | open |
| L2 character | weapons | `0x102347c0` | CWeaponRanged::FUN_102347c0 | 6 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | open |
| L2 character | weapons | `0x102343a0` | CWeaponRanged::FUN_102343a0 | 6 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x102347e0` | CWeaponRanged::FUN_102347e0 | 11 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10234800` | CWeaponRanged::FUN_10234800 | 3 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10239250` | CWeaponRanged::FUN_10239250 | 8 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10239350` | CWeaponRanged::FUN_10239350 | 429 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x1023a210` | CWeaponRanged::FUN_1023a210 | 44 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x102391c0` | Global::FUN_102391c0 | 61 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10239210` | CWeaponRanged::FUN_10239210 | 44 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10239f70` | CWeaponRanged::FUN_10239f70 | 519 | hw_warrens_1, la_bradbury_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10239710` | CWeaponRanged::ItemBusyFrame | 115 | hw_warrens_1, la_bradbury_2, la_library_1, sm_warehouse_1, sp_soc_2 | open |
| L2 character | weapons | `0x102355e0` | CWeaponRanged_Rifle_M37::vfunc80 | 6 | la_bradbury_2, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x10235600` | CWeaponRanged_Rifle_M37::vfunc81 | 3 | la_bradbury_2, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x102357b0` | CWeaponRanged_Rifle_M37::vfunc362 | 6 | la_bradbury_2, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x102357d0` | CWeaponRanged_Rifle_M37::vfunc363 | 6 | la_bradbury_2, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x1023a410` | CWeaponRanged_Rifle_M37::vfunc5 | 30 | la_bradbury_2, sm_warehouse_1, sp_soc_2 | likely open |
| L2 character | weapons | `0x103e3200` | CWeaponRanged_FlameThrower::ItemBusyFrame | 36 | la_bradbury_1, la_bradbury_2 | open |
| L2 character | weapons | `0x10235920` | CWeaponRanged_Smg_Mac10::vfunc80 | 6 | la_bradbury_2, sp_soc_2 | likely open |
| L2 character | weapons | `0x10235940` | CWeaponRanged_Smg_Mac10::vfunc81 | 3 | la_bradbury_2, sp_soc_2 | likely open |
| L2 character | weapons | `0x10235af0` | CWeaponRanged_Smg_Mac10::vfunc362 | 6 | la_bradbury_2, sp_soc_2 | likely open |
| L2 character | weapons | `0x10235b10` | CWeaponRanged_Smg_Mac10::vfunc363 | 6 | la_bradbury_2, sp_soc_2 | likely open |
| L2 character | weapons | `0x10236fb0` | CWeaponRanged_Pistol_Thirtyeight::vfunc80 | 6 | la_library_1, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x10236fd0` | CWeaponRanged_Pistol_Thirtyeight::vfunc81 | 3 | la_library_1, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x10237180` | CWeaponRanged_Pistol_Thirtyeight::vfunc362 | 6 | la_library_1, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x102371a0` | CWeaponRanged_Pistol_Thirtyeight::vfunc363 | 6 | la_library_1, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x1023a440` | CWeaponRanged_Smg_Mac10::vfunc5 | 30 | la_bradbury_2, sp_soc_2 | likely open |
| L2 character | weapons | `0x1023a530` | CWeaponRanged_Pistol_Thirtyeight::vfunc5 | 30 | la_library_1, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x103e2740` | CWeaponRanged_FlameThrower::vfunc80 | 6 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e2760` | CWeaponRanged_FlameThrower::vfunc81 | 3 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e29d0` | CWeaponRanged_FlameThrower::vfunc362 | 6 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e29f0` | CWeaponRanged_FlameThrower::vfunc363 | 6 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e2b90` | CWeaponRanged_FlameThrower::vfunc5 | 134 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e2e70` | CWeaponRanged_FlameThrower::vfunc265 | 51 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e6760` | CWeaponMelee_OccultBlade::vfunc80 | 6 | la_bradbury_2, la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e6780` | CWeaponMelee_OccultBlade::vfunc81 | 3 | la_bradbury_2, la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e6930` | CWeaponMelee_OccultBlade::vfunc362 | 6 | la_bradbury_2, la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e6950` | CWeaponMelee_OccultBlade::vfunc363 | 6 | la_bradbury_2, la_bradbury_3 | likely open |
| L2 character | weapons | `0x103ecd50` | CWeaponMelee_OccultBlade::vfunc5 | 30 | la_bradbury_2, la_bradbury_3 | likely open |
| L2 character | weapons | `0x10252470` | Global::FUN_10252470 | 42 | la_ventruetower_2, sm_warehouse_1 | likely open |
| L2 character | weapons | `0x103e1710` | Global::FUN_103e1710 | 32 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e1c00` | Global::FUN_103e1c00 | 89 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e2470` | Global::FUN_103e2470 | 225 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e2d10` | Global::FUN_103e2d10 | 263 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3130` | Global::FUN_103e3130 | 97 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e32b0` | Global::FUN_103e32b0 | 378 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3490` | Global::FUN_103e3490 | 166 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3570` | Global::FUN_103e3570 | 604 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3900` | Global::FUN_103e3900 | 32 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3930` | Global::FUN_103e3930 | 612 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3c40` | Global::FUN_103e3c40 | 241 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3ec0` | Global::FUN_103e3ec0 | 118 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e3f60` | Global::FUN_103e3f60 | 103 | la_bradbury_1, la_bradbury_2 | likely open |
| L2 character | weapons | `0x103e5bf0` | CWeaponMelee_ZombieFists::Spawn | 17 | la_skyline_1 | open |
| L2 character | weapons | `0x103e74a0` | CWeaponMelee_Torch::Spawn | 17 | la_bradbury_3 | open |
| L2 character | weapons | `0x103e7510` | CWeaponMelee_Torch::UpdateOnRemove | 16 | la_bradbury_3 | open |
| L2 character | weapons | `0x103e77c0` | CWeaponMelee_Torch::Deploy | 27 | la_bradbury_3 | open |
| L2 character | weapons | `0x103e7960` | CWeaponMelee_Torch::Hide | 16 | la_bradbury_3 | open |
| L2 character | weapons | `0x103e86f0` | CWeaponMelee_WerewolfAttacks::Spawn | 17 | la_hub_1 | open |
| L2 character | weapons | `0x103eb880` | CWeaponMelee_Tzimisce3Claw::Spawn | 17 | la_malkavian_2 | open |
| L2 character | weapons | `0x103e5280` | CWeaponMelee_Claws_Protean5::vfunc80 | 6 | la_library_1 | likely open |
| L2 character | weapons | `0x103e52a0` | CWeaponMelee_Claws_Protean5::vfunc81 | 3 | la_library_1 | likely open |
| L2 character | weapons | `0x103e5450` | CWeaponMelee_Claws_Protean5::vfunc362 | 6 | la_library_1 | likely open |
| L2 character | weapons | `0x103e5470` | CWeaponMelee_Claws_Protean5::vfunc363 | 6 | la_library_1 | likely open |
| L2 character | weapons | `0x103e5500` | CWeaponMelee_Claws_Protean5::vfunc373 | 6 | la_library_1 | likely open |
| L2 character | weapons | `0x103e5970` | CWeaponMelee_ZombieFists::vfunc80 | 6 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103e5990` | CWeaponMelee_ZombieFists::vfunc81 | 3 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103e5b40` | CWeaponMelee_ZombieFists::vfunc362 | 6 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103e5b60` | CWeaponMelee_ZombieFists::vfunc363 | 6 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103e5c20` | CWeaponMelee_ZombieFists::vfunc373 | 6 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103e6aa0` | CWeaponMelee_Sheriff_Sword::vfunc80 | 6 | la_ventruetower_3 | likely open |
| L2 character | weapons | `0x103e6ac0` | CWeaponMelee_Sheriff_Sword::vfunc81 | 3 | la_ventruetower_3 | likely open |
| L2 character | weapons | `0x103e6c70` | CWeaponMelee_Sheriff_Sword::vfunc362 | 6 | la_ventruetower_3 | likely open |
| L2 character | weapons | `0x103e6c90` | CWeaponMelee_Sheriff_Sword::vfunc363 | 6 | la_ventruetower_3 | likely open |
| L2 character | weapons | `0x103e7100` | CWeaponMelee_Torch::vfunc80 | 6 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7120` | CWeaponMelee_Torch::vfunc81 | 3 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7300` | CWeaponMelee_Torch::vfunc362 | 6 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7320` | CWeaponMelee_Torch::vfunc363 | 6 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7410` | CWeaponMelee_Torch::vfunc82 | 6 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e75d0` | CWeaponMelee_Torch::vfunc130 | 70 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7640` | CWeaponMelee_Torch::vfunc316 | 36 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7920` | CWeaponMelee_Torch::vfunc299 | 34 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e8470` | CWeaponMelee_WerewolfAttacks::vfunc80 | 6 | la_hub_1 | likely open |
| L2 character | weapons | `0x103e8490` | CWeaponMelee_WerewolfAttacks::vfunc81 | 3 | la_hub_1 | likely open |
| L2 character | weapons | `0x103e8640` | CWeaponMelee_WerewolfAttacks::vfunc362 | 6 | la_hub_1 | likely open |
| L2 character | weapons | `0x103e8660` | CWeaponMelee_WerewolfAttacks::vfunc363 | 6 | la_hub_1 | likely open |
| L2 character | weapons | `0x103e8720` | CWeaponMelee_WerewolfAttacks::vfunc373 | 6 | la_hub_1 | likely open |
| L2 character | weapons | `0x103eafb0` | CWeaponMelee_GargoyleFist::vfunc80 | 6 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103eafd0` | CWeaponMelee_GargoyleFist::vfunc81 | 3 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103eb180` | CWeaponMelee_GargoyleFist::vfunc362 | 6 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103eb1a0` | CWeaponMelee_GargoyleFist::vfunc363 | 6 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103eb230` | CWeaponMelee_GargoyleFist::vfunc373 | 4 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103eb600` | CWeaponMelee_Tzimisce3Claw::vfunc80 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103eb620` | CWeaponMelee_Tzimisce3Claw::vfunc81 | 3 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103eb7d0` | CWeaponMelee_Tzimisce3Claw::vfunc362 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103eb7f0` | CWeaponMelee_Tzimisce3Claw::vfunc363 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103eb930` | CWeaponMelee_Tzimisce3Claw::vfunc373 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ebd20` | CWeaponMelee_ManBatClaw::vfunc80 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ebd40` | CWeaponMelee_ManBatClaw::vfunc81 | 3 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ebef0` | CWeaponMelee_ManBatClaw::vfunc362 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ebf10` | CWeaponMelee_ManBatClaw::vfunc363 | 6 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ebfa0` | CWeaponMelee_ManBatClaw::vfunc373 | 4 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ecc30` | CWeaponMelee_Claws_Protean5::vfunc5 | 30 | la_library_1 | likely open |
| L2 character | weapons | `0x103ecc90` | CWeaponMelee_ZombieFists::vfunc5 | 30 | la_skyline_1 | likely open |
| L2 character | weapons | `0x103ecd80` | CWeaponMelee_Sheriff_Sword::vfunc5 | 30 | la_ventruetower_3 | likely open |
| L2 character | weapons | `0x103ecde0` | CWeaponMelee_Torch::vfunc5 | 30 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103ecea0` | CWeaponMelee_WerewolfAttacks::vfunc5 | 30 | la_hub_1 | likely open |
| L2 character | weapons | `0x103ecf90` | CWeaponMelee_GargoyleFist::vfunc5 | 30 | la_chantry_1 | likely open |
| L2 character | weapons | `0x103ecff0` | CWeaponMelee_Tzimisce3Claw::vfunc5 | 30 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103ed050` | CWeaponMelee_ManBatClaw::vfunc5 | 30 | la_malkavian_2 | likely open |
| L2 character | weapons | `0x103e7530` | Global::FUN_103e7530 | 56 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7580` | Global::FUN_103e7580 | 60 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e7680` | Global::FUN_103e7680 | 255 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e77f0` | Global::FUN_103e77f0 | 131 | la_bradbury_3 | likely open |
| L2 character | weapons | `0x103e78b0` | Global::FUN_103e78b0 | 70 | la_bradbury_3 | likely open |
| L4 NPC | npc_species | `0x1035c190` | CNPC_VAndreiBlood::FUN_1035c190 | 5 | ch_temple_2, hw_609_1, la_bradbury_1, la_bradbury_3, la_hub_1, la_ventruetower_3, sm_vamparena, sm_warehouse_1, sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x103a0500` | CNPC_VNewscaster::Classify | 6 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | open |
| L4 NPC | npc_species | `0x1039fff0` | CNPC_VNewscaster::vfunc488 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0010` | CNPC_VNewscaster::vfunc489 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0030` | CNPC_VNewscaster::vfunc490 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0050` | CNPC_VNewscaster::vfunc491 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0070` | CNPC_VNewscaster::vfunc492 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0090` | CNPC_VNewscaster::vfunc493 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a00b0` | CNPC_VNewscaster::vfunc494 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a00d0` | CNPC_VNewscaster::vfunc495 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a00f0` | CNPC_VNewscaster::vfunc496 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0110` | CNPC_VNewscaster::vfunc497 | 1 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0150` | CNPC_VNewscaster::vfunc362 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0170` | CNPC_VNewscaster::vfunc365 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0190` | CNPC_VNewscaster::vfunc364 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a01d0` | CNPC_VNewscaster::vfunc587 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a01f0` | CNPC_VNewscaster::vfunc473 | 3 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0210` | CNPC_VNewscaster::vfunc377 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0230` | CNPC_VNewscaster::vfunc378 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a0250` | CNPC_VNewscaster::vfunc72 | 5 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x103a4f60` | CNPC_VSabbatGunman::vfunc82 | 6 | hw_warrens_4, la_bradbury_1, la_bradbury_2, la_crackhouse_1, sm_hub_2, sp_giovanni_2a, sp_giovanni_2b | likely open |
| L4 NPC | npc_species | `0x103a50a0` | CNPC_VSabbatGunman::vfunc580 | 6 | hw_warrens_4, la_bradbury_1, la_bradbury_2, la_crackhouse_1, sm_hub_2, sp_giovanni_2a, sp_giovanni_2b | likely open |
| L4 NPC | npc_species | `0x103a50e0` | CNPC_VSabbatGunman::vfunc5 | 30 | hw_warrens_4, la_bradbury_1, la_bradbury_2, la_crackhouse_1, sm_hub_2, sp_giovanni_2a, sp_giovanni_2b | likely open |
| L4 NPC | npc_species | `0x103a5240` | CNPC_VSabbatGunman::vfunc546 | 29 | hw_warrens_4, la_bradbury_1, la_bradbury_2, la_crackhouse_1, sm_hub_2, sp_giovanni_2a, sp_giovanni_2b | likely open |
| L4 NPC | npc_species | `0x103a0eb0` | Global::FUN_103a0eb0 | 252 | hw_tawni_1, la_chantry_1, la_empire_2, la_hub_1, la_skyline_1, sm_apartment_1, sm_pawnshop_1 | likely open |
| L4 NPC | npc_species | `0x1037a6e0` | CNPC_VGhoulCroucher::vfunc82 | 6 | la_malkavian_2, la_malkavian_3, la_malkavian_3b, la_malkavian_4 | likely open |
| L4 NPC | npc_species | `0x1037a950` | CNPC_VGhoulCroucher::vfunc546 | 29 | la_malkavian_2, la_malkavian_3, la_malkavian_3b, la_malkavian_4 | likely open |
| L4 NPC | npc_species | `0x1037afb0` | CNPC_VGhoulCroucher::vfunc580 | 6 | la_malkavian_2, la_malkavian_3, la_malkavian_3b, la_malkavian_4 | likely open |
| L4 NPC | npc_species | `0x1037aff0` | CNPC_VGhoulCroucher::vfunc5 | 54 | la_malkavian_2, la_malkavian_3, la_malkavian_3b, la_malkavian_4 | likely open |
| L4 NPC | npc_species | `0x10388ca0` | CNPC_VLasombra::vfunc82 | 6 | la_bradbury_2, la_library_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x10388de0` | CNPC_VLasombra::vfunc580 | 6 | la_bradbury_2, la_library_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x10388e20` | CNPC_VLasombra::vfunc5 | 30 | la_bradbury_2, la_library_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x10388f80` | CNPC_VLasombra::vfunc546 | 29 | la_bradbury_2, la_library_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x10384700` | CNPC_VHuman::Classify | 6 | la_confession_1, sm_asylum_1 | open |
| L4 NPC | npc_species | `0x1035c1b0` | CNPC_VVampireBoss::vfunc580 | 6 | la_hub_1, sm_warehouse_1 | likely open |
| L4 NPC | npc_species | `0x1035c1f0` | CNPC_VVampireBoss::vfunc5 | 43 | la_hub_1, sm_warehouse_1 | likely open |
| L4 NPC | npc_species | `0x10383f10` | CNPC_VHuman::FUN_10383f10 | 6 | la_confession_1, sm_asylum_1 | likely open |
| L4 NPC | npc_species | `0x10384060` | CNPC_VHuman::vfunc580 | 6 | la_confession_1, sm_asylum_1 | likely open |
| L4 NPC | npc_species | `0x103840a0` | CNPC_VHuman::vfunc5 | 30 | la_confession_1, sm_asylum_1 | likely open |
| L4 NPC | npc_species | `0x10384200` | CNPC_VHuman::vfunc546 | 29 | la_confession_1, sm_asylum_1 | likely open |
| L4 NPC | npc_species | `0x103c5060` | CNPC_VVampireBoss::vfunc82 | 6 | la_hub_1, sm_warehouse_1 | likely open |
| L4 NPC | npc_species | `0x103c5270` | CNPC_VVampireBoss::vfunc546 | 29 | la_hub_1, sm_warehouse_1 | likely open |
| L4 NPC | npc_species | `0x1038b0e0` | CNPC_VManBat::Classify | 6 | la_ventruetower_3 | open |
| L4 NPC | npc_species | `0x103ac470` | CNPC_VScurrying::Classify | 6 | la_hub_1 | open |
| L4 NPC | npc_species | `0x101578b0` | Global::FUN_101578b0 | 12 | la_ventruetower_3 | open |
| L4 NPC | npc_species | `0x101578d0` | Global::FUN_101578d0 | 36 | la_ventruetower_3 | open |
| L4 NPC | npc_species | `0x103ae7f0` | CNPC_VSheriffMan::vfunc127 | 59 | la_ventruetower_3 | open |
| L4 NPC | npc_species | `0x10369cf0` | CNPC_VCameraSecurity::vfunc82 | 6 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x10369e40` | CNPC_VCameraSecurity::vfunc5 | 30 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x103875d0` | CNPC_VHumanCombatPatrol::vfunc82 | 6 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x10387710` | CNPC_VHumanCombatPatrol::vfunc580 | 6 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x10387750` | CNPC_VHumanCombatPatrol::vfunc5 | 30 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x103878b0` | CNPC_VHumanCombatPatrol::vfunc546 | 29 | la_museum_1 | likely open |
| L4 NPC | npc_species | `0x10389db0` | CNPC_VManBat::vfunc580 | 6 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x10389df0` | CNPC_VManBat::vfunc5 | 30 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x10389f50` | CNPC_VManBat::vfunc546 | 29 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x1038ac80` | CNPC_VManBat::vfunc82 | 6 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x1038fb20` | CNPC_VManBat::vfunc102 | 104 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x103abbe0` | CNPC_VScurrying::vfunc5 | 30 | la_hub_1 | likely open |
| L4 NPC | npc_species | `0x103adaa0` | CNPC_VSheriffMan::vfunc82 | 6 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x103adcb0` | CNPC_VSheriffMan::vfunc546 | 29 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x103ae4b0` | CNPC_VSheriffMan::vfunc580 | 6 | la_ventruetower_3 | likely open |
| L4 NPC | npc_species | `0x103ae4f0` | CNPC_VSheriffMan::vfunc5 | 54 | la_ventruetower_3 | likely open |
| L4 NPC | social | `0x10135d00` | CLogicSquadCondition::InputTest | 59 | la_ventruetower_1b | open |
| L4 NPC | social | `0x101359c0` | CLogicSquadCondition::vfunc82 | 6 | la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10135b00` | CLogicSquadCondition::vfunc113 | 152 | la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10136210` | CLogicSquadCondition::vfunc5 | 54 | la_ventruetower_1b | likely open |
| L4 NPC | social | `0x101c9910` | CAI_ChangeTarget::vfunc117 | 8 | la_bradbury_1 | likely open |
| L4 NPC | social | `0x101c9930` | CAI_ChangeTarget::vfunc82 | 6 | la_bradbury_1 | likely open |
| L4 NPC | social | `0x101cc0e0` | CAI_ChangeTarget::vfunc5 | 30 | la_bradbury_1 | likely open |
| L4 NPC | social | `0x10135bd0` | Global::FUN_10135bd0 | 42 | la_ventruetower_1b | likely open |
| L4 NPC | social | `0x10135c10` | Global::FUN_10135c10 | 183 | la_ventruetower_1b | likely open |
| L5 script | camera_ui | `0x1020c0d0` | CSecCamera::Spawn | 293 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | open |
| L5 script | camera_ui | `0x1020b3e0` | CSecCamera::vfunc82 | 6 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020bf90` | CSecCamera::vfunc5 | 87 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c250` | CSecCamera::vfunc113 | 56 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c7f0` | CSecCamera::vfunc266 | 24 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c850` | CSecCamera::vfunc133 | 37 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c3d0` | Global::FUN_1020c3d0 | 187 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c4f0` | Global::FUN_1020c4f0 | 565 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c820` | Global::FUN_1020c820 | 25 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c890` | Global::FUN_1020c890 | 195 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020c9a0` | Global::FUN_1020c9a0 | 455 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x1020cbf0` | Global::FUN_1020cbf0 | 74 | la_museum_1, la_ventruetower_1, la_ventruetower_1b, sm_medical_1 | likely open |
| L5 script | camera_ui | `0x102124a0` | CGameSign::vfunc82 | 6 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x102125c0` | CGameSign::vfunc32 | 5 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x102125e0` | CGameSign::vfunc37 | 7 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212620` | CGameSign::vfunc5 | 30 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212c00` | CGameSign::vfunc41 | 116 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212ca0` | CGameSign::vfunc39 | 149 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212d70` | CGameSign::vfunc42 | 27 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212650` | Global::FUN_10212650 | 32 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10212a30` | Global::FUN_10212a30 | 361 | la_hub_1, sm_pawnshop_1, sp_tutorial_1 | likely open |
| L5 script | camera_ui | `0x10103800` | CEnvFloatingCamera::InputStartWaves | 27 | la_dane_1 | open |
| L5 script | camera_ui | `0x10103830` | CEnvFloatingCamera::InputStopWaves | 27 | la_dane_1 | open |
| L5 script | camera_ui | `0x10118db0` | CGameUI::Remove | 851 | la_hub_1 | open |
| L5 script | camera_ui | `0x101036e0` | CEnvFloatingCamera::vfunc82 | 6 | la_dane_1 | likely open |
| L5 script | camera_ui | `0x10103900` | CEnvFloatingCamera::vfunc5 | 30 | la_dane_1 | likely open |
| L5 script | camera_ui | `0x10118390` | CGameUI::vfunc82 | 6 | la_hub_1 | likely open |
| L5 script | camera_ui | `0x101191f0` | CGameUI::vfunc5 | 186 | la_hub_1 | likely open |
| L5 script | camera_ui | `0x10103790` | Global::FUN_10103790 | 66 | la_dane_1 | likely open |
| L5 script | camera_ui | `0x10103860` | Global::FUN_10103860 | 58 | la_dane_1 | likely open |
| L5 script | camera_ui | `0x101038b0` | Global::FUN_101038b0 | 58 | la_dane_1 | likely open |
| L5 script | camera_ui | `0x10118b40` | Global::FUN_10118b40 | 302 | la_hub_1 | likely open |
| L5 script | camera_ui | `0x101cde40` | Global::FUN_101cde40 | 257 | la_dane_1 | likely open |
