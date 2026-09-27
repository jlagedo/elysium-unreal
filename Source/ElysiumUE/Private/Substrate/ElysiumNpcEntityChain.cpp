#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **EntityChain** — the 57 unnamed bodies of the entity chain BELOW the NPC,
// `order.md` layers 0–9. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// What the family turned out to BE, once the decompiled C was read rather than summarised: eight
// classes, not one. `CBaseEntity` (the network change-state, solidity, standability, the physics
// tick), `CBaseAnimating`/`CBaseAnimatingOverlay`/`CBaseFlex` (the gesture-layer, flinch, flex and
// scene-event slots — whose TABLES family Anim already ported, so these six slot bodies drive that
// family's members), `CBaseCombatCharacter`, **`CBasePlayer`** (half the family: law levels, the
// police response, the heightened alert, the closest-NPC cache, autoaim, the held use entity, the
// camera-override crossfade, the controller-NPC detach), `CCineNPC`, `CDialog`, `CGlobalEntityList`
// and `CPayphone`.
//
// `vtmb_fields CBasePlayer` NAMED most of what 29c's walk left unrecovered — `m_hClosestNPC`,
// `m_LevelSupernaturalAct`, `m_iCopsInPursuitCount`, `m_flSpawnResponseCopsTimer`, `m_vecAutoAim`,
// `m_fOnTarget`, `m_hControllerNPC`, `m_hCameraTargetEntity` — so those port methods carry the
// recovered name. Four rows still do not settle and keep 29c's `FUN_<address>` spelling.
//
// The `CBasePlayer` half runs over `ChainPlayer()`, the ONE player this runtime stands, and where
// that player ALREADY carries the word (`Law`, `Police`, `ScareQueue`, `Observer`) the body reads
// and writes THAT member. Nothing here stands a second copy of a rule.

namespace
{
	// ---------------------------------------------------------------------------------------------
	// The recovered `.rdata` constants this family reads, each with where its value was settled.
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.
	// ---------------------------------------------------------------------------------------------

	// `_DAT_10457f54` = **0.7f** (`docs/vtmb/computer-terminals.md` lines 524 and 1729,
	// `docs/vtmb/npc-ai/senses.md` line 401). The NEW-sample weight of the autoaim blend.
	constexpr float GChainAutoaimNewWeight = 0.7f;
	// `0x47c34ff3` = **100000.0f**, the "nothing is near" distance `UpdateClosestNpc` resets
	// `m_flClosestNPCDist` to. Read straight off the immediate in the decompilation.
	constexpr float GChainClosestNpcReset = 100000.0f;

	// `FL_AIMTARGET`, the `GetFlags()` bit both autoaim bodies screen on and the only thing either
	// of them raises `m_fOnTarget` for.
	constexpr int32 GChainFlAimTarget = 0x10000;

	// `AutoaimDeflection`'s trace distance, the `0x46800000` immediate `GetAutoaimVector` pushes.
	constexpr float GChainAutoaimDistance = 16384.0f;
	// The four clamps, read as literal floats out of the decompilation (`25.0` / `-25.0` are
	// spelled as immediates; `_DAT_10450568` is `360.0` and `_DAT_1044c3a8` / `_DAT_10462948` are
	// the `180.0` / `-180.0` the wrap tests against).
	constexpr float GChainAutoaimWrap = 360.0f;
	constexpr float GChainAutoaimHalfWrap = 180.0f;
	constexpr float GChainAutoaimPitchClamp = 25.0f;
	constexpr float GChainAutoaimYawClamp = 12.0f;

	// `NPC_STATE` ids `FixScriptNpcSchedule` reads and writes (`0x1026e3e0`'s own cases).
	constexpr int32 GChainNpcStateIdle = 1;
	constexpr int32 GChainNpcStateDead = 7;
	// The `__LINE__` retail stamps into `m_SelectIdealStateTrace.m_iLine` on the way out: `0x3ca`.
	constexpr int32 GChainFixScriptScheduleLine = 970;

	// `0x101a6420`'s identity passthrough needs somewhere to point when the caller supplies null;
	// retail returns the caller's own pointer, so there is no such place and the port answers null
	// too. Declared for readability at the one site.

	// `0x10182a90`'s fixed "over threshold" answer.
	constexpr int32 GChainClosestNpcOverThreshold = 0x264;

	// The two record offsets `FireGlobalActsOutput` is called with, so a reader can join them back
	// to `thunk_FUN_1023dcd0`'s object.
	constexpr int32 GChainAlertEndOutput = 0x480;
	constexpr int32 GChainAlertBeginOutput = 0x498;

