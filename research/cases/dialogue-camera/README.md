# VtMB dialogue-camera research case

RE46 recovers the server-side dialogue opener contract and the client/server boundary around
conversation cameras. It is intentionally separate from `tutorial-event-resolution`: the tutorial
case owns trigger, queue, Python, teleport, and VCD ordering, while this case owns opener variants,
`default_camera`, `DialogPOV`, and any physical ownership established at dialogue acquisition.

## Pinned inputs

- `Vampire/dlls/vampire.dll`: size `7860281`, SHA-256
  `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`, MD5
  `1a10efcfe332036a9ee60bce223303db`, image base `0x10000000`.
- Patch-first exported entity corpus below `$ELYSIUM_EXPORT_ROOT`.
- External `vdata/camerashots/*.txt` definitions below the user's export root.

Generated decompilation, inventories, reports, and captures remain below
`$ELYSIUM_WORK_ROOT/research` and are not committed.

## Questions

- Which gates and argument fields distinguish `StartPlayerDialog`, `StartPlayerDialogRemote`, and
  `StartPlayerDialogUnforced`?
- Which downstream function acquires the player/NPC pair, and does it write either transform,
  velocity, facing, view mode, or command state?
- Where is `default_camera` read, normalized, acquired, replaced, and released?
- Does `CreateControllerNPC` transfer control or only create a scene-owned body, and when is the
  real player surface hidden?
- Which owner consumes `DialogPOV`, and does that path change gaze only or actor facing too?
- Which behavior lies in `client.dll`/`engine.dll` and therefore requires a controlled capture?

## Confirmed static findings

The three server datamap inputs are distinct handlers.

- `InputStartPlayerDialog` (`0x1029ef80`) applies the common player/partner and NPC-state guards,
  reads an integer argument into NPC offset `+0x5bac`, sets the forced byte at `+0x6495`, and
  schedules activity `0x6d`.
- `InputStartPlayerDialogRemote` (`0x1029f060`) applies the common guards, never reads the input
  parameter, sets the forced byte, and schedules distinct activity `0x6e`.
- `InputStartPlayerDialogUnforced` (`0x1029f120`) adds a player-side refusal predicate, reads the
  integer field, clears the forced byte, and schedules activity `0x6d`.

Consequently the authored `Jack.StartPlayerDialogRemote 256` value has no bit semantics in the
pinned server handler. The purpose of the integer stored by the other two forms remains open. None
of the three handler bodies writes player/NPC origin, angles, velocity, view state, or input state.
They schedule dialogue work; they do not acquire the camera synchronously.

The current patch-first corpus contains 251 `default_camera` rows across 15 exported maps, 11
literal forms, and references 66 external shot files. The forms include bare names and
`vdata/CameraShots/...txt` paths with mixed case. Normalization is basename-without-extension,
separator-insensitive, and case-insensitive. Jack's tutorial definition selects `Jack`; its shot
uses a `DialogTarget` follow origin, head target, FOV 40, `DialogPOV 1`, and
`SyncRotateOnMove 1`.

## Reproduction

```powershell
uv run elysium research dialogue-camera --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_dialogue_camera_<task>" --kinds funcs,xrefs,fields,vtables,datamaps,consts,grep
uv run elysium research ent_survey --patch
```

## Hash-gated capture recipe

Static server evidence does not settle client camera weights, real/controller surface visibility,
or engine-side view publication. A capture is admissible only when the mapped server DLL matches the
hash above and the capture records raw values rather than inferred labels.

Run two starts of the authored `sp_tutorial_1` porch `Remote 256` beat and comparison starts using
the ordinary and unforced handlers in a disposable test map. At opener delivery, acquisition, every
view update, close, fade begin, each +1-second teleport, and the +4-second second acquisition record:

- real player, controller entity, and Jack origin/angles/velocity plus render visibility;
- view origin/angles/FOV, first/third-person weight, scripted-camera weight, active shot identity;
- accepted/suppressed move and look commands;
- dialogue owner, partner, line, `DialogPOV`, and release lifetime.

The comparison must vary only the opener form and argument. Any placement/facing claim remains open
until the capture shows the write boundary and order; no Unreal implementation is inferred from a
final screenshot.

## Consumers

- opener and body behavior: `docs/vtmb/game_runtime.md`
- shot behavior: `docs/vtmb/camera-view-modes.md`
- tutorial join: `docs/vtmb/sp_tutorial_1-event-surface.md`
- project director: `docs/architecture/camera-architecture.md`
- status: `docs/project/roadmap.md` RE46 and 11.13f
