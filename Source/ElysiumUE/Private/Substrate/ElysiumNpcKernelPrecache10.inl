// Story 29d, family **Precache10** — the declarations of slot 104's non-slot half.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. Slot 104
// `Precache` (`0x10298ad0`) is that one virtual; everything below is the half the generator does
// not declare — the base body beside it, the per-species arms, the two helpers they share and the
// seams they go through.
//
// The definitions are in `Substrate/ElysiumNpcKernelPrecache10.cpp` and the tests in
// `Tests/ElysiumNpcKernelPrecache10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family Precache10 — slot 104".
//
// --- What this family is -------------------------------------------------------------------------
//
// **One port method with twenty-six arms, not twenty-six methods.** Twenty-eight `rule` rows fill
// exactly two retail functions on the NPC line — `CAI_BaseNPC::Precache` `0x1027bb50` and
// `CAI_BaseNPCTroika::Precache` `0x10298ad0`, the latter owning slot 104 — plus twenty-three
// species overrides of that one slot and three `CNPCMaker*` bodies that land on
// `FElysiumNpcMaker::Precache` instead (`Substrate/ElysiumNpcMaker.h`), because this port models
// makers as a separate type and an `FElysiumNpc` arm would never run on one.
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
//     can never re-enter the species body. `FSpeciesDispatchScope` (story 29c-1's ported thunk:
//     while slot N's species body runs, slot N's dispatcher answers "no species body") is what
//     makes a species arm's own call to `Precache()` that direct call. Six arms chain the base
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

int32 ModeIndexTentacleToGrub = 0;   // +0x6664 CNPC_VMingXiaoTentacle
int32 ModeIndexGrub = 0;             // +0x6668 CNPC_VMingXiaoTentacle
int32 ModeIndexGrubToProxy = 0;      // +0x666c CNPC_VMingXiaoTentacle

// --- The recorded request ------------------------------------------------------------------------

/** The engine entry one precache request goes through. Retail reaches four different functions and
 *  WHICH one is part of the recovered body — a model index is returned and stored, a particle
 *  system is not, and `UTIL_PrecacheOther` spawns a whole entity to run ITS slot 104. */
enum class EPrecacheChannel : uint8
{
	/** `(*DAT_1070b22c)+0x34` — `IVEngineServer::PrecacheModel(name, preload)`. Answers a model
	 *  index, which three bodies in this family store. */
	Model,
	/** `(*DAT_1070b248)+0x00` — `CSoundEmitterSystem::PrecacheScriptSound(name, flag)`. */
	Sound,
	/** `(*DAT_1070b22c)+0x44` — the particle-system precache. `Preload` is 1 on the boss emitters
	 *  and 0 on Ming Xiao's, the Hengeyokai's and the Zombie's, and that split is retail data. */
	Particle,
	/** `UTIL_PrecacheOther` `0x101d0ec0` — create the entity by classname, dispatch ITS vtable
	 *  `+0x1a0` (slot 104), then remove it. A classname that creates nothing prints
	 *  `"NULL Ent in UTIL_PrecacheOther: %s"`. */
	Other,
	/** `0x101d0f10` — glob `"<dir>/*<ext>"` through the filesystem and precache every non-directory
	 *  hit as a sound. See `PrecacheDirectory`. */
	Directory,
};

/** One request, as retail issued it. */
struct FPrecacheOp
{
	EPrecacheChannel Channel = EPrecacheChannel::Model;
	/** The name retail pushed, verbatim. A null `string_t` reads as the empty string
	 *  (`DAT_106b8540`), so an empty name here is retail's own argument and not a gap. */
	FString Name;
	/** The engine call's second argument: the preload flag for `Model` and `Particle`, the sound
	 *  flag for `Sound`, and `0x101d0f10`'s FOURTH argument for `Directory`. */
	int32 Flag = 0;
	/** `Directory` only: the extension pattern (`".wav"` `DAT_10598a30` or `".mp3"`
	 *  `DAT_10548ed4`). */
	FString Extension;
	/** `Directory` only: `0x101d0f10`'s THIRD argument, which picks the name format each hit is
	 *  precached under — `"*%s/%s"` when set, `"%s/%s"` when clear, both over `dir + 6` (retail
	 *  skips the literal `"sound/"` the directory always opens with). The Troika body passes 1 and
	 *  the Werewolf passes 0, which is a real difference between two call sites of one function. */
	bool bStarPrefix = false;
};

