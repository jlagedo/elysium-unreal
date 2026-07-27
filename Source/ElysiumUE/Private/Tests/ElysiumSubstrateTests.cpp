// P2.8 — the content-free tier. Everything here runs under -nullrhi with no exported maps: the
// marshalling currency, the expression evaluator's error-to-false contract, the KeyValues reader,
// the event queue's ordering, the class registry's chain walk, and one end-to-end I/O chain driven
// through the real chokepoints on a bare entity world (logic entities only — no bodies, no PIE).
//
// These are inside the module (not a separate test module) because the substrate types carry no
// ELYSIUMUE_API export macros — a same-module test links their symbols directly. Tests compile only
// where the automation framework is present (WITH_DEV_AUTOMATION_TESTS), i.e. Editor/Development.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAppState.h"
#include "ElysiumBinds.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "Debug/ElysiumConsole.h"
#include "Visual/ElysiumDecals.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumExpr.h"
#include "ElysiumGameClock.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMapActor.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "ElysiumPlayer.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumRulebook.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Serialization/MemoryWriter.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// FElysiumVariant — the tagged value all four chokepoints marshal through.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumVariantTest, "Elysium.Substrate.Variant", GElysiumTestFlags)
bool FElysiumVariantTest::RunTest(const FString&)
{
	// Void is the falsy "no value" case (also how error-to-false surfaces).
	TestFalse(TEXT("Void is falsy"), FElysiumVariant::Void().ToBool());
	TestTrue(TEXT("Void.IsVoid"), FElysiumVariant::Void().IsVoid());

	// Scalar truthiness follows C rules.
	TestTrue(TEXT("Int(1) truthy"), FElysiumVariant::Int(1).ToBool());
	TestFalse(TEXT("Int(0) falsy"), FElysiumVariant::Int(0).ToBool());
	TestFalse(TEXT("Float(0) falsy"), FElysiumVariant::Float(0.0f).ToBool());
	TestFalse(TEXT("empty String falsy"), FElysiumVariant::String(TEXT("")).ToBool());
	TestTrue(TEXT("non-empty String truthy"), FElysiumVariant::String(TEXT("x")).ToBool());

	// Objects are truthy unless empty/unbound (Python object semantics).
	TestFalse(TEXT("zero Vector falsy"), FElysiumVariant::Vector(FVector::ZeroVector).ToBool());
	TestTrue(TEXT("non-zero Vector truthy"), FElysiumVariant::Vector(FVector(1, 0, 0)).ToBool());
	TestFalse(TEXT("unbound Handle falsy"), FElysiumVariant::Handle(FElysiumEntityHandle::Invalid()).ToBool());

	// Total coercions (never fail).
	TestEqual(TEXT("String->Int"), FElysiumVariant::String(TEXT("42")).ToInt(), 42);
	TestEqual(TEXT("Float->Int truncates"), FElysiumVariant::Float(3.9f).ToInt(), 3);
	TestEqual(TEXT("Bool->Int"), FElysiumVariant::Bool(true).ToInt(), 1);
	TestEqual(TEXT("Int->String"), FElysiumVariant::Int(7).ToString(), FString(TEXT("7")));

	// Equality is type-sensitive.
	TestTrue(TEXT("Int==Int"), FElysiumVariant::Int(5) == FElysiumVariant::Int(5));
	TestFalse(TEXT("Int!=Float same magnitude"), FElysiumVariant::Int(5) == FElysiumVariant::Float(5.0f));
	TestTrue(TEXT("Void==Void"), FElysiumVariant::Void() == FElysiumVariant::Void());

	return true;
}

// =====================================================================================
// ElysiumExpr — the restricted expression subset, and the load-bearing error-to-false.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExprTest, "Elysium.Substrate.Expr", GElysiumTestFlags)
bool FElysiumExprTest::RunTest(const FString&)
{
	// A stateless/worldless env: arithmetic and comparisons resolve; names do not.
	ElysiumExpr::FEnv Env;

	auto Eval = [&Env](const TCHAR* Src)
	{
		Env.bError = false;
		Env.Error.Reset();
		return ElysiumExpr::Eval(FString(Src), Env);
	};

	TestEqual(TEXT("1 + 2"), Eval(TEXT("1 + 2")).ToInt(), 3);
	TestEqual(TEXT("2 * 3 + 1"), Eval(TEXT("2 * 3 + 1")).ToInt(), 7);
	TestTrue(TEXT("1 == 1"), Eval(TEXT("1 == 1")).ToBool());
	TestFalse(TEXT("1 == 2"), Eval(TEXT("1 == 2")).ToBool());
	TestTrue(TEXT("2 > 1"), Eval(TEXT("2 > 1")).ToBool());

	// error-to-false (RE3): a divide-by-zero yields Void, not a throw.
	{
		const FElysiumVariant V = Eval(TEXT("1 / 0"));
		TestTrue(TEXT("divide-by-zero sets bError"), Env.bError);
		TestFalse(TEXT("divide-by-zero is falsy"), V.ToBool());
	}

	// A parse error is the same total failure.
	{
		const FElysiumVariant V = Eval(TEXT("1 +"));
		TestTrue(TEXT("parse error sets bError"), Env.bError);
		TestFalse(TEXT("parse error is falsy"), V.ToBool());
	}

	// An unresolved name (no world) is error-to-false too, so a gate over it reads OnFalse.
	{
		const FElysiumVariant V = Eval(TEXT("undefined_flag"));
		TestFalse(TEXT("NameError is falsy"), V.ToBool());
	}

	return true;
}

// =====================================================================================
// 9.7d — `OneOfSet(which, count)` and the shadowed-name split.
//
// The selector is `(roll % count) == which - 1` (script_api.md). What the shipped corpus
// asserts is the SET property: N sibling `.dlg` rows carrying the same choice text, row i
// gated on OneOfSet(i, N), must present exactly one row — which holds only if every gate in
// the set reads one roll. Both halves test here, then again through the real expression
// host, which is where a dialogue gate reaches it.
//
// The second half guards the collapse `script_api.md` warns about: `Whisper` and
// `FrenzyTrigger` are both `vamputil.py` helpers AND datamap input names, so they must never
// join the shared native table — the bare spelling belongs to the script, the qualified one
// to the datamap.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOneOfSetTest, "Elysium.Substrate.OneOfSet", GElysiumTestFlags)
bool FElysiumOneOfSetTest::RunTest(const FString&)
{
	// --- The selector, over a pinned roll -------------------------------------------------
	// Exactly one of 1..N passes, for every roll residue and every set size the corpus uses
	// (2, 4, 6, 7, 8) — this is the whole contract.
	for (const int32 Count : { 2, 4, 6, 7, 8 })
	{
		for (int32 Roll = 0; Roll < Count * 3; ++Roll)
		{
			ElysiumScriptNatives::SetOneOfSetRoll(Roll);
			int32 Passing = 0;
			for (int32 Which = 1; Which <= Count; ++Which)
			{
				if (ElysiumScriptNatives::OneOfSet(Which, Count)) { ++Passing; }
			}
			if (Passing != 1)
			{
				AddError(FString::Printf(TEXT("OneOfSet(1..%d, %d) passed %d rows at roll %d, expected 1"),
					Count, Count, Passing, Roll));
			}
		}
	}

	// It is 1-based: roll 0 selects `which` 1, not 0.
	ElysiumScriptNatives::SetOneOfSetRoll(0);
	TestTrue(TEXT("roll 0 selects which=1"), ElysiumScriptNatives::OneOfSet(1, 7));
	TestFalse(TEXT("which=0 never passes"), ElysiumScriptNatives::OneOfSet(0, 7));
	TestFalse(TEXT("which past count never passes"), ElysiumScriptNatives::OneOfSet(8, 7));
	ElysiumScriptNatives::SetOneOfSetRoll(3);
	TestTrue(TEXT("roll 3 selects which=4"), ElysiumScriptNatives::OneOfSet(4, 7));

	// A degenerate count is false, not a divide-by-zero.
	TestFalse(TEXT("count 0 is false"), ElysiumScriptNatives::OneOfSet(1, 0));
	TestFalse(TEXT("negative count is false"), ElysiumScriptNatives::OneOfSet(1, -3));

	// --- The roll is stable while nothing advances the frame ------------------------------
	ElysiumScriptNatives::SetOneOfSetRoll(-1);
	const int32 First = ElysiumScriptNatives::OneOfSetRoll();
	TestEqual(TEXT("the roll does not change between calls in one frame"),
		ElysiumScriptNatives::OneOfSetRoll(), First);

	// --- Through the expression host, as a dialogue gate reaches it -----------------------
	// The 7-way set from the shipped `.dlg` corpus, evaluated the way a turn's choice list is:
	// each row's gate in one burst. Exactly one row is visible.
	{
		ElysiumExpr::FEnv Env;
		auto Gate = [&Env](const TCHAR* Src)
		{
			Env.bError = false;
			Env.Error.Reset();
			return ElysiumExpr::Eval(FString(Src), Env).ToBool();
		};

		ElysiumScriptNatives::SetOneOfSetRoll(4);
		int32 Visible = 0;
		const TCHAR* const Set7[] = {
			TEXT("OneOfSet(1,7)"), TEXT("OneOfSet(2,7)"), TEXT("OneOfSet(3,7)"), TEXT("OneOfSet(4,7)"),
			TEXT("OneOfSet(5,7)"), TEXT("OneOfSet(6,7)"), TEXT("OneOfSet(7,7)"),
		};
		for (const TCHAR* Row : Set7)
		{
			if (Gate(Row)) { ++Visible; }
		}
		TestEqual(TEXT("a 7-row OneOfSet set shows exactly one row"), Visible, 1);
		TestTrue(TEXT("...and it is the row the roll names"), Gate(TEXT("OneOfSet(5,7)")));

		// The real corpus shape: the selector is ANDed with a second condition, so a failing
		// second half still closes the row (`OneOfSet(1,6) and pc.CurrentMoney() >= 5`).
		ElysiumScriptNatives::SetOneOfSetRoll(0);
		TestTrue(TEXT("selected row with a passing conjunct"), Gate(TEXT("OneOfSet(1,6) and 5 >= 5")));
		TestFalse(TEXT("selected row with a failing conjunct"), Gate(TEXT("OneOfSet(1,6) and 1 >= 5")));
		TestFalse(TEXT("unselected row with a passing conjunct"), Gate(TEXT("OneOfSet(2,6) and 5 >= 5")));
	}

	ElysiumScriptNatives::SetOneOfSetRoll(-1);   // leave the live roll armed

	// --- The shadowed names must not collapse into the native surface ---------------------
	TestFalse(TEXT("Whisper is not a module global"), ElysiumScriptNatives::IsNativeGlobal(TEXT("Whisper")));
	TestFalse(TEXT("Whisper is not a Character method"), ElysiumScriptNatives::IsCharacterMethod(TEXT("Whisper")));
	TestFalse(TEXT("FrenzyTrigger is not a module global"),
		ElysiumScriptNatives::IsNativeGlobal(TEXT("FrenzyTrigger")));
	TestFalse(TEXT("FrenzyTrigger is not a Character method"),
		ElysiumScriptNatives::IsCharacterMethod(TEXT("FrenzyTrigger")));

	// The receiver-qualified spelling resolves the other way: a registered datamap input on the
	// class chain, which is what `pc.Whisper(...)` / `pc.FrenzyTrigger()` fire.
	{
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		const FElysiumClassDesc* PlayerDesc = Reg.Find(ElysiumPlayerClassName());
		if (TestNotNull(TEXT("the player class is registered"), PlayerDesc))
		{
			TestTrue(TEXT("Whisper is a player datamap input"),
				Reg.FindInput(*PlayerDesc, FName(TEXT("Whisper"))) != nullptr);
			TestTrue(TEXT("FrenzyTrigger is inherited from the combat character"),
				Reg.FindInput(*PlayerDesc, FName(TEXT("FrenzyTrigger"))) != nullptr);
		}
	}

	return true;
}

// =====================================================================================
// FElysiumConsole — VtMB's console surface (9.3b): cfg alias/cvar parse + the ccmd execute
// path (alias expansion -> cvar set -> Python fallthrough). Content-free: no Python, no world.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumConsoleTest, "Elysium.Substrate.Console", GElysiumTestFlags)
bool FElysiumConsoleTest::RunTest(const FString&)
{
	FElysiumConsole C;
	// A slice of real cfg syntax: a full-line comment, the patch's Basic/Plus alias, a movement
	// alias whose body is a `;`-terminated engine command, two cvar settings, and a keybind.
	C.ParseText(TEXT(
		"// Plus user.cfg\n"
		"alias patchtype \"setPlus()\"\n"
		"alias run \"-speed;\"\n"
		"fps_max \"65\"\n"
		"vchar_skip_intro \"1\"\n"
		"bind \"TAB\" \"+wpn_secondaryatk\"\n"));

	TestEqual(TEXT("two aliases parsed"), C.NumAliases(), 2);
	TestTrue(TEXT("patchtype alias present"), C.HasAlias(TEXT("patchtype")));
	TestFalse(TEXT("a keybind is not an alias"), C.HasAlias(TEXT("TAB")));
	TestEqual(TEXT("cvar fps_max"), C.GetCvar(TEXT("fps_max")), FString(TEXT("65")));
	TestEqual(TEXT("cvar lookup is case-insensitive"), C.GetCvar(TEXT("FPS_MAX")), FString(TEXT("65")));
	TestEqual(TEXT("missing cvar reads empty"), C.GetCvar(TEXT("nope")), FString());

	// The load-bearing path: `c.patchtype=""` -> alias patchtype -> "setPlus()" -> Python fallthrough.
	TArray<FString> Fell;
	C.SetPythonSink([&Fell](const FString& Line) { Fell.Add(Line); return true; });
	C.Execute(TEXT("patchtype"));
	TestEqual(TEXT("one Python fallthrough"), Fell.Num(), 1);
	if (Fell.Num() == 1)
	{
		TestEqual(TEXT("setPlus() reached Python"), Fell[0], FString(TEXT("setPlus()")));
	}

	// A known cvar with an argument sets it and does NOT fall through to Python.
	Fell.Reset();
	C.Execute(TEXT("fps_max 30"));
	TestEqual(TEXT("cvar set does not fall through"), Fell.Num(), 0);
	TestEqual(TEXT("cvar updated"), C.GetCvar(TEXT("fps_max")), FString(TEXT("30")));

	// The precedence, stated once in the header and asserted here (11.6): **registered command ->
	// alias -> cvar -> Python**. The patch's `run` alias expands to `-speed;`, which is a declared
	// ButtonPair verb, so the registry consumes it and it never reaches the sink.
	Fell.Reset();
	C.SetPythonSink([&Fell](const FString& Line) { Fell.Add(Line); return false; });
	FElysiumUserCmdBuilder Buttons;
	Buttons.SetButton(EElysiumButton::Speed, true);
	FElysiumCommands::Get().SetUserCmdSink(&Buttons);
	C.Execute(TEXT("run"));
	TestEqual(TEXT("a registered verb does not reach Python"), Fell.Num(), 0);
	TestFalse(TEXT("`run` -> `-speed` lifted the gait latch"), Buttons.IsDown(EElysiumButton::Speed));

	// A command outranks an alias of the same name: nothing a player writes into user.cfg can
	// shadow `+forward`, which is Source's own Cmd_ExecuteString order.
	C.ParseText(TEXT("alias +forward \"setPlus()\"\n"));
	Fell.Reset();
	C.Execute(TEXT("+forward"));
	TestEqual(TEXT("the shadowing alias never ran"), Fell.Num(), 0);
	TestTrue(TEXT("+forward latched the button"), Buttons.IsDown(EElysiumButton::Forward));

	// A word that is neither a verb nor an alias nor a cvar still falls through to Python.
	Fell.Reset();
	C.Execute(TEXT("checkFeed()"));
	TestEqual(TEXT("an unknown word reaches Python"), Fell.Num(), 1);

	FElysiumCommands::Get().SetUserCmdSink(nullptr);
	return true;
}

// =====================================================================================
// S7 — the command registry (11.6, runtime-architecture.md §8.2). The inventory, the +/- pair
// semantics, implementation stacking and the button latch are plain C++, so all of it is
// asserted with no world, no controller and no input device.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCommandsTest, "Elysium.Substrate.Commands", GElysiumTestFlags)
bool FElysiumCommandsTest::RunTest(const FString&)
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// --- The inventory ------------------------------------------------------------------
	const FElysiumCommandDef* Forward = Registry.Find(TEXT("forward"));
	TestNotNull(TEXT("`forward` is declared"), Forward);
	if (Forward)
	{
		TestTrue(TEXT("`forward` is a +/- pair"), Forward->Kind == EElysiumCmdKind::ButtonPair);
		TestEqual(TEXT("`forward` carries the Forward bit"),
			Forward->Button, static_cast<uint64>(EElysiumButton::Forward));
	}
	const FElysiumCommandDef* Camera = Registry.Find(TEXT("togglecamera"));
	TestNotNull(TEXT("`togglecamera` is declared"), Camera);
	if (Camera)
	{
		TestTrue(TEXT("`togglecamera` happens once"), Camera->Kind == EElysiumCmdKind::Once);
	}
	// Bound by both shipped default.cfg files, present in no binary and no script; deliberately
	// dropped. If it ever reappears, that is a decision, not a typo.
	TestFalse(TEXT("`vphysicshand` is not declared"), Registry.IsDeclared(TEXT("vphysicshand")));
	// Case folding is the registry's, so `+USE` and `+use` are the same verb.
	TestTrue(TEXT("names are case-folded"), Registry.IsDeclared(TEXT("USE")));

	// --- Resolution ---------------------------------------------------------------------
	FElysiumUserCmdBuilder Builder;
	Registry.SetUserCmdSink(&Builder);

	TestTrue(TEXT("`+speed` resolves"), Registry.Execute(TEXT("+speed")));
	TestTrue(TEXT("`+speed` latched the gait"), Builder.IsDown(EElysiumButton::Speed));
	TestTrue(TEXT("`-speed` resolves"), Registry.Execute(TEXT("-speed")));
	TestFalse(TEXT("`-speed` lifted the gait"), Builder.IsDown(EElysiumButton::Speed));

	// A sign only means an edge on a pair. `+togglecamera` is not a verb — VtMB's console reports
	// the same, and the console needs the false to know to try an alias.
	TestFalse(TEXT("a signed Once verb is not a verb"), Registry.Execute(TEXT("+togglecamera")));
	TestFalse(TEXT("an unknown word is not a verb"), Registry.Execute(TEXT("checkFeed()")));

	// A pair invoked bare is a press — how a `.dlg` action or a script spells a momentary verb.
	TestTrue(TEXT("a bare pair resolves"), Registry.Execute(TEXT("use")));

	// --- Implementations ----------------------------------------------------------------
	TArray<FString> Seen;
	TestFalse(TEXT("`vhotkey` starts unimplemented"), Registry.IsBound(TEXT("vhotkey")));
	FElysiumCommandBinding First = Registry.Bind(TEXT("vhotkey"),
		[&Seen](const FElysiumCommandCall& Call) { Seen.Add(TEXT("first:") + Call.Args); });
	TestTrue(TEXT("binding a declared verb succeeds"), First.IsValid());
	Registry.Execute(TEXT("vhotkey #3"));
	TestEqual(TEXT("the argument string survives"), Seen.Num() == 1 ? Seen[0] : FString(),
		FString(TEXT("first:#3")));

	// Implementations stack: a system can take a verb for a while and give it back.
	FElysiumCommandBinding Second = Registry.Bind(TEXT("vhotkey"),
		[&Seen](const FElysiumCommandCall&) { Seen.Add(TEXT("second")); });
	Registry.Execute(TEXT("vhotkey #1"));
	TestEqual(TEXT("the newest implementation runs"), Seen.Last(), FString(TEXT("second")));
	Registry.Unbind(Second);
	TestFalse(TEXT("Unbind clears the handle"), Second.IsValid());
	Registry.Execute(TEXT("vhotkey #2"));
	TestEqual(TEXT("the one underneath is restored"), Seen.Last(), FString(TEXT("first:#2")));

	// Binding a name the inventory does not carry fails rather than inventing a verb.
	AddExpectedError(TEXT("refused: not a declared verb"), EAutomationExpectedErrorFlags::Contains, 1);
	FElysiumCommandBinding Bogus = Registry.Bind(TEXT("nosuchverb"), [](const FElysiumCommandCall&) {});
	TestFalse(TEXT("an undeclared verb cannot be bound"), Bogus.IsValid());

	Registry.Unbind(First);
	Registry.SetUserCmdSink(nullptr);

	// --- The default bind table ----------------------------------------------------------
	// Every key VtMB's patch default.cfg binds names either a declared verb or one of the patch's
	// own aliases, and none of them lands on a key the dev layer owns. A typo in the table is a
	// dead key in the shipped game, so it fails here instead.
	static const TCHAR* const PatchAliases[] = {
		TEXT("vm_discipline"), TEXT("vm_feed"), TEXT("vm_passives"), TEXT("skip"),
		TEXT("cam_restore"), TEXT("cam_rotateleft"), TEXT("cam_rotateright"),
	};
	for (const FElysiumDefaultBind& Bind : ElysiumBinds::Defaults())
	{
		TestFalse(FString::Printf(TEXT("'%s' is not a reserved key"), *Bind.Key.ToString()),
			ElysiumBinds::IsReserved(Bind.Key));

		FString Word = FString(Bind.Command);
		int32 Space = INDEX_NONE;
		Word.FindChar(TEXT(' '), Space);
		if (Space != INDEX_NONE)
		{
			Word = Word.Left(Space);
		}
		const bool bSigned = Word.StartsWith(TEXT("+")) || Word.StartsWith(TEXT("-"));
		const FName Verb(*(bSigned ? Word.Mid(1) : Word));

		bool bKnown = Registry.IsDeclared(Verb);
		for (const TCHAR* Alias : PatchAliases)
		{
			bKnown = bKnown || Word.Equals(Alias, ESearchCase::IgnoreCase);
		}
		TestTrue(FString::Printf(TEXT("bind '%s' -> '%s' names something"), Bind.VtmbKey, Bind.Command), bKnown);
	}

	return true;
}

// =====================================================================================
// S5 — intent is data (11.6, runtime-architecture.md §8.3). The user command is built from
// button latches and analog accumulators with no engine input in sight, which is what makes
// headless play and replay the same mechanism as playing.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUserCmdTest, "Elysium.Substrate.UserCmd", GElysiumTestFlags)
bool FElysiumUserCmdTest::RunTest(const FString&)
{
	FElysiumUserCmdBuilder Builder;

	// Opposed keys cancel; a diagonal is two axes, and clamping happens per axis.
	Builder.SetButton(EElysiumButton::Forward, true);
	Builder.SetButton(EElysiumButton::Back, true);
	FElysiumUserCmd Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("forward and back cancel"), Cmd.Move.X, 0.0);
	TestEqual(TEXT("the first command is #1"), (int32)Cmd.Seq, 1);

	Builder.SetButton(EElysiumButton::Back, false);
	Builder.SetButton(EElysiumButton::MoveRight, true);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("forward alone is +1"), Cmd.Move.X, 1.0);
	TestEqual(TEXT("moveright alone is +1"), Cmd.Move.Y, 1.0);
	TestEqual(TEXT("sequence advances"), (int32)Cmd.Seq, 2);

	// `+left`/`+right` are turn keys, at cl_yawspeed — until `+strafe` is held, when the same two
	// keys strafe instead. That is why they are buttons and not a look axis.
	Builder.SetButton(EElysiumButton::Forward, false);
	Builder.SetButton(EElysiumButton::MoveRight, false);
	Builder.SetButton(EElysiumButton::Right, true);
	Cmd = Builder.Build(1.0f);
	TestEqual(TEXT("a turn key yaws at cl_yawspeed"), (float)Cmd.LookDelta.X,
		ElysiumInput::KeyboardYawSpeed, 0.001f);
	TestEqual(TEXT("a turn key does not strafe"), Cmd.Move.Y, 0.0);

	Builder.SetButton(EElysiumButton::Strafe, true);
	Cmd = Builder.Build(1.0f);
	TestEqual(TEXT("+strafe turns the turn key into strafe"), Cmd.Move.Y, 1.0);
	TestEqual(TEXT("+strafe stops it yawing"), (float)Cmd.LookDelta.X, 0.0f, 0.001f);
	Builder.SetButton(EElysiumButton::Strafe, false);
	Builder.SetButton(EElysiumButton::Right, false);

	// Mouse counts accumulate within a frame and are consumed by the build, never carried over.
	Builder.AddLook(1.5f, -0.5f);
	Builder.AddLook(0.5f, 0.25f);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("look accumulates"), (float)Cmd.LookDelta.X, 2.0f, 0.001f);
	TestEqual(TEXT("look accumulates on pitch too"), (float)Cmd.LookDelta.Y, -0.25f, 0.001f);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("the accumulator is consumed"), (float)Cmd.LookDelta.X, 0.0f, 0.001f);

	// A held button survives a build; ClearButtons is what a scope change does to it, and it must
	// not produce a command of its own.
	Builder.SetButton(EElysiumButton::Speed, true);
	TestTrue(TEXT("a latch survives a build"), Builder.Build(0.016f).IsDown(EElysiumButton::Speed));
	Builder.ClearButtons();
	TestFalse(TEXT("ClearButtons drops the latch"), Builder.Build(0.016f).IsDown(EElysiumButton::Speed));

	// Press edges are what a Once-shaped consumer (jump, the noclip toggle) reads.
	FElysiumUserCmd Prev;
	FElysiumUserCmd Now;
	Now.Buttons = static_cast<uint64>(EElysiumButton::Jump);
	TestTrue(TEXT("a new press is an edge"), Now.JustPressed(EElysiumButton::Jump, Prev));
	TestFalse(TEXT("a held press is not"), Now.JustPressed(EElysiumButton::Jump, Now));
	TestTrue(TEXT("a release is an edge"), Prev.JustReleased(EElysiumButton::Jump, Now));

	// --- Record / replay -----------------------------------------------------------------
	// The acceptance: a recorded stream replays identically. The whole of it is here because the
	// stream is a value — 11.10 drives the same stream through a real world.
	FElysiumUserCmdStream Recorded;
	FElysiumUserCmdBuilder Source;
	static const EElysiumButton Script[] = {
		EElysiumButton::Forward, EElysiumButton::Speed, EElysiumButton::MoveLeft, EElysiumButton::Jump,
	};
	for (int32 Frame = 0; Frame < 16; ++Frame)
	{
		Source.SetButton(Script[Frame % UE_ARRAY_COUNT(Script)], (Frame % 3) != 2);
		Source.AddLook(Frame * 0.1f, Frame * -0.05f);
		Recorded.Record(Source.Build(1.0f / 60.0f));
	}
	TestEqual(TEXT("16 frames recorded"), Recorded.Num(), 16);

	// Replay is the router's loop: pull each command and use it verbatim.
	FElysiumUserCmdStream Played;
	Recorded.Rewind();
	while (const FElysiumUserCmd* Next = Recorded.Next())
	{
		FElysiumUserCmd Frame = *Next;
		// A replay at a different frame rate is still the same input, so the delta is re-stamped.
		Frame.DeltaSeconds = 1.0f / 30.0f;
		Played.Record(Frame);
	}
	TestTrue(TEXT("the replay is the recording"), Recorded.SameIntent(Played));

	// And it survives the text form, which is what makes a stream a beat-script input.
	FElysiumUserCmdStream RoundTrip;
	TestTrue(TEXT("the text form parses"),
		FElysiumUserCmdStream::FromText(Recorded.ToText(), RoundTrip));
	TestTrue(TEXT("the text form round-trips"), Recorded.SameIntent(RoundTrip));

	return true;
}

