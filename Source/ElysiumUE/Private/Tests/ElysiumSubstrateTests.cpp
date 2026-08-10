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

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
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
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
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

// A test-only leaf for the interaction world's captured-session rules. Production classes opt
// into these same virtuals when terminals/containers land; keeping this leaf here proves the
// foundation without prematurely making one of those content classes actionable.
class FElysiumTestUseSessionEntity final : public FElysiumEntity
{
public:
	int32 BeginCount = 0;
	int32 EndCount = 0;
	int32 EnterCount = 0;
	int32 LeaveCount = 0;
	EElysiumUseEndReason LastEndReason = EElysiumUseEndReason::Cancelled;

	static int32 TeardownEndCount;

	virtual bool IsUsable() const override { return true; }
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext&) override
	{
		++BeginCount;
		return FElysiumUseBeginResult::Started(
			TargetName == TEXT("explicit")
				? EElysiumUseSessionKind::Explicit : EElysiumUseSessionKind::WhileHeld);
	}
	virtual void EndPlayerUse(const FElysiumUseContext&, EElysiumUseEndReason Reason) override
	{
		++EndCount;
		LastEndReason = Reason;
		if (Reason == EElysiumUseEndReason::WorldTeardown)
		{
			++TeardownEndCount;
		}
	}
	virtual void OnUseCursorEnter() override { ++EnterCount; }
	virtual void OnUseCursorLeave() override { ++LeaveCount; }
};

int32 FElysiumTestUseSessionEntity::TeardownEndCount = 0;

static TUniquePtr<FElysiumEntity> MakeTestUseSessionEntity()
{
	return MakeUnique<FElysiumTestUseSessionEntity>();
}

static FElysiumClassRegistrar GTestUseSessionRegistrar(
	TEXT("test_use_session"), ElysiumBaseClassName(), &MakeTestUseSessionEntity,
	[](FElysiumClassDesc&) {});

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSignDismissPolicyTest,
	"Elysium.Substrate.SignDismissPolicy", GElysiumTestFlags)
bool FElysiumSignDismissPolicyTest::RunTest(const FString&)
{
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Activate(10.0);
	TSharedPtr<FElysiumSignData> Sign = MakeShared<FElysiumSignData>();
	Sign->bParsed = true;
	Sign->bCloseOnLeftClick = true;
	Sign->MinShowTime = 2.0f;
	World.OpenSign(FElysiumEntityHandle(0, World.GetEpoch()), Sign, 0.0f);

	TestFalse(TEXT("sign dwell blocks immediate UI dismissal"), World.CanPlayerDismissSign());
	TestFalse(TEXT("blocked dismissal reports rejection"), World.PlayerDismissSign());
	TestTrue(TEXT("blocked dismissal leaves the sign open"), World.GetOpenSign().IsSet());
	World.Tick(11.99);
	TestFalse(TEXT("sign remains blocked before minimum dwell"), World.CanPlayerDismissSign());
	World.Tick(12.0);
	TestTrue(TEXT("sign becomes dismissible at minimum dwell"), World.CanPlayerDismissSign());
	TestTrue(TEXT("authorized dismissal reports acceptance"), World.PlayerDismissSign());
	TestFalse(TEXT("authorized dismissal closes the sign"), World.GetOpenSign().IsSet());

	World.Tick(20.0);
	Sign->bCloseOnLeftClick = false;
	Sign->MinShowTime = 0.0f;
	World.OpenSign(FElysiumEntityHandle(0, World.GetEpoch()), Sign, 0.0f);
	TestFalse(TEXT("CloseOnLeftClick false rejects UI dismissal"), World.CanPlayerDismissSign());
	World.Tick(200.0);
	TestFalse(TEXT("forbidden click-close reports rejection"), World.PlayerDismissSign());
	TestTrue(TEXT("forbidden click-close stays open after any dwell"), World.GetOpenSign().IsSet());
	World.CloseSign(true);
	TestFalse(TEXT("scripted close still closes a click-forbidden sign"), World.GetOpenSign().IsSet());
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

	// Enhanced Input reports a 2D stick as (right, up); the user command stores (forward, right).
	const FVector2D StickMove = ElysiumInput::GamepadStickToMove(FVector2D(0.25f, 0.75f));
	TestEqual(TEXT("stick up becomes command forward"), (float)StickMove.X, 0.75f, 0.001f);
	TestEqual(TEXT("stick right becomes command side"), (float)StickMove.Y, 0.25f, 0.001f);
	Builder.SetAnalogMove(StickMove);
	Builder.SetButton(EElysiumButton::Forward, true);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("keyboard and analog forward compose then clamp"), (float)Cmd.Move.X, 1.0f, 0.001f);
	TestEqual(TEXT("analog side survives composition"), (float)Cmd.Move.Y, 0.25f, 0.001f);
	Builder.SetButton(EElysiumButton::Forward, false);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("analog movement is consumed each frame"), (float)Cmd.Move.X, 0.0f, 0.001f);

	// Mouse counts accumulate within a frame and are consumed by the build, never carried over.
	Builder.AddLook(1.5f, -0.5f);
	Builder.AddLook(0.5f, 0.25f);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("look accumulates"), (float)Cmd.LookDelta.X, 2.0f, 0.001f);
	TestEqual(TEXT("look accumulates on pitch too"), (float)Cmd.LookDelta.Y, -0.25f, 0.001f);
	Cmd = Builder.Build(1.0f / 60.0f);
	TestEqual(TEXT("the accumulator is consumed"), (float)Cmd.LookDelta.X, 0.0f, 0.001f);

	// --- The look curve is the MOUSE's alone (CCC3) ----------------------------------------
	// Three sources reach LookDelta and only one of them is a hand on a mouse. Build the same frame
	// twice under a deliberately extreme curve, once with mouse counts and once without: the
	// difference must be exactly what ShapeMouseLook returns, which means the turn key's
	// `KeyboardYawSpeed * dt` and the stick's rate passed through untouched. Moving the curve after
	// the merge in `Build` reddens this.
	ElysiumInput::FElysiumLookTuning Curved;
	Curved.Curve = 2.0f;
	Builder.SetLookTuning(Curved);
	const float CurveStep = 1.0f / 60.0f;
	const FVector2D StickLook(30.0f, 12.0f);
	const FVector2D MouseCounts(6.0f, -2.0f);

	Builder.SetButton(EElysiumButton::Right, true);
	Builder.SetAnalogLook(StickLook);
	const FElysiumUserCmd Dry = Builder.Build(CurveStep);   // keyboard + stick, no mouse

	Builder.SetAnalogLook(StickLook);
	Builder.AddLook(MouseCounts.X, MouseCounts.Y);
	const FElysiumUserCmd Wet = Builder.Build(CurveStep);   // the same frame, plus mouse
	Builder.SetButton(EElysiumButton::Right, false);
	Builder.SetLookTuning(ElysiumInput::FElysiumLookTuning());

	const FVector2D Shaped = ElysiumInput::ShapeMouseLook(MouseCounts, Curved, CurveStep);
	TestTrue(TEXT("the curve shaped the mouse counts at all"), !Shaped.Equals(MouseCounts, 1e-6));
	TestEqual(TEXT("the curve moves the yaw by exactly the shaped mouse delta"),
		(float)(Wet.LookDelta.X - Dry.LookDelta.X), (float)Shaped.X, 0.001f);
	TestEqual(TEXT("the curve moves the pitch by exactly the shaped mouse delta"),
		(float)(Wet.LookDelta.Y - Dry.LookDelta.Y), (float)Shaped.Y, 0.001f);
	// And the keyboard term is the unshaped one it always was, curve or no curve.
	TestEqual(TEXT("the turn key is untouched by the mouse curve"), (float)Dry.LookDelta.X,
		ElysiumInput::KeyboardYawSpeed * CurveStep + (float)StickLook.X * CurveStep, 0.001f);

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
	Builder.SetButton(EElysiumButton::Jump, true);       // Started -> +jump
	TestTrue(TEXT("jump Started latches the command"),
		Builder.Build(0.016f).IsDown(EElysiumButton::Jump));
	Builder.SetButton(EElysiumButton::Jump, false);      // Completed -> -jump
	TestFalse(TEXT("jump Completed releases the command"),
		Builder.Build(0.016f).IsDown(EElysiumButton::Jump));
	Builder.SetButton(EElysiumButton::Jump, true);
	Builder.ClearButtons();                              // Canceled/context removal -> clear
	TestFalse(TEXT("jump cancellation cannot leave a latch"),
		Builder.Build(0.016f).IsDown(EElysiumButton::Jump));

	// +use is the same command value in live and replay. The interaction world consumes these two
	// edges after focus settles; a held frame does not create a second press.
	FElysiumUserCmd UseUp;
	FElysiumUserCmd UseDown;
	UseDown.Buttons = static_cast<uint64>(EElysiumButton::Use);
	TestTrue(TEXT("use press is one command edge"),
		UseDown.JustPressed(EElysiumButton::Use, UseUp));
	TestFalse(TEXT("held use does not repeat"),
		UseDown.JustPressed(EElysiumButton::Use, UseDown));
	TestTrue(TEXT("use release is one command edge"),
		UseUp.JustReleased(EElysiumButton::Use, UseDown));
	const FElysiumUserCmd LiveUseFrames[] = { UseUp, UseDown, UseDown, UseUp };
	TArray<EElysiumUseEdge> LiveUseEdges;
	TArray<EElysiumUseEdge> ReplayUseEdges;
	for (int32 Index = 1; Index < UE_ARRAY_COUNT(LiveUseFrames); ++Index)
	{
		if (LiveUseFrames[Index].JustPressed(EElysiumButton::Use, LiveUseFrames[Index - 1]))
		{
			LiveUseEdges.Add(EElysiumUseEdge::Pressed);
		}
		if (LiveUseFrames[Index].JustReleased(EElysiumButton::Use, LiveUseFrames[Index - 1]))
		{
			LiveUseEdges.Add(EElysiumUseEdge::Released);
		}
		const FElysiumUserCmd Replayed = LiveUseFrames[Index];
		const FElysiumUserCmd ReplayPrevious = LiveUseFrames[Index - 1];
		if (Replayed.JustPressed(EElysiumButton::Use, ReplayPrevious))
		{
			ReplayUseEdges.Add(EElysiumUseEdge::Pressed);
		}
		if (Replayed.JustReleased(EElysiumButton::Use, ReplayPrevious))
		{
			ReplayUseEdges.Add(EElysiumUseEdge::Released);
		}
	}
	TestEqual(TEXT("live use produces press then release"), LiveUseEdges.Num(), 2);
	TestTrue(TEXT("replay produces identical use edges"), LiveUseEdges == ReplayUseEdges);

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
// CCC3 — the look response curve (`docs/architecture/input-architecture.md` § Feel). The whole
// mouse path from counts to degrees is one pure function, which is what lets the retail claim be
// asserted rather than recalled: at the shipped tuning the curve is the identity, exactly, so
// `Elysium.Substrate.LookCurve` failing means the faithful path moved.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLookCurveTest, "Elysium.Substrate.LookCurve", GElysiumTestFlags)
bool FElysiumLookCurveTest::RunTest(const FString&)
{
	using ElysiumInput::FElysiumLookTuning;
	using ElysiumInput::ShapeMouseLook;

	const FElysiumLookTuning Retail;

	// --- The recovered scale ---------------------------------------------------------------
	// `sensitivity` 3 x `m_yaw`/`m_pitch` 0.022 = 0.066 degrees per count.
	TestEqual(TEXT("the shipped yaw scale is VtMB's 0.066 deg/count"),
		Retail.YawScale(), 0.066f, 1e-6f);
	TestEqual(TEXT("the shipped pitch scale is VtMB's 0.066 deg/count"),
		Retail.PitchScale(), 0.066f, 1e-6f);
	TestTrue(TEXT("the shipped tuning is retail-linear"), Retail.IsRetailLinear());

	// A negative `m_pitch` is VtMB's invert-Y, and it is a sign on the scale rather than a setting
	// anything branches on.
	FElysiumLookTuning Inverted;
	Inverted.MousePitch = -0.022f;
	TestTrue(TEXT("a negative m_pitch inverts the pitch scale"), Inverted.PitchScale() < 0.0f);
	TestEqual(TEXT("inverting does not change the yaw scale"), Inverted.YawScale(), 0.066f, 1e-6f);

	// --- The identity, at zero tolerance ---------------------------------------------------
	// The claim the whole rung rests on: shipping this curve does not move the shipped feel. Not
	// "within a tolerance" — the gain is exactly 1.0, so the delta comes back bit-for-bit.
	static const FVector2D Deltas[] = {
		FVector2D(0.066, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, -1.0),
		FVector2D(5.0, 5.0), FVector2D(40.0, -13.3), FVector2D(-3.3, 0.7),
	};
	static const float Steps[] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 240.0f };
	for (const FVector2D& D : Deltas)
	{
		for (const float Step : Steps)
		{
			TestTrue(TEXT("the shipped tuning returns the mouse delta unchanged"),
				ShapeMouseLook(D, Retail, Step).Equals(D, 0.0));
		}
	}

	// --- Engaged -----------------------------------------------------------------------------
	FElysiumLookTuning Curved;
	Curved.Curve = 1.0f;
	TestFalse(TEXT("a non-zero curve is not retail-linear"), Curved.IsRetailLinear());

	const float Step = 1.0f / 60.0f;
	const FVector2D Slow(1.0, 0.0);
	const FVector2D Fast(2.0, 0.0);
	const double SlowOut = ShapeMouseLook(Slow, Curved, Step).X;
	const double FastOut = ShapeMouseLook(Fast, Curved, Step).X;
	TestTrue(TEXT("the curve does something at all"), SlowOut > Slow.X);
	TestTrue(TEXT("the curve is monotonic in the delta"), FastOut > SlowOut);
	TestTrue(TEXT("the curve is superlinear: twice the delta is more than twice the output"),
		FastOut > 2.0 * SlowOut);

	// The gain ceiling is what stops a hitch multiplying a frame without bound.
	const FVector2D Flick(500.0, 0.0);
	TestTrue(TEXT("the gain saturates at MaxScale"),
		ShapeMouseLook(Flick, Curved, Step).X <= Flick.X * Curved.MaxScale + 1e-6);

	// Both signs survive the shaping — the curve reads magnitude, never a component's sign.
	const FVector2D Diagonal(-4.0, 3.0);
	const FVector2D Shaped = ShapeMouseLook(Diagonal, Curved, Step);
	TestTrue(TEXT("the shaped delta keeps the yaw sign"), Shaped.X < 0.0);
	TestTrue(TEXT("the shaped delta keeps the pitch sign"), Shaped.Y > 0.0);
	// ...and it scales as one vector, so a diagonal does not skew.
	TestEqual(TEXT("the shaped delta keeps its direction"),
		(float)(Shaped.X / Shaped.Y), (float)(Diagonal.X / Diagonal.Y), 1e-4f);

	// --- The frame-rate property, stated rather than discovered -------------------------------
	// A rate-keyed curve is frame-rate dependent by construction: the same delta over a shorter
	// frame is a faster hand. The retail path is not, which is the whole reason the default is 0.
	TestEqual(TEXT("retail-linear is frame-rate independent"),
		(float)ShapeMouseLook(Slow, Retail, 1.0f / 30.0f).X,
		(float)ShapeMouseLook(Slow, Retail, 1.0f / 240.0f).X, 1e-6f);
	TestTrue(TEXT("the curve is frame-rate dependent, by construction"),
		ShapeMouseLook(Slow, Curved, 1.0f / 240.0f).X > ShapeMouseLook(Slow, Curved, 1.0f / 30.0f).X);

	// --- Guards -------------------------------------------------------------------------------
	// Each returns the input rather than dividing by it, so a paused frame or a broken tuning
	// cannot fling the view.
	TestTrue(TEXT("a zero delta time leaves the delta alone"),
		ShapeMouseLook(Slow, Curved, 0.0f).Equals(Slow, 0.0));
	TestTrue(TEXT("a negative delta time leaves the delta alone"),
		ShapeMouseLook(Slow, Curved, -1.0f).Equals(Slow, 0.0));
	FElysiumLookTuning NoThreshold = Curved;
	NoThreshold.Threshold = 0.0f;
	TestTrue(TEXT("a zero threshold leaves the delta alone"),
		ShapeMouseLook(Slow, NoThreshold, Step).Equals(Slow, 0.0));
	TestTrue(TEXT("a still mouse stays still"),
		ShapeMouseLook(FVector2D::ZeroVector, Curved, Step).IsNearlyZero());

	// --- The cvar surface ---------------------------------------------------------------------
	TArrayView<const ElysiumInput::FCvarDef> Defs = ElysiumInput::CvarDefs();
	// Seven for the mouse (three recovered, four ours) and eleven for the pad (all ours).
	TestEqual(TEXT("the look cvar surface is eighteen names"), Defs.Num(), 18);
	TSet<FString> Seen;
	for (const ElysiumInput::FCvarDef& Def : Defs)
	{
		const FString Name(Def.Name);
		TestFalse(FString::Printf(TEXT("'%s' is declared once"), *Name), Seen.Contains(Name));
		Seen.Add(Name);
		TestTrue(FString::Printf(TEXT("'%s' has help"), *Name), FCString::Strlen(Def.Help) > 0);
		TestTrue(FString::Printf(TEXT("'%s' has a numeric default"), *Name),
			FCString::IsNumeric(Def.Default));
	}
	TestTrue(TEXT("the three recovered names are declared"),
		Seen.Contains(TEXT("sensitivity")) && Seen.Contains(TEXT("m_yaw"))
			&& Seen.Contains(TEXT("m_pitch")));
	// The declared defaults must BE the struct's defaults, or the console and the code disagree
	// about what a stock install is.
	for (const ElysiumInput::FCvarDef& Def : Defs)
	{
		if (FCString::Strcmp(Def.Name, TEXT("look_curve")) == 0)
		{
			TestEqual(TEXT("look_curve is declared off"), FCString::Atof(Def.Default), 0.0f);
		}
	}

	// `LoadFrom` moves exactly the named field and leaves the rest at their defaults.
	TMap<FString, FString> Store;
	Store.Add(TEXT("sensitivity"), TEXT("5"));
	Store.Add(TEXT("m_pitch"), TEXT("-0.022"));
	FElysiumLookTuning Loaded;
	Loaded.LoadFrom([&Store](const TCHAR* Name)
	{
		const FString* Found = Store.Find(Name);
		return Found ? *Found : FString();
	});
	TestEqual(TEXT("LoadFrom reads sensitivity"), Loaded.Sensitivity, 5.0f, 1e-6f);
	TestEqual(TEXT("LoadFrom reads a negative m_pitch"), Loaded.MousePitch, -0.022f, 1e-6f);
	TestEqual(TEXT("an unread name keeps its default"), Loaded.MouseYaw, 0.022f, 1e-6f);
	TestTrue(TEXT("a store with no curve keys stays retail-linear"), Loaded.IsRetailLinear());

	return true;
}

