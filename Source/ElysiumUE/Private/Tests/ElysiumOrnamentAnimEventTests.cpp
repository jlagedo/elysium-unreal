// The combat-character band of `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`): the
// `m_hAnimFollowModel` ornament slot (4100/4101/4102), the discipline callback (4020) and the two
// feed boundaries (4006/4007).
//
// Every rule asserted here is a fact from `docs/vtmb/animation_events.md` -> "Port status — combat
// character band" and the disassembly it cites. The two `Elysium.Substrate.*` cases are
// content-free — the seam is the recording double, so what they prove is the HANDLER: which id it
// claims, what string it formats, and what it leaves in the slot. `Elysium.Content.*` at the bottom
// is the other half, because "retail's 4102 reaches this runtime" cannot be asserted without a real
// baked clip carrying the record.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumFeed.h"
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumCharacterAssets.h"

#include "Components/SkeletalMeshComponent.h"

namespace ElysiumOrnamentAnimEventTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The three shipped option spellings, and what `0x1032e330` formats out of each.
	constexpr const TCHAR* CigaretteOption = TEXT("models/items/cigarette/cigarette");
	constexpr const TCHAR* CigaretteMale   = TEXT("models/items/cigarette/cigarette_male.mdl");
	constexpr const TCHAR* CigaretteFemale = TEXT("models/items/cigarette/cigarette_female.mdl");
	// 4100 carries no gender word at all.
	constexpr const TCHAR* WalkieOption = TEXT("models/items/walkie_talkie/walkie_talkie");
	constexpr const TCHAR* WalkiePath   = TEXT("models/items/walkie_talkie/walkie_talkie.mdl");
	// The option that ALREADY carries `.mdl`. Retail strips nothing, so 4102 doubles the extension.
	constexpr const TCHAR* WineOption = TEXT("models/scenery/misc/wineglass/wineglass.mdl");
	constexpr const TCHAR* WinePath   = TEXT("models/scenery/misc/wineglass/wineglass.mdl_male.mdl");

	FElysiumAnimEvent Ev(int32 Event, const TCHAR* Options = TEXT(""))
	{
		FElysiumAnimEvent Record;
		Record.Event = Event;
		Record.Options = Options;
		return Record;
	}

	// Two bodied combat characters, so the paired feed guard has something to resolve.
	FElysiumEntityDefs MakeOrnamentDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__ornament_test__");
		for (const TCHAR* Name : { TEXT("smoker"), TEXT("partner") })
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("npc_VPedestrian");
			Def.TargetName = Name;
			// A model, because the ornament seam is keyed by the body and a bodiless character has
			// nothing to hang one on — which is itself one of the cases below.
			Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
			Defs.Defs.Add(MoveTemp(Def));
		}
		// The same class with no model: it spawns, it has no `Visual`, and the arm must still claim
		// its ids rather than raise.
		FElysiumEntityDef Bodiless;
		Bodiless.Classname = TEXT("npc_VPedestrian");
		Bodiless.TargetName = TEXT("bodiless");
		Defs.Defs.Add(MoveTemp(Bodiless));
		return Defs;
	}

	FElysiumCombatCharacter* Character(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}
}

// --- The pure format rule, and the ids the handler claims ----------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOrnamentAnimEventTest,
	"Elysium.Substrate.OrnamentAnimEvents", GElysiumTestFlags)
