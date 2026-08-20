// Content-free Substrate automation: the GROUNDED knockback family (LIFE5) — who is eligible for
// one, the direction bands retail cuts, the yaw it snaps the victim to, the cell that plays, and
// what the whole path spends off the Reaction stream (nothing).
//
// The facts asserted here are the recovered retail rules:
//
//  - **Admission is the margin band alone.** `knockback_chance` is parsed by retail and read by no
//    gameplay path, so it is a dead field and is not parsed here — there is nothing to assert about
//    it and no case below states a chance. `rules.txt`'s `Knockbacks { KnockbackPreventTime }` is
//    the PLAYER view kick's refractory, not a victim re-knockback window, so there is no window
//    here either.
//  - **Eligibility** is alive plus the NPC template's `General/Disallow_Knockbacks`, with two
//    omitted terms named on `ElysiumReactions::IsKnockbackAllowed`.
//  - **Direction** is the four asymmetric bands over `AngleMod(awayYaw - victimYaw)`, and
//    **the yaw snap** turns the victim so the authored cell's model-space direction reads true.
//  - **No randomness at all.** With one candidate per bucket there is nothing to draw for. The
//    rules stand the Reaction stream completely still on refusal AND on success; the producer
//    spends exactly one draw and it is the SHARED reaction path's weighted-variant pick, which the
//    flinch and the block family take on the same terms. Both counts are asserted.
//
// The one STAND-IN is the cell: retail picks it out of an authored per-attack activity table whose
// on-disk location is unrecovered, and until that is found the `NORMAL`/`HIGH` cell of the
// classified direction stands in. The `SMALL` family and the two `LOW_BACK` cells are asserted as
// vocabulary and asserted NOT to be selected, which is what makes finding the table a visible change.
//
// **What is NOT reachable here, and why.** The knockback branch is taken on the hit/knockback
// classification, and the classifier needs `rules.txt`'s `Melee_Reactions` block: a content-free
// world has no rulebook to supply it, and the tables load behind private lazy slots on a
// GameInstance subsystem, so no seam injects a margin table into a live contact. This is the same
// limit `ElysiumBlockReactionTests.cpp` records, and it has the same two consequences —
//
//  1. `.Producer` drives the composition the contact performs by making the same calls the contact
//     makes, rather than through `MeleeContact`.
//  2. `.Producer` DOES drive a real melee contact end to end, to assert the fail-safe: an
//     unclassified record names no band, so it knocks nobody back.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumAnimGraph.h"

