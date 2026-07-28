# Level transitions and player spawn

Where the player appears when a map loads. Three separate mechanisms decide this;
they are not interchangeable, and for `sp_tutorial_1` they disagree between retail
and the Unofficial Patch.

## The three spawn paths

**1. `info_player_start` (bare map load).** `Vampire/dlls/vampire.dll` registers
the `info_player_start` classname; on a plain `map <name>` console load the game
DLL spawns the player at that entity's `origin`/`angles`. This is the path the
pipeline and runtime reproduce — `UE_bsp_to_scene.py` writes the map's
`info_player_start` to the `<name>.spawn` sidecar and `AElysiumMapActor::ReadSpawn`
uses it.

**2. Landmark transition (`trigger_changelevel` → `info_landmark`).** When one map
sends you to another through a `trigger_changelevel` (`map` + `landmark` keys), the
engine places you **relative to the landmark**, *not* at `info_player_start`:

```
new_origin = dest.info_landmark[name].origin + (player_origin − src.info_landmark[name].origin)
```

`info_player_start` is bypassed entirely on this path. Both the source and
destination maps must carry an `info_landmark` with the same `targetname`.

**3. Scripted `ChangeMap` (the story path).** Level scripts call
`__main__.ChangeMap(delay, "<landmark>", "<trigger>")` (embedded Python), which
fires a named `trigger_changelevel` — so this reduces to path 2.

## How the game actually reaches the tutorial

New Game does **not** load `sp_tutorial_1` directly. `client.dll`'s menu issues the
literal command `map sp_genesisdevice_1` (character generation is a real map).
The chain, all via landmarks:

```
New Game
  → map sp_genesisdevice_1            (chargen; levelscript "demo")
  → wizard close: v_unpause + teleport_player firetrans
  → firetrans.OnStartTouch → boogieout,ChangeNow
  → trigger_changelevel "boogieout"   → sp_theatre        landmark newgame
  → walk_out_cam_k final keyframe:
      tutorial_change,ScriptUnhide + controls,Deactivate + fade_to_tutorial,Fade
  → sp_theatre trigger_changelevel "tutorial_change"
                                       → sp_tutorial_1     landmark tutorial
```

**Genesis exit.** `game_runtime.md` owns the wizard and character-state behavior. For travel,
both close paths issue `v_unpause` then `teleport_player firetrans`; that volume fires
`boogieout.ChangeNow`, entering `sp_theatre @ newgame`. The elevated spawn cannot reach the exit
volume by walking, so the teleport is the only authored route into the transition.

## Intro-skip divergence

VtMB exposes the wizard footer's `Skip Intro` state as the client-side `vchar_skip_intro` ConVar,
but the binary reader that applies it is not yet recovered. The separate `vskip_intro`
`vampire.dll` ConCommand (“Skips the Intro Scene”) is not that reader; the Unofficial Patch calls it
only for its clans 9–11 from `unhidePlus()`.

**Divergence — owner call.** New Game always plays genesis. With `elysium.SkipIntro 1`, the one
travel funnel rewrites only the authored `sp_theatre @ newgame` request to
`sp_tutorial_1 @ tutorial`. It drops the carried offset and yaw because genesis's `newgame`
displacement has no meaning relative to the tutorial's different landmark, making the rewrite the
same direct-entry placement used by an explicit landmark load. `elysium.SkipIntro 0` preserves the
authored theatre request with no fallback. `sp_theatre` is exported but not baked; the theatre act
remains a later playable-path step.

The theatre→tutorial handoff is driven **entirely by entities**. `theatre.py`'s
`tutorialLoad()` (`ChangeMap(2.5, "tutorial", "tutorial_change")`) is a parallel/legacy
path with **no caller** — no theatre entity output and no `.dlg` row invokes it
(`game_runtime.md` §4).

So the player enters the tutorial through the **`tutorial` landmark**, and where
they land is `info_landmark "tutorial"`, not `info_player_start`. On this map both
sit at the same spot (the patch's `info_player_start` is 8 units above its
`info_landmark "tutorial"`, same x/y), so the two paths coincide here — but that is
a property of the data, not a rule.

## Retail vs. patch (`sp_tutorial_1`)

The patch moved the tutorial entrance ~7,900 units in +Y and built new geometry
there. This is the "Fixed tutorial location continuity in downtown" change in
`VTMBup-readme.txt`: retail cut straight to a warehouse interior; the patch has you
walk out the theatre door and meet Jack on the front steps.

| entity                       | retail                | patch                   |
|------------------------------|-----------------------|-------------------------|
| `info_player_start`          | `-64 -376 0`, yaw 90  | `-14 7493 -164`, yaw 270|
| `info_landmark "tutorial"`   | `-64 -376 0`, yaw 90  | `-14 7492 -156`, yaw 270|
| `info_landmark "newgame"`    | `-47 -209 -31`        | `-23 7324 -194`         |
| `npc_VVampire "Jack"`        | `-221 -258 -40`       | `144 7352 -199`         |

Retail's start is inside the warehouse (surrounding faces `METAL/GALVANIZEDA`,
`METAL/CHAINLINKB`, `METAL/LOFTELEV`); the patch's start is the theatre porch
(`BRICK/THEATWLLA`, `BUILDING/THEATER_ARCH`, `CONCRETE/STAIRFRONT`,
`ASPHALT/ASPHALTA`). Retail has no world geometry above y≈2000; the patch has
~830 vertices up in the 7000 band. `point_teleport "teleport_very_beginning"`,
`trigger_multiple "trig_off_porch"`, the bus-stop sign prop and the `newgame`
landmark all moved with it.

So spawning "in a different place" than the old retail export is the patched
game's actual opening — not an export bug.

## Exporter behaviour

`bsp_to_scene.py` writes the **first** `info_player_start` it encounters (an
unordered ENTITIES scan, then `break`). Safe on every hub shipped so far (each has
exactly one), but on a hypothetical multi-`info_player_start` map whichever the
compiler emitted first wins, not a deliberate choice.

The runtime reproduces the landmark path (paths 2/3): `UElysiumMapSubsystem::RequestLandmarkTravel`
captures the player's source-landmark offset and view yaw, defers the travel to the next tick, and
`AElysiumMapActor::ResolveLandmarkSpawn` seats the player at `dest_landmark + offset` on the far
side (roadmap 4.6). A bare `map` command still bypasses this and spawns at `info_player_start`.
