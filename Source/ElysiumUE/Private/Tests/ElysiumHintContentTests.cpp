#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapEntities.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 8, wave 2: the hint list and its searches over the BAKED witnesses, `sp_tutorial_1` and
// `sm_hub_1`. Every expected number is `docs/specs/0018-world-ai-infrastructure/story8/census.md` (the
// section is named in the message) except where a message says it was read off the map export instead.
//
// The world is the map's whole entity table plus one or two probe NPCs of the Troika human line
// (`npc_VHumanCombatant`, the classname `thug_3` is) appended after it, so every map row keeps its BSP
// index as its handle index. The probes carry no `m_hHintCoverObject`, so the two cover validators
// (`0x102974f0` for types 100 / 101, `0x10297430` for 10200) run with a null enemy: the first passes
// ("a null enemy accepts before any test", `0x10296c40`), the second refuses ("no cover object",
// `0x10297430`). The tutorial's tactical search therefore admits the four type-101 rows (465..462) and
// not the eight corner rows; census § 3 step 3 names the first pick as row 465 either way.
//
// Coordinates: the census quotes Source units; the world is centimetres with Y negated
// (`ElysiumMove::U` = 2.54 cm per unit), so a census `(x, y, z)` stands as `(x, -y, z) * U`.

static constexpr EAutomationTestFlags GElysiumHintContentTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumHintContentTests
{
	constexpr double CmPerUnit = ElysiumMove::U;
	constexpr int32 Miss = INDEX_NONE;
	const TCHAR* const Tutorial = TEXT("sp_tutorial_1");
	const TCHAR* const Hub = TEXT("sm_hub_1");
	const TCHAR* const ProbeA = TEXT("hint_probe_a");
	const TCHAR* const ProbeB = TEXT("hint_probe_b");

	// The tactical search's arguments as every recovered call site gives them (census § 2): flags 8
	// (not the mask -- the signature is `(npc, flags, mask, radius)`), mask 1 the cover band, slot 550's
	// radius 1024.
	constexpr uint8 TacticalFlags = 8;
	constexpr int32 CoverMask = 1;
	constexpr float TacticalRadius = 1024.0f;

	// A baked map's entity table and its place set (the `Elysium.Content.Places.WanderHub` shape).
	struct FBaked
	{
		FElysiumEntityDefs Defs;
		const UElysiumMapPlaces* Places = nullptr;
	};

	bool LoadBaked(FAutomationTestBase& Test, const TCHAR* Map, FBaked& Out)
	{
		Out.Places = LoadObject<UElysiumMapPlaces>(nullptr, *FElysiumContentPaths::BakedMapPlaces(Map),
			nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: the places asset is baked"), Map), Out.Places))
		{
			return false;
		}
		return Test.TestTrue(*FString::Printf(TEXT("%s: the entity table loads"), Map),
			ElysiumEntityDefSource::Load(Map, Out.Defs) != EElysiumEntityDefSource::None);
	}

	// The builder for the whole map: the loaded table (copied, so a case can stand it twice) and the
	// adopted network. Probes are added by the caller, after the map's own rows.
	FElysiumNpcWorldBuilder Stand(const TCHAR* Map, uint32 Seed, const FBaked& Baked)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.Defs = Baked.Defs;
		Builder.Places = Baked.Places->Rows;
		Builder.WanderCaps = Baked.Places->WanderCaps;
		Builder.CrosswalkPairs = Baked.Places->CrosswalkPairs;
		Builder.CrosswalkMotions = Baked.Places->CrosswalkPairMotions;
		return Builder;
	}

	// `thug_3`'s `hint_groups` is "1 2 3 ... 32": every group.
	FString AllGroups()
	{
		FString Out;
		for (int32 Group = 1; Group <= 32; ++Group)
		{
			if (Group > 1)
			{
				Out += TEXT(" ");
			}
			Out += FString::FromInt(Group);
		}
		return Out;
	}

	void AddProbe(FElysiumNpcWorldBuilder& Builder, const TCHAR* Name, const FVector& OriginCm)
	{
		Builder.AddNpc(Name, OriginCm).Keys.Add(TEXT("hint_groups"), AllGroups());
	}

	const FElysiumEntityDef* FindDef(const FBaked& Baked, const TCHAR* TargetName)
	{
		return Baked.Defs.Defs.FindByPredicate(
			[TargetName](const FElysiumEntityDef& Def) { return Def.TargetName == TargetName; });
	}

	struct FRig
	{
		FElysiumNpcWorldFixture F;
		FElysiumNpc* A = nullptr;
		FElysiumNpc* B = nullptr;

		explicit FRig(FElysiumNpcWorldBuilder&& Builder)
			: F(MoveTemp(Builder))
		{
			A = F.Npc(ProbeA);
			B = F.Npc(ProbeB);
			FElysiumNpcWorldFixture::Quiet({ A, B });
		}

		FElysiumHint* HintAt(int32 Row)
		{
			return F.World.Entities().IsValidIndex(Row) ? FElysiumHint::Cast(F.World.Entities()[Row].Get()) : nullptr;
		}

		FElysiumHint* Named(const TCHAR* Name) { return FElysiumHint::Cast(F.World.FindByName(Name)); }

		void ResetCursor() { F.World.SetHintCursor(INDEX_NONE); }

		// The tactical search from a NPC, with no cursor: the walk starts at the head.
		int32 Tactical(FElysiumNpc* Npc, float Radius = TacticalRadius)
		{
			ResetCursor();
			return Npc->FindHintByClassMask(TacticalFlags, CoverMask, Radius);
		}

		// One input through the world's chokepoint, by handle (the hints are unnamed on the tutorial).
		void Fire(FElysiumHint* Hint, const TCHAR* Input)
		{
			F.World.AcceptInput(Hint->Handle, FName(Input), FElysiumVariant(), FElysiumEntityHandle::Invalid(),
				FElysiumEntityHandle::Invalid());
		}

		// The same, by targetname (the hub's named rows).
		void FireNamed(const TCHAR* Name, const TCHAR* Input)
		{
			F.World.AcceptInput(FString(Name), FName(Input), FElysiumVariant(), FElysiumEntityHandle::Invalid(),
				FElysiumEntityHandle::Invalid());
		}
	};

	// The tutorial's cover rows in list order (census § 1): 465..462 are `info_node_cover_low` (type
	// 101), 461..454 `info_node_cover_corner` (type 10200).
	constexpr int32 FirstCoverRow = 465;
	constexpr int32 LastCoverRow = 454;
	constexpr int32 FirstCoverListPos = 34;
}

