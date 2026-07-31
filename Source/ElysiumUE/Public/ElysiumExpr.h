#pragma once

#include "CoreMinimal.h"
#include "ElysiumScriptHost.h"   // FElysiumScriptContext (Self / Activator / World)
#include "ElysiumVariant.h"

class UElysiumGameStateSubsystem;

// P5 5.2 — the runtime expression evaluator. A self-contained recursive-descent parser + AST +
// tree-walking interpreter over the restricted Python-2.1 expression subset VtMB scripts speak
// (field-6 call strings, logic_pythoncheck gates, and — later — dlgexpr, 9.1). It is the one
// evaluator; there is no second dispatch mechanism.
//
// The binding surface (`docs/vtmb/python_bridge.md`):
//   * `G.<flag>`      -> the game-state global bag, default-on-miss integer 0 (decompiled tp_getattr).
//                        `G.<flag> = <expr>` writes it; assigning `None` deletes the key (tp_setattr).
//   * a bare name     -> an entity resolved by targetname against the current entity world.
//   * `<ent>.<name>`  -> ONE namespace, no new dispatch: the name is looked up in the class-chain
//                        input table (-> a bound callable that fires the input) then the field table
//                        (-> the marshalled field value), exactly as VtMB's Entity.__getattr__ walks
//                        the datamap. `<ent>.<field> = <expr>` writes a keyable field (__setattr__).
//
// The engine `vampire`-module surface (5.3): the 11 native module globals (`FindPlayer`,
// `FindEntityByName`, ...) resolve as bare names, and the 24 Character methods dispatch off a
// character object (`FindPlayer()` -> the PC; an NPC entity handle also accepts them). Most have no
// backing system yet, so they log a stub and return a sensible default; the two that exist route
// for real (`SetQuest`/`GetQuestState` -> the quest map). `self`/`activator` resolve to the
// evaluation's I/O provenance when set. Level-script functions and constants (cCelerity,
// DialogPostProcess, the level's `On*` callbacks, ...) arrive with the level-script host (5.5);
// until then those names are unresolved -> NameError, so `G.x |= cCelerity` still no-ops correctly.
//
// Everything is total: the evaluator NEVER throws. A parse error, a NameError, a type error, or a
// divide-by-zero aborts the current statement, records the message on the env, and yields Void —
// this is the confirmed error-to-false behaviour (RE3) the pythoncheck/dialogue paths rely on.
namespace ElysiumExpr
{
	// The evaluation environment — the collaborators + the out-of-band diagnostic. `Ctx` carries the
	// I/O provenance the evaluation runs against (`!self`/`!activator`, 5.3) and the entity world to
	// resolve targetnames + dispatch inputs against; `State` is the `G`/quest store. Both may be null
	// (a worldless or stateless probe eval) — resolution simply fails to those names then.
	struct FEnv
	{
		UElysiumGameStateSubsystem* State = nullptr;
		FElysiumScriptContext Ctx;

		// Set true when evaluation could not complete; `Error` carries a one-line reason. The result
		// is Void in that case (error-to-false). Reset per Eval/Exec call.
		bool bError = false;
		FString Error;
	};

	// Evaluate SOURCE as a single expression and return its value (Void on error). Rejects a
	// statement (an assignment) as a syntax error — mirrors Python `eval()` vs `exec()`. This is the
	// logic_pythoncheck / dlg-condition path (truthiness via FElysiumVariant::ToBool).
	FElysiumVariant Eval(const FString& Source, FEnv& Env);

	// Execute SOURCE as one or more `;`-separated simple statements (an assignment or a bare
	// expression) and return the last statement's value. This is the field-6 / dlg-action path.
	// A statement that errors aborts the rest of the sequence (error-to-false).
	FElysiumVariant Exec(const FString& Source, FEnv& Env);
}
