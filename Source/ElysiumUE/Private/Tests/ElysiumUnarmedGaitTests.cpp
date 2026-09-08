#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"
#include "ElysiumGaitSpeeds.h"               // the per-direction speed table the fan fills
#include "ElysiumNativeCharacterTestData.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumNpcClips.h"

// The unarmed cast gait.
//
// `PreTranslate_Human` (`vampire.dll 0x103854f0`) carries two rows that fire under `NotArmedAlert`:
// `ACT_WALK` -> `ACT_WALK_RELAXED` (9 -> 22) and `ACT_RUN` -> `ACT_RUN_RELAXED` (19 -> 23). An
// unarmed body satisfies that predicate unconditionally -- the recovered tree early-outs on "no
// active weapon" before it reads any state -- and **no shipped body carries either sequence**;
// the census over `out/npc/clips` finds `ACT_WALK_RELAXED` on none of the 293 exported bodies.
//
// So on every unarmed cast body the only thing that hands the gait back is the four-way
// availability probe at the tail of `CAI_BaseNPC::TranslateActivity` (`0x10271ff0`), whose rung 4
// is the untranslated request. That probe is unconditional in retail: its one early return is
// `ACT_SCRIPT_CUSTOM_MOVE` (`0x18`), and nothing else skips it.
//
// The distinction these two tests hold is that the probe never crosses activities -- its four rungs
// are the same request as the weapon table left it, as the class table left it, as the first weapon
// pass left it, and untranslated -- while three other rungs do: the `ACT_RUN` -> `ACT_WALK` last
// resort (`piStack_4 == 0x13` -> `9`), the ACT_DISPOSITION retry and sequence zero. A speed reader
// wants the first and must refuse the second, which is `bAllowSubstituteActivity`.

static constexpr EAutomationTestFlags GElysiumUnarmedGaitFixtureFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The substitution gate, on a fixture, both ways.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGaitSubstitutionGateTest,
	"Elysium.Substrate.GaitSubstitutionGate", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumGaitSubstitutionGateTest::RunTest(const FString&)
{
	// A body carrying a walk and no run at all: retail's last resort is the only thing that can
	// answer an `ACT_RUN` request on it, so the gate's two arms are separable by construction.
	FElysiumNpcClipSet Body;
	Body.Stem = TEXT("gate_body");
	FElysiumNpcClip Walk;
	Walk.Owner = TEXT("gate_bank");
	Walk.Activity = TEXT("ACT_WALK");
	Walk.Weight = 1;
	Walk.Flags = 0x1;
	Walk.Frames = 30;
	Walk.Fps = 30.0f;
	Body.Clips.Add(TEXT("Walk"), Walk);

	FElysiumAnimationCatalog Catalog;
	Catalog.Clips = &Body;
	Catalog.BlendTableFor = [](const FString&) -> const FElysiumBlendTable* { return nullptr; };

	auto ResolveRun = [&Catalog, &Body](bool bSubstitute)
	{
		FElysiumAnimationIntent Intent;
		Intent.Stem = Body.Stem;
		Intent.Activity = TEXT("ACT_RUN");
		Intent.Source = EElysiumAnimSource::Npc;
		Intent.BodyKind = EElysiumAnimBodyKind::Cast;
		Intent.bAllowFallbackLadder = true;
		Intent.bAllowSubstituteActivity = bSubstitute;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		return Out;
	};

	const FElysiumAnimationSelection Substituted = ResolveRun(true);
	TestEqual(TEXT("the per-frame publish takes retail's ACT_RUN -> ACT_WALK last resort"),
		Substituted.ResolvedActivity, FString(TEXT("ACT_WALK")));

	const FElysiumAnimationSelection Refused = ResolveRun(false);
	TestNotEqual(TEXT("a speed reader refuses it rather than publishing walk speeds as run"),
		Refused.ResolvedActivity, FString(TEXT("ACT_WALK")));

	// The four rungs above it are not substitutions and must survive the cleared flag. This is the
	// exact shape of the defect: an unarmed cast body whose class pre-translates the walk into a
	// relaxed walk it does not carry, rescued by rung 4 and nothing else.
	FElysiumAnimationIntent Unarmed;
	Unarmed.Stem = Body.Stem;
	Unarmed.Activity = TEXT("ACT_WALK");
	Unarmed.Source = EElysiumAnimSource::Npc;
	Unarmed.BodyKind = EElysiumAnimBodyKind::Cast;
	Unarmed.ActorClassname = TEXT("npc_VHumanCombatant");
	Unarmed.ActorState = EElysiumNpcState::Idle;
	Unarmed.bAllowFallbackLadder = true;
	Unarmed.bAllowSubstituteActivity = false;
	FElysiumAnimationSelection Probed;
	ElysiumAnimResolve::Resolve(Unarmed, Catalog, Probed);
	TestEqual(TEXT("the availability probe still hands an unarmed body its plain walk back"),
		Probed.ResolvedActivity, FString(TEXT("ACT_WALK")));
	TestFalse(TEXT("...naming a real label"), Probed.SequenceLabel.IsEmpty());
	TestTrue(TEXT("...through a rung of the probe rather than the translation's own answer"),
		Probed.AvailabilityRung > 1);
	return true;
}


