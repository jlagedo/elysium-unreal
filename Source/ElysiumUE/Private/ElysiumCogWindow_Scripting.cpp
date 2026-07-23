#include "ElysiumCogWindow_Scripting.h"

#if ENABLE_COG

#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumExpr.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumPythonVM.h"
#include "ElysiumScriptHost.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

namespace
{
	const ImVec4 GColorGood(0.45f, 0.85f, 0.45f, 1.f);
	const ImVec4 GColorBad(1.f, 0.40f, 0.35f, 1.f);
	const ImVec4 GColorName(0.60f, 0.80f, 1.00f, 1.f);
	const ImVec4 GColorDim(0.65f, 0.65f, 0.65f, 1.f);
}

void FElysiumCogWindow_Scripting::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Scripting::RenderHelp()
{
	ImGui::Text(
		"VtMB's story runs on loose plain-text Python 2.1 level scripts (out/scripts) and dlgexpr "
		"dialogue (out/dlg), copied verbatim from the install by tools/UE_extract_scripts.py.\n\n"
		"5.1 (top): a read-only delivery pre-flight — confirms the mirror is on disk and resolves the "
		"current map's worldspawn.levelscript module.\n\n"
		"5.2 (runtime): the expression evaluator + the G flag store. Type an expression and Eval it "
		"(G.Tut_Elev, 1+2, ent.field) or a statement and Exec it (G.Tut_Elev = 1). Poke the G flags "
		"directly in the table.\n\n"
		"5.4: live script eval is ON by default — field-6 payloads (mostly G.x = ... on this map), "
		"logic_pythoncheck gates (Test -> OnTrue/OnFalse), and ScheduleTask(delay, \"src\") deferred "
		"sources all run during play. Watch the flags flip; deferred tasks appear in the Event Queue "
		"window as '(python)' rows. Un-check 'Live script eval' to swap in the null host (everything goes "
		"dark) for A/B.\n\n"
		"5.3 (native bindings): the engine 'vampire' module — 11 globals + 24 Character methods. "
		"FindPlayer() returns the PC; FindPlayer().RemoveItem(...) and friends dispatch and log (most are "
		"stubs until their backing systems land; SetQuest/GetQuestState route to the quest map). The "
		"'Native bindings' table lists the surface + live call counts; 'Recent native calls' logs each "
		"dispatch. Level-script functions/constants (cCelerity, the level's On* callbacks) are still 5.5 "
		"— those names read as errors (error-to-false) until then.");
}

void FElysiumCogWindow_Scripting::Rescan()
{
	IFileManager& FM = IFileManager::Get();
	const FString ScriptsDir = FElysiumContentPaths::ScriptsDir();
	const FString DlgDir = FElysiumContentPaths::DlgDir();

	bScriptsDirExists = FM.DirectoryExists(*ScriptsDir);
	bDlgDirExists = FM.DirectoryExists(*DlgDir);

	TArray<FString> Files;
	FM.FindFilesRecursive(Files, *ScriptsDir, TEXT("*.py"), true, false);   // clears Files
	PyCount = Files.Num();
	FM.FindFilesRecursive(Files, *DlgDir, TEXT("*.dlg"), true, false);      // clears Files
	DlgCount = Files.Num();

	bScanned = true;
}

FString FElysiumCogWindow_Scripting::CurrentLevelScript() const
{
	const FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return FString();
	}
	for (const TUniquePtr<FElysiumEntity>& EPtr : World->Entities())
	{
		const FElysiumEntity* E = EPtr.Get();
		if (E != nullptr && E->Def != nullptr
			&& E->Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
		{
			return E->Def->Keys.FindRef(TEXT("levelscript"));   // "" if worldspawn has no such key
		}
	}
	return FString();
}

