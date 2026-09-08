// The facial flex chain. Two tiers, because the chain has two halves.
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
#include "ElysiumChoreoSettings.h"
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEyeTuningConfig.h"
#include "ElysiumLineService.h"
#include "Substrate/ElysiumLipTrack.h"
#include "Substrate/ElysiumSceneData.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation, the assets half of a selection
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumPoseDeviation.h"

#include "Animation/AnimCurveMetadata.h"
#include "Animation/AnimSequence.h"
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
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
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

	// The eyeball record that closes `GTestRigJson`'s lid family, in the exported sidecar's own shape.
	// Its `uppertarget` triple is the same three numbers the rig's ramps hinge on, which is how the
	// shipped models are authored — so the record and the `FElysiumFlexLid` reconstruction are two
	// independent derivations of one value and must agree wherever exactly one lid state is held.
	//
	// `up`/`forward` are stated already orthonormal, as every shipped record is, and `LoadJsonText`
	// leaves them in the file's own space — no import transform, no skeleton.
	const TCHAR* const GTestEyeJson = TEXT(R"JSON(
	{
	  "stem": "testrig",
	  "eyeballs": [
	    {"index":0, "bone":"Bip01 Head", "bone_index":6,
	     "org":[0.0,0.0,0.0], "up":[0.0,0.0,1.0], "forward":[1.0,0.0,0.0],
	     "zoffset":0.0, "radius":0.5, "iris_scale":2.0,
	     "upperflexdesc":[1,2,3], "uppertarget":[-0.16,0.208,0.298],
	     "upperlidflexdesc":0, "material":"eyeball_r"}
	  ]
	}
	)JSON");

	// Solve the first record's basis against a gaze point and hand the lid half on, exactly as the
	// per-frame pass does — `FromRecord` is the shared seam, so a divergence between the two cannot
	// hide here.
	FElysiumEyeInput AimEye(const FElysiumEyeSet& Set, const FVector& Target, bool bEyeMove,
		float Blink = 0.f)
	{
		FElysiumEyeTuning Tuning;
		Tuning.bEyeMove = bEyeMove;
		FElysiumEyeState State;
		ElysiumEyes::BuildState(Set.Eyeballs[0], FTransform::Identity, Target, Tuning, State);
		FElysiumEyeInput Input;
		Input.Eyes[0].FromRecord(Set.Eyeballs[0], State);
		Input.Blink = Blink;
		return Input;
	}

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
				|| !ElysiumNpcVisual::IsStemBaked(Pair.Key)
				|| !IFileManager::Get().FileExists(*ElysiumNativeTest::SourcePath(Pair.Key)))
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


// The target ramp — `R_StudioFlexVerts`' trapezoid, against hand-computed values.


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


// The rule machine and the lid combine, over the hand-written rig.


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


// The amplitude jaw — the rig's second input, and its precedence against the rule layer.

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


