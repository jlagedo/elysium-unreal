#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include <initializer_list>

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 8, wave 1: the hint list's four searches and the claim primitives.
//
// The contract is `docs/vtmb/npc-ai/shape.md` "The hint list and its four searches" (`0x102d1af0`,
// `0x102d24b0`, `0x102d2980`, `0x102d1760`; claim `0x102d1350`, release `0x102d1420`, unusable
// `0x102d14c0`). Every message names the sentence it pins. The hint list is head first and a hint
// PREPENDS on creation (`0x102d2e30`), so every layout below is written in LIST order and the
// builder authors it in reverse; each case says the resulting list order in a comment.
//
// Geometry: the NPC stands at the origin; a hint is authored `X` Source units along +X (centimetres
// in the def, `ElysiumMove::U` = 2.54 cm per unit). Every radius is 256 units, a power of two, so a
// hint authored at exactly 256 units has d^2 == r^2 whatever way the compare rounds.

static constexpr EAutomationTestFlags GElysiumHintSearchTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumHintSearchTests
{
	constexpr float Radius = 256.f;
	constexpr double CmPerUnit = ElysiumMove::U;
	constexpr int32 Miss = INDEX_NONE;
	// A type the Troika-line body accepts outright (`0x10295c20`: `0x2774` answers true after the group
	// gate) and that carries no class word (`+0x474` = 0), so only the type searches see it.
	constexpr int32 TypePlain = 10100;
	// The cover band (`0x102d0b60` row 100..101): class word 1.
	constexpr int32 TypeCover = 100;
	// `CNPC_VChangBros` (`0x1036c6a0`) answers slot 566 `return 1` for every hint, group gate included;
	// the class-mask cases stand it so a cover-band type reaches the search without the cover validator.
	const TCHAR* const ChangBros = TEXT("CNPC_VChangBros");

	// A base-line body: no Troika object behind it, so slot 566 is `0x1026a8d0` (`return 0`). The same
	// probe `ElysiumNpcKernelBaseSplitTests.cpp` stands, under a name of its own (unity builds).
	class FBaseOnlyNpc final : public FElysiumNpcBase
	{
	public:
	};

	struct FHintSpec
	{
		const TCHAR* Name = nullptr;
		FVector PositionUnits = FVector::ZeroVector;
		int32 Type = TypePlain;
		float Rating = 0.f;
		bool bStartDisabled = false;
		int32 GroupId = 0;
	};

	FHintSpec Spec(const TCHAR* Name, double XUnits, int32 Type = TypePlain, float Rating = 0.f,
		bool bStartDisabled = false)
	{
		FHintSpec Out;
		Out.Name = Name;
		Out.PositionUnits = FVector(XUnits, 0.0, 0.0);
		Out.Type = Type;
		Out.Rating = Rating;
		Out.bStartDisabled = bStartDisabled;
		return Out;
	}

	// One NPC named `npc` at the origin (`NpcClass` null = the bare Troika line) and the hints, given in
	// LIST order (head first) and authored in reverse so the prepend lands them in that order.
	FElysiumNpcWorldBuilder BuildWorld(const TCHAR* Map, uint32 Seed, const TCHAR* NpcClass,
		const TArray<FHintSpec>& ListOrder)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddNpcOfClass(TEXT("npc"), FVector::ZeroVector, NpcClass);
		for (int32 Index = ListOrder.Num() - 1; Index >= 0; --Index)
		{
			const FHintSpec& Row = ListOrder[Index];
			FElysiumEntityDef& Def = Builder.AddEntity(TEXT("info_node_hint"), Row.Name,
				Row.PositionUnits * CmPerUnit);
			Def.Keys.Add(TEXT("hinttype"), FString::FromInt(Row.Type));
			if (Row.Rating != 0.f)
			{
				Def.Keys.Add(TEXT("hint_rating"), FString::SanitizeFloat(Row.Rating));
			}
			if (Row.bStartDisabled)
			{
				Def.Keys.Add(TEXT("StartHintDisabled"), TEXT("1"));
			}
			if (Row.GroupId != 0)
			{
				Def.Keys.Add(TEXT("group_id"), FString::FromInt(Row.GroupId));
			}
		}
		return Builder;
	}

	void Fire(FElysiumEntityWorld& World, FElysiumEntity* Target, const TCHAR* Input)
	{
		World.AcceptInput(Target->Handle, FName(Input), FElysiumVariant(), FElysiumEntityHandle::Invalid(),
			FElysiumEntityHandle::Invalid());
	}

	// The world, the searching NPC and the thin wrappers every case asks through. Each wrapper writes
	// the rotating cursor first (`nullptr` = no cursor), because the cursor is a global the searches
	// both read and write.
	struct FRig
	{
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Npc = nullptr;

		explicit FRig(FElysiumNpcWorldBuilder&& Builder)
			: F(MoveTemp(Builder))
		{
			Npc = F.Npc(TEXT("npc"));
			FElysiumNpcWorldFixture::Quiet({ Npc, F.Npc(TEXT("other")) });
		}

		bool Ready() const { return Npc != nullptr; }

		FElysiumHint* Hint(const TCHAR* Name)
		{
			return FElysiumHint::Cast(F.World.FindByName(Name));
		}

		int32 Id(const TCHAR* Name)
		{
			const FElysiumHint* Found = Hint(Name);
			return Found != nullptr ? Found->Handle.Index : static_cast<int32>(INDEX_NONE);
		}

		bool Has(FAutomationTestBase& Test, std::initializer_list<const TCHAR*> Names)
		{
			bool bAll = Ready();
			for (const TCHAR* Name : Names)
			{
				if (Id(Name) == INDEX_NONE)
				{
					Test.AddError(FString::Printf(TEXT("the hint %s did not stand"), Name));
					bAll = false;
				}
			}
			return bAll;
		}

		void SetCursor(const TCHAR* Name)
		{
			F.World.SetHintCursor(Name != nullptr ? Id(Name) : static_cast<int32>(INDEX_NONE));
		}

		void Disable(const TCHAR* Name) { Hint(Name)->Disabled = 1; }
		void Enable(const TCHAR* Name) { Hint(Name)->Disabled = 0; }

		// `0x102d1af0`.
		int32 Near(const TCHAR* Cursor, int32 Type, uint8 Flags)
		{
			SetCursor(Cursor);
			return Npc->FindHintNear(Type, Flags, Radius);
		}
		// `0x102d24b0`, anchored on the NPC itself.
		int32 OfType(const TCHAR* Cursor, int32 Type, uint8 Flags)
		{
			SetCursor(Cursor);
			return Npc->FindHintOfTypeNear(Npc, Type, Flags, Radius);
		}
		// `0x102d2980`.
		int32 Mask(const TCHAR* Cursor, uint8 Flags, int32 ClassMask)
		{
			SetCursor(Cursor);
			return Npc->FindHintByClassMask(Flags, ClassMask, Radius);
		}
	};

	// `TraceRetail` answers clear for every ray: the line-of-sight arm passes.
	void AnswerAllClear(FRig& R)
	{
		R.F.Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
		{
			Out.Fraction = 1.0f;
			return true;
		};
	}
}

