// Content-free Substrate automation: the melee block family (LIFE5) — its pure rules, the two
// reactions a blocked contact produces, the damage that still lands behind them, and the one channel
// they and the flinch have to share.
//
// Every fact asserted here is `docs/vtmb/combat-and-damage.md` § "Block and stagger reactions": the
// defender's class-3/other-classes fork, the attacker's authored blocked reaction and its
// `ACT_BLOCKED_REACTION_RIGHT` fallback, `WasMeleeBlocked`'s three terms, and "blocked does not mean
// zero damage". Nothing loads a rulebook, a mesh or an export.
//
// **The margin table a live contact classifies against is bindable**, and cases that need one take
// `ElysiumMeleeTest::FRulesFixture` — a fabricated `Melee_Reactions` block installed as the
// process-wide fallback every combat leaf already reads when no rulebook subsystem is in reach. A
// case WITHOUT it classifies `Unclassified`, which names no block activity, and that is the
// fail-safe rather than a limit.
//
// One genuine limit remains, and it is about the rule rather than the scaffolding:
// `WasMeleeBlocked`'s NON-player fork reads the stored classification, so the producer cases drive
// the PLAYER fork — which is the one retail answers from the live block intent rather than from a
// roll. `.Rule` asserts the selection as a rule and `.Ownership` asserts the composition, because
// what each is about is one rule's arithmetic rather than the transaction around it.
//
// The ATTACKER half — the authored column, its key, the `ACT_BLOCKED_REACTION_RIGHT` fallback, the
// three terms of `WasMeleeBlocked` and the ordering against the damage exit — is driven end to end.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumUserCmd.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumBlockReactionTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;
	using ED = EElysiumMeleeDefenderReaction;

	// Classnames are suite-local: `ElysiumItems::Install` registers a class once per process and
	// never unregisters, so a name shared with another suite resolves to whichever ran first.
	const TCHAR* const GFists = TEXT("item_w_block_fists");
	const TCHAR* const GStick = TEXT("item_w_block_stick");   // a melee weapon that inflicts nothing
	const TCHAR* const GPistol = TEXT("item_w_block_pistol");

	// The label the recording seam answers every activity with, and therefore the label the swing
	// resolves to — which is the key the blocked-reaction column is authored against.
	const TCHAR* const GSwingLabel = TEXT("swing_long");
	const TCHAR* const GSwingBank = TEXT("cast_bank");
	// The bone the swing's authored contact segment is stated in, and which the fixture places.
	const TCHAR* const GSwingBone = TEXT("Bip01 R Hand");

	// Far enough past every commit estimate the fixture can produce. The recording seam answers a
	// one-second clip for every activity, so a melee contact lands at 0.5/0.7 s and a shot at 0.5 s.
	constexpr double GContactTick = 1.0;

	FElysiumWeaponMode MakeMode(const TCHAR* Dmg, int32 BaseLethality, float AttackRate,
		int32 AmmoCost = 0)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = AttackRate;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeBlockTable()
	{
		FElysiumItemTable Table;

		FElysiumItemDef Fists;
		Fists.Classname = GFists;
		Fists.PrintName = GFists;
		Fists.Type = EElysiumItemType::WeaponMelee;
		Fists.bWieldable = true;
		Fists.Modes.Add(MakeMode(TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), /*Lethality*/ 8, 0.5f));
		Table.Items.Add(MoveTemp(Fists));

		// Lethality 0 against a defence that fails safe at 0 is a margin of exactly 0 — the
		// "impact exits without damage" branch, which the block reactions must run AHEAD of.
		FElysiumItemDef Stick;
		Stick.Classname = GStick;
		Stick.PrintName = GStick;
		Stick.Type = EElysiumItemType::WeaponMelee;
		Stick.bWieldable = true;
		Stick.Modes.Add(MakeMode(TEXT("2 Bashing Close_Combat_Melee DMG_CLUB"), /*Lethality*/ 0, 0.5f));
		Table.Items.Add(MoveTemp(Stick));

		FElysiumItemDef Pistol;
		Pistol.Classname = GPistol;
		Pistol.PrintName = GPistol;
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.bWieldable = true;
		Pistol.AmmoType = TEXT("BlockTestRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.Modes.Add(MakeMode(TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 9, 0.4f, /*Ammo_Cost*/ 1));
		Table.Items.Add(MoveTemp(Pistol));

		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeBlockTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__block_reaction_test__");

		// The attacker: a cast body with a model, because the blocked reaction is played on ITS
		// body and a character with no visual reacts with nothing.
		FElysiumEntityDef Attacker;
		Attacker.Classname = TEXT("npc_VHumanCombatant");
		Attacker.TargetName = TEXT("attacker");
		Attacker.Origin = FVector(100.0f, 0.0f, 0.0f);
		Attacker.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Attacker));

		// A second cast body for the flinch-ownership cases, out of every swing's reach.
		FElysiumEntityDef Bystander;
		Bystander.Classname = TEXT("npc_VHumanCombatant");
		Bystander.TargetName = TEXT("bystander");
		Bystander.Origin = FVector(0.0f, 4000.0f, 0.0f);
		Bystander.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Bystander));
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

	FString FirstCall(const FElysiumRecordingServices& Services, const TCHAR* Prefix)
	{
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(Prefix))
			{
				return Call;
			}
		}
		return FString();
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

	// The whole fixture: an armed, grounded, blocking player at the origin facing +X, and a cast
	// attacker standing on that +X within melee reach and facing back.
	struct FBlockFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Attacker = nullptr;
		// The third term of the block input predicate, set before `Stand`. True is the standing case
		// every other assertion here needs; false is the one term a test has to ask for.
		bool bOnGround = true;

		// Whether the player is given a body of its own. Off by default — most cases here drive the
		// ATTACKER's reactions, and a player with no visual reacts with nothing, which is the state
		// those cases were written against. The held-block cases need one, because the pose the hold
		// stands is played on it.
		bool bPlayerHasBody = false;

		// `PlayerYawDegrees` is the SOURCE yaw written onto the entity: 0 faces the attacker
		// (frontal), 180 turns the player's back on it.
		bool Stand(FAutomationTestBase& Test, float PlayerYawDegrees, bool bBlockHeld,
			const TCHAR* PlayerWeapon = GFists)
		{
			// The reaction producer draws its weighted variant off the session's own Reaction
			// stream, so the suite seeds it — the idiom every stream-reading suite uses.
			ElysiumRng::SeedAll(0x424C4B31);
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = GSwingLabel;
			Services.ResolvedNpcActivityClip = GSwingLabel;
			Services.ResolvedNpcActivityOwner = GSwingBank;
			Services.bNpcOneShotsPlay = true;
			Services.bPlayerOnGround = bOnGround;

			// The swing's contact is the swept walk over the clip's own authored records, so the
			// fixture has to author one: a phase standing on the swing clip, the bone its segment is
			// stated in, and a window over the middle of the cycle.
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
			World->Load(MakeBlockTestDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			if (Player != nullptr && bPlayerHasBody)
			{
				// The ordinary door a map or a save uses; the recording seam answers it with a body.
				Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
			}
			Attacker = FindCharacter(*World, TEXT("attacker"));
			if (!Test.TestNotNull(TEXT("the player exists"), Player)
				|| !Test.TestNotNull(TEXT("the attacker exists"), Attacker))
			{
				return false;
			}
			if (!Test.TestNotNull(TEXT("the attacker carries a body to react with"), Attacker->Visual))
			{
				return false;
			}
			SeedHealth(*Player, 100);
			SeedHealth(*Attacker, 100);
			Player->Origin = FVector::ZeroVector;
			Player->Angles = FVector(0.0f, PlayerYawDegrees, 0.0f);
			// Written directly, as `PlaceFacing` does in the weapon suite: going through
			// `SetRuntimeOrigin` would drive a pawn the headless case does not have. Source yaw 180
			// points the attacker back down -X, at the player standing on the origin.
			Attacker->Origin = FVector(100.0f, 0.0f, 0.0f);
			Attacker->Angles = FVector(0.0f, 180.0f, 0.0f);

			// The block predicate's other two terms: a melee weapon in hand, and the button.
			if (!Test.TestNotNull(TEXT("the player is armed"),
				GiveWeapon(*Player, PlayerWeapon)))
			{
				return false;
			}
			World->SetPlayerButtons(
				bBlockHeld ? static_cast<uint64>(EElysiumButton::SecondaryAtk) : 0);
			World->RunPlayerThink(0.0);
			// Whom the sweep reaches. Geometry is the seam's answer here; the substrate still decides
			// eligibility, the opposed record and every reaction behind it.
			Services.SwingContacts = { Player->Handle };
			Services.Calls.Reset();
			return true;
		}

		// One accepted melee swing by the attacker, carried through to its contact. Two walked
		// frames: the first is the swing's first live frame, where the opposed roll and the notice
		// are staged, and the second carries the cycle into the authored window where the sweep
		// lands. The tick behind them retires the transaction at its recovery deadline.
		void Swing()
		{
			if (FElysiumWeapon* Weapon = GiveWeapon(*Attacker, GFists))
			{
				Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary);
			}
			// The clock moves FIRST, so the base-channel holds the contact takes are measured against
			// a `now` inside the swing rather than against zero. The walk does not advance the clock —
			// it is a per-frame pass over a cycle, not a scheduler.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlockReactionRuleTest,
	"Elysium.Substrate.BlockReaction.Rule", GElysiumTestFlags)
