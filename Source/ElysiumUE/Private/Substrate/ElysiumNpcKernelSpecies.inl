// Story 29c-1, family **Species** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelSpecies.cpp` and the tests in
// `Tests/ElysiumNpcKernelSpeciesTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THIS FAMILY IS THE SPECIES SPREAD ITSELF. Its 60 rows are eight different retail classes, not one
// leaf, and three standing facts follow from that:
//
//   * **One slot, many bodies.** Slots 599, 600, 601, 602, 606 and 609 each already carry a Troika
//     body (family **TroikaHelpers**) and a `CNPC_VAndreiBlood`-line twin; this family's rows are
//     the per-species REPLACEMENTS of those. `MeleeSlotLine` (TroikaHelpers) already answers
//     `EMeleeSlotLine::Species` for exactly the six classes below and says the species body "is
//     another family's row". This is that family: each species body lands under its own RETAIL
//     ADDRESS (`FUN_103c19e0`, …) and the dispatcher `SpeciesSlot…` picks between them off the
//     census, so a reader can check every arm against `docs/vtmb/npc-kernel/slots.md`. **WIRED**:
//     all eighteen dispatchers now stand at the top of the base body they sit in front of — that
//     prologue IS retail's vtable dispatch, and `SpeciesDispatchingSlot` below is retail's
//     non-virtual thunk back down to the base. The call sites are named in each base body's
//     comment (TroikaHelpers, BaseHelpers, Anim, Sounds and Closure).
//
//   * **The two tables disagree, and both answers are used.** The census says which retail class a
//     classname IS (`RetailClass()`); the spawn registry says which classnames a map may stand
//     (`Substrate/ElysiumNpcClasses.cpp`). `CNPC_VCop`'s census classname list is NULL, so a spawned
//     `npc_VCop`'s `RetailClass()` is null and every species lookup here correctly falls through to
//     the Troika line — asserted in the suite rather than worked around. `npc_VCamera` is claimed by
//     `CNPC_VCamera` in the census but is not a spawn leaf, so its rows are exercised by RETAIL
//     CLASS NAME through the table's own lookup.
//
//   * **Two of this family's rows do not belong on an NPC at all.** `CNPCMaker_Fleshpile`'s
//     `MakeNPC` (`0x1034c2d0`) and `DeathNotice` (`0x1034c8e0`) are the MAKER's bodies, and this
//     runtime's maker is `FElysiumNpcMaker` — a `FElysiumEntity`, not an NPC. They land there
//     (`Substrate/ElysiumNpcMaker.h`), per `.claude/rules/cpp.md`, and the report says so. A third,
//     `0x1034b430`, turned out to be ALREADY PORTED as `FElysiumNpcMaker::IsDepleted`; the address
//     is cited there and the row is reported as a `present` re-verdict, not re-implemented here.

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
int32 AndreiHitMax = 0;        // +0x66dc CNPC_VAndreiBlood::m_iHitMax (datamap)

// `CNPC_VBaseBoss::m_BlacklistedEntities`, `+0x665c` with its allocation count at `+0x6660`, grow
// size at `+0x6664`, element count at `+0x6668` and the `CUtlMemory` element mirror at `+0x666c`.
// Rows are the 8-byte `{EHANDLE, float expiry}` pair family **Bosses** already declared as
// `FBlacklistedEntity` for `CNPC_VHengeyokai`'s copy at `+0x66a4`; that struct is reused rather
// than restated. Werewolf locks a hint in it and MingXiao skips a thrown object for 20 s.
TArray<FBlacklistedEntity> BossBlacklist;

// `CNPC_VBach`'s fire gate — the one-shot arm slot 606 keeps around `COND_ENEMY_OCCLUDED`.
bool bBachFireOccluded = false;   // +0x66a3 CNPC_VBach::m_bFireOccluded (datamap)

// `CNPC_VMingXiaoTentacle`'s cached coordinate point — the three floats the head writes onto a
// severed tentacle when it re-aims it. `+0x668c` is `CNPC_VMingXiao::m_rhProxies` on the HEAD
// (family Squad's `Proxies`) and `CNPC_VTzimisce::m_ePathMode` on a Tzimisce (family Motor's
// `PathMode`); the tentacle is a third class at the same offset. SOURCE units, as retail stores it.
FVector TentacleCoordinatePosUnits = FVector::ZeroVector;   // +0x668c/+0x6690/+0x6694 (walked)

