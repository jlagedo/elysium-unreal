#include "Substrate/ElysiumSheetMath.h"

#include "ElysiumPlayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSheet, Log, All);

namespace
{

	int32 TraitKey(EElysiumTraitContainer Container, int32 Slot)
	{
		return (int32)Container * 256 + Slot;
	}

	int32 PayloadKey(EElysiumTraitOp Op, EElysiumTraitContainer Container, int32 Slot)
	{
		return (int32)Op * 65536 + TraitKey(Container, Slot);
	}

	bool IsPayloadOp(EElysiumTraitOp Op)
	{
		return Op == EElysiumTraitOp::Cost || Op == EElysiumTraitOp::BloodCost
			|| Op == EElysiumTraitOp::Damage || Op == EElysiumTraitOp::Duration;
	}

	bool IsFxFlag(const FString& Trait)
	{
		return Trait.StartsWith(TEXT("Fx_"), ESearchCase::IgnoreCase);
	}
}

// ================================================================================================
// FElysiumSheetEffects
// ================================================================================================

void FElysiumSheetEffects::Reset()
{
	TraitRows.Reset();
	FeatRows.Reset();
	PayloadRows.Reset();
	Flags.Reset();
	Groups.Reset();
	Unresolved.Reset();
	SkippedRows = 0;
}

void FElysiumSheetEffects::Build(const FElysiumTraitEffects& Table,
	TArrayView<const FString> GroupNames, const FElysiumFeatTable* Feats,
	const FElysiumStatTable* Stats, const FElysiumStrings* Strings)
{
	Reset();

	for (const FString& Name : GroupNames)
	{
		if (Name.IsEmpty())
		{
			continue;
		}
		const FElysiumTraitEffectGroup* Group = Table.Find(Name);
		if (!Group)
		{
			Unresolved.Add(Name);
			continue;
		}
		Groups.Add(Name);

		for (const FElysiumTraitEffect& Effect : Group->Effects)
		{
			if (!Effect.IsValid())
			{
				continue;
			}
			// A code-side flag: engine behaviour reads it where that behaviour lives, so it is
			// stored as a number and nothing here interprets it.
			if (IsFxFlag(Effect.Trait))
			{
				// `"+1"` is the shipped spelling; a flag authored with no amount still counts as set.
				const int32 Amount = (Effect.Amount != 0) ? Effect.Amount : 1;
				Flags.FindOrAdd(ElysiumFold(Effect.Trait)) += Amount;
				continue;
			}

			// The seven operators the accumulator switches on. `Cost` belongs to the buy path
			// (9.4f), and `BloodCost`/`Damage`/`Duration` carry payloads the systems that own them
			// read — the discipline transactions, the heal timer — not the trait's value; the
			// engine's own switch breaks on all four without touching the query. They are stored
			// under the payload key so their owner can read them, and never enter `TraitRows`.
			const bool bArithmetic =
				Effect.Op == EElysiumTraitOp::Add || Effect.Op == EElysiumTraitOp::Mul
				|| Effect.Op == EElysiumTraitOp::Div || Effect.Op == EElysiumTraitOp::Percent
				|| Effect.Op == EElysiumTraitOp::Max || Effect.Op == EElysiumTraitOp::Min
				|| Effect.Op == EElysiumTraitOp::Value;
			if (!bArithmetic)
			{
				if (!IsPayloadOp(Effect.Op))
				{
					++SkippedRows;
					continue;
				}
				// A payload row targets a trait slot by name, exactly as an arithmetic one does —
				// `"Duration 120%"` on `Fortitude`, `"BloodCost 2"` on `Thaumaturgy`. A name with
				// no slot is recorded rather than dropped, the same as any other unresolved row.
				EElysiumTraitContainer PayloadContainer;
				int32 PayloadSlot = INDEX_NONE;
				if (!ElysiumFindSheetSlot(*Effect.Trait, PayloadContainer, PayloadSlot))
				{
					Unresolved.Add(FString::Printf(TEXT("%s.%s"), *Name, *Effect.Trait));
					continue;
				}
				FRow Payload;
				Payload.Op = Effect.Op;
				Payload.Amount = Effect.Amount;
				Payload.bPercent = Effect.bPercent;
				PayloadRows.FindOrAdd(PayloadKey(Effect.Op, PayloadContainer, PayloadSlot))
					.Add(Payload);
				continue;
			}
			// A `Value` with a NAMED payload (`"Value Clawed_Form"`, `"Value Physical_Mental_Social"`)
			// resolves through an enum the owning layer supplies; the engine stores the resolved index
			// in the same slot the numeric form uses. Chargen supplies stats + strings for its order
			// enums. Other owners can omit them, in which case the name remains explicitly unresolved.
			if (Effect.Op == EElysiumTraitOp::Value && !Effect.ValueName.IsEmpty())
			{
				EElysiumTraitContainer NamedContainer;
				int32 NamedSlot = INDEX_NONE;
				const bool bHasSlot = ElysiumFindSheetSlot(
					*Effect.Trait, NamedContainer, NamedSlot);
				const FElysiumStat* Stat = bHasSlot && Stats
					? Stats->Container(NamedContainer).At(NamedSlot) : nullptr;
				const int32 NamedValue = Stat && Strings && !Stat->NameMapping.IsEmpty()
					? Strings->IndexOf(Stat->NameMapping, Effect.ValueName) : INDEX_NONE;
				if (NamedValue != INDEX_NONE)
				{
					FRow Row;
					Row.Op = EElysiumTraitOp::Value;
					Row.Amount = NamedValue;
					TraitRows.FindOrAdd(TraitKey(NamedContainer, NamedSlot)).Add(Row);
					continue;
				}
				Unresolved.Add(FString::Printf(
					TEXT("%s.%s=%s"), *Name, *Effect.Trait, *Effect.ValueName));
				continue;
			}

			FRow Row;
			Row.Op = Effect.Op;
			Row.Amount = Effect.Amount;

			EElysiumTraitContainer Container;
			int32 Slot = INDEX_NONE;
			if (ElysiumFindSheetSlot(*Effect.Trait, Container, Slot))
			{
				TraitRows.FindOrAdd(TraitKey(Container, Slot)).Add(Row);
				continue;
			}
			const FElysiumFeat* Feat = Feats ? Feats->Find(Effect.Trait) : nullptr;
			if (Feat)
			{
				FeatRows.FindOrAdd(Feat->Index).Add(Row);
				continue;
			}
			// An item, a ConVar, or a Numina power with no compiled slot — `CVStatRef` resolves
			// all three and this layer owns none of them.
			Unresolved.Add(FString::Printf(TEXT("%s.%s"), *Name, *Effect.Trait));
		}
	}

	if (!Unresolved.IsEmpty())
	{
		UE_LOG(LogElysiumSheet, Verbose, TEXT("effect layer: %d rows outside the sheet (%s)"),
			Unresolved.Num(), *FString::Join(Unresolved, TEXT(", ")));
	}
}

