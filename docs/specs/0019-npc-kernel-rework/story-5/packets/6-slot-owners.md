# Step 6 — every slot body on its retail owner; the census enforces the current tree

Base: `33fcd525` (step 5 recorded).
- 6a: records and checker, committed `5ddaee2c`.
- 6b: the two chain classes, committed `782a27e6`.
- 6c–6i: the one step-6 commit.

Manifest phase 6, revision `step-6-slot-owners`.

## Packets

- [6a-preflight.md](6a-preflight.md): the slot and member records and `kernel_migration --check step6`.
- [6b-chain-classes.md](6b-chain-classes.md): `FElysiumAnimatingOverlay` and `FElysiumFlex`.
- **6c — the per-owner slot surface (`gen_kernel_shape`).**
  - The chain's primary vtables are read from the corpus.
  - Each generated slot is declared by the port class of its introducer, with that class's own body,
    and overridden by each port class whose table refills it.
  - Generated files: `Public/Elysium{Entity,Animating,AnimatingOverlay,Flex,CombatCharacter}Slots.inl`
    and `Private/Substrate/…Slots.cpp`, beside the shrunken `ElysiumNpcBaseSlots.*` /
    `ElysiumNpcSlots.*`.
  - Stubs name their retail owner.
  - `ShadowedSlotDefaults()` probes the constant bodies a Troika instance never dispatches to.
  - `SLOT_PORT_MAP` rows:
    - `PORT` 118 `AcceptInput` → `FElysiumEntityWorld::AcceptInput`;
    - `PORT` 15 → `FElysiumEntity::SetAttackExtents`;
    - `DELETED` 582 `ReportOverThinkLimit`;
    - eight `ACCEPTED` subclass collisions.
  - The collision scan now covers every class deriving from the chain below the NPC line.
  - `CHAIN_HAND` restores the reference returns of 194/195/217/219/220/221 and makes 62/93 hand
    bodies.
  - The ledger, inventory and skeleton readers know the new files.
- **6d — hand bodies to their owners** (`step6/tools/relocate6.py`).
  - 84 chain-owned hand bodies and 129 closure members moved: 125 declarations and 152 definitions
    into `Elysium<Class>SlotBodies.{inl,cpp}`, plus 41 file locals (1 exported,
    `NpcBaseEntityChainShared::GChainOne`).
  - `FElysiumNpcBase::X` references were requalified; 93 overlay targets were retargeted.
  - Carriers:
    - `AttackExtentsCm` and `SetAttackExtents` → `FElysiumEntity` (its save line stays in the base
      record, same order);
    - `NpcFlags` → `FElysiumCombatCharacter`, with `ElysiumNpcFlags.h` now in `Public/`.
  - Retypes:
    - `FElysiumNpcSenses::ViewForward` / point `IsInViewCone` take `const FElysiumEntity&`;
    - `Combat10Now` likewise.
  - `IsAlive` reads the death latch through `AsCombatCharacter()`.
- **6e — the integrations** (`decisions-step6.json` `integrations`):
  - `SetOrigin` (62) writes through `SetRuntimeOrigin`, and only when the origin differs.
  - `SetMoveType` (93) is the `RetailMoveType`/`RetailMoveCollide` seam, moved to `FElysiumEntity`.
    `TestHullSpawn` goes through it.
  - 217/219/220/221 answer `Origin`/`Angles` by reference, and 194/195 follow. The local/abs split
    is a named modernization.
  - `AcceptInput` (118) is the world chokepoint.
  - `Weapon_Switch` (388) and `GetModelIndex` (8) stay counting stubs, on their owners.
- **6f — stub dispositions and remaps.**
  - One dead stub deleted (582).
  - Every other stub moved with its row.
  - `expectations/step-6.json` covers the owner-prefix remaps and the exact diagnostic changes.
- **6g — obsolete machinery:** nothing retired.
  - `FVocalization` still feeds the Camera/SabbatLeader/Tzimisce overrides through
    `SpeciesVocalize` (step 11).
  - `ElysiumEntityCaps::SpeciesRows` holds the unported `CAI_Hint` `ObjectCaps` override and the
    step-10 `CAI_TestHull` row.
  - `surviving_sites` is re-keyed to the current tree (50 sites, 55 tokens).
- **6h — census.**
  - `kernel_shape --unported` pins 987 live unported rules (`unported-step6.tsv`): 59 stubs, 201
    deferred-class bodies, 727 species bodies. The set must only fall.
  - New tests (`ElysiumNpcKernelChainSlotsTests.cpp`):
    - `NpcKernelSlots.ShadowedDefaults`;
    - `NpcKernelChainSlots.{StubNamesItsOwner, EntityIntegrations, DescriptorChain,
      PedestrianRestoreMoveType}`.
  - Re-pinned: the two closure `SetOrigin` cases, Motor10's `Hide` tally surface, and the census
    callable rule for the deleted row.
- **6i:**
  - regenerated ledger/lists/shape;
  - the oracle (`docs/vtmb/npc-ai/shape.md` § "The tables");
  - `spec.md` corrections (269/83; the eight are audited dispositions, not same-name merges);
  - packet 5e-5f's stale counts;
  - the records and acceptance.

## Retail corrections

- `pedestrian-set-move-type` (`0x100aad70`, caller `103a2816`): the Pedestrian restore's slot-93 call
  now writes `m_MoveType`/`m_MoveCollide`.
- `geometry-angles` (`0x100b3110`): `LocalEyeAngles() == GetAngles()` compared two null pointers;
  both now answer `Angles`.

## Acceptance ([acceptance-step6.json](../acceptance-step6.json))

- Runtime gate (`step6/gate`):
  - build green;
  - **1,279 Substrate + 14 Content + 1 PlayerWorld, zero failures.**
- `test_delta` against step 5's gate passes with 142 reviewed expectations:
  - 119 owner-prefix remaps: address and receiver unchanged;
  - 18 exact diagnostic changes: stubs that no longer fire, and map-epoch shifts;
  - 5 additions.
  - No stub fired on a new receiver class.
- Checks gate: the five generator checks, the kernel pytest, `--check factories/step0..5`, and
  `--check step6` PASS at phase 6.
- Map smoke (`step6/smoke.json`).

## Open follow-ups

- `.claude/rules/cpp.md` puts one class per file. 6b declared the two new chain classes in
  `ElysiumPlayer.h`. The conformant layout needs a verbatim move of `FElysiumAnimating` to its own
  header first (owner decision pending).
- `FElysiumNpcFlags` carries the `CAI_BaseNPC` refcount `m_iIsOblivious` (+0x5bb4) beside the two
  combat-character words; it moved whole (a named transitional aggregation).
- The build reports the accepted hides as C4263/C4264 warnings.