void FElysiumCogWindow_Scripting::RenderContent()
{
	Super::RenderContent();

	if (!bScanned)
	{
		Rescan();
	}

	// --- Mirror presence (out/scripts + out/dlg) ------------------------------------------
	ImGui::SeparatorText("Script + dialogue mirror");
	const auto MirrorLine = [](const char* Label, bool bExists, int32 Count, const char* Ext,
		const FString& Dir)
	{
		ImGui::Text("%-8s", Label);
		ImGui::SameLine();
		if (bExists)
		{
			ImGui::TextColored(GColorGood, "%d %s present", Count, Ext);
		}
		else
		{
			ImGui::TextColored(GColorBad, "missing — run tools/UE_extract_scripts.py");
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Dir));
		}
	};
	MirrorLine("scripts", bScriptsDirExists, PyCount, ".py", FElysiumContentPaths::ScriptsDir());
	MirrorLine("dlg", bDlgDirExists, DlgCount, ".dlg", FElysiumContentPaths::DlgDir());
	if (ImGui::SmallButton("Rescan"))
	{
		Rescan();
	}

	// --- Current map's level script -------------------------------------------------------
	ImGui::SeparatorText("This map's level script");
	const AElysiumMapActor* Map = GetMapActor();
	if (Map == nullptr)
	{
		ImGui::TextDisabled("No map loaded.");
	}
	else
	{
		ImGui::Text("map: %s", COG_TCHAR_TO_CHAR(*Map->LoadedMap));
		const FString Module = CurrentLevelScript();
		if (Module.IsEmpty())
		{
			ImGui::TextDisabled("worldspawn has no levelscript key on this map.");
		}
		else
		{
			ImGui::Text("worldspawn.levelscript =");
			ImGui::SameLine();
			ImGui::TextColored(GColorName, "\"%s\"", COG_TCHAR_TO_CHAR(*Module));

			const FString File = FElysiumContentPaths::ScriptModuleFile(Module);
			const bool bExists = IFileManager::Get().FileExists(*File);
			const FString Rel = FString::Printf(TEXT("scripts/%s/%s.py"), *Module, *Module);
			ImGui::Text("  -> %s", COG_TCHAR_TO_CHAR(*Rel));
			ImGui::SameLine();
			ImGui::TextColored(bExists ? GColorGood : GColorBad, bExists ? "[present]" : "[MISSING]");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*File));
			}

			// 9.3 — the map load imports this automatically; report what actually happened.
			if (const UElysiumGameStateSubsystem* State = GetGameState())
			{
				ImGui::Text("  import:");
				ImGui::SameLine();
				if (State->IsLevelScriptLoaded())
				{
					ImGui::TextColored(GColorGood, "loaded at map load into host '%s'",
						COG_TCHAR_TO_CHAR(State->ScriptHost().Name()));
				}
				else
				{
					ImGui::TextColored(GColorBad, "NOT loaded — %s",
						COG_TCHAR_TO_CHAR(*State->LevelScriptError()));
				}
			}
		}
	}

	// --- 5.5 embedded CPython VM ----------------------------------------------------------
	RenderCPythonPanel();

	// --- 5.2 runtime ----------------------------------------------------------------------
	RenderEvalPanel();
	RenderGStore();
	RenderRecentEvals();

	// --- 5.3 native bindings --------------------------------------------------------------
	RenderNativeBindings();
	RenderRecentNativeCalls();
}