/** One row of `CNPC_VNewscaster`'s two story queues — a fixed-stride `0x28` record whose first word
 *  is the story's own object and whose `+0x04`/`+0x08` pair, walked in steps of 8 up to `0x20`, are
 *  the four VCD/resource handles `0x103a0d50` releases. The runtime carries the NAME only: nothing
 *  in this substrate stands a VCD, so a row is what the debug listing prints and what the release
 *  counts, which is every observable the two bodies produce. */
struct FNewscasterStory
{
	FString Name;
};

// The two queues and their cursors. `CNPC_VNewscaster` has no datamap in the corpus, so every name
// here is WALKED off the bodies that touch it (`0x103a0270`, `0x103a0670`, `0x103a0ab0`,
// `0x103a0d50`, `0x103a0ff0`) and says so.
TArray<FNewscasterStory> NewscasterMainStories;   // +0x665c, count +0x6668 (walked)
TArray<FNewscasterStory> NewscasterSideStories;   // +0x6670, count +0x667c (walked)
int32 NewscasterPlayingSide = 0;                  // +0x668c — non-zero selects the SIDE queue (walked)
int32 NewscasterMainCursor = INDEX_NONE;          // +0x6684 (walked)
int32 NewscasterSideCursor = INDEX_NONE;          // +0x6688 (walked)
bool bNewscasterStoryActive = false;              // +0x6690, cleared by the teardown (walked)

// `CNPC_VTzimisce`'s own words. `m_hPickupTarget` (+0x6670) and `m_vecPickupTargetPos` (+0x6674)
// are family **Motor**'s `PickupTarget`/`PickupTargetPos`; `m_hPhysicsAnimlink` (+0x6684) is family
// **Damage**'s `TzimiscePhysicsAnimlink`. All three are read through their owners here.
int32 TzimiscePickupGrabBone = INDEX_NONE;   // +0x6680 m_iPickupTargetGrabBone (datamap)
double TzimisceBodyTimer = 0.0;              // +0x66a4 m_flBodyTimer (datamap), an absolute stamp
bool bTzimisceDidFakeThrow = false;          // +0x66b4 m_bDidFakeThrow (datamap)
// `+0x6690` with allocation count `+0x6694`, grow size `+0x6698`, element count `+0x669c` and the
// element mirror `+0x66a0` — the SAME `CUtlVector<{EHANDLE, float}>` shape as the boss blacklist
// above, written a second time on a different class at a different offset.
TArray<FBlacklistedEntity> TzimisceBlacklist;

// `CNPC_VTzimisceHeadClaw`'s slow stamp. `+0x6678` is `CNPC_VTzimisceRunner::m_hPotentialEnemy`,
// `CNPC_VZombie::m_iZombieAIType` and `CNPC_VManBat::m_flFlapTimer` on three other species.
double HeadClawSlowedExpire = 0.0;   // +0x6678 CNPC_VTzimisceHeadClaw::m_flSlowedExpire (datamap)

// `CNPC_VTzimisceRunner`'s own two.
FElysiumEntityHandle RunnerPotentialEnemy;   // +0x6678 m_hPotentialEnemy (datamap)
bool bRunnerDeathNoticeProcessed = false;    // +0x6671 m_bDeathNoticeProcessed (datamap)

// `CNPC_VZombie::m_iZombieAIType` — a mapper keyvalue (`ZombieAIType`) whose setter rerolls the
// value 4 into a random 1..3 and side-effects four of the seven values.
int32 ZombieAiType = 0;   // +0x6678 CNPC_VZombie::m_iZombieAIType (datamap, key ZombieAIType)

// `CNPC_VWerewolf`'s frame-memoised chase cache. `m_DoorState` (+0x6680) is family **Hints**'
// `WerewolfDoorState` and is read through it. No datamap names these; they are walked off
// `0x103d9c90`.
int32 WerewolfChaseFrame = INDEX_NONE;                        // +0x6670 (walked)
FVector WerewolfChasePosUnits = FVector::ZeroVector;          // +0x6674/+0x6678/+0x667c (walked)
// `+0x6684` / `+0x6688` are `m_hRotDoor1` / `m_hRotDoor2`, which family **Misc** declares off
// `0x103cade0` (the zone opener that FILLS them). `0x103d1e50` is the READER of the same pair and
// goes through Misc's members rather than standing a second copy.

