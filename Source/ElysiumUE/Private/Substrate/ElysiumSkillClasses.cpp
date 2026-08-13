#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMover.h"

#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkill, Log, All);

namespace
{
	const FName GOnSkillAttemptBegin(TEXT("OnSkillAttemptBegin"));
	const FName GOnSkillAttemptCycle(TEXT("OnSkillAttemptCycle"));
	const FName GOnSkillSuccess(TEXT("OnSkillSuccess"));
	const FName GOnSkillFail(TEXT("OnSkillFail"));
	const FName GOnSkillBotch(TEXT("OnSkillBotch"));
	const FName GOnUnlocked(TEXT("OnUnlocked"));
	const FName GOnUseBegin(TEXT("OnUseBegin"));
	const FName GOnUseEnd(TEXT("OnUseEnd"));

	const TCHAR* FeatForSkillType(int32 SkillType)
	{
		return SkillType == 1 ? TEXT("Intrusion") : SkillType == 2 ? TEXT("Hacking") : nullptr;
	}

	template <typename TObject, typename TMember>
	void AddLeafField(FElysiumClassDesc& D, const TCHAR* Name, TMember TObject::* Member)
	{
		FElysiumFieldAccessor Accessor;
		Accessor.ApplyFlags(ElysiumFieldDefault);
		if constexpr (std::is_same_v<TMember, int32>)
		{
			Accessor.Type = EElysiumVariantType::Int;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::Int(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, bool>)
		{
			Accessor.Type = EElysiumVariantType::Bool;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::Bool(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Accessor.Type = EElysiumVariantType::String;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::String(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToString(); };
		}
		D.Fields.Add(FName(Name), MoveTemp(Accessor));
	}
}

void FElysiumSkillEntity::InputResetDifficulty(int32 NewDifficulty)
{
	Difficulty = FMath::Clamp(NewDifficulty, 0, 10);
	SkillAttempts = 0;
}

float FElysiumSkillEntity::AttemptIntervalSeconds(int32 Rating) const
{
	// vampire.dll FUN_1020aea0: (5.0 - rating * 0.25) / m_flSpeedScale. The current player
	// runtime carries the retail default scale of 1; when speed-scale effects land this denominator
	// becomes the shared character value rather than a lock-local setting.
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
	UE_LOG(LogElysiumSkill, Display,
		TEXT("%s: %s attempt started (rating %d, difficulty %d); Intrusion HUD stubbed"),
		*DebugString(), Feat, Rating, Difficulty);
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
	// FUN_1020b090 selects the non-roll helper: a normal Intrusion/Hacking attempt can only
	// produce generic result tier 3 (pass) or 1 (fail). `diceroll` is a dead Hammer key here.
	LastRoll = Rating >= Difficulty ? 3 : 1;
	LastAttemptSeconds = static_cast<float>(World->NowSeconds());
	FireOutput(GOnSkillAttemptCycle, User.Handle);
	++SkillAttempts;
	UE_LOG(LogElysiumSkill, Display, TEXT("%s: %s attempt resolved — %s (%d >= %d); HUD stubbed"),
		*DebugString(), FeatName, LastRoll > 2 ? TEXT("Success") : TEXT("Failure"),
		Rating, Difficulty);

	if (LastRoll > 2)
	{
		FireOutput(GOnSkillSuccess, User.Handle);
		OnSkillSucceeded(User);
		return;
	}
	FireOutput(LastRoll == 0 ? GOnSkillBotch : GOnSkillFail, User.Handle);
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

void FElysiumLockableEntity::Spawn()
{
	LastRoll = Difficulty != 0 ? 1 : 3;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
	{
		return;
	}
	const FString Stem = Def->ModelMesh.IsEmpty()
		? FElysiumContentPaths::PropModelStem(Model) : Def->ModelMesh;
	if (Stem.IsEmpty())
	{
		return;
	}
	const FQuat Rotation = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f)) : Def->ModelQuat;
	WorldBody = Embodiment->BuildPropVisual(
		Stem, Origin, Rotation, Embodiment->BodyScaleFor(*Def));
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
	}
}

void FElysiumLockableEntity::PostSpawn()
{
	FElysiumEntity::PostSpawn();
	FElysiumEntity* Parent = World && !ParentName.IsEmpty() ? World->FindByName(ParentName) : nullptr;
	FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr;
	if (!Door)
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s attached to non-existent door: %s"),
			*DebugString(), *ParentName);
		Kill();
		return;
	}
	AttachedDoor = Door->Handle;
	Door->RegisterDoorknob(*this);
}

void FElysiumLockableEntity::InputLock()
{
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedDoor) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputLock();
			return;
		}
	}
	ApplyDoorLockState(true);
}

void FElysiumLockableEntity::InputUnlock(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUnlocked, Activator);
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedDoor) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputUnlock();
			return;
		}
	}
	ApplyDoorLockState(false);
}

