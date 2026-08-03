// The facial flex chain (roadmap 12.3). Two tiers, because the chain has two halves.
//
// The arithmetic — the RPN rule machine, the eyelid lid combine, and the four-value target ramp —
// is content-free and runs against a hand-written rig with hand-computed expectations. It is the
// half a bad edit breaks silently: every layer produces a plausible-looking float, and a wrong one
// reads as a modelling fault rather than a maths fault.
//
// The contracts the bake depends on need the real export, so they sit in the content tier and
// self-skip on an empty $ELYSIUM_EXPORT_ROOT: that a rigged model's morph targets come back merged
// across the material primitives they span, that their names survive uniquely, that the skeleton
// declares them as morph-target curves, and that with every controller at rest the whole cast's
// morph weights are exactly zero.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraSolve.h"   // FElysiumCameraShot, a by-value member of the recording services
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumSceneData.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimCurveMetadata.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumFacialTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A minimal rig in the exported sidecar's own shape, so the parse is under test alongside the
	// arithmetic. It reproduces the three structures the shipped cast actually uses:
	//
	//  - the eyelid trio (`lid_lowerer`/`lid_neutral`/`lid_raiser`) driving the unruled lid-value
	//    flexdesc `lid`, whose two ramps hinge at 0.208 — VtMB's own `upper_right` numbers;
	//  - a FETCH2 chain, `plain` <- `suppressor` <- `open`, which only resolves if rules are run in
	//    file order against the flexdesc array they are filling;
	//  - a DIV whose divisor is zero at rest, which is a shipped rule (`1 / right_open`);
	//  - the amplitude jaw's shape: a `mouth` flexdesc that a rule computes and no morph target reads,
	//    exactly as 85 of the 86 exported rigs carry it, beside a `jaw_drop` controller whose morph is
	//    the thing that actually moves when the jaw opens.
	//
	// `wide` carries a 0..2 range so controller normalization is covered; every shipped VtMB
	// controller is 0..1, where normalization is the identity.
	const TCHAR* const GTestRigJson = TEXT(R"JSON(
	{
	  "stem": "testrig",
	  "flexdescs": ["lid","lid_lowerer","lid_neutral","lid_raiser","open","suppressor","plain","mouth","jaw"],
	  "controllers": [
	    {"name":"blink",    "type":"eyelid", "min":0.0,"max":1.0},
	    {"name":"lid_up",   "type":"eyelid", "min":0.0,"max":1.0},
	    {"name":"droop",    "type":"eyelid", "min":0.0,"max":1.0},
	    {"name":"wide",     "type":"mouth",  "min":0.0,"max":2.0},
	    {"name":"jaw_drop", "type":"phoneme","min":0.0,"max":1.0}
	  ],
	  "rules": [
	    {"flexdesc":1,"ops":[["FETCH1",0]]},
	    {"flexdesc":2,"ops":[["CONST",1.0],["FETCH1",2],["CONST",0.8],["MUL"],["SUB"],
	                         ["CONST",1.0],["FETCH1",1],["SUB"],
	                         ["CONST",1.0],["FETCH1",0],["SUB"],["MUL"],["MUL"]]},
	    {"flexdesc":3,"ops":[["FETCH1",1],
	                         ["CONST",1.0],["FETCH1",2],["CONST",0.8],["MUL"],["SUB"],
	                         ["CONST",1.0],["FETCH1",0],["SUB"],["MUL"],["MUL"]]},
	    {"flexdesc":4,"ops":[["FETCH1",3]]},
	    {"flexdesc":5,"ops":[["CONST",1.0],["FETCH2",4],["DIV"]]},
	    {"flexdesc":6,"ops":[["FETCH1",1],["FETCH2",5],["MUL"]]},
	    {"flexdesc":8,"ops":[["FETCH1",4]]},
	    {"flexdesc":7,"ops":[["FETCH2",8],["CONST",0.5],["MUL"]]}
	  ],
	  "mouths": [{"bone":6,"forward":[0.0,-1.0,0.0],"flexdesc":7}],
	  "morphs": [
	    {"name":"lid",     "flexdesc":0,"targets":[-11.0,-10.0,-0.16,0.208]},
	    {"name":"lid#1",   "flexdesc":0,"targets":[0.208,0.298,10.0,11.0]},
	    {"name":"plain",   "flexdesc":6,"targets":[0.0,1.0,10.0,11.0]},
	    {"name":"open",    "flexdesc":4,"targets":[0.0,0.25,0.25,1.0]},
	    {"name":"open#1",  "flexdesc":4,"targets":[0.25,1.0,10.0,11.0]},
	    {"name":"jaw",     "flexdesc":8,"targets":[0.0,1.0,10.0,11.0]}
	  ]
	}
	)JSON");

	// Controller/flexdesc/morph indices in the rig above, so the assertions read as names.
	enum { CtlBlink = 0, CtlLidUp = 1, CtlDroop = 2, CtlWide = 3, CtlJawDrop = 4 };
	enum { FdLid = 0, FdLowerer = 1, FdNeutral = 2, FdRaiser = 3, FdOpen = 4, FdSuppressor = 5,
	       FdPlain = 6, FdMouth = 7, FdJaw = 8 };
	enum { MorphLid = 0, MorphLidHigh = 1, MorphPlain = 2, MorphOpen = 3, MorphOpenHigh = 4, MorphJaw = 5 };

	// Drive the rig from a set of raw controller writes, normalizing each the way the anim instance
	// does, and return both derived layers.
	void Drive(const FElysiumFacialRig& Rig, const TMap<int32, float>& Writes,
		TArray<float>& OutFlex, TArray<float>& OutMorph)
	{
		TArray<float> Controllers;
		Controllers.AddZeroed(Rig.Controllers.Num());
		for (const TPair<int32, float>& Write : Writes)
		{
			Controllers[Write.Key] = Rig.Controllers[Write.Key].Normalize(Write.Value);
		}
		Rig.Evaluate(Controllers, OutFlex, OutMorph);
	}

	// The first exported NPC that carries a facial sidecar and a glb on disk, preferring `nines`
	// because its rig is the one `docs/vtmb/facial_animation.md` works through.
	FString FirstRiggedStem(const FElysiumNpcIndex& Index)
	{
		FString Best;
		for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
		{
			if (Pair.Value.Facial.IsEmpty()
				|| !IFileManager::Get().FileExists(*FElysiumContentPaths::NpcFacial(Pair.Value.Facial))
				|| !IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(Pair.Key)))
			{
				continue;
			}
			if (Pair.Key.Equals(TEXT("nines")))
			{
				return Pair.Key;
			}
			if (Best.IsEmpty() || Pair.Key < Best)
			{
				Best = Pair.Key;   // stable, so a failure names the same model on every machine
			}
		}
		return Best;
	}
}

// =====================================================================================
// The target ramp — `R_StudioFlexVerts`' trapezoid, against hand-computed values.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFlexRampTest, "Elysium.Substrate.FlexRamp", GElysiumFacialTestFlags)
bool FElysiumFlexRampTest::RunTest(const FString&)
{
	// `(0, 1, 10, 11)` — the ordinary "ramp in over 0..1, then hold" shape 45 of the 53 shipped
	// morphs carry.
	const float Plain[4] = { 0.f, 1.f, 10.f, 11.f };
	TestEqual(TEXT("plain at 0 is off (w <= Target0)"), FElysiumFacialRig::RampWeight(Plain, 0.f), 0.f);
	TestEqual(TEXT("plain below Target0 is off"), FElysiumFacialRig::RampWeight(Plain, -1.f), 0.f);
	TestEqual(TEXT("plain ramps in linearly"), FElysiumFacialRig::RampWeight(Plain, 0.25f), 0.25f);
	TestEqual(TEXT("plain is full at Target1"), FElysiumFacialRig::RampWeight(Plain, 1.f), 1.f);
	TestEqual(TEXT("plain holds through the plateau"), FElysiumFacialRig::RampWeight(Plain, 5.f), 1.f);
	TestEqual(TEXT("plain ramps out past Target2"), FElysiumFacialRig::RampWeight(Plain, 10.5f), 0.5f);
	TestEqual(TEXT("plain at Target3 is off"), FElysiumFacialRig::RampWeight(Plain, 11.f), 0.f);

	// `(0, 0.25, 0.25, 1)` + `(0.25, 1, 10, 11)` — one flexdesc hinged into two morphs at 0.25, the
	// `AU12R` shape. The two halves sum to 1 anywhere in the crossfade, so the pair reads as a swap
	// between two authored shapes rather than a dip through the base mesh.
	const float Low[4]  = { 0.f, 0.25f, 0.25f, 1.f };
	const float High[4] = { 0.25f, 1.f, 10.f, 11.f };
	TestEqual(TEXT("low half ramps in"), FElysiumFacialRig::RampWeight(Low, 0.1f), 0.4f);
	TestEqual(TEXT("low half peaks at the hinge"), FElysiumFacialRig::RampWeight(Low, 0.25f), 1.f);
	TestEqual(TEXT("high half is off at the hinge"), FElysiumFacialRig::RampWeight(High, 0.25f), 0.f);
	TestEqual(TEXT("low half ramps out"), FElysiumFacialRig::RampWeight(Low, 0.5f), 0.5f / 0.75f);
	TestEqual(TEXT("high half ramps in"), FElysiumFacialRig::RampWeight(High, 0.5f), 0.25f / 0.75f);
	TestEqual(TEXT("the two halves sum to one"),
		FElysiumFacialRig::RampWeight(Low, 0.5f) + FElysiumFacialRig::RampWeight(High, 0.5f), 1.f);

	// The eyelid sentinel pair: `(-11, -10, a, b)` and `(b, c, 10, 11)`, VtMB's own `upper_right`
	// angles. Everything below the lowered angle is fully lowered, everything above the raised angle
	// fully raised, and the hinge between them is the neutral lid.
	const float LidLow[4]  = { -11.f, -10.f, -0.16f, 0.208f };
	const float LidHigh[4] = { 0.208f, 0.298f, 10.f, 11.f };
	TestEqual(TEXT("lid low is full at the lowered angle"), FElysiumFacialRig::RampWeight(LidLow, -0.16f), 1.f);
	TestEqual(TEXT("lid low is full below it"), FElysiumFacialRig::RampWeight(LidLow, -5.f), 1.f);
	TestEqual(TEXT("lid low is off at the hinge"), FElysiumFacialRig::RampWeight(LidLow, 0.208f), 0.f);
	TestEqual(TEXT("lid high is off at the hinge"), FElysiumFacialRig::RampWeight(LidHigh, 0.208f), 0.f);
	TestEqual(TEXT("lid high is full at the raised angle"), FElysiumFacialRig::RampWeight(LidHigh, 0.298f), 1.f);
	// A lid value of 0 is *not* neutral: it sits 0.208 rad below the hinge, which is why an unruled
	// lid flexdesc left at zero would show a half-lowered lid on a resting face.
	TestEqual(TEXT("lid low at zero is off-neutral"),
		FElysiumFacialRig::RampWeight(LidLow, 0.f), 0.208f / 0.368f);

	return true;
}

