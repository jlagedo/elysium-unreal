// Content-free Substrate automation: loose items, deterministic lockpicking, and container transactions.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumInventoryTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;


// Inventory: items are entities, the character owns handles to them, and the six
// Character methods read and write that one container. The contract is
// `docs/vtmb/inventory.md` §§2-6.
//
// Content-free: the item catalogue is built in code and installed for the length of this
// case, so the classnames and the policy values below are this test's statement of the
// retail contract rather than a reading of the export. `Elysium.Content.Items` is what
// checks the same values against the corpus.


namespace
{
	FElysiumItemDef MakeItemDef(const TCHAR* Classname, EElysiumItemType Type)
	{
		FElysiumItemDef Def;
		Def.Classname = Classname;
		Def.PrintName = Classname;
		Def.Type = Type;
		Def.PlayerModel = TEXT("models/items/test/ground.mdl");
		return Def;
	}

	// The four classnames the tutorial's inventory chain turns on, with the policy their real
	// `vdata/items` records carry.
	FElysiumItemTable MakeTestItemTable()
	{
		FElysiumItemTable Table;

		Table.Items.Add(MakeItemDef(TEXT("item_g_lockpick"), EElysiumItemType::Generic));

		FElysiumItemDef Gun = MakeItemDef(TEXT("item_w_thirtyeight"), EElysiumItemType::WeaponFirearm);
		Gun.bWieldable = true;
		Gun.AmmoType = TEXT("ThirtyeightRound");
		Gun.MagazineSize = 6;
		Gun.DefaultAmmo = 6;
		Table.Items.Add(MoveTemp(Gun));

		// Patch-authored stackable with a limit of ten — the one the doc warns not to read as stock.
		FElysiumItemDef TireIron = MakeItemDef(TEXT("item_w_tire_iron"), EElysiumItemType::WeaponMelee);
		TireIron.bWieldable = true;
		TireIron.bStackable = true;
		TireIron.StackLimit = 10;
		Table.Items.Add(MoveTemp(TireIron));

		FElysiumItemDef Keyring = MakeItemDef(TEXT("item_g_keyring"), EElysiumItemType::Generic);
		Keyring.bDroppable = false;
		Keyring.bPermanentInventory = true;
		Table.Items.Add(MoveTemp(Keyring));

		FElysiumItemDef Unarmed = MakeItemDef(TEXT("item_w_unarmed"), EElysiumItemType::WeaponMelee);
		Unarmed.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Table.Items.Add(MoveTemp(Unarmed));

		Table.Reindex();
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInventoryTest,
	"Elysium.Substrate.Inventory.Catalogue", GElysiumTestFlags)
bool FElysiumInventoryTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTestItemTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// --- The class list IS the catalogue --------------------------------------------------
	const FElysiumClassDesc* ItemDesc = Reg.Find(FName(TEXT("item_g_lockpick")));
	if (!TestNotNull(TEXT("a vdata/items classname is a registered entity class"), ItemDesc))
	{
		return false;
	}
	TestEqual(TEXT("an item sits under CBaseCombatWeapon"), ItemDesc->BaseName, ElysiumItemClassName());
	if (const FElysiumClassDesc* Chain = Reg.Find(ElysiumItemClassName()))
	{
		TestEqual(TEXT("...which sits under CBaseAnimating"),
			Chain->BaseName, ElysiumAnimatingClassName());
	}
	// The recovered datamap fields resolve through the same one R2 walk everything else does.
	for (const TCHAR* Field : { TEXT("m_hOwner"), TEXT("m_iInvenPos"), TEXT("m_iItemCount"),
		TEXT("m_iAmmoTypes"), TEXT("m_iMagazineCurAmts") })
	{
		TestNotNull(*FString::Printf(TEXT("item field %s resolves"), Field),
			reinterpret_cast<const void*>(Reg.FindField(*ItemDesc, FName(Field))));
	}
	if (const FElysiumClassDesc* CharDesc = Reg.Find(ElysiumCombatCharacterClassName()))
	{
		TestNotNull(TEXT("the active-weapon handle is a combat-character field"),
			reinterpret_cast<const void*>(Reg.FindField(*CharDesc, FName(TEXT("m_hActiveWeapon")))));
	}