// =====================================================================================
// FElysiumScriptFS — the embedded VM's filesystem namespace. The path policy is pure string
// work, so all of it tests here with no VM, no content and no filesystem: normalization,
// the moddir fold, the sandbox denial, and the three path styles VtMB's scripts actually use.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptFSTest, "Elysium.Substrate.ScriptFS", GElysiumTestFlags)
bool FElysiumScriptFSTest::RunTest(const FString&)
{
	const FString Root = FElysiumScriptFS::VirtualRoot();
	FString Rel;

	// --- the three spellings all land on the same sandbox-relative form ---------------------
	// Style A (vamputil.FixKeyBindings): getcwd() + "\\" + moddir + "\\cfg\\config.cfg".
	TestTrue(TEXT("style A normalizes"),
		FElysiumScriptFS::NormalizeToSandbox(Root + TEXT("\\Vampire\\cfg\\config.cfg"), Rel));
	TestEqual(TEXT("style A -> cfg/config.cfg"), Rel, FString(TEXT("cfg/config.cfg")));

	// Style B (vamputil.setPlus): moddir + "/vdata/...", relative, forward slashes.
	TestTrue(TEXT("style B normalizes"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("Vampire/vdata/hackterminals/haven_pc.txt"), Rel));
	TestEqual(TEXT("style B -> vdata/..."), Rel,
		FString(TEXT("vdata/hackterminals/haven_pc.txt")));

	// Style C (zvtool.zdumpg): bare relative, no moddir and no getcwd at all.
	TestTrue(TEXT("style C normalizes"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("zvtool_g_dump.txt"), Rel));
	TestEqual(TEXT("style C -> sandbox root"), Rel, FString(TEXT("zvtool_g_dump.txt")));

	// The moddir fold is what makes A and a bare tree path the same file, not two.
	TestTrue(TEXT("bare tree path normalizes"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("cfg/config.cfg"), Rel));
	TestEqual(TEXT("moddir-less spelling folds together with A"), Rel,
		FString(TEXT("cfg/config.cfg")));

	// Only the FIRST component, and only one: a `Vampire` deeper in a tree is a real directory.
	TestTrue(TEXT("nested moddir name survives"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("Vampire/vdata/Vampire/x.txt"), Rel));
	TestEqual(TEXT("only the leading moddir folds"), Rel, FString(TEXT("vdata/Vampire/x.txt")));

	// --- normalization detail ---------------------------------------------------------------
	TestTrue(TEXT("mixed separators + dot segments"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("Vampire\\vdata/./items\\..\\system/x.txt"), Rel));
	TestEqual(TEXT("dot segments collapse"), Rel, FString(TEXT("vdata/system/x.txt")));

	TestTrue(TEXT("a leading separator reads as sandbox-relative"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("/cfg/config.cfg"), Rel));
	TestEqual(TEXT("leading separator -> cfg/config.cfg"), Rel, FString(TEXT("cfg/config.cfg")));

	// --- the one hard denial: leaving the sandbox --------------------------------------------
	TestFalse(TEXT("climbing out is denied"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("../../../Windows/system32/x.dll"), Rel));
	TestFalse(TEXT("climbing out through the moddir is denied"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("Vampire/../../x"), Rel));
	TestFalse(TEXT("a foreign absolute path is denied"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("C:\\Windows\\system32\\x.dll"), Rel));
	TestFalse(TEXT("a UNC path is denied"),
		FElysiumScriptFS::NormalizeToSandbox(TEXT("\\\\server\\share\\x"), Rel));
	TestFalse(TEXT("empty is denied"), FElysiumScriptFS::NormalizeToSandbox(TEXT(""), Rel));
	// A sibling directory whose name merely starts with the root's is not inside it.
	TestFalse(TEXT("root-prefixed sibling is denied"),
		FElysiumScriptFS::NormalizeToSandbox(Root + TEXT("Other\\x"), Rel));

	// --- mode -> access ----------------------------------------------------------------------
	// `w`/`w+` truncate (nothing to carry over); `a`, `a+`, `r+` keep the existing bytes and so
	// need the mirror's copy brought up into the overlay first.
	TestTrue(TEXT("r reads"),
		FElysiumScriptFS::AccessFromMode(TEXT("r")) == EElysiumFsAccess::Read);
	TestTrue(TEXT("rb reads"),
		FElysiumScriptFS::AccessFromMode(TEXT("rb")) == EElysiumFsAccess::Read);
	TestTrue(TEXT("empty mode reads"),
		FElysiumScriptFS::AccessFromMode(TEXT("")) == EElysiumFsAccess::Read);
	TestTrue(TEXT("w writes"),
		FElysiumScriptFS::AccessFromMode(TEXT("w")) == EElysiumFsAccess::Write);
	TestTrue(TEXT("wb writes"),
		FElysiumScriptFS::AccessFromMode(TEXT("wb")) == EElysiumFsAccess::Write);
	TestTrue(TEXT("w+ truncates, so it writes rather than updates"),
		FElysiumScriptFS::AccessFromMode(TEXT("w+")) == EElysiumFsAccess::Write);
	TestTrue(TEXT("a updates"),
		FElysiumScriptFS::AccessFromMode(TEXT("a")) == EElysiumFsAccess::Update);
	TestTrue(TEXT("rb+ updates"),
		FElysiumScriptFS::AccessFromMode(TEXT("rb+")) == EElysiumFsAccess::Update);

	// --- the mount table ---------------------------------------------------------------------
	// The trees the offline pipeline mirrors resolve; the ones it does not are reported as absent
	// rather than mapped somewhere wrong. VtMB's own `scripts/` (kb_act.lst) is NOT out/scripts —
	// that is its `python/` tree — which is why hunter mode's keybinding copy finds nothing.
	FString Real;
	TestTrue(TEXT("cfg is mounted"), FElysiumScriptFS::MapToMirror(TEXT("cfg/config.cfg"), Real));
	TestTrue(TEXT("cfg maps under out/cfg"), Real.Replace(TEXT("\\"), TEXT("/"))
		.Contains(TEXT("tools/out/cfg/config.cfg")));
	TestTrue(TEXT("vdata is mounted"),
		FElysiumScriptFS::MapToMirror(TEXT("vdata/system/stats.txt"), Real));
	TestTrue(TEXT("vdata maps under out/vdata"), Real.Replace(TEXT("\\"), TEXT("/"))
		.Contains(TEXT("tools/out/vdata/system/stats.txt")));
	TestTrue(TEXT("vdata/signs is mounted ahead of vdata"),
		FElysiumScriptFS::MapToMirror(TEXT("vdata/signs/death.txt"), Real));
	TestTrue(TEXT("signs maps under out/signs, not out/vdata"), Real.Replace(TEXT("\\"), TEXT("/"))
		.Contains(TEXT("tools/out/signs/death.txt")));
	TestTrue(TEXT("python maps onto out/scripts"),
		FElysiumScriptFS::MapToMirror(TEXT("python/tutorial/tutorial.py"), Real));
	TestTrue(TEXT("python -> out/scripts"), Real.Replace(TEXT("\\"), TEXT("/"))
		.Contains(TEXT("tools/out/scripts/tutorial/tutorial.py")));
	TestTrue(TEXT("a mount point itself resolves (nt.listdir on a tree)"),
		FElysiumScriptFS::MapToMirror(TEXT("cfg"), Real));
	TestFalse(TEXT("VtMB's own scripts/ has no mirror"),
		FElysiumScriptFS::MapToMirror(TEXT("scripts/kb_act.lst"), Real));
	TestFalse(TEXT("materials/ has no mirror (baked offline)"),
		FElysiumScriptFS::MapToMirror(TEXT("materials/hud/new_ui/bloodbar.vmt"), Real));
	TestFalse(TEXT("maps/ has no mirror"), FElysiumScriptFS::MapToMirror(TEXT("maps/x.bsp"), Real));

	// --- resolve: reads may fall to the mirror, writes never do ------------------------------
	// Root() is regenerable pipeline output; a script write into it would vanish on the next
	// export. Every write lands in the Saved/ overlay instead.
	FString Err;
	TestTrue(TEXT("a write resolves"), FElysiumScriptFS::Resolve(
		TEXT("Vampire/vdata/hackterminals/haven_pc.txt"), EElysiumFsAccess::Write, Real, Err));
	TestTrue(TEXT("the write landed in the overlay"), Real.StartsWith(Root));
	TestFalse(TEXT("the write did NOT land in the content mirror"),
		Real.Replace(TEXT("\\"), TEXT("/")).Contains(TEXT("tools/out/vdata")));

	TestFalse(TEXT("an escaping write is denied"), FElysiumScriptFS::Resolve(
		TEXT("../../../Windows/system32/x.dll"), EElysiumFsAccess::Write, Real, Err));
	TestTrue(TEXT("the denial carries a reason"), !Err.IsEmpty());

	// A permitted-but-missing path still resolves — the scripts branch on missing files
	// (`fileutil.isFile(...lip)` gating a dialogue line), so they must keep getting the OS error
	// rather than an exception from us.
	TestTrue(TEXT("an unmirrored read still resolves"), FElysiumScriptFS::Resolve(
		TEXT("Vampire/models/nothing_here.mdl"), EElysiumFsAccess::Read, Real, Err));

	return true;
}

// =====================================================================================
// ElysiumKeyValues — the Source KV reader shared by sound schemes and sign panels.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumKeyValuesTest, "Elysium.Substrate.KeyValues", GElysiumTestFlags)
bool FElysiumKeyValuesTest::RunTest(const FString&)
{
	// Nesting, // comments, and a quoted value that SPANS LINES (the sign-definition case a
	// line-based scanner truncates).
	const FString Text = TEXT(
		"\"Root\"\n"
		"{\n"
		"    // a comment\n"
		"    \"name\" \"value\"\n"
		"    \"XPos\" \"\"\n"                        // authored-but-empty: the CSignUI centring branch
		"    \"Text\" \"line one\nline two\"\n"      // spans a newline inside the quotes
		"    \"gate\" \"BloodPool > 0\"\n"           // a repeated leaf key: the stats.txt case
		"    \"gate\" \"Health < Max_Health\"\n"
		"    \"Block\" { \"inner\" \"1\" }\n"        // a whole block inline on one line
		"}\n");

	TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	if (!TestNotNull(TEXT("parse produced a root"), Root.Get()))
	{
		return false;
	}

	const ElysiumKeyValues::FKvNode* RootBlock = Root->Child(TEXT("Root"));
	if (!TestNotNull(TEXT("Root block present"), RootBlock))
	{
		return false;
	}

	TestEqual(TEXT("leaf value"), RootBlock->Str(TEXT("name"), FString()), FString(TEXT("value")));

	// The multi-line value survived as one string.
	TestTrue(TEXT("multi-line Text kept both lines"),
		RootBlock->Str(TEXT("Text"), FString()).Contains(TEXT("line two")));

	// An authored empty value reads present-but-empty (distinct from absent).
	TestTrue(TEXT("empty XPos present"), RootBlock->Has(TEXT("XPos")));
	TestTrue(TEXT("empty XPos is empty"), RootBlock->Str(TEXT("XPos"), TEXT("DEFAULT")).IsEmpty());
	TestFalse(TEXT("absent key not present"), RootBlock->Has(TEXT("nope")));

	// A repeated leaf key is data, not an authoring slip: `Values` keeps the last, `Pairs` keeps
	// both. 17 of stats.txt's Active_Disciplines gate on two IncPredependency expressions this way.
	TestEqual(TEXT("repeated leaf: Values keeps the last"),
		RootBlock->Str(TEXT("gate"), FString()), FString(TEXT("Health < Max_Health")));
	TArray<FString> Gates;
	RootBlock->ValuesFor(TEXT("gate"), Gates);
	if (TestEqual(TEXT("repeated leaf: ValuesFor keeps both"), Gates.Num(), 2))
	{
		TestEqual(TEXT("in file order"), Gates[0], FString(TEXT("BloodPool > 0")));
	}
	TArray<FString> None;
	RootBlock->ValuesFor(TEXT("nope"), None);
	TestEqual(TEXT("ValuesFor on an absent key yields nothing"), None.Num(), 0);

	// Nested block and its typed read.
	const ElysiumKeyValues::FKvNode* Inner = RootBlock->Child(TEXT("Block"));
	if (TestNotNull(TEXT("nested Block present"), Inner))
	{
		TestEqual(TEXT("nested int"), Inner->Int(TEXT("inner"), 0), 1);
	}

	return true;
}

// =====================================================================================
// The rulebook's pure parsers (9.4a) — the four grammars VtMB authors inside its `vdata/`
// values, driven off literals so they are asserted with no exported content at all. Each is
// a place where reading the string wrong produces a plausible-but-wrong number rather than
// a failure, which is exactly why they are pinned here rather than only against the files.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRulebookTest, "Elysium.Substrate.Rulebook", GElysiumTestFlags)
bool FElysiumRulebookTest::RunTest(const FString&)
{
	// --- The cost grammar: three authored forms ------------------------------------------------
	FElysiumStatCost Cost;

	TestTrue(TEXT("`Current_Rating * 4` parses"), Cost.Parse(TEXT("Current_Rating * 4")));
	TestEqual(TEXT("  as per-rating"), (int32)Cost.Kind, (int32)FElysiumStatCost::EKind::PerRating);
	// Current_Rating is the PRE-purchase base, so the 3 -> 4 dot costs 12 and selling it refunds 12.
	TestEqual(TEXT("  the 3->4 attribute dot costs 12"), Cost.At(3), 12);
	TestEqual(TEXT("  and the 0->1 dot is free of the raise price"), Cost.At(0), 0);

	TestTrue(TEXT("`Table: 6, 8, 12, 18, 24` parses"), Cost.Parse(TEXT("Table: 6, 8, 12, 18, 24")));
	TestEqual(TEXT("  as a table"), (int32)Cost.Kind, (int32)FElysiumStatCost::EKind::Table);
	TestEqual(TEXT("  with five steps"), Cost.Steps.Num(), 5);
	TestEqual(TEXT("  rating 2 costs 12"), Cost.At(2), 12);
	TestEqual(TEXT("  past the end clamps to the last step"), Cost.At(99), 24);

	TestTrue(TEXT("a bare integer parses"), Cost.Parse(TEXT("3")));
	TestEqual(TEXT("  as flat"), (int32)Cost.Kind, (int32)FElysiumStatCost::EKind::Flat);
	TestEqual(TEXT("  at any rating"), Cost.At(4), 3);

	TestFalse(TEXT("an absent cost does not parse"), Cost.Parse(FString()));
	TestTrue(TEXT("30000 is the engine's cannot-buy"), Cost.Parse(TEXT("30000")) &&
		!Cost.CanBuy(0));

	// One of New/Raise authored copies into the other — CVStatCost_t::Load's own rule, and the
	// reason a stat with only a `Raise` still prices its first dot.
	{
		TSharedPtr<ElysiumKeyValues::FKvNode> Node =
			ElysiumKeyValues::ParseText(TEXT("Costs { \"Raise\" \"Current_Rating * 5\" }"));
		const ElysiumKeyValues::FKvNode* Block = Node.IsValid() ? Node->Child(TEXT("Costs")) : nullptr;
		if (TestNotNull(TEXT("Costs block parsed"), Block))
		{
			FElysiumStatCosts Costs;
			Costs.Load(*Block);
			TestTrue(TEXT("Costs present"), Costs.bPresent);
			TestEqual(TEXT("Raise copied into New"), Costs.New.At(2), Costs.Raise.At(2));
		}
	}

	// --- CVStatRef: a trait name with an optional / N or * N -----------------------------------
	FElysiumTraitRef Ref = FElysiumTraitRef::Parse(TEXT("Armor_Rating / 2"));
	TestEqual(TEXT("`Armor_Rating / 2` names the trait"), Ref.Trait, FString(TEXT("Armor_Rating")));
	TestEqual(TEXT("  and divides"), Ref.Apply(7), 3);
	Ref = FElysiumTraitRef::Parse(TEXT("Strength"));
	TestEqual(TEXT("a bare name is identity"), Ref.Apply(7), 7);
	Ref = FElysiumTraitRef::Parse(TEXT("Soak_Pool * 3"));
	TestEqual(TEXT("`* 3` multiplies"), Ref.Apply(7), 21);

	// --- The Base%d probe stops at the first ABSENT index --------------------------------------
	// `feats.txt` comments out a `Base2` while leaving `Base0`/`Base1` live, so a fixed-range scan
	// would read a base the authors parked.
	{
		TSharedPtr<ElysiumKeyValues::FKvNode> Node = ElysiumKeyValues::ParseText(
			TEXT("Feat { \"Base0\" \"Dexterity\" \"Base1\" \"Security\" \"Base3\" \"Parked\" }"));
		const ElysiumKeyValues::FKvNode* Feat = Node.IsValid() ? Node->Child(TEXT("Feat")) : nullptr;
		if (TestNotNull(TEXT("Feat block parsed"), Feat))
		{
			TArray<FElysiumTraitRef> Bases;
			FElysiumFeatTable::ProbeTraitRefs(*Feat, TEXT("Base"), Bases);
			TestEqual(TEXT("the probe stops at the gap"), Bases.Num(), 2);

			TArray<FElysiumTraitRef> None;
			FElysiumFeatTable::ProbeTraitRefs(*Feat, TEXT("Automatic"), None);
			TestEqual(TEXT("a feat with no Automatic0 has none"), None.Num(), 0);
		}
	}

	// --- The Modifier mini-DSL against the shipped operator vocabulary -------------------------
	// The vocabulary is DATA (`traiteffect.txt`); here it is supplied by hand so the parse rule is
	// asserted with no files. The content tier checks the shipped file still says this.
	FElysiumTraitEffects Effects;
	Effects.Operators.Names = { TEXT("+"), TEXT("*"), TEXT("/"), TEXT("Max"), TEXT("Min"),
		TEXT("%"), TEXT("Value"), TEXT("Cost"), TEXT("BloodCost"), TEXT("Damage"), TEXT("Duration") };

	FElysiumTraitEffect Fx;
	Effects.ParseModifier(TEXT("+1"), Fx);
	TestEqual(TEXT("`+1` is op Add"), (int32)Fx.Op, (int32)EElysiumTraitOp::Add);
	TestEqual(TEXT("  amount 1"), Fx.Amount, 1);

	Effects.ParseModifier(TEXT("-2"), Fx);
	TestEqual(TEXT("`-2` is op Add"), (int32)Fx.Op, (int32)EElysiumTraitOp::Add);
	TestEqual(TEXT("  amount -2"), Fx.Amount, -2);

	Effects.ParseModifier(TEXT("Max 3"), Fx);
	TestEqual(TEXT("`Max 3` is op Max"), (int32)Fx.Op, (int32)EElysiumTraitOp::Max);
	TestEqual(TEXT("  amount 3"), Fx.Amount, 3);

	Effects.ParseModifier(TEXT("Duration 200%"), Fx);
	TestEqual(TEXT("`Duration 200%` is op Duration"), (int32)Fx.Op, (int32)EElysiumTraitOp::Duration);
	TestEqual(TEXT("  amount 200"), Fx.Amount, 200);
	TestTrue(TEXT("  flagged percent"), Fx.bPercent);

	// `Value` takes a NAMED payload as readily as a number, which is the one operator that cannot
	// be modelled as an int.
	Effects.ParseModifier(TEXT("Value Clawed_Form"), Fx);
	TestEqual(TEXT("`Value Clawed_Form` is op Value"), (int32)Fx.Op, (int32)EElysiumTraitOp::Value);
	TestEqual(TEXT("  carries the name"), Fx.ValueName, FString(TEXT("Clawed_Form")));
	Effects.ParseModifier(TEXT("Value 1"), Fx);
	TestEqual(TEXT("`Value 1` is still op Value"), (int32)Fx.Op, (int32)EElysiumTraitOp::Value);
	TestEqual(TEXT("  with a number"), Fx.Amount, 1);
	TestTrue(TEXT("  and no name"), Fx.ValueName.IsEmpty());

	// An unmatched operator name falls to op 0 with a signed integer — the engine's own fallback.
	Effects.ParseModifier(TEXT("Nonsense 5"), Fx);
	TestEqual(TEXT("an unknown operator falls to Add"), (int32)Fx.Op, (int32)EElysiumTraitOp::Add);

	// --- The experience table's pipe rows ------------------------------------------------------
	FElysiumExperienceEntry Entry;
	TestTrue(TEXT("a row parses"),
		FElysiumExperienceTable::ParseRow(TEXT("Carson01  | Found the case\t | 101"), Entry));
	TestEqual(TEXT("  key trimmed of spaces"), Entry.Key, FString(TEXT("Carson01")));
	TestEqual(TEXT("  description trimmed of tabs"), Entry.Description, FString(TEXT("Found the case")));
	// Stored raw: the trailing `01` is not an encoding this layer strips.
	TestEqual(TEXT("  value stored raw"), Entry.Value, 101);

	TestFalse(TEXT("a `>` comment is not a row"),
		FElysiumExperienceTable::ParseRow(TEXT("> Elizabeth Dane"), Entry));
	TestFalse(TEXT("a blank line is not a row"), FElysiumExperienceTable::ParseRow(TEXT(""), Entry));
	// The loader's own rule, not general whitespace handling: under three characters is skipped.
	TestFalse(TEXT("a sub-3-character line is skipped"),
		FElysiumExperienceTable::ParseRow(TEXT("ab"), Entry));
	TestFalse(TEXT("a two-field line is not a row"),
		FElysiumExperienceTable::ParseRow(TEXT("Key | 101"), Entry));
	TestTrue(TEXT("a NONE description is still a row"),
		FElysiumExperienceTable::ParseRow(TEXT("Title01  | NONE\t | 51"), Entry));

	// --- The shared integer-keyed lookup table -------------------------------------------------
	{
		TSharedPtr<ElysiumKeyValues::FKvNode> Node = ElysiumKeyValues::ParseText(
			TEXT("Table { \"InternalName\" \"T\" \"Clamping\" \"1\" \"0\" \"0\" \"1\" \"3.0\" \"2\" \"4.0\" }"));
		const ElysiumKeyValues::FKvNode* Block = Node.IsValid() ? Node->Child(TEXT("Table")) : nullptr;
		if (TestNotNull(TEXT("Table block parsed"), Block))
		{
			FElysiumRuleTable Table;
			Table.Load(*Block);
			// The named keys are not rows; only the numeric ones are.
			TestEqual(TEXT("three rows, not five"), Table.Rows.Num(), 3);
			TestEqual(TEXT("row 1 reads as a float"), Table.Lookup(1), 3.0f);
			TestEqual(TEXT("clamping takes the nearest end"), Table.Lookup(99), 4.0f);
		}
	}

	return true;
}

// =====================================================================================
// The character sheet's compiled half — the slot tables and the fields they register.
//
// This is the content-free side: the table's own shape, and that every slot reaches the R2 walk
// under both spellings. `Elysium.Content.Sheet` is the other half, checking the table against the
// real `stats.txt`.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSheetTest, "Elysium.Substrate.Sheet", GElysiumTestFlags)
bool FElysiumSheetTest::RunTest(const FString&)
{
	using EC = EElysiumTraitContainer;

	// --- The tables ---------------------------------------------------------------------------
	// The widths are `CBaseCombatCharacter`'s compiled arrays, read off the datamap offsets: the
	// base -> current delta is 0x8C on Attributes (35 ints) and 0x34 on the other three (13).
	// Disciplines is 13 even though `stats.txt` authors 17 — the four Numina rows are file-only.
	TestEqual(TEXT("Attributes is 35 slots"), ElysiumSheetSlotCount(EC::Attributes), 35);
	TestEqual(TEXT("Abilities is 13"), ElysiumSheetSlotCount(EC::Abilities), 13);
	TestEqual(TEXT("Disciplines is 13, not stats.txt's 17"), ElysiumSheetSlotCount(EC::Disciplines), 13);
	TestEqual(TEXT("Active_Disciplines matches it"), ElysiumSheetSlotCount(EC::ActiveDisciplines), 13);

	TSet<FString> AllDatamapNames;
	for (uint8 i = 0; i < (uint8)EC::Count; ++i)
	{
		const EC Container = (EC)i;
		int32 Expected = 0;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			TestEqual(FString::Printf(TEXT("%s slot %d states its own index"),
				ElysiumTraitContainerName(Container), Expected), Slot.Index, Expected);
			TestTrue(FString::Printf(TEXT("%s slot %d names a trait"),
				ElysiumTraitContainerName(Container), Expected),
				Slot.Datamap && *Slot.Datamap && Slot.Internal && *Slot.Internal);
			// One name, one slot, across the whole sheet — a collision would make the second
			// registration silently overwrite the first in the class's field map.
			bool bAlready = false;
			AllDatamapNames.Add(FString(Slot.Datamap).ToLower(), &bAlready);
			TestFalse(FString::Printf(TEXT("`%s` is not a duplicate datamap name"), Slot.Datamap), bAlready);
			++Expected;
		}
	}

	// The five rows where the datamap and `stats.txt` disagree. Three are RE24's off the image; the
	// two `v`-prefixed health rows are the collision-avoidance pattern RE24 sampled at slot 17.
	auto SlotName = [](EC Container, int32 Index) -> FString
	{
		TArrayView<const FElysiumSheetSlot> Slots = ElysiumSheetSlots(Container);
		return Slots.IsValidIndex(Index) ? FString(Slots[Index].Datamap) : FString();
	};
	TestEqual(TEXT("Intimidation is `intimidate` on the datamap"), SlotName(EC::Abilities, 3), FString(TEXT("intimidate")));
	TestEqual(TEXT("Computer is `computers`"), SlotName(EC::Abilities, 9), FString(TEXT("computers")));
	TestEqual(TEXT("Gender carries its trailing underscore"), SlotName(EC::Attributes, 11), FString(TEXT("gender_")));
	TestEqual(TEXT("Health is `vhealth`"), SlotName(EC::Attributes, ElysiumSlot::Health), FString(TEXT("vhealth")));
	TestEqual(TEXT("Max_Health is `vmax_health`"), SlotName(EC::Attributes, ElysiumSlot::MaxHealth), FString(TEXT("vmax_health")));

	// `CVStatRef`'s resolver: by the `stats.txt` name, case-insensitive, across all four containers.
	EC FoundContainer = EC::Count;
	int32 FoundSlot = INDEX_NONE;
	TestTrue(TEXT("`Max_Health` resolves by its stats.txt name"),
		ElysiumFindSheetSlot(TEXT("max_health"), FoundContainer, FoundSlot));
	TestEqual(TEXT("...to the Attributes container"), (int32)FoundContainer, (int32)EC::Attributes);
	TestEqual(TEXT("...at slot 17"), FoundSlot, ElysiumSlot::MaxHealth);
	TestTrue(TEXT("a discipline resolves too"),
		ElysiumFindSheetSlot(TEXT("Thaumaturgy"), FoundContainer, FoundSlot));
	TestEqual(TEXT("...in its own container"), (int32)FoundContainer, (int32)EC::Disciplines);
	TestFalse(TEXT("a Numina power does not — the compiled array does not reach it"),
		ElysiumFindSheetSlot(TEXT("Shield_of_Faith"), FoundContainer, FoundSlot));

	// --- Storage ------------------------------------------------------------------------------
	FElysiumSheet Sheet;
	TestEqual(TEXT("a fresh sheet is sized to the tables"),
		Sheet.Base[(uint8)EC::Attributes].Num(), 35);
	TestEqual(TEXT("an unset slot reads 0"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength), 0);
	TestEqual(TEXT("an out-of-range slot reads 0 rather than crashing"),
		Sheet.GetCurrent(EC::Abilities, 999), 0);

	Sheet.SetBase(EC::Attributes, ElysiumSlot::Strength, 4);
	TestEqual(TEXT("a base write is readable"), Sheet.GetBase(EC::Attributes, ElysiumSlot::Strength), 4);
	TestEqual(TEXT("and the current follows it"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength), 4);
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Strength, -1);
	TestEqual(TEXT("AddBase moves it"), Sheet.GetBase(EC::Attributes, ElysiumSlot::Strength), 3);
	TestEqual(TEXT("a different container is untouched"), Sheet.GetBase(EC::Abilities, 1), 0);

	Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Tremere")));
	TestEqual(TEXT("clan is a slot, not a member"), Sheet.Clan(), 7);
	TestEqual(TEXT("...reachable through the container too"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Clan), 7);
	Sheet.SetMale(false);
	TestFalse(TEXT("sex is the Gender slot"), Sheet.IsMale());

	// With no rulebook nothing is clamped — the same fail-closed posture the seed takes.
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Humanity, 99);
	Sheet.RecomputeCurrent(nullptr);
	TestEqual(TEXT("with no stat table the current is the base"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity), 99);

	// --- The R2 walk --------------------------------------------------------------------------
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Player = Reg.Find(ElysiumPlayerClassName());
	const FElysiumClassDesc* Npc = Reg.Find(FName(TEXT("npc_VVampire")));
	if (!TestNotNull(TEXT("the player class is registered"), reinterpret_cast<const void*>(Player))
		|| !TestNotNull(TEXT("and npc_VVampire"), reinterpret_cast<const void*>(Npc)))
	{
		return false;
	}

	auto FieldOn = [&Reg](const FElysiumClassDesc* Class, const TCHAR* Name) -> const FElysiumFieldAccessor*
	{
		return Reg.FindField(*Class, FName(Name));
	};

	// Both spellings of a slot, on the player and — because the sheet is on CBaseCombatCharacter,
	// where VtMB puts it — on every NPC through the same walk.
	for (const TCHAR* Name : { TEXT("strength"), TEXT("base_strength"), TEXT("humanity"),
		TEXT("base_humanity"), TEXT("masquerade"), TEXT("bloodpool"), TEXT("clan"),
		TEXT("generation"), TEXT("experience"), TEXT("vmax_health"), TEXT("intimidate"),
		TEXT("computers"), TEXT("celerity"), TEXT("base_celerity"), TEXT("active_obfuscate"),
		TEXT("gender_"), TEXT("gender") })
	{
		TestNotNull(*FString::Printf(TEXT("player.%s resolves as a field"), Name),
			reinterpret_cast<const void*>(FieldOn(Player, Name)));
		TestNotNull(*FString::Printf(TEXT("npc_VVampire.%s resolves too"), Name),
			reinterpret_cast<const void*>(FieldOn(Npc, Name)));
	}

	// The shadowing guard. `health` and `max_health` are CBaseEntity keyfields — `m_iHealth` and
	// `m_iMaxHealth` — and the sheet's own damage/ceiling slots are `vhealth`/`vmax_health`
	// precisely so they do not shadow them on the character chain. If a sheet slot ever took the
	// bare name, `trigger_hurt` would silently start writing the wrong number.
	{
		FElysiumPlayer Probe;
		Probe.Health = 42;
		Probe.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 7);
		const FElysiumFieldAccessor* Health = FieldOn(Player, TEXT("health"));
		if (TestNotNull(TEXT("`health` still resolves"), reinterpret_cast<const void*>(Health)))
		{
			TestEqual(TEXT("...to the entity keyfield, not the sheet's damage slot"),
				Health->Get(Probe).ToInt(), 42);
		}
		const FElysiumFieldAccessor* VHealth = FieldOn(Player, TEXT("vhealth"));
		if (TestNotNull(TEXT("`vhealth` resolves"), reinterpret_cast<const void*>(VHealth)))
		{
			TestEqual(TEXT("...to the sheet's damage slot"), VHealth->Get(Probe).ToInt(), 7);
		}

		// A write through the field lands on the base and carries to the current.
		const FElysiumFieldAccessor* Str = FieldOn(Player, TEXT("base_strength"));
		if (TestNotNull(TEXT("`base_strength` resolves"), reinterpret_cast<const void*>(Str)))
		{
			Str->Set(Probe, FElysiumVariant::Int(5));
			TestEqual(TEXT("a field write reaches the slot's base"),
				Probe.Sheet.GetBase(EC::Attributes, ElysiumSlot::Strength), 5);
			TestEqual(TEXT("...and the current with it"),
				FieldOn(Player, TEXT("strength"))->Get(Probe).ToInt(), 5);
		}
	}

	// Every sheet field is Save-flagged, so the whole sheet is in the save walk's enumeration.
	{
		const TArray<FName> SaveNames = Reg.SaveFields(*Player);
		TestTrue(TEXT("the save walk enumerates a sheet slot"), SaveNames.Contains(FName(TEXT("base_strength"))));
		TestTrue(TEXT("...both halves of it"), SaveNames.Contains(FName(TEXT("strength"))));
	}

	return true;
}