// The eye basis — `R_StudioEyeballPosition`, without a mesh under it.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEyeSolveTest, "Elysium.Substrate.EyeSolve", GElysiumFacialTestFlags)
bool FElysiumEyeSolveTest::RunTest(const FString&)
{
	FElysiumEyeSet Set;
	FString Error;
	if (!TestTrue(TEXT("the test eye record parses"), Set.LoadJsonText(GTestEyeJson, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("one record"), Set.Eyeballs.Num(), 1);
	TestTrue(TEXT("found by its material name, case-insensitively"),
		Set.FindByMaterial(TEXT("EYEBALL_R")) == &Set.Eyeballs[0]);
	TestTrue(TEXT("and by its record index"), Set.Find(0) == &Set.Eyeballs[0]);

	// --- the slot join, both spellings ------------------------------------------------------------
	// `InstallEyes` reaches a record through a material SLOT NAME. A baked mesh takes the
	// container's own material name verbatim; a slot built per LOD section carries a
	// `LOD_<lod>_Section_<index>_` prefix. Both must reduce to the same key, and the key is joined
	// case-insensitively — the sidecar lowercases what the container capitalises.
	//
	// This is asserted because its failure is silent and looks like success: an unjoined section
	// still draws the eye master, so it has a round iris of the master's default texture that simply
	// never aims and never blinks.
	TestEqual(TEXT("a baked slot is already the material name"),
		ElysiumEyes::MaterialNameFromSlot(TEXT("Eyeball_r")), FString(TEXT("Eyeball_r")));
	TestEqual(TEXT("a sectioned slot drops its LOD/section prefix"),
		ElysiumEyes::MaterialNameFromSlot(TEXT("LOD_0_Section_2_eyeball_r")),
		FString(TEXT("eyeball_r")));
	TestEqual(TEXT("a multi-digit section index is consumed whole"),
		ElysiumEyes::MaterialNameFromSlot(TEXT("LOD_0_Section_11_eyeball_r")),
		FString(TEXT("eyeball_r")));
	TestTrue(TEXT("the baked slot spelling joins the record"),
		Set.FindByMaterial(ElysiumEyes::MaterialNameFromSlot(TEXT("Eyeball_r"))) == &Set.Eyeballs[0]);
	TestTrue(TEXT("and so does the sectioned spelling"),
		Set.FindByMaterial(ElysiumEyes::MaterialNameFromSlot(TEXT("LOD_0_Section_2_eyeball_r")))
			== &Set.Eyeballs[0]);
	TestTrue(TEXT("a body section is not mistaken for an eye"),
		Set.FindByMaterial(ElysiumEyes::MaterialNameFromSlot(TEXT("ninesheadnew"))) == nullptr);

	const FElysiumEyeball& Eye = Set.Eyeballs[0];
	FElysiumEyeTuning Tuning;
	FElysiumEyeState State;

	// --- the resting aim, and its sign ---------------------------------------------------------
	// `bEyeMove` off is a real retail configuration, and the record stores the resting aim NEGATED:
	// drop the sign and the whole cast looks out of the back of its head, which is invisible until
	// someone stands in front of one.
	Tuning.bEyeMove = false;
	ElysiumEyes::BuildState(Eye, FTransform::Identity, FVector(100.f, 0.f, 0.f), Tuning, State);
	TestTrue(TEXT("the resting basis solves"), State.bValid);
	TestTrue(TEXT("resting forward is the record's own, negated"),
		State.Forward.Equals(FVector(-1.f, 0.f, 0.f), 1e-4f));
	TestTrue(TEXT("and the target is ignored with bEyeMove off"),
		State.Forward.X < 0.f);
	TestTrue(TEXT("up is the record's up"), State.Up.Equals(FVector(0.f, 0.f, 1.f), 1e-4f));

	// The bone-local pair the eyelid bridge consumes. Under an identity bone these are the record's
	// own vectors back again, which is the only configuration where the two spaces are checkable
	// against each other by inspection.
	TestTrue(TEXT("UpLocal is the authored up"), State.UpLocal.Equals(Eye.Up, 1e-4f));
	TestEqual(TEXT("the lid axis and the aim are orthogonal at rest"),
		static_cast<float>(FVector::DotProduct(State.ForwardLocal, Eye.Up)), 0.f, 1e-4f);

	// --- the iris plane scale is an inverse length ---------------------------------------------
	// `1/(1/iris_scale + EyeSize)` is in eyeball-unit⁻¹, so it reaches centimetres by DIVISION.
	// Multiplying instead is a 6.45x error that still draws a perfectly round iris.
	const float Expected = (1.f / (1.f / 2.f)) / ElysiumEyes::UnitsToCm;
	TestEqual(TEXT("|IrisU| divides by the unit scale"),
		static_cast<float>(State.IrisU.Size()), Expected, 1e-4f);
	TestEqual(TEXT("|IrisV| matches it"),
		static_cast<float>(State.IrisV.Size()), Expected, 1e-4f);
	TestEqual(TEXT("the plane axes are perpendicular"),
		static_cast<float>(FVector::DotProduct(State.IrisU, State.IrisV)), 0.f, 1e-4f);
	// EyeSize widens the iris by *shrinking* the inverse length.
	Tuning.EyeSize = 0.25f;
	ElysiumEyes::BuildState(Eye, FTransform::Identity, FVector::ZeroVector, Tuning, State);
	TestEqual(TEXT("EyeSize enters as an inverse"), static_cast<float>(State.IrisU.Size()),
		(1.f / (1.f / 2.f + 0.25f)) / ElysiumEyes::UnitsToCm, 1e-4f);
	Tuning.EyeSize = 0.f;

	// --- the two origins are two parameters -----------------------------------------------------
	// The shift moves the iris planes and deliberately not the shading normal's origin. They
	// coincide at the shipped default, so collapsing them into one is invisible until the knob moves.
	ElysiumEyes::BuildState(Eye, FTransform::Identity, FVector::ZeroVector, Tuning, State);
	TestTrue(TEXT("unshifted, the two origins coincide"), State.Org.Equals(State.NormalOrg, 1e-5f));
	FElysiumEyeSet Shifted;
	Shifted.LoadJsonText(GTestEyeJson, Error);
	Shifted.Eyeballs[0].Org = FVector(2.f, -3.f, 0.f);
	Tuning.EyeShift = FVector(0.5f, 0.5f, 0.f);
	ElysiumEyes::BuildState(Shifted.Eyeballs[0], FTransform::Identity, FVector::ZeroVector, Tuning, State);
	// By the sign of each component, so a mirrored pair moves apart rather than both one way.
	TestTrue(TEXT("the shift follows each component's sign"),
		State.Org.Equals(FVector(2.5f, -3.5f, 0.f), 1e-4f));
	TestTrue(TEXT("and the shading origin does not take it"),
		State.NormalOrg.Equals(FVector(2.f, -3.f, 0.f), 1e-4f));
	Tuning.EyeShift = FVector::ZeroVector;

	// --- looking at something --------------------------------------------------------------------
	Tuning.bEyeMove = true;
	ElysiumEyes::BuildState(Eye, FTransform::Identity, FVector(0.f, 50.f, 0.f), Tuning, State);
	TestTrue(TEXT("the eye turns to the target"), State.Forward.Equals(FVector(0.f, 1.f, 0.f), 1e-4f));
	TestTrue(TEXT("the basis stays right-handed"),
		FVector::CrossProduct(State.Right, State.Forward).Equals(State.Up, 1e-4f));

	// A target on the up axis has no basis to build — `forward` and `up` are parallel there, so the
	// solve reports invalid rather than handing on a degenerate frame.
	ElysiumEyes::BuildState(Eye, FTransform::Identity, FVector(0.f, 0.f, 50.f), Tuning, State);
	TestFalse(TEXT("a target straight along up is degenerate"), State.bValid);

	// --- zoffset shifts the aim sideways, in the plane the lid axis is normal to ------------------
	FElysiumEyeSet Offset;
	Offset.LoadJsonText(GTestEyeJson, Error);
	Offset.Eyeballs[0].ZOffset = 0.1f;
	ElysiumEyes::BuildState(Offset.Eyeballs[0], FTransform::Identity, FVector(0.f, 50.f, 0.f), Tuning, State);
	TestTrue(TEXT("the nudged aim is still a unit vector"),
		FMath::IsNearlyEqual(static_cast<float>(State.Forward.Size()), 1.f, 1e-4f));
	TestEqual(TEXT("the nudge is sideways only — up is untouched"),
		static_cast<float>(FVector::DotProduct(State.Up, Offset.Eyeballs[0].Up)), 1.f, 1e-4f);
	TestTrue(TEXT("and the aim actually moved"), FMath::Abs(State.Forward.X) > 1e-3f);

	return true;
}

// The blink cadence's player-distance gate and its clock, and the view-target latch's rest.
//
// Retail's cadence is one gated statement in `CAI_BaseNPCTroika::MaintainEyeDirection`
// (`vampire.dll` 0x102BFF20):
//
//     if (m_flPlayerDist < _DAT_10483AAC && (m_blinkTimer -= dt) < _DAT_104454C4) {
//         vfunc 0x450;                                            // CBaseFlex::Blink, 0x100B5CE0
//         m_blinkTimer = RandomFloat(m_flMinBlink, m_flMaxBlink);  // per disposition, 2.5-6.0 s
//     }
//
// Three things follow, and each is asserted below: an NPC out of range does not blink; its
// countdown is FROZEN rather than merely unread, so approaching one does not fire a backlog; and
// the countdown is decremented by a think delta — the pausable game clock — while only the 0.3 s
// envelope is client-side.
//
// The latch is the third fidelity fact: retail recomputes the eye aim every think, so a body whose
// maintainer stopped running rests on its authored aim on the next frame instead of holding the
// last world point it was handed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEyeBlinkTest, "Elysium.Substrate.EyeBlink",
	GElysiumFacialTestFlags)
bool FElysiumEyeBlinkTest::RunTest(const FString&)
{
	constexpr float Near = 200.f;                                  // 2 m — a conversation's range
	const float Far = ElysiumEyes::BlinkPlayerDistance + 100.f;
	constexpr float Min = 2.5f;
	constexpr float Max = 6.f;
	constexpr float Step = 0.1f;

	// --- a far body does not blink, and does not bank one either ---------------------------------
	{
		FElysiumBlinkSchedule Schedule;
		float Now = 0.f;
		float Peak = 0.f;
		for (int32 i = 0; i < 200; ++i)   // 20 s, more than three of the longest intervals
		{
			Now += Step;
			Peak = FMath::Max(Peak, ElysiumEyes::AdvanceBlink(Schedule, Now, Step, Far, Min, Max,
				EElysiumBlinkCommand::Cadence));
		}
		TestEqual(TEXT("a body out of the player-distance gate never blinks"), Peak, 0.f);
		TestEqual(TEXT("...and its countdown is frozen, not run down"), Schedule.Timer, 0.f);

		// Walking up to it fires ONE blink — the first think in range — and reseeds from the
		// disposition. It does not fire the twenty seconds of blinks the gate held back, because
		// there were none held back.
		Now += Step;
		ElysiumEyes::AdvanceBlink(Schedule, Now, Step, Near, Min, Max, EElysiumBlinkCommand::Cadence);
		TestTrue(TEXT("the first in-range think blinks"), Schedule.BlinkEndsAt > Now);
		TestTrue(TEXT("...and reseeds inside the disposition's interval"),
			Schedule.Timer >= Min && Schedule.Timer <= Max);
	}

	// --- a near body blinks, and the envelope is the authored asymmetric one ----------------------
	{
		FElysiumBlinkSchedule Schedule;
		const float Toggle = 10.f;
		const float AtToggle = ElysiumEyes::AdvanceBlink(Schedule, Toggle, Step, Near, Min, Max,
			EElysiumBlinkCommand::Cadence);
		TestTrue(TEXT("a body inside the gate blinks"), Schedule.BlinkEndsAt > Toggle);
		TestEqual(TEXT("the lid is still open on the toggle frame"), AtToggle, 0.f);
		// 48 ms later the lid is shut; it reopens over the remaining quarter second.
		const float Shut = ElysiumEyes::AdvanceBlink(Schedule, Toggle + 0.05f, 0.05f, Near, Min, Max,
			EElysiumBlinkCommand::Cadence);
		TestTrue(TEXT("the lid slams shut within 50 ms of the toggle"), Shut > 0.9f);
		const float Reopened = ElysiumEyes::AdvanceBlink(Schedule, Toggle + ElysiumEyes::BlinkSeconds,
			0.25f, Near, Min, Max, EElysiumBlinkCommand::Cadence);
		TestEqual(TEXT("and is open again at the end of the 0.3 s window"), Reopened, 0.f);

		// The client half runs to completion regardless of the gate: stepping out of range mid-blink
		// does not leave a lid stuck shut.
		FElysiumBlinkSchedule Leaving;
		ElysiumEyes::AdvanceBlink(Leaving, 0.f, Step, Near, Min, Max, EElysiumBlinkCommand::Cadence);
		TestTrue(TEXT("an envelope in flight survives the player leaving"),
			ElysiumEyes::AdvanceBlink(Leaving, 0.05f, 0.05f, Far, Min, Max,
				EElysiumBlinkCommand::Cadence) > 0.9f);
	}

	// --- the pausable clock ------------------------------------------------------------------------
	// The cadence takes a GAME-clock delta, so a held world advances neither it nor the gaze fidget
	// beside it. A wall-clock deadline would slide past under the pause and fire on resume.
	{
		FElysiumBlinkSchedule Schedule;
		ElysiumEyes::AdvanceBlink(Schedule, 1.f, Step, Near, Min, Max, EElysiumBlinkCommand::Cadence);
		const float Seeded = Schedule.Timer;
		TestTrue(TEXT("the schedule is seeded"), Seeded > 0.f);
		for (int32 i = 0; i < 100; ++i)
		{
			ElysiumEyes::AdvanceBlink(Schedule, 1.f, 0.f, Near, Min, Max,
				EElysiumBlinkCommand::Cadence);
		}
		TestEqual(TEXT("a paused clock does not advance the schedule"), Schedule.Timer, Seeded);
	}

	// --- the green room's two overrides are inputs to the same schedule ----------------------------
	{
		FElysiumBlinkSchedule Schedule;
		Schedule.Timer = 4.f;
		// Force blinks a body the gate would otherwise hold silent, and leaves the cadence alone.
		TestTrue(TEXT("a forced blink ignores the distance gate"),
			ElysiumEyes::AdvanceBlink(Schedule, 0.05f, Step, Far, Min, Max,
				EElysiumBlinkCommand::Force) >= 0.f && Schedule.BlinkEndsAt > 0.05f);
		TestEqual(TEXT("a forced blink does not reset the cadence"), Schedule.Timer, 4.f);
		// Hold pins the lids open AND reseeds, so releasing does not fire the window's backlog.
		TestEqual(TEXT("a held blink pins the lid open"),
			ElysiumEyes::AdvanceBlink(Schedule, 0.06f, Step, Near, Min, Max,
				EElysiumBlinkCommand::Hold), 0.f);
		TestTrue(TEXT("...and reseeds while held"),
			Schedule.Timer >= Min && Schedule.Timer <= Max && Schedule.BlinkEndsAt == 0.f);
	}

	// --- the view-target latch rests when nothing maintains it -------------------------------------
	// `TickGaze` stamps a body it decided an aim for; `TickEyes` rests every latch the frame did not
	// stamp. A conversation that ends stops supplying a DialogPOV point, the cascade finds no
	// candidate, nothing calls `SetViewTarget`, and the iris returns to the record's authored aim
	// one frame later rather than staring at the last camera position forever.
	{
		FElysiumEyeGazeLatch Latch;
		TestFalse(TEXT("an unstamped latch has no aim"), Latch.Maintain(1));
		Latch.Set(FVector(300.f, 0.f, 160.f), 1);
		TestTrue(TEXT("the frame that stamped it aims"), Latch.Maintain(1));
		TestTrue(TEXT("...at the point it was handed"),
			Latch.Target.Equals(FVector(300.f, 0.f, 160.f)));
		// One eye frame passes with no producer.
		TestFalse(TEXT("the next frame with no producer rests the eye"), Latch.Maintain(2));
		TestFalse(TEXT("...and the latch reports no view target at all"), Latch.bHasTarget);
		// It does not come back on its own; only a fresh stamp re-aims it.
		TestFalse(TEXT("a rested latch stays rested"), Latch.Maintain(3));
		Latch.Set(FVector(10.f, 20.f, 30.f), 3);
		TestTrue(TEXT("a fresh stamp re-aims it"), Latch.Maintain(3));
	}
	return true;
}

// R4.5: the corpus-wide baseline (`UElysiumEyeTuningConfig`, `/ElysiumAuthored/Eyes/DA_EyeTuning`)
// composes additively with the Green Room's live debug nudge, and an absent asset leaves the debug
// state untouched — both are what let the asset's defaults equal today's behaviour exactly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEyeTuningComposeTest,
	"Elysium.Substrate.EyeTuningCompose", GElysiumFacialTestFlags)
