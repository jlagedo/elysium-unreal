# Step 4, packet 4c — merged carriers, relocation tool, header batch plan

Start: `0019-5-class-tree`, `9f087e42` + the uncommitted 4b tree (identical to
`step4/checkpoints/4b-done.patch`), manifest `step-3-species-dispatch` (phase 3).
Executor: Claude Code, Opus 5.5.
Outcome: each retail word that 4b found carried by two or three port members has one carrier, and
the species bodies and words can be relocated to their class files mechanically, one branch at a
time. Part of the atomic step-4 change; uncommitted.

## Owns

- The 11 `collapse:merge` rows of `moves-step4.tsv`. Every use is renamed to the survivor
  (`step4/merge_carriers.py`), and the loser declarations and their comments are removed from the
  family `.inl` files:

  | Loser | Survivor | Retail word |
  | --- | --- | --- |
  | `bSpeciesPathBlocked` | `bAsianVampirePathBlocked` | `CNPC_VAsianVampire` `+0x66d4` |
  | `bAsianVampireSuppressRanged` | `bSuppressRanged` | `CNPC_VAsianVampire` `+0x66e8` |
  | `BachFailStamp`, `BachRepositionTimer` | `BachNextHolyLightTime` | `CNPC_VBach` `+0x6690` |
  | `MoveGoalNodeId` | `ManBatMoveGoalNodeId` | `CNPC_VManBat` `+0x6674` |
  | `MingXiaoPickupCooldownA`, `MingXiaoPickupCooldownB` | `MingXiaoAttackTimers[4]`, `[5]` | `CNPC_VMingXiao` `+0x66d4`, `+0x66d8` |
  | `SeveredTentacleMask` | `MingXiaoSeveredTentacleMask` | `CNPC_VMingXiao` `+0x6710` |
  | `SpeciesThrowObject` | `MingXiaoThrowObject` | `CNPC_VMingXiao` `+0x6718` |
  | `SabbatLeaderLastSplashTime` | `SabbatLastSplashTime` | `CNPC_VSabbatLeader` `+0x66c8` |
  | `bSabbatLeaderDiving` | `bSabbatDiving` | `CNPC_VSabbatLeader` `+0x66d5` |

- The relocation tool, `step4/relocate.py`. Given a set of owner classes, it takes their `move` and
  `collapse:<override>` rows and:
  - moves each declaration from its family `.inl` to `ElysiumNpc<X>.h`, with its attached comment;
  - moves each `FElysiumNpc::` definition to `ElysiumNpc<X>.cpp`, requalified as
    `FElysiumNpc<X>::`;
  - moves the file-scope locals those bodies use. A local that only the owner's bodies use goes
    to the owner's `.cpp`. Any other local goes to a narrow support header,
    `ElysiumNpcKernel<Family>Shared.h`, under a namespace, and every use is qualified. Unity
    builds make an anonymous-namespace name collide across files, so no moved local relies on
    being file-local.
  - `step4/reloc_scan.py` measured the locals: 1,664 across 61 kernel `.cpp` files. About 470 are
    used only by one class's moving bodies, 99 are shared with staying bodies, and the rest are
    staying-only or unused.

## Retail corrections (merges)

Each merge makes a write one family made visible to the other family's reader:

- AsianVampire `NPCInit` `0x10360ce0` sets `m_bSuppressRanged = 1`. The two ranged arms of
  `SelectScheduleMeleeCombat` `0x10361be0` read that word, so they are now closed after init, as in
  retail. The port's init wrote a second carrier nobody read.
- `TaskFail` `0x10362390` sets `m_bPathBlocked`, and `NPCInit` clears it: now one word.
- Bach: both COND `0x7b` arms, `0x10364080` and `0x103642f0`, stamp `m_flNextHolyLightTime`. The
  shield block `0x10363db0` reads that stamp.
- ManBat: both `SelectSchedule` `0x1038e340` early-out arms write `m_iMoveGoalNodeID`. The fly
  velocity body `0x1038b370` saves and restores the same word around its hint search.
- MingXiao: the pickup gate `0x10396bc0` reads attack timers 4 and 5. The sever mask the body
  group copies is the one `IsTentacleConnected` `0x10398000` reads. The throw object `TaskFail`
  `0x10394090` releases is the one the throw bodies use.
- SabbatLeader: `NPCInit` clears the splash stamp and the dive flag that
  `UpdateBloodSplash` reads.

## Order of the remaining packets

The split carriers (`SpeciesShunnedFindCount`, `SpeciesPickupTarget`, `PathMode`, the tentacle id)
are written by per-class bodies: `OnScheduleChange`, `TaskFail`, `NPCInit` and slot 410. 4b declared
the split members on the species classes, so each split lands with its branch's relocation.

The header batch is applied per branch, not in one pass. Relocating a declaration forces its
definition, its locals and its tests to follow in the same build, so each branch is one
build-and-fix cycle:

- 4d: Animal line, Camera line, Payphone, Placeholder, Newscaster, MingXiaoTentacle.
- 4e: BaseBoss line.
- 4f: Human line.
- 4g: Vampire line.

Each branch then collapses its forwarding overrides and table rows.

## Checks

- Build and the focused prefixes after each branch.
- The merge and split tests landed together in 4h:
  `Elysium.Substrate.NpcKernelSpeciesBindings.MergedAndSplitCarriers` writes each retail word
  through its datamap binding and reads the survivor member.

## As executed

Relocation ran per branch as planned: 4d, 4e, 4f, 4g. After it, the class-keyed tables were
collapsed ([4-species-bodies-words-bindings.md](4-species-bodies-words-bindings.md)). Placement
rules found during the relocation are folded into `decisions-step4.json` `rules`:
`base_seams_stay`, `adapters_stay_helpers`, `typed_species_access`, `protected_troika_state`,
`splits_land_with_the_last_reader` and `record_only_tables`.
