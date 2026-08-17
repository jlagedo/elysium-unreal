# LIFE2 — the action catalog: normalization schema, exporter home, bake shape

The approved design behind [`animation.md` → LIFE2](animation.md). Status stays in
`docs/project/roadmap.md`; this file carries the specification only.

## Context

LIFE2 is the one LIFE rung the playbook sends through plan mode first
(`docs/operations/life-agent-playbook.md` → "LIFE2"). Its deliverable is **the normalization
schema, the exporter home, and the bake shape** — after which one direct session lands each
artifact.

The reverse engineering is closed (RE37, `docs/vtmb/animation_and_movers.md` A.3). What is missing
is that its output exists only as research instruments and hand-run reports under
`$ELYSIUM_WORK_ROOT/research`; nothing the game loads is derived from it. The runtime therefore
resolves activities through a **5-row hand-seeded weapon table**
(`Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp:46-80` — five weapon tags, each
translating one activity) against 9,214 recovered rows over 169 weapon classes. LIFE3's real
translation tables, LIFE5's transition traversal and every rung's activity→state coverage wait on
that data reaching the export corpus and the mount.

This plan is what the seven implementation sessions execute against.

## Owner calls made

| Question | Decision |
|---|---|
| Export path | **Drop `out/`.** Land at `$ELYSIUM_EXPORT_ROOT/animation/actions/`, consistent with `npc/`, `items/`, `vdata/`. The `out/animation/actions/` in `plans/animation.md` and `animation-architecture.md` §3.4 is a doc correction S1 carries. |
| Decode seam | **Move the decode cores into `elysium_pipeline/formats/`.** A PE32/RTTI reader is a format parser; probes become reporting shells over it. |
| Transition graph | **Its own session** — decode, verify, record the format fact, then emit. |
| The 5-row stub | **LIFE2 exports and loads; LIFE3 deletes.** Every LIFE2 session is offline or inert. The roadmap LIFE2 row's "deletes the 5-row stub" clause moves to LIFE3. |
| Bake shape | **The whole catalog becomes `DA_ActionCatalog`**, as §3.4 documents. The JSON artifacts are intermediate-only inputs to the bake; the runtime reads the data asset. No divergence to record. |
| Unpinned install | **Hard-fail the `actions` bundle, no escape hatch.** |

## Facts the design rests on

- **There is no `out/` tier.** `export_root()` (`pipeline/src/elysium_pipeline/paths.py:36-38`) is
  `$ELYSIUM_EXPORT_ROOT` itself, and `FElysiumContentPaths::Root()`
  (`Source/ElysiumUE/Private/ElysiumContentPaths.h:14-37`) is the same directory. Every accessor is
  `Root()/<domain>/…`.
- **The probes already read the user's own install** — `vampire.dll`/`client.dll` through
  `paths.vtmb_root()` against pinned SHA-256s, `models/**/*.mdl` through `install.build_index`. The
  rows are re-derivable at export time; banked JSON is not needed.
- **The decode core already exists in the wrong tree.** Six probes import
  `weapon_activity_survey.py` for `PEImage`, `TYPE_DESCRIPTOR_RE`, `_rtti_bases`,
  `_undecorate_type`, `follow_jump`, `constant_return`, `decode_activity_registry`. That set is a
  library today; it is merely shelved as a probe.
- **No pipeline code imports from `research/`.** The CLI runs probes as a subprocess
  (`cli.py:1298-1322`) and `research` is not in `[tool.setuptools.packages.find]`.
- **Events are decoded and never emitted.** `formats/mdl_skel.py` `Seq` carries `events`
  (`read_events`, 76-byte records `{cycle, event, type, options}`); `npc_export.py:568` drops them.
- **Autolayers already ship** in `npc/blends/<stem>.json` beside `grids` (`npc_export.py:384-429`).
- **The transition graph is not decoded at all.** `mdl_skel.py` reads sequence-descriptor offsets
  0/4/8/12/16/20/24/28/40/52/56/572/580/588/596/612/660/664 out of a 764-byte record.
  `animation_and_movers.md:958-964` records the *behaviour* (`AdvanceToIdealActivity` asks for an
  intermediate sequence) but no layout that answers it. A.4c is a **different** mechanism — the
  client-side crossfade transitioner over the @612 duration triple — and must not be conflated with
  the node graph.
- **`write_blends(stem, model, table, prefix="")` is the per-owning-model sequence sidecar** — NPCs,
  banks and animated props alike — and already justifies itself as "both are read from the same
  764-byte sequence descriptor, so they ship in one file."
- **`_clip_meta` states the corpus's own storage rule**: selection keys are "stored once per owning
  stem, not per NPC that resolves it — 157 characters × ~1,400 resolved clips would be two orders of
  magnitude more rows."
