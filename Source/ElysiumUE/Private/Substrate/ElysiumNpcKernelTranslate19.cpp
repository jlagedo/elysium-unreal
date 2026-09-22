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

	int32 Translate19FromEnum(int32 Id)
	{
		return Id;
	}

	int32 Translate19ToEnum(int32 Number)
	{
		static const int32 All[] = {
			ElysiumScheduleId::None,
			ElysiumSched::IDLE_STAND,
			ElysiumSched::FAIL,
			ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION,
			ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI,
			ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE,
			ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE,
			ElysiumSched::SCHED_TROIKA_TAKE_COVER_HINT_DOOR,
			ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1,
			ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_NR,
			ElysiumSched::SCHED_TROIKA_MELEE_DODGE,
			ElysiumSched::SCHED_TROIKA_MELEE_PREBLOCK,
			ElysiumSched::SCHED_TROIKA_MELEE_KICK,
			ElysiumSched::SCHED_TROIKA_MELEE_STEPBACK,
			ElysiumSched::SCHED_TROIKA_MELEE_IDLE,
			ElysiumSched::SCHED_TROIKA_MELEE_ADVANCE,
			ElysiumSched::SCHED_TROIKA_MELEE_CIRCLE_ADVANCE,
			ElysiumSched::SCHED_TROIKA_CHASE_ENEMY,
			ElysiumSched::SCHED_TROIKA_RANGE_ATTACK1_SHOOT_AT_HINT,
			ElysiumSched::SCHED_TROIKA_RUN_AWAY_FROM_ENEMY,
			ElysiumSched::SMALL_FLINCH,
			ElysiumSched::ALERT_SMALL_FLINCH,
			ElysiumSched::TAKE_COVER_FROM_ORIGIN,
			ElysiumSched::SCHED_TROIKA_MESMERIZED,
		};
		for (const int32 Id : All)
		{
			if (Id == Number && Number != 0)
			{
				return Id;
			}
		}
		return ElysiumSched::IDLE_STAND;
	}

	int32 Translate19SpeciesTable(FElysiumNpc& Npc, int32 Id, const TCHAR* Address);
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
	const int32 Saved = SpeciesDispatchingSlot;
	SpeciesDispatchingSlot = 0;
	const int32 Answer = TranslateScheduleRetail(Mapped);
	SpeciesDispatchingSlot = Saved;
	return Answer;
}

int32 FElysiumNpc::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (SpeciesDispatchingSlot != GTranslate19Slot)
	{
		const FElysiumNpcClassSlot* Override =
			ElysiumNpcKernelClass::OverrideOf(RetailClass(), GTranslate19Slot);
		if (Override != nullptr && Override->Address != nullptr
			&& FCString::Strcmp(Override->Address, TEXT("0x102b12f0")) != 0)
		{
			const FSpeciesDispatchScope Scope(*this, GTranslate19Slot);
			return Translate19SpeciesTable(*this, ScheduleNumber, Override->Address);
		}
	}

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
	LastTranslateScheduleRetail = TranslateScheduleRetail(Translate19FromEnum(Id));
	if (LastTranslateScheduleRetail == Translate19FromEnum(Id))
	{
		return Id;
	}
	const int32 Mapped = Translate19ToEnum(LastTranslateScheduleRetail);
	if (Mapped == ElysiumSched::IDLE_STAND
		&& LastTranslateScheduleRetail != 1 && LastTranslateScheduleRetail != 0)
	{
		ElysiumStub::Fired(TEXT("schedule"), TEXT("TranslateSchedule registry miss"),
			DebugString(), *FString::Printf(TEXT("0x%x"), LastTranslateScheduleRetail),
			TEXT("0002/25: untranslated IDLE_STAND"));
	}
	return Mapped;
}

