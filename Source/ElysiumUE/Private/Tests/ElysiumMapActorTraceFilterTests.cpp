#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumWorldServices.h"
#include "Map/ElysiumRetailMaskRecipe.h"

// 0019 story 6: `StandardFilterRules 0x101d3080`'s two entity arms the map actor's `TraceRetail`
// carries per hit -- solid flag `0x20` (`101d30b6`) and render mode without WINDOW (`101d3112`) --
// and the re-trace loop that passes a refused entity. The world is scripted: the tutorial geometry
// fixture traces the collision payload with no map actor and no entity world, so no hit there names
// an entity and neither arm can be reached through it.

static constexpr EAutomationTestFlags GElysiumMapActorTraceFilterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr int32 TraceFilterSightMask = 0x2804091;   // FVisible: no WINDOW
	constexpr int32 TraceFilterSolidMask = 0x200400b;   // MASK_SOLID-like: WINDOW `0x2` set

	// One scripted line: the brush entity `Near` at 0.3, the static world at 0.7. `Near` is refused
	// by the arms when `bRefuse`; the ignore list decides whether the next pass meets it.
	struct FTraceFilterWorld
	{
		FElysiumEntityHandle Near = FElysiumEntityHandle(7, 1);
		int32 Passes = 0;

		bool Trace(const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out)
		{
			++Passes;
			Out = FElysiumRetailTraceResult();
			if (!Request.Ignore.Contains(Near))
			{
				Out.Fraction = 0.3f;
				Out.HitEntity = Near;
				return true;
			}
			Out.Fraction = 0.7f;
			return true;
		}
	};

	float TraceFilterRun(FTraceFilterWorld& World, int32 Mask, int32 RenderMode, uint32 SolidFlags)
	{
		FElysiumRetailTrace Request;
		Request.RetailMask = Mask;
		FElysiumRetailTraceResult Out;
		ElysiumRetailMask::TraceSkippingRefusedEntities(Request, Out,
			[&World](const FElysiumRetailTrace& R, FElysiumRetailTraceResult& O) { return World.Trace(R, O); },
			[Mask, RenderMode, SolidFlags](const FElysiumEntityHandle&)
			{
				return ElysiumRetailMask::EntityArmsReject(Mask, RenderMode, SolidFlags);
			});
		return Out.Fraction;
	}

	// 0019/6: one scripted line through one ENTITY prop at 0.4, standing for a body the world lane
	// built. Like Unreal's scene query, the prop is met unless the bits it wears share one with the
	// query's `IgnoreMask`, which is built by the same `QueryIgnoreMask` `ElysiumWorldGeometry::Trace`
	// uses. A prop hit names no entity (its body is a component of the map actor), so the arms never
	// see it: the bits are the whole filter.
	constexpr int32 TraceFilterThrowMask = 0x400b;   // a species throw line: no MONSTER

	float TraceFilterPropRun(uint8 PropBits, int32 Mask,
		EElysiumRetailTraceFilter Filter = EElysiumRetailTraceFilter::Simple)
	{
		FElysiumRetailTrace Request;
		Request.RetailMask = Mask;
		Request.Filter = Filter;
		const uint8 IgnoreMask = ElysiumRetailMask::QueryIgnoreMask(ElysiumRetailMask::Recipe(Request.RetailMask),
			Request.Filter == EElysiumRetailTraceFilter::FVisible);
		return (PropBits & IgnoreMask) == 0 ? 0.4f : 1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTraceFilterRenderModeTest,
	"Elysium.Arm.Map.TraceFilter.RenderMode", GElysiumMapActorTraceFilterFlags)
