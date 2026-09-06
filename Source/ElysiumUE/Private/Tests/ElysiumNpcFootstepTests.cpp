// The NPC footstep arm of `CAI_BaseNPC::HandleAnimEvent` (`0x10274e30` -> `0x1026d460`), end to
// end against the recording services.
//
// Every rule asserted here is a fact from `docs/vtmb/footsteps.md` §1 and §5.1 and the disassembly
// it cites. The `Elysium.Substrate.*` cases are content-free — the seam is the recording double, so
// what they prove is the CHAIN: which id is claimed, what gate silences it, where the volume and the
// distance come from, which pool the coin flip draws, and what request reaches
// `IElysiumAudio::PlayBodySound`. `Elysium.Content.FootstepRecords` at the bottom is the other half,
// because "retail's 2052 reaches this runtime" cannot be asserted without a real baked clip carrying
// the record.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumFootstepTuning.h"
#include "ElysiumRng.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumRulebook.h"
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "Tests/ElysiumNpcTestHooks.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumCharacterAssets.h"

#include "Templates/UniquePtr.h"

namespace ElysiumNpcFootstepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The surface the walker stands on. Two alternates a side, because a pool with more than one
	// entry is what makes the second draw visible and the duplicate below is what proves duplicates
	// are kept rather than collapsed (`FElysiumSurfaceSounds`'s own contract).
	const TCHAR* const ConcreteLeftA = TEXT("surfaces/concrete/concrete_stepleft1.wav");
	const TCHAR* const ConcreteLeftB = TEXT("surfaces/concrete/concrete_stepleft2.wav");
	const TCHAR* const ConcreteRightA = TEXT("surfaces/concrete/concrete_stepright1.wav");
	const TCHAR* const ConcreteRightB = TEXT("surfaces/concrete/concrete_stepright2.wav");

	FElysiumSurfaceSounds ConcreteRow()
	{
		FElysiumSurfaceSounds Row;
		Row.StepLeft = { ConcreteLeftA, ConcreteLeftB, ConcreteLeftA };   // the duplicate is authored
		Row.StepRight = { ConcreteRightA, ConcreteRightB };
		Row.GameMaterial = TEXT("C");
		return Row;
	}

	// A surface whose baked record resolves and carries no step key anywhere in its `base` chain.
	// 37 of the 63 shipped entries are shaped like this.
	FElysiumSurfaceSounds PoollessRow()
	{
		FElysiumSurfaceSounds Row;
		Row.Impact = TEXT("Glass.Impact");
		return Row;
	}

	// The shipped authoring: `npctemplate*.txt` states 0.45/300 and 0.85/600 where it states these
	// at all, and the keys go into `General` FOLDED, which is how the KV reader stores every pair.
	FElysiumClanTemplate ShippedTemplate()
	{
		FElysiumClanTemplate Template;
		Template.TemplateName = TEXT("Test_Footfall");
		Template.General.Add(TEXT("normalfootfallvol"), TEXT("0.45"));
		Template.General.Add(TEXT("normalfootfalldist"), TEXT("300"));
		Template.General.Add(TEXT("heavyfootfallvol"), TEXT("0.85"));
		Template.General.Add(TEXT("heavyfootfalldist"), TEXT("600"));
		return Template;
	}

	FElysiumAnimEvent Ev(int32 Event)
	{
		FElysiumAnimEvent Record;
		Record.Event = Event;
		return Record;
	}

	// One bodied pedestrian to step, one BODILESS pedestrian to open a conversation on, and one
	// Tzimisce runner — the only species row in the table whose classname this port registers.
	//
	// The talker carries no model on purpose. A bodied NPC's `BeginDialogueBodySession` is refused
	// until its mind has been admitted by a think, and this fixture never thinks; a bodiless one has
	// no body to arbitrate over and opens the session directly. The gate under test is the PLAYER's
	// (`+0x19b4`), so who owns the conversation is immaterial to it.
	FElysiumEntityDefs MakeFootstepDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__footstep_test__");
		auto Add = [&Defs](const TCHAR* Classname, const TCHAR* Name, bool bBodied)
		{
			FElysiumEntityDef Def;
			Def.Classname = Classname;
			Def.TargetName = Name;
			if (bBodied)
			{
				// A model, because the motor is built beside the body and the surface a step reads
				// is the motor's published cache.
				Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
			}
			Defs.Defs.Add(MoveTemp(Def));
		};
		Add(TEXT("npc_VPedestrian"), TEXT("walker"), /*bBodied*/ true);
		Add(TEXT("npc_VPedestrian"), TEXT("talker"), /*bBodied*/ false);
		Add(TEXT("npc_VTzimisceRunner"), TEXT("runner"), /*bBodied*/ true);
		return Defs;
	}

	// The whole fixture, because six of the cases below need exactly the same five lines and a
	// shared world would make the RNG-pinned ones depend on the order they run in.
	struct FStepWorld
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumNpc* Walker = nullptr;
		FElysiumNpc* Talker = nullptr;
		FElysiumNpc* Runner = nullptr;

		// The motor a given NPC's body was built with — matched on the owner handle rather than
		// taken as "the last one", because three bodied NPCs stand three motors.
		FElysiumRecordingNpcMotor* MotorFor(const FElysiumNpc* Npc) const
		{
			for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
			{
				if (Npc != nullptr && Motor.IsValid() && Motor->Owner == Npc->Handle)
				{
					return Motor.Get();
				}
			}
			return nullptr;
		}

		void Build()
		{
			Services.bProvideNpcMotor = true;
			Services.SurfaceSounds.Add(FName(TEXT("concrete")), ConcreteRow());
			Services.SurfaceSounds.Add(FName(TEXT("glass")), PoollessRow());
			World = MakeUnique<FElysiumEntityWorld>(static_cast<AActor*>(nullptr),
				static_cast<UElysiumGameStateSubsystem*>(nullptr), Services.Bundle());
			World->Load(MakeFootstepDefs());
			World->Activate(0.0);
			// One deterministic think, no executor action: `Activate` only ARMS the mind's admission
			// barrier, and an unadmitted NPC refuses `BeginDialogueBodySession`, which the muted case
			// needs in order to open a conversation at all. Same line, same reason, as
			// `ElysiumDialogueCameraTests`.
			World->Tick(0.0);
			auto Find = [this](const TCHAR* Name) -> FElysiumNpc*
			{
				FElysiumEntity* Ent = World->FindByName(Name);
				return Ent != nullptr ? Ent->AsNpc() : nullptr;
			};
			Walker = Find(TEXT("walker"));
			Talker = Find(TEXT("talker"));
			Runner = Find(TEXT("runner"));
		}
	};

	// A conversation with one line, the shape `ElysiumDialogueCameraTests` opens headlessly.
	TSharedRef<FElysiumDlgConversation> MakeOneLineConversation()
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		FElysiumDlgLine& Line = File->Lines.AddDefaulted_GetRef();
		Line.Id = 1;
		Line.TextMale = TEXT("Test line");
		Line.Link = TEXT("#");
		Line.Role = EElysiumDlgRole::NpcLine;
		File->IndexById.Add(1, 0);
		TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
			File, true, false, [](const FString&) { return true; }, [](const FString&) {});
		Conversation->Start();
		return Conversation;
	}
}

