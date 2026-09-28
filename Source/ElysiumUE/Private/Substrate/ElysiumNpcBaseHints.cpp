// `CAI_BaseNPC`'s bodies of the `Hints` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseHints.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumRetailActivities.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HintWords(int32 HintNode, FHintWords& Out) const
{
	// Retail's hint words live on the `CAI_Hint` ENTITY reached through `m_pHintNode` (`+0x5ddc`).
	// This runtime carries a hint reference as that entity's index (0018 story 2), so the words are
	// the live `ai_hint`'s own. An index that is not a live hint answers false and leaves `Out`
	// untouched.
	if (World == nullptr || !World->Entities().IsValidIndex(HintNode))
	{
		return false;
	}
	const FElysiumHint* Hint = FElysiumHint::Cast(World->Entities()[HintNode].Get());
	if (Hint == nullptr || Hint->IsDead())
	{
		return false;
	}
	Out = Hint->ToWords();
	return true;
}

int32 FElysiumNpcBase::FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const
{
	// SEAM for `0x102d1af0`.
	(void)HintType;
	(void)SearchFlags;
	(void)RadiusUnits;
	return INDEX_NONE;
}

int32 FElysiumNpcBase::FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const
{
	// SEAM for `0x102d24b0`. The recovered walk, for whoever stands the store: start at the rotating
	// cursor `DAT_10925454`'s successor (else the head `DAT_10925450`), follow `+0x5d8`, wrap to the
	// head, and stop when the cursor comes round again; admit a node when `IsHintUnusable` is false,
	// `HintType == 0 || node->m_nHintType == HintType`, the squared distance to `Near` is under
	// `RadiusUnits * RadiusUnits`, and slot 566 `FValidateHintType` accepts it; with bit 0 of the
	// flags additionally require a clear trace. Bit 2 diverts the whole call to `0x102d1760`.
	(void)Near;
	(void)HintType;
	(void)SearchFlags;
	(void)RadiusUnits;
	return INDEX_NONE;
}

void FElysiumNpcBase::ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds)
{
	// `0x102d1420`, exactly: `hint->m_hHintOwner (+0x5e0) = -1` and
	// `hint->m_flNextUseTime (+0x5ec) = ReuseDelaySeconds + gpGlobals->curtime`.
	//
	// SEAM on the write side only. `FElysiumNpcScheduleHost` carries `HintReusableAt` and
	// `bOwnsHint` — the NPC's half of the same claim — but the fields retail writes are the HINT's,
	// and there is no hint to write them into.
	(void)HintNode;
	(void)ReuseDelaySeconds;
}

bool FElysiumNpcBase::IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive)
{
	// `0x102d14c0`, arm for arm and in retail's order.
	if (Hint.Disabled != 0)                    // `m_iDisabled != 0`
	{
		return true;
	}
	if (Now < Hint.NextUseTime)                // `curtime < m_flNextUseTime`
	{
		return true;
	}
	// `m_hHintOwner` resolves to a live entity — retail's `EHANDLE` serial check plus a non-null
	// entity pointer, which is what `bOwnerAlive` stands for.
	return bOwnerAlive;
}

bool FElysiumNpcBase::IsHintUnusable(int32 HintNode, double Now) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		// The seam could not resolve it. A hint that is not there is not usable.
		return true;
	}
	return IsHintUnusable(Hint, Now, Hint.HintOwner.IsSet());
}

void FElysiumNpcBase::RestartIdealActivityId(int32 RetailActivityId)
{
	// `CAI_BaseNPC::RestartIdealActivity` `0x10289ee0`, the whole body: when `m_Activity` (`+0xfec`)
	// already IS the activity asked for it is reset to 0 so the change re-triggers
	// (`0x10289ee4..0x10289eee`), then `SetIdealActivity(act)` (`0x10289efc JMP 0x100097d2` ->
	// `0x10272650`). The play that follows is the maintain loop's, off the ideal activity.
	if (ActivityNumber == RetailActivityId)                  // 0x10289eea CMP [ECX+0xfec],EAX
	{
		ActivityNumber = 0;                                  // 0x10289eee
	}
	SetIdealActivity(RetailActivityId);                      // 0x10289efc
}

