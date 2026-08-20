// Content-free Substrate automation: the PLAYER's attack producer (LIFE5) — the combat button
// field the world retains, the press edges it derives, the refusals that stand ahead of the weapon,
// and `FElysiumWeapon::ItemPostFrame` turning one frame of buttons into at most one transaction.
//
// The facts asserted here are `docs/vtmb/player-entity.md` § "Recovered `PostThink` body" (the
// controlled-use first refusal, then `ItemPostFrame`), `docs/vtmb/combat-and-damage.md` § "Weapon
// and input surface" (`CWeaponMelee::ItemPostFrame` reads `m_afButtonPressed` — the PRESS EDGE — so
// one press is one swing and holding produces nothing further; `CWeaponUnarmed` authors no attack
// table) and `docs/vtmb/controls.md` § "Attack, block and weapon commands" (`+wpn_secondaryatk` is
// a held composite — a block bit plus the ordinary secondary-fire route).
//
// The three CHOSEN answers this suite pins, each stated beside the code that makes it:
//  * RE-A1 — the secondary route acts on the PRESS EDGE, not a held bit.
//  * RE-A2 — the four tests run empty-record, primary, secondary, reload, and the frame reports the
//    first non-`Idle` verdict.
//  * RE-A6 — `IsMobile()` stands in for retail's `m_iPlayerLocked`.
//
// Nothing here loads a rulebook, a mesh or an export: the item catalogue is built in code for the
// length of one case. Every rating therefore fails safe at 0, which fixes the melee playback rate
// at `MeleePlaybackRate(0)` = 0.70 and makes every deadline below arithmetic rather than a guess.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumComboChain.h"   // ElysiumCombo::In* — the file's own button numbering
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInteraction.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumUserCmd.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumSignData.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumPlayerAttackTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A captured `+use` session, so the controlled-use refusal can be driven without a terminal or a
// container. Registered at module load like every other class; the classname is suite-local because
// `FElysiumClassRegistrar` registers once per process.
class FElysiumAttackUseSessionEntity final : public FElysiumEntity
{
public:
	virtual bool IsUsable() const override { return true; }
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext&) override
	{
		return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
	}
};

static TUniquePtr<FElysiumEntity> MakeAttackUseSessionEntity()
{
	return MakeUnique<FElysiumAttackUseSessionEntity>();
}

static FElysiumClassRegistrar GAttackUseSessionRegistrar(
	TEXT("test_attack_use_session"), ElysiumBaseClassName(), &MakeAttackUseSessionEntity,
	[](FElysiumClassDesc&) {});

namespace
{
	using EC = EElysiumTraitContainer;
	using EB = EElysiumWeaponButton;

	// The button field's own bits, as the controller's combat mask hands them over.
	constexpr uint64 GAtk    = static_cast<uint64>(EElysiumButton::Attack);
	constexpr uint64 GAtk2   = static_cast<uint64>(EElysiumButton::Attack2);
	constexpr uint64 GSecAtk = static_cast<uint64>(EElysiumButton::SecondaryAtk);
	constexpr uint64 GReload = static_cast<uint64>(EElysiumButton::Reload);
	// The movement half of the same field. Direction-keyed attack selection reads exactly these, so
	// they are part of what the controller forwards rather than a separate channel.
	constexpr uint64 GFwd    = static_cast<uint64>(EElysiumButton::Forward);
	constexpr uint64 GBack   = static_cast<uint64>(EElysiumButton::Back);
	constexpr uint64 GMoveL  = static_cast<uint64>(EElysiumButton::MoveLeft);
	constexpr uint64 GMoveR  = static_cast<uint64>(EElysiumButton::MoveRight);
	constexpr uint64 GJump   = static_cast<uint64>(EElysiumButton::Jump);

	// Classnames are suite-local: `ElysiumItems::Install` registers a class once per process and
	// never unregisters, so a name shared with another suite resolves to whichever ran first.
	// `item_w_unarmed` in particular is already claimed by the weapon suite with a different shape.
	const TCHAR* const GFists   = TEXT("item_w_atk_fists");
	const TCHAR* const GPistol  = TEXT("item_w_atk_pistol");
	const TCHAR* const GUzi     = TEXT("item_w_atk_uzi");
	const TCHAR* const GUnarmed = TEXT("item_w_atk_unarmed");
	const TCHAR* const GRound   = TEXT("AtkRound");

	// `MeleePlaybackRate(0)`, which is what every rating in a rulebook-free world evaluates to.
	constexpr double GMeleeRate = 0.70;

