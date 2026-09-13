# NPC AI — The NPC AI oracle

The recovered retail facts about VtMB's NPC AI, one file per kernel subsystem. The
tables under [`../npc-kernel/`](../npc-kernel/README.md) are the generated ledger of the
same kernel (every class, slot, field and reachable function); the prose here is what
has been *walked*. `../npc-kernel/index.md` maps an address to the section that walks it.

| File | Holds |
|---|---|
| [`population.md`](./population.md) | The authored population |
| [`lifecycle.md`](./lifecycle.md) | Lifecycle: spawn, activation, the think |
| [`senses.md`](./senses.md) | Senses, sound and memory |
| [`conditions-and-states.md`](./conditions-and-states.md) | Conditions and states |
| [`schedule-kernel.md`](./schedule-kernel.md) | The schedule kernel |
| [`programs.md`](./programs.md) | The programs |
| [`social.md`](./social.md) | Relationships, enemies, squads and reactions |
| [`authored-control.md`](./authored-control.md) | Authored control: outputs, scripts, keyfields |
| [`rebuild.md`](./rebuild.md) | What the rebuild needs |

Conventions for new sections: a walked retail function is a `###` whose header carries
the address (`### \`MaintainSchedule\` \`0x102817c0\``); provenance is a one-line
`_Recovered YYYY-MM-DD, story N._` under the header, never in it; every section ends with
`**Unrecovered:**` (or `nothing`). Specs cite `§ "<header>"`; the header text is the key.

_Split from `npc-ai-reverse-engineering.md` on 2026-09-13; the sections below were moved verbatim._

## Scope and confidence

This report reconstructs the general non-player-character AI surface of the user's installed
*Vampire: The Masquerade - Bloodlines* build. It connects four bodies of evidence:

- the entity dictionaries exported from the 22 maps currently present in the corpus;
- the installed `vdata/system` tables and level Python/dialogue scripts;
- static recovery from the pinned retail `vampire.dll`; and
- the repository's existing native schedule, task, translation, and script-action surveys.

The installed corpus includes Unofficial Patch/Wesp additions. Counts and authored examples in
this report therefore describe that installed build, not a claim about pristine 1.2 retail.
Generated exports, binaries, decompilation, and survey artifacts remain external evidence and are
not repository content.

Confidence is high for class names, authored keyvalues and outputs, script calls, native datamap
fields, relationship token and priority decoding, the AI update loop, enemy eligibility and
ranking, schedule-gated enemy replacement, the state-switch cases, ordinary humanoid melee/ranged
selection, damage-condition generation, death admission, schedule/task registrations, and the
`aiscripted_schedule` execution modes. Confidence is lower for the exact meaning of several
numeric map keyfields, the human-readable distinction between the two move and two follow modes of
`aiscripted_schedule`, class-specific incapacitation policy, and frame-exact presentation during a
live aggression incident. Those open points need controlled retail capture rather than naming
inference.

This report owns the cross-system NPC-AI reconstruction. Detailed animation selection and task
translation remain in [animation_and_movers.md](../animation_and_movers.md); damage calculation and
health commit remain in [combat-and-damage.md](../combat-and-damage.md); the generic I/O and scripted
sequence contracts remain in [entity_io.md](../entity_io.md); disposition-driven gaze and blinking
remain in [facial_animation.md](../facial_animation.md); and the complete recovered Python action
surface remains in [script_api.md](../script_api.md).

## Executive reconstruction

VtMB does not define an NPC in one place. A spawned NPC is the product of several layers:

1. A map entity chooses a native classname, model, stat template, equipment, squad, initial
   relationship, perception parameters, investigation policy, ambient groups, and I/O outputs.
2. `vdata/system/npctemplate*.txt` supplies the RPG/stat sheet and inherited character defaults.
3. The native DLL supplies the class hierarchy, senses, enemy memory, relationship tables,
   conditions, state machine, schedule selector, task executors, navigation, combat actions, and
   animation-activity policy.
4. Map outputs and Python add authored consequences: quest flags, dialogue gates, stealth failure,
   tutorial progression, cinematic control, and explicit changes to relationship or schedule.
5. The model resolves the selected activity through class and weapon translation into a weighted
   animation sequence.

The central native loop is:

```text
stimulus
  -> senses, damage memory, relationship query, and scripted conditions
  -> gathered conditions and enemy memory
  -> current and ideal NPC state
  -> class-specific schedule selection
  -> schedule task execution
  -> navigation, motor, weapon, and animation activity
  -> entity outputs, sound emission, Python, dialogue, and quest side effects
  -> next AI update
```

"Aggression" is consequently not one callback. Seeing or hearing a hostile actor, taking damage,
being assigned an enemy, crossing a criminal/supernatural threshold, or receiving a script input
can establish hostility. That stimulus updates memory and conditions; may fire outputs such as
`OnFoundPlayer`, `OnFoundEnemy`, `OnHearCombat`, or `OnDamaged`; may interrupt the current schedule;
causes an idle/alert/combat state decision; and finally selects tasks such as face, pursue, take
cover, flee, cower, equip, or attack. Authored I/O and Python then attach story consequences to the
same incident.

Three similarly named systems must remain separate:

- `SetRelationship` writes the native combat-AI relation table (`D_HT`, `D_FR`, `D_LI`, `D_NU`).
- `SetDisposition` changes the character's emotional/dialogue presentation and associated stance,
  expression, gaze, and fidget policy.
- `reaction.txt` and `reactions000.txt` define an RPG/social reaction score and modifiers.

Conflating those three would make combat hostility, conversational mood, and social-stat outcomes
incorrectly drive one another.

## Evidence set and reproducibility

### Installed data surveyed

The current export root contains entity exports for 22 maps. The survey counts direct `npc_*`
placements and one requested child-class definition for every `npc_maker`; it does not multiply a
maker by the number of children it may produce at runtime. The script corpus contains 36
survey-visible Python files, 147 dialogue files with
50,393 rows, and 22 entity exports with 1,635 output payloads that execute Python. The installation
also contains 36 `npctemplate*.txt` files with 150 template declarations.

The pinned native module used by the recovery is:

| Property | Value |
|---|---:|
| Module | `vampire.dll` |
| Size | 7,860,281 bytes |
| SHA-256 | `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f` |

The principal reproducible repository surveys are:

```powershell
uv run elysium research native_schedule_survey
uv run elysium research npc_task_override_survey
uv run elysium research npc_translation_survey
uv run elysium research action_animation_survey
uv run elysium research script_api_survey --json E:\elysium-work\research\npc_ai_script_api.json
uv run elysium research tutorial_npc_bootstrap --json E:\elysium-work\research\tutorial-npc-bootstrap.json
```

The JSON path above is an external work artifact. Exact paths to the user's game and generated
corpus are supplied by local environment configuration and are intentionally absent from this
document.

### Evidence limitations

This is a static and corpus-correlated reconstruction. No new live retail aggression capture was
performed for this survey. Native code proves the control structures, ordering and thresholds,
and authored data proves what maps request, but static recovery alone does not prove the rendered
pose, blend, impulse or precise frame on which two externally observed outputs appear. Numeric
fields whose consumers have not been decoded are reported as fields and distributions, not given
invented semantics.
