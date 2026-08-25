# The activity enum, the registry, and sequence selection

VtMB's `ACT_*` vocabulary is **4,460 entries** with values **1 … 4492** and 32 holes. It exists only
in the binary — no shipped `.mdl` carries a resolved activity number, and no data file lists the
names. This document is the recovered table and the machinery around it.

**Where the numbers come from.** `ActivityList_RegisterSharedActivities`
(`vampire.dll 0x104126e0`, 68,194 bytes) makes 4,460 calls in a fixed order. The table below is
exactly reconstructible from it: all 30 weapon-family blocks were machine-verified against their
templates with zero deviations.

> **The `activities` array in `$ELYSIUM_EXPORT_ROOT/npc/clips/*.json` is NOT this enum.** It is the
> exporter's own per-stem intern table (`npc_export.py` → "interning into `activities`"), so
> `cabbie.json` has 7 entries and `beckett.json` 1,206, and index 1 differs per file. Reading a
> number out of a clips sidecar and treating it as an `ACT_*` value is wrong.
> `ACT_RUN_M37` is **1065**, not 174.

## The registry

| symbol | address | meaning |
|---|---|---|
| `ActivityList_AddActivityToList(name, value, isPrivate)` | `0x10412260` | inserts at the vector head, records the max value, writes the reverse row |
| `RegisterSharedActivity(name, value)` | `0x104123a0` | the 4,431 ordinary registrations |
| `RegisterGrappleActivity(name, value)` | `0x10412590` | 29 calls; **also** pushes the value into a second list |
| `ActivityList_Find(name)` | `0x10412420` | dictionary find; warns past index `0x118c` |
| `ActivityList_IndexForName(name)` | `0x10412520` | the value, or `-1` |
| `ActivityList_RegisterPrivateActivity(name)` | `0x104124b0` | appends at `g_HighestActivity + 1`; warns `"Shared<->Private Activity collision!"` |
| `ActivityList_NameForIndex(value)` | `0x10412550` | the name, for diagnostics |
| `IsGrappleActivity(value)` | `0x104126a0` | linear scan of the second list |

Globals: the `CUtlVector<activitylist_t>` at `0x109d3010` (capacity `+4`, count `+0xc`), the
name↔index `CUtlSymbolTable` at `0x1094afe0`, `g_HighestActivity` at `0x109d3080`, a reverse array
at `0x1094afe8` with **stride 0x44** (`char name[0x40]` + `int listIndex`) written only for values
below `0x2000`, and the grapple list at `0x109d306c`.

`activitylist_t` is **8 bytes**: `int activityValue`, `short symbolHandle`, `byte isPrivate`, pad.

### Name resolution folds case

The symbol table is constructed by `FUN_10411ca0` → `FUN_1024a230` →
`FUN_1024b2d0(tbl, 0, 0, caseInsensitive = 1)`, and that last argument installs comparator
`0x10011383`, an ILT jump to `0x1024b230` whose body is `__strcmpi(a, b) < 0`. The case-sensitive
alternative (`0x1000c496` → `0x1024b150`, a byte-wise `strcmp`) is **not** the one installed.

So `ACT_MELEE_ATTACK_sledgehammer` and `ACT_MELEE_ATTACK_SLEDGEHAMMER` are one value, and the 91
mixed-case activity literals in the shipped corpus are live data.

### The `.mdl`'s baked activity number is discarded at load

`Studio_SetActivityForSequence` (`0x10427a70`, guarded by `studiohdr+0x118`) walks every local
sequence and, for any with a non-empty `szactivityname`, resolves the **name** and overwrites
`seqdesc.activity`@12. An unknown name is **auto-registered** as a private activity rather than
dropped. `seqdesc.activity` reads `-1` on disk for essentially every shipped sequence — the name is
the only durable key.

## The base block, 1 … 307

