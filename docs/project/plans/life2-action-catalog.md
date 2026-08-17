# LIFE2 — the action catalog: the behaviour table as project source

The approved design behind [`animation.md` → LIFE2](animation.md). Status stays in
`docs/project/roadmap.md`; this file carries the specification only.

## Context

The reverse engineering is closed (RE37, `docs/vtmb/animation_and_movers.md` A.3), but its output
lives only as research instruments and hand-run reports under `$ELYSIUM_WORK_ROOT/research`.
Nothing the game loads is derived from it. The runtime resolves activities through a **5-row
hand-seeded weapon table** (`Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp:46-80`)
against 9,214 recovered rows over 169 weapon classes. LIFE3's real translation tables, LIFE5's
transition traversal and every rung's activity→state coverage wait on that data landing.

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
| Storage form | **Static `constexpr` literal tables**, interned once at subsystem init. Not compile-time expansion — see §3. |
| Transition graph | **Its own session** — decode, verify, record the format fact, then emit. |
| The 5-row stub | **LIFE2 lands the tables; LIFE3 deletes the stub** and repoints the resolver. |

## The measurement this rests on

Run against the pinned `vampire.dll` and the 166-body export corpus. These numbers are the
evidence; a session that changes the model re-runs them.

**The table is a product, and it round-trips.** The 9,214 rows are ladder-major:

```
for family in <this weapon's ladder>:          # 110 blocks, avg 1.8 per weapon
    for base in <one of 18 shared sequences>:  # 792 entries total
        emit  base -> rewrite(base, family)
```

Regenerating from the stored model alone reproduces **61/61 weapon classes, all 9,214 rows, in the
recovered walk order, `required` column included**. Blocks are found structurally — a block ends
when a base repeats — so the shape is a finding, not an assumption. It is also what makes retail's
front-to-back `ActivityOverride` walk work: block 1 is the weapon's own animation set, block 2 the
shared class set, block 3 a cousin weapon.

| Stored | Count |
|---|---:|
| base sequences (18) | 792 entries |
| block headers (weapon, sequence, family) | 110 |
| exception rows | 595 |
| `required` flags | 57 |
| rename rules | 3 |
| substitute bases | 8 |
| **total stored units** | **1,565 — 17.0% of 9,214 rows** |

`required` is a property of the base-sequence *entry* rather than of the block: every block walking
a given sequence agrees on it across all 61 classes, so the 201 flagged rows store as 57 flags. The
block's family is fitted as the token leaving the fewest literals, deterministically on a tie, and
the rows it does not produce are the exception list.

**The decode is validated by the content.** Walking all 61 ladders against the corpus vocabulary
gives 4,580 (weapon, base) requests, of which 1,739 resolve. A request's rung is counted over the
rungs that *declare* that base, not over every block: a base absent from block 1 is not a rung the
walk skipped, it is a rung the walk never had.

- **38% of all resolutions come from rung 2 or later** (1,082 at rung 1, 574 at rung 2, 75 at rung
  3, 8 at rung 4). The fallback order is load-bearing and behaves as read; flattened to one row per
  base, 657 resolutions would become misses.
- `_BACK` is a family slot in one family and a literal direction in the other, and the table gets
  both right: the eight `ACT_SNEAKATTACK_*_BACK` bases **substitute**, and their targets resolve 48
  times against the corpus; the five `ACT_KNOCKBACK_*_BACK` bases **append**, and the round trip is
  what proves that reading rather than a resolution count — neither reading resolves for knockback,
  because the corpus carries no weapon-decorated knockback direction at all.
- No rewrite kind is dead: `append` answers 1,418 requests, `exception` 212, `rename` 49,
  `substitute` 48 and `identity` 12. A misdecoded rule shows as a kind at ~0%.

**The vocabulary match folds case, and that is not cosmetic.** 91 of the corpus's 1,246 activity
literals are not upper case while the DLL registers every name upper case, so a case-sensitive walk
resolves 1,511 instead of 1,739 and five weapons lose their whole alert-transition set. The
measurement above is the case-folded one; `docs/vtmb/animation_and_movers.md` A.3 carries the
finding and marks what is still unverified in the binary.

