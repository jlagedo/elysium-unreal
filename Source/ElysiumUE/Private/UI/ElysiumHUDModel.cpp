#include "ElysiumHUDModel.h"

namespace
{
	FElysiumHUDSelectorEntry Entry(const TCHAR* Label, const TCHAR* Detail = TEXT(""))
	{
		FElysiumHUDSelectorEntry Out;
		Out.Label = FText::FromString(Label);
		Out.Detail = FText::FromString(Detail);
		return Out;
	}
}

void UElysiumHUDModel::Apply(const FElysiumViewState& View, EElysiumHUDPreview Preview)
{
	bVisible = View.bPlayerSurface && !View.bCinematic && !View.bSignHidesHUD;
	bVitalsValid = View.Vitals.bValid;
	Health = View.Vitals.Health;
	MaxHealth = View.Vitals.MaxHealth;
	BloodPool = View.Vitals.BloodPool;
	BloodCapacity = View.Vitals.MaxBloodPool;
	Humanity = View.Vitals.Humanity;
	Masquerade = View.Vitals.Masquerade;
	bFeedVictimVisible = View.Feed.bVisible;
	FeedVictimBlood = View.Feed.BloodPool;
	FeedVictimBloodCapacity = View.Feed.MaxBloodPool;
	UseIcon = View.Interaction.Icon;
	UsePromptAlpha = View.Interaction.bVisible ? View.Interaction.PromptAlpha : 0.0f;
	bUseActionable = View.Interaction.bActionable;
	bUseLocked = View.Interaction.bLocked;
	UseAction = View.Interaction.Action;

	switch (ElysiumView::ResolveReticle(View))
	{
	case ElysiumView::EReticle::Cross:   Reticle = EElysiumHUDReticle::Cross; break;
	case ElysiumView::EReticle::UseIcon: Reticle = EElysiumHUDReticle::UseIcon; break;
	default:                              Reticle = EElysiumHUDReticle::None; break;
	}
	Fade = View.Fade;

	// Inventory and disciplines do not have production owners yet. Invalid is the contract, not a
	// zero-valued fake item. The preview branch below is compiled in every config so this value type
	// remains deterministic in tests, but only the non-Shipping subsystem exposes a way to select it.
	Equipment = FElysiumHUDEquipmentView();
	Discipline = FElysiumHUDDisciplineView();
	Selector = FElysiumHUDSelectorView();

	if (Preview != EElysiumHUDPreview::Off)
	{
		bVisible = true;
		bVitalsValid = true;
		MaxHealth = 100;
		Health = Preview == EElysiumHUDPreview::Critical ? 18 : 72;
		BloodCapacity = 15;
		BloodPool = Preview == EElysiumHUDPreview::Critical ? 2 : 9;
		Humanity = 6;
		Masquerade = 1;
		Reticle = EElysiumHUDReticle::Cross;

		if (Preview == EElysiumHUDPreview::Combat || Preview == EElysiumHUDPreview::Weapon)
		{
			Equipment.bValid = true;
			Equipment.Name = FText::FromString(TEXT(".38 Revolver"));
			Equipment.AmmoCurrent = 6;
			Equipment.AmmoReserve = 24;
		}
		if (Preview == EElysiumHUDPreview::Discipline)
		{
			Discipline.bValid = true;
			Discipline.Name = FText::FromString(TEXT("Bloodheal"));
			Discipline.BloodCost = 1;
			Selector.Type = EElysiumHUDSelector::Disciplines;
			Selector.Entries = {
				Entry(TEXT("Bloodheal"), TEXT("1 blood")),
				Entry(TEXT("Bloodbuff"), TEXT("1 blood")),
				Entry(TEXT("Celerity"), TEXT("1 blood")),
			};
			Selector.SelectedIndex = 0;
		}
		else if (Preview == EElysiumHUDPreview::Weapon)
		{
			Selector.Type = EElysiumHUDSelector::Weapons;
			Selector.Entries = {
				Entry(TEXT("Unarmed")),
				Entry(TEXT(".38 Revolver"), TEXT("6 / 24")),
				Entry(TEXT("Tire Iron")),
			};
			Selector.SelectedIndex = 1;
		}
		else if (Preview == EElysiumHUDPreview::Inventory)
		{
			Selector.Type = EElysiumHUDSelector::Inventory;
			Selector.Entries = {
				Entry(TEXT("Blood Pack"), TEXT("Restores 3 blood")),
				Entry(TEXT("Key Ring")),
				Entry(TEXT("Wallet")),
			};
			Selector.SelectedIndex = 0;
		}
	}

	++Revision;
	OnChanged.Broadcast();
}