// =====================================================================================
// FElysiumEventQueue — the one time-sorted queue: ordering, FIFO ties, cancel-by-caller.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEventQueueTest, "Elysium.Substrate.EventQueue", GElysiumTestFlags)
bool FElysiumEventQueueTest::RunTest(const FString&)
{
	FElysiumEventQueue Queue;

	auto MakeEvent = [](double FireTime, const TCHAR* Target, const FElysiumEntityHandle& Caller)
	{
		FElysiumIOEvent Event;
		Event.FireTime = FireTime;
		Event.Target = Target;
		Event.Input = FName(TEXT("Trigger"));
		Event.Caller = Caller;
		return Event;
	};

	const FElysiumEntityHandle CallerA(1, 1);
	const FElysiumEntityHandle CallerB(2, 1);

	// Insert out of time order; the head must always be the earliest.
	Queue.Add(MakeEvent(3.0, TEXT("late"), CallerA));
	Queue.Add(MakeEvent(1.0, TEXT("early"), CallerA));
	Queue.Add(MakeEvent(2.0, TEXT("mid"), CallerB));

	TestEqual(TEXT("three queued"), Queue.Num(), 3);
	TestFalse(TEXT("nothing due before t=1"), Queue.HasDue(0.5));
	TestTrue(TEXT("head due at t=1"), Queue.HasDue(1.0));

	FElysiumIOEvent Out;
	TestTrue(TEXT("pop 1"), Queue.PopEarliest(Out));
	TestEqual(TEXT("earliest first"), Out.Target, FString(TEXT("early")));
	TestTrue(TEXT("pop 2"), Queue.PopEarliest(Out));
	TestEqual(TEXT("mid second"), Out.Target, FString(TEXT("mid")));

	// Equal fire times keep insertion (FIFO) order via the serial tiebreaker.
	Queue.Reset();
	Queue.Add(MakeEvent(5.0, TEXT("first"), CallerA));
	Queue.Add(MakeEvent(5.0, TEXT("second"), CallerA));
	Queue.Add(MakeEvent(5.0, TEXT("third"), CallerA));
	TestTrue(TEXT("pop a"), Queue.PopEarliest(Out));
	TestEqual(TEXT("FIFO first"), Out.Target, FString(TEXT("first")));
	TestTrue(TEXT("pop b"), Queue.PopEarliest(Out));
	TestEqual(TEXT("FIFO second"), Out.Target, FString(TEXT("second")));

	// Cancel drops exactly the events a given caller queued.
	Queue.Reset();
	Queue.Add(MakeEvent(1.0, TEXT("a"), CallerA));
	Queue.Add(MakeEvent(1.0, TEXT("b"), CallerB));
	Queue.Add(MakeEvent(1.0, TEXT("c"), CallerA));
	TestEqual(TEXT("cancel A removes 2"), Queue.Cancel(CallerA), 2);
	TestEqual(TEXT("B remains"), Queue.Num(), 1);

	// Pause/step flags the world's service loop honours.
	Queue.Reset();
	TestFalse(TEXT("starts unpaused"), Queue.IsPaused());
	Queue.Pause();
	TestTrue(TEXT("paused"), Queue.IsPaused());
	Queue.RequestSteps(3);
	TestEqual(TEXT("3 steps pending"), Queue.StepsPending(), 3);
	Queue.ConsumeStep();
	TestEqual(TEXT("2 steps after consume"), Queue.StepsPending(), 2);
	Queue.Resume();
	TestFalse(TEXT("resumed"), Queue.IsPaused());
	TestEqual(TEXT("resume clears steps"), Queue.StepsPending(), 0);

	return true;
}

// =====================================================================================
// S1 — the one clock and the one pause/time-scale facade over it (11.1).
// FElysiumGameClock keeps its writers private and friends only FElysiumTimeControl, so this
// exercises the facade, which is the only way the clock moves anywhere. Unbound (no game
// instance), so only the clock half applies — exactly the headless case.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTimeControlTest, "Elysium.Substrate.TimeControl", GElysiumTestFlags)
bool FElysiumTimeControlTest::RunTest(const FString&)
{
	FElysiumGameClock Clock;
	FElysiumTimeControl Time(Clock);

	TestEqual(TEXT("clock starts at 0"), Clock.GetNow(), 0.0);
	TestEqual(TEXT("scale starts at 1"), Time.GetScale(), 1.0);
	TestFalse(TEXT("starts running"), Time.IsPaused());

	// A frame advances by exactly the delta it is handed, within the frame bound.
	TestEqual(TEXT("a frame applies its whole delta"), Time.AdvanceFrame(0.05), 0.05);
	TestEqual(TEXT("now advanced"), Clock.GetNow(), 0.05);

	// Scale is applied EXACTLY ONCE (runtime-architecture.md §4). Engine dilation has already
	// scaled the tick's delta by the time it reaches AdvanceFrame, so the clock must multiply by
	// nothing: at 0.25x a 0.04 s delta is still 0.04 s of game time, not 0.01.
	Time.SetScale(0.25);
	TestEqual(TEXT("scale recorded on the clock"), Time.GetScale(), 0.25);
	TestEqual(TEXT("the clock adds no factor of its own"), Time.AdvanceFrame(0.04), 0.04);
	TestEqual(TEXT("now advanced by the dilated delta"), Clock.GetNow(), 0.09);
	Time.SetScale(1.0);

	// The frame bound (`source_movement.md` → "Frame timing"): VtMB's `Host_FilterTime` clamps
	// host_frametime to [0.001, 0.1] before the game DLL sees it, so a hitch cannot fire a whole
	// interval's thinks and queued I/O in one frame. **The mover clamps with this same constant** —
	// game time and player motion must not disagree about how long the frame was.
	TestEqual(TEXT("a hitch is bounded to MaxFrameSeconds"), Time.AdvanceFrame(5.0),
		ElysiumFrame::MaxFrameSeconds);
	TestEqual(TEXT("and a sub-millisecond frame is raised to the floor"), Time.AdvanceFrame(0.0001),
		ElysiumFrame::MinFrameSeconds);
	// A zero delta is passed through rather than raised: the engine's own filter never calls the
	// game with one, so inventing a millisecond here would fabricate time retail never advances.
	TestEqual(TEXT("but a zero delta stays zero"), Time.AdvanceFrame(0.0), 0.0);
	Time.ResetClock(0.09);

	// A negative scale is meaningless; time never runs backwards.
	Time.SetScale(-2.0);
	TestEqual(TEXT("negative scale clamps to 0"), Time.GetScale(), 0.0);
	Time.SetScale(1.0);

	// A hold stops the clock dead; the frame still ticks, it just buys no game time.
	Time.SetPaused(true);
	TestTrue(TEXT("held"), Time.IsPaused());
	TestEqual(TEXT("a held frame applies nothing"), Time.AdvanceFrame(1.0), 0.0);
	TestEqual(TEXT("now unmoved while held"), Clock.GetNow(), 0.09);

	// Stepping releases exactly N frames and holds again. EndFrame is the tail of a released
	// frame (the map actor's post-move tick), so one step = one Advance + one EndFrame.
	Time.StepFrames(2);
	TestFalse(TEXT("stepping releases the hold"), Time.IsPaused());
	TestEqual(TEXT("2 steps armed"), Time.StepsPending(), 2);

	Time.AdvanceFrame(0.1);
	Time.EndFrame();
	TestFalse(TEXT("still released after the first step"), Time.IsPaused());
	TestEqual(TEXT("1 step left"), Time.StepsPending(), 1);

	Time.AdvanceFrame(0.1);
	Time.EndFrame();
	TestTrue(TEXT("the last step re-holds the world"), Time.IsPaused());
	TestEqual(TEXT("no steps left"), Time.StepsPending(), 0);
	TestEqual(TEXT("exactly two frames of time bought"), Clock.GetNow(), 0.29);
	TestEqual(TEXT("held again, so nothing more"), Time.AdvanceFrame(1.0), 0.0);

	// Stepping a running world is meaningless — there is nothing to release.
	Time.SetPaused(false);
	Time.StepFrames(3);
	TestEqual(TEXT("no steps armed against a running world"), Time.StepsPending(), 0);

	// A hand pause/resume outranks a countdown: the steps were only holding the world open.
	Time.SetPaused(true);
	Time.StepFrames(5);
	Time.SetPaused(false);
	TestEqual(TEXT("resume clears armed steps"), Time.StepsPending(), 0);
	TestFalse(TEXT("resumed"), Time.IsPaused());

	// A rewind (fresh session / load restoring a saved curtime) clears everything.
	Time.SetScale(0.5);
	Time.SetPaused(true);
	Time.ResetClock(42.0);
	TestEqual(TEXT("rewound to the given curtime"), Clock.GetNow(), 42.0);
	TestEqual(TEXT("rewind restores real time"), Time.GetScale(), 1.0);
	TestFalse(TEXT("rewind releases the hold"), Time.IsPaused());

	return true;
}

