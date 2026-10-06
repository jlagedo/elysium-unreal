| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [Task facing random range](stories/L4.npc_kernel.task-facing-random-range.md) · live (17 stub hits) | npc_kernel | M | 1 | 1 | — | yes |
| 2 | [Sign use transaction](stories/L5.camera_ui.sign-use-transaction.md) | camera_ui | M | 1 | 1 | — | yes |
| 3 | [Dialogue row storage](stories/L5.dialogue.dialogue-row-storage.md) | dialogue | M | 5 | 1 | — | yes |
| 4 | [Interesting place visitor eviction](stories/L4.hints_places.interesting-place-visitor-eviction.md) | hints_places | M | 1 | 3 | — | yes |
| 5 | [Current waypoint distance](stories/L4.navigation.current-waypoint-distance.md) | navigation | M | 1 | 2 | — | yes |
| 6 | [Hit group records](stories/L4.npc_kernel.hit-group-records.md) | npc_kernel | M | 3 | 2 | — | yes |
| 7 | [Node search costs and tie order](stories/L4.npc_species.node-search-primitives.md) | npc_species | L | 2 | 2 | — | yes |
| 8 | [Initialize the Python bridge](stories/L5.python.python-bridge-init.md) | python | M | 2 | 1 | — | yes |
| 9 | [Expression table lifecycle](stories/L5.scripted.expression-table-lifecycle.md) | scripted | S | 2 | 1 | — | yes |
| 10 | [Grow the relationship table](stories/L4.social.relationship-table-growth.md) | social | M | 1 | 2 | — | yes |
| 11 | [Game text color parsing](stories/L5.camera_ui.game-text-color-parse.md) | camera_ui | M | 1 | 1 | — | yes |
| 12 | [Dialogue source and stream](stories/L5.dialogue.dialogue-source-stream.md) | dialogue | M | 5 | 1 | — | yes |
| 13 | [Targeted discipline table](stories/L4.npc_kernel.targeted-discipline-table.md) | npc_kernel | M | 4 | 1 | Hit group records | yes |
| 14 | [Random-node visited bitset](stories/L4.npc_species.random-node-visited-bitset.md) | npc_species | S | 1 | 1 | — | yes |
| 15 | [Expression identity and lookup](stories/L5.scripted.expression-index-lookup.md) | scripted | M | 4 | 1 | Expression table lifecycle | yes |
| 16 | [Resolve relationship answers](stories/L4.social.relationship-answers.md) | social | L | 2 | 2 | Grow the relationship table | yes |
| 17 | [Dialogue table parser](stories/L5.dialogue.dialogue-row-parser.md) | dialogue | M | 2 | 1 | Dialogue row storage, Dialogue source and stream | yes |
| 18 | [Discipline mask removal](stories/L4.npc_kernel.discipline-mask-removal.md) | npc_kernel | M | 2 | 1 | — | yes |
| 19 | [Weighted flex setting groups](stories/L5.scripted.flex-setting-groups.md) | scripted | L | 3 | 1 | — | yes |
| 20 | [Read the boss template flag](stories/L4.social.boss-template-flag.md) | social | M | 1 | 1 | — | yes |
| 21 | [Dialogue row lookup](stories/L5.dialogue.dialogue-row-lookup.md) | dialogue | S | 2 | 1 | Dialogue table parser | yes |
| 22 | [Clear active disciplines](stories/L4.npc_kernel.discipline-clear-sweep.md) | npc_kernel | M | 1 | 1 | — | yes |
| 23 | [Scene event intensity](stories/L5.scripted.scene-event-intensity.md) | scripted | M | 1 | 1 | — | yes |
| 24 | [Return the base follower boss](stories/L4.social.base-follower-boss.md) | social | S | 1 | 1 | — | yes |
| 25 | [Clan template cache](stories/L5.dialogue.dialogue-clan-cache.md) | dialogue | S | 2 | 1 | — | yes |
| 26 | [Interrupt schedules for active disciplines](stories/L4.npc_kernel.discipline-schedule-interrupt.md) | npc_kernel | M | 1 | 1 | Targeted discipline table | yes |
| 27 | [Squad notification on memory miss](stories/L4.npc_species.squad-memory-miss-notifier.md) | npc_species | L | 1 | 1 | — | yes |
| 28 | [Raw scene event duration](stories/L5.scripted.scene-raw-duration.md) | scripted | M | 1 | 1 | — | yes |
| 29 | [Construct and register a squad](stories/L4.social.squad-construction.md) | social | L | 2 | 1 | — | waits for 1 lower-layer rows |
| 30 | [Dialogue dependency test](stories/L5.dialogue.dialogue-dependency-test.md) | dialogue | S | 2 | 1 | Clan template cache, Dialogue table parser | yes |
| 31 | [Discipline projectile launch](stories/L4.npc_kernel.discipline-projectile-launch.md) | npc_kernel | L | 2 | 1 | — | waits for 2 lower-layer rows |
| 32 | [Sight cache first-handle semantics](stories/L4.npc_species.sight-cache-first-handle.md) | npc_species | M | 1 | 1 | — | yes |
| 33 | [Gesture timing tag percentages](stories/L5.scripted.gesture-timing-tags.md) | scripted | M | 1 | 1 | — | yes |
| 34 | [Dialogue speech asset name](stories/L5.dialogue.dialogue-speech-asset-name.md) | dialogue | S | 1 | 1 | Clan template cache, Dialogue row lookup | yes |
| 35 | [Discipline trigger sequence](stories/L4.npc_kernel.discipline-trigger.md) | npc_kernel | L | 2 | 1 | Discipline projectile launch, Hit group records, Targeted discipline table | waits for 3 lower-layer rows |
| 36 | [Flex track sampling](stories/L5.scripted.flex-track-sampling.md) | scripted | L | 5 | 1 | — | yes |
| 37 | [Dialogue script report](stories/L5.dialogue.dialogue-script-report.md) | dialogue | L | 8 | 1 | Dialogue dependency test, Dialogue row lookup, Dialogue table parser, Dialogue s | yes |
| 38 | [Base NPC restore](stories/L4.npc_kernel.npc-base-restore.md) | npc_kernel | M | 1 | 1 | — | waits for 1 lower-layer rows |
| 39 | [Letterless dialogue report](stories/L5.dialogue.dialogue-ellipsis-report.md) | dialogue | S | 1 | 1 | Dialogue table parser, Dialogue source and stream | yes |
| 40 | [Troika NPC save and restore](stories/L4.npc_kernel.npc-troika-save-restore.md) | npc_kernel | L | 2 | 1 | Base NPC restore | yes |
| 41 | [Automatic end report](stories/L5.dialogue.dialogue-autoend-report.md) | dialogue | S | 1 | 1 | Dialogue row lookup, Dialogue table parser, Dialogue source and stream, Dialogue | yes |
| 42 | [Task running status](stories/L4.npc_kernel.task-running.md) | npc_kernel | S | 1 | 1 | — | yes |
| 43 | [Dialogue script problem audit](stories/L5.dialogue.dialogue-script-problem-audit.md) | dialogue | M | 3 | 1 | Dialogue dependency test, Dialogue table parser, Dialogue source and stream | yes |
| 44 | [NPC state flags](stories/L4.npc_kernel.state-change-flags.md) | npc_kernel | M | 1 | 1 | — | yes |
| 45 | [Navigation condition gate](stories/L4.npc_kernel.condition-46-gate.md) | npc_kernel | S | 1 | 1 | — | yes |
| 46 | [NPC eye direction](stories/L4.npc_kernel.eye-direction.md) | npc_kernel | L | 1 | 1 | — | waits for 1 lower-layer rows |
| 47 | [Expression event queue](stories/L4.npc_kernel.expression-event-queue.md) | npc_kernel | M | 2 | 1 | — | yes |
| 48 | [Choreography flex timeline](stories/L4.npc_kernel.flex-timeline.md) | npc_kernel | L | 3 | 1 | — | yes |
| 49 | [NPC sound key values](stories/L4.npc_kernel.npc-keyvalue-sound.md) | npc_kernel | S | 1 | 1 | — | waits for 1 lower-layer rows |
| 50 | [Base reload activity](stories/L4.npc_kernel.reload-base.md) | npc_kernel | M | 1 | 2 | — | yes |
| 51 | [NPC motor construction](stories/L4.npc_kernel.motor-construction.md) | npc_kernel | M | 1 | 1 | — | yes |
| 52 | [Model eye position](stories/L4.npc_kernel.model-eye-position.md) | npc_kernel | S | 1 | 1 | — | yes |
| 53 | [Squad member lookup](stories/L4.npc_kernel.squad-member-lookup.md) | npc_kernel | S | 1 | 1 | — | yes |
| 54 | [Notify squad when an enemy eludes](stories/L4.npc_kernel.squad-enemy-eluded-notify.md) | npc_kernel | M | 1 | 1 | — | yes |
| 55 | [NPC entity handle validation](stories/L4.npc_kernel.valid-entity-handle.md) | npc_kernel | S | 1 | 1 | — | yes |
| 56 | [Interesting place death activity](stories/L4.npc_kernel.interesting-place-death-activity.md) | npc_kernel | M | 2 | 1 | — | yes |
| 57 | [Select a kickable prop](stories/L4.npc_kernel.kickable-prop-selection.md) | npc_kernel | M | 2 | 1 | — | waits for 1 lower-layer rows |
| 58 | [Set navigator target and offset](stories/L4.npc_kernel.navigator-target-offset.md) | npc_kernel | M | 1 | 1 | — | yes |
| 59 | [Refresh approach goals](stories/L4.npc_kernel.approach-goal-refresh.md) | npc_kernel | L | 2 | 1 | Set navigator target and offset | yes |
| 60 | [Witness global act maxima](stories/L4.npc_kernel.witness-global-act-maxima.md) | npc_kernel | L | 1 | 1 | — | waits for 1 lower-layer rows |
| 61 | [Walk a directed path](stories/L4.npc_kernel.directed-path-point.md) | npc_kernel | L | 3 | 1 | Node search costs and tie order, Random-node visited bitset | yes |
| 62 | [Back-away path search](stories/L4.npc_kernel.back-away-path.md) | npc_kernel | L | 2 | 1 | Node search costs and tie order, Random-node visited bitset | yes |
| 63 | [Find a shooting position](stories/L4.npc_kernel.shooting-position.md) | npc_kernel | L | 3 | 1 | — | waits for 1 lower-layer rows |
| 64 | [Compute jump velocity](stories/L4.npc_kernel.jump-velocity.md) | npc_kernel | M | 1 | 1 | — | yes |
| 65 | [Find floor for NPC movement](stories/L4.npc_kernel.floor-drop.md) | npc_kernel | M | 1 | 1 | — | yes |
| 66 | [Validate two-leg movement](stories/L4.npc_kernel.two-leg-move-limit.md) | npc_kernel | M | 1 | 1 | — | yes |
| 67 | [Python global-state reset](stories/L5.entity_core.l5-python-global-state-reset.md) | entity_core | S | 1 | 1 | — | yes |
| 68 | [Initialize the runtime AI node network](stories/L4.navigation.l4-navigation-node-network-initialization.md) | navigation | L | 3 | 1 | — | yes |
| 69 | [Restore AI node connectivity rules](stories/L4.navigation.l4-npc-species-node-connectivity.md) | navigation | S | 1 | 1 | — | yes |
| 70 | [Scripted map-change transaction](stories/L5.scripted.l5-scripted-scripted-map-change.md) | scripted | M | 1 | 1 | — | yes |
| 71 | [Build node-route waypoint chains](stories/L4.audio.l4-npc-species-node-route-waypoint-chain.md) | audio | L | 5 | 1 | — | waits for 2 lower-layer rows |
| 72 | [Simplify generated NPC paths](stories/L4.audio.l4-npc-species-path-waypoint-simplification.md) | audio | L | 4 | 1 | Build node-route waypoint chains, Current waypoint distance | waits for 4 lower-layer rows |
| 73 | [Select the retail NPC floor trace](stories/L4.entity_core.l4-npc-kernel-floor-trace-selection.md) | entity_core | M | 1 | 1 | — | yes |
| 74 | [Complete NPC linear and angular movement](stories/L4.navigation.l4-npc-kernel-npc-movement-completion.md) | navigation | M | 2 | 1 | — | yes |
| 75 | [Initialize default AI schedules](stories/L4.entity_core.l4-npc-kernel-default-schedule-initialization.md) | entity_core | S | 1 | 1 | — | yes |
| 76 | [Forget an entity across NPCs](stories/L4.physics.l4-npc-kernel-entity-forget-broadcast.md) | physics | M | 1 | 1 | — | yes |
| 77 | [Restore retail hearing occlusion sampling](stories/L4.player.l4-perception-sound-occlusion.md) | player | L | 1 | 1 | — | yes |
| 78 | [Search and reserve police response nodes](stories/L4.player.l4-navigation-police-spawn-node-search.md) | player | L | 1 | 1 | — | yes |
| 79 | [Normalize NPC directions in the horizontal plane](stories/L4.navigation.l4-npc-kernel-horizontal-direction-normalization.md) | navigation | S | 1 | 1 | — | yes |
| 80 | [Emit NPC script sounds with retail sound levels](stories/L4.npc_species.l4-npc-kernel-npc-script-sound-emission.md) | npc_species | M | 2 | 1 | — | waits for 1 lower-layer rows |
| 81 | [Filter traces against an entity's root owner](stories/L4.npc_species.l4-npc-species-root-owner-trace-filter.md) | npc_species | S | 1 | 1 | — | waits for 1 lower-layer rows |
| 82 | [Classify human combatants](stories/L4.npc_species.l4-npc-species-human-combatant-classification.md) | npc_species | S | 1 | 1 | — | yes |
| 83 | [Load and apply stealth deaf-zone rules](stories/L4.perception.l4-perception-stealth-deaf-zone-rules.md) | perception | L | 5 | 1 | — | waits for 1 lower-layer rows |
| 84 | [Run the sight reaction at the Look boundary](stories/L4.perception.l4-look-tail.md) | perception | M | 1 | 2 | Sight cache first-handle semantics, Resolve relationship answers | yes |
| 85 | [Promote heard conditions before firing outputs](stories/L4.perception.l4-listen-tail.md) | perception | M | 1 | 2 | Run the sight reaction at the Look boundary | yes |
| 86 | [Node route endpoint legs](stories/L4.npc_species.node-route-endpoint-legs.md) | npc_species | L | 3 | 2 | Build node-route waypoint chains, Node search costs and tie order | waits for 1 lower-layer rows |
| 87 | [Select the nearest retained sound or scent](stories/L4.perception.l4-nearest-sound-scent.md) | perception | L | 1 | 2 | Promote heard conditions before firing outputs | yes |
| 88 | [Path waypoint tail finalization](stories/L4.npc_species.path-waypoint-finalization.md) | npc_species | M | 1 | 1 | Node route endpoint legs | yes |
| 89 | [Translate the base NPC enemy chase position](stories/L4.perception.l4-enemy-lkp-chase-anchor.md) | perception | M | 1 | 2 | Run the sight reaction at the Look boundary | yes |
| 90 | [NPC activity translation](stories/L4.npc_kernel.activity-translation.md) | npc_kernel | M | 2 | 1 | Base reload activity | yes |
| 91 | [Build a local route](stories/L4.npc_kernel.local-route.md) | npc_kernel | L | 5 | 2 | Build node-route waypoint chains | waits for 1 lower-layer rows |
