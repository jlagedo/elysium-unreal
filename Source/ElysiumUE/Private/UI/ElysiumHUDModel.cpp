#include "ElysiumHUDModel.h"

namespace
{
	FElysiumHUDSelectorEntry Entry(const TCHAR* Label, const TCHAR* Detail = TEXT(""),
		FName Icon = NAME_None, int32 Quantity = 0, bool bEnabled = true)
	{
		FElysiumHUDSelectorEntry Out;
		Out.Label = FText::FromString(Label);
		Out.Detail = FText::FromString(Detail);
		Out.Icon = Icon;
		Out.Quantity = Quantity;
		Out.bEnabled = bEnabled;
		return Out;
	}

	void FillRangedRevolver(FElysiumHUDEquipmentView& Equipment)
	{
		Equipment.bValid = true;
		Equipment.Name = FText::FromString(TEXT(".38 Revolver"));
		Equipment.WeaponClass = EElysiumWeaponClass::Ranged;
		Equipment.Icon = ElysiumHUDArt::Inventory(TEXT("weapons_ranged/thirtyeight"));
		Equipment.AmmoCurrent = 6;
		Equipment.AmmoReserve = 24;
	}

	void FillActiveBloodheal(FElysiumHUDDisciplineView& Discipline)
	{
		Discipline.bValid = true;
		Discipline.Name = FText::FromString(TEXT("Bloodheal"));
		Discipline.Icon = ElysiumHUDArt::Discipline(TEXT("bloodheal"));
		Discipline.BloodCost = 1;
	}
}

void UElysiumHUDModel::Apply(const FElysiumViewState& View, EElysiumHUDPreview Preview)
{
	// The heads-up layer is up unless a named shot asked for it down or a sign panel covers the game.
	// Owning the view is not by itself a reason to hide it.
	bVisible = View.bPlayerSurface && View.Camera.bShowHud && !View.bSignHidesHUD;
	bVitalsValid = View.Vitals.bValid;
	Health = View.Vitals.Health;
	MaxHealth = View.Vitals.MaxHealth;
	BloodPool = View.Vitals.BloodPool;
	BloodCapacity = View.Vitals.MaxBloodPool;
	Humanity = View.Vitals.Humanity;
	Masquerade = View.Vitals.Masquerade;
	ZoneState = EElysiumZoneState::None;
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
	case ElysiumView::EReticle::Cross:       Reticle = EElysiumHUDReticle::Cross; break;
	case ElysiumView::EReticle::UseIcon:     Reticle = EElysiumHUDReticle::UseIcon; break;
	case ElysiumView::EReticle::ThirdPerson: Reticle = EElysiumHUDReticle::ThirdPerson; break;
	default:                                 Reticle = EElysiumHUDReticle::None; break;
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

		// Zone-state stub: Combat for weapon-bearing previews, Elysium for its dedicated preview,
		// Masquerade otherwise. Stays None in Passive and Off to exercise the collapsed state.
		if (Preview == EElysiumHUDPreview::Combat || Preview == EElysiumHUDPreview::Weapon
			|| Preview == EElysiumHUDPreview::Radial || Preview == EElysiumHUDPreview::Brief)
		{
			ZoneState = EElysiumZoneState::Combat;
		}
		else if (Preview == EElysiumHUDPreview::Elysium)
		{
			ZoneState = EElysiumZoneState::Elysium;
		}
		else if (Preview != EElysiumHUDPreview::Passive)
		{
			ZoneState = EElysiumZoneState::Masquerade;
		}

		if (Preview == EElysiumHUDPreview::Combat || Preview == EElysiumHUDPreview::Weapon
			|| Preview == EElysiumHUDPreview::Radial || Preview == EElysiumHUDPreview::Brief)
		{
			FillRangedRevolver(Equipment);
		}
		if (Preview == EElysiumHUDPreview::Combat || Preview == EElysiumHUDPreview::Discipline
			|| Preview == EElysiumHUDPreview::Radial)
		{
			FillActiveBloodheal(Discipline);
		}
		if (Preview == EElysiumHUDPreview::Discipline)
		{
			Selector.Type = EElysiumHUDSelector::Disciplines;
			Selector.Entries = {
				Entry(TEXT("Bloodheal"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("bloodheal"))),
				Entry(TEXT("Fortitude"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("fortitude"))),
				Entry(TEXT("Celerity"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("celerity"))),
				Entry(TEXT("Potence"), TEXT("2 blood"), ElysiumHUDArt::Discipline(TEXT("potence"))),
				Entry(TEXT("Presence"), TEXT("2 blood"), ElysiumHUDArt::Discipline(TEXT("presence")), 0, false),
			};
			Selector.SelectedIndex = 0;
		}
		else if (Preview == EElysiumHUDPreview::Weapon || Preview == EElysiumHUDPreview::Brief)
		{
			Selector.Type = EElysiumHUDSelector::Weapons;
			Selector.bBriefMode = Preview == EElysiumHUDPreview::Brief;
			Selector.Entries = {
				Entry(TEXT("Unarmed"), TEXT(""), ElysiumHUDArt::Inventory(TEXT("weapons_melee/fists"))),
				Entry(TEXT(".38 Revolver"), TEXT("6 / 24"),
					ElysiumHUDArt::Inventory(TEXT("weapons_ranged/thirtyeight"))),
				Entry(TEXT("Tire Iron"), TEXT(""),
					ElysiumHUDArt::Inventory(TEXT("weapons_melee/tire_iron"))),
				Entry(TEXT("Frag Grenade"), TEXT("2 / 2"),
					ElysiumHUDArt::Inventory(TEXT("weapons_ranged/grenade_frag"))),
			};
			Selector.SelectedIndex = 1;
		}
		else if (Preview == EElysiumHUDPreview::Inventory)
		{
			Equipment.bValid = true;
			Equipment.Name = FText::FromString(TEXT("Tire Iron"));
			Equipment.WeaponClass = EElysiumWeaponClass::Melee;
			Equipment.Icon = ElysiumHUDArt::Inventory(TEXT("weapons_melee/tire_iron"));
			Selector.Type = EElysiumHUDSelector::Inventory;
			Selector.Entries = {
				Entry(TEXT("Blood Pack"), TEXT("Restores 3 blood"),
					ElysiumHUDArt::Inventory(TEXT("general_items/bloodpack")), 4),
				Entry(TEXT("Lockpicks"), TEXT(""),
					ElysiumHUDArt::Inventory(TEXT("general_items/lockpicks")), 2),
				Entry(TEXT("Key Ring"), TEXT(""), ElysiumHUDArt::Inventory(TEXT("key"))),
			};
			Selector.SelectedIndex = 0;
		}
		else if (Preview == EElysiumHUDPreview::Radial)
		{
			Selector.Type = EElysiumHUDSelector::Radial;
			Selector.Entries = {
				Entry(TEXT("Bloodheal"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("bloodheal"))),
				Entry(TEXT("Fortitude"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("fortitude"))),
				Entry(TEXT("Celerity"), TEXT("1 blood"), ElysiumHUDArt::Discipline(TEXT("celerity"))),
				Entry(TEXT("Potence"), TEXT("2 blood"), ElysiumHUDArt::Discipline(TEXT("potence"))),
				Entry(TEXT("Presence"), TEXT("2 blood"), ElysiumHUDArt::Discipline(TEXT("presence")), 0, false),
			};
			Selector.SelectedIndex = 2;
		}
	}

	++Revision;
	OnChanged.Broadcast();
}