// =====================================================================================
// The stick path (`docs/architecture/input-architecture.md` § Gamepad). A pad reports a *held
// deflection* that the game integrates, so the device's noise is integrated with it — measured on
// the shipped pad, the resting centre sits ~0.04 off zero and a steady hold swings ±0.2 between
// frames. Every property below is one of the terms that answers that, asserted with no world, no
// device and no local player.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStickLookTest, "Elysium.Substrate.StickLook", GElysiumTestFlags)
bool FElysiumStickLookTest::RunTest(const FString&)
{
	using ElysiumInput::FElysiumStickTuning;
	using ElysiumInput::FElysiumStickState;
	using ElysiumInput::ShapeStickLook;
	using ElysiumInput::ShapeStickMove;

	const FElysiumStickTuning Ship;
	const float Step = 1.0f / 60.0f;

	// Unfiltered for the shape assertions, so each one reads the curve rather than the filter's
	// approach to it. The filter gets its own section below.
	FElysiumStickTuning Sharp = Ship;
	Sharp.SmoothHalfLife = 0.0f;

	auto RateOf = [&Sharp, Step](const FVector2D& Deflection)
	{
		FElysiumStickState State;
		return ShapeStickLook(Deflection, Sharp, Step, State);
	};

	// --- The band -----------------------------------------------------------------------------
	// The measured resting offset is ~0.04 and peaks near 0.05, so the shipped dead zone has to
	// cover it with margin or the view drifts with the pad untouched.
	TestTrue(TEXT("the shipped dead zone clears the measured resting noise"), Ship.DeadZone > 0.06f);
	TestTrue(TEXT("a centred stick produces no rate"),
		RateOf(FVector2D::ZeroVector).IsNearlyZero());
	TestTrue(TEXT("resting noise produces no rate"),
		RateOf(FVector2D(-0.0353, 0.0196)).IsNearlyZero());
	TestTrue(TEXT("a deflection just inside the dead zone produces no rate"),
		RateOf(FVector2D(Ship.DeadZone - 0.001f, 0.0)).IsNearlyZero());

	// Saturation: the top of the travel is retired, so the noise riding on a hard push cannot reach
	// the view. The measured pad reports magnitudes above 1 on a diagonal, which must also clamp
	// rather than overshoot the rate.
	TestEqual(TEXT("full deflection is the full yaw rate"),
		(float)RateOf(FVector2D(1.0, 0.0)).X, Ship.YawRate, 0.01f);
	TestEqual(TEXT("saturation reaches the full rate early"),
		(float)RateOf(FVector2D(Ship.Saturation, 0.0)).X, Ship.YawRate, 0.01f);
	TestEqual(TEXT("a magnitude past 1 does not exceed the full rate"),
		(float)RateOf(FVector2D(-0.3098, 0.9608)).Size(), Ship.YawRate, Ship.YawRate);
	TestTrue(TEXT("an over-unit diagonal clamps rather than overshooting"),
		RateOf(FVector2D(0.7139, 0.7139)).Size() <= Ship.YawRate + 0.01);

	// --- The curve ----------------------------------------------------------------------------
	// Superlinear, which is the term that buys back the fine-control region the dead zone costs.
	const double Half = RateOf(FVector2D(0.52, 0.0)).X;   // ~halfway up the band
	const double Full = RateOf(FVector2D(0.92, 0.0)).X;
	TestTrue(TEXT("the curve is monotonic"), Full > Half && Half > 0.0);
	TestTrue(TEXT("the curve is superlinear: half the band is well under half the rate"),
		Half < 0.5 * Full);

	// It shapes speed, never direction — the whole point of keying on magnitude.
	const FVector2D Diagonal(0.6, -0.45);
	const FVector2D ShapedDiagonal = RateOf(Diagonal);
	TestTrue(TEXT("the shaped rate keeps the yaw sign"), ShapedDiagonal.X > 0.0);
	TestTrue(TEXT("the shaped rate keeps the pitch sign"), ShapedDiagonal.Y < 0.0);
	// Rates differ per axis by design, so the direction is compared after dividing that back out.
	TestEqual(TEXT("the shaped rate keeps the stick's direction"),
		(float)((ShapedDiagonal.X / Ship.YawRate) / (ShapedDiagonal.Y / Ship.PitchRate)),
		(float)(Diagonal.X / Diagonal.Y), 1e-3f);

	// Pitch is the slower axis. It is the shorter gesture and the noisier axis on the measured pad,
	// so rate spent there costs more than it buys.
	TestTrue(TEXT("pitch is the slower axis"), Ship.PitchRate < Ship.YawRate);

	// --- The filter ---------------------------------------------------------------------------
	// The term that answers the measured frame-to-frame swing directly: feed the shaping a square
	// wave at full deflection and assert the output does not follow it.
	FElysiumStickState Noisy;
	double Spread = 0.0;
	double Previous = -1.0;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		const double Deflection = (Frame % 2) ? 0.55 : 0.95;
		const double Rate = ShapeStickLook(FVector2D(Deflection, 0.0), Ship, Step, Noisy).X;
		if (Frame > 60)   // past the filter's approach to the mean
		{
			Spread = FMath::Max(Spread, FMath::Abs(Rate - Previous));
		}
		Previous = Rate;
	}
	const double UnfilteredSpread =
		FMath::Abs(RateOf(FVector2D(0.95, 0.0)).X - RateOf(FVector2D(0.55, 0.0)).X);
	TestTrue(TEXT("the filter is doing something at all"), UnfilteredSpread > 1.0);
	// A quarter, not an order of magnitude. The shipped half-life is 35 ms, which is a deliberate
	// latency budget of roughly two frames — enough to take the worst out of a noisy pad, not enough
	// to make aiming feel like it is happening through water. `joy_smoothing` is the knob for a pad
	// that needs more; this bound is what stops the shipped default quietly becoming less.
	TestTrue(TEXT("the filter cuts a frame-alternating swing to under a quarter"),
		Spread < UnfilteredSpread * 0.25);

	// And it is a half-life, so it settles the same amount per second of real time at any step —
	// the property that stops the pad feeling different at 60 and at 144.
	auto SettleAfter = [&Ship](float Dt, int32 Frames)
	{
		FElysiumStickState State;
		FVector2D Rate = FVector2D::ZeroVector;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Rate = ShapeStickLook(FVector2D(1.0, 0.0), Ship, Dt, State);
		}
		return Rate.X;
	};
	TestEqual(TEXT("the filter settles the same in a tenth of a second at 60 and at 240 Hz"),
		(float)SettleAfter(1.0f / 60.0f, 6), (float)SettleAfter(1.0f / 240.0f, 24), 0.5f);

	// A released stick decays to still rather than coasting — the filter's tail is bounded.
	FElysiumStickState Releasing;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		ShapeStickLook(FVector2D(1.0, 0.0), Ship, Step, Releasing);
	}
	TestTrue(TEXT("a held stick reached its rate"),
		ShapeStickLook(FVector2D(1.0, 0.0), Ship, Step, Releasing).X > Ship.YawRate * 0.9f);
	// **Release is instant, and that is a rule rather than a consequence of the filter being short.**
	// Filtering toward zero would coast `YawRate * HalfLife / ln2` — about ten degrees at the
	// shipped tuning — so a centred stick is taken rather than approached.
	double Coast = 0.0;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Coast += FMath::Abs(ShapeStickLook(FVector2D::ZeroVector, Ship, Step, Releasing).X) * Step;
	}
	TestEqual(TEXT("releasing the stick stops the view on the same frame"), (float)Coast, 0.0f);
	// It is the *zero* that snaps, not a threshold: the shaped value leaves the dead zone
	// continuously, so nothing measurable is discarded at the boundary.
	FElysiumStickState Edge;
	TestTrue(TEXT("the shaped rate leaves the dead zone continuously"),
		FMath::Abs(ShapeStickLook(FVector2D(Ship.DeadZone + 0.002f, 0.0), Ship, Step, Edge).X) < 0.1);

	// --- The ramp -----------------------------------------------------------------------------
	// Off at the shipped tuning: the curve is doing the work, and a ramp is the next delta to
	// reach for rather than one already taken.
	TestEqual(TEXT("the shipped ramp is inert"), Ship.AccelScale, 1.0f);
	FElysiumStickTuning Ramped = Sharp;
	Ramped.AccelScale = 2.0f;
	Ramped.AccelTime = 0.4f;
	FElysiumStickState Charging;
	const double FirstFrame = ShapeStickLook(FVector2D(1.0, 0.0), Ramped, Step, Charging).X;
	for (int32 Frame = 0; Frame < 60; ++Frame)   // a full second of hold
	{
		ShapeStickLook(FVector2D(1.0, 0.0), Ramped, Step, Charging);
	}
	const double Sustained = ShapeStickLook(FVector2D(1.0, 0.0), Ramped, Step, Charging).X;
	// The ramp charges on the frame it is given, so the first sample already carries one step of it —
	// `Step / AccelTime` of the way to `AccelScale`. That is the tolerance, not a round number.
	const float OneStepOfCharge = Ramped.YawRate * (Step / Ramped.AccelTime);
	TestEqual(TEXT("the ramp starts within one step of the unramped rate"),
		(float)FirstFrame, Ramped.YawRate, OneStepOfCharge + 0.01f);
	TestEqual(TEXT("a sustained hold reaches AccelScale"),
		(float)Sustained, Ramped.YawRate * Ramped.AccelScale, 1.0f);
	// It discharges on the raw band, not the filtered one, so letting go stops accelerating at once.
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		ShapeStickLook(FVector2D::ZeroVector, Ramped, Step, Charging);
	}
	TestEqual(TEXT("the ramp discharges when the stick is released"),
		(float)ShapeStickLook(FVector2D(1.0, 0.0), Ramped, Step, Charging).X, Ramped.YawRate,
		OneStepOfCharge + 0.01f);

	// --- Guards -------------------------------------------------------------------------------
	// A broken tuning returns a still stick rather than dividing by it or flinging the view.
	FElysiumStickTuning Degenerate = Sharp;
	Degenerate.Saturation = Degenerate.DeadZone;   // a band with no width
	FElysiumStickState Guarded;
	const FVector2D Switched = ShapeStickLook(FVector2D(0.5, 0.0), Degenerate, Step, Guarded);
	TestFalse(TEXT("a zero-width band does not produce NaN"), Switched.ContainsNaN());
	Guarded.Reset();
	TestTrue(TEXT("a NaN deflection produces no rate"),
		ShapeStickLook(FVector2D(NAN, 0.0), Sharp, Step, Guarded).IsNearlyZero());
	Guarded.Reset();
	TestTrue(TEXT("a zero frame delta produces the unfiltered rate rather than a division"),
		!ShapeStickLook(FVector2D(1.0, 0.0), Ship, 0.0f, Guarded).ContainsNaN());

	// --- The movement stick --------------------------------------------------------------------
	// Deliberately curve-free and filter-free: the mover already owns acceleration, and a curve
	// would move the walk/run threshold away from where the stick says it is.
	TestTrue(TEXT("a centred move stick is still"),
		ShapeStickMove(FVector2D::ZeroVector, Ship).IsNearlyZero());
	TestTrue(TEXT("move resting noise is still"),
		ShapeStickMove(FVector2D(-0.0353, 0.0196), Ship).IsNearlyZero());
	TestEqual(TEXT("a full push is a unit wish"),
		(float)ShapeStickMove(FVector2D(0.0, 1.0), Ship).Size(), 1.0f, 1e-3f);
	const FVector2D Midway = ShapeStickMove(
		FVector2D(0.0, Ship.MoveDeadZone + (Ship.MoveSaturation - Ship.MoveDeadZone) * 0.5f), Ship);
	TestEqual(TEXT("move is linear across its band"), (float)Midway.Size(), 0.5f, 1e-3f);
	// The band is wider than the look band, because a character that creeps is the louder failure.
	TestTrue(TEXT("the move dead zone is the wider of the two"), Ship.MoveDeadZone > Ship.DeadZone);
	// And it preserves direction, so a diagonal walks where the stick points.
	const FVector2D MoveDiagonal = ShapeStickMove(FVector2D(0.6, 0.6), Ship);
	TestEqual(TEXT("a move diagonal keeps its direction"),
		(float)MoveDiagonal.X, (float)MoveDiagonal.Y, 1e-4f);

	// --- The cvar surface ----------------------------------------------------------------------
	// The declared defaults must BE the struct's defaults, or the console and the code disagree
	// about what a stock install is.
	TMap<FString, FString> Declared;
	for (const ElysiumInput::FCvarDef& Def : ElysiumInput::CvarDefs())
	{
		Declared.Add(FString(Def.Name), FString(Def.Default));
	}
	FElysiumStickTuning FromStore;
	FromStore.LoadFrom([&Declared](const TCHAR* Name)
	{
		const FString* Found = Declared.Find(Name);
		return Found ? *Found : FString();
	});
	TestEqual(TEXT("joy_deadzone is declared at its default"), FromStore.DeadZone, Ship.DeadZone, 1e-6f);
	TestEqual(TEXT("joy_saturation is declared at its default"),
		FromStore.Saturation, Ship.Saturation, 1e-6f);
	TestEqual(TEXT("joy_response_look is declared at its default"),
		FromStore.Exponent, Ship.Exponent, 1e-6f);
	TestEqual(TEXT("joy_yawsensitivity is declared at its default"),
		FromStore.YawRate, Ship.YawRate, 1e-6f);
	TestEqual(TEXT("joy_pitchsensitivity is declared at its default"),
		FromStore.PitchRate, Ship.PitchRate, 1e-6f);
	TestEqual(TEXT("joy_smoothing is declared at its default"),
		FromStore.SmoothHalfLife, Ship.SmoothHalfLife, 1e-6f);
	TestEqual(TEXT("joy_accelscale is declared at its default"),
		FromStore.AccelScale, Ship.AccelScale, 1e-6f);
	TestEqual(TEXT("joy_acceltime is declared at its default"),
		FromStore.AccelTime, Ship.AccelTime, 1e-6f);
	TestEqual(TEXT("joy_accelenter is declared at its default"),
		FromStore.AccelEnter, Ship.AccelEnter, 1e-6f);
	TestEqual(TEXT("joy_move_deadzone is declared at its default"),
		FromStore.MoveDeadZone, Ship.MoveDeadZone, 1e-6f);
	TestEqual(TEXT("joy_move_saturation is declared at its default"),
		FromStore.MoveSaturation, Ship.MoveSaturation, 1e-6f);

	// An unread name keeps its default, so a run with no `config.cfg` behaves like a stock install.
	FElysiumStickTuning Partial;
	Partial.LoadFrom([](const TCHAR* Name)
	{
		return FCString::Strcmp(Name, TEXT("joy_yawsensitivity")) == 0 ? FString(TEXT("260"))
																	   : FString();
	});
	TestEqual(TEXT("LoadFrom reads joy_yawsensitivity"), Partial.YawRate, 260.0f, 1e-6f);
	TestEqual(TEXT("an unread stick name keeps its default"),
		Partial.SmoothHalfLife, Ship.SmoothHalfLife, 1e-6f);

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
	AddExpectedError(TEXT("DENY ../../../Windows/system32/x.dll"),
		EAutomationExpectedErrorFlags::Contains, 1);
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
	// nothing: at 0.25x a 0.02 s delta is still 0.02 s of game time, not 0.005.
	Time.SetScale(0.25);
	TestEqual(TEXT("scale recorded on the clock"), Time.GetScale(), 0.25);
	TestEqual(TEXT("the clock adds no factor of its own"), Time.AdvanceFrame(0.02), 0.02);
	TestEqual(TEXT("now advanced by the dilated delta"), Clock.GetNow(), 0.07);
	// The BOUND scales with it, exactly as `Host_FilterTime`'s does (`timescale * 0.1`) and as
	// AWorldSettings::FixupDeltaSeconds does (`Max * Dilation`). A flat bound here would cut the
	// substrate short of the delta every other actor on the frame received.
	TestEqual(TEXT("a hitch at 0.25x bounds to a quarter of MaxFrameSeconds"), Time.AdvanceFrame(5.0),
		ElysiumFrame::MaxFrameSeconds * 0.25);
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

		// These two constants are also what FElysiumTimeControl::ApplyToWorld writes into
		// AWorldSettings::Min/MaxUndilatedFrameTime, and what Config/DefaultGame.ini declares.
		// If they drift, the engine's filter and this one stop being the same filter and
		// engine-tick consumers advance further on a long frame than the substrate does.
		TestEqual(TEXT("the floor is Host_FilterTime's"), ElysiumFrame::MinFrameSeconds, 0.001);
		TestEqual(TEXT("and the ceiling is Host_FilterTime's"), ElysiumFrame::MaxFrameSeconds, 0.1);

		// The bound scales with time dilation, as retail's (`timescale * [0.001, 0.1]`) and
		// Unreal's (`Min/Max * Dilation`) both do. The delta handed in is already dilated, so a
		// flat bound would disagree with the engine in both directions.
		TestTrue(TEXT("a hitch at 0.25x bounds to 0.025 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0, 0.25), 0.025, 1e-9));
		TestTrue(TEXT("and the floor scales down with it rather than fabricating motion"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(0.00001, 0.25), 0.00025, 1e-9));
		TestTrue(TEXT("a hitch at 2x bounds to 0.2 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0, 2.0), 0.2, 1e-9));
		// A held world advances nothing, and a negative scale must not invert the bound.
		TestTrue(TEXT("scale 0 collapses the bound to zero"),
			ElysiumFrame::ClampFrameDelta(0.05, 0.0) == 0.0);
		TestTrue(TEXT("and a negative scale is treated as held, not reversed"),
			ElysiumFrame::ClampFrameDelta(0.05, -1.0) == 0.0);
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
// The gym's specification (CCC0). The layout is derived from `ElysiumMove`'s constants, so what
// is asserted here is the **derivation** — that each lane straddles the value it names — and never
// what a body will do on it. An expectation recomputed from the same constants as the geometry
// moves with the geometry and could never fail; the behaviour is the headless run's to record.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSpecTest, "Elysium.Substrate.GymSpec", GElysiumTestFlags)
bool FElysiumGymSpecTest::RunTest(const FString&)
{
	using namespace ElysiumMove;
	const FElysiumMoveTuning T;
	const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);

	TestTrue(TEXT("the gym has lanes"), Spec.Lanes.Num() > 0);
	TestTrue(TEXT("the gym has solids"), Spec.Placements.Num() > 0);

	// --- Structural invariants the spawner depends on -----------------------------------------
	{
		TSet<FName> LaneNames;
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			TestFalse(TEXT("a lane is named"), L.Name.IsNone());
			TestFalse(FString::Printf(TEXT("lane '%s' is declared once"), *L.Name.ToString()),
				LaneNames.Contains(L.Name));
			LaneNames.Add(L.Name);
		}

		// (Lane, Tag) becomes a spawned component's name, so a duplicate is a silently dropped solid.
		TSet<FString> Keys;
		for (const ElysiumGym::FPlacement& P : Spec.Placements)
		{
			const FString Key = P.Lane.ToString() + TEXT("_") + P.Tag.ToString();
			TestFalse(FString::Printf(TEXT("solid '%s' is placed once"), *Key), Keys.Contains(Key));
			Keys.Add(Key);

			TestTrue(FString::Printf(TEXT("solid '%s' has a real extent"), *Key),
				P.Extent.X > 0.0 && P.Extent.Y > 0.0 && P.Extent.Z > 0.0);
			TestTrue(FString::Printf(TEXT("solid '%s' is finite"), *Key),
				P.Center.ContainsNaN() == false && P.Rot.ContainsNaN() == false);
			TestTrue(FString::Printf(TEXT("solid '%s' belongs to a declared lane"), *Key),
				LaneNames.Contains(P.Lane));
		}
	}

	// --- Lanes do not overlap, so one body's course cannot touch another's geometry -----------
	{
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			const double LaneY = L.FeetOrigin.Y;
			for (const ElysiumGym::FPlacement& P : Spec.Placements)
			{
				if (P.Lane != L.Name)
				{
					continue;
				}
				const double Reach = FMath::Abs(P.Center.Y - LaneY) + P.Extent.Y;
				// A doorway jamb reaches exactly to the band edge, and the spec derives its centre
				// and half-extent in float — so the slack here is float rounding at cm scale, not a
				// margin the layout is allowed to spend.
				TestTrue(FString::Printf(TEXT("'%s/%s' stays inside its lane's Y band"),
					*P.Lane.ToString(), *P.Tag.ToString()),
					Reach <= ElysiumGym::LaneHalfWidth * U + 0.01);
			}
		}
		TestTrue(TEXT("the lane pitch clears the lane width"),
			ElysiumGym::LanePitch > 2.0f * ElysiumGym::LaneHalfWidth);
	}

	// --- Each bracket straddles the constant it names -----------------------------------------
	auto Bracket = [&Spec, this](const TCHAR* Below, const TCHAR* At, const TCHAR* Above,
		float Threshold, const TCHAR* What)
	{
		const ElysiumGym::FLane* B = Spec.FindLane(FName(Below));
		const ElysiumGym::FLane* M = Spec.FindLane(FName(At));
		const ElysiumGym::FLane* A = Spec.FindLane(FName(Above));
		if (!B || !M || !A)
		{
			AddError(FString::Printf(TEXT("%s is missing a bracket lane"), What));
			return;
		}
		TestEqual(FString::Printf(TEXT("%s: the middle rung IS the constant"), What),
			M->BracketUnits, Threshold);
		TestTrue(FString::Printf(TEXT("%s: one rung below, one above"), What),
			B->BracketUnits < Threshold && A->BracketUnits > Threshold);
	};

	Bracket(TEXT("riser_m1"), TEXT("riser_0"), TEXT("riser_p1"), T.StepSize / U, TEXT("StepSize"));
	Bracket(TEXT("pop_m1"), TEXT("pop_0"), TEXT("pop_p1"), T.JumpBoost, TEXT("JumpBoost"));
	Bracket(TEXT("stand_m1"), TEXT("stand_0"), TEXT("stand_p1"), StandHeight / U, TEXT("StandHeight"));
	Bracket(TEXT("duck_m1"), TEXT("duck_0"), TEXT("duck_p1"), DuckHeight / U, TEXT("DuckHeight"));
	Bracket(TEXT("door_m1"), TEXT("door_0"), TEXT("door_p1"), 2.0f * HullHalfWidth / U,
		TEXT("the hull width"));

	// The riser bracket also has to reach past the cliff on both sides, which is what tells 19 from
	// 20 and 24 — a two-sided bracket alone would only ever prove the first refusal.
	{
		const ElysiumGym::FLane* Low = Spec.FindLane(FName(TEXT("riser_m2")));
		const ElysiumGym::FLane* High = Spec.FindLane(FName(TEXT("riser_p6")));
		TestTrue(TEXT("the riser bracket reaches two below the step"),
			Low && Low->BracketUnits < T.StepSize / U - 1.0f);
		TestTrue(TEXT("and well above it"),
			High && High->BracketUnits > T.StepSize / U + 1.0f);
	}

	// --- The slope bracket is a statement about a normal, not about a pitch --------------------
	{
		const float LimitDeg = FMath::RadiansToDegrees(FMath::Acos(StandableZ));
		int32 Standable = 0;
		int32 Steep = 0;
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			if (L.Family != ElysiumGym::EFamily::Slope)
			{
				continue;
			}
			(L.BracketUnits < LimitDeg ? Standable : Steep)++;

			// A ramp pitched at P has a top-face normal whose Z is cos(P). That identity is the
			// whole reason a pitch brackets `StandableZ` at all, so assert it on the emitted solid
			// rather than trusting the constructor.
			const ElysiumGym::FPlacement* Ramp = Spec.Placements.FindByPredicate(
				[&L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L.Name && P.Tag == FName(TEXT("ramp")); });
			if (!Ramp)
			{
				AddError(FString::Printf(TEXT("slope lane '%s' has no ramp"), *L.Name.ToString()));
				continue;
			}
			const double NormalZ = Ramp->Rot.RotateVector(FVector::UpVector).Z;
			TestTrue(FString::Printf(TEXT("'%s' top-face normal Z is cos(pitch)"), *L.Name.ToString()),
				FMath::IsNearlyEqual(NormalZ,
					FMath::Cos(FMath::DegreesToRadians(L.BracketUnits)), 1e-4));
		}
		TestTrue(TEXT("slopes straddle the standable limit on both sides"),
			Standable >= 2 && Steep >= 2);
	}

	// --- The unduck chamber admits a ducked hull and refuses a standing one --------------------
	{
		const ElysiumGym::FLane* Chamber = Spec.FindLane(FName(TEXT("unduck_ground")));
		TestNotNull(TEXT("the grounded unduck chamber exists"), Chamber);
		TestNotNull(TEXT("the airborne unduck chamber exists"),
			Spec.FindLane(FName(TEXT("unduck_air"))));
		if (Chamber)
		{
			TestTrue(TEXT("the chamber is taller than the ducked hull"),
				Chamber->BracketUnits > DuckHeight / U);
			TestTrue(TEXT("and shorter than the standing one"),
				Chamber->BracketUnits < StandHeight / U);
		}
	}

	// --- The crouch-jump lane names the lift, which is half the hull difference ----------------
	{
		const ElysiumGym::FLane* DuckPop = Spec.FindLane(FName(TEXT("duckpop")));
		TestNotNull(TEXT("the crouch-jump lane exists"), DuckPop);
		if (DuckPop)
		{
			TestTrue(TEXT("its bracket is the airborne duck's own lift"),
				FMath::IsNearlyEqual(DuckPop->BracketUnits,
					(StandHeight - DuckHeight) * 0.5f / U, 1e-3f));
		}
	}

	// --- The leniency lanes: one lip, one drop, brackets in frames (CCC3) ---------------------
	{
		static const TCHAR* const LedgeNames[] =
			{ TEXT("ledge_m1"), TEXT("ledge_0"), TEXT("ledge_p1"),
			  TEXT("ledge_p2"), TEXT("ledge_p4") };
		static const float LedgeOffsets[] = { -1.0f, 0.0f, 1.0f, 2.0f, 4.0f };
		static const TCHAR* const LandNames[] =
			{ TEXT("land_p1"), TEXT("land_0"), TEXT("land_m1"),
			  TEXT("land_m2"), TEXT("land_m4") };
		static const float LandOffsets[] = { 1.0f, 0.0f, -1.0f, -2.0f, -4.0f };

		auto CheckLeniencyLane = [&Spec, this](const TCHAR* Name, float Offset,
			ElysiumGym::EFamily Family)
		{
			const ElysiumGym::FLane* L = Spec.FindLane(FName(Name));
			if (!TestNotNull(FString::Printf(TEXT("leniency lane '%s' exists"), Name), L))
			{
				return;
			}
			TestEqual(FString::Printf(TEXT("'%s' brackets its own frame offset"), Name),
				L->BracketUnits, Offset);
			TestTrue(FString::Printf(TEXT("'%s' is the family it is named for"), Name),
				L->Family == Family);
			// It starts where every other lane starts, so the seat rule has no special case.
			TestTrue(FString::Printf(TEXT("'%s' seats at the standard inset"), Name),
				FMath::IsNearlyEqual(L->FeetOrigin.X,
					(-ElysiumGym::RunUp + ElysiumGym::StartInset) * U, 1e-3));

			// The lip is the upper slab's far face, at X = 0 — the feature-face convention every
			// family uses — and the drop below it is `PitDepth`, deep enough that the fall spans far
			// more frames than the widest bracket asks for.
			const ElysiumGym::FPlacement* Upper = Spec.Placements.FindByPredicate(
				[L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L->Name && P.Tag == FName(TEXT("upper")); });
			const ElysiumGym::FPlacement* Lower = Spec.Placements.FindByPredicate(
				[L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L->Name && P.Tag == FName(TEXT("lower")); });
			if (!TestNotNull(FString::Printf(TEXT("'%s' has a run-up"), Name), Upper)
				|| !TestNotNull(FString::Printf(TEXT("'%s' has a floor to land on"), Name), Lower))
			{
				return;
			}
			TestTrue(FString::Printf(TEXT("'%s' puts the lip at the feature face"), Name),
				FMath::IsNearlyEqual(Upper->Center.X + Upper->Extent.X, 0.0, 1e-3));
			TestTrue(FString::Printf(TEXT("'%s' walks off the top of the run-up"), Name),
				FMath::IsNearlyEqual(Upper->Center.Z + Upper->Extent.Z, 0.0, 1e-3));
			TestTrue(FString::Printf(TEXT("'%s' drops a full PitDepth"), Name),
				FMath::IsNearlyEqual(Lower->Center.Z + Lower->Extent.Z,
					-ElysiumGym::PitDepth * U, 1e-3));
			// The lower floor starts under the lip and runs to the wall, so the body lands on it and
			// then keeps going until it is stopped — which is what makes the reach saturate.
			TestTrue(FString::Printf(TEXT("'%s' catches the body from the lip onward"), Name),
				FMath::IsNearlyEqual(Lower->Center.X - Lower->Extent.X, 0.0, 1e-3)
				&& FMath::IsNearlyEqual(Lower->Center.X + Lower->Extent.X,
					ElysiumGym::LaneLength * U, 1e-3));
		};

		for (int32 i = 0; i < UE_ARRAY_COUNT(LedgeNames); ++i)
		{
			CheckLeniencyLane(LedgeNames[i], LedgeOffsets[i], ElysiumGym::EFamily::Ledge);
			CheckLeniencyLane(LandNames[i], LandOffsets[i], ElysiumGym::EFamily::Landing);
		}

		// The brackets straddle the decision on both sides, which is what makes a refusal a cliff
		// rather than an absence: `ledge_m1` must jump and `ledge_p1` must not, and if leniency is
		// ever added it is `ledge_p1`/`land_m1` that move.
		const ElysiumGym::FLane* LedgeBefore = Spec.FindLane(FName(TEXT("ledge_m1")));
		const ElysiumGym::FLane* LedgeAfter = Spec.FindLane(FName(TEXT("ledge_p1")));
		TestTrue(TEXT("the coyote bracket straddles the ground-loss frame"),
			LedgeBefore && LedgeAfter
			&& LedgeBefore->BracketUnits < 0.0f && LedgeAfter->BracketUnits > 0.0f);
		const ElysiumGym::FLane* LandAfter = Spec.FindLane(FName(TEXT("land_p1")));
		const ElysiumGym::FLane* LandBefore = Spec.FindLane(FName(TEXT("land_m1")));
		TestTrue(TEXT("the buffer bracket straddles the landing frame"),
			LandAfter && LandBefore
			&& LandAfter->BracketUnits > 0.0f && LandBefore->BracketUnits < 0.0f);
	}

	// --- What `CCC7` may move is flagged, and what it may not is not --------------------------
	{
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			// Only the two families whose answer is a horizontal *distance* move with the gait. The
			// leniency lanes are deliberately not among them: their answer is whether one press
			// became a jump, and a press placed against a body event produces the same 0 or 1
			// however fast the body reached the lip. This row is what enforces that claim.
			const bool bHorizontalReach =
				L.Family == ElysiumGym::EFamily::Gap || L.Family == ElysiumGym::EFamily::Flat;
			TestEqual(FString::Printf(TEXT("'%s' declares the right speed class"), *L.Name.ToString()),
				L.bSpeedDependent, bHorizontalReach);
		}
	}

	return true;
}

// A constant owns exactly one bracket. This is the acceptance criterion "moving a constant in
// `ElysiumMove` turns exactly the bracket that constant owns red", asserted on the geometry with
// no world — the headless run then confirms it on behaviour.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSpecOwnershipTest,
	"Elysium.Substrate.GymSpecOwnership", GElysiumTestFlags)
bool FElysiumGymSpecOwnershipTest::RunTest(const FString&)
{
	const FElysiumMoveTuning Base;
	const ElysiumGym::FSpec Before = ElysiumGym::Build(Base);

	FElysiumMoveTuning Moved = Base;
	Moved.StepSize += ElysiumMove::U;                 // one Source unit taller
	const ElysiumGym::FSpec After = ElysiumGym::Build(Moved);

	TestEqual(TEXT("moving a constant does not add or drop a solid"),
		After.Placements.Num(), Before.Placements.Num());

	int32 Moved3D = 0;
	for (int32 i = 0; i < Before.Placements.Num() && i < After.Placements.Num(); ++i)
	{
		const ElysiumGym::FPlacement& A = Before.Placements[i];
		const ElysiumGym::FPlacement& B = After.Placements[i];
		TestEqual(TEXT("the lane order is stable"), B.Lane, A.Lane);
		TestEqual(TEXT("the tag order is stable"), B.Tag, A.Tag);

		const bool bSame = A.Center.Equals(B.Center, 1e-3) && A.Extent.Equals(B.Extent, 1e-3);
		if (!bSame)
		{
			++Moved3D;
			// Only the lanes `StepSize` owns may move, and every one of them must.
			TestTrue(FString::Printf(TEXT("'%s/%s' moved, and it is a riser"),
				*A.Lane.ToString(), *A.Tag.ToString()),
				A.Lane.ToString().StartsWith(TEXT("riser_")));
		}
	}
	TestTrue(TEXT("the risers did move"), Moved3D > 0);

	// And the lane the constant *is* moves by exactly what the constant moved by.
	{
		const ElysiumGym::FLane* A = Before.FindLane(FName(TEXT("riser_0")));
		const ElysiumGym::FLane* B = After.FindLane(FName(TEXT("riser_0")));
		TestTrue(TEXT("the middle riser tracks the step exactly"),
			A && B && FMath::IsNearlyEqual(B->BracketUnits - A->BracketUnits, 1.0f, 1e-3f));
	}
	return true;
}

// The one place the spec's feet-anchored convention meets the pawn's centre-anchored box. This is
// the "stands **on** the gym rather than above it" acceptance in the form that can be asserted:
// the hull's own underside, not its origin, is what has to land on the floor.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSeatTest, "Elysium.Substrate.GymSeat", GElysiumTestFlags)
bool FElysiumGymSeatTest::RunTest(const FString&)
{
	// The pawn's constructed hull, read off the CDO rather than restated — a half-height typed
	// twice is a half-height that can disagree with itself.
	const AElysiumPawn* Pawn = GetDefault<AElysiumPawn>();
	TestNotNull(TEXT("the player pawn has a CDO"), Pawn);
	if (!Pawn)
	{
		return false;
	}
	const float HalfHeight = Pawn->GetBodyHalfHeight();
	TestTrue(TEXT("the hull half-height is the standing hull's"),
		FMath::IsNearlyEqual(HalfHeight, ElysiumMove::StandHeight * 0.5f, 0.01f));

	const FVector Feet(1234.0, -567.0, 89.0);
	const FVector Origin = ElysiumGym::SeatOrigin(Feet, HalfHeight);

	TestTrue(TEXT("seating moves nothing horizontally"),
		FMath::IsNearlyEqual(Origin.X, Feet.X, 1e-4) && FMath::IsNearlyEqual(Origin.Y, Feet.Y, 1e-4));

	const double HullMinZ = Origin.Z - HalfHeight;
	TestTrue(TEXT("the hull's underside clears the floor by exactly DistEpsilon"),
		FMath::IsNearlyEqual(HullMinZ - Feet.Z, ElysiumMove::DistEpsilon, 1e-4));
	TestTrue(TEXT("so the body is above the surface, never inside it"), HullMinZ > Feet.Z);

	// And a gym lane's own start seats the same way — the lanes are authored at floor level, so a
	// start that needed a per-lane fudge would mean the convention had leaked.
	{
		const FElysiumMoveTuning T;
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			TestTrue(FString::Printf(TEXT("lane '%s' starts at floor level"), *L.Name.ToString()),
				FMath::IsNearlyEqual(L.FeetOrigin.Z, 0.0, 1e-4));
		}

		// The lane the green room's drive mode seats a body on by default (CCC6). It is named here
		// because the harness names it: a lane that is renamed or dropped would otherwise turn a
		// hand-driven session into a body standing in the void, with nothing red to say so.
		const ElysiumGym::FLane* Drive = Spec.FindLane(TEXT("flat"));
		TestNotNull(TEXT("the gym carries the 'flat' lane drive mode seats on"), Drive);
		if (Drive != nullptr)
		{
			// Long enough to reach a run and stop again — that is what the flat lane is for, and it is
			// why drive mode starts there rather than on a bracket rung.
			TestTrue(TEXT("and it is the long clear run rather than a bracket rung"),
				Drive->Family == ElysiumGym::EFamily::Flat);
		}
	}
	return true;
}

// =====================================================================================
// The pose deviation measure (CCC5/CCC6). Two callers share it: the Content tier asserts a real
// baked body left its bind pose, and the green room's drive panel reports the same number live. It
// is the only observable the T-pose failure has, so the arithmetic is worth pinning on its own.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPoseDeviationTest,
	"Elysium.Substrate.PoseDeviation", GElysiumTestFlags)
bool FElysiumPoseDeviationTest::RunTest(const FString&)
{
	TArray<FTransform> A;
	A.SetNum(4);
	TArray<FTransform> B = A;

	{
		const ElysiumPose::FDeviation Same = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("identical poses move no bone"), Same.MovedBones, 0);
		TestTrue(TEXT("and deviate by nothing"), FMath::IsNearlyEqual(Same.MaxDegrees, 0.f, 1e-4f));
	}

	// One bone, one known angle.
	B[2].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(30.0)));
	{
		const ElysiumPose::FDeviation One = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("one rotated bone reads as one moved bone"), One.MovedBones, 1);
		TestTrue(TEXT("at the angle it was rotated by"),
			FMath::IsNearlyEqual(One.MaxDegrees, 30.0f, 0.01f));
	}

	// **Bone 0 is not a pose.** In component space the root carries the actor transform, so a body
	// that merely walked across the gym would otherwise read as a changed pose — which is exactly the
	// signal drive mode watches for a T-pose.
	B = A;
	B[0].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0)));
	{
		const ElysiumPose::FDeviation Root = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("the root is skipped"), Root.MovedBones, 0);
		TestTrue(TEXT("and contributes no angle"), FMath::IsNearlyEqual(Root.MaxDegrees, 0.f, 1e-4f));
	}

	// The threshold separates a posed bone from arithmetic noise, and it is exclusive.
	B = A;
	B[1].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(0.4)));
	B[3].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(0.6)));
	{
		const ElysiumPose::FDeviation Noise = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("only the bone past the threshold counts"), Noise.MovedBones, 1);
	}

	// Mismatched lengths compare what both carry rather than reading off the end: a component's
	// transform array and a reference pose can legitimately disagree while an LOD is settling.
	{
		TArray<FTransform> Short;
		Short.SetNum(2);
		const ElysiumPose::FDeviation Ragged = ElysiumPose::Measure(A, Short);
		TestEqual(TEXT("a shorter pose is compared as far as it goes"), Ragged.MovedBones, 0);
	}
	return true;
}

#if !UE_BUILD_SHIPPING
// =====================================================================================
// The event-timed press (CCC3). A leniency course cannot say "jump at 2.4 seconds": the time it
// takes to reach a lip moves with the gait, and `CCC7` may halve it. It says "jump K frames after
// the ground is lost" instead, and the harness measures the event in a probe pass. What is pure —
// and therefore asserted here — is the placement: given a resolved frame, exactly one command in
// the stream carries the press, and nothing else about the stream moves.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveCoursesTest,
	"Elysium.Substrate.MoveCourses", GElysiumTestFlags)
bool FElysiumMoveCoursesTest::RunTest(const FString&)
{
	using namespace ElysiumMoveCourses;
	const float Step = 1.0f / 60.0f;
	const uint64 JumpBit = static_cast<uint64>(EElysiumButton::Jump);

	FCourse C;
	C.Name = FName(TEXT("test_ledge"));
	C.Host = EHost::GymStage;
	C.Segments.Add({ 2.0f, FVector2D(1.0, 0.0), 0, 0.0f });
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, 2 };

	// The probe pass: the same course, no press. This is what makes the two passes comparable —
	// the stream is the same length and carries the same intent, minus the one frame under test.
	const FElysiumUserCmdStream Probe = Expand(C, Step, INDEX_NONE);
	TestEqual(TEXT("the probe stream is the whole course"), Probe.Num(), 120);
	int32 ProbePresses = 0;
	for (const FElysiumUserCmd& Cmd : Probe.Cmds)
	{
		ProbePresses += Cmd.IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("the probe pass presses nothing"), ProbePresses, 0);

	// The record pass: the press lands at event + offset, and on exactly one frame.
	const FElysiumUserCmdStream Record = Expand(C, Step, 40);
	TestEqual(TEXT("both passes are the same length"), Record.Num(), Probe.Num());
	int32 Presses = 0;
	int32 PressAt = INDEX_NONE;
	for (int32 i = 0; i < Record.Num(); ++i)
	{
		if (Record.Cmds[i].IsDown(EElysiumButton::Jump))
		{
			++Presses;
			PressAt = i;
		}
	}
	TestEqual(TEXT("exactly one frame presses jump"), Presses, 1);
	TestEqual(TEXT("and it is the resolved frame plus the offset"), PressAt, 42);

	// Nothing else moved: the press is OR-ed on, so the body keeps walking through it.
	for (int32 i = 0; i < Record.Num(); ++i)
	{
		TestEqual(TEXT("the move intent is untouched by the press"),
			Record.Cmds[i].Move, Probe.Cmds[i].Move);
		TestEqual(TEXT("no other button is touched"),
			Record.Cmds[i].Buttons & ~JumpBit, Probe.Cmds[i].Buttons & ~JumpBit);
	}

	// A negative offset is the buffer bracket, and it reaches backwards from the event.
	C.EventJump = FEventJump{ EBodyEvent::GroundGained, -4 };
	const FElysiumUserCmdStream Before = Expand(C, Step, 40);
	TestTrue(TEXT("a negative offset presses before the event"),
		Before.Cmds.IsValidIndex(36) && Before.Cmds[36].IsDown(EElysiumButton::Jump));

	// An offset that would fall off either end places nothing rather than clamping — a press
	// silently moved to frame 0 would be a course quietly measuring something else.
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, 500 };
	const FElysiumUserCmdStream Past = Expand(C, Step, 40);
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, -500 };
	const FElysiumUserCmdStream Under = Expand(C, Step, 40);
	int32 OutOfRangePresses = 0;
	for (int32 i = 0; i < Past.Num(); ++i)
	{
		OutOfRangePresses += Past.Cmds[i].IsDown(EElysiumButton::Jump) ? 1 : 0;
		OutOfRangePresses += Under.Cmds[i].IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("an out-of-range offset places no press at all"), OutOfRangePresses, 0);

	// A course with no event jump ignores a resolved frame entirely, which is what lets the harness
	// run every existing course through the same call unchanged.
	FCourse Plain;
	Plain.Segments.Add({ 1.0f, FVector2D(1.0, 0.0), 0, 0.0f });
	const FElysiumUserCmdStream PlainStream = Expand(Plain, Step, 10);
	int32 PlainPresses = 0;
	for (const FElysiumUserCmd& Cmd : PlainStream.Cmds)
	{
		PlainPresses += Cmd.IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("a course with no event jump is unaffected"), PlainPresses, 0);

	// --- Every leniency lane gets a recipe, and it is a single press against the right edge -----
	{
		const FElysiumMoveTuning T;
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);
		const TArray<FCourse> Courses = Gym(Spec);
		int32 Leniency = 0;
		for (const FCourse& Course : Courses)
		{
			const ElysiumGym::FLane* Lane = Spec.FindLane(Course.GymLane);
			if (!Lane || (Lane->Family != ElysiumGym::EFamily::Ledge
				&& Lane->Family != ElysiumGym::EFamily::Landing))
			{
				TestFalse(FString::Printf(TEXT("'%s' does not time a press against an event"),
					*Course.Name.ToString()), Course.EventJump.IsSet());
				continue;
			}
			++Leniency;
			if (!TestTrue(FString::Printf(TEXT("'%s' times its press against an event"),
				*Course.Name.ToString()), Course.EventJump.IsSet()))
			{
				continue;
			}
			const EBodyEvent Expected = Lane->Family == ElysiumGym::EFamily::Ledge
				? EBodyEvent::GroundLost : EBodyEvent::GroundGained;
			TestTrue(FString::Printf(TEXT("'%s' watches the edge its family is about"),
				*Course.Name.ToString()), Course.EventJump->Event == Expected);
			TestEqual(FString::Printf(TEXT("'%s' carries its lane's frame offset"),
				*Course.Name.ToString()),
				Course.EventJump->FrameOffset, FMath::RoundToInt32(Lane->BracketUnits));
			// A committed bracket: nothing here may be deferred, or the measurement never lands.
			TestFalse(FString::Printf(TEXT("'%s' is committed, not deferred"),
				*Course.Name.ToString()), Course.bDeferBaseline);
		}
		TestEqual(TEXT("both leniency brackets are five rungs"), Leniency, 10);
	}

	return true;
}
#endif // !UE_BUILD_SHIPPING

// =====================================================================================
// The animation's per-direction speed (CCC7). The fan below is the male body's authored `walk`
// grid, in cm/s, read out of `docs/vtmb/animation_and_movers.md` — so what is asserted is the
// table's arithmetic against numbers the export produces, not the export itself, which is
// `Elysium.Content.GaitSpeeds`.
// =====================================================================================

namespace
{
	// `move_and_ranged`'s `walk`, cells 0..8 at move_yaw -180..+180 in 45-degree steps. Cells 0 and 8
	// are the same clip across the wrap seam.
	FElysiumGaitSpeedTable MaleWalkFan()
	{
		FElysiumGaitSpeedTable Fan;
		Fan.Count = 9;
		Fan.AxisMin = -180.0f;
		Fan.AxisMax = 180.0f;
		const float Authored[9] = { 88.6f, 113.9f, 97.1f, 88.1f, 136.7f, 88.1f, 60.7f, 113.9f, 88.6f };
		FMemory::Memcpy(Fan.Cells, Authored, sizeof(Authored));
		return Fan;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGaitSpeedsTest,
	"Elysium.Substrate.GaitSpeeds", GElysiumTestFlags)
bool FElysiumGaitSpeedsTest::RunTest(const FString&)
{
	// --- A table with nothing in it answers nothing, rather than answering zero convincingly -----
	const FElysiumGaitSpeedTable Empty;
	TestFalse(TEXT("a default table is not valid"), Empty.IsValid());
	TestEqual(TEXT("and it commands no speed"), Empty.SpeedAt(0.0f), 0.0f);
	TestEqual(TEXT("and it has no ceiling"), Empty.Peak(), 0.0f);

	FElysiumGaitSpeedTable Walk = MaleWalkFan();
	TestTrue(TEXT("the authored walk fan is valid"), Walk.IsValid());

	// --- The cells sit on the angles, and 0 lands exactly on cell 4 -----------------------------
	// This is the whole reason the axis is divided by `Count - 1` rather than by `Count`: the cells
	// are the range's endpoints, not its buckets.
	TestEqual(TEXT("forward is the 0-degree cell"), Walk.Forward(), 136.7f, 0.01f);
	TestEqual(TEXT("the backpedal is the wrap seam"), Walk.SpeedAt(-180.0f), 88.6f, 0.01f);
	TestEqual(TEXT("+180 is the same direction as -180"), Walk.SpeedAt(180.0f), 88.6f, 0.01f);
	TestEqual(TEXT("strafing right is the +90 cell"), Walk.SpeedAt(90.0f), 60.7f, 0.01f);
	TestEqual(TEXT("strafing left is the -90 cell"), Walk.SpeedAt(-90.0f), 97.1f, 0.01f);

	// --- Between two cells ----------------------------------------------------------------------
	// 20 degrees is 4/9ths of the way from cell 4 to cell 5. Retail would snap to cell 4 here — its
	// speed comes from digital keys and can only land on a cell — and a stick lands between two, so
	// the blend is the recorded divergence.
	const float Blended = FMath::Lerp(136.7f, 88.1f, 20.0f / 45.0f);
	TestEqual(TEXT("an angle between two cells blends them"), Walk.SpeedAt(20.0f), Blended, 0.01f);

	// --- The wrap, which is where an unwrapped index walks off the front ------------------------
	// A body 190 degrees off its facing is 170 degrees off it the other way. Written without the
	// double `Fmod` this indexes negatively and reads the wrong cell or crashes.
	TestEqual(TEXT("past the seam wraps rather than clamping"),
		Walk.SpeedAt(190.0f), Walk.SpeedAt(-170.0f), 0.01f);
	TestEqual(TEXT("and so does a full turn"), Walk.SpeedAt(360.0f), Walk.Forward(), 0.01f);
	TestEqual(TEXT("a turn and a bit is the bit"), Walk.SpeedAt(380.0f),
		Walk.SpeedAt(20.0f), 0.01f);

	// --- The ceiling ----------------------------------------------------------------------------
	TestEqual(TEXT("the peak is the largest cell"), Walk.Peak(), 136.7f, 0.01f);
	TestTrue(TEXT("and no direction can command more than it"),
		Walk.SpeedAt(-133.0f) <= Walk.Peak() && Walk.SpeedAt(47.0f) <= Walk.Peak());

	// --- The scale is a field, not a pre-multiply ------------------------------------------------
	// `sv_sneakscale` is 2.3, which is what makes retail's crouch faster than its walk.
	FElysiumGaitSpeedTable Sneak;
	Sneak.Count = 9;
	Sneak.AxisMin = -180.0f;
	Sneak.AxisMax = 180.0f;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Sneak.Cells[Index] = 70.0f;
	}
	Sneak.Cells[2] = 79.3f;
	Sneak.Scale = 2.3f;
	TestEqual(TEXT("the scale multiplies the peak"), Sneak.Peak(), 79.3f * 2.3f, 0.01f);
	TestEqual(TEXT("and every reading"), Sneak.Forward(), 70.0f * 2.3f, 0.01f);
	TestTrue(TEXT("so a scaled sneak outruns the authored walk"), Sneak.Forward() > Walk.Forward());