	// The five literals `0x1017ddd0` folds `m_LevelCriminalAct`'s stored word through.
	constexpr uint32 GChainObfAnd0 = 0x08a66e35u;
	constexpr uint32 GChainObfXor0 = 0x00793f90u;
	constexpr uint32 GChainObfAdd = 0x08b10412u;
	constexpr uint32 GChainObfAnd1 = 0x175991cau;
	constexpr uint32 GChainObfXor1 = 0x783682a9u;
}

// -------------------------------------------------------------------------------------------------
// The `CBasePlayer` half of the chain, and the seams.
// -------------------------------------------------------------------------------------------------

FElysiumPlayer* FElysiumNpc::ChainPlayer() const
{
	// Retail's `this` for every `+0x19b8`..`+0x22b0` body in this file. Null on a worldless probe
	// entity and in a headless world with no player, which every caller has retail's own null arm
	// for.
	return World != nullptr ? World->FindPlayer() : nullptr;
}

bool FElysiumNpc::HeadBonePosition(const FElysiumEntity& Speaker, FVector& OutWorld) const
{
	// SEAM for `CBaseAnimating::LookupBone("bip01_head")` followed by slot 192 (`+0x300`) for the
	// resolved bone's world position. Family Anim already records that this runtime's kernel stands
	// no bone table; the same refusal under this family's name.
	(void)Speaker;
	OutWorld = FVector::ZeroVector;
	return false;
}

float FElysiumNpc::SoundDurationOf(const TCHAR* SoundName) const
{
	// SEAM for `(*DAT_1070b248 + 0x30)(name)`, the sound-length query `0x10182c40` adds to its
	// animation deadline. The kernel reaches no sound catalogue.
	(void)SoundName;
	return 0.f;
}

void FElysiumNpc::ChainEntityList(TArray<FElysiumEntity*>& Out) const
{
	// THE ORDER IS RETAIL'S, and it is observable. `AutoaimDeflection` (`0x10176930`) does not walk
	// an entity list at all — it walks the ENGINE'S EDICT ARRAY, by index, ascending:
	//
	//     edict = engine->PEntityOfEntIndex(0);          // (**(DAT_1070b22c + 0x98))()
	//     for (i = 1; i < gpGlobals->maxEntities; ++i) { // gpGlobals+0x38
	//         edict += 0x78;                             // sizeof(edict_t), one slot
	//         if (edict[0x4c] != 0) continue;            // the free-slot byte
	//         ...
	//     }
	//
	// It matters which order that is, because the score comparison is `score <= best` and NOT `<`:
	// a candidate that ties the running best REPLACES it, so among equally well-aligned candidates
	// the one at the HIGHEST edict index wins. (29c-1's note said the opposite — "the first of a tie
	// wins" — which is what a `<` would give. The decompiled C is `if (fVar15 <= fStack_f8) { best =
	// fVar15; winner = candidate; }`. The scoring loop in `AutoaimDeflection` below already had the
	// `<=`; only this comment was wrong.)
	//
	// `FElysiumEntityWorld::EntityList` IS this port's edict array: `ElysiumEntityWorld.cpp` states
	// the invariant at both of its two writers — "the handle index IS the def-array index: stable,
	// never recycled", and a runtime spawn "continues past the map's def array". So the list is the
	// map's entity lump in lump order followed by every runtime spawn in creation order, indexed
	// exactly as retail indexes edicts, and this walk is by index, ascending, like retail's.
	//
	// One structural difference, which is not an order difference: retail's loop starts at index 1
	// because edict 0 is the engine's own reserved world edict, and this list reserves no such slot
	// — its index 0 is the map's first entity. The same set, in the same order. (Retail's world
	// edict would fail the `FL_AIMTARGET` screen in `AutoaimDeflection` anyway, which is why retail
	// can skip it without a test.)
	Out.Reset();
	if (World == nullptr)
	{
		return;
	}
	for (int32 Index = 0; Index < World->NumEntities(); ++Index)
	{
		// `edict[0x4c] != 0` — the free-slot byte. A reaped slot here is a dead entity.
		const TUniquePtr<FElysiumEntity>& Entity = World->Entities()[Index];
		if (Entity.IsValid() && !Entity->IsDead())
		{
			Out.Add(Entity.Get());
		}
	}
}

FElysiumEntity* FElysiumNpc::TraceAimRay(const FVector& Src, const FVector& Dir,
	float Distance) const
{
	// SEAM for `UTIL_TraceLine(src, src + dir * dist, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr)`
	// and `tr.m_pEnt`. This substrate's trace surface answers world geometry, not entities, so the
	// ray hits nothing that takes damage — which is retail's own arm, and the arm that then walks
	// the entity list.
	(void)Src;
	(void)Dir;
	(void)Distance;
	return nullptr;
}