int32 FElysiumSheetEffects::NumRows() const
{
	int32 N = Flags.Num();
	for (const TPair<int32, TArray<FRow>>& Pair : TraitRows)   { N += Pair.Value.Num(); }
	for (const TPair<int32, TArray<FRow>>& Pair : FeatRows)    { N += Pair.Value.Num(); }
	for (const TPair<int32, TArray<FRow>>& Pair : PayloadRows) { N += Pair.Value.Num(); }
	return N;
}

namespace
{
	// The single-winner rule the engine's switch implements for `*`, `/`, `Max` and `Min`: a higher
	// group priority takes the slot outright; at equal priority the SMALLER amount wins — including
	// for `Min`, where "smaller" is the looser bound. That asymmetry is the engine's, reproduced.
	void ClaimSlot(int32& Stored, int32& StoredPriority, const FElysiumSheetEffects::FRow& Row)
	{
		if (Row.Priority > StoredPriority)
		{
			Stored = Row.Amount;
			StoredPriority = Row.Priority;
		}
		else if (Row.Priority == StoredPriority && Row.Amount < Stored)
		{
			Stored = Row.Amount;
		}
	}

	int32 ApplyRows(const TArray<FElysiumSheetEffects::FRow>& Rows, int32 Value)
	{
		FElysiumSheetEffects::FQuery Query;
		Query.Value = Value;
		for (const FElysiumSheetEffects::FRow& Row : Rows)
		{
			Query.Accumulate(Row);
		}
		return Query.Finalize();
	}
}

void FElysiumSheetEffects::FQuery::Accumulate(const FRow& Row)
{
	switch (Row.Op)
	{
	case EElysiumTraitOp::Add:     Add += Row.Amount; break;
	case EElysiumTraitOp::Mul:     ClaimSlot(Mul, MulPri, Row); break;
	case EElysiumTraitOp::Div:     ClaimSlot(Div, DivPri, Row); break;
	case EElysiumTraitOp::Max:     ClaimSlot(Max, MaxPri, Row); break;
	case EElysiumTraitOp::Min:     ClaimSlot(Min, MinPri, Row); break;
	// `%` accumulates `100 - amount`, so `"50%"` reads as 150% and `"200%"` as 0. That inversion is
	// the engine's arithmetic as read, and no shipped effect uses the operator.
	case EElysiumTraitOp::Percent: Percent += (100 - Row.Amount); break;
	case EElysiumTraitOp::Value:   Value = Row.Amount; break;
	default: break;
	}
}

