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
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumConsole.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumExpr.h"
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumMapActor.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumPlayer.h"
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
// The second half guards the collapse `docs/vtmb/script_api.md` warns about: `Whisper` and
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

	// The shape a `ccmd` attribute-touch produces: a bare command word, no argument. Both field-6
	// uses in the shipped corpus are this (`ccmd.createplayer`, `ccmd.wc_create`), so both ends of
	// the resolution order have to behave — a registered verb runs, an unmodelled name is inert.
	int32 Fired = 0;
	FElysiumCommandBinding Once = FElysiumCommands::Get().Bind(
		TEXT("togglechareditor"), [&Fired](const FElysiumCommandCall&) { ++Fired; });
	Fell.Reset();
	C.Execute(TEXT("togglechareditor"));
	TestEqual(TEXT("a bare registered verb runs"), Fired, 1);
	TestEqual(TEXT("and does not reach Python"), Fell.Num(), 0);

	// `wc_create` is an engine command we do not model: no verb, no alias, no cvar, and not a
	// defined Python name, so the sink refuses it and the console drops it rather than raising.
	Fell.Reset();
	C.Execute(TEXT("wc_create"));
	TestEqual(TEXT("an unmodelled engine command is offered to Python once"), Fell.Num(), 1);
	TestEqual(TEXT("and fires no verb"), Fired, 1);

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
	TestTrue(TEXT("cfg maps under the export root"), Real.Replace(TEXT("\\"), TEXT("/"))
		.EndsWith(TEXT("/cfg/config.cfg")));
	TestTrue(TEXT("vdata is mounted"),
		FElysiumScriptFS::MapToMirror(TEXT("vdata/system/stats.txt"), Real));
	TestTrue(TEXT("vdata maps under the export root"), Real.Replace(TEXT("\\"), TEXT("/"))
		.EndsWith(TEXT("/vdata/system/stats.txt")));
	TestTrue(TEXT("vdata/signs is mounted ahead of vdata"),
		FElysiumScriptFS::MapToMirror(TEXT("vdata/signs/death.txt"), Real));
	TestTrue(TEXT("signs map under the export root"), Real.Replace(TEXT("\\"), TEXT("/"))
		.EndsWith(TEXT("/signs/death.txt")));
	TestTrue(TEXT("python maps onto the exported script mirror"),
		FElysiumScriptFS::MapToMirror(TEXT("python/tutorial/tutorial.py"), Real));
	TestTrue(TEXT("python maps under the export root"), Real.Replace(TEXT("\\"), TEXT("/"))
		.EndsWith(TEXT("/scripts/tutorial/tutorial.py")));
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
		Real.StartsWith(FPaths::ConvertRelativePathToFull(FElysiumContentPaths::VdataDir())));

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
// The sheet's arithmetic (9.4c), content-free: the write gates, the feat evaluator, the
// predependency reader and the XP banking. The rulebook halves that need real `vdata` —
// the trait-effect layer and the experience table — are `Elysium.Content.SheetMath`'s.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSheetMathTest, "Elysium.Substrate.SheetMath", GElysiumTestFlags)
bool FElysiumSheetMathTest::RunTest(const FString&)
{
	using EC = EElysiumTraitContainer;

	// A stand-in rulebook: every slot present (the recompute walks all four containers by index),
	// with the three rows this test actually reads authored the way `stats.txt` authors them.
	FElysiumStatTable Rules;
	for (uint8 i = 0; i < (uint8)EC::Count; ++i)
	{
		FElysiumStatContainer& Container = Rules.Containers[i];
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots((EC)i))
		{
			FElysiumStat Stat;
			Stat.InternalName = Slot.Internal;
			Stat.Index = Slot.Index;
			Stat.Min = 0;
			Stat.Max = 10;
			Container.Stats.Add(MoveTemp(Stat));
		}
	}
	FElysiumStat& Humanity = Rules.Containers[(uint8)EC::Attributes].Stats[ElysiumSlot::Humanity];
	Humanity.Max = 10;
	FElysiumStat& Blood = Rules.Containers[(uint8)EC::Attributes].Stats[ElysiumSlot::BloodPool];
	Blood.Max = 15;
	// `Health`'s authored Max is the NAME of another stat, and its raise is gated on the same pair.
	FElysiumStat& Damage = Rules.Containers[(uint8)EC::Attributes].Stats[ElysiumSlot::Health];
	Damage.MaxExpr = TEXT("Max_Health");
	Damage.Max = 0;
	Damage.IncPredependency.Add(TEXT("Health < Max_Health"));
	FElysiumStat& MaxHealth = Rules.Containers[(uint8)EC::Attributes].Stats[ElysiumSlot::MaxHealth];
	MaxHealth.Max = 99999;

	FElysiumSheet Sheet;
	Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Humanity, 7);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 10);

	// --- AddBase: the base is written RAW, and only the current is clamped --------------------
	// The engine never reads the max here (RE26). The overflow banks in the base, which is why a
	// big gain followed by a small loss does not walk back down from the ceiling.
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Humanity, 99, &Rules);
	TestEqual(TEXT("a gain is written raw, past the ceiling"),
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Humanity), 106);
	TestEqual(TEXT("...while the current reads the authored max"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity), 10);
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Humanity, -99, &Rules);
	TestEqual(TEXT("a loss moves the same raw base"),
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Humanity), 7);
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Humanity, -99, &Rules);
	TestEqual(TEXT("...below the floor as well — a negative delta bypasses the gate"),
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Humanity), -92);
	TestEqual(TEXT("...and only the CURRENT value is clamped up to the authored min"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity), 0);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Humanity, 7);

	// The one gate a gain does face: the stat's own `IncPredependency`.
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 100);   // Health == Max_Health
	Sheet.RecomputeCurrent(&Rules);
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Health, 5, &Rules);
	TestEqual(TEXT("a gain is refused outright when `Health < Max_Health` reads false"),
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Health), 100);
	Sheet.AddBase(EC::Attributes, ElysiumSlot::Health, -5, &Rules);
	TestEqual(TEXT("...and the loss through the same slot is not"),
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Health), 95);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);

	// --- IncBase: the bound and the predependency ----------------------------------------------
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 99);
	TestTrue(TEXT("a dot lands while the gate holds"),
		Sheet.IncBase(EC::Attributes, ElysiumSlot::Health, &Rules));
	TestEqual(TEXT("...and it is one dot"), Sheet.GetBase(EC::Attributes, ElysiumSlot::Health), 100);
	TestFalse(TEXT("at the named bound (Max_Health) the next one is refused"),
		Sheet.IncBase(EC::Attributes, ElysiumSlot::Health, &Rules));
	TestEqual(TEXT("...and nothing moved"), Sheet.GetBase(EC::Attributes, ElysiumSlot::Health), 100);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);

	// --- The predependency reader ---------------------------------------------------------------
	TestTrue(TEXT("`Health < Max_Health` reads true at zero damage"),
		ElysiumSheetRules::EvalPredependency(TEXT("Health < Max_Health"), Sheet));
	TestTrue(TEXT("a literal side reads"),
		ElysiumSheetRules::EvalPredependency(TEXT("BloodPool > 0"), Sheet));
	Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 0);
	Sheet.RecomputeCurrent(&Rules);
	TestFalse(TEXT("...and answers the other way when the sheet does"),
		ElysiumSheetRules::EvalPredependency(TEXT("BloodPool > 0"), Sheet));
	TestTrue(TEXT("`>=` is not read as `>`"),
		ElysiumSheetRules::EvalPredependency(TEXT("BloodPool >= 0"), Sheet));
	TestTrue(TEXT("an expression naming nothing opens the gate rather than closing it"),
		ElysiumSheetRules::EvalPredependency(TEXT("Squid_Rating > 4"), Sheet));
	Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 10);
	Sheet.RecomputeCurrent(&Rules);

	// --- FeatValue -------------------------------------------------------------------------------
	// `Persuasion` = Charisma + Academics, the common two-name shape.
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Strength, 3);
	Sheet.SetBase(EC::Abilities, /*Academics*/ 12, 2);
	Sheet.SetBase(EC::Attributes, /*Charisma*/ 4, 4);
	Sheet.RecomputeCurrent(&Rules);

	FElysiumFeat Persuasion;
	Persuasion.InternalName = TEXT("Persuasion");
	Persuasion.Index = 7;
	Persuasion.MaxValue = 10;
	Persuasion.Bases.Add(FElysiumTraitRef::Parse(TEXT("Charisma")));
	Persuasion.Bases.Add(FElysiumTraitRef::Parse(TEXT("Academics")));
	TestEqual(TEXT("a feat is the sum of its bases"),
		ElysiumFeats::FeatValue(Persuasion, Sheet, nullptr), 6);

	// The per-base modifier, and the floor: the nine attributes read at least 1 however low they
	// sit, and the derived stats do not.
	FElysiumFeat Soak;
	Soak.InternalName = TEXT("Soak_vs_Lethal_Falling");
	Soak.Index = 15;
	Soak.MaxValue = 20;
	Soak.Bases.Add(FElysiumTraitRef::Parse(TEXT("Armor_Rating / 2")));
	Soak.Bases.Add(FElysiumTraitRef::Parse(TEXT("Stamina")));
	Sheet.SetBase(EC::Attributes, /*Armor_Rating*/ 19, 5);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Stamina, 0);
	Sheet.RecomputeCurrent(&Rules);
	TestEqual(TEXT("`Armor_Rating / 2` divides, and Stamina floors at 1"),
		ElysiumFeats::FeatValue(Soak, Sheet, nullptr), 3);

	// The clamp, and the containers a base may NOT come from: a discipline base contributes zero,
	// which is what `FeatValue`'s category test does.
	Persuasion.MaxValue = 4;
	TestEqual(TEXT("the rating clamps to the feat's MaxValue"),
		ElysiumFeats::FeatValue(Persuasion, Sheet, nullptr), 4);
	FElysiumFeat Odd;
	Odd.InternalName = TEXT("Odd");
	Odd.Index = 22;
	Odd.Bases.Add(FElysiumTraitRef::Parse(TEXT("Celerity")));
	Sheet.SetBase(EC::Disciplines, /*Celerity*/ 3, 5);
	Sheet.RecomputeCurrent(&Rules);
	TestEqual(TEXT("a discipline base contributes nothing"),
		ElysiumFeats::FeatValue(Odd, Sheet, nullptr), 0);

	// --- The effect accumulator (RE26) -------------------------------------------------------------
	// The engine's own query: its defaults, its per-operator rules and its finalize order.
	{
		using FQuery = FElysiumSheetEffects::FQuery;
		using FRow = FElysiumSheetEffects::FRow;
		auto Row = [](EElysiumTraitOp Op, int32 Amount, int32 Priority = 0)
		{
			FRow R; R.Op = Op; R.Amount = Amount; R.Priority = Priority; return R;
		};

		FQuery Empty;
		Empty.Value = 5;
		TestEqual(TEXT("an empty query is the identity"), Empty.Finalize(), 5);

		FQuery Adds;
		Adds.Value = 5;
		Adds.Accumulate(Row(EElysiumTraitOp::Add, 2));
		Adds.Accumulate(Row(EElysiumTraitOp::Add, -1));
		TestEqual(TEXT("every additive effect sums"), Adds.Finalize(), 6);

		// `(value + add) * mul / div` — the order is the engine's, so the add lands INSIDE the
		// multiply rather than after it.
		FQuery Scale;
		Scale.Value = 4;
		Scale.Accumulate(Row(EElysiumTraitOp::Add, 2));
		Scale.Accumulate(Row(EElysiumTraitOp::Mul, 3));
		Scale.Accumulate(Row(EElysiumTraitOp::Div, 2));
		TestEqual(TEXT("(4+2)*3/2"), Scale.Finalize(), 9);

		// A single winner per slot: equal priority -> the SMALLER amount, higher priority -> outright.
		FQuery Caps;
		Caps.Value = 10;
		Caps.Accumulate(Row(EElysiumTraitOp::Max, 4));
		Caps.Accumulate(Row(EElysiumTraitOp::Max, 6));
		TestEqual(TEXT("two equal-priority caps: the smaller wins"), Caps.Finalize(), 4);
		FQuery Priority;
		Priority.Value = 10;
		Priority.Accumulate(Row(EElysiumTraitOp::Max, 4));
		Priority.Accumulate(Row(EElysiumTraitOp::Max, 8, /*Priority*/ 1));
		TestEqual(TEXT("a higher-priority cap takes the slot outright"), Priority.Finalize(), 8);

		// `Value` replaces; `%` accumulates `100 - amount`, so 50% reads as 150%.
		FQuery Replaced;
		Replaced.Value = 9;
		Replaced.Accumulate(Row(EElysiumTraitOp::Value, 2));
		Replaced.Accumulate(Row(EElysiumTraitOp::Add, 1));
		TestEqual(TEXT("`Value` replaces the value, the adds still apply"), Replaced.Finalize(), 3);
		FQuery Percent;
		Percent.Value = 10;
		Percent.Accumulate(Row(EElysiumTraitOp::Percent, 50));
		TestEqual(TEXT("`50%` reads as 150% — the engine's own inversion"), Percent.Finalize(), 15);

		// The payload operators never touch the value.
		FQuery Ignored;
		Ignored.Value = 7;
		Ignored.Accumulate(Row(EElysiumTraitOp::Duration, 200));
		Ignored.Accumulate(Row(EElysiumTraitOp::Damage, 3));
		TestEqual(TEXT("Duration/Damage are read elsewhere, not here"), Ignored.Finalize(), 7);
	}

	// --- The XP arithmetic ------------------------------------------------------------------------
	// The bonus applies above 2 XP only — the threshold is on the raw hundredths.
	TestEqual(TEXT("a 2 XP award takes no modifier"), ElysiumXp::WithModifier(201, 5), 201);
	TestEqual(TEXT("a 3 XP award takes it"), ElysiumXp::WithModifier(301, 5), 306);
	TestEqual(TEXT("a negative modifier cannot take an award below 1 XP"),
		ElysiumXp::WithModifier(301, -900), 100);

	{
		float Remainder = 0.f, Lifetime = 0.f;
		TestEqual(TEXT("a 301 award banks 3 points"), ElysiumXp::Bank(301, Remainder, Lifetime), 3);
		TestEqual(TEXT("...and keeps the sub-100 residue"), (int32)Remainder, 1);
		TestEqual(TEXT("...while the lifetime total stays raw"), (int32)Lifetime, 301);
		// The residue is what it is FOR: 100 awards of `N01` fall out as one extra point.
		float R = 0.f, L = 0.f;
		int32 Banked = 0;
		for (int32 i = 0; i < 100; ++i) { Banked += ElysiumXp::Bank(101, R, L); }
		TestEqual(TEXT("100 awards of 1.01 XP bank 101, not 100"), Banked, 101);
		TestEqual(TEXT("...and land back on zero residue"), (int32)R, 0);
	}

	// --- The give-once ledger ---------------------------------------------------------------------
	// Give-once is the ledger, not the trailing `01`: every key is refused a second time.
	{
		FElysiumPlayer Pc;
		TestFalse(TEXT("a fresh ledger holds nothing"), Pc.HasAwarded(TEXT("Tut_Jack")));
		FElysiumXpEntry Entry;
		Entry.Entry = TEXT("Tut_Jack");
		Entry.Amount = 101;
		Pc.ExperienceLog.Add(Entry);
		TestTrue(TEXT("the awarded key is refused"), Pc.HasAwarded(TEXT("Tut_Jack")));
		TestTrue(TEXT("...case-insensitively, as Q_strnicmp compares"), Pc.HasAwarded(TEXT("tut_jack")));
		TestFalse(TEXT("an unrelated key still awards"), Pc.HasAwarded(TEXT("Tut_Mercurio")));
		// The prefix compare is VtMB's own, reproduced rather than fixed — no shipped key pair
		// triggers it, and `Elysium.Content.Rulebook` asserts that premise still holds.
		TestTrue(TEXT("a key EXTENDING an awarded one reads as already given (VtMB's prefix compare)"),
			Pc.HasAwarded(TEXT("Tut_Jack_Extra")));
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

	// The frame bound (`docs/vtmb/source_movement.md` → "Frame timing"): VtMB's `Host_FilterTime` clamps
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
// The mover's arithmetic (4.7) — every number here is one `docs/vtmb/source_movement.md` records
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGameFlowCommandsTest,
	"Elysium.Substrate.GameFlowCommands", GElysiumTestFlags)
bool FElysiumGameFlowCommandsTest::RunTest(const FString&)
{
	TestEqual(TEXT("the theatre replay registers exactly two spellings"),
		static_cast<int32>(UE_ARRAY_COUNT(ElysiumStory::TheatreReplayCommands)), 2);
	TestEqual(TEXT("the canonical theatre replay spelling"),
		FString(ElysiumStory::TheatreReplayCommands[0]), FString(TEXT("elysium.newgame_ttd")));
	TestEqual(TEXT("the compact theatre replay spelling used by QA"),
		FString(ElysiumStory::TheatreReplayCommands[1]), FString(TEXT("newgame_ttd")));
	return true;
}

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
	const FElysiumInputScope Character = MakeScope(TEXT("Character"), Prio::Character, EMode::UIOnly,    true);
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
		const TArray<FElysiumInputScope> Screens = { Sign, Cinematic, Chargen, Dialogue, Character, Menu, Debug };
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
	TestTrue(TEXT("so does the character screen"), ElysiumInput::RevokesDebugCapture(Character));
	TestFalse(TEXT("a sign does not — it is dismissed by a world click"),
		ElysiumInput::RevokesDebugCapture(Sign));
	TestFalse(TEXT("nor does a cutscene"), ElysiumInput::RevokesDebugCapture(Cinematic));
	TestFalse(TEXT("and the debug scope never revokes itself"), ElysiumInput::RevokesDebugCapture(Debug));

	// The priority table is the ordering the whole system rests on; state it once, here.
	TestTrue(TEXT("gameplay is the floor"), Prio::Game < Prio::Sign);
	TestTrue(TEXT("a cutscene outranks a sign"), Prio::Sign < Prio::Cinematic);
	TestTrue(TEXT("chargen outranks a cutscene"), Prio::Cinematic < Prio::Chargen);
	TestTrue(TEXT("a conversation outranks chargen"), Prio::Chargen < Prio::Dialogue);
	TestTrue(TEXT("the character screen outranks a conversation"), Prio::Dialogue < Prio::Character);
	TestTrue(TEXT("a menu outranks the character screen"), Prio::Character < Prio::Menu);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapReadinessTest,
	"Elysium.Substrate.MapReadiness", GElysiumTestFlags)