**Two families sit at exactly 0%, both accounted:** viewmodel `ACT_VM_*` (417 requests — the NPC
corpus has no viewmodel bodies; that is LIFE6's 21+17 corpus), and directional walk/run (222
requests — `ACT_WALK_45` exists in no shipped model even unsuffixed). Both are content facts, and
both are the "an unresolved row stays named with its provenance" case.

**The residual is content, not a misdecoded rule.** The other 2,202 never-resolving requests spread
across some thirty activity families — knockback 775, alert 360, the turn/180/90 trio 444 — with no
rewrite kind falling below 11% candidate availability, which is the signature that separates absent
content from a wrong rule. 1,232 of them carry their bare base in the corpus and no
weapon-decorated form, which is the shared bank holding `ACT_WALK` and no `ACT_WALK_ANACONDA`.
`Elysium.Content.ActionTableConformance` prints the full named list; grouping it by family is
`action_coverage`'s job.

## 1. Where the tables live

| Artifact | Home |
|---|---|
| weapon activity translation | `Source/ElysiumUE/Private/Visual/ElysiumWeaponActivityTables.cpp` (generated, committed) |
| actor/form + NPC class translation | `ElysiumNpcActivityTables.cpp` (generated, committed) |
| player action rules | `ElysiumPlayerActionRules.cpp` (generated, committed) |
| per-model events | `npc/blends/<stem>.json`, new `"events"` block |
| per-model transition graph | `npc/blends/<stem>.json`, new `"transitions"` block |
| coverage report | `$ELYSIUM_WORK_ROOT` QA report, not an export-corpus artifact |

Each generated `.cpp` holds its arrays in an anonymous namespace behind a small accessor declared
in `Private/Visual/ElysiumActionTables.h` — the shape `ElysiumGymSpec` already uses (`Public/*.h`
declarations, `Private/*.cpp` data), and the shape the 5-row stub already has inside
`ElysiumAnimationIntent.cpp`. No `.inl`: the repo has none, and UnrealBuildTool compiles a `.cpp`
under `Private/` without anyone having to remember to include it.

The generator is `research/tooling/gen_action_tables.py`, run as
`uv run elysium research gen_action_tables`. It reads the pinned binary through the existing
probes, emits the headers, and is **owner-run archaeology, not part of any build**.

The activity registry is **not committed**. The runtime keys on names and never on IDs, and 4,460
registrations — most unreachable in gameplay — is a binary dump rather than a rule. It stays a
research artifact; the reachable names appear in the tables that use them.

## 2. The committed form

```cpp
// ElysiumWeaponActivityTables.cpp — generated by `elysium research gen_action_tables`.
// Do not hand-edit; regenerate and let the round-trip test prove it.
namespace
{
    // 18 shared ordered base sequences, 792 entries, with the authored `required` positions
    // beside each: the flag belongs to the entry, and every block walking the sequence agrees.
    constexpr const TCHAR* Bases_111_A[] = { TEXT("ACT_RUN"), TEXT("ACT_WALK"), ... };
    constexpr int32 Bases_111_A_Required[] = { 62, 63 };

    // One block per ladder rung: which base sequence, which animation family.
    constexpr FActionBlock CWeaponMelee_Katana_Blocks[] = {
        { Bases_111_A, 111, Bases_111_A_Required, 2, TEXT("KATANA") },
        { Bases_111_A, 111, Bases_111_A_Required, 2, TEXT("MELEESHARED_ONEHAND") },
        { Bases_111_A, 111, Bases_111_A_Required, 2, TEXT("BASEBALLBAT") },
    };

    // (block, base) -> literal target, per ladder, sorted for binary search. 595 rows in all.
    constexpr FActionException CWeaponUnarmed_Exceptions[] = {
        { 0, TEXT("ACT_RUN_RELAXED"), TEXT("ACT_RUN") }, ...
    };
}
```

The exception list is **per ladder rather than one global array**: the key is already scoped by the
weapon, so a per-weapon array is both the reviewable form and the faster lookup, and no ladder
carries more than 59 rows.

The rewrite rules are data too, so the model is entirely declarative and a dead rule is visible as
one. Three renames (`ACT_AIM → ACT_READY_<F>`, `ACT_RANGE_ATTACK1 → ACT_RANGE_ATTACK_<F>`,
`ACT_RANGE_ATTACK1_LAYER → ACT_RANGE_ATTACK_LAYER_<F>`) and the eight substitute bases are emitted
as tables beside the ladders, with the RE citation in a comment; `Rewrite` in `ElysiumActionTables.cpp`
is a pure function over them. `required` is carried as provenance on 57 base-sequence entries and
never branched on — the pinned server translator does not read it.