// --- Elysium.Substrate.Footsteps.NpcNormal -----------------------------------------------------
//
// 2050 on a body whose motor publishes `concrete`, with the shipped template: one `PlayBodySound`
// out of concrete's pools, volume 0.45, level 58, channel 4.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepNormalTest,
	"Elysium.Substrate.Footsteps.NpcNormal", GElysiumTestFlags)
bool FElysiumNpcFootstepNormalTest::RunTest(const FString&)
{
	FStepWorld Fixture;
	Fixture.Build();
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker))
	{
		return false;
	}
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	Motor->GroundSurface = FName(TEXT("concrete"));
	TestTrue(TEXT("the fixture template applies"),
		ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate()));

	Fixture.Services.Calls.Reset();
	Fixture.Services.BodySounds.Reset();
	Fixture.Services.BodySoundOwners.Reset();

	TestTrue(TEXT("2050 is claimed"),
		Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft)));
	if (!TestEqual(TEXT("...and made exactly one body sound"),
		Fixture.Services.BodySounds.Num(), 1))
	{
		return false;
	}
	const FElysiumBodySound& Sound = Fixture.Services.BodySounds[0];
	// `1026d68d`: the template's volume reaches the emit unmodified.
	TestEqual(TEXT("the volume is the template's NormalFootfallVol"), Sound.Volume, 0.45f, 1e-4f);
	// `0x10228350`: 20*log10(300/36) + 40, truncated.
	TestEqual(TEXT("300 units is soundlevel 58"), Sound.SoundLevelDb, 58);
	TestEqual(TEXT("the pitch is retail's hard 100"), Sound.Pitch, 1.0f, 1e-4f);
	TestEqual(TEXT("the channel is CHAN_BODY"),
		static_cast<int32>(Sound.Channel), static_cast<int32>(EElysiumSoundChannel::Body));
	const bool bFromConcrete = Sound.Rel == ConcreteLeftA || Sound.Rel == ConcreteLeftB
		|| Sound.Rel == ConcreteRightA || Sound.Rel == ConcreteRightB;
	TestTrue(FString::Printf(TEXT("the wav comes from concrete's pools ('%s')"), *Sound.Rel),
		bFromConcrete);
	TestTrue(TEXT("...for this body"),
		Fixture.Services.BodySoundOwners[0] == Fixture.Walker->Handle);
	TestEqual(TEXT("the surface table was asked for the motor's surface"),
		Fixture.Services.Count(TEXT("ResolveSurfaceSounds concrete -> yes")), 1);

	// 2051 is the SAME arm: the left/right in the id is discarded and only the mode is carried.
	Fixture.Services.BodySounds.Reset();
	TestTrue(TEXT("2051 is claimed too"),
		Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkRight)));
	if (TestEqual(TEXT("...and is the same normal footfall"), Fixture.Services.BodySounds.Num(), 1))
	{
		TestEqual(TEXT("...at the same volume"), Fixture.Services.BodySounds[0].Volume, 0.45f, 1e-4f);
		TestEqual(TEXT("...and the same level"), Fixture.Services.BodySounds[0].SoundLevelDb, 58);
	}

	// An id in no footstep arm still falls to the chain — the ornament band and then the census.
	TestFalse(TEXT("an unrelated id is not claimed by the footstep arm"),
		Fixture.Walker->HandleAnimEvent(Ev(2040)));
	return true;
}

// --- Elysium.Substrate.Footsteps.NpcHeavy ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepHeavyTest,
	"Elysium.Substrate.Footsteps.NpcHeavy", GElysiumTestFlags)
