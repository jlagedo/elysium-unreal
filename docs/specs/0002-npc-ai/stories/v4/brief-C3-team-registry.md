# Brief C3 — V4c: the combat-character team registry (coder; no build)

Final, 2026-10-04. Read `AGENTS.md` first, `HANDOVER.md`, README § "Rules for every agent of V4"
/ "Shared names", S5 item 3 D1, S12, and **S13 §2 / Changes to the plan item 2**. The owner has
ruled that the registry is built in V4c, with a Green Room record of two same-team NPCs.
The symbol predicate and writer/reader closure are settled; the level-boundary reset corrected
S13's older draft. Re-locate existing port sites by function name; addresses are retail.

## Files (only these)

- `Source/ElysiumUE/Public/ElysiumPlayer.h` — declarations/state on **FElysiumCombatCharacter**
  only (the class is here, not in a separate CombatCharacter.h).
- New `Source/ElysiumUE/Private/Substrate/ElysiumTeamRegistry.{h,cpp}`.
- New `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterTeam.cpp` — common character
  AddToTeam/getter/IsSameTeam implementations and the TeamFilter wrapper.
- New `Source/ElysiumUE/Private/Tests/ElysiumTeamRegistryTests.cpp`.
- New `docs/vtmb/npc-ai/teams.md` — complete S13 writer/reader/lifecycle recovery, dated V4c C3.

C3 ∩ C1 = C3 ∩ C2 = ∅. No weapon, damage, spawn/player main file, generated binding or Arena
file is yours. C1 applies the contact predicate; C2 applies damage/spawn/player hooks. Give
these owners exact patches against the API below; report world/restore/generation patches for
serial integration. If another lane needs a declaration in Public/ElysiumPlayer.h, you as its
owner add it, scoped to this lane; otherwise integrator applies the reported line after reports.

## The job

1. **Field/constructor `0x10326de0`, WORD store `0x103272f6`, datamap `0x1031a600`.** In
   `Public/ElysiumPlayer.h::FElysiumCombatCharacter`, add `FString TeamName` for +0x10ac
   m_sTeamName/keyfield **team_name**, and `uint16 TeamSymbol = 0xffff` for +0x10b0. m_TeamSymbol
   is the project's inferred member name; unsigned WORD width and invalid 0xffff are verified.
   No keyfield/input on symbol; no new SetTeam input. Equal class, squad or relationship is not
   a team. Both witness manifests have no team_name keys, so placed NPCs/maker children start
   invalid; the player joins player. Maker does not copy a team symbol.
2. **AddToTeam `0x103239a0`, store AX `0x10323a21`, registration `0x10230880` / table
   `0x1024b5e0`, registry `0x10751140`.** Declare
   `void FElysiumCombatCharacter::AddToTeam(const FString& Name)` and implement it in new
   ElysiumCombatCharacterTeam.cpp through FElysiumTeamRegistry::FindOrInsert in new registry
   files. Strip **one** leading !; null/empty answer 0xffff (sole ! also invalid). Copy through
   retail Q_strncpy(...,0x80): at most **127 bytes payload**, lowercase, find-or-insert; reuse
   existing symbol. Match byte normalization/truncation, not 127 wide characters or repeated
   ! removal. Store the returned WORD; final getter's answer discarded. **Do not change
   TeamName inside AddToTeam**. Registry state is normalized names→uint16 symbols and next
   count/string storage only; no members, team actors, squad coupling or faction matrix.
