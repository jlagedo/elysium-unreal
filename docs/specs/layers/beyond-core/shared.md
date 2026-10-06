# Shared band — code 11 to 53 maps need

**Standing rule:** follow retail behaviour; the core of every layer is finished first. These rows were
not settled tonight (only the core was): most statuses are inferred from callers, so each spec starts
by settling its rows (the read-only sweep of 2026-10-06: `$ELYSIUM_WORK_ROOT/build-order/settle_*`),
then plans them into stories the way the core was planned, then builds them, layer order inside.

670 open, partial or likely-open functions, 69 KB of retail code. Built after the
core of their layer, before the map-specific tails.

| layer | subsystem | functions |
|---|---|---|
| L0 entity | effects_world | 49 |
| L0 entity | entity_core | 10 |
| L0 entity | entity_io | 10 |
| L0 entity | movers_doors | 84 |
| L0 entity | physics | 152 |
| L0 entity | triggers | 17 |
| L1 animation | animation | 1 |
| L2 character | combat | 4 |
| L2 character | inventory_ui_items | 41 |
| L2 character | rpg | 1 |
| L2 character | weapons | 9 |
| L4 NPC | hints_places | 7 |
| L4 NPC | makers | 3 |
| L4 NPC | npc_kernel | 12 |
| L4 NPC | npc_species | 50 |
| L4 NPC | perception | 5 |
| L5 script | camera_ui | 47 |
| L5 script | python | 3 |
| L5 script | scripted | 165 |

## Rows

