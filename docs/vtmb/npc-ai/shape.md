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
