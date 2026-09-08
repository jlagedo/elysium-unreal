# Decisions

Append-only ledger of owner calls: one entry per call, added at the bottom of its domain group.
Entries are never re-edited or deleted; a reversal is a new entry that says what it reverses.
The port default is reproduce-retail, so nothing that merely reproduces VtMB is recorded here —
only deliberate divergences (named modernizations), owner rulings, and traps a reader could not
derive from the code. Retail defects live in `docs/vtmb/retail-defects.md`, not here.

## Docs & process

- **undated** — Only the human owner accepts a deliverable, by eye on screen; a session may produce a measured artifact but never adjudicate its own result. A screenshot the session cannot judge is not acceptance.
- **undated** — Task worktrees are code-and-test only; `reconstruct`, every `export`, `run editor|play`, `debug`, `gr` and `mcp` fail before launch outside the primary checkout. Generated assets, Live Coding state and visual acceptance stay singly owned on `main`.
- **undated** — `Content/ElysiumAuthored/**` is for independently-owned authored work only; putting game-derived exports, transforms, timings or scripts there is a policy violation. It is the one hand-owned, git-LFS-tracked namespace.
- **2026-08-31** — Roadmap rule "wire first, tune later": a pipeline stage only makes things appear, move or work, and agents never read screenshots to tune. Shots are a did-it-appear witness.
- **2026-08-31** — Every taste value is tuned on editor knobs (developer-settings page, data-asset grid, material-instance editor), never by an agent in a build-launch-look loop or by frame capture. The editor is the viewer; `shots_diff.py` records, never tunes.
- **2026-09-07** — Acceptance must be beat-scripted in the Play tier; dev-console shortcuts are never acceptance evidence, and nothing on the playable path is done until the driver exists. Console shortcuts prove the code path, not the beat.
- **2026-09-07** — No terminal acceptance is planned on a live map; headless gym and tutorial-slice acceptance only. Owner call.
- **2026-09-07** — The port is a VM host: every observable transition is contract, and a modernization is admissible only when it is pixel-only and Unreal already ships the algorithm. Framing that governs every divergence below.
- **2026-09-07** — Docs layout is `vision.md`, `decisions.md`, `vtmb/` (oracle), `contracts/` (seam formats) and `specs/NNNN-<witness>/` (open threads, deleted on landing); no roadmap and no architecture prose. The code is the architecture; tests and commit messages are the as-built record.
- **2026-09-07** — Specs are sliced by witness (a played beat or a retail program), never by subsystem; subsystems are built across specs in dependency order, and cross-spec inputs are seams answering "nothing" until the owning spec fills them.
- **2026-09-07** — By default a model edits only `docs/vtmb` and `docs/decisions.md`; vision, contracts and specs are owner-edited, except that a session executing a spec may flip that spec's task status.

## Camera

- **undated** — The recovered VtMB camera stays the reference/compatibility evaluator, not the shipped feel target: persistent first/third-person modes, third-person orbit separate from character facing, and the player's camera preference is never permanently overridden by weapons, dialogue, focus or cutscenes. Feel target is the modern rig, not 2004 parity.
- **undated** — Weapon-class camera arbitration (bits at player+0x2440) is deliberately deferred, not built. The recovered evidence reads as Logic, not preference; reproducing on a guess would need un-reproducing later.
- **undated** — Named feel divergence: the camera damper is frame-rate independent (half-life decay) instead of VtMB's Euler `K*Dt` step. Measured 0.36–0.47 u apart from the faithful boom.
- **undated** — Named feel divergence: camera collision recovery is rate-limited and asymmetric (instant retract, damped recovery) rather than the faithful symmetric re-seed.
- **undated** — Named feel divergence: a shoulder offset is applied in boom space, which the retail rig has none of. Explains new clipping against a centred boom.
- **undated** — Named divergence: a sphere probe substitutes for VtMB's box-hull (`UTIL_TraceHull`) camera sweep.
- **2026-09-07** — Named modernization M1: shot release is same-tick removal, with no blend cvar.
- **2026-09-07** — Named modernization M2: the goal-publish think runs at 24 Hz with the retail internal order kept verbatim.
- **2026-09-07** — Named modernization M3: the AttachType compare stays case-sensitive, so a mis-cased "follow" latches exactly as retail.
- **2026-09-07** — Named modernization M4: skip the bit quantization but keep the encoder clamps as contract.
- **2026-09-07** — Named modernization M5: the fade state machine is ported verbatim.
- **2026-09-07** — Named modernization M6: `RemainingTime` is verbatim, clamping the radicand only at zero.
- **2026-09-07** — Named modernization M7: the `MoveAccel == 0` crawl is an explicit branch, not a real divide.
- **2026-09-07** — Named modernization M8: one adoption slot, and the terminal closer drops to the player view.
- **2026-09-07** — Named modernization M9 withdrawn: `SimpleSpline` composition is reclassified as contract, not a modernization.
- **2026-09-07** — Named modernization M10: the vehicle-view arm is ruled dead code and not ported.
- **2026-09-07** — Named modernization M11: `camortho` is re-scoped to Source's ortho debug view, leaving the letterbox as the only pixel divergence.
- **2026-09-07** — Named modernization M12: the cine-FOV guard closed by RC9 keeps the retail name and threshold.
- **2026-09-07** — Named modernization M13: PVS replacement is not applicable and the retail artefact is deliberately not reproduced.
- **2026-09-07** — Named modernization M14: the retail HUD element set is not reproduced (the HUD is a new asset), but the HUD edge timing stays contract.
- **2026-09-07** — Named modernization M15: no Hor+ FOV widening; FOV stays Unreal-native.
- **2026-09-07** — Named modernization M16: the `camera_showdebug` overlay stays undrawn and the cvar is kept only for the record. The retail overlay draws nothing on live retail either.
- **2026-09-07** — Named modernization: the `DialogPOV` gaze flag defaults **set** when a dialogue path has no source shot at all. 51 of 66 shipped shot files set it, so the majority case should not default unset.

