#include "Substrate/ElysiumConversationPlace.h"

#include "ElysiumClassRegistry.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumNpcKernelBindings.h"

void FElysiumConversationPlace::InputEnable(const FElysiumInputArgs&)
{
	bEnabled = true;
	// SEAM for `FUN_102dc3e0`, the enable half of the conversation machinery (0018 story 14).
	++NodesEnabledCalls;
}

void FElysiumConversationPlace::InputDisable(const FElysiumInputArgs&)
{
	bEnabled = false;
	// SEAM for `FUN_102dc490`, the disable half of the conversation machinery (0018 story 14).
	++NodesDisabledCalls;
	// `CBaseEntity::ThinkSet(this, NULL, 0.0, NULL)`.
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumConversationPlace::InputPlayOneOffSound(const FElysiumInputArgs&)
{
	bPlayOneOffSound = true;
}

void FElysiumConversationPlace::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Places"), InterestingPlaces.IsEmpty() ? TEXT("(none)") : InterestingPlaces);
	Out.Emplace(TEXT("One-off sound pending"), bPlayOneOffSound ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Behaviour"), TEXT("unbuilt (0018 story 14)"));
}

void FElysiumConversationPlace::BuildClass(FElysiumClassDesc& D)
{
	ElysiumNpcKernelBindings::AddConversationPlaceFields(D);

	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumConversationPlace&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumConversationPlace&>(E).InputDisable(Args); });
	D.Input(TEXT("PlayOneOffSound"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumConversationPlace&>(E).InputPlayOneOffSound(Args); });

	ElysiumAddClassField(D, TEXT("m_bPlayOneOffSound"), &FElysiumConversationPlace::bPlayOneOffSound,
		EElysiumField::Save);
}
