// Content-free Substrate automation: the game-sound stimulus bus — the third event kind.
//
// Two halves are under test and they are deliberately separate:
//
//   * the BUS — radius and occlusion resolution against `sound_volume_table.txt`, the
//     stealth-reduction hook, the bounded retention window, and the serial cursor a consumer polls
//     with. Every table here is built in code, so these cases are this runtime's statement of the
//     contract rather than a reading of the export.
//   * the PRODUCERS — that a ranged shot commit and a typed damage commit actually raise the
//     stimulus, driven through the same headless world the weapon and damage suites use. A bus with
//     nothing pushing into it is not a hearing surface.
//
// The authored facts: `docs/vtmb/npc-ai/senses.md` → "Visual and auditory input" and
// `docs/vtmb/stealth.md` → "Auditory stealth".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumGameSoundTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;

	// --- The synthetic sound-volume table -----------------------------------------------------
	// The levels and categories this suite exercises, authored exactly as the shipped file does.
	// Category names go in FOLDED, which is how the KV reader stores every block key.
	FElysiumSoundVolumeTable MakeVolumeTable()
	{
		FElysiumSoundVolumeTable Table;

		auto AddLevel = [&Table](int32 Index, float RadiusUnits, bool bOccludable)
		{
			FElysiumSoundLevel Row;
			Row.Level = Index;
			Row.RadiusUnits = RadiusUnits;
			Row.bOccludable = bOccludable;
			Table.Levels.Add(Row);
		};
		AddLevel(FElysiumSoundVolumeTable::SilentLevel, 0.f, false);
		AddLevel(FElysiumSoundVolumeTable::QuietLevel, 180.f, true);
		AddLevel(FElysiumSoundVolumeTable::NormalLevel, 240.f, true);
		AddLevel(FElysiumSoundVolumeTable::LoudLevel, 1200.f, false);
		AddLevel(FElysiumSoundVolumeTable::PlayerStealthLevel, 120.f, true);
		AddLevel(FElysiumSoundVolumeTable::FeedingLevel, 240.f, true);

		auto AddCategory = [&Table](const TCHAR* Name, int32 Level)
		{
			FElysiumSoundCategory Row;
			Row.Name = FString(Name).ToLower();
			Row.Level = Level;
			Table.Categories.Add(MoveTemp(Row));
		};
		AddCategory(TEXT("PLAYER_GUNSHOT_BASE"), FElysiumSoundVolumeTable::LoudLevel);
		AddCategory(TEXT("NPC_TAKE_DAMAGE"), FElysiumSoundVolumeTable::NormalLevel);
		AddCategory(TEXT("PLAYER_AGGRESSIVE_FEED"), FElysiumSoundVolumeTable::FeedingLevel);
		AddCategory(TEXT("PLAYER_FOOTSTEP_SNEAK"), FElysiumSoundVolumeTable::QuietLevel);

		Table.Misc.Add(TEXT("player_run_speed"), TEXT("128.0"));
		Table.Reindex();
		return Table;
	}

	float Units(float Cm) { return Cm / ElysiumMove::U; }

	bool NearlyEqual(float A, float B) { return FMath::Abs(A - B) < 0.05f; }

	// --- The producer fixture ------------------------------------------------------------------
	// Classnames are suite-local (`_gamesound_`): `ElysiumItems::Install` registers a class once per
	// process and never unregisters, so a name shared with another suite would resolve to whichever
	// suite ran first.
	const TCHAR* const GPistol = TEXT("item_w_gamesound_pistol");
	const TCHAR* const GKnife  = TEXT("item_w_gamesound_knife");

	FElysiumItemTable MakeItemTable()
	{
		FElysiumItemTable Table;

		FElysiumItemDef Pistol;
		Pistol.Classname = GPistol;
		Pistol.PrintName = GPistol;
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.bWieldable = true;
		Pistol.AmmoType = TEXT("GameSoundRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		{
			FElysiumWeaponMode Mode;
			Mode.Tag = TEXT("Primary");
			Mode.TypeName = TEXT("Attack");
			Mode.Type = EElysiumWeaponModeType::Attack;
			Mode.Dmg = TEXT("2 Lethal Ranged_Combat DMG_BULLET");
			Mode.BaseLethality = 9;
			Mode.AttackRate = 0.4f;
			Mode.AmmoCost = 1;
			Mode.AmmoFired = 1;
			Pistol.Modes.Add(MoveTemp(Mode));
		}
		Table.Items.Add(MoveTemp(Pistol));

		// A melee weapon, so the "only a RANGED commit is a gunshot" half is assertable.
		FElysiumItemDef Knife;
		Knife.Classname = GKnife;
		Knife.PrintName = GKnife;
		Knife.Type = EElysiumItemType::WeaponMelee;
		Knife.bWieldable = true;
		{
			FElysiumWeaponMode Mode;
			Mode.Tag = TEXT("Primary");
			Mode.TypeName = TEXT("Attack");
			Mode.Type = EElysiumWeaponModeType::Attack;
			Mode.Dmg = TEXT("3 Lethal Close_Combat_Melee DMG_SLASH");
			Mode.BaseLethality = 12;
			Mode.AttackRate = 1.0f;
			Knife.Modes.Add(MoveTemp(Mode));
		}
		Table.Items.Add(MoveTemp(Knife));

		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeProducerDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__gamesound_test__");

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VPedestrian");
		Victim.TargetName = TEXT("victim");
		Victim.Origin = FVector(100.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Victim));
		return Defs;
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	FElysiumDmg ResolvedDmg(int32 Amount)
	{
		FElysiumDmg Dmg;
		Dmg.Family = EElysiumDmgFamily::Bashing;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = Amount;
		Dmg.RolledSuccesses = Amount;
		Dmg.Remainder = Amount;
		Dmg.AppliedDamage = Amount;
		Dmg.bResolved = true;
		return Dmg;
	}

	// Every event of one category still in the window.
	TArray<FElysiumGameSoundEvent> Of(const FElysiumGameSoundBus& Bus, const FName& Category)
	{
		TArray<FElysiumGameSoundEvent> Out;
		for (const FElysiumGameSoundEvent& Event : Bus.Retained())
		{
			if (Event.Category == Category) { Out.Add(Event); }
		}
		return Out;
	}
}


