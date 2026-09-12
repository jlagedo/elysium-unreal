// The Discipline runtime. `Substrate/ElysiumDisciplines.h` is the contract;
// `docs/vtmb/disciplines.md` is the behaviour.
//
// Two execution families over one shared authority, both landing behind existing seams: the sheet
// (blood, the thirteen learned/active slot pairs, `HealthBuffer`, `Automatic_Soak_Successes`), the
// trait-effect layer, the typed damage path, and one owned timed event per (character, discipline)
// on the one queue. Nothing here adds a clock, a scheduler or a dispatcher.
// HitInfo flag words and comfort membership use the same apply/cleanup lifecycle as trait effects.

#include "Substrate/ElysiumDisciplines.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U — the one units conversion
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"        // the block's one field list
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplineTargetTables.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLaw.h"      // the law channels the commit writes through
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"      // the `AI_Schedule` channel's one door
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"

namespace
{
	using EC = EElysiumTraitContainer;
	// DAT_10739a64 is zero only while HitInfo installs its authored AI_Schedule.
	bool GApplyingDisciplineSchedule = false;

	// The compiled Discipline order. These are `stats.txt`'s own InternalNames for the thirteen
	// learned slots, so one array serves the slot lookup, the `DisciplineTgt` `Discipline` key and
	// the trait-effect payload rows (`"Duration 120%"` on `Fortitude`).
	const TCHAR* const GDisciplineNames[ElysiumDisciplines::Count] =
	{
		TEXT("Animalism"), TEXT("Auspex"), TEXT("Blood_Healing"), TEXT("Celerity"),
		TEXT("Corpus_Vampirus"), TEXT("Dementation"), TEXT("Dominate"), TEXT("Fortitude"),
		TEXT("Obfuscate"), TEXT("Potence"), TEXT("Presence"), TEXT("Protean"),
		TEXT("Thaumaturgy"),
	};

	// The three tables this domain reads, resolved once per entry point. The character's own
	// rulebook subsystem always wins; a bare substrate world falls back to whatever a Substrate-tier
	// test bound (`Substrate/ElysiumSheetMath.h` → "The headless table binding"), which is null in a
	// real run. Held as borrowed pointers because every one of them is session-lifetime.
	struct FRules
	{
		const FElysiumStatTable* Stats = nullptr;
		const FElysiumDisciplineTargets* Targets = nullptr;
		const FElysiumClanTable* Clans = nullptr;

		bool IsValid() const { return Stats != nullptr; }

		const FElysiumStat* Learned(int32 Index) const
		{
			return Stats ? Stats->Container(EC::Disciplines).At(Index) : nullptr;
		}
		const FElysiumStat* Active(int32 Index) const
		{
			return Stats ? Stats->Container(EC::ActiveDisciplines).At(Index) : nullptr;
		}
	};

	FRules ResolveRules(const FElysiumCombatCharacter& Char)
	{
		FRules Out;
		UElysiumSessionSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
		if (UElysiumRulebookSubsystem* Book = GameState ? GameState->Rulebook() : nullptr)
		{
			Out.Stats = &Book->Stats();
			Out.Targets = &Book->DisciplineTargets();
			Out.Clans = &Book->Clans();
			return Out;
		}
		const ElysiumSheetRules::FBoundTables& Bound = ElysiumSheetRules::BoundTables();
		Out.Stats = Bound.Stats;
		Out.Targets = Bound.DisciplineTargets;
		Out.Clans = Bound.Clans;
		return Out;
	}

	// One warning per distinct subject for the whole session — the shape `ElysiumDamage`'s
	// unapplied-filter report uses. A carried-but-unexecuted channel is a gap, so it is reported
	// (the runtime-failures rule); reporting it once per record/channel keeps it a work list rather
	// than per-cast spam.
	void ReportOnce(const FString& Key, const FString& Message)
	{
		static TSet<FString> Reported;
		if (Reported.Contains(Key))
		{
			return;
		}
		Reported.Add(Key);
		UE_LOG(LogElysiumPlayer, Warning, TEXT("discipline: %s"), *Message);
	}

	// Every random draw in this domain comes off the owned Dice stream, so a save carries the
	// run across a load and a seeded test is deterministic.
	int32 RollPercent()
	{
		return ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(1, 100);
	}

	int32 RollRange(int32 Min, int32 Max)
	{
		return (Max <= Min) ? Min : ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(Min, Max);
	}

	// An authored `FElysiumDiscAmount` resolved to one number. A range rolls; a percentage is
	// applied to `Base` by the caller, which is the only thing that knows what it is a percentage
	// of.
	int32 ResolveAmount(const FElysiumDiscAmount& Amount)
	{
		return Amount.IsRange() ? RollRange(Amount.Min, Amount.Max) : Amount.Min;
	}

	// --- The world-area eligibility read ---
	// The gate reads the world area type through the one owner, `ElysiumLaw::WorldAreaType`: the
	// registered `events_world.safearea` field, falling back to the authored `worldspawn` baseline
	// the entity is seeded from.

	// The player eligibility virtual (`docs/vtmb/disciplines.md` § "World-area eligibility and
	// transition teardown"): the ordinary path refuses world area type 2, Elysium, and one branch
	// precedes it — compiled index 4, Bloodbuff, is admitted when the retained action target is an
	// entity whose compact player action is 300, `LockPick`.
	bool PassesWorldAreaGate(FElysiumCombatCharacter& Char, int32 Index)
	{
		if (Char.World == nullptr)
		{
			return true;   // a bare substrate world has no area policy to refuse against
		}
		if (ElysiumLaw::WorldAreaType(*Char.World) != static_cast<int32>(ElysiumLaw::EArea::Elysium))
		{
			return true;
		}
		if (Index == ElysiumDisciplines::CorpusVampirus)
		{
			// **SEAM** — the exception needs the retained action-target handle and the compact
			// player-action table, neither of which this runtime carries. The branch is written out
			// so the recovered admission lands as one predicate rather than as a redesign, and it
			// is reported because a Bloodbuff cast in Elysium is refused here where retail admits it.
			ReportOnce(TEXT("worldarea.lockpick"),
				TEXT("the Bloodbuff-while-LockPick exception is unbuilt — this runtime has no "
					"retained action-target handle and no compact player-action table, so a "
					"Bloodbuff cast in Elysium is refused with every other power"));
		}
		return false;
	}

	// --- Predependency ---------------------------------------------------------------------------
	// `stats.txt` authors two shapes of gate on an `Active_*` stat: the numeric comparisons
	// `ElysiumSheetRules::EvalPredependency` already reads (`"BloodPool > 0"`,
	// `"Active_Protean < 5"`), and an `Action`'s `"Clan != Toreador"`, whose right side is a clan
	// DISPLAY NAME rather than a number. The clan slot stores the level-script 2..8 encoding and
	// `Clan` carries `NameFunc ClanNameFunc`, so resolving the name to that encoding is the file's
	// own indirection, not an invention.
	bool EvalActionPredependency(const FString& Expr, const FElysiumSheet& Sheet)
	{
		const FString S = Expr.TrimStartAndEnd();
		if (S.IsEmpty())
		{
			return true;
		}
		static const TCHAR* const Ops[] = { TEXT("=="), TEXT("!=") };
		for (const TCHAR* Op : Ops)
		{
			const int32 At = S.Find(Op);
			if (At == INDEX_NONE)
			{
				continue;
			}
			const FString Lhs = S.Left(At).TrimStartAndEnd();
			const FString Rhs = S.Mid(At + 2).TrimStartAndEnd();
			if (!Lhs.Equals(TEXT("Clan"), ESearchCase::IgnoreCase) || Rhs.IsNumeric())
			{
				break;   // an ordinary numeric comparison — the shared evaluator owns it
			}
			const int32 Named = FElysiumSheet::ClanFromName(Rhs);
			if (Named == 0)
			{
				// A clan name nothing owns: reported, and the gate opens, which is the shared
				// evaluator's own posture for a side it cannot read.
				ReportOnce(FString::Printf(TEXT("predep.clan.%s"), *Rhs),
					FString::Printf(TEXT("`%s` names no clan — the gate opens"), *S));
				return true;
			}
			return (FCString::Strcmp(Op, TEXT("==")) == 0)
				? Sheet.Clan() == Named
				: Sheet.Clan() != Named;
		}
		return ElysiumSheetRules::EvalPredependency(S, Sheet);
	}

	// --- The active-state `Action` rows ----------------------------------------------------------
	// `"BloodPool -1"` — an InternalName and a signed delta, applied through the sheet.
	void ApplyStatMutation(FElysiumCombatCharacter& Char, const FString& Mutation)
	{
		FString Name;
		FString Amount;
		if (!Mutation.TrimStartAndEnd().Split(TEXT(" "), &Name, &Amount))
		{
			ReportOnce(FString::Printf(TEXT("action.stat.%s"), *Mutation),
				FString::Printf(TEXT("unreadable Action Stat `%s`"), *Mutation));
			return;
		}
		EElysiumTraitContainer Container;
		int32 Slot = INDEX_NONE;
		if (!ElysiumFindSheetSlot(*Name.TrimStartAndEnd(), Container, Slot))
		{
			ReportOnce(FString::Printf(TEXT("action.stat.%s"), *Mutation),
				FString::Printf(TEXT("Action Stat `%s` names no trait"), *Mutation));
			return;
		}
		Char.AddTrait(Container, Slot, FCString::Atoi(*Amount.TrimStartAndEnd()));
	}

