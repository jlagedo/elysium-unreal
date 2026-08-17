# LIFE agent playbook — how to drive a session per task

How to call an agent session for each LIFE rung: the session shape, a kickoff prompt to
paste, what the session must read, how it validates, and the scope traps to name up front.
Task specs stay in `docs/project/plans/animation.md`; status stays in
`docs/project/roadmap.md`. This document owns only the working method.

## The standing rules, in every kickoff

1. **One bullet, one session, one commit.** A LIFE rung is a slice; its bullets are the work
   units. Never hand a session a whole rung that has more than one bullet.
2. **Fresh session, tight pointer.** Start clean and name the task: "Work LIFE\<n\>'s
   \<bullet\>. Read `docs/project/plans/animation.md` → LIFE\<n\> first, then the docs it
   names." Do not have one agent "read the task" and brief another — the implementing session
   reads the spec itself.
3. **Plan mode only where the *how* is open.** The split per rung is below. For a
   tightly-specified bullet, plan mode just paraphrases the spec.
4. **Subagents search; the session implements.** Explore-type agents are for fan-out reads
   ("map every caller of X", "survey what the research tooling emits"); they never own a task.
5. **Authorize the loop in the prompt.** Sessions may not start live runs, builds, exports or
   test tiers on their own. Grant the standing loop explicitly:
   *"You may build incrementally, run `uv run elysium run play`, and drive the green room via
   the `elysium.gr_*` verbs over MCP to validate."* Anything wider — a re-export, a re-bake, a
   test tier — the session must propose with exact scope and wait.
6. **Name the export scope when a bake input changes.** One stem, one bundle, one generator.
   A session that wants `export characters` unscoped is asking for a planned operation.
7. **Acceptance is the plan's sentence, shown live.** For LIFE0–LIFE4 that means the green
   room: pose on screen, misses named in the log. A quiet log is not acceptance.
8. **Landing is a doc operation.** When you accept the result, say "land it": the session
   deletes the plan entry, flips the roadmap row, writes durable facts to the owning doc, and
   commits code + docs together.

## Per rung

### LIFE0 — composition seam (two bullets remain, both direct)

Direct sessions, no plan mode; each bullet is nearly mechanical and validated in the green
room. Reads: LIFE0 in the plan, `docs/architecture/animation-architecture.md` §2.1/§4, the
engine gotchas in `Source/ElysiumUE/CLAUDE.md`.

- **Aim-grid neutral.** Adjudication first, fix second: the session must decide runtime
  mapping vs baked sample placement against the recovered axis conventions
  (`docs/vtmb/animation_and_movers.md` A.3) before touching either. If the fix is bake-side,
  the re-bake scope is one bank — make the session name it.
- **Mount/sidecar verification.** A single scoped run of the character bake verifier; cheap,
  read-only. Good first session of a working day.

### LIFE1 — the bake closes (direct sessions; pipeline-side; scope discipline matters most here)

No plan mode needed, but every bullet can tempt a session into a full re-bake — pin the scope
in each prompt.

- **Prop-bone channels.** Two-phase prompt: *"First verify by reading one baked biped base
  clip's track list for the prop-bone chain; report before changing anything."* Then the bake
  change, then re-export **one named stem** and prove the tracks exist. The full-cast re-bake
  is a separate, owner-accepted operation.
- **Cardinality invariant / orphan census.** Exporter-side logic plus a preflight failure
  path; validated by running the planner against the existing corpus, no bake needed.
- **Frame interpolation.** An analysis session, not a code session: it reads the banked
  capture corpus under `$ELYSIUM_WORK_ROOT/research` and answers LINEAR-or-not with numbers.
  Runs fine as a background/research agent while you work something else; its deliverable is a
  finding plus the divergence text if one is needed.
- **`A_dance01` seam.** Instrumented investigation via the existing
  `Instrument.Elysium.DancerDecodeProbe3`; give the session the four named bodies and the
  question as stated in the plan, nothing more.

