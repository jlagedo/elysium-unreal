# Brief S5 — retail's `ai_debug_npc` for the hint validators; `gr_hints --validate` (spike)

Read `CLAUDE.md`. Retail debugging, reproduced: `DAT_10925444` is the `ai_debug_npc` handle; when it
resolves to *this* NPC, the hint validators format their reason strings (`schedule-kernel.md` §
"The three hint validators": `"Target name mismatch (%s)"`, `"Distance (%d) < %d or > %d"`,
`"Enemy outside of good range (%.2f) <= %.2f"`, `"Enemy inside of bad range"`, `"Projection (%.2f)
< 0.2"`, `"Failed LOS check (%s)"`, `"Failed hint LOS"`), and `debug_hint_los` draws the LOS
trace. The port's `IsHintDebugNpc()` answers false always and the strings are comments. You edit
ONLY:
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBaseHelpers.cpp` (+ `.inl`): `IsHintDebugNpc`,
  the two cover validators `0x10295ed0` / `0x102961a0` (reason strings)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcHints.cpp` (+ `.inl`): `ValidateHintCoverRange`
  `0x10296c40` (reason strings), `HintIdleActivityGate` untouched
- `Source/ElysiumUE/Public/ElysiumEntityWorld.h` + `Private/Substrate/ElysiumEntityWorld.cpp`: the
  debug-NPC handle word (`DAT_10925444` is a DLL global; hold it on the world as
  `AiDebugNpc` handle + accessor; not saved)
- `Source/ElysiumUE/Private/Debug/ElysiumEntityDebugSubsystem.cpp`: the console verb
  `elysium.ai_debug_npc <targetname|index|none>` (retail's ConVar name kept as the verb name)
- `Source/ElysiumUE/Private/Debug/ElysiumGreenRoomConsole.cpp`: `elysium.gr_hints --validate [npc]`
Do NOT build or run. Report ≤300 words.

## Deliver

1. `IsHintDebugNpc()` = the world's `AiDebugNpc` resolves to this NPC (retail: handle serial match
   + non-null). Set by the verb; cleared with `none`.
2. Reason strings, retail's texts verbatim, at the same arm and with the same operands retail
   formats (read the walked prose: `%d` for the distance band, `%.2f` for projections, the
   blocker's name for the LOS check), logged through `UE_LOG(LogElysiumNpc, Display, ...)` prefixed
   with the NPC's `DebugString()` and the hint's index/name — only when `IsHintDebugNpc()`. Retail
   builds the string only under that gate; keep that (no formatting cost otherwise). Do not
   change any verdict.
3. `elysium.gr_hints --validate [npc]` (default NPC `arena_gunman`): for each hint on the list,
   run EXACTLY the admission the tactical search `FindHintByClassMask` runs on that NPC — the
   unusable test, the class mask (print which mask the NPC's last selector call used if known,
   else 1), the distance vs `CoverRadius()`, slot 566 `FValidateHintType` (which routes to the
   cover validators by type) — WITHOUT writing the cursor or claiming: reuse the existing
   functions (`IsHintUnusable`, `FValidateHintType` on the hint's words; if the walk's gate helper
   in `ElysiumNpcBaseHints.cpp` is file-local, expose a `const` "dry run" entry beside
   `FindHintByClassMask` that reports the first failing gate per hint and never writes the
   cursor — add it to `ElysiumNpcBaseHints.{cpp,inl}`, which you may edit for that one function).
   Print per hint: `#idx name type mask node  → admitted | refused at <gate> (<reason>)`, and,
   with the debug NPC set to that NPC for the duration of the call (set, run, restore), the
   validators' reason strings appear inline. Also print the NPC's enemy, `m_hHintCoverObject`,
   `bStayEntrenched`, `CoverRadius()`, and the facing values the validator computed (the
   projection / bad-range numbers) so the arena's geometry can be authored from them.
4. `elysium.npc_brief`: append one line `debug: ai_debug_npc=<yes|no>`.