	FElysiumWeaponMode MakeMode(const TCHAR* Tag, EElysiumWeaponModeType Type, const TCHAR* TypeName,
		const TCHAR* Dmg, float AttackRate, int32 AmmoCost = 0)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = Tag;
		Mode.TypeName = TypeName;
		Mode.Type = Type;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = 5;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = AttackRate;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeAttackTable()
	{
		FElysiumItemTable Table;

		// A melee weapon with BOTH presses authored: the primary is the held-bit route and the
		// secondary is `ACT_MELEE_ATTACK_HEAVY`.
		FElysiumItemDef Fists;
		Fists.Classname = GFists;
		Fists.PrintName = GFists;
		Fists.Type = EElysiumItemType::WeaponMelee;
		Fists.bWieldable = true;
		Fists.Modes.Add(MakeMode(TEXT("Primary"), EElysiumWeaponModeType::Attack, TEXT("Attack"),
			TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), /*Attack_Rate*/ 0.5f));
		Fists.Modes.Add(MakeMode(TEXT("Secondary"), EElysiumWeaponModeType::SecondaryAttack,
			TEXT("Secondary_Attack"), TEXT("3 Bashing Close_Combat_Brawl DMG_FIST"), 0.5f));
		Table.Items.Add(MoveTemp(Fists));

		// A firearm with no `allow_autofire`: the primary is the press-edge route. Its secondary is a
		// real authored second shot rather than a mode toggle, so "the authored secondary ran" is
		// observable as a transaction.
		FElysiumItemDef Pistol;
		Pistol.Classname = GPistol;
		Pistol.PrintName = GPistol;
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.bWieldable = true;
		Pistol.AmmoType = GRound;
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.ReloadTime = 99.0f;   // authored, and deliberately NOT the reload clock
		Pistol.Modes.Add(MakeMode(TEXT("Primary"), EElysiumWeaponModeType::Attack, TEXT("Attack"),
			TEXT("2 Lethal Ranged_Combat DMG_BULLET"), /*Attack_Rate*/ 0.4f, /*Ammo_Cost*/ 1));
		Pistol.Modes.Add(MakeMode(TEXT("Secondary"), EElysiumWeaponModeType::SecondaryAttack,
			TEXT("Secondary_Attack"), TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 0.4f, /*Ammo_Cost*/ 1));
		Table.Items.Add(MoveTemp(Pistol));

		// The same family with `allow_autofire` set — the one key that puts a firearm back on the
		// held bit.
		FElysiumItemDef Uzi;
		Uzi.Classname = GUzi;
		Uzi.PrintName = GUzi;
		Uzi.Type = EElysiumItemType::WeaponFirearm;
		Uzi.bWieldable = true;
		Uzi.AmmoType = GRound;
		Uzi.MagazineSize = 30;
		Uzi.DefaultAmmo = 30;
		Uzi.Modes.Add(MakeMode(TEXT("Primary"), EElysiumWeaponModeType::Attack, TEXT("Attack"),
			TEXT("2 Lethal Ranged_Combat DMG_BULLET"), /*Attack_Rate*/ 0.1f, /*Ammo_Cost*/ 1));
		Uzi.Modes[0].bAllowAutofire = true;
		Table.Items.Add(MoveTemp(Uzi));

		// The `CWeaponUnarmed` shape: a weapon-family record that authors no `Activation` block at
		// all. It still takes a controller — the type says weapon — and the frame has to answer
		// `Idle` for it rather than reaching `AttackIntent`'s warning arm.
		FElysiumItemDef Unarmed;
		Unarmed.Classname = GUnarmed;
		Unarmed.PrintName = GUnarmed;
		Unarmed.Type = EElysiumItemType::WeaponMelee;
		Unarmed.bWieldable = true;
		Table.Items.Add(MoveTemp(Unarmed));

		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeAttackTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__player_attack_test__");

		FElysiumEntityDef Session;
		Session.Classname = TEXT("test_attack_use_session");
		Session.TargetName = TEXT("panel");
		Defs.Defs.Add(MoveTemp(Session));
		return Defs;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	// The whole fixture: an activated world with a live, grounded, mobile player at the origin.
	struct FAttackFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		double Now = 0.0;

		bool Stand(FAutomationTestBase& Test, const TCHAR* WeaponClass = nullptr)
		{
			// The block classification's pose request draws its weighted variant off the Reaction
			// stream, so the suite seeds it — the idiom every stream-reading suite uses.
			ElysiumRng::SeedAll(0x41544B31);
			Services.bPlayerOnGround = true;

			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeAttackTestDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			if (!Test.TestNotNull(TEXT("the player exists"), Player))
			{
				return false;
			}
			SeedHealth(*Player, 100);
			// Written directly: going through `SetRuntimeOrigin` would drive a pawn the headless case
			// does not have.
			Player->Origin = FVector::ZeroVector;
			Player->Angles = FVector::ZeroVector;
			if (WeaponClass != nullptr
				&& !Test.TestNotNull(TEXT("the player is armed"), Give(WeaponClass)))
			{
				return false;
			}
			Services.Calls.Reset();
			return true;
		}