void FElysiumCogWindow_Scripting::RenderCPythonPanel()
{
	ImGui::SeparatorText("Embedded CPython 2.7 (5.5)");

	FElysiumPythonVM& VM = FElysiumPythonVM::Get();
	UElysiumGameStateSubsystem* State = GetGameState();

	if (!FElysiumPythonVM::IsAvailable())
	{
		ImGui::TextColored(GColorBad, "Built without CPython (ELYSIUM_WITH_CPYTHON=0).");
		ImGui::TextColored(GColorDim, "Win64 + the vendored ThirdParty/CPython27 SDK are required.");
		return;
	}

	// Status line + start button.
	if (VM.IsStarted())
	{
		ImGui::Text("VM:");
		ImGui::SameLine();
		ImGui::TextColored(GColorGood, "started");
		ImGui::SameLine();
		ImGui::TextColored(GColorDim, "%s", COG_TCHAR_TO_CHAR(*VM.GetVersion()));
	}
	else
	{
		ImGui::Text("VM:");
		ImGui::SameLine();
		ImGui::TextColored(GColorDim, "not started");
		ImGui::SameLine();
		if (ImGui::SmallButton("Start"))
		{
			if (State) { VM.SetGameState(State); }
			FString Err;
			if (!VM.EnsureStarted(Err))
			{
				LastPySource = TEXT("(start)");
				LastPyResult = Err;
				bLastPyError = true;
			}
		}
	}

	// Active script host (expr vs cpython) — the field-6 / pythoncheck routing.
	if (State)
	{
		const TCHAR* HostName = State->ScriptHost().Name();
		ImGui::Text("Field-6 host:");
		ImGui::SameLine();
		const bool bCPython = FCString::Strcmp(HostName, TEXT("cpython")) == 0;
		ImGui::TextColored(bCPython ? GColorGood : GColorName, "%s", COG_TCHAR_TO_CHAR(HostName));
		ImGui::SameLine();
		if (ImGui::SmallButton(bCPython ? "Use ElysiumExpr" : "Use CPython"))
		{
			if (bCPython) { State->SetScriptHost(MakeUnique<FElysiumExprScriptHost>(State)); }
			else          { State->SetScriptHost(MakeUnique<FElysiumCPythonScriptHost>(State)); }
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Route field-6 + logic_pythoncheck through the real CPython VM (resolves\n"
				"level-script names like cCelerity) or the ElysiumExpr evaluator (expression-only).");
		}
	}

	// Re-load a level script by hand. The map load already imports this map's own module (9.3);
	// this is for pulling in a different map's script, or re-importing after editing one on disk.
	ImGui::Spacing();
	const FString MapModule = CurrentLevelScript();
	const FString HintModule = MapModule.IsEmpty() ? TEXT("tutorial") : MapModule;
	FCogWidgets::InputTextWithHint("##PyLoad", COG_TCHAR_TO_CHAR(*HintModule), PyLoadTarget);
	ImGui::SameLine();
	if (ImGui::Button("Load level script"))
	{
		const FString Module = PyLoadTarget.TrimStartAndEnd().IsEmpty()
			? HintModule : PyLoadTarget.TrimStartAndEnd();
		LastPySource = FString::Printf(TEXT("load %s"), *Module);
		if (State)
		{
			// Through the subsystem, so the tracked module + outcome above stay truthful.
			bLastPyError = !State->LoadLevelScript(Module);
			LastPyResult = bLastPyError ? State->LevelScriptError()
				: FString::Printf(TEXT("imported module '%s'"), *Module);
		}
		else
		{
			const FString Abs = FPaths::ConvertRelativePathToFull(FElysiumContentPaths::ScriptModuleFile(Module));
			FString ModName, Err;
			bLastPyError = !VM.LoadLevelScript(Abs, ModName, Err);
			LastPyResult = bLastPyError ? Err : FString::Printf(TEXT("imported module '%s'"), *ModName);
		}
	}
	ImGui::SameLine();
	ImGui::TextColored(GColorDim, "(tools/out/scripts/<name>/<name>.py)");

	const FString Loaded = VM.GetLoadedModule();
	if (!Loaded.IsEmpty())
	{
		ImGui::Text("loaded module:");
		ImGui::SameLine();
		ImGui::TextColored(GColorName, "%s", COG_TCHAR_TO_CHAR(*Loaded));

		// The On* callbacks, each with a Fire button.
		CallableFilter.Draw("Filter callbacks", GetDpiScale() * 160.0f);
		const TArray<FString> Callables = VM.GetModuleCallables(TEXT(""));
		int32 Shown = 0;
		if (ImGui::BeginChild("##Callables", ImVec2(0, GetDpiScale() * 150.0f), true))
		{
			for (const FString& Fn : Callables)
			{
				if (!CallableFilter.PassFilter(COG_TCHAR_TO_CHAR(*Fn)))
				{
					continue;
				}
				ImGui::PushID(Shown);
				if (ImGui::SmallButton("Fire"))
				{
					FString Err;
					const bool bOk = VM.FireCallback(Fn, Err);
					LastPySource = Fn + TEXT("()");
					LastPyResult = bOk ? TEXT("ok") : Err;
					bLastPyError = !bOk;
				}
				ImGui::PopID();
				ImGui::SameLine();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Fn));
				++Shown;
			}
		}
		ImGui::EndChild();
		ImGui::TextColored(GColorDim, "%d callables", Callables.Num());
	}

	// A python exec box (runs against the loaded namespace, else __main__).
	ImGui::Spacing();
	FCogWidgets::InputTextWithHint("##PyExec", "G.Tut_Jack = cCelerity   |   FindPlayer()   |   G.keys()", PyExecInput);
	ImGui::SameLine();
	ImGui::BeginDisabled(PyExecInput.TrimStartAndEnd().IsEmpty());
	if (ImGui::Button("Run (python)"))
	{
		if (State) { VM.SetGameState(State); }
		const FString Src = PyExecInput.TrimStartAndEnd();
		FString Err;
		const FElysiumVariant R = VM.Eval(Src, FElysiumScriptContext(), Err);
		LastPySource = Src;
		bLastPyError = !Err.IsEmpty();
		LastPyResult = bLastPyError ? Err : R.Describe();
	}
	ImGui::EndDisabled();

	if (!LastPySource.IsEmpty())
	{
		ImGui::TextColored(GColorName, "%s", COG_TCHAR_TO_CHAR(*LastPySource));
		ImGui::SameLine();
		ImGui::TextUnformatted("->");
		ImGui::SameLine();
		ImGui::TextColored(bLastPyError ? GColorBad : GColorGood, "%s", COG_TCHAR_TO_CHAR(*LastPyResult));
	}
}