/** Every request this NPC's precache issued, in retail's order. Never cleared by a body: retail's
 *  precaches are cumulative on the engine side and a second `Precache()` appends, as retail's
 *  second call would. */
TArray<FPrecacheOp> PrecacheLog;

/** SEAM — the acquisition half of every request above, and the one place a future asset path hooks
 *  in. It records the op and does nothing else.
 *
 *  There is no per-entity precache entry point in this substrate to route to: model residency is
 *  the map epoch's, derived from the entity defs before any NPC stands
 *  (`FElysiumMapActor::PreparePropAndWieldModels`, whose own comment says "residency is
 *  entity-derived, as retail's per-entity Precache was"), sounds are baked soundscripts the bake
 *  resolves by name, and this runtime stands no Source particle systems at all. So the four engine
 *  entries named on `EPrecacheChannel` have no callee here and this answers NOTHING for all four.
 *  What it does carry is the recovered half — the name, the channel, the flag and the ORDER. */
void IssuePrecache(const FPrecacheOp& Op);

// --- The two base bodies --------------------------------------------------------------------------

/** `CAI_BaseNPC::Precache` (`0x1027bb50`), slot 104's body on the nine `CAI_BaseNPC`-line classes
 *  and the tail `0x10298ad0` chains into. A DISTINCT retail function beside the Troika override, so
 *  it is ported under its own name.
 *
 *  Three steps, in order:
 *    1. `m_spawnEquipment` (`+0x5dec`) goes through `UTIL_PrecacheOther` when it is non-null AND is
 *       not the one-character sentinel `"0"` (`DAT_105399a0`).
 *    2. slot 452 `LoadedSchedules` (vtable `+0x710`) decides the rest. **False** prints
 *       `DevMsg("ERROR: Rejecting spawn of %s as error in NPC's schedules.\n", GetDebugName())`,
 *       `UTIL_Remove`s the entity and **returns without chaining the base at all**.
 *    3. True falls through to `CBaseCombatCharacter::Precache`.
 *
 *  The port answers `LoadedSchedules` **true always by design** (`ElysiumNpcKernelSchedule.cpp:297`
 *  — nothing here parses schedule text, so no flag can be cleared), so the reject arm is
 *  UNREACHABLE in this runtime today. It is ported anyway, because the day a schedule parser lands
 *  the arm is what a malformed schedule reaches, and a test drives it through the same seam.
 *
 *  `CBaseCombatCharacter::Precache` (`0x10011324`) is `CBaseCombatCharacter::PrecacheOnce`'s
 *  once-guarded global block — the discipline, damage-effect and HUD emitters every character
 *  shares. It is NOT an NPC-kernel row and has no port body; see the definition. */
void BasePrecache();

/** `CAI_BaseNPCTroika::Precache` (`0x10298ad0`) — slot 104's own body, without the species
 *  prologue. `Precache()` is the slot; this is what it runs when no species arm claims it, and what
 *  a species arm's own chain call reaches through `FSpeciesDispatchScope`.
 *
 *  In retail's order: the model keyfield falls back to `"models/error/error.mdl"` when unset or
 *  empty; the keyfield is `PrecacheModel`d and the returned index handed to slot 10
 *  `SetModelIndex`; `m_altEquipment` (`+0x1a98`) is `UTIL_PrecacheOther`d unless it is `"0"` or the
 *  literal `"item_w_unarmed"`; `CAI_BaseNPC::Precache` is chained; a set `m_iDialog` (`+0x0128`)
 *  builds `"sound/character/<dialog>"`, **chops FOUR characters off the end**, lowercases it and
 *  glob-precaches that directory twice, `.wav` then `.mp3`; `+0x64e8` takes the disposition-table
 *  row index; and slot 608 is dispatched with `"Normal"`. */
void TroikaPrecache();

