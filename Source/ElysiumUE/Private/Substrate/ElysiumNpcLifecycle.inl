// Story 29c-1, family **Lifecycle** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcLifecycle.cpp` and the tests in
// `Tests/ElysiumNpcKernelLifecycleTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// --- What this family is --------------------------------------------------------------------------
//
// Spawn, init, precache, save/restore, dormancy and the per-think clocks. 59 rows.
//
// TWELVE of them fill Troika-line slots and are defined (not declared) here: slots 0, 3, 4, 91,
// 116, 137, 152, 158, 423, 471, 552 and 559. **Slot 158 `IsAlive`** is the one other landed
// families were waiting on — family Bosses' slot-482 `CanPlaySequenceSpecies` and family Sounds'
// `PlaySentence` both dispatch it and took the dead arm while its stub answered false.
//
// The rest are SPECIES or VARIANT bodies of slots whose Troika-line body belongs to a later story
// (29d/29e) and whose generated stub is therefore still standing: slot 77/78 `ScriptHide`/
// `ScriptUnhide`, 103 `Spawn`, 104 `Precache`, 110 `KeyValue`, 113 `Activate`, 117 `ObjectCaps`,
// 119 `Kill`, 127 `Restore`, 130 `OnRestore`, 175 `Touch`, 180 `UpdateOnRemove`, 412–414 the think
// stamps and 434 `PrescheduleThink` (the `CAI_ExpressiveNPC` / `CAI_BaseHumanoid` fills of 512
// `GetExpresser` and 585 were deleted by 0019 story 5 step 1: no instance). Each lands under
// a NAMED method here that the slot's own body routes to when its story lands — the shape family
// **Hints** used for its slot-566/567 species halves, and for the same reason: defining the slot
// twice is a link error, and guessing at the Troika-line body is not this story's licence.
//
// Rows whose target is another struct landed on that struct: `FElysiumEntity::ObjectCaps` and
// `FElysiumEntity::ScriptHide`/`ScriptUnhide` (`ElysiumEntity.h`), `FElysiumInterestingPlace::Spawn`
// /`OnRestore`/`Reset` (`ElysiumInterestingPlace.h`), `FElysiumNpcMaker::ParseMapData`
// (`ElysiumNpcMaker.h`), `FElysiumNpcSenses::Tick` (`ElysiumNpcSenses.h`).

// --- The words this family's bodies read that 29b did not declare ---------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout. What is missing is (a) base
// `CBaseEntity` words BELOW `+0x1a40`, which the NPC word table does not cover, and (b) the species
// words above `+0x665c`, where one offset means a different thing per retail class — the same
// situation families Hints and Squad declared theirs in. Each carries its offset and the retail
// class that owns it.

/** `+0x2830` and `+0x30e9`, the two words `FUN_100e58b0` tests. **Unrecovered**, same situation:
 *  a free function with one direct caller and no oracle or port citation. */
int32 Field_0x2830 = 0;
bool bField_0x30e9 = false;

/** `CNPC_VMingXiao`'s proxy block, read by `ProxyReadyTimer` (`0x10397b40`): `+0x66a4` the ready
 *  stamp (family **Bosses**' `MingXiaoProxyReadyTimer`), `+0x668c` the six-slot handle array (family
 *  **Squad**'s `Proxies`) and `+0x6684` the six-byte registered flags — the only one of the three
 *  that had no owner, so it lands here. SIX is the loop bound in the body (`iVar6 < 6`), and it is
 *  the same six as those two arrays'. */
static constexpr int32 MingXiaoProxySlots = 6;

// --- Slots 412/413/414: the three think stamps ----------------------------------------------------
//
// `CAI_BaseNPCTroika::GetLastUpdateThink` / `GetLastNormalThink` / `GetLastMoveThink`
// (`0x101aa6d0`, `0x101aa6f0`, `0x101aa710`) each return one of the Troika's own think stamps
// (`+0x6254..+0x625c`) of the four-clock bookkeeping the schedule/condition kernel times its
// interrupts off. This runtime CARRIES those words — `FElysiumNpcScheduleHost::LastUpdate` /
// `LastNormal` / `LastMove` / `LastAI` — so the bodies are that read and nothing else. The base
// bodies beneath (`0x101a6440` / `60` / `80`) return `CBaseEntity::GetLastThink(NULL)` (`+0x178`)
// instead; story 5 step 5 corrected these citations, which named the base addresses.

float LastUpdateThink() const;   // 0x101aa6d0
float LastNormalThink() const;   // 0x101aa6f0
float LastMoveThink() const;     // 0x101aa710

// --- Slot 77/78/119: dormancy, by species ---------------------------------------------------------