bool FElysiumNpc::GameRulesAllowAutoTargetCrosshair() const
{
	// SEAM for `(*DAT_1070ba0c + 0xa0)()`. TRUE deliberately: false is the arm that CLEARS
	// `m_fOnTarget`, and a missing rules object must not clear a flag retail only clears when the
	// rules say so.
	return true;
}

bool FElysiumNpc::GameRulesAutoAimEnabled() const
{
	// SEAM for `GetAutoAimMode() > AUTOAIM_NONE` (`(*DAT_1070ba0c + 0x58)` and the
	// `thunk_FUN_101764d0()` gate both autoaim bodies open with). FALSE is `AUTOAIM_NONE`: autoaim
	// off, which is the shipped default for a mouse-aimed single-player game.
	return false;
}

bool FElysiumNpc::GameRulesAllowsAutoAimAt(const FElysiumEntity& Candidate) const
{
	// SEAM for `(*DAT_1070ba0c + 0x80)(this, edict)`, the per-candidate admission inside the walk.
	(void)Candidate;
	return false;
}

bool FElysiumNpc::HasStudioModel(const FElysiumEntity& Candidate) const
{
	// SEAM for `CBaseAnimating::GetModelPtr()`. Family Anim's flex seam records the same fact: the
	// animating tier stands no studio header. FALSE is retail's arm for a candidate with no model,
	// and it is the arm that refuses to cache it.
	(void)Candidate;
	return false;
}

bool FElysiumNpc::ClosestNpcCandidateRefused(const FElysiumEntity& Candidate) const
{
	// SEAM for `thunk_FUN_100b5190(candidate)`, whose TRUE answer refuses an otherwise accepted
	// candidate. **Unrecovered** predicate; false is the accepting arm.
	(void)Candidate;
	return false;
}

float FElysiumNpc::ClosestNpcPerception(const FElysiumEntity& Npc) const
{
	// SEAM for `thunk_FUN_1029c970(npc)` and the two folds `thunk_FUN_1029c9f0(v, player)` /
	// `thunk_FUN_1029ca30(v)`. The port's stealth surface publishes its own COMMITTED observation
	// meter (`FElysiumPlayer::Observer.Meter`, `docs/vtmb/stealth.md`: "nothing that reads it may
	// run range, cone, trace, memory or enemy work"), so the kernel reads that rather than
	// recomputing a second perception here.
	(void)Npc;
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->Observer.Meter : 0.f;
}

int32 FElysiumNpc::ClosestNpcSenseScalar() const
{
	// SEAM for `thunk_FUN_101e9000(0x10739d08)`. NAME and DEFAULT **unrecovered** — the pointer
	// lives in uninitialised `.data` and no corpus function constructs it. 0 zeroes the scaled term.
	return 0;
}

void FElysiumNpc::FireGlobalActsOutput(int32 RecordOffset)
{
	// `thunk_FUN_100cd660(singleton + <offset>, this, this, 0)` — a `COutputEvent::FireOutput` on a
	// record hanging off the singleton `thunk_FUN_1023dcd0()` resolves.
	//
	// RECOVERED, by joining this walk to `docs/vtmb/player-entity.md` § "Law, Masquerade and world
	// response", which story 16 read from the consumer side: the two records are the cop ALERT
	// edges. `+0x498` is `OnStartCopAlertMode`, fired beside the `m_bInHeightenedAlert` /
	// `m_flHeightenedAlertExpireTimer` write; `+0x480` is `OnEndCopAlertMode`, fired beside the
	// timer clear. The singleton is the game-rules object those outputs live on.
	//
	// **SEAM, and deliberately a recording one**: this runtime ALREADY fires both edges, from
	// `Substrate/ElysiumLaw.cpp`'s own pass. Firing them a second time from the kernel body would
	// double-fire an authored output, which is a real behavioural difference — so the kernel records
	// the call and names the output rather than duplicating story 16's transport.
	UnrecoveredChainCalls.Add(FString::Printf(TEXT("0x1023dcd0+0x%x FireOutput"), RecordOffset));
}

bool FElysiumNpc::SpawnResponseCopsDelayCvars(float& OutLow, float& OutHigh) const
{
	// SEAM for `DAT_10725894` (the high bound) and `DAT_107257bc` (the low bound). Both NAMES
	// **unrecovered**. Retail's `IsCommand()` arms write literal `0` and `_DAT_104454c4` = 0.0, so
	// the draw is over an empty range.
	OutLow = NpcKernelEntityChainShared::GChainZero;
	OutHigh = NpcKernelEntityChainShared::GChainZero;
	return false;
}