bool FElysiumNpcFootstepHeavyTest::RunTest(const FString&)
{
	FStepWorld Fixture;
	Fixture.Build();
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker)
		|| !TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	Motor->GroundSurface = FName(TEXT("concrete"));
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate());

	for (const int32 EventId : { ElysiumFootsteps::EventRunLeft, ElysiumFootsteps::EventRunRight })
	{
		Fixture.Services.BodySounds.Reset();
		TestTrue(FString::Printf(TEXT("%d is claimed"), EventId),
			Fixture.Walker->HandleAnimEvent(Ev(EventId)));
		if (!TestEqual(TEXT("...and made one body sound"), Fixture.Services.BodySounds.Num(), 1))
		{
			return false;
		}
		// `1026d51c`: the HEAVY pair, which is the mode's only effect.
		TestEqual(TEXT("the volume is HeavyFootfallVol"),
			Fixture.Services.BodySounds[0].Volume, 0.85f, 1e-4f);
		TestEqual(TEXT("600 units is soundlevel 64"),
			Fixture.Services.BodySounds[0].SoundLevelDb, 64);
	}
	return true;
}

// --- Elysium.Substrate.Footsteps.NpcCvarPath ---------------------------------------------------
//
// `footstep_npc_use_templates 0` (`0x109203f8`): the template is never consulted and the four cvars
// are. The shipped defaults are 0.5/256 and 0.85/512, which are levels 57 and 63.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepCvarPathTest,
	"Elysium.Substrate.Footsteps.NpcCvarPath", GElysiumTestFlags)
bool FElysiumNpcFootstepCvarPathTest::RunTest(const FString&)
{
	// The pure rule first, both sources, so the entity case below is only about the wiring.
	{
		FElysiumFootstepTuning Tuning;   // retail defaults
		TestTrue(TEXT("footstep_npc_use_templates defaults to 1"), Tuning.bNpcUseTemplates);
		const FElysiumClanTemplate Template = ShippedTemplate();

		const ElysiumFootsteps::FStepSource Normal =
			ElysiumFootsteps::NpcSource(&Template, /*bHeavy*/ false, Tuning);
		TestEqual(TEXT("the template's normal volume"), Normal.Volume, 0.45f, 1e-4f);
		TestEqual(TEXT("the template's normal distance"), Normal.DistanceUnits, 300.f, 1e-3f);

		// A template that authors none of the four takes `0x101d3f10`'s own defaults, and so does a
		// null template — an NPC with no `stattemplate` has no record to read.
		const FElysiumClanTemplate Bare;
		const ElysiumFootsteps::FStepSource BareHeavy =
			ElysiumFootsteps::NpcSource(&Bare, /*bHeavy*/ true, Tuning);
		TestEqual(TEXT("an unauthored heavy volume is the loader default"),
			BareHeavy.Volume, ElysiumFootstep::TemplateHeavyVolDefault, 1e-4f);
		const ElysiumFootsteps::FStepSource NoTemplate =
			ElysiumFootsteps::NpcSource(nullptr, /*bHeavy*/ false, Tuning);
		TestEqual(TEXT("no template at all answers the loader default volume"),
			NoTemplate.Volume, ElysiumFootstep::TemplateNormalVolDefault, 1e-4f);
		TestEqual(TEXT("...and the loader default distance"),
			NoTemplate.DistanceUnits, ElysiumFootstep::TemplateNormalDistDefault, 1e-3f);

		Tuning.bNpcUseTemplates = false;
		const ElysiumFootsteps::FStepSource CvarNormal =
			ElysiumFootsteps::NpcSource(&Template, /*bHeavy*/ false, Tuning);
		TestEqual(TEXT("the cvar arm ignores the template's volume"), CvarNormal.Volume, 0.5f, 1e-4f);
		TestEqual(TEXT("...and its distance"), CvarNormal.DistanceUnits, 256.f, 1e-3f);
		const ElysiumFootsteps::FStepSource CvarHeavy =
			ElysiumFootsteps::NpcSource(&Template, /*bHeavy*/ true, Tuning);
		TestEqual(TEXT("footstep_heavy_vol is 0.85"), CvarHeavy.Volume, 0.85f, 1e-4f);
		TestEqual(TEXT("footstep_heavy_dist is 512"), CvarHeavy.DistanceUnits, 512.f, 1e-3f);
		TestEqual(TEXT("256 units is soundlevel 57"),
			ElysiumSoundLevel::FromDistanceUnits(CvarNormal.DistanceUnits), 57);
		TestEqual(TEXT("512 units is soundlevel 63"),
			ElysiumSoundLevel::FromDistanceUnits(CvarHeavy.DistanceUnits), 63);
	}

	FStepWorld Fixture;
	Fixture.Build();
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker)
		|| !TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	Motor->GroundSurface = FName(TEXT("concrete"));
	// The template IS applied, so the case proves the cvar wins rather than that there was nothing
	// to read.
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate());
	Fixture.World->FootstepTuning().bNpcUseTemplates = false;

	Fixture.Services.BodySounds.Reset();
	Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft));
	if (TestEqual(TEXT("the cvar walk step plays"), Fixture.Services.BodySounds.Num(), 1))
	{
		TestEqual(TEXT("at footstep_normal_vol"), Fixture.Services.BodySounds[0].Volume, 0.5f, 1e-4f);
		TestEqual(TEXT("and level 57"), Fixture.Services.BodySounds[0].SoundLevelDb, 57);
	}

	Fixture.Services.BodySounds.Reset();
	Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventRunRight));
	if (TestEqual(TEXT("the cvar run step plays"), Fixture.Services.BodySounds.Num(), 1))
	{
		TestEqual(TEXT("at footstep_heavy_vol"), Fixture.Services.BodySounds[0].Volume, 0.85f, 1e-4f);
		TestEqual(TEXT("and level 63"), Fixture.Services.BodySounds[0].SoundLevelDb, 63);
	}
	return true;
}