| layer | subsystem | address | function | bytes | maps | status |
|---|---|---|---|---|---|---|
| L0 entity | effects_world | `0x1010c3d0` | CAreaPortal::FUN_1010c3d0 | 8 | 53 | likely open |
| L0 entity | effects_world | `0x1010c930` | Global::FUN_1010c930 | 185 | 53 | likely open |
| L0 entity | effects_world | `0x1010ca20` | CFuncAreaPortalBase::vfunc241 | 172 | 53 | likely open |
| L0 entity | effects_world | `0x1010c420` | CAreaPortal::Spawn | 8 | 44 | open |
| L0 entity | effects_world | `0x1010c520` | CAreaPortal::Use | 41 | 44 | open |
| L0 entity | effects_world | `0x1010c310` | CAreaPortal::vfunc82 | 6 | 44 | likely open |
| L0 entity | effects_world | `0x1010c3f0` | CAreaPortal::vfunc5 | 30 | 44 | likely open |
| L0 entity | effects_world | `0x1010c4f0` | CAreaPortal::vfunc241 | 32 | 44 | likely open |
| L0 entity | effects_world | `0x1010c560` | CAreaPortal::vfunc110 | 76 | 44 | likely open |
| L0 entity | effects_world | `0x1010c610` | CAreaPortal::vfunc86 | 5 | 44 | likely open |
| L0 entity | effects_world | `0x1010d0d0` | CFuncAreaPortalWindow::Spawn | 36 | 36 | open |
| L0 entity | effects_world | `0x1010cd30` | CFuncAreaPortalWindow::vfunc80 | 6 | 36 | likely open |
| L0 entity | effects_world | `0x1010cd50` | CFuncAreaPortalWindow::vfunc81 | 3 | 36 | likely open |
| L0 entity | effects_world | `0x1010cfc0` | CFuncAreaPortalWindow::vfunc82 | 6 | 36 | likely open |
| L0 entity | effects_world | `0x1010d080` | CFuncAreaPortalWindow::vfunc5 | 30 | 36 | likely open |
| L0 entity | effects_world | `0x1010d110` | CFuncAreaPortalWindow::vfunc113 | 206 | 36 | likely open |
| L0 entity | effects_world | `0x1010d220` | CFuncAreaPortalWindow::vfunc241 | 290 | 36 | likely open |
| L0 entity | effects_world | `0x1010d0b0` | Global::FUN_1010d0b0 | 11 | 36 | likely open |
| L0 entity | effects_world | `0x1013c7e0` | Global::FUN_1013c7e0 | 171 | 36 | likely open |
| L0 entity | effects_world | `0x10114ac0` | CFunc_LOD::Spawn | 180 | 32 | open |
| L0 entity | effects_world | `0x10114720` | CFunc_LOD::vfunc80 | 6 | 32 | likely open |
| L0 entity | effects_world | `0x10114740` | CFunc_LOD::vfunc81 | 3 | 32 | likely open |
| L0 entity | effects_world | `0x10114990` | CFunc_LOD::vfunc82 | 6 | 32 | likely open |
| L0 entity | effects_world | `0x10114a50` | CFunc_LOD::vfunc117 | 8 | 32 | likely open |
| L0 entity | effects_world | `0x10114a70` | CFunc_LOD::vfunc5 | 30 | 32 | likely open |
| L0 entity | effects_world | `0x10114bf0` | CFunc_LOD::vfunc110 | 205 | 32 | likely open |
| L0 entity | effects_world | `0x10114aa0` | Global::FUN_10114aa0 | 11 | 32 | likely open |
| L0 entity | effects_world | `0x10114bb0` | CFunc_LOD::vfunc223 | 8 | 32 | likely open |
| L0 entity | effects_world | `0x10102cf0` | CEnvShake::Spawn | 164 | 25 | open |
| L0 entity | effects_world | `0x101030e0` | CEnvShake::InputStartShake | 13 | 25 | open |
| L0 entity | effects_world | `0x10103100` | CEnvShake::InputStopShake | 13 | 25 | open |
| L0 entity | effects_world | `0x10103120` | CEnvShake::InputAmplitude | 57 | 25 | open |
| L0 entity | effects_world | `0x10103170` | CEnvShake::InputFrequency | 57 | 25 | open |
| L0 entity | effects_world | `0x101031c0` | CEnvShake::Remove | 326 | 25 | open |
| L0 entity | effects_world | `0x101043a0` | CParamsExplosion::Spawn | 19 | 25 | open |
| L0 entity | effects_world | `0x10102bf0` | CEnvShake::vfunc82 | 6 | 25 | likely open |
| L0 entity | effects_world | `0x10102cc0` | CEnvShake::vfunc5 | 30 | 25 | likely open |
| L0 entity | effects_world | `0x10104290` | CParamsExplosion::vfunc82 | 6 | 25 | likely open |
| L0 entity | effects_world | `0x10104370` | CParamsExplosion::vfunc5 | 30 | 25 | likely open |
| L0 entity | effects_world | `0x10130af0` | CEnvLight::vfunc110 | 51 | 25 | likely open |
| L0 entity | effects_world | `0x10130b60` | CEnvLight::vfunc5 | 30 | 25 | likely open |
| L0 entity | effects_world | `0x10102c80` | Global::FUN_10102c80 | 42 | 25 | likely open |
| L0 entity | effects_world | `0x10102dd0` | Global::FUN_10102dd0 | 599 | 25 | likely open |
| L0 entity | effects_world | `0x100fca60` | CFuncParticle::Spawn | 182 | 22 | open |
| L0 entity | effects_world | `0x100fc6a0` | CFuncParticle::FUN_100fc6a0 | 6 | 22 | likely open |
| L0 entity | effects_world | `0x100fc6c0` | CFuncParticle::FUN_100fc6c0 | 3 | 22 | likely open |
| L0 entity | effects_world | `0x100fcb50` | CFuncParticle::FUN_100fcb50 | 296 | 22 | likely open |
| L0 entity | effects_world | `0x100fc9d0` | CFuncParticle::vfunc82 | 6 | 11 | likely open |
| L0 entity | effects_world | `0x100fd500` | CFuncParticle::vfunc5 | 30 | 11 | likely open |
| L0 entity | entity_core | `0x1020dd70` | Global::CPropSwitchSwitchThink | 312 | 50 | open |
| L0 entity | entity_core | `0x100a7bd0` | CBaseEntity::PassesuseFilter | 239 | 49 | open |
| L0 entity | entity_core | `0x10216f40` | CPointTarget::vfunc5 | 30 | 40 | likely open |
| L0 entity | entity_core | `0x1018d9b0` | CPointTeleport::vfunc82 | 6 | 35 | likely open |
| L0 entity | entity_core | `0x1018da40` | CPointTeleport::vfunc113 | 340 | 35 | likely open |
| L0 entity | entity_core | `0x1018ded0` | CPointTeleport::vfunc5 | 30 | 35 | likely open |
| L0 entity | entity_core | `0x100b1380` | CBaseEntity::GetSentLastFrame | 121 | 25 | open |
| L0 entity | entity_core | `0x100a0b20` | CBaseEntity::ClearParent | 123 | 21 | open |
| L0 entity | entity_core | `0x1019bfd0` | Global::FUN_1019bfd0 | 59 | 15 | likely open |
| L0 entity | entity_core | `0x10112b90` | Global::FUN_10112b90 | 348 | 12 | likely open |
| L0 entity | entity_io | `0x10107c20` | CFilterName::vfunc241 | 150 | 21 | likely open |
| L0 entity | entity_io | `0x10107cf0` | CFilterName::vfunc82 | 6 | 21 | likely open |
| L0 entity | entity_io | `0x101085d0` | CFilterName::vfunc5 | 54 | 21 | likely open |
| L0 entity | entity_io | `0x10133640` | CMathCounter::Spawn | 204 | 18 | open |
| L0 entity | entity_io | `0x101333f0` | CMathCounter::vfunc82 | 6 | 18 | likely open |
| L0 entity | entity_io | `0x101335c0` | CMathCounter::vfunc110 | 94 | 18 | likely open |
| L0 entity | entity_io | `0x10135f60` | CMathCounter::vfunc5 | 65 | 18 | likely open |
| L0 entity | entity_io | `0x101342f0` | CLogicCase::Spawn | 1 | 15 | open |
| L0 entity | entity_io | `0x10133b60` | CLogicCase::vfunc82 | 6 | 15 | likely open |
| L0 entity | entity_io | `0x10135fd0` | CLogicCase::vfunc5 | 71 | 15 | likely open |
| L0 entity | movers_doors | `0x100bf900` | CFuncRotating::ShouldSavePhysics | 3 | 51 | open |
| L0 entity | movers_doors | `0x100c0c00` | CFuncRotating::InputStartForward | 22 | 51 | open |
| L0 entity | movers_doors | `0x100c0c30` | CFuncRotating::InputStartBackward | 22 | 51 | open |
| L0 entity | movers_doors | `0x100c0cd0` | CFuncRotating::Blocked | 45 | 51 | open |
| L0 entity | movers_doors | `0x100bf8e0` | CFuncRotating::vfunc117 | 8 | 51 | likely open |
| L0 entity | movers_doors | `0x100bf920` | CFuncRotating::vfunc82 | 6 | 51 | likely open |
| L0 entity | movers_doors | `0x100bf9b0` | CFuncRotating::vfunc110 | 189 | 51 | likely open |
| L0 entity | movers_doors | `0x100c0e20` | CFuncRotating::vfunc5 | 30 | 51 | likely open |
| L0 entity | movers_doors | `0x100c0970` | Global::FUN_100c0970 | 265 | 51 | likely open |
| L0 entity | movers_doors | `0x1020d850` | CPropSwitch::Spawn | 287 | 50 | open |
| L0 entity | movers_doors | `0x1020db30` | CPropSwitch::Use | 324 | 50 | open |
| L0 entity | movers_doors | `0x1020d150` | CPropSwitch::vfunc82 | 6 | 50 | likely open |
| L0 entity | movers_doors | `0x1020d680` | CPropSwitch::vfunc5 | 76 | 50 | likely open |
| L0 entity | movers_doors | `0x1020d9c0` | CPropSwitch::vfunc113 | 245 | 50 | likely open |
| L0 entity | movers_doors | `0x1020e100` | CPropSwitch::vfunc130 | 56 | 50 | likely open |
| L0 entity | movers_doors | `0x1020e150` | CPropSwitch::vfunc117 | 6 | 50 | likely open |
| L0 entity | movers_doors | `0x1020dce0` | Global::FUN_1020dce0 | 107 | 50 | likely open |
| L0 entity | movers_doors | `0x1020df00` | Global::FUN_1020df00 | 133 | 50 | likely open |
| L0 entity | movers_doors | `0x1020e020` | Global::FUN_1020e020 | 7 | 50 | likely open |
| L0 entity | movers_doors | `0x1020e040` | Global::FUN_1020e040 | 7 | 50 | likely open |
| L0 entity | movers_doors | `0x100c8740` | CBaseButton::ShouldSavePhysics | 3 | 41 | open |
| L0 entity | movers_doors | `0x100c8430` | CBaseButton::FUN_100c8430 | 6 | 41 | likely open |
| L0 entity | movers_doors | `0x100c8760` | CBaseButton::vfunc5 | 98 | 41 | likely open |
| L0 entity | movers_doors | `0x100c9670` | CBaseButton::FUN_100c9670 | 26 | 41 | likely open |
| L0 entity | movers_doors | `0x100ed720` | CBaseDoor::vfunc82 | 6 | 40 | likely open |
| L0 entity | movers_doors | `0x100ee390` | CBaseDoor::vfunc241 | 3 | 40 | likely open |
| L0 entity | movers_doors | `0x100ee400` | CBaseDoor::vfunc249 | 1 | 40 | likely open |
| L0 entity | movers_doors | `0x100ee420` | CBaseDoor::vfunc5 | 142 | 40 | likely open |
| L0 entity | movers_doors | `0x100efa70` | CBaseDoor::vfunc156 | 48 | 40 | likely open |
| L0 entity | movers_doors | `0x100f0f10` | CBaseDoor::vfunc247 | 7 | 40 | likely open |
| L0 entity | movers_doors | `0x10218ec0` | Global::FUN_10218ec0 | 237 | 35 | likely open |
| L0 entity | movers_doors | `0x10218ff0` | Global::FUN_10218ff0 | 237 | 35 | likely open |
| L0 entity | movers_doors | `0x10248af0` | Global::FUN_10248af0 | 72 | 35 | likely open |
| L0 entity | movers_doors | `0x10219710` | CPropHacking::vfunc82 | 6 | 34 | likely open |
| L0 entity | movers_doors | `0x10219ec0` | CPropHacking::vfunc117 | 8 | 34 | likely open |
| L0 entity | movers_doors | `0x10219ee0` | CPropHacking::vfunc5 | 30 | 34 | likely open |
| L0 entity | movers_doors | `0x1021a590` | CPropHacking::vfunc33 | 6 | 34 | likely open |
| L0 entity | movers_doors | `0x1021c6a0` | CPropHacking::vfunc277 | 20 | 34 | likely open |
| L0 entity | movers_doors | `0x1021cb60` | CPropHacking::vfunc274 | 39 | 34 | likely open |
| L0 entity | movers_doors | `0x10219fa0` | Global::FUN_10219fa0 | 392 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e100` | Global::FUN_1021e100 | 8 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e120` | Global::FUN_1021e120 | 40 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e160` | Global::FUN_1021e160 | 8 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e180` | Global::FUN_1021e180 | 40 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e1c0` | Global::FUN_1021e1c0 | 8 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e230` | Global::FUN_1021e230 | 40 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e410` | Global::FUN_1021e410 | 8 | 34 | likely open |
| L0 entity | movers_doors | `0x1021e540` | Global::FUN_1021e540 | 40 | 34 | likely open |
| L0 entity | movers_doors | `0x101c1cb0` | Global::FUN_101c1cb0 | 201 | 31 | likely open |
| L0 entity | movers_doors | `0x10115be0` | CFuncMoveLinear::ShouldSavePhysics | 3 | 22 | open |
| L0 entity | movers_doors | `0x10116030` | CFuncMoveLinear::Spawn | 764 | 22 | open |
| L0 entity | movers_doors | `0x10116d20` | CFuncMoveLinear::Use | 263 | 22 | open |
| L0 entity | movers_doors | `0x10116550` | CFuncElevator::FUN_10116550 | 8 | 22 | likely open |
| L0 entity | movers_doors | `0x101163f0` | CFuncElevator::FUN_101163f0 | 146 | 22 | likely open |
| L0 entity | movers_doors | `0x10116570` | Global::FUN_10116570 | 804 | 22 | likely open |
| L0 entity | movers_doors | `0x102262f0` | CPropDoorknobElectronic::Spawn | 50 | 21 | open |
| L0 entity | movers_doors | `0x10226240` | CPropDoorknobElectronic::vfunc117 | 8 | 21 | likely open |
| L0 entity | movers_doors | `0x10226260` | CPropDoorknobElectronic::vfunc5 | 109 | 21 | likely open |
| L0 entity | movers_doors | `0x10226340` | CPropDoorknobElectronic::vfunc274 | 11 | 21 | likely open |
| L0 entity | movers_doors | `0x10226360` | CPropDoorknobElectronic::vfunc275 | 16 | 21 | likely open |
| L0 entity | movers_doors | `0x10226380` | CPropDoorknobElectronic::vfunc276 | 20 | 21 | likely open |
| L0 entity | movers_doors | `0x10215750` | CPropButton::Spawn | 302 | 18 | open |
| L0 entity | movers_doors | `0x102159f0` | CPropButton::InputUse | 46 | 18 | open |
| L0 entity | movers_doors | `0x10214dd0` | CPropButton::vfunc82 | 6 | 18 | likely open |
| L0 entity | movers_doors | `0x10215570` | CPropButton::vfunc5 | 82 | 18 | likely open |
| L0 entity | movers_doors | `0x102158d0` | CPropButton::vfunc117 | 8 | 18 | likely open |
| L0 entity | movers_doors | `0x10215b80` | CPropButton::vfunc35 | 59 | 18 | likely open |
| L0 entity | movers_doors | `0x10215a30` | CPropButton::vfunc265 | 127 | 18 | likely open |
| L0 entity | movers_doors | `0x10215ad0` | Global::FUN_10215ad0 | 72 | 18 | likely open |
| L0 entity | movers_doors | `0x10116ff0` | CFuncMoveLinear::InputToggleMovement | 327 | 12 | open |
| L0 entity | movers_doors | `0x101172c0` | CFuncMoveLinear::InputSetPosition | 49 | 12 | open |
| L0 entity | movers_doors | `0x10117310` | CFuncMoveLinear::InputStartMoving | 262 | 12 | open |
| L0 entity | movers_doors | `0x10117470` | CFuncMoveLinear::InputStopMoving | 27 | 12 | open |
| L0 entity | movers_doors | `0x101174a0` | CFuncMoveLinear::Blocked | 64 | 12 | open |
| L0 entity | movers_doors | `0x10115cc0` | CFuncMoveLinear::vfunc82 | 6 | 12 | likely open |
| L0 entity | movers_doors | `0x10116bf0` | CFuncMoveLinear::vfunc133 | 237 | 12 | likely open |
| L0 entity | movers_doors | `0x10117740` | CFuncMoveLinear::vfunc5 | 120 | 12 | likely open |
| L0 entity | movers_doors | `0x10116e80` | Global::FUN_10116e80 | 228 | 12 | likely open |
| L0 entity | movers_doors | `0x1020f750` | CFuncElevator::Blocked | 3 | 11 | open |
| L0 entity | movers_doors | `0x1020e870` | CFuncElevator::vfunc82 | 6 | 11 | likely open |
| L0 entity | movers_doors | `0x1020f270` | CFuncElevator::vfunc130 | 58 | 11 | likely open |
| L0 entity | movers_doors | `0x1020f6c0` | CFuncElevator::vfunc133 | 36 | 11 | likely open |
| L0 entity | movers_doors | `0x10216e30` | CFuncElevator::vfunc5 | 205 | 11 | likely open |
| L0 entity | movers_doors | `0x1020f700` | Global::FUN_1020f700 | 53 | 11 | likely open |
| L0 entity | physics | `0x100c01d0` | Global::FUN_100c01d0 | 453 | 51 | likely open |
| L0 entity | physics | `0x100c0420` | Global::FUN_100c0420 | 496 | 51 | likely open |
| L0 entity | physics | `0x1010d6a0` | CBreakable::Spawn | 379 | 32 | open |
| L0 entity | physics | `0x1010d430` | CBreakable::FUN_1010d430 | 8 | 32 | likely open |
| L0 entity | physics | `0x1010d590` | CBreakable::FUN_1010d590 | 208 | 32 | likely open |
| L0 entity | physics | `0x1010efe0` | CBreakable::FUN_1010efe0 | 49 | 32 | likely open |
| L0 entity | physics | `0x1010d880` | CBreakable::FUN_1010d880 | 8 | 32 | likely open |
| L0 entity | physics | `0x1010e620` | Global::FUN_1010e620 | 239 | 32 | likely open |
| L0 entity | physics | `0x1010e750` | CBreakable::FUN_1010e750 | 1680 | 32 | likely open |
| L0 entity | physics | `0x1010efc0` | Global::FUN_1010efc0 | 15 | 32 | likely open |
| L0 entity | physics | `0x1010e5c0` | CBreakable::VPhysicsCollision | 38 | 31 | open |
| L0 entity | physics | `0x1010e600` | CBreakable::ReceivesImpactDamage | 8 | 31 | open |
| L0 entity | physics | `0x1010e100` | CBreakable::OnTakeDamage | 842 | 26 | open |
| L0 entity | physics | `0x1010dea0` | CBreakable::FUN_1010dea0 | 472 | 26 | likely open |
| L0 entity | physics | `0x1010db90` | CBreakable::InputAddHealth | 116 | 25 | open |
| L0 entity | physics | `0x1010dc50` | CBreakable::InputRemoveHealth | 116 | 25 | open |
| L0 entity | physics | `0x1010dcf0` | CBreakable::InputSetHealth | 106 | 25 | open |
| L0 entity | physics | `0x1010dd80` | CBreakable::InputSetDamageable | 43 | 25 | open |
| L0 entity | physics | `0x1002b420` | CPhysConstraint::FUN_1002b420 | 30 | 25 | likely open |
| L0 entity | physics | `0x1002b680` | CPhysConstraint::FUN_1002b680 | 93 | 25 | likely open |
| L0 entity | physics | `0x1010d450` | CBreakable::vfunc82 | 6 | 25 | likely open |
| L0 entity | physics | `0x1010f240` | CBreakable::vfunc5 | 54 | 25 | likely open |
| L0 entity | physics | `0x1002a210` | Global::FUN_1002a210 | 339 | 25 | likely open |
| L0 entity | physics | `0x1002b130` | Global::FUN_1002b130 | 46 | 25 | likely open |
| L0 entity | physics | `0x1002b1e0` | Global::FUN_1002b1e0 | 301 | 25 | likely open |
| L0 entity | physics | `0x1002b360` | Global::FUN_1002b360 | 132 | 25 | likely open |
| L0 entity | physics | `0x1002b450` | Global::FUN_1002b450 | 71 | 25 | likely open |
| L0 entity | physics | `0x1002b4c0` | Global::FUN_1002b4c0 | 338 | 25 | likely open |
| L0 entity | physics | `0x1004d650` | Global::FUN_1004d650 | 63 | 25 | likely open |
| L0 entity | physics | `0x1010ddc0` | Global::FUN_1010ddc0 | 167 | 25 | likely open |
| L0 entity | physics | `0x1002b1b0` | CPhysConstraint::Spawn | 19 | 21 | open |
| L0 entity | physics | `0x1002a3d0` | CPhysConstraint::FUN_1002a3d0 | 6 | 21 | likely open |
| L0 entity | physics | `0x1004e260` | CPhysBox::Spawn | 293 | 17 | open |
| L0 entity | physics | `0x1004e8a0` | CPhysBox::CausesImpactDamage | 21 | 17 | open |
| L0 entity | physics | `0x1004e8d0` | CPhysBox::VPhysicsCollision | 38 | 17 | open |
| L0 entity | physics | `0x1004e970` | CPhysBox::OnTakeDamage | 49 | 17 | open |
| L0 entity | physics | `0x10190f20` | COrnamentProp::Spawn | 81 | 17 | open |
| L0 entity | physics | `0x101910c0` | COrnamentProp::InputSetAttached | 67 | 17 | open |
| L0 entity | physics | `0x10191200` | COrnamentProp::InputDetach | 8 | 17 | open |
| L0 entity | physics | `0x1004dbf0` | CPhysBox::vfunc80 | 6 | 17 | likely open |
| L0 entity | physics | `0x1004dc10` | CPhysBox::vfunc81 | 3 | 17 | likely open |
| L0 entity | physics | `0x1004de80` | CPhysBox::vfunc82 | 6 | 17 | likely open |
| L0 entity | physics | `0x1004e110` | CPhysBox::vfunc117 | 10 | 17 | likely open |
| L0 entity | physics | `0x1004e130` | CPhysBox::vfunc5 | 76 | 17 | likely open |
| L0 entity | physics | `0x1004e1c0` | CPhysBox::vfunc32 | 31 | 17 | likely open |
| L0 entity | physics | `0x1004e1f0` | CPhysBox::vfunc39 | 29 | 17 | likely open |
| L0 entity | physics | `0x1004e220` | CPhysBox::vfunc41 | 14 | 17 | likely open |
| L0 entity | physics | `0x1004e240` | CPhysBox::vfunc42 | 14 | 17 | likely open |
| L0 entity | physics | `0x1004e500` | CPhysBox::vfunc241 | 496 | 17 | likely open |
| L0 entity | physics | `0x1004e7c0` | CPhysBox::vfunc226 | 165 | 17 | likely open |
| L0 entity | physics | `0x1004e910` | CPhysBox::vfunc242 | 22 | 17 | likely open |
| L0 entity | physics | `0x1004e940` | CPhysBox::vfunc243 | 22 | 17 | likely open |
| L0 entity | physics | `0x10190b80` | COrnamentProp::vfunc82 | 6 | 17 | likely open |
| L0 entity | physics | `0x10190c80` | COrnamentProp::vfunc80 | 6 | 17 | likely open |
| L0 entity | physics | `0x10190ca0` | COrnamentProp::vfunc81 | 3 | 17 | likely open |
| L0 entity | physics | `0x10191090` | COrnamentProp::vfunc113 | 27 | 17 | likely open |
| L0 entity | physics | `0x10194490` | COrnamentProp::vfunc5 | 87 | 17 | likely open |
| L0 entity | physics | `0x1004e430` | CPhysBox::vfunc223 | 151 | 17 | likely open |
| L0 entity | physics | `0x10190fa0` | Global::FUN_10190fa0 | 187 | 17 | likely open |
| L0 entity | physics | `0x10191130` | Global::FUN_10191130 | 39 | 17 | likely open |
| L0 entity | physics | `0x1004eae0` | CPhysExplosion::Spawn | 145 | 15 | open |
| L0 entity | physics | `0x1004ec40` | CPhysExplosion::InputExplode | 20 | 15 | open |
| L0 entity | physics | `0x1004ef60` | CPhysImpact::Spawn | 145 | 15 | open |
| L0 entity | physics | `0x1004f030` | CPhysImpact::InputImpact | 1512 | 15 | open |
| L0 entity | physics | `0x100502b0` | CPhysConvert::InputConvertTarget | 513 | 15 | open |
| L0 entity | physics | `0x1004ea30` | CPhysExplosion::vfunc117 | 8 | 15 | likely open |
| L0 entity | physics | `0x1004ea50` | CPhysExplosion::vfunc82 | 6 | 15 | likely open |
| L0 entity | physics | `0x1004ee90` | CPhysImpact::vfunc117 | 8 | 15 | likely open |
| L0 entity | physics | `0x1004eeb0` | CPhysImpact::vfunc82 | 6 | 15 | likely open |
| L0 entity | physics | `0x10050110` | CPhysConvert::vfunc82 | 6 | 15 | likely open |
| L0 entity | physics | `0x10050240` | CPhysConvert::vfunc5 | 43 | 15 | likely open |
| L0 entity | physics | `0x10050550` | CPhysExplosion::vfunc5 | 30 | 15 | likely open |
| L0 entity | physics | `0x10050580` | CPhysImpact::vfunc5 | 30 | 15 | likely open |
| L0 entity | physics | `0x1004ebb0` | Global::FUN_1004ebb0 | 107 | 15 | likely open |
| L0 entity | physics | `0x1004ec60` | Global::FUN_1004ec60 | 337 | 15 | likely open |
| L0 entity | physics | `0x1004feb0` | Global::FUN_1004feb0 | 112 | 15 | likely open |
| L0 entity | physics | `0x1004ff40` | Global::FUN_1004ff40 | 269 | 15 | likely open |
| L0 entity | physics | `0x100c1e20` | Global::FUN_100c1e20 | 87 | 15 | likely open |
| L0 entity | physics | `0x100c2f30` | Global::FUN_100c2f30 | 259 | 15 | likely open |
| L0 entity | physics | `0x100dd990` | Global::FUN_100dd990 | 101 | 15 | likely open |
| L0 entity | physics | `0x1012c600` | Global::FUN_1012c600 | 132 | 15 | likely open |
| L0 entity | physics | `0x1013d7a0` | Global::FUN_1013d7a0 | 109 | 15 | likely open |
| L0 entity | physics | `0x10155010` | Global::FUN_10155010 | 38 | 15 | likely open |
| L0 entity | physics | `0x101554a0` | Global::FUN_101554a0 | 773 | 15 | likely open |
| L0 entity | physics | `0x101563b0` | Global::FUN_101563b0 | 136 | 15 | likely open |
| L0 entity | physics | `0x1019c0b0` | Global::FUN_1019c0b0 | 500 | 15 | likely open |
| L0 entity | physics | `0x1019c330` | Global::FUN_1019c330 | 756 | 15 | likely open |
| L0 entity | physics | `0x1019c6f0` | Global::FUN_1019c6f0 | 223 | 15 | likely open |
| L0 entity | physics | `0x10154e40` | CRagdollProp::Spawn | 232 | 14 | open |
| L0 entity | physics | `0x10155050` | CRagdollProp::UpdateOnRemove | 87 | 14 | open |
| L0 entity | physics | `0x10155360` | CRagdollProp::ShouldSavePhysics | 3 | 14 | open |
| L0 entity | physics | `0x10155920` | CRagdollProp::OnTakeDamage | 194 | 14 | open |
| L0 entity | physics | `0x10155ae0` | CRagdollProp::SetupBones | 241 | 14 | open |
| L0 entity | physics | `0x10155c20` | CRagdollProp::TestCollision | 417 | 14 | open |
| L0 entity | physics | `0x10156360` | CRagdollProp::VPhysicsGetObjectList | 64 | 14 | open |
| L0 entity | physics | `0x1002bb90` | CPhysBallSocket::vfunc241 | 235 | 14 | likely open |
| L0 entity | physics | `0x1002cb10` | CPhysBallSocket::vfunc5 | 30 | 14 | likely open |
| L0 entity | physics | `0x10152300` | CRagdollProp::FUN_10152300 | 6 | 14 | likely open |
| L0 entity | physics | `0x10152320` | CRagdollProp::FUN_10152320 | 3 | 14 | likely open |
| L0 entity | physics | `0x101525e0` | CRagdollProp::vfunc82 | 6 | 14 | likely open |
| L0 entity | physics | `0x10154f70` | CRagdollProp::FUN_10154f70 | 32 | 14 | likely open |
| L0 entity | physics | `0x10154fa0` | CRagdollProp::FUN_10154fa0 | 74 | 14 | likely open |
| L0 entity | physics | `0x10155150` | CRagdollProp::FUN_10155150 | 3 | 14 | likely open |
| L0 entity | physics | `0x10155170` | CRagdollProp::FUN_10155170 | 3 | 14 | likely open |
| L0 entity | physics | `0x10155190` | CRagdollProp::FUN_10155190 | 5 | 14 | likely open |
| L0 entity | physics | `0x101551b0` | CRagdollProp::FUN_101551b0 | 5 | 14 | likely open |
| L0 entity | physics | `0x101551d0` | CRagdollProp::FUN_101551d0 | 6 | 14 | likely open |
| L0 entity | physics | `0x101551f0` | CRagdollProp::FUN_101551f0 | 5 | 14 | likely open |
| L0 entity | physics | `0x10155210` | CRagdollProp::FUN_10155210 | 122 | 14 | likely open |
| L0 entity | physics | `0x101552b0` | CRagdollProp::FUN_101552b0 | 3 | 14 | likely open |
| L0 entity | physics | `0x101552d0` | CRagdollProp::vfunc5 | 30 | 14 | likely open |
| L0 entity | physics | `0x101553f0` | CRagdollProp::FUN_101553f0 | 59 | 14 | likely open |
| L0 entity | physics | `0x10155440` | CRagdollProp::FUN_10155440 | 3 | 14 | likely open |
| L0 entity | physics | `0x101558b0` | CRagdollProp::FUN_101558b0 | 69 | 14 | likely open |
| L0 entity | physics | `0x10155a70` | CRagdollProp::FUN_10155a70 | 71 | 14 | likely open |
| L0 entity | physics | `0x10155fb0` | CRagdollProp::FUN_10155fb0 | 744 | 14 | likely open |
| L0 entity | physics | `0x10157390` | CRagdollProp::FUN_10157390 | 218 | 14 | likely open |
| L0 entity | physics | `0x101574b0` | CRagdollProp::FUN_101574b0 | 93 | 14 | likely open |
| L0 entity | physics | `0x10158080` | CRagdollProp::FUN_10158080 | 46 | 14 | likely open |
| L0 entity | physics | `0x101580c0` | CRagdollProp::FUN_101580c0 | 14 | 14 | likely open |
| L0 entity | physics | `0x101580e0` | CRagdollProp::FUN_101580e0 | 14 | 14 | likely open |
| L0 entity | physics | `0x10158100` | CRagdollProp::FUN_10158100 | 14 | 14 | likely open |
| L0 entity | physics | `0x10155300` | Global::FUN_10155300 | 11 | 14 | likely open |
| L0 entity | physics | `0x1019bf80` | Global::FUN_1019bf80 | 57 | 14 | likely open |
| L0 entity | physics | `0x1019c950` | Global::FUN_1019c950 | 125 | 14 | likely open |
| L0 entity | physics | `0x1019cae0` | Global::FUN_1019cae0 | 135 | 14 | likely open |
| L0 entity | physics | `0x1019cd10` | Global::FUN_1019cd10 | 51 | 14 | likely open |
| L0 entity | physics | `0x10193a50` | CPropDestructable::Spawn | 247 | 13 | open |
| L0 entity | physics | `0x10193c30` | CPropDestructable::InputSetToDamageLevel | 109 | 13 | open |
| L0 entity | physics | `0x10193ea0` | CPropDestructable::InputRemoveHealth | 198 | 13 | open |
| L0 entity | physics | `0x10193fb0` | CPropDestructable::OnTakeDamage | 159 | 13 | open |
| L0 entity | physics | `0x10194080` | CPropDestructable::Event_Killed | 168 | 13 | open |
| L0 entity | physics | `0x10194160` | CPropDestructable::InputPlayAnimation | 76 | 13 | open |
| L0 entity | physics | `0x10193480` | CPropDestructable::vfunc82 | 6 | 13 | likely open |
| L0 entity | physics | `0x101939b0` | CPropDestructable::vfunc5 | 93 | 13 | likely open |
| L0 entity | physics | `0x10193a30` | CPropDestructable::vfunc265 | 1 | 13 | likely open |
| L0 entity | physics | `0x10193be0` | CPropDestructable::vfunc113 | 54 | 13 | likely open |
| L0 entity | physics | `0x10193cc0` | Global::FUN_10193cc0 | 372 | 13 | likely open |
| L0 entity | physics | `0x10110b20` | CBreakableSurface::OnTakeDamage | 509 | 12 | open |
| L0 entity | physics | `0x101134f0` | CBreakableSurface::Spawn | 114 | 12 | open |
| L0 entity | physics | `0x1010fa80` | CBreakableSurface::vfunc82 | 6 | 12 | likely open |
| L0 entity | physics | `0x1010fb80` | CBreakableSurface::vfunc80 | 6 | 12 | likely open |
| L0 entity | physics | `0x1010fba0` | CBreakableSurface::vfunc81 | 3 | 12 | likely open |
| L0 entity | physics | `0x1010fef0` | CBreakableSurface::vfunc130 | 58 | 12 | likely open |
| L0 entity | physics | `0x10110da0` | CBreakableSurface::vfunc141 | 2857 | 12 | likely open |
| L0 entity | physics | `0x101135c0` | CBreakableSurface::vfunc5 | 54 | 12 | likely open |
| L0 entity | physics | `0x10111ba0` | Global::FUN_10111ba0 | 1605 | 12 | likely open |
| L0 entity | physics | `0x101123b0` | Global::FUN_101123b0 | 79 | 12 | likely open |
| L0 entity | physics | `0x101128d0` | Global::FUN_101128d0 | 247 | 12 | likely open |
| L0 entity | physics | `0x10112a10` | Global::FUN_10112a10 | 292 | 12 | likely open |
| L0 entity | physics | `0x10113050` | Global::FUN_10113050 | 459 | 12 | likely open |
| L0 entity | physics | `0x101132a0` | Global::FUN_101132a0 | 462 | 12 | likely open |
| L0 entity | triggers | `0x101c5e80` | CTriggerHurt::Spawn | 137 | 40 | open |
| L0 entity | triggers | `0x101c6410` | CTriggerHurt::EndTouch | 251 | 40 | open |
| L0 entity | triggers | `0x101c6630` | CTriggerHurt::Touch | 34 | 40 | open |
| L0 entity | triggers | `0x101c5950` | CTriggerHurt::vfunc82 | 6 | 40 | likely open |
| L0 entity | triggers | `0x101c5d40` | CTriggerHurt::vfunc5 | 160 | 40 | likely open |
| L0 entity | triggers | `0x101cbba0` | CTriggerEnvAudio::Spawn | 16 | 21 | open |
| L0 entity | triggers | `0x101cbbc0` | CTriggerEnvAudio::Touch | 50 | 21 | open |
| L0 entity | triggers | `0x101cbc10` | CTriggerEnvAudio::EndTouch | 57 | 21 | open |
| L0 entity | triggers | `0x101cba80` | CTriggerEnvAudio::vfunc82 | 6 | 21 | likely open |
| L0 entity | triggers | `0x101cc440` | CTriggerEnvAudio::vfunc5 | 76 | 21 | likely open |
| L0 entity | triggers | `0x101753a0` | Global::FUN_101753a0 | 37 | 21 | likely open |
| L0 entity | triggers | `0x101753e0` | Global::FUN_101753e0 | 7 | 21 | likely open |
| L0 entity | triggers | `0x101c6c50` | CTriggerLook::Spawn | 25 | 17 | open |
| L0 entity | triggers | `0x101c6c80` | CTriggerLook::EndTouch | 27 | 17 | open |
| L0 entity | triggers | `0x101c6cb0` | CTriggerLook::Touch | 565 | 17 | open |
| L0 entity | triggers | `0x101c6bc0` | CTriggerLook::vfunc82 | 6 | 17 | likely open |
| L0 entity | triggers | `0x101cbf40` | CTriggerLook::vfunc5 | 87 | 17 | likely open |
| L1 animation | animation | `0x10090b20` | CBaseAnimating::RepositionOnAnimationExit | 121 | 30 | open |
| L2 character | combat | `0x10327790` | CBaseCombatCharacter::UpdateOnRemove | 353 | 29 | open |
| L2 character | combat | `0x10348980` | CBaseCombatCharacter::Save | 176 | 29 | open |
| L2 character | combat | `0x1032f930` | CBaseCombatCharacter::IncrementStealthModifier | 134 | 20 | open |
| L2 character | combat | `0x1032f9f0` | CBaseCombatCharacter::DecrementStealthModifier | 134 | 20 | open |
| L2 character | inventory_ui_items | `0x102172e0` | CBaseTerminal::FUN_102172e0 | 6 | 35 | likely open |
| L2 character | inventory_ui_items | `0x10217300` | CBaseTerminal::FUN_10217300 | 3 | 35 | likely open |
| L2 character | inventory_ui_items | `0x10208fc0` | CItemContainer::InputBarterBegin | 31 | 29 | open |
| L2 character | inventory_ui_items | `0x10208ff0` | CItemContainer::InputBarterEnd | 114 | 29 | open |
| L2 character | inventory_ui_items | `0x10209350` | CItemContainer::Spawn | 768 | 29 | open |
| L2 character | inventory_ui_items | `0x10209c50` | CItemContainer::OnTakeDamage | 95 | 29 | open |
| L2 character | inventory_ui_items | `0x10207f20` | CItemContainer::FUN_10207f20 | 6 | 29 | likely open |
| L2 character | inventory_ui_items | `0x102088f0` | CItemContainer::FUN_102088f0 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208910` | CItemContainer::FUN_10208910 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208930` | CItemContainer::FUN_10208930 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208950` | CItemContainer::FUN_10208950 | 3 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208970` | CItemContainer::FUN_10208970 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x102089f0` | CItemContainer::FUN_102089f0 | 3 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208a10` | CItemContainer::FUN_10208a10 | 3 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208a30` | CItemContainer::FUN_10208a30 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208a50` | CItemContainer::FUN_10208a50 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208a90` | CItemContainer::FUN_10208a90 | 3 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208ab0` | CItemContainer::FUN_10208ab0 | 5 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208ba0` | CItemContainer::FUN_10208ba0 | 62 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208bf0` | CItemContainer::FUN_10208bf0 | 35 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10209090` | CItemContainer::FUN_10209090 | 515 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10209330` | CItemContainer::FUN_10209330 | 10 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10208b00` | Global::FUN_10208b00 | 50 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10209a20` | Global::FUN_10209a20 | 124 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10209b20` | Global::FUN_10209b20 | 234 | 29 | likely open |
| L2 character | inventory_ui_items | `0x10209da0` | CItemContainerAnimated::Spawn | 28 | 28 | open |
| L2 character | inventory_ui_items | `0x10209d70` | CItemContainerAnimated::vfunc5 | 30 | 28 | likely open |
| L2 character | inventory_ui_items | `0x10209df0` | CItemContainerAnimated::vfunc406 | 11 | 28 | likely open |
| L2 character | inventory_ui_items | `0x10209e10` | CItemContainerAnimated::vfunc407 | 11 | 28 | likely open |
| L2 character | inventory_ui_items | `0x10209e30` | Global::FUN_10209e30 | 137 | 28 | likely open |
| L2 character | inventory_ui_items | `0x103f56f0` | CBaseMoneyObject::FUN_103f56f0 | 6 | 15 | likely open |
| L2 character | inventory_ui_items | `0x103f57e0` | CBaseMoneyObject::FUN_103f57e0 | 13 | 15 | likely open |
| L2 character | inventory_ui_items | `0x103f5860` | CBaseMoneyObject::FUN_103f5860 | 11 | 15 | likely open |
| L2 character | inventory_ui_items | `0x104086d0` | CItemKeyGeneric::vfunc80 | 6 | 12 | likely open |
| L2 character | inventory_ui_items | `0x104086f0` | CItemKeyGeneric::vfunc81 | 3 | 12 | likely open |
| L2 character | inventory_ui_items | `0x10408920` | CItemKeyGeneric::vfunc341 | 121 | 12 | likely open |
| L2 character | inventory_ui_items | `0x1040ad50` | CItemKeyGeneric::vfunc5 | 30 | 12 | likely open |
| L2 character | inventory_ui_items | `0x100fd100` | CInspectionBrush::Spawn | 19 | 11 | open |
| L2 character | inventory_ui_items | `0x100fd070` | CInspectionBrush::vfunc82 | 6 | 11 | likely open |
| L2 character | inventory_ui_items | `0x100fd180` | CInspectionBrush::vfunc86 | 187 | 11 | likely open |
| L2 character | inventory_ui_items | `0x100fd560` | CInspectionBrush::vfunc5 | 30 | 11 | likely open |
| L2 character | rpg | `0x10055ec0` | Global::FUN_10055ec0 | 103 | 17 | likely open |
| L2 character | weapons | `0x10158ab0` | Global::FUN_10158ab0 | 48 | 38 | likely open |
| L2 character | weapons | `0x10158b40` | Global::FUN_10158b40 | 57 | 30 | likely open |
| L2 character | weapons | `0x103e9bf0` | CWeaponMelee::Kick | 307 | 17 | open |
| L2 character | weapons | `0x103ea510` | CWeaponMelee::Deploy | 117 | 17 | open |
| L2 character | weapons | `0x10254fd0` | CBaseCombatWeapon::FUN_10254fd0 | 6 | 17 | likely open |
| L2 character | weapons | `0x10254ff0` | CBaseCombatWeapon::FUN_10254ff0 | 6 | 17 | likely open |
| L2 character | weapons | `0x103e4410` | CWeaponMelee_Torch::FUN_103e4410 | 3 | 17 | likely open |
| L2 character | weapons | `0x103ea6d0` | CWeaponMelee_Torch::FUN_103ea6d0 | 193 | 17 | likely open |
| L2 character | weapons | `0x10158c00` | Global::FUN_10158c00 | 36 | 15 | likely open |
| L4 NPC | hints_places | `0x102dbd50` | CAI_InterestingPlaceConverstation::Save | 53 | 17 | open |
| L4 NPC | hints_places | `0x102dcce0` | CAI_InterestingPlaceConverstation::InputDisable | 32 | 17 | open |
| L4 NPC | hints_places | `0x102db7f0` | CAI_InterestingPlaceConverstation::vfunc82 | 6 | 17 | likely open |
| L4 NPC | hints_places | `0x102dbbc0` | CAI_InterestingPlaceConverstation::vfunc5 | 132 | 17 | likely open |
| L4 NPC | hints_places | `0x102dbda0` | CAI_InterestingPlaceConverstation::vfunc127 | 40 | 17 | likely open |
| L4 NPC | hints_places | `0x102dce10` | Global::FUN_102dce10 | 8 | 17 | likely open |
| L4 NPC | hints_places | `0x102dce80` | Global::FUN_102dce80 | 40 | 17 | likely open |
| L4 NPC | makers | `0x1034af10` | CNPCMaker::FUN_1034af10 | 5 | 31 | likely open |
| L4 NPC | makers | `0x1034ab50` | CNPCMaker::vfunc82 | 6 | 23 | likely open |
| L4 NPC | makers | `0x1034af70` | CNPCMaker::vfunc5 | 65 | 23 | likely open |
| L4 NPC | npc_kernel | `0x10073b80` | Global::FUN_10073b80 | 80 | 30 | likely open |
| L4 NPC | npc_kernel | `0x100740f0` | Global::FUN_100740f0 | 10 | 30 | likely open |
| L4 NPC | npc_kernel | `0x100746e0` | Global::FUN_100746e0 | 246 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10074820` | Global::FUN_10074820 | 270 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10074a30` | Global::FUN_10074a30 | 10 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10074a70` | Global::FUN_10074a70 | 10 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10074a90` | Global::FUN_10074a90 | 10 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10077490` | Global::FUN_10077490 | 246 | 30 | likely open |
| L4 NPC | npc_kernel | `0x100775d0` | Global::FUN_100775d0 | 88 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10077650` | Global::FUN_10077650 | 34 | 30 | likely open |
| L4 NPC | npc_kernel | `0x10078fd0` | Global::FUN_10078fd0 | 106 | 30 | likely open |
| L4 NPC | npc_kernel | `0x1028cd10` | CAI_BaseNPCTroika::FUN_1028cd10 | 6 | 11 | likely open |
| L4 NPC | npc_species | `0x103a2910` | CNPC_VPedestrian::Classify | 6 | 50 | open |
| L4 NPC | npc_species | `0x103a1c20` | CNPC_VPedestrian::vfunc82 | 6 | 50 | likely open |
| L4 NPC | npc_species | `0x103a1e00` | CNPC_VPedestrian::vfunc580 | 6 | 50 | likely open |
| L4 NPC | npc_species | `0x103a1e40` | CNPC_VPedestrian::vfunc5 | 30 | 50 | likely open |
| L4 NPC | npc_species | `0x103a1fa0` | CNPC_VPedestrian::vfunc546 | 29 | 50 | likely open |
| L4 NPC | npc_species | `0x103c4f20` | CNPC_VVampire::Classify | 6 | 44 | open |
| L4 NPC | npc_species | `0x103c47e0` | CNPC_VVampire::FUN_103c47e0 | 6 | 42 | likely open |
| L4 NPC | npc_species | `0x103c4920` | CNPC_VVampire::vfunc5 | 30 | 41 | likely open |
| L4 NPC | npc_species | `0x103c4a80` | CNPC_VVampire::FUN_103c4a80 | 29 | 41 | likely open |
| L4 NPC | npc_species | `0x10368e60` | CNPC_VCamera::Classify | 3 | 18 | open |
| L4 NPC | npc_species | `0x10368f60` | CNPC_VCamera::FUN_10368f60 | 5 | 18 | open |
| L4 NPC | npc_species | `0x10367f60` | CNPC_VCamera::vfunc82 | 6 | 18 | likely open |
| L4 NPC | npc_species | `0x103681b0` | CNPC_VCamera::FUN_103681b0 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x103681d0` | CNPC_VCamera::FUN_103681d0 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x10368230` | CNPC_VCamera::FUN_10368230 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x10368250` | CNPC_VCamera::FUN_10368250 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x10368270` | CNPC_VCamera::FUN_10368270 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x103682d0` | CNPC_VCamera::FUN_103682d0 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x103682f0` | CNPC_VCamera::FUN_103682f0 | 1 | 18 | likely open |
| L4 NPC | npc_species | `0x10368330` | CNPC_VCamera::FUN_10368330 | 3 | 18 | likely open |
| L4 NPC | npc_species | `0x10368350` | CNPC_VCamera::FUN_10368350 | 3 | 18 | likely open |
| L4 NPC | npc_species | `0x10368370` | CNPC_VCamera::FUN_10368370 | 3 | 18 | likely open |
| L4 NPC | npc_species | `0x10368390` | CNPC_VCamera::FUN_10368390 | 5 | 18 | likely open |
| L4 NPC | npc_species | `0x103683f0` | CNPC_VCamera::vfunc5 | 30 | 18 | likely open |
| L4 NPC | npc_species | `0x10368550` | CNPC_VCamera::FUN_10368550 | 29 | 18 | likely open |
| L4 NPC | npc_species | `0x10368f00` | CNPC_VCamera::FUN_10368f00 | 3 | 18 | likely open |
| L4 NPC | npc_species | `0x10369240` | CNPC_VCamera::FUN_10369240 | 5 | 18 | likely open |
| L4 NPC | npc_species | `0x1035ebf0` | CNPC_VAnimal::FUN_1035ebf0 | 5 | 17 | likely open |
| L4 NPC | npc_species | `0x103b3630` | CNPC_VTaxiDriver::Classify | 6 | 16 | open |
| L4 NPC | npc_species | `0x103b3910` | CNPC_VTaxiDriver::OnTakeDamage | 5 | 16 | open |
| L4 NPC | npc_species | `0x103b3950` | CNPC_VTaxiDriver::Event_Killed | 3 | 16 | open |
| L4 NPC | npc_species | `0x103b2f50` | CNPC_VTaxiDriver::vfunc72 | 5 | 16 | likely open |
| L4 NPC | npc_species | `0x103b2f70` | CNPC_VTaxiDriver::vfunc580 | 6 | 16 | likely open |
| L4 NPC | npc_species | `0x103b2fb0` | CNPC_VTaxiDriver::vfunc82 | 6 | 16 | likely open |
| L4 NPC | npc_species | `0x103b3170` | CNPC_VTaxiDriver::vfunc546 | 29 | 16 | likely open |
| L4 NPC | npc_species | `0x103b3650` | CNPC_VTaxiDriver::vfunc432 | 13 | 16 | likely open |
| L4 NPC | npc_species | `0x103b3930` | CNPC_VTaxiDriver::vfunc390 | 5 | 16 | likely open |
| L4 NPC | npc_species | `0x103b3970` | CNPC_VTaxiDriver::vfunc5 | 30 | 16 | likely open |
| L4 NPC | npc_species | `0x10391010` | CNPC_VMingXiao::FUN_10391010 | 5 | 13 | likely open |
| L4 NPC | npc_species | `0x103ad660` | CNPC_VRat::Classify | 6 | 11 | open |
| L4 NPC | npc_species | `0x103aba70` | CNPC_VScurrying::FUN_103aba70 | 6 | 11 | likely open |
| L4 NPC | npc_species | `0x103abba0` | CNPC_VScurrying::FUN_103abba0 | 6 | 11 | likely open |
| L4 NPC | npc_species | `0x103abd40` | CNPC_VScurrying::FUN_103abd40 | 29 | 11 | likely open |
| L4 NPC | npc_species | `0x103aca60` | CNPC_VScurrying::FUN_103aca60 | 7 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad6a0` | CNPC_VRat::vfunc428 | 31 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad850` | CNPC_VRat::vfunc316 | 3 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad870` | CNPC_VRat::vfunc317 | 5 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad910` | CNPC_VRat::vfunc330 | 3 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad930` | CNPC_VRat::vfunc5 | 30 | 11 | likely open |
| L4 NPC | npc_species | `0x103ad540` | Global::FUN_103ad540 | 32 | 11 | likely open |
| L4 NPC | perception | `0x101cb9e0` | CStealthModifier::Spawn | 16 | 20 | open |
| L4 NPC | perception | `0x101cba00` | CStealthModifier::StartTouch | 45 | 20 | open |
| L4 NPC | perception | `0x101cba40` | CStealthModifier::EndTouch | 45 | 20 | open |
| L4 NPC | perception | `0x101cb8c0` | CStealthModifier::vfunc82 | 6 | 20 | likely open |
| L4 NPC | perception | `0x101cc3d0` | CStealthModifier::vfunc5 | 76 | 20 | likely open |
| L5 script | camera_ui | `0x100cb750` | CCameraTrack::TrackThink | 344 | 43 | open |
| L5 script | camera_ui | `0x1018a5e0` | CSkyCamera::Spawn | 8 | 43 | open |
| L5 script | camera_ui | `0x100cb980` | CCameraTrack::vfunc82 | 6 | 43 | likely open |
| L5 script | camera_ui | `0x100cbef0` | CCameraTrack::vfunc48 | 7 | 43 | likely open |
| L5 script | camera_ui | `0x100cbf10` | CCameraTrack::vfunc49 | 7 | 43 | likely open |
| L5 script | camera_ui | `0x100cbfb0` | CCameraTrack::vfunc5 | 65 | 43 | likely open |
| L5 script | camera_ui | `0x1018a550` | CSkyCamera::vfunc82 | 6 | 43 | likely open |
| L5 script | camera_ui | `0x1018bef0` | CSkyCamera::vfunc5 | 30 | 43 | likely open |
| L5 script | camera_ui | `0x100cb610` | CCameraKeyFrame::vfunc82 | 6 | 41 | likely open |
| L5 script | camera_ui | `0x100cc040` | CCameraKeyFrame::vfunc5 | 54 | 41 | likely open |
| L5 script | camera_ui | `0x10211b40` | CPropSign::Spawn | 210 | 35 | open |
| L5 script | camera_ui | `0x10211910` | CPropSign::vfunc82 | 6 | 35 | likely open |
| L5 script | camera_ui | `0x10211ac0` | CPropSign::vfunc5 | 30 | 35 | likely open |
| L5 script | camera_ui | `0x10211c60` | CPropSign::vfunc32 | 127 | 35 | likely open |
| L5 script | camera_ui | `0x10211d00` | CPropSign::vfunc33 | 6 | 35 | likely open |
| L5 script | camera_ui | `0x10211db0` | CPropSign::vfunc39 | 58 | 35 | likely open |
| L5 script | camera_ui | `0x10211e00` | CPropSign::vfunc41 | 80 | 35 | likely open |
| L5 script | camera_ui | `0x10211e70` | CPropSign::vfunc42 | 114 | 35 | likely open |
| L5 script | camera_ui | `0x10211af0` | Global::FUN_10211af0 | 54 | 35 | likely open |
| L5 script | camera_ui | `0x10211f10` | Global::FUN_10211f10 | 332 | 35 | likely open |
| L5 script | camera_ui | `0x102120c0` | Global::FUN_102120c0 | 247 | 35 | likely open |
| L5 script | camera_ui | `0x10100f70` | CEnvFade::Spawn | 1 | 32 | open |
| L5 script | camera_ui | `0x10100e90` | CEnvFade::vfunc82 | 6 | 32 | likely open |
| L5 script | camera_ui | `0x10101340` | CEnvFade::vfunc5 | 43 | 32 | likely open |
| L5 script | camera_ui | `0x101153a0` | CFuncMonitor::InputSetCamera | 58 | 16 | open |
| L5 script | camera_ui | `0x101154b0` | CFuncMonitor::UpdateOnRemove | 132 | 16 | open |
| L5 script | camera_ui | `0x10114e20` | CFuncMonitor::vfunc80 | 6 | 16 | likely open |
| L5 script | camera_ui | `0x10114e40` | CFuncMonitor::vfunc81 | 3 | 16 | likely open |
| L5 script | camera_ui | `0x10115020` | CFuncMonitor::vfunc82 | 6 | 16 | likely open |
| L5 script | camera_ui | `0x101150b0` | CFuncMonitor::vfunc113 | 42 | 16 | likely open |
| L5 script | camera_ui | `0x101151c0` | CFuncMonitor::vfunc32 | 127 | 16 | likely open |
| L5 script | camera_ui | `0x10115260` | CFuncMonitor::vfunc39 | 63 | 16 | likely open |
| L5 script | camera_ui | `0x101152b0` | CFuncMonitor::vfunc41 | 52 | 16 | likely open |
| L5 script | camera_ui | `0x10115350` | CFuncMonitor::vfunc35 | 57 | 16 | likely open |
| L5 script | camera_ui | `0x10115af0` | CFuncMonitor::vfunc5 | 30 | 16 | likely open |
| L5 script | camera_ui | `0x1018c260` | CPointCamera::vfunc5 | 30 | 16 | likely open |
| L5 script | camera_ui | `0x1018c290` | CPointCamera::vfunc113 | 8 | 16 | likely open |
| L5 script | camera_ui | `0x1018c410` | CPointCamera::vfunc86 | 130 | 16 | likely open |
| L5 script | camera_ui | `0x1018c7a0` | CPointCamera::vfunc82 | 6 | 16 | likely open |
| L5 script | camera_ui | `0x1018c8a0` | CPointCamera::vfunc80 | 6 | 16 | likely open |
| L5 script | camera_ui | `0x1018c8c0` | CPointCamera::vfunc81 | 3 | 16 | likely open |
| L5 script | camera_ui | `0x101150f0` | Global::FUN_101150f0 | 102 | 16 | likely open |
| L5 script | camera_ui | `0x10115300` | CFuncMonitor::vfunc42 | 49 | 16 | likely open |
| L5 script | camera_ui | `0x10115660` | Global::FUN_10115660 | 265 | 16 | likely open |
| L5 script | camera_ui | `0x101157c0` | Global::FUN_101157c0 | 346 | 16 | likely open |
| L5 script | camera_ui | `0x1018c2b0` | Global::FUN_1018c2b0 | 264 | 16 | likely open |
| L5 script | camera_ui | `0x1018c6e0` | Global::FUN_1018c6e0 | 130 | 16 | likely open |
| L5 script | python | `0x10135420` | CLogicPythonCheck::InputTest | 59 | 36 | open |
| L5 script | python | `0x10135150` | CLogicPythonCheck::vfunc82 | 6 | 36 | likely open |
| L5 script | python | `0x10136170` | CLogicPythonCheck::vfunc5 | 54 | 36 | likely open |
| L5 script | scripted | `0x10080f60` | CSceneEntity::Spawn | 8 | 30 | open |
| L5 script | scripted | `0x100819b0` | CSceneEntity::Remove | 325 | 30 | open |
| L5 script | scripted | `0x1006d1f0` | CBaseCineCam::vfunc80 | 6 | 30 | likely open |
| L5 script | scripted | `0x1006d210` | CBaseCineCam::vfunc81 | 3 | 30 | likely open |
| L5 script | scripted | `0x1006d500` | CBaseCineCam::vfunc82 | 6 | 30 | likely open |
| L5 script | scripted | `0x1006d8f0` | CBaseCineCam::vfunc117 | 8 | 30 | likely open |
| L5 script | scripted | `0x1006d950` | CBaseCineCam::vfunc5 | 54 | 30 | likely open |
| L5 script | scripted | `0x100803c0` | CSceneEntity::FUN_100803c0 | 6 | 30 | likely open |
| L5 script | scripted | `0x100809c0` | CSceneEntity::vfunc5 | 30 | 30 | likely open |
| L5 script | scripted | `0x10080f80` | CSceneEntity::FUN_10080f80 | 118 | 30 | likely open |
| L5 script | scripted | `0x10081210` | CSceneEntity::FUN_10081210 | 275 | 30 | likely open |
| L5 script | scripted | `0x10081450` | CSceneEntity::FUN_10081450 | 25 | 30 | likely open |
| L5 script | scripted | `0x100815d0` | CSceneEntity::FUN_100815d0 | 52 | 30 | likely open |
| L5 script | scripted | `0x10081620` | CSceneEntity::FUN_10081620 | 173 | 30 | likely open |
| L5 script | scripted | `0x100818d0` | CSceneEntity::vfunc261 | 31 | 30 | likely open |
| L5 script | scripted | `0x10082c20` | CSceneEntity::FUN_10082c20 | 66 | 30 | likely open |
| L5 script | scripted | `0x10083700` | CSceneEntity::FUN_10083700 | 248 | 30 | likely open |
| L5 script | scripted | `0x10083ad0` | CSceneEntity::FUN_10083ad0 | 223 | 30 | likely open |
| L5 script | scripted | `0x10083bf0` | CSceneEntity::FUN_10083bf0 | 126 | 30 | likely open |
| L5 script | scripted | `0x10083c90` | CSceneEntity::vfunc267 | 48 | 30 | likely open |
| L5 script | scripted | `0x10072b60` | Global::FUN_10072b60 | 44 | 30 | likely open |
| L5 script | scripted | `0x10072d20` | Global::FUN_10072d20 | 33 | 30 | likely open |
| L5 script | scripted | `0x10072d60` | Global::FUN_10072d60 | 22 | 30 | likely open |
| L5 script | scripted | `0x10072db0` | Global::FUN_10072db0 | 70 | 30 | likely open |
| L5 script | scripted | `0x10072eb0` | Global::FUN_10072eb0 | 7 | 30 | likely open |
| L5 script | scripted | `0x10072ed0` | Global::FUN_10072ed0 | 33 | 30 | likely open |
| L5 script | scripted | `0x10072f10` | Global::FUN_10072f10 | 206 | 30 | likely open |
| L5 script | scripted | `0x10073220` | Global::FUN_10073220 | 42 | 30 | likely open |
| L5 script | scripted | `0x10073260` | Global::FUN_10073260 | 25 | 30 | likely open |
| L5 script | scripted | `0x100732b0` | Global::FUN_100732b0 | 13 | 30 | likely open |
| L5 script | scripted | `0x10073380` | Global::FUN_10073380 | 44 | 30 | likely open |
| L5 script | scripted | `0x100734f0` | Global::FUN_100734f0 | 22 | 30 | likely open |
| L5 script | scripted | `0x10073520` | Global::FUN_10073520 | 4 | 30 | likely open |
| L5 script | scripted | `0x10073540` | Global::FUN_10073540 | 7 | 30 | likely open |
| L5 script | scripted | `0x10073560` | Global::FUN_10073560 | 33 | 30 | likely open |
| L5 script | scripted | `0x10073760` | Global::FUN_10073760 | 23 | 30 | likely open |
| L5 script | scripted | `0x100737b0` | Global::FUN_100737b0 | 9 | 30 | likely open |
| L5 script | scripted | `0x100737d0` | Global::FUN_100737d0 | 13 | 30 | likely open |
| L5 script | scripted | `0x10073810` | Global::FUN_10073810 | 47 | 30 | likely open |
| L5 script | scripted | `0x10073850` | Global::FUN_10073850 | 53 | 30 | likely open |
| L5 script | scripted | `0x100738a0` | Global::FUN_100738a0 | 3 | 30 | likely open |
| L5 script | scripted | `0x100738c0` | Global::FUN_100738c0 | 7 | 30 | likely open |
| L5 script | scripted | `0x10073940` | Global::FUN_10073940 | 56 | 30 | likely open |
| L5 script | scripted | `0x10073990` | Global::FUN_10073990 | 41 | 30 | likely open |
| L5 script | scripted | `0x100739d0` | Global::FUN_100739d0 | 33 | 30 | likely open |
| L5 script | scripted | `0x10073a50` | Global::FUN_10073a50 | 47 | 30 | likely open |
| L5 script | scripted | `0x10073a90` | Global::FUN_10073a90 | 53 | 30 | likely open |
| L5 script | scripted | `0x10074000` | Global::FUN_10074000 | 61 | 30 | likely open |
| L5 script | scripted | `0x10074b10` | Global::FUN_10074b10 | 168 | 30 | likely open |
| L5 script | scripted | `0x10075040` | Global::FUN_10075040 | 174 | 30 | likely open |
| L5 script | scripted | `0x10075350` | Global::FUN_10075350 | 310 | 30 | likely open |
| L5 script | scripted | `0x10075c00` | Global::FUN_10075c00 | 4 | 30 | likely open |
| L5 script | scripted | `0x10075d00` | Global::FUN_10075d00 | 224 | 30 | likely open |
| L5 script | scripted | `0x10076630` | Global::FUN_10076630 | 44 | 30 | likely open |
| L5 script | scripted | `0x10076690` | Global::FUN_10076690 | 7 | 30 | likely open |
| L5 script | scripted | `0x100766f0` | Global::FUN_100766f0 | 13 | 30 | likely open |
| L5 script | scripted | `0x10076710` | Global::FUN_10076710 | 7 | 30 | likely open |
| L5 script | scripted | `0x10076990` | Global::FUN_10076990 | 137 | 30 | likely open |
| L5 script | scripted | `0x10076b10` | Global::FUN_10076b10 | 7 | 30 | likely open |
| L5 script | scripted | `0x10076e70` | Global::FUN_10076e70 | 90 | 30 | likely open |
| L5 script | scripted | `0x10076ef0` | Global::FUN_10076ef0 | 7 | 30 | likely open |
| L5 script | scripted | `0x10076f10` | Global::FUN_10076f10 | 106 | 30 | likely open |
| L5 script | scripted | `0x10076fa0` | Global::FUN_10076fa0 | 7 | 30 | likely open |
| L5 script | scripted | `0x10076fc0` | Global::FUN_10076fc0 | 7 | 30 | likely open |
| L5 script | scripted | `0x100773f0` | Global::FUN_100773f0 | 48 | 30 | likely open |
| L5 script | scripted | `0x10077750` | Global::FUN_10077750 | 7 | 30 | likely open |
| L5 script | scripted | `0x10077770` | Global::FUN_10077770 | 35 | 30 | likely open |
| L5 script | scripted | `0x100777b0` | Global::FUN_100777b0 | 29 | 30 | likely open |
| L5 script | scripted | `0x100777e0` | Global::FUN_100777e0 | 38 | 30 | likely open |
| L5 script | scripted | `0x10077820` | Global::FUN_10077820 | 34 | 30 | likely open |
| L5 script | scripted | `0x10077860` | Global::FUN_10077860 | 30 | 30 | likely open |
| L5 script | scripted | `0x100785d0` | Global::FUN_100785d0 | 7 | 30 | likely open |
| L5 script | scripted | `0x100785f0` | Global::FUN_100785f0 | 13 | 30 | likely open |
| L5 script | scripted | `0x10078610` | Global::FUN_10078610 | 7 | 30 | likely open |
| L5 script | scripted | `0x10078690` | Global::FUN_10078690 | 262 | 30 | likely open |
| L5 script | scripted | `0x10078860` | Global::FUN_10078860 | 11 | 30 | likely open |
| L5 script | scripted | `0x10078880` | Global::FUN_10078880 | 228 | 30 | likely open |
| L5 script | scripted | `0x10078a30` | Global::FUN_10078a30 | 168 | 30 | likely open |
| L5 script | scripted | `0x10078b10` | Global::FUN_10078b10 | 8 | 30 | likely open |
| L5 script | scripted | `0x10078b30` | Global::FUN_10078b30 | 40 | 30 | likely open |
| L5 script | scripted | `0x10078b70` | Global::FUN_10078b70 | 8 | 30 | likely open |
| L5 script | scripted | `0x10078b90` | Global::FUN_10078b90 | 40 | 30 | likely open |
| L5 script | scripted | `0x10078bd0` | Global::FUN_10078bd0 | 8 | 30 | likely open |
| L5 script | scripted | `0x10078bf0` | Global::FUN_10078bf0 | 40 | 30 | likely open |
| L5 script | scripted | `0x10078c30` | Global::FUN_10078c30 | 8 | 30 | likely open |
| L5 script | scripted | `0x10078c50` | Global::FUN_10078c50 | 40 | 30 | likely open |
| L5 script | scripted | `0x10078ea0` | Global::FUN_10078ea0 | 91 | 30 | likely open |
| L5 script | scripted | `0x10078f20` | Global::FUN_10078f20 | 76 | 30 | likely open |
| L5 script | scripted | `0x10078f90` | Global::FUN_10078f90 | 40 | 30 | likely open |
| L5 script | scripted | `0x10079060` | Global::FUN_10079060 | 115 | 30 | likely open |
| L5 script | scripted | `0x10079100` | Global::FUN_10079100 | 114 | 30 | likely open |
| L5 script | scripted | `0x10079320` | Global::FUN_10079320 | 170 | 30 | likely open |
| L5 script | scripted | `0x10079400` | Global::FUN_10079400 | 64 | 30 | likely open |
| L5 script | scripted | `0x10079450` | Global::FUN_10079450 | 110 | 30 | likely open |
| L5 script | scripted | `0x10079910` | Global::FUN_10079910 | 113 | 30 | likely open |
| L5 script | scripted | `0x100799b0` | Global::FUN_100799b0 | 351 | 30 | likely open |
| L5 script | scripted | `0x10079f50` | Global::FUN_10079f50 | 195 | 30 | likely open |
| L5 script | scripted | `0x1007a060` | Global::FUN_1007a060 | 195 | 30 | likely open |
| L5 script | scripted | `0x1007a250` | Global::FUN_1007a250 | 4 | 30 | likely open |
| L5 script | scripted | `0x1007a270` | Global::FUN_1007a270 | 26 | 30 | likely open |
| L5 script | scripted | `0x1007b7b0` | Global::FUN_1007b7b0 | 378 | 30 | likely open |
| L5 script | scripted | `0x1007b9e0` | Global::FUN_1007b9e0 | 51 | 30 | likely open |
| L5 script | scripted | `0x1007ba30` | Global::FUN_1007ba30 | 79 | 30 | likely open |
| L5 script | scripted | `0x1007baa0` | Global::FUN_1007baa0 | 53 | 30 | likely open |
| L5 script | scripted | `0x1007baf0` | Global::FUN_1007baf0 | 40 | 30 | likely open |
| L5 script | scripted | `0x1007bb30` | Global::FUN_1007bb30 | 103 | 30 | likely open |
| L5 script | scripted | `0x1007bbc0` | Global::FUN_1007bbc0 | 329 | 30 | likely open |
| L5 script | scripted | `0x1007bd70` | Global::FUN_1007bd70 | 303 | 30 | likely open |
| L5 script | scripted | `0x1007ce20` | Global::FUN_1007ce20 | 101 | 30 | likely open |
| L5 script | scripted | `0x1007ceb0` | Global::FUN_1007ceb0 | 201 | 30 | likely open |
| L5 script | scripted | `0x1007cfc0` | Global::FUN_1007cfc0 | 365 | 30 | likely open |
| L5 script | scripted | `0x1007d190` | Global::FUN_1007d190 | 63 | 30 | likely open |
| L5 script | scripted | `0x1007d1e0` | Global::FUN_1007d1e0 | 52 | 30 | likely open |
| L5 script | scripted | `0x1007d230` | Global::FUN_1007d230 | 81 | 30 | likely open |
| L5 script | scripted | `0x1007d2b0` | Global::FUN_1007d2b0 | 53 | 30 | likely open |
| L5 script | scripted | `0x1007d300` | Global::FUN_1007d300 | 518 | 30 | likely open |
| L5 script | scripted | `0x1007d5a0` | Global::FUN_1007d5a0 | 460 | 30 | likely open |
| L5 script | scripted | `0x1007d7f0` | Global::FUN_1007d7f0 | 1541 | 30 | likely open |
| L5 script | scripted | `0x1007dfc0` | Global::FUN_1007dfc0 | 10 | 30 | likely open |
| L5 script | scripted | `0x1007dfe0` | Global::FUN_1007dfe0 | 13 | 30 | likely open |
| L5 script | scripted | `0x1007e5a0` | Global::FUN_1007e5a0 | 13 | 30 | likely open |
| L5 script | scripted | `0x1007e5f0` | Global::FUN_1007e5f0 | 431 | 30 | likely open |
| L5 script | scripted | `0x1007e970` | Global::FUN_1007e970 | 227 | 30 | likely open |
| L5 script | scripted | `0x1007ecf0` | Global::FUN_1007ecf0 | 13 | 30 | likely open |
| L5 script | scripted | `0x1007ed10` | Global::FUN_1007ed10 | 7 | 30 | likely open |
| L5 script | scripted | `0x1007ee00` | Global::FUN_1007ee00 | 8 | 30 | likely open |
| L5 script | scripted | `0x1007ee20` | Global::FUN_1007ee20 | 8 | 30 | likely open |
| L5 script | scripted | `0x1007ee40` | Global::FUN_1007ee40 | 8 | 30 | likely open |
| L5 script | scripted | `0x1007ee60` | Global::FUN_1007ee60 | 61 | 30 | likely open |
| L5 script | scripted | `0x1007eeb0` | Global::FUN_1007eeb0 | 61 | 30 | likely open |
| L5 script | scripted | `0x1007ef00` | Global::FUN_1007ef00 | 669 | 30 | likely open |
| L5 script | scripted | `0x1007f250` | Global::FUN_1007f250 | 240 | 30 | likely open |
| L5 script | scripted | `0x1007f380` | Global::FUN_1007f380 | 51 | 30 | likely open |
| L5 script | scripted | `0x1007f3d0` | Global::FUN_1007f3d0 | 23 | 30 | likely open |
| L5 script | scripted | `0x1007f400` | Global::FUN_1007f400 | 140 | 30 | likely open |
| L5 script | scripted | `0x1007f4c0` | Global::FUN_1007f4c0 | 52 | 30 | likely open |
| L5 script | scripted | `0x1007f510` | Global::FUN_1007f510 | 17 | 30 | likely open |
| L5 script | scripted | `0x1007f540` | Global::FUN_1007f540 | 40 | 30 | likely open |
| L5 script | scripted | `0x1007f610` | Global::FUN_1007f610 | 40 | 30 | likely open |
| L5 script | scripted | `0x1007f650` | Global::FUN_1007f650 | 40 | 30 | likely open |
| L5 script | scripted | `0x1007f690` | Global::FUN_1007f690 | 143 | 30 | likely open |
| L5 script | scripted | `0x1007f750` | Global::FUN_1007f750 | 62 | 30 | likely open |
| L5 script | scripted | `0x1007f7a0` | Global::FUN_1007f7a0 | 219 | 30 | likely open |
| L5 script | scripted | `0x1007f8c0` | Global::FUN_1007f8c0 | 751 | 30 | likely open |
| L5 script | scripted | `0x1007fc70` | Global::FUN_1007fc70 | 79 | 30 | likely open |
| L5 script | scripted | `0x1007fce0` | Global::FUN_1007fce0 | 103 | 30 | likely open |
| L5 script | scripted | `0x1007fd70` | Global::FUN_1007fd70 | 461 | 30 | likely open |
| L5 script | scripted | `0x1007ffc0` | Global::FUN_1007ffc0 | 464 | 30 | likely open |
| L5 script | scripted | `0x100809f0` | Global::FUN_100809f0 | 109 | 30 | likely open |
| L5 script | scripted | `0x10080ac0` | Global::FUN_10080ac0 | 47 | 30 | likely open |
| L5 script | scripted | `0x10080b00` | Global::FUN_10080b00 | 323 | 30 | likely open |
| L5 script | scripted | `0x10080cb0` | Global::FUN_10080cb0 | 340 | 30 | likely open |
| L5 script | scripted | `0x10081020` | CSceneEntity::FUN_10081020 | 280 | 30 | likely open |
| L5 script | scripted | `0x10081190` | Global::FUN_10081190 | 91 | 30 | likely open |
| L5 script | scripted | `0x10081480` | CSceneEntity::FUN_10081480 | 20 | 30 | likely open |
| L5 script | scripted | `0x100814e0` | CSceneEntity::FUN_100814e0 | 20 | 30 | likely open |
| L5 script | scripted | `0x100815a0` | CSceneEntity::FUN_100815a0 | 20 | 30 | likely open |
| L5 script | scripted | `0x10081950` | CSceneEntity::FUN_10081950 | 20 | 30 | likely open |
| L5 script | scripted | `0x10083960` | Global::FUN_10083960 | 10 | 30 | likely open |
| L5 script | scripted | `0x100839c0` | Global::FUN_100839c0 | 74 | 30 | likely open |
| L5 script | scripted | `0x10083a30` | Global::FUN_10083a30 | 30 | 30 | likely open |
| L5 script | scripted | `0x10083a60` | Global::FUN_10083a60 | 73 | 30 | likely open |
| L5 script | scripted | `0x10084170` | Global::FUN_10084170 | 184 | 30 | likely open |
| L5 script | scripted | `0x100845a0` | Global::FUN_100845a0 | 212 | 30 | likely open |
| L5 script | scripted | `0x10099140` | Global::FUN_10099140 | 78 | 30 | likely open |