void FElysiumLockableEntity::ApplyDoorLockState(bool bLocked)
{
	LastRoll = bLocked ? 1 : 3;
}

FElysiumUseBeginResult FElysiumLockableEntity::BeginPlayerUse(const FElysiumUseContext& Context)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		return FElysiumUseBeginResult::Completed();
	}
	if (!IsUseLocked())
	{
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (!KeyName.IsEmpty() && User->Inventory.Has(*User, KeyName))
	{
		if (bDeleteKey)
		{
			User->Inventory.ScriptRemove(*User, KeyName);
		}
		InputUnlock(Context.Activator);
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (bRequiresKey || !User->Inventory.Has(*User, TEXT("item_g_lockpick")))
	{
		UE_LOG(LogElysiumSkill, Display, TEXT("%s: locked use refused — %s"), *DebugString(),
			bRequiresKey ? TEXT("requires key") : TEXT("no item_g_lockpick"));
		return FElysiumUseBeginResult::Completed();
	}
	FireOutput(GOnUseBegin, Context.Activator);
	bUseOutputsOpen = true;
	if (StartAttempt(*User))
	{
		return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::WhileHeld);
	}
	FinishUseOutputs(Context.Activator);
	return FElysiumUseBeginResult::Completed();
}

void FElysiumLockableEntity::EndPlayerUse(const FElysiumUseContext& Context,
	EElysiumUseEndReason Reason)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (User)
	{
		if (const TCHAR* Feat = FeatForSkillType(SkillType))
		{
			LastSkillLevel = User->CalcFeat(Feat);
		}
		OnAttemptStopped(*User, Reason);
	}
	else
	{
		StopAttempt();
	}
	FinishUseOutputs(Context.Activator);
}

void FElysiumLockableEntity::Use(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUseBegin, Activator);
	ForwardUse(Activator);
	FireOutput(GOnUseEnd, Activator);
}

void FElysiumLockableEntity::OnSkillSucceeded(FElysiumCombatCharacter& User)
{
	InputUnlock(User.Handle);
	if (World)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed);
	}
	ForwardUse(User.Handle);
}

void FElysiumLockableEntity::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	StopAttempt();
}

void FElysiumLockableEntity::FinishUseOutputs(const FElysiumEntityHandle& Activator)
{
	if (bUseOutputsOpen)
	{
		bUseOutputsOpen = false;
		FireOutput(GOnUseEnd, Activator);
	}
}

void FElysiumLockableEntity::ForwardUse(const FElysiumEntityHandle&) {}

void FElysiumPropDoorknob::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedDoor) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
	}
}

void FElysiumLockableEntity::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

UPrimitiveComponent* FElysiumLockableEntity::GetAttachBody() const
{
	return WorldBody;
}

void FElysiumLockableEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumSkillEntity::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), IsUseLocked() ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Attached door"), AttachedDoor.IsSet() ? AttachedDoor.ToString() : TEXT("(none)"));
}

namespace
{
	TUniquePtr<FElysiumEntity> MakeSkillEntity() { return MakeUnique<FElysiumSkillEntity>(); }
	TUniquePtr<FElysiumEntity> MakeLockableEntity() { return MakeUnique<FElysiumLockableEntity>(); }
	TUniquePtr<FElysiumEntity> MakeDoorknob() { return MakeUnique<FElysiumPropDoorknob>(); }

	struct FElysiumSkillRegistrar
	{
		FElysiumSkillRegistrar()
		{
			FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
			FElysiumClassDesc& Skill = Registry.Register(ElysiumSkillEntityClassName(),
				ElysiumBaseClassName(), &MakeSkillEntity);
			Skill.Input(TEXT("ResetDifficulty"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumSkillEntity&>(E).InputResetDifficulty(A.Param.ToInt()); });
			AddLeafField(Skill, TEXT("difficulty"), &FElysiumSkillEntity::Difficulty);
			AddLeafField(Skill, TEXT("skilltype"), &FElysiumSkillEntity::SkillType);

			FElysiumClassDesc& Lockable = Registry.Register(ElysiumLockableEntityClassName(),
				ElysiumSkillEntityClassName(), &MakeLockableEntity);
			Lockable.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumLockableEntity&>(E).InputLock(); });
			Lockable.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).InputUnlock(A.Activator); });
			Lockable.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).Use(A.Activator); });
			AddLeafField(Lockable, TEXT("key_name"), &FElysiumLockableEntity::KeyName);
			AddLeafField(Lockable, TEXT("delete_key"), &FElysiumLockableEntity::bDeleteKey);
			AddLeafField(Lockable, TEXT("requires_key"), &FElysiumLockableEntity::bRequiresKey);

			Registry.Register(FName(TEXT("prop_doorknob")),
				ElysiumLockableEntityClassName(), &MakeDoorknob);
		}
	};

	const FElysiumSkillRegistrar GSkillRegistrar;
}
