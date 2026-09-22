#include "Substrate/ElysiumScheduleText.h"

#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumScheduleId.h"
#include "Substrate/ElysiumScheduleTokenizer.h"
#include "Substrate/ElysiumSymbolRegistry.h"

const TCHAR* ElysiumScheduleParseFailureName(EElysiumScheduleParseFailure Failure)
{
	switch (Failure)
	{
	case EElysiumScheduleParseFailure::None:                  return TEXT("none");
	case EElysiumScheduleParseFailure::UnknownToken:          return TEXT("unknown-token");
	case EElysiumScheduleParseFailure::DuplicateScheduleName: return TEXT("duplicate-schedule-name");
	case EElysiumScheduleParseFailure::UnknownScheduleName:   return TEXT("unknown-schedule-name");
	case EElysiumScheduleParseFailure::MissingTasks:          return TEXT("missing-tasks");
	case EElysiumScheduleParseFailure::TaskCap:               return TEXT("task-cap");
	case EElysiumScheduleParseFailure::UnknownTask:           return TEXT("unknown-task");
	case EElysiumScheduleParseFailure::MissingOperand:        return TEXT("missing-operand");
	case EElysiumScheduleParseFailure::BadSyntaxAtTask:       return TEXT("bad-syntax-at-task");
	case EElysiumScheduleParseFailure::UnknownPrefix:         return TEXT("unknown-prefix");
	case EElysiumScheduleParseFailure::StrayColon:            return TEXT("stray-colon");
	case EElysiumScheduleParseFailure::UnknownValue:          return TEXT("unknown-value");
	}
	return TEXT("?");
}

namespace
{
	/**
	 * The parse, as one pass over the token stream.
	 *
	 * Every method answers false the moment a failure row fires and leaves the row, its message and
	 * its offset on the result. The caller stops; retail's caller then abandons the rest of that
	 * class's texts, which is why one bad text costs a whole class its programs and why the export
	 * refuses to publish one.
	 */
	class FScheduleTextParser
	{
	public:
		FScheduleTextParser(const TArray<FElysiumScheduleToken>& InTokens,
			const FElysiumScheduleParseContext& InContext, FElysiumScheduleParseResult& InResult)
			: Tokens(InTokens)
			, Context(InContext)
			, Result(InResult)
		{
		}

		void Run()
		{
			// A text whose first token is not `Schedule` -- an empty one included -- is not a
			// failure. It loads nothing and the owner carries on to its next text.
			const FElysiumScheduleToken* First = Peek();
			if (First == nullptr || !First->Equals(TEXT("Schedule")))
			{
				return;
			}

			while (!Exhausted())
			{
				const FElysiumScheduleToken& Keyword = *Peek();
				if (!Keyword.Equals(TEXT("Schedule")))
				{
					Fail(EElysiumScheduleParseFailure::UnknownToken, Keyword.Offset,
						FString::Printf(TEXT("expected `Schedule` and found `%s`"), *Keyword.Text));
					return;
				}
				Advance();
				if (!ParseRecord(Keyword))
				{
					return;
				}
			}
		}

	private:
		// --- The token stream -------------------------------------------------------------------

		bool Exhausted() const { return Index >= Tokens.Num(); }
		const FElysiumScheduleToken* Peek() const
		{
			return Tokens.IsValidIndex(Index) ? &Tokens[Index] : nullptr;
		}
		const FElysiumScheduleToken* Advance()
		{
			const FElysiumScheduleToken* Token = Peek();
			if (Token != nullptr)
			{
				++Index;
			}
			return Token;
		}

		void Fail(EElysiumScheduleParseFailure Row, int32 Offset, FString&& Message)
		{
			Result.Failure = Row;
			Result.Offset = Offset;
			Result.Message = MoveTemp(Message);
		}

		static bool IsSectionKeyword(const FElysiumScheduleToken& Token)
		{
			return Token.Equals(TEXT("Interrupts")) || Token.Equals(TEXT("Flags"))
				|| Token.Equals(TEXT("Schedule"));
		}