// =====================================================================================
// The rule machine and the lid combine, over the hand-written rig.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFlexRulesTest, "Elysium.Substrate.FlexRules", GElysiumFacialTestFlags)
bool FElysiumFlexRulesTest::RunTest(const FString&)
{
	FElysiumFacialRig Rig;
	FString Error;
	if (!TestTrue(TEXT("the test rig parses"), Rig.LoadJsonText(GTestRigJson, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("controllers"), Rig.Controllers.Num(), 5);
	TestEqual(TEXT("rules"), Rig.Rules.Num(), 8);
	TestEqual(TEXT("morphs"), Rig.Morphs.Num(), 6);
	TestEqual(TEXT("controller lookup is case-insensitive"), Rig.FindController(TEXT("BLINK")), (int32)CtlBlink);
	TestEqual(TEXT("an absent controller is INDEX_NONE"), Rig.FindController(TEXT("nostril")), INDEX_NONE);
	TestEqual(TEXT("the mouth flexdesc is read"), Rig.Mouth.FlexDesc, (int32)FdMouth);
	TestEqual(TEXT("the jaw bridge resolves to jaw_drop"), Rig.MouthBridge, (int32)CtlJawDrop);

	// The lid combine is derived, not authored: `lid` is the only flexdesc that deforms the mesh,
	// carries two hinged ramps, has no rule, and names all three of its lid sources. `open` hinges
	// two ramps too, but a rule computes it, so it is not a lid.
	if (TestEqual(TEXT("exactly one lid is reconstructed"), Rig.Lids.Num(), 1))
	{
		const FElysiumFlexLid& Lid = Rig.Lids[0];
		TestEqual(TEXT("lid flexdesc"), Lid.FlexDesc, (int32)FdLid);
		TestEqual(TEXT("lid lowerer source"), Lid.Lowerer, (int32)FdLowerer);
		TestEqual(TEXT("lid neutral source"), Lid.Neutral, (int32)FdNeutral);
		TestEqual(TEXT("lid raiser source"), Lid.Raiser, (int32)FdRaiser);
		TestEqual(TEXT("lowered angle is the low ramp's Target2"), Lid.LoweredAngle, -0.16f);
		TestEqual(TEXT("neutral angle is the hinge"), Lid.NeutralAngle, 0.208f);
		TestEqual(TEXT("raised angle is the high ramp's Target1"), Lid.RaisedAngle, 0.298f);
	}

	TArray<float> Flex, Morph;

	// --- rest: every controller at zero -----------------------------------------------------
	// The neutral rule resolves to 1 and the other two to 0, so the lid sits exactly on its hinge
	// and both of its ramps are off. This is the invariant that makes a resting face the authored
	// mesh: not one morph target carries weight.
	Drive(Rig, {}, Flex, Morph);
	TestEqual(TEXT("rest: lowerer"), Flex[FdLowerer], 0.f);
	TestEqual(TEXT("rest: neutral"), Flex[FdNeutral], 1.f);
	TestEqual(TEXT("rest: raiser"), Flex[FdRaiser], 0.f);
	TestEqual(TEXT("rest: the lid sits on its hinge"), Flex[FdLid], 0.208f);
	// `1 / open` with `open` at zero: Source's DIV returns 0 rather than dividing, and without that
	// guard every downstream weight would be an infinity.
	TestEqual(TEXT("rest: DIV by zero yields zero, not infinity"), Flex[FdSuppressor], 0.f);
	for (int32 i = 0; i < Morph.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("rest: morph '%s' is off"), *Rig.Morphs[i].Name), Morph[i], 0.f);
	}

	// --- blink: the eyelid interaction ------------------------------------------------------
	// `blink` drives the lowerer directly and cancels the neutral and raiser terms, so the lid lands
	// on its lowered angle and the low ramp goes fully on. This is the case that proves the RPN
	// layer is real: `blink` names no morph target, and the morph it moves is not the flexdesc the
	// rule computes.
	Drive(Rig, { { CtlBlink, 1.f } }, Flex, Morph);
	TestEqual(TEXT("blink: lowerer"), Flex[FdLowerer], 1.f);
	TestEqual(TEXT("blink: neutral is cancelled"), Flex[FdNeutral], 0.f);
	TestEqual(TEXT("blink: raiser is cancelled"), Flex[FdRaiser], 0.f);
	TestEqual(TEXT("blink: the lid is at its lowered angle"), Flex[FdLid], -0.16f);
	TestEqual(TEXT("blink: the lowered morph is full"), Morph[MorphLid], 1.f);
	TestEqual(TEXT("blink: the raised morph is off"), Morph[MorphLidHigh], 0.f);

	// Half a blink is half a lid: the low ramp is linear between the hinge and the lowered angle.
	Drive(Rig, { { CtlBlink, 0.5f } }, Flex, Morph);
	TestEqual(TEXT("half blink: the lid is halfway"), Flex[FdLid], 0.5f * -0.16f + 0.5f * 0.208f);
	TestEqual(TEXT("half blink: the lowered morph is half on"), Morph[MorphLid], 0.5f);

	// --- droop: the suppression term --------------------------------------------------------
	// `droop` scales the neutral term by 0.8 and nothing else, so the three lid weights no longer
	// sum to one and the lid slides down without reaching its lowered angle: 0.2 * 0.208.
	Drive(Rig, { { CtlDroop, 1.f } }, Flex, Morph);
	TestEqual(TEXT("droop: neutral is suppressed"), Flex[FdNeutral], 0.2f);
	TestEqual(TEXT("droop: the lid slides toward lowered"), Flex[FdLid], 0.2f * 0.208f);
	TestEqual(TEXT("droop: the lowered morph is partly on"), Morph[MorphLid],
		(0.208f - 0.2f * 0.208f) / (0.208f + 0.16f));

	// --- the raiser -------------------------------------------------------------------------
	Drive(Rig, { { CtlLidUp, 1.f } }, Flex, Morph);
	TestEqual(TEXT("raise: raiser"), Flex[FdRaiser], 1.f);
	TestEqual(TEXT("raise: the lid is at its raised angle"), Flex[FdLid], 0.298f);
	TestEqual(TEXT("raise: the lowered morph is off"), Morph[MorphLid], 0.f);
	TestEqual(TEXT("raise: the raised morph is full"), Morph[MorphLidHigh], 1.f);

	// --- FETCH2: one rule reading what an earlier one wrote ----------------------------------
	// `plain = lid_up * (1 / open)` reaches through two rules. At `wide` = 0.5 of a 0..2 range the
	// controller normalizes to 0.25, so `open` is 0.25, `suppressor` is 4, and `plain` is 4 — well
	// inside its ramp's plateau.
	Drive(Rig, { { CtlLidUp, 1.f }, { CtlWide, 0.5f } }, Flex, Morph);
	TestEqual(TEXT("fetch2: the controller normalizes across its range"), Flex[FdOpen], 0.25f);
	TestEqual(TEXT("fetch2: the divisor chain resolves"), Flex[FdSuppressor], 4.f);
	TestEqual(TEXT("fetch2: the dependent rule reads it"), Flex[FdPlain], 4.f);
	TestEqual(TEXT("fetch2: the plateau holds the morph full"), Morph[MorphPlain], 1.f);
	TestEqual(TEXT("fetch2: open sits on its own hinge"), Morph[MorphOpen], 1.f);
	TestEqual(TEXT("fetch2: open's high half is off"), Morph[MorphOpenHigh], 0.f);

	// A write past the controller's authored maximum clamps rather than running away.
	Drive(Rig, { { CtlWide, 99.f } }, Flex, Morph);
	TestEqual(TEXT("an over-range write clamps to the maximum"), Flex[FdOpen], 1.f);
	Drive(Rig, { { CtlWide, -5.f } }, Flex, Morph);
	TestEqual(TEXT("an under-range write clamps to the minimum"), Flex[FdOpen], 0.f);

	return true;
}