void FElysiumCogWindow_Scripting::RenderEvalPanel()
{
	ImGui::SeparatorText("Evaluator (5.2)");

	UElysiumGameStateSubsystem* State = GetGameState();
	if (State == nullptr)
	{
		ImGui::TextDisabled("Game-state subsystem unavailable.");
		return;
	}

	// Live script-eval toggle (on by default as of 5.4; off swaps in the null host for A/B).
	bool bLive = State->IsLiveScriptEval();
	if (ImGui::Checkbox("Live script eval", &bLive))
	{
		State->SetLiveScriptEval(bLive);
	}
	ImGui::SameLine();
	ImGui::TextColored(GColorDim, "(on: field-6 + pythoncheck + ScheduleTask run; off = null host)");

	const bool bHaveWorld = GetEntityWorld() != nullptr;
	if (!bHaveWorld)
	{
		ImGui::TextColored(GColorDim, "No map loaded — G reads/writes work; entity names won't resolve.");
	}

	// The eval input box. Expression and statement shapes both run down one path — the installed
	// host (UElysiumGameStateSubsystem::EvalScript), so this resolves what a field-6 payload does.
	FCogWidgets::InputTextWithHint("##EvalInput", "G.Tut_Elev = 1   |   1 + 2   |   G.Story_State", EvalInput);
	ImGui::BeginDisabled(EvalInput.TrimStartAndEnd().IsEmpty());
	if (ImGui::Button("Run"))
	{
		const FString Src = EvalInput.TrimStartAndEnd();
		FString Err;
		const FElysiumVariant R = State->EvalScript(Src, Err);
		LastEvalSource = Src;
		bLastEvalError = !Err.IsEmpty();
		LastEvalResult = bLastEvalError ? Err : R.Describe();
	}
	ImGui::SameLine();
	ImGui::TextColored(GColorDim, "(host: %s)", COG_TCHAR_TO_CHAR(State->ScriptHost().Name()));
	ImGui::EndDisabled();

	if (!LastEvalSource.IsEmpty())
	{
		ImGui::TextUnformatted("eval");
		ImGui::SameLine();
		ImGui::TextColored(GColorName, "%s", COG_TCHAR_TO_CHAR(*LastEvalSource));
		ImGui::SameLine();
		ImGui::TextUnformatted("->");
		ImGui::SameLine();
		ImGui::TextColored(bLastEvalError ? GColorBad : GColorGood, "%s", COG_TCHAR_TO_CHAR(*LastEvalResult));
	}
}

