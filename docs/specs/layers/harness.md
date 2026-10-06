# The test instrument — shared by every layer

Every layer's records need to observe and drive entities with no NPC involved. A planner consolidated
the 400-odd harness requests of L0 and L1's 191 records into four general pieces; L0 builds them
once (its story 0), and every later story only adds its own emit sites, fields and allowlist entries in
the same slice as the code it ports. The rules below keep the records testing retail behaviour, not
port mechanisms.

| piece | kind | what | where | records using it (L0+L1) |
|---|---|---|---|---|
| `entity_call` | action | Fields: target (entity name, world service, or utility), function (allowlisted retail operation), args (typed values, vectors, handles, and optional fixture references). Invoke once at the scheduled time; preserve its return value and call order. Covers direct entity/world calls, keyvalue lex/parse/access, sound and animation selectors, physics and trigger operations, save helpers, and query calls | `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp::ReadAction/ReadActionName; Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.cpp::FireDueAction` | 128 |
| `entity_field` | probe | Fields: who, field (named field path), optional to/index/member selectors, and one typed comparison. Read a named scalar, boolean, string, handle, vector component, indexed table member, or list word at the requested time/fence; no state mutation. Add field adapters only for retail values the current witnesses do not expose. | `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp::ReadProbe/ReadProbeName; Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.cpp::ReadProbe/ReadW` | 178 |
| `retail_site` | tap | One trace kind for the packet's distinct tap tags. Emit at the port implementation point corresponding to the tagged retail function/branch and its original vampire.dll VA, at the semantic event (entry, branch, write, callback, or return) that the record measures. Text: `tag=<tag> fn=<retail symbol> va=0x<retail VA> phase=<phase> <ordered typed payload>`; preserve same-time order and include handl | `Source/ElysiumUE/Public/ElysiumEntityWorld.h::EmitAiTrace; Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp::GTraceKinds; Source/ElysiumUE/Private/Debug/` | 102 |
| `typed_fixture_catalog` | host | Add a typed `fixtures` catalog to scenario staging. Each fixture has an id, kind, and typed configuration; stage it before the scenario's observation window and let `entity_call` resolve its handle. Cover controlled KeyValues/sound tables, datamap fields and save bytes/blocks, callbacks and registries, raw entity text, PVS/AI nodes, physics bodies/fluids/surfaces/ropes, event queues and inventory, | `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp::ParseText/ReadRow; Source/ElysiumUE/Private/Debug/ElysiumArenaStage.cpp::BuildRow/ConfigureHost; Source` | 103 |

## Rules for records built on it

- `entity_call` drives only what retail itself exposes to the world: an input, `Use`, a touch, a
  think, spawn and activate, damage through the damage entry. Never an internal helper: a record that
  calls a helper directly tests the port's structure, not the game.
- `retail_site` events are emitted at the port line that carries the retail address they name, and
  their text states the retail values at that point (no port-only state).
- `entity_field` reads retail fields by their retail name (`m_...`, the datamap name or the ledger's
  field name), so a probe survives the port renaming its members.
- A record states what retail does — setup, action, observable outcome and its timing — and cites
  the retail address that defines the outcome.

## Story 0 of L0: build it

One story, three slices, before any other L0 story:

1. the record reader and runner for `fixtures`, `entity_call` (with an empty allowlist) and
   `entity_field`, with the harness's own self-test records (a fixture that must stage, a call the
   allowlist refuses, a field probe that must fail);
2. the `retail_site` trace kind in `GTraceKinds`, its reader and its `expect` / `never` matching on the
   site tag, with a self-test record;
3. `Arena/README.md` documented for the four pieces and the rules above.

Later stories add their sites and allowlist entries in their own slices.
