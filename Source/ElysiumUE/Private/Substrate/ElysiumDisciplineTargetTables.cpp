#include "Substrate/ElysiumDisciplineTargetTables.h"
#include "Substrate/ElysiumMiscFlags.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::Index;
	using ElysiumVdata::Trim;
}

// --- disciplinetgt_000..004.txt ---

const TCHAR* ElysiumDiscFilterName(EElysiumDiscFilter Filter)
{
	switch (Filter)
	{
	case EElysiumDiscFilter::Critter:          return TEXT("Critter");
	case EElysiumDiscFilter::Human:            return TEXT("Human");
	case EElysiumDiscFilter::Supernatural:     return TEXT("Supernatural");
	case EElysiumDiscFilter::NoSupernatural:   return TEXT("No_Supernatural");
	case EElysiumDiscFilter::Boss:             return TEXT("Boss");
	case EElysiumDiscFilter::NoBoss:           return TEXT("No_Boss");
	case EElysiumDiscFilter::Self:             return TEXT("Self");
	case EElysiumDiscFilter::NoSelf:           return TEXT("No_Self");
	case EElysiumDiscFilter::NoFriends:        return TEXT("No_Friends");
	case EElysiumDiscFilter::Invulnerable:     return TEXT("Invulnerable");
	case EElysiumDiscFilter::NoInvulnerable:   return TEXT("No_Invulnerable");
	case EElysiumDiscFilter::Player:           return TEXT("Player");
	case EElysiumDiscFilter::PrimaryTarget:    return TEXT("Primary_Target");
	case EElysiumDiscFilter::Combatant:        return TEXT("Combatant");
	case EElysiumDiscFilter::NonCombatant:     return TEXT("NonCombatant");
	case EElysiumDiscFilter::CharTemplate:     return TEXT("CharTemplate");
	case EElysiumDiscFilter::DisciplineStrata: return TEXT("DisciplineStrata");
	case EElysiumDiscFilter::Chance:           return TEXT("Chance");
	default:                                   return TEXT("?");
	}
}