// =====================================================================================
// The amplitude jaw — the rig's second input, and its precedence against the rule layer.
// =====================================================================================
//
// The jaw is the only facial write that is not a controller: `mstudiomouth_t` names a FLEXDESC, so
// it lands downstream of the 60 RPN rules instead of upstream of them. That ordering is the whole
// risk, and it is not hypothetical — 85 of the 86 exported rigs carry a rule that computes the mouth
// flexdesc, so a jaw applied anywhere but last would be recomputed away on every frame and the fault
// would read as "the envelope is not reaching the face".
//
// The test rig reproduces both halves of the shipped shape: `mouth` is computed by a rule and read by
// no morph target, and `jaw_drop` is the controller whose morph actually moves.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFlexJawTest, "Elysium.Substrate.FlexJaw", GElysiumFacialTestFlags)
bool FElysiumFlexJawTest::RunTest(const FString&)
{
	FElysiumFacialRig Rig;
	FString Error;
	if (!TestTrue(TEXT("the test rig parses"), Rig.LoadJsonText(GTestRigJson, Error)))
	{
		AddError(Error);
		return false;
	}

	TArray<float> Controllers;
	Controllers.AddZeroed(Rig.Controllers.Num());
	TArray<float> Flex, Morph;

	// --- the rule owns the mouth flexdesc, until the jaw does ----------------------------------
	// `mouth = jaw * 0.5` and `jaw = jaw_drop`, so the rule layer alone puts 0.35 there. That is the
	// value the jaw has to beat, and it is read off the rule layer directly because `Evaluate` never
	// leaves it standing — the write lands on every evaluation, closed jaw included, exactly as
	// Source's `ControlMouth` overwrites it after `RunFlexRules`.
	Controllers[CtlJawDrop] = 0.7f;
	Rig.EvalFlexWeights(Controllers, Flex);
	TestEqual(TEXT("the rule layer computes the mouth flexdesc"), Flex[FdMouth], 0.35f, 1e-5f);
	Rig.Evaluate(Controllers, Flex, Morph);
	TestEqual(TEXT("and a closed jaw overwrites it with zero"), Flex[FdMouth], 0.f);

	// --- the jaw wins, and wins every time -----------------------------------------------------
	// Evaluated twice from the same inputs: the override is applied on each pass rather than latched,
	// so the rule cannot reclaim the flexdesc on the frame after the write.
	FElysiumJawInput Jaw;
	Jaw.Open = 0.25f;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Rig.Evaluate(Controllers, Jaw, Flex, Morph);
		TestEqual(FString::Printf(TEXT("pass %d: the jaw overrides the rule's mouth value"), Pass),
			Flex[FdMouth], 0.25f, 1e-5f);
	}

	// --- the bridge is what actually moves anything --------------------------------------------
	// The mouth flexdesc carries no morph target here, as on every shipped rig, so the faithful write
	// alone leaves the face exactly where the controllers left it.
	Jaw.Open = 1.f;
	Jaw.bBridge = false;
	Rig.Evaluate(Controllers, Jaw, Flex, Morph);
	TestEqual(TEXT("bridge off: the mouth flexdesc still takes the jaw"), Flex[FdMouth], 1.f);
	TestEqual(TEXT("bridge off: the jaw morph is untouched by it"), Morph[MorphJaw], 0.7f, 1e-5f);

	Jaw.bBridge = true;
	Rig.Evaluate(Controllers, Jaw, Flex, Morph);
	TestEqual(TEXT("bridge on: the jaw morph opens"), Morph[MorphJaw], 1.f, 1e-5f);
	// And it reaches the morph through the rules rather than around them: `jaw` is a ruled flexdesc.
	TestEqual(TEXT("bridge on: through the rule layer"), Flex[FdJaw], 1.f, 1e-5f);

	// --- the bridge raises, it never lowers ----------------------------------------------------
	// An expression already holding the jaw wider than the line does keeps its own value: the jaw
	// layer opens a mouth and never closes one.
	Jaw.Open = 0.2f;
	Rig.Evaluate(Controllers, Jaw, Flex, Morph);
	TestEqual(TEXT("a wider controller write survives a narrower jaw"), Flex[FdJaw], 0.7f, 1e-5f);
	TestEqual(TEXT("and the flexdesc write is still the jaw's"), Flex[FdMouth], 0.2f, 1e-5f);

	// --- a closed jaw evaluates exactly as it did before this input existed ---------------------
	TArray<float> BaseFlex, BaseMorph;
	Rig.Evaluate(Controllers, BaseFlex, BaseMorph);
	Jaw.Open = 0.f;
	Rig.Evaluate(Controllers, Jaw, Flex, Morph);
	TestEqual(TEXT("a closed jaw leaves every morph where the controllers put it"),
		Morph.Num(), BaseMorph.Num());
	for (int32 i = 0; i < Morph.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("closed jaw: morph '%s'"), *Rig.Morphs[i].Name),
			Morph[i], BaseMorph[i]);
	}

	// --- a rig with no mouth record ------------------------------------------------------------
	// `female_raver_1` is the shipped shape of this and the export carries it: a mouth record pointing
	// at a flexdesc, a rig with nothing to deform. A rig with no record at all must not have some
	// other flexdesc written by default.
	FElysiumFacialRig NoMouth;
	if (TestTrue(TEXT("a mouthless rig parses"), NoMouth.LoadJsonText(
		TEXT(R"JSON({"stem":"nomouth","flexdescs":["a"],
		  "controllers":[{"name":"blink","type":"eyelid","min":0.0,"max":1.0}],
		  "rules":[{"flexdesc":0,"ops":[["FETCH1",0]]}],
		  "morphs":[{"name":"a","flexdesc":0,"targets":[0.0,1.0,10.0,11.0]}]})JSON"), Error)))
	{
		TestFalse(TEXT("it reports no mouth"), NoMouth.Mouth.IsValid());
		TestEqual(TEXT("and derives no bridge"), NoMouth.MouthBridge, INDEX_NONE);
		TArray<float> F, M;
		const TArray<float> One = { 1.f };
		FElysiumJawInput Open;
		Open.Open = 1.f;
		NoMouth.Evaluate(One, Open, F, M);
		TestEqual(TEXT("the jaw changes nothing on it"), F[0], 1.f);
	}

	return true;
}

// =====================================================================================
// The exported rigs — every one of them, at rest and at a blink.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFacialRigCorpusTest,
	"Elysium.Content.FacialRigs", GElysiumFacialTestFlags)
bool FElysiumFacialRigCorpusTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("npc_index parses"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}

	int32 Rigged = 0, Unrigged = 0, Blinkers = 0, Inert = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (Pair.Value.Facial.IsEmpty())
		{
			// A model with no flex rig is an ordinary load, not a failure: the whole animal, dancer
			// and crowd half of the cast carries no flex data, and neither does any player body.
			++Unrigged;
			continue;
		}
		if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcFacial(Pair.Value.Facial)))
		{
			AddError(FString::Printf(TEXT("%s: the index names '%s' and the file is not there"),
				*Pair.Key, *Pair.Value.Facial));
			continue;
		}
		FElysiumFacialRig Rig;
		if (!TestTrue(FString::Printf(TEXT("%s facial parses"), *Pair.Key),
			Rig.Load(Pair.Value.Facial, Error)))
		{
			AddError(Error);
			continue;
		}

		// The index's morph count is what the glb was written with; a disagreement means the two
		// halves of one export ran against different data.
		TestEqual(FString::Printf(TEXT("%s morph count matches the index"), *Pair.Key),
			Rig.Morphs.Num(), Pair.Value.MorphCount);

		if (!Rig.IsValid())
		{
			// Two exported models carry flex data that deforms nothing — the whole rig with no flex
			// record on any mesh, and a lone flexdesc. They parse and drive no face, which is the
			// same outcome as carrying no rig at all.
			TestTrue(FString::Printf(TEXT("%s drives nothing because it has no morph targets"), *Pair.Key),
				Rig.Morphs.IsEmpty());
			++Inert;
			continue;
		}
		++Rigged;

		// glTFRuntime keys a UMorphTarget by name, so a repeat would silently merge two ramps into
		// one. The `#k` suffix on the second and later ramp of one flexdesc is what prevents it.
		TSet<FString> Names;
		for (const FElysiumFlexMorph& Morph : Rig.Morphs)
		{
			bool bDuplicate = false;
			Names.Add(Morph.Name, &bDuplicate);
			if (bDuplicate)
			{
				AddError(FString::Printf(TEXT("%s: morph name '%s' repeats"), *Pair.Key, *Morph.Name));
			}
		}

		// Nothing that deforms the mesh may be left undriven: every morph-bearing flexdesc is either
		// computed by a rule or reconstructed as a lid.
		TSet<int32> Driven;
		for (const FElysiumFlexRule& Rule : Rig.Rules) { Driven.Add(Rule.FlexDesc); }
		for (const FElysiumFlexLid& Lid : Rig.Lids)    { Driven.Add(Lid.FlexDesc); }
		for (const FElysiumFlexMorph& Morph : Rig.Morphs)
		{
			if (!Driven.Contains(Morph.FlexDesc))
			{
				AddError(FString::Printf(TEXT("%s: morph '%s' hangs off flexdesc '%s', which no rule "
					"computes and no lid reconstructs"), *Pair.Key, *Morph.Name,
					*Rig.FlexDescs[Morph.FlexDesc]));
			}
		}

		TArray<float> Controllers, Flex, Morphs;
		Controllers.AddZeroed(Rig.Controllers.Num());

		// At rest the whole face is the authored mesh. This is the assertion the lid reconstruction
		// exists for: the four lid-value flexdescs no rule computes have to land exactly on their
		// hinges, or the cast stands around with its eyelids half-shut.
		Rig.Evaluate(Controllers, Flex, Morphs);
		for (int32 i = 0; i < Morphs.Num(); ++i)
		{
			if (!FMath::IsNearlyZero(Morphs[i]))
			{
				AddError(FString::Printf(TEXT("%s: at rest, morph '%s' is driven at %.4f"),
					*Pair.Key, *Rig.Morphs[i].Name, Morphs[i]));
			}
		}

		// And a blink closes the lids. `blink` drives no morph target directly — every weight it
		// produces comes out of the rule layer.
		const int32 Blink = Rig.FindController(TEXT("blink"));
		if (Blink == INDEX_NONE)
		{
			continue;
		}
		++Blinkers;
		Controllers[Blink] = 1.f;
		Rig.Evaluate(Controllers, Flex, Morphs);
		int32 Moved = 0;
		for (const float Weight : Morphs)
		{
			Moved += FMath::IsNearlyZero(Weight) ? 0 : 1;
		}
		TestTrue(FString::Printf(TEXT("%s: a blink moves at least one morph per lid"), *Pair.Key),
			Moved >= Rig.Lids.Num() && Rig.Lids.Num() > 0);
	}

	AddInfo(FString::Printf(
		TEXT("%d deforming rig(s), %d with a blink controller; %d rig(s) deform nothing; %d model(s) carry no flex rig"),
		Rigged, Blinkers, Inert, Unrigged));
	TestTrue(TEXT("the export carries at least one facial rig"), Rigged > 0);
	return true;
}

// =====================================================================================
// The two load contracts: morph targets merged across primitives, and declared as curves.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFacialMorphTargetsTest,
	"Elysium.Content.FacialMorphTargets", GElysiumFacialTestFlags)
bool FElysiumFacialMorphTargetsTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("npc_index parses"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}
	const FString Stem = FirstRiggedStem(Index);
	if (Stem.IsEmpty())
	{
		AddInfo(TEXT("skipping: no exported model carries both a glb and a facial sidecar"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("checking the load contracts on '%s'"), *Stem));

	FElysiumFacialRig Rig;
	if (!TestTrue(TEXT("the facial sidecar parses"), Rig.Load(Index.Npcs[Stem].Facial, Error)))
	{
		AddError(Error);
		return false;
	}

	UglTFRuntimeAsset* Asset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, Error);
	if (!TestNotNull(TEXT("the rigged mesh loads"), Mesh))
	{
		AddError(Error);
		return false;
	}

	const TArray<TObjectPtr<UMorphTarget>>& Targets = Mesh->GetMorphTargets();
	TestEqual(TEXT("the mesh carries one morph target per exported flex record"),
		Targets.Num(), Rig.Morphs.Num());

	// Contract 1 — MorphTargetsDuplicateStrategy::Merge. A morph that spans two materials arrives as
	// one same-named piece per glTF primitive, and Merge stitches them into a single UMorphTarget
	// covering several sections. Under the plugin default (Ignore) every target would cover exactly
	// one, which is a jaw that moves and leaves its teeth behind.
	int32 MultiSection = 0;
	for (const TObjectPtr<UMorphTarget>& Target : Targets)
	{
		if (Target != nullptr && !Target->GetMorphLODModels().IsEmpty()
			&& Target->GetMorphLODModels()[0].SectionIndices.Num() > 1)
		{
			++MultiSection;
		}
	}
	TestTrue(TEXT("at least one morph target is merged across primitives"), MultiSection > 0);
	AddInfo(FString::Printf(TEXT("%d of %d morph targets span more than one material primitive"),
		MultiSection, Targets.Num()));

	// Contract 2 — the exported name is the key, `#k` suffix and all, and it is what the anim curve
	// is named. A mismatch here means the facial track writes curves nothing is listening for.
	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!TestNotNull(TEXT("the runtime mesh has a skeleton"), Skeleton))
	{
		return false;
	}
	TSet<FName> Loaded;
	for (const TObjectPtr<UMorphTarget>& Target : Targets)
	{
		if (Target != nullptr)
		{
			Loaded.Add(Target->GetFName());
		}
	}
	int32 Hinged = 0;
	for (const FElysiumFlexMorph& Morph : Rig.Morphs)
	{
		Hinged += Morph.Name.Contains(TEXT("#")) ? 1 : 0;
		if (!TestTrue(FString::Printf(TEXT("morph '%s' is on the mesh"), *Morph.Name),
			Loaded.Contains(Morph.Curve)))
		{
			continue;
		}
		// The bone container takes its morph-target curve flags from this metadata, and without the
		// flag an evaluated curve never reaches the component's morph weights.
		const FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(Morph.Curve);
		if (TestTrue(FString::Printf(TEXT("'%s' is declared as a curve"), *Morph.Name),
			MetaData != nullptr))
		{
			TestTrue(FString::Printf(TEXT("'%s' is declared as a morph-target curve"), *Morph.Name),
				MetaData->Type.bMorphtarget);
		}
	}
	TestTrue(TEXT("the rig hinges at least one flexdesc into two named ramps"), Hinged > 0);

	return true;
}

// =====================================================================================
// The whole chain on a real body: a controller write reaching the component's morph weights.
// =====================================================================================
//
// Everything above this point checks one link. This drives the assembled thing — a registered
// skeletal-mesh component running UElysiumNpcAnimInstance over the real mesh — and reads the answer
// off USkeletalMeshComponent::MorphTargetWeights, which is what the skinning actually consumes. It
// is the check that would otherwise only exist in a running game: the anim-curve route from the
// proxy's Evaluate to a morph weight is silent when it fails.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFacialTrackTest,
	"Elysium.Content.FacialTrack", GElysiumFacialTestFlags)
bool FElysiumFacialTrackTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("npc_index parses"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}
	const FString Stem = FirstRiggedStem(Index);
	if (Stem.IsEmpty())
	{
		AddInfo(TEXT("skipping: no exported model carries both a glb and a facial sidecar"));
		return true;
	}

	TSharedPtr<FElysiumFacialRig> Rig = MakeShared<FElysiumFacialRig>();
	if (!TestTrue(TEXT("the facial sidecar parses"), Rig->Load(Index.Npcs[Stem].Facial, Error))
		|| !TestTrue(TEXT("the rig deforms something"), Rig->IsValid()))
	{
		AddError(Error);
		return false;
	}
	const int32 Blink = Rig->FindController(TEXT("blink"));
	if (!TestTrue(TEXT("the rig carries a blink controller"), Blink != INDEX_NONE))
	{
		return false;
	}

	UglTFRuntimeAsset* Asset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, Error);
	if (!TestNotNull(TEXT("the rigged mesh loads"), Mesh))
	{
		AddError(Error);
		return false;
	}

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("body owner spawned"), Owner))
	{
		return false;
	}

	// The same recipe UElysiumEntityBodies::BuildNpcVisual uses, minus the placement.
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	Owner->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance());
	if (!TestNotNull(TEXT("the Elysium animation host is installed"), Inst))
	{
		return false;
	}
	Inst->SetFacialRig(Rig);

	// A null tick function keeps the evaluation on this thread, so the weights are readable the
	// moment RefreshBoneTransforms returns rather than a frame later.
	auto Advance = [Comp]()
	{
		Comp->TickAnimation(1.f / 30.f, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
	};
	auto WeightOf = [Mesh, Comp](const FName Name) -> float
	{
		int32 WeightIndex = INDEX_NONE;
		Mesh->FindMorphTargetAndIndex(Name, WeightIndex);
		return Comp->MorphTargetWeights.IsValidIndex(WeightIndex) ? Comp->MorphTargetWeights[WeightIndex] : 0.f;
	};

	// At rest the whole face is off, so no morph target carries weight on the component either.
	Advance();
	float RestTotal = 0.f;
	for (const float Weight : Comp->MorphTargetWeights)
	{
		RestTotal += FMath::Abs(Weight);
	}
	TestEqual(TEXT("at rest the component drives no morph target"), RestTotal, 0.f);

	// One controller write, three layers of arithmetic, and the component's morph weights move.
	Inst->SetFlexControllerByIndex(Blink, 1.f);
	Advance();
	const TArray<float>& MorphWeights = Inst->GetMorphWeights();
	int32 Checked = 0;
	for (int32 i = 0; i < MorphWeights.Num(); ++i)
	{
		if (FMath::IsNearlyZero(MorphWeights[i]))
		{
			continue;
		}
		++Checked;
		TestEqual(FString::Printf(TEXT("blink drives '%s' on the component"), *Rig->Morphs[i].Name),
			WeightOf(Rig->Morphs[i].Curve), MorphWeights[i]);
	}
	TestTrue(TEXT("a blink moves at least one morph target"), Checked > 0);
	AddInfo(FString::Printf(TEXT("'%s': a blink drives %d of %d morph targets through to the component"),
		*Stem, Checked, MorphWeights.Num()));

	// And releasing it puts the face back: a curve written at zero every frame is what clears the
	// component's weight rather than leaving the last non-zero one latched.
	Inst->ResetFlexControllers();
	Advance();
	float ReleasedTotal = 0.f;
	for (const float Weight : Comp->MorphTargetWeights)
	{
		ReleasedTotal += FMath::Abs(Weight);
	}
	TestEqual(TEXT("releasing the controller clears every morph weight"), ReleasedTotal, 0.f);

	// A model with no facial sidecar stands on the same host and poses the same way — the absence is
	// an ordinary load, not a failure, and it is the majority case across the exported cast.
	FString Bare;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (Pair.Value.Facial.IsEmpty() && Pair.Value.Bones > 0
			&& IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(Pair.Key))
			&& (Bare.IsEmpty() || Pair.Key < Bare))
		{
			Bare = Pair.Key;
		}
	}
	if (Bare.IsEmpty())
	{
		AddInfo(TEXT("no exported model lacks a facial sidecar; the absent-rig case is not covered here"));
		return true;
	}

	UglTFRuntimeAsset* BareAsset = nullptr;
	USkeletalMesh* BareMesh = ElysiumNpcVisual::LoadMesh(Bare, BareAsset, Error);
	if (!TestNotNull(FString::Printf(TEXT("the unrigged mesh '%s' loads"), *Bare), BareMesh))
	{
		AddError(Error);
		return false;
	}
	AActor* BareOwner = World->SpawnActor<AActor>();
	USkeletalMeshComponent* BareComp = NewObject<USkeletalMeshComponent>(BareOwner);
	BareComp->SetMobility(EComponentMobility::Movable);
	BareComp->SetSkeletalMeshAsset(BareMesh);
	BareComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	BareComp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	BareOwner->SetRootComponent(BareComp);
	BareComp->RegisterComponent();

	UElysiumNpcAnimInstance* BareInst = Cast<UElysiumNpcAnimInstance>(BareComp->GetAnimInstance());
	if (!TestNotNull(TEXT("the unrigged body gets an animation host"), BareInst))
	{
		return false;
	}
	BareInst->SetFacialRig(nullptr);          // what the subsystem answers for a model with no sidecar
	BareComp->TickAnimation(1.f / 30.f, false);
	BareComp->RefreshBoneTransforms(nullptr);

	TestEqual(TEXT("the unrigged body still poses its whole skeleton"),
		BareComp->GetNumComponentSpaceTransforms(), BareMesh->GetRefSkeleton().GetNum());
	TestNull(TEXT("the unrigged body carries no rig"), BareInst->GetFacialRig());
	TestFalse(TEXT("a controller write on an unrigged body is answered, not obeyed"),
		BareInst->SetFlexController(TEXT("blink"), 1.f));
	TestEqual(TEXT("the unrigged mesh carries no morph targets"), BareMesh->GetMorphTargets().Num(), 0);

	return true;
}

// =====================================================================================
// The Faceposer weight table (roadmap 12.3) — the reader, against a hand-written fixture.
// =====================================================================================
//
// The one thing this format lets a reader get wrong while still producing a plausible face is
// `$hasweighting`: a row carries value and influence interleaved, two floats per key, and reading
// them as one float per key silently halves the key set and shifts every value onto the wrong
// controller. So the fixture is deliberately asymmetric — no two keys share a value — and the
// assertions name the controller each number has to land on.

