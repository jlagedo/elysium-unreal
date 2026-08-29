#include "Debug/ElysiumWireDump.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumWireReport.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWires, Log, All);

namespace
{
	// JSON string escaping. Deliberately not FString::ReplaceCharWithEscapedChar, which also escapes
	// the single quote — legal C, illegal JSON. Targetnames are authored data and a trailing-`*`
	// pattern or a Python payload can carry anything, so this is the whole minimal set plus the
	// control range.
	FString JsonString(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 2);
		for (const TCHAR Ch : In)
		{
			switch (Ch)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Ch < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), (uint32)Ch);
				}
				else
				{
					Out.AppendChar(Ch);
				}
				break;
			}
		}
		return Out;
	}

	const TCHAR* JsonBool(bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }

	// The live entity world, or null when no map is loaded. Same walk the MCP tools and the ent_*
	// verbs take; duplicated rather than exported because it is three dereferences and the Debug
	// layer must not grow a shared back door into the map actor.
	FElysiumEntityWorld* CurrentWorld(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		if (const UGameInstance* GI = World->GetGameInstance())
		{
			if (UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
			{
				if (AElysiumMapActor* Map = Maps->GetCurrentMap())
				{
					return Map->GetEntityWorld();
				}
			}
		}
		return nullptr;
	}
}

namespace ElysiumWireDump
{
	FString BuildJson(const FElysiumEntityWorld& World)
	{
		TArray<FElysiumWireReportRow> Rows;
		World.BuildWireReport(Rows);
		const FElysiumWireSummary Summary = ElysiumWireSummarize(Rows);

		FString Json;
		Json.Reserve(256 * (Rows.Num() + 8));
		Json += TEXT("{\n");
		Json += FString::Printf(TEXT("  \"map\": \"%s\",\n"), *JsonString(World.MapName()));
		Json += FString::Printf(TEXT("  \"now\": %.3f,\n"), World.NowSeconds());
		Json += TEXT("  \"summary\": {\n");
		Json += FString::Printf(TEXT("    \"authored\": %d,\n"), Summary.Authored);
		Json += FString::Printf(TEXT("    \"runtime_rows\": %d,\n"), Summary.RuntimeRows);
		Json += FString::Printf(TEXT("    \"fired\": %d,\n"), Summary.Fired);
		Json += FString::Printf(TEXT("    \"fully_delivered\": %d,\n"), Summary.FullyDelivered);
		Json += FString::Printf(TEXT("    \"never_fired\": %d,\n"), Summary.NeverFired);
		Json += FString::Printf(TEXT("    \"exhausted\": %d,\n"), Summary.Exhausted);
		Json += FString::Printf(TEXT("    \"unknown_target\": %d,\n"), Summary.UnknownTarget);
		Json += FString::Printf(TEXT("    \"unknown_input\": %d,\n"), Summary.UnknownInput);
		Json += FString::Printf(TEXT("    \"pending\": %d,\n"), Summary.Pending);
		Json += FString::Printf(TEXT("    \"python_rows\": %d,\n"), Summary.PythonRows);
		Json += FString::Printf(TEXT("    \"python_forwarded\": %d\n"), Summary.PythonForwarded);
		Json += TEXT("  },\n");
		Json += TEXT("  \"wires\": [\n");

		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FElysiumWireReportRow& R = Rows[i];
			Json += TEXT("    {");
			Json += FString::Printf(TEXT("\"entity\": %d, "), R.Wire.SourceIndex);
			Json += FString::Printf(TEXT("\"name\": \"%s\", "), *JsonString(R.SourceName));
			Json += FString::Printf(TEXT("\"classname\": \"%s\", "), *JsonString(R.SourceClass));
			Json += FString::Printf(TEXT("\"output\": \"%s\", "), *JsonString(R.Wire.Output.ToString()));
			// `row` is the position in the def's whole Outputs array (the runtime key); `output_row`
			// is the position among the rows of THIS output, which is how an offline enumeration
			// counts them. Both are stated so the join needs neither to be re-derived.
			Json += FString::Printf(TEXT("\"row\": %d, "), R.Wire.Row);
			Json += FString::Printf(TEXT("\"output_row\": %d, "), R.OutputRow);
			Json += FString::Printf(TEXT("\"target\": \"%s\", "), *JsonString(R.Target));
			Json += FString::Printf(TEXT("\"input\": \"%s\", "), *JsonString(R.Input));
			Json += FString::Printf(TEXT("\"param\": \"%s\", "), *JsonString(R.Param));
			Json += FString::Printf(TEXT("\"delay\": %.3f, "), R.Delay);
			Json += FString::Printf(TEXT("\"times\": %d, "), R.AuthoredTimes);
			Json += FString::Printf(TEXT("\"python\": %s, "), JsonBool(R.HasPython()));
			Json += FString::Printf(TEXT("\"runtime_source\": %s, "), JsonBool(R.bRuntimeSource));
			Json += FString::Printf(TEXT("\"outcome\": \"%s\", "),
				ElysiumWireOutcomeName(ElysiumWireOutcome(R)));
			Json += FString::Printf(
				TEXT("\"fired\": %d, \"times_exhausted\": %d, \"delivered\": %d, ")
				TEXT("\"unknown_target\": %d, \"unknown_input\": %d, \"python_forwarded\": %d"),
				R.Tally.Fired, R.Tally.TimesExhausted, R.Tally.Delivered,
				R.Tally.UnknownTarget, R.Tally.UnknownInput, R.Tally.PythonForwarded);
			Json += (i + 1 < Rows.Num()) ? TEXT("},\n") : TEXT("}\n");
		}