```
  1 ACT_IDLE                     2 ACT_TRANSITION               3 ACT_FIDGET
  4 ACT_CHARSHEET_FIDGET         5 ACT_AIM                      6 ACT_COVER
  7 ACT_COVER_MED                8 ACT_COVER_LOW                9 ACT_WALK
 10 ACT_WALK_45                 11 ACT_WALK_90                 12 ACT_WALK_135
 13 ACT_WALK_225                14 ACT_WALK_270                15 ACT_WALK_315
 16 ACT_WALKB                   17 ACT_WALK_AIM                18 ACT_SNEAK
 19 ACT_RUN                     20 ACT_RUNB                    21 ACT_RUN_AIM
 22 ACT_WALK_RELAXED            23 ACT_RUN_RELAXED             24 ACT_SCRIPT_CUSTOM_MOVE
 25 ACT_RANGE_ATTACK1           26 ACT_RANGE_ATTACK1_LAYER     27 ACT_RANGE_ATTACK2
 28 ACT_RANGE_ATTACK2_LAYER     29 ACT_DIESIMPLE               30 ACT_DIEBACKWARD
 31 ACT_DIEFORWARD              32 ACT_DIEVIOLENT              33 ACT_DIERAGDOLL
 34 ACT_FLY                     35 ACT_HOVER                   36 ACT_GLIDE
 37 ACT_SWIM                    38 ACT_TREADWATER              39 ACT_JUMP
 40 ACT_HOP                     41 ACT_HOP_UP                  42 ACT_HOP_DOWN
 43 ACT_PRE_JUMP                44 ACT_LEAP                    45 ACT_LEAP_ASCEND
 46 ACT_LEAP_DESCEND            47 ACT_FALLING                 48 ACT_LAND
 49 ACT_LAND_CROUCH             50 ACT_LAND_HARD               51 ACT_CLIMB_UP
 52 ACT_CLIMB_DOWN              53 ACT_SHIPLADDER_UP           54 ACT_SHIPLADDER_DOWN
 55 ACT_STRAFE_LEFT             56 ACT_STRAFE_RIGHT            57 ACT_ROLL_LEFT
 58 ACT_ROLL_RIGHT              59 ACT_TURN_LEFT               60 ACT_TURN_RIGHT
 61 ACT_TURN_LEFT_ALERT         62 ACT_TURN_RIGHT_ALERT        63 ACT_CROUCH
 64 ACT_SIT                     65 ACT_STAND                   66 ACT_USE
 67 ACT_DANCE                   68 ACT_SIGNAL1                 69 ACT_SIGNAL2
 70 ACT_SIGNAL3                 71 ACT_LOOKBACK_RIGHT          72 ACT_LOOKBACK_LEFT
 73 ACT_SMALL_FLINCH            74 ACT_BIG_FLINCH              75 ACT_MELEE_ATTACK
 76 ACT_MELEE_AIR_ATTACK        77 ACT_MELEE_ATTACK_2COMBO     78 ACT_MELEE_ATTACK_HEAVY
 79 ACT_MELEE_ATTACK_FROMCROUCH 80 ACT_DODGE                   81 ACT_STEPBACK
 82 ACT_BLOCK                   83 ACT_KICK                    84 ACT_RELOAD
 85 ACT_RELOAD_FAST             86 ACT_RELOAD_LAYER            87 ACT_RELOAD_LOW
 88 ACT_DRYFIRE                 89 ACT_DRYFIRE_LAYER           90 ACT_ARM
 91 ACT_DISARM                  92 ACT_PICKUP_GROUND           93 ACT_IDLE_ANGRY
 94 ACT_SPECIAL_ATTACK1         95 ACT_SPECIAL_ATTACK2         96 ACT_COMBAT_IDLE
 97 ACT_VICTORY_DANCE           98 ACT_DIE_HEADSHOT            99 ACT_DIE_CHESTSHOT
100 ACT_DIE_GUTSHOT            101 ACT_DIE_BACKSHOT           102 ACT_FLINCH_HEAD
103 ACT_FLINCH_CHEST           104 ACT_FLINCH_STOMACH         105 ACT_FLINCH_LEFTARM
106 ACT_FLINCH_RIGHTARM        107 ACT_FLINCH_LEFTLEG         108 ACT_FLINCH_RIGHTLEG
109 ACT_FLINCH_PHYSICS         110 ACT_SNARL                  111 ACT_IDLE_ON_FIRE
112 ACT_WALK_ON_FIRE           113 ACT_RUN_ON_FIRE            114 ACT_RAPPEL_LOOP
115 ACT_HIT_HEAD               116 ACT_HIT_TORSO              117 ACT_KNOCKBACK_SMALLLOW
118 ACT_KNOCKBACK_SMALLHIGH    119 ACT_KNOCKBACK_BIGHIGHLEFT  120 ACT_KNOCKBACK_BIGHIGHRIGHT
121 ACT_KNOCKBACK_BIGUPPERCUT  122 ACT_KNOCKBACK_BIGUPPERCUTLEFT
123 ACT_KNOCKBACK_BIGUPPERCUTRIGHT       124 ACT_KNOCKBACK_BIGLOW
125 ACT_KNOCKBACK_KNOCKDOWNRIGHT         126 ACT_KNOCKBACK_SMALLHIGHRIGHT
127 ACT_KNOCKBACK_SMALLHIGHLEFT          128 ACT_KNOCKBACK_SMALLDAZED
129 ACT_KNOCKBACK_SMALL_LOW_BACK         130 ACT_KNOCKBACK_SMALL_HIGH_FORWARD
131 ACT_KNOCKBACK_SMALL_HIGH_RIGHT       132 ACT_KNOCKBACK_SMALL_HIGH_LEFT
133 ACT_KNOCKBACK_SMALL_HIGH_BACK        134 ACT_KNOCKBACK_NORMAL_LOW_BACK
135 ACT_KNOCKBACK_NORMAL_HIGH_FORWARD    136 ACT_KNOCKBACK_NORMAL_HIGH_RIGHT
137 ACT_KNOCKBACK_NORMAL_HIGH_LEFT       138 ACT_KNOCKBACK_NORMAL_HIGH_BACK
139 ACT_KNOCKBACK_FLYING_INTO_FORWARD    140 ACT_KNOCKBACK_FLYING_INTO_RIGHT
141 ACT_KNOCKBACK_FLYING_INTO_LEFT       142 ACT_KNOCKBACK_FLYING_INTO_BACK
143 ACT_KNOCKBACK_FLYING_IDLE  144 ACT_KNOCKBACK_FLYING_LAND  145 ACT_KNOCKBACK_FLYING_WALL_HIT
146 ACT_KNOCKBACK_FLYING_WALL_FALL       147 ACT_KNOCKBACK_FLYING_WALL_LAND
148 ACT_FINISHING_MOVE                              (grapple-registered)
149 ACT_FINISHING_MOVE_ATTACKER_SHORTVICTIM_FRONT   150 ..._ATTACKER_TALLVICTIM_FRONT
151 ACT_FINISHING_MOVE_VICTIM_SHORTATTACKER_FRONT   152 ..._VICTIM_TALLATTACKER_FRONT
153 ACT_FINISHING_MOVE_ATTACKER_SHORTVICTIM_BACK    154 ..._ATTACKER_TALLVICTIM_BACK
155 ACT_FINISHING_MOVE_VICTIM_SHORTATTACKER_BACK    156 ..._VICTIM_TALLATTACKER_BACK
157 ACT_180_LEFT               158 ACT_180_RIGHT              159 ACT_180_LEFT_ALERT
160 ACT_180_RIGHT_ALERT        161 ACT_90_LEFT                162 ACT_90_RIGHT
163 ACT_90_LEFT_ALERT          164 ACT_90_RIGHT_ALERT         165 ACT_STEP_LEFT
166 ACT_STEP_RIGHT             167 ACT_STEP_BACK              168 ACT_STEP_FORE
169 ACT_45_LEFT                170 ACT_45_RIGHT               171 ACT_TURN
172 ACT_PULLBACK               173 ACT_PULLBACK_LAYER         174 ACT_HOLD
175 ACT_HOLD_LAYER             176 ACT_THROW                  177 ACT_THROW_LAYER
178 ACT_ROLL                   179 ACT_ROLL_LAYER             180 ACT_VM_DRAW
181 ACT_VM_HOLSTER             182 ACT_VM_IDLE                183 ACT_VM_FIDGET
184 ACT_VM_PULLBACK            185 ACT_VM_THROW               186 ACT_VM_ROLL
187 ACT_VM_PULLPIN             188 ACT_VM_PRIMARYATTACK       189 ACT_VM_PRIMARYATTACK_2
190 ACT_VM_PRIMARYATTACK_3     191 ACT_VM_PRIMARYATTACK_4     192 ACT_VM_SECONDARYATTACK
193 ACT_VM_RELOAD_BEGIN        194 ACT_VM_RELOAD              195 ACT_VM_RELOAD_COMPLETE
196 ACT_VM_DRYFIRE             197 ACT_VM_HITLEFT             198 ACT_VM_HITLEFT2
199 ACT_VM_HITRIGHT            200 ACT_VM_HITRIGHT2           201 ACT_VM_HITCENTER
202 ACT_VM_HITCENTER2          203 ACT_VM_MISSLEFT            204 ACT_VM_MISSLEFT2
205 ACT_VM_MISSRIGHT           206 ACT_VM_MISSRIGHT2          207 ACT_VM_MISSCENTER
208 ACT_VM_MISSCENTER2         209 ACT_VM_HAULBACK            210 ACT_VM_SWINGHARD
211 ACT_VM_SWINGMISS           212 ACT_VM_SWINGHIT            213 ACT_VM_IDLE_TO_LOWERED
214 ACT_VM_IDLE_LOWERED        215 ACT_VM_LOWERED_TO_IDLE     216 ACT_VM_IDLE_EMPTY
217 ACT_VM_IDLE2_EMPTY         218 ACT_VM_FIDGET2             219 ACT_VM_EMPTIED
220 ACT_VM_EMPTIED2            221 ACT_VM_LOWER2              222 ACT_VM_LOWER
223 ACT_VM_IDLE2               224 ACT_VM_FIRE2               225 ACT_VM_DRAW2
226 ACT_VM_RELOAD2_BEGIN       227 ACT_VM_RELOAD2             228 ACT_VM_RELOAD2_COMPLETE
229 ACT_VM_DRYFIRE2            230 ACT_VM_TOPRIMARY1          231 ACT_VM_TOPRIMARY2
232 ACT_VM_LOCKPICK_IDLE       233 ACT_VM_LOCKPICK_START      234 ACT_VM_LOCKPICK_PICK
235 ACT_VM_LOCKPICK_END        236 ACT_VM_DRAW_LOCKPICK       237 ACT_VM_LOWER_LOCKPICK
238 ACT_VM_IDLE_LOCKPICK       239 ACT_VM_PRIMARYATTACK_LOCKPICK
240 ACT_TECH                   241 ACT_DISPOSITION            242 ACT_THROW_BODY
243 ACT_THROW_BODY_L           244 ACT_THROW_BODY_FAKE        245 ACT_THROW_BODY_L_FAKE
246 ACT_PICKUP_BODY_NORMAL     247 ACT_PICKUP_BODY_REVERSE    248 ACT_PICKUP_BODY_NORMAL_L
249 ACT_PICKUP_BODY_REVERSE_L  250 ACT_TZIM_DEATH_SPLIT1      251 ACT_TZIM_DEATH_SPLIT2
252 ACT_IDLE_BODY              253 ACT_IDLE_BODY_L            254 ACT_WALK_BODY
255 ACT_WALK_BODY_L            256 ACT_ROAR_LONG              257 ACT_ROAR_SHORT
258 ACT_POUNCE                 259 ACT_POUNCE1                260 ACT_POUNCE2
261 ACT_POUNCE3                262 ACT_CLAW_LEFT              263 ACT_CLAW_RIGHT
264 ACT_DROP_DOWN_IDLE         265 ACT_DROP_DOWN              266 ACT_SNAP_EXIT
267 ACT_ROOF_EGRESS            268 ACT_ROOF_JUMP_DOWN         269 ACT_WINDOW_LEAP
270 ACT_PATIO_LEAP             271 ACT_FENCE_LEAP             272 ACT_ROCK_LEAP
273 ACT_DESTROY_STAIRS         274 ACT_SQUEEZETHROUGH_FRONT_QUICK
275 ACT_SQUEEZETHROUGH_FRONT_SLOW        276 ACT_SQUEEZETHROUGH_LEFT_QUICK
277 ACT_SQUEEZETHROUGH_LEFT_SLOW         278 ACT_SQUEEZETHROUGH_RIGHT_QUICK
279 ACT_SQUEEZETHROUGH_RIGHT_SLOW        280 ACT_SQUEEZETHROUGH_FRONT_SLOW_RIGHT
281 ACT_SMASH_DOOR_SHOULDER_FRONT        282 ACT_SMASH_DOOR_SHOULDER_LEFT
283 ACT_SMASH_DOOR_SHOULDER_RIGHT        284 ACT_DEATH_INTO
285 ACT_DEATH_ATTACK           286 ACT_DEATH_FINALE           287 ACT_DEATH_OUTOF
288 ACT_PLAY_DEAD              289 ACT_OBS_DOOR_SQUEEZE       290 ACT_PLATFORM_JUMPDOWN
291 ACT_PLATFORM_JUMPUP        292 ACT_SNIFFING               293 ACT_SEARCH
294 ACT_PICKUP_LIGHTTHROW      295 ACT_PICKUP_LIGHT           296 ACT_PICKUP_LIGHTIDLE
297 ACT_PICKUP_LIGHTCARRY      298 ACT_HENGEYOKAI_TRANSFORM   299 ACT_TOOL_STAKE
300 ACT_TOOL_LOCKPICK          301 ACT_TOOL_HACKPANEL         302 ACT_TOOL_KEYBOARD
303 ACT_PLACE_BOMB             304 ACT_PRAY                   305 ACT_PRAYING_BEGIN
306 ACT_PRAYING_IDLE           307 ACT_PRAYING_END            308 <hole>
```

