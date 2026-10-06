# Tail — story and special maps (`sp_*`)

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

127 open, partial or likely-open functions that only these maps reach (11 KB).
Their witnesses are records on their own maps (`stage: map:<map>`); a map not baked yet is baked first.

## The maps

| map | baked | classes only this group spawns |
|---|---|---|
| `sp_endsequences_a` | yes | — |
| `sp_endsequences_b` | yes | — |
| `sp_epilogue` | yes | — |
| `sp_genesisdevice_1` | yes | — |
| `sp_giovanni_1` | yes | — |
| `sp_giovanni_2a` | yes | CResearchBook_HG_DODGE |
| `sp_giovanni_2b` | yes | CResearchBook_HG_DODGE |
| `sp_giovanni_3` | yes | CItemGPishaBook, COccultExperience, CWeaponMelee_TzimisceMelee |
| `sp_giovanni_4` | yes | CAI_DynamicLink |
| `sp_giovanni_5` | yes | npc VChangBrosBlade |
| `sp_masquerade_1` | yes | — |
| `sp_ninesintro` | yes | — |
| `sp_observatory_1` | yes | — |
| `sp_observatory_2` | **no** | CPropClockHand, CPropLargeHullIgnore, CTriggerWWZone, npc VWerewolf |
| `sp_soc_1` | yes | — |
| `sp_soc_2` | yes | CItemGBachJournal, CItemGVampyrApocrypha, CResearchBook_HG_FIRE, CWeaponRanged_Rifle_SteyrAug |
| `sp_soc_3` | yes | — |
| `sp_soc_4` | yes | npc VBach |
| `sp_taxiride` | yes | — |
| `sp_theatre` | yes | — |
| `sp_tutorial_1` | yes | CItemContainer |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | entity_io | `0x10107e10` | CFilterClass::vfunc241 | 61 | ch_fulab_1, hw_cemetery_1, la_ventruetower_1, sp_endsequences_a, sp_endsequences_b, sp_observatory_2 | likely open |
| L0 entity | entity_io | `0x10107e60` | CFilterClass::vfunc82 | 6 | ch_fulab_1, hw_cemetery_1, la_ventruetower_1, sp_endsequences_a, sp_endsequences_b, sp_observatory_2 | likely open |
| L0 entity | entity_io | `0x10108620` | CFilterClass::vfunc5 | 54 | ch_fulab_1, hw_cemetery_1, la_ventruetower_1, sp_endsequences_a, sp_endsequences_b, sp_observatory_2 | likely open |
| L0 entity | movers_doors | `0x10223cc0` | CFuncKeyframedMover::Spawn | 312 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | open |
| L0 entity | movers_doors | `0x10222fd0` | CMoverKeyFrame::vfunc82 | 6 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x102230b0` | CMoverKeyFrame::vfunc113 | 81 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223160` | CBaseKeyframedMover::FUN_10223160 | 6 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223600` | CBaseKeyframedMover::FUN_10223600 | 109 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223a10` | CBaseKeyframedMover::FUN_10223a10 | 16 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223f90` | CMoverKeyFrame::vfunc5 | 43 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223fd0` | CFuncKeyframedMover::vfunc5 | 120 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10001ec4` | Global::FUN_10001ec4 | 5 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223470` | Global::FUN_10223470 | 307 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223690` | Global::FUN_10223690 | 70 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223700` | Global::FUN_10223700 | 70 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223a30` | Global::FUN_10223a30 | 57 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223a80` | Global::FUN_10223a80 | 208 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | movers_doors | `0x10223e50` | CFuncKeyframedMover::vfunc223 | 146 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | physics | `0x1002b9f0` | CPhysHinge::Spawn | 227 | sm_pawnshop_1, sm_warehouse_1, sp_soc_1, sp_soc_2, sp_tutorial_1 | open |
| L0 entity | physics | `0x1002b700` | CPhysHinge::vfunc82 | 6 | sm_pawnshop_1, sm_warehouse_1, sp_soc_1, sp_soc_2, sp_tutorial_1 | likely open |
| L0 entity | physics | `0x1002b800` | CPhysHinge::vfunc241 | 216 | sm_pawnshop_1, sm_warehouse_1, sp_soc_1, sp_soc_2, sp_tutorial_1 | likely open |
| L0 entity | physics | `0x1002cae0` | CPhysHinge::vfunc5 | 30 | sm_pawnshop_1, sm_warehouse_1, sp_soc_1, sp_soc_2, sp_tutorial_1 | likely open |
| L0 entity | physics | `0x101cf680` | Global::FUN_101cf680 | 116 | sm_pawnshop_1, sm_warehouse_1, sp_soc_1, sp_soc_2, sp_tutorial_1 | likely open |
| L0 entity | physics | `0x10223130` | Global::FUN_10223130 | 26 | hw_ash_sewer_1, sp_giovanni_1, sp_observatory_1, sp_observatory_2, sp_taxiride | likely open |
| L0 entity | physics | `0x1012d9f0` | CPropClockHand::Spawn | 201 | sp_observatory_2 | open |
| L0 entity | physics | `0x1012d850` | CPropClockHand::vfunc82 | 6 | sp_observatory_2 | likely open |
| L0 entity | physics | `0x1012d9c0` | CPropClockHand::vfunc5 | 30 | sp_observatory_2 | likely open |
| L0 entity | physics | `0x1012db00` | CPropClockHand::vfunc113 | 90 | sp_observatory_2 | likely open |
| L0 entity | physics | `0x1012db80` | CPropClockHand::vfunc86 | 56 | sp_observatory_2 | likely open |
| L0 entity | physics | `0x1012dbd0` | Global::FUN_1012dbd0 | 422 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103da540` | CTriggerWWZone::Activate | 126 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103da660` | CTriggerWWZone::Kill | 155 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103da730` | CTriggerWWZone::InitLocationMarkers | 593 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103daa30` | CTriggerWWZone::InitHintFlags | 671 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103dafd0` | CTriggerWWZone::Touch | 1127 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103db590` | CTriggerWWZone::EndTouch | 1035 | sp_observatory_2 | open |
| L0 entity | triggers | `0x103da3e0` | CTriggerWWZone::vfunc82 | 6 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103da5e0` | CTriggerWWZone::vfunc251 | 90 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103dbf50` | CTriggerWWZone::vfunc5 | 178 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103cecd0` | Global::FUN_103cecd0 | 39 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103dad80` | Global::FUN_103dad80 | 159 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103dae50` | Global::FUN_103dae50 | 141 | sp_observatory_2 | likely open |
| L0 entity | triggers | `0x103daf10` | Global::FUN_103daf10 | 141 | sp_observatory_2 | likely open |
| L2 character | inventory_ui_items | `0x103f8730` | CResearchBook_HG_DODGE::vfunc80 | 6 | sp_giovanni_2a, sp_giovanni_2b | likely open |
| L2 character | inventory_ui_items | `0x103f8750` | CResearchBook_HG_DODGE::vfunc81 | 3 | sp_giovanni_2a, sp_giovanni_2b | likely open |
| L2 character | inventory_ui_items | `0x10409dc0` | CResearchBook_HG_DODGE::vfunc5 | 30 | sp_giovanni_2a, sp_giovanni_2b | likely open |
| L2 character | inventory_ui_items | `0x10208c90` | CItemContainer::InputUse | 348 | sp_tutorial_1 | open |
| L2 character | inventory_ui_items | `0x10208ad0` | CItemContainer::vfunc5 | 30 | sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x103f7830` | CResearchBook_HG_FIRE::vfunc80 | 6 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x103f7850` | CResearchBook_HG_FIRE::vfunc81 | 3 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x10400bb0` | CItemGBachJournal::vfunc80 | 6 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x10400bd0` | CItemGBachJournal::vfunc81 | 3 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x10400e60` | CItemGVampyrApocrypha::vfunc80 | 6 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x10400e80` | CItemGVampyrApocrypha::vfunc81 | 3 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x10402690` | CItemGPishaBook::vfunc80 | 6 | sp_giovanni_3 | likely open |
| L2 character | inventory_ui_items | `0x104026b0` | CItemGPishaBook::vfunc81 | 3 | sp_giovanni_3 | likely open |
| L2 character | inventory_ui_items | `0x10409cd0` | CResearchBook_HG_FIRE::vfunc5 | 30 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a5d0` | CItemGBachJournal::vfunc5 | 30 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a600` | CItemGVampyrApocrypha::vfunc5 | 30 | sp_soc_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a7b0` | CItemGPishaBook::vfunc5 | 30 | sp_giovanni_3 | likely open |
| L2 character | inventory_ui_items | `0x1017c6a0` | Global::FUN_1017c6a0 | 26 | sp_tutorial_1 | likely open |
| L2 character | rpg | `0x103fabd0` | COccultExperience::vfunc80 | 6 | sp_giovanni_3 | likely open |
| L2 character | rpg | `0x103fabf0` | COccultExperience::vfunc81 | 3 | sp_giovanni_3 | likely open |
| L2 character | rpg | `0x10409fa0` | COccultExperience::vfunc5 | 30 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x102365f0` | CWeaponRanged_Rifle_SteyrAug::vfunc80 | 6 | sp_soc_2 | likely open |
| L2 character | weapons | `0x10236610` | CWeaponRanged_Rifle_SteyrAug::vfunc81 | 3 | sp_soc_2 | likely open |
| L2 character | weapons | `0x102367c0` | CWeaponRanged_Rifle_SteyrAug::vfunc362 | 6 | sp_soc_2 | likely open |
| L2 character | weapons | `0x102367e0` | CWeaponRanged_Rifle_SteyrAug::vfunc363 | 6 | sp_soc_2 | likely open |
| L2 character | weapons | `0x1023a4a0` | CWeaponRanged_Rifle_SteyrAug::vfunc5 | 30 | sp_soc_2 | likely open |
| L2 character | weapons | `0x103e87b0` | CWeaponMelee_TzimisceMelee::vfunc80 | 6 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e87d0` | CWeaponMelee_TzimisceMelee::vfunc81 | 3 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e8980` | CWeaponMelee_TzimisceMelee::vfunc362 | 6 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e89a0` | CWeaponMelee_TzimisceMelee::vfunc363 | 6 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e8a80` | CWeaponMelee_TzimisceMelee::vfunc373 | 4 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e8aa0` | CWeaponMelee_TzimisceMelee::vfunc5 | 30 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e8ad0` | CWeaponMelee_TzimisceMelee::vfunc367 | 206 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x103e8cd0` | CWeaponMelee_TzimisceMelee::vfunc269 | 191 | sp_giovanni_3 | likely open |
| L2 character | weapons | `0x10235bf0` | Global::FUN_10235bf0 | 248 | sp_soc_2 | likely open |
| L4 NPC | hints_places | `0x103c9fc0` | CHintData_WW::Init | 201 | sp_observatory_2 | open |
| L4 NPC | makers | `0x1034c9f0` | CNPCMaker_Zombie::vfunc82 | 6 | hw_cemetery_1, la_crackhouse_1, la_hospital_1, sp_giovanni_2a, sp_giovanni_2b, sp_giovanni_3 | likely open |
| L4 NPC | makers | `0x1034cbf0` | CNPCMaker_Zombie::vfunc5 | 65 | hw_cemetery_1, la_crackhouse_1, la_hospital_1, sp_giovanni_2a, sp_giovanni_2b, sp_giovanni_3 | likely open |
| L4 NPC | navigation | `0x102cc870` | CAI_DynamicLink::vfunc82 | 6 | sp_giovanni_4 | likely open |
| L4 NPC | navigation | `0x102cceb0` | CAI_DynamicLink::vfunc5 | 30 | sp_giovanni_4 | likely open |
| L4 NPC | navigation | `0x102ccee0` | Global::FUN_102ccee0 | 79 | sp_giovanni_4 | likely open |
| L4 NPC | npc_species | `0x103df280` | CNPC_VZombie::Classify | 6 | hw_cemetery_1, la_crackhouse_1, sp_giovanni_3, sp_giovanni_4 | open |
| L4 NPC | npc_species | `0x103ddde0` | CNPC_VZombie::vfunc82 | 6 | hw_cemetery_1, la_crackhouse_1, sp_giovanni_3, sp_giovanni_4 | likely open |
| L4 NPC | npc_species | `0x103de2f0` | CNPC_VZombie::vfunc5 | 43 | hw_cemetery_1, la_crackhouse_1, sp_giovanni_3, sp_giovanni_4 | likely open |
| L4 NPC | npc_species | `0x103de4d0` | CNPC_VZombie::vfunc546 | 29 | hw_cemetery_1, la_crackhouse_1, sp_giovanni_3, sp_giovanni_4 | likely open |
| L4 NPC | npc_species | `0x10363990` | CNPC_VBach::Classify | 6 | sp_soc_4 | open |
| L4 NPC | npc_species | `0x103ca7c0` | CNPC_VWerewolf::~CNPC_VWerewolf | 484 | sp_observatory_2 | open |
| L4 NPC | npc_species | `0x103cb200` | CNPC_VWerewolf::Activate | 116 | sp_observatory_2 | open |
| L4 NPC | npc_species | `0x103ccb50` | CNPC_VWerewolf::IsMonster | 116 | sp_observatory_2 | open |
| L4 NPC | npc_species | `0x103d0780` | CNPC_VWerewolf::GetLastSharedCondition | 119 | sp_observatory_2 | open |
| L4 NPC | npc_species | `0x10362c50` | CNPC_VBach::vfunc580 | 6 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10362c90` | CNPC_VBach::vfunc5 | 30 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10362df0` | CNPC_VBach::vfunc546 | 29 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10363720` | CNPC_VBach::vfunc82 | 6 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10363970` | CNPC_VBach::vfunc473 | 6 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10363b40` | CNPC_VBach::vfunc461 | 15 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x10364550` | CNPC_VBach::vfunc554 | 49 | sp_soc_4 | likely open |
| L4 NPC | npc_species | `0x1036eaf0` | CNPC_VChangBrosBlade::vfunc5 | 43 | sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x1036eb30` | CNPC_VChangBrosBlade::vfunc82 | 6 | sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x1036ecf0` | CNPC_VChangBrosBlade::vfunc546 | 29 | sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x103c87d0` | CNPC_VWerewolf::vfunc82 | 6 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103c8ed0` | CNPC_VWerewolf::vfunc546 | 29 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103ca6d0` | CNPC_VWerewolf::vfunc316 | 3 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103ca750` | CNPC_VWerewolf::vfunc580 | 6 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103ca790` | CNPC_VWerewolf::vfunc5 | 30 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103dc220` | Global::FUN_103dc220 | 40 | sp_observatory_2 | likely open |
| L4 NPC | npc_species | `0x103dc5b0` | Global::FUN_103dc5b0 | 158 | sp_observatory_2 | likely open |
| L5 script | camera_ui | `0x1012cf30` | CHudTimer::InputRestartTimer | 8 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012cf50` | CHudTimer::InputStartTimer | 13 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012cf70` | CHudTimer::InputPauseTimer | 13 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012cf90` | CHudTimer::InputShow | 8 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012cfb0` | CHudTimer::InputHide | 8 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012d720` | CHudTimer::UpdateOnRemove | 16 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | open |
| L5 script | camera_ui | `0x1012d040` | CHudTimer::vfunc82 | 6 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d190` | CHudTimer::vfunc80 | 6 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d1b0` | CHudTimer::vfunc81 | 3 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d440` | CHudTimer::vfunc5 | 43 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d810` | CHudTimer::vfunc86 | 37 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d480` | Global::FUN_1012d480 | 20 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d4e0` | Global::FUN_1012d4e0 | 77 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d550` | Global::FUN_1012d550 | 56 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d5a0` | Global::FUN_1012d5a0 | 15 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d5c0` | Global::FUN_1012d5c0 | 15 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
| L5 script | camera_ui | `0x1012d5e0` | Global::FUN_1012d5e0 | 249 | ch_shrekhub, hw_cemetery_1, la_ventruetower_2, sm_warehouse_1, sp_observatory_2, sp_soc_3, sp_soc_4 | likely open |