namespace
{
	// Five keys, `$hasweighting`, quoted names that carry spaces, and a trailing description. The
	// numbers are chosen so a one-float-per-key misread lands on a different, checkable value.
	//
	//   Amused   claims brow/mouth and leaves `bite`/`wrinkler` alone (influence 0)
	//   Wince    overlaps Amused on `smile` and adds `wrinkler`
	//   No Deform  a row named with a trailing space and mixed case, as therese_expressions has
	//   short    a malformed row: one number short, so it is dropped and counted
	const TCHAR* const GTestTableText = TEXT(
		"$keys blink smile bite wrinkler nostril_flare\r\n"
		"$hasweighting\r\n"
		"\"Amused\" \"_\" 0.100 1.000 0.200 1.000 0.300 0.000 0.400 0.000 0.500 1.000 \"a wry look\"\r\n"
		"\"Wince\" \"_\" 0.000 0.000 0.900 1.000 0.000 0.000 0.700 1.000 0.000 0.000 \"pain\"\r\n"
		"\"Sly Smile \" \"_\" 0.110 1.000 0.220 1.000 0.330 1.000 0.440 1.000 0.550 1.000 \"trailing space\"\r\n"
		"\"short\" \"_\" 0.100 1.000 0.200 1.000 0.300 1.000 0.400 1.000 0.500 \"one number short\"\r\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumExpressionTableTest,
	"Elysium.Substrate.ExpressionTable", GElysiumFacialTestFlags)
bool FElysiumExpressionTableTest::RunTest(const FString&)
{
	FElysiumExpressionTable Table;
	FString Error;
	if (!TestTrue(TEXT("the fixture parses"), Table.ParseText(GTestTableText, TEXT("testface"), Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("$hasweighting is read"), Table.bHasWeighting);
	TestEqual(TEXT("$keys names five controllers"), Table.Keys.Num(), 5);
	TestEqual(TEXT("key 0"), Table.Keys[0], FString(TEXT("blink")));
	TestEqual(TEXT("key 4"), Table.Keys[4], FString(TEXT("nostril_flare")));
	// Three usable rows and the short one dropped: the corpus has none, and a silent mis-split would
	// otherwise read the description as a number and every value one key late.
	TestEqual(TEXT("three usable rows"), Table.Rows.Num(), 3);
	TestEqual(TEXT("the short row is dropped and counted"), Table.NumMalformedRows, 1);

	const int32 Amused = Table.FindRow(TEXT("Amused"));
	if (!TestTrue(TEXT("'Amused' is found"), Amused != INDEX_NONE))
	{
		return false;
	}
	const FElysiumExpressionRow& Row = Table.Rows[Amused];
	TestEqual(TEXT("the class field is the second quoted word"), Row.Class, FString(TEXT("_")));
	TestEqual(TEXT("the description is the trailing quoted word"), Row.Description, FString(TEXT("a wry look")));
	TestEqual(TEXT("one value per key"), Row.Values.Num(), 5);
	TestEqual(TEXT("one weight per key"), Row.Weights.Num(), 5);

	// The interleave, key by key: value at 2k, influence at 2k+1.
	const float ExpectValues[]  = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
	const float ExpectWeights[] = { 1.f,  1.f,  0.f,  0.f,  1.f  };
	for (int32 k = 0; k < 5; ++k)
	{
		TestEqual(FString::Printf(TEXT("'%s' value"), *Table.Keys[k]), Row.Values[k], ExpectValues[k]);
		TestEqual(FString::Printf(TEXT("'%s' influence"), *Table.Keys[k]), Row.Weights[k], ExpectWeights[k]);
	}
	// A key at influence 0 still carries its authored value. It is the blend that must not write it,
	// not the reader that must drop it — dropping it here would make the row unable to say "leave
	// this one alone" as distinct from "set it to zero".
	TestEqual(TEXT("an influence-0 key keeps its value"), Row.Values[2], 0.3f);

	// Lookup folds case and trims, because the corpus authors both.
	TestEqual(TEXT("row lookup folds case"), Table.FindRow(TEXT("amused")), Amused);
	TestEqual(TEXT("row lookup trims an authored trailing space"),
		Table.FindRow(TEXT("Sly Smile")), Table.FindRow(TEXT("sly smile ")));
	TestNotEqual(TEXT("the trailing-space row is reachable at all"),
		Table.FindRow(TEXT("Sly Smile")), int32(INDEX_NONE));
	TestEqual(TEXT("an absent row is INDEX_NONE"), Table.FindRow(TEXT("Ennui")), INDEX_NONE);

	// A file with no $keys is not a table. Both halves are checked because the two failures have
	// different causes — a truncated export against an empty one.
	FElysiumExpressionTable Empty;
	TestFalse(TEXT("a file with no $keys is rejected"),
		Empty.ParseText(TEXT("$hasweighting\r\n"), TEXT("empty"), Error));
	TestFalse(TEXT("a file with keys and no row is rejected"),
		Empty.ParseText(TEXT("$keys blink\r\n$hasweighting\r\n"), TEXT("keysonly"), Error));

	// --- a key the model does not carry ------------------------------------------------------
	// The 249 shipped tables draw on 48 distinct key names and no single rig carries all of them, so
	// a write set will name controllers a given face has not got. Those come back by name rather
	// than being dropped, which is the difference between a reportable data gap and a silent one.
	FElysiumFacialRig Rig;
	if (!TestTrue(TEXT("the test rig parses"), Rig.LoadJsonText(GTestRigJson, Error)))
	{
		AddError(Error);
		return false;
	}
	// The hand-written rig carries `blink` and not `smile`/`bite`/`wrinkler`/`nostril_flare`.
	TestNotEqual(TEXT("the rig carries 'blink'"), Rig.FindController(TEXT("blink")), int32(INDEX_NONE));
	TestEqual(TEXT("the rig does not carry 'nostril_flare'"),
		Rig.FindController(TEXT("nostril_flare")), INDEX_NONE);

	int32 Carried = 0;
	TArray<FString> Missing;
	for (const FString& Key : Table.Keys)
	{
		if (Rig.FindController(Key) != INDEX_NONE) { ++Carried; } else { Missing.Add(Key); }
	}
	TestEqual(TEXT("one of the table's five keys is on this rig"), Carried, 1);
	TestEqual(TEXT("the other four are reported by name"), Missing.Num(), 4);
	TestTrue(TEXT("'nostril_flare' is among them"), Missing.Contains(TEXT("nostril_flare")));

	return true;
}

// =====================================================================================
// The wire: a scene's expression events composed onto an actor's flex controllers.
// =====================================================================================
//
// Content-free — an inline `.vcd`, an inline table, and the recording embodiment standing in for a
// rig. What it covers is the part that is arithmetic over the scene clock and therefore silent when
// it is wrong: the ramp's time base, the influence blend between two overlapping rows, and the
// return to rest when an expression ends.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneExpressionTest,
	"Elysium.Substrate.SceneExpression", GElysiumFacialTestFlags)
bool FElysiumSceneExpressionTest::RunTest(const FString&)
{
	ElysiumExpressions::ClearCache();
	ElysiumExpressions::RegisterInline(TEXT("testface_expressions"), GTestTableText);

	// Two actors. `Face` carries `Amused` over 0..4, whose ramp rises from 0 at event-time 1 to 1 at
	// event-time 3; `Wince` over 2.5..6 at full intensity on a second channel; and one event naming a
	// row the table does not carry. `Blank` is an actor with no body, which is the shipped faceless
	// case — `!playercontroller` carries 20 of sp_theatre's own expression events.
	const FString SceneText =
		TEXT("// Choreo version 1\n")
		TEXT("actor \"Face\"\n{\n")
		TEXT("  channel \"Expressions\"\n  {\n")
		TEXT("    event expression \"amused\"\n    {\n")
		TEXT("      time 0.000000 4.000000\n")
		TEXT("      param \"testface_expressions\"\n")
		TEXT("      param2 \"Amused\"\n")
		TEXT("      event_ramp\n      {\n        1.0000 0.0000\n        3.0000 1.0000\n      }\n")
		TEXT("    }\n")
		TEXT("  }\n")
		TEXT("  channel \"brows\"\n  {\n")
		TEXT("    event expression \"wince\"\n    {\n")
		TEXT("      time 2.500000 6.000000\n")
		TEXT("      param \"testface_expressions\"\n")
		TEXT("      param2 \"Wince\"\n")
		TEXT("    }\n")
		TEXT("    event expression \"nosuchrow\"\n    {\n")
		TEXT("      time 2.500000 6.000000\n")
		TEXT("      param \"testface_expressions\"\n")
		TEXT("      param2 \"Ennui\"\n")
		TEXT("    }\n")
		TEXT("  }\n")
		TEXT("}\n")
		TEXT("actor \"Blank\"\n{\n")
		TEXT("  channel \"Expressions\"\n  {\n")
		TEXT("    event expression \"amused\"\n    {\n")
		TEXT("      time 0.000000 4.000000\n")
		TEXT("      param \"testface_expressions\"\n")
		TEXT("      param2 \"Amused\"\n")
		TEXT("    }\n")
		TEXT("  }\n")
		TEXT("}\n")
		TEXT("fps 60\nsnap off\n");
	ElysiumScene::RegisterInline(TEXT("test/facial.vcd"), SceneText);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	FElysiumEntityDef S;
	S.Classname = TEXT("logic_choreographed_scene");
	S.TargetName = TEXT("scene1");
	S.Keys.Add(TEXT("SceneFile"), TEXT("test/facial.vcd"));
	Defs.Defs.Add(MoveTemp(S));

	// A body-carrying character, so the write reaches the embodiment seam.
	FElysiumEntityDef Face;
	Face.Classname = TEXT("npc_VVampire");
	Face.TargetName = TEXT("Face");
	Face.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/testface.mdl"));
	Defs.Defs.Add(MoveTemp(Face));

	// No model, so no body and no face. The scene has to pose the one actor that has a face and let
	// this one alone rather than treating the absence as a failure.
	FElysiumEntityDef Blank;
	Blank.Classname = TEXT("npc_VVampire");
	Blank.TargetName = TEXT("Blank");
	Defs.Defs.Add(MoveTemp(Blank));

	FElysiumRecordingServices Services;
	// The rig this fake face carries: four of the table's five keys, so `nostril_flare` is the key
	// the model does not carry and has to come back reported.
	Services.FlexControllers = { TEXT("blink"), TEXT("smile"), TEXT("bite"), TEXT("wrinkler") };

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* SceneEnt = World.FindByName(TEXT("scene1"));
	if (!TestNotNull(TEXT("the scene entity resolved"), SceneEnt))
	{
		return false;
	}

	double T = 0.0;
	World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
	World.Tick(T);

	// --- t = 0: the ramp is at its first authored sample, which is zero -----------------------
	// `Amused` is live and its ramp starts at 0, so every key it claims composes to exactly zero.
	// A reader that ignored `event_ramp` would already be showing the full expression here.
	TestEqual(TEXT("t=0: the ramp holds smile at zero"), Services.FlexValue(TEXT("smile")), 0.f);
	TestEqual(TEXT("t=0: the ramp holds blink at zero"), Services.FlexValue(TEXT("blink")), 0.f);

	// --- t = 2: halfway up the ramp, Amused alone ---------------------------------------------
	// The ramp's times are event-relative and this event starts at 0, so t=2 is halfway between the
	// samples at 1 and 3: intensity 0.5. `blink` is 0.1 at influence 1, so it composes to 0.05. Read
	// on the scene clock instead, the ramp would still be at its first sample and everything zero.
	T = 2.0; World.Tick(T);
	TestEqual(TEXT("t=2: blink is half its authored value"), Services.FlexValue(TEXT("blink")), 0.05f, 1e-4f);
	TestEqual(TEXT("t=2: smile is half its authored value"), Services.FlexValue(TEXT("smile")), 0.1f, 1e-4f);
	// `bite` is influence 0 in `Amused`: the row does not claim it, so it stays at rest rather than
	// being pulled to the row's own 0.3.
	TestEqual(TEXT("t=2: an influence-0 key is left alone"), Services.FlexValue(TEXT("bite")), 0.f);

	// --- t = 3.5: the ramp is settled and both rows are live ----------------------------------
	// `Wince` starts later and claims `smile` at influence 1, so it wins the key the two rows share.
	// This is what separates the influence blend from a sum, which would read 0.2 + 0.9 here.
	T = 3.5; World.Tick(T);
	TestEqual(TEXT("t=3.5: blink is fully ramped"), Services.FlexValue(TEXT("blink")), 0.1f, 1e-4f);
	TestEqual(TEXT("t=3.5: the later row wins the key both claim"),
		Services.FlexValue(TEXT("smile")), 0.9f, 1e-4f);
	TestEqual(TEXT("t=3.5: and brings its own key with it"),
		Services.FlexValue(TEXT("wrinkler")), 0.7f, 1e-4f);

	// --- t = 5: Amused has ended, Wince has not -----------------------------------------------
	// The keys `Amused` was driving and `Wince` does not claim go back to zero; a scene that only
	// wrote what was live would leave `blink` latched at the last frame of an expression that ended.
	T = 5.0; World.Tick(T);
	TestEqual(TEXT("t=5: the ended expression's key is back at rest"), Services.FlexValue(TEXT("blink")), 0.f);
	TestEqual(TEXT("t=5: the live expression still holds its keys"),
		Services.FlexValue(TEXT("smile")), 0.9f, 1e-4f);

	// --- past the end: every key back at rest -------------------------------------------------
	T = 8.0; World.Tick(T);
	TestEqual(TEXT("completion returns smile to rest"), Services.FlexValue(TEXT("smile")), 0.f);
	TestEqual(TEXT("completion returns wrinkler to rest"), Services.FlexValue(TEXT("wrinkler")), 0.f);

	// --- diagnostics ---------------------------------------------------------------------------
	// A row the table does not carry is counted, not dropped in silence; so is a key the rig lacks.
	TArray<TPair<FString, FString>> State;
	SceneEnt->GetDebugState(State);
	FString Unresolved, Expressions;
	for (const TPair<FString, FString>& Row : State)
	{
		if (Row.Key == TEXT("Unresolved"))  { Unresolved = Row.Value; }
		if (Row.Key == TEXT("Expressions")) { Expressions = Row.Value; }
	}
	TestTrue(TEXT("the unresolvable row is counted"), Unresolved.Contains(TEXT("expressions 1")));
	TestTrue(TEXT("the key the rig lacks is counted"), Expressions.Contains(TEXT("key(s) the model lacks")));
	TestEqual(TEXT("nostril_flare never reached the rig"), Services.FlexValue(TEXT("nostril_flare")), 0.f);

	ElysiumExpressions::ClearCache();
	return true;
}

// =====================================================================================
// The wire: a scene's silence/loud envelope composed onto its actors' jaws.
// =====================================================================================
//
// Content-free — two inline `.vcd`s and the recording embodiment standing in for a rig. What it
// covers is the arithmetic over the scene clock that is silent when it is wrong: the three levels
// and their precedence, the fact that the jaw runs on UNOFFSET scene time while the speak event that
// carries it was dispatched a mixahead early, the envelope borrowed from a line's own `.vcd`, the
// lag between levels, and the return to a shut mouth when the scene ends.

namespace
{
	// Set a float cvar and hand back the old value, so a test restores what it changed.
	float SwapFloatCVar(const TCHAR* Name, float Value)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var == nullptr)
		{
			return 0.f;
		}
		const float Was = Var->GetFloat();
		Var->Set(Value, ECVF_SetByCode);
		return Was;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneJawTest, "Elysium.Substrate.SceneJaw", GElysiumFacialTestFlags)
bool FElysiumSceneJawTest::RunTest(const FString&)
{
	// Exact levels rather than an asymptote: the lag has its own section below.
	const float WasSmoothing = SwapFloatCVar(TEXT("elysium.JawSmoothing"), 0.f);
	const float WasSpeech = SwapFloatCVar(TEXT("elysium.JawSpeechLevel"), 0.5f);
	ON_SCOPE_EXIT
	{
		SwapFloatCVar(TEXT("elysium.JawSmoothing"), WasSmoothing);
		SwapFloatCVar(TEXT("elysium.JawSpeechLevel"), WasSpeech);
	};

	// `Marked` carries the shipped shape: a `speak` on one channel and a `Speech Triggers` channel
	// beside it holding one silence span and one loud span, neither abutting the other, both inside
	// the line. `Borrowed` speaks a line whose envelope lives only in the line's own `.vcd`, which is
	// what all eleven of sp_theatre's scenes look like. `Faceless` speaks with no body at all.
	const FString SceneText =
		TEXT("// Choreo version 1\n")
		TEXT("actor \"Marked\"\n{\n")
		TEXT("  channel \"Speech\"\n  {\n")
		TEXT("    event speak \"line\"\n    {\n")
		TEXT("      time 1.000000 5.000000\n")
		TEXT("      param \"test/nolineenvelope.wav\"\n")
		TEXT("    }\n  }\n")
		TEXT("  channel \"Speech Triggers\"\n  {\n")
		TEXT("    event silence \"s\"\n    {\n")
		TEXT("      time 2.000000 2.500000\n      param \"0.500\"\n")
		TEXT("    }\n")
		TEXT("    event loud \"l\"\n    {\n")
		TEXT("      time 3.000000 3.200000\n      param \"0.200\"\n")
		TEXT("    }\n")
		TEXT("    event loud \"l2\"\n    {\n")
		TEXT("      time 4.410000 4.800000\n      param \"0.390\"\n")
		TEXT("    }\n  }\n}\n")
		TEXT("actor \"Borrowed\"\n{\n")
		TEXT("  channel \"Speech\"\n  {\n")
		TEXT("    event speak \"line\"\n    {\n")
		TEXT("      time 1.000000 5.000000\n")
		TEXT("      param \"test/Line.WAV\"\n")
		TEXT("    }\n  }\n}\n")
		TEXT("actor \"Faceless\"\n{\n")
		TEXT("  channel \"Speech\"\n  {\n")
		TEXT("    event speak \"line\"\n    {\n")
		TEXT("      time 1.000000 5.000000\n")
		TEXT("      param \"test/Line.WAV\"\n")
		TEXT("    }\n  }\n}\n")
		TEXT("fps 60\nsnap off\n");
	// The per-line scene beside `test/line.wav`. Its times are the line's, not the scene's.
	const FString LineText =
		TEXT("// Choreo version 1\n")
		TEXT("actor \"Borrowed\"\n{\n")
		TEXT("  channel \"Speech Triggers\"\n  {\n")
		TEXT("    event loud \"l\"\n    {\n")
		TEXT("      time 1.000000 1.200000\n      param \"0.200\"\n")
		TEXT("    }\n  }\n}\n")
		TEXT("fps 60\nsnap off\n");
	ElysiumScene::ClearCache();
	ElysiumScene::RegisterInline(TEXT("test/jaw.vcd"), SceneText);
	ElysiumScene::RegisterInline(TEXT("test/line.vcd"), LineText);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	FElysiumEntityDef S;
	S.Classname = TEXT("logic_choreographed_scene");
	S.TargetName = TEXT("scene1");
	S.Keys.Add(TEXT("SceneFile"), TEXT("test/jaw.vcd"));
	Defs.Defs.Add(MoveTemp(S));
	for (const TCHAR* Name : { TEXT("Marked"), TEXT("Borrowed") })
	{
		FElysiumEntityDef Actor;
		Actor.Classname = TEXT("npc_VVampire");
		Actor.TargetName = Name;
		Actor.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/testface.mdl"));
		Defs.Defs.Add(MoveTemp(Actor));
	}
	FElysiumEntityDef Faceless;
	Faceless.Classname = TEXT("npc_VVampire");
	Faceless.TargetName = TEXT("Faceless");
	Defs.Defs.Add(MoveTemp(Faceless));

	FElysiumRecordingServices Services;
	Services.FlexControllers = { TEXT("jaw_drop") };

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	const FElysiumEntity* Marked = World.FindByName(TEXT("Marked"));
	const FElysiumEntity* Borrowed = World.FindByName(TEXT("Borrowed"));
	const FElysiumEntity* Faceles = World.FindByName(TEXT("Faceless"));
	FElysiumEntity* SceneEnt = World.FindByName(TEXT("scene1"));
	if (!TestNotNull(TEXT("the scene entity resolved"), SceneEnt)
		|| !TestNotNull(TEXT("Marked resolved"), Marked)
		|| !TestNotNull(TEXT("Borrowed resolved"), Borrowed))
	{
		return false;
	}

	double T = 0.0;
	World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
	World.Tick(T);
	TestEqual(TEXT("t=0: nothing is speaking, so every jaw is shut"),
		Services.MouthOpenOf(Marked), 0.f);

	// --- t = 0.95: the speak event has been dispatched, the line has not started ---------------
	// `elysium.SceneMixahead` pulls a speak event's start 0.1 s earlier so the sample is HEARD at the
	// authored instant. The jaw runs on the authored clock instead, and the audio catches up to it —
	// so a jaw reading the dispatch would already be half open here.
	T = 0.95; World.Tick(T);
	TestEqual(TEXT("t=0.95: the mixahead does not open the jaw early"),
		Services.MouthOpenOf(Marked), 0.f);

	// --- t = 1.5: speaking, no marker ----------------------------------------------------------
	T = 1.5; World.Tick(T);
	TestEqual(TEXT("t=1.5: an unmarked instant of a line holds the speaking level"),
		Services.MouthOpenOf(Marked), 0.5f, 1e-4f);

	// --- t = 2.1: Marked is inside its silence span; Borrowed is inside its borrowed one --------
	// The scene clock only ever moves forward — a substrate think is armed at a time, so a test that
	// stepped backwards would silently stop ticking the scene — so the two envelope sources are read
	// at the same instant rather than in two passes.
	//
	// `Borrowed`'s own scene marks nothing, which is the shape of all eleven of sp_theatre's, so its
	// whole flap comes out of the `.vcd` beside its audio, placed at the speak event's own authored
	// start: line-time 1.0 is scene-time 2.0.
	T = 2.1; World.Tick(T);
	TestEqual(TEXT("t=2.1: a silence span shuts Marked's mouth"),
		Services.MouthOpenOf(Marked), 0.f, 1e-4f);
	TestEqual(TEXT("t=2.1: the line's own loud span reaches Borrowed's jaw"),
		Services.MouthOpenOf(Borrowed), 1.f, 1e-4f);

	// --- t = 3.1: inside Marked's loud span -----------------------------------------------------
	T = 3.1; World.Tick(T);
	TestEqual(TEXT("t=3.1: a loud span opens it fully"), Services.MouthOpenOf(Marked), 1.f, 1e-4f);
	TestEqual(TEXT("t=3.1: and Borrowed is back to speaking past its own span"),
		Services.MouthOpenOf(Borrowed), 0.5f, 1e-4f);

	// --- t = 4: past both markers, still speaking ----------------------------------------------
	T = 4.0; World.Tick(T);
	TestEqual(TEXT("t=4: back to the speaking level once the markers end"),
		Services.MouthOpenOf(Marked), 0.5f, 1e-4f);

	// --- the faceless actor ----------------------------------------------------------------------
	// No model, so no body: the write has nowhere to land and the scene must not treat that as a
	// failure. It is the majority case — no player body in the install carries a flexdesc at all.
	TestEqual(TEXT("a bodiless actor's jaw stays at rest"), Services.MouthOpenOf(Faceles), 0.f);

	// --- the lag ---------------------------------------------------------------------------------
	// The one invented mechanism in this track, so it is asserted as arithmetic rather than as a
	// look: a first-order lag closes 1 - 1/e = 63.2 % of the remaining distance in one time constant.
	// At the shipped 0.05 s, one 0.05 s step into the second loud span must land partway — a step
	// would already be at 1, and a longer constant short of 0.816.
	SwapFloatCVar(TEXT("elysium.JawSmoothing"), 0.05f);
	T = 4.0;  World.Tick(T);   // settled at the speaking level over a long step
	T = 4.4;  World.Tick(T);   // still unmarked, and a settled level does not drift
	TestEqual(TEXT("a settled level does not drift under smoothing"),
		Services.MouthOpenOf(Marked), 0.5f, 1e-3f);
	T = 4.45; World.Tick(T);   // one tau into the loud span
	TestEqual(TEXT("one time constant covers 63.2% of the way to a loud span"),
		Services.MouthOpenOf(Marked), 0.5f + 0.5f * (1.f - FMath::Exp(-1.f)), 1e-3f);

	// --- completion ------------------------------------------------------------------------------
	// The scene ends at 5.0 + the mixahead. Every jaw it moved shuts at once rather than lagging
	// down, because there is no next frame to lag it in.
	T = 8.0; World.Tick(T);
	TestEqual(TEXT("completion shuts Marked's jaw"), Services.MouthOpenOf(Marked), 0.f);
	TestEqual(TEXT("completion shuts Borrowed's jaw"), Services.MouthOpenOf(Borrowed), 0.f);

	// --- diagnostics -----------------------------------------------------------------------------
	TArray<TPair<FString, FString>> State;
	SceneEnt->GetDebugState(State);
	FString Jaw;
	for (const TPair<FString, FString>& Row : State)
	{
		if (Row.Key == TEXT("Jaw")) { Jaw = Row.Value; }
	}
	TestTrue(TEXT("the inspector counts the authored markers"), Jaw.Contains(TEXT("1 silence + 2 loud")));
	// Two of the three lines have a sibling `.vcd`, one does not — the ordinary split, reported as a
	// count rather than warned about, because a line with no envelope is not a fault.
	TestTrue(TEXT("and the envelopes borrowed from line files"),
		Jaw.Contains(TEXT("2 line envelope(s), 1 line(s) with none")));

	ElysiumScene::ClearCache();
	return true;
}

// =====================================================================================
// The theatre's own scenes: every expression event, end to end, against the real export.
// =====================================================================================
//
// The acceptance corpus. For each `expression` event on each of sp_theatre's twelve scenes, the
// three joins have to hold: `param` names a table on disk, `param2` names a row in it, and every
// key of that row is a flex controller on the rig of the model the map's own actor entity carries.
// An unresolvable reference here is a finding about the export or the map data, not a case to skip,
// so each one is reported by name.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreExpressionsTest,
	"Elysium.Content.TheatreExpressions", GElysiumFacialTestFlags)
bool FElysiumTheatreExpressionsTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("skipping: sp_theatre is not exported (run: uv run elysium export map sp_theatre)"));
		return true;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre.ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("skipping: no NPC index (%s)"), *Error));
		return true;
	}

	// targetname -> model stem, the same rule FElysiumAnimating::ModelStem applies.
	TMap<FString, FString> StemByName;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		const FString* Model = Def.Keys.Find(TEXT("model"));
		if (Def.TargetName.IsEmpty() || Model == nullptr || Model->IsEmpty())
		{
			continue;
		}
		StemByName.Add(Def.TargetName.ToLower(), FPaths::GetBaseFilename(*Model).ToLower());
	}

	// One rig per stem, loaded once.
	TMap<FString, TSharedPtr<FElysiumFacialRig>> RigByStem;
	auto RigFor = [&](const FString& Stem) -> const FElysiumFacialRig*
	{
		if (const TSharedPtr<FElysiumFacialRig>* Found = RigByStem.Find(Stem))
		{
			return Found->Get();
		}
		TSharedPtr<FElysiumFacialRig> Rig;
		const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Stem);
		if (Entry != nullptr && !Entry->Facial.IsEmpty())
		{
			TSharedPtr<FElysiumFacialRig> Loaded = MakeShared<FElysiumFacialRig>();
			FString RigError;
			if (Loaded->Load(Entry->Facial, RigError))
			{
				Rig = Loaded;
			}
			else
			{
				AddError(FString::Printf(TEXT("%s: %s"), *Stem, *RigError));
			}
		}
		RigByStem.Add(Stem, Rig);
		return Rig.Get();
	};

	int32 NumScenes = 0, NumEvents = 0, NumResolved = 0, NumOnFacelessActor = 0;
	int32 NumKeyChecks = 0, NumKeysOnRig = 0;
	TSet<FString> MissingTables, MissingRows, MissingKeys, FacelessActors;

	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		const FString* SceneFile = Def.Classname == TEXT("logic_choreographed_scene")
			? Def.Keys.Find(TEXT("SceneFile")) : nullptr;
		if (SceneFile == nullptr || SceneFile->IsEmpty())
		{
			continue;
		}
		TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
		if (!Scene.IsValid())
		{
			AddWarning(FString::Printf(TEXT("%s: SceneFile '%s' does not resolve"),
				*Def.TargetName, **SceneFile));
			continue;
		}
		++NumScenes;

		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if (Event.Type != EElysiumChoreoEvent::Expression)
			{
				continue;
			}
			++NumEvents;
			const FString Actor = Scene->Actors.IsValidIndex(Event.ActorIndex)
				? Scene->Actors[Event.ActorIndex].Name : FString();

			// 1 — the table.
			TSharedPtr<const FElysiumExpressionTable> Table =
				ElysiumExpressions::Load(Event.Param, TEXT("expressions"));
			if (!Table.IsValid())
			{
				MissingTables.Add(FString::Printf(TEXT("%s (%s)"), *Event.Param, *Scene->SourceRel));
				continue;
			}
			// 2 — the row.
			const int32 RowIndex = Table->FindRow(Event.Param2);
			if (RowIndex == INDEX_NONE)
			{
				MissingRows.Add(FString::Printf(TEXT("%s / \"%s\" (%s)"),
					*Table->Stem, *Event.Param2, *Scene->SourceRel));
				continue;
			}
			++NumResolved;

			// 3 — the controller indices, on the rig of the model this actor's entity carries.
			// `!playercontroller` is the shipped faceless case and it is the majority here: no
			// player body in the install carries a flexdesc, so those 20 events are an authored
			// no-op rather than a gap.
			const FString* Stem = StemByName.Find(Actor.ToLower());
			const FElysiumFacialRig* Rig = Stem != nullptr ? RigFor(*Stem) : nullptr;
			if (Rig == nullptr || !Rig->IsValid())
			{
				++NumOnFacelessActor;
				FacelessActors.Add(FString::Printf(TEXT("%s -> %s"), *Actor,
					Stem != nullptr ? **Stem : TEXT("(no map entity)")));
				continue;
			}
			for (const FString& Key : Table->Keys)
			{
				++NumKeyChecks;
				if (Rig->FindController(Key) != INDEX_NONE)
				{
					++NumKeysOnRig;
				}
				else
				{
					MissingKeys.Add(FString::Printf(TEXT("%s: '%s' (table %s)"), **Stem, *Key, *Table->Stem));
				}
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d scene(s), %d expression event(s): %d resolve to a table and a row; %d sit on an actor "
			 "with no rig; %d of %d controller key(s) are on the actor's rig"),
		NumScenes, NumEvents, NumResolved, NumOnFacelessActor, NumKeysOnRig, NumKeyChecks));
	for (const FString& A : FacelessActors)
	{
		AddInfo(FString::Printf(TEXT("faceless actor: %s"), *A));
	}
	for (const FString& M : MissingTables) { AddError(FString::Printf(TEXT("no expression table: %s"), *M)); }
	for (const FString& M : MissingRows)   { AddError(FString::Printf(TEXT("no such row: %s"), *M)); }
	for (const FString& M : MissingKeys)   { AddError(FString::Printf(TEXT("key not on the rig: %s"), *M)); }

	TestTrue(TEXT("sp_theatre's scenes carry expression events at all"), NumEvents > 0);
	TestEqual(TEXT("every expression event resolves to a table and a row"), NumResolved, NumEvents);
	TestEqual(TEXT("every key of every resolved row is a controller on the actor's rig"),
		NumKeysOnRig, NumKeyChecks);
	TestTrue(TEXT("at least one event lands on an actor that has a rig"), NumKeyChecks > 0);

	return true;
}