	// The named native functions an `Action` can call. Only `BloodHealFunc` is authored.
	void ApplyActionFunction(FElysiumCombatCharacter& Char, const FString& Function, int32 Level)
	{
		if (Function.Equals(TEXT("BloodHealFunc"), ESearchCase::IgnoreCase))
		{
			// **SEAM** — `docs/vtmb/disciplines.md` marks Blood Healing's exact blood/health steps
			// **[open]**: the authored block supplies the 5 s window and the `Discipline (Blood_Heal)`
			// group, and states that `BloodHealFunc` owns the transaction, but not how much blood it
			// spends or over what cadence. What IS recovered and already built is the ratio
			// transaction itself — `VampHeal_Info.VampFeedingHeal_Info`'s `BloodToHealthRatio`,
			// which `FElysiumCombatCharacter::BloodHeal` implements — so the one blood the
			// activation's own `Stat` row does not deduct is the missing term, not the arithmetic.
			ReportOnce(TEXT("func.bloodheal"),
				TEXT("`BloodHealFunc` is unbuilt — the active block gives the 5 s window and the "
					"trait group, but the blood spent and the pulse cadence are open "
					"(disciplines.md, \"close Blood Healing's exact blood/health transaction\"); "
					"`FElysiumCombatCharacter::BloodHeal` is the transaction it would call"));
			return;
		}
		ReportOnce(FString::Printf(TEXT("func.%s"), *Function),
			FString::Printf(TEXT("Action Function `%s` (level %d) has no implementation"),
				*Function, Level));
	}

	// The per-power native consumers `docs/vtmb/disciplines.md` marks open. Each is reported at its
	// own activation so a live run enumerates the remaining work rather than looking complete.
	void ReportNativeConsumerSeams(const FElysiumCombatCharacter& Char, int32 Index, int32 Level)
	{
		switch (Index)
		{
		case ElysiumDisciplines::Celerity:
			// The consumer is reachable — `UElysiumSessionSubsystem::TimeControl().SetScale` is
			// the one place world time scales — but the per-rank curve is the missing value.
			ReportOnce(TEXT("native.celerity"),
				TEXT("Celerity's time/movement consumer is unbuilt: `FElysiumTimeControl::SetScale` "
					"is reachable, but the per-rank scale curve is open "
					"(disciplines.md, \"recover Celerity's native time/movement consumer and level "
					"curve\"). The active slot, its duration and its trait group are live"));
			break;
		case ElysiumDisciplines::Obfuscate:
			ReportOnce(TEXT("native.obfuscate"),
				TEXT("Obfuscate's visibility/break matrix is unbuilt: the duration gates, the active "
					"slot and the translucency trait groups are live, the movement/bump/interaction/"
					"stealth-kill break rules and the break-damage multipliers are open "
					"(disciplines.md, \"recover the complete Obfuscate visibility/break/damage-bonus "
					"matrix\")"));
			break;
		case ElysiumDisciplines::Protean:
			ReportOnce(TEXT("native.protean"),
				TEXT("Protean's transform lifecycle is unbuilt: the cumulative level trait groups "
					"apply, the equipment swap (`item_w_claws`, the excluded ordinary form) and the "
					"war-form model transition are open (disciplines.md, \"recover Protean's "
					"transform start/finish, equipment and teardown lifecycle\")"));
			break;
		case ElysiumDisciplines::Presence:
			ReportOnce(TEXT("native.presence"),
				TEXT("Presence's AoE pulse is unbuilt: the 16 s active state and its trait groups are "
					"live, the pulse caller and the second radius table "
					"(`Presence_Effect_Radius` against the target records' own radii) are open "
					"(disciplines.md, \"identify Presence's native pulse caller and reconcile its "
					"two radius tables\")"));
			break;
		default:
			break;
		}
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s activated %s at %d"),
			*Char.DebugString(), GDisciplineNames[Index], Level);
	}

	// --- Target-set construction -----------------------------------------------------------------

	// What one candidate is, resolved once so the ordered filter walk reads properties rather than
	// re-querying the rulebook per row.
	struct FCandidate
	{
		FElysiumCombatCharacter* Char = nullptr;
		bool bIsPlayer = false;
		bool bIsSelf = false;
		bool bIsPrimary = false;
		bool bKindred = false;
		bool bBoss = false;
		bool bHasTemplate = false;
		FString Template;
		int32 Strata = INDEX_NONE;
	};

	// The registered `stattemplate` field on the NPC leaf, read through the ordinary class-chain
	// walk so this service needs nothing from `ElysiumNpcClasses.cpp`.
	FString ReadStatTemplate(const FElysiumEntity& Ent)
	{
		if (Ent.Class == nullptr)
		{
			return FString();
		}
		static const FName StatTemplate(TEXT("stattemplate"));
		const FElysiumFieldAccessor* Acc =
			FElysiumClassRegistry::Get().FindField(*Ent.Class, StatTemplate);
		return (Acc && Acc->Get) ? Acc->Get(Ent).ToString() : FString();
	}

	void ResolveCandidate(FCandidate& Out, const FRules& Rules)
	{
		Out.bKindred = Out.Char->IsKindred();
		Out.Template = ReadStatTemplate(*Out.Char);
		Out.bHasTemplate = !Out.Template.IsEmpty();
		if (Rules.Clans && Out.bHasTemplate)
		{
			// `Boss`, `Supernatural` and `DisciplineStrata` are General keys on the resolved
			// `npctemplate*.txt` block — the same place `Kindred` lives, and the same read
			// `SeedSheet` already makes.
			FElysiumClanTemplate Resolved;
			if (Rules.Clans->Resolve(Out.Template, Resolved))
			{
				Out.bBoss = Resolved.GeneralInt(TEXT("Boss"), 0) != 0;
				Out.Strata = Resolved.HasGeneral(TEXT("DisciplineStrata"))
					? Resolved.GeneralInt(TEXT("DisciplineStrata"), 0) : INDEX_NONE;
				if (Resolved.HasGeneral(TEXT("Supernatural")))
				{
					Out.bKindred = Resolved.GeneralInt(TEXT("Supernatural"), 0) != 0;
				}
			}
		}
	}

	// One `Affects_Filters` row against one candidate.
	//
	// A row this runtime cannot evaluate resolves as **"the candidate is not that thing"**: the
	// positive form fails and the `No_*` form passes. That keeps the two spellings consistent and
	// keeps an unevaluable identity table (`CharTemplate Gargoyle`) from shadowing the generic
	// `Human`/`Supernatural` tables that follow it in the ordered walk — which is the outcome a
	// character that is not a Gargoyle would have had anyway.
	bool FilterPasses(const FElysiumDiscFilterRow& Row, const FCandidate& Candidate,
		const FString& RecordName)
	{
		switch (Row.Kind)
		{
		case EElysiumDiscFilter::Critter:
			// Every candidate in the set is a combat character, which is what "a living creature"
			// admits; a `"0"` value asserts nothing.
			return true;
		case EElysiumDiscFilter::Human:          return !Candidate.bKindred;
		case EElysiumDiscFilter::Supernatural:   return Candidate.bKindred;
		case EElysiumDiscFilter::NoSupernatural: return !Candidate.bKindred;
		case EElysiumDiscFilter::Boss:           return Candidate.bBoss;
		case EElysiumDiscFilter::NoBoss:         return !Candidate.bBoss;
		case EElysiumDiscFilter::Self:           return Candidate.bIsSelf;
		case EElysiumDiscFilter::NoSelf:         return !Candidate.bIsSelf;
		case EElysiumDiscFilter::Invulnerable:   return Candidate.Char->IsUnkillable();
		case EElysiumDiscFilter::NoInvulnerable: return !Candidate.Char->IsUnkillable();
		case EElysiumDiscFilter::Player:         return Candidate.bIsPlayer;
		case EElysiumDiscFilter::PrimaryTarget:  return Candidate.bIsPrimary;
		case EElysiumDiscFilter::CharTemplate:
			return Candidate.bHasTemplate
				&& Candidate.Template.Equals(Row.Text, ESearchCase::IgnoreCase);
		case EElysiumDiscFilter::DisciplineStrata:
			return Candidate.Strata != INDEX_NONE && Candidate.Strata == Row.Number;
		case EElysiumDiscFilter::Chance:
			return RollPercent() <= Row.Number;
		case EElysiumDiscFilter::NoFriends:
			// **SEAM** — the combat relationship table lives on the NPC leaf and this service does
			// not reach it (the three social domains stay apart, so nothing else answers it).
			// "Not a friend" is the passing side, so an unevaluable row admits the candidate.
			ReportOnce(FString::Printf(TEXT("filter.nofriends.%s"), *RecordName),
				FString::Printf(TEXT("`%s` filters on No_Friends and the relationship read is "
					"unbuilt — every candidate passes"), *RecordName));
			return true;
		case EElysiumDiscFilter::Combatant:
		case EElysiumDiscFilter::NonCombatant:
			// **SEAM** — the combatant classification has no producer. Positive form fails,
			// negative form passes, per the rule above.
			ReportOnce(FString::Printf(TEXT("filter.combatant.%s"), *RecordName),
				FString::Printf(TEXT("`%s` filters on %s and the combatant classification is "
					"unbuilt"), *RecordName, ElysiumDiscFilterName(Row.Kind)));
			return Row.Kind == EElysiumDiscFilter::NonCombatant;
		default:
			ReportOnce(FString::Printf(TEXT("filter.unknown.%s"), *Row.RawKey),
				FString::Printf(TEXT("unrecognised Affects_Filters key `%s` in `%s`"),
					*Row.RawKey, *RecordName));
			return false;
		}
	}

	bool FiltersPass(const FElysiumDiscFilterSet& Set, const FCandidate& Candidate,
		const FString& RecordName)
	{
		// The set is an AND: every authored row has to admit the candidate.
		for (const FElysiumDiscFilterRow& Row : Set.Rows)
		{
			if (!FilterPasses(Row, Candidate, RecordName))
			{
				return false;
			}
		}
		return true;
	}

	// The caster's aim target — the record's `Target` shape and its `Primary_Target` filter.
	//
	// **CHOSEN** — retail reads the player's retained action-target handle; this runtime has none
	// (the same gap the ranged weapon half reports at its own producer). The primary target is the
	// nearest live combat character inside the record's own `Range` and
	// `ElysiumDisciplines::AimConeHalfAngleDegrees`, which is the acquisition rule the melee half
	// already uses. Range and the filter set stay the record's.
	FElysiumCombatCharacter* AcquirePrimary(FElysiumCombatCharacter& Caster, float RangeSourceUnits)
	{
		if (Caster.World == nullptr)
		{
			return nullptr;
		}
		const float Reach = FMath::Max(RangeSourceUnits, 0.f) * ElysiumMove::U;
		const float ConeDot =
			FMath::Cos(FMath::DegreesToRadians(ElysiumDisciplines::AimConeHalfAngleDegrees));
		const FVector Eye = Caster.EyePosition();
		const FVector Forward =
			FRotator(Caster.Angles.X, Caster.Angles.Y, Caster.Angles.Z).Vector().GetSafeNormal();

		FElysiumCombatCharacter* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (const TUniquePtr<FElysiumEntity>& Candidate : Caster.World->Entities())
		{
			FElysiumEntity* Ent = Candidate.Get();
			if (Ent == nullptr || Ent == &Caster || Ent->IsInert())
			{
				continue;
			}
			FElysiumCombatCharacter* Other = Ent->AsCombatCharacter();
			if (Other == nullptr)
			{
				continue;
			}
			const FVector To = Other->EyePosition() - Eye;
			const float DistSq = To.SizeSquared();
			if (Reach > 0.f && DistSq > Reach * Reach)
			{
				continue;
			}
			if (!Forward.IsNearlyZero() && !To.IsNearlyZero()
				&& FVector::DotProduct(Forward, To.GetSafeNormal()) < ConeDot)
			{
				continue;
			}
			if (Best == nullptr || DistSq < BestDistSq
				|| (FMath::IsNearlyEqual(DistSq, BestDistSq) && Other->Handle.Index < Best->Handle.Index))
			{
				Best = Other;
				BestDistSq = DistSq;
			}
		}
		return Best;
	}

	// Step 3: the legal target set, in stable entity order.
	void BuildTargetSet(FElysiumCombatCharacter& Caster, const FElysiumDisciplineTgt& Record,
		const FRules& Rules, TArray<FCandidate>& Out)
	{
		Out.Reset();
		if (Caster.World == nullptr)
		{
			return;
		}
		FElysiumCombatCharacter* Primary = nullptr;
		if (Record.AoE.Shape == EElysiumDiscShape::Target || Record.AoE.bSourceIsTarget)
		{
			Primary = AcquirePrimary(Caster, Record.AoE.Range);
		}

		// Where a Radius/Cone shape is centred: the caster, or the aim target when the record says
		// `"Source" "Target"`.
		const FVector Centre = (Record.AoE.bSourceIsTarget && Primary)
			? Primary->Origin : Caster.Origin;
		const float RangeCm = FMath::Max(Record.AoE.Range, 0.f) * ElysiumMove::U;
		const float MinRadiusCm = FMath::Max(Record.AoE.MinRadius, 0.f) * ElysiumMove::U;
		const float MaxRadiusCm = Record.AoE.MaxRadius > 0.f
			? Record.AoE.MaxRadius * ElysiumMove::U : RangeCm;
		const float ConeDot =
			FMath::Cos(FMath::DegreesToRadians(ElysiumDisciplines::ConeHalfAngleDegrees));
		const FVector Forward =
			FRotator(Caster.Angles.X, Caster.Angles.Y, Caster.Angles.Z).Vector().GetSafeNormal();

		TArray<FElysiumCombatCharacter*> Raw;
		switch (Record.AoE.Shape)
		{
		case EElysiumDiscShape::Self:
			Raw.Add(&Caster);
			break;
		case EElysiumDiscShape::Target:
			if (Primary) { Raw.Add(Primary); }
			break;
		case EElysiumDiscShape::Radius:
		case EElysiumDiscShape::Cone:
			for (const TUniquePtr<FElysiumEntity>& Candidate : Caster.World->Entities())
			{
				FElysiumEntity* Ent = Candidate.Get();
				if (Ent == nullptr || Ent->IsInert())
				{
					continue;
				}
				FElysiumCombatCharacter* Other = Ent->AsCombatCharacter();
				if (Other == nullptr)
				{
					continue;
				}
				const FVector To = Other->Origin - Centre;
				const float Dist = To.Size();
				if (Dist > MaxRadiusCm || Dist < MinRadiusCm)
				{
					continue;
				}
				if (Record.AoE.Shape == EElysiumDiscShape::Cone && Other != &Caster
					&& !Forward.IsNearlyZero() && !To.IsNearlyZero()
					&& FVector::DotProduct(Forward, To.GetSafeNormal()) < ConeDot)
				{
					continue;
				}
				Raw.Add(Other);
			}
			break;
		}

		const FElysiumEntityHandle PlayerHandle = Caster.World->PlayerHandle();
		for (FElysiumCombatCharacter* Char : Raw)
		{
			FCandidate Candidate;
			Candidate.Char = Char;
			Candidate.bIsSelf = (Char == &Caster);
			Candidate.bIsPrimary = (Char == Primary);
			Candidate.bIsPlayer = PlayerHandle.IsSet() && Char->Handle.Index == PlayerHandle.Index;
			ResolveCandidate(Candidate, Rules);
			// The AoE's own filter set is admission: it runs before any `Affects_Table` is read.
			if (FiltersPass(Record.AoE.Filters, Candidate, Record.InternalName))
			{
				Out.Add(MoveTemp(Candidate));
			}
		}
	}

	// Step 7's first half: which `HitTable` this target resolves to. Ordered `Affects_Table` walk;
	// within the chosen table the ordered chance mappings branch and a `Default_Mapping` catches
	// the remainder. `Count` limits how many of the resolved set a mapping claims, so the counter
	// is per cast, not per target.
	FString ResolveHitTable(const FElysiumDisciplineTgt& Record, const FCandidate& Candidate,
		TMap<int32, int32>& InOutCounts)
	{
		for (int32 t = 0; t < Record.AoE.Tables.Num(); ++t)
		{
			const FElysiumDiscAffectsTable& Table = Record.AoE.Tables[t];
			if (!FiltersPass(Table.Filters, Candidate, Record.InternalName))
			{
				continue;
			}
			auto TakeMapping = [&](const TArray<FElysiumDiscMapping>& Rows, int32 Base) -> const FElysiumDiscMapping*
			{
				for (int32 m = 0; m < Rows.Num(); ++m)
				{
					const FElysiumDiscMapping& Mapping = Rows[m];
					const int32 Key = t * 1000 + Base + m;
					if (Mapping.Count != INDEX_NONE)
					{
						int32& Remaining = InOutCounts.FindOrAdd(Key, Mapping.Count);
						if (Remaining <= 0)
						{
							continue;
						}
						if (Mapping.ChancePercent != INDEX_NONE && RollPercent() > Mapping.ChancePercent)
						{
							continue;
						}
						--Remaining;
						return &Mapping;
					}
					if (Mapping.ChancePercent != INDEX_NONE && RollPercent() > Mapping.ChancePercent)
					{
						continue;
					}
					return &Mapping;
				}
				return nullptr;
			};

			if (const FElysiumDiscMapping* Chosen = TakeMapping(Table.Mappings, 0))
			{
				return Chosen->HitTable;
			}
			if (const FElysiumDiscMapping* Fallback = TakeMapping(Table.DefaultMappings, 500))
			{
				return Fallback->HitTable;
			}
			// The table admitted the target and named nothing: authored, and a real outcome (the
			// target is in the set and takes no hit). Not a failure.
			return FString();
		}
		return FString();
	}
}

