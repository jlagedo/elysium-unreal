#include "HAL/IConsoleManager.h"
#include "Substrate/ElysiumScheduleCorpus.h"

#if !UE_BUILD_SHIPPING
namespace
{
	void DescribeSchedules(const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
	{
		FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
		if (!Corpus.EnsureLoaded())
		{
			Out.Log(TEXT("Schedule corpus unavailable; run `uv run elysium import ai-schedules`."));
			return;
		}
		Out.Log(*Corpus.DescribeCensus());
		TArray<TPair<FString, int32>> Rows;
		for (const auto& Row : Corpus.TaskOps().UnportedByName())
		{
			if (Args.IsEmpty() || Row.Key.Contains(Args[0])) Rows.Add(Row);
		}
		Rows.Sort([](const auto& A, const auto& B)
		{
			return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key;
		});
		for (const auto& Row : Rows)
		{
			Out.Logf(TEXT("%4d steps  %s"), Row.Value, *Row.Key);
		}
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice SchedulesCommand(
		TEXT("elysium.schedules"),
		TEXT("Schedule corpus and unported task coverage, largest reference count first. Optional task-name filter."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DescribeSchedules));
}
#endif
