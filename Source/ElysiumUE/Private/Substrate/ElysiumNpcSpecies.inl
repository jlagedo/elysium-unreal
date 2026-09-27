// Story 29c-1, family **Species** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcSpecies.cpp` and the tests in
// `Tests/ElysiumNpcKernelSpeciesTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THIS FAMILY IS THE SPECIES SPREAD ITSELF. Its 60 rows are eight different retail classes, not one
// leaf, and three standing facts follow from that:
//
//   * **One slot, many bodies.** Slots 599, 600, 601, 602, 606 and 609 each already carry a Troika
//     body (family **TroikaHelpers**) and a `CNPC_VAndreiBlood`-line twin; this family's rows are
//     the per-species REPLACEMENTS of those. Each species body lands under its own RETAIL ADDRESS
//     (`FUN_103c19e0`, …) and, since story 5 step 3, is the body of its class's C++ override (the
//     controller line's 599/600 since fold A2, on `FElysiumNpcFrenzyShadow`).
//
//   * **The class answers.** Since story 5 step 2 every living classname builds its own C++ class
//     (`Substrate/ElysiumNpcClasses.cpp`, from retail's factories) and `RetailClass()` is that
//     class's census row, so a spawned `npc_VCop` is `CNPC_VCop` and a spawned `npc_VCamera` is
//     `CNPC_VCamera`. Until then the census gave `CNPC_VCop` no classname and a cop fell through to
//     the Troika line. Step 3 turned these dispatchers into overrides; since commit B the census row
//     is identity only and a type question is `AsSpecies<T>()`.
//
//   * **Two of this family's rows are the makers'.** `CNPCMaker_Fleshpile`'s `MakeNPC`
//     (`0x1034c2d0`) and `DeathNotice` (`0x1034c8e0`) are overrides on `FElysiumNpcMakerFleshpile`
//     since fold A4 (the maker is a Troika NPC). A third, `0x1034b430`, is
//     `FElysiumNpcMaker::IsDepleted`, a `present` re-verdict.

// --- Species words this family's bodies touch ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These are this family's, by retail name and owning class, read off
// each class's own datamap (`vtmb_fields`) rather than guessed. Where another family already
// declared a word at one of these offsets for a DIFFERENT species it is left alone and a separate
// member stands here — the convention families Bosses, Motor, Hints and Squad set.

// `CNPC_VAndreiBlood`'s runner budget. `+0x66b8` is `CNPC_VChangBros::m_ChangType` (family Squad's
// `ChangType`), `CNPC_VManBat::m_bHasPlayedFlyBySound` (family Sounds) and
// `CNPC_VAsianVampire::m_vLastJumpPosition` (family Motor) on three other species; this is the
// fourth reading of the same offset and stands beside them rather than over them.
//
// `m_iActiveRunnerCount` is a datamap `FIELD_INTEGER` that every body touching it reads and writes
// as a FLOAT (`FLD`/`FCOMP`/`FADD` in all four: `0x1035e920`, `0x1034c2d0` twice, `0x1034c8e0`).
// Carried as `int32` because integer is what the datamap declares and what the counter means; the
// bodies below say where retail's float arithmetic is reproduced on it.
int32 ActiveRunnerCount = 0;   // +0x66b8 CNPC_VAndreiBlood::m_iActiveRunnerCount (datamap)
int32 AndreiKillCount = 0;     // +0x66bc CNPC_VAndreiBlood::m_iKillCount (datamap)

/** One row of `CNPC_VNewscaster`'s two story queues — a fixed-stride `0x28` record whose first word
 *  is the story's name and whose `+0x04`/`+0x08` pair, walked in steps of 8 up to `0x20`, are the
 *  four `(dependency, filename)` pairs `0x103a0d50` releases.
 *
 *  Story **29d**, family SpeciesMisc10, added the four pairs and the selected index: `0x103a07f0`'s
 *  tail (`103a098f`..`103a0a0b`) stores at `+0x24` the index of the FIRST version whose `dependency`
 *  is absent, empty, or evaluates non-zero, and `0x103a0670` plays `record + 8 + selected * 8` —
 *  that version's FILENAME. Without them the play body has nothing to choose between, which is why
 *  they land here rather than in a second struct beside this one. */