// `CScriptedTarget`'s two, which are NOT NPC words at all: `0x1034d6e0` is that class's `Spawn`
// and the census gives `CScriptedTarget` no entity classname, so no map stands one here. They are
// declared for the same reason family **BaseHelpers** declared `CAI_BaseActor`'s: the body is
// ported and it has to have somewhere to write.
FVector ScriptedTargetLastPositionUnits = FVector::ZeroVector;   // +0x5f44 m_vLastPosition (datamap)
bool bScriptedTargetDisabled = false;                            // m_iDisabled (datamap)

// --- The seams this family stands ------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. Nothing below invents a value.

/** SEAM for `(**(code **)(*DAT_1070b22c + 0x1e0))()` — the ENGINE FRAME NUMBER `0x103d9c90`
 *  memoises its chase position on. This runtime's clock is `FElysiumEntityWorld::NowSeconds()` and
 *  carries no frame counter at all.
 *
 *  **NAMED DECISION**: this answers `INDEX_NONE`, and the body reads that as "the stamp can never
 *  match", so the cache is ALWAYS STALE and the position is recomputed on every call. Retail calls
 *  the body at most once per frame per NPC, so recomputing per call is retail's own answer at
 *  retail's own call rate; answering a CONSTANT frame instead would freeze the cache after its
 *  first fill and hand every later caller a stale point, which is the one behaviour retail never
 *  has. The word `+0x6670` is still written, so a frame counter arriving later needs no other edit. */
int32 EngineFrameNumber() const;

/** SEAM for `thunk_FUN_1025de90(coord, this)` reached through slot 602's SPECIES arm
 *  (`0x103c1b10`, `0x103c3ab0`). Family **TroikaHelpers** already stands this exact retail body as
 *  `MeleeCoordinatorHoldsMe()`; it is CALLED rather than restated, so the two cannot drift. Named
 *  here only so the two species bodies below have the call site recorded. */

/** SEAM for `(**(code **)(*DAT_10924a1c + 4))()` and `DAT_10924a1c[10]` — the melee-range ConVar the
 *  species slot-602 bodies threshold `m_flEnemyDist` against. TroikaHelpers' `MeleeRangeUnits()`
 *  reads the SAME global and answers `0.0` (UNRECOVERED); it is called here for the same reason. */

/** SEAM for `thunk_FUN_1039ede0(this)` — the MingXiao HEAD a `CNPC_VMingXiaoTentacle` forwards its
 *  slots 21, 22 and 23 to. Family **Motor** already stands the same retail call
 *  (`ElysiumNpcKernelMotor.cpp:543`) and found the tentacle proxy chain absent; this answers null,
 *  which is retail's "no companion" arm and the one that forwards nothing. */
FElysiumEntity* MingXiaoTentacleHead() const;

/** SEAM for `thunk_FUN_10397dd0(head, this, param)` — `CNPC_VMingXiao`'s per-tentacle notice, which
 *  family **Damage** declared as the body that resets `m_flSpitAttackTimer`. Records the forward so
 *  the three slots can be told apart, and reaches nothing. */
int32 TentacleHeadForwards = 0;

/** SEAM for the three world singletons `CNPC_VTzimisce`'s slot 488 reads before firing `SPI_DIES`
 *  — `DAT_1093cf94`, `DAT_1093cfdc` and `DAT_1093cebc`, each substituted with 0 when its own
 *  `vtable+0x4` answers true and otherwise handing over `+0x2c`, `+0x2c` and `+0x28`. Nothing in
 *  this substrate stands them. Answers `false` (the singleton is not available), which is the arm
 *  that substitutes 0 — so all three arguments are 0 and the event still fires, which is what the
 *  script host observes. */
bool TzimisceDeathScriptArgument(int32 SingletonIndex, int32& OutArgument) const;

