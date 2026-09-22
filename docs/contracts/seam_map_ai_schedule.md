# AI schedule GLB seam

This document defines the binary glTF 2.0 units for VtMB's NPC behaviour programs: the 691
schedule texts Troika typed into `.rdata`, the per-class id spaces they compile against, and the
vocabulary the schedule-text parser resolves their operands through. Shared rules are owned by
`seam_map_unit_contract.md`. The retail facts are owned by
`docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked" (the
grammar) and § "The schedule owners and their registrations" (the call-site recipe and the
per-owner table); this document states only what the units carry and how the bytes are proven.

Unlike every other seam here, the source is **code**, not content: `Vampire/dlls/vampire.dll`.
That is why this seam is the first to use the executable-image rule
(`seam_map_unit_contract.md` § "Source resolution") and the first to deploy derived products
(§ "Derived products" there).

## Unit identity

```text
<VTMB>/Vampire/dlls/vampire.dll -> the class's CAI_ClassScheduleIdSpace
  -> vtmb:ai-schedule:<owning classname, lower-cased>
  -> $ELYSIUM_EXPORT_V2_ROOT/ai-schedules/<key>.glb

<VTMB>/Vampire/dlls/vampire.dll -> the parser's vocabulary and the image partition
  -> vtmb:ai-schedule:vocabulary
  -> $ELYSIUM_EXPORT_V2_ROOT/ai-schedules/vocabulary.glb
```

**The unit is the SPACE, not the class.** Four pairs of classes share one
`CAI_ClassScheduleIdSpace` through a common slot-580 getter — `CNPC_VCamera` /
`CNPC_VCameraSecurity` (`0x103683b0`), `CNPC_VVampire` / `CNPC_VPlayerController` (`0x103750e0`),
`CNPC_VHumanCombatant` / `CNPC_ProneDialog` (`0x10386ae0`), `CNPC_VScurrying` / `CNPC_VRat`
(`0x103abba0`) — and the second of each pair has no init body of its own. The key names the
**owning** class, the one whose body runs `CAI_LocalIdSpace::Init` on the space; every classname
whose slot-580 getter returns that space is listed in `identity.classNames`, sorted. The space's
virtual address is the true identity but is not legible, so it is published rather than keyed on.

```text
uv run elysium export_v2 ai-schedule-glb cnpc_vbrujah
uv run elysium export_v2 ai-schedules-glb
```

**57 units: 56 spaces plus the root.** 56 init bodies call the parser thunk `0x10006398`; 48 of
them feed at least one text and eight feed none — `CAI_StandoffBehavior`, `CNPC_VChangBrosBlade`,
`CNPC_VChangBrosClaw`, `CNPC_VLasombra`, `CNPC_VSabbatGunman`, `CNPC_VStalker`,
`CNPC_VTaxiDriver`, `CNPC_VYukie`. The empty eight are published anyway, because they exist so
their class's four spaces have the right parents and the runtime's translation walks that chain;
a class with no unit would make the runtime invent a space rather than load one.

The 57th caller of the thunk, `0x1030f220`, is a generic `vdata/schedules/<name>.sch` loader with
no caller in the image and no shipped file. It is a dead door, recorded in the root's `deadDoors`
and published as no unit.

## The cut region

A byte of `dlls/vampire.dll` is a **unit span** exactly when it belongs to a NUL-terminated
printable-ASCII run in the image's string pool whose first whitespace-delimited token compares
equal to `Schedule` under `strcmpi` **and whose third token compares equal to `Tasks`**.

Both halves are retail's own acceptance test. `0x1030d850` reads the first token and, if it is not
`Schedule`, returns success and loads nothing; having read the schedule's name it then requires a
`Tasks` section and fails the text without one. So a run the rule admits is a run retail would have
compiled had it been fed, and a run it rejects is one retail would have ignored or refused.

The second half is not decoration. The image keeps the parser's **own diagnostics** in the same
pool — `"Schedule has invalid state ID '%s'"`, `"Schedule has invalid Memory ID '%s'"`,
`"Schedule not found"`, `"Schedule file: %s"`, `"Schedule %s, "` and eight more — and every one of
them opens with the token `Schedule` while being a format string rather than a program. Measured on
the pinned image the first half alone admits 704 runs; the full rule admits **691**, which is
exactly the set the 56 init bodies feed.

**The pool is `.data`, not `.rdata`.** The oracle's prose says `.rdata`; in the pinned image the
linker merged the read-only string pool into `.data`, and every one of the 691 fed texts resolves
there while `.rdata` admits none. The rule follows the bytes.

The rule reads the image alone and never asks which unit took what, which is what
`seam_map_unit_contract.md` requires of an executable-image seam.

The export enumerates that set from the section bytes, enumerates the set of texts the 56 init
bodies actually feed, and **refuses the seam unless the two sets are equal**. On the pinned image
they are, at 691 each. A run in the first
set and not the second is an unfed schedule text; a text in the second and not the first is a feed
the region rule does not admit. Either is reported with its virtual address and its first sixty
characters, and neither is published around. The banked fact this turns into a check is
`schedule-kernel.md`'s "matched by `.rdata` address, not by name: no text is fed by two owners and
none is left unfed".

The terminating NUL is **outside** the span. The parser is handed a `char*`; the terminator is the
root's byte, graded with the rest of the image.

## Source closure

| Unit | Source member | Role | Capsule |
|---|---|---|---|
| a space unit | `dlls/vampire.dll#ai/schedules/<space>/<name>.sch`, one per text, each with `span` | `schedule-text` | **declared and carried** |
| the root | `dlls/vampire.dll`, whole | `image` | **none** |

