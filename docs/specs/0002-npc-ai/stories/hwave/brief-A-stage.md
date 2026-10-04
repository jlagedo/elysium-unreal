# Brief A — the stage (H1's stage half, H4)

Read `README.md` here first. Your files: `Source/ElysiumUE/Private/Debug/ElysiumArenaStage.cpp`
and `.h`.

## H1 — a row with no `model` key never activates the stage

Shown by `Arena/scenarios/world/rollcall_vcamera.json` and `rollcall_vcamerasecurity.json` (the two
camera rows carry no `model`; retail's cameras have none). The stage waits on model residency and
never reports ready, so the record errors.

- Find the residency wait and make a row without a model contribute nothing to it (as the map
  path treats a model-less baked row: read how `AElysiumMapActor`'s activation does it and do the
  same; do not invent a second rule).
- A stage that does fail must leave the next record a clean start: whatever `Load` / the teardown
  keeps after a failure (the failed flag, the half-built entity world, pending residency handles)
  is released so the next record's stage builds fresh. The runner's half (not giving up on the
  boot) is brief B's; say in your report what the runner must call.

## H4 — the player's state leaks from one record to the next

Shown in the full run: `verbs_stealth_kill`'s crouch stayed on for every later record of the boot
and turned `sense_beyond_vision` red. The seat (`SeatPlayerAt`, `ElysiumArenaStage.cpp:408`) resets
the mover, the feet, the yaw and the camera, and nothing else.

- At each record's seating, return the player to what a new game's player has: standing (the duck
  state and any held `+duck` / `+attack` / `+use` command released), no grapple, feed or
  stealth-kill in progress, the wielded item as the record's `armed` says (nothing when absent),
  health full, no light pin (`debug_stealth_light` released), no discipline active.
- Use the game's own doors for each (the functions a respawn or a new game calls); list the ones
  you used with their `file:line`. Where no door exists, do not write into the fields: report it.

## Self-tests to describe (the integrator adds them)

A two-record pair proving H4 (the first crouches and wields; the second probes `player_crouched`
false and `player_weapon` none — brief B's probes), and that a failed stage is followed by a
record that passes.