/** SEAM for `thunk_FUN_10289ee0(this, 1)` — `RestartIdealActivity(1)`, which
 *  `CNPC_VTzimisceRunner`'s slot 588 calls UNCONDITIONALLY where the base override
 *  (`0x10293e50`) gates it on `IsActivityFinished`. Family **Hints** already stands the same retail
 *  body as `RestartIdealActivity()`; it is called here and records the call. */

/** SEAM for `thunk_FUN_101cca80(&trace, 2, mins, maxs, 0x2080, 0)` — the spawn-box overlap query
 *  `CNPCMaker_Fleshpile::MakeNPC` runs. `FElysiumNpcMaker::CanMakeNpc` already asks the embodiment
 *  the same question (`IsNpcMakerSpawnAreaOccupied`); the fleshpile body calls THAT rather than
 *  standing a second seam. Named here for the reader. */

/** SEAM for `thunk_FUN_101d3190(&out, this, 0)` + `(*DAT_1070b254 + 0x10)(...)` — the downward
 *  trace `CNPCMaker_Fleshpile::MakeNPC` runs once to cache `m_flGround`. `FElysiumNpcMaker` already
 *  caches the same number through `ResolveNpcMakerGroundZ`; the fleshpile body calls that. */

// --- Slot 323's four answers -----------------------------------------------------------------------

/** `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`, slot 323 — the four codes retail returns for a movement
 *  direction relative to the body's own facing, by the angle band the direction falls in. Every
 *  boundary below was read out of the pinned `vampire.dll`'s `.rdata`, so the bands are recovered
 *  numbers and the NAMES are this port's reading of them (`+yaw` is counter-clockwise in Source, so
 *  a relative yaw of 90 is to the body's left). The NUMBERS are retail's own return values.
 *
 *  Note `_DAT_1049e8a4` is **316**, not 315: the four bands are 89, 90, 90 and 91 degrees wide, not
 *  a clean quarter split. That is retail's number and it is kept. */
enum class EMoveDirectionCode : int32
{
	Behind = 0,   // (135, 225]                — `_DAT_1049e8a0` .. `_DAT_1049e89c`, and the too-slow arm
	Right = 1,    // (225, 316]                — above `_DAT_1049e89c`
	Ahead = 2,    // (316, 360) and [0, 45]    — above `_DAT_1049e8a4`, or at-or-below `_DAT_1049949c`
	Left = 3,     // (45, 135]                 — at-or-below `_DAT_1049e8a0`
};

// --- The per-species slot table ---------------------------------------------------------------------

/** One row: the retail class whose vtable carries the body, the slot it fills, and the RETAIL
 *  ADDRESS of that body, so every row is checkable against `docs/vtmb/npc-kernel/slots.md`. This is
 *  the whole of this family's species dispatch — `FElysiumNpc` is `final` and species variation is
 *  data, never a subclass. */
struct FSpeciesSlotRow
{
	const TCHAR* RetailClass = nullptr;
	int32 Slot = 0;
	const TCHAR* Address = nullptr;
};

/** Every row this family ports, in slot then class order. */
static const FSpeciesSlotRow* SpeciesSlotRows(int32& OutCount);

/** The row for `Slot` on `InRetailClass`, walking the base chain exactly as the vtable does, or
 *  null when no class in the chain replaces the body this family's siblings already carry. */
static const FSpeciesSlotRow* SpeciesSlotRowOf(const TCHAR* InRetailClass, int32 Slot);

/** The same, for the class THIS npc is. Null for a classname the census does not claim — which is
 *  `npc_VCop`'s recovered answer and not a bug. */
const FSpeciesSlotRow* SpeciesSlotRow(int32 Slot) const;

// --- Retail's NON-VIRTUAL call back down to the base body --------------------------------------------
//
// Four of this family's bodies end in a call to the very body they replace: `CNPC_VBach`'s slot 606
// delegates to `thunk_FUN_102b8320`, `CNPC_VTzimisce`'s 593 runs `thunk_FUN_1029a070` FIRST and then
// overwrites what it wrote, `CNPC_VZombie`'s 510 tails into `CAI_BaseNPC::ShouldPlayFloatSound` and
// the five slot-482 copies ARE the base instruction for instruction. Every one of those calls is a
// DIRECT call in retail — a `thunk_`, never a vtable dispatch — so it lands on the base body and can
// never reach the species body a second time.
//
// This runtime stands ONE function per slot: the base body IS the vtable entry, and the species
// prologue at the top of it is what the vtable does. `SpeciesDispatchingSlot` is what makes the
// species body's own call to that function the direct call retail makes — while slot N's species
// body is running, slot N's dispatcher answers "no species body" and the base arm runs. That is the
// non-virtual thunk, written once instead of splitting five base bodies in five other families'
// files. A DIFFERENT slot dispatched from inside a species body is untouched, because the guard is
// per slot and not a depth count: retail's thunks are per body too.
int32 SpeciesDispatchingSlot = 0;