// Resolution: what a category costs, what an explicit radius overrides, and what an
// unknown category does.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGameSoundResolveTest,
	"Elysium.Substrate.GameSound.Resolve", GElysiumTestFlags)
bool FElysiumGameSoundResolveTest::RunTest(const FString&)
{
	const FElysiumSoundVolumeTable Table = MakeVolumeTable();

	// --- The table answers the radius AND the occlusion policy -------------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Shot;
		Shot.Position = FVector(10.0, 20.0, 30.0);
		Shot.Category = ElysiumGameSounds::Gunshot();
		const FElysiumGameSoundEvent Fired = Bus.Emit(Shot, /*Now*/ 1.0);

		TestTrue(TEXT("a gunshot carries the loud level's 1200 units"),
			NearlyEqual(Units(Fired.RadiusCm), 1200.f));
		TestFalse(TEXT("...and the loud level is not occluded by geometry"), Fired.bOccludable);
		TestTrue(TEXT("the stimulus keeps the position it was raised at"),
			Fired.Position.Equals(Shot.Position));
		TestEqual(TEXT("the clock it was raised on is the substrate's"), Fired.Time, 1.0);
		TestTrue(TEXT("serials start at one, so zero can mean 'nothing seen yet'"),
			Fired.Serial == 1);

		FElysiumGameSoundRequest Quiet;
		Quiet.Category = FName(TEXT("PLAYER_FOOTSTEP_SNEAK"));
		const FElysiumGameSoundEvent Step = Bus.Emit(Quiet, 1.0);
		TestTrue(TEXT("a quiet category carries 180 units"), NearlyEqual(Units(Step.RadiusCm), 180.f));
		TestTrue(TEXT("...and the quiet level IS occludable"), Step.bOccludable);
	}

	// --- An explicit radius overrides the reach, never the occlusion policy --------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Explicit;
		Explicit.Category = ElysiumGameSounds::Gunshot();
		Explicit.RadiusCm = 500.0f;
		const FElysiumGameSoundEvent Fired = Bus.Emit(Explicit, 0.0);
		TestTrue(TEXT("an explicit radius is taken verbatim, in centimetres"),
			NearlyEqual(Fired.RadiusCm, 500.0f));
		TestFalse(TEXT("...while occlusion still comes from the category's own level"),
			Fired.bOccludable);
	}

	// --- The stealth hook subtracts at insertion and floors at zero ---------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Sneak;
		Sneak.Category = FName(TEXT("PLAYER_FOOTSTEP_SNEAK"));   // 180 units
		Sneak.StealthHearingReductionCm = 80.f * ElysiumMove::U;
		TestTrue(TEXT("the stealth reduction comes off the authored radius"),
			NearlyEqual(Units(Bus.Emit(Sneak, 0.0).RadiusCm), 100.f));

		Sneak.StealthHearingReductionCm = 5000.f * ElysiumMove::U;
		TestTrue(TEXT("...and the result floors at zero rather than going negative"),
			NearlyEqual(Bus.Emit(Sneak, 0.0).RadiusCm, 0.0f));

		// The default every producer passes today. Stated as a case so the seam is visible: when the
		// stealth surface lands, this is the assertion that has to change.
		FElysiumGameSoundRequest Plain;
		Plain.Category = FName(TEXT("PLAYER_FOOTSTEP_SNEAK"));
		TestTrue(TEXT("an unreduced emission is the whole authored radius"),
			NearlyEqual(Units(Bus.Emit(Plain, 0.0).RadiusCm), 180.f));
	}

	// --- An unknown category warns ONCE and takes the normal level ----------------------------
	{
		AddExpectedError(TEXT("is not named in sound_volume_table.txt"),
			EAutomationExpectedErrorFlags::Contains, 1);

		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Unknown;
		Unknown.Category = FName(TEXT("NOT_IN_THE_TABLE"));
		const FElysiumGameSoundEvent First = Bus.Emit(Unknown, 0.0);
		TestTrue(TEXT("an unknown category resolves to the normal level's 240 units"),
			NearlyEqual(Units(First.RadiusCm), 240.f));
		TestTrue(TEXT("...with the normal level's occlusion policy"), First.bOccludable);

		// The latch: the same category again is silent, which is what keeps a per-frame producer
		// from burying the one authoring fact under thousands of copies of it.
		for (int32 i = 0; i < 20; ++i)
		{
			Bus.Emit(Unknown, 0.0);
		}
		TestEqual(TEXT("every emission still lands"), Bus.NumRetained(), 21);
	}

	// --- No table at all is the supported headless case, and it is SILENT ----------------------
	{
		FElysiumGameSoundBus Bus;   // never given a table
		FElysiumGameSoundRequest Request;
		Request.Category = ElysiumGameSounds::Gunshot();
		const FElysiumGameSoundEvent Fired = Bus.Emit(Request, 0.0);
		TestTrue(TEXT("an unbound bus falls open to the normal level"),
			NearlyEqual(Units(Fired.RadiusCm), 240.f));
		TestTrue(TEXT("...occludable, as the normal level is"), Fired.bOccludable);
		// Nothing is warned here on purpose: the rulebook owns the missing-export diagnostic, and
		// repeating it per category would bury it. `AddExpectedError` above would fail this case if
		// a warning were raised, because it is scoped to the whole test.
	}
	return true;
}


