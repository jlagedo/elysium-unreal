#include "Substrate/ElysiumRulebookSubsystem.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumRulebook, Log, All);

// ================================================================================================
// Lifetime + the lazy accessors
// ================================================================================================

void UElysiumRulebookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterCommands();
}

void UElysiumRulebookSubsystem::Deinitialize()
{
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();
	Super::Deinitialize();
}

template <typename T>
const T& UElysiumRulebookSubsystem::Get(T& Table, bool& bLoaded, const TCHAR* Name, FString& Error)
{
	if (!bLoaded)
	{
		bLoaded = true;            // set first: a failure is remembered, not retried per read
		if (!Table.Load(Error))
		{
			UE_LOG(LogElysiumRulebook, Warning, TEXT("%s: %s"), Name, *Error);
		}
	}
	return Table;
}

const FElysiumStatTable& UElysiumRulebookSubsystem::Stats()
{
	return Get(StatTable, bStatsLoaded, TEXT("stats"), StatsError);
}

const FElysiumFeatTable& UElysiumRulebookSubsystem::Feats()
{
	return Get(FeatTable, bFeatsLoaded, TEXT("feats"), FeatsError);
}

const FElysiumRules& UElysiumRulebookSubsystem::Rules()
{
	return Get(RuleData, bRulesLoaded, TEXT("rules"), RulesError);
}

const FElysiumTraitEffects& UElysiumRulebookSubsystem::TraitEffects()
{
	return Get(EffectData, bEffectsLoaded, TEXT("traiteffects"), EffectsError);
}

const FElysiumClanTable& UElysiumRulebookSubsystem::Clans()
{
	return Get(ClanTable, bClansLoaded, TEXT("clans"), ClansError);
}

const FElysiumHistoryTable& UElysiumRulebookSubsystem::Histories()
{
	return Get(HistoryTable, bHistoriesLoaded, TEXT("histories"), HistoriesError);
}

const FElysiumQuestTables& UElysiumRulebookSubsystem::Quests()
{
	return Get(QuestTables, bQuestsLoaded, TEXT("quests"), QuestsError);
}

const FElysiumExperienceTable& UElysiumRulebookSubsystem::Experience()
{
	return Get(ExperienceTable, bExperienceLoaded, TEXT("experience"), ExperienceError);
}

const FElysiumLevelingTemplates& UElysiumRulebookSubsystem::Leveling()
{
	return Get(LevelingTemplates, bLevelingLoaded, TEXT("leveling"), LevelingError);
}

const FElysiumWizard& UElysiumRulebookSubsystem::Wizard()
{
	return Get(WizardData, bWizardLoaded, TEXT("wizard"), WizardError);
}

const FElysiumStrings& UElysiumRulebookSubsystem::Strings()
{
	return Get(StringData, bStringsLoaded, TEXT("strings"), StringsError);
}

int32 UElysiumRulebookSubsystem::LoadAll()
{
	TArray<FStatus> Status;
	GetStatus(Status);
	int32 Ok = 0;
	for (const FStatus& S : Status) { Ok += S.bLoaded ? 1 : 0; }
	return Ok;
}

