# Brief D2 — V3d: `NPCThink` runs retail's phases and nothing else; the order, the save, the dialogue claim

Rewritten 2026-10-04 from `packets.md` (R1, R2) and the tree with V3c integrated (V3c's commit lands
before you; line numbers are that tree's — **re-locate each by Grep on the symbol before editing**).
Read `README.md` here (the "Corrections" block first — it overrides §1; then §2 M5, M12, M14–M16,
§4 V3d, §7 K1, §8 Q5/Q8), `packets.md` R1 item 3 and R2 items 1–3, `brief-D1-dialogue.md` (what moves
to D1), and `docs/vtmb/npc-ai/authored-control.md` § the executor `0x101a98c0` (lines ~296-312).
Runs beside D1 and D3. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`, `ElysiumNpc.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp` (`OnScheduleChange`'s order pre-step,
  `130-136`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMaintain.cpp` (`MaintainSchedule`, `50-61`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (`HasLiveDialogPartner`, `81-88`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunAi.inl` (the `:15` comment only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSpawn.cpp` (the `Event_Killed` tail's call, `80-87`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumScriptedScheduleOrder.h`
- `Source/ElysiumUE/Public/ElysiumEntity.h` (`451-471`: the cancel's comment and the dialogue-body
  virtuals, only)
- `Source/ElysiumUE/Public/ElysiumSaveTypes.h` (one schema step)

D1 owns every dialogue body, `Public/ElysiumEntityWorld.h`, `Public/ElysiumPlayer.h`, the payphone and
the use focus; D3 owns the mind, `Public/ElysiumNpcMindTypes.h`, the debug views and the arbiter's
tests. No test file is yours.

## What is left of the arbiter (counted by Grep, 2026-10-04, V3c in the tree)

V3a–V3c removed every claim outside the dialogue and the order. **51 production sites in 10 files**
(declarations excluded): 8 claims, 14 releases, 17 gates and token reads, 12 debug reads. Yours are
every `ElysiumNpc.cpp` site plus `ElysiumNpcMaintain.cpp:132-135` and `ElysiumNpcSpawn.cpp:86`; D1
takes `ElysiumNpcDialogue.cpp:24, 148-150, 379` and `ElysiumEntityWorldDialogue.cpp:258-292, 315, 1133,
1332, 1378`; D3 takes the debug views.

| kind | your sites (`ElysiumNpc.cpp` unless named) |
|---|---|
| claims | `1268` (`Mind.Acquire` in `AcquireProgramBody`), `1290` (`AcquireScriptedScheduleBody`), `1382` (the `ScriptedSchedule` claim in `BeginScriptedSchedule`), `1914` (`Mind.Acquire(Dialogue)` in `BeginDialogueBodySession`), `1955` (`PrepareBodyForDialogue`) |
| releases | `875` (`EndScriptedSchedule` in `ThinkStanceOrIdle`), `1283` (`Mind.Release` in `ReleaseProgramBody`), `1295`, `1444`, `1913` (`EndScriptedSchedule("dialogue opened")`), `1928` (`Mind.Release` in `EndDialogueBodySession`), `2138`, `2144`, `2147` (`Mind.Invalidate`), `2169` (`ReleaseAllBodyOwnership` from `OnDormancyChanged`); `ElysiumNpcMaintain.cpp:135`; `ElysiumNpcSpawn.cpp:86`; token resets `1387`, `1437`, `2149-2151`, `2357-2358` |
| gates | `842-850` (the `UpdateIdealState` gate), `869-880` (the program-end coupling in `ThinkStanceOrIdle`), `1257`, `1278` (the wrappers' generation tests), `1431-1432`, `1874`, `1925-1926`, `2130`, `2341-2352` (`RestoreMindState`'s owner byte), `2452-2463` (`SerializeMindBlock`'s owner byte), `2524-2534` (`SaveBlockReason`); `ElysiumNpcMaintain.cpp:132` |
| debug | `1384-1386` (the refused-claim warning), `2584-2585` (`GetDebugState` "Body owner") |
| vocabulary | `ElysiumNpc.h` `340`, `345-347`, `573-577`, `596-616`, `716`, `1063`, `1067-1080`, `1116-1117`; `Public/ElysiumEntity.h:464-471`; `ElysiumScriptedScheduleOrder.h:51-56` (comment) |
| routing (M5) | `RouteScheduleMaintenance` `751-775`, `ThinkInDialog` `777-797`, `ThinkStanceOrIdle` `857-885`; `ElysiumNpc.h` `366-369`, `400-401`, `1039-1042`; caller `ElysiumNpcBaseMaintain.cpp:56-59` |

Not the arbiter, and staying: the admission barrier (`RunAdmissionBarrier`, `FElysiumNpcMind::Admit`,
the deferred order `1311-1324` and `ReplayDeferredScriptedOrder` `714-726` — V6's, README Q5);
`UpdateIdealState` `810-855` and its tests (V9's, Q8 — you remove only `842-850`); K1
(`FElysiumScriptedSequence::SaveBlockReason`, a cine refuses a save while it possesses an NPC);
the world's own refusal (`ElysiumEntityWorldDialogue.cpp:1152-1157`, Q4).

## The job

1. **The routing goes** (M5). `FElysiumNpcBase::MaintainSchedule` (`ElysiumNpcBaseMaintain.cpp:50-61`)
   calls `MaintainScheduleRetail` for every body — `MaintainSchedule 0x102817c0` as `RunAI 0x1026f302`
   reaches it; the `AsNpc()` branch and its comment go. Delete `RouteScheduleMaintenance`,
   `ThinkInDialog` and, once item 4 removes its order coupling, `ThinkStanceOrIdle` (it is then
   `MaintainScheduleRetail` alone and has no other caller), with their declarations and the
   STORY8-TWIN markers (`764-766`, `ElysiumNpc.h:366-369`). `ThinkInDialog`'s read of
   `World->HasActiveDialogueBodyClip` (`787`) is that query's only production reader; the world query
   itself stays (D1's file; tests read it). See "Open" for the per-line clip it protected.
2. **The dialogue claim goes** (M12). Delete `BeginDialogueBodySession` (`1862-1921`),
   `EndDialogueBodySession` (`1923-1948`), `PrepareBodyForDialogue` (`1950-1956`), their declarations
   and `GetDialogueBodyOwner` (`ElysiumNpc.h:596-610`), `DialogueBodyOwner` (`:1117`), the
   caller-less `BeginDialog` (`:612-616`), and the base virtuals (`Public/ElysiumEntity.h:464-471`).
   What those bodies did is **re-homed by retail's own writers, not carried here**:
   - slot 614 `ResetThinkTimers` (`1883`) → D1's three inputs and `+use` (R2 items 1, 5);
   - the cine cancel (`1884-1909`) → retail cancels a live cine in **both** `StartTalking 0x102c0270`
     (R1 item 3; D1's new door) and `ForceScheduleChange 0x102ae490` (R2 item 3; already ported,
     `ElysiumNpcMaintain.cpp:96-112`, reached by every `0x102ae750(id, 0)` install). Neither is yours;
   - `EndScriptedSchedule("dialogue opened")` (`1913`) → nothing: the order has no end, the forced
     install replaces the program;
   - the place release (`1954`) → `ForceScheduleChange` → `OnScheduleChange 0x102a0940`
     (`ElysiumNpcMaintain.cpp:164-171`);
   - `bInDialog = true` (`1919`) → D1's `StartTalking`; the silent arm (`1932-1944`: `bInDialog`,
     `bForceDialogStart`, `RestoreDialogHolster`) → D1's close (`EndDialogSession` /
     `EndPlayerDialogTail` = `0x10178400`).
   Keep `FElysiumEntity::CancelScriptedSequenceForDialogue` (`ElysiumEntity.h:459`) and its signature —
   `ForceScheduleChange` (`ElysiumNpcMaintain.cpp:103`) and D1's `StartTalking` call it; rewrite its
   comment (`455-458`) to name those two callers.
3. **`HasLiveDialogPartner`** (`ElysiumNpcAnim.cpp:81-88`) resolves `GetDialogPartner()` (`+0xfe8`,
   D1 fills `SetDialogPartner 0x10107050`) to a live entity — not `bInDialog || IsTalking`. Name and
   signature unchanged. Its readers then see retail's word unchanged: RunAI's gather skip
   (`ElysiumNpcBaseRunAi.cpp:25`, `0x1026f1f0`), `ElysiumNpcSelect.cpp:399, 518`, `ElysiumNpcClosure.cpp:256`,
   `ElysiumNpcTroikaHelpers.cpp:974`, `ElysiumNpcTroikaHelpers2.cpp:365` (D1's `OnDialogRelease`),
   `ElysiumNpcEntityChain2.cpp:661`, `ElysiumNpcAnim.cpp:210`. Rewrite the `ElysiumNpcBaseRunAi.inl:15`
   comment.
4. **The pushed order** (M14). `BeginScriptedSchedule` (`1300-1425`) is the executor `0x101a98c0`'s
   NPC half. The goal resolve and its early return are already the director's
   (`ElysiumAiScriptedSchedule.cpp:100-109`, retail's order) — do not add a second one. Then:
   `forcestate` through **`SetState 0x1026e340`** (`FElysiumNpcBase::SetState(int32)`,
   `ElysiumNpcBaseState.cpp:63`, which traces and fires slot 463 itself), not `Mind.RequestState`
   (`1333-1338`); map the typed state with an existing typed→retail helper (e.g. `CondRetailStateId`,
   `ElysiumNpcConditionsBodiesShared.h:30`) — no new map. Then the mode's call. Delete the
   `FinishAmbientUse` + `ClearSchedule` before the push (`1372-1378`; retail does not clear,
   `schedule-kernel.md:663`), the claim and its warning (`1380-1389`), `EndScriptedSchedule`
   (`1427-1445`) and its callers (`875`, `1913`, `2144`), the wrappers (`1254-1296`;
   `ElysiumNpc.h:573-577, 1067-1073`), `ScriptedScheduleOwner` (`:1116`), the `UpdateIdealState` gate
   (`842-850`) and `OnScheduleChange`'s pre-step (`ElysiumNpcMaintain.cpp:130-136`).
   **Keep `BeginScriptedSchedule`'s signature**: 43 test call sites in 9 test files no lane owns
   use it as `(FElysiumScriptedScheduleOrder(), true, State)`. `FElysiumScriptedScheduleOrder` keeps
   `RetailOrderId` (`+0x65cc m_eForcedState`, `0x102ae840`'s word; read by
   `ElysiumNpcKernelCombat10Tests.cpp:719`, `ElysiumNpcKernelScheduleTests.cpp:742-745`), the deferral
   fields and `Route` (the director writes it); drop only a field with no reader left; rewrite the
   header comment (`47-56`, it cites the NPC's `SaveBlockReason`). The "Scripted schedule" debug row
   (`2588-2594`) follows the fields.
5. **The owner, as this file sees it** (M15/M16).
   - `ReleaseAllBodyOwnership` (`2128-2152`; callers `2169`, `ElysiumNpcSpawn.cpp:86`) keeps what
     retail's death / dormancy path does: `0x102b53d0(this, 0)` (`FinishAmbientUse(false)`, `2143`),
     the open conversation's close when this NPC owns it — keyed on `World->GetOpenDialogOwner() ==
     Handle` (`2132-2135`), not on a token; whether retail runs `CDialog::Release` on the partner's
     death is unrecovered (D1's open item): name it at the line — and `Mind.Invalidate` (`2147`).
     Its token lines (`2130`, `2136-2139`, `2144`, `2149-2151`) go. Rename it if its name lies, and
     the `ElysiumNpcSpawn.cpp:80-87` call and comment with it.
   - `SaveBlockReason` (`2524-2534`, `ElysiumNpc.h:716`) deleted; the base virtual
     (`ElysiumEntity.h:368`) stays for the other entities.
   - `SerializeMindBlock` (`2447-2465`) writes the state byte only; `RestoreMindState` (`2336-2359`)
     restores the state only through `Mind.Restore(State)`; `RestoredMindOwner` (`ElysiumNpc.h:1063`)
     goes. One schema step in `Public/ElysiumSaveTypes.h`: `NpcMindOwnerRetired = 42` after `:108`,
     `MinSupported` (`:137`) moves to it, its comment line beside the others (saves are disposable).
   - `GetDebugState`: the "Body owner" row (`2584-2585`) becomes `"Cine"` (the resolved `ScriptOwner`,
     `m_hCine +0x5d74`) and `"Dialog partner"` (`GetDialogPartner()` resolved); `"Script state"`
     (`2605`) stays. The "In dialog" row (`2543-2547`) keeps its key and its `YES …` / `no` prefix
     (`ElysiumDialogueCameraTests.cpp:189` reads it) and drops `DialogFlags` / `DecodedDialogFlags`
     (D1 deletes both).

## Shared names

- **With D1.** D1 deletes `FElysiumNpcDialogue::DialogFlags` / `DecodedDialogFlags` and every call of
  the bodies you delete (`ElysiumNpcDialogue.cpp:24, 148-150, 379`; `ElysiumEntityWorldDialogue.cpp:258-292,
  315, 1332, 1378`; `ElysiumNpcThinkCadenceTests.cpp:397-404`). Unchanged signatures both lanes rely on:
  `FElysiumNpc::SetSchedule(int32, bool)`, `ResetThinkTimers`, `HasLiveDialogPartner()`,
  `CancelScriptedSequenceForDialogue`, `FElysiumEntityWorld::CloseDialog(bool)`,
  `GetOpenDialogOwner()`, `FElysiumCombatCharacter::GetDialogPartner()`.
- **With D3.** `FElysiumNpcMind::Restore(EElysiumNpcState SavedState)` (one argument);
  `Invalidate(const TCHAR* Reason, bool bDead)` unchanged, no owner inside; `IsSupportedState` stays;
  `RequestState`, `Owner()`, `SuspendedOwner()`, `Generation()`, `CurrentToken()`, `CanAcquire`,
  `Acquire`, `Release`, `ForgetSuspended`, `IsResumableOwner` deleted. Your debug rows `"Cine"`,
  `"Dialog partner"`, `"Script state"` are what D3's `ElysiumEntityDebugSubsystem.cpp:219` reads.

## Open — name, report (the packets do not settle these)

- **The per-line VCD body clip.** `ThinkInDialog` held the body while the line's gesture event was live
  (`ElysiumNpc.cpp:783-790`, the STORY8-TWIN note: "no retail schedule stand yet"). Once it goes, a
  talking NPC runs its program (`0x6a/0x6d/0x6e` → `TASK_RUN_DIALOG`, slot 310 with `0x102c1400`'s
  answer). Whether the port's line clip survives the base's sequence write is unread;
  `Elysium.Substrate.Dialogue.BodyScene` (`ElysiumDialogueTests.cpp:1774-1775`) pins the old hold.
  Add no gate; report what you see in the code.
- `Mind.Invalidate`'s dormancy write (state Idle) has no retail writer named in `docs/vtmb/`; keep it,
  name it at the line.

## Rules

README § "Rules for every agent of V3". Retail first: cite the address at every line you port or
delete. The query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops,
never retried as-is or widened; never read a file over ~200 KB whole (read `ElysiumNpc.cpp` by ranges).
Look an address up with `uv run elysium research where|section <addr>` before searching `docs/`. Text
through Grep / Read / Glob, never shell `grep`/`cat`/`sed`. Wait on a background command by its
completion notification, never a sleep or a polling loop. Touch only these files; a line another file
needs goes in the report, exact. Do not build, do not run the arena or a suite, do not edit a record,
do not commit.

## Report (≤300 words)

Per change, the retail address; the executor's order as it now reads; what `ReleaseAllBodyOwnership`
became (and its name); every symbol another lane must change for the build (exact file:line); each
open item and how it stands at its line.
