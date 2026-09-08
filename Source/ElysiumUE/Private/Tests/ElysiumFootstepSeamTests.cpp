// The footstep subsystem's SEAMS — wave 1's half.
//
// Four Substrate-tier cases with no RHI, no actors and no export corpus, plus one content case that
// reads the baked surfaceprop assets:
//
//   * `SoundLevel`        — `vampire.dll 0x10228350` and `0x1026d460`'s attenuation, transcribed,
//                           and the Unreal falloff the level is turned into.
//   * `Tuning`            — the seven cvars, their retail defaults and the store round-trip.
//   * `TemplateFootfall`  — the four `General` footfall keys through `ParentTemplateName`.
//   * `BodySoundReplacement` — the `PlayBodySound` request as a producer makes it.
//   * `Elysium.Content.SurfaceSoundTable` — every baked `PM_*` resolves, with the 17 authored
//                           step pools at the sizes the export carries.
//
// The producers (the NPC animation-event arm and the player's step clock) are wave 2's; nothing
// here asserts a step is taken, only that everything a step needs answers correctly.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityHandle.h"
#include "ElysiumFootstepTuning.h"
#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U
#include "ElysiumSoundLevel.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumRulebook.h"
#include "Tests/ElysiumTestServices.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"    // GetDefault

namespace ElysiumFootstepSeamTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// --- Elysium.Substrate.Footsteps.SoundLevel ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepSoundLevelTest,
	"Elysium.Substrate.Footsteps.SoundLevel", GElysiumTestFlags)