// =====================================================================================
// The mover's arithmetic (4.7) — every number here is one `docs/source_movement.md` records
// off the decompile, asserted without a pawn, a world or an RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMovementTest, "Elysium.Substrate.Movement", GElysiumTestFlags)
bool FElysiumMovementTest::RunTest(const FString&)
{
	using namespace ElysiumMove;
	const FElysiumMoveTuning T;

	// FVector is double-precision; the tuning and the recorded constants are float. Narrow at the
	// comparison rather than widening every constant, so the numbers below read as the doc writes
	// them.
	auto F = [](double D) { return static_cast<float>(D); };

	// --- Friction: 3D speed, a stopspeed floor, and all three components scaled ---------------
	{
		// Below the 0.1 cut-off nothing happens at all.
		FVector V(0.05f, 0.0f, 0.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, 1.0f / 60.0f);
		TestEqual(TEXT("friction ignores a near-stopped body"), F(V.X), 0.05f);

		// Above stopspeed the drop is proportional to the speed itself.
		V = FVector(500.0f, 0.0f, 0.0f);
		const float Dt = 1.0f / 60.0f;
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		const float Expected = 500.0f - 500.0f * T.Friction * Dt;
		TestTrue(TEXT("friction drops control * friction * dt"), FMath::IsNearlyEqual(F(V.X), Expected, 0.01f));

		// Under stopspeed the *control* speed floors at sv_stopspeed, so a slow body loses a
		// constant amount rather than a proportional one — that is what makes the stop crisp.
		V = FVector(10.0f, 0.0f, 0.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		const float FlooredDrop = T.StopSpeed * T.Friction * Dt;
		TestTrue(TEXT("below stopspeed the control speed floors"),
			FMath::IsNearlyEqual(F(V.X), 10.0f - FlooredDrop, 0.01f));

		// It scales the vertical component too — the decompile does not special-case Z.
		V = FVector(300.0f, 0.0f, 300.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		TestTrue(TEXT("friction scales Z as well as XY"), F(V.Z) < 300.0f);
	}

	// --- Accelerate, and the air-accel asymmetry ----------------------------------------------
	{
		const float Dt = 1.0f / 60.0f;
		const FVector Dir(1.0f, 0.0f, 0.0f);

		FVector V = FVector::ZeroVector;
		ApplyAccelerate(V, Dir, RunSpeed, T.Accelerate, 1.0f, Dt);
		TestTrue(TEXT("ground accel adds accel * dt * wishspeed"),
			FMath::IsNearlyEqual(F(V.X), T.Accelerate * Dt * RunSpeed, 0.01f));

		// Already at the target: nothing is added.
		V = FVector(RunSpeed, 0.0f, 0.0f);
		ApplyAccelerate(V, Dir, RunSpeed, T.Accelerate, 1.0f, Dt);
		TestTrue(TEXT("ground accel adds nothing at the target"),
			FMath::IsNearlyEqual(F(V.X), RunSpeed, 0.01f));

		// The whole of air-strafing: the CAP binds the target, but the UNCAPPED wishspeed drives
		// accelspeed. Past the 30 u/s cap the air gives no more forward speed...
		V = FVector(AirSpeedCap + 10.0f, 0.0f, 0.0f);
		FVector Before = V;
		ApplyAirAccelerate(V, Dir, RunSpeed, T.AirAccel, T.AirSpeedCap, 1.0f, Dt);
		TestTrue(TEXT("air accel is capped along the wish direction"),
			FMath::IsNearlyEqual(F(V.X), F(Before.X), 0.01f));

		// ...but sideways, where the projected speed is still 0, it gives a full uncapped kick.
		// That asymmetry is why a Source player can gain speed by strafing in the air.
		V = FVector(1000.0f, 0.0f, 0.0f);
		const FVector Side(0.0f, 1.0f, 0.0f);
		ApplyAirAccelerate(V, Side, RunSpeed, T.AirAccel, T.AirSpeedCap, 1.0f, Dt);
		TestTrue(TEXT("but sideways it accelerates on the UNCAPPED wishspeed"),
			FMath::IsNearlyEqual(F(V.Y), FMath::Min(T.AirAccel * RunSpeed * Dt, AirSpeedCap), 0.01f));
		TestTrue(TEXT("and the sideways kick beats the capped target"), F(V.Y) > 0.0f);
	}

	// --- CheckVelocity clamps per COMPONENT, not per magnitude --------------------------------
	{
		FVector V(T.MaxVelocity * 2.0f, T.MaxVelocity * 2.0f, 0.0f);
		CheckVelocity(V, T.MaxVelocity);
		TestTrue(TEXT("each component clamps to sv_maxvelocity"),
			FMath::IsNearlyEqual(F(V.X), T.MaxVelocity, 0.01f) &&
			FMath::IsNearlyEqual(F(V.Y), T.MaxVelocity, 0.01f));
		// The distinction from GetClampedToMaxSize: the magnitude is allowed past the limit.
		TestTrue(TEXT("so the magnitude may exceed it — this is not a size clamp"),
			F(V.Size()) > T.MaxVelocity);

		V = FVector(FMath::Sqrt(-1.0f), 0.0f, 0.0f);
		CheckVelocity(V, T.MaxVelocity);
		TestTrue(TEXT("and a non-finite component is scrubbed"), F(V.X) == 0.0f);
	}

	// --- ClipVelocity's blocked bits ----------------------------------------------------------
	{
		FVector Out;
		const int32 Floor = ClipVelocity(FVector(100.0f, 0.0f, -100.0f), FVector::UpVector, Out);
		TestEqual(TEXT("a floor plane reports bit 1"), Floor, 1);
		TestTrue(TEXT("and the downward component is removed"), FMath::IsNearlyEqual(F(Out.Z), 0.0f, 0.01f));

		const int32 Wall = ClipVelocity(FVector(100.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f), Out);
		TestEqual(TEXT("a vertical wall reports bit 2"), Wall, 2);
		TestTrue(TEXT("and the into-wall component is removed"), FMath::IsNearlyEqual(F(Out.X), 0.0f, 0.01f));
	}

	// --- The jump: a held push under reduced gravity, not a single impulse --------------------
	{
		// The four numbers are `rules.txt`'s, not `CGameMovement` constants. sv_jump_boost is an
		// origin pop measured in inches, NOT an apex height — asserting that here is what stops
		// the old `sqrt(2 * boost * g)` reading coming back.
		TestTrue(TEXT("BaseJumpVelocity is 185 Source units/s"),
			FMath::IsNearlyEqual(T.BaseJumpVelocity, 185.0f * U, 0.5f));
		TestTrue(TEXT("gravity runs at 0.75 during a jump"),
			FMath::IsNearlyEqual(T.JumpGravityMultiplier, 0.75f, 1e-4f));
		TestTrue(TEXT("and the push window is JumpHoldTime"),
			FMath::IsNearlyEqual(T.JumpHoldSeconds, 0.2f, 1e-4f));
		TestTrue(TEXT("sv_jump_boost is an origin pop in inches, not an apex"),
			FMath::IsNearlyEqual(T.JumpBoost, 25.0f, 1e-4f));

		// The ballistic tail, once the push window has closed. The half-step split is exact for
		// constant acceleration, so this piece is dt-invariant even though the height it reaches
		// is no longer `sv_jump_boost`.
		// ApexHeight already returns Source units.
		const float TailApex = ApexHeight(T.BaseJumpVelocity,
			T.Gravity * T.JumpGravityMultiplier);
		TestTrue(TEXT("the ballistic tail alone clears 28 units"), TailApex > 28.0f);

		// Integrate the whole thing the way FullWalkMove does, holding the button for the window.
		auto SimulateApex = [&T](float Dt)
		{
			const float G = T.Gravity * T.JumpGravityMultiplier;
			FVector V(0.0f, 0.0f, T.BaseJumpVelocity);
			float Z = T.JumpBoost * U * ElysiumMove::JumpBoostScale;   // the press-frame pop
			float Peak = Z;
			float Hold = T.JumpHoldSeconds;
			for (int32 i = 0; i < 4096 && (V.Z > 0.0f || Z > 0.0f); ++i)
			{
				StartGravity(V, G, Dt);
				// The held push re-asserts the launch speed while the window is open.
				if (Hold > 0.0f) { V.Z = FMath::Max(V.Z, T.BaseJumpVelocity); Hold -= Dt; }
				Z += V.Z * Dt;
				FinishGravity(V, G, Dt);
				Peak = FMath::Max(Peak, Z);
			}
			return Peak / U;
		};

		const float Apex60 = SimulateApex(1.0f / 60.0f);
		const float Apex120 = SimulateApex(1.0f / 120.0f);
		const float Apex240 = SimulateApex(1.0f / 240.0f);

		// The headline: the reachable height is far above the 25 units the old model produced,
		// which is what made the tutorial's crate stack unclimbable.
		TestTrue(TEXT("a held jump clears well past 25 units"), Apex60 > 60.0f);
		// The push window is wall-clock, so the three rates agree to within a frame's worth of it.
		TestTrue(TEXT("and 120 fps agrees with 60"), FMath::Abs(Apex120 - Apex60) < 4.0f);
		TestTrue(TEXT("and 240 fps agrees with 60"), FMath::Abs(Apex240 - Apex60) < 4.0f);

		// A tap is shorter than a hold — the mousewheel-vs-spacebar difference players report.
		TestTrue(TEXT("the ballistic tail alone is shorter than a full held jump"),
			TailApex < Apex60);
	}

	// --- The timestep: 0 is the faithful path, a fixed step carries its remainder --------------
	{
		FElysiumMoveStepper Stepper;
		float Step = 0.0f;

		// Faithful: one step, at exactly the delta handed in.
		TestFalse(TEXT("the default stepper is the raw variable delta"), Stepper.IsFixed());
		TestEqual(TEXT("raw mode runs one step"), Stepper.BeginFrame(0.0321f, Step), 1);
		TestTrue(TEXT("at the frame's own delta"), FMath::IsNearlyEqual(Step, 0.0321f, 1e-6f));

		// Fixed: whole steps now, remainder carried rather than dropped.
		Stepper.Reset();
		Stepper.FixedStep = 0.01f;
		TestEqual(TEXT("a 25 ms frame at a 10 ms step runs two"), Stepper.BeginFrame(0.025f, Step), 2);
		TestTrue(TEXT("each at the fixed interval"), FMath::IsNearlyEqual(Step, 0.01f, 1e-6f));
		// The carried 5 ms plus another 25 ms is 30 ms — three steps, not two.
		TestEqual(TEXT("and the carried remainder lands the third step next frame"),
			Stepper.BeginFrame(0.025f, Step), 3);

		// The sub-step backstop: with the frame delta already bounded this is unreachable in
		// practice, so it exists to stop an absurd FixedStep from hanging the frame.
		Stepper.Reset();
		Stepper.FixedStep = 0.0001f;
		Stepper.MaxSubSteps = 4;
		TestEqual(TEXT("the sub-step count is capped"), Stepper.BeginFrame(0.1f, Step), 4);
		TestTrue(TEXT("and the backlog is dropped, not carried into the next frame"),
			Stepper.Alpha() == 0.0f);
	}

	// --- The hulls (RE22) and the frame bound --------------------------------------------------
	{
		// Read off the CGameMovement constructor: the ducked hull keeps the standing footprint and
		// halves the height, and its eye is at 30 — not stock Source's VEC_DUCK_VIEW of 28.
		TestTrue(TEXT("the standing hull is 72u tall"), FMath::IsNearlyEqual(StandHeight / U, 72.0f, 0.01f));
		TestTrue(TEXT("the ducked hull is 36u"), FMath::IsNearlyEqual(DuckHeight / U, 36.0f, 0.01f));
		TestTrue(TEXT("the standing eye is at 64u"), FMath::IsNearlyEqual(StandViewZ / U, 64.0f, 0.01f));
		TestTrue(TEXT("and the ducked eye at 30u, not Source's 28"),
			FMath::IsNearlyEqual(DuckViewZ / U, 30.0f, 0.01f));

		// The frame bound is ONE number, and the mover and the clock must clamp with it together.
		TestTrue(TEXT("a hitch bounds to 0.1 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0), 0.1, 1e-9));
		TestTrue(TEXT("a sub-millisecond frame floors at 0.001 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(0.00001), 0.001, 1e-9));
		TestTrue(TEXT("and zero passes through rather than fabricating time"),
			ElysiumFrame::ClampFrameDelta(0.0) == 0.0);
	}

	// --- The cvar surface is declared, so a config.cfg governs and nothing falls to Python -----
	{
		TestTrue(TEXT("the sv_* movement surface is declared"), CvarDefs().Num() >= 9);

		TMap<FString, FString> Store;
		auto Lookup = [&Store](const TCHAR* Name)
		{
			const FString* Found = Store.Find(Name);
			return Found ? *Found : FString();
		};

		FElysiumMoveTuning Tuned;
		Tuned.LoadFrom(Lookup);
		TestTrue(TEXT("an empty store keeps VtMB's own defaults"),
			FMath::IsNearlyEqual(Tuned.Gravity, Gravity, 0.01f));

		Store.Add(TEXT("sv_gravity"), TEXT("400"));
		Store.Add(TEXT("sv_jump_boost"), TEXT("100"));
		Tuned.LoadFrom(Lookup);
		TestTrue(TEXT("a console value wins, converted from Source units once"),
			FMath::IsNearlyEqual(Tuned.Gravity, 400.0f * U, 0.01f));
		// The pop stays in Source units because it *is* a distance in inches — read back literally.
		TestTrue(TEXT("and sv_jump_boost reads back in Source units, unconverted"),
			FMath::IsNearlyEqual(Tuned.JumpBoost, 100.0f, 0.01f));
	}

	return true;
}

// =====================================================================================
// The application state machine (11.3, runtime-architecture.md §10). The transition table is
// plain C++ with no game instance behind it, so the whole rule set is asserted here rather than
// inferred from a play-through — including the two rules the acceptance turns on: the front end
// deliberately does not pause, and Boot is reachable from nowhere.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAppStateTest, "Elysium.Substrate.AppState", GElysiumTestFlags)
bool FElysiumAppStateTest::RunTest(const FString&)
{
	using EState = EElysiumAppState;
	static const EState All[] = { EState::Boot, EState::FrontEnd, EState::Loading,
		EState::Playing, EState::Paused, EState::GameOver };

	// Every state names itself, and every name round-trips — the console verbs and the agent
	// surface both speak these strings.
	for (const EState S : All)
	{
		EState Parsed = EState::Boot;
		TestTrue(TEXT("state name parses back"), ElysiumAppState::Parse(ElysiumAppState::Name(S), Parsed));
		TestEqual(TEXT("round trip"), static_cast<int32>(Parsed), static_cast<int32>(S));
	}
	EState Unused = EState::Boot;
	TestFalse(TEXT("a non-state does not parse"), ElysiumAppState::Parse(TEXT("Menu"), Unused));

	// Every entry point is idempotent: re-entering the state you are in is never an error.
	for (const EState S : All)
	{
		TestTrue(TEXT("self-transition is legal"), ElysiumAppState::CanEnter(S, S));
	}

	// **Pause is reachable only from Playing.** The front end running a live backdrop behind the
	// menu is the feature (8.6), so Esc there must be a no-op rather than a hold.
	TestTrue(TEXT("Playing pauses"), ElysiumAppState::CanEnter(EState::Playing, EState::Paused));
	TestFalse(TEXT("FrontEnd does not pause"), ElysiumAppState::CanEnter(EState::FrontEnd, EState::Paused));
	TestFalse(TEXT("Loading does not pause"), ElysiumAppState::CanEnter(EState::Loading, EState::Paused));
	TestFalse(TEXT("GameOver does not pause"), ElysiumAppState::CanEnter(EState::GameOver, EState::Paused));

	// Boot is decided once, at game-instance init, and never returned to.
	for (const EState S : All)
	{
		if (S != EState::Boot)
		{
			TestFalse(TEXT("nothing re-enters Boot"), ElysiumAppState::CanEnter(S, EState::Boot));
		}
	}

	// Any state can travel — New Game, Load, Reload, quit-to-menu, and a trigger_changelevel the
	// substrate fires on its own all pass through Loading.
	for (const EState S : All)
	{
		TestTrue(TEXT("every state can travel"), ElysiumAppState::CanEnter(S, EState::Loading));
	}

	// A world only becomes playable by arriving in one (or by leaving the pause menu).
	TestTrue(TEXT("Loading lands in Playing"), ElysiumAppState::CanEnter(EState::Loading, EState::Playing));
	TestTrue(TEXT("Paused resumes"), ElysiumAppState::CanEnter(EState::Paused, EState::Playing));
	TestFalse(TEXT("FrontEnd cannot become Playing without a load"),
		ElysiumAppState::CanEnter(EState::FrontEnd, EState::Playing));
	TestFalse(TEXT("a lost run cannot simply resume"),
		ElysiumAppState::CanEnter(EState::GameOver, EState::Playing));

	// The front end is only ever arrived at: cold boot, or a quit-to-menu travel landing in the
	// backdrop world.
	TestTrue(TEXT("boot raises the front end"), ElysiumAppState::CanEnter(EState::Boot, EState::FrontEnd));
	TestTrue(TEXT("quit-to-menu lands in the front end"),
		ElysiumAppState::CanEnter(EState::Loading, EState::FrontEnd));
	TestFalse(TEXT("Playing cannot jump to the front end"),
		ElysiumAppState::CanEnter(EState::Playing, EState::FrontEnd));

	// A run can be lost from play or from the pause menu, and from nowhere else.
	TestTrue(TEXT("death ends a running run"), ElysiumAppState::CanEnter(EState::Playing, EState::GameOver));
	TestTrue(TEXT("death ends a held run"), ElysiumAppState::CanEnter(EState::Paused, EState::GameOver));
	TestFalse(TEXT("the front end cannot die"),
		ElysiumAppState::CanEnter(EState::FrontEnd, EState::GameOver));

	// The two derived predicates the UI and the time facade read.
	TestFalse(TEXT("Boot has no session"), ElysiumAppState::IsInSession(EState::Boot));
	TestFalse(TEXT("FrontEnd has no session"), ElysiumAppState::IsInSession(EState::FrontEnd));
	TestTrue(TEXT("Playing is a session"), ElysiumAppState::IsInSession(EState::Playing));
	TestTrue(TEXT("Paused is a session"), ElysiumAppState::IsInSession(EState::Paused));
	TestTrue(TEXT("GameOver is a session"), ElysiumAppState::IsInSession(EState::GameOver));

	TestTrue(TEXT("Paused holds the world"), ElysiumAppState::HoldsWorld(EState::Paused));
	TestTrue(TEXT("GameOver holds the world"), ElysiumAppState::HoldsWorld(EState::GameOver));
	TestFalse(TEXT("Playing runs the world"), ElysiumAppState::HoldsWorld(EState::Playing));
	TestFalse(TEXT("FrontEnd runs the world"), ElysiumAppState::HoldsWorld(EState::FrontEnd));

	return true;
}

// =====================================================================================
// S6 — the input scope stack (11.5, runtime-architecture.md §8.1). The arbitration is plain
// C++, so the whole rule set is asserted with no local player, no controller and no viewport:
// what the top scope resolves to, what an out-of-order pop restores, and that the stack is
// balanced across every screen transition the game can make.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInputScopesTest, "Elysium.Substrate.InputScopes", GElysiumTestFlags)
bool FElysiumInputScopesTest::RunTest(const FString&)
{
	using EMode = EElysiumInputMode;
	namespace Prio = ElysiumInput::Priority;

	// The scopes the game actually pushes, as data — so a transition is a pair of these rather
	// than a hand-written sequence, and the table below can walk every ordered pair.
	auto MakeScope = [](const TCHAR* Name, int32 Priority, EMode Mode, bool bCursor)
	{
		FElysiumInputScope Scope;
		Scope.Name = Name;
		Scope.Priority = Priority;
		Scope.Mode = Mode;
		Scope.bShowCursor = bCursor;
		return Scope;
	};
	const FElysiumInputScope Sign      = MakeScope(TEXT("Sign"),      Prio::Sign,      EMode::GameOnly,  false);
	const FElysiumInputScope Cinematic = MakeScope(TEXT("Cinematic"), Prio::Cinematic, EMode::GameOnly,  false);
	const FElysiumInputScope Chargen   = MakeScope(TEXT("Chargen"),   Prio::Chargen,   EMode::UIOnly,    true);
	const FElysiumInputScope Dialogue  = MakeScope(TEXT("Dialogue"),  Prio::Dialogue,  EMode::UIOnly,    true);
	const FElysiumInputScope Menu      = MakeScope(TEXT("Menu"),      Prio::Menu,      EMode::UIOnly,    true);
	const FElysiumInputScope Debug     = MakeScope(TEXT("Debug"),     Prio::Debug,     EMode::GameOnly,  true);

	// --- the empty stack is the game holding the mouse ---
	{
		FElysiumInputScopeStack Stack;
		const FElysiumInputState Base = Stack.Resolve();
		TestTrue(TEXT("an empty stack has no top"), Stack.Top() == nullptr);
		TestTrue(TEXT("empty resolves to the gameplay default"), Base.Mode == EMode::GameOnly);
		TestFalse(TEXT("no cursor over the world"), Base.bShowCursor);
		TestTrue(TEXT("no deciding scope"), Base.Name.IsNone());
	}

	// --- the top decides everything, and it is the priority top, not the last push ---
	{
		FElysiumInputScopeStack Stack;
		FElysiumInputScopeHandle MenuH = Stack.Push(Menu);
		Stack.Push(Sign);   // a sign opening under a menu changes nothing
		TestEqual(TEXT("the menu still decides"), Stack.Resolve().Name, FName(TEXT("Menu")));
		TestTrue(TEXT("still UI-only"), Stack.Resolve().Mode == EMode::UIOnly);

		// F1 over a menu: the debug UI is the top of the table on purpose.
		Stack.Push(Debug);
		TestEqual(TEXT("debug outranks a menu"), Stack.Resolve().Name, FName(TEXT("Debug")));
		TestTrue(TEXT("and stands the menu's UI-only mode down"), Stack.Resolve().Mode == EMode::GameOnly);
		Stack.PopByName(TEXT("Debug"));
		TestEqual(TEXT("closing it restores the menu"), Stack.Resolve().Name, FName(TEXT("Menu")));

		TestTrue(TEXT("the menu handle is still the menu's"), Stack.Pop(MenuH));
		TestEqual(TEXT("the sign underneath is restored"), Stack.Resolve().Name, FName(TEXT("Sign")));
		TestTrue(TEXT("and with it game input"), Stack.Resolve().Mode == EMode::GameOnly);
	}

	// --- same priority stacks like modals: the later push wins, and popping it restores the earlier ---
	{
		FElysiumInputScopeStack Stack;
		FElysiumInputScope First = Menu;
		First.Contexts.Add(TEXT("MenuContext"));
		FElysiumInputScope Second = Menu;
		Second.Name = TEXT("Menu2");
		Second.Contexts.Add(TEXT("OptionsContext"));

		Stack.Push(First);
		FElysiumInputScopeHandle SecondH = Stack.Push(Second);
		TestEqual(TEXT("the later same-priority push wins"), Stack.Resolve().Name, FName(TEXT("Menu2")));
		TestEqual(TEXT("contexts follow the top"), Stack.Resolve().Contexts.Num(), 1);
		TestEqual(TEXT("and they are the top's"), Stack.Resolve().Contexts[0], FName(TEXT("OptionsContext")));

		Stack.Pop(SecondH);
		TestEqual(TEXT("the first menu is back"), Stack.Resolve().Name, FName(TEXT("Menu")));
		TestEqual(TEXT("with its own contexts"), Stack.Resolve().Contexts[0], FName(TEXT("MenuContext")));
	}

	// --- a stale or double pop takes nothing else down (ids are never reused) ---
	{
		FElysiumInputScopeStack Stack;
		FElysiumInputScopeHandle DialogueH = Stack.Push(Dialogue);
		TestTrue(TEXT("popped once"), Stack.Pop(DialogueH));
		TestFalse(TEXT("popping the same handle again does nothing"), Stack.Pop(DialogueH));

		FElysiumInputScopeHandle MenuH = Stack.Push(Menu);
		TestFalse(TEXT("the stale handle is not the new scope's"), Stack.Pop(DialogueH));
		TestEqual(TEXT("so the menu survives it"), Stack.Num(), 1);
		TestTrue(TEXT("and its own handle still works"), Stack.Pop(MenuH));
		TestTrue(TEXT("balanced"), Stack.IsEmpty());

		FElysiumInputScopeHandle Never;
		TestFalse(TEXT("an unpushed handle pops nothing"), Stack.Pop(Never));
	}

	// --- the acceptance: opening any screen over any other restores exactly the mode it found,
	//     and the stack is balanced afterwards. Every ordered pair, both close orders — including
	//     the out-of-order one (the thing under closes first, e.g. a conversation ending behind an
	//     open pause menu).
	{
		const TArray<FElysiumInputScope> Screens = { Sign, Cinematic, Chargen, Dialogue, Menu, Debug };
		for (const FElysiumInputScope& Under : Screens)
		{
			for (const FElysiumInputScope& Over : Screens)
			{
				FElysiumInputScopeStack Stack;
				const FElysiumInputScopeHandle UnderH = Stack.Push(Under);
				const FElysiumInputState Found = Stack.Resolve();

				const FElysiumInputScopeHandle OverH = Stack.Push(Over);

				// Last-in-first-out: the top goes away and what was underneath is exactly restored.
				FElysiumInputScopeStack Lifo = Stack;
				Lifo.Pop(OverH);
				TestTrue(*FString::Printf(TEXT("%s over %s restores what it found"),
					*Over.Name.ToString(), *Under.Name.ToString()), Lifo.Resolve() == Found);
				Lifo.Pop(UnderH);
				TestTrue(*FString::Printf(TEXT("%s/%s balances"),
					*Under.Name.ToString(), *Over.Name.ToString()), Lifo.IsEmpty());

				// Out of order: the first-pushed screen closes first. When it is not the one deciding,
				// nothing may move — that is the conversation ending behind an open pause menu.
				FElysiumInputScopeStack Fifo = Stack;
				const FElysiumInputState Top = Fifo.Resolve();
				const bool bUnderDecides = Fifo.Top() && Fifo.Top()->Handle == UnderH;
				Fifo.Pop(UnderH);
				if (!bUnderDecides)
				{
					TestTrue(*FString::Printf(TEXT("%s closing under %s leaves the top alone"),
						*Under.Name.ToString(), *Over.Name.ToString()), Fifo.Resolve() == Top);
				}
				Fifo.Pop(OverH);
				TestTrue(*FString::Printf(TEXT("%s/%s balances out of order"),
					*Under.Name.ToString(), *Over.Name.ToString()), Fifo.IsEmpty());
			}
		}
	}

	// --- Cog can never eat a menu click: a scope that takes the mouse off the world revokes an
	//     inherited ImGui capture. The subsystem does the revoking; this is the rule it asks.
	TestTrue(TEXT("a menu revokes debug capture"), ElysiumInput::RevokesDebugCapture(Menu));
	TestTrue(TEXT("so does a conversation"), ElysiumInput::RevokesDebugCapture(Dialogue));
	TestTrue(TEXT("so does chargen"), ElysiumInput::RevokesDebugCapture(Chargen));
	TestFalse(TEXT("a sign does not — it is dismissed by a world click"),
		ElysiumInput::RevokesDebugCapture(Sign));
	TestFalse(TEXT("nor does a cutscene"), ElysiumInput::RevokesDebugCapture(Cinematic));
	TestFalse(TEXT("and the debug scope never revokes itself"), ElysiumInput::RevokesDebugCapture(Debug));

	// The priority table is the ordering the whole system rests on; state it once, here.
	TestTrue(TEXT("gameplay is the floor"), Prio::Game < Prio::Sign);
	TestTrue(TEXT("a cutscene outranks a sign"), Prio::Sign < Prio::Cinematic);
	TestTrue(TEXT("chargen outranks a cutscene"), Prio::Cinematic < Prio::Chargen);
	TestTrue(TEXT("a conversation outranks chargen"), Prio::Chargen < Prio::Dialogue);
	TestTrue(TEXT("a menu outranks a conversation"), Prio::Dialogue < Prio::Menu);
	TestTrue(TEXT("F1 outranks everything"), Prio::Menu < Prio::Debug);

	return true;
}

// =====================================================================================
// S2 — the frame order (runtime-architecture.md §3), asserted at both levels it is declared
// at: the engine tick table (tick groups + the pause split, read off the class defaults —
// the late-bound prerequisites are wired at registration and belong to the Play tier), and
// the substrate's own two-pass drive inside one frame.
//
// The order is RETAIL's, and it is move-FIRST (RE21): the engine runs the whole
// ProcessUsercmds -> CPlayerMove::RunCommand chain while draining the client's `clc_move`
// message, strictly before SV_Frame calls GameFrame — so the pawn has already moved by the
// time the first think or queued event runs.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFrameOrderTest, "Elysium.Substrate.FrameOrder", GElysiumTestFlags)
bool FElysiumFrameOrderTest::RunTest(const FString&)
{
	// --- the tick table: declared with tick groups, never inferred from registration order ---
	const AElysiumMapActor* Map = GetDefault<AElysiumMapActor>();
	if (!TestNotNull(TEXT("map actor class defaults"), Map))
	{
		return false;
	}

	// Steps 2-3 run before physics AND before the move: the clock advances, a freshly seated pawn is
	// placed and frozen, and the player entity's own think runs — the RunCommand shell retail wraps
	// the move in.
	TestTrue(TEXT("the pre-move pass ticks"), Map->PreMoveTickFunction.bCanEverTick);
	TestTrue(TEXT("the pre-move pass starts enabled"), Map->PreMoveTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the pre-move pass is TG_PrePhysics"),
		static_cast<int32>(Map->PreMoveTickFunction.TickGroup), static_cast<int32>(TG_PrePhysics));

	// Steps 5-6 also run before physics, but AFTER the pawn's move — this is retail's GameFrame,
	// and the move is not in it. Both passes share TG_PrePhysics, so what separates them is the
	// prerequisite the map actor wires at registration, not the group; the Play tier reads that
	// chain back off the running graph.
	TestTrue(TEXT("the gameplay pass ticks"), Map->PrimaryActorTick.bCanEverTick);
	TestEqual(TEXT("the gameplay pass is TG_PrePhysics"),
		static_cast<int32>(Map->PrimaryActorTick.TickGroup), static_cast<int32>(TG_PrePhysics));

	// Step 4 — the pawn's mover is in the same group as both, for the same reason: it is ordered by
	// prerequisite, between them.
	const UElysiumMovementComponent* Mover = GetDefault<UElysiumMovementComponent>();
	if (TestNotNull(TEXT("movement component class defaults"), Mover))
	{
		TestTrue(TEXT("the mover ticks"), Mover->PrimaryComponentTick.bCanEverTick);
		TestEqual(TEXT("the mover is TG_PrePhysics, between the two gameplay passes"),
			static_cast<int32>(Mover->PrimaryComponentTick.TickGroup), static_cast<int32>(TG_PrePhysics));
	}

	// Step 8 runs after physics, on the same actor: the +use cursor traces against the frame's
	// final positions. Three tick functions, not three actors — one actor keeps the order declarable.
	TestTrue(TEXT("the post-move pass ticks"), Map->PostMoveTickFunction.bCanEverTick);
	TestTrue(TEXT("the post-move pass starts enabled"), Map->PostMoveTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the post-move pass is TG_PostPhysics"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup), static_cast<int32>(TG_PostPhysics));
	TestTrue(TEXT("the post-move pass is later than both pre-physics passes"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup)
			> static_cast<int32>(Map->PreMoveTickFunction.TickGroup));

	// Step 10 rebuilds the view state after everything that could change it has run, so it is later
	// than all three gameplay passes (11.8).
	const UElysiumPresentationSubsystem* Present = GetDefault<UElysiumPresentationSubsystem>();
	if (TestNotNull(TEXT("presentation subsystem class defaults"), Present))
	{
		TestTrue(TEXT("the publish pass ticks"), Present->PublishTickFunction.bCanEverTick);
		TestTrue(TEXT("the publish pass starts enabled"), Present->PublishTickFunction.bStartWithTickEnabled);
		TestEqual(TEXT("the publish pass is TG_PostUpdateWork"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup), static_cast<int32>(TG_PostUpdateWork));
		TestTrue(TEXT("the publish pass is last of the four"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup)
				> static_cast<int32>(Map->PostMoveTickFunction.TickGroup));
	}

	// §4 — a hold stops the world and keeps the screen alive. Gameplay ticks are false on all three
	// passes; the publish pass, which is presentation, is true — pause is exactly when a dialogue
	// box has to be taken down, and that comes off a publish.
	TestFalse(TEXT("the pre-move pass stops when held"), Map->PreMoveTickFunction.bTickEvenWhenPaused);
	TestFalse(TEXT("the gameplay pass stops when held"), Map->PrimaryActorTick.bTickEvenWhenPaused);
	TestFalse(TEXT("the post-move pass stops when held"), Map->PostMoveTickFunction.bTickEvenWhenPaused);
	if (Present)
	{
		TestTrue(TEXT("presentation keeps publishing when held"),
			Present->PublishTickFunction.bTickEvenWhenPaused);
	}

	// The HUD itself does not tick at all: it reconciles from the publisher's OnViewPublished, which
	// is a frame later if it comes off an actor tick in TG_PrePhysics.
	const AElysiumHUD* Hud = GetDefault<AElysiumHUD>();
	if (TestNotNull(TEXT("HUD class defaults"), Hud))
	{
		TestFalse(TEXT("the HUD does not tick"), Hud->PrimaryActorTick.bCanEverTick);
	}

	// --- steps 5-6: think first, then service the queue (RE2's retail order) ---------------
	// A logic_timer due this frame fires OnTimer from its Think; the wire it fires lands in the
	// queue at zero delay. Think-first means the same tick's queue pass delivers it, so the
	// counter has moved after ONE tick. Queue-first would leave it for the next frame.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test_frame__");

	FElysiumEntityDef Timer;
	Timer.Classname = TEXT("logic_timer");
	Timer.TargetName = TEXT("timer1");
	Timer.Keys.Add(TEXT("RefireTime"), TEXT("1"));
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTimer");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Timer.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Timer));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	// Null game state, so the world's own Now() reads 0 and the tick's `Now` is whatever the
	// caller passes — which is what lets the test place the think's due time by hand.
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));

	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("counter1 resolved"), CounterEntity))
	{
		return false;
	}
	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.f;
	};

	// Before the timer is due nothing thinks and nothing is queued.
	World.Tick(0.5);
	TestEqual(TEXT("nothing before the think is due"), CounterValue(CounterEntity), 0.f);

	// The frame the think is due: its output is delivered in that same frame's queue pass.
	World.Tick(1.0);
	TestEqual(TEXT("the think's wire lands in the same frame's queue pass"),
		CounterValue(CounterEntity), 5.f);

	// --- steps 3 vs 5: the player thinks on the OTHER side of the move ----------------------
	// Retail runs the player's own think inside CPlayerMove::RunCommand, around the move, and not
	// in Physics_RunThinkFunctions. So the world is driven twice a frame and the player is the one
	// entity the general think pass must skip — otherwise it thinks twice, and the second one lands
	// after the move instead of before it.
	FElysiumEntityDefs PlayerDefs;
	PlayerDefs.MapName = TEXT("__test_player_think__");
	FElysiumEntityWorld PlayerWorld(/*Owner*/ nullptr, /*GameState*/ nullptr);
	PlayerWorld.Load(MoveTemp(PlayerDefs));
	PlayerWorld.SpawnPlayer();

	FElysiumPlayer* PlayerEnt = PlayerWorld.FindPlayer();
	if (TestNotNull(TEXT("the player entity spawned"), PlayerEnt))
	{
		// Arm it due, then run the pre-move pass: the think fires and disarms itself.
		PlayerEnt->NextThink = 1.0f;
		PlayerWorld.RunPlayerThink(2.0);
		TestEqual(TEXT("the pre-move pass runs the player's think"),
			PlayerEnt->NextThink, ELYSIUM_NEVER_THINK);

		// Arm it again and run the POST-move pass instead. The general think pass skips the player,
		// so it stays armed — the pre-move pass is the only thing that may fire it.
		PlayerEnt->NextThink = 1.0f;
		PlayerWorld.Tick(2.0);
		TestEqual(TEXT("the post-move think pass leaves the player alone"),
			PlayerEnt->NextThink, 1.0f);
	}

	return true;
}

// =====================================================================================
// FElysiumClassRegistry — the case-folded base-chain walk that drives all I/O dispatch.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRegistryTest, "Elysium.Substrate.Registry", GElysiumTestFlags)
bool FElysiumRegistryTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();

	// The base is always registered at module load.
	const FElysiumClassDesc* Base = Registry.BaseDesc();
	if (!TestNotNull(TEXT("CBaseEntity base registered"), Base))
	{
		return false;
	}

	// A known leaf class links up to the base.
	const FElysiumClassDesc* Relay = Registry.Find(FName(TEXT("logic_relay")));
	if (!TestNotNull(TEXT("logic_relay registered"), Relay))
	{
		return false;
	}

	// Its own input resolves.
	TestNotNull(TEXT("logic_relay.Trigger resolves"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("Trigger")))));

	// A base input resolves through the chain from the leaf (case-folded).
	TestNotNull(TEXT("inherited Kill resolves"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("Kill")))));
	TestNotNull(TEXT("Kill folds case"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("kILL")))));

	// A nonsense input resolves to nothing.
	TestNull(TEXT("unknown input is null"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("NoSuchInput")))));

	// An unregistered classname is not found (the world falls it back to an inert record).
	TestNull(TEXT("unknown class not found"), Registry.Find(FName(TEXT("not_a_real_class_xyz"))));

	return true;
}

// =====================================================================================
// End-to-end I/O through a bare world: logic_relay -> math_counter, no bodies, no PIE.
// Exercises both chokepoints (AcceptInput + the event queue), FireOutput, and the
// "falsy when dead" identity contract.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumIOChainTest, "Elysium.Substrate.IOChain", GElysiumTestFlags)
bool FElysiumIOChainTest::RunTest(const FString&)
{
	// Two point entities wired in code: firing the relay's Trigger re-fires OnTrigger, which the
	// def routes to the counter's Add(5). No brush entities, so BuildBrushBody never touches the
	// (null) owner actor; GameState null means the clock reads 0 and Python no-ops — neither is
	// needed to exercise the I/O bus.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	FElysiumEntityDef Relay;
	Relay.Classname = TEXT("logic_relay");
	Relay.TargetName = TEXT("relay1");
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTrigger");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Relay.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Relay));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));

	TestEqual(TEXT("two entities loaded"), World.NumEntities(), 2);

	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	FElysiumEntity* RelayEntity = World.FindByName(TEXT("relay1"));
	if (!TestNotNull(TEXT("counter1 resolved"), CounterEntity) ||
		!TestNotNull(TEXT("relay1 resolved"), RelayEntity))
	{
		return false;
	}

	// Reads the counter's live Value out of its debug-state rows (the concrete class is file-local
	// to the .cpp, so this base virtual is the seam).
	auto CounterValue = [](const FElysiumEntity* Entity) -> FString
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return Row.Value;
			}
		}
		return FString();
	};

	TestEqual(TEXT("counter starts at 0"), FCString::Atof(*CounterValue(CounterEntity)), 0.0f);

	// Fire through the real queue chokepoint, addressed at the relay as `!self`.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), RelayEntity->Handle);

	// Drain: every event fires at t=0 (null clock), so a couple of ticks carries the whole chain.
	for (int32 i = 0; i < 4; ++i)
	{
		World.Tick(0.0);
	}

	TestEqual(TEXT("counter advanced to 5 via the I/O chain"),
		FCString::Atof(*CounterValue(CounterEntity)), 5.0f);
	TestTrue(TEXT("history recorded the delivery"), World.RingBuffer().Num() > 0);

	// Identity: a live handle resolves; after Kill it reads falsy (the contract scripts rely on).
	const FElysiumEntityHandle CounterHandle = CounterEntity->Handle;
	TestNotNull(TEXT("live handle resolves"), World.Resolve(CounterHandle));

	World.EnqueueInput(TEXT("!self"), FName(TEXT("Kill")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), CounterHandle);
	for (int32 i = 0; i < 4; ++i)
	{
		World.Tick(0.0);
	}
	TestNull(TEXT("killed handle resolves to null"), World.Resolve(CounterHandle));

	return true;
}

// =====================================================================================
// FElysiumDecals — the `.decals` projector sidecar parser + the orientation contract the bake
// places each ADecalActor by (7.2). Pure data + math, no RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalsTest, "Elysium.Substrate.Decals", GElysiumTestFlags)
bool FElysiumDecalsTest::RunTest(const FString&)
{
	// --- parse: 15 tokens -> one def with fields in order; malformed lines dropped ---
	TArray<FString> Lines;
	Lines.Add(TEXT("decals/blood1 10.0 20.0 30.0 1 0 0 0 1 0 0 0 1 12.5 7.5"));
	Lines.Add(TEXT("# too few tokens -> skipped"));
	Lines.Add(TEXT("decals/blood2 0 0 0 0 0 1 1 0 0 0 -1 0 4 4"));
	Lines.Add(FString());   // blank -> skipped

	TArray<FElysiumDecalDef> Defs;
	FElysiumDecals::ParseLines(Lines, Defs);
	TestEqual(TEXT("two valid decals parsed (two junk lines dropped)"), Defs.Num(), 2);

	if (Defs.Num() >= 1)
	{
		const FElysiumDecalDef& D = Defs[0];
		TestEqual(TEXT("material name"), D.Mat, FString(TEXT("decals/blood1")));
		TestTrue(TEXT("loc parsed"), D.Loc.Equals(FVector(10, 20, 30)));
		TestTrue(TEXT("normal parsed"), D.Normal.Equals(FVector(1, 0, 0)));
		TestTrue(TEXT("s_dir parsed"), D.SDir.Equals(FVector(0, 1, 0)));
		TestTrue(TEXT("t_dir parsed"), D.TDir.Equals(FVector(0, 0, 1)));
		TestEqual(TEXT("half-width"), D.HalfW, 12.5f);
		TestEqual(TEXT("half-height"), D.HalfH, 7.5f);
	}

	// --- orientation: the bake rotates each decal by MakeRotFromXZ(Normal, SDir). A deferred decal
	// maps texture U -> local Z and V -> local Y, so the surface horizontal (SDir, the U axis) goes
	// on local Z; local +X stays the room normal, so the component's -X (its projection axis) fires
	// into the wall. ---
	const FVector Normal(1, 0, 0), SDir(0, -1, 0);   // wall decal facing +X, U axis along -Y
	const FMatrix R = FRotationMatrix::MakeFromXZ(Normal, SDir);
	TestTrue(TEXT("local +X aligns with the room normal (projection is -X into the wall)"),
		R.GetUnitAxis(EAxis::X).Equals(Normal));
	TestTrue(TEXT("local +Z aligns with the surface horizontal / texture U (SDir)"),
		R.GetUnitAxis(EAxis::Z).Equals(SDir));
	// The three axes stay orthonormal (a valid rotation, not the exporter's reflected frame).
	TestTrue(TEXT("Y is orthonormal to X and Z"),
		FMath::IsNearlyZero(FVector::DotProduct(R.GetUnitAxis(EAxis::Y), Normal)) &&
		FMath::IsNearlyZero(FVector::DotProduct(R.GetUnitAxis(EAxis::Y), SDir)));

	return true;
}