bool FElysiumMapReadinessTest::RunTest(const FString&)
{
	FString Failure;
	FElysiumMapRuntimePrerequisites Gameplay;

	// Prerequisites are independent observations, not an assumed tick order. Complete the
	// player-facing half first and prove the gate still waits for construction/collision.
	Gameplay.bPossessedPawnReady = true;
	Gameplay.bPlayerBodyReady = true;
	Gameplay.bFinalPlacementReady = true;
	Gameplay.bTickPrerequisitesReady = true;
	TestEqual(TEXT("out-of-order partial completion waits"),
		Gameplay.Evaluate(0.5, Failure), EElysiumMapReadinessResult::Waiting);

	Gameplay.bCollisionReady = true;
	Gameplay.bSpawnTransformReady = true;
	Gameplay.bPlayerEntityReady = true;
	Gameplay.bEntityWorldReady = true;
	TestEqual(TEXT("construction has not completed yet"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Waiting);
	Gameplay.bConstructionComplete = true;
	TestEqual(TEXT("all gameplay prerequisites open the gate in any completion order"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites Backdrop;
	Backdrop.bMenuBackdrop = true;
	Backdrop.bConstructionComplete = true;
	Backdrop.bEntityWorldReady = true;
	Backdrop.bCollisionReady = true;
	TestEqual(TEXT("a backdrop omits every pawn prerequisite"),
		Backdrop.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites CollisionFailure = Gameplay;
	CollisionFailure.bCollisionReady = false;
	CollisionFailure.bCollisionFailed = true;
	TestEqual(TEXT("a collision failure never opens the gate"),
		CollisionFailure.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("collision failure is structured"), Failure.Contains(TEXT("collision")));

	FElysiumMapRuntimePrerequisites Timeout = Gameplay;
	Timeout.bPossessedPawnReady = false;
	Timeout.bFinalPlacementReady = false;
	TestEqual(TEXT("an incomplete map waits before the watchdog"),
		Timeout.Evaluate(FElysiumMapRuntimePrerequisites::WatchdogSeconds - 0.01, Failure),
		EElysiumMapReadinessResult::Waiting);
	TestEqual(TEXT("the watchdog fails closed"),
		Timeout.Evaluate(FElysiumMapRuntimePrerequisites::WatchdogSeconds, Failure),
		EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("watchdog reason names missing prerequisites"),
		Failure.Contains(TEXT("possessed player pawn"))
		&& Failure.Contains(TEXT("final player placement")));

	FElysiumMapRuntimePrerequisites MissingSubstrate = Backdrop;
	MissingSubstrate.bEntityWorldReady = false;
	TestEqual(TEXT("a completed build with no substrate fails immediately"),
		MissingSubstrate.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumActivationLifecycleTest,
	"Elysium.Substrate.ActivationLifecycle", GElysiumTestFlags)
bool FElysiumActivationLifecycleTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__activation_lifecycle__");

	auto AddWire = [](FElysiumEntityDef& Source, const TCHAR* Output,
		const TCHAR* Target, const TCHAR* Input, const TCHAR* Param)
	{
		FElysiumOutputDef Wire;
		Wire.Name = Output;
		Wire.Target = Target;
		Wire.Input = Input;
		Wire.Param = Param;
		Wire.Times = -1;
		Source.Outputs.Add(MoveTemp(Wire));
	};

	FElysiumEntityDef Auto;
	Auto.Classname = TEXT("logic_auto");
	Auto.TargetName = TEXT("auto1");
	AddWire(Auto, TEXT("OnMapLoad"), TEXT("counter1"), TEXT("Add"), TEXT("3"));
	Defs.Defs.Add(MoveTemp(Auto));

	FElysiumEntityDef Timer;
	Timer.Classname = TEXT("logic_timer");
	Timer.TargetName = TEXT("timer1");
	Timer.Keys.Add(TEXT("RefireTime"), TEXT("1"));
	AddWire(Timer, TEXT("OnTimer"), TEXT("counter1"), TEXT("Add"), TEXT("11"));
	Defs.Defs.Add(MoveTemp(Timer));

	FElysiumEntityDef Trigger;
	Trigger.Classname = TEXT("trigger_multiple");
	Trigger.TargetName = TEXT("trigger1");
	Trigger.Keys.Add(TEXT("spawnflags"), TEXT("1")); // ALLOW_CLIENTS
	AddWire(Trigger, TEXT("OnStartTouch"), TEXT("counter1"), TEXT("Add"), TEXT("7"));
	Defs.Defs.Add(MoveTemp(Trigger));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	FElysiumEntity* TriggerEntity = World.FindByName(TEXT("trigger1"));
	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("trigger resolved"), TriggerEntity)
		|| !TestNotNull(TEXT("counter resolved"), CounterEntity))
	{
		return false;
	}

	auto CounterValue = [](const FElysiumEntity* Entity)
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
		return -1.0f;
	};

	// Construction may queue map-entry work, but every gameplay drive and physical ingress is
	// inert until the map actor opens the gate.
	World.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(5), 0.0,
		Player, Player);
	for (int32 i = 0; i < 3; ++i)
	{
		World.RunPlayerThink(10.0 + i);
		World.Tick(10.0 + i);
		World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	}
	TestFalse(TEXT("Load leaves the world dormant"), World.IsActive());
	TestEqual(TEXT("dormant thinks, timers and events do not run"), CounterValue(CounterEntity), 0.0f);
	TestEqual(TEXT("dormant overlap callbacks are forgotten"), World.TouchBegins(), 0);
	TestEqual(TEXT("the queued input remains pending"), World.Queue().Num(), 1);

	World.Activate(0.0);
	World.Activate(99.0); // idempotent: must not move the frozen activation time or replay anything
	TestTrue(TEXT("Activate opens the gate"), World.IsActive());
	TestEqual(TEXT("a second Activate does not change now"), World.NowSeconds(), 0.0);

	// This mirrors the map actor's activation transaction: reconcile final containment, then run
	// player-think and the ordinary think-first/event-queue pass at the unchanged game time.
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RunPlayerThink(0.0);
	World.Tick(0.0);
	TestEqual(TEXT("initial auto, queued input and touch complete in the activation pass"),
		CounterValue(CounterEntity), 15.0f);
	TestEqual(TEXT("activation containment emits exactly one begin"), World.TouchBegins(), 1);

	World.Tick(1.0);
	TestEqual(TEXT("timers start only after activation"), CounterValue(CounterEntity), 26.0f);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, false);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.Tick(1.0);
	TestEqual(TEXT("a genuine exit and re-entry emits a new edge"), CounterValue(CounterEntity), 33.0f);
	TestEqual(TEXT("one exit was recorded"), World.TouchEnds(), 1);
	TestEqual(TEXT("the re-entry is the second begin"), World.TouchBegins(), 2);

	// `elysium.trigger off` is a global exploration gate, broader than ent_pause: overlap ingress,
	// entity automation and queued I/O all hold together. It is intentionally reversible, so map
	// load work queued while off starts only when the owner explicitly re-enables it.
	FElysiumEntityWorld::SetTriggerResolutionEnabled(false);
	World.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(5), 0.0,
		Player, Player);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, false);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RunPlayerThink(10.0);
	World.Tick(10.0);
	TestEqual(TEXT("trigger-off holds entity automation and queued I/O"), CounterValue(CounterEntity), 33.0f);
	TestEqual(TEXT("trigger-off ignores proximity ingress"), World.TouchBegins(), 2);
	TestEqual(TEXT("trigger-off keeps queued work for an explicit resume"), World.Queue().Num(), 1);

	FElysiumEntityWorld::SetTriggerResolutionEnabled(true);
	World.Tick(10.0);
	TestEqual(TEXT("trigger-on resumes the held timer and queued I/O"), CounterValue(CounterEntity), 49.0f);
	return true;
}

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

	// §4 — before activation, the two lifecycle-bearing passes are allowed to poll while held, but
	// their gameplay branches are phase-gated. ActivateRuntime restores both flags to false; the
	// post-move gameplay pass is never needed for readiness. Presentation remains live throughout.
	TestTrue(TEXT("the pre-move pass can poll readiness while held"), Map->PreMoveTickFunction.bTickEvenWhenPaused);
	TestTrue(TEXT("the activation pass can run while held"), Map->PrimaryActorTick.bTickEvenWhenPaused);
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
	World.Activate(0.0);

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
	PlayerWorld.Activate(0.0);

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
// RE29 — CGlobalEntityList::FindEntityByName's exact matching rule. Only a final `*` is special;
// matching is case-insensitive, and a bare star selects every named entity.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEntityNameMatchTest,
	"Elysium.Substrate.EntityNameMatch", GElysiumTestFlags)
bool FElysiumEntityNameMatchTest::RunTest(const FString&)
{
	TestTrue(TEXT("exact names fold case"),
		FElysiumEntityWorld::NameMatches(TEXT("Plus_Closet"), TEXT("plus_closet")));
	TestTrue(TEXT("a trailing star is a prefix match"),
		FElysiumEntityWorld::NameMatches(TEXT("plus_Closet"), TEXT("PLUS_*")));
	TestTrue(TEXT("a bare star matches every named entity"),
		FElysiumEntityWorld::NameMatches(TEXT("anything"), TEXT("*")));
	TestFalse(TEXT("a star away from the end is literal"),
		FElysiumEntityWorld::NameMatches(TEXT("guard_alpha"), TEXT("guard*_alpha")));
	TestFalse(TEXT("an empty pattern matches nothing"),
		FElysiumEntityWorld::NameMatches(TEXT("anything"), FString()));
	TestFalse(TEXT("a nameless entity never matches"),
		FElysiumEntityWorld::NameMatches(FString(), TEXT("*")));
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
	World.Activate(0.0);

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
	Lines.Add(TEXT("glass 1"));
	Lines.Add(TEXT("bumpmap tex/glass_glass_n.png"));
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
	// Source Refract overlay: no albedo, only the converted DUDV normal + authored amount.
	Lines.Add(TEXT("newmtl rain_refract"));
	Lines.Add(TEXT("refract 0.010000"));
	Lines.Add(TEXT("refractmap tex/rain_refract_n.png"));

	TMap<FString, FElysiumMaterialDef> Mats;
	FElysiumObjModel::ParseMtlLines(Lines, Mats);
	TestEqual(TEXT("nine materials parsed"), Mats.Num(), 9);

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
		TestTrue(TEXT("glass semantic parsed"), G->bGlass);
		TestEqual(TEXT("glass normal parsed"), G->Bump, FString(TEXT("tex/glass_glass_n.png")));
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
	// is bimodal (docs/vtmb/reflections.md), so these are the two sides plus the glass exclusion.
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
	if (const FElysiumMaterialDef* R = Mats.Find(TEXT("rain_refract")))
	{
		TestTrue(TEXT("Source Refract semantic parsed"), R->bRefract);
		TestEqual(TEXT("Source Refract amount parsed"), R->RefractAmount, 0.01f, 1e-6f);
		TestEqual(TEXT("Source Refract map parsed"), R->RefractMap,
			FString(TEXT("tex/rain_refract_n.png")));
		TestTrue(TEXT("Source Refract is not a generic blend flag"),
			!R->bBlend && !R->bScissor && !R->bAdditive && !R->bGlass);
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
	World.Activate(0.0);

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
	World.Activate(0.0);

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

	// A skill-only condition: implicit `>=`, wrapped in CalcFeat. The receiver is explicit, because
	// `CalcFeat` is a Character method — a bare call resolves to nothing in either host.
	TestEqual(TEXT("bare skillcheck"), ConditionToPython(TEXT("Seduction 7")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7")));
	// Explicit relop preserved.
	TestEqual(TEXT("skillcheck relop"), ConditionToPython(TEXT("Humanity >= 5")),
		FString(TEXT("pc.CalcFeat(\"Humanity\") >= 5")));
	// Skillcheck joined to a python expr by `&` -> `and`.
	TestEqual(TEXT("skillcheck & expr"), ConditionToPython(TEXT("Seduction 7 & G.Johnny_Dead == 0")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7 and G.Johnny_Dead == 0")));
	// `|` -> `or`.
	TestEqual(TEXT("pipe -> or"), ConditionToPython(TEXT("Persuasion 7 | G.x == 1")),
		FString(TEXT("pc.CalcFeat(\"Persuasion\") >= 7 or G.x == 1")));
	// The `M_`/`F_` prefix is the engine dependency's SEX GATE, not part of the trait name — the
	// corpus authors the same beat twice, once per sex, with different lines (`cal.dlg`).
	TestEqual(TEXT("male-gated skillcheck"), ConditionToPython(TEXT("M_Persuasion 3")),
		FString(TEXT("(pc.IsMale() and pc.CalcFeat(\"Persuasion\") >= 3)")));
	TestEqual(TEXT("female-gated skillcheck"), ConditionToPython(TEXT("F_Seduction 8 & G.x == 0")),
		FString(TEXT("(not pc.IsMale() and pc.CalcFeat(\"Seduction\") >= 8) and G.x == 0")));
	// A trait that merely STARTS with a letter and an underscore is not gated.
	TestEqual(TEXT("an ordinary underscored trait is untouched"),
		ConditionToPython(TEXT("Max_Health 5")),
		FString(TEXT("pc.CalcFeat(\"Max_Health\") >= 5")));
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
	World.Activate(0.0);

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
	// docs/vtmb/sky-ambience.md -> "K1 ... (settled)" / B3.
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
	// The slots the material graph reads (pipeline/unreal/mat_fog.py) and the bake writes
	// (pipeline/unreal/bake_map.py FOG_CPD_*). A colour is a float4, so it owns 0..3.
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
	World.Activate(0.0);

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
	World2.Activate(0.0);
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
// anything else. No RHI, no actors, no `$ELYSIUM_EXPORT_ROOT` — the recording stub is the body.
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
	World.Activate(0.0);
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
		Backdrop.Activate(0.0);
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
// Opening-embrace embodiment: the controller is a real map-epoch entity, and prop_dynamic
// selects the generated v4 skeletal representation only for indexed models.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimatedPropManifestTest,
	"Elysium.Substrate.AnimatedPropManifest", GElysiumTestFlags)
bool FElysiumAnimatedPropManifestTest::RunTest(const FString&)
{
	const FString MinimalNpc = TEXT("\"npcs\":{\"dummy\":{\"glb\":\"dummy.glb\","
		"\"model\":\"models/dummy.mdl\",\"clips\":1,\"split_bones\":[\"Bip01 Spine1\"]}}");
	FString Error;
	FElysiumNpcIndex V3;
	const FString Json3 = FString::Printf(TEXT("{\"manifest_version\":3,%s,\"banks\":{},\"cinematics\":{}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("v3 manifest parses: %s"), *Error), V3.LoadJsonText(Json3, Error));
	TestEqual(TEXT("v3 version retained"), V3.ManifestVersion, 3);
	TestTrue(TEXT("v3 reads animated_props as empty"), V3.AnimatedProps.IsEmpty());
	TestEqual(TEXT("optional split-bone metadata is accepted in a v3 index"),
		V3.Npcs[TEXT("dummy")].SplitRotationBones.Num(), 1);
	TestEqual(TEXT("split-bone name is retained"),
		V3.Npcs[TEXT("dummy")].SplitRotationBones[0], FString(TEXT("Bip01 Spine1")));
	TestNull(TEXT("v3 resolves no animated prop"),
		V3.FindAnimatedProp(TEXT("models/cinematic/cin_wineglass.mdl")));

	FElysiumNpcIndex V4;
	const FString Json4 = FString::Printf(TEXT("{\"manifest_version\":4,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"cin_wineglass\":{\"glb\":\"animated_props/cin_wineglass.glb\","
		"\"model\":\"models/cinematic/cin_wineglass.mdl\",\"bones\":4,"
		"\"split_bones\":[\"glass hinge\"],\"clips\":[\"Idle\",\"Pour\"]}}}"),
		*MinimalNpc);
	Error.Reset();
	TestTrue(FString::Printf(TEXT("v4 manifest parses: %s"), *Error), V4.LoadJsonText(Json4, Error));
	TestEqual(TEXT("v4 version retained"), V4.ManifestVersion, 4);
	const FElysiumAnimatedPropEntry* Glass =
		V4.FindAnimatedProp(TEXT("cinematic\\cin_wineglass.mdl"));
	if (TestNotNull(TEXT("v4 normalizes and resolves animated prop model"), Glass))
	{
		TestEqual(TEXT("v4 retains generated glb"), Glass->Glb,
			FString(TEXT("animated_props/cin_wineglass.glb")));
		TestTrue(TEXT("clip lookup folds case"), Glass->HasClip(TEXT("POUR")));
		TestEqual(TEXT("clip inventory retained"), Glass->Clips.Num(), 2);
		TestEqual(TEXT("animated-prop split-bone metadata is retained"),
			Glass->SplitRotationBones.Num(), 1);
	}

	FElysiumNpcIndex Future;
	Error.Reset();
	const FString Json5 = FString::Printf(TEXT("{\"manifest_version\":5,%s}"), *MinimalNpc);
	TestFalse(TEXT("future manifest is rejected"), Future.LoadJsonText(Json5, Error));
	TestTrue(TEXT("future rejection explains supported versions"), Error.Contains(TEXT("expected 3 or 4")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningEmbodimentTest,
	"Elysium.Substrate.OpeningEmbodiment", GElysiumTestFlags)
bool FElysiumOpeningEmbodimentTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(10.f, 20.f, 30.f);
	Services.PlayerRotation = FRotator(0.f, 35.f, 0.f);
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_wineglass.mdl"), TEXT("cin_wineglass"));

	auto BuildDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__opening_embodiment__");
		FElysiumEntityDef Glass;
		Glass.Classname = TEXT("prop_dynamic");
		Glass.TargetName = TEXT("wineglass");
		Glass.Origin = FVector(100.f, 200.f, 300.f);
		Glass.ModelMesh = TEXT("cin_wineglass");
		Glass.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_wineglass.mdl"));
		Glass.Keys.Add(TEXT("LoopSequence"), TEXT("glass_idle"));
		Defs.Defs.Add(MoveTemp(Glass));
		return Defs;
	};

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(BuildDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("player spawned"), Player))
	{
		return false;
	}
	Player->SetRuntimeOrigin(FVector(40.f, 50.f, 60.f));
	Player->SetRuntimeAngles(FVector(0.f, 70.f, 0.f));
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	Player->Skin = 2;
	Player->Disposition = TEXT("Cinematic");

	const FElysiumEntityHandle ControllerHandle = World.CreatePlayerControllerEntity();
	FElysiumEntity* Controller = World.FindPlayerController();
	if (!TestTrue(TEXT("controller handle is valid"), ControllerHandle.IsSet())
		|| !TestNotNull(TEXT("controller entity exists"), Controller))
	{
		return false;
	}
	TestEqual(TEXT("!playercontroller resolves the relationship entity"),
		World.FindByName(TEXT("!playercontroller")), Controller);
	TestEqual(TEXT("controller creation is idempotent"),
		World.CreatePlayerControllerEntity().Index, ControllerHandle.Index);
	TestTrue(TEXT("controller copies player origin"), Controller->Origin.Equals(Player->Origin));
	TestTrue(TEXT("controller copies player orientation"), Controller->Angles.Equals(Player->Angles));
	TestEqual(TEXT("controller copies player model"), Controller->Model, Player->Model);
	TestTrue(TEXT("controller stands through the NPC skeletal path"),
		Services.Saw(TEXT("BuildNpcVisual tremere_armor_0")));

	Controller->SetRuntimeOrigin(FVector(400.f, 500.f, 600.f));
	Controller->SetRuntimeAngles(FVector(0.f, 135.f, 0.f));
	Controller->SetRuntimeModel(TEXT("models/character/pc/female/toreador_armor_0.mdl"));
	if (FElysiumCombatCharacter* ControllerCharacter = Controller->AsCombatCharacter())
	{
		ControllerCharacter->Skin = 4;
		ControllerCharacter->Disposition = TEXT("Neutral");
	}
	TestTrue(TEXT("controller removal succeeds"), World.RemovePlayerControllerEntity());
	TestNull(TEXT("controller relationship clears"), World.FindPlayerController());
	TestTrue(TEXT("final controller origin transfers to player"),
		Player->Origin.Equals(FVector(400.f, 500.f, 600.f)));
	TestTrue(TEXT("final controller orientation transfers to player"),
		Player->Angles.Equals(FVector(0.f, 135.f, 0.f)));
	TestEqual(TEXT("final controller model transfers to player"), Player->Model,
		FString(TEXT("models/character/pc/female/toreador_armor_0.mdl")));
	TestEqual(TEXT("final controller skin transfers to player"), Player->Skin, 4);
	TestEqual(TEXT("final controller disposition transfers to player"), Player->Disposition,
		FString(TEXT("Neutral")));
	TestFalse(TEXT("removing an absent controller is a no-op"), World.RemovePlayerControllerEntity());

	World.CreatePlayerControllerEntity();
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	FElysiumEntityWorld Restored(nullptr, nullptr, Services.Bundle());
	Restored.Load(BuildDefs());
	Restored.SpawnPlayer();
	Restored.Activate(0.0);
	Restored.ApplySnapshot(Snapshot);
	TestNotNull(TEXT("snapshot rebinds the controller relationship"), Restored.FindPlayerController());
	TestEqual(TEXT("snapshot preserves !playercontroller resolution"),
		Restored.FindByName(TEXT("!playercontroller")), Restored.FindPlayerController());

	TestTrue(TEXT("indexed prop selects the animated visual"),
		Services.Saw(TEXT("BuildAnimatedPropVisual cin_wineglass")));
	TestTrue(TEXT("authored loop starts looping"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_idle loop=1")));
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("SetAnimation")),
		FElysiumVariant::String(TEXT("glass_pour")), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("Skin")), FElysiumVariant::Int(3), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestTrue(TEXT("SetAnimation selects a non-looping cinematic clip"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_pour loop=0")));
	TestTrue(TEXT("Skin reaches skeletal prop material families"),
		Services.Saw(TEXT("ApplyAnimatedPropSkin cin_wineglass family=3")));

	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->SetRuntimeModel(TEXT("models/props/furniture/chair.mdl"));
	}
	TestTrue(TEXT("SetModel to an unindexed model rebuilds static representation"),
		Services.Saw(TEXT("BuildPropVisual chair")));
	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->SetRuntimeModel(TEXT("models/cinematic/cin_wineglass.mdl"));
	}
	TestTrue(TEXT("SetModel back to an indexed model rebuilds skeletal representation"),
		Services.Count(TEXT("BuildAnimatedPropVisual cin_wineglass")) >= 2);

	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("Break")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TArray<TPair<FString, FString>> Debug;
	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->GetDebugState(Debug);
	}
	TestTrue(TEXT("Break leaves the animated prop destroyed"),
		Debug.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Broken") && Row.Value == TEXT("yes");
		}));
	return true;
}

// =====================================================================================// Genesis's recovered exit: the chargen panel teleports the player into `firetrans`, whose
// OnStartTouch forces `boogieout,ChangeNow`. The recording services keep this content-free while
// exercising the real command parser, entity I/O and travel seam end to end.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGenesisExitTest,
	"Elysium.Substrate.GenesisExit", GElysiumTestFlags)
bool FElysiumGenesisExitTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(20.f, 30.f, 40.f);
	Services.PlayerRotation = FRotator(0.f, 75.f, 0.f);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__genesis_exit__");

	FElysiumEntityDef Landmark;
	Landmark.Classname = TEXT("info_landmark");
	Landmark.TargetName = TEXT("newgame");
	Landmark.Origin = FVector::ZeroVector;
	Defs.Defs.Add(MoveTemp(Landmark));

	FElysiumEntityDef Change;
	Change.Classname = TEXT("trigger_changelevel");
	Change.TargetName = TEXT("boogieout_direct");
	Change.Keys.Add(TEXT("map"), TEXT("sp_theatre"));
	Change.Keys.Add(TEXT("landmark"), TEXT("newgame"));
	Defs.Defs.Add(MoveTemp(Change));

	FElysiumEntityDef BoogieoutDef;
	BoogieoutDef.Classname = TEXT("trigger_changelevel");
	BoogieoutDef.TargetName = TEXT("boogieout");
	BoogieoutDef.Keys.Add(TEXT("map"), TEXT("sp_theatre"));
	BoogieoutDef.Keys.Add(TEXT("landmark"), TEXT("newgame"));
	Defs.Defs.Add(MoveTemp(BoogieoutDef));

	FElysiumEntityDef Fire;
	Fire.Classname = TEXT("trigger_multiple");
	Fire.TargetName = TEXT("firetrans");
	Fire.Origin = FVector(400.f, 500.f, 60.f);
	Fire.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnStartTouch");
		Wire.Target = TEXT("boogieout");
		Wire.Input = TEXT("ChangeNow");
		Wire.Times = -1;
		Fire.Outputs.Add(MoveTemp(Wire));
	}
	Defs.Defs.Add(MoveTemp(Fire));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* DirectChange = World.FindByName(TEXT("boogieout_direct"));
	FElysiumEntity* Boogieout = World.FindByName(TEXT("boogieout"));
	FElysiumEntity* Firetrans = World.FindByName(TEXT("firetrans"));
	if (!TestNotNull(TEXT("direct changelevel resolved"), DirectChange)
		|| !TestNotNull(TEXT("boogieout resolved"), Boogieout)
		|| !TestNotNull(TEXT("firetrans resolved"), Firetrans))
	{
		return false;
	}

	// The forced input itself is live — this was the dead wire before ChangeNow registered.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("ChangeNow")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, DirectChange->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("ChangeNow reaches the travel seam"),
		Services.Saw(TEXT("RequestLandmarkTravel sp_theatre@newgame")));
	TestEqual(TEXT("ChangeNow is not counted as an unknown input"), World.UnknownInputs(), 0);

	FElysiumCommands& Commands = FElysiumCommands::Get();
	FElysiumCommandBinding TeleportBinding = Commands.Bind(TEXT("teleport_player"),
		[&World](const FElysiumCommandCall& Call)
		{
			ElysiumCommands::TeleportPlayer(World, Call.Args);
		});
	if (!TestTrue(TEXT("teleport_player test implementation bound"), TeleportBinding.IsValid()))
	{
		return false;
	}

	Services.Calls.Reset();
	TestTrue(TEXT("the named form is a declared command"),
		Commands.Execute(TEXT("teleport_player firetrans")));
	TestTrue(TEXT("the named form moves through the embodiment seam"),
		Services.PlayerLocation.Equals(Firetrans->Origin));
	TestTrue(TEXT("the named form preserves yaw"),
		FMath::IsNearlyEqual((float)Services.PlayerRotation.Yaw, 75.f));

	TestTrue(TEXT("the coordinate form is a declared command"),
		Commands.Execute(TEXT("teleport_player 7 8 9")));
	TestTrue(TEXT("the coordinate form lands at the requested feet origin"),
		Services.PlayerLocation.Equals(FVector(7.f, 8.f, 9.f)));

	const int32 CallsBeforeMissing = Services.Calls.Num();
	AddExpectedError(TEXT("Could not find entity named missing_target"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("an unresolved target is still a recognized command"),
		Commands.Execute(TEXT("teleport_player missing_target")));
	TestEqual(TEXT("an unresolved target does not move the player"),
		Services.Calls.Num(), CallsBeforeMissing);

	// Reproduce the complete close tail: move into firetrans, route the resulting touch, drain the
	// output event, and observe the same travel request as direct ChangeNow.
	Services.Calls.Reset();
	Commands.Execute(TEXT("teleport_player firetrans"));
	const int32 TouchBeginsBefore = World.TouchBegins();
	World.RouteBrushTouch(Firetrans->Handle, PlayerHandle, /*bBegin*/ true);
	World.RouteBrushTouch(Firetrans->Handle, PlayerHandle, /*bBegin*/ true);
	TestEqual(TEXT("movement and teleport reconciliation collapse to one begin edge"),
		World.TouchBegins(), TouchBeginsBefore + 1);
	for (int32 i = 0; i < 3; ++i)
	{
		World.Tick(1.0 + i);
	}
	TestTrue(TEXT("teleport -> OnStartTouch -> ChangeNow reaches travel"),
		Services.Saw(TEXT("RequestLandmarkTravel sp_theatre@newgame")));
	TestEqual(TEXT("the recovered chain delivers no unknown input"), World.UnknownInputs(), 0);
	const int32 TouchEndsBefore = World.TouchEnds();
	World.RouteBrushTouch(Firetrans->Handle, PlayerHandle, /*bBegin*/ false);
	World.RouteBrushTouch(Firetrans->Handle, PlayerHandle, /*bBegin*/ false);
	TestEqual(TEXT("duplicate reconciliation also collapses to one end edge"),
		World.TouchEnds(), TouchEndsBefore + 1);

	Commands.Unbind(TeleportBinding);
	return true;
}