// --- Names and results ---

namespace ElysiumDisciplines
{

const TCHAR* InternalName(int32 Index)
{
	return (Index >= 0 && Index < Count) ? GDisciplineNames[Index] : nullptr;
}

int32 IndexFromName(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (Name.IsNumeric())
	{
		const int32 N = FCString::Atoi(*Name);
		return (N >= 0 && N < Count) ? N : INDEX_NONE;
	}
	for (int32 i = 0; i < Count; ++i)
	{
		// The datamap spelling is the lowercased InternalName, so one compare serves both.
		if (Name.Equals(GDisciplineNames[i], ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	// The two names the compiled order and the display vocabulary disagree on.
	if (Name.Equals(TEXT("Bloodbuff"), ESearchCase::IgnoreCase)) { return CorpusVampirus; }
	if (Name.Equals(TEXT("BloodHealing"), ESearchCase::IgnoreCase)) { return BloodHealing; }
	return INDEX_NONE;
}

const TCHAR* ResultName(EResult Result)
{
	switch (Result)
	{
	case EResult::Accepted:             return TEXT("accepted");
	case EResult::RefusedIndex:         return TEXT("no such discipline");
	case EResult::RefusedNoCharacter:   return TEXT("no character");
	case EResult::RefusedDead:          return TEXT("the caster is not active");
	case EResult::RefusedNoRules:       return TEXT("no rulebook");
	case EResult::RefusedNotLearned:    return TEXT("not learned");
	case EResult::RefusedWorldArea:     return TEXT("refused by the world area");
	case EResult::RefusedPredependency: return TEXT("a predependency read false");
	case EResult::RefusedNoRecord:      return TEXT("no DisciplineTgt record");
	case EResult::RefusedBlood:         return TEXT("not enough blood");
	case EResult::RefusedNoTargets:     return TEXT("no legal target");
	case EResult::RefusedRecovering:    return TEXT("still recovering");
	default:                            return TEXT("?");
	}
}

int32 ActiveRank(const FElysiumCombatCharacter& Char, int32 Index)
{
	if (Index < 0 || Index >= Count)
	{
		return 0;
	}
	return Char.Sheet.GetCurrent(EC::ActiveDisciplines, Index);
}

const FName& ExpiryInput()
{
	// A project-owned input rather than a recovered datamap name: the owned event has to be
	// delivered by name through the one queue, and no recovered CBaseCombatCharacter input carries
	// a Discipline expiry. It is registered on the chain like any other so the queue, the inspector
	// and the save all see one shape (the `ElysiumWeaponCommit` precedent).
	static const FName Name(TEXT("ElysiumDisciplineExpire"));
	return Name;
}

// --- The native active-state path ---

namespace
{
	// The activation's own trait-effect groups: the `Action` rows without `Triggers Inc` whose
	// `Value` predicate admits this level and whose `Predependency` holds.
	void InstallActiveGroups(FElysiumCombatCharacter& Char, const FElysiumStat& Active,
		int32 Index, int32 Level)
	{
		FElysiumDisciplineState& State = Char.Disciplines;
		for (const FElysiumStat::FAction& Action : Active.Actions)
		{
			if (Action.bOnIncrement || Action.Effect.IsEmpty() || !Action.Admits(Level))
			{
				continue;
			}
			if (!EvalActionPredependency(Action.Predependency, Char.Sheet))
			{
				continue;
			}
			if (!State.Groups[Index].Contains(Action.Effect))
			{
				State.Groups[Index].Add(Action.Effect);
				Char.Effects.Add(Action.Effect);
			}
		}
	}

	EElysiumTraitContainer DisciplineContainer() { return EC::Disciplines; }
}

static EResult ActivateNative(FElysiumCombatCharacter& Char, int32 Index, int32 Level)
{
	const FRules Rules = ResolveRules(Char);
	const FElysiumStat* Active = Rules.Active(Index);
	if (Active == nullptr)
	{
		UE_LOG(LogElysiumPlayer, Warning,
			TEXT("%s cannot activate %s: no Active_* stat block is loaded"),
			*Char.DebugString(), GDisciplineNames[Index]);
		return EResult::RefusedNoRules;
	}

	// 1. The authored increment gates. `ObfuscateCanInc == 1` names no trait, so the shared
	//    evaluator opens on it — the stated posture for a side it cannot read, and the one native
	//    rule state this path does not carry.
	for (const FString& Expr : Active->IncPredependency)
	{
		if (!ElysiumSheetRules::EvalPredependency(Expr, Char.Sheet))
		{
			UE_LOG(LogElysiumPlayer, Log, TEXT("%s refused %s: `%s`"),
				*Char.DebugString(), GDisciplineNames[Index], *Expr);
			return EResult::RefusedPredependency;
		}
	}

	FElysiumDisciplineState& State = Char.Disciplines;
	const bool bRenewing = State.IsActive(Index);
	const double Now = Char.World ? Char.World->NowSeconds() : 0.0;

	// 2. The duration: `Initial_N` on a fresh activation, `Add_N` on a renewal, then the character's
	//    `Duration` trait-effect payload on the LEARNED Discipline trait (`"Duration 120%"` on
	//    Fortitude), which is where every shipped modifier is authored.
	const int32 ClampedLevel = FMath::Clamp(Level, 1, 5);
	int32 Seconds = bRenewing
		? Active->Durations.Add[ClampedLevel]
		: Active->Durations.Initial[ClampedLevel];
	if (const FElysiumSheetEffects* Layer = Char.SheetEffects())
	{
		Seconds = Layer->ApplyPayload(EElysiumTraitOp::Duration, DisciplineContainer(), Index, Seconds);
	}

	// 3. The increment actions — the blood payment through the sheet and any named native function.
	//    Each accepted activation runs them, renewal included: they are the `IncBase` path's own
	//    rows, and what renewal must not duplicate is the timed EVENT, not the price.
	for (const FElysiumStat::FAction& Action : Active->Actions)
	{
		if (!Action.bOnIncrement || !Action.Admits(ClampedLevel))
		{
			continue;
		}
		if (!EvalActionPredependency(Action.Predependency, Char.Sheet))
		{
			continue;
		}
		if (!Action.StatMutation.IsEmpty())
		{
			ApplyStatMutation(Char, Action.StatMutation);
		}
		if (!Action.Function.IsEmpty())
		{
			ApplyActionFunction(Char, Action.Function, ClampedLevel);
		}
	}

	// 4. The active slot itself, then the state actions' trait-effect groups over it.
	Char.Sheet.SetBase(EC::ActiveDisciplines, Index, ClampedLevel);
	InstallActiveGroups(Char, *Active, Index, ClampedLevel);
	Char.RebuildEffects();   // resolves the layer AND recomputes the sheet (rules + effects)

	// 5. The expiry: ONE owned event on the one queue, keyed (character, discipline) by the serial.
	//    A renewal mints a new serial and enqueues against the extended deadline, so the event still
	//    pending under the old serial is dropped as stale on delivery — extension, never a stack.
	if (Seconds > 0)
	{
		const double End = bRenewing ? FMath::Max(State.EndTime[Index], Now) + Seconds : Now + Seconds;
		State.EndTime[Index] = End;
		State.ExpirySerial[Index] = ++State.SerialCounter;
		if (Char.World)
		{
			Char.World->EnqueueInput(TEXT("!self"), ExpiryInput(),
				FElysiumVariant::Int(State.ExpirySerial[Index]), FMath::Max(End - Now, 0.0),
				Char.Handle, Char.Handle);
		}
	}
	else
	{
		// An authored duration of zero is an instantaneous state: it applies and ends in the same
		// call rather than sitting active forever with no event to end it.
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s %s authors no duration at level %d — applied and ended immediately"),
			*Char.DebugString(), GDisciplineNames[Index], ClampedLevel);
		EndNative(Char, Index);
		return EResult::Accepted;
	}

	ReportNativeConsumerSeams(Char, Index, ClampedLevel);
	return EResult::Accepted;
}

void EndNative(FElysiumCombatCharacter& Char, int32 Index)
{
	if (Index < 0 || Index >= Count)
	{
		return;
	}
	FElysiumDisciplineState& State = Char.Disciplines;
	bool bChanged = State.EndTime[Index] != 0.0;
	for (const FString& Group : State.Groups[Index])
	{
		bChanged |= Char.Effects.RemoveAll([&Group](const FString& Entry)
			{ return Entry.Equals(Group, ESearchCase::IgnoreCase); }) > 0;
	}
	State.Groups[Index].Reset();
	State.EndTime[Index] = 0.0;
	State.ExpirySerial[Index] = 0;
	if (Char.Sheet.GetBase(EC::ActiveDisciplines, Index) != 0)
	{
		Char.Sheet.SetBase(EC::ActiveDisciplines, Index, 0);
		bChanged = true;
	}
	if (bChanged)
	{
		// The recompute has to see BOTH halves — the rules and the effect layer — which is what
		// RebuildEffects passes and RecomputeCurrent alone would not.
		Char.RebuildEffects();
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s %s ended"),
			*Char.DebugString(), GDisciplineNames[Index]);
	}
}

// --- The targeted `DisciplineTgt` transaction ---

namespace
{
	bool ApplyHit(FElysiumCombatCharacter* Caster, FElysiumCombatCharacter& Target,
		const FElysiumDisciplineTgt& Record, const FElysiumDiscHit& Hit, double Now, int32 Depth,
		bool bTrackOwner = true);

	// Remove one tracked targeted effect: drop the groups it installed, forget the row, recompute.
	void RemoveTargetEffect(FElysiumCombatCharacter& Char, int32 EffectIndex, bool bInterrupted = false)
	{
		FElysiumDisciplineState& State = Char.Disciplines;
		if (!State.TargetEffects.IsValidIndex(EffectIndex))
		{
			return;
		}
		const FElysiumActiveDisciplineEffect Effect = State.TargetEffects[EffectIndex];
		State.TargetEffects.RemoveAt(EffectIndex);
		bool bChanged = false;
		for (const FString& Group : Effect.Effects)
		{
			// ONE occurrence per entry the cast added, never every match: `Char.Effects` is a list
			// with one copy per live effect, and two records may name the same authored group. A
			// remove-all here would strip the group out from under every other effect holding it.
			const int32 At = Char.Effects.IndexOfByPredicate([&Group](const FString& Entry)
				{ return Entry.Equals(Group, ESearchCase::IgnoreCase); });
			if (At != INDEX_NONE)
			{
				Char.Effects.RemoveAt(At);
				bChanged = true;
			}
		}
		if (bChanged)
		{
			Char.RebuildEffects();
		}
		// 0x101dfe80 -> 0x101def10: remove the trait effect, clear the original
		// HitInfo's raw mask, remove ONE comfort entry, then run the schedule tail.
		// Multiple effects sharing a bit do not retain it after the first expiry.
		if (FElysiumNpc* Npc = Char.AsNpc())
		{
			Npc->NpcFlags.Clear(static_cast<EElysiumNpcFlag>(Effect.AiNpcFlag1));
			Npc->NpcFlags.Clear(static_cast<EElysiumNpcFlag2>(Effect.AiNpcFlag2));
		}
		if (Effect.bAddedToComfort) { Char.RemoveFromComfortList(); }
		if (Effect.bHadAiSchedule)
		{
			if (FElysiumNpc* Npc = Char.AsNpc()) { Npc->EndDisciplineSchedule(); }
		}
		// MiscFlag is deliberately persistent: 0x101def10 has no +0xa4 clear.
		// End/interrupt are direct HitInfo calls, not new HitGroups with new timers.
		{
			const FRules Rules = ResolveRules(Char);
			const FElysiumDisciplineTgt* Record = Rules.Targets ? Rules.Targets->Find(Effect.Record) : nullptr;
			FElysiumDiscHit Hit;
			if (Record && Record->ResolveHit(Effect.HitTable, Hit))
			{
				const TArray<FElysiumDiscHit>& Callbacks = bInterrupted ? Hit.OnInterrupt : Hit.OnEnd;
				FElysiumEntity* Source = Char.World ? Char.World->Resolve(Effect.Source) : nullptr;
				for (const FElysiumDiscHit& Callback : Callbacks)
				{
					ApplyHit(Source ? Source->AsCombatCharacter() : nullptr, Char, *Record, Callback,
						Char.World ? Char.World->NowSeconds() : 0.0, 0, false);
				}
			}
		}
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s lost %s/%s"),
			*Char.DebugString(), *Effect.Record, *Effect.HitTable);
	}

	// The channels this runtime carries but does not execute. Reported once per (record, hit,
	// channel) at the cast that authors them — the whole point of parsing them is that the report
	// is a work list rather than a silence.
	void ReportCarriedChannels(const FElysiumDisciplineTgt& Record, const FElysiumDiscHit& Hit)
	{
		auto Carry = [&Record, &Hit](const TCHAR* Channel, const FString& Value, const TCHAR* Owner)
		{
			if (Value.IsEmpty())
			{
				return;
			}
			ReportOnce(FString::Printf(TEXT("hit.%s.%s.%s"), *Record.InternalName, *Hit.Name, Channel),
				FString::Printf(TEXT("`%s`/%s authors %s \"%s\" — parsed and carried, not executed (%s)"),
					*Record.InternalName, *Hit.Name, Channel, *Value, Owner));
		};
		Carry(TEXT("Expression"), Hit.Expression, TEXT("the disposition/expression layer"));
		Carry(TEXT("Gesture_Anim"), Hit.GestureAnim, TEXT("the gesture layer has no producer"));
		Carry(TEXT("Player_Anim"), Hit.PlayerAnim,
			TEXT("the compact player-action resolver is unbuilt"));
		if (Hit.FlinchPercent != INDEX_NONE || Hit.KnockbackPercent != INDEX_NONE)
		{
			ReportOnce(FString::Printf(TEXT("hit.%s.%s.impulse"), *Record.InternalName, *Hit.Name),
				FString::Printf(TEXT("`%s`/%s authors flinch/knockback — parsed and carried; the "
					"reaction and impulse producers are the combat animation cycle's"),
					*Record.InternalName, *Hit.Name));
		}
		if (Hit.bDoFrenzy || Hit.bDoPossession || Hit.bGibOnDeath || Hit.bClearCopyProp)
		{
			ReportOnce(FString::Printf(TEXT("hit.%s.%s.flags"), *Record.InternalName, *Hit.Name),
				FString::Printf(TEXT("`%s`/%s authors DoFrenzy/DoPossession/GibOnDeath/"
					"Clear_Copy_Prop — parsed and carried; the frenzy family stays "
					"pending and the rest have no consumer"), *Record.InternalName, *Hit.Name));
		}
		if (Record.bHasProjectile)
		{
			ReportOnce(FString::Printf(TEXT("hit.%s.projectile"), *Record.InternalName),
				FString::Printf(TEXT("`%s` authors a Projectile — parsed and carried; this cast "
					"applies its payload immediately rather than on impact, so the projectile's "
					"flight time is not observed"), *Record.InternalName));
		}
	}

	// --- The `HitInfo` AI-schedule channel ---
	// The cast "executes `HitInfo` as
	// independent channels — ... AI schedule assignment (§5.5 kernel) ...". §5.5.4's kernel owns
	// what a schedule IS; this channel only names one, so it goes through the one door that already
	// turns a name into a running program (`FElysiumNpc::StartNamedSchedule`) rather than reaching
	// into the schedule runner itself. Interrupts, the fail schedule and the motor work are then
	// the named program's own — the recovered point of a policy-level schedule command.
	//
	// Returns whether a program started. A name no registered program carries FAILS BY NAME through
	// the stub funnel inside that door; the Berserk/Possession families the shipped records name are
	// the expected occupants of that work list, and starting something else under an authored name
	// would be behaviour invented out of a string.
	bool ApplyAiSchedule(FElysiumCombatCharacter& Target, const FElysiumDisciplineTgt& Record,
		const FElysiumDiscHit& Hit)
	{
		FElysiumNpc* Npc = Target.AsNpc();
		if (Npc == nullptr)
		{
			// The channel landed on something with no schedule kernel — the player, or a bare
			// combat character. Reported once per (record, hit): an authored record aiming a
			// schedule at the caster is an authoring fact worth reading back, not a per-cast log.
			ReportOnce(FString::Printf(TEXT("hit.%s.%s.aisched.nonnpc"),
					*Record.InternalName, *Hit.Name),
				FString::Printf(TEXT("`%s`/%s assigns AI schedule \"%s\", but this target runs no "
					"schedule kernel — only an NPC does"),
					*Record.InternalName, *Hit.Name, *Hit.AiSchedule));
			return false;
		}
		TGuardValue<bool> ScheduleGuard(GApplyingDisciplineSchedule, true);
		// `0x101de660`'s `AI_Schedule` arm: slot 614 on the target's Troika pointer FIRST, then
		// `SetSchedule(byName)` only if it is still alive. The re-base happens whether or not the
		// install then takes.
		Npc->ResetThinkTimers(Npc->World ? Npc->World->NowSeconds() : 0.0);
		return Npc->StartNamedSchedule(Hit.AiSchedule,
			FString::Printf(TEXT("DisciplineTgt.%s/%s.AI_Schedule(%s)"),
				*Record.InternalName, *Hit.Name, *Hit.AiSchedule),
			FString::Printf(TEXT("record=%s hit=%s"), *Record.InternalName, *Hit.Name));
	}

	// `Trigger_Casting` — a nested cast into another record. Only a helper record (zero blood, zero
	// recovery) is executed, and only its implementable channels: a nested cast is an effect-graph
	// node, not a second purchase, so it must not re-enter the paying transaction.
	void RunTriggerCasting(FElysiumCombatCharacter& Caster, FElysiumCombatCharacter& Target,
		const FElysiumDisciplineTgt& Record, const FElysiumDiscHit& Hit, double Now, int32 Depth)
	{
		if (Depth >= 3)
		{
			ReportOnce(FString::Printf(TEXT("cast.depth.%s"), *Record.InternalName),
				FString::Printf(TEXT("`%s` nests Trigger_Casting more than three deep — the chain "
					"is cut"), *Record.InternalName));
			return;
		}
		const FRules Rules = ResolveRules(Caster);
		if (Rules.Targets == nullptr)
		{
			return;
		}
		for (const FElysiumDiscTriggerCast& Cast : Hit.TriggerCasting)
		{
			const FElysiumDisciplineTgt* Nested = Rules.Targets->Find(Cast.DisciplineFx);
			if (Nested == nullptr)
			{
				ReportOnce(FString::Printf(TEXT("cast.missing.%s"), *Cast.DisciplineFx),
					FString::Printf(TEXT("`%s` triggers `%s`, which no DisciplineTgt file holds"),
						*Record.InternalName, *Cast.DisciplineFx));
				continue;
			}
			if (!Nested->IsHelper())
			{
				// **SEAM** — a nested cast into a record that costs blood would be a second
				// purchase inside one transaction, which the recovered eight steps do not describe.
				ReportOnce(FString::Printf(TEXT("cast.paid.%s"), *Cast.DisciplineFx),
					FString::Printf(TEXT("`%s` triggers the paid record `%s` — not executed; the "
						"recovered transaction pays once and nests only zero-cost helpers"),
						*Record.InternalName, *Cast.DisciplineFx));
				continue;
			}
			// `Affects` picks which half of the pair the helper's payload lands on.
			FElysiumCombatCharacter& Receiver =
				Cast.Affects.Equals(TEXT("Self"), ESearchCase::IgnoreCase) ? Caster : Target;
			// The helper's own hit tables are resolved against the receiver with the ordinary walk.
			TMap<int32, int32> Counts;
			FCandidate Candidate;
			Candidate.Char = &Receiver;
			Candidate.bIsSelf = (&Receiver == &Caster);
			Candidate.bIsPrimary = true;
			Candidate.bIsPlayer = Caster.World && Caster.World->PlayerHandle().IsSet()
				&& Receiver.Handle.Index == Caster.World->PlayerHandle().Index;
			ResolveCandidate(Candidate, Rules);
			const FString HitTable = ResolveHitTable(*Nested, Candidate, Counts);
			FElysiumDiscHit NestedHit;
			if (!HitTable.IsEmpty() && Nested->ResolveHit(HitTable, NestedHit))
			{
				ApplyHit(&Caster, Receiver, *Nested, NestedHit, Now, Depth + 1);
			}
		}
	}

	bool ApplyHit(FElysiumCombatCharacter* Caster, FElysiumCombatCharacter& Target,
		const FElysiumDisciplineTgt& Record, const FElysiumDiscHit& Hit, double Now, int32 Depth,
		bool bTrackOwner)
	{
		// `Chance_Effective` gates the whole payload: the target was hit, and the hit did nothing.
		if (Hit.ChanceEffectivePercent < 100 && RollPercent() > Hit.ChanceEffectivePercent)
		{
			UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s: %s/%s rolled ineffective"),
				*Target.DebugString(), *Record.InternalName, *Hit.Name);
			return false;
		}
		ReportCarriedChannels(Record, Hit);
		// 0x101dfc20 emits a target BULLET_IMPACT sound independently of source
		// activation. Direct HitInfo callbacks do not repeat the HitGroup prelude.
		if (bTrackOwner && Record.bTriggerAISound && Target.World)
		{
			Target.World->EmitGameSound(Target.Origin, ElysiumGameSounds::DisciplineAlert(),
				-1.f, Target.Handle, 0.f, ElysiumGameSounds::BulletImpact, 0.2);
		}

		bool bCommitted = false;
		FElysiumActiveDisciplineEffect Effect;
		Effect.Record = Record.InternalName;
		Effect.HitTable = Hit.Name;
		Effect.Source = Caster ? Caster->Handle : FElysiumEntityHandle();
		Effect.bRemoveOnTakeDamage = Record.bRemoveOnTakeDamage;
		Effect.bRemoveOnHearCombat = Record.bRemoveOnHearCombat;
		Effect.bRemoveOnWasBumped = Record.bRemoveOnWasBumped;
		Effect.bHadAiSchedule = !Hit.AiSchedule.IsEmpty();
		// 0x101de660 writes these before damage and before installing AI_Schedule.
		uint32 MiscMask = 0;
		if (!Hit.MiscFlag.IsEmpty() && !ElysiumMiscFlags::ParseName(Hit.MiscFlag, MiscMask))
		{
			ReportOnce(TEXT("miscflag.") + Hit.MiscFlag,
				FString::Printf(TEXT("unknown MiscFlag '%s' in %s/%s; resolver returns zero"),
					*Hit.MiscFlag, *Record.InternalName, *Hit.Name));
		}
		MiscMask |= Hit.InheritedMiscFlags;
		ElysiumMiscFlags::Set(Target.MiscFlags, MiscMask);
		bCommitted |= MiscMask != 0;
		if (!Hit.AiNpcFlag.IsEmpty())
		{
			EElysiumNpcFlag Flag1;
			EElysiumNpcFlag2 Flag2;
			if (!FElysiumNpcFlags::ParseName(Hit.AiNpcFlag, Flag1, Flag2))
			{
				ReportOnce(TEXT("npcflag.") + Hit.AiNpcFlag,
					FString::Printf(TEXT("unknown AI_NPCFlag '%s' in %s/%s"),
						*Hit.AiNpcFlag, *Record.InternalName, *Hit.Name));
			}
			else if (FElysiumNpc* Npc = Target.AsNpc())
			{
				Effect.AiNpcFlag1 = static_cast<uint32>(Flag1);
				// The resolver's sign bit is also ORed into the retail word, and the
				// expiry clears it along with the named bit (0x101de6e1/0x101def10).
				Effect.AiNpcFlag2 = Flag2 == EElysiumNpcFlag2::None ? 0
					: static_cast<uint32>(Flag2) | 0x80000000u;
				Npc->NpcFlags.Set(Flag1);
				Npc->NpcFlags.Set(static_cast<EElysiumNpcFlag2>(Effect.AiNpcFlag2));
				bCommitted = true;
			}
		}

		// --- Health buffer (the Bloodshield join, §5.1/§5.3) ------------------------------------
		if (Hit.HealthBuffer.bAuthored)
		{
			const int32 Amount = ResolveAmount(Hit.HealthBuffer);
			// `-1` is the authored clear (`OnEnd { "Health_Buffer" "-1" }`).
			Target.Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer,
				Amount < 0 ? 0 : Amount);
			Target.RecomputeSheet();
			bCommitted = true;
			if (Hit.HealthBufferBlockPercent != INDEX_NONE)
			{
				// **SEAM** — `Health_Buffer_Block_Percent` says what FRACTION of an incoming hit the
				// buffer absorbs; the one typed health commit absorbs the whole hit until the buffer
				// is exhausted, which is the recovered commit order. Splitting it needs the retail
				// split point, which is not recovered.
				ReportOnce(FString::Printf(TEXT("hit.%s.blockpct"), *Record.InternalName),
					FString::Printf(TEXT("`%s` authors Health_Buffer_Block_Percent %d%% — parsed and "
						"carried; CommitDamage absorbs the whole hit until the buffer is exhausted, "
						"which is the recovered commit order"),
						*Record.InternalName, Hit.HealthBufferBlockPercent));
			}
		}