bool FElysiumEyeTuningComposeTest::RunTest(const FString&)
{
	FElysiumEyeTuning DebugTuning;
	DebugTuning.EyeSize = 0.1f;
	DebugTuning.EyeShift = FVector(0.5f, -0.25f, 0.f);
	DebugTuning.bEyeMove = false;

	const FElysiumEyeTuning NoAsset = ElysiumEyes::ComposeTuning(DebugTuning, nullptr);
	TestEqual(TEXT("a null config leaves EyeSize untouched"), NoAsset.EyeSize, DebugTuning.EyeSize);
	TestTrue(TEXT("a null config leaves EyeShift untouched"),
		NoAsset.EyeShift.Equals(DebugTuning.EyeShift, 1e-6f));
	TestFalse(TEXT("a null config leaves bEyeMove untouched"), NoAsset.bEyeMove);

	UElysiumEyeTuningConfig* Config = NewObject<UElysiumEyeTuningConfig>();
	Config->EyeSize = 0.2f;
	Config->EyeShift = FVector(1.0f, 0.0f, 0.5f);
	const FElysiumEyeTuning Composed = ElysiumEyes::ComposeTuning(DebugTuning, Config);
	TestEqual(TEXT("EyeSize adds"), Composed.EyeSize, 0.3f, 1e-6f);
	TestTrue(TEXT("EyeShift adds component-wise"),
		Composed.EyeShift.Equals(FVector(1.5f, -0.25f, 0.5f), 1e-6f));
	TestFalse(TEXT("bEyeMove is the debug state's alone"), Composed.bEyeMove);

	// Today's shipped defaults are neutral on both sides, so an unedited asset composes to exactly
	// the unedited debug state — the round-trip a "values = today's defaults exactly" claim rests on.
	UElysiumEyeTuningConfig* Defaults = NewObject<UElysiumEyeTuningConfig>();
	const FElysiumEyeTuning Neutral = ElysiumEyes::ComposeTuning(FElysiumEyeTuning(), Defaults);
	TestEqual(TEXT("default asset + default debug state composes to zero size"), Neutral.EyeSize, 0.f);
	TestTrue(TEXT("default asset + default debug state composes to zero shift"),
		Neutral.EyeShift.Equals(FVector::ZeroVector, 1e-6f));

	return true;
}


// The eyelid bridge — the record's write-back, against the reconstruction it supersedes.