bool // The non-fanned half of retail's one speed pipeline.
//
// `ResetSequenceInfo` (`0x10090950`) calls `GetSequenceGroundSpeed` (`0x10091490`) for every
// sequence it commits; that is `GetSequenceMoveDist` (`0x1008fbe0`, the magnitude of
// `GetSequenceLinearMotion`) over `SequenceDuration`. The mover underneath (`0x100c5d10`)
// accumulates `weight * motion` across four bilinear corners, and `0x100c1c60` returns fraction 0
// and index 0 the instant `poseparamindex[axis]` reads `-1` -- so for a sequence that binds no pose
// parameter the weights are `{1,0,0,0}` and the speed is one number no direction can change.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFlatGaitFanTest,
	"Elysium.Substrate.FlatGaitFan", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumFlatGaitFanTest::RunTest(const FString&)
{
	// A rat's walk, as the corpus authors it: 11 frames at 30 fps over 71.9109 cm.
	FElysiumClipMotion Motion;
	Motion.CycleSeconds = 10.0f / 30.0f;
	Motion.GroundDistanceCm = 71.9109f;
	Motion.GroundSpeedCmPerSecond = Motion.GroundDistanceCm / Motion.CycleSeconds;

	FElysiumGaitSpeedTable Flat;
	TestTrue(TEXT("a clip with authored motion fills a fan"),
		ElysiumBlendGrids::FlatFan(Motion, 1.0f, Flat));
	// `IsValid` refuses a single-cell table, which is why the flat answer is spread over the cells
	// rather than written once.
	TestTrue(TEXT("...that the consumer accepts"), Flat.IsValid());
	TestEqual(TEXT("...spanning the shipped move_yaw geometry"), Flat.Count,
		FElysiumGaitSpeedTable::MaxCells);

	// The claim itself: no direction can change it.
	const float Forward = Flat.Forward();
	TestTrue(TEXT("the forward speed is the authored one"),
		FMath::IsNearlyEqual(Forward, Motion.GroundSpeedCmPerSecond, 0.01f));
	for (const float Yaw : { -180.0f, -90.0f, -22.5f, 0.0f, 45.0f, 90.0f, 179.0f })
	{
		TestTrue(*FString::Printf(TEXT("...and it is the same at %.1f degrees"), Yaw),
			FMath::IsNearlyEqual(Flat.SpeedAt(Yaw), Forward, 0.01f));
	}
	TestTrue(TEXT("the peak is that same speed, so nothing reads it as a ceiling to clamp under"),
		FMath::IsNearlyEqual(Flat.Peak(), Forward, 0.01f));

	// Symmetrize is what the gait seam runs over every table; on a flat one it must change nothing.
	FElysiumGaitSpeedTable Symmetric = Flat;
	Symmetric.Symmetrize();
	TestTrue(TEXT("symmetrizing a flat fan is a no-op"),
		FMath::IsNearlyEqual(Symmetric.Forward(), Forward, 0.01f));

	// The gait's own multiplier still applies, exactly as it does to a fanned table.
	FElysiumGaitSpeedTable Scaled;
	TestTrue(TEXT("the scale is carried, not folded"),
		ElysiumBlendGrids::FlatFan(Motion, 2.3f, Scaled));
	TestTrue(TEXT("...so a sneak commands 2.3x the authored speed"),
		FMath::IsNearlyEqual(Scaled.Forward(), Forward * 2.3f, 0.01f));

	// A sequence that authors no movement is retail's zero, and it is refused rather than published.
	FElysiumClipMotion Motionless;
	Motionless.CycleSeconds = 1.0f;
	FElysiumGaitSpeedTable Refused;
	TestFalse(TEXT("a clip with no authored movement fills no fan"),
		ElysiumBlendGrids::FlatFan(Motionless, 1.0f, Refused));
	TestFalse(TEXT("...and leaves the table absent rather than zeroed-but-present"),
		Refused.IsValid());
	return true;
}


bool #endif // WITH_DEV_AUTOMATION_TESTS
