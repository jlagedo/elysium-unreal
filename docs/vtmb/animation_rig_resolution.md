# Animation rig resolution

How VtMB turns a sequence number into a clip, and how the clip's bones reach the body that
plays it. `animation_and_movers.md` owns the sequence descriptors, the compressed tracks and
the activity ladder; `mdl_v2531.md` owns the on-disk struct layout. This document owns the
**resolution**: the global sequence number space, the include-model walk that resolves a
number to an owning bank, and the bone correspondence the engine builds between an including
model and the banks it pulls in. It also carries the NPC arm of the melee selector, the
comparison against **this repository's own implementation**, the one measured divergence
between them, the ways of asking these questions that return a wrong answer, everything still
open, and the procedure that regenerates every number in it.

The load-bearing fact is that **the shipped files carry these tables allocated and empty** — the
engine fills them at model load, so a survey that reads the on-disk records reads sentinels
(`0x7fffffff`, `0`, `-1`, `0xffff`) rather than behaviour.

**The values themselves are not a runtime secret.** Both the numbering and the bone
correspondence are pure functions of the shipped files, because the builder's own algorithm is:
accumulate `NumLocalSeq`@272 over the include DAG, and match bones by `stricmp` on name. Re-running
that offline against the install reproduces the live figures — `nosferatu_female_armor_0` answers
1,534 sequence numbers, `tremere_male_armor_3` and `brujah_male_armor_3` 1,584 each, all at include
depth five, against the capture's own "roughly 1,535 to 1,584" and "five levels". What genuinely
needs a live process is much narrower: the *matrices* the builder derives, the pose-parameter
resolution, and anything about which sequences a body actually commits. Read the counts below as
what a session witnessed; where a question is decidable from the files, it is decided from the files
and said so.

## The global sequence number space

`CBaseAnimating::LookupSequence` walks three model slots in order and accumulates each one's
sequence count into a single flat index space:

| slot | source |
|---|---|
| `-1` | the entity's own model |
| `0` | the first extra animation-model slot |
| `1` | the second extra animation-model slot |

`CBaseAnimating::GetModelPtr(slot)` answers each one. For slot `-1` it resolves the entity's
own model; for `0` and `1` it reads a per-entity model index and answers `0` when the slot is
unset. Within a slot the count is `NumLocalSeq`@272 when the model includes nothing, and
otherwise the last include group's `seq_base` plus its `seq_count`.

**The two extra slots are unset on every entity observed** — bodies, weapons, viewmodels and
scenery alike. Every bank a body animates from is reached through the include tree instead.
The slot mechanism is live code with a working index space; no shipped content uses it. This
is a capture observation over two sessions' entity populations — 48 bodies, weapons, viewmodels
and scenery — not a corpus-wide proof.

## The include tree and the resolution walk

`FUN_10089c40` (`client.dll` `+0x89c40`) resolves a number against a model and evaluates it:

1. If the number is below the model's own `NumLocalSeq`@272, the model owns it — evaluate
   locally and stop.
2. Otherwise find the include group whose runtime range contains it —
   `seq_base <= index < seq_base + seq_count` — and recurse into that group's bank with the
   index rebased by `seq_base`.
3. On the way back, walk the group's bone remap to carry the bank's pose onto the including
   model's bones.
4. After the local evaluation, recurse once per `numautolayers`@660 entry, which is why one
   pose build carries several sequence contributions.

The tree is a DAG resolved transitively (`animation_and_movers.md` A.7). Observed resolution
depth for a player body reaches **five levels**, and a body answers roughly 1,535 (female) to
1,584 (male) distinct sequence numbers of which only a handful are its own.

## `StudioModelGroup`: what is authored and what is filled at load

The 116-byte group record (`mdl_v2531.md` owns the disk layout) carries four runtime fields
this document depends on. Their on-disk values are sentinels:

| offset | field | on disk | after model load |
|---|---|---|---|
| `+0x00` | filename index, group-relative | authored | unchanged |
| `+0x04` | label index / model pointer | `0` | the loaded model |
| `+0x08` | `seq_base` | `0x7fffffff` | the group's first global sequence number |
| `+0x0C` | `seq_count` | `0` | how many numbers the group owns |
| `+0x10` | bone remap offset, group-relative | authored | unchanged |
| `+0x14` | pose-parameter map, global → local, 24 `uint16` | `0xffff` | resolved slot indices |
| `+0x44` | pose-parameter map, local → global, 24 `uint16` | `0xffff` | resolved slot indices |

The remap **offset** at `+0x10` is authored and the table it points at is allocated in the
file, but every record in it reads `-1` on disk. The table is filled at load. A survey that
counts on-disk remap records therefore counts empty records, and any conclusion drawn from
their contents describes the sentinel rather than the behaviour.

## The bone remap record

`animation_and_movers.md` A.4b owns the 56-byte record layout, the builder that fills it, and the
matrix derivation. The builder is confirmed at `vampire.dll 0x100c67b0` and `engine.dll 0x2000ce40`
by string xref; the `client.dll 0x1008cfa0` address the doc set also cites is not confirmable in the
present corpus — that module has no indexed function there and its copy of the `chained model %s`
string has no referrer — so read the two mirrors as the citation. In summary, so the
measurements below are readable: `+0x00` is the source bone in the bank and negative writes the
including model's own bind pose; `+0x02` is the branch selector; `+0x03` selects copying the
position over transforming it; `+0x04` and `+0x06` are the mapped parent and the nearest common
ancestor; `+0x08` is a `matrix3x4` applied to the position by `FUN_10107f80` as an affine point
transform, never to the rotation, which is copied verbatim.

Three shorts, two bytes and a 48-byte matrix account for the 56-byte stride exactly — `+0x02` and `+0x03` are read as bytes by both the builder and the consumer, not as one short.

## What the runtime tables contain

Read from a live process over two sessions. The second was a fight in `sm_hub_1` and covers
roughly five times the cast of the first, which is what makes the agreements below worth more
than one body's worth of evidence:

| | player session | fight session |
|---|---|---|
| loaded studio headers | 104 | 152 |
| include groups, all carrying a remap | 86 | 93 |
| remap records | 5,552 | 5,873 |
| mapped / undriven | 4,815 / 737 | 5,241 / 632 |
| bodies reaching a bank | 9 | 48 |
| animation banks | 17 | 46 |
| ownership chains | 176 | 739 |
| activity ids recovered | 84 | 216 |

**Correspondence is by identical bone name.** Every mapped record in both sessions — 4,815 and
then 5,241, across 46 banks and 48 bodies — names a bank bone whose name matches the body bone
it drives, case-insensitively. **Zero conflicts, in either.** A name-matched rig reproduces the
correspondence exactly, and the wider cast did not weaken that.

**Banks drive a subset of the body's bones.** 737 records carry `-1`. The undriven set is
dominated by prop and helper bones — `Bat`, `gerber`, `Cylinder01`, `Sledgehammer`, `handle`,
`tire iron`, `bush hook` — followed by the anatomy helpers `Bip01 L/R Wrist`, `Bicep`,
`Shoulder`, `Ankle` and `Quadricep`. A Brujah body of 73 bones is driven on 60 by
`runotherspc_pcidles_allsequences.mdl` and on 53 by `frenzy.mdl`, which leaves every weapon
bone to the reference pose.

**A bank may carry more bones than the body.** Groups appear with 78, 79, 80, 81 and 96 bank
bones mapping into a 60-bone including model; the surplus is dropped.

**`sub == 1` is a real per-bone path, and it scales with skeletal mismatch.** 131 of 4,815
mapped records take it in the first session and 96 of 5,241 in the second, across 15 groups
either way, with an identical scale range of `0.5412`–`2.8795`. Most groups take it on two bones — the
root `Bip01` and `Bip01 Pelvis`. A pair whose skeletons differ more takes it far more widely:
`allsequences.mdl` ← `g2.mdl` maps 59 bones and transforms 57 of them, and its bank carries 96
bones against the body's 59.