// The window: a serial cursor a consumer polls with, and the two bounds that keep the
// buffer from growing.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGameSoundWindowTest,
	"Elysium.Substrate.GameSound.Window", GElysiumTestFlags)
bool FElysiumGameSoundWindowTest::RunTest(const FString&)
{
	const FElysiumSoundVolumeTable Table = MakeVolumeTable();

	// --- The cursor: everything once, and never twice -----------------------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Request;
		Request.Category = ElysiumGameSounds::NpcTakeDamage();

		uint64 Cursor = 0;
		TestEqual(TEXT("a fresh bus has nothing to consume"), Bus.EventsSince(Cursor).Num(), 0);
		TestTrue(TEXT("...and no serial yet"), Bus.LastSerial() == 0);

		Bus.Emit(Request, 0.0);
		Bus.Emit(Request, 0.1);
		TArrayView<const FElysiumGameSoundEvent> Pending = Bus.EventsSince(Cursor);
		if (TestEqual(TEXT("a consumer sees both emissions"), Pending.Num(), 2))
		{
			TestTrue(TEXT("oldest first"), Pending[0].Serial == 1);
			Cursor = Pending.Last().Serial;
		}
		TestEqual(TEXT("the same consumer polling again sees nothing twice"),
			Bus.EventsSince(Cursor).Num(), 0);

		Bus.Emit(Request, 0.2);
		TestEqual(TEXT("only what arrived after the cursor"), Bus.EventsSince(Cursor).Num(), 1);
		TestEqual(TEXT("a consumer starting at the head skips the backlog entirely"),
			Bus.EventsSince(Bus.LastSerial()).Num(), 0);
	}

	// --- Retention: a stimulus older than the window is gone, and a late consumer sees only
	//     what survived ------------------------------------------------------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Request;
		Request.Category = ElysiumGameSounds::NpcTakeDamage();

		Bus.Emit(Request, 0.0);
		Bus.Emit(Request, 0.5);
		TestEqual(TEXT("both are retained inside the window"), Bus.NumRetained(), 2);

		// Well past the retention window: the next emission evicts everything before it.
		Bus.Emit(Request, FElysiumGameSoundBus::RetentionSeconds + 1.0);
		TestEqual(TEXT("the window drops what aged out"), Bus.NumRetained(), 1);
		TestEqual(TEXT("...and says how many it dropped"), Bus.NumEvicted(), 2);
		TestTrue(TEXT("serials keep counting past an eviction"), Bus.NumEmitted() == 3);

		// A consumer that has seen nothing does NOT get the evicted pair back.
		TestEqual(TEXT("a late consumer sees only the retained window"),
			Bus.EventsSince(0).Num(), 1);
	}

	// --- The count cap holds when the clock does not move at all -------------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);

		FElysiumGameSoundRequest Request;
		Request.Category = ElysiumGameSounds::Gunshot();
		for (int32 i = 0; i < FElysiumGameSoundBus::MaxRetained + 30; ++i)
		{
			Bus.Emit(Request, 0.0);   // one instant, so only the count cap can bound it
		}
		TestEqual(TEXT("the window never exceeds its cap"), Bus.NumRetained(),
			static_cast<int32>(FElysiumGameSoundBus::MaxRetained));
		TestTrue(TEXT("and the survivors are the NEWEST ones"),
			Bus.Retained().Last().Serial == Bus.LastSerial());
	}

	// --- Reset clears the window and the serial ------------------------------------------------
	{
		FElysiumGameSoundBus Bus;
		Bus.SetVolumeTable(&Table);
		FElysiumGameSoundRequest Request;
		Request.Category = ElysiumGameSounds::Gunshot();
		Bus.Emit(Request, 0.0);
		Bus.Reset();
		TestEqual(TEXT("a reset window is empty"), Bus.NumRetained(), 0);
		TestTrue(TEXT("...and its cursor restarts"), Bus.LastSerial() == 0);
	}
	return true;
}