// =====================================================================================
// FElysiumRopes — the `.ropes` cable sidecar parser + the rest-length contract BuildRopes builds
// each UCableComponent from (8.7). Pure data + math, no RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRopesTest, "Elysium.Substrate.Ropes", GElysiumTestFlags)
bool FElysiumRopesTest::RunTest(const FString&)
{
	// --- parse: 14 tokens -> one def with fields in order; malformed lines dropped ---
	TArray<FString> Lines;
	Lines.Add(TEXT("tex/rope_cable_cable.png 0 0 300 400 0 300 2.54 203.2 10 0.2 0 tex/rope_cable_cable_n.png 0"));
	Lines.Add(TEXT("# too few tokens -> skipped"));
	// "-" = no decoded texture; Type-2 + Dangling; $alphatest + $envmap (the cable/chain case)
	Lines.Add(TEXT("- 0 0 0 0 0 100 5 0 2 1 1 - 5"));
	Lines.Add(TEXT("- 0 0 0 0 0 100 5 0 99 1 0 - 0"));  // out-of-range node count -> clamped to 10
	Lines.Add(FString());   // blank -> skipped

	TArray<FElysiumRopeDef> Defs;
	FElysiumRopes::ParseLines(Lines, Defs);
	TestEqual(TEXT("three valid ropes parsed (two junk lines dropped)"), Defs.Num(), 3);

	if (Defs.Num() >= 1)
	{
		const FElysiumRopeDef& D = Defs[0];
		TestEqual(TEXT("texture path"), D.Tex, FString(TEXT("tex/rope_cable_cable.png")));
		TestTrue(TEXT("endpoint A parsed"), D.A.Equals(FVector(0, 0, 300)));
		TestTrue(TEXT("endpoint B parsed"), D.B.Equals(FVector(400, 0, 300)));
		TestEqual(TEXT("width cm"), D.WidthCm, 2.54f);
		TestEqual(TEXT("rest cm"), D.RestCm, 203.2f);
		TestEqual(TEXT("nodes"), D.Nodes, 10);
		TestEqual(TEXT("texscale"), D.TexScale, 0.2f);
		TestEqual(TEXT("flags"), static_cast<int32>(D.Flags), 0);
		TestEqual(TEXT("bump path"), D.Bump, FString(TEXT("tex/rope_cable_cable_n.png")));
		TestEqual(TEXT("matflags"), static_cast<int32>(D.MatFlags), 0);

		// --- rest-length contract: BuildRopes feeds RestCm straight into CableLength, and the
		// exporter has already resolved VtMB's own arithmetic into it. Rest *below* the straight
		// span is the normal case, not a bug: `RecomputeSprings` subtracts a flat 100 units, so a
		// 4 m span at 2.032 m rest is a taut cable the solver draws along the chord. ---
		TestTrue(TEXT("rest length below the span -> taut, no sag"),
			D.RestCm < static_cast<float>(FVector::Dist(D.A, D.B)));
	}

	if (Defs.Num() >= 2)
	{
		const FElysiumRopeDef& D = Defs[1];
		TestEqual(TEXT("dashed texture kept verbatim (runtime falls back to a plain MID)"),
			D.Tex, FString(TEXT("-")));
		// A Type-2 rope has two nodes, so BuildRopes gives it one span — a straight line that
		// cannot sag, which is the whole point of the type.
		TestEqual(TEXT("Type-2 rope keeps two nodes"), D.Nodes, 2);
		TestEqual(TEXT("Type-2 rope is one cable span"), FMath::Max(1, D.Nodes - 1), 1);
		TestTrue(TEXT("Dangling flag parsed"), (D.Flags & FElysiumRopeDef::Dangling) != 0);
		// $alphatest must survive to the runtime or BuildRopes instances the opaque master and
		// fills in the ~47% of the chain texture that is cut out between the links.
		TestTrue(TEXT("Masked matflag parsed"), (D.MatFlags & FElysiumRopeDef::Masked) != 0);
		TestTrue(TEXT("Envmap matflag parsed"), (D.MatFlags & FElysiumRopeDef::Envmap) != 0);
		TestFalse(TEXT("Translucent matflag not set"),
			(D.MatFlags & FElysiumRopeDef::Translucent) != 0);
		TestEqual(TEXT("dashed bump kept verbatim"), D.Bump, FString(TEXT("-")));
	}

	if (Defs.Num() >= 3)
	{
		// Activate() clamps m_nSegments to [2, 10]; the parser holds the same bound so a bad
		// sidecar cannot ask for an unbounded Verlet chain.
		TestEqual(TEXT("node count clamped to VtMB's ROPE_MAX_SEGMENTS"), Defs[2].Nodes, 10);
	}

	return true;
}

// =====================================================================================
// 7.4 world material set — ParseMtlLines reads every channel/flag the master-selection and
// param-binding depend on, and the blend flags stay mutually exclusive (the exporter writes at
// most one). No RHI, no assets: the factory's master choice is a pure function of these fields.
// =====================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldMaterialsTest, "Elysium.Substrate.WorldMaterials", GElysiumTestFlags)
bool FElysiumWorldMaterialsTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	// opaque with every optional feature channel present at once
	Lines.Add(TEXT("newmtl brick"));
	Lines.Add(TEXT("map_Kd tex/brick.png"));
	Lines.Add(TEXT("map_Ke tex/brick_ke.png"));
	Lines.Add(TEXT("bumpmap tex/brick_n.png"));
	Lines.Add(TEXT("envmap c0_0_0"));
	Lines.Add(TEXT("envmapmask tex/brick_envmask.png"));
	Lines.Add(TEXT("basetex2 tex/grass.png"));
	// masked (illum 4)
	Lines.Add(TEXT("newmtl fence"));
	Lines.Add(TEXT("map_Kd tex/fence.png"));
	Lines.Add(TEXT("illum 4"));
	// translucent (blend 1)
	Lines.Add(TEXT("newmtl glass"));
	Lines.Add(TEXT("map_Kd tex/glass.png"));
	Lines.Add(TEXT("blend 1"));
	// additive
	Lines.Add(TEXT("newmtl neon"));
	Lines.Add(TEXT("map_Kd tex/neon.png"));
	Lines.Add(TEXT("additive 1"));
	// reflective, no explicit mask (uniform reflectivity)
	Lines.Add(TEXT("newmtl marble"));
	Lines.Add(TEXT("map_Kd tex/marble.png"));
	Lines.Add(TEXT("envmap cubemapdefault"));
	// 7.5 — a grey $envmaptint: a reflection-strength dim-down, not a metal
	Lines.Add(TEXT("newmtl dimtile"));
	Lines.Add(TEXT("map_Kd tex/dimtile.png"));
	Lines.Add(TEXT("envmap cubemapdefault"));
	Lines.Add(TEXT("envtint 0.5000 0.5000 0.5000"));
	// 7.5 — a chromatic $envmaptint: VtMB naming a metal (brass)
	Lines.Add(TEXT("newmtl brassrail"));
	Lines.Add(TEXT("map_Kd tex/brassrail.png"));
	Lines.Add(TEXT("envmap env_cubemap"));
	Lines.Add(TEXT("envmapmask tex/brassrail_envmask.png"));
	Lines.Add(TEXT("envtint 0.6500 0.5000 0.0000"));
	// 7.5 — a chromatic tint on a TRANSLUCENT surface is coloured glass, which stays dielectric
	Lines.Add(TEXT("newmtl bluepane"));
	Lines.Add(TEXT("map_Kd tex/bluepane.png"));
	Lines.Add(TEXT("blend 1"));
	Lines.Add(TEXT("envmap env_cubemap"));
	Lines.Add(TEXT("envtint 0.5000 0.6000 0.9000"));

	TMap<FString, FElysiumMaterialDef> Mats;
	FElysiumObjModel::ParseMtlLines(Lines, Mats);
	TestEqual(TEXT("eight materials parsed"), Mats.Num(), 8);

	if (const FElysiumMaterialDef* B = Mats.Find(TEXT("brick")))
	{
		TestEqual(TEXT("brick albedo"), B->Albedo, FString(TEXT("tex/brick.png")));
		TestEqual(TEXT("brick emissive"), B->Emissive, FString(TEXT("tex/brick_ke.png")));
		TestEqual(TEXT("brick bump"), B->Bump, FString(TEXT("tex/brick_n.png")));
		TestEqual(TEXT("brick envmask"), B->EnvMask, FString(TEXT("tex/brick_envmask.png")));
		TestEqual(TEXT("brick basetex2"), B->BaseTex2, FString(TEXT("tex/grass.png")));
		TestTrue(TEXT("brick is reflective"), B->bEnvmap);
		TestTrue(TEXT("brick is opaque (no blend flag)"), !B->bBlend && !B->bScissor && !B->bAdditive);
	}
	if (const FElysiumMaterialDef* F = Mats.Find(TEXT("fence")))
	{
		TestTrue(TEXT("fence is masked"), F->bScissor && !F->bBlend && !F->bAdditive);
	}
	if (const FElysiumMaterialDef* G = Mats.Find(TEXT("glass")))
	{
		TestTrue(TEXT("glass is translucent"), G->bBlend && !G->bScissor && !G->bAdditive);
	}
	if (const FElysiumMaterialDef* N = Mats.Find(TEXT("neon")))
	{
		TestTrue(TEXT("neon is additive"), N->bAdditive && !N->bBlend && !N->bScissor);
	}
	if (const FElysiumMaterialDef* M = Mats.Find(TEXT("marble")))
	{
		TestTrue(TEXT("marble is reflective with no explicit mask"), M->bEnvmap && M->EnvMask.IsEmpty());
		// An unauthored $envmaptint is white, so the grey path is a multiply by 1.
		TestEqual(TEXT("marble tint defaults to white"), M->EnvTint, FLinearColor::White);
		TestEqual(TEXT("marble tint luma is unity"), M->TintLuma(), 1.f, 1e-4f);
		TestFalse(TEXT("marble is not a metal"), M->IsChromatic());
	}
	// 7.5 — $envmaptint splits two ways, and the split is what decides Metallic. The population
	// is bimodal (docs/reflections.md), so these are the two sides plus the glass exclusion.
	if (const FElysiumMaterialDef* D = Mats.Find(TEXT("dimtile")))
	{
		TestFalse(TEXT("a grey tint is not a metal"), D->IsChromatic());
		TestEqual(TEXT("grey tint luma dims the reflection"), D->TintLuma(), 0.5f, 1e-3f);
	}
	if (const FElysiumMaterialDef* Br = Mats.Find(TEXT("brassrail")))
	{
		TestTrue(TEXT("a chromatic tint on an opaque surface is a metal"), Br->IsChromatic());
		TestEqual(TEXT("brass tint parsed"), Br->EnvTint, FLinearColor(0.65f, 0.5f, 0.f));
	}
	if (const FElysiumMaterialDef* Bp = Mats.Find(TEXT("bluepane")))
	{
		// Chromatic, but translucent: coloured glass, not brass. Metalness comes off VtMB's own
		// authoring, and a $translucent surface is never it.
		TestTrue(TEXT("bluepane tint is chromatic"),
			(Bp->EnvTint.B - Bp->EnvTint.R) >= FElysiumMaterialDef::ChromaticSpread);
		TestFalse(TEXT("tinted glass stays dielectric"), Bp->IsChromatic());
	}
	return true;
}

// =====================================================================================
// FElysiumNpc / npc_maker (B3) — registry coverage, npc_maker.Spawn creating a live child
// on a bare world (Owner null, so the visual build no-ops), and the WillTalk latch +
// OnDialogBegin fire. No RHI, no glTF: this is the AI-free substrate half of the beat.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcTest, "Elysium.Substrate.Npc", GElysiumTestFlags)
bool FElysiumNpcTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// The character leaf is registered for the beat's class and resolves its dialog-gating inputs.
	const FElysiumClassDesc* Vamp = Reg.Find(FName(TEXT("npc_VVampire")));
	if (!TestNotNull(TEXT("npc_VVampire registered"), Vamp))
	{
		return false;
	}
	for (const TCHAR* In : { TEXT("WillTalk"), TEXT("UseInteresting"), TEXT("StartPlayerDialogRemote"),
		TEXT("EndDialog"), TEXT("Kill") })
	{
		TestNotNull(FString::Printf(TEXT("npc_VVampire.%s resolves"), In),
			reinterpret_cast<const void*>(Reg.FindInput(*Vamp, FName(In))));
	}
	// 9.3 field-table audit: `npc.times_talked` is a script-read field (santamonica et al.), so it must
	// resolve as a datamap field — otherwise the read raises AttributeError instead of returning 0.
	TestNotNull(TEXT("npc_VVampire.times_talked resolves as a field"),
		reinterpret_cast<const void*>(Reg.FindField(*Vamp, FName(TEXT("times_talked")))));

	// The maker leaf resolves Spawn/Enable.
	const FElysiumClassDesc* MakerDesc = Reg.Find(FName(TEXT("npc_maker")));
	if (!TestNotNull(TEXT("npc_maker registered"), MakerDesc))
	{
		return false;
	}
	TestNotNull(TEXT("npc_maker.Spawn resolves"),
		reinterpret_cast<const void*>(Reg.FindInput(*MakerDesc, FName(TEXT("Spawn")))));
	TestNotNull(TEXT("npc_maker.Enable resolves"),
		reinterpret_cast<const void*>(Reg.FindInput(*MakerDesc, FName(TEXT("Enable")))));

	// --- A bare world: Jack, a counter wired off his OnDialogBegin, and the blueblood maker ---
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npc_test__");

	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	{
		FElysiumOutputDef Wire;   // OnDialogBegin -> counter.Add(1), to observe the fire
		Wire.Name = TEXT("OnDialogBegin");
		Wire.Target = TEXT("dlgcount");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("1");
		Jack.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Jack));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("dlgcount");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityDef BluebloodMaker;
	BluebloodMaker.Classname = TEXT("npc_maker");
	BluebloodMaker.TargetName = TEXT("blueblood_maker");
	BluebloodMaker.Keys.Add(TEXT("NPCType"), TEXT("npc_VPedestrian"));
	BluebloodMaker.Keys.Add(TEXT("NPCTargetname"), TEXT("blueblood"));
	BluebloodMaker.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	Defs.Defs.Add(MoveTemp(BluebloodMaker));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));

	FElysiumEntity* JackEnt = World.FindByName(TEXT("Jack"));
	FElysiumEntity* MakerEnt = World.FindByName(TEXT("blueblood_maker"));
	if (!TestNotNull(TEXT("Jack resolved"), JackEnt) || !TestNotNull(TEXT("maker resolved"), MakerEnt))
	{
		return false;
	}
	TestFalse(TEXT("Jack is a real class, not an inert record"), JackEnt->IsRecordOnly());
	const FElysiumEntityHandle JackHandle = JackEnt->Handle;

	// npc_maker.Spawn produces the child NPC (the acceptance case).
	TestNull(TEXT("blueblood absent before Spawn"), World.FindByName(TEXT("blueblood")));
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), MakerEnt->Handle);
	for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

	FElysiumEntity* Blueblood = World.FindByName(TEXT("blueblood"));
	if (TestNotNull(TEXT("blueblood spawned via npc_maker.Spawn"), Blueblood))
	{
		TestEqual(TEXT("blueblood is npc_VPedestrian"), Blueblood->Def->Classname, FString(TEXT("npc_VPedestrian")));
		TestFalse(TEXT("blueblood is a real NPC, not an inert record"), Blueblood->IsRecordOnly());
		TestNotNull(TEXT("blueblood handle resolves"), World.Resolve(Blueblood->Handle));
	}

	// WillTalk latch + StartPlayerDialogRemote fires OnDialogBegin.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("WillTalk")), FElysiumVariant::Int(1), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("StartPlayerDialogRemote")), FElysiumVariant::Int(256), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

	// Read a keyed debug row (the concrete leaf is file-local, so its state surfaces via GetDebugState).
	auto DebugRow = [](const FElysiumEntity* E, const TCHAR* Key) -> FString
	{
		TArray<TPair<FString, FString>> Rows;
		E->GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == Key) { return Row.Value; }
		}
		return FString();
	};

	TestEqual(TEXT("WillTalk latched"), DebugRow(World.Resolve(JackHandle), TEXT("WillTalk")), FString(TEXT("yes")));
	TestEqual(TEXT("OnDialogBegin fired once (counter=1)"),
		FCString::Atof(*DebugRow(World.FindByName(TEXT("dlgcount")), TEXT("Value"))), 1.0f);

	return true;
}

// =====================================================================================
// 9.3 scripted entity manipulation — the two-phase runtime create (CreateEntityNoSpawn ->
// CallEntitySpawn), Entity.SetName re-keying the name index, and the runtime origin backing
// SetOrigin. A bare world (Owner null) exercises the substrate half; bodies no-op.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRuntimeSpawnTest, "Elysium.Substrate.RuntimeSpawn", GElysiumTestFlags)
bool FElysiumRuntimeSpawnTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__spawn_test__");
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));   // empty map — everything here is runtime-created

	// --- Phase 1: CreateEntityNoSpawn appends a live, findable, NOT-yet-spawned entity ---
	FElysiumEntityDef D;
	D.Classname = TEXT("math_counter");
	D.TargetName = TEXT("c1");
	D.Origin = FVector(1, 2, 3);
	const FElysiumEntityHandle H = World.CreateRuntimeEntityNoSpawn(MoveTemp(D));
	FElysiumEntity* E = World.Resolve(H);
	if (!TestNotNull(TEXT("create returned a live entity"), E))
	{
		return false;
	}
	TestFalse(TEXT("not spawned yet"), E->bSpawnCalled);
	TestEqual(TEXT("findable by its targetname immediately"), World.FindByName(TEXT("c1")), E);
	TestTrue(TEXT("runtime origin seeded from the def"), E->Origin.Equals(FVector(1, 2, 3)));

	// --- SetName re-keys the name index: old name drops, new name resolves ---
	World.RenameEntity(*E, TEXT("c2"));
	TestNull(TEXT("old name no longer resolves"), World.FindByName(TEXT("c1")));
	TestEqual(TEXT("new name resolves to the same entity"), World.FindByName(TEXT("c2")), E);

	// --- SetOrigin mutates the live origin (what GetOrigin reads back) ---
	E->SetRuntimeOrigin(FVector(9, 9, 9));
	TestTrue(TEXT("SetOrigin moved the live origin"), E->Origin.Equals(FVector(9, 9, 9)));

	// --- Phase 2: CallEntitySpawn runs Spawn() once; a second call is an idempotent no-op ---
	World.CallEntitySpawn(*E);
	TestTrue(TEXT("spawned after CallEntitySpawn"), E->bSpawnCalled);
	World.CallEntitySpawn(*E);   // must not crash or re-spawn
	TestTrue(TEXT("still spawned (idempotent)"), E->bSpawnCalled);

	// --- The fused SpawnRuntimeEntity (npc_maker's path) spawns immediately ---
	FElysiumEntityDef D2;
	D2.Classname = TEXT("math_counter");
	D2.TargetName = TEXT("c3");
	const FElysiumEntityHandle H2 = World.SpawnRuntimeEntity(MoveTemp(D2));
	FElysiumEntity* E2 = World.Resolve(H2);
	if (TestNotNull(TEXT("fused spawn returned a live entity"), E2))
	{
		TestTrue(TEXT("fused path is spawned on return"), E2->bSpawnCalled);
	}

	return true;
}

// =====================================================================================
// 9.1 / B4 — `.dlg` parser, the dlgexpr normalizer, and the branch state machine. All
// content-free: a synthetic in-memory `.dlg` and injected condition/action callbacks, so
// the branch logic is tested independently of both the normalizer and the script host.
// =====================================================================================

namespace
{
	// Build one 13-field `.dlg` row in the on-disk shape (`{ TAB content TAB }` concatenated) from the
	// fields the tests care about; cols 6-11 are empty. Handy so the fixtures read like the data.
	FString ElysiumDlgRow(int32 Id, const FString& Text, const FString& Link,
		const FString& Cond, const FString& Action, const FString& Malk = FString())
	{
		auto F = [](const FString& S) { return FString::Printf(TEXT("{\t%s\t}"), *S); };
		FString R;
		R += F(FString::FromInt(Id)); // 0
		R += F(Text);                 // 1 male
		R += F(Text);                 // 2 female (same)
		R += F(Link);                 // 3
		R += F(Cond);                 // 4
		R += F(Action);               // 5
		for (int32 i = 6; i <= 11; ++i) { R += F(FString()); }
		R += F(Malk);                 // 12 — Malkavian-PC variant
		return R;
	}

