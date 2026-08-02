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

#include "ElysiumContentPaths.h"
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
	//  - a DIV whose divisor is zero at rest, which is a shipped rule (`1 / right_open`).
	//
	// `wide` carries a 0..2 range so controller normalization is covered; every shipped VtMB
	// controller is 0..1, where normalization is the identity.
	const TCHAR* const GTestRigJson = TEXT(R"JSON(
	{
	  "stem": "testrig",
	  "flexdescs": ["lid","lid_lowerer","lid_neutral","lid_raiser","open","suppressor","plain"],
	  "controllers": [
	    {"name":"blink",  "type":"eyelid","min":0.0,"max":1.0},
	    {"name":"lid_up", "type":"eyelid","min":0.0,"max":1.0},
	    {"name":"droop",  "type":"eyelid","min":0.0,"max":1.0},
	    {"name":"wide",   "type":"mouth", "min":0.0,"max":2.0}
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
	    {"flexdesc":6,"ops":[["FETCH1",1],["FETCH2",5],["MUL"]]}
	  ],
	  "mouths": [{"bone":6,"forward":[0.0,-1.0,0.0],"flexdesc":0}],
	  "morphs": [
	    {"name":"lid",     "flexdesc":0,"targets":[-11.0,-10.0,-0.16,0.208]},
	    {"name":"lid#1",   "flexdesc":0,"targets":[0.208,0.298,10.0,11.0]},
	    {"name":"plain",   "flexdesc":6,"targets":[0.0,1.0,10.0,11.0]},
	    {"name":"open",    "flexdesc":4,"targets":[0.0,0.25,0.25,1.0]},
	    {"name":"open#1",  "flexdesc":4,"targets":[0.25,1.0,10.0,11.0]}
	  ]
	}
	)JSON");

	// Controller/flexdesc/morph indices in the rig above, so the assertions read as names.
	enum { CtlBlink = 0, CtlLidUp = 1, CtlDroop = 2, CtlWide = 3 };
	enum { FdLid = 0, FdLowerer = 1, FdNeutral = 2, FdRaiser = 3, FdOpen = 4, FdSuppressor = 5, FdPlain = 6 };
	enum { MorphLid = 0, MorphLidHigh = 1, MorphPlain = 2, MorphOpen = 3, MorphOpenHigh = 4 };

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

	TestEqual(TEXT("controllers"), Rig.Controllers.Num(), 4);
	TestEqual(TEXT("rules"), Rig.Rules.Num(), 6);
	TestEqual(TEXT("morphs"), Rig.Morphs.Num(), 5);
	TestEqual(TEXT("controller lookup is case-insensitive"), Rig.FindController(TEXT("BLINK")), (int32)CtlBlink);
	TestEqual(TEXT("an absent controller is INDEX_NONE"), Rig.FindController(TEXT("nostril")), INDEX_NONE);
	TestEqual(TEXT("the mouth flexdesc is read"), Rig.Mouth.FlexDesc, (int32)FdLid);

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

#endif // WITH_DEV_AUTOMATION_TESTS