// --- Elysium.Substrate.Footsteps.NpcMuted ------------------------------------------------------
//
// `1026d467`: the three predicates on the LOCAL PLAYER. The gate is global — while one of them
// holds, NO NPC in the level makes a footfall — and the id is still claimed.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepMutedTest,
	"Elysium.Substrate.Footsteps.NpcMuted", GElysiumTestFlags)
bool FElysiumNpcFootstepMutedTest::RunTest(const FString&)
{
	FStepWorld Fixture;
	Fixture.Build();
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker)
		|| !TestNotNull(TEXT("the talker exists"), Fixture.Talker)
		|| !TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	Motor->GroundSurface = FName(TEXT("concrete"));
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate());

	TestFalse(TEXT("an ordinary world mutes nothing"),
		ElysiumFootsteps::NpcStepsMuted(*Fixture.World));

	auto StepsSilently = [&](const TCHAR* Why)
	{
		TestTrue(FString::Printf(TEXT("%s: the world reports muted"), Why),
			ElysiumFootsteps::NpcStepsMuted(*Fixture.World));
		Fixture.Services.BodySounds.Reset();
		Fixture.Services.Calls.Reset();
		TestTrue(FString::Printf(TEXT("%s: 2050 is still CLAIMED"), Why),
			Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft)));
		TestEqual(FString::Printf(TEXT("%s: and nothing plays"), Why),
			Fixture.Services.BodySounds.Num(), 0);
		// The gate is checked BEFORE the surface lookup (`1026d467` precedes `1026d597`), so a muted
		// step must not even ask the table.
		TestEqual(FString::Printf(TEXT("%s: the surface table is not consulted"), Why),
			Fixture.Services.Count(TEXT("ResolveSurfaceSounds concrete -> yes")), 0);
	};

	// `+0x19c0` — the scripted camera view entity.
	Fixture.World->SetScriptedCamera(TEXT("shots/test.txt"), Fixture.Talker->Handle);
	StepsSilently(TEXT("a scripted camera"));
	Fixture.World->ClearScriptedCamera();
	TestFalse(TEXT("clearing it un-mutes the level"),
		ElysiumFootsteps::NpcStepsMuted(*Fixture.World));

	// `+0x19cc` — the camera target role.
	Fixture.World->SelectTrackCameraRole(/*bTargetRole*/ false, Fixture.Talker->Handle);
	Fixture.World->PublishTrackCamera(/*bTargetRole*/ false, Fixture.Talker->Handle,
		FVector(10.f, 0.f, 0.f), FRotator::ZeroRotator, 0.f, 75.f, 0.f);
	StepsSilently(TEXT("a camera track"));
	Fixture.World->ClearTrackCamera();
	TestFalse(TEXT("clearing the track un-mutes the level"),
		ElysiumFootsteps::NpcStepsMuted(*Fixture.World));

	// `+0x19b4` with `+0x1ec4 > 0` — a dialogue/control entity owns the player. The conversation is
	// opened on the OTHER NPC, because the gate is the player's and not the stepper's.
	Fixture.World->OpenDialog(Fixture.Talker->Handle, MakeOneLineConversation());
	StepsSilently(TEXT("an open conversation"));
	Fixture.World->CloseDialog(/*bSilent*/ true);

	// And with all three down the same body steps, so the case proves a gate rather than a body that
	// could not step at all.
	Fixture.Services.BodySounds.Reset();
	Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft));
	TestEqual(TEXT("with the gate down the step plays"), Fixture.Services.BodySounds.Num(), 1);
	return true;
}

// --- Elysium.Substrate.Footsteps.NpcNoSurface --------------------------------------------------
//
// `1026d597`: `if (!this->m_pSurfaceData /* +0x5b90 */) return;`. A body that has never travelled
// has no cached surface, and its footfall is silent rather than defaulted.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepNoSurfaceTest,
	"Elysium.Substrate.Footsteps.NpcNoSurface", GElysiumTestFlags)
bool FElysiumNpcFootstepNoSurfaceTest::RunTest(const FString&)
{
	FStepWorld Fixture;
	Fixture.Build();
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker)
		|| !TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate());
	TestTrue(TEXT("a body that has never travelled publishes no surface"),
		Motor->GroundSurface.IsNone());

	Fixture.Services.BodySounds.Reset();
	Fixture.Services.Calls.Reset();
	// Four footfalls: the report is once per BODY, and the id is claimed every time.
	for (const int32 EventId : { ElysiumFootsteps::EventWalkLeft, ElysiumFootsteps::EventWalkRight,
		ElysiumFootsteps::EventRunLeft, ElysiumFootsteps::EventRunRight })
	{
		TestTrue(FString::Printf(TEXT("%d is claimed with no surface"), EventId),
			Fixture.Walker->HandleAnimEvent(Ev(EventId)));
	}
	TestEqual(TEXT("...and nothing played"), Fixture.Services.BodySounds.Num(), 0);
	TestEqual(TEXT("...and the surface table was never asked"),
		Fixture.Services.Count(TEXT("ResolveSurfaceSounds")), 0);

	// Give the body a surface and the same id sounds, so "silent" is the null surface and not the
	// handler refusing.
	Motor->GroundSurface = FName(TEXT("concrete"));
	Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft));
	TestEqual(TEXT("with a surface under it the body steps"), Fixture.Services.BodySounds.Num(), 1);
	return true;
}