## 3. Static tables, not compile-time expansion

The resolver never indexes a row. Retail's `ActivityOverride` walks front to back and takes the
first translated activity the model can play, which over this model is:

```
for (family, bases) in Ladder(Weapon):
    if base not in bases: continue
    candidate = Exception(weapon, block, base) ?? Rewrite(base, family)
    if ModelHasSequenceFor(candidate): return candidate
return base                                    // untranslated
```

The candidate is **synthesized and tested** against the body's own clip vocabulary, which the
resolver already loads from `npc/clips`. Expanding the 9,214 rows would materialise ~9,000 target
strings with no consumer. Size is not decisive at this scale (~130 KB against ~750 KB, and the
binary shrugs at either); what decides it is that the compressed form is the one a reviewer can
read and the one the resolver queries.

`FName` cannot be `constexpr`, so the tables are `const TCHAR*` literals in `.rdata` and
`UElysiumAnimSubsystem::GetActionTables()` interns them once at init into the plain-C++
`FElysiumActionTables` the resolver reads — ~825 distinct names, not 9,214. `static_assert` guards
array sizes and ladder arity so a malformed regeneration fails the build.

## 4. The character-catalog extension

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

## 5. Nothing is baked

Every artifact is now either committed source or a per-install sidecar the runtime already reads.
There is no install-varying action data left for an editor pass to produce, so `DA_ActionCatalog`
and `bake_actions.py` are dropped.