## The weapon-family blocks

Each block is `base + k`, with the family suffix appended to every template name. All 30 conform
byte-for-byte.

### Ranged template, 107 slots

```
 0 ACT_RUN                  1 ACT_WALK                 2 ACT_WALK_RELAXED      3 ACT_RUN_RELAXED
 4 ACT_WALK_45              5 ACT_WALK_90              6 ACT_WALK_135          7 ACT_WALK_225
 8 ACT_WALK_270             9 ACT_WALK_315            10 ACT_SNEAK            11 ACT_CROUCH
12 ACT_DODGE               13 ACT_DODGE_DUCK          14 ACT_DODGE_SIDE       15 ACT_IDLE
16 ACT_KICK                17 ACT_ALERT_FRONT_INTO    18 ACT_ALERT_L45_INTO   19 ACT_ALERT_L90_INTO
20 ACT_ALERT_L135_INTO     21 ACT_ALERT_R45_INTO      22 ACT_ALERT_R90_INTO   23 ACT_ALERT_R135_INTO
24 ACT_ALERT_180_INTO      25 ACT_ALERT_FRONT_OUTOF   26 ACT_ALERT_L45_OUTOF  27 ACT_ALERT_L90_OUTOF
28 ACT_ALERT_L135_OUTOF    29 ACT_ALERT_R45_OUTOF     30 ACT_ALERT_R90_OUTOF  31 ACT_ALERT_R135_OUTOF
32 ACT_ALERT_180_OUTOF     33 ACT_ALERT_FIDGET_AGRO_LOOKAROUND  34 ACT_ALERT_FIDGET_LOOKAROUND
35 ACT_TURN_LEFT           36 ACT_TURN_RIGHT          37 ACT_180_LEFT         38 ACT_180_RIGHT
39 ACT_90_LEFT             40 ACT_90_RIGHT            41 ACT_TURN_LEFT_ALERT  42 ACT_TURN_RIGHT_ALERT
43 ACT_180_LEFT_ALERT      44 ACT_180_RIGHT_ALERT     45 ACT_90_LEFT_ALERT    46 ACT_90_RIGHT_ALERT
47 ACT_LEAP                48 ACT_LEAP_ASCEND         49 ACT_LAND             50 ACT_HUNT_WALK
51 ACT_HUNT_WALK_LOOKLEFT  52 ACT_HUNT_WALK_LOOKRIGHT 53 ACT_WALKB            54 ACT_CORNER_COVER_IDLE
55 ACT_LEAN_LEFT_INTO      56 ACT_LEAN_RIGHT_INTO     57 ACT_LEAN_LEFT_IDLE   58 ACT_LEAN_RIGHT_IDLE
59 ACT_LEAN_LEFT_OUTOF     60 ACT_LEAN_RIGHT_OUTOF    61 ACT_AIM              62 ACT_RANGE_ATTACK
63 ACT_RANGE_ATTACK_LEANING_LEFT                      64 ACT_RANGE_ATTACK_LEANING_RIGHT
65 ACT_DRYFIRE             66 ACT_RELOAD              67 ACT_RELOAD_FAST      68 ACT_RANGE_ATTACK_LAYER
69 ACT_DRYFIRE_LAYER       70 ACT_RELOAD_LAYER        71 ACT_CRUNCH_INTO      72 ACT_CRUNCH_IDLE
73 ACT_CRUNCH_OUTOF        74 ACT_RANGE_ATTACK_CROUCHED                       75 ACT_MIDCRUNCH_INTO
76 ACT_MIDCRUNCH_IDLE      77 ACT_MIDCRUNCH_OUTOF     78 ACT_MIDCROUCH        79 ACT_RANGE_ATTACK_MIDCROUCHED
80 ACT_VM_DRAW             81 ACT_VM_DRAW2            82 ACT_VM_LOWER         83 ACT_VM_LOWER2
84 ACT_VM_FIDGET           85 ACT_VM_FIDGET2          86 ACT_VM_TOPRIMARY1    87 ACT_VM_TOPRIMARY2
88 ACT_VM_IDLE             89 ACT_VM_IDLE2            90 ACT_VM_IDLE_EMPTY    91 ACT_VM_IDLE2_EMPTY
92 ACT_VM_EMPTIED          93 ACT_VM_EMPTIED2         94 ACT_VM_PRIMARYATTACK 95 ACT_VM_FIRE2
96 ACT_VM_DRYFIRE          97 ACT_VM_DRYFIRE2         98 ACT_VM_RELOAD_BEGIN  99 ACT_VM_RELOAD2_BEGIN
100 ACT_VM_RELOAD         101 ACT_VM_RELOAD2         102 ACT_VM_RELOAD_COMPLETE
103 ACT_VM_RELOAD2_COMPLETE                          104 ACT_VM_PULLBACK     105 ACT_VM_THROW
106 ACT_VM_ROLL
```