A member's path carries its corpus-relative deploy path after the `#`, which is what lets the
import lane split one string and know nothing else about this seam (the precedent is
`sound_script_glb`'s `f"{table.path}#{folded}"`). The image resolves UP-first like any member; no
shipped Unofficial Patch replaces `dlls/vampire.dll`, so it resolves retail loose in every install
this seam has seen.

The root declares no `capsule` key and carries no BIN chunk: reproducing a 7.86 MB engine binary
out of a GLB is not a thing any consumer of this corpus needs, and the amendment in
`seam_map_unit_contract.md` licenses the omission for exactly this case. The image's `sha256` and
`byteLength` are published, so a reader still knows which image the units were cut from; the
documented pin is `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`,
7,860,281 bytes, and a different digest is an `anomalies[] image-differs-from-pin` row rather than
a refusal.

## GLB structure

Every unit is scene-less and declares no accessor. A space unit's BIN chunk is the source capsule
alone — one `bufferView` per text, 4-byte aligned, named by that member's `capsule` row. The root
has no BIN chunk at all.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_ai_schedule"],
  "extensionsRequired": ["ELYSIUM_vtmb_ai_schedule"],
  "extensions": {
    "ELYSIUM_vtmb_ai_schedule": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "dependencies": [],
      "coverage": {},
      "spaces": {},
      "registrations": {},
      "texts": [],
      "records": [],
      "activities": [],
      "anomalies": [],
      "omissions": []
    }
  }
}
```

The root replaces `spaces` / `registrations` / `texts` / `records` / `activities` with
`partition`, `namespaces`, `squadSlots`, `vocabulary`, `classes`, `order`, `deadDoors` and
`census`.

## What a space unit carries

### `identity`

`asset`, `sourcePath` (`dlls/vampire.dll`), `sourcePolicy`, `className` (the owning class),
`classNames[]` (every class whose slot-580 getter answers this space, sorted) and `initBody` (the
init body's virtual address).

### `spaces`

The four `CAI_LocalIdSpace`s `CAI_LocalIdSpace::Init` (`0x102ea0e0`) sets up, keyed `schedule`,
`task`, `condition`, `squadSlot`. The first three sit `0x18` apart; the squad-slot space lives
elsewhere in the class's statics. Each row carries `address`, `namespace` (the global namespace
the space registers into), `parent` (the parent space's address, or null at a root), `parentUnit`
(the unit key that owns the parent, or null) and `initVa`.

**The parent is what the init body passes, and it is not the C++ base chain.** `CNPC_VTzimisce`
derives from `CNPC_VBaseBoss` but parents its spaces on `CAI_BaseNPCTroika`'s;
`CNPC_VFrenzyShadow` derives from `CNPC_VPlayerController` but parents on `CNPC_VVampire`'s. A
consumer that wants the space graph reads `parentUnit` and nothing else.

### `registrations`

`{schedule, task, condition, squadSlot}`, each an array of `{name, localId, sourceVa}` in the
order the body registers them. Recovered from literal immediates ahead of each append or register
call — the extractor reads operands and never executes a body.

Known shapes, all three published identically:

- the **literal** shape, one call per name, arguments pushed inline (`CAI_BaseNPC`);
- the **loop** shape, a stack-local pair vector built by `0x102c6a50` and then walked
  (every species; the worked example is `CNPC_VBrujah` `0x10367a40`, `SCHED_VBRUJAH_WALK` `0x158`
  at `0x10367ac2`);
- the **helper** shape, the four-argument `0x102bea80` / `0x102beab0` / `0x102beae0` trio that
  supplies the category string itself (`CAI_BaseNPCTroika`, and eleven of `CAI_BaseNPC`'s rows).

No class registers a local squad-slot name; every `squadSlot` array is empty. The two global
squad-slot names are the root's.

### `texts` and `records`

`texts[]` is one row per fed text — `order`, `name`, `file` (the deployed leaf), `stringVa`,
`sourceOffset`, `byteLength`, `feedVa` — in the order the init body feeds them, because retail
breaks on the first failure and everything after it never loads.

`records[]` is the decode: per text, the schedule `name` and `localId`, `tasks[]` as
`{name, operand}`, `interrupts[]` as `{name, inverted, globalOrdinal}` and `flags[]`. An operand
is `{form, spelling, …}` where `form` is `prefix` (with `prefix` and the resolver that answered),
`boolean`, or `number`. The decode is produced by a faithful port of `0x1030d850`'s tokenizer and
grammar, and **the export refuses to publish a text the retail parser would refuse** — retail
would have stopped that owner's load there, so publishing it would state a program the game never
had.

Two feed shapes deserve naming because a decoder gets them wrong by default. `CAI_BaseNPCTroika`
feeds in two blocks (`158 × 0x102c6920` then `116 × 0x102c65d0`), concatenated in address order.
`CAI_BaseNPC` feeds through a **pointer indirection** — 64 calls each reading its own cell of the
static table at `0x106034b8`..`0x106035b4` — and **the cells are not in feed order**, `FAIL` being
fed last. A decoder that walks the table instead of the call sites publishes the wrong order.

### `dependencies`

| Role | Produced by |
|---|---|
| `schedule-space-parent`, `task-space-parent`, `condition-space-parent`, `squadslot-space-parent` | each `Init` whose parent is not null, resolving to the parent's unit |
| `vocabulary` | every space unit, resolving to the root |
| `schedule`, `task`, `condition` | the unit that registered each name a text resolves through the chain; a self-reference keeps its row |
| `model` | a `Model:` operand, resolving to `vtmb:model:<path>` — the parser precaches it |

An unresolved parent **fails the unit**: it is the unit's own structure, not a cross-reference.
`Activity:` and `SOUND:` produce no row — there is no `vtmb:activity:` kind, and a `SOUND:` name
addresses a run-time table rather than anything below `sound/`.

### `coverage` and the byte ledger

`mapped`: `identity`, `sourceResolution`, `spaces`, `registrations`, `texts`, `records`,
`dependencies`. `unresolved` and `unsupported` are empty on a complete unit.

`typedUnidentified[]` carries an operand whose resolver answered but whose meaning is not in the
vocabulary, each keeping its `sourceOffset`: a `MiscFlag:` name outside the 22 (the resolver reads
0 silently), a bare token `_atof` turns into `0.0`, a `HintFlags:` token matching both `nearest`
and `random` (which warns and reads as nearest). These are retail's own non-failing quirks and are
recorded rather than smoothed.

The ledger is one row per text member, gapless over that text's span alone:

| Owner | Range |
|---|---|
| `records[i].keyword` | the `Schedule` token |
| `records[i].name` | the schedule name token |
| `records[i].tasksKeyword` | the `Tasks` token |
| `records[i].tasks[j].name` | one task name token |
| `records[i].tasks[j].operand` | one operand, the `prefix`, `:` and value tokens together |
| `records[i].interruptsKeyword`, `.interrupts[j]` | the `Interrupts` token and each condition, `!` included |
| `records[i].flagsKeyword`, `.flags[j]` | the `Flags` token and each flag |
| `comments[i]` | a `//` run through end of line |
| `whitespace` | the separators the engine tokenizer consumes |

