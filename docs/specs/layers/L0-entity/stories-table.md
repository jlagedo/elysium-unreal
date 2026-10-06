| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [The test instrument: fixtures, entity_call, entity_field, retail_site](stories/L0.harness.test-instrument.md) | harness | L | 0 | 3 | — | yes |
| 2 | [The kernel ledger stops citing port lines](stories/L0.tooling.ledger-port-citations.md) | tooling | S | 0 | 1 | — | yes |
| 3 | [Retail-default slot stubs become default bodies](stories/L0.tooling.default-stubs.md) | tooling | S | 0 | 1 | — | yes |
| 4 | [AI sound base slots](stories/L0.entity_core.aisound-base-slots.md) · live (376 stub hits) | entity_core | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 5 | [Ambient radius sound level](stories/L0.audio.ambient-radius-level.md) | audio | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 6 | [Entity flag word](stories/L0.entity_core.eflags-word.md) | entity_core | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 7 | [Convert input variants](stories/L0.entity_io.variant-convert.md) | entity_io | L | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 8 | [Door sound stimulus](stories/L0.movers_doors.door-sound-stimulus.md) | movers_doors | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 9 | [Entity collision bounds](stories/L0.physics.entity-bounds.md) | physics | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 10 | [Named target input dispatch](stories/L0.rules_cvars.named-target-dispatch.md) | rules_cvars | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 11 | [Write named field headers](stories/L0.save_restore.save-field-headers.md) | save_restore | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 12 | [Client sound slot index](stories/L0.sound_list.client-sound-index.md) | sound_list | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 13 | [KeyValues lexer](stories/L0.audio.keyvalues-lexer.md) | audio | S | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 14 | [Solid changes and untouch checks](stories/L0.entity_core.collision-touch.md) | entity_core | L | 5 | 1 | Entity flag word, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 15 | [Invoke queued Python results](stories/L0.entity_io.python-payload.md) | entity_io | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 16 | [Ammo definition registration](stories/L0.rules_cvars.ammo-definition-table.md) | rules_cvars | M | 3 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 17 | [Sound record lookup](stories/L0.sound_list.sound-record-lookup.md) | sound_list | M | 1 | 2 | Client sound slot index, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 18 | [KeyValues file tree parsing](stories/L0.audio.keyvalues-tree.md) | audio | M | 3 | 1 | KeyValues lexer, The test instrument: fixtures, entity_call, entity_field, retai | yes |
| 19 | [Collision bounds and size](stories/L0.entity_core.bounds-size.md) | entity_core | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 20 | [Arm timer deadlines](stories/L0.entity_io.timer-arm.md) | entity_io | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 21 | [Angular move timing](stories/L0.movers_doors.angular-move-kinematics.md) | movers_doors | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 22 | [Timed law activity channels](stories/L0.rules_cvars.timed-law-channel.md) | rules_cvars | M | 2 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 23 | [Admit base entities to physics saving](stories/L0.save_restore.physics-save-eligibility.md) | save_restore | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 24 | [Trigger toucher admission](stories/L0.triggers.trigger-class-admission.md) | triggers | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 25 | [SoundScheme file loading](stories/L0.audio.scheme-file-load.md) | audio | M | 3 | 1 | KeyValues lexer, The test instrument: fixtures, entity_call, entity_field, retai | yes |
| 26 | [Base entity construction](stories/L0.entity_core.base-construction.md) | entity_core | M | 1 | 1 | Collision bounds and size, Solid changes and untouch checks, The test instrument | yes |
| 27 | [Fire logic auto on its first think](stories/L0.entity_io.logic-auto-first-fire.md) | entity_io | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 28 | [Trigger exit filtering and output](stories/L0.triggers.trigger-endtouch-output.md) | triggers | S | 1 | 2 | Trigger toucher admission, The test instrument: fixtures, entity_call, entity_fi | yes |
| 29 | [KeyValues lookup and mutation](stories/L0.audio.keyvalues-access.md) | audio | M | 4 | 1 | SoundScheme file loading, The test instrument: fixtures, entity_call, entity_fie | yes |
| 30 | [World sweep query primitives](stories/L0.effects_world.world-sweep-query.md) | effects_world | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 31 | [Rearm logic auto after restore](stories/L0.entity_io.logic-auto-restore.md) | entity_io | M | 1 | 2 | Solid changes and untouch checks, The test instrument: fixtures, entity_call, en | yes |
| 32 | [Doorknob spawn state](stories/L0.movers_doors.doorknob-spawn-state.md) | movers_doors | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 33 | [Parent unlink](stories/L0.physics.parent-link-lifecycle.md) | physics | S | 1 | 1 | Entity collision bounds, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 34 | [Read restore field header words](stories/L0.save_restore.restore-buffer-readers.md) | save_restore | S | 3 | 1 | Write named field headers, The test instrument: fixtures, entity_call, entity_fi | yes |
| 35 | [Lock key use priority](stories/L0.triggers.lock-key-use-priority.md) | triggers | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 36 | [Runtime decal projection](stories/L0.effects_world.runtime-decal.md) | effects_world | L | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 37 | [Entity data object registry](stories/L0.entity_core.data-object-registry.md) | entity_core | L | 6 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 38 | [Initialize timer state](stories/L0.entity_io.timer-spawn.md) | entity_io | M | 1 | 1 | Arm timer deadlines, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 39 | [Door knob registration](stories/L0.movers_doors.doorknob-registration.md) | movers_doors | M | 1 | 1 | Doorknob spawn state, The test instrument: fixtures, entity_call, entity_field,  | yes |
| 40 | [Clear eligible datamap fields](stories/L0.save_restore.restore-field-clear.md) | save_restore | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 41 | [Target name predicates](stories/L0.entity_core.target-name-predicates.md) | entity_core | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 42 | [Fire timer outputs](stories/L0.entity_io.timer-fire.md) | entity_io | M | 1 | 2 | Arm timer deadlines, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 43 | [Door activation target cache](stories/L0.movers_doors.door-activation-target-cache.md) | movers_doors | M | 1 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 44 | [Restore basic datamap field types](stories/L0.save_restore.restore-basic-types.md) | save_restore | L | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 45 | [Use toggle acceptance](stories/L0.entity_core.toggle-acceptance.md) | entity_core | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 46 | [Change timer refire time](stories/L0.entity_io.timer-refire.md) | entity_io | M | 1 | 1 | Arm timer deadlines, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 47 | [Opening a door unlocks registered knobs](stories/L0.movers_doors.door-open-unlocks-knobs.md) | movers_doors | S | 1 | 1 | Door knob registration, The test instrument: fixtures, entity_call, entity_field | yes |
| 48 | [Restore reference and engine-backed field types](stories/L0.save_restore.restore-extended-types.md) | save_restore | L | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 49 | [AI sound owner cache](stories/L0.audio.ai-sound-owner.md) | audio | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 50 | [Base entity perception defaults](stories/L0.entity_core.base-perception.md) | entity_core | S | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 51 | [Return input effect position](stories/L0.entity_io.input-effect-position.md) | entity_io | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 52 | [Door and knob use icons](stories/L0.movers_doors.door-use-icon-resolution.md) | movers_doors | M | 2 | 1 | Door activation target cache, Door knob registration, Doorknob spawn state, The  | yes |
| 53 | [Restore a named datamap block](stories/L0.save_restore.restore-datamap-block.md) | save_restore | L | 1 | 1 | Restore basic datamap field types, Read restore field header words, Restore refe | yes |
| 54 | [Sound emission origin](stories/L0.audio.sound-emission-origin.md) | audio | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 55 | [Blood effect admission and dispatch](stories/L0.effects_world.blood-effects.md) | effects_world | L | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 56 | [Monster template flag](stories/L0.entity_core.monster-template.md) | entity_core | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 57 | [Store and restore global states](stories/L0.entity_io.global-state-table.md) | entity_io | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 58 | [VSound folder membership](stories/L0.audio.sound-folder-index.md) | audio | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 59 | [Attach doorknobs during activation](stories/L0.entity_io.doorknob-activation.md) | entity_io | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 60 | [Entity water sampling](stories/L0.physics.entity-water-state.md) | physics | M | 1 | 1 | Entity collision bounds, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 61 | [Voice table category selection](stories/L0.audio.voice-table-index.md) | audio | M | 1 | 1 | Monster template flag, The test instrument: fixtures, entity_call, entity_field, | yes |
| 62 | [Angle writes and integration](stories/L0.entity_core.angle-motion.md) | entity_core | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 63 | [Drain due event records](stories/L0.entity_io.event-queue-drain.md) | entity_io | L | 1 | 3 | Invoke queued Python results, Convert input variants, The test instrument: fixtu | yes |
| 64 | [Sound channel parsing](stories/L0.audio.sound-channel-parse.md) | audio | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 65 | [Parented absolute transforms](stories/L0.entity_core.parented-transforms.md) | entity_core | L | 4 | 1 | Angle writes and integration, The test instrument: fixtures, entity_call, entity | yes |
| 66 | [Apply world area policy to the player](stories/L0.entity_io.world-area-policy.md) | entity_io | M | 1 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 67 | [Sound script descriptor defaults](stories/L0.audio.sound-script-defaults.md) | audio | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 68 | [Recursive entity teleport](stories/L0.entity_core.recursive-teleport.md) | entity_core | L | 2 | 1 | Angle writes and integration, Parented absolute transforms, The test instrument: | yes |
| 69 | [Sound script resolution](stories/L0.audio.sound-script-resolve.md) | audio | M | 4 | 1 | Sound channel parsing, Sound script descriptor defaults, The test instrument: fi | yes |
| 70 | [Named think contexts](stories/L0.entity_core.think-contexts.md) | entity_core | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 71 | [SoundScheme activation and track switching](stories/L0.audio.sound-scheme-switch.md) | audio | L | 5 | 1 | KeyValues lookup and mutation, SoundScheme file loading, The test instrument: fi | yes |
| 72 | [Base spawn defaults](stories/L0.entity_core.base-spawn.md) | entity_core | S | 1 | 1 | Base entity construction, The test instrument: fixtures, entity_call, entity_fie | yes |
| 73 | [Entity removal cleanup](stories/L0.entity_core.removal-cleanup.md) | entity_core | L | 5 | 1 | Entity flag word, Parent unlink, The test instrument: fixtures, entity_call, ent | yes |
| 74 | [Deferred entity deletion](stories/L0.entity_core.deferred-removal.md) | entity_core | L | 3 | 1 | Entity removal cleanup, The test instrument: fixtures, entity_call, entity_field | yes |
| 75 | [Entity post construction](stories/L0.entity_core.postconstructor-registration.md) | entity_core | M | 2 | 1 | Base entity construction, Deferred entity deletion, Entity flag word, The test i | yes |
| 76 | [Map entity text and parent ordering](stories/L0.entity_core.map-ordering.md) | entity_core | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 77 | [Named target removal](stories/L0.entity_core.named-target-removal.md) | entity_core | M | 1 | 1 | Deferred entity deletion, The test instrument: fixtures, entity_call, entity_fie | yes |
| 78 | [Registered system callbacks](stories/L0.entity_core.system-callbacks.md) | entity_core | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 79 | [Server shutdown and purge](stories/L0.entity_core.server-shutdown.md) | entity_core | L | 2 | 1 | Deferred entity deletion, Registered system callbacks, The test instrument: fixt | yes |
| 80 | [Sound volume table loading](stories/L0.entity_core.sound-volume-table.md) | entity_core | M | 2 | 1 | SoundScheme file loading, The test instrument: fixtures, entity_call, entity_fie | yes |
| 81 | [Once-only game initialization](stories/L0.entity_core.global-initialization.md) | entity_core | L | 5 | 1 | Sound volume table loading, The test instrument: fixtures, entity_call, entity_f | yes |
| 82 | [Instant hit effect dispatch](stories/L0.entity_core.instant-hit-effects.md) | entity_core | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 83 | [Landmark lookup](stories/L0.entity_core.landmark-lookup.md) | entity_core | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 84 | [Damage record initialization](stories/L0.physics.damage-record-packet.md) | physics | M | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 85 | [Save and restore blocks](stories/L0.entity_core.save-block-framework.md) | entity_core | L | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 86 | [Map transition save state](stories/L0.entity_core.transition-state.md) | entity_core | L | 5 | 1 | Save and restore blocks, Store and restore global states, The test instrument: f | yes |
| 87 | [Player map restore](stories/L0.entity_core.player-map-restore.md) | entity_core | L | 2 | 1 | Landmark lookup, Map transition save state, Apply world area policy to the playe | yes |
| 88 | [World impact damage rule](stories/L0.entity_core.world-impact-policy.md) | entity_core | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 89 | [Entity floating query](stories/L0.entity_core.floating-query.md) | entity_core | M | 1 | 1 | Entity flag word, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 90 | [Point contents cache](stories/L0.entity_core.point-contents-cache.md) | entity_core | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 91 | [Activity copy prop ownership](stories/L0.entity_core.activity-copy-owner.md) | entity_core | M | 1 | 1 | Entity post construction, Named think contexts, The test instrument: fixtures, e | yes |
| 92 | [Model activity metadata](stories/L0.entity_core.activity-metadata.md) | entity_core | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 93 | [View directed node and link selection](stories/L0.entity_core.view-node-links.md) | entity_core | L | 5 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 94 | [Entity aligned with player view](stories/L0.entity_core.entity-view-pick.md) | entity_core | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 95 | [Entity sight and search traces](stories/L0.entity_core.entity-sight-traces.md) | entity_core | L | 3 | 1 | Parented absolute transforms, The test instrument: fixtures, entity_call, entity | yes |
| 96 | [Client PVS selection](stories/L0.entity_core.client-pvs-selection.md) | entity_core | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 97 | [Entity sphere query](stories/L0.entity_core.sphere-entity-query.md) | entity_core | M | 1 | 1 | Collision bounds and size, The test instrument: fixtures, entity_call, entity_fi | yes |
| 98 | [Body queue creation](stories/L0.entity_core.body-queue.md) | entity_core | M | 1 | 1 | Base entity construction, Base spawn defaults, Entity post construction, The tes | yes |
| 99 | [Bind door completion and close sound at arrival](stories/L0.movers_doors.l0-movers-doors-door-arrival-binding.md) | movers_doors | S | 3 | 1 | Angular move timing, Drain due event records, Sound script resolution, The test  | yes |
| 100 | [Connect VPhysics data to baked ragdoll assets](stories/L0.parked.l0-physics-ragdoll-physics-asset-import.md) | parked | M | 0 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 101 | [Reliable native slot storage](stories/L0.parked.l0-save-restore-reliable-native-slot-storage.md) | parked | M | 0 | 1 | Save and restore blocks, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 102 | [Entity model and visibility](stories/L0.entity_core.entity-visual-state.md) · live (688 stub hits) | entity_core | M | 4 | 1 | Base entity construction, Entity flag word, The test instrument: fixtures, entit | yes |
| 103 | [Entity parenting and pose preservation](stories/L0.effects_world.entity-parenting.md) | effects_world | L | 6 | 1 | Parent unlink, The test instrument: fixtures, entity_call, entity_field, retail_ | yes |
| 104 | [Teleport trigger initialization](stories/L0.triggers.teleport-trigger-spawn.md) | triggers | M | 1 | 1 | Angle writes and integration, Solid changes and untouch checks, Entity model and | yes |
| 105 | [Particle parent attachment](stories/L0.effects_world.particle-attachment.md) | effects_world | M | 2 | 1 | Entity parenting and pose preservation, The test instrument: fixtures, entity_ca | yes |
| 106 | [Rotating door axes and spawn](stories/L0.movers_doors.rotating-door-axis-spawn.md) | movers_doors | M | 2 | 1 | Angle writes and integration, Solid changes and untouch checks, Entity model and | yes |
| 107 | [Physics object and collision interface](stories/L0.physics.physics-object-contract.md) | physics | L | 7 | 1 | Parented absolute transforms, Entity collision bounds, The test instrument: fixt | yes |
| 108 | [Prepare entity transform before saving](stories/L0.save_restore.on-save-transform-prep.md) | save_restore | M | 1 | 1 | Parented absolute transforms, The test instrument: fixtures, entity_call, entity | yes |
| 109 | [Multiple trigger wait and touch setup](stories/L0.triggers.multiple-trigger-spawn.md) | triggers | S | 1 | 1 | Teleport trigger initialization, The test instrument: fixtures, entity_call, ent | yes |
| 110 | [Particle emitter spawn](stories/L0.effects_world.particle-emitter-spawn.md) | effects_world | M | 1 | 1 | Particle parent attachment, The test instrument: fixtures, entity_call, entity_f | yes |
| 111 | [Entity collision mask and position query / Entity trace path](stories/L0.physics.collision-query.md) | physics | L | 7 | 2 | Entity collision bounds, Physics object and collision interface, The test instru | yes |
| 112 | [Particle rate ramp clock](stories/L0.effects_world.particle-rate-ramp.md) | effects_world | S | 1 | 1 | Particle emitter spawn, The test instrument: fixtures, entity_call, entity_field | yes |
| 113 | [Rotating door open-time query](stories/L0.movers_doors.rotating-door-open-time.md) | movers_doors | S | 1 | 1 | Rotating door axes and spawn, The test instrument: fixtures, entity_call, entity | yes |
| 114 | [Save base entity datamap state](stories/L0.save_restore.base-datamap-save.md) | save_restore | L | 1 | 1 | Prepare entity transform before saving, Write named field headers, The test inst | yes |
| 115 | [Datamap keyvalue parsing and reading](stories/L0.entity_core.datamap-keyvalues.md) | entity_core | L | 9 | 1 | Collision bounds and size, Parented absolute transforms, The test instrument: fi | yes |
| 116 | [Ambient sound initialization](stories/L0.audio.ambient-init.md) | audio | M | 3 | 1 | Ambient radius sound level, Datamap keyvalue parsing and reading, The test instr | yes |
| 117 | [Trigger volume bounds](stories/L0.physics.trigger-volume-bounds.md) | physics | M | 1 | 1 | Solid changes and untouch checks, Entity collision bounds, Physics object and co | yes |
| 118 | [Ambient source resolution and resume](stories/L0.audio.ambient-source-resume.md) | audio | M | 1 | 1 | Ambient sound initialization, The test instrument: fixtures, entity_call, entity | yes |
| 119 | [Switchable light state](stories/L0.effects_world.switchable-light.md) | effects_world | L | 6 | 1 | Datamap keyvalue parsing and reading, Parented absolute transforms, Use toggle a | yes |
| 120 | [Entity touch lifecycle](stories/L0.physics.touch-lifecycle.md) | physics | L | 9 | 1 | Entity data object registry, Entity collision mask and position query / Entity t | yes |
| 121 | [Ambient pitch input](stories/L0.audio.ambient-pitch.md) | audio | M | 1 | 1 | Ambient sound initialization, Ambient source resolution and resume, The test ins | yes |
| 122 | [Sprite lifecycle and animation state](stories/L0.effects_world.sprite-lifecycle.md) | effects_world | L | 6 | 1 | Angle writes and integration, Use toggle acceptance, The test instrument: fixtur | yes |
| 123 | [Physics shadow and child relink](stories/L0.physics.physics-shadow-relations.md) | physics | L | 5 | 1 | Parent unlink, Physics object and collision interface, Entity touch lifecycle, T | yes |
| 124 | [Illusionary brush spawn](stories/L0.effects_world.illusionary-brush-spawn.md) | effects_world | M | 1 | 1 | Angle writes and integration, Entity model and visibility, The test instrument:  | yes |
| 125 | [Movement vector primitives](stories/L0.physics.motion-primitives.md) | physics | M | 3 | 1 | Physics object and collision interface, The test instrument: fixtures, entity_ca | yes |
| 126 | [Door restore seating](stories/L0.movers_doors.door-restore-seating.md) | movers_doors | M | 2 | 2 | Angle writes and integration, Rotating door axes and spawn, The test instrument: | yes |
| 127 | [Ground and conveyor velocity](stories/L0.physics.conveyor-base-velocity.md) | physics | M | 1 | 1 | Physics object and collision interface, The test instrument: fixtures, entity_ca | yes |
| 128 | [Restore base entity transforms and runtime state](stories/L0.save_restore.base-entity-restore.md) | save_restore | L | 1 | 1 | Entity model and visibility, Restore a named datamap block, The test instrument: | yes |
| 129 | [Blocked door response](stories/L0.movers_doors.door-blocked-response.md) | movers_doors | L | 4 | 3 | Angle writes and integration, Parented absolute transforms, Angular move timing, | yes |
| 130 | [Single entity push sweep](stories/L0.physics.entity-push-sweep.md) | physics | M | 1 | 1 | Entity collision mask and position query / Entity trace path, Entity touch lifec | yes |
| 131 | [Four-bump entity movement](stories/L0.physics.entity-four-bump-move.md) | physics | L | 1 | 1 | Parented absolute transforms, Entity collision mask and position query / Entity  | yes |
| 132 | [Pushed entity transaction core / Rotational pusher transaction](stories/L0.physics.pushed-entity-transaction.md) | physics | L | 15 | 2 | Parented absolute transforms, Entity collision mask and position query / Entity  | yes |
| 133 | [Linear pusher transaction](stories/L0.physics.linear-push.md) | physics | L | 4 | 1 | Parented absolute transforms, Movement vector primitives, Physics object and col | yes |
| 134 | [Pusher time and blocked reaction](stories/L0.physics.pusher-blocked-reconciliation.md) | physics | L | 5 | 1 | AI sound base slots, Angle writes and integration, Parented absolute transforms, | yes |
| 135 | [Entity noclip movement](stories/L0.physics.noclip-movetype.md) | physics | M | 1 | 1 | Angle writes and integration, Parented absolute transforms, Physics object and c | yes |
| 136 | [Parented entity movement](stories/L0.physics.parented-entity-motion.md) | physics | L | 2 | 1 | AI sound base slots, Parented absolute transforms, Linear pusher transaction, Mo | yes |
| 137 | [Custom entity physics](stories/L0.physics.custom-movetype.md) | physics | M | 1 | 1 | Sound emission origin, Angle writes and integration, Parented absolute transform | yes |
| 138 | [Map entity construction](stories/L0.entity_core.map-entity-spawn.md) | entity_core | L | 1 | 1 | Base entity construction, Base spawn defaults, Datamap keyvalue parsing and read | yes |
| 139 | [Entity STEP movement](stories/L0.physics.step-movetype.md) | physics | L | 1 | 1 | Sound emission origin, Parented absolute transforms, Named think contexts, Entit | yes |
| 140 | [Fly collision resolution](stories/L0.physics.fly-collision-resolution.md) | physics | L | 1 | 1 | Single entity push sweep, Movement vector primitives, Entity touch lifecycle, Th | yes |
| 141 | [Entity toss and fly movement](stories/L0.physics.toss-movetype.md) | physics | L | 1 | 1 | Sound emission origin, Angle writes and integration, Entity collision mask and p | yes |
| 142 | [Entity physics simulation dispatch](stories/L0.physics.entity-simulation-dispatch.md) | physics | L | 3 | 1 | Parented absolute transforms, Ground and conveyor velocity, Custom entity physic | yes |
| 143 | [Physics prop construction](stories/L0.physics.physics-prop-construction.md) | physics | M | 3 | 1 | Entity collision mask and position query / Entity trace path, Physics object and | yes |
| 144 | [Ornament physics shape](stories/L0.physics.ornament-physics-shape.md) | physics | M | 1 | 1 | Physics object and collision interface, Physics shadow and child relink, The tes | yes |
| 145 | [Physics object pickup limit](stories/L0.physics.physics-object-pickup-limit.md) | physics | M | 1 | 1 | Physics object and collision interface, The test instrument: fixtures, entity_ca | yes |
| 146 | [Impact damage eligibility](stories/L0.physics.impact-damage-eligibility.md) | physics | M | 3 | 1 | Physics prop construction, The test instrument: fixtures, entity_call, entity_fi | yes |
| 147 | [Collision damage queue](stories/L0.physics.collision-damage-queue.md) | physics | L | 6 | 1 | Damage record initialization, Physics object and collision interface, The test i | yes |
| 148 | [Physics attacker attribution](stories/L0.physics.physics-attacker-attribution.md) | physics | M | 3 | 1 | Physics object and collision interface, The test instrument: fixtures, entity_ca | yes |
| 149 | [Impact damage resolution](stories/L0.physics.impact-damage-resolution.md) | physics | L | 5 | 1 | Collision damage queue, Damage record initialization, Impact damage eligibility, | yes |
| 150 | [World configuration and lifecycle](stories/L0.entity_core.world-lifecycle.md) | entity_core | L | 3 | 3 | Datamap keyvalue parsing and reading, Entity model and visibility, Once-only gam | yes |
| 151 | [Physics friction sound](stories/L0.physics.friction-feedback.md) | physics | L | 4 | 1 | Physics attacker attribution, Physics object and collision interface, The test i | yes |
| 152 | [Rope runtime activation](stories/L0.physics.rope-runtime.md) | physics | L | 8 | 1 | Datamap keyvalue parsing and reading, Entity collision bounds, Parent unlink, Th | yes |
| 153 | [Rope break and attachment release](stories/L0.physics.rope-break-release.md) | physics | M | 2 | 1 | Rope runtime activation, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 154 | [Inspection node creation](stories/L0.entity_core.inspection-node.md) | entity_core | M | 1 | 1 | Entity parenting and pose preservation, Base spawn defaults, Entity post constru | yes |
| 155 | [FuncBrush spawn](stories/L0.movers_doors.l0-entity-core-func-brush-spawn.md) | movers_doors | M | 1 | 1 | Solid changes and untouch checks, Entity model and visibility, The test instrume | yes |
| 156 | [Physics constraint and force entity family](stories/L0.physics.l0-physics-constraint-entity-family.md) | physics | M | 14 | 2 | Map entity construction, Physics object and collision interface, Physics prop co | yes |
| 157 | [Prop switch sound events and reset state](stories/L0.movers_doors.l0-entity-io-prop-switch-sound-and-reset.md) | movers_doors | S | 2 | 1 | Map entity construction, Restore base entity transforms and runtime state, Sound | yes |
| 158 | [Save content compatibility preflight](stories/L0.parked.l0-save-restore-content-compatibility-preflight.md) | parked | M | 0 | 1 | Save and restore blocks, Restore base entity transforms and runtime state, The t | yes |
| 159 | [Semantic save and import diagnostics](stories/L0.parked.l0-save-restore-semantic-save-diagnostics.md) | parked | M | 0 | 1 | Save and restore blocks, Restore base entity transforms and runtime state, Map t | yes |
| 160 | [Match retail ETABLE roster to native map definitions](stories/L0.parked.l0-save-restore-retail-save-roster-match.md) | parked | L | 0 | 1 | Restore a named datamap block, Restore base entity transforms and runtime state, | yes |
| 161 | [Import a retail save into one native slot](stories/L0.parked.l0-save-restore-retail-save-native-slot-import.md) | parked | M | 0 | 1 | Save and restore blocks, Restore base entity transforms and runtime state, Map t | yes |
| 162 | [Game frame simulation](stories/L0.entity_core.gameframe-simulation.md) | entity_core | L | 2 | 1 | Deferred entity deletion, Registered system callbacks, Entity physics simulation | yes |
| 163 | [Surface impact feedback](stories/L0.physics.surface-impact-feedback.md) | physics | L | 6 | 1 | Physics friction sound, Physics attacker attribution, Physics object and collisi | yes |
| 164 | [Breakable collision and death](stories/L0.physics.breakable-collision-lifecycle.md) | physics | L | 3 | 1 | Impact damage eligibility, Impact damage resolution, Surface impact feedback, Th | yes |
| 165 | [Breakable debris and fade](stories/L0.physics.breakable-debris.md) | physics | L | 7 | 1 | Solid changes and untouch checks, Parented absolute transforms, Breakable collis | yes |
