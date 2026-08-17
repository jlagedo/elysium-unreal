# LIFE2 — the action catalog: the behaviour table as project source

The approved design behind [`animation.md` → LIFE2](animation.md). Status stays in
`docs/project/roadmap.md`; this file carries the specification only.

## Context

The reverse engineering is closed (RE37, `docs/vtmb/animation_and_movers.md` A.3). The weapon
translation tables and the player action rules are committed source, generated from the pinned
binary by `research/tooling/gen_action_tables.py`; their storage form, the resolver shape they
serve and the two test levels that hold them are `docs/architecture/animation-architecture.md`
§3.4 and the landed sources themselves. What every remaining session builds, it builds against
that established form.

What is still only a research instrument is the **NPC** side — the pre-translation and
class-translation bodies, the delegates, the grapple arithmetic and the task routes — and the
**per-model events and transition graph**, which are per-install data no export carries yet. The
runtime meanwhile still resolves activities through a **5-row hand-seeded weapon table**
(`Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp:46-80`); LIFE3 deletes it and
repoints the resolver onto the committed tables.

**The recovered tables are project source, not an export product.** A translation table is a game
*rule*, the same category as the `CGameMovement` constants in `ElysiumMoveSolve.h`, the compiled
slot tables in `ElysiumSheetSlots.h`, and the dice and disposition tables — all of which this repo
already commits. `CLAUDE.md`'s "Bring-your-own-game" rule governs decoder *output* — the export
corpus and the `.uasset` mount — and its `Content/` clause names "bytes, transforms, timing," which
is asset language. Reproducing Source's rules is stated there as faithful work. So the tables are
written down once, reviewed, and maintained; nobody re-derives them, and no build reads
`vampire.dll`.

What still varies per install stays derived: the per-model events and transition graph come from
the user's `.mdl` files, and the coverage report joins the committed tables against the user's own
model corpus.

## Owner calls made

| Question | Decision |
|---|---|
| Where the tables live | **Committed project source.** Generated once from the pinned binary, reviewed as text, maintained by us. |
| The oracle | **Behaviour, not bytes.** The gate is conformance against the exported model vocabulary, not a diff against a DLL decode. |
| Reproduction | **Nobody re-derives.** No pinned hash, no `actions` export bundle, no clean domain, no hard-fail on a mismatched install. |
| Storage form | **Static `constexpr` literal tables**, interned once at subsystem init. Not compile-time expansion. |
| Transition graph | **Its own session** — decode, verify, record the format fact, then emit. |
| The 5-row stub | **LIFE2 lands the tables; LIFE3 deletes the stub** and repoints the resolver. |
| Baking | **Nothing about actions is baked.** Every artifact is committed source or a per-install sidecar the runtime already reads, so `DA_ActionCatalog` and `bake_actions.py` are dropped. |

## 1. Where the remaining artifacts live

| Artifact | Home |
|---|---|
| actor/form + NPC class translation | `Source/ElysiumUE/Private/Visual/ElysiumNpcActivityTables.cpp` (generated, committed) |
| per-model events | `npc/blends/<stem>.json`, new `"events"` block |
| per-model transition graph | `npc/blends/<stem>.json`, new `"transitions"` block |
| coverage report | `$ELYSIUM_WORK_ROOT` QA report, not an export-corpus artifact |

The generated `.cpp` holds its arrays in an anonymous namespace behind accessors declared in
`Private/Visual/ElysiumActionTables.h`, beside the two already there. The generator is
`research/tooling/gen_action_tables.py`, run as `uv run elysium research gen_action_tables`, and
it is **owner-run archaeology, not part of any build**; `--only` scopes it to one artifact and
`--check` re-derives without writing.

## 2. The character-catalog extension

Both payloads land in the existing **per-owning-model** sidecar, `npc/blends/<stem>.json`, which
`write_blends` already writes for NPCs, banks and animated props from the same 764-byte descriptor.

```json
"event_fields":["cycle","event","type","options_i"],
"event_options":["footstep_l", "..."],
"events":{"<label>":[[0.35,1004,0,3], ...]},
"transitions":{"nodes":12, "sequence_nodes":{"<label>":[entry,exit,nodeflags]}, "matrix":[[...]]}
```

Per-owning-model rather than `npc/clips/<stem>.json`, because the clips sidecar resolves labels
*through* banks — a bank clip's events would be duplicated across every character resolving it.
`_clip_meta` makes exactly this argument for the selection keys: "stored once per owning stem, not
per NPC that resolves it." The runtime already has the owner in hand (`Clip->Owner`) and reaches
owner-scoped data through `BlendTableFor`, so the same callback shape serves events.

Do not widen the 7-element clip row. Bump `npc_export.MANIFEST_VERSION` 7 → 8; add no version to
the per-stem sidecars. `npc_index.json` gains an `event_sequences` count beside `blend_grids`.

**Autolayers already ship** in that file. LIFE2's work there is an assertion against the recovered
census — entry order semantic, depth 1, fan-out ≤ 2, only the two `move_and_ranged` banks carrying
any, the one inverted host — not a re-export.

### The transition graph is new MDL decode

`mdl_skel.py` reads sequence-descriptor offsets 0/4/8/12/16/20/24/28/40/52/56/572/580/588/596/612/
660/664 and nothing node-shaped. `animation_and_movers.md:958-964` records the behaviour
(`AdvanceToIdealActivity` asks for an intermediate sequence) but no layout that answers it. A.4c is
a **different** mechanism — the client-side crossfade transitioner over the @612 duration triple —
and must not be conflated with the node graph.

