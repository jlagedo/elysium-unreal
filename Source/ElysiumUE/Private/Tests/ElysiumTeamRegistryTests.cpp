#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcKernelBindings.h"
#include "Substrate/ElysiumTeamRegistry.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

static constexpr EAutomationTestFlags GElysiumTeamRegistryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamInitialWordTest,
	"Elysium.Arm.TeamRegistry.ConstructorWord", GElysiumTeamRegistryFlags)
bool FElysiumTeamInitialWordTest::RunTest(const FString&)
{
	FElysiumCombatCharacter Character;
	TestEqual(TEXT("0x103272f6: constructor writes unsigned WORD 0xffff"),
		Character.GetTeamSymbol(), uint16(0xffff));
	TestEqual(TEXT("0x1031a600: unkeyed team_name starts empty"), Character.TeamName, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamNameTest,
	"Elysium.Arm.TeamRegistry.NameNormalization", GElysiumTeamRegistryFlags)
bool FElysiumTeamNameTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld TestWorld(nullptr, nullptr, Services.Bundle());
	FElysiumTeamRegistry& Registry = TestWorld.TeamRegistry();
	TestEqual(TEXT("0x10230880: null is invalid"), Registry.FindOrInsert(nullptr), uint16(0xffff));
	TestEqual(TEXT("0x10230880: empty is invalid"), Registry.FindOrInsert(""), uint16(0xffff));
	TestEqual(TEXT("0x10230880: neither invalid name allocated storage"), Registry.GetLiveCount(), uint16(0));
	FElysiumCombatCharacter Character;
	Character.World = &TestWorld;
	Character.TeamName = TEXT("authored_name");
	Character.AddToTeam(TEXT(""));
	TestEqual(TEXT("0x103239a0: empty AddToTeam stores invalid"), Character.GetTeamSymbol(), uint16(0xffff));
	Character.AddToTeam(TEXT("!"));
	TestEqual(TEXT("0x10323a13: sole ! becomes empty, invalid"), Character.GetTeamSymbol(), uint16(0xffff));
	Character.AddToTeam(TEXT("!Arena_Melee"));
	const uint16 ArenaSymbol = Character.GetTeamSymbol();
	TestTrue(TEXT("0x10323a21: named team has a valid WORD"), ArenaSymbol != uint16(0xffff));
	TestEqual(TEXT("0x10230880/0x1024b5e0: lowercase name reuses symbol"),
		Registry.FindOrInsert("arena_melee"), ArenaSymbol);
	Character.AddToTeam(TEXT("!!Arena_Melee"));
	TestEqual(TEXT("0x10323a13: exactly one ! removed"),
		Character.GetTeamSymbol(), Registry.FindOrInsert("!arena_melee"));
	TestTrue(TEXT("0x10323a13: !!name differs from !name"), Character.GetTeamSymbol() != ArenaSymbol);
	Character.AddToTeam(TEXT("!!"));
	TestEqual(TEXT("0x10323a13: !! registers a literal !"), Character.GetTeamSymbol(), Registry.FindOrInsert("!"));
	TestEqual(TEXT("0x103239a0: AddToTeam never writes m_sTeamName"), Character.TeamName, FString(TEXT("authored_name")));
	TestTrue(TEXT("0x1024b5e0: different normalized name allocates a different symbol"),
		Registry.FindOrInsert("other") != ArenaSymbol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamByteLimitTest,
	"Elysium.Arm.TeamRegistry.ByteLimit127", GElysiumTeamRegistryFlags)
bool FElysiumTeamByteLimitTest::RunTest(const FString&)
{
	FElysiumTeamRegistry Registry;
	ANSICHAR First[130];
	ANSICHAR Second[130];
	for (int32 ByteIndex = 0; ByteIndex < 127; ++ByteIndex)
	{
		First[ByteIndex] = 'A';
		Second[ByteIndex] = 'a';
	}
	First[127] = 'x'; First[128] = '\0';
	Second[127] = 'y'; Second[128] = '\0';
	const uint16 LongSymbol = Registry.FindOrInsert(First);
	TestEqual(TEXT("0x10230880: Q_strncpy(0x80) keeps 127 BYTE payload then lowers"),
		Registry.FindOrInsert(Second), LongSymbol);
	TestEqual(TEXT("0x1024b5e0: copied string includes one terminator"), Registry.GetStoredStringBytes(), uint32(128));
	Second[126] = 'b';
	TestTrue(TEXT("0x10230880: byte 127 is retained"), Registry.FindOrInsert(Second) != LongSymbol);

	FElysiumRecordingServices Services;
	FElysiumEntityWorld TestWorld(nullptr, nullptr, Services.Bundle());
	FElysiumCombatCharacter Character;
	Character.World = &TestWorld;
	FString MultibytePrefix;
	for (int32 CharacterIndex = 0; CharacterIndex < 63; ++CharacterIndex)
	{
		MultibytePrefix += FString::Chr(TCHAR(0x00e9)); // 126 UTF-8 bytes.
	}
	Character.AddToTeam(MultibytePrefix + FString::Chr(TCHAR(0x00e9)));
	const uint16 MultibyteSymbol = Character.GetTeamSymbol();
	Character.AddToTeam(MultibytePrefix + FString::Chr(TCHAR(0x00ea)));
	TestEqual(TEXT("0x10230880: cut inside a multibyte character, no wide-character limit/repair"),
		Character.GetTeamSymbol(), MultibyteSymbol);
	TestEqual(TEXT("0x10230880: multibyte payload also uses only 127 bytes"),
		TestWorld.TeamRegistry().GetStoredStringBytes(), uint32(128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamEqualityFilterTest,
	"Elysium.Arm.TeamRegistry.GetterSameTeamFilter", GElysiumTeamRegistryFlags)
bool FElysiumTeamEqualityFilterTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld TestWorld(nullptr, nullptr, Services.Bundle());
	FElysiumCombatCharacter Self;
	FElysiumCombatCharacter Other;
	FElysiumEntity NonCharacter;
	Self.World = &TestWorld;
	Other.World = &TestWorld;
	TestFalse(TEXT("0x10323930: two invalid WORDs are not a team"), Self.IsSameTeam(&Other));
	TestFalse(TEXT("0x10323930: invalid self is not on its own team"), Self.IsSameTeam(&Self));
	Other.AddToTeam(TEXT("team"));
	TestFalse(TEXT("0x10323930: invalid self, valid other"), Self.IsSameTeam(&Other));
	Self.AddToTeam(TEXT("!TEAM"));
	TestEqual(TEXT("0x10323a70: getter reads the stored unsigned WORD"), Self.GetTeamSymbol(), Self.TeamSymbol);
	TestFalse(TEXT("0x10323930: valid self, null other"), Self.IsSameTeam(nullptr));
	TestTrue(TEXT("0x10323930: valid equal symbols"), Self.IsSameTeam(&Other));
	TestTrue(TEXT("0x10323930: valid self equals itself; consumers own their self exception"), Self.IsSameTeam(&Self));
	TestFalse(TEXT("0x103426b0: null entity candidate"), ElysiumTeamFilter(Self, nullptr));
	TestFalse(TEXT("0x10342706: entity with null combat-character self-cast"), ElysiumTeamFilter(Self, &NonCharacter));
	TestTrue(TEXT("0x103426b0: same-team entity candidate"), ElysiumTeamFilter(Self, &Other));
	Other.AddToTeam(TEXT("different"));
	TestFalse(TEXT("0x10323930: valid different symbols"), Self.IsSameTeam(&Other));
	TestFalse(TEXT("0x103426b0: different-team entity candidate"), ElysiumTeamFilter(Self, &Other));
	Other.AddToTeam(TEXT(""));
	TestFalse(TEXT("0x10323930: valid self, invalid other"), Self.IsSameTeam(&Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamLevelHooksTest,
	"Elysium.Arm.TeamRegistry.LevelHooks", GElysiumTeamRegistryFlags)
bool FElysiumTeamLevelHooksTest::RunTest(const FString&)
{
	FElysiumTeamRegistry Registry;
	Registry.FindOrInsert("before_pre");
	Registry.LevelInitPreEntity();
	TestEqual(TEXT("0x10230820/0x1024b880: pre-init resets live count"), Registry.GetLiveCount(), uint16(0));
	TestEqual(TEXT("0x10230820/0x1024b880: pre-init frees string state"), Registry.GetStoredStringBytes(), uint32(0));
	TestEqual(TEXT("0x1024b880: first re-registration starts from zero"), Registry.FindOrInsert("after_pre"), uint16(0));
	TestEqual(TEXT("0x1024b880: pre-init erased lookup tree"), Registry.FindOrInsert("before_pre"), uint16(1));
	const uint32 BeforeDiagnosticBytes = Registry.GetStoredStringBytes();
	Registry.LevelInitPostEntity();
	TestEqual(TEXT("0x102308f0/0x1024b940: diagnostic post-init keeps live count"), Registry.GetLiveCount(), uint16(2));
	TestEqual(TEXT("0x102308f0/0x1024b940: diagnostic post-init keeps strings"), Registry.GetStoredStringBytes(), BeforeDiagnosticBytes);
	TestEqual(TEXT("0x102308f0/0x1024b940: diagnostic post-init keeps lookup"), Registry.FindOrInsert("before_pre"), uint16(1));
	Registry.LevelShutdownPostEntity();
	TestEqual(TEXT("0x10230860/0x1024b880: post-shutdown resets live count"), Registry.GetLiveCount(), uint16(0));
	TestEqual(TEXT("0x10230860/0x1024b880: post-shutdown frees string state"), Registry.GetStoredStringBytes(), uint32(0));
	TestEqual(TEXT("0x1024b880: shutdown erased lookup tree"), Registry.FindOrInsert("after_shutdown"), uint16(0));
	TestEqual(TEXT("0x1024b880: old name must be inserted again"), Registry.FindOrInsert("after_pre"), uint16(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamWorldBoundaryTest,
	"Elysium.Arm.TeamRegistry.WorldBoundaries", GElysiumTeamRegistryFlags)
bool FElysiumTeamWorldBoundaryTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld TestWorld(nullptr, nullptr, Services.Bundle());
	TestWorld.TeamRegistry().FindOrInsert("retired_level");
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("team_boundary");
	TestWorld.Load(MoveTemp(Defs));
	TestEqual(TEXT("0x10230820: world Load clears before entities register"), TestWorld.TeamRegistry().GetLiveCount(), uint16(0));
	TestWorld.TeamRegistry().FindOrInsert("current_level");
	TestWorld.Teardown();
	TestEqual(TEXT("0x10230860: world Teardown clears after EntityList.Empty"), TestWorld.TeamRegistry().GetLiveCount(), uint16(0));
	TestEqual(TEXT("0x1024b880: headless world uses the same string lifecycle"), TestWorld.TeamRegistry().GetStoredStringBytes(), uint32(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamKeyBindingTest,
	"Elysium.Arm.TeamRegistry.GeneratedCombatCharacterKey", GElysiumTeamRegistryFlags)
bool FElysiumTeamKeyBindingTest::RunTest(const FString&)
{
	FElysiumClassDesc Desc;
	ElysiumNpcKernelBindings::AddCombatCharacterFields(Desc);
	const FElysiumFieldAccessor* Binding = Desc.Fields.Find(FName(TEXT("team_name")));
	if (!TestNotNull(TEXT("0x1031a600: team_name belongs to the common combat-character chain"), Binding)) return false;
	TestFalse(TEXT("0x1031a600: flags 6 are SAVE|KEY, without INPUT 8; runtime write gate stays closed"), Binding->bKeyable);
	TestTrue(TEXT("0x1031a600: replay flags 6 retain SAVE"), Binding->bSave);
	FElysiumCombatCharacter Character;
	Binding->Set(Character, FElysiumVariant::String(TEXT("!Arena_Melee")));
	TestEqual(TEXT("0x1031ae41..0x1031ae69: keyfield writes m_sTeamName +0x10ac"), Character.TeamName, FString(TEXT("!Arena_Melee")));
	TestEqual(TEXT("0x10323a21: setting key does not register a symbol"), Character.GetTeamSymbol(), uint16(0xffff));
	const FElysiumClassRegistry& ClassRegistry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* NpcDesc = ClassRegistry.Find(FName(TEXT("npc_VHumanCombatant")));
	const FElysiumClassDesc* PlayerDesc = ClassRegistry.Find(FName(TEXT("player")));
	if (!TestNotNull(TEXT("NPC descriptor exists"), NpcDesc) || !TestNotNull(TEXT("player descriptor exists"), PlayerDesc)) return false;
	TestNotNull(TEXT("0x1031a600: NPC inherits team_name"), ClassRegistry.FindField(*NpcDesc, FName(TEXT("team_name"))));
	TestNotNull(TEXT("0x1031a600: player inherits team_name"), ClassRegistry.FindField(*PlayerDesc, FName(TEXT("team_name"))));
	TestNull(TEXT("0x1031a600: symbol is not a keyfield"), ClassRegistry.FindField(*NpcDesc, FName(TEXT("m_TeamSymbol"))));
	TestTrue(TEXT("0x1031a600: no invented SetTeam input"), ClassRegistry.FindInput(*NpcDesc, FName(TEXT("SetTeam"))) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamRegistrationSitesTest,
	"Elysium.Arm.TeamRegistry.SpawnAndPlayerRestoreSites", GElysiumTeamRegistryFlags)
bool FElysiumTeamRegistrationSitesTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(([]
	{
		FElysiumNpcWorldBuilder Builder(TEXT("team_sites"), 20261004u);
		Builder.AddNpc(TEXT("troika")).Keys.Add(TEXT("team_name"), TEXT("!Arena_Melee"));
		Builder.AddNpc(TEXT("no_team"));
		return Builder;
	})());
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Unnamed = Fixture.Npc(TEXT("no_team"));
	FElysiumPlayer* Player = Fixture.World.FindPlayer();
	if (!TestNotNull(TEXT("Troika exists"), Troika) || !TestNotNull(TEXT("unnamed NPC exists"), Unnamed)
		|| !TestNotNull(TEXT("player exists"), Player)) return false;
	TestEqual(TEXT("0x10298d30/0x10298db6: Troika Spawn registers nonempty team_name"),
		Troika->GetTeamSymbol(), Fixture.World.TeamRegistry().FindOrInsert("arena_melee"));
	TestEqual(TEXT("0x103272f6: placed NPC without team_name remains invalid"), Unnamed->GetTeamSymbol(), uint16(0xffff));
	Unnamed->TeamName = TEXT("base_spawn_team");
	Unnamed->Spawn19CombatCharacterSpawn();
	TestEqual(TEXT("0x10323a90: combat-character Spawn registers nonempty m_sTeamName"),
		Unnamed->GetTeamSymbol(), Fixture.World.TeamRegistry().FindOrInsert("base_spawn_team"));
	TestEqual(TEXT("0x1016d260/0x1016db7e: player Spawn joins literal player"),
		Player->GetTeamSymbol(), Fixture.World.TeamRegistry().FindOrInsert("player"));
	Player->AddToTeam(TEXT("temporary_player_team"));
	FElysiumPlayerRecord Record;
	Player->Dehydrate(Record);
	Player->Hydrate(Record);
	TestEqual(TEXT("0x1016ebd0: successful player Restore joins player after base restore"),
		Player->GetTeamSymbol(), Fixture.World.TeamRegistry().FindOrInsert("player"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamRestoreNamesTest,
	"Elysium.Arm.TeamRegistry.RestoreNamesNotIds", GElysiumTeamRegistryFlags)
bool FElysiumTeamRestoreNamesTest::RunTest(const FString&)
{
	const auto MakeBuilder = []
	{
		FElysiumNpcWorldBuilder Builder(TEXT("team_restore"), 20261004u);
		Builder.AddNpc(TEXT("subject")).Keys.Add(TEXT("team_name"), TEXT("spawn_team"));
		Builder.AddNpc(TEXT("witness")).Keys.Add(TEXT("team_name"), TEXT("spawn_team"));
		return Builder;
	};
	FElysiumNpcWorldFixture Source(MakeBuilder());
	FElysiumNpc* Subject = Source.Npc(TEXT("subject"));
	FElysiumNpc* Witness = Source.Npc(TEXT("witness"));
	if (!TestNotNull(TEXT("source subject exists"), Subject) || !TestNotNull(TEXT("source witness exists"), Witness)) return false;
	Subject->TeamName = TEXT("!restored_team");
	Witness->TeamName = TEXT("RESTORED_TEAM");
	Subject->AddToTeam(Subject->TeamName);
	Witness->AddToTeam(Witness->TeamName);
	const uint16 OldSymbol = Subject->GetTeamSymbol();
	FElysiumMapSnapshot Snapshot;
	Source.World.Freeze(Snapshot);
	FElysiumNpcWorldFixture Destination(MakeBuilder());
	const uint16 RetainedSymbol = Destination.World.TeamRegistry().FindOrInsert("unrelated_live_team");
	Destination.World.TeamRegistry().FindOrInsert("another_live_team");
	TestTrue(TEXT("0x10348890: restore applies after the saved field walk"), Destination.World.ApplySnapshot(Snapshot) > 0);
	FElysiumNpc* Restored = Destination.Npc(TEXT("subject"));
	FElysiumNpc* RestoredWitness = Destination.Npc(TEXT("witness"));
	if (!TestNotNull(TEXT("restored subject exists"), Restored) || !TestNotNull(TEXT("restored witness exists"), RestoredWitness)) return false;
	TestEqual(TEXT("0x10348890: restored name differs from spawn and is retained verbatim"), Restored->TeamName, FString(TEXT("!restored_team")));
	TestEqual(TEXT("0x10348890 -> 0x103239a0: symbol recomputed from restored name"),
		Restored->GetTeamSymbol(), Destination.World.TeamRegistry().FindOrInsert("restored_team"));
	TestTrue(TEXT("0x10348890: numeric IDs are not copied across registries"), Restored->GetTeamSymbol() != OldSymbol);
	TestTrue(TEXT("0x10323930: restored same names share a valid team"), Restored->IsSameTeam(RestoredWitness));
	TestEqual(TEXT("0x10230820/0x10230860: ApplyEntityRecord is not a level boundary"),
		Destination.World.TeamRegistry().FindOrInsert("unrelated_live_team"), RetainedSymbol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTeamPresencePredicateTest,
	"Elysium.Arm.TeamRegistry.PresenceFriendlyPredicate", GElysiumTeamRegistryFlags)
bool FElysiumTeamPresencePredicateTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld TestWorld(nullptr, nullptr, Services.Bundle());
	FElysiumCombatCharacter Target;
	FElysiumCombatCharacter Caster;
	Target.World = &TestWorld;
	Caster.World = &TestWorld;
	// 0x10322b40: independently pin only the recovered classification, not the wider discipline body.
	const auto Friendly = [&Target, &Caster](int32 CasterDisposition)
	{
		return Target.IsSameTeam(&Caster) || CasterDisposition == 3; // D_LI, caster's relation toward target.
	};
	TestFalse(TEXT("0x10322b40: unteamed hate is enemy Presence"), Friendly(1));
	TestTrue(TEXT("0x10322b40: D_LI is friendly even with invalid symbols"), Friendly(3));
	Target.AddToTeam(TEXT("presence"));
	Caster.AddToTeam(TEXT("!PRESENCE"));
	TestTrue(TEXT("0x10322b40/0x10323930: hated same teammate gets friendly Presence"), Friendly(1));
	TestTrue(TEXT("0x10322b40: feared same teammate gets friendly Presence"), Friendly(2));
	Caster.AddToTeam(TEXT("different"));
	TestFalse(TEXT("0x10322b40: different-team fear is enemy Presence"), Friendly(2));
	TestTrue(TEXT("0x10322b40: different-team D_LI still gets friendly Presence"), Friendly(3));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