	// --- Symmetrization, which is a divergence and therefore has to be visible -------------------
	// The authored fan walks left half again as fast as it walks right. Averaging the mirrored pairs
	// removes that; the forward cell is unpaired and cannot move, and cells 0 and 8 are one clip so
	// averaging them is a no-op.
	const float MirroredMean = 0.5f * (97.1f + 60.7f);
	Walk.Symmetrize();
	TestEqual(TEXT("strafing right takes the mirrored mean"), Walk.SpeedAt(90.0f),
		MirroredMean, 0.01f);
	TestEqual(TEXT("and so does strafing left"), Walk.SpeedAt(-90.0f), MirroredMean, 0.01f);
	TestEqual(TEXT("forward is unpaired and does not move"), Walk.Forward(), 136.7f, 0.01f);
	TestEqual(TEXT("the seam cell is its own mirror"), Walk.SpeedAt(180.0f), 88.6f, 0.01f);

	// --- The set answers for the body as a whole -------------------------------------------------
	FElysiumGaitSpeeds Set;
	TestFalse(TEXT("an unresolved set is not valid"), Set.IsValid());
	Set.Walk = Walk;
	TestTrue(TEXT("one resolved gait is enough to steer by"), Set.IsValid());
	Set.Sneak = Sneak;
	TestEqual(TEXT("the set's ceiling spans every gait"), Set.Peak(),
		FMath::Max(Walk.Peak(), Sneak.Peak()), 0.01f);