bool FElysiumDiscAmount::Parse(const FString& Raw)
{
	*this = FElysiumDiscAmount();
	FString S = Trim(Raw);
	if (S.IsEmpty())
	{
		return false;
	}
	if (S.EndsWith(TEXT("%")))
	{
		bPercent = true;
		S = Trim(S.LeftChop(1));
	}
	// `"6-8"` is a range; `"-1"` is a single negative number, so the hyphen only splits when it is
	// not the first character.
	const int32 Dash = S.Find(TEXT("-"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
	if (Dash != INDEX_NONE)
	{
		Min = FCString::Atoi(*Trim(S.Left(Dash)));
		Max = FCString::Atoi(*Trim(S.Mid(Dash + 1)));
	}
	else
	{
		Min = FCString::Atoi(*S);
		Max = Min;
	}
	if (Max < Min)
	{
		Swap(Min, Max);
	}
	bAuthored = true;
	return true;
}

namespace
{
	EElysiumDiscFilter ParseDiscFilter(const FString& FoldedKey)
	{
		static const TMap<FString, EElysiumDiscFilter> Table =
		{
			{ TEXT("critter"),          EElysiumDiscFilter::Critter },
			{ TEXT("human"),            EElysiumDiscFilter::Human },
			{ TEXT("supernatural"),     EElysiumDiscFilter::Supernatural },
			{ TEXT("no_supernatural"),  EElysiumDiscFilter::NoSupernatural },
			{ TEXT("boss"),             EElysiumDiscFilter::Boss },
			{ TEXT("no_boss"),          EElysiumDiscFilter::NoBoss },
			{ TEXT("self"),             EElysiumDiscFilter::Self },
			{ TEXT("no_self"),          EElysiumDiscFilter::NoSelf },
			{ TEXT("no_friends"),       EElysiumDiscFilter::NoFriends },
			{ TEXT("invulnerable"),     EElysiumDiscFilter::Invulnerable },
			{ TEXT("no_invulnerable"),  EElysiumDiscFilter::NoInvulnerable },
			{ TEXT("player"),           EElysiumDiscFilter::Player },
			{ TEXT("primary_target"),   EElysiumDiscFilter::PrimaryTarget },
			{ TEXT("combatant"),        EElysiumDiscFilter::Combatant },
			{ TEXT("noncombatant"),     EElysiumDiscFilter::NonCombatant },
			{ TEXT("chartemplate"),     EElysiumDiscFilter::CharTemplate },
			{ TEXT("disciplinestrata"), EElysiumDiscFilter::DisciplineStrata },
			{ TEXT("chance"),           EElysiumDiscFilter::Chance },
		};
		const EElysiumDiscFilter* Found = Table.Find(FoldedKey);
		return Found ? *Found : EElysiumDiscFilter::Unknown;
	}

	// A percentage token — `"50%"`, `"00%"`, `"0%"`. INDEX_NONE when nothing was authored.
	int32 ParseDiscPercent(const FKvNode& Node, const TCHAR* Key)
	{
		const FString* Raw = Node.Value(Key);
		if (Raw == nullptr)
		{
			return INDEX_NONE;
		}
		FString S = Trim(*Raw);
		if (S.EndsWith(TEXT("%")))
		{
			S = Trim(S.LeftChop(1));
		}
		return FCString::Atoi(*S);
	}

	void LoadDiscFilters(const FKvNode* Node, FElysiumDiscFilterSet& Out)
	{
		Out.Rows.Reset();
		if (Node == nullptr)
		{
			return;
		}
		// File order, repeats kept: a filter set is an AND of every row it authors.
		for (const TPair<FString, FString>& Pair : Node->Pairs)
		{
			FElysiumDiscFilterRow Row;
			Row.RawKey = Pair.Key;
			Row.Kind = ParseDiscFilter(Pair.Key);
			const FString Value = Trim(Pair.Value);
			switch (Row.Kind)
			{
			case EElysiumDiscFilter::CharTemplate:
				Row.Text = Value;
				break;
			case EElysiumDiscFilter::Chance:
			{
				FString S = Value;
				if (S.EndsWith(TEXT("%")))
				{
					S = Trim(S.LeftChop(1));
				}
				Row.Number = FCString::Atoi(*S);
				break;
			}
			case EElysiumDiscFilter::DisciplineStrata:
				Row.Number = FCString::Atoi(*Value);
				break;
			default:
				Row.Number = FCString::Atoi(*Value);
				Row.bEnabled = Row.Number != 0;
				break;
			}
			Out.Rows.Add(MoveTemp(Row));
		}
	}

	void LoadDiscMapping(const FKvNode& Node, FElysiumDiscMapping& Out)
	{
		Out.ChancePercent = ParseDiscPercent(Node, TEXT("Chance"));
		Out.Count = Node.Has(TEXT("Count")) ? Node.Int(TEXT("Count"), INDEX_NONE) : INDEX_NONE;
		Out.HitTable = Trim(Node.Str(TEXT("HitTable"), FString()));
	}

	void LoadDiscHit(const FKvNode& Node, const FString& Name, FElysiumDiscHit& Out);

	void LoadDiscHitBody(const FKvNode& Node, FElysiumDiscHit& Out)
	{
		Out.InheritFrom = Trim(Node.Str(TEXT("InheritFrom"), FString()));

		Out.DmgHealth.Parse(Node.Str(TEXT("Dmg_Health"), FString()));
		Out.HealBlood.Parse(Node.Str(TEXT("Heal_Blood"), FString()));
		Out.BloodSuck.Parse(Node.Str(TEXT("Blood_Suck"), FString()));
		Out.HealthBuffer.Parse(Node.Str(TEXT("Health_Buffer"), FString()));
		Out.HealthBufferBlockPercent = ParseDiscPercent(Node, TEXT("Health_Buffer_Block_Percent"));
		Out.Duration.Parse(Node.Str(TEXT("Duration"), FString()));
		const int32 Effective = ParseDiscPercent(Node, TEXT("Chance_Effective"));
		Out.ChanceEffectivePercent = (Effective == INDEX_NONE) ? 100 : Effective;

		Out.AiSchedule = Trim(Node.Str(TEXT("AI_Schedule"), FString()));
		Out.AiNpcFlag = Trim(Node.Str(TEXT("AI_NPCFlag"), FString()));
		Out.Expression = Trim(Node.Str(TEXT("Expression"), FString()));
		Out.GestureAnim = Trim(Node.Str(TEXT("Gesture_Anim"), FString()));
		Out.GestureDuration = Node.Flt(TEXT("Gesture_Duration"), 0.f);
		Out.PlayerAnim = Trim(Node.Str(TEXT("Player_Anim"), FString()));
		Out.MiscFlag = Trim(Node.Str(TEXT("MiscFlag"), FString()));
		Out.FlinchPercent = ParseDiscPercent(Node, TEXT("Flinch"));
		Out.KnockbackPercent = ParseDiscPercent(Node, TEXT("Knockback"));
		Out.AddToComfort = Node.Has(TEXT("AddToComfort"))
			? Node.Int(TEXT("AddToComfort"), INDEX_NONE) : INDEX_NONE;
		Out.bDoFrenzy = Node.Bool(TEXT("DoFrenzy"), false);
		Out.bDoPossession = Node.Bool(TEXT("DoPossession"), false);
		Out.bGibOnDeath = Node.Bool(TEXT("GibOnDeath"), false);
		Out.bClearCopyProp = Node.Bool(TEXT("Clear_Copy_Prop"), false);

		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Node.Kids)
		{
			if (!Kid.Value.IsValid())
			{
				continue;
			}
			if (Kid.Key == TEXT("traiteffect"))
			{
				const FString Effect = Trim(Kid.Value->Str(TEXT("Effect"), FString()));
				if (!Effect.IsEmpty())
				{
					Out.TraitEffects.Add(Effect);
				}
			}
			else if (Kid.Key == TEXT("trigger_casting"))
			{
				FElysiumDiscTriggerCast Cast;
				Cast.DisciplineFx = Trim(Kid.Value->Str(TEXT("DisciplineFX"), FString()));
				Cast.Source = Trim(Kid.Value->Str(TEXT("Source"), FString()));
				Cast.Affects = Trim(Kid.Value->Str(TEXT("Affects"), FString()));
				Out.TriggerCasting.Add(MoveTemp(Cast));
			}
			else if (Kid.Key == TEXT("onend"))
			{
				FElysiumDiscHit End;
				LoadDiscHit(*Kid.Value, TEXT("OnEnd"), End);
				Out.OnEnd.Add(MoveTemp(End));
			}
			else if (Kid.Key == TEXT("oninterrupt"))
			{
				FElysiumDiscHit Interrupt;
				LoadDiscHit(*Kid.Value, TEXT("OnInterrupt"), Interrupt);
				Out.OnInterrupt.Add(MoveTemp(Interrupt));
			}
			// Every remaining sub-block (`Effect_Tgt_Hit`, `Effect_Tgt_Instant`, `SoundFX`,
			// `OnInterruptSchedule`) is presentation. It is not carried here: the particle/sound
			// layer reads the file itself when it lands, and an empty carrier would read as support.
		}
	}

	void LoadDiscHit(const FKvNode& Node, const FString& Name, FElysiumDiscHit& Out)
	{
		Out = FElysiumDiscHit();
		Out.Name = Name;
		LoadDiscHitBody(Node, Out);
	}

	// The blocks a `DisciplineTgt` owns itself. Anything else that is a BLOCK is a hit table,
	// because hit-table names are authored freely.
	bool IsReservedDiscBlock(const FString& FoldedKey)
	{
		static const TSet<FString> Reserved =
		{
			TEXT("aoe"), TEXT("projectile"), TEXT("effect_src_instant"), TEXT("effect_src_hit"),
			TEXT("effect_src_proj_interrupt"),
		};
		// The `FREEME_`/`UNUSED`/`Old_` prefixed blocks are authored dead weight: not hit tables,
		// and no mapping names them.
		return Reserved.Contains(FoldedKey)
			|| FoldedKey.StartsWith(TEXT("freeme"))
			|| FoldedKey.StartsWith(TEXT("unused"))
			|| FoldedKey.StartsWith(TEXT("old_"));
	}
}

void FElysiumDiscHit::InheritFromRow(const FElysiumDiscHit& Base)
{
	auto TakeAmount = [](FElysiumDiscAmount& Mine, const FElysiumDiscAmount& Theirs)
	{
		if (!Mine.bAuthored && Theirs.bAuthored) { Mine = Theirs; }
	};
	auto TakeString = [](FString& Mine, const FString& Theirs)
	{
		if (Mine.IsEmpty() && !Theirs.IsEmpty()) { Mine = Theirs; }
	};

	TakeAmount(DmgHealth, Base.DmgHealth);
	TakeAmount(HealBlood, Base.HealBlood);
	TakeAmount(BloodSuck, Base.BloodSuck);
	TakeAmount(HealthBuffer, Base.HealthBuffer);
	TakeAmount(Duration, Base.Duration);
	if (HealthBufferBlockPercent == INDEX_NONE) { HealthBufferBlockPercent = Base.HealthBufferBlockPercent; }
	if (ChanceEffectivePercent == 100) { ChanceEffectivePercent = Base.ChanceEffectivePercent; }
	if (FlinchPercent == INDEX_NONE) { FlinchPercent = Base.FlinchPercent; }
	if (KnockbackPercent == INDEX_NONE) { KnockbackPercent = Base.KnockbackPercent; }
	if (AddToComfort == INDEX_NONE) { AddToComfort = Base.AddToComfort; }

	TakeString(AiSchedule, Base.AiSchedule);
	TakeString(AiNpcFlag, Base.AiNpcFlag);
	TakeString(Expression, Base.Expression);
	TakeString(GestureAnim, Base.GestureAnim);
	TakeString(PlayerAnim, Base.PlayerAnim);
	uint32 BaseMiscFlag = 0;
	ElysiumMiscFlags::ParseName(Base.MiscFlag, BaseMiscFlag);
	InheritedMiscFlags |= Base.InheritedMiscFlags | BaseMiscFlag;
	TakeString(MiscFlag, Base.MiscFlag);
	if (GestureDuration == 0.f) { GestureDuration = Base.GestureDuration; }

	bDoFrenzy = bDoFrenzy || Base.bDoFrenzy;
	bDoPossession = bDoPossession || Base.bDoPossession;
	bGibOnDeath = bGibOnDeath || Base.bGibOnDeath;
	bClearCopyProp = bClearCopyProp || Base.bClearCopyProp;

	if (TraitEffects.IsEmpty()) { TraitEffects = Base.TraitEffects; }
	if (TriggerCasting.IsEmpty()) { TriggerCasting = Base.TriggerCasting; }
	if (OnEnd.IsEmpty()) { OnEnd = Base.OnEnd; }
	if (OnInterrupt.IsEmpty()) { OnInterrupt = Base.OnInterrupt; }
}

bool FElysiumDisciplineTgt::ResolveHit(const FString& HitTable, FElysiumDiscHit& Out) const
{
	if (HitTable.IsEmpty())
	{
		return false;
	}
	const FElysiumDiscHit* Row = Hits.FindByPredicate([&HitTable](const FElysiumDiscHit& H)
		{ return H.Name.Equals(HitTable, ESearchCase::IgnoreCase); });
	if (Row == nullptr)
	{
		return false;
	}
	Out = *Row;

	// `InheritFrom` is a chain in principle; the shipped corpus is one link deep. The guard is a
	// visit set rather than a depth count so a hand-authored cycle terminates instead of hanging.
	TSet<FString> Seen;
	Seen.Add(ElysiumFold(Out.Name));
	FString Parent = Out.InheritFrom;
	while (!Parent.IsEmpty() && !Seen.Contains(ElysiumFold(Parent)))
	{
		Seen.Add(ElysiumFold(Parent));
		const FString ParentName = Parent;
		const FElysiumDiscHit* Base = Hits.FindByPredicate([&ParentName](const FElysiumDiscHit& H)
			{ return H.Name.Equals(ParentName, ESearchCase::IgnoreCase); });
		if (Base == nullptr)
		{
			break;
		}
		Out.InheritFromRow(*Base);
		Parent = Base->InheritFrom;
	}
	return true;
}

void FElysiumDisciplineTargets::Add(FElysiumDisciplineTgt&& Record)
{
	Index(ByName, Record.InternalName, Records.Num());
	Records.Add(MoveTemp(Record));
}

const FElysiumDisciplineTgt* FElysiumDisciplineTargets::Find(const FString& InternalName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InternalName));
	return (Idx && Records.IsValidIndex(*Idx)) ? &Records[*Idx] : nullptr;
}