// --- Elysium.Substrate.Footsteps.NpcNoPool -----------------------------------------------------
//
// A surface that RESOLVES and carries no step key anywhere in its chain, and a surface the table
// does not carry at all. Both are `1026d666`'s empty-name return.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepNoPoolTest,
	"Elysium.Substrate.Footsteps.NpcNoPool", GElysiumTestFlags)
bool FElysiumNpcFootstepNoPoolTest::RunTest(const FString&)
{
	FStepWorld Fixture;
	Fixture.Build();
	FElysiumRecordingNpcMotor* Motor = Fixture.MotorFor(Fixture.Walker);
	if (!TestNotNull(TEXT("the walker exists"), Fixture.Walker)
		|| !TestNotNull(TEXT("its body stood a motor"), Motor))
	{
		return false;
	}
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Walker, ShippedTemplate());

	// A row with no pools at all.
	Motor->GroundSurface = FName(TEXT("glass"));
	Fixture.Services.BodySounds.Reset();
	Fixture.Services.Calls.Reset();
	TestTrue(TEXT("a poolless surface still claims 2050"),
		Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft)));
	TestEqual(TEXT("...the table WAS asked"),
		Fixture.Services.Count(TEXT("ResolveSurfaceSounds glass -> yes")), 1);
	TestEqual(TEXT("...and nothing played"), Fixture.Services.BodySounds.Num(), 0);

	// A surface the table does not carry — the headless answer the interface states.
	Motor->GroundSurface = FName(TEXT("unbaked_surface"));
	Fixture.Services.BodySounds.Reset();
	TestTrue(TEXT("an unresolvable surface still claims 2052"),
		Fixture.Walker->HandleAnimEvent(Ev(ElysiumFootsteps::EventRunLeft)));
	TestEqual(TEXT("...and nothing played"), Fixture.Services.BodySounds.Num(), 0);

	// The pure rule's own half: an empty pool answers null rather than an empty string, and a
	// HALF-authored surface is silent on exactly one side.
	{
		FRandomStream Stream(1234);
		FElysiumSurfaceSounds Empty;
		TestNull(TEXT("a surface with no pools picks nothing"),
			ElysiumFootsteps::PickNpcWav(Empty, Stream));

		FElysiumSurfaceSounds LeftOnly;
		LeftOnly.StepLeft = { ConcreteLeftA };
		int32 Played = 0;
		int32 Silent = 0;
		for (int32 i = 0; i < 200; ++i)
		{
			(ElysiumFootsteps::PickNpcWav(LeftOnly, Stream) != nullptr ? Played : Silent)++;
		}
		TestTrue(TEXT("a left-only surface sounds on some draws"), Played > 0);
		TestTrue(TEXT("...and is silent on the others, which is retail's empty stepright"),
			Silent > 0);
	}
	return true;
}

// --- Elysium.Substrate.Footsteps.CoinFlip ------------------------------------------------------
//
// `1026d626`: `RandomInt(0,1)` — non-zero takes `stepleft`, zero takes `stepright`. The foot the
// event named is thrown away. Pinned stream, so the sequence is a value and not a distribution.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepCoinFlipTest,
	"Elysium.Substrate.Footsteps.CoinFlip", GElysiumTestFlags)
bool FElysiumNpcFootstepCoinFlipTest::RunTest(const FString&)
{
	const FElysiumSurfaceSounds Concrete = ConcreteRow();

	// The oracle: the same stream, drawn by hand in the same order the rule draws it — the side
	// first, then the index inside the chosen pool.
	FRandomStream Actual(20260906);
	FRandomStream Expected(20260906);
	int32 Lefts = 0;
	int32 Rights = 0;
	for (int32 i = 0; i < 64; ++i)
	{
		const bool bLeft = Expected.RandRange(0, 1) != 0;
		const TArray<FString>& Pool = bLeft ? Concrete.StepLeft : Concrete.StepRight;
		const FString& Want = Pool[Expected.RandRange(0, Pool.Num() - 1)];
		(bLeft ? Lefts : Rights)++;

		const FString* Got = ElysiumFootsteps::PickNpcWav(Concrete, Actual);
		if (!TestNotNull(FString::Printf(TEXT("draw %d picks something"), i), Got))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("draw %d matches the stream's own sequence"), i), *Got, Want);
	}
	// Both sides are reachable — otherwise the equality above would pass on a rule that always took
	// the same branch.
	TestTrue(TEXT("the flip reached stepleft"), Lefts > 0);
	TestTrue(TEXT("the flip reached stepright"), Rights > 0);

	// Duplicates in a pool are ALTERNATES and legal picks: concrete's left pool authors
	// `stepleft1` twice, and the rule must be able to answer the repeated entry.
	{
		FRandomStream Stream(7);
		bool bSawDuplicateEntry = false;
		for (int32 i = 0; i < 400 && !bSawDuplicateEntry; ++i)
		{
			const FString* Got = ElysiumFootsteps::PickNpcWav(Concrete, Stream);
			bSawDuplicateEntry = Got != nullptr && *Got == ConcreteLeftA;
		}
		TestTrue(TEXT("the duplicated pool entry is reachable"), bSawDuplicateEntry);
		TestEqual(TEXT("and the pool kept its duplicate rather than collapsing it"),
			Concrete.StepLeft.Num(), 3);
	}

	// The foot in the id is discarded: the same body fed 2050 and 2051 draws from the same stream in
	// the same order, so a run of one id and a run of the other are indistinguishable.
	TestFalse(TEXT("2050 is not the heavy mode"),
		ElysiumFootsteps::IsHeavyFootstep(ElysiumFootsteps::EventWalkLeft));
	TestTrue(TEXT("2052 is"),
		ElysiumFootsteps::IsHeavyFootstep(ElysiumFootsteps::EventRunLeft));
	TestTrue(TEXT("all four ids are footsteps"),
		ElysiumFootsteps::IsFootstepEvent(2050) && ElysiumFootsteps::IsFootstepEvent(2051)
		&& ElysiumFootsteps::IsFootstepEvent(2052) && ElysiumFootsteps::IsFootstepEvent(2053));
	TestFalse(TEXT("2049 and 2054 are not"),
		ElysiumFootsteps::IsFootstepEvent(2049) || ElysiumFootsteps::IsFootstepEvent(2054));
	return true;
}

