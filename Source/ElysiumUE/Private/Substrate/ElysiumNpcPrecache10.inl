// Story 29d, family **Precache10** — the declarations of slot 104's non-slot half.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. Slot 104
// `Precache` (`0x10298ad0`) is that one virtual; everything below is the half the generator does
// not declare — the base body beside it, the per-species arms, the two helpers they share and the
// seams they go through.
//
// The definitions are in `Substrate/ElysiumNpcPrecache10.cpp` and the tests in
// `Tests/ElysiumNpcKernelPrecache10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family Precache10 — slot 104".
//
// --- What this family is -------------------------------------------------------------------------
//
// **Twenty-two species overrides of one slot.** The family's rows fill two
// retail functions on the NPC line — `CAI_BaseNPC::Precache` and
// `CAI_BaseNPCTroika::Precache` `0x10298ad0`, the latter owning slot 104 — plus the census's
// twenty-five distinct species bodies of that one slot and three `CNPCMaker*` bodies, which are the
// makers' own overrides (`Substrate/ElysiumNpcMaker*.h`, story 5 fold A4). Six of the
// twenty-five sit on classes with no instance in the install (`CNPC_Crow`, the three generic
// classes, `CNPC_VTest`, `CGenericNPC`) and carry no port arm; the other nineteen and the three
// maker rows are the twenty-two arms.
//
// Each arm is its class's `Precache` override (story 5 step 3; the makers' since fold A4), and a
// subclass that shares its base's body inherits the override, which is what makes the three Chang
// forms (`CNPC_VChangBros`, `…Blade`, `…Claw`, all three carrying one body) and the two camera
// forms (`CNPC_VCamera`, `CNPC_VCameraSecurity`, both `0x103689c0`) one override apiece.
//
// --- Three standing facts, stated once ------------------------------------------------------------
//
//   * **The chain is a chain, not a merge.** Most species bodies do their own work and then call
//     `CAI_BaseNPCTroika::Precache` `0x10298ad0`, which itself calls `CAI_BaseNPC::Precache`.
//     Every one of those calls is a DIRECT `thunk_`, never a vtable dispatch, so it
//     can never re-enter the species body; the port spells it as a direct call to `TroikaPrecache`
//     (story 5 step 3). Six arms chain the base
//     LAST rather than first and one chains it FIRST and then hard-codes a model; the order is the
//     recovered fact and is reproduced per arm.
//   * **A precache is asset acquisition, which Unreal owns** (0019/6, verdict `mechanism`, service
//     `Bake`). Residency is entity-derived for the whole map epoch
//     (`FElysiumMapActor::PreparePropAndWieldModels`) and assets are baked package references, so
//     a body that only requested assets is gone. What a body still carries is every word it WRITES
//     that a later rule reads (a model name, a stored index, a coordinator bind). `IssuePrecache`
//     / `PrecacheLog` remain only for the callers outside this family that still record a request.
//   * **`DAT_105399a0` is the one-character string `"0"`.** The two-byte `REPE CMPSB` that 229
//     bodies run against it is retail's "is this keyfield the authored none sentinel" test, and
//     this runtime already spells it `ElysiumNpcLoadout::IsNoneSentinel`. `m_spawnEquipment` and
//     `m_altEquipment` are precached only when they are neither null nor `"0"`.

// --- The three `CBaseEntity` words slot 104's Werewolf arm writes ---------------------------------
//
// `+0x00b4 m_iVSoundGroup`, `+0x00bc m_iVSoundTableIdx` and `+0x00c0 m_iszVSoundGroup` are
// `CBaseEntity` words and live there since L0-r007 (`FElysiumEntity::VSoundGroup` / `VSoundTableIdx` /
// `SoundGroup`, with `VSoundGroupFemale` beside them; `ElysiumEntityVSound.cpp` ports slot 71 and the
// getters). The Werewolf and the two Zombie-line `SetModel` bodies write them through the base
// entity's `VSoundGroupIndexFor`, retail's `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, 0)`.

// --- `CNPC_VMingXiaoTentacle`'s three model indices ----------------------------------------------
//
// `+0x6664 m_iModeIndexTentacleToGrub`, `+0x6668 m_iModeIndexGrub` and `+0x666c
// m_iModeIndexGrubToProxy` (`ElysiumNpcKernelShape.cpp`) — the three `PrecacheModel` return values
// `0x1039c220` stores, and the state its later mode-change bodies read. Declared by retail class,
// as family Lifecycle declares its species words, because all three offsets mean something else on
// another class (`+0x6664` alone is `CNPCMaker::m_flSpawnFrequency`, `CNPC_VCop::m_hPursuitPlayer`
// and six more).