// --- Slot 104's species dispatch ------------------------------------------------------------------

/** The species prologue at the top of `Precache()`. True means a species body ran and the Troika
 *  body must NOT — which is what a vtable dispatch to an override does.
 *
 *  Keyed on the override row's retail ADDRESS, so one arm serves every class that shares a body.
 *  A test asserts the arm table covers every slot-104 override row the census carries, which is
 *  what keeps an unlisted class from silently falling through to the Troika body. */
bool PrecacheSpecies();

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
 *  template's `+0x78` (`0x101d4f20`). `CGenericSabbat_NPC::Precache` opens by precaching it.
 *  Which template column `+0x78` is has no recovered name, and this runtime's template records
 *  expose none, so this answers the empty string — which is also what retail precaches when the
 *  template's pointer is null (`DAT_106b8540`). */
FString CharTemplateModelName() const;

// --- The twenty-three species arms ---------------------------------------------------------------
//
// One per distinct retail body, named after the class the census names it on and carrying that
// class's `0x10……` address at the definition. `PrecacheSpecies` is what selects one; a body that
// wants the base calls `Precache()` (the Troika body, reached through `FSpeciesDispatchScope`) or
// `BasePrecache()` (`CAI_BaseNPC`'s, which the four `CAI_BaseNPC`-line classes chain instead).
//
// WHERE EACH ONE CHAINS, because it is the fact that separates them: **first** for Andrei Blood,
// the Asian Vampire, Bach, the Chang brothers, the Gargoyle, the Ghoul Croucher, the Hengeyokai,
// the ManBat, Ming Xiao, the Newscaster, the Sabbat Leader, the Sheriff, the Tzimisce Head Claw,
// the Tzimisce Runner, the Werewolf and the Zombie; **last** for `CGeneric_NPC`,
// `CGeneric_NPC_bathack`, `CGenericSabbat_NPC`, `CNPC_VTest` and `CNPC_VTzimisce`; **first, and
// then a hard-coded model** for `CNPC_Crow`; **after its own model fallback** for
// `CNPC_VMingXiaoTentacle`; and **not at all** for `CGenericNPC`.

void CrowPrecache();                  // 0x10358ec0
void GenericNpcTroikaPrecache();      // 0x10359f70 — CGeneric_NPC, npc_generic
void GenericNpcBathackPrecache();     // 0x1035ade0
void GenericSabbatNpcPrecache();      // 0x1035b5d0
void AndreiBloodPrecache();           // 0x1035cb90
void AsianVampirePrecache();          // 0x10360bc0
void BachPrecache();                  // 0x103637b0
void ChangBrosPrecache();             // 0x1036ae60 — and the Blade and Claw forms
void GargoylePrecache();              // 0x10378470
void GhoulCroucherPrecache();         // 0x1037b1a0
void HengeyokaiPrecache();            // 0x1037f960
void ManBatPrecache();                // 0x1038aec0
void MingXiaoPrecache();              // 0x10392660
void MingXiaoTentaclePrecache();      // 0x1039c220
void NewscasterPrecache();            // 0x103a03e0
void SabbatLeaderPrecache();          // 0x103a6ab0
void SheriffManPrecache();            // 0x103ae540
void TestNpcPrecache();               // 0x103b41e0
void TzimiscePrecache();              // 0x103b8fa0
void TzimisceHeadClawPrecache();      // 0x103c1400
void TzimisceRunnerPrecache();        // 0x103c31e0
void WerewolfPrecache();              // 0x103cb2a0
void ZombiePrecache();                // 0x103df120

// --- The two slot-104 arms story 29c-1 ported and left unwired ------------------------------------
//
// Both bodies are family **Lifecycle**'s and are CALLED, not re-recovered. Slot 104 was a generated
// stub when they landed, so nothing ran them; these two arms are the wiring, plus the tail
// `CameraPrecacheModel`'s own comment names as "a later story's" — which is this one.

void GenericNpcLinePrecache();        // 0x1034aa40 — CGenericNPC, via `GenericNpcPrecache`
void CameraPrecache();                // 0x103689c0 — CNPC_VCamera / …Security, via `CameraPrecacheModel`
