#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumScheduleManager.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Tests/ElysiumTestServices.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumV6NpcTests
{
static constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
// A recording motor is the geometry seam; all serialization, fixup and restore consumers are real.
class FProbe : public FElysiumNpc
{
public:
	FElysiumRecordingNpcMotor RecordingMotor;
	FProbe() { Motor = &RecordingMotor; }
	virtual ~FProbe() override { Motor = nullptr; }
	virtual void Spawn() override {} // no model/asset admission in this substrate arm fixture
	virtual void Activate() override {}
	virtual void OnRuntimeModelChanged() override {}
	virtual bool IsCurTaskContinuousMove() override { return bContinuous; } // slot529 input, 0x102f1dc0
	bool bContinuous = true;
	void CommitCorpse() { bDeathCommitted = true; WriteNpcStateRetail(7); } // 0x1032c0e0 narrow staging
};

// Explicit successor-source fixture; production slot172 remains an integrator prerequisite.
class FRestoreCorner : public FElysiumEntity
{
public:
	virtual FElysiumEntity* GetNextTarget() override
	{
		return World != nullptr && !Target.IsEmpty() ? World->FindByName(Target) : nullptr; // 0x100a1d20
	}
};
static TUniquePtr<FElysiumEntity> MakeCorner() { return MakeUnique<FRestoreCorner>(); }
static FElysiumClassRegistrar CornerRegistration(TEXT("__v6_restore_corner"), ElysiumBaseClassName(), &MakeCorner, [](FElysiumClassDesc&) {});
static TUniquePtr<FElysiumEntity> MakeProbe() { return MakeUnique<FProbe>(); }
static FElysiumClassRegistrar Registration(TEXT("__v6_npc_restore"), TEXT("CAI_BaseNPCTroika"), &MakeProbe,
	[](FElysiumClassDesc&) {});

struct FProgramScope
{
	FElysiumScheduleManager Backup;
	FElysiumScheduleManager& Manager;
	static constexpr int32 ProgramId = 1700000001;
	FProgramScope() : Manager(const_cast<FElysiumScheduleManager&>(FElysiumScheduleCorpus::Get().Manager()))
	{
		FElysiumScheduleCorpus::Get().EnsureLoaded();
		Backup = Manager;
		FElysiumScheduleProgram Program; Program.Name = TEXT("__V6_LONG_RESUME"); Program.GlobalId = ProgramId;
		Program.Tasks.SetNum(48); // legal retail maximum64; cursor43 must not be clamped to1
		Manager.Add(MoveTemp(Program)); // actual name/CRC/header consumer, 0x1027bf50
	}
	~FProgramScope() { Manager = MoveTemp(Backup); }
};
static FElysiumEntityDefs Defs()
{
	FElysiumEntityDefs Result; Result.MapName = TEXT("__v6_npc__");
	FElysiumEntityDef Npc; Npc.Classname = TEXT("__v6_npc_restore"); Npc.TargetName = TEXT("npc"); Result.Defs.Add(Npc);
	FElysiumEntityDef Goal; Goal.Classname = TEXT("point_target"); Goal.TargetName = TEXT("goal"); Goal.Origin = FVector(500, 0, 0); Result.Defs.Add(Goal);
	FElysiumEntityDef Cine; Cine.Classname = TEXT("scripted_sequence"); Cine.TargetName = TEXT("cine"); Result.Defs.Add(Cine);
	return Result;
}
static FProbe* Probe(FElysiumEntityWorld& World) { return static_cast<FProbe*>(World.FindByName(TEXT("npc"))); }
static void Stage(FProbe& Npc)
{
	Npc.WriteNpcStateRetail(1); Npc.WriteIdealStateRetail(1);
	Npc.Schedule.Current = FProgramScope::ProgramId; Npc.Schedule.TaskIndex = 43;
	Npc.Schedule.TaskStatus = EElysiumTaskStatus::RunningMovement;
	Npc.Schedule.ScheduleStartedAt = 17; Npc.Schedule.TaskStartedAt = 18; Npc.Schedule.TaskEndsAt = 24;
	Npc.Schedule.FailScheduleOverride = 12; Npc.Schedule.ToleranceUnits = 32; Npc.Schedule.bDidMaintainSchedule = true;
	Npc.BaseScheduleHost.WaitFinished = 0; Npc.Cognition.GatheredAt = 20;
	Npc.ThinkSet(TEXT("LAB_1000f4e8"), 0); Npc.NextThink = 21;
}
static bool Codec(const FElysiumMapSnapshot& Snapshot, FElysiumMapSnapshot& Decoded)
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true); Writer << const_cast<FElysiumMapSnapshot&>(Snapshot);
	FMemoryReader Reader(Bytes, true); Reader << Decoded; // normal map codec then common applier, 0x101a2e40
	return !Writer.IsError() && !Reader.IsError();
}
static bool CorruptCrc(FElysiumMapSnapshot& Snapshot, int32 NpcIndex)
{
	for (FElysiumEntityState& Record : Snapshot.Entities)
	{
		if (Record.Index != NpcIndex) continue;
		FMemoryReader Reader(Record.LeafState, true); FElysiumSaveArchive Ar(Reader, Snapshot.SchemaVersion, Snapshot.SaveBase, Snapshot.SaveBase);
		int16 Version = 0; uint32 HeaderFlags = 0; FString ScheduleName;
		Ar << Version << HeaderFlags << ScheduleName;
		const int64 CrcOffset = Reader.Tell(); uint32 Crc = 0; Ar << Crc;
		if (Reader.IsError()) return false;
		FMemoryWriter Writer(Record.LeafState, true); Writer.Seek(CrcOffset); Crc ^= 0xffffffffu; Writer << Crc; // 0x1027c064 control
		return !Writer.IsError();
	}
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcCursorTest, "Elysium.Arm.V6.NpcRestore.CursorPhaseAndTypes", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcCursorTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	TArray<ElysiumRng::FState> SavedRng; ElysiumRng::Snapshot(SavedRng);
	ON_SCOPE_EXIT { ElysiumRng::Restore(SavedRng); };
	FProgramScope ProgramScope;
	FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(Defs()); Source.Activate(20);
	FProbe* Original = Probe(Source); if (!TestNotNull(TEXT("source"), Original)) return false;
	Stage(*Original); Original->BaseScheduleHost.FailureReason = 42; // separate from legal cursor43, 0x1027bff6
	Original->SequenceNumber = 3; Original->SequenceCycle = .4f; Original->SequencePlaybackRate = .75f;
	Original->SequenceCycleRate = 2; Original->AnimTime = 19; Original->PrevAnimTime = 18.5f; Original->LastEventCheck = 18;
	Original->bSequenceFinished = true; Original->SequencePastHalf = true;
	for (int32 LayerIndex = 0; LayerIndex < ElysiumOverlay::NumSlots; ++LayerIndex)
	{
		auto& Layer = Original->AnimOverlay[LayerIndex]; Layer.Sequence = LayerIndex + 2; Layer.Activity = 30 + LayerIndex;
		Layer.Flags = LayerIndex; Layer.Cycle = .3f; Layer.PlaybackRate = .7f; Layer.Weight = .8f; Layer.WeightMax = 1;
		Layer.BlendIn = .2f; Layer.BlendOut = .25f; Layer.SequenceFinished = 1; Layer.bAutoKillWhenFinished = true; Layer.LastEventCheck = .2f;
	}
	Original->Flinch[0].Sequence = 4; Original->Flinch[0].Latch = 3; Original->Flinch[0].ExpireTime = 22;
	Original->MoveAndShootOverlay.bMovingAndShooting = true; Original->MoveAndShootOverlay.MoveShots = 2;
	Original->MoveAndShootOverlay.NextShotTime = MAX_flt; Original->MoveAndShootOverlay.PauseMin = .5f;
	FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot);
	if (!TestTrue(TEXT("normal map codec"), Codec(Snapshot, Decoded))) return false;
	FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(Defs());
	FRandomStream ExpectedDraw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule); ExpectedDraw.FRandRange(2, 2.5f);
	if (!TestTrue(TEXT("common reconstructed-world applier"), Restored.ApplySnapshot(Decoded, 100) != INDEX_NONE)) return false;
	FProbe* Resumed = Probe(Restored); if (!TestNotNull(TEXT("restored"), Resumed)) return false;
	TestEqual(TEXT("valid cursor43 is retained"), Resumed->Schedule.TaskIndex, 43);
	TestEqual(TEXT("failure42 clamps independently"), Resumed->BaseScheduleHost.FailureReason, 1);
	TestEqual(TEXT("running movement status"), static_cast<int32>(Resumed->Schedule.TaskStatus), static_cast<int32>(EElysiumTaskStatus::RunningMovement));
	TestEqual(TEXT("schedule TIME"), Resumed->Schedule.ScheduleStartedAt, 97.0);
	TestEqual(TEXT("task TIME"), Resumed->Schedule.TaskStartedAt, 98.0);
	TestEqual(TEXT("wait zero sentinel"), Resumed->BaseScheduleHost.WaitFinished, 0.0);
	TestEqual(TEXT("gathered BOOL uses selected base"), Resumed->Cognition.GatheredAt, 100.0);
	TestEqual(TEXT("base event is TIME even though consumer uses cycles"), Resumed->LastEventCheck, 98.f);
	TestEqual(TEXT("base cycle FLOAT"), Resumed->SequenceCycle, .4f);
	TestEqual(TEXT("playback FLOAT"), Resumed->SequencePlaybackRate, .75f);
	TestEqual(TEXT("prev animation TIME"), Resumed->PrevAnimTime, 98.5f);
	TestTrue(TEXT("finish retained without ResetSequenceInfo"), Resumed->bSequenceFinished);
	for (int32 LayerIndex = 0; LayerIndex < ElysiumOverlay::NumSlots; ++LayerIndex)
	{
		const auto& Layer = Resumed->AnimOverlay[LayerIndex];
		TestEqual(TEXT("layer event remains raw FLOAT"), Layer.LastEventCheck, .2f);
		TestEqual(TEXT("layer activity"), Layer.Activity, 30 + LayerIndex);
		TestEqual(TEXT("layer blend"), Layer.BlendOut, .25f);
		TestTrue(TEXT("layer auto-kill byte"), Layer.bAutoKillWhenFinished);
	}
	TestEqual(TEXT("flinch TIME without new producer"), Resumed->Flinch[0].ExpireTime, 102.f);
	TestEqual(TEXT("move-shoot never sentinel"), Resumed->MoveAndShootOverlay.NextShotTime, MAX_flt);
	TestEqual(TEXT("move-shoot count"), Resumed->MoveAndShootOverlay.MoveShots, 2);
	TestEqual(TEXT("only lawful Troika shoot-at reroll"), ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed(), ExpectedDraw.GetCurrentSeed());
	TestFalse(TEXT("no restored StartTask"), Resumed->Schedule.TaskStatus == EElysiumTaskStatus::New);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcInvalidTest, "Elysium.Arm.V6.NpcRestore.InvalidPrerequisites", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcInvalidTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FProgramScope ProgramScope;
	for (int32 Control = 0; Control < 5; ++Control)
	{
		FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(Defs()); Source.Activate(20);
		FProbe* Original = Probe(Source); Stage(*Original);
		FElysiumEntity* Goal = Source.FindByName(TEXT("goal")); FElysiumEntity* Cine = Source.FindByName(TEXT("cine"));
		if (Control == 1) { Original->WriteNpcStateRetail(4); Original->ScriptOwner = Cine->Handle; }
		if (Control == 2) Original->SetTarget(Goal->Handle);
		if (Control == 3) { Original->Navigator.GoalType = 3; Original->Navigator.bHasHeadWaypoint = true; Original->BaseScheduleHost.GoalEnt = Goal->Handle; }
		if (Control == 4) ElysiumNpcEnemy::SetEnemy(*Original, Goal->Handle); // 0x1027bf50 saved enemy prerequisite
		FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot);
		if (Control == 0 && !TestTrue(TEXT("header corruption layout"), CorruptCrc(Snapshot, Original->Handle.Index))) return false;
		if (Control == 1) Snapshot.AbsentEntities.Add(Cine->Handle.Index);
		if (Control == 2 || Control == 3 || Control == 4) Snapshot.AbsentEntities.Add(Goal->Handle.Index);
		if (!TestTrue(TEXT("normal corrupted-control codec"), Codec(Snapshot, Decoded))) return false;
		FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(Defs());
		if (!TestTrue(TEXT("common apply"), Restored.ApplySnapshot(Decoded, 20) != INDEX_NONE)) return false;
		FProbe* Resumed = Probe(Restored);
		TestFalse(TEXT("CRC/cine/target/path/enemy refusal drops schedule"), Resumed->Schedule.IsRunning()); // 0x1027be60
		if (Control == 1) { TestEqual(TEXT("missing cine becomes IDLE"), Resumed->NpcStateRetail(), 1); TestEqual(TEXT("ideal IDLE"), Resumed->GetMind().IdealStateRetail(), 1); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcPathTest, "Elysium.Arm.V6.NpcRestore.RouteDispatchAndRetry", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcPathTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FProgramScope ProgramScope;
	for (int32 GoalType : {1, 2, 4, 5, 6, 7, 8, 9})
	{
		FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(Defs()); Source.Activate(20);
		FProbe* Original = Probe(Source); Stage(*Original);
		FElysiumEntity* Goal = Source.FindByName(TEXT("goal"));
		Original->Navigator.GoalType = GoalType; Original->Navigator.GoalPosCm = Goal->Origin;
		Original->Navigator.TargetEntity = Goal->Handle; Original->Navigator.bHasHeadWaypoint = true;
		Original->Navigator.MovementActivity = 0x13; Original->Navigator.GoalToleranceCm = 12;
		Original->Senses.Memory.BestSeeUnknown = Goal->Handle;
		Original->EnemyMemory.UpdateAtPosition(*Original, Goal->Handle, FVector(400, 0, 0), 20);
		FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot); Codec(Snapshot, Decoded);
		FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(Defs());
		if (!TestTrue(TEXT("route common apply"), Restored.ApplySnapshot(Decoded, 100) != INDEX_NONE)) return false;
		FProbe* Resumed = Probe(Restored);
		TestTrue(TEXT("route rebuilt through actual MoveTo"), Resumed->RecordingMotor.bMoving);
		TestTrue(TEXT("cursor remains live"), Resumed->Schedule.IsRunning());
		TestEqual(TEXT("cursor retained across path"), Resumed->Schedule.TaskIndex, 43);
		TestEqual(TEXT("remembered enemy goal uses Anchor"), Resumed->Navigator.GoalPosCm.X, GoalType == 2 ? 400.0 : 500.0);
	}
	FElysiumEntityWorld RetryWorld(nullptr, nullptr); RetryWorld.Load(Defs()); RetryWorld.Activate(20);
	FProbe* RetryNpc = Probe(RetryWorld); Stage(*RetryNpc); RetryNpc->Navigator.GoalType = 4;
	RetryNpc->RecordingMotor.bAcceptMoves = false; RetryNpc->Navigator.RouteSearchTime = 5; RetryNpc->Navigator.RouteRetryInterval = 1;
	TestFalse(TEXT("initial route refusal"), RetryNpc->RefindPostRestorePath());
	TestEqual(TEXT("retry arm"), RetryNpc->BaseScheduleHost.MemoryBits & 0x20, 0x20);
	TestEqual(TEXT("duration arms timeout"), RetryNpc->Navigator.RouteGiveUpTime, 25.0);
	RetryNpc->Navigator.RouteRetryTime = 20; RetryNpc->RecordingMotor.bAcceptMoves = true;
	TestFalse(TEXT("retry equality does not attempt"), RetryNpc->RefindPostRestorePath());
	RetryNpc->Navigator.RouteRetryTime = 19; RetryNpc->Navigator.RouteGiveUpTime = 20;
	TestTrue(TEXT("timeout equality still allows retry success"), RetryNpc->RefindPostRestorePath());
	TestEqual(TEXT("retry success clears bit"), RetryNpc->BaseScheduleHost.MemoryBits & 0x20, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcCallbacksTest, "Elysium.Arm.V6.NpcRestore.HiddenCorpseCallbacks", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcCallbacksTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FProgramScope ProgramScope;
	for (const TCHAR* Callback : {TEXT("0x101c0b10"), TEXT("0x102696f0"), TEXT("0x10269960"), TEXT("")})
	{
		FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(Defs()); Source.Activate(20);
		FProbe* Original = Probe(Source); Stage(*Original); Original->CommitCorpse();
		Original->ThinkSet(Callback, 0); Original->NextThink = Callback[0] ? 27.f : MAX_flt; Original->RenderAlphaByte = 111;
		FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot); Codec(Snapshot, Decoded);
		FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(Defs()); Restored.ApplySnapshot(Decoded, 100);
		FProbe* Resumed = Probe(Restored);
		TestEqual(TEXT("selected corpse callback"), Resumed->ThinkCallback, FName(Callback));
		TestEqual(TEXT("corpse deadline rebases without fresh +10"), Resumed->NextThink, Callback[0] ? 107.f : MAX_flt);
		TestEqual(TEXT("fade alpha"), Resumed->RenderAlphaByte, static_cast<uint8>(111));
	}
	FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(Defs()); Source.Activate(20);
	FProbe* Original = Probe(Source); Stage(*Original); Original->ScriptHide();
	FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot); Codec(Snapshot, Decoded);
	FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(Defs()); Restored.ApplySnapshot(Decoded, 100);
	FProbe* Resumed = Probe(Restored);
	TestTrue(TEXT("hidden NULL callback"), Resumed->ThinkCallback.IsNone());
	TestEqual(TEXT("hidden valid cursor preserved"), Resumed->Schedule.TaskIndex, 43);
	Resumed->ScriptUnhide();
	TestEqual(TEXT("unhide reinstates saved callback"), Resumed->ThinkCallback, FName(TEXT("LAB_1000f4e8")));
	TestEqual(TEXT("unhide due NOW"), Resumed->NextThink, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcCineTest, "Elysium.Arm.V6.NpcRestore.CineAndWeaponRetainedWords", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcCineTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FProgramScope ProgramScope;
	FElysiumItemTable Table; FElysiumItemDef SingleRound;
	SingleRound.Classname = TEXT("item_w_v6_single_round"); SingleRound.PrintName = SingleRound.Classname;
	SingleRound.Type = EElysiumItemType::WeaponFirearm; SingleRound.bWieldable = true;
	SingleRound.bReloadSingle = true; SingleRound.MagazineSize = 4; SingleRound.AmmoType = TEXT("V6Shell");
	Table.Items.Add(SingleRound); Table.Reindex(); ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };
	FElysiumEntityDefs SceneDefs = Defs();
	for (const TCHAR* EventName : {TEXT("OnBeginSequence"), TEXT("OnEndSequence")})
	{
		FElysiumOutputDef Output; Output.Name = EventName; Output.Target = TEXT("goal"); Output.Input = TEXT("Use"); Output.Times = 3;
		SceneDefs.Defs[2].Outputs.Add(Output);
	}
	FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(FElysiumEntityDefs(SceneDefs)); Source.Activate(20);
	FProbe* Original = Probe(Source); Stage(*Original);
	FElysiumScriptedSequence* Cine = static_cast<FElysiumScriptedSequence*>(Source.FindByName(TEXT("cine")));
	Original->WriteNpcStateRetail(4); Original->WriteIdealStateRetail(4); Original->ScriptOwner = Cine->Handle;
	Original->ScriptState = 2; Original->SequenceCycle = .45f; Cine->SetTarget(Original->Handle); Cine->Delay = 2;
	Cine->LastInputActivator = Original->Handle; Cine->LastInputCaller = Source.FindByName(TEXT("goal"))->Handle;
	Cine->OutputTimesRemaining[0] = 1; Cine->OutputTimesRemaining[1] = 2; // remaining output counts are ordinary saved state
	const FElysiumEntityHandle WeaponHandle = Original->Inventory.GiveNamedItem(*Original, SingleRound.Classname);
	FElysiumEntity* WeaponEntity = Source.Resolve(WeaponHandle);
	FElysiumWeapon* Weapon = WeaponEntity != nullptr && WeaponEntity->AsItem() != nullptr ? WeaponEntity->AsItem()->AsWeapon() : nullptr;
	if (!TestNotNull(TEXT("normal granted single-round weapon"), Weapon)) return false;
	Weapon->bInReload = Weapon->bIsJammed = Weapon->bInterruptReload = true;
	Weapon->NextPrimaryAttackTime = 12; Weapon->NextSecondaryAttackTime = 13; Weapon->MagazineCount = 0;
	Weapon->FinishReload(); // REAL 0x1025506f..77 NPC gate, no fake reload text
	TestTrue(TEXT("real single-round NPC early exit retains bytes"), Weapon->bInReload && Weapon->bIsJammed && Weapon->bInterruptReload);
	FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot);
	if (!TestTrue(TEXT("normal scene/weapon codec"), Codec(Snapshot, Decoded))) return false;
	FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(MoveTemp(SceneDefs));
	if (!TestTrue(TEXT("common scene/weapon applier"), Restored.ApplySnapshot(Decoded, 100) != INDEX_NONE)) return false;
	FProbe* Resumed = Probe(Restored); FElysiumScriptedSequence* SavedCine = static_cast<FElysiumScriptedSequence*>(Restored.FindByName(TEXT("cine")));
	TestEqual(TEXT("SCRIPT not clamped by old restart policy"), Resumed->NpcStateRetail(), 4);
	TestEqual(TEXT("cine handle rebased before prerequisites"), Resumed->ScriptOwner, SavedCine->Handle);
	TestEqual(TEXT("director target rebased before any OnRestore"), SavedCine->GetTarget(), Resumed->Handle);
	TestEqual(TEXT("script task cursor"), Resumed->Schedule.TaskIndex, 43);
	TestEqual(TEXT("script state"), Resumed->ScriptState, 2);
	TestEqual(TEXT("sequence phase"), Resumed->SequenceCycle, .45f);
	TestEqual(TEXT("no second Begin output"), SavedCine->OutputTimesRemaining[0], 1);
	TestEqual(TEXT("no second End output"), SavedCine->OutputTimesRemaining[1], 2);
	FElysiumEntity* SavedWeaponEntity = Restored.Resolve(Restored.RestoreHandle(WeaponHandle));
	FElysiumWeapon* SavedWeapon = SavedWeaponEntity != nullptr && SavedWeaponEntity->AsItem() != nullptr ? SavedWeaponEntity->AsItem()->AsWeapon() : nullptr;
	if (!TestNotNull(TEXT("runtime weapon identity reconstructed"), SavedWeapon)) return false;
	TestTrue(TEXT("all three BOOL SAVE bytes retained"), SavedWeapon->bInReload && SavedWeapon->bIsJammed && SavedWeapon->bInterruptReload);
	TestEqual(TEXT("primary is raw FLOAT"), SavedWeapon->NextPrimaryAttackTime, 12.0);
	TestEqual(TEXT("secondary is raw FLOAT"), SavedWeapon->NextSecondaryAttackTime, 13.0);
	TestEqual(TEXT("weapon owner identity"), SavedWeapon->Owner, Resumed->Handle);
	TestEqual(TEXT("load never called FinishReloadBulk"), SavedWeapon->MagazineCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6NpcCornerTest, "Elysium.Arm.V6.NpcRestore.CornerCutoffAndArrival", ElysiumV6NpcTests::Flags)