	TArray<uint8> ElysiumDlgBytes(const TArray<FString>& Rows)
	{
		FString Joined = FString::Join(Rows, TEXT("\r\n")) + TEXT("\r\n");
		TArray<uint8> Bytes;
		Bytes.Reserve(Joined.Len());
		for (const TCHAR C : Joined) { Bytes.Add(static_cast<uint8>(C)); }   // Latin-1 round-trip
		return Bytes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgParseTest, "Elysium.Substrate.DlgParse", GElysiumTestFlags)
bool FElysiumDlgParseTest::RunTest(const FString&)
{
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("G.Story_State = -3"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Who are you?"), TEXT("21"), TEXT("not IsClan(pc,\"Malkavian\")"), TEXT(""), TEXT("The rain of ages?")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Padding"), TEXT(""), TEXT(""), TEXT("")));   // empty link = padding

	FElysiumDlgFile File;
	TestTrue(TEXT("parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File));
	TestEqual(TEXT("row count"), File.Lines.Num(), 3);

	const FElysiumDlgLine* Npc = File.FindById(11);
	if (TestNotNull(TEXT("finds id 11"), Npc))
	{
		TestTrue(TEXT("11 is an NPC line"), Npc->IsNpcLine());
		TestEqual(TEXT("11 text"), Npc->Text(true), FString(TEXT("Greeting.")));
		TestEqual(TEXT("11 col-4 kept raw"), Npc->Condition, FString(TEXT("G.Story_State = -3")));
	}
	const FElysiumDlgLine* Pc = File.FindById(12);
	if (TestNotNull(TEXT("finds id 12"), Pc))
	{
		TestTrue(TEXT("12 is a PC choice"), Pc->IsPcChoice());
		TestEqual(TEXT("12 link target"), Pc->LinkTarget(), 21);
		// col-12 is the Malkavian variant: a non-Malkavian sees col-1, a Malkavian sees col-12.
		TestEqual(TEXT("12 normal text (non-malk)"), Pc->RawFor(true, false), FString(TEXT("Who are you?")));
		TestEqual(TEXT("12 malkavian variant"), Pc->RawFor(true, true), FString(TEXT("The rain of ages?")));
	}
	TestTrue(TEXT("13 is padding"), File.FindById(13)->Role == EElysiumDlgRole::Padding);

	// 14-field tolerance (the kiki.dlg typo): a valid 13-field prefix parses, the extra is ignored.
	FString FourteenField = ElysiumDlgRow(1, TEXT("hi"), TEXT("#"), TEXT(""), TEXT("")) + TEXT("{\textra\t}");
	FElysiumDlgFile Wide;
	TestTrue(TEXT("14-field row parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({ FourteenField }), Wide));
	TestEqual(TEXT("14-field row yields one line"), Wide.Lines.Num(), 1);
	TestEqual(TEXT("14-field text intact"), Wide.Lines[0].Text(true), FString(TEXT("hi")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgExprTest, "Elysium.Substrate.DlgExpr", GElysiumTestFlags)
bool FElysiumDlgExprTest::RunTest(const FString&)
{
	using namespace ElysiumDlgExpr;

	// A skill-only condition: implicit `>=`, wrapped in CalcFeat.
	TestEqual(TEXT("bare skillcheck"), ConditionToPython(TEXT("Seduction 7")),
		FString(TEXT("CalcFeat(\"Seduction\") >= 7")));
	// Explicit relop preserved.
	TestEqual(TEXT("skillcheck relop"), ConditionToPython(TEXT("Humanity >= 5")),
		FString(TEXT("CalcFeat(\"Humanity\") >= 5")));
	// Skillcheck joined to a python expr by `&` -> `and`.
	TestEqual(TEXT("skillcheck & expr"), ConditionToPython(TEXT("Seduction 7 & G.Johnny_Dead == 0")),
		FString(TEXT("CalcFeat(\"Seduction\") >= 7 and G.Johnny_Dead == 0")));
	// `|` -> `or`.
	TestEqual(TEXT("pipe -> or"), ConditionToPython(TEXT("Persuasion 7 | G.x == 1")),
		FString(TEXT("CalcFeat(\"Persuasion\") >= 7 or G.x == 1")));
	// A pure python condition (no skillcheck) round-trips (normalised spacing).
	TestEqual(TEXT("pure python"), ConditionToPython(TEXT("G.Patch_Plus == 0")),
		FString(TEXT("G.Patch_Plus == 0")));
	// A member/call ident that happens to precede a number is NOT a skillcheck.
	TestEqual(TEXT("member not skillcheck"), ConditionToPython(TEXT("pc.humanity >= 5")),
		FString(TEXT("pc.humanity >= 5")));
	TestEqual(TEXT("call not skillcheck"), ConditionToPython(TEXT("OneOfSet(1,4)")),
		FString(TEXT("OneOfSet(1,4)")));
	// Empty -> empty.
	TestEqual(TEXT("empty condition"), ConditionToPython(TEXT("")), FString());

	// Actions: `&` between statements -> `;`; a lone assignment round-trips.
	TestEqual(TEXT("action assign"), ActionToPython(TEXT("G.Tut_Jack = 1")),
		FString(TEXT("G.Tut_Jack = 1")));
	TestEqual(TEXT("action &-join -> ;"), ActionToPython(TEXT("G.a = 1 & G.b = 2")),
		FString(TEXT("G.a = 1 ; G.b = 2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgDisplayTest, "Elysium.Substrate.DlgDisplay", GElysiumTestFlags)
bool FElysiumDlgDisplayTest::RunTest(const FString&)
{
	using ElysiumDlgText::StripStageDirections;

	// The real jack_tutorial line 11 opener: a leading `[...]` and an interior one, no space after either.
	TestEqual(TEXT("jack opener stripped"),
		StripStageDirections(TEXT("[laughing at something no one else thinks is funny]What a scene, man! [chuckle]How 'bout that?")),
		FString(TEXT("What a scene, man! How 'bout that?")));
	// A direction between words leaves a single space, not a doubled one.
	TestEqual(TEXT("interior collapse"), StripStageDirections(TEXT("a [nods] b")), FString(TEXT("a b")));
	// No brackets -> verbatim (fast path).
	TestEqual(TEXT("no directions"), StripStageDirections(TEXT("Who are you?")), FString(TEXT("Who are you?")));
	// An unterminated `[` is kept (not a stage direction).
	TestEqual(TEXT("unterminated bracket kept"), StripStageDirections(TEXT("cost is [50")), FString(TEXT("cost is [50")));
	// A direction-only string strips to empty.
	TestEqual(TEXT("direction-only -> empty"), StripStageDirections(TEXT("[sighs]")), FString());

	// The line accessor path: raw Text() keeps the direction, DisplayText() drops it. col-12 (Malkavian)
	// replaces the text for a Malkavian player, and is stage-direction-stripped the same way.
	FElysiumDlgLine Line;
	Line.TextMale = TEXT("[chuckle]Hey there.");
	Line.TextMalkavian = TEXT("[cackles]The walls whisper hello.");
	Line.Role = EElysiumDlgRole::NpcLine;
	TestEqual(TEXT("raw text verbatim"), Line.Text(true), FString(TEXT("[chuckle]Hey there.")));
	TestEqual(TEXT("display non-malk stripped"), Line.DisplayText(true, false), FString(TEXT("Hey there.")));
	TestEqual(TEXT("display malk variant stripped"), Line.DisplayText(true, true), FString(TEXT("The walls whisper hello.")));
	// A line with no col-12 falls back to the gendered text even for a Malkavian.
	FElysiumDlgLine Plain;
	Plain.TextMale = TEXT("Plain.");
	TestEqual(TEXT("no malk variant -> col-1"), Plain.DisplayText(true, true), FString(TEXT("Plain.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgBranchTest, "Elysium.Substrate.DlgBranch", GElysiumTestFlags)
bool FElysiumDlgBranchTest::RunTest(const FString&)
{
	// A miniature of the jack_tutorial shape: two blank leading NPC lines, then the real entry (11),
	// a gated + an ungated choice, a follow NPC line (21) with a choice that sets a flag and ends.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(1, TEXT(""), TEXT("#"), TEXT(""), TEXT("")));            // blank opener - skipped
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("SPEAK_11"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Gated"), TEXT("21"), TEXT("SHOW"), TEXT("PICK_12")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Hidden"), TEXT("31"), TEXT("HIDE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(14, TEXT("Always"), TEXT("21"), TEXT(""), TEXT("")));    // ungated
	Rows.Add(ElysiumDlgRow(21, TEXT("More."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(22, TEXT("Set flag & bye"), TEXT("0"), TEXT(""), TEXT("SET_FLAG")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}

	// Record actions; a condition passes unless it is the string "HIDE".
	TArray<FString> Ran;
	auto Cond = [](const FString& C) { return C != TEXT("HIDE"); };
	auto Act = [&Ran](const FString& A) { Ran.Add(A); };

	FElysiumDlgConversation Conv(File, /*bMale*/ true, /*bMalk*/ false, Cond, Act);
	Conv.Start();

	// Entry skips the blank line 1 and opens at 11, running its col-4 action.
	if (TestNotNull(TEXT("opened on an NPC line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("entry is line 11"), Conv.CurrentNpcLine()->Id, 11);
	}
	TestTrue(TEXT("ran SPEAK_11"), Ran.Contains(TEXT("SPEAK_11")));

	// Two of the three following rows are visible (gated SHOW passes, HIDE fails, ungated shows).
	TestEqual(TEXT("visible choice count"), Conv.VisibleChoices().Num(), 2);
	TestEqual(TEXT("first visible is 12"), Conv.VisibleChoice(0)->Id, 12);
	TestEqual(TEXT("second visible is 14"), Conv.VisibleChoice(1)->Id, 14);

	// Pick the first choice -> its action runs, jump to NPC 21.
	Conv.Choose(0);
	TestTrue(TEXT("ran PICK_12"), Ran.Contains(TEXT("PICK_12")));
	if (TestNotNull(TEXT("advanced to a line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("now on line 21"), Conv.CurrentNpcLine()->Id, 21);
	}

	// Line 21 offers one ending choice; picking it runs the flag action then ends the conversation.
	TestEqual(TEXT("21 has one choice"), Conv.VisibleChoices().Num(), 1);
	Conv.Choose(0);
	TestTrue(TEXT("ran SET_FLAG"), Ran.Contains(TEXT("SET_FLAG")));
	TestTrue(TEXT("conversation is over"), Conv.IsOver());
	TestNull(TEXT("no current line after end"), Conv.CurrentNpcLine());

	return true;
}

// =====================================================================================
// 9.3 CPython end-to-end — the writers and the two-phase spawn driven through the REAL
// Python glue (arg parsing, __getattr__ dispatch, the module globals), not just the C++
// substrate. Self-skips when the embedded VM is unavailable, so it never yields a false
// failure on a non-CPython build/host.
// =====================================================================================

#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCPythonWritersTest, "Elysium.Substrate.CPythonWriters", GElysiumTestFlags)
bool FElysiumCPythonWritersTest::RunTest(const FString&)
{
	FElysiumPythonVM& VM = FElysiumPythonVM::Get();
	FString Err;
	if (!VM.EnsureStarted(Err))
	{
		AddInfo(FString::Printf(TEXT("embedded CPython unavailable (%s) — skipping"), *Err));
		return true;   // self-skip: never a false failure where the SDK is absent
	}

	// A bare world (Owner null, so no bodies/visuals) with one named entity to drive through Python.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__py_test__");
	FElysiumEntityDef Relay;
	Relay.Classname = TEXT("logic_relay");
	Relay.TargetName = TEXT("wr_target");
	Relay.Origin = FVector(1, 2, 3);
	Defs.Defs.Add(MoveTemp(Relay));
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));

	// Ctx.World is what CurrentWorld() resolves against for the duration of each eval.
	FElysiumScriptContext Ctx;
	Ctx.World = &World;
	auto Eval = [&](const TCHAR* Src)
	{
		FString E;
		const FElysiumVariant V = VM.Eval(FString(Src), Ctx, E);
		if (!E.IsEmpty()) { AddError(FString::Printf(TEXT("eval '%s' raised: %s"), Src, *E)); }
		return V;
	};

	// SetName re-keys through the glue: old targetname stops resolving, the new one resolves.
	Eval(TEXT("FindEntityByName(\"wr_target\").SetName(\"wr_renamed\")"));
	TestNull(TEXT("old name gone after SetName"), World.FindByName(TEXT("wr_target")));
	FElysiumEntity* Renamed = World.FindByName(TEXT("wr_renamed"));
	if (!TestNotNull(TEXT("new name resolves after SetName"), Renamed))
	{
		return false;
	}

	// SetOrigin (tuple-arg form) mutates the runtime origin.
	Eval(TEXT("FindEntityByName(\"wr_renamed\").SetOrigin((7,8,9))"));
	TestTrue(TEXT("SetOrigin moved the runtime origin"), Renamed->Origin.Equals(FVector(7, 8, 9)));

	// SetModel + GetModelName round-trip through the glue (clean string, no repr).
	Eval(TEXT("FindEntityByName(\"wr_renamed\").SetModel(\"models/test/foo.mdl\")"));
	const FElysiumVariant Model = Eval(TEXT("FindEntityByName(\"wr_renamed\").GetModelName()"));
	TestEqual(TEXT("GetModelName reflects SetModel"), Model.ToString(), FString(TEXT("models/test/foo.mdl")));

	// Two-phase spawn end to end: CreateEntityNoSpawn -> SetName -> CallEntitySpawn.
	Eval(TEXT("__pytest_e = CreateEntityNoSpawn(\"math_counter\", (4,5,6), (0,0,0))"));
	Eval(TEXT("__pytest_e.SetName(\"pytest_spawned\")"));
	FElysiumEntity* Spawned = World.FindByName(TEXT("pytest_spawned"));
	if (TestNotNull(TEXT("CreateEntityNoSpawn made a findable entity"), Spawned))
	{
		TestFalse(TEXT("not spawned before CallEntitySpawn"), Spawned->bSpawnCalled);
		TestTrue(TEXT("origin came from the create args"), Spawned->Origin.Equals(FVector(4, 5, 6)));
		Eval(TEXT("CallEntitySpawn(__pytest_e)"));
		TestTrue(TEXT("spawned after CallEntitySpawn"), Spawned->bSpawnCalled);
	}

	// --- 9.7d: the shadowed-name split, through the real getattro -------------------------
	// `Whisper` and `FrenzyTrigger` are `vamputil.py` helpers AND datamap input names, so the
	// receiver — not the name — decides which one runs (script_api.md). Stand up the helper the
	// way vamputil defines it (a plain `__main__` function) and check the two spellings stay
	// distinct: the bare call reaches the script, the qualified one the datamap input.
	World.SpawnPlayer();
	Eval(TEXT("__pytest_whispers = []"));
	Eval(TEXT("def Whisper(s):\n    __pytest_whispers.append(s)\n"));
	Eval(TEXT("def FrenzyTrigger(char):\n    char.FrenzyTrigger(1)\n"));

	TestTrue(TEXT("pc resolves to the player entity"), Eval(TEXT("pc is not None")).ToBool());
	TestTrue(TEXT("pc.Whisper is not the __main__ helper"),
		Eval(TEXT("pc.Whisper is not Whisper")).ToBool());
	TestTrue(TEXT("pc.FrenzyTrigger is not the __main__ helper"),
		Eval(TEXT("pc.FrenzyTrigger is not FrenzyTrigger")).ToBool());

	Eval(TEXT("Whisper(\"Crying\")"));      // bare -> the script helper (24 `.dlg` sites)
	Eval(TEXT("pc.Whisper(\"Crying\")"));   // receiver-qualified -> the player datamap input (9 sites)
	World.Tick(0.0);
	TestEqual(TEXT("only the bare spelling ran the helper"),
		Eval(TEXT("len(__pytest_whispers)")).ToInt(), 1);
	// `__main__` is process-global, so take the two real script names back out again.
	Eval(TEXT("del Whisper, FrenzyTrigger, __pytest_whispers"));

	// OneOfSet through the real module global: the same exactly-one-of-N set property, with the
	// roll pinned so the assertion is deterministic.
	ElysiumScriptNatives::SetOneOfSetRoll(2);
	TestTrue(TEXT("the roll's row passes under CPython"), Eval(TEXT("OneOfSet(3,7)")).ToBool());
	TestFalse(TEXT("a sibling row does not"), Eval(TEXT("OneOfSet(4,7)")).ToBool());
	TestEqual(TEXT("a 7-row set shows exactly one row"),
		Eval(TEXT("len([w for w in range(1,8) if OneOfSet(w,7)])")).ToInt(), 1);
	ElysiumScriptNatives::SetOneOfSetRoll(-1);

	return true;
}
#endif // ELYSIUM_WITH_CPYTHON

// =====================================================================================
// The sky cube face->slice transform (sky-ambience B3). Checks BuildSkyCube's table against
// the two conventions it was derived from, not against itself: for every Unreal cube slice
// and a grid of its texels, the direction UE's own GetCubemapVector assigns that texel must
// be the direction VtMB's draw tables assign the source pixel the transform reads from.
// A wrong face binding, a wrong rotation or a mirror each break it.
// =====================================================================================

namespace
{
	// K1, carried into Unreal space by source_to_unreal (x, -y, z): the direction a Source sky
	// face's image pixel (u, v) looks along, v = 0 the top row.
	// docs/sky-ambience.md -> "K1 ... (settled)" / B3.
	FVector ElysiumK1FaceDir(const FString& Face, double U, double V)
	{
		const double S = 2.0 * U - 1.0;
		const double T = 1.0 - 2.0 * V;
		if (Face == TEXT("rt")) { return FVector( 1,  S,  T); }
		if (Face == TEXT("lf")) { return FVector(-1, -S,  T); }
		if (Face == TEXT("bk")) { return FVector( S, -1,  T); }
		if (Face == TEXT("ft")) { return FVector(-S,  1,  T); }
		if (Face == TEXT("up")) { return FVector(-T,  S,  1); }
		return FVector(T, S, -1);   // dn
	}

	// K2, GetCubemapVector (ReflectionEnvironmentShaders.usf): the raw Unreal world direction of
	// slice texel (U, V), U growing right and V growing down over the slice's own texels.
	FVector ElysiumK2SliceDir(int32 Slice, double U, double V)
	{
		const double SX = 2.0 * U - 1.0;
		const double SY = 2.0 * V - 1.0;
		switch (Slice)
		{
		case 0:  return FVector( 1, -SY, -SX);
		case 1:  return FVector(-1, -SY,  SX);
		case 2:  return FVector(SX,   1,  SY);
		case 3:  return FVector(SX,  -1, -SY);
		case 4:  return FVector(SX, -SY,   1);
		default: return FVector(-SX, -SY, -1);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyCubeTest, "Elysium.Substrate.SkyCube", GElysiumTestFlags)
bool FElysiumSkyCubeTest::RunTest(const FString&)
{
	// Every slice takes a distinct face, and together they are the whole set.
	TSet<FString> Faces;
	for (int32 Slice = 0; Slice < 6; ++Slice)
	{
		Faces.Add(ElysiumEnvironment::SkySliceFace(Slice));
	}
	TestEqual(TEXT("the six slices take six distinct faces"), Faces.Num(), 6);
	for (const TCHAR* Face : { TEXT("rt"), TEXT("lf"), TEXT("ft"), TEXT("bk"), TEXT("up"), TEXT("dn") })
	{
		TestTrue(FString::Printf(TEXT("face %s is bound to a slice"), Face), Faces.Contains(Face));
	}

	// N is odd and > 1 so the sample grid includes the centre and both parities of edge texel;
	// a rotation that happened to be its own inverse on an even grid still has to survive.
	const int32 N = 7;
	for (int32 Slice = 0; Slice < 6; ++Slice)
	{
		const FString Face = ElysiumEnvironment::SkySliceFace(Slice);
		double Worst = 0.0;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				int32 SX = 0, SY = 0;
				ElysiumEnvironment::SkySliceSource(Slice, X, Y, N, SX, SY);
				TestTrue(TEXT("the source texel is inside the face"),
					SX >= 0 && SX < N && SY >= 0 && SY < N);

				// Texel centres on both sides: the cube samples a slice texel, the transform
				// hands it the pixel of the decoded face that must carry that direction.
				const FVector Want = ElysiumK2SliceDir(Slice, (X + 0.5) / N, (Y + 0.5) / N).GetSafeNormal();
				const FVector Got = ElysiumK1FaceDir(Face, (SX + 0.5) / N, (SY + 0.5) / N).GetSafeNormal();
				Worst = FMath::Max(Worst, 1.0 - FVector::DotProduct(Want, Got));
			}
		}
		TestTrue(FString::Printf(
			TEXT("slice %d (%s): every texel looks where VtMB's tables say (worst 1-dot %g)"),
			Slice, *Face, Worst), Worst < 1e-12);
	}

	return true;
}

// =====================================================================================
// The per-primitive fog packing (sky-ambience B8b). The whole term rests on one property:
// an unwritten custom-primitive-data slot reads as zero, and zero must mean "not fogged" —
// so the material stays neutral on anything nobody stamped, with no branch to get wrong.
// This checks the packing keeps that property, and reproduces Source's own factor.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFogPackTest, "Elysium.Substrate.FogPack", GElysiumTestFlags)
bool FElysiumFogPackTest::RunTest(const FString&)
{
	// The slots the material graph reads (tools/mat_fog.py) and the bake writes
	// (tools/bake_map.py FOG_CPD_*). A colour is a float4, so it owns 0..3.
	TestEqual(TEXT("colour is the first slot"), ElysiumFog::SlotColor, 0);
	TestEqual(TEXT("start follows the colour's float4"), ElysiumFog::SlotStart, 4);
	TestEqual(TEXT("inverse range follows start"), ElysiumFog::SlotInvRange, 5);
	TestEqual(TEXT("six floats in all"), ElysiumFog::NumFloats, 6);

	const FLinearColor Color(0.25f, 0.5f, 0.75f, 1.f);
	TArray<float> Data;

	// A map that authored fog: the factor is Source's own saturate((d - start) / (end - start)).
	ElysiumFog::Pack(true, Color, 1270.f, 12700.f, Data);
	TestEqual(TEXT("packed float count"), Data.Num(), ElysiumFog::NumFloats);
	// The authored colour is gamma-encoded, like every VtMB colour, and lands linear.
	for (int32 C = 0; C < 3; ++C)
	{
		TestTrue(FString::Printf(TEXT("colour channel %d decodes to linear"), C),
			FMath::IsNearlyEqual(Data[ElysiumFog::SlotColor + C],
				FMath::Pow(Color.Component(C), 2.2f), 1e-6f));
	}
	TestEqual(TEXT("start passes through"), Data[ElysiumFog::SlotStart], 1270.f);
	TestTrue(TEXT("the inverse range spans start->end"),
		FMath::IsNearlyEqual(Data[ElysiumFog::SlotInvRange], 1.f / (12700.f - 1270.f), 1e-9f));
	// At `end` the surface is gone and only the fog's own colour is left; at `start`, nothing yet.
	TestTrue(TEXT("f is 1 at the authored end"), FMath::IsNearlyEqual(
		(12700.f - Data[ElysiumFog::SlotStart]) * Data[ElysiumFog::SlotInvRange], 1.f, 1e-5f));
	TestEqual(TEXT("f is 0 at the authored start"),
		(1270.f - Data[ElysiumFog::SlotStart]) * Data[ElysiumFog::SlotInvRange], 0.f);

	// The three ways a map says "no fog" must all land on the zero an unwritten slot reads as:
	// the flag is off (23 of the 43 sky_camera maps), or the range is degenerate.
	for (const TTuple<bool, float, float>& Off : {
			MakeTuple(false, 1270.f, 12700.f),   // authored, but fogenable 0
			MakeTuple(true, 12700.f, 12700.f),   // start == end
			MakeTuple(true, 12700.f, 1270.f) })  // end before start
	{
		ElysiumFog::Pack(Off.Get<0>(), Color, Off.Get<1>(), Off.Get<2>(), Data);
		TestEqual(TEXT("an unfogged set packs the same zero an unwritten slot reads"),
			Data[ElysiumFog::SlotInvRange], 0.f);
	}

	return true;
}

// =====================================================================================
// scripted_sequence (8.5) — the cutscene beat's state machine, driven through the real queue.
//
// No skeletal body here, so no action animation: this covers the half every map depends on —
// placement on the mark, OnBeginSequence/OnEndSequence, and the m_iszNextScript chain. A beat
// with no `m_iszPlay` is zero-length (59 of the 108 exported sequences are), so the whole chain
// settles within a few ticks of the null clock.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceTest,
	"Elysium.Substrate.ScriptedSequence", GElysiumTestFlags)
bool FElysiumScriptedSequenceTest::RunTest(const FString&)
{
	// Builds one scripted_sequence def. The target is a logic_relay purely because it is a point
	// entity the world will place — the sequence drives FElysiumEntity::SetRuntimeOrigin, which is
	// on the base, not on the NPC leaf.
	auto MakeSeq = [](const TCHAR* Name, const TCHAR* MoveTo, const FVector& At, const TCHAR* SpawnFlags)
	{
		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = Name;
		Seq.Origin = At;
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("mover1"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), MoveTo);
		Seq.Keys.Add(TEXT("angles"), TEXT("0 90 0"));
		Seq.Keys.Add(TEXT("spawnflags"), SpawnFlags);
		return Seq;
	};
	auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Input, const TCHAR* Param)
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = TEXT("counter1");
		W.Input = Input;
		W.Param = Param;
		On.Outputs.Add(W);
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	const FVector Mark(100.f, 200.f, 300.f);
	FElysiumEntityDef Seq1 = MakeSeq(TEXT("seq1"), TEXT("1"), Mark, TEXT("0"));
	Seq1.Keys.Add(TEXT("m_iszNextScript"), TEXT("seq2"));
	Wire(Seq1, TEXT("OnBeginSequence"), TEXT("Add"), TEXT("1"));
	Wire(Seq1, TEXT("OnEndSequence"), TEXT("Add"), TEXT("2"));
	Defs.Defs.Add(MoveTemp(Seq1));

	// The chained beat. m_fMoveTo 0 means "already in place", so it must NOT move the target — which
	// is how the test tells the chain fired without also re-placing.
	FElysiumEntityDef Seq2 = MakeSeq(TEXT("seq2"), TEXT("0"), FVector(-999.f, -999.f, -999.f), TEXT("0"));
	Wire(Seq2, TEXT("OnEndSequence"), TEXT("Add"), TEXT("4"));
	Defs.Defs.Add(MoveTemp(Seq2));

	// The mapper's "don't move the NPC" override (HL1 CCineMonster SF_SCRIPT_NOSCRIPTMOVEMENT).
	FElysiumEntityDef Seq3 = MakeSeq(TEXT("seq3"), TEXT("1"), FVector(-777.f, -777.f, -777.f), TEXT("128"));
	Defs.Defs.Add(MoveTemp(Seq3));

	FElysiumEntityDef Mover;
	Mover.Classname = TEXT("logic_relay");
	Mover.TargetName = TEXT("mover1");
	Defs.Defs.Add(MoveTemp(Mover));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));

	FElysiumEntity* Seq = World.FindByName(TEXT("seq1"));
	FElysiumEntity* Target = World.FindByName(TEXT("mover1"));
	FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("seq1 registers a leaf class"), Seq) ||
		!TestNotNull(TEXT("mover1 resolved"), Target) ||
		!TestNotNull(TEXT("counter1 resolved"), Count))
	{
		return false;
	}
	TestFalse(TEXT("scripted_sequence is not an inert record"), Seq->IsRecordOnly());

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.f;
	};

	TestTrue(TEXT("the target starts at the origin"), Target->Origin.IsNearlyZero());

	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), Seq->Handle);
	for (int32 i = 0; i < 8; ++i)
	{
		World.Tick(0.0);
	}

	// Both of seq1's outputs fired, then the chain carried seq2's.
	TestEqual(TEXT("OnBeginSequence + OnEndSequence + the chained beat all fired"),
		CounterValue(Count), 7.f);
	// m_fMoveTo 1 placed the target on seq1's mark; seq2's m_fMoveTo 0 left it there.
	TestTrue(TEXT("the target was placed on the mark"), Target->Origin.Equals(Mark, 0.01));
	TestTrue(TEXT("the mark's facing was applied"),
		FMath::IsNearlyEqual(Target->Angles.Y, 90.f, 0.01f));

	// NOSCRIPTMOVEMENT: the beat still runs, but the target stays where it is.
	FElysiumEntity* NoMove = World.FindByName(TEXT("seq3"));
	if (TestNotNull(TEXT("seq3 resolved"), NoMove))
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), NoMove->Handle);
		for (int32 i = 0; i < 4; ++i)
		{
			World.Tick(0.0);
		}
		TestTrue(TEXT("SF_SCRIPT_NOSCRIPTMOVEMENT left the target on its previous mark"),
			Target->Origin.Equals(Mark, 0.01));
	}

	// A sequence naming the player has no body to drive; it must still run as a timing shell so the
	// map's flow continues rather than dead-ending (10 of the 108 target `!playercontroller`).
	FElysiumEntityDefs PlayerDefs;
	PlayerDefs.MapName = TEXT("__test2__");
	FElysiumEntityDef PlayerSeq = MakeSeq(TEXT("pseq"), TEXT("1"), Mark, TEXT("0"));
	PlayerSeq.Keys.Add(TEXT("m_iszEntity"), TEXT("!playercontroller"));
	Wire(PlayerSeq, TEXT("OnEndSequence"), TEXT("Add"), TEXT("9"));
	PlayerDefs.Defs.Add(MoveTemp(PlayerSeq));
	FElysiumEntityDef Counter2;
	Counter2.Classname = TEXT("math_counter");
	Counter2.TargetName = TEXT("counter1");
	PlayerDefs.Defs.Add(MoveTemp(Counter2));

	FElysiumEntityWorld World2(nullptr, nullptr);
	World2.Load(MoveTemp(PlayerDefs));
	FElysiumEntity* PSeq = World2.FindByName(TEXT("pseq"));
	FElysiumEntity* Count2 = World2.FindByName(TEXT("counter1"));
	if (TestNotNull(TEXT("pseq resolved"), PSeq) && TestNotNull(TEXT("counter1 resolved"), Count2))
	{
		World2.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), PSeq->Handle);
		for (int32 i = 0; i < 4; ++i)
		{
			World2.Tick(0.0);
		}
		TestEqual(TEXT("a player-targeted beat still fires OnEndSequence"), CounterValue(Count2), 9.f);
	}

	return true;
}

// =====================================================================================
// The player entity (11.4, S3). The claim under test is that the player stopped being a
// special case: it is a registry class on VtMB's own chain, it answers to a targetname the
// maps already write (`!player`), its inputs arrive through the same R2 walk from either
// direction, it is a real `!activator`, and `point_teleport` moves it exactly as it moves
// anything else. No RHI, no actors, no `tools/out` — the recording stub is the body.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerEntityTest, "Elysium.Substrate.PlayerEntity", GElysiumTestFlags)
bool FElysiumPlayerEntityTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// --- The chain is VtMB's ------------------------------------------------------------
	const FElysiumClassDesc* PlayerDesc = Reg.Find(ElysiumPlayerClassName());
	if (!TestNotNull(TEXT("`player` is registered"), PlayerDesc))
	{
		return false;
	}
	TestEqual(TEXT("player's base is CBaseCombatCharacter"),
		PlayerDesc->BaseName.ToString(), ElysiumCombatCharacterClassName().ToString());
	const FElysiumClassDesc* CharDesc = Reg.Find(ElysiumCombatCharacterClassName());
	const FElysiumClassDesc* AnimDesc = Reg.Find(ElysiumAnimatingClassName());
	if (!TestNotNull(TEXT("CBaseCombatCharacter is registered"), CharDesc) ||
		!TestNotNull(TEXT("CBaseAnimating is registered"), AnimDesc))
	{
		return false;
	}
	TestEqual(TEXT("combat character's base is CBaseAnimating"),
		CharDesc->BaseName.ToString(), ElysiumAnimatingClassName().ToString());
	TestEqual(TEXT("animating's base is CBaseEntity"),
		AnimDesc->BaseName.ToString(), ElysiumBaseClassName().ToString());

	// One walk from the leaf reaches all four levels of the chain.
	auto Resolves = [&Reg, PlayerDesc](const TCHAR* Input)
	{
		return reinterpret_cast<const void*>(Reg.FindInput(*PlayerDesc, FName(Input)));
	};
	TestNotNull(TEXT("player input GiveItem resolves"), Resolves(TEXT("GiveItem")));
	TestNotNull(TEXT("combat-character input MoneyAdd resolves"), Resolves(TEXT("MoneyAdd")));
	TestNotNull(TEXT("animating input SetAnimation resolves"), Resolves(TEXT("SetAnimation")));
	TestNotNull(TEXT("base input Kill resolves"), Resolves(TEXT("Kill")));
	TestNotNull(TEXT("input names fold case"), Resolves(TEXT("moneyadd")));
	// The NPC inherits the same middle nodes — one MoneyAdd for every character in the game.
	if (const FElysiumClassDesc* NpcDesc = Reg.Find(FName(TEXT("npc_VVampire"))))
	{
		TestEqual(TEXT("npc_* sits under CBaseCombatCharacter"),
			NpcDesc->BaseName, ElysiumCombatCharacterClassName());
		TestNotNull(TEXT("an NPC resolves MoneyAdd through the same node"),
			reinterpret_cast<const void*>(Reg.FindInput(*NpcDesc, FName(TEXT("MoneyAdd")))));
	}

	// --- A world with a player ----------------------------------------------------------
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(100, 200, 30);
	Services.PlayerRotation = FRotator(0.f, 90.f, 0.f);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__player_test__");

	// A trigger whose OnStartTouch fires at `!activator` — the activator must be the player.
	FElysiumEntityDef Trig;
	Trig.Classname = TEXT("trigger_multiple");
	Trig.TargetName = TEXT("trig1");
	Trig.Keys.Add(TEXT("spawnflags"), TEXT("1"));   // ALLOW_CLIENTS
	{
		FElysiumOutputDef W;
		W.Name = TEXT("OnStartTouch");
		W.Target = TEXT("!activator");
		W.Input = TEXT("MoneyAdd");
		W.Param = TEXT("7");
		Trig.Outputs.Add(W);
	}
	Defs.Defs.Add(MoveTemp(Trig));

	// A point_teleport aimed at `!player` by name, exactly as 48 of the 49 in the shipped maps are.
	FElysiumEntityDef Tele;
	Tele.Classname = TEXT("point_teleport");
	Tele.TargetName = TEXT("tp1");
	Tele.Origin = FVector(500, 600, 70);
	Tele.Keys.Add(TEXT("target"), TEXT("!player"));
	Tele.Keys.Add(TEXT("angles"), TEXT("0 45 0"));
	Defs.Defs.Add(MoveTemp(Tele));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));

	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("SpawnPlayer created a player entity"), Player))
	{
		return false;
	}
	TestEqual(TEXT("the world reports the same handle"), World.PlayerHandle().Index, PlayerHandle.Index);
	TestEqual(TEXT("it answers to `!player` by name"),
		World.FindByName(ElysiumPlayerTargetName()), static_cast<FElysiumEntity*>(Player));
	TestFalse(TEXT("it is a registered class, not an inert record"), Player->IsRecordOnly());
	TestEqual(TEXT("a second SpawnPlayer is a no-op"), World.SpawnPlayer().Index, PlayerHandle.Index);
	TestTrue(TEXT("Spawn sampled the body's origin"), Player->Origin.Equals(FVector(100, 200, 30)));

	// This world has no game state, so no rulebook reached Spawn and the sheet stayed at zero —
	// which is the fail-closed posture, not a bug: a character with no health model does not die of
	// arithmetic. Seed the ceiling by hand for the damage assertions below. `Elysium.Content.Sheet`
	// is where the real `stats.txt` seed (a flat 100) is checked.
	TestEqual(TEXT("with no rulebook the sheet has no health track"), Player->MaxHealth, 0);
	Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
	Player->SyncHealthFromSheet();
	TestEqual(TEXT("the health keyfield is derived from the sheet's ceiling"), Player->MaxHealth, 100);
	TestEqual(TEXT("...with no damage taken yet"), Player->Health, 100);

	// --- Both directions land on the same field ------------------------------------------
	// (a) the console / Hammer-wire direction: address it by targetname.
	World.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(50),
		0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("ent_fire !player MoneyAdd 50"), Player->Money, 50);

	// (b) the script direction: a bound input fires at `!self` with the entity as caller, which is
	//     exactly what `pc.MoneyAdd(50)` manufactures in the CPython host.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(50),
		0.0, FElysiumEntityHandle::Invalid(), PlayerHandle);
	World.Tick(0.0);
	TestEqual(TEXT("pc.MoneyAdd(50) lands on the same field"), Player->Money, 100);