/** The row `Slot`'s dispatcher should run, or null — `SpeciesSlotRow` plus the rule above. */
const FSpeciesSlotRow* SpeciesDispatchRow(int32 Slot) const;

/** Marks `Slot`'s species body as the one running, for the life of the scope. */
struct FSpeciesDispatchScope
{
	FSpeciesDispatchScope(FElysiumNpc& InNpc, int32 Slot);
	~FSpeciesDispatchScope();

	FSpeciesDispatchScope(const FSpeciesDispatchScope&) = delete;
	FSpeciesDispatchScope& operator=(const FSpeciesDispatchScope&) = delete;

	FElysiumNpc& Npc;
	int32 Previous = 0;
};

#if WITH_DEV_AUTOMATION_TESTS
/** Test-only: stand this NPC as `RetailClassName` for the species dispatch.
 *
 *  The two tables disagree (standing fact two above) and the gap is wider than `npc_VCop`:
 *  `Substrate/ElysiumNpcClasses.cpp` registers fourteen spawnable `npc_*` classnames while the
 *  census carries 77 classes, and the census gives `CNPC_VZombie` and `CNPC_VCop` NO classname at
 *  all. Twelve of this family's eighteen wired slots are therefore carried by a class no fixture can
 *  spawn, so the suite sets the latch the spawn path would have set and then drives the REAL base
 *  slot through it — which is the only way to assert that the prologue picks the species arm.
 *  Nothing in the shipping build calls this; `RetailClass()` stays the runtime's only writer. */
void SetRetailClassForTests(const TCHAR* RetailClassName);
#endif

// --- The slot dispatchers ---------------------------------------------------------------------------
//
// Each answers whether a SPECIES body ran, and hands back what it answered. The Troika-line and
// `CNPC_VAndreiBlood`-line bodies these sit in front of are family **TroikaHelpers**' (and Anim's,
// Sounds', Closure's and BaseHelpers'): a dispatcher that answers false means "run the body you
// already have", which is exactly what the one-line prologue at the top of each of those does.

bool SpeciesSlot599(FElysiumEntity* Enemy, bool& OutAnswer);
bool SpeciesSlot600(FElysiumEntity* Enemy, bool& OutAnswer);
bool SpeciesSlot601(FElysiumEntity* Enemy);
bool SpeciesSlot602(bool& OutAnswer);
bool SpeciesSlot606(int32 Arg, int32& OutAnswer);
/** Slot 609's species arm is a GATE plus a write, never a search: the three byte-identical
 *  overrides zero `m_pShootAtHintNode` and answer null unless `m_NPCState` is 4 or 0xc. `OutRunBase`
 *  says whether the base search may run. */
bool SpeciesSlot609(bool bArg, bool& OutRunBase);
bool SpeciesSlot21(FElysiumEntity* Arg);
bool SpeciesSlot22(FElysiumEntity* Arg);
bool SpeciesSlot23(FElysiumEntity* Arg);
bool SpeciesSlot25(FElysiumEntity* Arg);
bool SpeciesSlot26(FElysiumEntity* Arg);
bool SpeciesDeathSound();            // slot 488
bool SpeciesSlot497();
bool SpeciesSlot506();
bool SpeciesSlot588();
bool SpeciesSlot593();
bool SpeciesShouldPlayFloatSound(bool& OutAnswer);   // slot 510
/** Slot 482's species arm. Five classes carry a body of their own and every one of them is
 *  BYTE-IDENTICAL to the base `CAI_BaseNPC::CanPlaySequence` (`0x10278090`, family **Anim**'s
 *  `CanPlaySequence`), so the answer is the base's and this exists to SAY so with the address. */
