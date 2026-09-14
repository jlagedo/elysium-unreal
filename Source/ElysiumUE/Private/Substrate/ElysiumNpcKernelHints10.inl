// Story 29d, family **Hints10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it (slot 566). What lands here is the non-slot half — the species arm slot 566 dispatches
// to, the four `CNPC_VWerewolf` hint bodies, and the seams they read through.
//
// The definitions are in `Substrate/ElysiumNpcKernelHints10.cpp`; the tests are
// `Tests/ElysiumNpcKernelHints10Tests.cpp` and the walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Hints10 — `FValidateHintType` and the Werewolf's
// hint endpoints".
//
// --- What this family is --------------------------------------------------------------------------
//
// **Which hint an NPC is allowed to use, and where a hint's far end is.** Seven rows: slot 566's
// Troika-line dispatcher (`0x10295c20`) and its one real species arm (`CNPC_VManBat`, `0x1038e480`);
// the Werewolf's end-entity cache (`0x103d6390`), its endpoint accessor (`0x103d6650`), its forward
// partner search (`0x103d7090`) and its teleport-hint gate (`0x103d8300`); and
// `CNPC_VVampireBoss::SelectHintNode` (`0x103c59d0`), read against the already-ported
// `FElysiumNpc::FindHintNode` and left `present`.
//
// **This family stands no second hint store.** 29c-1's family Hints built the seam
// (`FHintWords`, `HintWords()`, `FindHintByName`, `FindHintNear`) and it is reused unchanged; every
// body here takes an `FHintWords` or a node index and answers retail's own null arm when the seam
// comes back empty. `ElysiumNpcKernelHints.inl`'s `FWerewolfHintGroundpoint` IS retail's `+0x6714`
// record and this family adds the word at `+0x00` that `GetHintEndEntity` reads.

// --- Slot 566 `FValidateHintType` (`0x10295c20`) --------------------------------------------------
//
// The Troika-line body, shared by ~51 census classes. Read at the listing (`10295d80`..`10295e3d`):
// a null hint answers false; otherwise `hint->m_iGroupID (+0x470) & this->m_iHintGroups (+0x62e4)`
// must be non-zero or the body answers false after an `ai_debug_npc`-gated `DevMsg`; and the
// admitted hint is then dispatched on `m_nHintType (+0x5dc)` by numeric range, in retail's order.
//
// `0x10297430` and `0x102974f0` are family Hints' `IsHintCoverValid` / `IsHintCoverValidLoose`;
// `0x10295ed0` and `0x102961a0` are family BaseHelpers' `FUN_10295ed0` / `FUN_102961a0`. All four are
// CALLED here, never restated.

/** One arm of `0x10295c20`'s type dispatch, in the order the listing tests them. */
enum class EHintTypeArm : uint8
{
	/** Every range the switch does not name — the body's `XOR AL,AL` tail. */
	Refuse,
	/** `== 0x27d8` → `0x10297430` `IsHintCoverValid`. */
	CoverValid,
	/** `0x64 <= t <= 0x65` → `0x102974f0` `IsHintCoverValidLoose`. */
	CoverValidLoose,
	/** `== 0x2774` → `MOV AL,1`, an outright accept with no further test. */
	Accept,
	/** `0x283c <= t <= 0x283d` → `0x10295ed0`, the quiet cover validator. */
	QuietCoverRule,
	/** `== 0x28a0` → `0x102961a0`, the verbose cover validator. */
	VerboseCoverRule,
};

/** `0x10295c20`'s type dispatch as a pure function of `m_nHintType`, with the group gate already
 *  passed. Both previously inferred boundaries are confirmed at the listing: `< 0x64` refuses
 *  (`10295d92`), `0x64..0x65` takes the loose validator (`10295d97`), `< 0x283c` refuses
 *  (`10295df2`) and `0x283c..0x283d` takes the quiet one (`10295dfd`). */
static EHintTypeArm FValidateHintTypeArm(int32 HintType);

/** The bit list `0x10295c20`'s debug arm formats — the 1-BASED indices of the set bits of `Mask`,
 *  each rendered with the format at `0x105a1814`, which the pinned image reads as `" %d"`. Retail
 *  walks bits 0..0x1f and appends `sprintf`'s return, so bit 0 prints `" 1"`. */
static FString HintGroupBitList(uint32 Mask);

/** The node-index form of slot 566, for the callers that carry a `ScheduleHost::HintNode`-shaped
 *  index rather than an `FHintWords`. An unresolvable node is retail's null hint: false. */
bool FValidateHintTypeNode(int32 HintNode) const;

/** How many times the group gate refused and the debug arm was reached — the observable half of an
 *  arm whose only other effect is a `DevMsg` this runtime does not emit. */
int32 HintGroupRefusals = 0;

// --- `CNPC_VManBat::FValidateHintType` (`0x1038e480`), slot 566's one real species arm ------------
//
// It REPLACES the base dispatcher rather than extending it: no group gate, no range switch. Only
// hint type 20000 is considered at all; everything else answers false at `1038e5b3`.

/** `+0x6670` — `CNPC_VManBat`'s SCRAMBLED mode word. Retail never reads it plainly: every reader
 *  runs the XOR/AND ladder at `1038e49b`..`1038e4b9` and then `0x1042fbf0` over it. Declared by
 *  retail offset, as family Hints declares the Werewolf's species words. */
uint32 ManBatHintModeWord = 0;    // +0x6670 CNPC_VManBat (walked)