		// --- Blood ------------------------------------------------------------------------------
		if (Hit.HealBlood.bAuthored)
		{
			Target.AddBlood(ResolveAmount(Hit.HealBlood));
			bCommitted = true;
		}
		if (Hit.BloodSuck.bAuthored)
		{
			// **CHOSEN** — the one authored value is `"100"` with no `%`, and the BloodPool's
			// authored Max is 15, so a flat hundred cannot be what it means. It is read as a
			// percentage of the target's current pool, moved to the caster.
			const int32 Pool = Target.BloodPoolValue();
			const int32 Stolen = FMath::Clamp((Pool * ResolveAmount(Hit.BloodSuck)) / 100, 0, Pool);
			if (Stolen > 0)
			{
				Target.AddBlood(-Stolen);
				if (Caster) { Caster->AddBlood(Stolen); }
				bCommitted = true;
			}
		}

		// --- Health -----------------------------------------------------------------------------
		if (Hit.DmgHealth.bAuthored)
		{
			const int32 Raw = ResolveAmount(Hit.DmgHealth);
			const int32 Ceiling = Target.Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
			const int32 Amount = Hit.DmgHealth.bPercent ? (Ceiling * Raw) / 100 : Raw;
			if (Amount > 0)
			{
				// **CHOSEN** — `Dmg_Health` authors a magnitude and neither a damage family nor a
				// `DMG_*` mask, and it is stated as a fraction of the victim's health rather than as
				// a dice pool. The descriptor is therefore direct-input Lethal with a forced soak of
				// zero: the authored number is the health delta, so the resolver has no pool to roll
				// and no soak to subtract. It still travels the one typed path, so the commit,
				// the buffer, the aggravated tracking, `OnDamaged` and the death test are the shared
				// ones.
				FElysiumDmg Dmg;
				Dmg.Family = EElysiumDmgFamily::Lethal;
				Dmg.Flags = ElysiumDamage::FlagDirectInput;
				Dmg.ExtraInput = Amount;
				Dmg.ForcedSoak = 0;
				Dmg.Source = Caster ? Caster->Handle : FElysiumEntityHandle();
				Target.TakeDamage(Dmg, Caster);
				bCommitted = true;
			}
		}

