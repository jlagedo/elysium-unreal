| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [Initialize animating ragdoll slots](stories/L1.animation.animating-spawn-state.md) | animation | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 2 | [Rebind model animation data / Resolve sequence labels and activity names](stories/L1.animation.model-animation-banks.md) | animation | M | 7 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 3 | [Choose activity sequences with retail draws](stories/L1.animation.activity-sequence-choice.md) | animation | L | 6 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 4 | [Find transition sequences](stories/L1.animation.transition-graph.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 5 | [Advance the retail sequence clock](stories/L1.animation.sequence-clock.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 6 | [Derive movement from sequence motion](stories/L1.animation.sequence-motion.md) | animation | M | 3 | 1 | Advance the retail sequence clock, The test instrument: fixtures, entity_call, e | waits for 1 lower-layer rows |
| 7 | [Dispatch sequence events in retail order](stories/L1.animation.animation-event-dispatch.md) | animation | M | 1 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Advanc | yes |
| 8 | [Carry deferred sequence effects and transforms](stories/L1.animation.next-sequence-effects.md) | animation | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 9 | [Pack and resolve bodygroups](stories/L1.animation.bodygroup-state.md) | animation | M | 2 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 10 | [Delegate animating collision ignore](stories/L1.animation.collision-ignore.md) | animation | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 11 | [Return ideal speed plus acceleration allowance](stories/L1.animation.ideal-acceleration.md) | animation | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 12 | [Add and remove extra animation models](stories/L1.animation.extra-model-lifecycle.md) | animation | M | 2 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Carry  | yes |
| 13 | [Restore extra models and wind state](stories/L1.animation.animating-restore.md) | animation | M | 1 | 1 | Add and remove extra animation models, The test instrument: fixtures, entity_cal | waits for 1 lower-layer rows |
| 14 | [Toggle retail flex blink state](stories/L1.animation.flex-blink.md) | animation | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 15 | [Preserve locked and unlocked handle clips](stories/L1.animation.lockable-handle-clip.md) | animation | M | 1 | 2 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 16 | [Read the ragdoll render marker](stories/L1.physics.l1-animation-ragdoll-marker-query.md) | physics | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 17 | [Bake retail overlay bone masks](stories/L1.parked.l1-animation-overlay-mask-bake.md) | parked | S | 0 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 18 | [Read and write indexed pose parameters](stories/L1.animation.pose-parameters.md) · live (90,430 stub hits) | animation | M | 4 | 1 | Rebind model animation data / Resolve sequence labels and activity names, The te | yes |
| 19 | [Build gameplay bones and trace studio hitboxes](stories/L1.animation.studio-hitbox-traces.md) | animation | L | 11 | 1 | Rebind model animation data / Resolve sequence labels and activity names, Read a | waits for 1 lower-layer rows |
| 20 | [Resolve bone transforms and attachment endpoints](stories/L1.animation.bone-attachments.md) | animation | L | 6 | 2 | Rebind model animation data / Resolve sequence labels and activity names, Build  | waits for 3 lower-layer rows |
| 21 | [Apply burn state and timed hitbox effects](stories/L1.animation.burn-model.md) | animation | L | 3 | 1 | Build gameplay bones and trace studio hitboxes, The test instrument: fixtures, e | waits for 2 lower-layer rows |
