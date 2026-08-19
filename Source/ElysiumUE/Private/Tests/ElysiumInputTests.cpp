// Content-free Substrate automation: commands, user-command shaping, look input, diagnostics channels, app state, and input scopes.
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
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
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
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
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
namespace ElysiumInputTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

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

	// user.cfg is personal installer state, not the project's content profile. A source install
	// configured as Basic must not change Elysium's owner-called Plus default.
	const FString CfgDir = FPaths::AutomationTransientDir()
		/ FString::Printf(TEXT("ElysiumConsoleProfile-%s"), *FGuid::NewGuid().ToString());
	IFileManager::Get().MakeDirectory(*CfgDir, /*Tree*/ true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*CfgDir, /*RequireExists*/ false, /*Tree*/ true);
	};
	TestTrue(TEXT("synthetic Basic user.cfg writes"), FFileHelper::SaveStringToFile(
		TEXT("// Basic user.cfg\nalias patchtype \"setBasic()\"\n"), *(CfgDir / TEXT("user.cfg"))));
	FElysiumConsole Profile;
	Profile.LoadFromCfgDir(CfgDir);
	const FString* PatchType = Profile.FindAlias(TEXT("patchtype"));
	if (TestNotNull(TEXT("the profile alias exists"), PatchType))
	{
		TestEqual(TEXT("Elysium pins the Plus profile"), *PatchType, FString(TEXT("setPlus()")));
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerViewModelsTest,
	"Elysium.Substrate.PlayerViewModels", GElysiumTestFlags)
bool FElysiumPlayerViewModelsTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test_player_viewmodels__");
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();

	TArray<FElysiumEntity*> ViewModels;
	for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
	{
		if (Entity && Entity->Def
			&& Entity->Def->Classname.Equals(
				ElysiumViewModelClassName().ToString(), ESearchCase::IgnoreCase))
		{
			ViewModels.Add(Entity.Get());
		}
	}
	TestEqual(TEXT("the player owns four script-addressable viewmodel slots"),
		ViewModels.Num(), ElysiumViewModelSlotCount);
	if (ViewModels.Num() == ElysiumViewModelSlotCount)
	{
		ViewModels[3]->SetRuntimeModel(TEXT("models/hands/male/tremere/v_tremere_male_hands.mdl"));
		TestEqual(TEXT("patch Python can write the fourth slot's model"), ViewModels[3]->Model,
			FString(TEXT("models/hands/male/tremere/v_tremere_male_hands.mdl")));
	}
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

	// --- command publication follows the resolved contexts, including retained body state ---
	{
		FElysiumUserCmd Walking;
		Walking.Seq = 17;
		Walking.DeltaSeconds = 1.0f / 60.0f;
		Walking.Move = FVector2D(1.0, -0.5);
		Walking.Up = 0.25f;
		Walking.LookDelta = FVector2D(4.0, -2.0);
		Walking.Buttons = static_cast<uint64>(EElysiumButton::Jump)
			| static_cast<uint64>(EElysiumButton::Use);

		FElysiumInputScopeStack GameStack;
		const FElysiumUserCmd Gameplay =
			ElysiumInput::GateGameplayCommand(GameStack.Resolve(), Walking);
		TestTrue(TEXT("game contexts preserve the complete command"), Gameplay.SameIntent(Walking));

		FElysiumInputScopeStack DialogueStack;
		DialogueStack.Push(Dialogue);
		const FElysiumUserCmd Gated =
			ElysiumInput::GateGameplayCommand(DialogueStack.Resolve(), Walking);
		TestTrue(TEXT("dialogue clears movement"), Gated.Move.IsNearlyZero());
		TestTrue(TEXT("dialogue clears vertical movement"), FMath::IsNearlyZero(Gated.Up));
		TestTrue(TEXT("dialogue clears look"), Gated.LookDelta.IsNearlyZero());
		TestEqual(TEXT("dialogue clears buttons"), Gated.Buttons, uint64(0));
		TestEqual(TEXT("gating preserves sequence identity"), Gated.Seq, Walking.Seq);
		TestEqual(TEXT("gating preserves frame timing"), Gated.DeltaSeconds, Walking.DeltaSeconds);

		FElysiumInputScopeStack DebugStack;
		DebugStack.Push(Debug);
		const FElysiumUserCmd DebugCommand =
			ElysiumInput::GateGameplayCommand(DebugStack.Resolve(), Walking);
		TestTrue(TEXT("debug contexts retain gameplay commands"), DebugCommand.SameIntent(Walking));
	}

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

} // namespace ElysiumInputTests

#endif // WITH_DEV_AUTOMATION_TESTS