3. **Getter `0x10323a70` (thunk `0x1000bc44`), SameTeam `0x10323930` (thunk
   `0x10008d7d`), filter `0x103426b0`.** Declare/implement
   `uint16 FElysiumCombatCharacter::GetTeamSymbol() const` and
   `bool FElysiumCombatCharacter::IsSameTeam(const FElysiumCombatCharacter* Other) const` in
   ElysiumCombatCharacterTeam.cpp: invalid self, null other, invalid other → false; otherwise
   equality → true (only retail's low-byte bool matters). TeamFilter takes candidate's
   combat-character self-cast +0x9c (null if no candidate) then the same predicate; expose a
   wrapper in ElysiumTeamRegistry.h with an explicit entity-candidate signature. Getter's
   complete recovered callers are SameTeam and AddToTeam's discarded final read.
4. **All registration writers/callers `0x10323a90`, `0x10298d30`, `0x10348890`,
   `0x1016d260`, `0x1016ebd0`.** Record both direct WORD writers (constructor/AddToTeam) and
   all five callers of thunk `0x10015bcc` in teams.md. Nonempty TeamName at combat-character
   Spawn, Troika Spawn and restored combat-character state registers; player Spawn and Restore
   register literal **player**, Restore after base restore. Keyfield sets name, registration
   sets symbol, never numeric save compatibility. Give C2 exact patches:
   ElysiumNpcBaseSpawn.cpp::Spawn19TeamName returns TeamName; Spawn19AddToTeam calls AddToTeam;
   existing Spawn and ElysiumNpcSpawn.cpp::TroikaSpawnBody already reach these hooks. C2 updates
   ElysiumPlayerEntity.cpp::Spawn / Hydrate. Give integrator the restore join after restored
   fields are applied in ElysiumEntityWorldPersistence.cpp::ApplyEntityRecord (before any
   post-restore consumer), using restored nonempty name; player restore explicitly joins player.
   Recompute symbols from names, including entities whose restored name differs from spawn.
5. **Game-system ownership/lifetime `0x10230750`, `0x102307b0`, reset hooks `0x10230820`
   / `0x10230860` → `0x1024b880`.** New FElysiumTeamRegistry implements
   LevelInitPreEntity / LevelShutdownPostEntity: clear tree, free strings, reset live count.
   Registry constructor initializes, destructor destroys. **Both level hooks reset**, not just
   New Game; numeric symbols never survive transitions. `LevelInitPostEntity 0x102308f0`
   calls diagnostic enumeration `0x1024b940`, **not reset**. Game system owns one table;
   integrate ownership/access in Public/ElysiumSessionSubsystem.h /
   Session/ElysiumSessionSubsystem.cpp::Initialize/Deinitialize and a common world accessor in
   Public/ElysiumEntityWorld.h. Report exact patches; do not edit these files. For headless
   fixtures the world supplies a registry with the same pre/post level lifecycle, not per-NPC
   tables. Wire ElysiumEntityWorld.cpp::Load before construction/spawn to pre-entity clear;
   Teardown after EntityList.Empty to post-entity clear. Names re-register after pre-reset.
   Clear calls must correspond to actual level boundaries; do not clear between entities or
   during a mid-level ApplyEntityRecord and invalidate already registered teammates.
6. **Generated key binding `0x1031a600`, +0x10ac, strings `0x1061ea74/0x1061ea68`.** Read
   research/tooling/gen_kernel_bindings.py::classify and CHAIN_MEMBER_MAPS/CHAIN_UNBOUND first.
   Report this exact generation-source change to integrator: CBaseCombatCharacter 0x10ac maps
   to (FElysiumCombatCharacter, TeamName); remove the stale CHAIN_UNBOUND "no reader" reason;
   preserve replay's field flags. Integrator regenerates ElysiumNpcKernelBindings.cpp, never
   hand-edit it. Binding belongs on the combat-character chain so NPC/player both inherit it;
   TeamSymbol itself is not a key/input field. Report any kernel shape/verdict declarations
   needed, with address; generated slot bodies follow matching SlotBodies convention.
7. **All four SameTeam readers — `0x10343020`, `0x1032ef60`, `0x10322b40`,
   `0x103426b0`.** Document their verified order/meaning in teams.md. C1 changes
   ElysiumWeaponClasses.cpp::ElysiumSwingSameTeam / AdvanceSwingContact: NPC, distinct character,
   FF off requires hate/fear then rejects same team at `0x103439a0`; player/self/noncharacter/
   debug arms bypass this particular contact filter. C2 changes
   ElysiumCombatCharacter.cpp::CombatTeamSymbolOf / CombatSameTeam / OnTakeDamage: same accessor,
   teammate refusal before discipline notification/life-state dispatch, **self damage excepted**.
   Presence UpdatePresenceEffect `0x10322b40` treats same team **or D_LI** as friendly benefit;
   recover its team call here, leave wider discipline implementation to that owner (no false
   constant substituting for this accessor). TeamFilter is a reader wrapper, not a second owner.
8. **Arm tests, write only, every address pinned.** New ElysiumTeamRegistryTests.cpp uses
   `Elysium.Arm.TeamRegistry.*`: constructor invalid WORD; null/empty/!/one-! handling;
   case equality, 127-byte normalization/truncation, reused/different names; getter/null/invalid
   equality; TeamFilter null/noncharacter and same/different. Pre-init and post-shutdown each
   erase table/count/string state; diagnostic post-init does not; restore re-registers names
   rather than saved IDs. Pin all five registration sites including player; test key binding
   after integrator regeneration. Consumer predicates include NPC hate/fear+same-team refusal,
   debug/player/self bypass, and damage self exception versus named teammate attacker. Report
   exact consumer test lines owed to C1/C2 rather than editing their tests. Pin Presence's
   recovered predicate independently without constructing its wider discipline body.
9. **Green Room records for integrator — S13 §2 / Changes item 2 (acceptance predictions).**
   Specify **melee_same_team** with an NPC attacker hating an NPC bystander (D_HT below player's
   priority), no squad, debug_allow_melee_ff=0; names **!Arena_Melee / arena_melee**. Bystander
   overlaps swing but stands off slot331's centre ray (otherwise ENEMY_BLOCKED prevents swing).
   Assert equal non-0xffff symbols; expect admitted swing and positive damage to an otherwise
   identical **different-team control**. Never damage, contact-side impact/knockback or a
   **Swing.RecordHits entry** for same-team bystander. If absent, owe read-only hit-list/side-effect
   observation to integrator; damage alone also passes via outer damage rejection. Existing
   melee_ally_in_the_way stays neutral/no-team relation control. Cite `0x1034394d..0x103439a7`,
   `0x10323930`, `0x10347180` (line gate) and `0x102579f0` (impact side effects).
   Also specify **team_damage_gate**: named gunman's live weapon packet retains attacker handle;
   target explicitly hated teammate → no damage; identical different-team target → damage;
   self damage admitted by predicate. Cite `0x1032ef60` / `0x10323930`. No attackerless scalar
   TakeDamage input can prove this gate. Staging can inject keyvalues for these fixtures despite
   the witness maps' absent team_name keys; no map edit or invented team input.

## Not yours

Wider Presence/discipline behavior, factions/squads and save migrations. Team identity is fully
built here; no no-team constant is left in either live consumer. Source owners apply hooks,
integrator applies generation/world/restore wiring. No pipeline import or re-bake is needed.

## Rules

- Touch only the listed files; lines owed by other files go in your report with file/function
  and exact patch. Generated `*Slots.cpp` and bindings are never hand-edited: hand bodies go
  in matching `*SlotBodies.cpp`; the integrator owns verdict rows and regeneration.
- Never build, run tests, the arena, editor or game, or commit/push. No `Arena/` edits.
- Retail first: look up each address before searching docs, read the listing when required,
  and cite the retail address at every ported line. A missing input gets a named seam answering
  nothing. A new divergence is recorded in the report, not adopted.
- Shadowed locals are compile errors here (C4458/C4459); check includes and double definitions.
- Every query has a 60 s timeout; >10 s warns and is logged. At 60 s stop and optimize before
  retrying; never widen/retry as-is. Never read a file over ~200 KB whole. Wait by completion
  notification, never a polling loop.
- Report ≤300 words: addresses/behaviour, tests added/deleted, remaining reads/seams, and exact
  lines owed by other files. Deliver it in the worker response, never a file named `report*.md`.
