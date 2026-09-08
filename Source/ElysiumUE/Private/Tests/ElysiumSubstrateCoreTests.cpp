// Content-free Substrate automation: value types, expressions, script paths, rulebook, character sheet, queue, and clock.
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
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
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
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumDiceTables.h"
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
namespace ElysiumSubstrateCoreTests
{
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
// logic_pythoncheck's gate truthiness — the retail integer-only rule (RE C073/C079).
//
// Retail's `logic_pythoncheck` (vampire.dll FUN_10135290) decides truth by an exact
// `ob_type == PyInt_Type` + `PyInt_AsLong` test, NOT a generic `PyObject_IsTrue`. So a result
// is TRUE only when it is a Python integer with a non-zero value; any non-integer result — a
// non-empty string, a list (repr'd to a String by our marshaller), even the float `1.0` — is
// FALSE, exactly where generic truthiness (`ToBool`) would call it TRUE. Our marshaller records
// retail's comparison-produced PyInt as either Bool (a Python bool) or Int, so both count.
// `FElysiumVariant::IsPythonCheckTrue` owns the rule; this pins it and the divergence from
// `ToBool` so a later "just use ToBool" cannot regress it silently.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPythonCheckTruthinessTest,
	"Elysium.Substrate.PythonCheckTruthiness", GElysiumTestFlags)
