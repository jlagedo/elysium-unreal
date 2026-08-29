#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// Two layers:
//   * Delivery pre-flight (read-only): confirms the loose Python 2.1 level scripts (out/scripts)
//     + dlgexpr dialogue (out/dlg) are mirrored to disk and resolves the current map's
//     worldspawn.levelscript to its module file.
//   * Runtime: the live `G` flag store (read / poke / add / clear), the expression evaluator's
//     eval + exec box (run any script expression/statement against the current map), an opt-in
//     "live field-6" toggle that swaps the real evaluator onto the output path, and a recent-eval
//     log. Native module globals + level-script functions that are not bound yet read as errors
//     (error-to-false).
class FElysiumCogWindow_Scripting : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	void Rescan();                        // recount the on-disk out/scripts + out/dlg mirror
	FString CurrentLevelScript() const;   // this map's worldspawn.levelscript value, or empty

	void RenderEvalPanel();               // live toggle + eval/exec input box + last result
	void RenderGStore();                  // the G flag table + add/clear
	void RenderRecentEvals();             // the recent-eval history log

	void RenderNativeBindings();          // the vampire-module surface table (globals + Character methods)
	void RenderRecentNativeCalls();       // the recent native-call log

	// The real CPython 2.7 VM — status/version, host toggle (expr <-> cpython), load a real
	// level script, list + fire its On* callbacks, and a python exec box. The G table
	// (RenderGStore) + recent-eval log below reflect its evals too.
	void RenderCPythonPanel();

	ImGuiTextFilter NativeFilter;         // filters the native-binding surface table

	bool bScanned = false;
	bool bScriptsDirExists = false;       // out/scripts present
	bool bDlgDirExists = false;           // out/dlg present
	int32 PyCount = 0;                    // .py under out/scripts (recursive)
	int32 DlgCount = 0;                   // .dlg under out/dlg (recursive)

	FString EvalInput;                    // the expression/statement in the input box
	FString LastEvalSource;               // the last run source (for the result line)
	FString LastEvalResult;               // Describe() of the value, or the error text
	bool bLastEvalError = false;          // colour the result line

	ImGuiTextFilter GFilter;              // filters the G flag table
	FString NewFlagName;                  // add-flag row: name
	FString NewFlagValue;                 // add-flag row: value (int if numeric, else string; "none" deletes)

	FString PyLoadTarget = TEXT("tutorial"); // which level script to load (dir/module name)
	FString PyExecInput;                  // the python exec box
	FString LastPySource;                 // last exec'd source (result line)
	FString LastPyResult;                 // Describe() of the value, or the error text
	bool bLastPyError = false;            // colour the result line
	ImGuiTextFilter CallableFilter;       // filters the loaded module's callable list
};

#endif // ENABLE_COG