	// --- A world with a player and one loose world item -----------------------------------
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
		EElysiumItemGroundModelState::Geometryless);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__inventory_test__");
	{
		FElysiumEntityDef Loose;
		Loose.Classname = TEXT("item_g_lockpick");
		Loose.TargetName = TEXT("tut_lockpicks");
		Loose.Origin = FVector(10, 20, 30);
		Defs.Defs.Add(MoveTemp(Loose));
		FElysiumEntityDef Unarmed;
		Unarmed.Classname = TEXT("item_w_unarmed");
		Unarmed.TargetName = TEXT("unarmed_world_body");
		Defs.Defs.Add(MoveTemp(Unarmed));
	}

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	FElysiumEntity* LooseEnt = World.FindByName(TEXT("tut_lockpicks"));
	FElysiumItem* Lockpick = LooseEnt ? LooseEnt->AsItem() : nullptr;
	if (!TestNotNull(TEXT("the player exists"), Player)
		|| !TestNotNull(TEXT("the map's item spawned as an item entity"), Lockpick))
	{
		return false;
	}
	TestEqual(TEXT("a geometry-bearing loose item builds its shared ground mesh"),
		Services.Count(TEXT("BuildPropVisual ground")), 1);
	TestEqual(TEXT("a loose item's body is a camera +use target"),
		Services.Count(TEXT("RegisterUseAnchor")), 1);
	TestEqual(TEXT("an authored geometryless item never asks for a missing mesh"),
		Services.Count(TEXT("BuildPropVisual w_null")), 0);

	// --- Ownership round trip: loose -> owned -> script-removed ---------------------------
	TestFalse(TEXT("a loose world item has no owner"), Lockpick->IsOwned());
	TestEqual(TEXT("...and is unslotted (255)"), Lockpick->InvenPos, FElysiumItem::Unslotted);
	TestFalse(TEXT("HasItem does not see it yet"),
		Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	TestTrue(TEXT("a loose item is camera-usable"), Lockpick->IsUsable());
	TestEqual(TEXT("...and publishes the open-hand context icon"), Lockpick->GetUseIcon(), 9);

	const FElysiumUseBeginResult Pickup = World.BeginPlayerUseSession(Lockpick->Handle, PlayerHandle);
	TestEqual(TEXT("camera +use completes the loose pickup"), Pickup.Outcome,
		EElysiumUseOutcome::Completed);
	TestTrue(TEXT("camera +use hands the SAME entity to the player"), Lockpick->IsOwned());
	TestFalse(TEXT("a carried item is no longer camera-usable"), Lockpick->IsUsable());
	TestFalse(TEXT("...and disables its camera target"),
		Services.UseAnchorEnabled.FindRef(Lockpick->Handle));
	TestEqual(TEXT("a successful loose pickup posts one HUD notification"),
		Services.Notifications.Num(), 1);
	if (Services.Notifications.IsValidIndex(0))
	{
		TestEqual(TEXT("pickup notification is typed as an item"),
			Services.Notifications[0].Kind, EElysiumNotificationKind::ItemAcquired);
		TestEqual(TEXT("pickup notification resolves the print name"),
			Services.Notifications[0].Subject, FString(TEXT("item_g_lockpick")));
		TestEqual(TEXT("pickup notification reports the admitted quantity"),
			Services.Notifications[0].Quantity, 1);
	}
	TestEqual(TEXT("...which now owns it"), Lockpick->Owner, PlayerHandle);
	TestEqual(TEXT("...at a compact position"), Lockpick->InvenPos, 0);
	TestEqual(TEXT("...held as a handle, not a name"), Player->Inventory.Num(), 1);
	TestTrue(TEXT("HasItem sees it"), Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	TestTrue(TEXT("...case-insensitively"), Player->Inventory.Has(*Player, TEXT("ITEM_G_LockPick")));
	const FElysiumUseBeginResult DuplicatePickup =
		World.BeginPlayerUseSession(Lockpick->Handle, PlayerHandle);
	TestEqual(TEXT("a carried item refuses a second camera pickup"), DuplicatePickup.Outcome,
		EElysiumUseOutcome::Unavailable);
	TestEqual(TEXT("a duplicate camera use cannot add a second slot"), Player->Inventory.Num(), 1);

	TestTrue(TEXT("RemoveItem matches the owned classname"),
		Player->Inventory.ScriptRemove(*Player, TEXT("item_g_lockpick")));
	TestTrue(TEXT("...and destroys the final entity"), Lockpick->IsDead());
	TestEqual(TEXT("...leaving the slot list empty"), Player->Inventory.Num(), 0);
	TestFalse(TEXT("RemoveItem on nothing matches nothing"),
		Player->Inventory.ScriptRemove(*Player, TEXT("item_g_lockpick")));

	// --- Stacking: decrement, then destroy the final entity --------------------------------
	TestTrue(TEXT("GiveNamedItem creates and equips a named item"),
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_tire_iron")).IsSet());
	TestTrue(TEXT("a second grant of a stackable item is accepted"),
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_tire_iron")).IsSet());
	TestEqual(TEXT("both same-frame stack grants remain distinct notifications"),
		Services.Notifications.Num(), 3);
	if (Services.Notifications.IsValidIndex(2))
	{
		TestEqual(TEXT("the merged grant retains its quantity"),
			Services.Notifications[2].Quantity, 1);
	}
	TestEqual(TEXT("...but merges rather than taking a second slot"), Player->Inventory.Num(), 1);
	FElysiumItem* Iron = Player->Inventory.FindOrdinary(*Player, TEXT("item_w_tire_iron"));
	if (!TestNotNull(TEXT("the merged stack is carried"), Iron))
	{
		return false;
	}
	TestEqual(TEXT("...counting two"), Iron->ItemCount, 2);

	TestTrue(TEXT("RemoveItem on a stack of two"),
		Player->Inventory.ScriptRemove(*Player, TEXT("item_w_tire_iron")));
	TestEqual(TEXT("...decrements the count"), Iron->ItemCount, 1);
	TestFalse(TEXT("...and destroys nothing"), Iron->IsDead());
	TestTrue(TEXT("RemoveItem on the last of a stack"),
		Player->Inventory.ScriptRemove(*Player, TEXT("item_w_tire_iron")));
	TestTrue(TEXT("...destroys the entity"), Iron->IsDead());
	TestEqual(TEXT("...and empties the slot"), Player->Inventory.Num(), 0);

	// --- The keyring: one carried entity owning logical records ----------------------------
	TestTrue(TEXT("the keyring is granted like any other item"),
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_g_keyring")).IsSet());
	FElysiumKeyring* Ring = Player->Inventory.FindKeyring(*Player);
	if (!TestNotNull(TEXT("item_g_keyring builds the keyring leaf"), Ring))
	{
		return false;
	}
	TestFalse(TEXT("its own item data says it cannot be dropped"), Ring->IsDroppable());
	TestTrue(TEXT("...and that it is permanent inventory"), Ring->IsPermanentInventory());

	const TCHAR* const StairsKey = TEXT("item_k_tutorial_chopshop_stairs_key");
	TestTrue(TEXT("a collected key becomes a keyring record"), Ring->AddKey(StairsKey));
	TestFalse(TEXT("...added once"), Ring->AddKey(StairsKey));
	TestTrue(TEXT("HasItem falls through to the keyring"), Player->Inventory.Has(*Player, StairsKey));
	TestTrue(TEXT("...case-insensitively"),
		Player->Inventory.Has(*Player, TEXT("ITEM_K_Tutorial_ChopShop_Stairs_Key")));
	TestTrue(TEXT("RemoveItem falls through to the keyring"),
		Player->Inventory.ScriptRemove(*Player, TEXT("Item_K_Tutorial_ChopShop_Stairs_Key")));
	TestFalse(TEXT("...removing the record"), Player->Inventory.Has(*Player, StairsKey));
	TestNotNull(TEXT("...and leaving the keyring entity carried"),
		Player->Inventory.FindKeyring(*Player));

	// --- AmmoCount reports the magazine; GiveAmmo grants reserve ---------------------------
	TestTrue(TEXT("the .38 is granted"),
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_thirtyeight")).IsSet());
	FElysiumItem* Gun = Player->Inventory.FindOrdinary(*Player, TEXT("item_w_thirtyeight"));
	if (!TestNotNull(TEXT("the .38 is carried"), Gun))
	{
		return false;
	}
	TestEqual(TEXT("a fresh firearm spawns loaded with its Default_Size"), Gun->MagazineCount, 6);
	TestEqual(TEXT("...and names its ammo type from the item data"), Gun->AmmoType,
		FString(TEXT("ThirtyeightRound")));
	// The tutorial's beat: the gun is owned and its magazine is empty, so dialogue grants six
	// RESERVE rounds — which must not move what AmmoCount reads.
	Gun->MagazineCount = 0;

	// --- The same container through the script surface -------------------------------------
	// One dispatch per method, through the real `CallCharacterMethod` both hosts share.
	auto Method = [&World, &PlayerHandle](const TCHAR* Name, TArrayView<const FElysiumVariant> Args)
	{
		return ElysiumScriptNatives::CallCharacterMethod(/*State*/ nullptr, &World, PlayerHandle,
			FName(Name), Args);
	};
	const FElysiumVariant Lock = FElysiumVariant::String(TEXT("item_g_lockpick"));
	TestTrue(TEXT("Character.GiveItem grants to the player"),
		Method(TEXT("GiveItem"), { Lock }).IsVoid());
	TestTrue(TEXT("Character.HasItem answers off the same container"),
		Method(TEXT("HasItem"), { Lock }).ToBool());
	TestFalse(TEXT("...and false for a classname nothing carries"),
		Method(TEXT("HasItem"), { FElysiumVariant::String(TEXT("item_g_nothing")) }).ToBool());

	// A stackable item: AmmoCount reads the STACK COUNT and GiveAmmo adds to it.
	const FElysiumVariant TireIron = FElysiumVariant::String(TEXT("item_w_tire_iron"));
	Method(TEXT("GiveItem"), { TireIron });
	TestEqual(TEXT("Character.AmmoCount on a stackable item is its count"),
		Method(TEXT("AmmoCount"), { TireIron }).ToInt(), 1);
	Method(TEXT("GiveAmmo"), { TireIron, FElysiumVariant::Int(4) });
	TestEqual(TEXT("...and GiveAmmo adds to that count directly"),
		Method(TEXT("AmmoCount"), { TireIron }).ToInt(), 5);

	// A non-stackable item: the LOADED MAGAZINE, which the same call does not move.
	const FElysiumVariant ThirtyEight = FElysiumVariant::String(TEXT("item_w_thirtyeight"));
	TestEqual(TEXT("Character.AmmoCount on a firearm is its loaded magazine"),
		Method(TEXT("AmmoCount"), { ThirtyEight }).ToInt(), 0);
	Method(TEXT("GiveAmmo"), { ThirtyEight, FElysiumVariant::Int(6) });
	TestEqual(TEXT("...which GiveAmmo does not move"),
		Method(TEXT("AmmoCount"), { ThirtyEight }).ToInt(), 0);
	TestEqual(TEXT("...because it granted RESERVE rounds"),
		Player->Inventory.Reserve(TEXT("ThirtyeightRound")), 6);
	TestEqual(TEXT("...pooled case-insensitively by ammo type"),
		Player->Inventory.Reserve(TEXT("thirtyeightround")), 6);
	TestEqual(TEXT("AmmoCount for an item nothing carries is zero"),
		Method(TEXT("AmmoCount"), { FElysiumVariant::String(TEXT("item_g_nothing")) }).ToInt(), 0);

	// HasWeaponEquipped: the tire iron's grant made it active.
	TestTrue(TEXT("Character.HasWeaponEquipped names the active weapon"),
		Method(TEXT("HasWeaponEquipped"), { TireIron }).ToBool());
	TestFalse(TEXT("...with an EXACT, case-sensitive compare"),
		Method(TEXT("HasWeaponEquipped"), { FElysiumVariant::String(TEXT("Item_W_Tire_Iron")) }).ToBool());
	TestFalse(TEXT("...and owning an item is not wielding it"),
		Method(TEXT("HasWeaponEquipped"), { FElysiumVariant::String(TEXT("item_g_keyring")) }).ToBool());

	// RemoveItem returns None whether or not anything matched.
	TestTrue(TEXT("Character.RemoveItem returns None on a match"),
		Method(TEXT("RemoveItem"), { Lock }).IsVoid());
	TestFalse(TEXT("...having removed it"), Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	TestTrue(TEXT("...and None on no match"),
		Method(TEXT("RemoveItem"), { FElysiumVariant::String(TEXT("item_g_nothing")) }).IsVoid());

	// --- Detach compacts and reindexes; it never destroys ----------------------------------
	// Carried now: keyring(0), .38(1), tire iron(2, active).
	TestEqual(TEXT("three ordinary slots"), Player->Inventory.Num(), 3);
	FElysiumItem* Slot0 = Player->Inventory.At(*Player, 0);
	FElysiumItem* Slot1 = Player->Inventory.At(*Player, 1);
	FElysiumItem* Slot2 = Player->Inventory.At(*Player, 2);
	if (!TestNotNull(TEXT("slot 0 resolves"), Slot0) || !TestNotNull(TEXT("slot 1 resolves"), Slot1)
		|| !TestNotNull(TEXT("slot 2 resolves"), Slot2))
	{
		return false;
	}
	TestEqual(TEXT("positions are the compact ordering"), Slot2->InvenPos, 2);

	TestTrue(TEXT("Detach takes the middle item out"), Player->Inventory.Detach(*Player, *Slot1));
	TestEqual(TEXT("...closing the gap"), Player->Inventory.Num(), 2);
	TestFalse(TEXT("...without destroying it"), Slot1->IsDead());
	TestFalse(TEXT("...leaving it unowned"), Slot1->IsOwned());
	TestEqual(TEXT("...and unslotted"), Slot1->InvenPos, FElysiumItem::Unslotted);
	TestEqual(TEXT("...while the item behind it is reindexed"), Slot2->InvenPos, 1);
	TestEqual(TEXT("...and the one in front keeps its place"), Slot0->InvenPos, 0);
	TestFalse(TEXT("detaching something not carried does nothing"),
		Player->Inventory.Detach(*Player, *Slot1));

	// --- The `Inventory_Remove` INPUT is entity-valued and only detaches --------------------
	World.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("Inventory_Remove")),
		FElysiumVariant::Handle(Slot2->Handle), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestFalse(TEXT("Inventory_Remove detaches"), Slot2->IsOwned());
	TestFalse(TEXT("...and never destroys"), Slot2->IsDead());
	TestEqual(TEXT("...keeping the stack intact"), Slot2->ItemCount, 5);
	TestEqual(TEXT("...leaving only the keyring carried"), Player->Inventory.Num(), 1);
	TestNull(TEXT("...and clearing the active-weapon handle it named"),
		reinterpret_cast<const void*>(Player->Inventory.Active(*Player)));

	// --- Item policy comes ONLY from the catalogue ------------------------------------------
	{
		FElysiumEntityDef Unknown;
		Unknown.Classname = TEXT("item_w_not_in_the_catalogue");
		Unknown.TargetName = TEXT("phantom");
		const FElysiumEntityHandle H = World.SpawnRuntimeEntity(MoveTemp(Unknown));
		FElysiumEntity* Phantom = World.Resolve(H);
		TestNull(TEXT("a classname the catalogue does not hold is not an item class"),
			reinterpret_cast<const void*>(Phantom ? Phantom->AsItem() : nullptr));
		TestTrue(TEXT("...it is an inert record, prefix or no prefix"),
			Phantom != nullptr && Phantom->IsRecordOnly());
		TestFalse(TEXT("GiveNamedItem refuses a classname with no item data"),
			Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_not_in_the_catalogue")).IsSet());
		TestEqual(TEXT("a failed grant emits no notification"),
			Services.Notifications.Num(), 7);
	}

	return true;
}


// Tutorial beat three: DefaultTouch lockpick acquisition -> held Intrusion attempt -> the
// attached doorknob unlocks and drives the existing door's real OnOpen producer.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialLockpickDoorTest,
	"Elysium.Substrate.TutorialLockpickDoor", GElysiumTestFlags)
bool FElysiumTutorialLockpickDoorTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTestItemTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* KnobClass = Registry.Find(FName(TEXT("prop_doorknob")));
	if (!TestNotNull(TEXT("prop_doorknob is registered"), KnobClass))
	{
		return false;
	}
	TestEqual(TEXT("doorknob sits on the shared lockable chain"), KnobClass->BaseName,
		ElysiumLockableEntityClassName());
	for (const TCHAR* Name : { TEXT("difficulty"), TEXT("skilltype"), TEXT("delete_key") })
	{
		TestNotNull(*FString::Printf(TEXT("lockable field %s resolves"), Name),
			reinterpret_cast<const void*>(Registry.FindField(*KnobClass, FName(Name))));
	}

	auto Counter = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("100"));
		return Def;
	};
	auto Wire = [](FElysiumEntityDef& Def, const TCHAR* Output, const TCHAR* Target)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = Target;
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Def.Outputs.Add(MoveTemp(Row));
	};
	auto ReadCounter = [](const FElysiumEntity* Entity)
	{
		TArray<TPair<FString, FString>> State;
		if (Entity)
		{
			Entity->GetDebugState(State);
		}
		const TPair<FString, FString>* Value = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Value"); });
		return Value ? FCString::Atof(*Value->Value) : -1.0f;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__tutorial_lockpick_door__");
	FElysiumEntityDef Door;
	Door.Classname = TEXT("func_door_rotating");
	Door.TargetName = TEXT("tutchopdoorc");
	Door.Keys.Add(TEXT("spawnflags"), TEXT("2304")); // PUSE | LOCKED
	Wire(Door, TEXT("OnOpen"), TEXT("opened"));
	Defs.Defs.Add(MoveTemp(Door));

	FElysiumEntityDef Knob;
	Knob.Classname = TEXT("prop_doorknob");
	Knob.TargetName = TEXT("office_knob");
	Knob.ModelMesh = TEXT("test_knob");
	Knob.Keys.Add(TEXT("model"), TEXT("models/test/knob.mdl"));
	Knob.Keys.Add(TEXT("parentname"), TEXT("tutchopdoorc"));
	Knob.Keys.Add(TEXT("difficulty"), TEXT("1"));
	Knob.Keys.Add(TEXT("skilltype"), TEXT("1"));
	Knob.Keys.Add(TEXT("delete_key"), TEXT("0"));
	Knob.Keys.Add(TEXT("use_icon"), TEXT("10"));
	Knob.Keys.Add(TEXT("locked_icon"), TEXT("3"));
	Wire(Knob, TEXT("OnSkillAttemptBegin"), TEXT("began"));
	Wire(Knob, TEXT("OnSkillAttemptCycle"), TEXT("cycled"));
	Wire(Knob, TEXT("OnSkillSuccess"), TEXT("succeeded"));
	Wire(Knob, TEXT("OnSkillFail"), TEXT("failed"));
	Wire(Knob, TEXT("OnUnlocked"), TEXT("unlocked"));
	Wire(Knob, TEXT("OnUseBegin"), TEXT("use_began"));
	Wire(Knob, TEXT("OnUseEnd"), TEXT("use_ended"));
	Defs.Defs.Add(MoveTemp(Knob));

	FElysiumEntityDef Lockpick;
	Lockpick.Classname = TEXT("item_g_lockpick");
	Lockpick.TargetName = TEXT("tut_lockpicks");
	Defs.Defs.Add(MoveTemp(Lockpick));
	for (const TCHAR* Name : { TEXT("opened"), TEXT("began"), TEXT("cycled"), TEXT("succeeded"),
		TEXT("failed"), TEXT("unlocked"), TEXT("use_began"), TEXT("use_ended") })
	{
		Defs.Defs.Add(Counter(Name));
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	// The headless test deliberately has no brush embodiment for the door. Logical registration
	// still resolves the parent and is what this test exercises.
	AddExpectedError(TEXT("resolved parent 'tutchopdoorc', but its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	FElysiumItem* Loose = World.FindByName(TEXT("tut_lockpicks"))
		? World.FindByName(TEXT("tut_lockpicks"))->AsItem() : nullptr;
	FElysiumLockableEntity* LiveKnob = World.FindByName(TEXT("office_knob"))
		? World.FindByName(TEXT("office_knob"))->AsLockableEntity() : nullptr;
	FElysiumDoorBase* LiveDoor = World.FindByName(TEXT("tutchopdoorc"))
		? World.FindByName(TEXT("tutchopdoorc"))->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("player resolves"), Player)
		|| !TestNotNull(TEXT("loose lockpick resolves"), Loose)
		|| !TestNotNull(TEXT("doorknob resolves"), LiveKnob)
		|| !TestNotNull(TEXT("door resolves"), LiveDoor))
	{
		return false;
	}

	World.RouteEntityTouch(Loose->Handle, PlayerHandle, /*bBegin*/ true);
	TestTrue(TEXT("world overlap makes HasItem see the lockpick"),
		Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	// The knob's lock is its own, seeded from `difficulty` in Spawn — attaching to a door does not
	// write it. Here both authorities happen to agree (spawnflags 2304 carries LOCKED, difficulty is
	// 1); Elysium.Substrate.DoorKnobLockAuthority covers the cases where they disagree.
	TestTrue(TEXT("the doorknob starts locked from its own difficulty"), LiveKnob->IsUseLocked());
	TestTrue(TEXT("a knobbed door keeps its slab as a look-ray target"),
		Services.UseAnchorEnabled.FindRef(LiveDoor->Handle));

	FElysiumUseCandidate Candidate;
	Candidate.Owner = LiveKnob->Handle;
	Candidate.Selection = EElysiumUseSelection::Exact;
	Services.UseQuery.Candidates = { Candidate };
	World.UpdatePlayerInteraction();
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("carried lockpick starts an attempt-owned session"), World.GetLastUseOutcome(),
		EElysiumUseOutcome::Locked); // focus was locked at press; the session is still captured
	TestTrue(TEXT("skill session blocks save while active"),
		World.ScriptedSessionSaveBlockReason().Contains(TEXT("skill attempt")));
	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();
	TestTrue(TEXT("releasing +use does not cancel the timed attempt"),
		World.ScriptedSessionSaveBlockReason().Contains(TEXT("skill attempt")));
	World.Tick(5.0);
	TestEqual(TEXT("attempt begin is a real queued output"),
		ReadCounter(World.FindByName(TEXT("began"))), 1.0f);
	TestEqual(TEXT("zero-rating headless attempt still produces its cycle"),
		ReadCounter(World.FindByName(TEXT("cycled"))), 1.0f);
	TestEqual(TEXT("the deterministic threshold takes the fail branch"),
		ReadCounter(World.FindByName(TEXT("failed"))), 1.0f);
	TestEqual(TEXT("failure increments the resolved-attempt count"), LiveKnob->SkillAttempts, 1);
	TestTrue(TEXT("failure leaves both authorities locked"),
		LiveKnob->IsUseLocked() && LiveDoor->IsUseLocked());
	TestTrue(TEXT("failure ends the attempt-owned session"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());
	TestEqual(TEXT("failure closes the doorknob use boundary"),
		ReadCounter(World.FindByName(TEXT("use_ended"))), 1.0f);
	TestTrue(TEXT("failure queues cycle, fail, then use-end"),
		Sink->AppearsInOrder(TEXT("queue"),
			{ TEXT("cycled.Add"), TEXT("failed.Add"), TEXT("use_ended.Add") }));
	Sink->Reset();

	// Headless CalcFeat is zero. Lowering the threshold to zero makes the next real timed use pass,
	// proving the success callback rather than injecting Unlock.
	World.AcceptInput(TEXT("office_knob"), FName(TEXT("ResetDifficulty")),
		FElysiumVariant::Int(0), PlayerHandle, PlayerHandle);
	TestEqual(TEXT("ResetDifficulty clears the resolved-attempt count"), LiveKnob->SkillAttempts, 0);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	World.Tick(10.0);
	TestEqual(TEXT("the passing threshold emits success"),
		ReadCounter(World.FindByName(TEXT("succeeded"))), 1.0f);
	TestEqual(TEXT("success increments the resolved-attempt count"), LiveKnob->SkillAttempts, 1);
	TestEqual(TEXT("success emits the unlock output"),
		ReadCounter(World.FindByName(TEXT("unlocked"))), 1.0f);
	// The knob is the authority a knobbed door reads, so picking it is enough to admit the player —
	// the door's own LOCKED byte is untouched and stays set, exactly as retail leaves it.
	TestFalse(TEXT("success unlocks the doorknob"), LiveKnob->IsUseLocked());
	TestTrue(TEXT("success does not write the door's own lock byte"), LiveDoor->bLocked);
	TestFalse(TEXT("the unlocked knob admits the player at the door"),
		LiveDoor->IsUseRefused(PlayerHandle));
	TestEqual(TEXT("unlocked knob drives the door's real OnOpen"),
		ReadCounter(World.FindByName(TEXT("opened"))), 1.0f);
	TestTrue(TEXT("opening never consumes the reusable lockpick"),
		Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	TestTrue(TEXT("success queues cycle, success, unlock, then the attached door open"),
		Sink->AppearsInOrder(TEXT("queue"), { TEXT("cycled.Add"), TEXT("succeeded.Add"),
			TEXT("unlocked.Add"), TEXT("opened.Add") }));

	return true;
}


// Inventory containers: combat-character ownership, deferred seed materialization,
// the recovered inputs, authoritative Take/Give, output delivery, and restore idempotence.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInventoryContainerTest,
	"Elysium.Substrate.InventoryContainers", GElysiumTestFlags)
bool FElysiumInventoryContainerTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTestItemTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Animated = Registry.Find(FName(TEXT("item_container_animated")));
	if (!TestNotNull(TEXT("animated container class registered"), Animated))
	{
		return false;
	}
	TestEqual(TEXT("animated container inherits the container leaf"), Animated->BaseName,
		FName(TEXT("item_container")));
	for (const TCHAR* Input : { TEXT("SpawnItemInContainer"), TEXT("AddEntityToContainer"),
		TEXT("DeleteItems") })
	{
		TestNotNull(*FString::Printf(TEXT("container input %s resolves through the chain"), Input),
			reinterpret_cast<const void*>(Registry.FindInput(*Animated, FName(Input))));
	}
	TestNotNull(TEXT("container owns the combat-character inventory chain"),
		reinterpret_cast<const void*>(Registry.FindField(*Animated, FName(TEXT("m_hActiveWeapon")))));

	auto Counter = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("100"));
		return Def;
	};
	auto Wire = [](FElysiumEntityDef& Def, const TCHAR* Output, const TCHAR* Target)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = Target;
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Def.Outputs.Add(MoveTemp(Row));
	};
	auto ReadCounter = [](const FElysiumEntity* Entity)
	{
		if (!Entity)
		{
			return -1.f;
		}
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		const TPair<FString, FString>* Value = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Value"); });
		return Value ? FCString::Atof(*Value->Value) : -1.f;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__inventory_container_test__");
	FElysiumEntityDef Container;
	Container.Classname = TEXT("item_container_animated");
	Container.TargetName = TEXT("crate");
	Container.ModelMesh = TEXT("test_crate");
	Container.Keys.Add(TEXT("model"), TEXT("models/test/crate.mdl"));
	Container.Keys.Add(TEXT("use_icon"), TEXT("5"));
	Container.Keys.Add(TEXT("equip0"), TEXT("item_g_lockpick"));
	Wire(Container, TEXT("OnItemRemove"), TEXT("removed"));
	Wire(Container, TEXT("OnItemInsert"), TEXT("inserted"));
	Wire(Container, TEXT("OnUseBegin"), TEXT("use_began"));
	Wire(Container, TEXT("OnUseEnd"), TEXT("use_ended"));
	Defs.Defs.Add(MoveTemp(Container));

	FElysiumEntityDef ContainerLock;
	ContainerLock.Classname = TEXT("item_container_lock");
	ContainerLock.TargetName = TEXT("crate_lock");
	ContainerLock.Keys.Add(TEXT("parentname"), TEXT("crate"));
	ContainerLock.Keys.Add(TEXT("difficulty"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(ContainerLock));

	FElysiumEntityDef Loose;
	Loose.Classname = TEXT("item_w_thirtyeight");
	Loose.TargetName = TEXT("loose_gun");
	Defs.Defs.Add(MoveTemp(Loose));
	for (const TCHAR* Name : { TEXT("removed"), TEXT("inserted"), TEXT("use_began"), TEXT("use_ended") })
	{
		Defs.Defs.Add(Counter(Name));
	}
	FElysiumEntityDefs RestoreDefs = Defs;

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	// The lock is deliberately bodiless. It retains the logical parent without requiring two
	// Unreal components or producing a false physical-attachment warning, both initially and after
	// save restore.
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // container seed think precedes event service

	FElysiumItemContainer* Live = World.FindByName(TEXT("crate"))
		? World.FindByName(TEXT("crate"))->AsItemContainer() : nullptr;
	FElysiumLockableEntity* LiveLock = World.FindByName(TEXT("crate_lock"))
		? World.FindByName(TEXT("crate_lock"))->AsLockableEntity() : nullptr;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("container leaf constructed"), Live)
		|| !TestNotNull(TEXT("container lock leaf constructed"), LiveLock)
		|| !TestNotNull(TEXT("player constructed"), Player))
	{
		return false;
	}
	TestEqual(TEXT("equip0 materializes one real contained entity"), Live->Inventory.Num(), 1);
	FElysiumItem* Seed = Live->Inventory.At(*Live, 0);
	TestTrue(TEXT("seed item is owned by the container"), Seed && Seed->Owner == Live->Handle);
	TestEqual(TEXT("container body requests its existing prop representation"),
		Services.Count(TEXT("BuildPropVisual crate")), 1);
	TestTrue(TEXT("attached container lock starts locked"), LiveLock->IsUseLocked());
	TestFalse(TEXT("locked attachment suppresses the container's direct +use anchor"),
		Services.UseAnchorEnabled.FindRef(Live->Handle));
	World.AcceptInput(TEXT("crate_lock"), FName(TEXT("Unlock")), FElysiumVariant::Void(),
		Player->Handle, Player->Handle);
	TestFalse(TEXT("Unlock releases the attached container lock"), LiveLock->IsUseLocked());
	TestTrue(TEXT("unlock restores the container +use anchor"),
		Services.UseAnchorEnabled.FindRef(Live->Handle));

	// The world-owned explicit session is entered through the ordinary +use query, then the same
	// narrow intents used by CommonUI move one authoritative slot at a time.
	FElysiumUseCandidate ContainerHit;
	ContainerHit.Owner = Live->Handle;
	ContainerHit.Selection = EElysiumUseSelection::Exact;
	Services.UseQuery.Candidates = { ContainerHit };
	World.UpdatePlayerInteraction();
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("container records its exclusive user"), Live->CurrentUser, Player->Handle);
	TestEqual(TEXT("container +use owns an explicit session"), World.GetLastUseOutcome(),
		EElysiumUseOutcome::SessionStarted);
	World.Tick(0.0);
	TestEqual(TEXT("container queues OnUseBegin once"),
		ReadCounter(World.FindByName(TEXT("use_began"))), 1.0f);
	TestTrue(TEXT("an open loot container blocks saving"),
		World.ScriptedSessionSaveBlockReason().Contains(TEXT("loot container")));
	FElysiumLootView Loot;
	TestTrue(TEXT("open container publishes a loot view"), World.BuildLootView(Loot));
	TestEqual(TEXT("loot view owner is the captured container"), Loot.Owner, Live->Handle);
	TestEqual(TEXT("loot view exposes the seeded container item"), Loot.ContainerItems.Num(), 1);
	AddExpectedError(TEXT("inventory transfer refused: source="),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("a stale loot slot is refused and reported"), World.PlayerLootTake(99));
	TestTrue(TEXT("Take commits through the server transaction"),
		World.PlayerLootTake(0));
	TestEqual(TEXT("loot transfer into the player emits one item notification"),
		Services.Notifications.Num(), 1);
	TestEqual(TEXT("Take empties the source"), Live->Inventory.Num(), 0);
	TestTrue(TEXT("Take gives the same item classname to the player"),
		Player->Inventory.Has(*Player, TEXT("item_g_lockpick")));
	World.Tick(0.0);
	TestEqual(TEXT("Take enqueues OnItemRemove"),
		ReadCounter(World.FindByName(TEXT("removed"))), 1.f);

	TestTrue(TEXT("Give commits through the same transaction"),
		World.PlayerLootGive(0));
	TestEqual(TEXT("Give returns one item to the container"), Live->Inventory.Num(), 1);
	TestEqual(TEXT("Give removes it from the player"), Player->Inventory.Num(), 0);
	World.Tick(0.0);
	TestEqual(TEXT("Give enqueues OnItemInsert"),
		ReadCounter(World.FindByName(TEXT("inserted"))), 1.f);

	World.AcceptInput(TEXT("crate"), FName(TEXT("AddEntityToContainer")),
		FElysiumVariant::String(TEXT("loose_*")), Live->Handle, Live->Handle);
	FElysiumItem* LooseGun = World.FindByName(TEXT("loose_gun"))
		? World.FindByName(TEXT("loose_gun"))->AsItem() : nullptr;
	TestTrue(TEXT("AddEntityToContainer accepts an unowned wildcard match"),
		LooseGun && LooseGun->Owner == Live->Handle);
	TestEqual(TEXT("container now owns two items"), Live->Inventory.Num(), 2);

	World.AcceptInput(TEXT("crate"), FName(TEXT("DeleteItems")), FElysiumVariant::Void(),
		Live->Handle, Live->Handle);
	TestEqual(TEXT("DeleteItems clears all slots"), Live->Inventory.Num(), 0);
	TestTrue(TEXT("DeleteItems destroys every contained entity"),
		Seed && Seed->IsDead() && LooseGun && LooseGun->IsDead());

	World.AcceptInput(TEXT("crate"), FName(TEXT("SpawnItemInContainer")),
		FElysiumVariant::String(TEXT("item_w_tire_iron")), Live->Handle, Live->Handle);
	TestEqual(TEXT("SpawnItemInContainer creates one contained item"), Live->Inventory.Num(), 1);
	FElysiumItem* Stack = Live->Inventory.At(*Live, 0);
	if (TestNotNull(TEXT("spawned stack resolves"), Stack))
	{
		Stack->ItemCount = 3;
		TestTrue(TEXT("Take splits one unit from a multi-count stack"),
			World.PlayerLootTake(0));
		TestEqual(TEXT("split leaves the source entity with two units"), Stack->ItemCount, 2);
		FElysiumItem* PlayerStack = Player->Inventory.FindOrdinary(*Player, TEXT("item_w_tire_iron"));
		TestTrue(TEXT("split creates one destination stack unit"),
			PlayerStack && PlayerStack->ItemCount == 1);
		TestTrue(TEXT("Give splits one unit back through the inverse path"),
			World.PlayerLootGive(0));
		TestEqual(TEXT("inverse split restores the container stack"), Stack->ItemCount, 3);
		TestEqual(TEXT("inverse split consumes the last destination entity"), Player->Inventory.Num(), 0);
	}

	TestTrue(TEXT("loot close completes the captured session"), World.PlayerCloseLoot());
	World.Tick(World.NowSeconds());
	TestFalse(TEXT("loot close releases exclusive ownership"), Live->CurrentUser.IsSet());
	TestEqual(TEXT("loot close queues OnUseEnd once"),
		ReadCounter(World.FindByName(TEXT("use_ended"))), 1.0f);
	TestTrue(TEXT("closed loot session no longer blocks saving"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());

	// The seed latch and runtime item definitions make a save restore idempotent.
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	FElysiumRecordingServices RestoreServices;
	RestoreServices.bHasPlayer = true;
	FElysiumEntityWorld Restored(/*Owner*/ nullptr, /*GameState*/ nullptr, RestoreServices.Bundle());
	Restored.Load(MoveTemp(RestoreDefs));
	Restored.SpawnPlayer();
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(0.0);
	Restored.Tick(0.0);
	TestEqual(TEXT("inventory reconstruction during restore remains silent"),
		RestoreServices.Notifications.Num(), 0);
	FElysiumItemContainer* RestoredContainer = Restored.FindByName(TEXT("crate"))
		? Restored.FindByName(TEXT("crate"))->AsItemContainer() : nullptr;
	TestTrue(TEXT("restored container resolves"), RestoredContainer != nullptr);
	if (RestoredContainer)
	{
		TestEqual(TEXT("restore does not materialize equip seeds a second time"),
			RestoredContainer->Inventory.Num(), 1);
		FElysiumItem* RestoredStack = RestoredContainer->Inventory.FindOrdinary(
			*RestoredContainer, TEXT("item_w_tire_iron"));
		TestTrue(TEXT("restored contents are the saved runtime stack"),
			RestoredStack && RestoredStack->ItemCount == 3);
		FElysiumEntity* RestoredLock = Restored.FindByName(TEXT("crate_lock"));
		TestTrue(TEXT("restored lock keeps its unlocked result"),
			RestoredLock && RestoredLock->AsLockableEntity()
			&& !RestoredLock->AsLockableEntity()->IsUseLocked());
		TestEqual(TEXT("restored container keeps its logical lock attachment"),
			RestoredContainer->AttachedLock, RestoredLock ? RestoredLock->Handle : FElysiumEntityHandle());
		TestTrue(TEXT("restored unlocked state re-enables the container anchor"),
			RestoreServices.UseAnchorEnabled.FindRef(RestoredContainer->Handle));
	}

	return true;
}

} // namespace ElysiumInventoryTests

#endif // WITH_DEV_AUTOMATION_TESTS