bool SpeciesCanPlaySequence(bool bDisregardState, int32 InterruptLevel, int32& OutAnswer);

// --- The bodies, by retail address ------------------------------------------------------------------
//
// The naming is 29c's overlay target verbatim, so the ledger's `hand:` claim is checkable. A body
// whose retail name IS recovered says so in its definition comment; none of these has one.

/** `CScriptedTarget::Spawn` `0x1034d6e0`, slot 103 on a class no map stands here. */
void FUN_1034d6e0();

/** `CNPC_Crow::vfunc197` `0x103577d0` — slot 197's whole body, a same-object forward to slot 192. */
FVector FUN_103577d0(const FVector& InUnits);

/** `0x10357be0` — `CNPC_Crow`'s fly-toward-the-hint-node step. `m_pHintNode`'s ENTITY has no source
 *  in this substrate (hints are node indices here), so the wrapper takes retail's NULL arm; the
 *  arithmetic of the other arm is `CrowFlyStep`, which is pure and is what the suite drives. */
struct FCrowFlyStep
{
	bool bArrived = false;                            // field_0x5f54 = 1
	FVector VelocityUnits = FVector::ZeroVector;      // SetAbsVelocity's argument, SOURCE units
	float MotorYaw = 0.f;                             // thunk_FUN_102e1c10's yaw
	float Pitch = 0.f;                                // the pitch written back through SetAngles
};
static FCrowFlyStep CrowFlyStep(float Scale, const FVector& ToUnits, const FVector& FromUnits);
void FUN_10357be0(float Scale);

/** `0x1035e920` — `CNPC_VAndreiBlood`: may another runner be made? */
bool FUN_1035e920() const;

/** `0x1035e950` — `CNPC_VAndreiBlood`: roll `m_iHitMax`. */
void FUN_1035e950();

/** `0x1035fd40` / `0x103bd270` — `CNPC_VAnimal`'s and `CNPC_VTzimisce`'s slot 482, both
 *  byte-identical to the base. */
int32 FUN_1035fd40(bool bDisregardState, int32 InterruptLevel);
int32 FUN_103bd270(bool bDisregardState, int32 InterruptLevel);

/** `0x103662d0` / `0x10366400` / `0x10366490` — `CNPC_VBaseBoss::m_BlacklistedEntities`'s add,
 *  test-and-expire and index-of. */
void FUN_103662d0(const FElysiumEntityHandle& Entity, float Seconds);
bool FUN_10366400(const FElysiumEntity* Candidate);
int32 FUN_10366490(const FElysiumEntity* Candidate) const;

/** `0x1036c7f0` — `CNPC_VChangBros::SetChangType`, a plain setter over family Squad's `ChangType`. */
void FUN_1036c7f0(int32 InChangType);

/** `0x1039ef90` — `CNPC_VMingXiaoTentacle`: cache a coordinate point and raise condition 0x78. */
void FUN_1039ef90(const FVector& PositionUnits);

/** `0x103a0d50` / `0x103a0ff0` — `CNPC_VNewscaster`'s story-queue teardown and debug listing. */
void FUN_103a0d50();
int32 FUN_103a0ff0(int32 FirstLine, TArray<FString>& OutLines) const;

/** `0x103be0b0` / `0x103be150` — `CNPC_VTzimisce`'s `CARRYING_BODY` latch and its timer read. */
void FUN_103be0b0(bool bCarrying);
bool FUN_103be150() const;

/** `0x103be3d0` — `CNPC_VTzimisce`'s nearest-forearm grab-bone search. */
bool FUN_103be3d0(FElysiumEntity* InTarget);

/** `0x103be8e0` — `CNPC_VTzimisce`: is the pickup target close enough to grab? */
bool FUN_103be8e0(FElysiumEntity* InTarget);

/** `0x103bea90` / `0x103bef20` — `CNPC_VTzimisce`'s physics-animlink release and attach. */
void FUN_103bea90(FElysiumEntity* AimTarget);
bool FUN_103bef20(FElysiumEntity* InTarget, int32 ElementKey);

/** `0x103bf200` / `0x103bf330` / `0x103bf3c0` — `CNPC_VTzimisce`'s own blacklist triple. */
void FUN_103bf200(const FElysiumEntityHandle& Entity);
bool FUN_103bf330(const FElysiumEntity* Candidate);
int32 FUN_103bf3c0(const FElysiumEntity* Candidate) const;

