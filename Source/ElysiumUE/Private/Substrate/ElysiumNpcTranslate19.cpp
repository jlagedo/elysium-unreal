#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Translate19** — slot 440 whole.

namespace
{
	constexpr int32 GTranslate19Slot = 440;
	constexpr uint32 GTranslate19FrenziedBit = 0x100;
	constexpr int32 GTranslate19HintTypeDoor = 0x2774;
}

int32 FElysiumNpc::FrenziedTranslateSchedule(int32 ScheduleNumber)
{
	switch (ScheduleNumber)
	{
	case 0x87:
	case 0x88:
		if (!NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH))
		{
			return GetEnemy() != nullptr ? 0x7c : 0x7d;
		}
		return 0x7e;
	case 0xc7:
		return 0xc9;
	case 0xca:
	case 0xcb:
	case 0xd1:
	case 0xd2:
		return 0xcc;
	case 0xef:
		return 0xf0;
	default:
		return 0;
	}
}

int32 FElysiumNpc::BaseTranslateSchedule(int32 ScheduleNumber)
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
		Mapped = 1;
	}
	else
	{
		switch (TranslateCineMoveTo)
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
	// `slot440(mapped)` through the vtable: a species class's own body answers.
	return TranslateScheduleRetail(Mapped);
}

int32 FElysiumNpc::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// Nineteen species classes override this method on their C++ classes (story 5 step 3), each
	// ending in a direct call into the Troika body `0x102b12f0` (`TroikaTranslateScheduleRetail`).
	// The controller line's `0x10375f20` (`CNPC_VFrenzyShadow`) stays a census arm until the
	// controller fold (step 7): only a test-latched Troika-line instance reaches it.
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GTranslate19Slot);
	if (Override != nullptr && Override->Address != nullptr
		&& FCString::Strcmp(Override->Address, TEXT("0x10375f20")) == 0)
	{
		return FrenzyShadowTranslateSchedule(ScheduleNumber);
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

int32 FElysiumNpc::TroikaTranslateScheduleRetail(int32 ScheduleNumber)
{
	// `CAI_BaseNPCTroika::TranslateSchedule` `0x102b12f0`.
	if (NpcFlags.HasFrenzied(GTranslate19FrenziedBit))
	{
		if (const int32 Frenzied = FrenziedTranslateSchedule(ScheduleNumber); Frenzied != 0)
		{
			return Frenzied;
		}
	}

	switch (ScheduleNumber)
	{
	case 1:
	case 0x6b:
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x6b;
	case 2: return 0x46;
	case 3: return 0x47;
	case 6: return 0x4a;
	case 0xf: return 0xb1;
	case 0x10: return 0xb7;
	case 0x15: return 0xb8;
	case 0x21: return 0xed;
	case 0x22: return 0xee;
	case 0x25: return 0xc1;
	case 0x28: return 0xc2;
	case 0x2f: return 0xf2;
	case 0x30: return 0xf4;
	case 0x31: return 0xf6;
	case 0x32: return 0xf8;
	case 0x33: return 0xf9;
	case 0x77:
	{
		// `102b1408`: `m_pHintNode != 0 && *(int*)(hint + 0x5dc) == 0x2774`. `+0x5dc` is
		// `m_nHintType`, which family Hints owns as `FHintWords::HintType` — `HintWords()`
		// answering false covers retail's null-pointer half.
		FHintWords Hint;
		if (HintWords(BaseScheduleHost.HintNode, Hint) && Hint.HintType == GTranslate19HintTypeDoor)
		{
			return 0x78;
		}
		break;
	}
	case 0x94:
		if (GetFollowerBoss() != nullptr)
		{
			return 0x95;
		}
		break;
	case 0x96:
		if (GetFollowerBoss() != nullptr)
		{
			return 0x97;
		}
		break;
	default:
		break;
	}
	return BaseTranslateSchedule(ScheduleNumber);
}

int32 FElysiumNpc::TranslateSchedule(int32 Id)
{
	// Slot 440 returns the number verbatim. Only GetScheduleOfType may decide it is absent.
	LastTranslateScheduleRetail = TranslateScheduleRetail(Id);
	return LastTranslateScheduleRetail;
}

// `0x10375f20`, `CNPC_VFrenzyShadow::TranslateSchedule` — the controller line's, a census arm of
// `TranslateScheduleRetail` until the controller fold (step 7).
int32 FElysiumNpc::FrenzyShadowTranslateSchedule(int32 Id)
{
	if (const int32 Frenzied = FrenziedTranslateSchedule(Id); Frenzied != 0)
	{
		return Frenzied;
	}
	return TroikaTranslateScheduleRetail(Id);
}