The measured matrices confirm A.4b's derivation — axis-angle from the two bind positions,
scaled by their length ratio. Across all 132 records carrying one, the three row norms are
equal to within `1e-4`, and dividing by that common norm leaves a matrix orthonormal to
`1.3e-5`: a similarity transform, no shear and no per-axis scale. What it carries follows the
bone. A finger bone reads a pure uniform scale — `0.70423` on the
diagonal, zero translation — which is a bone-length retarget and nothing else. A bone whose
bind orientation also differs reads scale times rotation, such as `Bip01 L Finger0` at scale
`0.967` with off-diagonals near `0.064`. Translation appears only on the root and pelvis, at
magnitudes up to `1.23`, and reverses sign between banks: `Bip01 Pelvis` reads an identity
rotation with a pure X translation of `±1.21591` depending on which bank drives it. Eight of
the 131 records carry scale exactly `1`, so the path is taken for a rotation or a translation
alone, not only for a length difference.

**The chain-rebase path is reachable, and the corpus answers how often.** Both sessions find the
same live record taking `mode == 1`: `alsequences.mdl` ← `baseball.mdl`, bone `handle` ← `handle`,
with `chain_start == chain_end == 29`. Re-deriving the builder's own `+0x02`/`+0x04`/`+0x06` logic
offline over **every** include pair in the install finds **four** such records across 443 distinct
(including model, bank) pairs, all with equal shorts:

| including model | bank | bone | `+0x04` / `+0x06` |
|---|---|---|---|
| `sheriff_battle.mdl` | `npc_allsequences.mdl` | `Cylinder01` | `-1` / `-1` |
| `mistidance.mdl` | `stripper3.mdl` | `Bone05` | `9` / `9` |
| `mistidance.mdl` | `stripper3.mdl` | `Bone03` | `9` / `9` |
| `alsequences.mdl` | `baseball.mdl` | `handle` | `29` / `29` |

The session witnessed one because one is what loaded. **No shipped pair carries unequal shorts**, so
the hang `animation_and_movers.md` A.4b reads in that branch's second loop is latent rather than
live — see **Open** for what the builder can emit in principle.

**Pose parameters are remapped per group.** The `+0x44` local → global map resolves to real
indices at load — a player body's first group reads slots drawn from `{0, 1, 2, 3}` with the
rest left unset. An unset slot means the group contributes no value for that pose parameter.
A.4b owns the pair and the case-insensitive global namespace they accumulate into.

## The NPC melee selector

Slot 331 is filled by **254 classes across five implementations**, not two. The two that matter
here are `CBasePlayer::ChooseMeleeAttackSequence` at `0x10160f90` — shared by `CHL2_Player` — and
`CBaseCombatCharacter::ChooseMeleeAttackSequence` at `0x10347180`, held by **83** classes:
every `CNPC_V*`, `CAI_*`, `CGenericNPC`, `CCine*`, `CItemContainer*` and `CNPCMaker*`. The other
169 take neither, splitting across `CBaseMoneyObject` (103), `CBaseCombatWeapon` (49) and
`CWeaponRanged` (17). A player-only capture never reaches the NPC arm, which is why it went unhooked
until a session with a fighting cast.

**The signature is not the shape the rest of the selection chain has:**

```
uint __thiscall ChooseMeleeAttackSequence(this, void *weapon, CBaseEntity *enemy,
                                          int activity, int *outSequence)
```

`eax` carries a **bool in AL**, not an index, and the chosen sequence is written through the
fourth argument. `*outSequence` is set to `-1` on entry and stays `-1` when nothing is chosen,
so a refusal is a value rather than a return code. A capture that reads `eax` as a sequence
records a truncated boolean — and the upper bits are undefined on every failing exit
(`x & 0xffffff00`), so only AL may be read.

**The converse does not hold.** A non-negative pick is written to `*outSequence` *before* the bool
is decided: AL is set to 1 only when the winning flag word satisfies `(f&3) && (f&4) && (f&8)`, so
the function can return **false with a valid sequence already in the out-parameter**. A hook that
treats `false` as "nothing was chosen" mislabels those.

The body scores candidates geometrically rather than drawing by weight: it walks
`GetSequencesForActivity`'s candidates, and for each one reads the descriptor's own
`+0x2cc`/`+0x2d0` range pair against the enemy distance, sweeps the movement through
`thunk_FUN_102e3450`, tests the swing boxes at `+0x2c0`/`+700` against the enemy's bounds, and
accumulates a flag word per candidate. It then calls `FUN_10348100` — the picker — repeatedly
against a fixed preference order (`7, 5, 6, 3, 1, 2, 4, 0`, twice, the first pass with an
extra bit set), taking the first pass that returns a non-negative index. With no enemy it takes
a shorter path: the picker once with `0x10`, then once with `0`.

**Observed live**, a tire-iron cop and a clawed Tremere:

| body | activity | chosen | owner | clip |
|---|---|---|---|---|
| `Regular_Cop` | `2911` | `1094` | `shared/male/tireiron.mdl` | `tireiron_attack_slash` |
| `Regular_Cop` | `2911` | `1097` | `shared/male/tireiron.mdl` | `tireiron_attack_hack2` |
| `Regular_Cop` | `2911` | `1099` | `shared/male/tireiron.mdl` | `tireiron_attack_W1` |
| `Regular_Cop` | `2911` | `-1` | — | refused |
| `Regular_Cop` | `2913` | `1102` | `shared/male/tireiron.mdl` | `tireiron_attack_medcombo` |
| `Regular_Cop` | `2913` | `1103` | `shared/male/tireiron.mdl` | `tireiron_attack_lowcombo` |
| `tremere_female_Armor_3` | `2239` | `674` | `shared/female/claws.mdl` | `claws_attack_far` |

**These rows cannot carry the conclusion they were first read as carrying.** The recipe keys this
target on `(ecx, activity, chosen_sequence)`, but `chosen_sequence` is a `when: "leave"` field and
the agent deferred a record only when its key named `return_value` — so the key part was filtered
out at enter and stringified to the literal `"null"`. The effective key was `(entity, activity)`:
**one record per body per activity for the whole session**. The four `Regular_Cop`/`2911` rows are
therefore four different cops, not one cop answering four ways, and different entities answering
differently is equally consistent with a weighted draw. The keying is fixed (`agent.js` now defers
on any leave-phase key part, and `frida_probe` refuses a key whose phase the recipe cannot satisfy),
so a re-capture will settle it; until then the table shows that the selector *can* refuse and that
its answers vary across a cast, and nothing more.

**What is not established**: which candidate flag each bit of the preference order selects, and
therefore which geometric condition produced any one of these answers. The flag word is built from
the range test, the sweep and the box test; mapping bit to condition needs either the picker decoded
or a capture that records the per-candidate flag array beside the enemy's relative position.

## Activity enums are per-process, not per-file

`StudioSeqDesc.activity`@12 stays `-1` on disk; the DLL resolves `szactivitynameindex`@4 to an
enum at model load, so an activity id exists only in a running process and the **name is the
durable key**. A capture that records the enum a selection requested, joined against the
descriptor the selection committed, recovers id/name pairs. Two distinct ids can carry the
same literal — `182` and `613` both commit descriptors labelled `ACT_VM_IDLE` on different
models — so a single observation names a pair without establishing that the id is global.

## How Elysium solves the same problem

This section describes **this repository's implementation**, not VtMB. It sits here rather than
in `docs/architecture/animation-architecture.md` because the comparison against the capture is
the point of the document; the architecture file owns the design itself.

### The shape of our answer