// The producers. A bus nothing pushes into is not a hearing surface — these drive the
// real commit paths and assert the stimulus came out of them.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGameSoundProducerTest,
	"Elysium.Substrate.GameSound.Producers", GElysiumTestFlags)
bool FElysiumGameSoundProducerTest::RunTest(const FString&)
{
	const FElysiumItemTable Items = MakeItemTable();
	ElysiumItems::Install(Items);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Items); };

	const FElysiumSoundVolumeTable Volumes = MakeVolumeTable();

	// --- A ranged shot commit raises the gunshot; a melee one does not -------------------------
	{
		ElysiumRng::SeedAll(5150);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.GameSounds().SetVolumeTable(&Volumes);
		World.Load(MakeProducerDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 200);
		Player->Origin = FVector(500.0, 250.0, 0.0);

		const FElysiumEntityHandle PistolHandle =
			Player->Inventory.GiveNamedItem(*Player, GPistol);
		FElysiumEntity* PistolEnt = World.Resolve(PistolHandle);
		FElysiumItem* PistolItem = PistolEnt ? PistolEnt->AsItem() : nullptr;
		FElysiumWeapon* Pistol = PistolItem ? PistolItem->AsWeapon() : nullptr;
		if (!TestNotNull(TEXT("the pistol is granted as a weapon"), Pistol))
		{
			return false;
		}

		Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle);
		TestEqual(TEXT("accepting a shot makes no noise — the commit does"),
			Of(World.GameSounds(), ElysiumGameSounds::Gunshot()).Num(), 0);

		World.Tick(0.3);   // the queued commit lands
		const TArray<FElysiumGameSoundEvent> Shots =
			Of(World.GameSounds(), ElysiumGameSounds::Gunshot());
		if (TestEqual(TEXT("the shot commit raises exactly one gunshot"), Shots.Num(), 1))
		{
			TestTrue(TEXT("it is raised where the shooter stands"),
				Shots[0].Position.Equals(FVector(500.0, 250.0, 0.0)));
			TestTrue(TEXT("the shooter owns it"), Shots[0].Source == Player->Handle);
			TestTrue(TEXT("and it carries the table's loud reach"),
				NearlyEqual(Units(Shots[0].RadiusCm), 1200.f));
			TestFalse(TEXT("...which no wall stops"), Shots[0].bOccludable);
		}

		// The same hit also raised the victim's own damage stimulus, from the health commit.
		const TArray<FElysiumGameSoundEvent> Hurt =
			Of(World.GameSounds(), ElysiumGameSounds::NpcTakeDamage());
		if (TestEqual(TEXT("the damage commit raises NPC_TAKE_DAMAGE"), Hurt.Num(), 1))
		{
			TestTrue(TEXT("at the VICTIM, not the shooter"),
				Hurt[0].Position.Equals(Victim->Origin));
			TestTrue(TEXT("owned by the victim, because it is the body making the noise"),
				Hurt[0].Source == Victim->Handle);
			TestTrue(TEXT("at the normal level's 240 units"),
				NearlyEqual(Units(Hurt[0].RadiusCm), 240.f));
		}

		// A melee commit is a hit, not a gunshot.
		const int32 ShotsBefore = Of(World.GameSounds(), ElysiumGameSounds::Gunshot()).Num();
		const FElysiumEntityHandle KnifeHandle = Player->Inventory.GiveNamedItem(*Player, GKnife);
		FElysiumEntity* KnifeEnt = World.Resolve(KnifeHandle);
		FElysiumItem* KnifeItem = KnifeEnt ? KnifeEnt->AsItem() : nullptr;
		if (FElysiumWeapon* Knife = KnifeItem ? KnifeItem->AsWeapon() : nullptr)
		{
			Knife->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle);
			World.Tick(1.5);
			TestEqual(TEXT("a melee commit raises no gunshot"),
				Of(World.GameSounds(), ElysiumGameSounds::Gunshot()).Num(), ShotsBefore);
		}
	}

	// --- The health commit is the producer, not the descriptor ---------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.GameSounds().SetVolumeTable(&Volumes);
		World.Load(MakeProducerDefs());
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 100);

		Victim->CommitDamage(ResolvedDmg(5));
		TestEqual(TEXT("a landed commit raises one stimulus"),
			Of(World.GameSounds(), ElysiumGameSounds::NpcTakeDamage()).Num(), 1);

		// Nothing to commit is not a hit, so it is not a noise either.
		Victim->CommitDamage(ResolvedDmg(0));
		TestEqual(TEXT("a zero-damage commit raises nothing"),
			Of(World.GameSounds(), ElysiumGameSounds::NpcTakeDamage()).Num(), 1);
	}
	return true;
}

}   // namespace ElysiumGameSoundTests

#endif // WITH_DEV_AUTOMATION_TESTS