- **The data-asset bake precedent** is `pipeline/unreal/bake_wield.py` →
  `/ElysiumBaked/Items/DA_WieldModels` (`UElysiumWieldTable : UDataAsset`,
  `Public/ElysiumWieldTable.h:159-187`), with `build_data_asset` (`bake_wield.py:605-637`) showing
  load-first-then-`DataAssetFactory`, wholesale array overwrite, and the rule that **every row is
  filled on every run** because a row's soft path is deterministic from the package layout alone.
  Its bake is driven by its **own** command (`uv run elysium export wield`) beside the `items`
  bundle that produces its manifest.
- **The runtime plug-in seam is `FElysiumAnimationCatalog`**
  (`Private/Visual/ElysiumAnimationResolve.h:26-41`) — a plain-C++ **view** whose blend table is a
  `TFunction<const T*(const FString& OwnerStem)>` callback "because the owning bank is not known
  until the weighted pick has run" — assembled by `UElysiumAnimSubsystem::BuildCatalog`
  (`ElysiumAnimSubsystem.cpp:586-603`). `ElysiumAnimResolve::Resolve` holds no `UObject` and touches
  no filesystem, which is what lets `Elysium.Substrate.AnimationResolve` build a fixture on the stack.
- **Activities are `FString` `ACT_*` literals compared case-insensitively.** The only enum is the
  13-value slice `EElysiumAnimActivityCode`, whose header forbids merging it with the 4,460-entry
  registry.
- **Bundle registration is a four-place sync** — `exporters/profiles.toml`, the hardcoded `allowed`
  set at `cli.py:1054-1057`, `export_all.py::_run_bundle`, `export_manager.py::_bundle_outputs` —
  plus a `clean.py DOMAINS` entry, which drives the `.elysium-incomplete.<domain>` marker Content
  tests abstain on. Domain name need not equal the leading path segment: `use-icons` writes `hud/`.
- **The "missing mapping is a hard failure" precedent** is
  `Private/Tests/ElysiumBakedClipCoverageTests.cpp` (`Elysium.Content.BakedClipCoverage`):
  `AddError` per miss, capped at 8 per body with a rollup.

---

## 1. The decode seam

Two new parsers under `pipeline/src/elysium_pipeline/formats/`:

| Module | Owns | Lifted from |
|---|---|---|
| `vampire_pe.py` | PE32 mapper, MSVC RTTI walk, call/jump/constant-return helpers, the pinned-hash gate. **No animation semantics.** | `weapon_activity_survey.py:38-290` |
| `vtmb_actions.py` | `activity_registry()`, `weapon_tables()`, `player_actions()`, `npc_translations()`, `npc_task_routes()`, `native_schedules()`, `sequence_events()`, `layer_bindings()` | the eight probes' decode halves |

Probes keep their filenames, their `build_report()` / `print_report()` / `main(--json)` shape, their
`--json`-or-nothing behaviour and their RE bookkeeping (addresses, evidence counts,
`non_action_calls_at_same_slot`, summaries). Only the decode bodies leave. **Move only what the
session in hand needs** — no big-bang lift.

This satisfies both trees' declared ownership: `pipeline/CLAUDE.md` puts file parsers in `formats/`,
and `research/CLAUDE.md` says the research tree holds instruments, not evidence. It keeps an export
reproducible from a clean checkout plus a user install.

*Runner-up:* an exporter importing `research.tooling.probes.*` — loses on packaging. `research` is
not an installed package, so the import works only when the repo root happens to be on `sys.path`,
which a lane worktree or the editor process does not guarantee, and `pipeline/CLAUDE.md` forbids a
module that assumes a current directory. Consuming banked `--json` loses harder: it makes the export
depend on an undocumented eight-probe ritual whose output silently goes stale.

### Hash posture — verify and refuse

The reason is the failure mode, not purity. A mismatched build decodes to **structurally plausible
but wrong** rows: wrong activity IDs, wrong vtable slots, a weapon table read from whatever now sits
at `+0x5a8`. Every downstream consumer would then agree with itself and report success. An empty
table is worse — it makes content tests pass by giving them nothing to check. Both violate "runtime
failures are never silent."

The stop is **narrow**, so a user on a different patch still gets a working corpus:

- `actions` is its own clean domain. On mismatch the bundle raises naming expected and actual digest
  and the path; `.elysium-incomplete.actions` stays set; every action-family Content test abstains
  via `FElysiumContentPaths::IsIncomplete(TEXT("actions"))` + `ELYSIUM_TEST_ABSTAIN`.
- **The character-catalog half is not gated on the DLL.** Events, autolayers and the transition graph
  come from `.mdl` files, stay in the `npc` domain, and export normally on any install.
- With no action corpus the resolver performs identity translation with zero iterations and the
  selection record says so — the same honest answer a content-free unit test gets.
- No `--allow-unpinned-binary`. That path is untested by construction until a second build is in hand
  to verify against, and its only possible output is wrong data, quietly.

**Test coupling to watch:** `pipeline/tests/test_weapon_activity_survey.py` and siblings import probe
internals directly (`constant_return`, `survey_equipment`). If they assert on decode functions rather
than on `build_report()` output, S1's move breaks them and the fix re-points them at `formats/`.