namespace
{
	int32 Translate19SpeciesTable(FElysiumNpc& Npc, int32 Id, const TCHAR* Address)
	{
		auto Troika = [&Npc, Id]()
		{
			return Npc.TranslateScheduleRetail(Id);
		};

		if (FCString::Strcmp(Address, TEXT("0x10362910")) == 0)
		{
			// `10362972`: the arm CALLS `CNPC_VAsianVampire::GetJumpSchedule` (`0x10362430`) and
			// returns its answer; Troika is never reached. The C renders the return value away.
			if (Id > 0xe4 && Id < 0xe7)
			{
				return Npc.GetJumpSchedule();
			}
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10363a30")) == 0)
		{
			if (Id == 0xb1) { return 0x15f; }
			if (Id == 0xdc) { return 0x15e; }
			if (Id == 0xef) { return 0x15f; }
			if (Id == 0x17 || Id == 0xed || Id == 0x15f)
			{
				if (!Npc.bBachMovementSpot)
				{
					return 0x15f;
				}
				// `10363a93`: the katana arm, four gates in retail's order. Each miss falls to
				// Troika.
				FElysiumItem* const Active = Npc.Inventory.Active(Npc);
				FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
				if (Weapon != nullptr && Weapon->Def != nullptr
					&& Weapon->Def->Classname.Equals(GTranslate19BachKatana,
						ESearchCase::IgnoreCase)
					// `10363ac2 TEST EAX,EAX / JZ`: sequence index **zero refuses**, unlike every
					// other `SelectWeightedSequence` probe in the port, which tests INDEX_NONE.
					&& Npc.SelectWeightedSequenceForActivity(0x10) != 0
					// `10363acf`: `STOP_BACKUP 0x2c` SET sends the arm to Troika.
					&& !Npc.Cognition.Conditions.Has(EElysiumNpcCond::StopBackup))
				{
					return 0x160;
				}
			}
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x1036b460")) == 0)
		{
			return Id == 0x17 ? 0x15d : Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10372150")) == 0)
		{
			if (Id == 0x6b)
			{
				return Npc.NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
			}
			if (Id == 0x89) { return 0x15e; }
			if (Id == 0x94) { return 0x15c; }
			if (Id == 0x96) { return 0x15d; }
			if (Id == 0x103) { return 0x15a; }
			if (Id >= 0xaa && Id <= 0xb7)
			{
				return 0x160 + (Id - 0xaa);
			}
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10374370")) == 0)
		{
			if (Id == 0x6b) { return 0x164; }
			if (Id == 0x156) { return 0x161; }
			if (Id == 0x157) { return 0x162; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10375f20")) == 0)
		{
			if (const int32 Frenzied = Npc.FrenziedTranslateSchedule(Id); Frenzied != 0)
			{
				return Frenzied;
			}
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10378a30")) == 0)
		{
			if (Id == 0x5b) { return 0x15d; }
			if (Id == 0xdc) { return 0x15f; }
			if (Id == 0xdd) { return 0x160; }
			if (Id == 0xea) { return 0x15e; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x1037d240")) == 0)
		{
			if (Id == 0x6b)
			{
				return Npc.NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15a;
			}
			if (Id == 0x103) { return 0x159; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x1037ffa0")) == 0)
		{
			if (Id != 0x16e && Npc.HengeyokaiSkin == 1)
			{
				++Npc.HengeyokaiThawCalls;
			}
			if (Id == 0x5b) { return 0x15a; }
			if (Id == 0xdc) { return 0x15c; }
			if (Id == 0xdd) { return 0x15d; }
			if (Id == 0xea) { return 0x15b; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10388a40")) == 0)
		{
			if (Id == 0x6b)
			{
				return Npc.NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
			}
			if (Id == 0x103) { return 0x15a; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x10394570")) == 0)
		{
			return Id == 0x147 ? 0x16f : Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x1039e2d0")) == 0)
		{
			if (Id == 0xc7) { return 0xc8; }
			if (Id == 0x147) { return 0x16d; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103a7390")) == 0)
		{
			if (Id > 0xe4 && Id < 0xe7)
			{
				return 0x15f;
			}
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103ac490")) == 0)
		{
			if (Id == 0x6b) { return 0x161; }
			if (Id == 0x156) { return 0x15e; }
			if (Id == 0x157) { return 0x15f; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103b0320")) == 0)
		{
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103bd390")) == 0)
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
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103c1720")) == 0)
		{
			if (Id == 0xbf) { return 0x157; }
			if (Id == 0xc7) { return 0xc8; }
			if (Id == 0xe7) { return 0x156; }
			if (Id == 0xed || Id == 0xef) { return 0xf0; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103c3560")) == 0)
		{
			if (Id == 0xc7) { return 0xc8; }
			if (Id == 0xe7) { return 0x156; }
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103d5e00")) == 0)
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
			return Troika();
		}
		if (FCString::Strcmp(Address, TEXT("0x103df580")) == 0)
		{
			if (Id == 0x5b)
			{
				// `103df58e`: the console line at `0x10665640`, part of the contract a map author
				// reads — the zombie says it is ignoring the schedule while translating it.
				Npc.RecordScheduleEvent(TEXT(
					"npc_zombie: encountered schedule:[investigate unknown] ...ignoring!"));
				return 0x165;
			}
			if (Id == 0x156) { return 0x15e; }
			if (Id == 0x157) { return 0x15f; }
			return Troika();
		}
		return Troika();
	}
}
