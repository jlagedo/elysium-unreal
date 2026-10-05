# Brief V10-1 — Listen freshness, active traversal and observation

Read AGENTS.md, README and packets-V10 P1/P2 first. Base is landed V4c+V4d+V5b,
not a whole-file copy from the planner snapshot. The coordinator names your worktree.
Find functions by name; all sites below are under repository root.

## Only these four files

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.h`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcSensesTests.cpp`
- `docs/vtmb/npc-ai/senses.md`

A∩B=A∩C=∅; this lane has no V4d/V5b coder-manifest overlap. World, think and
condition-gather taps outside these files are exact integrator owed lines.

## Numbered jobs

1. **Observation-only hunk**, `ElysiumNpcSenses.h::FElysiumNpcSenses` and
   `ElysiumNpcSenses.cpp::TickHearing/PerformSensing`, retail
   **0x1030f940 / 0x1030f7b0 / 0x10310710**: add a const accessor for the actual
   last-listen stamp; trace actual entry and exit old/new stamps, interests,
   identity/revision, candidate insert/expiry and every existing rejection gate.
   A listener with no Pending records must still emit its Listen stamp. Emit a
   sense-enable refusal at PerformSensing; never synthesize an entered Listen.
   Use `World->HasAiTraceSink()/EmitAiTrace` and lane3's agreed text contract,
   with no draw, state mutation, altered ordering or forced clock. Keep this hunk
   separable from item2: first integration build diagnoses old behavior. At insert
   lane3 reads the accessor to capture lastListenAtInsert. Typed rejection reasons
   include mask, freshness, legacy expiry, self/owner, range, occlusion, slot467;
   after repair there is no expiry rejection branch. Keep observations available.

2. **Correction hunk**, `ElysiumNpcSenses.cpp::TickHearing`, retail
   **0x1030f940 / 0x1030f7b0 / 0x101bab50**: enumerate lane2's retained active
   records rather than EventsSince(Cursor); remove only the in-Listen expiry
   predicate. Keep **Event.Time > LastListenTime** exactly (equal time declines),
   interest-mask, owner/self/liveness, sensitivity/radius equality, stealth,
   occlusion and QueryHearSound in their retail order. Admission traversal is
   newest **allocation** first. Build the admitted list by prepending; process
   it oldest admitted first in item3. Refresh does not move a reserved allocation.
   Revisions are event-consumer notifications, not an extra freshness gate.
   Do not invoke QueryHearSound twice, or draw during admission. LastListenTime
   is stamped after OnListened work even with interest0 or no active record.
   Preserve the sensing-disabled gate: no stamp when Listen never ran.
   Existing SoundCursor/StartSoundCursorAtHead may remain for API/archive
   compatibility until V6, but must not limit ordinary Listen's active scan.
   Do not change initial/reset/restore words or serialize the active list.

3. `ElysiumNpcSenses.cpp::TickHearing`, retail **0x1026a5e0 / 0x1026a8a0 /
   0x102cc6c0 / 0x102cc590**: retain existing exact-type mapping, memory selection,
   delay draw for each recognized admitted record in the admitted-list order,
   capacity-before-duplicate check, duplicate min deadline, promotion/output
   sequence and vision-override writes. Same unchanged timestamp on a later
   Listen must neither queue another delay nor consume another random draw.
   A refreshed reserved record with later time is eligible again. Keep WORLD
   and PLAYER paths distinct. This is an access/lifetime repair, not a new
   OnListened/alert/memory implementation. Report a newly found divergence at
   exact function/address instead of opportunistically replacing the chain.

4. `ElysiumNpcSensesTests.cpp`, real **TickHearing/PerformSensing** transaction
   tests, **0x1030f940 / 0x1030f7b0 / 0x1026a5e0**: first listen after finite
   expiry but before grace cleanup admits; equal-time rejects, strictly later
   admits; a second pass with unchanged time draws/queues nothing. Test interest0
   stamping/empty list, disabled senses no stamp, same reserved identity refreshed
   with later time, query-vs-delay order with two types, radius equality and old
   owner/occlusion/slot467 refusals. Use lane2 bus API; tests use recording services
   already present but do **not** edit TestServices.h. Give needed recording
   behavior a local fixture in the owned test file. Migrate only assertions
   pinning the removed expiry/cursor assumptions, name each in your report.
   The arena counterparts are README's grace/freshness/listen-order records;
   arm tests do not replace them.

5. `docs/vtmb/npc-ai/senses.md`, matching **Hearing, walked** and ambient sections,
   **0x1030f7b0 / 0x1030f940 / 0x101ba890 / 0x101ad470**: record the settled
   no-expiry predicate, strict freshness, cleanup equality/grace/recurrence,
   allocation/listen ordering and diagnostic distinction. State N4 cause as
   unmeasured until integration evidence exists. Do not overwrite unrelated
   recovery or claim pool-capacity/other-producer parity. Oracle is retail facts;
   final port/run narrative belongs in triage/commit, not a speculative oracle.

## Dependencies and exact owed lines

Lane2 owns stable allocation identity vs revision, active data, cleanup and the
player refresh. Lane3 owns trace whitelist, diagnostic callback and records.
Agree names by messages/report; never edit their files. Expected integrator lines:

- `ElysiumNpcThink.cpp::NPCThink/Think19NormalSet2` (**0x10292e8e /
  0x102934a9 / 0x1029357c**): trace actual normal-due, AI-due/reduced, disabled
  and next deadlines, read-only. The diagnostic must distinguish no full gather
  from an empty Listen, including bDisableAi returns.
- `ElysiumNpcBaseConditions2.cpp::GatherConditions` (**0x1026ecce /
  0x1026ecf1 / 0x1026ee04**): trace actual state/PVS/spawnflag gate and entry
  to Conditions19PerformSensing. No cadence or gate change is authorized.
- If old `ElysiumFootstepSenseTests.cpp` assertions expect expired PLAYER loss,
  supply exact assertion replacements justified by the reserved-slot contract.
  Do not silently remove those tests or restore tests belonging to V6.

N4 diagnostic excludes deliberate timestamp/sense fixtures. If it observes
equality, retail also rejects it: neither adding grace nor changing freshness
can be advertised as the historical repair. Report that evidence to integrator.

## Coder rules

Write only the four listed files in the worktree the coordinator names, **by
absolute path**. Read files and run read-only tools from
`E:\dev\elysium-unreal`. Never build, run editor/game/tests/arena, bake or commit;
never push. No git clean/reset/stash/checkout/worktree/delete. Address at every
changed runtime line and every numbered job; report a new divergence, do not adopt
it. Never hand-edit generated `*Slots.cpp`; hand bodies belong in matching
`*SlotBodies.cpp`, and the integrator owns `kernel_verdicts.tsv` and regeneration.
Shadowed locals/members/globals are compile errors **C4458 / C4459**. End with a
report **under 350 words**: changed files/functions/addresses, observation vs
correction hunks, read-only checks, assertions changed, and exact lines owed by
files outside the lane (or explicitly none). No file named `report*.md`.
