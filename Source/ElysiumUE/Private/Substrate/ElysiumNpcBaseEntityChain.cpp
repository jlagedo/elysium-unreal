// `CAI_BaseNPC`'s bodies of the `EntityChain` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseEntityChain.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_1045d650` = **1024.0f** (`docs/vtmb/computer-terminals.md` line 1508,
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885). Slot 550 `CoverRadius`'s answer.
	constexpr float GChainCoverRadius = ElysiumNpcTunables::OneThousandTwentyFour;
}

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 5) ---

const FVector* FElysiumNpcBase::TranslateNavGoalPosition(const FVector* GoalPosition)
{
	// 0x101a6420, slot 410 — `return param_1;`. An IDENTITY PASSTHROUGH, not a fixed literal: the
	// base answer is whatever the caller supplied, so a caller that passes null gets null back. A
	// species that wants to move the goal overrides the slot.
	return GoalPosition;
}

int32 FElysiumNpcBase::GetLocalScheduleId(int32 GlobalId)
{
	// 0x101a6620, slot 447 — `0x102ea280(GetClassScheduleIdSpace(), id)`, the GLOBAL-to-LOCAL
	// direction of the range translation family Schedule ports the other half of. The walk is:
	// -1 stays -1; otherwise follow the chain at `+0x10`, and for the first space whose local base
	// is not the 9999 sentinel and whose `[globalBase, localTop]` range holds the id, answer
	// `(localBase - globalBase) + id`.
	return GlobalToLocalId(IdSpace(EElysiumIdCategory::Schedule), GlobalId);
}

int32 FElysiumNpcBase::ResolveScheduleId(int32 Id) const
{
	// `GetScheduleOfType` 0x102cc260: slot 580 belongs to the receiving NPC, not Troika.
	if (ElysiumScheduleId::IsGlobal(Id)) return Id;
	const FElysiumLocalIdSpace* Space = IdSpace(EElysiumIdCategory::Schedule);
	return Space != nullptr ? Space->LocalToGlobal(Id) : INDEX_NONE;
}

int32 FElysiumNpcBase::GetLocalTaskId(int32 GlobalId)
{
	// 0x101a6640, slot 450 — the same call with the space pointer advanced by `+0x18`, which is the
	// TASK sub-space of the same `CAI_ClassScheduleIdSpace` (the schedule space is at +0x00, tasks
	// at +0x18, conditions at +0x30 and squad slots at +0x48; family Squad reaches +0x48 the same
	// way).
	//
	// The seam is CLOSED. All four sub-spaces are the corpus's own, filled by the registration pass
	// that runs each class's `InitCustomSchedules` recipe, so this is retail's translation over
	// retail's ranges.
	return GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), GlobalId);
}

const FElysiumLocalIdSpace* FElysiumNpcBase::IdSpace(EElysiumIdCategory Category) const
{
	// Slot 580 `GetClassScheduleIdSpace`, answered out of the corpus rather than out of a table
	// typed here: the class -> space map is the sidecar's, so a class whose space is SHARED with a
	// sibling gets the sibling's, and a class with no slot-580 body of its own falls to the Troika
	// line exactly as the vtable would take it.
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	Corpus.EnsureLoaded();
	const FString ClassName = RetailClass() != nullptr ? FString(RetailClass()->Name) : FString();
	if (const FElysiumLocalIdSpace* Own = Corpus.SpaceFor(ClassName, Category))
	{
		return Own;
	}
	return Corpus.SpaceFor(TEXT("CAI_BaseNPCTroika"), Category);
}

bool FElysiumNpcBase::CanPlaySentence(bool bDisregardState)
{
	// 0x101a6840, slot 483 — forwards to slot 158 `IsAlive` (`+0x278`) AND DROPS ITS OWN ARGUMENT on
	// the way. `bDisregardState` never reaches the callee; a caller that passed true to mean "ask me
	// anyway" is answered exactly as one that passed false. That is a fact a program can observe and
	// it is ported as such, not tidied.
	(void)bDisregardState;
	return IsAlive();
}

float FElysiumNpcBase::CoverRadius()
{
	// 0x101a6c20, slot 550 — `return (float10)_DAT_1045d650;` = **1024.0f**
	// (`docs/vtmb/computer-terminals.md` line 1508 reads the same word as `1024.0`;
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885 repeats it). `CNPC_VPedestrian` and
	// `CNPC_VTzimisce` override it for real elsewhere; this is the line's own answer.
	return GChainCoverRadius;
}

const FElysiumLocalIdSpace* FElysiumNpcBase::ClassScheduleIdSpace() const
{
	// 0x101a6d00 — slot 580's BASE body, `return &DAT_1090ff08`. It is a DIFFERENT id space from the
	// Troika line's `&DAT_10924248`, and family Schedule's table has no row for it, so the row is
	// stood here as a static.
	//
	// 49 dispatch sites reach slot 580 and every species subclass overrides it; the base is reached
	// only by the classes between `CAI_BaseNPC` and `CAI_BaseNPCTroika`, none of which is an entity
	// classname this runtime spawns. It is the corpus's `cai_basenpc` unit -- 68 registered schedule
	// names for 64 texts, the root every other space parents on.
	return FElysiumScheduleCorpus::Get().SpaceFor(TEXT("CAI_BaseNPC"),
		EElysiumIdCategory::Schedule);
}

float FElysiumNpcBase::HearingSensitivity()
{
	// 0x101a67c0 — slot 476 `HearingSensitivity`'s base body, `return (float10)_DAT_104454c0;`.
	// `_DAT_104454c0` is the image's shared **1.0f**, so the base sensitivity is UNITY and
	// `CanHearSound`'s `volume * sensitivity` (`docs/vtmb/npc-ai/senses.md` § "radius =
	// HearingSensitivity") is the bare volume. Troika's own override (`0x101aa5f0`) reads `+0x63c0`
	// and is the body every spawned NPC actually gets; this is what the line under it answers.
	return NpcBaseEntityChainShared::GChainOne;
}

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 5) ---

// -------------------------------------------------------------------------------------------------
// `CBaseAnimatingOverlay` / `CBaseFlex` — the six slots over family Anim's tables.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::GlobalToLocalId(const FElysiumLocalIdSpace* Space, int32 GlobalId)
{
	// `0x102ea280`, whole, including the parent walk at `+0x10` and the bound against the
	// TRANSLATED top (`+0x0c`) rather than `m_localTop`. Both used to be missing here and both were
	// invisible, because every row this runtime carried was the 9999 sentinel and the body answered
	// -1 for every id. `FElysiumLocalIdSpace` carries all six words and is where the arms live now;
	// a null space is retail's end-of-chain and still answers -1.
	return Space != nullptr ? Space->GlobalToLocal(GlobalId) : INDEX_NONE;
}
