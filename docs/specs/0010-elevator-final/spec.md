# 0010 elevator-final — the elevator, the final fight, the last Jack conversation and the transition to sm_hub_1

## Witness
`sp_tutorial_1`'s closing beat, played start to finish: the player rides the tutorial elevator
(`func_elevator` + `prop_button`, landed), fights through the full encounter using only combat,
discipline and stealth behaviour already ported under 0005–0009 (this spec adds no combat
requirement), has the final conversation with Jack, and walks through the exit trigger into
`sm_hub_1` with quest/inventory/save state carried over. Proven as an integration run — no new
automation is owned here beyond what already exercises the elevator/door movers (4.8/4.12), the
9.3 script-call surface, and the 10.6 input path; the map load itself is 4.6's landed
`trigger_changelevel` + landmark travel. *Slice acceptance carried from the roadmap:* "the
tutorial elevator chain works; walking out loads `sm_pawnshop_1`" (P4) generalizes here to the
`sm_hub_1` walk-out at the end of `sp_tutorial_1`.

## Scope
- Roadmap rows absorbed: 4.8 (Rotating/linear/elevator family — remaining), 4.12 (Door
  faithfulness gaps — remaining), 9.3 (Level-script execution — delegated fills), 10.6 (Input
  path — gameplay pad remainder).
- Out of scope (belongs to another spec or is parked):
  - The fight itself: stealth, first kill, disciplines, distraction — specs 0005–0009. This spec
    integrates their output; it states no new combat requirement.
  - The Jack conversation content and the dialogue UI/runtime (`.dlg` parsing, `GetStartingLine`,
    conversation presentation) — 9.1/9.2, owned by the dialogue spec, not
    assigned here.
  - `trigger_changelevel` + landmark travel mechanism itself — roadmap 4.6, already landed `[x]`,
    no open plan text; consumed here as the transition primitive.
  - 10.4 Async travel state machine — explicitly excluded from this assignment; triggers "when
    hitches matter," not before. 10.5 Packaged-build content path — unrelated to this beat.
  - 4.10 (`game_sign`/`prop_sign`), 4.11 (trigger/`+use`-prop I/O gaps other than doors), 9.8
    (inventory & items), 9.9 (NPC disposition & reactions), 9.10 (Economy) — other `gameplay.md`
    sections, not assigned to this spec; 9.3's delegated fills reference them but do not restate
    their requirements.
  - Remapping screen (10.6g) — lands with 8.10/8.6, not here.

## Requirements
Movers (4.8):
1. `func_elevator` and `prop_button` are landed and are what the witness rides.
2. `func_rotating` (spin-up/down, hurt-touch), `func_movelinear`, and keyframed movers are open —
   still required for the map's other movers, not the tutorial elevator itself.
3. **Mover-push observable** (not the retail mechanism): movers are `MOVETYPE_PUSH` in retail and
   displace what they touch from their own side. `FElysiumMoverBase` sweeps instead; Chaos
   resolving that sweep already shoves the pawn clear of a closing door's arc (measured, nothing
   tunnels) but `dmg` is not dealt and `OnBlockedClosing` does not fire unless the sweep is fully
   blocked. Required: a blocked closing door deals `dmg`, fires `OnBlockedClosing`, and displaces
   the pawn from its own side, reproduced through the engine's sweep/displacement response (S12),
   never a ported `PhysicsPushEntity`. *Deps:* 4.1.

Doors (4.12) — the doorknob lock authority (a knob owns its own lock; the door consults its
nearest one via `IsUseRefused`), silent `Open`, the unconditional `+use`
`ResolveToggleStateFromTransform` resync, and `OpenAwayFromEntity`/`bResolveSwing`/
`SF_DOOR_ONEWAY` are landed (`docs/vtmb/animation_and_movers.md` B, `docs/vtmb/entity_io.md`).
Remaining, recovered-to-retail:
4. **`CBaseDoor::Use` guard chain.** `+use` still routes through `InputToggle`, not retail's
   ordered chain: `noopenwanted` refusal, the mid-motion self-heal step, and the
   `{AT_BOTTOM, AT_TOP} ∪ NO_AUTO_RETURN` admission set (a `GOING_*` non-return door is a silent
   no-op in retail, a reverse here).
5. **`MoveDone` arrival binding.** Retail dispatches on a move-start arrival callback; Elysium
   dispatches on the mutable `ToggleState`, so a locked door caught mid-close by a resync can drop
   a single `OnFullyClosed`. Fix: make arrival independent of the live state.
6. **Input-level outputs and admission.** Decompiled (`FUN_100f0170`/`FUN_100f00a0`/
   `FUN_100f0210`): retail fires `OnOpen` at the `Open` input and again in `DoorGoUp`; `Open`/
   `Toggle` gate on `IsUseRefused`; `Close` carries no lock test; admission is `!= AT_TOP` /
   `!= AT_BOTTOM` (each also runs from the opposite in-flight state); `Toggle` reaches the motion
   helpers directly so only `Open`/`Close` double-fire. → `animation_and_movers.md` B.4.
