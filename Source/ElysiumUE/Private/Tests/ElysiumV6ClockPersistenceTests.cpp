#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumTimeControl.h"
#include "Engine/GameInstance.h"
#include "ElysiumRng.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumV6ClockTests
{
static constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
static TArray<int32> RestoreOrder;
static bool AllWordsPresent = true;
static int32 ThinkCount = 0;

class FClockProbe : public FElysiumEntity
{
public:
	double SpawnAt = 0.0; // init fence witness, never saved
	virtual void Spawn() override { SpawnAt = World->NowSeconds(); } // engine 0x200f5bc4
	float Stamp = 0.0f, RawNextAttack = 0.0f, LayerEvent = 0.0f; // 0x101a0a80; weapon/layer exceptions are FLOAT
	float Wait = 0.0f, MinusOne = -1.0f, Negative = -1.0f, Never = MAX_flt;
	float BaseEvent = 0.0f; // CBaseAnimating +0x658 TIME, unlike layer event FLOAT
	FElysiumEntityHandle Peer;
	bool bLateDeadline = false;
	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar.Time(Wait, EElysiumTimePolicy::Zero); // 0x1027bc60 mode 3
		Ar.Time(MinusOne, EElysiumTimePolicy::MinusOne); // 0x102e0b60 mode 2
		Ar.Time(Negative, EElysiumTimePolicy::Negative); // 0x101cf250 mode 1
		Ar.Time(Never, EElysiumTimePolicy::MaxFloat); // 0x102e8aa0 mode 4
		Ar.Time(BaseEvent); // 0x101a0a80 base event TIME SAVE
		Ar << LayerEvent << Peer << bLateDeadline; // raw layer FLOAT / EHANDLE
	}
	virtual void RebaseSavedReferences(FElysiumEntityWorld& InWorld) override
	{
		Peer = InWorld.RestoreHandle(Peer); // 0x101a2e40 before prerequisite validation
	}
	virtual void OnPostRestore(FElysiumEntityWorld& InWorld) override
	{
		RestoreOrder.Add(Handle.Index); // 0x1011a620 reversed restored list
		if (Peer.IsSet())
		{
			const FClockProbe* Other = static_cast<const FClockProbe*>(InWorld.Resolve(Peer));
			AllWordsPresent &= Other && Other->Stamp == 105.0f && Other->BaseEvent == 98.0f;
		}
		if (bLateDeadline) NextThink = static_cast<float>(InWorld.NowSeconds() + 7.0); // 0x102998c0 later hook wins
	}
	virtual void Think() override { ++ThinkCount; }
};
static TUniquePtr<FElysiumEntity> MakeProbe() { return MakeUnique<FClockProbe>(); }
static FElysiumClassRegistrar Registration(TEXT("__v6_clock_probe"), ElysiumBaseClassName(), &MakeProbe,
	[](FElysiumClassDesc& Desc)
	{
		FElysiumFieldAccessor StampAccessor;
		StampAccessor.Type = EElysiumVariantType::Float; StampAccessor.bSave = true;
		StampAccessor.PersistenceType = EElysiumPersistenceType::Time; // raw TIME row, 0x101a0a80
		StampAccessor.Get = [](const FElysiumEntity& Entity) { return FElysiumVariant::Float(static_cast<const FClockProbe&>(Entity).Stamp); };
		StampAccessor.Set = [](FElysiumEntity& Entity, const FElysiumVariant& Value) { static_cast<FClockProbe&>(Entity).Stamp = Value.ToFloat(); };
		Desc.Fields.Add(TEXT("stamp"), MoveTemp(StampAccessor));
		FElysiumFieldAccessor RawAccessor;
		RawAccessor.Type = EElysiumVariantType::Float; RawAccessor.bSave = true;
		RawAccessor.Get = [](const FElysiumEntity& Entity) { return FElysiumVariant::Float(static_cast<const FClockProbe&>(Entity).RawNextAttack); };
		RawAccessor.Set = [](FElysiumEntity& Entity, const FElysiumVariant& Value) { static_cast<FClockProbe&>(Entity).RawNextAttack = Value.ToFloat(); };
		Desc.Fields.Add(TEXT("raw_next_attack"), MoveTemp(RawAccessor)); // weapon +0x730 FLOAT SAVE
	});