		// --- Trait-effect groups and their duration ----------------------------------------------
		if (Hit.AddToComfort != INDEX_NONE && Hit.AddToComfort != 0)
		{
			Target.AddToComfortList();
			Effect.bAddedToComfort = true;
			bCommitted = true;
		}
		if (!Hit.TraitEffects.IsEmpty())
		{
			for (const FString& Group : Hit.TraitEffects)
			{
				Effect.Effects.Add(Group);
				Target.Effects.Add(Group);
			}
			Target.RebuildEffects();
			bCommitted = true;
		}
		// Track flag/schedule/comfort-only HitGroups too. Retail AddDiscFlag is called
		// for the HitGroup, irrespective of whether it has a TraitEffect channel.
		if (bTrackOwner && (!Effect.Effects.IsEmpty() || Effect.AiNpcFlag1 != 0
			|| Effect.AiNpcFlag2 != 0 || MiscMask != 0 || Effect.bAddedToComfort
			|| Effect.bHadAiSchedule || !Hit.OnEnd.IsEmpty() || !Hit.OnInterrupt.IsEmpty()))
		{
			const int32 Seconds = Hit.Duration.bAuthored ? ResolveAmount(Hit.Duration) : 0;
			if (Seconds > 0)
			{
				Effect.EndTime = Now + Seconds;
				Effect.Serial = ++Target.Disciplines.SerialCounter;
				if (Target.World)
				{
					Target.World->EnqueueInput(TEXT("!self"), ExpiryInput(),
						FElysiumVariant::Int(Effect.Serial), Seconds, Effect.Source, Target.Handle);
				}
			}
			else
			{
				// `"-1"` is the authored infinite duration; an unauthored one is the same shape —
				// the effect stands until teardown or an interruption flag ends it.
				Effect.EndTime = -1.0;
			}
			// The hear-combat flag is polled from the owner's think, so the owner needs one due.
			// Nothing else re-arms it: `Think` clears `NextThink` before running, and the poll
			// below re-arms while a listener remains.
			if (Effect.bRemoveOnHearCombat)
			{
				Target.NextThink = FMath::Min(Target.NextThink, static_cast<float>(Now));
			}
			Target.Disciplines.TargetEffects.Add(MoveTemp(Effect));
			bCommitted = true;
		}

