# Recovered mechanic — World of Darkness dice resolution

Source: `vampire.dll` `FUN_101d8b40` (anchor string `"Dice Results:"`), decompiled via
Ghidra headless (`research/tooling/ghidra/scripts/ghidra_extract_mechanics.java` → `$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/vtmb_mechanics.c`).

**Status: verified (RE5 closed).** Confirmed by decompilation across the whole roll cluster
(constructor `FUN_101d88b0`, roller `FUN_101d8b40`, `vroll` handler `0x100d7040`, the RNG /
table path, and the data-file loader `FUN_101d92b0`) **plus reading the shipped data file
`vdata/system/DiceRolls.txt`** — which is the actual ground truth. No running game was
needed: the golden-test premise (drive retail's `vroll`) was superseded when the roll's
face-distribution turned out to be **data-driven** by that file, readable offline. See
"Verification" below.

The resolver does not decide what counts as success for dialogue, locks, feeding or combat.
That consumer policy is consolidated in `docs/vtmb/skills-and-checks.md`.

## Roll struct field map (recovered)

The constructor `FUN_101d88b0` initialises the struct; the roller `FUN_101d8b40` consumes it.
Both decompiles and the raw-listing offsets agree.

| Offset | Field | Confidence | Set at |
|---|---|---|---|
| `[0]` | dice pool size (clamped ≤ 250) | high | ctor `*p = pool` |
| `[1]` | successes (working; **seeded from a ctor arg**, not forced 0) | high | ctor `p[1] = arg7` |
| `[2]` | count of 10s rolled | high | roller `[ESI+8]` |
| `[3]` | botches (1s) | high | roller `[ESI+0xc]` |
| `[4]` | roll-context handle → selects the **weighting table** (0 when none) | high | ctor |
| `[5]` | roller/subject object handle | medium | ctor |
| `[6]` | botch-table object (null = none; `vroll` passes null) | high | ctor |
| `[7]`–`[10]` | attribute/ability context (drives pool via `f(sheet, ability)`) | medium | ctor |
| `[0xb]` (11) | **difficulty − 1** (stored decremented; human target = `[0xb]+1`) | high | ctor `p[0xb] = diff-1`; roller `[ESI+0x2c]` |
| `[0xc]` (12) | result tier, written as output (0–4); defaults to 1 | high | ctor default 1, roller final |
| `[0xd]` (13) | working "dice remaining" counter (init = pool) | high | ctor `p[0xd] = pool`; roller `[ESI+0x34]` |
| `[0xe]` (14) | wound/health penalty subtracted from pool (`HealthModifiers`, all 0 today) | high | zeroed in ctor, set from data |

`[0xe]` is confirmed by the roller's debug print (`"… %d (%d - %d health mod) …"`), which
emits `pool − [0xe]`, `pool`, `[0xe]`.

**Difficulty scale — settled: CLI is human (1..10).** The engine stores `[0xb] = difficulty − 1`
and, per die, tests `face(0..9) >= [0xb]`. A face `f∈0..9` is the physical d10 showing
`f+1∈1..10`, so this is exactly "physical die `≥` human difficulty". The `vroll` handler
(`0x100d7040`) takes `difficulty = atoi(arg2)` **raw** and passes it straight into the ctor's
`param_6` (→ `[0xb] = atoi(arg2) − 1`), so the console argument is the human target number and
the port's `difficulty − 1` is correct. (A port comparing a 0-based `die` against a *raw*
`difficulty` would be off by one.)

Dice-pool hard cap: **250** (`FUN_101d88b0`, anchor `"Dice roll count out of bounds …
capped at %d"`; the results buffer is `[252]`). The roller loop carries a **second** 250 cap
on *total rolls* (`local_4f8 < 0xfa`), which bounds runaway 10-again explosions independently
of the initial-pool clamp.

## Algorithm

Internally a die face is `0..9`, representing a physical **d10 showing 1..10**. `difficulty`
below is the **human** WoD target number (1..10); the engine stores it as `difficulty − 1`
in `[0xb]` (see the field map) and compares against that.

1. If pool `< 1` → immediate **Failure** (tier 1).
2. For each die in the pool:
   - face `== 9` (a **10**): success **and** add one die back to the pool → *10-again
     exploding dice*; also counted as a 10. (Checked first, so a 10 succeeds at any difficulty.)
   - face `== 0` (a **1**): a **botch die**. (Checked before the difficulty compare, so a 1 is
     never a success.)
   - `face >= difficulty − 1` (equivalently physical `face+1 >= difficulty`): a **success**.
3. `net = successes − botches`, tiered into the output:

| net successes | tier | meaning |
|---|---|---|
| ≥ 5 | 4 | exceptional success |
| 3–4 | 3 | good success |
| 1–2 | 2 | success |
| ≤ 0 **with** ≥1 raw success, or exactly 0 | 1 | failure |
| < 0 **and** zero raw successes | 0 | **botch** (rolls botch table if botches > 1) |

## The die face is a data-driven table lookup

Each die face is **not** `rand()%10`. The roller (raw listing `0x101d8ba6`) does, per die:

1. `r99 = engineRandom->Method8(0, 99)` — a uniform integer in `[0,99]` from the engine's
   random interface (`DAT_1070b244`, vtable slot `+8`).