| VtMB | Elysium |
|---|---|
| one flat sequence-number space per body, built at load | clips addressed by label per owner stem, resolved through the include DAG at export |
| a bone remap table per (including model, bank) pair, built at load | bone correspondence by name, at bake and at play |
| a per-bone `matrix3x4` applied to position, rotation copied | one translation-only `FAnimNode_ElysiumBankRemap` after each composed bank closure |
| bank pose composed hop by hop up the include DAG | one hop: the bank closure remapped straight onto the playing mesh |
| every bank a distinct rig, remapped pairwise | banks partitioned into families, one `USkeleton` per family |

Each body gets its own `USkeleton` seeded from its own container alone. Banks are grouped by
`formats/eskm.rig_families`, greedily, into families whose tree is the growing union of their
members; each bank sequence is built once on its family skeleton and names a `RetargetSource`
holding its own donor bind pose. `ElysiumSkeletalBuild.cpp` sets translation retargeting to
`Animation`, and the graph remaps each completed base or overlay closure once from that named donor
pose to the playing mesh.

### What the capture confirms

Measured by comparing retail's captured matrices against this repository's own exported bind
poses, bone for bone, on `nosferatu_female_armor_0` ← `frenzy` and `tremere_male_armor_3` ←
`frenzy`:

- **The bone correspondence is the same.** Retail matches by `stricmp`; we match case-folded.
  Zero conflicts in 4,815 mapped records — every mapped bone pairs with an identically named one.
- **The scale rule is the same, to five decimal places**, on 19 of 20 transformed bones. Retail's
  scale equals `|body bone bind| / |bank bone bind|` computed from our own exported containers:

  | bone | retail | ours |
  |---|---|---|
  | `Bip01 L Finger01` | `0.70423` | `0.70423` |
  | `Bip01 L Finger41` | `1.50120` | `1.50120` |
  | `Bip01 L Finger3` | `0.98474` | `0.98474` |
  | `Bip01` | `0.99993` | `0.99993` |

- **Rotation is untouched on both sides.** Retail copies the bank's quaternion verbatim;
  `FAnimNode_ElysiumBankRemap` changes translation only.
- **Undriven bones agree.** Retail writes the including model's bind pose; an Unreal sequence
  with no track for a bone leaves it on the playing mesh's reference pose.
- **One hop composes retail's five on the scale.** Each hop's length ratio multiplies out to the
  direct one exactly. The rotation is a separate question and is not settled by the same argument —
  see "What one hop proves, and what it does not" below.

### The origin branch that stock retargeting missed

**The pelvis diverges, and it is the largest measured divergence in the reproduction.** Two
separate pairs are involved and they must not be read as one: the finger scales below reproduce
exactly, and the pelvis does not. A scale measured on one (body, bank) pair carries nothing about
another, and joining the two reads as agreement where there is none.

| bone | pair | bank bind | body bind | ratio |
|---|---|---|---|---|
| `Bip01 L Finger01` | female, `nosferatu_female_armor_0` ← `female/frenzy` | `4.62698` | `3.25844` | `0.70423` |
| `Bip01 L Finger41` | female | `1.48890` | `2.23514` | `1.50121` |
| `Bip01 L Finger3` | female | `8.45028` | `8.32130` | `0.98474` |
| `Bip01` | male, `tremere_male_armor_3` ← `male/frenzy` | `99.01932` | `99.01218` | `0.99993` |
| `Bip01 Pelvis` | male | `2.96802` | `0.14496` | `0.04884` |
| `Bip01 Pelvis` | **female** | `1.22112` | `1.22112` | **`1.00000`** |

Retail's builder decides its branch on **the origin** rather than on proximity between the two
binds (`vampire.dll 0x100c67b0`). On the female pair the two binds are identical, so retail copies
(`+0x03` clear) and Unreal skips the bone entirely (`BoneContainer.cpp` refuses a cache entry when
the binds agree within `0.001`). Both sides answer `1.00000`.

**On the male pair retail takes the pure-translation branch, and the captured matrix says so
without ambiguity.** Every `Bip01 Pelvis` record on `tremere_Male_Armor_2/3 ← frenzy` and
`← runotherspc_pcidles_allsequences` reads three row norms of exactly `1.00000` — identity rotation,
unit scale — beside a translation of `1.225514 in` = `3.11281 cm`, which is our own `|b − a|` for
that pair to five decimal places. `toreador_Male_Armor_3`, `goth_male` and `Bertram` carry the same
record; `security_guard ← fat_male` carries its own value, `1.167083 in` = `2.96439 cm`, and that
too equals our `|b − a|`. So the branch is not in question, and neither is our bind data — only
which rule is applied to it.

**The guard is recovered, not inferred.** `vampire.dll 0x100c67b0` compares squared Source-space
lengths against `0.01`, so the linear threshold is **0.1 inch** (`0.254 cm`). The observable branch
structure is: binds within the copy threshold copy; exactly one bind within the origin threshold
takes the pure `b - a` translation; both binds within the origin threshold copy; otherwise the
shortest-arc orientation and length ratio map the position. The both-small copy path is distinct
from the exactly-one-small translation path.

**Two populations sit behind this, and only one of them is small.**

The first is the case where the bind is *at* the origin. Unreal declines to retarget there —
`BoneContainer.cpp`'s `IsNearlyZero(SourceLen * TargetLen)` — and passes the bank's translation
through verbatim, where retail applies a pure `b − a` offset. Swept over all 653 exported
containers:

| | |
|---|---|
| containers carrying an origin-bind non-root bone | **16** of 653 |
| distinct bone names | **4** |
| `Bip01 Pelvis` / `Bip02 Pelvis` / `Bip03 Pelvis` | 10 / 3 / 3 containers |
| `bush hook` | 6 containers |
| bodies affected | `swat`, `swat2`, `swat3`, `werewolf`, `wolf_form_2`, `cat`, `dog_guard`, `creation1_full`, `creation1_scripted_both`, `creation1_unused` |
| worst-case error | a constant offset of at most the other side's bind length, ≤ 3 cm |

`ElysiumSkeletalBuild.cpp::RegisterRetargetSource` names each one at bake time.

**The second population is the band between the two engines' thresholds, and it is neither small
nor confined to non-humanoids.** A bone binding further from its parent than Unreal's `0.001`
but nearer than retail's epsilon was **translated by retail and retargeted by the former stock
path** — the two guards disagree about what counts as the origin. Swept over the 293 exported
character bodies,
**71 carry such a bone**: 70 of them `Bip01 Pelvis`, the rest a `Bip01 Spine1` or a prop bone. The
cluster sits at `0.14411`–`0.14496 cm` against the male banks' `2.96802`, so `OrientAndScale`
scales the bank's pelvis translation by `0.04884` where retail adds a constant offset.

**The figures above are bind-space exposure, not the current running graph.** The pelvis is a
translation bone, so an uncorrected error is not confined to it — every child inherits the
displacement. `Elysium.Content.RigPose` evaluates sequences directly and therefore still measures
that pre-graph exposure on `tremere_male_armor_3`; it is not an end-to-end bank-remap test:

| median, base clip alone | bodies outside the band | `tremere_male_armor_3` |
|---|---|---|
| radius from the root | `0.003 cm` | **`6.090 cm`** |
| joint angle | `0.005°` | measured with the radius |
| `Bip01 Pelvis` radius | `0.000 cm` | **`4.652 cm`** (p90 `36.815`, max `53.433`) |
| lower body radius | `0.007 cm` | **`3.734 cm`** |
| bone segment length | `0.000 cm` | `0.000 cm` (p99 `0.103`) |

`malkavian_male_armor_0` (pelvis bind `2.96811`) and `tremere_male_armor_0` (`2.96804`) read the
left-hand column through the same test and the same code path, which is what attributes the
right-hand column to the bind rather than to the body. Bone lengths hold either way, so the
skeleton is **displaced, not stretched** — hip sway and vertical bob are scaled to a twentieth
while every limb keeps its proportions.

### What one hop proves, and what it does not

**One hop is equivalent to retail's five on the scale, which is the part that composes.**
`|c|/|b| · |b|/|a| = |c|/|a|` exactly, so a chain of length retargets and the direct one agree to
floating point.