## Dialogue

- **undated** — Named modernization M-SAVE: no save inside a conversation, ever.
- **2026-09-06** — Named modernization M-UI: the conversation panel is the project's own style, not retail fonts and dots.
- **2026-09-06** — Named modernization M-REQ: requirement labels (BG3-style) replace retail's font/colour encoding.
- **2026-09-06** — Named modernization M-DISABLED: failed skill-front rows are shown greyed with a label, under a precise disable rule with dedupe.
- **2026-09-06** — Named modernization M-REVEAL: choices show immediately instead of being withheld during NPC speech.
- **2026-09-06** — Named modernization M-SKIP: the hurry verb becomes a skip key.
- **2026-09-06** — Named modernization M-CAP: no 4-response cap; overage is logged as an authoring finding.
- **2026-09-06** — Named modernization M-REFUSE: retail's silent refusal becomes a HUD notification.
- **2026-09-06** — Trap: feat 0x16 `FrenzyComparison` is unrecovered and deliberately excluded from the disabled-row set. Including it would draw a `[FRENZY 0/1]` label retail never shows.
- **2026-09-06** — Retail's unread third dialogue-file extension string is carried as a TODO rather than guessed. The corpus has no example to confirm it.
- **2026-09-07** — The dialogue band type switched from the Nocturne mix to all-Inter, the panel slab was recoloured from washed grey to pure black at 0.69 alpha, and per-row plates were removed. Measured against a retail capture: Spectral's serifs and the old grey slab were both wrong at the corrected pitch.

## Audio

- **undated** — Trap: retail `ambient_generic` has no FadeIn/FadeOut inputs — `fadein`/`fadeout` are `m_dpv` KeyValues, and mixer wrap comes from `smpl`/cue/`flag_force_looping`, not the entity's `m_fLooping` bit.
- **undated** — `m_flSpeechVol` always answers 1.0 as a seam, not a real read. No exported map, NPC template or vdata table authors a `speechvol`-like key.
- **undated** — Named presentation modernization: VtMB/Miles DSP presets are translated into Unreal reverb/submix presets rather than emulated bit-identically.
- **2026-09-06** — Approved moving the player water-step clock off the map actor into the substrate. The only refactor of existing behaviour the footstep proposal makes.
- **2026-09-06** — Approved `SoundLevelDb → radius` as one shared modernization for every future `EmitSound` port (impacts, 4020, weapons), calibrated once by ear. Keeps later ports from re-deriving the mapping per producer.
- **2026-09-06** — Species footstep overrides (VMingXiao, VTzimisceRunner, VHengeyokai, VSabbatLeader, werewolf) are out of scope until those NPC classes are ported. Scope fence, not an omission.
- **2026-09-06** — Named divergence: ground sense probes twice — the floor hit's own material, then a render-surface trace on `ElysiumPickOnly` — and answers `default` when the floor has no drawn half. Converted maps split the collision hull from the drawn `$surfaceprop` geometry, unlike Source where they are one brush.
- **2026-09-06** — Named divergence: `snd_foliage_db_loss` (4 dB per 1200 units) is deliberately not applied. Stock Source gates it behind a foliage trace the port does not have; applying it would cut a level-58 step's reach from ~2860 to ~1570 units.
- **2026-09-06** — Named divergence: the attenuation curve's near field inside `D_ref` is flat (edge value held) instead of retail's rising compressor, and the `D_ref` field term is held at 0.912 vs retail's 0.9997. Bounded under 1 dB over a 28-unit radius at level 58.
- **2026-09-06** — Named divergence: the dry player-step arm always plays instead of reproducing retail's button/speed test. That test is a decompiler artefact (a denormal float from an integer clobber), so the observable behaviour is preserved, not the broken machinery.
- **2026-09-06** — Named divergence: NPC footsteps stay audible during monitor, keypad and hacking-terminal sessions, unlike retail. The retail dialogue/control field has no substrate state yet; the divergence closes when interaction-session state is exposed.
- **2026-09-06** — Trap: the NPC step-sound variation pick draws twice from the RNG (side, then entry) where retail draws once. The audible result matches but the RNG stream position diverges, and a save carries that — a hazard for save/replay determinism work.
- **2026-09-06** — Named divergence: the player hearing stimulus refreshes at the 0.1 s think cadence instead of retail's per-frame `PostThink` rewrite. Judged observably equivalent; landing categories are still latched by the per-frame step clock.