7. **Mover sound emission points.** Retail emits `close` at arrival (`DoorHitBottom`), not motion
   start, starts no loop and stops none (`swing` is one event; any looping is the soundgroup's).
   Elysium plays `close` at motion start and owns the loop in code. Owner-adjudicated against
   AUD2. → `animation_and_movers.md` B.4.3.
8. **The blocked family.** Retail's `Blocked` damages in both directions, self-reverses only
   behind `CRotDoor`'s re-entrancy byte (a plain `func_door` never reverses), and synchronises its
   targetname group to its own direction rather than reversing it; `StartBlocked`/`EndBlocked`
   carry `OnBlocked*`/`OnUnblocked*` with different activators; `CRotDoor::Blocked` adds a stuck
   detector. → `animation_and_movers.md` B.4.4. The any-entity blocker filter is NPC-pending
   (P13, and the 10.7 door-obstruction reaction). A pure rotation is never swept
   (`UPrimitiveComponent::MoveComponentImpl` skips the sweep on zero translation), so
   `OnMoveBlocked` is unreachable for a rotating leaf until a rotating-sweep mechanism exists.
9. **Swing blocked-latch inversion** — held on a live-retail capture. The flip and its arming
   predicate are recovered; the blocker's `+0x98` identity is not statically recoverable
   (zero-initialised in `CBaseEntity`'s constructor, no setter/accessor in the image; the adjacent
   `+0x94` is the `CAI_BaseNPC` pointer, narrowing `+0x98` to the same cached derived-type family
   without naming it). Capture which blocker classes set it.
   (Closed: the mid-motion self-heal reissued-move arguments — `0x100efdc0`/`0x100efddf` show
   `DoorGoDown(1)` and `DoorGoUp(1,1)`; both reissues propagate to the linked leaf and the open one
   re-resolves its swing.)
10. **Runtime visible rotation.** A live session showed a baked `func_door_rotating` firing
    `OnOpen` and the swing sound while its body did not visibly rotate; state/collision cycle is
    correct, so this is a map/visual-body concern (whether the baked brush body follows the mover
    transform — confirm 4.3's brush-travel holds for a rotating leaf at runtime). →
    `docs/vtmb/animation_and_movers.md` B. *Deps:* 4.1, 4.3.

Level-script execution (9.3):
11. Core landed (B2, 9.3b, 9.3c). Character-method fill is delegated to backing systems:
    inventory natives are 9.8's, feats/stats were 9.4's (landed), disposition/camera/barter arrive
    with their domains. `SquadSeesPlayer` stays a stub — no shipped caller. Remaining is blocked
    on the inventory follow-up (9.8, out of scope here).

Input path (10.6), gameplay pad remainder — Enhanced Input is the driver, the VtMB console
command string stays the action's identity; one `UInputAction` per `kb_act.lst` command;
`UPlayerMappableKeySettings.Name` a stable id; the command executed through `FElysiumConsole` on
`Started`/`Completed`; `EPlayerMappableKeySlot` First/Second/Third = Key/Alternate/Gamepad. The
gameplay pad layout and generated Xbox/DualSense glyph switching are landed. Remaining sub-steps:
12. **b (rest).** `SetAnalogUp`, keyboard movement and remaining keyboard/mouse binds onto
    Enhanced Input; every button-pair action binds `ETriggerEvent::Canceled` alongside
    `Completed` (a Hold/Tap released early otherwise leaves the button latched).
13. **c. Reserved keys.** Console on `` ` `` plus `F7`, Cog shell shortcuts on `Ctrl+F1`–`Ctrl+F4`,
    dev keys on `BindDebugKey`; enforced by a Substrate-tier test over every generated IMC;
    `elysium.input.ReserveDebugKeys 0` to A/B in dev builds.
14. **e (rest).** Additional DS4/Edge `FGameInputDeviceConfiguration` and glyph entries; LB hold →
    quickbar radial; melee stick quantised to four directions on a combat deadzone;
    adaptive triggers/haptics deferred; `GameInputRedist.msi` joins 10.5's packaging story.
15. **f. `UElysiumInputUserSettings` + `config.cfg` projection.** The key profile is authoritative;
    `FElysiumConfigWriter` emits Valve-format text so `vamputil.py`'s `FixKeyBindings` reads a
    faithful view (imported once on first run; slot Third excluded). Not write-only:
    `FixKeyBindings` issues `bind <KEY> "vm_discipline"`, so a runtime `bind` must resolve through
    `MapPlayerKey` instead of being dropped, or the patch's discipline/feed re-routing silently
    dies off default keys. Declare `execonsole`, `player_immobilize`, `player_mobilize`. Rebinding
    works headlessly before any UI exists.
16. **Acceptance (10.6):** the tutorial is playable start to finish on keyboard+mouse and on an
    Xbox *and* a DualSense pad with no third-party driver; every action rebindable to
    primary/alternate/gamepad and surviving a restart; the reserved-key test green. Defaults are
    the Patch 11.5 set. *Deps:* 11.5, 11.6; 8.6/8.10 for the screen only. Physical Xbox acceptance
    remains open.

## Design
No new subsystem shape is introduced by this spec: it is the join point where the landed
`FElysiumMoverBase` mover family, the ported `CBaseDoor` state machine, the 9.3 script-call
surface and the Enhanced Input action catalog must all already be correct for one continuous
played beat to hold. Fixes above are localized to the mover/door C++ (`animation_and_movers.md`
B/B.4/B.4.3/B.4.4) and to the Enhanced Input asset/action wiring; the code is the architecture.

## Seams
- Consumes:
  - 0005–0009 — the combat, stealth, discipline and distraction behaviour exercised during the
    fight; this spec adds nothing new to that surface.
  - Dialogue spec (owns 9.1/9.2) — the last Jack conversation itself; spec
    number unknown from this assignment's inputs.
  - 4.6 (landed) — `trigger_changelevel` + landmark travel, the mechanism that performs the
    `sm_hub_1` transition and state carry-over.
  - 9.4, 9.8, 9.9 (feats/stats landed; inventory; disposition) — backing systems the 9.3
    delegated character-method fill calls into.
  - 11.5, 11.6 — input defaults and dependencies for 10.6's acceptance.
- Provides:
  - The mover-push observable and door faithfulness fixes (4.8/4.12) that the elevator/door beats
    in this witness exercise.
  - The 9.3 delegated-fill inventory hookup once 9.8 lands.
  - The 10.6 gameplay-pad input remainder needed to play the whole beat on keyboard+mouse and
    gamepad.
  - The proven `sp_tutorial_1` → `sm_hub_1` integration witness for later specs to build on.

## Tasks
- [x] `func_elevator` + `prop_button` (4.8)
- [ ] `func_rotating` — spin-up/down, hurt-touch (4.8)
- [ ] `func_movelinear`, keyframed movers (4.8)
- [ ] Mover-push observable: `dmg` on blocked close, `OnBlockedClosing`, own-side displacement
      via engine sweep/displacement (4.8)
- [x] Doorknob lock authority, silent `Open`, `+use` resync, `OpenAwayFromEntity` swing gates
      (4.12, landed)
- [ ] `CBaseDoor::Use` guard chain: `noopenwanted`, mid-motion self-heal, admission set (4.12)
- [ ] `MoveDone` arrival binding independent of live `ToggleState` (4.12)
- [ ] Input-level outputs/admission per `FUN_100f0170`/`FUN_100f00a0`/`FUN_100f0210` (4.12)
- [ ] Mover sound emission at arrival, no code-owned loop (4.12, owner-adjudicated vs AUD2)
- [ ] Blocked family: bidirectional damage, `CRotDoor` re-entrancy self-reverse, group sync,
      `StartBlocked`/`EndBlocked` edges, stuck detector (4.12)
- [x] Mid-motion self-heal reissue arguments (`DoorGoDown(1)`, `DoorGoUp(1,1)`) (4.12, closed)
- [ ] Swing blocked-latch inversion — blocker `+0x98` identity, held on live capture (4.12)
- [ ] Runtime visible rotation on baked `func_door_rotating` — investigate brush-travel (4.12)
- [x] 9.3 core: B2, 9.3b, 9.3c
- [ ] 9.3 delegated fills: inventory natives (blocked on 9.8), disposition/camera/barter with
      their domains; `SquadSeesPlayer` stays a stub
- [x] 10.6 gameplay pad layout + generated Xbox/DualSense glyph switching
- [ ] 10.6b (rest): `SetAnalogUp`, keyboard movement, remaining binds, `Canceled` trigger on
      button-pair actions
- [ ] 10.6c: reserved keys + Substrate-tier IMC test + `ReserveDebugKeys` cvar
- [ ] 10.6e (rest): DS4/Edge config + glyphs, LB quickbar radial, melee-stick quantisation,
      `GameInputRedist.msi`
- [ ] 10.6f: `UElysiumInputUserSettings` + `config.cfg` projection, `FixKeyBindings` round trip,
      `bind` → `MapPlayerKey`
- [ ] 10.6 acceptance: full tutorial playable keyboard+mouse, Xbox, DualSense; rebinding persists;
      reserved-key test green

## Open questions
- Which blocker classes set `CBaseEntity+0x98` (the swing blocked-latch identity) — needs a
  live-retail capture; not statically recoverable.
- Whether 4.3's brush-travel holds for a rotating leaf at runtime (the observed non-rotating baked
  `func_door_rotating` body).
- Physical Xbox pad acceptance for 10.6 remains open (DualSense acceptance status not stated
  beyond "open" in the plan).
- Exact spec number owning the last Jack conversation content/UI — unspecified by this
  assignment's inputs; recorded as "unknown" in Seams.
- Mover sound emission point (close-at-arrival vs close-at-motion-start, code-owned loop vs
  soundgroup-owned) is flagged owner-adjudicated against AUD2 but not yet decided.