bool FElysiumMapActorTraceFilterRenderModeTest::RunTest(const FString&)
{
	// `101d3112`: `m_nRenderMode != 0` is refused unless the mask carries WINDOW `0x2`.
	FTraceFilterWorld World;
	TestEqual(TEXT("a render-transparent brush entity does not stop FVisible's 0x2804091"),
		TraceFilterRun(World, TraceFilterSightMask, 1, 0u), 0.7f);
	TestEqual(TEXT("...the line was traced twice"), World.Passes, 2);
	World.Passes = 0;
	TestEqual(TEXT("it stops a mask carrying WINDOW"), TraceFilterRun(World, TraceFilterSolidMask, 1, 0u), 0.3f);
	TestEqual(TEXT("...in one pass"), World.Passes, 1);
	TestEqual(TEXT("render mode 0 stops FVisible"), TraceFilterRun(World, TraceFilterSightMask, 0, 0u), 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTraceFilterSolidFlagTest,
	"Elysium.Arm.Map.TraceFilter.SolidFlag20", GElysiumMapActorTraceFilterFlags)
bool FElysiumMapActorTraceFilterSolidFlagTest::RunTest(const FString&)
{
	// `101d30b6`: solid flag `0x20` is refused under every mask, WINDOW or not.
	FTraceFilterWorld World;
	TestEqual(TEXT("solid flag 0x20 is passed under 0x2804091"),
		TraceFilterRun(World, TraceFilterSightMask, 0, 0x20u), 0.7f);
	TestEqual(TEXT("...and under a WINDOW mask"), TraceFilterRun(World, TraceFilterSolidMask, 0, 0x20u), 0.7f);
	TestEqual(TEXT("another solid flag is not"), TraceFilterRun(World, TraceFilterSolidMask, 0, 0x10u), 0.3f);
	TestTrue(TEXT("the arm reads 0x20 before render mode (retail's order)"),
		ElysiumRetailMask::EntityArmsReject(TraceFilterSolidMask, 0, 0x20u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTraceFilterBoundTest,
	"Elysium.Arm.Map.TraceFilter.PassBound", GElysiumMapActorTraceFilterFlags)
bool FElysiumMapActorTraceFilterBoundTest::RunTest(const FString&)
{
	// A world whose every pass meets a fresh refused entity stops after the bound, answering the last.
	int32 Passes = 0;
	FElysiumRetailTrace Request;
	Request.RetailMask = TraceFilterSightMask;
	FElysiumRetailTraceResult Out;
	ElysiumRetailMask::TraceSkippingRefusedEntities(Request, Out,
		[&Passes](const FElysiumRetailTrace&, FElysiumRetailTraceResult& O)
		{
			++Passes;
			O = FElysiumRetailTraceResult();
			O.Fraction = 0.5f;
			O.HitEntity = FElysiumEntityHandle(100 + Passes, 1);
			return true;
		},
		[](const FElysiumEntityHandle&) { return true; });
	TestEqual(TEXT("one pass plus the bound"), Passes, 1 + ElysiumRetailMask::MaxRefusedEntityPasses);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTraceFilterBlocksTracesTest,
	"Elysium.Arm.Map.TraceFilter.PropBlocksTraces", GElysiumMapActorTraceFilterFlags)
bool FElysiumMapActorTraceFilterBlocksTracesTest::RunTest(const FString&)
{
	// `101d30f2`: a non-brush entity is admitted without MONSTER when `m_bBlocksTraces` (+0xfd) is set.
	const uint8 Plain = ElysiumRetailMask::PropBodyMaskBits(false, false);
	const uint8 Blocks = ElysiumRetailMask::PropBodyMaskBits(true, false);
	TestEqual(TEXT("a plain entity prop wears PropMaskBit"), Plain, ElysiumRetailMask::PropMaskBit);
	TestEqual(TEXT("a blocks_traces prop wears none"), Blocks, static_cast<uint8>(0));
	TestEqual(TEXT("a plain entity prop is passed by a mask without MONSTER"),
		TraceFilterPropRun(Plain, TraceFilterThrowMask), 1.f);
	TestEqual(TEXT("a blocks_traces prop blocks it"), TraceFilterPropRun(Blocks, TraceFilterThrowMask), 0.4f);
	TestEqual(TEXT("...and a MONSTER mask"), TraceFilterPropRun(Blocks, TraceFilterSolidMask), 0.4f);
	TestEqual(TEXT("...and FVisible's"),
		TraceFilterPropRun(Blocks, TraceFilterSightMask, EElysiumRetailTraceFilter::FVisible), 0.4f);
	TestEqual(TEXT("a plain entity prop blocks a MONSTER mask"), TraceFilterPropRun(Plain, TraceFilterSolidMask), 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTraceFilterNpcTransparentPropTest,
	"Elysium.Arm.Map.TraceFilter.PropNpcTransparent", GElysiumMapActorTraceFilterFlags)
bool FElysiumMapActorTraceFilterNpcTransparentPropTest::RunTest(const FString&)
{
	// `CTraceFilterFVisible::ShouldHitEntity 0x10107630` skips `m_bNPCTransparent` (+0xfc); the plain
	// `CTraceFilterSimple` (`StandardFilterRules 0x101d3080`) has no such gate.
	const uint8 Transparent = ElysiumRetailMask::PropBodyMaskBits(false, true);
	TestEqual(TEXT("an npc_transparent prop is passed by the FVisible filter"),
		TraceFilterPropRun(Transparent, TraceFilterSightMask, EElysiumRetailTraceFilter::FVisible), 1.f);
	TestEqual(TEXT("it blocks the same mask under the default filter"),
		TraceFilterPropRun(Transparent, TraceFilterSightMask), 0.4f);
	TestEqual(TEXT("a plain entity prop blocks FVisible"),
		TraceFilterPropRun(ElysiumRetailMask::PropBodyMaskBits(false, false), TraceFilterSightMask,
			EElysiumRetailTraceFilter::FVisible), 0.4f);
	TestEqual(TEXT("both keyfields: FVisible passes it even without MONSTER"),
		TraceFilterPropRun(ElysiumRetailMask::PropBodyMaskBits(true, true), TraceFilterThrowMask,
			EElysiumRetailTraceFilter::FVisible), 1.f);
	TestEqual(TEXT("...the default filter meets it"),
		TraceFilterPropRun(ElysiumRetailMask::PropBodyMaskBits(true, true), TraceFilterThrowMask), 0.4f);
	TestEqual(TEXT("the bits are distinct FMaskFilter values"),
		static_cast<int32>(ElysiumRetailMask::NpcTransparentMaskBit & (ElysiumRetailMask::CharacterMaskBit
			| ElysiumRetailMask::MoverMaskBit | ElysiumRetailMask::PropMaskBit)), 0);
	return true;
}

#endif