/** `0x103bf560` — `CNPC_VTzimisce`: release the motor's yaw hold. */
void FUN_103bf560();

/** `0x103c1ad0` / `0x103c1b10` — `CNPC_VTzimisceHeadClaw`'s slots 601 and 602. */
void FUN_103c1ad0(FElysiumEntity* Enemy);
bool FUN_103c1b10();

/** `0x103c19e0` / `0x103c1a60` — `CNPC_VTzimisceHeadClaw`'s slots 599 and 600. */
bool FUN_103c19e0(FElysiumEntity* Enemy);
bool FUN_103c1a60(FElysiumEntity* Enemy);

/** `0x103c24a0` — `CNPC_VTzimisceHeadClaw`: is the slow window armed? */
bool FUN_103c24a0() const;

/** `0x103c3960` / `0x103c39e0` / `0x103c3a70` / `0x103c3ab0` / `0x103c3fd0` —
 *  `CNPC_VTzimisceRunner`'s slots 599, 600, 601, 602 and 588. */
bool FUN_103c3960(FElysiumEntity* Enemy);
bool FUN_103c39e0(FElysiumEntity* Enemy);
void FUN_103c3a70(FElysiumEntity* Enemy);
bool FUN_103c3ab0();
void FUN_103c3fd0();

/** `0x10376b70` / `0x10376ba0` — `CNPC_VFrenzyShadow`'s slots 599 and 600. */
bool FUN_10376b70(FElysiumEntity* Enemy);
bool FUN_10376ba0(FElysiumEntity* Enemy);

/** `0x10379ef0` / `0x10379f20` — `CNPC_VGargoyle`'s slots 599 and 600. */
bool FUN_10379ef0(FElysiumEntity* Enemy);
bool FUN_10379f20(FElysiumEntity* Enemy);

/** `0x103dd900` — `CNPC_VYukie`'s slot 600. */
bool FUN_103dd900(FElysiumEntity* Enemy);

/** `0x10364280` — `CNPC_VBach`'s slot 606. `Arg` is the base body's own argument, passed straight
 *  through on the delegating arm and read by nothing in the gate. */
int32 FUN_10364280(int32 Arg);

/** `0x103661f0` / `0x10367740` / `0x103b26f0` — the three byte-identical slot-609 gates of
 *  `CNPC_VBach`, `CNPC_VBatSwarm` and `CNPC_VSheriffSwarm`. */
bool FUN_103661f0(bool bArg);
bool FUN_10367740(bool bArg);
bool FUN_103b26f0(bool bArg);

/** `0x103d1e50` — `CNPC_VWerewolf`: are the two door halves near enough to count as shut? */
bool FUN_103d1e50() const;

/** `0x103d9c90` — `CNPC_VWerewolf`'s frame-memoised chase position. SOURCE units out. */
void FUN_103d9c90(FVector& OutPositionUnits);

/** `0x103e0980` — `CNPC_VZombie::SetZombieAIType`. */
void FUN_103e0980(int32 InZombieAiType);

/** `0x103e1080` — `CNPC_VZombie`'s slot 510. */
bool FUN_103e1080(bool bArg);

/** `0x103e12c0` / `0x103e12f0` — `CNPC_VZombie`'s slots 25 and 26, both firing
 *  `m_OnAttackedVictim` (`+0x66e8`) with no base forward. */
void FUN_103e12c0(FElysiumEntity* Victim);
void FUN_103e12f0(FElysiumEntity* Victim);

/** `0x1039e800` / `0x1039e830` / `0x1039e860` — `CNPC_VMingXiaoTentacle`'s slots 21, 22 and 23. */
void FUN_1039e800(FElysiumEntity* Arg);
void FUN_1039e830(FElysiumEntity* Arg);
void FUN_1039e860(FElysiumEntity* Arg);

/** `0x103b9180` / `0x103b92a0` — `CNPC_VTzimisce`'s slots 593 and 488. */
void FUN_103b9180();
void FUN_103b92a0();

/** `0x103681d0` / `0x103682f0` — `CNPC_VCamera`'s empty slots 497 and 506. */
void FUN_103681d0();
void FUN_103682f0();