bool FElysiumFootstepSoundLevelTest::RunTest(const FString&)
{
	using namespace ElysiumSoundLevel;

	// --- `vampire.dll 0x10228350`: the authored distance -> the level ------------------------
	// The four shipped distances. 300/600 are what every `npctemplate*.txt` authors; 256/512 are
	// the cvar defaults the template path falls back from.
	TestEqual(TEXT("300 units is level 58"), FromDistanceUnits(300.f), 58);
	TestEqual(TEXT("600 units is level 64"), FromDistanceUnits(600.f), 64);
	TestEqual(TEXT("256 units is level 57"), FromDistanceUnits(256.f), 57);
	TestEqual(TEXT("512 units is level 63"), FromDistanceUnits(512.f), 63);
	// This function's own reference is 40 dB, so `snd_refdist` reads as 40 here and the truncation
	// is toward zero.
	TestEqual(TEXT("the reference distance reads 40 on the game DLL's scale"),
		FromDistanceUnits(36.f), 40);
	TestEqual(TEXT("and just inside it truncates down"), FromDistanceUnits(35.f), 39);
	// `dist <= 0` is retail's early-out: SNDLVL_NONE, not "very quiet".
	TestEqual(TEXT("zero distance is SNDLVL_NONE"), FromDistanceUnits(0.f), 0);
	TestEqual(TEXT("a negative distance takes the same arm"), FromDistanceUnits(-10.f), 0);

	// --- `0x1026d460`'s PAS attenuation: retail, and inert -------------------------------------
	// Kept as a transcription only. It feeds `CPASAttenuationFilter`, a server-side recipient cull
	// that `maxClients == 1` skips (`docs/vtmb/footsteps.md` 3.5); the falloff below is built from
	// the SOUND LEVEL and never from this number.
	TestEqual(TEXT("level 58 attenuates by 2"), SourceAttenuation(58), 2.f);
	TestEqual(TEXT("level 64 attenuates by 1"), SourceAttenuation(64), 1.f);
	// The integer divide is the whole point: 57 and 58 are the same answer, as are 63 and 64.
	TestEqual(TEXT("level 57 attenuates by 2 as well"), SourceAttenuation(57), 2.f);
	TestEqual(TEXT("level 63 attenuates by 1 as well"), SourceAttenuation(63), 1.f);
	TestEqual(TEXT("level 51 is the hyperbola's first step"), SourceAttenuation(51), 20.f);
	TestEqual(TEXT("level 50 takes the fixed floor"), SourceAttenuation(50), 4.f);
	TestEqual(TEXT("and so does everything below it"), SourceAttenuation(0), 4.f);
	// 20/(75-50) is 0 in integer arithmetic -- the player's own level. The transcription must not
	// round it up to 1.
	TestEqual(TEXT("level 75 divides to zero, not to 0.8"), SourceAttenuation(75), 0.f);

	// --- `engine.dll 0x20119fe0`: the level's reference distance, on the snd_refdb 60 scale -----
	// `D_ref(L) = 36 * 10^((L - 60)/20)`. The recovered table (`footsteps.md` 3.3) is
	// 57 -> 25.5, 58 -> 28.6, 63 -> 50.9, 64 -> 57.1, 75 -> 202.4.
	TestTrue(TEXT("level 58's reference distance is 28.6 units"),
		FMath::IsNearlyEqual(ReferenceDistanceUnits(58), 28.60f, 0.05f));
	TestTrue(TEXT("level 64's is 57.1"),
		FMath::IsNearlyEqual(ReferenceDistanceUnits(64), 57.06f, 0.05f));
	TestTrue(TEXT("the player's level 75 is 202.4"),
		FMath::IsNearlyEqual(ReferenceDistanceUnits(75), 202.44f, 0.05f));
	TestTrue(TEXT("centimetres are the units times 2.54"),
		FMath::IsNearlyEqual(ReferenceDistanceCm(58),
			ReferenceDistanceUnits(58) * ElysiumMove::U, 0.01f));

	// **The identity that makes the two DLLs agree.** `0x10228350` uses 40 dB where the engine uses
	// 60, so the authored footfall distance is exactly ten reference distances -- the -20 dB point.
	TestTrue(TEXT("the authored distance is ten reference distances"),
		FMath::IsNearlyEqual(AuthoredDistanceUnits(58),
			ReferenceDistanceUnits(58) * 10.f, 0.01f));
	// 300 authored -> level 58 -> 286 back, which is 300 to within the level's own truncation band.
	TestTrue(TEXT("300 units sits in level 58's authored band"),
		AuthoredDistanceUnits(58) <= 300.f && 300.f < AuthoredDistanceUnits(59));
	TestTrue(TEXT("600 units sits in level 64's authored band"),
		AuthoredDistanceUnits(64) <= 600.f && 600.f < AuthoredDistanceUnits(65));
	// The band round-trips: anything strictly inside it answers 58. Sampled a hundredth of a unit
	// inside each edge so a float `pow` landing one ULP below the boundary is not what is tested.
	TestEqual(TEXT("just inside the band's lower edge is level 58"),
		FromDistanceUnits(AuthoredDistanceUnits(58) + 0.01f), 58);
	TestEqual(TEXT("and just inside its upper edge still is"),
		FromDistanceUnits(AuthoredDistanceUnits(59) - 0.01f), 58);

	// The audible range is where `snd_gain_min` (0.01) is reached: ten times the authored distance.
	TestTrue(TEXT("level 58 is audible to 2860 units"),
		FMath::IsNearlyEqual(AudibleRangeUnits(58), 2859.6f, 1.0f));
	TestTrue(TEXT("which is ten times the authored distance"),
		FMath::IsNearlyEqual(AudibleRangeUnits(58), AuthoredDistanceUnits(58) * 10.f, 0.5f));

	// --- `engine.dll 0x2011a0b0` SND_GetGain ---------------------------------------------------
	{
		const float Ref = ReferenceDistanceUnits(58);

		// Beyond twice the reference distance it is pure inverse distance: -6 dB per doubling.
		TestTrue(TEXT("half gain at twice the reference distance"),
			FMath::IsNearlyEqual(Gain(58, Ref * 2.f), 0.5f, 0.001f));
		TestTrue(TEXT("quarter gain at four times it"),
			FMath::IsNearlyEqual(Gain(58, Ref * 4.f), 0.25f, 0.001f));
		TestTrue(TEXT("an eighth at eight times it"),
			FMath::IsNearlyEqual(Gain(58, Ref * 8.f), 0.125f, 0.001f));
		// The authored distance IS the -20 dB point. This is the whole of what a footfall distance
		// means, and it is the number a designer typed.
		TestTrue(TEXT("the authored distance is -20 dB"),
			FMath::IsNearlyEqual(Gain(58, AuthoredDistanceUnits(58)), 0.1f, 0.001f));
		// The floor, at ten times that again.
		TestTrue(TEXT("the audible range is -40 dB"),
			FMath::IsNearlyEqual(Gain(58, AudibleRangeUnits(58)), 0.01f, 0.0005f));

		// Inside the reference distance the knee compresses towards `snd_gain_max`, and the
		// `relative` floor of 0.1 caps the raw gain at 10.
		TestTrue(TEXT("the reference distance itself is 0.912, not 1"),
			FMath::IsNearlyEqual(Gain(58, Ref), 0.9116f, 0.001f));
		TestTrue(TEXT("the knee is continuous at gain 0.5"),
			FMath::IsNearlyEqual(Gain(58, Ref * 2.0001f), 0.5f, 0.002f));
		TestTrue(TEXT("the near field is capped just under one"),
			FMath::IsNearlyEqual(Gain(58, Ref * 0.01f), 0.99972f, 0.0005f));
		TestTrue(TEXT("...and standing on the source is the same cap"),
			FMath::IsNearlyEqual(Gain(58, 0.f), 0.99972f, 0.0005f));
	}

	// --- The Unreal falloff the level is turned into -------------------------------------------
	{
		const FSoundAttenuationSettings Normal = MakeAttenuation(58);
		const FSoundAttenuationSettings Heavy = MakeAttenuation(64);

		TestTrue(TEXT("a body sound attenuates"), Normal.bAttenuate != 0);
		TestTrue(TEXT("and is spatialized"), Normal.bSpatialize != 0);
		TestEqual(TEXT("the inner radius is the level's reference distance"),
			static_cast<float>(Normal.AttenuationShapeExtents.X), ReferenceDistanceCm(58));
		TestEqual(TEXT("and the stated drop at the reach is snd_gain_min, -40 dB"),
			Normal.dBAttenuationAtMax, -40.f);

		// The cull distance IS the audible range. `GetMaxDimension` is falloff + the sphere's
		// extent, so it comes back out as the same number `AudibleRangeCm` states.
		TestTrue(TEXT("level 58 is culled at its audible range"),
			FMath::IsNearlyEqual(Normal.GetMaxDimension(), AudibleRangeCm(58),
				AudibleRangeCm(58) * 0.001f));
		TestTrue(TEXT("and level 64 at its own"),
			FMath::IsNearlyEqual(Heavy.GetMaxDimension(), AudibleRangeCm(64),
				AudibleRangeCm(64) * 0.001f));

		// Monotone in the level, across the whole band the two producers use (an NPC's computed
		// level and the player's fixed 75) and well past it.
		bool bMonotone = true;
		float Previous = 0.f;
		for (int32 Level = 41; Level <= 100; ++Level)
		{
			const float Reach = MakeAttenuation(Level).GetMaxDimension();
			bMonotone = bMonotone && Reach > Previous;
			Previous = Reach;
		}
		TestTrue(TEXT("reach is strictly monotone in the sound level"), bMonotone);

		// The curve IS `SND_GetGain`, sampled. Checked from the reference distance outward, which is
		// the whole region the settings claim to reproduce exactly.
		const float RefCm = ReferenceDistanceCm(58);
		auto GainAt = [&Normal](float DistCm)
		{
			return Normal.Evaluate(FTransform::Identity,
				FVector(static_cast<double>(DistCm), 0.0, 0.0), 1.f);
		};
		TestTrue(TEXT("the curve matches the law at the reference distance"),
			FMath::IsNearlyEqual(GainAt(RefCm), Gain(58, ReferenceDistanceUnits(58)), 0.005f));
		TestTrue(TEXT("...at twice it"),
			FMath::IsNearlyEqual(GainAt(RefCm * 2.f), 0.5f, 0.005f));
		TestTrue(TEXT("...at four times it"),
			FMath::IsNearlyEqual(GainAt(RefCm * 4.f), 0.25f, 0.005f));
		TestTrue(TEXT("...at the authored distance, which is -20 dB"),
			FMath::IsNearlyEqual(GainAt(AuthoredDistanceUnits(58) * ElysiumMove::U), 0.1f, 0.005f));
		TestTrue(TEXT("...and at the floor, which is -40 dB"),
			FMath::IsNearlyEqual(GainAt(AudibleRangeCm(58) * 0.999f), 0.01f, 0.002f));
		// Inside the reference distance the extent holds the curve's first key -- the one bounded
		// simplification the model states (retail rises to 0.99972 there; this holds 0.912).
		TestTrue(TEXT("inside the reference distance the field is held at its edge value"),
			FMath::IsNearlyEqual(GainAt(RefCm * 0.25f), GainAt(RefCm), 0.001f));

		// SNDLVL_NONE: audible everywhere, still placed.
		const FSoundAttenuationSettings None = MakeAttenuation(0);
		TestFalse(TEXT("SNDLVL_NONE does not attenuate"), None.bAttenuate != 0);
		TestTrue(TEXT("but is still spatialized"), None.bSpatialize != 0);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.Tuning -------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepTuningTest,
	"Elysium.Substrate.Footsteps.Tuning", GElysiumTestFlags)
bool FElysiumFootstepTuningTest::RunTest(const FString&)
{
	// --- The retail defaults, as the DLL constructs them ---------------------------------------
	// Not `const`: a const object needs a const-default-constructible type, and these structs
	// carry members whose defaults come from their own constructors.
	FElysiumFootstepTuning Defaults;
	TestTrue(TEXT("footstep_npc_use_templates defaults on"), Defaults.bNpcUseTemplates);
	TestEqual(TEXT("footstep_normal_vol 0.5"), Defaults.NormalVolume, 0.5f);
	TestEqual(TEXT("footstep_normal_dist 256"), Defaults.NormalDistanceUnits, 256.f);
	TestEqual(TEXT("footstep_heavy_vol 0.85"), Defaults.HeavyVolume, 0.85f);
	TestEqual(TEXT("footstep_heavy_dist 512"), Defaults.HeavyDistanceUnits, 512.f);
	TestEqual(TEXT("footstep_pc_vol 0.5"), Defaults.PlayerVolume, 0.5f);
	TestTrue(TEXT("sv_footsteps defaults on"), Defaults.bServerFootsteps);

	// The mode selectors both producers read.
	TestEqual(TEXT("normal mode takes the normal pair"), Defaults.VolumeFor(false), 0.5f);
	TestEqual(TEXT("heavy mode takes the heavy pair"), Defaults.VolumeFor(true), 0.85f);
	TestEqual(TEXT("...and its distance"), Defaults.DistanceUnitsFor(true), 512.f);

	// --- The declaration is the whole surface, and its default STRINGS agree with the struct ----
	// A default declared into the console store that disagreed with the member initialiser would
	// make `footstep_pc_vol` read one value before a cfg is loaded and another after.
	{
		TArrayView<const ElysiumFootstep::FCvarDef> Defs = ElysiumFootstep::CvarDefs();
		TestEqual(TEXT("all seven footstep cvars are declared"), Defs.Num(), 7);

		TMap<FString, FString> Store;
		for (const ElysiumFootstep::FCvarDef& Def : Defs)
		{
			TestTrue(FString::Printf(TEXT("%s has a default"), Def.Name),
				Def.Default != nullptr && *Def.Default != TEXT('\0'));
			TestTrue(FString::Printf(TEXT("%s has help"), Def.Name),
				Def.Help != nullptr && *Def.Help != TEXT('\0'));
			Store.Add(Def.Name, Def.Default);
		}
		auto Lookup = [&Store](const TCHAR* Name)
		{
			const FString* Found = Store.Find(Name);
			return Found ? *Found : FString();
		};

		FElysiumFootstepTuning FromDeclaration;
		FromDeclaration.LoadFrom(Lookup);
		TestEqual(TEXT("the declared defaults reproduce the struct: normal vol"),
			FromDeclaration.NormalVolume, Defaults.NormalVolume);
		TestEqual(TEXT("...normal dist"),
			FromDeclaration.NormalDistanceUnits, Defaults.NormalDistanceUnits);
		TestEqual(TEXT("...heavy vol"), FromDeclaration.HeavyVolume, Defaults.HeavyVolume);
		TestEqual(TEXT("...heavy dist"),
			FromDeclaration.HeavyDistanceUnits, Defaults.HeavyDistanceUnits);
		TestEqual(TEXT("...pc vol"), FromDeclaration.PlayerVolume, Defaults.PlayerVolume);
		TestTrue(TEXT("...use templates"), FromDeclaration.bNpcUseTemplates);
		TestTrue(TEXT("...sv_footsteps"), FromDeclaration.bServerFootsteps);
	}

	// --- The store round-trip: an empty read keeps the default, a set value wins ---------------
	{
		TMap<FString, FString> Store;
		auto Lookup = [&Store](const TCHAR* Name)
		{
			const FString* Found = Store.Find(Name);
			return Found ? *Found : FString();
		};

		FElysiumFootstepTuning Tuned;
		Tuned.LoadFrom(Lookup);
		TestEqual(TEXT("an empty store keeps VtMB's own defaults"), Tuned.NormalVolume, 0.5f);

		Store.Add(TEXT("footstep_normal_dist"), TEXT("300"));
		Store.Add(TEXT("footstep_pc_vol"), TEXT("0.8"));
		Store.Add(TEXT("sv_footsteps"), TEXT("0"));
		Store.Add(TEXT("footstep_npc_use_templates"), TEXT("0"));
		Tuned.LoadFrom(Lookup);
		// **Unconverted.** The distance is the argument of `20*log10(d/36)`, so it stays in Source
		// units; converting it here would move every NPC's step level.
		TestEqual(TEXT("a console distance reads back in Source units"),
			Tuned.NormalDistanceUnits, 300.f);
		TestEqual(TEXT("...and 300 units is level 58"),
			ElysiumSoundLevel::FromDistanceUnits(Tuned.NormalDistanceUnits), 58);
		TestEqual(TEXT("a console volume wins"), Tuned.PlayerVolume, 0.8f);
		TestFalse(TEXT("sv_footsteps 0 turns the player's clock off"), Tuned.bServerFootsteps);
		TestFalse(TEXT("footstep_npc_use_templates 0 selects the cvar pair"), Tuned.bNpcUseTemplates);

		// A present but unparsable value keeps the last good one rather than collapsing to zero —
		// a footstep distance of 0 is SNDLVL_NONE, i.e. audible across the whole map.
		AddExpectedError(TEXT("footstep_normal_dist is set to"),
			EAutomationExpectedErrorFlags::Contains, 1);
		Store.Add(TEXT("footstep_normal_dist"), TEXT("nonsense"));
		Tuned.LoadFrom(Lookup);
		TestEqual(TEXT("a malformed value keeps the last good one"),
			Tuned.NormalDistanceUnits, 300.f);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.TemplateFootfall ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepTemplateFootfallTest,
	"Elysium.Substrate.Footsteps.TemplateFootfall", GElysiumTestFlags)
bool FElysiumFootstepTemplateFootfallTest::RunTest(const FString&)
{
	using namespace ElysiumFootstep;

	// A fabricated parent/child pair, in the shape the shipped `npctemplate*.txt` author:
	// the parent states all four footfall keys, the child overrides ONE of them and inherits the
	// other three through `ParentTemplateName` (`0x1026d460` reads whichever the resolve produced).
	FElysiumClanTable Table;
	{
		FElysiumClanTemplate Parent;
		Parent.TemplateName = TEXT("Test_Thug_Base");
		// The keys go in FOLDED, which is how the KV reader stores every `General` pair.
		Parent.General.Add(TEXT("normalfootfallvol"), TEXT("0.45"));
		Parent.General.Add(TEXT("normalfootfalldist"), TEXT("300"));
		Parent.General.Add(TEXT("heavyfootfallvol"), TEXT("0.85"));
		Parent.General.Add(TEXT("heavyfootfalldist"), TEXT("600"));
		Table.AddNpcTemplate(MoveTemp(Parent));

		FElysiumClanTemplate Child;
		Child.TemplateName = TEXT("Test_Thug_Heavy");
		Child.ParentTemplateName = TEXT("Test_Thug_Base");
		Child.General.Add(TEXT("heavyfootfallvol"), TEXT("1.0"));
		Table.AddNpcTemplate(MoveTemp(Child));

		FElysiumClanTemplate Bare;
		Bare.TemplateName = TEXT("Test_Thug_Bare");
		Table.AddNpcTemplate(MoveTemp(Bare));
	}

	TestNotNull(TEXT("the fabricated parent is indexed"), Table.Find(TEXT("Test_Thug_Base")));
	TestNotNull(TEXT("and so is the child"), Table.Find(TEXT("Test_Thug_Heavy")));

	// --- The child's own map carries only what it authored ------------------------------------
	if (const FElysiumClanTemplate* Raw = Table.Find(TEXT("Test_Thug_Heavy")))
	{
		TestEqual(TEXT("the raw child holds its own override"),
			Raw->GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 1.0f);
		// Unresolved, the inherited key is simply absent — which is why `Resolve` is load-bearing.
		TestFalse(TEXT("and does not carry the parent's distance"), Raw->HasGeneral(KeyNormalDist));
		TestEqual(TEXT("so an unresolved read falls to the loader default"),
			Raw->GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), TemplateNormalDistDefault);
	}

	// --- The resolved child inherits three keys and overrides one ------------------------------
	FElysiumClanTemplate Resolved;
	if (TestTrue(TEXT("the child resolves"), Table.Resolve(TEXT("Test_Thug_Heavy"), Resolved)))
	{
		TestEqual(TEXT("NormalFootfallVol is inherited"),
			Resolved.GeneralFloat(KeyNormalVol, TemplateNormalVolDefault), 0.45f);
		TestEqual(TEXT("NormalFootfallDist is inherited"),
			Resolved.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), 300.f);
		TestEqual(TEXT("HeavyFootfallDist is inherited"),
			Resolved.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault), 600.f);
		TestEqual(TEXT("HeavyFootfallVol is the child's override"),
			Resolved.GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 1.0f);
		TestEqual(TEXT("the resolved template keeps its own identity"),
			Resolved.TemplateName, FString(TEXT("Test_Thug_Heavy")));

		// The shipped authoring is what the recovered levels come from: 300 -> 58, 600 -> 64.
		TestEqual(TEXT("the inherited normal distance is level 58"),
			ElysiumSoundLevel::FromDistanceUnits(
				Resolved.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault)), 58);
		TestEqual(TEXT("the inherited heavy distance is level 64"),
			ElysiumSoundLevel::FromDistanceUnits(
				Resolved.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault)), 64);
	}

	// --- A template that authors none of the four takes `0x101d3f10`'s own defaults -------------
	FElysiumClanTemplate Bare;
	if (TestTrue(TEXT("the keyless template resolves"), Table.Resolve(TEXT("Test_Thug_Bare"), Bare)))
	{
		TestEqual(TEXT("NormalFootfallVol default 0.45"),
			Bare.GeneralFloat(KeyNormalVol, TemplateNormalVolDefault), 0.45f);
		TestEqual(TEXT("NormalFootfallDist default 256"),
			Bare.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), 256.f);
		TestEqual(TEXT("HeavyFootfallVol default 0.85"),
			Bare.GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 0.85f);
		TestEqual(TEXT("HeavyFootfallDist default 512"),
			Bare.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault), 512.f);
	}

	// `GeneralFloat` exists because `GeneralInt` would read every authored volume as zero, and a
	// step at volume 0 is a step that never happened.
	if (const FElysiumClanTemplate* Raw = Table.Find(TEXT("Test_Thug_Base")))
	{
		TestEqual(TEXT("GeneralInt truncates a footfall volume to nothing"),
			Raw->GeneralInt(KeyNormalVol, -1), 0);
		TestEqual(TEXT("which is exactly what GeneralFloat exists to avoid"),
			Raw->GeneralFloat(KeyNormalVol, -1.f), 0.45f);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.BodySoundReplacement ------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepBodySoundTest,
	"Elysium.Substrate.Footsteps.BodySoundReplacement", GElysiumTestFlags)
bool FElysiumFootstepBodySoundTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	IElysiumAudio& Audio = Services;

	const FElysiumEntityHandle Walker(7, 1);
	const FElysiumEntityHandle Other(9, 1);

	FElysiumBodySound Left;
	Left.Rel = TEXT("surfaces/concrete/stepleft1.wav");
	Left.Volume = 0.45f;
	Left.SoundLevelDb = ElysiumSoundLevel::FromDistanceUnits(300.f);
	Left.Pitch = 1.0f;
	Left.Channel = EElysiumSoundChannel::Body;

	// The defaults are the player's own emission (`0x1011e430`: CHAN_BODY, level 75).
	FElysiumBodySound Bare;
	TestEqual(TEXT("a body sound defaults to CHAN_BODY"),
		static_cast<int32>(Bare.Channel), static_cast<int32>(EElysiumSoundChannel::Body));
	TestEqual(TEXT("CHAN_BODY is Source's 4"), static_cast<int32>(EElysiumSoundChannel::Body), 4);
	TestEqual(TEXT("and the default level is the player's 75"), Bare.SoundLevelDb, 75);

	const FElysiumAudioVoiceHandle First = Audio.PlayBodySound(Walker, Left);
	TestTrue(TEXT("the double answers with a live voice"), First.IsValid());
	TestTrue(TEXT("...that is playing"), Audio.IsVoicePlaying(First));

	FElysiumBodySound Right = Left;
	Right.Rel = TEXT("surfaces/concrete/stepright2.wav");
	const FElysiumAudioVoiceHandle Second = Audio.PlayBodySound(Walker, Right);
	TestTrue(TEXT("the second step is its own voice"), Second.IsValid() && Second != First);

	// The recorded line is the producer's whole request, in the shape wave 2's tests read.
	TestEqual(TEXT("two body sounds were requested"), Services.Count(TEXT("PlayBodySound")), 2);
	TestTrue(TEXT("the first records its wav, volume, level, pitch and channel"),
		Services.Saw(TEXT("PlayBodySound surfaces/concrete/stepleft1.wav vol=0.45 lvl=58 pitch=1.00 chan=4")));
	TestTrue(TEXT("and so does the second"),
		Services.Saw(TEXT("PlayBodySound surfaces/concrete/stepright2.wav vol=0.45 lvl=58 pitch=1.00 chan=4")));

	// The value mirror, so a producer test asserts a volume without parsing one out of a string.
	if (TestEqual(TEXT("both requests are kept as values"), Services.BodySounds.Num(), 2))
	{
		TestEqual(TEXT("the level is the one 300 units buys"), Services.BodySounds[0].SoundLevelDb, 58);
		TestTrue(TEXT("the owner travels with the request"),
			Services.BodySoundOwners[0] == Walker);
		TestTrue(TEXT("and the second is the same owner"),
			Services.BodySoundOwners[1] == Walker);
	}

	// A different owner is a different channel slot even on the same channel enumerator.
	const FElysiumAudioVoiceHandle Third = Audio.PlayBodySound(Other, Left);
	TestTrue(TEXT("another body's step is its own voice"), Third.IsValid());
	TestEqual(TEXT("three requests in all"), Services.Count(TEXT("PlayBodySound")), 3);

	// **What this case does NOT prove, and why.** Source's `(entity, channel)` replacement — a new
	// CHAN_BODY sound stopping the previous one on the same body — is `AElysiumMapActor`'s policy,
	// implemented over its live voice pool. Reproducing it here would need a `UGameInstance` with a
	// `UElysiumAudioSubsystem`, a map actor with a loaded `.ents` entity world (the owner handle has
	// to RESOLVE for a body sound to be placed at all), and a spawned body to attach to — three
	// things no Substrate-tier fixture has and no headless engine-tier test in this suite builds.
	// The recording double therefore keeps every voice alive on purpose: it is a recorder, and a
	// test that saw it stop a voice would be asserting the double's behaviour, not the port's.
	// The map-actor half is read in wave 3's review against `vampire.dll 1026d460`'s CHAN_BODY emit.
	TestTrue(TEXT("the double keeps every voice, replacement being the map actor's policy"),
		Audio.IsVoicePlaying(First) && Audio.IsVoicePlaying(Second) && Audio.IsVoicePlaying(Third));

	return true;
}

