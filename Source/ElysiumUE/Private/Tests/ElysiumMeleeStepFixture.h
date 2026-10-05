#pragma once

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumWeaponClasses.h"

// 0x10343020 contact fixtures supply their own interval; slot315 scheduling has separate arms.
inline void ElysiumTestMeleeStep(FElysiumEntityWorld& StepWorld, const FElysiumClipPhase& StepPhase)
{
	for (const TUniquePtr<FElysiumEntity>& StepEntity : StepWorld.Entities())
	{
		FElysiumCombatCharacter* const StepCharacter = StepEntity->AsCombatCharacter();
		FElysiumItem* const StepItem = StepCharacter ? StepCharacter->Inventory.Active(*StepCharacter) : nullptr;
		FElysiumWeapon* const StepWeapon = StepItem ? StepItem->AsWeapon() : nullptr;
		if (!StepWeapon || !StepWeapon->Swing.bActive || !StepWeapon->Swing.bMelee) continue;
		StepCharacter->bMeleeSwingIsLive = true;
		StepWeapon->PrepareSwingContact();
		StepWeapon->MeleeSwingStep(StepCharacter->Origin, StepCharacter->Angles,
			FMath::Max(StepWeapon->Swing.PrevCycle, 0.f), StepPhase.Cycle);
	}
}