bool FElysiumNpc::ActLevelOverrideCvar(int32 CvarId, int32& OutValue) const
{
	// SEAM for `DAT_10724ffc` / `DAT_1072594c` / `DAT_107250d4`. All three NAMES **unrecovered**.
	// Retail's shape is `IsCommand()` false plus a NEGATIVE `m_nValue` (`+0x2c`, word 0xb) meaning
	// "no override", which is the arm that reads the stored field. This answers that arm: the cvar
	// is not a command and its value is -1.
	(void)CvarId;
	OutValue = -1;
	return false;
}

// -------------------------------------------------------------------------------------------------
// `CBaseEntity` — the slots.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// The `CAI_BaseNPC` line's own unnamed slots.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::LocalScheduleId(int32 GlobalId) const
{
	const FElysiumLocalIdSpace* Space = IdSpace(EElysiumIdCategory::Schedule);
	return Space != nullptr ? Space->GlobalToLocal(GlobalId) : INDEX_NONE;
}

void* FElysiumNpc::GetClassScheduleIdSpace()
{
	// 0x101aa790, slot 580 — `CAI_BaseNPCTroika`'s override, `return &DAT_10924248`.
	//
	// The space is the CORPUS's, keyed on this NPC's retail class (`IdSpace` below), so what this
	// answers is the same `CAI_ClassScheduleIdSpace` retail returns -- filled, with its parent link
	// and its four sub-spaces, rather than the empty static the port used to stand here.
	return const_cast<FElysiumLocalIdSpace*>(IdSpace(EElysiumIdCategory::Schedule));
}

float FElysiumNpc::LastAiThink() const
{
	// 0x101aa730 — the Troika's stamp for the FOURTH think channel (+0x6260; the base body
	// `0x101a64a0` answers `CBaseEntity::GetLastThink(NULL)`), beside family
	// Lifecycle's `LastUpdateThink` / `LastNormalThink` / `LastMoveThink`. This runtime carries the
	// word as `FElysiumNpcScheduleHost::LastAI`.
	return static_cast<float>(ScheduleHost.LastAI);
}

// -------------------------------------------------------------------------------------------------
// `CDialog` — two bodies of the dialogue file object.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::DialogLineHeadPosition(int32 LineIndex, FVector& OutWorld) const
{
	// 0x100e49b0. Four gates before anything happens, in retail's order:
	//   LineIndex >= 0, LineIndex < the line count (`+0x2834`), the dialog's SPEAKER handle
	//   (word 0 of the object) resolves live, and the line's flag byte (`+0x2810 + i`) has bit 0.
	// Then the line TYPE (`+0x2820 + i*4`) selects:
	//   5 and 6 -> `LookupBone("bip01_head")` on the speaker, then slot 192 for the bone's world
	//              position, then `thunk_FUN_101d26b0()`.
	//   anything else -> return, having done nothing.
	//
	// The two arms are BYTE-IDENTICAL in the decompilation — retail duplicated the block rather than
	// falling through — and both re-resolve the speaker handle a second time between the bone lookup
	// and the position fetch, which is why a speaker that died inside `LookupBone` reaches slot 192
	// through a NULL pointer. Ported as one arm because the two are the same instructions; the
	// re-resolve is reproduced, and here it cannot crash.
	OutWorld = FVector::ZeroVector;
	if (LineIndex < 0 || LineIndex >= DialogPcLines.Num())
	{
		return false;
	}
	FElysiumEntity* Speaker = World != nullptr ? World->Resolve(DialogSpeaker) : nullptr;
	if (Speaker == nullptr)
	{
		return false;
	}
	if ((DialogPcLines[LineIndex].Flags & 1) == 0)
	{
		return false;
	}
	const int32 Type = DialogPcLines[LineIndex].Type;
	if (Type != 5 && Type != 6)
	{
		return false;
	}
	// The second resolve, retail's own.
	Speaker = World->Resolve(DialogSpeaker);
	if (Speaker == nullptr)
	{
		return false;
	}
	return HeadBonePosition(*Speaker, OutWorld);
}

void FElysiumNpc::CallPendingDialogEventScript()
{
	// 0x100e4ef0. Run the 0x100-byte buffer at `CDialog+0x31ea` through `CDialog::CallEventScript`
	// if its first byte is non-zero, then zero-fill ALL 0x100 bytes — the clear is OUTSIDE the `if`,
	// so an empty buffer is still wiped.
	//
	// The buffer is filled by `CDialog::process_pc_line` (`0x100e8520`,
	// `Q_strncpy(this+0x31ea, this + line*0x228 + 0x2964, 0x100)`). It is a DIFFERENT field from the
	// one the port's `FElysiumDlgConversation::FlushPendingNpcAction` drains, which is the col-5 NPC
	// action; this one is the line's own event script.
	if (!PendingDialogEventScript.IsEmpty())
	{
		DialogEventScriptCalls.Add(PendingDialogEventScript);
	}
	PendingDialogEventScript.Empty();
}