**The rotation does not compose that way, and the earlier claim that it did was wrong.** Each hop's
rotation is a *shortest-arc* rotation between two bind directions; composing two of them yields a
rotation that maps the first direction onto the last, but with an added twist about that axis — the
spherical excess of the triangle the three bind directions span. It equals the direct shortest-arc
rotation only when the three are coplanar. Sampled over 21,190 two-hop compositions across the
139-bone family: median `0.000°`, p99 `5.19°`, maximum `165.86°` on `Bip01 Pelvis`, whose bind flips
sign between the male and female families and makes the shortest arc degenerate.

Those are arbitrary container pairs rather than real include chains, so read the tail as an upper
bound on what a chain *could* produce rather than as an error anyone has observed. What is settled
is that the argument — "each hop maps one bind frame to the next, so the product is the direct
map" — proves the scale and not the rotation, and that the measurement which confirmed it only ever
compared scales.

### Bank families

The bank partition is by **tree shape only** — `rig_families` compares bone names and parents
and never reads a bind pose. Over the shipped corpus that yields 9 families across 360 banks,
and the distribution is lopsided: 257 banks share one 139-bone union skeleton, 93 share a
75-bone one, and the remaining 10 sit in small families.

That is sound, but for a reason worth stating rather than assuming.
`ElysiumSkeletalBuild.cpp::RegisterRetargetSource` seeds each retarget source from the family
skeleton's reference pose and then **overwrites every bone the donor container declares** with
that donor's own bind. A clip can only animate bones its own bank declares, so the retarget
never reads a foreign member's bind for a bone it is retargeting. The family skeleton's own
reference pose is therefore metadata that no evaluated bone consults.

The exposure is real — the 257-member family's union skeleton is 139 bones against a smallest donor
of 53, so 86 of its reference-pose entries belong to other members — and the invariant now holds
three ways rather than one. It is structural (the bake enumerates tracks from the donor's own bone
list), it is **asserted** (a track naming a bone outside that list fails the bake by name rather than
being dropped), and it is **measured**: 6,981 bank clips across all 360 containers, zero
violations.

### A silent loss in the export chain

`npc_index.json` and `npc_manifest.json` each declare **372** banks. The container directory
holds **360**. `families.json`, which the bake reads, is built from the containers that exist,
so the twelve banks with no container are absent from the partition, absent from every family,
and absent from the bake — and the bake's own guard cannot see them, because it validates
family members against the same file listing that omitted them.

**The mechanism is upstream of the container writer.** `npc_export` drops a charsheet fidget owned
by another stem as another clan's, and those twelve banks uniquely own nothing else — their
`ragdoll` label is already claimed by an earlier model in DFS order. After the drop no clip map
names them, so `character_source_plan` never plans them, no container task is created, and nothing
downstream can miss what was never asked for. (`UE_mdl_skeletal.write_bank` does return `None` with
no warning when `_anim_section` yields no clip, and that is its own defect — but the task graph
checks declared outputs, so a *planned* bank returning `None` fails the run loudly rather than
vanishing.)

The twelve are the per-clan PC banks, `character/shared/{male,female}/pc/{br,ma,no,to,tr,ve}.mdl`,
three to five sequences each. **Their loss is harmless**: every PC body carries its own clan
charsheet fidgets in its own container — verified on Tremere, Malkavian, Brujah, Ventrue,
Toreador and Nosferatu — and `npc_export` deliberately drops a charsheet fidget owned by
another stem as another clan's. The banks are duplicates of what the bodies already own. The
defect is the **detection gap**, not the lost data: a bank that mattered would disappear the
same way, and nothing in the chain would fail.

### What the reproduction does not carry

Each of these is a named absence rather than an unknown: the retail behaviour is recorded, and
this runtime does something else.

**The cast still draws its gait by weight.** Both of retail's pickers exist here —
`ElysiumAnimResolve::PickHeaviest` reproduces `FUN_104280f0` (maximum `actweight`, a strict `<` so
ties keep the lowest global sequence number, no randomness) and `PickWeighted` reproduces
`FUN_10427fc0` (raw weights, and a uniform draw when they sum to zero). The **fork** between them
is reproduced only on the player: retail latches the heaviest picker on a commanded state change
through entity flag `0x40000000` (`animation_and_movers.md`), and this runtime commits the
canonical clip on the player's locomotion request in its place. NPC gaits are requested by AI
schedules whose call sites choose a picker per task — the corpus shows `CAI_BaseNPC` thunks
calling both — so no mapping is grounded and none is invented; the cast draws. A capture keyed to
the requesting schedule would settle which arm each task takes.

**The weighted draw is seeded, not random.** Retail calls `RandomInt(0, sum-1)`; this runtime
draws from `HashCombineFast(stem, variant)` so a body resolves the same clip every load. That is a
deliberate divergence for testability, and it is the reason the weighted arm cannot be asserted
against a capture the way the heaviest arm can — only its candidate set and its weights are
comparable, never its answer.

**The `0x80` include-shadowing rule is not modelled.** Retail's candidate collector stores the
flags of the last locally matched descriptor and searches the include tree only if that word's low
byte carries `0x80` (`FUN_10427df0`, confirmed at instruction level: `MOV [ESP+0x18],flags` …
`MOV AL,[ESP+0x18]; TEST AL,AL; JNS`). A local match without the bit therefore **shadows every
bank's candidates for that activity**. Nothing in this repository's export or resolver carries it,
and no witnessed selection distinguishes the two behaviours, so it is unreproduced rather than
contradicted. A body whose own container declares an activity a bank also declares would
distinguish them.

**The two engines disagree about bind equality, and the disagreement is live.** Retail's copy
threshold sits near `0.1 in`; `BoneContainer.cpp` refuses a retarget cache entry only when the
binds agree within `0.001 cm`. Six bones on `nosferatu_female_armor_0 ← frenzy` fall in the gap —
`Bip01 L/R Finger2` (`0.2109 cm`), `L/R Clavicle` (`0.1794`), `L/R Finger31` (`0.0800`) — and are
copied by retail while this runtime retargets them. This is the same threshold the pelvis section
brackets, seen on bones small enough that the consequence is a fraction of a millimetre.

**The overlay subsystem does not exist here.** `CBaseAnimatingOverlay`'s four game-pushed layers
have no counterpart in this runtime, which is why an armed body composes a strict subset of
retail's channels — see "A composed pose is a subset" below. This is the largest missing
mechanism in the reproduction, and it is a subsystem rather than a rule. Its contract is recovered:
the record, the slot count, the lifecycle, the weight envelope and the two producers are stated in
"The overlay contract" below.

**The composed pose is unmeasured.** `Elysium.Content.RigPose` evaluates one clip per captured
frame, so a frame retail built from several contributions is compared against a single clip by
construction — which is why its layered-frame figures describe the layers rather than a defect.
Closing it means driving `ABP_ElysiumBiped` on a real component in a ticking world and feeding it
retail's own captured channel set, which separates two questions the current instruments cannot:
whether the graph composes correctly given the right inputs, and whether the runtime can produce
those inputs at all.

### A composed pose is a subset, and the missing half comes from outside the studio data

`Elysium.Content.RigLayers` compares, per captured frame, the channels retail accumulated against
the channels this runtime arms for the same request. Over 1,718 frames: **1,113 are short at least
one channel retail played, and none arms a channel retail did not.** The unreachable set is every
`_attack_layer`, `_attack_delta` and `_reload_layer` across all five weapon families — 1,154 of
1,230 missing instances; `_aim_layer` and `_bobble_delta` are armed correctly. Removing the
committed base by its own sequence number, 955 frames need a second overlay and 239 a second
additive.

**The autolayer closure does not reach the missing family *from a gait*.** Read from
`move_and_ranged.mdl`'s own records (`numautolayers`@660, `autolayerindex`@664,
**`mstudioautolayer_t` stride 4** — each entry is a bare `int` sequence index and nothing else; the
runtime addresses them at `base + i*4` in `FUN_10089c40` / `FUN_100c25c0`, and the record has no
room for a flags field):