static FElysiumEntityDefs MakeDefs()
{
	FElysiumEntityDefs Result; Result.MapName = TEXT("__v6_clock__");
	for (int32 Row = 0; Row < 3; ++Row)
	{
		FElysiumEntityDef Definition; Definition.Classname = TEXT("__v6_clock_probe");
		Definition.TargetName = FString::Printf(TEXT("probe%d"), Row); Result.Defs.Add(MoveTemp(Definition));
	}
	FElysiumEntityDef Counter; Counter.Classname = TEXT("math_counter"); Counter.TargetName = TEXT("counter");
	Counter.Keys.Add(TEXT("max"), TEXT("100")); Result.Defs.Add(MoveTemp(Counter));
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6ClockSelectionTest, "Elysium.Arm.V6.ClockPersistence.MapSelection", ElysiumV6ClockTests::Flags)
bool FElysiumV6ClockSelectionTest::RunTest(const FString&)
{
	TArray<ElysiumRng::FState> PriorRng; ElysiumRng::Snapshot(PriorRng);
	const int32 PriorSeed = ElysiumRng::SessionSeed();
	ON_SCOPE_EXIT { ElysiumRng::SeedAll(PriorSeed); ElysiumRng::Restore(PriorRng); };
	UGameInstance* TestGI = NewObject<UGameInstance>();
	UElysiumSessionSubsystem* Session = NewObject<UElysiumSessionSubsystem>(TestGI);
	Session->TimeControl().ResetClock(); // engine 0x200f5bc4
	Session->SelectMapClock(TEXT("first"));
	TestEqual(TEXT("first map before entity initialization"), Session->GameClock().GetNow(), 1.0);
	{
		FElysiumEntityWorld Initial(nullptr, Session); Initial.Load(ElysiumV6ClockTests::MakeDefs());
		const ElysiumV6ClockTests::FClockProbe* InitialProbe = static_cast<const ElysiumV6ClockTests::FClockProbe*>(Initial.FindByName(TEXT("probe0")));
		if (!TestNotNull(TEXT("initial probe"), InitialProbe)) return false;
		TestEqual(TEXT("Spawn observes fresh map 1.0"), InitialProbe->SpawnAt, 1.0); // actual initialization path
	}
	FElysiumMapSnapshot Visit; Visit.MapName = TEXT("return"); Visit.FrozenAt = 37.0; Visit.SaveBase = 37.0;
	Session->StoreMapSnapshot(MoveTemp(Visit));
	Session->TimeControl().SetPaused(true);
	Session->SelectMapClock(TEXT("return")); // engine 0x200975f0
	TestEqual(TEXT("destination frozen clock"), Session->GameClock().GetNow(), 37.0);
	TestTrue(TEXT("selection preserves pause"), Session->GameClock().IsPaused());
	FElysiumSavePayload Payload; Payload.Session.ClockNow = 53.0; Payload.World.CurrentMap = TEXT("saved");
	FElysiumMapSnapshot Saved; Saved.MapName = TEXT("saved"); Saved.FrozenAt = 53.0; Saved.SaveBase = 53.0;
	Payload.Maps.Add(Saved.MapName, Saved);
	TArray<uint8> Bytes; FString Error;
	if (!TestTrue(TEXT("normal payload codec"), ElysiumSave::Write(Payload, Bytes, Error))) return false;
	FElysiumSavePayload Decoded;
	if (!TestTrue(TEXT("normal decode"), ElysiumSave::Read(Bytes, Decoded, Error))) return false;
	Session->ApplyPayload(Decoded); Session->SelectMapClock(Decoded.World.CurrentMap);
	TestEqual(TEXT("saved current-map clock"), Session->GameClock().GetNow(), 53.0);
	Session->ClearMapSnapshot(TEXT("saved")); Session->SelectMapClock(TEXT("saved"));
	TestEqual(TEXT("fresh-load target epoch"), Session->GameClock().GetNow(), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6TimeRestoreTest, "Elysium.Arm.V6.ClockPersistence.SectionRestore", ElysiumV6ClockTests::Flags)
bool FElysiumV6TimeRestoreTest::RunTest(const FString&)
{
	using namespace ElysiumV6ClockTests;
	RestoreOrder.Reset(); AllWordsPresent = true; ThinkCount = 0;
	FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(MakeDefs()); Source.Activate(20.0);
	for (int32 Row = 0; Row < 3; ++Row)
	{
		FClockProbe* Probe = static_cast<FClockProbe*>(Source.FindByName(FString::Printf(TEXT("probe%d"), Row)));
		if (!TestNotNull(TEXT("probe"), Probe)) return false;
		Probe->Stamp = 25.0f; Probe->RawNextAttack = 12.0f; Probe->BaseEvent = 18.0f; Probe->LayerEvent = 0.3f;
		Probe->NextThink = Row == 1 ? -1.0f : 24.0f; // 0x100a9f70 NextThinkSR exception
	}
	FClockProbe* First = static_cast<FClockProbe*>(Source.FindByName(TEXT("probe0")));
	FClockProbe* Second = static_cast<FClockProbe*>(Source.FindByName(TEXT("probe1")));
	FClockProbe* Hidden = static_cast<FClockProbe*>(Source.FindByName(TEXT("probe2")));
	First->Peer = Second->Handle; First->bLateDeadline = true;
	Hidden->ScriptHide(); // 0x100a8710 NULL think transaction
	Source.FindByName(TEXT("counter"))->NextThink = 0.0f; // NextThinkSR zero exception
	Source.EnqueueInput(TEXT("counter"), TEXT("Add"), FElysiumVariant::Int(3), 5.0,
		FElysiumEntityHandle::Invalid(), First->Handle); // actual delayed I/O
	// A camera excluded from the carrier leaves a stable-identity hole before a saved runtime entity.
	FElysiumEntityDef Excluded; Excluded.Classname = TEXT("camera_cinematic"); Excluded.TargetName = TEXT("excluded_camera");
	Hidden->Peer = Source.SpawnRuntimeEntity(MoveTemp(Excluded)); // existing exclusion, fixup must invalidate missing target
	FElysiumEntityDef RuntimeDef; RuntimeDef.Classname = TEXT("math_counter"); RuntimeDef.TargetName = TEXT("runtime");
	const FElysiumEntityHandle OldRuntime = Source.SpawnRuntimeEntity(MoveTemp(RuntimeDef));
	FElysiumSavePayload Payload; FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
	Payload.Maps.Add(Snapshot.MapName, Snapshot); Payload.World.CurrentMap = Snapshot.MapName;
	TArray<uint8> Bytes; FString Error;
	if (!TestTrue(TEXT("actual carrier codec"), ElysiumSave::Write(Payload, Bytes, Error))) return false;
	FElysiumSavePayload Decoded;
	if (!TestTrue(TEXT("actual carrier decode"), ElysiumSave::Read(Bytes, Decoded, Error))) return false;
	const FElysiumMapSnapshot& Restore = Decoded.Maps.FindChecked(Snapshot.MapName);
	FElysiumEntityWorld Destination(nullptr, nullptr); Destination.Load(MakeDefs());
	bool AppliedBeforeThink = false;
	Destination.OnSnapshotApplied = [&]() { AppliedBeforeThink = ThinkCount == 0; };
	if (!TestEqual(TEXT("every carried record applied"), Destination.ApplySnapshot(Restore, 100.0), Restore.Entities.Num())) return false;
	if (!TestEqual(TEXT("all probe hooks ran"), RestoreOrder.Num(), 3)) return false;
	TestTrue(TEXT("all entity words before callbacks"), AllWordsPresent);
	TestEqual(TEXT("reverse first restored probe"), RestoreOrder[0], 2);
	TestEqual(TEXT("reverse last restored probe"), RestoreOrder[2], 0);
	TestTrue(TEXT("applied fence before think"), AppliedBeforeThink);
	FClockProbe* Loaded = static_cast<FClockProbe*>(Destination.FindByName(TEXT("probe0")));
	FClockProbe* LoadedHidden = static_cast<FClockProbe*>(Destination.FindByName(TEXT("probe2")));
	TestEqual(TEXT("accessor TIME onto different section base"), Loaded->Stamp, 105.0f);
	TestEqual(TEXT("base event TIME"), Loaded->BaseEvent, 98.0f);
	TestEqual(TEXT("weapon next attack FLOAT"), Loaded->RawNextAttack, 12.0f);
	TestEqual(TEXT("layer event FLOAT"), Loaded->LayerEvent, 0.3f);
	TestEqual(TEXT("zero wait sentinel"), Loaded->Wait, 0.0f);
	TestEqual(TEXT("minus-one motor sentinel"), Loaded->MinusOne, -1.0f);
	TestEqual(TEXT("negative sentinel"), Loaded->Negative, -1.0f);
	TestEqual(TEXT("max-float move-shot sentinel"), Loaded->Never, MAX_flt);
	TestEqual(TEXT("later OnRestore deadline wins"), Loaded->NextThink, 107.0f);
	TestEqual(TEXT("NextThinkSR minus-one stays exact"), Destination.FindByName(TEXT("probe1"))->NextThink, -1.0f);
	TestTrue(TEXT("hidden callback parked"), LoadedHidden->ThinkCallback.IsNone());
	TestEqual(TEXT("hidden next think parked"), LoadedHidden->NextThink, MAX_flt);
	TestFalse(TEXT("excluded target invalid before prerequisite hook"), LoadedHidden->Peer.IsSet());
	TestEqual(TEXT("NextThinkSR zero stays exact"), Destination.FindByName(TEXT("counter"))->NextThink, 0.0f);
	TestNull(TEXT("old epoch handle invalid"), Destination.Resolve(OldRuntime));
	TestNotNull(TEXT("runtime carrier restored"), Destination.FindByName(TEXT("runtime")));
	if (!TestNotNull(TEXT("restored delayed queue"), Destination.Queue().PeekEarliest())) return false;
	TestEqual(TEXT("queue delay rebased"), Destination.Queue().PeekEarliest()->FireTime, 105.0);
	Destination.Activate(100.0); Destination.Tick(104.0);
	TestEqual(TEXT("delayed queue not early"), Destination.Queue().Num(), 1);
	Destination.Tick(105.0); Destination.Tick(106.0);
	TestEqual(TEXT("delayed I/O fires once"), Destination.Queue().Num(), 0);
	const FElysiumFieldAccessor* CounterValue = FElysiumClassRegistry::Get().FindField(*Destination.FindByName(TEXT("counter"))->Class, TEXT("startvalue"));
	if (!TestNotNull(TEXT("counter accessor"), CounterValue)) return false;
	TestEqual(TEXT("counter received one delivery"), CounterValue->Get(*Destination.FindByName(TEXT("counter"))).ToFloat(), 3.0f);
	LoadedHidden->ScriptUnhide();
	TestEqual(TEXT("unhide due now"), LoadedHidden->NextThink, 106.0f); // 0x100a8990
	TestFalse(TEXT("unhide restores identity"), LoadedHidden->ThinkCallback.IsNone());
	FElysiumEntityWorld Fresh(nullptr, nullptr); Fresh.Load(MakeDefs());
	TestNull(TEXT("fresh world has independent epoch"), Fresh.Resolve(OldRuntime));
	TestNull(TEXT("fresh load lacks prior runtime row"), Fresh.FindByName(TEXT("runtime")));
	TestEqual(TEXT("fresh load lacks old one-shot queue"), Fresh.Queue().Num(), 0);
	return true;
}
#endif