// --- Elysium.Substrate.Footsteps.SpeciesPolicy -------------------------------------------------
//
// `docs/vtmb/footsteps.md` §1.7: the overrides that replace `0x1026d460` outright.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFootstepSpeciesTest,
	"Elysium.Substrate.Footsteps.SpeciesPolicy", GElysiumTestFlags)
bool FElysiumNpcFootstepSpeciesTest::RunTest(const FString&)
{
	// --- The table, as data -------------------------------------------------------------------
	{
		TestNull(TEXT("an ordinary pedestrian has no override"),
			ElysiumFootsteps::SpeciesFor(TEXT("npc_VPedestrian")));
		TestNull(TEXT("an empty classname has none either"),
			ElysiumFootsteps::SpeciesFor(FString()));

		const FElysiumFootstepSpecies* Ming = ElysiumFootsteps::SpeciesFor(TEXT("npc_vmingxiao"));
		if (TestNotNull(TEXT("Ming Xiao's row resolves case-insensitively"), Ming))
		{
			TestEqual(TEXT("...and is Silent"),
				static_cast<int32>(Ming->Policy), static_cast<int32>(EElysiumFootstepPolicy::Silent));
			TestTrue(TEXT("...claiming 2050"),
				ElysiumFootsteps::SpeciesClaims(*Ming, ElysiumFootsteps::EventWalkLeft));
			// 2052/2053 are not in `0x10392a70`'s switch.
			TestFalse(TEXT("...and leaving 2052 to the base handler"),
				ElysiumFootsteps::SpeciesClaims(*Ming, ElysiumFootsteps::EventRunLeft));
			FRandomStream Stream(3);
			TestNull(TEXT("...and drawing no wav at all"),
				ElysiumFootsteps::PickSpeciesWav(*Ming, ElysiumFootsteps::EventWalkLeft, Stream));
		}

		const FElysiumFootstepSpecies* Shark = ElysiumFootsteps::SpeciesFor(TEXT("npc_VHengeyokai"));
		if (TestNotNull(TEXT("the Hengeyokai's row resolves"), Shark))
		{
			TestEqual(TEXT("its shake amplitude is 2.0"), Shark->ShakeAmplitude, 2.0f, 1e-4f);
			TestEqual(TEXT("its frequency is 0.2"), Shark->ShakeFrequency, 0.2f, 1e-4f);
			TestEqual(TEXT("its duration is 0.2 s"), Shark->ShakeDurationSeconds, 0.2f, 1e-4f);
			TestEqual(TEXT("its radius is 1024"), Shark->ShakeRadiusUnits, 1024.f, 1e-3f);
			// `0x1037fb60` is the only row whose switch covers all four ids.
			TestTrue(TEXT("it claims the run pair too"),
				ElysiumFootsteps::SpeciesClaims(*Shark, ElysiumFootsteps::EventRunRight));
			TestEqual(TEXT("its pool is four stomps"), Shark->LeftWavs.Num(), 4);
			TestTrue(TEXT("and the foot is not read — both sides are the same pool"),
				Shark->RightWavs.GetData() == Shark->LeftWavs.GetData());
		}

		const FElysiumFootstepSpecies* Claw =
			ElysiumFootsteps::SpeciesFor(TEXT("npc_VTzimisceHeadClaw"));
		if (TestNotNull(TEXT("the head-claw's row resolves"), Claw))
		{
			TestEqual(TEXT("its shake amplitude is 1.3"), Claw->ShakeAmplitude, 1.3f, 1e-4f);
			TestFalse(TEXT("and it leaves 2052 to the base"),
				ElysiumFootsteps::SpeciesClaims(*Claw, ElysiumFootsteps::EventRunLeft));
		}

		const FElysiumFootstepSpecies* Runner =
			ElysiumFootsteps::SpeciesFor(TEXT("npc_VTzimisceRunner"));
		if (TestNotNull(TEXT("the runner's row resolves"), Runner))
		{
			TestEqual(TEXT("it is CustomWav — no shake"), static_cast<int32>(Runner->Policy),
				static_cast<int32>(EElysiumFootstepPolicy::CustomWav));
			TestEqual(TEXT("and raises no shake"), Runner->ShakeAmplitude, 0.0f, 1e-6f);
			TestEqual(TEXT("its two pools are two wavs each"), Runner->LeftWavs.Num(), 2);
			TestEqual(TEXT("and the breath pool is four"), Runner->ExtraWavs.Num(), 4);
			// The flag `103c32eb`/`103c32d9` passes: 2050 -> 1, 2051 -> 0, and the two ids therefore
			// draw from different pools.
			FRandomStream Stream(11);
			const TCHAR* Left =
				ElysiumFootsteps::PickSpeciesWav(*Runner, ElysiumFootsteps::EventWalkLeft, Stream);
			const TCHAR* Right =
				ElysiumFootsteps::PickSpeciesWav(*Runner, ElysiumFootsteps::EventWalkRight, Stream);
			if (TestNotNull(TEXT("2050 draws a wav"), Left)
				&& TestNotNull(TEXT("2051 draws a wav"), Right))
			{
				TestTrue(TEXT("2050 draws from foot_steps_1/2"),
					FString(Left).Contains(TEXT("foot_steps_1"))
					|| FString(Left).Contains(TEXT("foot_steps_2")));
				TestTrue(TEXT("2051 draws from foot_steps_3/4"),
					FString(Right).Contains(TEXT("foot_steps_3"))
					|| FString(Right).Contains(TEXT("foot_steps_4")));
			}
		}
	}

	// --- The live row: `npc_VTzimisceRunner` is a registered classname -------------------------
	FStepWorld Fixture;
	Fixture.Build();
	if (!TestNotNull(TEXT("the runner exists"), Fixture.Runner)
		|| !TestNotNull(TEXT("the walker exists"), Fixture.Walker))
	{
		return false;
	}
	if (FElysiumRecordingNpcMotor* RunnerMotor = Fixture.MotorFor(Fixture.Runner))
	{
		// A surface UNDER it, so a silent species is provably the override and not a missing floor.
		RunnerMotor->GroundSurface = FName(TEXT("concrete"));
	}
	ElysiumNpcTestHooks::ApplyResolvedTemplate(*Fixture.Runner, ShippedTemplate());

	Fixture.Services.BodySounds.Reset();
	Fixture.Services.Calls.Reset();
	TestTrue(TEXT("2050 on the runner is claimed"),
		Fixture.Runner->HandleAnimEvent(Ev(ElysiumFootsteps::EventWalkLeft)));
	// The vfunc emits BOTH: a footfall and a breath, on the same channel.
	if (TestEqual(TEXT("...playing the row's two fixed wavs"),
		Fixture.Services.BodySounds.Num(), 2))
	{
		const FElysiumBodySound& Step = Fixture.Services.BodySounds[0];
		TestTrue(FString::Printf(TEXT("the first is a TC_Runner footfall ('%s')"), *Step.Rel),
			Step.Rel.Contains(TEXT("TC_Runner/foot_steps_")));
		TestEqual(TEXT("at volume 1.0"), Step.Volume, 1.0f, 1e-4f);
		TestEqual(TEXT("at soundlevel 75, which is ATTN_NORM 0.8 inverted"), Step.SoundLevelDb, 75);
		TestEqual(TEXT("and pitch 100"), Step.Pitch, 1.0f, 1e-4f);
		TestTrue(TEXT("the second is the breath"),
			Fixture.Services.BodySounds[1].Rel.Contains(TEXT("TC_Runner/Breath")));
	}
	// The override replaces the whole chain, so the surface table is never consulted.
	TestEqual(TEXT("...without reading a surfacedata_t"),
		Fixture.Services.Count(TEXT("ResolveSurfaceSounds concrete -> yes")), 0);

	// 2052/2053 fall through to the base chain, which IS the shared function.
	Fixture.Services.BodySounds.Reset();
	Fixture.Services.Calls.Reset();
	TestTrue(TEXT("2052 on the runner is claimed"),
		Fixture.Runner->HandleAnimEvent(Ev(ElysiumFootsteps::EventRunLeft)));
	TestEqual(TEXT("...through the shared chain, which does read the surface"),
		Fixture.Services.Count(TEXT("ResolveSurfaceSounds concrete -> yes")), 1);
	if (TestEqual(TEXT("...and plays one concrete step"), Fixture.Services.BodySounds.Num(), 1))
	{
		TestTrue(TEXT("from the surface pools, not the species pools"),
			Fixture.Services.BodySounds[0].Rel.Contains(TEXT("surfaces/concrete/")));
		TestEqual(TEXT("at the template's heavy volume"),
			Fixture.Services.BodySounds[0].Volume, 0.85f, 1e-4f);
	}

	// --- The unbuilt screenshake, once per row --------------------------------------------------
	{
		ElysiumFootsteps::ResetUnimplementedShakeReports();
		const FElysiumFootstepSpecies* Shark = ElysiumFootsteps::SpeciesFor(TEXT("npc_VHengeyokai"));
		const FElysiumFootstepSpecies* Runner =
			ElysiumFootsteps::SpeciesFor(TEXT("npc_VTzimisceRunner"));
		if (Shark != nullptr && Runner != nullptr)
		{
			AddExpectedError(TEXT("UNIMPLEMENTED footstep screenshake"),
				EAutomationExpectedErrorFlags::Contains, 1);
			for (int32 i = 0; i < 5; ++i)
			{
				ElysiumFootsteps::ReportUnimplementedShake(*Shark);
			}
			// A row that raises no shake never warns, however often it steps.
			for (int32 i = 0; i < 5; ++i)
			{
				ElysiumFootsteps::ReportUnimplementedShake(*Runner);
			}
		}
	}
	return true;
}