### LIFE2 — the action catalog (plan mode first, then one session per artifact)

The one rung where design review pays. Session 1 in **plan mode**: read LIFE2, the artifact
table in `docs/architecture/animation-architecture.md` §3.4, and survey (via Explore
subagents) what `research/tooling/probes/*_survey.py` already emit — then propose the
normalization schema, the exporter home, and the bake shape for your approval. After approval,
one direct session per artifact (registry, player rules, NPC rules, weapon tables,
catalog/sidecar extension, coverage join), each with a content-test acceptance. The
trace-comparison acceptance runs against the **banked** corpus; a session claiming it needs a
new capture is proposing an owner call, not blocked.

### LIFE3 — one resolver for the whole cast (direct; two halves)

- **Gait push** is unblocked now: a direct session on `FElysiumAnimationDriver` and the NPC
  motor, validated by a cast member walking a patrol at its authored cell speed (no
  foot-slide) in a real map.
- **Tables + activity→state coverage** waits on LIFE2; then a direct session that deletes the
  seeded table in `ElysiumAnimationIntent.cpp` and proves the selection record names
  state+asset for every locomotion request. Have it use the selection record as its own
  evidence — that is what the record exists for.

### LIFE4 — weapons in hands (plan-mode-light; green-room heavy; needs LIFE1's prop bones)

One short plan-mode pass is worth it only for the check rewrite (what "rendered vs wearer
hand over time" measures and where it runs); the rest is direct. Order inside the rung:
honest check first — *then* the tracking work, so progress is measured by an instrument that
cannot lie, which is the lesson of the current 0.0000-cm check. Kickoff must include the live
green-room authorization; validation is `gr_wield` + `gr_skeleton` + screenshots through
locomotion and a swing, then the equip funnels proven on a real map NPC.

### LIFE5 — reactions and combat actions (plan mode, then one session per family)

Plan mode first: the rung adds several resolver rules and the `UAnimNotify` carrier behind
existing seams, and the gameplay integration contract in `Source/ElysiumUE/CLAUDE.md` governs
every one. After the plan: one session per action family (flinch, knockback/death, blocked,
paired, transitions/restart, the event carrier last with its guard test). Each family's
acceptance sentence is already in the plan — quote it back in the kickoff.

### LIFE6 — first-person viewmodel (plan mode; strictly two halves in order)

Session 1, plan mode: the corpus exporter (the 21+17 manifest, package contract, stale
sweep) — it is pipeline work with a census acceptance, and the plan entry is nearly a spec of
record; the design question is only cache/fingerprint plumbing. Session 2+, after the corpus
verifies: the two-component runtime body, presentation-only acceptance, driven by test
intents — remind the session it must not touch ammo, projectiles or events (13.3's and
LIFE5's). Waits on 11.13d; check that row before starting.

### LIFE7 — cinematic path and gestures (plan mode, owner in the loop)

Two design calls are explicitly this rung's to make **with you** (montage slot vs overlay
treatment for gestures; whether scene clip changes regain a crossfade) — so plan mode, and
expect the session to present options rather than pick. The **layer-weight measurement** is a
separable analysis session over the banked captures (same shape as LIFE1's interpolation
bullet — background-able, numeric deliverable, coverage argument as stated in the plan). The
`Prince_Escort_Male` diagnosis is likewise a self-contained investigation session; give it
the actor-shaped starting point from the plan.

### LIFE8 — played acceptance (you play; agents prepare and repair)

No agent owns this rung. Agent sessions here are: pre-flight (walk the acceptance list in the
plan and verify each beat is demonstrable), the capture-tooling trim (a direct session with
the retained-evidence-path exclusions from the plan), and fix sessions for whatever your
played run surfaces — each of those goes back through the matching rung's session shape
above.

### LIFE9 — parked

Do not start sessions against it. Its revisit is an owner call at the thaw, and its capture
scope is a second owner call on top.