namespace ElysiumKnockbackTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;
	using ESize = ElysiumReactions::EKnockbackSize;
	using EHeight = ElysiumReactions::EKnockbackHeight;
	using EDir = ElysiumReactions::EKnockbackDirection;

	// Classnames are suite-local: `ElysiumItems::Install` registers a class once per process and
	// never unregisters, so a name shared with another suite resolves to whichever ran first.
	const TCHAR* const GHammer = TEXT("item_w_knock_hammer");

	const TCHAR* const GSwingLabel = TEXT("swing_long");
	const TCHAR* const GSwingBank = TEXT("cast_bank");
	// The bone the swing's authored contact segment is stated in, and which the fixture places.
	const TCHAR* const GSwingBone = TEXT("Bip01 R Hand");

	// Where the clock stands when the contact walk runs. It is not a commit deadline — melee
	// schedules nothing — but the base-channel holds a contact takes are measured against it.
	constexpr double GContactTick = 1.0;

	// A victim origin, an attacker origin and the victim's yaw — the three inputs every direction
	// case is stated in. The victim stands on the origin facing Unreal +X unless a case turns it.
	const FVector GVictim = FVector::ZeroVector;
	const FVector GAhead = FVector(100.0f, 0.0f, 0.0f);
	const FVector GBehind = FVector(-100.0f, 0.0f, 0.0f);
	const FVector GOnTheRight = FVector(0.0f, 100.0f, 0.0f);
	const FVector GOnTheLeft = FVector(0.0f, -100.0f, 0.0f);
	// Off a bucket centre, so the yaw snap has an actual turn to make: the away direction is the
	// third quadrant's diagonal and the victim has to end up with its BACK along it.
	const FVector GAheadRight = FVector(100.0f, 100.0f, 0.0f);

	FElysiumWeaponMode MakeMode(const TCHAR* Dmg, int32 BaseLethality)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = 0.5f;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeKnockbackTable()
	{
		FElysiumItemTable Table;

		FElysiumItemDef Hammer;
		Hammer.Classname = GHammer;
		Hammer.PrintName = GHammer;
		Hammer.Type = EElysiumItemType::WeaponMelee;
		Hammer.bWieldable = true;
		Hammer.Modes.Add(MakeMode(TEXT("3 Bashing Close_Combat_Melee DMG_CLUB"), /*Lethality*/ 10));
		Table.Items.Add(MoveTemp(Hammer));

		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeKnockbackTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__knockback_test__");

		FElysiumEntityDef Attacker;
		Attacker.Classname = TEXT("npc_VHumanCombatant");
		Attacker.TargetName = TEXT("attacker");
		Attacker.Origin = GAhead;
		Attacker.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Attacker));

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VHumanCombatant");
		Victim.TargetName = TEXT("victim");
		Victim.Origin = GVictim;
		Victim.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Victim));
		return Defs;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	int32 DamageTaken(const FElysiumCombatCharacter& Char)
	{
		return Char.Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	FElysiumWeapon* GiveWeapon(FElysiumCombatCharacter& Char, const TCHAR* Classname)
	{
		const FElysiumEntityHandle Handle = Char.Inventory.GiveNamedItem(Char, Classname);
		FElysiumEntity* Ent = Char.World ? Char.World->Resolve(Handle) : nullptr;
		FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
		return Item ? Item->AsWeapon() : nullptr;
	}

	int32 CountCalls(const FElysiumRecordingServices& Services, const TCHAR* Prefix,
		const TCHAR* Contains = nullptr)
	{
		int32 Count = 0;
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(Prefix) && (Contains == nullptr || Call.Contains(Contains)))
			{
				++Count;
			}
		}
		return Count;
	}

	bool Saw(const FElysiumRecordingServices& Services, const TCHAR* Prefix, const TCHAR* Contains)
	{
		return CountCalls(Services, Prefix, Contains) > 0;
	}

	FElysiumDmg ResolvedDmg(int32 Amount)
	{
		FElysiumDmg Dmg;
		Dmg.Family = EElysiumDmgFamily::Bashing;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = Amount;
		Dmg.DmgMask = ElysiumDamage::DmgClub;
		Dmg.RolledSuccesses = Amount;
		Dmg.Remainder = Amount;
		Dmg.AppliedDamage = Amount;
		Dmg.bResolved = true;
		return Dmg;
	}

	// How many draws a call spent off a stream, measured rather than asserted about: a reference
	// stream seeded identically is advanced by `Draws` variant picks and the two seeds compared. A
	// seed comparison alone proves only "some" or "none"; this proves the count.
	bool SpentDraws(int32 Seed, const FRandomStream& After, int32 Draws)
	{
		FRandomStream Reference(Seed);
		for (int32 i = 0; i < Draws; ++i)
		{
			Reference.RandHelper(MAX_int32);
		}
		return Reference.GetCurrentSeed() == After.GetCurrentSeed();
	}

	// The one direction rule the whole slice turns on, stated once for the cases below: the away
	// vector's own Unreal world yaw.
	float AwayYawOf(const FVector& AttackerOrigin, const FVector& VictimOrigin)
	{
		float AwayYaw = 0.0f;
		float RelativeYaw = 0.0f;
		ElysiumReactions::KnockbackRelativeYaw(
			ElysiumReactions::KnockbackAwayFrom(AttackerOrigin, VictimOrigin), 0.0f, AwayYaw,
			RelativeYaw);
		return AwayYaw;
	}

	// The whole producer fixture: a cast attacker and a cast victim, both with bodies to react with.
	struct FKnockbackFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumCombatCharacter* Attacker = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;

		// `VictimUnrealYawDegrees` is the victim's UNREAL yaw; the entity carries its Source negation.
		bool Stand(FAutomationTestBase& Test, float VictimUnrealYawDegrees = 0.0f)
		{
			// The stream is seeded so a case can measure it. Nothing in the knockback path draws off
			// it, which is exactly what the cases below assert.
			ElysiumRng::SeedAll(0x4B4E4B31);
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = GSwingLabel;
			Services.ResolvedNpcActivityClip = GSwingLabel;
			Services.ResolvedNpcActivityOwner = GSwingBank;
			Services.bNpcOneShotsPlay = true;

			// The swing's contact is the swept walk over the clip's own authored records, so the
			// fixture authors one: a phase standing on the swing clip, the bone its segment is stated
			// in, and a window over the middle of the cycle.
			Services.bBodyClipPhaseSet = true;
			Services.BodyClipPhase = FElysiumClipPhase();
			Services.BodyClipPhase.OwnerStem = GSwingBank;
			Services.BodyClipPhase.Label = GSwingLabel;
			Services.BodyClipPhase.Length = 1.0f;
			Services.BodyClipPhase.PlayId = 1;
			Services.BoneFrames.Add(FString(GSwingBone).ToLower(), FTransform::Identity);
			{
				FElysiumSwingRecord Record;
				Record.Start = 0.30f;
				Record.End = 0.70f;
				Record.Bone = GSwingBone;
				Record.BCm = FVector(30.f, 0.f, 0.f);
				Services.SwingsByClip.Add(FString(GSwingLabel).ToLower(), { Record });
			}

			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeKnockbackTestDefs());
			World->Activate(0.0);
			World->Tick(0.0);

			Attacker = FindCharacter(*World, TEXT("attacker"));
			Victim = FindCharacter(*World, TEXT("victim"));
			if (!Test.TestNotNull(TEXT("the attacker exists"), Attacker)
				|| !Test.TestNotNull(TEXT("the victim exists"), Victim))
			{
				return false;
			}
			if (!Test.TestNotNull(TEXT("the victim carries a body to react with"), Victim->Visual))
			{
				return false;
			}
			SeedHealth(*Attacker, 100);
			SeedHealth(*Victim, 100);
			Attacker->Origin = GAhead;
			Attacker->Angles = FVector(0.0f, 180.0f, 0.0f);
			Victim->Origin = GVictim;
			Victim->Angles = FVector(0.0f, -VictimUnrealYawDegrees, 0.0f);
			// Whom the sweep reaches. Geometry is the seam's answer; eligibility, the opposed record
			// and every reaction behind it stay in the substrate.
			Services.SwingContacts = { Victim->Handle };
			Services.Calls.Reset();
			return true;
		}

		float VictimUnrealYaw() const
		{
			return ElysiumSkeletalBasis::FromSourceAngles(Victim->Angles).Yaw;
		}

		// The composition `FElysiumWeapon::KnockbackContact` performs, made from the same calls in the
		// same order: eligibility, classify, SNAP THE FACING, play, hold the base channel. Returns
		// whether a knockback played.
		bool Knockback(double NowSeconds)
		{
			if (!ElysiumReactions::IsKnockbackAllowed(!Victim->IsInert() && !Victim->HasReportedDeath(),
				Victim->DisallowsKnockbacks()))
			{
				return false;
			}
			ElysiumReactions::FElysiumKnockback Selected;
			ElysiumReactions::BuildKnockback(
				ElysiumReactions::KnockbackAwayFrom(Attacker->Origin, Victim->Origin),
				VictimUnrealYaw(), Selected);
			if (Selected.Activity() == nullptr)
			{
				return false;
			}

			FVector Facing = Victim->Angles;
			Facing.Y = -Selected.SnapYawDegrees;
			Victim->SetRuntimeAngles(Facing);

			FElysiumReactionPlayRequest Request;
			Request.Activity = Selected.Activity();
			Request.bAllowFallbackLadder = true;
			float Held = 0.0f;
			if (!Victim->PlayReactionActivity(Request, &Held))
			{
				return false;
			}
			Victim->HoldBaseForMeleeReaction(NowSeconds + static_cast<double>(Held));
			return true;
		}

		// One accepted melee swing by the attacker, carried through to its contact.
		void Swing(const TCHAR* Weapon)
		{
			// Melee ignores an explicit victim handle and acquires its own opponent, which is the
			// victim standing 100 cm dead ahead of the attacker.
			if (FElysiumWeapon* Held = GiveWeapon(*Attacker, Weapon))
			{
				Held->AttackIntent(FElysiumWeapon::EIntent::Primary);
			}
			// The clock moves FIRST, so the holds the contact takes are measured against a `now`
			// inside the swing. Then two walked frames: the swing's first live frame stages the
			// opposed roll and the notice, and the second carries the cycle into the authored window.
			World->Tick(GContactTick);
			Services.BodyClipPhase.Cycle = 0.0f;
			World->AdvanceMeleeSwings(0.02f);
			Services.BodyClipPhase.Cycle = 0.50f;
			World->AdvanceMeleeSwings(0.02f);
		}
	};
}