| sequence | `numautolayers` | reaches |
|---|---|---|
| `supershotgun_aggressive_walk` | 2 | `supershotgun_aim_layer` (flags `0x0`), `supershotgun_bobble_delta` (flags `0x14`) |
| `supershotgun_aim_layer` | 0 | — |
| `supershotgun_bobble_delta` | 0 | — |
| `supershotgun_attack_layer` | 2 | `supershotgun_aim_layer`, `supershotgun_attack_delta` |

A gait's transitive closure is aim and bobble and nothing else, so no walk of the studio data
reaches `_attack_layer` or `_reload_layer` **from a gait** — which is the finding that matters here,
and it stands. But `_attack_delta` **is** an autolayer, of `_attack_layer`, on 13 hosts per bank:
the attack layer brings its own aim pose and its own delta, which is why the two co-occur 1:1 in
every captured frame. `_reload_layer` genuinely declares none. The channels this runtime arms from
the base closure are exactly the two the gait offers; the attack family is armed from somewhere
else, and arrives with its own closure attached.

**That somewhere else is `CBaseAnimatingOverlay`, and it is a subsystem rather than a rule.**
`vampire.dll` carries the class with `m_AnimOverlay[]` in its datamap, per-layer `m_nSequence` and
`m_fSequenceFinished` fields, its own vtable and datamap builder, and a `DevMsg` reading
`CBaseAnimatingOverlay::AddGesture…`. That is Source's animation **overlay** system: a stateful
stack of layers game code pushes, each carrying its own sequence, weight, playback rate and
lifetime, and faded independently of the pose graph. It is a **peer** of the studio autolayer
mechanism, not part of it — which is why an autolayer walk cannot find its members however deep it
goes.

It also accounts for the one observation an autolayer cannot: a channel's weight falling
`0.923 → 0.02` across consecutive frames. **An autolayer's composition weight is the DLL constant
`1.0f`** — pushed literally at `client.dll 0x1008a0ce` = `vampire.dll 0x100c2a4e`, the same
instruction at `+0x48E` of both copies of the walk — multiplied only by the target animation's
per-bone `weight`@0 mask, which is binary `{0.0, 1.0}` over all 736,208 shipped records. So an
autolayer contributes at exactly 1.0 or is skipped, and it can never ramp. An overlay fades because
its own `StudioFrameAdvance` recomputes a smoothstep from its own cycle. The `0.923 → 0.02` fall is
an overlay's; nothing in the studio data can produce it.

### The overlay contract

The layer array is **four records of 48 bytes** at `this+0x734`, named by the class's own datamap:

| offset in record | field | offset in record | field |
|---|---|---|---|
| `+0x00` | `m_fFlags` | `+0x18` | `m_flWeightMax` |
| `+0x04` | `m_fSequenceFinished` | `+0x1C` | `m_flBlendIn` |
| `+0x08` | `m_nSequence` | `+0x20` | `m_flBlendOut` |
| `+0x0C` | `m_flCycle` | `+0x24` | `m_nActivity` |
| `+0x10` | `m_flPlaybackRate` | `+0x28` | `m_bAutoKillWhenFinished` |
| `+0x14` | `m_flWeight` | `+0x2C` | `m_flLastEventCheck` |

All twelve fields of all four slots are declared save/restore. Whether they are networked is a
separate question the datamap cannot answer, and it is not answered here.

**Four slots, and exhaustion is refusal.** The datamap declares `m_AnimOverlay_0..3`, and
`m_Flinch_0` begins at `0x7F4` = `0x734 + 4 × 0x30` exactly, so nothing further fits.
`AllocateLayer` (`0x10099470`) scans from `GetFirstGestureLayer` (vfunc `+0x42c`) — which answers
`0` for every class in the hierarchy, so no slot is reserved — and takes the lowest slot whose
`m_flWeight` is zero, returning `-1` when all four are held. `AddGesture` propagates that `-1`.
There is no eviction and no priority displacement.

**A layer's defaults are the contract a reproduction must match.** `SetLayer` (vfunc `+0x430`,
`0x10099020`) writes `m_flWeight = 0.1`, `m_flWeightMax = 1.0`, `m_flBlendIn = m_flBlendOut = 0.2`,
`m_flPlaybackRate = 1.0`, and zeroes the cycle, the finished flag and `m_flLastEventCheck`. The
`0.1` is load-bearing: it is what makes the slot read as occupied against `AllocateLayer`'s
zero-weight test. **`m_fFlags` is not written and keeps whatever the previous tenant left.** When
the sequence's studio `flags@8` carries `0x2` (SNAP), both blend times are set to zero, so a snap
sequence gets no envelope at all.

**Weight rides the layer's own cycle, and this is what separates an overlay from an autolayer.**
The per-layer advance (`0x10098830`) recomputes it every tick; no caller writes it:

```
cycle += SequenceCycleRate(m_nSequence) × m_flPlaybackRate × dt
w = 1
if (m_flBlendIn  && cycle < m_flBlendIn)        w = cycle / m_flBlendIn
if (m_flBlendOut && cycle > 1 - m_flBlendOut)   w = (1 - cycle) / m_flBlendOut
w = 3w² − 2w³
m_flWeight = min(w, m_flWeightMax)
```

With the default `m_flBlendOut` of `0.2` the weight falls from one to zero over the last fifth of
the cycle on a smoothstep, which is the `0.923 → 0.02` fade the capture records. An autolayer's
weight is a function of the *owning* sequence's cycle or of a pose parameter; an overlay's is a
function of its own independently advancing cycle, and that is the observable difference between
them. The spline's leading coefficient reads as a folded constant (`_DAT_10450010`) and is
**inferred** as `3.0` from the `3w² − 2w³` shape rather than read; the envelope-branch guard
constant `_DAT_1045001c` is likewise unread, and with both blend times zero the inner tests are
no-ops either way.

**Death is weight-zeroing.** The cycle clamps to `1.0` on a non-looping sequence and wraps on a
looping one, and on reaching `1.0` sets `m_fSequenceFinished`. In the owner loop (`0x10098bb0`) a
layer that is live, finished and auto-kill has `m_flWeight` set to zero — which returns the slot to
the pool — and vfunc `+0x1c0` is called with `(layer index, m_nActivity)` as a completion
notification. **A finished layer whose auto-kill flag is clear holds its slot at cycle 1.0 only
when its envelope leaves it live** — which means only a SNAP clip. With the shipped `blendOut = 0.2`
the envelope at cycle 1.0 evaluates to `(1.0 - 1.0) / 0.2 = 0`, then `3*0 - 2*0 = 0` exactly, so the
layer's own out-ramp zeroes its weight on the tick it finishes and `AllocateLayer` sees the slot free
from the next one. Auto-kill therefore decides two things and not a third: whether the slot is
released one tick earlier, and whether `+0x1c0` fires. It does not decide whether the slot is
recoverable. `SetLayer` zeroes both blend times for a clip carrying `flags@8 & 0x2`, and *that* is
the case where a non-auto-kill layer pins its slot forever at weight `min(1.0, WeightMax)`.

**There is no ordering.** No priority or order field exists in the record. Composition runs by slot
index `0..3` and `AllocateLayer` answers the lowest free slot, so a freed slot is reused by the next
push and **layer order is not stable over time** — it is neither authored nor durably
insertion-ordered.

**The producers are asymmetric, and this is the third player/cast fork in this system.** The cast
pushes through `AddGesture` (`0x100991b0`) into a dynamically allocated slot with an auto-kill flag,
and `AddGesture` selects its sequence with `SelectWeightedSequence` rather than
`SelectHeaviestSequence`. The player does not use `AddGesture` at all: its layer arrives as the
**second argument to the player activity commit** (`0x101644f0`), where `0` clears the channel, `-1`
leaves it, and anything else is translated and handed to `FUN_1015fbb0`, which writes **slot 0**
directly through vfunc `+0x430`. A current/next queue in the `CBaseCombatCharacter` datamap fields
`m_aCurWpnActivity` and `m_aNextWpnActivity` drives it, and `CBasePlayer::vfunc103` resets both to
`-1`.

