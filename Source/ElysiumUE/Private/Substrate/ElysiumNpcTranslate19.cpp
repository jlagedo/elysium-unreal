#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Translate19** — slot 440 whole.

namespace
{
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

int32 FElysiumNpc::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// Twenty species classes override this method on their C++ classes (story 5 step 3; the
	// controller line's `CNPC_VFrenzyShadow` `0x10375f20` since fold A2), each ending in a direct
	// call into the Troika body `0x102b12f0` (`TroikaTranslateScheduleRetail`).
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

int32 FElysiumNpc::TroikaTranslateScheduleRetail(int32 ScheduleNumber)
{
	// `CAI_BaseNPCTroika::TranslateSchedule` `0x102b12f0`.
	if (HasFrenzied(GTranslate19FrenziedBit))
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
	return FElysiumNpcBase::TranslateSchedule(ScheduleNumber);
}

int32 FElysiumNpc::TranslateSchedule(int32 Id)
{
	// Slot 440 returns the number verbatim. Only GetScheduleOfType may decide it is absent.
	LastTranslateScheduleRetail = TranslateScheduleRetail(Id);
	return LastTranslateScheduleRetail;
}
