// Story 29c-1, family **Senses** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcSensesBodies.cpp` and the tests in
// `Tests/ElysiumNpcKernelSensesTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// --- What this family is --------------------------------------------------------------------------
//
// The bodies that answer "what do I perceive, who is my enemy, and what did I witness": the two
// aim cones, the two enemy accessors, the enemy-memory pair, `OnListened`, the two witness-record
// setters, the occlusion-edge state machine and the species overrides of `FVisible` /
// `FInViewCone` / `QuerySeeEntity` / `PassesFindEntityFOVTrace`. 26 rows; NINE of them fill
// Troika-line vtable slots and are DEFINED (not declared) here — 86, 167, 168, 196, 364, 365,
// 470, 541 and 543.
//
// THE STANDING FACT OF THIS FAMILY: **four of the six species overrides are not senses at all,
// they are the two debug ConVars.** `CNPC_VWerewolf#201`, `CNPC_VYukie#201` and `CNPC_VYukie#363`
// replace the whole LOS/cone test with `npc_ignore_senses` / `npc_ignore_player`
// (`DAT_10924fba` / `DAT_10924fb9`, `ElysiumNpcSense::IgnoreSenses` / `IgnorePlayer`) and answer
// TRUE for everything else — a werewolf has no view cone and no line-of-sight check. The shared
// gate is `SpeciesStealthSenseGate` and every one of the three arms it feeds is recovered, not
// guessed.
//
// THE SECOND STANDING FACT: **29c named three of these rows wrong, and the decompiled C says so.**
// `0x1027de00` is not `SetEnemy` — it writes `m_hBlockedDoor` (`+0x5d28`) and dispatches slot 532
// with retail's door-blocked reason `2` (`npc-kernel/signatures.md`, slot 532), so it is
// `OnDoorBlocked`. `0x101aaf80` runs no FOV cone — `0x10240250` is a six-term AABB overlap.
// `0x1036a030` does not add a gate on top of the base `QuerySeeEntity`, it REPLACES it: the whole
// body is `candidate->m_pPlayer != NULL`. Each is stated again at its definition.

// --- Words and seams this family needs ------------------------------------------------------------

// --- Non-slot bodies ------------------------------------------------------------------------------

/** The gate `CNPC_VWerewolf::FVisible` (`0x103cb810`), `CNPC_VYukie::FVisible` (`0x103ddaf0`) and
 *  `CNPC_VYukie::FInViewCone` (`0x103ddaa0`) share, in retail's order: a null candidate fails;
 *  `DAT_10924fba` (`npc_ignore_senses`) set fails; `DAT_10924fb9` (`npc_ignore_player`) set AND the
 *  candidate being the player fails. Nothing else. */
bool SpeciesStealthSenseGate(const FElysiumEntity* Candidate) const;

/** `0x1028ea60` — the CRIMINAL witness record, written whole: the witnessed level (obfuscated into
 *  `+0x6364`), the three-float location (`+0x6380`) and the offender handle (`+0x638c`, or `-1`
 *  for no entity). `0x1028eb30` below is its supernatural twin. */
void RecordCriminalWitness(int32 Level, const FVector& AtCm, const FElysiumEntity* Offender);

/** `0x1028eb30` — the SUPERNATURAL witness record: the level PLAIN at `+0x6368`, the location at
 *  `+0x6374`, the offender at `+0x6390` and `m_bPLSupernaturalActFleeOnly` at `+0x6394`, which is
 *  written on BOTH arms. */
void RecordSupernaturalWitness(int32 Level, const FVector& AtCm, const FElysiumEntity* Offender,
	bool bFleeOnly);

/** `m_iPLCriminalLevelWitnessed` (`+0x635c`) is a `custom` datamap type — a `CSecureType` whose
 *  payload lives at `+0x6364` scrambled. `0x1028ea60` writes it through `0x1042fde0` and
 *  `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) reads it back through `0x1042fe90`; the two
 *  round-trip. Ported as a pair of pure functions so the recovered constants are in the tree and
 *  checkable, while the port's own `FElysiumNpcWitnessChannel::Level` carries the plain number —
 *  the obfuscation is anti-tamper, not behaviour, and nothing the bytecode runs can observe it. */
static uint32 EncodeWitnessedLevel(uint32 Level);

/** `+0x6360` and `+0x6361`, the two bytes `0x1028ea60` writes beside the scrambled level.
 *  **RECOVERED RETAIL DEFECT**: the listing (`1028ea72` `MOV DL,[ESP+0x8]`, `1028eaa9`
 *  `MOV CL,[ESP+0x5]`) reads them out of the eight bytes `SUB ESP,0x8` just allocated and nothing
 *  ever writes — they are uninitialised stack. Carried so the write is not silently dropped; this
 *  runtime writes 0, which is the one value an indeterminate read cannot be reproduced as. */
uint8 CriminalWitnessByte6360 = 0;
uint8 CriminalWitnessByte6361 = 0;

