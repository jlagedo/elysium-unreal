# NPC AI — The object's shape

What the NPC object *is* in `vampire.dll`: its layout past what the datamaps save, the words the
constructors and debug paths own, and the virtuals the SDK 2013 headers declare differently or not
at all. The row-by-row record is generated — `../npc-kernel/layout.md` (every member, typed) and
`../npc-kernel/signatures.md` (every slot's declaration) — from the datamap records, the image, and
the two reading overlays `research/tooling/ghidra/driver/kernel_fields.tsv` and
`kernel_signatures.tsv`, whose evidence column cites the bodies each row was read from. This file
holds the facts that cut across rows.

## The tables

_Recovered 2026-09-13, story 29b-0._

- `CAI_BaseNPCTroika`'s primary vtable holds **617** slots, not 600. `DumpVtables.java` walked
  600 per table and stopped, so slots 600–627 had no corpus row; every entry past 600 is an
  incremental-link `JMP` thunk the dump resolves to its body. The walk now ends only at the next
  table or a non-code entry (4,096 is a runaway guard), and the corpus was re-dumped: 1,153 more
  slot rows in `vampire.dll`. 64 family tables carry 600–616.
- Past a base's table, each branch declares its **own** virtuals at the same index. One
  signature per slot is wrong there: `CAI_BaseNPC`'s table ends at 583, and `CAI_BaseActor`
  (under `CAI_BaseHumanoid`), `CAI_ExpressiveNPC` and `CCineNPC` each introduce unrelated
  virtuals from 583 (`CreateExpresser`/`Speak`/`PickLookTarget`…, the expresser template's pair,
  `PossessEntity`/`StartSequence`/`FCanOverrideState`/`FixScriptNPCSchedule` in HL1
  `CCineMonster` order). Troika's table ends at 616, and from 617 the `CNPC_VBaseBoss`,
  `CNPC_VVampireBoss`, `CNPCMaker`, `CNPC_VGargoyle`, `CNPC_VHengeyokai`, `CPayphone` branches, and
  individual bosses below them (`CNPC_VTzimisce` to 627), each start their own run.
  `signatures.md` carries one row per (slot, introducing class).
- Slot 0 is `IHandleEntity::SetRefEHandle`: retail `IHandleEntity` has no virtual destructor; the
  scalar deleting destructor is slot 5.
- Overloaded virtuals sit in reverse declaration order (MSVC): `KeyValue` 108–110, `CanStandOn`
  165/166, `GetEnemy` 167/168, `AddFacingTarget` 517–519.

**Unrecovered:** nothing.

## The layout the datamaps do not save

_Recovered 2026-09-13, story 29b-0._

- `+0x19b0` is `CAI_DefMovementSink`'s vtable pointer: the base constructor `0x1027c300` installs
  `vftable_CAI_DefMovementSink`, then `vftable_CAI_BaseNPC_at6576`.
- `+0x19b4..+0x1a3f` is a member `AIExtendedSaveHeader_t` (0x8c: `version` short = 1, `flags`,
  `szSchedule[128]`, `scheduleCrc`) that `Restore` `0x1027c160` reads through its own datamap —
  SDK 2013 keeps it on the stack.
- `+0x1a48..+0x1a93` is `m_DeferredDeathInfo`, a whole retail `CTakeDamageInfo` (0x4c bytes, a
  `CVDmg_t*` at +0) that `Event_Killed` stores when a scripted sequence defers the death.
- `+0x1b2c..+0x1b4b` are three debug stamps written before a selector runs, before every write of
  `m_IdealNPCState` `+0x5cc4`, and before each `TaskFail` (slot `+0x700`): `{selector tag, file,
  line}`, `{tag, file, line}`, `{file, line}`. Nothing reads them. The tag names the class whose
  selector ran (1 base, 4 AndreiBlood, 5 Animal, 9 Camera…).
- `+0x1b4e..+0x5b4d` is a **16,384-byte circular text log**: `0x1027ef20` `sprintf`s into it at
  the cursor `+0x5b50` (wrap flag `+0x5b54`), `0x1027efb0` prints it between `BEGIN/END BUFFER
  DUMP FOR %s`. `NPCThink` tests and clears the dump request `+0x5b55`; no body in the image sets it.
- Retail's condition sets are **six words (192 bits)**, and there are three before
  `m_bConditionsGathered` `+0x5ca4`, not SDK 2013's four: `m_Conditions` `+0x5c5c`,
  `m_CustomInterruptConditions` `+0x5c74` (copied from the schedule's `+0x28` mask),
  `m_InverseInterruptConditions` `+0x5c8c` (the schedule's `+0x00` inverse mask, tested as mask
  AND NOT conditions). `IsScheduleValid` `0x10280ff0` scans bits 0..0xbf. A fourth set,
  `m_HeardConditions` `+0x5ca8`, holds the `HEAR_*` bits `OnListened` raised.
- Retail `CUtlVector` is **0x14** bytes (`m_Memory` 12, `m_Size` +0xc, `m_pElements` +0x10), so
  `+0x5d5c` after `m_UnreachableEnts` is its own member (`m_flCheckOnGroundTime`).
- `+0x660c` is `m_LastTakeDamageInfo` (a `CTakeDamageInfo` `OnTakeDamage` `0x102beda0` copies the
  incoming packet into; nothing reads it), `+0x6659` is Troika's `CScheduleLoader`
  (`DEFINE_CUSTOM_SCHEDULE_PROVIDER`), and Troika's words end at `+0x665c`, where `CNPCMaker`,
  `CNPC_VAnimal` and `CNPC_VBaseBoss` begin — not at the datamap's `+0x660c`. Every species that
  declares schedules carries its own loader byte (Bach `+0x66a9`, Cop `+0x6673`, Guard1 `+0x6661`,
  ManBat `+0x66b9`, MingXiao `+0x6751`, Tzimisce `+0x66bd`, …).
- `m_iPLCriminalLevelWitnessed` `+0x635c` (and ManBat `+0x6668`, Hengeyokai `+0x6698`) is a
  SafeDisc `MvsnSec::CSecureType<int>`: a vtable, two bytes copied from uninitialised stack that
  nothing reads, and the encoded value at +8.
- `CBaseEntity +0x94..+0xb0` are per-class self pointers each constructor fills (`+0x94`
  `CAI_BaseNPC`, `+0x98` Troika, `+0x9c` `CBaseCombatCharacter`, `+0xa8` `CBasePlayer`, `+0xac`
  `CNPC_VAnimal`, …); `PhysicsDispatchThink` reads `+0x5c38` through `+0x94`.
- `CNPC_VBaseBoss` `+0x665c` is a `CUtlVector` of `{EHANDLE, expiry}` (`0x103662d0` add,
  `0x10366400`/`0x10366490` test and expire) that Werewolf uses to lock hints and MingXiao to skip
  a thrown object for 20 s; Hengeyokai repeats the same code at `+0x66a4`.

**Unrecovered:** 22 layout rows stay `unsettled` in `layout.md` with their reasons. Fourteen are
the un-named members of retail `CTakeDamageInfo` in its two stored copies (`m_DeferredDeathInfo`
and Zombie's `m_DeathDamageInfo`): the int at +0, the three floats at +4..+0xc that default to
1.0, the float at +0x44, and the three bools at +0x48..+0x4a — only their defaults and copies
were walked; `Event_TookLife` receives +0x48/+0x49 as its two flags (slot 300), which is the next
body to read. Seven are words a constructor defaults and nothing reads (Troika `+0xa9c`,
`+0x10d8`, `+0x1b4d`, `+0x5b58`, `+0x5d00`, `+0x6658`; Yukie `+0x6664`). One is Hengeyokai's
secure int `+0x6698` (it holds 30), whose two consumers are unwalked.

## Virtuals whose retail signature differs from SDK 2013

_Recovered 2026-09-13, story 29b-0._

The rows in `signatures.md` carry the evidence; the ones a kernel story ports onto:

- `RunAI(bool)` (432): when set, `GatherConditions` is skipped, `MaintainSchedule` runs one pass,
  and the end-of-pass clear of conditions `0x4c`/`0x4d`/`0x38` is skipped. Troika's `NPCThink`
  passes "AI think not yet due"; the base and camera pass 0.
- `OnScheduleChange(CAI_Schedule*)` (435) receives the incoming schedule (`SetSchedule` the new
  program, `ClearSchedule` the cleared `+0x5c38`); the base hands it to the navigator's `+0x10`.
- `QuerySeeEntity(CBaseEntity*)` (468) has no `bOnlyHateOrFearIfNPC`; `SelectIdealState` (461)
  returns `NPC_STATE`, with `PreSelectIdealState` (460) gating it as 437 gates 438.
- `EnterGrappleState(CBaseCombatCharacter*, int role, int type, int position, const Vector&,
  float yaw, bool holster)` (379); `LeaveGrappleState()` (380).
- `Weapon_TranslateActivity(Activity)` (381) has no `bool*`; `Weapon_Equip(CBaseCombatWeapon*,
  bool)` (383) stows without drawing when set; `CorpseGib()` returns bool with no damage info.
- `TraceAttack(const CTakeDamageInfo&, const Vector&, trace_t*)` (141) has no accumulator;
  `OnRestore(bool)` (130); `OnSave()` (129); `FVisible` (201) takes a fourth int choosing the
  target point (eyes, box centre, eight corners).
- `ProcessTweakParam(const char*, const char*)` (585) is Troika's; the 3-word and 0-word bodies
  at 585 belong to other branches.
- The Troika line 600–616: 600/601 acquire and release a melee slot on the attack coordinator
  for a `CBaseEntity*`, 603 is `float(float*, const float*)` tail-jumping into `RandomFloat`,
  608 binds the attack coordinator by name (`"Normal"`), 609 returns the shoot-at `CAI_Hint*`,
  610 picks the Anger/Fear expression for an `NPC_STATE`, 614 `ResetThinkTimers`, 615
  `CanBeSetOnFire`, 616 clears ON_FIRE and re-arms the burn timer 15 s out.
- `CAI_BaseNPCTroika::GetEnemy` (168, `0x102b5360`) falls back to `m_hLastEnemy` when there is no
  enemy and `m_bfNPCStateFlags` bit 6 is set.

**Unrecovered:** 9 slot rows stay `unsettled` — empty bodies or pass-throughs with no call site on
an NPC (156, 182, 206, 317, 338–340; Werewolf 618/619): the arity is the image's, the parameter
types are not observable.

## The damaged bodies of layers 0–9, read from the listing (2026-09-13)

`coverage.md` marks 21 bodies of the kernel's first ten layers ‼: Ghidra lost a jump table in each
and the decompiled C shows only a fragment, usually the tail jump. Every one was read with
`corpus asm` for story 29c, and all but two turn out to be short. Thirteen are **slot forwards** —
the whole body is one `JMP [vtable + 0xNNN]`, which is slot `0xNNN / 4` of the same object — and a
forward is a fact about the table, not a missing body:

| Body | Slot | Forwards to | With |
|---|---|---|---|
| `0x10027020` | 168 `GetEnemy()` | 167 `GetEnemy() const` | nothing changed |
| `0x100aa5a0` | 130 `OnRestore(bool)` | 6 `SetCheckUntouch(bool)` | its own argument replaced by `0` |
| `0x100b4bc0` | 194 `EyeAngles()` | 219 `GetAbsAngles()` | nothing changed |
| `0x100b4be0` | 195 `LocalEyeAngles()` | 221 `GetAngles()` | nothing changed |
| `0x102d08c0` | `CAI_Hint`'s 119 `Kill()` | 77 `ScriptHide()` | nothing changed — a hint node's `Kill` hides it rather than tearing it down |
| `0x10026c50` | 133 `MoveDone()` | the callback at `+0x114` | only when one is set |
| `0x102623a0` | `CAI_Motor`'s 1 | the owner NPC's 448 `TaskFail` | through `m_pOuter +0x4` |
| `0x102eeb50` | `CAI_Navigator`'s 9 | `CAI_Navigator`'s 10 | its second argument copied down one word |
| `0x10278cb0` | 519 `AddFacingTarget(CBaseEntity*, float, float, float)` | the motor at `+0x5d44`'s 14 | gated, see below |
| `0x10278d20` | 518 `AddFacingTarget(const Vector&, …)` | the motor's 13 | the same gate |
| `0x10278d90` | 517 `AddFacingTarget(CBaseEntity*, const Vector&, …)` | the motor's 12 | the same gate |
| `0x102dbc80` | `CAI_InterestingPlaceConverstation`'s 103 `Spawn` | 104 `Precache` | after its own field init — this class precaches at the END of spawn |
| `0x103b92a0` | `CNPC_VTzimisce`'s 488 | 487 | after firing the script event `SPI_DIES` |

The three `AddFacingTarget` overloads share one gate: the engine interface answers `false` to its
slot 1 **and** holds a non-null `+0x2c`, else the call returns and no facing target is added.

The rest carry a rule:

- **`0x100b4ea0` slot 154 `DamageDecal(int, int)`** — `-1` when `m_nRenderMode +0x16c` is `4`;
  `0x34` when the render mode is non-zero **and** the second argument is `0x47`; otherwise the
  decal singleton at `0x1070b244` slot 2, called with both arguments replaced by `(0, 4)`.
- **`0x1026d7f0` slot 459 `RemoveIgnoredConditions`** — acts only while `m_NPCState +0x5cc0` is
  `4` and the handle at `+0x5d74` still resolves through the entity table, then dispatches that
  entity's own slot 459. Outside state 4 it returns having touched nothing.
- **`0x1027db30`** — the task-index lookup: bounds-check the index against the current schedule's
  task list (`+0x5d34` → `+0x2c`), bump the global failure counter `0x106c994c` and answer false
  when it is negative or past the end, answer false on a null task, else tail-jump to slot 527
  with the task pointer.
- **`0x10272650` `SetIdealActivity(Activity)`** — activity `0` tail-jumps to slot 310 with `0`;
  anything else stores the logical activity at `+0xff0` and resolves it into the ideal-activity
  triple `+0x5ccc`/`+0x5cd0`/`+0x5cd4` **without committing it**.
- **`0x100b6250` slot 283 `ProcessSceneEvents`** — dispatches slot 279 with `(index, 0)` for each
  scene event, then tail-jumps to slot 284. The count is re-read every iteration, so an event
  that removes itself shortens the loop.
- **`0x101c1720` slot 133 `MoveDone`** (`CAI_BaseNPC`'s own) — copies `+0x494` into `+0x498`,
  switches on the pending move mode at `+0x558` (1 runs the arrival helper on `this`, 2 the
  global one), clears `+0x558`, and only then dispatches the callback at `+0x114`.
- **`0x103a4a60` `CNPC_VPlayerController::RemoveExtraAnimationModels`** — clears its own list,
  asks slot 97 `GetOwnerEntity`, and forwards to the owner's `+0xa8` object's slot 246; with no
  owner it warns *"Player Controller NPC removing extra animations, but is not attached to a
  player. This is probably bad."*
- **`0x100e58e0` `CDialog::message_send`** — a `CDialog` body the kernel closure reaches through a
  numeric offset collision, not an NPC method: it resolves the speaker handle at `+0x4`, prints
  the response under `"\n\n NPC Response: %s"`, writes line 0 then lines `1..m_LineCount +0x2834`
  into the sheet, and takes the has-reply branch (notify the speaker, close on 0) against the
  no-reply branch (close on 1). It belongs to the dialogue session, not to this kernel.

**Unrecovered:** two of the 21 are still `unsettled` after the listing. `0x102702d0` is an
`__ftol` with no visible argument compared against `0xfff0bdbd`/`0xfff0bdbe`/`0xfff0bdc0`, which
are x87 status-word artifacts rather than domain values, and the listing shows the same folded FPU
sequence. `0x1028e870` had its float at `+0x62cc` bound to the return-storage pointer and lost the
comparison against the constant at `0x104454c4` inside `0x1028e830`; what it answers on each side
of that compare is not recoverable. A third row outside the damaged set, `0x102623e0`
(`CAI_Motor` slot 11), returns an unassigned register and has no caller of any kind.

## The species vocalization table — slots 488–508, 620, 621

Twenty-one sound hooks sit in the 488–508 band (`DeathSound`, `AlertSound`, `IdleSound`,
`PainSound`, `FearSound`, `LostEnemySound`, `FoundEnemySound`, `SurprisedSound`,
`TargetAcquiredSound`, `FleeSound`, `IdleAgitatedSound`, `ExertHvySound`, `ExertLightSound`,
`RiledSound`, `ComfortSound`, `UpsetSound`, `TargetGiveUpSound`, `FloatSound`, `SpeakSentence` and
the two unnamed 497/506), and `CNPC_VSabbatLeader` adds `FootstepSound` (620) and `AttackSound`
(621) past the end of the Troika table. Twenty-six species bodies of layers 0–4 fill them, and they
are **one behaviour written many times**, not twenty-six behaviours:

    CPASAttenuationFilter filter( GetSoundEmissionOrigin() /*slot 222*/, attn );
    filter.MakeReliable();
    EmitSound( filter, engine->IndexOfEdict(edict()), channel,
               WavTable[ RandomInt(0, N) ], volume, 0.8f, 0, 100, NULL, NULL, true, 0 );

Every one of them **returns without calling the Troika-line body**, so a species row is a
replacement and never a modifier. The attenuation is `0.8` (`ATTN_NORM`) and the pitch `100` on all
twenty-six; only the wav table, `N`, the volume and the channel vary. The PAS filter is a recipient
cull single-player never runs.

**`CGeneric_NPC`** (`0x10359f70` precaches) and **`CGenericSabbat_NPC`** (`0x1035b5d0`) fill the
same four hooks with the same four wav paths out of two separate `.rdata` pointer tables:
488 `npc/metropolice/die1.wav` at volume **0.5** (`0x1035a500` / `0x1035bb70`), 489
`npc/metropolice/alert1.wav` (`0x1035a390` / `0x1035ba00`), 491 `npc/citizen/pain{1..4}.wav` with
`RandomInt(0, 3)` (`0x1035a670` / `0x1035bce0`), 495 `npc/metropolice/surprise1.wav`
(`0x1035a220` / `0x1035b890`). All on `CHAN_VOICE`.

**`CNPC_VTest`** (`0x103b41e0` precaches) fills eight, all `character/npc/test/*.wav` on
`CHAN_VOICE`: 488 `death1` at volume 0.5 (`0x103b4320`), 489 `alert1` (`0x103b4490`), 490 `idle1`
(`0x103b4600`), 491 `pain{1..4}` (`0x103b4780`), 492 `fear1` (`0x103b48f0`), 493 `lostenemy1`
(`0x103b4a60`), 494 `foundenemy1` (`0x103b4bd0`), 495 `surprise1` (`0x103b4d40`). **Slot 490 is the
only one of the eight that opens with a `FOkToMakeSound()` refusal** (`CALL [EAX+0x798]` at
`103b4609`): the idle vocalisation is rate-limited and the reactive ones are not.

**`CNPC_VSabbatLeader`** fills its two extended slots on `CHAN_BODY`, both inside a VPROF scope:
620 `FootstepSound` (`0x103aa5e0`) draws `RandomInt(0, 6)` over
`character/monster/andrei_transformed/step{1..7}.wav`, and 621 `AttackSound` (`0x103aa7a0`) draws
`RandomInt(0, 2)` over `exert_heavy_{1..3}.wav`. The footstep is **not** an animation event —
`docs/vtmb/footsteps.md` records that this class is absent from the `HandleAnimEvent` override set
because retail drives its step from the schedule tasks `TASK_VSABBATLEADER_PLAY_FOOTSTEP_SOUND` /
`..._STOP_FOOTSTEP_SOUND`.

**`CNPC_VCamera`** fills nineteen of the band's slots with an empty body — one byte of `RET` each
(`0x103680b0` 488, `0x103680d0` 489, `0x103680f0` 490, `0x10368110` 491, `0x10368130` 492,
`0x10368150` 493, `0x10368170` 494, `0x10368190` 495, `0x103681b0` 496, `0x103681f0` 498,
`0x10368210` 499, `0x10368230` 500, `0x10368250` 501, `0x10368270` 502, `0x10368290` 503,
`0x103682b0` 504, `0x103682d0` 505, `0x10368310` 507; `0x10368330` at 508 is three bytes because
`SpeakSentence(int)` still has to pop its argument). `CNPC_VCameraSecurity` derives from
`CNPC_VCamera` and inherits every one of them. A security camera is completely mute — and that
matters because the Troika-line body behind slot 490 (`0x10294280`) does a real idle-sound-modifier
lookup and emit, which the camera suppresses.

**`CNPC_VTzimisce`** answers a **sentence group** rather than a wav pool, through
`SENTENCEG_PlayRndSz(edict(), group, volume, soundlevel, 0, pitch)` (`0x101aeb60`) with all three
numbers read off ConVar objects (`0x1093cf94`, `0x1093cfdc`, `0x1093cebc`) shared by its whole
sound family. Slot 490 (`0x103b9380`) is `FOkToMakeSound()`-gated and plays `"SPI_IDLE"`; slot 491
(`0x103b9500`) is gated, plays `"SPI_TAKE_DAMAGE"` and then calls slot 487 `JustMadeSound()`
(`CALL [EDX+0x79c]` at `103b9592`) — the only vocalisation in the family that re-arms the sound
clock itself. Outside the gate it always calls `0x103b9f90(this, 1, 1.0)`, which is
`CBaseCombatCharacter::SetExpression(ExpressionTable[1], 0.0, 0.15, max(1.0 - k, floor), 0.15, 1.0)`
— the pain **facial** expression.

**Unrecovered:** the three Tzimisce ConVars' names and defaults (the corpus has the objects, no
registration string reaches them); whether the `SPI_*` groups resolve against a shipped
`sentences.txt` at all. **Not built:** the port has no sentence system and no `SetExpression`, so
the Tzimisce rows are carried as data and their emit refuses.

## Speech and the looping-sound stop — `0x10278e30`, `0x10279000`, `0x1027caa0`, `0x100e0a40`, `0x102d3ec0`

`CAI_BaseNPC::PlaySentence` (`0x10278e30`, 353 bytes) is slot 484 and the one entry every spoken
line goes through. Four arms, read off the listing because the decompiler mis-groups the two nested
calls at the end: a null name returns `-1` (`10278e3f`); a false `IsAlive()` — slot 158, vtable
`+0x278` — returns `-1` (`10278e49`); a leading `!` names a **raw sentence**, so
`SENTENCEG_Lookup(name)` and `EmitSentenceByIndex(CPASAttenuationFilter, entindex(), CHAN_VOICE,
index, volume, soundlevel, 0, pitch 100, NULL, NULL, true, 0)`, returning the looked-up index
(`10278e57`); anything else is a sentence **group**, `SENTENCEG_PlayRndSz(edict(), name, volume,
soundlevel, 0, 100)` returned unchanged (`10278f5f`). Its `delay` and `pListener` parameters are
**read by nothing** — the listing never touches `[ESP+0x4c]` or `[ESP+0x58]`.

One retail quirk worth keeping: the attenuation the raw-sentence arm hands its filter is Source's
`SNDLVL_TO_ATTN` done in **integer** arithmetic — `MOV EAX,0x14 / IDIV (soundlevel - 0x32) / FILD`
at `10278ea8`–`10278eb7` — not the SDK's float divide, so level 51 gives 20 and level 71 gives 1
with everything between truncated. It feeds only the PAS cull.

`CAI_BaseNPC::PlayScriptedSentence` (`0x10279000`, 33 bytes) is slot 485 and its whole body is the
forward: it tail-calls slot 484 (`+0x790`) with `(name, delay, volume, soundlevel, NULL)`, dropping
its own `bool` and `CBaseEntity*` arguments and **hardcoding a null listener** rather than passing
the one it was handed.

`CAI_BaseNPC::StopLoopingSounds` (`0x1027caa0`, 44 bytes) is slot 511 and one call:
`IEngineSound::vfunc5(engine->IndexOfEdict(this->edict() /*+0x2e0*/), 1)`. **Unrecovered:** what the
literal `1` is — this call site does not pin the slot-5 signature, and the only other uses of the
interface in the family are slot 3 `EmitSound` and slot 4 `EmitSentenceByIndex`.

`CDialog::PlayWhisper` (`0x100e0a40`, 157 bytes) is the conversation object's one-shot whisper
drain, not an NPC virtual. `+0x33ad` is the **queued whisper text** — a char buffer written by
`CDialog::Acquire` (`0x100e05f0`), `FUN_100e09a0` and `CDialog::load` (`0x100e5410`) as the running
conversation reaches a line that carries one. If it is non-empty the body resolves the
conversation's bound player (`+0x4`, an `EHANDLE`) and hands it `(text, pszSoundName)` through
`0x10182c40`, then clears the buffer. The clear is **unconditional** once the text was non-empty: a
whisper queued while the player handle is dead is consumed and lost. `0x10182c40` copies the text
into the HUD's global whisper buffer, points the player's `+0x1ca4` at it, sets the display deadline
`+0x1ca8` to `curtime - k` plus either the named sound's duration (`IEngineSound` slot 12, only when
the player has a live dialogue partner and the sound name is non-empty) or a fixed fallback, and
raises bit 0 of `+0x1cac`. **Unrecovered:** the two float constants `_DAT_10449270` (= **0.5**, float64; read 2026-09-21, `rdata-cells.md`) and
`_DAT_10471720` (= **0.6**, float64; read 2026-09-21, `rdata-cells.md`), and therefore how long a whisper with no sound stays up.

`CAI_Hint::vfunc130` (`0x102d3ec0`, 108 bytes) is the hint NODE's reaction to an AI sound, not the
NPC's. After the `CAISound` base handler (`0x100aa5a0`) it resolves the hint's owning node
(`0x102d3e60`); with no node it logs retail's `"AI hint has incorrect origin"` warning and returns,
and with one it emits a sound at the node's stored position through vtable `+0x2d4` and **claims the
node for this hint** by writing `this` into the node's `CAI_Hint*` slot at `+0xa0`. **Not built:**
this runtime has no `CAI_Hint` entity at all — hints are node indices on
`FElysiumNpcScheduleHost::HintNode` — so the body is recovered and not ported; standing a hint
entity is a different story's, and 29c's `FElysiumNpcHint::ClaimAndPlaySound` target was its own
recorded guess.

## The facing-target queue — `0x10278cb0`, `0x10278d20`, `0x10278d90`, `0x102e11f0`, `0x10278e00`, `0x10278c80`

_Recovered 2026-09-13, story 29c-1._

Four of `CAI_BaseNPC`'s facing virtuals are a gate plus a forward into `CAI_Motor` (`m_pMotor`,
`+0x5d44`), and the gate is the same on all of them: a global `ConVar` pointer at `0x10924f74`, read
inline as `!vtable[1]() && m_nValue != 0` — `ConVar::GetBool()` with the compiler's own inlining of
`IsCommand()` and `m_nValue` (`+0x2c`). When it answers false the body returns and **adds nothing**.
The three `AddFacingTarget` overloads (`0x10278cb0` slot 519 to motor slot 14 `0x102e20f0`,
`0x10278d20` slot 518 to motor slot 13 `0x102e2120`, `0x10278d90` slot 517 to motor slot 12
`0x102e2150`) are 71 bytes each and identical past the gate: a tail jump with `this` swapped for the
motor and every argument untouched. The four stack writes after the `POP ESI` in the listing are the
frame fix-up, not an argument shift. Each motor overload is a 32-byte forward into the motor's own
queue at `motor+0x54`.

`GetFacingDirection` (`0x10278e00`, slot 520) is 19 bytes and has no gate at all: it forwards its
out-vector straight to `CAI_Motor` slot 15 (`+0x3c`). `FacingIdeal` (`0x10278c80`) reads
`CAI_Motor::DeltaIdealYaw` (`0x102e1f90`), takes its absolute value and compares it against the
**double** at `0x10499568` — `0.006` — and the listing's `TEST AH,0x41` / `JP` makes that compare
`<=`, not the strict `<` the decompiler prints. `DeltaIdealYaw` itself is
`UTIL_AngleDiff(m_IdealYaw (motor+0x34), UTIL_AngleMod(GetAngles().y))`, with an exact-equality
short circuit returning `0.0`, and `UTIL_AngleMod` the usual 16-bit quantisation
`0.0054931640625f * (int(a * 65536/360) & 65535)` (`_DAT_1044ffdc`).

`CAI_Motor` slot 7 (`0x102e11f0`, 95 bytes) cancels the queue's current entry: `SetIdealYaw(-1)`
(`0x102e1e20`), then `0x102e26b0` fills three floats and the second of them is compared against
`_DAT_104454c4 = 0.0f`; above it the motor dispatches `0x2d` through its outer NPC's vtable `+0x4d8`
(slot 342) and at or below it `0x2e`, and `motor+0x30` is zeroed either way.

**Unrecovered:** the cvar's NAME and DEFAULT. Its pointer lives in uninitialised `.data` (an RVA past
`.data`'s raw size), no corpus function constructs it, and its eight readers —
`CAI_BaseNPCTroika::NPCThink` twice, the three `AddFacingTarget` overloads,
`CNPC_VHuman::NPC_EarlyTranslateActivity`, `CNPC_VAndreiBlood`'s thunk to it and `FUN_102b93c0` —
are all the evidence there is. Also unrecovered: what slot 342's `0x2d` / `0x2e` arguments mean.

## The turn-activity ladder — `0x10289d10` and `0x10297640`

_Recovered 2026-09-13, story 29c-1._

Slot 572, twice: `CAI_BaseNPC::SetTurnActivity` (`0x10289d10`, 359 bytes) on the base line and
`CAI_BaseNPCTroika`'s override (`0x10297640`, 593 bytes) on the line every spawnable `npc_*` is on.
Both read the motor's yaw delta once and walk a ladder of bands, asking
`SelectWeightedSequence(act)` at each rung and taking the first the body authors; a rung whose clip
is missing is skipped and the walk continues. The tail of both is `SetIdealActivity(ACT_IDLE)`.
Sign is retail's: a negative delta turns RIGHT.

The base ladder, with the `.rdata` constants: `[-100, -80)` gives `ACT_90_RIGHT` `0xa2`
(`_DAT_1049a194`, `_DAT_1049a198`); `[80, 100)` gives `ACT_90_LEFT` `0xa1` (`_DAT_104454c8`,
`_DAT_10450564`); `|d| >= 160` gives `ACT_180_LEFT` `0x9d` (`_DAT_1049a188`, a **double**);
`d < -45` gives `ACT_TURN_RIGHT` `0x3c` (`_DAT_1049a180`); `d >= 45` gives `ACT_TURN_LEFT` `0x3b`
(`_DAT_1049949c`, the same word `CAI_BaseNPC::MaxYawSpeed` returns). The first three picks tag
`m_afMemory |= 0x2000` (`+0x5d8c`); the last two do not.

The Troika ladder is wider and differs in three ways. Its whole body is gated on
`cvar(0x109247ec) || m_bAllowTurningAnims (+0x65f9)`, and with the gate closed it still falls
through to the `ACT_IDLE` tail. Its bands are `[-140, -70)` for `0xa2` (`_DAT_1049ae3c`,
`_DAT_1049ae40`) and `[70, 140)` for `0xa1` (`_DAT_104528d4`, `_DAT_1049ae38`), and the 180 rungs are
**ordered pairs**: past `-140` it asks `ACT_180_RIGHT` `0x9e` first and falls back to `0x9d`, past
`+140` it asks `0x9d` first and falls back to `0x9e`. Its loose pair is `-15` / `+15`
(`_DAT_1049ae34`, `_DAT_10463584`) rather than `-45` / `+45`, and **every** pick tags `0x2000`,
including the loose ones. Exactly plus or minus 140 reaches neither 90 band nor either 180 rung and
lands on a loose turn.

**Unrecovered:** the cvar at `0x109247ec` — name and default, for the same reason as `0x10924f74`
above. It is also read by `CAI_BaseNPCTroika::MaxYawSpeed` (`0x10297ce0`) and `CNPC_VDog`'s
(`0x10374130`), where it selects between a tuning record's yaw speed and a literal fallback.

## The head and eye direction readers — `0x1026b210`, `0x1026b240`, `0x1025e7b0`, `0x1025f040`, `0x1025f0b0`, `0x1025f0f0`, `0x1025f160`

_Recovered 2026-09-13, story 29c-1._

Slots 368–373 are one family of six accessors and most of the fills are forwards. On the base line
`EyeDirection2D` (`0x1026b210`, slot 372) and `EyeDirection3D` (`0x1026b240`, slot 373) are 20-byte
tail jumps through the SAME object's vtable at `+0x5c8` and `+0x5cc` — slots 370 and 371, the head
readers. The base tier therefore has no independent eye aim: an eye direction IS a head direction.

Eight classes go one step further and answer their head aim with their BODY direction, forwarding
370 to 368 and 371 to 369: `CCineNPC`/`CCineAI`/`CCineAISchedule` (`0x101a6d40`, `0x101a6d70`),
`CPayphone` (`0x101aa7f0`, `0x101aa820`), `CNPCMaker` (`0x1034adf0`, `0x1034ae20`),
`CNPCMaker_Fleshpile` (`0x1034be90`, `0x1034bec0`), `CNPCMaker_Zombie` (`0x1034cad0`, `0x1034cb00`)
and `CNPC_VRat` (`0x103ad7f0`, `0x103ad820`). The three `CNPCMaker` listings appear to forward
through `+0x5c0`, which is slot 370 itself; that is a multiple-inheritance thunk onto the adjusted
subobject, and the forward is the whole behaviour.

`CAI_BaseHumanoid` is the one class in the family that keeps a real implementation, and it is a
cache. `0x1025e7b0` refreshes it behind two independent latch bits in `+0x5f4c`. Bit `0x2` covers
the head ORIGIN and DIRECTION: `GetAttachment("head", &origin, &ang)` (`0x105c8ed8` is the name);
on success the direction is `AngleVectors(ang)`, and on FAILURE the origin falls back to
`EyePosition()` (`0x100b4b40`) and the angles to `GetAngles()` (vtable `+0x374`) — so a model with
no head attachment answers its body's forward. Bit `0x1` covers the eye direction: `EyePosition()`
(vtable `+0x458`) minus the cached head origin, normalized. On the no-attachment arm those two
points coincide and the eye direction is the zero vector, which retail normalizes anyway.
`0x1025f160` (slot 371) and `0x1025f0b0` (slot 373) call the refresh and return the cached vectors
at `+0x5f68` and `+0x5f5c`; `0x1025f0f0` (slot 370) and `0x1025f040` (slot 372) call their own 3-D
twin through the vtable, zero Z and renormalize — the 2-D form is always the 3-D one flattened.

**Unrecovered:** nothing in the chain itself. The head attachment's own transform is the animating
tier's and is not a fact about the AI.

## `SetHeadDirection` — `0x1026af70` and `0x1025eaf0`

_Recovered 2026-09-13, story 29c-1._

Slot 537 has two very different bodies. Both open with `CapabilitiesGet() & 0x1000`
(`bits_CAP_TURN_HEAD`, vtable `+0x804`) and return at once without it.

`CAI_BaseNPC`'s (`0x1026af70`, 517 bytes, read from the listing because the decompiler lost the
pitch half's register aliasing) integrates two angles through a fixed-step filter. The yaw target is
`UTIL_AngleDiff(VecToYaw(lookTarget - GetOrigin()), GetAngles().y)`, where `VecToYaw` comes through
`0x1027db80`, which answers the body's current angles for a zero delta rather than 0 — so a
degenerate delta lands on a yaw difference of exactly zero. The pitch target is
`-RAD2DEG(atan(dz / |lookTarget - EyePosition()|))`, and the divisor is the FULL 3-D distance, not
the flat one (`_DAT_10446758 = 57.29578f`). Each angle is then integrated by
`v = v * 0.8f + target * 0.2f` (`_DAT_1047049c`, `_DAT_1049954c`) once per `0.1 s`
(`_DAT_104493d0`, a **double**) of the passed interval, in a DO/WHILE — an interval at or under one
step still integrates once, and an interval of zero skips the loop entirely. Both angles then take a
single one-sided guard, `> 360` becomes `0` (`_DAT_10450568`): a filter that ran away NEGATIVE is
left alone. Finally `SetBoneController(0, m_flHeadYaw)` and `(1, m_flHeadPitch)` (`0x10095cb0`), and
the yaw's return value is assigned back to `m_flHeadYaw` (`+0x0e5c`) while the pitch's is discarded.
`Studio_SetController` returns its argument unchanged when the model declares no controller at that
index, and no shipped VtMB model declares one, so the assignment is the identity and neither angle
reaches a skeleton.

`CAI_BaseHumanoid`'s (`0x1025eaf0`, 972 bytes) is a different mechanism: pose parameters, not bone
controllers. Past the same gate it takes the "head" attachment, builds head yaw, head pitch and
chest yaw as `AngleDiff(...) + GetPoseParameter(idx)` and writes each back through vtable `+0x564`
(slot 345) onto the indices stored at `+0x5fe0`, `+0x5fe4` and `+0x5fe8`; then it copies ten more
pose parameters from the source indices at `+0x5fec`..`+0x6010` onto the destination indices at
`+0x5fb8`..`+0x5fdc`, calls `FlushBoneCache()` and clears bits `0x3` of `+0x5f4c` — the two cache
latches `0x1025e7b0` sets — so the next direction read re-derives.

**Unrecovered:** which pose parameters the twenty indices at `+0x5fb8`..`+0x6010` name. They are
`CAI_BaseHumanoid`'s own words, resolved at model load, and the corpus holds no writer for them
inside layers 0–9.

## `SetAim` and `SetViewtarget` — `0x1026b480`, `0x100b5b00`

_Recovered 2026-09-13, story 29c-1._

`SetViewtarget` (`0x100b5b00`, slot 277) is three floats into `m_viewtarget` (`+0x0848`) and
nothing else; retail networks that word to the client and it is the one gaze value that leaves the
server.

`SetAim` (`0x1026b480`, slot 539, 79 bytes) converts the aim vector to angles through
`VectorAngles` (`0x10139970`) and writes exactly two pose parameters by name through vtable `+0x564`
(slot 345): `"aim_pitch"` gets `angles[0]`, and `"aim_yaw"` gets a **hard zero** — the listing
pushes an integer `0`, not the computed yaw. Retail aims the pitch pose and pins the yaw pose at
neutral, and the aim's own yaw is discarded.

**Unrecovered:** nothing.

## `CAI_BaseHumanoid`'s look-target list — `0x1025f760`, `0x1025f8e0`

_Recovered 2026-09-13, story 29c-1._

Slots 535 and 536 on the Troika line are `return;` (`0x101a6be0`, `0x101a6bc0`), so nothing a
spawned `npc_*` dispatches through reaches a list at all. `CAI_BaseHumanoid` fills both with a real
one: a `CUtlVector` whose base pointer is `+0x5f88` and whose count is `+0x5f94`, holding 0x24-byte
records laid out as kind (`+0x00`, 0 for an entity and 1 for a position), `EHANDLE` (`+0x04`),
position (`+0x08`), start stamp (`+0x14`, `gpGlobals->curtime`), end stamp (`+0x18`,
`curtime + duration`), rate (`+0x1c`) and priority (`+0x20`).

Both overloads do the same two things. First they REMOVE any record that already holds this target —
`0x1025f760` resolves each stored handle and compares the resulting POINTER, so a stale handle never
matches, and `0x1025f8e0` compares the stored position component by component with exact float
equality — shifting the tail down one record and decrementing the count. Retail breaks on the first
match, so a duplicate that somehow got in would survive. Then they grow by one and fill the new TAIL
record, which means re-adding an existing target moves it to the end of the list. The rate is
`influence / duration`, divided unguarded.

**Unrecovered:** what reads the list. No body inside layers 0–9 consumes it.

## `OverrideMoveFacing` — `0x1027d9f0`

_Recovered 2026-09-13, story 29c-1._

Slot 526's whole body is retail's `g_ScopeTraceStack` push/pop around `return false;`. The
bookkeeping is retail's own debug stack — the same push/pop that opens `PlayerIsFacingMe`,
`UpdateFacingTimer`, `FacePlayerAdvance` and `CNPC_VWerewolf::OnChangeActivity` — and Unreal's
stat/trace system replaces it. The ANSWER is the rule: the base declines to take over a move's
facing, unconditionally, and that is what every species override of slot 526 is measured against.

**Unrecovered:** nothing.

## Slot 465's species overrides — `0x10357b30`, `0x103947b0`, `0x103a56f0`, `0x103d5f60`

_Recovered 2026-09-13, story 29c-1._

`CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`) is `return;`, so the shared algorithm is the
chain alone and every override below ends by calling it.

`CNPC_Crow` (`0x10357b30`, 40 bytes) is one activity and one write: when the activity is `0x22`,
`m_flCycle` takes `RandomFloat(0.0f, 0.75f)` (`0x3f400000` in the listing, `DAT_1070b244` is
`VEngineRandom001`). A crow entering that activity starts its clip at a random phase, so a flock
does not beat in step.

`CNPC_VWerewolf` (`0x103d5f60`, 126 bytes) is the scope-trace push/pop and an unconditional forward.
There is no species behaviour in it at all.

`CNPC_VSabbatGunman` (`0x103a56f0`, 160 bytes) reads three species convars — `DAT_1093c104` a
ground-speed threshold, `DAT_1093c14c` a motion-trail id, `DAT_1093c1f4` a playback scalar — through
the same inlined `ConVar` shape as `0x10924f74`, where an unreadable convar yields `0`. At or BELOW
the threshold it clears `m_nMotionTrail` (`+0x1484`) and sets the playback scalar to the literal
`-1.0f`; above it, it takes the trail id and the scalar from the other two convars. Either way it
calls `SetPlaybackAndSpeedScalar` and chains.

`CNPC_VMingXiao` (`0x103947b0`, 303 bytes) picks a playback scalar from a tuning record
(`0x101e8da0(0x10739d08)`) in two arms of three rows, selected by `0x10398870` — the gate
`docs/vtmb/animation_and_movers.md` names "+0x6674". On that arm the scalar is a flat field:
`+0x1c` for `ACT_WALK` (9) or `ACT_RUN` (0x13), `+0x18` for activity `0x4b`, `+0x14` otherwise; the
`0x4b` row writes its scalar and leaves at once, skipping the second write. Off it the scalar is
`per * (6 - m_iConnectedTentacleCount (+0x670c)) + base` with the pairs `(+0x5c, +0x60)`,
`(+0x54, +0x58)` and `(+0x4c, +0x50)` for the same three rows, floored at `_DAT_104493d0 = 0.1`.
The tail is `0x1039ab30` for activity `0x1132` or for a walk/run pick and `0x1039aca0` otherwise,
then the chain.

**Unrecovered:** the three Sabbat-gunman convars' names and defaults, and what `0x10398870`,
`0x1039ab30` and `0x1039aca0` do. The tuning record `0x10739d08`'s field names are also unsettled;
only the offsets each arm reads are recovered here.

## The four player-relative facing bodies — `0x103aaf50`, `0x1036d600`, `0x1036dc60`, `0x1035e5f0`

_Recovered 2026-09-13, story 29c-1._

All four read `m_hClosestPlayer` (`+0x628c`) first and refuse outright when it does not resolve.

`CNPC_VSabbatLeader::PlayerIsFacingMe` (`0x103aaf50`, 349 bytes; the decompiler drops the in-place
normalize, so this is off the listing) takes the player's full pitch-and-yaw forward through
`AngleVectors`, forms `delta = GetAbsOrigin() - player->GetAbsOrigin()`, normalizes it in place
keeping the length, and returns **false** only when `len > 0.0001f` (`_DAT_104c3ce4`) AND
`dot(forward, delta) < 0.34202f` (`_DAT_104c3cf0`, which is the cosine of 70 degrees). That is a
140-degree cone, and the length test comes first: a player standing ON the leader counts as facing
him.

`CNPC_VChangBros::UpdateFacingTimer` (`0x1036d600`, 392 bytes) is three nested gates over the same
delta, and passing ALL of them LEAVES `m_fFacingTime` (`+0x66d0`) alone — anything else resets it to
`gpGlobals->curtime`. The gates are `|delta.z| < 50.0` (`_DAT_104ada10`), then a flat
`(delta.x, delta.y, 0)` whose normalized LENGTH must lie in `(1e-05, 150.0)`
(`_DAT_104ad9fc`, `_DAT_104ada14`), then `|AngleDiff(player->GetAbsAngles().y,
VectorAngles(flat).y)| < 70.0` (`_DAT_104ada18`). The yaw term compares the PLAYER's yaw against the
yaw of the brother-minus-player delta, so it asks whether the player is pointed at the brother and
not the other way round. The timer therefore measures how long the player has stood close, level and
looking this way.

`CNPC_VChangBros::GetFacingTimeToTeleport` (`0x1036dc60`, 138 bytes) is the clock that reads:
`21.0f` (`_DAT_104ada0c`) when `m_iSquadDisconnected (+0x5bb0) < 1` and `m_pSquad (+0x5da4)` is
non-null and its member count (`0x103160a0`) is greater than 1, else `7.0f` (`_DAT_104ada08`). A
Chang brother waits three times as long before teleporting while the other one is still standing.

`CNPC_VAndreiBlood::FacePlayerAdvance` (`0x1035e5f0`, 165 bytes) is the gate plus one motor command:
`m_pMotor->0x102e20b0(player->GetAbsOrigin(), 10.0f)`, with `DAT_104a6f80 = 10.0f` the fixed turn
rate. That is the whole body.

**Unrecovered:** what `0x102e20b0`'s second argument is in the motor's own terms — it is a rate, but
`CAI_Motor`'s facing interface is not otherwise walked here. The squad member count `0x103160a0` is
a squad-object call with no counterpart in this substrate.

## The hint node's own words — `CAI_Hint`'s datamap, and `OnRestore` `0x102d3ec0`

_Recovered 2026-09-13, story 29c-1._

`CAI_Hint` is an ENTITY, not a graph node. Every NPC word that names one — `m_pHintNode` (`+0x5ddc`),
`m_pShootAtHint` (`+0x6444`), `CNPC_VWerewolf::m_pMoveHint` (`+0x66bc`) and `m_pTeleportHint`
(`+0x66b0`) — is a `CAI_Hint*` onto the global list `DAT_10925450`, linked through `+0x5d8` and
searched from a rotating cursor `DAT_10925454`. Its own datamap ends with the words every hint rule
in the family reads: `m_strActivity +0x450`, `m_flTargetAngleRange +0x454` (key `target_angle_range`),
`m_flTargetAngleRangeDot +0x458`, `m_flTargetDistMin +0x45c`, `m_flTargetDistMax +0x460`,
`m_flHintRating +0x464` (key `hint_rating`), `m_strTargetName +0x468` (key `target_name`),
`m_iIPPercent +0x46c`, `m_iGroupID +0x470` (key `group_id`), `m_iszUserData +0x5d4`,
`m_nHintType +0x5dc` (key `HintType`), `m_hHintOwner +0x5e0`, `m_nNodeID +0x5e4`,
`m_iDisabled +0x5e8` (key `StartHintDisabled`), `m_flNextUseTime +0x5ec`, `m_strGroup +0x5f0`
(key `Group`) and the output `m_OnNPCArrival +0x5f4`.

Two tiny bodies are the whole claim protocol. `0x102d1420` is the RELEASE: `m_hHintOwner = -1` and
`m_flNextUseTime = delay + curtime`, nothing else. `0x102d14c0` is the "unusable" predicate, three
arms in order — `m_iDisabled != 0`, `curtime < m_flNextUseTime`, or a live `m_hHintOwner`. Every
hint search (`0x102d24b0`) and every hint rule that admits a node runs the second one first.

`CAI_Hint::vfunc130` (`0x102d3ec0`, 108 bytes) is **`OnRestore`**, not a sound handler: `vtmb_slot
130` shows `CAI_BaseNPC::OnRestore` (`0x1027bf50`) and `CAI_BaseNPCTroika::OnRestore` (`0x102998c0`)
filling that slot across the family, and the `CAISound::FUN_100aa5a0` this body opens with is
`CBaseEntity::OnRestore`, whose whole body tail-calls slot 6. This supersedes the reading in
the section "Speech and the looping-sound stop" above, which took it for a reaction to an AI sound.
After the base call it resolves its own AI-network node — `0x102d3e60` bounds-checks `m_nNodeID`
against `(*DAT_1093407c)` and indexes `DAT_1093407c[1]`, bumping an error counter for an
out-of-range id — and with no node logs `"Warning: AI hint has incorrect origin"` and returns. With
one it snaps itself to the node's stored origin (`node+0x08..+0x10`) through
`Teleport(&origin, NULL, NULL)`, vtable `+0x2d4`, and then writes `this` into the node's `CAI_Hint*`
back-pointer at `+0xa0`. So the hint-to-node binding is re-established on every load rather than
saved.

`CAI_Hint::vfunc5` (`0x102d2f00`, 30 bytes) is the destructor: it unlinks from the list, decrements
the count, and — when `this[0x178]` names an owning NPC — calls
`CAI_BaseNPCTroika::ClearHintNode(0.0)` on it so the owner's reference does not dangle.

**Unrecovered:** what `m_iszUserData` is read by; the AI-network node layout past the origin at
`+0x08`; and which `CAI_Hint` slot carries the outputs' activator convention.

### The live hint, stood (0018 story 2, 2026-09-19)

The datamap replay (`CAI_Hint 0x106099f0`, base `CBaseEntity`) names eleven keyfields, all
`SAVE|KEY` and none `INPUT`, so every one is read only to Python: `target_angle_range`,
`target_dist_min`, `target_dist_max`, `hint_rating` (float); `target_name`, `UserData`, `Group`
(string); `ip_percent`, `group_id`, `HintType`, `StartHintDisabled` (int). Its five input
handlers and their writes:

| Input | Body | Effect |
|---|---|---|
| `EnableHint` | `0x102d09f0` | `CBaseEntity::ScriptUnhide` (by address), `m_iDisabled := 0` |
| `DisableHint` | `0x102d0a20` | `CBaseEntity::ScriptHide` (by address), `m_iDisabled := 1` |
| `Walk` / `DontWalk` | `0x102d0a50` / `0x102d0a80` | `FUN_102d3e60` resolves the network node; `FUN_102f97c0(node, 1/0)`; a null node returns |
| `SetUserData` | `0x102d4060` | a string variant (type 2) replaces `m_iszUserData`; any other type clears it |

The class overrides three base slots: slot 77 `CAI_Hint::ScriptHide` (`0x102d0860`) and slot 78
`ScriptUnhide` (`0x102d0890`) add `m_iDisabled := 1 / 0` to the base bodies, and slot 119 `Kill`
(`0x102d08c0`) jumps to slot 77 — a hint's `Kill` hides and disables it. Every input still passes
`CBaseEntity::AcceptInput`'s one entry gate (`FUN_100abc90`): a hidden entity answers true, doing
nothing, to any input whose name is not a case-insensitive prefix of `ScriptUnhide`
(`Q_strnicmp(input, "ScriptUnhide", strlen(input))`), so a hidden hint swallows `EnableHint`.

The constructor (`0x102d2e30`) sets `m_hHintOwner = -1`, leaves every other word zero, and
**prepends** the hint to `DAT_10925450`. The map parse (`0x10136650`) spawns every unparented row
immediately in BSP order (parented rows are deferred and sorted), and no hint row in the 108
exported maps has a `parentname`, so the list runs in **reverse BSP order**. That order is
observable: the patrol lookup `0x102d2840` takes the first type-10000/800 hint whose `Group`
matches byte for byte, and `sp_soc_3` authors `d1..d4` twice (rows 355–358 and 362–365), so its
patrol tokens resolve to the later rows.

**The port** (`Substrate/ElysiumHint.{h,cpp}`) stands `ai_hint` with these inputs and overrides,
applies the entry gate to the hint's own inputs (the gate is not global in this substrate yet — a
named seam), and keeps the list on `FElysiumEntityWorld::HintList`. A hint reference is the hint's
entity index: `FHintWords::HintIndex`, which the Werewolf's rows and end-entity walks compare where
retail compares the `CAI_Hint*`. `m_nNodeID` stays -1 until 0018 story 4.

**Caller audit of the live `HintWords`.** Every reader gets its hint index from one of these
sources, so turning the words on made only the Werewolf's list walks answer:

| Source of the index | Readers | Answers now? |
|---|---|---|
| The searches `0x102d1af0`, `0x102d24b0`, `0x102d2980`, `NavAllHintNodes`, `ClaimHintNode`, `NthHintOfType` — still seams (story 4) | `ScheduleHost.HintNode` / `ShootAtHintNode` readers: `IsHintUnusable`, the validators `0x10295ed0` / `0x102961a0` / `0x10296c40`, slot 566, `SelectScheduleForHint`, the cover validators `0x10297430` / `0x102974f0`, `PlayHintIdleActivity`, `TranslateSchedule` case 0x77, `ClosureHintTypeOf`, `SelectCoverOrKickSchedule`, `TeleportIn`, `CacheFloorHeights`, `EyeOffset`, `GatherHintNodes` | No: no index reaches them |
| `SetMoveHint` / `SetTeleportHint` (`0x103d44e0` / `0x103d45c0`) | the Werewolf endpoint readers | No: their selectors are not wired |
| `GlobalHintList()` | `InitializeHintData` (`0x103d7710`), `GetForwardHintForHint` (`0x103d7090`) | Yes — retail's own walks, Werewolf only (`sp_observatory_2`) |
| `FindHintByName` (name lookup + `CAI_Hint` cast) | `FindHintEndEntity` (`0x103d6520`) | Yes — retail's |

`InitializeHintData` also writes each hint's angles (`103d7888`, slot `0x104`) and warns when a
hint's `target_name` does not name exactly one entity (`103d7a70`); both are the Werewolf program's
and are not ported.

## The hint list and its four searches — `0x102d1af0`, `0x102d24b0`, `0x102d2980`, `0x102d1760` (2026-09-19, 0018 story 8)

_Recovered 2026-09-19 by two independent opencode walks and an adjudication pass. `0x102d1af0`,
`0x102d24b0`, the unusable test, the claim and the release were re-read from the listing here;
`0x102d2980` and `0x102d1760` stand on the two walks agreeing row for row. The port still carries
these as seams (the caller audit above); this is the retail contract story 4 stands them against._

**The list.** `DAT_10925450` is the head of a singly linked, NULL-terminated list through
`hint+0x5d8`; `DAT_10925454` is a rotating search cursor; `DAT_10925458` counts live hints. The
constructor `0x102d2e30` inserts at the HEAD — so list order is the reverse of spawn order, which is
what "first match in hint-list order" means for the patrol lookup — and sets `m_hHintOwner`
(`+0x5e0`) invalid and `m_flNextUseTime` (`+0x5ec`) 0. The factory `0x102d2f30` zeroes the cursor
and increments the count on every hint it makes, and is the ONLY writer of `m_nNodeID` (`+0x5e4`,
`SAVE` only, not a keyfield): `102d2fce MOV [ESI+0x5e4],ECX` stores its fourth argument, which
`CNodeEnt::Spawn` passes as the running node counter `DAT_10926a3c` (`102d79cc`) or `-1` for the
standalone classes (`102d7bbc`). The destructor `0x102d3040` is the only unlinker; a cursor sitting
on the removed node is zeroed, not advanced. It also notifies a live owner: the NPC at
`owner+0x98` gets condition `0x29` and `ClearHintNode(0.0)`. **`Kill` never reaches it**: slot 119
`0x102d08c0` tail-jumps to slot 77, which on a hint is `0x102d0860` — `CBaseEntity::ScriptHide`
(`0x100a8710`) then `m_iDisabled = 1`. A killed hint stays on the list, disabled.

**Unusable** (`0x102d14c0`), in order: `m_iDisabled` (`+0x5e8`) non-zero; `curtime <
m_flNextUseTime` (strict — `FLD curtime` / `FCOMP [+0x5ec]` / `TEST AH,5` / `JP`, so an equal time
is usable); `m_hHintOwner` resolving to a live entity. Nothing else — no node test, no separate
hidden word.

**The three cursor searches** share one walk. Arguments: `0x102d1af0(npc, type, flags, radius,
origin*, outScore*)`; `0x102d24b0(npc, anchor, type, flags, radius)`; `0x102d2980(npc, flags, mask,
radius, origin*, outScore*)`. An empty list answers NULL and leaves the cursor alone. The walk
starts at `cursor->next`, or at the head when the cursor or its next is NULL. `0x102d1af0` wraps to
the head unconditionally and stops when it returns to its START node, so every node is visited once,
the cursor node last. `0x102d24b0` and `0x102d2980` wrap only while the cursor is non-NULL and stop
on reaching the CURSOR, so the cursor node itself is never examined; with a NULL cursor they walk
head to tail once. The cursor is written on exit only: the returned node, or NULL on a miss.

Admission, in evaluation order:

1. not unusable (`0x102d14c0`);
2. `0x102d1af0` / `0x102d24b0`: `type == 0` (any) or `m_nHintType` (`+0x5dc`) equal to it.
   `0x102d2980` has no type argument: it requires `(mask & hint+0x474) != 0`, the class word `Spawn`
   (`0x102d0b60`) derives from the type — 1 for 100, 101 and `0x27d8`; 4 for `0x283c`; 8 for
   `0x283d`; `0x10` for `0x28a0`; 0 otherwise — so a zero mask admits nothing;
3. 3-D squared distance STRICTLY under `radius²` (NaN rejects), measured from `origin` (the NPC's
   `GetAbsOrigin` when NULL), or in `0x102d24b0` from the ANCHOR's origin;
4. slot 566 `FValidateHintType(hint)` on the NPC. The base body `0x1026a8d0` answers 0, so only the
   Troika line ever finds a hint; `0x10295c20` opens on `hint->m_iGroupID (+0x470) &
   npc->m_iHintGroups (+0x62e4)`. `Spawn` turns an authored `group_id` of 1..32 into `1 << (id-1)`
   and anything else into `0xFFFFFFFF`;
5. scoring (`0x102d1af0`, `0x102d2980`): flags bit 3 (`8`) scores `sqrt(d²) × m_flHintRating`
   (`+0x464`), else bit 1 (`2`) scores `d²`, both from the NPC's own origin; a candidate is dropped
   only when STRICTLY worse than the best so far, so an equal score replaces — the later node in
   walk order wins a tie;
6. flags bit 0 (`1`): a line trace from the eye (slot 193) of the NPC — of the ANCHOR in
   `0x102d24b0` — to the hint's origin plus that same entity's `m_vecViewOffset` (`+0x184`), mask
   `0x2400b`, `CTraceFilterSimple` skipping that entity; admitted only on `fraction == 1.0`.

`0x102d1af0` stops at the first admitted node unless bit 1 or bit 3 is set (`102d1e7c TEST AL,0xa`).
`0x102d24b0` always returns the first admitted node. `0x102d2980` stops at the first admitted node
iff bit 0 is CLEAR — a different bit — and then leaves `*outScore` at `FLT_MAX`; with bit 0 set and
no scoring bit the LAST admitted node wins. `0x102d2940` is `0x102d2980` with mask 1.

**`0x102d1760`, the random pick.** Flags bit 2 (`4`) diverts `0x102d1af0` and `0x102d24b0` — not
`0x102d2980` — to `0x102d1760(npc, type, flags, radius, NULL)`, dropping the anchor, the origin and
the score pointer. It walks head to tail with no cursor and no wrap, applies steps 1–4 and 6,
collects every admitted node, and draws ONE `RandomInt(0, count-1)` — only when `count >= 1`; the
cursor becomes the pick, or NULL.

**The searches write nothing on the NPC.** Callers store the result in `m_pHintNode` (`+0x5ddc`)
and then claim it; a refused claim clears the field again (`102a28f5`, `102b7190`).

**Claim `0x102d1350(hint, npc)`**: refused (AL 0) only when the owner handle resolves to a live
entity that is not the requester; a stale owner, no owner, or the requester itself succeed, writing
`m_hHintOwner = *npc->GetRefEHandle()`. **Release `0x102d1420(hint, delay)`** is two stores:
owner invalid, `m_flNextUseTime = curtime + delay`. `CAI_BaseNPCTroika::ClearHintNode 0x10295ab0`
releases only when `0x102d1450` says this NPC owns the hint, then zeroes `m_pHintNode` and
`m_iFailedCoverLOSChecks` (`+0x6404`), clears bit `0x2000` and resets the saved attack extents.
Delays seen: 5.0 `Event_Killed`, 5.0 on a path failure and 1.0 before a re-search in Troika
`StartTask`, 0.5 for the hint `0x103bfa50` did not pick, 0.0 from `UpdateOnRemove` (which releases
without the owner gate) and from the hint's own destructor.

**Call sites**, all through thunk `0x1000633e` for `0x102d1af0`: base `StartTask` tasks `0x40`
(find) / `0x41` (find and claim) `(type 0, flags = (byte)taskData, 2000.0)`; Troika `StartTask`
`(0x2774, 2, (slot 0x688 radius + 8192.0) × 0.5)`; `CNPC_Crow` `(700, 3, 5000.0)`; `0x10365780`
`(type, flags, 5000.0)`; `0x1038b370` `(20000, 0, 15000.0)` after a `RandomInt(1,3)` stored at
`+0x6674`; `CNPC_VManBat` `StartTask` / `RunTask` `(20000, 0, 5000.0)` with one flags-2 site
(`1038c5c5`). `0x102d24b0`: `0x103bfa50` twice, `(14000 | 14001, 0, 200.0)`. `0x102d2980`:
`0x102b6b50` `(8, 0x10, weapon +0x8c0 or 1024.0)` after a `RandomFloat(2.0, 2.5)`; `0x102b7110`
`(8, its argument, slot 550)`, retried once when `+0x6435` is set. **Corrected 2026-09-19
(review against the listing): a native site DOES set flags bit 2.**
`CNPC_VVampireBoss::SelectHintNode` (`0x103c59d0`, thunk `0x100053a3`) forwards its flags byte
unchanged to `0x102d1af0` at radius 5000, and `CNPC_VSabbatLeader::StartTask` (`0x103a78c0`)
calls it five times — `(0x3e83, 2)`, `(16000, 4)`, `(16000, 2)`, `(0x3e81, 2)`, `(0x3e82, 2)`.
The `(16000, 4)` site reaches the random pick `0x102d1760` and its one `RandomInt` draw with no
authored operand involved; tasks `0x40` / `0x41` remain the authored road to it. The review
also names `0x10367680` and `0x103b2630` as forwarders of caller-supplied flags at radius 5000
(not re-read here).

`DAT_10924a6c` is the ConVar `ent_trace_conditions` — the static object at `0x10924a68` (registrar `0x1028bde0`: name `0x105d7ad0`, default `"1"`, help "When ent_trace is on, this will dump info about conditions also."), whose `+4` word is the pointer every condition setter reads a value through (slot 1) and discards: a debug-trace read, no game state (closed 2026-09-19).

**`owner+0x98` and its five neighbours are cached self-downcasts (closed 2026-09-19).** Troika
gave `CBaseEntity` six pointer fields that the base constructor zeroes and exactly one derived
constructor each fills with `this` (byte scan for `MOV [ESI+off],ESI`: one site per offset), so
"is it an X" is a null test instead of a `dynamic_cast`:

| field | written at | by the constructor of |
|---|---|---|
| `+0x94` | `1027c64b` | `CAI_BaseNPC` (`0x1027c300`) |
| `+0x98` | `1028d3bc` | `CAI_BaseNPCTroika` (`0x1028d230`; its first call is `0x1027c300`) |
| `+0x9c` | `103271db` | `CBaseCombatCharacter` (`0x10326de0`; the first call of both `0x1027c300` and `0x1015d7c0`) |
| `+0xa0` | `10250b78` | `CBaseCombatWeapon` (`0x10250ac0`) |
| `+0xa4` | `100ee347` | `CBaseDoor` (`0x100ee2b0`) |
| `+0xa8` | `1015d998` | `CBasePlayer` (`0x1015d7c0`) |

None is in a datamap; none is saved. Every "`entity+0x9c != 0`" gate in the weapon and LOS
ladders is therefore "the hit entity is a combat character", and `+0x98` is "is a Troika NPC".

**Unrecovered:**
the clamp `Spawn` applies to `m_flHintRating` through the `NPC_Cover_Distance_Scalar` cvar helper
`0x1006caa0`;
`0x102d1fe0`, a fifth cursor writer with its own criteria compare, not walked; the list walkers
`0x102d0910`, `0x102d31c0` and `0x103cb4b0`; whether any shipped schedule authors flags bit 2.

**`m_iHintGroups` — the parser `0x102989e0` (thunk `0x1000b8e3`; closed 2026-09-19).** A NULL or
empty string writes `0xffffffff` — every group. Otherwise the mask starts at 0, the string is
copied to a 256-byte stack buffer with NO length check, and each space-separated token goes
through `atoi`: a value `1..32` sets bit `value − 1`, anything else (0, negatives, above 32,
text) sets nothing, so a non-empty string of junk yields mask 0 and matches no hint. Only the
space `0x20` separates; a tab or comma ends nothing and `atoi` stops at it. Two callers:
`CAI_BaseNPCTroika::Spawn` (`1029922d`, with `m_sHintGroups +0x62e0`, `""` when unset) and
`ProcessTweakParam 0x1029aa10` on the key `HINTGROUPS` (`0x105d94d0`, site `1029ac4c`). Its
near-twin `0x10298910` (thunk `0x10014ff6`) fills `m_iInterestingPlaceGroups +0x62dc` — the one
difference is that a NULL or empty string leaves **0**, no group, where the hint parser writes all
ones (`1029892a JE` straight to the exit) — from the same two places — `Spawn` (`10299216`) and the key `IPGROUPS` (`0x105d94e0`, `1029ac28`).

## `FValidateHintType`'s species half — slot 566's ten decided bodies

_Recovered 2026-09-13, story 29c-1._

Slot 566's Troika-line body (`0x10295c20`) is the group gate plus a type switch: a null hint is
false; `hint->m_iGroupID (+0x470) & this->m_iHintGroups (+0x62e4)` must be non-zero, and when it is
not, the body prints the two bit lists into `"Hint group id %s usable %s"` for the debug NPC only and
answers false; otherwise it switches on `m_nHintType` — `0x27d8` forwards to `0x10297430`, 100 and
101 to `0x102974f0`, `0x2774` answers true outright, `0x283c`/`0x283d` forward to `0x10295ed0`,
`0x28a0` to `0x102961a0`, and everything else is false.

Ten distinct bodies replace it outright across eleven classes, and every one of the ten reads
`m_nHintType` and NOTHING else — the group gate is the base body's alone. `CNPC_Crow` (`0x10358c60`) accepts only 700.
`CNPC_VDog` (`0x10374aa0`) only 12000. `CNPC_VSabbatLeader` (`0x103a9340`) accepts `15999 < t <
0x3e86`, i.e. 16000–16005. `CNPC_VTzimisce` (`0x103ba780`) accepts 14000–14001 and is the ONE body
of the set that null-checks its hint at all; the other four would have dereferenced null.
`CNPC_VWerewolf` (`0x103d7ce0`) accepts 15000–15018 **except** 15007 (`0x3a9f`) — the same type
`GetHintTeleportPriority` ranks highest, so a hint the Werewolf teleports to on priority is one its
own validator rejects. Five more are constants: `CNPC_VAndreiBlood` (`0x1035db00`),
`CNPC_VAsianVampire` (`0x10361470`), `CNPC_VChangBros` (`0x1036c6a0`, inherited by the Blade and
Claw leaves) and `CNPC_VSheriffMan` (`0x103af810`) are `return 1`; `CNPC_VZombie` (`0x103e03b0`) is
`return 0`. Two are neither and belong to a later band: `CNPC_VBach` (`0x10365800`) accepts
17000–17005 and otherwise chains into the base body, and `CNPC_VManBat` (`0x1038e480`) builds an
authored node name from a random draw (`ManBat_Landpoint`, `ManBat_Divepoint %d`,
`ManBat_Divepoint %d Bottom`, `ManBat_Script_Node %d`, `ManBat %d`) and name-matches the hint
against it at type 20000.

Slot 567 `GetHintActivity` has exactly one species override: `CNPC_Crow` (`0x10358c90`) answers
activity `0x22` for hint type 700 and tail-calls the Troika line's `return 1` for anything else.
Slot 568 `GetHintDelay` (`0x1026a910`) is two instructions — `FLD [0x104454c4] / RET 4` — and
`_DAT_104454c4` is the image's shared `0.0f` (1,328 readers, no writer; `0x102961a0` compares a
squared length against it to make a "non-zero length" bool). No class overrides it.

**Unrecovered:** the two debug-only `sprintf` format strings the base body's group-mismatch arm
builds, and what `0x2774` (10100) names.

## The Werewolf's hint surface — move, teleport, break, imperative, activity, position

_Recovered 2026-09-13, story 29c-1._

`CNPC_VWerewolf` carries a private hint pair and one shared flag: `m_pTeleportHint +0x66b0`,
`m_pMoveHint +0x66bc`, `m_bRandomHint +0x66c8`, plus the hint-gate bit word `+0x66e8`, the door state
`+0x6680` and an authored groundpoint array at `+0x6714` with its count at `+0x6720`, stride `0x48`,
the hint pointer at `+0x00` and the point at `+0x08`.

`SetMoveHint` (`0x103d44e0`) clears any existing move hint FIRST — which releases it with a zero
reuse delay and clears condition `0x78` — then writes `m_bRandomHint`, then `m_pMoveHint`, then
raises `0x78` again. `ClearMoveHint` (`0x103d4690`) releases, nulls, and clears `0x78`
**unconditionally**, outside the null test, so it is also the way to drop the condition with no hint
installed. `SetTeleportHint` (`0x103d45c0`) and `ClearTeleportHint` (`0x103d4760`) are the same pair
without any condition traffic — but `SetTeleportHint` clears the SHARED `m_bRandomHint`, so setting a
teleport hint un-randomises a move hint that is still installed.

`IsValidBreakHint` (`0x103d8550`) is null, then `0x102d14c0`, then `m_nHintType == 0x3aa3`.
`GetHintTeleportPriority` (`0x103d3220`) is a chain of equality tests: `0x3a99` → 2, `0x3a9a` → 3,
`0x3a9b` → 3, 15000 → 1, `0x3a9f` → 4, everything else 0. `SelectScheduleForHint` (`0x103ce9b0`)
answers `0x163` for no hint, `0x15f` / `0x160` / `0x164` for types `0x3aa4` / `0x3aa5` / `0x3aaa`, and
otherwise compares the ROOTED distance from `m_vSavePosition` to the current origin against
`m_flGoalTolerance` — `0x15a` when it is greater, `0x15b` otherwise.

`SetHintActivity` (`0x103d6000`) reads a percentage `ConVar` and draws `RandomInt(1, 100)` BEFORE the
switch, on every call, whether or not the type that lands uses it; the roll subtracts one from the
answers for `0x3a9c` (`0x113`), `0x3a9d` (`0x115`) and `0x3a9e` (`0x117`), and a second draw,
`RandomInt(0, 1)`, does the same for `0x3aa9` (`0x125`). The remaining types map flat: 15000 →
`0x10c`, `0x3a99` → `0x119`, `0x3a9a` → `0x11a`, `0x3a9b` → `0x11b`, `0x3a9f` → `0x10d`, `0x3aa0` →
`0x10f`, `0x3aa1` → `0x110`, `0x3aa2` → `0x10e`, `0x3aa3` → `0x111`, `0x3aa6` → `0x122`, `0x3aa7` →
`0x123`, `0x3aa8` → `0x10b`, `0x3aaa` → `0x121`. `0x3aa4` and `0x3aa5` are NOT in the switch and take
the default, which returns false without positioning. A type that does land then runs
`PositionAtHint` and `RestartIdealActivity`.

`PositionAtHint` (`0x103d6280`) takes the hint's groundpoint into the origin (vtable `+0xf8`), the
HINT's own angles into the angles (vtable `+0x368`, fed from the hint's `+0x36c`), and relinks.
`GetHintGroundpoint` (`0x103d6770`) linear-scans the authored array for the matching hint and, on a
miss, prints `"Werewolf did not find the hint %s"` and falls back to `GetGroundpoint` of the hint's
own origin. That fallback can fail: `CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`) traces a hull
1000 units straight down and, when the trace does not hit, answers `vec3_invalid` —
`DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills with `0x7f7fffff` (`FLT_MAX`) — rather
than any position. **Neither `GetHintGroundpoint` nor `PositionAtHint` tests for it**, so a Werewolf
handed a hint with no authored groundpoint over no ground writes `FLT_MAX` into its own origin.
Recovered by family Motor, which owns `GetGroundpoint`.

`IsImperativeTeleportHint` (`0x103d3360`, 1,397 bytes and the largest body in the family) is four
gates over `+0x66e8`, each admitting one or two AUTHORED HINT NAMES at one hint type — retail inlines
`FStrEq`-with-trailing-`*` at every compare, the same matcher `CGlobalEntityList::FindEntityByName`
uses. Bit `0x200` admits `cheater_outside_hint_2` at `0x3aa9` and `shard_hint_3` at 15000. Bit
`0x400` admits `cheater_outside_hint_3` at `0x3aa9`. Bit `0x4` splits on the door state: with
`m_DoorState` 2 or 3 it admits `cheater_outside_hint_1` at `0x3aa9`, and at `0x3aa4` it admits
`jump_to_platform_hint_1` or `_2` **only when the trace through vtable `+0x9a4` comes back clear**;
with any other door state it admits `skylight_2_teleports` at 15000 and — through the one compare
retail left as a call, `0x103d3a40`, whose `this` is the hint — `fulldoor_a_3_breakthrough_f` at
`0x3a99`.

**Unrecovered:** the name and default of the percentage `ConVar` at `DAT_1093f85c` (two referrers,
both `SetHintActivity`); the global at `DAT_10924a6c` whose `vtable[1]` `SetMoveHint` calls between
the hint write and the condition raise; and the producer that fills the `+0x6714` groundpoint array.

## The two hint searches and the cover forwards — `0x10365780`, `0x103bfa50`, `0x10297430`, `0x102974f0`

_Recovered 2026-09-13, story 29c-1._

`0x10365780` is the task-side hint install: `0x102d1af0` within 5000 units, the result written to
`m_pHintNode` (`+0x5ddc`) on BOTH paths, then either `TaskComplete(false)` and true, or the assert
file and line into `+0x1b44` / `+0x1b48` (the `hl2_dll` NPC source path, line `0x48a`) followed by
`TaskFail(4)` and false.

`0x103bfa50` is `CNPC_VTzimisce`'s two-group pick. A null target zeroes `m_pHintNode` and returns 0.
Otherwise it searches for hint type 14000 and for `0x36b1` (14001), each within 200 units of the
target, through `0x102d24b0`. With both found it compares the squared distances from the target
through the vtable `+0x370` position accessor — not `+0x364 GetAbsOrigin` — with NO tie-break, so an
exact tie takes the second; the loser is released with a 0.5 s reuse delay. The winner is installed
and the answer is `0x36ba` (first group) or `0x36bb` (second), MINUS TEN when `0x103bfc20` calls the
winner usable. A double miss leaves `m_pHintNode` alone and returns 0.

`0x10297430` and `0x102974f0` are the same forward into the shared cover validator `0x10296c40`:
resolve `m_hHintCoverObject` (`+0x6448`) and call it with the hint's `m_flTargetAngleRangeDot`
(`+0x458`) and a constant — `0.731` for the first, `1.1` for the second. They differ in exactly one
thing: `0x10297430` checks the handle's validity BEFORE the call and answers false without calling at
all when it fails, while `0x102974f0` hands the null through. The decompiler types `0x102974f0`
`void` because it leaves the callee's `EAX` alone; the base `FValidateHintType` returns that `EAX`
for hint types 100 and 101, so its answer is the validator's.

`CNPC_VChangBros::CheckJumpPathToHintNode` (`0x1036df50`) asks, in order: is `m_hClosestPlayer`
(`+0x628c`) live at all — no, and the jump is blocked; is that player within `_DAT_104ada34` of the
segment from my origin to the hint's with BOTH endpoint Zs forced to zero — yes, and blocked; is the
other brother within the same distance of the same segment — yes, and blocked; is my own sector 4 —
yes, and blocked; is the hint's sector 4 — yes, and blocked; otherwise the jump is allowed.
`CNPC_VAsianVampire::AddHintToStoredJumpPositions` (`0x10361990`) pushes the hint's origin into the
two-entry ring `m_vLastJumpPosition` (`+0x66b8`) at `m_iLastJumpPositionIdx` (`+0x66d0`), advances,
and wraps when the advanced index exceeds 1 — the buffer size 2 is baked into the wrap test.

`CNPC_VVampireBoss::DistToSegment` (`0x103c6b70`) is the clamped 3D point-to-segment distance, and
its DEGENERATE arm is the part worth stating: when `|B-A|^2 < _DAT_104ce8c0` it returns
`_DAT_104454c4`, the shared `0.0f`, and **not** the distance to the collapsed point. Because
`CheckJumpPathToHintNode`'s test is `dist < threshold`, a zero-length segment therefore always reads
as "something is standing on the line". The remaining arms are `t = dot(P-A, B-A) / |B-A|^2` with
`|P-A|` for `t <= 0`, the perpendicular for `0 < t < 1` and `|P-B|` for `t >= 1`; Ghidra spells the
sign test `(t < 0) == (t == 0)`, true only when both are false, so `t == 0` takes the `|P-A|` arm —
the same value the projection arm gives there, so the split is not observable. Story 29c-1's first
pass wrote `|P-A|` for the degenerate arm and was wrong; family Positions found it.

`CNPC_VWerewolf::FindHintEndEntity` (`0x103d6520`) makes two asymmetric hops: the hint's
`m_strTargetName` through `FindEntityByName` and an RTTI cast to `CAI_Hint`, falling back to the hint
itself on a failed cast; then the RESOLVED hint's own target name through the same lookup with NO
cast, so the second hop can hand back an entity that is not a hint at all. A null second hop returns
the first.

`CNPC_VVampireBoss::DistToHintCenterLine2D_2` (`0x103c6570`) builds a line from the hint's origin
with Z forced to 0 and `AngleVectors(hint->GetAbsAngles())`'s forward with Z forced to 0 and
re-normalised, then calls `_3` (`0x103c6680`), which projects the point onto that line WITHOUT
clamping — `t = (P.x-S.x)*D.x + (P.y-S.y)*D.y + D.z*K`, then `sqrt(dx*dx + dy*dy + (t*D.z)^2)`. `K`
is `_DAT_104454c4`, the shared `0.0f`, and the caller has already zeroed `D.z`, which is why a body
carrying a Z term is a 2D distance. Compare `CNPC_VVampireBoss::DistToSegment`, which its jump-path
caller uses and which DOES clamp.

**Unrecovered:** the value of `_DAT_104ada34` (= **100.0f**, float32; read 2026-09-21, `rdata-cells.md`) (two referrers, both `CheckJumpPathToHintNode`); the
width of `_DAT_104ce8c0` (= **9.999999747e-6f**, float32; read 2026-09-21, `rdata-cells.md`), `DistToSegment`'s degenerate floor (its ANSWER is recovered); what vtable
slot `+0x370` (220) returns; and what `CNPC_VChangBros::GetSector`'s sector 4 is.

## `PlayHintIdleActivity` `0x102aaa60` and the five hint-node activity lookups

_Recovered 2026-09-13, story 29c-1._

`0x102aaa60` stamps `m_flLastAttackTime` (`+0x5d9c`) with `curtime` first and on every arm. It then
switches on the current hint node's `m_nHintType`, and each of the three cases is gated on
`0x102b5de0`: type 100 restarts activity `0x1114`, type `0x65` restarts `0x110f`, and type `0x27d8`
restarts `0x1120`, or `0x111f` when `m_bLeaningLeft` (`+0x63fd`) is set. A FAILED gate returns false
and restarts nothing — it does not fall through. No hint node, or a type outside the three, falls to
`HasCondition(99)`, and only the ABSENCE of that condition restarts anything, activity `0x19`.

Five sibling bodies — `0x102a13d0`, `0x102a1420`, `0x102a1470`, `0x102a14c0`, `0x102a1510` — are one
lookup written five times: no hint node answers -1, and otherwise the same three types answer a
per-body triple, with three of the five subtracting one from the `0x27d8` answer when
`m_bLeaningLeft` is set. `0x102a13d0` answers `0x1110` / `0x110c` / `0x111e` and leans;
`0x102a1420` `0x1111` / `0x110d` / `0x1118` and does not; `0x102a1470` `0x1112` / `0x110e` / `0x111a`
and leans; `0x102a14c0` `0x1113` / `0x3f` / `0x111c` and leans; `0x102a1510` repeats `0x102a1420`'s
first two and answers `0x55` for `0x27d8` without leaning. The `0x3f` in the fourth is the one answer
of the fifteen outside the `0x110x`–`0x112x` band.

`0x102781e0` is the `m_strHintGroup` (`+0x5db0`) setter: assign, then dispatch slot 551
`OnChangeHintGroup(old, new)` through vtable `+0x89c` ONLY when the two string_t differ — a POINTER
compare, which interning makes a text compare for every authored path. Slot 551's own Troika-line
body (`0x101a6c40`) is empty.

**Unrecovered:** what `0x102b5de0`'s gate is in schedule terms — its shape is a shoot-target or enemy
LOS trace — and what the fifteen activity ids above name.

## The interesting-place wait and its loop — `0x102a9f40`, `0x102aa210`, `0x1029f780`

_Recovered 2026-09-13, story 29c-1._

`CAI_InterestingPlace` carries `m_sType +0x544`, `m_vecMinBounds/_MaxBounds +0x54c/+0x558`,
`m_fMinStayTime +0x568`, `m_fMaxStayTime +0x56c`, `m_bMatchOrientation +0x570`,
`m_bHolsterWeapon +0x571`, `m_iGroupID +0x574`, `m_iRating +0x578`, `m_bEnabled +0x57c`,
`m_pMarkers +0x580`, `m_iMarkersAllocated +0x584` (key `max_npcs`) and the outputs
`m_OnNPCArrived +0x450` / `m_OnNPCLeft +0x468`.

`0x102a9f40` is "the wait" `TASK_DO_INTEREST_ACTIVITY`, `_LOITER_` and `_INTERACT_` share. In order:
claim the marker (`0x102da7c0`); fire the NPC's own `m_OnInterestingPlaceArrived` (`+0x5f74`) with
the PLACE as activator and this NPC as caller; set `m_bInterestingPlaceArrived` (`+0x62e8`); then
branch on whether the place's type carries an INTO activity NAME at all — retail tests the string's
first byte. With one, it resolves the name, falls back to `ACT_IDLE` with
`"Can not find interest into activity %s"` when `ActivityNameToId` answers -1, raises
`m_bfAINPCFlags |= 0x20000000` (`INTERESTING_INTO`), sets the phase word `+0x6304` to 1, restarts the
activity and parks the refresh clock `+0x63d4` at `-1.0f`. Without one, it takes the idle activity,
falls back the same way with `"Can not find interest activity %s"`, sets the phase to 2, restarts and
sets the refresh clock to `curtime + RandomFloat(2, 10)`. AFTER both arms, unconditionally, it stamps
`m_flWaitFinished` (`+0x5db4`) with `curtime + RandomFloat(m_fMinStayTime, m_fMaxStayTime)` — the
field every `TASK_WAIT*` reads — releases the motor's yaw hold, and with `m_bMatchOrientation` set
hands the motor a yaw with a half-turn flip applied when the motor's `+0x28` byte is set.

`0x102aa210` is the loop over the same three phases. The refresh decision has two mutually exclusive
gates: with no refresh pending (`+0x63d4 == -1.0f`) or a non-looping sequence it keys on
`m_bSequenceFinished`, advancing phase 1 to phase 2 and, at phase 3, ending the visit — clearing
`INTERESTING_INTO`, committing the root motion with `MoveToBoneOriginAngles("Bip01", false, true)`,
setting `m_flWaitFinished` to `curtime`, and consuming `m_bfAINPCFlags2 & 8` (`INTERESTING_LOST`) as
the "task finished" answer; otherwise it keys on `curtime >= +0x63d4` plus `m_bSequenceFinished`. A
refresh re-resolves the idle activity and restarts it only when the id differs from `m_Activity`
(`+0xfec`) or the current sequence does not loop. Independently, reaching `m_flWaitFinished` — or
`INTERESTING_LOST` standing — enters the OUTOF phase when `INTERESTING_INTO` is set and the phase is
neither 1 nor 3, and otherwise finishes the task outright. The body ends by releasing the motor yaw.

`0x1029f780` caches a patrol node's interesting place at `+0x6300` and never invalidates it: on a
zero cache it resolves the patrol node's interest record (`0x1029f730`), looks the record's `+0x468`
name up by name, and stores that entity's own `+0xb0` — not the entity.

**Unrecovered:** `_DAT_104454d0` (= **0.5f**, float32; read 2026-09-21, `rdata-cells.md`), the OUTOF-phase wait `0x102aa210` adds; `_DAT_1044c3a8` (= **180.0f**, float32; read 2026-09-21, `rdata-cells.md`), the
half-turn constant the match-orientation arm adds or subtracts; which of the two yaw sources a given
caller of `0x102a9f40` reaches (the decompiler's branch keys on a register the prologue never loads);
and what `+0xb0` on the entity `0x1029f780` resolves is.

## The motor and navigator seam — `0x1027d990`, `0x1027d9b0`, `0x102ecb50`, `0x102eeae0`, `0x102eeb50`, `0x1029f6c0`, `0x1027a6c0`, `0x10382d20`, `0x102bf7e0`, `0x102e1300`

_Recovered 2026-09-13, story 29c-1._

Ten bodies, all of them one NPC word away from `CAI_Navigator` or `CAI_Motor`, and together they are
the whole of what layers 0–9 ask of either object.

`FUN_1027d990` (10 bytes, **29 direct callers** — the widest read of the family) is
`m_pNavigator (+0x5d34)->field_0x18`, the native navigation type, and `FUN_1027d9b0` (11 bytes) is
its setter through `0x102eeba0`. Every ground/fly/climb branch in the kernel keys on that one word.

`CAI_Navigator::vfunc3` (`0x102ecb50`, 40 bytes) snapshots three of the OWNER's pointers into itself
— `m_pMotor` (`npc+0x5d44`) into `nav+0x20`, `m_pMoveProbe` (`+0x5d40`) into `+0x24`,
`m_pLocalNavigator` (`+0x5d38`) into `+0x28` — and stores its argument at `+0x2c`.

`CAI_Navigator::OnNavFailed` (`0x102eeae0`, 75 bytes, slot 10) calls the navigator's own reset
(`0x102eeb70`), writes the file/line marker `"E:\Vampire\main\dlls\ai_navigator…"` / `0x406` into the
owner's `+0x1b44`/`+0x1b48`, dispatches the owner's `TaskFail` (`vtable+0x700`, slot 448) with the
caller's reason, re-plays `SetIdealActivity(owner, FUN_1027a6c0(owner))` (`0x10272650`), and sets
`this->+0x1c = 1`. **`CAI_Navigator#9` (`0x102eeb50`) is the same body**: the listing is
`MOV EDX,[ESP+8]; MOV EAX,[ECX]; MOV [ESP+8],EDX; JMP [EAX+0x28]` — it shifts its second argument
down one stack word and tail-jumps to slot 10. 29c recorded which two navigator virtuals those
indices are as unrecovered; the listing settles it.

`FUN_1027a6c0` (41 bytes, 10 direct callers) is the activity `OnNavFailed` re-plays: the pending
link's cached value (`0x102ee510`) when the link is valid (`0x102ee6a0`) and not `-1`, else `1`
(`ACT_IDLE`). `FUN_10382d20` (14 bytes) is its cancel half, `m_pMotor (+0x5d44)->0x102e1e20(-1)`.

`FUN_102bf7e0` (38 bytes) stops an active goal — `if (0x102ee2e0(nav)) 0x102ee2c0(nav)`, SDK's
`IsGoalActive` / `StopMoving` — and then sets `m_bShouldMove` (`+0x1a40`) **unconditionally, outside
the `if`**.

`FUN_1029f6c0` (74 bytes) resolves a `CAI_Node` through the navigator's node array (`nav+0x2c`,
count at `[0]`, entries at `[1]`) using the index the argument's route step carries (`arg->+4`, then
`+0x14 + arg->+4->+0x10 * 4`), and answers `node+0xa0`. An index out of range bumps the counter
`DAT_106c994c` and answers 0, which is also its answer for a null argument or a `-1` index.

`CAI_Motor::vfunc16` (`0x102e1300`, 186 bytes) is the deceleration query:
`decel = owner->vtable[0x3e4]()`; when it is above `_DAT_1044fab0 = 0.0`,
`v = Length(motor+0x3c..+0x44)`, `t = v / decel`, `d = v*t - decel*t*t*0.5` (`_DAT_10449270 = 0.5`),
and `d` is returned when it exceeds `_DAT_1044fac0 = 10.0`; otherwise `_DAT_1044e664 = 10.0`. It is
`max(0.5 * v² / decel, 10.0)` written the long way, and **10.0 is retail's own floor**, which is what
a port with no deceleration source honestly answers.

**Unrecovered:** the link object `0x102ee6a0` / `0x102ee510` / `0x102e1e20` reach — the ledger
itemises nothing past the `m_pNavigator` chain redirect, so what a "link" is in retail's own terms is
not settled here. `0x102eeb70`'s reset, `0x102eeba0`'s write and `0x102ee620`'s goal-state word are
named only by their call sites. The three snapshot pointers `vfunc3` copies have no reader in layers
0–9.

## `AutoMovement`, `PerformMovement` and `PostRun` — `0x10280a50`, `0x1026c120`, `0x1026c7c0`

_Recovered 2026-09-13, story 29c-1._

The three per-think hand-offs between the NPC and its motor, and all three are mostly scaffolding
around one recovered fact.

`CAI_BaseNPC::AutoMovement` (`0x10280a50`, 270 bytes) runs `vtable[1000/4 = 250]`
(`StudioFrameAdvance`) FIRST, then `CBaseAnimating::GetIntervalMovement(m_flAnimTime (+0x174) -
m_flPrevAnimTime (+0x170), …)`, and only then tests the gate: `GetMoveType()` (slot 94,
`vtable+0x178`) must be **4** and `GetFlags() & 0x400` must be clear. Both true, it forwards the
delta to `m_pMotor->0x102e0bd0(delta, 0x102729d0(this), yaw, …)` and answers whether that returned 1;
either false, it answers false without touching the motor. **The gate and the order are the retail
contract**: a port may hand the root-motion EXTRACTION to its own animation system, but not the
decision about when extraction is allowed to move the body.

`CAI_BaseNPC::PerformMovement` (`0x1026c120`, 536 bytes) is a VProf push/pop and an `rdtsc` pair
around ONE statement — `m_pNavigator->vtable[0x14/4 = 5](param_1, param_2)`, both parameters
forwarded untouched.

`CAI_BaseNPC::PostRun` (`0x1026c7c0`, 552 bytes) is the same scaffolding around TWO, and the pairing
is the content: `float dt = 0x1026c540(this)`, then `vtable[0x408/4 = 258](dt, this)`
(`DispatchAnimEvents`, `0x10098c80`), then `CBaseCombatCharacter::Weapon_FrameUpdate(dt)` **with the
same number**. The weapon's frame update is downstream of the anim-event dispatch, not beside it.

**Unrecovered:** what `0x102729d0` contributes to `AutoMovement`'s motor call (it is read into the
call's second argument and nothing else in this pack reaches it), and what `0x1026c540` measures
beyond "the elapsed animation interval".

## `CheckOnGround`, `CanStandAt` and `GetGroundpoint` — `0x1026e5e0`, `0x102a0ed0`, `0x103d6a40`

_Recovered 2026-09-13, story 29c-1._

The three world probes of the family. All three trace with the mask `0x202400b`.

`CAI_BaseNPC::CheckOnGround` (`0x1026e5e0`, 678 bytes) has two halves. When condition `0x73` is
already SET, it returns without touching anything if `FL_ONGROUND` (`GetFlags() & 1`) is clear AND
the nav type is 0; anything else clears `0x73` (`0x10269b50`). When `0x73` is clear it needs nav type
0, `GetMoveType() != 7`, and `curtime - m_flCheckOnGroundTime (+0x5d5c) > _DAT_10497530 = -0.001`;
it then stamps `m_flCheckOnGroundTime = curtime + 0.5` (`_DAT_104454d0`) and traces the collision
hull from `GetAbsOrigin() + (0,0,0.1)` (`_DAT_104493d0`) to `GetAbsOrigin() - (0,0,4.0)`
(`_DAT_10449148`). On a CLEAR trace (`fraction == _DAT_10449280 = 1.0`) it sets `0x73`
(`0x10269a20`) and calls `SetGroundEntity(NULL)` (slot 208); otherwise, if the trace hit an entity
that is not already `GetGroundEntity()` (slot 209), it adopts it. The deadline is stamped BEFORE the
trace, so a refused probe still costs half a second.

`CAI_BaseNPCTroika::CanStandAt` (`0x102a0ed0`, 167 bytes) is three statements:
`m_bForceNPCCheck (+0x63da) = 1`, `m_pMoveProbe (+0x5d40)->CheckStandPosition(pos, …, 0, 0)`
(`0x102e7270`), `m_bForceNPCCheck = 0`. The BRACKET is the behaviour — for the duration of the probe
both collision-ignore chains below skip their NPC/player/sleeping arm, so the probe sees other NPCs
as solid.

`CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`, 553 bytes) traces the `m_eHull` (`+0x1568`) extents
(`0x102d6140` / `0x102d6160`) straight down `_DAT_10447ee0 = 1000.0` units from the caller's point,
with the filter `0x101d3190(this, 7)`. On a hit (`fraction < 1.0`) the answer is
`start + fraction * (0, 0, -1000)` — the compiler folded the ray's X and Y deltas to
`_DAT_104454c4 = 0.0` and its Z to `_DAT_104d00ac = -1000.0`, because the ray is straight down. With
NO hit the answer is the triple `DAT_10713de0/de4/de8`, and **that triple is `vec3_invalid`, not
`vec3_origin`**: `staticinit_101371a0` writes `0x7f7fffff` (`FLT_MAX`) into all three, and its only
other reader is `CNPC_VWerewolf::GetForwardYawForHint` (`0x103d7210`), which treats it as a sentinel.
A no-hit ground snap therefore answers "there is no ground point" and a caller must test for it —
distinct from slot 210's `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` zeroes and which 369
readers use as `vec3_origin`.

**Unrecovered:** what `GetMoveType() == 7` names (retail's move-type numbering is not itemised
here); what condition `0x73` is called — it sits in the unnamed tail of `CAI_BaseNPC`'s 0x00..0x76
registrar; and `CheckStandPosition`'s flags argument.

## `ValidateNavGoal` and `MoveDone` — `0x10280360`, `0x101c1720`, `0x10026c50`

_Recovered 2026-09-13, story 29c-1._

`CAI_BaseNPC::ValidateNavGoal` (`0x10280360`, 576 bytes, slot 528, 6 dispatch sites) is retail's
`IsCoverPosition` check spelled inline. It runs ONLY when the navigator's goal state (`0x102ee620`)
is exactly **6**, and only with a live `GetEnemy()` (slot 167). It takes the goal
(`0x102ee140`), pushes its Z through `0x102f9c70`, adds `GetViewOffset()` (`vtable+0x854`), and
traces from there to `GetEnemy()->EyePosition()` (`vtable+0x304`) with the mask `0x2804091`. A trace
that reaches — `fraction == _DAT_10449280 = 1.0`, i.e. **nothing is in the way** — means the goal is
not cover, and that is the FAILURE case: if the running program's mask does not list condition
`0x39` (`ConditionInterruptsCurrentSchedule 0x10269c70`) it writes the file/line pair
`"E:\Vampire\main\dlls\AI_BaseNPC…"` / `0xbc` into `+0x1b44`/`+0x1b48`, calls `TaskFail(0x1b)`
(slot 448) and answers false; if the mask does list it, it merely sets `0x39` (`0x10269a20`) and
answers true. Every other path answers true.

`MoveDone` is two bodies. The base slot-133 fill (`0x10026c50`, 13 bytes) is a tail JMP through the
stored callback: `MOV EAX,[ECX+0x114]; TEST EAX,EAX; JZ ret; JMP EAX`. `CAI_BaseNPC`'s own
(`0x101c1720`, 68 bytes; the decompiler cannot recover the tail's jump table) prefixes it with the
`CBaseEntity` mover's own shutdown — `MoverData_CurrPos (+0x498) = MoverData_TargetPos (+0x494)`,
then `m_movementType (+0x558)` 1 runs `0x101c1790` and 2 runs `0x101c19e0`, then `m_movementType = 0`
— and only then dispatches `m_pfnMoveDone` (`+0x114`).

**Unrecovered:** what `0x102f9c70` does to the goal's Z, and condition `0x39`'s name (the same
unnamed tail as `0x73`). `0x101c1790` and `0x101c19e0` are the linear and angular move-done halves
by position in the switch, not by a recovered name.

## The collision-ignore chains — `0x1029afc0`, `0x1029b180` and their seven species arms

_Recovered 2026-09-13, story 29c-1._

`CAI_BaseNPCTroika::ShouldIgnoreCollision` (`0x1029afc0`, 351 bytes, slot 68) and
`NavIgnoreCollision` (`0x1029b180`, 206 bytes, slot 69) share their first three arms byte for byte
and then diverge once.

The shared head, in order. **Unless `m_bForceNPCCheck` (`+0x63da`) is set**: with
`m_bfAINPCFlags (+0x14b8) & 0x40` (`NAV_IGNORE_NPC`), a candidate whose `+0x94` (`m_pBaseNPC`) or
`+0xa8` (`m_pPlayer`) is non-null is ignored; without it, a candidate whose `+0x98`
(`m_pBaseNPCTroika`) is non-null AND whose own `m_bfAINPCFlags & 0x20000` (`SLEEPING`) is set is
ignored. Those `+0x9x` words are `CAI_BaseNPCTroika`'s self-cast cache
(`../npc-kernel/layout.md`, `+0x0094`..`+0x00a8`), so each test is "is the candidate of that class".
Then: the candidate being `m_hKickPhysicsProp` (`+0x643c`) is ignored, and the candidate being a
`CBaseCombatWeapon` (`+0xa0 m_pCombatWeapon`) is ignored.

They then part. Slot 68 tests the claimed hint node: `m_pHintNode (+0x5ddc)` non-null with
`m_nHintType (+0x5dc)` strictly inside `(0x283b, 0x283e)` AND `candidate->m_edtDerivedType (+0x4c) &
0x14`; then `m_edtDerivedType & 0x2` alone; then the grapple pair — `m_GrapplePartner (+0x1538)`
resolving non-null with `m_GrappleRole (+0x153c) != -1` and naming the candidate. Slot 69 replaces
all of that with one test whose MASK is chosen by `m_bNavIgnorePhysicsProps` (`+0x65f7`):
`m_edtDerivedType & 0x16` when it is set, `& 0x12` when it is not. Both chains tail into
`CBaseAnimating::IsIgnoreCollisionEntity` (`0x1008be20`), which is
`m_hIgnoreCollisionEntity (+0x055c)` resolved and compared.

Seven classes add an arm in front. `CNPC_VHengeyokai#69` (`0x10380f90`), `CNPC_VMingXiao#69`
(`0x10396fd0`) and `CNPC_VTzimisce#69` (`0x103bfa00`) are byte-identical: `m_edtDerivedType & 0x16`
ignores, else the base. `CNPC_VWerewolf` fills both (`0x103d9ab0`, `0x103d9ba0`, 184 bytes each) with
that same test plus `GetFlags2() & 8`. `CNPC_VGargoyle#69` (`0x10379490`, 408 bytes) runs the `0x16`
test and then three `FClassnameIs` compares — `prop_dynamic`, `func_brush`, `func_door_rotating`,
all case-insensitive `__strcmpi` with no trailing `*` — and ignores on any match.
`CNPC_VMingXiaoTentacle` fills both (`0x1039eb50`, `0x1039eb90`) with the inverse shape: it ignores
UNLESS the candidate is non-null and (it is not a `CBaseCombatCharacter` (`+0x9c`) OR
`m_bIgnoreCollision (+0x6688)` is clear). `CNPC_VRat#68` (`0x103ad6d0`) ignores one fixed global
entity (`0x101cda50`) and otherwise falls to the base.

**Unrecovered:** what `m_edtDerivedType`'s bits mean. Only bit 2 is named anywhere in the oracle
(`programs.md` § "The cover and kick chooser, and the combat leftovers (2026-09-08)" reads `& 4` as
PHYSICS_PROP); the `0x2`, `0x10`, `0x12`, `0x14` and `0x16` masks these chains test are otherwise
unread. `0x101cda50`'s global entity has no recovered identity — the body takes no argument.

## The yaw-speed ladder — `0x10297ce0`, `0x10280bb0`, `0x102624b0`, `0x1035a810`, `0x1035b080`, `0x1035be80`, `0x10374130`, `0x10394930`, `0x103ba020`, `0x103d0a30`

_Recovered 2026-09-13, story 29c-1._

Slot 516, `MaxYawSpeed`, in degrees per second. The Troika body is what every spawnable species
dispatches to; nine classes replace it.

`CAI_BaseNPCTroika::MaxYawSpeed` (`0x10297ce0`, 471 bytes) tests `m_afMemory (+0x5d8c) & 0x2000`
first — the "turning" tag the turn-activity ladder writes (§ "The turn-activity ladder —
`0x10289d10` and `0x10297640`") — and on it answers `ABS(GetIdealYawSpeed()) * cvar(0x10924c94)`,
clamped up to `_DAT_104454c0 = 1.0`. Off it, `m_bfAINPCFlags (+0x14b8) & 0x8000000`
(`PLAYING_FACE_ANIM`) short-circuits to the default. Otherwise it switches on `m_Activity (+0xfec)`:
`0x3b`/`0x3c` and `0x1121` answer `_DAT_104492a8 = 30`; `0x13` and `0x1093`..`0x1096` answer
`_DAT_1047a3ac = 160`; `1`/`5` and `9` take a two-way branch on **bit 7 of the per-state capability
byte `+0x5b64`**, which is set only for the combat (`0x8f`) and flee (`0x85`) states. Out of combat
those two arms lazily construct and read their own convars — **`debug_slow_idle_yaw_speed`, default
`"20"`** (`0x10924e94`; the ctor's argument strings are at `0x105d902c` and `0x1057ba50`) and
**`debug_slow_walk_yaw_speed`, default `"25"`** (`0x1092411c`; `0x105d9008` and `0x105d9028`). In
combat, `1`/`5` reads `m_bAllowTurningAnims`'s convar `0x109247ec`: false gives the convar
`0x10923e84`, true gives 30; `9` falls out of the switch. Everything else is `_DAT_1049949c = 45`.

`CAI_BaseNPC::MaxYawSpeed` (`0x10280bb0`) is that default alone. `CAI_BaseHumanoid`'s
(`0x102624b0`) is a three-arm switch: `9`/`0x12`/`0x13` give `_DAT_10463584 = 15`, `0x3b`/`0x3c`
give `_DAT_104492a4 = 60`, else 45. `CGeneric_NPC` (`0x1035a810`), `CGeneric_NPC_bathack`
(`0x1035b080`) and `CGenericSabbat_NPC` (`0x1035be80`) are byte-identical: `0x13` gives 160,
`0x3b`..`0x3c` give `_DAT_1044f00c = 120`, else 45 — with no walk arm at all.

`CNPC_VDog` (`0x10374130`, 237 bytes) is the Troika ladder with three differences: its turning convar
is `0x1093ad24`, it has no `debug_slow_*` arm (idle goes straight to the turning-anims branch), and
the fall-out for a RECOGNISED activity is `_DAT_10462950 = 40` rather than 45. `CNPC_VTzimisce`
(`0x103ba020`, 143 bytes) keeps the turning arm (convar `0x1093c9fc`) over a four-arm switch:
`1`/`0xfc`/`0xfd` give `_DAT_10454110 = 5`, `0x13` gives 30, else `_DAT_104cc504 = 11`.
`CNPC_VMingXiao` (`0x10394930`) reads a tuning record (`0x101e8da0(0x10739d08)`): `+0x48` inside
`(0x1129, 0x112e)` and `+0x44` outside it. `CNPC_VWerewolf` (`0x103d0a30`, 116 bytes) is a scope
trace around an unconditional forward to the Troika body — its retail answer IS the family default.

**Unrecovered:** the names and defaults of `0x10924c94`, `0x1093ad24`, `0x1093c9fc` and
`0x10923e84`. All four are `ConVar*` in uninitialised `.data` that no corpus function constructs, so
the read sites are the whole of the evidence — the same gap the facing-target convar `0x10924f74`
has. The tuning record `0x10739d08`'s field names are unsettled (see § "Slot 465's species overrides
— `0x10357b30`, `0x103947b0`, `0x103a56f0`, `0x103d5f60`"). What activities `0x1093`..`0x1096`,
`0x1121`, `0x112a`..`0x112d`, `0xfc` and `0xfd` are is not recovered here.

## The movement tunables and `IsJumpLegal` — `0x101a6b40`, `0x101a6b60`, `0x101aa670`, `0x101a6b80`, `0x10280880`, `0x10280790`, `0x102d72b0`, `0x102d72d0`, `0x102d7760`

_Recovered 2026-09-13, story 29c-1._

Four slots of pure tunables, read out of `.rdata`. **Step height** (slot 522) is
`_DAT_10453b94 = 18.0` (`0x101a6b40`), and that body is what slot 522 carries on the Troika line.
**Slot 523 — CORRECTED 2026-09-19: this is the STEP-DOWN height, not a jump speed.** Its two
readers are `CAI_MoveProbe`'s forwarders `0x102e7e20` (`JMP [vtable+0x82c]`): the stand probe
`0x102e7270` uses it as the drop below the feet, and the ground test `0x102e4f50` stores it in the
step record right after slot 522's step height (`102e5193` / `102e51a1`). By the same SDK ordering
slot 524's 350.0 is then the max jump speed, not a gravity — CONFIRMED by use 2026-09-19: the
jump arm `0x102e6290` reads it (`102e63dd`) and passes it as the horizontal-speed argument of
`CalcJumpLaunchVelocity 0x102e7060`, where it divides the 2-D distance
(`../navigation-jump-links.md` § "Triangulate, the ground accounting and the jump arm"). The old label, kept for the cross-references: **Max jump speed**
(slot 523) is where the two lines differ: `CAI_BaseNPC` (`0x101a6b60`) returns the
SAME 18.0, while `CAI_BaseNPCTroika` (`0x101aa670`) overrides it with `_DAT_1044faa8 = 36.0`.
**Jump gravity** (slot 524, `0x101a6b80`) is `_DAT_10477ce8 = 350.0` and nothing overrides it — zero
dispatch sites in the closure, but the slot is filled, so the body is not dead. `CAI_TestHull`
answers 522 and 523 with one constant, `_DAT_10462950 = 40.0` (`0x102d72b0`, `0x102d72d0`).

`IsJumpLegal` (slot 521) is a two-line forward into the shared geometry helper `FUN_10280790`, with
**80.0 / 250.0 / 160.0** on the base line (`0x10280880`) and **1024 / 1024 / 1024** on
`CAI_TestHull` (`0x102d7760`). The helper's four arms, in order and all against the three points
`start`, `apex`, `end`:

1. `maxRise + 0.1 < end.z - start.z` → illegal (`_DAT_104493d0 = 0.1`);
2. `maxDrop + 0.1 < start.z - end.z` → illegal;
3. `maxRise * 1.25 < apex.z - start.z` → illegal (`_DAT_10460020 = 1.25`, and this one takes NO
   slack);
4. `maxDistance + 0.1 < Length(end - start)` → illegal, a full 3-D length.

Arm 2 is unreachable on the base thresholds: a drop big enough to trip 250 is also a distance big
enough to trip 160, and arm 4 runs on the same delta.

**Unrecovered:** which three species override step height "for real" outside this pack (the ledger
counts them but the bodies are in other layers), and why the base line's jump speed and step height
share one constant.

## `OverrideMove`, `ShouldMoveAndShoot` and `OnObstructingDoor`'s base branch — `0x1027da90`, `0x10357ba0`, `0x102bf4a0`, `0x10278c60`, `0x1027dc80`

_Recovered 2026-09-13, story 29c-1._

`CAI_BaseNPC::OverrideMove` (`0x1027da90`, 118 bytes, slot 525) is a scope-trace push/pop around an
unconditional `false`. **The base DECLINES**, and that is what every species override is measured
against. `CNPC_Crow#525` (`0x10357ba0`) is the only override in layers 0–9: at nav type **2** it
hands the move to `0x10357be0` and answers true, otherwise it declines like the base.

`CAI_BaseNPCTroika::ShouldMoveAndShoot` (`0x102bf4a0`, 80 bytes, slot 575) is a three-gate funnel:
a live `GetEnemy()` (slot 167), `m_bfAINPCFlags2 (+0x14bc) & 0x400` (`MOVE_FACE_ENEMY`), a live
`GetActiveWeapon()`, and the weapon's own `vtable[0x5a0]() & 0x6000`. Only all four pass does it
answer `CAI_BaseNPC::ShouldMoveAndShoot` (`0x10278c60`), whose entire body is
`CapabilitiesGet() >> 6 & 1` — bit 6 of `m_afCapability` (`+0x5cec`), `bits_CAP_MOVE_SHOOT`. Note
that the weapon arms fall out with a ZERO rather than with the enemy test's value.

`CAI_BaseNPC::OnObstructingDoor` (`0x1027dc80`, 102 bytes) is slot 531's BASE branch — the Troika
line carries `0x102984a0` instead. Arm by arm, with `moveGoal->+0x28` the goal's own maximum
distance and `door->+0x4f8` its state word: a goal already shorter than the clearance is not
obstructed; a door in any state but **1 or 3** is not obstructed; a clearance below
`_DAT_104493d0 = 0.1` answers `-1` and leaves the goal alone; anything else SHORTENS the goal to the
clearance and answers `0`.

**Unrecovered:** what `0x10357be0` does for the crow, what the weapon capability bits `0x6000` are
called, and what door states 1 and 3 are named.

## `CanStandOn` and `GetGroundVelocityToApply` — `0x10026f80`, `0x1039ebd0`, `0x10027370`

_Recovered 2026-09-13, story 29c-1._

Slot 166 (`0x10026f80`, 28 bytes) is two lines: a null candidate is standable, and a non-null one
answers its own `IsStandable()` (slot 164, `0x100b50a0` — solid flags `0x10` clear, then move type
1 / 6 / 2, else `0x100b5110`). `CNPC_VMingXiaoTentacle#166` (`0x1039ebd0`) adds one arm in front:
its own companion (`0x1039ede0`) is never standable, which excludes the tentacle's head from its own
test.

Slot 210 (`0x10027370`, 33 bytes) writes the three shared statics `DAT_1070d1b0/b4/b8` into its
`Vector&` out-parameter. `staticinit_101370b0` zeroes all three and nothing else writes them, and
369 functions read them: it is `vec3_origin`, so the default ground-velocity contribution is exactly
zero.

**Unrecovered:** `0x100b5110`, `IsStandable`'s own tail, and what `0x1039ede0` resolves — the
tentacle's companion is not itemised in the proxy chain.

## The jump chain — `0x10361a70`, `0x103b1300`, `0x1036e160`, `0x1036c8d0`, `0x10362430`, `0x10361730`, `0x103618a0`, `0x103aad40`, `0x103a9e70`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VAsianVampire::SetupJump` (`0x10361a70`) and `CNPC_VSheriffMan::SetupJump` (`0x103b1300`) are
the same 284 bytes apart from one constant. Both refuse outright when their float argument is `0.0`.
Then: `m_vJumpOrigin (+0x649c) = GetAbsOrigin()`,
`m_vJumpTarget (+0x64a8) = m_pHintNode->GetAbsOrigin()` (the hint is dereferenced WITHOUT a null
check), `m_fJumpHeight (+0x64b4) = rise + (max(hint.z, self.z) - self.z)`, then the commit
`0x102c4e80`. The rise is `_DAT_104a9310 = 100.0` for the Asian vampire and
`_DAT_104c614c = 400.0` for the sheriff.

`CNPC_VChangBros::SetupSuperJump` (`0x1036e160`, 318 bytes) is the same shape with two additions:
the rise is chosen per jump — `ABS(deltaZ) < _DAT_104ada3c = 40.0` gives `_DAT_104ada1c = 50.0`,
otherwise `_DAT_104ada20 = 150.0` — and it CLEARS condition `0x7b` (`0x10269b50`) before the commit.
29c's one-line walk read that call as "plays activity 0x7b"; `0x10269b50` is `ClearCondition`, and
`0x7b` is above `CAI_BaseNPC`'s 0x00..0x76 registrar, so it is a species registration.

`CNPC_VChangBros::CheckForJumpAttack` (`0x1036c8d0`, 375 bytes) gates the leap:
`m_ChangType (+0x66b8)` must be 0, `m_hClosestPlayer (+0x628c)` must resolve, **both** the player's
sector and this NPC's own must not be 4, and `curtime - m_fLastJumpTime (+0x66cc)` must be at least
`_DAT_104ada00 = 16.0`. It then answers TRUE at once when `m_iSquadDisconnected (+0x5bb0) > 0` or
`m_pSquad (+0x5da4)` is null; otherwise it walks the squad for a `CNPC_VChangBros` sibling
(`___RTDynamicCast`) other than itself that jumped inside the same 16 seconds and refuses if it
finds one.

`CNPC_VAsianVampire::GetJumpSchedule` (`0x10362430`, 208 bytes) answers schedule `0x15b` when the
closest player's Z is more than `_DAT_104a9314 = 40.0` BELOW this NPC's — the comparison is against
the NEGATED constant — and `0x15a` otherwise, including with no player at all.
`IsPosNearStoredJumpPositions` (`0x103618a0`, 188 bytes) walks the two entries of
`m_vLastJumpPosition (+0x66b8)` and answers true when the 2-D distance to the argument is strictly
under `_DAT_104a9308 = 30.0`; Z is ignored. `SelectJumpbaseNode` (`0x10361730`, 287 bytes) walks the
global hint list `DAT_10925450` (next link `+0x5d8`) for the nearest node of type **18000** that
`PositionClearForTeleport(hint, DAT_104a9320 = 150.0)` admits, by 3-D distance, and remembers it
through `AddHintToStoredJumpPositions`. It is `SelectLedgeNode`'s shape with hint type `0x4653`
swapped for 18000.

`CNPC_VSabbatLeader::SetJumpVelocityTowardPlayer` (`0x103aad40`, 416 bytes) builds a lead position
from the POSITION delta between the two bodies — not from the player's velocity —
`lead = (player - self) * DAT_104c3cb8 = 25.0` with its **Z term scaled from a literal zero**, feeds
`self.GetAbsOrigin()` and `player.GetAbsOrigin() - lead` to the arc solver `0x102c4cc0` with
`GetAbsVelocity()` (`vtable+0x318`) as the out-parameter, and assigns the result with
`SetAbsVelocity`. `PlayerInNoJumpZone` (`0x103a9e70`, 261 bytes) walks the same global hint list for
type `0x3e84` and answers true only when the player AND this NPC are both within
`DAT_104c3ccc = 100.0` of the **same** hint's centre line (`DistToHintCenterLine2D_2`).

**Unrecovered:** the arc solver `0x102c4cc0` and the commit `0x102c4e80` in their own terms;
`CNPC_VChangBros::GetSector`'s partition; `PositionClearForTeleport`'s test; and what schedules
`0x15a` / `0x15b` and hint types 18000 / `0x3e84` are named.

## The stationary watchdog and the stuck push — `0x10362540`, `0x10362670`, `0x103ab580`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VAsianVampire::UpdateMovedTimeStamp` (`0x10362540`, 227 bytes) re-stamps
`m_fMovedTimeStamp (+0x66d8) = curtime` and refreshes `m_vMovedPosition (+0x66dc)` only when the
3-D distance travelled since the last refresh is strictly above `_DAT_104a931c = 40.0`. Its reader
`StationaryForTooLong` (`0x10362670`, 129 bytes) answers true when
`_DAT_104a9318 = 3.0 <= curtime - m_fMovedTimeStamp` — inclusive.

`CNPC_VSabbatLeader::CheckStuck` (`0x103ab580`, 864 bytes; read from the listing, because the
decompiler aliased the two entities' collision extents onto one pair of registers) is a
box-overlap test and a push. It builds, for the closest player and for itself, a 2-D "radius"
`Length2D(maxs - mins) * 0.5` and a centre `GetAbsOrigin() + (mins + maxs) * 0.5` from
`m_Collision`'s OBB slots. It proceeds only when the two Z spans overlap, then flattens
`delta = selfCentre - playerCentre` to `delta.z = 0`, and proceeds again only when
`Length(delta) < selfRadius + playerRadius`. A degenerate delta (`< _DAT_104c3d14 = 1e-06`) gets
`delta.x = 1.0`. The push is `delta * (sumRadius + 1.0)` — **not normalised**, so its magnitude is
`len * (sumRadius + 1)` — applied from the PLAYER's centre, with `+ _DAT_10454110 = 5.0` added to Z
and the result clamped up to the NPC's own box bottom, then written with `SetAbsOrigin`
(`vtable+0x360`).

**A retail bug, and it is in the Z span.** `selfTop` and `selfBottom` are built from SELF's origin
and the **PLAYER's** box extents: the listing loads `[EBP+8]` and `[EBX+8]` — which still hold the
player's maxs and mins — after self's own pair has been read into `EDI` and `[ESP+0x14]`. The two
spans therefore agree only when the two bodies use the same hull. A shipped program was tuned against
it, so a port keeps it.

**Unrecovered:** nothing in these three; every constant and every arm is read.

## `TranslateNavGoalPosition`'s Tzimisce branch — `0x103bf580`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VTzimisce#410` (112 bytes) replaces the base `0x101a6420`. With `m_ePathMode (+0x668c) == 1`
and a live `GetEnemy()` (slot 167) it answers the enemy's `vtable[0x370/4 = 220]` position; with
`m_ePathMode` neither 1 nor 2 it answers the caller's goal unchanged; otherwise, and **also when
path mode 1 finds no enemy** — retail falls THROUGH rather than returning — it answers
`m_vecPickupTargetPos (+0x6674)` if `m_hPickupTarget (+0x6670)` resolves, else the caller's goal.

Note the offsets: `m_ePathMode` is the HIGHEST of the three, not the lowest. 29c's one-line walk has
the triple in the wrong order.

**Unrecovered:** what path modes 1 and 2 are called.

## The boss node selectors — `0x103b0930`, `0x103b0ab0`, `0x103615c0`, `0x103a9540`, `0x103a9760`, `0x103a9ad0`, `0x103b0630`, `0x1035ddd0`, `0x1036cce0`

_Recovered 2026-09-13, story 29c-1._

Nine bodies across five boss species, all the same shape and none of them a vtable slot: resolve
`m_hClosestPlayer` (`+0x628c`), walk the global `CAI_Hint` list `DAT_10925450` (link `node+0x5d8`,
type word `node+0x5dc`), filter on the hint type, score each survivor, keep the best. The position
of a node is its `GetAbsOrigin` (slot 217) and the distances are ROOTED, not squared — every one of
them goes through `PTR_thunk_FUN_101371d0`, which is `sqrt`. Ties keep the FIRST candidate, because
every compare is a strict less-than.

The hint types are the whole of the per-species vocabulary: 17000 and `0x4269` (`CNPC_VAndreiBlood`),
18000 (`CNPC_VChangBros`), `0x4651` centre, `0x4653` ledge and `0x4652` jumpbase
(`CNPC_VSheriffMan`, and the last two for the Changs too), `0x3e82` archway and `0x3e85` dive
(`CNPC_VSabbatLeader`).

The five ungated pickers are plain minimisations. `SelectCenterNode` (`0x103b0930`) takes the
`0x4651` node nearest the player. `SelectLedgeNode` (`0x103b0ab0`) takes the nearest `0x4653`, and
its `char` argument chooses the reference point — zero measures from the PLAYER and anything else
from the NPC — but the closest-player gate stands in front of both arms, so a Sheriff with no player
answers null even when measuring from himself. `SelectTeleportArchway` (`0x103a9540`) requires a FLAT
distance of at least `_DAT_104c3cbc = 24.0` and then scores on the yaw delta ALONE
(`AngleDiff(player.yaw, VectorAngles(player - node).y)`), so distance never breaks a tie.
`SelectDiveOutPoint` (`0x103a9ad0`) requires `_DAT_104c3cfc = 40.0` flat and scores
`|yawDelta| + flatDistance`, a weighting in which one degree costs exactly one unit.
`SelectDiveInPoint` (`0x103a9760`) is that body plus two gates: the leader's own flat distance to the
player, normalized in place by `FUN_10137220`, must reach `_DAT_104c3d00 = 45.0` or the list is never
walked, and each candidate's `node - self` delta must normalize to more than `_DAT_104c3ce4 = 1e-4`
and dot NEGATIVE against that player direction — the dive point has to lie in the hemisphere away
from the player.

The four gated pickers add `PositionClearForTeleport` (below) as a filter.
`CNPC_VAsianVampire::SelectLedgeNode` (`0x103615c0`) is the only one of the nine that never asks for
a player at all: it runs the clearance test at `DAT_104a9320 = 150.0` BEFORE it measures anything,
takes the `0x4653` node nearest its own origin, and records the winner through
`AddHintToStoredJumpPositions`.

`CNPC_VSheriffMan::SelectTeleportNode` (`0x103b0630`) is the elaborate one. Its distance multiplies
the Z component by `_DAT_10450564 = 100.0` before the root, so one unit of height reads as a hundred
and the Sheriff will not teleport off his own floor; the same constant `DAT_104c6124 = 100.0` is both
the distance floor and the clearance handed to the gate; and the score is a normalized weighted sum,
`|yawDelta| * (1/180) * 0.4 + min(dist, 1000) / 1000 * 0.6` (`_DAT_104c6d40`, `_DAT_1044a2bc`,
`DAT_104c6144`, `_DAT_104c6d3c`). It caches the winner into `m_vLastTeleportPosition` (`+0x66d4`) and
stamps `m_fLastTeleportTime` (`+0x66e0`).

`CNPC_VAndreiBlood::SelectTeleportNode` (`0x1035ddd0`) draws a coin ONCE before the walk —
`RandomInt(0, 1)` through `DAT_1070b244` — and picks the NEAREST or the FARTHEST accordingly, seeding
its running best at `FLT_MAX` or `0.0` to match. It caches the position at `+0x66c0` and stamps no
clock; Andrei has no `m_fLastTeleportTime` at all.

`CNPC_VChangBros::SelectTeleportNode` (`0x1036cce0`) keeps two bests, the nearest of all candidates
and the nearest whose `GetSector` matches the sector of the PLAYER's position (not its own — the
reference comes from `GetSector(player->GetAbsOrigin())`), and the sector-matched one replaces the
plain one outright when it exists. It caches at `+0x66bc` and stamps `+0x66c8`.

Retail's tail is a shipped crash and the port names it as a divergence: all three teleport selectors
dereference the winning node UNCONDITIONALLY to cache its position, so a boss on a map that authors
no reachable node of its types faults. The port leaves the cache untouched and answers null.

**Unrecovered:** which of the two vectors `SelectDiveInPoint` hands its first `FUN_10137220` — the
flat delta at `ebp-0x3c` or the full one at `ebp-0x30`. The listing builds both immediately before
the call and the later dot product reads the flat one's three floats as a unit vector, which is what
settles it in favour of the flat one, but the call's argument register is not written in the
prologue. Also unrecovered: what a `0x4652` jumpbase node means to a Sheriff, which accepts it as a
teleport destination alongside its ledges.

## `PositionClearForTeleport`, four species — `0x1035e030`, `0x103629d0`, `0x1036d350`, `0x103b0c70`

_Recovered 2026-09-13, story 29c-1._

One retail name on exactly four classes and on nothing else — there is no base body — and four
genuinely different rules. Each takes a candidate position and a clearance and answers whether the
NPC may teleport there.

`CNPC_VAndreiBlood` (`0x1035e030`) asks two things, both 3-D: the candidate must be at least the
clearance from his own `GetAbsOrigin`, and at least `DAT_104a6f7c = 100.0` from
`m_vLastTeleportPosition`. `CNPC_VSheriffMan` (`0x103b0c70`) is the same pair, with
`DAT_104c6124 = 100.0` as its floor, plus a third term: the candidate must also be at least the
clearance from the closest player, when one resolves.

`CNPC_VAsianVampire` (`0x103629d0`) is the odd one. It leads with
`IsPosNearStoredJumpPositions(pos)`, and both of its distance terms are FLAT — so a candidate
directly overhead is clear however close it is vertically, which is what lets a ledge-hopping vampire
stack its jumps. The player term's compare is at-or-under where its own is strictly under; the
listing's `(a < b) != (a == b)` is that at-or-under, and the asymmetry is retail's.

`CNPC_VChangBros` (`0x1036d350`) is Andrei's pair (with `DAT_104ad9f4 = 100.0`) followed by the whole
squad. When `m_iSquadDisconnected < 1` and `m_pSquad` is live it asks the squad object directly
(`thunk_FUN_10315a80`), then walks the member list re-reading the count every iteration, RTTI-casts
each member to `CNPC_VChangBros`, skips itself, and refuses the candidate when a brother's own
`GetTeleportPosition` lands within the clearance of it. That is how the two brothers never claim the
same spot inside the three-second window `GetTeleportPosition` enforces.

**Unrecovered:** what `thunk_FUN_10315a80` asks the squad object — the port names the call and leaves
it answering "not taken", which is what an empty squad answers.

## The Chang brothers' arena — `0x1036e580`, `0x1036b6b0`, `0x1036d270`, `0x1036e6f0`

_Recovered 2026-09-13, story 29c-1._

`GetSector` (`0x1036e580`, name from `NameFromStrings` at `0x106313c8`) partitions the brothers'
arena around `m_vArenaCenter` (`+0x66dc`), gated on `m_bCenterStored` (`+0x66e8`) — a clear byte
answers 0 for every position. Read from the listing, because the decompiler turned both FPU compares
into `(a < b) != (a == b)`, which is at-or-under:

```text
d2 = (p.x - c.x)^2 + (p.y - c.y)^2                        // XY only
if (p.z - c.z >= 200.0)   return d2 <= 525^2 ? 3 : 4      // _DAT_104ada6c, _DAT_104ada68
if (d2 <= 270.0)          return 1                        // _DAT_104ada60
if (d2 <= 370^2)          return 2                        // _DAT_104ada64
return 4
```

The sector-1 threshold is the odd one and is recorded as it stands: `370.0` and `525.0` are squared
by their own static initializers (`0x1036e500` and `0x1036e550`) before the compare, while `270.0` is
compared against the squared distance directly. Sector 1 is therefore a circle of radius
`sqrt(270) = 16.4` units, not 270 — a shipped inconsistency the two bodies that read the sector were
tuned against.

`SectorIsInPit` (`0x1036b6b0`) is `0 < s && s < 3`: sectors 1 and 2 are the pit, and 0, 3 and 4 are
not. `GetTeleportPosition` (`0x1036d270`) answers `m_vLastTeleportPosition` only while
`curtime - m_fLastTeleportTime` is at or under `_DAT_104ada04 = 3.0` seconds, which is the window
inside which a brother's claim on a node stands.

`CNPC_VChangBros::IsUnreachable` (`0x1036e6f0`, slot 530 for the three Chang classes) puts those
together: RTTI-cast the target to `CBaseCombatCharacter`, take both sectors, and answer unreachable
when one of the two is in the pit and the other is exactly sector 3. It is symmetric — both orders
are written out — and it is about the ARENA rather than about pathing. A target that is not a combat
character skips the whole thing and falls to `CAI_BaseNPC::IsUnreachable`.

**Unrecovered:** what writes `m_vArenaCenter` and `m_bCenterStored`; neither is in this band's rows.

## The Sheriff's floor heights — `0x103b1510`, `0x103b1680`, `0x103b1790`

_Recovered 2026-09-13, story 29c-1._

`CacheFloorHeights` (`0x103b1510`) is two nested gates and three writes, and the nesting is the
point: it takes `SelectCenterNode`'s Z into `+0x66e8` and only then asks `SelectLedgeNode(0)` —
measured from the PLAYER — for a second height, which it takes into `+0x66ec` while setting the latch
at `+0x66e7`. A map that authors ledges but no centre node leaves both the latch and the ledge height
untouched.

`CategorizeHeight` (`0x103b1790`) is `*out = |z - centerZ| < |z - ledgeZ| ? 0 : 1` — strictly nearer
wins for the centre, so an exact tie goes to the LEDGE. It does not consult the `+0x66e7` latch at
all, which is why a Sheriff with no cached ledge categorises everything against a zero height.
`CategorizeHeights` (`0x103b1680`) zeroes BOTH out-parameters first and then, only if the closest
player resolves, fills them with the player's height and its own in that order — so no player means
`0, 0`, not an untouched pair.

**Unrecovered:** what consumes the two categories; no caller of `0x103b1680` is in layers 0–9.

## `DistToSegment` — `0x103c6b70`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VVampireBoss::DistToSegment` is a free helper — nothing in it touches `this`; the first vector
arrives in `ECX` because the compiler gave the function `__thiscall` — and it differs from the
ordinary clamped point-to-segment distance in two arms:

```text
ab = b - a;  L2 = ab.LengthSqr();
if (L2 < 1e-5) return 0.0f;                        // _DAT_104ce8c0, a SQUARED length
t = Dot(p - a, ab) / L2;
if (t <= 0)      d2 = (p - a).LengthSqr();
else if (t >= 1) d2 = (p - b).LengthSqr();
else             d2 = (p - (a + ab*t)).LengthSqr();
return sqrt(d2);
```

A degenerate segment answers zero rather than the distance to its endpoint, and `t` is BRANCHED
rather than clamped. The listing's `(t < 0) == (t == 0)` is `t > 0`, which is what selects the
projection arm.

**Unrecovered:** nothing.

## The unreachable-entity cache — `0x102741e0`

_Recovered 2026-09-13, story 29c-1._

`CAI_BaseNPC::IsUnreachable` (slot 530, filled by 74 classes) walks `m_UnreachableEnts` (`+0x5d48`,
0x14 bytes per record: an `EHANDLE`, an expiry time, a position) BACKWARDS from the count at
`+0x5d54`. A record whose handle no longer resolves is removed and the walk continues. A record
naming the argument answers true only when `curtime <= record.expiry` AND the target has not moved
more than `_DAT_10499560 = 14400` — 120 units, squared — from where it was recorded; otherwise the
record is removed and the answer is false. Falling off the end answers false.

The removal is `FUN_10430fa0`, a memmove of the LAST record over the one being dropped, followed by a
decrement of the count — and the loop index then decrements PAST the record that was just moved in,
so a record shifted down from the tail is never examined on the same pass. That is retail's own sweep
and it is reproduced rather than corrected.

**Unrecovered:** what ADDS a record. No writer of `+0x5d48` is in layers 0–9, so the cache is only
ever read and compacted by this body.

## `IsValidCover`, `IsValidShootPosition` and `IsAreaClear` — `0x1028af20`, `0x1028b0b0`, `0x102a0fb0`

_Recovered 2026-09-13, story 29c-1._

`IsValidCover` (slot 548) was read from the listing, because the decompiler mis-assigned both of its
stack arguments: `ESP+0x5f` is `trace_t+0x37` (`startsolid`) and `ESP+0xbc` is the second argument,
the `CAI_Hint*`.

```text
end = ( cover.x, cover.y, cover.z - NAI_Hull::Mins(m_eHull).z + 0.01 );   // _DAT_1044e658, a double
Ray_t::Init( cover, end, m_Collision.OBBMins(), m_Collision.OBBMaxs(), true, 0 );
enginetrace->TraceRay( ray, 0x202400b, CTraceFilterSimple(this, 0), &tr );
if (tr.startsolid) return false;
if (m_strHintGroup && (!pHint || pHint->m_strGroup (+0x5f0) != m_strHintGroup)) return false;
return true;
```

The trace's end is barely below its start — the hull's own `mins.z` plus a hundredth of a unit — so
it is a STANDING hull test at the spot rather than a drop test. `IsValidShootPosition` (slot 549) is
36 bytes: the hint-group half alone, with no trace and with its position argument never read, so an
NPC that has been given no hint group accepts every shoot position.

`CAI_BaseNPCTroika::IsAreaClear` (`0x102a0fb0`) traces the NPC's own collision hull from a position
to ITSELF — start and end are the same point — with `m_bForceNPCCheck` (`+0x63da`) raised for exactly
the duration of the call, and answers true only on `fraction >= 1.0` with neither `allsolid` nor
`startsolid`. The flag is the whole reason the body is not just a trace call: it is what makes the
move probe count other NPCs as blockers for this one query and for no other.

The hull table `PTR_DAT_1060a750` all three reach (`FUN_102d6100` is `table[hull] + 8`, the mins;
`FUN_102d6120` is `+0x14`, the maxs; `FUN_102d61b0` is `maxs.y - mins.y`, the width) points at
records that live past `.data`'s raw size in the pinned image: they are filled at runtime and are not
readable from the file.

**Unrecovered:** the hull table's contents, and therefore every number derived from `m_eHull`
(`+0x1568`).

## `EnemyCouldSeeHull` — `0x10366510`, `0x103da230`

_Recovered 2026-09-13, story 29c-1, from the listing._

Slot 617 on the boss branch. `signatures.tsv` gives the signature as
`bool EnemyCouldSeeHull(Vector, bool, bool, Vector)` and the listing's eight stack arguments confirm
it: an origin, a skip-the-view-cone flag, a use-the-hitbox flag and a Vector of extents. The
decompiler lost all eight.

```text
e = GetEnemy();                          if (!e) return false;          // slot 167
enemy = e->m_pCombatCharacter (+0x9c);   if (!enemy) return false;      // a non-character cannot look
eye = enemy->EyePosition();                                            // slot 193 — the trace START

if (useHitbox && GetSeqDesc(m_nSequence (+0x6f0)))  ComputeHitboxSurroundingBox(&mins, &maxs);
else { mins = origin + NAI_Hull::Mins(m_eHull);  maxs = origin + NAI_Hull::Maxs(m_eHull); }

mins -= extents;  maxs += extents;
mid = (mins + maxs) * 0.5;  mid.z = RandomFloat( mins.z, maxs.z );     // _DAT_104454d0

filter = CTraceFilterSimple(0);  filter.AddIgnore(this);  filter.AddIgnore(enemy);
for (p in { mid, mins, maxs })
    if (skipViewCone || enemy->FInViewCone(p))                         // slot 362, vtable +0x5a8
        if (TraceRay(eye -> p, mask 0x4081) is clear)  return true;    // fraction >= 1, no solids
return false;
```

The three candidates are the inflated box's CENTRE with a randomized height and then its two opposite
CORNERS — not a bottom/mid/top triple. The random draw is taken unconditionally, before any candidate
is tested. `skipViewCone` skips only the cone; the trace still runs. Mask `0x4081` is
`CONTENTS_SOLID | CONTENTS_OPAQUE | CONTENTS_MOVEABLE` — no character bit at all, so another body
standing in the way does not break the line and the filter's two ignores are belt and braces.

`CNPC_VWerewolf::EnemyCouldSeeHull` (`0x103da230`) puts two gates in front of it and forwards every
argument: a `ConVar` at `DAT_1093d694` read as `!IsCommand() && m_nValue`, and then — only when there
IS an enemy — that enemy's own vtable `+0x278` (slot 158). A Werewolf with no enemy skips the second
gate and reaches the base body, which then refuses for want of an enemy anyway.

Its one recovered caller is `UpdateConditionCanTeleport` (below), which passes the extents
`DAT_1070d1b0/b4/b8` — `vec3_origin`, three zeros written by `staticinit_0x101370b0` and read by 369
bodies. The box is therefore not inflated at all on that path.

**Unrecovered:** the `DAT_1093d694` cvar's name and default (uninitialised `.data`, no constructor in
the corpus), and what slot 158 on the enemy asks.

## Slot 563 `TranslateEnemyChasePosition`, eight bodies — `0x10289f20`, `0x10295300`, `0x1035f5c0`, `0x10368ee0`, `0x10384760`, `0x10392c40`, `0x103ba640`, `0x103d9e00`

_Recovered 2026-09-13, story 29c-1._

The signature is `void TranslateEnemyChasePosition(CBaseEntity* pEnemy, Vector& chasePosition,
float& tolerance, float* scratch)`; the decompiler shifts the arguments on every one of the eight and
the listings settle them (`[esp0+4]` is the enemy, `+8` the position, `+0xc` the tolerance, `+0x10`
the scratch). All eight share one gate, `thunk_FUN_1027d990(this) == 2` — the navigator's type word
at `CAI_Navigator+0x18`, where 2 is FLY — and one effect on that arm: the chase position is offset by
`pEnemy->EyePosition() (slot 193) - pEnemy->GetAbsOrigin()`, so a flying chaser aims at the enemy's
eye rather than its feet.

What differs is entirely what happens on the OTHER arm, and there are six shapes:

* `CAI_BaseNPC` (`0x10289f20`) writes `NAI_Hull::Width(m_eHull)` into the tolerance on the nav arm and
  zero into it on the else arm. It is the only one of the eight whose else arm writes anything.
* `CAI_BaseNPCTroika` (`0x10295300`) is byte-identical but for that else arm, which it does not have.
  This is the body every spawnable species dispatches to.
* `CNPC_VAnimal` (`0x1035f5c0`, with `CNPC_VDog` and `CNPC_VRat`) and `CNPC_VHuman` (`0x10384760`,
  covering 42 classes) take the offset and never touch the tolerance at all.
* `CNPC_VCamera` (`0x10368ee0`) is three bytes: a bare return, all four arguments ignored.
* `CNPC_VMingXiao` (`0x10392c40`) and `CNPC_VTzimisce` (`0x103ba640`) are byte-identical to each
  other. Off the nav arm they write `m_flGoalTolerance` (`+0x6320`) into the SCRATCH, run
  `thunk_FUN_102c3b50` over it, copy the scratch into the tolerance, and then run
  `thunk_FUN_102c36d0` with `GetLocalVelocity()` (slot 220) and `m_flGroundSpeed` (`+0x0654`) to move
  the chase position ahead of a running enemy.
* `CNPC_VWerewolf` (`0x103d9e00`) is that with a `ConVar` (`DAT_1093d52c`) added to the tolerance and
  WITHOUT the second helper — it takes the tolerance lead but not the position lead.

**Unrecovered:** `thunk_FUN_102c3b50` and `thunk_FUN_102c36d0` themselves (neither is in layers 0–9),
and the `DAT_1093d52c` cvar's name and default.

## The Werewolf teleport pair and its condition — `0x103d4a60`, `0x103d4d60`, `0x103cc0d0`, `0x103b0560`

_Recovered 2026-09-13, story 29c-1._

`TeleportOut` (`0x103d4a60`) and `TeleportIn` (`0x103d4d60`) are mirrors, and the ORDER inside them
is the recovered fact: the effects bit and the solid flag are written before the relink, so the
engine sees a non-solid invisible body on the same frame.

```text
TeleportOut:  m_flTimeTeleportedOut (+0x66f0) = curtime
              Hide()                                   // slot 66, vtable +0x108
              m_fEffects (+0x019c) |= 0x20             // EF_NODRAW
              m_Collision.AddSolidFlags( 0x4 )         // FSOLID_NOT_SOLID, +0x02b4
              Relink()
              (+0x66ac) = 0;  ClearTeleportHint();  (+0x66e8) = 0
              m_OnTeleportOut.FireOutput( GetEnemy(), this, 0 )
              [gated] EmitSound( CSingleUserRecipientFilter(enemy), "dev/ww_tele_out.wav", 1.0, 100 )

TeleportIn:   PositionAtHint( m_pTeleportHint (+0x66b0) )
              Unhide()                                 // slot 67, vtable +0x10c
              m_fEffects &= ~0x20;  RemoveSolidFlags( 0x4 );  Relink()
              (+0x66ec) = curtime
              m_OnTeleportIn.FireOutput( GetEnemy(), this, 0 )
              [gated] EmitSound( ..., "dev/ww_tele_in.wav", ... )
```

Two asymmetries are worth naming. `TeleportIn` does NOT clear the hint on the way in, and the stamp
it writes is `+0x66ec` — the same word `UpdateConditionCanTeleport` stamps when the enemy could see
the hull — so arriving counts as "just been seen" and the teleport cooldown starts over. The sound's
recipient filter is built on the ENEMY's entity index, not the werewolf's: it is a dev cue played at
whatever the wolf is fighting, which in single player is the player.

`UpdateConditionCanTeleport` (`0x103cc0d0`) is written the other way round from 29c's one-line walk.
The condition is CLEARED at the top of every pass and SET only at the end:

```text
ClearCondition( 0x77 );                                      // 0x10269b50
if (!IsViewable()) return;                                   // slot 163, vtable +0x28c
close = m_flPlayerDist (+0x6264) < 800.0;                    // _DAT_10457ac4
GetBonePosition( "Bip01", &bone, &ang );
if (IsViewable() && EnemyCouldSeeHull( bone, close, true, vec3_origin ))   // slot 617
    (+0x66ec) = curtime;
since = curtime - (+0x66ec);  if (since < 0) since = 0;
if (ConVar(DAT_1093d414).GetFloat() < since &&
    (+0x66d0) + (+0x66cc) + 100.0 < m_flPlayerDist)          // _DAT_10450564
{
    ConVar(DAT_10924a6c).Get();                              // result discarded
    SetCondition( 0x77 );                                    // 0x10269a20
}
```

So the wolf may teleport when it has been out of its enemy's sight for long enough AND the player is
further away than its two stored distance terms plus a hundred units. `IsViewable` is asked twice —
once as the entry gate and again immediately before the sight test — which is retail's own redundancy.

`CNPC_VSheriffMan::KillTeleportBats` (`0x103b0560`) is the small one beside them: resolve
`m_hTeleportSwarm` (`+0x66d0`), `UTIL_Remove` it when it is live, and invalidate the handle whether or
not it resolved — which is what makes it idempotent.

**Unrecovered:** what `+0x66cc` and `+0x66d0` on `CNPC_VWerewolf` hold (two float distance terms with
no writer in layers 0–9) and what `+0x66ac` is; the `DAT_1093d414`, `DAT_1093f73c` and `DAT_10924a6c`
cvars' names and defaults; and the human-readable name of condition `0x77`. The `DAT_1093f73c` gate
is the one `lifecycle.md` already records as unrecovered.

## `GetNearestNodeToPlayer` — `0x103d0bf0`

_Recovered 2026-09-13, story 29c-1._

A cache in front of the navigator's nearest-node query, with two details that matter. The cache is
keyed on ZERO, not on `-1`: `+0x6704` is reset to 0 once `+0x6700 + _DAT_10450aa4` (0.01 s) is past,
and a zero is what triggers the re-query — so a map whose node 0 genuinely is nearest re-queries every
interval. And the refresh stamp is written on the way out whether the query succeeded or not, so a
permanent failure still waits the full interval between attempts.

The query itself writes two scratch words into the navigator first (`nav+8` from the owner's
`+0x156c`, `nav+0xc` from `gpGlobals->frametime`), then calls `thunk_FUN_102f41b0` on the node
network at `nav+0x2c` with the player's `GetLocalOrigin` (slot 220). `-1` takes the
`DevWarning("GetNearestNodeToPlayer failed")` arm; an index outside the network's bounds bumps a
counter at `DAT_106c994c` and leaves the cache zero; anything else stores `network->nodes[index]`.

**Unrecovered:** what `owner+0x156c` is and why the query needs it.

## `FUN_102c5570`, which is not `IsNearShootTarget` — `0x102c5570`

_Recovered 2026-09-13, story 29c-1._

29c's overlay names this row `IsNearShootTarget`. The listing does not support the name: the body
returns a FLOAT in `ST0`, it has zero callers anywhere in the image, and its tail multiplies by two
fields of a pointer argument. What it computes is a falloff:

```text
float f( void* p, float value )                        // __thiscall on the NPC, RET 8
{
    float scale = 1.0f;                                             // _DAT_104454c0
    if (p->f26c > 0.0f) {                                           // _DAT_104454c4
        Vector d;
        if (m_hShootTargetOverride (+0x5ba8) resolves)
            d = override->GetAbsOrigin() - GetAbsOrigin();
        else if (GetEnemy())                                        // slot 167
            d = GetEnemies()->GetLastKnownPosition(GetEnemy()) - GetAbsOrigin();  // slot 541
        else { scale = sqrt(1.0f / p->f26c); goto tail; }
        float dist = d.Length();
        scale = (dist > 0.0f) ? sqrt(dist / p->f26c) : dist;
    }
tail:
    return (value - p->f260) * scale;
}
```

The override wins outright — the enemy is not even asked for — which is why a schedule that pins a
shoot target keeps aiming at it after the enemy moves.

**Unrecovered:** the CLASS of the pointer argument. `+0x260` and `+0x26c` are `m_pMoveChild` (an
`EHANDLE`) and `m_iName` (a `string_t`) on a `CBaseEntity`, so it is not an entity, and with no
caller in the image nothing states what it is. The retail NAME of the function is unrecovered too;
there is no `IsNearShootTarget` string anywhere in the image.

## `CNPC_VTzimisce`'s aim origin — `0x103bfd80`

_Recovered 2026-09-13, story 29c-1._

Slot 389 `Weapon_ShootPosition` for `CNPC_VTzimisce`. Two activities get an offset aim origin built
out of the body basis and everything else falls through to
`CBaseCombatCharacter::Weapon_ShootPosition`:

```text
AngleVectors( GetAbsAngles(), &forward, &right, &up );        // vtable +0x374 (slot 221), 0x10139610
F = ConVar(DAT_1093cc3c).GetFloat();  R = ConVar(DAT_1093cbf4);  Up = ConVar(DAT_1093cbac);
m_Activity (+0x0fec) == 0x106:  out = src + forward*F - right*R + up*Up
m_Activity           == 0x107:  out = src + forward*F + right*R + up*Up
```

The two arms differ in ONE sign — the right term — and in nothing else. Retail spells the second as
two statements (the sum without the up term, then `Vector::operator+` at `0x1011e060` with it) and
the first as one expression; the split is the compiler's and the result is the same.

**Unrecovered:** all three cvars' names and defaults. They live in uninitialised `.data` and no
corpus function constructs them, which is the same finding the Facing family recorded for the
facing-target gate. With all three unreadable, retail's own `ConVar::GetFloat()` answers `0.0f`, and
both arms then answer `src` exactly as the base body does.
## `CNPC_VHengeyokai`'s pickup chain — `0x10381e90`, `0x103822a0`, `0x10382670`, `0x10382400`

_Recovered 2026-09-13, story 29c-1._

The Hengeyokai grabs a body, carries it on its right hand and throws it. Four bodies, and their
words are its own datamap's: `m_hPickupTarget` `+0x6664`, `m_iPickupTargetGrabBone` `+0x6668`,
`m_vecPickupTargetPos` `+0x6684`, `m_hPhysicsAnimlink` `+0x6690` and the SafeDisc secure int
`m_SecurePickupParam` `+0x6698` (encoded word `+0x66a0`).

The **acquire** is `0x10381cd0`, which is nobody's row: it walks every entity named `"fish"`
(`DAT_1063f484`) within 1024 units, skips any the blacklist still holds, requires a zero velocity
(slot 199 against `vec3_origin`), keeps the nearest, stores its handle in `m_hPickupTarget` and
hands it to **`0x10381e90`**. That body RTTI-casts the target and walks a FIXED two-name bone table
(`PTR_s_Bone01_1063bd88`: `"Bone01"`, `"Bone04"`, terminated by the shared empty string — the loop's
condition is the first CHARACTER of the next row, not a null pointer), asking the cast object for
the element at each name (`+0x424`) and that element's world position (`+0x94`). It keeps the bone
nearest this NPC's own `GetOrigin()` (slot 220, re-read per bone) inside **1025 units** (the initial
best is the square, `1050625.0`), writing the position to `m_vecPickupTargetPos` and the table INDEX
to `m_iPickupTargetGrabBone`. A target that does not cast takes a separate arm entirely: the
target's own origin, bone 0, and `true` unconditionally.

**`0x103822a0`** is the facing gate: with a live `m_hPickupTarget`, `UTIL_AngleDiff` of
`UTIL_VecToYaw(target - me)` against `GetAngles().y` (slot 221) must land inside
**`[-20, +20]` degrees** (`_DAT_1049ae98` = −20.0, `_DAT_1044eb0c` = 20.0 — the same pair the kick
`0x102b6890` clamps against). Both early-outs — a null argument and an unresolvable
`m_hPickupTarget` — answer `MOV AL, 1`, so the gate is PERMISSIVE when there is nothing to face.
Three callers: `SelectSchedule`, `StartTask` and `RunTask`.

**`0x10382670`** makes the carry. It creates a `phys_animlink` at `vec3_origin`, scans the model's
bone table for `"Bip01 R Hand"`, RTTI-casts the carried thing to `CRagdollProp`, latches it held
(`0x10157890`), decodes `m_SecurePickupParam` and hands the decoded word BOTH to the ragdoll's
`+0x2fc` (as a float) and to its `+0x424` element lookup, wires the link (`0x1014f210`), stores the
link handle in `m_hPhysicsAnimlink`, clears `FINDING_BODY` (`0x10381ba0`) and sets `CARRYING_BODY`
through `0x10381c00` — whose true arm also stamps `m_flFishTimer` `+0x666c` with
`curtime + RandomFloat(5, 8)` and clears `m_bDidFakeThrow` `+0x667d`. Its one caller is
`HandleAnimEvent`.

**`0x10382400`** is the throw, from the same `HandleAnimEvent`. It removes the link entity and
clears `m_hPhysicsAnimlink`, then, with an argument AND a live `m_hPickupTarget`, solves a ballistic
impulse (`0x102c4cc0`) from the carried thing's origin onto a point **48 units** (`_DAT_10447ee8`)
above the argument's origin and applies it — through `CRagdollProp`'s `+0x428` when the carried
thing casts, else through its `IPhysicsObject` at `+0x36c` (`GetPosition` `+0xa0` then
`ApplyForceCenter` `+0x9c`). It then clears `m_hPickupTarget`, re-arms the collision-ignore expiry
at **0.75 s** (`0x102c43b0`) and clears `CARRYING_BODY`. Note the order: the handle is cleared
BEFORE the re-arm, and unlike the ManBat twin it never calls `StartIgnoringCollision` at all.

The bone-name table, the `"fish"` classname and the `.rdata` floats above were read out of the
pinned image; the secure int is carried DECODED in the port, which is a named modernization
(the obfuscation is copy protection and the decoded word is the whole observable state).

**Unrecovered:** what `m_SecurePickupParam` actually holds — its only writer in layers 0–9 is the
constructor `0x1037e680` and nothing states the value, so whether it is a bodygroup, a ragdoll
element index or a damage scalar is not settled by the two consumers. The `0x102c4cc0` solve's three
cells `_DAT_1044f030`, `_DAT_10450aa0` and `_DAT_1049a1c8` live past `.data`'s raw size.

## The expiring entity blacklists — `0x10382970`, `0x10382aa0`, `0x10382b30`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VHengeyokai` repeats `CNPC_VBaseBoss`'s `m_BlacklistedEntities` at `+0x66a4` (allocation count
`+0x66a8`, grow size `+0x66ac`, size `+0x66b0`, element pointer `+0x66b4`) and its three bodies are
byte-for-byte the boss's `0x103662d0` / `0x10366400` / `0x10366490` moved to that offset.

**`0x10382970`** appends `{EHANDLE, curtime + 20.0}` (`_DAT_1044eb0c`) — the same 20 s MingXiao
blacklists a thrown object for. The `CUtlVector` growth around it (4 on an empty store, then
doubling, then by the grow size) and its `memmove` are bookkeeping: the move's length is
`((size + 1) - old) - 1`, which is ZERO on every path, so the operation is a plain append. It does
not de-duplicate: a second add of the same entity is a second row.

**`0x10382b30`** is the index-of: it RESOLVES each stored handle and compares the POINTER, so a row
whose entity has died compares equal to a null candidate. **`0x10382aa0`** is the test: not found is
false; found and `curtime < expiry` is TRUE; found and expired SWAP-REMOVES the row with the last
one and answers false. The swap is a `memmove` of 8 bytes from `base + (size - 1) * 8`, so the store
is unordered after the first expiry, and the `size > 0` guard in front of it is dead.

**Unrecovered:** nothing.

## Slot 482 `CanPlaySequence`, the species half — `0x103850a0`, `0x10396e90`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VAndreiBlood`'s `0x103850a0` (which fills the slot for 42 classes of the same human line:
`CNPC_ProneDialog`, `CNPC_VAsianVampire`, `CNPC_VBach`, …) and `CNPC_VMingXiao`'s `0x10396e90` are
byte-identical to each other, to `CNPC_VAnimal`'s `0x1035fd40` and to `CNPC_VTzimisce`'s
`0x103bd270`. **They are NOT identical to the base `0x10278090`**, and the one instruction that
differs is the reason the four exist at all.

```text
result = 1;
if (m_hCine resolves live) { if (!CineAllowsInterrupt(m_hCine)) return 0;  result = 2; }  // 0x101a8ac0
if (!IsAlive()) return 0;                                                                // slot 158
if (fDisregardState == 0 && m_NPCState != 0 && m_NPCState != 1
    && m_IdealNPCState != 1 && !(m_NPCState == 3 && interruptLevel >= 1))
    return ((m_NPCState != 4) - 1) & result;     // the species four
    // return 0;                                 // the base 0x10278090
return result;
```

The species tail is a branchless mask, spelled at `0x10385159` as
`XOR ECX,ECX / CMP EAX,4 / SETNZ CL / DEC ECX / AND ECX,EDI`: 0 for every state that is not 4, and
`result` for state 4. The base reaches the same gate and answers a flat `XOR EAX, EAX`
(`0x102780e8`). So **a body in retail state 4 keeps its 1-or-2 under the four species bodies where
the base would have refused**, and nothing else about them differs.

Retail state 4 is `NPC_STATE_SCRIPT`. This image's `NPC_STATE` ordinals are **not** SDK 2013's —
[`conditions-and-states.md`](./conditions-and-states.md)
§ "The species `SelectIdealState` overrides — `0x10369060`, `0x103945a0`, `0x1039e310` (2026-09-13)"
pins them off `0x1027e660`'s name table and
`0x1026e3e0`'s switch as **1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD** — which is why the `== 3` escape is
ALERT (and needs `interruptLevel >= 1`), the `== 4` survivor is SCRIPT, and COMBAT falls to the mask
with the rest. Reading these ordinals as SDK 2013's would invert both arms.

**Unrecovered:** nothing in the body. Why a scripted body is allowed to keep a yes on these four
species and not on the base is not a question the image answers.

## The `CNPC_VAndreiBlood` line's melee-slot bodies — `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70`

_Recovered 2026-09-13, story 29c-1._

Slots 599–602 are the Troika line's melee-slot acquire/release pair over the global attack
coordinator `m_pAttackCoordinator` `+0x65e8`, and the `CNPC_VAndreiBlood` human line (38 classes)
carries its own four. Three are **identical** to the Troika bodies and one is not:

- `0x10385ab0` (slot 599) is `CAI_BaseNPCTroika::0x102b5650` — the melee-entry decision from the
  frenzy flag `+0x5b84 & 2`, slot 293 `GetFollowerBoss`, `m_flMeleeCanEnterTimer` `+0x6070`, the
  doubled `DAT_10924a1c` melee range against `m_flEnemyDist` `+0x6268`, `m_flEnemyHeightDiff`
  `+0x626c` against `_DAT_10451acc` (64.0), condition `0x59`, and the squad frenzy bit `0x1000`.
  On entry it sets `m_bInMelee`, arms `m_flMeleeMustLeaveTimer` `+0x6074` with
  `curtime + RandomFloat(7.5, 15.0)` (`0x40f00000`, `0x41700000`) and fires the global melee event
  `DAT_10924edc+4`.
- `0x10385c30` (slot 600) is `0x102b57c0`.
- `0x10385d70` (slot 602) is `0x102b5900` **minus one short-circuit**: the Troika body's early-out
  reads `flag || GetFollowerBoss() || m_pAttackCoordinator == 0`, and this one drops the third test.
  Every later use of `m_pAttackCoordinator` in the body — `0x1025db50` and `0x1025de90` — is
  therefore unguarded on this line. That asymmetry is the recovered fact; nothing in the image
  suggests it was intended.
- `0x10385cf0` (slot 601) differs from the Troika body `0x102b5880` in TWO ways, both ported: it
  fires the global melee event `DAT_10924edc+4` FIRST (the Troika body fires nothing), and its
  forward into `0x1025ddd0` carries no `m_pAttackCoordinator != 0` guard. Otherwise it is the same:
  clear `m_bInMelee`, and when slot 308 `HasUsableRangedWeapon` answers true stamp
  `m_flMeleeCanEnterTimer` with `curtime + RandomFloat(5.0, 10.0)`.

**Unrecovered:** what `DAT_10924edc` is — the global whose `+0x4` all three of 599, 600 and 601 fire
is constructed nowhere the corpus holds, so only its call SHAPE (one argument-less dispatch) is
recovered, not its identity. `DAT_10924a1c`'s cvar name and default are unreadable for the same
reason.

## `CNPC_VManBat`'s flight velocity — `0x1038b370`, `0x1038bec0`, and the four flap timers

_Recovered 2026-09-13, story 29c-1._

`0x1038b370` (2,274 bytes, the largest body of story 29c-1's Bosses family) is the Sheriff's bat
form asking "what velocity do I want this frame". Its three shapes are selected by `m_pFlyNode`
`+0x6688` and by `m_iMoveGoalNodeMode` `+0x6668` — a `MvsnSec::CSecureType<int>` whose encoded word
`+0x6670` every read runs back through `0x103908c0` and `0x1042fbf0`. **The two constants it
compares against, `0xfa0b069a` and `0xfa0b069b`, decode to 6 and 7**, and the mode it forces around
its hint search (`0x1042fb50(2)`) is 2; the obfuscation is SafeDisc copy protection over a plain
small integer.

- **No fly node and a mode that is neither 6 nor 7.** `m_vecAbsVelocity` `+0x3bc`'s X and Y, and a Z
  SEEDED WITH THE LITERAL `10.0` rather than the velocity's own. That triple's length is the speed
  handed to the obstacle probe, and its normalized direction the probe's direction; when the probe
  reports a hit, the answer is the probe's steer times that speed.
- **Four activities answer a dead stop** before anything else runs: `0x30`, `0xb0`, `0x4b`, `0x1171`.
- **A level-wide stationary watchdog.** `_DAT_1093b8b8..c0` (a position) and `_DAT_1093b8c4` (a
  stamp) are FILE STATICS installed by an `atexit`-registered initializer at the top of the body, so
  there is ONE watchdog for every ManBat in the level, not one per NPC. When this frame's
  `GetOrigin()` equals the stored position and more than **0.5 s** (`_DAT_10449270`, a double) have
  passed, the body forces the mode to 2, draws `RandomInt(1, 3)` into `m_iMoveGoalNodeID` `+0x6674`,
  runs `0x102d1af0(20000, 0, 15000.0)` for a hint, restores BOTH words, and — with a hint — places
  `sheriff_teleport_emitter` at its own origin and then at the hint's before teleporting itself to
  **its own saved origin**, not the hint's. A null hint zeroes the output velocity and returns.
- **The destination**, by mode: 6 is `m_hClosestPlayer` `+0x628c`'s origin plus **150** units
  (`_DAT_1046eca8`, a double); 7 is `m_hFlyByTarget` `+0x66ac`'s origin plus its own box height plus
  **1.0** (`_DAT_10449280`), gated on slot 158 `IsAlive` and on a navigator reachability probe
  (`0x102f1a20`, mask `0x2400b`) whose refusal is `TaskFail(0x1a)` and the plain-velocity fallback;
  anything else is `m_pFlyNode`'s origin.
- **The homing arm.** Speed is **700** units, or **500** when the raw delta Z is at or above **−30**
  (`_DAT_10462868`) — measured BEFORE normalization. The obstacle probe gets first refusal; when it
  reports clear, the wanted velocity is clamped toward the current one (slot 198
  `GetLocalVelocity`) by `cvar(DAT_1093b7cc) * interval` per axis, with Z clamped UP at that limit
  and DOWN at **three times** it (`_DAT_10450010`). Finally, with a non-zero interval, a remaining
  distance under **0.2** (`_DAT_10449198`) of the produced velocity's length latches
  `m_bReachedMoveGoal` `+0x6664`.

**`0x1038bec0`** is the obstacle probe: a hull sweep of this NPC's own collision box from
`GetAbsOrigin()` along `direction * speed * 0.1` (`_DAT_104491b4`) with mask `0x202400b`. A fraction
under **1.0** answers a LITERAL straight-up steer `(0, 0, 1)` — not a reflection off the hit normal
— and, unless `m_Activity` `+0x0fec` is already `0x22`, stamps `m_flFlapTimer` `+0x6678` with
`curtime` itself rather than a deadline. A clear sweep answers `vec3_origin` and false.

The four 32/35-byte bodies beside them are one behaviour written four times —
`SetIdealActivity(act)` then `m_flFlapTimer = curtime + T`: `0x1038e640` (`0x22`, **2.3 s**,
`_DAT_104bc690`), `0x1038e670` (`0x24`, **4.0 s**, `_DAT_10449148`), `0x1038e6a0` (`0x116d`,
**0.2 s**, `_DAT_10449198`) and `0x1038e6e0` (`0x116e`, the SAME 0.2 s cell). All four durations are
doubles in `.rdata`.

**Unrecovered:** `DAT_1093b7cc`'s cvar name and default (it lives past `.data`'s raw size, so
retail's own `GetFloat()` answers 0.0 for an unconstructed cvar and the clamp collapses); what the
ten `m_iMoveGoalNodeMode` values other than 2, 6 and 7 mean; and the `m_iMoveGoalNodeID` the search
draws, whose only consumer is the hint search itself.

## `CNPC_VManBat`'s carry link — `0x1038f430`, `0x1038f790`

_Recovered 2026-09-13, story 29c-1._

The same acquire/throw shape as the Hengeyokai pair above, on a different bone and with a different
aim. `0x1038f430` is reached from `ThrowModel` `0x1038f2c0` (the body that spawns the prop) and
`0x1038f790` from `RunTask`.

**`0x1038f430`** creates a `phys_animlink`, scans the bone table for `"Bip01_R_Foot"`, asks the
carried thing's `+0x424` for the element at its `param_2` (no RTTI cast and no decode, unlike the
Hengeyokai arm), wires the link, stores it at `m_hPhysicsAnimlink` `+0x6690`, sets `CARRYING_BODY`
(`0x1038f600`), records the carried thing's breakable latch into `m_bPickupTargetBreakable`
`+0x6694` and clears that latch on the prop.

**`0x1038f790`** removes the link and clears the handle, then aims not at an argument but at
`m_hClosestPlayer` `+0x628c`, solving the same `0x102c4cc0` impulse onto a point 48 units above it
and applying it the same two ways. On the ragdoll arm it RESTORES `m_bPickupTargetBreakable` onto the
thrown prop. It then calls `StartIgnoringCollision(m_hPickupTarget)` (`0x102c4380`, which parks the
expiry at `FLT_MAX`), re-arms that expiry at **2.0 s**, and only THEN clears `m_hPickupTarget`
`+0x668c` and `CARRYING_BODY`. The Hengeyokai twin clears its handle before the re-arm and never
calls `StartIgnoringCollision`; the ordering difference is what makes the ManBat's ignore resolve a
live entity and the Hengeyokai's not exist.

**Unrecovered:** the same three `0x102c4cc0` cells as the Hengeyokai section, and what
`0x101578b0` / `0x101578d0` read and write — the pair is used only as a save/restore around a throw,
so only the round trip is recovered, not the word.

## `CTraceFilterManBatNoIBeamEntity` — `0x1038fb20`

_Recovered 2026-09-13, story 29c-1._

`CNPC_VManBat`'s slot 102 `Physics_TraceEntity`. It repeats the Troika-line body `0x100ab450` — take
the entity's collideable (`+0x8`), read its collision group (`+0x38`), build a `CTraceFilterSimple`
(`0x101ccf50`), set up the ray (`+0x24`) and sweep it through `enginetrace`'s `+0x14` — with one
change, which is the whole reason the override exists: **the freshly built filter's vtable pointer
is overwritten with `vftable_CTraceFilterManBatNoIBeamEntity`** (`0x104bc6b0`) before the sweep.

That vtable has two entries, read out of the image. `GetTraceType` (`0x100017ee`) is `return 0`.
`ShouldHitEntity` (`0x10006c4e`) matches the candidate's `m_iName` `+0x26c` against the literal
`"lbeam*"` (`DAT_10642d28`) with retail's inlined `NameMatches`, and an entity that MATCHES is NOT
hit; anything else falls through to `CTraceFilterSimple::ShouldHitEntity`. `NameMatches` reads
`strlen(pattern) + 1`: exactly 1 (an empty pattern) answers `m_iName == NULL_STRING`; a pattern
ending in `*` is a case-insensitive prefix compare of the characters before it; anything else is a
whole-string case-insensitive compare. So the bat's sweeps pass straight through every I-beam in the
level.

**Unrecovered:** nothing. The string, the vtable and both slot bodies were read from the image.

## `CNPC_VMingXiao`'s tentacle rules — `0x10396dc0`, `0x10397a50`, `0x10397f70`, `0x10398030`, `0x103983d0`

_Recovered 2026-09-13, story 29c-1._

MingXiao's six tentacles are proxies: `m_rhProxies[6]` `+0x668c`, `m_rbProxyRegistered[6]` `+0x6684`,
`m_rhSeveredTentacles[6]` `+0x66a8`, `m_rflAttackTimers[6]` `+0x66c4`, `m_rflHitPoints[6]` `+0x66dc`,
`m_rflRegrowTimers[6]` `+0x66f4`, the count `m_iConnectedTentacleCount` `+0x670c` and the bit set
`m_iSeveredTentacleMask` `+0x6710`. Two one-line helpers gate everything else:
`0x10398000` is `(m_iSeveredTentacleMask & (1 << n)) == 0` and `0x10398870` is
`m_iTentacleID (+0x6674) != -1`, which distinguishes a proxy from the head.

**`0x10396dc0`** is `SelectSchedule`'s grabbed-object arm: with a live `m_hThrowObject` `+0x6718` and
`m_eThrowableObjectMode` `+0x673c` STRICTLY inside `(2, 5)` — modes 3 and 4 only — condition `0x7b`
answers schedule `0x15c` and, failing that, condition `0x7c` answers `0x15d`. Anything else answers
0. It also writes its own `__FILE__` / `__LINE__` (lines `0xbe1` and `0xbe5`) into the selector trace
at `+0x1b30` / `+0x1b34`, which nothing reads.

**`0x10397a50`** runs from `Event_Killed`. It stamps `m_flProxyReadyTimer` `+0x66a4` with
`curtime + max(Tuning[100] + Tuning[0x68] * (6 − count), 0)` and, when `m_rhProxies[id]` still
resolves to the dead tentacle (the id read off the TENTACLE's own `m_iTentacleID`, not the head's),
severs it through `0x10397930` — which clears the severed and proxy handles and the registered flag,
resets that tentacle's hit points from `Tuning[4]`, arms its attack timer at
`curtime + _DAT_1044e664` and its regrow timer at `curtime + 1.0`, and sets a bodygroup.

**`0x10397f70`**, from `Spawn` and `StartTask`, is the same shape in one line: a proxy answers
`Tuning[0x20]` flat, the head answers `max(Tuning[0x6c] + Tuning[0x70] * (6 − count), 0)`.

**`0x10398030`** is the six-way attack gate `SelectSchedule` and `GatherConditions` share. The mask
gate comes first, then the per-slot `m_rflAttackTimers` deadline, then the switch:

| slot | extra gates | schedule |
|---|---|---|
| 0, 1 | not `m_bBlockedByFriend`; `100 <= dist < 300` | `0x112a`, `0x112b` |
| 2, 3 | not `m_bBlockedByFriend`; `100 <= dist < 200` | `0x112c`, `0x112d` |
| 4, 5 | not `m_bBlockedByFriend`; `dist >= 150`; a live `m_hThrowObject`; `m_eThrowingTentacle (+0x671c) == slot` | none |

`dist` is `m_flClosestPlayerDistance` `+0x6264`. Slots 4 and 5 leave the schedule at −1, which is why
the body's `param_2` tail — the `m_hMeleeWeapon` `+0x667c` owner chain into slot 376
`NPC_TranslateActivity` and slot 331 `ChooseMeleeAttackSequence` — is SKIPPED for them however the
flag was passed. A slot outside 0..5 falls through the switch with the schedule still −1 and answers
TRUE, having passed only the mask and timer gates.

**`0x103983d0`** is `RunTask`'s throw-force curve, keyed on `m_eThrowingTentacle`. **Its decompiled C
is wrong** — the decompiler lost the jump table at `0x10398598` and the ST0 return and read the
selector as a return-storage pointer — so it was read from the listing:

```text
0, 1: scale = dist <= 150 ? Tuning[0x38] : Tuning[0x34];   v = Tuning[0x74] + Tuning[0x78]*(6-count)
2, 3: scale = dist <= 150 ? Tuning[0x40] : Tuning[0x3c];   v = Tuning[0x74] + Tuning[0x78]*(6-count)
      return max(v, 0) * scale;
4, 5: (a, b) = dist <= 200 ? (Tuning[0x84], Tuning[0x88]) : (Tuning[0x7c], Tuning[0x80]);
      return max(a + b*(6-count), 0);                       // NO second factor
else: return 20.0;                                          // _DAT_1044eb0c
```

Slots 0/1 also make a Tuning fetch whose result is discarded before the next instruction when
`dist > 200`; it is dead code.

**Unrecovered:** the tuning record itself. `thunk_FUN_101e8da0(0x10739d08)` is `record + 700` inside
the game-rules singleton at `0x10739d08`, which is filled at runtime and lives past `.data`'s raw
size — so every cell above is a named OFFSET and not a number. `_DAT_1044e664` is in the same
position. Conditions `0x7b` and `0x7c` are above the base condition table `0x00..0x76` and the
registrar that names them has not been decoded.

## `CNPC_VMingXiao`'s pedestal pick — `0x103989b0`, `0x10398b20`

_Recovered 2026-09-13, story 29c-1._

**`0x103989b0`** decides which tentacle takes a target that is abeam of the boss. Its decompiled C
also lost a call — `VectorNormalize` `0x10137220` — so it was read from the listing:

```text
d = target - GetAbsOrigin();
if (fabs(d.z) > 64.0) return false;                 // _DAT_1049ae28, a double
d.z = 0;  VectorNormalize(&d);
f = d.x*m_vecForward.x + d.y*m_vecForward.y;        // +0x6290
if (f < -0.17 || f > 0.5) return false;             // _DAT_104bde64, _DAT_104454d0
r = d.x*m_vecRight.x + d.y*m_vecRight.y;            // +0x629c
if (r <= 0) { if (TentacleConnected(5)) { *out = 5; return true; } }
else        { if (TentacleConnected(4)) { *out = 4; return true; } }
return false;
```

The dot is against a NORMALIZED 2-D direction, so `[-0.17, 0.5]` is an ANGLE band — roughly 60° to
100° off the facing, an abeam wedge — and not a distance. The `r == 0` edge takes tentacle 5, and a
severed tentacle closes its own side without falling through to the other.

**`0x10398b20`**, reached from `0x10396bc0`, is the search around it. It runs only with
`m_flClosestPlayerDistance` at or past **150** units, at least one of tentacles 4 and 5 connected,
and the species cvar `DAT_1093ba8c` reading non-zero. It then walks the entity list in a **257**-unit
sphere, skipping anything `CNPC_VBaseBoss`'s blacklist `0x10366400` still holds, keeping entities
whose `m_iName` starts with `"Pedestal"` (an eight-character case-insensitive prefix) whose velocity
(slot 199) is within **0.1** of zero on X and Y and strictly under it on Z — retail spells the third
compare differently from the first two — and which `0x103989b0` accepts. **The search radius shrinks
to each accepted winner's distance**, so the walk tightens as it goes. The winner's origin goes
through `0x10398890`, which pushes it `_DAT_10463584` along `m_vecForward` and `_DAT_1049ae40` along
`m_vecRight`, ADDING the right term for task 4 and subtracting it otherwise, and copies
`m_vecForward` out beside it.

**Unrecovered:** `DAT_1093ba8c`'s cvar name and default, and both of `0x10398890`'s offsets
(`_DAT_10463584`, `_DAT_1049ae40`) — all three live past `.data`'s raw size. With the cvar
unconstructed retail's own `GetInt()` answers 0 and the whole search never runs, which is what the
port reproduces.

## The scene-event queue — `0x100b5e60`, `0x102c1680`, `0x100b6250`, `0x100b70e0`, `0x100b7040`

`CBaseFlex` keeps its queued choreo events as a `CUtlVector<CSceneEventInfo>` at `+0x0a58`: the heap
block, its capacity at `+0x0a5c`, its grow step at `+0x0a60`, its element count at `+0x0a64`, and a
mirror of the block pointer at `+0x0a68` that `AddSceneEvent` rewrites after every grow and nothing
ever reads back. The record is **0x1c bytes / seven words**: the `CChoreoEvent` at `+0x00`, the
`CChoreoScene` at `+0x04`, an "expressions applied" byte at `+0x08`, a handle seeded `-1` at `+0x0c`,
a resolved sequence at `+0x10`, an animation-clock stamp at `+0x14`, and one word `AddSceneEvent`
never writes. `RemoveSceneEvent` (slot 287) searches by the EVENT at `+0x00`; `ClearSceneEvents`
(slot 285) searches by the SCENE at `+0x04` and, with a null scene, simply zeroes the count.

`CBaseFlex::AddSceneEvent` (`0x100b5e60`, the base-line body of slot 286) refuses a null scene or
event with `Msg("CBaseFlex::AddExpression: scene or event was null!")`. Two of the nineteen event
types arm the record. Type 6 (gesture) looks the event's parameter string up with `LookupSequence`
and, only on a hit, marks the actor busy, warns about a gesture sequence whose `flags & 1` says it
loops, and stamps `+0x14` with `m_flAnimTime` wound BACK by how far into the event the scene already
is — so a gesture queued mid-event enters at the right cycle rather than at zero. Type 7 (sequence)
does the same lookup and stamps `+0x14` with `m_flAnimTime` itself. Every other type queues the
record with `+0x0c` and `+0x10` **uninitialised**, which makes it inert: all three `Process*` bodies
gate on `record[+0x0c] >= 0`.

The queue write is an APPEND. Retail inlines `CUtlVector::InsertBefore(m_Size)`, whose shift count
computes to exactly zero, so the `memmove` at `0x10430fa0` never moves anything on an add; the two
removers are the only compactors. The growth is `CUtlMemory::Grow` inlined: a capacity of 0 becomes
**2**, then a grow step of 0 **doubles** and a non-zero one **adds**, until the capacity covers
`m_Size + 1`; a grow step of `-1` is external memory and the reallocation is skipped entirely.

`CAI_BaseNPCTroika::AddSceneEvent` (`0x102c1680`) is slot 286 itself and dispatches on the event
type before the base ever sees it. Type 2 (expression) goes to `0x102c1a80`. Type 7 (sequence) looks
the parameter up and, **only on a hit**, forces it through slot 311 with the sequence's own activity
on both arguments and clears `m_bInDispositionFidget` / `m_bInStanceChange`; a miss falls out of the
body without reaching the base. Type 0xd (silence) needs a live `m_hDialogPartner`, reads the
parameter as an intensity, requires it to EXCEED the disposition row's threshold (`0x100ec5d0` →
record `+0x108`), rolls `RandomInt(1,100)` against the row's percentage (record `+0x10c`), and on
success installs the sequence `ChangeStance` left in `EAX`, zeroes the cycle, stamps `m_flAnimTime`,
sets both `m_IdealActivity` and `m_Activity` to `0xf1` and raises `m_bInStanceChange`. Type 0xe
(loud) is gated on `m_flLoudExpressionTime` (`+0x6574`) having passed; `0x100ec450` picks one of the
row's authored expression names at random over `record[+0x21c]` from the 0x40-byte table at
`record + 0x11c` and hands back fade-in (`+0x220`), fade-out (`+0x224`), minimum (`+0x228`) and
maximum (`+0x22c`); the event's parameter is clamped into that band, handed to
`AddScriptedExpression(name, fadeIn, fadeOut, 1.0, 0.0, level)`, and the cooldown is rewritten to
`level + curtime + fadeIn + fadeOut + 1e-4`. Type 0xf (python) resolves the partner — substituting
the player when there is none — and calls `CDialogDependency::CallPyDialogFunc` with the event's
SECOND parameter. Everything else forwards to `0x100b5e60`.

`ProcessSceneEvents` (slot 283, `0x100b6250`) **does not walk the queue**. It zeroes every flex
controller through slot 279, re-reading `GetNumFlexControllers()` each iteration, and then tail-jumps
to slot 284 `AddSceneExpressions`, which is what rebuilds the frame's expression. Reset then
accumulate: an expression that stopped this frame leaves no residue.

`ProcessSequenceSceneEvent` (slot 290, `0x100b70e0`) and `ProcessGestureSceneEvent` (slot 291,
`0x100b7040`) share four guards — record, event, scene, `+0x0c >= 0` — and then **compute values and
discard them**. The listing ends every call in both with `FSTP ST0`. The sequence body samples the
event's ramp at the scene's current time (`CChoreoEvent::GetIntensity` `0x10076280`) and drops it.
The gesture body saves `CChoreoEvent::GetDuration` (`0x10076b30`), calls `SequenceDuration` on the
record's sequence and **drops that**, computes
`((m_flAnimTime - record[+0x14]) + 1e-4) / eventDuration`, feeds it to
`GetOriginalPercentageFromPlaybackPercentage` (`0x10077f10`) and drops the answer, then samples the
ramp and drops that too. The divisor is the EVENT's authored length, not the sequence's; 29c's
one-line walk reads it the other way round and the listing settles it.

**Unrecovered:** what the seventh record word (`+0x18`) carries, and why the two `Process*` bodies
keep their computations — no writer for either result appears in the corpus, so this build's scene
processing is effectively the guards plus `AddSceneExpressions`.

## The flex controllers by name — `0x100b5d10`, `0x100b5b60`, `0x100b5c20`, `0x100b5c50`, `0x100b6960`, `0x100b6cf0`

`CBaseFlex` stores 128 floats at `+0x0858`, one per flex controller, and every one of them is
**normalised**: slot 279 (`0x100b5ba0`) divides the written value by the controller's authored range
before storing it, and slot 281 (`0x100b5c50`) multiplies it back out on the way in. The range is
the studio flex-controller descriptor at `studiohdr + studiohdr[+0x164] + index * 0x14`, whose
`+0xc` is `min` and `+0x10` is `max`. Slot 281's decompilation is DAMAGED exactly where the answer
is; the listing gives it as: a negative index, an index past `GetNumFlexControllers()` or a null
model answer **0**, `max != min` answers `(max - min) * m_flexWeight[i] + min`, and `max == min`
answers the stored weight untouched.

`LookupFlexController` (`0x100b5d10`) is a linear `__strcmpi` scan that re-reads
`GetNumFlexControllers()` at the bottom of the loop, and **answers 0 rather than -1** when nothing
matches — the fall-through returns the zeroed counter. A misspelt flex name therefore writes and
reads controller zero rather than being dropped, and both name-addressed slots inherit it: slot 280
(`0x100b5b60`) resolves and dispatches slot 279, slot 282 (`0x100b5c20`) resolves and dispatches
slot 281, two statements each.

`AddFlexAnimation` (slot 289, `0x100b6960`) is the scene-event expression writer. It resolves once
per event, latched on the event itself (`0x10077710` reads the latch, `0x10077730` raises it): for
each flex-animation track it takes the track's name and, when the track is a COMBO, builds two names
by `Q_strncpy`ing `"right_"` and `"left_"` into a 512-byte buffer and appending the track name —
resolving each through `LookupFlexController` and storing the index back on the track. It then reads
the scene's clock, samples the event's ramp (and drops it, as the `Process*` pair do), and walks
every ACTIVE track writing
`SetFlexWeight(j, trackValue * t + (1 - t) * GetFlexWeight(j))` — a LERP of the controller toward the
track's own value by the track's intensity — twice for a combo track, once otherwise, skipping a
negative controller index. The record's `+0x08` byte is raised on the way out.

`AddFlexSetting` (slot 288, `0x100b6cf0`) works over a `flexsettinghdr_t`, i.e. a `.vfe` expression
file: a linear `__strcmpi` scan of the header's setting table (`hdr+0x8c` count, `hdr+0x90` offset,
0x18 stride), an optional preset chain when the found setting's type is 1, an optional OVERRIDE
header lookup by the same name that replaces both the setting and the header it is read from, and
then a per-`(key, weight)` pair blend that maps the key through the header's index table (`hdr+0xa8`)
to a controller number, reads it through slot 281 and writes it back through slot 279.

**Unrecovered:** `AddFlexSetting`'s exact blend. Ghidra scrambles the `__thiscall` argument order —
the same `param_1` is used as the `char*` for the name compare and as the `float` in the blend — so
whether the write is `current + scale * weight` or the SDK's `current * (1 - s) + weight * s` cannot
be settled from this body, and the corpus holds one caller, which passes constants through a thunk.

## Slot 482 `CanPlaySequence`'s Troika-line body — `0x10278090`

The base that `0x103850a0` and `0x10396e90` replace. Its return is `CanPlaySequence_t`, and **1 and
2 are two different yeses**: the answer starts at 1, and a `m_hCine` (`+0x5d74`) that resolves to a
live entity raises it to 2 — after a dynamic-interaction check (`0x101a8ac0`) whose failure returns 0
outright. Then `IsAlive()` (slot 158) must hold, and last the state gate refuses when EVERY term of
`!bDisregardState && m_NPCState != 0 && m_NPCState != 1 && m_IdealNPCState != 1 &&
(m_NPCState != 3 || interruptLevel < 1)` holds — which is SDK's "NONE, IDLE or ideal-IDLE may always
play; ALERT may play when the caller asked by name" written as one refusal.

## The activity commit — `0x10272650`, `0x10272130`, `0x10272400`, `0x10272900`, `0x102bf510`, `0x10272790`

`SetIdealActivity` (`0x10272650`) has two arms and the listing shows both. `ACT_INVALID` (0) **tail
jumps** to slot 310 `SetActivity(0)` without storing anything; every other activity stores
`m_IdealActivity` (`+0x0ff0`) and calls `ResolveActivityToSequence(activity, &m_nIdealSequence,
&m_IdealTranslatedActivity, &m_IdealWeaponActivity)` — it re-resolves the ideal triple and does NOT
commit it.

`ResolveActivityToSequence` (`0x10272130`) is that resolver, an outer `do { … } while (true)` with
exactly three exits. Each pass clears the out-sequence to -1 and translates the request through
`0x10271ff0`, which also fills the weapon activity. `ACT_SCRIPT_CUSTOM_MOVE` (0x18) **with a live
`m_hCine`** looks up the cine's own `m_iszCustomMove` (`cine+0x5f50`) by NAME and returns on a hit;
a miss warns about a custom move with no sequence and falls to `SelectWeightedSequence(9)`.
`ACT_DISPOSITION` (0xf1) **with `m_pBaseNPCTroika` set** — which every spawned NPC has — goes to
`0x10295a80` and, on a miss, warns that the model has no sequence for it. Everything else draws
`SelectWeightedSequence(translated)`, and on a miss emits a warning rate-limited by a
(receiver, activity, time) triple held in three globals; if the TRANSLATED activity was `ACT_RUN`
(0x13) it is rewritten to 9 and drawn again. Anything still unresolved retries the WHOLE request as
`ACT_DISPOSITION`, and a request that already IS `ACT_DISPOSITION` answers **sequence 0** — the
floor.

`ForcePreTranslatedSequenceAndActivity` (slot 311, `0x10272400`) refuses a negative sequence
outright — nothing at all is written. Otherwise it fires slot 465 `OnChangeActivity` **before** the
store and only when `m_Activity` actually changes, then overwrites `m_Activity` (`+0x0fec`) and
`m_IdealActivity` (`+0x0ff0`) with the first argument, `m_IdealWeaponActivity` (`+0x5cd4`),
`m_IdealTranslatedActivity` (`+0x5cd0`) and `m_TranslatedActivity` (`+0x0ff4`) with the second, and
`m_nIdealSequence` (`+0x5ccc`) with the third; it hands the sequence to `0x10260a50` and zeroes
`m_flCycle` (`+0x06f8`) and `m_flPrevAnimTime` (`+0x0170`).

`IsActivityFinished` (slot 251, `0x10272900`) is `m_bSequenceFinished && m_nSequence ==
m_nIdealSequence` — both terms, and the second is what stops a body still blending toward its ideal
from reporting done.

`ShouldMaintainActivity` (slot 466, `0x102bf510`) refuses when `GetCurSchedule()` exists and its id
is `0xb9` (retail returns `EAX & 0xffffff00`, i.e. false), answers true on `m_bForceMaintainActivity`
(`+0x65fa`), and otherwise forwards to `CAI_BaseNPC::ShouldMaintainActivity` (`0x10272790`), which
is `!(GetState() == 4 && m_Activity != 2)` — only a `NPC_STATE_SCRIPT` body on some other activity
gives its activity up.

**Unrecovered:** what activity `2` is in `0x10272790`'s test, and the retail names behind schedule
`0xb9` and activity `0x61` — the registry numbers are the only handles the corpus offers.

## `IdleSequenceGate` and the species animation odds and ends — `0x102b8a10`, `0x10398800`, `0x1032fb80`, `0x10279060`, `0x102b51e0`, `0x102b5220`, `0x102b5260`, `0x103a49c0`, `0x103a4a60`, `0x1025e4e0`, `0x10368ec0`

`0x102b8a10` gates `COND_ENEMY_DEAD` (0x58) and then `SelectWeightedSequence(0x61)`; only when the
body authors a clip for that activity does it stamp its selector trace (`+0x1b30` the source path,
`+0x1b34` line `0x5f20`) and answer schedule **8**. Otherwise 0.

`CNPC_VMingXiao::BodyGroup` (`0x10398800`) has three arms and reads its cvar `DAT_1093bb14` twice
rather than caching it: not a command and a negative value copies `m_iSeveredTentacleMask`
(`+0x6710`) into `m_nBody` (`+0x067c`); a command writes 0; otherwise the cvar's value is the body.

`SetPoseParameter(const char*, float, bool)` (slot 345, `0x1032fb80`) pushes a VPROF scope naming
`CBaseCombatCharacter::SetPoseParameter` and the entity's `m_iName`, resolves the name through
`LookupPoseParameter` and dispatches slot 346 (`+0x568`, the index overload) with the resolved index,
then pops the scope. Nothing else.

`PlayScene` (slot 540, `0x10279060`) is nineteen bytes: a tail call into `CBaseFlex::PlayScene`
(`0x10084b40`) with a null out-handle. That body creates an `instanced_scripted_scene`
(`CreateNoSpawn`), points `+0x114` at the scene name (or null for an empty string) and `+0x160` at
the target's `RefEHandle` (or -1), spawns it, and answers its play length — an "unknown scene"
message and 0 when the file will not load.

Slots 59, 60 and 61 (`0x102b51e0`, `0x102b5220`, `0x102b5260`) are the choreo-scene latches.
59 raises `m_bInChoreoScene` (`+0x5bc4`); 60 and 61 are **byte for byte the same body** and clear
it. All three touch `m_bCutsceneForceLOD` (`+0x1590`, named by the sendtable builder `0x10321a60`)
only when the scene entity's own `+0x57d` byte is set, so a scene that never forced LOD cannot clear
a flag another scene raised.

`CNPC_VPlayerController::AddExtraAnimationModels` (slot 245, `0x103a49c0`) calls the base
(`0x1008e0a0`), asks slot 97 `GetOwnerEntity()` and — when that resolves and its `+0xa8`
(`m_pPlayer`, the self-downcast cache only `CBasePlayer`'s constructor fills) is set — forwards the
same five arguments to that player's `+0x3d4`; with no player it warns that the controller is not
attached to one. `RemoveExtraAnimationModels` (slot 246, `0x103a4a60`) is its exact inverse and TAIL
JUMPS into `+0x3d8`. Three census classes carry the pair: `CNPC_VFrenzyShadow`,
`CNPC_VPlayerController`, `CNPC_VWolfMorph`.

`CAI_BaseHumanoid::vfunc250` (`0x1025e4e0`) clears bits 0 and 1 of `+0x5f4c` — the cached
head/eye-direction validity pair — **before** chaining to `CBaseAnimating::StudioFrameAdvance`
(`0x10098bb0`), so the cached vectors recompute on the next read rather than lagging the frame the
model just advanced.

`CNPC_VCamera::HandleAnimEvent` (slot 259, `0x10368ec0`) is three bytes: an empty body that swallows
every animation event, footsteps included. `CNPC_VCameraSecurity` shares it.

**Unrecovered:** `DAT_1093bb14`'s cvar name and default (the object lives in uninitialised `.data`
and no corpus function constructs it), the retail names behind schedule `8` and activity `0x61`, and
what authors `CSceneEntity + 0x57d`.

## `IsTalking` — `0x102c0aa0`

Not `IsInDialog` (`0x102c1170`), which is the four-term session gate; the dialogue box hides the
player's choices while this answers true, and `UpdateCharacter` calls `FinishTalking` when
`m_bIsTalking` is set and this answers FALSE. Two arms, in order: `m_hDialogScene` (`+0x6554`)
resolving to a live entity whose `+0x498` byte is set answers true without consulting the clock at
all; otherwise it is `curtime < m_flTalkEnd` (`+0x64cc`), **strictly** less, so the stamp's own
instant already reads as finished.

**Unrecovered:** what writes `CSceneEntity + 0x498` — the corpus holds readers of the byte and no
writer inside the `CAI_BaseNPC` closure.

## `CAI_BaseHumanoid`'s branch — `0x1025e780`, `0x1025f1a0`, `0x10260540`, `0x10260670`, `0x10260750` (2026-09-13)

Five bodies that are **not on the Troika line**. `classes.md` derives `CAI_BaseHumanoid` from
`CAI_BaseActor` and gives it **no entity classname**, so no map stands one and nothing a spawned
`npc_*` dispatches reaches them; the words they read sit at offsets the flattened
`CAI_BaseNPCTroika` layout spends on output descriptors.

`0x1025e780` fills slot 277 `SetViewtarget` and is 19 bytes: clear bit 0 of `m_fLatchedPositions`
(`+0x5f4c`), then chain `CBaseFlex::SetViewtarget` (`0x100b5b00`). `0x1025f1a0` fills slot 586 on
that table and is `CAI_BaseActor::HasActiveLookTargets` — the look-queue `CUtlVector` count at
`+0x5f94`, whose 0x24-byte elements hang off `+0x5f88`; `MaintainEyeDirection` (`0x1025fa50`) skips
`PickLookTarget` when it is true.

`0x10260540` fills slot 589 there and is `CAI_BaseActor::SelectRandomExpressionForState(NPC_STATE)`.
`m_iszExpressionOverride` (`+0x5fa4`) wins for every state **except** 7 `NPC_STATE_DEAD`, which takes
the per-state table even with an override set. The pairing the body uses is **not** SDK 2013's field
order: state 1 reads `+0x5fa8`, state **2** reads `+0x5fb0`, state **3** reads `+0x5fac`, and states
5 and 7 both read `+0x5fb4`. A state with no case answers NULL, and a `string_t` null answers
retail's shared empty string `DAT_106b8540`.

`0x10260670` and `0x10260750` are `CAI_BaseActor::SetExpression` and `ClearExpression`, the SDK twin
arm for arm: a null or empty name clears; a name equal to `m_iszExpressionScene` (`+0x5f9c`) under
`__strcmpi` is a no-op; otherwise the word is cleared, `InstancedScriptedScene(this, name)`
(`0x10084b40`) writes `m_hExpressionSceneEnt` (`+0x5fa0`), and the pooled string is stored **only**
when that handle resolves to a live entity. Retail's `ClearExpression` is 11 bytes — it writes the
one word and returns, where SDK 2013's also stops the scene.

**Unrecovered:** whether `+0x5f4c` is SDK 2013's `m_fLatchedPositions` or another latch word — bit 0
is the only bit any of these bodies touches.

## `CAI_BaseNPC`'s geometry and memory helpers — `0x102729d0`, `0x10274080`, `0x10274b30`, `0x10274ca0` (2026-09-13)

Four unnamed base-line bodies the SDK twin settles.

`0x102729d0` is `GetNavTargetEntity`. It re-reads the navigator's goal type
(`thunk_FUN_102ee620(m_pNavigator)`) for **every** arm: 2 `GOALTYPE_ENEMY` answers `m_hEnemy`
(`+0x5ce0`), 1 `GOALTYPE_TARGETENT` answers `m_hTargetEnt` (`+0x5ce4`), and 7 — which SDK 2013 has no
arm for — answers the handle behind `(this+0x98)->vtable+0x928`. Anything else answers NULL, and
every arm resolves its handle through the global table, so a stale one also answers NULL.

`0x10274080` is `RememberUnreachable`, over `m_UnreachableEnts` (`+0x5d48`, count `+0x5d54`, 0x14-byte
records of handle / expiry / position). The scan is **backward** from the last record and stops at the
first match; a hit refreshes the expiry to `curtime + _DAT_10449258` (**3.0**) and falls through to
the position write without touching the handle, a miss appends. The position written is the entity's
`GetAbsOrigin()` (slot 217) at the moment of the call, which is what `IsUnreachable`'s later
"has it moved" test compares against.

`0x10274b30` fills slot 515 `CalcIdealYaw(const Vector&)`. It branches on the navigator's state
(`thunk_FUN_102ee3f0`) to pick how the delta is built — `0x37` gives `(-p.y - o.x, p.x - o.y)`,
`0x38` gives `(p.y - o.x, p.x - o.y)`, anything else the plain `(p.x - o.x, p.y - o.y)` — and always
tails into `VecToYaw` (`0x101d2c70`), which reads X and Y only. The origin is `GetOrigin()` (slot
220, the raw `m_vecOrigin`), not `GetAbsOrigin`. The Z term of the two special arms is built from an
**uninitialised** stack slot, which is harmless precisely because `VecToYaw` never reads it.

`0x10274ca0` is `SetDefaultEyeOffset`, named by its own string. It asks the model for its eye
position into `m_vDefaultEyeOffset` (`+0x5d60`) and, when that comes back the `vec3_origin` sentinel
(`DAT_1070d1b0/b4/b8`), DevMsgs `"WARNING: %s has no eye offset in .qc!"` and falls back to
`(WorldAlignMins() + WorldAlignMaxs()) * _DAT_104629b8`, which SDK 2013 states as **0.75**; then
`SetViewOffset`. Retail DevMsgs **unconditionally** where SDK 2013 gates the message on
`Classify() != CLASS_NONE`.

**Unrecovered:** what `(this+0x98)->vtable+0x928` is in `GetNavTargetEntity`'s mode-7 arm, and the
retail meaning of navigator states `0x37` / `0x38` in `CalcIdealYaw`.

## The trace channel and the concept cache — `0x1028dfb0`, `0x102947e0` (2026-09-13)

`0x1028dfb0` fills slot 19, `TraceMessageBare(const char*) const`. A null message does nothing at
all. Otherwise the global dev byte `DAT_10920534` picks the channel: set copies up to 0x200 bytes
into the NPC's own trace ring (`thunk_FUN_1027ee20`), clear prints straight through `DevMsg`. No
member is written either way, which is why it is the `const` half of the slot-19/20 pair.

`0x102947e0` fills slot 497 and is **global** state, not per-NPC. Bit 0 of `DAT_109249c4` guards it;
the first call linear-scans `DAT_1073dc40[0 .. DAT_1073dc3c)`, compares each entry's `+0x04` name
case-insensitively against the fixed string at `DAT_105d8ccc`, and caches the matching entry's
`+0x00` id — or **-1** — into `_DAT_109247dc`. `signatures.md` reads that string as the placeholder
`"???"`, so the body caches a concept index and plays nothing; every later call returns at the latch.

**Unrecovered:** what `DAT_1073dc40` is a table of beyond "entries with a name at `+0x04` and an id
at `+0x00`", and what reads `_DAT_109247dc`.

## Story 29c-1, family TroikaHelpers — the unnamed helper bodies

### The cover-lean hint pair `0x102b6120` and `0x102b7110`

_Recovered 2026-09-13, story 29c-1._

`0x102b7110` finds a tactical hint. It raises `m_bForceCoverLOSCheck` (`+0x6408`), searches hint type
8 at the ideal range (slot 550, vtable `+0x898`) through `0x102d2980`, stores the result at
`m_pHintNode` (`+0x5ddc`) and lowers the flag again; the bracket is around the FIRST search only. If
`m_bStayEntrenched` (`+0x6435`) stands and the search missed, it repeats it once, unbracketed. With a
node in hand it zeroes `m_iPeekOutCount` (`+0x640c`) and `m_iFailedCoverLOSChecks` (`+0x6404`), then
claims the node through `0x102d1350` — a refused claim drops it. For a claimed node of type `0x27d8`
it decides which side to lean from: a 2-D cross product of `(m_hHintCoverObject->origin - hint
origin)` against the hint's own forward, `dx * fwd.y - fwd.x * dy`, with a result at or below zero
setting `m_bLeaningLeft` (`+0x63fd`). Finally it caches slot 16's extents into
`m_vecSavedSleepExtents` (`+0x65d0`) and calls `CBaseEntity::SetAbsoluteAttackExtents`.

`0x102b6120` consumes that decision. With no hint node it ZEROES the caller's point and answers
false. Otherwise `0x102d1180` puts the point where the hint says to stand, and for a `0x27d8` node
it offsets it along the hint's facing rotated by `+-_DAT_1049949c` — **plus when `m_bLeaningLeft` is
CLEAR, minus when it is set** — scaled by slot 214's `+0x04` field times `_DAT_1049ae90` (the
caller's second argument set) or `_DAT_1049ae8c` (clear).

**Unrecovered:** the three `.rdata` cells `_DAT_1049949c` (= **45.0f**, float32; read 2026-09-21, `rdata-cells.md`), `_DAT_1049ae8c` (= **1.3999999762f**, float32; read 2026-09-21, `rdata-cells.md`) and `_DAT_1049ae90` (= **1.2000000477f**, float32; read 2026-09-21, `rdata-cells.md`); what
slot 214 answers; and what the caller's second argument means beyond selecting between the two
scales.

### The shoot-at hint search `0x102b6b50`

_Recovered 2026-09-13, story 29c-1._

Slot 609 answers a `CAI_Hint*`. A cached `m_pShootAtHint` (`+0x6444`) that still passes slot 566
`FValidateHintType` is reused outright. Otherwise, unless the caller's `bool` forces it,
`curtime < m_flNextShootAtHintSearchTime` (`+0x6440`) answers NULL. Past the wait it re-rolls that
deadline to `curtime + RandomFloat(2.0, 2.5)` — the draw is taken whether or not the search then
succeeds, so a miss still costs a full cooldown — and searches hint type 8 with flag byte `0x10`
through `0x102d2980`, at the active weapon's `+0x8c0` max range or at the literal `1024.0` when
there is no weapon.

**Unrecovered:** the weapon word at `+0x8c0`, and the meaning of the `0x10` search-flag byte.

### The predictive aim point `0x102c36d0`

_Recovered 2026-09-13, story 29c-1._

The lead calculation the five `m_flTargetLead*` words (`+0x655c`..`+0x656c`) exist for. A null lead
source copies the fallback through and returns, or leaves the point alone when there is no fallback.
Otherwise, with a positive interval, the pathfinder (vtable `+0x874`, slot 541) is asked through
`0x102e0290` for a point and a velocity; a refused query falls all the way through to the lead's
plain abs-origin. On success the ratio is `|aimFrom - point| / interval`, clamped between
`m_flTargetLeadMin` and `m_flTargetLeadMax` in retail's own shape (`if (r <= max) { if (r < min) r =
min; } else r = max;`). The predicted point is the queried point plus the velocity times that ratio,
with the Z term multiplied by `_DAT_104454c4` — the shared zero — so **the lead is planar**. The
blend is `(predicted * m_flTargetLeadPredictedWeight + other * m_flTargetLeadCurrentWeight) *
m_flTargetLeadWeightScale`, where `other` is the QUERIED point when the caller passed no fallback
and the FALLBACK when it did. With a fallback the blend is then hull-swept from the fallback to it
with this entity's own collision box and mask `0x2000b` under a world-only filter; a fraction below
1.0 discards the blend and answers the fallback.

**Unrecovered:** nothing in the body; the five weights are authored per NPC and `0x102e0290`'s
contract beyond "a point and a velocity" is not walked here.

### The jump origin and target `0x102c4ad0`

_Recovered 2026-09-13, story 29c-1._

Stamps the pair `CNPC_VAsianVampire::SetupJump` later reads. `m_vJumpOrigin` (`+0x649c`) takes this
NPC's abs-origin and `m_fJumpHeight` (`+0x64b4`) the caller's second argument. The distance to the
goal comes from `VectorNormalize(goal - self)` (`0x10137220`, which answers the length before
normalising). **Below the caller's third argument the target IS the goal's origin; at or above it
the planar components are pulled back by that same argument used as a FRACTION**, and the height is
taken from `goal.z - backoff * 0.0` — that is, the goal's height untouched on both arms. One
argument serving as a distance on the test and a fraction on the offset is retail's and is
reproduced.

**Unrecovered:** nothing; the odd double duty of the third argument is what the body does.

### The scheduled-move stop `0x102bf770`

_Recovered 2026-09-13, story 29c-1._

The inverse of `0x102bf7e0`. When `m_IdealActivity` (`+0x0ff0`) still equals the navigator's current
link activity (`0x102ee3f0`) it re-derives the link's idle activity through `0x1027a6c0` and applies
it with `SetActivity` (`0x10272650`). Then, unconditionally: `m_bShouldMove` (`+0x1a40`) is cleared,
the navigator is reset (`0x102ee2a0`) and `m_flDesiredMoveYaw` (`+0x63ec`) is zeroed.

**Unrecovered:** the link object behind `0x102ee3f0` / `0x1027a6c0`, which family Motor records the
same gap for.

### The dialogue-release path `0x102c0360`

_Recovered 2026-09-13, story 29c-1._

The owning NPC's half of `CDialog::Release`. With a live `m_hDialogPartner` (`+0x0fe8`) it clears
`m_bCutsceneForceLOD` (`+0x1590`), fires the `m_OnDialogEnd` output at `+0x5f5c` with the PARTNER as
activator (`0x100cd660`), and calls `CBaseCombatCharacter::SetDialogPartner(NULL)` — in that order.
The tail, outside the partner test, is a debug hook behind the cvar `DAT_109241fc`:
`thunk_FUN_10245660("entity_debug_stats")` and a `+0x10` dispatch on whatever it finds, then
`thunk_FUN_10119900(NULL)`. It reaches no game state.

**Unrecovered:** `DAT_109241fc`'s name and default, and what `entity_debug_stats` is.

### The dialogue network priority `0x102c04b0`

_Recovered 2026-09-13, story 29c-1._

Slot 612 answers `0x80` with no live `m_hDialogPartner` (`+0x0fe8`); with one it answers `0x280` when
`m_bDialogQueIsFinal` (`+0x654c`) stands and `0x680` when it does not. The final flag is read only
inside the partner arm, so a body with no partner answers `0x80` whatever it says.

**Unrecovered:** the retail name, and what consumes the three numbers — they have the shape of
network priorities and no reader in the corpus pins them.

### The disposition stance selector `0x102c12a0`

_Recovered 2026-09-13, story 29c-1._

Slot 611 answers a sequence index for `ACT_DISPOSITION`. `0x100ecee0` fetches the disposition record
plus three out-parameters: a delay, a stance-change chance and an alternate-clip chance. The record
carries two arrays of three, at `+0x08` and `+0x14`, indexed by `m_CurrStance` (`+0x64c8`). Arm
order: `m_bIsTalking` (`+0x64c0`) answers the `+0x08` entry and clears the fidget latch;
`m_bFidget` (`+0x64e0`) or `m_bTransition` (`+0x64e1`) — the settle after a one-shot — does the same;
otherwise, when the two arrays differ at this stance and `RandomInt(1, 100)` comes in under the
alternate chance, the `+0x14` entry is taken and the fidget latch is SET; otherwise, when the delay
has elapsed since `m_flStanceTime` (`+0x64e4`) and a second `RandomInt(1, 100)` comes in under the
change chance, `0x102c1230` picks a new stance, clears the fidget latch and sets the transition one;
otherwise the `+0x08` entry. Every arm but the stance change clears `m_bTransition` on the way out.
A resolved sequence of `-1` falls back to `m_nSequence` (`+0x6f0`).

**Unrecovered:** nothing in the arms; the record's layout past the two arrays is not walked.

### CAI_Motor's unnamed bodies `0x102e0ea0`, `0x102e0f90`, `0x102e1180`, `0x102e1270`, `0x102e19e0`, `0x102e2180`, `0x102e2580`

_Recovered 2026-09-13, story 29c-1._

Seven bodies on `CAI_Motor`'s OWN vtable — slots 3, 4, 6, 8, 18, 15 and 17 there, unrelated to the
shared entity numbering. `this->field_0x4` is the owning NPC throughout.

Slot 3 (`0x102e0ea0`) is the init: force activity `0x33` through the owner's `+0x4d8` **first**, then
reset the motor state (`0x102e2840`), `SetSolid(owner, 2)`, zero `m_flGravity` (`+0x3ec`) and
dispatch the owner's `+0x340` with `(0, 5, 0)`.

Slot 4 (`0x102e0f90`) decelerates toward a goal. The distance is `VectorNormalize(goal -
owner->slot220())` and the velocity issued is that raw delta times `_DAT_10450564`, written before
the branch and so on both arms. Under `m_flMoveInterval * _DAT_10450564` it draws `distance *
_DAT_10450aa4` off the interval — with distance forced to zero when it is at or below
`_DAT_1044e658`, so a body inside the arrival radius decelerates by nothing — and dispatches the
owner's `+0xf8`, answering 1. At or above, the interval is zeroed and the move reissued at speed
`-1.0` (`0x102e1c10`), answering 0.

Slot 6 (`0x102e1180`) is stop-and-face: `SetAbsVelocity(goal)` — the goal vector itself, not a scaled
direction — the owner's `+0x340(0)`, forced activity `0x2c`, then a reissue at
`UTIL_VecToYaw(goal)` and speed `-1.0`. Slot 8 (`0x102e1270`) is the full stop: `SetAbsVelocity(0,
0, 0)` and forced activity `0x30`, with no reissue.

Slot 15 (`0x102e2180`) averages the facing queue (`motor+0x54`, stride `0x24`, count at `+0x3c`). It
first compacts the queue in place, dropping every entry `0x102d8b50` marks expired through
`0x102e2b10` without advancing the cursor, then walks the survivors accumulating `(target - self) *
w + acc * (1 - w)` with `w` from `0x102d8bc0`, normalising the accumulator after each. The per-entry
delta is normalised too and the result **discarded** — the registers the blend multiplies are loaded
before that call.

Slot 17 (`0x102e2580`) answers the move speed: the base speed (`0x102e12c0`) raised to the motor's
own `+0x40` when it is below it, then raised again to the hull table's floor for this hull
(`0x102d61b0`). **Both bounds are floors**, not clamps.

Slot 18 (`0x102e19e0`) writes the steering. The owner's `+0x838` clip test refuses outright. Past it
the body reads `m_nSequence` (`owner+0x6f0`) and asks `0x102e2820` whether that sequence carries the
pose parameter `"move_yaw"`. Without it, the move is reissued at `(ftol(yaw) & 0xffff) *
_DAT_1044ffdc`. With it, the motor's own `+0x3c` — **which is slot 15, the facing-queue average
above** — supplies the heading, the move is reissued at the same quantised yaw, and the difference
between that heading and the owner's own yaw (`UTIL_AngleDiff`, `0x1013d580`) is written NEGATED
either to the owning NPC's `m_flDesiredMoveYaw` (`+0x63ec`) when `owner->field_0x98` resolves, or
through `0x102e27d0` as the `"move_yaw"` pose parameter when it does not.

**Unrecovered:** `_DAT_10450564` (= **100.0f**, float32; read 2026-09-21, `rdata-cells.md`), `_DAT_10450aa4` (= **0.0099999998f**, float32; read 2026-09-21, `rdata-cells.md`), `_DAT_1044e658` (= **0.01**, float64; read 2026-09-21, `rdata-cells.md`) and `_DAT_1044ffdc` (= **0.0054931640625f**, float32; read 2026-09-21, `rdata-cells.md`); what slots
220, 208 (`+0x340`), 62 (`+0xf8`) and 526 (`+0x838`) are; and the facing-queue entry's own layout
beyond a target and a weight.

### CAI_Navigator's unnamed bodies `0x102eea70`, `0x102eeac0`, `0x102eee40`

_Recovered 2026-09-13, story 29c-1._

Slots 7 and 11 on `CAI_Navigator`'s own vtable are **byte-identical**: the shared reset `0x102eeb70`
writes `+0x54 = -1`, `+0x58 = -1.0` and `+0x60 = -1.0` and clears the route list at `+0x28`
(`0x102ddc40`), then each sets `+0x1c = 1` — the same "this navigator has failed" latch `OnNavFailed`
(`0x102eeae0`) sets.

Slot 17 (`0x102eee40`) builds a 16-word move-info block from `m_pPath`: the nav type at `+0x18` into
word 12; the current path point (`0x10012805`) into 0–2; the per-axis delta to it from
`owner->slot220()` into 3–5, with word 5 zeroed and the length taken 2-D at nav type 0 and the whole
vector NORMALISED and its length taken at any other type; words 6–8 copy 3–5 **after** that
normalise, so they are a direction at nav type != 0 and a raw delta at 0; word 9 the motor's base
speed (`0x102e12c0` on `nav+0x20`); word 11 that speed times the motor's `+0x30`; word 13 the
navigator radius (`0x102ecc40`); then word 11 is floored at word 10. Bit 0 of word 14 is set for a
straight-line path (`0x1030bd50`) and bit 2 when the next waypoint's kind (`+0x2c`) differs from its
owner's; word 15 always takes the path pointer.

**Unrecovered:** the three reset words' names; the `CAI_Path` layout beyond `+0x24`, `+0x2c` and
`+0x30`; and what word 14's other bits mean.

### CAI_StandoffBehavior's unnamed bodies `0x102c7410`, `0x102c7530`, `0x102c7960`, `0x102c79a0`

_Recovered 2026-09-13, story 29c-1._

Four bodies on `CAI_StandoffBehavior`'s own vtable; `this+0x4` is the owning NPC. They sit beside
the selector `0x102c7600` (slot 13 there) that story 29c-1's family Lifecycle walked.

`vfunc3` (`0x102c7410`) asks whether the standoff may do its ranged thing. A clear byte at
`this+0x19` answers false and touches nothing. With it set: the owner's `+0x804` (slot 513) must
carry `0x8000000`, and `SelectHeaviestSequence(owner, 8, -1)` must answer a non-negative index —
**each of those two refusals clears `+0x19`**. Past both, the answer is `m_iDisciplineCastCounter ==
2` and a live `GetActiveWeapon()`, and that refusal does NOT clear the byte.

`vfunc5` (`0x102c7530`) drops the owner's claimed hint. With a live `m_pHintNode` (`owner+0x5ddc`) it
tests `IsHintUnusable` (`0x102d14c0`) and, on TRUE, checks ownership (`0x102d1450`) and releases with
a 0.0 reuse delay (`0x102d1420`) — so it releases only a hint it has already judged unusable. The
node is then zeroed unconditionally, `this+0x50` is copied into the owner's `m_flDistTooFar`
(`+0x5de4`), and the owner's `+0x1fc` is forced to 2.

`vfunc20` (`0x102c7960`) and `vfunc21` (`0x102c79a0`) are **byte-identical**: when the owner's
`m_pSchedule` (`+0x5c38`) is non-null and equals `GetSchedule(this, 0x17)` (`0x102cc1f0`), clear
`COND_NEW_ENEMY` (0x54) on the owner.

**Unrecovered:** `this+0x19`'s and `this+0x50`'s retail names; `owner+0x1fc`, which no other body in
layers 0–9 reads; what distinguishes slots 20 and 21, which carry the same body; and what
behaviour-local schedule `0x17` is.

## Story 29c-1, family EntityChain — the unnamed bodies of the chain below the NPC

_Recovered 2026-09-13, story 29c-1._

Fifty-seven bodies under `0x101d0000` that 29a's naming pass could not name. Reading the decompiled C
showed they are **eight classes, not one**: `CBaseEntity`, `CBaseAnimatingOverlay`/`CBaseFlex`,
`CBasePlayer`, `CCineNPC`, `CDialog`, `CGlobalEntityList` and `CPayphone`. `vtmb_fields CBasePlayer`
then named most of what the walk had left open — `m_hClosestNPC`, `m_flSpawnResponseCopsTimer`,
`m_vecAutoAim`, `m_fOnTarget`, `m_hControllerNPC`, `m_hCameraTargetEntity`, `m_LevelSupernaturalAct`
— so the sections below use retail's own field names. The five law/police bodies are walked in
`docs/vtmb/player-entity.md` § "Law, Masquerade and world response", beside the state they write.

### Two `.rdata` constants the ledger left open — `0x10026710`, `0x101a6c20`, `0x101a67c0`

Slot 37's whole body is `return (float10)_DAT_104454c8;` and `npc-kernel/checklist-0-9.md` records
the value as unrecovered, naming `UpdateEnemyPos` (`0x10271900`) and `UpdateTargetPos`
(`0x10271b10`) as where it would be recovered. It is **80.0**: the same word is read as "farther
than `_DAT_104454c8 = 80.0` units from the goal point" in `npc-ai/conditions-and-states.md` and as
`CPropDoorknob`'s 80-unit break-off in `computer-terminals.md`. Slot 550 `CoverRadius`
(`0x101a6c20`) is `_DAT_1045d650` = **1024.0**, the same word `computer-terminals.md` reads as the
1024-unit downward ray. Slot 476 `HearingSensitivity`'s base (`0x101a67c0`) is `_DAT_104454c0` =
**1.0**, the image's shared one — so the base sensitivity is unity and `CanHearSound`'s
`volume * sensitivity` is the bare volume, with Troika's `+0x63c0` override the only thing that
changes it.

### Standability is two predicates, not one — `0x100b50a0`, `0x100b5110`, `0x10026f20`, `0x10026fb0`

`0x100b5110` is the shared helper: `GetSolid() == SOLID_BSP` (1) is standable outright,
`SOLID_VPHYSICS` (6) asks the physics environment whether the object is at rest, and everything else
is not. Slot 164 `IsStandable` (`0x100b50a0`) puts `GetSolidFlags() & FSOLID_NOT_SOLID` in front of
it and then takes `SOLID_BSP`, `SOLID_VPHYSICS` **and** `SOLID_BBOX` (2) as standable outright,
falling to the helper only for the rest. The asymmetry is real and observable: a moving physics prop
is standable to slot 164 and not to the helper, because slot 164 never asks the physics object. Slot
159 `ReflectGauss` (`0x10026f20`) is the helper **and** `m_takedamage == 0` — "a solid I could stand
on that takes no damage", i.e. world geometry rather than a body. Slot 165 `CanStandOn(edict_t*)`
(`0x10026fb0`) only selects an argument: `edict->m_pNetworkable (+0x40)->GetBaseEntity() (+0x10)`
when both are non-null, else literal 0 — and it dispatches slot 166 on **both** arms, so a null edict
is `CanStandOn(nullptr)` and not a refusal.

**Unrecovered:** nothing in these four.

### Slot 226 `VPhysicsUpdate` — the per-movetype physics tick — `0x100b4f30`

Three arms on slot 94's answer. Movetype **7** reads the object's transform back through
`IPhysicsObject +0x94`, warns "Infinite values from vphysics!" on any component whose exponent field
is all-ones (`& 0x7f800000 == 0x7f800000`), and then writes it anyway — the warn does not abort —
before `SetAbsOrigin` (slot 216), `SetAbsAngles` (slot 218), `PhysicsTouchTriggers(0)` and
`PhysicsRelinkChildren`, in that order. Movetypes **1** and **8** call
`CBaseEntity::VPhysicsUpdatePusher`. Every other movetype does nothing at all.

**Unrecovered:** which Troika `MoveType_t` names 1, 7 and 8 are. Stock Source's enum does not fit —
7 is `MOVETYPE_PUSH` there and takes the pusher arm, while here it takes the read-back arm — so the
numbers are carried as numbers.

### Slot 135 — the move-rebound easing — `0x101c10d0`

Five strict gates in retail's order: the interval, `m_flMoveDoneTime`, `m_flMoveReboundStartTime`,
`m_flMoveReboundStartTime < m_flMoveDoneTime`, and `m_flMoveReboundDuration`, all `> 0`. Then
`t = (interval + m_flLocalTime) - m_flMoveReboundStartTime`, refused at or below zero and clamped
above at the duration. The blend is `f(t) = (t*t + 1)*t - (t/D)*(D*D + 1)*t` — a cubic minus a linear
scaled so `f(0) == f(D) == 0`, i.e. a rebound that leaves and returns. Each half runs only when its
rebound triple is not all zero: the linear one writes
`SetLocalVelocity((m_vecFinalDest + f(t)*m_flMoveReboundVelocity - GetLocalOrigin()) / interval)` and
the angular one the same shape against `m_vecFinalAngle`, slot 221 and `SetLocalAngularVelocity`. The
answer is always the interval, on every path including every refusal.

**Unrecovered:** nothing in the body. The seven `CBaseEntity` mover words it reads have no port
member, so the port's first gate refuses.

### Slot 268 `SetLayer` and slot 271 `FindLayerByOwner` — `0x10099020`, `0x100994c0`

Eleven writes and one conditional pair over `CBaseAnimatingOverlay`'s four 0x30-byte records at
`+0x0734`: activity, sequence, cycle 0, playback rate 1.0, weight 0.1, weight max 1.0, blend in and
out 0.2, the auto-kill flag, the finished marker and the last event check — and `m_fFlags` (+0x00) is
**not** written. A sequence whose studio flags carry bit 2 (SNAP) then has both blend fractions
zeroed, so it stands at full weight from the frame it is armed. Slot 271 scans the same table from
`GetFirstGestureLayer()` — 0 for every class in the hierarchy — refusing outright if that is 4 or
more, and tests three terms per slot: the weight is not zero (`_DAT_104454c4`), the owner is not
`ACT_INVALID`, and the owner is the activity asked for. **-1 on a miss**, which is the convention
every caller tests against.

### Slot 266, slot 279 — the flinch and flex writes — `0x100997f0`, `0x100b5ba0`

Slot 266 loops the three flinch records at `+0x07f4` (stride 0x1c) and writes exactly two fields
each: the sequence to -1, and the expire time to `curtime - 1.0` — one second in the *past*, already
expired on purpose. The latch, both fades and the pose-parameter index survive a clear, which is why
a cleared record still remembers which pose parameter it drove. Slot 279 `SetFlexWeight(int, float)`
normalises: out of range (below zero, at or past `GetNumFlexControllers()`) or no studio header drops
the write entirely; `max != min` stores `(value - min) / (max - min)`; `max == min` stores the value
unchanged. **It does not clamp** — a value outside the authored range stores outside 0..1 and slot
281 maps it straight back out. Slots 279 and 281 are exact inverses wherever the range is
non-degenerate.

### Slots 285 and 287 — the scene-event queue, and why they differ — `0x100b5d80`, `0x100b6180`

Slot 285 `ClearSceneEvents(scene)` is two bodies. With a **null** scene it sets the count at
`+0x0a64` to zero and does nothing else — no release, no zeroing, no compaction; the records are
still there and retail simply stops counting them. With a scene it walks the array, and for every
record whose SCENE word matches it releases the event (`0x10075b70`), zeroes words 0 and 1 and the
byte at 2, `memmove`s the tail down one stride, and decrements **both** the count and the cursor — so
the compacted-in record is re-tested, which is what lets one pass remove every event of a scene. Slot
287 `RemoveSceneEvent(event)` keys on the EVENT by pointer equality and **stops at the first match**:
a second record carrying the same event survives, which a program can observe. A null event is not
special-cased there the way a null scene is in slot 285.

### Slots 447 and 450 — the id-space translation — `0x101a6620`, `0x101a6640`

Both forward into `0x102ea280`, the GLOBAL-to-LOCAL direction of the range walk family Squad ports
the other half of: -1 stays -1; otherwise follow the chain at `+0x10` and, for the first space whose
local base is not the 9999 sentinel and whose `[globalBase, localTop]` range holds the id, answer
`(localBase - globalBase) + id`. Slot 447 passes `GetClassScheduleIdSpace()` (the SCHEDULE sub-space
at `+0x00`); slot 450 passes the same pointer advanced by `+0x18`, which is the **task** sub-space of
the same `CAI_ClassScheduleIdSpace`. Slot 580's own Troika-line body (`0x101aa790`) is
`return &DAT_10924248` and the `CAI_BaseNPC` base under it (`0x101a6d00`) is a *different* global,
`&DAT_1090ff08`.

**Unrecovered:** what `DAT_1090ff08` holds. Nothing in the image registers a schedule into it, so it
stands at the empty state `0x102ea090(isRoot = false)` left.

### `CDialog`'s two bodies — `0x100e49b0`, `0x100e4ef0`

`0x100e49b0` walks the loaded dialogue's PC-line table: a flag byte per line at `+0x2810`, a type int
per line at `+0x2820`, the count at `+0x2834`. Four gates — index in range, the dialog's speaker
handle (word 0 of the object) resolving live, and the line's flag bit 0 — then the type selects.
Types **5** and **6** take byte-identical arms: `LookupBone("bip01_head")` on the speaker, then slot
192 for that bone's world position, then `thunk_FUN_101d26b0`. Every other type returns having done
nothing. Both arms re-resolve the speaker handle a second time *between* the bone lookup and the
position fetch, which means a speaker that died inside `LookupBone` reaches slot 192 through a null
pointer.

`0x100e4ef0` runs the 0x100-byte buffer at `CDialog+0x31ea` through `CDialog::CallEventScript` when
its first byte is non-zero, and then zero-fills all 0x100 bytes **unconditionally** — the clear is
outside the `if`, so an empty flush is still a wipe. The buffer is filled by
`CDialog::process_pc_line` (`0x100e8520`,
`Q_strncpy(this+0x31ea, this + line*0x228 + 0x2964, 0x100)`) and is a different field from the col-5
NPC action the port's `FlushPendingNpcAction` drains.

**Unrecovered:** what `thunk_FUN_101d26b0` does with the fetched head position.

### `CGlobalEntityList`'s listener registry — `0x100f6d80`, `0x100f6e40`

`+0x18048` is a `CUtlVector<IEntityListener*>` on the global entity list `g_pEntityList`
(`DAT_106eb5d8`) — not an NPC field. `0x100f8700` walks it **backwards** calling each element's slot
1, which is what identifies the elements as listener objects, and the registrants in the image are
`CNavPropertyDatabase`, `CPhysSaveRestoreBlockHandler`, `CEntityListSystem` and the aim-target
manager (`0x102cd650` / `0x102cdb70`, at `param+0x114`). So `0x100f6d80` is `AddListenerEntity`: a
linear dedupe scan that returns on a hit, a grow when the count would exceed `+0x1804c`, and an
insert at the end — retail computes a `memmove` for the tail past the insertion point and then finds
its length is zero, because the insertion point *is* the end. `0x100f6e40` is `RemoveListenerEntity`:
search by value, `memmove` compaction, decrement; a value not held is a silent no-op and the capacity
is not reduced.

### The `npc_VPlayerController` detach — `0x101618e0`

Gated on `m_hControllerNPC` (`+0x1db0`) resolving live; nothing happens otherwise, not even the
handle clear, which is inside the gate. The first flag additionally requires the controller to carry
a `CAI_BaseNPCTroika` (`controller+0x98`) and then bulk-copies, in retail's order, `m_nSequence`
(`+0x6f0`), `m_flAnimTime` (`+0x174`), `m_flCycle` (`+0x6f8`), `m_flPlaybackRate` (`+0x6f4`), then
**0xc0 bytes from `+0x734`** — the whole four-record gesture-layer table — and **0x54 bytes from
`+0x7f4`** — the whole three-record flinch table. Both are `rep movsd`, so every field travels,
including ones no body on the receiving side ever writes. The second flag transfers absolute linear
and angular velocity, recomputing the source's cached value first only when its `m_iEFlags` bit 0xc
is set. Then, unconditionally and outside both flags:
`ThinkSet(controller, 0x101c0b10, 0.0)`, `controller->m_flNextThink = curtime + _DAT_1044e658`, and
`m_hControllerNPC = -1`. Retail arms the one-shot think on the entity it is *letting go of*.

**Unrecovered:** `_DAT_1044e658` (= **0.01**, float64; read 2026-09-21, `rdata-cells.md`), and what `0x101c0b10` does when that think fires.

### Autoaim — `0x10176520`, `0x10176930`

`0x10176520` is `CBasePlayer::GetAutoaimVector`. With autoaim off the whole body is
`AngleVectors(m_Local.m_vecPunchAngle + pl.v_angle)` — note that it does **not** include
`m_vecAutoAim`, so a retained deflection survives a toggle and is ignored while off. With autoaim on:
`Weapon_ShootPosition(GetOrigin())` (slot 389 off slot 220); `m_vecAutoAim` is zeroed *before* the
deflection is computed, so the deflection's own angle sum sees a zero term; `AutoaimDeflection` over
**16384 units**; `!AllowAutoTargetCrosshair()` clears `m_fOnTarget` and only that; the two returned
angles are wrapped into `(-180, 180]` by a **single-pass** pair of tests (an angle outside
`(-540, 540)` comes out still outside) and clamped to **±25 degrees pitch and ±12 degrees yaw**; then
either scaled by `_DAT_10450a9c` or blended as `old * _DAT_10451ab8 + new * _DAT_10457f54`; then
`AngleVectors(punch + v_angle + m_vecAutoAim)`.

`0x10176930` is `AutoaimDeflection`. It asks the autoaim gate a second time, clears `m_fOnTarget`
before anything is found, and traces the aim ray. Something in the way that takes damage ends the
search at zero deflection — the water-level pair (`mine != 3 && his == 3`, or `mine == 3 && his == 0`)
is the one thing that lets it fall through — and `FL_AIMTARGET` (`0x10000`) on the hit raises
`m_fOnTarget`. Otherwise it walks the live entity list screening on, in order: the edict's free byte,
"not my own edict" (`this+0x2e0`), the rules' per-candidate admission, a resolvable base entity,
`FL_AIMTARGET`, slot 158 `IsAlive`, the same water-level pair, and
`IRelationType(candidate) == D_HT` **or** the candidate being a player **or** a rules override. A
survivor's `BodyTarget` (slot 197) is the aim point, admitted only when `dot(toTarget, look) >= 0`
and its score is at or below the running best, which starts at the caller's delta; a second trace
confirms line of sight.

**The walk order, recovered (2026-09-13).** It is not "the live entity list" in any order the engine
happens to hold: it is the **edict array, by ascending index**, and the body walks it by pointer
arithmetic rather than through a list API —

```
edict = engine->PEntityOfEntIndex(0);            // (**(DAT_1070b22c + 0x98))()
for (i = 1; i < gpGlobals->maxEntities; ++i) {   // gpGlobals + 0x38
    edict += 0x78;                               // sizeof(edict_t), one slot
    if (edict[0x4c] != 0) continue;              // the free-slot byte
    ...
}
```

Index 0 is never visited: it is the engine's reserved world edict. The order is **observable**,
because the admission is `score <= best` and not `score < best` — a candidate that ties the running
best REPLACES it, so among two equally well-aligned candidates the one at the **higher edict index**
wins the aim assist. 29c-1 first recorded this as a named divergence ("the walk is in world order
rather than edict order") on the reading that a tie kept the first candidate; the decompiled C is
`if (fVar15 <= fStack_f8) { fStack_f8 = fVar15; pCStack_f4 = pCVar12; }`, so the reading was wrong
and the order matters. The port reproduces it: `FElysiumEntityWorld::EntityList` **is** this
runtime's edict array — its index is the handle index, "stable, never recycled", the map's entity
lump in lump order followed by every runtime spawn in creation order — and
`FElysiumNpc::ChainEntityList` walks it by ascending index. The one structural difference is that
this list reserves no slot for a world edict, so its index 0 is the map's first entity; that is a
missing reserved slot and not a different order, and retail's world edict fails the `FL_AIMTARGET`
screen in any case.

**Unrecovered:** four things. The caller's `flDelta` (it arrives in the caller's frame); the score's
three weights (`_DAT_10449198`, `_DAT_10449280` (= **1.0**, float64; read 2026-09-21, `rdata-cells.md`), `_DAT_10449270` (= **0.5**, float64; read 2026-09-21, `rdata-cells.md`), all shared `.rdata` words with no
value in the corpus) and the frame-local divisor beside them, of which only the SHAPE is recovered —
a distance term times an off-axis term, smaller is better; `DAT_1070ba3c`, the branch selector, which
sits exactly where the SDK's sticky-aim test `m_fOldTargetTime + 2.0 < curtime` does; and the two
blend weights `_DAT_10450a9c` and `_DAT_10451ab8`. Only `_DAT_10457f54 = 0.7` is settled, and it does
**not** pair with the SDK's 0.4, so Troika retuned the blend and the other two cannot be taken from
the SDK.

### The held use entity — `0x1017c6d0`

`m_hUseEntity` at `+0x1eb8` is an **edict index**, not an EHANDLE: `0x100d5000` hands it straight to
the engine's `PEntityOfEntIndex` (`+0x98`). The guard and the clear are not the same guard — the
dispatch runs only with a held entity **and** the one-shot byte at `+0x1ec0` set **and** the edict
resolving through its networkable, while `m_hUseEntity = 0` runs outside every guard on every path.
So a release with the one-shot already down still drops the held entity, silently. The engine resolve
is performed twice and the second can answer differently from the first.

**Unrecovered:** the input's name. `DAT_10555f7c` has 13 referrers including
`datamap_CBaseEntity_builder`, `CBaseDoor::Use` and `CBasePlayer::PlayerUse`, which makes it an input
`CBaseEntity` itself registers on every class; the string's bytes are not in the corpus.

### The camera-override crossfade — `0x1017d900`, `0x1017d680`

Gated on `m_flCameraOverrideFadeMarkTime` (`+0x19b8`) being positive **and** at least one of
`m_hCameraViewEntity` (`+0x19c0`) and `m_hCameraTargetEntity` (`+0x19cc`) resolving live — either one
alone is enough, which is what lets a shot with only a target keep fading. A **zero** duration
answers 1.0 outright; a **positive** one answers `clamp((now - mark) / duration, 0, 1)`; a
**negative** one answers `clamp(1 + (now - mark) / duration, 0, 1)` — a count *down* from 1 — and its
underflow does not clamp, it falls through to the reset. The reset arm, taken by the gate's refusal
and by that underflow, zeroes `+0x19b8`, `+0x19bc`, `+0x19e4` and both handles and answers 0. **It is
a side effect of a query**, which is why `0x1017d680` calls it before resolving its own handle:
reading the fraction is what tears a finished fade down, so the resolve answers null on the very call
that observes the end.

**Unrecovered:** `+0x19e4`'s name and concern. The datamap names nothing between
`m_flCameraTargetCrossfadeDuration` (`+0x19d4`) and `m_bObfWasMoving` (`+0x19f4`).

### The closest-NPC cache — `0x101828b0`, `0x10182a90`

`m_hClosestNPC` (`+0x1cc0`), `m_flClosestNPCDist` (`+0x1cc4`) and `m_iClosestNPCSense` (`+0x1cc8`).
`0x101828b0` resolves the cached handle first, and a cache that does not resolve resets the triple to
(-1, `0x47c34ff3` = **100000.0**, 0). Three arms follow. The cached entity being *the same* entity
refreshes the distance and the sense if it is alive and resets the triple if it is not — and this arm
accepts **any** distance, including a larger one, because the nearest NPC staying nearest is not a
comparison. A cached entity that is a different, now-dead entity resets the triple and falls through.
The acceptance ladder then needs all seven of: the new distance strictly less than the cached one;
the candidate alive; `candidate+0x19c & 0x40` clear; `GetModelPtr()` non-null;
`IRelationType(me) == D_HT` — the candidate hates *me*, not the other way round;
`candidate->GetFollowerBoss() != me` (slot 293), so my own follower is never my nearest threat; and
`0x100b5190` answering false.

`0x10182a90` computes the sense value the cache stores beside the NPC. It folds three perception
terms and answers the fixed code **`0x264`** when the folded value exceeds the stored distance, a
packed `(int)(scalar * term * _DAT_10450aa4) | (dist < that) << 8` when the distance is at or below
the first term, and 0 otherwise. `0x264` is a magic answer and not a number the caller does
arithmetic with — it collides with neither the low-byte truncation nor the bit-8 boolean.

**Unrecovered:** `candidate+0x19c`'s `0x40` bit; `0x100b5190`; the three perception folds
(`0x1029c970`, `0x1029c9f0`, `0x1029ca30`); the integer `0x101e9000(0x10739d08)` scales by; and
`_DAT_10450aa4` (= **0.0099999998f**, float32; read 2026-09-21, `rdata-cells.md`).

### The player-animation arm — `0x10182c40`

A hand-rolled `strcpy` of the caller's name into the **global** buffer `DAT_10724ef0` — one buffer
for the whole process, so a second call overwrites the first caller's name in place. `+0x1ca4` then
takes `&DAT_10724ef0` when the *copied* name is non-empty and literal 0 when it is not, so an empty
name still wipes the buffer and then disarms the pointer. `+0x1ca8` takes `curtime - _DAT_10449270`
plus either the sound length `(*DAT_1070b248 + 0x30)(sound)` — when `m_hDialogPartner` (`+0x0fe8`)
resolves live, that partner carries a `CAI_BaseNPCTroika` (`partner+0x98`) *and* the sound name is a
non-empty string — or the fallback `_DAT_10471720`. Then `+0x1cac |= 1`.

**Unrecovered:** `+0x1ca4`/`+0x1ca8`/`+0x1cac`'s retail names (the datamap covers
`m_bPlayerAnimCyclePlaying` at `+0x1cb0` and `m_aLastplayerAnim` at `+0x1cb4` and nothing below
them); `_DAT_10449270` (= **0.5**, float64; read 2026-09-21, `rdata-cells.md`), the lead-in subtracted from curtime, which is the same shared word the
autoaim score multiplies by; and `_DAT_10471720` (= **0.6**, float64; read 2026-09-21, `rdata-cells.md`), the no-sound fallback duration, which this file
already asks the same question of elsewhere.

### `CCineNPC`'s three — `0x101a7540`, `0x101a8930`, `0x101a8840`

`0x101a7540` is `IsTimeToStart`: `m_iDelay` (`+0x5f70`) below 1 **AND** `m_startTime` (`+0x5f74`) at
or before curtime. The SDK's own body is an **OR** of the same two terms, so retail's is a shipped
divergence: a beat with a delay never starts by its start time alone, and one with no delay still
waits for it.

`0x101a8930` is `CanInterrupt`: `m_interruptable` (`+0x5f90`) set **and** the resolved `m_hTargetEnt`
(`+0x5ce4`) answering slot 158 `IsAlive`. A missing target answers **false**, not true — an
interruptable beat whose actor has gone cannot be interrupted, it is already over.

`0x101a8840` is `FixScriptNPCSchedule`, one of the SDK's own named bodies and the tail of every
scripted-sequence teardown: if the NPC's `m_IdealNPCState` (`+0x5cc4`) is not 7 (`NPC_STATE_DEAD`),
stamp the `m_SelectIdealStateTrace` pair (`+0x1b3c` the `scripted_cp` path, `+0x1b40` the `__LINE__`
`0x3ca` = **970**) and store 1 (`NPC_STATE_IDLE`); then `ClearSchedule` (`0x10280d30`) on every path,
including the dead one. **29c's walk read that file/line write as "a one-shot assertion/error-state
tripwire". It is not**: `npc-kernel/layout.md` names `+0x1b3c`/`+0x1b40` the ideal-state selector
trace, and every `m_IdealNPCState` writer in the image — `Event_Killed` `0x10265ad0`, `NPCInit`
`0x10273390`, `CineCleanup` `0x1027d170` — stamps it the same way.

### `CPayphone`'s three — `0x101aa950`, `0x101aad90`, `0x101aadb0`

`CPayphone#35` (`0x101aa950`) is the whole capability mask behind one virtual:
`return CanTalk(other) ? 0x2f : 0`, with `-(c != 0) & 0x2f` the compiler's branchless spelling.
`CPayphone#286` (`0x101aad90`) is an EMPTY `AddSceneEvent` — a payphone swallows every choreographed
scene event instead of queueing it the way `CBaseFlex::AddSceneEvent` would, and because only
`CPayphone` fills slot 286 with it, it is a body and not a `default:`. `CPayphone#612` (`0x101aadb0`)
answers the speech sound FLAGS the line emitter (`0x102c0520`) passes to `EmitSound`: `0xa80` when
`m_bDialogQueIsFinal` (`+0x654c`) is **set** and `0xe80` when it is clear. Note the sign against the
Troika line's own body (`0x102c04b0`), which adds `0x400` when the flag is *clear*: the payphone's
two answers differ by `0x400` in the opposite direction, so a payphone's last line is the quiet one
and the Troika line's is not.


### `0x1026db30` — `CapabilitiesGet`

_Recovered 2026-09-13, story 29c-1._

Slot 513, and once the named scope-trace push/pop that brackets it is stripped, the whole body is
`m_afCapability` (`+0x5cec`) ORed with the active weapon's own capability word:
`if (GetActiveWeapon()) caps |= GetActiveWeapon()->vtable[+0x5a0]()`. `GetActiveWeapon` is called
twice, once for the null test and once for the dispatch — it cannot answer differently between them.
Slot 360 (`+0x5a0`, retail body `0x1014f930`) is the weapon's own `CapabilitiesGet`, so a capability
an NPC does not carry itself can still arrive with the thing in its hands, and dropping the weapon
takes it away again. Nothing else contributes: there is no per-state mask and no squad term.

### `0x1027cae0` and the six component factories — slots 424–430

_Recovered 2026-09-13, story 29c-1._

`CreateComponents` (slot 424) is a fail-fast chain over six factory virtuals, and its ORDER is not
the slot order: **senses (425), motor (427), local navigator (428), move probe (426), navigator
(429), pathfinder (430)**. Each result is stored before it is tested and the first null returns
false, which is how a failed factory aborts the whole NPC. Past the six, the body writes `this` into
`+0x5cf8` (the pathfinder's owner), threads the local navigator's second-base pointer
(`localnav + 0x10`) into the motor via `thunk_FUN_102e0ba0`, threads the navigator's
(`navigator + 0x10`) into the local navigator via `thunk_FUN_102ddc00`, and finally calls the
navigator's own `vtable+0xc` with `DAT_1093407c` — the global node graph. The two null guards before
those thunks are dead: the chain has already returned false on either being null.

Each factory is one allocation and a handful of stores, and the byte size is the identity of the
type it makes:

| slot | class | body | bytes | constructor |
|---|---|---|---|---|
| 425 `CreateSenses` | `CAI_BaseNPC` | `0x1027cc10` | `0x88` | three chained sub-objects, `0x1027f8f0` each |
| 426 `CreateMoveProbe` | `CAI_BaseNPC` | `0x1027cef0` | `0x14` | none — four stores |
| 427 `CreateMotor` | `CAI_BaseNPC` | `0x1027cec0` | `0x6c` | `0x102e0900` |
| 427 | `CAI_BaseHumanoid` | `0x10260f40` | `0x70` | `0x102e0900`, then `vftable_CAI_HumanoidMotor` |
| 428 `CreateLocalNavigator` | `CAI_BaseNPC` | `0x1027cf60` | `0x20` | `0x102ddab0` |
| 428 | `CNPC_VRat` | `0x103ad6a0` | `0x20` | `0x103ad540` |
| 429 `CreateNavigator` | `CAI_BaseNPC` | `0x1027cf90` | `0x68` | `0x102eca50` |
| 429 | `CAI_BaseHumanoid` | `0x10262430` | `0x6c` | `0x102eca50`, then `vftable_CAI_HumanoidNavigator` |
| 430 `CreatePathfinder` | `CAI_BaseNPC` | `0x1027cfc0` | `0x18` | none — six stores |

Two shapes recur. Both humanoid overrides are their base **plus four bytes**, call the same base
constructor and then install a derived vtable pair (the primary at `[0]` and the `_at16` adjustor
table at `[4]`) — so the humanoid variants are the base object with one extra word, not separate
types. `CNPC_VRat`'s local navigator is the **same size** as the base's with a different constructor,
which is why a size alone does not identify a row. `CreateSenses` also seeds three retail sense
intervals — `0.15`, `0.25`, `0.45` — through `0x1027cda0` / `0x1027cd50`, and a sentinel `-1` plus
`3072.0f` (`0x45400000`) at `[3]`/`[4]`.

### `0x102c7490` — `CAI_StandoffBehavior::vfunc4`

_Recovered 2026-09-13, story 29c-1._

The standoff's reaction-timer reset, and the write half of a pair with `vfunc5` (`0x102c7530`). It
latches `+0x4c` (the "saw a new enemy" byte), zeroes the reaction counter at `+0x48`, and recomputes
`+0x3c` from `curtime`: when `+0x44` equals `0.0f` (`_DAT_104454c4`) it adds `+0x40` alone, otherwise
it draws `RandomFloat(+0x40, +0x44)` — the same "a zero max means no range, use the min" convention
the slot-13 selector uses on the same two words.

The load-bearing part is the next two lines: it copies the owner's `m_flDistTooFar` (`+0x5de4`) into
the behaviour's `+0x50` and stands `FLT_MAX` (`0x7f7fffff`) in the NPC's own field. `vfunc5` copies
`+0x50` back on the way out. So **a standoff parks the NPC's too-far distance for its whole
duration**, during which no enemy is ever too far. Finally, when the behaviour's `+0x1a` byte is set,
the owner's `+0x1fc` is forced to `1` — `vfunc5` forces the same word to `2`. **Unrecovered:** what
`+0x1fc` is, and what writes `+0x1a` or `+0x20`.

### `0x10348ba0` — slot 296, the grapple predicate

_Recovered 2026-09-13, story 29c-1._

Three terms and a literal: `m_GrappleRole` (`+0x153c`) is not `-1`, `m_GrapplePartner` (`+0x1538`)
is a live handle (index `& 0x1fff`, generation `>> 0xd`, non-null entity), and the argument equals
`0xb`. Anything else is false. The three grapple terms are exactly the "structurally paired" test the
port records on `+0x1534`..`+0x1558`. **Unrecovered: what `0xb` is.** Slot 296 has no dispatch site
anywhere in the image, so nothing states the argument's domain, and it is not a `m_GrappleType`
value — those run `0`..`8`.

### `0x102c1c10` — `GetExpressionEventParams`, over the base `0x10014ba0`

_Recovered 2026-09-13, story 29c-1._

`CBaseCombatCharacter`'s base (`0x10014ba0`) answers exactly two events, both named `"Knockback"`
into a 64-character buffer: event `0` gives `(1.0, 0.1, 1.0, 0.2)` and event `1` gives
`(1.0, 0.25, 2.0, 0.5)`. Its `DevWarning("Unhandled Expression Event: …")` is **unreachable** — the
range test admits only `0` and `1` and both are handled above it — which is the compiler's switch
default and says the enum once had more members.

`CAI_BaseNPCTroika` overrides only event `1`, and keeps three of the base's four floats: the name,
`1.0`, `0.25` and `0.5` are unchanged and the **third** float becomes `0.8` plus the duration of
`SelectHeaviestSequence(m_knockbackType (+0x6068), -1)` when that lookup is non-negative. So the
third float is how long the expression holds, and the override makes it track the knockback
animation instead of a flat two seconds. Event `0` is not special-cased and falls to the base.

### `0x1029f8d0`, `0x1037a450`, `0x103ab4a0`, `0x103e1280` — `OnVictimHitByMe`, four lines

_Recovered 2026-09-13, story 29c-1._

Slot 24's Troika-line body is fourteen bytes: `thunk_FUN_1028b160(&this->field_0x6028)`, which zeroes
five twelve-byte `MeleeMoveRecord_t` entries in one loop. **The victim argument is read by nothing.**

Three species replace it, and only one of the three keeps it:

- `CNPC_VGargoyle` (`0x1037a450`) calls slot 266 **on the victim** (`MOV ECX,ESI` at `0x1037a54a`,
  the Gargoyle pushed as the one argument) if and only if the victim's classname is `pillar` or
  `central_pillar`, and does nothing at all otherwise — the base record clear never runs for a
  Gargoyle. The control flow inverts twice; the two pointer-identity `JZ 0x1037a547` and both `SETZ`
  tails land on the *dispatch*, and only "neither name matched" reaches the return at `0x1037a552`.
  **29c's walk has this backwards** ("skips the base hit reaction when the classname is pillar");
  corrected here. **Unrecovered:** what a pillar's class fills slot 266 with — on the `CAI_BaseNPC`
  line it is the flinch-record clear (`0x100997f0`), but a prop is a different hierarchy.
- `CNPC_VSabbatLeader` (`0x103ab4a0`) resolves `m_hClosestPlayer` (`+0x628c`) and, when the resolved
  entity **is** the victim, decrements `m_RoarAttackCount` (`+0x66e0`) and clamps it at zero. The
  decrement sits inside the condition's second term, so a hit on anything else costs nothing. The
  base body does not run.
- `CNPC_VZombie` (`0x103e1280`) is the only one that keeps it, and the ORDER is the fact: the Troika
  body first, then `m_OnAttackedVictim` (`+0x66e8`) fired with the victim as the activator.

### `0x10381c00` / `0x10381ca0` — `CNPC_VHengeyokai`'s carry form bit

_Recovered 2026-09-13, story 29c-1._

The setter takes a bool. On the **false** arm it clears `m_bfAINPCFlags` (`+0x14b8`) bit `0x20`
(`CARRYING_BODY`) and nothing else — the fish timer and the throw byte keep whatever the last pickup
left. On the **true** arm it raises the bit, draws `RandomFloat(5.0, 8.0)`, clears `m_bDidFakeThrow`
(`+0x667d`) and stores `curtime + draw` into `m_flFishTimer` (`+0x666c`), in that store order. The
reader (`0x10381ca0`) is one compare: `m_flFishTimer <= curtime`. Together they are a carry window —
the Hengeyokai may fake-throw once the timer has run out, and picking a body up re-arms it and
forgets any fake throw already made. `0x10381c00` is the only writer of `+0x666c` in the image.

### `0x103dd8b0` / `0x103dd9a0` — `CNPC_VYukie`'s ungated melee pair

_Recovered 2026-09-13, story 29c-1._

Slots 599 and 601 for `CNPC_VYukie`, against the Troika line's `0x102b5650` / `0x102b5880`. The
enter body has **no gates at all** — no frenzy bit, no follower boss, no can-enter timer, no range
term, no height term, no attack coordinator: it fires the global melee event `(*DAT_10924edc)`,
sets `m_bInMelee` (`+0x6078`) and arms `m_flMeleeMustLeaveTimer` (`+0x6074`) to
`curtime + RandomFloat(22.5, 45.0)`, three times the Troika line's `7.5`..`15.0` window, and always
answers true. The leave body is the Troika line's with its **last** line dropped: the event, the
`m_bInMelee` clear and the `HasUsableRangedWeapon()`-gated `m_flMeleeCanEnterTimer` re-arm
(`curtime + RandomFloat(5.0, 10.0)`) are identical, and there is no attack-coordinator release —
she never took a slot, so she never gives one back.

### `0x1036e400` — `CNPC_VChangBros::StoreArenaCenter`

_Recovered 2026-09-13, story 29c-1._

Name recovered by `NameFromStrings` at `0x106313a0`. It walks the global `CAI_Hint` list from
`DAT_10925450` along `+0x5d8` for the first node whose `m_nHintType` (`+0x5dc`) is `0x4651`, takes
that node's `GetAbsOrigin` (`vtable+0x364`) into `m_vArenaCenter` (`+0x66dc`, three floats) and
raises `m_bCenterStored` (`+0x66e8`). **Both writes are inside the found arm** — a map with no
`0x4651` hint leaves the flag false and the centre untouched, which is the state the Chang fight's
own guard reads. It is the same walk the squad family's `NthHintOfType` seam stands for.

### `0x103cade0` — the Werewolf zone opener

_Recovered 2026-09-13, story 29c-1._

Three hardcoded map entity names and no parameter. It sweeps every entity whose **targetname** is
`trigger_werewolf_zone` — `0x100f7380` compares `entity+0x11c` (`m_iName`), not `+0x26c`
(`m_iClassname`) — and dispatches `vtable+0x3ec` (slot 251) on each. It then resolves `rotdoor1` and
`rotdoor2` through the five-argument name lookup (`0x100f7770`, the form that understands `!self` /
`!activator`, passing the Werewolf as both searching entity and activator), RTTI-casts each to
`CFuncMoveLinear`, and stores the handle in `m_hRotDoor1` (`+0x6684`) / `m_hRotDoor2` (`+0x6688`) —
or the invalid handle when the cast fails. The cast is load-bearing: a map that names something else
`rotdoor1` disarms the door rather than crashing later. Retail name unrecovered; the name here is
inferred from the entity names. **Unrecovered:** what slot 251 is on a `trigger_werewolf_zone` — on
the `CAI_BaseNPC` line the index is `IsActivityFinished` (`0x10272900`), but a trigger is a different
hierarchy sharing the index and the census carries no table for it.

## Story 29c-1, family Species — the per-species bodies of layers 0–9

_Recovered 2026-09-13, story 29c-1._

Sixty rows over eight retail classes. Every `.rdata` constant quoted below was read out of the
pinned retail `vampire.dll` (image base `0x10000000`, `.rdata` VA `0x10445000`, raw `0x445000`), so
the numbers are recovered and not inferred; where a datum lives in `.data` and is filled at runtime
the section says **Unrecovered**.

### Slot 323 — the movement-direction code, `0x10344dd0`

Seventy-eight classes share the one body. It takes the 2-D length of the direction vector, refuses
below `_DAT_1049e028` = **1e-07** by answering `0`, flattens Z, and folds
`UTIL_VecToYaw(dir) - GetAbsAngles().y` through `UTIL_AngleMod` — the `ftol`/`& 0xffff`/`* 360/65536`
triple, `_DAT_1044ffdc` = **0.0054931640625**. The resulting angle in `[0, 360)` picks one of four
codes against `_DAT_1049949c` = **45**, `_DAT_1049e8a0` = **135**, `_DAT_1049e89c` = **225** and
`_DAT_1049e8a4` = **316**:

| band | code |
|---|---|
| `(316, 360)` and `[0, 45]` — the body's own heading | 2 |
| `(45, 135]` — to its left (Source `+yaw` is counter-clockwise) | 3 |
| `(135, 225]` — behind it | 0 |
| `(225, 316]` — to its right | 1 |

**316, not 315**: the four bands are 89, 90, 90 and 91 degrees wide. The decompiler lost the
subtraction between `VecToYaw` and `GetAbsAngles` — both calls are present with their results
dropped on the FPU stack and a bare `__ftol()` after them — and the reading is settled by the
arithmetic that survived: a body that bucketed an ABSOLUTE yaw would answer the same code for the
same world direction whichever way the NPC faced, which is not a direction code.

### The two `CUtlVector<{EHANDLE, float}>` blacklists — `0x103662d0`, `0x10366400`, `0x10366490`, `0x103bf200`, `0x103bf330`, `0x103bf3c0`

One store written twice: `CNPC_VBaseBoss::m_BlacklistedEntities` at `+0x665c` (allocation count
`+0x6660`, grow size `+0x6664`, element count `+0x6668`, `CUtlMemory` mirror `+0x666c`) and
`CNPC_VTzimisce`'s own at `+0x6690`/`+0x6694`/`+0x6698`/`+0x669c`/`+0x66a0`. Three bodies each:

* **add** — a hand-inlined `InsertBefore` at the end of the list. The doubling growth (first
  allocation 4, then `cap * 2` or `cap + growSize`) is followed by a `memmove` of
  `(count + 1 - index - 1) * 8` bytes with `index == count`, i.e. **zero** — the shift is dead on
  every call and the insert is an append. The boss form takes the duration as a parameter; the
  Tzimisce form hardcodes `_DAT_1044eb0c` = **20.0** seconds, the same cell
  `CNPC_VHengeyokai`'s blacklist and MingXiao's thrown-object skip use.
* **index-of** — a linear walk resolving each row's `EHANDLE` through `PTR_DAT_10566458` (sentinel
  `0xffffffff`, serial in the top 19 bits) and answering the index whose RESOLVED POINTER matches,
  else -1. A dead row resolves to **0**, so a null argument matches the first dead row.
* **test-and-expire** — `-1` answers false; `curtime < expiry` answers true and leaves the row
  standing; otherwise the LAST row is memmoved over the one being dropped and the count
  decremented. A **swap-remove**, so the list's order is not preserved. Both bodies return
  `index & 0xffffff00` on every non-"still live" path, so the sign of the index never leaks out and
  every one of them reads as false.

### The melee quartet's species replacements — slots 599, 600, 601, 602

`MeleeSlotLine` answers `EMeleeSlotLine::Species` for six classes; these are their bodies. Every one
is a replacement, not a wrapper — none calls the Troika line.

**Slot 599's argument is `GetEnemy()`, and it is a `CBaseEntity*`.** `signatures.md` types the slot
`bool vfunc599(int)` because `CAI_BaseNPCTroika::0x102b5650` reads the parameter with no
instruction, but `CNPC_VTzimisceRunner`'s `0x103c3960` casts it and calls `GetRefEHandle` on it. The
callers settle it: `CAI_BaseNPCTroika::SelectScheduleMeleeCombat` `0x102b6c30` is
`CALL [EAX+0x29c]` / `MOV EDI,EAX` / … / `PUSH EDI` / `CALL [EDX+0x95c]`, and
`CNPC_VTzimisceRunner::SelectScheduleMeleeCombat` `0x103c4430` is `CALL [EAX+0x29c]` / `PUSH EAX` /
`CALL [EDX+0x95c]`. `+0x29c` is slot **167**, the const `GetEnemy()` (`0x101a67e0`), not the Troika
line's mutable slot 168. The same EDI is pushed into `+0x964` (slot 601) at `0x102b6c64`, so 601's
argument is the same enemy.

* **`CNPC_VFrenzyShadow` `0x10376b70` / `CNPC_VGargoyle` `0x10379ef0` (599)** and
  **`0x10376ba0` / `0x10379f20` (600)** — byte-identical pairs, 26 and 23 bytes. Fire the global
  melee event through `(*DAT_10924edc)->vfunc1()` and set `m_bInMelee`. **Every gate is gone**: the
  frenzied bits, the melee-enter timer, the range and height terms, the weapon-capability word and
  the attack coordinator. These two species enter melee unconditionally and can never be refused,
  and neither arms `m_flMeleeMustLeaveTimer`. The 599 pair writes the flag SECOND and the 600 pair
  writes it FIRST; nothing observes the order.
* **`CNPC_VTzimisceHeadClaw` `0x103c19e0` (599)** — the coordinator alone (`0x1025db70`): on success
  the event, `m_bInMelee = 1` and `m_flMeleeMustLeaveTimer = curtime + RandomFloat(7.5, 15.0)`
  (`0x40f00000` / `0x41700000`, the Troika line's own pair); on refusal `m_bInMelee = 0`.
* **`CNPC_VTzimisceRunner` `0x103c3960` (599)** — the same, plus `m_hPotentialEnemy` (`+0x6678`)
  cached from the argument **on both arms, refusal included**, and **without** arming the leave
  timer. It is the one slot-599 body in the family that reads its parameter.
* **`CNPC_VTzimisceHeadClaw` `0x103c1a60` (600)** — the event fires FIRST and unconditionally, so a
  body already in melee still fires it; then the coordinator request (`0x1025dca0`) under an
  `m_bInMelee == 0` guard. The guard is on the way IN and the clear is outside it, so calling this
  on a body already in melee takes it back OUT. **No leave-timer re-arm** — a real divergence from
  the base.
* **`CNPC_VTzimisceRunner` `0x103c39e0` (600)** — `0x103c1a60` with `m_hPotentialEnemy` cached
  between the event and the guard.
* **`CNPC_VYukie` `0x103dd900` (600)** — not a melee body at all. It keeps the weapon-capability
  gate (`weapon->slot360() & 0x18000`) and the `m_bInMelee` latch, drops the coordinator, and arms
  `+0x6074` with `curtime + RandomFloat(22.5, 45.0)` — immediates `0x41b40000` and `0x42340000`;
  **22.5, not 22.0**. The latch is never cleared by this body, so it is one-shot.
* **`CNPC_VTzimisceHeadClaw` `0x103c1ad0` / `CNPC_VTzimisceRunner` `0x103c3a70` (601)** — the event,
  `m_bInMelee = 0` and an **unguarded** `ReleaseMeleeSlot` (`0x1025ddd0`). The Troika line's
  `HasUsableRangedWeapon()` test and its `m_flMeleeCanEnterTimer` re-arm are gone, so either species
  may re-enter melee on the next pass. The runner's form additionally clears `m_hPotentialEnemy` to
  the invalid handle, completing its matched set.
* **`CNPC_VTzimisceHeadClaw` `0x103c1b10` / `CNPC_VTzimisceRunner` `0x103c3ab0` (602)** —
  byte-identical, 83 bytes, and they keep only the FAR arm:
  `2 * MeleeRange < m_flEnemyDist && !CoordinatorHasRoom()` answers true, else
  `CoordinatorDoesNotHoldMe()`. The frenzied gate, the follower-boss gate, the null-coordinator
  gate, the `HasUsableRangedWeapon()` split and the `m_flMeleeMustLeaveTimer` deadline are all gone:
  neither species leaves melee because a timer ran out.

### Slot 606 — `CNPC_VBach`'s fire gate, `0x10364280`

An arm-then-fire hysteresis around `COND_ENEMY_OCCLUDED` (`0x48`). Without the condition it clears
`m_bFireOccluded` (`+0x66a3`) and answers 0; with the condition it answers 0 and ARMS the flag on
the first pass, and only delegates to the Troika body (`0x102b8320`) once armed. The flag is cleared
the moment the condition drops, so the one-pass delay is re-paid every time the enemy goes behind
cover — hysteresis, not a one-shot.

### How a species body reaches the body it replaces

Four of the species bodies in this section END in a call to the very slot they override:
`CNPC_VBach`'s 606 delegates to `thunk_FUN_102b8320`, `CNPC_VTzimisce`'s 593 runs
`thunk_FUN_1029a070` first and then overwrites all five words it wrote, `CNPC_VZombie`'s 510 tail
jumps into `CAI_BaseNPC::ShouldPlayFloatSound` (`thunk_FUN_1027a530`) and the five slot-482 copies
are `CAI_BaseNPC::CanPlaySequence` (`0x10278090`) instruction for instruction.

**Every one of those is a DIRECT call, never a vtable dispatch** — a `thunk_` to the base class's
own body — so it lands on the base and can never re-enter the species body. Retail gets that for
free from having two functions per slot (the base's and the override's). A port that stands ONE
function per slot, with the species resolution as a prologue at the top of the base body, has to say
it: while slot N's species body is running, slot N's prologue must decline. That is what
`FElysiumNpc::SpeciesDispatchingSlot` is, and it is per slot rather than a depth count because
retail's thunks are per body too.

### Slot 609 — the three state gates, `0x103661f0`, `0x10367740`, `0x103b26f0`

Byte-identical on `CNPC_VBach`, `CNPC_VBatSwarm` and `CNPC_VSheriffSwarm`, 44 bytes each. Only
`m_NPCState` 4 (`NPC_STATE_SCRIPT`) or `0xc` reaches the Troika hint search (`0x102b6b50`); every
other state **zeroes `m_pShootAtHintNode` (`+0x6444`) as a side effect of asking** and answers null.

### Slot 482 — five species, one byte-identical copy — `0x1035fd40`, `0x103bd270`

`CNPC_VAnimal` (with `CNPC_VDog`, `CNPC_VRat` and two more) and `CNPC_VTzimisce` each carry their own
`CanPlaySequence`, and every copy is instruction-for-instruction the base `0x10278090` above: the
cine-handle resolve, the `0x101a8ac0` upgrade from 1 to 2, the `IsAlive()` gate at vtable `+0x278`
and the four-term state refusal. Five vtable entries, one behaviour.

### `CNPC_VTzimisce`'s carry chain — `0x103be0b0`, `0x103be150`, `0x103be3d0`, `0x103be8e0`, `0x103bea90`, `0x103bef20`

* **`0x103be0b0`** is `CNPC_VHengeyokai`'s `0x10381c00` on a different class: setting raises
  `m_bfAINPCFlags` `CARRYING_BODY` (`0x20`), arms `m_flBodyTimer` (`+0x66a4`) with
  `curtime + RandomFloat(7.5, 10.0)` (`0x40f00000` / `0x41200000`) and clears `m_bDidFakeThrow`
  (`+0x66b4`); clearing writes **only** the flag, leaving the timer and the byte standing. The draw
  happens before `curtime` is read.
* **`0x103be150`** is `m_flBodyTimer <= curtime` — at-or-before, so a timer armed at exactly
  `curtime` reads as already elapsed, and an NPC that has never carried anything answers true.
* **`0x103be3d0`** is family Bosses' `0x10381e90` over the `PTR_s_Bip01_L_Forearm_106530e8` bone
  table, with **no fallback arm when the RTTI cast fails**. The initial best is `1050625.0` —
  **1025 units squared** — so a bone further than 1025 units never wins. The winner's position goes
  to `m_vecPickupTargetPos` (`+0x6674`) and its table index to `m_iPickupTargetGrabBone` (`+0x6680`).
  **Unrecovered:** every entry of the bone table after the first.
* **`0x103be8e0`** is a **+-20 degree FACING CONE**, not a range test: `thunk_FUN_1013d580` is
  `UTIL_AngleDiff` and `*(float *)(iVar8 + 4)` is `GetAngles().y`, so the two comparisons against
  `_DAT_1049ae98` = **-20.0** and `_DAT_1044eb0c` = **20.0** are the same cone
  `CNPC_VHengeyokai`'s `0x103822a0` uses off the same cells. A NULL target answers **true**.
* **`0x103bef20`** attaches the `phys_animlink`. Its one distinguishing number is a range gate the
  two boss attach arms do not have: `distSq <= _DAT_104cc51c` = **25600.0**, i.e. **160 units**.
  The tail calls `0x103be0b0(true)`, so a successful attach is what raises `CARRYING_BODY`.
* **`0x103bea90`** releases it and throws. `UTIL_Remove` the link, clear `m_hPhysicsAnimlink`, then
  with an aim target: the lead point lifted **48 units** (`_DAT_10447ee8`), the same +-20 cone
  deciding which side to nudge toward, a speed floored at **1000.0** (`_DAT_10447ee0`, and the
  literal `0x447a0000` on the other arm is the same 1000), and the impulse applied through
  `CRagdollProp`'s vtable `+0x428` or the physics object's `ApplyForceCenter`. It always ends on
  `m_hPickupTarget = -1`, `ArmIgnoreCollisionExpiry(0.75)` and `0x103be0b0(false)`.

### `CNPC_VNewscaster`'s two story queues — `0x103a0d50`, `0x103a0ff0`

Two fixed-stride `0x28` arrays — Main at `+0x665c` (count `+0x6668`) and Side at `+0x6670` (count
`+0x667c`) — with cursors at `+0x6684` and `+0x6688` and a selector at `+0x668c`.

`0x103a0d50` drains both by **FRONT removal**: each pass releases the record's own object plus eight
handles (`+0x04`/`+0x08` at four strides of 8, up to `0x20`), then memmoves the whole remainder down
by one `0x28` record. It clears the story-active flag at `+0x6690` last. `0x103a0ff0` draws the
overlay: `"not playing VCD"` when the handle at `+0x6554` fails to resolve, else
`"Main Stories (%d)"` and `"Side Stories (%d)"` with one row each through `0x103a0eb0`. **The two
highlight predicates are opposites and that is what `+0x668c` means**: a Main row is current when
`+0x668c == 0` and a Side row when `+0x668c != 0`, so the word selects which queue is playing.

### `CNPC_VWerewolf`'s door pair and chase cache — `0x103d1e50`, `0x103d9c90`

`0x103d1e50` short circuits on `m_DoorState` (`+0x6680`): 2 answers true, 0 answers false, and only
in between does it measure. The measurement is Source's **octagonal length approximation** — the
largest axis plus `_DAT_1044bef8` = **0.25** of the other two summed, not a Chebyshev distance —
between the world-space centres of `m_hRotDoor1` (`+0x6684`) and `m_hRotDoor2` (`+0x6688`), the pair
the Werewolf zone opener fills. The answer is the NEGATION of
`distance <= _DAT_104cf498` = **135.0**, so true means the halves are apart.

`0x103d9c90` memoises the chase position on `gpGlobals->framecount` (`DAT_1070b22c + 0x1e0`) at
`+0x6670`, with the point at `+0x6674`/`+0x6678`/`+0x667c`. Two details: the frame stamp is written
**whether or not there was an enemy**, so a werewolf with none answers the previous frame's point
for the rest of this frame; and the goal tolerance (`+0x6320`) is read into a local, handed to
`0x102c3b50` and then dropped.

### `CNPC_VZombie` — `0x103e0980`, `0x103e1080`, `0x103e12c0`, `0x103e12f0`

`0x103e0980` is the writer behind the `ZombieAIType` keyvalue (`+0x6678`). **4 is the "pick one for
me" value and is never stored** — it is rerolled by `RandomInt(1, 3)`, inclusive, so a mapper who
writes 4 gets 1, 2 or 3. The values that then push a scripted order (`0x102ae840(this, 1, false)`)
are **2, 3, 5 and 6** — not a contiguous band, and 1 and 4 are the two that do not, which means 4
can reach the side effect through its reroll.

`0x103e1080` is slot 510. `m_iFloatSoundFrequency` (`+0x10e8`) is set to **9 first and on every
call**, before any gate. The gates then are: not already moaning (`0x102c1170`); no live cached
float sound (`+0x1538` resolves and `+0x153c != -1`); not unconscious; `m_bfAINPCFlags` `SLEEPING`
(`0x20000`) clear; a resolvable `m_hClosestPlayer` whose own `+0xfe8` does NOT resolve; and finally
`m_flPlayerDist <= _DAT_10940490`, which is lazily read once out of a `"Float Sound Info"` KeyValues
block under the `DAT_10940495` bits 1 and 2. Only then does it chain to the base `0x1027a530`.
**Unrecovered:** `_DAT_10940490` — it lives in `.data` and is filled at runtime, so it is not a
`.rdata` cell that can be read out of the image.

`0x103e12c0` and `0x103e12f0` are byte-identical: both fire `m_OnAttackedVictim` (`+0x66e8`, mapper
key `OnAttackedVictim`) with the victim as ACTIVATOR and this NPC as CALLER, and **neither forwards
to its base**, so whatever slots 25 and 26 do for every other class does not happen for a zombie.

### `CNPC_Crow`'s two bodies — `0x103577d0`, `0x10357be0`

`0x103577d0` is the whole of slot 197: it dispatches slot 192 (`WorldSpaceCenter`) on itself,
**discards the result** and answers its own argument back. So a crow's body target is wherever the
caller asked, never its own centre — the opposite of every other class at that slot.

`0x10357be0` flies toward the entity at `m_pHintNode` (`+0x5ddc`). Three facts that each look like a
slip and are not: the scale clamp is `if ((float)_DAT_10449280 < scale) scale = 1.0` and
`_DAT_10449280` read as a FLOAT is **0.0**, so **any positive scale is replaced by exactly 1.0**;
the offset is NOT normalised before it becomes a velocity, it is the raw delta scaled by
`_DAT_10454028` = **170.0**; and the arrival latch at `+0x5f54` compares a LENGTH (not a squared
length) against `scale * 170` using the pre-avoidance offset. The literal `0x432a0000` handed to the
avoidance step is the same 170. With a null hint the body reads its own origin and zeroes the
velocity.

### `CNPC_VTzimisce`'s slots 488 and 593 — `0x103b92a0`, `0x103b9180`

`0x103b92a0`'s decompiled C is damaged; read off the listing, it takes three world singletons
(`DAT_1093cf94`, `DAT_1093cfdc`, `DAT_1093cebc`), substitutes **0** for each whose own `vtable+0x4`
answers true and otherwise hands over `+0x2c`, `+0x2c` and `+0x28`, fires `"SPI_DIES"` on the script
host at `+0x2e0` with `(c, b, 0, a)` — the third singleton first, the literal zero between the
second and the first — and then **`JMP`s**, not calls, into slot 487 (`vtable+0x79c`), so the base
body runs on this frame and its return is what the caller sees.

`0x103b9180` calls the base (`0x1029a070`) and then overwrites the five `m_flTargetLead*` words
(`+0x655c`..`+0x656c`) with `0x3c23d70a` = **0.01**, `0x3f800000` = **1.0**, `0x42480000` = **50.0**,
`0x42480000` = **50.0** and `0x3c23d70a` = **0.01**. The base runs first, so its own values are
unobservable on a Tzimisce.

### The remaining species one-liners

`0x1036c7f0` sets `CNPC_VChangBros::m_ChangType` (`+0x66b8`) with no clamp and no validation.
`0x1039ef90` calls a discarded `ConVar` read (`DAT_10924a6c`), raises condition **0x78** — one past
the base registrar's `0x00..0x76` namespace, with **no name in the recovered vocabulary** — and
caches three floats at `+0x668c`/`+0x6690`/`+0x6694`. `0x1039e800`, `0x1039e830` and `0x1039e860`
are three byte-identical `CNPC_VMingXiaoTentacle` slots (21, 22, 23) that fetch the head through
`0x1039ede0` and forward `(this, param)` to `0x10397dd0` on it, answering nothing themselves.
`0x103bf560` is `SetIdealYaw(m_pMotor, -1)`. `0x103c24a0` is `0.0 < m_flSlowedExpire` (`+0x6678`) —
strictly greater and against **zero**, not `curtime`, so a head claw slowed once answers true for
ever until the word is written back. `0x103c3fd0` is `RestartIdealActivity(1)` with the base
override's `IsActivityFinished` gate **removed**, so a runner restarts mid-clip. `0x103681d0` and
`0x103682f0` are one byte each — `CNPC_VCamera` replaces two sound hooks with nothing.
`0x1035e920` is `CNPC_VAndreiBlood::m_iActiveRunnerCount (+0x66b8) < _DAT_10452dc4` = **2.0**, and
`0x1035e950` draws `m_iHitMax` (`+0x66dc`) as `RandomInt(2, 4)`, inclusive. `0x1034b430` is
`CNPCMaker`'s `!m_bInfChild (+0x66c3) && m_iMaxNumNPCs (+0x6660) < 1`, which the port already
carried verbatim as `FElysiumNpcMaker::IsDepleted`.

**Unrecovered:** `_DAT_10940490` (`CNPC_VZombie`'s float-sound distance, a runtime KeyValues read),
the `DAT_10924a1c` melee-range ConVar's default, `DAT_1093ca8c` and `DAT_1093ca44` (the two ConVars
`0x103be8e0` and `0x103bea90` scale by), `DAT_10924a6c` (the discarded read in front of every
`SetCondition`), the name of condition `0x78`, and every entry after the first of
`CNPC_VTzimisce`'s grab-bone table.

## Story 29c-1, family Geometry — where a body sits and how big it is

_Recovered 2026-09-13._ The six eye/anchor slots, the hull-bit query, `SetSize`, and the four bodies
that push one body out of another. Ported in `Substrate/ElysiumNpcKernelGeometry.inl` /
`.cpp`, tested by `Elysium.Substrate.NpcKernelGeometry.*`.

Ten of the family's cells were only ever named in the decompiled C and are settled here by reading
them out of the pinned image (base `0x10000000`, `.rdata` VA `0x10445000` at file offset
`0x445000`): `_DAT_1044bef8` = **0.25**, `_DAT_1046bac0` = **6.0**, `_DAT_1049aea8` /
`_DAT_1049aea4` = **+0.707 / -0.707**, `_DAT_1049a1f4` = **0.1**, `_DAT_1049a1f8` = **40.0**,
`_DAT_10462914` = **1.25**, `_DAT_10457f5c` = **500.0**, `_DAT_1046dcd0` = **128.0**,
`_DAT_104454c4` = **0.0** and `_DAT_10449260`, which is a **double** and reads **0.25** — as a float
that cell is 0.0.

`DAT_1070d1b0` is **`vec3_origin`**: 371 readers, one writer (the static initialiser `0x101370b0`),
and it lives past `.data`'s raw size. Three reads of it in one body below each mean something else.

### The eye points — `0x100b4b40`, `0x101aae60`, `0x1025e8e0`, `0x100b4bc0`, `0x100b4be0`

Slot 193 `EyePosition`'s Troika-line body (`0x100b4b40`, 85 bytes, 24 call sites) is
`GetAbsOrigin()` (slot 217) plus `m_vecViewOffset` (`+0x0184`), component by component — a FIXED
per-entity offset, not a bounds fraction and not a head bone. Two classes in the `CAI_BaseNPC`
census replace it. `CPayphone::vfunc193` (`0x101aae60`) looks up the bone `"Phone_bone_01"` and
answers its world position with no view offset added and the bone angles discarded; a `LookupBone`
answer of -1 falls through to the base body, so a payphone model without that bone still has an eye.
`CAI_BaseHumanoid::vfunc193` (`0x1025e8e0`) refreshes a cache through `0x1025e7b0` and answers the
three words at `+0x5f50`. That refresh is lazily latched on two bits of `+0x5f4c`: bit 1 guards the
eye point, which comes from a NAMED attachment (`&DAT_105c8ed8`) and falls back to
`CBaseEntity::EyePosition()` plus `GetAngles()` when the model has no such attachment, and bit 0
guards a second cached vector from that eye to slot 278 (`+0x458`). No class in the spawnable census
derives from `CAI_BaseHumanoid`.

Slots 194 and 195 are eight bytes each and have no frame: `0x100b4bc0` is
`MOV EAX,[ECX]; JMP [EAX+0x36c]` and `0x100b4be0` is the same with `+0x374`. `EyeAngles()` **is**
slot 219 `GetAbsAngles()` and `LocalEyeAngles()` **is** slot 221 `GetAngles()`; 82 and 80 classes
fill them and every one with that tail call. There is no separate head orientation at these slots.

**Unrecovered:** the attachment name behind `DAT_105c8ed8`, and `CAI_BaseHumanoid`'s `+0x5f4c` latch
bits beyond which half each guards.

### Slot 197 `BodyTarget` — `0x102789c0`

518 bytes, and the decompiled C is unusable (`unaff_EDI`, `unaff_retaddr`, a `pfStack_8` written
before it is read); read off the listing. **Retail's `const Vector& posSrc` reaches no instruction.**
The body's `RET 0x10` covers the struct-return buffer, that reference and the two bools, and only
the two bools and `this` are ever loaded.

    A      = WorldSpaceCenter()                       // slot 192, vtable +0x300
    B      = GetAbsOrigin()                           // slot 217, vtable +0x364
    C      = WorldSpaceCenter()                       // slot 192 AGAIN
    anchor = C - (A - B) * 0.25                       // _DAT_1044bef8
    eye    = EyePosition()                            // slot 193, vtable +0x304
    span   = eye - anchor

    if  (bNoisy)      return anchor + span*RandomFloat(0,0.5) + span*RandomFloat(0,0.5);
    if  (bThirdBool)  return eye;
                      return anchor + span * 0.5;     // _DAT_104454d0

Two facts the shape of the expression hides. The noisy arm makes **two independent draws and adds
the span once for each**, so its blend parameter spans 0..1 with a triangular distribution, not the
uniform 0..0.5 one draw would give — an NPC's aim point can reach the eyes exactly. And the second
bool is tested FIRST (`10278a84`), so a call with both bools set takes the noisy arm and never looks
at the third. The anchor is the bounds centre pulled a quarter of the way back down toward the feet,
which is a chest point on a humanoid.

### Slot 192, `CNPC_Crow`'s override — `0x10357760`

Three chained dispatches of slot 220 `GetOrigin()` (vtable `+0x370`) whose results are kept in EDI,
EBX and EAX, and then `out = (EBX[0], EDI[1], EAX[2] + _DAT_1046bac0)` — X from the second call, Y
from the first, Z from the third. All three answer the same vector, so the whole of it is
`GetOrigin() + (0, 0, 6)`. A crow's centre is a fixed lift off its own origin rather than the base
body's bounds midpoint, which is what keeps it from breathing with the wing animation.

### Slot 213 `SetSize` — `0x100b1890`

146 bytes, of which 132 are a scope-trace push and pop: the body reads `m_iName` (`+0x026c`) purely
to label a crash-report breadcrumb (`"CBaseEntity::SetSize"`, with `"NULL ENTITY"` for a null `this`
and the empty string for an unnamed entity), pushes the row, writes `m_vecSize` (`+0x038c`) and the
two words after it, and pops. Nothing in layers 0–9 reads those words but slot 214 `GetSize`.

### Slot 337 `GetUsedHullBits` — `0x10341710`, `0x10270820`, `0x1029a050` and fourteen replacements

`CBaseCombatCharacter::GetUsedHullBits` (`0x10341710`) is a scope-trace pair and `return 1`.
`CAI_BaseNPC` (`0x10270820`) is `CALL base; OR AL,0x1; RET` and `CAI_BaseNPCTroika` (`0x1029a050`)
is the same two instructions over THAT, so **the Troika line's answer is 1 and both ORs are
no-ops** — three rungs adding the same bit.

Fourteen census classes replace it, in two shapes. Six call the chain and OR onto its answer:

| class | body | ORs |
|---|---|---|
| `CNPC_VTzimisce` | `0x103b9160` | `0x400` |
| `CNPC_VTzimisceHeadClaw` | `0x103c1cb0` | `0x800` |
| `CNPC_VTzimisceRunner` | `0x103c3cb0` | `0x2000` |
| `CNPC_VMingXiao` | `0x10392a50` | `0x38000` |
| `CNPC_VScurrying`, `CNPC_VRat` | `0x103ac4e0` | `0x80000` |

**The first three are where the decompiler lies.** Its C for all three is a bare forward with no bit
added; the listing is `CALL 0x1000be92; OR AH,0x4 / 0x8 / 0x20; RET` — a byte-wide OR into bits
8..15. Eight more answer a bare constant and never call up, so bit 0 is ABSENT from their answer:
`CNPC_VCamera` and `CNPC_VCameraSecurity` `0x80` (`0x10368e80`), `CNPC_VGargoyle` `0x4000`
(`0x10378680`), `CNPC_VHengeyokai` `0x40001` (`0x1037fb20`), `CNPC_VManBat` `0x100000`
(`0x1038b100`), `CNPC_VMingXiaoTentacle` `0x38000` (`0x1039c480`), `CNPC_VSheriffMan` `0x200000`
(`0x103ae840`) and `CNPC_VWerewolf` `0x1000` (`0x103cab50`).

**Which hull each bit names** was recovered 2026-09-20 with the table itself
(`docs/vtmb/data/hull_table.json`, `navigation-jump-links.md` § "What the shipped graphs and maps
actually use"). A row's index is its bit, so the join is direct:

| bits | hull | class(es) |
|---|---|---|
| `0x80` | 7 `TINY_CENTERED_HULL` | `CNPC_VCamera`, `CNPC_VCameraSecurity` |
| `0x400` | 10 `TZIMISCE1_HULL` | `CNPC_VTzimisce` |
| `0x800` | 11 `TZIMISCE2_HULL` | `CNPC_VTzimisceHeadClaw` |
| `0x1000` | 12 `WEREWOLF_HULL` | `CNPC_VWerewolf` |
| `0x2000` | 13 `TZIMISCERUNNER_HULL` | `CNPC_VTzimisceRunner` |
| `0x4000` | 14 `GARGOYLE_HULL` | `CNPC_VGargoyle` |
| `0x38000` | 15 + 16 + 17, the MING_XIAO trio | `CNPC_VMingXiao`, `CNPC_VMingXiaoTentacle` |
| `0x40001` | 18 `HENGEYOKAI_HULL` + human | `CNPC_VHengeyokai` |
| `0x80000` | 19 `RAT_HULL` | `CNPC_VScurrying`, `CNPC_VRat` |
| `0x100000` | 20 `MANBAT_HULL` | `CNPC_VManBat` |
| `0x200000` | 21 `SHERIFF_HULL` | `CNPC_VSheriffMan` |

That `TINY_CENTERED_HULL` — 8,986 links, the second most linked hull in the corpus — belongs to
the two security cameras is worth stating plainly: a hull nothing walks on carries a sixth of the
game's link budget.

**But this slot declares a PRECACHE SET, not the hull the body stands on.** That is `m_eHull`
(`+0x1568`), and `SetHullSizeNormal 0x10273070` reads the table row for it, sizing the body
through `UTIL_SetSize`; its guard `(Bits(m_eHull) & GetUsedHullBits()) != Bits(m_eHull)` is what
the DevMsg "`%s is using hull %s which has no ...`" reports. The two are not the same fact, and
the port must not treat them as one.

What writes `m_eHull`: the setter `0x102d7730` has exactly two callers, `0x102f7a90` and
`InitLinks 0x102fb4e0` — both graph-build code driving the `CAI_TestHull` probe, never an NPC.
Every other write is a direct store, and there are only eight: `CNPC_Crow::Spawn` 4 (`TINY_HULL`,
a hull with no link in any graph — consistent with no link flying), `CNPC_VMingXiao::StartTask`
15, `CNPC_VMingXiaoTentacle` 17 and 15 (`vfunc442` and `StartTask`), `CNPC_VHengeyokai::StartTask`
18, and `CNPC_VVampireBoss::StartTask` / `CNPC_VSabbatLeader::TransformationStart` back to 0, plus
`CGeneric_NPC`, `CGeneric_NPC_bathack` and `CGenericSabbat_NPC`'s Spawns setting 0.

**Recovered 2026-09-20** (this paragraph used to read "Unrecovered"): how `CNPC_VRat` /
`CNPC_VScurrying`, `CNPC_VCamera`, `CNPC_VGargoyle`, `CNPC_VManBat`, `CNPC_VSheriffMan` and
`CNPC_VWerewolf` acquire theirs. **A CONSTRUCTOR writes it**, and the corpus field ledger does not
record constructor accesses — which is the whole reason "no write to `+0x1568` reaches them in the
corpus" looked true. The stores, the second hull word at `+0x156c` that decides ROUTING, and the
three species whose two words differ are in `navigation-jump-links.md` § "The two hull words";
the table itself is `docs/vtmb/data/class_hulls.json`. Still open: `CNPC_VWerewolf` keeps a FAKE
hull (`UpdateFakeHull`, `CheckStuck`, `GetGroundpoint`), so what its constructor's 12 sizes and
what its fake hull sizes are two questions, and only the first is answered.

### Slot 533 `EyeOffset` — `0x10274db0`, `0x102b4ab0`

`CAI_BaseNPC::FUN_10274db0` (92 bytes) is two lines: with slot 513 (`vtable +0x804`) carrying
`0x8000000` **and** the activity equal to `0x57` or `8`, a fixed `(0, 0, 1.5)`; otherwise
`m_vDefaultEyeOffset` (`+0x5d60`). Both immediates are literals, not `.rdata` cells. Retail's second
`Activity` parameter reaches no instruction in either body.

`CAI_BaseNPCTroika::FUN_102b4ab0` (241 bytes) puts one arm in front of it, entered only with a
claimed hint node (`m_pHintNode` `+0x5ddc`) and only for six activities — `0x1119`, `0x111a`,
`0x111b`, `0x111c`, `0x111f`, `0x1120`. It is a jump table with **two holes**: `0x111d` and `0x111e`
are inside the numeric range and are not cases, so a range test would be wrong. The arm is

    pos = GetAbsOrigin();
    ApplyHintLeanOffset(&pos, /*standing*/ false);      // 0x102b6120
    HintStandPosition(m_pHintNode, this, &hint);        // 0x102d1180, hint is UNINITIALISED
    return (hint - pos) + m_vDefaultEyeOffset;

and it consults neither the debug-overlay bit nor the base body's two special-cased activities.

### `CAI_BaseNPCTroika::ResolveStandingOnHead` — `0x102bf820`

1,179 bytes; the decompiled C loses the one stack argument to `fStack_4` and the direction's two
components to `unaff_EBX`/`unaff_ESI`. Read off the listing, the argument is a **float interval** at
`[ESP+0xac]` and the body is a spring that pushes this NPC off whatever it is standing on.

Two refusals come first and both land on the same write, `m_flStandingOnHeadTimer (+0x65fc) = 0`:
`m_hGroundEntity` (`+0x384`) failing to resolve, and the resolved entity's collision object
(`+0x9c`) being null. The ramp only survives while an NPC is continuously standing on something.

    d = myOrigin - groundOrigin;  d.z = 0;              // Z FORCED to zero after it is computed
    if (d.x == 0.0 && d.y == 0.0) {                     // _DAT_104454c4, an exact float compare
        switch (RandomInt(0,3)) {                       // the four diagonals, DEC EAX order
          case 1: d = (-0.707, +0.707); break;          // _DAT_1049aea8 on Y
          case 2: d = (+0.707, -0.707); break;          // _DAT_1049aea4 on Y
          case 3: d = (-0.707, -0.707); break;
          default: d = (+0.707, +0.707);
        }
    } else {
        VectorNormalize(&d);
        d.x += RandomFloat(-0.1,0.1);  d.y += RandomFloat(-0.1,0.1);
        VectorNormalize(&d);                            // normalised TWICE
    }
    timer = min(m_flStandingOnHeadTimer + interval, 5.0);   // _DAT_10454110
    m_flStandingOnHeadTimer = timer;
    start = myOrigin + (0,0,0.1);                       // _DAT_1049a1f4
    delta = d * timer * 40.0 * interval;                // _DAT_1049a1f8

The jitter is applied to a UNIT vector, so it is a fixed angular spread of about 8 degrees rather
than a distance-dependent one; and the push is a RAMP, so a body that has been stood on for a while
is shoved harder than one that just was. The move is then hull-traced from `start` to `start+delta`
with this NPC's own collision extents, mask `0x202400b` and a filter built from `m_pMoveProbe`'s
entity (`+0x5d40`) and its collision group, under a `CAI_MoveProbe_TraceHull` VProf scope. A blocked
or solid result retries with the delta **subtracted** — the opposite direction, not renormalised and
not redrawn. Both gates read the same `trace_t`, so an unblocked first trace passes the second gate
without a second trace running. Clear: subtract the 0.1 lift back off, `SetAbsOrigin` (slot 216) and
`CBaseEntity::Relink`, and **do not clear the timer** — which is what lets the ramp build while the
push is succeeding. Blocked both ways: clear it.

### `CNPC_VAsianVampire::StandingOnPlayer` — `0x10362730`

383 bytes, 100 of them a scope-trace pair. The subject is `m_hClosestPlayer` (`+0x628c`), the sense
pass's cache, **not** `GetEnemy()` — an asian vampire standing on a player it is not fighting still
answers true. Retail builds three 2-D lengths with `sqrtf` and compares

    sqrtf(dx*dx + dy*dy)  <  |playerMaxs.xy - playerMins.xy| * 0.5
                           + |myMaxs.xy     - myMins.xy|     * 0.5

The two right-hand terms are **half-DIAGONALS of the XY footprints, not radii**: for a 32x32 hull
that is 22.63 units, not 16, so the test admits a diagonal overlap a circle of the box's half-width
would refuse. `_DAT_104454d0` is 0.5. The decision at `10362880` is `FCOMPP` — the limit against the
separation — then `FNSTSW AX; AND EAX,0x4100; JNZ <return false>`: the mask keeps C0 (limit below
separation) **and** C3 (equal), and either one returns false, so the comparison is **strictly** less
and exactly touching is not standing on. Z is never measured.

The two sides are also computed at different precisions and retail does not make them agree: the
separation comes back from a `sqrtf` call (`0x10579660`, single) while both half-diagonals are built
with x87 `FSQRT` at extended precision and only rounded on the `FST` that spills them. At the
boundary that difference is what decides, which is why the port's test pins the edge with a 3-by-4
footprint — diagonal exactly 5, half exactly 2.5 — rather than with a hull whose half-diagonal is
`sqrt(2)*16` and representable in neither float nor double.

### `CNPC_VWerewolf::UpdateFakeHull` — `0x103d93b0`

1,202 bytes. The word it maintains is `+0x66dc`, the world position of the `Bip01` bone as of the
previous call, and the one cell it compares against is `DAT_1070d1b0` — which is `vec3_origin`, so
the body only reads correctly once that is settled. **29c's walk of it ("when it moved") is wrong**:
the test is whether the cache is still the zero vector.

    if (GetEnemy() == 0) { m_vecFakeHullPos = vec3_origin; return; }    // 103d9431: a RESET

    bone  = LookupBone("Bip01");  hdr = GetModelPtr(-1);
    GetBoneTransform(bone, m);                                          // slot 256
    VectorTransform(vec3_origin, hdr->bone[bone] + 0x58, local);        // the bone's own point
    VectorTransform(local, m, world);

    if (m_vecFakeHullPos != vec3_origin) {                              // 103d94c0: is it armed?
        if (DAT_1093f73c->GetInt()) DrawDebugHullAtPoint(world, 0);
        box = [hullMins(m_eHull)*1.25 + world, hullMaxs(m_eHull)*1.25 + world];  // _DAT_10462914
        enemy->m_Collision->vfunc0x3c(&eMins, &eMaxs);
        if (BoxesOverlap(box, eMins, eMaxs)) <the push arm>              // 0x10240250, inclusive
    }
    m_vecFakeHullPos = world;                                            // on EVERY enemy path

So the first call after every reset does nothing but fill the cache, and a failed `Bip01` lookup
leaves the point at zero, which resets the cache and disarms the next call. The fake hull is a
quarter larger than the real one and is hung off the **pelvis**, not the origin.

The push arm is the other half:

    delta  = world - m_vecFakeHullPos;                      // how far the BONE moved, not the NPC
    pushed = GetEnemy()->+0xa8;                             // NOT the enemy itself
    near   = pushed->CollisionProp()->CalcNearestPoint(world);           // 0x100dd000
    dir    = pushed->GetAbsOrigin() - WorldSpaceCenter();
    cls    = pushed->vfunc0x50c(&dir);                      // slot 323, BEFORE the normalise
    VectorNormalize(&dir);                                  // result discarded — dead
    act    = cls == 1 ? 0x7a : cls == 3 ? 0x7b : 0x79;
    pushed->vfunc0x500(this, act);                          // slot 320, NOT rate-limited
    if (m_flFakeHullPushTime + 1.0 < curtime) {                          // _DAT_104454c0
        info = CTakeDamageInfo(this, this, 20.0, 1, 0, 0, -1);  // attacker AND inflictor are this
        info.subtype = 2;  info.field = 1;  info.scale = 1.0;
        info.force = delta * 500.0;  info.position = near;                // _DAT_10457f5c
        pushed->TakeDamage(info);
        m_flFakeHullPushTime = curtime;
    }

Three things a reading of the C alone gets wrong: the classifier sees the **un-normalised**
direction (the `VectorNormalize` runs after the call and its result is `FSTP ST0`-ed away); the
knockback is **not** rate-limited, only the damage is; and the entity pushed is the enemy's `+0xa8`,
not the enemy.

**Unrecovered:** which field `CBaseEntity+0xa8` is — no datamap in the corpus declares it and no
body in layers 0–9 writes it; the name and default of the `DAT_1093f73c` debug cvar, which lives
past `.data`'s raw size and is constructed by no corpus body; and the retail names of `+0x66dc` and
`+0x66f4`, neither of which is in `CNPC_VWerewolf`'s datamap.

### `CNPC_VMingXiao`'s severed-tentacle scatter — `0x10397e00`, `0x103998d0`, `0x1039ef60`, `0x1039ef90`

Two bodies, two different receivers, one notice. `0x1039ef90` is the notice: fire the global object
at `DAT_10924a6c + 4`, `SetCondition(0x78)` on the notified tentacle and write its
`m_vecScatterCenter` (`CNPC_VMingXiaoTentacle +0x668c`) — the point that tentacle is to scatter away
from. The condition and the centre both land on the **notified** entity, not on the notifier.

`0x10397e00` (100 bytes) is reached through `0x1039ef60`, which resolves a tentacle's `m_hMingXiao`
(`+0x665c`) and runs the walk **on the owner**. It walks the owner's `m_rhSeveredTentacles[6]`
(`+0x66a8`) unconditionally, skips an unresolved handle and skips the moved entity itself, and hands
each survivor the **moved entity's** own `GetAbsOrigin()`. A null moved entity does nothing. So a
severed tentacle that moves scatters its siblings away from where it now is.

`0x103998d0` (212 bytes) is `CNPC_VMingXiao::CoordinateTroops`'s (`0x10399610`) severed half, which
runs on ONE tentacle per call — the coordinator walks `m_iCoordinateTentacleID` (`+0x6740`) 0..5 and
wraps, so the set is coordinated over six calls. Its gates, in order:

- the tentacle's `m_iForcedSchedule` (`+0x65c8`) is neither `0x163` nor `0x165`;
- its `m_ePhase` (`+0x6670`) is exactly **2**;
- `VectorNormalize(tentacleOrigin - myOrigin)` answers at most `_DAT_1046dcd0` = **128** units —
  inclusive, because the flag mask is `AND EAX,0x4100`, which keeps both the below and the equal
  bits;
- the **2-D** dot of that unit direction with `m_vecForward` (`+0x6290`) is at least
  `_DAT_10449260`, which the listing reads as `FCOMP double ptr` and which is **0.25**, a
  75.5-degree half-angle. Read as a float that cell is 0.0 and the cone would be the whole forward
  half-plane.

Past all four, the centre handed over is **my** origin — the opposite receiver from `0x10397e00`'s.
Z is not multiplied by anything in the dot, so a tentacle straight overhead is judged only by its XY
bearing.

**Unrecovered:** (`DAT_10924a6c` is the ConVar `ent_trace_conditions`, see the hint-list section) what condition `0x78` is named (it is one past the base
registrar's `0x00..0x76` namespace), and what `m_ePhase` values other than 2 mean.

## Story 29c-1, family Dialogue — `CDialog`'s two bodies, the `CBasePlayer` half and the security camera

### `CDialog::message_send` — `0x100e58e0` (2026-09-13)

716 bytes, and the decompiler reports DAMAGED on it, so it is walked off the listing. It pushes one
conversation turn at the player: `CDialog+0x04` is the RECIPIENT `EHANDLE` (a second entity handle
beside the speaker at word 0) and a `CReliableSingleUserRecipientFilter` is built over it — a dead
handle yields a null recipient and every message below is still sent.

Its caller hands it a page buffer whose layout the listing gives: `+0x0000` the NPC's own response
text, `+0x0804 + (i-1)*0x800` response text *i*, `+0x2804 + i*4` and `+0x2814 + i*4` two int arrays.
Three user messages go out through `VEngineServer014` slots 65/67/68/66 (`UserMessageBegin`,
`WriteByte`, a second writer, `MessageEnd`): opcode `0` with `m_ResponseCount` (`+0x2834`), opcode
`2` with array A whole and then array B whole, opcode `3` bare.

Between them the body appends the turn to the history sheet — `message_append(0, page)` then
`message_append(i, page + 0x804 + (i-1)*0x800)` for `i = 1..N` **inclusive**, so N+1 appends — while
the two int loops run `0..N-1`. The asymmetry is retail's.

The fork is `LookupSpeechFile(this, m_CurrentLineId +0x2830)` (`0x100e1880`), and it runs the way
round that surprises: a line that **has** a voice resolves the speaker at `CDialog+0x00`, calls
`SetDialogQue(speaker, speech, IsFinal())` (`0x102c0470`, a `Q_strncpy` into `m_szDialogQue +0x64ec`
capped at `0x60` plus `m_bDialogQueIsFinal +0x654c`) and **hides** the player's choices; a line with
**no** voice shows the choices, shows the history window and drains the queued whisper
(`PlayWhisper(NULL)`, `0x100e0a40`).

`+0x2830` is the current NPC line's **id**, not a count: `0x100e58b0` — the `bIsFinal` argument —
is `return m_bFinalLatch (+0x30e9) || m_CurrentLineId == 0;`, the latch short-circuiting.

### `CDialog::goto_line_for_response` — `0x100e4840` (2026-09-13)

Which line the conversation goes to when the player picks shown response *n*. `CDialog+0x08` is the
current node record, with `node+0x104` a count and `node+0x10c` an array of 0x34-byte rows of which
the body touches two words: `+0x00`, an id, and `+0x0c`, a goto-line. `CDialog+0x2838` is one int
per shown response — that response's target id — and `+0x2834` counts them.

A null node or `m_CurrentLineId (+0x2830) <= 0` answers **0**. So does a **zero** target id: the
`goto` in the decompilation jumps past the warning to that same outer return, which 29c's walk
misses. Otherwise the node's table is scanned for a row whose `+0x00` equals the target id and that
row's `+0x0c` is returned, under `Msg(1, "Player responded: %d %s")`.

**The fallback reads a different field from the hit path.** On no match — and on a negative index —
the body logs `Msg(3, "Could not find response for play…")` and returns `node->pResponses[0]`'s
`+0x00`, the **id**, not its goto-line at `+0x0c`. Both 29c's walk and the obvious reading call it
"the first response's goto-line"; the C does not. Retail also never bounds-checks the index against
`+0x2834` — only `>= 0` is tested — and dereferences `pResponses[0]` even for an empty table.

### The `CBasePlayer` half — `0x1017cf90`, `0x10161a70`, `0x10175180` (2026-09-13)

`GetCineCamera` (`0x1017cf90`, 111 bytes) is four terms: `m_iCameraOverrideIdx` (`+0x1ec4`)
strictly positive, then `+0x19b4` live with a matching serial, then the same handle resolved and
serial-checked a **second** time before its pointer is returned. `SetCineCamera` (`0x1017cef0`) is
the pair's only writer, so an entity-less adopter — a terminal, a `SetCamera` shot — holds `+0x1ec4`
with a dead `+0x19b4` and this body answers 0 for it.

`GetControllerNPC` (`0x10161a70`, 534 bytes) is the `npc_VPlayerController` factory. A live
`m_hControllerNPC` (`+0x1db0`) whose classname `__strcmpi`-matches the request is returned
unchanged; a mismatch emits
`"GetControllerNPC() asked for NPC class: %s, but already has NPC of class %s. Deleting old NPC,
this better be OK!!"`, calls `0x101618e0` with **both copy flags clear** — which clears the handle
and copies nothing — and falls through to the create. The create is
`CreateEntityByName(classname)` + `entity+0x98`, and on failure
`"GetControllerNPC() created NULL Entity for :%s"` with `+0x1db0 = -1`. On success, in order:
`m_spawnflags |= 4`; `SetOrigin(me->GetAbsOrigin())` (slot 62 from slot 217); `SetAngles(me->
GetAngles())` (slot 64 from slot 221); `SetOwnerEntity(me)` (slot 202);
`CopyAnimationDataFrom(npc, me)` (`0x10097310` — `SetModel`, `SetModelIndex`, `m_flCycle`,
`m_nTopColor`, `m_nBottomColor`, `m_fEffects = src | 0x10`, `m_nSequence`, `m_flAnimTime`,
`m_nBody`, `m_nSkin`, `m_nPhysicsChainDisableMask`); `+0x63b4 m_flSeekDistBase = 4096.0`
(`0x45800000`); `SetModel(me->GetModelName())` (slot 105 from **slot 9**); `DispatchSpawn`
(`0x101d1280`); `m_fEffects |= 0x60`, giving `0x70`; `ResetThinkTimers()` (slot 614); and the handle
store. 29c's walk says the model name comes from the owner's *classname* — the listing calls slot 9,
`GetModelName`, at `10161c02`.

`0x10175180` (116 bytes, unnamed) reads the **same** word: `m_hControllerNPC` resolved, then
`vtable+0x228` — slot 138, `Classify()` by the SDK ordering — compared against **3**. 29c's walk
calls `+0x1db0` a dialogue partner; `vtmb_fields CBasePlayer` names it `m_hControllerNPC`, so the
predicate is "a controller NPC is driving this body and it is in state 3". Its readers are the
dialogue refusal predicate (`0x10178170`), `CAI_BaseNPCTroika::CanTalk` (`0x102c21c0`) and the
stealth-kill gate (`0x101681a0`). The two seven-byte getters beside it, `0x1017f770` and
`0x1017f8b0`, are `+0x1d10 m_iCopsInPursuitCount` and `+0x1d14 m_iHuntersInPursuitCount`, and
`0x10178170` tests them as two arms in that order rather than as a sum.

**Unrecovered:** what `Class_T == 3` is named; `DAT_1072608c`, the user-message type; what the two
per-response int arrays carry.

### `CNPC_VCameraSecurity`'s linked camera — `0x10369e70` (2026-09-13)

252 bytes, unnamed in retail; its three callers are all `CNPC_VCameraSecurity`'s — slot 201
`FVisible` (`0x10369ff0`), slot 363 `FInViewCone` (`0x10369fb0`) and `vfunc201`. A security-camera
NPC sees through the `CSecCamera` it is linked to, not through its own eyes.

Three blocks, each re-reading `+0x6664` from scratch. If the cached handle does not resolve,
`FindEntityByName(gEntList, NULL, m_iszLinkedCamera +0x6660, this, this)` (`0x100f7770`) followed by
`dynamic_cast<CSecCamera*>` — so an entity of the right *name* but the wrong class clears the handle
rather than caching it. Then: live, and `+0x6668` is latched to 1; dead **and the latch was already
set**, and `UTIL_Remove(this)` (`0x101cd940`) removes the camera NPC itself. The latch is a one-way
door — a camera NPC that never linked simply keeps answering null, while one that loses its camera
deletes itself.

### Slot 366's `CNPC_VSabbatLeader` override — `0x103a76d0` (2026-09-13)

111 bytes, of which the body past the scope-trace prologue and epilogue is one tail call to
`CNPC_VAndreiBlood::HandleInteraction` (`0x10385a70`) — whose entire body is `return 0`. The
override therefore adds nothing but its own name on the scope-trace stack, and both addresses answer
false. It is recorded as two rows rather than collapsed to one because `npc-kernel/slots.md` records
two distinct fills and a reader must be able to check the port against it.

### The four debug output channels — `0x10119750` and its three siblings (2026-09-13)

Every debug body in the `CAI_BaseNPC` family writes to one of four devices, and which one it is is
part of the body. `DevMsg(fmt, …)` goes straight to the dev console and is `ReportAIState`'s
only channel. `0x10119750` is the **message ring**: four `ConVar::GetBool()` gates —
`DAT_1070b1dc` with a `0x10119940` predicate, then `DAT_1070b07c`, `DAT_1070b0f4`, and finally
`DAT_1070b13c`, whose false answer RETURNS — then `Q_vsnprintf` into 512 bytes, `strncpy` of the
first `0x60`, and a shift-insert into a growing array of `0x60`-byte rows. `DrawDebugStatOverlays`
uses it for every line it prints. The engine's `AddEntityTextOverlay` (`DAT_1070b22c` `+0x8c`,
followed by `0x101434b0`) is the **numbered** channel: a caller hands it a line index, and the whole
contract between `CBaseEntity::DrawDebugTextOverlays` and its overrides is that index.
`NDebugOverlay` is the fourth — `Box` `0x10142aa0`/`0x10143b70`, `BoxDirection` `0x10142af0`,
`Line` `0x10142e90`, `Text` `0x10143710`, `EntityBounds` `0x10142e20`.

### `ReportAIState` — `0x102779a0` (2026-09-13)

Slot 581, 568 bytes, the SDK dev dump, and it is a single console line assembled from eight
fragments — several of them deliberately unterminated. In order: `"%s: "` with `GetClassname()`;
`"State: %s, "` with `GetStateName(m_NPCState)`; then, **only when `m_Activity` and
`m_IdealActivity` are BOTH not -1**, `"Activity: %s  -  Ideal Activity: %s"` with each activity
pushed through `SelectWeightedSequence(act, -1)` and `GetSequenceActivityName(seq)`, so the name is
the one the chosen SEQUENCE carries rather than the one asked for. Then the schedule: `"No Schedule,
"` for a null `m_pSchedule`, otherwise `"Schedule %s, "` (the program's name pointer, or `"Unknown"`
when it is null) and, when `GetCurTask()` answers, `"Task %d (#%d), "` with the task number and
`m_ScheduleState`. Then the enemy: `"No enemy "`, or — after building `(x, y, z + 64.0)`
(`_DAT_10451acc`) from the enemy's origin and handing it to `PTR_DAT_10566258`'s `+0xc` with
`(1, 1, 0)`, a debug marker whose result is dropped — `"Enemy is %s"`. Then, when `IsMoving()`,
`" Moving "` plus exactly one of `": Stopped for %.2f. "` (when `m_flMoveWaitFinished > curtime`;
the binary's compare is `<=` the other way, so a wait expiring on this very frame takes the other
arm) or `": In stopped anim. "` (when `m_IdealActivity` equals `GetStoppedActivity()`,
`0x1027a6c0`). Then `"Leader."` and a newline, **unconditionally** — SDK 2013 gates the same
line on `IsLeader()` and Troika's build does not. Then `"Yaw speed:%3.1f,Health: %3d"` with the
motor's stored `m_YawSpeed` (`m_pMotor+0x38`) and `m_iHealth`. Finally `GetGroundEntity()` twice,
for `"Groundent:%s"` or `"Groundent: NULL"`.

**Unrecovered:** what `PTR_DAT_10566258`'s `+0xc` draws. **Not built:** the motor's stored yaw speed
has no port word — slot 516 `MaxYawSpeed` computes a different one, the ceiling — so the
line prints 0.0, and the task number is family BaseHelpers' `CurrentRetailTaskNumber` seam, so the
task fragment never appears.

### `DrawDebugStatOverlays` — `0x102775e0`, `0x1029c010`, `0x10366290` (2026-09-13)

Slot 76, three bodies, and the dispatch between them is the interesting part. `CNPC_VBaseBoss`
(`0x10366290`, 36 bytes) prints `"Dist to player: %.3f"` from `+0x6264` and then **tail-calls the
BASE body `0x102775e0` directly**, skipping `CAI_BaseNPCTroika`'s override entirely — so a boss
never gets the expression dump even when it has a dialogue. `CAI_BaseNPCTroika` (`0x1029c010`, 925
bytes) tail-calls the base when `m_iDialog` is zero and otherwise replaces it.

The base body (759 bytes) was recovered from the LISTING: the decompiler lost four of its six
`Q_snprintf` format strings to the 512-byte frame and swapped the task line's two markers. It
returns immediately when `GetModelPtr()` is null. Then `"Seq: "` plus either `"(INVALID)"` or the
sequence descriptor's label, `" / "` and its activity name. Then `"Cycle: %.2f"` (`m_flCycle`). Then
the activity line, three arms in this order: when either activity is -1, `"Actv: RESET"` if
`m_Activity` is 0 and `"Actv: INVALID"` otherwise; when `m_Activity` alone is 0, `"Actv: RESET"`;
otherwise `"Actv: %s (%s)"` with both activities pushed through slot 375
(`NPC_EarlyTranslateActivity`), slot 381 (`Weapon_TranslateActivity`), slot 376
(`NPC_TranslateActivity`) and then the sequence resolve. Then `"State: %s, "` and `"Move: %s, "`
(`GetNavTypeName(GetNavType())`). Then, **and only when `m_pSchedule` is non-null — there is no
"no schedule" line here, unlike `ReportAIState`** — `"Schd: %s, "`, then `"Task: None"` or
`"Task: %s (#%d), "`, then one line per task of the program re-reading `numTasks` every iteration:
`"%s%s%s%s"` with a prefix (`"Task:"` on index 0, seven spaces after), a leading marker (`"->"` on
the current index, three spaces otherwise), the task name, and a trailing marker (`"<-"` on the
current index, empty otherwise). `Task_t` strides eight bytes.

The Troika body prints, in order: `"Expression / Gesture information for %s:"` with
`GetDebugName()`; `"Seq (%.2f): %s / %s "`; `"Disposition:  %s,  Expression: %s (%.2f)"` with the
disposition name, the expression name (or `"None"` when `IsValidExpressionIndex` refuses) and
`+0x10b8`; one of `"No Scene Entity"` / `"No Scene"` / `"Scene time: %.2f"` decided by
`m_hDialogScene` and that entity's `+0x4bc`; one `"Expression:(N|O) %s level: %0.2f"` per row of
`m_Expressions` (`+0x10bc`, count `+0x10c8`, stride `0x20`), the letter chosen by the expression
data's `+0x18c & 1`; one `"Anim Layer %d: %s (weight: %f)"` per anim-overlay layer (`+0x748`, stride
`0x30`, four of them) whose weight is **strictly greater than 0.0** (`_DAT_104454c4`); `"Eye targets
-  current: %d  default: %d  step: %d"` from `+0x5b94`, the disposition table's default
(`0x100eccf0`) and `+0x6578`; `"Looking at entity %d (%s)"` when `m_hEyeLookTarget` resolves; `"Qued
Dialog: %s"` when `m_szDialogQue`'s FIRST BYTE is non-zero; and `"Talk Time Remaining: %.2f"` with
`max(m_flTalkTime - curtime, 0.0)`.

**Unrecovered:** nothing in the three bodies. **Not built:** the studio header, the
scripted-expression vector, the anim-overlay array, the scene object and the disposition's default
eye target are all seams, so the sequence line takes `"(INVALID)"`, the two loops run zero times,
the scene line reads `"No Scene Entity"` and the default eye target reads -1.

### `DrawDebugGeometryOverlays` — `0x10275760` and `0x1034e070` (2026-09-13)

Slot 123. The base body is 3,359 bytes and eight arms; it too was recovered from the listing, which
has every colour byte and box extent the decompiler folded into the 508-byte frame. In order:

1. `m_debugOverlays & 0x10000` (SDK's `OVERLAY_NPC_ZAP_BIT`) — **not a drawing at all**:
   `VacateSquadSlot()` (`0x1028ae60`), `Weapon_Drop(GetActiveWeapon())` (slot 385) and
   `ThinkSet(SUB_Remove, 0.0, NULL)`. The bit is **not cleared**, so it re-fires every call; SDK
   2013 clears it, and the squad-slot release is Troika's own addition.
2. `0x4000` — `CAI_Navigator::DrawDebugRouteOverlay` (`0x102f28e0`).
3. Un-gated by `m_debugOverlays` entirely: when `!(DAT_1092053c & 1)` and `m_pSchedule` is the
   program `GetSchedule(0x39)` — `SCHED_FORCED_GO` — a magenta wireframe box
   `(255, 0, 255, 0)` of half-extent 5 at `m_vSavePosition` (`+0x5db8`).
4. `0x1000` — `GetVectors` into three locals that are then **never read**, then the collision
   box: when the `m_Collision` OBB's mins and maxs are equal on all three axes, a half-extent-5
   orange box `(255, 128, 0, 20)` at `GetAbsOrigin()`; otherwise the real OBB in red
   `(255, 0, 0, 20)`.
5. `0x2000` — stamp the navigator's `+0x08` from the NPC's `+0x156c` and `+0x0c` from
   `gpGlobals+0x04`, then `CAI_Pathfinder::NearestNodeToNPC` (`0x102f3c10`) and
   `CAI_Node::GetPosition(node, m_eHull)` (`0x102fb0d0`), and a white half-extent-10 wireframe box
   at it.
6. `0x400000` — the view cone, **and its second gate is `m_pBaseNPCTroika` (`+0x0098`) being
   NULL**. That word is one of `CBaseEntity`'s cached downcasts (`+0x0094 m_pBaseNPC`,
   `+0x0098 m_pBaseNPCTroika`, `+0x009c m_pCombatCharacter`) and it is non-null for every
   `CAI_BaseNPCTroika`, so **the arm is dead for every Troika-line NPC**. What it would draw:
   `range = acos(m_flFieldOfView)` (`+0x1574`, 0.2 on the Troika line), `eye = EyeDirection2D()`
   (slot 372), the eye vector rotated by ±`range` in the XY plane, and four
   `NDebugOverlay::BoxDirection` calls from `EyePosition()` (slot 193) — left and right in red
   `(255, 0, 0, 50)` with mins `(0, 0, -40)` and maxs `(200, 0, 40)`, then the eye direction in
   green `(0, 255, 0, 50)` with the same box, then the eye direction again in green with mins
   `(0, 0, -10)` and maxs `(40, 0, 10)`. SDK 2013's fourth call is a plain `Box`; Troika's is a
   fourth `BoxDirection`.
7. `0x20000` — the enemy-memory labels, below.
8. `0x200000` — `Line(EyePosition(), GetEnemy()->EyePosition(), 255, 0, 0, noDepthTest, 0)` and
   `Line(EyePosition(), target->EyePosition(), 0, 0, 255, noDepthTest, 0)`, each behind its own
   resolve.

Then two tail calls, in order: `CAI_Pathfinder::DrawDebugGeometryOverlays(m_pPathfinder,
m_debugOverlays)` (`0x103061e0`) and `CBaseEntity::DrawDebugGeometryOverlays()`.

The `0x20000` arm walks `GetEnemies()`'s record list (`+0x0c` head, `+0x38` next). It skips a record
whose remembered entity does not resolve, and one whose entity carries no `m_pCombatCharacter`
(`+0x9c`). It then builds a label by three exclusive tests in order — the committed enemy is
`"Current Enemy"`, `m_hTargetEnt` is `"Current Target"`, anything else is `"Other Memory"` — and
appends `" (Unreachable)"` (slot 530) and `" (Eluded)"` (the record's `+0x35`) independently. The
COLOUR is a **second** ladder and not the label's: unreachable wins outright and is green, then
eluded is blue, then enemy red, target magenta, and everything else `(255, 100, 100)`. The label is
drawn with `NDebugOverlay::Text` at the record's remembered position (`+0x0c`). Then one of two
shapes: when the remembered entity is the player (`+0xa8 m_pPlayer`) **and** its remembered position
is within 10.0 units (`_DAT_1044e664`) of its current origin, a two-line cross built from its basis
scaled by 10.0 with `_DAT_10452dc4` = 2.0 added to and subtracted from the endpoints' Z; otherwise
the hull box at the remembered position — `NAI_Hull::Mins`/`Maxs` of the REMEMBERED NPC's
`m_eHull`, not this one's — **drawn twice with identical arguments**, which is retail's own
duplication.

`CScriptedTarget`'s override (`0x1034e070`, 541 bytes) gates on one test of two bits,
`m_debugOverlays & 0x24`. Enabled: a white ±8 box at `m_vLastPosition`, a red ±5 box at the
origin, and a line between them. Disabled: one box at `(200, 100, 100)` whose mins and maxs the
listing pushes the other way round — `(+5,+5,+5)` as mins and `(-5,-5,-5)` as maxs. Then a
`(200, 100, 100)` line to `GetNextTarget()` when there is one, and a green line to the resolved
`m_hTargetEnt` when there is one. Then `CBaseEntity::DrawDebugGeometryOverlays()`.

**Unrecovered:** what `DAT_1092053c`'s bit 0 is and who sets it. **Not built:** the navigator route,
the node graph, the collision OBB and the pathfinder are all seams, so arms 2 and 5 draw nothing and
arm 4 always takes the degenerate half; arm 6 is dead by its own recovered gate.

### `DrawDebugTextOverlays` — `0x102d1600` and `0x1034ddf0` (2026-09-13)

Slot 124 on two classes that are not NPCs. Both begin with `CBaseEntity::DrawDebugTextOverlays()`,
whose answer is the next free overlay LINE, gate on `m_debugOverlays & 0x1`, and return the base
answer plus however many lines they added — which is the whole contract between a base body and
its overrides.

`CAI_Hint` (`0x102d1600`, 260 bytes) adds two. The first format is a **bare `"%i"`** (`0x1057ae88`)
with `m_nHintType` and no label at all, where SDK 2013 prints `GetHintTypeDescription()`. The second
is `"delay %f"` (`0x1060a508`) with `max(m_flNextUseTime - curtime, 0.0)` — the floor is
`_DAT_104454c4` = 0.0, so an already-usable hint reads `delay 0.000000`.

`CScriptedTarget` (`0x1034ddf0`, 497 bytes) adds three: `"State: On"`/`"State: Off"` from
`m_iDisabled`; `"Next: -NONE-"` or `"Next: %s"` with `GetNextTarget()`'s `GetDebugName()`; and the
user line, whose NO-TARGET spelling depends on `m_iDisabled` too — `"User: -LOOKING-"` when
enabled and `"User: -NONE-"` when disabled — against `"User: %s"` for a resolved
`m_hTargetEnt`. Every line is white and opaque.

### `CNPC_VWerewolf`'s two debug draws — `0x103d5050` and `0x103d4820` (2026-09-13)

`DrawBBoxOverlay` (`0x103d5050`, 164 bytes) fills **slot 620 for `CNPC_VWerewolf` only**, and slot
620 is not one virtual across the family: `npc-kernel/slots.md` records `CNPC_VSabbatLeader#620` as
`FootstepSound` (`0x103aa5e0`), a different function at the same index on a vtable that diverged
earlier. Inside a scope-trace push keyed on `m_iName`, the body asks `ShouldPursueEnemy`
(`0x103cf5f0`): false draws `NDebugOverlay::EntityBounds(this, 50, 255, 50, 0, 0)` — a green
whole-entity box — and true falls through to `CBaseEntity::DrawBBoxOverlay()`. So a werewolf
that has stopped pursuing is recoloured and one that is pursuing is not.

`DrawDebugHullAtPoint` (`0x103d4820`, 398 bytes, `RET 0x10` — a `Vector` by value plus a
duration) draws `NAI_Hull::Mins`/`Maxs` of `m_eHull` (`+0x1568`) as a box at the point, colour
`(255, 100, 0)` at alpha 100, then a second overlay call (`0x1000566e`) carrying the hull's planar
radius `sqrt(halfX^2 + halfY^2)` — the halves being `(max + min) * 0.5` (`_DAT_104454d0`)
— with the constant 5.0 and colour `(255, 255, 0)` at alpha 20.

**Unrecovered:** which `NDebugOverlay` entry point `0x1000566e` is; its argument shape fits a circle
or a swept box and the listing does not name it. **Not built:** `NAI_Hull`'s table is a seam, so
both boxes are degenerate and the radius is 0.

### `CNPC_VTzimisce::GetEventName` — `0x103bdd10` (2026-09-13)

Slot 241's only species override, 202 bytes of which almost all is seven inlined `strcpy` loops.
Anim-event ids 2..8 copy a fixed name into the caller's buffer — `START_IDLE`, `START_FIDGET`,
`START_RUN`, `START_LANDHARD`, `START_ATTACK`, `START_ATTACKBIG`, `START_POUNCE`, at `0x1065c87c`
down to `0x1065c818` — and every other id tail-calls `CBaseAnimating::GetEventName`. The ids are
read from the `animevent_t`'s first word, not from the buffer pointer, which is the first argument.

## Story 29c-1, family Closure — the slots the port already answered, wired to the slot

_Recovered 2026-09-13._ The 41 Troika-line slots of layers 0–9 whose 29c verdict is `present` (the
port already runs the body somewhere) or `mechanism` (the engine supplies it). Each of them was a
generated stub, so the slot the kernel dispatches through answered a tally rather than the port's own
answer; the family is the wire. Ported in `Substrate/ElysiumNpcKernelClosure.inl` / `.cpp`, tested by
`Elysium.Substrate.NpcKernelClosure.*`. The sections below are the bodies over 64 bytes that had no
walked paragraph; the eye maintainer and the point view cone are in `senses.md` and the feed-end
output is in `lifecycle.md`.

### Slots 192 and 215 `WorldSpaceCenter` — `0x10027160`, `0x100b4c30`

**One body compiled twice.** The decompiled C of the two is identical instruction for instruction
except for how the answer leaves: `0x10027160` (slot 192, 334 bytes, 496 classes) writes three floats
through the hidden struct-return pointer, `0x100b4c30` (slot 215) returns a pointer. Both allocate
**two** entries from the image's rotating temp-vector ring (`DAT_109f0cc0`, 128 entries, index
`DAT_106b856c` advanced with `& 0x7f` — so one call consumes two slots of the ring), write the local
midpoint `mins + (maxs - mins) * 0.5` (`m_Collision +0x274` and `+0x280`, `_DAT_104454d0` = 0.5) into
the second, and then take one of two arms: if the entity is solid, `m_nSolidType` (`+0x2b0`) is
neither 0 nor 2, and `m_Collision`'s angles (vfunc `+0x24`) differ from `vec3_angle`
(`DAT_1070d9d0`), transform the midpoint by the entity-to-world matrix (vfunc `+0x28`) through
`VectorTransform`; otherwise add the collision origin (vfunc `+0x20`) componentwise. So it is the
collision OBB's centre with an axis-aligned fast path, which is the quantity Unreal's
`UPrimitiveComponent::Bounds` carries.

The `const Vector&` overload's return value is a pointer INTO the ring, which is Source's way of
returning a reference from a value computation: the reference is valid until 64 further calls have
wrapped the index around.

**Unrecovered:** nothing in the body. What the port cannot reproduce is the ring's aliasing — it
caches one centre per entity instead (`FElysiumNpc::WorldSpaceCentreCacheCm`, a named modernization
stated at its declaration), which is strictly more stable. No call site in layers 0–9 holds the
reference across another call.

### Slots 569 and 570, the cover and reload activity delegates — `0x10297560`, `0x102954b0`

`CAI_BaseNPCTroika::Cover_Troika` (161 bytes, 64 classes) and `Reload_Troika` (141 bytes, 64
classes). Both are reached with `m_pHintNode` (`+0x5ddc`) as their only argument — every call site
passes that and nothing else: `NPC_EarlyTranslateActivity` (`0x10295590`) at both its delegate arms
(`vtable +0x8e8` for `ACT_RELOAD_FAST`, `+0x8e4` for `ACT_COVER` and for `ACT_IDLE` under
`m_afMemory & 2`), and `StartTask` (`0x102a1910`). Both switch on the hint's `m_nHintType`
(`+0x5dc`) over exactly 100, 101 (`0x65`) and, for cover only, 10200 (`0x27d8`), and both probe each
candidate with a weighted-sequence lookup before answering it.

Cover's first line is **unconditional and precedes everything**: `if ((m_bfAINPCFlags & 0x200) ==
0x200) return 8` — `ACT_COVER_LOW`, with no probe, no hint read and no fall-through. `0x200` is
`COWER_PATH`. Past it, hint type 100 offers `ACT_MIDCRUNCH_IDLE` (`0x1111`), 101 offers
`ACT_CRUNCH_IDLE` (`0x110d`) and 10200 offers `ACT_CORNER_COVER_IDLE` (`0x1118`), each taken only if
`thunk_FUN_10295460` (the Troika line's stat-filtered `SelectWeightedSequence`) answers something
other than -1; everything else falls to `CAI_BaseNPC::Cover_Base` (`0x10274aa0`), which offers
`ACT_COVER_MED` for 100, `ACT_COVER_LOW` for 101, then `ACT_COVER`, then an unconditional `ACT_IDLE`.

Reload has **no base fall-through and no `ACT_RELOAD`** anywhere in it. For hint types 100 and 101 it
tries `ACT_RELOAD_LOW` (`0x57`) through `CBaseAnimating::SelectWeightedSequence` and then the
matching crunch idle through the stat-filtered twin; every miss, and every other hint type, answers
`ACT_RELOAD_FAST` (`0x55`). The base body `CAI_BaseNPC::Reload_Base` (`0x10274820`, 13 classes)
answers `ACT_RELOAD` instead, and the Troika line never reaches it.

**Unrecovered:** nothing in either body. The port's committed activity table
(`Visual/ElysiumNpcActivityTables.cpp`) models cover's forced-low arm as a cover-CONTEXT force rather
than as the unconditional return it is, which is a divergence in the table rather than in the reading
— the slot answers the body.

### Slot 346 `SetPoseParameter(int, float, bool)` — `0x1032fc50`

413 bytes, 85 classes, and the stock SDK looping pose-parameter setter. Past the crash-report
breadcrumb push, it walks the two-entry registry at `m_flSet_PoseParameters`, comparing the asked-for
index against each entry's third word. On a hit it stores the value; then, if the `bool` is set and
`GetModelPtr()` resolves, it looks the pose parameter's descriptor up (`thunk_FUN_100c73b0`) and,
when the descriptor's `+0x10` (the loop span) is not `_DAT_104454c4` (0.0), wraps the value as
`(span - (span + desc+0xc + desc+0x8) * _DAT_10449270 + value) / span`. An index that is not one of
the two registered entries falls out of the loop into `CBaseAnimating::SetPoseParameter02` (slot 260,
`0x10091fe0`), the ordinary non-looping setter, which resolves the model, refuses a negative index,
and writes `m_flPoseParameter[index]` through `thunk_FUN_100c43e0`.

**Unrecovered:** the value of `_DAT_10449270` (= **0.5**, float64; read 2026-09-21, `rdata-cells.md`) (the SDK wrap fraction) and the retail names of the
two registry entries. Neither is reachable from this runtime, which stands no `studiohdr_t` and
therefore no pose-parameter descriptors, so every call takes the fall-through — which is retail's own
answer for an index the registry does not carry.

### Slots 102 and 184, the two trace-shaped mechanisms — `0x100ab450`, `0x10267260`

Both fill about 496 classes, most of them props and triggers rather than NPCs, which is what says
they are engine plumbing rather than an NPC rule.

`Physics_TraceEntity` (`0x100ab450`, 121 bytes) is 100 bytes of crash-report breadcrumb around one
call: it pushes `"Physics_TraceEntity"` onto the scope-trace stack, calls `thunk_FUN_101cd110` with
all five arguments unchanged — the entity, the start, the end, the content mask and the `trace_t*` —
and pops. `0x101cd110` issues the trace through the engine trace service's own vtable
(`DAT_1070b254 + 0x14`). There is nothing else in the body.

`MakeTracer` (`0x10267260`, 186 bytes) builds a `CPASFilter` around its first argument (the tracer's
start) through `thunk_FUN_1019ce00` / `thunk_FUN_1019d280`, and **only when the third argument is 1**
(`TRACER_LINE`) fires the bullet-tracer temp entity from that point to the trace's `endpos`
(`param_2 + 0xc`) with this entity's index (`+0x2e0`) as the attachment owner; the rest of the body is
the filter's heap unwind. Every other tracer type builds the filter and fires nothing. Nothing
downstream reads anything either body writes except the `trace_t` itself.

**Unrecovered:** the `trace_t` layout. The port stands no counterpart for it, so the generated
parameter is `void*` and both slots refuse rather than fill a buffer whose shape they do not know —
`Physics_TraceEntity` leaves the caller's buffer byte-for-byte untouched, which its case asserts with
a sentinel fill.

## Story 29d, family Debug10 — the four big overlay dumps, the trace messages and the debug ring

_Recovered 2026-09-14, story 29d._

Fifteen `rule` rows of layers 10–18. Two facts apply to every one of them and are stated once here.

**`DAT_1070b22c + 0x8c` is `IVEngineServer::IndexOfEdict`, not `AddEntityTextOverlay`.** Story
29c-1 read the pair as one engine call; the listing of every overlay block in this band says
otherwise. The edict at `+0x2e0` is pushed, the interface method is called, and its ANSWER is then
the first argument of `NDebugOverlay::EntityText(entIndex, lineOffset, text, duration, r, g, b, a)`
(`0x101434b0`) — which is why each block pushes eight dwords and cleans `0x20`. Every call site in
this band passes duration 0.0 and the colour white at full alpha.

**Every format string below was read out of the pinned image's `.rdata`.** The decompiler folds
these argument lists into the 512-byte stack frames and loses most of them; where the ledger's
one-line evidence and the image disagree, the image is what is recorded.

### `CAI_BaseNPC::DrawDebugTextOverlays` — `0x102767d0`

_Recovered 2026-09-14, story 29d._

Slot 124's BASE body, 2,872 bytes, and a distinct retail function beside the Troika override that
owns the slot: the Troika body calls it first for its starting line index and `CNPC_Crow`'s calls it
INSTEAD of the Troika one. It starts from `CBaseCombatCharacter::DrawDebugTextOverlays()`'s answer
and returns the next free line.

Under `m_debugOverlays & 0x80000` it emits **four** lines, not three:

1. `"Health: %i"` (`0x105cc8cc`) with `m_iHealth`. The ledger's evidence omitted this line, which is
   why it read the index advancing by four for three lines.
2. `"Squad: %c : "` (`0x105cc8bc`) — the format carries no `%s`. The character comes out of
   `SETLE AL; DEC AL; AND AL,9; ADD AL,0x4f` over `m_iSquadDisconnected`: `'O'` (0x4f) at or below
   zero, `'X'` (0x58) above. `m_pSquad`'s name (`squad + 4`) is then `strncat`ed, followed by `"\n"`;
   with no squad object the appended literal is `" - \n"` (`0x105cc8b4`, four characters), not
   `"none"`. This read does NOT apply the disconnect gate — the gate only picks the `%c`.
3. `"Enemy: "` (`0x105cc8a8`) plus the enemy's `m_iName` (`+0x26c`) when set else its
   `m_iClassname` (`+0x11c`), then `"\n"`; with no enemy the same `" - \n"` block is written over
   the terminator.
4. `"Slot:  %s \n"` (`0x105cc898`) with slot 546 `SquadSlotName(m_iMySquadSlot)`.

Under `m_debugOverlays & 0x1`, in order:

* one `"MEM%02d: %s"` (`0x105cc888`) per resolving `CAI_Memory` record, walking `GetEnemies()`'s
  `+0xc` head and `+0x38` next. The `%02d` is the record ORDINAL and it advances for every record
  including the ones whose handle no longer resolves; the line index advances only for the printed
  ones.
* a SECOND `"Health: %i"` line — also absent from the ledger's evidence.
* the weapon line. `"UNARMED"` (`0x105cc85c`) with no active weapon, otherwise
  `"Weapon: %s (%d/%d) (%d/%d)"` (`0x105cc868`) with the name slot `0x570` answers, `m_iClip1`
  (`+0x74c`), the ammo count for `m_iPrimaryAmmoType` (`+0x744`), `m_iClip2` (`+0x750`) and the count
  for `m_iSecondaryAmmoType` (`+0x748`). A negative ammo TYPE answers -1 without asking for a count.
  The listing's push order is what pairs clip1 with the primary count and clip2 with the secondary.
* `"Stat: %s, "` (`0x105cc84c`) with slot 406 over `m_NPCState`, then `"Move: %s, "` (`0x105cc83c`)
  with slot 407 over `GetNavType()`.
* with a schedule, `"Schd: %s, "` (`0x105cc82c`) — `"Unknown"` (`0x1053c828`) for a null name — then
  either one `"Task: %s (#%d), "` / `"Task: None"` line, or, under `m_debugOverlays & 0x100000`, one
  `"%s%s%s%s"` (`0x105cc804`) line per task with the prefix `"Task:"`/`"       "`, the lead
  `"->"`/`"   "` and the trail `"<-"`/`""`.
* the activity line, UNCONDITIONALLY and outside the schedule block: `"Actv: RESET"` or
  `"Actv: INVALID"` when either activity is -1, `"Actv: RESET"` for activity 0, otherwise
  `"Actv: %s (%s)\n"` (`0x105cc7c8`, the same string the stat overlay uses).
* `"Intr: %s (%s)\n"` (`0x105cc7b4`) with `m_interuptSchedule` (`+0x5f3c`) and `m_interruptText`
  (`+0x5f34`), then `"Fail: %s (%s)\n"` (`0x105cc7a0`) with `m_failedSchedule` (`+0x5f38`) and
  `m_failText` (`+0x5f30`). The ledger's evidence had these as "two conditional lines gated on two
  non-zero ints just past the decompiler's view"; all four words and both strings are in the listing.
* `"Enemy too far to attack"` (`0x105cc784`) while `COND_ENEMY_TOO_FAR` stands, printed as a bare
  literal with no `Q_snprintf`.
* `"Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n"` (`0x105cc750`) — no colon after `Vel`, three spaces
  before `Ang:` — gated on `m_vecAbsVelocity != vec3_origin || m_vecAngVelocity != vec3_angle`.
  `DAT_1070d1b0` and `DAT_1070d9d0` are those two BSS zero vectors. The body calls
  `CalcAbsoluteVelocity()` four times under separate `m_iEFlags` bit-12 tests, an artefact of the
  SDK body this was cut down from.

**Unrecovered:** nothing in the body. **Not built:** the active weapon and its five words, the
studio header behind the activity names, and `GetNavType()` are seams, so the weapon line reads
`UNARMED`, the activity names are empty and the movement line reads `Move: None, `.

### `CAI_BaseNPCTroika::DrawDebugTextOverlays` — `0x1029d4e0`

_Recovered 2026-09-14, story 29d._

Slot 124's Troika-line body, 3,754 bytes. Chains `0x102767d0` for the first free line and returns it
unchanged unless `m_debugOverlays & 1`. Its lines, in order: `"Seq: "` (`0x105cc90c`) plus the
sequence label, `" / "` (`0x105cc8fc`) and the activity name off `GetSeqDesc(m_nSequence)`, or
`"(INVALID)"` (`0x105cc900`); `"Cycle: %.2f"` (`0x105cc8ec`); three pose lines when `GetModelPtr()`
is non-null; `"ground speed: %.3f"` (`0x105d9c3c`) from `+0x654`; `"dist to player: %.3f"`
(`0x105d9c20`) from `+0x6264`; `"Disposition: %s"` (`0x105d9c0c`); `"pos: %5.1f, %5.1f, %5.1f"`
(`0x105d9bec`) from slot 217 and `"dir: %5.1f, %5.1f, %5.1f"` (`0x105d9bcc`) from slot 219 — the
second is `dir`, not `ang`.

**The third pose line is a retail typo and it is reproduced.** `0x105d9c54` reads `"aim_yaw: %.3"`:
the conversion character is missing, so the pose parameter the body just fetched is never formatted
at all. Its two neighbours (`"move_yaw: %.3f"` `0x105d9c78`, `"aim_pitch: %.3f"` `0x105d9c64`) are
whole. The line still appears and still consumes an index, which is the part a later override can
observe.

Then `"HG - %d : HB - %d"` (`0x105d9b90`) with `m_LastHitGroup` (`+0x1594`) and the last
`CTakeDamageInfo`'s `+0x40` — that is `+0x664c`, read by the four-byte `0x101c2a30`. Then, under
ConVar `DAT_1092429c`, one of four `EALTAI_*` lines (`0x105d9b64`, `0x105d9b38`, `0x105d9b0c`,
`0x105d9ae0`) for `m_eAlternateAI` 1..4 carrying `m_flAlternateAIExpireTimer (+0x6450) - curtime` —
the REMAINING time; mode 0 and anything above 4 print nothing. Finally the dialogue scene at
`+0x6554`: `"Scene: %s time: %.2f"` (`0x105d9ac4`) when the `CChoreoScene` at `+0x4bc` is live and
`"Scene: %s"` (`0x105d9ab8`) when it is not — both of which RETURN out of the body — and
`"??? scene"` (`0x105d9aac`) otherwise. An unresolved scene handle prints nothing at all and does
not advance the index.

**Unrecovered:** whether the shipped CRT's `Q_snprintf` emits the literal prefix or nothing for the
incomplete `"%.3"` specifier; the port emits the prefix. **Not built:** the studio header (so the
sequence line is `(INVALID)` and the three pose values read 0.0), `m_flGroundSpeed`, the
`CTakeDamageInfo` packet and the `CChoreoScene` object.

### The condition dump — `0x1029d4e0`'s `0x10000000` block

_Recovered 2026-09-14, story 29d._

A third of the Troika text body, and three of its rules were not in the ledger's evidence.

The block opens with an **`"INT COND\n"` header line** (`0x105d9bc0`). It then copies six words of
`m_CustomInterruptConditions` (`+0x5c74`) and six of `m_InverseInterruptConditions` (`+0x5c8c`) onto
the stack and walks ids 0..0x76, appending a three-character abbreviation from slot 408 for every
standing bit of either mask into one 512-byte buffer, flushed every eight appended entries.

The set arm's format is `"%s  "` (`0x105d9bb8`, two trailing spaces) and the interrupt arm's is
`"!%s "` (`0x105d9bb0`). **The two arms use opposite upper-case probes.** Both compute the
upper-case count as `(v * 4) / 10` where `v` is 10 or 0, but the set arm takes 10 when
`HasCondition(id)` stands (`JNZ`) and the interrupt arm takes 10 when it does NOT (`JZ`). Four is
past the end of a three-character name, so each abbreviation is either wholly upper or wholly lower;
a standing condition therefore shouts in the set list and whispers in the interrupt list.

After the walk, the leftover is flushed when `count & 7` is non-zero and that value is SAVED. A
`"COND\n"` header (`0x105d9ba8`) follows, then the fixed 26-id watch list — `0x40, 0x46, 0x01, 0x47,
0x48, 0x49, 0x4c, 0x4f, 0x51, 0x08, 0x5f, 0x60, 0x09, 0x54, 0x56, 0x57, 0x58, 0x59, 0x63, 0x6d,
0x6f, 0x70, 0x0a, 0x0b, 0x0c, 0x0d` in that order — through `"%s "` (`0x105a1518`), flushed on
`(i & 7) == 7` and therefore three times over 24 entries.

**The final flush is a retail bug and it is reproduced.** It tests the SAVED `count & 7` from the
bitfield loop against 7, not the watch list's own remainder of two. A bitfield walk that happened to
end with exactly seven pending abbreviations therefore drops the last two watch entries silently.

When the block's gate is closed (`m_debugOverlays & 0x10000000` clear, or no schedule) the body
instead prints a SECOND `"Enemy too far to attack"` line on `COND_ENEMY_TOO_FAR` — the base body has
already printed one under its own `0x1` bit.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::DrawDebugGeometryOverlays` — `0x1029ca50`

_Recovered 2026-09-14, story 29d._

Slot 123's Troika-line body, 2,129 bytes, read from the listing. Four `m_debugOverlays` masks, three
unmasked blocks, then `CAI_BaseNPC::DrawDebugGeometryOverlays` (`0x10275760`).

`0x400000` draws three cones through `0x1029c4a0`, each with eight arguments: mine
(length `0x1029c970`, `m_flFieldOfView`, 0.0, 64, 192, 64, 50, 1); the `m_hClosestPlayer` one
(length `0x1029c9f0`, `m_flFieldOfView` divided by that player's slot `+0x74`, 2.0, 192, 64, 0, 100,
0); and a third (length `0x1029ca30`, 4.0, 255, 0, 128, 150, 0). **The second call overwrites the
length and field-of-view registers**, so the third cone is drawn with the player-scaled pair whenever
a player resolved and with the plain pair otherwise.

`0x1000` draws four things. The collision box through `NDebugOverlay::BoxDirection`
(`0x10144cd0`) ORIENTED by `m_vecForward` (`+0x6290`) at (128, 0, 0) alpha 40, when the OBB mins and
maxs differ. A ±10 `NDebugOverlay::BoxAngles` (`0x10142b80`) at the `"Bip01"` bone carrying that
bone's own ANGLES, same colour, unconditional within the arm. The collision box widened by
`m_vecAttackExtents` (`+0x50`) at (255, 128, 0) alpha 20, when that vector is not `vec3_origin`. And
the `+0x156c` hull's box at (255, 255, 64) alpha 10 when that hull differs from `m_eHull`
(`+0x1568`).

`0x20000000` draws two `NDebugOverlay::Circle` rings about the axis `(1, 0, 0)`. **The radii are the
weapon's range words and `0x42000000` is the ring HEIGHT, not the radius** — the ledger's evidence
had these the other way round, and `CNPC_VMingXiao#123` settles it. The near ring's radius is
`min(+0x8b8, +0x8bc)` at (255, 255, 32) alpha 128 and the far ring's is `max(+0x8c0, +0x8c4)` at
(128, 128, 16) alpha 128, both of height 32.0; unarmed the pair is 2000.0 and 0.0.

Under ConVar `DAT_10924f24`, five ±2 boxes at (255, 32, 32) alpha 1 for 0.1 s at the enemy's slot
197 `BodyTarget`, which is called with the NOISY flag — that is why the loop runs five times, and it
passes an UNINITIALISED stack vector as the call's `posSrc`.

With `m_pHintNode` (`+0x5ddc`) set, two 200-unit `BoxDirection` facings at (255, 0, 0) alpha 50 with
extents `(0, 0, -10)` to `(200, 0, 10)`, drawn from the hint's origin plus `m_vDefaultEyeOffset`.
The two yaws are built on `0x102d12e0`'s node yaw: for hint types 100, 0x65, 0x283c and 0x283d the
first is MINUS `hint + 0x454` and the second PLUS it; for type 0x27d8 the leaning-left arm jumps
PAST that negation and gives a bare `+43.0` with the hint's own value, while the not-leaning arm
gives minus the hint's value with `-43.0`. Every other hint type skips the arm outright.

Finally the relationship-line walk, gated on **`+0x6658` and not `+0x63fd`**. Every entity within
4096.0 units (`0x100f8490`, mask `0x820`) whose `+0x9c` is a combat character and is not this NPC
gets a line whose channel weights come from slot 404 — 1 to (4,1,1), 2 to (4,4,1), 3 to (1,4,1),
4 to (1,1,4), default (4,0,0) — each multiplied by `clamp((slot 405 + 2) * 6, 0, 0x3f)`. A null
`+0x9c` ENDS the walk rather than skipping the entity. **The arm is dead in retail**: `layout.md`
records `+0x6658` as `m_bUnread6658`, a byte the Troika constructor (`0x1028d230`) zeroes and
nothing else in the corpus touches.

**Unrecovered:** the body of `0x1029c4a0` and of its two length helpers `0x1029c9f0` and
`0x1029ca30`, none of which is a core function of the band; the port records the three calls with
their eight arguments instead. **Not built:** the collision OBB, the skeleton, the second hull, the
active weapon and the hint store, so of the four `0x1000` sub-arms only the attack-extents box draws.

### `CAI_BaseNPCTroika::NPCThinkDebugPre` — `0x10292500`

_Recovered 2026-09-14, story 29d._

The pre-think debug pass, 1,782 bytes, one caller (`NPCThink` `0x10292de0`). The decompiler reports
it DAMAGED — type propagation does not settle and both witness colour ladders come out as
`extraout_ST0` — so it was read from the listing.

Two witness boxes, each under `curtime <= timer` (the `TEST AH,0x41; JP` pair is taken for BOTH
"less" and "equal"). Each computes one scalar `x = (timer - curtime) * 0.2` (`_DAT_10451ab4`) and
then clamps **each colour channel separately** as `(int)min(ceiling, ceiling * x)`:

* `m_flCriminalWitnessedTimer` (`+0x63a4`): ceilings 255.0, 64.0, 64.0 (`_DAT_1044fffc`,
  `_DAT_10451acc`), box ±3 at `GetOrigin()` lifted by `OBBMaxs().z + 16.0` (`_DAT_10451ad0`),
  alpha 1, duration 0.5.
* `m_flSupernaturalWitnessedTimer` (`+0x63a8`): ceilings 192.0, 255.0, 64.0 (`_DAT_1049ae14`), lift
  `+ 12.0` (`_DAT_1044faa4`), otherwise identical. The ledger's evidence read this as "the same box
  with the opposite corner order" and the whole scalar "clamped to 64.0"; the corner order is
  identical — only the two stack slots the extents live in are swapped — and the ceilings differ.

Under ConVar `DAT_1092479c`, two arms. `COND_SEE_PLAYER` with a resolving `m_hClosestPlayer` traces
eye to eye, draws `CBaseEntity::DrawBBoxOverlay` on whatever it hit when that is neither the player
nor null, **and then draws the line itself** — red (255, 0, 0) when something blocked it, green
(0, 255, 0) when `tr.fraction == 1.0` (`0x10449280`, a double) or the hit IS the player. The line is
the arm's output and the ledger's evidence omitted it. `COND_ENEMY_OCCLUDED` labels
`"Blocked by %s"` (`0x105d8b44`) 20.0 units (`_DAT_1044eb0c`) above the eye, naming
`m_hEnemyOccluder` (`+0x5d90`) through `GetDebugName()` — or the literal `"**UNKNOWN**"`
(`0x105477a4`) when that handle does not resolve.

Under ConVar `DAT_109244c4`, slot 389 `Weapon_ShootPosition(GetAbsOrigin())` gets a ±3 box at
(255, 64, 64) alpha 1 for 0.2 s, `0x10278650`'s anchor gets a ±2 box at (64, 255, 64), and a line of
the same green joins them.

ConVar `DAT_1092435c` is an INT selector and not a flag: 1 calls `0x1028e030`, 2 calls `0x1028e060`,
everything else calls neither.

The tail is the body's only state write: `if (+0x5b55) { +0x5b55 = 0; 0x1027efb0(this); }`. The
clear happens only when the byte was already set.

**Unrecovered:** nothing in the body. **Not built:** story 29b records the 16 KB debug ring
(`+0x1b4e`) and its three cursors (`+0x5b50`, `+0x5b54`, `+0x5b55`) absent, so the dump is never
requested and `0x1027efb0` never runs; the eye-to-eye ray cast, `0x1028e030`/`0x1028e060` and
`0x10278650` (family Motor10's row) are seams.

### The debug ring — `0x1027ef20`, `0x1027ee20`, `0x1027efb0`

_Recovered 2026-09-14, story 29d._

`0x1027ef20` appends one line to the NPC's OWN 16 KB ring. The whole body is under
`if (param_1 != 0)`, so a null line does nothing at all. Otherwise it `sprintf`s at
`this + 0x1b4e + cursor`, advances the cursor at `+0x5b50` by the returned byte count, and — when
the POST-advance cursor is **strictly above** `0x3dff` — zero-fills `0x4000 - cursor` bytes from
there, sets the wrap latch at `+0x5b54` and resets the cursor to 0. A cursor landing exactly on
`0x3dff` does not wrap; one at `0x3e00` does. The zero-fill length is computed from the post-advance
cursor, so a cursor past `0x4000` memsets a negative length, which the `rep stos` treats as a very
large unsigned one.

`0x1027ee20` is the same shape over a GLOBAL ring rather than the entity's, and is what slots 17 and
19 use. `0x1027efb0` is the DUMP `NPCThinkDebugPre`'s tail runs, walking the buffer from the cursor
in 512-byte chunks.

**Unrecovered:** nothing. **Not built:** all three buffers are story 29b's ABSENT words. The port
carries the cursor rule as a pure function and routes the LINES to its own channel, which is what
the shape map's reason for the absence names.

### The three Troika trace messages — `0x1028de10`, `0x1028de90`, `0x1028df30`

_Recovered 2026-09-14, story 29d._

Four slots, two pairs, and the asymmetry inside each pair is the reason both members exist.

Slot 18 (`0x1028de10`, non-const, 94 bytes) formats through `0x1028d990` into a 512-byte buffer and
then, under the trace toggle `DAT_10920534`, appends the FORMATTED text to the NPC's own ring
(`0x1027ef20`); with the toggle clear it `DevMsg`s the same text.

Slot 17 (`0x1028de90`, const, 125 bytes) runs the SAME formatter into a 512-byte buffer and then, on
the ring arm, `Q_strncpy`s the **RAW** message into a SECOND 512-byte buffer and rings THAT through
the GLOBAL `0x1027ee20`. So the ring never sees the formatted text; only the `DevMsg` arm does. The
formatter still runs on the ring arm — it is called before the branch — and its output is dropped.

Slot 20 (`0x1028df30`, non-const, 89 bytes) does no formatting at all: a null message returns
immediately, and otherwise the raw message goes to the NPC's own ring or to `DevMsg`. It is
byte-identical to slot 19 (`0x1028dfb0`, story 29c-1) except that slot 19 rings the global buffer.

`0x1028d990`'s three format arms, for the record, are `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"`
(`0x105d8828`, the `DevMsg` arm with `GetDebugName`), `"%6.2f : %*s %s\n%s%s %s%s %s\n\n"`
(`0x105d8868`) and `"%6.2f : %*s %s\n"` (`0x105d8854`); its blocks are `"CONDS:"` (`0x105d8908`),
the 32-glyph `"PIS__PF_T_L__TTEPLM________ICCCC"` (`0x105d88e0`) over `m_afMemory` (`+0x5d8c`), the
30-glyph `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"` (`0x105d88b8`) over `m_bfAINPCFlags` (`+0x14b8`) and
`"NAV %s %s"` (`0x105d888c`). The indent is clamped at 0 and a null message becomes the empty string.

**Unrecovered:** nothing. **Not built:** `0x1028d990` itself is family Conditions10's row in this
same band and is not written twice; until it lands the formatter answers the message with the indent
clamp applied, which is the shortest of its three arms with every optional block empty.

### `CNPC_Crow::DrawDebugTextOverlays` — `0x10358f90`

_Recovered 2026-09-14, story 29d._

259 bytes, and the one slot-124 body in the census that chains the BASE (`0x102767d0`) instead of
the Troika one — a crow never gets the sequence, pose, disposition, position or condition lines. It
returns the base's answer unchanged when `m_debugOverlays & 1` is clear. Otherwise line N is
`"morale: %d"` (`0x10628c84`) with `m_nMorale` (`+0x5f50`), and, only with an enemy, line N+1 is
`"enemy (dist): %s (%g)"` (`0x10628c68`) with the enemy's **classname** (`GetClassname`, not
`GetDebugName`) and the crow's own cached `m_flEnemyDist` (`+0x5f4c`). Retail pushes the float FIRST
and re-dispatches `GetEnemy()` afterwards for the classname.

**Unrecovered:** nothing. **Not built:** `CNPC_Crow`'s two words; the morale reads 0 and the
distance 0.0.

### `CNPC_VHengeyokai::DrawDebugTextOverlays` — `0x10383560`

_Recovered 2026-09-14, story 29d._

106 bytes: the Troika body for the first free line, then — under `m_debugOverlays & 1` — slot 9's
string printed with NO format string at all, the empty string (`DAT_106b8540`) substituting for a
null one, and the return is that line plus one. The `+1` is the contract every later overlay body
consumes.

**Unrecovered:** nothing. **Not built:** slot 9 is a generated stub owned by another story, so the
line prints retail's null arm.

### `CNPC_VNewscaster::DrawDebugTextOverlays` — `0x103a1250`

_Recovered 2026-09-14, story 29d._

151 bytes. A scope-trace push carrying `GetDebugName()` — `"NULL ENTITY"` (`0x105387dc`) for a null
`this`, the empty string for a null `m_iName` — then the Troika body, then, under
`m_debugOverlays & 1`, `0x103a0ff0`'s story-queue lines. `0x103a0ff0` answers a COUNT and the
newscaster ADDS it to the Troika body's line index, so the two bodies share one budget.

**Unrecovered:** nothing. **Not built:** `0x103a0ff0` is family Species' row in band 5–9 and is not
re-ported here; the count reads 0 until the two are wired.

### `CNPC_VTzimisce::DrawDebugTextOverlays` — `0x103c08d0`

_Recovered 2026-09-14, story 29d._

387 bytes. The Troika body, then under `m_debugOverlays & 1`: the distance starts at
`_DAT_104454c4` = 0.0 and becomes the full 3-D separation between `m_hPickupTarget` (`+0x6670`) and
this NPC when that handle resolves. When `0x103be130`'s carry probe stands AND the distance exceeds
`_DAT_1047a3ac` = 160.0, the distance is latched into `_DAT_1093d01c` and the current schedule's
name (`+0x5c38` then `+0x40`) is `strcpy`ed into the buffer `DAT_1093cd70`. The line is then
`"Body - %5.1f|%5.1f|%s"` (`0x1065c904`) with the live distance, the latched distance and the
latched name, and the return is the line plus one.

**The two globals are a cross-NPC latch and not per-instance state**: every Tzimisce in the map
writes and reads the same pair, which is why the printed pair can name a schedule this NPC is not
running.

**Unrecovered:** nothing. **Not built:** `0x103be130` has no verdict row and no port counterpart, so
the latch never updates.

### `CNPC_VZombie::DrawDebugTextOverlays` — `0x103e0e80`

_Recovered 2026-09-14, story 29d._

224 bytes. The Troika body, then — under `m_debugOverlays & 0x40000`, which is NOT bit 0 like its
siblings — one line per set bit of the 0..0xbf bitfield at `+0x5c5c`. Each id is offset by
1,000,000,000, converted back through `ConditionGlobalToLocal` (`0x102ea280`) over
`GetClassScheduleIdSpace() + 0x30`, named through slot 458 and printed as `"Cond: %s\n"`
(`0x10665864`); the line index advances once per printed condition and is the return. The body's
`id == -1` guard is unreachable, the loop starting at 0.

**Unrecovered:** nothing. **Not built:** `+0x5c5c` is one of the schedule block's six words with no
port member of its own, so the walk finds nothing to print.

### `CNPC_VCop::DrawDebugGeometryOverlays` — `0x10372f00`

_Recovered 2026-09-14, story 29d._

1,020 bytes, debug-only, but the arms are the recovered statement of what a cop knows about the
player. Three gates in order: `m_debugOverlays & 1`, a resolving `m_hClosestPlayer`, and a
non-degenerate collision OBB. Each failure jumps straight to the tail, and **the Troika body always
runs**, whichever gate closed.

The label sits `(OBBMaxs().z - OBBMins().z) + 8.0` (`_DAT_1045597c`) above `GetAbsOrigin()` and is
built by a switch on slot 404 `IRelationType(closestPlayer)`: 1 to `"D_HT"` (`0x105cc520`),
2 to `"D_FR"` (`0x105cc518`), 3 to `"D_LI"` (`0x105cc510`), 4 to `"D_NU"` (`0x105cc508`), and 0 or
anything above 4 to `"D_ER"` (`0x10636728`).

The `D_HT` arm first re-runs the TROIKA `IRelationType` (`0x10299da0`) and throws the answer away,
then appends four suffixes, each independently: `" Suspect"` (`0x10636750`) when the player IS the
cop class's shared provoker handle `DAT_1093ac3c` and `curtime < _DAT_1093aca8`; `" Alert"`
(`0x10636748`) when `0x1017f8d0` stands on the player's `+0xa8` record; `" Count%d"` (`0x1063673c`,
no space before the number) when `0x1017f770` answers above zero; and `" Pursuit"` (`0x10636730`)
when the player is `m_hPursuitPlayer` (`+0x6664`). Every arm then appends `"  %d  %d"`
(`0x1063671c`) with two further cop-class statics, `DAT_1093acac` and `DAT_1093acb0`, and draws the
whole label through `NDebugOverlay::Text` with `bViewCheck` false and duration 0.

**Unrecovered:** what `DAT_1093acac` and `DAT_1093acb0` count; neither has a writer in this band.
**Not built:** the collision OBB, so the label gate never opens for a spawned NPC; the four cop
statics and `m_hPursuitPlayer`, so all four suffixes are dropped. The two player words ARE real —
`Police.CopsInPursuit` and `Police.HeightenedAlertExpiry` — and are read when the candidate is the
player. Note also that the census gives `CNPC_VCop` no entity classname, so a spawned `npc_VCop`
resolves to no census class at all and takes the Troika body: this override is unreachable in the
port for the same reason it is unreachable through the port's spawn registry.

### `CNPC_VMingXiao::DrawDebugGeometryOverlays` — `0x10399d40`

_Recovered 2026-09-14, story 29d._

328 bytes, and the shortest body in the family with a recovered fact in it. Gated on
`m_debugOverlays & 0x20000000` — the WEAPON-RING bit, not bit 0 like its siblings, so MingXiao's
bands and the Troika body's two weapon rings appear together and are meant to be read against each
other. Four `NDebugOverlay::Circle` rings about `(1, 0, 0)` at radii **100.0, 150.0, 200.0 and
300.0** source units, each of height 8.0, colour (255, 32, 32) at alpha 128, no depth test,
duration 0. The Troika body then always runs. The four radii are MingXiao's recovered range bands
and are also what settles the radius/height argument order of the Troika body's own rings.

**Unrecovered:** nothing.
## Story 29d, families Anim10 and SpeciesAnim10 — activity, sequence, pose and model

_Recovered 2026-09-14, story 29d._

Twenty-seven `rule` rows across seven retail surfaces: slot 105 `SetModel`, the activity triple
(`SetActivityAndSequence`, the base `SetActivity`, `MaintainActivity`, `AdvanceToIdealActivity`),
slot 310 `SetActivity`, slot 375 `NPC_EarlyTranslateActivity`, slot 314 `UpdatePoseParameters`,
slots 326 / 330 / 359 / 584, `CAI_BaseHumanoid`'s own slot-333 body, and family SpeciesAnim10's
four species arms of slots 604 and 509.

**Animation is state, not a picture.** An activity id, the sequence chosen for it, the answer a
translation table gives and the ORDER of a pose write against a dispatch are all things a retail
program observes. `SetActivityAndSequence` fires slot 465 and the global activity-change listener
on two DIFFERENT edges; `SetActivity`'s two refusals are what make a repeated request a no-op; and
`MaintainActivity`'s `ACT_TRANSITION` arm is what lets a transition clip finish. Only the rendering
of a chosen clip is visual, and nothing below is taken as visual-only.

**Slot 375 was split across two surfaces and no longer is.** Before this story the visual animation
path walked the generated `PreTranslate_*` rows through `ElysiumActionTables::NpcTranslate` while
the kernel's slot 375 was a stub answering 0, and the table walker's live-predicate evaluator
answered FALSE to every predicate but two — so its frenzy, gait, cover, reload and form rows could
not fire at all. The kernel body below is now the interpreter, and `FElysiumNpc::
PreTranslatePredicate` is the ONE evaluator both surfaces read: every predicate in the generated
vocabulary is answered from the same read the slot-375 body makes.

### `CAI_BaseNPCTroika::SetModel` — `0x10298ce0`

_Recovered 2026-09-14, story 29d._

Fifty bytes and four calls, and the ORDER is the whole body because each reads what the one before
it wrote: `CBaseCombatCharacter::SetModel(name)`, then `SetHullSizeNormal(force = 1)`
(`0x10273070`), then `SetDefaultEyeOffset` (`0x10274ca0`), then `+0x64e8` from the disposition
stance-table row resolver (`0x100ec640`). The model must be set before the hull and the hull before
the eye — `SetDefaultEyeOffset`'s fallback arm reads the mins and maxs `SetHullSizeNormal` has just
written.

Three classes replace it. `CAI_BaseHumanoid` (`0x1025e510`) is a different body entirely;
`CNPC_VGhoulCroucher` (`0x1037b1f0`) and `CNPC_VZombie` (`0x103e0540`) share one 119-byte body that
runs this one FIRST.

**Unrecovered:** nothing.

### `CAI_BaseHumanoid::SetModel` — `0x1025e510`

_Recovered 2026-09-14, story 29d._

485 bytes: `CBaseCombatCharacter::SetModel` and then twenty-six indices cached into the consecutive
words `+0x5fb8`..`+0x601c`, in this order — `body_trans_Y`, `body_trans_X`, `body_lift`,
`body_yaw`, `body_pitch`, `body_roll`, `spine_yaw`, `spine_pitch`, `spine_roll`, `neck_trans`,
`head_yaw`, `head_pitch`, `head_roll`, `move_rightleft`, `move_forwardback`, `move_updown`,
`body_rightleft`, `body_updown`, `body_tilt`, `chest_rightleft`, `chest_updown`, `chest_tilt`,
`head_forwardback`, `head_rightleft`, `head_updown`, `head_tilt`.

**The cache is not one lookup repeated.** The first THIRTEEN go through
`CBaseAnimating::LookupPoseParameter`, which answers `-1` on a miss; the last THIRTEEN go through
`thunk_FUN_100b5d10`, which is `LookupFlexController` — the body story 29c-1 recovered as answering
**0**, not -1, when nothing matches. So the two halves of one array take different miss values, and
a misspelt flex name caches controller zero rather than being dropped. Indices 10..12 (`head_yaw`,
`head_pitch`, `head_roll`) are the three `0x1025efc0` zeroes at the top of every
`MaintainEyeDirection` pass, which is what ties the two bodies together.

`CAI_BaseHumanoid` sits under `CAI_BaseActor` on a sibling SDK branch with 590 vtable slots and no
entity classname anywhere in the 77-class census, so no spawned `npc_*` reaches this arm. It is
recovered because it is the only statement of what those twenty-six words are.

**Unrecovered:** nothing.

### `CAI_BaseNPC::SetActivityAndSequence` — `0x10272490`

_Recovered 2026-09-14, story 29d._

243 bytes, the commit every activity change ends in, and every one of its six effects is ordered
against the others.

1. `m_TranslatedActivity` (`+0x0ff4`) is written FIRST, before anything below can read it.
2. A NEGATIVE sequence takes `ResetSequence(0)` and skips the cycle word, the duration and the
   weapon activity — but not the rest of the body.
3. Otherwise `m_flCycle` (`+0x06f8`) is zeroed UNLESS the sequence equals `m_nSequence` (`+0x06f0`)
   with `+0x065d` set, OR the old activity (`+0x0fec`) and the new one are BOTH in
   `{9 ACT_WALK, 0x13 ACT_RUN}`. That pair is retail's walk/run cycle carry, and it is what keeps a
   footfall in phase across a gait change.
4. `ResetSequence(seq)`, `CBaseAnimating::SequenceDuration(seq)`, then
   `CBaseCombatCharacter::Weapon_SetActivity(weaponActivity, duration)`.
5. Slot 533 `EyeOffset(activity, m_TranslatedActivity)` feeds `SetViewOffset` (`0x1009f380`) — for
   EVERY request, the negative-sequence one included.
6. Slot 465 `OnChangeActivity(activity)` fires only when `m_Activity` differs from the NEW
   ACTIVITY; the global activity-change listener (`0x101f6010` over `DAT_1073dd58`) fires on a
   DIFFERENT comparison — `m_Activity` against the TRANSLATED activity — and only while
   `m_bKeepSound` (`+0x5cd8`) is clear. Both read `m_Activity` before step 7 overwrites it.
7. `m_Activity = activity`, then `CAI_Navigator::OnNewActivity` through `m_pNavigator` (`+0x5d44`).

`m_bKeepSound` exists for exactly one caller: `CAI_BaseNPCTroika::SetActivity` raises it around its
random fidget/hunt pick so the listener is not told about the intermediate activity.

**Unrecovered:** nothing.

### `CAI_BaseNPC::SetActivity` — `0x102725d0`

_Recovered 2026-09-14, story 29d._

Slot 310's BASE body, a distinct retail function beside the Troika override `0x10295750` that owns
the slot. Two refusals then three writes:

```
if ((m_Activity != act) && (act == 0 || m_Activity != 2)) { ... }
```

A request equal to what is already playing is a NO-OP — which is why `RestartIdealActivity`
(`0x10289ee0`) has to clear `m_Activity` before asking — and a body in `ACT_TRANSITION` (2) refuses
everything except `ACT_RESET` (0). Past them: `m_IdealActivity` takes the request,
`ResolveActivityToSequence` (`0x10272130`) fills `m_nIdealSequence`, `m_IdealTranslatedActivity` and
`m_IdealWeaponActivity`, and `SetActivityAndSequence` commits all four — re-reading
`m_IdealActivity` rather than using its own argument.

**Unrecovered:** nothing.

### `CAI_BaseNPC::MaintainActivity` — `0x102727d0`

_Recovered 2026-09-14, story 29d._

Gated on slot 466 `ShouldMaintainActivity`. Inside, work happens only when `m_Activity` differs
from `m_IdealActivity` OR `m_nSequence` from `m_nIdealSequence` — either mismatch is enough.

Activity 2 is the special arm: it WAITS for `m_bSequenceFinished` (`+0x065c`) and then runs
`AdvanceToIdealActivity` (`0x102726a0`) and nothing else. That is what lets a transition clip play
out. Every other activity re-resolves the ideal through `0x10272130` FIRST and then advances. The
body writes no field of its own; the rest of its 230 bytes is the scope-trace push and pop.

Not a vtable slot — `__thiscall` with no dispatch site — so it is reached by direct call, and
`m_bForceMaintainActivity` (`+0x65fa`) brackets exactly one of them (`FUN_10290350`, the
alternate-AI door transaction).

**Unrecovered:** nothing.

### `AdvanceToIdealActivity` — `0x102726a0`

_Recovered 2026-09-14, story 29d._

`FindTransitionSequence(m_nSequence, m_nIdealSequence)`; a `-NAN` answer — retail's own sentinel,
produced by comparing the returned float against itself — dispatches slot 310 with
`m_IdealActivity` and stops. A transition sequence that is NOT the ideal one commits activity **2**
with that sequence, re-resolving the translated and weapon pair from the transition sequence's OWN
activity when `GetSequenceActivity` gives it one; retail seeds both locals with 2 first, so a
transition whose sequence carries no activity commits `(2, seq, 2, 2)`. Only when the transition IS
the ideal sequence does the ideal activity commit outright.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::SetActivity` — `0x10295750`

_Recovered 2026-09-14, story 29d._

612 bytes, slot 310, and three top arms. Every request outside the `0x1093`-`0x1096` and
`0x1115`-`0x1117` families forwards to `0x102725d0` unchanged.

**Request `0x1093`, the idle-fidget family.** When `m_Activity` is OUTSIDE `0x1093`..`0x1096`, hand
`0x1093` to the base and stop. When it is inside and slot 251 `IsActivityFinished` is FALSE, the
body **does nothing at all** — no base call, no roll — which is the arm that makes a fidget play to
its end. When it is inside and finished: `m_bKeepSound` 1, `RandomInt(0, 99)`, and `0x1095` below
15, `0x1096` below 30, `0x1094` below 40, else `0x1093`; `m_bKeepSound` returns to 0 on every one
of the four exits.

**Request `0x1115`, the hunt-walk family.** `m_bfNPCFrenziedFlags` bit `0x40` forces
`0xf17 ACT_RUN_FRENZY` unless it is ALREADY playing, in which case the body does nothing rather
than restarting it; else bit `0x20` forces `0x13 ACT_RUN` on the same terms; else, outside
`0x1115`..`0x1117`, hand `0x1115` down; else, if `IsActivityFinished`, the two roll bounds start at
15 and 15 and widen to **8 and 0x18** only when slot 168 `GetEnemy` is non-null AND slot 541's
enemy memory carries a record for him (`0x102dfa20`). On that path retail reads the last known
position (`0x102dfed0`) and its own slot-217 origin into stack vectors and DISCARDS both; the reads
are dispatches a program can observe, the values go nowhere. Then `m_bKeepSound` 1,
`RandomInt(0, 99)`, `0x1116` below the first bound, `0x1117` below the sum, else `0x1115`, and
`m_bKeepSound` 0.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::NPC_EarlyTranslateActivity` — `0x10295590`

_Recovered 2026-09-14, story 29d._

295 bytes, slot 375, five steps in this order.

1. The gait-override ConVar `DAT_10924d6c` (the `vtable+4` "is this a command" probe, value at
   `+0x2c`). Value 1 rewrites `9 ACT_WALK` and `0x1115 ACT_HUNT_WALK` to `0x13 ACT_RUN`; value 2
   rewrites `0x13` to `9`.
2. `m_bfNPCFrenziedFlags` (`+0x5b84`) bit `0x40` rewrites
   `{0x17, 9, 0x13, 0x16, 0x1115, 0x1121}` to `0xf17 ACT_RUN_FRENZY`; ELSE bit `0x20` rewrites
   `{0x1115, 9, 0x16, 0x1121}` to `0x13`. The two are an `else if`, so `0x40` wins outright.
3. `3 ACT_FIDGET` becomes `1 ACT_IDLE` — **and only when step 2 took neither rewrite**, because
   both of those arms `goto LAB_10295655` past this test. Unobservable, since 3 is in neither
   rewrite set; recorded because the listing says so.
4. **One capability test in front of BOTH delegates.** `10295655 CALL [vtable+0x804]` (slot 513
   `CapabilitiesGet`) then `1029565f TEST 0x8000000`: under that bit, `0x55 ACT_RELOAD_FAST`
   RETURNS slot 570 `GetReloadActivity(m_pHintNode)`'s answer, and `6 ACT_COVER` — or `1 ACT_IDLE`
   when `m_afMemory` (`+0x5d8c`) bit 2 is set — RETURNS slot 569 `GetCoverActivity(m_pHintNode)`'s.
   Neither falls through.
5. Otherwise `CBaseCombatCharacter::NPC_EarlyTranslateActivity`, which is the identity.

**Correction.** The earlier one-line walk placed only the reload delegate under `0x8000000` and left
`ACT_COVER` outside it; so did the generated `PreTranslate_Troika` table, whose `ACT_COVER` row was
predicated `Always`. The listing puts one `TEST` in front of both.

**Unrecovered:** the ConVar's registered name and default; `0` is the value a defaulted object
answers and is the arm the port takes.

### `CNPC_VHuman::NPC_EarlyTranslateActivity` — `0x103854f0`

_Recovered 2026-09-14, story 29d._

565 bytes and slot 375 for **39** census classes — the armed/alert decision tree, then the
rewrites. It writes exactly one field, `m_bAggressiveAnims` (`+0x6410`), on four different paths,
and every rewrite below reads only that.

1. Slot 513 `CapabilitiesGet` bit `0x40` rewrites `0x11 ACT_WALK_AIM` to `9` and
   `0x15 ACT_RUN_AIM` to `0x13`. This runs BEFORE the weapon test, so it lands on an unarmed body
   too.
2. With NO active weapon, or one whose `+0x19c` carries `0x40 NODRAW`, `m_bAggressiveAnims` is
   CLEARED and the request goes straight to `0x10295590`, skipping both rewrite blocks. Retail
   calls `GetActiveWeapon()` twice here; the second call is what reads `+0x19c`.
3. Otherwise the tree. The flag is SET when capability `0x40` stands AND the ConVar `DAT_10924f74`
   is live with a non-zero `+0x2c` AND `m_bfAINPCFlags2` (`+0x14bc`) carries
   `0x400 MOVE_FACE_ENEMY`. Failing that: `m_bfAINPCFlags` (`+0x14b8`) `0x10000 FORCE_RELAXED_ANIMS`
   clears it and beats everything below; `m_bfNPCFrenziedFlags` `0x200` skips the state ladder and
   SETS it; otherwise the flag is cleared and then the state ladder decides — `m_NPCState` 2 sets
   it, and for state 3 or `0xb` **it is set when the matching ConVar (`DAT_10923f5c` for 3,
   `DAT_10924034` for `0xb`) is LIVE and non-zero, OR `m_afMemory` carries `0x8000000`**. It stays
   clear only when that ConVar is dead or zero AND the memory bit is clear — `goto LAB_10385611`
   requires both terms. The pack-07 walk had this arm inverted.
4. Aggressive CLEAR rewrites `9` to `0x16` and `0x13` to `0x17`, through `0x10295590` rather than
   by returning.
5. Aggressive SET: `1 ACT_IDLE` becomes `5` ONLY when there is an active weapon whose slot 360
   (`+0x5a0`) answer carries `0x6000`; then the six fixed pairs `0x3b`→`0x3d`, `0x3c`→`0x3e`,
   `0x9d`→`0x9f`, `0x9e`→`0xa0`, `0xa1`→`0xa3`, `0xa2`→`0xa4`.
6. Everything else falls to `0x10295590` unchanged.

**Unrecovered:** the three ConVars' names and defaults, and the weapon's `+0x19c` draw-flag word,
which no port member carries.

### `CNPC_VTzimisce::NPC_EarlyTranslateActivity` — `0x103bde40`

_Recovered 2026-09-14, story 29d._

Gated on `0x103be130`, which is `m_bfAINPCFlags` (`+0x14b8`) bit 5 `0x20 CARRYING_BODY`. Under it
the polarity is a ZERO test: `m_bHeavyBodyTarget` (`+0x6688`) **CLEAR** returns `0xfd` for request
1 and `0xff` for 9 or `0x13`, and SET returns `0xfc` and `0xfe`. Every other request, and the whole
un-flagged case, falls to `0x10295590`.

The generated `PreTranslate_Tzimisce` rows carry the right ids; its `BodySideLeft` predicate was
documented with the opposite polarity and is corrected with this body.

**Unrecovered:** what writes `+0x6688`.

### `CNPC_VTzimisceRunner::NPC_EarlyTranslateActivity` — `0x103c3e10`

_Recovered 2026-09-14, story 29d._

It chains `0x10295590` **first** and only then remaps the TRANSLATED activity under a non-zero form
byte `+0x6672` — so it is a post-pass on the base's answer and not a replacement, which is exactly
why its slot-310 twin `0x103c3d80` remaps the REQUEST instead and the two look alike but are not.
The switch is on the incoming activity: 1 and `0xf1` become `0x1134`, 3 `0x1135`, 9 `0x1136`,
`0x13` `0x1137`, everything else passes through.

Because the base has already rewritten `3 ACT_FIDGET` to `1 ACT_IDLE`, the `0x1135` row is
unreachable through the chain and a fidget request answers `0x1134`.

The generated `PreTranslate_TzimisceRunner` rows keyed `RunnerVariantIs == 0..3` with no `From`
activity, which is the wrong key; the generator is corrected with this body.

**Unrecovered:** what writes `+0x6672`.

### `CNPC_VTzimisceRunner::SetActivity` — `0x103c3d80`

_Recovered 2026-09-14, story 29d._

110 bytes. With `+0x6672` non-zero the REQUEST is remapped before `0x10295750`, tested in retail's
own order — 1, 9, `0x13`, 3, `0xf1`, which is not numeric order — to `0x1134`, `0x1136`, `0x1137`,
`0x1135`, `0x1134`. With the byte clear every request forwards unchanged.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::UpdatePoseParameters` — `0x102bf070`

_Recovered 2026-09-14, story 29d._

528 bytes, slot 314. The aim target is `m_hShootTargetOverride` (`+0x5ba8`) resolved and, failing
that, slot 167 `GetEnemy`; retail's `goto` pair means a LIVE override enters the aim arm even when
`GetEnemy()` is null. `m_iIsOblivious` (`+0x5bb4`) at 1 or more takes the no-aim arm even WITH a
target, because the test is INSIDE the aim arm and not in front of it.

The no-aim arm writes `m_bAimWeaponAtTarget` (`+0x0e6c`) = 0, `m_flSet_PoseParameters` (`+0x1064`)
= `_DAT_104454c4` = 0.0 and `+0x1068` = 0.0.

The aim arm: slot 193 `EyePosition`, `0x10278650` for the aim point, the delta normalised;
`m_bAimWeaponAtTarget` = 1; `m_hWeaponAimTarget` (`+0x0e70`) = the target's handle or `0xffffffff`;
`m_vWeaponAimOffset` (`+0x0e74`) = the aim point minus the TARGET's slot-217 origin;
`VectorAngles(delta)` with the pitch biased by `_DAT_1049ae9c` and the yaw taken relative to slot
219 `GetAbsAngles`; then both clamped low-bound-first to `_DAT_1049a180` (-45.0) and `_DAT_1049949c`
(45.0) into `+0x1064` and `+0x1068`.

`CBaseCombatCharacter::UpdatePoseParameters(interval)` is the tail on BOTH arms.

**`_DAT_1049ae9c` is 7.5f**, read out of the pinned image's `.rdata` on 2026-09-14; the checklist
left it unnamed.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::vfunc326` — `0x1029fec0`

_Recovered 2026-09-14, story 29d._

81 bytes over the base `0x103482e0`. The Troika arm can only ever NARROW the base's answer: a base
FALSE passes straight through, and a base TRUE becomes false unless `m_iHitBuildupCount` (`+0x6064`)
is at or below the int of the ConVar `DAT_109245e4` (`+0x2c`, 0 when the object is defaulted) OR
the SWING RECORD's byte `+0xba` is 2. `+0xba` is read off `param_1`, the FIRST argument, not off
the attacker.

**Unrecovered:** the ConVar's name; the port reads the same value
`docs/vtmb/combat-and-damage.md` § "Who may be knocked back" recovered.

### `CAI_BaseNPCTroika::vfunc330` — `0x1029fbe0`

_Recovered 2026-09-14, story 29d._

205 bytes, `void(float, FireBulletsInfo_t*)`, the near-miss flinch. `delta = GetAbsOrigin() -
info->m_vecSrc` (`+0x08`..`+0x10`), and its LENGTH comes out of `0x10137220` — which family Senses10
recovered as `VectorNormalize`, a body that ANSWERS the length while normalising in place. Retail
hands the UN-normalised delta to the reaction, because the normalise writes through `ECX`, which
points at a second copy.

The whole body is gated on three non-null terms in this order: `info + 0x98` (the firing weapon),
`info + 0x94` (the attacker) and `attacker + 0x9c` (the attacker's combat-character record). Past
them, the weapon's `CVDmg_t` row (`0x102517e0`, three rows of stride `0xec84` searched on the
weapon's `+0x848`) gives two distance bands: a length below `row + 0x404` plays reaction kind 2,
else a length below `row + 0x408` plays kind 0, both through `0x10344f80`. Beyond `row + 0x408`
nothing happens at all.

`0x10344f80`'s own three refusal gates are exactly the knockback-eligibility rule's — slot 400
`AllowsKnockbackBypass` first, then the template byte `+0x9e Disallow_Knockbacks`, then
`CVStatList_t::IsEqual(0x0f, 0x11)`.

**Unrecovered:** the `CVDmg_t` table, so the two distance bands have no values here.

### `CAI_BaseNPCTroika::vfunc359` — `0x1029e750`

_Recovered 2026-09-14, story 29d._

Slot 359 (`vtable+0x59c`) is the **Auspex aura index**.
`CBaseCombatCharacter::UpdateAuspex` (`0x100532b0`) calls it with the observer and uses the answer
as `DAT_10937160[idx + IsKindred * 8]` to pick the aura colour, treating a NEGATIVE as no aura at
all. Arms in retail's order:

* `CBaseEntity::EntityUnselectable(this)` true → `-1`.
* `m_iCurFrenzyCount` (`+0x146c`) greater than 0 → `4`.
* `m_bfAINPCFlags2` (`+0x14bc`) bit `0x80000` `D_MILDLY_CRAZY` → `3`. **This test comes before the
  state switch**, so a mildly-crazy body never reads its state.
* otherwise a switch on `m_NPCState` (`+0x5cc0`): 2 and `0xb` → `1`; 3 and `0xe` → `7`; 5, 6, 8, 9,
  10 and `0xd` → `0`; 7 → `-1`; the default arm, with a non-null observer whose slot 404
  `IRelationType` answers `1 D_HT`, → `2`, else `5`.

**Unrecovered:** what `EntityUnselectable` reads; and what colours `DAT_10937160`'s sixteen cells
carry.

### `CAI_BaseHumanoid::MaintainEyeDirection` — `0x1025fa50`

_Recovered 2026-09-14, story 29d._

2,226 bytes and `CAI_BaseHumanoid#333` — a distinct body beside the Troika slot-333 one
(`0x102bff20`). Read off the LISTING: the decompiler aliases six stack floats across the three
passes (`fStack_78` is both the normalised time and a head-vector component, `fStack_88` both a
ConVar value and the loop counter) and loses which vector is which twice.

**Pass 1, `1025fa5c`.** When `m_iszExpressionScene` (`+0x5f9c`) is set and the handle at `+0x5fa0`
does NOT resolve, `PlayScene(name, &m_hExpressionSceneEnt)`. A null `string_t` reads as the empty
string, so retail plays the EMPTY scene rather than skipping.

**Pass 2, `1025fa98`.** Slot 283 `ProcessSceneEvents`, slot 193 `EyePosition`, `0x1025efc0` (which
writes pose parameters `HumanoidPoseParams[10..12]` to 0, flushes the bone cache and clears the two
cached-direction bits of `+0x5f4c`), then slot 371 `HeadDirection3D` into the running head vector.

**Pass 3, `1025facf`.** The look queue at `+0x5f88` (0x24-byte records, count at `+0x5f94`) is
pruned: an entry whose END stamp (`entry + 0x18`) is past, or whose kind (`entry + 0x00`) is 0 with
a dead handle at `entry + 0x04`, is dropped by a `memmove` of the tail (`0x10260820`) and the count
decremented. **The walk does not advance its index on a removal** — `1025fb17` jumps over the
`INC EBX` — so two adjacent dead entries are both caught.

**Pass 4, `1025fb25`.** The classname compare against `"cycler_actor"` happens first and
unconditionally, because its `SETZ BL` result is used by both arms. Then, **only when slot 586
answers false** — and slot 586 on this branch is `0x1025f1a0`, whose whole body is
`return m_LookTargets.Count() != 0`, so the gate is "I have nothing to look at" and the block is an
ACQUISITION: a body in `m_NPCState` 4 `NPC_STATE_SCRIPT` takes the cycler arm only if it IS a
`cycler_actor` and otherwise does **nothing at all**, no `PickLookTarget`; any other state takes the
cycler arm for a `cycler_actor` and `PickLookTarget(false, 1.5, 2.5)` for everyone else. The cycler
arm is `AddLookTarget(UTIL_PlayerByIndex(1), 0.5, RandomFloat(DAT_1090fc0c, DAT_1090fc9c))`, each
ConVar reading 0.0 when its `vtable+4` probe answers true.

The earlier walk read `+0x928` as "is it in a scene"; it is `HasActiveLookTargets`.

**Pass 5, `1025fc13`.** Every surviving entry that does not name THIS entity accumulates. For each:
`t = (curtime - entry[+0x14]) / (entry[+0x18] - entry[+0x14])`, zero-weighted strictly below 0 and
strictly above 1 (both bounds inclusive — `1025fc71`'s `TEST AH,5 / JNP` and `1025fc82`'s
`AND 0x4100 / JZ`); ramped by `entry[+0x1c]` (`f = t / ramp` below the ramp, `1.0` in the middle,
`(1.0 - t) / ramp` past `1 - ramp`, the 1.0 being the DOUBLE at `0x10449280`); and scaled by
`entry[+0x20]` through the cubic `3f² - 2f³` (`_DAT_10449258` = 3.0). An entity-backed entry
refreshes its cached position from the entity's own `EyePosition` first. The offset from the eye is
normalised and dotted against slot 371 — re-dispatched per entry — and accepted on
`dot >= _DAT_10497ca0`, which is a **DOUBLE reading -0.5**, so a target up to 120° off the head
direction is admitted. An accepted entry blends into the head vector as
`head * (1 - w) + offset * w` and the head is NOT re-normalised inside the loop.

**Pass 6, `1025fe49`.** With at least one accepted entry: slot 537 `SetHeadDirection(eye + head *
100.0, interval)` is called and THEN `+0x5f74`..`+0x5f7c` is written. With none — the empty queue
included — the stored vector decays first, `stored * 0.8 + head * 0.2`, is written, is NORMALISED
in place, and only then is slot 537 called. **The two arms differ in order, and only the decay arm
normalises.**

**Pass 7, `1025ffce`.** The view target, walked BACKWARDS from `count - 1`. The first entry naming
THIS entity takes `eye + head * 100.0` — reading the stack copy of `HeadDirection3D`, which on the
decay arm is still the ORIGINAL head direction. Otherwise an entry whose position passes slot 587
(`0x1025e920`, the 3-D 0.5 eye-target cone) claims the view target and the walk STOPS; retail
refreshes an entity-backed position twice more on that path. When the walk finds nobody, slot 587
is asked about the CURRENT view target (slot 278) and, only when that refuses, the fallback is
built: `VectorVectors(HeadDirection3D(), &right, &up)` and then
`eye + forward * 128.0 (_DAT_1046dcd0) + right * RandomFloat(-16, 16) + up * RandomFloat(-32, 32)`.
The earlier walk called this "slot +0x458 plus a velocity-projected point"; there is no velocity in
it.

**Pass 8, `102601e1`.** When `+0x5f80` is strictly past `curtime`, `+0x0854` is **toggled**
(`x = (x == 0)`, not set) and `+0x5f80` is re-armed to `curtime + RandomFloat(1.5, 4.5)`. The
compare is strict, so a deadline exactly at `curtime` does not fire.

**Unrecovered:** the two cycler ConVars' names and defaults.

### `CNPC_VGhoulCroucher::SetModel` and `CNPC_VZombie::SetModel` — `0x1037b1f0`, `0x103e0540`

_Recovered 2026-09-14, story 29d._

The SAME 119 bytes on two classes. `CAI_BaseNPCTroika::SetModel` (`0x10298ce0`) runs FIRST — so the
model write, the hull and the eye all happen before `IsMale` is ever asked — then
`CBaseCombatCharacter::IsMale` selects `m_iszVSoundGroup` (`+0x00c0`) as `"Zombie_Male"`
(`0x1063b150`) or `"Zombie_Female"` (`0x1063b140`), each through the `-(s[0] != 0) & addr` idiom
that yields a null `string_t` for an empty literal; then `+0x00bc` takes the literal **2**,
unconditionally and AFTER the gender read; then the group (substituting the empty string when null)
is resolved through the vocalization registry `0x101f55a0` over `DAT_1073dc28` with the create flag
**clear**, and the record pointer stored into `+0x00b4`.

So the sound group is bound at MODEL time on these two classes, where `CNPC_VWerewolf::Precache`
makes the same three writes once at precache.

**Unrecovered:** nothing.

### `FUN_10260dc0` — `CAI_ExpressiveNPC`'s slot 584

_Recovered 2026-09-14, story 29d._

Eleven bytes: `MOV ECX,[ECX+0x5f48]` then `JMP 0x100116d0` — a tail jump through the Expresser
pointer at `+0x5f48` into `0x10311c10` with the concept id and a modifier string, and no pre- or
post-work at all. `CAI_BaseHumanoid` and `CAI_ExpressiveNPC` are its only two census rows; the
Troika line fills slot 584 with `0x1028d910` (`ResetAllThinkStamps`), which has nothing to do with
it. The two bodies do not even share an arity.

**Unrecovered:** nothing. **Not reachable:** neither class carries an entity classname in the
77-class census.

### `CNPC_VHuman::SelectScheduleMeleeCombat` — `0x10385e40`

_Recovered 2026-09-14, story 29d._

1,449 bytes and slot 604 for **34** census classes — the most-inherited melee selector in the game.
It REPLACES the Troika body `0x102b6c30` wholesale and never chains it. Every return also stamps the
selector trace, `+0x1b30` with `"E:\Vampire\main\dlls\hl2_dll\NPC_VHuman.cpp"` and `+0x1b34` with
the line.

**Out of melee** (`m_bInMelee` `+0x6078` clear) **with slot 599 refusing**: the melee failure gate
`0x102b6fe0` is offered first and any non-zero answer returns; then, with the melee-range ConVar
`DAT_10924a1c` (0.0 when its `vtable+4` probe answers true, else the float at `+0x28`), the far arm
needs `range + 200.0 (_DAT_104492b8)` **strictly below** `m_flEnemyDist` (`+0x6268`) and answers
`0xe7` (line 1563); otherwise `RandomInt(0, 99)` must **exceed 24** (`CMP EAX,0x19; JL`) and the
distance must be at or beyond the bare range for `0xe5` (line 1575), else `0xe4` (line 1571).

**In melee with slot 602 agreeing to leave**: slot 601, then slot 308 gives `0xe9` (line 1535);
else `2 * range` **at or below** the distance gives `0xe7` (line 1541) — `10385ee1`'s
`TEST AH,0x41; JP` admits equality — else `0xe4` (line 1545).

**The common tail** offers `0x102b7370` `SelectDoorObstructionSchedule` and THEN `0x102b6fe0`,
returning either non-zero answer. Then, in strict order: `COND 0x0c SHOULD_DODGE` → `0xd5`;
`COND 0x0d SHOULD_BLOCK` → `0xd6` (both read through the INTERRUPT form `0x10269d30`, not the plain
`HasCondition`); `COND 0x0f SHOULD_KICK` and `COND 0x0e SHOULD_STEPBACK` — **both read before either
is tested** — give `0xdb` for kick alone, `0xdb` for kick + stepback with `RandomInt(0, 1) == 1`,
and `0xd3` otherwise, with stepback alone also `0xd3`; `COND 0x51 CAN_MELEE_ATTACK1` → `0xdc` or
`0xdd` by slot 308.

Then the height-difference retry stamp at `+0x6274`, ticked whatever the arms below decide: forced
to `-1.0` when `+0x626c` is at or below `_DAT_10451acc` (64.0), armed to
`curtime + RandomFloat(3.0, 4.0)` when it holds `-1.0`, and flagged expired once it is at or below
`curtime`. Under slot 308, together with `COND 9`, `COND 0x1a` or `COND 0x59` or the expired stamp,
slot 601 then `0xe9`; `COND 0x59` alone gives `0x17`; neither `COND 9` nor `COND 0x60` gives 199;
otherwise the enemy's `WorldSpaceCenter` is read and discarded and `0x102a11d0` gives `0xe0`/`0xe1`
by slot 308; then `0x102b7690(0, 1, 0, 1)` is offered; and the last pair is `0xd1`/`0xd2` when the
distance is **strictly below** the bare range and the stamp has not expired, else `0xca`/`0xcb`.

**Three float compares were decoded off the listing** because the decompiler renders them as
`(a < b) != (a == b)` and `(a < b) == (a == b)`: `2 * range <= distance`, `range + 200 < distance`,
and `distance < range`. The earlier walk had the first as "exceeds" and the last as "at or below".

**Unrecovered:** the melee-range ConVar's name and default; what `+0x626c` and `+0x6268` are
written by.

### `CNPC_VMingXiao::SelectScheduleMeleeCombat` — `0x10396050`

_Recovered 2026-09-14, story 29d._

1,522 bytes, the same skeleton as `0x10385e40` with four differences, and its own trace file
`"E:\Vampire\main\dlls\hl2_dll\NPC_VMingXiao.cpp"`.

1. The not-yet-engaged arm tests the distance against `range + 200` **first** and does not offer the
   melee failure gate `0x102b6fe0` at all.
2. The common tail offers ONLY `0x102b7370`.
3. It OPENS the ladder with an arm the human body lacks: `COND 0x48 ENEMY_OCCLUDED` → slot 601 then
   `0xe9` when slot 308 passes, `0xcd` otherwise.
4. There is NO `COND 0x0d SHOULD_BLOCK` arm at all; only `COND 0x0c SHOULD_DODGE` → `0xd5`.

It also re-fetches `GetEnemy()` at every use rather than caching it, which is observable only in the
dispatch count.

**Unrecovered:** as `0x10385e40`.

### `CNPC_VBach::SelectScheduleMeleeCombat` — `0x10364080`

_Recovered 2026-09-14, story 29d._

395 bytes, `CNPC_VBach#604` only, and a weapon-DISCIPLINE prologue in front of the human body: Bach
must be holding the right gun or the right sword for the condition he is in. Its trace file is
`"E:\Vampire\main\dlls\hl2_dll\NPC_VBach.cpp"`.

`COND 0x7b` stamps `+0x6690` with `curtime + _DAT_10463584` (15.0f) and returns `0x15a` (line
0x26e). Otherwise `GetActiveWeapon()` is fetched ONCE and `COND 0x7a` read immediately after it.
Unarmed: `0x7a` gives `0x158` (line 0x28c) and `0x79` gives `0x159` (line 0x290). Armed with `0x7a`
CLEAR and `0x79` set: the weapon's classname must be `"item_w_rem_m_700_bach"` or the body answers
`0x159` (line 0x280), and on a match it RETURNS slot 605 `SelectScheduleRangedCombat`'s answer —
the one arm that leaves the melee family. Armed with `0x7a` set: the classname must be
`"item_w_katana"` or `0x158` (line 0x279).

The fall-through calls `0x10385e40` DIRECTLY (not through the vtable), clears `+0x6444` unless
`m_NPCState` is 4 or `0xc` — on every path out, including the ones that answered non-zero — and
forces `0x159` (line 0x29d) when that call returned 0.

**Unrecovered:** what reads `+0x6690` and `+0x6444`; neither has a recovered reader.

### `CNPC_VZombie::vfunc509` — `0x103e0fa0`

_Recovered 2026-09-14, story 29d._

166 bytes, slot 509, `CNPC_VZombie#509` only. It REPLACES the Troika body `0x10294040` wholesale:
there is no `IsInDialog` refusal, no `m_NPCState` test and no `SF_NPC_GAG` test in it at all, which
is why a zombie vocalises in states where a human body would have refused. Every refusal returns
false through the same low-byte clear.

1. `m_bIsBCCTargetable` clear → false.
2. A LIVE `m_hDialogPartner` (`+0x0fe8`) → false.
3. `IsBusyWithDiscipline()` → false.
4. The odds default to `RandomInt(0, 999)`; when the current schedule (`+0x5c38`) is non-null and
   its `+0x1c` passed through slot 447 `GetLocalScheduleId` equals `0x12f`, the odds become
   `RandomInt(0, 20)` **and the float-sound check is SKIPPED**.
5. Otherwise slot 510 `ShouldPlayFloatSound` true plays slot 507 `FloatSound` and returns FALSE —
   the float sound is played INSTEAD of an idle sound, not beside it.
6. True only when the roll is exactly 0.

**Unrecovered:** nothing.

## Story 29d, family SpeciesMisc10 — the one-off species words: the grapple exit, the Bach shield, the Chang brothers, the croucher's burn, the ManBat cone, the Tzimisce pair, the boss health record and the Werewolf

_Recovered 2026-09-14, story 29d._

The per-species half of the family's forty-one rows. The dialogue and player-record bodies are in
[`social.md`](./social.md) and the two spawn-side bodies in [`lifecycle.md`](./lifecycle.md), under
the same family header.

**Five facts hold across the whole family and are stated once here.**

1. **`CNPC_VVampireBoss::GetCurrHealthPercent` (`0x103c6830`) RISES with damage.** It is
   `stat 0x0f (wounds) / stat 0x11 (cap)`, not a remaining-health fraction. So `0x103c6a20`'s
   `GetCurrHealthPercent() - m_HealthPercentRecord` answers **positive** for a body that has LOST
   health — the earlier walk says negative — and every threshold that reads it is a fraction of the
   bar lost since the mark.
2. **`+0x9c` is the `CBaseCombatCharacter` self-downcast cache and `+0xa8` the `CBasePlayer` one.**
   `victim->+0xa8 != 0` reads "is this the player", which is what makes the ManBat cone, the head
   claw's slot 332 and the croucher's burn player-only bodies.
3. **`_DAT_1044fab0` is a DOUBLE reading 0.0** (`103c1db6` is `FCOMP double ptr`), read at file
   offset `0x44fab0` of the pinned `vampire.dll`. It is the "no slow running" sentinel the ManBat
   and the head claw both compare their expiry against.
4. **`CBaseCombatCharacter::BeginSlowEntity`/`EndSlowEntity` take `500.0` at every one of their
   three call sites in this image** (`1038eb5e`, `103c1dca`, `103c2296`).
5. **`CNPC_VWerewolf`'s two hint words are the other way round from the earlier walk.** The datamap
   reads `+0x66b0 m_pTeleportHint`, `+0x66b4 m_pLastUsedTeleportHint`, `+0x66bc m_pMoveHint`,
   `+0x66c0 m_pLastUsedMoveHint`, `+0x66c4 m_pBreakHint`.

### `CAI_BaseNPC::LeaveGrappleState` `0x1026ce30`

_Recovered 2026-09-14, story 29d._

A distinct retail function beside the Troika override `0x102b5d90` that owns slot 380 —
`0x102b5d90` is only this call plus a tail `JMP [vtable + 0x998]` into slot 614. Four
**unconditional** steps, with no grapple-type gate anywhere in the body:

1. `1026ce30` — fire `m_OnGrappleEnd` (`+0x5bf0`) through `0x100cd660` with the entity the handle at
   `+0x1538` resolves to as activator, or null when `+0x153c` is `-1` or the handle word fails its
   `& 0x1fff` index / `>> 13` serial check.
2. `1026ce78` — `CBaseCombatCharacter::LeaveGrappleState`.
3. `1026ce82` — slot 416 (`vt+0x680`) `SetForceFrequentThink(false)`.
4. `1026ce8d` — `0x10007ea0`: `--m_iIsOblivious` (`+0x5bb4`), clamped at 0, then the squad reconnect
   `0x10009601`.

**Unrecovered:** nothing.

### `CNPC_VBach::GatherAttackConditions` `0x10363db0`

_Recovered 2026-09-14, story 29d._

Slot 561's species body, and a prologue: it ADDS the shield, teleport and weapon-switch block in
front of the base `0x1026dd10` and changes nothing the base gathers.

`10363dc6` gates the whole first block on `HasCondition(0x4c LIGHT_DAMAGE)` or
`HasCondition(0x4d HEAVY_DAMAGE)`. Inside, `m_bCondTookDamage` (`+0x5b80`) is cleared FIRST
(`10363de3`), then the distance argument at or above `DAT_1062d200[m_iBachTeleportState]` **or**
`m_flNextHolyLightTime` already past takes the SHIELD arm; anything else reads the cvar at
`DAT_10924a6c` and sets condition `0x7b` (teleport).

`DAT_1062d200` is **768.0, 768.0, 384.0, 384.0** — four floats read at file offset `0x62d200` of the
pinned image.

The shield arm fires only when `m_flNextShieldTime` (`+0x6688`) is past, and then walks the
`+0x13bc`/`+0x13c0` stat-list array for the entry whose `+0x10` is **3** (the scripted list), falling
back to the lazily constructed global `CVStatList_t` at `DAT_109f0b40`; calls
`CVStatList_t::SetBase(stat 0xd, 5)`; sets `m_flNextShieldTime = curtime + _DAT_1044eb0c` (**20.0**),
`m_bShieldActive` (`+0x66a5`) = 1 and `m_flShieldTime` (`+0x6684`) = `curtime + _DAT_1046bac0`
(**6.0**); and emits `"Character/Boss/Bach/bach_shield.wav"` (`0x1062ea6c` — **with** the `.wav`,
which the earlier walk dropped) through a `CPASAttenuationFilter` built from slot `+0x378` at volume
1.0, attenuation 0.8, pitch 100, channel 2. On EITHER branch `+0x66a6` is then cleared
(`10363f81`).

Independently of all of that (`10363f87`), when `m_flNextWeaponSwitchTime` (`+0x668c`) is past the
body reads the same cvar and sets condition `0x79` at or above `_DAT_104704d0` and `0x7a` below it.
`_DAT_104704d0` is **72.0**, read at file offset `0x4704d0`.

**Unrecovered:** what `+0x66a6` means; only its clear is in this body.

### `CNPC_VChangBros::CheckForTeleport` `0x1036cab0`, `::CheckForUnited` `0x1036cbd0` and `::SelectLedgeNode` `0x1036cfa0`

_Recovered 2026-09-14, story 29d._

**`CheckForTeleport`**, three arms in retail's order: `m_ChangType` (`+0x66b8`) equal to 1 answers
false outright; else the health delta `0x103c6a20` at or above `_DAT_104ad9f8` answers true; else,
only for `m_ChangType` 0 and only when `GetOtherBrother` returns an entity, `curtime - m_fFacingTime`
(`+0x66d0`) strictly greater than `GetFacingTimeToTeleport()` answers true. `_DAT_104ad9f8` is
**0.1**, read at file offset `0x4ad9f8` — and by fact 1 that is a tenth of the bar LOST since the
mark.

**`CheckForUnited`**: false at once with no other brother, and false while `curtime` has not passed
`m_fLastUnitedAttackTime` (`+0x66ec`) plus `_DAT_104ada48`; past both it is true UNLESS both
brothers' `GetCurrHealthPercent` are below `_DAT_104ada4c`. `_DAT_104ada48` is **30.0** s and
`_DAT_104ada4c` is **0.5**, both read out of the pinned image. By fact 1, "below 0.5" is the HEALTHY
half, so **two healthy brothers refuse the united attack** and one hurt brother is what allows it —
the opposite reading from the one the threshold suggests at a glance.

**`SelectLedgeNode`**: walks the global hint list from `DAT_10925450` through the `+0x5d8` next link,
keeps only nodes whose type word `+0x5dc` is `0x4653` and that `CheckJumpPathToHintNode(this, node)`
accepts, and scores each survivor by the full 3-D distance between the node's `GetAbsOrigin`
(`vt+0x364`) and its own. The incumbent is seeded `-3.4028235e+38` and REPLACED on a **strictly
greater** distance, so the Chang brothers pick the **farthest** reachable ledge — the opposite
comparison from `CNPC_VSheriffMan::SelectLedgeNode` (`0x103b0ab0`), and unlike the Sheriff's twin
there is no closest-player gate in front of it. Null when nothing qualifies.

**Unrecovered:** nothing.

### `CNPC_VGhoulCroucher::OnVictimHitByMe` `0x1037be80` and `::BurnPlayer` `0x1037c090`

_Recovered 2026-09-14, story 29d._

Slot 24's fourth species override. The victim's `+0xa8` player record is read first, and when
`m_bSpawnBurning` (`+0x6665`) is set and that pointer is live it calls `BurnPlayer(player, 10.0)` —
`1037bf3c` pushes `0x41200000`. It then ALWAYS runs the Troika line `0x1029f8d0`, the melee-move
record clear, unlike the Gargoyle and Sabbat-leader arms which drop it.

`BurnPlayer` itself: with a non-null target, iterate every hitbox of its model — the count read via
`CBaseAnimating::GetModelPtr` then `*(ptr + *(ptr + 0x104) + 4)` — calling
`CBaseCombatCharacter::BurnHitbox(target, index, 5.0, 0.5)` on each; then build a `CTakeDamageInfo`
through `thunk_FUN_101c26d0` with inflictor = `GetActiveWeapon()`, attacker = `this`, the CALLER's
damage amount, `bitsDamageType` **8**, and the trailing `0, 0, -1`; then `CBaseEntity::TakeDamage`.
The earlier walk read the `8` as the whole descriptor and lost the damage amount.

**Unrecovered:** nothing.

### `CNPC_VManBat::StartScreechCone` `0x1038e9c0`

_Recovered 2026-09-14, story 29d._

No vtable slot; one direct caller, `CNPC_VManBat::StartTask` (`0x1038c390`), once with the resolved
enemy and once with null. 1,283 bytes, in order:

1. `1038e9d2` — when the handle at `+0x66a4` still resolves, `UTIL_Remove` (`0x101cd940`) that
   emitter and null the handle.
2. `1038ea01` — spawn `Manbat_screechcone_emitter` through `0x100fbc90` at slot 217 `GetAbsOrigin`
   (`vt+0x364`), store it in `+0x66a4`, attach it to bone `"Bip01 Jaw"` (`vt+0x3cc` with mode 1) and
   start it (`vt+0x3c4`). This half runs even for a NULL target.
3. `1038ea33` — everything below needs a non-null target that carries a player record at `+0xa8`.
4. `1038ea60` — `UTIL_ScreenShake` (`0x101cdba0`) taking slot 220 `GetOrigin` (`vt+0x370`) **on
   `this`, not on the target** — with `2.5, 0.2, 3.0, 0.0, 0, 0`.
5. `1038ea69` — the target's `+0x9c` combat view; null ends the body.
6. `1038eb2c` — `0x10344f80(view, targetSlot217Origin - mySlot217Origin, 2, 0)`, the push.
7. `1038eb43` — only when the slow stamp `+0x6684` still equals `_DAT_1044fab0`,
   `BeginSlowEntity(view, 500.0)`.
8. `1038eb85` — restamp `+0x6684 = curtime + RandomFloat(15.0, 25.0)` unconditionally.
9. `1038eb9a` — cache the victim's handle into `+0x6680`.
10. `1038ebb0` — when `+0x669c` holds nothing, spawn `Manbat_player_emitter` at the target's origin,
    attach it at `"Bip01 Spine"` mode 1 and start it.
11. `1038ec4a` — the same shape for `Manbat_blast_player` at `+0x66a8`, at the SAME nesting level.
12. `1038ed3c` — when `0x1015d680(playerRecord, 0)` resolves an item and `+0x66a0` is stale, spawn
    `HUD_Manbat_emitter`, attach it to THAT ITEM with mode `0xe` and an EMPTY bone name, start it,
    and set the byte at `+0x4a1` of the spawned emitter to 1.
13. `1038edd2` — whenever the player record stands, OR bit 0 into its `+0x2454`.

### `CNPC_VManBat::ReleaseSlowedEntity` `0x1038f020`

_Recovered 2026-09-14, story 29d._

`RunAI` (`0x1038e990`) calls it with `force = 0` and `Event_Killed` (`0x1038e8c0`) with `force = 1`.
It runs only while `0x1038f290` says `m_flSlowedExpire` (`+0x6684`) is above `_DAT_1044fab0`, and
then only when the caller forces it or that expiry has reached `curtime`. It zeroes `+0x6684`; calls
`EndSlowEntity(victim's +0x9c, 500.0)` only while the handle resolves with a combat view;
`UTIL_Remove`s the three effect entities at `+0x669c`, `+0x66a8` and `+0x66a0` **in that order**,
setting each handle to `-1`; clears bit 0 of the victim's `+0xa8` `+0x2454` word; and finally sets
`m_hSlowedEntity` to `-1` **outside** that guard, so the handle is cleared even when the victim never
resolved.

**Unrecovered:** what `+0x2454` bit 0 and the spawned emitter's `+0x4a1` byte mean on the player.

### `CNPC_VSabbatLeader::CheckForJumpCondition` `0x103a9d90` and `::UpdateBloodSplash` `0x103aa960`

_Recovered 2026-09-14, story 29d._

**`CheckForJumpCondition`**, three arms in retail order: `0x103c67f0(this, DAT_104c3cc8)` — whose own
body is `curtime - m_flLastAttackTime (+0x5d9c) > argument`, STRICTLY — then the health delta
`0x103c6a20` at or above `_DAT_104c3cc4`, then `PlayerDamagedEnoughThisRound`. The threshold is
handed to `0x103c67f0` as an ARGUMENT rather than compared inline. `_DAT_104c3cc8` is **8.0** and
`_DAT_104c3cc4` is **0.0666667**, both read out of the pinned image at `0x4c3cc8` and `0x4c3cc4`.

**`UpdateBloodSplash`**: does nothing at all while `m_bDiving` (`+0x66d5`) is set — **not even the
level copy**, which is what freezes the edge. Otherwise, when the current water level (`+0x3e0`
`m_nWaterLevel`) is above 0 AND either the previous level `+0x66c4` was 0 or
`m_fLastSplashTime (+0x66c8) + _DAT_104c3cdc` has passed `curtime`, it spawns the
`"bloodsplash_emitter"` pool emitter through `SpawnBloodPoolEmitter` and, ONLY on the dry-to-wet edge
(previous level 0), additionally `"bloodbigsplash_emitter"`, then stamps `+0x66c8` with `curtime`.
The previous level is copied from the current one on every non-diving pass, so entering a dive
freezes it and leaving one re-fires the big splash. `_DAT_104c3cdc` is **0.25** s.

**Unrecovered:** nothing.

### `CNPC_VTzimisceHeadClaw::vfunc332` `0x103c1d80` and `::EndSlow` `0x103c2230`

_Recovered 2026-09-14, story 29d._

**Four offsets are corrected here**, read off the listing rather than the decompiler's struct:
`m_flSlowedExpire` is `+0x6678` (`103c1db0` `FLD float ptr [EBX + 0x6678]`), `m_hSlowedEntity`
`+0x6674` (`103c1e02`), the player emitter `+0x667c` (`103c1dfc` `LEA EDI,[EBX + 0x667c]`) and the
HUD emitter `+0x6680` (`103c1ff2`). The earlier walk swapped the first two and shifted the last two;
`0x103c2230` reads the same four, which is the cross-check.

Slot 332's Troika-line base (`0x1014f890`) is `return;`. The species body is guarded on a target
that carries a player record at `+0xa8` **and** a combat view at `+0x9c`, so it is a player-only
body. Then, in order:

1. `103c1db0` — `BeginSlowEntity(view, 500.0)` only while `m_flSlowedExpire` still equals the
   DOUBLE-`0.0` sentinel `_DAT_1044fab0`.
2. `103c1dd5` — `m_flSlowedExpire = RandomFloat(5.0, 8.0) + curtime`. The bounds are recovered from
   the pushes (`PUSH 0x41000000` then `PUSH 0x40a00000`), which the earlier walk left unstated.
3. `103c1e02` — `m_hSlowedEntity` = the victim's handle.
4. `103c1e08` — with the `+0x667c` handle dead, spawn `"Tzim2_player_emitter"` at the victim's slot
   217 origin, store the handle, attach at `"Bip01 Spine"` mode 1 (`103c1e6f`) and start it.
5. `103c1f13` / `103c1f5c` — a `CPASAttenuationFilter` on the victim's slot 222
   `GetSoundEmissionOrigin`, then `"Character/Monster/TC_FatGuy/Sluge_Hit.wav"` on channel **4** and
   `"Character/Monster/TC_FatGuy/Sluge_Affected.wav"` on channel **3**, both at volume 1.0,
   attenuation 0.8 and pitch 100. The earlier walk named neither string.
6. `103c1f87` — with the player's inventory slot 0 (`0x1015d680`) holding an entity and the `+0x6680`
   handle dead, spawn `"HUD_Tzim2_emitter"`, store the handle, attach at that item with mode `0xe`
   and an empty bone name, and start it.
7. `103c20ba` — the **2-D** distance (`sqrt(dx*dx + dy*dy)`, no Z) between the two slot 217 origins;
   at or above `_DAT_104cd108` it reads the cvar at `DAT_10924a6c` and `SetCondition(0x35)`.
   `_DAT_104cd108` is a **DOUBLE reading 240.0** — `103c20dc` is `FCOMP double ptr` — read at file
   offset `0x4cd108`.

`EndSlow` (`0x103c2230`) is the teardown. Gate `0x103c24a0`: act only while `m_flSlowedExpire` is
above 0.0, then only when the caller forces it or the timer has reached `curtime`. It zeroes
`+0x6678`; and ONLY while `m_hSlowedEntity` resolves with a `+0x9c` view, calls
`EndSlowEntity(view, 500.0)`, sets the handle to `0xffffffff` and emits `Sluge_Affected.wav` on
channel 3 from the VICTIM's slot 222 origin at attenuation 0.8 — so a stale handle leaves
`m_hSlowedEntity` standing. Then `UTIL_Remove`s the two owned effects at `+0x667c` and `+0x6680`,
each only when the handle's serial matches its slot, setting both to `0xffffffff`. `0x1065d43c` is
`Sluge_Affected.wav`, not the `"Sluge_Affect"` the earlier walk named, and it is the same wav slot
332's second emit uses.

**Unrecovered:** nothing.

### `CNPC_VTzimisceRunner::NotifyChangeSizeSmall` `0x103c3cd0` and `::NotifyChangeSizeNormal` `0x103c3d10`

_Recovered 2026-09-14, story 29d._

Slots 335 and 336, whose Troika-line bases (`0x103415f0`, `0x10341680`) are pure no-ops — a
scope-trace push and pop and nothing else — so these arms ADD the entire behaviour.

Slot 335, four writes in order: `SetHullSizeSmall(force = 1)` (`0x10273180`); the form byte `+0x6672`
= 1, which `PreTranslate_TzimisceRunner` (`0x103c3e10`) selects its four `TZ` activity variants on;
`m_bWantsLargeHull` (`+0x5f2c`) = 0; and `+0x6674` = the float the engine interface at
`DAT_1070b22c` returns from vtable `+0x1dc` called with 0.

Slot 336 is the inverse **and its guard**: re-call the same engine accessor and compare against the
value slot 335 cached at `+0x6674`. **Only when the two differ** does it `SetHullSizeNormal(force =
1)` (`0x10273070`), clear `+0x6672` and set `m_bWantsLargeHull` = 1. An unchanged token leaves the
runner small and the form byte set, so the restore is edge-triggered on that engine value and not on
a request — and slot 336 can refuse outright. It also takes an `int` argument the override ignores.

**Unrecovered:** what `DAT_1070b22c` vtable `+0x1dc` answers. Only the EQUALITY of two successive
reads is observable in these two bodies.

### `CNPC_VVampireBoss::WaitForTransformation` `0x103c63c0`, the health record pair and `::SpawnBodyEmitters` `0x103c6f40`

_Recovered 2026-09-14, story 29d._

**`WaitForTransformation`**, one gate then three writes: nothing until `curtime` passes
`m_flProteanTransformStartTime` (`+0x66b0`) plus `_DAT_104ce8bc` (**2.0** s, read at `0x4ce8bc`);
then it selects the type-0 (Attributes) `CVStatList_t` from `+0x13bc`/`+0x13c0` with the lazily built
global fallback and calls `CVStatList_t::Set(stat 0x0f, 0)` — **a full heal**, because `0x0f` is the
wound counter and not current health; resolves `m_hTransformPartner`, `RTDynamicCast`s it and fires
the partner's output at `+0x6664` with `this` as both activator and caller; then `TaskComplete(0)`
(`0x10273e80`, which returns untouched while `COND 0x5c TASK_FAILED` stands).

**The health record pair.** `0x103c6a00` is one write —
`m_HealthPercentRecord (+0x6698) = GetCurrHealthPercent(this)` — the boss line's snapshot taken
before a phase, with five direct callers across the boss species. `0x103c6a20` answers
`GetCurrHealthPercent(this) - m_HealthPercentRecord`, in that order and as a `float10`. By fact 1
that is **positive** for a body that has lost health.

**`SpawnBodyEmitters`**: `KillBodyEmitters` FIRST, then a fixed four-iteration loop calling
`SpawnBodyEmitter(i, name)` with the four attachment names from the pointer table at
`PTR_s_Bip01_L_Hand_1065e6d0`, storing each spawned entity's handle (its vtable `+0x4` accessor)
into `m_hParticleEmitters[i]` at `+0x66a0`. A spawn that answers null leaves that slot's PREVIOUS
handle untouched rather than clearing it, which is why the kill has to run first. The four names,
read out of the pinned image at `0x65e6d0`: `"Bip01 L Hand"`, `"Bip01 R Hand"`, `"Bip01 Spine"`,
`"Bip01 Spine"` — the last two are the SAME string.

**Unrecovered:** what the partner's `+0x6664` output is named.

### `CNPC_VWerewolf::TaskFail` `0x103ce750`, `::HasPath` `0x103d0db0`, `::DrawDebugStatOverlays` `0x103d5130` and `::SnapToAnimationPoint` `0x103d9f90`

_Recovered 2026-09-14, story 29d._

**`TaskFail`** (slot 448). The DIAGNOSTIC half runs only when the running schedule (`+0x5c38`) equals
one of `0x160`, `0x161` or `0x162` resolved through `0x102cc1f0`: `DevWarning` `"Werewolf failed task
'%32s' schedule..."` with the task name from slot `0x704` over `0x1028a150`'s current task
(`"INVALID TASK"` when there is none), the schedule name at `schedule+0x40` and the failure text from
`0x10316fa0`; then `"Current MOVE Hint: %s"` for `m_pMoveHint` (`+0x66bc`), `"Current TELEPORT Hint:
%s"` for `m_pTeleportHint` (`+0x66b0`) and `"Current BREAK Hint: %s"` — which is **gated on
`m_pBreakHint` but prints the TELEPORT hint**, a retail copy-paste bug to reproduce.

The rest is unconditional, in order: `SetHullSizeSmall(1)`, `ClearMoveHint`, `ClearTeleportHint`,
`m_pBreakHint` (`+0x66c4`) = 0, `+0x66a4` = 0, `+0x66a1` = 1, the Troika base `0x1029adb0`,
`CheckStuck(NULL)`, `+0x66e8` = 0, `+0x6708` = `-1`, `+0x670c` = `-1`.

**`HasPath`**: 120 of its 138 bytes are the scope-trace frame that supplies the name; the body is ONE
call, `0x102ee380(m_pNavigator (+0x5d34), &start, &end)`. **The two navigator-cache stamps the
earlier walk attributes to this body are inside `0x102ee380`** — it writes `nav+8` from the NPC's own
`+0x156c` hull and `nav+0xc` from the global frame word, repeats the same pair on the path object
`0x102ecc00`, and only then forwards both `Vector`s to `0x102fdcc0`. Twelve direct callers plus one
from outside the kernel; the `void` return is a decompile artifact of the forwarded call.

**`DrawDebugStatOverlays`** (slot 76), in retail's order:

1. `103d51a1` — `"Not Seen Time  : %3.1f"` with `max(curtime - +0x66ec, 0.0)`. The clamp against
   `_DAT_104454c4` (0.0) is folded away by the decompiler and is recovered from the listing.
2. `103d51d3` — `"Player Distance: %3.1f"` reading `+0x6264 m_flPlayerDist`, the CACHED distance and
   not a computed range.
3. `103d51ee` — the tail into `CNPC_VMingXiao`'s arm `0x10366290`, chained rather than replaced.
4. `103d51f3` — the zone word `+0x66e8`, bit by bit: `0x1 ZONE_NO_TALL_ANIMS`,
   `0x2 ZONE_PLAYER_ON_BREAKABLE`, `0x40 ZONE_PLAYER_INSIDE`, `0x80 ZONE_PLAYER_OUTSIDE`,
   `0x4 ZONE_PLAYER_ON_PLATFORM`, `0x800 ZONE_WOLF_INSIDE`, `0x1000 ZONE_WOLF_OUTSIDE`,
   `0x100 ZONE_WOLF_ON_PLATFORM`.
5. `103d52dd` — conditions `0x77 CAN_TELEPORT`, `0x78 CAN_SPECIAL_MOVE`, `0x79 ENEMY_REACHABLE`,
   `0x7b SHOULD_BREAKHINT`, `0x7a DEATH_TRIGGERED` — `0x7b` BEFORE `0x7a`.
6. `103d535f` — `m_DoorState` (`+0x6680`) printed `"door state: (%d)closed"` / `closing` / `open` /
   `opening` for 0..3. There is **no `default:` arm**: any other value prints nothing at all.
7. `103d539e` — `DrawDebugHintInfo` on a cvar-selected PLAYER hint, reached through
   `UTIL_PlayerByIndex(1)` then `0x10172710` and an RTTI cast to `CAI_Hint`.
8. `103d53ea` — then on the NPC's own hint: `m_pMoveHint` (`+0x66bc`) first, else `m_pTeleportHint`
   (`+0x66b0`), else `m_pLastUsedTeleportHint` (`+0x66b4`) or `m_pLastUsedMoveHint` (`+0x66c0`)
   selected by two more cvars.
9. `103d5450` — the LAST FIVE entries of the schedule-stack array `+0x668c` with count `+0x6698`
   (`start = max(count - 5, 0)`), printing `"INVALID SCHEDULE"` for a null row or one whose `+0x40`
   name is null.

**`SnapToAnimationPoint`**: six writes and two calls in order —
`CBaseAnimating::MatchOriginAnglesToAnimation("Bip01", 1, 1)`, which snaps origin AND angles onto the
animation's root bone; `SetHullSizeSmall(force = 0)` (`0x10273180`, which therefore does nothing when
`+0x5f2d` already says small); `+0x66dc`/`+0x66e0`/`+0x66e4` set from `DAT_1070d1b0/b4/b8`
(`vec3_origin`), clearing the cached fake-hull position; `+0x66a8` = 0; and both hint handles
`+0x6708` and `+0x670c` = `-1`, so the snap also drops whatever hint the Werewolf was heading for.

**Unrecovered:** the retail names of `+0x66a1`, `+0x66a4`, `+0x66a8`, `+0x6708` and `+0x670c` on
`CNPC_VWerewolf` — none is in the class's datamap and no corpus body declares them.

### The three species `Restore` bodies — `0x10360e10`, `0x103a6e80`, `0x103ae7f0`

_Recovered 2026-09-14, story 29d._

Three slot-127 species overrides that chain `CNPC_VVampireBoss::Restore` and then write constants.
All values below are read verbatim out of the pinned `vampire.dll`.

* `CNPC_VAsianVampire::restore` (`0x10360e10`): `m_fJumpGravity` (`+0x64b8`) = `_DAT_104a9300`, a
  float **2.0** at file offset `0x4a9300`.
* `CNPC_VSabbatLeader::vfunc127` (`0x103a6e80`): `m_pMonsterModelName` (`+0x6680`) =
  `"models/character/monster/Andrei/andrei.mdl"`, `m_pszMonsterClassname` (`+0x6694`) =
  `"npc_VSabbatLeader"`, and `SetBodyEmitterName(0, "Andrei_powerup_emitter")` and
  `(1, "Andrei_powerup_emitter")` — the same string twice.
* `CNPC_VSheriffMan::vfunc127` (`0x103ae7f0`): `m_pMonsterModelName` =
  `"models/character/monster/manbat/manbat.mdl"`, `m_pszMonsterClassname` = `"npc_VSheriffMan"`,
  `m_fJumpGravity` = `_DAT_104c6148` — a **float** (`103ae811` is `FLD dword`) reading **2.0** — and
  finally the GLOBAL `DAT_109340d8` = `0x15`.

That last write is not species data. `DAT_109340d8` is shared with four `NPCInit` bodies
(`CNPC_VGargoyle` `0x103785f0`, `CNPC_VHengeyokai` `0x1037fa70`, `CNPC_VManBat` `0x1038b070`,
`CNPC_VSheriffMan` `0x103ae6c0`) and read by seven functions outside the NPC kernel. It is the one
part of these rows a class -> slot -> value data table alone cannot carry.

**Unrecovered:** what `DAT_109340d8` selects; its seven readers are outside this kernel's closure.