bool FElysiumV6NpcCornerTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FProgramScope ProgramScope;
	for (int32 CornerCount : {1, 127, 128})
	{
		FElysiumEntityDefs CornerDefs = Defs();
		CornerDefs.Defs[1].Classname = TEXT("__v6_restore_corner");
		CornerDefs.Defs[1].Keys.Add(TEXT("speed"), TEXT("110"));
		if (CornerCount > 1) CornerDefs.Defs[1].Keys.Add(TEXT("target"), TEXT("corner1"));
		for (int32 CornerIndex = 1; CornerIndex < CornerCount; ++CornerIndex)
		{
			FElysiumEntityDef Corner; Corner.Classname = TEXT("__v6_restore_corner"); Corner.TargetName = FString::Printf(TEXT("corner%d"), CornerIndex);
			Corner.Origin = FVector(500 + CornerIndex * 10, 0, 0);
			if (CornerIndex + 1 < CornerCount) Corner.Keys.Add(TEXT("target"), FString::Printf(TEXT("corner%d"), CornerIndex + 1));
			CornerDefs.Defs.Add(Corner);
		}
		FElysiumEntityWorld Source(nullptr, nullptr); Source.Load(FElysiumEntityDefs(CornerDefs)); Source.Activate(20);
		FProbe* Original = Probe(Source); Stage(*Original);
		Original->Navigator.GoalType = 3; Original->Navigator.bHasHeadWaypoint = true;
		Original->Navigator.GoalPosCm = FVector(777, 0, 0); Original->BaseScheduleHost.GoalEnt = Source.FindByName(TEXT("goal"))->Handle;
		FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot); Codec(Snapshot, Decoded);
		FElysiumEntityWorld Restored(nullptr, nullptr); Restored.Load(MoveTemp(CornerDefs)); Restored.ApplySnapshot(Decoded, 100);
		FProbe* Resumed = Probe(Restored);
		TestTrue(TEXT("type3 reconstructed head route"), Resumed->RecordingMotor.bMoving);
		TestEqual(TEXT("nonzero corner speed copied"), Resumed->AuthoredSpeed, 110.f);
		TestEqual(TEXT("terminal publication occurs only below128"), Resumed->Navigator.GoalPosCm.X, CornerCount < 128 ? 500.0 + (CornerCount - 1) * 10 : 777.0);
		TestEqual(TEXT("one-corner head is the goal"), Resumed->Navigator.bHeadIsGoal, CornerCount == 1);
	}
	FElysiumEntityWorld ArrivalWorld(nullptr, nullptr); ArrivalWorld.Load(Defs()); ArrivalWorld.Activate(20);
	FProbe* ArrivalNpc = Probe(ArrivalWorld); Stage(*ArrivalNpc); ArrivalNpc->bContinuous = false;
	ArrivalNpc->Navigator.GoalType = 4; ArrivalNpc->Navigator.GoalPosCm = FVector(500, 0, 0);
	TestTrue(TEXT("initial success"), ArrivalNpc->RefindPostRestorePath());
	TestEqual(TEXT("initial success dispatches arrival unless slot529 suppresses"), static_cast<int32>(ArrivalNpc->Schedule.TaskStatus), static_cast<int32>(EElysiumTaskStatus::Complete));
	ArrivalNpc->BaseScheduleHost.MemoryBits |= 0x20; ArrivalNpc->Navigator.RouteGiveUpTime = 19;
	TestFalse(TEXT("strict timeout expires past equality"), ArrivalNpc->RefindPostRestorePath());
	TestEqual(TEXT("timeout failure0xc"), ArrivalNpc->BaseScheduleHost.FailureReason, 0x0c);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6RestoreCallerTest, "Elysium.Arm.V6.NpcRestore.OrdinaryLoadVersusTransition", ElysiumV6NpcTests::Flags)
