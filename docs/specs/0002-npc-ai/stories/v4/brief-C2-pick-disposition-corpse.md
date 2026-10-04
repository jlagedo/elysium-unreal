# Brief C2 — V4c: the death chain's doc, the weighted pick, `SetDisposition`, the `ACT_DIERAGDOLL` seed (coder; no build)

**Final (amended after V4r, 2026-10-04 — R2, J8, K4).** Read `README.md` here (§1 "The frame"
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

- `docs/vtmb/npc-ai/lifecycle.md` (§ "The ordered chain" steps 5–6 and § "A death sounds more than
  once" only)
- `ElysiumNpcAnim.cpp` (`SequenceForActivity` ~:375-391, the row cache, and `SequenceBounds`'s
  body) and `ElysiumNpc.h` (the declarations)
- `ElysiumNpcBaseAnim.cpp` (`RunAnimation`'s idle re-pick ~:168-182 only)
- `ElysiumNpcBaseStartTask.cpp` (`StartTaskSlot442`'s `SelectWeightedSequence` ~:399-400 only)
- `ElysiumNpc.cpp` (`FElysiumNpc::SetDisposition` ~:1107-1177, `BecomeClientRagdoll` ~:285-311,
  `PlayActivity`'s variant ~:1124, `StartWalkingAnimation`'s variant ~:529 — these only)
- K4: `Source/ElysiumUE/Private/Visual/ElysiumAnimationResolve.cpp` (`PickWeighted` ~:703-748,
  `TryActivity` ~:187), `ElysiumCombatCharacter.cpp` (`PlayReactionActivity` ~:2025 only),
  `Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp` (the pick's variant ~:538 only),
  `ElysiumProp.cpp` (the random animator's pick ~:136 only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp` (`NpcCombat.Death` ~:1629-1661)

**Not** `ElysiumWeaponClasses.cpp` (C1's whole): K4's line there
(`FElysiumWeapon::BuildActivityClipRequest` ~:1077, `Variant = Handle.Index`) you write **in your
report**, exact, and the integrator applies it.

## The job

1. **First, walk `CreateCorpse`'s arms and fix `lifecycle.md`** (J8) — before any death code.
   What a fresh read found, for you to verify in the listing and then write down:
   `CreateCorpse 0x1032c0e0` at `0x1032c404` pushes `(0x10009c9b, 0, 0)` into the think setter
   and takes `curtime + _DAT_1044e664` (10.0) — it **replaces the NPC's think** with
   `SUB_PVSRemove` at +10 s — and it does so **on every ordinary arm**: the branch at
   `0x1032c400` is the only skip. So **an ordinary kill never reaches `SCHED_DIE`, rig or no
   rig**; and the state-7 fork's `BecomeClientRagdoll(vec3_origin, -1, 0)` at `0x1028a8ec` — the
   only caller that passes bone −1 and so the only one that reaches the `ACT_DIERAGDOLL` seed
   (`0x1009021a`) — **is reached only by other state-7 writers**. Walk each arm of `CreateCorpse`
   (the player → `SpawnStaticCorpse`; `MiscFlag 0x80000` → `SpawnStaticCorpse`, `Hide`, removal at
   +0.5 s; otherwise `BecomeClientRagdoll(force, bone, 0)` with the hit bone or
   `LookupBone("Bip01 Spine2")`) and say for each whether it passes through `0x1032c404`. Then:
   - rewrite § "The ordered chain" steps 5–6 as the listing has them, dated, "V4c lane C2";
   - rewrite § "A death sounds more than once": its count depends on a second `Event_Killed`
     that the replaced think may never run — state the count the corrected chain gives, or
     "unrecovered" with the address if the second caller is not found;
   - **which writers put an NPC in state 7 without `CreateCorpse`'s think replacement is
     unrecovered; the coder reads it first** (`0x1028a8ec`; `research where 0x1028a8ec`,
     `m_NPCState`'s writers in `fields.md` through `research where`) and lists them in the doc.
   If your walk contradicts the fresh read on any point, the listing wins: write what you read,
   say so at the top of your report, and port item 5 as the listing has it.
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
   leaving `StartBodyRagdoll`'s answer as it is until V4d gives the mesh a physics asset. If the
   port has no caller that passes bone −1 (the state-7 fork), the seed's arm is ported and
   reached by the arm test only: say so, and say who retail's callers are (item 1).
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
   `bool FElysiumNpc::SequenceBounds(int32 Seq, FVector& OutMinCm, FVector& OutMaxCm) const` —
   the row's clip's `BboxMinCm` / `BboxMaxCm` (C1 adds the fields to `FElysiumNpcClip`); false
   for "no descriptor": no row, no clip, or a clip loaded without a bbox. C1's slot 247 calls it.
8. **Tests**: `Elysium.Arm.NpcKernelAnim.WeightedPick`, `.HeaviestPick`, `.RunAnimationPick` (R2's
   gate: `m_Activity == 1`, the pick on `m_TranslatedActivity`, the loop-bit fork),
   `.SetDisposition` (each arm of item 4, including "`m_bDisableAI` set: the ideal words written,
   the commit not"), `.DieRagdollSeed` (`0x10090180`: bone −1 → the seed; a real bone → no seed;
   the test's text states who reaches bone −1 in retail), each naming its address. Delete
   `NpcCombat.Death`'s port-only assertions (`StartBodyRagdoll → 0`, `HoldBodyFinalPose`, "no
   `PlayNpcClip`") and any test pinning one clip per activity or the hash seed (Grep); list them.

## Not yours

The row's baked data (V4a), the body's speed (V4b), attack producers, the bbox's data and slot 247
(C1), the fall and whether a model has a rig (V4d), `ElysiumWeaponClasses.cpp`.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole —
`lifecycle.md` is large: read its sections by line range). Text through Grep / Read / Glob. Do not
commit. Report ≤300 words.

## The owner's ruling, 2026-10-04 — the corpse (stands; folded into item 5)

The corpse's fall is story V4d (`brief-D-ragdoll.md`), not yours. The "hold the `ACT_DIERAGDOLL`
seed pose" stand-in an earlier text of this brief described is withdrawn: the seed runs only for
bone −1 (`0x1009021a`) and `CreateCorpse 0x1032c0e0` always passes a real bone. Your death items
are the doc (item 1), the seed on its one arm, and the transaction left as it is.
