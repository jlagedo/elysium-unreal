# Lane B report

## Removed (Source/ElysiumUE/Private/Substrate)
- `SelectTrace` call sites: Select.cpp 115-ish sites, SelectSpecies.cpp ~140, BaseSelect.cpp 35 (all forms: `return SelectTrace(f,l,X);` became `return X;`, bare statements deleted, branches intact). The definition and declaration are gone.
- `SelectScheduleSelector` writes (Select.cpp, SelectSpecies.cpp, BaseSelect.cpp) and its declaration in BaseSelect.inl.
- `SelectIdealStateSelector`: declaration (BaseDamage.inl) and Cop.cpp write.
- Now-unused `GTroikaFile`, `GBaseSelectFile`, `GFile*` constants (Select files only; Combat10_2 and RunTaskSpecies keep their own).
- Select.cpp `DevMsg` print (patrol path has no schedule) deleted, rule kept.
- Cop: `DrawDebugGeometryOverlays` (0x10372f00) and `SquadSlotName` (0x10370ad0) overrides, label constants, Debug10 shared includes. Both are dead rows whose Port sites name Cop.cpp.
- Tests (KernelSelectTests.cpp): 13 selector-id assertions deleted; every schedule-chosen assertion kept.

## Re-homed
- `RetailFieldOfViewDot`: file-local function taking `const FElysiumNpc&` in BaseSelect.cpp (same name, same body; the member could not be redefined outside its class).
- `PlayerHeightenedAlert`, `PlayerCopsInPursuitCount`: `FElysiumNpcCop` members, verbatim bodies, declared in Cop.h.

## Needs another owner
- Stamp writes remaining: FrenzyShadow.cpp:324 (SelectScheduleSelector); PlayerController.cpp:330 (plus `GControllerPreSelectTraceId`:41, FrenzyShadow:48); SelectIdealStateSelector in AndreiBlood:165, AsianVampire:94, Bach:216, BaseState.cpp:92,110, ChangBros:155, Camera:385, Dog:51, Gargoyle:130, Guard1:92, Hengeyokai:251, HumanCombatant:113, Hunter:95, Pedestrian:150, SabbatLeader:216, SheriffMan:118, State.cpp:58, State_2.cpp:30,95, Tzimisce:367, Werewolf:286, Yukie:154, Zombie:213; comments at BaseState.inl:12, State.inl:24. These files are not in lane C's species list (BaseState/State/State_2/Camera/PlayerController): orchestrator should check.
- Tests: PlayerControllerTests.cpp:254-259,463-477,693-695; DamageTests.cpp:799-810; StateTests.cpp:214-217,298-301,521; Debug10Tests.cpp:1139-1149,1445-1452 call `Cop->DrawDebugGeometryOverlays()` (now Troika body) and 1468-1487 use the re-homed cop helpers (still compile).
- Generated `ElysiumNpcKernelShapeMap.cpp:50` names `SelectScheduleSelector`.
- Kernel override census test may list Cop's two removed overrides.

## Not refused / not deleted
- `CopPursuitPlayer` (overlay suffix reader) has no delete row; left.

`targets-B.tsv`: 2 rows.