2. `face = WeightTable[r99]` — a **100-entry lookup** (`this[r99*4 + 0xc]`), where the table is
   selected by the roll context `[4]` from a manager at `0x10739194` (array of 412-byte
   tables, stride `0x19c` = a 12-byte header + `int[100]`).

The tables are loaded at startup by `FUN_101d92b0` from **`vdata/system/DiceRolls.txt`** (a
Valve-KeyValues file; the loader builds the path from `"vdata\system\"` + `"DiceRolls"` +
`"%s%s.txt"`, error string `"Unable to read RollInfo data file"`). Its `Rules/TableWeightings`
block defines one child table per die type, each mapping the 100 raw values `"0".."99"` to a
face (`GetInt(key, default 1)` per entry — `FUN_101d90c0`). The file comments this directly:
*"these tables are data-driven, so you can create your own if you wish."*

**In the shipped install all weighting tables are a plain uniform d10.** The file defines
three (`Normal`, `Heavy`, `Light`) and every one maps `face = r99 / 10` (`0–9→0, 10–19→1, …,
90–99→9`) — each face 0..9 appears exactly 10×. So `rng.Next(0, 10)` reproduces the shipped
behaviour exactly. The weighting *mechanism* is real (a mod could bias a die); see "Runtime
consumer" below for what the resolver should load instead of hard-coding uniformity.

`DiceRolls.txt` also carries, in its `Text` block, the display strings that confirm the tier
enum names 1:1 — `RollResult`: `0 Botched · 1 Failure · 2 Partial Success · 3 Success ·
4 Critical Success` — and a separate `Success` margin scale (`0 Failure · 1 Marginal ·
2 Moderate · 3 Complete · 4 Exceptional · 5 Phenomenal`) keyed by net successes. `Rules/
HealthModifiers` (health levels `0..7`) is the source of `[0xe]`; every entry is `0` in the
shipped data, so the wound penalty is currently a no-op.

## Faithful C# port

```csharp
public enum RollResult { Botch = 0, Failure = 1, Success = 2, GoodSuccess = 3, Exceptional = 4 }

// Reconstructed from vampire.dll FUN_101d8b40 (roller) + FUN_101d88b0 (constructor).
// `difficulty` is the human WoD target (1..10); the engine stores difficulty-1 and
// compares face(0..9) >= difficulty-1. `freeSuccesses` mirrors the ctor's successes seed
// (p[1] = arg7); pass 0 for the plain roll.
public static RollResult ResolveRoll(int pool, int difficulty, System.Random rng,
                                     IBotchTable botchTable = null, int freeSuccesses = 0)
{
    pool = System.Math.Min(pool, 250);          // FUN_101d88b0 clamp
    if (pool < 1) return RollResult.Failure;    // empty pool auto-fails

    int internalDiff = difficulty - 1;          // engine stores [0xb] = difficulty - 1
    int successes = freeSuccesses, botches = 0, diceLeft = pool;
    int rolls = 0;                              // roller caps total rolls at 250 too
    while (diceLeft >= 1 && rolls < 250)
    {
        int die = rng.Next(0, 10); rolls++;     // 0..9 == a d10 face 1..10
        if (die == 0)                 botches++;                   // a '1'
        else if (die == 9)          { successes++; diceLeft++; }   // a '10': success + re-roll
        else if (die >= internalDiff) successes++;                // physical face+1 >= difficulty
        diceLeft--;
    }

    int net = successes - botches;
    if (net >= 5) return RollResult.Exceptional;
    if (net >= 3) return RollResult.GoodSuccess;
    if (net >= 1) return RollResult.Success;
    if (net == 0 || successes != 0) return RollResult.Failure;
    if (botches > 1) botchTable?.Resolve(botches);
    return RollResult.Botch;
}
```

## Verification (RE5 — resolved without the running game)

The planned golden test — driving retail's `vroll` and matching its printed `"Dice Results:"`
report — proved unnecessary once the RNG-dependent step turned out to be data-driven (see
status above). Every other step — loop, tiering, difficulty comparison, caps, field layout —
is fixed in code and confirmed against both the decompilation and the raw listing.

What each open question resolved to:

- **Difficulty scale** — human (1..10), confirmed by `vroll`'s raw `atoi(arg2)` → ctor
  `[0xb] = arg − 1` path (`0x100d7040` → `FUN_101d88b0`; see "Difficulty scale" above).
- **RNG distribution** — a per-context 100-entry weighting table indexed by
  `RandomInt(0,99)`; the shipped tables are all uniform d10, so `rng.Next(0,10)` is faithful
  (see "data-driven table lookup" above).
- **Provisional fields** — `[4]`, `[6]`, `[0xe]`, and the pool source (`f(character sheet,
  ability)`, not a CLI arg) are confirmed from the ctor + handler (see field map above).

The `vroll` console command (`FUN_100d71a0`, real handler `0x100d7040`, help *"Processes a
Vampire Dice Roll."*) still exists and, in its debug branch, prints the face list (physical
1..10), a summary (net, 10s, human difficulty `[0xb]+1`, tier), and a running
`"Breakdown: ave = …"` histogram — useful for a live cross-check if a future divergence
surfaces.

**Runtime consumer (roadmap 9.6).** Copy `vdata/system/DiceRolls.txt` into the offline
mirror (a small `UE_`-style extract, patch-first like the other `vdata` copies) and have the
C++ resolver load its `TableWeightings` + `HealthModifiers` rather than hard-code uniformity —
so a data mod that reweights a die is honoured, matching the engine.