void UElysiumRulebookSubsystem::GetStatus(TArray<FStatus>& Out)
{
	// Touching each accessor is what loads it, so this doubles as LoadAll's engine.
	const FElysiumClanTable& C = Clans();
	const FElysiumStatTable& S = Stats();

	int32 StatRows = 0;
	for (int32 i = 0; i < (int32)EElysiumTraitContainer::Count; ++i)
	{
		StatRows += S.Container((EElysiumTraitContainer)i).Num();
	}
	Out.Add({ TEXT("stats"), TEXT("system/stats.txt"), StatRows, S.IsValid(), StatsError });

	Out.Add({ TEXT("feats"),        TEXT("system/feats.txt"),
		Feats().Num(), Feats().IsValid(), FeatsError });

	Out.Add({ TEXT("rules"),        TEXT("system/rules.txt + rules_tables.txt"),
		Rules().BlockOrder.Num() + Rules().Tables.Num(), Rules().IsValid(), RulesError });

	Out.Add({ TEXT("traiteffects"), TEXT("system/traiteffect.txt + traiteffects000.txt"),
		TraitEffects().NumEffects(), TraitEffects().IsValid(), EffectsError });

	Out.Add({ TEXT("clans"),        TEXT("system/clandoc000.txt"),
		C.Clans.Num(), C.IsValid(), ClansError });

	Out.Add({ TEXT("npctemplates"), FString::Printf(TEXT("system/npctemplate*.txt (%d files)"),
		C.NpcFiles.Num()), C.NpcTemplates.Num(), !C.NpcTemplates.IsEmpty(), ClansError });

	Out.Add({ TEXT("histories"),    TEXT("system/histories000.txt"),
		Histories().Num(), Histories().IsValid(), HistoriesError });

	Out.Add({ TEXT("quests"),       TEXT("system/quests_*.txt (5 files)"),
		Quests().NumQuests(), Quests().IsValid(), QuestsError });

	Out.Add({ TEXT("experience"),   TEXT("system/experience_table.txt"),
		Experience().Num(), Experience().IsValid(), ExperienceError });

	Out.Add({ TEXT("leveling"),     TEXT("system/levelingtemplate_000.txt"),
		Leveling().Num(), Leveling().IsValid(), LevelingError });

	// Counted in popups rather than groups: the popups are the content, and the count is what a
	// parse regression would move first.
	Out.Add({ TEXT("wizard"),       TEXT("system/charcreatewizard.txt"),
		Wizard().NumPopups(), Wizard().IsValid(), WizardError });

	// Counted in entries rather than groups: a `NameMapping` resolves an INDEX, so a group that
	// silently lost its tail is the regression that matters.
	Out.Add({ TEXT("strings"),      TEXT("system/strings.txt + strings_internal.txt"),
		Strings().NumEntries(), Strings().IsValid(), StringsError });
}

// ================================================================================================
// Verbs
// ================================================================================================

namespace
{
	FString JoinRefs(const TArray<FElysiumTraitRef>& Refs)
	{
		FString Out;
		for (const FElysiumTraitRef& Ref : Refs)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" + ")) + Ref.Describe();
		}
		return Out.IsEmpty() ? FString(TEXT("(none)")) : Out;
	}

	FString DescribeEffect(const FElysiumTraitEffect& Fx)
	{
		if (Fx.Op == EElysiumTraitOp::Cost)
		{
			return FString::Printf(TEXT("%-28s Cost  %s"), *Fx.Trait, *Fx.Costs.Describe());
		}
		if (!Fx.ValueName.IsEmpty())
		{
			return FString::Printf(TEXT("%-28s %-9s %s"), *Fx.Trait,
				ElysiumTraitOpName(Fx.Op), *Fx.ValueName);
		}
		return FString::Printf(TEXT("%-28s %-9s %d%s"), *Fx.Trait, ElysiumTraitOpName(Fx.Op),
			Fx.Amount, Fx.bPercent ? TEXT("%") : TEXT(""));
	}

	// The whole-table dumps, kept out of ExecRules so the dispatch stays readable.
	void DumpTraits(const FElysiumStatTable& Stats)
	{
		for (int32 i = 0; i < (int32)EElysiumTraitContainer::Count; ++i)
		{
			const FElysiumStatContainer& C = Stats.Container((EElysiumTraitContainer)i);
			UE_LOG(LogElysiumRulebook, Display, TEXT("%s (%d) — default cost %s"),
				*C.InternalName, C.Num(), *C.DefaultCosts.Describe());
			for (const FElysiumStat& S : C.Stats)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %2d %-28s min %-7s max %-11s def %-6d %s"),
					S.Index, *S.InternalName, *S.MinExpr, *S.MaxExpr, S.Default,
					S.bDisabled ? TEXT("[disabled]") : TEXT(""));
			}
		}
	}
}

void UElysiumRulebookSubsystem::RegisterCommands()
{
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.rules"),
		TEXT("elysium.rules [<table> | stat|feat|quest|xp|clan|history|leveling <name>] — dump the ")
		TEXT("loaded vdata rulebook, one table, or one row. No args = every table's load state."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			ExecRules(Args);
		}),
		ECVF_Default));
}