// =====================================================================================
// Engine integration for the one part GenesisExit cannot model on a bare entity world:
// SetActorLocation(..., TeleportPhysics) must make UE recompute the real player hull's overlaps
// and synchronously deliver BeginOverlap to the real runtime convex trigger component.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEngineTeleportOverlapTest,
	"Elysium.Substrate.EngineTeleportOverlap", GElysiumTestFlags)
bool FElysiumEngineTeleportOverlapTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	if (!TestNotNull(TEXT("transient game world exists"), World))
	{
		return false;
	}

	APlayerController* PC = World->SpawnActor<APlayerController>();
	AElysiumPawn* Pawn = World->SpawnActor<AElysiumPawn>(
		FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player controller spawned"), PC)
		|| !TestNotNull(TEXT("faithful player hull spawned"), Pawn))
	{
		return false;
	}
	PC->Possess(Pawn);
	TestTrue(TEXT("transient world exposes the possessed pawn"),
		World->GetFirstPlayerController() == PC && PC->GetPawn() == Pawn);

	AActor* TriggerOwner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("trigger owner spawned"), TriggerOwner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(TriggerOwner, TEXT("Root"));
	TriggerOwner->SetRootComponent(Root);
	Root->RegisterComponent();
	TriggerOwner->AddInstanceComponent(Root);

	// The destination is the entity's feet-origin, matching `teleport_player firetrans`. The convex
	// encloses the lifted standing hull but is far enough from the start that registration itself
	// cannot produce the notification under test.
	const FVector FeetDestination(500.f, 0.f, 0.f);
	FElysiumConvexHull Hull;
	for (const float X : { -200.f, 200.f })
	{
		for (const float Y : { -200.f, 200.f })
		{
			for (const float Z : { -100.f, 220.f })
			{
				Hull.Vertices.Emplace(X, Y, Z);
			}
		}
	}
	UElysiumBrushComponent* Trigger =
		NewObject<UElysiumBrushComponent>(TriggerOwner, TEXT("Firetrans"));
	Trigger->InitBrush(FElysiumEntityHandle::Invalid(), { Hull }, EElysiumBrushSolidity::Trigger);
	Trigger->SetupAttachment(Root);
	Trigger->SetRelativeLocation(FeetDestination);
	Trigger->RegisterComponent();
	TriggerOwner->AddInstanceComponent(Trigger);

	UElysiumOverlapTestProbe* Probe = NewObject<UElysiumOverlapTestProbe>(World);
	Trigger->OnComponentBeginOverlap.AddDynamic(
		Probe, &UElysiumOverlapTestProbe::HandleBeginOverlap);
	TestEqual(TEXT("fixture starts outside firetrans"), Probe->BeginCount, 0);
	TestFalse(TEXT("fixture starts with no player overlap"), Trigger->IsOverlappingActor(Pawn));

	// Deferred construction avoids loading a map; TeleportPlayer itself needs only this actor's
	// world and its first player controller, so this calls the exact production wrapper.
	AElysiumMapActor* MapActor = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("production map actor wrapper spawned deferred"), MapActor))
	{
		return false;
	}
	MapActor->TeleportPlayer(FeetDestination, 37.f);

	TestEqual(TEXT("engine teleport synchronously emits one begin overlap"), Probe->BeginCount, 1);
	TestTrue(TEXT("begin overlap identifies the player pawn"), Probe->LastOther == Pawn);
	TestTrue(TEXT("player hull is registered inside firetrans after teleport"),
		Trigger->IsOverlappingActor(Pawn));
	TestTrue(TEXT("production wrapper lifts the feet-origin by the hull half-height"),
		Pawn->GetActorLocation().Equals(
			FeetDestination + FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f)));
	TestTrue(TEXT("production wrapper applies the requested yaw"),
		FMath::IsNearlyEqual((float)PC->GetControlRotation().Yaw, 37.f));

	MapActor->Destroy();
	return true;
}

// =====================================================================================
// The theatre detour is a pure decision at the travel funnel: only the authored genesis→theatre
// destination is rewritten, and a rewrite is a direct tutorial-landmark entry.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStorySkipTest,
	"Elysium.Substrate.StorySkip", GElysiumTestFlags)