void FElysiumCogWindow_Scripting::RenderGStore()
{
	UElysiumGameStateSubsystem* State = GetGameState();
	if (State == nullptr)
	{
		return;
	}

	const FElysiumGlobalMap& Globals = State->GetGlobals();
	ImGui::SeparatorText("G store");
	ImGui::Text("%d flags set", Globals.Num());
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear all"))
	{
		State->ClearAllGlobals();
	}

	GFilter.Draw("Filter", GetDpiScale() * 160.0f);

	// Sorted, filtered flag table. Int/Bool flags get an inline editor (the overwhelming shape);
	// other types show read-only. The × button deletes the key (None-assign semantics).
	TArray<FString> Keys;
	Globals.GetKeys(Keys);
	Keys.Sort();

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##GStore", 4, TableFlags, ImVec2(0, GetDpiScale() * 180.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Flag");
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 120.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 24.0f);
		ImGui::TableHeadersRow();

		for (const FString& Key : Keys)
		{
			if (!GFilter.PassFilter(COG_TCHAR_TO_CHAR(*Key)))
			{
				continue;
			}
			const FElysiumVariant V = State->GetGlobal(Key);

			ImGui::TableNextRow();
			ImGui::PushID(COG_TCHAR_TO_CHAR(*Key));

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Key));

			ImGui::TableNextColumn();
			ImGui::TextColored(GColorDim, "%s", V.IsBool() ? "bool" : V.IsInt() ? "int"
				: V.IsFloat() ? "float" : V.IsString() ? "str" : "?");

			ImGui::TableNextColumn();
			if (V.IsInt() || V.IsBool())
			{
				int32 Val = V.ToInt();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputInt("##v", &Val, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					State->SetGlobal(Key, FElysiumVariant::Int(Val));
				}
			}
			else
			{
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*V.ToString()));
			}

			ImGui::TableNextColumn();
			if (ImGui::SmallButton("x"))
			{
				State->ClearGlobal(Key);
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// Add / set a flag by hand (mirrors the elysium.g verb: numeric -> int, "none" -> delete, else str).
	ImGui::SetNextItemWidth(GetDpiScale() * 150.0f);
	FCogWidgets::InputTextWithHint("##NewName", "flag name", NewFlagName);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(GetDpiScale() * 90.0f);
	FCogWidgets::InputTextWithHint("##NewVal", "value", NewFlagValue);
	ImGui::SameLine();
	ImGui::BeginDisabled(NewFlagName.TrimStartAndEnd().IsEmpty());
	if (ImGui::Button("Set"))
	{
		const FString FlagName = NewFlagName.TrimStartAndEnd();
		const FString Raw = NewFlagValue.TrimStartAndEnd();
		if (Raw.IsEmpty() || Raw.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			State->ClearGlobal(FlagName);
		}
		else if (Raw.IsNumeric())
		{
			State->SetGlobalInt(FlagName, FCString::Atoi(*Raw));
		}
		else
		{
			State->SetGlobal(FlagName, FElysiumVariant::String(Raw));
		}
		NewFlagName.Reset();
		NewFlagValue.Reset();
	}
	ImGui::EndDisabled();
}

