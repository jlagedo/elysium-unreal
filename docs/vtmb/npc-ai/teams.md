# Combat-character teams — V4c C3, 2026-10-04

Recovered from vampire.dll listings/decompilation and the indexed caller closure. S13 §2 and
Changes item 2 are the settled packet; S5 item 3 D1's former symbol inference is now verified.
This is team identity, independent of class, squad, faction and disposition. No new team input
is justified. No build, test, editor, game or arena run was made by C3.

## Field and writer closure

`CBaseCombatCharacter +0x10ac m_sTeamName` is a `string_t`, external key `team_name`.
`datamap_CBaseCombatCharacter_builder 0x1031a600`, instructions `0x1031ae41..0x1031ae79`,
install the member-name string `0x1061ea74`, offset `0x10ac`, and external string `0x1061ea68`.
The bounded replay lookup gives type `string`, **flags 6: SAVE | KEY**, count 1. Preserve both
flags in generation. `+0x10b0` is an unsigned WORD; `m_TeamSymbol` is the project's inferred
member name. It has no keyfield/input row, and no numeric save contract is introduced.

The complete recovered direct writers are:

| Writer | Exact store | Meaning |
|---|---|---|
| Constructor `0x10326de0` | `0x103272f6 MOV word ptr [ESI+0x10b0],0xffff` | invalid symbol |
| AddToTeam `0x103239a0` | `0x10323a21 MOV word ptr [ESI+0x10b0],AX` | registry's returned symbol |

The constructor uses array index `0x42c` in decompiled C, not a typed field; absence from a
typed field-ledger section is not writer-closure evidence. The constructor WORD store was read
in assembly before writing the port.

`AddToTeam 0x103239a0`, read in assembly: `0x10323a0e` tests the first byte for `!`,
`0x10323a13` increments the pointer **once**, `0x10323a1a` calls registry thunk `0x1000a72c`
at global `0x10751140`, then stores AX and calls getter thunk `0x1000bc44` at `0x10323a28`,
discarding its answer. It never writes the name field. The retail method assumes a nonnull
string; its registry handles null. FString callers pass an empty string for absence.

`0x10230880`: null/empty returns `0xffff`; otherwise `Q_strncpy(scratch, name, 0x80)`,
CRT `_strlwr`, then find-or-insert `0x1024b5e0` on manager +8. The scratch is `0x107510c0`.
Payload is at most **127 bytes**, including a possible partial multibyte character. There is
no trimming, repeated bang removal or TCHAR-length limit. `!` passed to AddToTeam is invalid;
`!!` registers literal `!`. Registry direct calls do not strip bangs.

`0x1024b5e0` first looks up the normalized bytes (`0x1024b400`), reuses an existing WORD,
otherwise copies the NUL-terminated string into owned pools and inserts a node. Node allocation
`0x1024b9e0` consumes the free-node chain before growing high-water storage. Since this manager
never deletes individual names, indices are insertion order within each level; a reset chains
the retained nodes starting at zero. The C3 table uses owned byte strings and insertion symbols,
with no per-character member list or actor. Its string-byte observer counts live payload plus
terminators, not retail's retained tree capacity or 0x800-byte pool slack.

The port converts FString to the project's UTF-8 byte boundary before truncation and uses CRT
byte lowercase, preserving byte keys without decoding them back to FString. Non-ASCII corpus
encoding/process-locale parity should be checked before claiming arbitrary non-ASCII retail
names equivalent; the acceptance names are ASCII. Exhausting the 16-bit table is outside the
recovered shipped reach; no invented saturation rule is adopted.

## Every registration caller

The indexed callers of thunk `0x10015bcc` are exactly these five:

| Caller | Name and order |
|---|---|
| Combat-character Spawn `0x10323a90` | nonempty m_sTeamName, before CBaseAnimating::Spawn |
| Troika Spawn `0x10298d30`, call `0x10298db6` | nonempty m_sTeamName at its existing spawn position |
| Combat-character Restore `0x10348890` | after base Restore applies fields, nonempty restored m_sTeamName |
| Player Spawn `0x1016d260`, call `0x1016db7e` | literal `player`, string `0x10566574` |
| Player Restore `0x1016ebd0` | successful base Restore, then literal `player`; base failure returns before joining |