**No new family-resolution machinery is needed for either arm.** `CAI_BaseNPC::TranslateActivity`
(`0x10271ff0`) is structurally the ladder `animation_and_movers.md` already records — pre-translation,
weapon translation, an alternation loop capped at five, an availability probe, and the terminal
`ACT_RUN → ACT_WALK`. The player path calls the same two virtuals inline before committing its
layer.

### Where overlays enter the pose, and how they blend

The client pose build (`FUN_100979b0`) composes in this order, everything converging on the same
accumulate:

| # | stage | weight |
|---|---|---|
| 1 | the base sequence | `1.0` |
| 2 | `m_AnimOverlay[0..3]`, in slot order | the layer's own |
| 3 | a global autoplay pass over sequences carrying `flags@8 & 0x8` | `1.0`, hardcoded |
| 4 | `m_Flinch[0..2]` | a linear ramp |
| 5 | a virtual at `+0x1e0` | — |

**Autolayers nest inside every accumulate rather than forming a stage.** The resolution walk
evaluates a sequence and then recurses once per `numautolayers`@660 entry at a hardcoded weight of
`1.0`, so the base *and each overlay* each bring their own closure. This is what produces the
multiplicity of one to eight identical repeated contributions the capture records, and why
`supershotgun_aim_layer` appears beside `supershotgun_attack_layer` — the attack layer's own closure
names it.

**The blend operation is the sequence's own `flags@8`, never the caller's choice.** With `0x4`
clear a layer replaces — the running rotation slerps toward it by the weight and the position lerps;
with `0x4` set it is additive, and **`0x10` selects between two additive variants**. That second
variant is not modelled in this repository.

**Masking is the sequence's own per-bone weight list**, not a bone root: `seqdesc+0x38` indexes
`0x48`-byte records at `studiohdr+0x10c`, and the effective per-bone weight is the layer weight
times the bone's weight, skipped below an epsilon. Being authored per clip, it corresponds to a
clip-derived blend mask rather than to anything the caller supplies.

**Weights are clamped to `[0,1]` and are never normalised across layers.** Each layer moves the
running pose toward itself by its own weight or adds to it; weights summing past one simply let the
later layers dominate.

### `m_Flinch` is a second, separate stack

Immediately after the overlay array, `m_Flinch[3]` at `+0x7F4` holds three 28-byte records —
`nSequence`, `nLatch`, `flFadeIn`, `flFadeOut`, `nPoseParamIndex`, `flPoseParamValue`,
`flExpireTime` — gated by `m_bNoFlinch` at `+0x730`. It is not part of `m_AnimOverlay`: it expires
on a time rather than on an auto-kill flag, ramps linearly rather than on a spline, and carries a
**per-layer pose-parameter override** the overlay records have no equivalent of. That override is
the shape a directional reaction grid needs, so the correspondence with the reaction stream this
project already models is worth checking; it is not asserted here.

### What the overlay contract still leaves open

All five of the questions this section once listed are now answered; they are recorded here as
resolved rather than deleted, because each one's answer is load-bearing.

- **Networking — settled.** `DT_BaseAnimatingOverlay` exists (server builder `vampire.dll 0x10098120`,
  client receive `client.dll 0x10097480`), 35 props. Per layer it sends **exactly four of the twelve
  fields**: `sequence` (11 bits), `cycle` (10 bits, `[0,1]`, roundup), `playbackrate` (8 bits,
  `[-4,12]`), `weight` (8 bits, `[0,1]`). The envelope, both blend times, `WeightMax`, the activity
  identity, the auto-kill flag and `m_flLastEventCheck` are **server-only**. The client receives a
  weight quantised to ~1/255 and knows nothing about how it was produced. **Nothing is predicted** —
  the client class has no `StudioFrameAdvance` over the array, no cycle advance and no `AllocateLayer`.
  The client record is a different object: `+0x790`, stride `0xE4 = 228`, three 76-byte interpolation
  samples with the newest at `+0x98`, shifted by `C_BaseAnimatingOverlay` slot 70. Only the weight is
  interpolated (`(1-t)*prev + t*cur`); on a sequence change the previous weight snaps to 0 or 1
  against a 0.5 threshold.
- **`m_fFlags` — settled: it has no semantics in 2531.** Zero writers anywhere in either module; the
  only reader is `Dump`. `SetLayer` does not clear it, so a slot keeps its previous tenant's value.
- **The two additive variants — settled.** `flags@8 & 0x10` clear selects `FUN_10088d00`
  (`vampire.dll 0x100c1230`), which multiplies the scaled delta on the **left**; set selects
  `FUN_10088d60` (`0x100c12b0`), which multiplies it on the **right**. The two bodies are
  instruction-identical apart from the operand order at the `QuaternionMult` call. Every one of the
  118 shipped `_delta` sequences carries `0x14`, so retail always post-multiplies. The pre-multiply
  side is not dead code, though — `CalcBoneAdj` calls it at weight 1.0 for every rotational bone
  controller, a path that is itself inert because no shipped model declares a controller.
- **The three unread virtuals — settled.** `+0x438` is `IsPlayingGesture(Activity)`, `+0x43c` is
  `FindGestureLayer(Activity)` (matching on `weight != 0 && activity != -1 && activity == wanted`),
  and `+0x1c0` is `OnLayerFinished(int iLayer, int nActivity)`. The base is empty and there is
  **exactly one override in the whole hierarchy**: `CBasePlayer` at `0x1015fcd0`, which is the weapon
  activity queue's drain. `AddGesture` consults the first pair *before* allocating and **returns the
  existing layer unchanged** when the activity is already playing — it does not restart it, and it
  does not take a second slot.
- **Whether the server evaluates the stack — settled: it does.** `CBaseAnimatingOverlay` slot 255
  (`+0x3fc`, `vampire.dll 0x10098eb0`) runs `CalcPose` for the base and then, for each slot with
  `weight > 0`, a `CalcPose` + `SlerpBones` at that weight, followed by autoplay and the bone
  controllers. Server hitboxes, attachments and `GetBoneTransform` therefore all see the composed
  overlay pose, not the base sequence alone.

## What a reproduction has to carry

- The sequence numbering and the bone correspondence cannot be baked from the install alone.
  Either the load-time build is reproduced, or the correspondence is derived by bone name,
  which the capture shows is equivalent on this corpus.
- A bone a bank does not drive holds the **body's reference pose**, not the previous
  contribution and not the bank's own rest value.
- A `sub == 1` bone's position crosses a similarity transform — uniform scale, rotation,
  translation — before it reaches the body. Translation is confined to the root and pelvis;
  elsewhere the transform is a bone-length retarget, and on strongly mismatched pairs it
  applies to most of the skeleton. Rotation is never transformed, only copied.
- A label alone does not name a clip; the tree it resolved through does
  (`animation_and_movers.md` A.7).
- A bone whose bind sits **within retail's epsilon of the origin** needs retail's origin branch,
  which writes a pure `b − a` offset only when exactly one bind is small; two small binds copy.
  That epsilon is `0.1 inch`, far wider than Unreal's own: a pelvis merely close to its parent —
  `0.145 cm` — is inside it, so the length-ratio rule is correct only outside the band, and 71 of
  293 character bodies sit in it.
- An NPC picks a melee attack geometrically against its enemy and may refuse outright; the
  player draws by weight. Both arms of the fork have to exist, and a refusal is an answer.

## Traps

Ways of asking these questions that return a confident wrong answer.