		// --- The AI schedule channel ---
		// Last of the independent channels, and deliberately after the sheet ones: the program the
		// victim starts runs against the state this hit has already committed (a `Dmg_Health` that
		// killed it leaves an inert NPC, which the door refuses), not against the state before it.
		// It counts as a commit in its own right — a record whose whole payload is a schedule
		// assignment did something to its target.
		if (!Hit.AiSchedule.IsEmpty() && ApplyAiSchedule(Target, Record, Hit))
		{
			bCommitted = true;
		}

		if (Caster) { RunTriggerCasting(*Caster, Target, Record, Hit, Now, Depth); }
		return bCommitted;
	}
}

// --- The shared authority ---

namespace
{
	EResult CastTargeted(FElysiumCombatCharacter& Char, int32 Index, int32 Tier)
	{
		const FRules Rules = ResolveRules(Char);
		if (Rules.Targets == nullptr)
		{
			UE_LOG(LogElysiumPlayer, Warning, TEXT("%s cannot cast %s: no DisciplineTgt table"),
				*Char.DebugString(), GDisciplineNames[Index]);
			return EResult::RefusedNoRules;
		}
		// 1. The record for (compiled Discipline, selected tier).
		const FElysiumDisciplineTgt* Record =
			Rules.Targets->FindFor(GDisciplineNames[Index], Tier);
		if (Record == nullptr)
		{
			UE_LOG(LogElysiumPlayer, Warning,
				TEXT("%s cannot cast %s tier %d: no DisciplineTgt record"),
				*Char.DebugString(), GDisciplineNames[Index], Tier);
			return EResult::RefusedNoRecord;
		}
		const double Now = Char.World ? Char.World->NowSeconds() : 0.0;
		FElysiumDisciplineState& State = Char.Disciplines;
		if (const double* Until = State.Recovery.Find(Record->InternalName))
		{
			if (Now < *Until)
			{
				UE_LOG(LogElysiumPlayer, Log, TEXT("%s: %s is still recovering (%.2fs left)"),
					*Char.DebugString(), *Record->InternalName, *Until - Now);
				return EResult::RefusedRecovering;
			}
		}

		// 2. The adjusted blood cost and the learned rank. The `BloodCost` trait-effect payload is
		//    authored on the LEARNED Discipline trait (`"BloodCost 2"` on Thaumaturgy).
		int32 Cost = Record->BloodCost;
		if (const FElysiumSheetEffects* Layer = Char.SheetEffects())
		{
			Cost = Layer->ApplyPayload(EElysiumTraitOp::BloodCost, EC::Disciplines, Index, Cost);
		}
		Cost = FMath::Max(Cost, 0);
		if (Char.Sheet.GetCurrent(EC::Disciplines, Index) < Tier)
		{
			UE_LOG(LogElysiumPlayer, Log, TEXT("%s cannot cast %s tier %d: rank %d"),
				*Char.DebugString(), GDisciplineNames[Index], Tier,
				Char.Sheet.GetCurrent(EC::Disciplines, Index));
			return EResult::RefusedNotLearned;
		}
		if (Char.BloodPoolValue() < Cost)
		{
			UE_LOG(LogElysiumPlayer, Log, TEXT("%s cannot cast %s: %d blood, needs %d"),
				*Char.DebugString(), *Record->InternalName, Char.BloodPoolValue(), Cost);
			return EResult::RefusedBlood;
		}

		// 3. The legal target set.
		TArray<FCandidate> Targets;
		BuildTargetSet(Char, *Record, Rules, Targets);

		// 4. Abort with feedback before anything is spent.
		if (Targets.IsEmpty())
		{
			UE_LOG(LogElysiumPlayer, Log, TEXT("%s: %s found no legal target"),
				*Char.DebugString(), *Record->InternalName);
			return EResult::RefusedNoTargets;
		}

		// 5. Deduct once.
		if (Cost > 0)
		{
			Char.AddBlood(-Cost);
		}

		// 6. Source-side effects and the cast counter. The source instant/hit blocks are particles
		//    and sounds only in the shipped corpus, so the executed half is the AI-sound
		//    classification, which is a real producer on the substrate sound bus (§2.5.3).
		FElysiumPlayer* Player = Char.World ? Char.World->FindPlayer() : nullptr;
		const double* SourceEnd = State.SourceActivationEnd.Find(Record->InternalName);
		if ((!SourceEnd || *SourceEnd < Now) && !Char.HasDisciplineStatus(Record->InternalName))
		{
			// 0x101e3560 before the per-target loop: source status, Fired_Gun misc
			// bit, then COMBAT. DAT_1072bc40/bcb0 are the gunshot table globals.
			State.SourceActivationEnd.Add(Record->InternalName, Now + 0.1);
			ElysiumMiscFlags::Set(Char.MiscFlags, 0x200000u);
			if (Record->bTriggerAISound && Char.World)
			{
				Char.World->EmitGameSound(Char.Origin, ElysiumGameSounds::Gunshot(), -1.f,
					Char.Handle, 0.f, ElysiumGameSounds::Combat, 0.2);
			}
		}
		const bool bPlayerCaster = (Player == &Char);
		if (bPlayerCaster)
		{
			++Player->DisciplineCastCount;
		}

		// 7. The hit mapping, per target, in stable entity order.
		TMap<int32, int32> Counts;
		int32 Committed = 0;
		for (const FCandidate& Candidate : Targets)
		{
			const FString HitTable = ResolveHitTable(*Record, Candidate, Counts);
			if (HitTable.IsEmpty())
			{
				continue;
			}
			FElysiumDiscHit Hit;
			if (!Record->ResolveHit(HitTable, Hit))
			{
				// An authored mapping naming a table the record does not hold: a counted defect,
				// preserved rather than repaired.
				ReportOnce(FString::Printf(TEXT("hit.missing.%s.%s"), *Record->InternalName, *HitTable),
					FString::Printf(TEXT("`%s` maps to `%s`, which the record does not define"),
						*Record->InternalName, *HitTable));
				continue;
			}
			if (ApplyHit(&Char, *Candidate.Char, *Record, Hit, Now, /*Depth*/ 0))
			{
				++Committed;
			}
		}

		// The law production. `docs/vtmb/disciplines.md` § "Overt use, AI sound and the Masquerade":
		// the successful targeted commit raises the caster's player owner's supernatural activity to
		// the record's `SupernaturalLvl`, and criminal activity to 3 when the record's independent
		// `Overt` byte is set. Whether an NPC witnesses it is not this commit's question.
		if (Committed > 0 && bPlayerCaster)
		{
			// Both writes go through the player setters, which is what makes them raise-never-lower,
			// refresh the deadline, and increment the channel's act count. Both pass `DeriveDuration`
			// (-1), the finite-duration sentinel every recovered non-native caller passes: the
			// deadline becomes `max(previously retained level, pl_min_act_timer)` rather than
			// indefinite. The expiry pass in the player think ages them out.
			ElysiumLaw::SetSupernaturalLevel(*Player, Record->SupernaturalLvl);
			if (Record->bOvert)
			{
				ElysiumLaw::SetCriminalLevel(*Player, 3);
			}
		}

		// 8. The recovery state.
		if (Record->RecoveryTime > 0.f)
		{
			State.Recovery.Add(Record->InternalName, Now + Record->RecoveryTime);
		}

		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s cast %s (tier %d): %d blood, %d/%d target(s) committed"),
			*Char.DebugString(), *Record->InternalName, Tier, Cost, Committed, Targets.Num());
		return EResult::Accepted;
	}
}