Setting the key writes the name, registration writes the symbol. Restore registers the restored
name, even when it differs from the spawn name; it does not restore numeric identities. Player
restore's final literal join overrides any base restored-name join. ApplyEntityRecord must not
clear the common table between entities. The ordinary maker does not copy a parent's symbol.

S13's bounded witness-manifest lookup found zero team_name keys in sm_hub_1 / sp_tutorial_1.
Writer closure plus those map facts imply invalid placed-NPC/maker-child symbols at map start;
the player explicitly registers `player`. This is not a claim about later keyvalue injection.

## Getter and all four readers

Raw getter `0x10323a70` (thunk `0x1000bc44`) reads an unsigned WORD at +0x10b0.
The indexed getter-thunk callers are only SameTeam and AddToTeam's discarded final read.
SameTeam `0x10323930` (thunk `0x10008d7d`) checks self invalid first, null other next, other
invalid next, then repeats the getters and compares. Only AL represents its bool: the high
return byte may retain symbol bits. Invalid never equals a team, including invalid self.

The complete recovered four-call SameTeam closure is:

1. MeleeSwingStep `0x10343020`, assembly `0x1034394d..0x103439a7`: attacker NPC self-cast +0x94,
   FF debug flag zero, candidate combat-character +0x9c nonnull and distinct from attacker;
   then slot 404 (+0x650) relation must be **D_HT=1 or D_FR=2**, and only afterward SameTeam
   at `0x103439a0` rejects it. Player attackers, self, noncharacters and debug-on bypass this
   particular filter. It runs before solidity, targetability, hit-once bookkeeping and impact.
2. OnTakeDamage `0x1032ef60`: takedamage zero first; resolve packet attacker +0x2c's
   combat-character +0x9c; SameTeam true **and attacker entity != victim** returns zero,
   before discipline notification `0x101e3cf0` and before life-state dispatch. Self damage is
   admitted by this predicate. An attackerless scalar input cannot exercise named-team rejection.
3. UpdatePresenceEffect `0x10322b40`: after the caster/target/radius gates, ask the **caster's**
   slot 404 relation toward the target, then target.SameTeam(caster). Same team **or D_LI=3**
   selects friendly benefit; otherwise enemy effect. The relation call precedes SameTeam even
   when team equality alone would suffice. The wider Presence timers, maxima, ranges and visual
   effects remain with the discipline owner. Its recovered predicate has an independent arm
   truth-table test here, without constructing a replacement wider body or a false constant.
4. TeamFilter `0x103426b0`: null candidate -> null character; otherwise take candidate's +0x9c
   self-cast (`0x10342706`), then call SameTeam (`0x10342711`). Decompiled return type is void,
   but assembly preserves AL through scope-stack decrement and RET; the wrapper returns bool.
   It is a reader, not a second table owner.

## Ownership and level lifecycle

Constructor `0x10230750` builds the one game-system manager at `0x10751140`, table +8;
destructor `0x102307b0` destroys it. CTeamManager vtable `0x1048e6dc`:

| Hook | Body | Effect |
|---|---|---|
| slot 2 LevelInitPreEntity | `0x10230820 -> 0x1024b880` | clear before construction/spawn |
| slot 5 LevelShutdownPostEntity | `0x10230860 -> 0x1024b880` | clear after entities are gone |
| slot 3 LevelInitPostEntity | `0x102308f0 -> 0x1024b940` | diagnostic enumeration, no reset |

`0x1024b880`, read before writing: invalidate root, reset live count +0x12, rebuild free nodes,
free every string pool and zero pool count +0x24. The earlier session-long identity claim was
wrong. Manager ownership survives travel; its table contents do not. Headless worlds have one
fallback table per world, with these same boundaries. Worldless inspection probes have no
manager; their AddToTeam answers the named m_TeamSymbol invalid seam, not a per-NPC registry.

## Exact serial integration patches (outside C3's file ownership)

