# 0006 first-disciplines — downstairs in the tutorial the player activates disciplines for the first time

## Witness
The `sp_tutorial_1` downstairs beat: the player activates a discipline offered to the clan
chosen at genesis, holds it through its expiry window, and the game resolves a targeted cast
against a tutorial target; the blood pool and health vitals spend and reflect it. Which
discipline the tutorial offers per clan is read from the level script, not assumed.

## Scope
The activation kernel (activation, blood cost, queue-owned expiry, the targeted cast, teardown)
and every per-discipline consumer the tutorial can reach. Owned elsewhere and consumed here: the
damage spine — **0005**; the `HitInfo` AI-schedule consumer, possession and frenzy — **0002**
(16c); the HUD beyond the two vitals, the disciplines selector UI — unowned; the discipline
viewmodels — **0013**.

## Sources
- Oracle: `docs/vtmb/disciplines.md` (RE41, RE53), `docs/vtmb/npc-ai/authored-control.md`
  § "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload".
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `vdata/disciplinetgt_*`,
  `vdata/` (the discipline tables), `scripts/` (`sp_tutorial_1` level script, the downstairs
  lesson's gates), `maps/sp_tutorial_1.entities.glb`.

## Witness data
- One queue owns activation, the blood-cost debit, expiry and teardown
  (`ClearActiveDisciplines`) for the targeted cast; per-discipline effects join downstream
  consumers (0005's commit, 0002's schedule kernel) rather than each discipline owning its own
  timer or HUD write. The HUD reads `FElysiumViewState` and discrete notifications only.
- Discipline authority/interpreter, activity/witness admission and Elysium/HUD world-area
  authority are closed (RE41), the Bloodbuff/`LockPick` exception with them.
- RE53: Blood Buff's `Min 5` floor, Potence's flat Strength `+1` and Fortitude's single
  re-applied group are retail's authoring; the patch-first corpus scales all three. Default is
  retail.
- A HitGroup's `AI_NPCFlag` is set on apply (`0x101de660`) and cleared on expiry (`0x101def10`,
  with `RemoveFromComfortList` and a schedule teardown); `DoPossession` / `DoFrenzy` are 0002/16c.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The activation kernel.** Activation/deactivation over `vdata/disciplinetgt_*`, blood
  cost, queue-owned expiry, the targeted cast transaction, `ClearActiveDisciplines`. Oracle:
  `disciplines.md`.
- [x] **2. The joins.** Bloodshield/Fortitude/Potence into 0005's commit; the `HitInfo`
  AI-schedule channel into 0002's kernel. Oracle: `disciplines.md`.
- [x] **3. HUD vitals.** Blood pool and health on the stable HUD model (was 8.9).
- [ ] **4. The tutorial's offer.**
  Retail: the downstairs lesson's script gates which discipline it asks for per clan.
  Job: the offer recovered from the level script and recorded; the lesson's gates run on the
  kernel's events.
  Oracle: `disciplines.md` § "The tutorial lesson" (new).
  Size: S. Effort: Sonnet / medium.
- [ ] **5. Celerity.**
  Retail: the native time/movement consumer and its level table.
  Job: the consumer on the player's clock and movement, the table read.
  Oracle: `disciplines.md` § "Celerity".
  Size: M. Effort: Opus / medium.
- [ ] **6. Obfuscate.**
  Retail: the visibility/break/damage-bonus matrix; the cloak/detection-record producers 0002's
  senses read.
  Job: the matrix; the producers wired into 0002/6a's seam.
  Provides: the cloak/detection records to 0002.
  Oracle: `disciplines.md` § "Obfuscate".
  Size: M. Effort: Opus / high.
- [ ] **7. Protean.**
  Retail: the per-rank consumers.
  Job: each rank's consumer.
  Oracle: `disciplines.md` § "Protean".
  Size: M. Effort: Sonnet / high.
- [ ] **8. The frenzy family and `SetScriptedDiscipline`.**
  Retail: the frenzy family's HitGroups and the `SetScriptedDiscipline` pending inputs.
  Job: both, over 0002/16c's `DoFrenzy` / `DoPossession` arms.
  Consumes: 0002/16c.
  Oracle: `disciplines.md`, `npc-ai/authored-control.md` § "Disciplines that possess or
  frenzy an NPC".
  Size: M. Effort: Opus / medium.
- [ ] **9. The client-disable presentation.**
  Job: the shape recovered (none given yet) and ported.
  Size: S. Effort: Sonnet / medium.
- [ ] **10. RE53, the magnitude authoring.**
  Job: the owner's call recorded (default retail) and the three values authored accordingly.
  Oracle: `disciplines.md` (RE53).
  Size: XS. Effort: Haiku / low.

## Seams
- Provides: the activation/expiry/targeted-transaction kernel and the vitals writes to 0005 and
  0002; the cloak/detection records to 0002 (6).
- Consumes: 0005's commit (2); 0002's kernel and 16c (2, 8).
- Open recoveries: the tutorial's per-clan offer (4); the client-disable shape (9).
