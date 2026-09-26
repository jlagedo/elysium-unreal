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
	constexpr TCHAR GTranslate19BachKatana[] = TEXT("item_w_katana");   // 0x10587668
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
		if (HintWords(ScheduleHost.HintNode, Hint) && Hint.HintType == GTranslate19HintTypeDoor)
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

// `0x10362910`, `CNPC_VAsianVampire::TranslateSchedule`, the body of `FElysiumNpcAsianVampire::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::AsianVampireTranslateSchedule(int32 Id)
{
	// `10362972`: the arm CALLS `CNPC_VAsianVampire::GetJumpSchedule` (`0x10362430`) and
	// returns its answer; Troika is never reached. The C renders the return value away.
	if (Id > 0xe4 && Id < 0xe7)
	{
		return GetJumpSchedule();
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x10363a30`, `CNPC_VBach::TranslateSchedule`, the body of `FElysiumNpcBach::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::BachTranslateSchedule(int32 Id)
{
	if (Id == 0xb1) { return 0x15f; }
	if (Id == 0xdc) { return 0x15e; }
	if (Id == 0xef) { return 0x15f; }
	if (Id == 0x17 || Id == 0xed || Id == 0x15f)
	{
		if (!bBachMovementSpot)
		{
			return 0x15f;
		}
		// `10363a93`: the katana arm, four gates in retail's order. Each miss falls to
		// Troika.
		FElysiumItem* const Active = Inventory.Active(*this);
		FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
		if (Weapon != nullptr && Weapon->Def != nullptr
			&& Weapon->Def->Classname.Equals(GTranslate19BachKatana,
				ESearchCase::IgnoreCase)
			// `10363ac2 TEST EAX,EAX / JZ`: sequence index **zero refuses**, unlike every
			// other `SelectWeightedSequence` probe in the port, which tests INDEX_NONE.
			&& SelectWeightedSequenceForActivity(0x10) != 0
			// `10363acf`: `STOP_BACKUP 0x2c` SET sends the arm to Troika.
			&& !Cognition.Conditions.Has(EElysiumNpcCond::StopBackup))
		{
			return 0x160;
		}
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x1036b460`, `CNPC_VChangBros::TranslateSchedule`, the body of `FElysiumNpcChangBros::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::ChangBrosTranslateSchedule(int32 Id)
{
	return Id == 0x17 ? 0x15d : TroikaTranslateScheduleRetail(Id);
}

// `0x10372150`, `CNPC_VCop::TranslateSchedule`, the body of `FElysiumNpcCop::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::CopTranslateSchedule(int32 Id)
{
	if (Id == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
	}
	if (Id == 0x89) { return 0x15e; }
	if (Id == 0x94) { return 0x15c; }
	if (Id == 0x96) { return 0x15d; }
	if (Id == 0x103) { return 0x15a; }
	if (Id >= 0xaa && Id <= 0xb7)
	{
		return 0x160 + (Id - 0xaa);
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x10374370`, `CNPC_VDog::TranslateSchedule`, the body of `FElysiumNpcDog::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::DogTranslateSchedule(int32 Id)
{
	if (Id == 0x6b) { return 0x164; }
	if (Id == 0x156) { return 0x161; }
	if (Id == 0x157) { return 0x162; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x10378a30`, `CNPC_VGargoyle::TranslateSchedule`, the body of `FElysiumNpcGargoyle::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::GargoyleTranslateSchedule(int32 Id)
{
	if (Id == 0x5b) { return 0x15d; }
	if (Id == 0xdc) { return 0x15f; }
	if (Id == 0xdd) { return 0x160; }
	if (Id == 0xea) { return 0x15e; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x1037d240`, `CNPC_VGuard1::TranslateSchedule`, the body of `FElysiumNpcGuard1::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::Guard1TranslateSchedule(int32 Id)
{
	if (Id == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15a;
	}
	if (Id == 0x103) { return 0x159; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x1037ffa0`, `CNPC_VHengeyokai::TranslateSchedule`, the body of `FElysiumNpcHengeyokai::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::HengeyokaiTranslateSchedule(int32 Id)
{
	if (Id != 0x16e && HengeyokaiSkin == 1)
	{
		++HengeyokaiThawCalls;
	}
	if (Id == 0x5b) { return 0x15a; }
	if (Id == 0xdc) { return 0x15c; }
	if (Id == 0xdd) { return 0x15d; }
	if (Id == 0xea) { return 0x15b; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x10388a40`, `CNPC_VHunter::TranslateSchedule`, the body of `FElysiumNpcHunter::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::HunterTranslateSchedule(int32 Id)
{
	if (Id == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
	}
	if (Id == 0x103) { return 0x15a; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x10394570`, `CNPC_VMingXiao::TranslateSchedule`, the body of `FElysiumNpcMingXiao::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::MingXiaoTranslateSchedule(int32 Id)
{
	return Id == 0x147 ? 0x16f : TroikaTranslateScheduleRetail(Id);
}

// `0x1039e2d0`, `CNPC_VMingXiaoTentacle::TranslateSchedule`, the body of `FElysiumNpcMingXiaoTentacle::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::MingXiaoTentacleTranslateSchedule(int32 Id)
{
	if (Id == 0xc7) { return 0xc8; }
	if (Id == 0x147) { return 0x16d; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103a7390`, `CNPC_VSabbatLeader::TranslateSchedule`, the body of `FElysiumNpcSabbatLeader::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::SabbatLeaderTranslateSchedule(int32 Id)
{
	if (Id > 0xe4 && Id < 0xe7)
	{
		return 0x15f;
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103ac490`, `CNPC_VScurrying::TranslateSchedule`, the body of `FElysiumNpcScurrying::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::ScurryingTranslateSchedule(int32 Id)
{
	if (Id == 0x6b) { return 0x161; }
	if (Id == 0x156) { return 0x15e; }
	if (Id == 0x157) { return 0x15f; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103b0320`, `CNPC_VSheriffMan::TranslateSchedule`, the body of `FElysiumNpcSheriffMan::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::SheriffManTranslateSchedule(int32 Id)
{
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103bd390`, `CNPC_VTzimisce::TranslateSchedule`, the body of `FElysiumNpcTzimisce::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::TzimisceTranslateSchedule(int32 Id)
{
	// Retail stamps its own `__FILE__`/`__LINE__` (lines `0xd00`–`0xd0f`) into `+0x1b30`/
	// `+0x1b34` on each of these fifteen rows BEFORE answering, and the default arm writes
	// neither — the pair records "this class decided". The shape map calls that pair
	// ABSENT; the mind's transition trace carries the same account.
	struct FRow { int32 From; int32 To; };
	static const FRow Rows[] = {
		{ 1, 0x157 }, { 5, 0x19b }, { 6, 0x158 },
		{ 0xf, 0x166 }, { 0x10, 0x16a }, { 0x15, 0x16c },
		{ 0x21, 0x184 }, { 0x22, 0x185 }, { 0x25, 0x16d },
		{ 0x28, 0x16e }, { 0x2f, 0x188 }, { 0x30, 0x189 },
		{ 0x31, 0x18a }, { 0x32, 0x18b }, { 0x33, 0x18c },
	};
	for (const FRow& Row : Rows)
	{
		if (Row.From == Id)
		{
			return Row.To;
		}
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103c1720`, `CNPC_VTzimisceHeadClaw::TranslateSchedule`, the body of `FElysiumNpcTzimisceHeadClaw::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::TzimisceHeadClawTranslateSchedule(int32 Id)
{
	if (Id == 0xbf) { return 0x157; }
	if (Id == 0xc7) { return 0xc8; }
	if (Id == 0xe7) { return 0x156; }
	if (Id == 0xed || Id == 0xef) { return 0xf0; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103c3560`, `CNPC_VTzimisceRunner::TranslateSchedule`, the body of `FElysiumNpcTzimisceRunner::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::TzimisceRunnerTranslateSchedule(int32 Id)
{
	if (Id == 0xc7) { return 0xc8; }
	if (Id == 0xe7) { return 0x156; }
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103d5e00`, `CNPC_VWerewolf::TranslateSchedule`, the body of `FElysiumNpcWerewolf::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::WerewolfTranslateSchedule(int32 Id)
{
	// `103d5e00`: three rows in retail's order. `0x43 SCHED_FAIL` is one of them, so a
	// werewolf never runs the base FAIL program. Retail stamps its `__FILE__`/`__LINE__`
	// (lines `0x1134`, `0x112f`, `0x112a`) into `+0x1b30`/`+0x1b34` on each; the shape map
	// calls that pair ABSENT.
	struct FWolfRow { int32 From; int32 To; };
	static const FWolfRow WolfRows[] = {
		{ 0x43, 0x15c }, { 0xb7, 0x157 }, { 0xf0, 0xb1 },
	};
	for (const FWolfRow& Row : WolfRows)
	{
		if (Row.From == Id)
		{
			return Row.To;
		}
	}
	return TroikaTranslateScheduleRetail(Id);
}

// `0x103df580`, `CNPC_VZombie::TranslateSchedule`, the body of `FElysiumNpcZombie::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpc::ZombieTranslateSchedule(int32 Id)
{
	if (Id == 0x5b)
	{
		// `103df58e`: the console line at `0x10665640`, part of the contract a map author
		// reads — the zombie says it is ignoring the schedule while translating it.
		RecordScheduleEvent(TEXT(
			"npc_zombie: encountered schedule:[investigate unknown] ...ignoring!"));
		return 0x165;
	}
	if (Id == 0x156) { return 0x15e; }
	if (Id == 0x157) { return 0x15f; }
	return TroikaTranslateScheduleRetail(Id);
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