// =====================================================================================
// The amplitude jaw against the real export: every envelope, and the face it has to reach.
// =====================================================================================
//
// Two joins, over every map-placed choreo scene the export resolves. For a `silence`/`loud` event:
// the actor it was authored under has to name a map entity whose model carries a facial sidecar with
// an `mstudiomouth_t` record, or the flap has nowhere to land. For a `speak` event: the per-line
// `.vcd` beside its audio has to resolve, because on the scenes that mark nothing themselves — all
// eleven of sp_theatre's — that file is the only envelope the line has.
//
// An actor with no mouth record is not a failure, it is the majority of the cast; what the test
// asserts about it is that it is *reported*, and the runtime's own answer for it (a `SetMouthOpen`
// returning false, changing nothing) is covered content-free by `Elysium.Substrate.SceneJaw`.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreJawTest,
	"Elysium.Content.TheatreJaw", GElysiumFacialTestFlags)
bool FElysiumTheatreJawTest::RunTest(const FString&)
{
	if (!FElysiumContentPaths::IsConfigured()
		|| !IFileManager::Get().FileExists(*FElysiumContentPaths::MapEnts(TEXT("sp_theatre"))))
	{
		AddInfo(TEXT("skipping: sp_theatre is not exported (run: uv run elysium export map sp_theatre)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString IndexError;
	if (!Index.Load(IndexError))
	{
		AddInfo(FString::Printf(TEXT("skipping: no NPC index (%s)"), *IndexError));
		return true;
	}

	// Every exported map, so the property has data at all: sp_theatre's own scenes carry no envelope
	// (see the count below), and the maps that do are what prove the join holds.
	TArray<FString> Maps;
	IFileManager::Get().IterateDirectory(*FElysiumContentPaths::Root(),
		[&Maps](const TCHAR* Path, bool bIsDir) -> bool
		{
			const FString Leaf = FPaths::GetCleanFilename(Path);
			if (bIsDir && !Leaf.StartsWith(TEXT("_"))
				&& IFileManager::Get().FileExists(*FElysiumContentPaths::MapEnts(Leaf)))
			{
				Maps.Add(Leaf);
			}
			return true;
		});
	Maps.Sort();

	TMap<FString, TSharedPtr<FElysiumFacialRig>> RigByStem;
	auto RigFor = [&](const FString& Stem) -> const FElysiumFacialRig*
	{
		if (const TSharedPtr<FElysiumFacialRig>* Found = RigByStem.Find(Stem))
		{
			return Found->Get();
		}
		TSharedPtr<FElysiumFacialRig> Rig;
		const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Stem);
		if (Entry != nullptr && !Entry->Facial.IsEmpty())
		{
			TSharedPtr<FElysiumFacialRig> Loaded = MakeShared<FElysiumFacialRig>();
			FString RigError;
			if (Loaded->Load(Entry->Facial, RigError)) { Rig = Loaded; }
		}
		RigByStem.Add(Stem, Rig);
		return Rig.Get();
	};

	int32 TheatreEnvelope = 0, TheatreSpeak = 0, TheatreLines = 0, TheatreLineFiles = 0;
	int32 NumEnvelope = 0, NumOnMouth = 0, NumFaceless = 0;
	int32 NumSpeak = 0, NumSpeakWithFile = 0, NumSpeakWithLine = 0;
	TSet<FString> Faceless, MapsWithEnvelope, NoBridge, SilentLines;

	for (const FString& Map : Maps)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(Map), Defs))
		{
			AddWarning(FString::Printf(TEXT("%s: .ents did not parse"), *Map));
			continue;
		}
		TMap<FString, FString> StemByName;
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			const FString* Model = Def.Keys.Find(TEXT("model"));
			if (!Def.TargetName.IsEmpty() && Model != nullptr && !Model->IsEmpty())
			{
				StemByName.Add(Def.TargetName.ToLower(), FPaths::GetBaseFilename(*Model).ToLower());
			}
		}
		TSet<FString> Seen;
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			const FString* SceneFile = Def.Classname == TEXT("logic_choreographed_scene")
				? Def.Keys.Find(TEXT("SceneFile")) : nullptr;
			if (SceneFile == nullptr || SceneFile->IsEmpty()
				|| Seen.Contains(ElysiumScene::NormalizeSceneRel(*SceneFile)))
			{
				continue;
			}
			Seen.Add(ElysiumScene::NormalizeSceneRel(*SceneFile));
			TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
			if (!Scene.IsValid())
			{
				continue;   // map data outliving its assets; the expression test already reports it
			}
			const bool bTheatre = Map == TEXT("sp_theatre");

			for (const FElysiumSceneEvent& Event : Scene->Events)
			{
				const FString Actor = Scene->Actors.IsValidIndex(Event.ActorIndex)
					? Scene->Actors[Event.ActorIndex].Name : FString();

				if (Event.Type == EElysiumChoreoEvent::Silence || Event.Type == EElysiumChoreoEvent::Loud)
				{
					++NumEnvelope;
					MapsWithEnvelope.Add(Map);
					if (bTheatre) { ++TheatreEnvelope; }
					const FString* Stem = StemByName.Find(Actor.ToLower());
					const FElysiumFacialRig* Rig = Stem != nullptr ? RigFor(*Stem) : nullptr;
					if (Rig != nullptr && Rig->Mouth.IsValid())
					{
						++NumOnMouth;
						// The reconstruction's own reach: a mouth record with no `jaw_drop` to bridge
						// into writes a flexdesc no shipped model consumes and moves nothing.
						if (Rig->MouthBridge == INDEX_NONE) { NoBridge.Add(*Stem); }
					}
					else
					{
						++NumFaceless;
						Faceless.Add(FString::Printf(TEXT("%s / actor \"%s\" -> %s"), *Map, *Actor,
							Stem != nullptr ? **Stem : TEXT("(no map entity)")));
					}
				}
				else if (Event.Type == EElysiumChoreoEvent::Speak && !Event.Param.IsEmpty())
				{
					++NumSpeak;
					if (bTheatre) { ++TheatreSpeak; }
					const FString Rel = FPaths::SetExtension(
						ElysiumScene::NormalizeSceneRel(Event.Param), TEXT("vcd"));
					TSharedPtr<const FElysiumSceneData> Line = ElysiumScene::Load(Rel);
					const bool bHasEnvelope = Line.IsValid()
						&& (Line->CountOf(EElysiumChoreoEvent::Silence)
							+ Line->CountOf(EElysiumChoreoEvent::Loud)) > 0;
					if (Line.IsValid())
					{
						++NumSpeakWithFile;
						if (bTheatre) { ++TheatreLineFiles; }
					}
					if (bHasEnvelope)
					{
						++NumSpeakWithLine;
						if (bTheatre) { ++TheatreLines; }
					}
					else if (bTheatre)
					{
						// 517 of the 5,444 shipped scenes carry no envelope at all; a line that is one
						// of them simply holds the speaking level, which is a state rather than a gap.
						SilentLines.Add(FString::Printf(TEXT("%s / actor \"%s\" -> %s"),
							*Scene->SourceRel, *Actor, *Rel));
					}
				}
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d exported map(s): %d silence/loud event(s) on map-placed scenes, %d of them on an "
			 "actor whose model carries an mstudiomouth_t; %d speak event(s), %d resolving to a "
			 "per-line .vcd, %d of those carrying an envelope"),
		Maps.Num(), NumEnvelope, NumOnMouth, NumSpeak, NumSpeakWithFile, NumSpeakWithLine));
	AddInfo(FString::Printf(TEXT("maps whose placed scenes mark anything: %s"),
		MapsWithEnvelope.IsEmpty() ? TEXT("(none)")
			: *FString::Join(MapsWithEnvelope.Array(), TEXT(", "))));
	// The finding this slice turns on, asserted so it cannot quietly change under a re-export.
	AddInfo(FString::Printf(
		TEXT("sp_theatre: %d silence/loud event(s) authored on its own scenes, %d speak event(s), "
			 "%d resolving to a line file, %d of those carrying an envelope"),
		TheatreEnvelope, TheatreSpeak, TheatreLineFiles, TheatreLines));
	for (const FString& F : Faceless) { AddInfo(FString::Printf(TEXT("no mouth record: %s"), *F)); }
	for (const FString& F : NoBridge) { AddInfo(FString::Printf(TEXT("mouth but no jaw_drop: %s"), *F)); }
	for (const FString& F : SilentLines) { AddInfo(FString::Printf(TEXT("line marks nothing: %s"), *F)); }

	TestTrue(TEXT("some exported map places a scene that marks its own envelope"), NumEnvelope > 0);
	TestEqual(TEXT("every one of those events lands on an actor with a mouth record"),
		NumOnMouth, NumEnvelope);
	// sp_theatre carries no envelope of its own on any of its eleven scenes, which is why the
	// per-line join exists at all. If a re-export ever changes that, this is where it shows up.
	TestEqual(TEXT("sp_theatre's own scenes mark no envelope"), TheatreEnvelope, 0);
	TestTrue(TEXT("sp_theatre's own scenes speak"), TheatreSpeak > 0);
	TestEqual(TEXT("every one of its speak events resolves to a per-line .vcd"),
		TheatreLineFiles, TheatreSpeak);
	// The eight that mark nothing are the Prince's escort walk-out and one whispered line; every
	// line of the trial itself carries one, which is what puts the courtroom cast's jaws in motion.
	TestTrue(TEXT("and most of them carry an envelope in it"), TheatreLines * 2 > TheatreSpeak);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