		// --- The grammar ------------------------------------------------------------------------

		bool ParseRecord(const FElysiumScheduleToken& Keyword)
		{
			const FElysiumScheduleToken* Name = Advance();
			if (Name == nullptr)
			{
				Fail(EElysiumScheduleParseFailure::UnknownScheduleName, Keyword.Offset,
					TEXT("a `Schedule` with no name"));
				return false;
			}

			for (const FElysiumScheduleProgram& Declared : Result.Programs)
			{
				if (Declared.Name.Equals(Name->Text, ESearchCase::IgnoreCase))
				{
					Fail(EElysiumScheduleParseFailure::DuplicateScheduleName, Name->Offset,
						FString::Printf(TEXT("the text declares `%s` twice"), *Name->Text));
					return false;
				}
			}

			// Retail links the node into the manager here, before a single task is read, and no
			// error path unlinks it. Adding it now is what reproduces that: a text that fails below
			// leaves a real, task-less program behind.
			FElysiumScheduleProgram& Program = Result.Programs.AddDefaulted_GetRef();
			Program.Name = Name->Text;

			if (Context.ScheduleSpace != nullptr && Context.ScheduleSpace->Namespace != nullptr)
			{
				Program.GlobalId = Context.ScheduleSpace->Namespace->Find(Name->Text);
				if (Program.GlobalId == INDEX_NONE)
				{
					Fail(EElysiumScheduleParseFailure::UnknownScheduleName, Name->Offset,
						FString::Printf(TEXT("`%s` is not a registered schedule name"), *Name->Text));
					return false;
				}
			}

			const FElysiumScheduleToken* TasksKeyword = Advance();
			if (TasksKeyword == nullptr || !TasksKeyword->Equals(TEXT("Tasks")))
			{
				Fail(EElysiumScheduleParseFailure::MissingTasks,
					TasksKeyword != nullptr ? TasksKeyword->Offset : Name->End(),
					FString::Printf(TEXT("`%s` has no `Tasks` section"), *Name->Text));
				return false;
			}

			if (!ParseTasks(Program))
			{
				return false;
			}

			const FElysiumScheduleToken* Section = Peek();
			if (Section != nullptr && Section->Equals(TEXT("Interrupts")))
			{
				Advance();
				if (!ParseInterrupts(Program))
				{
					return false;
				}
				Section = Peek();
			}
			if (Section != nullptr && Section->Equals(TEXT("Flags")))
			{
				Advance();
				ParseFlags(Program);
			}
			return true;
		}

		bool ParseTasks(FElysiumScheduleProgram& Program)
		{
			while (true)
			{
				const FElysiumScheduleToken* Token = Peek();
				if (Token == nullptr || IsSectionKeyword(*Token))
				{
					return true;
				}
				const FElysiumScheduleToken TaskName = *Token;
				Advance();

				if (Program.Tasks.Num() >= FElysiumScheduleProgram::MaxTasks)
				{
					Fail(EElysiumScheduleParseFailure::TaskCap, TaskName.Offset,
						FString::Printf(TEXT("`%s` declares more than %d tasks"),
							*Program.Name, FElysiumScheduleProgram::MaxTasks));
					return false;
				}

				FElysiumScheduleStep Step;
				if (!ResolveTaskName(TaskName, Step.TaskId))
				{
					return false;
				}
				if (!ParseOperand(Program, TaskName, Step))
				{
					return false;
				}
				Program.Tasks.Add(Step);
			}
		}

		bool ResolveTaskName(const FElysiumScheduleToken& TaskName, int32& OutTaskId)
		{
			if (Context.TaskSpace == nullptr || Context.TaskSpace->Namespace == nullptr)
			{
				return true;
			}
			const int32 GlobalId = Context.TaskSpace->Namespace->Find(TaskName.Text);
			if (GlobalId == INDEX_NONE)
			{
				Fail(EElysiumScheduleParseFailure::UnknownTask, TaskName.Offset,
					FString::Printf(TEXT("`%s` is not a registered task name"), *TaskName.Text));
				return false;
			}
			// Retail translates this to the class-local number here and stores that. See the
			// divergence on `FElysiumScheduleStep::TaskId`: the global id is what one leaf runtime
			// can key on, and it fits an `int32` exactly where the local translation would not
			// survive a sibling space.
			OutTaskId = GlobalId;
			return true;
		}

