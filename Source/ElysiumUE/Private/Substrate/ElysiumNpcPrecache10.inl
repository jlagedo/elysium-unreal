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
// **One port method with twenty-two arms, not twenty-two methods.** The family's rows fill two
// retail functions on the NPC line — `CAI_BaseNPC::Precache` `0x1027bb50` and
// `CAI_BaseNPCTroika::Precache` `0x10298ad0`, the latter owning slot 104 — plus the census's
// twenty-five distinct species bodies of that one slot and three `CNPCMaker*` bodies that land on
// `FElysiumNpcMaker::Precache` instead (`Substrate/ElysiumNpcMaker.h`), because this port models
// makers as a separate type and an `FElysiumNpc` arm would never run on one. Six of the
// twenty-five sit on classes with no instance in the install (`CNPC_Crow`, the three generic
// classes, `CNPC_VTest`, `CGenericNPC`) and carry no port arm; the other nineteen and the three
// maker rows are the twenty-two arms.
//
// A species override is an `OverrideOf(RetailClass(), 104)` case inside slot 104's one port
// method, keyed through `Substrate/ElysiumNpcKernelClassLookup.h` — the convention story 29c-1 set
// for the 57 addresses that land on `FElysiumNpc::SquadSlotName`. The dispatch keys on the
// override row's RETAIL ADDRESS rather than on the class name, which is what makes the three Chang
// forms (`CNPC_VChangBros`, `…Blade`, `…Claw`, all three carrying `0x1036ae60`) and the two camera
// forms (`CNPC_VCamera`, `CNPC_VCameraSecurity`, both `0x103689c0`) one arm apiece instead of five.
//
// --- Three standing facts, stated once ------------------------------------------------------------
//
//   * **The chain is a chain, not a merge.** Most species bodies do their own work and then call
//     `CAI_BaseNPCTroika::Precache` `0x10298ad0`, which itself calls `CAI_BaseNPC::Precache`
//     `0x1027bb50`. Every one of those calls is a DIRECT `thunk_`, never a vtable dispatch, so it
//     can never re-enter the species body; the port spells it as a direct call to `TroikaPrecache`
//     (story 5 step 3). Six arms chain the base
//     LAST rather than first and one chains it FIRST and then hard-codes a model; the order is the
//     recovered fact and is reproduced per arm.
//   * **A precache is asset acquisition, which Unreal owns.** This substrate has no per-entity
//     precache entry point at all — residency is entity-derived for the whole map epoch
//     (`FElysiumMapActor::PreparePropAndWieldModels`, "as retail's per-entity Precache was"). So
//     every request is RECORDED in `PrecacheLog` in retail's own order, with the engine entry it
//     went through and the flag it carried, and `IssuePrecache` is the seam that would acquire it.
//     The recorded order and flags ARE the recovered body; the acquisition is the part Unreal has
//     already done by the time an NPC stands.
//   * **`DAT_105399a0` is the one-character string `"0"`.** The two-byte `REPE CMPSB` that 229
//     bodies run against it is retail's "is this keyfield the authored none sentinel" test, and
//     this runtime already spells it `ElysiumNpcLoadout::IsNoneSentinel`. `m_spawnEquipment` and
//     `m_altEquipment` are precached only when they are neither null nor `"0"`.

// --- The three `CAI_BaseNPCTroika` words slot 104's Werewolf arm writes ---------------------------
//
// `+0x00b4` / `+0x00bc` / `+0x00c0` are base words the port's shape map does not bind: family
// **Sounds10** records that nothing in this runtime writes `m_iVSoundTableIdx` (`+0x00bc`) and that
// the VSound concept list is never parsed. `CNPC_VWerewolf::Precache` (`0x103cb2a0`) is the one
// body in the whole kernel closure that writes all three, so they are declared here — by offset and
// retail name, the way family Lifecycle declares its unbound words.

int32 VSoundGroupRow = 0;      // +0x00b4 m_iVSoundGroup — what `0x101f55a0` answered
int32 VSoundTableIndex = 0;    // +0x00bc m_iVSoundTableIdx — the Werewolf writes the literal 2
FString VSoundGroupName;       // +0x00c0 m_iszVSoundGroup — `"Werewolf"`, NULL when the literal is empty

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
 *  In retail's order: the model keyfield falls back to `"models/error/error.mdl"` when unset or
 *  empty; the keyfield is `PrecacheModel`d and the returned index handed to slot 10
 *  `SetModelIndex`; `m_altEquipment` (`+0x1a98`) is `UTIL_PrecacheOther`d unless it is `"0"` or the
 *  literal `"item_w_unarmed"`; `CAI_BaseNPC::Precache` is chained; a set `m_iDialog` (`+0x0128`)
 *  builds `"sound/character/<dialog>"`, **chops FOUR characters off the end**, lowercases it and
 *  glob-precaches that directory twice, `.wav` then `.mp3`; `+0x64e8` takes the disposition-table
 *  row index; and slot 608 is dispatched with `"Normal"`. */
void TroikaPrecache();

/** `"sound/character/%s"` built from `m_iDialog`, chopped and lowercased — the directory
 *  `0x10298ad0` globs. Pure, so the chop is stated without standing a world.
 *
 *  The chop is `buffer[strlen(buffer) - 4] = 0`, read out of the listing at `10298bfc`
 *  (`NOT ECX / DEC ECX / SUB EDX,0x4 / MOV [ECX+EDX],AL`, with `EDX` the buffer): **four**
 *  characters, not the five the checklist's walk claims. `Q_strnlwr` then lowercases the chopped
 *  string over its NEW length. A dialog name shorter than four characters writes the NUL BEFORE the
 *  buffer — retail's own out-of-bounds write, which this port does not reproduce; see the
 *  definition. */
static FString DialogueSoundDirectory(const FString& Dialog);

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

/** SEAM for `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, 0)` — the VSound group-table row
 *  `CNPC_VWerewolf::Precache` stores into `m_iVSoundGroup` (`+0x00b4`). Family **Sounds10**
 *  recovered that this runtime parses no VSound concept list at all and takes retail's own
 *  count-zero miss, so this answers the same miss: `INDEX_NONE`. */
static int32 VSoundGroupRowFor(const TCHAR* GroupName);

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
// One per distinct retail body, named after the class the census names it on and carrying that
// class's `0x10……` address at the definition. Each is the slot-104 override on its species' C++
// class (story 5 step 3); a body that wants the base calls `TroikaPrecache()` (the Troika body,
// the direct call retail makes into `0x10298ad0`) or `FElysiumNpcBase::Precache()` (`CAI_BaseNPC`'s, which a
// `CAI_BaseNPC`-line class chains instead).
//
// WHERE EACH ONE CHAINS, because it is the fact that separates them: **first** for Andrei Blood,
// the Asian Vampire, Bach, the Chang brothers, the Gargoyle, the Ghoul Croucher, the Hengeyokai,
// the ManBat, Ming Xiao, the Newscaster, the Sabbat Leader, the Sheriff, the Tzimisce Head Claw,
// the Tzimisce Runner, the Werewolf and the Zombie; **last** for `CNPC_VTzimisce`; and **after its
// own model fallback** for `CNPC_VMingXiaoTentacle`.

// --- The slot-104 arm story 29c-1 ported and left unwired ----------------------------------------
//
// The body is family **Lifecycle**'s and is CALLED, not re-recovered. Slot 104 was a generated stub
// when it landed, so nothing ran it; this arm is the wiring, plus the tail `CameraPrecacheModel`'s
// own comment names as "a later story's" — which is this one.