// -------------------------------------------------------------------------------------------------
// `CGlobalEntityList` — the listener registry.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::AddListenerEntity(void* Listener)
{
	// 0x100f6d80 — `CGlobalEntityList::AddListenerEntity`. Called only ever on `&DAT_106eb5d8`, the
	// global entity list, by `CNavPropertyDatabase`, `CPhysSaveRestoreBlockHandler`,
	// `CEntityListSystem` and the aim-target manager (`0x102cd650`); `0x100f8700` walks the result
	// BACKWARDS calling each element's slot 1, which is what makes the elements listener objects.
	//
	// Three steps, in retail's order: a linear dedupe scan that RETURNS on a hit; a grow when the
	// count would exceed `+0x1804c`; and an insert at the end — retail computes a `memmove` for the
	// tail after the insertion point and then finds the length is zero, because the insertion point
	// IS the end. The mirror at `+0x18058` is rewritten with the block pointer on every append and
	// nothing reads it back.
	if (EntityListeners.Listeners.Contains(Listener))
	{
		return;
	}
	if (EntityListeners.Listeners.Num() + 1 > EntityListeners.Allocated)
	{
		EntityListeners.Allocated = EntityListeners.Listeners.Num() + 1;
	}
	EntityListeners.Listeners.Add(Listener);
	EntityListeners.DebugBlock = EntityListeners.Listeners.GetData();
}

void FElysiumNpc::RemoveListenerEntity(void* Listener)
{
	// 0x100f6e40 — the counterpart: a linear search by VALUE, a `memmove` compaction of the tail and
	// a decrement. A value the list does not hold is a silent no-op, and the capacity word is NOT
	// reduced.
	const int32 Index = EntityListeners.Listeners.Find(Listener);
	if (Index == INDEX_NONE)
	{
		return;
	}
	EntityListeners.Listeners.RemoveAt(Index);
}

// -------------------------------------------------------------------------------------------------
// `CCineNPC` — the scripted-sequence trio.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CineIsTimeToStart() const
{
	// 0x101a7540 — `m_iDelay` (+0x5f70) below 1 **AND** `m_startTime` (+0x5f74) at or before
	// curtime.
	//
	// The SDK's `CAI_ScriptedSequence::IsTimeToStart` is `(m_iDelay <= 0 || curtime >= m_startTime)`
	// — an **OR**. Retail's is an AND, in the decompiled C, which means a beat with a delay never
	// starts by its start time alone and one with no delay still waits for it. A shipped divergence
	// from the SDK, ported as it stands.
	//
	// SEAM: `CCineNPC`'s own datamap words are not on this leaf — the port's scripted sequence is
	// `FElysiumScriptedSequence` and its beat clock is that entity's, not the NPC's — so both words
	// are read through `CineDelayState()` below, which answers the resting (0, 0) pair. Both terms
	// then hold, and the answer is TRUE: a beat with no authored delay is ready, which is retail's
	// own answer for `m_iDelay == 0, m_startTime == 0`.
	int32 Delay = 0;
	float StartTime = 0.f;
	CineDelayState(Delay, StartTime);
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	return Delay < 1 && StartTime <= Now;
}

void FElysiumNpc::CineDelayState(int32& OutDelay, float& OutStartTime) const
{
	// SEAM for `CCineNPC::m_iDelay` (+0x5f70) and `m_startTime` (+0x5f74). The port's beat lives on
	// the `scripted_sequence` entity and the kernel holds no pointer to it; (0, 0) is the resting
	// state retail's constructor leaves.
	OutDelay = 0;
	OutStartTime = 0.f;
}

void FElysiumNpc::FixScriptNpcSchedule(FElysiumNpc& Npc)
{
	// 0x101a8840 — `CCineNPC::FixScriptNPCSchedule(npc)`, the body every scripted-sequence teardown
	// ends on, and one of the SDK's own named bodies:
	//
	//     if ( pNPC->GetIdealState() != NPC_STATE_DEAD )
	//         pNPC->SetIdealState( NPC_STATE_IDLE );
	//     pNPC->ClearSchedule( ... );
	//
	// Retail's `m_IdealNPCState` (+0x5cc4) is compared against 7 and stored as 1, with the
	// `m_SelectIdealStateTrace` pair stamped first: `+0x1b3c` takes the
	// `e:\vampire\main\dlls\scripted_cp...` path string and `+0x1b40` the `__LINE__` 0x3ca = 970.
	//
	// 29c's walk read that pair as "a one-shot assertion/error-state tripwire". It is NOT:
	// `docs/vtmb/npc-kernel/layout.md` names `+0x1b3c`/`+0x1b40` the ideal-state selector trace, and
	// every writer of `m_IdealNPCState` in the image — `Event_Killed` `0x10265ad0`, `NPCInit`
	// `0x10273390`, `CineCleanup` `0x1027d170` — stamps it the same way. The shape map records both
	// words ABSENT because this runtime traces selections through the mind's transition trace, which
	// is where the reason lands instead.
	//
	// `ClearSchedule` runs on EVERY path, including the dead one.
	if (Npc.Mind.DesiredRetailState() != GChainNpcStateDead)
	{
		Npc.Mind.RequestDesiredState(GChainNpcStateIdle, GChainFixScriptScheduleLine);
	}
	Npc.ClearSchedule();
}