| family | base | family | base |
|---|---|---|---|
| `PISTOL` | 309 | `TWOHANDED` | 417 |
| `ANACONDA` | 525 | `SMITH` | 633 |
| `DESERTEAGLE` | 741 | `GLOCK` | 849 |
| `CROSSBOW` | 957 | **`M37`** | **1065** |
| `ENFIELD` | 1173 | `REM700` | 1416 |
| `STEYR` | 1524 | `SUPERSHOTGUN` | 1632 |
| `FLAMETHROWER` | 1740 | `THROWING_STAR` | 1848 |

### Melee template, 111 slots

Offsets 0 … 53 are identical to the ranged template. Then:

```
54 ACT_READY               55 ACT_PREBLOCK            56 ACT_BLOCK           57 ACT_BLOCK_HEAVY
58 ACT_DODGE_ATTACK        59 ACT_MELEE_ATTACK        60 ACT_MELEE_AIR_ATTACK
61 ACT_MELEE_ATTACK_2COMBO 62 ACT_MELEE_ATTACK_HEAVY  63 ACT_MELEE_ATTACK_FROMCROUCH
64 ACT_STEPBACK            65 ACT_COMBATMOVE          66 ACT_BLOCKED_REACTION_LEFT
67 ACT_BLOCKED_REACTION_RIGHT
68..75  ACT_SNEAKATTACK_{SUCCESS,FAILURE}_{ATTACKER_SHORTVICTIM, ATTACKER_TALLVICTIM,
                                           VICTIM_SHORTATTACKER, VICTIM_TALLATTACKER}
76..106 the 31 ACT_KNOCKBACK_* forms, in the same order as base-block 117..147
107..110 ACT_FINISHING_MOVE_{ATTACKER_SHORTVICTIM, ATTACKER_TALLVICTIM,
                             VICTIM_SHORTATTACKER, VICTIM_TALLATTACKER}_FRONT
```

