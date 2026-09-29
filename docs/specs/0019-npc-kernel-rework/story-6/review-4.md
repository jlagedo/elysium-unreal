# Review · wave 4 (Q1–Q4, the inline cells)

## Findings

1. `ElysiumNpcScriptSpecies.cpp:48` — reads `ElysiumNpcTunables::HeadAngleRunawayLimitDouble`, which does not exist. The `ThreeSixty` rename was a substring replace and hit `ThreeSixtyDouble`. **Build break**, and `ThreeSixtyDouble` now has no reader. Fix: restore `ThreeSixtyDouble`; rename whole-word.
2. `ElysiumNpcVampireBoss.cpp:52` — the comment is `_DAT_104ce8bc`, but the code reads `ProteanTransformWait`, the Hengeyokai cell `104b6808`; the image reads `104ce8bc` at `0x103c6416 FLD`. Fix: read `ProteanTransformWaitAtE8BC`. Same value, 2.0.
3. `ElysiumNpcBaseStartTask.cpp:2843` — the comment is `_DAT_104994a0`, but the code reads `MinusOne` (`104492dc`). Fix: read `MinusOneAt94A0`.
4. `ElysiumNpcBaseStartTask.cpp:198,200` — the comments cite `1049a160`/`1049a164` (StartTask's cells, `0x10282f71`…). The code reads `GoalToleranceKeep`/`Hull` (`1049d97c`/`d980`, read at `0x102ecdbc`/`ddd`). No row holds `1049a160`/`164`. The same drift is at `ElysiumNpcScript.cpp:428,543` (which cite `1049a1ac`/`1049a154`). Also, `ElysiumNpcStartTask.inl:57` says `0x102ecd20` compares `1049a1ac`/`1049a1b0`, but the image says `1049d97c`/`d980`. Values match (-1/-2); only the provenance is wrong.
5. `kernel_tunables.tsv` `10924984 DispositionSeedCell` (`ref`) — the cell is in `.bss` and has no writer, so retail seeds `m_nCurrDisposition = 0`. That is a value, not a ref. `ElysiumNpcLifecycle2.cpp:424` still says "not recoverable". Patrol table `1049df20..30` (ref) feeds unchecked literals at `ElysiumNpcScript.cpp:41-42` (they match).
6. `ElysiumNpcBaseHelpers.cpp:43` — the "UNRECOVERED literals" heading still sits above `FollowRunDistance`, which is recovered.
7. Tests: `ElysiumNpcKernelFacingTests.cpp:428` still pins `0.2f`, not `HeadFilterBlend` (passes on 0.01 tolerance). `Social10Tests:285` pins `-1.f` (Q4 noted this).

## Value changes

- `1044ddb0` 256 (`conditions-and-states.md:1388-1396`). `COND_TOO_FAR_FOR_MELEE` now fires above 256 units for attack 1; the test was rewritten.
- ScheduleHost wait 1000 (`schedule-kernel.md:992-995`). A `wait` operand ≤ 0 now waits 1000 s instead of expiring the same frame.
- `1049954c` 0x3e4ccccc (`shape.md:532`). The head filter is 1 ulp lower.
- Squad-seen `0.2f`, MingXiao `0.3f`, zombie maker `0.9f`: each is an f32 widened, about 1e-8 relative. Supported.
- `MeleeReachPad` is an address fix only (100 → 100).
- `TentaclePhase3Seconds` 0 → 9999 has no reader.

## Checked and clean

- 373 swaps checked by script (all four lanes): 168 name the row's address on the line, 127 name it within 5 lines. The only mismatches are items 2–4.
- Every removed literal equals its row's value, apart from the named changes.
- All 201 wave-4 f32/f64 rows match their readers' opcode width.