// -------------------------------------------------------------------------------------------------
// 1. TutorialList
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentTutorialListTest,
	"Elysium.Content.Hints.TutorialList", GElysiumHintContentTestFlags)
bool FElysiumHintContentTutorialListTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Tutorial, Baked))
	{
		return false;
	}
	FRig R(Stand(Tutorial, 8001, Baked));
	const TArray<int32>& List = R.F.World.HintList();

	TestEqual(TEXT("census § 1: 49 hint rows on sp_tutorial_1"), List.Num(), 49);
	TestEqual(TEXT("...and the store's count word agrees"), R.F.World.HintCount(), 49);
	TestEqual(TEXT("`DAT_10925454` is NULL after the load: every hint the factory makes zeroes the cursor"),
		R.F.World.HintCursor(), Miss);

	// Census § 1 "First five in retail LIST order": the last BSP row heads the list.
	const int32 Head[] = { 1764, 1763, 1760, 1759, 1717 };
	for (int32 Pos = 0; Pos < static_cast<int32>(UE_ARRAY_COUNT(Head)); ++Pos)
	{
		TestEqual(*FString::Printf(TEXT("census § 1: list position %d is BSP row %d (`0x102d2e30` prepends)"), Pos,
			Head[Pos]), List.IsValidIndex(Pos) ? List[Pos] : Miss, Head[Pos]);
	}
	// ...and the 12 mask-1 rows are positions 34..45 = rows 465, 464, 463, 462, then 461..454.
	for (int32 Offset = 0; Offset <= FirstCoverRow - LastCoverRow; ++Offset)
	{
		TestEqual(*FString::Printf(TEXT("census § 1: list position %d is cover row %d"),
			FirstCoverListPos + Offset, FirstCoverRow - Offset),
			List.IsValidIndex(FirstCoverListPos + Offset) ? List[FirstCoverListPos + Offset] : Miss,
			FirstCoverRow - Offset);
	}

	// The class word (`+0x474`, derived by `CAI_Hint::Spawn`): 0 on a patrol point, 1 on a cover row.
	// The rating: `0x102d0b60`'s typed arms REPLACE the authored 3 with the `NPC_Cover_Distance_Scalar`
	// row (2.5) when a rulebook stands. This fixture's world is built with no game state, so the
	// authored float stays; the expectation follows whichever case the fixture is in.
	UElysiumSessionSubsystem* GameState = R.F.World.GetGameState();
	const bool bRulebook = GameState != nullptr && GameState->Rulebook() != nullptr;
	const float ExpectedRating = bRulebook ? 2.5f : 3.0f;
	int32 Patrol = 0;
	int32 Cover = 0;
	for (const int32 Row : List)
	{
		const FElysiumHint* Hint = R.HintAt(Row);
		if (Hint == nullptr)
		{
			AddError(FString::Printf(TEXT("list row %d is not a live hint"), Row));
			continue;
		}
		if (Hint->HintType == 10000)
		{
			++Patrol;
			TestEqual(*FString::Printf(TEXT("row %d (patrol point) carries class word 0"), Row), Hint->ClassMask, 0);
		}
		else
		{
			++Cover;
			TestEqual(*FString::Printf(TEXT("row %d (cover, type %d) carries class word 1"), Row, Hint->HintType),
				Hint->ClassMask, 1);
			TestNearlyEqual(*FString::Printf(TEXT("row %d m_flHintRating: %s"), Row,
				bRulebook ? TEXT("a rulebook stands, the authored 3 becomes the scalar row 2.5")
					: TEXT("NO rulebook in this fixture, so the authored 3 stays")),
				Hint->HintRating, ExpectedRating, 0.001f);
		}
	}
	TestEqual(TEXT("census § 1: 37 patrol points (type 10000)"), Patrol, 37);
	TestEqual(TEXT("census § 1: 12 cover rows (8 corner + 4 low)"), Cover, 12);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 2. TutorialTacticalSearch
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentTutorialTacticalTest,
	"Elysium.Content.Hints.TutorialTacticalSearch", GElysiumHintContentTestFlags)
bool FElysiumHintContentTutorialTacticalTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Tutorial, Baked))
	{
		return false;
	}
	const FElysiumEntityDef* Thug = FindDef(Baked, TEXT("thug_3"));
	if (!TestNotNull(TEXT("thug_3 (row 472) stands in the table"), Thug))
	{
		return false;
	}
	// Census § 3: thug_3 at (-1725, 471, 0) Source units; the world's Y is negated.
	TestTrue(*FString::Printf(TEXT("census § 3: thug_3's origin is (-1725, 471, 0) units (%s)"),
		*Thug->Origin.ToString()), Thug->Origin.Equals(FVector(-1725.0, -471.0, 0.0) * CmPerUnit, CmPerUnit));

	FElysiumNpcWorldBuilder Builder = Stand(Tutorial, 8002, Baked);
	AddProbe(Builder, ProbeA, Thug->Origin);
	FRig R(MoveTemp(Builder));
	if (!TestNotNull(TEXT("the probe stands"), R.A))
	{
		return false;
	}
	TestEqual(TEXT("hint_groups all: the probe's group set is every bit"),
		static_cast<int64>(R.A->ScheduleHost.HintGroupMask), static_cast<int64>(0xffffffffu));

	// Census § 3 step 3: from the head with no cursor the first admitted row is 465.
	const int32 First = R.Tactical(R.A);
	TestEqual(TEXT("census § 3 step 3: FindHintByClassMask(8, 1, 1024) answers row 465 (0x102d2980, first admitted)"),
		First, 465);
	TestEqual(TEXT("...and the cursor is now that hint (`DAT_10925454`)"), R.F.World.HintCursor(), 465);

	// The walk starts at cursor->next, so an identical call rotates on: 464.
	const int32 Second = R.A->FindHintByClassMask(TacticalFlags, CoverMask, TacticalRadius);
	TestEqual(TEXT("a second identical call answers the next admitted row, 464: the walk starts after the cursor"),
		Second, 464);
	TestEqual(TEXT("...and the cursor moved to it"), R.F.World.HintCursor(), 464);

	// Census-correct misses (§ 2): no mask-8 (kick) and no type-10100 (cower) rows on this map.
	R.ResetCursor();
	TestEqual(TEXT("census § 2: the melee arm's mask 8 finds nothing on sp_tutorial_1 -- a census-correct miss, "
		"not a defect (no kick_at / kick_over hints exist)"),
		R.A->FindHintByClassMask(8, 8, 1024.0f), Miss);
	R.ResetCursor();
	TestEqual(TEXT("census § 2: the cower search (type 10100, flags 2, radius 4096) finds nothing -- a "
		"census-correct miss, not a defect (zero info_hint rows); the flee path takes over"),
		R.A->FindHintNear(10100, 2, 4096.0f), Miss);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 3. TutorialClaimAndCooldown
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentTutorialClaimTest,
	"Elysium.Content.Hints.TutorialClaimAndCooldown", GElysiumHintContentTestFlags)
bool FElysiumHintContentTutorialClaimTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Tutorial, Baked))
	{
		return false;
	}
	const FElysiumEntityDef* Thug = FindDef(Baked, TEXT("thug_3"));
	if (!TestNotNull(TEXT("thug_3 stands in the table"), Thug))
	{
		return false;
	}
	FElysiumNpcWorldBuilder Builder = Stand(Tutorial, 8003, Baked);
	AddProbe(Builder, ProbeA, Thug->Origin);
	AddProbe(Builder, ProbeB, Thug->Origin);
	FRig R(MoveTemp(Builder));
	FElysiumHint* Row = R.HintAt(465);
	if (!TestNotNull(TEXT("both probes stand"), R.A) || !TestNotNull(TEXT("...both"), R.B)
		|| !TestNotNull(TEXT("row 465 is a hint"), Row))
	{
		return false;
	}

	TestTrue(TEXT("0x102d1350: A's claim on row 465 succeeds"), R.A->ClaimHint(465));
	TestTrue(TEXT("...m_hHintOwner is A"), Row->HintOwner == R.A->Handle);
	TestTrue(TEXT("0x102d1450: A owns it"), R.A->OwnsHint(465));
	TestFalse(TEXT("...B does not"), R.B->OwnsHint(465));
	TestTrue(TEXT("0x102d1540 arm 1: the hint is available to its owner A"), R.A->IsHintAvailableToMe(465));
	TestFalse(TEXT("0x102d1540 arm 3: ...and not to B (a live owner that is not B)"), R.B->IsHintAvailableToMe(465));

	TestEqual(TEXT("0x102d14c0 arm 3: B's search at the same origin skips the owned row 465 and gets 464"),
		R.Tactical(R.B), 464);

	// The release: owner cleared, NextUseTime = curtime + 5.0, and the hint is cooling.
	const double Now = R.F.World.NowSeconds();
	R.A->ReleaseHintNode(465, 5.0f);
	TestFalse(TEXT("0x102d1420: the release clears the owner"), Row->HintOwner.IsSet());
	TestNearlyEqual(TEXT("...and stamps NextUseTime = curtime + 5.0"), Row->NextUseTime,
		static_cast<float>(Now) + 5.0f, 0.001f);
	TestFalse(TEXT("0x102d1540 arm 2: cooling, so unavailable to A"), R.A->IsHintAvailableToMe(465));
	TestFalse(TEXT("...and to B"), R.B->IsHintAvailableToMe(465));

	R.F.World.Tick(Now + 4.9);
	TestEqual(TEXT("0x102d14c0 arm 2: at Now + 4.9 the cooling row is still skipped"), R.Tactical(R.B), 464);
	R.F.World.Tick(Now + 5.0);
	TestEqual(TEXT("0x102d14c0: at Now + 5.0 `curtime < m_flNextUseTime` is false (strict), so row 465 is admitted"),
		R.Tactical(R.B), 465);
	TestTrue(TEXT("0x102d1540: available to A once the stamp has passed"), R.A->IsHintAvailableToMe(465));
	TestTrue(TEXT("...and to B"), R.B->IsHintAvailableToMe(465));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 4. TutorialDisableHint
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentTutorialDisableTest,
	"Elysium.Content.Hints.TutorialDisableHint", GElysiumHintContentTestFlags)
bool FElysiumHintContentTutorialDisableTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Tutorial, Baked))
	{
		return false;
	}
	const FElysiumEntityDef* Thug = FindDef(Baked, TEXT("thug_3"));
	if (!TestNotNull(TEXT("thug_3 stands in the table"), Thug))
	{
		return false;
	}
	FElysiumNpcWorldBuilder Builder = Stand(Tutorial, 8004, Baked);
	AddProbe(Builder, ProbeA, Thug->Origin);
	FRig R(MoveTemp(Builder));
	FElysiumHint* Row = R.HintAt(465);
	if (!TestNotNull(TEXT("the probe stands"), R.A) || !TestNotNull(TEXT("row 465 is a hint"), Row))
	{
		return false;
	}
	const int32 Count = R.F.World.HintCount();

	// Inputs go through `FElysiumEntityWorld::AcceptInput`, the only input path in the game; the brief
	// states Python's `DisableHint()` takes this same path.
	TestEqual(TEXT("baseline: the search answers row 465"), R.Tactical(R.A), 465);

	R.Fire(Row, TEXT("DisableHint"));
	TestEqual(TEXT("DisableHint (0x102d0a20) sets m_iDisabled: the search skips row 465 and answers 464"),
		R.Tactical(R.A), 464);
	TestEqual(TEXT("...m_iDisabled is 1"), Row->Disabled, 1);

	R.Fire(Row, TEXT("EnableHint"));
	TestEqual(TEXT("EnableHint on the hint DisableHint just HID is swallowed by retail's hidden-entity gate "
		"(FUN_100abc90: only ScriptUnhide passes a hidden entity), so m_iDisabled stays 1"), Row->Disabled, 1);
	TestEqual(TEXT("...and the search still answers 464 (this is where the brief's 'EnableHint gives 465 again' "
		"does not hold: it holds after ScriptUnhide, next)"), R.Tactical(R.A), 464);

	R.Fire(Row, TEXT("ScriptUnhide"));
	TestEqual(TEXT("ScriptUnhide (slot 78, 0x102d0890) passes the gate and clears m_iDisabled: row 465 again"),
		R.Tactical(R.A), 465);

	// A hint disabled while NOT hidden takes EnableHint: the input's own effect.
	Row->Disabled = 1;
	TestEqual(TEXT("(a disabled, visible row 465 is skipped)"), R.Tactical(R.A), 464);
	R.Fire(Row, TEXT("EnableHint"));
	TestEqual(TEXT("EnableHint (0x102d09f0) on a visible hint clears m_iDisabled: 465 again"), R.Tactical(R.A), 465);

	R.Fire(Row, TEXT("Kill"));
	TestFalse(TEXT("slot 119 0x102d08c0 is ScriptHide: Kill never kills a hint"), Row->IsDead());
	TestEqual(TEXT("...the killed hint is skipped by the search (464)"), R.Tactical(R.A), 464);
	TestEqual(TEXT("...and HintCount() is unchanged: a killed hint stays on the list (shape.md)"),
		R.F.World.HintCount(), Count);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 5. HubListAndNamedDisable
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentHubTest,
	"Elysium.Content.Hints.HubListAndNamedDisable", GElysiumHintContentTestFlags)