| family | base | family | base |
|---|---|---|---|
| `BASEBALLBAT` | 1956 | `BUSHHOOK` | 2068 |
| `CLAWS` | 2180 | `FISTS` | 2292 |
| `KATANA` | 2404 | `KNIFE` | 2516 |
| `SHERIFFSWORD` | 2628 | `SLEDGEHAMMER` | 2740 |
| `TIREIRON` | 2852 | `BATON` | 2964 |
| `WOLFHEAD` | 3076 | `MELEESHARED_ONEHAND` | 3188 |
| `MELEESHARED_TWOHAND` | 3300 | `WEREWOLF` | 3412 |
| `CHANG_CLAW` | 3524 | `CHANG_BLADE` | 3636 |

### The irregular blocks

- **SMG, 1281 … 1414.** Ranged offsets 0 … 79 suffixed `_SUBMACHINEGUN` (1281 … 1360), then the 27
  `ACT_VM_*` slots **twice**: `_MAC10` at 1361 … 1387 and `_UZI` at 1388 … 1414.
- **GRENADE, 3748 … 3862.** Ranged offsets 0 … 79 suffixed `_GRENADE`, then
  `ACT_{PULLBACK,PULLBACK_LAYER,HOLD,HOLD_LAYER,THROW,THROW_LAYER,ROLL,ROLL_LAYER}_GRENADE`
  (3828 … 3835), then the 27 `ACT_VM_*` suffixed `_GRENADE_PINEAPPLE` (3836 … 3862).

