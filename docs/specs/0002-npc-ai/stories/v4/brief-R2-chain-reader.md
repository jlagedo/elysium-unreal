# Brief R2 — V4r, packet R2: the event chain, the attack producers and the small bodies (one reader, no code, no build)

Read `README.md` here (§1, §2, §5). You recover retail bodies the V4a and V4c coders will port and
write them into `docs/vtmb/`. No C++, no Python, no record, no run. Runs in parallel with R1.

## Your files (only these)

- `docs/vtmb/animation_events.md` § "The server dispatcher" (~:39) and § "Overlay layers…" (~:92).
- `docs/vtmb/combat-and-damage.md` § "No shipped sequence authors a melee commit event" (~:1027) and
  the NPC attack sections next to it.
- `docs/vtmb/npc-ai/shape.md` § "The activity commit" (~:2647): `RunAnimation`'s pick.
- `docs/vtmb/npc-ai/senses.md` § "Cone" (~:53).
- `docs/vtmb/animation_and_movers.md` § "The disposition stance machine" (~:1846): `SetDisposition`.
- Your report: `docs/specs/0002-npc-ai/stories/v4/packets.md`, section "R2" (append; R1 writes "R1").

## Packet R2

1. **The dispatcher.** `CBaseAnimatingOverlay::DispatchAnimEvents 0x10098c80` and the per-layer body
   `0x10098cd0`; the base `0x10091880`. Confirm the order (base, then layers 0..3), every write
   (`m_bSequenceFinished`, `m_fSequencePastHalf +0x568`, `m_flLastEventCheck +0x658`), the
   `OnSequenceFinished` call (slot, rising edge). Then `NPCThink`'s order (`research where` the
   Troika `NPCThink`, `RunAI`, `PostRun 0x1026c7c0`): do tasks run before `PostRun` in one think, so
   slot 251 reads the dispatcher's look-ahead finish from the previous think? `StudioFrameAdvance`
   writes the flag too (README §1): state which value a `RunTask` arm sees and when.
2. **`CBaseCombatCharacter::Weapon_FrameUpdate`** (called by `PostRun` with the interval): its body,
   and what it does for an NPC (the weapon's own frame advance / events? the melee swing's sweep?).
3. **The NPC attack producers.** (a) Ranged: `TASK_RANGE_ATTACK1`'s Troika and base start / run arms
   → the activity → the 3031 event → `CWeaponRanged::Operator_HandleAnimEvent 0x10238160` → the fire
   body. Is there **any** retail path that fires an NPC's shot without the event (the port's
   `ContactEventCycle` estimate, `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h:303`,
   `.cpp:1263-1317, 2166`, is exactly that question)? (b) Melee: how `TASK_MELEE_ATTACK1` starts an
   NPC swing (3047 is authored nowhere, `combat-and-damage.md` :1035-1043) — `PrimaryAttack`? a
   weapon slot? — and where the per-frame contact sweep runs for an NPC (`Weapon_FrameUpdate`?
   `ItemPostFrame`?). The port sweeps from the world interaction tick
   (`ElysiumEntityWorldInteraction.cpp:299-326`). (c) Is `melee_swing.json`'s `hit_event` (an
   `animevent` within 3 s of `task_melee_attack1`) retail? If the swing clip authors no event, say
   it is a record error and what the record should expect instead.
4. **Overlay layers.** Who pushes an NPC overlay (`AddGesture`, `AddLayeredSequence`, the
   `*_attack_layer` / `*_reload_layer` sequences, `animation_events.md` :104-107) on the paths the
   step-2 records reach (`cover`, `ranged_open_fire`, `range_bands`, the patrols)? "None reached" is
   a fine answer.
5. **`SetAttackExtentsForSequence 0x10090c80`** (decompile in README §1): name the slot 15 (`+0x3c`)
   it calls, and find the writers of `Flags2 & 4` (`GetFlags2`'s word): does any NPC class set it?
6. **Slot 363's inputs** (`0x10326750`, README §1): slot 192 (`+0x300`) and slot 29 (`+0x74`) on an
   NPC candidate (what point; what scalar — `CHL2_Player` answers `m_flStealthVisionCone +0x1c74`,
   the NPC?); the player's `m_flFieldOfView +0x1574` writer and value (`senses.md` says 0.5).
7. **`SetDisposition 0x102c0f70`** (387 bytes) arm by arm: the `m_bDisableAI` gate, what it writes
   (`m_IdealActivity +0xff0 = 0xf1`? `+0x5ccc`? `+0x5b94`, `+0x64d4` per `order.md`), the
   `GetTransitionAnim 0x100ed150` call and its fallbacks, `ResetSequenceInfo`, the order. Write
   `RunAnimation 0x1026c540`'s pick (`m_bSequenceLoops +0x65d` false → `SelectHeaviestSequence`, true
   → `SelectWeightedSequence`, committed through `0x10260a50`; gated on state ≠ 4, ≠ 7,
   `m_IdealActivity == 1` and slot 251) into `shape.md` with the addresses.
8. **`AutoMovement`'s `StudioFrameAdvance(0)`** (the port calls it at
   `ElysiumNpcBaseMotor.cpp:564`, a second advance per think): retail or not? Find retail's
   `AutoMovement` and say.

## How to read

- Look every address up first: `uv run elysium research where <addr>`, `research section <addr>`;
  then the `vtmb-corpus` MCP (`vtmb_func`, `vtmb_asm`, `vtmb_code`, `vtmb_callers`, `vtmb_fields`,
  `vtmb_string`). Prefer `vtmb_asm` slices to whole decompiles.
- The query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops; a
  stopped query is not retried as-is or widened. Never read a file over ~200 KB whole.
- Text through the built-in Grep / Read / Glob tools. No sleep or polling loop.
- Every claim carries its address. What you could not settle is **Unrecovered:** with what you tried.

## Report (≤300 words, plus `packets.md` § R2)

`packets.md` § R2: the eight items as numbered findings with addresses and the doc section each
went into. The report: what you settled, the answers C1 and the judge need (items 3 and 5 first),
what stays unrecovered, every query over 10 s.
