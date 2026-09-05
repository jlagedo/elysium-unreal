#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcGait.h"   // the travel speeds and the scripted-move bounds

// The one movement-only owner shared by ordinary NPCs and the scene-owned player duplicate. Unreal
// owns path following through IElysiumNpcMotor; this class owns the authored scripted_sequence
// gait, arrival/facing order and failure bounds.

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

	// The motor this character stands beside its skeletal body: ordinary NPCs take the shared
	// build verbatim; the player duplicate layers its non-solid variant on top.
	virtual void BuildOwnMotor() { BuildMotor(); }
	// Async completion restores only the disposable movement body, not script movement/state.
	virtual void OnPreparedVisualAttached() override { BuildOwnMotor(); }

	// A runtime model swap under the leaf's bodies gate. The swap destroys the motor a beat may
	// be steering, so the scripted hold is released first — the beat reads Unsupported next tick
	// and finishes on the placement fallback — then the animating node rebuilds the body and the
	// motor is stood back up. Gated off, the swap leaves a bodiless record and nothing to rebuild.
	void RebuildForModelChange(bool bBodiesEnabled);

	virtual bool ClaimScriptMove() { return true; }
	virtual void ReleaseScriptMove(const TCHAR*) {}

	bool StartScriptWalkingAnimation(bool bRunning);

	EElysiumScriptMove BeginScriptFacing(double Now);

	// One motor sample written back into the entity. The motor is the physical authority while it
	// holds a request: its feet/yaw land straight on Origin/Angles rather than through
	// SetRuntimeOrigin, which would teleport the body back. Callers guarantee a motor.
	EElysiumNpcMoveStatus SampleMotorIntoEntity();

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