const FElysiumDisciplineTgt* FElysiumDisciplineTargets::FindFor(const FString& Discipline,
	int32 Level) const
{
	// First match in file order. Within one Discipline the main level records precede the helper
	// records, so this is the power the player cast rather than a `Trigger_Casting` node that
	// shares its (Discipline, Level) pair.
	for (const FElysiumDisciplineTgt& Record : Records)
	{
		if (Record.Level == Level && Record.Discipline.Equals(Discipline, ESearchCase::IgnoreCase))
		{
			return &Record;
		}
	}
	return nullptr;
}

bool FElysiumDisciplineTargets::Load(FString& OutError)
{
	Records.Reset();
	ByName.Reset();

	int32 Loaded = 0;
	for (int32 File = 0; File <= 4; ++File)
	{
		const FString Rel = FString::Printf(TEXT("system/disciplinetgt_%03d.txt"), File);
		TSharedPtr<FKvNode> Root;
		FString FileError;
		if (!ReadVdata(*Rel, Root, FileError))
		{
			OutError = FileError;
			continue;
		}
		const FKvNode* List = Root.IsValid() ? Root->Child(TEXT("DisciplineTgtList")) : nullptr;
		if (List == nullptr)
		{
			OutError = FString::Printf(TEXT("no DisciplineTgtList block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
			continue;
		}
		++Loaded;

		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : List->Kids)
		{
			if (Kid.Key != TEXT("disciplinetgt") || !Kid.Value.IsValid())
			{
				continue;
			}
			const FKvNode& N = *Kid.Value;

			FElysiumDisciplineTgt Record;
			Record.Name = Trim(N.Str(TEXT("Name"), FString()));
			Record.InternalName = Trim(N.Str(TEXT("InternalName"), FString()));
			Record.Discipline = Trim(N.Str(TEXT("Discipline"), FString()));
			Record.Level = N.Int(TEXT("Level"), 0);
			Record.BloodCost = N.Int(TEXT("BloodCost"), 0);
			Record.bOvert = N.Bool(TEXT("Overt"), false);
			Record.bTriggerAISound = N.Bool(TEXT("TriggerAISound"), false);
			Record.SupernaturalLvl = N.Int(TEXT("SupernaturalLvl"), 0);
			Record.RecoveryTime = N.Flt(TEXT("RecoveryTime"), 0.f);
			Record.bRemoveOnTakeDamage = N.Bool(TEXT("ShouldRemove_OnTakeDamage"), false);
			Record.bRemoveOnHearCombat = N.Bool(TEXT("ShouldRemove_OnHearCombat"), false);
			Record.bRemoveOnWasBumped = N.Bool(TEXT("ShouldRemove_OnWasBumped"), false);
			Record.ViewModel = Trim(N.Str(TEXT("ViewModel"), FString()));
			Record.TargetHighlightParticle = Trim(N.Str(TEXT("TgtHightlightParticle"), FString()));

			if (const FKvNode* Proj = N.Child(TEXT("Projectile")))
			{
				Record.bHasProjectile = true;
				Record.ProjectileSpeed = Proj->Flt(TEXT("Speed"), 0.f);
				Record.ProjectileModel = Trim(Proj->Str(TEXT("Model"), FString()));
				Record.ProjectileParticle = Trim(Proj->Str(TEXT("Particle"), FString()));
				Record.bProjectileDiesOnHit = Proj->Bool(TEXT("ParticleDiesOnHit"), false);
			}

			if (const FKvNode* Area = N.Child(TEXT("AoE")))
			{
				Record.AoE.Range = Area->Flt(TEXT("Range"), 0.f);
				Record.AoE.MinRadius = Area->Flt(TEXT("MinRadius"), 0.f);
				Record.AoE.MaxRadius = Area->Flt(TEXT("MaxRadius"), 0.f);
				Record.AoE.bSourceIsTarget =
					Area->Str(TEXT("Source"), TEXT("Self")).Equals(TEXT("Target"), ESearchCase::IgnoreCase);

				const FString Shape = Trim(Area->Str(TEXT("Affects"), TEXT("Target")));
				if (Shape.StartsWith(TEXT("Self"), ESearchCase::IgnoreCase))
				{
					Record.AoE.Shape = EElysiumDiscShape::Self;
				}
				else if (Shape.StartsWith(TEXT("Radius"), ESearchCase::IgnoreCase))
				{
					Record.AoE.Shape = EElysiumDiscShape::Radius;
				}
				else if (Shape.StartsWith(TEXT("Cone"), ESearchCase::IgnoreCase))
				{
					Record.AoE.Shape = EElysiumDiscShape::Cone;
				}
				else
				{
					Record.AoE.Shape = EElysiumDiscShape::Target;
				}

				LoadDiscFilters(Area->Child(TEXT("Affects_Filters")), Record.AoE.Filters);
				for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : Area->Kids)
				{
					if (Sub.Key != TEXT("affects_table") || !Sub.Value.IsValid())
					{
						continue;
					}
					FElysiumDiscAffectsTable Table;
					LoadDiscFilters(Sub.Value->Child(TEXT("Affects_Filters")), Table.Filters);
					for (const TPair<FString, TSharedPtr<FKvNode>>& Row : Sub.Value->Kids)
					{
						if (!Row.Value.IsValid())
						{
							continue;
						}
						if (Row.Key == TEXT("mapping"))
						{
							FElysiumDiscMapping Mapping;
							LoadDiscMapping(*Row.Value, Mapping);
							Table.Mappings.Add(MoveTemp(Mapping));
						}
						else if (Row.Key == TEXT("default_mapping"))
						{
							FElysiumDiscMapping Mapping;
							LoadDiscMapping(*Row.Value, Mapping);
							Table.DefaultMappings.Add(MoveTemp(Mapping));
						}
					}
					Record.AoE.Tables.Add(MoveTemp(Table));
				}
			}

			// The hit tables: every remaining block, under its authored name.
			for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : N.Kids)
			{
				if (!Sub.Value.IsValid() || IsReservedDiscBlock(Sub.Key))
				{
					continue;
				}
				FElysiumDiscHit Hit;
				LoadDiscHit(*Sub.Value, Sub.Key, Hit);
				Record.Hits.Add(MoveTemp(Hit));
			}

			Add(MoveTemp(Record));
		}
	}

	if (Loaded == 0)
	{
		return false;
	}
	OutError.Reset();
	return !Records.IsEmpty();
}
