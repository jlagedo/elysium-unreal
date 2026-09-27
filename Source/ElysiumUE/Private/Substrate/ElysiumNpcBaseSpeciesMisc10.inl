// `CAI_BaseNPC`'s declarations of the `SpeciesMisc10` family (story 5 step 5),
// moved from `ElysiumNpcSpeciesMisc10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`) override; the definitions are in `ElysiumNpcBaseSpeciesMisc10.cpp`.

/** `CAI_BaseNPC::LeaveGrappleState` (`0x1026ce30`), 100 bytes — a DISTINCT retail function beside
 *  the Troika override `0x102b5d90` that owns slot 380, so it takes its own name and is not a slot
 *  body. Four UNCONDITIONAL steps with no grapple-type gate anywhere: fire `m_OnGrappleEnd` with the
 *  grapple partner as activator (null when the handle is stale), `CBaseCombatCharacter::
 *  LeaveGrappleState`, slot 416 `SetForceFrequentThink(false)`, then `0x10007ea0` — the saturating
 *  decrement of `m_iIsOblivious` (`+0x5bb4`) clamped at 0, and the squad reconnect `0x10009601`.
 *
 *  `FElysiumNpcBase::LeaveGrappleState` (`ElysiumNpc.cpp`) calls this and then slot 614. */
void LeaveGrappleState() override;

/** SEAM for `0x1015d680(player, 0)` — the entity in the player's inventory slot 0 (the `+0x2308`
 *  handle array), which both the ManBat's HUD emitter and the head claw's gate on. This runtime's
 *  inventory is `FElysiumInventory`; this answers its active weapon entity, which IS retail's slot 0
 *  for a character carrying one, and null otherwise — retail's own refusal. */
FElysiumEntity* PlayerInventorySlot0() const;