All sites were relocated by function name. Apply these serially; C3 does not edit them.

### C2: spawn/player hooks and shadowing

In `Private/Substrate/ElysiumNpcBaseSpawn.cpp::Spawn19TeamName`, replace the empty return:

```cpp
return TeamName; // 0x10323a90: m_sTeamName +0x10ac.
```

Replace `Spawn19AddToTeam` with:

```cpp
void FElysiumNpcBase::Spawn19AddToTeam(const FString& Name) // 0x103239a0.
{
    ++Spawn19AddToTeamCalls; // Existing observation only.
    AddToTeam(Name); // 0x103239a0 / 0x10323a21.
}
```

In `ElysiumNpcBaseSpawn.inl`, declaration becomes
`void Spawn19AddToTeam(const FString& Name);`. Replace its two stale seam comments with:
`// 0x10323a90 / 0x10298d30: nonempty m_sTeamName joins the shared level table.`
and `// 0x103239a0: real AddToTeam; count retained only for observation.`

**C4458:** in both `ElysiumNpcBaseSpawn.cpp::Spawn19CombatCharacterSpawn` and
`ElysiumNpcSpawn.cpp::TroikaSpawnBody`, replace the local three uses with exactly:

```cpp
const FString RegistrationTeamName = Spawn19TeamName(); // 0x10323a90 / 0x10298d30.
if (!RegistrationTeamName.IsEmpty()) // 0x10323a90 / 0x10298dae.
{
    Spawn19AddToTeam(RegistrationTeamName); // 0x103239a0 / 0x10298db6.
}
```

In `ElysiumPlayerEntity.cpp::Spawn`, after SyncFromBody and before visual installation:
`AddToTeam(TEXT("player")); // 0x1016d260 / 0x1016db7e.`
At the end of successful `Hydrate`, after its restored sheet/effects/discipline work:
`AddToTeam(TEXT("player")); // 0x1016ebd0: after base restore.`
Neither call changes TeamName. Preserve the later ApplyEntityRecord player join below, since
Hydrate may precede the snapshot field walk.

### C1/C2: live consumers

`ElysiumWeaponClasses.cpp::ElysiumSwingSameTeam`, keep the named parameters, replace false:
`return Attacker.IsSameTeam(&Victim); // 0x10323930, at 0x103439a0 after relation.`
Delete its obsolete no-registry commentary. AdvanceSwingContact keeps its existing outer
NPC/debug/distinct-character gates and relation-first short circuit:

```cpp
if ((Relation != 1 && Relation != 2) || ElysiumSwingSameTeam(*Attacker, *VictimCC)) // 0x1034398f..0x103439a7.
```

`ElysiumCombatCharacter.cpp::CombatTeamSymbolOf` must have a named Character parameter and:
`return Character.GetTeamSymbol(); // 0x10323a70.`
`CombatSameTeam` becomes `return Self.IsSameTeam(Other); // 0x10323930.`
Remove the stale no-registry comments and unused GNoTeamSymbol. OnTakeDamage retains
`if (CombatSameTeam(*this, AttackerCharacter) && AttackerEntity != this) // 0x1032ef60.`
before discipline notification/life-state dispatch. No false team constant remains.

### Integrator: one owner and the common world accessor

`Public/ElysiumSessionSubsystem.h`, **before its generated.h include**, add
`#include "Substrate/ElysiumTeamRegistry.h"` (complete type for UObject/TUniquePtr destruction).
Public declaration: `FElysiumTeamRegistry& TeamRegistry() const; // 0x10751140.`
Private member: `TUniquePtr<FElysiumTeamRegistry> TeamRegistryPtr; // 0x10230750 / 0x102307b0.`

`Session/ElysiumSessionSubsystem.cpp` includes `Substrate/ElysiumTeamRegistry.h` and
`Misc/AssertionMacros.h`; in Initialize, immediately after Super::Initialize:
`TeamRegistryPtr = MakeUnique<FElysiumTeamRegistry>(); // 0x10230750.`
In Deinitialize, before Super::Deinitialize:
`TeamRegistryPtr.Reset(); // 0x102307b0.`
Add the single accessor body:

