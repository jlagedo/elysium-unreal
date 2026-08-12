// 9.4b — the character sheet's compiled half: the slot tables, the `CVStatList_t` accessors, and
// the seed that fills them from `vdata/system/stats.txt`.
//
// `Public/ElysiumSheetSlots.h` is the design note. In short: the slot layout and the datamap names
// are code because they are code in `vampire.dll`; the values are data because they are data in
// `stats.txt`. `Elysium.Content.Sheet` asserts the two still agree, slot for slot, against the real
// file — that test is what keeps the table below honest.

#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"

// ================================================================================================
// The slot tables — `stats.txt` file order, transcribed
// ================================================================================================

namespace
{
	// Both spellings are written out per row: the datamap name (lowercase, what a keyvalue and
	// `__getattr__` resolve) and the `stats.txt` `InternalName` in the file's own casing (what
	// `BumpStat` and a `feats.txt` trait reference resolve). Every comparison against either is
	// case-insensitive; the casing is kept so the row reads back against the file.
	#define SLOT(Index, Map, Name)            { Index, TEXT(Map), TEXT(Name), nullptr }
	#define SLOT_ALIAS(Index, Map, Alt, Name) { Index, TEXT(Map), TEXT(Name), TEXT(Alt) }

	// Attributes — 35 slots. Not "the nine attributes": the container runs `Attrib_Order`(0), the
	// nine World-of-Darkness attributes, then every derived and bookkeeping stat through
	// `Experience`(34).
	//
	// Two rows carry a `v` prefix rather than the plain lowercase, because the plain name would
	// collide with `CBaseEntity`'s own `health`/`max_health` keyfields — and our registry resolves
	// derived-shadows-base, so a sheet field named `health` would silently repoint `trigger_hurt`.
	// RE24 sampled `base_vmax_health` off the datamap directly; `vhealth` is **inferred** from that
	// pattern and has not been read out of the image (`docs/vtmb/game_runtime.md` section 3).
	const FElysiumSheetSlot GAttributeSlots[] =
	{
		SLOT( 0, "attrib_order",             "Attrib_Order"),
		SLOT( 1, "strength",                 "Strength"),
		SLOT( 2, "dexterity",                "Dexterity"),
		SLOT( 3, "stamina",                  "Stamina"),
		SLOT( 4, "charisma",                 "Charisma"),
		SLOT( 5, "manipulation",             "Manipulation"),
		SLOT( 6, "appearance",               "Appearance"),
		SLOT( 7, "perception",               "Perception"),
		SLOT( 8, "intelligence",             "Intelligence"),
		SLOT( 9, "wits",                     "Wits"),
		SLOT(10, "clan",                     "Clan"),
		// The trailing underscore is VtMB's, not a typo: RE24 read `base_gender_` off the datamap.
		// `gender` is accepted as an alias because that is how the save documents it.
		SLOT_ALIAS(11, "gender_", "gender",  "Gender"),
		SLOT(12, "bloodpool",                "BloodPool"),
		SLOT(13, "bloodpool_max",            "BloodPool_Max"),
		SLOT(14, "faithpoints",              "FaithPoints"),
		SLOT(15, "vhealth",                  "Health"),
		SLOT(16, "health_aggravated_dmg",    "Health_Aggravated_Dmg"),
		SLOT(17, "vmax_health",              "Max_Health"),
		SLOT(18, "generation",               "Generation"),
		SLOT(19, "armor_rating",             "Armor_Rating"),
		SLOT(20, "level",                    "Level"),
		SLOT(21, "frenzy_check_mod",         "Frenzy_Check_Mod"),
		SLOT(22, "soak_pool",                "Soak_Pool"),
		SLOT(23, "automatic_soak_successes", "Automatic_Soak_Successes"),
		SLOT(24, "automatic_str_successes",  "Automatic_Str_Successes"),
		SLOT(25, "health_buffer",            "HealthBuffer"),
		SLOT(26, "encumbrance",              "Encumbrance"),
		SLOT(27, "humanity",                 "Humanity"),
		SLOT(28, "masquerade",               "Masquerade"),
		SLOT(29, "experience_modifier",      "Experience_Modifier"),
		SLOT(30, "starting_equipment",       "Starting_Equipment"),
		SLOT(31, "excluded_equipment",       "Excluded_Equipment"),
		SLOT(32, "vampheal_type",            "VampHeal_Type"),
		SLOT(33, "autolevel_template",       "AutoLevel_Template"),
		SLOT(34, "experience",               "Experience"),
	};

