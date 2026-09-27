// `CAI_BaseNPC`'s bodies of the `Conditions10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseConditions10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::CanBeFedUponTemplate() const
{
	// `CBaseCombatCharacter::CanBeFedUpon` (`0x10339a90`): `GetCharTemplate(this)->+0x95 == 0`.
	// **SEAM**: `+0x95` has no recovered column name and `FElysiumClanTemplate` exposes none, so
	// this answers TRUE — retail's own answer for a template byte of zero, and the ADMITTING arm.
	return true;
}

bool FElysiumNpcBase::IsUnconsciousMiscFlag() const
{
	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) past its scope-trace push:
	// `return (m_iMiscFlags & 1) != 0`. Bit 0 of the name table at `0x10619ec8` is `Unconscious`.
	return ElysiumMiscFlags::Has(MiscFlags, ElysiumMiscFlags::Unconscious);
}

bool FElysiumNpcBase::CanBeFedUponBy(FElysiumEntity* Feeder)
{
	// `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), 237 bytes. **The feeder argument is
	// never read**: every one of the five terms is about the victim, which is exactly why the Troika
	// override above it has to make the follower test itself.
	(void)Feeder;

	// `1033988a`: `CanBeFedUpon()`.
	if (!CanBeFedUponTemplate())
	{
		return false;
	}
	// `1033989a`: `m_bfAINPCFlags2 & 0x8000000` — `NOT_FEEDABLE`.
	if (NpcFlags.Has(EElysiumNpcFlag2::NOT_FEEDABLE))
	{
		return false;
	}
	// `103398a6`: a live grapple refuses. The handle `m_GrapplePartner` (`+0x1538`) must fail to
	// resolve, OR `m_GrappleRole` (`+0x153c`) must be -1; anything else is a body already in a pair.
	{
		const FElysiumEntity* const Partner =
			World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;
		if (Partner != nullptr && Grapple.Role != EElysiumGrappleRole::None)
		{
			return false;
		}
	}
	// `103398f1`: slot 158 `IsAlive()` (`vtable +0x278`).
	if (!IsAlive())
	{
		return false;
	}
	// `103398fa`: `IsUnconscious()`.
	if (IsUnconsciousMiscFlag())
	{
		return false;
	}
	return true;
}

bool FElysiumNpcBase::NavIsGoalSet() const
{
	// `0x102ee2e0` — `CAI_Navigator::IsGoalSet()`, `m_pPath(+0x30)->GoalType(+0x10) != 0`. DISTINCT
	// from `0x102ee680` (`IsGoalActive`, the current-waypoint test) which family Motor wires to the
	// same latch. **SEAM**: the mover keeps one goal latch and no goal-type word, so this answers
	// that latch — the admitting value, since `IsGoalActive` implies `IsGoalSet`. The one case it
	// under-admits is a goal set with no current waypoint, which the mover cannot represent.
	return NavIsGoalActive();
}

void FElysiumNpcBase::Slot532(int32 FailureBits)
{
	// `CAI_BaseNPC::vfunc532` (`0x1027e0f0`): the whole body is the two door words and `return 1`.
	(void)FailureBits;
	OpeningDoor = FElysiumEntityHandle::Invalid();                       // +0x5d24
	bOpeningDoorWait = false;                                            // +0x5d30
}