bool FElysiumOrnamentAnimEventTest::RunTest(const FString&)
{
	using namespace ElysiumAnimEvents;

	// --- `1032e435` / `1032e448`: the two `Q_snprintf` formats, and the gender word between them --
	{
		TestEqual(TEXT("4102 on a male body formats the gendered path"),
			FormatFollowModelPath(AttachFollowModelGendered, CigaretteOption, /*bMale*/ true),
			FString(CigaretteMale));
		TestEqual(TEXT("4102 on a female body takes the other word"),
			FormatFollowModelPath(AttachFollowModelGendered, CigaretteOption, /*bMale*/ false),
			FString(CigaretteFemale));
		TestEqual(TEXT("4100 formats no gender at all"),
			FormatFollowModelPath(AttachFollowModel, WalkieOption, /*bMale*/ true),
			FString(WalkiePath));
		// The option is NOT extension-stripped: `"%s_%s.mdl"` is applied to whatever the record
		// carries, so a shipped `wineglass.mdl` option doubles the extension. This is the case that
		// makes the catalogue key the FORMATTED path rather than a model id.
		TestEqual(TEXT("an option that already carries .mdl keeps it"),
			FormatFollowModelPath(AttachFollowModelGendered, WineOption, /*bMale*/ true),
			FString(WinePath));
		TestEqual(TEXT("the gender word does not vary with the id"),
			FormatFollowModelPath(AttachFollowModel, CigaretteOption, /*bMale*/ false),
			FString(TEXT("models/items/cigarette/cigarette.mdl")));
		// An empty payload names nothing. Retail would build `".mdl"`, fail `GetModelPtr` and take
		// its failure tail; the port declines to format at all and reaches the same empty slot.
		TestTrue(TEXT("an empty option formats nothing"),
			FormatFollowModelPath(AttachFollowModelGendered, FString(), true).IsEmpty());
		TestTrue(TEXT("a blank option formats nothing either"),
			FormatFollowModelPath(AttachFollowModel, TEXT("  "), true).IsEmpty());
		// The catalogue key contract, shared with `ornament_models.model_key`: case and separators
		// fold, and nothing else does. A key the bake wrote forward-slashed must be reachable from
		// an option the model authored the other way.
		TestEqual(TEXT("case and separators fold exactly as the bake's key does"),
			FormatFollowModelPath(AttachFollowModel, TEXT(" models\\Items\\Can\\Drink_Can "), true),
			FString(TEXT("models/items/can/drink_can.mdl")));
	}

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeOrnamentDefs());
	World.Activate(0.0);

	FElysiumCombatCharacter* Smoker = Character(World, TEXT("smoker"));
	FElysiumCombatCharacter* Bodiless = Character(World, TEXT("bodiless"));
	if (!TestNotNull(TEXT("the bodied character exists"), Smoker)
		|| !TestNotNull(TEXT("the bodiless one does too"), Bodiless))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the bodied character carries a body"), Smoker->Visual))
	{
		return false;
	}
	TestNull(TEXT("the bodiless one carries none"), Bodiless->Visual);

	// The rows the fixture catalogue carries. `wineglass.mdl_male.mdl` is deliberately absent, so
	// the same test covers both the found and the mock-don't-fail arms.
	Services.OrnamentModels.Add(CigaretteMale);
	Services.OrnamentModels.Add(CigaretteFemale);
	Services.OrnamentModels.Add(WalkiePath);
	Smoker->Sheet.SetMale(true);
	Smoker->RecomputeSheet();   // `IsMale` reads the CURRENT column; the base write lands on it here

	// --- 4102 attaches, and the slot IS the transaction ------------------------------------------
	{
		Services.Calls.Reset();
		TestTrue(TEXT("4102 is claimed"),
			Smoker->HandleAnimEvent(Ev(AttachFollowModelGendered, CigaretteOption)));
		TestEqual(TEXT("...and asked the seam for the formatted path"),
			Services.Count(FString::Printf(TEXT("AttachOrnamentModel %s"), CigaretteMale)), 1);
		TestEqual(TEXT("...leaving the follow-model slot naming it"),
			Smoker->AnimFollowModel, FString(CigaretteMale));
		TestEqual(TEXT("...and the body actually wearing it"),
			Services.WornOrnament(Smoker->Visual), FString(CigaretteMale));
	}

	// --- The re-issue. `cigarette_Idle`/`cigarette_Inhale` fire 4102 on every loop wrap ----------
	{
		Services.Calls.Reset();
		TestTrue(TEXT("a second 4102 for the same path is claimed again"),
			Smoker->HandleAnimEvent(Ev(AttachFollowModelGendered, CigaretteOption)));
		// The SUBSTRATE re-issues unconditionally, exactly as `0x1032e330` does — it removes and
		// re-creates with no comparison of its own. The no-op that keeps a smoking body from
		// churning a component twice a second is the visual layer's, stated as a modernization in
		// `ElysiumNpcVisual::InstallOrnamentModel`, and is invisible from here by design.
		TestEqual(TEXT("...reaching the seam a second time, as retail's unconditional remove/create does"),
			Services.Count(FString::Printf(TEXT("AttachOrnamentModel %s"), CigaretteMale)), 1);
		// ONE seam call, not a detach followed by an attach. The attach contract is replace, and a
		// separate detach ahead of it would sweep the standing component before the visual layer's
		// same-path guard could see it — the modernization would exist only in its own comment.
		TestEqual(TEXT("...and the handler did not detach separately ahead of it"),
			Services.Count(TEXT("DetachOrnamentModel")), 0);
		TestEqual(TEXT("...and the slot is unchanged"),
			Smoker->AnimFollowModel, FString(CigaretteMale));
	}

	// --- A different path REPLACES ---------------------------------------------------------------
	{
		Services.Calls.Reset();
		TestTrue(TEXT("4100 is claimed"), Smoker->HandleAnimEvent(Ev(AttachFollowModel, WalkieOption)));
		TestEqual(TEXT("...and 4100 takes no gender word"),
			Services.Count(FString::Printf(TEXT("AttachOrnamentModel %s"), WalkiePath)), 1);
		TestEqual(TEXT("...replacing the slot rather than adding to it"),
			Smoker->AnimFollowModel, FString(WalkiePath));
		TestEqual(TEXT("...and the body wears exactly one thing"),
			Services.WornOrnament(Smoker->Visual), FString(WalkiePath));
	}

	// --- 4101 detaches ---------------------------------------------------------------------------
	{
		Services.Calls.Reset();
		TestTrue(TEXT("4101 is claimed"), Smoker->HandleAnimEvent(Ev(DetachFollowModel)));
		TestEqual(TEXT("...and removes what was worn"), Services.Count(TEXT("DetachOrnamentModel")), 1);
		TestTrue(TEXT("...emptying the slot, which is retail's 0xffffffff"),
			Smoker->AnimFollowModel.IsEmpty());
		TestTrue(TEXT("...and the body wears nothing"),
			Services.WornOrnament(Smoker->Visual).IsEmpty());
	}

	// --- The failure tail. A path the catalogue does not carry -----------------------------------
	{
		// Stand something on the slot first, so "the tail leaves it empty" is a statement about the
		// removal-before-creation order and not merely about a slot that was already empty.
		Smoker->HandleAnimEvent(Ev(AttachFollowModelGendered, CigaretteOption));
		TestEqual(TEXT("the slot is loaded before the failing issue"),
			Smoker->AnimFollowModel, FString(CigaretteMale));

		Services.Calls.Reset();
		TestTrue(TEXT("an unbaked ornament path is still CLAIMED"),
			Smoker->HandleAnimEvent(Ev(AttachFollowModelGendered, WineOption)));
		TestEqual(TEXT("...the seam was asked for the doubled-extension path"),
			Services.Count(FString::Printf(TEXT("AttachOrnamentModel %s"), WinePath)), 1);
		// Retail removes the standing model FIRST and unconditionally, then formats, then creates —
		// so a creation that fails ends at `LAB_1032e4ce` with the handle cleared, NOT with the
		// previous ornament still worn.
		TestTrue(TEXT("...and the failure tail leaves the slot empty, not the old model standing"),
			Smoker->AnimFollowModel.IsEmpty());
		TestTrue(TEXT("...with nothing worn"), Services.WornOrnament(Smoker->Visual).IsEmpty());
	}

	// --- Gender comes off the sheet's own slot 11, which is what `IsMale` reads -------------------
	{
		Smoker->Sheet.SetMale(false);
		Smoker->RecomputeSheet();
		Services.Calls.Reset();
		Smoker->HandleAnimEvent(Ev(AttachFollowModelGendered, CigaretteOption));
		TestEqual(TEXT("a female body takes the female rig"),
			Smoker->AnimFollowModel, FString(CigaretteFemale));
		Smoker->Sheet.SetMale(true);
		Smoker->RecomputeSheet();
	}

	// --- A bodiless character claims the id and hangs nothing -------------------------------------
	{
		Services.Calls.Reset();
		TestTrue(TEXT("a bodiless character still claims 4102"),
			Bodiless->HandleAnimEvent(Ev(AttachFollowModelGendered, CigaretteOption)));
		TestEqual(TEXT("...and never reaches the seam"),
			Services.Count(FString::Printf(TEXT("AttachOrnamentModel %s"), CigaretteMale)), 0);
		TestTrue(TEXT("...its slot stays empty"), Bodiless->AnimFollowModel.IsEmpty());
	}

	// --- 4020: claimed, and mocked ----------------------------------------------------------------
	{
		// Retail's arm returns without reaching `CBaseAnimating::HandleAnimEvent`, so the id must
		// read as claimed even while the discipline callback behind it is unbuilt — otherwise the
		// census would list it as work with no handler, which is a different fact.
		TestTrue(TEXT("4020 is claimed"),
			Smoker->HandleAnimEvent(Ev(ElysiumAnimEvents::DisciplineCallbackHit,
				TEXT("Thaumaturgy_Purge"))));
	}

	// --- An id in none of the arms still falls through to the base handler -------------------------
	{
		// 2010 (`NPC_SWISHSOUND`), which no arm of this chain claims and which the shipped clips
		// never author. It replaced 2050 here when the footstep lane landed: these bodies are
		// `npc_VPedestrian`, so `FElysiumNpc::HandleAnimEvent` now claims 2050-2053 ahead of the
		// combat-character band and the walk footfall is no longer an unclaimed id
		// (`docs/vtmb/animation_events.md` -> "Port status - NPC footstep band").
		TestFalse(TEXT("an unrelated id is not claimed by the character"),
			Smoker->HandleAnimEvent(Ev(2010, TEXT("left"))));
		TestTrue(TEXT("...while 2050 IS claimed, by the NPC leaf's footstep arm"),
			Smoker->HandleAnimEvent(Ev(2050, TEXT("left"))));
	}
	return true;
}