		/** Exactly one operand per task. Seventeen prefixes, four boolean words, or a number. */
		bool ParseOperand(FElysiumScheduleProgram& Program, const FElysiumScheduleToken& TaskName,
			FElysiumScheduleStep& Step)
		{
			const FElysiumScheduleToken* Token = Advance();
			if (Token == nullptr)
			{
				Fail(EElysiumScheduleParseFailure::MissingOperand, TaskName.End(),
					FString::Printf(TEXT("task `%s` in `%s` has no operand"),
						*TaskName.Text, *Program.Name));
				return false;
			}

			// A section keyword or another `TASK_*` in the operand slot is retail's
			// "Bad syntax at task #%d" -- the failure an author gets for forgetting an operand
			// rather than for omitting one at the end of the text.
			if (Token->Equals(TEXT("Interrupts")) || Token->Equals(TEXT("Flags"))
				|| Token->Text.StartsWith(TEXT("TASK_"), ESearchCase::IgnoreCase))
			{
				Fail(EElysiumScheduleParseFailure::BadSyntaxAtTask, Token->Offset,
					FString::Printf(TEXT("bad syntax at task #%d (`%s`): its operand is `%s`"),
						Program.Tasks.Num(), *TaskName.Text, *Token->Text));
				return false;
			}

			const FElysiumScheduleToken Head = *Token;
			const FElysiumScheduleToken* Following = Peek();
			if (Following != nullptr && Following->Is(TEXT(':')))
			{
				const FElysiumScheduleToken Colon = *Following;
				Advance();
				const FElysiumScheduleToken* Value = Advance();
				if (Value == nullptr)
				{
					Fail(EElysiumScheduleParseFailure::MissingOperand, Colon.End(),
						FString::Printf(TEXT("prefix `%s` in `%s` has no value"),
							*Head.Text, *Program.Name));
					return false;
				}

				const EElysiumOperandPrefix Prefix = ElysiumScheduleOperands::PrefixFromName(Head.Text);
				if (Prefix == EElysiumOperandPrefix::None)
				{
					Fail(EElysiumScheduleParseFailure::UnknownPrefix, Head.Offset,
						FString::Printf(TEXT("`%s` is not one of the seventeen operand prefixes"),
							*Head.Text));
					return false;
				}

				const FElysiumScheduleToken* Stray = Peek();
				if (Stray != nullptr && Stray->Is(TEXT(':')))
				{
					Fail(EElysiumScheduleParseFailure::StrayColon, Stray->Offset,
						FString::Printf(TEXT("a stray `:` after `%s`"), *Value->Text));
					return false;
				}

				return ResolvePrefixedOperand(Program, Prefix, Head, *Value, Step);
			}

			if (Head.Is(TEXT(':')))
			{
				Fail(EElysiumScheduleParseFailure::StrayColon, Head.Offset,
					TEXT("an operand that is a bare `:`"));
				return false;
			}

			float Boolean = 0.0f;
			if (ElysiumScheduleOperands::BooleanWord(Head.Text, Boolean))
			{
				Step.Data = Boolean;
				return true;
			}

			Step.Data = ElysiumScheduleOperands::Atof(Head.Text);
			return true;
		}