**Surveying the shipped files answers about sentinels.** The bone remap, the group sequence
ranges and the pose-parameter maps are all allocated and empty on disk. A corpus sweep over
them counts records that read `-1` and `0x7fffffff` and concludes the mechanism is unused. That
is how the chain-rebase branch came to be recorded as unreachable.

**Reading `eax` as the answer.** `ChooseMeleeAttackSequence` returns a bool and writes its
sequence through an out-parameter. Any hook on this chain has to be read against the
disassembly rather than by analogy with its neighbours.

**A change key compared against the previous call.** With more than one body, consecutive calls
belong to different entities, so such a key suppresses nothing and the budget goes on restating
the same few tuples. A key over a crowd must be partitioned by the entity.

**Reading a deferred hook's arguments on the way out.** Once a call returns, its argument area
is below the caller's stack pointer, and the interceptor's own return path writes there. The
window has to be read on the way in.

**Assuming a bone's transform is a pure length retarget.** It is a similarity, and on a bone whose
bind sits within retail's epsilon of the origin retail switches to a pure translation instead.
Reading only the first row of the matrix shows a plausible scale and hides the branch; the three row
norms together name it, because the translation branch writes all three as exactly `1.00000`.

**Reading one bone's numbers off two different pairs.** A scale is a property of a (body, bank)
pair, not of a bone name, and the corpus offers the same bone at wildly different ratios: `Bip01
Pelvis` is `1.00000` on the female pair and `0.04884` on the male one. Joining a captured matrix to
a computed ratio without carrying the pair through manufactures a divergence out of two correct
measurements — which is how the section above came to claim one.

**Counting overlay slots from `Dump`.** `CBaseAnimatingOverlay::Dump` (`0x10098a90`) iterates
**24** records at stride `0x30` over an array that holds **four**, walking through `m_Flinch` and
well past the end of it. The slot count is four, fixed three ways: the datamap declares
`m_AnimOverlay_0..3`, `m_Flinch_0` begins exactly where a fifth record would, and both
`AllocateLayer` and `RemoveAllGestures` bound at four.

**Looking for a game-pushed layer in the studio data.** An overlay is a peer of the autolayer
mechanism, not a member of it, so no walk of `numautolayers`@660 finds one however deep it goes —
the gait's closure reaches `_aim_layer` and `_bobble_delta` and stops. The tell that separates the
two at runtime is the weight: an autolayer's follows the owning sequence's cycle or a pose
parameter, an overlay's follows its own independently advancing cycle.

## Open

Everything this document leaves unresolved, and what would resolve each.

**Direct retail capture of a differing-bind body remains open.** The runtime implements the exact
builder rule and the Tremere running-graph identity falls from `3.113 cm` to `0.009 cm` over 480
frames while the all-copy Malkavian control remains `0.009 cm`. The complete retail capture corpus,
however, contains pose and layer oracles together only for the two all-copy Malkavians. A capture
of Ash or a Tremere carrying both oracles would close the live evidence boundary; until then the
differing-bind result rests on disassembly, synthetic branch coverage, compositor identity and the
unchanged control.

**A declared bank can leave no trace.** `npc_index.json` declares 372 banks and 360 containers
exist. The plan↔disk edge *is* guarded — `character_source_plan` hard-fails for a planned container
that did not land — but the plan is derived from the NPC clip maps, which the charsheet-fidget drop
has already filtered, so `manifest["banks"]` is compared against nothing anywhere in the pipeline,
the bake or the repository policy check. A cross-check of the manifest's bank list against the
derived plan, run before the partition, would fail the run instead. The twelve banks currently lost
are harmless duplicates, so this is a latent defect rather than a live one.

**The chain-rebase branch's semantics are undecoded; its hang is latent, not unreachable.** The
builder's write path is now read. `+0x04` is the mapped parent `P` and `+0x06` is the first
ancestor-or-self of the matched source bone `S` that is also an ancestor-or-self of `P` — so the two
are equal **iff** `P` is an ancestor-or-self of `S`, and nothing in the builder enforces that. A
cross-branch reparent between an including model and its bank emits unequal shorts, as does a
multi-root bank where the two chains never meet (`+0x06` stays `-1`). The sharp form: the second
loop exists to compose `P`'s chain up to the common ancestor, which is needed exactly when `P` is
not an ancestor of `S` — and that is exactly the input on which A.4b reads it as never terminating.
The branch is correct only on the inputs where its work is a no-op.

No shipped pair emits one: 443 include pairs, four branch records, all equal. So the hang cannot be
reached from this install, and what stays undecoded is the branch's *semantics* — one terminating
record does not exercise them.

**The NPC selector's candidate flags are not mapped.** The preference order
`7, 5, 6, 3, 1, 2, 4, 0` is read from the binary, but which geometric condition each bit
encodes is not. A capture recording the per-candidate flag array beside the enemy's relative
position, or `FUN_10348100` decoded, would close it.

**Activity ids are observed pairs, not a table.** Each id/name pair comes from one committed
descriptor. Two ids can carry the same literal — `182` and `613` both commit `ACT_VM_IDLE` —
so nothing here establishes that an id is global rather than per-model, and a pair seen once is
not a pair confirmed. The registration order at model load would settle it.

**Sampling can break an ownership chain.** The fight recipe processes one call in eight for
`client.resolve_sequence_owner`, which can catch a parent without its child: 14 of 739 chains
came back incomplete. `sequence_map.csv` resolves ownership independently, so nothing is lost in
practice, but a claim about the *chain* rather than about ownership must not be made from a
sampled session.

**The counts are observations, not coverage — but less of that is now unavoidable.** Two sessions,
one map, one cast, one owner's install, so every *witnessed* number should be read as a lower bound
on variety. What needed a session, though, is narrower than it looked: the sequence numbering, the
bone correspondence and the `+0x02` branch's reachability are all pure functions of the shipped
files and have been swept corpus-wide offline (443 include pairs; 653 exported containers). What
still needs a live process is the derived **matrices**, the pose-parameter resolution, and which
sequences a body actually commits. A corpus-wide *runtime* pass — every PC body and every NPC model
loaded in turn, remap read for each — would raise those three from observations to coverage.

## Capturing this again

Everything above is regenerable from the owner's own install. Three stages, and the middle one
**must run while the game is still open** — it reads live memory addressed by the first stage's
own records.

### 1. The session

Start VtMB and reach a map with weapons available. Find its process id, then attach for the
length of the session you intend to play. **Two recipes**, and they differ only in how their
keys are partitioned:

| recipe | for | keys |
|---|---|---|
| `life_rig_resolution` | one body, weapons and models swapped by hand | selection targets keep call order |
| `life_rig_chaos` | a fighting cast | selection targets are a per-entity census, four targets sampled |

```powershell
Get-Process Vampire | Select-Object Id
uv run elysium research frida_probe attach --pid <PID> `
  --recipe life_rig_chaos --duration-seconds 300