### The holes

One per family-block boundary, 32 in total: 308, 416, 524, 632, 740, 848, 956, 1064, 1172, 1280,
1415, 1523, 1631, 1739, 1847, 1955, 2067, 2179, 2291, 2403, 2515, 2627, 2739, 2851, 2963, 3075,
3187, 3299, 3411, 3523, 3635, 3747.

## The grapple set

29 values go through `RegisterGrappleActivity` and land in a second list that
`NPC_EarlyTranslateActivity` tests membership against before applying the role/size/orientation
arithmetic: **148** (`ACT_FINISHING_MOVE`), then 3904, 3913, 3922, 3931, 3940, 3949, 3958, 3967,
3976, 3985, 3994, 4005, 4014, 4023, 4032, 4042, 4051, 4060, 4069, 4078, 4087, 4096, 4105, 4117,
4126, 4135, 4144, 4153 — the payphone, feeding, seductive-feed, zombie-feed, sneak-attack and
rat-feed **base** verbs.

Each base verb is followed by eight role forms in the order `ATTACKER_SHORTVICTIM_FRONT,
ATTACKER_TALLVICTIM_FRONT, VICTIM_SHORTATTACKER_FRONT, VICTIM_TALLATTACKER_FRONT,
ATTACKER_SHORTVICTIM_BACK, ATTACKER_TALLVICTIM_BACK, VICTIM_SHORTATTACKER_BACK,
VICTIM_TALLATTACKER_BACK` — **except the four `ACT_ZOMBIE_FEEDING_{ENGAGE,IDLE,BITE,FEED_LOOP}`
families (4042, 4051, 4060, 4069), which list VICTIM before ATTACKER.**

## The tail block, 4162 … 4492

