# L0 planning handoffs — higher-layer remainders

These are retained requirements, not implementation or hook-completion claims. They correct
parked work appended to L0 briefs. The [delivery map](delivery-map.tsv) is exhaustive at brief
level; mixed briefs retain their L0 portion and list their higher-layer consumers separately.
No retail core function is removed from L0 merely because a later layer calls it.

Before closing an affected L0 service, identify the actual caller/callee or named data field,
verify/register the upward hook in `../hooks.tsv` when there is a runtime call, and attach this
obligation to the recipient layer's story or explicit backlog. A data/content handoff need not
invent a call-graph edge. Missing hook metadata is routine planning repair; it is not permission
to answer an unfinished L0 producer with nothing. Recipient suggestions below require matching
the complete contract, not just a similar story title.

| Original appended brief | Primary owner and retained obligation | L0 service that remains required | Recipient evidence / remaining planning work |
|---|---|---|---|
| `L0.audio.voice-table-index-2` | L5 speech volume/extension/lip residue; verify L2/L4 field producers | D04 voice categories, source resolution and emission | `../L4-npc/stories/L5.dialogue.dialogue-speech-asset-name.md`; add uncovered source/asset residue explicitly. |
| `L0.entity_core.activity-metadata-3` | L2 combat reaction default, with L4 class-specific consumer where applicable | D01 activity-name registry and model metadata | `../L2-character/stories/L2.combat.activity-translation.md` and `../L4-npc/stories/L4.npc_kernel.activity-translation.md`; recover exact field producer before assigning its implementation. |
| `L0.entity_core.base-perception-2` | L3 illumination/stealth sample lifetime and saved state; L4 observation/refresh consumer | D01 base defaults; D11 block/restore service | `../L3-player/stories/L3.player.illumination.md`, player restore and L4 perception stories; do not implement the observer in a base-default packet. |
| `L0.entity_core.map-entity-spawn-2` | L4 maker think/child/cycle behavior | D01 real entity construction, registration and removal | Original parked R6 remains a named L4 backlog obligation; no complete recipient story was established by this review. |
| `L0.entity_core.recursive-teleport-2` | L5 scene actor resolution and scene-owned transform publication | D02 transform/teleport contract | Attach to the scripted-scene ownership work; the base teleport record does not test scene rebinding. |
| `L0.entity_core.removal-cleanup-3` | L5 scene claim release and clip-stop policy | D01 retirement/removal callback contract | Attach to the same scripted-scene lifetime owner; preserve the `SceneActors`/`DrivesActor` issue as evidence. |
| `L0.physics.physics-object-contract-4` | L1 ragdoll bone/fallback force application, consuming L2 death-force data | D02 body/impulse service; D09 generic physics assets | `../L1-animation/stories/L1.physics.l1-animation-ragdoll-marker-query.md` and `../L2-character/stories/L2.combat.corpse-death-flow.md` are related, not proof of complete coverage; add the remaining force/hit-bone obligation. |
| `L0.physics.physics-object-contract-5` | L1 animation-to-ragdoll/no-rig handoff; L2 death transition | D02 body creation/lifetime and D09 asset capability | Record the no-rig false arm; do not replace it with a fabricated pose/body success. |
| `L0.physics.trigger-volume-bounds-2` | Presentation camera director after L5: focus requests, handles and epoch ownership | D02 actual volume/query contract; D07 trigger events | Parked `../../0012-camera-director/spec.md` remains the acceptance owner. It is not a generic trigger-bounds prerequisite. |
| `L0.rules_cvars.named-target-dispatch-2` | L4 nineteen NPC input bindings and associated writes | D06 name/input dispatch | Explicit L4 binding/table-check obligation; this review did not establish an existing complete recipient story. |
| `L0.save_restore.physics-save-eligibility-2` | L1 death/ragdoll phase restore with L2 death consequences | D02 native body snapshots and D11 physics admission | `../L1-animation/stories/L1.animation.animating-restore.md` plus L2 corpse/death flow; no schedule/consequence replay. |
| `L0.sound_list.client-sound-index-2` | L3 player footstep producer; L4 hearing witness | D05 real stimulus insertion/slots; D02 surface queries | Explicit L3 footstep obligation remains; the L0 lookup record cannot close it. |

## Mixed briefs

- D04 owns generic typed emission and ambient resource lifetime. Weapon/discipline/player/NPC
  decisions and dialogue timing remain L2/L3/L4/L5. The known SoundScheme player-state write
  still needs its hook metadata checked, not a new policy decision.
- D05 owns insertion, capacity, identity, retention and expiry. L4 owns the `0xaf` program
  builder, alert ladder, `SpeakVSound` decisions, listener freshness/selection and its saved
  consumer state. Relevant existing recipient: `../L4-npc/stories/L4.perception.l4-listen-tail.md`;
  record any uncovered program/alert requirements explicitly.
- D01/D11 own clock, teardown, restore barriers and generic carried-entity identity. L2 owns
  item contents, L3 full player movement/placement, L4 sensing/NPC restore, and L5 dialogue/
  script state. Generic transport remains real; later blocks are registered by their owners.
- D08 owns contact-generated damage/stimulus packets. L2 consumes damage, L3 supplies player
  throw actions and L4 hears stimuli. A higher consumer does not excuse an absent L0 producer.
- D09 owns generic physics content and the retained link/constraint service. L1 skeletal
  gameplay and the registered L4 animlink callback remain outside the generic body adapter.

## Scope accounting still required

The delivery map marks four existing additional briefs `scope-review`: the three low-reach
constraint/force/animlink-family slices and the generic ragdoll physics-asset bake. They remain
accounted for in D09 until their required core producer role or explicit later-scope disposition
is recorded. This status does not authorize a scope cut or an IVP reconstruction.

The four `excluded` briefs are different: the owner explicitly excluded retail-save importing.
They require no permission to omit and create no higher-layer importer backlog.
