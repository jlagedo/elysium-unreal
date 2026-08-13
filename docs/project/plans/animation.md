# Skeletal animation plan (ANM) — open-task specifications

Specifications for **open** ANM tasks only. Status lives solely in `docs/project/roadmap.md`;
a task that lands is deleted here. No status marks in this file.

The governing owner call is recorded in `docs/architecture/animation-architecture.md`: VtMB's
animation data bakes into native Unreal assets and Unreal's animation system runs it — every
VtMB rule resolved at bake, with axis interpolation the one live composition stage kept (the
standing exemption in root `CLAUDE.md`). The goal is VtMB's animation *content* on Unreal, not
numerical equivalence; retail capture returns as an oracle when a visible divergence needs
explaining, never as a gate. The CCC slice consumes what this programme bakes.

### ANM1 Bake the character assets

One body `USkeleton` per compatible named bone tree (animals, skeletal props and `wolf_form`
naturally on their own), a `USkeletalMesh` per model with its exact authored bind and morph targets
intact, and a compressed `UAnimSequence` per distinct clip. Shared-bank sequences are generated
once on compatible bank skeletons and reused by body skeletons through Unreal's compatible-skeleton
and `RetargetSource`/`OrientAndScale` asset metadata. Common `USkeleton` reference rotations are
neutral so compatible-skeleton remapping cannot rotate a decoded pose; animation rotations pass
verbatim and translation retargeting implements the recovered donor-to-target position mapping.
The `_delta` family names and subtracts its own baked base. The editor commandlet consumes the
Unreal-native ESKM container on the same `/ElysiumBaked` mount; glTF stays an inspection product.
Cinematic actor banks are emitted once in the shared namespace; no body family receives an empty
scene-package copy. *Retires:* the
per-map-epoch retarget cache, `RemoveTracks` bank filtering, and map-actor sequence caching.
*Acceptance:* every character loads from the baked mount with no glTFRuntime call at runtime —
the only build of a character, so a missed stem fails by name — and the facial morph-target
contract holds. A preflight inventory proves that every source bank clip is packaged once (plus
named base/overlay derivatives): adding a compatible body family cannot increase the bank package
count, and any projected `bank clips × body families` cross-product fails before Unreal starts.

### ANM2 Carry the two discarded MDL fields — remaining

Both halves are delivered (masks as named `UBlendProfile`s per family skeleton, the binding
table across the include DAG into `blends/<stem>.json`, consumed by CCC10's nodes). What
remains:

- **The orphan census reproducing** — orphanhood measured across the include DAG, not assumed.
- **The composition weight the dispatcher passes.** The four-byte autolayer record carries no
  weight, ramp or flags, so the scalar lives in the game DLL and only retail answers it. First
  move: an analysis pass over the finalized captures rather than a new hook — the combine is
  closed arithmetic, so a host's decoded local, its layer's decoded local and the composed
  local determine the scalar per bone; a value consistent across the mask's bones measures it
  while confirming the combine. Acceptance is a coverage argument, not a number: the recipe
  exercises weapon draw, an aim transition and a sequence crossfade, and constancy counts only
  when witnessed over conditions that would have varied it. A weapon and action select their
  own `_aim_layer` and `_delta` through the runtime's own caller at a weight measured or named
  in the code as a stand-in; the green room displays the selection, the authority and the
  declared entry order beside what runs.

### ANM4b Export and bake the action catalog

Extend the clip/grid/index sidecars with events, autolayers and the per-model
sequence-transition graph — the NPC resolver traverses an intermediate transition sequence
between current and ideal, so the graph is a catalog input CCC11 depends on. Emit
activity/player/NPC/weapon rule artifacts, join them into a reachability report, and bake the
resolved catalog beside the native character assets. Game data remains below
`$ELYSIUM_EXPORT_ROOT` and `/ElysiumBaked`. *Acceptance:* every producer in the accepted action
families resolves to an exact model/sequence identity and baked asset or a named fallback; a
controlled retail/remake trace agrees on base activity, each translation and the final
sequence; missing required mappings fail content tests; player and NPC records use one trace
schema. (ANM4a — the extraction RE — is closed as RE37.)

### ANM6 Migrate the cinematic path

Choreographed playback from the sequence player's absolute-time seek to a montage position.
**Deliberately last**: the theatre is the proven ground and stays on its verified seek path
until the stack under it is established. Owns the gesture/sequence un-collapse handed over from
CCC10 — a gesture is an overlay in the same four-slot `CBaseAnimatingOverlay` array as an
`ACT_*_LAYER_*` selection, rate-scaled at start then free-running
(`docs/vtmb/animation_and_movers.md` A.4c), so a second scene-time-pinned player would
reproduce timing retail does not have; whether the composite wants a montage slot or the
weapon layers' overlay treatment is this rung's design call. Also this rung's call: whether a
scene's clip changes regain a crossfade (the divergence CCC9 recorded). *Open input:* the
caller's composition weight — ANM2's question — gates how a gesture reads against its base.

### Open defect — `A_dance01` arm seam

`A_dance01` leaves a residual arm seam on some bodies: edge stretch past 5× on `goth_female`
and `tremere_female_armor_0` (identical maxima to nine figures), 3.59× on `female_dancer_2`'s
tracked seam pair, clean on the Joy idles. Visibility unmeasured — the metric is CPU-skinned
edge stretch, not an observed frame. `Instrument.Elysium.DancerDecodeProbe3` reports per-bone
remap state and per-edge deformation on four bodies; the open question is which bones carry the
stretch, then whether their translations, rotations or skin influences differ from Joy's.

### Divergences to record when settled

Each lands beside the faithful behaviour in its owning doc: layer blend-in/out times (ours —
the record carries no ramp) → `docs/vtmb/animation_and_movers.md`; blend-space interpolation
replacing the authored cell selection, the bake-then-blend residual across a crossfade (bounded
≤10° measured), and an overlay/additive composed over a host that does not declare it →
`docs/architecture/animation-architecture.md`.