// -------------------------------------------------------------------------------------------------
// `CBasePlayer` — the law and police half.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::LevelSupernaturalAct() const
{
	// 0x1017dd80 — `m_LevelSupernaturalAct` (+0x1ccc) behind `DAT_10724ffc`. The shape, in retail's
	// order:
	//   !IsCommand() and m_nValue < 0  -> the stored field
	//   IsCommand()                    -> 0
	//   otherwise                      -> m_nValue
	// Retail dispatches `IsCommand()` TWICE rather than caching it, which the port keeps.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarSupernatural, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		return Player != nullptr ? Player->Law.Supernatural : 0;
	}
	if (ActLevelOverrideCvar(ActCvarSupernatural, Override))
	{
		return 0;
	}
	return Override;
}

int32 FElysiumNpc::LevelInvestigateAct() const
{
	// 0x1017de60 — the same three arms over `m_LevelInvestigateAct` (+0x1cdc) and `DAT_107250d4`.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarInvestigate, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		return Player != nullptr ? Player->Law.Investigate : 0;
	}
	if (ActLevelOverrideCvar(ActCvarInvestigate, Override))
	{
		return 0;
	}
	return Override;
}

uint32 FElysiumNpc::ObfuscateActLevel(uint32 Stored)
{
	// The arithmetic inside `0x1017ddd0`, verbatim:
	//     (((stored & 0x8a66e35) ^ 0x793f90) + 0x8b10412) & 0x175991ca ^ stored ^ 0x783682a9
	// then through `thunk_FUN_1042fd40`.
	//
	// **Unrecovered:** `thunk_FUN_1042fd40` itself. The corpus carries no body for it, so what it
	// does to the folded word is not known and the fold is returned unchanged. That is why the port
	// stores the criminal level as PLAINTEXT (`FElysiumLawState::Criminal`, documented `+0x1cd8`)
	// and reproduces the fold here as an assertable function rather than as storage — an
	// obfuscation whose final transform is unknown cannot be a round trip.
	const uint32 Folded = ((((Stored & GChainObfAnd0) ^ GChainObfXor0) + GChainObfAdd)
		& GChainObfAnd1) ^ Stored ^ GChainObfXor1;
	return Folded;
}

int32 FElysiumNpc::LevelCriminalAct() const
{
	// 0x1017ddd0 — the criminal level, and the odd one of the three: its stored word at `+0x1cd8` is
	// OBFUSCATED, which is why `CBasePlayer`'s datamap types `+0x1cd0 m_LevelCriminalAct` as a
	// record rather than as an int. The cvar arms are identical to the other two.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarCriminal, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		const int32 Stored = Player != nullptr ? Player->Law.Criminal : 0;
		// The port stores plaintext; the fold is what retail applies to its own storage, and it is
		// exercised so the five literals are asserted rather than dropped.
		(void)ObfuscateActLevel(static_cast<uint32>(Stored));
		return Stored;
	}
	if (ActLevelOverrideCvar(ActCvarCriminal, Override))
	{
		return 0;
	}
	return Override;
}

int32 FElysiumNpc::CriminalActCount() const
{
	// 0x1017e720 — a bare getter of `m_iCriminalActCount` (+0x1ce8). The port's player already
	// carries the word and already publishes the read (`FElysiumPlayer::CriminalActCount()`, which
	// the NPC conditions 31-34 lane reads); this row IS that call and stands nothing beside it.
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->CriminalActCount() : 0;
}

int32 FElysiumNpc::SupernaturalActCount() const
{
	// 0x1017e740 — the same over `m_iSupernaturalActCount` (+0x1cec).
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->SupernaturalActCount() : 0;
}

int32 FElysiumNpc::FUN_10178120() const
{
	// 0x10178120 — a bare getter of `+0x1d24`. The datamap names `m_flSetOnHeadTimer` at +0x1d20 and
	// nothing again until `m_iVFlags` (+0x1d60), so the field's NAME and CONCERN are
	// **unrecovered** and the port method keeps 29c's `FUN_` spelling.
	//
	// What IS recovered: three kernel callers and five outside read it through this getter, and
	// NOTHING in `vampire.dll` writes it — a `vtmb_grep` for the offset finds only this body and its
	// thunk. So the shipped answer is whatever the constructor left, and the constructor zeroes it.
	return Field_0x1d24;
}

