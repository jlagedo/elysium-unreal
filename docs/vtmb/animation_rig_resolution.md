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
| a per-bone `matrix3x4` applied to position, rotation copied | Unreal `OrientAndScale` translation retargeting, rotation never retargeted |
| bank pose composed hop by hop up the include DAG | one hop: bank sequence retargeted straight onto the playing mesh |
| every bank a distinct rig, remapped pairwise | banks partitioned into families, one `USkeleton` per family |

Each body gets its own `USkeleton` seeded from its own container alone. Banks are grouped by
`formats/eskm.rig_families`, greedily, into families whose tree is the growing union of their
members; each bank sequence is built once on its family skeleton and names a `RetargetSource`
holding its own donor bind pose. `ElysiumSkeletalBuild.cpp` sets
`EBoneTranslationRetargetingMode::OrientAndScale` from the root with `bChildrenToo`.

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
  `OrientAndScale` retargets translation only.
- **Undriven bones agree.** Retail writes the including model's bind pose; an Unreal sequence
  with no track for a bone leaves it on the playing mesh's reference pose.
- **One hop composes retail's five on the scale.** Each hop's length ratio multiplies out to the
  direct one exactly. The rotation is a separate question and is not settled by the same argument —
  see "What one hop proves, and what it does not" below.

### The residual: a bone whose bind sits near the origin

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

**The guard is therefore not a test against zero.** A pelvis binding `0.14496 cm` (`0.05707 in`)
from its parent satisfies it, so `EPS ≥ 0.00326 in²`. The copy/transform bracket narrows it from the
other side: the largest bind separation retail **copied** is `0.210946 cm` (`0.08305 in`) and the
smallest it **transformed** is `0.321769 cm` (`0.126681 in`). A single **0.1 inch** constant sits in
that gap and would explain both tests at once — a hypothesis the corpus is consistent with rather
than a recovered value, since one constant explaining two thresholds is suggestive and not proof.

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
but nearer than retail's epsilon is **translated by retail and retargeted by us** — the two guards
simply disagree about what counts as the origin. Swept over the 293 exported character bodies,
**71 carry such a bone**: 70 of them `Bip01 Pelvis`, the rest a `Bip01 Spine1` or a prop bone. The
cluster sits at `0.14411`–`0.14496 cm` against the male banks' `2.96802`, so `OrientAndScale`
scales the bank's pelvis translation by `0.04884` where retail adds a constant offset.

**The figures above are bind-space bounds; they are not what the player sees.** The pelvis is a
translation bone, so the error is not confined to it — every bone below and above inherits the
displacement. Measured against a live retail session by `Elysium.Content.RigPose`, on
`tremere_male_armor_3` and comparing only frames where a base clip is the whole drawn pose:

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
  which writes a pure `b − a` offset. That epsilon is far wider than Unreal's own: a pelvis merely
  close to its parent — `0.145 cm` — is inside it, so the length-ratio rule is correct only outside
  the band, and 71 of 293 character bodies sit in it.
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

## Open

Everything this document leaves unresolved, and what would resolve each.

**The origin branch has no implementation, and the divergence is measured rather than bounded.**
No stock translation-retargeting mode carries it, so a fix means a translation rule with retail's
three branches — copy when the binds agree, a pure `b − a` offset when either bind sits inside
retail's epsilon of the origin, otherwise axis-angle scaled by the length ratio — applied where
`OrientAndScale` is applied today. It reaches **71 of 293 character bodies**, and on an affected
body it displaces the whole pose by a median of `6.090 cm` rather than offsetting one bone; the
at-origin sub-case on ten non-humanoid bodies is the smaller half of it. `Elysium.Content.RigPose`
holds the defect to a recorded envelope and would accept a fix without being edited, so the
instrument exists and the decision is an owner call.

**Retail's epsilon is bracketed, not read.** The corpus places it between `0.08305 in` and
`0.126681 in` and a single `0.1 inch` constant would satisfy both bounds, but the constant itself is
inferred from where retail copied and transformed rather than recovered from the builder. Reading
`vampire.dll 0x100c67b0`'s comparand would settle it, and a translation rule wants the real value
rather than a plausible one.

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

The recipes hook 22 and 23 targets across `vampire.dll` and `client.dll`. Five were added for
this work — `vampire.lookup_sequence`, `vampire.get_model_ptr`,
`client.resolve_sequence_owner`, `client.accumulate_sequence_pose` and, for the fighting cast,
`vampire.npc_choose_melee_sequence`.

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