		// The grant goes through `Weapon_Equip`, so what comes back is the active weapon.
		FElysiumWeapon* Give(const TCHAR* Classname)
		{
			const FElysiumEntityHandle Handle = Player->Inventory.GiveNamedItem(*Player, Classname);
			FElysiumEntity* Ent = World->Resolve(Handle);
			FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
			return Item ? Item->AsWeapon() : nullptr;
		}

		FElysiumWeapon* Active() const
		{
			FElysiumItem* Item = Player ? Player->Inventory.Active(*Player) : nullptr;
			return Item ? Item->AsWeapon() : nullptr;
		}

		// One whole frame in the map actor's own order: the controller publishes the button field
		// pre-move, the player thinks, the world ticks, and the post-move pass drains the buttons.
		void Frame(double At, uint64 Buttons)
		{
			Now = At;
			World->SetPlayerButtons(Buttons);
			World->RunPlayerThink(Now);
			World->Tick(Now);
			World->UpdatePlayerWeaponFrame();
		}

	};

	FString Verdict(FElysiumWeapon::EVerdict V)
	{
		return FString(FElysiumWeapon::VerdictName(V));
	}

	bool NearlyEqual(double A, double B) { return FMath::Abs(A - B) < 1.0e-3; }
}

// =====================================================================================
// The transport: one button field, and the edges derived from it
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackButtonsTest,
	"Elysium.Substrate.PlayerAttack.Buttons", GElysiumTestFlags)
bool FElysiumPlayerAttackButtonsTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The field round-trips, and the block still reads one bit off it ------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		TestEqual(TEXT("a fresh world holds no buttons"), F.World->GetPlayerButtons(), uint64(0));

		F.World->SetPlayerButtons(GSecAtk | GReload);
		TestEqual(TEXT("the whole mask round-trips"), F.World->GetPlayerButtons(), GSecAtk | GReload);
		TestTrue(TEXT("the block reads the dedicated `+wpn_secondaryatk` bit off the field"),
			F.World->IsPlayerBlockHeld());

		// `+attack2` alone cannot set the block bit — retail's `0x10160ec0` tests `0x08000000`, which
		// only `+wpn_secondaryatk` presses (`docs/vtmb/controls.md`).
		F.World->SetPlayerButtons(GAtk | GAtk2);
		TestFalse(TEXT("`+attack2` alone is not a block"), F.World->IsPlayerBlockHeld());

		F.World->SetPlayerButtons(0);
		TestFalse(TEXT("an empty field is not a block"), F.World->IsPlayerBlockHeld());
	}

	// --- The edge is measured against the CONSUMED mask, not against the last field -------------
	// A pistol authors no `allow_autofire`, so it fires exactly once per press edge. That makes the
	// swing serial the readout for how many edges the frame actually saw.
	{
		FAttackFixture F;
		if (!F.Stand(*this, GPistol))
		{
			return false;
		}
		FElysiumWeapon* Pistol = F.Active();
		if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
		{
			return false;
		}

		// Two publishes of the same held press with NO weapon frame between them — the shape of a
		// paused frame, or of a `PostMoveTick` the map actor never reached. Nothing consumes, so the
		// press is still an edge when the frame finally runs.
		F.World->SetPlayerButtons(GAtk);
		F.World->SetPlayerButtons(GAtk);
		F.Now = 0.0;
		F.World->Tick(0.0);
		F.World->UpdatePlayerWeaponFrame();
		TestEqual(TEXT("a press held across skipped frames is not lost"), Pistol->AcceptedSwingCount(), 1);

		// The same held bit on the next frame is not a second press: it was consumed by the frame
		// above, and the deadline has passed so nothing else could be refusing it.
		F.Frame(1.0, GAtk);
		TestEqual(TEXT("...and it is not double-spent by the next frame"), Pistol->AcceptedSwingCount(), 1);

		// Release and press again: a genuine new edge.
		F.Frame(2.0, 0);
		F.Frame(3.0, GAtk);
		TestEqual(TEXT("a released-and-pressed button is a fresh edge"), Pistol->AcceptedSwingCount(), 2);
	}

	return true;
}

// =====================================================================================
// The producer: which button route each weapon family takes
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackProducerTest,
	"Elysium.Substrate.PlayerAttack.Producer", GElysiumTestFlags)
bool FElysiumPlayerAttackProducerTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- Melee is a PRESS EDGE, end to end -------------------------------------------------------
	// `CWeaponMelee::ItemPostFrame` (`0x103EAEC0`) reads `m_afButtonPressed`, which is
	// `(last ^ current) & current` and never the held field
	// (`docs/vtmb/combat-and-damage.md` § "Weapon and input surface"). One press is one swing.
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}

		F.Frame(0.0, GAtk);
		TestEqual(TEXT("the press swings"), Fists->AcceptedSwingCount(), 1);
		// Melee recovery is the resolved clip's duration over the playback rate — with no embodiment
		// the clip falls back to the mode's authored `Attack_Rate`, so it is 0.5 / 0.70.
		TestTrue(TEXT("recovery is the clip over the playback rate, not the authored Attack_Rate"),
			NearlyEqual(Fists->Swing.RecoveryDeadline, 0.5 / GMeleeRate));

		// **The stuck-held case.** A button that never comes back up — a lost focus, a key the OS keeps
		// reporting down — produces no further edge and therefore nothing at all, however many frames
		// run and however far past the recovery deadline they reach.
		F.Frame(0.1, GAtk);
		F.Frame(1.0, GAtk);
		F.Frame(2.0, GAtk);
		F.Frame(9.0, GAtk);
		TestEqual(TEXT("a held primary swings exactly once, however long it is held"),
			Fists->AcceptedSwingCount(), 1);
		TestEqual(TEXT("...and a held-only frame asked for nothing at all"),
			Verdict(Fists->ItemPostFrame(EB::Primary, EB::None)), FString(TEXT("idle")));

		// A release and a fresh press is a fresh swing.
		F.Frame(10.0, 0);
		F.Frame(11.0, GAtk);
		TestEqual(TEXT("a released-and-pressed button is a fresh swing"),
			Fists->AcceptedSwingCount(), 2);

		// And a press INSIDE the recovery window reaches the busy path rather than the plain refusal:
		// that is where the combo lives, and a terminal attack spends the press there.
		TestEqual(TEXT("a press while the attack is still running is busy, not `not ready`"),
			Verdict(Fists->ItemPostFrame(EB::Primary, EB::Primary)), FString(TEXT("busy")));
		TestEqual(TEXT("...and it stages no second swing"), Fists->AcceptedSwingCount(), 2);
	}

	// --- A firearm without `allow_autofire` acts on the press EDGE -------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GPistol))
		{
			return false;
		}
		FElysiumWeapon* Pistol = F.Active();
		if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
		{
			return false;
		}

		F.Frame(0.0, GAtk);
		TestEqual(TEXT("the press fires"), Pistol->AcceptedSwingCount(), 1);
		TestTrue(TEXT("ranged recovery is the mode's authored Attack_Rate"),
			NearlyEqual(Pistol->Swing.RecoveryDeadline, 0.4));

		// Held past the deadline and still nothing: without the key, held attack intent is lost
		// after the edge (`Substrate/ElysiumItemTable.h` — `allow_autofire`).
		F.Frame(1.0, GAtk);
		F.Frame(2.0, GAtk);
		TestEqual(TEXT("a held button never re-fires a weapon with no allow_autofire"),
			Pistol->AcceptedSwingCount(), 1);
		TestEqual(TEXT("...and a held-only frame asked for nothing at all"),
			Verdict(Pistol->ItemPostFrame(EB::Primary, EB::None)), FString(TEXT("idle")));

		F.Frame(3.0, 0);
		F.Frame(4.0, GAtk);
		TestEqual(TEXT("a fresh edge fires again"), Pistol->AcceptedSwingCount(), 2);
	}

	// --- `allow_autofire` puts the same family back on the held bit ------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GUzi))
		{
			return false;
		}
		FElysiumWeapon* Uzi = F.Active();
		if (!TestNotNull(TEXT("the autofire weapon is active"), Uzi))
		{
			return false;
		}

		F.Frame(0.0, GAtk);
		TestEqual(TEXT("the press fires"), Uzi->AcceptedSwingCount(), 1);
		F.Frame(1.0, GAtk);
		TestEqual(TEXT("the held bit repeats past the deadline"), Uzi->AcceptedSwingCount(), 2);
		F.Frame(2.0, GAtk);
		TestEqual(TEXT("...and keeps repeating"), Uzi->AcceptedSwingCount(), 3);
		// A round is spent by the COMMIT, which rides the event queue a fraction of a clip behind the
		// press, so the count is read after a frame that lets the last one land.
		F.Frame(3.0, 0);
		TestEqual(TEXT("each repeat spent one round"), Uzi->MagazineCount, 27);
	}

	return true;
}

// =====================================================================================
// The direction keys: how the movement half of the button field reaches attack selection
//
// The player selector compares each candidate sequence's authored mask with `+0x2088 & 0x79A`
// (`docs/vtmb/combat-and-damage.md` § "Melee attack, combo, block and damage"), and the authored
// masks are exported RAW in the file's own `IN_*` numbering. This runtime numbers its own button
// bits differently, so the field has to carry the movement bits AND be translated by direction on
// its way to the resolver — and this is where both halves are pinned.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackDirectionKeysTest,
	"Elysium.Substrate.PlayerAttack.DirectionKeys", GElysiumTestFlags)
