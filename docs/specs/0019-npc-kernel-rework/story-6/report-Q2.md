# Lane Q2 report · inline cells, files B

(Saved by the orchestrator from the lane's final answer. Nothing built or run.)

**Scope.** 342 `DAT_10…` mentions over 159 addresses in 53 files. 23 addresses were already held (or ConVar objects at DAT-4). The other 136 became 83 cell rows and 49 refs; a script read each of the 83 values back from the pinned image at its width.

**Moved.** Cells: 72 rows (67 f32, 4 f64 `FifthDouble` / `EightTenthsDouble` / `QuarterDouble` / `FiveDouble`, 1 i32 `AiStepIndexFloor`, a `.data` global holding -1). Code: 23 files, ~80 literal-to-`ElysiumNpcTunables::` swaps (most in ChangBros 26, Facing, Combat10, Conditions2Species, EntityChain/EntityChain2, Hengeyokai). Five stale "names unrecovered" comments now name the ConVars and values. ConVars: 11 object rows at DAT-4 from the image's static initialisers: the three `pl_*_level`, `debug_response_timer_min` / `_max`, `debug_heightened_alert_expire_time`, `debug_show_cs_acts`, `npc_vphysics`, `particle_scale`, `ent_trace_melee`, `zombie_gib_amt`.

**Findings.** Two sites now read retail's f32 where the port used a double: the squad-seen window (`0.2f`) and the MingXiao side-width factor (`0.3f`). `Damage3.cpp` read `Hundred` (`10450564`) for `_DAT_1049e048`; it now reads `MeleeReachPad`. `10449258` is an f32 3.0 (the port comment said double). Pooled cells other lanes may claim: `10450568`, `104492b8`, `10451ab8`, `10462948`. Bound to existing names: `Fifteen`, `Twenty`, `Half`, `Tenth`, `InterestCubicThree`, `ChangBrosJumpGravity`.

**Needs another owner.** `ElysiumNpcDamage2.cpp:44` multiplies `UpAxis` by the "Right" cell and `RightAxis` by the "Up" cell; `ChangBros.h:33` lists the triple forward/right/up — check against `0x1036dd20`. The `AutoaimBlendWeights` seam answers scale 0; the image holds 0.9 (`NineTenths`) and 0.3 (`ThreeTenths`) — a wire, not an inline cell. `ElysiumCameraOverride.h` restates 75.0 (`SeventyFive`) without the tunable.