The disposition, madness, prop-idle, boss and late-added set, in registration order. A trap worth
naming: the **unsuffixed** `ACT_ALERT_*`, `ACT_CRUNCH_*`, `ACT_MIDCRUNCH_*`, `ACT_HUNT_WALK*`,
`ACT_LEAN_*`, `ACT_CORNER_COVER_IDLE`, `ACT_COMBATMOVE`, `ACT_PREBLOCK`, `ACT_BLOCK_HEAVY`,
`ACT_DODGE_DUCK`, `ACT_DODGE_SIDE`, `ACT_DODGE_ATTACK` and `ACT_BLOCKED_REACTION_*` sit at
**4344 … 4440**, far above their weapon-suffixed twins, because they were registered after the
family blocks. Sorting by number and assuming a base verb precedes its suffixed forms gets this
family wrong.

Notable anchors in the tail: `ACT_DISPOSITION_AFRAID` 4162, `ACT_DIALOG_SCRIPTED_SEQUENCE` 4172,
`ACT_SLEEP_IDLE` 4177, `ACT_CHOKING` 4182, `ACT_MADNESS_IDLE` 4185, `ACT_JUMP_TO_DEATH` 4233,
`ACT_VISION_OF_DEATH` 4235, `ACT_PANIC_RUN` 4243, `ACT_LAP_DANCE` 4298, `ACT_POLE_DANCE` 4299,
`ACT_MING_XIAO_TRANSFORM` 4403, `ACT_ANDREI_SUMMON` 4410, `ACT_WOLF_MORPH` 4421,
`ACT_SHERIFF_TAUNT` 4423, `ACT_WALKIE_TALKIE` 4492.

## Sequence selection

`CBaseAnimating::SelectWeightedSequence(Activity, int statGate)` (`0x1008dc40`) is three stages:
gather candidates across the three model slots, then a weighted draw.

```c
int PickWeighted(int n, int *seqs, int *weights) {
    if (n < 1)  return -1;
    if (n == 1) return seqs[0];
    total = sum(weights);
    if (total > 0) {
        g_lastActivityRoll = RandomInt(0, total - 1);        // 0x106ac398
        r = g_lastActivityRoll; i = 0;
        while (weights[i] <= r) { r -= weights[i]; if (++i >= n) return -1; }
        return seqs[i];
    }
    g_lastActivityRoll = RandomInt(0, n - 1);                // all weights zero -> uniform
    return seqs[g_lastActivityRoll];
}
```

- **Returns `-1` when nothing matched.**
- **A weight of 0 is unreachable while any sibling is positive** — the walk tests `weights[i] <= r`.
- The candidate arrays are **2048 entries with no bounds check**.
- RNG is the engine's `CEngineUniformRandomStream` (`VEngineRandom001`, pointer at `0x1070b244`),
  slot 2 = `RandomInt(min, max)`, unseeded per call.
- **There is no "same as last time" avoidance.** The only memory is `g_lastActivityRoll`, and it is
  consumed by a different function.

`SelectHeaviestSequence` (`0x1008dd30`) takes the maximum `actweight` with a strict `<`, so ties
keep the first (lowest global index). `SelectSameSequence` (`0x1008de20`) **replays the stored
roll** against a different candidate list instead of drawing — a correlated draw, and the mechanism
by which the hands viewmodel lands on the same variant the weapon drew.

### The include-shadowing rule

`Studio_GetSequencesForActivity` (`0x10427df0`) initialises a flags sentinel to `0x80` and
overwrites it with the flags of each **local** match. It descends into the include groups only when
that sentinel still has `0x80` set. So **a model with no local match always descends**, and only a
local match *without* `flags & 0x80` shadows the banks — and the gate is the **last** local match's
flags, not an OR over all of them.

The base offset accumulates across the three model slots as
`(numincludemodels == 0) ? numlocalseq : lastGroup.seqbase + lastGroup.seqcount`, which is what
makes global sequence numbers flat across them.

## The translation chain

Vtable slots (slot × 4 = byte offset):

| slot | offset | name | base |
|---|---|---|---|
| 375 | `+0x5dc` | `NPC_EarlyTranslateActivity` (pre-translate) | `0x10328030` |
| 376 | `+0x5e0` | `NPC_TranslateActivity` (class translate) | `0x10328110`, identity |
| 381 | `+0x5f4` | `Weapon_TranslateActivity` | `0x10327ec0` |
| 361 | `+0x5a4` | `CBaseCombatWeapon::ActivityOverride` | `0x1024f210`, one impl for all 254 weapon classes |
| 362 / 363 | `+0x5a8` / `+0x5ac` | the weapon's activity table pointer and count | per-class `MOV EAX,imm32; RET` |

### The NPC loop — `CAI_BaseNPC::TranslateActivity` `0x10271ff0`