bool FElysiumHintContentHubTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Hub, Baked))
	{
		return false;
	}
	// Census § 3: `cover_front_10`, row 190, type 100, at (-1173, 583, -111) units.
	const FElysiumEntityDef* Front = FindDef(Baked, TEXT("cover_front_10"));
	if (!TestNotNull(TEXT("cover_front_10 stands in the table"), Front))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("census § 3: cover_front_10's origin is (-1173, 583, -111) units (%s)"),
		*Front->Origin.ToString()), Front->Origin.Equals(FVector(-1173.0, -583.0, -111.0) * CmPerUnit, CmPerUnit));

	FElysiumNpcWorldBuilder Builder = Stand(Hub, 8005, Baked);
	// 100 units east (+X, the axis the Y flip leaves alone) of the hint.
	AddProbe(Builder, ProbeA, Front->Origin + FVector(100.0 * CmPerUnit, 0.0, 0.0));
	FRig R(MoveTemp(Builder));
	if (!TestNotNull(TEXT("the probe stands"), R.A))
	{
		return false;
	}
	const TArray<int32>& List = R.F.World.HintList();
	TestEqual(TEXT("census § 1: 274 hint rows on sm_hub_1"), List.Num(), 274);
	TestEqual(TEXT("census § 1: the list head is BSP row 2399"), List.Num() > 0 ? List[0] : Miss, 2399);

	FElysiumHint* Cover = R.Named(TEXT("cover_front_10"));
	if (!TestNotNull(TEXT("cover_front_10 is found by name, as a hint"), Cover))
	{
		return false;
	}
	TestEqual(TEXT("census § 3: cover_front_10 is row 190"), Cover->Handle.Index, 190);
	TestEqual(TEXT("...of type 100 (cover_med)"), Cover->HintType, 100);
	TestTrue(TEXT("census § 1: the 24 named cover_med rows are StartHidden 1, so cover_front_10 is born hidden"),
		Cover->IsHidden());

	// The brief's radius 1024 puts row 1826 (list position 112, 766 units away, an unnamed cover_med) ahead
	// of row 190 (position 255): a list-order fact read off the map export, not the census.
	TestEqual(TEXT("export-derived, not census: at radius 1024 the first admitted row from the head is 1826, "
		"ahead of cover_front_10 in list order"), R.Tactical(R.A), 1826);
	// So the disable is watched at radius 128 units, where cover_front_10 (100 units) is the only
	// type-100/101 row in reach (the corner row 2076 at 125 units needs a cover object and is refused).
	constexpr float NearRadius = 128.0f;
	TestEqual(TEXT("census § 3 (radius 128 to isolate it): cover_front_10 is the answer before the disable"),
		R.Tactical(R.A, NearRadius), 190);

	// `DisableHint` on a HIDDEN hint: retail's `CBaseEntity::AcceptInput` gate (`FUN_100abc90`) swallows
	// every input but `ScriptUnhide` on a hidden entity, so the named row keeps m_iDisabled 0 and stays
	// the answer. The census recipe (§ 3 last paragraph) does not mention the gate.
	R.FireNamed(TEXT("cover_front_10"), TEXT("DisableHint"));
	TestEqual(TEXT("DisableHint on the born-hidden row is swallowed by the hidden-entity gate: m_iDisabled stays 0"),
		Cover->Disabled, 0);
	TestEqual(TEXT("...and the row is still the answer"), R.Tactical(R.A, NearRadius), 190);

	R.FireNamed(TEXT("cover_front_10"), TEXT("ScriptUnhide"));
	R.FireNamed(TEXT("cover_front_10"), TEXT("DisableHint"));
	TestEqual(TEXT("after ScriptUnhide the same DisableHint lands: m_iDisabled is 1"), Cover->Disabled, 1);
	const int32 After = R.Tactical(R.A, NearRadius);
	TestTrue(TEXT("census § 3: the disabled row vanishes from admission"), After != 190);
	TestEqual(TEXT("...with no other type-100/101 row inside 128 units, the search misses"), After, Miss);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 6. StartHiddenRows
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintContentStartHiddenTest,
	"Elysium.Content.Hints.StartHiddenRows", GElysiumHintContentTestFlags)
bool FElysiumHintContentStartHiddenTest::RunTest(const FString&)
{
	using namespace ElysiumHintContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, Tutorial, Baked))
	{
		return false;
	}
	FRig R(Stand(Tutorial, 8006, Baked));

	// OBSERVATION, not a retail claim: census § "Unrecovered" left `StartHidden` on hint rows unwalked and
	// § 1 counts 4 tutorial rows carrying the key. Read off the export, the four rows (patrol points
	// 1759, 1760, 1763, 1764, `zz4`..`zz1`) author `StartHidden 0`, not 1; no tutorial hint row authors 1.
	// What the port does with them today is pinned below.
	const int32 KeyRows[] = { 1759, 1760, 1763, 1764 };
	for (const int32 Row : KeyRows)
	{
		const FElysiumHint* Hint = R.HintAt(Row);
		if (!TestNotNull(*FString::Printf(TEXT("row %d is a hint"), Row), Hint))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("OBSERVATION (not a retail claim): row %d's StartHidden reads 0"), Row),
			Hint->bStartHidden);
		TestFalse(*FString::Printf(TEXT("OBSERVATION: row %d is not hidden after the load"), Row), Hint->IsHidden());
		TestEqual(*FString::Printf(TEXT("OBSERVATION: row %d is not Disabled after the load"), Row),
			Hint->Disabled, 0);
		TestFalse(*FString::Printf(TEXT("OBSERVATION: row %d is not dead (Kill/hide is not applied)"), Row),
			Hint->IsDead());
	}

	int32 Hidden = 0;
	int32 Disabled = 0;
	for (const int32 Row : R.F.World.HintList())
	{
		const FElysiumHint* Hint = R.HintAt(Row);
		Hidden += Hint != nullptr && Hint->IsHidden() ? 1 : 0;
		Disabled += Hint != nullptr && Hint->Disabled != 0 ? 1 : 0;
	}
	TestEqual(TEXT("OBSERVATION (census § 1: StartHintDisabled all 0): no tutorial hint is Disabled after the load"),
		Disabled, 0);
	TestEqual(TEXT("OBSERVATION: no tutorial hint is hidden after the load -- the export authors StartHidden 0 "
		"on the four rows that carry the key, and 1 on none"), Hidden, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