## 2. The exporter home

- **Product:** `$ELYSIUM_EXPORT_ROOT/animation/actions/`. Clean domain **`actions`** (not
  `animation`), following the `use-icons`→`hud/` precedent.
- **Exporter:** `pipeline/src/elysium_pipeline/exporters/action_export.py`. Deliberately **no `UE_`
  prefix** — that prefix is a coordinate claim (`pipeline/CLAUDE.md` → "The `UE_` exporter
  convention") and this exporter emits no coordinate-bearing data. It sits beside `npc_export.py`,
  named the same way for the same reason.
- **A new `actions` bundle, not folded into `npc`.** Fast QA is a scope contract: a session iterating
  on `weapon_activity_tables.json` must not re-export the skeletal corpus. Folding into `npc` would
  also couple a DLL-hash failure to the character corpus.
- **Not all six artifacts live in this bundle.** The character-catalog extension is written by
  `npc_export.write_sidecars`/`write_blends` and stays in `npc`.

| Registration point | Change |
|---|---|
| `exporters/profiles.toml` | `"actions"` appended to `[profiles.grid].bundles` and `[profiles.all].bundles`, **after** `vdata`, `scripts` and `npc`. Bundles run serially in list order, so position *is* the dependency: player rules read the discipline `Player_Anim` vdata rows, NPC rules read map `.ents` and the Python producers, and the coverage join reads `npc/npc_index.json`. |
| `cli.py:1054-1057` `allowed` | add `"actions"` |
| `export_all.py::_run_bundle` | `elif name == "actions": … action_export.main(index=index, strict=True)` |
| `export_manager.py::_bundle_outputs` | `"actions": (export_root/"animation"/"actions"/"action_index.json",)` |
| `clean.py DOMAINS` | insert `"actions"` |

`_bundle_outputs` names **one witness**, `action_index.json`, not the six artifacts: it is written
last, so its presence means the whole set landed, and it doubles as the single load-and-validate
entry point — the `npc_index.json` role.

**The generic fingerprint is insufficient.** `_bundle_tasks` fingerprints
`("bundle", b, source_fingerprint, *maps)` where `source_fingerprint` hashes pipeline *code*. That
covers the new modules but **not the DLLs**, so a user changing install patch level would get a stale
no-op. `actions` folds the content digests of **both** `vampire.dll` and `client.dll` into its
`extra=` tuple via `tasking.ContentDigestCache` — both are read, since the event and layer decodes
need the client.

## 3. The normalization schema

**Convention: the checked `{"schema": "elysium.<name>", "version": N}` pair** used by
`character_partition.py`, `character_cache.py`, `wield_corpus.py` and `bake_cache.py` — not the bare
`manifest_version` of the older `npc` family. Shared header on every file:

```json
{"schema":"elysium.activity-registry","version":1,
 "provenance":{"server_sha256":"c546f4de…","client_sha256":"e88beae0…","pinned":true}}
```

**No timestamp field** — reruns must be byte-identical or the fingerprint cache means nothing.

### The activity key: the name is the key; the ID lives in exactly one file

The runtime compares `FString` `ACT_*` case-insensitively everywhere; `ElysiumNpcClips.cpp:107-116,149`
interns names from the MDL; and `mdl_skel.py:499` records that the sibling `activity`@12 int stays −1
on disk because the game resolves the *name* at load — **the format itself states that the name is the
durable key.** A numeric ID crossing into any other artifact would create exactly the second identity
`EElysiumAnimActivityCode`'s header forbids.

Names are emitted **verbatim as registered** (upper-case), never case-folded: the registry is the
authority on spelling, and a folded file loses the ability to report a mis-cased authored row.
Consumers compare case-insensitively.

```json
{"schema":"elysium.activity-registry","version":1,"provenance":{…},
 "fields":["name","id","ordinal"],
 "activities":[["ACT_IDLE",1,1],["ACT_TRANSITION",2,2],…],
 "id_holes":[[low,high],…],
 "summary":{"registrations":4460,"id_min":1,"id_max":4492,"holes":32}}
```

Row-arrays plus a `fields` header — the `npc/clips` interning idiom — rather than 4,460 objects.
`id_holes` as ranges because "32 holes" is the interesting negative fact, and a consumer asserting
"every ID in 1…0x118c resolves" needs the sanctioned gaps.

### Weapon tables: dedupe by blob, reference by index

```json
{"schema":"elysium.weapon-activity-tables","version":1,"provenance":{…},
 "translator":{"table_slot":"0x5a8","count_slot":"0x5ac","row_stride":12,
   "required_field_read":false,
   "row_order":"as walked by CBaseCombatWeapon::ActivityOverride, front to back",
   "selection":"first matching row whose translated activity is available; later duplicate-base rows remain fallbacks"},
 "row_fields":["base","translated","required"],
 "tables":[{"id":0,"rows":[["ACT_RANGE_ATTACK1","ACT_RANGE_ATTACK_GLOCK",1],…]},…],
 "classes":[{"class":"CWeaponGlock","entity":["weapon_glock"],"tag":"glock","table":0},…],
 "empty_tables":["…"],
 "summary":{"classes":169,"nonempty":61,"distinct_tables":58,"class_rows":9214}}
```

Dedupe wins on a fact, not on size: **shared-table identity is itself a recovered result** (58 blobs
over 61 non-empty tables). Repeating rows per class destroys it — a consumer could no longer tell
"these two weapons share retail's table" from "these two happen to have equal rows." The `table`
integer *is* the shared-table identity. *Runner-up* (repeat per class) would need a redundant
per-class blob hash to recover what the index already states, at ~3× the bytes.

Order is array order and is never sorted; `row_order` says why. `required` is the third element, with
`required_is_inert` stated once at file level rather than 9,214 times.

The runtime keys by `WeaponTag` (`"glock"`), the DLL by C++ class. **Emit all three** on the class
row. A class with no derivable tag gets `"tag": null` and becomes a named unresolved row in the
coverage join rather than a guess.

### Rules: confidence is an enum; evidence is free-form

```json
{"schema":"elysium.player-action-rules","version":1,"provenance":{…},
 "rules":[{"code":"PLAYER_RELOAD","ordinal":14,"mode":"ordinary",
   "predicates":[{"field":"weapon_present","op":"eq","value":true}],
   "base_activity":"ACT_RELOAD",
   "layer_activity":"ACT_RELOAD_LAYER",
   "pose_writes":[{"param":"move_yaw","source":"velocity_yaw"}],
   "reachability":"reachable",
   "confidence":"capture-verified",
   "evidence":{"address":"0x10164870","note":"…"}}]}
```

`confidence` is a fixed three-value enum — `capture-verified` | `decompiled` | `inferred` — because
the coverage gate branches on it: an `inferred` row that misses is a warning, a `capture-verified`
row that misses is an error. `evidence` is free-form because addresses are pinned-build bookkeeping.
**Addresses appear only inside `evidence`**, never at rule top level: a rule row must be legible to a
consumer that knows nothing about the DLL.

All 17 `PLAYER_*` codes ship. The four dormant compiled ones (3, 6, 15, 16) carry
`"reachability":"dormant"`, are excluded from the required closure, and are never dropped — §3.5
step 2 requires keeping them distinct from reachable gameplay.

`npc_action_rules.json` uses the same header and confidence shape, with rows
`{class, aliases, producer{kind,name,task}, desired{activity|sequence|model}, route:"base"|"layer",
layer_weight, interrupt{conditions,restart_on_identical}, completion{on,then}}` — plus a **separate
top-level `translations` array** for the 10 pre-translation and 5 class-translation bodies with
their inheritor lists, the 2+2 cover/reload delegates, and the grapple family as **29 bases plus the
`+1…+8` role/size/side arithmetic, not 232 enumerated rows**. Those are structurally a translation
table, not a rule, and LIFE3 consumes them beside the weapon table. 15 tables, so no dedupe.

`action_coverage.json` interns subjects and labels (56 player bodies × NPC classes × 7 families is a
large row count) with rows
`{subject, family, producer, requested, translations[], resolved, owner, label, asset, status,
fallback, reason, confidence}`, `status ∈ {resolved, fallback, unresolved}`.

`action_index.json` is the seventh file: per-artifact filename, row count and SHA-256, written last.

## 4. The character-catalog extension

Three payloads. **No new sidecar** — all three land in the existing per-owning-model file.

| Payload | Home | Why |
|---|---|---|
| autolayers | `npc/blends/<stem>.json` — **already there** | LIFE2's work is a verification assertion, not an exporter change |
| events | new optional `"events"` block, **same file** | per-owning-model, like the grids beside it |
| transition graph | new `"transitions"` block, **same file** | per-model by construction |

```json
"event_fields":["cycle","event","type","options_i"],
"event_options":["footstep_l","…"],
"events":{"<label>":[[0.35,1004,0,3],…]},
"transitions":{"nodes":12,
  "sequence_nodes":{"<label>":[entry,exit,nodeflags]},
  "matrix":[[…]],
  "confidence":"decoded"}
```

**Why the per-owning-model file rather than `npc/clips/<stem>.json`:** the clips sidecar is per-NPC
and resolves labels *through* banks, so a bank clip's events would be duplicated across every
character that resolves it. `_clip_meta`'s own comment makes exactly this argument for the selection
keys — "stored once per owning stem, not per NPC that resolves it." The runtime already has the
owner in hand (`Clip->Owner`) and already reaches owner-scoped data through `BlendTableFor`, so the
same callback shape serves events with no new mechanism.

**Do not widen the 7-element clip row.** That would force a lockstep C++ change to
`ElysiumNpcClips.cpp:107-116,149` for a payload most clips do not carry. An optional block is
additive in both directions.

Versioning: bump `npc_export.MANIFEST_VERSION` 7 → 8 (the manifest and index carry it, and
`npc_export.py:949` refuses cross-version integration) and add **no** version to the per-stem
sidecars. A v7 runtime ignores the new blocks; a v8 runtime sees none on a v7 sidecar. That is the
honest behaviour. `npc_index.json` gains an `event_sequences` count beside `blend_grids`.

### The transition graph is new MDL decode work

Scope of the decode-and-verify task:

1. **Per-sequence:** entry node id, exit node id, node flags. The inline 16×16 grid occupies
   56…568, so the unclaimed regions of the 764-byte descriptor are **@600-608**, **@616-656** and
   **@668-763**. HL1's `mstudioseqdesc_t` carried `entrynode, exitnode, nodeflags, nextseq` as four
   consecutive ints; look for a consecutive triple/quad whose first two are small non-negative and
   bounded by a model-level count.
2. **Model-level:** a `numlocalnodes`/`localnodeindex` header pair addressing an `n × n` byte
   adjacency matrix. `research/tooling/probes/studiohdr_unclaimed_fields.py` is the existing
   instrument for exactly this hunt.

**Acceptance for the decode, before a byte is emitted:** every entry/exit node in `[0, n)` across all
4,445 v2531 models; `n` from the header equals `max(node)+1`; on a model carrying `*_to_*` sequences
the matrix's non-trivial cells name exactly those sequences; and the transition-lookup function
reached from `0x102726a0` is confirmed to read those offsets. That last one is what turns "plausible"
into "recovered." The finding belongs in **`docs/vtmb/animation_and_movers.md`** — the seqdesc offset
table at line 211 and A.4c — never in a `CLAUDE.md`.

## 5. The bake shape

Per the owner call, the whole catalog becomes one asset, as §3.4 documents.

**`/ElysiumBaked/Actions/DA_ActionCatalog`** — `UElysiumActionCatalog : UDataAsset`
(`Public/ElysiumActionCatalog.h`):

```
ActivityRegistry : TArray<FElysiumActivityRow>        // name, id, ordinal
WeaponTables     : TArray<FElysiumWeaponActivityTable> // ordered rows, verbatim
ClassToTable     : TMap<FName, int32>                  // class + entity + tag -> table id
PlayerRules      : TArray<FElysiumPlayerActionRule>
NpcRules         : TArray<FElysiumNpcActionRule>
NpcTranslations  : TArray<FElysiumActivityTranslationTable>
Coverage         : TArray<FElysiumActionCoverageRow>   // carries TSoftObjectPtr<UObject> Asset
```

`TSoftObjectPtr<UObject>` rather than two typed pointers, because a coverage row resolves to either a
`UAnimSequence` or a `UBlendSpace` and the resolver already distinguishes those by asset form. Keys
case-folded to lower, matching `FElysiumWieldRow`'s stated contract.

One asset, not several: it is queried as one thing, it is small, and splitting per-body would
multiply the half-run-partial-asset hazard by 56.

**Owner: a new `pipeline/unreal/bake_actions.py`, driven by its own `uv run elysium export actions`**
— the exact `items` bundle / `export wield` split that already exists. `export bundle actions` does
the offline decode; `export actions` runs the editor step over its output. Folding it into
`bake_characters.py` loses because that worker is per-stem scoped (`-BakeCharacters=<csv>`) while
this table is global, so scoped character iteration would pay for a whole-table rewrite.

**Partial-bake rule, taken verbatim from wield:** every row is filled on every run, whether or not
this run baked that stem, because a coverage row's soft path is a pure function of the export corpus
(`BakedCharacterAnim(Family, Owner, Label)`). Soft paths resolve without loading assets. A row is
marked unresolved only when `action_coverage.json` already said so — never because this run happened
not to bake that stem. `Rows` are overwritten wholesale and never merged, because a bake that died
mid-run leaves a partial asset. Follow `bake_lib.py:708-732`'s load-first-then-`DataAssetFactory`
pattern.

**Per-model events and the transition graph do not become assets.** They extend `npc/blends` and
`npc_index`, which §3.4 names as the catalog's own home ("extends the existing `npc/clips`, blend and
index sidecars"), and which `UElysiumAnimSubsystem` already reads. The line is clean: DLL-derived
rules plus the join → the data asset; MDL-derived per-model data → the existing sidecars. Turning an
event into a `UAnimNotify` is LIFE5's carrier bullet, not this rung's.

## 6. The runtime consumption seam

| Data | Seam | Shape |
|---|---|---|
| registry, rules, translations, weapon tables | `UElysiumActionCatalog::Load()` → converted once into plain-C++ `FElysiumActionTables` by `UElysiumAnimSubsystem::GetActionTables()` | the `UElysiumWieldTable::Load()` rooted-static shape — null with one warning naming the producing command when the bake has not run |
| coverage | the same asset, read by tests and tooling only | never per-frame |
| events, transitions | `GetEvents(Stem)` / `GetTransitions(Stem)` caches → new `FElysiumAnimationCatalog` members | exactly the `GetClipSet`/`GetBlendTable` pattern, owner-scoped callbacks |

The conversion step is load-bearing. `ElysiumAnimResolve::Resolve` is deliberately pure C++ with no
`UObject` and no filesystem — that is what lets `Elysium.Substrate.AnimationResolve` build a fixture
on the stack. So the baked asset is read **once** by the subsystem and flattened into plain structs,
and the resolver reaches them through the catalog as a view:

```cpp
struct FElysiumAnimationCatalog {
    …
    // The translation ladder this resolve runs. A content-free test supplies its own on the
    // stack, which is why it is a view and not a subsystem lookup.
    const FElysiumActionTables* Actions = nullptr;
};
```

`BuildCatalog` assigns `Catalog.Actions = &GetActionTables();`.

**LIFE2 lands the asset, the loader and their tests. It changes no behaviour.** The resolver keeps
consuming `GWeaponTranslationTables`/`GActorTranslations` until LIFE3 swaps it, so
`Elysium.Substrate.AnimationResolve` stays content-free and unchanged through this rung.

**Decided now, so LIFE2's row types are shaped for it —** three constraints LIFE3 inherits:

- `TranslateActivity` grows a `const FElysiumActionTables*` parameter; `nullptr` means identity
  translation, zero iterations, said so in the selection record. That is the honest answer for "no
  action corpus" and for a hash-mismatched install alike, and `FormTag` finally has data behind it.
- The seven stub rows **move into the test fixture** in `ElysiumAnimationActionTests.cpp`, so the
  substrate suites keep their assertions verbatim with no export corpus. A default-populated
  production table is the stub surviving under a new name and is rejected.
- `FElysiumAnimationSelection` gains `TArray<FElysiumTranslationStep> TranslationHistory`, replacing
  today's count-plus-first/last approximation, and `PreTranslationActivity` becomes fillable (RE37
  recovered the +0x5dc surface, 10 bodies). `TransitionSequence` stays empty; LIFE5 fills it over
  LIFE2's graph.

