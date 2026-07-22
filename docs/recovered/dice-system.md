# Recovered mechanic — World of Darkness dice resolution

Source: `vampire.dll` `FUN_101d8b40` (anchor string `"Dice Results:"`), decompiled via
Ghidra headless (`tools/re/ghidra_extract_mechanics.java` → `tools/re/out/vtmb_mechanics.c`).
Reconstructed by reading the decompilation; **must be verified against the running
game** (see below) before it is treated as canonical.

## Roll struct field map (recovered)

| Offset | Field | Confidence |
|---|---|---|
| `[0]` | dice pool size | high |
| `[1]` | successes (working) | high |
| `[2]` | count of 10s rolled | high |
| `[3]` | botches (1s) | high |
| `[4]` | RNG/roll context handle | inferred |
| `[6]` | botch-table object (null = none) | inferred |
| `[0xb]` (11) | difficulty / target number (die-face scale) | high |
| `[0xc]` (12) | result tier, written as output (0–4) | high |
| `[0xd]` (13) | working "dice remaining" counter | high |
| `[0xe]` (14) | wound/health penalty subtracted from pool | medium |

Dice-pool hard cap: **250** (`FUN_101d88b0`, anchor `"Dice roll count out of bounds …
capped at %d"`; the results buffer is `[252]`).

## Algorithm

Internally a die face is `0..9`, representing a physical **d10 showing 1..10**.

1. If pool `< 1` → immediate **Failure** (tier 1).
2. For each die in the pool:
   - face `== 9` (a **10**): success **and** add one die back to the pool → *10-again
     exploding dice*; also counted as a 10.
   - face `== 0` (a **1**): a **botch die**.
   - face `>= difficulty`: a **success**.
3. `net = successes − botches`, tiered into the output:

| net successes | tier | meaning |
|---|---|---|
| ≥ 5 | 4 | exceptional success |
| 3–4 | 3 | good success |
| 1–2 | 2 | success |
| ≤ 0 **with** ≥1 raw success, or exactly 0 | 1 | failure |
| < 0 **and** zero raw successes | 0 | **botch** (rolls botch table if botches > 1) |

## Faithful C# port

```csharp
public enum RollResult { Botch = 0, Failure = 1, Success = 2, GoodSuccess = 3, Exceptional = 4 }

// Reconstructed from vampire.dll FUN_101d8b40.
public static RollResult ResolveRoll(int pool, int difficulty, System.Random rng,
                                     IBotchTable botchTable = null)
{
    pool = System.Math.Min(pool, 250);          // FUN_101d88b0 clamp
    if (pool < 1) return RollResult.Failure;    // empty pool auto-fails

    int successes = 0, botches = 0, diceLeft = pool;
    while (diceLeft >= 1)
    {
        int die = rng.Next(0, 10);              // 0..9 == a d10 face 1..10
        if (die == 0)               botches++;                     // a '1'
        else if (die == 9)        { successes++; diceLeft++; }     // a '10': success + re-roll
        else if (die >= difficulty) successes++;                  // meets difficulty
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

## Verification (the discipline that makes this canonical)

The original exposes the roll via the `vroll` console command and spews a full
`"Dice Results:"` / `"Breakdown: ave …"` report (the debug branch, `param_3 != 0`).
Golden test: drive the original with fixed inputs (a seeded pool + difficulty), capture
its printed successes/botches/tier, and assert the C# port produces the identical tier
distribution over many trials. Until that passes, treat `[4]`, `[6]`, `[0xe]` as
provisional.