	// Abilities — 13 slots, `Ability_Order` at 0. Talents/Skills/Knowledges is an ordering lookup,
	// not a nesting, so this is one flat list. Two datamap names diverge (RE24).
	const FElysiumSheetSlot GAbilitySlots[] =
	{
		SLOT( 0, "ability_order",  "Ability_Order"),
		SLOT( 1, "brawl",          "Brawl"),
		SLOT( 2, "dodge",          "Dodge"),
		SLOT( 3, "intimidate",     "Intimidation"),
		SLOT( 4, "subterfuge",     "Subterfuge"),
		SLOT( 5, "firearms",       "Firearms"),
		SLOT( 6, "melee",          "Melee"),
		SLOT( 7, "security",       "Security"),
		SLOT( 8, "stealth",        "Stealth"),
		SLOT( 9, "computers",      "Computer"),
		SLOT(10, "finance",        "Finance"),
		SLOT(11, "investigation",  "Investigation"),
		SLOT(12, "academics",      "Academics"),
	};

	// Disciplines — 13 slots, no order block, `Animalism` at 0. `stats.txt` authors 17: the four
	// Numina powers follow, and the compiled array does not reach them.
	const FElysiumSheetSlot GDisciplineSlots[] =
	{
		SLOT( 0, "animalism",         "Animalism"),
		SLOT( 1, "auspex",            "Auspex"),
		SLOT( 2, "blood_healing",     "Blood_Healing"),
		SLOT( 3, "celerity",          "Celerity"),
		SLOT( 4, "corpus_vampirus",   "Corpus_Vampirus"),
		SLOT( 5, "dementation",       "Dementation"),
		SLOT( 6, "dominate",          "Dominate"),
		SLOT( 7, "fortitude",         "Fortitude"),
		SLOT( 8, "obfuscate",         "Obfuscate"),
		SLOT( 9, "potence",           "Potence"),
		SLOT(10, "presence",          "Presence"),
		SLOT(11, "protean",           "Protean"),
		SLOT(12, "thaumaturgy",       "Thaumaturgy"),
	};

	// Active_Disciplines — the parallel container holding the currently-toggled level.
	const FElysiumSheetSlot GActiveDisciplineSlots[] =
	{
		SLOT( 0, "active_animalism",       "Active_Animalism"),
		SLOT( 1, "active_auspex",          "Active_Auspex"),
		SLOT( 2, "active_blood_healing",   "Active_Blood_Healing"),
		SLOT( 3, "active_celerity",        "Active_Celerity"),
		SLOT( 4, "active_corpus_vampirus", "Active_Corpus_Vampirus"),
		SLOT( 5, "active_dementation",     "Active_Dementation"),
		SLOT( 6, "active_dominate",        "Active_Dominate"),
		SLOT( 7, "active_fortitude",       "Active_Fortitude"),
		SLOT( 8, "active_obfuscate",       "Active_Obfuscate"),
		SLOT( 9, "active_potence",         "Active_Potence"),
		SLOT(10, "active_presence",        "Active_Presence"),
		SLOT(11, "active_protean",         "Active_Protean"),
		SLOT(12, "active_thaumaturgy",     "Active_Thaumaturgy"),
	};

	#undef SLOT
	#undef SLOT_ALIAS

	bool IsValidContainer(EElysiumTraitContainer Container)
	{
		return (uint8)Container < (uint8)EElysiumTraitContainer::Count;
	}
}

const TCHAR* ElysiumTraitContainerName(EElysiumTraitContainer Container)
{
	switch (Container)
	{
	case EElysiumTraitContainer::Attributes:        return TEXT("Attributes");
	case EElysiumTraitContainer::Abilities:         return TEXT("Abilities");
	case EElysiumTraitContainer::Disciplines:       return TEXT("Disciplines");
	case EElysiumTraitContainer::ActiveDisciplines: return TEXT("Active_Disciplines");
	default:                                        return TEXT("?");
	}
}