//
// The four eyelid rules compute weights no morph reads, and the lid morphs hang off flexdescs no
// rule computes. `StudioEyeball` is the authored bridge between them, and the failure mode is that
// every wrong version of it still moves a lid: reading `uppertarget` as radians moves one, dropping
// the aim term moves one, and running the pass before the rules instead of after moves one. So the
// assertions below pin the resting *value*, not the presence of motion.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEyeLidTest, "Elysium.Substrate.EyeLids", GElysiumFacialTestFlags)
bool FElysiumEyeLidTest::RunTest(const FString&)
{
	FElysiumFacialRig Rig;
	FElysiumEyeSet Set;
	FString Error;
	if (!TestTrue(TEXT("the test rig parses"), Rig.LoadJsonText(GTestRigJson, Error))
		|| !TestTrue(TEXT("the test eye record parses"), Set.LoadJsonText(GTestEyeJson, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the rig reconstructs exactly one lid"), Rig.Lids.Num(), 1);
	TestEqual(TEXT("hinged where the two ramps meet"), Rig.Lids[0].NeutralAngle, 0.208f, 1e-5f);

	TArray<float> Controllers;
	Controllers.AddZeroed(Rig.Controllers.Num());
	TArray<float> Flex, Morph;
	const FElysiumJawInput NoJaw;

	// --- at rest the record and the reconstruction agree, exactly on the hinge -------------------
	// The record says `radius * sin(asin(0.208/radius))` and the reconstruction says `1 x 0.208`.
	// They are two independent derivations — one off the eyeball record, one off the morph ramps —
	// so their agreement is a free check on the whole recovery. Reading the targets as radians would
	// give `0.5 * sin(0.208)` = 0.1032 here, which still moves a lid and still looks like a face.
	const FElysiumEyeInput Rest = AimEye(Set, FVector::ZeroVector, /*bEyeMove=*/false);
	TestTrue(TEXT("the resting aim is usable"), Rest.Eyes[0].bValid);
	Rig.Evaluate(Controllers, NoJaw, Rest, Flex, Morph);
	TestEqual(TEXT("the resting lid lands on the neutral target"), Flex[FdLid], 0.208f, 1e-4f);
	TestEqual(TEXT("which is exactly the reconstruction's answer"),
		Flex[FdLid], Rig.Lids[0].NeutralAngle, 1e-4f);
	// And the hinge is the point of it: both ramps read zero there, so a face at rest is still.
	TestEqual(TEXT("the low lid morph is off at rest"), Morph[MorphLid], 0.f, 1e-3f);
	TestEqual(TEXT("the high lid morph is off at rest"), Morph[MorphLidHigh], 0.f, 1e-3f);

	// --- the lid follows the eye -----------------------------------------------------------------
	// The property that says the pass is coupled to the gaze at all: identical controllers, three
	// aims, three answers. `dot(v, up)` is `radius * sin(sum + elevation)`, so an eye looking up
	// carries its upper lid up with it.
	const FElysiumEyeInput Up = AimEye(Set, FVector(10.f, 0.f, 5.f), /*bEyeMove=*/true);
	Rig.Evaluate(Controllers, NoJaw, Up, Flex, Morph);
	const float LidUp = Flex[FdLid];
	TestEqual(TEXT("looking 26.6 degrees up raises the lid"), LidUp, 0.38942f, 1e-4f);
	TestEqual(TEXT("far enough to saturate the high ramp"), Morph[MorphLidHigh], 1.f, 1e-4f);

	const FElysiumEyeInput Down = AimEye(Set, FVector(10.f, 0.f, -5.f), /*bEyeMove=*/true);
	Rig.Evaluate(Controllers, NoJaw, Down, Flex, Morph);
	TestEqual(TEXT("and looking down lowers it symmetrically"), Flex[FdLid], -0.01729f, 1e-4f);
	TestTrue(TEXT("the two aims differ"), FMath::Abs(LidUp - Flex[FdLid]) > 0.1f);

	// --- a blink, at rest ------------------------------------------------------------------------
	// `blink` is assigned raw ahead of the rules, which drive the lowerer to 1 and everything else to
	// 0; the record then places the lid on its lowered offset, which is exactly where the low ramp
	// saturates. A blink that reached the flexdesc any other way would not land on that number.
	const FElysiumEyeInput Blink = AimEye(Set, FVector::ZeroVector, /*bEyeMove=*/false, /*Blink=*/1.f);
	Rig.Evaluate(Controllers, NoJaw, Blink, Flex, Morph);
	TestEqual(TEXT("a blink drives the lowerer alone"), Flex[FdLowerer], 1.f, 1e-5f);
	TestEqual(TEXT("and cancels the neutral"), Flex[FdNeutral], 0.f, 1e-5f);
	TestEqual(TEXT("the lid lands on its lowered offset"), Flex[FdLid], -0.16f, 1e-4f);
	TestEqual(TEXT("closing the eye"), Morph[MorphLid], 1.f, 1e-4f);
	TestEqual(TEXT("with the high morph off"), Morph[MorphLidHigh], 0.f, 1e-4f);

	// --- the record supersedes the reconstruction, per flexdesc ----------------------------------
	// Re-authored so the two disagree: the record now says 0.30 where the ramps still hinge at 0.208.
	// Whichever one wins is visible in the flexdesc, and the record must.
	FElysiumEyeSet Moved;
	Moved.LoadJsonText(GTestEyeJson, Error);
	Moved.Eyeballs[0].UpperTarget[1] = 0.30f;
	FElysiumEyeInput MovedRest = AimEye(Moved, FVector::ZeroVector, /*bEyeMove=*/false);
	Rig.Evaluate(Controllers, NoJaw, MovedRest, Flex, Morph);
	TestEqual(TEXT("the record's own target wins"), Flex[FdLid], 0.30f, 1e-4f);

	// Off, the reconstruction is what stands — the A/B, and the state of every body whose model
	// carries no record at all.
	MovedRest.bWriteLids = false;
	Rig.Evaluate(Controllers, NoJaw, MovedRest, Flex, Morph);
	TestEqual(TEXT("with the pass off the reconstruction stands"), Flex[FdLid], 0.208f, 1e-4f);

	// A record naming a flexdesc this rig does not have must write nothing rather than anything.
	FElysiumEyeSet Foreign;
	Foreign.LoadJsonText(GTestEyeJson, Error);
	Foreign.Eyeballs[0].UpperLidFlexDesc = 99;
	Rig.Evaluate(Controllers, NoJaw, AimEye(Foreign, FVector::ZeroVector, false), Flex, Morph);
	TestEqual(TEXT("an out-of-range lid flexdesc leaves the reconstruction alone"),
		Flex[FdLid], 0.208f, 1e-4f);

	// --- order: the pass runs after the rules ----------------------------------------------------
	// Rules first, then the eye pass, is the recovered order. Reversed, the rule layer's own lid
	// reconstruction recomputes the flexdesc and the record's answer is gone — which is why the two
	// orders have to be told apart on a record whose targets differ from the ramps'.
	MovedRest.bWriteLids = true;
	Rig.EvalFlexWeights(Controllers, Flex);
	TestEqual(TEXT("the rule layer alone leaves the reconstruction"), Flex[FdLid], 0.208f, 1e-4f);
	Rig.ApplyEyesToFlexWeights(MovedRest, Flex);
	TestEqual(TEXT("rules then eyes: the record stands"), Flex[FdLid], 0.30f, 1e-4f);

	Rig.ApplyEyesToFlexWeights(MovedRest, Flex);
	Rig.EvalFlexWeights(Controllers, Flex);
	TestEqual(TEXT("eyes then rules: the record is lost"), Flex[FdLid], 0.208f, 1e-4f);

	// --- the blink envelope ------------------------------------------------------------------------
	// Not a symmetric curve: shut in 48 ms, open over the remaining quarter second.
	TestEqual(TEXT("open at the top of the window"),
		ElysiumEyes::BlinkWeight(ElysiumEyes::BlinkSeconds), 0.f, 1e-3f);
	TestEqual(TEXT("open at the bottom"), ElysiumEyes::BlinkWeight(0.f), 0.f);
	TestEqual(TEXT("open outside it"), ElysiumEyes::BlinkWeight(0.5f), 0.f);
	TestEqual(TEXT("open before it"), ElysiumEyes::BlinkWeight(-0.1f), 0.f);
	// Fully closed where `2*sqrt(cos a)` reaches 1, i.e. `cos a = 0.25`.
	const float ClosedRemaining = FMath::Acos(0.25f) / ElysiumEyes::BlinkRate;
	TestEqual(TEXT("fully closed 48 ms in"), ElysiumEyes::BlinkWeight(ClosedRemaining), 1.f, 1e-3f);
	TestEqual(TEXT("which is 48.3 ms after the toggle"),
		ElysiumEyes::BlinkSeconds - ClosedRemaining, 0.0483f, 1e-3f);
	// Monotone on each side of that peak, which is what makes it read as a blink rather than a twitch.
	for (int32 i = 1; i <= 8; ++i)
	{
		const float A = ClosedRemaining + (ElysiumEyes::BlinkSeconds - ClosedRemaining) * (i / 8.f);
		const float B = ClosedRemaining * (1.f - i / 8.f);
		TestTrue(FString::Printf(TEXT("closing is monotone at step %d"), i),
			ElysiumEyes::BlinkWeight(A) <= ElysiumEyes::BlinkWeight(ClosedRemaining));
		TestTrue(FString::Printf(TEXT("opening is monotone at step %d"), i),
			ElysiumEyes::BlinkWeight(B) <= ElysiumEyes::BlinkWeight(ClosedRemaining));
	}

	return true;
}


// The whole chain on a real body: a controller write reaching the component's morph weights.

//
// Everything above this point checks one link. This drives the assembled thing — a registered
// skeletal-mesh component running UElysiumBipedAnimInstance over the real mesh — and reads the answer
// off USkeletalMeshComponent::MorphTargetWeights, which is what the skinning actually consumes. It
// is the check that would otherwise only exist in a running game: the anim-curve route from the
// proxy's Evaluate to a morph weight is silent when it fails.

bool // The Faceposer weight table — the reader, against a hand-written fixture.

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


// The wire: a scene's expression events composed onto an actor's flex controllers.

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


// The wire: a scene's silence/loud envelope composed onto its actors' jaws.

//
// Content-free — two inline `.vcd`s and the recording embodiment standing in for a rig. What it
// covers is the arithmetic over the scene clock that is silent when it is wrong: the three levels
// and their precedence, the fact that the jaw runs on UNOFFSET scene time while the speak event that
// carries it was dispatched a mixahead early, the envelope borrowed from a line's own `.vcd`, the
// lag between levels, and the return to a shut mouth when the scene ends.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneJawTest, "Elysium.Substrate.SceneJaw", GElysiumFacialTestFlags)
bool FElysiumSceneJawTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("scene 'test/nolineenvelope.vcd' not found"),
		EAutomationExpectedErrorFlags::Contains, 1);
	// Exact levels rather than an asymptote: the lag has its own section below. R4.5 moved these
	// off cvars onto `UElysiumChoreoSettings`; the CDO is mutated and restored the same way
	// SwapFloatCVar used to swap the cvar.
	UElysiumChoreoSettings* ChoreoSettings = GetMutableDefault<UElysiumChoreoSettings>();
	const float WasSmoothing = ChoreoSettings->JawSmoothing;
	const float WasSpeech = ChoreoSettings->JawSpeechLevel;
	ChoreoSettings->JawSmoothing = 0.f;
	ChoreoSettings->JawSpeechLevel = 0.5f;
	ON_SCOPE_EXIT
	{
		ChoreoSettings->JawSmoothing = WasSmoothing;
		ChoreoSettings->JawSpeechLevel = WasSpeech;
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
	// A speak event is dispatched a lead early so its sample is HEARD at the authored instant; the
	// jaw below runs on the unoffset clock. The lead is normally the audio path's own, so
	// it is pinned here rather than inherited — this test is about the jaw, not about the device.
	Services.OutputLead = 0.1f;

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
	// The audio lead pulls a speak event's start 0.1 s earlier here so the sample is HEARD at the
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
	ChoreoSettings->JawSmoothing = 0.05f;
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


// The theatre's own scenes: every expression event, end to end, against the real export.

//
// The acceptance corpus. For each `expression` event on each of sp_theatre's twelve scenes, the
// three joins have to hold: `param` names a table on disk, `param2` names a row in it, and every
// key of that row is a flex controller on the rig of the model the map's own actor entity carries.
// An unresolvable reference here is a finding about the export or the map data, not a case to skip,
// so each one is reported by name.

bool // The amplitude jaw against the real export: every envelope, and the face it has to reach.

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

bool // Lipsync: the `.lip` reader, the recovered envelope, and the scene driver.


namespace
{
	// `facial_animation.md`'s own worked example, plus the two shapes a reader gets wrong silently:
	// a five-field row (version 1.0/1.1 omit the trailing flag; 11,331 rows corpus-wide) and a row
	// whose time is not a number, which must be counted rather than read as zero.
	const TCHAR* const GTestLipText = TEXT(
		"VERSION 1.2\r\n"
		"PLAINTEXT\r\n"
		"{\r\n"
		"Out, devil!\r\n"
		"}\r\n"
		"WORDS\r\n"
		"{\r\n"
		"WORD Out 0.140 0.578\r\n"
		"{\r\n"
		"593 aw 0.140 0.437 1.000 0\r\n"
		"116 t 0.437 0.578 1.000\r\n"
		"999 zz notanumber 0.500 1.000 0\r\n"
		"}\r\n"
		"WORD devil 0.702 1.155\r\n"
		"{\r\n"
		"100 d 0.702 0.900 1.000 0\r\n"
		"618 ih 0.900 1.155 1.000 0\r\n"
		"}\r\n"
		// Faceposer writes the caption's own punctuation into the word text. A quote-aware tokenizer
		// swallows this line and loses both phonemes under it — 1,159 files corpus-wide.
		"WORD \"I'll 1.200 1.400\r\n"
		"{\r\n"
		"593 ay 1.200 1.300 1.000 0\r\n"
		"108 l 1.300 1.400 1.000 0\r\n"
		"}\r\n"
		"}\r\n"
		"EMPHASIS\r\n"
		"{\r\n"
		"}\r\n"
		"CLOSECAPTION\r\n"
		"{\r\n"
		"english\r\n"
		"{\r\n"
		"PHRASE char 13 \"Out, devil!\" 0.140 1.155\r\n"
		"}\r\n"
		"}\r\n"
		"OPTIONS\r\n"
		"{\r\n"
		"voice_duck 1\r\n"
		"speaker_name Bach\r\n"
		"}\r\n");

	// Two abutting phonemes chosen so the envelope's numbers come out exact against the shipped
	// filter pair (0.080, 0.100): `aw` spans 0.200 (clamps to the 0.100 maximum) and `t` spans 0.050
	// (clamps up to the 0.080 minimum, so its peak is 0.050/0.080 = 0.625, not 1).
	const TCHAR* const GEnvelopeLipText = TEXT(
		"VERSION 1.2\r\n"
		"WORDS\r\n"
		"{\r\n"
		"WORD out 1.000 1.250\r\n"
		"{\r\n"
		"593 aw 1.000 1.200 1.000 0\r\n"
		"116 t 1.200 1.250 1.000 0\r\n"
		"}\r\n"
		"}\r\n");

	// The same two phonemes on the audio clock a scene event would give them.
	const TCHAR* const GSceneLipText = TEXT(
		"VERSION 1.2\r\n"
		"WORDS\r\n"
		"{\r\n"
		"WORD out 0.000 0.250\r\n"
		"{\r\n"
		"593 aw 0.000 0.200 1.000 0\r\n"
		"116 t 0.200 0.250 1.000 0\r\n"
		"}\r\n"
		"}\r\n");

	// A phoneme table over two mouth controllers.
	//
	//   `aa`  is reachable ONLY by its class code 0x0251 = 593. The `.lip` rows that carry 593 spell
	//         themselves `aw` and `ay`, neither of which is a row name here — so a string lookup
	//         finds nothing and a code lookup finds `aa`, which is the whole correction.
	//   `t`   carries a NON-ZERO value under a ZERO influence on `smile` — the shape
	//         `lacroix_phonemes`'s `r2` row actually has, and the one that separates retail's
	//         accumulate (which reads the value alone) from an influence-weighted read.
	const TCHAR* const GTestPhonemeTableText = TEXT(
		"$keys jaw_drop smile\r\n"
		"$hasweighting\r\n"
		"\"aa\" \"0x0251\" 0.800 1.000 0.000 0.000 \"reached by code 593\"\r\n"
		"\"t\" \"t\" 0.900 1.000 0.500 0.000 \"value under zero influence\"\r\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLipTrackTest,
	"Elysium.Substrate.LipTrack", GElysiumFacialTestFlags)
bool FElysiumLipTrackTest::RunTest(const FString&)
{
	FElysiumLipTrack Track;
	ElysiumLip::ParseText(GTestLipText, TEXT("test/bach.lip"), Track);

	TestTrue(TEXT("the fixture yields a usable track"), Track.bValid);
	TestEqual(TEXT("VERSION is read"), Track.Version, FString(TEXT("1.2")));
	TestEqual(TEXT("three words"), Track.Words.Num(), 3);
	TestEqual(TEXT("six usable phoneme rows"), Track.NumPhonemes(), 6);
	// The row whose start is not a number is dropped, not read as zero — a zero start would put the
	// phoneme at the head of the line and it would fire on every sample from t=0.
	TestEqual(TEXT("the malformed row is dropped and counted"), Track.NumMalformedRows, 1);
	TestEqual(TEXT("LatestTime is the last phoneme's end"), Track.LatestTime, 1.400f, 1e-4f);

	// The regression the corpus taught: a word whose text opens with a quotation mark keeps both its
	// phonemes. Quote-aware tokenization reads the whole line as one token and loses them.
	if (TestEqual(TEXT("the quote-prefixed word survives"), Track.Words.Num(), 3))
	{
		TestEqual(TEXT("its text keeps the quote"), Track.Words[2].Text, FString(TEXT("\"I'll")));
		TestEqual(TEXT("and it keeps its phonemes"), Track.Words[2].Phonemes.Num(), 2);
	}

	// The leading integer is carried, because it is the key.
	TestEqual(TEXT("the phoneme code is read"), Track.Words[0].Phonemes[0].Code, 593);
	TestEqual(TEXT("the string is carried beside it"),
		Track.Words[0].Phonemes[0].Phoneme, FString(TEXT("aw")));

	// The five-field row parses identically to the six-field one.
	if (TestEqual(TEXT("the first word keeps both its rows"), Track.Words[0].Phonemes.Num(), 2))
	{
		TestEqual(TEXT("six-field row phoneme"), Track.Words[0].Phonemes[0].Phoneme, FString(TEXT("aw")));
		TestEqual(TEXT("five-field row phoneme"), Track.Words[0].Phonemes[1].Phoneme, FString(TEXT("t")));
		TestEqual(TEXT("five-field row start"), Track.Words[0].Phonemes[1].Start, 0.437f, 1e-4f);
		TestEqual(TEXT("five-field row end"), Track.Words[0].Phonemes[1].End, 0.578f, 1e-4f);
	}
	TestEqual(TEXT("the word text is carried"), Track.Words[1].Text, FString(TEXT("devil")));

	// OPTIONS, for the debug surface. CLOSECAPTION and PLAINTEXT are read and dropped: a `}` inside
	// the caption would desync the block walk, which is what Elysium.Content.LipCorpus proves it does
	// not do across the shipped corpus.
	TestTrue(TEXT("voice_duck is read"), Track.bVoiceDuck);
	TestEqual(TEXT("speaker_name is read"), Track.SpeakerName, FString(TEXT("Bach")));

	// A file with no WORDS block moves no mouth, and says so rather than looking loaded.
	FElysiumLipTrack Empty;
	ElysiumLip::ParseText(TEXT("VERSION 1.2\r\nWORDS\r\n{\r\n}\r\n"), TEXT("empty.lip"), Empty);
	TestFalse(TEXT("a track with no phoneme is not valid"), Empty.bValid);

	// The path fold is the scene reader's, with the extension swapped — a `speak` param names a wav
	// or an mp3 and the mirror holds neither.
	TestEqual(TEXT("a speak param folds to the lip mirror key"),
		ElysiumLip::NormalizeLipRel(TEXT("Character\\dlg\\Downtown LA\\prince1\\line1015_col_f.mp3")),
		FString(TEXT("character/dlg/downtown la/prince1/line1015_col_f.lip")));
	TestEqual(TEXT("a leading sound/ is stripped as it is for scenes"),
		ElysiumLip::NormalizeLipRel(TEXT("sound/CINEMATIC/x.wav")), FString(TEXT("cinematic/x.lip")));
	// The dialogue half derives its path from the line service's own rule, which returns a stem with
	// NO extension — so the fold has to append one rather than replace one.
	TestEqual(TEXT("a dialogue turn's extension-less source gains .lip"),
		ElysiumLip::NormalizeLipRel(
			FElysiumLineService::DialogueLineSource(TEXT("dlg/Downtown LA/prince1.dlg"), 1001)),
		FString(TEXT("character/dlg/downtown la/prince1/line1001_col_e.lip")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLipSampleTest,
	"Elysium.Substrate.LipSample", GElysiumFacialTestFlags)
bool FElysiumLipSampleTest::RunTest(const FString&)
{
	FElysiumLipTrack Track;
	ElysiumLip::ParseText(GEnvelopeLipText, TEXT("test/envelope.lip"), Track);
	if (!TestTrue(TEXT("the envelope fixture parses"), Track.bValid))
	{
		return false;
	}

	// One of the two shipped rigged pairs; the fixture's spans are chosen against it so the
	// envelope's numbers come out exact.
	const float Lo = 0.08f, Hi = 0.10f;
	TArray<FElysiumLipSample> Live;

	auto ScaleOf = [&](float T, const TCHAR* Phoneme) -> float
	{
		Track.SampleAt(T, Lo, Hi, Live);
		for (const FElysiumLipSample& S : Live)
		{
			if (S.Phoneme->Phoneme == Phoneme) { return S.Scale; }
		}
		return 0.f;
	};

	// --- the lead-in happens BEFORE the phoneme's authored start ------------------------------
	// `aw` starts at 1.000 with S = 0.100, so its window opens at 0.900. A reader that ramped inside
	// the span would have it silent here and at full weight in the middle of the span instead.
	Track.SampleAt(0.89f, Lo, Hi, Live);
	TestEqual(TEXT("t=0.89: before the window, nothing is live"), Live.Num(), 0);
	Track.SampleAt(0.90f, Lo, Hi, Live);
	TestEqual(TEXT("t=0.90: the window opens exactly at start-S, still at zero"), Live.Num(), 0);
	TestEqual(TEXT("t=0.95: halfway up the lead-in"), ScaleOf(0.95f, TEXT("aw")), 0.5f, 1e-4f);

	// --- peak at the authored start, then a linear decay ending at the authored end ------------
	TestEqual(TEXT("t=1.00: a long phoneme reaches full weight at its start"),
		ScaleOf(1.00f, TEXT("aw")), 1.f, 1e-4f);
	TestEqual(TEXT("t=1.15: decaying"), ScaleOf(1.15f, TEXT("aw")), 0.5f, 1e-4f);
	TestEqual(TEXT("t=1.20: zero exactly at the authored end"), ScaleOf(1.20f, TEXT("aw")), 0.f);

	// --- a span shorter than S never reaches 1 -------------------------------------------------
	// `t` spans 0.050 and S clamps up to 0.080, so its peak is 0.625. This is the case a symmetric
	// min(D, span/2) reconstruction gets wrong: that one reaches 1.0 on every phoneme.
	TestEqual(TEXT("t=1.20: the short phoneme peaks at span/S, not 1"),
		ScaleOf(1.20f, TEXT("t")), 0.625f, 1e-4f);
	TestEqual(TEXT("t=1.25: and is zero at its authored end"), ScaleOf(1.25f, TEXT("t")), 0.f);

	// --- abutting spans overlap, because one's lead-in runs under the other's decay -------------
	Track.SampleAt(1.15f, Lo, Hi, Live);
	TestEqual(TEXT("t=1.15: both phonemes are live at once"), Live.Num(), 2);
	TestEqual(TEXT("t=1.15: the next phoneme is already leading in"),
		ScaleOf(1.15f, TEXT("t")), 0.375f, 1e-4f);

	// --- the accumulate: value-only, additive, clamped -----------------------------------------
	FElysiumExpressionTable Table;
	FString Error;
	if (!TestTrue(TEXT("the phoneme table parses"),
		Table.ParseText(GTestPhonemeTableText, TEXT("testface_phonemes"), Error)))
	{
		AddError(Error);
		return false;
	}

	// The correction the corpus forced: the row is reached by CODE, through the class column. The
	// `.lip` spells code 593 as `aw` here and as `ay` elsewhere, and neither is a row name.
	TestEqual(TEXT("the class column becomes a code"), Table.Rows[0].PhonemeCode, 593);
	TestEqual(TEXT("code 593 resolves to the 'aa' row"), Table.FindRowByPhonemeCode(593), 0);
	TestEqual(TEXT("a single-character class is its own code point"),
		Table.FindRowByPhonemeCode(static_cast<int32>('t')), 1);
	TestEqual(TEXT("and the string the .lip spells it with names no row at all"),
		Table.FindRow(TEXT("aw")), INDEX_NONE);

	FElysiumLipSyncBinding Binding;
	Binding.Track = MakeShared<FElysiumLipTrack>(Track);
	Binding.Table = MakeShared<FElysiumExpressionTable>(Table);
	// Pinned to this test's own pair rather than left on the default, so the accumulate's numbers
	// follow from the same S the sampling above used.
	Binding.BlendMin = Lo;
	Binding.BlendMax = Hi;
	TestTrue(TEXT("the binding is usable"), Binding.IsValid());

	TMap<FString, float> Pose;
	TArray<FString> Unresolved;
	TestEqual(TEXT("both live phonemes contribute"),
		Binding.Accumulate(1.15f, Pose, &Unresolved), 2);
	TestEqual(TEXT("every phoneme resolved to a row"), Unresolved.Num(), 0);
	// 0.5 x 0.800 + 0.375 x 0.900. A blend rather than a sum would read 0.900 here.
	TestEqual(TEXT("the two phonemes SUM on the key they share"),
		Pose.FindRef(TEXT("jaw_drop")), 0.7375f, 1e-4f);
	// `t`'s smile value is 0.500 under influence 0.000. Retail's accumulate never reads the
	// influence column, so this is 0.375 x 0.500 and NOT zero.
	TestEqual(TEXT("a value under zero influence still contributes"),
		Pose.FindRef(TEXT("smile")), 0.1875f, 1e-4f);

	// Composed on top of an expression that already claimed the key, the sum clamps rather than
	// running past 1 — which is what the controller min/max would do to it downstream anyway.
	Pose.Reset();
	Pose.Add(TEXT("jaw_drop"), 0.6f);
	Binding.Accumulate(1.15f, Pose, nullptr);
	TestEqual(TEXT("accumulation onto an expression clamps at 1"),
		Pose.FindRef(TEXT("jaw_drop")), 1.f, 1e-4f);

	// An instant with nothing live writes nothing at all, so the mouth relaxes to whatever else is
	// driving it rather than being pinned to zero by the phoneme track.
	Pose.Reset();
	TestEqual(TEXT("past the line, nothing contributes"), Binding.Accumulate(2.f, Pose, nullptr), 0);
	TestEqual(TEXT("and nothing is written"), Pose.Num(), 0);

	// A phoneme string the table does not carry comes back by name rather than being dropped.
	FElysiumLipTrack Junk;
	ElysiumLip::ParseText(
		TEXT("VERSION 1.2\r\nWORDS\r\n{\r\nWORD x 0.000 0.100\r\n{\r\n1 ??? 0.000 0.100 1.000 0\r\n}\r\n}\r\n"),
		TEXT("junk.lip"), Junk);
	FElysiumLipSyncBinding JunkBinding;
	JunkBinding.Track = MakeShared<FElysiumLipTrack>(Junk);
	JunkBinding.Table = Binding.Table;
	Unresolved.Reset();
	Pose.Reset();
	JunkBinding.Accumulate(0.05f, Pose, &Unresolved);
	TestEqual(TEXT("an unknown phoneme is reported"), Unresolved.Num(), 1);
	// Reported as code/string, because the code is what failed to resolve and the string is what a
	// reader would recognise in the file.
	TestTrue(TEXT("and it names both halves"), Unresolved.Contains(TEXT("1/???")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneLipsyncTest,
	"Elysium.Substrate.SceneLipsync", GElysiumFacialTestFlags)
bool FElysiumSceneLipsyncTest::RunTest(const FString&)
{
	ElysiumExpressions::ClearCache();
	ElysiumLip::ClearCache();
	// The resolver asks for the SPEAKER'S model stem, so the table has to be seeded under that stem
	// rather than under the table's own file name.
	ElysiumExpressions::RegisterInline(TEXT("testface"), GTestPhonemeTableText);
	ElysiumLip::RegisterInline(TEXT("test/line1.wav"), GSceneLipText);
	// A cached negative for the per-line `.vcd` the jaw looks for, so this test exercises only the
	// phoneme track and does not warn about a scene it was never going to find.
	ElysiumScene::RegisterInline(TEXT("test/line1.vcd"), TEXT(""));

	// One actor, one line, authored to start at scene time 1.0.
	const FString SceneText =
		TEXT("// Choreo version 1\n")
		TEXT("actor \"Face\"\n{\n")
		TEXT("  channel \"VO\"\n  {\n")
		TEXT("    event speak \"line1\"\n    {\n")
		TEXT("      time 1.000000 3.000000\n")
		TEXT("      param \"test/line1.wav\"\n")
		TEXT("    }\n")
		TEXT("  }\n")
		TEXT("}\n")
		TEXT("fps 60\nsnap off\n");
	ElysiumScene::RegisterInline(TEXT("test/lipsync.vcd"), SceneText);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	FElysiumEntityDef S;
	S.Classname = TEXT("logic_choreographed_scene");
	S.TargetName = TEXT("scene1");
	S.Keys.Add(TEXT("SceneFile"), TEXT("test/lipsync.vcd"));
	Defs.Defs.Add(MoveTemp(S));

	FElysiumEntityDef Face;
	Face.Classname = TEXT("npc_VVampire");
	Face.TargetName = TEXT("Face");
	Face.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/testface.mdl"));
	Defs.Defs.Add(MoveTemp(Face));

	FElysiumRecordingServices Services;
	Services.FlexControllers = { TEXT("jaw_drop"), TEXT("smile") };

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	double T = 0.0;
	World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
	World.Tick(T);

	// --- before the authored start: silent ----------------------------------------------------
	// The mixahead dispatches a speak event early so the sample is HEARD at the authored instant.
	// The phoneme track has to run on the authored clock or the mouth leads the voice by the lead.
	T = 0.5; World.Tick(T);
	TestEqual(TEXT("t=0.5: dispatched early, but not speaking yet"),
		Services.FlexValue(TEXT("jaw_drop")), 0.f);

	// --- t = 1.15, i.e. 0.15 into the line ----------------------------------------------------
	// The scene builds the binding itself, so this runs on the DEFAULT filter pair (0.065, 0.100) —
	// which is what lacroix, nines and skelter all carry. `aw` spans 0.200 and clamps to the 0.100
	// maximum, decaying to 0.5; `t` spans 0.050 and clamps up to the 0.065 minimum, leading in at
	// 1 - 0.05/0.065 = 0.230769. So the controllers read 0.5x0.8 + 0.230769x0.9 and 0.230769x0.5.
	T = 1.15; World.Tick(T);
	TestEqual(TEXT("t=1.15: the phoneme pair sums on jaw_drop"),
		Services.FlexValue(TEXT("jaw_drop")), 0.607692f, 1e-3f);
	TestEqual(TEXT("t=1.15: and a zero-influence value still reaches smile"),
		Services.FlexValue(TEXT("smile")), 0.115384f, 1e-3f);

	// --- past the line's last phoneme: back to rest -------------------------------------------
	// The keys the line was driving are written back to zero by the same release pass an expression
	// uses; a driver that only wrote what was live would latch the last frame of the last phoneme.
	T = 2.0; World.Tick(T);
	TestEqual(TEXT("t=2.0: the phoneme track has ended and released jaw_drop"),
		Services.FlexValue(TEXT("jaw_drop")), 0.f);
	TestEqual(TEXT("t=2.0: and smile"), Services.FlexValue(TEXT("smile")), 0.f);

	// --- the A/B holds ------------------------------------------------------------------------
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.SceneLipsync"));
	if (TestNotNull(TEXT("elysium.SceneLipsync is registered"), CVar))
	{
		const int32 Was = CVar->GetInt();
		ON_SCOPE_EXIT { CVar->Set(Was, ECVF_SetByCode); };
		CVar->Set(0, ECVF_SetByCode);
		T = 1.15; World.Tick(T);
		TestEqual(TEXT("with lipsync off the mouth stays at rest"),
			Services.FlexValue(TEXT("jaw_drop")), 0.f);
	}

	ElysiumLip::ClearCache();
	ElysiumExpressions::ClearCache();
	return true;
}


// 12.5 — the phoneme filter is the SPEAKER'S, not a constant.
//
// `studiohdr` +232/+236 clamps a phoneme's span into the envelope's blend width, and the pair is
// authored per model: 57 of the rigged cast carry (0.065, 0.100), 32 carry (0.080, 0.100) and
// `Jeanette` alone (0.080, 0.105). The floor is what decides how far a SHORT phoneme opens at all —
// a span below it peaks at `span/S` — and about a third of a line's phonemes are below either
// value, so hardcoding one is visibly wrong on the half of the cast carrying the other.
//
// This drives the same scene as SceneLipsync with one thing changed: the body answers the wider
// floor. Both halves are asserted — the rig reads the field off the sidecar, and the value survives
// the whole trip out through IElysiumEmbodiment, into the driver's binding, and into the arithmetic.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhonemeFilterTest,
	"Elysium.Substrate.PhonemeFilter", GElysiumFacialTestFlags)
bool FElysiumPhonemeFilterTest::RunTest(const FString&)
{
	// --- the rig half: what the sidecar says, and what stands when it says nothing usable -----
	{
		FElysiumFacialRig Rig;
		FString Error;
		TestTrue(TEXT("the fixture rig parses"), Rig.LoadJsonText(GTestRigJson, Error));
		// The fixture carries no `phoneme_filter`, which is the pre-field export state.
		TestEqual(TEXT("a sidecar with no filter keeps the modal minimum"), Rig.PhonemeFilterMin, 0.065f, 1e-6f);
		TestEqual(TEXT("  and the modal maximum"), Rig.PhonemeFilterMax, 0.100f, 1e-6f);

		const FString WithField = FString(GTestRigJson).Replace(
			TEXT("\"stem\": \"testrig\","),
			TEXT("\"stem\": \"testrig\", \"phoneme_filter\": [0.080, 0.105],"));
		FElysiumFacialRig Jeanette;
		TestTrue(TEXT("a sidecar carrying the field parses"), Jeanette.LoadJsonText(WithField, Error));
		TestEqual(TEXT("and the authored minimum is taken"), Jeanette.PhonemeFilterMin, 0.080f, 1e-6f);
		TestEqual(TEXT("as is the authored maximum"), Jeanette.PhonemeFilterMax, 0.105f, 1e-6f);

		// 113 of the 339 loose models read (0, 0). All are unrigged so none reaches the viseme path,
		// but S would be 0 and 1/S infinite if one ever did — retail's own clamp does not guard it.
		const FString Zeroed = FString(GTestRigJson).Replace(
			TEXT("\"stem\": \"testrig\","),
			TEXT("\"stem\": \"testrig\", \"phoneme_filter\": [0.0, 0.0],"));
		FElysiumFacialRig Unrigged;
		TestTrue(TEXT("a (0,0) sidecar parses"), Unrigged.LoadJsonText(Zeroed, Error));
		TestEqual(TEXT("and (0,0) does not become the blend width"), Unrigged.PhonemeFilterMin, 0.065f, 1e-6f);
		TestEqual(TEXT("  on either bound"), Unrigged.PhonemeFilterMax, 0.100f, 1e-6f);
	}

	// --- the trip: rig -> IElysiumEmbodiment -> entity -> binding -> envelope ------------------
	ElysiumExpressions::ClearCache();
	ElysiumLip::ClearCache();
	ElysiumExpressions::RegisterInline(TEXT("testface"), GTestPhonemeTableText);
	ElysiumLip::RegisterInline(TEXT("test/line1.wav"), GSceneLipText);
	ElysiumScene::RegisterInline(TEXT("test/line1.vcd"), TEXT(""));

	const FString SceneText =
		TEXT("// Choreo version 1\n")
		TEXT("actor \"Face\"\n{\n")
		TEXT("  channel \"VO\"\n  {\n")
		TEXT("    event speak \"line1\"\n    {\n")
		TEXT("      time 1.000000 3.000000\n")
		TEXT("      param \"test/line1.wav\"\n")
		TEXT("    }\n")
		TEXT("  }\n")
		TEXT("}\n")
		TEXT("fps 60\nsnap off\n");
	ElysiumScene::RegisterInline(TEXT("test/filter.vcd"), SceneText);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	FElysiumEntityDef S;
	S.Classname = TEXT("logic_choreographed_scene");
	S.TargetName = TEXT("scene1");
	S.Keys.Add(TEXT("SceneFile"), TEXT("test/filter.vcd"));
	Defs.Defs.Add(MoveTemp(S));

	FElysiumEntityDef Face;
	Face.Classname = TEXT("npc_VVampire");
	Face.TargetName = TEXT("Face");
	Face.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/testface.mdl"));
	Defs.Defs.Add(MoveTemp(Face));

	FElysiumRecordingServices Services;
	Services.FlexControllers = { TEXT("jaw_drop"), TEXT("smile") };
	// The one difference from SceneLipsync: this speaker ships the wider floor.
	Services.PhonemeFilterMin = 0.080f;
	Services.PhonemeFilterMax = 0.100f;

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
	World.Tick(0.0);

	// Same instant SceneLipsync samples, 0.15 into the line. `aw` spans 0.200 and still clamps to the
	// 0.100 maximum, so its 0.5 is unchanged — the floor is the only term that moves. `t` spans 0.050
	// and now clamps up to 0.080 rather than 0.065, so it leads in at 1 - 0.05/0.08 = 0.375 instead of
	// 0.230769: the same phoneme opening half again as far on this face as on LaCroix's.
	World.Tick(1.15);
	TestEqual(TEXT("the wider floor opens a short phoneme further"),
		Services.FlexValue(TEXT("jaw_drop")), 0.7375f, 1e-3f);
	TestEqual(TEXT("and carries through to every key the row writes"),
		Services.FlexValue(TEXT("smile")), 0.1875f, 1e-3f);
	// The value SceneLipsync asserts on the default pair, restated as the thing that must NOT happen
	// here: one hardcoded constant would give both faces the same number.
	TestTrue(TEXT("which is not what the modal pair would have produced"),
		!FMath::IsNearlyEqual(Services.FlexValue(TEXT("jaw_drop")), 0.607692f, 1e-3f));

	ElysiumLip::ClearCache();
	ElysiumExpressions::ClearCache();
	return true;
}


bool bool #endif // WITH_DEV_AUTOMATION_TESTS
