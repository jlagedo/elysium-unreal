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
	void Fired(const TCHAR* Kind, const FString& Surface, const FString& Receiver,
		const FString& Params, const FString& Owner)
	{
		const FString Key = FString::Printf(TEXT("%s|%s"), Kind, *Surface);
		FTally& Row = Tally().FindOrAdd(Key);
		if (Row.Count == 0)
		{
			Row.Kind = Kind;
			Row.Surface = Surface;
			Row.Owner = Owner;
		}
		++Row.Count;

		if (GStubWarn <= 0 || (GStubWarn == 1 && Row.Count > 1))
		{
			return;
		}

		// Built in one Printf so a stub firing inside a tight think does not cost a string
		// concatenation per optional part.
		UE_LOG(LogElysiumStub, Warning, TEXT("Stub fired!!!!! [%s] %s%s%s%s"),
			Kind,
			*Surface,
			Receiver.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" | on %s"), *Receiver),
			Params.IsEmpty()   ? TEXT("") : *FString::Printf(TEXT(" | params: %s"), *Params),
			Owner.IsEmpty()    ? TEXT("") : *FString::Printf(TEXT(" | owner: %s"), *Owner));
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

// --- Verification command -----------------------------------------------------------------
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
			UE_LOG(LogElysiumStub, Display, TEXT("  %6d  [%s] %s%s"),
				R.Count, *R.Kind, *R.Surface,
				R.Owner.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  (%s)"), *R.Owner));
		}
	}));