// --- Elysium.Content.SurfaceSoundTable ----------------------------------------------------------
// Engine tier: the baked `PM_*` assets under `/ElysiumBaked/SurfaceProperties`, read through the
// real `IElysiumEmbodiment::ResolveSurfaceSounds` on a map actor.

namespace
{
	// The step pools the export carries, read out of
	// `$ELYSIUM_WORK_ROOT/exports_v2/surface-properties/<name>.glb` extension `footsteps.left` /
	// `footsteps.right`. These 17 entries are the ones that DECLARE a pool; the importer flattens
	// each `base` chain, so 46 of the 63 baked assets carry one (`brick` inherits `concrete`'s).
	// Pinned here as a recovery: a re-export that dropped an alternate would fail this.
	struct FDeclaredPool
	{
		const TCHAR* Surface;
		int32 Left;
		int32 Right;
	};
	const FDeclaredPool GDeclaredStepPools[] =
	{
		{ TEXT("carpet"), 2, 2 },     { TEXT("concrete"), 4, 4 },   { TEXT("default"), 4, 4 },
		{ TEXT("dirt"), 2, 2 },       { TEXT("grass"), 4, 4 },      { TEXT("gravel"), 2, 2 },
		{ TEXT("ladder"), 2, 2 },     { TEXT("metal"), 2, 2 },      { TEXT("metalgrate"), 2, 2 },
		{ TEXT("metalvent"), 2, 2 },  { TEXT("mud"), 2, 2 },        { TEXT("quiet"), 1, 1 },
		{ TEXT("sand"), 2, 2 },       { TEXT("tile"), 4, 4 },       { TEXT("wade"), 2, 2 },
		{ TEXT("water"), 2, 2 },      { TEXT("wood"), 4, 4 },
	};
}

bool } // namespace ElysiumFootstepSeamTests

#endif // WITH_DEV_AUTOMATION_TESTS
