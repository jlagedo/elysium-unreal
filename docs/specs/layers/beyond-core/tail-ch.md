# Tail — Chinatown (`ch_*`)

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

124 open, partial or likely-open functions that only these maps reach (6 KB).
Their witnesses are records on their own maps (`stage: map:<map>`); a map not baked yet is baked first.

## The maps

| map | baked | classes only this group spawns |
|---|---|---|
| `ch_cloud_1` | yes | — |
| `ch_dragon_1` | yes | CItemGWirelessCamera4 |
| `ch_fishmarket_1` | yes | npc VHengeyokai, npc VYukie |
| `ch_fulab_1` | yes | CItemGWirelessCamera3, CResearchBook_HG_MELEE, CWeaponMelee_Katana |
| `ch_glaze_1` | yes | — |
| `ch_hub_1` | yes | npc VYukie |
| `ch_lotus_1` | yes | — |
| `ch_ramen_1` | yes | — |
| `ch_shrekhub` | yes | CItemGWirelessCamera2, CWeaponMelee_SabbatLeaderAttack |
| `ch_temple_1` | yes | — |
| `ch_temple_2` | yes | CFilterMass |
| `ch_temple_3` | yes | CItemContainerOneItemFiltered, CItemGIdolCat, CItemGIdolCrane, CItemGIdolDragon, CItemGIdolElephant |
| `ch_temple_4` | yes | CItemKSarcophagusKey, npc VMingXiao |
| `ch_tsengs_1` | yes | — |
| `ch_zhaos_1` | yes | — |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | entity_io | `0x101079a0` | CFilterMultiple::vfunc82 | 6 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x10107a30` | CFilterMultiple::vfunc113 | 69 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x10107aa0` | CFilterMultiple::vfunc241 | 191 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x10108130` | CFilterInventory::vfunc241 | 208 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x10108240` | CFilterInventory::vfunc82 | 6 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x10108580` | CFilterMultiple::vfunc5 | 54 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x101086c0` | CFilterInventory::vfunc5 | 54 | ch_temple_3, hw_tawni_1, la_library_1, sp_theatre | likely open |
| L0 entity | entity_io | `0x1020fe70` | CLogicVisible::InputCheckVisibility | 619 | ch_glaze_1, sm_medical_1 | open |
| L0 entity | entity_io | `0x1020f850` | CLogicVisible::vfunc82 | 6 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x1020fa30` | CLogicVisible::vfunc5 | 120 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x101d20a0` | Global::FUN_101d20a0 | 274 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x1020fad0` | Global::FUN_1020fad0 | 734 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x10210180` | Global::FUN_10210180 | 50 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x102101d0` | Global::FUN_102101d0 | 89 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x10210250` | Global::FUN_10210250 | 112 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x102102e0` | Global::FUN_102102e0 | 773 | ch_glaze_1, sm_medical_1 | likely open |
| L0 entity | entity_io | `0x10107f80` | CFilterMass::vfunc241 | 88 | ch_temple_2 | likely open |
| L0 entity | entity_io | `0x10108000` | CFilterMass::vfunc82 | 6 | ch_temple_2 | likely open |
| L0 entity | entity_io | `0x10108670` | CFilterMass::vfunc5 | 54 | ch_temple_2 | likely open |
| L0 entity | movers_doors | `0x1021da40` | CPropKeypad::LoadTextStrings | 306 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | open |
| L0 entity | movers_doors | `0x1021d6d0` | CPropKeypad::vfunc82 | 6 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021d8e0` | CPropKeypad::vfunc5 | 131 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021d9a0` | CPropKeypad::vfunc113 | 21 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021dc00` | CPropKeypad::vfunc33 | 6 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021dc20` | CPropKeypad::vfunc274 | 16 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021dc40` | CPropKeypad::vfunc275 | 86 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021dcc0` | CPropKeypad::vfunc276 | 167 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021ddc0` | CPropKeypad::vfunc32 | 20 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021ddf0` | CPropKeypad::vfunc39 | 53 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021de40` | CPropKeypad::vfunc42 | 24 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021de70` | CPropKeypad::vfunc41 | 36 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021e0b0` | CPropKeypad::vfunc278 | 51 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x1021df90` | CPropKeypad::vfunc277 | 219 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x10248b60` | Global::FUN_10248b60 | 61 | ch_fulab_1, ch_hub_1, ch_shrekhub, la_museum_1, la_skyline_1, sm_medical_1 | likely open |
| L0 entity | movers_doors | `0x101c1bc0` | Global::FUN_101c1bc0 | 163 | ch_temple_3, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x102264f0` | CItemContainerLock::Spawn | 24 | ch_temple_1, ch_temple_2, hw_metalhead_1, hw_sinbin_1, la_skyline_1, sm_apartment_1, sm_medical_1, sp_tutorial_1 | open |
| L2 character | inventory_ui_items | `0x10226460` | CItemContainerLock::vfunc5 | 109 | ch_temple_1, ch_temple_2, hw_metalhead_1, hw_sinbin_1, la_skyline_1, sm_apartment_1, sm_medical_1, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x10226520` | CItemContainerLock::vfunc113 | 123 | ch_temple_1, ch_temple_2, hw_metalhead_1, hw_sinbin_1, la_skyline_1, sm_apartment_1, sm_medical_1, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x10209ac0` | Global::FUN_10209ac0 | 42 | ch_temple_1, ch_temple_2, hw_metalhead_1, hw_sinbin_1, la_skyline_1, sm_apartment_1, sm_medical_1, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe0b0` | CItemGRing03::vfunc80 | 6 | ch_glaze_1, hw_609_1, la_empire_2 | likely open |
| L2 character | inventory_ui_items | `0x103fe0d0` | CItemGRing03::vfunc81 | 3 | ch_glaze_1, hw_609_1, la_empire_2 | likely open |
| L2 character | inventory_ui_items | `0x103fe8c0` | CItemGWatchNormal::vfunc80 | 6 | ch_dragon_1, hw_asphole_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe8e0` | CItemGWatchNormal::vfunc81 | 3 | ch_dragon_1, hw_asphole_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a2d0` | CItemGRing03::vfunc5 | 30 | ch_glaze_1, hw_609_1, la_empire_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a360` | CItemGWatchNormal::vfunc5 | 30 | ch_dragon_1, hw_asphole_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x102099f0` | CItemContainer::FUN_102099f0 | 20 | ch_temple_3, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x103feb70` | CItemGWatchFancy::vfunc80 | 6 | ch_lotus_1, hw_sinbin_1 | likely open |
| L2 character | inventory_ui_items | `0x103feb90` | CItemGWatchFancy::vfunc81 | 3 | ch_lotus_1, hw_sinbin_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a390` | CItemGWatchFancy::vfunc5 | 30 | ch_lotus_1, hw_sinbin_1 | likely open |
| L2 character | inventory_ui_items | `0x10208c30` | CItemContainer::FUN_10208c30 | 31 | ch_temple_3, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x10208c60` | CItemContainer::FUN_10208c60 | 31 | ch_temple_3, sp_tutorial_1 | likely open |
| L2 character | inventory_ui_items | `0x10209fc0` | CItemContainerOneItemFiltered::Spawn | 27 | ch_temple_3 | open |
| L2 character | inventory_ui_items | `0x10209f90` | CItemContainerOneItemFiltered::vfunc5 | 30 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x103f8130` | CResearchBook_HG_MELEE::vfunc80 | 6 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8150` | CResearchBook_HG_MELEE::vfunc81 | 3 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x10403150` | CItemGWirelessCamera2::vfunc80 | 6 | ch_shrekhub | likely open |
| L2 character | inventory_ui_items | `0x10403170` | CItemGWirelessCamera2::vfunc81 | 3 | ch_shrekhub | likely open |
| L2 character | inventory_ui_items | `0x10403400` | CItemGWirelessCamera3::vfunc80 | 6 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x10403420` | CItemGWirelessCamera3::vfunc81 | 3 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x104036b0` | CItemGWirelessCamera4::vfunc80 | 6 | ch_dragon_1 | likely open |
| L2 character | inventory_ui_items | `0x104036d0` | CItemGWirelessCamera4::vfunc81 | 3 | ch_dragon_1 | likely open |
| L2 character | inventory_ui_items | `0x10405190` | CItemGIdolCat::vfunc80 | 6 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x104051b0` | CItemGIdolCat::vfunc81 | 3 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x10405440` | CItemGIdolCrane::vfunc80 | 6 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x10405460` | CItemGIdolCrane::vfunc81 | 3 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x104056f0` | CItemGIdolDragon::vfunc80 | 6 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x10405710` | CItemGIdolDragon::vfunc81 | 3 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x104059a0` | CItemGIdolElephant::vfunc80 | 6 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x104059c0` | CItemGIdolElephant::vfunc81 | 3 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x104071d0` | CItemKSarcophagusKey::vfunc80 | 6 | ch_temple_4 | likely open |
| L2 character | inventory_ui_items | `0x104071f0` | CItemKSarcophagusKey::vfunc81 | 3 | ch_temple_4 | likely open |
| L2 character | inventory_ui_items | `0x10409d60` | CResearchBook_HG_MELEE::vfunc5 | 30 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a870` | CItemGWirelessCamera2::vfunc5 | 30 | ch_shrekhub | likely open |
| L2 character | inventory_ui_items | `0x1040a8a0` | CItemGWirelessCamera3::vfunc5 | 30 | ch_fulab_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a8d0` | CItemGWirelessCamera4::vfunc5 | 30 | ch_dragon_1 | likely open |
| L2 character | inventory_ui_items | `0x1040aab0` | CItemGIdolCat::vfunc5 | 30 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x1040aae0` | CItemGIdolCrane::vfunc5 | 30 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x1040ab10` | CItemGIdolDragon::vfunc5 | 30 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x1040ab40` | CItemGIdolElephant::vfunc5 | 30 | ch_temple_3 | likely open |
| L2 character | inventory_ui_items | `0x1040acf0` | CItemKSarcophagusKey::vfunc5 | 30 | ch_temple_4 | likely open |
| L2 character | weapons | `0x103e90b0` | CWeaponMelee_SabbatLeaderAttack::Spawn | 17 | ch_shrekhub | open |
| L2 character | weapons | `0x103e60c0` | CWeaponMelee_Katana::vfunc80 | 6 | ch_fulab_1 | likely open |
| L2 character | weapons | `0x103e60e0` | CWeaponMelee_Katana::vfunc81 | 3 | ch_fulab_1 | likely open |
| L2 character | weapons | `0x103e6290` | CWeaponMelee_Katana::vfunc362 | 6 | ch_fulab_1 | likely open |
| L2 character | weapons | `0x103e62b0` | CWeaponMelee_Katana::vfunc363 | 6 | ch_fulab_1 | likely open |
| L2 character | weapons | `0x103e8e30` | CWeaponMelee_SabbatLeaderAttack::vfunc80 | 6 | ch_shrekhub | likely open |
| L2 character | weapons | `0x103e8e50` | CWeaponMelee_SabbatLeaderAttack::vfunc81 | 3 | ch_shrekhub | likely open |
| L2 character | weapons | `0x103e9000` | CWeaponMelee_SabbatLeaderAttack::vfunc362 | 6 | ch_shrekhub | likely open |
| L2 character | weapons | `0x103e9020` | CWeaponMelee_SabbatLeaderAttack::vfunc363 | 6 | ch_shrekhub | likely open |
| L2 character | weapons | `0x103e90e0` | CWeaponMelee_SabbatLeaderAttack::vfunc373 | 6 | ch_shrekhub | likely open |
| L2 character | weapons | `0x103eccf0` | CWeaponMelee_Katana::vfunc5 | 30 | ch_fulab_1 | likely open |
| L2 character | weapons | `0x103eced0` | CWeaponMelee_SabbatLeaderAttack::vfunc5 | 30 | ch_shrekhub | likely open |
| L4 NPC | npc_species | `0x1036a190` | CNPC_VChangBros::Classify | 6 | ch_temple_2, sp_giovanni_5 | open |
| L4 NPC | npc_species | `0x103dd650` | CNPC_VYukie::Classify | 6 | ch_fishmarket_1, ch_hub_1 | open |
| L4 NPC | npc_species | `0x1036f2f0` | CNPC_VChangBrosClaw::vfunc5 | 43 | ch_temple_2, sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x1036f330` | CNPC_VChangBrosClaw::vfunc82 | 6 | ch_temple_2, sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x1036f4f0` | CNPC_VChangBrosClaw::vfunc546 | 29 | ch_temple_2, sp_giovanni_5 | likely open |
| L4 NPC | npc_species | `0x103dd050` | CNPC_VYukie::vfunc580 | 6 | ch_fishmarket_1, ch_hub_1 | likely open |
| L4 NPC | npc_species | `0x103dd090` | CNPC_VYukie::vfunc5 | 30 | ch_fishmarket_1, ch_hub_1 | likely open |
| L4 NPC | npc_species | `0x103dd1f0` | CNPC_VYukie::vfunc546 | 29 | ch_fishmarket_1, ch_hub_1 | likely open |
| L4 NPC | npc_species | `0x103dd780` | CNPC_VYukie::vfunc461 | 15 | ch_fishmarket_1, ch_hub_1 | likely open |
| L4 NPC | npc_species | `0x103dd7a0` | CNPC_VYukie::vfunc432 | 13 | ch_fishmarket_1, ch_hub_1 | likely open |
| L4 NPC | npc_species | `0x1037fb00` | CNPC_VHengeyokai::Classify | 6 | ch_fishmarket_1 | open |
| L4 NPC | npc_species | `0x10380f70` | CNPC_VHengeyokai::IsMonster | 3 | ch_fishmarket_1 | open |
| L4 NPC | npc_species | `0x10383540` | CNPC_VHengeyokai::ReceivesImpactDamage | 5 | ch_fishmarket_1 | open |
| L4 NPC | npc_species | `0x103929f0` | CNPC_VMingXiao::Classify | 6 | ch_temple_4 | open |
| L4 NPC | npc_species | `0x10395270` | CNPC_VMingXiao::GetLastSharedCondition | 6 | ch_temple_4 | open |
| L4 NPC | npc_species | `0x10395f80` | CNPC_VMingXiao::Save | 81 | ch_temple_4 | open |
| L4 NPC | npc_species | `0x10396fb0` | CNPC_VMingXiao::IsMonster | 3 | ch_temple_4 | open |
| L4 NPC | npc_species | `0x1037e4f0` | CNPC_VHengeyokai::vfunc82 | 6 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x1037e810` | CNPC_VHengeyokai::vfunc378 | 5 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x1037e830` | CNPC_VHengeyokai::vfunc580 | 6 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x1037e870` | CNPC_VHengeyokai::vfunc5 | 134 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x1037ea60` | CNPC_VHengeyokai::vfunc546 | 29 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x1037fae0` | CNPC_VHengeyokai::vfunc473 | 6 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x10380100` | CNPC_VHengeyokai::vfunc461 | 15 | ch_fishmarket_1 | likely open |
| L4 NPC | npc_species | `0x10390de0` | CNPC_VMingXiao::vfunc82 | 6 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x10391070` | CNPC_VMingXiao::vfunc580 | 6 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x103910b0` | CNPC_VMingXiao::vfunc5 | 134 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x10391390` | CNPC_VMingXiao::vfunc546 | 29 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x103929d0` | CNPC_VMingXiao::vfunc473 | 6 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x10394970` | CNPC_VMingXiao::vfunc432 | 13 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x10395dc0` | CNPC_VMingXiao::vfunc599 | 23 | ch_temple_4 | likely open |
| L4 NPC | npc_species | `0x10396000` | CNPC_VMingXiao::vfunc127 | 53 | ch_temple_4 | likely open |