/** `CAI_Hint::vfunc5` (`0x102d2f00`), the hint node's deleting destructor — `thunk_FUN_102d3040`
 *  then MSVC's `if (flags & 1) operator delete(this)`. What `0x102d3040` does that the KERNEL can
 *  observe is done to the **owning NPC**, so it lands as a method on the owner and not as a static
 *  over `FHintWords`:
 *
 *      if (m_hOwner resolves && owner->m_pNPC (+0x98) != NULL) {
 *          (**(DAT_10924a6c + 4))();           // the memory-alloc hook; no NPC state
 *          SetCondition(owner_npc, 0x29);      // 0x10269a20
 *          CAI_BaseNPCTroika::ClearHintNode(owner_npc, 0.0);
 *      }
 *
 *  The ORDER is retail's and is the point: the condition is raised BEFORE the hint reference is
 *  dropped, so a selector that runs on the same pass sees both the bit and the cleared node.
 *  `ClearHintNode(0.0)` is `ClearScheduleHint(0.f)` here — a ZERO reuse delay, unlike the 5.0 s
 *  `TaskFail` and the state-change path pass, because the node is going away and there is nothing
 *  left to cool down.
 *
 *  Condition `0x29` has no recovered name in the port's registrar, so it is spelled as the raw
 *  number, the convention family Conditions set for the Werewolf's `0x7a`.
 *
 *  **SEAM, stated:** the rest of `0x102d3040` unlinks the node from the engine's global hint list
 *  (`DAT_10925450` head, `+0x5d8` next, `DAT_10925454`/`DAT_1092545c` cursors, `DAT_10925458`
 *  count) and frees its `CUtlVector`s. This substrate stands hint nodes as rows and not as engine
 *  objects with a list, so there is no list to unlink from; nothing the kernel reads changes. */
void HintDeletingDestructor();

/** What `CAI_BaseNPCTroika::ScriptUnhide` (`0x102c1ec0`) hands the `scripted_sequence` that hid this
 *  body — the six words it writes into the cine entity at `+0x5f78`..`+0x5f8c`. Returned rather than
 *  written, because this runtime's beat carries no such block: see the definition. */
struct FCineUnhideRecord
{
	/** False when `m_bCineScriptHidden` (`+0x5d78`) was clear or `m_hCine` (`+0x5d74`) did not
	 *  resolve — the arm that writes nothing to a cine and only clears the latch. */
	bool bWroteToCine = false;
	int32 MoveType = 0;     // +0x5f78 <- slot 94 GetMoveType
	int32 MoveCollide = 0;  // +0x5f7c <- slot 95 GetMoveCollide
	int32 Solid = 0;        // +0x5f80 <- slot 92 GetSolid
	int32 SolidFlags = 0;   // +0x5f84 <- slot 211 GetSolidFlags
	int32 Effects = 0;      // +0x5f88 <- m_fEffects
	uint32 NpcFlagWord = 0; // +0x5f8c <- m_bfAINPCFlags
};

/** `CAI_BaseNPCTroika::ScriptUnhide` (`0x102c1ec0`), slot 78 for 62 classes — the whole tail that
 *  runs AFTER `CBaseEntity::ScriptUnhide`. Three things, in retail's order: dispatch slot 614
 *  `ResetThinkTimers`; unhide the active weapon (slot 78 on it, vtable `+0x138`); record this body's
 *  four physics words plus `m_fEffects` and `m_bfAINPCFlags` onto the cine that hid it, then clear
 *  `m_bCineScriptHidden`.
 *
 *  The slot-614 half is ALREADY CARRIED by `FElysiumNpc::OnDormancyChanged`, which cites this exact
 *  address; this body does not repeat it. */
FCineUnhideRecord TroikaScriptUnhideTail();

// The three tails above are not yet reached from `FElysiumEntity::ScriptUnhide`: the NPC's slot-78
// chain is unwired (story 5 step 3 records the species rows as residue), so there is no dispatcher.

// --- Slot 103 `Spawn`: the species bodies ---------------------------------------------------------

/** `CCineNPC::Spawn` (`0x101a6f10`), shared with `CCineAI` — the cine actor's own spawn. Pure: it
 *  reads the spawnflags and the name and answers the whole state it would have written, because the
 *  cine entity is `FElysiumScriptedSequence` here and not an NPC at all. */
struct FCineSpawnState
{
	int32 Solid = 0;                  // SetSolid(0) — SOLID_NONE
	int32 AddedSolidFlags = 4;        // AddSolidFlags(m_Collision.flags | 4)
	bool bTargetable = false;         // m_bIsBCCTargetable = 0
	bool bAlive = false;              // m_bIsAlive = 0
	bool bAutoRemoveThink = false;    // an unnamed cine, or spawnflag 0x10
	double NextThink = 0.0;           // curtime + _DAT_10449280 when the think is armed
	bool bHasStartTime = false;       // only a NAMED cine that armed the think gets one
	double StartTime = 0.0;           // curtime + _DAT_10449e10
	bool bInterruptable = true;       // spawnflag 0x20 CLEARS it
	int32 SequenceStarted = 0;
	bool bNextCineSet = false;        // m_hNextCine = -1
	int32 AddedFlags2 = 0x10;         // AddFlag2(0x10)
};
static FCineSpawnState CineSpawn(int32 SpawnFlags, bool bNamed, double Now);

