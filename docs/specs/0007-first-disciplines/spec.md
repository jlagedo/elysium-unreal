# 0007 first-disciplines — downstairs in the tutorial the player activates disciplines for the first time

## Witness
The `sp_tutorial_1` downstairs beat: the player activates a discipline offered to the clan chosen
at genesis, holds it through its expiry window, and the game resolves a targeted cast against a
tutorial target. Proof is the queue-owned activation/expiry/teardown transaction plus the HUD
vitals it spends and reflects (blood pool, health) driven from real input on the tutorial map
slice, backed by the discipline test suite (`ElysiumDisciplineTests.cpp`). Which discipline(s)
the tutorial offers per clan is unspecified in the source plan.

## Scope
- Roadmap rows absorbed: 13.2 (Disciplines); 8.9 (HUD on the UI foundation) — vitals only (blood
  pool, health) as read/written by discipline activation and cost.
- Out of scope (belongs to another spec or is parked): the rest of 8.9 — reticle/use icons,
  Masquerade/Humanity standings, fade, cutscene suppression, contrast treatments, queued
  item/quest notifications, equipment/inventory selectors, the stealth cluster's slot, stance
  trigger and its two producers, the disciplines *selector* UI, subtitles, and the worn-slot
  armour portrait; 13.1 Stealth (stealth-kill transaction, deaf arc, grapple mode 3); 13.3
  Firearms & melee (word-15 commit, spread cone interpolation, melee contact instant,
  `SkillRequirement`); 13.5 Combat AI (the `HitInfo` AI-schedule channel's consumer kernel,
  flinch); frenzy family and `SetScriptedDiscipline` pending inputs; the client-disable
  presentation; RE53's magnitude-authoring owner call (Blood Buff `Min 5` floor vs. patch scaling)
  is referenced but not resolved here.

## Requirements

### (a) The activation kernel
1. Activation/deactivation over `vdata/disciplinetgt_*` — built. `docs/vtmb/disciplines.md`.
2. Blood cost charged against the blood-pool vital on activation. `docs/vtmb/disciplines.md`.
3. Queue-owned expiry — the active discipline's duration is tracked and torn down by the same
   queue that owns activation, not an ad hoc timer. `docs/vtmb/disciplines.md`.
4. The targeted cast transaction — a discipline aimed at a target resolves through one
   transaction. `docs/vtmb/disciplines.md`.
5. `ClearActiveDisciplines` teardown — built. `docs/vtmb/disciplines.md`.
6. Discipline authority/interpreter, activity/witness admission, and Elysium/HUD world-area
   authority closed; the Bloodbuff/`LockPick` exception closed. `docs/vtmb/disciplines.md`; RE41.
7. RE53 (open owner call): the discipline-magnitude retail/patch delta — Blood Buff's `Min 5`
   floor, Potence's flat Strength `+1`, and Fortitude's single re-applied group are retail's
   authoring; the patch-first corpus scales all three instead. Which a remake reproduces is an
   open owner call; default is retail. `docs/vtmb/disciplines.md`; RE53.
8. HUD vitals the kernel reads and writes: blood pool (cost debit, display) and health (effects
   that modify it). The HUD reads `FElysiumViewState` and discrete notifications from the
   publisher only; PP4 owns the snapshot consumer and cleared/absent state. Region rules:

### (b) Per-discipline effects
9. The Bloodshield/Fortitude/Potence joins into 13.3's damage/attack commit — built.
   `docs/vtmb/disciplines.md`.
10. The `HitInfo` AI-schedule channel into 13.5's kernel — built (the kernel-side consumer is
    13.5's, out of scope here). `docs/vtmb/disciplines.md`.
11. Open: Celerity's native time/movement consumer and its level table. `docs/vtmb/disciplines.md`.
12. Open: Obfuscate's visibility/break/damage-bonus matrix. `docs/vtmb/disciplines.md`.
13. Open: Protean's per-rank consumers. `docs/vtmb/disciplines.md`.
14. Which discipline(s) the tutorial offers to the clan chosen at genesis: unspecified.

## Design
The port's shape as already decided: one queue owns activation, blood-cost debit, expiry and
teardown (`ClearActiveDisciplines`) for the targeted cast transaction; per-discipline effects join
downstream consumers (13.3's damage/attack commit, 13.5's AI-schedule kernel) rather than each
discipline owning its own timer or its own HUD write. The HUD is a pure reader of
`FElysiumViewState` and discrete notifications — no discipline logic queries the HUD, and the HUD
carries no perception of its own.

## Seams
- Consumes: 13.3's damage/attack commit (for Bloodshield/Fortitude/Potence joins); 13.5's
  AI-schedule kernel (for the `HitInfo` channel); 8.6's UI foundation (`FElysiumViewState`
  publisher) — spec unknown.
- Provides: the activation/expiry/targeted-transaction kernel and the blood-pool/health vitals
  writes that 13.3, 13.5 and any later discipline-consumer spec build on; the disciplines
  *selector* UI remains unowned and stays invalid rather than fabricating rows (belongs to a
  later HUD spec).

## Tasks
- [x] Activation/deactivation over `vdata/disciplinetgt_*`
- [x] Blood cost
- [x] Queue-owned expiry
- [x] The targeted cast transaction
- [x] Bloodshield/Fortitude/Potence joins into 13.3's commit
- [x] `HitInfo` AI-schedule channel into 13.5's kernel
- [x] `ClearActiveDisciplines` teardown
- [ ] The played discipline lesson (tutorial, `sp_tutorial_1` downstairs beat)
- [ ] Celerity's native time/movement consumer and level table
- [ ] Obfuscate's visibility/break/damage-bonus matrix
- [ ] Protean's per-rank consumers
- [ ] Frenzy family and `SetScriptedDiscipline` pending inputs
- [ ] Client-disable presentation
- [ ] RE53: owner call on discipline-magnitude authoring (retail vs. patch scaling)
- [x] HUD vitals (blood pool, health) landed as part of 8.9's stable HUD model

## Open questions
- RE53: which discipline-magnitude authoring a remake reproduces (retail's per-discipline values
  vs. the patch-first corpus's uniform scaling) — owner call, default retail.
- Which discipline(s) the tutorial offers per clan chosen at genesis — not stated in the plan.
- The client-disable presentation for disciplines — no shape given yet.