## Animation

- **undated** — Trap: rotation provenance is closed to three named sources — no fixed quarter-turn, per-model facing correction or corrective rest-pose transform exists anywhere in the pipeline or runtime. A visible quarter-turn must be diagnosed as a missing semantic input, never patched with a rotation.
- **undated** — The `_delta` additive family ships raw and is composed at runtime in retail's post-multiply order, not Unreal's pre-multiply. The conjugation depends on which pose the delta lands on, which the clip cannot state, so no bake-time or reference-pose substitute works.
- **undated** — Trap: the exporter still writes the host-composed `<delta>@<host>` form but the bake deliberately never builds it. If one ever lands on the mount it silently wins over the correct resolution path.
- **undated** — Owner-accepted divergence: the blend stack uses nlerp where retail's transitioner slerps. Measured error bounded at a median of 1.6e-6 rad.
- **undated** — Owner-accepted divergence: the stack accumulates oldest-player-first while retail folds newest-previous-first, differing only at blend depth ≥ 3. Intermediate weights differ; endpoints and the settled pose do not.
- **undated** — Owner-accepted divergence: the blend stack caps at 4 concurrent players (the deepest observed in capture) against retail's unbounded insert-and-evict. A 5th request accumulates into a stored pose instead of the pop retail never produces.
- **undated** — Named divergence: the port recomputes every bone every frame instead of retail's mask-selective partial bone refresh. The per-bone refresh mask bits are loader-written and unrecoverable from the installed file.
- **undated** — Named divergence: blend-space interpolation between authored cells uses Unreal's own scheme, not retail's cell-selection rule. Only authored content — cells and ranges — is reproduced faithfully.
- **undated** — Named approximation: the hair-chain proof uses stock `AnimDynamics`, not a port of retail's point/segment solver, and has no ground or spherical collision. A presentation choice to bound acceleration; it cannot be promoted to "faithful" without a game-independent retail replay.
- **undated** — Trap: a stem's hair-dynamics entry is the entire opt-in — there is no code allow-list, so a stem absent from the data asset simulates nothing, silently and without a warning.
- **undated** — Leaving breasts rigid is the current proof's unfinished state, not an approved divergence. The project default is reproduce; the cone-clamp presentation choice still needs an owner call.
- **undated** — Trap: do not copy retail's authored max angle (Jeanette's 90°, Jack's 100°) onto an `AnimDynamics` cone. It is a solver ceiling, not a look target, and produces flop; even the 100° ceiling is never reached in the controlled hair capture.
- **undated** — Held-weapon binding reuses the garment leader-pose mechanism instead of a bespoke weapon-attach system, and `FAnimNode_CopyPoseFromMesh` is explicitly ruled out. Leader-pose already matches retail's evaluate-then-overwrite composition; the copy-pose node has no input link and resets to reference pose.
- **undated** — The `handleclaws` material named `null` (empty texture payload) and `w_f_bushhook.mdl`'s degenerate identity bind are carried through verbatim, not repaired. Both are retail behaviour; a future dev must not mistake either for corrupt data.
- **undated** — Trap: the seven zero-skin-weight prop bones and their clip channels must survive character-bake stripping. Nothing about the geometry states they are needed, so an import optimizer removes them and silently breaks every melee weapon's swing.
- **undated** — LIFE7 stays on absolute-time seek rather than montage until the staging stack is re-proven, and paired actions land in LIFE7's cinematic path, not the reaction channel.
- **undated** — LIFE9 secondary-motion calibration is parked behind the graphics freeze.
- **undated** — LIFE6 scope is ranged-only by owner call; lockpick and Discipline viewmodels are deferred, not designed out.
- **undated** — Melee has no first-person model in retail, measured. Not a bug and not to be "fixed".
- **undated** — No cvar, feature flag or toggle for any landed animation task: a task lands complete and previous behaviour is recoverable only via git.
- **undated** — Named divergence: the retail event-window behaviour (no look-ahead, once-per-lap drop on the ≥ 5000 client band) is reproduced rather than fixed.
- **2026-09-07** — Cast bodies carry a committed base label, and an unowned base channel on an Idle-state cast body republishes that committed clip by exact label instead of resolving `ACT_IDLE`. Reproduces retail's activity resolver keeping `m_nSequence` on a miss; player behaviour is untouched.