EResult Use(FElysiumCombatCharacter& Char, int32 Index, int32 Tier)
{
	if (Index < 0 || Index >= Count)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s: discipline index %d is out of range"),
			*Char.DebugString(), Index);
		return EResult::RefusedIndex;
	}
	if (Char.IsInert())
	{
		return EResult::RefusedDead;
	}
	// The eligibility virtual runs BEFORE either execution family, so a targeted power cannot
	// bypass the restriction by deferring target selection.
	if (!PassesWorldAreaGate(Char, Index))
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s refused %s: world area"),
			*Char.DebugString(), GDisciplineNames[Index]);
		return EResult::RefusedWorldArea;
	}

	const int32 Learned = Char.Sheet.GetCurrent(EC::Disciplines, Index);
	if (Learned < 1)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s has not learned %s"),
			*Char.DebugString(), GDisciplineNames[Index]);
		return EResult::RefusedNotLearned;
	}
	const int32 UseTier = FMath::Clamp(Tier <= 0 ? Learned : Tier, 1, FMath::Max(Learned, 1));

	const FRules Rules = ResolveRules(Char);
	const FElysiumStat* Learn = Rules.Learned(Index);
	if (Learn == nullptr)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("%s cannot use %s: no stats.txt block"),
			*Char.DebugString(), GDisciplineNames[Index]);
		return EResult::RefusedNoRules;
	}
	// The one authoritative split: `Is_Instant == 0` enters the targeted manager, `== 1` the
	// native active-stat path. The name is misleading and is not turned into a design rule.
	return Learn->bIsInstant
		? ActivateNative(Char, Index, UseTier)
		: CastTargeted(Char, Index, UseTier);
}

int32 TierFor(const FElysiumPlayer& Player, int32 Index)
{
	if (Player.SelectedTier > 0)
	{
		return Player.SelectedTier;
	}
	return Player.Sheet.GetCurrent(EC::Disciplines, Index);
}

EResult Select(FElysiumPlayer& Player, int32 Index)
{
	if (Index < 0 || Index >= Count)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("vdiscipline_int %d — out of range"), Index);
		return EResult::RefusedIndex;
	}
	// It rejects an index whose current learned value is below one, and the selection does not move.
	if (Player.Sheet.GetCurrent(EC::Disciplines, Index) < 1)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("vdiscipline_int %s — not learned"),
			GDisciplineNames[Index]);
		return EResult::RefusedNotLearned;
	}
	// A newly selected index resets the remembered tier to zero; reselecting the same index
	// preserves it.
	if (Player.SelectedDiscipline != Index)
	{
		Player.SelectedDiscipline = Index;
		Player.SelectedTier = 0;
	}
	return Use(Player, Index, TierFor(Player, Index));
}

EResult UseLast(FElysiumPlayer& Player)
{
	if (Player.SelectedDiscipline == INDEX_NONE)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("vdiscipline_last — nothing is selected"));
		return EResult::RefusedIndex;
	}
	// Only the last step, with the remembered pair: no second cast implementation.
	return Use(Player, Player.SelectedDiscipline, TierFor(Player, Player.SelectedDiscipline));
}

// --- Teardown, expiry and interruption ---

