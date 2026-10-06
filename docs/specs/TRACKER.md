# Tracker — the layer sequence (2026-10-06)

**What's next = the first unfinished layer, then its first open story.** The order and why:
[`layers/README.md`](layers/README.md). The rules: `AGENTS.md` § Project rules (layer order) and
§ When a problem is reported (missing inputs, upward hooks). What still needs a READ:
[RE-BACKLOG.md](RE-BACKLOG.md).

The previous tracker — 0002's character-AI sequence, steps 1–3 — is
[`0002-npc-ai/tracker-record-2026-10-06.md`](0002-npc-ai/tracker-record-2026-10-06.md); 0002 and
0003–0017 are parked, their open work mapped to layers in `layers/README.md`.

- [ ] **L0 entity** — [`layers/L0-entity/spec.md`](layers/L0-entity/spec.md): 156 stories, 208 briefs in
  83 worker runs (`runs.md`). First: story 0, the test instrument (`layers/harness.md`), and the two
  tooling stories; then the stories in `stories-table.md` order. Witnesses `sp_tutorial_1` + `sm_hub_1`.
- [ ] **L1 animation** — [`layers/L1-animation/spec.md`](layers/L1-animation/spec.md)
- [ ] **L2 character** — [`layers/L2-character/spec.md`](layers/L2-character/spec.md): the combat character with stats, disciplines, feeding; weapons; inventory
- [ ] **L3 player** — [`layers/L3-player/spec.md`](layers/L3-player/spec.md)
- [ ] **L4 NPC + L5 scripting** — [`layers/L4-npc/spec.md`](layers/L4-npc/spec.md); resumes 0002's open work (V7, V10's listener, R1–R8) and 0003 / 0004
- [ ] **Beyond the cores** — [`layers/beyond-core/`](layers/beyond-core/README.md): the shared band, then the tails per hub group (`la`, `hw`, `sm`, `sp`, `ch`)
- [ ] **Acceptance** — the parked specs' witnesses: the tutorial beats (0004–0009), the theatre
  (0010–0011), V8's maps live

Baseline before L0 (branch `layers/l0`, code = `main` `8779bd51`): see `layers/baseline.md`.
