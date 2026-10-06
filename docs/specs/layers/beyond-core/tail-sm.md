# Tail — Santa Monica (`sm_*`)

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

168 open, partial or likely-open functions that only these maps reach (7 KB).
Their witnesses are records on their own maps (`stage: map:<map>`); a map not baked yet is baked first.

## The maps

| map | baked | classes only this group spawns |
|---|---|---|
| `sm_apartment_1` | yes | CItemGMercurioJournal, CResearchBook_LG_FIREARMS, npc ProneDialog |
| `sm_asylum_1` | yes | — |
| `sm_bailbonds_1` | yes | — |
| `sm_basement_1` | yes | — |
| `sm_beachhouse_1` | **no** | CItemGAstrolite, CTriggerDisciplineContext |
| `sm_coffee_1` | yes | — |
| `sm_diner_1` | yes | CItemGLillyPurse |
| `sm_gallery_1` | yes | CItemGCashBox, CPropSlashable |
| `sm_hub_1` | yes | CItemGLillyDiary, CWeaponMelee_TireIron |
| `sm_hub_2` | yes | npc ProneDialog |
| `sm_junkyard_1` | **no** | — |
| `sm_medical_1` | yes | CItemGWerewolfBlood, CResearchBook_LG_COMP, npc ProneDialog |
| `sm_oceanhouse_1` | yes | — |
| `sm_oceanhouse_2` | yes | CEnvParticleHUD, CItemGGhostPendant, CItemGOHDiary, CWeaponMelee_FireAxe, npc VBrujah |
| `sm_pawnshop_1` | yes | CGameText, CResearchBook_LG_STEALTH |
| `sm_pawnshop_2` | yes | — |
| `sm_pier_1` | yes | CItemGStake, CWeaponMelee_MingXiaoTentacle |
| `sm_shreknet_1` | yes | — |
| `sm_smoke_1` | yes | — |
| `sm_tattoo` | yes | — |
| `sm_vamparena` | yes | npc VAsianVampire |
| `sm_warehouse_1` | yes | CEnvParticleHUD, CGameText, CTriggerCheckVolume, CTriggerDisciplineContext |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | movers_doors | `0x101928f0` | CPropMover::Spawn | 16 | hw_warrens_2b, sm_beachhouse_1, sm_oceanhouse_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x101928a0` | CPropMover::vfunc117 | 8 | hw_warrens_2b, sm_beachhouse_1, sm_oceanhouse_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x101928c0` | CPropMover::vfunc5 | 30 | hw_warrens_2b, sm_beachhouse_1, sm_oceanhouse_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x10192910` | CPropMover::vfunc113 | 20 | hw_warrens_2b, sm_beachhouse_1, sm_oceanhouse_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x1010f1b0` | CPushable::CausesImpactDamage | 5 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x1010f1d0` | CPushable::ReceivesImpactDamage | 5 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x1010f290` | CPushable::Spawn | 220 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x1010f440` | CPushable::OnTakeDamage | 23 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x1010f470` | CPushable::VPhysicsCollision | 53 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | open |
| L0 entity | movers_doors | `0x10225db0` | CPropPadlock::Spawn | 270 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | open |
| L0 entity | movers_doors | `0x1010f0a0` | CPushable::vfunc82 | 6 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x1010f1f0` | CPushable::vfunc5 | 54 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x1010f4c0` | CPushable::vfunc35 | 9 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x10225d00` | CPropPadlock::vfunc117 | 8 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x10225d20` | CPropPadlock::vfunc5 | 109 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x10225f10` | CPropPadlock::vfunc113 | 56 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x10225f60` | CPropPadlock::vfunc274 | 313 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x102260f0` | CPropPadlock::vfunc266 | 123 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x100ee750` | Global::FUN_100ee750 | 133 | ch_lotus_1, sm_hub_1, sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | movers_doors | `0x1010f3d0` | CPushable::vfunc223 | 67 | ch_temple_2, la_confession_1, sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L0 entity | movers_doors | `0x103f4af0` | CWeaponLockpick::vfunc80 | 6 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f4b10` | CWeaponLockpick::vfunc81 | 3 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f4cc0` | CWeaponLockpick::vfunc362 | 6 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f4ce0` | CWeaponLockpick::vfunc363 | 6 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f4d70` | CWeaponLockpick::vfunc268 | 3 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f4f40` | CWeaponLockpick::vfunc292 | 120 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x103f5070` | CWeaponLockpick::vfunc285 | 44 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | movers_doors | `0x10409af0` | CWeaponLockpick::vfunc5 | 30 | sm_warehouse_1, sp_tutorial_1 | likely open |
| L0 entity | physics | `0x10027700` | CPhysForce::Spawn | 42 | sm_warehouse_1, sp_soc_3 | open |
| L0 entity | physics | `0x10027740` | CPhysForce::FUN_10027740 | 39 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027780` | CPhysForce::FUN_10027780 | 146 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027c20` | CPhysThruster::vfunc82 | 6 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027cb0` | CPhysThruster::vfunc242 | 228 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027de0` | CPhysThruster::vfunc241 | 396 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10029b50` | CPhysThruster::vfunc5 | 30 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10025f00` | Global::FUN_10025f00 | 81 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027670` | CPhysForce::FUN_10027670 | 1 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x100276c0` | Global::FUN_100276c0 | 42 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x100278e0` | Global::FUN_100278e0 | 82 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027960` | Global::FUN_10027960 | 208 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10027fe0` | Global::FUN_10027fe0 | 90 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x10028060` | Global::FUN_10028060 | 81 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x100280e0` | Global::FUN_100280e0 | 81 | sm_warehouse_1, sp_soc_3 | likely open |
| L0 entity | physics | `0x101932c0` | CPropSlashable::Spawn | 160 | sm_gallery_1 | open |
| L0 entity | physics | `0x10193390` | CPropSlashable::OnTakeDamage | 178 | sm_gallery_1 | open |
| L0 entity | physics | `0x10193160` | CPropSlashable::vfunc82 | 6 | sm_gallery_1 | likely open |
| L0 entity | physics | `0x10194580` | CPropSlashable::vfunc5 | 43 | sm_gallery_1 | likely open |
| L0 entity | triggers | `0x102108a0` | CTriggerPlayerActivityLevel::Spawn | 16 | la_skyline_1, sm_diner_1, sm_hub_1, sm_medical_1 | open |
| L0 entity | triggers | `0x102108e0` | CTriggerPlayerActivityLevel::Touch | 154 | la_skyline_1, sm_diner_1, sm_hub_1, sm_medical_1 | open |
| L0 entity | triggers | `0x102109b0` | CTriggerPlayerActivityLevel::EndTouch | 199 | la_skyline_1, sm_diner_1, sm_hub_1, sm_medical_1 | open |
| L0 entity | triggers | `0x10210730` | CTriggerPlayerActivityLevel::vfunc82 | 6 | la_skyline_1, sm_diner_1, sm_hub_1, sm_medical_1 | likely open |
| L0 entity | triggers | `0x10210830` | CTriggerPlayerActivityLevel::vfunc5 | 76 | la_skyline_1, sm_diner_1, sm_hub_1, sm_medical_1 | likely open |
| L0 entity | triggers | `0x101c97d0` | CTriggerSave::Spawn | 37 | sm_pawnshop_1, sp_tutorial_1 | open |
| L0 entity | triggers | `0x101c9810` | CTriggerSave::Touch | 48 | sm_pawnshop_1, sp_tutorial_1 | open |
| L0 entity | triggers | `0x10210da0` | CTriggerDisciplineContext::Spawn | 16 | sm_beachhouse_1, sm_warehouse_1 | open |
| L0 entity | triggers | `0x10210dc0` | CTriggerDisciplineContext::StartTouch | 76 | sm_beachhouse_1, sm_warehouse_1 | open |
| L0 entity | triggers | `0x10210e30` | CTriggerDisciplineContext::Touch | 3 | sm_beachhouse_1, sm_warehouse_1 | open |
| L0 entity | triggers | `0x10210e50` | CTriggerDisciplineContext::EndTouch | 68 | sm_beachhouse_1, sm_warehouse_1 | open |
| L0 entity | triggers | `0x101c4980` | CBaseTrigger::FUN_101c4980 | 6 | sm_pawnshop_1, sp_tutorial_1 | likely open |
| L0 entity | triggers | `0x101cc070` | CTriggerSave::vfunc5 | 76 | sm_pawnshop_1, sp_tutorial_1 | likely open |
| L0 entity | triggers | `0x10210c50` | CTriggerDisciplineContext::vfunc82 | 6 | sm_beachhouse_1, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x10210d30` | CTriggerDisciplineContext::vfunc5 | 76 | sm_beachhouse_1, sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x101cb8a0` | CTriggerCheckVolume::Spawn | 16 | sm_warehouse_1 | open |
| L0 entity | triggers | `0x101cc7d0` | CTriggerCheckVolume::InputCheckNow | 8 | sm_warehouse_1 | open |
| L0 entity | triggers | `0x101cb650` | CTriggerCheckVolume::vfunc82 | 6 | sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x101cc350` | CTriggerCheckVolume::vfunc5 | 87 | sm_warehouse_1 | likely open |
| L0 entity | triggers | `0x101cb7d0` | CTriggerCheckVolume::vfunc251 | 159 | sm_warehouse_1 | likely open |
| L1 animation | animation | `0x1008a760` | CBaseAnimating::FUN_1008a760 | 6 | hw_warrens_2b, sm_beachhouse_1, sm_oceanhouse_1, sm_oceanhouse_2, sm_warehouse_1, sp_observatory_2 | open |
| L1 animation | animation | `0x101aad20` | Global::FUN_101aad20 | 77 | ch_hub_1, hw_hub_1, sm_diner_1, sm_tattoo | likely open |
| L2 character | inventory_ui_items | `0x103f5ef0` | CItemMMoneyEnvelope::vfunc80 | 6 | ch_lotus_1, hw_sinbin_1, la_skyline_1, sm_beachhouse_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103f5f10` | CItemMMoneyEnvelope::vfunc81 | 3 | ch_lotus_1, hw_sinbin_1, la_skyline_1, sm_beachhouse_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x10409b80` | CItemMMoneyEnvelope::vfunc5 | 30 | ch_lotus_1, hw_sinbin_1, la_skyline_1, sm_beachhouse_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe360` | CItemGRingSilver::vfunc80 | 6 | la_crackhouse_1, sm_apartment_1, sm_medical_1, sm_warehouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103fe380` | CItemGRingSilver::vfunc81 | 3 | la_crackhouse_1, sm_apartment_1, sm_medical_1, sm_warehouse_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a300` | CItemGRingSilver::vfunc5 | 30 | la_crackhouse_1, sm_apartment_1, sm_medical_1, sm_warehouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103fdb50` | CItemGDrugsPillBottle::vfunc80 | 6 | la_hub_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103fdb70` | CItemGDrugsPillBottle::vfunc81 | 3 | la_hub_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a270` | CItemGDrugsPillBottle::vfunc5 | 30 | la_hub_1, sm_medical_1, sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103f67d0` | CItemGAstrolite::Spawn | 17 | sm_beachhouse_1 | open |
| L2 character | inventory_ui_items | `0x103f6b90` | CItemGAstrolite::Use | 26 | sm_beachhouse_1 | open |
| L2 character | inventory_ui_items | `0x103f6530` | CItemGAstrolite::vfunc80 | 6 | sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6550` | CItemGAstrolite::vfunc81 | 3 | sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6af0` | CItemGAstrolite::vfunc35 | 20 | sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6b50` | CItemGAstrolite::vfunc117 | 41 | sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6c30` | CResearchBook_LG_COMP::vfunc80 | 6 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x103f6c50` | CResearchBook_LG_COMP::vfunc81 | 3 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8a30` | CResearchBook_LG_STEALTH::vfunc80 | 6 | sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8a50` | CResearchBook_LG_STEALTH::vfunc81 | 3 | sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8d30` | CResearchBook_LG_FIREARMS::vfunc80 | 6 | sm_apartment_1 | likely open |
| L2 character | inventory_ui_items | `0x103f8d50` | CResearchBook_LG_FIREARMS::vfunc81 | 3 | sm_apartment_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd090` | CItemGCashBox::vfunc80 | 6 | sm_gallery_1 | likely open |
| L2 character | inventory_ui_items | `0x103fd0b0` | CItemGCashBox::vfunc81 | 3 | sm_gallery_1 | likely open |
| L2 character | inventory_ui_items | `0x103fde00` | CItemGGhostPendant::vfunc80 | 6 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x103fde20` | CItemGGhostPendant::vfunc81 | 3 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x103ff0d0` | CItemGLillyDiary::vfunc80 | 6 | sm_hub_1 | likely open |
| L2 character | inventory_ui_items | `0x103ff0f0` | CItemGLillyDiary::vfunc81 | 3 | sm_hub_1 | likely open |
| L2 character | inventory_ui_items | `0x103ff380` | CItemGLillyPurse::vfunc80 | 6 | sm_diner_1 | likely open |
| L2 character | inventory_ui_items | `0x103ff3a0` | CItemGLillyPurse::vfunc81 | 3 | sm_diner_1 | likely open |
| L2 character | inventory_ui_items | `0x103ffb90` | CItemGWerewolfBlood::vfunc80 | 6 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x103ffbb0` | CItemGWerewolfBlood::vfunc81 | 3 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x10400900` | CItemGOHDiary::vfunc80 | 6 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x10400920` | CItemGOHDiary::vfunc81 | 3 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x10403ec0` | CItemGStake::vfunc80 | 6 | sm_pier_1 | likely open |
| L2 character | inventory_ui_items | `0x10403ee0` | CItemGStake::vfunc81 | 3 | sm_pier_1 | likely open |
| L2 character | inventory_ui_items | `0x10404980` | CItemGMercurioJournal::vfunc80 | 6 | sm_apartment_1 | likely open |
| L2 character | inventory_ui_items | `0x104049a0` | CItemGMercurioJournal::vfunc81 | 3 | sm_apartment_1 | likely open |
| L2 character | inventory_ui_items | `0x10409be0` | CItemGAstrolite::vfunc5 | 30 | sm_beachhouse_1 | likely open |
| L2 character | inventory_ui_items | `0x10409c10` | CResearchBook_LG_COMP::vfunc5 | 30 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x10409df0` | CResearchBook_LG_STEALTH::vfunc5 | 30 | sm_pawnshop_1 | likely open |
| L2 character | inventory_ui_items | `0x10409e20` | CResearchBook_LG_FIREARMS::vfunc5 | 30 | sm_apartment_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a1b0` | CItemGCashBox::vfunc5 | 30 | sm_gallery_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a2a0` | CItemGGhostPendant::vfunc5 | 30 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a3f0` | CItemGLillyDiary::vfunc5 | 30 | sm_hub_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a420` | CItemGLillyPurse::vfunc5 | 30 | sm_diner_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a4b0` | CItemGWerewolfBlood::vfunc5 | 30 | sm_medical_1 | likely open |
| L2 character | inventory_ui_items | `0x1040a5a0` | CItemGOHDiary::vfunc5 | 30 | sm_oceanhouse_2 | likely open |
| L2 character | inventory_ui_items | `0x1040a960` | CItemGStake::vfunc5 | 30 | sm_pier_1 | likely open |
| L2 character | inventory_ui_items | `0x1040aa20` | CItemGMercurioJournal::vfunc5 | 30 | sm_apartment_1 | likely open |
| L2 character | weapons | `0x103e5f30` | CWeaponMelee_FireAxe::Spawn | 17 | sm_oceanhouse_2 | open |
| L2 character | weapons | `0x103e8000` | CWeaponMelee_TireIron::Spawn | 17 | sm_hub_1 | open |
| L2 character | weapons | `0x103e5cb0` | CWeaponMelee_FireAxe::vfunc80 | 6 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e5cd0` | CWeaponMelee_FireAxe::vfunc81 | 3 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e5e80` | CWeaponMelee_FireAxe::vfunc362 | 6 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e5ea0` | CWeaponMelee_FireAxe::vfunc363 | 6 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e5f60` | CWeaponMelee_FireAxe::vfunc298 | 63 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e5fb0` | CWeaponMelee_FireAxe::vfunc300 | 53 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103e7d80` | CWeaponMelee_TireIron::vfunc80 | 6 | sm_hub_1 | likely open |
| L2 character | weapons | `0x103e7da0` | CWeaponMelee_TireIron::vfunc81 | 3 | sm_hub_1 | likely open |
| L2 character | weapons | `0x103e7f50` | CWeaponMelee_TireIron::vfunc362 | 6 | sm_hub_1 | likely open |
| L2 character | weapons | `0x103e7f70` | CWeaponMelee_TireIron::vfunc363 | 6 | sm_hub_1 | likely open |
| L2 character | weapons | `0x103e8030` | CWeaponMelee_TireIron::vfunc360 | 11 | sm_hub_1 | likely open |
| L2 character | weapons | `0x103ec5f0` | CWeaponMelee_MingXiaoTentacle::vfunc80 | 6 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec610` | CWeaponMelee_MingXiaoTentacle::vfunc81 | 3 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec7c0` | CWeaponMelee_MingXiaoTentacle::vfunc362 | 6 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec7e0` | CWeaponMelee_MingXiaoTentacle::vfunc363 | 6 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec8c0` | CWeaponMelee_MingXiaoTentacle::vfunc373 | 4 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec8e0` | CWeaponMelee_MingXiaoTentacle::vfunc5 | 30 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103ec910` | CWeaponMelee_MingXiaoTentacle::vfunc367 | 206 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103eca50` | CWeaponMelee_MingXiaoTentacle::vfunc269 | 191 | sm_pier_1 | likely open |
| L2 character | weapons | `0x103eccc0` | CWeaponMelee_FireAxe::vfunc5 | 30 | sm_oceanhouse_2 | likely open |
| L2 character | weapons | `0x103ece40` | CWeaponMelee_TireIron::vfunc5 | 30 | sm_hub_1 | likely open |
| L4 NPC | npc_species | `0x10371b50` | CNPC_VCop::Classify | 6 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | open |
| L4 NPC | npc_species | `0x103705e0` | Global::FUN_103705e0 | 63 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | open |
| L4 NPC | npc_species | `0x10370460` | CNPC_VCop::vfunc82 | 6 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | likely open |
| L4 NPC | npc_species | `0x10370970` | CNPC_VCop::vfunc5 | 30 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | likely open |
| L4 NPC | npc_species | `0x10370ad0` | CNPC_VCop::vfunc546 | 29 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | likely open |
| L4 NPC | npc_species | `0x10372aa0` | CNPC_VCop::vfunc432 | 13 | ch_hub_1, hw_hub_1, sm_beachhouse_1, sm_hub_1, sm_pier_1 | likely open |
| L4 NPC | npc_species | `0x10374080` | CNPC_VDog::Classify | 6 | hw_warrens_1, sm_beachhouse_1, sm_junkyard_1 | open |
| L4 NPC | npc_species | `0x10373570` | CNPC_VDog::vfunc5 | 30 | hw_warrens_1, sm_beachhouse_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x103736d0` | CNPC_VDog::vfunc546 | 29 | hw_warrens_1, sm_beachhouse_1, sm_junkyard_1 | likely open |
| L4 NPC | npc_species | `0x103a4d40` | CNPC_ProneDialog::vfunc5 | 30 | sm_apartment_1, sm_hub_2, sm_medical_1 | likely open |
| L4 NPC | npc_species | `0x10367f00` | CNPC_VBrujah::Classify | 6 | sm_oceanhouse_2 | open |
| L4 NPC | npc_species | `0x103603d0` | CNPC_VAsianVampire::vfunc580 | 6 | sm_vamparena | likely open |
| L4 NPC | npc_species | `0x10360410` | CNPC_VAsianVampire::vfunc5 | 43 | sm_vamparena | likely open |
| L4 NPC | npc_species | `0x10360450` | CNPC_VAsianVampire::vfunc82 | 6 | sm_vamparena | likely open |
| L4 NPC | npc_species | `0x10360610` | CNPC_VAsianVampire::vfunc546 | 29 | sm_vamparena | likely open |
| L4 NPC | npc_species | `0x103678b0` | CNPC_VBrujah::vfunc5 | 30 | sm_oceanhouse_2 | likely open |
| L4 NPC | npc_species | `0x10367a10` | CNPC_VBrujah::vfunc546 | 29 | sm_oceanhouse_2 | likely open |
| L5 script | camera_ui | `0x100fc500` | CEnvParticleHUD::Spawn | 74 | sm_oceanhouse_2, sm_warehouse_1 | open |
| L5 script | camera_ui | `0x1020e1e0` | CGameText::Use | 13 | sm_pawnshop_1, sm_warehouse_1 | open |
| L5 script | camera_ui | `0x1020e3a0` | CGameText::InputDisplay | 15 | sm_pawnshop_1, sm_warehouse_1 | open |
| L5 script | camera_ui | `0x1020e4f0` | CGameText::InputDisplayWindow | 338 | sm_pawnshop_1, sm_warehouse_1 | open |
| L5 script | camera_ui | `0x100fc570` | CEnvParticleHUD::vfunc241 | 62 | sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L5 script | camera_ui | `0x100fd4d0` | CEnvParticleHUD::vfunc5 | 30 | sm_oceanhouse_2, sm_warehouse_1 | likely open |
| L5 script | camera_ui | `0x1020e200` | CGameText::vfunc82 | 6 | sm_pawnshop_1, sm_warehouse_1 | likely open |
| L5 script | camera_ui | `0x1020e290` | CGameText::vfunc110 | 203 | sm_pawnshop_1, sm_warehouse_1 | likely open |
| L5 script | camera_ui | `0x10216e00` | CGameText::vfunc5 | 30 | sm_pawnshop_1, sm_warehouse_1 | likely open |
| L5 script | camera_ui | `0x1020e6b0` | Global::FUN_1020e6b0 | 108 | sm_pawnshop_1, sm_warehouse_1 | likely open |