```cpp
FElysiumTeamRegistry& UElysiumSessionSubsystem::TeamRegistry() const // 0x10751140.
{
    check(TeamRegistryPtr); // 0x10230750: manager exists during entity lifetime.
    return *TeamRegistryPtr; // 0x10751140: one table, no copy.
}
```

`Public/ElysiumEntityWorld.h`, forward declaration `class FElysiumTeamRegistry;`;
public `FElysiumTeamRegistry& TeamRegistry() const; // 0x10751140.`;
private `TUniquePtr<FElysiumTeamRegistry> HeadlessTeamRegistry; // 0x10230750: fixture owner.`
`ElysiumEntityWorld.cpp`, include `Substrate/ElysiumTeamRegistry.h` and `Misc/AssertionMacros.h`;
constructor before any entity work:

```cpp
if (!GameState) // 0x10751140: fixtures supply the same single-manager contract.
{
    HeadlessTeamRegistry = MakeUnique<FElysiumTeamRegistry>(); // 0x10230750.
}
```

Add this single body:

```cpp
FElysiumTeamRegistry& FElysiumEntityWorld::TeamRegistry() const // 0x10751140.
{
    if (GameState) // 0x10751140: game-system ownership.
    {
        return GameState->TeamRegistry(); // 0x10751140.
    }
    check(HeadlessTeamRegistry); // 0x10230750: one headless table.
    return *HeadlessTeamRegistry; // 0x10751140.
}
```

Load, first statement before construction/spawn:
`TeamRegistry().LevelInitPreEntity(); // 0x10230820 -> 0x1024b880.`
Teardown, immediately after EntityList.Empty:
`TeamRegistry().LevelShutdownPostEntity(); // 0x10230860 -> 0x1024b880.`
Activate, once after all entity/player registration, before `bActive = true`:
`TeamRegistry().LevelInitPostEntity(); // 0x102308f0 -> 0x1024b940: diagnostics only.`
Check session initialization/deinitialization and outgoing-world teardown order before building:
an old detached world's late shutdown must not erase a new level's already-registered table.
Calls belong to actual level boundaries, never per entity or mid-level record restore.

### Integrator: restore after fields, before consumers

`ElysiumEntityWorldPersistence.cpp::ApplyEntityRecord`, immediately after leaf state has
deserialized and before OnDormancyChanged/OnPostRestore (both can consume state), insert:

```cpp
if (FElysiumCombatCharacter* RestoredCharacter = E->AsCombatCharacter()) // 0x10348890.
{
    if (!RestoredCharacter->TeamName.IsEmpty()) // 0x10348890: restored +0x10ac.
    {
        RestoredCharacter->AddToTeam(RestoredCharacter->TeamName); // 0x10348890 -> 0x103239a0.
    }
    if (E->AsPlayer()) // 0x1016ebd0: successful player restore after base.
    {
        RestoredCharacter->AddToTeam(TEXT("player")); // 0x1016ebd0 / 0x10566574.
    }
}
```

Use/include `ElysiumPlayer.h` for the complete character type. No table reset here. The retail
empty-name branch does nothing; no extra direct symbol writer is invented for that branch.

### Integrator: generation, shape and verdicts

`research/tooling/gen_kernel_bindings.py::CHAIN_MEMBER_MAPS["CBaseCombatCharacter"]`, add:
`0x10AC: ("FElysiumCombatCharacter", "TeamName"),`
Remove the entire CHAIN_UNBOUND entry `( "CBaseCombatCharacter", 0x10AC )` and its stale
"no reader" reason. `classify` already carries replay flags; do not override them. Regenerate
ElysiumNpcKernelBindings.cpp; never hand-edit it. Binding is common-chain, not NPC-only.
Existing shape rows at 0x10ac/0x10b0 already describe string_t/unsigned short correctly. No new
slot declaration/body is needed: these methods/filter are direct calls, so no generated
Slots.cpp is touched. Optional closure verdict rows (tab-separated) for the new hand ports:

