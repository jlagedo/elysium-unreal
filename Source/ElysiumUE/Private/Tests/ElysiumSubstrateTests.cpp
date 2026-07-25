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

#include "ElysiumClassRegistry.h"
#include "ElysiumConsole.h"
#include "ElysiumDecals.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEventQueue.h"
#include "ElysiumExpr.h"
#include "ElysiumKeyValues.h"
#include "ElysiumObjModel.h"
#include "ElysiumPythonVM.h"
#include "ElysiumRopes.h"
#include "ElysiumScriptHost.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"

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

	// An alias body that is an engine command (`-speed;`) expands to one non-empty statement and
	// falls through; the sink reporting "not Python" is how such commands are dropped.
	Fell.Reset();
	C.SetPythonSink([&Fell](const FString& Line) { Fell.Add(Line); return false; });
	C.Execute(TEXT("run"));
	TestEqual(TEXT("run expands to one non-empty statement"), Fell.Num(), 1);
	if (Fell.Num() == 1)
	{
		TestEqual(TEXT("engine command reached the sink"), Fell[0], FString(TEXT("-speed")));
	}

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
		"    \"Block\"\n"
		"    {\n"
		"        \"inner\" \"1\"\n"
		"    }\n"
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

	// Nested block and its typed read.
	const ElysiumKeyValues::FKvNode* Inner = RootBlock->Child(TEXT("Block"));
	if (TestNotNull(TEXT("nested Block present"), Inner))
	{
		TestEqual(TEXT("nested int"), Inner->Int(TEXT("inner"), 0), 1);
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
// FElysiumDecals — the `.decals` projector sidecar parser + the orientation contract the
// map actor builds each UDecalComponent from (7.2). Pure data + math, no RHI.
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

	// --- orientation: BuildDecals rotates each decal by MakeFromXZ(Normal, SDir). A deferred decal
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

	TMap<FString, FElysiumMaterialDef> Mats;
	FElysiumObjModel::ParseMtlLines(Lines, Mats);
	TestEqual(TEXT("five materials parsed"), Mats.Num(), 5);

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

	return true;
}
#endif // ELYSIUM_WITH_CPYTHON

#endif // WITH_DEV_AUTOMATION_TESTS