```

Attach begins instantly, so start playing at once. The duration is capped at 600 seconds. Fire
melee and ranged weapons, swap weapons, and swap body and sex; every distinct resolution is
recorded once and repeats are suppressed, so coverage comes from variety rather than from time.

`frida_probe launch` exists and spawns its own instance from
`Unofficial_Patch/cfg/elysium_load.cfg`, but it does not attach to a game already running.

### 2. The runtime tables, before the game closes

```powershell
uv run elysium research capture_rig_remap --pid <PID>
```

Reads the bone remap, the group sequence ranges and the pose-parameter maps out of process for
every studio header the session named. Defaults to the newest session. **A closed game cannot
answer**, and the addresses are only valid for the process that produced them.

### 3. The guide

```powershell
uv run elysium research analyze_rig_resolution
```

Resolves every committed sequence number to its owning bank and clip label, using stage 2's
range table where an ownership chain was suppressed by a change key.

### 4. The drawn pose

A separate session and a separate recipe, because it answers a different question: what the bones
actually held, rather than which clip was chosen or how it was remapped.

```powershell
uv run elysium research frida_probe attach --pid <PID> --recipe life_rig_pose --duration-seconds 200
uv run elysium research analyze_rig_pose
```

`client.setup_bones` runs per entity per frame, so it is sampled and its records are read from the
entity's own bone array rather than from the caller's output buffer — that buffer is one shared
scratch allocation every entity writes through, and identifies nothing. A pose record names its own
model from the entity's cached studio header, so it needs no join against the entity census.
Measured at 60 fps with the session's own frame counter.

**What the session must contain is variety of pose, not variety of weapon.** One clean sample of
each composition shape — a base clip standing, a gait, a crouch, an aim layer, a one-shot, a
reaction — held still for a few seconds so several cycle values land. A body swap costs nothing and
covers a second rig; a body whose pelvis binds inside retail's epsilon is what exercises the
divergence above, and `tremere_male_armor_2`, `tremere_male_armor_3` and `security_guard` are among
them.

### What lands, and where

Under `$ELYSIUM_WORK_ROOT/research/frida/<timestamp>-attach-life_rig_resolution/`:

| file | contents |
|---|---|
| `events.jsonl` | the raw hook stream, one JSON record per line |
| `manifest.json` | session identity, hook install results, agent summary and drop counts |
| `rig_remap.csv` | one row per (group, bone): source bone, mode, sub, chain bounds, `matrix3x4` |
| `rig_remap.json` | per-group summary: bones mapped, name conflicts, modes, pose-parameter map |
| `sequence_map.csv` | every global sequence number a body answers → bank, local index, label |
| `resolution.csv` | the guide: action → activity → sequence → owning bank → clip |
| `ownership.csv` | one row per resolution chain, with the full model#index path |
| `blend.csv` | per pose contribution: model, sequence, weight |
| `activities.csv` | recovered activity id → name pairs with observation counts |

The evidence is game-derived and stays under `ELYSIUM_WORK_ROOT`; nothing here is committed.

### What it costs to run, and why

A hook's cost is per **call**, not per record, so no change key reduces it — a suppressed call
has already paid for the interception. Four targets answer per entity per frame and account for
almost all of it. Measured on one body walking `sm_hub_1`:

| | hits / 20 s | emitted |
|---|---|---|
| `vampire.get_model_ptr` | 126,918 | 838 |
| `client.get_studio_hdr` | 99,294 | 453 |
| `client.resolve_sequence_owner` | 22,206 | 103 |
| everything else | 8,269 | 365 |

88% of the cost for 1,291 useful records. Three changes made the session playable, in order of
payoff:

- **Sampling.** Those four targets process one call in 32 (8 for the owner resolver, 4 for the
  accumulator) and return from the rest before touching the CPU context. A census still
  converges, because an entity on screen is asked thousands of times. `frida_probe` refuses a
  sampled target that carries no `on_first` key: sampling an ordered target would punch holes
  that read as absences.
- **Key-first evaluation.** The change key is computed from the smallest read that can answer
  it, and a suppressed call returns before a record, a full stack window or a module lookup
  exists.
- **A module-lookup cache.** `Process.findModuleByAddress` ran on every hook entry; modules are
  page-aligned and never move, so one lookup per 64 KB page answers every address in it.

Together: **7.7 → 56 frames a second** with the same coverage. `vampire.player_item_post_frame`
fires once per frame and its hit count is in every manifest, so each session carries its own
frame-rate measurement.

### The tools

| path | role |
|---|---|
| `research/tooling/capture/frida/recipes/life_rig_resolution.json` | the single-body recipe: targets, field reads, change keys |
| `research/tooling/capture/frida/recipes/life_rig_chaos.json` | the fighting-cast recipe: per-entity census keys and sample rates |
| `research/tooling/capture/frida/agent.js` | the Frida agent — `cstr` reads, `return` field base, `on_change` / `on_first` keys |
| `research/tooling/capture/frida_probe.py` | recipe validation, attach/launch/collect |
| `research/tooling/capture/contracts/binary_profiles.json` | hash-pinned hook targets and prologues |
| `research/tooling/capture/frida/recipes/life_rig_pose.json` | the pose recipe: sampled `setup_bones`, its own model name, the contribution clock |
| `research/tooling/capture/capture_rig_remap.py` | the out-of-process runtime-table reader |
| `research/tooling/capture/analyze_rig_resolution.py` | the offline resolver and guide writer |
| `research/tooling/capture/analyze_rig_pose.py` | the pose fixture writer: frames, contributions, bones by name |
| `research/tooling/capture/analyze_rig_layers.py` | the layer fixture writer: a frame's channels, deduplicated, additive-marked |
| `research/tooling/capture/rig_parity.py` | the diff: numbering, ownership, retarget and sequence numbers against the session |

The recipes hook 22 and 23 targets across `vampire.dll` and `client.dll`. Five were added for
this work — `vampire.lookup_sequence`, `vampire.get_model_ptr`,
`client.resolve_sequence_owner`, `client.accumulate_sequence_pose` and, for the fighting cast,
`vampire.npc_choose_melee_sequence`.

### The instruments, and what each proves

Four automation tests read these fixtures. Each is scoped to one link in the chain, so a failure
names the link rather than the symptom; none of them needs the game running.

| test | proves | cannot prove |
|---|---|---|
| `Elysium.Content.RigOracle` | for every selection the session committed, the export names the same owning bank, activity and clip, and the resolver reaches it under the picker retail used | anything about a clip the session never committed; the weighted arm's *answer*, only its candidates |
| `Elysium.Content.RigRetarget` | our bone correspondence, our identity cases and our similarity transforms agree with the captured remap tables | anything about an evaluated pose — it compares bind-time correspondence only |
| `Elysium.Content.RigPose` | the drawn pose, bone for bone against a live retail frame, wherever a base clip is the whole pose | a composed frame: it evaluates one clip, so a layered frame is compared against a composition by construction |
| `Elysium.Content.RigLayers` | which channels retail accumulated against which this runtime arms for the same request | whether the graph *composes* those channels correctly once armed |

`rig_parity` is the offline sibling: it compares the export's include-DAG numbering, per-label
ownership and sequence numbers against the same sessions without an editor, and it is the cheapest
thing to run when a numbering or ownership question comes up.

### Volume, and why the keys matter

A target that answers per entity per frame never repeats consecutively, because entities
interleave. A key compared only against the previous call therefore suppresses nothing:
`client.get_studio_hdr` alone spent 41,913 events in a twenty-second run. The four rig and
ownership targets carry `on_first` keys instead — one record per distinct tuple for the whole
session — which brought the same twenty seconds to 5,992 events. A four-minute session records
about 77,000 events against a 300,000 budget, with 1.5 million suppressed and nothing capped.

`research/tooling/capture/frida/recipes/smoke_change.json` proves the three mechanisms the
recipe depends on — a bounded string read, a read off the return pointer at leave, and a
change key suppressing a repeat — against synthetic IA-32 fixtures, never the retail install:

```powershell
uv run elysium research frida_probe smoke --recipe smoke_change
```

## Provenance

The runtime tables are read out of process with `ReadProcessMemory` by
`research/tooling/capture/capture_rig_remap.py`, against studio-header addresses named by a
`life_rig_resolution` Frida session (`research/tooling/capture/frida/recipes/`). Neither
attaches a debugger, suspends a thread, nor writes to the game. The selection chain and the
committed sequence numbers come from the same session and are read by
`research/tooling/capture/analyze_rig_resolution.py`. Captured evidence stays under
`ELYSIUM_WORK_ROOT` and is regenerable by re-running those tools.

Counts here describe two sessions on one map. The first exercised one body at a time — three
Malkavian female, three Brujah male, three Malkavian male — through melee, firearm and unarmed
weapons. The second was a fight in `sm_hub_1` with the cast fighting back, and reached 48
bodies and 46 banks. Both ran against one owner's install. They are observations, not a
corpus-wide sweep; see **Open** for what that limits.
