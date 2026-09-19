#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumExpr.h"
#include "Substrate/ElysiumConversationPlace.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNodeEntity.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 2: the live `ai_hint` and the node-to-hint lifecycle.
//
// Retail never keeps an `info_node*` authoring entity: `CNodeEnt::Spawn` (`0x102d78d0`) builds a
// `CAI_Hint` of classname `ai_hint` from the row's raw keyvalue block (`FUN_102d2f30`) when the row
// earns one, and removes the authoring entity. These cases stand the def-level half of that and the
// hint entity itself: the class-forced type table (`FUN_102d7d30`), the global hint list's order
// (`0x102d2e30` prepends), the hint inputs and the slot-77/78/119 overrides, the patrol lookup
// (`0x102d2840`), and the read-only keyfields.

static constexpr EAutomationTestFlags GElysiumHintTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumHintTests
{
	FElysiumEntityDef& AddNode(FElysiumNpcWorldBuilder& Builder, const TCHAR* Classname, const TCHAR* Name,
		std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Keys, const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(Classname, Name, Origin);
		for (const TPair<const TCHAR*, const TCHAR*>& Key : Keys)
		{
			Def.Keys.Add(Key.Key, Key.Value);
		}
		return Def;
	}

	FElysiumHint* HintNamed(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		return FElysiumHint::Cast(World.FindByName(Name));
	}

	void Fire(FElysiumEntityWorld& World, FElysiumEntity* Target, const TCHAR* Input,
		const FElysiumVariant& Param = FElysiumVariant())
	{
		World.AcceptInput(Target->Handle, FName(Input), Param, FElysiumEntityHandle::Invalid(),
			FElysiumEntityHandle::Invalid());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintNodeReplacementTest,
	"Elysium.Substrate.Hint.NodeReplacement", GElysiumHintTestFlags)
bool FElysiumHintNodeReplacementTest::RunTest(const FString&)
{
	using namespace ElysiumNodeEntity;
	auto Makes = [](const TCHAR* Classname, std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Keys)
	{
		TMap<FString, FString> Map;
		for (const TPair<const TCHAR*, const TCHAR*>& Key : Keys)
		{
			Map.Add(Key.Key, Key.Value);
		}
		return MakesHint(Classname, Map);
	};

	// `FUN_102d7d30`: the class forces the type whatever the row authored.
	TestTrue(TEXT("a cover corner is a hint with no hinttype at all (forced 10200)"),
		Makes(TEXT("info_node_cover_corner"), {}));
	TestFalse(TEXT("a plain info_node is forced to 0 even when it authors a type"),
		Makes(TEXT("info_node"), { { TEXT("hinttype"), TEXT("100") } }));
	TestTrue(TEXT("...but a Group still makes it a hint"), Makes(TEXT("info_node"), { { TEXT("Group"), TEXT("g") } }));
	TestTrue(TEXT("info_node_tzimisce spawns as info_node"),
		Makes(TEXT("info_node_tzimisce"), { { TEXT("Group"), TEXT("g") } }));
	TestFalse(TEXT("a werewolf hint outside 15000..15018 is forced to 0"),
		Makes(TEXT("info_node_werewolf_hint"), { { TEXT("hinttype"), TEXT("14999") } }));
	TestTrue(TEXT("...and inside it keeps its type"),
		Makes(TEXT("info_node_werewolf_hint"), { { TEXT("hinttype"), TEXT("15018") } }));
	// The standalone set makes a hint only for a non-zero type; a Group does not save it.
	TestFalse(TEXT("an info_hint with type 0 and a Group makes no hint"),
		Makes(TEXT("info_hint"), { { TEXT("hinttype"), TEXT("0") }, { TEXT("Group"), TEXT("g") } }));
	TestTrue(TEXT("an info_hint with a type is one"), Makes(TEXT("info_hint"), { { TEXT("hinttype"), TEXT("10100") } }));
	TestFalse(TEXT("info_node_link is CAI_DynamicLink, never a hint"),
		Makes(TEXT("info_node_link"), { { TEXT("hinttype"), TEXT("100") } }));
	TestFalse(TEXT("an empty Group is no Group"), Makes(TEXT("info_node"), { { TEXT("Group"), TEXT("") } }));

	// The def-level replacement, at load and at runtime creation alike.
	FElysiumNpcWorldBuilder Builder(TEXT("__hint_replacement__"), 7);
	ElysiumHintTests::AddNode(Builder, TEXT("info_node_patrol_point"), TEXT("pp"),
		{ { TEXT("hinttype"), TEXT("10000") }, { TEXT("Group"), TEXT("A1") } });
	ElysiumHintTests::AddNode(Builder, TEXT("info_node"), TEXT("plain"), {});
	FElysiumNpcWorldFixture F(MoveTemp(Builder));

	const FElysiumEntity* Patrol = F.World.FindByName(TEXT("pp"));
	const FElysiumEntity* Plain = F.World.FindByName(TEXT("plain"));
	if (!TestNotNull(TEXT("the patrol point stands"), Patrol) || !TestNotNull(TEXT("the plain node stands"), Plain))
	{
		return false;
	}
	TestEqual(TEXT("a hint-making row lives as ai_hint"), Patrol->Def->Classname, FString(TEXT("ai_hint")));
	TestEqual(TEXT("...and keeps its authored classname"), Patrol->Def->SourceClassname,
		FString(TEXT("info_node_patrol_point")));
	TestNotNull(TEXT("...as the hint class"), FElysiumHint::Cast(Patrol));
	TestEqual(TEXT("a row that makes no hint keeps its classname"), Plain->Def->Classname, FString(TEXT("info_node")));
	TestTrue(TEXT("...and its authored classname is not duplicated"), Plain->Def->SourceClassname.IsEmpty());

	FElysiumEntityDef Runtime;
	Runtime.Classname = TEXT("info_node_cover_low");
	Runtime.TargetName = TEXT("runtime_cover");
	const FElysiumEntityHandle Made = F.World.CreateRuntimeEntityNoSpawn(MoveTemp(Runtime));
	const FElysiumEntity* Created = F.World.Resolve(Made);
	TestTrue(TEXT("a runtime-created node row is replaced too"),
		Created != nullptr && FElysiumHint::Cast(Created) != nullptr);
	TestEqual(TEXT("...and heads the hint list, as the constructor prepends"),
		F.World.HintList().Num() > 0 ? F.World.HintList()[0] : INDEX_NONE, Made.Index);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintListTest,
	"Elysium.Substrate.Hint.List", GElysiumHintTestFlags)
bool FElysiumHintListTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__hint_list__"), 7);
	ElysiumHintTests::AddNode(Builder, TEXT("info_node_cover_low"), TEXT("first"), {}, FVector(10.0, 0.0, 0.0));
	ElysiumHintTests::AddNode(Builder, TEXT("info_node_hint"), TEXT("second"),
		{ { TEXT("hinttype"), TEXT("10100") }, { TEXT("group_id"), TEXT("3") }, { TEXT("target_name"), TEXT("spot") } });
	Builder.AddNpc(TEXT("reader"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));

	const FElysiumHint* First = ElysiumHintTests::HintNamed(F.World, TEXT("first"));
	const FElysiumHint* Second = ElysiumHintTests::HintNamed(F.World, TEXT("second"));
	FElysiumNpc* Reader = F.Npc(TEXT("reader"));
	if (!TestNotNull(TEXT("first"), First) || !TestNotNull(TEXT("second"), Second) || !TestNotNull(TEXT("reader"), Reader))
	{
		return false;
	}
	TestEqual(TEXT("the list holds every hint"), F.World.HintList().Num(), 2);
	TestEqual(TEXT("the last hint the map authored heads the list (0x102d2e30 prepends)"),
		F.World.HintList()[0], Second->Handle.Index);
	TestTrue(TEXT("...and the kernel walks the same list"), Reader->GlobalHintList() == F.World.HintList());

	FElysiumNpc::FHintWords Words;
	TestTrue(TEXT("HintWords reads a live hint"), Reader->HintWords(Second->Handle.Index, Words));
	TestEqual(TEXT("...its entity index"), Words.HintIndex, Second->Handle.Index);
	TestEqual(TEXT("...its authored type"), Words.HintType, 10100);
	TestEqual(TEXT("...its interest-record place name"), Words.TargetName, FString(TEXT("spot")));
	TestEqual(TEXT("...its group folded by CAI_Hint::Spawn (1..32 -> one bit)"), Words.GroupMask, 1 << 2);
	TestEqual(TEXT("...and no network node until story 3"), Words.NodeId, static_cast<int32>(INDEX_NONE));
	FElysiumNpc::FHintWords CoverWords;
	Reader->HintWords(First->Handle.Index, CoverWords);
	TestEqual(TEXT("the cover row's type is the one it authored (none), not the class-forced one"),
		CoverWords.HintType, 0);
	TestEqual(TEXT("an ungrouped hint folds to every group"), CoverWords.GroupMask, -1);
	TestFalse(TEXT("a non-hint index answers false"), Reader->HintWords(Reader->Handle.Index, Words));
	TestEqual(TEXT("FindHintByName resolves a hint's targetname"), Reader->FindHintByName(TEXT("second")),
		Second->Handle.Index);
	TestEqual(TEXT("...and refuses a first match that is not a hint"), Reader->FindHintByName(TEXT("reader")),
		static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintInputsTest,
	"Elysium.Substrate.Hint.Inputs", GElysiumHintTestFlags)
bool FElysiumHintInputsTest::RunTest(const FString&)
{
	using namespace ElysiumHintTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__hint_inputs__"), 7);
	AddNode(Builder, TEXT("info_node_cover_med"), TEXT("cover"), { { TEXT("StartHintDisabled"), TEXT("1") } });
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumHint* Hint = HintNamed(F.World, TEXT("cover"));
	if (!TestNotNull(TEXT("the cover hint stands"), Hint))
	{
		return false;
	}
	TestEqual(TEXT("StartHintDisabled authors m_iDisabled"), Hint->Disabled, 1);

	Fire(F.World, Hint, TEXT("EnableHint"));
	TestEqual(TEXT("EnableHint clears m_iDisabled (0x102d09f0)"), Hint->Disabled, 0);
	Fire(F.World, Hint, TEXT("DisableHint"));
	TestTrue(TEXT("DisableHint hides the hint (0x102d0a20)"), Hint->IsHidden());
	TestEqual(TEXT("...and sets m_iDisabled"), Hint->Disabled, 1);

	// A hidden entity swallows every input but ScriptUnhide* (`FUN_100abc90`'s entry gate).
	Fire(F.World, Hint, TEXT("EnableHint"));
	TestTrue(TEXT("a hidden hint swallows EnableHint"), Hint->IsHidden());
	TestEqual(TEXT("...and stays disabled"), Hint->Disabled, 1);
	// `Q_strnicmp(input, "ScriptUnhide", strlen(input))`: any case-insensitive prefix passes.
	auto Args = [](const TCHAR* Name) { FElysiumInputArgs A; A.Input = FName(Name); return A; };
	TestFalse(TEXT("the gate passes ScriptUnhide"), Hint->SwallowsInput(Args(TEXT("ScriptUnhide"))));
	TestFalse(TEXT("...and a prefix of it, in any case"), Hint->SwallowsInput(Args(TEXT("scriptun"))));
	TestTrue(TEXT("...but not a longer name"), Hint->SwallowsInput(Args(TEXT("ScriptUnhideNow"))));
	TestTrue(TEXT("...nor anything else"), Hint->SwallowsInput(Args(TEXT("Kill"))));
	Fire(F.World, Hint, TEXT("ScriptUnhide"));
	TestFalse(TEXT("ScriptUnhide passes the gate"), Hint->IsHidden());
	TestEqual(TEXT("...and the hint's slot 78 clears m_iDisabled (0x102d0890)"), Hint->Disabled, 0);

	// Slot 119 on a hint is a jump to slot 77: Kill hides and disables, it never removes.
	Fire(F.World, Hint, TEXT("Kill"));
	TestFalse(TEXT("Kill does not kill a hint"), Hint->IsDead());
	TestTrue(TEXT("...it hides it"), Hint->IsHidden());
	TestEqual(TEXT("...and disables it (0x102d0860)"), Hint->Disabled, 1);
	TestTrue(TEXT("...and the hint stays on the list"), F.World.HintList().Contains(Hint->Handle.Index));

	Fire(F.World, Hint, TEXT("ScriptUnhide"));
	Fire(F.World, Hint, TEXT("SetUserData"), FElysiumVariant::String(TEXT("payload")));
	TestEqual(TEXT("SetUserData takes a string (0x102d4060)"), Hint->UserData, FString(TEXT("payload")));
	Fire(F.World, Hint, TEXT("SetUserData"), FElysiumVariant::Int(4));
	TestTrue(TEXT("...and any other type clears it"), Hint->UserData.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintPatrolLookupTest,
	"Elysium.Substrate.Hint.PatrolLookup", GElysiumHintTestFlags)
bool FElysiumHintPatrolLookupTest::RunTest(const FString&)
{
	using namespace ElysiumHintTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__hint_patrol__"), 7);
	// Two points authoring the same exact Group (sp_soc_3's `d1..d4` do this): the later row heads
	// the list, so it wins.
	AddNode(Builder, TEXT("info_node_patrol_point"), TEXT("early"), { { TEXT("hinttype"), TEXT("10000") }, { TEXT("Group"), TEXT("d1") } });
	AddNode(Builder, TEXT("info_node_patrol_point"), TEXT("late"), { { TEXT("hinttype"), TEXT("10000") }, { TEXT("Group"), TEXT("d1") } });
	AddNode(Builder, TEXT("info_node_patrol_point"), TEXT("upper"), { { TEXT("hinttype"), TEXT("10000") }, { TEXT("Group"), TEXT("A1") } });
	AddNode(Builder, TEXT("info_node_hint"), TEXT("typed800"), { { TEXT("hinttype"), TEXT("800") }, { TEXT("Group"), TEXT("q") } });
	AddNode(Builder, TEXT("info_node_hint"), TEXT("wrongtype"), { { TEXT("hinttype"), TEXT("10100") }, { TEXT("Group"), TEXT("w") } });
	Builder.AddNpc(TEXT("walker"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Walker = F.Npc(TEXT("walker"));
	if (!TestNotNull(TEXT("walker"), Walker))
	{
		return false;
	}
	TestTrue(TEXT("a duplicated Group resolves to the later row, which heads the list"),
		Walker->FindPatrolPoint(TEXT("d1")) == HintNamed(F.World, TEXT("late")));
	TestNotNull(TEXT("an exact-case Group resolves"), Walker->FindPatrolPoint(TEXT("A1")));
	TestNull(TEXT("a Group differing only in case does not (0x102d2840 compares bytes)"),
		Walker->FindPatrolPoint(TEXT("a1")));
	TestNotNull(TEXT("type 800 is a patrol hint too"), Walker->FindPatrolPoint(TEXT("q")));
	TestNull(TEXT("any other type is not"), Walker->FindPatrolPoint(TEXT("w")));
	TestNull(TEXT("a targetname is never a patrol token"), Walker->FindPatrolPoint(TEXT("upper")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintKeyfieldPermissionsTest,
	"Elysium.Substrate.Hint.KeyfieldPermissions", GElysiumHintTestFlags)
bool FElysiumHintKeyfieldPermissionsTest::RunTest(const FString&)
{
	// No keyfield of these classes carries the datamap's INPUT bit, so none is writable from Python
	// (`python_bridge.md` § "The write path"); every mutation goes through an input.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	struct FProbe { const TCHAR* Class; const TCHAR* Field; };
	const FProbe Probes[] =
	{
		{ TEXT("ai_hint"), TEXT("HintType") }, { TEXT("ai_hint"), TEXT("Group") },
		{ TEXT("ai_hint"), TEXT("StartHintDisabled") }, { TEXT("ai_hint"), TEXT("target_name") },
		{ TEXT("intersting_place_conversation"), TEXT("enabled") },
		{ TEXT("intersting_place_conversation"), TEXT("interesting_places") },
		{ TEXT("npc_maker_zombie"), TEXT("Flag_ZombieAIType") }, { TEXT("npc_maker_zombie"), TEXT("NPCType") },
	};
	for (const FProbe& Probe : Probes)
	{
		const FElysiumClassDesc* Desc = Reg.Find(FName(Probe.Class));
		if (!TestNotNull(*FString::Printf(TEXT("%s is registered"), Probe.Class), Desc))
		{
			continue;
		}
		const FElysiumFieldAccessor* Field = Reg.FindField(*Desc, FName(Probe.Field));
		if (TestNotNull(*FString::Printf(TEXT("%s.%s is registered"), Probe.Class, Probe.Field), Field))
		{
			TestFalse(*FString::Printf(TEXT("%s.%s is read only"), Probe.Class, Probe.Field), Field->bKeyable);
		}
	}
	const FElysiumClassDesc* Conversation = Reg.Find(FName(TEXT("intersting_place_conversation")));
	TestTrue(TEXT("the conversation place is a class, not a stub"), Conversation != nullptr
		&& !Conversation->bStub && Reg.FindInput(*Conversation, FName(TEXT("PlayOneOffSound"))) != nullptr);
	const FElysiumClassDesc* Zombie = Reg.Find(FName(TEXT("npc_maker_zombie")));
	TestTrue(TEXT("the zombie maker takes the maker's inputs"),
		Zombie != nullptr && Reg.FindInput(*Zombie, FName(TEXT("Spawn"))) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHintBridgeTest,
	"Elysium.Substrate.InfraBridge.Hints", GElysiumHintTestFlags)
bool FElysiumHintBridgeTest::RunTest(const FString&)
{
	using namespace ElysiumHintTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__infra_bridge__"), 7);
	// Two hints sharing a targetname, as `temple.py`'s `Bottleneck_Cover` set does.
	AddNode(Builder, TEXT("info_node_cover_low"), TEXT("Bottleneck_Cover"), { { TEXT("StartHintDisabled"), TEXT("1") } });
	AddNode(Builder, TEXT("info_node_cover_low"), TEXT("Bottleneck_Cover"), { { TEXT("StartHintDisabled"), TEXT("1") } });
	Builder.AddNpc(TEXT("reader"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Reader = F.Npc(TEXT("reader"));
	const TArray<int32> Hints = F.World.HintList();
	if (!TestNotNull(TEXT("reader"), Reader) || !TestEqual(TEXT("two hints"), Hints.Num(), 2))
	{
		return false;
	}
	FElysiumHint* A = FElysiumHint::Cast(F.World.Entities()[Hints[1]].Get());
	FElysiumHint* B = FElysiumHint::Cast(F.World.Entities()[Hints[0]].Get());

	// A script call on a name resolves the FIRST match and calls it by handle: no fan-out.
	ElysiumExpr::FEnv Env;
	Env.Ctx.World = &F.World;
	ElysiumExpr::Exec(TEXT("Bottleneck_Cover.EnableHint()"), Env);
	TestFalse(TEXT("the script call evaluates"), Env.bError);
	TestTrue(TEXT("...and reaches only the first name match, the lower index"), A->Disabled == 0 && B->Disabled == 1);
	FElysiumNpc::FHintWords Words;
	Reader->HintWords(A->Disabled == 0 ? A->Handle.Index : B->Handle.Index, Words);
	TestEqual(TEXT("the NPC's hint query observes the input's write"), Words.Disabled, 0);

	// A queued delivery by name fans out over every match.
	F.World.EnqueueInput(TEXT("Bottleneck_Cover"), FName(TEXT("EnableHint")), FElysiumVariant(), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	F.World.Tick(F.World.NowSeconds() + 0.05);
	TestTrue(TEXT("a queued name delivery reaches both"), A->Disabled == 0 && B->Disabled == 0);
	return true;
}

#endif