// -------------------------------------------------------------------------------------------------
// 1. EmptyList
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchEmptyListTest,
	"Elysium.Arm.HintSearch.EmptyList", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchEmptyListTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	FRig R(BuildWorld(TEXT("__hint_search_empty__"), 1, nullptr, TArray<FHintSpec>()));
	if (!R.Has(*this, {}))
	{
		return false;
	}
	// The cursor is a bare entity index; any value stands for "the cursor was here".
	const int32 Sentinel = R.Npc->Handle.Index;
	R.F.World.SetHintCursor(Sentinel);

	TestEqual(TEXT("0x102d1af0: an empty list answers NULL (shape.md 'An empty list answers NULL')"),
		R.Npc->FindHintNear(TypePlain, 0, Radius), Miss);
	TestEqual(TEXT("0x102d24b0: an empty list answers NULL"),
		R.Npc->FindHintOfTypeNear(R.Npc, TypePlain, 0, Radius), Miss);
	TestEqual(TEXT("0x102d2980: an empty list answers NULL"), R.Npc->FindHintByClassMask(0, 1, Radius), Miss);
	TestEqual(TEXT("0x102d2940: mask 1 on an empty list answers NULL"), R.Npc->FindHintByClassMask1(0, Radius),
		Miss);
	TestEqual(TEXT("...and leaves the cursor alone (shape.md 'leaves the cursor alone')"),
		R.F.World.HintCursor(), Sentinel);
	TestEqual(TEXT("the store's count is the list length"), R.F.World.HintCount(), 0);

	// The random pick's FIRST arm is the empty-list test (`102d1760`: `DAT_10925450 == NULL` returns 0
	// before anything else), so the cursor is left alone here too; "the cursor becomes the pick, or
	// NULL" (`102d1997`) is the arm a NON-empty list with nothing admitted reaches (RandomPick case).
	TestEqual(TEXT("0x102d1760: an empty list answers NULL"), R.Npc->FindHintRandom(TypePlain, 4, Radius),
		Miss);
	TestEqual(TEXT("...and leaves the cursor alone (102d1760: the NULL-head test returns first)"),
		R.F.World.HintCursor(), Sentinel);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 2. BaseNeverFinds
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchBaseNeverFindsTest,
	"Elysium.Arm.HintSearch.BaseNeverFinds", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchBaseNeverFindsTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: near (10100, 50 units), cover (100, 60 units). The control is a ChangBros NPC, whose
	// slot 566 accepts everything; the base-line probe stands in the SAME world.
	FRig R(BuildWorld(TEXT("__hint_search_base__"), 2, ChangBros,
		{ Spec(TEXT("near"), 50.0), Spec(TEXT("cover"), 60.0, TypeCover) }));
	if (!R.Has(*this, { TEXT("near"), TEXT("cover") }))
	{
		return false;
	}
	FBaseOnlyNpc Base;
	Base.World = &R.F.World;
	Base.Origin = R.Npc->Origin;

	TestEqual(TEXT("control: a Troika-line NPC finds the plain hint (0x102d1af0)"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("near")));
	TestEqual(TEXT("control: ...and the cover-band hint by class mask (0x102d2980)"),
		R.Mask(nullptr, 0, 1), R.Id(TEXT("cover")));

	TestEqual(TEXT("slot 566 base body 0x1026a8d0 answers 0: 0x102d1af0 finds nothing"),
		Base.FindHintNear(TypePlain, 0, Radius), Miss);
	TestEqual(TEXT("...with type 0 (any) too: only the Troika line ever finds a hint"),
		Base.FindHintNear(0, 0, Radius), Miss);
	TestEqual(TEXT("0x102d24b0 finds nothing on a base body"), Base.FindHintOfTypeNear(&Base, TypePlain, 0, Radius),
		Miss);
	TestEqual(TEXT("0x102d2980 finds nothing on a base body"), Base.FindHintByClassMask(0, 1, Radius), Miss);
	TestEqual(TEXT("0x102d2940 finds nothing on a base body"), Base.FindHintByClassMask1(0, Radius), Miss);
	TestEqual(TEXT("0x102d1760 finds nothing on a base body"), Base.FindHintRandom(TypePlain, 4, Radius), Miss);
	TestEqual(TEXT("a miss writes NULL to the cursor (shape.md 'or NULL on a miss')"), R.F.World.HintCursor(),
		Miss);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 3. CursorWalk1af0
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchCursorWalk1af0Test,
	"Elysium.Arm.HintSearch.CursorWalk1af0", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchCursorWalk1af0Test::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: a, b, c (all admitted, flags 0 stops at the first admitted).
	FRig R(BuildWorld(TEXT("__hint_search_1af0__"), 3, nullptr,
		{ Spec(TEXT("a"), 50.0), Spec(TEXT("b"), 100.0), Spec(TEXT("c"), 150.0) }));
	if (!R.Has(*this, { TEXT("a"), TEXT("b"), TEXT("c") }))
	{
		return false;
	}
	auto Ask = [this, &R](const TCHAR* What, const TCHAR* Cursor, const TCHAR* Expect)
	{
		const int32 Want = Expect != nullptr ? R.Id(Expect) : Miss;
		TestEqual(*FString::Printf(TEXT("0x102d1af0: %s"), What), R.Near(Cursor, TypePlain, 0), Want);
		TestEqual(*FString::Printf(TEXT("...and the cursor is written to the result (%s)"), What),
			R.F.World.HintCursor(), Want);
	};

	Ask(TEXT("no cursor starts at the head ('or at the head when the cursor ... is NULL')"), nullptr, TEXT("a"));
	Ask(TEXT("cursor b starts at cursor->next: the first admitted AFTER the cursor is c"), TEXT("b"),
		TEXT("c"));
	Ask(TEXT("cursor c has no next: the walk starts at the head"), TEXT("c"), TEXT("a"));

	R.Disable(TEXT("c"));
	Ask(TEXT("cursor b, c refused: the walk wraps to the head (wraps unconditionally)"), TEXT("b"), TEXT("a"));

	R.Disable(TEXT("a"));
	Ask(TEXT("cursor b only: the cursor's own node is examined LAST, and is found"), TEXT("b"), TEXT("b"));

	R.Disable(TEXT("b"));
	Ask(TEXT("nothing admitted: a miss answers NULL"), TEXT("b"), nullptr);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 4. CursorWalk24b0And2980
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchCursorWalk24b0And2980Test,
	"Elysium.Arm.HintSearch.CursorWalk24b0And2980", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchCursorWalk24b0And2980Test::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: a, b, c, all cover-band (type 100, class word 1) so one layout serves both searches.
	FRig R(BuildWorld(TEXT("__hint_search_24b0__"), 4, ChangBros,
		{ Spec(TEXT("a"), 50.0, TypeCover), Spec(TEXT("b"), 100.0, TypeCover), Spec(TEXT("c"), 150.0, TypeCover) }));
	if (!R.Has(*this, { TEXT("a"), TEXT("b"), TEXT("c") }))
	{
		return false;
	}
	auto Ask = [this, &R](const TCHAR* What, const TCHAR* Cursor, const TCHAR* Expect)
	{
		const int32 Want = Expect != nullptr ? R.Id(Expect) : Miss;
		TestEqual(*FString::Printf(TEXT("0x102d24b0: %s"), What), R.OfType(Cursor, TypeCover, 0), Want);
		TestEqual(*FString::Printf(TEXT("0x102d24b0 writes the cursor (%s)"), What), R.F.World.HintCursor(), Want);
		TestEqual(*FString::Printf(TEXT("0x102d2980: %s"), What), R.Mask(Cursor, 0, 1), Want);
		TestEqual(*FString::Printf(TEXT("0x102d2980 writes the cursor (%s)"), What), R.F.World.HintCursor(), Want);
	};

	Ask(TEXT("a NULL cursor walks head to tail: the first admitted is a"), nullptr, TEXT("a"));
	Ask(TEXT("cursor b starts at cursor->next: c"), TEXT("b"), TEXT("c"));
	Ask(TEXT("cursor c has no next: the walk starts at the head and stops on reaching c: a"), TEXT("c"),
		TEXT("a"));

	R.Disable(TEXT("c"));
	Ask(TEXT("cursor b, c refused: wraps to the head while the cursor is set"), TEXT("b"), TEXT("a"));

	R.Disable(TEXT("a"));
	Ask(TEXT("the cursor's own node is NEVER examined: b alone is admissible, cursor b, so a miss"),
		TEXT("b"), nullptr);
	Ask(TEXT("...but with a NULL cursor every node is examined once: b is found"), nullptr, TEXT("b"));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 5. AdmissionOrder
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchAdmissionOrderTest,
	"Elysium.Arm.HintSearch.AdmissionOrder", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchAdmissionOrderTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: disabled, cooling, owned, wrongtype, boundary, admitted. Only `admitted` passes every
	// arm; `boundary` sits at exactly 256 units, d^2 == r^2.
	FElysiumNpcWorldBuilder Builder = BuildWorld(TEXT("__hint_search_admission__"), 5, nullptr,
		{ Spec(TEXT("disabled"), 50.0, TypePlain, 0.f, true), Spec(TEXT("cooling"), 60.0),
			Spec(TEXT("owned"), 70.0), Spec(TEXT("wrongtype"), 80.0, 10101), Spec(TEXT("boundary"), 256.0),
			Spec(TEXT("admitted"), 100.0) });
	Builder.AddNpcOfClass(TEXT("other"), FVector(50000.0, 0.0, 0.0), nullptr);
	FRig R(MoveTemp(Builder));
	FElysiumNpc* Other = R.F.Npc(TEXT("other"));
	if (!R.Has(*this, { TEXT("disabled"), TEXT("cooling"), TEXT("owned"), TEXT("wrongtype"), TEXT("boundary"),
			TEXT("admitted") })
		|| !TestNotNull(TEXT("the second NPC stands"), Other))
	{
		return false;
	}
	const float Now = static_cast<float>(R.F.World.NowSeconds());
	R.Hint(TEXT("cooling"))->NextUseTime = Now + 10.f;
	R.Hint(TEXT("owned"))->HintOwner = Other->Handle;

	TestEqual(TEXT("only the node that passes every arm is admitted"), R.Near(nullptr, TypePlain, 0),
		R.Id(TEXT("admitted")));

	// With `admitted` out, the five refusals below all still stand, so nothing is found.
	R.Disable(TEXT("admitted"));
	TestEqual(TEXT("0x102d14c0 m_iDisabled, curtime < NextUseTime, a live owner, the type compare and "
		"d^2 STRICTLY under r^2 (an equal d^2 is OUT) each refuse"), R.Near(nullptr, TypePlain, 0), Miss);

	R.Hint(TEXT("cooling"))->NextUseTime = Now;
	TestEqual(TEXT("0x102d14c0: curtime < NextUseTime is strict, so an equal time is USABLE"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("cooling")));
	R.Hint(TEXT("cooling"))->NextUseTime = Now + 10.f;

	R.Hint(TEXT("owned"))->HintOwner = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("0x102d14c0: with no owner the node is usable"), R.Near(nullptr, TypePlain, 0),
		R.Id(TEXT("owned")));
	R.Hint(TEXT("owned"))->HintOwner = Other->Handle;

	R.Enable(TEXT("disabled"));
	TestEqual(TEXT("0x102d14c0: m_iDisabled clear makes it usable"), R.Near(nullptr, TypePlain, 0),
		R.Id(TEXT("disabled")));
	R.Disable(TEXT("disabled"));

	R.Hint(TEXT("boundary"))->Origin = FVector(255.5 * CmPerUnit, 0.0, 0.0);
	TestEqual(TEXT("a node just inside the radius is admitted (d^2 < r^2)"), R.Near(nullptr, TypePlain, 0),
		R.Id(TEXT("boundary")));
	R.Hint(TEXT("boundary"))->Origin = FVector(256.0 * CmPerUnit, 0.0, 0.0);

	TestEqual(TEXT("0x102d1af0: type 0 admits any type, but slot 566 refuses 10101 on the bare Troika line"),
		R.Near(nullptr, 0, 0), Miss);

	Other->Kill();
	TestEqual(TEXT("0x102d14c0: a stale owner is not a live entity, so the owned node is usable"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("owned")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 5b. GroupGate (slot 566's own gate, `0x10295c20`)
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchGroupGateTest,
	"Elysium.Arm.HintSearch.GroupGate", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchGroupGateTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: g1 (group 1), g2 (group 2), gall (no group_id = every group).
	TArray<FHintSpec> Layout = { Spec(TEXT("g1"), 50.0), Spec(TEXT("g2"), 60.0), Spec(TEXT("gall"), 70.0) };
	Layout[0].GroupId = 1;
	Layout[1].GroupId = 2;
	FRig R(BuildWorld(TEXT("__hint_search_groups__"), 6, nullptr, Layout));
	if (!R.Has(*this, { TEXT("g1"), TEXT("g2"), TEXT("gall") }))
	{
		return false;
	}
	R.Npc->ScheduleHost.HintGroupMask = 2u;
	TestEqual(TEXT("0x10295c20: hint->m_iGroupID & npc->m_iHintGroups must be non-zero: group 1 is refused, "
		"group 2 found"), R.Near(nullptr, TypePlain, 0), R.Id(TEXT("g2")));
	R.Disable(TEXT("g2"));
	TestEqual(TEXT("Spawn turns an authored group of none into 0xFFFFFFFF, which every mask admits"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("gall")));
	R.Npc->ScheduleHost.HintGroupMask = 0u;
	TestEqual(TEXT("a mask of 0 (a junk hint_groups string, 0x102989e0) matches no hint"),
		R.Near(nullptr, TypePlain, 0), Miss);
	R.Npc->ScheduleHost.HintGroupMask = 1u;
	TestEqual(TEXT("group 1's bit admits g1"), R.Near(nullptr, TypePlain, 0), R.Id(TEXT("g1")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 6. Scoring
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchScoringTest,
	"Elysium.Arm.HintSearch.Scoring", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchScoringTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: l0 (150 units, rating 1), l1 (50, rating 10), l2 (100, rating 1). Nearest d^2 is l1
	// (the middle); the lowest d x rating is l2 (the LAST): 150, 500, 100. Walk order alone gives l0.
	FRig R(BuildWorld(TEXT("__hint_search_scoring__"), 7, nullptr,
		{ Spec(TEXT("l0"), 150.0, TypePlain, 1.f), Spec(TEXT("l1"), 50.0, TypePlain, 10.f),
			Spec(TEXT("l2"), 100.0, TypePlain, 1.f) }));
	if (!R.Has(*this, { TEXT("l0"), TEXT("l1"), TEXT("l2") }))
	{
		return false;
	}
	R.SetCursor(nullptr);
	TestEqual(TEXT("flags 0 stops at the first admitted, whatever the distance (0x102d1af0, 'stops at the "
		"first admitted node unless bit 1 or bit 3 is set')"), R.Near(nullptr, TypePlain, 0), R.Id(TEXT("l0")));

	float Score = 0.f;
	R.SetCursor(nullptr);
	TestEqual(TEXT("bit 1 keeps the best: the nearest d^2 wins over walk order"),
		R.Npc->FindHintNear(TypePlain, 2, Radius, nullptr, &Score), R.Id(TEXT("l1")));
	TestNearlyEqual(TEXT("...and *outScore is the winner's d^2 (50 x 50)"), Score, 2500.f, 1.f);

	R.SetCursor(nullptr);
	TestEqual(TEXT("bit 3 scores sqrt(d^2) x m_flHintRating: l2 (100 x 1) beats the nearer l1 (50 x 10)"),
		R.Npc->FindHintNear(TypePlain, 8, Radius, nullptr, &Score), R.Id(TEXT("l2")));
	TestNearlyEqual(TEXT("...and *outScore is that product"), Score, 100.f, 0.5f);

	TestEqual(TEXT("bit 3 takes precedence over bit 1 ('bit 3 scores ..., else bit 1')"),
		R.Near(nullptr, TypePlain, 10), R.Id(TEXT("l2")));

	// The origin argument moves the DISTANCE test: from 200 units behind the NPC only l1 (250 away) is
	// inside 256; from the NPC itself the first admitted is l0.
	const FVector Behind(-200.0 * CmPerUnit, 0.0, 0.0);
	R.SetCursor(nullptr);
	TestEqual(TEXT("0x102d1af0's origin argument replaces the NPC's origin for the radius test"),
		R.Npc->FindHintNear(TypePlain, 0, Radius, &Behind, nullptr), R.Id(TEXT("l1")));

	// A tie: t0 (100 along +X) and t1 (100 along +Y) are the same distance and the same score.
	TArray<FHintSpec> Ties = { Spec(TEXT("t0"), 100.0, TypePlain, 1.f), Spec(TEXT("t1"), 0.0, TypePlain, 1.f) };
	Ties[1].PositionUnits = FVector(0.0, 100.0, 0.0);
	FRig T(BuildWorld(TEXT("__hint_search_tie__"), 7, nullptr, Ties));
	if (!T.Has(*this, { TEXT("t0"), TEXT("t1") }))
	{
		return false;
	}
	TestEqual(TEXT("an equal d^2 REPLACES: the later node in walk order (t1) wins a tie"),
		T.Near(nullptr, TypePlain, 2), T.Id(TEXT("t1")));
	TestEqual(TEXT("an equal bit-3 score replaces too"), T.Near(nullptr, TypePlain, 8), T.Id(TEXT("t1")));
	TestEqual(TEXT("with the cursor on t0 the walk is t1 then t0, so the later node is t0"),
		T.Near(TEXT("t0"), TypePlain, 2), T.Id(TEXT("t0")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 7. MaskSearch
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchMaskSearchTest,
	"Elysium.Arm.HintSearch.MaskSearch", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchMaskSearchTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order and class word (`0x102d0b60`): h100 (1, 30 units), h101 (1, 10), h10200 (1, 20),
	// h10300 (4, 40), h10301 (8, 50), h10400 (0x10, 60), h10100 (0, 70).
	FRig R(BuildWorld(TEXT("__hint_search_mask__"), 8, ChangBros,
		{ Spec(TEXT("h100"), 30.0, 100), Spec(TEXT("h101"), 10.0, 101), Spec(TEXT("h10200"), 20.0, 10200),
			Spec(TEXT("h10300"), 40.0, 10300), Spec(TEXT("h10301"), 50.0, 10301),
			Spec(TEXT("h10400"), 60.0, 10400), Spec(TEXT("h10100"), 70.0, 10100) }));
	if (!R.Has(*this, { TEXT("h100"), TEXT("h101"), TEXT("h10200"), TEXT("h10300"), TEXT("h10301"),
			TEXT("h10400"), TEXT("h10100") }))
	{
		return false;
	}
	AnswerAllClear(R);

	TestEqual(TEXT("0x102d2980 mask 8 admits only type 0x283d (10301)"), R.Mask(nullptr, 0, 8),
		R.Id(TEXT("h10301")));
	TestEqual(TEXT("mask 4 admits only 0x283c (10300)"), R.Mask(nullptr, 0, 4), R.Id(TEXT("h10300")));
	TestEqual(TEXT("mask 0x10 admits only 0x28a0 (10400)"), R.Mask(nullptr, 0, 0x10), R.Id(TEXT("h10400")));
	TestEqual(TEXT("a zero mask admits nothing ('so a zero mask admits nothing')"), R.Mask(nullptr, 0, 0),
		Miss);
	TestEqual(TEXT("a mask no type carries admits nothing (10100 has class word 0)"), R.Mask(nullptr, 0, 0x20),
		Miss);
	TestEqual(TEXT("mask 0x1f is the union: the first admitted in list order"), R.Mask(nullptr, 0, 0x1f),
		R.Id(TEXT("h100")));

	float Score = 0.f;
	R.SetCursor(nullptr);
	TestEqual(TEXT("0x102d2980 stops at the first admitted when bit 0 is clear"),
		R.Npc->FindHintByClassMask(0, 1, Radius, nullptr, &Score), R.Id(TEXT("h100")));
	TestEqual(TEXT("...and leaves *outScore at FLT_MAX"), Score, MAX_FLT);

	// `0x102d2980`'s stop rule is bit 0, not the scoring bits (`if ((param_2 & 1) == 0) break;` runs
	// BEFORE `local_bc = score`): with bit 1 alone the first admitted still wins and the best-so-far
	// stays FLT_MAX, so the score is written only when bit 0 keeps the walk going.
	R.SetCursor(nullptr);
	Score = 0.f;
	TestEqual(TEXT("bit 1 without bit 0 still stops at the first admitted (h100, 30 units): 0x102d2980 "
		"breaks on bit 0 clear before it keeps a score"),
		R.Npc->FindHintByClassMask(2, 1, Radius, nullptr, &Score), R.Id(TEXT("h100")));
	TestEqual(TEXT("...and *outScore is FLT_MAX, the best-so-far it never updated"), Score, MAX_FLT);
	R.SetCursor(nullptr);
	TestEqual(TEXT("bits 0|1 keep the best d^2 over the whole walk: h101 (10 units), neither first nor last"),
		R.Npc->FindHintByClassMask(3, 1, Radius, nullptr, &Score), R.Id(TEXT("h101")));
	TestNearlyEqual(TEXT("...and *outScore is its d^2 (10 x 10)"), Score, 100.f, 0.5f);

	TestEqual(TEXT("bit 0 set with no scoring bit: the LAST admitted wins (0x102d2980 stops at the first "
		"iff bit 0 is CLEAR)"), R.Mask(nullptr, 1, 1), R.Id(TEXT("h10200")));

	TestEqual(TEXT("0x102d2940 is 0x102d2980 with mask 1: the first of the cover band"),
		(R.SetCursor(nullptr), R.Npc->FindHintByClassMask1(0, Radius)), R.Id(TEXT("h100")));
	R.Disable(TEXT("h100"));
	TestEqual(TEXT("class word 1 covers type 101"), (R.SetCursor(nullptr), R.Npc->FindHintByClassMask1(0, Radius)),
		R.Id(TEXT("h101")));
	R.Disable(TEXT("h101"));
	TestEqual(TEXT("...and type 0x27d8 (10200)"), (R.SetCursor(nullptr), R.Npc->FindHintByClassMask1(0, Radius)),
		R.Id(TEXT("h10200")));
	R.Disable(TEXT("h10200"));
	TestEqual(TEXT("...and no other type: 10300, 10301, 10400 and 10100 carry other class words"),
		(R.SetCursor(nullptr), R.Npc->FindHintByClassMask1(0, Radius)), Miss);
	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

// -------------------------------------------------------------------------------------------------
// 8. LineOfSightBit0
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchLineOfSightTest,
	"Elysium.Arm.HintSearch.LineOfSightBit0", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchLineOfSightTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: off (disabled, 20 units), blocked (50), clear (100). The disabled node is refused at
	// admission arm 1, so it must cost no trace.
	FRig R(BuildWorld(TEXT("__hint_search_los__"), 9, nullptr,
		{ Spec(TEXT("off"), 20.0, TypePlain, 0.f, true), Spec(TEXT("blocked"), 50.0), Spec(TEXT("clear"), 100.0) }));
	if (!R.Has(*this, { TEXT("off"), TEXT("blocked"), TEXT("clear") }))
	{
		return false;
	}
	const FVector Eye = R.Npc->DefaultEyeOffsetCm();
	const FVector BlockedEnd = R.Hint(TEXT("blocked"))->Origin + Eye;
	const FVector ClearEnd = R.Hint(TEXT("clear"))->Origin + Eye;
	TArray<FElysiumRetailTrace> Seen;
	R.F.Services.TraceRetailQuery = [&Seen, BlockedEnd](const FElysiumRetailTrace& Trace,
		FElysiumRetailTraceResult& Out)
	{
		Seen.Add(Trace);
		Out.Fraction = Trace.EndCm.Equals(BlockedEnd, 0.01) ? 0.4f : 1.0f;
		return true;
	};

	TestEqual(TEXT("flags bit 0: the node whose ray stops short is skipped, the clear one is found"),
		R.Near(nullptr, TypePlain, 1), R.Id(TEXT("clear")));
	if (!TestEqual(TEXT("...after exactly two traces (a refused node costs none; step 6 runs last)"),
			Seen.Num(), 2))
	{
		R.F.Services.TraceRetailQuery = nullptr;
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FElysiumRetailTrace& Trace = Seen[Index];
		TestEqual(TEXT("the trace mask is 0x2400b (shape.md step 6)"), Trace.RetailMask, 0x2400b);
		TestTrue(TEXT("the ray starts at the NPC's eye (slot 193)"),
			Trace.StartCm.Equals(R.Npc->EyePosition(), 0.01));
		TestTrue(TEXT("CTraceFilterSimple skips the NPC itself"), Trace.Ignore.Contains(R.Npc->Handle));
		TestTrue(TEXT("...and is the Simple filter"), Trace.Filter == EElysiumRetailTraceFilter::Simple);
	}
	TestTrue(TEXT("the first ray ends at the hint's origin plus that same entity's view offset"),
		Seen[0].EndCm.Equals(BlockedEnd, 0.01));
	TestTrue(TEXT("...and the second at the next hint's"), Seen[1].EndCm.Equals(ClearEnd, 0.01));

	Seen.Reset();
	TestEqual(TEXT("with bit 0 clear no trace is cast: the first admitted node is the blocked one"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("blocked")));

	R.F.Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
	{
		Out.Fraction = 0.9f;
		return true;
	};
	TestEqual(TEXT("admitted only on fraction == 1.0: every ray short answers NULL"),
		R.Near(nullptr, TypePlain, 1), Miss);
	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

// -------------------------------------------------------------------------------------------------
// 9. RandomPick
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchRandomPickTest,
	"Elysium.Arm.HintSearch.RandomPick", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchRandomPickTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: r0, off (disabled), r1, r2, r3. The admitted set in walk order is r0, r1, r2, r3.
	// Cover-band type so `0x102d2980` sees them too.
	const TArray<FHintSpec> Layout = { Spec(TEXT("r0"), 30.0, TypeCover), Spec(TEXT("off"), 20.0, TypeCover, 0.f, true),
		Spec(TEXT("r1"), 40.0, TypeCover), Spec(TEXT("r2"), 50.0, TypeCover), Spec(TEXT("r3"), 60.0, TypeCover) };
	const TCHAR* const Admitted[] = { TEXT("r0"), TEXT("r1"), TEXT("r2"), TEXT("r3") };

	TArray<FString> Picks;
	for (int32 Run = 0; Run < 2; ++Run)
	{
		FRig R(BuildWorld(TEXT("__hint_search_random__"), 909, ChangBros, Layout));
		if (!R.Has(*this, { TEXT("r0"), TEXT("off"), TEXT("r1"), TEXT("r2"), TEXT("r3") }))
		{
			return false;
		}
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		FRandomStream Control(Stream.GetCurrentSeed());

		// Bit 2 on 0x102d1af0, with the cursor parked on r2: the pick ignores the cursor, walks head to
		// tail and draws ONE RandomInt(0, count-1) over the admitted set.
		const int32 First = R.Near(TEXT("r2"), TypeCover, 4);
		const int32 FirstWant = R.Id(Admitted[Control.RandRange(0, 3)]);
		TestEqual(TEXT("0x102d1af0 bit 2 diverts to 0x102d1760: the pick is Admitted[RandRange(0, count-1)]"),
			First, FirstWant);
		TestEqual(TEXT("...the cursor becomes the pick"), R.F.World.HintCursor(), First);
		TestEqual(TEXT("...after exactly one draw on the NpcSchedule stream"), Stream.GetCurrentSeed(),
			Control.GetCurrentSeed());

		const int32 Second = R.OfType(TEXT("r0"), TypeCover, 4);
		const int32 SecondWant = R.Id(Admitted[Control.RandRange(0, 3)]);
		TestEqual(TEXT("0x102d24b0 bit 2 diverts to the same pick (anchor and cursor dropped)"), Second,
			SecondWant);
		TestEqual(TEXT("...the cursor becomes the pick"), R.F.World.HintCursor(), Second);
		TestEqual(TEXT("...after one more draw"), Stream.GetCurrentSeed(), Control.GetCurrentSeed());

		if (!R.F.World.Entities().IsValidIndex(First) || !R.F.World.Entities().IsValidIndex(Second))
		{
			AddError(TEXT("a pick answered no entity index"));
			return false;
		}
		const FElysiumHint* FirstHint = FElysiumHint::Cast(R.F.World.Entities()[First].Get());
		const FElysiumHint* SecondHint = FElysiumHint::Cast(R.F.World.Entities()[Second].Get());
		if (FirstHint == nullptr || SecondHint == nullptr)
		{
			AddError(TEXT("a pick did not name a hint"));
			return false;
		}
		Picks.Add(FirstHint->Def->TargetName + TEXT("/") + SecondHint->Def->TargetName);

		if (Run == 0)
		{
			// `0x102d2980` is NOT diverted by bit 2: bit 0 clear stops at the first admitted, no draw.
			const int32 Before = Stream.GetCurrentSeed();
			TestEqual(TEXT("0x102d2980 with bit 2 is unaffected: the first admitted"),
				R.Mask(nullptr, 4, 1), R.Id(TEXT("r0")));
			TestEqual(TEXT("...and draws nothing"), Stream.GetCurrentSeed(), Before);

			// Nothing admitted: no draw, and the cursor becomes NULL.
			for (const TCHAR* Name : Admitted)
			{
				R.Disable(Name);
			}
			const int32 Untouched = Stream.GetCurrentSeed();
			TestEqual(TEXT("0x102d1760 with zero admitted answers NULL"), R.Near(TEXT("r1"), TypeCover, 4), Miss);
			TestEqual(TEXT("...and the cursor becomes NULL"), R.F.World.HintCursor(), Miss);
			TestEqual(TEXT("...and draws only when count >= 1: the stream is untouched"),
				Stream.GetCurrentSeed(), Untouched);
			TestEqual(TEXT("0x102d24b0 with zero admitted draws nothing either"),
				R.OfType(nullptr, TypeCover, 4), Miss);
			TestEqual(TEXT("...the stream is still untouched"), Stream.GetCurrentSeed(), Untouched);
		}
	}
	if (Picks.Num() == 2)
	{
		TestEqual(TEXT("the same seed and layout give the same picks"), Picks[0], Picks[1]);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// 10. ClaimReleaseOwner
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchClaimReleaseOwnerTest,
	"Elysium.Arm.HintSearch.ClaimReleaseOwner", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchClaimReleaseOwnerTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// One hint; A is `npc`, B is `other`.
	FElysiumNpcWorldBuilder Builder = BuildWorld(TEXT("__hint_search_claim__"), 10, nullptr,
		{ Spec(TEXT("h"), 10.0) });
	Builder.AddNpcOfClass(TEXT("other"), FVector(50000.0, 0.0, 0.0), nullptr);
	FRig R(MoveTemp(Builder));
	FElysiumNpc* A = R.Npc;
	FElysiumNpc* B = R.F.Npc(TEXT("other"));
	if (!R.Has(*this, { TEXT("h") }) || !TestNotNull(TEXT("the second NPC stands"), B))
	{
		return false;
	}
	FElysiumHint* Hint = R.Hint(TEXT("h"));
	const int32 H = Hint->Handle.Index;

	TestFalse(TEXT("0x102d2e30: a new hint's owner is invalid"), Hint->HintOwner.IsSet());
	TestTrue(TEXT("0x102d1540 else arm: a free hint is available to A"), A->IsHintAvailableToMe(H));
	TestTrue(TEXT("...and to B"), B->IsHintAvailableToMe(H));

	TestTrue(TEXT("0x102d1350: a claim on a hint with no owner succeeds"), A->ClaimHint(H));
	TestTrue(TEXT("...writing m_hHintOwner = the requester's handle"), Hint->HintOwner == A->Handle);
	TestTrue(TEXT("0x102d1450: A owns it"), A->OwnsHint(H));
	TestFalse(TEXT("...and B does not"), B->OwnsHint(H));

	TestFalse(TEXT("0x102d1350: a claim while another live entity owns the hint is refused"), B->ClaimHint(H));
	TestTrue(TEXT("...and a refusal writes nothing"), Hint->HintOwner == A->Handle);
	TestTrue(TEXT("0x102d1350: the requester itself succeeds (a re-claim)"), A->ClaimHint(H));
	TestTrue(TEXT("...and keeps the owner"), Hint->HintOwner == A->Handle);

	TestTrue(TEXT("0x102d1540 arm 1: the owner is me, so available"), A->IsHintAvailableToMe(H));
	TestFalse(TEXT("0x102d1540 arm 3: a live owner that is not me, so unavailable"), B->IsHintAvailableToMe(H));
	Hint->NextUseTime = 100.f;
	TestTrue(TEXT("0x102d1540: the owner-is-me arm precedes the cooling arm"), A->IsHintAvailableToMe(H));
	TestFalse(TEXT("0x102d1540 arm 2: curtime < NextUseTime, unavailable to B"), B->IsHintAvailableToMe(H));
	Hint->HintOwner = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("...and with no owner the cooling arm still refuses A"), A->IsHintAvailableToMe(H));
	TestFalse(TEXT("...and B"), B->IsHintAvailableToMe(H));
	Hint->NextUseTime = 0.f;
	TestTrue(TEXT("0x102d1540 else arm: no owner, no cooling, available"), B->IsHintAvailableToMe(H));

	// Release is two stores with no owner gate: a NON-owner's release still writes them.
	TestTrue(TEXT("(A claims again)"), A->ClaimHint(H));
	const float Now = static_cast<float>(R.F.World.NowSeconds());
	B->ReleaseHintNode(H, 0.5f);
	TestFalse(TEXT("0x102d1420 has no owner gate: it writes the owner invalid for anyone"),
		Hint->HintOwner.IsSet());
	TestNearlyEqual(TEXT("...and NextUseTime = curtime + delay"), Hint->NextUseTime, Now + 0.5f, 0.001f);
	TestTrue(TEXT("(A claims again)"), A->ClaimHint(H));
	TestTrue(TEXT("0x102d1350 ignores the cooling stamp: a claim on a cooling, unowned hint succeeds"),
		Hint->HintOwner == A->Handle);

	// A dead owner is a stale handle. Killing the NPC may run its own teardown, so the stale state the
	// claim reads is stated again explicitly.
	A->Kill();
	Hint->HintOwner = A->Handle;
	Hint->NextUseTime = 0.f;
	TestTrue(TEXT("0x102d1540: a stale owner is not a live entity, so the hint is available to B"),
		B->IsHintAvailableToMe(H));
	TestTrue(TEXT("0x102d1350: a claim over a stale owner succeeds"), B->ClaimHint(H));
	TestTrue(TEXT("...and writes the new owner"), Hint->HintOwner == B->Handle);
	TestTrue(TEXT("0x102d1450: B now owns it"), B->OwnsHint(H));

	const float ReleasedAt = static_cast<float>(R.F.World.NowSeconds());
	B->ReleaseHintNode(H, 5.f);
	TestFalse(TEXT("0x102d1420: owner invalid"), Hint->HintOwner.IsSet());
	TestNearlyEqual(TEXT("...NextUseTime = curtime + 5"), Hint->NextUseTime, ReleasedAt + 5.f, 0.001f);
	TestFalse(TEXT("0x102d1540 arm 2: cooling, unavailable"), B->IsHintAvailableToMe(H));
	R.F.Advance(6.0);
	TestTrue(TEXT("0x102d1540: available again once curtime passes NextUseTime"), B->IsHintAvailableToMe(H));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 11. DisableHintHidesFromSearch
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchDisableHintTest,
	"Elysium.Arm.HintSearch.DisableHintHidesFromSearch", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchDisableHintTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	// List order: k0 (authored StartHintDisabled), k1.
	FRig R(BuildWorld(TEXT("__hint_search_disable__"), 11, nullptr,
		{ Spec(TEXT("k0"), 50.0, TypePlain, 0.f, true), Spec(TEXT("k1"), 100.0) }));
	if (!R.Has(*this, { TEXT("k0"), TEXT("k1") }))
	{
		return false;
	}
	FElysiumHint* K0 = R.Hint(TEXT("k0"));
	const int32 Count = R.F.World.HintList().Num();

	TestEqual(TEXT("0x102d14c0 m_iDisabled: a StartHintDisabled hint is skipped"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("k1")));
	Fire(R.F.World, K0, TEXT("EnableHint"));
	TestEqual(TEXT("EnableHint (0x102d09f0) clears m_iDisabled: the search finds it again"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("k0")));
	Fire(R.F.World, K0, TEXT("DisableHint"));
	TestEqual(TEXT("DisableHint (0x102d0a20) sets m_iDisabled: the search skips it"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("k1")));
	Fire(R.F.World, K0, TEXT("EnableHint"));
	TestEqual(TEXT("DisableHint also hides the node, and a hidden entity swallows EnableHint "
		"(FUN_100abc90): still skipped"), R.Near(nullptr, TypePlain, 0), R.Id(TEXT("k1")));
	Fire(R.F.World, K0, TEXT("ScriptUnhide"));
	TestEqual(TEXT("ScriptUnhide (slot 78, 0x102d0890) passes the gate and clears m_iDisabled"),
		R.Near(nullptr, TypePlain, 0), R.Id(TEXT("k0")));

	Fire(R.F.World, K0, TEXT("Kill"));
	TestFalse(TEXT("slot 119 0x102d08c0 is ScriptHide: Kill never kills a hint"), K0->IsDead());
	TestEqual(TEXT("...the killed hint is skipped by the search"), R.Near(nullptr, TypePlain, 0),
		R.Id(TEXT("k1")));
	TestEqual(TEXT("...and stays on the list, count unchanged (shape.md 'A killed hint stays on the list')"),
		R.F.World.HintList().Num(), Count);
	TestEqual(TEXT("...and the store's count word agrees"), R.F.World.HintCount(), Count);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 12. SaveRoundTripClaim
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintSearchSaveRoundTripClaimTest,
	"Elysium.Arm.HintSearch.SaveRoundTripClaim", GElysiumHintSearchTestFlags)
bool FElysiumHintSearchSaveRoundTripClaimTest::RunTest(const FString&)
{
	using namespace ElysiumHintSearchTests;
	const TArray<FHintSpec> Layout = { Spec(TEXT("a"), 50.0), Spec(TEXT("b"), 100.0) };
	FRig From(BuildWorld(TEXT("__hint_search_save__"), 12, nullptr, Layout));
	FRig To(BuildWorld(TEXT("__hint_search_save__"), 12, nullptr, Layout));
	if (!From.Has(*this, { TEXT("a"), TEXT("b") }) || !To.Has(*this, { TEXT("a"), TEXT("b") }))
	{
		return false;
	}
	// `a` is claimed by the NPC; `b` is released with a five-second delay (owner invalid, cooling).
	TestTrue(TEXT("(the claim lands)"), From.Npc->ClaimHint(From.Id(TEXT("a"))));
	From.Npc->ReleaseHintNode(From.Id(TEXT("b")), 5.f);
	const float SavedNextUse = From.Hint(TEXT("b"))->NextUseTime;
	TestTrue(TEXT("(the release stamped a future time)"), SavedNextUse > 0.f);
	// The cursor is a global, not a saved word: park it in the source, and leave the destination's alone.
	From.F.World.SetHintCursor(From.Id(TEXT("a")));

	TestFalse(TEXT("the destination starts with no owner"), To.Hint(TEXT("a"))->HintOwner.IsSet());
	ElysiumRoundTripSnapshot(From.F.World, To.F.World);

	FElysiumNpc* RestoredNpc = To.F.Npc(TEXT("npc"));
	const FElysiumHint* A = To.Hint(TEXT("a"));
	const FElysiumHint* B = To.Hint(TEXT("b"));
	if (!TestNotNull(TEXT("the restored NPC stands"), RestoredNpc) || !TestNotNull(TEXT("hint a"), A)
		|| !TestNotNull(TEXT("hint b"), B))
	{
		return false;
	}
	TestTrue(TEXT("m_hHintOwner (+0x5e0) is SAVE: the restored hint is owned by the same NPC"),
		A->HintOwner == RestoredNpc->Handle);
	TestTrue(TEXT("...and the restored NPC owns it (0x102d1450)"), RestoredNpc->OwnsHint(To.Id(TEXT("a"))));
	TestFalse(TEXT("a released hint restores with no owner"), B->HintOwner.IsSet());
	TestNearlyEqual(TEXT("m_flNextUseTime (+0x5ec) is SAVE: the release's stamp survives"), B->NextUseTime,
		SavedNextUse, 0.001f);
	TestEqual(TEXT("DAT_10925454 is a global, not saved: the restored world's cursor is NULL"),
		To.F.World.HintCursor(), Miss);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