TArrayView<const FElysiumSheetSlot> ElysiumSheetSlots(EElysiumTraitContainer Container)
{
	switch (Container)
	{
	case EElysiumTraitContainer::Attributes:        return GAttributeSlots;
	case EElysiumTraitContainer::Abilities:         return GAbilitySlots;
	case EElysiumTraitContainer::Disciplines:       return GDisciplineSlots;
	case EElysiumTraitContainer::ActiveDisciplines: return GActiveDisciplineSlots;
	default:                                        return TArrayView<const FElysiumSheetSlot>();
	}
}

bool ElysiumFindSheetSlot(const TCHAR* InternalName, EElysiumTraitContainer& OutContainer, int32& OutSlot)
{
	if (!InternalName || !*InternalName)
	{
		return false;
	}
	// Declaration order — `CVStatRef`'s own search across the four containers.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			if (FCString::Stricmp(Slot.Internal, InternalName) == 0)
			{
				OutContainer = Container;
				OutSlot = Slot.Index;
				return true;
			}
		}
	}
	return false;
}

// ================================================================================================
// FElysiumSheet
// ================================================================================================

FElysiumSheet::FElysiumSheet()
{
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const int32 Count = ElysiumSheetSlotCount((EElysiumTraitContainer)i);
		Base[i].SetNumZeroed(Count);
		Current[i].SetNumZeroed(Count);
	}
}

int32 FElysiumSheet::GetBase(EElysiumTraitContainer Container, int32 Slot) const
{
	return IsValidContainer(Container) && Base[(uint8)Container].IsValidIndex(Slot)
		? Base[(uint8)Container][Slot] : 0;
}

int32 FElysiumSheet::GetCurrent(EElysiumTraitContainer Container, int32 Slot) const
{
	return IsValidContainer(Container) && Current[(uint8)Container].IsValidIndex(Slot)
		? Current[(uint8)Container][Slot] : 0;
}

void FElysiumSheet::SetBase(EElysiumTraitContainer Container, int32 Slot, int32 Value)
{
	if (!IsValidContainer(Container) || !Base[(uint8)Container].IsValidIndex(Slot))
	{
		return;
	}
	Base[(uint8)Container][Slot] = Value;
	// The clamp needs the rulebook, which this level does not hold; the caller that has it calls
	// RecomputeCurrent. Keeping current in step here means a sheet is never internally inconsistent
	// even when nothing has loaded `stats.txt`.
	Current[(uint8)Container][Slot] = Value;
}

void FElysiumSheet::AddBase(EElysiumTraitContainer Container, int32 Slot, int32 Delta,
	const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects)
{
	// `CVStatList_t::AddBase` writes the base RAW — it never consults the max, and it never clamps.
	// Its only gate is the stat's `IncPredependency`, which **a negative Delta bypasses**. So a
	// counter can bank base above its ceiling (the current value is what the clamp answers), and a
	// loss can push it below the floor.
	if (Delta > 0 && Stats)
	{
		if (const FElysiumStat* Stat = Stats->Container(Container).At(Slot))
		{
			for (const FString& Expr : Stat->IncPredependency)
			{
				if (!ElysiumSheetRules::EvalPredependency(Expr, *this))
				{
					return;
				}
			}
		}
	}
	SetBase(Container, Slot, GetBase(Container, Slot) + Delta);
	RecomputeCurrent(Stats, Effects);
}

bool FElysiumSheet::IncBase(EElysiumTraitContainer Container, int32 Slot,
	const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects)
{
	if (!IsValidContainer(Container) || !Base[(uint8)Container].IsValidIndex(Slot))
	{
		return false;
	}
	const int32 Value = GetBase(Container, Slot);

	if (Stats)
	{
		int32 Min = 0, Max = 0;
		BoundsFor(Container, Slot, Stats, Effects, Min, Max);
		if (Max >= Min && Value >= Max)
		{
			return false;
		}
		// The stat's own gate on being raised — `"BloodPool > 0"`, `"Health < Max_Health"`. All of
		// them must hold; 17 of the Active_Disciplines author more than one.
		if (const FElysiumStat* Stat = Stats->Container(Container).At(Slot))
		{
			for (const FString& Expr : Stat->IncPredependency)
			{
				if (!ElysiumSheetRules::EvalPredependency(Expr, *this))
				{
					return false;
				}
			}
		}
	}

	SetBase(Container, Slot, Value + 1);
	RecomputeCurrent(Stats, Effects);
	return true;
}