int32 FElysiumSheetEffects::FQuery::Finalize() const
{
	int32 Result = Value + Add;
	Result *= Mul;
	if (Div != 0)
	{
		Result /= Div;   // signed, truncating — the engine's CDQ/IDIV
	}
	if (Percent != 100)
	{
		Result = (Percent * Result) / 100;
	}
	if (Result > Max) { return Max; }
	if (Result < Min) { return Min; }
	return Result;
}

int32 FElysiumSheetEffects::ApplyToTrait(EElysiumTraitContainer Container, int32 Slot, int32 Value) const
{
	const TArray<FRow>* Rows = TraitRows.Find(TraitKey(Container, Slot));
	return Rows ? ApplyRows(*Rows, Value) : Value;
}

int32 FElysiumSheetEffects::ApplyToFeat(int32 FeatIndex, int32 Value) const
{
	const TArray<FRow>* Rows = FeatRows.Find(FeatIndex);
	return Rows ? ApplyRows(*Rows, Value) : Value;
}

int32 FElysiumSheetEffects::ApplyToBound(EElysiumTraitContainer Container, int32 Slot, int32 Bound) const
{
	// The same walk, on the bound instead of the value — the engine reaches its effective max and
	// min by resolving the authored `CVStatRef` and handing the number to this identical pass.
	return ApplyToTrait(Container, Slot, Bound);
}

int32 FElysiumSheetEffects::ApplyPayload(EElysiumTraitOp Op, EElysiumTraitContainer Container,
	int32 Slot, int32 Value) const
{
	const TArray<FRow>* Rows = PayloadRows.Find(PayloadKey(Op, Container, Slot));
	if (Rows == nullptr)
	{
		return Value;
	}
	// The stated composition (see the header): every plain row adds, then every percentage row
	// scales in authored order, truncating each time the way the engine's integer maths does.
	int32 Result = Value;
	for (const FRow& Row : *Rows)
	{
		if (!Row.bPercent) { Result += Row.Amount; }
	}
	for (const FRow& Row : *Rows)
	{
		if (Row.bPercent) { Result = (Result * Row.Amount) / 100; }
	}
	return Result;
}

bool FElysiumSheetEffects::HasPayload(EElysiumTraitOp Op, EElysiumTraitContainer Container,
	int32 Slot) const
{
	const TArray<FRow>* Rows = PayloadRows.Find(PayloadKey(Op, Container, Slot));
	return Rows != nullptr && !Rows->IsEmpty();
}

int32 FElysiumSheetEffects::Flag(const TCHAR* FxName) const
{
	const int32* Value = FxName ? Flags.Find(ElysiumFold(FxName)) : nullptr;
	return Value ? *Value : 0;
}

// ================================================================================================
// The feat evaluator
// ================================================================================================

namespace ElysiumFeats
{
	int32 FeatValue(const FElysiumFeat& Feat, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects, const FElysiumCombatCharacter* Owner)
	{
		using EC = EElysiumTraitContainer;

		int32 Rating = 0;
		for (const FElysiumTraitRef& Ref : Feat.Bases)
		{
			EC Container;
			int32 Slot = INDEX_NONE;
			if (!ElysiumFindSheetSlot(*Ref.Trait, Container, Slot))
			{
				continue;   // a base naming nothing on the sheet contributes nothing
			}
			// Only the Attributes and Abilities containers are summed: `FeatValue` tests the
			// reference's category and every other one falls through contributing zero.
			if (Container == EC::Attributes)
			{
				int32 Value = Ref.Apply(Sheet.GetCurrent(Container, Slot));
				// The floor is the nine attributes' — the test is on the slot index, so it also
				// covers `Attrib_Order` at 0, which no feat names.
				if (Value < 1 && Slot < 10)
				{
					Value = 1;
				}
				Rating += Value;
			}
			else if (Container == EC::Abilities)
			{
				Rating += Ref.Apply(Sheet.GetCurrent(Container, Slot));
			}
		}

		// The per-feat code terms. 13.1's is here: the recovered category-1 walk adds
		// `GetStealthModifier()` — the CLAMPED read of the raw `trigger_stealth_mod` aggregate — to
		// feat id 1 and to no other feat, before the effect pass and before the `MaxValue` clamp.
		// The three combat feats' presence-minus-shaky-hands term (13.3) belongs to a system that
		// has not landed; it is additive, so its absence is a rating short by exactly that term,
		// never a wrong shape. `Automatic%d` is deliberately not summed: those are automatic
		// successes, and the dice resolver reads them separately.
		if (Owner != nullptr && Feat.Index == SneakingFeatIndex)
		{
			Rating += Owner->GetStealthModifier();
		}

		if (Effects)
		{
			Rating = Effects->ApplyToFeat(Feat.Index, Rating);
		}
		return FMath::Clamp(Rating, 0, Feat.MaxValue);
	}