// --- Elysium.Content.FootstepRecords -----------------------------------------------------------
//
// The other half: the baked shared male banks, read the way the runtime loads them. `2050`/`2051`
// on the walk family and `2052`/`2053` on `*_run`, which is
// `docs/vtmb/animation_events.md` -> "2050/2051 versus 2052/2053 is the walk/run split".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepRecordsTest,
	"Elysium.Content.FootstepRecords", GElysiumTestFlags)
bool FElysiumFootstepRecordsTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: native DA_Cast is absent "
			"(run: uv run elysium import characters)"));
		return true;
	}

	// The shared male banks the cast stands its locomotion on. Scanned rather than named one by one
	// because which bank carries the walk cycle is a bake fact, not a recovered one; the assertions
	// below are about the RECORDS, wherever they live.
	static const TCHAR* const Banks[] = {
		TEXT("character_shared_male_move_and_ranged"),
		TEXT("character_shared_male_baseball"),
		TEXT("character_shared_male_fat_male"),
		TEXT("character_shared_male_frenzy"),
		TEXT("character_shared_male_misc"),
		TEXT("character_shared_male_stances"),
	};

	// The four recovered run cycles: `1/3`, `5/9`, `7/9`, `8/9`.
	const float RunCycles[] = { 1.0f / 3.0f, 5.0f / 9.0f, 7.0f / 9.0f, 8.0f / 9.0f };

	int32 BanksRead = 0;
	int32 WalkRecords = 0;
	int32 RunRecords = 0;
	int32 RunClips = 0;
	bool bPairedWalkClip = false;
	bool bPairedRunClip = false;
	TArray<FString> Rows;

	for (const TCHAR* Bank : Banks)
	{
		FString Error;
		if (ElysiumCharacterAssets::Body(Bank, Error) == nullptr)
		{
			continue;
		}
		FElysiumBlendTable Table;
		if (!ElysiumNativeTest::Load(Table, Bank, Error))
		{
			return false;   // `Load` already raised the error
		}
		++BanksRead;

		for (const TPair<FString, TArray<FElysiumAnimEvent>>& Pair : Table.Events)
		{
			bool bClipHasRun = false;
			bool bClipHasBothWalk[2] = { false, false };
			bool bClipHasBothRun[2] = { false, false };
			for (const FElysiumAnimEvent& Record : Pair.Value)
			{
				if (!ElysiumFootsteps::IsFootstepEvent(Record.Event))
				{
					continue;
				}
				Rows.Add(FString::Printf(TEXT("%s: %s: %d @ %.4f"),
					Bank, *Pair.Key, Record.Event, Record.Cycle));
				if (ElysiumFootsteps::IsHeavyFootstep(Record.Event))
				{
					++RunRecords;
					bClipHasRun = true;
					bClipHasBothRun[Record.Event == ElysiumFootsteps::EventRunLeft ? 0 : 1] = true;
					// Every clip carrying the heavy pair is a `*_run` — the corroboration the
					// walk/run reading rests on.
					TestTrue(FString::Printf(
						TEXT("'%s' carries %d and is a run clip"), *Pair.Key, Record.Event),
						Pair.Key.Contains(TEXT("run"), ESearchCase::IgnoreCase));
					bool bKnownCycle = false;
					for (const float Cycle : RunCycles)
					{
						bKnownCycle = bKnownCycle || FMath::IsNearlyEqual(Record.Cycle, Cycle, 1e-3f);
					}
					TestTrue(FString::Printf(
						TEXT("'%s' %d at %.4f is one of the recovered run cycles"),
						*Pair.Key, Record.Event, Record.Cycle), bKnownCycle);
				}
				else
				{
					++WalkRecords;
					bClipHasBothWalk[Record.Event == ElysiumFootsteps::EventWalkLeft ? 0 : 1] = true;
				}
			}
			if (bClipHasRun)
			{
				++RunClips;
			}
			// Both ids of a pair are authored on the same clip, which is why the coin flip's
			// discarded foot costs nothing: the alternation is in the TIMELINE, not in the handler.
			bPairedWalkClip = bPairedWalkClip || (bClipHasBothWalk[0] && bClipHasBothWalk[1]);
			bPairedRunClip = bPairedRunClip || (bClipHasBothRun[0] && bClipHasBothRun[1]);
		}
	}

	if (BanksRead == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: none of the shared male banks is baked"));
		return true;
	}
	Rows.Sort();
	for (const FString& Row : Rows)
	{
		AddInfo(FString::Printf(TEXT("footstep record — %s"), *Row));
	}
	AddInfo(FString::Printf(
		TEXT("%d bank(s) read: %d walk records (2050/2051), %d run records (2052/2053) over %d run clips"),
		BanksRead, WalkRecords, RunRecords, RunClips));

	TestTrue(TEXT("the shared male banks carry walk footfall records at all"), WalkRecords > 0);
	TestTrue(TEXT("...and run footfall records"), RunRecords > 0);
	TestTrue(TEXT("...over at least one run clip"), RunClips > 0);
	TestTrue(TEXT("a walk clip carries BOTH 2050 and 2051"), bPairedWalkClip);
	TestTrue(TEXT("a run clip carries BOTH 2052 and 2053"), bPairedRunClip);
	return true;
}
}

#endif   // WITH_DEV_AUTOMATION_TESTS