FElysiumNpcBase::FHintRestoreResult FElysiumNpcBase::HintOnRestore(const FHintWords& Hint)
{
	// `CAI_Hint::OnRestore` — slot 130, `0x102d3ec0`, handed over by family Sounds.
	//
	// SLOT 130 IS `OnRestore`, not a sound handler: `vtmb_slot 130` shows `CAI_BaseNPC::OnRestore`
	// (`0x1027bf50`) and `CAI_BaseNPCTroika::OnRestore` (`0x102998c0`) filling it, and the
	// `CAISound::FUN_100aa5a0` this body opens with is `CBaseEntity::OnRestore`, whose whole body
	// tail-calls slot 6. `docs/vtmb/npc-ai/shape.md` § "Speech and the looping-sound stop" reads it
	// as a reaction to an AI sound; the section this family added supersedes that identification.
	//
	// The body, verbatim:
	//   1. the base `CBaseEntity::OnRestore`;
	//   2. resolve the hint's own AI-network node — `0x102d3e60` bounds-checks `m_nNodeID`
	//      (`+0x5e4`) against `(*DAT_1093407c)` and indexes `DAT_1093407c[1]`, bumping an error
	//      counter for an out-of-range id;
	//   3. NO NODE: `DevMsg("Warning: AI hint has incorrect origin")` and return;
	//   4. a node: `Teleport(&node->origin /*+0x08..+0x10*/, NULL, NULL)` through vtable `+0x2d4`,
	//      then claim the node by writing `this` into its `CAI_Hint*` slot at `+0xa0`.
	//
	// THIS RUNTIME HAS NO AI NETWORK, so step 2 never finds a node and step 3 is the arm every call
	// takes — which is a recovered arm, not a refusal. Ported as such.
	(void)Hint;
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Warning: AI hint has incorrect origin"));
	return FHintRestoreResult{};
}

int32 FElysiumNpcBase::ActivityIdForName(const FString& ActivityName) const
{
	// `ActivityList_IndexForName` (`0x10412520`) over retail's registered enum, case-folded; -1 is
	// retail's own "not in the table" answer.
	return ElysiumRetailActivities::ValueOf(ActivityName);
}

void FElysiumNpcBase::SetMotorHintYaw(float Yaw)
{
	// `0x102e2020`'s tail after its yaw computation (`0x10014f0b`, done by the caller): the `+0x28`
	// animation-movement flip (`102e202d..102e205d`, `AND EAX,0x100`: below 180.0 or unordered adds
	// the half turn), then `102e2061 CMP [+0x1c],180.0f` -> `102e206e` the direct store into
	// `motor+0x34`. The clamped arm (`0x102e0a80`) needs the `+0x1c` max-yaw word no port motor
	// carries, so the direct store is the arm taken, as `NPCInit` / `0x102e1c10` take it. No
	// `UpdateYaw`: `0x102e2020` does not call it. `Yaw` is a RETAIL (Source) yaw. (Story 8 L05
	// integration: was an empty seam.)
	float Ideal = Yaw;
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		Ideal = !(Ideal >= MotorYawHalfTurn) ? Ideal + MotorYawHalfTurn : Ideal - MotorYawHalfTurn;
	}
	MotorIdealYaw = Ideal;                               // 102e206e motor+0x34
}

void FElysiumNpcBase::ReleaseMotorHintYaw()
{
	// `0x102e1e20(motor, -1)` — `UpdateYaw(-1)`, the turn toward `motor+0x34` the interest bodies
	// end with. (The claim's opening `0x102e0b40` is `MotorMoveStop()`, called at its site.) Wired
	// at story 8 wave 2: the navigator step keeps `motor+0x34` at the travel yaw while a route runs
	// (`NavigatorMoveStep`, retail's `MoveExecute`), so the word is live after a walk.
	MotorUpdateYaw(-1);
}

FVector FElysiumNpcBase::HintComparePosition(const FElysiumEntity* Entity) const
{
	// SEAM for the vtable `+0x370` accessor (slot 220). `0x103bfa50` uses it for BOTH sides of its
	// squared-distance compare, so answering the origin keeps the comparison self-consistent even
	// though the accessor itself is unidentified. **Unrecovered:** what slot 220 returns.
	return Entity ? Entity->Origin : FVector::ZeroVector;
}

// -------------------------------------------------------------------------------------------------
// Slot 568 `GetHintDelay` — the Troika-line body, filled by 77 classes with no override.
// -------------------------------------------------------------------------------------------------

float FElysiumNpcBase::GetHintDelay(int16)
{
	// `0x1026a910`. The whole function is two instructions:
	//     1026a910  FLD float ptr [0x104454c4]
	//     1026a916  RET 0x4
	// `_DAT_104454c4` is the image's shared 0.0f. The `short` argument is not read.
	return NpcKernelHintsShared::GHintsZero;
}

void FElysiumNpcBase::SetHintGroup(const FString& NewHintGroup)
{
	// `0x102781e0`. Retail reads the old `m_strHintGroup` (`+0x5db0`), assigns the new one, and
	// dispatches slot 551 `OnChangeHintGroup(old, new)` — vtable `+0x89c`, `0x89c / 4 == 551` —
	// ONLY when the two differ. Both arguments are the string_t values, old first.
	const FString Old = BaseScheduleHost.HintGroup;
	BaseScheduleHost.HintGroup = NewHintGroup;
	if (!Old.Equals(NewHintGroup, ESearchCase::CaseSensitive))
	{
		// NAMED MODERNIZATION, one word wide: retail compares the two `string_t` POINTERS, so two
		// separately allocated strings with the same text would dispatch. VtMB interns keyfield
		// strings through `AllocPooledString`, which makes pointer equality text equality for every
		// authored path; the port compares the text.
		OnChangeHintGroup(FName(*Old), FName(*NewHintGroup));
	}
}