Every token range is `mapped-text`; whitespace is `omitted-proven`.

## What the root carries

`partition[]` — one row per unit span (`unit`, `member`, `offset`, `length`) plus the named
non-unit rows, graded `omitted-proven` under `image.notDecodedAsContent`. The ledger is one row
over all 7,860,281 bytes and must account for every one of them. No `reserved-zero` or
`padding-zero` claim is made anywhere in it: the seam decodes no content outside the texts and
says so rather than asserting something about bytes it did not read.

`namespaces` — the three global namespaces `0x109203cc` (schedule), `0x109203d4` (task),
`0x109203dc` (condition) and the squad-slot one, each with the note that its id counter is
**seeded** at 1,000,000,000 and that a registered global id is that counter's value, not an
ordinal with an offset applied.

`squadSlots` — `SQUAD_SLOT_ATTACK1` = 1,000,000,000 and `SQUAD_SLOT_ATTACK2` = 1,000,000,001,
seeded globally by `0x10316e80`.

`vocabulary` — the thirteen operand tables with their resolver addresses and their quirks stated
as data: `state` (14 names, 12 unnamed), `memory` (17 masks), `path` (3), `goal` (5),
`hintFlags` (4, `"match": "substring"`), `npcFlag` (62, `"word2Marker": "0x80000000"`),
`miscFlag` (22, `"stores": "index"`, `"unknownReads": 0`), `expression` (2), `sto` (2), `dist`
(9 negative sentinels resolved at run time by slot 418), `mxtPhase` (4), `toMode` (5), and
`scheduleFlags` (`NONE` 0, `DELAY_INTERRUPTS` 1 — the only two the engine has). Every row is
verified against the image before publication: the name string is present at its stated address
and referenced by the stated resolver, and the value immediate appears in the resolver's body. A
row that fails verification refuses the unit.