```tsv
103239a0	rule	0-4	hand:FElysiumCombatCharacter::AddToTeam	[V4c C3 obs=via: Spawn/Restore and melee/damage team gates] one !; byte normalization registry; WORD store; discarded getter; name unchanged
10323a70	rule	0-4	hand:FElysiumCombatCharacter::GetTeamSymbol	[V4c C3 obs=via: SameTeam] unsigned WORD +0x10b0
10323930	rule	0-4	hand:FElysiumCombatCharacter::IsSameTeam	[V4c C3 obs=via: 10343020/1032ef60/10322b40/103426b0] invalid/null/invalid then equality; low-byte bool
103426b0	rule	0-4	ElysiumTeamFilter	[V4c C3 obs=via: entity candidate filter] +0x9c self-cast then SameTeam
```

## Arm tests and exact consumer assertions owed

C3 adds ten `Elysium.Arm.TeamRegistry.*` tests, deleting none: constructor WORD, name handling,
127-byte including partial UTF-8 truncation, getter/SameTeam/TeamFilter, both clear hooks and
diagnostic preservation, headless world boundaries, generated common-chain KEY|SAVE, base/Troika
and player spawn/player Hydrate, restored names rather than IDs (including different spawn
name and preservation of unrelated live names), and Presence's recovered classification.
The generated/world tests intentionally wait for the serial integration above; they were not run.

C1 owes actual AdvanceSwingContact cases in `Tests/ElysiumWeaponTests.cpp`, with named
same-team NPC hate/fear, different-team control, debug-on, player, self and noncharacter arms.
After the existing live-record setup, attacker/bystander join `!Arena_Melee` / `arena_melee`;
capture wounds and contact-effect/knockback counts before stepping. Exact team assertions:

```cpp
TestTrue(TEXT("0x10323930: valid same-team fixture"), Attacker->IsSameTeam(Bystander));
TestTrue(TEXT("0x10323930: symbol is not invalid"), Attacker->GetTeamSymbol() != uint16(0xffff));
TestEqual(TEXT("0x103439a0: rejected contact changes no wounds"), DamageTaken(*Bystander), WoundsBefore);
TestFalse(TEXT("0x103439a7: rejected contact never enters hit-once state"), Weapon->Swing.RecordHits[RecordIndex].Contains(Bystander->Handle));
TestEqual(TEXT("0x103439a7/0x102579f0: no impact side effects"), ImpactCountAfter, ImpactCountBefore);
TestEqual(TEXT("0x103439a7/0x102579f0: no knockback"), KnockbackCountAfter, KnockbackCountBefore);
```

These are assertions in each owner-created contact fixture, with the shown before/after
observation locals; they are not a mirrored implementation of the contact predicate. The
different-team control must reach RecordHits and impact with positive damage. Debug/player
bypass may still produce zero wounds via the independent damage team gate, so prove contact
admission by hit-once/impact observation, not damage. Self and noncharacter cases must reach
their subsequent ordinary ownership/solid/contact arms, without demanding they damage self.

C2 owes named-attacker gate cases in its damage tests, through actual OnTakeDamage, with
same-team rejection before notification and life-state dispatch. Within its existing fixture
(`Victim`, `Attacker`, `Info`), seed positive nonlethal packet damage and clean health, then:

```cpp
Victim->AddToTeam(TEXT("!damage_team")); // 0x103239a0.
Attacker->AddToTeam(TEXT("DAMAGE_TEAM")); // 0x103239a0.
Info.Attacker = Attacker->Handle; // 0x1032ef60: +0x2c attacker retained.
TestEqual(TEXT("0x1032ef60: named teammate packet refused"), Victim->OnTakeDamage(&Info), 0);
TestEqual(TEXT("0x1032ef60: refusal precedes life-state dispatch"), AliveDispatchesAfter, AliveDispatchesBefore);
TestEqual(TEXT("0x1032ef60: refusal precedes discipline notification"), DisciplineNoticesAfter, DisciplineNoticesBefore);
Attacker->AddToTeam(TEXT("different_damage_team")); // 0x103239a0.
TestTrue(TEXT("0x1032ef60: different-team control damages"), Victim->OnTakeDamage(&Info) > 0);
Info.Attacker = Victim->Handle; // 0x1032ef60: self exception despite same symbol.
TestTrue(TEXT("0x1032ef60: self packet admitted"), Victim->OnTakeDamage(&Info) > 0);
```