## NPC AI & stealth

- **undated** — Owner call: the NPC mind stays a plain C++ state machine and schedule kernel, never Behavior Trees or StateTree; only navigation and locomotion sit behind a motor interface. Schedule identity is script-visible API, interrupts and progress must serialize, and decisions must run headless and deterministic.
- **undated** — The retreat/cover task's destination comes from a navmesh projection plus a re-test against the task's own rule, instead of retail's node-graph lookup. Mechanism differs on purpose; the behaviour, including failing when no route exists, is kept faithful and must not be "fixed" to always succeed.
- **undated** — Trap: relationship-row priority arbitration between competing rows is unresolved at the native level; the port uses highest-priority-wins with insertion-order tiebreak as a marked placeholder. Not confirmed retail behaviour.
- **2026-09-07** — The `TaskFail` refcount leak is ported verbatim, including the un-decremented refcount, with a trace row when the bit was set. The leak is bounded (NAV_JUMP plus discipline-forced schedule) and is not a blocker.
- **2026-09-07** — Modernization boundary for AI: pathfinding may use Unreal NavMesh with the nav-type/failure contract kept, while sensing order, schedule selection and think cadence stay retail verbatim — no AIPerception, no Behavior or State Trees.

## Combat & physics

- **undated** — Trap: the recovered `ChooseMeleeAttackSequence` cast arm is deliberately NOT reproduced; a weighted-draw stand-in runs instead. This is a decision, not an unrecovered gap.
- **undated** — Trap: the 2COMBO substitution is correctly unreachable on the player, verified 43/43 live. Not a bug.
- **undated** — `elysium.PhysicsProps` is the only physics toggle; no second A/B toggle is ever added. Avoids competing feature flags.
- **undated** — The corpse's use/feed/loot interaction volume stays pinned at the death origin as the ragdoll slides away, reproducing a retail quirk.
- **undated** — Asymmetric VtMB joint limits are converted to Unreal's symmetric swing/twist by taking half the range as the limit and rotating the child frame by the midpoint. Any other resolution changes reachable poses.
- **undated** — A ragdoll joint whose six limits are all zero with zero friction is built as a weld, locking all angular axes, not as a zero-width limit. A naive port produces a degenerate constraint instead of a rigid joint.
- **undated** — Grab/carry state lives in a per-player physics-hands service, not on the `weapon_physcannon` item record, though that record must still exist and be granted for inventory and law checks. Avoids reproducing Source's controller-hung-off-a-weapon mechanism.
- **undated** — `physics_prop_ragdoll`, `prop_ragdoll_attached` and `prop_ragdoll_special` register in VtMB but are placed in no map and are deliberately not implemented; a map found placing one is a new task, not an oversight.
- **2026-09-05** — PHYS1 ragdoll is deferred until after R8: simulation is future work and does not block R8, whose physics scope is export/import and data conservation only.
- **2026-09-07** — Trap: mass/size eligibility constants for physics pickup are unrecovered (asm `0x10411160`); the size test must fail loudly rather than invent a constant.
- **2026-09-07** — `player_pickup` / `CPlayerPickupController` are proven dead with zero callers and are not ported.
- **2026-09-07** — Named modernization: `UPhysicsHandleComponent` (Unreal's PD constraint) is the accepted replacement for Source's shadow controller.
- **2026-09-07** — Throw force is derived from the unit conversion, not measured; a visibly-correct arc is the acceptance proxy.

- **undated** — The player pawn's hull is a box, not a capsule: Source's step move depends on a flat-bottom AABB, and a capsule's rounded bottom reports about 0.65 against the 0.7 standable-normal test, so no step would ever be taken. A hard requirement, not a style choice.
- **undated** — Named divergence, off by default: a fixed-step accumulator ships behind `elysium.move.FixedStep` (default 0 = faithful variable delta). Retail has no tick, only a bounded variable frametime.
- **undated** — Trap: root motion must stay disabled on locomotion states, with displacement carried as exporter metadata. Enabling root motion on a locomotion state double-moves the body.
- **undated** — The player is modelled as an entity with the pawn as its disposable body: the pawn carries no `+use` routing, no key polling and no subsystem walk, and verbs are named commands. A future feature that ignores this recreates the old split.

## Disciplines & HUD

- **2026-09-07** — The disciplines selector has no production owner, so the HUD stays invalid rather than fabricate rows.
- **2026-09-07** — The HUD reads only the published view state and discrete notifications, and carries no perception of its own.
- **undated** — Named divergence: Masquerade is drawn permanently on the HUD as five struck marks, where retail carries no Masquerade HUD element at all. A resource whose exhaustion ends the run is kept visible rather than requiring a trip to the sheet.

## Movers & doors

- **undated** — Named divergence: a door's think issues its swept move one frame behind the pawn's move, so retail's push-based mover resolution becomes sweep-plus-reverse here. The mover family's true push behaviour is deferred.
- **undated** — Trap: the swing blocked-latch blocker identity (`+0x98`) is not statically recoverable and requires a live-retail capture.
- **undated** — The async travel state machine is deferred: it gets built when hitches matter, not before.
- **2026-09-07** — The mover-push observable targets the engine-sweep displacement, never a ported `PhysicsPushEntity`.

## World & rendering

- **undated** — Trap: the `.ents` sidecar's `trigger_changelevel`/`info_landmark` graph is VtMB's own; travel is data, not new design.
- **undated** — Trap: `UWorld::PostLoad` marks a loaded map standalone in an editor-built `-game` process, so the flag is cleared immediately before `OpenLevel` to let same-map reloads collect the old world. Packaged builds never acquire the flag, so a reload leak appears in editor-built games only.
- **undated** — Trap: SM6/DX12 is mandatory; a DX11/SM5 fallback is silent and disables Lumen, MegaLights, VSM and hardware ray tracing entirely — roughly 20 vs 170 fps on an RTX 5070. It looks like a generic perf problem and is a renderer-tier fallback.
- **undated** — Trap: forced quality values belong in `[SystemSettings]`, not `DefaultScalability.ini`, because the system-settings priority outranks scalability and game settings. In the wrong ini a forced value is silently overridden.
- **undated** — Trap: enhancement pass order is fixed — delight before super-resolving, and delight before deriving normal or height. Doing the passes in the "obvious" order bakes shading in as fake geometry and resolution.
- **undated** — Water: `%compilewater` is the compiler key that selects the master, not the shader name; a `tools/` `$basetexture` on such a unit is a compiler annotation, not a colour, and `$translucent 1` is consumed by the water lane. Trap for reading VMT text literally.
- **undated** — Water owner decision 3: the `$bumpoffset` scroll is honoured though the 2004 shader ignored it. Authored intent over 2004 result.
- **undated** — Water owner decision 1: `sm_pier_1` stays on the Unofficial Patch recompile, both destroyed leaf annotations are derived rather than read, and no rain gate may key on the precipitation mask for this map. The UP build flags no leaf.
- **undated** — Named divergence: `waterbigsplash_emitter` is read from the retail pack member because the Unofficial Patch copy is an authored-empty stub — the one named key override in an otherwise UP-first corpus. Without it the effect would not resolve at all.
- **undated** — The water surface takes no scene fog (named modernization, ruling H) and the camera-clearance offset is closed-form, dropping retail's one-unit Z quantization (ruling I). Refraction and reflection already carry per-primitive fog, and continuous distance is preferred.
- **undated** — Named modernization: buoyancy is Archimedes over the body's AABB using authored `density`/`damping`, because vphysics' fluid controller is not in the corpus.
- **undated** — Water player movement (`WaterMove`, swim, tread, camera water band) is out of scope by owner call and stays on the substrate tier until a `run play` witness spends the basin.
- **2026-09-04** — The drawn-sheet water alternative (owner decision 2) — an SLW sheet over the nodraw pier water — was withdrawn the next day against the owner's own VtMB frames. It hid the `blackwater` card and shaded as a bare lit plane.
- **2026-09-04** — Owner call: the camera offset's 1-unit quantization is dropped for the exact closed-form distance.
- **2026-09-05** — Ruling F revised: `$forcecheap` water is no volume at all — the cheap program zeroes both coefficients and emits `$fogcolor` directly. The first cut (extinction ×16) rendered an unlit basin black, because scattering needs light.
- **2026-09-05** — Ruling E revised: underside-ness is a property of the face (`normal.z < 0`), not the material, and the earlier "zero extinction underside" is withdrawn — an underside only drops reflection. Retail decided it load-order-dependently on a shared material, which is unreproducible.

## Pipeline & corpus

- **undated** — Trap: the skin-family clamp is `skinFamilies[min(skin, familyCount-1)]`, never family 0. Without it 83 temple guards plus Gary, Ash and the Sabbat henchman draw the wrong body invisibly, and no test map catches it.
- **2026-08-30** — Terminal definitions stay ScriptFS-overlay-first forever while rulebook tables and signs read corpus-only, making the hunter-mode easter-egg variant swap a named unsupported divergence. Faithful support needs overlay-aware reads, cache invalidation and cross-session overlay semantics nobody built.
- **2026-08-30** — The unit contract's `reject_opaque_source` rule is repudiated: an export unit is a source capsule carrying the exact winning source bytes beside the decode, and import reads only `exports_v2`. Reverses an earlier contract rule; every seam adopts the capsule shape as it migrates.
- **2026-08-30** — Trap: the VtMB install had the hunter easter egg triggered (9 vdata base files byte-equal to their hunter twins) and was one-time restored from the vampire twins. Any export taken before this date silently carried hunter data in stats, strings, credits, trait effects, four armor items and signs-death.
- **2026-08-30** — Named divergence: Unreal 5.8 refuses block-compressed DDS on both import paths, so the texture lane decodes BC itself, stages BGRA8 and accepts Oodle re-encode loss, reported as a measured per-unit texel delta. Bit-identity would need a custom texture class outside the DDC; the GLB remains the bit-exact record.
- **2026-08-30** — sRGB is decided from material bindings, and the 590 conflict textures get a `_linear` twin under the same unit id; doubling all 11,243 and a formula-only fix were both rejected. Source filters raw bytes, so a shader-side conversion is only exact on flat regions.
- **2026-08-31** — The matte-world premise (Specular 0, Roughness 1, light specular scale 0) is repudiated: the three zeroes flip and `$envmap`, its masks and `$envmaptint` become the "how shiny" input, never "what is reflected". The premise was a fact about Source's Lambert renderer, not about the surfaces.
- **2026-08-31** — Named divergence: self-illum masters use the plain spelling while retail's masked variant multiplies and squares the base. Both instruction lists were printed and the plain one chosen.
- **2026-08-31** — Provisional owner calls a reader must not "fix" back to literal retail without re-opening: `$alphatest` clips at 0.5; `decalmodulate` goes to Modulate/translucent instead of retail's wireframe fallback; the fixed cube goes to Emissive behind the ray-tracing switch; `$envmapsphere` gets nothing; `$ignorez` re-routes 5 unlit users to the sprite master while the 1 vertex-lit user keeps the lit master; `$detail` is dropped.
- **2026-08-31** — `.ents` is a join, not a projection: the replacement emits one row per lump block in lump order because saves apply entity state by index, the cutover bumps the save version, and datamap retyping is its own named divergence with its own bump. Restore length-gates the output counters and silently drops them on a cardinality change.
- **2026-08-31** — Collision authority for props is the placement record, not the model: 2,530 placements demand VPHYSICS on models with no `.phy` and 3,428 override a present one. A model-driven rule would be wrong on the majority of placements.
- **2026-08-31** — Trap: the legacy model naming fold and material slot naming are load-bearing — four independent C++ call sites recompute the stem live, and skin swaps and map-material binding bind by slot name. Changing the fold anywhere without all four breaks binding silently.
- **2026-09-01** — Permanent named divergence: `.dispcol` publishes float32 positions rather than the packed displacement verts, so drift (max 0.0019 cm) is tolerance-gated at 0.01 cm. Do not chase a bit-exactness the seam cannot supply.
- **2026-09-01** — Trap: the shots-diff harness noise floor exceeds its signal — an identical build on an identical level can differ on up to 94% of pixels — so it is a boot/appear witness only and a percentage is never proof of regression.
- **2026-09-02** — Detail-prop sway sits behind a static switch defaulting off, and the 12.7 cm amplitude is an owner stand-in borrowed from Source. VtMB's client never reads `swayAmount` as a sine driver, so zero amplitude is the faithful value.
- **2026-09-02** — `func_areaportalwindow` deliberately gets no cull or fade range, because its distances govern the unexported backing brush and the background model is glass VtMB draws at all distances. Culling would invert retail; the faithful fix is parked.
- **2026-09-03** — Owner call A: the 38 `decalmodulate` units draw as translucent decals under DBuffer where retail falls back to wireframe; turning off DBuffer to restore true Modulate is rejected unless the owner asks. An impact hole is a translucent stain.
- **2026-09-03** — Owner call B: one decal subsystem owns every decal (adopt-and-fog, lay, pool and cap, save records), and the 2048 cap is a stated stand-in, not retail's `r_decals` value. Deliberately breaks the park-the-seam-for-its-first-caller pattern.
- **2026-09-03** — The ranged-shot decal trace runs on the `ElysiumPick` channel, never visibility. Only baked render geometry carries the face's physical-material class; a visibility trace stops on material-less player clip and stains everything as concrete.
- **2026-09-03** — Trap: `Angles` is Source QAngles while `Origin` is Unreal world space, so a raw rotator-to-vector expression mirrors pitch and yaw. The shot-impact trace was fixed to the conversion helper, but the melee-opponent and discipline-acquire paths still carry the raw expression, latent for the first non-zero-yaw caller.
- **2026-09-04** — The `@host` derived-clip fan-out is kept verbatim for R8 even though it triples bank-clip count, and composing the overlay in the anim graph instead is recorded as a post-R8 modernization candidate. "Poses are baked native" is a hard constraint this milestone.
- **2026-09-05** — R8 closure is scoped to matching the old lane's coverage, not fidelity; only critical migration defects block retirement, and a second transport is not retained to finish parity comparisons. Supersedes the stricter gates written in the R8 plan.
- **2026-09-06** — Named modernization: retail `SetModel` synchronously precaches an undeclared model, and the port answers a miss with async admission, replaying live entities once admitted. The synchronous read is forbidden.
- **2026-09-06** — Trap: every runtime skeletal construction — NPC and player bodies, wield, garments, preview, stage — must stamp the lightstyle-brightness CPD slot or it renders fully black. Only bake-placed props and brushes stamped it.
- **undated** — Trap: surface-property inheritance is flattened at export — a child pool replaces the parent's, `default` is not an implicit parent, and the whole chain is hashed. Editing one parent entry re-keys every descendant.
- **undated** — Trap: `SurfaceClassIndex` is the row's `Index` field, never its array position. Reordering the pinned surface list silently reassigns every surface's shine and sound class.
- **undated** — Collision auto-detection of boxes, spheres and capsules is forced off for baked meshes after it approximated 96 of 283 convex ledges. Re-enabling it breaks ledge collision without any warning.
- **undated** — Trap: the `.ents` join emits one row per lump block in lump order because saves apply entity state by index. Any resort or dedupe of that file silently corrupts every save.
- **undated** — `light_dynamic` brightness is a fitted convention, not a recovered VRAD row. It reads as recovered in the exporter and is not; do not cite it as retail.

## Save & runtime

- **undated** — Retail `.sav` import is an explicit non-goal; only the *shape* of what a save holds is reproduced, not the container.
- **undated** — Decal `Normal` is recorded in the save format though VtMB's own decal list does not carry it. Source re-traces geometry at restore; recording costs 12 bytes and avoids a stain landing on moved geometry.
- **undated** — `FTimerManager` is banned for game-visible time: everything delayed goes through the one event queue so it serializes with think times. Reaching for a timer is the wrong reflex here.
- **undated** — Named divergence: the event queue enforces a 10,000-delivery service cap per pass, deferring any due tail across a think boundary. Retail drains unbounded and can hang the frame on a zero-delay cycle.
- **undated** — Post-movement trigger containment diffs are applied deterministically (all old-ends, then all new-begins, in stable entity order). A port rule invented to make an otherwise-unordered retail operation reproducible, not a recovered retail fact.
- **undated** — Trap: retail is move-first at the top level and think-first inside the game frame — two different orderings nested — and an output fired during a think is serviced after all thinks that frame, not interleaved. Both must be matched unless a determinism reason argues otherwise.
- **undated** — Trap: the movement component takes its delta from the pending command's own delta rather than the tick's. Identical today with one command per frame, and silently not identical the moment a frame carries more or fewer commands (replay, hitch clamp, fixed step).
- **undated** — Trap: pause has exactly one owner, the game-flow subsystem; the pause menu's input scope is pushed *because* the flow paused, never the reverse. A second pause writer would race.
- **undated** — The theatre act (`logic_choreographed_scene`) blocks the theatre milestone in full, eyes and lipsync included; `elysium.SkipIntro` is the dev shortcut until then.
- **undated** — Named divergence: pure-visual non-addressable entities (lights, coronas, decals, static props) ship through typed sidecars rather than as spawned entity-substrate records. Accepted divergence from the recovered entity-visuals model.

## UI

- **undated** — Named divergence: the passive notification card is a full presentation divergence from retail's info bar — no retail icon or sound, and it posts even for otherwise-hidden items.
- **undated** — Quest unread markers are cleared on leaving a hub, per hub. VtMB sets the byte and never clears it, so this clear rule is unrecoverable and invented; otherwise the marker would say "updated" forever.
- **undated** — Trap: the MCP server auto-starts only because its define is set for the Editor target; it compiles to an empty shell in Game, Shipping and Test. A reader could assume the server is live in a packaged build.
- **undated** — Owner-called presentation divergence: the terminal character-cell screen renders as a modern UI surface projected over the recovered model-screen geometry, instead of a rasterized 512×512 model texture. The 36×24 grid semantics, commands and server authority must still reproduce exactly.
- **undated** — CommonUI's own input-mode writer is deliberately starved so the scope stack stays the sole mode authority. A fourth mode-owner exists inside the engine and must be kept silent.
- **2026-09-02** — Named divergence: the use-icon ring drops VtMB's composited atlas in favour of 72 individual brush textures.
- **2026-09-02** — Named divergence: the menu seal draws the sheet's clan sigil instead of VtMB's particle-scene sprite, and the sign background is the panel's fixed inner plate with the parsed rect never applied. No lane publishes loose particle TGAs.
- **2026-09-06** — Computer terminals target keyboard only: no gamepad action palette and no virtual text entry. VtMB is PC-only and has no gamepad implementation to reproduce.
- **2026-09-07** — Pawn pinning during a terminal session is reproduced verbatim (immobilize plus a per-tick capsule sweep holding the pawn at the monitor), not replaced with a seam.
- **2026-09-07** — A terminal camera shot that fails to resolve is warned once by name and the session continues cameraless, matching retail's null path.

## Input

- **undated** — Reserved dev keys (backtick, F7) must never appear in a default bind, enforced by a registry and an automation test rather than convention. An action added on a reserved key should fail tests rather than silently shadow the debug menu.
- **undated** — The one-frame `vhotkey` deferral is deliberately not reproduced on gamepad. It is the defect the community's `wait 1` idiom works around, not a behaviour worth keeping.
- **undated** — `+duck` is a toggle on gamepad and a hold on keyboard. Crouch and Obfuscate are held for minutes, which is unplayable as a pad hold.
- **undated** — Named feel divergence: Enhanced Input's smoothing modifier ships on mouse look against VtMB's unsmoothed linear response; legacy smoothing stays off.
- **undated** — `config.cfg` gamepad rows are excluded from the config-writer projection. A modern pad binding has no VtMB keyname, and emitting one would produce a file VtMB could never write.

## Effects

- **undated** — Menu and HUD are new authored assets, not VtMB reproductions: the menu particle scene, the HUD particle entity, the HUD sprite families and screen-space attach modes 5/7/14/16 are all out of scope.
- **undated** — A light an emitter never had is a divergence needing an explicit owner call, and dropping a light VtMB does throw is equally a divergence; check the owning topic before deciding. Easy mistake in either direction.
- **undated** — Named modernization: `env_shooter` gibs bounce on their own hull instead of VtMB's zero-size point.
- **undated** — Named modernization: particle-emitted dynamic light via the Light Renderer, since VtMB fire and lightning carry no dynamic light except the explosion params' own keys. Risk of over-attributing a light to retail.
- **undated** — Trap: AlphaComposite drops opacity-only fades to white — fade emissive and opacity together or the card blows out — and the decoded VtMB sprite atlas must stay premultiplied-on-black. Unpremultiplying looks more "correct" and is wrong here.
- **undated** — Trap: Niagara collision events require CPU simulation and persistent IDs, so the rain fall layer's colliding portion is pinned to CPU. Moving it to GPU as a perf "fix" silently breaks the events.
- **undated** — Trap: the Niagara sprite vertex factory hardcodes vertex colour to white, so any sprite master tinting through a VertexColor node silently discards every authored ramp; read particle colour instead. Root cause of both the "black card" and "smoke reads as dark blob" defects.
- **undated** — Rule: never generate Niagara module scripts, scratch pads or custom HLSL bodies — author them once by hand, review as code, version them; the MCP toolset is for inspection and one-off tweaks only. Used at scale it silently leaves systems non-activatable.
- **2026-09-03** — The 20-slot generic-floor particle system was retired: it never compiled outside the Niagara editor, per-instance parent/child readers are impossible by engine design, the fixed slot layout dropped leaves, and the blob was unreadable. Replaced by a generated per-root system.
- **2026-09-03** — Owner call: a hand-authored Niagara module library plus one generated system per root, inherited from base emitters. Runtime-built systems are impossible in a packaged game and CPU simulation is mandatory for the VtMB particle corpus.
- **2026-09-03** — Owner call: Niagara effects are authored one archetype at a time, and nothing scales to the rest of the corpus until a human has looked at the contact-sheet result.

## Open owner calls

- **open** — The 60° living lens (`m_iFOV == 0` giving 60 against a default FOV of 75) wants the owner's eye before it is settled.
- **open** — Programme priority for audio: the audio programme is unstarted and blocks the theatre and fight beats.
- **open** — Mover close/loop semantics diverge from retail — the port plays close at motion start and owns a loop, where retail emits close only at door-hit-bottom with no loop — to be adjudicated against the audio programme.
- **open** — Camera coupling for the cinematic life path: legacy shot stack versus the camera service's Sequence kind.
- **open** — Whether the corpse interaction volume should follow the pelvis instead of staying pinned at the death origin; a Feel divergence needing an explicit call.
- **open** — Discipline-magnitude authoring: retail per-discipline values versus patch-uniform scaling; retail is the default until called.
- **open** — Attach mode 3 (`BoneTreeWithColors`) per-segment tint source is not decoded beyond the name.
- **open** — Whether breast dynamics ship at all, and the cone-clamp presentation choice if they do.