		bool ResolvePrefixedOperand(FElysiumScheduleProgram& Program, EElysiumOperandPrefix Prefix,
			const FElysiumScheduleToken& Head, const FElysiumScheduleToken& Value,
			FElysiumScheduleStep& Step)
		{
			switch (Prefix)
			{
			case EElysiumOperandPrefix::Activity:
				// Interns rather than failing; see `ElysiumSymbolRegistry.h`.
				Step.Data = Context.Activities != nullptr
					? static_cast<float>(Context.Activities->Intern(Value.Text))
					: 0.0f;
				return true;

			case EElysiumOperandPrefix::Sound:
				Step.Data = Context.Sounds != nullptr
					? static_cast<float>(Context.Sounds->Intern(Value.Text))
					: 0.0f;
				return true;

			case EElysiumOperandPrefix::Model:
				// Raw, and interned: the symbol table it would resolve against is filled by a
				// precache path this runtime has not built.
				Step.SetRawWord(Context.Models != nullptr
					? static_cast<uint32>(Context.Models->Intern(Value.Text))
					: 0u);
				return true;

			case EElysiumOperandPrefix::Task:
				return ResolveIdOperand(Program, Context.TaskSpace, TEXT("task"), Head, Value, Step);

			case EElysiumOperandPrefix::Schedule:
				return ResolveIdOperand(
					Program, Context.ScheduleSpace, TEXT("schedule"), Head, Value, Step);

			case EElysiumOperandPrefix::NpcFlag:
			{
				EElysiumNpcFlag Word1 = static_cast<EElysiumNpcFlag>(0);
				EElysiumNpcFlag2 Word2 = static_cast<EElysiumNpcFlag2>(0);
				if (!FElysiumNpcFlags::ParseName(Value.Text, Word1, Word2))
				{
					return FailValue(Prefix, Head, Value, Program);
				}
				// The sign bit is how the resolver says "this is in the second flag word"; the
				// task arm tests it rather than carrying two operand forms.
				const uint32 Raw = static_cast<uint32>(Word1) != 0
					? static_cast<uint32>(Word1)
					: (0x80000000u | static_cast<uint32>(Word2));
				Step.SetRawWord(Raw);
				return true;
			}

			case EElysiumOperandPrefix::MiscFlag:
				// Raw INDEX, and it cannot fail: an unknown name silently reads 0.
				Step.SetRawWord(
					static_cast<uint32>(ElysiumMiscFlags::ParseScheduleIndex(Value.Text)));
				return true;

			case EElysiumOperandPrefix::HintFlags:
				// A substring search, and it cannot fail either.
				Step.Data = static_cast<float>(
					ElysiumScheduleOperands::ResolveHintFlags(Value.Text));
				return true;

			default:
			{
				int32 Resolved = 0;
				if (!ElysiumScheduleOperands::ResolveTable(Prefix, Value.Text, Resolved))
				{
					return FailValue(Prefix, Head, Value, Program);
				}
				// Signed conversion, which is `FILD`. `Memory:CUSTOM1` is `0x80000000` and lands
				// here as -2147483648.0f; a consumer casts back through `int32` to recover the mask.
				Step.Data = static_cast<float>(Resolved);
				return true;
			}
			}
		}

		/** `Task:` and `Schedule:`: the global namespace, then the class's own local translation.
		 *
		 *  Unlike the task record's id, this one is stored LOCAL, exactly as retail does -- the
		 *  data word is a float, and a global id near 1,000,000,000 does not survive a float. The
		 *  consumers of these operands are the translate tables, which speak retail-local numbers. */
		bool ResolveIdOperand(FElysiumScheduleProgram& Program, const FElysiumLocalIdSpace* Space,
			const TCHAR* Category, const FElysiumScheduleToken& Head,
			const FElysiumScheduleToken& Value, FElysiumScheduleStep& Step)
		{
			if (Space == nullptr || Space->Namespace == nullptr)
			{
				return true;
			}
			const int32 GlobalId = Space->Namespace->Find(Value.Text);
			if (GlobalId == INDEX_NONE)
			{
				Fail(EElysiumScheduleParseFailure::UnknownValue, Head.Offset,
					FString::Printf(TEXT("`%s:%s` in `%s` names no registered %s"),
						*Head.Text, *Value.Text, *Program.Name, Category));
				return false;
			}
			// A name registered by some OTHER class's private space translates to -1 here, which
			// is what retail stores too -- the lookup succeeded, the translation did not, and no
			// failure row covers it.
			Step.Data = static_cast<float>(Space->GlobalToLocal(GlobalId));
			return true;
		}

