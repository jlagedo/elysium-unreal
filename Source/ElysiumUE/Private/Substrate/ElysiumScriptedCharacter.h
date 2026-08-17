#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcGait.h"   // the travel speeds and the scripted-move bounds

// ============================================================================================
// FElysiumScriptedCharacter — the one movement-only owner shared by ordinary NPCs and the
// scene-owned player duplicate. Unreal owns path following through IElysiumNpcMotor; this class
// owns the authored scripted_sequence gait, arrival/facing order and failure bounds.
// ============================================================================================

class FElysiumScriptedCharacter : public FElysiumCombatCharacter
{
public:
	virtual ~FElysiumScriptedCharacter() override;

	virtual bool BeginScriptMove(const FVector& Mark, const FVector& MarkAngles,
		EElysiumScriptGait Gait, const FString& CustomClip) override;

	virtual EElysiumScriptMove AdvanceScriptMove() override;

	virtual void EndScriptMove() override;

	virtual void SetBodyFrozen(bool bFrozen) override;

	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;

	virtual void OnRuntimeTransformChanged() override;

protected:
	enum class EScriptPhase : uint8 { None, Travel, Facing };

	void BuildMotor();

	void DestroyMotor();

	virtual bool ClaimScriptMove() { return true; }
	virtual void ReleaseScriptMove(const TCHAR*) {}

	bool StartScriptWalkingAnimation(bool bRunning);

	EElysiumScriptMove BeginScriptFacing(double Now);

	IElysiumNpcMotor* Motor = nullptr;
	EScriptPhase ScriptPhase = EScriptPhase::None;
	FVector ScriptMark = FVector::ZeroVector;
	FVector ScriptMarkAngles = FVector::ZeroVector;
	float ScriptBestDistance = 0.0f;
	double ScriptProgressAt = 0.0;
	double ScriptDeadline = 0.0;
	double ScriptWatchdogAt = 0.0;
	bool bScriptMoveClaimed = false;
};
