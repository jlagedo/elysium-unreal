// Story 29d, family **Hints10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it (slot 566). What lands here is the non-slot half — the species arm slot 566 dispatches
// to, the four `CNPC_VWerewolf` hint bodies, and the seams they read through.
//
// The definitions are in `Substrate/ElysiumNpcHints10.cpp`; the tests are
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
// **This family stands no second hint store.** It reads the live hints on the world's list through
// family Hints' view and the base searches (`FHintWords`, `HintWords()`, `FindHintByName`,
// `FindHintNear`); every body here takes an `FHintWords` or a node index and answers retail's own
// null arm when the index names no live hint. `ElysiumNpcHints.inl`'s `FWerewolfHintGroundpoint` IS retail's `+0x6714`
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

/** `0x1042fbf0` on its own — `p ^ ((((p & 0x67c8c535) ^ 0xdcb8cc14) + 0x18e71cec) ^ 0x82aa05e1)
 *  & 0x98373aca ^ 0xea3e269c`. Named so the descramble above can be checked one fold at a time. */
static uint32 HintObfuscationFold(uint32 Value);

// --- The Werewolf's hint endpoints ----------------------------------------------------------------

/** SEAM for the global hint list `DAT_10925450` walked through its `+0x5d8` next link (index
 *  `0x176`), which `GetForwardHintForHint` iterates from the head. There is no hint store on this
 *  substrate — family Hints' standing fact — so this answers an EMPTY list and the search takes
 *  retail's own no-match arm, which warns and answers null. */
TArray<int32> GlobalHintList() const;