	// VtMB's own no-op rule: a zero-valued MoneyAdd changes nothing.
	World.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(0),
		0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("a zero-valued MoneyAdd is a silent no-op"), Player->Money, 100);

	// --- `!activator` is real -------------------------------------------------------------
	FElysiumEntity* Trigger = World.FindByName(TEXT("trig1"));
	if (!TestNotNull(TEXT("trigger resolved"), Trigger))
	{
		return false;
	}
	World.RouteBrushTouch(Trigger->Handle, PlayerHandle, /*bBegin*/ true);
	World.Tick(0.0);
	TestEqual(TEXT("a trigger the player walked into resolves !activator to it"), Player->Money, 107);

	// --- point_teleport moves it like any other entity ------------------------------------
	FElysiumEntity* Teleport = World.FindByName(TEXT("tp1"));
	if (!TestNotNull(TEXT("point_teleport resolved"), Teleport))
	{
		return false;
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Teleport->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("the entity's own origin moved"), Player->Origin.Equals(FVector(500, 600, 70)));
	// The body followed through the embodiment — the Source yaw is negated on the way out.
	TestTrue(TEXT("the body was placed at the destination"),
		Services.Saw(TEXT("TeleportPlayer")) && Services.PlayerLocation.Equals(FVector(500, 600, 70)));
	TestTrue(TEXT("the body took the destination's facing"),
		FMath::IsNearlyEqual((float)Services.PlayerRotation.Yaw, -45.f));

	// --- Damage, the unkillable latch, and the death path ---------------------------------
	// The number lands on the sheet's `Health` slot, which counts damage TAKEN (RE24); the entity's
	// `health` keyfield is the engine-space projection of that against `Max_Health`.
	Player->TakeDamage(40.f);
	TestEqual(TEXT("damage accumulates on the sheet's damage slot"),
		Player->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health), 40);
	TestEqual(TEXT("...and the health keyfield follows it down"), Player->Health, 60);
	Player->SetUnkillable(true);
	Player->TakeDamage(1000.f);
	TestEqual(TEXT("an unkillable player floors at 1"), Player->Health, 1);
	Player->SetUnkillable(false);
	Player->TakeDamage(1.f);
	TestEqual(TEXT("health runs out"), Player->Health, 0);

	// --- Hydrate / dehydrate is the map boundary ------------------------------------------
	Player->Money = 250;
	Player->Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Malkavian")));
	Player->Sheet.SetBase(EElysiumTraitContainer::Disciplines, /*Celerity*/ 3, 3);
	FElysiumPlayerRecord Record;
	Player->Dehydrate(Record);
	TestEqual(TEXT("dehydrate carries money"), Record.Money, 250);
	TestEqual(TEXT("dehydrate carries the clan"), Record.Sheet.Clan(), 4);

	FElysiumPlayer Fresh;
	Fresh.Hydrate(Record);
	TestEqual(TEXT("hydrate restores money"), Fresh.Money, 250);
	TestEqual(TEXT("hydrate restores the sheet"),
		Fresh.Sheet.GetCurrent(EElysiumTraitContainer::Disciplines, 3), 3);

	// --- The sheet reads as a registered field, and the bag holds what is left -------------
	// A script read resolves the field table BEFORE the dynamic hook (`Entity_getattro`), so a
	// compiled slot never reaches the bag — that ordering is what turns `pc.base_Celerity` from a
	// default-0 rescue into the real rating.
	{
		const FElysiumFieldAccessor* Celerity = Reg.FindField(*Player->Class, FName(TEXT("base_celerity")));
		if (TestNotNull(TEXT("base_celerity resolves as a datamap field"),
			reinterpret_cast<const void*>(Celerity)))
		{
			TestEqual(TEXT("...reading the slot the sheet holds"), Celerity->Get(*Player).ToInt(), 3);
		}
	}
	FElysiumVariant Dynamic;
	TestTrue(TEXT("a base_ name no slot owns resolves to 0 rather than raising"),
		Player->GetDynamicField(FName(TEXT("base_NotAStat")), Dynamic));
	TestEqual(TEXT("...as zero"), Dynamic.ToInt(), 0);
	TestFalse(TEXT("an unrelated name does not resolve dynamically"),
		Player->GetDynamicField(FName(TEXT("SetExpression")), Dynamic));

	// --- A world with no player is a legal world ------------------------------------------
	{
		FElysiumEntityDefs Bare;
		Bare.MapName = TEXT("__backdrop__");
		FElysiumEntityWorld Backdrop(nullptr, nullptr);
		Backdrop.Load(MoveTemp(Bare));
		TestNull(TEXT("a map built without a player has none"), Backdrop.FindPlayer());
		// And an input addressed at one is an unknown target, not a crash.
		Backdrop.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(1),
			0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		Backdrop.Tick(0.0);
		TestTrue(TEXT("it is reported as an unknown target"), Backdrop.UnknownTargets() > 0);
	}

	return true;
}

// =====================================================================================
// FElysiumWorldServices (11.2) — the substrate's outbound seam. Runs the shape of the
// tutorial's own logic_auto chain end to end against the recording stub: no RHI, no actors,
// no `tools/out`. sp_tutorial_1's five logic_autos fire OnMapLoad at an NPC (WillTalk), a
// door (Lock), a math_counter and a delayed wire; this reproduces that shape and adds one
// entity per service, so all four seams are exercised by the same ignition.
//
// The second half is the contract that makes the first half meaningful: the SAME defs on a
// world with NO services must reach the SAME logical state. Embodiment, audio, travel and
// presentation are outputs of the logic, never inputs to it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldServicesTest, "Elysium.Substrate.WorldServices", GElysiumTestFlags)
bool FElysiumWorldServicesTest::RunTest(const FString&)
{
	// One map's worth of defs, built twice (Load consumes them).
	auto BuildDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Target,
			const TCHAR* Input, const TCHAR* Param, float Delay)
		{
			FElysiumOutputDef W;
			W.Name = Output;
			W.Target = Target;
			W.Input = Input;
			W.Param = Param;
			W.Delay = Delay;
			W.Times = -1;
			On.Outputs.Add(W);
		};

		// The ignition, shaped like the tutorial's own: an NPC latch, an NPC animation, a door
		// lock, an ambient sound, a screen fade, and one delayed counter wire.
		FElysiumEntityDef Auto;
		Auto.Classname = TEXT("logic_auto");
		Wire(Auto, TEXT("OnMapLoad"), TEXT("Jack"),      TEXT("WillTalk"),     TEXT("0"),          0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("Jack"),      TEXT("SetAnimation"), TEXT("cower_idle"), 0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("frontdoor"), TEXT("Lock"),         TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("amb1"),      TEXT("PlaySound"),    TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("fade1"),     TEXT("Fade"),         TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("counter1"),  TEXT("Add"),          TEXT("5"),          0.1f);
		Defs.Defs.Add(MoveTemp(Auto));

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Jack");
		Npc.Origin = FVector(100.f, 200.f, 300.f);
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Npc.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
		Defs.Defs.Add(MoveTemp(Npc));

		// A door with no hulls: the class runs, no brush body is built (there is no owner actor
		// to build one on either), and `Lock` still lands.
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door");
		Door.TargetName = TEXT("frontdoor");
		Defs.Defs.Add(MoveTemp(Door));

		FElysiumEntityDef Amb;
		Amb.Classname = TEXT("ambient_generic");
		Amb.TargetName = TEXT("amb1");
		Amb.Keys.Add(TEXT("message"), TEXT("ambient\\tutorial\\hum.wav"));
		Amb.Keys.Add(TEXT("health"), TEXT("8"));          // VtMB VOLUME 0-10 -> 0.8 linear
		Defs.Defs.Add(MoveTemp(Amb));

		// start_enabled: the scheme fades in from the entity's own Spawn(), which is why the
		// audio service has to be live before the spawn pass rather than after it.
		FElysiumEntityDef Scheme;
		Scheme.Classname = TEXT("ambient_soundscheme");
		Scheme.TargetName = TEXT("scheme1");
		Scheme.Keys.Add(TEXT("scheme_file"), TEXT("sound/Schemes/Tutorial.txt"));
		Scheme.Keys.Add(TEXT("start_enabled"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(Scheme));

		// SF_FADE_IN (1): the colour sits flat at MaxAlpha instead of ramping, so the fade is
		// already visible at its own start time — which is what makes it assertable against a
		// clock that never advances (a bare world has no game state, so `now` is always 0).
		FElysiumEntityDef Fade;
		Fade.Classname = TEXT("env_fade");
		Fade.TargetName = TEXT("fade1");
		Fade.Keys.Add(TEXT("duration"), TEXT("2.5"));
		Fade.Keys.Add(TEXT("holdtime"), TEXT("1.5"));
		Fade.Keys.Add(TEXT("spawnflags"), TEXT("1"));
		Wire(Fade, TEXT("OnBeginFade"), TEXT("counter1"), TEXT("Add"), TEXT("3"), 0.0f);
		Defs.Defs.Add(MoveTemp(Fade));

		FElysiumEntityDef Change;
		Change.Classname = TEXT("trigger_changelevel");
		Change.TargetName = TEXT("toalley");
		Change.Keys.Add(TEXT("map"), TEXT("la_hub_1"));
		Change.Keys.Add(TEXT("landmark"), TEXT("lm_alley"));
		Defs.Defs.Add(MoveTemp(Change));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));

		return Defs;
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.f;
	};

	// --- With services: the chain reaches all four seams ---------------------------------
	FElysiumRecordingServices Rec;
	Rec.bHasPlayer = true;
	Rec.PlayerLocation = FVector(1000.f, 0.f, 0.f);
	Rec.PlayerRotation = FRotator(0.f, 90.f, 0.f);

	{
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Rec.Bundle());
		World.Load(BuildDefs());
		// A played map has a player entity (11.4) — the map actor creates one after Load, and the
		// travel seam below reads its placement, so the test builds the same shape.
		World.SpawnPlayer();

		// Spawn-time reaches: the NPC stood a body, the start_enabled scheme faded in.
		TestTrue(TEXT("the NPC stood a body through the embodiment"),
			Rec.Saw(TEXT("BuildNpcVisual jack")));
		TestTrue(TEXT("start_enabled ambient_soundscheme faded in at spawn"),
			Rec.Saw(TEXT("FadeInScheme sound/Schemes/Tutorial.txt")));
		TestEqual(TEXT("the ambient_soundscheme reports itself active"),
			Rec.ActiveSchemeRel(), FString(TEXT("sound/Schemes/Tutorial.txt")));

		// logic_auto ignites on the first tick; the 0.1 s wire lands on a later one.
		World.Tick(0.0);
		for (int32 i = 0; i < 4; ++i)
		{
			World.Tick(0.2);
		}

		FElysiumEntity* Counter = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("counter1 resolved"), Counter))
		{
			return false;
		}
		// 5 from the delayed OnMapLoad wire + 3 from env_fade's own OnBeginFade.
		TestEqual(TEXT("the delayed OnMapLoad wire and the fade's own output both drained"),
			CounterValue(Counter), 8.f);

		TestTrue(TEXT("SetAnimation reached the embodiment"),
			Rec.Saw(TEXT("PlayNpcClip jack cower_idle")));
		TestTrue(TEXT("ambient_generic played a voice through the audio service"),
			Rec.Saw(TEXT("PlayVoice ambient/tutorial/hum.wav")));
		TestTrue(TEXT("env_fade announced the fade to the presenter"),
			Rec.Saw(TEXT("StartFade dur=2.50 hold=1.50")));

		// The travel seam: a forced ChangeLevel captures the placement of the
		// player entity — sampled off the body at the top of the frame (11.4) — and asks the travel
		// service for the transition. This map has no source landmark, so the offset stays zero —
		// the warning path — and the yaw is the one the body reported.
		FElysiumEntity* Change = World.FindByName(TEXT("toalley"));
		if (TestNotNull(TEXT("toalley resolved"), Change))
		{
			AddExpectedError(TEXT("source landmark"), EAutomationExpectedErrorFlags::Contains, 0);
			World.EnqueueInput(TEXT("!self"), FName(TEXT("ChangeLevel")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), Change->Handle);
			World.Tick(1.0);
			TestTrue(TEXT("trigger_changelevel asked the travel service for the transition"),
				Rec.Saw(TEXT("RequestLandmarkTravel la_hub_1@lm_alley")));
			TestTrue(TEXT("the transition carried the player's yaw"), Rec.Log().Contains(TEXT("yaw=90.0")));
		}
	}

	// Every service call the run made, so a failure message is worth reading.
	AddInfo(FString::Printf(TEXT("service calls: %s"), *Rec.Log()));

	// --- Without services: the same logic, the same state --------------------------------
	// A default bundle is four null pointers. Nothing may crash, and the logic layer must land
	// exactly where it did above — that is the whole claim of the seam, and it is also the
	// `elysium.NpcBodies 0` / `elysium.BrushBodies 0` A/B path the game already ships.
	{
		FElysiumEntityWorld Bare(/*Owner*/ nullptr, /*GameState*/ nullptr);
		Bare.Load(BuildDefs());

		TestNull(TEXT("a bare world has no embodiment"), Bare.Embodiment());
		TestNull(TEXT("a bare world has no audio"), Bare.Audio());
		TestNull(TEXT("a bare world has no travel"), Bare.Travel());
		TestNull(TEXT("a bare world has no presenter"), Bare.Presenter());

		Bare.Tick(0.0);
		for (int32 i = 0; i < 4; ++i)
		{
			Bare.Tick(0.2);
		}

		FElysiumEntity* Counter = Bare.FindByName(TEXT("counter1"));
		if (TestNotNull(TEXT("counter1 resolved without services"), Counter))
		{
			TestEqual(TEXT("the chain reaches the same state with no services at all"),
				CounterValue(Counter), 8.f);
		}
		// The fade is still world state, held for AElysiumHUD to poll, whether or not a presenter
		// heard about it — 11.8 is what moves that state onto the published view state.
		FLinearColor Faded;
		TestTrue(TEXT("the screen fade is still world state, not presenter state"),
			Bare.GetScreenFade(Faded));

		// The +use cursor with no embodiment: no view point, so nothing is ever aimed at, and
		// pressing use is a safe no-op rather than a null deref.
		Bare.UpdateUseCursor();
		TestFalse(TEXT("no embodiment means no use cursor"), Bare.GetAimedUsable().IsSet());
		Bare.PlayerUse();
	}

	return true;
}

// =====================================================================================
// The camera (11.7) — the weight driver and the scripted-shot channel.
//
// VtMB ships one camera with a blend weight, not two cameras, and the whole first<->third
// transition is that weight ramping at a fixed rate with a priority order over three latches
// (`camera-view-modes.md` §3). That is a pure function of time and flags, so it is asserted here
// with no pawn, no world and no RHI — which is also what makes "0 -> 1 in 0.5 s" a number rather
// than a stopwatch.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraTest, "Elysium.Substrate.Camera", GElysiumTestFlags)
bool FElysiumCameraTest::RunTest(const FString&)
{
	// Advance a weight block in fixed steps, the way a frame would.
	auto Run = [](FElysiumCameraWeights& W, float Seconds, float Step, float TimeScale = 1.0f)
	{
		for (float T = 0.0f; T < Seconds - KINDA_SMALL_NUMBER; T += Step)
		{
			W.Advance(Step, TimeScale);
		}
	};

	// --- the transition is 0.5 s in both directions, at 2.0/s ---
	{
		FElysiumCameraWeights W;
		TestEqual(TEXT("a fresh camera is first person"), W.Third, 0.0f);
		TestFalse(TEXT("and reports first person"), W.IsThirdPerson());

		W.bUserThird = true;
		Run(W, 0.25f, 1.0f / 60.0f);
		TestTrue(TEXT("halfway through the blend the weight is mid-ramp"),
			W.Third > 0.4f && W.Third < 0.6f);
		TestTrue(TEXT("but it already reports third person -- the predicate is a disjunction"),
			W.IsThirdPerson());

		Run(W, 0.30f, 1.0f / 60.0f);
		TestEqual(TEXT("a full traversal takes 0.5 s and clamps at 1"), W.Third, 1.0f);

		W.bUserThird = false;
		Run(W, 0.50f, 1.0f / 60.0f);
		TestEqual(TEXT("and 0.5 s back down, clamped at 0"), W.Third, 0.0f);
		TestFalse(TEXT("only at exactly 0 does it stop being third person"), W.IsThirdPerson());
	}

	// --- the frame rate does not change the duration ---
	{
		FElysiumCameraWeights Fast, Slow;
		Fast.bUserThird = Slow.bUserThird = true;
		Run(Fast, 0.5f, 1.0f / 240.0f);
		Run(Slow, 0.5f, 1.0f / 30.0f);
		TestEqual(TEXT("240 Hz and 30 Hz reach the same place"), Fast.Third, Slow.Third);
	}

	// --- it is scaled by the player's time scale, not wall-clock ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f, /*TimeScale*/ 0.5f);
		TestTrue(TEXT("at half time scale half a second gets halfway"),
			FMath::IsNearlyEqual(W.Third, 0.5f, 0.02f));
		Run(W, 0.5f, 1.0f / 60.0f, 0.5f);
		TestEqual(TEXT("and the full second finishes it"), W.Third, 1.0f);

		FElysiumCameraWeights Held;
		Held.bUserThird = true;
		Run(Held, 1.0f, 1.0f / 60.0f, /*TimeScale*/ 0.0f);
		TestEqual(TEXT("a held world does not blend at all"), Held.Third, 0.0f);
	}

	// --- symmetric and interruptible: reversing mid-blend resumes, it does not restart ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		Run(W, 0.25f, 1.0f / 60.0f);
		const float Mid = W.Third;

		W.bUserThird = false;
		Run(W, 0.10f, 1.0f / 60.0f);
		TestTrue(TEXT("reversing continues from the current weight"), W.Third < Mid && W.Third > 0.0f);

		// Turning it straight back on and running the *remaining* time finishes the blend: there is no
		// transition object holding a start snapshot, so nothing restarts.
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("and re-reversing finishes without restarting"), W.Third, 1.0f);
	}

	// --- the priority order: forced-third and feed win, then forced-first, then the user toggle ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = false;
		W.bForcedThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("forced-third outranks the user's first-person toggle"), W.Third, 1.0f);
		TestEqual(TEXT("and says so"), FString(W.Driver()), FString(TEXT("forced-third")));

		W.bForcedThird = false;
		W.bForcedFirst = true;
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("forced-first outranks the user's third-person toggle"), W.Third, 0.0f);

		// The feed camera raises its own weight, and that alone holds third person.
		W.bForcedFirst = false;
		W.bUserThird = false;
		W.bFeed = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("the feed camera drives the third weight up"), W.Third, 1.0f);
		TestEqual(TEXT("and is the deciding driver"), FString(W.Driver()), FString(TEXT("feed")));
	}

	// --- a scripted camera counts as third person, which is what draws the player model under it ---
	{
		FElysiumCameraWeights W;
		W.Scripted = 0.3f;
		TestTrue(TEXT("a scripted shot reads as third person"), W.IsThirdPerson());
		W.Scripted = 0.0f;
		W.Secondary = 0.2f;
		TestTrue(TEXT("so does the secondary weight"), W.IsThirdPerson());
		Run(W, 0.5f, 1.0f / 60.0f);
		TestTrue(TEXT("which decays at 0.5/s"), FMath::IsNearlyEqual(W.Secondary, 0.2f - 0.25f * 1.0f, 0.02f)
			|| W.Secondary == 0.0f);
	}

	// --- the easing is at the point of use, not in the ramp ---
	{
		TestEqual(TEXT("SimpleSpline(0)"), ElysiumCam::SimpleSpline(0.0f), 0.0f);
		TestEqual(TEXT("SimpleSpline(1)"), ElysiumCam::SimpleSpline(1.0f), 1.0f);
		TestEqual(TEXT("SimpleSpline(0.5) is its own midpoint"), ElysiumCam::SimpleSpline(0.5f), 0.5f);
		TestTrue(TEXT("and it eases in below the midpoint"), ElysiumCam::SimpleSpline(0.25f) < 0.25f);
		TestTrue(TEXT("and out above it"), ElysiumCam::SimpleSpline(0.75f) > 0.75f);
		TestEqual(TEXT("it clamps rather than extrapolating"), ElysiumCam::SimpleSpline(2.0f), 1.0f);
	}

	// --- the rate-limited approach: clamp the step, snap when inside it, wrap in angle space ---
	{
		TestEqual(TEXT("a step larger than the gap arrives"),
			ElysiumCam::Approach(0.0f, 10.0f, 1000.0f, 1.0f), 10.0f);
		TestEqual(TEXT("a step smaller than the gap is clamped"),
			ElysiumCam::Approach(0.0f, 10.0f, 4.0f, 1.0f), 4.0f);
		TestEqual(TEXT("and it works downward too"),
			ElysiumCam::Approach(10.0f, 0.0f, 4.0f, 1.0f), 6.0f);
		TestEqual(TEXT("a zero speed snaps"), ElysiumCam::Approach(0.0f, 10.0f, 0.0f, 1.0f), 10.0f);
		// 359 -> 1 is two degrees, not 358.
		TestTrue(TEXT("angle space takes the short way round"),
			FMath::IsNearlyEqual(ElysiumCam::ApproachAngle(179.0f, -179.0f, 1.0f, 1.0f), 180.0f, 0.01f));
	}

	// --- the cvar surface: defaults are VtMB's, a console value overrides, units convert once ---
	{
		TMap<FString, FString> Store;
		auto Lookup = [&Store](const TCHAR* Name) -> FString
		{
			const FString* V = Store.Find(FString(Name).ToLower());
			return V ? *V : FString();
		};

		FElysiumCameraCvars Cvars;
		Cvars.LoadFrom(Lookup);
		TestTrue(TEXT("cam_idealdist defaults to 85 Source units, held in cm"),
			FMath::IsNearlyEqual(Cvars.IdealDist, 85.0f * 2.54f, 0.01f));
		TestEqual(TEXT("cam_targetangle is degrees and needs no conversion"), Cvars.TargetAngle, 15.0f);
		TestTrue(TEXT("the damper is on with two constants"),
			Cvars.bDampOn && Cvars.HookesConstant == 4.0f && Cvars.HookesConstantWall == 15.0f);

		Store.Add(TEXT("cam_idealdist"), TEXT("50"));
		Store.Add(TEXT("cdamp_on"), TEXT("0"));
		Cvars.LoadFrom(Lookup);
		TestTrue(TEXT("a console value wins over the default"),
			FMath::IsNearlyEqual(Cvars.IdealDist, 50.0f * 2.54f, 0.01f));
		TestFalse(TEXT("and cdamp_on 0 bypasses the damper"), Cvars.bDampOn);

		// Every name `camera-view-modes.md` §1 lists is declared, so none of them can fall through to
		// Python when a user's config.cfg or the patch's aliases write it.
		TestTrue(TEXT("the whole cvar surface is declared"), ElysiumCam::CvarDefs().Num() >= 24);
		bool bFoundPrefs = false;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			bFoundPrefs |= FString(Def.Name) == TEXT("camera_prefs");
		}
		TestTrue(TEXT("including the archived weapon prefs, so a cfg round-trips"), bFoundPrefs);
	}

	// --- the scripted-shot channel: handle-based push/pop and a timed ramp ---
	{
		FElysiumCameraShotStack Stack;
		TestFalse(TEXT("an idle channel has no shot"), Stack.IsActive());
		TestEqual(TEXT("and no weight"), Stack.GetWeight(), 0.0f);

		FElysiumCameraShot Dialogue;
		Dialogue.DebugName = TEXT("DialogDefault");
		Dialogue.BlendSeconds = 0.5f;
		Dialogue.FieldOfView = 40.0f;
		const int32 DlgId = Stack.Push(Dialogue);
		TestTrue(TEXT("a push mints an id"), DlgId > 0);
		TestEqual(TEXT("and the shot decides"), Stack.Top()->DebugName, FString(TEXT("DialogDefault")));

		for (int32 i = 0; i < 30; ++i) { Stack.Advance(1.0f / 60.0f); }
		TestEqual(TEXT("the ramp takes the shot's own duration, not a fixed rate"), Stack.GetWeight(), 1.0f);

		// A cutscene opening over a conversation: the later push decides, and the ramp holds at 1.
		FElysiumCameraShot Cutscene;
		Cutscene.DebugName = TEXT("Theatre");
		const int32 CutId = Stack.Push(Cutscene);
		TestEqual(TEXT("the later push decides"), Stack.Top()->DebugName, FString(TEXT("Theatre")));
		Stack.Advance(1.0f / 60.0f);
		TestEqual(TEXT("and the channel stays at full weight across the swap"), Stack.GetWeight(), 1.0f);

		// Shots end out of order — a conversation closing behind a running cutscene.
		TestTrue(TEXT("a shot pops from wherever it sits"), Stack.Pop(DlgId));
		TestEqual(TEXT("the cutscene is untouched"), Stack.Top()->DebugName, FString(TEXT("Theatre")));
		TestFalse(TEXT("a doubled pop is a no-op"), Stack.Pop(DlgId));

		// Refreshing a live shot is what a `Follow` attach type is; it must not restart the ramp.
		FElysiumCameraShot Moved = Cutscene;
		Moved.Origin = FVector(100.0f, 0.0f, 0.0f);
		Moved.BlendSeconds = 99.0f;
		TestTrue(TEXT("a live shot refreshes"), Stack.Update(CutId, Moved));
		TestEqual(TEXT("with its new values"), (float)Stack.Top()->Origin.X, 100.0f);
		TestEqual(TEXT("but keeps the blend it was pushed with"), Stack.Top()->BlendSeconds, 0.5f);
		TestFalse(TEXT("and a stale id refreshes nothing"), Stack.Update(DlgId, Moved));

		TestTrue(TEXT("the last shot pops"), Stack.Pop(CutId));
		TestTrue(TEXT("leaving nothing on top"), Stack.Top() == nullptr);
		for (int32 i = 0; i < 30; ++i) { Stack.Advance(1.0f / 60.0f); }
		TestEqual(TEXT("and the weight ramps back out over the same duration"), Stack.GetWeight(), 0.0f);
		TestFalse(TEXT("the channel is idle again"), Stack.IsActive());

		// A shot with no blend cuts.
		FElysiumCameraShot Cut;
		Cut.BlendSeconds = 0.0f;
		Stack.Push(Cut);
		Stack.Advance(1.0f / 60.0f);
		TestEqual(TEXT("a zero-duration shot is a cut"), Stack.GetWeight(), 1.0f);
	}

	return true;
}