Scope: per-sequence entry node, exit node and node flags; and a model-level
`numlocalnodes`/`localnodeindex` pair addressing an `n × n` byte adjacency matrix. The unclaimed
regions of the 764-byte descriptor are **@600-608**, **@616-656** and **@668-763**; HL1's
`mstudioseqdesc_t` carried `entrynode, exitnode, nodeflags, nextseq` as four consecutive ints.
`research/tooling/probes/studiohdr_unclaimed_fields.py` is the instrument.

**Acceptance before a byte is emitted:** every entry/exit node in `[0, n)` across all 4,445 v2531
models; `n` equals `max(node)+1`; on a model carrying `*_to_*` sequences the matrix's non-trivial
cells name exactly those sequences; and the lookup reached from `0x102726a0` is confirmed to read
those offsets. The finding belongs in `docs/vtmb/animation_and_movers.md`, never in a `CLAUDE.md`.

## 3. Verification still owed

The two committed test levels — content-free round trip and well-formedness, then behavioural
conformance against the export corpus — are the shape each remaining table inherits;
`Private/Tests/ElysiumActionTableTests.cpp` is the worked example. Beyond them:

**Retail agreement** — owner-run, gates nothing. `research/tooling/probes/action_trace_join.py`
joins the banked corpus's 1,232 observed NPC translation resolutions against the committed tables,
on the full serial-bearing entity handle plus model identity (entity index alone is reusable and is
not an identity key). Its numbers are quoted as acceptance evidence the way "180,812 agreeing / 0
disagreeing" already appears in `animation_and_movers.md`. It reads machine-local research evidence,
so it is never a Content test. A disagreement the banked corpus cannot settle escalates to a scoped
new capture as an owner call.

`action_coverage` is a **QA report**, not runtime data and not an export-corpus artifact: the
reachable rule → translation → sequence closure per body and NPC class, grouped by family, with
every unresolved row named with its provenance. The conformance tests already name their residual;
what the report adds is the per-body and per-NPC-class closure behind it.

## 4. The sessions

One bullet and one commit each.

| # | Session | Touches | Deps |
|---|---|---|---|
| **S3** | **NPC rules and class translation as source** — 10 pre-translation + 5 class-translation bodies, 2+2 delegates, grapple as 29 bases plus the `+1…+8` arithmetic, 111 task routes | generator, `ElysiumNpcActivityTables.cpp`✚, tests | — |
| **S4** | **Catalog: events + autolayer census** | `exporters/npc_export.py`, `Private/Visual/ElysiumBlendGrids.{h,cpp}`, tests | — |
| **S5** | **Catalog: transition graph** — decode, verify, emit, write the VtMB doc | `formats/mdl_skel.py`, `npc_export.py`, `animation_and_movers.md` | S4 |

**S4 is parallel with S3**: different source, different files.

### Acceptance sentences, quoted back in each kickoff

- **S3** — *77 descendants collapse to 10 pre-translation, 5 class-translation and 2+2 delegate
  bodies; 111 task routes over 100 policy rows with zero custom exact-label routes; the 232 grapple
  variants generated from 29 bases rather than enumerated.*
- **S4** — *for the selected stems every clip carrying events in the MDL carries them in
  `npc/blends/<stem>.json` with cycle in [0,1] and `type == 0`, matching `npc_index.json`'s rollup;
  the autolayer census still reads `{0:13544, 1:237, 2:224}` with the one inverted host named.*
- **S5** — *every decoded entry/exit node lies in `[0, n)` across all 4,445 v2531 models, `n` equals
  `max(node)+1`, a `*_to_*`-carrying model's matrix names exactly those sequences, and the lookup
  reached from `0x102726a0` reads those offsets; the layout is recorded in `animation_and_movers.md`.*

### Verification per session

Narrowest first — no broad profile, no `--force`, no unscoped tier.

- Python unit: `uv run python -m unittest pipeline.tests.<module>` for the one module touched.
- Export: `uv run elysium export characters --only <stem>` (S4, S5), stems named in the kickoff.
- Runtime: the one focused filter the session adds. Never the whole `Content` tier as routine
  validation.
- Build: where a session touched C++ (S3) — state the scope, get owner acceptance, then
  `uv run elysium build`.
- Owner-run corroboration, reporting only: `uv run elysium research action_trace_join`.

## 5. Risks

1. **The transition-graph offsets may not be there.** VtMB v2531 is an odd internal fork; if the
   node fields were stripped, S5 has nothing to decode. Default: **S5 does not block LIFE2** — the
   sidecar carries no `"transitions"` block and LIFE5 inherits the dependency. Settle before S5
   starts, not after it stalls.
2. **Weapon class → runtime tag.** The runtime keys by `WeaponTag` (`"glock"`), the tables by C++
   class and entity classname. A class with no derivable tag is unreachable from the runtime, which
   would mean the runtime should key by entity classname — a change to
   `FElysiumAnimationIntent.WeaponTag`'s meaning that reaches into LIFE4.
3. **`MANIFEST_VERSION` 7 → 8.** `npc_export.py:949` refuses cross-version integration, so anything
   holding a v7 manifest in a work root needs a re-export. If a lane or cached bake plan pins v7,
   S4 is wider than it looks.
4. **The tables become ours to maintain.** A wrong row is now a bug we fix, with the RE record as
   its rationale rather than a binary to re-diff. That is the intended posture, and it means the
   conformance tests are the only thing standing between a bad edit and a silently wrong pose.