// --- The feed boundaries, on retail's own guards --------------------------------------------------

// A SIBLING of `OrnamentAnimEvents`, never a child: the automation tree treats a name that is also a
// prefix as a group, and the leaf under it stops being discoverable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedBoundaryAnimEventTest,
	"Elysium.Substrate.FeedBoundaryAnimEvents", GElysiumTestFlags)
bool FElysiumFeedBoundaryAnimEventTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeOrnamentDefs());
	World.Activate(0.0);

	FElysiumCombatCharacter* Attacker = Character(World, TEXT("smoker"));
	FElysiumCombatCharacter* Victim = Character(World, TEXT("partner"));
	if (!TestNotNull(TEXT("the attacker exists"), Attacker)
		|| !TestNotNull(TEXT("the victim exists"), Victim))
	{
		return false;
	}
	Victim->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, 3);
	Victim->RecomputeSheet();

	// --- Unpaired: the guard fails, and the record is SWALLOWED -----------------------------------
	{
		// `1032e5b0` and `1032e630` jump to the epilogue, not to `CBaseAnimating::HandleAnimEvent`.
		// So a 4007 on a character in no pair is claimed and does nothing — the id has a handler,
		// which is a different fact from an id nothing owns, and the census must not list it.
		TestTrue(TEXT("4007 with no paired partner is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedBegin)));
		TestFalse(TEXT("...and opens no transaction"), Attacker->FeedState.IsTransacting());
		TestTrue(TEXT("4006 with no paired partner is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedTeardown)));
	}

	// --- The VICTIM half refuses: retail's `+0x153c` role test -------------------------------------
	{
		Victim->FeedState.Peer = Attacker->Handle;
		Victim->FeedState.bVictim = true;
		TestTrue(TEXT("4007 on the victim half is claimed"),
			Victim->HandleAnimEvent(Ev(ElysiumFeed::EventFeedBegin)));
		// Both arms test `+0x153c == 0`, and the 4006 arm additionally excludes `== 1` up front.
		// Role 1 is the victim, so only the attacker half of a pair ever runs FeedBegin.
		TestFalse(TEXT("...and the victim never opens a transaction of its own"),
			Victim->FeedState.IsTransacting());
	}

	// --- The ATTACKER half runs it: 4007 -> FeedBegin (`+0x57c`) -----------------------------------
	{
		Attacker->FeedState.Peer = Victim->Handle;
		Attacker->FeedState.bVictim = false;
		Attacker->FeedState.Phase = EElysiumFeedPhase::Bite;
		TestTrue(TEXT("4007 on the attacker half is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedBegin)));
		if (!TestTrue(TEXT("...and the authored record opens the blood transaction"),
			Attacker->FeedState.IsTransacting()))
		{
			return false;
		}
		TestEqual(TEXT("...seeding the cadence from the victim's pool, as FeedBegin does"),
			Attacker->FeedState.Interval, ElysiumFeed::InitialInterval(3), 1e-4f);

		// Retail's own re-entry guard: `FeedBegin` refuses while `m_hFeedTarget` is set. That is what
		// makes a boundary that reaches the transaction twice — a scheduled raise racing an authored
		// record — inert rather than a second transaction.
		const float Seeded = Attacker->FeedState.NextPulse;
		TestTrue(TEXT("a second 4007 is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedBegin)));
		TestEqual(TEXT("...and re-seeds nothing"), Attacker->FeedState.NextPulse, Seeded, 1e-4f);
	}

	// --- 4006 -> FeedInterrupt (`+0x584`) ----------------------------------------------------------
	{
		Attacker->FeedState.Phase = EElysiumFeedPhase::Release;
		TestTrue(TEXT("4006 on the attacker half is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedTeardown)));
		TestFalse(TEXT("...and closes the blood transaction"), Attacker->FeedState.IsTransacting());
		TestEqual(TEXT("...entering the presentation-only release tail"),
			static_cast<int32>(Attacker->FeedState.Phase),
			static_cast<int32>(EElysiumFeedPhase::ReleaseTail));
	}

	// --- Exactly one firing: the schedule stands down for a bite clip that carries 4007 -----------
	//
	// Two same-model bodies tie on height, and a tie takes the short-victim cell (asserted in
	// `Elysium.Substrate.Feeding`), so this is the bite label `FeedBoundaryArrivesFromAnimEvent` will
	// resolve. The fixture stands the attacker's base channel on it with a timeline carrying the
	// record, which is what the real dispatcher would walk on its next pass.
	const ElysiumFeed::FClipPair Bite = ElysiumFeed::ResolveClipPair(
		EElysiumFeedPhase::Bite, ElysiumFeed::EPartnerHeight::Shorter);
	const FString BiteOwner = TEXT("character_shared_male_feeding");
	auto ArmPairAtEngage = [&]()
	{
		Attacker->FeedState = FElysiumFeedState();
		Victim->FeedState = FElysiumFeedState();
		Attacker->FeedState.Peer = Victim->Handle;
		Attacker->FeedState.bVictim = false;
		Attacker->FeedState.bContinuation = true;
		Attacker->FeedState.Phase = EElysiumFeedPhase::Engage;
		Attacker->FeedState.PhaseDeadline = 0.0f;
		Victim->FeedState.Peer = Attacker->Handle;
		Victim->FeedState.bVictim = true;
		Victim->FeedState.Phase = EElysiumFeedPhase::Engage;
		Victim->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, 3);
		Victim->RecomputeSheet();
	};
	auto StandOnBiteClip = [&](bool bOn)
	{
		Services.ClipOwnerByLabel.Add(Bite.Attacker.ToLower(), BiteOwner);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.FindOrAdd(
			FElysiumRecordingServices::EventTimelineKey(BiteOwner, Bite.Attacker));
		Timeline.Reset();
		Timeline.Add(Ev(ElysiumFeed::EventFeedBegin));   // cycle 0, as the shipped bite clips author
		Services.bBodyClipPhaseSet = bOn;
		Services.BodyClipPhase = FElysiumClipPhase();
		Services.BodyClipPhase.Channel = EElysiumAnimChannel::Base;
		Services.BodyClipPhase.OwnerStem = BiteOwner;
		Services.BodyClipPhase.Label = Bite.Attacker;
		Services.BodyClipPhase.Cycle = 0.1f;
		Services.BodyClipPhase.Length = 1.0f;
		Services.BodyClipPhase.PlayId = 1;
	};
	{
		ArmPairAtEngage();
		StandOnBiteClip(true);
		Attacker->TickFeed(1.0);
		TestEqual(TEXT("the engage boundary moved the pair to the bite"),
			static_cast<int32>(Attacker->FeedState.Phase), static_cast<int32>(EElysiumFeedPhase::Bite));
		TestFalse(TEXT("...and the schedule did NOT raise 4007 itself, because the clip carries it"),
			Attacker->FeedState.IsTransacting());
		// The dispatcher's pass: the authored record is what opens the transaction.
		TestTrue(TEXT("the authored 4007 is claimed"),
			Attacker->HandleAnimEvent(Ev(ElysiumFeed::EventFeedBegin)));
		TestTrue(TEXT("...and it is the one firing that opened the transaction"),
			Attacker->FeedState.IsTransacting());
		const FElysiumEntityHandle Opened = Attacker->FeedState.Target;
		// The bite deadline then passes with the transaction open: the backstop is inert. (The
		// pulse that lands on that tick moves `NextPulse`/`Interval` on its own, so the evidence
		// of "no second FeedBegin" is the target handle and the open transaction, not the cadence.)
		Attacker->TickFeed(static_cast<double>(Attacker->FeedState.PhaseDeadline) + 0.01);
		TestEqual(TEXT("the bite boundary moved on to the loop"),
			static_cast<int32>(Attacker->FeedState.Phase), static_cast<int32>(EElysiumFeedPhase::Loop));
		TestTrue(TEXT("...with the same transaction still open"),
			Attacker->FeedState.IsTransacting() && Attacker->FeedState.Target == Opened);
		Attacker->FeedInterrupt();
	}

	// --- The backstop: a stand-down whose record never lands is raised at the bite's end ---------
	{
		ArmPairAtEngage();
		StandOnBiteClip(true);
		Attacker->TickFeed(1.0);
		TestFalse(TEXT("the schedule stood down for the clip"), Attacker->FeedState.IsTransacting());
		// The channel is taken away before the dispatcher's next pass ever reaches cycle 0 — a scene
		// posing the body, a displaced clip. Nothing will fire the record now.
		StandOnBiteClip(false);
		Attacker->TickFeed(static_cast<double>(Attacker->FeedState.PhaseDeadline) + 0.01);
		TestTrue(TEXT("the bite -> loop arm raised 4007 itself rather than entering the loop unopened"),
			Attacker->FeedState.IsTransacting());
		TestEqual(TEXT("...and the pair is in the loop"),
			static_cast<int32>(Attacker->FeedState.Phase), static_cast<int32>(EElysiumFeedPhase::Loop));
		Attacker->FeedInterrupt();
	}
	return true;
}

bool }

#endif   // WITH_DEV_AUTOMATION_TESTS
