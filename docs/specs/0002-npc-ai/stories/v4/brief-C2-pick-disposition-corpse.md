# Brief C2 — V4c: the weighted pick, `SetDisposition`, the `ACT_DIERAGDOLL` seed (coder; no build)

Read `README.md` here (§1 "The weighted pick", "The disposition change", "Death"; §2 M8, M9, M12;
§7 K4, K5), `packets.md` § R2 item 7, the judge's ruling on Q3, `docs/vtmb/activity_enum.md`
:294-337, `docs/vtmb/npc-ai/lifecycle.md` :2705-2728, `stories/v1/divergences.md` row 4. After V4b's
commit. V3c filled `LookupSequenceByName` — use it. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (`SequenceForActivity` ~:375-391 and the
  row cache)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp` (`RunAnimation`'s idle re-pick
  ~:168-182)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp` (`StartTaskSlot442`'s
  `SelectWeightedSequence` ~:399-400 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp` (`FElysiumNpc::SetDisposition` ~:1107-1177,
  `FElysiumNpc::BecomeClientRagdoll` ~:285-311 only) and `ElysiumNpc.h` for their declarations
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp` (`NpcCombat.Death` ~:1629-1661)

## The job

1. **`SelectWeightedSequence 0x1008dc40` on the kernel** (`FElysiumNpc::SelectWeightedSequence(int32
   Activity)`): gather the body's sequences whose baked activity matches (the sequence table behind
   `UElysiumBodyData::Sequences` / `FElysiumNpcClipSet`, with the include-shadowing rule
   `activity_enum.md` :328-337 as far as the table carries the include groups — say what it does not
   carry), then the draw: none → −1, one → it, `RandomInt(0, total−1)` on the **`NpcSchedule`** stream
   walked by `weights[i] <= r`, all-zero → uniform. `SelectHeaviestSequence 0x1008dd30`: strict max,
   first wins. The answer is a **row** the bridge plays (`m_nSequence`), so every candidate becomes a
   row; the one-clip-per-activity cache goes. Close divergence row 4 at its line.
2. **The callers**: `SequenceForActivity` draws through it; `StartTaskSlot442` (its
   `SelectWeightedSequence(act, -1)`); `RunAnimation`'s re-pick — `m_bSequenceLoops` false →
   `SelectHeaviestSequence(m_Activity)`, true → `SelectWeightedSequence(m_Activity)`, commit through
   the port of `0x10260a50` when not −1 (`0x1026c540`, read in README §1).
3. **`SetDisposition 0x102c0f70`** as R2 item 7 walks it: the `m_bDisableAI` gate, the writes in
   retail's order (`m_IdealActivity = 0xf1`, `m_nIdealSequence +0x5ccc` from
   `GetTransitionAnim 0x100ed150` resolved by `LookupSequence`, then its fallbacks, `ResetSequenceInfo`)
   — no direct `PlayNpcClip`, no `ResetAnimToIdle`. Keep `CommitDisposition` (the table row) as it is.
   The `IsFeedBusy()` term: keep it only if R2 finds it in the listing; otherwise remove it and say so.
4. **`BecomeClientRagdoll 0x10090180`'s seed**: before the handoff, `SelectWeightedSequence(ACT_DIERAGDOLL
   0x21)` → `m_nSequence`, `m_flCycle = 0`, `ResetSequenceInfo` (`lifecycle.md` :2713-2715); then
   the existing handoff (`StartBodyRagdoll`, else `HoldBodyFinalPose` — K5 until 0014). A model with
   no `ACT_DIERAGDOLL` sequence: −1, no seed, as retail. The rest as the judge ruled on Q3.
5. **Tests**: `Elysium.Arm.NpcKernelAnim.WeightedPick`, `.HeaviestPick`, `.RunAnimationPick`,
   `.SetDisposition`, `.DieRagdollSeed` (README §6), each naming its address. Delete
   `NpcCombat.Death`'s port-only assertions (`StartBodyRagdoll → 0`, `HoldBodyFinalPose`, "no
   `PlayNpcClip`") and any test pinning one clip per activity (Grep); list them.

## Not yours

The row's baked data (V4a), the body's speed (V4b), attack producers (C1), the fall (0014).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.

## Changed by the owner's ruling, 2026-10-04 — the corpse

The corpse's fall is story V4d (`brief-D-ragdoll.md`), not yours. Do not write the "hold the
`ACT_DIERAGDOLL` seed pose" stand-in this brief describes: that seed runs only for bone -1
(`0x1009021a`) and `CreateCorpse 0x1032c0e0` always passes a real bone. Your death item is the
transaction only — slot 144 → `CreateCorpse`, `BecomeClientRagdoll(force, bone, 0)` with the hit
bone or `LookupBone("Bip01 Spine2")`, the force latched only for |F| > 0 and bone > 0, the entity
made not solid with its think cleared, the +10 s `SUB_PVSRemove` — leaving `StartBodyRagdoll`'s
answer as it is until V4d gives the mesh a physics asset.
