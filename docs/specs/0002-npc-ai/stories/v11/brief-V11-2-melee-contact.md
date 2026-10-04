# Brief V11-2 — the melee contact to retail: `MeleeSwingStep 0x10343020` and slot 270 `0x102579f0` (coder; no build)

Planner, 2026-10-04, after `../v4/packets-S5.md` item 3. The port's contact (`AdvanceSwingContact`,
`MeleeContact`, `QuerySwingContacts`) is landed work that differs from retail in D1–D11. Under the
bug protocol it is fixed to retail now, and by "testable first" it runs in **V11's wave**
([V11-1, V11-2, V11-3], its own wave after V4b): V11's records are the ones that observe it —
`melee_swing`'s damage (D2, D3, D4), `melee_ally_in_the_way`'s two brawlers side by side (D1).
**Ten differences are yours; D9 is C1's** (`../v4/brief-C1-attack-producers.md` item 3: it is the
sub-step clock `MeleeSwingUpdate 0x10346cd0` supplies, which C1 ports in V4c).

Read `../v4/packets-S5.md` item 3 whole (both bodies arm by arm and the table D1–D11 — the
authority beside the listings `vtmb_asm 10343020` from `0x1034394d` to `0x10343f96`, and
`vtmb_code 102579f0`), `README.md` here §5, `CLAUDE.md`, `spec.md` § Standing rules and § "The bug
protocol", `../v4/README.md` § "Rules for every agent of V4", `docs/vtmb/combat-and-damage.md`
(by `research section 0x10343020`). **Re-locate every site by Grep on the function name.**

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h`, `.cpp` — `AdvanceSwingContact`'s
  walk from the window test to the contact call (~:2760-2840), `MeleeContact` (~:2842),
  `KnockbackContact` (~:3080), their declarations; nothing else in the file (the shot, the
  estimate, the trigger set, who calls the sweep and over which cycles are C1's in V4c)
- `Source/ElysiumUE/Private/Substrate/ElysiumSwingContact.h`, `.cpp`
- `Source/ElysiumUE/Public/ElysiumWorldServices.h` (`FElysiumSwingSweep` ~:643-660, the contact
  record beside it, `QuerySwingContacts`' declaration ~:1535 and what you add beside it; nothing
  else), `Source/ElysiumUE/Public/ElysiumMapActor.h` (the override's declaration ~:439),
  `Source/ElysiumUE/Private/Map/ElysiumMapActor.cpp` (`QuerySwingContacts` ~:1650-1720 and what
  you add beside it), `Source/ElysiumUE/Private/Tests/ElysiumTestServices.h` (the double ~:1193)
- `Source/ElysiumUE/Private/Tests/ElysiumSwingContactTests.cpp`, `ElysiumKnockbackTests.cpp`,
  `ElysiumMeleeTestHelpers.h`, new `ElysiumMeleeSwingStepTests.cpp`

`ElysiumEnvBeam.cpp` (~:263) is a second caller of `QuerySwingContacts` and **not yours**: its
answers must not move. Add retail's step query beside the existing one rather than changing what
the beam receives; if that cannot hold, stop and report.

## The job — one numbered line per difference

- **D1 — the NPC attacker's relation filter** (`0x1034394d..0x103439a7`). In the walk, before any
  other test: attacker is an NPC (`this+0x94`), `debug_allow_melee_ff` (`0x10936ee0`) 0, the
  candidate a combat character other than the attacker → slot 404 `IRelationType` must be 1 or 2
  (hate, fear) **and** `0x10323930(npc, victim)` false (same 16-bit team id at `+0x10b0`, neither
  `0xffff`); else skipped. A player attacker has no filter. The team word: Grep `+0x10b0` /
  `m_sTeamName`; with no home, a seam answering "no team", named for `+0x10b0`, and the
  declaration in your report.
- **D2 — `rolls == 0`** (`0x102579f0` step 5): `GetMeleeDiceRolls` answering 0 → unblocked,
  successes 1, the damage dispatched. Remove the "no staged roll → no contact" return (~:2859-2867).
- **D3 — the dispatch condition** (step 5–6): damage is dispatched unless `blocked &&
  !DamageWentThrough(rolls)`; `successes < 1 → 1`. Replace `if (Margin > 0)` (~:3005).
  *Unread:* the operand that converts the roll to `successes` (lost to the x87 stack): keep the
  port's value, floor it at 1, name the gap at the line.
- **D4 — the hit test** (`MeleeSwingStep` step 2 and 3.9): `n = ceil(|B − A| × 0.1666667)` (f32
  `0x10488874`), `n < 2` → 1 and `A = B =` the midpoint; per sample `i`, `f = n > 1 ? 1 − i/(n−1)
  : 0`, a zero-extent ray **from `Q = lastA + (lastB − lastA)·f` to `P = A + (B − A)·f`**, clipped
  to the candidate alone (`0x101d2530`, mask `0x200400b`); the first sample that hits is the
  hit. **No occlusion trace** between limb and entity: delete the use-channel one (~:1663-1700).
  The record's last endpoints are stored at the end of the pass, window open or closed.
- **D5 — the candidates** (step 2, `0x101cca80`): every solid entity in the AABB over `{A, B,
  lastA, lastB}` (at most 100, mask `0x22102080`), not combat characters only. A non-character
  (`+0x9c == 0`) is **hit by the box overlap, no ray** (step 3.8): trace start = the attacker's
  origin, end = the entity's origin; it reaches slot 270 (props, breakables). Remove the
  container / reported-dead exclusions (~:1707-1719) unless one stands for a gate of D6 — say
  which at the line.
- **D6 — the gates, in retail's order** (step 3.2–3.7): the candidate solid (`m_nSolidType
  +0x2b0 != 0`, `+0x2b4 & 4` clear); the **attacker** not `FSOLID_NOT_SOLID`, else nothing is
  hit; the candidate's byte `+0xf4` zero (unnamed: a seam answering 0, named for `0x100b5190`);
  `victimCC == 0` or `m_bIsBCCTargetable (+0x1480)`; already in this record's list → skip; the
  root of the candidate's owner chain (`0x1012c9c0`) is the attacker → listed for this record,
  no hit.
- **D7 — victim slot 329 true** (`0x10343b00..0x10343b16` → `0x10343eb0`; base `0x1014f850`
  false, `CNPC_VTzimisceRunner 0x103c30c0` true): the box overlap is the hit and the entity is
  added to **every** record's list, no window test. Call the virtual (`vfunc329`); if the
  Runner's override is not ported, its exact body and place go in your report (not your file).
- **D8 — the wall contact** (`0x10343f96`), only when the attacker's slot 328 is true
  (`CBasePlayer 0x1015dca0`, `CNPC_VWerewolf 0x103ca730`; base `0x1014f830` false): per sample
  the same `Q → P` ray against the world, ignoring the attacker; normal non-zero with `|normal.z|
  < 0.3` (`0x10451ab8`), flattened forward length² `> 1e-12`, `|dot(normal, forward)| > 0.7071`
  (`0x1049e03c`) and a length `< 20.0` (`0x1049e040`) → the attacker's slot 319 (the blocked
  reaction), sample loop ends; otherwise once per swing (`+0xaa0`) the impact effect
  (`0x101cfef0(&tr, 0x80, 1, weapon)`). *Unread:* which length is compared to 20.0 — port the
  arm with the hit's distance from `Q`, **named at the line as inferred**, and report it. If
  slot 328's two overrides are not ported, report their lines.
- **D10 — slot 270's side effects** (`0x102579f0` steps 2 and 8): weapon slot 339, then
  `CSoundEnt::InsertSound(0x10, endpos, [0x1072bc5c], 0.2, [0x1072bcb7], owner)` **before** the
  null-entity test; the tail on every path — the impact effect, the entity's slot 21, the
  owner's slot 24. Grep first whether another file already does one of them for a melee hit;
  port what is missing, cite the address, and list what was already there.
- **D11 — plain hit or knockback** (step 7): `!blocked && victimCC`: `(record == 0 || record ==
  −0x28 || !victim slot 326(record)) && !victim slot 400()` → victim slot 321; else
  `GetKnockbackActivity(victim, trace, owner, record)`: −1 → a Warning, else victim slot 320
  `(owner)`. Asked **after** the damage. Replace the margin-class selector (`HitKnockback`,
  ~:2972) where `KnockbackContact` does not ask those slots; keep what already does.
- **Also retail, check and fix at the line**: the window is inclusive at both ends (`start <=
  cycle && end >= prevCycle`; `ElysiumSwing::WindowOverlaps`, `ElysiumSwingContact.cpp` ~:61); a
  ray hit spreads to every record whose window overlaps this one's (`0x10343e37..0x10343e84`);
  records `count..19` have their lists cleared; blocked reactions (victim slot 318, owner slot
  319) before the damage; `total = (modifier + m_iDiceAmt) × mult × inflicted` with the Potence
  floor (step 6) — report a difference you do not fix, with its line.
- **Keep for C1**: the endpoints of a record at a sub-step come from **one function** (today's
  live-bone lerp, ~:2786-2795) taking the sub-step's cycle, so C1 replaces its body for D9
  without touching your walk. Do not port D9.
- **Tests**, each assertion naming its address — `Elysium.Arm.MeleeSwingStep.*`: `.Relation`
  (D1: neutral and same-team NPCs not hit by an NPC, hit by the player; hated hit),
  `.Samples` (D4: `n` for 5, 6, 13 units; the midpoint case; `Q → P`; no occlusion), `.Candidates`
  (D5–D7: a prop hit by overlap; the six gates; slot 329 marks every record), `.Wall` (D8);
  `Elysium.Arm.MeleeContact.*`: `.NoRolls` (D2), `.Dispatch` (D3: unblocked non-positive margin
  still damages; blocked and not through does not), `.SideEffects` (D10), `.Reaction` (D11).
  Rewrite or delete assertions in your three test files that pin the old test (four-edge patch,
  the occlusion trace, `Margin > 0`, the margin-class knockback); list them.

## Not yours

Where the sweep runs and over which cycles (`AdvanceMeleeSwings`, slot 315 / 312,
`MeleeSwingUpdate 0x10346cd0`, the stamp, **D9**), `Weapon_FrameUpdate`, the swing start and the
trigger set (C1, V4c), slot 331 (V11-3), the coordinator (V11-1), the dice themselves
(`GetMeleeDiceRolls`, `WasMeleeBlocked`: called), `ElysiumCombatCharacterSlots.cpp`, the species
files, `ElysiumEnvBeam.cpp`, `ElysiumEntityWorldInteraction.cpp`, any record under `Arena/`.

Wave check ([V11-1, V11-2, V11-3]): none of your files is V11-1's (`README.md` §4) or V11-3's
(`ElysiumMeleeSequenceChoice.{h,cpp}`, `ElysiumCombatCharacterSlots.cpp`, its test). B1 and B2 are
not in this wave: `Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h` and
`Tests/ElysiumTestServices.h` are yours here for the swing query only. V11-1 **calls**
`TraceRetail` in that header and edits nothing in it; V11-3's `+0x8b8` / `+0x8c0` declaration, if
it reports one, is the integrator's line in your `ElysiumWeaponClasses.h`.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole (`ElysiumWeaponClasses.cpp` and
`ElysiumMapActor.cpp`: Grep, then read the section). Text through Grep / Read / Glob.
`research where <addr>` before searching `docs/`. Do not commit. Report ≤300 words: per D (1–8,
10, 11) ported / already retail / seam, with the address; the two unread operands as written;
tests added and deleted; cross-lane lines; what stays unrecovered.
