#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"

// The skill family's shared log category; defined in `ElysiumSkillClasses.cpp`.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumSkill, Log, All);

inline FName ElysiumSkillEntityClassName()
{
	return FName(TEXT("CBaseVampireSkillEntity"));
}

// The shared timed-threshold state. One accepted use resolves one deterministic feat check; the
// five outputs always use the ordinary entity queue and the think only produces them. Presentation
// is deliberately diagnostic until the Intrusion view publishes this state.
class FElysiumSkillEntity : public FElysiumEntity
{
public:
	int32 Difficulty = 0;
	int32 SkillType = 0;
	int32 LastRoll = 3;
	float LastAttemptSeconds = 0.0f;
	int32 SkillAttempts = 0;
	int32 LastSkillLevel = 0;
	FElysiumEntityHandle AttemptUser;

	void InputResetDifficulty(int32 NewDifficulty);
	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual const TCHAR* SaveBlockReason() const override;

protected:
	// The feat a `skilltype` resolves against — 1 is Intrusion, 2 is Hacking, anything else none.
	static const TCHAR* FeatForSkillType(int32 SkillType);

	bool StartAttempt(FElysiumCombatCharacter& User);
	void StopAttempt();
	virtual int32 AttemptDifficulty() const { return Difficulty; }
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) {}
	virtual void OnSkillFailed(FElysiumCombatCharacter& User);
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) {}

private:
	void ResolveAttempt(FElysiumCombatCharacter& User);
	float AttemptIntervalSeconds(int32 Rating) const;
};

// The terminal and lockable subsystems each live in their own file on this chain; the skill
// family's includers reach them through here. (These stay below the base class they derive from.)
#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumTerminal.h"
