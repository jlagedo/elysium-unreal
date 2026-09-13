#include "ElysiumStub.h"

#include "ElysiumEntity.h"
#include "ElysiumVariant.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumStub, Log, All);

namespace
{
	// 2 = warn on every fire, 1 = warn the first time each surface fires, 0 = tally silently. The
	// default is the loud one on purpose: a stub that fires without saying so is indistinguishable
	// from a working system, which is exactly the confusion this exists to remove.
	int32 GStubWarn = 2;
	FAutoConsoleVariableRef CVarStubWarn(
		TEXT("elysium.StubWarn"),
		GStubWarn,
		TEXT("Volume of the 'Stub fired!!!!!' warning: 2 every fire (default), 1 first fire per ")
		TEXT("surface, 0 tally only. The tally is kept at every setting; read it with elysium.stubs."),
		ECVF_Default);

	// Keyed on kind+surface, which is what makes the tally a work list: one row per unimplemented
	// thing, however many instances or maps fire it.
	TMap<FString, ElysiumStub::FTally>& Tally()
	{
		static TMap<FString, ElysiumStub::FTally> Instance;
		return Instance;
	}
}

namespace ElysiumStub
{
	void Fired(const FSurface& What, const FString& Receiver, const FString& Params,
		const FString& Owner)
	{
		const FString Key = FString::Printf(TEXT("%s|%s"), What.Kind, *What.Surface);
		FTally& Row = Tally().FindOrAdd(Key);
		if (Row.Count == 0)
		{
			Row.Kind = What.Kind;
			Row.Surface = What.Surface;
			Row.Owner = Owner;
			Row.Address = What.Address;
			Row.Story = What.Story;
		}
		++Row.Count;

		if (GStubWarn <= 0 || (GStubWarn == 1 && Row.Count > 1))
		{
			return;
		}

		// Built in one Printf so a stub firing inside a tight think does not cost a string
		// concatenation per optional part. The address and the story ride with the surface,
		// because a line a reader can paste into the ledger is the point of recording them.
		UE_LOG(LogElysiumStub, Warning, TEXT("Stub fired!!!!! [%s] %s%s%s%s%s"),
			What.Kind,
			*What.Surface,
			What.Address.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" | %s%s"), *What.Address,
				What.Story.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *What.Story)),
			Receiver.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" | on %s"), *Receiver),
			Params.IsEmpty()   ? TEXT("") : *FString::Printf(TEXT(" | params: %s"), *Params),
			Owner.IsEmpty()    ? TEXT("") : *FString::Printf(TEXT(" | owner: %s"), *Owner));
	}

	void Fired(const TCHAR* Kind, const FString& Surface, const FString& Receiver,
		const FString& Params, const FString& Owner)
	{
		FSurface What;
		What.Kind = Kind;
		What.Surface = Surface;
		Fired(What, Receiver, Params, Owner);
	}

	FString DescribeInput(const FElysiumInputArgs& Args)
	{
		// Provenance is half of what makes an unfired wire diagnosable — which entity aimed this
		// input at this target — so activator/caller are always spelled, unset included.
		return FString::Printf(TEXT("param=%s activator=%s caller=%s"),
			*Args.Param.Describe(),
			*Args.Activator.ToString(),
			*Args.Caller.ToString());
	}

	FString DescribeArgs(TArrayView<const FElysiumVariant> Args)
	{
		FString Out;
		for (int32 i = 0; i < Args.Num(); ++i)
		{
			if (i > 0) { Out += TEXT(", "); }
			Out += Args[i].Describe();
		}
		return Out;
	}

	void CollectTally(TArray<FTally>& Out)
	{
		Out.Reset();
		for (const TPair<FString, FTally>& Pair : Tally())
		{
			Out.Add(Pair.Value);
		}
		Out.Sort([](const FTally& A, const FTally& B)
		{
			if (A.Count != B.Count) { return A.Count > B.Count; }
			return A.Surface < B.Surface;
		});
	}

	void ClearTally()
	{
		Tally().Reset();
	}
}

// `elysium.stubs` reads the work list back: every unimplemented surface reached since load, what
// owns it, and how hard the shipped content leans on it. `elysium.stubs clear` resets the counts
// so a single map load or a single scene can be measured on its own.

static FAutoConsoleCommandWithWorldAndArgs GElysiumStubsCmd(
	TEXT("elysium.stubs"),
	TEXT("elysium.stubs [clear] — list every stubbed/unwired surface reached since load, most-fired first"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() >= 1 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			ElysiumStub::ClearTally();
			UE_LOG(LogElysiumStub, Display, TEXT("stub tally cleared"));
			return;
		}

		TArray<ElysiumStub::FTally> Rows;
		ElysiumStub::CollectTally(Rows);
		if (Rows.Num() == 0)
		{
			UE_LOG(LogElysiumStub, Display, TEXT("no stub reached since load"));
			return;
		}

		int32 Total = 0;
		for (const ElysiumStub::FTally& R : Rows) { Total += R.Count; }
		UE_LOG(LogElysiumStub, Display, TEXT("%d stubbed surfaces reached, %d fires total"),
			Rows.Num(), Total);
		for (const ElysiumStub::FTally& R : Rows)
		{
			// The address and story columns come before the owner so the readout greps as a
			// work list against `docs/vtmb/npc-kernel/functions.md`.
			UE_LOG(LogElysiumStub, Display, TEXT("  %6d  [%s] %s%s%s%s"),
				R.Count, *R.Kind, *R.Surface,
				R.Address.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  %s"), *R.Address),
				R.Story.IsEmpty()   ? TEXT("") : *FString::Printf(TEXT("  %s"), *R.Story),
				R.Owner.IsEmpty()   ? TEXT("") : *FString::Printf(TEXT("  (%s)"), *R.Owner));
		}
	}));
