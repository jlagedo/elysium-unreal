#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcGait.h"   // the travel speeds

// The movement body shared by ordinary NPCs and the scene-owned player duplicate: the motor Unreal
// path following stands behind (IElysiumNpcMotor), which the kernel's navigator drives. A scene's
// travel is the NPC's own program (`SCHED_AISCRIPT 0x2e`, tasks 8/9/10's navigator goal); V3c
// deleted the scripted-move seam that used to live here (M11).

class FElysiumScriptedCharacter : public FElysiumCombatCharacter
{
public:
	virtual ~FElysiumScriptedCharacter() override;

	virtual void SetBodyFrozen(bool bFrozen) override;

	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;

	virtual void OnRuntimeTransformChanged() override;

protected:
	void BuildMotor();

	void DestroyMotor();

	// The motor this character stands beside its skeletal body: ordinary NPCs take the shared
	// build verbatim; the player duplicate layers its non-solid variant on top.
	virtual void BuildOwnMotor() { BuildMotor(); }
	// Async completion restores only the disposable movement body, not script movement/state.
	virtual void OnPreparedVisualAttached() override { BuildOwnMotor(); }

	// A runtime model swap under the leaf's bodies gate: the animating node rebuilds the body and
	// the motor is stood back up. Gated off, the swap leaves a bodiless record and nothing to
	// rebuild.
	void RebuildForModelChange(bool bBodiesEnabled);

	// The kernel NPC's move-ignore set (`FElysiumNpcBase::ClearMoveIgnores`), dropped at every
	// `Motor->Stop()` the kernel issues. Nothing below the NPC.
	virtual void ClearMoveIgnores() {}

	// One motor sample written back into the entity. The motor is the physical authority while it
	// holds a request: its feet/yaw land straight on Origin/Angles rather than through
	// SetRuntimeOrigin, which would teleport the body back. Callers guarantee a motor.
	EElysiumNpcMoveStatus SampleMotorIntoEntity();

public:
	// **Named divergence.** Retail's entity origin IS the body: `PerformMovement(interval)`
	// integrates the whole elapsed interval inside `NPCThink`, so a far NPC hops but its record is
	// never wrong. This runtime's body is a movement component integrated on the actor tick while
	// `Origin` is written only from a think -- and under the NPC think cadence a body out of the
	// player's PVS thinks as rarely as every six seconds, which would leave every distance test in
	// the game (other NPCs' senses, sound propagation, triggers, the witness lanes) reading a
	// position the guard left long ago. So the record is synced on the FRAME instead.
	//
	// It reads `SampleTransform`, not `Sample`: the latter consumes the terminal `Reached` status
	// by calling `Stop()`, and stealing that from the executor waiting on it would strand the move.
	void SyncMovingRecord();

protected:

	IElysiumNpcMotor* Motor = nullptr;
};