void ClearAll(FElysiumCombatCharacter& Char)
{
	FElysiumDisciplineState& State = Char.Disciplines;
	for (int32 i = 0; i < Count; ++i)
	{
		EndNative(Char, i);
	}
	// Every tracked targeted effect, removed through the one removal path so the trait groups go
	// with it rather than the particle alone.
	for (int32 i = State.TargetEffects.Num() - 1; i >= 0; --i)
	{
		RemoveTargetEffect(Char, i);
	}
	State.Recovery.Reset();
	State.SourceActivationEnd.Reset();
	// Retire every pending expiry input in one step: a delivery whose serial is not the live one is
	// dropped, and after this nothing on this character holds a live serial.
	++State.SerialCounter;
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s: every active discipline cleared"), *Char.DebugString());
}

void CommitExpiry(FElysiumCombatCharacter& Char, int32 Serial)
{
	FElysiumDisciplineState& State = Char.Disciplines;
	for (int32 i = 0; i < Count; ++i)
	{
		if (State.ExpirySerial[i] == Serial && Serial != 0)
		{
			EndNative(Char, i);
			return;
		}
	}
	for (int32 i = 0; i < State.TargetEffects.Num(); ++i)
	{
		if (State.TargetEffects[i].Serial == Serial && Serial != 0)
		{
			RemoveTargetEffect(Char, i);
			return;
		}
	}
	// A renewal minted a newer serial, or teardown ran first. Dropping it is the point of the
	// guard, not a failure.
	UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s dropped a stale discipline expiry #%d"),
		*Char.DebugString(), Serial);
}

namespace
{
	// One interruption sweep. `Predicate` says whether a tracked effect answers this reason.
	void Interrupt(FElysiumCombatCharacter& Char, const TCHAR* Reason,
		TFunctionRef<bool(const FElysiumActiveDisciplineEffect&)> Predicate)
	{
		FElysiumDisciplineState& State = Char.Disciplines;
		for (int32 i = State.TargetEffects.Num() - 1; i >= 0; --i)
		{
			if (!Predicate(State.TargetEffects[i]))
			{
				continue;
			}
			UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s: %s interrupted by %s"),
				*Char.DebugString(), *State.TargetEffects[i].Record, Reason);
			RemoveTargetEffect(Char, i, true);
		}
	}
}

void NotifyDamaged(FElysiumCombatCharacter& Char)
{
	Interrupt(Char, TEXT("damage"), [](const FElysiumActiveDisciplineEffect& Effect)
		{ return Effect.bRemoveOnTakeDamage; });

	// The Bloodshield join: the one typed health commit ends the power when the buffer exhausts
	// (`FElysiumCombatCharacter::EndBloodshield`), which removes its trait group but knows nothing
	// about this tracking. Reconcile by dropping any tracked row whose groups are all gone.
	FElysiumDisciplineState& State = Char.Disciplines;
	for (int32 i = State.TargetEffects.Num() - 1; i >= 0; --i)
	{
		const FElysiumActiveDisciplineEffect& Effect = State.TargetEffects[i];
		if (Effect.Effects.IsEmpty())
		{
			continue;
		}
		const bool bAnyLive = Effect.Effects.ContainsByPredicate([&Char](const FString& Group)
		{
			return Char.Effects.ContainsByPredicate([&Group](const FString& Entry)
				{ return Entry.Equals(Group, ESearchCase::IgnoreCase); });
		});
		if (!bAnyLive)
		{
			RemoveTargetEffect(Char, i);
		}
	}
}

void PollHeardCombat(FElysiumCombatCharacter& Char, double Now)
{
	if (Char.World == nullptr)
	{
		return;
	}
	FElysiumDisciplineState& State = Char.Disciplines;
	const bool bAnyListener = State.TargetEffects.ContainsByPredicate(
		[](const FElysiumActiveDisciplineEffect& Effect) { return Effect.bRemoveOnHearCombat; });
	const FElysiumGameSoundBus& Bus = Char.World->GameSounds();
	if (!bAnyListener)
	{
		State.SoundCursor = Bus.LastSerial();   // stay current so a later effect sees no backlog
		return;
	}
	bool bHeard = false;
	for (const FElysiumGameSoundEvent& Event : Bus.EventsSince(State.SoundCursor))
	{
		State.SoundCursor = FMath::Max(State.SoundCursor, Event.Serial);
		if (Event.Source.IsSet() && Event.Source.Index == Char.Handle.Index)
		{
			continue;   // a character does not interrupt itself
		}
		// The combat categories this runtime's producers emit: a shot, a body taking damage, and
		// the discipline alert this domain raises itself.
		if (Event.Category != ElysiumGameSounds::Gunshot()
			&& Event.Category != ElysiumGameSounds::NpcTakeDamage()
			&& Event.Category != ElysiumGameSounds::DisciplineAlert())
		{
			continue;
		}
		if (FVector::Dist(Event.Position, Char.Origin) <= Event.RadiusCm)
		{
			bHeard = true;
		}
	}
	if (bHeard)
	{
		Interrupt(Char, TEXT("heard combat"), [](const FElysiumActiveDisciplineEffect& Effect)
			{ return Effect.bRemoveOnHearCombat; });
	}
	// Re-arm while a listener is still tracked. `FMath::Min` so the feed transaction's own, earlier
	// deadline is never pushed out by this.
	if (State.TargetEffects.ContainsByPredicate(
		[](const FElysiumActiveDisciplineEffect& Effect) { return Effect.bRemoveOnHearCombat; }))
	{
		Char.NextThink = FMath::Min(Char.NextThink, static_cast<float>(Now));
	}
}

void NotifyBumped(FElysiumCombatCharacter& Char)
{
	Interrupt(Char, TEXT("bump"), [](const FElysiumActiveDisciplineEffect& Effect)
		{ return Effect.bRemoveOnWasBumped; });
}

void NotifyScheduleChanged(FElysiumCombatCharacter& Char)
{
	if (!GApplyingDisciplineSchedule)
	{
		Interrupt(Char, TEXT("schedule change"), [](const FElysiumActiveDisciplineEffect&) { return true; });
	}
}

// --- The declared verbs ---

namespace
{
	void ReportVerb(const TCHAR* Verb, EResult Result)
	{
		UE_LOG(LogElysiumPlayer, Display, TEXT("%s: %s"), Verb, ResultName(Result));
	}
}

void ExecuteSelect(FElysiumEntityWorld& World, const FString& Args)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (Player == nullptr)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("vdiscipline_int: no player entity in this world"));
		return;
	}
	// The client sends the compiled index; the name is accepted too, because the console is a
	// human surface and the two spellings resolve through one lookup.
	const int32 Index = IndexFromName(Args.TrimStartAndEnd());
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("vdiscipline_int: `%s` is not a discipline"), *Args);
		return;
	}
	ReportVerb(TEXT("vdiscipline_int"), Select(*Player, Index));
}

void ExecuteLast(FElysiumEntityWorld& World)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (Player == nullptr)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("vdiscipline_last: no player entity in this world"));
		return;
	}
	ReportVerb(TEXT("vdiscipline_last"), UseLast(*Player));
}

void ExecuteEndAll(FElysiumEntityWorld& World)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (Player == nullptr)
	{
		UE_LOG(LogElysiumPlayer, Warning, TEXT("vdiscipline_endall: no player entity in this world"));
		return;
	}
	ClearAll(*Player);
}

}   // namespace ElysiumDisciplines

// --- The block's one field list ---
// Persistent state is a declared save block with one
// owner. This domain's state has two HOMES (the player record, because the player entity is
// excluded from the map snapshot; the NPC leaf, because a targeted effect lands on whichever
// character the cast hit) and exactly one field list, which is this function. Both call sites gate
// it on their own schema version and re-zero `SoundCursor`, which is session state.
void FElysiumDisciplineState::Serialize(FArchive& Ar)
{
	Ar << bObfuscateCloaked;
	Ar << bObfuscateDetectionReady;
	Ar << ObfuscateDetectionRadiusUnits;
	for (int32 i = 0; i < SlotCount; ++i)
	{
		Ar << EndTime[i];
		Ar << ExpirySerial[i];
		Ar << Groups[i];
	}
	Ar << Recovery;
	Ar << SourceActivationEnd;
	Ar << SerialCounter;

	int32 NumEffects = TargetEffects.Num();
	Ar << NumEffects;
	if (Ar.IsLoading())
	{
		TargetEffects.SetNum(FMath::Max(NumEffects, 0));
	}
	for (FElysiumActiveDisciplineEffect& Effect : TargetEffects)
	{
		Ar << Effect.Record << Effect.HitTable << Effect.Effects;
		Ar << Effect.EndTime << Effect.Serial;
		Ar << Effect.bRemoveOnTakeDamage << Effect.bRemoveOnHearCombat << Effect.bRemoveOnWasBumped;
		Ar << Effect.Source;
		Ar << Effect.AiNpcFlag1 << Effect.AiNpcFlag2;
		Ar << Effect.bAddedToComfort << Effect.bHadAiSchedule;
	}
}

void FElysiumCombatCharacter::AddToComfortList()
{
	if (World) { World->AddComfortTarget(Handle); }
	ComfortingCount = 0;
}

void FElysiumCombatCharacter::RemoveFromComfortList()
{
	if (World) { World->RemoveComfortTarget(Handle); }
	ComfortingCount = 0;
}

void FElysiumCombatCharacter::SerializeDisciplineFlags(FElysiumSaveArchive& Ar)
{
	if (Ar.Version() >= FElysiumSaveVersion::DisciplineFlags)
	{
		Ar << MiscFlags << ComfortingCount;
	}
	else if (Ar.IsLoading())
	{
		MiscFlags = 0;
		ComfortingCount = 0;
	}
}
