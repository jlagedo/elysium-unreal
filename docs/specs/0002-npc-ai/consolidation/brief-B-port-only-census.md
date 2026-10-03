# Brief B — port-only mechanisms and state-changing "modernizations" in the substrate

Scope: `Source/ElysiumUE/Private/Substrate/` (640 files, 219k lines) and `Source/ElysiumUE/Public/ElysiumNpc*.h`.
Do NOT read whole files; grep, then read the 20–60 lines around each hit.

Deliver `findings-B-port-only.tsv` (rows: mechanism | files:lines | retail counterpart (address or
"none") | changes state or event order? (yes/no + one line) | consumers (count) | tests pinning it |
retire / keep / rename) and `findings-B-port-only.md` (≤2 pages). Cover:

1. The body arbiter: `EElysiumBodyOwner`, `FElysiumNpcMind::Acquire/Release/Suspend`, every claim
   site (`AcquireScheduleBody`, `AcquireProgramBody`, `Sequence`, `Dialogue`, `Ambient`,
   `ScriptedSchedule`, `Follower`), every gate that reads `Mind.Owner()` (`PlaySequenceClip`, the
   stance transition, `SaveBlockReason`, `SerializeMindBlock`, `ReleaseProgramBody`'s `Motor->Stop`),
   and what retail does instead at each site (`m_scriptState`, `m_hCine`, `IsInAScript`,
   `SCHED_SCRIPTED_*`, `ClearSchedule`; cite `docs/vtmb/npc-ai/*.md` sections where they exist).
2. The executors that are not kernel programs: `ThinkAmbient`, `ThinkPatrol` (if still present),
   `ThinkAutonomous` routing, `bReturnToExternalExecutorAfterSchedule`, `ThinkSchedulePolicy`,
   `FailedSpotIndices`, `bMoveIssued` / `bWalkingAnimation`, `ScheduleHost` stand-in words.
3. Every comment containing `modernization` or `MODERNIZATION`: file:line, one line what it swaps,
   and your judgement: visual-only (keeps retail contract and event order) or state-changing (must
   be re-classed as a divergence or retired). Same for `divergence` / `DIVERGENCE` comments.
4. Every `SEAM` comment whose body answers nothing / false / INDEX_NONE / 0: count by family
   (senses, navigator, sounds, memory, hints, anim, squad, script, ...), and the 20 most consequential
   (a seam a live schedule on `sp_tutorial_1` or `sm_hub_1` reaches — use
   `docs/vtmb/npc-kernel/reach/*.md` if present).
5. `STORY8-TWIN` markers (count, files) and `ElysiumStub::Fired` / `FireAnimatingSlot` /
   `FireAnimatingOverlaySlot` tally sites: which are on a live path.
6. The interface layer: list the interfaces between the kernel and Unreal (`IElysiumEmbodiment`,
   `IElysiumNpcMotor`, geometry services, sound, trace) with method counts, and the places where
   kernel code calls Unreal (`UWorld`, `AActor`, `UAnimInstance`, `FCollisionQueryParams`,
   `GetWorld()`) DIRECTLY bypassing an interface (grep in `Substrate/`; list file:line).

Report ≤300 words.
