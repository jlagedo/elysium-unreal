// Content-free Substrate automation: the player's melee COMBO CHAIN, driven end to end (LIFE5).
//
// This suite exists because the chain is not one rule, it is a path: the controller's button field,
// the press edge derived from it, `ItemPostFrame` staging the first swing, the busy predicate
// routing every later press to `ItemBusyFrame`, and that frame taking a hand-off only when the
// playing clip's own window is open. Asserting the predicates in isolation proves none of that
// path, so nothing here calls them directly — every case presses a button and reads what came out.
//
// The facts asserted are `docs/vtmb/combat-and-damage.md` § "The combo chain is a press-edge
// hand-off inside the busy frame":
//  * a press edge, never a held bit — a key held down chains nothing;
//  * the successor is the playing clip's authored `chain` LABEL, so the live direction cannot
//    change it once a chain is running;
//  * the window is closed at both ends, and a press outside it is IGNORED rather than queued;
//  * the recovery deadline is NOT pushed again by a hand-off — the whole chain is paid for by the
//    first swing;
//  * a clip naming no successor terminates, and a further press starts a fresh swing instead.
//
// The clip values are the shipped katana's own — `katana_combo_D1/D2/D3`, the neutral entry chain,
// and `katana_running_attack`, whose `w_hold` (0.900) sits BELOW its `w_close` (1.000). Transcribed
// rather than loaded: the suite opens no export and needs no baked body.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Misc/ScopeExit.h"