/** `+0x6674` — the plain index the four `%d` templates print. */
int32 ManBatHintIndex = 0;        // +0x6674 CNPC_VManBat (walked)

/** The descramble, verbatim: the caller-side ladder at `1038e49b` and then `0x1042fbf0`, whose whole
 *  body is one more XOR/AND fold. Answers retail's `EAX` before the `DEC`/`CMP 7` range test. */
static uint32 ManBatHintMode(uint32 ScrambledWord);

/** `0x1042fbf0` on its own — `p ^ ((((p & 0x67c8c535) ^ 0xdcb8cc14) + 0x18e71cec) ^ 0x82aa05e1)
 *  & 0x98373aca ^ 0xea3e269c`. Named so the descramble above can be checked one fold at a time. */
static uint32 HintObfuscationFold(uint32 Value);

/** The five name templates, by decoded mode. Mode 1 is copied RAW (a byte loop at `1038e4f7`, no
 *  `sprintf`), 2 and 4 share one format, 3 and 8 have their own, and 5/6/7 and everything outside
 *  1..8 take the default — the jump table at `0x1038e5c0` sends 5, 6 and 7 to the default label.
 *  Every string is the pinned image's, read at its `.rdata` address. */
static FString ManBatHintName(uint32 Mode, int32 Index);

/** `CNPC_VManBat::FValidateHintType` (`0x1038e480`), 317 bytes — slot 566's `CNPC_VManBat` arm.
 *  The built name is matched against the hint's `m_iName` (`+0x26c`) with `__strcmpi`, or with
 *  `__strnicmp` over `strlen-1` characters when the template's last character is `*`. Retail's
 *  EMPTY-NAME arm is reproduced as retail wrote it: a zero-length template compares the hint's NAME
 *  POINTER against zero, so an unnamed hint matches and a named one does not. */
bool ManBatValidateHintType(const FHintWords& Hint) const;

// --- The Werewolf's hint endpoints ----------------------------------------------------------------

/** `CNPC_VWerewolf::GetHintEndEntity` (`0x103d6390`), 306 bytes — the cache in front of
 *  `FindHintEndEntity` (`0x103d6520`, family Hints, already ported and CALLED here).
 *
 *  A linear scan of the `+0x6714` record array (count `+0x6720`, stride `0x48`) for the row whose
 *  HINT word at `+0x04` is this hint and whose cached handle at `+0x00` resolves. **Correction to
 *  the checklist walk**: the hit arm does not return "a second cached handle at the same slot" — it
 *  re-reads `base[i * 0x12]`, which is the SAME word `+0x00`, and re-validates it. The redundancy is
 *  retail's; the answer is the one cached handle, or null when the second read fails. */
int32 GetHintEndEntity(const FHintWords& Hint) const;

/** `CNPC_VWerewolf::GetHintEndpoint` (`0x103d6650`), 211 bytes. A null hint answers the global
 *  `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` fills with `(0,0,0)` — `vec3_origin`. Any other
 *  hint resolves its end entity through `GetHintEndEntity` and copies that entity's `GetAbsOrigin`
 *  (vtable `+0x364`). CENTIMETRES, as every port position is. */
FVector GetHintEndpoint(const FHintWords* Hint) const;

/** The 14 hint types `GetForwardHintForHint` hands straight back, read off the switch at
 *  `103d70dd`. **Correction to the checklist walk**: the run is NOT `0x3a9f..0x3aaa` — `0x3aa2` is
 *  absent from the jump table, so the exempt set is `15000`, `0x3a99`, `0x3a9c`, `0x3a9f`, `0x3aa0`,
 *  `0x3aa1` and `0x3aa3..0x3aaa`. */
static bool IsForwardHintExemptType(int32 HintType);

/** `CNPC_VWerewolf::GetForwardHintForHint` (`0x103d7090`), 274 bytes — the partner hint of type
 *  `0x3a9c` that shares this hint's end entity, or null with retail's `DevWarning`. */
int32 GetForwardHintForHint(const FHintWords& Hint) const;

/** SEAM for the global hint list `DAT_10925450` walked through its `+0x5d8` next link (index
 *  `0x176`), which `GetForwardHintForHint` iterates from the head. There is no hint store on this
 *  substrate — family Hints' standing fact — so this answers an EMPTY list and the search takes
 *  retail's own no-match arm, which warns and answers null. */
TArray<int32> GlobalHintList() const;

/** `CNPC_VWerewolf::IsValidTeleportHint` (`0x103d8300`), 457 bytes — seven ordered refusals and
 *  then the endpoint's own availability. */
bool IsValidTeleportHint(const FHintWords* Hint, double Now) const;

/** The six hint types `IsValidTeleportHint` excludes outright, `0x3aa3..0x3aa8`, each its own `CMP`
 *  in the listing (`103d83c8`..`103d8430`). */
static bool IsTeleportHintExcludedType(int32 HintType);

/** SEAM for `thunk_FUN_100b5190(endEntity)`, the LAST gate of `IsValidTeleportHint`. `0x100b5190` is
 *  a seven-byte getter of `+0xf4`, which `docs/vtmb/npc-kernel/fields.md` names `m_bScriptHidden` —
 *  **a correction to the checklist walk**, which called it an "entity-busy/occupied test". A hint
 *  endpoint this substrate cannot resolve has no `m_bScriptHidden` to read, so this answers FALSE,
 *  which is the ADMITTING value: retail returns the NEGATION, so a not-hidden endpoint is a valid
 *  teleport hint and nothing is silently refused. */
bool HintEndEntityScriptHidden(int32 EndEntityNode) const;
