#include "Visual/ElysiumActionTables.h"

#include "Containers/StringConv.h"

namespace ElysiumActionTables
{
namespace
{
	// The exception rows are sorted by (Block, Base) with an ordinal string compare, which is the
	// order the generator writes them in. Both sides therefore have to agree on "ordinal": every
	// base literal is upper-case ASCII, so a byte compare and a code-point compare are the same
	// ordering here, and `Strcmp` is the one that does not fold case.
	int32 CompareException(const FActionException& Row, int32 Block, const TCHAR* Base)
	{
		if (Row.Block != Block)
		{
			return Row.Block < Block ? -1 : 1;
		}
		return FCString::Strcmp(Row.Base, Base);
	}

	int32 IndexOfBase(const FActionBlock& Block, const FString& Base)
	{
		for (int32 Index = 0; Index < Block.BaseCount; ++Index)
		{
			if (FCString::Stricmp(Block.Bases[Index], *Base) == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool IsRequiredPosition(const FActionBlock& Block, int32 Position)
	{
		for (int32 Index = 0; Index < Block.RequiredCount; ++Index)
		{
			if (Block.RequiredPositions[Index] == Position)
			{
				return true;
			}
		}
		return false;
	}

	const TCHAR* FindRenamePrefix(const FString& Base)
	{
		for (const FActionRename& Rule : RenameRules())
		{
			if (FCString::Stricmp(Rule.Base, *Base) == 0)
			{
				return Rule.Prefix;
			}
		}
		return nullptr;
	}

	bool IsSubstituteBase(const FString& Base)
	{
		for (const TCHAR* const& Entry : SubstituteBases())
		{
			if (FCString::Stricmp(Entry, *Base) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

FString Rewrite(const FString& Base, const FString& Family)
{
	if (Family.IsEmpty())
	{
		return Base;
	}
	if (const TCHAR* Prefix = FindRenamePrefix(Base))
	{
		return FString(Prefix) + Family;
	}
	if (IsSubstituteBase(Base))
	{
		int32 Tail = INDEX_NONE;
		if (Base.FindLastChar(TEXT('_'), Tail))
		{
			return Base.Left(Tail + 1) + Family;
		}
	}
	return Base + TEXT("_") + Family;
}

ERewriteKind KindOf(const FString& Base, const FString& Family)
{
	if (Family.IsEmpty())
	{
		return ERewriteKind::Identity;
	}
	if (FindRenamePrefix(Base) != nullptr)
	{
		return ERewriteKind::Rename;
	}
	if (IsSubstituteBase(Base))
	{
		return ERewriteKind::Substitute;
	}
	return ERewriteKind::Append;
}

const FWeaponLadder* FindLadder(const FString& CppClass)
{
	for (const FWeaponLadder& Ladder : WeaponLadders())
	{
		if (FCString::Stricmp(Ladder.CppClass, *CppClass) == 0)
		{
			return &Ladder;
		}
	}
	return nullptr;
}

const FWeaponLadder* FindLadderByEntityClass(const FString& EntityClassname)
{
	for (const FWeaponLadder& Ladder : WeaponLadders())
	{
		for (int32 Index = 0; Index < Ladder.EntityClassnameCount; ++Index)
		{
			if (FCString::Stricmp(Ladder.EntityClassnames[Index], *EntityClassname) == 0)
			{
				return &Ladder;
			}
		}
	}
	return nullptr;
}

const TCHAR* FindException(const FWeaponLadder& Ladder, int32 Block, const FString& Base)
{
	int32 Low = 0;
	int32 High = Ladder.ExceptionCount - 1;
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		const int32 Order = CompareException(Ladder.Exceptions[Mid], Block, *Base);
		if (Order == 0)
		{
			return Ladder.Exceptions[Mid].Target;
		}
		if (Order < 0)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}
	return nullptr;
}

FTranslation Translate(const FWeaponLadder& Ladder, const FString& Base,
	TFunctionRef<bool(const FString&)> HasActivity)
{
	FTranslation Out;
	Out.Activity = Base;

	for (int32 BlockIndex = 0; BlockIndex < Ladder.BlockCount; ++BlockIndex)
	{
		const FActionBlock& Block = Ladder.Blocks[BlockIndex];
		const int32 Position = IndexOfBase(Block, Base);
		if (Position == INDEX_NONE)
		{
			// Not a rung this base can be asked for. Retail's walk never sees the row, so it is
			// not a rung the ladder failed at either.
			continue;
		}
		++Out.ApplicableRungs;
		if (Out.bTranslated)
		{
			// Already answered; keep counting so the record can say how deep the ladder went.
			continue;
		}

		const FString Literal(Block.Bases[Position]);
		const FString Family(Block.Family != nullptr ? Block.Family : TEXT(""));
		const TCHAR* Exception = FindException(Ladder, BlockIndex, Literal);
		const FString Candidate = Exception != nullptr ? FString(Exception)
			: Rewrite(Literal, Family);
		if (!HasActivity(Candidate))
		{
			continue;
		}

		Out.Activity = Candidate;
		Out.bTranslated = true;
		Out.Rung = Out.ApplicableRungs;
		Out.Block = BlockIndex;
		Out.Kind = Exception != nullptr ? ERewriteKind::Exception : KindOf(Literal, Family);
		Out.bRequired = IsRequiredPosition(Block, Position);
	}
	return Out;
}

void CollectBases(const FWeaponLadder& Ladder, TArray<FString>& OutBases)
{
	OutBases.Reset();
	for (int32 BlockIndex = 0; BlockIndex < Ladder.BlockCount; ++BlockIndex)
	{
		const FActionBlock& Block = Ladder.Blocks[BlockIndex];
		for (int32 Index = 0; Index < Block.BaseCount; ++Index)
		{
			const FString Base(Block.Bases[Index]);
			if (!OutBases.ContainsByPredicate([&Base](const FString& Seen)
				{ return Seen.Equals(Base, ESearchCase::IgnoreCase); }))
			{
				OutBases.Add(Base);
			}
		}
	}
}

void ExpandAll(TArray<FExpandedRow>& OutRows)
{
	OutRows.Reset();
	OutRows.Reserve(Census().RetailRows);
	for (const FWeaponLadder& Ladder : WeaponLadders())
	{
		for (int32 BlockIndex = 0; BlockIndex < Ladder.BlockCount; ++BlockIndex)
		{
			const FActionBlock& Block = Ladder.Blocks[BlockIndex];
			const FString Family(Block.Family != nullptr ? Block.Family : TEXT(""));
			for (int32 Index = 0; Index < Block.BaseCount; ++Index)
			{
				const FString Base(Block.Bases[Index]);
				const TCHAR* Exception = FindException(Ladder, BlockIndex, Base);
				FExpandedRow Row;
				Row.CppClass = Ladder.CppClass;
				Row.Base = Base;
				Row.Target = Exception != nullptr ? FString(Exception) : Rewrite(Base, Family);
				Row.bRequired = IsRequiredPosition(Block, Index);
				OutRows.Add(MoveTemp(Row));
			}
		}
	}
}

const FPlayerAction* FindPlayerAction(int32 Code)
{
	for (const FPlayerAction& Action : PlayerActions())
	{
		if (Action.Code == Code)
		{
			return &Action;
		}
	}
	return nullptr;
}

const FPlayerAction* FindPlayerAction(const FString& Name)
{
	for (const FPlayerAction& Action : PlayerActions())
	{
		if (FCString::Stricmp(Action.Name, *Name) == 0)
		{
			return &Action;
		}
	}
	return nullptr;
}

bool RuleApplies(const FPlayerRule& Rule, FPlayerStateQuery State)
{
	for (const EPlayerPredicate Predicate : Rule.Predicates)
	{
		if (Predicate == EPlayerPredicate::Always)
		{
			continue;
		}
		if (!State(Predicate, Rule.Operand))
		{
			return false;
		}
	}
	return true;
}

const FPlayerRule* SelectRule(TArrayView<const FPlayerRule> Rules, FPlayerStateQuery State)
{
	for (const FPlayerRule& Rule : Rules)
	{
		if (RuleApplies(Rule, State))
		{
			return &Rule;
		}
	}
	return nullptr;
}

FString TranslatePlayerActivity(const FString& Activity)
{
	for (const FPlayerTranslation& Row : PlayerTranslations())
	{
		if (FCString::Stricmp(Row.From, *Activity) == 0)
		{
			return FString(Row.To);
		}
	}
	return Activity;
}

void CollectPlayerActivities(TArray<FString>& OutActivities)
{
	OutActivities.Reset();
	auto Add = [&OutActivities](const TCHAR* Activity)
	{
		if (Activity == nullptr)
		{
			return;
		}
		const FString Name(Activity);
		if (!OutActivities.ContainsByPredicate([&Name](const FString& Seen)
			{ return Seen.Equals(Name, ESearchCase::IgnoreCase); }))
		{
			OutActivities.Add(Name);
		}
	};

	for (const FPlayerRule& Rule : PlayerGaitLadder())
	{
		Add(Rule.Activity);
		Add(Rule.Layer);
	}
	for (const FPlayerAction& Action : PlayerActions())
	{
		for (int32 Index = 0; Index < Action.RuleCount; ++Index)
		{
			Add(Action.Rules[Index].Activity);
			Add(Action.Rules[Index].Layer);
		}
	}
	// The two translation targets are activities the body is asked for as much as any row's is —
	// they are what an unarmed relaxed gait actually resolves against.
	for (const FPlayerTranslation& Row : PlayerTranslations())
	{
		Add(Row.To);
	}
	Add(PlayerTuning().MeleeHoldIdeal);
}

// The NPC translation surface.

namespace
{
	// The chain graph is a DAG four bodies deep at most (Hengeyokai -> Human -> Troika -> the
	// paired-action tail). The bound is a generator-bug tripwire, not a shape the ledger has.
	constexpr int32 GMaxNpcChainDepth = 8;

	void AddUnique(TArray<FString>& Out, const TCHAR* Value)
	{
		if (Value == nullptr || *Value == TEXT('\0'))
		{
			return;
		}
		const FString Name(Value);
		if (!Out.ContainsByPredicate([&Name](const FString& Seen)
			{ return Seen.Equals(Name, ESearchCase::IgnoreCase); }))
		{
			Out.Add(Name);
		}
	}

	void RunNpcBody(int32 BodyIndex, FNpcTranslation& Out, FNpcStateQuery State,
		TFunctionRef<bool(const FString&)> HasActivity, int32 Depth, bool& bStopped)
	{
		if (!ensureMsgf(Depth < GMaxNpcChainDepth,
			TEXT("NPC translation body chain exceeded %d hops at body %d; the generated ledger is "
				"cyclic"), GMaxNpcChainDepth, BodyIndex))
		{
			bStopped = true;
			return;
		}
		TArrayView<const FNpcTranslationBody> Bodies = NpcTranslationBodies();
		if (!Bodies.IsValidIndex(BodyIndex))
		{
			bStopped = true;
			return;
		}
		const FNpcTranslationBody& Body = Bodies[BodyIndex];

		if (Body.Chain == ENpcChain::BeforeRules && Body.ChainTo != INDEX_NONE)
		{
			RunNpcBody(Body.ChainTo, Out, State, HasActivity, Depth + 1, bStopped);
			if (bStopped)
			{
				return;
			}
		}

		// `CoverContextIs` is answered here rather than by the caller, because a `ForceCoverContext`
		// row earlier in the same body can have changed it.
		auto Holds = [&Out, &State](ENpcPredicate Predicate, int32 Operand)
		{
			if (Predicate == ENpcPredicate::Always)
			{
				return true;
			}
			if (Predicate == ENpcPredicate::CoverContextIs)
			{
				return Operand == Out.CoverContext;
			}
			return State(Predicate, Operand);
		};

		for (int32 Index = 0; Index < Body.RuleCount; ++Index)
		{
			const FNpcRule& Rule = Body.Rules[Index];
			if (!Holds(Rule.Predicates[0], Rule.Operand) || !Holds(Rule.Predicates[1], Rule.Operand))
			{
				continue;
			}
			// A row standing for a request family the recovered reading did not enumerate cannot be
			// tested against the request at all. Say so and leave the request alone; guessing a
			// membership here would be the one failure mode these tables exist to prevent.
			if (Rule.FromFamily != nullptr)
			{
				Out.bUnresolved = true;
				continue;
			}
			if (Rule.From != nullptr && !Out.Activity.Equals(Rule.From, ESearchCase::IgnoreCase))
			{
				continue;
			}

			auto Apply = [&Out, &Rule, BodyIndex, Index]()
			{
				Out.Activity = Rule.To;
				Out.bTranslated = true;
				Out.Body = BodyIndex;
				Out.Rule = Index;
			};

			switch (Rule.Route)
			{
			case ENpcRoute::Rewrite:
				Apply();
				break;
			case ENpcRoute::RewriteAndReturn:
				Apply();
				bStopped = true;
				return;
			case ENpcRoute::RewriteIfAvailable:
				if (HasActivity(FString(Rule.To)))
				{
					Apply();
					bStopped = true;
					return;
				}
				break;
			case ENpcRoute::RewriteThroughTranslator:
				Apply();
				Out.bThroughTranslator = true;
				bStopped = true;
				return;
			case ENpcRoute::Delegate:
				Out.bDelegated = true;
				Out.Delegate = Rule.Delegate;
				Out.Body = BodyIndex;
				Out.Rule = Index;
				bStopped = true;
				return;
			case ENpcRoute::ForceCoverContext:
				Out.CoverContext = Rule.Operand;
				break;
			case ENpcRoute::Grapple:
				// The tail answers only for a registered paired-action base with resolved role
				// state, which is the caller's to supply; the walk records that it got here.
				Out.bGrappleTail = true;
				break;
			}
		}

		if (Body.Chain == ENpcChain::AfterRules && Body.ChainTo != INDEX_NONE)
		{
			RunNpcBody(Body.ChainTo, Out, State, HasActivity, Depth + 1, bStopped);
		}
	}
}

const FNpcClass* FindNpcClass(const FString& CppClass)
{
	for (const FNpcClass& Class : NpcClasses())
	{
		if (FCString::Stricmp(Class.CppClass, *CppClass) == 0)
		{
			return &Class;
		}
	}
	return nullptr;
}

const FNpcClass* FindNpcClassByEntityClass(const FString& EntityClassname)
{
	TArrayView<const FNpcClass> Classes = NpcClasses();
	for (const FNpcEntityAlias& Alias : NpcEntityAliases())
	{
		if (FCString::Stricmp(Alias.Classname, *EntityClassname) == 0)
		{
			return Classes.IsValidIndex(Alias.ClassIndex) ? &Classes[Alias.ClassIndex] : nullptr;
		}
	}
	// No constructor claimed the name. The second step is the canonical spelling, which is how the
	// survey resolves a class whose constructor string the alias scan did not reach.
	if (EntityClassname.StartsWith(TEXT("npc_"), ESearchCase::IgnoreCase))
	{
		return FindNpcClass(TEXT("CNPC_") + EntityClassname.RightChop(4));
	}
	return nullptr;
}

const FNpcTranslationBody* NpcBodyFor(const FNpcClass& Class, ENpcSlot Slot)
{
	int32 Index = INDEX_NONE;
	switch (Slot)
	{
	case ENpcSlot::PreTranslate:   Index = Class.PreTranslate;   break;
	case ENpcSlot::ClassTranslate: Index = Class.ClassTranslate; break;
	case ENpcSlot::Cover:          Index = Class.Cover;          break;
	case ENpcSlot::Reload:         Index = Class.Reload;         break;
	case ENpcSlot::EarlyTranslate:
		// Not a vtable slot: every class reaches the tail by chain, never by its own column.
		break;
	}
	TArrayView<const FNpcTranslationBody> Bodies = NpcTranslationBodies();
	return Bodies.IsValidIndex(Index) ? &Bodies[Index] : nullptr;
}

FNpcTranslation NpcTranslate(int32 BodyIndex, const FString& Base, int32 CoverContext,
	FNpcStateQuery State, TFunctionRef<bool(const FString&)> HasActivity)
{
	FNpcTranslation Out;
	Out.Activity = Base;
	Out.CoverContext = CoverContext;

	bool bStopped = false;
	RunNpcBody(BodyIndex, Out, State, HasActivity, 0, bStopped);
	return Out;
}

const FNpcGrappleFamily* FindGrappleFamily(const FString& Base)
{
	for (const FNpcGrappleFamily& Family : NpcGrappleFamilies())
	{
		if (FCString::Stricmp(Family.Base, *Base) == 0)
		{
			return &Family;
		}
	}
	return nullptr;
}

int32 GrappleOffset(bool bTallCounterpart, bool bVictimRole, bool bBackPosition)
{
	return 1 + (bTallCounterpart ? 1 : 0) + (bVictimRole ? 2 : 0) + (bBackPosition ? 4 : 0);
}

FNpcGrappleVariant GrappleVariant(const FNpcGrappleFamily& Family, bool bTallCounterpart,
	bool bVictimRole, bool bBackPosition)
{
	FNpcGrappleVariant Out;
	Out.Offset = GrappleOffset(bTallCounterpart, bVictimRole, bBackPosition);
	Out.ActivityId = Family.BaseId + Out.Offset;
	Out.bNameAgreesWithRole = Family.RoleOrder == ENpcGrappleRoleOrder::Canonical;

	TArrayView<const TCHAR* const> Suffixes = NpcGrappleRoleSuffixes(Family.RoleOrder);
	Out.Activity = FString(Family.Base) + TEXT("_") + Suffixes[Out.Offset - 1];
	return Out;
}

void CollectNpcTaskPolicies(const FString& TaskName, TArray<const FNpcTaskPolicy*>& OutPolicies)
{
	OutPolicies.Reset();
	for (const FNpcTaskPolicy& Policy : NpcTaskPolicies())
	{
		for (int32 Index = 0; Index < Policy.TaskCount; ++Index)
		{
			if (FCString::Stricmp(Policy.Tasks[Index], *TaskName) == 0)
			{
				OutPolicies.Add(&Policy);
				break;
			}
		}
	}
}

bool RouteRestartsIdenticalRequest(ENpcTaskRoute Route)
{
	// The two restart-helper routes, and only those. `SetIdeal` and `SetIdealArgument` go straight to
	// `SetIdealActivity`, which leaves an unchanged ideal alone; `SetActivity` commits immediately but
	// still through the unchanged-selection test; `SetIdealNavigator` and `RemapSharedTask` end in one
	// of the above.
	return Route == ENpcTaskRoute::RestartIdeal || Route == ENpcTaskRoute::RestartIdealChoice;
}

bool TaskRestartsIdenticalRequest(const FString& TaskName)
{
	TArray<const FNpcTaskPolicy*> Policies;
	CollectNpcTaskPolicies(TaskName, Policies);
	for (const FNpcTaskPolicy* Policy : Policies)
	{
		if (Policy != nullptr && RouteRestartsIdenticalRequest(Policy->Route))
		{
			return true;
		}
	}
	return false;
}

bool ActivityRestartsIdenticalRequest(const FString& Activity)
{
	if (Activity.IsEmpty())
	{
		return false;
	}
	for (const FNpcTaskPolicy& Policy : NpcTaskPolicies())
	{
		if (!RouteRestartsIdenticalRequest(Policy.Route))
		{
			continue;
		}
		for (int32 Index = 0; Index < Policy.ActivityCount; ++Index)
		{
			if (FCString::Stricmp(Policy.Activities[Index], *Activity) == 0)
			{
				return true;
			}
		}
	}
	return false;
}

void CollectNpcActivities(TArray<FString>& OutActivities)
{
	OutActivities.Reset();
	for (const FNpcTranslationBody& Body : NpcTranslationBodies())
	{
		for (int32 Index = 0; Index < Body.RuleCount; ++Index)
		{
			AddUnique(OutActivities, Body.Rules[Index].From);
			AddUnique(OutActivities, Body.Rules[Index].To);
		}
	}
	for (const FNpcTaskPolicy& Policy : NpcTaskPolicies())
	{
		for (int32 Index = 0; Index < Policy.ActivityCount; ++Index)
		{
			AddUnique(OutActivities, Policy.Activities[Index]);
		}
	}
	for (const FNpcGrappleFamily& Family : NpcGrappleFamilies())
	{
		AddUnique(OutActivities, Family.Base);
	}
}

void CollectGrappleVariants(TArray<FString>& OutVariants)
{
	OutVariants.Reset();
	OutVariants.Reserve(NpcCensus().GrappleVariants);
	for (const FNpcGrappleFamily& Family : NpcGrappleFamilies())
	{
		for (const TCHAR* const& Suffix : NpcGrappleRoleSuffixes(Family.RoleOrder))
		{
			OutVariants.Add(FString(Family.Base) + TEXT("_") + Suffix);
		}
	}
}

uint64 DigestOf(const TArray<FExpandedRow>& Rows)
{
	uint64 Digest = 0xCBF29CE484222325ull;
	for (const FExpandedRow& Row : Rows)
	{
		const FString Line = FString::Printf(TEXT("%s|%s|%s|%d\n"), *Row.CppClass, *Row.Base,
			*Row.Target, Row.bRequired ? 1 : 0);
		const FTCHARToUTF8 Utf8(*Line);
		const ANSICHAR* Bytes = Utf8.Get();
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Digest ^= static_cast<uint8>(Bytes[Index]);
			Digest *= 0x100000001B3ull;
		}
	}
	return Digest;
}
}