	// --- The seam's decision table ---------------------------------------------------------------
	// The whole of what the mover asks. Asserted here rather than on the component because none of
	// it needs a pawn: it is a decision over body state, two cvars and a table.
	FElysiumGaitSpeeds Body;
	Body.Walk = MaleWalkFan();
	Body.Run = MaleWalkFan();
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Body.Run.Cells[Index] *= 3.5f;      // a run fan, roughly the shipped ratio
	}
	Body.Sneak = MaleWalkFan();
	Body.Sneak.Scale = 2.3f;

	FElysiumWishSpeedInput In;
	In.JumpMaxSpeed = ElysiumMove::JumpMaxSpeed;
	In.NoclipSpeed = ElysiumMove::NoclipSpeed;

	// A body with no fan at all falls back to the shipped constants.
	const FElysiumGaitSpeeds NoFan;
	TestEqual(TEXT("a body with no fan runs at speed_runbase"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::RunSpeed, 0.01f);
	In.bWalkKey = true;
	TestEqual(TEXT("...and +speed selects the slow gait"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::WalkSpeed, 0.01f);
	In.bDucked = true;
	TestEqual(TEXT("...and a ducked body takes Source's third"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::WalkSpeed / 3.0f, 0.01f);
	In.bDucked = false;
	In.bWalkKey = false;

	// With a fan: the gait's own cells, at the commanded direction.
	TestEqual(TEXT("a body with a fan runs at the run fan's forward cell"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Run.Forward(), 0.01f);
	In.bWalkKey = true;
	TestEqual(TEXT("+speed reads the walk fan"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.Forward(), 0.01f);
	In.bDucked = true;
	TestEqual(TEXT("a ducked body reads the sneak fan, with no third applied"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Sneak.Forward(), 0.01f);
	TestTrue(TEXT("...so the authored crouch outruns the authored walk, as retail's does"),
		Body.Sneak.Forward() > Body.Walk.Forward());
	In.bDucked = false;

	// The direction is the point: a strafe commands a different speed from a walk forward.
	In.WishYawDegrees = 90.0f;
	TestEqual(TEXT("a strafe commands its own cell"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.SpeedAt(90.0f), 0.01f);
	TestTrue(TEXT("...which is slower than forward on this fan"),
		ElysiumGait::WishSpeedFrom(In, Body) < Body.Walk.Forward());
	In.WishYawDegrees = 0.0f;

	// The deflection scales it, so a half-pushed stick commands half the gait.
	In.Scale = 0.5f;
	TestEqual(TEXT("the command's deflection scales the answer"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.Forward() * 0.5f, 0.01f);
	In.Scale = 1.0f;
	In.bWalkKey = false;

	// A gait that resolved no fan falls back on its own, not wholesale: this body walks and runs at
	// its authored speed and sneaks at the constant.
	FElysiumGaitSpeeds Partial = Body;
	Partial.Sneak = FElysiumGaitSpeedTable();
	In.bDucked = true;
	TestEqual(TEXT("a missing sneak fan falls back to the constant"),
		ElysiumGait::WishSpeedFrom(In, Partial), ElysiumMove::RunSpeed / 3.0f, 0.01f);
	In.bDucked = false;
	TestEqual(TEXT("...while the gaits that did resolve are unaffected"),
		ElysiumGait::WishSpeedFrom(In, Partial), Body.Run.Forward(), 0.01f);

	// Airborne: the held grounded speed, because retail's tables stop refreshing for the jump.
	// `sv_jump_maxspeed` is the ceiling and never fires — the run peak is below it.
	In.bOnGround = false;
	In.LastGroundedWishSpeed = 400.0f;
	TestEqual(TEXT("an airborne body keeps commanding what it left the ground with"),
		ElysiumGait::WishSpeedFrom(In, Body), 400.0f, 0.01f);
	In.LastGroundedWishSpeed = 0.0f;
	TestEqual(TEXT("...and falls back to sv_jump_maxspeed with nothing held"),
		ElysiumGait::WishSpeedFrom(In, Body), ElysiumMove::JumpMaxSpeed, 0.01f);
	In.bOnGround = true;

	// Noclip short-circuits the ladder: a crouched fly is not a sneak.
	In.bNoclip = true;
	In.bDucked = true;
	TestEqual(TEXT("noclip ignores the gait tables"),
		ElysiumGait::WishSpeedFrom(In, Body), ElysiumMove::NoclipSpeed, 0.01f);

	// Between two cells the answer is the blend, not the cell retail would have snapped to — the
	// recorded divergence, asserted so it cannot quietly revert.
	FElysiumWishSpeedInput Between;
	Between.WishYawDegrees = 20.0f;
	Between.bWalkKey = true;
	const float Blended20 = ElysiumGait::WishSpeedFrom(Between, Body);
	TestEqual(TEXT("an angle between two cells commands the blend"),
		Blended20, Body.Walk.SpeedAt(20.0f), 0.01f);
	TestTrue(TEXT("...which is not the cell retail would have snapped to"),
		!FMath::IsNearlyEqual(Blended20, Body.Walk.Forward(), 0.01f));

	return true;
}

// =====================================================================================
// The body sample (CCC1). One struct, two producers — so what is asserted here is the part of it
// that is a *rule* rather than a reading: how a world yaw becomes a facing-relative one, how
// Source's two duck flags become one stance, and how the jump phase falls out of the hold window
// and the vertical sign. A reading needs a body; a rule does not, and a rule the two producers
// disagreed about would be the contract failing quietly.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLocomotionSampleTest,
	"Elysium.Substrate.Locomotion", GElysiumTestFlags)
bool FElysiumLocomotionSampleTest::RunTest(const FString&)
{
	using namespace ElysiumLocomotion;

	// --- The relative yaw, which has to wrap ---------------------------------------------------
	// Every one of these is a body that is walking 20 degrees off its facing, or straight backwards.
	// Written without the wrap they come out as 340, -340 and -360.
	TestEqual(TEXT("a yaw right of facing is positive"), RelativeYaw(20.0f, 0.0f), 20.0f);
	TestEqual(TEXT("a yaw left of facing is negative"), RelativeYaw(-20.0f, 0.0f), -20.0f);
	TestEqual(TEXT("crossing north from the left"), RelativeYaw(10.0f, 350.0f), 20.0f);
	TestEqual(TEXT("crossing north from the right"), RelativeYaw(350.0f, 10.0f), -20.0f);
	TestEqual(TEXT("a full turn is no turn"), RelativeYaw(360.0f, 0.0f), 0.0f);
	// The backpedal, which is the boundary itself: it must land on one side and stay there.
	TestEqual(TEXT("straight backwards is the boundary"), FMath::Abs(RelativeYaw(180.0f, 0.0f)),
		180.0f);
	TestEqual(TEXT("and the boundary is reached the same way from either side"),
		FMath::Abs(RelativeYaw(0.0f, 180.0f)), 180.0f);

	// --- The pose parameter: recovered sign, plus a slew and a hold that are behaviour ------------
	// The three sign cases first. `RelativeYaw` is already right-positive with zero forward, which is
	// what the retail selector's reversed subtraction produces — so a strafe right is +90 and the
	// value maps onto `CalculateDirection` with no negation.
	{
		FElysiumLocomotionSample Body;
		Body.FacingYaw = 0.0f;
		Body.MoveYawPose = RelativeYaw(0.0f, Body.FacingYaw);
		TestEqual(TEXT("running forward is zero"), Body.MoveYaw(), 0.0f);
		Body.MoveYawPose = RelativeYaw(90.0f, Body.FacingYaw);
		TestEqual(TEXT("strafing right is +90"), Body.MoveYaw(), 90.0f);
		Body.MoveYawPose = RelativeYaw(-90.0f, Body.FacingYaw);
		TestEqual(TEXT("strafing left is -90"), Body.MoveYaw(), -90.0f);
		Body.MoveYawPose = RelativeYaw(180.0f, Body.FacingYaw);
		TestEqual(TEXT("backpedalling is the seam"), FMath::Abs(Body.MoveYaw()), 180.0f);
	}

	// The slew. 720 deg/s, so a sixteenth of a second covers 45 degrees and no more.
	{
		FElysiumMoveYawFilter Filter;
		// A fresh filter has never written, so its first moving frame snaps — that is what arms the
		// slew for the frames after it.
		TestEqual(TEXT("the first moving frame is where the body is going"),
			AdvanceMoveYaw(Filter, 30.0f, 100.0f, 1.0f / 60.0f), 30.0f, 0.01f);
		AdvanceMoveYaw(Filter, 0.0f, 100.0f, 1.0f / 16.0f);
		TestEqual(TEXT("...and the frame after it slews"), Filter.Value, 0.0f, 0.01f);

		const float Stepped = AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 16.0f);
		TestEqual(TEXT("a 90-degree turn is rationed to 45 in a sixteenth of a second"), Stepped,
			45.0f, 0.01f);
		TestTrue(TEXT("...so it has not arrived yet"), Stepped < 90.0f);
		// And it does arrive, rather than easing forever.
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 16.0f);
		}
		TestEqual(TEXT("...and it lands exactly on the target"), Filter.Value, 90.0f, 0.01f);
	}

	// The wrap: a turn across the seam takes the short way round, which is the whole reason this is
	// a fixed turn and not a lerp. Written as a lerp, -170 to +170 sweeps 340 degrees the wrong way.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, -170.0f, 100.0f, 1.0f / 60.0f);
		const float Crossed = AdvanceMoveYaw(Filter, 170.0f, 100.0f, 1.0f / 120.0f);
		TestTrue(TEXT("a turn across the seam goes the short way"), Crossed < -170.0f);
		TestTrue(TEXT("...staying on the near side of the boundary"), Crossed >= -180.0f);
	}

	// The hold. A body that stops keeps the direction it was going — retail's write is gated on
	// movement, so nothing is written and the parameter does not fall back to forward.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 60.0f);
		TestEqual(TEXT("a strafing body's stride is sideways"), Filter.Value, 90.0f, 0.01f);
		const float Stopped = AdvanceMoveYaw(Filter, 0.0f, 0.0f, 1.0f / 60.0f);
		TestEqual(TEXT("and stopping holds it rather than snapping forward"), Stopped, 90.0f, 0.01f);
	}

	// The re-arm. Because the write is gated on movement, 0.3 s without one means the body has been
	// standing still — so a standing start snaps to the new direction instead of slewing into it,
	// which is what stops an eighth of a second of walking forward out of every sidestep.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, 0.0f, 100.0f, 1.0f / 60.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)      // half a second stationary
		{
			AdvanceMoveYaw(Filter, 0.0f, 0.0f, 1.0f / 60.0f);
		}
		TestEqual(TEXT("a standing start snaps to the direction it leaves in"),
			AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 60.0f), 90.0f, 0.01f);

		// A brief stumble is not a standing start, and slews.
		FElysiumMoveYawFilter Stumble;
		AdvanceMoveYaw(Stumble, 0.0f, 100.0f, 1.0f / 60.0f);
		AdvanceMoveYaw(Stumble, 0.0f, 0.0f, 0.1f);
		const float Slewed = AdvanceMoveYaw(Stumble, 90.0f, 100.0f, 1.0f / 60.0f);
		TestTrue(TEXT("a momentary stop still slews"), Slewed < 90.0f);
	}

	// --- The stance, which is four values because the flags are two -----------------------------
	// The pair that matters is the last one: the release edge sets `bDucking` while `bDucked` is
	// still true, and under a low ceiling the body stays there rather than passing through it. A
	// three-value stance reports that as an ordinary crouch and loses the stand-up entirely.
	TestEqual(TEXT("neither flag is standing"),
		static_cast<int32>(StanceFrom(false, false)),
		static_cast<int32>(EElysiumStance::Standing));
	TestEqual(TEXT("ducking alone is the duck ramp"),
		static_cast<int32>(StanceFrom(false, true)),
		static_cast<int32>(EElysiumStance::Lowering));
	TestEqual(TEXT("ducked alone is the settled crouch"),
		static_cast<int32>(StanceFrom(true, false)),
		static_cast<int32>(EElysiumStance::Ducked));
	TestEqual(TEXT("both is the unduck ramp, not a deeper crouch"),
		static_cast<int32>(StanceFrom(true, true)),
		static_cast<int32>(EElysiumStance::Rising));

	// --- The jump phase, derived rather than stored ----------------------------------------------
	FElysiumLocomotionSample S;
	S.bOnGround = true;
	S.LocalVelocity.Z = 400.0f;
	S.JumpHoldRemaining = 0.1f;
	TestEqual(TEXT("a grounded body is grounded whatever it carries"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Grounded));

	S.bOnGround = false;
	TestEqual(TEXT("rising is ascending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Ascend));

	// The held push is the other half: VtMB's jump keeps pushing while the button is down, so the
	// window being open means ascending even on the frame the vertical sign has already turned.
	S.LocalVelocity.Z = -1.0f;
	TestEqual(TEXT("an open hold window is still ascending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Ascend));

	S.JumpHoldRemaining = 0.0f;
	TestEqual(TEXT("falling with the window closed is descending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Descend));

	// --- The speed is a derivation, so it cannot disagree with the velocity ----------------------
	S.LocalVelocity = FVector(30.0f, 40.0f, -900.0f);
	TestEqual(TEXT("speed is the planar magnitude and ignores the fall"), S.Speed2D(), 50.0f);

	// A default sample is a body standing still that is asking for nothing, and the zero wish scale
	// is what distinguishes that from a body asking to walk straight ahead.
	const FElysiumLocomotionSample Fresh;
	TestEqual(TEXT("a fresh sample is at rest"), Fresh.Speed2D(), 0.0f);
	TestEqual(TEXT("a fresh sample commands nothing"), Fresh.WishScale, 0.0f);
	TestEqual(TEXT("a fresh sample is standing"),
		static_cast<int32>(Fresh.Stance), static_cast<int32>(EElysiumStance::Standing));
	return true;
}

// =====================================================================================
// The recorded channels (CCC0). A value that reaches disk with nothing that knows how to compare
// it is the failure this registry exists to close, so what is asserted is that every declaration
// carries a usable rule and that the recorder refuses anything undeclared.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChannelRegistryTest,
	"Elysium.Substrate.ChannelRegistry", GElysiumTestFlags)
bool FElysiumChannelRegistryTest::RunTest(const FString&)
{
	using namespace ElysiumChannels;

	TSet<FString> Names;
	int32 FrameChannels = 0;
	int32 RunChannels = 0;

	for (const FChannelDef& Def : Defs())
	{
		const FString Name = Def.Name ? FString(Def.Name) : FString();
		TestFalse(TEXT("a channel is named"), Name.IsEmpty());
		TestFalse(FString::Printf(TEXT("'%s' is declared once"), *Name), Names.Contains(Name));
		Names.Add(Name);

		TestTrue(FString::Printf(TEXT("'%s' names a producer"), *Name),
			Def.Producer && FCString::Strlen(Def.Producer) > 0);
		TestTrue(FString::Printf(TEXT("'%s' says what it is"), *Name),
			Def.Help && FCString::Strlen(Def.Help) > 0);

		// The rule itself. A toleranced channel with no tolerance is exactly the silent-pass case the
		// registry replaces, so it is a failure here rather than a surprise in the differ. `Angle` is
		// toleranced like `Numeric` — it differs only in how the difference is taken.
		if (Def.Kind == EKind::Exact)
		{
			TestEqual(FString::Printf(TEXT("exact '%s' carries no tolerance"), *Name),
				Def.Tolerance, 0.0f);
			TestEqual(FString::Printf(TEXT("exact '%s' prints no decimals"), *Name),
				static_cast<int32>(Def.Precision), 0);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("toleranced '%s' carries a tolerance"), *Name),
				Def.Tolerance > 0.0f);
			TestTrue(FString::Printf(TEXT("toleranced '%s' names its unit or is dimensionless"), *Name),
				Def.Unit != nullptr);
			if (Def.Kind == EKind::Angle)
			{
				// The wrapped comparison is only meaningful on degrees. A radian channel declared
				// Angle would be compared against a 360 that is not its period.
				TestEqual(FString::Printf(TEXT("angle '%s' is measured in degrees"), *Name),
					FString(Def.Unit ? Def.Unit : TEXT("")), FString(TEXT("deg")));
			}
		}
		TestTrue(FString::Printf(TEXT("'%s' prints a sane number of decimals"), *Name),
			Def.Precision >= 0 && Def.Precision <= 6);

		(Def.Scope == EScope::Frame ? FrameChannels : RunChannels)++;

		TestEqual(FString::Printf(TEXT("'%s' is findable by name"), *Name), Find(Def.Name), &Def);
	}

	TestTrue(TEXT("there are frame channels"), FrameChannels > 0);
	TestTrue(TEXT("there are run channels"), RunChannels > 0);
	TestNull(TEXT("an unknown name resolves to nothing"), Find(TEXT("no_such_channel")));

	// Every per-frame channel is speed-dependent, and that is structural rather than incidental:
	// *when* a body reaches a feature moves with its gait even when *whether* it does not. The
	// committed baselines are the run channels, which is what lets them be promoted before `CCC7`.
	for (const FChannelDef& Def : Defs())
	{
		if (Def.Scope == EScope::Frame)
		{
			TestTrue(FString::Printf(TEXT("frame channel '%s' is speed-dependent"), Def.Name),
				Def.bSpeedDependent);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChannelRecorderTest,
	"Elysium.Substrate.ChannelRecorder", GElysiumTestFlags)
bool FElysiumChannelRecorderTest::RunTest(const FString&)
{
	static const TCHAR* const Cols[] = { TEXT("frame"), TEXT("pz"), TEXT("onground") };

	// --- Undeclared names are refused at the door ---------------------------------------------
	{
		FElysiumChannelRecorder R;
		FString Error;
		static const TCHAR* const Bogus[] = { TEXT("frame"), TEXT("no_such_channel") };
		TestFalse(TEXT("an unregistered channel cannot be opened"), R.Open(Bogus, Error));
		TestTrue(TEXT("and the refusal names it"), Error.Contains(TEXT("no_such_channel")));

		static const TCHAR* const Dup[] = { TEXT("frame"), TEXT("frame") };
		TestFalse(TEXT("a duplicated channel cannot be opened"), R.Open(Dup, Error));

		// A run channel is not a column; putting one in the CSV is how a value ends up compared
		// against the wrong thing.
		static const TCHAR* const Mixed[] = { TEXT("frame"), TEXT("top_stand") };
		TestFalse(TEXT("a run channel cannot be a column"), R.Open(Mixed, Error));
	}

	// --- A complete frame, and an incomplete one ----------------------------------------------
	{
		FElysiumChannelRecorder R;
		FString Error;
		TestTrue(TEXT("the declared channels open"), R.Open(Cols, Error));

		R.BeginFrame();
		R.Set(TEXT("frame"), 0);
		R.Set(TEXT("pz"), 12.5);
		TestFalse(TEXT("a frame missing a channel is refused"), R.EndFrame(Error));
		TestTrue(TEXT("and the refusal names it"), Error.Contains(TEXT("onground")));
		TestEqual(TEXT("the half-written frame is discarded"), R.FrameCount(), 0);

		R.BeginFrame();
		R.Set(TEXT("frame"), 0);
		R.Set(TEXT("pz"), 12.5);
		R.Set(TEXT("onground"), true);
		TestTrue(TEXT("a complete frame is accepted"), R.EndFrame(Error));

		R.BeginFrame();
		R.Set(TEXT("frame"), 1);
		R.Set(TEXT("pz"), -0.00001);      // rounds to all zeros at four decimals
		R.Set(TEXT("onground"), false);
		TestTrue(TEXT("a second frame is accepted"), R.EndFrame(Error));
		TestEqual(TEXT("two frames were recorded"), R.FrameCount(), 2);

		R.SetRun(TEXT("top_stand"), 18.0);
		R.SetMeta(TEXT("course"), TEXT("gym_riser_0"));
		R.SetConstant(TEXT("StepSize"), 45.72);

		FString Csv;
		FString Manifest;
		R.Serialize(Csv, Manifest);

		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		TestEqual(TEXT("a header and one line per frame"), Lines.Num(), 3);
		TestEqual(TEXT("the header is the declared order"), Lines[0], FString(TEXT("frame,pz,onground")));
		TestEqual(TEXT("values print at the channel's own precision"), Lines[1],
			FString(TEXT("0,12.5000,1")));
		// A negative zero prints with a sign and compares equal to the one without, which is a
		// baseline that fails for no reason.
		TestEqual(TEXT("a negative zero loses its sign"), Lines[2], FString(TEXT("1,0.0000,0")));

		TestTrue(TEXT("the manifest declares the frame channels"),
			Manifest.Contains(TEXT("\"name\": \"pz\"")) && Manifest.Contains(TEXT("\"scope\": \"frame\"")));
		TestTrue(TEXT("with the tolerance the differ must apply"),
			Manifest.Contains(TEXT("\"tolerance\": 0.250000")));
		TestTrue(TEXT("the run channel carries its measured value"),
			Manifest.Contains(TEXT("\"name\": \"top_stand\"")) && Manifest.Contains(TEXT("\"value\": 18.000")));
		TestTrue(TEXT("the metadata rides along"), Manifest.Contains(TEXT("\"course\": \"gym_riser_0\"")));
		TestTrue(TEXT("and so do the constants the geometry came from"),
			Manifest.Contains(TEXT("\"StepSize\": 45.720000")));
	}

	// --- A run channel that is not registered as one is reported, never accepted ---------------
	{
		FElysiumChannelRecorder R;
		FString Error;
		TestTrue(TEXT("open"), R.Open(Cols, Error));
		R.SetRun(TEXT("pz"), 1.0);                 // frame-scoped, not a run channel
		R.SetRun(TEXT("no_such_channel"), 1.0);
		TestEqual(TEXT("both misuses are recorded"), R.Errors().Num(), 2);

		FString Csv;
		FString Manifest;
		R.Serialize(Csv, Manifest);
		TestTrue(TEXT("and the manifest carries them where a run cannot hide them"),
			Manifest.Contains(TEXT("\"errors\"")));
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

	// **Pause is reachable only from Playing.** The empty front-end shell has no run to hold, so Esc
	// there must be a no-op rather than a hold.
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
	TestTrue(TEXT("Auto cursor is visible for mouse and keyboard"),
		ElysiumInput::ResolveCursorVisible(EElysiumCursorPolicy::Auto, true));
	TestFalse(TEXT("Auto cursor is hidden for gamepad"),
		ElysiumInput::ResolveCursorVisible(EElysiumCursorPolicy::Auto, false));
	TestTrue(TEXT("Always cursor ignores a gamepad device"),
		ElysiumInput::ResolveCursorVisible(EElysiumCursorPolicy::Always, false));
	TestFalse(TEXT("Never cursor ignores a mouse device"),
		ElysiumInput::ResolveCursorVisible(EElysiumCursorPolicy::Never, true));

	// The scopes the game actually pushes, as data — so a transition is a pair of these rather
	// than a hand-written sequence, and the table below can walk every ordered pair.
	auto MakeScope = [](const TCHAR* Name, int32 Priority, EMode Mode,
		EElysiumCursorPolicy CursorPolicy)
	{
		FElysiumInputScope Scope;
		Scope.Name = Name;
		Scope.Priority = Priority;
		Scope.Mode = Mode;
		Scope.CursorPolicy = CursorPolicy;
		return Scope;
	};
	const FElysiumInputScope Sign = MakeScope(TEXT("Sign"), Prio::Sign,
		EMode::UIOnly, EElysiumCursorPolicy::Auto);
	const FElysiumInputScope Cinematic = MakeScope(TEXT("Cinematic"), Prio::Cinematic,
		EMode::GameOnly, EElysiumCursorPolicy::Never);
	const FElysiumInputScope Chargen = MakeScope(TEXT("Chargen"), Prio::Chargen,
		EMode::UIOnly, EElysiumCursorPolicy::Auto);
	const FElysiumInputScope Dialogue = MakeScope(TEXT("Dialogue"), Prio::Dialogue,
		EMode::UIOnly, EElysiumCursorPolicy::Auto);
	const FElysiumInputScope Character = MakeScope(TEXT("Character"), Prio::Character,
		EMode::UIOnly, EElysiumCursorPolicy::Auto);
	const FElysiumInputScope Menu = MakeScope(TEXT("Menu"), Prio::Menu,
		EMode::UIOnly, EElysiumCursorPolicy::Auto);
	FElysiumInputScope Debug = MakeScope(TEXT("Debug"), Prio::Debug,
		EMode::GameOnly, EElysiumCursorPolicy::Always);
	ElysiumInput::AddPlayerContexts(Debug.Contexts);

	// --- the empty stack is the game holding the mouse ---
	{
		FElysiumInputScopeStack Stack;
		const FElysiumInputState Base = Stack.Resolve();
		TestTrue(TEXT("an empty stack has no top"), Stack.Top() == nullptr);
		TestTrue(TEXT("empty resolves to the gameplay default"), Base.Mode == EMode::GameOnly);
		TestEqual(TEXT("gameplay never shows a cursor"), Base.CursorPolicy,
			EElysiumCursorPolicy::Never);
		TestTrue(TEXT("no deciding scope"), Base.Name.IsNone());
		TestEqual(TEXT("gameplay applies both device contexts"), Base.Contexts.Num(), 2);
		TestEqual(TEXT("keyboard/mouse context is first"), Base.Contexts[0],
			ElysiumInput::PlayerKeyboardMouseContext());
		TestEqual(TEXT("gamepad context is second"), Base.Contexts[1],
			ElysiumInput::PlayerGamepadContext());
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
		TestTrue(TEXT("and with it UI-only capture"), Stack.Resolve().Mode == EMode::UIOnly);
		TestTrue(TEXT("the sign removes gameplay contexts"), Stack.Resolve().Contexts.IsEmpty());
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
		const TArray<FElysiumInputScope> Screens = {
			Sign, Cinematic, Chargen, Dialogue, Character, Menu, Debug
		};
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
	TestTrue(TEXT("a sign revokes debug capture because it is now a UI modal"),
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
	Gameplay.bAnimationPreloadReady = true;
	TestEqual(TEXT("construction has not completed yet"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Waiting);
	Gameplay.bConstructionComplete = true;
	TestEqual(TEXT("all gameplay prerequisites open the gate in any completion order"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites Backdrop;
	Backdrop.bMenuBackdrop = true;
	Backdrop.bConstructionComplete = true;
	Backdrop.bEntityWorldReady = true;
	Backdrop.bAnimationPreloadReady = true;
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

	FElysiumMapRuntimePrerequisites MissingAnimations = Gameplay;
	MissingAnimations.bAnimationPreloadReady = false;
	TestEqual(TEXT("a completed build cannot activate before its map animations are resident"),
		MissingAnimations.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("animation residency failure is structured"),
		Failure.Contains(TEXT("animation residency")));

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

	// The primary actor tick is an explicit barrier because Unreal makes an NPC movement component
	// depend on the actor owning its current floor — this map actor. It carries no GameFrame work.
	TestTrue(TEXT("the movement-base barrier ticks"), Map->PrimaryActorTick.bCanEverTick);
	TestEqual(TEXT("the movement-base barrier is TG_PrePhysics"),
		static_cast<int32>(Map->PrimaryActorTick.TickGroup), static_cast<int32>(TG_PrePhysics));

	// Steps 5-6 also run before physics, but AFTER both player and NPC movement. A separate tick is
	// what permits the forward movement->gameplay edge without closing the floor-owner cycle.
	TestTrue(TEXT("the gameplay pass ticks"), Map->GameplayTickFunction.bCanEverTick);
	TestTrue(TEXT("the gameplay pass starts enabled"), Map->GameplayTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the gameplay pass is TG_PrePhysics"),
		static_cast<int32>(Map->GameplayTickFunction.TickGroup), static_cast<int32>(TG_PrePhysics));

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
	// final positions. Four tick functions, not separate actors — one actor keeps the order declarable.
	TestTrue(TEXT("the post-move pass ticks"), Map->PostMoveTickFunction.bCanEverTick);
	TestTrue(TEXT("the post-move pass starts enabled"), Map->PostMoveTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the post-move pass is TG_PostPhysics"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup), static_cast<int32>(TG_PostPhysics));
	TestTrue(TEXT("the post-move pass is later than both pre-physics passes"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup)
			> static_cast<int32>(Map->PreMoveTickFunction.TickGroup));

	// CCC1 — and it is later than the mover too, which is what makes the post-move pass a safe place
	// to read the published body sample: by the time it runs, the mover's tick tail has written it.
	//
	// What this does **not** cover is the player visual's own ordering. `AElysiumMapActor::
	// BuildPlayerVisual` adds the mesh -> mover tick prerequisite when it attaches the visual, and a
	// per-instance prerequisite created at runtime is not reachable from a class default. It is
	// asserted by the built world or not at all; asserting an engine default here instead would be
	// asserting somebody else's ordering and calling it ours.
	if (Mover)
	{
		TestTrue(TEXT("the post-move pass is later than the mover, so the body sample is settled"),
			static_cast<int32>(Map->PostMoveTickFunction.TickGroup)
				> static_cast<int32>(Mover->PrimaryComponentTick.TickGroup));
	}

	// CCC4 — the cast's half of the same rule. An NPC body's actor tick carries no ordering against
	// its own CharacterMovement (the engine wires no such prerequisite; `ACharacter` orders only the
	// mesh), so a selection read from `Tick` would be reading whichever of the two happened to
	// register first. The animation pass is therefore its own tick function, in the same group as the
	// player's — declared on the class, which is what makes it assertable here at all.
	const AElysiumNpcBody* Npc = GetDefault<AElysiumNpcBody>();
	if (TestNotNull(TEXT("NPC body class defaults"), Npc))
	{
		TestTrue(TEXT("the NPC animation pass ticks"), Npc->AnimTickFunction.bCanEverTick);
		TestTrue(TEXT("the NPC animation pass starts enabled"),
			Npc->AnimTickFunction.bStartWithTickEnabled);
		TestEqual(TEXT("the NPC animation pass is TG_PostPhysics"),
			static_cast<int32>(Npc->AnimTickFunction.TickGroup), static_cast<int32>(TG_PostPhysics));
		TestFalse(TEXT("and it stops while the world is held"),
			Npc->AnimTickFunction.bTickEvenWhenPaused);
		if (const UCharacterMovementComponent* NpcMove = Npc->GetCharacterMovement())
		{
			TestTrue(TEXT("the NPC animation pass is later than its own mover, so its sample is settled"),
				static_cast<int32>(Npc->AnimTickFunction.TickGroup)
					> static_cast<int32>(NpcMove->PrimaryComponentTick.TickGroup));
		}
		// The turn-in-place stays where it was: adding the selection did not move an existing behaviour
		// a frame later.
		TestEqual(TEXT("the NPC actor tick stays in pre-physics"),
			static_cast<int32>(Npc->PrimaryActorTick.TickGroup), static_cast<int32>(TG_PrePhysics));
	}

	// Step 10 rebuilds the view state after everything that could change it has run, so it is later
	// than all four map passes (11.8).
	const UElysiumPresentationSubsystem* Present = GetDefault<UElysiumPresentationSubsystem>();
	if (TestNotNull(TEXT("presentation subsystem class defaults"), Present))
	{
		TestTrue(TEXT("the publish pass ticks"), Present->PublishTickFunction.bCanEverTick);
		TestTrue(TEXT("the publish pass starts enabled"), Present->PublishTickFunction.bStartWithTickEnabled);
		TestEqual(TEXT("the publish pass is TG_PostUpdateWork"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup), static_cast<int32>(TG_PostUpdateWork));
		TestTrue(TEXT("the publish pass is last of the five"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup)
				> static_cast<int32>(Map->PostMoveTickFunction.TickGroup));
	}

	// §4 — before activation, the lifecycle-bearing passes are allowed to poll while held, but
	// their gameplay branches are phase-gated. ActivateRuntime restores their flags to false; the
	// post-move gameplay pass is never needed for readiness. Presentation remains live throughout.
	TestTrue(TEXT("the pre-move pass can poll readiness while held"), Map->PreMoveTickFunction.bTickEvenWhenPaused);
	TestTrue(TEXT("the movement-base barrier can run while held"), Map->PrimaryActorTick.bTickEvenWhenPaused);
	TestTrue(TEXT("the activation pass can run while held"), Map->GameplayTickFunction.bTickEvenWhenPaused);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHullCollisionBoundsTest,
	"Elysium.Substrate.HullCollisionBounds", GElysiumTestFlags)
bool FElysiumHullCollisionBoundsTest::RunTest(const FString&)
{
	UElysiumHullCollisionComponent* Hull = NewObject<UElysiumHullCollisionComponent>();
	if (!TestNotNull(TEXT("collision-only hull component constructs"), Hull))
	{
		return false;
	}

	const FBox Local(FVector(-10.0, -20.0, -30.0), FVector(40.0, 50.0, 60.0));
	Hull->SetLocalCollisionBounds(Local);
	const FBox World = Hull->CalcBounds(FTransform(FVector(100.0, 200.0, 300.0))).GetBox();
	TestTrue(TEXT("collision-only hull bounds remain valid without a render section"), World.IsValid != 0);
	TestEqual(TEXT("collision-only hull minimum transforms into world space"),
		World.Min, FVector(90.0, 180.0, 270.0));
	TestEqual(TEXT("collision-only hull maximum transforms into world space"),
		World.Max, FVector(140.0, 250.0, 360.0));
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
// FElysiumNpc / npc_maker — registry coverage, npc_maker.Spawn creating a live child on a bare
// world, dialogue latches, and the engine-neutral half of named patrol resolution/persistence.
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
		TEXT("EndDialog"), TEXT("SetupPatrolType"), TEXT("FollowPatrolPath"),
		TEXT("ClearPatrolPath"), TEXT("Kill") })
	{
		TestNotNull(FString::Printf(TEXT("npc_VVampire.%s resolves"), In),
			reinterpret_cast<const void*>(Reg.FindInput(*Vamp, FName(In))));
	}
	// 9.3 field-table audit: `npc.times_talked` is a script-read field (santamonica et al.), so it must
	// resolve as a datamap field — otherwise the read raises AttributeError instead of returning 0.
	TestNotNull(TEXT("npc_VVampire.times_talked resolves as a field"),
		reinterpret_cast<const void*>(Reg.FindField(*Vamp, FName(TEXT("times_talked")))));
	TestNotNull(TEXT("npc_VVampire.interesting_place_groups resolves as a field"),
		reinterpret_cast<const void*>(Reg.FindField(*Vamp,
			FName(TEXT("interesting_place_groups")))));

	// The typo is the retail classname, not a fixture typo. It must resolve as a live capacity/
	// timing entity or sm_hub_1's 76 authored destinations remain inert records.
	const FElysiumClassDesc* InterestingDesc = Reg.Find(FName(TEXT("intersting_place")));
	if (!TestNotNull(TEXT("intersting_place registered with retail spelling"), InterestingDesc))
	{
		return false;
	}
	for (const TCHAR* In : { TEXT("Enable"), TEXT("Disable"), TEXT("Toggle") })
	{
		TestNotNull(FString::Printf(TEXT("intersting_place.%s resolves"), In),
			reinterpret_cast<const void*>(Reg.FindInput(*InterestingDesc, FName(In))));
	}
	for (const TCHAR* Field : { TEXT("type"), TEXT("enabled"), TEXT("max_npcs"),
		TEXT("group_id"), TEXT("match_orientation"), TEXT("min_time"), TEXT("max_time") })
	{
		TestNotNull(FString::Printf(TEXT("intersting_place.%s resolves"), Field),
			reinterpret_cast<const void*>(Reg.FindField(*InterestingDesc, FName(Field))));
	}

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

	// --- A recorded world: Jack, a counter wired off his OnDialogBegin, and the blueblood maker ---
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npc_test__");

	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
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
	BluebloodMaker.Keys.Add(TEXT("use_interesting"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("interesting_place_groups"), TEXT("31"));
	Defs.Defs.Add(MoveTemp(BluebloodMaker));

	FElysiumEntityDef Interesting;
	Interesting.Classname = TEXT("intersting_place");
	Interesting.TargetName = TEXT("ambient_1");
	Interesting.Origin = FVector(50.0f, 25.0f, 0.0f);
	Interesting.Keys.Add(TEXT("type"), TEXT("Citizen_Idle"));
	Interesting.Keys.Add(TEXT("enabled"), TEXT("1"));
	Interesting.Keys.Add(TEXT("max_npcs"), TEXT("4"));
	Interesting.Keys.Add(TEXT("group_id"), TEXT("31"));
	Interesting.Keys.Add(TEXT("min_time"), TEXT("10"));
	Interesting.Keys.Add(TEXT("max_time"), TEXT("20"));
	Defs.Defs.Add(MoveTemp(Interesting));

	for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
	{
		FElysiumEntityDef Point;
		Point.Classname = TEXT("info_node_patrol_point");
		Point.TargetName = FString::Printf(TEXT("route_%d"), PointIndex);
		Point.Origin = FVector(static_cast<float>(PointIndex * 100), 25.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Point));
	}

	FElysiumRecordingServices Services;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* JackEnt = World.FindByName(TEXT("Jack"));
	FElysiumEntity* MakerEnt = World.FindByName(TEXT("blueblood_maker"));
	if (!TestNotNull(TEXT("Jack resolved"), JackEnt) || !TestNotNull(TEXT("maker resolved"), MakerEnt))
	{
		return false;
	}
	TestFalse(TEXT("Jack is a real class, not an inert record"), JackEnt->IsRecordOnly());
	FElysiumEntity* InterestingEnt = World.FindByName(TEXT("ambient_1"));
	if (TestNotNull(TEXT("interesting place resolved"), InterestingEnt))
	{
		TestFalse(TEXT("interesting place is live rather than record-only"),
			InterestingEnt->IsRecordOnly());
	}
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
	if (Blueblood)
	{
		TestEqual(TEXT("maker forwards interesting-place groups"),
			DebugRow(Blueblood, TEXT("Interesting groups")), FString(TEXT("31")));
	}

	TestEqual(TEXT("WillTalk latched"), DebugRow(World.Resolve(JackHandle), TEXT("WillTalk")), FString(TEXT("yes")));
	TestEqual(TEXT("OnDialogBegin fired once (counter=1)"),
		FCString::Atof(*DebugRow(World.FindByName(TEXT("dlgcount")), TEXT("Value"))), 1.0f);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("EndDialog")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.0);

	// The Python surface calls these exact names on sm_hub_1's two cops. The recording motor keeps
	// the route engine-neutral while making its move/stop requests observable in this tier.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("SetupPatrolType")),
		FElysiumVariant::String(TEXT("255 0 FOLLOW_PATROL_PATH_WALK")), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
		FElysiumVariant::String(TEXT("route_1 route_2")), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.0);
	TestTrue(TEXT("named patrol resolves and arms both authored points"),
		DebugRow(World.Resolve(JackHandle), TEXT("Patrol")).Contains(TEXT("point 1/2")));
	World.Tick(0.05);
	FElysiumRecordingNpcMotor* JackMotor = Services.NpcMotors.IsEmpty()
		? nullptr : Services.NpcMotors[0].Get();
	if (TestNotNull(TEXT("Jack owns the recording motor"), JackMotor))
	{
		TestTrue(TEXT("an active patrol issues a native movement request"), JackMotor->bMoving);
		TestTrue(TEXT("the native request carries the first authored point"),
			JackMotor->RequestedFeet.Equals(FVector(100.0f, 25.0f, 0.0f)));
	}

	FElysiumMapSnapshot PatrolSnapshot;
	World.Freeze(PatrolSnapshot);
	const FElysiumEntityState* SavedJack = PatrolSnapshot.Entities.FindByPredicate(
		[JackHandle](const FElysiumEntityState& State) { return State.Index == JackHandle.Index; });
	if (TestNotNull(TEXT("active patrol contributes a save record"), SavedJack))
	{
		TestTrue(TEXT("patrol cursor/path are carried by leaf state"), !SavedJack->LeafState.IsEmpty());
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("ClearPatrolPath")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.05);
	if (JackMotor)
	{
		TestFalse(TEXT("clearing a patrol stops its native request"), JackMotor->bMoving);
		TestTrue(TEXT("clearing a patrol crosses the explicit motor Stop seam"),
			Services.Saw(TEXT("NpcMotor Stop")));
	}

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
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: embedded CPython unavailable (%s)"), *Err));
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
// scripted_sequence locomotion (8.5) — the travel phase, over the recording motor.
//
// The claim: a beat whose `m_fMoveTo` says walk sends its NPC to the mark under its own power
// and holds `OnEndSequence` until it gets there. 132 of the 188 exported sequences travel, and
// the ones that carry no `m_iszPlay` — sp_theatre's five-beat courtroom walk-out among them —
// have nothing BUT the transit to time their outputs off, so a beat that ends at the input
// collapses the shot to a blink. The mirror claim is that no failure can hang the beat: a mark
// with no path falls back to the placement and ends in the same pass.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceLocomotionTest,
	"Elysium.Substrate.ScriptedSequenceLocomotion", GElysiumTestFlags)
bool FElysiumScriptedSequenceLocomotionTest::RunTest(const FString&)
{
	const FVector Spawn(0.f, 0.f, 0.f);
	const FVector Mark(1000.f, 0.f, 0.f);

	auto BuildDefs = [&Mark](FElysiumEntityDefs& Defs)
	{
		Defs.MapName = TEXT("__walkout__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Isaac");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/isaac/isaac.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		// walk_out_people_walk_3's shape: walk, no action animation, one OnEndSequence wire.
		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("walk_out");
		Seq.Origin = Mark;
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Isaac"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("1"));
		Seq.Keys.Add(TEXT("angles"), TEXT("0 270 0"));
		FElysiumOutputDef W;
		W.Name = TEXT("OnEndSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("5");
		Seq.Outputs.Add(W);
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
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

	// --- The beat walks, and holds its output until the mark is reached ---------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityClip = TEXT("walk_0");
		Services.ResolvedNpcGroundSpeedCmPerSecond = 136.7f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Isaac"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("walk_out resolved"), Seq) || !TestNotNull(TEXT("Isaac resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count)
			|| !TestNotNull(TEXT("Isaac stands on a motor"), Motor))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		double Now = 0.0;
		for (int32 i = 0; i < 10; ++i) { World.Tick(Now); Now += 0.1; }

		TestTrue(TEXT("the beat asked the motor for the mark"),
			Motor->RequestedFeet.Equals(Mark, 0.01));
		TestTrue(TEXT("the scripted walk uses the selected clip's authored ground speed"),
			FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 136.7f, 0.01f));
		TestTrue(TEXT("the speed came from the concrete forward walk cell"),
			Services.Saw(TEXT("ResolveNpcActivityClip isaac ACT_WALK")));
		TestTrue(TEXT("the activity label entered the global bank resolver after the move"),
			Services.Saw(TEXT("PlayNpcClip isaac walk loop=1")));
		TestFalse(TEXT("the concrete bank cell was not mistaken for a vocabulary label"),
			Services.Saw(TEXT("PlayNpcClip isaac walk_0")));
		TestFalse(TEXT("the resolved scripted walk was not selected a second time"),
			Services.Saw(TEXT("PlayNpcActivity isaac ACT_WALK")));
		TestFalse(TEXT("the NPC was not teleported onto the mark"), Npc->Origin.Equals(Mark, 0.01));
		TestEqual(TEXT("OnEndSequence is held while the NPC is still walking"),
			CounterValue(Count), 0.f);

		// The body reaches the mark; the beat then turns it onto the mark's angles before ending.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		World.Tick(Now); Now += 0.1;
		TestTrue(TEXT("arriving starts the turn onto the mark"),
			Services.Calls.ContainsByPredicate([](const FString& C) { return C.StartsWith(TEXT("NpcMotor Face")); }));
		TestEqual(TEXT("OnEndSequence is still held through the turn"), CounterValue(Count), 0.f);

		// The turn is bounded, so the beat completes even against a motor that never reports level.
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("OnEndSequence fires once the beat's travel is over"), CounterValue(Count), 5.f);
	}

	// --- A mark with no path still ends the beat, on the placement fallback -----------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityClip = TEXT("walk_0");
		Services.ResolvedNpcGroundSpeedCmPerSecond = 0.f;   // older sidecar: clip, no motion block
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Isaac"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("walk_out resolved"), Seq) || !TestNotNull(TEXT("Isaac resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (Motor)
		{
			Motor->bAcceptMoves = false;   // the navigation graph has nothing to offer this mark
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

		TestTrue(TEXT("an unreachable mark places the NPC on it instead"), Npc->Origin.Equals(Mark, 0.01));
		TestTrue(TEXT("and applies the mark's facing"),
			FMath::IsNearlyEqual(Npc->Angles.Y, 270.f, 0.01f));
		if (Motor)
		{
			TestTrue(TEXT("an older sidecar without motion retains the scripted-walk fallback speed"),
				FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 254.f, 0.01f));
		}
		TestEqual(TEXT("and the beat still fires OnEndSequence"), CounterValue(Count), 5.f);
	}

	return true;
}

// =====================================================================================
// The three VtMB spawnflag additions on CCineNPC (`docs/vtmb/entity_io.md`): 256 holds the
// post-idle so the beat never completes, 512 makes the beat's claim on its NPC unbreakable,
// and 4096 turns off character collision for the beat's duration. sp_theatre's courtroom
// walk-out authors all three — `0x1260` on the five who walk, `0x360` on the two who stand.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceFlagsTest,
	"Elysium.Substrate.ScriptedSequenceFlags", GElysiumTestFlags)
bool FElysiumScriptedSequenceFlagsTest::RunTest(const FString&)
{
	// One NPC, and two beats aimed at it so the queue rules have something to arbitrate.
	// `MoveTo` 1 keeps a beat alive in its travel phase for as long as the motor reports Moving,
	// which is the only way to observe state a beat holds *while running*; `MoveTo` 0 with no action
	// animation is the in-place shape Ash and Damsel use, and ends in the pass that starts it.
	auto BuildDefs = [](FElysiumEntityDefs& Defs, int32 FirstFlags, const TCHAR* PostIdle, int32 MoveTo)
	{
		Defs.MapName = TEXT("__flags__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Damsel");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/downtown/damsel/damsel.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("beat_a");
		Seq.Origin = FVector(1000.f, 0.f, 0.f);
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), *FString::FromInt(MoveTo));
		Seq.Keys.Add(TEXT("spawnflags"), *FString::FromInt(FirstFlags));
		if (PostIdle && *PostIdle)
		{
			Seq.Keys.Add(TEXT("m_iszPostIdle"), PostIdle);
		}
		FElysiumOutputDef W;
		W.Name = TEXT("OnEndSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("5");
		Seq.Outputs.Add(W);
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Other;
		Other.Classname = TEXT("scripted_sequence");
		Other.TargetName = TEXT("beat_b");
		Other.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Other.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
		FElysiumOutputDef W2;
		W2.Name = TEXT("OnBeginSequence");
		W2.Target = TEXT("counter1");
		W2.Input = TEXT("Add");
		W2.Param = TEXT("100");
		Other.Outputs.Add(W2);
		Defs.Defs.Add(MoveTemp(Other));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
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

	auto Begin = [](FElysiumEntityWorld& World, FElysiumEntity* Seq)
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
	};

	// --- 4096: the beat borrows the NPC's character collision and gives it back --------------
	// The walk-out's own shape: `0x1260` on a walking beat (NOINTERRUPT | OVERRIDESTATE | priority
	// | ignore-collision), which is what lets five NPCs share one aisle.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x1260, nullptr, /*m_fMoveTo*/ 1);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat_a"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("beat_a resolved"), Seq)
			|| !TestNotNull(TEXT("Damsel stands on a motor"), Motor))
		{
			return false;
		}
		TestFalse(TEXT("collision is ordinary before the beat"), Motor->bIgnoreCharacterCollision);

		double Now = 0.0;
		Begin(World, Seq);
		World.Tick(Now); Now += 0.1;
		TestTrue(TEXT("4096 turns character collision off for the beat"),
			Motor->bIgnoreCharacterCollision);

		// The body reaches the mark; with no action animation the beat then ends, and must hand
		// the borrowed collision back with it.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }
		TestFalse(TEXT("and gives it back when the beat ends"), Motor->bIgnoreCharacterCollision);
	}

	// --- 4096: a cancelled beat gives it back too --------------------------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x360 | 4096, TEXT("Converse_Normal_Talk_A"), /*m_fMoveTo*/ 0);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat_a"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("beat_a resolved"), Seq) || !TestNotNull(TEXT("counter1 resolved"), Count)
			|| !TestNotNull(TEXT("Damsel stands on a motor"), Motor))
		{
			return false;
		}

		double Now = 0.0;
		Begin(World, Seq);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }

		// 256: the post-idle is held, so the beat never completes and its wire never fires.
		TestEqual(TEXT("256 suppresses OnEndSequence"), CounterValue(Count), 0.f);
		TestTrue(TEXT("a held beat keeps the collision it borrowed"),
			Motor->bIgnoreCharacterCollision);

		World.EnqueueInput(TEXT("beat_a"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(),
			0.0, {}, {});
		for (int32 i = 0; i < 3; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("cancelling a held beat still fires no OnEndSequence"),
			CounterValue(Count), 0.f);
		TestFalse(TEXT("cancelling releases the borrowed collision"),
			Motor->bIgnoreCharacterCollision);
	}

	// --- 512: a priority beat cannot be kicked out of the queue ------------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x360, TEXT("Converse_Normal_Talk_A"), /*m_fMoveTo*/ 0);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* First = World.FindByName(TEXT("beat_a"));
		FElysiumEntity* Second = World.FindByName(TEXT("beat_b"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("beat_a resolved"), First) || !TestNotNull(TEXT("beat_b resolved"), Second)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		double Now = 0.0;
		Begin(World, First);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }

		Begin(World, Second);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("512 refuses the challenger outright — not even OnBeginSequence"),
			CounterValue(Count), 0.f);

		// Releasing the claim reopens the queue: the same challenger now gets the NPC.
		World.EnqueueInput(TEXT("beat_a"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(),
			0.0, {}, {});
		for (int32 i = 0; i < 3; ++i) { World.Tick(Now); Now += 0.1; }
		Begin(World, Second);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("and admits it once the claim is released"), CounterValue(Count), 100.f);
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
	AddExpectedError(TEXT("base_NotAStat"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("no entity named '!player' in this map"),
		EAutomationExpectedErrorFlags::Contains, 1);
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

	// v5 adds the eyeball sidecar. It rides on its own fields rather than on the flex rig's,
	// because a player body carries eyeballs and no flex data at all — so `eyes` present with
	// `facial` absent is the shipped state for 57 of the 59 of them, not a malformed row.
	FElysiumNpcIndex V5;
	Error.Reset();
	const FString Json5 = FString::Printf(
		TEXT("{\"manifest_version\":5,\"npcs\":{\"dummy\":{\"glb\":\"dummy.glb\","
			 "\"model\":\"models/dummy.mdl\",\"clips\":1,"
			 "\"eyes\":\"eyes/dummy.json\",\"eyeballs\":2}},\"banks\":{},\"cinematics\":{}}"));
	TestTrue(FString::Printf(TEXT("v5 manifest parses: %s"), *Error), V5.LoadJsonText(Json5, Error));
	TestEqual(TEXT("v5 version retained"), V5.ManifestVersion, 5);
	TestEqual(TEXT("v5 eyeball sidecar path is retained"),
		V5.Npcs[TEXT("dummy")].Eyes, FString(TEXT("eyes/dummy.json")));
	TestEqual(TEXT("v5 eyeball count is retained"), V5.Npcs[TEXT("dummy")].EyeballCount, 2);
	TestTrue(TEXT("eyeballs do not imply a flex rig"), V5.Npcs[TEXT("dummy")].Facial.IsEmpty());

	// An older index simply carries no eye fields, which is a model with no eyeballs rather
	// than a parse failure.
	TestTrue(TEXT("v3 index reads no eyeball sidecar"), V3.Npcs[TEXT("dummy")].Eyes.IsEmpty());

	// v6 turns `clips` from a sorted name array into the model's own sequence table, in declaration
	// order, with the selection keys beside each label. Order is what retail's index-0 rest-pose
	// fallback selects on, so the alphabetically-first clip must NOT win: this table is
	// `drknobantique`'s real shape, where sorting would stand the knob on `handle_locked`.
	FElysiumNpcIndex V6;
	Error.Reset();
	const FString Json6 = FString::Printf(TEXT("{\"manifest_version\":6,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"drknobantique\":{\"glb\":\"animated_props/drknobantique.glb\","
		"\"model\":\"models/scenery/doorknoba/drknobantique.mdl\",\"bones\":2,\"clips\":["
		"{\"name\":\"idle\",\"index\":0,\"activity\":\"\",\"weight\":0,\"flags\":1,\"frames\":16,\"fps\":15.0},"
		"{\"name\":\"handle_locked\",\"index\":1,\"activity\":\"\",\"weight\":0,\"flags\":0,\"frames\":16,\"fps\":15.0},"
		"{\"name\":\"handle_unlocked\",\"index\":2,\"activity\":\"\",\"weight\":0,\"flags\":0,\"frames\":16,\"fps\":15.0}"
		"]}}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("v6 manifest parses: %s"), *Error), V6.LoadJsonText(Json6, Error));
	TestEqual(TEXT("v6 version retained"), V6.ManifestVersion, 6);
	const FElysiumAnimatedPropEntry* Knob =
		V6.FindAnimatedProp(TEXT("models/scenery/doorknoba/drknobantique.mdl"));
	if (TestNotNull(TEXT("v6 resolves the animated prop"), Knob))
	{
		TestEqual(TEXT("v6 keeps declaration order"), Knob->Clips[0].Name, FString(TEXT("idle")));
		TestEqual(TEXT("v6 rest sequence is the declared first, not the alphabetical first"),
			Knob->RestSequence(), FString(TEXT("idle")));
		const FElysiumPropClip* Idle = Knob->FindClip(TEXT("IDLE"));
		if (TestNotNull(TEXT("v6 clip lookup folds case"), Idle))
		{
			TestTrue(TEXT("STUDIO_LOOPING is read off bit 0"), Idle->IsLooping());
			TestEqual(TEXT("v6 carries the declared ordinal"), Idle->Index, 0);
		}
		const FElysiumPropClip* Locked = Knob->FindClip(TEXT("handle_locked"));
		if (TestNotNull(TEXT("v6 resolves a later clip"), Locked))
		{
			TestFalse(TEXT("a non-looping clip reads as one shot"), Locked->IsLooping());
		}
	}

	// `bounds_radius_m` is additive and optional. Its presence states the reach the clip needs
	// about the model origin, in the metres the glb is written in; its absence is "no claim", not
	// "zero reach". These are `cin_sheriff_sword`'s real numbers — a 2 m mesh whose scene clip
	// draws it up to 22 m from the anchor it is culled on.
	FElysiumNpcIndex Reach;
	Error.Reset();
	const FString JsonReach = FString::Printf(TEXT("{\"manifest_version\":6,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"cin_sheriff_sword\":{\"glb\":\"animated_props/cin_sheriff_sword.glb\","
		"\"model\":\"models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl\",\"bones\":15,\"clips\":["
		"{\"name\":\"idle01\",\"index\":0,\"frames\":4701,\"fps\":10.0,\"bounds_radius_m\":22.388},"
		"{\"name\":\"scene\",\"index\":1,\"frames\":4701,\"fps\":30.0}"
		"]}}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("an index carrying a clip reach parses: %s"), *Error),
		Reach.LoadJsonText(JsonReach, Error));
	const FElysiumAnimatedPropEntry* Sword =
		Reach.FindAnimatedProp(TEXT("models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl"));
	if (TestNotNull(TEXT("the sword resolves"), Sword))
	{
		TestEqual(TEXT("a declared clip reach is read in the glb's own metres"),
			Sword->Clips[0].BoundsRadiusMeters, 22.388f);
		TestEqual(TEXT("a clip that declares no reach makes no claim"),
			Sword->Clips[1].BoundsRadiusMeters, 0.f);
	}

	// The v4 shape above still parses, and its bare names land as ordered rows with no selection
	// keys — an index that predates the re-export keeps working, it just has no loop flags.
	if (Glass)
	{
		TestEqual(TEXT("v4 names land in array order"), Glass->Clips[0].Name, FString(TEXT("Idle")));
		TestEqual(TEXT("v4 rest sequence falls back to the first name"),
			Glass->RestSequence(), FString(TEXT("Idle")));
		TestEqual(TEXT("a v4 row declares no clip reach"), Glass->Clips[0].BoundsRadiusMeters, 0.f);
	}

	FElysiumNpcIndex Future;
	Error.Reset();
	const FString Json7 = FString::Printf(TEXT("{\"manifest_version\":7,%s}"), *MinimalNpc);
	TestFalse(TEXT("future manifest is rejected"), Future.LoadJsonText(Json7, Error));
	TestTrue(TEXT("future rejection explains supported versions"), Error.Contains(TEXT("expected 3 to 6")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropBoundsTest,
	"Elysium.Substrate.PropBounds", GElysiumTestFlags)
bool FElysiumPropBoundsTest::RunTest(const FString&)
{
	// `cin_sheriff_sword`'s bind pose in the centimetres the loaded mesh is in: a sword about a
	// metre off the model origin, nowhere near the 22 m its scene clip draws it at.
	const FBoxSphereBounds Bind(FVector(-65.0, 145.0, 75.0), FVector(35.0, 35.0, 115.0), 125.0);
	const double Reach = 2238.8;
	FVector Positive, Negative;

	TestTrue(TEXT("a clip reaching past the bind pose widens it"),
		ElysiumPropBounds::ExtensionFor(Bind, Reach, Positive, Negative));

	// Assert the property, not the arithmetic: compose the box the way CalculateExtendedBounds
	// does and require it to contain the whole +/-Reach cube about the MODEL origin, which is
	// what a clip's reach is measured from and is not where the bind pose is centred.
	const FVector Min = Bind.Origin - Bind.BoxExtent - Negative;
	const FVector Max = Bind.Origin + Bind.BoxExtent + Positive;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TestTrue(FString::Printf(TEXT("the widened box reaches -%.1f on axis %d"), Reach, Axis),
			Min[Axis] <= -Reach + UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("the widened box reaches +%.1f on axis %d"), Reach, Axis),
			Max[Axis] >= Reach - UE_KINDA_SMALL_NUMBER);
		// And lands exactly there unless the bind pose had already passed it. A side that
		// overshoots is a sign error on the centre offset, which containment alone would not see.
		const double BindMin = Bind.Origin[Axis] - Bind.BoxExtent[Axis];
		const double BindMax = Bind.Origin[Axis] + Bind.BoxExtent[Axis];
		TestTrue(FString::Printf(TEXT("axis %d does not overshoot below"), Axis),
			FMath::IsNearlyEqual(Min[Axis], -Reach) || BindMin <= -Reach);
		TestTrue(FString::Printf(TEXT("axis %d does not overshoot above"), Axis),
			FMath::IsNearlyEqual(Max[Axis], Reach) || BindMax >= Reach);
	}

	// An index that states no reach must leave the mesh exactly as glTFRuntime built it — that is
	// how a pre-re-export corpus keeps today's behaviour instead of gaining a zero-sized bound.
	TestFalse(TEXT("no declared reach writes no extension"),
		ElysiumPropBounds::ExtensionFor(Bind, 0.0, Positive, Negative));
	TestEqual(TEXT("the positive extension is left at zero"), Positive, FVector::ZeroVector);
	TestEqual(TEXT("the negative extension is left at zero"), Negative, FVector::ZeroVector);

	// A prop whose bind pose already contains its clip — every ordinary skeletal prop — is not
	// widened either, so the common case pays nothing and keeps its own tight bounds.
	const FBoxSphereBounds Roomy(FVector::ZeroVector, FVector(500.0), 900.0);
	TestFalse(TEXT("a bind pose that already covers the reach is left alone"),
		ElysiumPropBounds::ExtensionFor(Roomy, 400.0, Positive, Negative));
	TestEqual(TEXT("a covered prop takes no positive extension"), Positive, FVector::ZeroVector);
	TestEqual(TEXT("a covered prop takes no negative extension"), Negative, FVector::ZeroVector);
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
	Services.AnimatedPropClipLoops.Add(TEXT("cin_wineglass|glass_idle"), true);

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
	// `LoopSequence` resolves in the Activate phase and starts behind a 0.1-0.99 s stagger, so at
	// spawn the prop is holding its rest pose and the loop has not begun. Elysium.Substrate.
	// PropAnimateThink is the dedicated coverage; this only pins that the seam still reaches here.
	TestFalse(TEXT("the authored loop waits for the Activate-phase stagger"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_idle loop=1")));
	World.Tick(1.0);
	TestTrue(TEXT("authored loop starts looping once the stagger elapses"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_idle loop=1")));
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("SetAnimation")),
		FElysiumVariant::String(TEXT("glass_pour")), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("Skin")), FElysiumVariant::Int(3), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(1.0);   // the clock has already reached 1.0 above; it must not run backwards
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
	World.Tick(1.0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSleepTest,
	"Elysium.Substrate.NpcMotorSleep", GElysiumTestFlags)
bool FElysiumNpcMotorSleepTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumNpcBody* Body = World ? World->SpawnActor<AElysiumNpcBody>() : nullptr;
	if (!TestNotNull(TEXT("native NPC motor spawned"), Body))
	{
		return false;
	}

	Body->InitializeAtFeet(FVector::ZeroVector, 0.0f);
	UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	if (!TestNotNull(TEXT("native NPC motor owns CharacterMovement"), Movement))
	{
		return false;
	}
	TestFalse(TEXT("NPC capsule never contributes navigation geometry"),
		Body->GetCapsuleComponent()->CanEverAffectNavigation());
	TestFalse(TEXT("NPC movement never contributes navigation geometry"),
		Movement->CanEverAffectNavigation());
	TestNull(TEXT("an idle NPC does not create a crowd controller before Recast is ready"),
		Body->GetController());
	TestFalse(TEXT("movement stays inactive before the runtime barrier"), Movement->IsActive());

	Body->SetRuntimeReady(true);
	TestTrue(TEXT("the enabled idle body keeps physical collision"), Body->GetActorEnableCollision());
	TestFalse(TEXT("crossing the runtime barrier does not tick an idle motor"), Movement->IsActive());
	TestNull(TEXT("crossing the runtime barrier still does not create an idle controller"),
		Body->GetController());

	Movement->Activate();
	TestTrue(TEXT("the test can model an outstanding movement request"), Movement->IsActive());
	Body->Stop();
	TestFalse(TEXT("Stop deactivates CharacterMovement"), Movement->IsActive());
	TestFalse(TEXT("Stop disables the movement component tick"), Movement->IsComponentTickEnabled());

	Body->SetEnabled(false);
	TestFalse(TEXT("a disabled NPC drops physical collision"), Body->GetActorEnableCollision());
	Body->SetEnabled(true);
	TestTrue(TEXT("re-enabling restores collision"), Body->GetActorEnableCollision());
	TestFalse(TEXT("re-enabling an idle NPC does not wake CharacterMovement"), Movement->IsActive());
	return true;
}

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
	TestFalse(TEXT("the moving faithful player hull never reshapes Recast"),
		Pawn->GetRootComponent()->CanEverAffectNavigation());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUseTargetingEmbodimentTest,
	"Elysium.Substrate.UseTargetingEmbodiment", GElysiumTestFlags)
bool FElysiumUseTargetingEmbodimentTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	APlayerController* PC = World ? World->SpawnActor<APlayerController>() : nullptr;
	AElysiumPawn* Pawn = World ? World->SpawnActor<AElysiumPawn>(
		FVector(0, 0, ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("targeting controller"), PC)
		|| !TestNotNull(TEXT("targeting player body"), Pawn))
	{
		return false;
	}
	PC->Possess(Pawn);
	PC->SetControlRotation(FRotator::ZeroRotator);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}

	AElysiumMapActor* Map = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("targeting map embodiment"), Map))
	{
		return false;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	FVector BodyOrigin;
	if (!TestTrue(TEXT("final player camera POV resolves"),
		Map->GetPlayerViewPoint(CameraLocation, CameraRotation))
		|| !TestTrue(TEXT("player body use origin resolves"), Map->GetPlayerUseOrigin(BodyOrigin)))
	{
		return false;
	}

	auto AddTarget = [Map](const TCHAR* Name, const FVector& Location,
		const FVector& Extent, const FElysiumEntityHandle& Handle)
	{
		UBoxComponent* Source = NewObject<UBoxComponent>(Map, FName(Name));
		Source->InitBoxExtent(Extent);
		Source->SetupAttachment(Map->GetRootComponent());
		Source->SetWorldLocation(Location);
		Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Source->RegisterComponent();
		Map->AddInstanceComponent(Source);
		Map->RegisterUseAnchor(Source, Handle);
		return Source;
	};

	const FVector Aim = CameraRotation.Vector().GetSafeNormal();
	const FVector Side = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y).GetSafeNormal();
	const FElysiumEntityHandle ExactHandle(10, 1);
	AddTarget(TEXT("ExactSource"), CameraLocation + Aim * 150.0f,
		FVector(3.0f), ExactHandle);
	FElysiumUseQueryResult Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("exact ray produces one target"), Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("exact ray selects its logical entity"), Query.Candidates[0].Owner, ExactHandle);
		TestEqual(TEXT("exact ray is classified exact"),
			Query.Candidates[0].Selection, EElysiumUseSelection::Exact);
		TestTrue(TEXT("query reports body distance"), Query.Candidates[0].BodyDistance > 0.0f);
		TestTrue(TEXT("query reports camera distance"), Query.Candidates[0].CameraDistance > 0.0f);
	}

	Map->SetUseAnchorEnabled(ExactHandle, false);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("disabled anchor is removed from exact targeting"), Query.Candidates.IsEmpty());

	const FElysiumEntityHandle AssistedHandle(11, 1);
	AddTarget(TEXT("AssistedSource"), CameraLocation + Aim * 150.0f + Side * 10.0f,
		FVector(2.0f), AssistedHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("outside the base cone is not assisted"), Query.Candidates.IsEmpty());
	Query = Map->QueryPlayerUse(AssistedHandle);
	TestEqual(TEXT("current focus receives the 25 percent assistance hysteresis"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("near miss is classified assisted"),
			Query.Candidates[0].Selection, EElysiumUseSelection::Assisted);
	}

	Map->SetUseAnchorEnabled(AssistedHandle, false);
	const FElysiumEntityHandle NearHandle(12, 1);
	AddTarget(TEXT("NearAssistSource"), CameraLocation + Aim * 150.0f + Side * 5.0f,
		FVector(2.0f), NearHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("small near miss receives restrained assistance"), Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("assistance returns the intended small prop"), Query.Candidates[0].Owner, NearHandle);
	}

	Map->SetUseAnchorEnabled(NearHandle, false);
	const FElysiumEntityHandle LeftHandle(20, 1);
	const FElysiumEntityHandle RightHandle(21, 1);
	const FVector LeftPoint = CameraLocation + Aim * 160.0f - Side * 7.0f;
	const FVector RightPoint = CameraLocation + Aim * 160.0f;
	AddTarget(TEXT("LeftButtonSource"), LeftPoint, FVector(3.0f), LeftHandle);
	AddTarget(TEXT("RightButtonSource"), RightPoint, FVector(3.0f), RightHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("exact aim among adjacent buttons returns one control"), Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("exact aim picks the intended adjacent button"),
			Query.Candidates[0].Owner, RightHandle);
	}

	Map->SetUseAnchorEnabled(LeftHandle, false);
	Map->SetUseAnchorEnabled(RightHandle, false);
	PC->SetControlRotation(FRotator::ZeroRotator);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
	Map->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FElysiumEntityHandle FarHandle(30, 1);
	AddTarget(TEXT("FarSource"), CameraLocation + CameraRotation.Vector() * 240.0f,
		FVector(2.0f), FarHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("camera hit beyond body reach is rejected"), Query.Candidates.IsEmpty());
	TestEqual(TEXT("body reach rejection is reported"), Query.MissOutcome, EElysiumUseOutcome::OutOfRange);

	Map->SetUseAnchorEnabled(FarHandle, false);
	const FElysiumEntityHandle OccludedHandle(31, 1);
	const FVector OccludedPoint = CameraLocation + CameraRotation.Vector() * 150.0f;
	AddTarget(TEXT("OccludedSource"), OccludedPoint, FVector(3.0f), OccludedHandle);
	AActor* WallOwner = World->SpawnActor<AActor>();
	UBoxComponent* Wall = NewObject<UBoxComponent>(WallOwner, TEXT("UseWall"));
	WallOwner->SetRootComponent(Wall);
	Wall->InitBoxExtent(FVector(4.0f, 50.0f, 50.0f));
	Wall->SetWorldLocation(CameraLocation + CameraRotation.Vector() * 75.0f);
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionObjectType(ECC_WorldStatic);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	Wall->RegisterComponent();
	WallOwner->AddInstanceComponent(Wall);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("wall occlusion rejects the target"), Query.Candidates.IsEmpty());
	TestEqual(TEXT("wall occlusion is reported"), Query.MissOutcome, EElysiumUseOutcome::Occluded);
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Offset the final POV like a third-person camera while leaving body reach at the pawn pivot.
	ACameraActor* OffsetCamera = World->SpawnActor<ACameraActor>();
	const FVector OffsetLocation = BodyOrigin + FVector(-180.0f, 80.0f, 40.0f);
	OffsetCamera->SetActorLocationAndRotation(OffsetLocation,
		(OccludedPoint - OffsetLocation).Rotation());
	PC->SetViewTarget(OffsetCamera);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("offset third-person POV still selects through body validation"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("offset POV keeps the logical target"), Query.Candidates[0].Owner, OccludedHandle);
	}

	Map->ClearUseAnchors();
	Map->Destroy();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTeardownTest,
	"Elysium.Substrate.MapActorTeardown", GElysiumTestFlags)
bool FElysiumMapActorTeardownTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumMapActor* Map = World ? World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity) : nullptr;
	if (!TestNotNull(TEXT("stage map actor"), Map))
	{
		return false;
	}
	Map->bStageOnly = true;
	Map->MapName.Reset();
	Map->FinishSpawning(FTransform::Identity);

	FElysiumEntityWorld* EntityWorld = Map->GetEntityWorld();
	if (!TestNotNull(TEXT("stage map owns an entity world before teardown"), EntityWorld))
	{
		return false;
	}

	// A non-brush source creates the owned query proxy that exposed the late-destruction crash.
	UBoxComponent* Source = NewObject<UBoxComponent>(Map, TEXT("TeardownUseSource"));
	Source->SetupAttachment(Map->GetRootComponent());
	Source->RegisterComponent();
	Map->AddInstanceComponent(Source);
	Map->RegisterUseAnchor(Source, EntityWorld->PlayerHandle());

	if (!TestWorld.EndPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	TestNull(TEXT("EndPlay destroys the substrate before UObject reclamation"), Map->GetEntityWorld());
	return !HasAnyErrors();
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

	{
		FVector Offset(1120.756f, 325.981f, -89.643f);
		bool bHasYaw = true;
		TestTrue(TEXT("the authored theatre exit selects direct tutorial placement"),
			ElysiumStory::ResolveTheatreExitPlacement(ElysiumStory::TheatreMap,
				ElysiumStory::TutorialMap, ElysiumStory::TutorialLandmark, Offset, bHasYaw));
		TestTrue(TEXT("the theatre cinematic displacement is dropped"), Offset.IsNearlyZero());
		TestFalse(TEXT("the tutorial landmark supplies the arrival facing"), bHasYaw);
	}

	{
		FVector Offset(7.f, 8.f, 9.f);
		bool bHasYaw = true;
		TestFalse(TEXT("ordinary landmark travel keeps its relative placement"),
			ElysiumStory::ResolveTheatreExitPlacement(TEXT("sp_tutorial_1"),
				TEXT("sm_pawnshop_1"), TEXT("newgame"), Offset, bHasYaw));
		TestTrue(TEXT("ordinary placement is untouched"),
			Offset.Equals(FVector(7.f, 8.f, 9.f)) && bHasYaw);
	}

	return true;
}

// =====================================================================================
// FElysiumWorldServices (11.2) — the substrate's outbound seam. Runs the shape of the
// tutorial's own logic_auto chain end to end against the recording stub: no RHI, no actors,
// no `$ELYSIUM_EXPORT_ROOT`. sp_tutorial_1's five logic_autos fire OnMapLoad at an NPC (WillTalk), a
// door (Lock), a math_counter and a delayed wire; this reproduces that shape and adds one
// entity per entity-addressed service; all five seams are exercised by the same world activation.
//
// The second half is the contract that makes the first half meaningful: the SAME defs on a
// world with NO services must reach the SAME logical state. Embodiment, audio, travel,
// presentation, and weather are outputs of the logic, never inputs to it.
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

	// --- With services: the chain reaches all five seams ---------------------------------
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

		// Player interaction with no embodiment: no query result means no focus, and a queued press
		// is a safe no-op rather than a null deref.
		Bare.UpdatePlayerInteraction();
		TestFalse(TEXT("no embodiment means no use cursor"), Bare.GetAimedUsable().IsSet());
		Bare.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		Bare.UpdatePlayerInteraction();
	}

	return true;
}

// =====================================================================================
// Modern +use — deterministic selection order and the world-owned focus/session lifecycle.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInteractionLifecycleTest,
	"Elysium.Substrate.InteractionLifecycle", GElysiumTestFlags)
bool FElysiumInteractionLifecycleTest::RunTest(const FString&)
{
	// Candidate scoring is total and stable. Exact beats assistance; hysteresis affects cone
	// admission only, and assisted candidates still resolve by aim/depth/handle.
	TArray<FElysiumUseCandidate> Scored;
	auto Candidate = [](int32 Index, EElysiumUseSelection Selection, float Aim, float Depth,
		bool bHysteresis = false)
	{
		FElysiumUseCandidate C;
		C.Owner = FElysiumEntityHandle(Index, 7);
		C.Selection = Selection;
		C.AimError = Aim;
		C.CameraDepth = Depth;
		C.bHysteresis = bHysteresis;
		return C;
	};
	Scored.Add(Candidate(3, EElysiumUseSelection::Assisted, 0.01f, 10.0f, true));
	Scored.Add(Candidate(2, EElysiumUseSelection::Exact, 1.0f, 100.0f));
	Scored.Add(Candidate(1, EElysiumUseSelection::Assisted, 0.0f, 1.0f));
	ElysiumInteraction::SortCandidates(Scored);
	TestEqual(TEXT("an exact hit always wins"), Scored[0].Owner.Index, 2);
	TestEqual(TEXT("assisted angular error wins after cone admission"), Scored[1].Owner.Index, 1);
	Scored.Reset();
	Scored.Add(Candidate(9, EElysiumUseSelection::Assisted, 0.2f, 30.0f));
	Scored.Add(Candidate(8, EElysiumUseSelection::Assisted, 0.1f, 40.0f));
	Scored.Add(Candidate(7, EElysiumUseSelection::Assisted, 0.1f, 20.0f));
	Scored.Add(Candidate(6, EElysiumUseSelection::Assisted, 0.1f, 20.0f));
	ElysiumInteraction::SortCandidates(Scored);
	TestEqual(TEXT("ties finish on the stable entity handle"), Scored[0].Owner.Index, 6);

	auto SessionDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__interaction__");
		for (const TCHAR* Name : { TEXT("hold"), TEXT("explicit") })
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("test_use_session");
			Def.TargetName = Name;
			Def.Keys.Add(TEXT("use_icon"), Name == FString(TEXT("hold")) ? TEXT("5") : TEXT("9"));
			Defs.Defs.Add(MoveTemp(Def));
		}
		return Defs;
	};

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(SessionDefs());
	World.Activate(0.0);
	auto* Hold = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("hold")));
	auto* Explicit = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("explicit")));
	if (!TestNotNull(TEXT("while-held test entity"), Hold)
		|| !TestNotNull(TEXT("explicit test entity"), Explicit))
	{
		return false;
	}

	FElysiumUseCandidate HoldHit = Candidate(
		Hold->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 50.0f);
	HoldHit.Owner = Hold->Handle;
	HoldHit.AnchorPoint = FVector(50, 0, 0);
	Services.UseQuery.Candidates = { HoldHit };
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("focus enters once"), Hold->EnterCount, 1);
	TestEqual(TEXT("the exact entity becomes focus"), World.GetFocusedUsable(), Hold->Handle);
	TestFalse(TEXT("the prompt starts its fade at zero"), World.GetInteractionView().bVisible);
	World.Tick(0.05);
	const FElysiumInteractionView HalfFade = World.GetInteractionView();
	TestTrue(TEXT("the prompt is visible during fade-in"), HalfFade.bVisible);
	TestTrue(TEXT("the 0.10 second fade is halfway at 0.05"),
		FMath::IsNearlyEqual(HalfFade.PromptAlpha, 0.5f, 0.02f));
	TestTrue(TEXT("focused prompt is actionable before capture"), HalfFade.bActionable);

	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("one press begins one interaction"), Hold->BeginCount, 1);
	TestEqual(TEXT("captured while-held session reports started"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::SessionStarted);
	TestFalse(TEXT("a captured session is not actionable a second time"),
		World.GetInteractionView().bActionable);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("held frames never repeat Begin"), Hold->BeginCount, 1);

	FElysiumUseCandidate ExplicitHit = Candidate(
		Explicit->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 45.0f);
	ExplicitHit.Owner = Explicit->Handle;
	Services.UseQuery.Candidates = { ExplicitHit };
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("looking away fires leave"), Hold->LeaveCount, 1);
	TestEqual(TEXT("new target receives focus while old session remains captured"),
		Explicit->EnterCount, 1);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("another press while captured is busy"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::Busy);
	TestEqual(TEXT("busy press never begins the new target"), Explicit->BeginCount, 0);

	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("release routes to the captured while-held owner"), Hold->EndCount, 1);
	TestEqual(TEXT("release carries its reason"), Hold->LastEndReason, EElysiumUseEndReason::Released);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("explicit session begins after capture clears"), Explicit->BeginCount, 1);
	TestFalse(TEXT("an explicit modal session suppresses the world prompt"),
		World.GetInteractionView().bVisible);

	Services.UseQuery = FElysiumUseQueryResult();
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("looking away clears focus"), World.GetFocusedUsable().IsSet());
	TestEqual(TEXT("looking away does not end an explicit session"), Explicit->EndCount, 0);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("release does not end an explicit session"), Explicit->EndCount, 0);
	TestTrue(TEXT("the explicit leaf/UI can complete its captured session"),
		World.EndPlayerUseSession(Explicit->Handle, EElysiumUseEndReason::Completed));
	TestEqual(TEXT("explicit completion reaches the captured owner"), Explicit->EndCount, 1);
	TestEqual(TEXT("explicit completion carries its reason"),
		Explicit->LastEndReason, EElysiumUseEndReason::Completed);
	TestEqual(TEXT("explicit completion reports completed"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::Completed);

	Services.UseQuery.Candidates = { ExplicitHit };
	World.UpdatePlayerInteraction();
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("the explicit owner can start another session"), Explicit->BeginCount, 2);
	World.AcceptInput(TEXT("explicit"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		Explicit->Handle, Explicit->Handle);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("an inert captured owner is cancelled"), Explicit->EndCount, 2);
	TestEqual(TEXT("target invalidation carries its reason"),
		Explicit->LastEndReason, EElysiumUseEndReason::TargetInvalid);
	World.Tick(0.125);
	TestTrue(TEXT("the 0.15 second fade-out is halfway after 0.075 seconds"),
		FMath::IsNearlyEqual(World.GetInteractionView().PromptAlpha, 0.25f, 0.02f));
	World.Tick(0.20);
	TestFalse(TEXT("the retained prompt is gone after its fade-out"),
		World.GetInteractionView().bVisible);

	const FElysiumEntityHandle Stale = Hold->Handle;
	World.AcceptInput(TEXT("hold"), FName(TEXT("Kill")), FElysiumVariant::Void(),
		Hold->Handle, Hold->Handle);
	FElysiumUseCandidate StaleHit = Candidate(
		Stale.Index, EElysiumUseSelection::Exact, 0.0f, 20.0f);
	StaleHit.Owner = Stale;
	Services.UseQuery.Candidates = { StaleHit };
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("a stale handle cannot become focus"), World.GetFocusedUsable().IsSet());

	const int32 TeardownsBefore = FElysiumTestUseSessionEntity::TeardownEndCount;
	{
		FElysiumRecordingServices TeardownServices;
		FElysiumEntityWorld TeardownWorld(nullptr, nullptr, TeardownServices.Bundle());
		TeardownWorld.Load(SessionDefs());
		TeardownWorld.Activate(0.0);
		FElysiumEntity* TeardownHold = TeardownWorld.FindByName(TEXT("hold"));
		FElysiumUseCandidate Hit = Candidate(
			TeardownHold->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 10.0f);
		Hit.Owner = TeardownHold->Handle;
		TeardownServices.UseQuery.Candidates = { Hit };
		TeardownWorld.UpdatePlayerInteraction();
		TeardownWorld.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		TeardownWorld.UpdatePlayerInteraction();
	}
	TestEqual(TEXT("map teardown cancels a captured session"),
		FElysiumTestUseSessionEntity::TeardownEndCount, TeardownsBefore + 1);

	return !HasAnyErrors();
}

// =====================================================================================
// Brush movers — visible body attachment, PASSABLE doors, use_override, prop_button,
// and the recovered func_elevator state machine exercised as one authored-style chain.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorElevatorTest,
	"Elysium.Substrate.DoorElevator", GElysiumTestFlags)
bool FElysiumDoorElevatorTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("does_not_exist.Use"), EAutomationExpectedErrorFlags::Contains, 1);
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
	TestTrue(TEXT("model-backed prop_button registers an enabled use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
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
	FElysiumUseCandidate LockedHit;
	LockedHit.Owner = LiveButton->Handle;
	LockedHit.Selection = EElysiumUseSelection::Exact;
	LockedHit.CameraDepth = 40.0f;
	Services.UseQuery.Candidates = { LockedHit };
	World.UpdatePlayerInteraction();
	const FElysiumInteractionView LockedView = World.GetInteractionView();
	TestEqual(TEXT("locked focus publishes its locked icon"), LockedView.Icon, 8);
	TestTrue(TEXT("locked focus publishes locked state"), LockedView.bLocked);
	TestTrue(TEXT("a locked control remains actionable so it can emit its denial"),
		LockedView.bActionable);

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

	World.AcceptInput(TEXT("elev_button"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	TestFalse(TEXT("hidden prop_button removes its use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("a stale exact query cannot focus a hidden button"),
		World.GetFocusedUsable().IsSet());
	TestFalse(TEXT("hidden button has no actionable prompt"),
		World.GetInteractionView().bActionable);
	World.AcceptInput(TEXT("elev_button"), FName(TEXT("ScriptUnhide")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	TestTrue(TEXT("ScriptUnhide restores the prop_button use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("ScriptUnhide makes the button focusable again"),
		World.GetFocusedUsable(), LiveButton->Handle);

	return true;
}

// =====================================================================================
// func_rotating + the character attach point.
//
// Two things a `parentname` needs that the substrate did not have: a character that can BE a
// parent (an ornament worn on an NPC, which survives the model swap a level script does), and the
// continuous spinner its own children ride. The rate is the assertion that matters — VtMB authors
// clock hands as `maxspeed` in degrees/second, so a second hand is 6 and a minute hand is 0.1.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingAttachTest,
	"Elysium.Substrate.RotatingAttach", GElysiumTestFlags)
bool FElysiumRotatingAttachTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("cannot attach to parent 'no_body'"),
		EAutomationExpectedErrorFlags::Contains, 2);
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("rotating owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("RotatingRoot"));
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
	auto DebugRow = [](const FElysiumEntity* Entity, const TCHAR* Key)
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		const TPair<FString, FString>* Row = State.FindByPredicate(
			[Key](const TPair<FString, FString>& R) { return R.Key == Key; });
		return Row ? Row->Value : FString();
	};
	auto AngleOf = [&DebugRow](const FElysiumEntity* Entity)
	{
		return FCString::Atof(*DebugRow(Entity, TEXT("Angle")));
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__rotating_attach__");

	// The character an ornament hangs off — the sp_theatre shape (a level script SetModels these).
	FElysiumEntityDef Understudy;
	Understudy.Classname = TEXT("npc_VPedestrian");
	Understudy.TargetName = TEXT("understudy");
	Understudy.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	Defs.Defs.Add(MoveTemp(Understudy));

	FElysiumEntityDef Ornament;
	Ornament.Classname = TEXT("prop_dynamic_ornament");
	Ornament.TargetName = TEXT("worn_sign");
	Ornament.ModelMesh = TEXT("prophet_sign");
	Ornament.Keys.Add(TEXT("model"), TEXT("models/props/prophet_sign.mdl"));
	Ornament.Keys.Add(TEXT("parentname"), TEXT("understudy"));
	Defs.Defs.Add(MoveTemp(Ornament));

	// A bodiless character cannot be a parent — the honest "nothing to attach to" path.
	FElysiumEntityDef Bodiless;
	Bodiless.Classname = TEXT("npc_VPedestrian");
	Bodiless.TargetName = TEXT("no_body");
	Defs.Defs.Add(MoveTemp(Bodiless));

	FElysiumEntityDef Orphan;
	Orphan.Classname = TEXT("prop_dynamic_ornament");
	Orphan.TargetName = TEXT("orphan_sign");
	Orphan.ModelMesh = TEXT("prophet_sign");
	Orphan.Keys.Add(TEXT("model"), TEXT("models/props/prophet_sign.mdl"));
	Orphan.Keys.Add(TEXT("parentname"), TEXT("no_body"));
	Defs.Defs.Add(MoveTemp(Orphan));

	// sm_bailbonds_1's second hand: maxspeed 6 deg/s = one revolution per minute.
	// spawnflags 69 = START_ON | Z_AXIS | NOT_SOLID, exactly as exported.
	FElysiumEntityDef SecondHand;
	SecondHand.Classname = TEXT("func_rotating");
	SecondHand.TargetName = TEXT("secondhand");
	SecondHand.Model = 1;
	SecondHand.Hulls.Add(BoxHull());
	SecondHand.BrushMesh = TEXT("brush_1");
	SecondHand.Keys.Add(TEXT("model"), TEXT("*1"));
	SecondHand.Keys.Add(TEXT("maxspeed"), TEXT("6"));
	SecondHand.Keys.Add(TEXT("spawnflags"), TEXT("69"));
	Defs.Defs.Add(MoveTemp(SecondHand));

	FElysiumEntityDef Hand;
	Hand.Classname = TEXT("prop_dynamic");
	Hand.TargetName = TEXT("second");
	Hand.ModelMesh = TEXT("clock_hand");
	Hand.Keys.Add(TEXT("model"), TEXT("models/props/clock_hand.mdl"));
	Hand.Keys.Add(TEXT("parentname"), TEXT("secondhand"));
	Defs.Defs.Add(MoveTemp(Hand));

	// A rotator that is not START_ON, to prove Start/Stop/Reverse/SetSpeed drive it.
	FElysiumEntityDef Idle;
	Idle.Classname = TEXT("func_rotating");
	Idle.TargetName = TEXT("idlerotator");
	Idle.Origin = FVector(200.f, 0.f, 0.f);
	Idle.Model = 2;
	Idle.Hulls.Add(BoxHull());
	Idle.BrushMesh = TEXT("brush_2");
	Idle.Keys.Add(TEXT("model"), TEXT("*2"));
	Idle.Keys.Add(TEXT("maxspeed"), TEXT("10"));
	Idle.Keys.Add(TEXT("spawnflags"), TEXT("4"));    // Z_AXIS, no START_ON
	Defs.Defs.Add(MoveTemp(Idle));

	// sp_tutorial_1's ceiling fan: spawnflags 513 = START_ON | SND_LARGE, so neither axis bit is
	// set and it falls to the default. Every fan and sky rotator in the corpus is authored this
	// way, and all of them must turn about Z — the case the Z_AXIS clock hand above cannot cover.
	FElysiumEntityDef Fan;
	Fan.Classname = TEXT("func_rotating");
	Fan.TargetName = TEXT("fanrot1");
	Fan.Origin = FVector(400.f, 0.f, 0.f);
	Fan.Model = 3;
	Fan.Hulls.Add(BoxHull());
	Fan.BrushMesh = TEXT("brush_3");
	Fan.Keys.Add(TEXT("model"), TEXT("*3"));
	Fan.Keys.Add(TEXT("maxspeed"), TEXT("360"));
	Fan.Keys.Add(TEXT("spawnflags"), TEXT("513"));
	Defs.Defs.Add(MoveTemp(Fan));

	FElysiumEntityDefs RestoreDefs = Defs;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.Tick(0.0);   // the activation frame: a START_ON rotator starts its clock on its first think

	FElysiumEntity* LiveUnderstudy = World.FindByName(TEXT("understudy"));
	FElysiumEntity* LiveOrnament = World.FindByName(TEXT("worn_sign"));
	FElysiumEntity* LiveOrphan = World.FindByName(TEXT("orphan_sign"));
	FElysiumEntity* LiveSecond = World.FindByName(TEXT("secondhand"));
	FElysiumEntity* LiveHand = World.FindByName(TEXT("second"));
	FElysiumEntity* LiveIdle = World.FindByName(TEXT("idlerotator"));
	FElysiumEntity* LiveFan = World.FindByName(TEXT("fanrot1"));
	if (!TestNotNull(TEXT("ceiling fan resolved"), LiveFan)
		|| !TestNotNull(TEXT("character resolved"), LiveUnderstudy)
		|| !TestNotNull(TEXT("ornament resolved"), LiveOrnament)
		|| !TestNotNull(TEXT("orphan ornament resolved"), LiveOrphan)
		|| !TestNotNull(TEXT("func_rotating resolved"), LiveSecond)
		|| !TestNotNull(TEXT("rotator child resolved"), LiveHand)
		|| !TestNotNull(TEXT("idle func_rotating resolved"), LiveIdle))
	{
		return false;
	}

	// --- A character is an attach parent ---------------------------------------------------
	TestTrue(TEXT("a character's attach body is its skeletal body"),
		LiveUnderstudy->GetAttachBody() != nullptr
		&& LiveUnderstudy->GetAttachBody() == LiveUnderstudy->GetSkeletalBody());
	TestTrue(TEXT("parentname attaches an ornament to the character body"),
		LiveOrnament->GetAttachBody()
		&& LiveOrnament->GetAttachBody()->GetAttachParent() == LiveUnderstudy->GetAttachBody());
	TestNull(TEXT("a bodiless character has no attach body"),
		World.FindByName(TEXT("no_body"))->GetAttachBody());
	TestTrue(TEXT("a child of a bodiless character stays unattached"),
		LiveOrphan->GetAttachBody() && LiveOrphan->GetAttachBody()->GetAttachParent() == nullptr);

	// --- A model swap carries the children onto the new body --------------------------------
	const FTransform WornOffset(FRotator(0.f, 30.f, 0.f), FVector(0.f, 0.f, 90.f));
	LiveOrnament->GetAttachBody()->SetRelativeTransform(WornOffset);
	USkeletalMeshComponent* BeforeSwap = LiveUnderstudy->GetSkeletalBody();
	LiveUnderstudy->SetRuntimeModel(TEXT("models/character/npc/common/blueblood/female/Blueblood_Female.mdl"));
	TestTrue(TEXT("SetModel rebuilds the character body"),
		LiveUnderstudy->GetSkeletalBody() && LiveUnderstudy->GetSkeletalBody() != BeforeSwap);
	TestTrue(TEXT("SetModel re-parents the ornament onto the new body"),
		LiveOrnament->GetAttachBody()
		&& LiveOrnament->GetAttachBody()->GetAttachParent() == LiveUnderstudy->GetSkeletalBody());
	TestTrue(TEXT("SetModel preserves the ornament's worn offset"),
		LiveOrnament->GetAttachBody()->GetRelativeTransform().Equals(WornOffset, 0.01f));

	// --- func_rotating turns at maxspeed degrees per second ----------------------------------
	TestTrue(TEXT("parentname attaches the clock hand to the rotator body"),
		LiveHand->GetAttachBody()
		&& LiveHand->GetAttachBody()->GetAttachParent() == LiveSecond->Body);
	TestEqual(TEXT("NOT_SOLID rotator takes the traceable passable profile"),
		LiveSecond->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));

	World.Tick(15.0);
	TestTrue(TEXT("a 6 deg/s rotator has turned 90 degrees at fifteen seconds"),
		FMath::IsNearlyEqual(AngleOf(LiveSecond), 90.f, 0.01f));
	// The Z_AXIS flag selects the roll component of Spawn's m_vecMoveAng, so a clock hand sweeps
	// about X — the wall normal — not about Z. X survives the Y-negating reflection unchanged, and
	// the reflection reverses the turn, so a quarter turn reads as roll -90.
	// The reflected turn is the same one the fan below makes, but it reads back as roll +90 rather
	// than -90: FRotator's Roll and Pitch run opposite to a right-handed turn about X and Y, while
	// Yaw runs with it. The quaternion is FQuat(+X, -90 deg) in both readings.
	TestTrue(TEXT("a Z_AXIS rotator turns about Unreal X with the reflected sign"),
		FMath::IsNearlyEqual(LiveSecond->Body->GetRelativeRotation().Roll, 90.f, 0.1f)
		&& FMath::IsNearlyZero(LiveSecond->Body->GetRelativeRotation().Pitch, 0.1f)
		&& FMath::IsNearlyZero(LiveSecond->Body->GetRelativeRotation().Yaw, 0.1f));
	TestTrue(TEXT("the parented hand rides the rotation"),
		FMath::IsNearlyEqual(LiveHand->GetAttachBody()->GetComponentRotation().Roll, 90.f, 0.1f));

	// The unflagged default is yaw. 360 deg/s for 15.25 s is a quarter turn past the wrap, which
	// reads as yaw -90 with the reflected sign — a ceiling fan sweeping the ceiling, not the wall.
	World.Tick(15.25);
	TestTrue(TEXT("an unflagged rotator turns about Unreal Z with the reflected sign"),
		FMath::IsNearlyEqual(LiveFan->Body->GetRelativeRotation().Yaw, -90.f, 0.1f)
		&& FMath::IsNearlyZero(LiveFan->Body->GetRelativeRotation().Pitch, 0.1f)
		&& FMath::IsNearlyZero(LiveFan->Body->GetRelativeRotation().Roll, 0.1f));

	World.Tick(60.0);
	TestTrue(TEXT("one full revolution takes a minute and wraps"),
		FMath::IsNearlyEqual(AngleOf(LiveSecond), 0.f, 0.01f));

	// --- Stop / Start / Reverse / SetSpeed ---------------------------------------------------
	FElysiumEntityHandle Self = LiveIdle->Handle;
	World.Tick(70.0);
	TestTrue(TEXT("a rotator without START_ON does not turn"), FMath::IsNearlyZero(AngleOf(LiveIdle)));
	// A queued input runs at the start of the tick that drains it, so each one is dispatched at the
	// time it should take effect and the interval is measured from there.
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Start")), FElysiumVariant::Void(), Self, Self);
	World.Tick(70.0);
	World.Tick(73.0);
	TestTrue(TEXT("Start turns it at maxspeed"), FMath::IsNearlyEqual(AngleOf(LiveIdle), 30.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Stop")), FElysiumVariant::Void(), Self, Self);
	World.Tick(73.0);
	World.Tick(80.0);
	TestTrue(TEXT("Stop freezes the angle where it was"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 30.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Reverse")), FElysiumVariant::Void(), Self, Self);
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Start")), FElysiumVariant::Void(), Self, Self);
	World.Tick(80.0);
	World.Tick(82.0);
	TestTrue(TEXT("Reverse turns the other way from where it stopped"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 10.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("SetSpeed")), FElysiumVariant::Float(1.0f), Self, Self);
	World.Tick(82.0);
	World.Tick(92.0);
	TestTrue(TEXT("SetSpeed keeps turning the same way from the angle it had"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 0.f, 0.01f));

	// --- A save resumes mid-spin -------------------------------------------------------------
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);

	AActor* RestoreOwner = EngineWorld->SpawnActor<AActor>();
	USceneComponent* RestoreRoot = NewObject<USceneComponent>(RestoreOwner, TEXT("RotatingRestoreRoot"));
	RestoreOwner->SetRootComponent(RestoreRoot);
	RestoreRoot->RegisterComponent();
	RestoreOwner->AddInstanceComponent(RestoreRoot);
	FElysiumRecordingServices RestoreServices;
	FElysiumEntityWorld Restored(RestoreOwner, nullptr, RestoreServices.Bundle());
	Restored.Load(MoveTemp(RestoreDefs));
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(92.0);
	Restored.Tick(92.0);
	FElysiumEntity* RestoredSecond = Restored.FindByName(TEXT("secondhand"));
	if (TestNotNull(TEXT("restored rotator resolved"), RestoredSecond))
	{
		// 6 deg/s for 92 s is 552 degrees — one revolution and 192 more.
		TestTrue(TEXT("a restored rotator resumes at the saved angle"),
			FMath::IsNearlyEqual(AngleOf(RestoredSecond), 192.f, 0.01f)
			&& FMath::IsNearlyEqual(AngleOf(RestoredSecond), AngleOf(LiveSecond), 0.01f));
		Restored.Tick(107.0);
		TestTrue(TEXT("and keeps turning at its speed from there"),
			FMath::IsNearlyEqual(AngleOf(RestoredSecond), 282.f, 0.01f));
	}

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

	// --- the strafe bank (`V_CalcRoll`) ---
	{
		const FRotator Level = FRotator::ZeroRotator;   // right vector is +Y
		const float Angle = 2.0f;                       // cl_rollangle
		const float Speed = 200.0f;                     // cl_rollspeed, cm/s

		TestEqual(TEXT("a still body does not bank"),
			ElysiumCam::SolveViewRoll(FVector::ZeroVector, Level, Angle, Speed), 0.0f);

		// Below `cl_rollspeed` the bank is proportional; the sign follows which way the strafe goes.
		TestEqual(TEXT("half the roll speed banks half the angle"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, Angle, Speed), 1.0f);
		TestEqual(TEXT("and the other way banks the other way"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, -100.0f, 0.0f), Level, Angle, Speed), -1.0f);

		// At or above it the bank saturates -- it never exceeds `cl_rollangle`.
		TestEqual(TEXT("past the roll speed the bank saturates"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 400.0f, 0.0f), Level, Angle, Speed), 2.0f);

		// Running straight ahead is orthogonal to the right vector, so it never banks.
		TestEqual(TEXT("forward motion does not bank"),
			ElysiumCam::SolveViewRoll(FVector(400.0f, 0.0f, 0.0f), Level, Angle, Speed), 0.0f);

		// Either cvar at zero disables it outright, which is what `cl_rollangle 0` is for.
		TestEqual(TEXT("a zero angle disables the bank"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, 0.0f, Speed), 0.0f);
		TestEqual(TEXT("and so does a zero speed"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, Angle, 0.0f), 0.0f);
	}

	// --- the scripted composition: the last term applied, over whatever the base rig produced ---
	{
		const FVector Base(0.0f, 0.0f, 0.0f);
		const FVector Shot(100.0f, 0.0f, 0.0f);

		// Weight 0 is the identity. This is the property that lets the layer run unconditionally.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator(10.0f, 20.0f, 0.0f), 40.0f, 0.0f);
			TestEqual(TEXT("a weightless shot leaves the base view alone"), L, Base);
			TestEqual(TEXT("including its fov"), Fov, 90.0f);
		}

		// Weight 1 is the shot outright.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator(10.0f, 20.0f, 0.0f), 40.0f, 1.0f);
			TestEqual(TEXT("a full-weight shot is the view"), L, Shot);
			TestEqual(TEXT("with its own fov"), Fov, 40.0f);
		}

		// Mid-ramp is the interpolation the cutscene was authored around.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator::ZeroRotator, 40.0f, 0.5f);
			TestEqual(TEXT("half weight is the midpoint"), (float)L.X, 50.0f);
			TestEqual(TEXT("and the fov meets in the middle"), Fov, 65.0f);
		}

		// A shot file with no `FieldOfView` keeps the player's.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator::ZeroRotator, 0.0f, 1.0f);
			TestEqual(TEXT("a shot with no fov keeps the player's"), Fov, 90.0f);
			TestEqual(TEXT("while still moving the camera"), L, Shot);
		}

		// The rotator lerp takes the short way round, so an edit across +/-180 does not spin. Going
		// the long way would land on 0; the short way lands on +/-180, which is the same heading.
		{
			FVector L = Base; FRotator R(0.0f, 170.0f, 0.0f); float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Base, FRotator(0.0f, -170.0f, 0.0f), 0.0f, 0.5f);
			TestEqual(TEXT("a shot across the +/-180 boundary takes the short way"),
				(float)FMath::Abs(FRotator::NormalizeAxis(R.Yaw)), 180.0f);
		}
	}

	return true;
}

