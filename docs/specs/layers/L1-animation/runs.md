# Worker runs — L1-animation

Each run is one Codex worker: the slice briefs listed, in order, in one go (`briefs/`). Bundled from
consecutive slices of one subsystem up to ~1.5 KB of retail code or 8 functions. Estimated minutes =
25 fixed + 20 per KB of retail code.

| run | subsystem | briefs | functions | retail B | est. min | ready | after |
|---|---|---|---|---|---|---|---|
| L1-r001 | animation | `L1.animation.animating-spawn-state-1`, `L1.animation.model-animation-banks-1`, `L1.animation.model-animation-banks-2`, `L1.animation.model-animation-banks-3`, `L1.animation.model-animation-banks-4` | 7 | 1522 | 55 | yes | L0-r082 |
| L1-r002 | animation | `L1.animation.model-animation-banks-5`, `L1.animation.model-animation-banks-6`, `L1.animation.activity-sequence-choice-1`, `L1.animation.activity-sequence-choice-2`, `L1.animation.activity-sequence-choice-3` | 7 | 1298 | 50 | yes | L0-r082, L1-r001 |
| L1-r003 | animation | `L1.animation.transition-graph-1`, `L1.animation.sequence-clock-1` | 2 | 844 | 41 | yes | L0-r082, L1-r001, L1-r002 |
| L1-r004 | animation | `L1.animation.sequence-motion-1` | 3 | 814 | 41 | no | L0-r042, L0-r043, L0-r082, L1-r003 |
| L1-r005 | animation | `L1.animation.pose-parameters-1` | 4 | 661 | 38 | yes | L0-r082, L1-r001, L1-r002 |
| L1-r006 | animation | `L1.animation.studio-hitbox-traces-1` | 4 | 2594 | 76 | no | L0-r009, L0-r082, L1-r001, L1-r002 … |
| L1-r007 | animation | `L1.animation.studio-hitbox-traces-2` | 3 | 735 | 39 | no | L0-r009, L0-r082, L1-r001, L1-r002 … |
| L1-r008 | animation | `L1.animation.studio-hitbox-traces-3` | 4 | 1379 | 52 | no | L0-r009, L0-r082, L1-r001, L1-r002 … |
| L1-r009 | animation | `L1.animation.bone-attachments-1` | 6 | 1530 | 55 | no | L0-r008, L0-r020, L0-r082, L1-r001 … |
| L1-r010 | animation | `L1.animation.animation-event-dispatch-1`, `L1.animation.animation-event-dispatch-2`, `L1.animation.animation-event-dispatch-3`, `L1.animation.next-sequence-effects-1`, `L1.animation.next-sequence-effects-2`, `L1.animation.bodygroup-state-1`, `L1.animation.collision-ignore-1` | 6 | 1520 | 55 | yes | L0-r082, L1-r001, L1-r002, L1-r003 |
| L1-r011 | animation | `L1.animation.ideal-acceleration-1`, `L1.animation.extra-model-lifecycle-1` | 3 | 908 | 43 | yes | L0-r082, L1-r001, L1-r002, L1-r010 |
| L1-r012 | animation | `L1.animation.animating-restore-1`, `L1.animation.animating-restore-2`, `L1.animation.burn-model-1`, `L1.animation.burn-model-2`, `L1.animation.burn-model-3` | 4 | 1377 | 52 | no | L0-r008, L0-r013, L0-r019, L0-r082 … |
| L1-r013 | animation | `L1.animation.flex-blink-1`, `L1.animation.flex-blink-2`, `L1.animation.lockable-handle-clip-1` | 2 | 107 | 27 | yes | L0-r082, L1-r001, L1-r002 |
| L1-r014 | physics | `L1.physics.l1-animation-ragdoll-marker-query-1` | 1 | 130 | 28 | yes | L0-r082 |
| L1-r015 | parked | `L1.parked.l1-animation-overlay-mask-bake-1` | 0 | 0 | 25 | yes | L0-r082, L1-r001, L1-r002 |