bool FElysiumBlockReactionRuleTest::RunTest(const FString&)
{
	// --- The defender's fork: class 3 against everything else ---------------------------------
	{
		TestEqual(TEXT("the block-stagger band is VtMB's whole melee stagger"),
			FString(ElysiumReactions::BlockActivityFor(ED::BlockStagger)),
			FString(TEXT("ACT_BLOCK_HEAVY")));
		TestEqual(TEXT("the block band plays the ordinary block"),
			FString(ElysiumReactions::BlockActivityFor(ED::Block)),
			FString(TEXT("ACT_BLOCK")));
		// The two dodge bands are blocked classes too: retail branches on class 3 alone and sends
		// every OTHER blocked class to `ACT_BLOCK`.
		TestEqual(TEXT("dodge is a blocked class"),
			FString(ElysiumReactions::BlockActivityFor(ED::Dodge)), FString(TEXT("ACT_BLOCK")));
		TestEqual(TEXT("...and so is dodge attack"),
			FString(ElysiumReactions::BlockActivityFor(ED::DodgeAttack)), FString(TEXT("ACT_BLOCK")));
		// Neither of these is a blocked class, so neither names a block activity. A knockback takes
		// the separate normal-hit callback; an unclassified record means no margin table loaded.
		TestNull(TEXT("hit/knockback names no block activity"),
			ElysiumReactions::BlockActivityFor(ED::HitKnockback));
		TestNull(TEXT("and neither does an unclassified record"),
			ElysiumReactions::BlockActivityFor(ED::Unclassified));
	}

	// --- The attacker's fallback: one literal, not a coin --------------------------------------
	{
		// Spelled out here on purpose. The left/right split is AUTHORED per swing sequence, so a
		// fallback that picked a side would invent a direction retail never derives.
		TestEqual(TEXT("the blocked-reaction fallback is the RIGHT one"),
			FString(ElysiumReactions::DefaultBlockedReaction),
			FString(TEXT("ACT_BLOCKED_REACTION_RIGHT")));
	}

	// --- The frontal test ----------------------------------------------------------------------
	{
		const FVector Victim = FVector::ZeroVector;
		// The victim faces +X throughout, so an attacker on +X is dead ahead.
		TestTrue(TEXT("an attacker dead ahead is frontal"),
			ElysiumReactions::IsFrontalContact(FVector(100.0f, 0.0f, 0.0f), Victim, 0.0f));
		// The two seam angles, both inclusive: the hemisphere's own edge is inside it.
		TestTrue(TEXT("an attacker at exactly +90 (the victim's right) is frontal"),
			ElysiumReactions::IsFrontalContact(FVector(0.0f, 100.0f, 0.0f), Victim, 0.0f));
		TestTrue(TEXT("an attacker at exactly -90 (the victim's left) is frontal"),
			ElysiumReactions::IsFrontalContact(FVector(0.0f, -100.0f, 0.0f), Victim, 0.0f));
		// One degree past either edge is behind the victim and cannot be blocked.
		TestFalse(TEXT("one degree past the right edge is behind"),
			ElysiumReactions::IsFrontalContact(
				FVector(-FMath::Tan(FMath::DegreesToRadians(1.0f)) * 100.0f, 100.0f, 0.0f),
				Victim, 0.0f));
		TestFalse(TEXT("one degree past the left edge is behind"),
			ElysiumReactions::IsFrontalContact(
				FVector(-FMath::Tan(FMath::DegreesToRadians(1.0f)) * 100.0f, -100.0f, 0.0f),
				Victim, 0.0f));
		// The +-180 seam — directly behind, which is the case the whole test exists to refuse.
		TestFalse(TEXT("an attacker directly behind is not frontal"),
			ElysiumReactions::IsFrontalContact(FVector(-100.0f, 0.0f, 0.0f), Victim, 0.0f));

		// It is measured in the victim's OWN frame, so turning the victim moves the hemisphere with
		// it rather than moving the attacker.
		TestTrue(TEXT("the same attacker is frontal once the victim turns to face it"),
			ElysiumReactions::IsFrontalContact(FVector(-100.0f, 0.0f, 0.0f), Victim, 180.0f));

		// Coincident origins name no direction, exactly as the flinch derivation refuses them.
		TestFalse(TEXT("two bodies at one point have no facing relationship"),
			ElysiumReactions::IsFrontalContact(Victim, Victim, 0.0f));
		TestFalse(TEXT("...and neither does an attacker directly overhead"),
			ElysiumReactions::IsFrontalContact(FVector(0.0f, 0.0f, 300.0f), Victim, 0.0f));
	}

	return true;
}