New `FElysiumContentPaths` accessors — `ActionsDir()`, `ActivityRegistry()`,
`WeaponActivityTables()`, `ActionCoverage()`, `BakedActionCatalog()` — and the file-local
`ReadJsonFile` shape at `ElysiumNpcClips.cpp:40-55` for the offline-side readers. No shared JSON
utility: 17 independent sites is the house pattern and LIFE2 is not the session to change it.

## 7. The coverage join and the banked-trace acceptance

Three separable things that must not be conflated.

**(A) The join — offline Python, in `exporters/action_export.py`.** It reads only export-corpus
inputs (`animation/actions/*.json`, `npc/npc_index.json`, `npc/clips/*`, `npc/blends/*`,
`items/wield_models.json`, the 22 maps' `.ents`) and writes `action_coverage.json`. `exporters/` owns
export products and the plan lists coverage as one of the six artifacts. The *checking* half — "is
this closure complete for the accepted families?" — is legitimately a comparator and lives in
`validation/action_coverage_check.py` for the offline QA path.

**(B) The gate — `Elysium.Content.ActionCoverage`**, shaped on `ElysiumBakedClipCoverageTests.cpp`:
read `DA_ActionCatalog`'s `Coverage` rows and the mount, `AddError` per row whose status is
unresolved and whose confidence is not `inferred`, capped at 8 per subject with a rollup, abstaining
on `IsIncomplete(TEXT("actions"))` or a null catalog. This is "missing required mappings fail content
tests."

**(C) The banked-trace agreement — research tier, owner-run, never a gate.** The corpus is
`$ELYSIUM_WORK_ROOT/research/retail-capture/<recipe>/<session>/capture.sqlite` plus
`gameplay-actions-report.json`: unversioned, machine-local, research evidence by `research/CLAUDE.md`'s
own rule. **A Content test reading it would fail on every machine but one.**

So it runs as an instrument that reports, not as a test that gates:

- New `research/tooling/probes/action_trace_join.py`, a probe like every other. It reads the banked
  `capture.sqlite` and the exported `animation/actions/*.json`, joins on the **full serial-bearing
  entity handle plus model identity** (entity index alone is reusable and is not an identity key,
  §3.5 step 5), and prints per-boundary agreement counts — base activity, each translation, final
  sequence — naming every disagreement. `--json` writes the ledger under `$ELYSIUM_WORK_ROOT/research`;
  it default-writes nowhere, like its siblings.
- It is run **once, by the owner, as LIFE2's acceptance evidence.** Its numbers are quoted in the
  session evidence and the roadmap line, the way "180,812 agreeing / 0 disagreeing" and "1,232
  complete NPC translation resolutions" already appear in `animation_and_movers.md`.
- It is wired into no bundle, no test and no CI path, and `pipeline/` gains no import of it. Capture
  is the oracle, not the gate; a disagreement the banked corpus cannot settle escalates to a scoped
  new capture as an owner call, which LIFE2's acceptance already says.

**No committed distilled fixture.** Freezing a few dozen `(subject, activity, translation, sequence)`
tuples would let a Content test check the trace invariants anywhere — but those are game-derived
names, and the root `CLAUDE.md`'s "nothing game-sourced is committed" has no names-only exemption.
Inventing that exemption is not LIFE2's call.

## 8. The session split

Seven sessions, one bullet and one commit each.

| # | Session | Key files | Selector | Deps |
|---|---|---|---|---|
| **S1** | **Seam + registry + registration** — `vampire_pe.py`, `vtmb_actions.activity_registry()`, rewire the probes, `action_export.py` emitting `activity_registry.json` + `action_index.json`, the five registration points, the DLL fingerprint, the `FElysiumContentPaths` accessors, the `out/` doc correction | `formats/vampire_pe.py`✚, `formats/vtmb_actions.py`✚, `exporters/action_export.py`✚, `profiles.toml`, `export_all.py`, `export_manager.py`, `clean.py`, `cli.py`, `research/tooling/probes/*.py`, `Private/ElysiumContentPaths.h` | `export bundle actions` | — |
| **S2** | **Weapon tables** — `weapon_tables()`, `weapon_activity_tables.json`, blob dedupe, the class/entity/tag triple | `formats/vtmb_actions.py`, `action_export.py` | `export bundle actions` | S1 |
| **S3** | **Player rules** — `player_actions()`, `player_action_rules.json`, 17 codes with 4 dormant, pose writes | `formats/vtmb_actions.py`, `action_export.py` | `export bundle actions` | S1 |
| **S4** | **NPC rules** — `npc_translations()` + `npc_task_routes()` + `native_schedules()`, `npc_action_rules.json` and its `translations` array | `formats/vtmb_actions.py`, `action_export.py` | `export bundle actions` | S1, S3 |
| **S5** | **Catalog: events + autolayer census** — emit `Seq.events` into `npc/blends/<stem>.json`, `MANIFEST_VERSION` 7→8, `npc_index` counts, assert the autolayer census | `exporters/npc_export.py`, `Private/Visual/ElysiumBlendGrids.{h,cpp}` | `export characters --only <one player body, one cast body>` | — |
| **S6** | **Catalog: transition graph** — decode-and-verify in `mdl_skel.py`, emit `"transitions"`, write the finding into the VtMB doc | `formats/mdl_skel.py`, `exporters/npc_export.py`, `docs/vtmb/animation_and_movers.md`, `probes/studiohdr_unclaimed_fields.py` | probe the install, then `export characters --only <two stems>` | S5 |
| **S7** | **Coverage join + bake + loader** — the join, `validation/action_coverage_check.py`, `bake_actions.py`, `UElysiumActionCatalog`, `GetActionTables()`, the gate test, `action_trace_join.py` and its one owner run | `action_export.py`, `validation/action_coverage_check.py`✚, `pipeline/unreal/bake_actions.py`✚, `Public/ElysiumActionCatalog.h`✚, `Private/Visual/ElysiumActionCatalog.cpp`✚, `Private/Tests/ElysiumActionCoverageTests.cpp`✚, `probes/action_trace_join.py`✚, `cli.py` | `export bundle actions` → `export actions` | S1–S6 |

**S1 goes first, and nothing else can.** It establishes the seam, the path, the bundle, the
header/provenance convention and the hash-failure posture — and the registry is the vocabulary every
other artifact references by name. Doing another artifact first means doing S1's work inside a
session with a different goal, which is how a schema gets decided by accident.

**S3 before S4** is a deliberate reorder from the brief's ordering. The player probe is ~490 lines
importing only `PEImage`/`follow_jump`; the NPC surface is the largest in the programme with three
upstream probe imports. S3 exercises the rule schema — `predicates`, `pose_writes`, `confidence`,
`evidence` — at small scale, so S4 inherits a settled shape. Getting the rule schema wrong on S4 is
far more expensive than getting it wrong on S3.

**S5 is the only genuine parallelism** — different bundle, different source, no shared file with
S1–S4. It can run alongside S1.

**Split further:** S4, if it overruns — separate the schedule/task-route half from the
class-translation half. **Merge:** nothing.

### Acceptance sentences, to be quoted back in each kickoff

- **S1** — *the moved cores leave every `pipeline/tests/test_*_survey.py` green;
  `Elysium.Content.ActivityRegistry` asserts 4,460 registrations spanning IDs 1…0x118c with exactly
  32 declared holes and every name a distinct ACT_* literal, abstaining on `.elysium-incomplete.actions`;
  a DLL whose hash does not match the pin fails the bundle with one named error and no partial file.*
- **S2** — *`Elysium.Content.WeaponActivityTables` asserts 169 classes, 61 non-empty tables
  referencing 58 distinct blobs, 9,214 class rows in verbatim order, every referenced activity name
  present in the registry, and the five previously-hardcoded weapon answers reproduced from the
  export.*
- **S3** — *`Elysium.Content.PlayerActionRules` asserts all 17 `PLAYER_*` codes present with exactly
  four marked dormant, 15 genuine `+0x704` calls recorded and the 14 `CAI_BaseNPC` calls at the same
  offset excluded, both effective `Player_Anim` vdata rows present, and every `base_activity`
  resolving in the registry.*
- **S4** — *`Elysium.Content.NpcActionRules` asserts 77 descendants collapsing to 10 pre-translation,
  5 class-translation and 2+2 delegate bodies; 111 task routes over 100 policy rows with zero custom
  exact-label routes; the five named scripted-label misses surviving with their provenance; and every
  NPC class the exported `.ents` place resolving to a non-`inferred` rule row or being named
  unresolved.*
- **S5** — *for the selected stems every clip carrying events in the MDL carries them in
  `npc/blends/<stem>.json` with cycle in [0,1] and `type == 0`, matching `npc_index.json`'s rollup;
  the autolayer census still reads `{0:13544, 1:237, 2:224}` with the one inverted host named;
  abstains on `.elysium-incomplete.npc`.*
- **S6** — *across all 4,445 v2531 models every decoded entry/exit node lies in `[0, n)` for the
  header's own node count, `n == max(node)+1`, the matrix's non-trivial cells on a `*_to_*`-carrying
  model name exactly those sequences, and the lookup reached from `0x102726a0` is confirmed to read
  those offsets; the layout and its counts are recorded in `animation_and_movers.md`.*
- **S7** — *`action_coverage.json` names every producer in the accepted families for the supported
  player bodies and placed NPC classes; every row is resolved, a named fallback, or unresolved with
  provenance; `Elysium.Content.ActionCoverage` errors on any unresolved row whose confidence is not
  `inferred`; `DA_ActionCatalog` carries every row on a scoped run; and the owner-run
  `action_trace_join` reports agreement on base activity, each translation and the final sequence for
  the traced population, naming every disagreement.*

## Verification

Per session, narrowest first — no broad profile, no `--force`, no unscoped tier:

- **Python unit:** `uv run python -m unittest pipeline.tests.<module>` for the one module the session
  touched (`test_weapon_activity_survey`, a new `test_action_export`, a new `test_mdl_transitions`).
- **Export:** `uv run elysium export bundle actions` (S1–S4, S7); `uv run elysium export characters
  --only <stem>` with the stems named in the kickoff (S5, S6).
- **Bake:** `uv run elysium export actions` (S7 only).
- **Runtime:** the one focused filter each session adds — e.g. `uv run elysium test
  Elysium.Content.ActionCoverage`. Never the whole `Content` tier as routine validation.
- **Build:** only where a session touched C++ (S7): state the scope, get owner acceptance, then
  `uv run elysium build`.
- **Research comparator (S7, reporting only):** `uv run elysium research action_trace_join`.

## Risks and unknowns

1. **The transition-graph offsets may not be there.** VtMB v2531 is an odd internal fork; if the node
   fields were stripped from the descriptor, S6 has nothing to decode and the answer becomes whatever
   the lookup at `0x102726a0` actually reads. Highest-variance item, which is why it is split out and
   sequenced last. **Default: S6 does not block LIFE2** — the sidecar simply carries no `"transitions"`
   block and LIFE5 inherits the dependency. Settle this before S6 starts, not after it stalls.
2. **Coverage-row cardinality.** 56 player bodies × NPC classes × 7 families × reachable producers
   could reach six figures. If `action_coverage.json` exceeds a few tens of MB, the rows need interned
   activity and label tables throughout — cheap if anticipated, expensive if discovered during the bake.
3. **Weapon class → runtime tag derivation.** The runtime keys by `"glock"`, the DLL by
   `CWeaponGlock` plus entity classnames. If constructor-alias recovery leaves classes with no
   derivable tag, those rows are unreachable from the runtime — which would mean the *runtime* should
   key by entity classname instead, a change to `FElysiumAnimationIntent.WeaponTag`'s meaning that
   reaches into LIFE4.
4. **`MANIFEST_VERSION` 7 → 8 blast radius.** `npc_export.py:949` refuses cross-version integration,
   so anything holding a v7 manifest in a work root needs a re-export. If a lane or a cached bake plan
   pins v7, S5 is wider than it looks.
5. **Probe test coupling.** `pipeline/tests/test_*_survey.py` import probe internals. If they assert
   on decode functions rather than `build_report()` output, S1's move breaks them.
6. **`native_schedule_survey.py` records a SHA-256 but does not appear to enforce it.** Whether that
   decode is genuinely build-independent or merely unguarded decides whether S4's data belongs inside
   the hard-fail `actions` domain.
7. **The pinned install.** The whole `actions` bundle refuses on a non-pinned patch build. Confirm the
   local `vampire.dll` hashes to `c546f4de…` before S1 starts.
8. **The `research` → `pipeline` dependency edge already carries load** — six probes import a seventh.
   S1 flips the root of that DAG into `pipeline`; if a helper turns out to be VtMB-semantic rather
   than PE-structural, the `vampire_pe.py`/`vtmb_actions.py` boundary moves.