void FElysiumCogWindow_Scripting::RenderNativeBindings()
{
	UElysiumGameStateSubsystem* State = GetGameState();

	ImGui::SeparatorText("Native bindings (5.3)");
	ImGui::TextColored(GColorDim,
		"The engine 'vampire' module: 11 globals + 24 Character methods. Most log a stub (no backing "
		"system yet); SetQuest/GetQuestState route to the quest map. FindPlayer() returns the PC.");

	NativeFilter.Draw("Filter", GetDpiScale() * 160.0f);

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##NativeBindings", 4, TableFlags, ImVec2(0, GetDpiScale() * 190.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 64.0f);
		ImGui::TableSetupColumn("Status");
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		ImGui::TableHeadersRow();

		for (const ElysiumExpr::FNativeBinding& B : ElysiumExpr::NativeBindings())
		{
			if (!NativeFilter.PassFilter(COG_TCHAR_TO_CHAR(B.Name)))
			{
				continue;
			}
			const int32 Calls = State ? State->NativeCallCount(FName(B.Name)) : 0;
			const bool bStub = FCString::Strstr(B.Status, TEXT("stub")) != nullptr;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(GColorName, "%s", COG_TCHAR_TO_CHAR(B.Name));
			ImGui::TableNextColumn();
			ImGui::TextColored(GColorDim, "%s", B.bMethod ? "method" : "global");
			ImGui::TableNextColumn();
			ImGui::TextColored(bStub ? GColorDim : GColorGood, "%s", COG_TCHAR_TO_CHAR(B.Status));
			ImGui::TableNextColumn();
			if (Calls > 0) { ImGui::TextColored(GColorGood, "%d", Calls); }
			else { ImGui::TextDisabled("0"); }
		}
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Scripting::RenderRecentNativeCalls()
{
	UElysiumGameStateSubsystem* State = GetGameState();
	if (State == nullptr)
	{
		return;
	}

	const TArray<FElysiumNativeCallRecord>& History = State->RecentNativeCalls();
	ImGui::SeparatorText("Recent native calls");
	ImGui::Text("%d recorded", History.Num());
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear##native"))
	{
		State->ClearNativeCallHistory();
	}
	if (History.Num() == 0)
	{
		ImGui::TextDisabled("No FindPlayer()/Character calls yet. Arm 'live field-6' or type one in the eval box.");
		return;
	}

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##RecentNativeCalls", 3, TableFlags, ImVec2(0, GetDpiScale() * 150.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 60.0f);
		ImGui::TableSetupColumn("Call");
		ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 130.0f);
		ImGui::TableHeadersRow();

		// Newest first.
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			const FElysiumNativeCallRecord& Rec = History[i];
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(GColorDim, "%.2f", Rec.Time);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rec.Call));
			ImGui::TableNextColumn();
			ImGui::TextColored(Rec.bStub ? GColorDim : GColorGood, "%s", COG_TCHAR_TO_CHAR(*Rec.Result));
		}
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Scripting::RenderRecentEvals()
{
	UElysiumGameStateSubsystem* State = GetGameState();
	if (State == nullptr)
	{
		return;
	}

	const TArray<FElysiumEvalRecord>& History = State->RecentEvals();
	ImGui::SeparatorText("Recent evaluations");
	ImGui::Text("%d recorded", History.Num());
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear log"))
	{
		State->ClearEvalHistory();
	}
	if (History.Num() == 0)
	{
		ImGui::TextDisabled("No field-6 or hand-run evaluations yet.");
		return;
	}

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##RecentEvals", 3, TableFlags, ImVec2(0, GetDpiScale() * 150.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 60.0f);
		ImGui::TableSetupColumn("Source");
		ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 150.0f);
		ImGui::TableHeadersRow();

		// Newest first.
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			const FElysiumEvalRecord& Rec = History[i];
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(GColorDim, "%.2f", Rec.Time);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rec.Source));
			ImGui::TableNextColumn();
			ImGui::TextColored(Rec.bError ? GColorBad : GColorGood, "%s", COG_TCHAR_TO_CHAR(*Rec.Result));
		}
		ImGui::EndTable();
	}
}

#endif // ENABLE_COG