void FElysiumSheet::BoundsFor(EElysiumTraitContainer Container, int32 Slot,
	const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects,
	int32& OutMin, int32& OutMax) const
{
	OutMin = 0;
	OutMax = -1;   // Max < Min — "unbounded", which is what no rulebook means
	const FElysiumStat* Stat = Stats ? Stats->Container(Container).At(Slot) : nullptr;
	if (!Stat)
	{
		return;
	}
	OutMin = Stat->Min;
	OutMax = Stat->Max;

	// A bound may NAME another stat (`"Max" "Max_Health"`) rather than hold a number. It resolves
	// against the BASE array, so the answer does not depend on which slot was recomputed first.
	EElysiumTraitContainer RefContainer;
	int32 RefSlot = INDEX_NONE;
	if (!Stat->MinExpr.IsNumeric() && ElysiumFindSheetSlot(*Stat->MinExpr, RefContainer, RefSlot))
	{
		OutMin = Base[(uint8)RefContainer][RefSlot];
	}
	if (!Stat->MaxExpr.IsNumeric() && ElysiumFindSheetSlot(*Stat->MaxExpr, RefContainer, RefSlot))
	{
		OutMax = Base[(uint8)RefContainer][RefSlot];
	}
	// The bound itself goes through the character's effect walk, exactly as the engine's two bound
	// accessors do: a `+1` on the stat raises its ceiling with it, a `Max 4` lowers it to 4.
	if (Effects)
	{
		OutMin = Effects->ApplyToBound(Container, Slot, OutMin);
		OutMax = Effects->ApplyToBound(Container, Slot, OutMax);
	}
}

void FElysiumSheet::RecomputeCurrent(const FElysiumStatTable* Stats, const FElysiumSheetEffects* Effects)
{
	// VtMB's `GetCurrent` is base -> trait effects -> clamp to the effective max -> clamp up to the
	// min, and the three steps are three passes here for one reason: a bound may name another stat,
	// and that stat can sit at a higher slot than the one being clamped.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		Current[i] = Base[i];
	}

	// Pass 2 — the modifier stack every group in force puts on the slot.
	if (Effects)
	{
		for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
		{
			const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
			for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
			{
				Current[i][Slot.Index] = Effects->ApplyToTrait(Container, Slot.Index, Current[i][Slot.Index]);
			}
		}
	}
	if (!Stats)
	{
		return;
	}

	// Pass 3 — the effective bounds.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			int32 Min = 0, Max = 0;
			BoundsFor(Container, Slot.Index, Stats, Effects, Min, Max);
			if (Max >= Min)
			{
				Current[i][Slot.Index] = FMath::Clamp(Current[i][Slot.Index], Min, Max);
			}
		}
	}
}

void FElysiumSheet::SeedFrom(const FElysiumStatTable& Stats)
{
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
		const FElysiumStatContainer& Table = Stats.Container(Container);

		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			// Address the file by *index*, not by name: file position is the engine's trait id, and
			// the content test asserts the two agree. A short container (an unparseable file) leaves
			// the slot at zero rather than mis-seeding it from a neighbour.
			const FElysiumStat* Stat = Table.At(Slot.Index);
			Base[i][Slot.Index] = Stat ? Stat->Default : 0;
		}
	}
	RecomputeCurrent(&Stats);
}

void FElysiumSheet::ApplyTemplate(const FElysiumClanTemplate& Template, const FElysiumStatTable* Stats,
	const FElysiumSheetEffects* Effects)
{
	// A template authors only the traits it sets, and `Resolve` has already folded the parent chain,
	// so an absent key means inherit — write nothing for it.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			if (const int32* Value = Template.Trait(Slot.Internal))
			{
				Base[i][Slot.Index] = *Value;
			}
		}
	}
	RecomputeCurrent(Stats, Effects);
}