void UElysiumRulebookSubsystem::ExecRules(const TArray<FString>& Args)
{
	// No args: the load state of every table, which is the first thing to look at when a reader
	// silently answers nothing.
	if (Args.Num() == 0)
	{
		TArray<FStatus> Status;
		GetStatus(Status);
		for (const FStatus& S : Status)
		{
			const FString Verdict = S.bLoaded
				? FString(TEXT("ok"))
				: FString::Printf(TEXT("FAILED — %s"), *S.Error);
			UE_LOG(LogElysiumRulebook, Display, TEXT("%-13s %-42s %5d rows  %s"),
				*S.Table, *S.File, S.Rows, *Verdict);
		}
		return;
	}

	const FString What = Args[0].ToLower();

	// --- whole-table dumps ----------------------------------------------------------------------
	if (Args.Num() == 1)
	{
		if (What == TEXT("stats"))
		{
			DumpTraits(Stats());
			return;
		}
		if (What == TEXT("feats"))
		{
			for (const FElysiumFeat& F : Feats().Feats)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %2d %-24s max %-3d %s  [%s/%s]"),
					F.Index, *F.InternalName, F.MaxValue, *JoinRefs(F.Bases),
					*F.PcWeighting, *F.NpcWeighting);
			}
			return;
		}
		if (What == TEXT("rules"))
		{
			const FElysiumRules& R = Rules();
			for (const FString& Block : R.BlockOrder)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %-24s %d keys"),
					*Block, R.Blocks[Block].Num());
			}
			for (const FElysiumRuleTable& T : R.Tables)
			{
				int32 Lo = 0, Hi = 0;
				T.KeyRange(Lo, Hi);
				UE_LOG(LogElysiumRulebook, Display, TEXT("  table %-40s [%d..%d]%s"),
					*T.InternalName, Lo, Hi, T.bClamping ? TEXT(" clamped") : TEXT(""));
			}
			return;
		}
		if (What == TEXT("traiteffects"))
		{
			const FElysiumTraitEffects& E = TraitEffects();
			for (const FElysiumTraitEffectGroup& G : E.Groups)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  [%s] %-36s %d effects"),
					*G.Category, *G.InternalName, G.Effects.Num());
			}
			return;
		}
		if (What == TEXT("clans"))
		{
			for (const FElysiumClanTemplate& C : Clans().Clans)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %2d %-20s %s"),
					C.Index, *C.TemplateName, C.IsPlayable() ? TEXT("[playable]") : TEXT(""));
			}
			return;
		}
		if (What == TEXT("npctemplates"))
		{
			for (const FElysiumClanTemplate& C : Clans().NpcTemplates)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %-28s parent %-20s %s"),
					*C.TemplateName, *C.ParentTemplateName, *C.SourceFile);
			}
			return;
		}
		if (What == TEXT("histories"))
		{
			for (const FElysiumHistory& H : Histories().Rows)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %2d %-28s %-18s %s"),
					H.Index, *H.InternalName, *H.CritterScope, *H.Effect);
			}
			return;
		}
		if (What == TEXT("quests"))
		{
			const FElysiumQuestTables& Q = Quests();
			for (int32 t = 0; t < FElysiumQuestTables::NumTables; ++t)
			{
				for (const FElysiumQuest& Quest : Q.Quests[t])
				{
					UE_LOG(LogElysiumRulebook, Display, TEXT("  [%d:%-11s] %-34s %d states"),
						t, FElysiumQuestTables::HubNames[t], *Quest.Title, Quest.States.Num());
				}
			}
			return;
		}
		if (What == TEXT("experience") || What == TEXT("xp"))
		{
			for (const FElysiumExperienceEntry& E : Experience().Rows)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %-14s %5d  %s"),
					*E.Key, E.Value, *E.Description);
			}
			return;
		}
		if (What == TEXT("leveling"))
		{
			for (const FElysiumLevelingTemplate& T : Leveling().Templates)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %-26s %2d groups, %4d steps%s"),
					*T.InternalName, T.Groups.Num(), T.NumSteps(),
					T.IsCharGen() ? TEXT("  [chargen]") : TEXT(""));
			}
			return;
		}

		UE_LOG(LogElysiumRulebook, Display, TEXT("unknown table '%s'. Tables: stats feats rules ")
			TEXT("traiteffects clans npctemplates histories quests experience leveling"), *Args[0]);
		return;
	}

	// --- single-row lookups ---------------------------------------------------------------------
	// Quest titles carry spaces, so the name is everything after the selector rejoined.
	FString Name;
	for (int32 i = 1; i < Args.Num(); ++i)
	{
		Name += (i > 1 ? TEXT(" ") : TEXT("")) + Args[i];
	}
	Name = Name.TrimQuotes();

	if (What == TEXT("stat"))
	{
		EElysiumTraitContainer Which = EElysiumTraitContainer::Attributes;
		const FElysiumStat* S = Stats().Find(Name, &Which);
		if (S == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no stat '%s'"), *Name);
			return;
		}
		const FElysiumStatContainer& C = Stats().Container(Which);
		UE_LOG(LogElysiumRulebook, Display,
			TEXT("%s — %s[%d]  min %s  max %s  default %d  sell>=%d buy<=%d  cost %s%s"),
			*S->InternalName, ElysiumTraitContainerName(Which), S->Index,
			*S->MinExpr, *S->MaxExpr, S->Default, S->MinSell, S->MaxBuy,
			*C.CostsFor(*S).Describe(), S->bDisabled ? TEXT("  [disabled]") : TEXT(""));
		for (const FString& Gate : S->IncPredependency)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("  requires  %s"), *Gate);
		}
		for (const TPair<FString, FElysiumRuleTable>& T : S->Tables)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("  table     %s (%d rows)"),
				*T.Value.InternalName, T.Value.Rows.Num());
		}
		return;
	}

	if (What == TEXT("feat"))
	{
		const FElysiumFeat* F = Feats().Find(Name);
		if (F == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no feat '%s'"), *Name);
			return;
		}
		UE_LOG(LogElysiumRulebook, Display,
			TEXT("%s — feat[%d]  max %d  bases: %s  automatic: %s  weighting %s/%s"),
			*F->InternalName, F->Index, F->MaxValue, *JoinRefs(F->Bases),
			*JoinRefs(F->Automatics), *F->PcWeighting, *F->NpcWeighting);
		return;
	}

	if (What == TEXT("quest"))
	{
		FElysiumQuestRef Ref;
		const FElysiumQuest* Q = Quests().Find(Name, &Ref);
		if (Q == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no quest '%s'"), *Name);
			return;
		}
		UE_LOG(LogElysiumRulebook, Display, TEXT("\"%s\" — %s  [table %d (%s), quest %d]"),
			*Q->Title, *Q->DisplayName, Ref.Table, FElysiumQuestTables::HubNames[Ref.Table],
			Ref.Quest);
		for (const FElysiumQuestState& S : Q->States)
		{
			// The XP award is a key, so it is resolved here rather than printed raw — that is the
			// whole cross-table contract in one line.
			FString Award;
			if (!S.AwardXp.IsEmpty())
			{
				const FElysiumExperienceEntry* E = Experience().Find(S.AwardXp);
				Award = E ? FString::Printf(TEXT("  xp %s=%d"), *S.AwardXp, E->Value)
					: FString::Printf(TEXT("  xp %s=UNRESOLVED"), *S.AwardXp);
			}
			UE_LOG(LogElysiumRulebook, Display, TEXT("  state %-4d %-11s%s  %s"),
				S.Id, *S.Type, *Award, *S.Description.Left(90));
		}
		return;
	}

	if (What == TEXT("xp") || What == TEXT("experience"))
	{
		const FElysiumExperienceEntry* E = Experience().Find(Name);
		if (E == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no experience row '%s'"), *Name);
			return;
		}
		// The award path takes floor(value/100) and keeps the remainder; both are shown so a wrong
		// number is traceable to the row rather than to the arithmetic.
		UE_LOG(LogElysiumRulebook, Display, TEXT("%s — raw %d => %d xp (+%d residue)  \"%s\""),
			*E->Key, E->Value, E->Value / 100, E->Value % 100, *E->Description);
		return;
	}

	if (What == TEXT("clan"))
	{
		FElysiumClanTemplate Resolved;
		if (!Clans().Resolve(Name, Resolved))
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no clan or npc template '%s'"), *Name);
			return;
		}
		TArray<FString> Chain;
		Clans().ParentChain(Name, Chain);
		UE_LOG(LogElysiumRulebook, Display,
			TEXT("%s — %s[%d]  clan '%s'  kindred %d  chain: %s"),
			*Resolved.TemplateName, *Resolved.SourceFile, Resolved.Index,
			*Resolved.GeneralStr(TEXT("Clan")), Resolved.GeneralInt(TEXT("Kindred")),
			*FString::Join(Chain, TEXT(" -> ")));
		UE_LOG(LogElysiumRulebook, Display, TEXT("  effects: clan '%s'  frenzy '%s'"),
			*Resolved.GeneralStr(TEXT("ClanEffect")), *Resolved.GeneralStr(TEXT("FrenzyEffect")));
		for (const TPair<FString, int32>& D : Resolved.Disciplines)
		{
			// -1 is the sentinel for a discipline this template cannot take at all.
			UE_LOG(LogElysiumRulebook, Display, TEXT("  discipline %-20s %d%s"),
				*D.Key, D.Value, D.Value < 0 ? TEXT("  (not available)") : TEXT(""));
		}
		UE_LOG(LogElysiumRulebook, Display, TEXT("  %d attributes, %d abilities, %d general keys"),
			Resolved.Attributes.Num(), Resolved.Abilities.Num(), Resolved.General.Num());
		return;
	}

	if (What == TEXT("history"))
	{
		const FElysiumHistory* H = Histories().Find(Name);
		if (H == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no history '%s'"), *Name);
			return;
		}
		UE_LOG(LogElysiumRulebook, Display, TEXT("%s — history[%d]  scope %s  effect '%s'"),
			*H->InternalName, H->Index, *H->CritterScope, *H->Effect);
		if (const FElysiumTraitEffectGroup* G = TraitEffects().Find(H->Effect))
		{
			for (const FElysiumTraitEffect& Fx : G->Effects)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("  %s"), *DescribeEffect(Fx));
			}
		}
		return;
	}

	if (What == TEXT("effect") || What == TEXT("traiteffect"))
	{
		const FElysiumTraitEffectGroup* G = TraitEffects().Find(Name);
		if (G == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no trait-effect group '%s'"), *Name);
			return;
		}
		UE_LOG(LogElysiumRulebook, Display, TEXT("%s — [%s] %d effects"),
			*G->InternalName, *G->Category, G->Effects.Num());
		for (const FElysiumTraitEffect& Fx : G->Effects)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("  %s"), *DescribeEffect(Fx));
		}
		return;
	}

	if (What == TEXT("leveling"))
	{
		const FElysiumLevelingTemplate* T = Leveling().Find(Name);
		if (T == nullptr)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("no leveling template '%s'"), *Name);
			return;
		}
		UE_LOG(LogElysiumRulebook, Display, TEXT("%s — %d groups, %d steps, clan '%s'"),
			*T->InternalName, T->Groups.Num(), T->NumSteps(), *T->ClanDependency);
		for (const FElysiumLevelGroup& G : T->Groups)
		{
			UE_LOG(LogElysiumRulebook, Display, TEXT("  group%s %s"),
				G.bGated ? TEXT("") : TEXT(" (ungated)"),
				*FString::Join(G.Dependencies, TEXT(" && ")));
			for (const FElysiumLevelStep& S : G.Steps)
			{
				UE_LOG(LogElysiumRulebook, Display, TEXT("    buy %-24s -> %d"), *S.Trait, S.Value);
			}
		}
		return;
	}

	UE_LOG(LogElysiumRulebook, Display, TEXT("usage: elysium.rules [<table> | ")
		TEXT("stat|feat|quest|xp|clan|history|effect|leveling <name>]"));
}