// =====================================================================================
// vdata/camerashots — the shot files SetCamera names (11.7).
//
// The grammar is documented by Troika in the shipped `camera shots how-to.txt`, so this asserts the
// read against that document rather than against itself: which block wins, how the two target points
// combine, and that Source units become cm exactly once.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraShotsTest, "Elysium.Substrate.CameraShots", GElysiumTestFlags)
bool FElysiumCameraShotsTest::RunTest(const FString&)
{
	// `dialogdefault.txt`, verbatim in shape — the conversation camera every `.dlg` line starts from.
	const FString Text = TEXT(R"(
CameraShotTable
{
	// This is the default camera that is used when dialog begins.
	DialogDefault
	{
		End
		{
			"Position"		"DialogTarget"
			"AttachPos"		"Origin"
			"AttachType"		"Follow"
			"OffsetOrigin"		"[40, 0, 65]"
		}

		Target
		{
			Point1
			{
				"Position"	"DialogTarget"
				"AttachPos"	"Bone: Bip01 Head"
				"AttachType"	"None"
			}
			Point2
			{
				"Position"	"Player"
				"AttachPos"	"EyePosition"
				"AttachType"	"None"
			}
		}

		CameraConstraints
		{
			"MoveSpeed"		"500"
			"MoveAccel"		"250"
			"MaxTurnRate"		"[60, 60, 60]"
			"DistanceTolerance"	"5"
			"FieldOfView"		"40"
			"DialogPOV"		"1"
		}
	}
}
)");

	FElysiumCameraShotDef Def;
	TestTrue(TEXT("the shot file parses"), ElysiumCameraShots::ParseText(Text, Def));
	TestEqual(TEXT("one file, one shot, named after it"), Def.Name, FString(TEXT("dialogdefault")));

	TestTrue(TEXT("End is where the shot lives"), Def.End.bPresent);
	TestFalse(TEXT("this one authors no Start, so it enters from wherever the camera is"),
		Def.Start.bPresent);
	TestTrue(TEXT("it hangs off the conversation partner"),
		Def.End.Position == EElysiumShotPosition::DialogTarget);
	TestTrue(TEXT("and follows them"), Def.End.Attach == EElysiumShotAttach::Follow);
	TestTrue(TEXT("OffsetOrigin converts Source units to cm exactly once"),
		Def.End.OffsetOrigin.Equals(FVector(40.0f, 0.0f, 65.0f) * 2.54f, 0.01f));

	TestTrue(TEXT("the first target point is a bone on the partner"),
		Def.Target1.bPresent && Def.Target1.AttachPos == TEXT("Bone: Bip01 Head"));
	TestTrue(TEXT("and it does not follow -- AttachType None is set-and-stay"),
		Def.Target1.Attach == EElysiumShotAttach::None);
	TestTrue(TEXT("the second point is the player's eye"),
		Def.Target2.bPresent && Def.Target2.Position == EElysiumShotPosition::Player);

	TestEqual(TEXT("FieldOfView is degrees, unconverted"), Def.Constraints.FieldOfView, 40.0f);
	TestTrue(TEXT("MoveSpeed is inches/sec, held in cm/s"),
		FMath::IsNearlyEqual(Def.Constraints.MoveSpeed, 500.0f * 2.54f, 0.01f));
	TestTrue(TEXT("MaxTurnRate parses out of its bracket form"),
		Def.Constraints.MaxTurnRate.Equals(FVector(60.0f, 60.0f, 60.0f), 0.01f));
	TestTrue(TEXT("DialogPOV rides through"), Def.Constraints.bDialogPOV);
	TestTrue(TEXT("DistanceTolerance converts too"),
		FMath::IsNearlyEqual(Def.Constraints.DistanceTolerance, 5.0f * 2.54f, 0.01f));
	// Nothing in the file says otherwise, so the defaults the how-to implies hold.
	TestFalse(TEXT("AutoPositionFromTarget defaults off"), Def.Constraints.bAutoPositionFromTarget);
	TestTrue(TEXT("ShowHud defaults on"), Def.Constraints.bShowHud);

	// A file with no shot block is a miss, not a half-built shot.
	FElysiumCameraShotDef Empty;
	TestFalse(TEXT("an empty table parses to nothing"),
		ElysiumCameraShots::ParseText(TEXT("CameraShotTable\n{\n}\n"), Empty));
	TestFalse(TEXT("and so does empty text"), ElysiumCameraShots::ParseText(FString(), Empty));

	// A `Named` anchor carries the entity name, and a bare name is taken as one (the how-to writes
	// `Named` both as the keyword and as "the name of an entity in the map").
	FElysiumCameraShotDef Named;
	TestTrue(TEXT("a named-entity shot parses"), ElysiumCameraShots::ParseText(TEXT(R"(
CameraShotTable { Vantage { End { "Position" "cam_marker_1" "AttachPos" "Origin" "AttachType" "None" } } }
)"), Named));
	TestTrue(TEXT("an unrecognised Position is the entity's own name"),
		Named.End.Position == EElysiumShotPosition::Named && Named.End.NamedEntity == TEXT("cam_marker_1"));

	return true;
}

// =====================================================================================
// The presentation seam (11.8, runtime-architecture.md §11). FElysiumViewState is a value and its
// rules are total functions over it, so the whole set is asserted with no world, no HUD and no
// viewport — the hand-built state a widget renders from is exactly what is built here.
//
// The load-bearing case is the one the polled HUD got wrong: a conversation already on screen when
// the pause menu opens is republished as *closed*, so the box reconciles to Teardown instead of
// drawing through the menu.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumViewStateTest, "Elysium.Substrate.ViewState", GElysiumTestFlags)
bool FElysiumViewStateTest::RunTest(const FString&)
{
	using EState = EElysiumAppState;
	static const EState All[] = { EState::Boot, EState::FrontEnd, EState::Loading,
		EState::Playing, EState::Paused, EState::GameOver };

	// --- the one gating rule ---------------------------------------------------------------
	// Playing with no screen up is the only combination that shows a player-facing surface. Both
	// writers are asked, because `elysium.menu` raises a screen without moving the app state and
	// TriggerGameOver moves the state without the menu having opened yet.
	for (const EState S : All)
	{
		const bool bExpected = (S == EState::Playing);
		TestEqual(*FString::Printf(TEXT("%s with no menu"), ElysiumAppState::Name(S)),
			ElysiumView::ShowsPlayerSurface(S, /*bMenuOpen*/ false), bExpected);
		TestFalse(*FString::Printf(TEXT("%s with a menu up"), ElysiumAppState::Name(S)),
			ElysiumView::ShowsPlayerSurface(S, /*bMenuOpen*/ true));
	}

	// --- the reticle ------------------------------------------------------------------------
	FElysiumViewState V;
	TestEqual(TEXT("no surface, no reticle"), ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);

	V.bPlayerSurface = true;
	TestEqual(TEXT("the plain aim cross by default"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::Cross);

	V.ReticleIcon = 7;
	TestEqual(TEXT("a usable under the cursor swaps in the context icon"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::UseIcon);

	// A panel with HideHUD owns the screen (P4.10) — no crosshair under it, icon or not.
	V.bSignHidesHUD = true;
	TestEqual(TEXT("a HideHUD panel takes the reticle with it"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);
	V.bSignHidesHUD = false;

	// The rule is total: a suppressed surface reports nothing even with an icon left in the field.
	V.bPlayerSurface = false;
	TestEqual(TEXT("suppression outranks a stale icon"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);

	// --- the dialogue reconcile ---------------------------------------------------------------
	// The pointers are identity only and never dereferenced, so two distinct addresses stand in for
	// two conversations.
	const FElysiumDlgConversation* const ConvA = reinterpret_cast<const FElysiumDlgConversation*>(0x1);
	const FElysiumDlgConversation* const ConvB = reinterpret_cast<const FElysiumDlgConversation*>(0x2);

	FElysiumDialogueView Closed;
	FElysiumDialogueView TurnOne;
	TurnOne.Conversation = ConvA;
	TurnOne.Revision = 3;
	FElysiumDialogueView TurnTwo = TurnOne;
	TurnTwo.Revision = 4;
	FElysiumDialogueView Other;
	Other.Conversation = ConvB;
	Other.Revision = 3;

	using EAction = ElysiumView::EDialogueAction;
	TestEqual(TEXT("nothing open, nothing up"),
		ElysiumView::ReconcileDialogue(nullptr, 0, Closed), EAction::None);
	TestEqual(TEXT("a conversation opens"),
		ElysiumView::ReconcileDialogue(nullptr, 0, TurnOne), EAction::Rebuild);
	TestEqual(TEXT("the same turn again leaves the retained box alone"),
		ElysiumView::ReconcileDialogue(ConvA, 3, TurnOne), EAction::None);
	TestEqual(TEXT("the turn advances"),
		ElysiumView::ReconcileDialogue(ConvA, 3, TurnTwo), EAction::Rebuild);
	TestEqual(TEXT("a different conversation at the same revision still rebuilds"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Other), EAction::Rebuild);
	TestEqual(TEXT("the conversation ends"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Closed), EAction::Teardown);

	// The bug the seam closes: the publisher withholds the whole player-facing surface while a
	// screen is up, so a box that is on screen when the pause menu opens is told to come down.
	FElysiumViewState Paused;
	Paused.App = EState::Paused;
	Paused.bPlayerSurface = ElysiumView::ShowsPlayerSurface(Paused.App, /*bMenuOpen*/ true);
	TestFalse(TEXT("a paused frame publishes no surface"), Paused.bPlayerSurface);
	TestFalse(TEXT("and therefore no conversation"), Paused.Dialogue.IsOpen());
	TestEqual(TEXT("so an open box comes down instead of drawing through the menu"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Paused.Dialogue), EAction::Teardown);

	// --- the meters -------------------------------------------------------------------------
	// Compared by value, because the change delegate fires on a difference and nothing else.
	FElysiumVitals Vit;
	TestFalse(TEXT("no player entity means no meters"), Vit.bValid);
	FElysiumVitals Same = Vit;
	TestTrue(TEXT("an unchanged sheet compares equal"), Same == Vit);
	Same.bValid = true;
	Same.Health = 80;
	Same.MaxHealth = 100;
	TestTrue(TEXT("a seeded sheet does not"), Same != Vit);
	FElysiumVitals Bled = Same;
	Bled.BloodPool = Same.BloodPool - 1;
	TestTrue(TEXT("and neither does one point of blood"), Bled != Same);

	return true;
}

// =====================================================================================
// 11.9 — persistence. `save-architecture.md` §10 asks for the strongest harness in the
// project, because a silent save bug surfaces hours later. Three tests, all content-free:
// a freeze/rebuild/apply/re-freeze **digest** round trip on a bare world, the payload
// container's version and integrity gates, and the schema rules that let a payload survive
// a build that has moved on.
// =====================================================================================

namespace
{
	// The map the round trip runs on: a relay wired into a counter, plus a timer — between them
	// they cover a registered leaf field, a think time, an output counter and the dormancy switch.
	FElysiumEntityDefs MakeSaveTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__save_test__");

		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("logic_relay");
		Relay.TargetName = TEXT("relay1");
		{
			FElysiumOutputDef Wire;
			Wire.Name = TEXT("OnTrigger");
			Wire.Target = TEXT("counter1");
			Wire.Input = TEXT("Add");
			Wire.Param = TEXT("5");
			Wire.Times = 2;            // a countdown the snapshot has to carry
			Relay.Outputs.Add(Wire);
		}
		Defs.Defs.Add(MoveTemp(Relay));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Counter.Keys.Add(TEXT("max"), TEXT("100"));
		Defs.Defs.Add(MoveTemp(Counter));

		FElysiumEntityDef Timer;
		Timer.Classname = TEXT("logic_timer");
		Timer.TargetName = TEXT("timer1");
		Timer.Keys.Add(TEXT("RefireTime"), TEXT("5"));
		Defs.Defs.Add(MoveTemp(Timer));

		return Defs;
	}

	// A snapshot's bytes — the digest §8's byte-identical rule is stated in terms of.
	TArray<uint8> ElysiumSaveDigest(const FElysiumMapSnapshot& Snapshot)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Ar << const_cast<FElysiumMapSnapshot&>(Snapshot);
		return Bytes;
	}

	// math_counter's Value, off the debug-state rows (the leaf class is file-local).
	float SaveTestCounterValue(const FElysiumEntity* Entity)
	{
		if (!Entity)
		{
			return -1.0f;
		}
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSaveRoundTripTest, "Elysium.Substrate.SaveRoundTrip",
	GElysiumTestFlags)
bool FElysiumSaveRoundTripTest::RunTest(const FString&)
{
	// --- Build, mutate through the real chokepoints, freeze ------------------------------------
	FElysiumEntityWorld A(/*Owner*/ nullptr, /*GameState*/ nullptr);
	A.Load(MakeSaveTestDefs());

	FElysiumEntity* Relay = A.FindByName(TEXT("relay1"));
	FElysiumEntity* Counter = A.FindByName(TEXT("counter1"));
	FElysiumEntity* Timer = A.FindByName(TEXT("timer1"));
	if (!TestNotNull(TEXT("relay1"), Relay) || !TestNotNull(TEXT("counter1"), Counter)
		|| !TestNotNull(TEXT("timer1"), Timer))
	{
		return false;
	}

	// One wire delivery: the counter advances and the relay's `times` counts down.
	A.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Relay->Handle);
	for (int32 i = 0; i < 4; ++i) { A.Tick(0.0); }
	TestEqual(TEXT("counter advanced before the freeze"), SaveTestCounterValue(Counter), 5.0f);

	// The dormancy switch, a runtime-created entity, and a delayed event still pending.
	Timer->ScriptHide();
	FElysiumEntityDef Spawned;
	Spawned.Classname = TEXT("math_counter");
	Spawned.TargetName = TEXT("runtime1");
	const FElysiumEntityHandle SpawnedHandle = A.SpawnRuntimeEntity(MoveTemp(Spawned));
	TestTrue(TEXT("runtime entity created"), A.Resolve(SpawnedHandle) != nullptr);

	A.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(3), /*Delay*/ 30.0,
		FElysiumEntityHandle::Invalid(), Relay->Handle);
	TestEqual(TEXT("one event still pending at freeze time"), A.Queue().Num(), 1);

	FElysiumMapSnapshot First;
	A.Freeze(First);
	TestEqual(TEXT("the frozen map names itself"), First.MapName, FString(TEXT("__save_test__")));
	TestEqual(TEXT("the pending delivery is in the snapshot"), First.Queue.Num(), 1);
	// The omission rule: only entities that differ from a fresh build of their own def are written.
	TestTrue(TEXT("something was recorded"), First.Entities.Num() >= 3);
	TestTrue(TEXT("but not more than the world holds"), First.Entities.Num() <= A.NumEntities());

	// --- Rebuild from the same defs, apply, re-freeze, compare digests -------------------------
	FElysiumEntityWorld B(/*Owner*/ nullptr, /*GameState*/ nullptr);
	B.Load(MakeSaveTestDefs());
	const int32 AppliedCount = B.ApplySnapshot(First);
	TestEqual(TEXT("every record applied"), AppliedCount, First.Entities.Num());

	FElysiumEntity* CounterB = B.FindByName(TEXT("counter1"));
	FElysiumEntity* TimerB = B.FindByName(TEXT("timer1"));
	if (!TestNotNull(TEXT("counter1 after restore"), CounterB)
		|| !TestNotNull(TEXT("timer1 after restore"), TimerB))
	{
		return false;
	}
	TestEqual(TEXT("the counter's value survived"), SaveTestCounterValue(CounterB), 5.0f);
	TestTrue(TEXT("the hidden timer is still hidden"), TimerB->IsHidden());
	TestNotNull(TEXT("the runtime entity restored under its own name"), B.FindByName(TEXT("runtime1")));
	TestEqual(TEXT("the queue was replaced, not appended to"), B.Queue().Num(), 1);
	if (const FElysiumIOEvent* Head = B.Queue().PeekEarliest())
	{
		TestEqual(TEXT("its fire time is absolute and unchanged"), Head->FireTime, 30.0);
		// §6 — a saved handle is re-stamped against the live epoch, so it resolves again.
		if (const FElysiumEntity* Caller = B.Resolve(Head->Caller))
		{
			TestEqual(TEXT("the caller handle re-resolved to the entity that queued it"),
				Caller->Handle.Index, Relay->Handle.Index);
		}
		else
		{
			AddError(TEXT("the restored caller handle did not resolve"));
		}
	}

	FElysiumMapSnapshot Second;
	B.Freeze(Second);
	// §10's round trip is a digest comparison rather than a semantic diff, and it can be because
	// §8 makes the walk order stable: entities by index, fields sorted by name.
	TestEqual(TEXT("freeze -> rebuild -> apply -> freeze is byte-identical"),
		ElysiumSaveDigest(Second), ElysiumSaveDigest(First));

	// --- Absent entities: what left with the player does not come back -------------------------
	FElysiumMapSnapshot WithAbsent = First;
	WithAbsent.AbsentEntities.Add(Counter->Handle.Index);
	FElysiumEntityWorld C(/*Owner*/ nullptr, /*GameState*/ nullptr);
	C.Load(MakeSaveTestDefs());
	C.ApplySnapshot(WithAbsent);
	TestNull(TEXT("an absent entity is not re-materialised"), C.FindByName(TEXT("counter1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSavePayloadTest, "Elysium.Substrate.SavePayload",
	GElysiumTestFlags)
bool FElysiumSavePayloadTest::RunTest(const FString&)
{
	// A payload with something in every block.
	FElysiumSavePayload Payload;
	Payload.Session.ClockNow = 123.5;
	Payload.Session.Globals.Emplace(TEXT("Story_State"), FElysiumVariant::Int(-4));
	Payload.Session.Globals.Emplace(TEXT("Tut_Jack"), FElysiumVariant::Int(2));
	Payload.Session.Quests.Emplace(TEXT("tutorial"), 3);
	Payload.Session.RngSessionSeed = 4242;
	ElysiumRng::SeedAll(4242);
	ElysiumRng::Stream(EElysiumRngStream::OneOfSet).GetUnsignedInt();
	ElysiumRng::Snapshot(Payload.Session.Rng);

	Payload.Player.Sheet.SetClan(7);
	Payload.Player.Sheet.SetMale(false);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, 3);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Disciplines, /*Celerity*/ 3, 2);
	Payload.Player.Money = 250;
	Payload.Player.Health = 61;
	Payload.Player.MaxHealth = 100;
	Payload.Player.Law.Criminal = 2;
	Payload.Player.ExperienceLog.Add({ TEXT("xp_tutorial"), 0 });

	FElysiumMapSnapshot Snap;
	Snap.MapName = TEXT("sp_tutorial_1");
	Snap.DefCount = 12;
	Snap.QueueNextSerial = 9;
	{
		FElysiumEntityState S;
		S.Index = 4;
		S.ClassName = FName(TEXT("math_counter"));
		S.TargetName = TEXT("counter1");
		S.NextThink = ELYSIUM_NEVER_THINK;
		S.Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(5));
		Snap.Entities.Add(MoveTemp(S));
	}
	{
		FElysiumIOEvent E;
		E.FireTime = 30.0;
		E.Target = TEXT("counter1");
		E.Input = FName(TEXT("Add"));
		E.Param = FElysiumVariant::Int(3);
		E.PythonSrc = TEXT("Tut_Advance()");
		E.Caller = FElysiumEntityHandle(4, 77);
		E.Serial = 8;
		Snap.Queue.Add(MoveTemp(E));
	}
	Payload.Maps.Add(Snap.MapName, MoveTemp(Snap));
	Payload.World.CurrentMap = TEXT("sp_tutorial_1");
	Payload.World.PlayerOrigin = FVector(10, 20, 30);
	Payload.World.PlayerYaw = 45.0f;
	Payload.World.bHasPlacement = true;
	Payload.World.VisitedMaps.Add(TEXT("sp_tutorial_1"));

	// --- Write / read ---------------------------------------------------------------------------
	TArray<uint8> Bytes;
	FString Error;
	if (!TestTrue(TEXT("the payload writes"), ElysiumSave::Write(Payload, Bytes, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the prologue declares this build's schema"),
		ElysiumSave::PeekVersion(Bytes), (int32)FElysiumSaveVersion::Latest);

	FElysiumSavePayload Back;
	if (!TestTrue(TEXT("and reads back"), ElysiumSave::Read(Bytes, Back, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the clock survived"), Back.Session.ClockNow, 123.5);
	TestEqual(TEXT("G survived"), Back.Session.Globals.Num(), 2);
	TestEqual(TEXT("in order, case-sensitively by key"), Back.Session.Globals[0].Key,
		FString(TEXT("Story_State")));
	TestEqual(TEXT("the quest map survived"), Back.Session.Quests.Num(), 1);
	TestEqual(TEXT("the RNG stream states survived"), Back.Session.Rng.Num(), Payload.Session.Rng.Num());
	TestEqual(TEXT("the clan survived"), Back.Player.Sheet.Clan(), 7);
	TestFalse(TEXT("and the sex"), Back.Player.Sheet.IsMale());
	TestEqual(TEXT("an attribute slot survived"),
		Back.Player.Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 3);
	TestEqual(TEXT("and so did a slot in another container"),
		Back.Player.Sheet.GetCurrent(EElysiumTraitContainer::Disciplines, 3), 2);
	TestEqual(TEXT("every container came back at its compiled width"),
		Back.Player.Sheet.Base[(uint8)EElysiumTraitContainer::Attributes].Num(),
		ElysiumSheetSlotCount(EElysiumTraitContainer::Attributes));
	TestEqual(TEXT("money survived"), Back.Player.Money, 250);
	TestEqual(TEXT("the law counters survived"), Back.Player.Law.Criminal, 2);
	TestEqual(TEXT("one map snapshot"), Back.Maps.Num(), 1);
	if (const FElysiumMapSnapshot* Read = Back.Maps.Find(TEXT("sp_tutorial_1")))
	{
		TestEqual(TEXT("its entity record survived"), Read->Entities.Num(), 1);
		if (Read->Entities.Num() == 1 && Read->Entities[0].Fields.Num() == 1)
		{
			TestEqual(TEXT("with its field"), Read->Entities[0].Fields[0].Value.ToInt(), 5);
		}
		TestEqual(TEXT("its queue survived"), Read->Queue.Num(), 1);
		if (Read->Queue.Num() == 1)
		{
			// §6 — a deferred script survives as its SOURCE STRING, which is how ScheduleTask does
			// in the original too.
			TestEqual(TEXT("including the deferred script source"), Read->Queue[0].PythonSrc,
				FString(TEXT("Tut_Advance()")));
			TestEqual(TEXT("the serial is preserved, not re-minted"), (int32)Read->Queue[0].Serial, 8);
			// The epoch is dropped on the way out; re-stamping is the applier's job.
			TestEqual(TEXT("a saved handle keeps its index"), Read->Queue[0].Caller.Index, 4);
			TestEqual(TEXT("and loses its epoch"), (int32)Read->Queue[0].Caller.Epoch, 0);
		}
	}
	TestEqual(TEXT("the world placement survived"), Back.World.PlayerYaw, 45.0f);

	// Determinism: the same state writes the same bytes (§8).
	TArray<uint8> Again;
	TestTrue(TEXT("a second write succeeds"), ElysiumSave::Write(Payload, Again, Error));
	TestEqual(TEXT("two writes of one state are byte-identical"), Again, Bytes);

	// --- The integrity gates -------------------------------------------------------------------
	FElysiumSavePayload Rejected;
	TArray<uint8> Garbage;
	Garbage.AddZeroed(64);
	TestFalse(TEXT("a foreign blob is refused"), ElysiumSave::Read(Garbage, Rejected, Error));
	TestTrue(TEXT("with a readable reason"), Error.Contains(TEXT("Elysium payload")));

	TArray<uint8> Truncated(Bytes.GetData(), 8);
	TestFalse(TEXT("a truncated payload is refused"), ElysiumSave::Read(Truncated, Rejected, Error));

	// A payload stamped below the floor: rejected with a reason, never half-read.
	{
		TArray<uint8> TooOld = Bytes;
		FMemoryWriter Patch(TooOld, /*bIsPersistent*/ true);
		uint32 Magic = ElysiumSaveMagic;
		int32 Version = FElysiumSaveVersion::MinSupported - 1;
		Patch << Magic << Version;
		TestFalse(TEXT("a below-floor schema is refused"), ElysiumSave::Read(TooOld, Rejected, Error));
		TestTrue(TEXT("naming the floor"), Error.Contains(TEXT("floor")));
	}
	{
		TArray<uint8> TooNew = Bytes;
		FMemoryWriter Patch(TooNew, /*bIsPersistent*/ true);
		uint32 Magic = ElysiumSaveMagic;
		int32 Version = FElysiumSaveVersion::Latest + 1;
		Patch << Magic << Version;
		TestFalse(TEXT("a future schema is refused"), ElysiumSave::Read(TooNew, Rejected, Error));
		TestTrue(TEXT("saying so"), Error.Contains(TEXT("newer build")));
	}

	// --- The readable dump ------------------------------------------------------------------
	TArray<FString> Lines;
	ElysiumSave::Describe(Payload, Lines);
	TestTrue(TEXT("the dump names a G flag"),
		Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("session.G.Story_State")); }));
	TestTrue(TEXT("and the map's queued delivery"),
		Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("queue @30.000")); }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSaveSchemaTest, "Elysium.Substrate.SaveSchema",
	GElysiumTestFlags)
bool FElysiumSaveSchemaTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// --- The field flags ARE the enumeration (§4) ----------------------------------------------
	const FElysiumClassDesc* CounterDesc = Reg.Find(FName(TEXT("math_counter")));
	if (!TestNotNull(TEXT("math_counter is registered"), CounterDesc))
	{
		return false;
	}
	const TArray<FName> SaveNames = Reg.SaveFields(*CounterDesc);
	TestTrue(TEXT("the chain walk reaches a leaf field"), SaveNames.Contains(FName(TEXT("startvalue"))));
	TestTrue(TEXT("and a base one"), SaveNames.Contains(FName(TEXT("health"))));
	// Sorted, because §8's digest comparison needs an order that is not a hash map's.
	TArray<FName> Sorted = SaveNames;
	Sorted.Sort(FNameLexicalLess());
	TestEqual(TEXT("the walk is sorted"), SaveNames, Sorted);
	// Derived shadows base, so a name appears once however many tables in the chain carry it.
	const TSet<FName> Unique(SaveNames);
	TestEqual(TEXT("and carries no duplicates"), Unique.Num(), SaveNames.Num());

	// A field registered with neither flag is in neither consumer's list. The player's law counters
	// are the case: their setter is a deliberate no-op and their durable home is the Player block.
	if (const FElysiumClassDesc* PlayerDesc = Reg.Find(ElysiumPlayerClassName()))
	{
		if (const FElysiumFieldAccessor* Law = Reg.FindField(*PlayerDesc, FName(TEXT("criminal_level"))))
		{
			TestFalse(TEXT("a read-only law counter is not save-walked"), Law->bSave);
			TestFalse(TEXT("nor keyable"), Law->bKeyable);
		}
	}

	// --- A field name this build no longer knows is skipped, not fatal (§4) --------------------
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__schema_test__");
	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumMapSnapshot Snapshot;
	Snapshot.MapName = TEXT("__schema_test__");
	Snapshot.DefCount = 1;
	{
		FElysiumEntityState S;
		S.Index = 0;
		S.ClassName = FName(TEXT("math_counter"));
		S.TargetName = TEXT("counter1");
		S.NextThink = ELYSIUM_NEVER_THINK;
		S.Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(11));
		S.Fields.Emplace(FName(TEXT("a_field_from_the_future")), FElysiumVariant::Int(1));
		Snapshot.Entities.Add(MoveTemp(S));
	}

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	TestEqual(TEXT("the record still applied"), World.ApplySnapshot(Snapshot), 1);
	TestEqual(TEXT("the known field landed"),
		SaveTestCounterValue(World.FindByName(TEXT("counter1"))), 11.0f);

	// --- A record whose class changed under the save is skipped, not misapplied ----------------
	FElysiumMapSnapshot Wrong = Snapshot;
	Wrong.Entities[0].ClassName = FName(TEXT("logic_relay"));
	Wrong.Entities[0].Fields.Reset();
	Wrong.Entities[0].Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(99));

	FElysiumEntityDefs Defs2;
	Defs2.MapName = TEXT("__schema_test__");
	FElysiumEntityDef Counter2;
	Counter2.Classname = TEXT("math_counter");
	Counter2.TargetName = TEXT("counter1");
	Defs2.Defs.Add(MoveTemp(Counter2));
	FElysiumEntityWorld Other(/*Owner*/ nullptr, /*GameState*/ nullptr);
	Other.Load(MoveTemp(Defs2));
	TestEqual(TEXT("the mismatched record is skipped"), Other.ApplySnapshot(Wrong), 0);
	TestEqual(TEXT("and nothing was written"),
		SaveTestCounterValue(Other.FindByName(TEXT("counter1"))), 0.0f);

	// --- The owned RNG streams restore their position (§8) -------------------------------------
	ElysiumRng::SeedAll(1234);
	ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	TArray<ElysiumRng::FState> State;
	ElysiumRng::Snapshot(State);
	const int32 Next = ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	ElysiumRng::Restore(State);
	TestEqual(TEXT("a restored stream continues the saved sequence"),
		ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9), Next);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