// --- The recorded request ------------------------------------------------------------------------

// --- The two base bodies --------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Precache` (`0x10298ad0`) — slot 104's own body, without the species
 *  prologue. `Precache()` is the slot; this is what it runs on the Troika line, and what a species
 *  class's override calls directly.
 *
 *  Since 0019/6 only the body's WRITES run, in retail's order: the model keyfield falls back to
 *  `"models/error/error.mdl"` when unset or empty; `+0x64e8` takes the disposition-table row
 *  (`EnsureStanceResolved`); slot 608 is dispatched with `"Normal"`. The requests between them
 *  (`PrecacheModel` + slot 10, `m_altEquipment` unless `"0"` / `"item_w_unarmed"`, the chained
 *  `CAI_BaseNPC::Precache`, the two `sound/character/<dialog>` globs) are the bake's. */
void TroikaPrecache();

/** `0x101d0f10`, the directory glob, spelled once for its three call sites (`0x10298ad0` twice,
 *  `CNPC_VNewscaster` twice, `CNPC_VWerewolf` twice).
 *
 *  Retail, arm by arm: a null directory answers 0; an EMPTY directory answers 0; a directory whose
 *  first three characters match `DAT_105a0410` or whose first character matches `DAT_105a040c`
 *  answers 0; otherwise `FindFirst`/`FindNext` walk `"<dir>/*<ext>"` and every hit that is not
 *  itself a directory is precached as a sound under `"*%s/%s"` or `"%s/%s"` of `dir + 6` and the
 *  hit's name, with `Flag` as the precache flag. The return is the number of hits precached.
 *
 *  **Unrecovered:** the two reject literals. `DAT_105a0410` is three characters and `DAT_105a040c`
 *  one, and the corpus holds neither's bytes; both are carried as empty constants at the definition,
 *  which makes the gate refuse nothing — and no call site in this family passes a directory either
 *  could match. **SEAM:** this runtime enumerates no `sound/` tree at kernel level, so the walk
 *  finds nothing and the op is recorded whole (directory, extension, both flags) instead. */
void PrecacheDirectory(const FString& Directory, const TCHAR* Extension, bool bStarPrefix,
	int32 Flag);

/** SEAM for `FUN_10207e60`'s model name — `GetCharTemplate(this)` (`0x10207c40`) then the
 *  template's `+0x78` (`0x101d4f20`), which `FUN_10207e60` hands to `PrecacheModel` with preload 0.
 *  Which template column `+0x78` is has no recovered name, and this runtime's template records
 *  expose none, so this answers the empty string — which is also what retail precaches when the
 *  template's pointer is null (`DAT_106b8540`).
 *
 *  **No port caller today.** Its one port reader was the dead `CGenericSabbat_NPC::Precache`
 *  (`0x1035b5d0`), removed by 0019 story 5 step 1. Its live retail caller is
 *  `CAI_BaseNPCTroika::Spawn` (`0x10298d30`), whose call to `FUN_10207e60` the port's `Spawn` does
 *  not make yet; the seam stays as that unported rule's named input. */
FString CharTemplateModelName() const;

// --- The species arms ----------------------------------------------------------------------------
//
// Since 0019/6 a species override survives only where it WRITES a word a later rule reads:
// `CNPC_VMingXiaoTentacle` (`0x1039c220`, its fallback model and the three mode indices) and
// `CNPC_VCamera` (`0x103689c0`, the null-model fallback and the group clear). The override calls
// `TroikaPrecache()` (the direct call retail makes into `0x10298ad0`). Every body that only
// requested assets is gone (service `Bake`); `CNPC_VBach` and `CNPC_VWerewolf` sit outside this
// family's files and still carry theirs.

// --- The slot-104 arm story 29c-1 ported and left unwired ----------------------------------------
//
// The body is family **Lifecycle**'s and is CALLED, not re-recovered. Slot 104 was a generated stub
// when it landed, so nothing ran it; this arm is the wiring, plus the tail `CameraPrecacheModel`'s
// own comment names as "a later story's" — which is this one.