void FElysiumNpc::SetSpawnResponseCops(int32 Level, FElysiumEntity* Source, const FVector& At)
{
	// 0x1017ed00 — record a delayed police response.
	//
	// Gate: `0x1017f8b0` reads `m_iHuntersInPursuitCount` (+0x1d14) and the body refuses when it is
	// 1 or more. A hunter on the street suppresses a cop response outright.
	//
	// Arm one, the FLT_MAX sentinel at `+0x1cf8` ("nothing pending"):
	//   store the level, the source's handle (or -1 when the source is null), the three location
	//   floats, and then `timer = curtime + RandomFloat(lo, hi)` — the bounds are two ConVars.
	// Arm two, a record already armed:
	//   ONLY when `Level` strictly exceeds the stored level, overwrite the level, the handle and the
	//   location. **The timer is not rescheduled**, which is what makes a burst of incidents one
	//   response landing at the first one's deadline.
	FElysiumPlayer* Player = ChainPlayer();
	const int32 HuntersInPursuit = Player != nullptr ? Player->Police.HuntersInPursuit : 0;
	if (HuntersInPursuit >= 1)
	{
		return;
	}

	const bool bUnused = SpawnResponseCopsTimer == FLT_MAX;
	if (!bUnused && SpawnResponseCopsLevel >= Level)
	{
		return;
	}

	SpawnResponseCopsLevel = Level;
	SpawnResponseCopsNpc = Source != nullptr ? Source->Handle : FElysiumEntityHandle::Invalid();
	SpawnResponseCopsLocation = At;

	if (bUnused)
	{
		float Low = 0.f;
		float High = 0.f;
		SpawnResponseCopsDelayCvars(Low, High);
		// `(*DAT_1070b244 + 4)(lo, hi)` is `RandomFloat`. **Named decision**: the port draws only
		// when the range is non-empty. Retail draws unconditionally, and a zero-width draw still
		// advances its stream; this runtime's streams are SHARED across systems, so a draw that
		// cannot change the answer but does move another system's sequence would be a real
		// behavioural difference.
		const float Delay = (High > Low)
			? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Low, High)
			: NpcKernelEntityChainShared::GChainZero;
		const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
		SpawnResponseCopsTimer = Delay + Now;
	}

	// The port's `FElysiumPoliceState` carries the CONSUMER's view of the same record (story 16,
	// recovered from the spawn side). Mirrored here so the two cannot disagree; the producer's four
	// words above are the retail ones and this is the port's own read side, not a second rule.
	if (Player != nullptr)
	{
		Player->Police.bResponsePending = true;
		Player->Police.ResponseSeverity = SpawnResponseCopsLevel;
		Player->Police.ResponseWitness = SpawnResponseCopsNpc;
		Player->Police.ResponsePosition = SpawnResponseCopsLocation;
		Player->Police.ResponseDeadline = SpawnResponseCopsTimer;
	}
}

void FElysiumNpc::RemoveCopInPursuit()
{
	// 0x1017f6e0 — one cop leaves the pursuit.
	//
	// The decrement is UNCONDITIONAL and unclamped: `m_iCopsInPursuitCount` can go negative, and
	// the zero test is `== 0`, not `<= 0`, so a count driven below zero never fires the alert again.
	// On exactly zero: the unrecovered `0x10370630`, then `BeginHeightenedAlert`.
	//
	// The trailing `DevMsg` reads `"CSActs:    %6.1f - OnCopPursuitStart - %d in pursuit"` — retail
	// prints "Start" from the REMOVE path, sharing the format string verbatim with `0x1017f650`,
	// which is the add. A shipped copy-paste, recorded rather than corrected.
	FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return;
	}
	Player->Police.CopsInPursuit -= 1;
	if (Player->Police.CopsInPursuit == 0)
	{
		UnrecoveredChainCalls.Add(TEXT("0x10370630"));
		BeginHeightenedAlert();
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnCopPursuitStart - %d in pursuit"),
		Player->Police.CopsInPursuit);
}

bool FElysiumNpc::IsHeightenedAlertActive() const
{
	// 0x1017f8d0 — `curtime < m_flHeightenedAlertExpireTimer` (+0x1d1c), and nothing else. It does
	// NOT read `m_bInHeightenedAlert` (+0x1d18), which is why `EndHeightenedAlert`'s zeroing of the
	// timer alone is enough to answer false while the flag still stands.
	const FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return Now < Player->Police.HeightenedAlertExpiry;
}