bool FElysiumPlayerAttackDirectionKeysTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FAttackFixture F;
	if (!F.Stand(*this, GFists))
	{
		return false;
	}
	FElysiumWeapon* Fists = F.Active();
	if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
	{
		return false;
	}
	// The activity request is made by a body, so the swing below needs one to make it from.
	F.Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));

	// --- The translation, direction by direction ---------------------------------------------------
	struct FCase { uint64 Ours; int32 Theirs; const TCHAR* Why; };
	const FCase Cases[] = {
		{ GFwd,   ElysiumCombo::InForward,   TEXT("`+forward` is IN_FORWARD") },
		{ GBack,  ElysiumCombo::InBack,      TEXT("`+back` is IN_BACK") },
		{ GMoveL, ElysiumCombo::InMoveLeft,  TEXT("`+moveleft` is IN_MOVELEFT") },
		{ GMoveR, ElysiumCombo::InMoveRight, TEXT("`+moveright` is IN_MOVERIGHT") },
		{ GJump,  ElysiumCombo::InJump,      TEXT("`+jump` is IN_JUMP") },
	};
	for (const FCase& Case : Cases)
	{
		F.World->SetPlayerButtons(Case.Ours);
		TestEqual(Case.Why, F.World->PlayerSelectionStateMask(), Case.Theirs);
	}

	// Two at once compose, and everything outside `0x79A` contributes nothing: a held attack button
	// is not a direction, and the whole point of the selection mask is that it cannot become one.
	F.World->SetPlayerButtons(GFwd | GMoveR);
	TestEqual(TEXT("held directions compose"), F.World->PlayerSelectionStateMask(),
		ElysiumCombo::InForward | ElysiumCombo::InMoveRight);
	F.World->SetPlayerButtons(GAtk | GAtk2 | GSecAtk | GReload);
	TestEqual(TEXT("the combat bits are invisible to attack selection"),
		F.World->PlayerSelectionStateMask(), 0);
	TestTrue(TEXT("...and the translated mask never leaves the selection bits"),
		(F.World->PlayerSelectionStateMask() & ~ElysiumCombo::SelectionMask) == 0);

	// --- The field reaches the resolver, on the swing's own request ---------------------------------
	// The seam records what it was handed, so this is the plumbing end to end: the controller's field,
	// the world's translation, `FillActivityClipRequest`, and the activity request the swing makes.
	F.Services.bNpcActivitiesResolve = true;
	F.Services.ResolvedNpcActivityLabel = TEXT("fists_attack_W1");
	F.Services.ResolvedNpcActivityOwner = TEXT("fists");
	F.Services.Calls.Reset();
	F.Frame(0.0, 0);
	F.Frame(0.0, GAtk | GFwd);
	TestEqual(TEXT("the press swung"), Fists->AcceptedSwingCount(), 1);
	TestTrue(TEXT("the swing's activity request carries the held direction, in the file's own bits"),
		F.Services.Saw(TEXT("ResolveNpcActivityClip male_pc ACT_MELEE_ATTACK"))
		&& F.Services.Log().Contains(
			FString::Printf(TEXT("buttons=%d"), ElysiumCombo::InForward)));

	// A player holding nothing states a mask of 0 — "no direction held", which is the neutral entry's
	// own exact match — and never `INDEX_NONE`, which is what a body with no button field states.
	F.Services.Calls.Reset();
	F.Frame(5.0, 0);
	F.Frame(5.0, GAtk);
	TestEqual(TEXT("the second press, well past the recovery, swung"),
		Fists->AcceptedSwingCount(), 2);
	TestTrue(TEXT("a player holding no direction states the neutral mask, not `no buttons at all`"),
		F.Services.Log().Contains(TEXT("buttons=0")));

	return true;
}

// =====================================================================================
// The refusals: each one by name, none of them silent
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackRefusalsTest,
	"Elysium.Substrate.PlayerAttack.Refusals", GElysiumTestFlags)
bool FElysiumPlayerAttackRefusalsTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The sign panel takes first refusal, and it spends the press ----------------------------
	// Retail's `ItemPostFrame` gives a controlling use entity first refusal
	// (`docs/vtmb/player-entity.md` § "Recovered `PostThink` body"); mapping that onto the open sign
	// panel is CHOSEN (C4), because every VtMB popup instructs "left-click to continue".
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}

		FElysiumEntity* Owner = F.World->FindByName(TEXT("panel"));
		if (!TestNotNull(TEXT("the sign's owning entity exists"), Owner))
		{
			return false;
		}
		TSharedPtr<FElysiumSignData> Panel = MakeShared<FElysiumSignData>();
		Panel->bParsed = true;
		Panel->bCloseOnLeftClick = true;
		Panel->MinShowTime = 0.0f;
		F.World->OpenSign(Owner->Handle, Panel, 0.0f);

		F.Frame(0.0, GAtk);
		TestFalse(TEXT("the click dismissed the panel"), F.World->GetOpenSign().IsSet());
		TestEqual(TEXT("...and did not also swing"), Fists->AcceptedSwingCount(), 0);

		// The button is still held on the next frame and the panel is gone — but the press was SPENT
		// on the dismissal, so no phantom edge follows it through to the weapon.
		F.Frame(0.1, GAtk);
		TestEqual(TEXT("a press spent on a dismissal does not also swing on the next frame"),
			Fists->AcceptedSwingCount(), 0);
		F.Frame(0.2, 0);
		F.Frame(0.3, GAtk);
		TestEqual(TEXT("...and the next real press does"), Fists->AcceptedSwingCount(), 1);
	}

	// --- A refused dismissal spends the press just the same --------------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GPistol))
		{
			return false;
		}
		FElysiumWeapon* Pistol = F.Active();
		if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
		{
			return false;
		}

		FElysiumEntity* Owner = F.World->FindByName(TEXT("panel"));
		if (!TestNotNull(TEXT("the sign's owning entity exists"), Owner))
		{
			return false;
		}
		TSharedPtr<FElysiumSignData> Panel = MakeShared<FElysiumSignData>();
		Panel->bParsed = true;
		Panel->bCloseOnLeftClick = true;
		Panel->MinShowTime = 5.0f;   // the enforced dwell refuses the click
		F.World->OpenSign(Owner->Handle, Panel, 0.0f);

		F.Frame(0.0, GAtk);
		TestTrue(TEXT("the dwell held the panel open"), F.World->GetOpenSign().IsSet());
		TestEqual(TEXT("the refused click did not reach the weapon"), Pistol->AcceptedSwingCount(), 0);

		// The panel closes on its own; the button was never released. A press-edge weapon must not
		// fire, because the press it would fire on was already spent on the panel.
		F.World->CloseSign(/*bSilent*/ true);
		F.Frame(0.1, GAtk);
		TestEqual(TEXT("a press spent on a refused dismissal cannot be re-spent on the weapon"),
			Pistol->AcceptedSwingCount(), 0);
	}

	// --- An open panel refuses the whole frame, not just the dismissing press --------------------
	// The panel owns the primary button for as long as it is up, so a press arriving at any point
	// while it stands — a second click at the panel, a trigger already held — is spent there.
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}

		FElysiumEntity* Owner = F.World->FindByName(TEXT("panel"));
		if (!TestNotNull(TEXT("the sign's owning entity exists"), Owner))
		{
			return false;
		}
		TSharedPtr<FElysiumSignData> Panel = MakeShared<FElysiumSignData>();
		Panel->bParsed = true;
		Panel->bCloseOnLeftClick = true;
		Panel->MinShowTime = 5.0f;   // the enforced dwell refuses the click, so the panel stays up
		F.World->OpenSign(Owner->Handle, Panel, 0.0f);

		// Press, release and press again, all while the panel stands and all well past a melee
		// recovery deadline. Every one of those edges is the panel's.
		F.Frame(0.0, GAtk);
		F.Frame(1.0, 0);
		F.Frame(2.0, GAtk);
		TestTrue(TEXT("the dwell held the panel open"), F.World->GetOpenSign().IsSet());
		TestEqual(TEXT("no press reaches the weapon behind an open panel"),
			Fists->AcceptedSwingCount(), 0);

		// And the panel closing gives the button back to the weapon — on the next press, not on the
		// one that is merely still down.
		F.World->CloseSign(/*bSilent*/ true);
		F.Frame(3.0, GAtk);
		TestEqual(TEXT("a button still down when the panel goes is not a fresh press"),
			Fists->AcceptedSwingCount(), 0);
		F.Frame(4.0, 0);
		F.Frame(5.0, GAtk);
		TestEqual(TEXT("...and the next press is the weapon's again"),
			Fists->AcceptedSwingCount(), 1);
	}

	// --- A captured `+use` session owns the player's hands ---------------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}
		FElysiumEntity* Panel = F.World->FindByName(TEXT("panel"));
		if (!TestNotNull(TEXT("the session entity exists"), Panel))
		{
			return false;
		}
		const FElysiumUseBeginResult Begun =
			F.World->BeginPlayerUseSession(Panel->Handle, F.World->PlayerHandle());
		TestTrue(TEXT("the session started"),
			Begun.Outcome == EElysiumUseOutcome::SessionStarted);

		F.Frame(0.0, GAtk);
		F.Frame(1.0, 0);
		F.Frame(2.0, GAtk);
		TestEqual(TEXT("a controlled use refuses every attack frame"), Fists->AcceptedSwingCount(), 0);

		F.World->EndPlayerUseSession(Panel->Handle, EElysiumUseEndReason::Completed);
		F.Frame(3.0, 0);
		F.Frame(4.0, GAtk);
		TestEqual(TEXT("...and releases them when it ends"), Fists->AcceptedSwingCount(), 1);
	}

	// --- An immobilised player is retail's `m_iPlayerLocked` (RE-A6) -----------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}
		F.Player->SetImmobilized(true);
		F.Frame(0.0, GAtk);
		F.Frame(1.0, 0);
		F.Frame(2.0, GAtk);
		TestEqual(TEXT("an immobilised player runs no weapon frame"), Fists->AcceptedSwingCount(), 0);

		F.Player->SetImmobilized(false);
		F.Frame(3.0, 0);
		F.Frame(4.0, GAtk);
		TestEqual(TEXT("...and swings again once released"), Fists->AcceptedSwingCount(), 1);
	}

	// --- A dead player runs no weapon frame ------------------------------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}
		// The direct setter rather than a lethal blow: the death REPORT is the term the frame reads,
		// and running the whole death path would drag the game-over route in with it.
		F.Player->SetDeathReportedForRestore(true);
		F.Frame(0.0, GAtk);
		F.Frame(1.0, 0);
		F.Frame(2.0, GAtk);
		TestEqual(TEXT("a dead player does not swing"), Fists->AcceptedSwingCount(), 0);
	}

	// --- The holstered record that authors nothing ------------------------------------------------
	// `CWeaponUnarmed` is not the melee implementation and has no ordinary attack table
	// (`docs/vtmb/combat-and-damage.md` § "Weapon and input surface"). The whole point of the
	// short-circuit is that this is an ORDINARY state — the frame answers `Idle` rather than
	// reaching `AttackIntent`, whose primary `NoMode` arm warns.
	{
		FAttackFixture F;
		if (!F.Stand(*this, GUnarmed))
		{
			return false;
		}
		FElysiumWeapon* Unarmed = F.Active();
		if (!TestNotNull(TEXT("the mode-less record still takes a weapon controller"), Unarmed))
		{
			return false;
		}
		TestEqual(TEXT("it resolves no primary mode"), Unarmed->PrimaryModeIndex, INDEX_NONE);
		TestEqual(TEXT("...and no secondary either"), Unarmed->SecondaryModeIndex, INDEX_NONE);

		// Every button, together: none of them may reach the mode dispatch.
		TestEqual(TEXT("a record with no attack mode is idle, not `no mode`"),
			Verdict(Unarmed->ItemPostFrame(EB::Primary | EB::Secondary | EB::Reload,
				EB::Primary | EB::Secondary | EB::Reload)),
			FString(TEXT("idle")));

		F.Frame(0.0, GAtk | GSecAtk | GReload);
		TestEqual(TEXT("...and it stages nothing through the world's own frame"),
			Unarmed->AcceptedSwingCount(), 0);
	}

	// --- No active weapon at all -------------------------------------------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		TestNull(TEXT("an empty hand holds no weapon"), F.Player->Inventory.Active(*F.Player));

		// The frame refuses, and it still spends the press: the edge is drained ahead of every
		// refusal, so arming a weapon under a button that is already down does not fire it.
		F.Frame(0.0, GAtk);
		FElysiumWeapon* Pistol = F.Give(GPistol);
		if (!TestNotNull(TEXT("the pistol arrives armed and active"), Pistol))
		{
			return false;
		}
		F.Frame(1.0, GAtk);
		TestEqual(TEXT("a press spent on an empty hand does not fire the weapon that follows it"),
			Pistol->AcceptedSwingCount(), 0);
		F.Frame(2.0, 0);
		F.Frame(3.0, GAtk);
		TestEqual(TEXT("...and the next real press does"), Pistol->AcceptedSwingCount(), 1);
	}

	return true;
}

