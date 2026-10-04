# Brief C — the room and the taps (H2, H5's stealth-kill event)

Read `README.md` here first. Your files: `Source/ElysiumUE/Private/Debug/ElysiumArenaBuilder.cpp`
and `.h`, `Source/ElysiumUE/Private/Substrate/ElysiumGrapple.cpp`, the trace seam's own files (the
event-kind table and the sink it emits through: find them from `stories/wave2/seam.md`),
`docs/specs/0002-npc-ai/stories/wave2/seam.md`.

## H2 — arena solids are transparent to NPC sight

The arena's walls and block are `BlockAll` boxes (`ElysiumArenaBuilder.cpp:81`). `BlockAll` lists
no custom channel, so each takes the channel's default, and `ElysiumSight` defaults to Ignore
(`Config/DefaultEngine.ini:63`): NPC sight passes through every wall and the block. Baked map
geometry answers sight by its contents; baked props use the `ElysiumPropSolid` profile (`:49` of
the same ini), which blocks it.

- Make every arena solid answer exactly as a baked world solid answers each of the project's
  channels (sight, the hull traces, the bullet trace, the use trace): read which profile or
  responses baked world brushes get (the map bake's collision actors) and give the arena the
  same, rather than patching the one channel. Do not edit `DefaultEngine.ini`: its defaults serve
  the game.
- Check, by reading, what else in the arena is a solid (the cover props, the anchors' markers, the
  floor) and that nothing that should be see-through becomes opaque.
- `ElysiumArenaSpec.cpp` carries a `static_assert` that the `far_ne` → `cover_seat` line clears
  the block: leave the geometry alone.

## H5 — the stealth-kill trace event

`Arena/scenarios/combat/verbs_stealth_kill.json` gets no kill and nothing in the trace says which
gate refused. Retail's query is `FindVictim 0x101be1f0`: the ray within `StealthKillDistMax`,
`IsValidStealthKillTarget 0x102c2300`, `InDeafArc 0x101be500`, `CanStartGrappleAttack(3)`; the
port's is at `ElysiumGrapple.cpp:93`.

- Emit kind `stealthkill` (text and gate names fixed in `README.md` § "The shared names") from the
  port's query: one event per query that reaches a candidate, naming the first gate that refuses
  or `admit`. The event's entity is the victim candidate (the player's own name when there is
  none). Nothing is emitted without a sink; the query's behaviour, order and results are
  unchanged — a tap only.
- Register the kind where the seam's kinds are registered and add its row to `seam.md`.
- If the port's gates are not in retail's order, or one is missing, do not fix it: report it with
  the addresses. That is a game red for the triage.