bool FElysiumPythonCheckTruthinessTest::RunTest(const FString&)
{
	// The only TRUE cases: an integer-category value that is non-zero (retail's PyInt path).
	TestTrue(TEXT("Int(1) gates TRUE"), FElysiumVariant::Int(1).IsPythonCheckTrue());
	TestTrue(TEXT("Int(-1) gates TRUE"), FElysiumVariant::Int(-1).IsPythonCheckTrue());
	TestTrue(TEXT("Bool(true) gates TRUE (retail comparison PyInt 1)"),
		FElysiumVariant::Bool(true).IsPythonCheckTrue());

	// Zero integers are FALSE (both category members).
	TestFalse(TEXT("Int(0) gates FALSE"), FElysiumVariant::Int(0).IsPythonCheckTrue());
	TestFalse(TEXT("Bool(false) gates FALSE"), FElysiumVariant::Bool(false).IsPythonCheckTrue());

	// Void (no host / error-to-false / evaluated to None) is FALSE.
	TestFalse(TEXT("Void gates FALSE"), FElysiumVariant::Void().IsPythonCheckTrue());

	// The divergence from generic truthiness: every non-integer result is FALSE under the retail
	// gate even though ToBool would call it TRUE. Float 1.0 is the canonical trap.
	const FElysiumVariant FloatOne = FElysiumVariant::Float(1.0f);
	TestFalse(TEXT("Float(1.0) gates FALSE (non-integer)"), FloatOne.IsPythonCheckTrue());
	TestTrue(TEXT("...but ToBool would call Float(1.0) TRUE"), FloatOne.ToBool());

	const FElysiumVariant NonEmptyStr = FElysiumVariant::String(TEXT("open"));
	TestFalse(TEXT("non-empty String gates FALSE"), NonEmptyStr.IsPythonCheckTrue());
	TestTrue(TEXT("...but ToBool would call a non-empty String TRUE"), NonEmptyStr.ToBool());

	// A Python list result marshals to a repr String — still a non-integer, still FALSE.
	const FElysiumVariant ListRepr = FElysiumVariant::String(TEXT("[1, 2, 3]"));
	TestFalse(TEXT("list-repr String gates FALSE"), ListRepr.IsPythonCheckTrue());

	// Empty string is FALSE under both rules (agreement case, kept as a guard).
	TestFalse(TEXT("empty String gates FALSE"), FElysiumVariant::String(TEXT("")).IsPythonCheckTrue());

	const FElysiumVariant NonZeroVec = FElysiumVariant::Vector(FVector(1, 0, 0));
	TestFalse(TEXT("non-zero Vector gates FALSE"), NonZeroVec.IsPythonCheckTrue());
	TestTrue(TEXT("...but ToBool would call a non-zero Vector TRUE"), NonZeroVec.ToBool());

	const FElysiumVariant SetHandle = FElysiumVariant::Handle(FElysiumEntityHandle(0, 1));
	TestFalse(TEXT("bound Handle gates FALSE"), SetHandle.IsPythonCheckTrue());
	TestTrue(TEXT("...but ToBool would call a bound Handle TRUE"), SetHandle.ToBool());

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
	TestTrue(TEXT("vdata maps under the corpus"), Real.Replace(TEXT("\\"), TEXT("/"))
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
// The rulebook's pure parsers — the four grammars VtMB authors inside its `vdata/`
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
// 9.6 — the World-of-Darkness d10 resolver, content-free.
//
// Most of it is asserted with no RNG at all: a weighting table IS the die, so a table whose every
// entry is one face turns the roller into a known answer and pins the exploding 10, the two 250
// caps, the botch branch and the difficulty compare exactly. The one genuinely random case is
// cross-checked against the recovered algorithm applied by hand to the same stream's RAW draws,
// which is what makes a wrong table lookup or a swapped branch visible rather than merely different.
//
// The consumer boundary this deliberately stops at:
// `docs/vtmb/skills-and-checks.md`.
// =====================================================================================

namespace
{
	// A weighting table whose every raw draw maps to one face — the die a modder could author, and
	// the only way to interrogate the roller without asking what the RNG did.
	FElysiumDiceTable ElysiumTestDieShowing(int32 PhysicalFace)
	{
		FElysiumDiceTable Table;
		Table.InternalName = FString::Printf(TEXT("always%d"), PhysicalFace);
		for (int32 i = 0; i < FElysiumDiceTable::NumEntries; ++i)
		{
			Table.Faces[i] = PhysicalFace - 1;
		}
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDiceTest, "Elysium.Substrate.Dice", GElysiumTestFlags)
bool FElysiumDiceTest::RunTest(const FString&)
{
	// --- The uniform fallback is the shipped distribution ---------------------------------------
	const FElysiumDiceTable& Uniform = FElysiumDiceTable::Uniform();
	TestTrue(TEXT("the fallback table reports itself uniform"), Uniform.IsUniform());
	TestEqual(TEXT("draw 0 is face 0"), Uniform.Face(0), 0);
	TestEqual(TEXT("draw 9 is still face 0"), Uniform.Face(9), 0);
	TestEqual(TEXT("draw 10 is face 1"), Uniform.Face(10), 1);
	TestEqual(TEXT("draw 99 is face 9"), Uniform.Face(99), 9);
	TestEqual(TEXT("an out-of-range draw clamps rather than reads off the end"), Uniform.Face(500), 9);

	// --- An absent entry reads as face 1, not 0 -------------------------------------------------
	// The loader's own `GetInt(key, 1)` default. Face 0 is the botch, so a reader defaulting to 0
	// would turn a mod's unauthored entries into botches instead of 2s.
	{
		const TSharedPtr<ElysiumKeyValues::FKvNode> Node = ElysiumKeyValues::ParseText(
			TEXT("Sparse { \"Name\" \"Sparse\" \"0\" \"9\" \"5\" \"3\" }"));
		if (TestTrue(TEXT("the sparse table text parses"), Node.IsValid()))
		{
			FElysiumDiceTable Sparse;
			Sparse.Load(*Node->Child(TEXT("Sparse")));
			TestEqual(TEXT("an authored entry wins"), Sparse.Face(0), 9);
			TestEqual(TEXT("  and another"), Sparse.Face(5), 3);
			TestEqual(TEXT("an unauthored entry defaults to face 1"), Sparse.Face(42), 1);
			TestEqual(TEXT("the block's Name is read"), Sparse.Name, FString(TEXT("Sparse")));
			TestFalse(TEXT("and it is not uniform"), Sparse.IsUniform());
		}
	}

	// --- The tier bands, every edge -------------------------------------------------------------
	auto Tier = [](int32 S, int32 B) { return (int32)ElysiumDice::TierFor(S, B); };
	TestEqual(TEXT("net 0 is a failure"), Tier(0, 0), (int32)EElysiumRollTier::Failure);
	TestEqual(TEXT("net 1 is a partial success"), Tier(1, 0), (int32)EElysiumRollTier::PartialSuccess);
	TestEqual(TEXT("net 2 is still partial"), Tier(2, 0), (int32)EElysiumRollTier::PartialSuccess);
	TestEqual(TEXT("net 3 is a success"), Tier(3, 0), (int32)EElysiumRollTier::Success);
	TestEqual(TEXT("net 4 is still a success"), Tier(4, 0), (int32)EElysiumRollTier::Success);
	TestEqual(TEXT("net 5 is critical"), Tier(5, 0), (int32)EElysiumRollTier::CriticalSuccess);
	TestEqual(TEXT("net 6 is critical"), Tier(6, 0), (int32)EElysiumRollTier::CriticalSuccess);
	// The two ways a roll can end up at or below zero are NOT the same verdict.
	TestEqual(TEXT("a negative net with no raw success botches"), Tier(0, 1),
		(int32)EElysiumRollTier::Botched);
	TestEqual(TEXT("a negative net WITH a raw success only fails"), Tier(1, 2),
		(int32)EElysiumRollTier::Failure);
	TestEqual(TEXT("successes cancelling botches exactly is a failure, not a botch"), Tier(2, 2),
		(int32)EElysiumRollTier::Failure);
	TestEqual(TEXT("net 5 through botches is still critical"), Tier(7, 2),
		(int32)EElysiumRollTier::CriticalSuccess);

	// --- An empty pool never reaches the loop ---------------------------------------------------
	{
		const FElysiumRollResult Empty = ElysiumDice::Roll(0, 6, Uniform);
		TestEqual(TEXT("an empty pool fails"), (int32)Empty.Tier, (int32)EElysiumRollTier::Failure);
		TestEqual(TEXT("  having rolled nothing"), Empty.Successes, 0);

		// The successes counter is SEEDED, so the automatic successes survive the early-out even
		// though the tier stays the constructor's default failure.
		const FElysiumRollResult Seeded = ElysiumDice::Roll(0, 6, Uniform, /*Automatic*/ 4);
		TestEqual(TEXT("the automatic successes survive the early-out"), Seeded.Successes, 4);
		TestEqual(TEXT("  and are the net"), Seeded.Net, 4);
		TestEqual(TEXT("  but the tier is still failure"), (int32)Seeded.Tier,
			(int32)EElysiumRollTier::Failure);
	}

	// --- A die that always shows 5: the difficulty compare, with no RNG in the answer ------------
	{
		const FElysiumDiceTable Fives = ElysiumTestDieShowing(5);
		const FElysiumRollResult Meets = ElysiumDice::Roll(5, 5, Fives);
		TestEqual(TEXT("a 5 meets difficulty 5"), Meets.Successes, 5);
		TestEqual(TEXT("  with no botches"), Meets.Botches, 0);
		TestEqual(TEXT("  net 5"), Meets.Net, 5);
		TestEqual(TEXT("  critical"), (int32)Meets.Tier, (int32)EElysiumRollTier::CriticalSuccess);

		// One higher and the same die misses — this is the off-by-one the engine's stored
		// `difficulty - 1` hides. A port comparing face(0..9) against a RAW difficulty would pass 5
		// dice here.
		const FElysiumRollResult Misses = ElysiumDice::Roll(5, 6, Fives);
		TestEqual(TEXT("a 5 misses difficulty 6"), Misses.Successes, 0);
		TestEqual(TEXT("  and does not botch either"), Misses.Botches, 0);
		TestEqual(TEXT("  so the roll fails"), (int32)Misses.Tier, (int32)EElysiumRollTier::Failure);

		// Automatic successes are added to a real roll, and they tier with it.
		const FElysiumRollResult Auto = ElysiumDice::Roll(3, 6, Fives, /*Automatic*/ 3);
		TestEqual(TEXT("automatic successes count on a failed pool"), Auto.Successes, 3);
		TestEqual(TEXT("  and tier"), (int32)Auto.Tier, (int32)EElysiumRollTier::Success);

		// The wound penalty comes off the pool the roller walks.
		TestEqual(TEXT("a health penalty removes dice from the pool"),
			ElysiumDice::Roll(5, 5, Fives, /*Automatic*/ 0, /*HealthPenalty*/ 2).Successes, 3);
		TestEqual(TEXT("a health penalty that empties the pool fails outright"),
			(int32)ElysiumDice::Roll(5, 5, Fives, 0, 5).Tier, (int32)EElysiumRollTier::Failure);

		// The initial-pool clamp: 1000 dice are 250.
		TestEqual(TEXT("the pool is clamped at 250"),
			ElysiumDice::Roll(1000, 5, Fives).Successes, ElysiumDice::MaxPool);
	}

	// --- A die that always shows 1: the botch branch precedes the difficulty compare -------------
	{
		const FElysiumDiceTable Ones = ElysiumTestDieShowing(1);
		// Difficulty 1 would otherwise admit every face, which is exactly why the branch order is
		// asserted at difficulty 1.
		const FElysiumRollResult Botch = ElysiumDice::Roll(5, 1, Ones);
		TestEqual(TEXT("a 1 is never a success, even at difficulty 1"), Botch.Successes, 0);
		TestEqual(TEXT("  every die botches"), Botch.Botches, 5);
		TestEqual(TEXT("  net -5"), Botch.Net, -5);
		TestEqual(TEXT("  botched"), (int32)Botch.Tier, (int32)EElysiumRollTier::Botched);
	}

	// --- A die that always shows 10: 10-again, and the roll cap that bounds it -------------------
	{
		const FElysiumDiceTable Tens = ElysiumTestDieShowing(10);
		// A 10 succeeds and puts a die back, so one die explodes forever; the roller's SEPARATE cap
		// on total rolls is the only thing that ends it. Difficulty 10 also proves the 10 is tested
		// before the difficulty compare.
		const FElysiumRollResult Explode = ElysiumDice::Roll(1, 10, Tens);
		TestEqual(TEXT("one exploding die runs to the roll cap"), Explode.Successes,
			ElysiumDice::MaxRolls);
		TestEqual(TEXT("  every one of them a ten"), Explode.Tens, ElysiumDice::MaxRolls);
		TestEqual(TEXT("  with no botches"), Explode.Botches, 0);
		TestEqual(TEXT("  critical"), (int32)Explode.Tier, (int32)EElysiumRollTier::CriticalSuccess);
	}

	// --- The uniform table over the owned stream, against the algorithm applied by hand ----------
	{
		constexpr int32 Seed = 20250606;
		constexpr int32 Pool = 8;
		constexpr int32 Difficulty = 6;

		// Harvest the RAW draws this seed produces, then walk them with the recovered algorithm.
		// Reading raw draws rather than faces is what makes a wrong table lookup visible.
		ElysiumRng::SeedAll(Seed);
		TArray<int32> Draws;
		for (int32 i = 0; i < ElysiumDice::MaxRolls; ++i)
		{
			Draws.Add(ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 99));
		}

		int32 ExpectSuccesses = 0, ExpectBotches = 0, ExpectTens = 0, DiceLeft = Pool;
		for (int32 i = 0; DiceLeft >= 1 && i < Draws.Num(); ++i)
		{
			const int32 Face = Draws[i] / 10;      // the uniform weighting, by hand
			if (Face == 9)      { ++ExpectSuccesses; ++ExpectTens; ++DiceLeft; }
			else if (Face == 0) { ++ExpectBotches; }
			else if (Face >= Difficulty - 1) { ++ExpectSuccesses; }
			--DiceLeft;
		}
		TestTrue(TEXT("the harvested reference roll is not vacuous"),
			ExpectSuccesses + ExpectBotches > 0);

		ElysiumRng::SeedAll(Seed);
		const FElysiumRollResult Rolled = ElysiumDice::Roll(Pool, Difficulty, Uniform);
		TestEqual(TEXT("the resolver's successes match the hand-walked draws"),
			Rolled.Successes, ExpectSuccesses);
		TestEqual(TEXT("  its botches"), Rolled.Botches, ExpectBotches);
		TestEqual(TEXT("  its tens"), Rolled.Tens, ExpectTens);
		TestEqual(TEXT("  its net"), Rolled.Net, ExpectSuccesses - ExpectBotches);
		TestEqual(TEXT("  and its tier"), (int32)Rolled.Tier,
			(int32)ElysiumDice::TierFor(ExpectSuccesses, ExpectBotches));

		// Determinism: the same seed is the same run.
		ElysiumRng::SeedAll(Seed);
		const FElysiumRollResult Again = ElysiumDice::Roll(Pool, Difficulty, Uniform);
		TestEqual(TEXT("the same seed rolls the same result"), Again.Successes, Rolled.Successes);
		TestEqual(TEXT("  down to the botches"), Again.Botches, Rolled.Botches);
	}

	// --- A restored stream continues the sequence (S8) -------------------------------------------
	// Through `ElysiumRng`'s own API, because the save restores by re-initialising to the CURRENT
	// position rather than replaying from the initial seed.
	{
		ElysiumRng::SeedAll(99);
		ElysiumDice::Roll(6, 6, FElysiumDiceTable::Uniform());
		TArray<ElysiumRng::FState> State;
		ElysiumRng::Snapshot(State);

		const FElysiumRollResult Next = ElysiumDice::Roll(6, 6, FElysiumDiceTable::Uniform());
		ElysiumDice::Roll(6, 6, FElysiumDiceTable::Uniform());   // walk further off
		ElysiumRng::Restore(State);

		const FElysiumRollResult Resumed = ElysiumDice::Roll(6, 6, FElysiumDiceTable::Uniform());
		TestEqual(TEXT("a restored stream rolls what the saved run would have"),
			Resumed.Successes, Next.Successes);
		TestEqual(TEXT("  and the same botches"), Resumed.Botches, Next.Botches);
	}

	// --- The feat -> weighting join, and the engine's fallback to index 0 ------------------------
	{
		// Two distinguishable tables, so "resolved by name" and "fell back to index 0" cannot be
		// confused for each other.
		FElysiumDiceTables Tables;
		FElysiumDiceTable Normal = ElysiumTestDieShowing(5);
		Normal.InternalName = TEXT("Normal");
		FElysiumDiceTable Heavy = ElysiumTestDieShowing(10);
		Heavy.InternalName = TEXT("Heavy");
		Tables.Tables.Add(MoveTemp(Normal));
		Tables.Tables.Add(MoveTemp(Heavy));
		Tables.HealthModifiers = { 0, 0, 1, 2 };
		Tables.Reindex();

		TestEqual(TEXT("a table resolves by name"), Tables.Find(TEXT("Heavy")).Face(0), 9);
		TestEqual(TEXT("  case-insensitively"), Tables.Find(TEXT("hEaVy")).Face(0), 9);
		TestEqual(TEXT("the wound penalty reads by health level"), Tables.HealthModifier(3), 2);
		TestEqual(TEXT("an unauthored health level costs nothing"), Tables.HealthModifier(99), 0);

		FElysiumFeat Feat;
		Feat.InternalName = TEXT("Intrusion");
		Feat.PcWeighting = TEXT("Heavy");
		Feat.NpcWeighting = TEXT("Nonexistent");
		TestEqual(TEXT("a feat's PC weighting resolves to its named table"),
			Tables.ForFeat(Feat, /*bNpc*/ false).Face(0), 9);
		TestEqual(TEXT("an unmatched weighting name falls back to index 0"),
			Tables.ForFeat(Feat, /*bNpc*/ true).Face(0), 4);
	}
	{
		// With nothing loaded at all, every lookup is the uniform d10 — the fail-open contract.
		const FElysiumDiceTables Absent;
		TestFalse(TEXT("an unloaded dice table is not valid"), Absent.IsValid());
		TestTrue(TEXT("but it still answers with the uniform d10"),
			Absent.Find(TEXT("Normal")).IsUniform());
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
// The sheet's arithmetic, content-free: the write gates, the feat evaluator, the
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
// The one clock and the one pause/time-scale facade over it.
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

	// Scale is applied EXACTLY ONCE. Engine dilation has already
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

} // namespace ElysiumSubstrateCoreTests

#endif // WITH_DEV_AUTOMATION_TESTS