#include "ElysiumClipMovement.h"
#include "ElysiumComboChain.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumUserCmd.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumComboChainTests
{
static constexpr EAutomationTestFlags GElysiumComboTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;

	constexpr uint64 GAtk   = static_cast<uint64>(EElysiumButton::Attack);
	constexpr uint64 GFwd   = static_cast<uint64>(EElysiumButton::Forward);
	constexpr uint64 GBack  = static_cast<uint64>(EElysiumButton::Back);
	constexpr uint64 GMoveR = static_cast<uint64>(EElysiumButton::MoveRight);

	const TCHAR* const GKatana = TEXT("melee_katana");
	const TCHAR* const GBank   = TEXT("katana");
	const TCHAR* const GD1     = TEXT("katana_combo_D1");
	const TCHAR* const GD2     = TEXT("katana_combo_D2");
	const TCHAR* const GD3     = TEXT("katana_combo_D3");

	// The katana's shipped neutral-entry window, on all three links: open 0.500, close 0.900,
	// hold 0.910. `D3` states no block at all, which is how the chain ends.
	FElysiumComboChain Link(const TCHAR* Chain)
	{
		FElysiumComboChain Record;
		Record.bStated = true;
		Record.Mask = 0;   // the neutral entry's own STATED mask
		Record.Chain = Chain;
		Record.WindowOpen = 0.500f;
		Record.WindowClose = 0.900f;
		Record.HoldCycle = 0.910f;
		return Record;
	}

	FElysiumItemTable MakeComboTable()
	{
		FElysiumItemTable Table;

		FElysiumWeaponMode Primary;
		Primary.Tag = TEXT("Primary");
		Primary.TypeName = TEXT("Attack");
		Primary.Type = EElysiumWeaponModeType::Attack;
		Primary.Dmg = TEXT("4 Lethal Close_Combat_Melee DMG_SLASH");
		Primary.BaseLethality = 5;
		Primary.SkillRequirement = 1;
		Primary.AttackRate = 0.5f;
		Primary.AmmoCost = 0;
		Primary.AmmoFired = 1;

		FElysiumItemDef Katana;
		Katana.Classname = GKatana;
		Katana.PrintName = GKatana;
		Katana.Type = EElysiumItemType::WeaponMelee;
		Katana.bWieldable = true;
		Katana.Modes.Add(MoveTemp(Primary));
		Table.Items.Add(MoveTemp(Katana));

		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeComboDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__combo_chain_test__");
		return Defs;
	}

	// An armed, bodied, grounded player, with the katana's three links declared as the embodiment's
	// own combo columns.
	//
	// `Visual` is a bare transient component: the busy path asks the embodiment for a clip phase and
	// only needs the body to be non-null to do it, so the fixture supplies a body without a mesh, a
	// skeleton or a baked clip. That is what keeps this suite content-free while still running the
	// real `MeleeBusyPress`.
	struct FComboFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		USkeletalMeshComponent* Body = nullptr;
		double Now = 0.0;

		bool Stand(FAutomationTestBase& Test)
		{
			ElysiumRng::SeedAll(0x434D4231);
			Services.bPlayerOnGround = true;

			// The chain's three links, and the bank each label resolves against. An unseeded label
			// answers EMPTY owner, which is retail's dangling link — so every label a case expects to
			// resolve is declared here on purpose.
			Services.ComboByClip.Add(FString(GD1).ToLower(), Link(GD2));
			Services.ComboByClip.Add(FString(GD2).ToLower(), Link(GD3));
			// `D3` names no successor: the record is absent entirely, which is the terminal shape.
			Services.ClipOwnerByLabel.Add(FString(GD1).ToLower(), GBank);
			Services.ClipOwnerByLabel.Add(FString(GD2).ToLower(), GBank);
			Services.ClipOwnerByLabel.Add(FString(GD3).ToLower(), GBank);

			// The first swing's clip comes back through the ordinary activity resolve.
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = GD1;
			Services.ResolvedNpcActivityOwner = GBank;

			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeComboDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			if (!Test.TestNotNull(TEXT("the player exists"), Player))
			{
				return false;
			}
			Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
			Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
			Player->RecomputeSheet();
			Player->Origin = FVector::ZeroVector;
			Player->Angles = FVector::ZeroVector;
			Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));

			Body = NewObject<USkeletalMeshComponent>();
			Body->AddToRoot();
			Player->Visual = Body;

			const FElysiumEntityHandle Handle =
				Player->Inventory.GiveNamedItem(*Player, GKatana);
			FElysiumEntity* Ent = World->Resolve(Handle);
			FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
			if (!Test.TestNotNull(TEXT("the player is armed"), Item ? Item->AsWeapon() : nullptr))
			{
				return false;
			}
			Services.Calls.Reset();
			return true;
		}

		void Release()
		{
			if (Body != nullptr)
			{
				Player->Visual = nullptr;
				Body->RemoveFromRoot();
				Body = nullptr;
			}
		}

		FElysiumWeapon* Active() const
		{
			FElysiumItem* Item = Player ? Player->Inventory.Active(*Player) : nullptr;
			return Item ? Item->AsWeapon() : nullptr;
		}

		// Where the playing clip is RIGHT NOW, as the embodiment would report it. The chain reads the
		// cycle off this, so a case advances the clip by moving it rather than by waiting.
		void PlaceClip(const TCHAR* Label, float Cycle)
		{
			FElysiumClipPhase Phase;
			Phase.OwnerStem = GBank;
			Phase.Label = Label;
			Phase.Cycle = Cycle;
			Phase.Length = 1.0f;
			Phase.bLooping = false;
			Phase.PlayId = 1;
			Services.bBodyClipPhaseSet = true;
			Services.BodyClipPhase = Phase;
		}

		// One whole frame in the map actor's own order.
		void Frame(double At, uint64 Buttons)
		{
			Now = At;
			World->SetPlayerButtons(Buttons);
			World->RunPlayerThink(Now);
			World->Tick(Now);
			World->UpdatePlayerWeaponFrame();
		}

		// A press: one frame with the button up so an edge can form, then one with it down.
		void Press(double At, uint64 Extra = 0)
		{
			Frame(At, Extra);
			Frame(At, GAtk | Extra);
		}
	};

	FString ClipOf(const FElysiumWeapon* Weapon)
	{
		return Weapon != nullptr ? Weapon->Swing.ClipLabel : FString();
	}
}   // namespace

// =====================================================================================
// The whole chain, from the button field to the committed successor
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumComboChainSequenceTest,
	"Elysium.Substrate.ComboChain.Sequence", GElysiumComboTestFlags)
bool FElysiumComboChainSequenceTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeComboTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FComboFixture F;
	if (!F.Stand(*this))
	{
		return false;
	}
	ON_SCOPE_EXIT { F.Release(); };
	FElysiumWeapon* Katana = F.Active();
	if (!TestNotNull(TEXT("the katana is the active weapon"), Katana))
	{
		return false;
	}

	// --- Hit 1: an ordinary press stages the entry swing -------------------------------------------
	F.Press(0.0);
	TestEqual(TEXT("the press staged a swing"), Katana->AcceptedSwingCount(), 1);
	TestEqual(TEXT("...on the neutral entry clip"), ClipOf(Katana), FString(GD1));
	const double FirstDeadline = Katana->Swing.RecoveryDeadline;
	const int32 FirstSerial = Katana->Swing.Serial;
	TestTrue(TEXT("...and set a recovery deadline"), FirstDeadline > 0.0);

	// --- A press BEFORE the window opens is ignored, not queued ------------------------------------
	// This is the whole reason mashing does not chain faster: the early press is spent in the busy
	// frame and nothing remembers it.
	F.PlaceClip(GD1, 0.30f);
	F.Press(0.10);
	TestEqual(TEXT("a press below the authored open does not hand off"), ClipOf(Katana),
		FString(GD1));
	TestEqual(TEXT("...and restages nothing, so the swing record still stands"),
		Katana->Swing.Serial, FirstSerial);

	// --- A press exactly ON the open commits, because the window is closed at both ends -------------
	F.PlaceClip(GD1, 0.500f);
	F.Press(0.20);
	TestEqual(TEXT("a press exactly on the open hands off to the authored successor"),
		ClipOf(Katana), FString(GD2));

	// --- Hit 2 -> hit 3, and the deadline is NOT pushed again ----------------------------------------
	TestEqual(TEXT("the hand-off did not push the recovery deadline"),
		Katana->Swing.RecoveryDeadline, FirstDeadline);
	// A link IS its own swing record — the opposed roll and the contact walk are per swing — so the
	// serial advances. The deadline above is what makes it a CHAIN rather than a fresh attack.
	TestTrue(TEXT("...while the link restages a swing record of its own"),
		Katana->Swing.Serial > FirstSerial);

	F.PlaceClip(GD2, 0.700f);
	F.Press(0.30);
	TestEqual(TEXT("the second hand-off commits the third link"), ClipOf(Katana), FString(GD3));
	TestEqual(TEXT("...still on the first swing's deadline"), Katana->Swing.RecoveryDeadline,
		FirstDeadline);

	// --- The terminal link ends the chain -------------------------------------------------------------
	// `D3` states no combo block, so the press is ignored: not queued for the clip's end, and not a
	// restart. A fourth hit has to be a fresh swing.
	F.PlaceClip(GD3, 0.700f);
	const int32 TerminalSerial = Katana->Swing.Serial;
	F.Press(0.40);
	TestEqual(TEXT("a press on the terminal link hands off to nothing"), ClipOf(Katana),
		FString(GD3));
	TestEqual(TEXT("...and restages nothing, because the press is ignored rather than queued"),
		Katana->Swing.Serial, TerminalSerial);

	// --- Past the recovery, a press is a FRESH swing rather than a chain -------------------------------
	// No clip standing: the busy predicate has no cycle to hold on, so the frame is the
	// ordinary one again.
	F.Services.bBodyClipPhaseSet = false;
	F.Press(FirstDeadline + 1.0);
	TestEqual(TEXT("a press past the recovery is a fresh swing, back at the entry clip"),
		ClipOf(Katana), FString(GD1));
	TestTrue(TEXT("...on a deadline of its own"),
		Katana->Swing.RecoveryDeadline > FirstDeadline);

	return true;
}

// =====================================================================================
// A held button chains nothing, and the live direction cannot redirect a chain
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumComboChainPressEdgeTest,
	"Elysium.Substrate.ComboChain.PressEdge", GElysiumComboTestFlags)
bool FElysiumComboChainPressEdgeTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeComboTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FComboFixture F;
	if (!F.Stand(*this))
	{
		return false;
	}
	ON_SCOPE_EXIT { F.Release(); };
	FElysiumWeapon* Katana = F.Active();
	if (!TestNotNull(TEXT("the katana is the active weapon"), Katana))
	{
		return false;
	}

	F.Press(0.0);
	TestEqual(TEXT("the entry swing staged"), ClipOf(Katana), FString(GD1));

	// --- Holding the button through an OPEN window chains nothing ------------------------------------
	// Retail's busy frame reads `m_afButtonPressed`, so a key that never comes back up produces no
	// further edge however many frames it spans — which is also why an NPC, with no button field at
	// all, never chains.
	F.PlaceClip(GD1, 0.600f);
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		F.Frame(0.10 + 0.01 * Tick, GAtk);
	}
	TestEqual(TEXT("a held attack button never hands off, however long the window stands open"),
		ClipOf(Katana), FString(GD1));

	// --- Releasing and pressing again inside the same window DOES hand off -----------------------------
	F.Press(0.30);
	TestEqual(TEXT("the release-and-press edge hands off"), ClipOf(Katana), FString(GD2));

	// --- The live direction cannot change which successor a chain takes ---------------------------------
	// The successor is the playing clip's authored LABEL. Holding a direction through the hand-off
	// changes nothing, which is what makes a combo's later hits fixed once it has started.
	F.PlaceClip(GD2, 0.600f);
	F.Press(0.40, GBack | GMoveR);
	TestEqual(TEXT("a hand-off under a held direction still commits the authored successor"),
		ClipOf(Katana), FString(GD3));

	return true;
}