		bool FailValue(EElysiumOperandPrefix Prefix, const FElysiumScheduleToken& Head,
			const FElysiumScheduleToken& Value, const FElysiumScheduleProgram& Program)
		{
			Fail(EElysiumScheduleParseFailure::UnknownValue, Head.Offset,
				FString::Printf(TEXT("`%s:%s` in `%s` is not a value %s answers"),
					*Head.Text, *Value.Text, *Program.Name,
					ElysiumScheduleOperands::ResolverAddress(Prefix)));
			return false;
		}

		bool ParseInterrupts(FElysiumScheduleProgram& Program)
		{
			while (true)
			{
				const FElysiumScheduleToken* Token = Peek();
				if (Token == nullptr || Token->Equals(TEXT("Flags")) || Token->Equals(TEXT("Schedule")))
				{
					return true;
				}
				const FElysiumScheduleToken Start = *Token;
				Advance();

				FString ConditionName = Start.Text;
				bool bInverted = false;
				if (Start.Text == TEXT("!"))
				{
					// `! COND_FOO`, spelled apart. The tokenizer does not cut `!`, so this only
					// happens where the text authored a space after it.
					bInverted = true;
					const FElysiumScheduleToken* Next = Advance();
					if (Next == nullptr)
					{
						Fail(EElysiumScheduleParseFailure::MissingOperand, Start.End(),
							TEXT("a `!` with no condition after it"));
						return false;
					}
					ConditionName = Next->Text;
				}
				else if (Start.Text.Len() > 1 && Start.Text[0] == TEXT('!'))
				{
					bInverted = true;
					ConditionName = Start.Text.RightChop(1);
				}

				SetCondition(Program, ConditionName, bInverted);
			}
		}

		/** The interrupt arm: global ordinal, minus the seed, into the mask. Conditions are NOT
		 *  translated -- the masks are in global condition ordinals, for every class. */
		void SetCondition(FElysiumScheduleProgram& Program, const FString& Name, bool bInverted)
		{
			if (Context.Conditions == nullptr)
			{
				return;
			}
			const int32 GlobalId = Context.Conditions->Find(Name);
			if (GlobalId == INDEX_NONE)
			{
				// Retail `DevMsg`s and drops the bit. It does NOT fail the text, which is why a
				// class naming a condition it never registered still loads.
				++Result.SkippedConditions;
				return;
			}
			const int32 Ordinal = GlobalId - ElysiumScheduleId::GlobalBase;
			if (bInverted)
			{
				Program.InvertedInterrupts.SetOrdinal(Ordinal);
			}
			else
			{
				Program.Interrupts.SetOrdinal(Ordinal);
			}
		}

		void ParseFlags(FElysiumScheduleProgram& Program)
		{
			while (true)
			{
				const FElysiumScheduleToken* Token = Peek();
				if (Token == nullptr || Token->Equals(TEXT("Schedule")))
				{
					return;
				}
				Advance();

				bool bKnown = false;
				const int32 Flag = ElysiumScheduleOperands::ResolveScheduleFlag(Token->Text, bKnown);
				Program.Flags |= Flag;
				if (Flag == 0)
				{
					// Retail reports "Unknown schedule flag" for every zero, so an authored
					// `Flags NONE` prints it harmlessly. Counting rather than logging keeps a load
					// of 691 texts from becoming a wall of the same true statement.
					++Result.UnknownFlags;
				}
			}
		}

		const TArray<FElysiumScheduleToken>& Tokens;
		const FElysiumScheduleParseContext& Context;
		FElysiumScheduleParseResult& Result;
		int32 Index = 0;
	};
}

FElysiumScheduleParseResult ElysiumScheduleText::Parse(
	const FString& Body, const FElysiumScheduleParseContext& Context)
{
	const TArray<FElysiumScheduleToken> Tokens = ElysiumScheduleTokenizer::Tokenize(Body);

	FElysiumScheduleParseResult Result;
	FScheduleTextParser(Tokens, Context, Result).Run();
	return Result;
}