bool FElysiumStorySkipTest::RunTest(const FString&)
{
	{
		FString Map = ElysiumStory::TheatreMap;
		FString Landmark = TEXT("newgame");
		FVector Offset(10.f, 20.f, 30.f);
		bool bHasYaw = true;
		TestTrue(TEXT("the enabled skip rewrites the theatre leg"),
			ElysiumStory::ResolveIntroSkip(true, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("the rewrite selects the tutorial map"), Map,
			FString(ElysiumStory::TutorialMap));
		TestEqual(TEXT("the rewrite selects the tutorial landmark"), Landmark,
			FString(ElysiumStory::TutorialLandmark));
		TestTrue(TEXT("the source-landmark offset is dropped"), Offset.IsNearlyZero());
		TestFalse(TEXT("the source yaw is dropped"), bHasYaw);
	}

	{
		FString Map = ElysiumStory::TheatreMap;
		FString Landmark = TEXT("newgame");
		FVector Offset(1.f, 2.f, 3.f);
		bool bHasYaw = true;
		TestFalse(TEXT("a disabled skip leaves theatre authored"),
			ElysiumStory::ResolveIntroSkip(false, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("theatre remains the destination"), Map,
			FString(ElysiumStory::TheatreMap));
		TestEqual(TEXT("the authored landmark remains"), Landmark, FString(TEXT("newgame")));
		TestTrue(TEXT("the authored offset remains"), Offset.Equals(FVector(1.f, 2.f, 3.f)));
		TestTrue(TEXT("the authored yaw remains"), bHasYaw);
	}

	{
		FString Map = TEXT("sm_pawnshop_1");
		FString Landmark = TEXT("newgame");
		FVector Offset(4.f, 5.f, 6.f);
		bool bHasYaw = true;
		TestFalse(TEXT("the skip does not rewrite another destination"),
			ElysiumStory::ResolveIntroSkip(true, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("another map remains untouched"), Map, FString(TEXT("sm_pawnshop_1")));
		TestTrue(TEXT("another destination keeps its placement"),
			Offset.Equals(FVector(4.f, 5.f, 6.f)) && bHasYaw);
	}

	return true;
}

// =====================================================================================
// FElysiumWorldServices (11.2) — the substrate's outbound seam. Runs the shape of the
// tutorial's own logic_auto chain end to end against the recording stub: no RHI, no actors,
// no `$ELYSIUM_EXPORT_ROOT`. sp_tutorial_1's five logic_autos fire OnMapLoad at an NPC (WillTalk), a
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
		World.Activate(0.0);

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
			Rec.Saw(TEXT("Submit ambient/tutorial/hum.wav owner=entity:")));
		if (FElysiumEntity* Jack = World.FindByName(TEXT("jack")))
		{
			World.EnqueueInput(TEXT("!self"), FName(TEXT("PlayDialogFile")),
				FElysiumVariant::String(TEXT("sound/character/dlg/jack/direct_line")),
				0.0, FElysiumEntityHandle::Invalid(), Jack->Handle);
			World.Tick(0.3);
			TestTrue(TEXT("PlayDialogFile enters the owned line-service request path"),
				Rec.Saw(TEXT("Submit character/dlg/jack/direct_line.mp3 owner=direct:")));
		}
		World.EnqueueInput(TEXT("!self"), FName(TEXT("Whisper")),
			FElysiumVariant::String(TEXT("Crying")), 0.0,
			FElysiumEntityHandle::Invalid(), World.PlayerHandle());
		World.Tick(0.4);
		TestTrue(TEXT("player Whisper enters the typed request path"),
			Rec.Saw(TEXT("Submit whispers/crying")));
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
		Bare.Activate(0.0);

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
// Brush movers — visible body attachment, PASSABLE doors, use_override, prop_button,
// and the recovered func_elevator state machine exercised as one authored-style chain.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorElevatorTest,
	"Elysium.Substrate.DoorElevator", GElysiumTestFlags)
bool FElysiumDoorElevatorTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("mover owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner->AddInstanceComponent(Root);

	auto BoxHull = []()
	{
		FElysiumConvexHull Hull;
		for (float X : { -20.f, 20.f })
		{
			for (float Y : { -20.f, 20.f })
			{
				for (float Z : { -20.f, 20.f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};
	auto Wire = [](FElysiumEntityDef& From, const TCHAR* Output, const TCHAR* Target,
		const TCHAR* Input, const TCHAR* Param = TEXT(""))
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = Target;
		W.Input = Input;
		W.Param = Param;
		W.Times = -1;
		From.Outputs.Add(MoveTemp(W));
	};
	auto CounterValue = [](const FElysiumEntity* Entity)
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

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_elevator__");
	auto AddCounter = [&Defs](const TCHAR* Name)
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Counter));
	};
	for (const TCHAR* Name : { TEXT("pressed"), TEXT("locked_press"), TEXT("state2"),
		TEXT("move_start"), TEXT("reach_any"), TEXT("reach1"), TEXT("reach2"),
		TEXT("invalid_open"), TEXT("case_default") })
	{
		AddCounter(Name);
	}

	// The tutorial resets this counter to zero on every elevator arrival and forwards OutValue to
	// logic_case_toggle.InValue. Retail InValue is value matching (not the added delta input), so
	// zero misses Case01/02 and terminates at OnDefault instead of recursively requesting a floor.
	FElysiumEntityDef TutorialCounter;
	TutorialCounter.Classname = TEXT("math_counter");
	TutorialCounter.TargetName = TEXT("counter_elev");
	Wire(TutorialCounter, TEXT("OutValue"), TEXT("case_elev"), TEXT("InValue"));
	Defs.Defs.Add(MoveTemp(TutorialCounter));

	FElysiumEntityDef TutorialCase;
	TutorialCase.Classname = TEXT("logic_case_toggle");
	TutorialCase.TargetName = TEXT("case_elev");
	TutorialCase.Keys.Add(TEXT("InitialCase"), TEXT("1"));
	TutorialCase.Keys.Add(TEXT("Case01"), TEXT("1"));
	TutorialCase.Keys.Add(TEXT("Case02"), TEXT("2"));
	Wire(TutorialCase, TEXT("OnCase01"), TEXT("lift"), TEXT("GotoFloor"), TEXT("1"));
	Wire(TutorialCase, TEXT("OnCase02"), TEXT("lift"), TEXT("GotoFloor"), TEXT("2"));
	Wire(TutorialCase, TEXT("OnDefault"), TEXT("case_default"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TutorialCase));

	FElysiumEntityDef Button;
	Button.Classname = TEXT("prop_button");
	Button.TargetName = TEXT("elev_button");
	Button.ModelMesh = TEXT("elevator_button");
	Button.Keys.Add(TEXT("model"), TEXT("models/elevator_button.mdl"));
	Button.Keys.Add(TEXT("parentname"), TEXT("lift"));
	Button.Keys.Add(TEXT("max_states"), TEXT("2"));
	Button.Keys.Add(TEXT("current_state"), TEXT("0"));
	Button.Keys.Add(TEXT("use_icon"), TEXT("7"));
	Button.Keys.Add(TEXT("locked_icon"), TEXT("8"));
	Wire(Button, TEXT("OnPressed"), TEXT("pressed"), TEXT("Add"), TEXT("1"));
	Wire(Button, TEXT("OnPressedLocked"), TEXT("locked_press"), TEXT("Add"), TEXT("1"));
	Wire(Button, TEXT("OnSetState2"), TEXT("state2"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Button));

	FElysiumEntityDef Elevator;
	Elevator.Classname = TEXT("func_elevator");
	Elevator.TargetName = TEXT("lift");
	Elevator.Model = 1;
	Elevator.Hulls.Add(BoxHull());
	Elevator.BrushMesh = TEXT("brush_1");
	Elevator.ElevatorFloors = { 0.f, 254.f };
	Elevator.Keys.Add(TEXT("model"), TEXT("*1"));
	Elevator.Keys.Add(TEXT("speed"), TEXT("100"));       // one second per 100 Source inches
	Elevator.Keys.Add(TEXT("numfloors"), TEXT("2"));
	Wire(Elevator, TEXT("OnMoveStart"), TEXT("move_start"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloorAny"), TEXT("reach_any"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloorAny"), TEXT("counter_elev"), TEXT("SetValue"), TEXT("0"));
	Wire(Elevator, TEXT("OnReachFloor1"), TEXT("reach1"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloor2"), TEXT("reach2"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Elevator));

	FElysiumEntityDef Door;
	Door.Classname = TEXT("func_door");
	Door.TargetName = TEXT("lift_door");
	Door.Origin = FVector(50.f, 0.f, 0.f);
	Door.Model = 2;
	Door.Hulls.Add(BoxHull());
	Door.BrushMesh = TEXT("brush_2");
	Door.Keys.Add(TEXT("model"), TEXT("*2"));
	Door.Keys.Add(TEXT("parentname"), TEXT("lift"));
	Door.Keys.Add(TEXT("spawnflags"), TEXT("8"));        // PASSABLE
	Door.Keys.Add(TEXT("use_override"), TEXT("elev_button"));
	Defs.Defs.Add(MoveTemp(Door));

	FElysiumEntityDef InvalidDoor;
	InvalidDoor.Classname = TEXT("func_door");
	InvalidDoor.TargetName = TEXT("invalid_override");
	InvalidDoor.Origin = FVector(100.f, 0.f, 0.f);
	InvalidDoor.Model = 3;
	InvalidDoor.Hulls.Add(BoxHull());
	InvalidDoor.BrushMesh = TEXT("brush_3");
	InvalidDoor.Keys.Add(TEXT("model"), TEXT("*3"));
	InvalidDoor.Keys.Add(TEXT("use_override"), TEXT("does_not_exist"));
	Wire(InvalidDoor, TEXT("OnOpen"), TEXT("invalid_open"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(InvalidDoor));

	FElysiumEntityDefs RestoreDefs = Defs;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* LiveButton = World.FindByName(TEXT("elev_button"));
	FElysiumEntity* LiveElevator = World.FindByName(TEXT("lift"));
	FElysiumEntity* LiveDoor = World.FindByName(TEXT("lift_door"));
	FElysiumEntity* LiveInvalid = World.FindByName(TEXT("invalid_override"));
	if (!TestNotNull(TEXT("prop_button resolved"), LiveButton)
		|| !TestNotNull(TEXT("elevator resolved"), LiveElevator)
		|| !TestNotNull(TEXT("door resolved"), LiveDoor)
		|| !TestNotNull(TEXT("invalid override door resolved"), LiveInvalid))
	{
		return false;
	}

	TestEqual(TEXT("every annotated brush requested one visual"),
		Services.Count(TEXT("BuildBrushVisual")), 3);
	TestTrue(TEXT("door visual is attached at identity to collision"),
		LiveDoor->Body && LiveDoor->Body->GetVisual()
		&& LiveDoor->Body->GetVisual()->GetAttachParent() == LiveDoor->Body
		&& LiveDoor->Body->GetVisual()->GetRelativeTransform().Equals(FTransform::Identity));
	TestEqual(TEXT("PASSABLE door keeps its dedicated traceable profile"),
		LiveDoor->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));
	TestTrue(TEXT("parentname attaches the door to the elevator body"),
		LiveDoor->Body->GetAttachParent() == LiveElevator->Body);
	TestTrue(TEXT("parentname attaches the cabin button visual to the elevator body"),
		LiveButton->GetAttachBody()
		&& LiveButton->GetAttachBody()->GetAttachParent() == LiveElevator->Body);
	TestTrue(TEXT("attachment preserves the exported world pose"),
		LiveDoor->Body->GetComponentLocation().Equals(FVector(50.f, 0.f, 0.f), 0.1f));

	World.AcceptInput(TEXT("lift_door"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("use_override delegates one normal Use"), CounterValue(World.FindByName(TEXT("pressed"))), 1.f);
	TestEqual(TEXT("button cycles skin/state after OnPressed"), CounterValue(World.FindByName(TEXT("state2"))), 1.f);
	World.AcceptInput(TEXT("elev_button"), FName(TEXT("Lock")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	World.AcceptInput(TEXT("lift_door"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("locked override emits only OnPressedLocked"),
		CounterValue(World.FindByName(TEXT("locked_press"))), 1.f);
	TestEqual(TEXT("locked use does not emit OnPressed again"),
		CounterValue(World.FindByName(TEXT("pressed"))), 1.f);
	TestEqual(TEXT("locked button exposes locked icon"), LiveButton->GetUseIcon(), 8);

	const FVector InvalidStart = LiveInvalid->Body->GetRelativeLocation();
	World.AcceptInput(TEXT("invalid_override"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveInvalid->Handle, LiveInvalid->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("missing use_override target fails closed"),
		LiveInvalid->Body->GetRelativeLocation().Equals(InvalidStart));
	TestEqual(TEXT("missing use_override never opens the door"),
		CounterValue(World.FindByName(TEXT("invalid_open"))), 0.f);

	// Retail completes an already-current GotoFloor synchronously: Any first, then the floor row.
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(1),
		LiveButton->Handle, LiveButton->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("current-floor request emits the generic arrival immediately"),
		CounterValue(World.FindByName(TEXT("reach_any"))), 1.f);
	TestEqual(TEXT("current-floor request emits floor-one arrival immediately"),
		CounterValue(World.FindByName(TEXT("reach1"))), 1.f);
	TestEqual(TEXT("current-floor request does not start movement"),
		CounterValue(World.FindByName(TEXT("move_start"))), 0.f);
	TestEqual(TEXT("tutorial arrival reset misses configured cases and terminates at OnDefault"),
		CounterValue(World.FindByName(TEXT("case_default"))), 1.f);

	AddExpectedError(TEXT("invalid floor"), EAutomationExpectedErrorFlags::Contains, 1);
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(9),
		LiveButton->Handle, LiveButton->Handle);
	TestEqual(TEXT("invalid floor does not start movement"),
		CounterValue(World.FindByName(TEXT("move_start"))), 0.f);

	World.AcceptInput(TEXT("counter_elev"), FName(TEXT("SetValue")), FElysiumVariant::Int(2),
		LiveButton->Handle, LiveButton->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("tutorial SetValue(2) matches Case02 and starts the requested floor"),
		CounterValue(World.FindByName(TEXT("move_start"))), 1.f);
	World.Tick(0.5);
	TestTrue(TEXT("elevator advances at constant speed"),
		FMath::IsNearlyEqual(LiveElevator->Body->GetRelativeLocation().Z, 127.f, 1.f));
	TestTrue(TEXT("parented door stays aligned while elevator moves"),
		FMath::IsNearlyEqual(LiveDoor->Body->GetComponentLocation().Z, 127.f, 1.f));
	TestTrue(TEXT("parented cabin button stays aligned while elevator moves"),
		LiveButton->GetAttachBody()
		&& FMath::IsNearlyEqual(LiveButton->GetAttachBody()->GetComponentLocation().Z, 127.f, 1.f));
	FElysiumMapSnapshot MovingSnapshot;
	World.Freeze(MovingSnapshot);
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(1),
		LiveButton->Handle, LiveButton->Handle); // ignored while moving
	World.Tick(1.1);
	TestTrue(TEXT("elevator reaches the requested absolute floor"),
		FMath::IsNearlyEqual(LiveElevator->Body->GetRelativeLocation().Z, 254.f, 0.1f));
	TestEqual(TEXT("arrival fires generic output once for the completed move"),
		CounterValue(World.FindByName(TEXT("reach_any"))), 2.f);
	TestEqual(TEXT("arrival fires floor-two output"),
		CounterValue(World.FindByName(TEXT("reach2"))), 1.f);
	TestEqual(TEXT("floor-two reset returns through the non-recursive default path"),
		CounterValue(World.FindByName(TEXT("case_default"))), 2.f);
	TestEqual(TEXT("mid-move retarget was ignored"),
		CounterValue(World.FindByName(TEXT("move_start"))), 1.f);

	// The mover save policy resolves an in-flight request at its destination, resting.
	AActor* RestoreOwner = EngineWorld->SpawnActor<AActor>();
	USceneComponent* RestoreRoot = NewObject<USceneComponent>(RestoreOwner, TEXT("RestoreRoot"));
	RestoreOwner->SetRootComponent(RestoreRoot);
	RestoreRoot->RegisterComponent();
	RestoreOwner->AddInstanceComponent(RestoreRoot);
	FElysiumRecordingServices RestoreServices;
	FElysiumEntityWorld Restored(RestoreOwner, nullptr, RestoreServices.Bundle());
	Restored.Load(MoveTemp(RestoreDefs));
	Restored.ApplySnapshot(MovingSnapshot);
	Restored.Activate(0.5);
	Restored.Tick(0.5);
	FElysiumEntity* RestoredElevator = Restored.FindByName(TEXT("lift"));
	TestTrue(TEXT("restored in-flight elevator seats at its requested destination"),
		RestoredElevator && RestoredElevator->Body
		&& FMath::IsNearlyEqual(RestoredElevator->Body->GetRelativeLocation().Z, 254.f, 0.1f));
	if (RestoredElevator)
	{
		TArray<TPair<FString, FString>> State;
		RestoredElevator->GetDebugState(State);
		const TPair<FString, FString>* Current = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Current floor"); });
		const TPair<FString, FString>* Target = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Target floor"); });
		TestTrue(TEXT("restored elevator is resting at floor two"),
			Current && Current->Value == TEXT("2")
			&& Target && Target->Value == TEXT("(none)"));
	}

	World.AcceptInput(TEXT("lift_door"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	TestEqual(TEXT("hidden mover drops collision"), LiveDoor->Body->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	TestFalse(TEXT("hidden mover hides its attached visual"), LiveDoor->Body->GetVisual()->IsVisible());
	World.AcceptInput(TEXT("lift_door"), FName(TEXT("ScriptUnhide")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	TestEqual(TEXT("unhidden PASSABLE mover restores its profile"),
		LiveDoor->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));
	TestTrue(TEXT("unhidden mover restores its attached visual"), LiveDoor->Body->GetVisual()->IsVisible());

	return true;
}

// =====================================================================================
// The camera (11.7) — the weight driver and the scripted-shot channel.
//
// VtMB ships one camera with a blend weight, not two cameras, and the whole first<->third
// transition is that weight ramping at a fixed rate with a priority order over three latches
// (`docs/vtmb/camera-view-modes.md` §3). That is a pure function of time and flags, so it is asserted here
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

	// --- the body is hidden only at true first-person zero, and a scripted shot reveals it ---
	{
		FElysiumCameraCvars Cvars;
		Cvars.IdealDist = 100.0f;
		Cvars.FadeStart = 80.0f;
		Cvars.FadeEnd = 20.0f;
		FElysiumCameraWeights W;
		TestEqual(TEXT("true first person hides the player body"),
			ElysiumCam::SolveModelAlpha(FVector(100.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
		W.Scripted = 0.5f;
		TestEqual(TEXT("a half-weight scripted camera reveals the body through the same ramp"),
			ElysiumCam::SolveModelAlpha(FVector::ZeroVector, W, Cvars), 0.5f);
		W.Scripted = 1.0f;
		TestEqual(TEXT("a full scripted camera makes the body fully visible"),
			ElysiumCam::SolveModelAlpha(FVector::ZeroVector, W, Cvars), 1.0f);
		W.Scripted = 0.0f;
		W.Third = 1.0f;
		TestEqual(TEXT("ordinary third person still uses the recovered distance band"),
			ElysiumCam::SolveModelAlpha(FVector(80.0f, 0.0f, 0.0f), W, Cvars), 1.0f);
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

		// Every name `docs/vtmb/camera-view-modes.md` §1 lists is declared, so none of them can fall through to
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
// camera_track — paired value streams and the recovered keyframe timing surface (12.1).
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraTrackTest, "Elysium.Substrate.CameraTrack", GElysiumTestFlags)
bool FElysiumCameraTrackTest::RunTest(const FString&)
{
	using namespace ElysiumCameraTrack;

	FPoint A;
	A.Position = FVector::ZeroVector;
	A.bTimeControl = true;
	A.MoveTime = 2.0f;
	A.Pause = 1.0f;
	A.Roll = 170.0f;
	A.FocalLength = 50.0f;
	FPoint B = A;
	B.Position = FVector(100.0f, 0.0f, 0.0f);
	B.Pause = 0.5f;
	B.Roll = -170.0f;
	B.FocalLength = 25.0f;

	FPath Timed;
	Timed.Points = { A, B };
	Timed.RebuildTimes();
	TestEqual(TEXT("the root pause is not a pre-roll dwell"), Timed.Departures[0], 0.0f);
	TestEqual(TEXT("TimeControl uses authored MoveTime"), Timed.Arrivals[1], 2.0f);
	TestEqual(TEXT("the destination pause extends completion"), Timed.EndTime, 2.5f);

	FSample Sample;
	TestTrue(TEXT("a path starts moving immediately"), Timed.Sample(0.5f, Sample));
	TestFalse(TEXT("the root is not held for its authored pause"), Sample.Position.Equals(A.Position, 0.01f));
	TestTrue(TEXT("the midpoint samples between endpoints"), Timed.Sample(1.0f, Sample));
	TestTrue(TEXT("linear rate defaults put the two-point Catmull midpoint at 50"),
		FMath::IsNearlyEqual(Sample.Position.X, 50.0f, 0.01f));
	TestTrue(TEXT("roll takes the short 20-degree path across 180"),
		FMath::Abs(FMath::Abs(Sample.Roll) - 180.0f) < 0.1f);
	TestTrue(TEXT("a positive lens becomes a horizontal FOV"), Sample.FieldOfView > 0.0f);
	TestTrue(TEXT("the path is complete after the destination pause"), Timed.Sample(2.5f, Sample) && Sample.bFinished);

	FPoint SpeedA;
	SpeedA.Position = FVector::ZeroVector;
	SpeedA.MoveSpeed = 50.0f;
	FPoint SpeedB = SpeedA;
	SpeedB.Position = FVector(254.0f, 0.0f, 0.0f);
	TestTrue(TEXT("speed timing converts Source units per second exactly once"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 2.0f, 0.001f));
	TestTrue(TEXT("RateOut/RateIn use the recovered endpoint-slope cubic"),
		FMath::IsNearlyEqual(EaseRate(0.25f, 2.0f, 1.0f), 0.390625f, KINDA_SMALL_NUMBER));
	SpeedB.MoveSpeed = 150.0f;
	TestTrue(TEXT("a smooth destination averages endpoint speeds"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 1.0f, 0.001f));
	SpeedB.bCorner = true;
	TestTrue(TEXT("a corner destination uses the source speed"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 2.0f, 0.001f));
	TestTrue(TEXT("50mm on a 36mm horizontal gate is about 39.6 degrees"),
		FMath::IsNearlyEqual(FocalLengthToHorizontalFov(50.0f), 39.5978f, 0.01f));

	FPath Cut;
	A.Pause = 0.0f;
	A.MoveTime = 0.0f;
	B.Pause = 0.0f;
	Cut.Points = { A, B };
	Cut.RebuildTimes();
	TestTrue(TEXT("a zero-duration chain reaches its final key immediately"),
		Cut.Sample(0.0f, Sample) && Sample.bFinished && Sample.Position.Equals(B.Position, 0.01f));

	FPath TheatreCut;
	A.MoveTime = 0.03f;
	B.Position = FVector(900.0f, -400.0f, 200.0f);
	TheatreCut.Points = { A, B };
	TheatreCut.RebuildTimes();
	TestFalse(TEXT("a positive authored MoveTime remains movement rather than an invented cut"),
		IsHardCut(A));
	TestTrue(TEXT("the short move keeps its authored duration in the path clock"),
		FMath::IsNearlyEqual(TheatreCut.EndTime, 0.03f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("the short move samples between the authored endpoints"),
		TheatreCut.Sample(0.015f, Sample)
			&& !Sample.Position.Equals(A.Position, 0.01f)
			&& !Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("the short move reaches its destination at the arrival boundary"),
		TheatreCut.Sample(0.03f, Sample) && Sample.Position.Equals(B.Position, 0.01f));

	FPath ZeroCut;
	A.MoveTime = 0.0f;
	ZeroCut.Points = { A, B };
	ZeroCut.RebuildTimes();
	TestTrue(TEXT("only an authored zero-time transition is classified as a hard cut"),
		IsHardCut(A));
	TestTrue(TEXT("a zero-time edit switches to the destination immediately"),
		ZeroCut.Sample(0.0f, Sample) && Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("forward playback reports the zero-time edit exactly once"),
		CrossesHardCut(ZeroCut, -KINDA_SMALL_NUMBER, 0.0f));
	TestFalse(TEXT("a sampled hard cut is not reported again on the next frame"),
		CrossesHardCut(ZeroCut, 0.0f, 0.1f));
	TestFalse(TEXT("positive-time movement never requests a temporal camera cut"),
		CrossesHardCut(TheatreCut, 0.0f, 0.03f));

	UElysiumCameraComponent* TemporalCamera = NewObject<UElysiumCameraComponent>();
	TestNotNull(TEXT("the temporal-cut phase test has a camera component"), TemporalCamera);
	if (TemporalCamera)
	{
		FElysiumCameraShot TemporalShot;
		TemporalShot.BlendSeconds = 0.0f;
		const int32 TemporalShotId = TemporalCamera->PushShot(TemporalShot);
		TestTrue(TEXT("a zero-blend push latches a cut until the camera apply phase"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TestFalse(TEXT("the camera apply phase consumes the cut exactly once"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TemporalCamera->UpdateCamera(1.0f / 60.0f);
		FMinimalViewInfo ScriptedView;
		ScriptedView.PostProcessSettings.MotionBlurAmount = 0.5f;
		TemporalCamera->ApplyToView(ScriptedView);
		TestTrue(TEXT("a visible scripted shot overrides camera motion blur"),
			ScriptedView.PostProcessSettings.bOverride_MotionBlurAmount);
		TestEqual(TEXT("scripted camera edits and dollies render without radial smear"),
			ScriptedView.PostProcessSettings.MotionBlurAmount, 0.0f);

		TemporalShot.BlendSeconds = 1.0f;
		TemporalShot.bCameraCut = true;
		TestTrue(TEXT("an authored cut update reaches the pending-until-apply latch"),
			TemporalCamera->UpdateShot(TemporalShotId, TemporalShot));
		TestTrue(TEXT("the authored cut remains pending until camera application"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TemporalShot.bCameraCut = false;
		TemporalCamera->UpdateShot(TemporalShotId, TemporalShot);
		TestFalse(TEXT("an ordinary value update does not invent a temporal cut"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TestTrue(TEXT("the focused shot can be popped"),
			TemporalCamera->PopShot(TemporalShotId, 0.0f));
		TestTrue(TEXT("a zero-blend pop also latches a cut until camera application"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
	}

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_track_test__");
	FElysiumEntityDef Pos;
	Pos.Classname = TEXT("camera_track");
	Pos.TargetName = TEXT("pos");
	Pos.Origin = FVector(10.0f, 20.0f, 30.0f);
	Pos.Keys.Add(TEXT("HoldAtEnd"), TEXT("1"));
	Pos.Keys.Add(TEXT("FocalLength"), TEXT("50"));
	Pos.Keys.Add(TEXT("FromPlayerTime"), TEXT("0.25"));
	Pos.Keys.Add(TEXT("ToPlayerTime"), TEXT("0.75"));
	FElysiumOutputDef Completed;
	Completed.Name = TEXT("OnAnimationCompleted");
	Completed.Target = TEXT("completed");
	Completed.Input = TEXT("Add");
	Completed.Param = TEXT("1");
	Pos.Outputs.Add(Completed);
	FElysiumOutputDef Reached = Completed;
	Reached.Name = TEXT("OnReachedKeyframe");
	Reached.Target = TEXT("reached");
	Pos.Outputs.Add(Reached);
	Defs.Defs.Add(Pos);
	FElysiumEntityDef Target = Pos;
	Target.TargetName = TEXT("target");
	Target.Origin = FVector(100.0f, 200.0f, 300.0f);
	Target.Outputs.Reset();
	Defs.Defs.Add(Target);
	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("completed");
	Defs.Defs.Add(Counter);
	Counter.TargetName = TEXT("reached");
	Defs.Defs.Add(Counter);

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.AcceptInput(TEXT("pos"), FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestEqual(TEXT("the first role pushes one value shot"), Services.Count(TEXT("PushCameraShotValue")), 1);
	World.Tick(0.0);
	auto CounterValue = [&World]()
	{
		TArray<TPair<FString, FString>> Rows;
		if (FElysiumEntity* CounterEnt = World.FindByName(TEXT("completed")))
		{
			CounterEnt->GetDebugState(Rows);
		}
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};
	TestEqual(TEXT("zero-duration completion fires once"), CounterValue(), 1.0f);
	TArray<TPair<FString, FString>> ReachedRows;
	World.FindByName(TEXT("reached"))->GetDebugState(ReachedRows);
	TestTrue(TEXT("arrival fires the authored OnReachedKeyframe output"),
		ReachedRows.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Value") && FMath::IsNearlyEqual(FCString::Atof(*Row.Value), 1.0f);
		}));
	World.Tick(1.0);
	TestEqual(TEXT("held completion does not fire again"), CounterValue(), 1.0f);
	World.AcceptInput(TEXT("target"), FName(TEXT("PlayAsCameraTarget")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestEqual(TEXT("the paired role updates rather than pushes another shot"),
		Services.Count(TEXT("PushCameraShotValue")), 1);
	TestTrue(TEXT("the composed value uses the authored target"),
		Services.LastCameraShot.bUseLookAt && Services.LastCameraShot.LookAt.Equals(Target.Origin, 0.01f));
	TestTrue(TEXT("the composed track shot bypasses the generic target-chase turn limiter"),
		Services.LastCameraShot.MaxTurnRate.IsZero());
	World.AcceptInput(TEXT("pos"), FName(TEXT("RestoreCameraToPlayerControl")), FElysiumVariant::Float(1.25f),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestTrue(TEXT("restoring position leaves the target-owned shot live"), World.HasTrackCamera());
	World.AcceptInput(TEXT("target"), FName(TEXT("Restore")), FElysiumVariant::Float(1.25f),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestFalse(TEXT("restoring the final role pops the shared value shot"), World.HasTrackCamera());
	TestTrue(TEXT("the explicit Restore parameter overrides ToPlayerTime"),
		Services.Saw(TEXT("PopCameraShot 1 blend=1.25")));

	const FElysiumEntity* PosEntity = World.FindByName(TEXT("pos"));
	TestNotNull(TEXT("the camera owner remains addressable"), PosEntity);
	if (PosEntity)
	{
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(1.0f, 2.0f, 3.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f, true);
		TestTrue(TEXT("the world carries a hard-cut instruction onto the value shot"),
			Services.LastCameraShot.bCameraCut);
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(2.0f, 3.0f, 4.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f);
		TestFalse(TEXT("the temporal cut instruction is one-shot"),
			Services.LastCameraShot.bCameraCut);
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
// 11.9 — persistence. `docs/architecture/save-architecture.md` §10 asks for the strongest harness in the
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
	A.Activate(0.0);

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
	B.Activate(0.0);
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

	Payload.Player.Name = TEXT("Carmilla");
	Payload.Player.Sheet.SetClan(7);
	Payload.Player.Sheet.SetMale(false);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, 3);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Disciplines, /*Celerity*/ 3, 2);
	Payload.Player.Money = 250;
	Payload.Player.ArmorSlot = 4;
	Payload.Player.Health = 61;
	Payload.Player.MaxHealth = 100;
	Payload.Player.Law.Criminal = 2;
	Payload.Player.ExperienceLog.Add({ TEXT("xp_tutorial"), 0 });
	// The journal rides with the record (9.4d) — the quest map is the Session block's, so a payload
	// that loses these rows keeps the states and forgets the order they were taken in.
	Payload.Player.Journal.Add({ TEXT("Arthur Knox"), /*Table*/ 4, /*Quest*/ 0, /*State*/ 2,
		/*Order*/ 1, /*bUnread*/ true });
	// `m_iCurrQuestLogArea` — the quest log's hub tab is player state in VtMB, not the panel's, so
	// it has to survive a save the way the sheet does.
	Payload.Player.QuestLogArea = 1;

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
	TestEqual(TEXT("the PC's name survived"), Back.Player.Name, FString(TEXT("Carmilla")));
	TestEqual(TEXT("the authored player body slot survived"), Back.Player.ArmorSlot, 4);
	TestEqual(TEXT("and the quest log's hub tab"), Back.Player.QuestLogArea, 1);
	TestEqual(TEXT("money survived"), Back.Player.Money, 250);
	TestEqual(TEXT("the law counters survived"), Back.Player.Law.Criminal, 2);
	TestEqual(TEXT("the journal survived"), Back.Player.Journal.Num(), 1);
	if (Back.Player.Journal.Num() == 1)
	{
		const FElysiumAssignedQuest& Row = Back.Player.Journal[0];
		TestEqual(TEXT("with its title"), Row.Title, FString(TEXT("Arthur Knox")));
		TestEqual(TEXT("its quest address"), Row.Table, 4);
		TestEqual(TEXT("its state"), Row.State, 2);
		TestEqual(TEXT("and its display order"), Row.Order, 1);
		TestTrue(TEXT("and the unread marker"), Row.bUnread);
	}
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

	// `BodyIdentity` is the first additive schema with an upgrade branch: v6 is still the floor,
	// and its absent armor slot migrates to retail's first body.
	TestEqual(TEXT("the floor remains the migratable history schema"),
		(int32)FElysiumSaveVersion::MinSupported, (int32)FElysiumSaveVersion::History);
	TestEqual(TEXT("body identity is the current schema"),
		(int32)FElysiumSaveVersion::Latest, (int32)FElysiumSaveVersion::BodyIdentity);

	// Build the exact v6 player byte stream (which has no ArmorSlot field) and read it through the
	// current operator. This is deliberately manual: asking the current writer to emit v6 would
	// test today's field list rather than the historical layout.
	{
		TArray<uint8> LegacyBytes;
		{
			FMemoryWriter Writer(LegacyBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::History);
			FElysiumPlayerRecord Legacy = Payload.Player;
			Ar << Legacy.Name << Legacy.Sheet << Legacy.Money;
			Ar << Legacy.Health << Legacy.MaxHealth;
			Ar << Legacy.ExperienceLog << Legacy.Effects << Legacy.EmailFlags;
			Ar << Legacy.ExperienceRemainder << Legacy.LifetimeExperience;
			Ar << Legacy.Law << Legacy.bUnkillable << Legacy.Journal << Legacy.QuestLogArea
				<< Legacy.HistoryId;
		}
		FElysiumPlayerRecord Migrated;
		Migrated.ArmorSlot = 5;
		FMemoryReader Reader(LegacyBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::History);
		Ar << Migrated;
		TestEqual(TEXT("a v6 player migrates to armor slot zero"), Migrated.ArmorSlot, 0);
		TestEqual(TEXT("v6 fields after the inserted slot stay aligned"), Migrated.Health,
			Payload.Player.Health);
	}

	// Corrupt/future body indices cannot escape the authored M_Body0..5 range.
	{
		FElysiumPlayerRecord Invalid = Payload.Player;
		Invalid.ArmorSlot = 99;
		TArray<uint8> RecordBytes;
		{
			FMemoryWriter Writer(RecordBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
			Ar << Invalid;
		}
		FElysiumPlayerRecord Clamped;
		FMemoryReader Reader(RecordBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Clamped;
		TestEqual(TEXT("a loaded armor slot clamps to the authored maximum"), Clamped.ArmorSlot, 5);
	}

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
	World.Activate(0.0);
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
	Other.Activate(0.0);
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

// ==================================================================================================
// 9.4d — the quest log: the decision and the journal bookkeeping, with no world and no disk.
// `FElysiumQuestTables` is plain C++ with public arrays, so the catalogue below is hand-built and
// every rule `docs/vtmb/game_runtime.md` -> "Quests" records is driven directly.
// ==================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQuestLogTest, "Elysium.Substrate.QuestLog", GElysiumTestFlags)

namespace
{
	// Two quests on one table, plus a third carrying a `botch` state — the type no shipped row
	// authors and the only thing that refuses a change.
	FElysiumQuestTables MakeQuestFixture()
	{
		FElysiumQuestTables T;

		FElysiumQuest& Knox = T.Quests[4].AddDefaulted_GetRef();   // santamonica
		Knox.Title = TEXT("Arthur Knox");
		Knox.DisplayName = TEXT("A Bounty For The Hunter");
		Knox.TableIndex = 4;
		Knox.Index = 0;
		for (int32 i = 1; i <= 3; ++i)
		{
			FElysiumQuestState& S = Knox.States.AddDefaulted_GetRef();
			S.Id = i;
			S.Type = (i == 3) ? TEXT("success") : TEXT("incomplete");
			S.Description = FString::Printf(TEXT("state %d"), i);
			if (i == 2) { S.AwardXp = TEXT("Carson01"); S.AwardMoney = 25; }
			if (i == 3) { S.Event = TEXT("G.Knox_Done = 1"); }
		}

		FElysiumQuest& Tut = T.Quests[4].AddDefaulted_GetRef();
		Tut.Title = TEXT("Tutorial");
		Tut.DisplayName = TEXT("Learning The Ropes");
		Tut.TableIndex = 4;
		Tut.Index = 1;
		FElysiumQuestState& TS = Tut.States.AddDefaulted_GetRef();
		TS.Id = 1;
		TS.Type = TEXT("incomplete");

		FElysiumQuest& Bad = T.Quests[0].AddDefaulted_GetRef();    // chinatown
		Bad.Title = TEXT("Botched");
		Bad.DisplayName = TEXT("Botched");
		Bad.TableIndex = 0;
		Bad.Index = 0;
		FElysiumQuestState& B1 = Bad.States.AddDefaulted_GetRef();
		B1.Id = 1; B1.Type = TEXT("botch");
		FElysiumQuestState& B2 = Bad.States.AddDefaulted_GetRef();
		B2.Id = 2; B2.Type = TEXT("incomplete");

		T.Reindex();
		return T;
	}
}

bool FElysiumQuestLogTest::RunTest(const FString&)
{
	using namespace ElysiumQuestLog;
	const FElysiumQuestTables Tables = MakeQuestFixture();
	TArray<FElysiumAssignedQuest> Journal;

	// --- The Type vocabulary: substring, in the engine's order, unknown reads as incomplete -----
	TestTrue(TEXT("incomplete"), ParseType(TEXT("incomplete")) == EType::Incomplete);
	TestTrue(TEXT("success"),    ParseType(TEXT("success"))    == EType::Success);
	TestTrue(TEXT("failure"),    ParseType(TEXT("failure"))    == EType::Failure);
	TestTrue(TEXT("botch"),      ParseType(TEXT("botch"))      == EType::Botch);
	TestTrue(TEXT("a substring match, not equality"),
		ParseType(TEXT("  Success!  ")) == EType::Success);
	TestTrue(TEXT("an unrecognised type reads as incomplete"),
		ParseType(TEXT("qqq")) == EType::Incomplete);

	// --- An unknown title, and a state the quest does not have, do nothing at all ---------------
	FOutcome Out = Apply(Tables, Journal, TEXT("No Such Quest"), 1);
	TestFalse(TEXT("an unknown title does not resolve"), Out.bResolved);
	TestFalse(TEXT("and awards nothing"), Out.bChanged);
	TestEqual(TEXT("and writes no row"), Journal.Num(), 0);

	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 9);
	TestFalse(TEXT("a state the quest does not have does not resolve"), Out.bResolved);
	TestEqual(TEXT("and writes no row"), Journal.Num(), 0);

	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 0);
	TestFalse(TEXT("state 0 is unassigned, never a real state"), Out.bResolved);

	// --- First assignment: a row, order 1, and the state's awards -------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a first assignment resolves"), Out.bResolved);
	TestTrue(TEXT("and changes"), Out.bChanged);
	TestEqual(TEXT("the first quest of a run gets order 1"), Out.Order, 1);
	TestEqual(TEXT("one row"), Journal.Num(), 1);
	TestEqual(TEXT("on the right table"), Journal[0].Table, 4);
	TestEqual(TEXT("at the right quest"), Journal[0].Quest, 0);
	TestEqual(TEXT("at the right state"), Journal[0].State, 1);
	TestTrue(TEXT("marked unread"), Journal[0].bUnread);
	TestEqual(TEXT("state 1 awards no xp"), Out.AwardXpKey, FString());

	// --- A repeat set is a no-op: no award, no re-run -------------------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a repeat set still resolves"), Out.bResolved);
	TestFalse(TEXT("but does not change"), Out.bChanged);
	TestEqual(TEXT("and owes no xp"), Out.AwardXpKey, FString());
	TestEqual(TEXT("and owes no money"), Out.AwardMoney, 0);
	TestEqual(TEXT("still one row"), Journal.Num(), 1);

	// --- A forward change replaces the row in place and pays out --------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 2);
	TestTrue(TEXT("a forward change changes"), Out.bChanged);
	TestEqual(TEXT("the row is replaced, not appended"), Journal.Num(), 1);
	TestEqual(TEXT("at the new state"), Journal[0].State, 2);
	TestEqual(TEXT("keeping its order"), Journal[0].Order, 1);
	TestEqual(TEXT("and owes the authored key"), Out.AwardXpKey, FString(TEXT("Carson01")));
	TestEqual(TEXT("and the authored money"), Out.AwardMoney, 25);

	// --- Backwards fires too: the gate tests inequality, not direction --------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a backwards move changes"), Out.bChanged);
	TestEqual(TEXT("and lands on the earlier state"), Journal[0].State, 1);

	// --- The title match is case- and whitespace-insensitive, and the row keeps the catalogue's --
	Out = Apply(Tables, Journal, TEXT("  arthur KNOX  "), 3);
	TestTrue(TEXT("a differently-cased, padded title resolves"), Out.bResolved);
	TestEqual(TEXT("and does not become a second row"), Journal.Num(), 1);
	TestEqual(TEXT("the row keeps the catalogue's spelling"),
		Journal[0].Title, FString(TEXT("Arthur Knox")));
	TestEqual(TEXT("state 3 carries the Event"), Out.Event, FString(TEXT("G.Knox_Done = 1")));

	// --- The second quest assigned gets order 2 -------------------------------------------------
	Out = Apply(Tables, Journal, TEXT("Tutorial"), 1);
	TestEqual(TEXT("the second quest assigned gets order 2"), Out.Order, 2);
	TestEqual(TEXT("two rows"), Journal.Num(), 2);

	// --- Leaving a `botch` state is refused outright --------------------------------------------
	TArray<FElysiumAssignedQuest> Botch;
	Out = Apply(Tables, Botch, TEXT("Botched"), 1);
	TestTrue(TEXT("entering a botched state is allowed"), Out.bChanged);
	Out = Apply(Tables, Botch, TEXT("Botched"), 2);
	TestTrue(TEXT("leaving it is refused"), Out.bBotched);
	TestFalse(TEXT("with nothing awarded"), Out.bChanged);
	TestEqual(TEXT("and the row untouched"), Botch[0].State, 1);

	// --- FindRow is the same case-insensitive match ---------------------------------------------
	TestNotNull(TEXT("FindRow matches case-insensitively"),
		FindRow(Journal, TEXT("ARTHUR knox")));
	TestNull(TEXT("and misses an unassigned quest"), FindRow(Journal, TEXT("Botched")));

	// --- Ordinal addressing is what SetQuest uses, not the authored ID --------------------------
	const FElysiumQuest* Knox = Tables.Find(TEXT("Arthur Knox"));
	TestNotNull(TEXT("the fixture quest resolves"), Knox);
	if (Knox)
	{
		TestNotNull(TEXT("ordinal 1 is the first state in file order"), Knox->StateByOrdinal(1));
		TestNull(TEXT("ordinal 0 is not a state"), Knox->StateByOrdinal(0));
		TestNull(TEXT("nor is one past the end"), Knox->StateByOrdinal(4));
		TestNull(TEXT("nor one past VtMB's 20-state cap"), Knox->StateByOrdinal(21));
	}

	return true;
}

// ==================================================================================================
// The journal as a screen reads it (9.4e) — `ElysiumQuestView`. The read side of the same rows
// `ElysiumQuestLog` writes: which column a row lands in, which hub tab shows it, and what order the
// list is in. Pure over a catalogue and an array, so the whole screen's model is testable with no
// world, no subsystem and no viewport.
// ==================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQuestViewTest, "Elysium.Substrate.QuestView", GElysiumTestFlags)

bool FElysiumQuestViewTest::RunTest(const FString&)
{
	using namespace ElysiumQuestView;
	constexpr int32 SantaMonica = 4;
	constexpr int32 Chinatown   = 0;
	constexpr int32 Downtown    = 1;
	constexpr int32 Main        = FElysiumQuestTables::MainTable;

	FElysiumQuestTables Tables = MakeQuestFixture();

	// A cross-hub quest, which is the case the hub tabs cannot express on their own.
	FElysiumQuest& Plot = Tables.Quests[Main].AddDefaulted_GetRef();
	Plot.Title = TEXT("The Sarcophagus");
	Plot.DisplayName = TEXT("The Epic Of The Ankaran Sarcophagus");
	Plot.TableIndex = Main;
	Plot.Index = 0;
	FElysiumQuestState& PS1 = Plot.States.AddDefaulted_GetRef();
	PS1.Id = 1; PS1.Type = TEXT("incomplete"); PS1.Description = TEXT("The prince wants it NOW!");
	FElysiumQuestState& PS2 = Plot.States.AddDefaulted_GetRef();
	PS2.Id = 2; PS2.Type = TEXT("failure"); PS2.Description = TEXT("Gone.");
	Tables.Reindex();

	// --- the empty journal is a real state, not a degenerate one --------------------------------
	{
		const FView Empty = Build(Tables, {}, SantaMonica);
		TestEqual(TEXT("nothing active"), Empty.Active.Num(), 0);
		TestEqual(TEXT("nothing completed"), Empty.Completed.Num(), 0);
		TestEqual(TEXT("nothing failed"), Empty.Failed.Num(), 0);
		TestEqual(TEXT("and every tab reads zero"), Empty.HubActive[SantaMonica], 0);
	}

	// One row per column, plus the cross-hub plot quest and a row the catalogue cannot place.
	TArray<FElysiumAssignedQuest> Journal;
	Journal.Add({ TEXT("Tutorial"),        SantaMonica, 1, /*State*/ 1, /*Order*/ 1, /*bUnread*/ false });
	Journal.Add({ TEXT("Arthur Knox"),     SantaMonica, 0, /*State*/ 3, /*Order*/ 2, true });   // success
	Journal.Add({ TEXT("The Sarcophagus"), Main,        0, /*State*/ 1, /*Order*/ 3, false });
	Journal.Add({ TEXT("Botched"),         Chinatown,   0, /*State*/ 1, /*Order*/ 4, false });  // botch
	Journal.Add({ TEXT("Ghost Row"),       SantaMonica, 99, /*State*/ 1, /*Order*/ 5, false }); // no such quest

	const FView SM = Build(Tables, Journal, SantaMonica);

	// --- the type decides the column -------------------------------------------------------------
	TestEqual(TEXT("success lands in Completed"), SM.Completed.Num(), 1);
	if (SM.Completed.Num() == 1)
	{
		TestEqual(TEXT("and carries the catalogue's display name"),
			SM.Completed[0].DisplayName, FString(TEXT("A Bounty For The Hunter")));
		TestTrue(TEXT("and its unread marker"), SM.Completed[0].bUnread);
	}
	TestEqual(TEXT("nothing failed under this hub"), SM.Failed.Num(), 0);

	// Tutorial (incomplete) + the plot quest (cross-hub) + the unplaceable row. `Botched` is
	// Chinatown's, so it is not here even though `botch` counts as still-open.
	TestEqual(TEXT("three rows are open under Santa Monica"), SM.Active.Num(), 3);

	// --- newest assignment first -----------------------------------------------------------------
	if (SM.Active.Num() == 3)
	{
		TestEqual(TEXT("the newest row sorts first"), SM.Active[0].Order, 5);
		TestEqual(TEXT("then the next"), SM.Active[1].Order, 3);
		TestEqual(TEXT("then the oldest"), SM.Active[2].Order, 1);
	}

	// --- a catalogue miss degrades, it does not vanish --------------------------------------------
	const FEntry* Ghost = SM.Active.FindByPredicate(
		[](const FEntry& E) { return E.Title == TEXT("Ghost Row"); });
	TestNotNull(TEXT("a row the catalogue cannot place is still shown"), Ghost);
	if (Ghost)
	{
		TestFalse(TEXT("marked unresolved"), Ghost->bResolved);
		TestEqual(TEXT("headed by its own title"), Ghost->DisplayName, FString(TEXT("Ghost Row")));
		TestTrue(TEXT("with no description to show"), Ghost->Description.IsEmpty());
	}

	// --- `main` rides along in every hub ----------------------------------------------------------
	auto HasPlot = [](const FView& V)
	{
		return V.Active.ContainsByPredicate(
			[](const FEntry& E) { return E.Table == FElysiumQuestTables::MainTable; });
	};
	TestTrue(TEXT("the cross-hub quest shows under Santa Monica"), HasPlot(SM));
	TestTrue(TEXT("and under Downtown"), HasPlot(Build(Tables, Journal, Downtown)));
	TestTrue(TEXT("and under Chinatown"), HasPlot(Build(Tables, Journal, Chinatown)));

	// --- botch is still open, and belongs to its own hub -------------------------------------------
	const FView CT = Build(Tables, Journal, Chinatown);
	TestTrue(TEXT("a botch state reads as still open"),
		CT.Active.ContainsByPredicate([](const FEntry& E) { return E.Title == TEXT("Botched"); }));

	// --- the tab counts describe the TABS, not the journal ------------------------------------------
	// Santa Monica: Tutorial + Ghost Row + the plot quest. Chinatown: Botched + the plot quest.
	// Downtown and Hollywood have only the plot quest — which is exactly why the count is per-tab.
	TestEqual(TEXT("Santa Monica counts three"), SM.HubActive[SantaMonica], 3);
	TestEqual(TEXT("Chinatown counts two"), SM.HubActive[Chinatown], 2);
	TestEqual(TEXT("Downtown counts the plot quest alone"), SM.HubActive[Downtown], 1);

	// --- the completed row is not counted as open ----------------------------------------------------
	{
		TArray<FElysiumAssignedQuest> Done;
		Done.Add({ TEXT("Arthur Knox"), SantaMonica, 0, 3, 1, false });
		const FView V = Build(Tables, Done, SantaMonica);
		TestEqual(TEXT("a finished quest leaves the tab count at zero"), V.HubActive[SantaMonica], 0);
	}

	// --- the opening hub, for a character that has never opened the screen ----------------------------
	{
		TestEqual(TEXT("an empty journal opens on Santa Monica"),
			DefaultHub(Tables, {}), SantaMonica);

		// Chinatown alone has open work, so that is where the screen opens.
		TArray<FElysiumAssignedQuest> OnlyCT;
		OnlyCT.Add({ TEXT("Botched"), Chinatown, 0, 1, 1, false });
		TestEqual(TEXT("otherwise it opens where the work is"),
			DefaultHub(Tables, OnlyCT), Chinatown);
	}

	return true;
}


// ================================================================================================
// Chargen — the pools, the price of a dot, and the row filter (9.4f)
// ================================================================================================

namespace ElysiumChargenTest
{
	// A synthetic `stats.txt`: the real slot indices with hand-authored bounds and prices, so the
	// arithmetic is exercised with no export on disk. The clan half of the pools needs the real
	// `rules_tables.txt` and lives in the Content tier; here `FElysiumChargenRules::Rules` is left
	// null, which zeroes the clan term — exactly what every shipped attribute/ability subpool holds.
	FElysiumStat MakeStat(int32 Index, const TCHAR* Internal, int32 Min, int32 Max, int32 Default)
	{
		FElysiumStat S;
		S.Index = Index;
		S.InternalName = Internal;
		S.Name = Internal;
		S.Min = Min;
		S.Max = Max;
		S.Default = Default;
		S.MinSell = Min;
		S.MaxBuy = Max;
		S.MinExpr = FString::FromInt(Min);
		S.MaxExpr = FString::FromInt(Max);
		return S;
	}

	FElysiumRuleTable MakeTable(const TCHAR* Internal, const TArray<int32>& Values)
	{
		FElysiumRuleTable T;
		T.InternalName = Internal;
		T.Name = Internal;
		T.bClamping = true;
		for (int32 i = 0; i < Values.Num(); ++i)
		{
			T.Rows.Add(i, FString::FromInt(Values[i]));
		}
		return T;
	}

	// The three orderings a test needs, flattened as the file authors them: three category indices
	// per ordering, in primary/secondary/tertiary order.
	const TArray<int32> GOrderLookups =
	{
		0, 1, 2,   // 0 Physical_Social_Mental   / Talents_Skills_Knowledges
		0, 2, 1,   // 1 Physical_Mental_Social   / Talents_Knowledges_Skills
		1, 0, 2,   // 2 Social_Physical_Mental   / Skills_Talents_Knowledges
		1, 2, 0,   // 3
		2, 1, 0,   // 4
		2, 0, 1,   // 5
		0, 1, 2,   // 6 No_Order
	};

	void BuildStats(FElysiumStatTable& Out)
	{
		FElysiumStatContainer& Attribs = Out.Containers[(uint8)EElysiumTraitContainer::Attributes];
		FElysiumStatContainer& Abilities = Out.Containers[(uint8)EElysiumTraitContainer::Abilities];
		FElysiumStatContainer& Disciplines = Out.Containers[(uint8)EElysiumTraitContainer::Disciplines];
		Attribs.InternalName = TEXT("Attributes");
		Abilities.InternalName = TEXT("Abilities");
		Disciplines.InternalName = TEXT("Disciplines");

		// Attributes: the order stat, then Strength(1) .. Wits(9). No `New` — an attribute is priced
		// by `Raise` at every rating, including its first dot.
		FElysiumStat Order = MakeStat(0, TEXT("Attrib_Order"), 0, 6, 0);
		Order.NameMapping = TEXT("AttributeOrder");
		Order.Tables.Add(ElysiumFold(TEXT("Attribute_Order_Lookups")),
			MakeTable(TEXT("Attribute_Order_Lookups"), GOrderLookups));
		Order.Tables.Add(ElysiumFold(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary")),
			MakeTable(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary"), { 2, 1, 0 }));
		Order.Tables.Add(ElysiumFold(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary_Kine")),
			MakeTable(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary_Kine"), { 1, 1, 1 }));
		Attribs.Stats.Add(MoveTemp(Order));

		Attribs.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 4"));
		Attribs.DefaultCosts.New = Attribs.DefaultCosts.Raise;
		Attribs.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Attributes))
		{
			if (Slot.Index == 0)
			{
				continue;
			}
			const bool bCore = Slot.Index >= 1 && Slot.Index <= 9;
			FElysiumStat S = MakeStat(Slot.Index, Slot.Internal, bCore ? 1 : 0, bCore ? 5 : 100,
				bCore ? 1 : 0);
			if (!bCore)
			{
				// The derived block is priced out of reach, which is how the shipped file keeps it
				// off the screen without a separate "buyable" flag.
				S.Costs.Raise.Parse(TEXT("10000"));
				S.Costs.New = S.Costs.Raise;
				S.Costs.bPresent = true;
			}
			Attribs.Stats.Add(MoveTemp(S));
		}

		// Abilities: a flat `New` of 3 and a per-rating `Raise`, so the 0 -> 1 step takes a different
		// branch from every step after it.
		FElysiumStat AbilityOrder = MakeStat(0, TEXT("Ability_Order"), 0, 6, 0);
		AbilityOrder.NameMapping = TEXT("AbilityOrder");
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Ability_Order_Lookups")),
			MakeTable(TEXT("Ability_Order_Lookups"), GOrderLookups));
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Subpool_Ability_Primary_Secondary_Tertiary")),
			MakeTable(TEXT("Subpool_Ability_Primary_Secondary_Tertiary"), { 3, 2, 1 }));
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Subpool_Ability_Primary_Secondary_Tertiary_Kine")),
			MakeTable(TEXT("Subpool_Ability_Primary_Secondary_Tertiary_Kine"), { 1, 1, 1 }));
		Abilities.Stats.Add(MoveTemp(AbilityOrder));

		Abilities.DefaultCosts.New.Parse(TEXT("3"));
		Abilities.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 2"));
		Abilities.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Abilities))
		{
			if (Slot.Index != 0)
			{
				Abilities.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 0));
			}
		}

		// Disciplines: `Min -1` / `Default -1`, which is what the row filter reads.
		Disciplines.DefaultCosts.New.Parse(TEXT("10"));
		Disciplines.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 7"));
		Disciplines.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Disciplines))
		{
			Disciplines.Stats.Add(MakeStat(Slot.Index, Slot.Internal, -1, 5, -1));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChargenTest, "Elysium.Substrate.Chargen", GElysiumTestFlags)
bool FElysiumChargenTest::RunTest(const FString&)
{
	using namespace ElysiumChargenTest;

	FElysiumStatTable Stats;
	BuildStats(Stats);

	FElysiumChargenRules Rules;
	Rules.Stats = &Stats;

	// --- which pool owns which slot --------------------------------------------------------------
	{
		const auto Pool = [](EElysiumTraitContainer C, int32 S)
		{
			return (int32)ElysiumChargen::PoolFor(C, S);
		};
		TestEqual(TEXT("Strength spends Physical"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength),
			(int32)EElysiumChargenPool::Physical);
		TestEqual(TEXT("Appearance spends Social"),
			Pool(EElysiumTraitContainer::Attributes, 6), (int32)EElysiumChargenPool::Social);
		TestEqual(TEXT("Wits spends Mental"),
			Pool(EElysiumTraitContainer::Attributes, 9), (int32)EElysiumChargenPool::Mental);
		TestEqual(TEXT("Subterfuge spends Talents"),
			Pool(EElysiumTraitContainer::Abilities, 4), (int32)EElysiumChargenPool::Talents);
		TestEqual(TEXT("Stealth spends Skills"),
			Pool(EElysiumTraitContainer::Abilities, 8), (int32)EElysiumChargenPool::Skills);
		TestEqual(TEXT("Academics spends Knowledges"),
			Pool(EElysiumTraitContainer::Abilities, 12), (int32)EElysiumChargenPool::Knowledges);
		TestEqual(TEXT("a discipline spends Disciplines"),
			Pool(EElysiumTraitContainer::Disciplines, 0), (int32)EElysiumChargenPool::Disciplines);

		// The order stat, the derived block and the whole parallel container are not sold.
		TestEqual(TEXT("Attrib_Order is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Clan is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Experience is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Active_Disciplines is not bought"),
			Pool(EElysiumTraitContainer::ActiveDisciplines, 0), (int32)EElysiumChargenPool::None);
	}

	// --- the tier term: an ordering picks which category gets 2/1/0 and 3/2/1 -------------------
	{
		// Order 1 is `Physical_Mental_Social`, which is Brujah's: Physical primary (2), Mental
		// secondary (1), Social tertiary (0) — the reading the shipped screen shows as
		// `PHYSICAL(2)` / `SOCIAL` / `MENTAL(1)`, with the zero pool drawing no parenthetical.
		const FElysiumChargenPools P = ElysiumChargen::BuildPools(Rules, 2, 1, 0, true);
		TestEqual(TEXT("Physical_Mental_Social gives Physical 2"),
			P[EElysiumChargenPool::Physical], 2);
		TestEqual(TEXT("  Mental 1"), P[EElysiumChargenPool::Mental], 1);
		TestEqual(TEXT("  and Social 0"), P[EElysiumChargenPool::Social], 0);

		TestEqual(TEXT("Talents_Skills_Knowledges gives Talents 3"),
			P[EElysiumChargenPool::Talents], 3);
		TestEqual(TEXT("  Skills 2"), P[EElysiumChargenPool::Skills], 2);
		TestEqual(TEXT("  and Knowledges 1"), P[EElysiumChargenPool::Knowledges], 1);

		TestEqual(TEXT("attributes total 3 and abilities 6, whatever the ordering"),
			P[EElysiumChargenPool::Physical] + P[EElysiumChargenPool::Social]
			+ P[EElysiumChargenPool::Mental], 3);

		// Reordering moves the points; it never creates or destroys them.
		const FElysiumChargenPools Q = ElysiumChargen::BuildPools(Rules, 2, 4, 3, true);
		TestEqual(TEXT("Mental_Social_Physical gives Mental 2"), Q[EElysiumChargenPool::Mental], 2);
		TestEqual(TEXT("  and Physical 0"), Q[EElysiumChargenPool::Physical], 0);
		TestEqual(TEXT("Skills_Knowledges_Talents gives Skills 3"), Q[EElysiumChargenPool::Skills], 3);
		TestEqual(TEXT("  and Talents 1"), Q[EElysiumChargenPool::Talents], 1);
		TestEqual(TEXT("the total is the ordering's invariant"), Q.Total(), P.Total());

		// A kine character takes the flat 1/1/1 tables instead.
		const FElysiumChargenPools K = ElysiumChargen::BuildPools(Rules, 2, 1, 0, false);
		TestEqual(TEXT("a kine character gets 1 in every attribute pool"),
			K[EElysiumChargenPool::Physical] + K[EElysiumChargenPool::Social]
			+ K[EElysiumChargenPool::Mental], 3);
		TestEqual(TEXT("  and 1 Social rather than 0"), K[EElysiumChargenPool::Social], 1);
	}

	// --- the row filter: [0, 6) on the CURRENT value ---------------------------------------------
	{
		FElysiumSheet Sheet;
		Sheet.SeedFrom(Stats);
		TestFalse(TEXT("a discipline at the -1 default draws no row"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Disciplines, 0));

		Sheet.SetBase(EElysiumTraitContainer::Disciplines, 0, 1);
		Sheet.RecomputeCurrent(&Stats);
		TestTrue(TEXT("a clan discipline at 1 draws one"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Disciplines, 0));

		TestTrue(TEXT("an attribute at its default draws"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength));
		TestFalse(TEXT("the derived block never does"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Attributes,
				ElysiumSlot::MaxHealth));
	}

	// --- buying and selling ----------------------------------------------------------------------
	{
		FElysiumChargenState State;
		State.Clan = 2;
		State.Sheet.SeedFrom(Stats);
		State.Sheet.SetBase(EElysiumTraitContainer::Disciplines, 0, 1);
		State.Sheet.RecomputeCurrent(&Stats);
		State.Baseline = State.Sheet;
		State.Pools[EElysiumChargenPool::Physical] = 20;
		State.Pools[EElysiumChargenPool::Talents] = 20;
		State.Pools[EElysiumChargenPool::Disciplines] = 20;

		// A chargen point buys a dot outright — the `Costs` blocks price the level-up path, not this
		// one, and a 2-point pool against `Current_Rating * 4` could never buy anything.
		int32 Cost = 0;
		TestTrue(TEXT("Strength can be raised"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  for one point"), Cost, 1);
		TestTrue(TEXT("  and it lands"), ElysiumChargen::Buy(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  Strength is now 2"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 2);
		TestEqual(TEXT("  and one Physical point is spent"),
			State.Spent[EElysiumChargenPool::Physical], 1);
		TestEqual(TEXT("  leaving 19"), State.Remaining(EElysiumChargenPool::Physical), 19);

		// The point comes back — a sold dot restores the pool exactly.
		int32 Refund = 0;
		TestTrue(TEXT("the dot can be sold back"), ElysiumChargen::CanSell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));
		TestEqual(TEXT("  for what it cost"), Refund, Cost);
		TestTrue(TEXT("  and the sale lands"), ElysiumChargen::Sell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  restoring the pool exactly"),
			State.Remaining(EElysiumChargenPool::Physical), 20);

		// The baseline is the floor: a granted dot was never paid for.
		TestFalse(TEXT("a baseline dot cannot be sold"), ElysiumChargen::CanSell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));

		// An ability's first dot costs the same one point as its fifth — the `New`/`Raise` split is
		// the XP path's, and chargen never asks.
		TestTrue(TEXT("Brawl's first dot is buyable"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  for one point"), Cost, 1);
		TestTrue(TEXT("  it lands"), ElysiumChargen::Buy(State, Rules,
			EElysiumTraitContainer::Abilities, 1));
		TestTrue(TEXT("the second is buyable too"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  for the same one point"), Cost, 1);
		TestEqual(TEXT("  with one Talents point spent"),
			State.Spent[EElysiumChargenPool::Talents], 1);

		// Each dot is a point, so a pool spends down to the dot: 4 points is 4 dots, one at a time.
		FElysiumChargenState Four;
		Four.Sheet.SeedFrom(Stats);
		Four.Baseline = Four.Sheet;
		Four.Pools[EElysiumChargenPool::Physical] = 4;
		int32 Bought = 0;
		while (ElysiumChargen::Buy(Four, Rules, EElysiumTraitContainer::Attributes,
			ElysiumSlot::Dexterity))
		{
			++Bought;
		}
		// Dexterity starts at 1 and caps at 5, so the ceiling refuses before the pool empties.
		TestEqual(TEXT("Dexterity stops at its Max"),
			Four.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity), 5);
		TestEqual(TEXT("  after four dots"), Bought, 4);
		TestEqual(TEXT("  which cost four points"), Four.Spent[EElysiumChargenPool::Physical], 4);
		TestTrue(TEXT("  spending the pool out exactly"), Four.IsSpentOut());
		TestFalse(TEXT("  and the sixth dot is refused"), ElysiumChargen::CanBuy(Four, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity, Cost));

		// An empty pool refuses too, and the refusal is the pool's, not the stat's.
		FElysiumChargenState Broke;
		Broke.Sheet.SeedFrom(Stats);
		Broke.Baseline = Broke.Sheet;
		TestFalse(TEXT("a spent-out pool refuses the next dot"), ElysiumChargen::CanBuy(Broke, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestTrue(TEXT("  with the pool reporting nothing left"), Broke.IsSpentOut());

		// A hidden row is never buyable, whatever the pool holds.
		FElysiumChargenState Rich;
		Rich.Sheet.SeedFrom(Stats);
		Rich.Baseline = Rich.Sheet;
		Rich.Pools[EElysiumChargenPool::Disciplines] = 99;
		TestFalse(TEXT("a non-clan discipline cannot be bought at all"),
			ElysiumChargen::CanBuy(Rich, Rules, EElysiumTraitContainer::Disciplines, 0, Cost));

		// The derived block sits in no category, so no pool can reach it.
		Rich.Pools[EElysiumChargenPool::Physical] = 99;
		TestFalse(TEXT("a derived stat belongs to no pool"), ElysiumChargen::CanBuy(Rich, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, Cost));
	}

	// --- the same rows, spent against experience instead of pools ---------------------------------
	{
		// `BeginLevelUp` makes the live sheet both the working copy and the sell floor, so the only
		// dots that can be sold back are the ones bought in this session.
		FElysiumSheet Live;
		Live.SeedFrom(Stats);
		Live.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, 20);
		Live.SetBase(EElysiumTraitContainer::Abilities, 1, 1);   // Brawl already at 1
		Live.RecomputeCurrent(&Stats);

		FElysiumChargenState XP;
		ElysiumChargen::BeginLevelUp(XP, Live, FElysiumSheetEffects());
		TestEqual(TEXT("the scratch takes the sheet's banked experience"), XP.Experience(), 20);

		// The XP path prices off the pre-purchase base, unlike chargen's flat point.
		int32 Cost = 0;
		TestTrue(TEXT("Strength is buyable"), ElysiumChargen::CanBuy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  the 1->2 attribute dot costs 4"), Cost, 4);
		TestTrue(TEXT("  and it lands"), ElysiumChargen::Buy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  debiting the experience slot"), XP.Experience(), 16);
		TestTrue(TEXT("the 2->3 dot costs more"), ElysiumChargen::CanBuy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  namely 8"), Cost, 8);

		// Sell(r) refunds Buy(r-1) — the dot gives back exactly what it took.
		int32 Refund = 0;
		TestTrue(TEXT("it sells back"), ElysiumChargen::CanSell(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));
		TestEqual(TEXT("  for 4"), Refund, 4);
		TestTrue(TEXT("  and the sale lands"), ElysiumChargen::Sell(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  restoring the experience exactly"), XP.Experience(), 20);
		TestFalse(TEXT("  after which the pre-existing dot is not sellable"),
			ElysiumChargen::CanSell(XP, Rules, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength, Refund));

		// An ability's first dot takes `New` on the XP path — the branch chargen never reaches.
		FElysiumChargenState Fresh;
		ElysiumChargen::BeginLevelUp(Fresh, Live, FElysiumSheetEffects());
		TestTrue(TEXT("Dodge at 0 prices as New"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Abilities, 2, Cost));
		TestEqual(TEXT("  a flat 3"), Cost, 3);
		TestTrue(TEXT("Brawl at 1 prices as Raise"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  off the pre-purchase base"), Cost, 2);

		// Not enough banked is a refusal, and it is the only thing standing in the way.
		Fresh.SetExperience(1);
		TestFalse(TEXT("an unaffordable dot is refused"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		Fresh.SetExperience(4);
		TestTrue(TEXT("  and affordable again the moment the total covers it"),
			ElysiumChargen::CanBuy(Fresh, Rules, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength, Cost));
	}

	// --- the string groups a symbolic value resolves through --------------------------------------
	{
		FElysiumStrings Strings;
		Strings.Groups.Add(TEXT("attributeorder"),
			{ TEXT("Physical_Social_Mental"), TEXT("Physical_Mental_Social") });
		TestEqual(TEXT("an ordering name resolves to its index"),
			Strings.IndexOf(TEXT("AttributeOrder"), TEXT("Physical_Mental_Social")), 1);
		TestEqual(TEXT("  case-insensitively"),
			Strings.IndexOf(TEXT("attributeorder"), TEXT("physical_social_mental")), 0);
		TestEqual(TEXT("  and an unknown name does not resolve"),
			Strings.IndexOf(TEXT("AttributeOrder"), TEXT("Nonsense")), (int32)INDEX_NONE);
		TestEqual(TEXT("the index reads back"),
			Strings.At(TEXT("AttributeOrder"), 1), FString(TEXT("Physical_Mental_Social")));
		TestEqual(TEXT("  and an absent group answers the default"),
			Strings.At(TEXT("NoSuchGroup"), 0, TEXT("fallback")), FString(TEXT("fallback")));
	}

	return true;
}

// =====================================================================================
// 12.1 — the `.vcd` choreo grammar.
//
// Ported from research/tooling/probes/probe_scenes.py; these cases pin the traps that make such a port wrong.
// The whole-corpus histogram check over all 5,444 shipped files is the Content tier's
// `Elysium.Content.SceneCorpus` — this one is content-free and runs on inline text.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneParseTest, "Elysium.Substrate.SceneParse", GElysiumTestFlags)

bool FElysiumSceneParseTest::RunTest(const FString&)
{
	// --- the shape 97% of the corpus has ------------------------------------------------
	{
		const FString Text =
			TEXT("// Choreo version 1\r\n")
			TEXT("actor \"Smiling Jack\"\r\n")
			TEXT("{\r\n")
			TEXT("  channel \"Speech\"\r\n")
			TEXT("  {\r\n")
			TEXT("    event speak \"NPC Line\"\r\n")
			TEXT("    {\r\n")
			TEXT("      time 0.000000 4.870386\r\n")
			TEXT("      param \"character/dlg/main characters/jack_tutorial/line191_col_e.wav\"\r\n")
			TEXT("      param2 \"70dB\"\r\n")
			TEXT("      fixedlength\r\n")
			TEXT("    }\r\n")
			TEXT("  }\r\n")
			TEXT("  bonerename \"Bip02\" \"Bip01\"\r\n")
			TEXT("}\r\n")
			TEXT("fps 60\r\n")
			TEXT("snap off\r\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/basic.vcd"), S);

		TestTrue(TEXT("a well-formed scene is valid"), S.bValid);
		TestEqual(TEXT("the version comment is read"), S.Version, 1);
		TestEqual(TEXT("fps is read"), S.Fps, 60.f);
		TestFalse(TEXT("snap off is read"), S.bSnap);
		TestEqual(TEXT("one actor"), S.Actors.Num(), 1);
		// The quoted actor name keeps its space — the whole reason tokens are not split on space.
		TestEqual(TEXT("a quoted name keeps its spaces"), S.Actors[0].Name, FString(TEXT("Smiling Jack")));
		TestEqual(TEXT("bonerename from"), S.Actors[0].BoneFrom, FString(TEXT("Bip02")));
		TestEqual(TEXT("bonerename to"), S.Actors[0].BoneTo, FString(TEXT("Bip01")));
		TestEqual(TEXT("one channel"), S.Channels.Num(), 1);
		TestEqual(TEXT("one event"), S.Events.Num(), 1);

		if (S.Events.Num() == 1)
		{
			const FElysiumSceneEvent& E = S.Events[0];
			TestTrue(TEXT("the event is a speak"), E.Type == EElysiumChoreoEvent::Speak);
			TestEqual(TEXT("its name"), E.Name, FString(TEXT("NPC Line")));
			TestTrue(TEXT("it has an end time"), E.bHasEnd);
			TestEqual(TEXT("its end time"), E.EndTime, 4.870386f);
			TestTrue(TEXT("fixedlength is lifted"), E.bFixedLength);
			TestEqual(TEXT("param2 is lifted"), E.Param2, FString(TEXT("70dB")));
			TestTrue(TEXT("the event is bound to its actor"), E.ActorIndex == 0);
			TestEqual(TEXT("LatestTime is the authored end"), S.LatestTime, 4.870386f);
		}
	}

	// --- the two traps: comments are line-leading only, and braces are matched after tokenizing --
	{
		const FString Text =
			TEXT("actor \"A\"\n")
			TEXT("{\n")
			TEXT("  channel \"C\"\n")
			TEXT("  {\n")
			TEXT("    event speak \"L\"\n")
			TEXT("    {\n")
			// An inline `//` inside a quoted param is part of the path, NOT a comment.
			TEXT("      param \"sound/dlg//odd/line.wav\"\n")
			// A brace inside a quoted string must not open a block.
			TEXT("      param2 \"a{b\"\n")
			TEXT("      time 1.0 2.0\n")
			TEXT("    }\n")
			TEXT("  }\n")
			TEXT("}\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/traps.vcd"), S);

		TestEqual(TEXT("the scene survives the traps"), S.Events.Num(), 1);
		if (S.Events.Num() == 1)
		{
			TestEqual(TEXT("an inline // is kept inside a quoted param"),
				S.Events[0].Param, FString(TEXT("sound/dlg//odd/line.wav")));
			TestEqual(TEXT("a { inside a quoted string does not open a block"),
				S.Events[0].Param2, FString(TEXT("a{b")));
		}
	}

	// --- instantaneous events, degenerate ranges, ramps, sequenceduration, unknown blocks -------
	{
		const FString Text =
			TEXT("actor \"A\"\n")
			TEXT("{\n")
			TEXT("  channel \"C\" {\n")                      // trailing-brace form
			TEXT("    event firetrigger \"t\" {\n")
			TEXT("      time 8.600 -1.000000\n")
			TEXT("      param \"1\"\n")
			TEXT("    }\n")
			TEXT("    event expression \"x\" {\n")
			TEXT("      time 5.0 1.0\n")                     // degenerate: end before start
			TEXT("      param \"expressions/jack\"\n")
			TEXT("      param2 \"Snarl\"\n")
			TEXT("      event_ramp {\n")
			TEXT("        0.0 0.0\n")
			TEXT("        1.0 1.0\n")
			TEXT("      }\n")
			TEXT("      tags {\n")                           // recognised by VtMB, used by nothing
			TEXT("        whatever 1\n")
			TEXT("      }\n")
			TEXT("    }\n")
			TEXT("    event gesture \"g\" {\n")
			TEXT("      time 2.0 3.0\n")
			TEXT("      sequenceduration 1.25\n")
			TEXT("    }\n")
			TEXT("  }\n")
			TEXT("}\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/edges.vcd"), S);

		TestEqual(TEXT("three events despite the unknown block"), S.Events.Num(), 3);
		TestEqual(TEXT("the degenerate range is counted"), S.NumDegenerate, 1);

		// Events come out sorted by start time: gesture 2.0, expression 5.0, firetrigger 8.6.
		if (S.Events.Num() == 3)
		{
			TestTrue(TEXT("events are sorted by start time"),
				S.Events[0].StartTime <= S.Events[1].StartTime
				&& S.Events[1].StartTime <= S.Events[2].StartTime);
			TestTrue(TEXT("first is the gesture"), S.Events[0].Type == EElysiumChoreoEvent::Gesture);
			TestEqual(TEXT("sequenceduration is lifted"), S.Events[0].SequenceDuration, 1.25f);

			const FElysiumSceneEvent& Expr = S.Events[1];
			TestTrue(TEXT("second is the expression"), Expr.Type == EElysiumChoreoEvent::Expression);
			TestEqual(TEXT("a degenerate end is clamped to its start"), Expr.EndTime, Expr.StartTime);
			TestEqual(TEXT("the ramp has both samples"), Expr.Ramp.Num(), 2);
			TestEqual(TEXT("the ramp interpolates"), Expr.RampAt(0.5f), 0.5f);
			TestEqual(TEXT("  and clamps below"), Expr.RampAt(-1.f), 0.f);
			TestEqual(TEXT("  and clamps above"), Expr.RampAt(99.f), 1.f);

			const FElysiumSceneEvent& Fire = S.Events[2];
			TestTrue(TEXT("third is the firetrigger"), Fire.Type == EElysiumChoreoEvent::FireTrigger);
			TestFalse(TEXT("an end of -1 means instantaneous"), Fire.bHasEnd);
			TestEqual(TEXT("  and its end is held at its start"), Fire.EndTime, Fire.StartTime);
			TestEqual(TEXT("LatestTime uses the start when there is no end"), S.LatestTime, 8.6f);
		}

		// An event with no authored ramp plays at full intensity.
		TestEqual(TEXT("an absent ramp is full intensity"), S.Events[2].RampAt(0.f), 1.f);
	}

	// --- what the reader refuses, and what it tolerates ----------------------------------
	{
		FElysiumSceneData S;
		ElysiumScene::ParseText(TEXT(""), TEXT("test/empty.vcd"), S);
		TestFalse(TEXT("empty text is not a valid scene"), S.bValid);

		ElysiumScene::ParseText(TEXT("}}}\n{{{\nnonsense \"x\"\n"), TEXT("test/garbage.vcd"), S);
		TestFalse(TEXT("garbage names no actor, so it is not valid"), S.bValid);

		// No version line: 10 shipped files are like this and they are still playable.
		ElysiumScene::ParseText(TEXT("actor \"A\"\n{\n}\n"), TEXT("test/nover.vcd"), S);
		TestTrue(TEXT("a scene with no version line is still valid"), S.bValid);
		TestEqual(TEXT("  and reports no version"), S.Version, (int32)INDEX_NONE);
	}

	// --- unknown event types are counted, never dropped silently into a live type --------
	{
		FElysiumSceneData S;
		ElysiumScene::ParseText(
			TEXT("actor \"A\"\n{\n channel \"C\"\n {\n  event notarealtype \"n\"\n  {\n   time 0 1\n  }\n }\n}\n"),
			TEXT("test/unknown.vcd"), S);
		TestEqual(TEXT("an unknown event type still parses"), S.Events.Num(), 1);
		TestTrue(TEXT("  as Unknown"), S.Events[0].Type == EElysiumChoreoEvent::Unknown);
		TestEqual(TEXT("  and is counted as such"), S.CountOf(EElysiumChoreoEvent::Unknown), 1);
	}

	// --- SceneFile -> mirror key ---------------------------------------------------------
	{
		TestEqual(TEXT("the sound/ prefix is stripped and the path lowercased"),
			ElysiumScene::NormalizeSceneRel(TEXT("sound/Character/dlg/MAIN CHARACTERS/x.vcd")),
			FString(TEXT("character/dlg/main characters/x.vcd")));
		TestEqual(TEXT("backslashes fold to forward"),
			ElysiumScene::NormalizeSceneRel(TEXT("sound\\CINEMATIC\\tutorial\\jack_VS_sabbat.vcd")),
			FString(TEXT("cinematic/tutorial/jack_vs_sabbat.vcd")));
		TestEqual(TEXT("a path with no sound/ prefix is left alone but folded"),
			ElysiumScene::NormalizeSceneRel(TEXT("Cinematic/X.vcd")), FString(TEXT("cinematic/x.vcd")));
	}

	// --- the inline cache seam the entity tests drive scenes through ---------------------
	{
		ElysiumScene::ClearCache();
		ElysiumScene::RegisterInline(TEXT("test/inline.vcd"),
			TEXT("actor \"Jack\"\n{\n channel \"C\"\n {\n  event firetrigger \"t\"\n  {\n   time 1.0 -1\n   param \"2\"\n  }\n }\n}\n"));

		TSharedPtr<const FElysiumSceneData> Loaded = ElysiumScene::Load(TEXT("sound/test/inline.vcd"));
		TestTrue(TEXT("an inline scene loads back through the normalized key"), Loaded.IsValid());
		if (Loaded.IsValid())
		{
			TestEqual(TEXT("  with its event"), Loaded->Events.Num(), 1);
			TestEqual(TEXT("  and its trigger number"), Loaded->Events[0].Param, FString(TEXT("2")));
		}
		ElysiumScene::ClearCache();
	}

	return true;
}

// =====================================================================================
// 12.1 — the choreo timeline. Drives FElysiumScenePlayer directly against a recording callback,
// with no entity and no world: start/continue/end classification, the audio mixahead, and the
// two-condition completion test.
// =====================================================================================

namespace
{
	// Records the callback traffic as flat strings so a test can assert on order, not just counts.
	class FElysiumSceneRecorder final : public IElysiumChoreoCallback
	{
	public:
		TArray<FString> Log;

		virtual void StartEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("start:%s"), *E.Name));
		}
		virtual void ProcessEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("cont:%s"), *E.Name));
		}
		virtual void EndEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("end:%s"), *E.Name));
		}
		virtual void RestoreEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("restore:%s"), *E.Name));
		}

		bool Has(const TCHAR* Entry) const { return Log.Contains(Entry); }
		int32 CountOf(const TCHAR* Entry) const
		{
			int32 N = 0;
			for (const FString& S : Log) { if (S == Entry) { ++N; } }
			return N;
		}
		FString Joined() const { return FString::Join(Log, TEXT(",")); }
		void Clear() { Log.Reset(); }
	};

	TSharedPtr<const FElysiumSceneData> MakeTestScene(const FString& Text)
	{
		TSharedPtr<FElysiumSceneData> Data = MakeShared<FElysiumSceneData>();
		ElysiumScene::ParseText(Text, TEXT("test/timeline.vcd"), *Data);
		return Data;
	}

	// Build one `event` block in the shape the shipped corpus actually uses: the brace on its own
	// line. (Neither this reader nor research/tooling/probes/probe_scenes.py accepts a `{ … }` opened and closed on
	// one line — no shipped `.vcd` writes that, and the two grammars are kept identical.)
	FString SceneEvent(const TCHAR* Type, const TCHAR* Name, float Start, float End,
		const TCHAR* Param = nullptr)
	{
		FString S = FString::Printf(TEXT("    event %s \"%s\"\n    {\n      time %f %f\n"),
			Type, Name, Start, End);
		if (Param != nullptr)
		{
			S += FString::Printf(TEXT("      param \"%s\"\n"), Param);
		}
		return S + TEXT("    }\n");
	}

	FString SceneWith(const FString& Events)
	{
		return TEXT("// Choreo version 1\nactor \"A\"\n{\n  channel \"C\"\n  {\n")
			+ Events + TEXT("  }\n}\nfps 60\nsnap off\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneTimelineTest, "Elysium.Substrate.SceneTimeline", GElysiumTestFlags)

bool FElysiumSceneTimelineTest::RunTest(const FString&)
{
	// A ranged event (2..4), an instantaneous one (3), and a second ranged one (1..5).
	TSharedPtr<const FElysiumSceneData> Scene = MakeTestScene(SceneWith(
		SceneEvent(TEXT("sequence"), TEXT("ranged"), 2.f, 4.f)
		+ SceneEvent(TEXT("firetrigger"), TEXT("inst"), 3.f, -1.f, TEXT("1"))
		+ SceneEvent(TEXT("gesture"), TEXT("wide"), 1.f, 5.f)));

	TestEqual(TEXT("the fixture parsed"), Scene->Events.Num(), 3);

	// --- start / continue / end classification ------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, /*Latency=*/0.f, /*MaxDuration=*/600.f);

		TestEqual(TEXT("latest time is the last authored end"), P.GetLatest(), 5.f);
		TestFalse(TEXT("a scene at time 0 is not finished"), P.IsFinished());

		P.AdvanceTo(0.5f, R);
		TestEqual(TEXT("nothing has started yet"), R.Log.Num(), 0);

		P.AdvanceTo(1.5f, R);
		TestTrue(TEXT("the wide event starts"), R.Has(TEXT("start:wide")));
		TestEqual(TEXT("  and only it"), R.Log.Num(), 1);

		R.Clear();
		P.AdvanceTo(2.5f, R);
		TestTrue(TEXT("the ranged event starts"), R.Has(TEXT("start:ranged")));
		TestTrue(TEXT("  and the wide one continues"), R.Has(TEXT("cont:wide")));

		R.Clear();
		P.AdvanceTo(3.5f, R);
		TestTrue(TEXT("an instantaneous event starts"), R.Has(TEXT("start:inst")));
		// One disposition per frame: the frame that starts it does not also end it.
		TestFalse(TEXT("  and does not also end on that frame"), R.Has(TEXT("end:inst")));
		TestEqual(TEXT("  so all three are active"), P.ActiveEvents(), 3);

		R.Clear();
		P.AdvanceTo(4.5f, R);
		TestTrue(TEXT("the instantaneous event ends on the next frame"), R.Has(TEXT("end:inst")));
		TestTrue(TEXT("the ranged event ends when the clock passes its end"), R.Has(TEXT("end:ranged")));
		TestFalse(TEXT("a finished event does not restart"), R.Has(TEXT("start:ranged")));

		R.Clear();
		P.AdvanceTo(5.5f, R);
		TestTrue(TEXT("the wide event ends last"), R.Has(TEXT("end:wide")));
		TestEqual(TEXT("nothing is active"), P.ActiveEvents(), 0);
		TestTrue(TEXT("and the scene is finished"), P.IsFinished());
	}

	// --- completion needs BOTH halves ----------------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, 0.f, 600.f);

		// Jump past the last authored end in one advance. Everything starts on that frame; the
		// ends land on the following one, because a frame dispatches one disposition per event.
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("a long frame still starts every event"), R.Has(TEXT("start:wide"))
			&& R.Has(TEXT("start:ranged")) && R.Has(TEXT("start:inst")));
		TestEqual(TEXT("  each exactly once"), R.CountOf(TEXT("start:wide")), 1);
		TestFalse(TEXT("  and the scene is not finished while they are still active"), P.IsFinished());

		P.AdvanceTo(99.f, R);
		TestEqual(TEXT("the next frame ends them all"), P.ActiveEvents(), 0);
		TestTrue(TEXT("  so the scene is finished"), P.IsFinished());

		// Starts are dispatched in authored start-time order even inside one advance.
		const int32 IdxWide = R.Log.IndexOfByKey(FString(TEXT("start:wide")));
		const int32 IdxRanged = R.Log.IndexOfByKey(FString(TEXT("start:ranged")));
		const int32 IdxInst = R.Log.IndexOfByKey(FString(TEXT("start:inst")));
		TestTrue(TEXT("a long frame dispatches starts in start-time order"),
			IdxWide < IdxRanged && IdxRanged < IdxInst);
	}

	// --- past the end, but something still running: NOT finished -------------------------
	{
		// One event that never ends within the clamp: start 0, end 500.
		TSharedPtr<const FElysiumSceneData> Long = MakeTestScene(SceneWith(
			SceneEvent(TEXT("sequence"), TEXT("long"), 0.f, 500.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Long, 0.f, /*MaxDuration=*/10.f);   // clamp the scene end well inside the event

		P.AdvanceTo(11.f, R);
		TestTrue(TEXT("the event is running"), R.Has(TEXT("start:long")));
		TestEqual(TEXT("  and still active"), P.ActiveEvents(), 1);
		// Clock is past the (clamped) latest, but an event is live, so the scene must not end.
		TestFalse(TEXT("past the end but still active is not finished"), P.IsFinished());

		P.AdvanceTo(501.f, R);
		TestTrue(TEXT("once the event ends, so does the scene"), P.IsFinished());
	}

	// --- the max-duration clamp keeps a nonsense range from stalling completion -----------
	{
		// The corpus really contains an event running to ~1.5 million seconds.
		TSharedPtr<const FElysiumSceneData> Absurd = MakeTestScene(SceneWith(
			SceneEvent(TEXT("firetrigger"), TEXT("t"), 0.5f, -1.f, TEXT("1"))
			+ SceneEvent(TEXT("silence"), TEXT("s"), 0.f, 1500000.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Absurd, 0.f, /*MaxDuration=*/600.f);
		TestEqual(TEXT("the scene end is clamped"), P.GetLatest(), 600.f);

		P.AdvanceTo(601.f, R);
		TestTrue(TEXT("the real events still fired"), R.Has(TEXT("start:t")));
		// The absurd event is still active, so completion waits on StopActiveEvents — which is what
		// the entity does when the clock passes the clamp. Without the clamp there is no such point.
		P.StopActiveEvents(R);
		TestTrue(TEXT("stopping the stragglers ends the scene"), P.IsFinished());
		TestTrue(TEXT("  and the straggler got its end callback"), R.Has(TEXT("end:s")));
	}

	// --- the audio mixahead pulls speak starts earlier and pushes the scene end later ------
	{
		TSharedPtr<const FElysiumSceneData> Spoken = MakeTestScene(SceneWith(
			SceneEvent(TEXT("speak"), TEXT("line"), 1.f, 2.f)
			+ SceneEvent(TEXT("gesture"), TEXT("gest"), 1.f, 2.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Spoken, /*Latency=*/0.25f, 600.f);

		TestEqual(TEXT("the scene end is extended by the mixahead"), P.GetLatest(), 2.25f);

		P.AdvanceTo(0.8f, R);
		TestTrue(TEXT("the speak event starts early by the mixahead"), R.Has(TEXT("start:line")));
		TestFalse(TEXT("  while a non-speak event at the same time does not"), R.Has(TEXT("start:gest")));

		R.Clear();
		P.AdvanceTo(1.1f, R);
		TestTrue(TEXT("the gesture starts at its authored time"), R.Has(TEXT("start:gest")));
	}

	// --- Reset replays, PreLatchTo does not ----------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, 0.f, 600.f);
		P.AdvanceTo(99.f, R);

		R.Clear();
		P.Reset();
		TestEqual(TEXT("reset returns the clock to zero"), P.GetTime(), 0.f);
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("after a reset the scene replays"), R.Has(TEXT("start:wide")));

		// The save-restore path: everything already passed is latched silently.
		R.Clear();
		P.Reset();
		P.PreLatchTo(3.5f);
		TestEqual(TEXT("pre-latching fires nothing"), R.Log.Num(), 0);
		P.AdvanceTo(4.0f, R);
		TestFalse(TEXT("an event whose start already passed does not re-fire"), R.Has(TEXT("start:wide")));
		TestFalse(TEXT("  nor the instantaneous one"), R.Has(TEXT("start:inst")));
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("a restored scene still reaches completion"), P.IsFinished());
	}

	// --- exact restore rebuilds continuous events without replaying instantaneous outputs -
	{
		FElysiumScenePlayer Original;
		FElysiumSceneRecorder Before;
		Original.Begin(Scene, 0.f, 600.f);
		Original.AdvanceTo(3.5f, Before);
		TArray<uint8> Started;
		TArray<uint8> Active;
		Original.CaptureLatches(Started, Active);

		FElysiumScenePlayer Restored;
		FElysiumSceneRecorder After;
		Restored.Begin(Scene, 0.f, 600.f);
		Restored.RestoreLatches(3.5f, Started, Active, After);
		TestTrue(TEXT("restore reconstitutes the wide ranged event"), After.Has(TEXT("restore:wide")));
		TestTrue(TEXT("restore reconstitutes the ranged event"), After.Has(TEXT("restore:ranged")));
		TestFalse(TEXT("restore does not replay the instantaneous output"), After.Has(TEXT("restore:inst")));
		TestEqual(TEXT("the exact active latch count survives"), Restored.ActiveEvents(), 3);
		After.Clear();
		Restored.AdvanceTo(4.5f, After);
		TestFalse(TEXT("past starts remain exactly-once after restore"), After.Has(TEXT("start:inst")));
		TestTrue(TEXT("the restored instantaneous event still closes"), After.Has(TEXT("end:inst")));
	}

	// --- active 0 actors/channels remain inspectable but never enter the timeline ----------
	{
		const FString DisabledText = TEXT("// Choreo version 1\nactor \"A\"\n{\n  channel \"off\"\n  {\n")
			+ SceneEvent(TEXT("firetrigger"), TEXT("disabled"), 50.f, -1.f, TEXT("1"))
			+ TEXT("    active 0\n  }\n}\nfps 60\n");
		TSharedPtr<const FElysiumSceneData> Disabled = MakeTestScene(DisabledText);
		TestEqual(TEXT("disabled events remain parsed"), Disabled->Events.Num(), 1);
		TestFalse(TEXT("the event inherits its channel's disabled state"), Disabled->Events[0].bActive);
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Disabled, 0.f, 600.f);
		TestEqual(TEXT("disabled content does not extend scene duration"), P.GetLatest(), 0.f);
		P.AdvanceTo(100.f, R);
		TestEqual(TEXT("disabled content dispatches nothing"), R.Log.Num(), 0);
		TestTrue(TEXT("and cannot stall completion"), P.IsFinished());
	}

	// --- an unbound player is inert -------------------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		TestFalse(TEXT("an unbound player is not bound"), P.IsBound());
		P.AdvanceTo(5.f, R);          // must not crash
		P.StopActiveEvents(R);
		TestEqual(TEXT("an unbound player dispatches nothing"), R.Log.Num(), 0);
	}

	return true;
}

// =====================================================================================
// 12.1 — `logic_choreographed_scene` end to end, through the real world and event queue.
//
// The scene text is seeded into the parse cache inline, so this stays in the content-free tier.
// Outputs land on a math_counter with distinct Add values, which is what lets one number prove
// both which outputs fired and in what order they did not.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChoreoSceneTest, "Elysium.Substrate.ChoreoScene", GElysiumTestFlags)

bool FElysiumChoreoSceneTest::RunTest(const FString&)
{
	auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Param)
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = Param;
		On.Outputs.Add(W);
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.f;
	};

	// One scene: an actor named "actor1", a 4-second sequence, and triggers 1, 2 and 7 (the last
	// out of range and therefore fired at nothing).
	ElysiumScene::RegisterInline(TEXT("test/scene.vcd"), SceneWith(
		SceneEvent(TEXT("sequence"), TEXT("body"), 0.f, 4.f, TEXT("some_clip"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("t1"), 1.f, -1.f, TEXT("1"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("t2"), 2.f, -1.f, TEXT("2"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("bad"), 2.5f, -1.f, TEXT("7"))));

	auto BuildWorld = [&](FElysiumEntityWorld& World, int32 PositionStart, int32 PositionEnd,
		const TCHAR* SceneFile, const TCHAR* ActorName)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("scene1");
		S.Origin = FVector(500.f, 600.f, 700.f);
		S.Keys.Add(TEXT("SceneFile"), SceneFile);
		S.Keys.Add(TEXT("angles"), TEXT("0 90 0"));
		S.Keys.Add(TEXT("position_start"), FString::FromInt(PositionStart));
		S.Keys.Add(TEXT("position_end"), FString::FromInt(PositionEnd));
		Wire(S, TEXT("OnStart"), TEXT("1"));
		Wire(S, TEXT("OnCompletion"), TEXT("10"));
		Wire(S, TEXT("OnCanceled"), TEXT("100"));
		Wire(S, TEXT("OnTrigger1"), TEXT("1000"));
		Wire(S, TEXT("OnTrigger2"), TEXT("10000"));
		Defs.Defs.Add(MoveTemp(S));

		// A point entity the world will place: the scene drives SetRuntimeOrigin, which is on the
		// base, so a logic_relay stands in for an NPC exactly as the scripted_sequence test does.
		FElysiumEntityDef A;
		A.Classname = TEXT("logic_relay");
		A.TargetName = ActorName;
		A.Origin = FVector(10.f, 20.f, 30.f);
		Defs.Defs.Add(MoveTemp(A));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));

		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
	};

	// --- bool keyfields read numerically, not as string truthiness -------------------------
	// A map keyvalue is a String, and Source reads a bool key as `atoi(v) != 0`. Reading it with
	// Python truthiness instead makes the literal "0" true, which silently flips every
	// map-authored `hide_ents "0"` on (sp_theatre writes exactly that).
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("flags1");
		S.Keys.Add(TEXT("SceneFile"), TEXT("test/scene.vcd"));
		S.Keys.Add(TEXT("hide_ents"), TEXT("0"));
		S.Keys.Add(TEXT("full_sound"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(S));
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		const FElysiumEntity* Ent = World.FindByName(TEXT("flags1"));
		if (TestNotNull(TEXT("flags1 resolved"), Ent))
		{
			const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			const FElysiumFieldAccessor* Hide = Reg.FindField(*Ent->Class, FName(TEXT("hide_ents")));
			const FElysiumFieldAccessor* Full = Reg.FindField(*Ent->Class, FName(TEXT("full_sound")));
			if (TestNotNull(TEXT("hide_ents is a registered field"), Hide)
				&& TestNotNull(TEXT("full_sound is a registered field"), Full))
			{
				TestFalse(TEXT("hide_ents \"0\" reads false"), Hide->Get(*Ent).ToBool());
				TestTrue(TEXT("full_sound \"1\" reads true"), Full->Get(*Ent).ToBool());
			}
		}
	}

	// --- the ordinary run: start, triggers, completion -------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 0, /*position_end*/ 0, TEXT("test/scene.vcd"), TEXT("A"));

		FElysiumEntity* SceneEnt = World.FindByName(TEXT("scene1"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("logic_choreographed_scene registers a leaf class"), SceneEnt)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}
		TestFalse(TEXT("it is not an inert record"), SceneEnt->IsRecordOnly());

		double T = 0.0;
		World.Tick(T);
		TestEqual(TEXT("an untriggered scene fires nothing"), CounterValue(Count), 0.f);

		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		TestEqual(TEXT("Start fires OnStart"), CounterValue(Count), 1.f);

		// A second Start while playing is ignored — VtMB's first gate.
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 0.5; World.Tick(T);
		TestEqual(TEXT("a second Start while playing does nothing"), CounterValue(Count), 1.f);

		T = 1.2; World.Tick(T);
		TestEqual(TEXT("trigger 1 fires when the clock crosses it"), CounterValue(Count), 1001.f);

		T = 1.5; World.Tick(T);
		TestEqual(TEXT("  and does not fire twice"), CounterValue(Count), 1001.f);

		T = 2.2; World.Tick(T);
		TestEqual(TEXT("trigger 2 fires"), CounterValue(Count), 11001.f);

		T = 2.8; World.Tick(T);
		TestEqual(TEXT("an out-of-range trigger number fires nothing"), CounterValue(Count), 11001.f);

		T = 3.9; World.Tick(T);
		TestEqual(TEXT("completion has not fired before the last event ends"), CounterValue(Count), 11001.f);

		// Past the end: one tick ends the sequence event, the next reports finished.
		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("OnCompletion fires once the scene ends"), CounterValue(Count), 11011.f);

		T = 6.0; World.Tick(T);
		TestEqual(TEXT("  and only once"), CounterValue(Count), 11011.f);
	}

	// --- Pause keeps the original start base; Resume catches up missed events ---------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Pause")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 0.5; World.Tick(T);
		T = 2.2; World.Tick(T);
		TestEqual(TEXT("paused scene dispatches no missed triggers"), CounterValue(Count), 1.f);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Resume")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 2.3; World.Tick(T);
		T = 2.31; World.Tick(T);
		TestEqual(TEXT("resume catches up both wall-clock triggers exactly once"), CounterValue(Count), 11001.f);
	}

	// --- A mid-scene snapshot restores latches without duplicating past outputs ------------
	{
		FElysiumEntityWorld Before(nullptr, nullptr);
		BuildWorld(Before, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		double T = 0.0;
		Before.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		Before.Tick(T);
		T = 1.2; Before.Tick(T);
		FElysiumMapSnapshot Snapshot;
		Before.Freeze(Snapshot);

		FElysiumEntityWorld After(nullptr, nullptr);
		BuildWorld(After, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		After.Tick(T); // match the saved wall clock before the leaf computes its restored base
		After.ApplySnapshot(Snapshot);
		const FElysiumEntity* Count = After.FindByName(TEXT("counter1"));
		TestEqual(TEXT("snapshot carried the first trigger"), CounterValue(Count), 1001.f);
		T = 1.3; After.Tick(T);
		TestEqual(TEXT("the first trigger is not replayed after restore"), CounterValue(Count), 1001.f);
		T = 2.2; After.Tick(T);
		TestEqual(TEXT("the future trigger still dispatches"), CounterValue(Count), 11001.f);
		T = 4.5; After.Tick(T);
		T = 4.6; After.Tick(T);
		TestEqual(TEXT("the restored scene completes exactly once"), CounterValue(Count), 11011.f);
	}

	// --- Cancel suppresses completion, permanently -----------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		T = 1.2; World.Tick(T);
		TestEqual(TEXT("the scene is running"), CounterValue(Count), 1001.f);

		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Cancel")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 1.3; World.Tick(T);
		TestEqual(TEXT("Cancel fires OnCanceled"), CounterValue(Count), 1101.f);

		// Keep ticking well past the scene's end: OnCompletion must never arrive.
		for (int32 i = 0; i < 10; ++i) { T += 1.0; World.Tick(T); }
		TestEqual(TEXT("a cancelled scene never completes"), CounterValue(Count), 1101.f);
	}

	// --- position_start places and re-pins; position_end 2 restores --------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 1, /*position_end*/ 2, TEXT("test/scene.vcd"), TEXT("A"));

		FElysiumEntity* Actor = World.FindByName(TEXT("A"));
		const FVector Home(10.f, 20.f, 30.f);
		const FVector Mark(500.f, 600.f, 700.f);
		TestEqual(TEXT("the actor starts at home"), Actor->Origin, Home);

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		TestEqual(TEXT("position_start 1 places the actor on the scene"), Actor->Origin, Mark);

		// Move it away mid-scene; the re-pin must drag it back.
		Actor->SetRuntimeOrigin(FVector(-1.f, -1.f, -1.f));
		T = 1.0; World.Tick(T);
		TestEqual(TEXT("  and re-pins it every frame"), Actor->Origin, Mark);

		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("position_end 2 restores the transform saved at Start"), Actor->Origin, Home);
	}

	// --- position_end 0 leaves the actor where the scene left it -----------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 1, /*position_end*/ 0, TEXT("test/scene.vcd"), TEXT("A"));
		FElysiumEntity* Actor = World.FindByName(TEXT("A"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("position_end 0 leaves the actor on the mark"),
			Actor->Origin, FVector(500.f, 600.f, 700.f));
	}

	// --- an actor the map does not have: its events drop, the scene still completes ----------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("someone_else"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		for (double Step = 0.5; Step < 6.0; Step += 0.5) { World.Tick(Step); }
		// Every output still fires: an unresolved actor drops its own events, not the scene.
		TestEqual(TEXT("a scene with an unbound actor still runs its triggers and completes"),
			CounterValue(Count), 11011.f);
	}

	// --- a SceneFile that resolves to nothing is a silent no-op ------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/does_not_exist.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		for (double Step = 0.5; Step < 6.0; Step += 0.5) { World.Tick(Step); }
		TestEqual(TEXT("an unresolvable SceneFile fires nothing at all"), CounterValue(Count), 0.f);
	}

	ElysiumScene::ClearCache();
	return true;
}

// =====================================================================================
// Skeleton-bound animation resolution — cache identity and cinematic root selection.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationBindingIdentityTest,
	"Elysium.Substrate.AnimationBindingIdentity", GElysiumTestFlags)
bool FElysiumAnimationBindingIdentityTest::RunTest(const FString&)
{
	const FString Clip(TEXT("entire_scene"));
	const FString Bank(TEXT("cinematic_santa_monica_courtroom_courtroom_bip1__bip02"));
	const FString Sheriff = ElysiumEntityAnimation::CinematicClipCacheKey(
		TEXT("sheriff"), Bank, Clip);
	const FString Sire = ElysiumEntityAnimation::CinematicClipCacheKey(
		TEXT("doppleganger_male"), Bank, Clip);
	TestFalse(TEXT("one cinematic bank retargeted to two models has two cache identities"),
		Sheriff == Sire);
	TestFalse(TEXT("ordinary and cinematic cache namespaces cannot alias"),
		Sheriff == ElysiumEntityAnimation::NpcClipCacheKey(TEXT("sheriff"), Clip));
	const FString NormalVisual = ElysiumEntityAnimation::NpcVisualCacheKey(
		TEXT("malkavian_male_armor_2"), false);
	const FString PlayerVisual = ElysiumEntityAnimation::NpcVisualCacheKey(
		TEXT("malkavian_male_armor_2"), true);
	TestFalse(TEXT("normal NPC and masked player meshes have distinct cache identities"),
		NormalVisual == PlayerVisual);
	TestFalse(TEXT("their skeleton-bound clips cannot alias either"),
		ElysiumEntityAnimation::NpcClipCacheKey(NormalVisual, Clip)
			== ElysiumEntityAnimation::NpcClipCacheKey(PlayerVisual, Clip));

	FElysiumNpcAnimProxy Proxy;
	UAnimSequence* Sequence = NewObject<UAnimSequence>();
	Proxy.Request(Sequence, /*bLoop=*/true, 0.25f);
	TestTrue(TEXT("the initial stance loops"), Proxy.IsPlayingLoop());
	Proxy.Request(Sequence, /*bLoop=*/false, 0.25f);
	TestFalse(TEXT("the same clip can change from a loop to a one-shot"), Proxy.IsPlayingLoop());
	Proxy.Request(Sequence, /*bLoop=*/true, 0.25f);
	TestTrue(TEXT("reset-to-idle restores looping on the same clip"), Proxy.IsPlayingLoop());

	FElysiumCinematicSet Multi;
	Multi.Roots.Add(TEXT("bip01"), TEXT("bank_one"));
	Multi.Roots.Add(TEXT("bip02"), TEXT("bank_two"));
	TestEqual(TEXT("cinematic roots match case-insensitively"),
		Multi.BankForRoot(TEXT("Bip02")), FString(TEXT("bank_two")));
	TestTrue(TEXT("an unknown root does not animate an arbitrary actor"),
		Multi.BankForRoot(TEXT("Bip99")).IsEmpty());
	TestTrue(TEXT("an empty root is ambiguous on a multi-actor set"),
		Multi.BankForRoot(FString()).IsEmpty());

	FElysiumCinematicSet Single;
	Single.Roots.Add(TEXT("bip01"), TEXT("solo_bank"));
	TestEqual(TEXT("a single-actor set may omit bonerename"),
		Single.BankForRoot(FString()), FString(TEXT("solo_bank")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAudioContractsTest,
	"Elysium.Substrate.AudioContracts", GElysiumTestFlags)

bool FElysiumAudioContractsTest::RunTest(const FString&)
{
	TestEqual(TEXT("leading sound and slash/case normalize once"),
		UElysiumAudioSubsystem::NormalizeSourcePath(
			TEXT(" /Sound\\Character/Dlg/Jack/LINE1_COL_E.MP3 ")),
		FString(TEXT("character/dlg/jack/line1_col_e.mp3")));
	TestEqual(TEXT("dialogue source derives its NPC line stem"),
		FElysiumLineService::DialogueLineSource(
			TEXT("E:/game/dlg/Main Characters/jack_tutorial.dlg"), 42),
		FString(TEXT("character/dlg/Main Characters/jack_tutorial/line42_col_e")));

	const FElysiumVoiceHandle First{ 7, 2 };
	const FElysiumVoiceHandle Reused{ 7, 3 };
	TestTrue(TEXT("generation distinguishes a reused slot"), First != Reused);
	TSet<FElysiumVoiceHandle> Handles;
	Handles.Add(First);
	TestFalse(TEXT("stale generation cannot find a reused voice"), Handles.Contains(Reused));

	TSharedPtr<FElysiumSoundCache::FDecoded, ESPMode::ThreadSafe> Pcm =
		MakeShared<FElysiumSoundCache::FDecoded, ESPMode::ThreadSafe>();
	Pcm->Info.Channels = 1;
	Pcm->Info.SampleRate = 4;
	Pcm->Info.FrameCount = 4;
	Pcm->Info.DurationSeconds = 1.f;
	const int16 Samples[] = { MIN_int16, -1, 0, MAX_int16 };
	Pcm->Pcm16.Append(reinterpret_cast<const uint8*>(Samples), sizeof(Samples));

	FSoundGeneratorInitParams GeneratorParams;
	GeneratorParams.NumChannels = 1;
	GeneratorParams.NumFramesPerCallback = 8;
	GeneratorParams.StartTime = 0.f;

	USoundWaveProcedural* OneShot = FElysiumSoundCache::MakeWave(Pcm, false);
	TestNotNull(TEXT("one-shot wave is created"), OneShot);
	if (OneShot)
	{
		TestEqual(TEXT("one-shot advances to EOF while inaudible"),
			OneShot->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
		ISoundGeneratorPtr Generator = OneShot->CreateSoundGenerator(GeneratorParams);
		float Out[8] = {};
		TestEqual(TEXT("one-shot generator stops at decoded EOF"),
			Generator->GetNextBuffer(Out, UE_ARRAY_COUNT(Out)), 4);
		TestTrue(TEXT("one-shot generator reports completion"), Generator->IsFinished());
	}

	USoundWaveProcedural* Loop = FElysiumSoundCache::MakeWave(Pcm, true);
	TestNotNull(TEXT("looping wave is created"), Loop);
	if (Loop)
	{
		TestEqual(TEXT("loop retains phase while inaudible"),
			Loop->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
		ISoundGeneratorPtr Generator = Loop->CreateSoundGenerator(GeneratorParams);
		float Out[10] = {};
		TestEqual(TEXT("looping generator fills across sample wrap"),
			Generator->GetNextBuffer(Out, UE_ARRAY_COUNT(Out)), 8);
		TestFalse(TEXT("looping generator does not report completion"), Generator->IsFinished());
		TestEqual(TEXT("loop wrap restarts at the first sample"), Out[4], Out[0]);
	}

	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Base = Registry.BaseDesc();
	TestNotNull(TEXT("base entity descriptor exists"), Base);
	if (Base)
	{
		TestTrue(TEXT("PlayDialogFile is a real base input"),
			Registry.FindInput(*Base, TEXT("PlayDialogFile")) != nullptr);
		TestTrue(TEXT("SetSoundOverrideEnt is a real base input"),
			Registry.FindInput(*Base, TEXT("SetSoundOverrideEnt")) != nullptr);
		TestTrue(TEXT("SetFakeSilence is a real base input"),
			Registry.FindInput(*Base, TEXT("SetFakeSilence")) != nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightRigTest,
	"Elysium.Substrate.LightRig", GElysiumTestFlags)

bool FElysiumLightRigTest::RunTest(const FString&)
{
	IFileManager::Get().MakeDirectory(*FPaths::AutomationTransientDir(), /*Tree*/ true);
	const FString LightsPath = FPaths::CreateTempFilename(
		*FPaths::AutomationTransientDir(), TEXT("ElysiumLightRig_"), TEXT(".lights"));
	const FString Sidecar =
		TEXT("1 0 0 0 0 0 0 100 50 25 1000 0 0 1 0 0\n")
		TEXT("2 0 0 0 1 0 0 50 40 30 2000 0.9396926 0.7660444 1 0 0\n")
		TEXT("3 0 0 0 0 0 -1 1 1 1 0 0 0 1 0 0\n");
	TestTrue(TEXT("synthetic light sidecar writes"), FFileHelper::SaveStringToFile(Sidecar, *LightsPath));

	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>();
	UPointLightComponent* Point = NewObject<UPointLightComponent>();
	USpotLightComponent* Spot = NewObject<USpotLightComponent>();
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>();
	Point->SetWorldLocation(FVector(100.f, 200.f, 300.f));

	TArray<UElysiumLightRig::FAdoptedLight> Adopted;
	Adopted.Add({Point, 0});
	Adopted.Add({Spot, 1});
	Adopted.Add({Sun, 2});
	TestEqual(TEXT("all synthetic sources adopt"), Rig->Adopt(Adopted, LightsPath), 3);

	TestFalse(TEXT("point baseline is non-inverse-square"), Point->bUseInverseSquaredFalloff != 0);
	TestTrue(TEXT("local source explicitly allows MegaLights"), Point->bAllowMegaLights != 0);
	TestEqual(TEXT("local source pins RT MegaLights shadows"),
		Point->MegaLightsShadowMethod.GetValue(), EMegaLightsShadowMethod::RayTracing);
	TestTrue(TEXT("point shadows are wired from rig calibration"), Point->CastShadows != 0);
	TestTrue(TEXT("spot inner cone comes from stopdot"), FMath::IsNearlyEqual(Spot->InnerConeAngle, 20.f, 0.05f));
	TestTrue(TEXT("spot outer cone comes from stopdot2"), FMath::IsNearlyEqual(Spot->OuterConeAngle, 40.f, 0.05f));

	Rig->SetNonSpotSourcesDisabled(true);
	TestTrue(TEXT("volumetric batch disables point"), Rig->IsSourceDisabled(0));
	TestFalse(TEXT("volumetric batch leaves spot alone"), Rig->IsSourceDisabled(1));
	TestTrue(TEXT("volumetric batch disables sun"), Rig->IsSourceDisabled(2));
	Rig->SetNonSpotSourcesDisabled(false);

	Point->SetSourceRadius(125.f);
	Point->SetIndirectLightingIntensity(3.f);
	Point->SetWorldLocation(FVector(999.f));
	Rig->SetSourceOverridden(0, true);
	Rig->RevertSource(0);
	TestFalse(TEXT("revert returns source to calibration"), Rig->IsSourceOverridden(0));
	TestTrue(TEXT("revert restores source shape"), FMath::IsNearlyZero(Point->SourceRadius));
	TestTrue(TEXT("revert restores Lumen contribution"),
		FMath::IsNearlyEqual(Point->IndirectLightingIntensity, 1.f));
	TestTrue(TEXT("revert restores authored transform"),
		Point->GetComponentLocation().Equals(FVector(100.f, 200.f, 300.f)));

	// The map-load contract restores global calibration first, then the complete override by the
	// stable sidecar index. Use the test's unique sidecar stem so no real survey can collide.
	const FString EditPath = FElysiumContentPaths::LightEdits(FPaths::GetBaseFilename(LightsPath));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::LightEditsDir(), /*Tree*/ true);
	const FString SavedEdit = TEXT(R"JSON({
		"calibration": {
			"point_spot_scale": 0.004,
			"max_brightness": 12.0,
			"falloff_exponent": 1.5,
			"radius_scale": 1.25,
			"indirect_lighting_scale": 1.5,
			"volumetric_scattering_scale": 0.75,
			"point_shadows": false
		},
		"edits": [{
			"index": 0,
			"disabled": true,
			"overridden": true,
			"intensity": 2.5,
			"pos_cm": [10.0, 20.0, 30.0],
			"rot_deg": [0.0, 45.0, 0.0],
			"color": [0.2, 0.4, 0.8],
			"reach_cm": 3456.0,
			"falloff_exponent": 2.25,
			"source_radius_cm": 75.0,
			"soft_source_radius_cm": 50.0,
			"source_length_cm": 120.0,
			"indirect_lighting_scale": 2.0,
			"volumetric_scatter": 0.5,
			"specular_scale": 0.25,
			"cast_shadows": true,
			"cast_volumetric_shadow": false
		}]
	})JSON");
	TestTrue(TEXT("synthetic light edit writes"), FFileHelper::SaveStringToFile(SavedEdit, *EditPath));
	FString LoadMessage;
	TestTrue(TEXT("saved light edit loads"), Rig->LoadSurvey(LoadMessage));
	TestTrue(TEXT("saved calibration restores"), FMath::IsNearlyEqual(Rig->PointSpotScale, 0.004f));
	TestTrue(TEXT("saved override restores"), Rig->IsSourceOverridden(0));
	TestTrue(TEXT("saved disabled state restores"), Rig->IsSourceDisabled(0));
	TestTrue(TEXT("saved intensity restores"), FMath::IsNearlyEqual(Point->Intensity, 2.5f));
	TestTrue(TEXT("saved transform restores"), Point->GetComponentLocation().Equals(FVector(10.f, 20.f, 30.f)));
	TestTrue(TEXT("saved reach restores"), FMath::IsNearlyEqual(Point->AttenuationRadius, 3456.f));
	TestTrue(TEXT("saved source shape restores"),
		FMath::IsNearlyEqual(Point->SourceRadius, 75.f)
		&& FMath::IsNearlyEqual(Point->SoftSourceRadius, 50.f)
		&& FMath::IsNearlyEqual(Point->SourceLength, 120.f));
	TestTrue(TEXT("saved light transport restores"),
		FMath::IsNearlyEqual(Point->IndirectLightingIntensity, 2.f)
		&& FMath::IsNearlyEqual(Point->VolumetricScatteringIntensity, 0.5f)
		&& FMath::IsNearlyEqual(Point->SpecularScale, 0.25f));
	TestTrue(TEXT("saved shadow overrides restore"), Point->CastShadows != 0
		&& Point->bCastVolumetricShadow == 0);

	IFileManager::Get().Delete(*EditPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	IFileManager::Get().Delete(*LightsPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
