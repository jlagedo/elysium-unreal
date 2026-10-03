# Brief S7 — retail's one-NPC trace, whole chain (`ai_debug_npc` readers, `ent_trace_conditions`)

Read `CLAUDE.md`. The owner approved building debugging tools; the rule is that they reproduce
RETAIL's own debug facilities, not new ones. Brief S5 stood the `ai_debug_npc` handle
(`FElysiumEntityWorld::AiDebugNpc`, verb `elysium.ai_debug_npc`) and the hint validators' reason
strings under it. Extend it to the rest of what retail prints for the debug NPC. You edit files
under `Source/ElysiumUE/Private/Substrate/` and `Debug/` as the reads require; before touching a
file, `git status` it — if another agent's edits are uncommitted there, coordinate by editing only
the function you need. Do NOT build or run. Report ≤300 words with each reader's address and where
it is stood.

## Read first (corpus MCP)
1. `vtmb_readers DAT_10925444` (the `ai_debug_npc` handle) and `vtmb_readers DAT_10924a6c`
   (`ent_trace_conditions`, default "1"), plus the `ent_trace` ConVar beside it: list every
   function that tests either, with the string it prints (`vtmb_string` / the listing's PUSH of
   the format). Group them: schedule selection / change (`SetSchedule`, `ChangeSchedule`,
   `MaintainSchedule`'s fail path), task start / complete / fail (`StartTask`, `TaskFail` with the
   fail code and the failing task id), condition set / clear (`SetCondition` / `ClearCondition`
   with the condition name), the hint validators (done), navigation / movement failures, the
   occluded-reaction ladder `0x102b8320`, `SelectSchedule`'s branch prints if any.
2. Record the list in `docs/vtmb/npc-ai/schedule-kernel.md` as a new subsection
   `### The debug NPC's prints — every reader of ai_debug_npc and ent_trace_conditions (2026-09-30)`
   (addresses, the format string, the operands).

## Deliver
3. Each reader stood at its port site: the same gate (`IsHintDebugNpc()` generalised to
   `IsAiDebugNpc()` on the base NPC; `ent_trace_conditions` read from the tunables table, it
   ships "1"), the same text, the same operands, through `UE_LOG(LogElysiumNpcTrace, Display,
   ...)` (a new category) prefixed with the NPC's `DebugString()`. Names of schedules / tasks /
   conditions: use the port's existing name tables (the schedule corpus knows program names; the
   condition enum has names; task identities have names) — retail prints numbers where it has no
   names; print `name (id)`.
4. Verb `elysium.npc_trace <targetname|index|none>` = `ai_debug_npc` (alias; keep both names,
   `ai_debug_npc` is retail's), and `elysium.npc_trace_tail [n]`: the last n trace lines for the
   debug NPC from a ring buffer (so an MCP driver reads the chain in one call instead of the raw
   log), default 60.
5. `elysium.npc_brief`: `debug:` line shows `trace=<n lines buffered>`.