/** `CAI_InterestingPlaceConverstation::Spawn` (`0x102dbc80`), whose whole body is its own field
 *  init followed by `JMP [[this]+0x1a0]` — vtable `+0x1a0` is slot 104, so **the spawn precaches at
 *  its END rather than at its start**. True means "and now run Precache", which is the recovered
 *  fact this answers. */
static bool ConversationPlaceSpawnPrecachesLast();

// --- Slot 104 `Precache`: the species bodies ------------------------------------------------------

/** One precache request a species `Precache` body issues, in retail's own order. `bModel` picks the
 *  model precacher (`*DAT_1070b22c+0x34`) over the sound one (`*DAT_1070b248`). */
struct FPrecacheRequest
{
	FString Name;
	bool bModel = false;
	/** Retail warned about this name instead of precaching it (an empty/invalid sound). */
	bool bWarnedInvalid = false;
};

/** `CAI_InterestingPlaceConverstation::Precache` (`0x102dbcb0`) — `m_iszSoundLoop` (warned, and NOT
 *  precached, when the name is empty) then `m_iszSoundOnce` (never warned). The asymmetry is a
 *  retail fact and is reproduced. */
static void ConversationPlacePrecache(const FString& SoundLoop, const FString& SoundOnce,
	TArray<FPrecacheRequest>& Out);

// --- Slot 110 `KeyValue`: the AI-helper-entity cascade ---------------------------------------------

// --- Slot 113 `Activate` ---------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Activate` (`0x1028e310`) — after `CBaseEntity::Activate`, an NPC whose
 *  `Classify()` (slot 138) is non-zero applies `m_sDefaultDisposition` (`+0x6558`, or the empty
 *  string when unset) through `SetDisposition(name, 1)`.
 *
 *  `FElysiumNpc::Activate` never ran this. It does now — this is that arm, split out so a fixture can
 *  state it without standing a whole world. */
void ApplyDefaultDispositionOnActivate();

/** SEAM for slot 138 `Classify()`, the gate above. Retail's `CAI_BaseNPCTroika` body answers a
 *  non-zero class for every living NPC and 0 for the inert helper entities that share the vtable
 *  (`CAI_Hint`, `CAI_TestHull`, `CScriptedTarget`); this runtime stands only living NPCs on this
 *  leaf, so it answers non-zero. Named rather than inlined so the day slot 138 lands the gate moves
 *  with it. */
bool ClassifyIsNonZero() const;

/** `CAI_InterestingPlaceConverstation::Activate` (`0x102dbde0`) — walk `m_iszInterestingPlaces` by
 *  name, admit each hit that is a conversation interesting place whose `field[0x161]` is 1, warn
 *  about the rest, then re-arm or silence the think by `m_bEnabled`. Returns the admitted list.
 *  `OutRejected` collects the names retail `DevWarning`s. */
TArray<FElysiumEntityHandle> ConversationPlaceActivate(const FString& PlacesName, bool bEnabled,
	TArray<FString>* OutRejected = nullptr) const;

// --- Slot 127/130: save and restore ----------------------------------------------------------------

// --- Slot 175 `Touch`, slot 180 `UpdateOnRemove` ---------------------------------------------------

/** `CCineNPC::Touch` (`0x101a75a0`), shared by `CCineAI` and `CCineAISchedule` — the body is
 *  `return;` with the parameter ignored. Nothing happens when something touches a cine actor.
 *  Answers whether the touch was consumed, which is always true (retail does not chain). */
static bool CineTouch(FElysiumEntity* Other);

// --- Slot 434 `PrescheduleThink`: the species bodies ----------------------------------------------

// `CNPC_VCamera::PrescheduleThink` (`0x10369100`), shared with `CNPC_VCameraSecurity` — an EMPTY
// body, `FElysiumNpcCamera`'s override. `CNPC_VSabbatLeader::PrescheduleThink` (`0x103a7650`) is the
// retail scope-trace wrapper and an unconditional forward to `0x10385a30` (the body
// `CNPC_VAndreiBlood` and 40-odd classes share), which has no port body: residue for story 8.

// --- The free functions and the unnamed tables ------------------------------------------------------

/** `FUN_100e58b0` — `*(char*)(this+0x30e9) != 0 || *(int*)(this+0x2830) == 0`. Two threshold checks
 *  over unmapped offsets; no port or oracle citation. Verbatim, arm order preserved. */
bool TestField_0x2830() const;

/** The elapsed cycle count `ReportSearchTimer` left behind — the read side of the static pair, for
 *  a test and for a debug panel. */
static uint64 SearchTimerElapsedCycles();

// --- `CAI_StandoffBehavior::vfunc13` (`0x102c7600`) -------------------------------------------------
//
// A standoff-behaviour selector. **There is no `CAI_StandoffBehavior` in this runtime and no
// behaviour object under the NPC at all**, so this lands the way family Hints landed its hint rules:
// the behaviour's own datamap as a typed view, and the selector as a PURE function over it, so every
// threshold and every arm is exercised without inventing a behaviour store.