void FElysiumNpc::BeginHeightenedAlert()
{
	// 0x1017f9c0 — arm it. Four steps in retail's order: fire the `+0x498` output
	// (`OnStartCopAlertMode`) off the `0x1023dcd0` singleton; read the duration ConVar
	// `DAT_10725f74` (`debug_heightened_alert_expire_time`); set `m_bInHeightenedAlert` and
	// `m_flHeightenedAlertExpireTimer = <duration> + curtime`; call the unrecovered `0x1017f900`.
	//
	// Note the order of the last two: retail reads curtime BEFORE it raises the flag, and the flag
	// is raised before the timer is written. Nothing observes the gap, and it is kept anyway.
	FireGlobalActsOutput(GChainAlertBeginOutput);

	float Duration = 0.f;
	HeightenedAlertDurationCvar(Duration);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	FElysiumPlayer* Player = ChainPlayer();
	if (Player != nullptr)
	{
		Player->Police.bHeightenedAlert = true;
		Player->Police.HeightenedAlertExpiry = static_cast<double>(Duration) + Now;
	}
	UnrecoveredChainCalls.Add(TEXT("0x1017f900"));
}

void FElysiumNpc::EndHeightenedAlert()
{
	// 0x1017f980 — end it. Fire the `+0x480` output (`OnEndCopAlertMode`) off the same singleton,
	// then zero `m_flHeightenedAlertExpireTimer`.
	//
	// It does **not** clear `m_bInHeightenedAlert` (+0x1d18). The flag and the timer are written by
	// different bodies and only the timer is read by the predicate, so after this the flag is stale
	// and nothing notices — which is exactly what the port reproduces rather than tidying.
	FireGlobalActsOutput(GChainAlertEndOutput);
	FElysiumPlayer* Player = ChainPlayer();
	if (Player != nullptr)
	{
		Player->Police.HeightenedAlertExpiry = 0.0;
	}
}

void FElysiumNpc::RememberScaredNpc(int32 Priority, FElysiumEntity* Npc)
{
	// 0x1017fd60 — the 16-byte scare records at `+0x1d90`.
	//
	// The key is the NPC's ENTITY INDEX, fetched through the engine (`(*DAT_1070b22c + 0x8c)` on the
	// NPC's edict at `npc+0x2e0`), and it is compared against WORD 2 of each record. On a hit:
	//   word 3 (the priority) takes the GREATER of the two — retail writes only when the new one is
	//   larger — and word 0 (the timestamp) is ALWAYS refreshed to curtime, hit or not.
	// On a miss: grow, append `{ <word0 from EBX>, curtime, index, <word3 from the return slot> }`,
	// and log `"Scared NPC: %d (dist:%.2f)"` with the index and the NPC's own `+0x6264`.
	//
	// The append's words 0 and 3 come out of registers the decompilation cannot attribute
	// (`unaff_EBX`, `unaff_retaddr`), so the LAYOUT of the appended record is **unrecovered** beyond
	// the timestamp and the index. The port's `FElysiumScareRecord` is story 16's recovery of the
	// same 16 bytes from the CONSUMER side — `{ Npc, Severity, Time }` with one word unidentified —
	// and this body writes THAT, which is the reading that already exists rather than a second one.
	if (Npc == nullptr)
	{
		return;
	}
	FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	for (FElysiumScareRecord& Record : Player->ScareQueue)
	{
		if (Record.Npc.Index == Npc->Handle.Index)
		{
			if (Record.Severity < Priority)
			{
				Record.Severity = Priority;
			}
			Record.Time = Now;
			return;
		}
	}

	FElysiumScareRecord Fresh;
	Fresh.Npc = Npc->Handle;
	Fresh.Severity = Priority;
	Fresh.Time = Now;
	Player->ScareQueue.Add(Fresh);

	// `Msg(s_Scared_NPC___d__dist___2f__105888bc, index, npc[0x1899])`. `npc + 0x6264` is a float on
	// the NPC the message calls a distance; no port member claims the offset, so the message names
	// the NPC instead and says so.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Scared NPC: %d (dist: unrecovered +0x6264)"),
		Npc->Handle.Index);
}

bool FElysiumNpc::HeightenedAlertDurationCvar(float& OutSeconds) const
{
	// `DAT_10725f74` is **`debug_heightened_alert_expire_time`** — recovered by joining this walk to
	// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response", which names the ConVar
	// this body's timer is scheduled from.
	//
	// **SEAM**: the kernel stands no ConVar table, so `IsCommand()` answers true and retail's own
	// `_DAT_104454c4` = 0.0 arm runs — the alert expires the instant it is armed. That is the
	// recovered refusal and not a chosen duration; the DURATION lives in `Substrate/ElysiumLaw.h`'s
	// own copy of the same cvar.
	OutSeconds = NpcKernelEntityChainShared::GChainZero;
	return false;
}
