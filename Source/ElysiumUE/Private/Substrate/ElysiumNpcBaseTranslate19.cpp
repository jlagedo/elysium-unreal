// `CAI_BaseNPC`'s bodies of the `Translate19` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseTranslate19.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::TranslateSchedule(int32 ScheduleNumber)
{
	if (ScheduleNumber != 0x2e)
	{
		return ScheduleNumber;
	}
	int32 Mapped = ScheduleNumber;
	// `102cc091`: `m_hCine` (`+0x5d74`) resolved with the serial check. The shape map binds that
	// word to `FElysiumEntity::ScriptOwner`, and family Anim already reads it as
	// `ScriptOwnerIsLive()` — so the failure arm is a REAL test, not a seam.
	if (!ScriptOwnerIsLive())
	{
		// `102cc0cc`: `DevWarning(2, "Script failed for %s\n", GetClassname())`, then
		// `CineCleanup` (`0x1027d170`), then `slot440(1)`.
		RecordScheduleEvent(FString::Printf(TEXT("Script failed for %s"), *DebugString()));
		++TranslateCineCleanupCalls;
		FElysiumScriptedSequence::CineCleanup(*this);
		Mapped = 1;
	}
	else
	{
		switch (TranslateCineMoveTo())
		{
		case 0:
		case 4:
			Mapped = 0x32;
			break;
		case 1:
			Mapped = 0x2f;
			break;
		case 2:
			Mapped = 0x30;
			break;
		case 3:
			Mapped = 0x31;
			break;
		case 5:
			Mapped = 0x33;
			break;
		default:
			return ScheduleNumber;
		}
	}
	// `slot440(mapped)` through the vtable: a species class's own body answers (the Troika line's
	// `TranslateSchedule` reaches `TranslateScheduleRetail`, which species override).
	return TranslateSchedule(Mapped);
}

int32 FElysiumNpcBase::TranslateCineMoveTo() const
{
	const FElysiumScriptedSequence* Cine = ResolveCine();
	return Cine != nullptr ? Cine->MoveTo : 0;
}