struct FNewscasterStory
{
	FString Name;

	/** One authored `Version` block: `dependency` at `+0x04 + i*8` and `filename` at `+0x08 + i*8`,
	 *  four of them per record — a fifth is refused with `"Newscaster: too many versions in %s!"`. */
	struct FVersion
	{
		FString Dependency;
		FString Filename;
	};
	TArray<FVersion> Versions;

	/** `+0x24`. `INDEX_NONE` never reaches a queue: a record whose every dependency is false is
	 *  answered `false` by the parser and never appended. */
	int32 SelectedVersion = INDEX_NONE;
};

// `CNPC_VZombie::m_iZombieAIType` — a mapper keyvalue (`ZombieAIType`) whose setter rerolls the
// value 4 into a random 1..3 and side-effects four of the seven values.
int32 ZombieAiType = 0;   // +0x6678 CNPC_VZombie::m_iZombieAIType (datamap, key ZombieAIType)

// `+0x6684` / `+0x6688` are `m_hRotDoor1` / `m_hRotDoor2`, which family **Misc** declares off
// `0x103cade0` (the zone opener that FILLS them). `0x103d1e50` is the READER of the same pair and
// goes through Misc's members rather than standing a second copy.

// --- The seams this family stands ------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. Nothing below invents a value.

/** SEAM for `thunk_FUN_1025de90(coord, this)` reached through slot 602's SPECIES arm
 *  (`0x103c1b10`, `0x103c3ab0`). Family **TroikaHelpers** already stands this exact retail body as
 *  `MeleeCoordinatorHoldsMe()`; it is CALLED rather than restated, so the two cannot drift. Named
 *  here only so the two species bodies below have the call site recorded. */

/** `(**(code **)(*DAT_10924a1c + 4))()` and `DAT_10924a1c[10]` — the melee-range ConVar
 *  (`debug_melee_advance_combatmove_dist`, "100") the species slot-602 bodies threshold
 *  `m_flEnemyDist` against, read through TroikaHelpers' `MeleeRangeUnits()`. */

/** SEAM for `thunk_FUN_10289ee0(this, 1)` — `RestartIdealActivity(1)`, which
 *  `CNPC_VTzimisceRunner`'s slot 588 calls UNCONDITIONALLY where the base override
 *  (`0x10293e50`) gates it on `IsActivityFinished`. Family **Hints** already stands the same retail
 *  body as `RestartIdealActivity()`; it is called here and records the call. */

// --- Slot 323's four answers -----------------------------------------------------------------------

// --- Retail's NON-VIRTUAL call back down to the base body --------------------------------------------
//
// Several species bodies end in a call to the very body they replace: `CNPC_VBach`'s slot 606
// delegates to `thunk_FUN_102b8320`, `CNPC_VTzimisce`'s 593 runs `thunk_FUN_1029a070` FIRST and then
// overwrites what it wrote, `CNPC_VZombie`'s 510 tails into `CAI_BaseNPC::ShouldPlayFloatSound`.
// Every one of those calls is a DIRECT call in retail — a `thunk_`, never a vtable dispatch. Since
// story 5 step 3 the species bodies are their classes' C++ overrides and those calls are spelled as
// qualified calls to the recovered owner (`FElysiumNpc::Slot606(Arg)`, `FElysiumNpcBase::ShouldPlayFloatSound()`;
// the full list is `docs/vtmb/npc-kernel/direct-calls.tsv`), so the per-slot guard that emulated the
// thunk is gone.

// --- The bodies, by retail address ------------------------------------------------------------------
//
// The naming is 29c's overlay target verbatim, so the ledger's `hand:` claim is checkable. A body
// whose retail name IS recovered says so in its definition comment; none of these has one.


/** `0x103e0980` — `CNPC_VZombie::SetZombieAIType`. */
void FUN_103e0980(int32 InZombieAiType);