```c
A = NPC_EarlyTranslateActivity(request);       // +0x5dc
B = Weapon_TranslateActivity(A);               // +0x5f4
if (pIdealWeaponActivity) *pIdealWeaponActivity = B;
lastClass = A;  cur = A;
for (i = 1; i <= 5; i++) {
    C = NPC_TranslateActivity(cur);            // +0x5e0
    if (C != cur) lastClass = C;
    D = Weapon_TranslateActivity(C);           // +0x5f4
    if (D == cur) break;
    cur = D;
}
if (cur == ACT_SCRIPT_CUSTOM_MOVE) return cur;                        // no availability probe
if (SelectHeaviestSequence(D,         -1) >= 0) return D;             // rung 1
if (lastClass != D && SelectHeaviestSequence(lastClass, -1) >= 0) return lastClass;   // rung 2
if (B != lastClass && SelectHeaviestSequence(B, -1) >= 0) return A;   // rung 3 -- tests B, RETURNS A
if (A != B && SelectHeaviestSequence(A, -1) >= 0) return A;           // rung 4
if (A == ACT_RUN) A = ACT_WALK;
return A;
```

Two details that a paraphrase gets wrong. **Rung 4 tests `A`, the pre-translate result, not the
original request** — the request's stack slot is overwritten at `0x1027200f` and is unrecoverable
inside the function. And **rung 3 tests `B` but returns `A`**, so rungs 3 and 4 yield the same value
and differ only in the predicate that lets you reach the tail. That reads as a Troika variable-naming
slip; it is recorded here as behaviour, not intent.

### The player's chain

`0x101644f0` calls the two virtuals inline, in the order `+0x5f4` then `+0x5e0`, with nothing before
them, no loop, no availability probe and no terminal `ACT_RUN → ACT_WALK`. The **same** two-call
chain is applied a second time to the overlay/layer activity argument.

`CBasePlayer::NPC_TranslateActivity` (`0x101647a0`) is two rows: `ACT_WALK_RELAXED → ACT_WALK`,
`ACT_RUN_RELAXED → ACT_RUN`. Everything else passes through.

### The weapon tables

`ActivityOverride` (`0x1024f210`) walks a per-class table of **12-byte rows**
`{int from; int to; int required;}`. `required` is `0` or `1` and **`ActivityOverride` never reads
it**, so a duplicate `from` row is a plain ordered fallback regardless. For each matching row it asks
the owner whether the target is playable (`NPC_TranslateActivity` then `SelectHeaviestSequence`),
then the view-model hands, and returns the first that answers; if none does, the input passes
through unchanged.

**This is how the label `run` reaches a different bank per weapon**, as an ordered fallback ladder:

| weapon | `ACT_RUN` ladder |
|---|---|
| `Rifle_M37` | `ACT_RUN_M37` → `ACT_RUN_TWOHANDED` |
| `Pistol_Glock` | `ACT_RUN_GLOCK` → `ACT_RUN_PISTOL` |
| `Katana` | `ACT_RUN_KATANA` → `ACT_RUN_MELEESHARED_ONEHAND` → `ACT_RUN_BASEBALLBAT` |
| `ChangBlade` | `..._CHANG_BLADE` → `..._KATANA` → `..._MELEESHARED_ONEHAND` → `..._BASEBALLBAT` |
| `Sledgehammer` / `FireAxe` | `..._SLEDGEHAMMER` → `..._MELEESHARED_TWOHAND` → `..._BUSHHOOK` |
| `Fists` / `ZombieFists` | rows 0–2 map `ACT_RUN_RELAXED → ACT_RUN`, `ACT_WALK_RELAXED → ACT_WALK`, `ACT_SNEAK → ACT_SNEAK` **first**, ahead of the `_FISTS` rows |

That last row is load-bearing: since a player body always has `ACT_RUN`, rung 0 always succeeds, so
**an unarmed player's relaxed gait is the plain `run`/`walk` and `ACT_RUN_RELAXED_FISTS` is
unreachable on any body carrying `ACT_RUN`.**

The M37's table is at `0x105ba680` with **245 rows** — one complete ranged block retargeting `_M37`,
followed by a second complete block retargeting the `TWOHANDED` family.

## Provenance

Read from `vampire.dll` (server) and `client.dll` via the RE corpus under `ELYSIUM_WORK_ROOT`, with
static tables decoded out of the shipped DLL images. The registration order, the family bases and
the template conformance were machine-verified across all 30 blocks. Census figures over the
installed corpus (4,445 v2531 models, 14,012 sequence descriptors) come from this repo's own
decoder.

`client.dll` carries the full `ACT_*` string block but **no activity registry and no function that
references those strings** — the client's activity numbers come entirely from the model file's
resolved `seqdesc+0x0C`, which the server wrote. Its one behavioural divergence: the client's
`LookupSequence` falls back to an **activity-name** lookup when the label lookup misses
(`client.dll 0x10079cf0`), which the server's does not.