// =====================================================================================
// The pure rules
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumKnockbackRuleTest,
	"Elysium.Substrate.Knockback.Rule", GElysiumTestFlags)
bool FElysiumKnockbackRuleTest::RunTest(const FString&)
{
	using namespace ElysiumReactions;

	// --- The ten cells, spelled once and asserted whole -----------------------------------------
	{
		TestEqual(TEXT("small/high/forward"),
			FString(KnockbackActivity(ESize::Small, EHeight::High, EDir::Forward)),
			FString(TEXT("ACT_KNOCKBACK_SMALL_HIGH_FORWARD")));
		TestEqual(TEXT("small/high/back"),
			FString(KnockbackActivity(ESize::Small, EHeight::High, EDir::Back)),
			FString(TEXT("ACT_KNOCKBACK_SMALL_HIGH_BACK")));
		TestEqual(TEXT("small/high/left"),
			FString(KnockbackActivity(ESize::Small, EHeight::High, EDir::Left)),
			FString(TEXT("ACT_KNOCKBACK_SMALL_HIGH_LEFT")));
		TestEqual(TEXT("small/high/right"),
			FString(KnockbackActivity(ESize::Small, EHeight::High, EDir::Right)),
			FString(TEXT("ACT_KNOCKBACK_SMALL_HIGH_RIGHT")));
		TestEqual(TEXT("normal/high/forward"),
			FString(KnockbackActivity(ESize::Normal, EHeight::High, EDir::Forward)),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD")));
		TestEqual(TEXT("normal/high/back"),
			FString(KnockbackActivity(ESize::Normal, EHeight::High, EDir::Back)),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK")));
		TestEqual(TEXT("normal/high/left"),
			FString(KnockbackActivity(ESize::Normal, EHeight::High, EDir::Left)),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_LEFT")));
		TestEqual(TEXT("normal/high/right"),
			FString(KnockbackActivity(ESize::Normal, EHeight::High, EDir::Right)),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_RIGHT")));
		TestEqual(TEXT("small/low/back"),
			FString(KnockbackActivity(ESize::Small, EHeight::Low, EDir::Back)),
			FString(TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK")));
		TestEqual(TEXT("normal/low/back"),
			FString(KnockbackActivity(ESize::Normal, EHeight::Low, EDir::Back)),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_LOW_BACK")));

		// The corpus authors LOW on BACK alone, so the other three name nothing. This is what stops a
		// future selector asking 155 bodies for a clip none of them carries.
		TestNull(TEXT("there is no low/forward cell"),
			KnockbackActivity(ESize::Small, EHeight::Low, EDir::Forward));
		TestNull(TEXT("there is no low/left cell"),
			KnockbackActivity(ESize::Normal, EHeight::Low, EDir::Left));
		TestNull(TEXT("there is no low/right cell"),
			KnockbackActivity(ESize::Small, EHeight::Low, EDir::Right));
	}

	// --- The buckets carry retail's own numbers ---------------------------------------------------
	//
	// The activity table and the yaw-offset table are both indexed by them, so a renumbering that
	// looked cosmetic would silently re-aim every knockback.
	{
		TestEqual(TEXT("bucket 0 is BEHIND, and its cell is BACK"),
			static_cast<int32>(EDir::Back), 0);
		TestEqual(TEXT("bucket 1 is LEFT"), static_cast<int32>(EDir::Left), 1);
		TestEqual(TEXT("bucket 2 is FRONT, and its cell is FORWARD"),
			static_cast<int32>(EDir::Forward), 2);
		TestEqual(TEXT("bucket 3 is RIGHT"), static_cast<int32>(EDir::Right), 3);
	}

	// --- The state projection: every one of the ten is named and lands somewhere ------------------
	{
		static const EElysiumAnimActivityCode Codes[] =
		{
			EElysiumAnimActivityCode::KnockbackSmallHighForward,
			EElysiumAnimActivityCode::KnockbackSmallHighBack,
			EElysiumAnimActivityCode::KnockbackSmallHighLeft,
			EElysiumAnimActivityCode::KnockbackSmallHighRight,
			EElysiumAnimActivityCode::KnockbackNormalHighForward,
			EElysiumAnimActivityCode::KnockbackNormalHighBack,
			EElysiumAnimActivityCode::KnockbackNormalHighLeft,
			EElysiumAnimActivityCode::KnockbackNormalHighRight,
			EElysiumAnimActivityCode::KnockbackSmallLowBack,
			EElysiumAnimActivityCode::KnockbackNormalLowBack,
		};
		for (const EElysiumAnimActivityCode Code : Codes)
		{
			const FString Named = ElysiumAnimIntent::ActivityName(Code);
			TestTrue(TEXT("every knockback code names an ACT_ literal"),
				Named.StartsWith(TEXT("ACT_KNOCKBACK_")));
			// The round trip is what keeps the two tables from drifting: a record carrying the literal
			// has to read back as the code, or the trace, Cog and the MCP surface disagree with the
			// producer about what played.
			TestEqual(*FString::Printf(TEXT("%s round-trips to its own code"), *Named),
				static_cast<int32>(ElysiumAnimIntent::ActivityCode(Named)),
				static_cast<int32>(Code));
			// **Stated, not defaulted.** A knockback plays on the reaction branch above the locomotion
			// pose, so the state underneath is a standing body — and the eight-state vocabulary has no
			// knockback state to name it with.
			TestEqual(*FString::Printf(TEXT("%s projects to Idle"), *Named),
				static_cast<int32>(ElysiumAnimGraph::StateForActivity(Code)),
				static_cast<int32>(EElysiumGraphState::Idle));
		}
		// The fallback cell is one of the same ten, not a spelling of its own.
		TestEqual(TEXT("the no-list fallback cell is a named code"),
			static_cast<int32>(ElysiumAnimIntent::ActivityCode(FallbackGroundedKnockbackActivity)),
			static_cast<int32>(EElysiumAnimActivityCode::KnockbackNormalHighForward));
	}

	// --- Eligibility ------------------------------------------------------------------------------
	{
		TestTrue(TEXT("a live victim whose template allows it is knocked back"),
			IsKnockbackAllowed(/*Alive*/ true, /*Disallow*/ false));
		TestFalse(TEXT("a dead victim is not"),
			IsKnockbackAllowed(/*Alive*/ false, /*Disallow*/ false));
		// The authored refusal eight `npctemplate*.txt` files carry — zombies, the cabbie, the
		// tutorial and crackhouse casts, the bomberman.
		TestFalse(TEXT("a template that disallows knockbacks refuses one"),
			IsKnockbackAllowed(/*Alive*/ true, /*Disallow*/ true));
		TestFalse(TEXT("and both together still refuse"),
			IsKnockbackAllowed(/*Alive*/ false, /*Disallow*/ true));
	}

	// --- The away direction, and the sign that is the whole of it ---------------------------------
	{
		// **The body travels AWAY from the blow**: victim minus attacker, not the reverse.
		const FVector Away = KnockbackAwayFrom(GAhead, GVictim);
		TestTrue(TEXT("an attacker dead ahead throws the body straight back along -X"),
			Away.GetSafeNormal().Equals(FVector(-1.0f, 0.0f, 0.0f), 1e-4f));
		// The z is zeroed, so a blow from above still names a horizontal direction.
		const FVector Overhead = KnockbackAwayFrom(FVector(100.0f, 0.0f, 300.0f), GVictim);
		TestEqual(TEXT("the vertical component is dropped"), static_cast<float>(Overhead.Z), 0.0f);
		TestTrue(TEXT("...leaving the horizontal direction intact"),
			Overhead.GetSafeNormal().Equals(FVector(-1.0f, 0.0f, 0.0f), 1e-4f));

		// **No attacker at all** — world damage, a script, a logic entity: straight backwards off the
		// victim's own facing.
		const FVector Backwards = KnockbackAwayWithoutAttacker(/*VictimYaw*/ 0.0f);
		TestTrue(TEXT("a victim facing +X with no attacker is thrown along -X"),
			Backwards.GetSafeNormal().Equals(FVector(-1.0f, 0.0f, 0.0f), 1e-4f));
		TestEqual(TEXT("...horizontally"), static_cast<float>(Backwards.Z), 0.0f);

		FElysiumKnockback NoAttacker;
		BuildKnockback(KnockbackAwayWithoutAttacker(30.0f), 30.0f, NoAttacker);
		TestEqual(TEXT("...which classifies as the BACK bucket whatever the facing"),
			static_cast<int32>(NoAttacker.Direction), static_cast<int32>(EDir::Back));
		TestEqual(TEXT("...naming the back cell"), FString(NoAttacker.Activity()),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK")));
		// And the snap is a no-op: a body already facing the right way is not turned.
		TestEqual(TEXT("...and the victim keeps the facing it had"), NoAttacker.SnapYawDegrees,
			30.0f, 1e-3f);
	}

	// --- The relative angle: the mirror that would survive a forward/back test only ----------------
	{
		float AwayYaw = 0.0f;
		float Rel = 0.0f;

		TestTrue(TEXT("an attacker dead ahead names a direction"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GAhead, GVictim), 0.0f, AwayYaw, Rel));
		TestEqual(TEXT("...away points along -X"), AwayYaw, 180.0f, 1e-3f);
		TestEqual(TEXT("...which is 180 relative to a victim facing +X"), Rel, 180.0f, 1e-3f);

		TestTrue(TEXT("an attacker behind names a direction"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GBehind, GVictim), 0.0f, AwayYaw, Rel));
		TestEqual(TEXT("...relative 0"), Rel, 0.0f, 1e-3f);

		// **The mirror check.** An attacker on the victim's RIGHT throws it to its LEFT, which is
		// relative 270. Reading the subtraction the other way round answers 90 and selects the RIGHT
		// cell — a failure invisible to any front/back case.
		TestTrue(TEXT("an attacker on the right names a direction"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GOnTheRight, GVictim), 0.0f, AwayYaw, Rel));
		TestEqual(TEXT("...and the body goes to the victim's left, relative 270"), Rel, 270.0f, 1e-3f);
		TestEqual(TEXT("...which is the LEFT bucket"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(Rel)), static_cast<int32>(EDir::Left));

		TestTrue(TEXT("an attacker on the left names a direction"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GOnTheLeft, GVictim), 0.0f, AwayYaw, Rel));
		TestEqual(TEXT("...and the body goes to its right, relative 90"), Rel, 90.0f, 1e-3f);
		TestEqual(TEXT("...which is the RIGHT bucket"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(Rel)),
			static_cast<int32>(EDir::Right));

		// Measured in the victim's OWN frame: turning the victim moves the answer with it.
		TestTrue(TEXT("the same attacker, victim turned around"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GAhead, GVictim), 180.0f, AwayYaw, Rel));
		TestEqual(TEXT("...reads as relative 0"), Rel, 0.0f, 1e-3f);

		// Degenerate: coincident origins. NOT a refusal of the knockback — the caller still gets one,
		// off the no-list fallback.
		TestFalse(TEXT("two bodies on one point degenerate"),
			KnockbackRelativeYaw(KnockbackAwayFrom(GVictim, GVictim), 0.0f, AwayYaw, Rel));
		TestEqual(TEXT("...and both answers are zeroed rather than left stale"), AwayYaw, 0.0f);
		TestEqual(TEXT("...both of them"), Rel, 0.0f);
		// A blow from directly overhead is NOT degenerate: the z is dropped before the test, so the
		// horizontal separation still names a direction.
		TestTrue(TEXT("an attacker directly overhead but offset still names one"),
			KnockbackRelativeYaw(KnockbackAwayFrom(FVector(1.0f, 0.0f, 300.0f), GVictim), 0.0f,
				AwayYaw, Rel));
	}

	// --- The four bands, on their own seams -------------------------------------------------------
	//
	// Retail's cascade is NOT symmetric: the front sector runs (316, 360) + [0, 45]. Every boundary
	// value is asserted on the inclusive side it actually falls on.
	{
		TestEqual(TEXT("relative 0 is the front band"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(0.0f)),
			static_cast<int32>(EDir::Forward));
		TestEqual(TEXT("45 is the last degree of the front band"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(45.0f)),
			static_cast<int32>(EDir::Forward));
		TestEqual(TEXT("one past it is the right flank"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(46.0f)),
			static_cast<int32>(EDir::Right));
		TestEqual(TEXT("135 is the last degree of the right flank"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(135.0f)),
			static_cast<int32>(EDir::Right));
		TestEqual(TEXT("one past it is the rear band"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(136.0f)),
			static_cast<int32>(EDir::Back));
		TestEqual(TEXT("225 is the last degree of the rear band"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(225.0f)),
			static_cast<int32>(EDir::Back));
		TestEqual(TEXT("one past it is the left flank"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(226.0f)),
			static_cast<int32>(EDir::Left));
		// The asymmetry, asserted rather than rounded off: the left flank runs one degree past 315.
		TestEqual(TEXT("316 is still the left flank"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(316.0f)),
			static_cast<int32>(EDir::Left));
		TestEqual(TEXT("...and one past THAT wraps back into the front band"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(317.0f)),
			static_cast<int32>(EDir::Forward));
		// The seam a fan would wrap on: 360 is 0.
		TestEqual(TEXT("360 folds onto 0"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(360.0f)),
			static_cast<int32>(EDir::Forward));
		TestEqual(TEXT("and a negative angle folds into the band it names"),
			static_cast<int32>(KnockbackDirectionForRelativeYaw(-90.0f)),
			static_cast<int32>(EDir::Left));
	}

	// --- The yaw snap -----------------------------------------------------------------------------
	//
	// The victim is turned so the authored cell's model-space direction points along `away`. The
	// four offsets are retail's, negated into Unreal's mirrored frame.
	{
		TestEqual(TEXT("the BACK cell puts the body's back on the away direction"),
			KnockbackSnapYaw(/*AwayYaw*/ 0.0f, EDir::Back), 180.0f, 1e-3f);
		TestEqual(TEXT("the LEFT cell puts its left there"),
			KnockbackSnapYaw(0.0f, EDir::Left), 90.0f, 1e-3f);
		TestEqual(TEXT("the FORWARD cell leaves it facing the away direction"),
			KnockbackSnapYaw(0.0f, EDir::Forward), 0.0f, 1e-3f);
		TestEqual(TEXT("the RIGHT cell puts its right there"),
			KnockbackSnapYaw(0.0f, EDir::Right), 270.0f, 1e-3f);
		// The answer is `AngleMod`ded: [0, 360), never a raw sum.
		TestEqual(TEXT("the sum wraps rather than running past 360"),
			KnockbackSnapYaw(200.0f, EDir::Back), 20.0f, 1e-3f);
		TestEqual(TEXT("...and a negative away yaw folds in too"),
			KnockbackSnapYaw(-90.0f, EDir::Left), 0.0f, 1e-3f);

		// The property all four encode, stated once: after the snap, the cell's own direction vector
		// IS the away direction.
		struct FCase { EDir Dir; float BodyOffset; };
		static const FCase Cases[] =
		{
			{ EDir::Forward, 0.0f },     // the body's forward
			{ EDir::Right, 90.0f },      // its right
			{ EDir::Back, 180.0f },      // its back
			{ EDir::Left, -90.0f },      // its left
		};
		for (const FCase& Case : Cases)
		{
			for (const float AwayYaw : { 0.0f, 37.0f, 190.0f, 350.0f })
			{
				const float Snapped = KnockbackSnapYaw(AwayYaw, Case.Dir);
				TestEqual(*FString::Printf(
					TEXT("bucket %d snapped to %.1f aims its cell along away yaw %.1f"),
					static_cast<int32>(Case.Dir), Snapped, AwayYaw),
					static_cast<float>(FRotator::ClampAxis(Snapped + Case.BodyOffset)),
					static_cast<float>(FRotator::ClampAxis(AwayYaw)), 1e-3f);
			}
		}
	}

	// --- The whole rule, composed ------------------------------------------------------------------
	{
		// The stand-in answers NORMAL/HIGH on every bucket, and the SMALL and LOW cells stay unselected
		// until the authored activity table is recovered.
		struct FCase
		{
			FVector AttackerOrigin;
			float VictimYaw;
			EDir Direction;
			const TCHAR* Cell;
			float SnapYaw;
			const TCHAR* What;
		};
		static const FCase Cases[] =
		{
			{ GAhead, 0.0f, EDir::Back, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK"), 0.0f,
				TEXT("a blow to the face") },
			{ GBehind, 0.0f, EDir::Forward, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD"), 0.0f,
				TEXT("a blow from behind") },
			{ GOnTheRight, 0.0f, EDir::Left, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_LEFT"), 0.0f,
				TEXT("a blow from the right") },
			{ GOnTheLeft, 0.0f, EDir::Right, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_RIGHT"), 0.0f,
				TEXT("a blow from the left") },
			// Off a bucket centre, so the snap actually turns the body: away runs along the
			// third-quadrant diagonal (225) and the BACK cell needs the body facing 45.
			{ GAheadRight, 0.0f, EDir::Back, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK"), 45.0f,
				TEXT("a diagonal blow") },
		};
		for (const FCase& Case : Cases)
		{
			FElysiumKnockback Out;
			BuildKnockback(KnockbackAwayFrom(Case.AttackerOrigin, GVictim), Case.VictimYaw, Out);
			TestEqual(*FString::Printf(TEXT("%s selects bucket %d"), Case.What,
				static_cast<int32>(Case.Direction)),
				static_cast<int32>(Out.Direction), static_cast<int32>(Case.Direction));
			TestEqual(*FString::Printf(TEXT("%s plays %s"), Case.What, Case.Cell),
				FString(Out.Activity()), FString(Case.Cell));
			TestEqual(*FString::Printf(TEXT("%s snaps the victim to %.1f"), Case.What, Case.SnapYaw),
				Out.SnapYawDegrees, Case.SnapYaw, 1e-3f);
			TestEqual(TEXT("...off the stand-in's NORMAL size"),
				static_cast<int32>(Out.Size), static_cast<int32>(StandInKnockbackSize));
			TestEqual(TEXT("...and its HIGH cell"),
				static_cast<int32>(Out.Height), static_cast<int32>(StandInKnockbackHeight));
			TestFalse(TEXT("...without reaching the no-list fallback"), Out.bFallbackCell);
			TestEqual(TEXT("...and the away yaw is carried for the record"), Out.AwayWorldYawDegrees,
				AwayYawOf(Case.AttackerOrigin, GVictim), 1e-3f);
		}

		// The stand-in never reaches the SMALL family or either LOW cell. Recovering the authored
		// activity table is what changes this, and this case is what says so.
		TestEqual(TEXT("the stand-in size is NORMAL"),
			static_cast<int32>(StandInKnockbackSize), static_cast<int32>(ESize::Normal));
		TestEqual(TEXT("the stand-in height is HIGH"),
			static_cast<int32>(StandInKnockbackHeight), static_cast<int32>(EHeight::High));
	}

	// --- The degenerate contact, and the no-list fallback it takes ---------------------------------
	{
		FElysiumKnockback Out;
		BuildKnockback(KnockbackAwayFrom(GVictim, GVictim), /*VictimYaw*/ 0.0f, Out);
		TestTrue(TEXT("coincident origins take the no-list fallback"), Out.bFallbackCell);
		// Retail's fallback is the flying activity 0x8b, downgraded on a grounded body.
		TestEqual(TEXT("...which is the flying-forward cell downgraded to the grounded one"),
			FString(Out.Activity()), FString(FallbackGroundedKnockbackActivity));
		TestEqual(TEXT("...and it is the NORMAL/HIGH forward cell"), FString(Out.Activity()),
			FString(TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD")));
		// The classifier's own answer on that path is bucket 0, stated rather than reached through
		// the bands — a zeroed relative yaw would otherwise fall in the FRONT band. The bucket and the
		// fallback cell therefore disagree here, and that is retail's shape rather than a defect.
		TestEqual(TEXT("...while the classifier still answers bucket 0"),
			static_cast<int32>(Out.Direction), static_cast<int32>(EDir::Back));
		TestEqual(TEXT("...so the snap runs off a zero away yaw and bucket 0's offset"),
			Out.SnapYawDegrees, 180.0f, 1e-3f);
		TestEqual(TEXT("...with both carried angles zeroed"), Out.AwayWorldYawDegrees, 0.0f);
		TestEqual(TEXT("...both of them"), Out.RelativeYawDegrees, 0.0f);
	}

	return true;
}

// =====================================================================================
// The RNG contract: nothing in this family draws
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumKnockbackRngTest,
	"Elysium.Substrate.Knockback.Rng", GElysiumTestFlags)
bool FElysiumKnockbackRngTest::RunTest(const FString&)
{
	using namespace ElysiumReactions;

	// Retail draws once, to pick among the candidates its authored activity table lists for the
	// selected bucket. The stand-in offers exactly one candidate, so there is nothing to draw for —
	// and until the table is recovered, a knockback must move the Reaction stream by NOTHING.
	// Otherwise how many knockbacks a fight contained would silently reshuffle every reaction after
	// it (`docs/architecture/save-architecture.md` § 8).
	ElysiumRng::SeedAll(0x4B4E4B32);
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
	const int32 SeedBefore = Rng.GetCurrentSeed();

	FElysiumKnockback Out;

	// A refusal — the eligibility gate, which is where every "no knockback" answer now lives.
	TestFalse(TEXT("a disallowed victim is refused"),
		IsKnockbackAllowed(/*Alive*/ true, /*Disallow*/ true));
	TestEqual(TEXT("...and the refusal advances the Reaction stream by nothing"),
		Rng.GetCurrentSeed(), SeedBefore);

	// A success, and a whole run of them: the stream still has not moved.
	for (int32 i = 0; i < 64; ++i)
	{
		const float Yaw = static_cast<float>(i) * 5.5f;
		BuildKnockback(KnockbackAwayFrom(FVector(100.0f, 25.0f, 0.0f), FVector::ZeroVector), Yaw,
			Out);
	}
	TestEqual(TEXT("64 knockbacks advance the Reaction stream by nothing"),
		Rng.GetCurrentSeed(), SeedBefore);

	// Including the degenerate one, which takes a different cell but still draws nothing.
	BuildKnockback(FVector::ZeroVector, 0.0f, Out);
	TestTrue(TEXT("the degenerate contact took the fallback"), Out.bFallbackCell);
	TestEqual(TEXT("...and still drew nothing"), Rng.GetCurrentSeed(), SeedBefore);

	return true;
}

// =====================================================================================
// The producer: what a knocked-back victim actually plays
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumKnockbackProducerTest,
	"Elysium.Substrate.Knockback.Producer", GElysiumTestFlags)
bool FElysiumKnockbackProducerTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeKnockbackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The request the victim produces ----------------------------------------------------------
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();

		if (!TestTrue(TEXT("the knockback plays"), F.Knockback(/*Now*/ 10.0)))
		{
			return false;
		}

		// The direction the blow states: the attacker is dead ahead, so the body goes back.
		TestTrue(TEXT("the victim resolves the back cell"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK")));
		// The reaction BAND, not the one-shot slot: a knockback replaces the pose the body holds.
		TestTrue(TEXT("it is played on the reaction route"),
			Saw(F.Services, TEXT("PlayNpcOneShot"), TEXT("route=reaction")));
		TestTrue(TEXT("...in the reaction band"),
			Saw(F.Services, TEXT("PlayNpcOneShot"), TEXT("prio=reaction")));
		// A knockback is four authored cells, not a fan: the direction is IN the cell name and the
		// yaw snap is what aims it, so the request carries no steering angle.
		TestTrue(TEXT("the request carries no hit yaw — the direction is in the cell"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("hit=0.0")));
		TestEqual(TEXT("exactly one reaction plays for the knockback"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);

		// The claim a played knockback takes.
		TestTrue(TEXT("the base channel is held for as long as it runs"),
			F.Victim->MeleeReactionHoldsBaseUntil > 10.0);
		// **The knockback itself adds NO draw.** The one the producer spends is the shared reaction
		// path's weighted-variant pick — retail's `SelectWeightedSequence`, which the flinch and the
		// block family take on exactly the same terms — and not a knockback term. If the authored
		// activity table is ever recovered, its `RandomInt` over the bucket's candidates is a SECOND
		// draw and this count is what says so.
		TestTrue(TEXT("the producer spends exactly one draw, and it is the shared variant pick"),
			SpentDraws(SeedBefore, Rng, 1));
	}

	// --- The four buckets, each through the real producer ------------------------------------------
	{
		struct FCase
		{
			FVector AttackerOrigin;
			const TCHAR* Cell;
			const TCHAR* What;
		};
		static const FCase Cases[] =
		{
			{ GAhead,      TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK"),    TEXT("a blow to the face") },
			{ GBehind,     TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD"), TEXT("a blow from behind") },
			{ GOnTheRight, TEXT("ACT_KNOCKBACK_NORMAL_HIGH_LEFT"),    TEXT("a blow from the right") },
			{ GOnTheLeft,  TEXT("ACT_KNOCKBACK_NORMAL_HIGH_RIGHT"),   TEXT("a blow from the left") },
		};
		for (const FCase& Case : Cases)
		{
			FKnockbackFixture F;
			if (!F.Stand(*this))
			{
				return false;
			}
			F.Attacker->Origin = Case.AttackerOrigin;
			if (!TestTrue(TEXT("the knockback plays"), F.Knockback(10.0)))
			{
				return false;
			}
			TestTrue(*FString::Printf(TEXT("%s sends the body onto %s"), Case.What, Case.Cell),
				Saw(F.Services, TEXT("ResolveNpcActivityClip"), Case.Cell));
			// Each of these four is already aimed, so the snap is a no-op and the victim keeps facing
			// the way it was.
			TestEqual(*FString::Printf(TEXT("%s leaves the already-aimed facing alone"), Case.What),
				F.VictimUnrealYaw(), 0.0f, 1e-3f);
		}
	}

	// --- The yaw snap actually reaches the victim's facing -----------------------------------------
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		// Away runs along the third-quadrant diagonal, so the BACK cell needs the body facing 45.
		F.Attacker->Origin = GAheadRight;
		if (!TestTrue(TEXT("the diagonal knockback plays"), F.Knockback(10.0)))
		{
			return false;
		}
		TestEqual(TEXT("the victim is turned so its back is on the away direction"),
			static_cast<float>(FRotator::ClampAxis(F.VictimUnrealYaw())), 45.0f, 1e-3f);
		TestTrue(TEXT("...and it plays the back cell"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK")));
		// The write goes through the ordinary substrate facing door, so the authoritative Source
		// field moved with it rather than only the body.
		TestEqual(TEXT("the entity's own Source yaw carries the negated Unreal one"),
			static_cast<float>(F.Victim->Angles.Y), -45.0f, 1e-3f);
	}

	// --- One channel, one claim: the flinch yields to a knockback -----------------------------------
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		if (!TestTrue(TEXT("the knockback plays"), F.Knockback(/*Now*/ 0.0)))
		{
			return false;
		}
		F.Services.Calls.Reset();

		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();

		FElysiumDmg Dmg = ResolvedDmg(5);
		Dmg.Source = F.Attacker->Handle;
		F.Victim->CommitDamage(Dmg);

		TestEqual(TEXT("the damage the same contact commits lands"), DamageTaken(*F.Victim), 5);
		TestEqual(TEXT("the victim plays exactly ONE reaction for the contact — the knockback"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		TestEqual(TEXT("...and the yielded flinch advances the Reaction stream by nothing"),
			Rng.GetCurrentSeed(), SeedBefore);
	}

	// --- A victim whose template disallows knockbacks -----------------------------------------------
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumNpc* Npc = F.Victim->AsNpc();
		if (!TestNotNull(TEXT("the victim is an NPC, which is what wears a template"), Npc))
		{
			return false;
		}
		Npc->bDisallowKnockbacks = true;

		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();

		TestFalse(TEXT("a template that disallows knockbacks refuses one"), F.Knockback(10.0));
		TestEqual(TEXT("...and plays nothing"), CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		TestEqual(TEXT("...and leaves the victim's facing alone"), F.VictimUnrealYaw(), 0.0f, 1e-3f);
		TestEqual(TEXT("...and advances the Reaction stream by nothing"),
			Rng.GetCurrentSeed(), SeedBefore);
	}

	// --- A dead victim is not knocked back ----------------------------------------------------------
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Victim->Kill();

		TestFalse(TEXT("a dead victim is refused"), F.Knockback(10.0));
		TestEqual(TEXT("...and plays nothing"), CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
	}

	// --- The fail-safe, driven end to end through a real contact -------------------------------------
	//
	// A content-free world has no `rules.txt`, so the margin classifier answers `Unclassified` — which
	// names no band. A knockback must NOT be produced from it, exactly as no block reaction is: an
	// absent margin table is a reported failure, never an invented outcome.
	{
		FKnockbackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		const float FacingBefore = F.VictimUnrealYaw();
		F.Swing(GHammer);

		TestTrue(TEXT("the swing landed"), DamageTaken(*F.Victim) > 0);
		TestFalse(TEXT("an unclassified record knocks nobody back"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_KNOCKBACK")));
		TestEqual(TEXT("...and turns nobody"), F.VictimUnrealYaw(), FacingBefore, 1e-3f);
		// The generic flinch is the reaction it DOES produce, from the shared health commit.
		TestTrue(TEXT("...while the ordinary damage flinch still plays"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_HIT_")));
	}

	return true;
}

}   // namespace ElysiumKnockbackTests

#endif   // WITH_DEV_AUTOMATION_TESTS
