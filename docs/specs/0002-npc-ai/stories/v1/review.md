# V2 review — the records against retail

Reviewer: V2, 2026-10-04. Runs (`$ELYSIUM_WORK_ROOT/reports/arena/`):

| run | what | why |
|---|---|---|
| `20261004T022053.584695Z` | **the full suite**, `uv run elysium arena`, 96 records, 8 m 46 s wall | the brief's one full run |
| `20261004T023113.336004Z` | 47 records: the 42 the full run's arena boot errored behind `rollcall_vcamera`, plus 5 corrected records | the full run judged 44 records `error` (harness fault H1, `triage.md`); without this no roll-call verdict existed |
| `20261004T023323.443866Z` | 9 corrected records | re-run of what was corrected after the second boot |

## What was verified, and how

**Against the listing** (vtmb-corpus, read this pass): `0x102b7690` (the cover chooser, whole tail,
asm `0x102b78a0..0x102b79ac`), slot 167 = `GetEnemy` `0x101a67e0`, `0x102b5650` (slot 599),
`0x1025db70` (coordinator admission), `0x1026dd10` (whole `GatherAttackConditions`, top clear and
tail), `0x1026dc80` (slot 560 `ClearAttackConditions`), `0x1030f7b0` (`CanHearSound`), `0x101ba890`
(`CSoundEnt` pool prune), `0x102b3270` (`ShouldInvestigate`), `0x1034bbf0` (`MakerThink`),
`0x1034bc90` (maker `DeathNotice`), `0x100aaf30` (`PostSpawn`), `0x100a8710` (`ScriptHide`),
`0x102c33f0` (`InputChangeSchedule`), `0x1029f2a0` (`InputDisableThink`), `0x10252450` /
`0x102c5730` (the attack-time wait's two terms). **Against the port** (read): `ElysiumNpcSchedule.cpp:473-495`,
`ElysiumNpcTroikaHelpers.cpp:130-160`, `ElysiumNpcStartTask.cpp:575-579`,
`ElysiumNpcConditions.cpp:1033-1130`, `ElysiumNpcSenses.cpp:690-735`, `ElysiumNpcMaker.cpp:296-360,
536-621`, `ElysiumNpc.cpp:885-1007`, `ElysiumNpcBaseRunAi.cpp:35-91`, `ElysiumNpcThink.cpp:358-385`,
`ElysiumArenaBuilder.cpp:81`, `DefaultEngine.ini:63`, `ElysiumArenaStage.cpp:92-103`,
`ElysiumArenaSpec.cpp` (seats and the block).

**Fully verified** (every record that passes outside the roll call, every new red): read the record,
its `about` against the address or text it cites, and its trace. The passes: `cover_reclaim`,
`damage_idle_reaction`, `fail_route_unreachable_sound`, `hear_world_out_of_range`,
`idle_lookaround`, `interest_mode_never`, `input_setrelationship`, `input_takedamage`,
`input_teleporttoentity`, `input_tweakparam_vision`, `lifecycle_relationship_flip`,
`map_tutorial_idle`, `sense_beyond_vision`, `sense_bodies_transparent`, `sense_cone_outside`,
`unknown_crouched_band`, `verbs_feed_trance` (plus the harness's own `control_sequence`). Where the
cited body was not re-read in the listing this pass (`0x10265ed0`, `FeedInterrupt 0x1033a9e0`,
`0x1033a9e0`'s slot 614, `TeleportToEntity 0x102c24a0`, `TakeDamage 0x102c29a0`) the check was the
walked prose the record cites plus the trace's order.

**Sampled**: the roll call (48): read `rollcall_vcop`, `_vmanbat`, `_vpedestrian`, `_vmercurio`,
`_vsabbatleader`, `_vvampireboss`, `_vwerewolf`, and the traces of `_vtaxidriver`, `_vrat`,
`_vzombie`, `_vdog`, `_vanimal`; the known-red records whose red was already the authors'
(`cover`, `sense_enemy_facing_me`, `damage_lethal_death`, `lifecycle_unhide_fights`, the patrols,
the places, the map records) were checked for their first unmet expectation and its class only.

## Records corrected (each with its source)

| record | fault | change | source |
|---|---|---|---|
| `combat/range_bands` | **too loose; `about` wrong.** It claimed `GatherAttackConditions` clears every attack condition each gather; it does not: slot 560 `0x1026dc80` clears `0x4f 0x50 0x51 0x52 0x2e 0x2f 0x62 0x63 0x64 0x65 0x66`, never the band words `0x08/0x5f/0x60/0x09`, which only the tail (`0x1026e0b0..0x1026e107`) clears, on a gather where a `CAN_*` holds. Its probes sampled at 7.5 s and 18.0 s, both after a program install (which the port's trace shows wiping the set), so it could not see known red 2. | `about` corrected; probe `at 16.2 TOO_CLOSE_FOR_RANGED equals false` added (one gather after the 10 m teleport, before the running `0xef` ends); `known_red: "2: …"` set. Re-run: **expected-fail**, the probe read true. | `0x1026dc80`, `0x1026dd10` lines 150-171 of the C |
| `world/input_setrelationship` | **staging error, red misfiled as known 5.** Hunter1's row authors `angles "0 270 0"`; the stage makes that Unreal yaw 90 (`ElysiumArenaStage.cpp:102`, `-yaw`), so from `north` the player at `start` stood 90° off its facing, outside `m_flFieldOfView 0.2`: no Hunter1 trace of the full run has a `SEE_PLAYER` (except `input_teleporttoentity` after it is moved). Also the end probe read `"player"`; the runtime answers `"!player"`. | player at arena `[975, 762, 0]` cm facing -90 (300 units in front of Hunter1, off the block); probe `"!player"`; `known_red` removed after the re-run passed (SEE_HATE, OnFoundPlayer, `-> Combat`, START_COMBAT at 2.10 s). **Pass.** | the row in `exports_v2/_sidecars/sp_tutorial_1/sp_tutorial_1.ents`; `ElysiumArenaStage.cpp:92-103` |
| `world/input_tweakparam_vision` | **passed for the wrong reason**: the same seat; Hunter1 never saw the player, so "no OnFoundPlayer after VISION 0" held trivially. | same seat; new first expectation `cond+ SEE_PLAYER by 0.9` (it must see before the tweak). Re-run: **pass** (SEE_PLAYER at 0.0/0.1, nothing after the tweak and the flip). | as above |
| `perception/unknown_crouched_band` | **record error ×2.** (1) the match `SEE_UNKNOWN (0x01)`: the trace prints `(0x1)` (in the full run the condition was raised at 0.20 s and the expectation still missed); (2) the crouch was the bare console `+duck`, which reached no player in a clean boot. | match `SEE_UNKNOWN (0x1)`; crouch through the command bus `elysium.cmd +duck` at 0.2 s, released at 9.5 s. Re-run: **pass** (SEE_UNKNOWN 0.65 s, OnUnknownVisionPlayer, `-> Alert`, `SCHED_TROIKA_INVESTIGATE_UNKNOWN`). The light-scalar assumption stays (H5). | trace text; the full run, where `verbs_stealth_kill`'s `elysium.cmd +duck` crouched the player for every later record |
| `world/rollcall_vmanbat`, `_vmercurio`, `_vsabbatleader`, `_vvampireboss`, `_vpedestrian`, `_vwerewolf` | **passed for the wrong reason**: StartHidden rows, unhidden at 0.5 s; "a schedule by 3 s" was met by `FALL_TO_GROUND (0x3e)` selected **while hidden** (known red 5's shape). | `never schedule` and `never cond+` until 0.45 s; `known_red: "5: …"`. Re-run: **expected-fail** ×6 (`FLOATING_OFF_GROUND` at 0.00, the werewolf `TOO_FAR_FOR_MELEE` at 0.00). | `PostSpawn 0x100aaf30` → slot 77 `ScriptHide 0x100a8710`: think saved, `NULL` installed, `m_flNextThink = FLT_MAX` |

## Records checked and left

- **`cover.json`** (lane K's rewrite to `_VS_MELEE`): **correct.** `0x102b7690`: slot 167 (`+0x29c`,
  `GetEnemy`, `0x101a67e0`), the enemy's `+0x9c` (its combat character), `GetActiveWeapon`, weapon
  slot 360 (`+0x5a0`) `TEST AH,0x60` (`& 0x6000`) → `XOR BL,BL` (ranged threat). `TEST BL,BL; JZ
  0x102b79e0` takes the ranged arm (`0xa3` at line `0x5cd9`, or `0xa0/0xa1`); `BL` still set (no
  weapon, or a melee word) answers `0xa4` at `0x5cc3` unless already in `0x9e` or `COVER_VS_MELEE_MODE`
  (`flags2 & 0x100`) → `0xa5`. An unarmed player (no active weapon, or fists' melee word) is `0xa4`.
  `cover_armed`'s `0xa3` expectation is the same listing's other arm.
- **`combat/lifecycle_relationship_flip`**: correct and discriminating; it passes, and so does the
  corrected `input_setrelationship` (a `D_LI 0` NPC flipped to `D_HT 5`). Known red 5's flip half
  does not reproduce when the flipped NPC has the player in its cone (`triage.md` § red 5).
- **`combat/range_bands`'s "0xef steps back forever"**: in the trace the shooter runs `0xef` four times
  at 96 cm (6.7, 9.4, 11.8, 14.4 s) with `TOO_CLOSE_FOR_RANGED` re-raised each time. Retail answers
  `0xef/0xf0` there too; whether each `TASK_STEP_BACK` moved the body is not in the trace (no
  position event or timed `distance_to` probe). Not settled (doubt 3).
- **`hear_world_investigate`**: `unexpected-pass` in the full run (heard at 2.93 s, 0.92 s after
  `PlaySound`), red in lane P's three boots. `known_red` kept: a red that shows in some boots and not
  others is still a red (`triage.md` N4); the fixed seed does not make the boot deterministic.
- **`verbs_stealth_kill`**: left unclassified (`triage.md` § unclassified).
- **The roll call (48)** passes on "any schedule by 3 s, no `taskfail`": an expectation any thinking
  NPC meets. The hidden rows were corrected (above). Two more pass for the wrong reason and are not
  correctable without a harness change: `rollcall_vtaxidriver` reinstalls `IDLE_DISPOSITION` ~450
  times in 10 s (`task_special_idle_activity` done on the frame it starts), `rollcall_vrat` reinstalls
  `SCHED_VSCURRYING_LOITER` 47 times — churn the roll call cannot state without `never … at_most`
  (H3). Proposed V1 follow-up: each roll-call record names its class's own first program (`match`),
  read per class from its `SelectSchedule`; not done here (48 listing reads).
- `idle_lookaround` picks seed 4 because the port's draw lands under 10 with it: staging, not a
  retail claim (the draw order is retail's; stream parity is divergence 9).
- `sense_bodies_transparent` passes but does not discriminate while arena solids are transparent to
  sight (H2): any wall would be "transparent" too.
- `hear_world_out_of_range` proves little while N4 stands (silence may be the red).
- `input_takedamage` matches the `damage` kind, which README says is emitted even with no damage
  committed; the following `OnDamaged` and `death` carry the verdict, so it stands.

## Doubts not settled from the sources

1. **The patrol walk's speed (known red 1?)**: `walk rate=0`, ~0.65 m/s against retail's 136.7 cm/s;
   but the cover gunman's `smith_aggressive_run rate=0` covers ~16.8 m in 6.7 s (~2.5 m/s). So the
   slow walk is not plainly the rate-0 commit; the motor's speed source for a walk was not read.
   Filed under red 1 provisionally (V3 must re-measure before claiming it).
2. **Hidden animals** (`rollcall_vanimal`, `_vdog`, `_vscurrying`): they think while hidden (red 5)
   **and** select no schedule at all after the unhide, where the human hidden rows at least select
   `FALL_TO_GROUND`. The Animal line's select (`0x1035fb50`, `ElysiumNpcSelectSpecies.cpp:266-302`)
   was not walked against the listing.
3. Whether `TASK_STEP_BACK` moves the body (range_bands, above).
4. **`maker_respawn`**: `MakerThink` and `DeathNotice` read retail-faithful; `OnNPCDied` fired, so
   the live count dropped. The refusal is then `CanMakeNPC` (`ElysiumNpcMaker.cpp:303-360`): the
   occupied box (the dead child's body still `FL_NPC` in the box?) or the player's visibility arms.
   The refusal reason is logged only at `Verbose` (`ElysiumNpcMaker.cpp:387`).
5. `damage_idle_reaction`: retail's `TASK_FIND_COVER_FROM_SAVEPOSITION` answer in this room (the port
   fails `Couldn't find cover (0x8)`) — not read.
6. `rollcall_vzombie`: a `task` at 0.00 with no `schedule` event before it — a tap that misses a
   spawn-time install, or an install that bypasses `SetSchedule 0x10280e50`.
7. `_DAT_10450aa0` (the sound pool's grace after expiry): lane P reads 4.0; not re-read.
8. Lane W's "OnFoundPlayer every think": not reproduced — the combat traces fire it twice (0.0 and
   0.1 s, beside two `NEW_ENEMY`), not every think.
