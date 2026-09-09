# 6b — The Troika cone override

Planning baseline: `f950cef5`, 2026-09-08; clean tree before this document. Scope:
[spec.md](spec.md), checkbox **6b**. This is an implementation plan; no runtime changes,
builds, or live acceptance were performed. The checkbox stays open.

## Outcome

Every virtual `FInViewCone` dispatch on a VtMB NPC runs the Troika override
(`CAI_BaseNPCTroika` slot 363, `0x102b4540`), not the base body 6a already ported. With the two
recovered sense-off switches at default 0, `thug_1`'s 157° cone is unchanged. Setting
`npc_ignore_senses` blinds every sense channel those bytes gate; setting `npc_ignore_player`
blinds only player-owned sight and hearing. A follower any-angle skip is named as a seam and
does not fire: it is not "see the player from behind", and the tutorial witness never reaches it.

## Findings that determine the work

| Seam | Current evidence | Implementation consequence |
|---|---|---|
| Virtual dispatch | Slot 363 on `CAI_BaseNPCTroika` and every shipped NPC class, including `CNPC_VVampire`. Direct callers of slot 363 include Look (`0x1030fb90`), the sound sweep (`0x102b1cd0`), and several other sites. Base `CBaseCombatCharacter::FInViewCone` `0x10326750` is what 6a ported. | Look admission and 10a's `SEE_SOUND_SOURCE` stranger arm must run the Troika wrapper. Do not put that wrapper on every point-only call. |
| 6a body | `FElysiumNpcSenses::IsInViewCone(Npc, TargetCm, Scalar)` is `FinViewCone3dNew` (`0x103264d0`): 3-D forward from Source pitch/`-yaw`, reject `dot < 0`, apex `ViewConeBodyOffsetCm`, cosine × target scalar vs `0.2`. Header comment "Horizontal only" is stale. | Keep this as step 5 / the point overload. Do not re-derive FOV or the 2-D mode (`0x10936f74`). |
| Two globals | Spec/oracle name `DAT_10924fb9` as `ai_ignoreplayers`. **That string is not in the image.** Constructors `0x10088c70` / `0x10088d70` register ConCommands `npc_ignore_player` (`0x106c9628`) and `npc_ignore_senses` (`0x106c96a0`), help `"NPCs will not hear or see the player"` / `"NPCs will not hear or see anything"`. Callbacks toggle `DAT_10924fb9` / `DAT_10924fba` and `DevMsg("Npc%s ignoring player.  Npc%s ignoring all senses\n")`. Default both bytes 0. Flags 0. | Expose the two bytes as ConVars under the recovered names (spec job: ConVars). Do not invent `ai_ignoreplayers`. Do not save them. |
| Other readers of the same bytes | `QuerySeeEntity` slot 468 `0x102b38b0`; `QueryHearSound` slot 467 `0x102b35b0`; Troika `FVisible` slot 201 `0x102b4630`. Yukie 363/201 and Werewolf `FVisible` also read them. | Wire the three generic Troika gates in this checkbox. Help strings name hearing. Leaving them only on the cone would make `npc_ignore_player` a sight-only switch. Yukie/Werewolf overrides stay out. |
| QuerySeeEntity in the port | Inlined in `TickSight`: any player; non-players only `D_HT`/`D_FR`. Missing the two globals and the frenzy-friend reject (`frenzied & 0x800`). | Add the two globals here, before cone. Frenzy-friend is 16c; leave that arm a named seam answering "not a friend". |
| Arm 4 (spec's "any-angle accept") | Slot 293 is `FUN_102c5470`: resolve `m_hFollowerBoss` (`+0x647c`) and return `boss+0x9c`. `+0x9c` is the cached `CBaseCombatCharacter*` (`PrecacheSoundTable` `0x1009d460` calls `IsMale` on it; choreo `CAMERAMOVE` uses `tgtEnt+0x9c` the same way). `+0x98` is the Troika self-pointer (`CAI_BaseNPCTroika` ctor `0x1028d3bc` `MOV [ESI+0x98], ESI`; `animation_and_movers.md`). `+0x6279` is `m_bInPlayerLOS` (datamap; writer `SetPlayerLOS` `0x10291610`). A player does not write `+0x98`, so looking at the player cannot take arm 4. Arm 4 is: follower of the closest player, looking at an NPC whose `m_bInPlayerLOS` is set, skip the cone. Spec/oracle "accepts that player at any angle" is a misread. | Do not invent a behind-the-player skip. Code the predicate against seams that currently answer nothing (no follower boss, no target NPC overlay). It will not fire until 16a and the LOS producer exist. `thug_1` has no `follower_boss`. |
| Closest-player cache | Port `TickSight` calls `IsInViewCone` to fill `bPlayerInCone`. Retail `SetPlayerLOS` `0x10291610` does **not** call `FInViewCone` (callees: PVS, origin, trace). | Keep the geometric fill on the **base** body so a debug switch does not rewrite a cache retail never cone-tested. Look still uses the override. |
| Point-only callers | `ElysiumNpcEnemy.cpp` (attacker origin), `ElysiumNpcWitness.cpp` (law-event origin), geometry tests. | Stay on the base body. They are not slot-363 dispatches. |
| File granularity | `ElysiumNpcSenses.{h,cpp}` already own this class (~278 / ~800 lines). | No move first. |

### Name correction established during planning

`ai_ignoreplayers` is HL2's name and is **absent** from `vampire.dll`. The two bytes are the
live state of ConCommands `npc_ignore_player` and `npc_ignore_senses`. Record that in the oracle
when the work lands. The spec's "as ConVars" job is kept: `TAutoConsoleVariable<int32>` with those
names, default 0, `ECVF_Default` (retail command flags are 0). Tests set them through
`IConsoleManager` and restore with `ON_SCOPE_EXIT`, matching `ElysiumFacialTests.cpp`. The
toggle-and-`DevMsg` command bodies are developer UX, not bytecode-observable; they are not
reproduced.

## Implementation sequence

### 1. Record the recovered chain in the oracle

Update `docs/vtmb/npc-ai-reverse-engineering.md` § "The Troika cone override `0x102b4540`"
(and the QuerySeeEntity / QueryHearSound sentences that still say `ai_ignoreplayers`) **before**
or with the runtime patch, not as a later cleanup. Cite addresses.

- Command constructors `0x10088c70` / `0x10088d70`, names, help strings, toggle callbacks
  `0x10088be0` / `0x10088ce0`, `DevMsg` at `0x1054c188`.
- Slot 293 = `FUN_102c5470` = follower-boss handle `+0x647c` then `boss+0x9c` (`CBaseCombatCharacter*`).
  Same slot 16a already names `GetFollowerBoss`. There is no separate `GetTarget` body.
- `+0x98` Troika self-pointer; `+0x9c` CC self-pointer; `+0xa8` player self-pointer (the existing
  "is player" test).
- `+0x6279` = `m_bInPlayerLOS`.
- Arm 4's actual predicate and the rejected "player at any angle" reading.
- Mark the override PORTED for arms 1–3 and the base fall-through; arm 4 remains a named seam.

Do not add a `docs/decisions.md` row. Using ConVars instead of toggle commands is the spec job,
not a gameplay-order change.

### 2. Publish the two bytes next to the cone

In `ElysiumNpcSenses.cpp`, file-static:

```cpp
TAutoConsoleVariable<int32> CVarNpcIgnorePlayer(
	TEXT("npc_ignore_player"), 0,
	TEXT("NPCs will not hear or see the player"), ECVF_Default);
TAutoConsoleVariable<int32> CVarNpcIgnoreSenses(
	TEXT("npc_ignore_senses"), 0,
	TEXT("NPCs will not hear or see anything"), ECVF_Default);
```

Narrow accessors in the `ElysiumNpcSense` namespace (or as private statics on `FElysiumNpcSenses`):
`IgnoreSenses()`, `IgnorePlayer()`. Read with `GetValueOnGameThread()`. Non-zero is on, matching
the retail bytes. Do not serialize. Do not prefix `elysium.`.

### 3. Split the cone API; implement arms 1–3

Keep the existing point signature as the **base body** (`FinViewCone3dNew`). Add the Troika
override as an entity overload — retail `FInViewCone` takes `CBaseEntity*`:

```cpp
static bool IsInViewCone(const FElysiumNpc& Npc, const FVector& TargetCm,
	float TargetConeScalar = 1.0f); // step 5, 6a body
static bool IsInViewCone(const FElysiumNpc& Npc, const FElysiumEntity& Target,
	float TargetConeScalar = 1.0f); // slot 363
```

Entity overload, in order:

1. No world / inert observer is not a retail arm; do not invent one. The Look loop already
   skips inert candidates.
2. `IgnoreSenses()` → false.
3. `IgnorePlayer()` and the target is the world's player (`Handle == World->PlayerHandle()`,
   the port's `+0xa8`) → false.
4. **Seam, does not fire.** Comment the recovered predicate: `GetFollowerBoss()` as CC equals
   `Memory.ClosestPlayer`, target's Troika overlay non-null, that overlay's `m_bInPlayerLOS` set
   → true, skip the cone. Today: no follower field, player's `+0x98` is null, `m_bInPlayerLOS` is
   this observer's cache bit not the target's. Return to step 5.
5. Point body at `Target.EyePosition()` with the caller's scalar.

Fix the stale "Horizontal only" comment on the point signature. Null-target retail arm 1 is the
entity pointer being absent; C++ takes a reference, so Look/10a never pass null. Do not add a
pointer overload just to return false.

Route:

| Caller | Overload |
|---|---|
| `TickSight` Look loop | entity (`*Candidate`, player cone scalar when the candidate is the player) |
| 10a stranger arm in `ElysiumNpcConditions.cpp` | entity (`*Source`, default scalar 1.0) |
| `TickSight` closest-player `bPlayerInCone` | **point** (base). Retail `SetPlayerLOS` does not cone-test. |
| `ElysiumNpcEnemy.cpp`, `ElysiumNpcWitness.cpp`, geometry tests | point |

### 4. The same two bytes on QuerySeeEntity, QueryHearSound, and FVisible

These are the other generic Troika readers of the same globals. They are in this checkbox because
the recovered help strings name hearing and because Look runs QuerySeeEntity **before** the cone.

- **`TickSight` candidate loop** (QuerySeeEntity `0x102b38b0`): after inert/self, before the
  relation gate: `IgnoreSenses()` → skip; `IgnorePlayer()` and `bPlayer` → skip. Leave frenzy
  friend (`m_bfNPCFrenziedFlags & 0x800` vs `m_hFriendPlayer`) as a comment seam until 16c.
- **`TickHearing`** (QueryHearSound `0x102b35b0`): after owner resolve, before concealment:
  `IgnoreSenses()` → skip; `IgnorePlayer()` and owner is the player → skip. Same frenzy-friend
  seam. Do not reorder concealment, deaf-arc, or cower/sleep.
- **`IsVisible`** (Troika `FVisible` `0x102b4630`): `IgnoreSenses()` → false; `IgnorePlayer()`
  and candidate is the player → false; then the existing range/concealment/BrainWipe/trace body.
  Retail writes `*blocker = 0` when provided; the port has no blocker out-param — do not add one.

Do not touch Yukie or Werewolf overrides.

### 5. Verify

Extend `ElysiumNpcSensesTests.cpp`. Restore every CVar with `ON_SCOPE_EXIT`. Default 0 must leave
existing 6a cases untouched.

| Test boundary | Required evidence |
|---|---|
| Default off | Player ahead still admitted; player behind still rejected; stealth cone scalars in `Elysium.Substrate.Stealth.Senses` unchanged; 10a stranger ahead still raises `SEE_SOUND_SOURCE`. |
| `npc_ignore_senses 1` | Entity cone false for player and NPC; Look admits nobody; a player footstep in radius does not queue `HEAR_PLAYER`; `IsVisible` false. Point-only geometry tests still answer the base body. |
| `npc_ignore_player 1` | Player ahead fails Look and entity cone; a hated NPC ahead still uses the base cone; player hearing rejected; non-player sound still heard. |
| QuerySeeEntity order | With `npc_ignore_player`, a player inside radius and cone is absent from `Sighted()` (never a `SEE_PLAYER` / memory write). |
| Arm 4 seam | Observer with no follower, player behind: still false. Do not add a fixture that claims any-angle player accept. |
| Cache split | With `npc_ignore_player`, Look rejects the player; `bPlayerInCone` still follows the base geometry (ahead = true). That split is the proof the cache did not pick up slot 363. |
| Restore | A later case in the same process sees default 0. |

Freeze source before `uv run elysium build`. Then:

```
uv run elysium test Elysium.Substrate.NpcSenses
uv run elysium test Elysium.Substrate.Stealth.Senses
uv run elysium test Elysium.Substrate.NpcConditions.SoundSweepSeeSource
```

Broaden only if shared sound/sense fixtures fail. Follow `execution.md`: one worker owns the
checkbox; the coordinator holds the execution lease; a fresh tester then a fresh reviewer.
Headless tests are the acceptance for this checkbox. The tutorial sneak-past (`thug_1`, switches
off) is a 6a regression, not a new Play witness.

## Completion and scope boundary

6b is one checkbox. Necessary QuerySeeEntity / QueryHearSound / `IsVisible` reads of the same two
bytes are part of finishing it; a cone-only switch would contradict the recovered help strings.
Do not build followers (16a), `SetPlayerLOS` as a producer (15), frenzy-friend (16c), Yukie or
Werewolf overrides, the 2-D cone mode, or a live apex ConVar.

Mark 6b complete when arms 1–3 and the three sibling gates match retail at default 0 and under
each switch, arm 4 is a named non-firing seam with the recovered predicate in the oracle, and the
6a cone tests still pass. Report compilation, focused tests, and independent review separately.