// =====================================================================================
// The modern rig (CCC2) — the remaster half of the camera A/B.
//
// Nothing here reproduces a decompiled function; it is the project's own third-person rig, and the
// assertions are about the three properties that make it *different* from the recovered one: the
// damper is frame-rate independent, collision is asymmetric, and the body sits off-centre.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraRigTest, "Elysium.Substrate.CameraRig", GElysiumTestFlags)
bool FElysiumCameraRigTest::RunTest(const FString&)
{
	using namespace ElysiumRig;

	// --- the damper is exact at any step ---
	{
		// One step of 0.2 s must land exactly where two of 0.1 s do. This is the whole reason the
		// decay is a half-life rather than the recovered rig's `Clamp(K * Dt, 0, 1)` Euler step,
		// and it is what `uv run elysium debug move --hz` compares across rates.
		const FVector Start(0.0f, 0.0f, 0.0f);
		const FVector Target(100.0f, 0.0f, 0.0f);
		const float HalfLife = 0.1f;

		const FVector OneStep = DampToward(Start, Target, HalfLife, 0.2f);
		const FVector TwoSteps = DampToward(DampToward(Start, Target, HalfLife, 0.1f), Target, HalfLife, 0.1f);
		TestTrue(TEXT("the damper composes exactly across a subdivided step"),
			OneStep.Equals(TwoSteps, KINDA_SMALL_NUMBER));

		// And the half-life means what it says.
		TestEqual(TEXT("one half-life closes exactly half the gap"),
			(float)DampToward(Start, Target, HalfLife, HalfLife).X, 50.0f);

		// The degenerate ends.
		TestEqual(TEXT("a zero half-life snaps"),
			(float)DampToward(Start, Target, 0.0f, 1.0f / 60.0f).X, 100.0f);
		TestEqual(TEXT("and a zero step holds"),
			(float)DampToward(Start, Target, HalfLife, 0.0f).X, 0.0f);
	}

	// --- the boom's rotation ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.PitchOffset = 10.0f;

		// The camera sits at `Pivot - Forward * Distance`, so lifting it above the eye line pitches
		// the boom DOWN. Getting this sign backwards puts the camera under the character, where it
		// drags through the floor -- which is what the clip channel showed when it was.
		const FRotator Level = BoomRotation(FRotator(0.0f, 90.0f, 0.0f), Tuning);
		TestEqual(TEXT("lifting the camera pitches the boom down"), (float)Level.Pitch, -10.0f);
		TestEqual(TEXT("and keeps the player's yaw"), (float)Level.Yaw, 90.0f);

		// And the resulting camera really is above the pivot, which is the property the sign is for.
		const FVector Above = BoomTarget(FVector::ZeroVector, Level, 200.0f, Tuning);
		TestTrue(TEXT("so the camera ends up above the pivot"), Above.Z > 0.0);

		// A banked view must not roll the arm, or the character swings across the frame.
		const FRotator Banked = BoomRotation(FRotator(0.0f, 0.0f, 30.0f), Tuning);
		TestEqual(TEXT("a banked view never rolls the boom"), (float)Banked.Roll, 0.0f);

		// The pitch clamp is the rig's, not the controller's.
		const FRotator Steep = BoomRotation(FRotator(-80.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("the boom pitch clamps at the rig's own limit"), (float)Steep.Pitch, Tuning.PitchMin);

		// **A control rotation arrives in Unreal's canonical [0, 360), not signed.**
		// `APlayerCameraManager::LimitViewPitch` ends with `FRotator::ClampAxis`, so a view looking
		// down five degrees reaches here as 355 rather than -5. Clamping that without normalizing
		// pins the boom at `PitchMax` for every downward view and only releases it once the angle
		// wraps past 360 — which reads in game as the camera snapping to maximum-up the moment you
		// look down, then recentring if you keep going. Yaw was always normalized here; pitch has to
		// be too, and these two cases are the difference.
		const FRotator DownWrapped = BoomRotation(FRotator(355.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("an unnormalized downward pitch is read as downward"),
			(float)DownWrapped.Pitch, -15.0f);
		const FRotator SteepWrapped = BoomRotation(FRotator(271.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("and still clamps to the rig's lower limit, never the upper"),
			(float)SteepWrapped.Pitch, Tuning.PitchMin);
	}

	// --- the shoulder offset is in boom space ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.ShoulderOffset = FVector(0.0f, 40.0f, 10.0f);

		// Looking down +X: the camera sits back along -X, right along +Y and up along +Z.
		const FVector Behind = BoomTarget(FVector::ZeroVector, FRotator::ZeroRotator, 200.0f, Tuning);
		TestTrue(TEXT("the camera sits behind the pivot, offset to the shoulder"),
			Behind.Equals(FVector(-200.0f, 40.0f, 10.0f), KINDA_SMALL_NUMBER));

		// Turned 90 degrees, the offset turns with it -- it stays on the same shoulder rather than
		// sliding across the frame.
		const FVector Turned = BoomTarget(FVector::ZeroVector, FRotator(0.0f, 90.0f, 0.0f), 200.0f, Tuning);
		TestTrue(TEXT("and it follows the boom rather than the world"),
			Turned.Equals(FVector(-40.0f, -200.0f, 10.0f), 0.01f));
	}

	// --- collision is asymmetric ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.BoomLength = 220.0f;
		Tuning.MinBoomLength = 40.0f;
		Tuning.WallPullIn = 12.0f;
		Tuning.ReturnSpeed = 260.0f;

		// Contact retracts on the frame it happens. Easing here would leave the wall inside the
		// near plane for the duration of the ease.
		const float Hit = SolveBoomDistance(220.0f, 220.0f, /*bHit*/ true, 100.0f, Tuning, 1.0f / 60.0f);
		TestEqual(TEXT("contact retracts immediately, less the wall pull-in"), Hit, 88.0f);

		// A contact closer than the floor still respects it.
		const float Crushed = SolveBoomDistance(220.0f, 220.0f, true, 20.0f, Tuning, 1.0f / 60.0f);
		TestEqual(TEXT("but never past the minimum boom"), Crushed, 40.0f);

		// Clearance grows back rate-limited -- this is the half that is *not* symmetric, and it is
		// why the camera does not pop out of a doorway the first frame the sweep misses.
		const float Recovering = SolveBoomDistance(88.0f, 220.0f, false, 0.0f, Tuning, 0.1f);
		TestEqual(TEXT("clearance grows back at the return speed"), Recovering, 114.0f);
		TestTrue(TEXT("which is slower than the retract that caused it"), Recovering < 220.0f);

		// It never overshoots the rest length.
		const float Restored = SolveBoomDistance(219.0f, 220.0f, false, 0.0f, Tuning, 1.0f);
		TestEqual(TEXT("and stops at the rest length"), Restored, 220.0f);

		// A zero return speed restores instantly, the A/B against the rate limit.
		Tuning.ReturnSpeed = 0.0f;
		TestEqual(TEXT("a zero return speed restores at once"),
			SolveBoomDistance(88.0f, 220.0f, false, 0.0f, Tuning, 1.0f / 60.0f), 220.0f);
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
	TestEqual(TEXT("the root is reached immediately and leaves after its authored pause"),
		Timed.Departures[0], 1.0f);
	TestEqual(TEXT("TimeControl movement starts after the root dwell"), Timed.Arrivals[1], 3.0f);
	TestEqual(TEXT("the destination pause extends completion"), Timed.EndTime, 3.5f);

	FSample Sample;
	TestTrue(TEXT("the root sample is available during its dwell"), Timed.Sample(0.5f, Sample));
	TestTrue(TEXT("the root is held for its authored pause"), Sample.Position.Equals(A.Position, 0.01f));
	TestTrue(TEXT("the midpoint samples between endpoints after the dwell"), Timed.Sample(2.0f, Sample));
	TestTrue(TEXT("linear rate defaults put the two-point Catmull midpoint at 50"),
		FMath::IsNearlyEqual(Sample.Position.X, 50.0f, 0.01f));
	// Roll is NOT unwrapped onto the shortest path. Retail normalises each key's roll once at spawn
	// and then Catmulls the plain values, so 170 -> -170 sweeps the long way through zero rather than
	// the short 20 degrees across 180. Reproduced rather than corrected: the sweep is what the shot
	// was authored against.
	TestTrue(TEXT("roll interpolates plainly, so 170 to -170 passes through zero"),
		FMath::Abs(Sample.Roll) < 0.1f);
	TestTrue(TEXT("a positive lens becomes a horizontal FOV"), Sample.FieldOfView > 0.0f);
	// Converted before interpolating, not after: the midpoint of a 50mm and a 25mm key is the mean of
	// their two FOVs, not the FOV of the mean focal length (which would read ~52.4 degrees).
	TestTrue(TEXT("the FOV is interpolated, not the focal length"),
		FMath::IsNearlyEqual(Sample.FieldOfView,
			0.5f * (FocalLengthToHorizontalFov(50.0f) + FocalLengthToHorizontalFov(25.0f)), 0.05f));
	TestTrue(TEXT("the path is complete after the destination pause"), Timed.Sample(3.5f, Sample) && Sample.bFinished);

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

	// `CCameraKeyFrame::Activate`'s fold. sp_theatre's courtroom chain authors 87 of its edits as
	// `MoveTime 0.03` and sm_gallery_1 writes 7 as `0.01`; retail's threshold is 0.05, so all of them
	// are rewritten to true zero-time edits at spawn and none survives to be interpolated.
	TestTrue(TEXT("a 0.03 TimeControl segment folds to an edit"), ShouldFold(true, 0.03f));
	TestTrue(TEXT("a 0.01 TimeControl segment folds to an edit"), ShouldFold(true, 0.01f));
	TestTrue(TEXT("retail's threshold is inclusive at 0.05"), ShouldFold(true, 0.05f));
	TestFalse(TEXT("just above the threshold stays camera movement"), ShouldFold(true, 0.0501f));
	TestFalse(TEXT("the fold only applies to TimeControl segments"), ShouldFold(false, 0.03f));
	TestTrue(TEXT("0.1 — the shortest authored move in the corpus — is movement"),
		!ShouldFold(true, 0.1f));

	// What the sampler sees after the fold: MoveTime zeroed, so the segment consumes no time and the
	// destination becomes current immediately. The folded 0.03 s is re-attributed to a pause by the
	// entity, which is where the chain's clock keeps it.
	FPath Folded;
	A.MoveTime = 0.0f;
	A.Pause = 0.0f;
	B.Position = FVector(900.0f, -400.0f, 200.0f);
	B.Pause = 0.03f;   // the re-attributed edit time, now spent AFTER the cut rather than before
	Folded.Points = { A, B };
	Folded.RebuildTimes();
	TestTrue(TEXT("a folded edit is a hard cut at the sampler"), IsHardCut(A));
	TestTrue(TEXT("the folded edit switches to the destination immediately"),
		Folded.Sample(0.0f, Sample) && Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("the re-attributed time is held on the destination key"),
		FMath::IsNearlyEqual(Folded.EndTime, 0.03f, KINDA_SMALL_NUMBER));

	// Above the threshold the segment is a real move — the theatre's own dollies run 0.3 to 15.5.
	FPath ShortMove;
	A.MoveTime = 0.3f;
	B.Pause = 0.0f;
	ShortMove.Points = { A, B };
	ShortMove.RebuildTimes();
	TestFalse(TEXT("a MoveTime above the fold threshold remains camera movement"), IsHardCut(A));
	TestTrue(TEXT("the short move samples between the authored endpoints"),
		ShortMove.Sample(0.15f, Sample)
			&& !Sample.Position.Equals(A.Position, 0.01f)
			&& !Sample.Position.Equals(B.Position, 0.01f));
	TestFalse(TEXT("camera movement never requests a temporal camera cut"),
		CrossesHardCut(ShortMove, 0.0f, 0.3f));

	FPath ZeroCut;
	A.MoveTime = 0.0f;
	ZeroCut.Points = { A, B };
	ZeroCut.RebuildTimes();
	TestTrue(TEXT("an authored zero-time transition is classified as a hard cut"), IsHardCut(A));
	TestTrue(TEXT("a zero-time edit switches to the destination immediately"),
		ZeroCut.Sample(0.0f, Sample) && Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("forward playback reports the zero-time edit exactly once"),
		CrossesHardCut(ZeroCut, -KINDA_SMALL_NUMBER, 0.0f));
	TestFalse(TEXT("a sampled hard cut is not reported again on the next frame"),
		CrossesHardCut(ZeroCut, 0.0f, 0.1f));

	// The A/B: at a threshold of 0 nothing folds, which leaves the theatre's edits as 30-millisecond
	// slews. An exact authored zero is still a cut either way — that one never needed folding.
	if (IConsoleVariable* CutCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.CameraCutSeconds")))
	{
		const float Restore = CutCvar->GetFloat();
		CutCvar->Set(0.0f, ECVF_SetByCode);
		TestFalse(TEXT("elysium.CameraCutSeconds 0 disables the fold"), ShouldFold(true, 0.03f));
		A.MoveTime = 0.0f;
		TestTrue(TEXT("an exact zero is a cut with the fold disabled"), IsHardCut(A));
		CutCvar->Set(Restore, ECVF_SetByCode);
	}

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
		// camera_track publishes a new value every frame. Exercise an updated origin, look-at and lens
		// before the component solves so the rendered-view seam, not only the value stack, is covered.
		TemporalShot.Origin = FVector(120.0f, -30.0f, 45.0f);
		TemporalShot.LookAt = FVector(220.0f, -30.0f, 45.0f);
		TemporalShot.bUseLookAt = true;
		TemporalShot.FieldOfView = FocalLengthToHorizontalFov(35.0f);
		TestTrue(TEXT("a moving track value reaches the live camera shot"),
			TemporalCamera->UpdateShot(TemporalShotId, TemporalShot));
		TemporalCamera->UpdateCamera(1.0f / 60.0f);
		FMinimalViewInfo ScriptedView;
		ScriptedView.PostProcessSettings.MotionBlurAmount = 0.5f;
		TemporalCamera->ApplyToView(ScriptedView);
		TestTrue(TEXT("the refreshed track origin reaches the rendered view"),
			ScriptedView.Location.Equals(TemporalShot.Origin, 0.01f));
		TestTrue(TEXT("the refreshed track target reaches the rendered view"),
			ScriptedView.Rotation.Equals((TemporalShot.LookAt - TemporalShot.Origin).Rotation(), 0.01f));
		TestTrue(TEXT("the refreshed track lens reaches the rendered view"),
			FMath::IsNearlyEqual(ScriptedView.FOV, TemporalShot.FieldOfView, 0.001f));
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
	auto CounterValue = [](FElysiumEntityWorld& CounterWorld, const TCHAR* Name)
	{
		TArray<TPair<FString, FString>> Rows;
		if (FElysiumEntity* CounterEnt = CounterWorld.FindByName(Name))
		{
			CounterEnt->GetDebugState(Rows);
		}
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};
	TestEqual(TEXT("zero-duration completion fires once"), CounterValue(World, TEXT("completed")), 1.0f);
	TArray<TPair<FString, FString>> ReachedRows;
	World.FindByName(TEXT("reached"))->GetDebugState(ReachedRows);
	TestTrue(TEXT("arrival fires the authored OnReachedKeyframe output"),
		ReachedRows.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Value") && FMath::IsNearlyEqual(FCString::Atof(*Row.Value), 1.0f);
		}));
	World.Tick(1.0);
	TestEqual(TEXT("held completion does not fire again"), CounterValue(World, TEXT("completed")), 1.0f);
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
		World.SelectTrackCameraRole(false, PosEntity->Handle);
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(1.0f, 2.0f, 3.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f, true);
		TestTrue(TEXT("the world carries a hard-cut instruction onto the value shot"),
			Services.LastCameraShot.bCameraCut);
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(2.0f, 3.0f, 4.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f);
		TestFalse(TEXT("the temporal cut instruction is one-shot"),
			Services.LastCameraShot.bCameraCut);
	}

	// Retail starts on the root, fires OnReached immediately, dwells for Pause, then leaves. Keep
	// that output ordering covered through the entity-world clock as well as through the pure path.
	FElysiumEntityDefs DwellDefs;
	DwellDefs.MapName = TEXT("__camera_track_root_dwell_test__");
	FElysiumEntityDef Dwell;
	Dwell.Classname = TEXT("camera_track");
	Dwell.TargetName = TEXT("dwell");
	Dwell.Keys.Add(TEXT("TimeControl"), TEXT("1"));
	Dwell.Keys.Add(TEXT("MoveTime"), TEXT("2"));
	Dwell.Keys.Add(TEXT("Pause"), TEXT("1"));
	Dwell.Keys.Add(TEXT("NextKey"), TEXT("dwell_end"));
	FElysiumOutputDef DwellReached = Reached;
	DwellReached.Target = TEXT("dwell_reached");
	Dwell.Outputs.Add(DwellReached);
	FElysiumOutputDef DwellLeaving = Reached;
	DwellLeaving.Name = TEXT("OnLeavingKeyframe");
	DwellLeaving.Target = TEXT("dwell_left");
	Dwell.Outputs.Add(DwellLeaving);
	FElysiumOutputDef DwellCompleted = Completed;
	DwellCompleted.Target = TEXT("dwell_completed");
	Dwell.Outputs.Add(DwellCompleted);
	DwellDefs.Defs.Add(Dwell);

	FElysiumEntityDef DwellEnd;
	DwellEnd.Classname = TEXT("camera_keyframe");
	DwellEnd.TargetName = TEXT("dwell_end");
	DwellEnd.Origin = FVector(100.0f, 0.0f, 0.0f);
	DwellEnd.Keys.Add(TEXT("Pause"), TEXT("0.5"));
	FElysiumOutputDef DwellEndReached = Reached;
	DwellEndReached.Target = TEXT("dwell_end_reached");
	DwellEnd.Outputs.Add(DwellEndReached);
	DwellDefs.Defs.Add(DwellEnd);

	for (const TCHAR* Name : { TEXT("dwell_reached"), TEXT("dwell_left"),
		TEXT("dwell_end_reached"), TEXT("dwell_completed") })
	{
		FElysiumEntityDef DwellCounter;
		DwellCounter.Classname = TEXT("math_counter");
		DwellCounter.TargetName = Name;
		DwellDefs.Defs.Add(MoveTemp(DwellCounter));
	}

	FElysiumRecordingServices DwellServices;
	DwellServices.bHasPlayer = true;
	FElysiumEntityWorld DwellWorld(nullptr, nullptr, DwellServices.Bundle());
	DwellWorld.Load(MoveTemp(DwellDefs));
	DwellWorld.Activate(0.0);
	DwellWorld.AcceptInput(TEXT("dwell"), FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	DwellWorld.Tick(0.0);
	TestEqual(TEXT("the root OnReached output fires at play time"),
		CounterValue(DwellWorld, TEXT("dwell_reached")), 1.0f);
	TestEqual(TEXT("the root does not leave at play time"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 0.0f);
	DwellWorld.Tick(0.5);
	TestEqual(TEXT("the root remains held inside its pause"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 0.0f);
	DwellWorld.Tick(1.0);
	TestEqual(TEXT("the root OnLeaving output fires when its pause expires"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 1.0f);
	DwellWorld.Tick(3.0);
	TestEqual(TEXT("the destination is reached after root pause plus MoveTime"),
		CounterValue(DwellWorld, TEXT("dwell_end_reached")), 1.0f);
	TestEqual(TEXT("the destination dwell delays completion"),
		CounterValue(DwellWorld, TEXT("dwell_completed")), 0.0f);
	DwellWorld.Tick(3.5);
	TestEqual(TEXT("completion follows the destination dwell"),
		CounterValue(DwellWorld, TEXT("dwell_completed")), 1.0f);

	// Position and target are exclusive retail player slots, not a last-writer-wins contest between
	// every live track. Put the old pair after the new pair in entity order: without selection leases,
	// their later Think calls reclaim both roles and their completion tears down the newer shot.
	FElysiumEntityDefs ReplacementDefs;
	ReplacementDefs.MapName = TEXT("__camera_track_replacement_test__");
	auto ReplacementTrack = [](const TCHAR* Name, const FVector& At, float Pause)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("camera_track");
		Def.TargetName = Name;
		Def.Origin = At;
		Def.Keys.Add(TEXT("Pause"), *FString::SanitizeFloat(Pause));
		return Def;
	};
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("new_position"), FVector(100.0f, 0.0f, 0.0f), 3.0f));
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("new_target"), FVector(200.0f, 0.0f, 0.0f), 3.0f));
	FElysiumEntityDef OldPosition = ReplacementTrack(TEXT("old_position"), FVector(10.0f, 0.0f, 0.0f), 2.0f);
	FElysiumOutputDef OldCompleted = Completed;
	OldCompleted.Target = TEXT("old_completed");
	OldPosition.Outputs.Add(OldCompleted);
	ReplacementDefs.Defs.Add(MoveTemp(OldPosition));
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("old_target"), FVector(20.0f, 0.0f, 0.0f), 2.0f));
	FElysiumEntityDef OldCounter;
	OldCounter.Classname = TEXT("math_counter");
	OldCounter.TargetName = TEXT("old_completed");
	ReplacementDefs.Defs.Add(MoveTemp(OldCounter));

	FElysiumRecordingServices ReplacementServices;
	ReplacementServices.bHasPlayer = true;
	FElysiumEntityWorld ReplacementWorld(nullptr, nullptr, ReplacementServices.Bundle());
	ReplacementWorld.Load(MoveTemp(ReplacementDefs));
	ReplacementWorld.Activate(0.0);
	auto PlayReplacementRole = [&ReplacementWorld](const TCHAR* Name, const TCHAR* Input)
	{
		ReplacementWorld.AcceptInput(Name, FName(Input), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
	};
	PlayReplacementRole(TEXT("old_target"), TEXT("PlayAsCameraTarget"));
	PlayReplacementRole(TEXT("old_position"), TEXT("PlayAsCameraPosition"));
	PlayReplacementRole(TEXT("new_target"), TEXT("PlayAsCameraTarget"));
	PlayReplacementRole(TEXT("new_position"), TEXT("PlayAsCameraPosition"));
	TestTrue(TEXT("the newer pair owns the composed shot immediately"),
		ReplacementServices.LastCameraShot.Origin.Equals(FVector(100.0f, 0.0f, 0.0f), 0.01f)
			&& ReplacementServices.LastCameraShot.bUseLookAt
			&& ReplacementServices.LastCameraShot.LookAt.Equals(FVector(200.0f, 0.0f, 0.0f), 0.01f));
	ReplacementWorld.Tick(0.5);
	TestTrue(TEXT("later entity-order thinks from superseded tracks cannot reclaim either role"),
		ReplacementServices.LastCameraShot.Origin.Equals(FVector(100.0f, 0.0f, 0.0f), 0.01f)
			&& ReplacementServices.LastCameraShot.LookAt.Equals(FVector(200.0f, 0.0f, 0.0f), 0.01f));
	ReplacementWorld.Tick(2.0);
	TestEqual(TEXT("a superseded track still advances and fires authored completion"),
		CounterValue(ReplacementWorld, TEXT("old_completed")), 1.0f);
	TestTrue(TEXT("superseded completion leaves the newer camera pair live"),
		ReplacementWorld.HasTrackCamera());
	TestEqual(TEXT("superseded completion does not pop the shared shot"),
		ReplacementServices.Count(TEXT("PopCameraShot")), 0);
	ReplacementWorld.Tick(3.0);
	TestFalse(TEXT("the selected pair restores normally when its own clock completes"),
		ReplacementWorld.HasTrackCamera());

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

	V.Interaction.bVisible = true;
	V.Interaction.Icon = 7;
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

// ============================================================================================
// CDynamicProp's spawn/activate/think lifecycle (RE35). Four facts, in the order they happen:
//   * `CBaseProp::Spawn` (FUN_1018df70) stands the prop on a HELD pose — the rest sequence at
//     frame 0 with the play rate at zero — not on a playing clip. `demo_sequence` is not an
//     engine keyfield and contributes nothing; the pose comes from the activity/index rule.
//   * `CDynamicProp::Activate` (FUN_101906c0) resolves `LoopSequence` and arms the start behind
//     a RandomFloat(0.1, 0.99) stagger, so nothing starts on the map's first frozen-time tick.
//   * `SetAnimation` plays on the clip's own STUDIO_LOOPING bit, not a forced one shot.
//   * A finished one-shot HOLDS ITS FINAL FRAME. The think returns without rewriting
//     m_flNextThink once `RandomAnimation` is 0 — true on all 749 shipped entities — so it
//     disarms permanently and the revert-to-LoopSequence branch is unreachable in shipped data.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropAnimateThinkTest,
	"Elysium.Substrate.PropAnimateThink", GElysiumTestFlags)
bool FElysiumPropAnimateThinkTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_stake.mdl"), TEXT("cin_stake"));
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_cigar.mdl"), TEXT("cin_cigar"));
	// Rest clips that are NOT the authored keyvalues, so a pose sourced from `demo_sequence`
	// would be distinguishable from one sourced by the activity/index rule.
	Services.AnimatedPropRestClips.Add(TEXT("cin_stake"), TEXT("idle01"));
	Services.AnimatedPropRestClips.Add(TEXT("cin_cigar"), TEXT("rest_pose"));
	Services.AnimatedPropClipLoops.Add(TEXT("cin_stake|idle01"), true);
	Services.ClipSeconds = 4.0f;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_anim__");
	{
		FElysiumEntityDef Stake;
		Stake.Classname = TEXT("prop_dynamic");
		Stake.TargetName = TEXT("stake");
		Stake.ModelMesh = TEXT("cin_stake");
		Stake.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_stake.mdl"));
		Stake.Keys.Add(TEXT("LoopSequence"), TEXT("idle01"));
		Defs.Defs.Add(MoveTemp(Stake));

		FElysiumEntityDef Cigar;
		Cigar.Classname = TEXT("prop_dynamic");
		Cigar.TargetName = TEXT("cigar");
		Cigar.ModelMesh = TEXT("cin_cigar");
		Cigar.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_cigar.mdl"));
		Cigar.Keys.Add(TEXT("demo_sequence"), TEXT("idle"));
		Defs.Defs.Add(MoveTemp(Cigar));
	}

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Stake = World.FindByName(TEXT("stake"));
	if (!TestNotNull(TEXT("stake resolved"), Stake))
	{
		return false;
	}

	// Spawn: a held rest pose on both props. `loop=0` is load-bearing — the anim proxy's Request
	// early-outs on a repeated looping clip without restoring the play rate, so a hold created as
	// a loop could never be released.
	TestEqual(TEXT("the prop stands its rest sequence, held"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=0")), 1);
	TestTrue(TEXT("the rest pose is seeked to frame 0"),
		Services.Saw(TEXT("SeekCinematicClip 0.000")));
	TestEqual(TEXT("demo_sequence is not an engine keyfield; the rest pose comes from the model"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_cigar rest_pose loop=0")), 1);
	TestEqual(TEXT("nothing stands on the demo_sequence value"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_cigar idle")), 0);

	// Activate armed the loop start behind the stagger, so the frozen-time pass must not fire it.
	World.Tick(0.0);
	TestEqual(TEXT("the authored loop does not start on the map's first tick"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 0);

	// Past the longest stagger (0.99) the loop starts, once, honouring its STUDIO_LOOPING bit.
	World.Tick(1.0);
	TestEqual(TEXT("the authored loop starts after the stagger"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 1);

	// A scripted one-shot replaces the loop and re-arms the think.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("SetAnimation")),
		FElysiumVariant::String(TEXT("scene")), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), Stake->Handle);
	World.Tick(1.0);
	TestEqual(TEXT("SetAnimation plays the one-shot on the clip's own loop bit"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);

	// Mid-clip nothing changes (ClipSeconds is 4).
	World.Tick(3.0);
	TestEqual(TEXT("the one-shot is left alone while it runs"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);

	// Past the clip's end the prop holds its final frame: OnAnimationDone fires and the think
	// disarms. Retail does not return to `LoopSequence` and does not fall back to a rest pose.
	World.Tick(5.5);
	TestEqual(TEXT("a finished one-shot does not revert to LoopSequence"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 1);
	TestEqual(TEXT("a finished one-shot does not restart itself"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);
	TestEqual(TEXT("the think disarms once the clip has finished"),
		Stake->NextThink, ELYSIUM_NEVER_THINK);
	return true;
}

// ============================================================================================
// A prop's clip phase is a function of the substrate clock, not of accumulated animation delta.
//
// Retail gets this without trying: `CBaseAnimating::StudioFrameAdvance` (FUN_1008f120) recomputes
// its interval as `curtime - m_flAnimTime` and leaves the stamp at curtime, so the advance
// telescopes and the total is exactly elapsed game time however many calls there were. That is why
// `CDynamicPropAnimThink` can run at 10 Hz beside a choreo actor seeked every frame and neither
// drifts. Unreal's sequence player accumulates instead, so the 10 Hz think measures the clip
// against elapsed game time and corrects it when the two have parted company.
//
// The correction is deliberately NOT `SeekCinematicClip`: that pins the body at play rate 0 and
// collapses any crossfade, which is right for a scene driving every frame and wrong for a clip that
// must keep running smoothly between corrections.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropClipResyncTest,
	"Elysium.Substrate.PropClipResync", GElysiumTestFlags)
bool FElysiumPropClipResyncTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_stake.mdl"), TEXT("cin_stake"));
	Services.AnimatedPropModels.Add(TEXT("models/props/lamp.mdl"), TEXT("lamp"));
	Services.AnimatedPropRestClips.Add(TEXT("cin_stake"), TEXT("idle01"));
	Services.AnimatedPropRestClips.Add(TEXT("lamp"), TEXT("idle"));
	Services.AnimatedPropClipLoops.Add(TEXT("cin_stake|idle01"), true);
	Services.ClipSeconds = 4.0f;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_resync__");
	{
		// A looping prop, so the think stays armed for the whole test.
		FElysiumEntityDef Stake;
		Stake.Classname = TEXT("prop_dynamic");
		Stake.TargetName = TEXT("stake");
		Stake.ModelMesh = TEXT("cin_stake");
		Stake.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_stake.mdl"));
		Stake.Keys.Add(TEXT("LoopSequence"), TEXT("idle01"));
		Defs.Defs.Add(MoveTemp(Stake));

		// No LoopSequence: this one never leaves its held rest pose, and never arms a think.
		FElysiumEntityDef Lamp;
		Lamp.Classname = TEXT("prop_dynamic");
		Lamp.TargetName = TEXT("lamp");
		Lamp.ModelMesh = TEXT("lamp");
		Lamp.Keys.Add(TEXT("model"), TEXT("models/props/lamp.mdl"));
		Defs.Defs.Add(MoveTemp(Lamp));
	}

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	USkeletalMeshComponent* Body = Services.AnimatedPropBodies.FindRef(TEXT("cin_stake"));
	USkeletalMeshComponent* LampBody = Services.AnimatedPropBodies.FindRef(TEXT("lamp"));
	if (!TestNotNull(TEXT("the animated body was built"), Body))
	{
		return false;
	}

	// Past the longest stagger (0.99) the loop is running, and AnimStartTime is stamped at the
	// think that started it. From here on the expected phase is `Now - that stamp`.
	World.Tick(1.0);
	const int32 AfterStart = Services.Count(TEXT("ResyncCinematicClip"));

	// A body that reports no position at all — no anim host — is an ordinary no-op, not an error
	// and not a correction.
	World.Tick(1.2);
	TestEqual(TEXT("a body that cannot report its position is never resynced"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// In phase: the clip sits exactly where elapsed game time says it should.
	Services.ClipPositions.Add(Body, 0.4f);
	World.Tick(1.4);
	TestEqual(TEXT("a clip in phase is left alone"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// Inside the tolerance. This is the assertion that stops the tolerance being quietly tightened:
	// the position read here is up to one frame stale by construction, so correcting a sub-frame
	// difference would snap on every think and read as a 10 Hz stutter.
	Services.ClipPositions.Add(Body, 0.6f - 0.02f);
	World.Tick(1.6);
	TestEqual(TEXT("drift under the tolerance is not worth a correction"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// Beyond it: exactly one correction, to elapsed game time — not to the position plus a delta.
	Services.ClipPositions.Add(Body, 0.3f);
	World.Tick(1.8);
	TestEqual(TEXT("real drift is corrected once"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart + 1);
	TestTrue(TEXT("and corrected to elapsed game time"),
		Services.Saw(TEXT("ResyncCinematicClip 0.800")));
	TestTrue(TEXT("the body reads the corrected phase back"),
		FMath::IsNearlyEqual(Services.ClipPositions.FindRef(Body), 0.8f, 1e-3f));

	// A looping clip wraps at the source, in double: at 9.0 s of elapsed time on a 4 s clip the
	// phase is 1.0, never 9.0. Narrowing an unbounded elapsed time to float instead would quantise
	// visibly over a long session.
	Services.ClipPositions.Add(Body, 0.0f);
	World.Tick(10.0);
	TestTrue(TEXT("a looping phase wraps into the clip's own length"),
		Services.Saw(TEXT("ResyncCinematicClip 1.000")));

	// Across the loop seam the distance is circular. The clip started at 1.0, so a tick at 13.01
	// puts the phase at 12.01 mod 4 = 0.01 — which is 0.02 from a position of 3.99 the short way
	// round, and 3.98 the long way. A plain difference would correct here on every single think.
	const int32 BeforeSeam = Services.Count(TEXT("ResyncCinematicClip"));
	Services.ClipPositions.Add(Body, 3.99f);
	World.Tick(13.01);
	TestEqual(TEXT("drift across the loop seam is measured the short way round"),
		Services.Count(TEXT("ResyncCinematicClip")), BeforeSeam);

	// A prop with no LoopSequence and no random animator stands its held rest pose and arms no
	// think at all, so the pose it was spawned on is never touched again. That — not the
	// bRestPoseHeld guard inside the think — is what actually keeps a resting prop at rest; the
	// guard is the defence for a prop that IS thinking and has been put back on a held pose.
	FElysiumEntity* Lamp = World.FindByName(TEXT("lamp"));
	TestNotNull(TEXT("lamp resolved"), Lamp);
	if (LampBody)
	{
		Services.ClipPositions.Add(LampBody, 3.0f);
	}
	// Put the stake back in phase first (13.0 mod 4 = 1.0 at the tick below), so the only prop that
	// could produce a correction on this tick is the lamp.
	Services.ClipPositions.Add(Body, 1.0f);
	const int32 BeforeLamp = Services.Count(TEXT("ResyncCinematicClip"));
	World.Tick(14.0);
	TestEqual(TEXT("a prop standing at rest never armed a think"),
		Lamp->NextThink, ELYSIUM_NEVER_THINK);
	TestEqual(TEXT("so its held pose is never resynced"),
		Services.Count(TEXT("ResyncCinematicClip")), BeforeLamp);

	return true;
}

// ============================================================================================
// A skeletal prop is placed with the glTF basis, not the static mesh's. `model_quat` is the
// placement of the exporter's Unreal-native OBJ; a glTF body needs the fixed model-local
// correction composed on top, because glTFRuntime imports mdl_gltf.py's Y-up output into its own
// basis. Composing (rather than substituting a yaw-only rotation) is what keeps a placement's
// pitch and roll — 15 of the corpus's animated-prop placements are leaning palms.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimatedPropPlacementTest,
	"Elysium.Substrate.AnimatedPropPlacement", GElysiumTestFlags)
bool FElysiumAnimatedPropPlacementTest::RunTest(const FString&)
{
	auto BuildOne = [](FElysiumRecordingServices& Services, const FVector& SourceAngles,
		const FQuat& ModelQuat, bool bAnimated)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__prop_place__");
		FElysiumEntityDef Prop;
		Prop.Classname = TEXT("prop_dynamic");
		Prop.TargetName = TEXT("prop");
		Prop.ModelMesh = TEXT("prop_mesh");
		Prop.ModelQuat = ModelQuat;
		Prop.Keys.Add(TEXT("model"), TEXT("models/test/prop.mdl"));
		Prop.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%f %f %f"),
			SourceAngles.X, SourceAngles.Y, SourceAngles.Z));
		Defs.Defs.Add(MoveTemp(Prop));
		if (bAnimated)
		{
			Services.AnimatedPropModels.Add(TEXT("models/test/prop.mdl"), TEXT("prop_anim"));
		}
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
	};

	// `sp_theatre` ships eleven of its sixteen animated props at `angles "0 270 0"`, whose exported
	// model_quat is a +90 degree Unreal yaw. The static mesh wants that verbatim; the skeletal body
	// wants 0, because the glTF basis already carries the other -90.
	const FQuat Yaw90(FRotator(0.f, 90.f, 0.f));
	{
		FElysiumRecordingServices Animated;
		BuildOne(Animated, FVector(0.f, 270.f, 0.f), Yaw90, /*bAnimated=*/true);
		TestTrue(TEXT("the skeletal body is built"),
			Animated.Saw(TEXT("BuildAnimatedPropVisual prop_anim")));
		const FRotator Got = Animated.LastAnimatedPropRotation.Rotator();
		TestTrue(FString::Printf(TEXT("a 270 degree Source yaw stands the skeletal body at 0 (got %s)"),
			*Got.ToString()), Got.Equals(FRotator::ZeroRotator, 0.01f));
	}
	{
		FElysiumRecordingServices Static;
		BuildOne(Static, FVector(0.f, 270.f, 0.f), Yaw90, /*bAnimated=*/false);
		const FRotator Got = Static.LastPropRotation.Rotator();
		TestTrue(FString::Printf(TEXT("the static mesh keeps model_quat verbatim (got %s)"),
			*Got.ToString()), Got.Equals(FRotator(0.f, 90.f, 0.f), 0.01f));
	}

	// `sm_oceanhouse_1`'s leaning palms — 15 of the corpus's animated-prop placements carry pitch
	// or roll. A yaw-only derivation would stand these bolt upright; composing preserves the lean,
	// which is the whole reason the correction is a quaternion rather than a replacement rotator.
	{
		FElysiumRecordingServices Leaning;
		const FRotator Authored(24.0994f, 284.031f, -4.27304f);
		BuildOne(Leaning, FVector(24.0994f, 284.031f, -4.27304f), FQuat(Authored), /*bAnimated=*/true);
		TestTrue(TEXT("the leaning skeletal body is built"),
			Leaning.Saw(TEXT("BuildAnimatedPropVisual prop_anim")));
		const FRotator Got = Leaning.LastAnimatedPropRotation.Rotator();
		TestTrue(FString::Printf(TEXT("the authored lean survives the model fix (got %s)"),
			*Got.ToString()), FMath::Abs(Got.Pitch) > 1.f && FMath::Abs(Got.Roll) > 1.f);
		// The fix is exactly a -90 degree model-local yaw on top of the authored placement.
		const FQuat Expected = FQuat(Authored) * FQuat(FRotator(0.f, -90.f, 0.f));
		TestTrue(TEXT("the composed rotation is placement * model fix"),
			Leaning.LastAnimatedPropRotation.Equals(Expected, 0.001f));
	}
	return true;
}

// ============================================================================================
// An indexed model that bakes no playable clip is not an animated representation — it is a
// bind-pose skeleton standing where the baked static mesh should be. `lampfloor`, `glassa` and
// `junkyardcraneb` are the shipped cases; the exporter now keeps them out of the index, and this
// is the runtime's own guard for an index that still carries one.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropZeroClipFallbackTest,
	"Elysium.Substrate.PropZeroClipFallback", GElysiumTestFlags)
bool FElysiumPropZeroClipFallbackTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/scenery/lampfloor.mdl"), TEXT("lampfloor"));
	Services.AnimatedPropRestClips.Add(TEXT("lampfloor"), FString());   // bakes no clip

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_fallback__");
	FElysiumEntityDef Lamp;
	Lamp.Classname = TEXT("prop_dynamic");
	Lamp.TargetName = TEXT("plus_bag");
	Lamp.ModelMesh = TEXT("lampfloor_static");
	Lamp.Keys.Add(TEXT("model"), TEXT("models/scenery/lampfloor.mdl"));
	Lamp.Keys.Add(TEXT("LoopSequence"), TEXT("idle"));
	Defs.Defs.Add(MoveTemp(Lamp));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.Tick(1.0);

	TestFalse(TEXT("a clipless model does not stand a skeletal body"),
		Services.Saw(TEXT("BuildAnimatedPropVisual lampfloor")));
	TestTrue(TEXT("it keeps its baked static mesh instead"),
		Services.Saw(TEXT("BuildPropVisual lampfloor_static")));
	TestEqual(TEXT("and plays nothing, authored LoopSequence notwithstanding"),
		Services.Count(TEXT("PlayAnimatedPropClip")), 0);
	return true;
}

// ============================================================================================
// CLogicRelay::InputTrigger (FUN_101364e0): spawnflag 0x1 removes the relay once it has fired, and
// without 0x2 a fired relay locks out re-entry until its longest delayed output has gone out.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLogicRelayLifetimeTest,
	"Elysium.Substrate.LogicRelayLifetime", GElysiumTestFlags)
bool FElysiumLogicRelayLifetimeTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("no entity named '!self' in this map"),
		EAutomationExpectedErrorFlags::Contains, 1);
	auto AddRelay = [](FElysiumEntityDefs& Defs, const TCHAR* Name, const TCHAR* Spawnflags)
	{
		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("logic_relay");
		Relay.TargetName = Name;
		if (Spawnflags) { Relay.Keys.Add(TEXT("spawnflags"), Spawnflags); }
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTrigger");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Wire.Times = -1;
		Relay.Outputs.Add(MoveTemp(Wire));
		Defs.Defs.Add(MoveTemp(Relay));
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__relay_lifetime__");
	AddRelay(Defs, TEXT("plain"), nullptr);
	AddRelay(Defs, TEXT("oneshot"), TEXT("1"));
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	}

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	auto CounterValue = [&World]() -> float
	{
		const FElysiumEntity* Counter = World.FindByName(TEXT("counter1"));
		if (!Counter) { return -1.0f; }
		TArray<TPair<FString, FString>> State;
		Counter->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};

	FElysiumEntity* Plain = World.FindByName(TEXT("plain"));
	FElysiumEntity* OneShot = World.FindByName(TEXT("oneshot"));
	if (!TestNotNull(TEXT("plain relay resolved"), Plain)
		|| !TestNotNull(TEXT("oneshot relay resolved"), OneShot))
	{
		return false;
	}

	auto Trigger = [&World](FElysiumEntity* Relay, double Now)
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(),
			/*Delay*/ 0.0, FElysiumEntityHandle::Invalid(), Relay->Handle);
		World.Tick(Now);
	};

	// --- The refire lockout ---------------------------------------------------------------------
	Trigger(Plain, 0.0);
	TestEqual(TEXT("the relay fired once"), CounterValue(), 5.0f);

	// A second Trigger inside the lockout is swallowed. The relay's OnTrigger rows carry no delay,
	// so the queued EnableRefire is due at 0.001.
	Trigger(Plain, 0.0);
	TestEqual(TEXT("a re-trigger inside the lockout is swallowed"), CounterValue(), 5.0f);

	World.Tick(0.002);   // deliver EnableRefire
	Trigger(Plain, 0.002);
	TestEqual(TEXT("the relay fires again once the lockout has lifted"), CounterValue(), 10.0f);

	// --- Remove on fire -------------------------------------------------------------------------
	Trigger(OneShot, 0.002);
	TestEqual(TEXT("the one-shot relay fired"), CounterValue(), 15.0f);
	TestNull(TEXT("a REMOVE_ON_FIRE relay is gone from name lookup"),
		World.FindByName(TEXT("oneshot")));

	// Its handle no longer resolves, so a second Trigger addressed at it goes nowhere.
	Trigger(OneShot, 0.002);
	TestEqual(TEXT("and cannot fire a second time"), CounterValue(), 15.0f);
	return true;
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
	// Two: this delayed Add, plus the relay's own refire lockout. Triggering relay1 above queued an
	// EnableRefire at max(OnTrigger delay) + 0.001 = 0.001, and the ticks above all run at t=0, so it
	// is never due and is still pending here.
	TestEqual(TEXT("both events still pending at freeze time"), A.Queue().Num(), 2);

	FElysiumMapSnapshot First;
	A.Freeze(First);
	TestEqual(TEXT("the frozen map names itself"), First.MapName, FString(TEXT("__save_test__")));
	TestEqual(TEXT("the pending deliveries are in the snapshot"), First.Queue.Num(), 2);
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
	TestEqual(TEXT("the queue was replaced, not appended to"), B.Queue().Num(), 2);
	// Address the delayed Add by name rather than by queue position — the relay's 0.001s refire
	// lockout sorts ahead of it.
	const FElysiumIOEvent* Delayed = B.Queue().Pending().FindByPredicate(
		[](const FElysiumIOEvent& E) { return E.Input == FName(TEXT("Add")); });
	if (TestNotNull(TEXT("the delayed Add survived the round trip"), Delayed))
	{
		TestEqual(TEXT("its fire time is absolute and unchanged"), Delayed->FireTime, 30.0);
		// §6 — a saved handle is re-stamped against the live epoch, so it resolves again.
		if (const FElysiumEntity* Caller = B.Resolve(Delayed->Caller))
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

	// `ScriptedBody` writes the cutscene body state mid-record inside two leaf blocks — a scene's
	// frozen cast and a beat's NPC claim — so an older payload would read those bytes as the fields
	// that followed them. It is therefore a breaking schema, not an additive one, and it carries the
	// floor up with it.
	TestEqual(TEXT("the floor is the scripted-body schema"),
		(int32)FElysiumSaveVersion::MinSupported, (int32)FElysiumSaveVersion::ScriptedBody);
	TestEqual(TEXT("scripted body is the current schema"),
		(int32)FElysiumSaveVersion::Latest, (int32)FElysiumSaveVersion::ScriptedBody);

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
	AddExpectedError(TEXT("has no saved field 'a_field_from_the_future'"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("is math_counter here but was logic_relay when saved"),
		EAutomationExpectedErrorFlags::Contains, 1);
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
// 12.2b — the lead a scene schedules its speech with belongs to the audio path we run on.
//
// VtMB hands its scenes `snd_mixahead`, 0.1 s. The *behaviour* that buys is "the sample is heard
// at the authored instant"; the *number* is Source's own mixer's lead. Carrying the number into
// Unreal's mixer reproduces Source's implementation instead of VtMB's behaviour, and the line is
// then heard tens of milliseconds ahead of every cue authored against it — lipsync, expressions,
// gestures and camera cuts alike.
//
// This test holds the derivation in place. It fails if the lead stops tracking what the output
// path reports — which is what reintroducing any constant inherited from a different mixer does.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneMixaheadTest, "Elysium.Substrate.SceneMixahead", GElysiumTestFlags)

bool FElysiumSceneMixaheadTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("scene 'test/line.vcd' not found"),
		EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleVariable* MixaheadVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.SceneMixahead"));
	if (!TestNotNull(TEXT("elysium.SceneMixahead is registered"), MixaheadVar))
	{
		return false;
	}
	const float WasMixahead = MixaheadVar->GetFloat();
	ON_SCOPE_EXIT{ MixaheadVar->Set(WasMixahead, ECVF_SetByCode); };

	// --- the default is "derive", not a constant -------------------------------------------
	{
		// A negative default is the whole guard: the moment somebody writes a number here, every
		// scene in the game goes back to being scheduled against a mixer we do not run.
		TestTrue(TEXT("the shipped default derives the lead from the audio path"), WasMixahead < 0.f);
		TestNotEqual(TEXT("  and is not VtMB's snd_mixahead default"), WasMixahead, 0.1f);
	}

	// --- the terms add up, and the fallback is named as one ---------------------------------
	{
		FElysiumAudioLatency L;
		L.MixerQueueSeconds = 0.020f;
		L.EndpointSeconds = 0.030f;
		L.SubmitToRenderSeconds = 0.004f;
		TestEqual(TEXT("the lead is the sum of its terms"), L.Lead(), 0.054f, 1e-6f);
		TestFalse(TEXT("a default-constructed latency has not been near a device"), L.bDeviceQueried);

		// The no-device value is one Unreal mixer callback at 48 kHz. It is a fallback, and the
		// point of asserting it is that it is not, and must not become, 0.1.
		TestEqual(TEXT("the fallback is one 1024-frame callback at 48 kHz"),
			ElysiumAudioLatency::FallbackLeadSeconds, 1024.f / 48000.f, 1e-9f);
		TestNotEqual(TEXT("  and is not Source's mixer's lead"),
			ElysiumAudioLatency::FallbackLeadSeconds, 0.1f);
	}

	// --- a scene leads its speech by whatever the output path reports ------------------------
	//
	// The scene is driven twice with two different reported leads. Comparing the two dispatch
	// instants is what pins the behaviour: a hardcoded constant would produce the same instant
	// both times no matter what the audio path said.
	ElysiumScene::ClearCache();
	ElysiumScene::RegisterInline(TEXT("test/mixahead.vcd"), SceneWith(
		SceneEvent(TEXT("speak"), TEXT("line"), 1.f, 2.f, TEXT("test/line.wav"))));

	// Drive one scene forward in small steps and answer the scene time at which the line was
	// submitted to the audio path, or -1 if it never was.
	auto DispatchTime = [](float ReportedLead) -> double
	{
		FElysiumRecordingServices Services;
		Services.OutputLead = ReportedLead;

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");
		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("scene1");
		S.Keys.Add(TEXT("SceneFile"), TEXT("test/mixahead.vcd"));
		Defs.Defs.Add(MoveTemp(S));

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});

		// 1 ms steps: fine enough that the answer is the lead rather than the step.
		for (int32 Step = 0; Step <= 1500; ++Step)
		{
			const double T = Step * 0.001;
			World.Tick(T);
			if (Services.Saw(TEXT("Submit")))
			{
				return T;
			}
		}
		return -1.0;
	};

	{
		const double Short = DispatchTime(0.020f);
		const double Long = DispatchTime(0.250f);
		TestTrue(TEXT("the line is submitted under a short lead"), Short > 0.0);
		TestTrue(TEXT("the line is submitted under a long lead"), Long > 0.0);
		// The authored start is 1.0, so each dispatch lands one step past `1 - lead`.
		TestEqual(TEXT("a 20 ms lead submits 20 ms before the authored instant"), Short, 0.980, 0.002);
		TestEqual(TEXT("a 250 ms lead submits 250 ms before it"), Long, 0.750, 0.002);
		TestTrue(TEXT("so the lead tracks the audio path rather than a constant"),
			FMath::Abs((Short - Long) - 0.230) < 0.004);
	}

	// --- the retail constant survives as an explicit override --------------------------------
	// Reproducing VtMB's own 0.1 is still one console command away, which is what keeps the
	// derived lead A/B-able against the behaviour the original shipped with.
	{
		MixaheadVar->Set(0.1f, ECVF_SetByCode);
		const double Forced = DispatchTime(0.250f);
		TestEqual(TEXT("a forced mixahead overrides what the audio path reports"), Forced, 0.900, 0.002);
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
	AddExpectedError(TEXT("scene 'test/does_not_exist.vcd' not found"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("SceneFile 'test/does_not_exist.vcd' did not resolve"),
		EAutomationExpectedErrorFlags::Contains, 1);
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

	// --- position_start places ONCE and freezes; position_end 2 restores ---------------------
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

		// The placement is the scene's only write. VtMB holds its cast by immobilising the body
		// (FUN_10081ed0's MOVETYPE_NONE + SOLID_NONE), not by rewriting the transform every frame,
		// so a mid-scene move stays where it was put — nothing drags it back.
		const FVector Moved(-1.f, -1.f, -1.f);
		Actor->SetRuntimeOrigin(Moved);
		T = 1.0; World.Tick(T);
		TestEqual(TEXT("  and does not re-pin it per frame"), Actor->Origin, Moved);
		Actor->SetRuntimeOrigin(Mark);   // put it back for the position_end assertion below

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

	// --- the dormant map walk resolves scene, scripted and output animation references --------
	{
		FElysiumRecordingServices Services;
		Services.bCinematicClipsResolve = true;
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__animation_preload__");

		FElysiumEntityDef Actor;
		Actor.Classname = TEXT("npc_VHumanCombatant");
		Actor.TargetName = TEXT("A");
		Actor.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Actor));

		FElysiumEntityDef SceneDef;
		SceneDef.Classname = TEXT("logic_choreographed_scene");
		SceneDef.TargetName = TEXT("resident_scene");
		SceneDef.Keys.Add(TEXT("SceneFile"), TEXT("test/scene.vcd"));
		SceneDef.Keys.Add(TEXT("BaseAnim"), TEXT("models/cinematic/test_scene.mdl"));
		Defs.Defs.Add(MoveTemp(SceneDef));

		FElysiumEntityDef Sequence;
		Sequence.Classname = TEXT("scripted_sequence");
		Sequence.TargetName = TEXT("resident_sequence");
		Sequence.Keys.Add(TEXT("m_iszEntity"), TEXT("A"));
		Sequence.Keys.Add(TEXT("m_iszPlay"), TEXT("action_clip"));
		Defs.Defs.Add(MoveTemp(Sequence));

		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("logic_relay");
		FElysiumOutputDef SetAnim;
		SetAnim.Name = TEXT("OnTrigger");
		SetAnim.Target = TEXT("A");
		SetAnim.Input = TEXT("SetAnimation");
		SetAnim.Param = TEXT("wire_clip");
		Relay.Outputs.Add(MoveTemp(SetAnim));
		Defs.Defs.Add(MoveTemp(Relay));

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		Services.Calls.Reset();
		World.PreloadMapAnimations();

		TestFalse(TEXT("the residency walk does not activate the entity world"), World.IsActive());
		TestTrue(TEXT("a choreographed sequence clip is preloaded"),
			Services.Saw(TEXT("PreloadCinematicClip male_citizen")));
		TestTrue(TEXT("a scripted_sequence action clip is preloaded"),
			Services.Saw(TEXT("PreloadNpcClip male_citizen action_clip")));
		TestTrue(TEXT("a SetAnimation output parameter is preloaded"),
			Services.Saw(TEXT("PreloadNpcClip male_citizen wire_clip")));
		TestFalse(TEXT("preloading does not play a cinematic clip"),
			Services.Saw(TEXT("PlayCinematicClip")));

		// Python may recast an actor after the dormant map walk. Start must resolve the VCD again
		// against that replacement skeleton and finish the batch before the scene clock/playback.
		World.Activate(0.0);
		FElysiumEntity* LiveActor = World.FindByName(TEXT("A"));
		Services.Calls.Reset();
		LiveActor->SetRuntimeModel(
			TEXT("models/character/npc/common/female_citizen.mdl"));
		TestTrue(TEXT("SetModel queues the map closure for the replacement skeleton"),
			Services.Saw(TEXT("PreloadCinematicClip female_citizen")));
		Services.Calls.Reset();
		World.EnqueueInput(TEXT("resident_scene"), FName(TEXT("Start")),
			FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(0.0);

		int32 RecastPreload = INDEX_NONE;
		int32 Finish = INDEX_NONE;
		for (int32 Index = 0; Index < Services.Calls.Num(); ++Index)
		{
			if (RecastPreload == INDEX_NONE
				&& Services.Calls[Index].StartsWith(
					TEXT("PreloadCinematicClip female_citizen")))
			{
				RecastPreload = Index;
			}
			if (Finish == INDEX_NONE
				&& Services.Calls[Index].StartsWith(TEXT("FinishAnimationPreload")))
			{
				Finish = Index;
			}
		}
		TestTrue(TEXT("scene Start re-resolves the Python-recast actor"),
			RecastPreload != INDEX_NONE);
		TestTrue(TEXT("scene Start closes residency after resolving its actual cast"),
			Finish > RecastPreload);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeatherStateTest,
	"Elysium.Substrate.Weather.State", GElysiumTestFlags)
bool FElysiumWeatherStateTest::RunTest(const FString&)
{
	FElysiumWeatherState State;
	State.Configure(/*fade in*/ 10.0f, /*fade out*/ 20.0f, /*initial*/ 0.0f, 0.0);
	State.Retarget(1.0f, 0.0);
	State.Tick(5.0);
	TestTrue(TEXT("ten-second wet fade is halfway at five seconds"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.5f));

	State.Retarget(0.0f, 5.0);
	State.Tick(15.0);
	TestTrue(TEXT("mid-fade retarget is continuous and uses the twenty-second dry duration"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.25f));
	State.Tick(15.0);
	TestTrue(TEXT("a paused game clock does not advance wetness"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.25f));
	State.Retarget(2.0f, 15.0);
	TestTrue(TEXT("wetness targets clamp to one"), FMath::IsNearlyEqual(State.TargetWetness, 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeatherEmitterTest,
	"Elysium.Substrate.Weather.Emitters", GElysiumTestFlags)
bool FElysiumWeatherEmitterTest::RunTest(const FString&)
{
	auto BuildDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("sm_hub_1");
		FElysiumEntityDef World;
		World.Classname = TEXT("worldspawn");
		World.Keys.Add(TEXT("wetness_fadein"), TEXT("10"));
		World.Keys.Add(TEXT("wetness_fadeout"), TEXT("20"));
		World.Keys.Add(TEXT("wetness_fadetarget"), TEXT("0"));
		Defs.Defs.Add(MoveTemp(World));
		FElysiumEntityDef EventsWorld;
		EventsWorld.Classname = TEXT("events_world");
		EventsWorld.TargetName = TEXT("world");
		Defs.Defs.Add(MoveTemp(EventsWorld));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FElysiumEntityDef Emitter;
			Emitter.Classname = TEXT("env_particle");
			Emitter.TargetName = TEXT("rain_emitter");
			Emitter.Origin = FVector(Index * 100.0f, 0.0f, 50.0f);
			Emitter.Keys.Add(TEXT("active"), TEXT("1"));
			Emitter.Keys.Add(TEXT("particle_definition"), TEXT("rain_follow_emitter"));
			Emitter.Keys.Add(TEXT("attach_type"), TEXT("11"));
			Emitter.Keys.Add(TEXT("bounds"), Index == 0 ? TEXT("512") : TEXT("256"));
			Emitter.Keys.Add(TEXT("ramp_scale"), TEXT("0"));
			Emitter.Keys.Add(TEXT("ramp_time"), TEXT("10"));
			Defs.Defs.Add(MoveTemp(Emitter));
		}
		return Defs;
	};

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(BuildDefs());
	World.Activate(0.0);
	TestEqual(TEXT("both authored emitters are represented"), Services.Emitters.Num(), 2);
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("emitters start active with an authored zero rate"),
			Pair.Value.bActive && FMath::IsNearlyZero(Pair.Value.RateScale));
	}

	World.AcceptInput(TEXT("rain_emitter"), FName(TEXT("SetRateScale")),
		FElysiumVariant::Float(1.0f), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.AcceptInput(TEXT("world"), FName(TEXT("FadeGlobalWetness")),
		FElysiumVariant::Float(1.0f), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(5.0);
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("duplicate targetname fanout advances both ramps"),
			FMath::IsNearlyEqual(Pair.Value.RateScale, 0.5f));
	}

	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	FElysiumRecordingServices RestoredServices;
	FElysiumEntityWorld Restored(nullptr, nullptr, RestoredServices.Bundle());
	Restored.Load(BuildDefs());
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(5.0);
	Restored.Tick(10.0);
	TestTrue(TEXT("saved wetness fade resumes rather than restarts"),
		FMath::IsNearlyEqual(RestoredServices.LastWetness.CurrentWetness, 1.0f));
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RestoredServices.Emitters)
	{
		TestTrue(TEXT("saved ramps resume rather than restart"),
			FMath::IsNearlyEqual(Pair.Value.RateScale, 1.0f));
	}

	// A missing presentation service is an explicitly supported headless state.
	FElysiumEntityWorld NullWeather(nullptr, nullptr, FElysiumWorldServices());
	NullWeather.Load(BuildDefs());
	NullWeather.Activate(0.0);
	NullWeather.AcceptInput(TEXT("world"), FName(TEXT("FadeGlobalWetness")),
		FElysiumVariant::Float(1.0f), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	NullWeather.Tick(10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeatherTimerSequenceTest,
	"Elysium.Substrate.Weather.TimerSequence", GElysiumTestFlags)
bool FElysiumWeatherTimerSequenceTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("sm_hub_1");
	auto AddWire = [](FElysiumEntityDef& Source, const TCHAR* Target,
		const TCHAR* Input, const TCHAR* Param, float Delay)
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTimer");
		Wire.Target = Target;
		Wire.Input = Input;
		Wire.Param = Param;
		Wire.Delay = Delay;
		Wire.Times = -1;
		Source.Outputs.Add(MoveTemp(Wire));
	};

	FElysiumEntityDef WorldSpawn;
	WorldSpawn.Classname = TEXT("worldspawn");
	WorldSpawn.Keys.Add(TEXT("wetness_fadein"), TEXT("10"));
	WorldSpawn.Keys.Add(TEXT("wetness_fadeout"), TEXT("20"));
	WorldSpawn.Keys.Add(TEXT("wetness_fadetarget"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(WorldSpawn));
	FElysiumEntityDef EventsWorld;
	EventsWorld.Classname = TEXT("events_world");
	EventsWorld.TargetName = TEXT("world");
	Defs.Defs.Add(MoveTemp(EventsWorld));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FElysiumEntityDef Emitter;
		Emitter.Classname = TEXT("env_particle");
		Emitter.TargetName = TEXT("rain_emitter");
		Emitter.Keys.Add(TEXT("active"), TEXT("1"));
		Emitter.Keys.Add(TEXT("particle_definition"), TEXT("rain_follow_emitter"));
		Emitter.Keys.Add(TEXT("attach_type"), TEXT("11"));
		Emitter.Keys.Add(TEXT("bounds"), Index == 0 ? TEXT("512") : TEXT("256"));
		Emitter.Keys.Add(TEXT("ramp_scale"), TEXT("0"));
		Emitter.Keys.Add(TEXT("ramp_time"), TEXT("10"));
		Defs.Defs.Add(MoveTemp(Emitter));
	}
	FElysiumEntityDef Sound;
	Sound.Classname = TEXT("ambient_generic");
	Sound.TargetName = TEXT("rain_sounds");
	Sound.Keys.Add(TEXT("spawnflags"), TEXT("17"));
	Sound.Keys.Add(TEXT("message"), TEXT("area/Santa_Monica/rain_light_loop.wav"));
	Sound.Keys.Add(TEXT("health"), TEXT("4"));
	Sound.Keys.Add(TEXT("fadein"), TEXT("10"));
	Sound.Keys.Add(TEXT("fadeout"), TEXT("10"));
	Defs.Defs.Add(MoveTemp(Sound));

	FElysiumEntityDef On;
	On.Classname = TEXT("logic_timer");
	On.TargetName = TEXT("rain_on_timer");
	On.Keys.Add(TEXT("StartDisabled"), TEXT("0"));
	On.Keys.Add(TEXT("UseRandomTime"), TEXT("1"));
	On.Keys.Add(TEXT("LowerRandomBound"), TEXT("180"));
	On.Keys.Add(TEXT("UpperRandomBound"), TEXT("300"));
	AddWire(On, TEXT("rain_sounds"), TEXT("PlaySound"), TEXT(""), 0.0f);
	AddWire(On, TEXT("rain_emitter"), TEXT("SetRateScale"), TEXT("1"), 0.0f);
	AddWire(On, TEXT("world"), TEXT("FadeGlobalWetness"), TEXT("1"), 10.0f);
	AddWire(On, TEXT("rain_on_timer"), TEXT("Disable"), TEXT(""), 1.0f);
	AddWire(On, TEXT("rain_off_timer"), TEXT("Enable"), TEXT(""), 1.0f);
	Defs.Defs.Add(MoveTemp(On));

	FElysiumEntityDef Off;
	Off.Classname = TEXT("logic_timer");
	Off.TargetName = TEXT("rain_off_timer");
	Off.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
	Off.Keys.Add(TEXT("UseRandomTime"), TEXT("1"));
	Off.Keys.Add(TEXT("LowerRandomBound"), TEXT("180"));
	Off.Keys.Add(TEXT("UpperRandomBound"), TEXT("500"));
	AddWire(Off, TEXT("rain_sounds"), TEXT("StopSound"), TEXT(""), 0.0f);
	AddWire(Off, TEXT("rain_emitter"), TEXT("SetRateScale"), TEXT("0"), 0.0f);
	AddWire(Off, TEXT("world"), TEXT("FadeGlobalWetness"), TEXT("0"), 10.0f);
	AddWire(Off, TEXT("rain_off_timer"), TEXT("Disable"), TEXT(""), 1.0f);
	AddWire(Off, TEXT("rain_on_timer"), TEXT("Enable"), TEXT(""), 1.0f);
	Defs.Defs.Add(MoveTemp(Off));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	FElysiumEntity* OnTimer = World.FindByName(TEXT("rain_on_timer"));
	FElysiumEntity* OffTimer = World.FindByName(TEXT("rain_off_timer"));
	if (!TestNotNull(TEXT("rain_on_timer exists"), OnTimer)
		|| !TestNotNull(TEXT("rain_off_timer exists"), OffTimer))
	{
		return false;
	}
	TestTrue(TEXT("dry interval is authored random 180-300 seconds"),
		OnTimer->NextThink >= 180.0f && OnTimer->NextThink <= 300.0f);
	TestTrue(TEXT("rain-off timer starts disabled"), OffTimer->NextThink >= ELYSIUM_NEVER_THINK);
	Services.Calls.Reset();

	World.AcceptInput(TEXT("rain_on_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("rain-on emits one audio start"), Services.Count(TEXT("Submit ")), 1);
	TestTrue(TEXT("authored audio fade-in is applied"), Services.Saw(TEXT("Submit area/Santa_Monica/rain_light_loop.wav"))
		&& Services.Calls.ContainsByPredicate([](const FString& Call)
		{
			return Call.StartsWith(TEXT("Submit ")) && Call.Contains(TEXT("fade=10.00"));
		}));
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("rain-on fans out once to each emitter"),
			FMath::IsNearlyEqual(Pair.Value.RampTargetScale, 1.0f));
	}
	World.Tick(1.0);
	TestTrue(TEXT("rain-on disables after one second"), OnTimer->NextThink >= ELYSIUM_NEVER_THINK);
	TestTrue(TEXT("rain duration is authored random 180-500 seconds"),
		OffTimer->NextThink >= 181.0f && OffTimer->NextThink <= 501.0f);
	World.Tick(10.0);
	TestTrue(TEXT("delayed wetness-on reaches target one"),
		FMath::IsNearlyEqual(Services.LastWetness.TargetWetness, 1.0f));

	World.AcceptInput(TEXT("rain_off_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(10.0);
	TestTrue(TEXT("authored audio fade-out is applied"),
		Services.Calls.ContainsByPredicate([](const FString& Call)
		{
			return Call.StartsWith(TEXT("StopVoice ")) && Call.Contains(TEXT("fade=10.00"));
		}));
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("rain-off fans out once to each emitter"),
			FMath::IsNearlyEqual(Pair.Value.RampTargetScale, 0.0f));
	}
	World.Tick(11.0);
	TestTrue(TEXT("rain-off disables after one second"), OffTimer->NextThink >= ELYSIUM_NEVER_THINK);
	TestTrue(TEXT("dry timer re-arms without duplicate output"),
		OnTimer->NextThink >= 191.0f && OnTimer->NextThink <= 311.0f);
	World.Tick(20.0);
	TestTrue(TEXT("delayed wetness-off reaches target zero"),
		FMath::IsNearlyZero(Services.LastWetness.TargetWetness));

	World.AcceptInput(TEXT("rain_on_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(20.0);
	TestEqual(TEXT("the repeating cycle produces one audio start per rain-on"),
		Services.Count(TEXT("Submit ")), 2);
	return true;
}

// =====================================================================================
// The stub report — that an unimplemented surface says so, and that an implemented one
// stays quiet. The second half is the one worth guarding: a stub class names the inputs
// the shipped maps fire at a classname with no leaf, and if such a row ever shadowed a
// base input (Kill/ScriptHide/ScriptUnhide) it would turn a working input into a warning
// that does nothing. The tally is the observable; the warning text is cosmetic, so the
// volume is turned down for the duration rather than expected line by line.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStubReportTest, "Elysium.Substrate.Stubs", GElysiumTestFlags)
bool FElysiumStubReportTest::RunTest(const FString&)
{
	IConsoleVariable* Warn = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.StubWarn"));
	const int32 PrevWarn = Warn ? Warn->GetInt() : 2;
	if (Warn) { Warn->Set(0); }
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT
	{
		if (Warn) { Warn->Set(PrevWarn); }
		ElysiumStub::ClearTally();
	};

	// One stub class (env_sprite — 208 wires across the shipped maps, no leaf), one classname with
	// no registration at all, and one real class to prove the quiet path.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	for (const TCHAR* Pair : { TEXT("env_sprite|sprite1"), TEXT("func_lod|lod1"), TEXT("math_counter|counter1") })
	{
		FString Class, Name;
		FString(Pair).Split(TEXT("|"), &Class, &Name);
		FElysiumEntityDef Def;
		Def.Classname = Class;
		Def.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Def));
	}

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Sprite = World.FindByName(TEXT("sprite1"));
	FElysiumEntity* Lod = World.FindByName(TEXT("lod1"));
	FElysiumEntity* Counter = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("sprite1 resolved"), Sprite)
		|| !TestNotNull(TEXT("lod1 resolved"), Lod)
		|| !TestNotNull(TEXT("counter1 resolved"), Counter))
	{
		return false;
	}

	// A stub class is still an inert record: it names inputs, it does not implement any.
	TestTrue(TEXT("a stub class spawns record-only"), Sprite->IsRecordOnly());
	TestTrue(TEXT("an unregistered classname spawns record-only"), Lod->IsRecordOnly());
	TestFalse(TEXT("a real class does not"), Counter->IsRecordOnly());

	auto FireAt = [&World](const TCHAR* Target, const TCHAR* Input)
	{
		World.AcceptInput(Target, FName(Input), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	};
	auto CountFor = [](const TCHAR* Kind, const TCHAR* Surface) -> int32
	{
		TArray<ElysiumStub::FTally> Rows;
		ElysiumStub::CollectTally(Rows);
		for (const ElysiumStub::FTally& R : Rows)
		{
			if (R.Kind == Kind && R.Surface == Surface) { return R.Count; }
		}
		return 0;
	};

	// 1. A stub class's named input resolves to the shared thunk and reports under its own name.
	FireAt(TEXT("sprite1"), TEXT("HideSprite"));
	FireAt(TEXT("sprite1"), TEXT("HideSprite"));
	TestEqual(TEXT("a stub input reports once per fire"),
		CountFor(TEXT("input"), TEXT("env_sprite.HideSprite")), 2);

	// 2. An input no class on the chain owns reports too — this is what covers the classnames the
	//    stub table does not enumerate.
	FireAt(TEXT("lod1"), TEXT("Frobnicate"));
	TestEqual(TEXT("an unresolvable input reports"),
		CountFor(TEXT("input"), TEXT("func_lod.Frobnicate")), 1);

	// 3. A wire naming an entity the map does not contain is its own kind, not an input gap.
	FireAt(TEXT("no_such_entity"), TEXT("Trigger"));
	TestEqual(TEXT("an unknown target reports as a target"),
		CountFor(TEXT("target"), TEXT("no_such_entity.Trigger")), 1);

	// 4. The shadowing guard: base inputs still work on a stub class and report nothing.
	FireAt(TEXT("sprite1"), TEXT("ScriptHide"));
	TestTrue(TEXT("a base input still reaches a stub class"), Sprite->IsHidden());
	TestEqual(TEXT("a base input on a stub class reports nothing"),
		CountFor(TEXT("input"), TEXT("env_sprite.ScriptHide")), 0);

	// 5. And an implemented input on a real class stays quiet.
	FireAt(TEXT("counter1"), TEXT("Add"));
	TestEqual(TEXT("an implemented input reports nothing"),
		CountFor(TEXT("input"), TEXT("math_counter.Add")), 0);

	return true;
}

// =====================================================================================
// Gaze — the selection cascade, the cone gate, the scripted inputs and the integrator (12.4)
// =====================================================================================
//
// Every arm of this is a plain function of positions and time, which is the point: the half that
// decides where a character looks holds no engine state, so the whole cascade is assertable here
// rather than only in a running world. The one thing this cannot check is that the answer reaches
// a material — that is the content tier's FacialMorphTargets and the live run's job.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGazeTest, "Elysium.Substrate.Gaze", GElysiumTestFlags)
bool FElysiumGazeTest::RunTest(const FString&)
{
	auto MakeWorld = [](FElysiumEntityWorld& World)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__gaze_test__");
		FElysiumEntityDef Watcher;
		Watcher.Classname = TEXT("npc_VVampire");
		Watcher.TargetName = TEXT("watcher");
		Watcher.Origin = FVector::ZeroVector;
		Watcher.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Watcher));
		FElysiumEntityDef Prop;
		Prop.Classname = TEXT("prop_static");
		Prop.TargetName = TEXT("statue");
		Prop.Origin = FVector(300.f, 0.f, 0.f);
		Defs.Defs.Add(MoveTemp(Prop));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
	};

	// The head frame every assertion measures in: eye height, facing +X.
	const FVector Head(0.f, 0.f, ElysiumMove::StandViewZ);
	const FVector Forward(1.f, 0.f, 0.f);
	FElysiumEyeTargetTuning Tuning;
	Tuning.TurnRate = 0.5f;
	// Keep the saccade out of the way of the selection assertions — a fidget would move the
	// commanded point off the subject as soon as the eyes converged on it.
	Tuning.MinInterval = 1000.f;
	Tuning.MaxInterval = 1000.f;

	// --- EyePosition is the view offset, not a bounds fraction -------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("the world has a player"), Player))
		{
			return false;
		}
		Player->Origin = FVector(10.f, 20.f, 30.f);
		TestTrue(TEXT("EyePosition is origin + the standing view offset"),
			Player->EyePosition().Equals(FVector(10.f, 20.f, 30.f + ElysiumMove::StandViewZ)));
	}

	// --- The autonomous scan, and the cone that gates it --------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (!TestNotNull(TEXT("the watcher is a combat character"), Watcher) || Player == nullptr)
		{
			return false;
		}

		// Straight ahead and well inside the 300-unit scan sphere: the player is picked.
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("a candidate inside the cone is chosen, at its EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// The same candidate off to the side is outside the ±30° cone, so the character looks
		// straight ahead instead. dot((1,0,0), normalize(100,500,0)) = 0.196, well under 0.866.
		Watcher->NextEyeLookTime = 0.f;   // let the scan re-pick
		Player->Origin = FVector(100.f, 500.f, 0.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		const FVector Ahead = Head + Forward * (500.f * ElysiumMove::U);
		TestTrue(TEXT("a candidate outside the cone is rejected for straight ahead"),
			Watcher->EyeLookTarget.Equals(Ahead, 0.1f));

		// A prop is not a candidate however well placed — retail's filter admits the player and
		// characters, and `statue` sits dead ahead at 300 units.
		Watcher->NextEyeLookTime = 0.f;
		Player->Origin = FVector(-500.f, 0.f, 0.f);   // behind, so only the prop is in the cone
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("a non-character in the cone is not a gaze candidate"),
			Watcher->EyeLookTarget.Equals(Ahead, 0.1f));
	}

	// --- The dialogue arm, and the DialogPOV redirect -----------------------------------------
	// The camera-shot table's how-to defines `DialogPOV "1"` as "NPCs will look at the camera during
	// dialog, rather than the player's eye position", and 51 of the 66 shipped shot files set it —
	// so this, not the 40 authored `LookAtEntity*` wires, is where most of VtMB's look-at happens.
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();

		// The smallest conversation that opens: one spoken NPC line.
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		if (!TestTrue(TEXT("the gaze fixture conversation parses"),
			FElysiumDlgFile::ParseBytes(
				ElysiumDlgBytes({ ElysiumDlgRow(11, TEXT("Hello."), TEXT("#"), TEXT(""), TEXT("")) }),
				File.Get())))
		{
			return false;
		}
		TSharedRef<FElysiumDlgConversation> Conv = MakeShared<FElysiumDlgConversation>(
			File, /*bMale*/ true, /*bMalk*/ false,
			[](const FString&) { return true; }, [](const FString&) {});
		Conv->Start();
		World.OpenDialog(Watcher->Handle, Conv);

		// With no shot asking for it, the NPC aims at the player's eye.
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("the dialogue arm aims at the partner's EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// With one, it aims at the camera instead. Placed inside the cone so nothing else can be
		// what moved it, and away from the player so the two answers cannot be confused.
		const FVector CameraPoint(420.f, 60.f, ElysiumMove::StandViewZ + 40.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning, &CameraPoint);
		TestTrue(TEXT("DialogPOV redirects the dialogue arm to the camera"),
			Watcher->EyeLookTarget.Equals(CameraPoint, 0.1f));

		// It replaces the *player* as the subject and nothing else: the player looking back at the
		// NPC still resolves the NPC, with the same point supplied.
		Player->NextFidgetTime = TNumericLimits<float>::Max();
		Player->TickGaze(0.f, 0.f, Head, Forward, Tuning, &CameraPoint);
		TestTrue(TEXT("DialogPOV does not redirect the player's own aim"),
			Player->EyeLookTarget.Equals(Watcher->EyePosition(), 0.1f));
	}

	// --- The four scripted inputs -------------------------------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		// Hold the saccade off. It engages the moment the eyes converge, and every assertion below
		// is about *what was selected*, which a fidget would immediately move off.
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();

		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());

		Watcher->InputLookAtEntityEye(Args);
		TestEqual(TEXT("LookAtEntityEye pushes mode 1"), Watcher->EyeLookMode, 1);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...and aims at the target's EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// The shipped defect: Center pushes the Eye constant, so it lands on EyePosition too. If
		// this ever starts resolving WorldSpaceCenter, we have diverged from retail.
		Watcher->InputLookAtEntityDefault(Args);
		Watcher->InputLookAtEntityCenter(Args);
		TestEqual(TEXT("LookAtEntityCenter pushes mode 1, reproducing the shipped defect"),
			Watcher->EyeLookMode, 1);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...so Center aims exactly where Eye does"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		Watcher->InputLookAtEntityOrigin(Args);
		TestEqual(TEXT("LookAtEntityOrigin pushes mode 3"), Watcher->EyeLookMode, 3);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...and aims at the target's origin, not its eyes"),
			Watcher->EyeLookTarget.Equals(Player->Origin, 0.1f));

		Watcher->InputLookAtEntityDefault(Args);
		TestEqual(TEXT("LookAtEntityDefault clears the mode"), Watcher->EyeLookMode, 0);
		TestTrue(TEXT("...and the target name with it"), Watcher->EyeLookTargetName.IsEmpty());

		// A scripted target naming an entity that is not there yields back to autonomous rather
		// than holding a dead aim.
		FElysiumInputArgs Gone;
		Gone.Param = FElysiumVariant::String(TEXT("no_such_entity"));
		Watcher->InputLookAtEntityEye(Gone);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestEqual(TEXT("a scripted target that is gone releases to autonomous"),
			Watcher->EyeLookMode, 0);
	}

	// --- The integrator is a fixed 0.1 s step, not a per-frame lerp ---------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());
		Watcher->InputLookAtEntityEye(Args);

		// Seed the smoothed point somewhere definite, then step exactly one interval at rate 0.5:
		// the result must be the midpoint. A frame-rate-dependent lerp would land elsewhere.
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		const FVector Commanded = Watcher->EyeLookTarget;
		const FVector Seed(0.f, 0.f, 100.f);
		Watcher->CurEyeTarget = Seed;
		Watcher->EyeIntegAccumulator = 0.f;
		Watcher->TickGaze(0.f, 0.1f, Head, Forward, Tuning);
		TestTrue(TEXT("one 0.1 s step at rate 0.5 moves the smoothed point halfway"),
			Watcher->CurEyeTarget.Equals(Seed + (Commanded - Seed) * 0.5f, 0.5f));

		// Two half-steps make one whole one — the accumulator carries the remainder rather than
		// discarding it, which is the whole reason the step is fixed.
		Watcher->CurEyeTarget = Seed;
		Watcher->EyeIntegAccumulator = 0.f;
		Watcher->TickGaze(0.f, 0.05f, Head, Forward, Tuning);
		TestTrue(TEXT("half an interval alone moves nothing"),
			Watcher->CurEyeTarget.Equals(Seed, 0.01f));
		Watcher->TickGaze(0.f, 0.05f, Head, Forward, Tuning);
		TestTrue(TEXT("...and the second half completes the same single step"),
			Watcher->CurEyeTarget.Equals(Seed + (Commanded - Seed) * 0.5f, 0.5f));

		TestEqual(TEXT("the integration rate is published from the disposition"),
			Watcher->EyeIntegRate, 0.5f);
	}

	// --- The fidget walks the authored keypad cells in order ----------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());
		Watcher->InputLookAtEntityEye(Args);

		// Anger's authored triple, and a zero hold so each call advances exactly one step.
		FElysiumEyeTargetTuning Anger;
		Anger.FidgetPoints[0] = 0;
		Anger.FidgetPoints[1] = 2;
		Anger.FidgetPoints[2] = 0;
		Anger.HoldMin = 0.f;
		Anger.HoldMax = 0.f;
		Anger.TurnRate = 1.f;   // converge immediately so the saccade can engage

		Watcher->TickGaze(0.f, 1.f, Head, Forward, Anger);   // converge on the target
		Watcher->TickGaze(1.f, 0.1f, Head, Forward, Anger);  // step 0 -> cell 0
		TestEqual(TEXT("the fidget starts at the first authored cell"), Watcher->FidgetCell, 0);
		TestEqual(TEXT("...as step 0"), Watcher->FidgetStep, 0);

		Watcher->TickGaze(2.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("the second step takes the second authored cell"), Watcher->FidgetCell, 2);
		// Cell 2 is bottom-centre: 20 degrees below the head's forward, no yaw. That has to move the
		// commanded point DOWN and leave it dead ahead in plan, or the keypad is transposed.
		TestTrue(TEXT("cell 2 aims below the head"), Watcher->EyeLookTarget.Z < Head.Z);
		TestTrue(TEXT("...and does not yaw off centre"),
			FMath::IsNearlyZero(Watcher->EyeLookTarget.Y, 0.5f));

		Watcher->TickGaze(3.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("the third step takes the third authored cell"), Watcher->FidgetCell, 0);
		Watcher->TickGaze(4.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("exhausting the sequence ends the fidget"), Watcher->FidgetStep, -1);

		// Cell 8 is top-centre, the mirror of cell 2 — the row arithmetic has to be symmetric. One
		// call, not two: with a zero hold every call advances a step, so a second would already be
		// on the next cell.
		FElysiumEyeTargetTuning Up = Anger;
		Up.FidgetPoints[0] = 8;
		Watcher->FidgetStep = -1;
		Watcher->NextFidgetTime = 0.f;
		Watcher->TickGaze(5.f, 0.1f, Head, Forward, Up);
		TestEqual(TEXT("cell 8 is the top-centre cell"), Watcher->FidgetCell, 8);
		TestTrue(TEXT("...and aims above the head"), Watcher->EyeLookTarget.Z > Head.Z);
	}

	return true;
}

