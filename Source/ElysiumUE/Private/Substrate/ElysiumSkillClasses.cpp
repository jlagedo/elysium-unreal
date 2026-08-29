#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumTerminal.h"

DEFINE_LOG_CATEGORY(LogElysiumSkill);

namespace
{
	const FName GOnSkillAttemptBegin(TEXT("OnSkillAttemptBegin"));
	const FName GOnSkillAttemptCycle(TEXT("OnSkillAttemptCycle"));
	const FName GOnSkillSuccess(TEXT("OnSkillSuccess"));
	const FName GOnSkillFail(TEXT("OnSkillFail"));
	const FName GOnSkillBotch(TEXT("OnSkillBotch"));
}

const TCHAR* FElysiumSkillEntity::FeatForSkillType(int32 SkillType)
{
	return SkillType == 1 ? TEXT("Intrusion") : SkillType == 2 ? TEXT("Hacking") : nullptr;
}

void FElysiumSkillEntity::InputResetDifficulty(int32 NewDifficulty)
{
	Difficulty = FMath::Clamp(NewDifficulty, 0, 10);
	SkillAttempts = 0;
}

float FElysiumSkillEntity::AttemptIntervalSeconds(int32 Rating) const
{
	// vampire.dll FUN_1020aea0: (5.0 - rating * 0.25) / m_flSpeedScale. The current player
	// runtime carries the retail default scale of 1.
	return FMath::Max(KINDA_SMALL_NUMBER, 5.0f - FMath::Max(0, Rating) * 0.25f);
}

bool FElysiumSkillEntity::StartAttempt(FElysiumCombatCharacter& User)
{
	const TCHAR* Feat = FeatForSkillType(SkillType);
	if (!World || !Feat || AttemptUser.IsSet())
	{
		return false;
	}
	AttemptUser = User.Handle;
	const int32 Rating = User.CalcFeat(Feat);
	if (Rating > LastSkillLevel)
	{
		SkillAttempts = 0;
	}
	LastAttemptSeconds = static_cast<float>(World->NowSeconds());
	FireOutput(GOnSkillAttemptBegin, User.Handle);
	NextThink = static_cast<float>(World->NowSeconds() + AttemptIntervalSeconds(Rating));
	const int32 EffectiveDifficulty = AttemptDifficulty();
	UE_LOG(LogElysiumSkill, Display,
		TEXT("%s: %s attempt started (rating %d, difficulty %d); Intrusion HUD stubbed"),
		*DebugString(), Feat, Rating, EffectiveDifficulty);
	return true;
}

void FElysiumSkillEntity::StopAttempt()
{
	AttemptUser = FElysiumEntityHandle::Invalid();
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumSkillEntity::ResolveAttempt(FElysiumCombatCharacter& User)
{
	const TCHAR* FeatName = FeatForSkillType(SkillType);
	if (!FeatName)
	{
		StopAttempt();
		return;
	}
	const int32 Rating = User.CalcFeat(FeatName);
	const int32 EffectiveDifficulty = AttemptDifficulty();
	// FUN_1020b090 selects the non-roll helper: a normal Intrusion/Hacking attempt can only
	// produce generic result tier 3 (pass) or 1 (fail). `diceroll` is a dead Hammer key here.
	LastRoll = Rating >= EffectiveDifficulty ? 3 : 1;
	LastAttemptSeconds = static_cast<float>(World->NowSeconds());
	FireOutput(GOnSkillAttemptCycle, User.Handle);
	++SkillAttempts;
	UE_LOG(LogElysiumSkill, Display, TEXT("%s: %s attempt resolved — %s (%d >= %d); HUD stubbed"),
		*DebugString(), FeatName, LastRoll > 2 ? TEXT("Success") : TEXT("Failure"),
		Rating, EffectiveDifficulty);

	if (LastRoll > 2)
	{
		FireOutput(GOnSkillSuccess, User.Handle);
		OnSkillSucceeded(User);
		return;
	}
	FireOutput(LastRoll == 0 ? GOnSkillBotch : GOnSkillFail, User.Handle);
	OnSkillFailed(User);
}

void FElysiumSkillEntity::OnSkillFailed(FElysiumCombatCharacter&)
{
	if (World)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed);
	}
}

void FElysiumSkillEntity::Think()
{
	FElysiumEntity* UserEntity = World ? World->Resolve(AttemptUser) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User || User->IsInert())
	{
		StopAttempt();
		return;
	}
	ResolveAttempt(*User);
}

void FElysiumSkillEntity::Serialize(FElysiumSaveArchive& Ar)
{
	Ar << LastRoll;
	Ar << LastAttemptSeconds;
	Ar << SkillAttempts;
	Ar << LastSkillLevel;
}

const TCHAR* FElysiumSkillEntity::SaveBlockReason() const
{
	return AttemptUser.IsSet() ? TEXT("a skill attempt is in progress") : nullptr;
}

void FElysiumSkillEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Skill"), FeatForSkillType(SkillType)
		? FString(FeatForSkillType(SkillType)) : TEXT("(none)"));
	Out.Emplace(TEXT("Difficulty"), FString::FromInt(Difficulty));
	Out.Emplace(TEXT("Last roll / lock"), FString::FromInt(LastRoll));
	Out.Emplace(TEXT("Attempts"), FString::FromInt(SkillAttempts));
	Out.Emplace(TEXT("Last skill level"), FString::FromInt(LastSkillLevel));
	Out.Emplace(TEXT("Attempt user"), AttemptUser.IsSet() ? AttemptUser.ToString() : TEXT("(none)"));
}

namespace
{
	TUniquePtr<FElysiumEntity> MakeSkillEntity() { return MakeUnique<FElysiumSkillEntity>(); }
	TUniquePtr<FElysiumEntity> MakeTerminal() { return MakeUnique<FElysiumTerminal>(); }
	TUniquePtr<FElysiumEntity> MakePropHacking() { return MakeUnique<FElysiumPropHacking>(); }
	TUniquePtr<FElysiumEntity> MakeLockableEntity() { return MakeUnique<FElysiumLockableEntity>(); }
	TUniquePtr<FElysiumEntity> MakeDoorknob() { return MakeUnique<FElysiumPropDoorknob>(); }
	TUniquePtr<FElysiumEntity> MakeElectronicDoorknob() { return MakeUnique<FElysiumElectronicDoorknob>(); }
	TUniquePtr<FElysiumEntity> MakeContainerLock() { return MakeUnique<FElysiumContainerLock>(); }
	TUniquePtr<FElysiumEntity> MakePadlock() { return MakeUnique<FElysiumPadlock>(); }

	struct FElysiumSkillRegistrar
	{
		FElysiumSkillRegistrar()
		{
			FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
			FElysiumClassDesc& Skill = Registry.Register(ElysiumSkillEntityClassName(),
				ElysiumBaseClassName(), &MakeSkillEntity);
			Skill.Input(TEXT("ResetDifficulty"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumSkillEntity&>(E).InputResetDifficulty(A.Param.ToInt()); });
			ElysiumAddClassField(Skill, TEXT("difficulty"), &FElysiumSkillEntity::Difficulty);
			ElysiumAddClassField(Skill, TEXT("skilltype"), &FElysiumSkillEntity::SkillType);

			FElysiumClassDesc& Terminal = Registry.Register(ElysiumTerminalClassName(),
				ElysiumSkillEntityClassName(), &MakeTerminal);
			Terminal.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumTerminal&>(E).InputEnable(); });
			Terminal.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumTerminal&>(E).InputDisable(); });
			ElysiumAddClassField(Terminal, TEXT("start_enabled"), &FElysiumTerminal::bStartEnabled);
			ElysiumAddClassField(Terminal, TEXT("textcolumns"), &FElysiumTerminal::TextColumns);
			ElysiumAddClassField(Terminal, TEXT("textrows"), &FElysiumTerminal::TextRows);
			ElysiumAddClassField(Terminal, TEXT("colorscheme"), &FElysiumTerminal::ColorScheme);
			ElysiumAddClassField(Terminal, TEXT("soundgroup"), &FElysiumTerminal::SoundGroup);

			FElysiumClassDesc& Hacking = Registry.Register(FName(TEXT("prop_hacking")),
				ElysiumTerminalClassName(), &MakePropHacking);
			ElysiumAddClassField(Hacking, TEXT("hack_file"), &FElysiumPropHacking::HackFile);
			ElysiumAddClassField(Hacking, TEXT("global_email"), &FElysiumPropHacking::bGlobalEmail);
			ElysiumAddClassField(Hacking, TEXT("ss_delay"), &FElysiumPropHacking::ScreenSaverDelay);
			ElysiumAddClassField(Hacking, TEXT("ss_start"), &FElysiumPropHacking::ScreenSaverStart);

			FElysiumClassDesc& Lockable = Registry.Register(ElysiumLockableEntityClassName(),
				ElysiumSkillEntityClassName(), &MakeLockableEntity);
			Lockable.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumLockableEntity&>(E).InputLock(); });
			Lockable.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).InputUnlock(A.Activator); });
			Lockable.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).Use(A.Activator); });
			ElysiumAddClassField(Lockable, TEXT("key_name"), &FElysiumLockableEntity::KeyName);
			ElysiumAddClassField(Lockable, TEXT("delete_key"), &FElysiumLockableEntity::bDeleteKey);
			ElysiumAddClassField(Lockable, TEXT("requires_key"), &FElysiumLockableEntity::bRequiresKey);
			ElysiumAddClassField(Lockable, TEXT("key_icon"), &FElysiumLockableEntity::KeyIcon);

			Registry.Register(FName(TEXT("prop_doorknob")),
				ElysiumLockableEntityClassName(), &MakeDoorknob);
			Registry.Register(FName(TEXT("prop_doorknob-wesp")),
				ElysiumLockableEntityClassName(), &MakeDoorknob);
			Registry.Register(FName(TEXT("prop_doorknob_electronic")),
				ElysiumLockableEntityClassName(), &MakeElectronicDoorknob);
			Registry.Register(FName(TEXT("item_container_lock")),
				ElysiumLockableEntityClassName(), &MakeContainerLock);
			Registry.Register(FName(TEXT("prop_padlock")),
				ElysiumLockableEntityClassName(), &MakePadlock);
		}
	};

	const FElysiumSkillRegistrar GSkillRegistrar;
}