// =====================================================================================
// What the chain travels: the authored displacement of the links it actually took
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumComboChainDisplacementTest,
	"Elysium.Substrate.ComboChain.Displacement", GElysiumComboTestFlags)
bool FElysiumComboChainDisplacementTest::RunTest(const FString&)
{
	constexpr float Tolerance = 0.01f;

	// Three links, each with its own authored lunge, in the shape the exporter writes: one record per
	// block, `pos` cumulative and `v0`/`v1` the block's own eased endpoints.
	auto MakePath = [](float FirstV1, float SecondV1) -> FElysiumClipMovementPath
	{
		FElysiumClipMovementPath Path;
		FElysiumMovementRecord A;
		A.EndFrame = 8;
		A.Flags = 4160;
		A.V0Cm = 0.0f;
		A.V1Cm = FirstV1;
		A.YawDegrees = 0.0f;
		A.Direction = FVector(1.0, 0.0, 0.0);
		A.PositionCm = FVector(0.5f * FirstV1, 0.0, 0.0);
		Path.Records.Add(A);

		FElysiumMovementRecord B;
		B.EndFrame = 16;
		B.Flags = 4160;
		B.V0Cm = FirstV1;
		B.V1Cm = SecondV1;
		B.YawDegrees = 0.0f;
		B.Direction = FVector(1.0, 0.0, 0.0);
		B.PositionCm = FVector(0.5f * FirstV1 + 0.5f * (FirstV1 + SecondV1), 0.0, 0.0);
		Path.Records.Add(B);
		return Path;
	};

	const FElysiumClipMovementPath D1 = MakePath(40.0f, 60.0f);
	const FElysiumClipMovementPath D2 = MakePath(30.0f, 50.0f);
	const FElysiumClipMovementPath D3 = MakePath(80.0f, 120.0f);
	constexpr int32 Frames = 17;

	// --- Each link's own whole-clip travel is its last record's cumulative position ------------------
	FVector Whole = FVector::ZeroVector;
	TestTrue(TEXT("the first link samples"),
		ElysiumClipMovement::SampleDelta(D1, Frames, 0.0f, 1.0f, Whole));
	TestEqual(TEXT("...and travels its authored lunge"), static_cast<float>(Whole.X),
		static_cast<float>(D1.Records.Last().PositionCm.X), Tolerance);

	// --- The chain travels the PART of each link it actually played ------------------------------------
	// A hand-off cuts the link off at the cycle the press landed on, so the combo's displacement is
	// three partial windows, not three whole clips. This is the arithmetic behind a three-hit combo
	// carrying the body further than three separate swings would.
	constexpr float HandOffCycle = 0.500f;
	FVector First = FVector::ZeroVector;
	FVector Second = FVector::ZeroVector;
	FVector Third = FVector::ZeroVector;
	TestTrue(TEXT("the first link's played part samples"),
		ElysiumClipMovement::SampleDelta(D1, Frames, 0.0f, HandOffCycle, First));
	TestTrue(TEXT("the second link's played part samples"),
		ElysiumClipMovement::SampleDelta(D2, Frames, 0.0f, 0.700f, Second));
	TestTrue(TEXT("the third link runs to its end"),
		ElysiumClipMovement::SampleDelta(D3, Frames, 0.0f, 1.0f, Third));

	const float Travelled =
		static_cast<float>(First.X) + static_cast<float>(Second.X) + static_cast<float>(Third.X);
	TestTrue(TEXT("a cut-short link travels less than its whole clip"),
		First.X < D1.Records.Last().PositionCm.X);
	TestTrue(TEXT("the chain carries the body further than its longest single link"),
		Travelled > static_cast<float>(D3.Records.Last().PositionCm.X));

	// --- Consecutive windows sum, so a chain's travel does not depend on how it is sliced --------------
	FVector Head = FVector::ZeroVector;
	FVector Tail = FVector::ZeroVector;
	ElysiumClipMovement::SampleDelta(D1, Frames, 0.0f, 0.25f, Head);
	ElysiumClipMovement::SampleDelta(D1, Frames, 0.25f, HandOffCycle, Tail);
	TestEqual(TEXT("two windows sum to the one they partition"),
		static_cast<float>(Head.X + Tail.X), static_cast<float>(First.X), Tolerance);

	// --- Nothing sideways, on a straight authored lunge --------------------------------------------------
	TestEqual(TEXT("a forward lunge carries no lateral travel"), static_cast<float>(First.Y), 0.0f,
		Tolerance);

	return true;
}

}   // namespace ElysiumComboChainTests

#endif   // WITH_DEV_AUTOMATION_TESTS
