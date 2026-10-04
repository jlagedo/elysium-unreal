#include "Substrate/ElysiumScriptedCharacter.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"

// V3c (M11): the scripted-move seam (`BeginScriptMove` / `AdvanceScriptMove` / `EndScriptMove`, its
// phase, mark, watchdog and travel cycle) is deleted. Retail's scene travel is the NPC's own program:
// `SCHED_AISCRIPT 0x2e` translated by `m_fMoveTo` (`TranslateSchedule 0x102cc080`) and tasks
// `TASK_WALK/RUN/SCRIPT_CUSTOM_MOVE_TO_TARGET 8/9/10`'s navigator goal on the cine
// (`ElysiumNpcBaseStartTask.cpp`). What stays here is the motor the kernel's navigator drives.

FElysiumScriptedCharacter::~FElysiumScriptedCharacter()
{
	DestroyMotor();
}

void FElysiumScriptedCharacter::SyncMovingRecord()
{
	if (Motor == nullptr || IsInert())
	{
		return;
	}
	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	Motor->SampleTransform(Feet, Yaw);
	// The yaw is compared tight: `CAI_Motor::DeltaIdealYaw`'s `FacingIdeal` tolerance is 0.006
	// degrees (`0x10278c80`), so a record left 0.01 behind a settled body would never face.
	if (Feet.Equals(Origin, 0.01) && FMath::IsNearlyEqual(-Yaw, Angles.Y, 1.0e-4f))
	{
		return;   // a standing body costs nothing
	}
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

EElysiumNpcMoveStatus FElysiumScriptedCharacter::SampleMotorIntoEntity()
{
	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
	return Status;
}

void FElysiumScriptedCharacter::SetBodyFrozen(bool bFrozen)
{
	if (Motor)
	{
		Motor->SetFrozen(bFrozen);
	}
}

void FElysiumScriptedCharacter::SetIgnoreCharacterCollision(bool bIgnore)
{
	if (Motor)
	{
		Motor->SetIgnoreCharacterCollision(bIgnore);
	}
}

void FElysiumScriptedCharacter::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Motor)
	{
		Motor->Teleport(Origin, -Angles.Y);
	}
	else
	{
		FElysiumAnimating::OnRuntimeTransformChanged();
	}
}

void FElysiumScriptedCharacter::BuildMotor()
{
	if (Motor || !Visual)
	{
		return;
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Motor = Embodiment->BuildNpcMotor(Visual, Handle, Origin, -Angles.Y, ModelStem(),
			FMath::Max(0, Handle.Index));
		if (Motor)
		{
			Motor->SetEnabled(!IsInert());
		}
	}
}

void FElysiumScriptedCharacter::RebuildForModelChange(bool bBodiesEnabled)
{
	if (!bBodiesEnabled)
	{
		return;
	}
	DestroyMotor();
	FElysiumAnimating::OnRuntimeModelChanged();
	BuildOwnMotor();
}

void FElysiumScriptedCharacter::DestroyMotor()
{
	if (Motor && World && World->Embodiment())
	{
		World->Embodiment()->DestroyNpcMotor(Motor);
	}
	Motor = nullptr;
}
