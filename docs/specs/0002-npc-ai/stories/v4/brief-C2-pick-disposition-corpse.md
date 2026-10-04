# Brief C2 — V4c: the death chain's doc, the weighted pick, `SetDisposition`, the `ACT_DIERAGDOLL` seed, the pedestrian's `CreateCorpse` (coder; no build)

**Final (amended after V4r, settling packets S1 and S4 and the judge's second sitting, 2026-10-04
— R2, J8, K4, S1 items 1–4; J2b, J13, J14.1, S4 items e and f.1).** The second sitting gives you
the pedestrian's `Think` gate (item 8) and **the death fade** (item 10), and closes
`SequenceBounds` as a named seam (item 7); read its section (`stories/v1/triage.md` § "Judge's
rulings, V4 — second sitting"; Grep, read only it). Where
this brief and the README disagree on death, this brief wins. Read `packets-S1.md` items 1–3 (the
listing walk of `CreateCorpse`, the two `Event_Killed` bodies, the state-7 writers). Read `README.md` here (§1 "The frame"
with R2's gate, "The weighted pick", "The disposition change" and "Death" with their amendments;
§2 M8, M9, M12; §7 K4, K5; § "Shared names"), `packets-R2.md` item 7 and extension 2, the ruling
J8 (`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it), `brief-D-ragdoll.md`
§ "What retail does", `docs/vtmb/activity_enum.md` :294-337, `docs/vtmb/npc-ai/shape.md` § "The
activity commit", `docs/vtmb/npc-ai/lifecycle.md` § "The death chain, kill to corpse" (~:2642;
§ "The ordered chain" ~:2730, § "A death sounds more than once" ~:2758),
`stories/v1/divergences.md` row 4. After V4b's commit. V3c filled `LookupSequenceByName` — use it.
Re-locate by Grep.

## Files (only these)

`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise:

- `docs/vtmb/npc-ai/lifecycle.md` (§ "A death sounds more than once" only — § "The ordered chain"
  steps 5–6 and the state-7 writers are **already rewritten by S1**; do not rewrite them)
- `ElysiumNpcAnim.cpp` (`SequenceForActivity` ~:375-391, the row cache, and `SequenceBounds`'s
  body) and `ElysiumNpc.h` (the declarations)
- `ElysiumNpcBaseAnim.cpp` (`RunAnimation`'s idle re-pick ~:168-182 only)
- `ElysiumNpcBaseStartTask.cpp` (`StartTaskSlot442`'s `SelectWeightedSequence` ~:399-400 only)
- `ElysiumNpc.cpp` (`FElysiumNpc::SetDisposition` ~:1107-1177, `BecomeClientRagdoll` ~:285-311,
  `PlayActivity`'s variant ~:1124, `StartWalkingAnimation`'s variant ~:529, and — item 8, J13 —
  `Think`'s committed-death gate ~:645 with the removal thinks beside it ~:574-660 — these only)
- item 10 (the fade): `ElysiumNpcBaseRunTask.cpp`, `ElysiumNpcBaseRunTask.inl` (`StartFadeOut`
  and its seam words only), and the one file that holds the port's `Event_Killed` step after
  `CreateCorpse` (Grep `0x10265d72` / `Spawn19StartFadeOut`: today `ElysiumNpcBaseSpawn.cpp`
  ~:135, ~:204 — that call and that function only)
- K4: `Source/ElysiumUE/Private/Visual/ElysiumAnimationResolve.cpp` (`PickWeighted` ~:703-748,
  `TryActivity` ~:187), `ElysiumCombatCharacter.cpp` (`PlayReactionActivity` ~:2025, and — item 8 —
  `FElysiumCombatCharacter::CreateCorpse` ~:1455, the chain's home; these two only),
  `Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp` (the pick's variant ~:538 only),
  `ElysiumProp.cpp` (the random animator's pick ~:136 only)
- item 8: `ElysiumNpcPedestrian.h`, `ElysiumNpcPedestrian.cpp` (`PedestrianCreateCorpse` and its
  seam words only), `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSpeciesMisc10Tests.cpp` (the
  assertions on `PedestrianCreateCorpse` only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp` (`NpcCombat.Death` ~:1629-1661)

Wave check (C2 against C1, by function): no file is shared. `ElysiumCombatCharacter.cpp` is not
in C1's list (C1's slot 315 is in `ElysiumCombatCharacterSlots.cpp`, another file, and not
yours); `ElysiumNpc.cpp`, `ElysiumNpcAnim.cpp`, `ElysiumNpcBaseStartTask.cpp` are not C1's
(C1's start arms are in `ElysiumNpcStartTask.cpp`). Re-checked after the second sitting: the
files it adds to you — `ElysiumNpcBaseRunTask.{cpp,inl}`, `ElysiumNpcBaseSpawn.cpp` — are not
C1's either (C1's `PostRun` line is in `ElysiumNpcBaseMotor.cpp`), and C1 lost the bbox files to
no lane (J2b).

**Not** `ElysiumWeaponClasses.cpp` (C1's whole): K4's line there
(`FElysiumWeapon::BuildActivityClipRequest` ~:1077, `Variant = Handle.Index`) you write **in your
report**, exact, and the integrator applies it.

## The job

1. **The death chain's doc — the walk is done and confirmed (S1); one section is left** (J8).
   `lifecycle.md` § "The ordered chain" steps 5–6 are **already rewritten by S1** and the state-7
   writers are listed there; you do not walk `CreateCorpse` again and you do not rewrite those
   steps. What S1 verified in the listing, for you to port against (`packets-S1.md` items 1–3):
   - `CreateCorpse 0x1032c0e0` **always passes a real bone** (the hit box's bone, or
     `LookupBone("Bip01 Spine2")`), and **every NPC arm ends with the NPC's think replaced**:
     the ordinary arm (`BecomeClientRagdoll(force, bone, 0)`, answer discarded; corpse = slot
     137 = `this`), the `MiscFlag 0x80000` arm (`SpawnStaticCorpse`, `Hide`, `SUB_Remove` at
     +0.5 s), then the tail when the corpse is non-null: not burning → `SUB_PVSRemove` at +10 s
     (`0x1032c404`); `burn` (`0x10207df0`: `Has_Burning_Death`, or Kindred without
     `Disallow_Kindred_Death`) → `BurnModel` and `SUB_Remove` at +10 s. The replacement is
     `CreateCorpse`'s, not `BecomeClientRagdoll`'s. So **an ordinary kill never returns to
     `NPCThink` and never reaches `SCHED_DIE`, rig or no rig**.
   - `BecomeClientRagdoll 0x10090180` with no rig **zeroes the collision bounds and returns
     false** — nothing else (no solid flag, no move type, no think change; not "zeroes
     velocity").
   - The state-7 fork's `BecomeClientRagdoll(vec3_origin, −1, 0)` at `0x1028a8ec` is **the only
     NPC caller that passes bone −1** (two non-NPC callers exist: `0x1012b370`, the `raggib`
     entity's setup, and `0x102b5bb0`, 17 bytes with no static caller) and so the only NPC path
     to the `ACT_DIERAGDOLL` seed (`0x1009021a`). Retail's reachers of the fork: **the deferred
     script death** (`CineCleanup 0x1027d170`, the `m_iHealth < 1` arm, `:0x2b22`), its sibling
     `0x1027d0a0` (from `SelectIdealState` case 4), `CNPC_VWerewolf::SelectIdealState
     0x103d0820`, and the zombie's collapse (`CNPC_VZombie::CreateCorpse 0x103dfbb0`'s
     non-ragdoll arm, when schedule `0x162` ends). **No step-2 arena record reaches it**, and
     step 2 needs neither the seed nor `SCHED_DIE` for a green record.
   Your doc edit: rewrite § "A death sounds more than once", dated, "V4c lane C2": the death
   sound plays **once** on an ordinary kill (`Event_Killed 0x10265ad0` step 3); **three times
   only on the rig-less fork route** (`DIE 0x2b` → `TASK_SOUND_DIE`, then `TASK_DIE` → `Die
   0x103392c0` → a full `Event_Killed`). If you read a listing and it contradicts S1 on any
   point, the listing wins: write what you read and say so at the top of your report.
2. **`SelectWeightedSequence 0x1008dc40` on the kernel** (`FElysiumNpc::SelectWeightedSequence(int32
   Activity)`): gather the body's sequences whose baked activity matches (the sequence table behind
   `UElysiumBodyData::Sequences` / `FElysiumNpcClipSet`, with the include-shadowing rule
   `activity_enum.md` :328-337 as far as the table carries the include groups — say what it does not
   carry), then the draw: none → −1, one → it, `RandomInt(0, total−1)` on the **`NpcSchedule`** stream
   walked by `weights[i] <= r`, all-zero → uniform on the same stream. `SelectHeaviestSequence
   0x1008dd30`: strict max, first wins. The answer is a **row** the bridge plays (`m_nSequence`), so
   every candidate becomes a row; the one-clip-per-activity cache goes. Close divergence row 4 at
   its line.
3. **The callers**: `SequenceForActivity` draws through it; `StartTaskSlot442` (its
   `SelectWeightedSequence(act, -1)`); and **`RunAnimation 0x1026c540`'s re-pick as R2 read it**:
   outside states 4 and 7, **`m_Activity (+0xfec) == 1`** (not `m_IdealActivity`) and slot 251
   true → `m_bSequenceLoops` false → `SelectHeaviestSequence(m_TranslatedActivity +0xff4)`, true
   → `SelectWeightedSequence(m_TranslatedActivity +0xff4)` (not `m_Activity`); commit through the
   port of `0x10260a50` when not −1. Slot 251 inside `PostRun` reads `StudioFrameAdvance`'s finish
   OR'd onto the previous think's look-ahead (R2 item 1 (a)) — nothing for you to add, do not
   re-clear the flag.
4. **`SetDisposition 0x102c0f70`, arm by arm as R2 item 7 read it** — it is **not** "gated on
   `m_bDisableAI`"; the flag gates the immediate commit only:
   - `old = +0x64d4`; the lookup `0x100ec530`; a miss → `("Neutral", 1)` with `old = -1`;
   - the tuning writes, always: `+0x64d8`, `+0x6584/8`, `+0x5b94`, `+0xe3c`, `+0x10b4`, `+0x64d0`,
     `+0x10b8` (keep `CommitDisposition`, the table row, as the port's home for them; report a
     word the port lacks, named by its offset);
   - if the index changed: `seq = old == -1 ? slot 611 (0x102c12a0) : GetTransitionAnim
     0x100ed150` (`stance_trans_<old>_<n>_<new>_<n>`, then `_1_…_1`, then the new disposition's
     `idle[stance]` — its fallbacks stay; resolve names through `LookupSequenceByName`). *Slot
     611's body is not walked in the packet: unrecovered; the coder reads it first
     (`0x102c12a0`).*
   - if `seq >= 0`: `m_IdealActivity = 0xf1`, `m_nIdealSequence = seq`;
   - and if `!m_bDisableAI (+0x6080)`: `m_nSequence = seq`, `m_flCycle = 0`, `m_Activity = 0xf1`,
     `m_flAnimTime = curtime`, `ResetSequenceInfo`.
   No direct `PlayNpcClip`, no `ResetAnimToIdle`. The port's `IsFeedBusy()` term is not in the
   listing as R2 read it: remove it and say so.
5. **`BecomeClientRagdoll 0x10090180`'s seed — only on the arm retail reaches** (J8; the owner's
   ruling of 2026-10-04 below stands). The seed — `SelectWeightedSequence(ACT_DIERAGDOLL 0x21)`
   → `m_nSequence`, `m_flCycle = 0`, `ResetSequenceInfo` — runs **only for bone −1**
   (`0x1009021a`). `CreateCorpse` always passes a real bone, so an ordinary corpse ragdolls from
   the pose it holds: **no seed, and no "hold the seed pose" stand-in**. Port the seed inside the
   bone −1 arm, with −1 from the pick → no seed (a model with no `ACT_DIERAGDOLL` sequence), and
   keep the death transaction as it is: slot 144 → `CreateCorpse`, `BecomeClientRagdoll(force,
   bone, 0)` with the hit bone or `LookupBone("Bip01 Spine2")`, the force latched only for |F| >
   0 and bone > 0, the entity made not solid with its think cleared, the +10 s `SUB_PVSRemove` —
   leaving `StartBodyRagdoll`'s answer as it is until V4d gives the mesh a physics asset.
   **Plainly: no step-2 record reaches the seed.** `damage_lethal_death` kills an idle NPC and
   `verbs_stealth_kill` does not defer (`EnterGrappleState 0x102b5c00` cancels a live `m_hCine`
   before the kill); neither the seed nor `SCHED_DIE` is needed for a green record. The seed is
   a **ported-but-arm-tested** item: the port's fork is `ElysiumNpcBaseSelect.cpp:~273` (not
   your file — if it does not pass bone −1, report the exact line), and the arm test is its
   only reacher in step 2. The test's text names retail's reachers (item 1: the deferred script
   death `CineCleanup :0x2b22`, `0x1027d0a0`, the Werewolf, the zombie's collapse).
6. **K4 — the visual side's picks through the same draw, as R2's table maps them**
   (`packets-R2.md` § "Extension 2"; README §7 K4). `PickWeighted` has one production call
   (`TryActivity`); what differs is who sets `Variant`. Replace the hash seed in `PickWeighted`
   with retail's draw (`RandomInt(0, total−1)` walked by `weights[i] <= r`; all-zero → uniform;
   the same function the kernel's pick uses — one body, two callers), and route each site:

   | port site | today's variant | retail body | route |
   |---|---|---|---|
   | `FElysiumNpc::PlayActivity` (`ElysiumNpc.cpp`) | `ScheduleActivityCycle++` | `ResolveActivityToSequence 0x10272130` | the kernel's `SelectWeightedSequence` |
   | `FElysiumNpc::SequenceForActivity` (`ElysiumNpcAnim.cpp`) | 0 | the same, and `RunAnimation 0x1026c540` | item 2 / item 3 |
   | `FElysiumNpc::StartWalkingAnimation` (`ElysiumNpc.cpp`) | `Handle.Index` | the navigator's movement activity → `0x10272130`; the motor's own draw `0x10264680` | the kernel's `SelectWeightedSequence` |
   | `FElysiumCombatCharacter::PlayReactionActivity` | the `Reaction` stream | `AddFlinchGesture 0x10099690`, `PlayerKnockbackReaction 0x101606e0` | the shared draw; it already draws on a stream — keep the stream, drop the hash |
   | `FElysiumWeapon::BuildActivityClipRequest` (C1's file) | `Handle.Index` | player `0x101644f0` / layer `0x1015fbb0`; NPC `0x10272130`; the weapon model `0x10253390`, `0x1024efa0` | **your report's line**; an NPC wielder through the kernel's pick |
   | the locomotion intent (`ElysiumAnimationIntent.cpp`) | the caller's | player: **Heaviest** (`0x101644f0`), no draw; cast: `0x10272130` | the player takes `SelectHeaviestSequence`'s rule (strict max, first wins), no draw |
   | the prop random animator (`ElysiumProp.cpp`) | — | `0x10190850` `SelectWeightedSequence(ACT_IDLE)`; `CBaseProp::Spawn 0x1018df70` | the shared draw |

   Pass-throughs, not picks — leave them: `ElysiumAnimationDriver.cpp:~498` (a cache key),
   `ElysiumAnimSubsystem.cpp:~826`, `ElysiumAnimationResolve.cpp:~801, ~851`;
   `ElysiumGrapple.cpp:~40` is Heaviest already. R2 found **no pick without a retail
   counterpart**; if you find one, list it by name for the owner and leave it.
   **Which port stream stands for retail's one shared engine stream (`*0x1070b244` slot 2) at
   the non-NPC sites (the player's layer, the weapon model, the prop) is not ruled.** NPC sites
   draw on `NpcSchedule` (README § "Shared names"). For the others: keep the stream a site
   already draws on; where a site has none (it used the hash), propose one in your report and
   wire the draw behind a single named function so the owner's answer is a one-line change — do
   not invent a new stream. The driver's variant-keyed cache is port-only: report whether it
   still has a meaning once the variant is gone, do not delete it on a guess.
7. **`SequenceBounds`** (README § "Shared names"; yours because `ElysiumNpcAnim.cpp` is):
   `bool FElysiumNpc::SequenceBounds(int32 Seq, FVector& OutMinCm, FVector& OutMaxCm) const`.
   **J2b is ruled: the bbox import is withdrawn and filed with the story that ports the
   player's acquire cone** (`0x1040f550` / `0x1040f080`); C1 adds no `BboxMinCm` / `BboxMaxCm`.
   So the body is **a named seam answering false ("no descriptor") for every sequence**, its
   comment exactly: "stands for the seqdesc bbox `+0x1c..+0x30`, read by slot 247 `0x10090c80`;
   filled when the acquire cone is ported (J2b)". C1's slot 247 calls it and writes nothing, as
   retail with no seqdesc. Make it virtual or otherwise replaceable by a test double only if
   C1's arm test needs that and the declaration is yours (`ElysiumNpc.h`); say which.
8. **The pedestrian's `CreateCorpse` override, wired** (new from S1; in nobody's files before).
   `CNPC_VPedestrian::CreateCorpse 0x103a38c0` (slot 301's species body) is recovered in the port
   as `PedestrianCreateCorpse` (`ElysiumNpcPedestrian.{h,cpp}`) but **not wired to slot 301**: a
   pedestrian's corpse today takes the base's `SUB_PVSRemove`, and retail's takes **no think at
   all — the chain never removes it**. Both witness maps carry pedestrians (28 / 3). Retail, in
   order: snapshot the collision mins / maxs into `+0x6660` / `+0x666c`; the base
   `CBaseCombatCharacter::CreateCorpse 0x1032c0e0` (all of it, its tail's think included); then
   **`ThinkSet(this, NULL)`** and **`SetSolid(SOLID_NONE)`**. Wire it so a pedestrian's slot 301
   runs that body around the real base (`FElysiumCombatCharacter::CreateCorpse`,
   `ElysiumCombatCharacter.cpp`, the chain's home — as the zombie's override already chains,
   `FElysiumNpcZombie::CreateCorpse`, `ElysiumNpcSpawnSpecies.cpp`, read not edited), replace the
   counted seam (`PedestrianCreateCorpseCalls`, `bPedestrianCorpseThinkStopped`,
   `PedestrianCorpseSolid`) with the real calls where the port has the words, and delete the
   header's "NOT WIRED TO SLOT 301" paragraph. If the dispatch needs a line outside your files
   (a generated slot binding, a `kernel_verdicts.tsv` row for `0x1032c0e0`, a virtual's
   declaration on the combat character's header), write the exact line in your report for the
   integrator.
   **As read whole** (`packets-S4.md` item e) — straight-line, no branch: (1)
   `m_vecPreDeathMins (+0x6660) =` the collision's `OBBMins`, `m_vecPreDeathMaxs (+0x666c) =`
   `OBBMaxs`, taken **before** the base, which zeroes the bounds; (2) the whole base body — the
   `OnDeath` latch, the bone, the three arms and the tail, so `SUB_PVSRemove` (or `BurnModel` +
   `SUB_Remove`) is armed here; (3) `ThinkSet(this, NULL, 0.0, NULL)` — the think the tail just
   armed is dropped (on the static-corpse arm this also drops the hidden NPC's `SUB_Remove` at
   +0.5 s); (4) `SetSolid(SOLID_NONE)` (`0x100dc480(&m_Collision, 0)`).
   **The `Think` gate — without it the wiring changes nothing** (J13). The port's
   `FElysiumNpc::Think` (`ElysiumNpc.cpp` ~:645) is `if (bDeathCommitted || ThinkFunctionName ==
   NpcSubPvsRemoveThinkName())`: it runs `SUB_PVSRemove` on **any** committed death, whatever
   the think name (its comment: a corpse whose think name was not carried, a load). So a
   pedestrian whose think was cleared is still removed. **The cleared think must win there**:
   a committed death whose think was explicitly cleared by slot 301's species body runs no
   removal think. Keep the load case the comment names working (a corpse restored without its
   think name) — distinguish "cleared" from "not carried" with a word the pedestrian's body
   sets, named for `0x103a38c0`'s `ThinkSet(NULL)`, and say at the line which retail state each
   branch stands for.
   **Order with the fade**: `Event_Killed`'s fade step runs after slot 301 (item 10), so **a
   pedestrian with spawnflag bit 9 still fades** — the later think wins (inferred from the
   order; no placed pedestrian on the two maps has the bit).
   Proving record: `corpse_pedestrian_stays` (written red by A0; the C integrator turns it).
10. **The death fade — new, J14.1; a game bug in landed work.** 25 of the 62 makers on the two
   witness maps (`Flag_InfChild 1` on 22, `Flag_Fade 1` on 3 — among them the tutorial's
   `stealth_victim_maker` and `guard_maker`) set `m_bFade`, and `CNPCMaker::MakeNPC 0x1034b7b0`
   then gives the child spawnflags **`| 0x204`** (else `| 4`). For such a body
   `CAI_BaseNPC::Event_Killed 0x10265ad0`, **after slot 301 `CreateCorpse`** (its step 14), asks
   slot 552 `ShouldFadeOnDeath` (spawnflag bit 9, `0x200`) and on true calls
   **`SUB_StartFadeOut 0x102695d0`**; the port's `StartFadeOut` (`ElysiumNpcBaseRunTask.cpp`
   ~:253) is a counted seam reached only from `TASK_DIE`'s arm (~:682), and the port's
   `Event_Killed` never fades. Port:
   - **the call** in the port's `Event_Killed`, after `CreateCorpse`, behind slot 552 — locate it
     by Grep on `0x10265d72` (the listing's call site; `Spawn19StartFadeOut` in
     `ElysiumNpcBaseSpawn.cpp` is today's stand-in for it): make that step reach the real body.
     If the port's `Event_Killed` step lives in a file that is not in your list, write the exact
     line in your report for the integrator and port the two bodies below all the same;
   - **`SUB_StartFadeOut 0x102695d0`**: render mode 2 / alpha 255 when the mode was 0;
     `AddSolidFlags(4)`; zero angular velocity; relink; `m_flNextThink = curtime + 10.0` (the
     f64 cell `0x1044fac0` — 10.0, not 0.0); `ThinkSet(SUB_FadeOut)` (thunk `0x100152b2`);
   - **`SUB_FadeOut 0x10269960`**: render alpha (`m_clrRender` byte 3) `> 7` → `alpha −= 7`,
     `m_flNextThink = curtime + 0.1` (f64 `0x104493d0`); else `alpha = 0`, `m_flNextThink =
     curtime + 0.2` (f64 `0x10449198`), `ThinkSet(SUB_Remove 0x101c0b10)`. From alpha 255: 36
     steps to 3, then 0, then the removal — **the entity is gone about 13.8 s after the death,
     seen or not**;
   - **the think's dispatch**: the port's `Think` (item 8's gate) must run the fade think by
     its name and not `SUB_PVSRemove`; **the later think wins** — a Kindred maker child both
     burns at death (the look and the sound) and fades, removed at about +13.8 s, not +10 s;
   - where the port has no render-alpha word on the entity for the drawn body to follow, keep
     the alpha as the kernel's word and name the visual seam at the line (the look is the
     visual side's; the clock and the removal are state);
   - **check, do not edit, that the maker passes `0x204`** (Grep the port's `MakeNPC`
     / `m_bFade` / `Flag_InfChild` / `Flag_Fade` in the maker's file): if it does not, write
     the exact line in your report — that one line is pulled from R6 into this wave and applied
     by the integrator.
   `verbs_stealth_kill` kills exactly such a child. Proving record: `corpse_fades` (A0 writes
   it red). `TASK_DIE`'s existing call keeps working through the same body.
9. **Tests**: `.PedestrianCorpse` (`0x103a38c0`: the snapshot before the base, the base called,
   the think cleared and the body `SOLID_NONE` after it — no `SUB_PVSRemove` left armed, and
   `Think` on that committed death removes nothing), in
   `ElysiumNpcKernelSpeciesMisc10Tests.cpp` beside the assertions it replaces;
   `Elysium.Arm.NpcKernelAnim.DeathFade` (in `ElysiumNpcCombatTests.cpp` or
   `ElysiumNpcKernelAnimTests.cpp`, say which): `0x102695d0` — the first think at +10.0, the
   solid flag; `0x10269960` — −7 per 0.1 s, 36 steps from 255, alpha 0 then `SUB_Remove` after
   0.2 s; spawnflag bit 9 clear → no fade; `Event_Killed` calls it after `CreateCorpse` and the
   fade think replaces `SUB_PVSRemove` / `SUB_Remove`; delete the assertions that pin
   `StartFadeOutCalls` as a counter and list them; and
   `Elysium.Arm.NpcKernelAnim.WeightedPick`, `.HeaviestPick`, `.RunAnimationPick` (R2's
   gate: `m_Activity == 1`, the pick on `m_TranslatedActivity`, the loop-bit fork),
   `.SetDisposition` (each arm of item 4, including "`m_bDisableAI` set: the ideal words written,
   the commit not"), `.DieRagdollSeed` (`0x10090180`: bone −1 → the seed; a real bone → no seed;
   the test's text states who reaches bone −1 in retail), each naming its address. Delete
   `NpcCombat.Death`'s port-only assertions (`StartBodyRagdoll → 0`, `HoldBodyFinalPose`, "no
   `PlayNpcClip`") and any test pinning one clip per activity or the hash seed (Grep); list them.

## Not yours

The row's baked data (V4a), the body's speed (V4b), attack producers and slot 247 (C1), the
bbox's data (withdrawn by J2b; filed with the acquire cone's story), the fall, whether a model
has a rig, the drawn body surviving its entity's removal and the burning-death sound (V4d lane
D), `BurnModel`'s look (filed to 0014), the maker (`MakeNPC`: checked, not edited; R6), the four
corpse records (A0 writes them, the C integrator turns them), `ElysiumWeaponClasses.cpp`,
`ElysiumCombatCharacterSlots.cpp`.

Settled by S4, nothing to port: `0x1010e530` (`CreateCorpse`'s first call) fills a vector with
three C-runtime `rand()` draws scaled into `[lo, hi]` and `CreateCorpse` discards it — not the
engine's stream. Unrecovered, not blocking you: what `0x102b5bb0` is reached from (no static
caller, no vtable).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole —
`lifecycle.md` is large: read its sections by line range). Text through Grep / Read / Glob. Do not
commit. Report ≤300 words.

## The owner's ruling, 2026-10-04 — the corpse (stands; folded into item 5)

The corpse's fall is story V4d (`brief-D-ragdoll.md`), not yours. The "hold the `ACT_DIERAGDOLL`
seed pose" stand-in an earlier text of this brief described is withdrawn: the seed runs only for
bone −1 (`0x1009021a`) and `CreateCorpse 0x1032c0e0` always passes a real bone. Your death items
are the doc (item 1: one section), the seed on its one arm (arm-tested only; no record reaches
it), the pedestrian's override wired with `Think`'s gate (item 8), the death fade (item 10), and
the transaction otherwise left as it is.