		Json += TEXT("  ]\n");
		Json += TEXT("}\n");
		return Json;
	}

	FString Summarize(const FElysiumEntityWorld& World)
	{
		TArray<FElysiumWireReportRow> Rows;
		World.BuildWireReport(Rows);
		const FElysiumWireSummary S = ElysiumWireSummarize(Rows);
		return FString::Printf(
			TEXT("%d authored wires: %d fired, %d fully delivered, %d never fired, ")
			TEXT("%d unknown target, %d unknown input"),
			S.Authored, S.Fired, S.FullyDelivered, S.NeverFired, S.UnknownTarget, S.UnknownInput);
	}

	FString Write(const FElysiumEntityWorld& World)
	{
		const FString Root = FElysiumContentPaths::Root();
		if (Root.IsEmpty())
		{
			return FString();
		}
		const FString Map = World.MapName().IsEmpty() ? TEXT("unknown") : World.MapName();
		const FString Path = Root / TEXT("_wires") / (Map + TEXT(".json"));
		return FFileHelper::SaveStringToFile(BuildJson(World), *Path) ? Path : FString();
	}
}

// The verb.
// `elysium.wires` is the acceptance instrument's read-out: it dumps every authored wire in the live
// map with what that wire did, and prints the one line that says whether the map's event surface is
// working. `clear` restarts the measurement without reloading, so one scene or one beat can be
// measured on its own — the same shape as `elysium.stubs clear`.

static FAutoConsoleCommandWithWorldAndArgs GElysiumWiresCmd(
	TEXT("elysium.wires"),
	TEXT("elysium.wires [clear] — dump per-wire I/O accounting for the live map to _wires/<map>.json "
	     "and print the summary. 'clear' resets the tally without reloading."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* EW = CurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWires, Warning, TEXT("elysium.wires: no live world (load a map first)"));
			return;
		}

		if (Args.Num() >= 1 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			EW->ResetWireTallies();
			UE_LOG(LogElysiumWires, Display, TEXT("wire tally cleared"));
			return;
		}

		UE_LOG(LogElysiumWires, Display, TEXT("%s"), *ElysiumWireDump::Summarize(*EW));

		const FString Path = ElysiumWireDump::Write(*EW);
		if (Path.IsEmpty())
		{
			UE_LOG(LogElysiumWires, Warning,
				TEXT("elysium.wires: could not write the report (no content root, or the write failed)"));
			return;
		}
		UE_LOG(LogElysiumWires, Display, TEXT("wrote %s"), *Path);
	}));
