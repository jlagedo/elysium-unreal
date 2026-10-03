# Brief D — callers onto the real searches; the NPC-side stand-ins deleted (0018/8, wave 2)

Read `CLAUDE.md` first. Wave 1 has landed and built: the searches and claim primitives in
`FElysiumNpcBase` (`Substrate/ElysiumNpcBaseHints.inl` — read its declarations; brief A's §2-3 in
`brief-A-store-searches.md` are the contract). You edit ONLY:
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers.cpp`, `ElysiumNpcTroikaHelpers.inl`,
  `ElysiumNpcTroikaHelpers2.cpp`
- `ElysiumNpcSquad.cpp`, `ElysiumNpcSquad.inl`
- `ElysiumNpcManBat.cpp`, `ElysiumNpcManBat.h`
- `ElysiumNpcChangBros.cpp` (comment at ~:392 only)
- `ElysiumNpc.cpp` (`ClearScheduleHint` only), `ElysiumNpcBaseLifecycle.cpp` (`UpdateOnRemove`'s
  hint arm only, ~:321-326), `ElysiumNpcStartTask.cpp` (~:1190-1215, the cower arm),
  `ElysiumNpcBaseStartTask.cpp` (comment ~:1243 only)
- `ElysiumNpcScheduleHost.h`, `ElysiumNpcScheduleHost.cpp`
- `Source/ElysiumUE/Public/ElysiumSaveTypes.h`
- stale comments: `ElysiumNpcHints.inl:~28`, `ElysiumNpcHints10.inl:~24`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelLifecycleTests.cpp`,
  `ElysiumNpcScheduleHostTests.cpp`, `ElysiumNpcKernelBaseSplitTests.cpp`
Do NOT build or run tests. Do not touch `ElysiumNpcBaseHints.*`, `ElysiumHint.*`, the world, the
validators. Report ≤300 words: every call site changed (file:line → what), every deletion, the
schema bump, anything unresolved.

## 1. The mask search's two callers (a divergence today)

`0x102d2980(npc, flags, mask, radius, origin*, outScore*)` takes a CLASS MASK, not a type. Both
port callers pass through `FindHintNear` (a TYPE search), which is a divergence. Retail
(`shape.md` l. 865-867, and the census confirms the reading): the first argument `8` is the FLAGS
byte (bit 3: score `sqrt(d²) × rating`), the second is the mask:
- `0x102b6b50` `FindShootAtHintNode` (`ElysiumNpcTroikaHelpers.cpp:~817`): flags `8`, mask
  `0x10` (= type 10400), radius the weapon's `+0x8c0` or 1024.0 → `FindHintByClassMask(8, 0x10,
  radius)`. Re-read the port's constants `TroikaShootAtHintType` / `TroikaShootAtHintSearchFlags`
  and rename / re-comment them to what they are. Confirm against `vtmb_code 0x102b6b50` and cite.
- `0x102b7110` `FindTacticalHintNode` (`ElysiumNpcTroikaHelpers2.cpp:~197-202`): flags `8`, mask =
  its own argument (the caller's `SearchType`), radius slot 550 → both calls (first with
  `bForceCoverLosCheck`, retry when entrenched) go to `FindHintByClassMask(8, SearchType & 0xff,
  CoverRadius())`. Confirm against `vtmb_code 0x102b7110` and cite.
- Slot 550 is `CoverRadius`, a constant per class (shape.md, the new subsection "The claim
  primitives, the hint LOS check and the idle gate"): `CAI_BaseNPC 0x101a6c20` answers 1024.0;
  `CNPC_VPedestrian 0x103a1de0` and `CNPC_VTzimisce 0x103b6e30` answer 4096.0. Rename the seam
  `IdealHintSearchRangeUnits` → `CoverRadius` (declaration in `ElysiumNpcTroikaHelpers.inl`; if
  the slot is generated in `ElysiumNpcBaseSlots.inl` / `signatures.tsv`, keep the generated name
  and say so), base body 1024.0 from the tunables table if the cell exists (grep
  `ElysiumNpcKernelTunables.h` for `1045d650` / `104563b0`; else add the two constants with their
  cell addresses), and the two overrides in `ElysiumNpcPedestrian.{h,cpp}` and
  `ElysiumNpcTzimisce.{h,cpp}` — those four files are ADDED to your set for this one slot.

## 1b. The kick-hint hide walk (`0x102d0910`), now live on the hint

Brief A added `FElysiumHint::NpcKicked(FElysiumEntityHandle Npc)`. `ElysiumNpcStartTask_2.cpp`
`TASK_KICK_HINT` (~:1285) and `TASK_KICK_HINT_AT` (~:1317) count it as `++TaskTailHintFires`
(`0x102a60df 0x102d0910(hint, this)`): replace both with the call on the hint entity
(`FElysiumHint::Cast(World->Entities()[BaseScheduleHost.HintNode].Get())`, null-safe), and delete
`TaskTailHintFires` (`ElysiumNpcStartTask_2.inl:~128`) plus any test reading it (grep). Those two
files are added to your set.

## 2. The other callers

- `ClaimHintNode` (`TroikaHelpers2.cpp:~82`): forward to `FElysiumNpcBase::ClaimHint`.
- `NthHintOfType` (`ElysiumNpcSquad.cpp:~47`): the Nth hint of `HintType` in LIST order
  (`World->HintList()`, head first, live hints only — is "disabled" tested? read the retail caller
  `ElysiumNpcChangBros.cpp:396/973/977`'s cited address with `vtmb_code` and port what it does);
  answer the entity.
- `ManBatFindMoveGoalHint` (`ElysiumNpcManBat.cpp:~254`): `FindHintNear(HintType, 0, RadiusUnits)`
  and answer the entity for the index (`World->Entities()[i].Get()`).
- Fix the stale comments listed above (they say "no hint list / no store").

## 3. Delete the NPC-side stand-ins

`FElysiumNpcScheduleHost::bOwnsHint` and `HintReusableAt` are not retail words; the hint's own
`m_hHintOwner` / `m_flNextUseTime` are, and they are live now.
- `ClearScheduleHint` (`ElysiumNpc.cpp:~1830`, `0x10295ab0`): `if (HintNode == INDEX_NONE) return;
  if (OwnsHint(HintNode)) ReleaseHintNode(HintNode, ReuseDelay);` then the existing clears. (Retail:
  "releases only when `0x102d1450` says this NPC owns the hint", `shape.md` l. 853-855.)
- `UpdateOnRemove` (`ElysiumNpcBaseLifecycle.cpp:~321`): retail releases WITHOUT the owner gate,
  delay 0.0 (`shape.md` l. 857): `ReleaseHintNode(HintNode, 0.0f)`; then `HintNode = INDEX_NONE`.
- The cower arm (`ElysiumNpcStartTask.cpp:~1208`): delete `bOwnsHint = true`; the claim already
  wrote the hint.
- Remove both fields, their `Ar <<` lines (`ScheduleHost.cpp:~100,103`) and any other reader
  (`grep -rn "bOwnsHint\|HintReusableAt" Source/` must be empty after).
- Schema: `FElysiumSaveVersion` (`Public/ElysiumSaveTypes.h:21-122`): append a new value `= 40`
  named for this change before `LatestPlusOne`, and move `MinSupported` to it (saves are
  disposable, the header's policy says so).
- The three test files assert the stand-ins; re-point them at the hint's words through
  `HintWords(Index, Words)` (`Words.HintOwner`, `Words.NextUseTime`) or delete an assertion that
  only existed for the stand-in, saying which.

Cite the retail address at every changed arm as the surrounding code does.