> **This reverses an earlier owner call** made under the export-and-bake framing ("bake the whole
> catalog as documented"). It is a consequence of the tables becoming source rather than a new
> judgement. The reversal is confirmed by the owner, and `animation-architecture.md` §3.4 —
> which specified the export corpus, the `out/animation/actions/` path and the bake — is rewritten
> to match.

## 6. Verification

Three levels. The first two are the gate; the third corroborates.

**Round trip and well-formedness** — content-free, runs anywhere, no game files.
`Elysium.Substrate.WeaponActivityTables` expands the committed model and asserts it yields 9,214
rows across 61 classes, each class's rows one contiguous run in the decode's own order, the 201
`required` flags intact; that every ladder is non-empty; that every exception addresses a real
block, names a base that block walks, and is reachable through the binary search its emitted order
depends on; that no rename or substitute rule names a base no ladder walks. The proof that the
compression is lossless is a **digest**: the generator takes an FNV-1a 64 over the retail decode's
own row stream and stamps it into the source, and the test recomputes it from the committed model,
so the two agree only if every row survived. This is what catches a bad regeneration or a
hand-edit.

**Behavioural conformance** — `Elysium.Content.ActionTableConformance`, against the export corpus,
abstaining on `.elysium-incomplete.npc`. Walks every (weapon, base) ladder against the exported
clip vocabulary and asserts the measured shape holds: more than a third of resolutions arrive at
rung 2 or later and the ladder reaches past rung 2, every rewrite kind that produces a candidate
answers at least one request, and the two accounted 0% families (`ACT_VM_*`, directional walk/run)
are named with their counts rather than silent. It prints the full named residual, so the
never-resolving set is a list rather than a number. This is the test that catches a misunderstood
rule, and it is strictly stronger than a byte diff — a table transcribed perfectly but interpreted
wrongly passes a diff and fails here.

The viewmodel clause is expected to fail when LIFE6 exports viewmodel bodies; that failure is the
corpus growing, not the tables breaking, and the test says so where it asserts.

**Retail agreement** — owner-run, gates nothing. `research/tooling/probes/action_trace_join.py`
joins the banked corpus's 1,232 observed NPC translation resolutions against the committed tables,
on the full serial-bearing entity handle plus model identity (entity index alone is reusable and is
not an identity key). Its numbers are quoted as acceptance evidence the way "180,812 agreeing / 0
disagreeing" already appears in `animation_and_movers.md`. It reads machine-local research evidence,
so it is never a Content test. A disagreement the banked corpus cannot settle escalates to a scoped
new capture as an owner call.

`action_coverage` is a **QA report**, not runtime data and not an export-corpus artifact: the
reachable rule → translation → sequence closure per body and NPC class, grouped by family, with
every unresolved row named with its provenance. The conformance test already names the residual per
activity family; what the report adds is the per-body and per-NPC-class closure behind it.

## 7. The sessions

Five, one bullet and one commit each.

| # | Session | Touches | Deps |
|---|---|---|---|
| **S1** | **Weapon tables as source** — `gen_action_tables.py`, the generated table, the accessor header and its pure rules, the round-trip test, the conformance test, and the §3.4 rewrite | `research/tooling/gen_action_tables.py`✚, `Private/Visual/ElysiumWeaponActivityTables.cpp`✚, `Private/Visual/ElysiumActionTables.{h,cpp}`✚, `Private/Tests/ElysiumActionTableTests.cpp`✚, `Private/Visual/ElysiumNpcClips.{h,cpp}` (a vocabulary-only read), `docs/architecture/animation-architecture.md` | — |
| **S2** | **Player action rules as source** — 17 codes, 4 marked dormant, predicates, pose writes | generator, `ElysiumPlayerActionRules.cpp`✚, tests | S1 |
| **S3** | **NPC rules and class translation as source** — 10 pre-translation + 5 class-translation bodies, 2+2 delegates, grapple as 29 bases plus the `+1…+8` arithmetic, 111 task routes | generator, `ElysiumNpcActivityTables.cpp`✚, tests | S1, S2 |
| **S4** | **Catalog: events + autolayer census** | `exporters/npc_export.py`, `Private/Visual/ElysiumBlendGrids.{h,cpp}`, tests | — |
| **S5** | **Catalog: transition graph** — decode, verify, emit, write the VtMB doc | `formats/mdl_skel.py`, `npc_export.py`, `animation_and_movers.md` | S4 |

**S1 first**: it establishes the generator, the committed form, both test shapes and the doc
correction that every later table inherits. **S2 before S3** — the player surface is small and
settles the rule schema before the largest surface in the programme inherits it. **S4 is parallel
with S1–S3**: different source, different files.

### Acceptance sentences, quoted back in each kickoff

- **S1** — *the committed model expands to 9,214 rows across 61 classes in the recovered order with
  the `required` column intact, digest-matched against the retail row stream;
  `Elysium.Content.ActionTableConformance` reproduces the measured fallback shape and names the two
  0% families; no rewrite kind resolves at 0%.*
- **S2** — *all 17 `PLAYER_*` codes present with exactly four dormant, every `base_activity`
  resolving in the corpus or named, both effective `Player_Anim` vdata rows carried.*
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
- Build: where a session touched C++ (S1–S3) — state the scope, get owner acceptance, then
  `uv run elysium build`.
- Owner-run corroboration, reporting only: `uv run elysium research action_trace_join`.

## 8. Risks

1. **The transition-graph offsets may not be there.** VtMB v2531 is an odd internal fork; if the
   node fields were stripped, S5 has nothing to decode. Default: **S5 does not block LIFE2** — the
   sidecar carries no `"transitions"` block and LIFE5 inherits the dependency. Settle before S5
   starts, not after it stalls.
2. **The never-resolving requests are absent content, not a wrong rule.** The 2,202 outside the two
   accounted families spread across some thirty activity families with no rewrite kind below 11%
   candidate availability, and 1,232 of them carry their bare base in the corpus with no
   weapon-decorated form. A misdecoded rule would have concentrated on one kind instead. The
   conformance test prints the named list on every run, so a later drift in that shape is visible
   rather than inferred.
3. **Weapon class → runtime tag.** The runtime keys by `WeaponTag` (`"glock"`), the tables by C++
   class and entity classname. A class with no derivable tag is unreachable from the runtime, which
   would mean the runtime should key by entity classname — a change to
   `FElysiumAnimationIntent.WeaponTag`'s meaning that reaches into LIFE4.
4. **`MANIFEST_VERSION` 7 → 8.** `npc_export.py:949` refuses cross-version integration, so anything
   holding a v7 manifest in a work root needs a re-export. If a lane or cached bake plan pins v7,
   S4 is wider than it looks.
5. **The tables become ours to maintain.** A wrong row is now a bug we fix, with the RE record as
   its rationale rather than a binary to re-diff. That is the intended posture, and it means the
   conformance test is the only thing standing between a bad edit and a silently wrong pose.