// =====================================================================================
// `+wpn_secondaryatk` — one bit field, two independent consumers
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackSecondaryCompositeTest,
	"Elysium.Substrate.PlayerAttack.SecondaryComposite", GElysiumTestFlags)
bool FElysiumPlayerAttackSecondaryCompositeTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- On a melee weapon the composite does BOTH halves ----------------------------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}

		F.Frame(0.0, GSecAtk);
		TestTrue(TEXT("the block classification stands while the composite is held"),
			F.Player->IsActivelyBlocking());
		TestEqual(TEXT("...and the same press ran the ordinary secondary attack"),
			Fists->AcceptedSwingCount(), 1);
		TestEqual(TEXT("the melee secondary is ACT_MELEE_ATTACK_HEAVY"),
			Fists->Swing.Activity, FString(TEXT("ACT_MELEE_ATTACK_HEAVY")));

		// RE-A1 — the secondary route is a PRESS EDGE. Holding the composite past the recovery
		// deadline keeps blocking and does NOT autofire heavy attacks.
		F.Frame(1.0, GSecAtk);
		F.Frame(2.0, GSecAtk);
		TestTrue(TEXT("the block still stands while the button is held"),
			F.Player->IsActivelyBlocking());
		TestEqual(TEXT("exactly one heavy attack per press, however long it is held"),
			Fists->AcceptedSwingCount(), 1);

		F.Frame(3.0, 0);
		TestFalse(TEXT("releasing the composite releases the block"), F.Player->IsActivelyBlocking());
		F.Frame(4.0, GSecAtk);
		TestEqual(TEXT("a second press is a second heavy attack"), Fists->AcceptedSwingCount(), 2);
	}

	// --- On a firearm the secondary half runs and the block half cannot --------------------------
	// Ranged capability `0x2000` does not intersect the melee block mask `0x18000`, so there is no
	// firearm block (`docs/vtmb/controls.md` § "Attack, block and weapon commands").
	{
		FAttackFixture F;
		if (!F.Stand(*this, GPistol))
		{
			return false;
		}
		FElysiumWeapon* Pistol = F.Active();
		if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
		{
			return false;
		}

		F.Frame(0.0, GSecAtk);
		TestFalse(TEXT("a firearm cannot block, however the composite is held"),
			F.Player->IsActivelyBlocking());
		TestEqual(TEXT("the authored secondary shot still ran"), Pistol->AcceptedSwingCount(), 1);
		TestEqual(TEXT("...out of the record's own Secondary mode"),
			Pistol->Swing.ModeIndex, Pistol->SecondaryModeIndex);
	}

	// --- `+attack2` reaches the same secondary route, without the block bit -----------------------
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}
		F.Frame(0.0, GAtk2);
		TestFalse(TEXT("`+attack2` alone is not a block"), F.Player->IsActivelyBlocking());
		TestEqual(TEXT("...but it is the same secondary attack"),
			Fists->Swing.Activity, FString(TEXT("ACT_MELEE_ATTACK_HEAVY")));
	}

	// --- The mask the bind actually produces: BOTH bits at once -----------------------------------
	// `wpn_secondaryatk` is declared with `Also = Attack2` (`Player/ElysiumCommands.cpp`), so one
	// press of TAB latches the dedicated bit and `+attack2` in the same `SetButtonBits` call. Both
	// fold onto the one `Secondary` route, which must not read as two secondary presses.
	{
		FAttackFixture F;
		if (!F.Stand(*this, GFists))
		{
			return false;
		}
		FElysiumWeapon* Fists = F.Active();
		if (!TestNotNull(TEXT("the fists are the active weapon"), Fists))
		{
			return false;
		}

		F.Frame(0.0, GSecAtk | GAtk2);
		TestTrue(TEXT("the composite still blocks off its dedicated bit"),
			F.Player->IsActivelyBlocking());
		TestEqual(TEXT("both bits in one frame are one secondary attack, not two"),
			Fists->AcceptedSwingCount(), 1);

		F.Frame(1.0, GSecAtk | GAtk2);
		TestEqual(TEXT("...and holding both repeats neither"), Fists->AcceptedSwingCount(), 1);
	}

	return true;
}