// =====================================================================================
// Blend grids (CAP7.3) — the axis arithmetic, content-free.
//
// The bug this guards is not subtle once stated: a VtMB locomotion sequence is a 9-cell fan over
// `move_yaw` running -180..180, and the exporter bakes the grid's base cell under the sequence's
// label. Cell 0 is the -180 cell, so the clip named `walk` is the BACKWARD walk. The whole point of
// resolving the grid is that 0 degrees lands on cell 4 and the character walks forward.
// =====================================================================================

namespace
{
	FElysiumBlendTable MakeYawTable()
	{
		FElysiumBlendTable Table;
		FElysiumPoseParamDesc MoveYaw;
		MoveYaw.Name = TEXT("move_yaw");
		MoveYaw.Flags = 1;
		MoveYaw.Start = -180.f;
		MoveYaw.End = 180.f;
		MoveYaw.Loop = 360.f;      // wraps
		Table.PoseParams.Add(MoveYaw);

		FElysiumPoseParamDesc AimYaw;
		AimYaw.Name = TEXT("aim_yaw");
		AimYaw.Start = -45.f;
		AimYaw.End = 45.f;
		AimYaw.Loop = 0.f;         // does NOT wrap
		Table.PoseParams.Add(AimYaw);

		// The shipped shape: 9 cells on axis 0, no axis 1.
		FElysiumBlendGrid Walk;
		Walk.Label = TEXT("walk");
		Walk.GroupSize[0] = 9;
		Walk.GroupSize[1] = 1;
		Walk.ParamIndex[0] = 0;
		Walk.ParamIndex[1] = INDEX_NONE;
		Walk.ParamStart[0] = -180.f;
		Walk.ParamEnd[0] = 180.f;
		static const TCHAR* Names[9] = { TEXT("walk_180"), TEXT("walk_225"), TEXT("walk_270"),
			TEXT("walk_315"), TEXT("walk_0"), TEXT("walk_45"), TEXT("walk_90"), TEXT("walk_135"),
			TEXT("walk_180") };
		for (int32 i = 0; i < 9; ++i)
		{
			FElysiumBlendCell Cell;
			Cell.Axis[0] = i;
			Cell.Axis[1] = 0;
			Cell.Clip = Names[i];
			if (i == 4)
			{
				Cell.Motion.CycleSeconds = 1.2f;
				Cell.Motion.GroundDistanceCm = 164.0f;
				Cell.Motion.GroundSpeedCmPerSecond = 136.7f;
			}
			Walk.Cells.Add(Cell);
		}
		Table.Grids.Add(Walk.Label, Walk);
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlendGridAxisTest,
	"Elysium.Substrate.BlendGrids", GElysiumTestFlags)
bool FElysiumBlendGridAxisTest::RunTest(const FString&)
{
	const FElysiumBlendTable Table = MakeYawTable();
	const FElysiumBlendGrid& Walk = *Table.Find(TEXT("walk"));
	const FElysiumPoseParamDesc* MoveYaw = Table.Param(0);

	int32 Cell = -1;
	float Fraction = -1.f;

	// THE assertion. A body nothing has steered sits at move_yaw 0, which is straight ahead, and a
	// -180..180 fan of nine puts that exactly on cell 4. Anything else and `walk` plays sideways or
	// backwards.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 0.f, Cell, Fraction);
	TestEqual(TEXT("move_yaw 0 selects the middle cell"), Cell, 4);
	TestTrue(TEXT("...exactly, with no fraction"), FMath::IsNearlyZero(Fraction));

	// The endpoints. Under a 360 wrap +180 IS -180 — the same heading named twice — so it resolves
	// to cell 0, and the fan's last cell holds the same clip precisely because of that seam. Cell 8
	// is therefore unreachable by any wrapped value, which costs nothing: it is a duplicate.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, -180.f, Cell, Fraction);
	TestEqual(TEXT("-180 is cell 0"), Cell, 0);
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 180.f, Cell, Fraction);
	TestEqual(TEXT("+180 wraps onto the same cell as -180"), Cell, 0);
	TestEqual(TEXT("...and the two ends of the fan are the same animation"),
		Walk.CellAt(0, 0)->Clip, Walk.CellAt(8, 0)->Clip);

	// Just short of the seam is the far end of the fan.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 179.f, Cell, Fraction);
	TestEqual(TEXT("179 degrees is the last distinct cell"), Cell, 7);
	TestTrue(TEXT("...nearly all the way to the seam"), Fraction > 0.9f);

	// A value past the end wraps through the loop modulus rather than clamping: 190 is -170.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 190.f, Cell, Fraction);
	TestEqual(TEXT("190 wraps to -170, just past cell 0"), Cell, 0);
	TestTrue(TEXT("...carrying a fraction toward cell 1"), Fraction > 0.f);

	// Halfway between two cells reports the fraction the two-cell blend will weigh on.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 22.5f, Cell, Fraction);
	TestEqual(TEXT("22.5 degrees sits on cell 4"), Cell, 4);
	TestTrue(TEXT("...half a cell toward cell 5"), FMath::IsNearlyEqual(Fraction, 0.5f, 0.001f));

	// An unused axis: documented as cell 0, weight 0. Its range is a degenerate 0/0, so this also
	// proves nothing divided by it.
	ElysiumBlendGrids::ResolveAxis(Walk, 1, nullptr, 0.f, Cell, Fraction);
	TestEqual(TEXT("an axis with no parameter is cell 0"), Cell, 0);
	TestTrue(TEXT("...with no weight"), FMath::IsNearlyZero(Fraction));

	// A non-wrapping parameter must CLAMP, not wrap. The aim parameters declare loop 0, and wrapping
	// one would swing a gun to the opposite extreme at the edge of its range.
	FElysiumBlendGrid Aim;
	Aim.Label = TEXT("aim");
	Aim.GroupSize[0] = 3;
	Aim.GroupSize[1] = 1;
	Aim.ParamIndex[0] = 1;
	Aim.ParamIndex[1] = INDEX_NONE;
	Aim.ParamStart[0] = -45.f;
	Aim.ParamEnd[0] = 45.f;
	for (int32 i = 0; i < 3; ++i)
	{
		FElysiumBlendCell C;
		C.Axis[0] = i;
		Aim.Cells.Add(C);
	}
	const FElysiumPoseParamDesc* AimYaw = Table.Param(1);
	ElysiumBlendGrids::ResolveAxis(Aim, 0, AimYaw, 0.f, Cell, Fraction);
	TestEqual(TEXT("a level aim is the centre cell"), Cell, 1);
	ElysiumBlendGrids::ResolveAxis(Aim, 0, AimYaw, 400.f, Cell, Fraction);
	TestEqual(TEXT("a non-looping parameter clamps rather than wrapping"), Cell, 2);

	// Selection: the neutral pose picks the forward walk out of the fan.
	const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(Walk, Table,
		FElysiumPoseParams::Neutral());
	if (TestNotNull(TEXT("the neutral pose selects a cell"), Pick.Cell))
	{
		TestEqual(TEXT("...and it is the forward walk, not the base cell"), Pick.Cell->Clip,
			FString(TEXT("walk_0")));
		TestTrue(TEXT("...and carries that cell's authored route speed"),
			FMath::IsNearlyEqual(Pick.Cell->Motion.GroundSpeedCmPerSecond, 136.7f, 0.01f));
	}

	// A steered pose selects a different cell — the property the resolved-name cache key exists for.
	FElysiumPoseParams Strafing;
	Strafing.Set(TEXT("move_yaw"), 90.f);
	const FElysiumBlendPick Sideways = ElysiumBlendGrids::SelectCell(Walk, Table, Strafing);
	if (TestNotNull(TEXT("a steered pose selects a cell"), Sideways.Cell))
	{
		TestEqual(TEXT("...the 90 degree one"), Sideways.Cell->Clip, FString(TEXT("walk_90")));
	}

	// A cell whose animation never baked is skipped rather than played as nothing.
	FElysiumBlendGrid Holed = Walk;
	Holed.Cells[4].Clip.Reset();
	const FElysiumBlendPick Repaired = ElysiumBlendGrids::SelectCell(Holed, Table,
		FElysiumPoseParams::Neutral());
	if (TestNotNull(TEXT("a hole in the fan still resolves"), Repaired.Cell))
	{
		TestTrue(TEXT("...to a neighbour that actually baked"), !Repaired.Cell->Clip.IsEmpty());
	}

	// The sidecar parse, including the null cell the schema permits and the single-cell grid the
	// exporter never writes.
	const FString Json = TEXT(R"({"stem":"t","model":"m",)")
		TEXT(R"("pose_parameters":[{"index":0,"name":"move_yaw","flags":1,)")
		TEXT(R"("start":-180.0,"end":180.0,"loop":360.0}],)")
		TEXT(R"("grids":{"walk":{"numblends":3,"groupsize":[3,1],"paramindex":[0,-1],)")
		TEXT(R"("paramstart":[-180.0,0.0],"paramend":[180.0,0.0],"cells":[)")
		TEXT(R"({"axis":[0,0],"anim":20,"clip":"walk_180","motion":)")
		TEXT(R"({"cycle_seconds":1.2,"ground_distance_cm":164.0,)")
		TEXT(R"("ground_speed_cm_s":136.7}},)")
		TEXT(R"({"axis":[1,0],"anim":16,"clip":null},)")
		TEXT(R"({"axis":[2,0],"anim":17,"clip":"walk_45"}]},)")
		TEXT(R"("lonely":{"numblends":1,"groupsize":[1,1],"paramindex":[-1,-1],)")
		TEXT(R"("paramstart":[0.0,0.0],"paramend":[0.0,0.0],)")
		TEXT(R"("cells":[{"axis":[0,0],"anim":1,"clip":"x"}]}}})");
	FElysiumBlendTable Parsed;
	FString Error;
	TestTrue(TEXT("the sidecar parses"), Parsed.LoadJsonText(Json, Error));
	TestEqual(TEXT("the pose parameter comes through"), Parsed.PoseParams.Num(), 1);
	TestNotNull(TEXT("the multi-cell grid is kept"), Parsed.Find(TEXT("walk")));
	TestNull(TEXT("a single-cell grid is not a blend space"), Parsed.Find(TEXT("lonely")));
	if (const FElysiumBlendGrid* Grid = Parsed.Find(TEXT("walk")))
	{
		TestEqual(TEXT("every cell is read, null included"), Grid->Cells.Num(), 3);
		const FElysiumBlendCell* Authored = Grid->CellAt(0, 0);
		if (TestNotNull(TEXT("the authored-motion cell exists"), Authored))
		{
			TestTrue(TEXT("...and its optional ground speed is read"),
				FMath::IsNearlyEqual(Authored->Motion.GroundSpeedCmPerSecond, 136.7f, 0.01f));
		}
		const FElysiumBlendCell* Null = Grid->CellAt(1, 0);
		if (TestNotNull(TEXT("the null cell exists"), Null))
		{
			TestTrue(TEXT("...and addresses no animation"), Null->Clip.IsEmpty());
			TestFalse(TEXT("...and an old/motionless cell invents no route speed"),
				Null->Motion.IsUsable());
		}
		// Case-insensitive, like every other label lookup in the module.
		TestNotNull(TEXT("labels resolve case-insensitively"), Parsed.Find(TEXT("WALK")));
	}

	// --- The speed fan (CCC7): the same grid read as per-direction speed rather than as clips ------
	// `MakeYawTable`'s fan authors motion on cell 4 alone, which is the hole case by construction: a
	// body that walked at 136.7 cm/s forward and at nothing in every other direction would stand
	// still the moment it strafed.
	FElysiumGaitSpeedTable Sparse;
	if (TestTrue(TEXT("a fan with one authored cell still yields a table"),
			ElysiumBlendGrids::SpeedFan(Walk, Table, 1.0f, Sparse)))
	{
		TestEqual(TEXT("...answering that cell where it was authored"), Sparse.Forward(), 136.7f, 0.01f);
		TestEqual(TEXT("...and filling every hole from it rather than with zero"),
			Sparse.SpeedAt(90.0f), 136.7f, 0.01f);
		TestEqual(TEXT("...including across the wrap seam"), Sparse.SpeedAt(180.0f), 136.7f, 0.01f);
	}

	// A fully authored fan, which is what a real sidecar carries. The hole fill must not touch it.
	FElysiumBlendGrid Full = Walk;
	const float Authored[9] = { 88.6f, 113.9f, 97.1f, 88.1f, 136.7f, 88.1f, 60.7f, 113.9f, 88.6f };
	for (int32 i = 0; i < 9; ++i)
	{
		Full.Cells[i].Motion.CycleSeconds = 1.0f;
		Full.Cells[i].Motion.GroundDistanceCm = Authored[i];
		Full.Cells[i].Motion.GroundSpeedCmPerSecond = Authored[i];
	}
	FElysiumGaitSpeedTable Fan;
	if (TestTrue(TEXT("a fully authored fan yields a table"),
			ElysiumBlendGrids::SpeedFan(Full, Table, 2.3f, Fan)))
	{
		TestEqual(TEXT("...cell by cell"), Fan.SpeedAt(-90.0f), 97.1f * 2.3f, 0.01f);
		TestEqual(TEXT("...with the gait's own scale applied"), Fan.Forward(), 136.7f * 2.3f, 0.01f);
		TestEqual(TEXT("...and the peak is the largest cell scaled"), Fan.Peak(), 136.7f * 2.3f, 0.01f);
	}

	// One hole in an otherwise authored fan interpolates from its two neighbours, not from the whole.
	FElysiumBlendGrid OneHole = Full;
	OneHole.Cells[5].Motion = FElysiumClipMotion();
	FElysiumGaitSpeedTable Patched;
	if (TestTrue(TEXT("one hole does not refuse the fan"),
			ElysiumBlendGrids::SpeedFan(OneHole, Table, 1.0f, Patched)))
	{
		TestEqual(TEXT("...it is filled from the cells either side"), Patched.SpeedAt(45.0f),
			0.5f * (136.7f + 60.7f), 0.01f);
		TestEqual(TEXT("...and its neighbours are untouched"), Patched.Forward(), 136.7f, 0.01f);
	}

	// The refusals. Each of these would otherwise produce a speed that is wrong rather than absent.
	FElysiumGaitSpeedTable Refused;
	FElysiumBlendGrid Motionless = Walk;
	for (FElysiumBlendCell& Blank : Motionless.Cells)
	{
		Blank.Motion = FElysiumClipMotion();
	}
	TestFalse(TEXT("a fan with no authored motion is refused"),
		ElysiumBlendGrids::SpeedFan(Motionless, Table, 1.0f, Refused));
	TestFalse(TEXT("a non-wrapping axis is not a gait fan"),
		ElysiumBlendGrids::SpeedFan(Aim, Table, 1.0f, Refused));
	FElysiumBlendGrid Sliced = Full;
	Sliced.ParamEnd[0] = 90.f;
	TestFalse(TEXT("a fan spanning part of its parameter is refused"),
		ElysiumBlendGrids::SpeedFan(Sliced, Table, 1.0f, Refused));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