// =====================================================================================
// The producer: what a blocked contact actually plays
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlockReactionProducerTest,
	"Elysium.Substrate.BlockReaction.Producer", GElysiumTestFlags)
bool FElysiumBlockReactionProducerTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeBlockTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The authored column wins ---------------------------------------------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, /*PlayerYaw*/ 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		TestTrue(TEXT("the player's block intent stands"), F.Player->IsActivelyBlocking());
		// The swing's own sequence descriptor names a LEFT blocked reaction.
		F.Services.BlockedReactionByClip.Add(FString(GSwingLabel).ToLower(),
			TEXT("ACT_BLOCKED_REACTION_LEFT"));

		F.Swing();

		// The column is read off the ATTACKING BODY's stem plus the swing's label — the same pair
		// `PlayNpcClip` addresses a clip with.
		TestTrue(TEXT("the blocked-reaction column is asked for, keyed by the swing's own label"),
			Saw(F.Services, TEXT("NpcClipBlockedReaction"), GSwingLabel));
		TestTrue(TEXT("the attacker resolves the AUTHORED reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION_LEFT")));
		TestFalse(TEXT("...and not the fallback"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION_RIGHT")));
		// The reaction BAND, not the one-shot slot: a reaction replaces the pose the body holds.
		TestTrue(TEXT("it is played on the reaction route"),
			Saw(F.Services, TEXT("PlayNpcOneShot"), TEXT("route=reaction")));
		TestTrue(TEXT("...in the reaction band"),
			Saw(F.Services, TEXT("PlayNpcOneShot"), TEXT("prio=reaction")));
		// A blocked reaction is authored, never derived from a direction: the request carries no
		// steering angle even though the attacker is standing off to one side of nothing.
		TestTrue(TEXT("the request carries no hit yaw — the reaction is authored, not directional"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("hit=0.0")));
	}

	// --- A swing whose sequence names none takes retail's one literal ---------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		// `BlockedReactionByClip` deliberately left empty: most shipped sequences author no column,
		// and an empty answer is the ordinary case rather than a lookup failure.
		F.Swing();

		TestTrue(TEXT("an unauthored column falls back to ACT_BLOCKED_REACTION_RIGHT"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION_RIGHT")));
	}

	// --- The frontal term ------------------------------------------------------------------------
	{
		FBlockFixture F;
		// Facing away: the blow arrives from behind, which `WasMeleeBlocked` refuses.
		if (!F.Stand(*this, /*PlayerYaw*/ 180.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		TestTrue(TEXT("the intent still stands — facing is not part of the input predicate"),
			F.Player->IsActivelyBlocking());
		F.Swing();

		TestFalse(TEXT("a blow from behind produces no blocked reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
		TestFalse(TEXT("...and asks for no blocked-reaction column at all"),
			Saw(F.Services, TEXT("NpcClipBlockedReaction"), GSwingLabel));
	}

	// --- The intent term -------------------------------------------------------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		TestFalse(TEXT("an unheld button is not a block"), F.Player->IsActivelyBlocking());
		F.Swing();

		TestFalse(TEXT("a frontal contact on a player who is not blocking produces no reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
	}

	// --- The ground term -------------------------------------------------------------------------
	{
		FBlockFixture F;
		// Everything else is identical to the first case; only the floor goes away. Retail's input
		// predicate at `0x10160ec0` requires ground contact, so an airborne player holding the button
		// is not blocking however it is facing or armed.
		F.bOnGround = false;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		TestFalse(TEXT("an airborne player is not blocking, however the button is held"),
			F.Player->IsActivelyBlocking());
		// The term is asked of the SEAM, not assumed: `Stand` clears the call log after its own
		// think, so the button is re-edged to arm the next one — which is also what proves the edge
		// arming reaches the deadline-driven player think at all.
		F.World->SetPlayerButtons(0);
		F.World->SetPlayerButtons(static_cast<uint64>(EElysiumButton::SecondaryAtk));
		F.World->RunPlayerThink(0.0);
		TestTrue(TEXT("...and the mover's own ground fact is what answered it"),
			Saw(F.Services, TEXT("IsPlayerOnGround"), TEXT("false")));
		F.Swing();

		TestFalse(TEXT("...so a frontal contact produces no blocked reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
	}

	// --- The capability term: there is no firearm analogue ---------------------------------------
	{
		FBlockFixture F;
		// Everything else is identical to the first case; only the hand changes. Retail's mask is
		// `0x18000`, and a ranged weapon's `0x2000` fails it.
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true, GPistol))
		{
			return false;
		}
		TestFalse(TEXT("a firearm cannot block: capability 0x2000 fails the 0x18000 mask"),
			F.Player->IsActivelyBlocking());
		F.Swing();

		TestFalse(TEXT("...so a frontal contact produces no blocked reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
	}

	// --- A ranged impact carries no block family at all -------------------------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		F.Services.BlockedReactionByClip.Add(FString(GSwingLabel).ToLower(),
			TEXT("ACT_BLOCKED_REACTION_LEFT"));

		FElysiumWeapon* Pistol = GiveWeapon(*F.Attacker, GPistol);
		if (!TestNotNull(TEXT("the attacker draws a firearm"), Pistol))
		{
			return false;
		}
		Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, F.Player->Handle);
		F.World->Tick(GContactTick);

		TestTrue(TEXT("the shot lands"), DamageTaken(*F.Player) > 0);
		TestFalse(TEXT("a shot is never blocked, however the target is standing"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
		TestFalse(TEXT("...and asks for no blocked-reaction column"),
			Saw(F.Services, TEXT("NpcClipBlockedReaction"), GSwingLabel));
	}

	return true;
}

// =====================================================================================
// Blocked does not mean zero damage
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlockReactionDamageTest,
	"Elysium.Substrate.BlockReaction.Damage", GElysiumTestFlags)
bool FElysiumBlockReactionDamageTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeBlockTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- A positive margin: the block plays AND the damage commits -------------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		F.Swing();

		TestTrue(TEXT("the blocked reaction played"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
		// A NUMBER rather than a bool: fists carry lethality 8 against a defence that fails safe at
		// 0, and `8 x (BaseDamage 2 + modifier 0) x 1.0` is 16. A block that zeroed the damage would
		// still pass a "took some damage" assertion after a one-line regression.
		TestEqual(TEXT("...and the damage still commits in full — blocked is not zero damage"),
			DamageTaken(*F.Player), 16);
	}

	// --- A margin of zero: the block plays, and nothing is spent ---------------------------------
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		// The stick inflicts nothing, so the record is not damaging. The reactions still run: what
		// decides the damage is the margin, and what decides the reaction is the block.
		if (FElysiumWeapon* Stick = GiveWeapon(*F.Attacker, GStick))
		{
			Stick->AttackIntent(FElysiumWeapon::EIntent::Primary);
		}
		F.World->Tick(GContactTick);
		F.Services.BodyClipPhase.Cycle = 0.0f;
		F.World->AdvanceMeleeSwings(0.02f);
		F.Services.BodyClipPhase.Cycle = 0.50f;
		F.World->AdvanceMeleeSwings(0.02f);

		TestTrue(TEXT("a non-damaging blocked contact still plays the blocked reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_BLOCKED_REACTION")));
		TestEqual(TEXT("...and spends nothing"), DamageTaken(*F.Player), 0);
	}

	return true;
}

// =====================================================================================
// One channel, one claim: the flinch yields to a contact reaction
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlockReactionOwnershipTest,
	"Elysium.Substrate.BlockReaction.Ownership", GElysiumTestFlags)
bool FElysiumBlockReactionOwnershipTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeBlockTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// The composition a blocked, damaging contact performs on the DEFENDER, assembled from the two
	// producers it calls: the block reaction claims the base channel, and the damage that follows it
	// commits without flinching over the pose already on screen.
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		FElysiumCombatCharacter* Defender = FindCharacter(*F.World, TEXT("bystander"));
		if (!TestNotNull(TEXT("the bystander exists"), Defender)
			|| !TestNotNull(TEXT("...with a body"), Defender->Visual))
		{
			return false;
		}
		SeedHealth(*Defender, 100);

		// 1. The block reaction, exactly as the contact plays it.
		FElysiumReactionPlayRequest Block;
		Block.Activity = ElysiumReactions::BlockActivityFor(ED::BlockStagger);
		Block.bAllowFallbackLadder = true;
		float Held = 0.0f;
		if (!TestTrue(TEXT("the block reaction plays"),
			Defender->PlayReactionActivity(Block, &Held)))
		{
			return false;
		}
		TestTrue(TEXT("...and the seam answers how long its claim stands"), Held > 0.0f);
		Defender->HoldBaseForMeleeReaction(0.0 + static_cast<double>(Held));
		TestEqual(TEXT("one reaction has played so far"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);

		// 2. The damage the same contact commits, inside that hold.
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();
		F.Services.Calls.Reset();

		FElysiumDmg Dmg = ResolvedDmg(5);
		Dmg.Source = F.Attacker->Handle;
		Defender->CommitDamage(Dmg);

		TestEqual(TEXT("the damage lands"), DamageTaken(*Defender), 5);
		TestEqual(TEXT("the defender plays exactly ONE reaction for the whole contact — the block"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		// **The stream, and this is the load-bearing half.** A yielded flinch is not a flinch, so it
		// must draw nothing: otherwise how many blocks a fight contained would silently reshuffle
		// every reaction after it.
		TestEqual(TEXT("...and a yielded flinch advances the Reaction stream by nothing"),
			Rng.GetCurrentSeed(), SeedBefore);

		// 3. Past the hold, an unrelated blow flinches normally — the yield is a WINDOW, not a mute,
		// and what closes it is the clock. The world is advanced past the deadline rather than the
		// field being cleared: the gate reads `World->NowSeconds()`, so expiry is the thing under
		// test and a hold that never expired would still pass an assignment.
		const double PastTheHold = Defender->MeleeReactionHoldsBaseUntil + 0.5;
		F.World->Tick(PastTheHold);
		TestTrue(TEXT("the clock has passed the hold's deadline"),
			F.World->NowSeconds() > Defender->MeleeReactionHoldsBaseUntil);
		F.Services.Calls.Reset();
		Defender->CommitDamage(Dmg);
		TestEqual(TEXT("once the hold has expired the next hit flinches again"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);
		TestTrue(TEXT("...as a hit reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_HIT_")));
	}

	// The ATTACKER's half of the same rule, driven end to end through a real blocked contact. Its
	// blocked reaction takes the same base channel the defender's block does, so it has to take the
	// same hold — otherwise one side of one exchange owns the channel on different terms from the
	// other, and a counter-blow lands a flinch over a recoil still on screen.
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ true))
		{
			return false;
		}
		F.Swing();

		if (!TestTrue(TEXT("the attacker played its blocked reaction"),
			Saw(F.Services, TEXT("PlayNpcOneShot"), TEXT("route=reaction"))))
		{
			return false;
		}
		TestTrue(TEXT("...and holds the base channel for as long as it plays"),
			F.Attacker->MeleeReactionHoldsBaseUntil > F.World->NowSeconds());

		// A counter-blow inside that hold: the damage lands, the pose does not change.
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();
		F.Services.Calls.Reset();

		FElysiumDmg Counter = ResolvedDmg(5);
		Counter.Source = F.Player->Handle;
		F.Attacker->CommitDamage(Counter);

		TestEqual(TEXT("the counter-blow lands on the attacker"), DamageTaken(*F.Attacker), 5);
		TestEqual(TEXT("...but no flinch plays over its own blocked reaction"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		TestEqual(TEXT("...and the yielded flinch advances the Reaction stream by nothing"),
			Rng.GetCurrentSeed(), SeedBefore);

		// And past the hold, the attacker flinches like anyone else.
		F.World->Tick(F.Attacker->MeleeReactionHoldsBaseUntil + 0.5);
		F.Services.Calls.Reset();
		F.Attacker->CommitDamage(Counter);
		TestEqual(TEXT("once its hold expires the attacker flinches normally"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);
		TestTrue(TEXT("...as a hit reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_HIT_")));
	}

	// The hold never shortens: a second reaction landing inside one extends it rather than cutting
	// the pose already on screen short.
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		FElysiumCombatCharacter* Defender = FindCharacter(*F.World, TEXT("bystander"));
		if (!TestNotNull(TEXT("the bystander exists"), Defender))
		{
			return false;
		}
		Defender->HoldBaseForMeleeReaction(5.0);
		Defender->HoldBaseForMeleeReaction(2.0);
		TestEqual(TEXT("an earlier deadline never shortens a standing hold"),
			Defender->MeleeReactionHoldsBaseUntil, 5.0);
		Defender->HoldBaseForMeleeReaction(9.0);
		TestEqual(TEXT("...and a later one extends it"),
			Defender->MeleeReactionHoldsBaseUntil, 9.0);
	}

	return true;
}

// =====================================================================================
// The held block pose: a claim a PREDICATE releases, not a duration
// =====================================================================================
//
// `docs/vtmb/combat-and-damage.md` § "Block and stagger reactions": retail holds the ideal activity
// `ACT_PREBLOCK` for as long as the input classification stands, and the clip's own loop bit keeps it
// on screen. What is asserted here is that our pose has the same LIFE as our classification — the
// same predicate starts both, the same predicate ends both, and no clock is involved on either side.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlockReactionHeldPoseTest,
	"Elysium.Substrate.BlockReaction.HeldPose", GElysiumTestFlags)
bool FElysiumBlockReactionHeldPoseTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeBlockTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The hold: requested on the edge, released by the button ----------------------------------
	{
		FBlockFixture F;
		F.bPlayerHasBody = true;
		// Standing with the button UP, so the press below is a real rising edge through the input
		// pass rather than fixture setup the call log has already been cleared of.
		if (!F.Stand(*this, /*PlayerYaw*/ 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		if (!TestNotNull(TEXT("the player carries a body to hold a pose on"), F.Player->Visual))
		{
			return false;
		}
		TestFalse(TEXT("nothing is blocked before the button"), F.Player->IsActivelyBlocking());
		TestFalse(TEXT("...and no claim is outstanding"), F.Player->IsHoldingReaction());

		F.World->SetPlayerButtons(static_cast<uint64>(EElysiumButton::SecondaryAtk));
		F.World->RunPlayerThink(0.0);

		TestTrue(TEXT("the classification stands on the rising edge"),
			F.Player->IsActivelyBlocking());
		TestTrue(TEXT("...and the pose is claimed with it"), F.Player->IsHoldingReaction());
		TestTrue(TEXT("the pose asked for is retail's ideal activity"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_PREBLOCK")));

		const FString Played = FirstCall(F.Services, TEXT("PlayNpcOneShot"));
		if (!TestFalse(TEXT("a pose was played"), Played.IsEmpty()))
		{
			return false;
		}
		// **The three facts that make it a hold rather than a one-shot.** The band is the reaction's
		// (it replaces the base pose), the release condition is the predicate (so the claim carries no
		// duration at all), and the clip repeats (so an unbounded hold is not a terminal frame).
		TestTrue(TEXT("...in the reaction band"), Played.Contains(TEXT("prio=reaction")));
		TestTrue(TEXT("...released by a predicate rather than by a clock"),
			Played.Contains(TEXT("release=predicate")));
		TestTrue(TEXT("...and repeating for as long as it stands"), Played.Contains(TEXT("loop=1")));

		// **Time does not end it, and that is the whole repair.** The world is run well past any clip
		// this fixture can produce — the recording seam answers one second — and the pose is asked for
		// exactly once over the whole span. Two things are being asserted at once: the claim did not
		// expire, and the producer did not re-request. The second is load-bearing on its own: every
		// request draws the weighted variant off the Reaction stream, so a cadence would make that
		// stream's position a function of how long a button was held.
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedAfterPress = Rng.GetCurrentSeed();
		for (int32 Step = 1; Step <= 40; ++Step)
		{
			const double Now = static_cast<double>(Step) * 0.25;
			F.World->Tick(Now);
			F.World->RunPlayerThink(Now);
		}
		TestEqual(TEXT("the pose is requested once for the whole hold"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);
		TestEqual(TEXT("...so ten seconds of holding advance the Reaction stream by nothing"),
			Rng.GetCurrentSeed(), SeedAfterPress);
		TestTrue(TEXT("...and the claim is still standing"), F.Player->IsHoldingReaction());
		TestTrue(TEXT("...as is the classification it was taken for"),
			F.Player->IsActivelyBlocking());

		// --- The release: the falling edge, and nothing else ---------------------------------------
		F.Services.Calls.Reset();
		F.World->SetPlayerButtons(0);
		F.World->RunPlayerThink(10.5);

		TestFalse(TEXT("the classification falls with the button"), F.Player->IsActivelyBlocking());
		TestFalse(TEXT("...and the claim goes back with it"), F.Player->IsHoldingReaction());
		TestTrue(TEXT("...through the seam's own release door"),
			Saw(F.Services, TEXT("ReleaseNpcReaction"), TEXT("body=1")));
		TestEqual(TEXT("releasing plays nothing"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);

		// Idempotent: a second release with nothing held reaches no body at all, which is what keeps a
		// think that re-evaluates a false predicate from spamming the seam.
		F.Services.Calls.Reset();
		F.Player->ReleaseHeldReaction();
		TestEqual(TEXT("releasing a claim that is not held touches nothing"),
			CountCalls(F.Services, TEXT("ReleaseNpcReaction")), 0);
	}

	// --- The flinch yields to a HELD claim exactly as it does to a timed one -----------------------
	{
		FBlockFixture F;
		F.bPlayerHasBody = true;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		SeedHealth(*F.Player, 100);
		F.World->SetPlayerButtons(static_cast<uint64>(EElysiumButton::SecondaryAtk));
		F.World->RunPlayerThink(0.0);
		if (!TestTrue(TEXT("the block pose is held"), F.Player->IsHoldingReaction()))
		{
			return false;
		}

		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedBefore = Rng.GetCurrentSeed();
		F.Services.Calls.Reset();

		FElysiumDmg Dmg = ResolvedDmg(5);
		Dmg.Source = F.Attacker->Handle;
		F.Player->CommitDamage(Dmg);

		TestEqual(TEXT("the damage lands on the blocking player"), DamageTaken(*F.Player), 5);
		TestEqual(TEXT("...and no flinch plays over the block pose it is holding"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		// The same load-bearing half the timed yield has: a yielded flinch is not a flinch, so it must
		// draw nothing.
		TestEqual(TEXT("...advancing the Reaction stream by nothing"), Rng.GetCurrentSeed(),
			SeedBefore);

		// Let the button go, and the very next blow flinches like anyone else. The yield is the
		// CLAIM's, not the player's — which is why releasing it is all it takes.
		F.World->SetPlayerButtons(0);
		F.World->RunPlayerThink(1.0);
		F.Services.Calls.Reset();
		F.Player->CommitDamage(Dmg);
		TestEqual(TEXT("once the block is released the next hit flinches"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);
		TestTrue(TEXT("...as a hit reaction"),
			Saw(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_HIT_")));
	}

	// --- Death releases a held claim ---------------------------------------------------------------
	//
	// On the shared combat-character door rather than through the player leaf, because that is where
	// the release lives: a dead character re-checks no predicate, so a claim left standing could never
	// be given back and would park the base channel its own death schedule is about to ask for.
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		FElysiumCombatCharacter* Holder = FindCharacter(*F.World, TEXT("bystander"));
		if (!TestNotNull(TEXT("the bystander exists"), Holder)
			|| !TestNotNull(TEXT("...with a body"), Holder->Visual))
		{
			return false;
		}
		SeedHealth(*Holder, 100);

		FElysiumReactionPlayRequest Held;
		Held.Activity = TEXT("ACT_PREBLOCK");
		Held.bAllowFallbackLadder = true;
		Held.Release = EElysiumReactionRelease::Predicate;
		if (!TestTrue(TEXT("the held pose plays"), Holder->PlayReactionActivity(Held)))
		{
			return false;
		}
		TestTrue(TEXT("...and the claim is outstanding"), Holder->IsHoldingReaction());

		F.Services.Calls.Reset();
		Holder->OnKilled();
		TestFalse(TEXT("death gives the held claim back"), Holder->IsHoldingReaction());
		TestTrue(TEXT("...through the seam's own release door"),
			Saw(F.Services, TEXT("ReleaseNpcReaction"), TEXT("body=1")));
	}

	// --- Preemption and resume: the pose falls and comes BACK with the classification ---------------
	//
	// The case the hold exists for, and the one that reopens the divergence if it regresses. An equal
	// band takes the base channel on `>=`, so the first blocked hit's own `ACT_BLOCK` displaces the
	// block pose while the button is still down. Retail re-derives the ideal activity every frame, so
	// its pose and its classification fall and resume together; the poll on this producer's own think
	// is that. **And the resume must not draw** — the weighted pick ran once, on the rising edge.
	{
		FBlockFixture F;
		F.bPlayerHasBody = true;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		F.World->SetPlayerButtons(static_cast<uint64>(EElysiumButton::SecondaryAtk));
		F.World->RunPlayerThink(0.0);
		if (!TestTrue(TEXT("the block pose is held"), F.Player->IsHoldingReaction()))
		{
			return false;
		}
		const FString FirstPlay = FirstCall(F.Services, TEXT("PlayNpcOneShot"));

		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
		const int32 SeedAtHold = Rng.GetCurrentSeed();

		// 1. Displaced by a reaction that is still playing. The claim is gone; the classification is
		//    NOT — the button never moved — and the resume must wait rather than cut the preemptor off.
		F.Services.PreemptNpcReaction();
		F.Services.bNpcReactionChannelFree = false;
		F.Services.Calls.Reset();
		F.World->RunPlayerThink(0.2);

		TestTrue(TEXT("the classification survives the preemption"),
			F.Player->IsActivelyBlocking());
		TestFalse(TEXT("...while the claim does not"), F.Player->IsHoldingReaction());
		TestEqual(TEXT("a contested channel is not fought over"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		// The poll happened — the producer asked the seam rather than trusting its own stale flag,
		// which is the staleness this re-derivation exists to close.
		TestTrue(TEXT("...but it IS polled, and the seam is what answers"),
			Saw(F.Services, TEXT("QueryNpcReactionHold"), TEXT("displaced")));

		// A second contested think re-asks and still does not fight, which is what makes this a retry
		// rather than a one-shot give-up.
		F.Services.Calls.Reset();
		F.World->RunPlayerThink(0.4);
		TestEqual(TEXT("...and it is retried, still without re-requesting"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);

		// 2. The preemptor's own claim expires. The channel comes free and the pose goes back on.
		F.Services.bNpcReactionChannelFree = true;
		F.Services.Calls.Reset();
		F.World->RunPlayerThink(0.6);

		TestTrue(TEXT("the pose resumes once the channel is free"), F.Player->IsHoldingReaction());
		TestEqual(TEXT("...as exactly one play"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 1);
		// **The invariant.** The resume replays the CELL the rising edge resolved: no translation
		// request, and therefore no weighted pick off the Reaction stream. A resume that re-resolved
		// would make that stream's position a function of how many times a body was hit while blocking.
		TestEqual(TEXT("a resume resolves nothing"),
			CountCalls(F.Services, TEXT("ResolveNpcActivityClip")), 0);
		TestEqual(TEXT("...and advances the Reaction stream by nothing"), Rng.GetCurrentSeed(),
			SeedAtHold);
		// The same cell, on the same terms, character for character.
		const FString Resumed = FirstCall(F.Services, TEXT("PlayNpcOneShot"));
		TestEqual(TEXT("...and it is the same play the rising edge made"), Resumed, FirstPlay);

		// 3. And the button still ends it, through the ordinary release door.
		F.Services.Calls.Reset();
		F.World->SetPlayerButtons(0);
		F.World->RunPlayerThink(0.8);
		TestFalse(TEXT("the released button ends the resumed hold too"),
			F.Player->IsHoldingReaction());
		TestTrue(TEXT("...through the seam's own release door"),
			Saw(F.Services, TEXT("ReleaseNpcReaction"), TEXT("body=1")));
	}

	// --- A displaced hold is not resumed after its predicate ends -----------------------------------
	//
	// The other half of the same rule: releasing drops the cached cell, so a poll arriving after the
	// button is up has nothing to put back. Without it, a block released during a blocked reaction
	// would re-appear by itself the moment that reaction ended.
	{
		FBlockFixture F;
		F.bPlayerHasBody = true;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		F.World->SetPlayerButtons(static_cast<uint64>(EElysiumButton::SecondaryAtk));
		F.World->RunPlayerThink(0.0);
		if (!TestTrue(TEXT("the block pose is held"), F.Player->IsHoldingReaction()))
		{
			return false;
		}

		// Displaced, then released while displaced.
		F.Services.PreemptNpcReaction();
		F.Services.bNpcReactionChannelFree = false;
		F.World->RunPlayerThink(0.2);
		F.Services.Calls.Reset();
		F.World->SetPlayerButtons(0);
		F.World->RunPlayerThink(0.4);
		TestFalse(TEXT("the classification is down"), F.Player->IsActivelyBlocking());
		// Nothing to give back — the claim was already gone — so the release does not report one it
		// does not own.
		TestEqual(TEXT("a displaced hold releases no claim it does not own"),
			CountCalls(F.Services, TEXT("ReleaseNpcReaction")), 0);

		// The channel comes free afterwards, and nothing comes back with it.
		F.Services.bNpcReactionChannelFree = true;
		F.Services.Calls.Reset();
		F.World->RunPlayerThink(0.6);
		F.World->RunPlayerThink(0.8);
		TestEqual(TEXT("a hold whose predicate ended is never resumed"),
			CountCalls(F.Services, TEXT("PlayNpcOneShot")), 0);
		TestFalse(TEXT("...and no claim stands"), F.Player->IsHoldingReaction());
	}

	// --- A refused play leaves nothing outstanding --------------------------------------------------
	//
	// The claim is the character's to give back only once the seam has actually taken it. A producer
	// that latched on the request instead would leave a body that resolved no clip yielding its
	// flinches forever to a claim nobody holds.
	{
		FBlockFixture F;
		if (!F.Stand(*this, 0.0f, /*bBlockHeld*/ false))
		{
			return false;
		}
		FElysiumCombatCharacter* Holder = FindCharacter(*F.World, TEXT("bystander"));
		if (!TestNotNull(TEXT("the bystander exists"), Holder))
		{
			return false;
		}
		F.Services.bNpcOneShotsPlay = false;   // the body resolves a clip and the host refuses it

		FElysiumReactionPlayRequest Held;
		Held.Activity = TEXT("ACT_PREBLOCK");
		Held.bAllowFallbackLadder = true;
		Held.Release = EElysiumReactionRelease::Predicate;
		TestFalse(TEXT("a refused play is a refusal"), Holder->PlayReactionActivity(Held));
		TestFalse(TEXT("...and leaves no claim outstanding"), Holder->IsHoldingReaction());
	}

	return true;
}

}   // namespace ElysiumBlockReactionTests

#endif   // WITH_DEV_AUTOMATION_TESTS