// =====================================================================================
// Reload — the fourth button, and the fire intent that arrives during it
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerAttackReloadTest,
	"Elysium.Substrate.PlayerAttack.Reload", GElysiumTestFlags)
bool FElysiumPlayerAttackReloadTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeAttackTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FAttackFixture F;
	if (!F.Stand(*this, GPistol))
	{
		return false;
	}
	FElysiumWeapon* Pistol = F.Active();
	if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
	{
		return false;
	}
	Pistol->MagazineCount = 0;
	F.Player->Inventory.AddReserve(GRound, 12);

	// The `+reload` press edge starts the transaction.
	F.Frame(0.0, GReload);
	TestTrue(TEXT("the reload press starts the transaction"), Pistol->bReloading);
	TestFalse(TEXT("nothing has interrupted it yet"), Pistol->bFireIntentDuringReload);

	// A primary press arriving during the reload sets the interruption latch and reports
	// `Reloading` — the reload frame finishes the transaction before normal firing resumes.
	TestEqual(TEXT("a fire intent during a reload reports `reloading`"),
		Verdict(Pistol->ItemPostFrame(EB::Primary, EB::Primary)), FString(TEXT("reloading")));
	TestTrue(TEXT("...and sets the interruption latch"), Pistol->bFireIntentDuringReload);
	TestEqual(TEXT("...and stages no shot"), Pistol->AcceptedSwingCount(), 0);

	// A repeated `+reload` while one is live is refused and leaves the running transaction alone.
	const int32 LiveSerial = Pistol->ReloadSerial;
	F.Frame(0.05, 0);
	F.Frame(0.06, GReload);
	TestEqual(TEXT("a second reload press does not restart the live transaction"),
		Pistol->ReloadSerial, LiveSerial);

	// The queued commit lands on the substrate clock and refills from the reserve.
	F.Frame(5.0, 0);
	TestFalse(TEXT("the reload completed"), Pistol->bReloading);
	TestEqual(TEXT("the magazine is full"), Pistol->MagazineCount, 6);
	TestEqual(TEXT("...out of the reserve"), F.Player->Inventory.Reserve(GRound), 6);

	return true;
}

}   // namespace ElysiumPlayerAttackTests

#endif   // WITH_DEV_AUTOMATION_TESTS