The owner supplies dispatch/notification observations, or its recording subclass/sink, rather
than changing packet provenance. Also cover null/noncharacter and two invalid-symbol attackers
as admitted by this particular gate, and teammate dead/dying victims as refused before their
life-state branches. The independent presence truth table is not a claim that wider Presence
is implemented.

## Green Room acceptance predictions (integrator-owned records)

**melee_same_team:** NPC attacker explicitly hates NPC bystander (D_HT, lower priority than
player); no squad; debug_allow_melee_ff=0; team_name keys `!Arena_Melee` / `arena_melee` injected
in staging. Bystander overlaps swing but stands off slot331's centre ray: `0x10347180`'s
ENEMY_BLOCKED gate must not prevent the swing. Assert valid equal symbols. Expect admitted
swing and positive damage to otherwise-identical different-team control. Never same-team
bystander damage, contact-side impact, knockback or **any Swing.RecordHits entry**. Cite
`0x1034394d..0x103439a7`, `0x10323930`, `0x10347180`, `0x102579f0`.
Keep melee_ally_in_the_way as neutral/no-team relation control. If current arena observations
cannot read hit-list/impact/knockback, integrator owes read-only observations; damage alone
also passes when only the outer damage refusal works.

**team_damage_gate:** named gunman's live weapon packet, retained attacker handle, explicitly
hated teammate -> no damage; identical different-team target -> damage; self damage admitted
by predicate. Cite `0x1032ef60` / `0x10323930`. An attackerless scalar TakeDamage cannot prove
this gate. Inject the team_name keyvalues; no map mutation or invented SetTeam input.

No newly proposed gameplay divergence was adopted. Wider Presence belongs to its discipline
story. The remaining work is serial cross-file wiring, consumer arm observations, generated
bindings and the two acceptance records above, not a missing team input or faction subsystem.

### V4c second-pass close measurement (2026-10-05; not green, no commit)

The three-build allowance is exhausted: build1 passed118.6s; build2 failed27.5s on two
fixture ResolveClipPair calls missing the required side argument; build3 passed25.2s.
No source changed after build3; temporary clock/sweep diagnostics were removed before it.
The lanes and all integration remain uncommitted and unstaged on spec-0002/step-2.
An external documentation commit advanced HEAD to947e57b6 (AGENTS/HANDOVER only).
Neither V4c nor V4s was ticked; no commit or push. Root200KB is absent.

Final default:169 executed,168 passed,1 failed (Stance.Driver),15.8s wall.
Final arm:1624 executed,1611 passed,13 failed,53.8s wall; no abort or unrun test.
Baseline default170 loses only the retired world-sweep MeleeBatch test, replaced by arm
character-clock coverage. Baseline arm1594 gains30 cases.
Full Arena ONCE after build3:132 records,110 pass/4 fail/16 expected-fail/2 unexpected-pass,
527.5s wall, report20261005T053609.881342Z. The eight new records all pass.
The earlier33-record named run was28 pass/4 fail/1 expected-fail; record-only cower/footstep
corrections passed by name and in the full run. After the full run, input_setrelationship's
unkillable staging passed by name (20261005T054700.512138Z); the full report still records
its original failure. No second full run. Kernel check7/7; no re-bake or corpus write.

The registry ownership, level hooks, restore joins and common predicates are wired. Flags6
remain SAVE|KEY, with bKeyable false (INPUT8 is absent). Registry construction now also covers
headless session fixtures. Team registry arm cases pass; neither team record is still parked.
melee_same_team proves equal valid symbols, a real admitted swing, no matching-team damage,
impact or hit-list insertion, and positive damage on its otherwise identical negative-Y control.
team_damage_gate proves the real event shot's different-team control and teammate refusal at
0x1032ef60 before life dispatch. The wave remains blocked by the two RNG verdicts and14 failing
unit/default cases documented in triage, not by a later story or a team-symbol guess.