`classes[]` — classname → unit key, the four shared pairs included, recovered by reading vtable
slot 580 and accepting only the one-line `MOV EAX, imm32 / RET` shape.

`order` — base, then Troika, then the species, with the recorded fact that species bodies load on
first touch in an order the oracle leaves unrecovered and that nothing observable depends on it,
since no space searches a sibling.

`census` — the counts, published rather than asserted so that a different image degrades to a
different census instead of a crash: parse call sites, init bodies, feeding owners, and the 691
attributed 64 (base) + 274 (Troika) + 353 (species).

## Import

```text
uv run elysium import ai-schedules
```

deploys into `Content/ElysiumCorpus/ai/schedules/`, reading the units alone:

```text
vocabulary.json          the root's vocabulary, namespaces, squad slots, class map and order
<space>/space.json       classNames, the four spaces with their parent unit, the registrations
<space>/<name>.sch       the capsule's bytes, verbatim
```

The `.sch` files are capsule bytes — the exact `.rdata` bytes retail's own parser read, proven by
digest at export and by read-back at deploy. The two `.json` files are **derived products**
(`seam_map_unit_contract.md` § "Derived products"): the registrations and the space parents are
recovered from call sites and no byte of the source spells them, so they cannot be capsule bytes
under any arrangement. Each is `json.dumps(body, indent=1, sort_keys=True)` plus a trailing
newline, UTF-8, a pure function of the unit's own extension root.

One file per **text**, not per owner: retail's failure is per text, so "which text failed to load"
is a filename in the log rather than a line number in a blob.

The runtime reads the deployed corpus and never the GLB, which is the rule
`docs/specs/0018-world-ai-infrastructure/spec.md` § Scope states for every lane.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] registered-name-with-no-text` | a schedule name registered in a space that no text declares; `CAI_BaseNPC` has four |
| `anomalies[] text-name-not-registered` | a text declaring a name its owner's space never registered |
| `anomalies[] image-differs-from-pin` | the source image's digest is not the documented pin |
| `anomalies[] duplicate-schedule-name` | two texts in one space declaring one name — retail's first failure row |
| `omissions[] whitespace` | the token separators, per text |
| `omissions[] image.notDecodedAsContent` | the root's non-unit bytes |

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows, and every text it declares
appears once in `texts`, once in `records`, once in `sourceResolution.members` and once in the
root's `partition`.

Export-time validation re-scans `.rdata` for `Schedule`-shaped runs and re-derives the feed census
from a fresh call-site pass, then compares span for span and owner for owner against what the
writer published. Standalone validation re-parses each capsule and requires its `Schedule <name>`
token to equal the published `records[0].name`, re-digests the ledger ranges, and checks that the
root declares no capsule while every space unit declares one.

A third, independent check lives outside the seam:
`uv run elysium research schedule_owner_survey --check` re-derives the per-owner table from the
image and diffs it against the committed table in `schedule-kernel.md` § "The schedule owners and
their registrations", exiting non-zero on any difference. That is the document-against-image check
the oracle's table would otherwise never get.

## What this seam does not recover

Each is a published row, not a silence:

- **The `SOUND:` array's contents.** `DAT_1073dc3c` / `DAT_1073dc40` are filled at load from the
  VSound table, so a `SOUND:` operand's numeric id does not exist in the image. The operand's
  **name** is the datum and is published verbatim; `vocabulary.sound` states
  `"resolution": "runtime"`.
- **`Model:` symbol numbers**, interned at run time through `DAT_10936b74`. The model path is
  published and produces the dependency row; the 16-bit symbol id is recorded absent.
- **`Activity:` ids**, which are statically recoverable but from the activity registry
  `DAT_1090fbe0` — another survey's surface and no unit kind of this corpus. The name is
  published; no dependency row.
- **The run-time order the first-touch species bodies fire in**, recorded in `order` with the
  reason it is unobservable.
- **A caller for `0x1030f220`**, recorded in `deadDoors`.
