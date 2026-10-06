| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [Initialize animating ragdoll slots](stories/L1.animation.animating-spawn-state.md) | animation | S | 1 | 1 | — | yes |
| 2 | [Rebind model animation data / Resolve sequence labels and activity names](stories/L1.animation.model-animation-banks.md) | animation | M | 7 | 2 | — | yes |
| 3 | [Choose activity sequences with retail draws](stories/L1.animation.activity-sequence-choice.md) | animation | L | 6 | 1 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 4 | [Find transition sequences](stories/L1.animation.transition-graph.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 5 | [Advance the retail sequence clock](stories/L1.animation.sequence-clock.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 6 | [Derive movement from sequence motion](stories/L1.animation.sequence-motion.md) | animation | M | 3 | 1 | Advance the retail sequence clock | waits for 1 lower-layer rows |
| 7 | [Dispatch sequence events in retail order](stories/L1.animation.animation-event-dispatch.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Advanc | yes |
| 8 | [Carry deferred sequence effects and transforms](stories/L1.animation.next-sequence-effects.md) | animation | M | 2 | 1 | — | yes |
| 9 | [Pack and resolve bodygroups](stories/L1.animation.bodygroup-state.md) | animation | M | 2 | 1 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 10 | [Delegate animating collision ignore](stories/L1.animation.collision-ignore.md) | animation | S | 1 | 1 | — | yes |
| 11 | [Return ideal speed plus acceleration allowance](stories/L1.animation.ideal-acceleration.md) | animation | S | 1 | 1 | — | yes |
| 12 | [Add and remove extra animation models](stories/L1.animation.extra-model-lifecycle.md) | animation | M | 2 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Carry  | yes |
| 13 | [Restore extra models and wind state](stories/L1.animation.animating-restore.md) | animation | M | 1 | 1 | Add and remove extra animation models | waits for 1 lower-layer rows |
| 14 | [Toggle retail flex blink state](stories/L1.animation.flex-blink.md) | animation | S | 1 | 1 | — | yes |
| 15 | [Preserve locked and unlocked handle clips](stories/L1.animation.lockable-handle-clip.md) | animation | M | 1 | 2 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 16 | [Read the ragdoll render marker](stories/L1.physics.l1-animation-ragdoll-marker-query.md) | physics | S | 1 | 1 | — | yes |
| 17 | [Read and write indexed pose parameters](stories/L1.animation.pose-parameters.md) · live (90,430 stub hits) | animation | M | 4 | 1 | Rebind model animation data / Resolve sequence labels and activity names | yes |
| 18 | [Build gameplay bones and trace studio hitboxes](stories/L1.animation.studio-hitbox-traces.md) | animation | L | 11 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Read a | waits for 1 lower-layer rows |
| 19 | [Resolve bone transforms and attachment endpoints](stories/L1.animation.bone-attachments.md) | animation | L | 6 | 2 | Rebind model animation data / Resolve sequence labels and activity names, Build  | waits for 3 lower-layer rows |
| 20 | [Apply burn state and timed hitbox effects](stories/L1.animation.burn-model.md) | animation | L | 3 | 1 | Build gameplay bones and trace studio hitboxes | waits for 2 lower-layer rows |