bool FElysiumV6RestoreCallerTest::RunTest(const FString&)
{
	// 0x1011a7a0 passes old-level presence to 0x1011a710; 0x1011a620 forwards it to slot130.
	// A pedestrian's 0x103a25a0 reset must not run on an ordinary save load.
	FElysiumEntityDefs PedDefs; PedDefs.MapName = TEXT("__v6_restore_caller__");
	FElysiumEntityDef PedDef; PedDef.Classname = TEXT("npc_VPedestrian"); PedDef.TargetName = TEXT("ped");
	PedDefs.Defs.Add(PedDef);
	TArray<ElysiumRng::FState> CallerRng; ElysiumRng::Snapshot(CallerRng); ON_SCOPE_EXIT { ElysiumRng::Restore(CallerRng); };
	FElysiumEntityWorld PedSource(nullptr, nullptr); PedSource.Load(FElysiumEntityDefs(PedDefs)); PedSource.Activate(20);
	auto* SavedPed = static_cast<FElysiumNpcPedestrian*>(PedSource.FindByName(TEXT("ped")));
	if (!TestNotNull(TEXT("pedestrian"), SavedPed)) return false;
	SavedPed->WriteNpcStateRetail(7); SavedPed->LifeState = 2; SavedPed->ThinkSet(nullptr, 0); // 0x103a38c0 corpse NULL
	FElysiumMapSnapshot PedSnapshot; PedSource.Freeze(PedSnapshot);
	FElysiumEntityWorld SaveDestination(nullptr, nullptr); SaveDestination.Load(FElysiumEntityDefs(PedDefs));
	TestEqual(TEXT("ordinary apply"), SaveDestination.ApplySnapshot(PedSnapshot, 20, false), PedSnapshot.Entities.Num());
	auto* SavedCorpse = static_cast<FElysiumNpcPedestrian*>(SaveDestination.FindByName(TEXT("ped")));
	TestTrue(TEXT("ordinary load retains NULL corpse callback"), SavedCorpse->ThinkCallback.IsNone());
	TestEqual(TEXT("ordinary load retains dead state"), SavedCorpse->NpcStateRetail(), 7);
	FElysiumEntityWorld TransitionDestination(nullptr, nullptr); TransitionDestination.Load(FElysiumEntityDefs(PedDefs));
	TestEqual(TEXT("transition apply"), TransitionDestination.ApplySnapshot(PedSnapshot, 100, true), PedSnapshot.Entities.Num());
	auto* ResetPed = static_cast<FElysiumNpcPedestrian*>(TransitionDestination.FindByName(TEXT("ped")));
	TestFalse(TEXT("level reset0 reinstalls startup callback"), ResetPed->ThinkCallback.IsNone());
	TestEqual(TEXT("level reset0 restores alive state"), ResetPed->LifeState, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6GroundContactTest, "Elysium.Arm.V6.NpcRestore.StepContactFlag", ElysiumV6NpcTests::Flags)
bool FElysiumV6GroundContactTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	FElysiumEntityWorld ContactWorld(nullptr, nullptr); ContactWorld.Load(Defs());
	FProbe* ContactNpc = Probe(ContactWorld);
	if (!TestNotNull(TEXT("contact NPC"), ContactNpc)) return false;
	ContactNpc->SetMoveType(4, 0);
	ContactNpc->RecordingMotor.Navigation.bGrounded = true;
	ContactNpc->SyncMovingRecord(); // public per-frame motor seam, no navigation request
	TestTrue(TEXT("0x1003b190 stationary STEP contact sets native flag"), (ContactNpc->Flags & 1) != 0);
	ContactNpc->RecordingMotor.Navigation.bGrounded = false;
	ContactNpc->SyncMovingRecord();
	TestFalse(TEXT("stationary airborne STEP clears native flag"), (ContactNpc->Flags & 1) != 0);
	ContactNpc->SetGroundEntity(ContactWorld.FindByName(TEXT("goal")));
	TestTrue(TEXT("0x100b1420 named ground sets flag"), (ContactNpc->Flags & 1) != 0);
	ContactNpc->SetGroundEntity(nullptr);
	TestFalse(TEXT("0x100b1420 NULL ground clears flag"), (ContactNpc->Flags & 1) != 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6PatrolPoolTeardownTest, "Elysium.Arm.V6.NpcRestore.PatrolPoolTeardown", ElysiumV6NpcTests::Flags)
bool FElysiumV6PatrolPoolTeardownTest::RunTest(const FString&)
{
	using namespace ElysiumV6NpcTests;
	// More reconstructions than the DLL's 32-slot pool. Keep both cells live until
	// teardown, as real tutorial loads do; no test-side reset or explicit path release.
	for (int32 ReloadIndex = 0; ReloadIndex < FElysiumNpc::PatrolPathPoolSize + 8; ++ReloadIndex)
	{
		FElysiumEntityWorld ReloadWorld(nullptr, nullptr);
		ReloadWorld.Load(Defs());
		FProbe* ReloadNpc = Probe(ReloadWorld);
		if (!TestNotNull(TEXT("reload NPC"), ReloadNpc)) return false;
		const int32 Nodes[] = { 1, -1 };
		ReloadNpc->BuildPatrolPath(&ReloadNpc->PatrolPathCell, 1, 0, 0, Nodes, FElysiumNpc::EPatrolPathBuild::Replace);
		ReloadNpc->BuildPatrolPath(&ReloadNpc->PatrolPathHuntCell, 1, 0, 0, Nodes, FElysiumNpc::EPatrolPathBuild::Replace);
		if (!TestNotNull(TEXT("normal cell survives construction"), ReloadNpc->PatrolPathCell.Path)
			|| !TestNotNull(TEXT("hunt cell survives construction"), ReloadNpc->PatrolPathHuntCell.Path)) return false;
		ReloadWorld.Teardown(); // 0x1028d610 -> 0x1028d6e0 -> both0x1029f5d0
	}
	return true;
}
#endif