	int32 Calc(const FElysiumFeatTable& Feats, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects* Effects, const FString& Name,
		bool& bOutResolved, bool& bOutIsFeat, const FElysiumCombatCharacter* Owner)
	{
		bOutResolved = false;
		bOutIsFeat = false;
		if (Name.IsEmpty())
		{
			return 0;
		}
		if (const FElysiumFeat* Feat = Feats.Find(Name))
		{
			bOutResolved = true;
			bOutIsFeat = true;
			return FeatValue(*Feat, Sheet, Effects, Owner);
		}
		// **A divergence, marked** (`docs/vtmb/script_api.md`): VtMB's `CalcFeat` raises on a non-feat name,
		// because its `.dlg` layer resolves a stat check without going through it. Our dlgexpr
		// normalizer routes every `"<Name> <int>"` check through `CalcFeat`, and the corpus checks
		// `Humanity`(272), `Dominate`(102) and `Thaumaturgy`(55) that way — all traits, not feats.
		// So the name falls back to `CVStatRef`'s own search and reads the trait's current value.
		EElysiumTraitContainer Container;
		int32 Slot = INDEX_NONE;
		if (ElysiumFindSheetSlot(*Name, Container, Slot))
		{
			bOutResolved = true;
			return Sheet.GetCurrent(Container, Slot);
		}
		return 0;
	}
}

// ================================================================================================
// The XP arithmetic
// ================================================================================================

namespace ElysiumXp
{
	int32 WithModifier(int32 RawValue, int32 ExperienceModifier)
	{
		if (RawValue <= 299)
		{
			return RawValue;
		}
		return FMath::Max(RawValue + ExperienceModifier, 100);
	}

	int32 Bank(int32 RawValue, float& InOutRemainder, float& InOutLifetime)
	{
		InOutLifetime += (float)RawValue;
		InOutRemainder += (float)RawValue;
		if (InOutRemainder < 100.f)
		{
			return 0;
		}
		const int32 Whole = (int32)(InOutRemainder * 0.01f);   // __ftol truncates
		InOutRemainder -= (float)Whole * 100.f;
		return Whole;
	}
}

// ================================================================================================
// Shared sheet predicates
// ================================================================================================

namespace ElysiumSheetRules
{
	namespace
	{
		// A side of a predependency: a trait name (its current value) or a literal.
		bool ReadSide(const FString& Raw, const FElysiumSheet& Sheet, int32& Out)
		{
			const FString S = Raw.TrimStartAndEnd();
			if (S.IsEmpty())
			{
				return false;
			}
			if (S.IsNumeric())
			{
				Out = FCString::Atoi(*S);
				return true;
			}
			EElysiumTraitContainer Container;
			int32 Slot = INDEX_NONE;
			if (ElysiumFindSheetSlot(*S, Container, Slot))
			{
				Out = Sheet.GetCurrent(Container, Slot);
				return true;
			}
			return false;
		}
	}

	namespace
	{
		// Process-wide, and deliberately not a singleton object: the whole facility is one struct of
		// borrowed pointers whose lifetime the binder owns.
		FBoundTables GBound;
	}

	void BindTables(const FBoundTables& Tables)
	{
		GBound = Tables;
	}

	const FBoundTables& BoundTables()
	{
		return GBound;
	}

	bool EvalPredependency(const FString& Expr, const FElysiumSheet& Sheet)
	{
		// The longer spellings first, so `<=` is never read as `<`.
		static const TCHAR* const Ops[] = { TEXT("<="), TEXT(">="), TEXT("=="), TEXT("!="),
			TEXT("<"), TEXT(">"), TEXT("=") };

		for (const TCHAR* Op : Ops)
		{
			const int32 At = Expr.Find(Op);
			if (At == INDEX_NONE)
			{
				continue;
			}
			int32 Lhs = 0, Rhs = 0;
			if (!ReadSide(Expr.Left(At), Sheet, Lhs)
				|| !ReadSide(Expr.Mid(At + FCString::Strlen(Op)), Sheet, Rhs))
			{
				return true;   // a side we cannot read — see the header: the gate opens
			}
			const FString OpStr(Op);
			if (OpStr == TEXT("<="))  { return Lhs <= Rhs; }
			if (OpStr == TEXT(">="))  { return Lhs >= Rhs; }
			if (OpStr == TEXT("==") || OpStr == TEXT("=")) { return Lhs == Rhs; }
			if (OpStr == TEXT("!="))  { return Lhs != Rhs; }
			if (OpStr == TEXT("<"))   { return Lhs <  Rhs; }
			return Lhs > Rhs;
		}
		return true;
	}
}
