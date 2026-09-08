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

	EElysiumWeaponClass ClassFor(EElysiumViewWeaponFamily Family)
	{
		switch (Family)
		{
		case EElysiumViewWeaponFamily::Unarmed: return EElysiumWeaponClass::Unarmed;
		case EElysiumViewWeaponFamily::Melee:   return EElysiumWeaponClass::Melee;
		case EElysiumViewWeaponFamily::Firearm: return EElysiumWeaponClass::Ranged;
		case EElysiumViewWeaponFamily::Thrown:  return EElysiumWeaponClass::Thrown;
		default:                                return EElysiumWeaponClass::None;
		}
	}

	// `6 / 24` for a weapon carrying a magazine, and nothing at all for one that does not — a tire
	// iron's row is blank rather than reading zero.
	FText AmmoDetail(const FElysiumInventoryEntryView& Weapon)
	{
		return Weapon.bHasMagazine
			? FText::FromString(FString::Printf(TEXT("%d / %d"), Weapon.AmmoCurrent, Weapon.AmmoReserve))
			: FText::GetEmpty();
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
	FeedVictimPercent = View.Feed.Percent;
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
	// Only a LIVE session raises the hint: an idle terminal's view carries serial 0 and no session
	// half at all, and the published line is already empty on every hidden arm (types 0 and 2).
	TerminalHint = View.Terminal.IsOpen() && !View.Terminal.HudHintText.IsEmpty()
		? FText::FromString(View.Terminal.HudHintText) : FText::GetEmpty();

	// Disciplines have no production owner. Invalid is the contract, not a zero-valued fake
	// item. The preview branch below is compiled in every config so this value type remains
	// deterministic in tests, but only the non-Shipping subsystem exposes a way to select it.
	Equipment = FElysiumHUDEquipmentView();
	Worn = FElysiumHUDEquipmentView();
	Discipline = FElysiumHUDDisciplineView();
	Selector = FElysiumHUDSelectorView();

	// The stealth readout. Only the stance is owned; each half carries its own validity, so
	// the gauge renders as unmeasured rather than as a confident zero.
	Stealth = FElysiumHUDStealthView();
	Stealth.bSneaking = View.Stealth.bSneaking;
	Stealth.bConcealmentValid = View.Stealth.bConcealmentValid;
	Stealth.ConcealmentStep = FMath::RoundToInt(FMath::Clamp(View.Stealth.LightRow, 0, 10)
		* float(ElysiumHUDArt::ConcealmentSteps - 1) / 10.f);
	Stealth.bObserverValid = View.Stealth.bObserverValid;
	Stealth.ObserverDistanceMetres = View.Stealth.ObserverDistanceCm / 100.0f;
	switch (View.Stealth.Detection)
	{
	case EElysiumDetection::Searching: Stealth.Detection = EElysiumHUDDetection::Searching; break;
	case EElysiumDetection::Detected:  Stealth.Detection = EElysiumHUDDetection::Detected; break;
	default:                           Stealth.Detection = EElysiumHUDDetection::Unaware; break;
	}

	// The preview verb replaces the whole readout with its fixture, so production projection and the
	// preview never half-fill each other.
	if (Preview == EElysiumHUDPreview::Off)
	{
		ProjectEquipment(View.Equipment);
	}

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

		// The stealth cluster's preview stands in for concealment and observer owners that have not
		// committed. It carries a MEASURED gauge and an observer on purpose: the unmeasured state
		// is what production already shows, so the fixture is the only way to see the readout those
		// owners will draw.
		if (Preview == EElysiumHUDPreview::Sneak)
		{
			Stealth.bSneaking = true;
			Stealth.bConcealmentValid = true;
			Stealth.ConcealmentStep = 2;
			Stealth.bObserverValid = true;
			Stealth.ObserverDistanceMetres = 12.0f;
			Stealth.Detection = EElysiumHUDDetection::Searching;
		}
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

// The one production writer of the equipment readout, the worn slot and the inventory selector.
//
// All three come off the same projection, so the readout and the selector can never disagree about
// what is in hand. The selector's own visibility is the peek's: a screen state the publisher
// resolved, not a mode the HUD holds.
void UElysiumHUDModel::ProjectEquipment(const FElysiumEquipmentView& View)
{
	if (!View.bValid)
	{
		return;
	}

	if (View.bEquippedValid)
	{
		Equipment.bValid = true;
		Equipment.Name = FText::FromString(View.Equipped.Label);
		Equipment.WeaponClass = ClassFor(View.Equipped.Family);
		Equipment.Icon = ElysiumHUDArt::ItemIcon(View.Equipped.Classname, Equipment.WeaponClass);
		Equipment.AmmoCurrent = View.Equipped.AmmoCurrent;
		Equipment.AmmoReserve = View.Equipped.AmmoReserve;
	}

	if (View.bWornValid)
	{
		Worn.bValid = true;
		Worn.Name = FText::FromString(View.Worn.Label);
		Worn.WeaponClass = EElysiumWeaponClass::None;
		Worn.Icon = ElysiumHUDArt::ItemIcon(View.Worn.Classname, EElysiumWeaponClass::None);
	}

	// A peek that has faded out leaves the selector closed, which is what collapses its region. The
	// two readouts above stay up either way — they describe the body, not the switch.
	if (View.PeekAlpha <= 0.0f || View.Entries.Num() == 0)
	{
		return;
	}

	// One selector type: the rows are whatever category the cursor is in, and the heading names it.
	Selector.Type = View.Section == EElysiumInvSection::WeaponMelee
		|| View.Section == EElysiumInvSection::WeaponRanged
		|| View.Section == EElysiumInvSection::WeaponThrown
		? EElysiumHUDSelector::Weapons : EElysiumHUDSelector::Inventory;
	Selector.Heading = FText::FromString(ElysiumInvSectionName(View.Section));
	// Barebones cycling shows the three-item peek on both devices: the full list is the
	// hold-to-open selector's.
	Selector.bBriefMode = true;
	Selector.Alpha = View.PeekAlpha;
	Selector.SelectedIndex = View.SelectedIndex;
	Selector.Entries.Reserve(View.Entries.Num());
	for (const FElysiumInventoryEntryView& Item : View.Entries)
	{
		FElysiumHUDSelectorEntry& Row = Selector.Entries.AddDefaulted_GetRef();
		Row.Label = FText::FromString(Item.Label);
		Row.Detail = AmmoDetail(Item);
		Row.Quantity = Item.Quantity;
		Row.Icon = ElysiumHUDArt::ItemIcon(Item.Classname, ClassFor(Item.Family));
	}
}
