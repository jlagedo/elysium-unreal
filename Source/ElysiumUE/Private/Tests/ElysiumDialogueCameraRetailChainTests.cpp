// SC9 — the dialogue and script chain around the camera.
//
// Retail's two entry points and the asymmetry between them:
//
//   * `CBasePlayer::SetCamera` `FUN_1017d020` — the script native's one caller (115 shipped call
//     sites, all `pc.SetCamera("Shot")`). With nothing adopted it creates a runtime
//     `camera_cinematic` and adopts it, falling back to the verbatim `"DialogDefault"`
//     (`s_DialogDefault_10587f04`) when the name does not load; with a camera already adopted it
//     re-shots THAT camera, with the same fallback and **without** running the shot start
//     (`FUN_1006e8e0`). It never immobilizes.
//   * `CBasePlayer::StartPlayerDialog` `0x10178280` — `CDialog::Acquire` (`0x100e05f0`), then
//     `SetDialogPartner`, `SetImmobilized(true)`, the `+0x1e01` active-weapon latch, the
//     `item_w_unarmed` holster, and then EITHER a runtime camera off the NPC's `default_camera`
//     (no anchor entities, so every anchor comes from the shot file's own `Position`) OR, for a
//     `CPayphone` partner, `StartGrappleAttack(this, npc, 5)` and **no camera at all**.
//     `Acquire`'s third outcome — a bark (`+0x30e9`) — sends its line and returns false, so none of
//     the tail runs. **There is no `DialogDefault` fallback on this path**: a `default_camera` that
//     does not load leaves the conversation cameraless.
//   * `CBasePlayer::EndPlayerDialog` `0x10178400` — `UTIL_Remove(GetCineCamera())`
//     **unconditionally** (not gated on the disposable bit `+0x204 & 0x4`), `SetCineCamera(NULL)`,
//     `SetImmobilized(false)`, the weapon re-draw, `SetDialogPartner(NULL)` and the events-manager
//     notify, all on one frame with **no blend** (M1, ruled 2026-09-07).
//
// Recovery: `docs/project/camera_scripted.md` §SC9, and `$ELYSIUM_WORK_ROOT/_camera_recovery/`
// `rc_group_de.md` RC5 (the 30-degree head cone and `MaintainEyeDirection`'s full chain), RC6 (the
// admission, the payphone arm, and the proof that `.dlg` has no camera column) and RC13 (the
// grapple role pair). The facts land in `docs/vtmb/camera-view-modes.md` §"How dialogue drives the
// camera".
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraService.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Tests/ElysiumDialogueTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

namespace ElysiumDialogueCameraRetailChainTests
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* const GSpeaker = TEXT("speaker");
	const TCHAR* const GPhone   = TEXT("phone");

	// A camera service that composes nothing. The port's authored-profile ladder is a NAMED
	// MODERNIZATION standing in a gap retail leaves empty, so this suite runs it OFF
	// (`bEnabled == false`) wherever retail's own answer is what is being asserted.
	class FLadderService final : public IElysiumCameraService
	{
	public:
		bool bEnabled = false;
		int32 AcquireCount = 0;
		int32 ReleaseCount = 0;
		int32 NextSlot = 1;
		TSet<int32> LiveSlots;
		FElysiumCameraRequest LastRequest;
		FElysiumResolvedCameraState Resolved;

		virtual FElysiumCameraHandle AcquireCamera(const FElysiumCameraRequest& Request) override
		{
			FElysiumCameraHandle Handle;
			Handle.Slot = NextSlot++;
			Handle.Generation = 1;
			Handle.Epoch = 91;
			LiveSlots.Add(Handle.Slot);
			LastRequest = Request;
			++AcquireCount;
			return Handle;
		}
		virtual bool UpdateCamera(FElysiumCameraHandle Handle,
			const FElysiumCameraRequest& Request) override
		{
			if (!IsCameraLive(Handle)) { return false; }
			LastRequest = Request;
			return true;
		}
		virtual bool ReleaseCamera(FElysiumCameraHandle Handle) override
		{
			if (Handle.Epoch != 91 || LiveSlots.Remove(Handle.Slot) == 0) { return false; }
			++ReleaseCount;
			return true;
		}
		virtual bool IsCameraLive(FElysiumCameraHandle Handle) const override
		{
			return Handle.Epoch == 91 && LiveSlots.Contains(Handle.Slot);
		}
		virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest&,
			FString& OutReason) const override
		{
			OutReason.Reset();
			return true;
		}
		virtual bool DialogueCamerasEnabled() const override { return bEnabled; }
		virtual void GetDialogueProfiles(TArray<FElysiumDialogueCameraProfile>& Out) const override
		{
			Out = ElysiumDialogueCamera::DefaultProfiles();
		}
		virtual const FElysiumResolvedCameraState& ResolvedCamera() const override { return Resolved; }
	};

	// A shot the table can find, with its one anchor left to the shot file's own `Position` keyword
	// — which is what `FUN_10070470(name, NULL x4)` leaves every anchor to.
	FElysiumCameraShotDef Shot(const TCHAR* Name, EElysiumShotPosition Position,
		const FVector& OffsetOrigin, bool bDialogPOV = false)
	{
		FElysiumCameraShotDef Def;
		Def.Name = Name;
		Def.End.bPresent = true;
		Def.End.Position = Position;
		Def.End.AttachPos = TEXT("Origin");
		Def.End.AttachPoint = EElysiumShotAttachPos::Origin;
		Def.End.Attach = EElysiumShotAttach::Follow;
		Def.End.OffsetOrigin = OffsetOrigin;
		Def.Constraints.bDialogPOV = bDialogPOV;
		return Def;
	}

	FElysiumEntityDefs MakeDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__dialogue_camera_retail_chain__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VPedestrian");
		Npc.TargetName = GSpeaker;
		Npc.Origin = FVector(200.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Npc));

		// `CPayphone` — a `CAI_BaseNPCTroika` subclass whose one shipped entity classname is
		// `npc_payphone` (RC6). Placed inside mode 5's 144-unit x/y admission.
		FElysiumEntityDef Phone;
		Phone.Classname = TEXT("npc_payphone");
		Phone.TargetName = GPhone;
		Phone.Origin = FVector(100.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Phone));

		return Defs;
	}

	// One spoken NPC line, and — when `bBark` — the `(Auto-End)` PC row whose passing turn is what
	// raises `CDialog +0x30e9` in `fill_packet` (`0x100e7da0`).
	TSharedRef<FElysiumDlgConversation> MakeConversation(bool bBark)
	{
		using ElysiumDialogueTestHelpers::ElysiumDlgRow;
		using ElysiumDialogueTestHelpers::ElysiumDlgBytes;
		TArray<FString> Rows;
		Rows.Add(ElysiumDlgRow(1, TEXT("A word with you."), TEXT("#"), FString(), FString()));
		if (bBark)
		{
			Rows.Add(ElysiumDlgRow(2, TEXT("(Auto-End)"), TEXT("0"), FString(), FString()));
		}
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get());
		return MakeShared<FElysiumDlgConversation>(File, /*bMale*/ true, /*bMalk*/ false,
			[](const FString&) { return true; }, [](const FString&) {});
	}

	FElysiumCameraCinematic* Adopted(FElysiumEntityWorld& World)
	{
		FElysiumEntity* Ent = World.Resolve(World.CineCameraEntity());
		return Ent ? Ent->AsCameraCinematic() : nullptr;
	}

	// The two melee stand-ins the holster pair needs, exactly as `Elysium.Substrate.DialogueEntry`
	// builds them: `item_w_unarmed` is the fallback `vfunc0x724` switches to, the iron is the
	// drawable weapon `+0x1e01` remembers.
	void FillWeaponTable(FElysiumItemTable& Table)
	{
		FElysiumItemDef Unarmed;
		Unarmed.Classname = TEXT("item_w_unarmed");
		Unarmed.PrintName = Unarmed.Classname;
		Unarmed.Type = EElysiumItemType::WeaponMelee;
		Unarmed.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Table.Items.Add(MoveTemp(Unarmed));

		FElysiumItemDef Iron;
		Iron.Classname = TEXT("item_w_test_iron");
		Iron.PrintName = Iron.Classname;
		Iron.Type = EElysiumItemType::WeaponMelee;
		Iron.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Iron.bWieldable = true;
		Table.Items.Add(MoveTemp(Iron));
		Table.Reindex();
	}

	// `ULocalPlayerSubsystem` has `ClassWithin = ULocalPlayer`; a transient local player gives the
	// real service its lifetime owner without a world, viewport, controller or RHI.
	UElysiumCameraService* MakeHeadlessCameraService()
	{
		ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
		return LocalPlayer ? NewObject<UElysiumCameraService>(LocalPlayer) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraRetailChainTest,
	"Elysium.Substrate.DialogueCamera.RetailChain", GFlags)

bool FElysiumDialogueCameraRetailChainTest::RunTest(const FString&)
{
	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	// Asking for a shot file that does not exist IS the subject of half this suite — both fallback
	// arms and the opener's cameraless case — and the shot table warns on the miss by design.
	AddExpectedError(TEXT("not found"), EAutomationExpectedErrorFlags::Contains, 0);
	// `SetShot`'s second lookup is `special-case.txt` (the two unshipped `CamMode` factories' file).
	// Seeded empty so a corpus-free run does not warn about a file this suite never asks for.
	ElysiumCameraShots::InstallNamed(TEXT("special-case"), TArrayView<const FElysiumCameraShotDef>());

	// The shot files this suite names, plus the verbatim `"DialogDefault"` literal. Nothing installs
	// `NoSuchShot`, which is the name every fallback case asks for.
	ElysiumCameraShots::Install(TEXT("DialogDefault"),
		Shot(TEXT("DialogDefault"), EElysiumShotPosition::World, FVector(40.0f, 0.0f, 65.0f)));
	ElysiumCameraShots::Install(TEXT("Tourette"),
		Shot(TEXT("Tourette"), EElysiumShotPosition::Player, FVector(120.0f, 0.0f, 60.0f),
			/*bDialogPOV*/ true));
	ElysiumCameraShots::Install(TEXT("tJeanette"),
		Shot(TEXT("tJeanette"), EElysiumShotPosition::Player, FVector(-90.0f, 30.0f, 60.0f)));
	ElysiumCameraShots::Install(TEXT("PartnerShot"),
		Shot(TEXT("PartnerShot"), EElysiumShotPosition::DialogTarget, FVector(40.0f, 0.0f, 65.0f)));

	FElysiumItemTable Table;
	FillWeaponTable(Table);
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- 1. `SetCamera`'s `"DialogDefault"` fallback, on BOTH arms ----------------------------
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("the world has a player"), Player))
		{
			return false;
		}

		// Arm A — nothing adopted.
		// `cam = FUN_10070470(name); if (!cam) cam = FUN_10070470("DialogDefault"); SetCineCamera(cam)`.
		World.SetScriptedCamera(TEXT("NoSuchShot"), FElysiumEntityHandle::Invalid());
		TestTrue(TEXT("SetCamera with no camera adopted lands on a camera"), World.HasScriptedCamera());
		TestEqual(TEXT("...and an unknown name falls back to the verbatim DialogDefault"),
			World.ScriptedCameraName(), FString(TEXT("DialogDefault")));
		TestTrue(TEXT("SetCamera does not immobilize (unlike InputStartShot)"), Player->IsMobile());

		const FElysiumEntityHandle First = World.CineCameraEntity();
		TestNotNull(TEXT("the create arm adopted a real camera_cinematic"), Adopted(World));

		// Arm B — a camera IS adopted, so `SetShot` runs in place on the same entity.
		World.SetScriptedCamera(TEXT("Tourette"), FElysiumEntityHandle::Invalid());
		TestTrue(TEXT("a second SetCamera re-shots the SAME camera rather than stacking"),
			World.CineCameraEntity() == First);
		TestEqual(TEXT("...and the slot reports the new shot"),
			World.ScriptedCameraName(), FString(TEXT("Tourette")));

		// The re-shot branch never calls `FUN_1006e8e0`, so the entity is not re-placed and the
		// shot-start anchor cache is not refilled: the camera stays where the FIRST shot put it.
		FElysiumCameraCinematic* Camera = Adopted(World);
		if (TestNotNull(TEXT("the re-shot camera still resolves"), Camera))
		{
			const FVector PlacedByFirstShot = Camera->PlacementOrigin;
			const FVector EntityOrigin = Camera->Origin;
			World.SetScriptedCamera(TEXT("tJeanette"), FElysiumEntityHandle::Invalid());
			TestTrue(TEXT("a mid-conversation SetCamera continues from the pose the FIRST shot set"),
				Camera->PlacementOrigin.Equals(PlacedByFirstShot, 0.01f));
			TestTrue(TEXT("...and does not re-place the entity"),
				Camera->Origin.Equals(EntityOrigin, 0.01f));

			// The fallback on the re-shot arm too.
			World.SetScriptedCamera(TEXT("NoSuchShot"), FElysiumEntityHandle::Invalid());
			TestEqual(TEXT("an unknown name on the RE-SHOT arm falls back to DialogDefault as well"),
				World.ScriptedCameraName(), FString(TEXT("DialogDefault")));
			TestTrue(TEXT("...still on the same camera"), World.CineCameraEntity() == First);
		}
		TestTrue(TEXT("no arm of SetCamera immobilizes"), Player->IsMobile());
	}

	// --- 2. The opener: immobilize, holster, adopt; and the close: cut, mobilize, restore -----
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
			EElysiumItemGroundModelState::Geometryless);
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);   // the NPC mind admits its body on its first think

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("the speaker spawned"), SpeakerEnt)
			|| !TestNotNull(TEXT("the world has a player"), Player))
		{
			return false;
		}

		const FElysiumEntityHandle Unarmed =
			Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_unarmed"));
		const FElysiumEntityHandle Iron =
			Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_test_iron"));
		if (FElysiumEntity* IronEnt = World.Resolve(Iron))
		{
			if (FElysiumItem* IronItem = IronEnt->AsItem())
			{
				Player->Inventory.SetActiveWeapon(*Player, *IronItem);
			}
		}
		TestTrue(TEXT("the weapon is in hand before the conversation"),
			Player->Inventory.ActiveWeapon == Iron);
		TestTrue(TEXT("and the player is mobile"), Player->IsMobile());

		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, TEXT("Tourette"));

		TestFalse(TEXT("StartPlayerDialog immobilizes (FUN_1015ef40)"), Player->IsMobile());
		TestTrue(TEXT("...and holsters to item_w_unarmed"),
			Player->Inventory.ActiveWeapon == Unarmed);
		TestTrue(TEXT("...remembering the drawable weapon (+0x1e01)"),
			Player->bDialogWeaponHolstered && Player->bDialogWeaponWasDrawable);
		TestTrue(TEXT("the NPC's default_camera is adopted into the ONE cine slot"),
			World.HasScriptedCamera());
		TestEqual(TEXT("...by name"), World.ScriptedCameraName(), FString(TEXT("Tourette")));
		TestEqual(TEXT("the opener publishes nothing on the authored-profile channel"),
			Ladder.AcquireCount, 0);

		const FElysiumEntityHandle CameraHandle = World.CineCameraEntity();
		World.CloseDialog(/*bSilent*/ false);

		TestTrue(TEXT("EndPlayerDialog mobilizes on the frame the camera dies"), Player->IsMobile());
		TestTrue(TEXT("...and re-draws the latched weapon"),
			Player->Inventory.ActiveWeapon == Iron);
		TestFalse(TEXT("...and forgets the latch"), Player->bDialogWeaponHolstered);
		TestFalse(TEXT("the cine slot is empty the same frame — no blend (M1)"),
			World.HasScriptedCamera());
		TestNull(TEXT("...and UTIL_Remove destroyed the camera entity"),
			World.Resolve(CameraHandle));
	}

	// --- 3. A conversation opened with no weapon drawn restores nothing -----------------------
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		FElysiumPlayer* Player = World.FindPlayer();
		if (SpeakerEnt == nullptr || Player == nullptr)
		{
			return false;
		}
		const FElysiumEntityHandle Before = Player->Inventory.ActiveWeapon;
		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, TEXT("Tourette"));
		TestFalse(TEXT("an empty-handed open latches nothing (+0x1e01 stays clear)"),
			Player->bDialogWeaponWasDrawable);
		World.CloseDialog(/*bSilent*/ false);
		TestTrue(TEXT("...so the close restores nothing"),
			Player->Inventory.ActiveWeapon == Before);
		TestTrue(TEXT("...and the player is mobile again"), Player->IsMobile());
	}

	// --- 4. The bark: `CDialog::Acquire` returns false, so NOTHING of the tail runs ------------
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
			EElysiumItemGroundModelState::Geometryless);
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		FElysiumPlayer* Player = World.FindPlayer();
		if (SpeakerEnt == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_unarmed"));
		const FElysiumEntityHandle Iron =
			Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_test_iron"));
		if (FElysiumEntity* IronEnt = World.Resolve(Iron))
		{
			if (FElysiumItem* IronItem = IronEnt->AsItem())
			{
				Player->Inventory.SetActiveWeapon(*Player, *IronItem);
			}
		}

		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ true),
			EElysiumDialogOpenerKind::Use, 0, TEXT("Tourette"));

		TestFalse(TEXT("a bark creates no camera"), World.HasScriptedCamera());
		TestFalse(TEXT("...not even a camera entity"), World.CineCameraEntity().IsSet());
		TestEqual(TEXT("...and nothing on the authored-profile channel either"),
			Ladder.AcquireCount, 0);
		TestTrue(TEXT("a bark does not immobilize"), Player->IsMobile());
		TestTrue(TEXT("...and does not holster"), Player->Inventory.ActiveWeapon == Iron);
		TestFalse(TEXT("...and latches nothing"), Player->bDialogWeaponHolstered);
	}

	// --- 5. A payphone partner takes the grapple arm and creates NO camera ---------------------
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* PhoneEnt = World.FindByName(GPhone);
		FElysiumCombatCharacter* Phone = PhoneEnt ? PhoneEnt->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("npc_payphone spawns as a character (CPayphone is a "
				"CAI_BaseNPCTroika subclass)"), Phone)
			|| Player == nullptr)
		{
			return false;
		}
		// Inside mode 5's 144-unit x/y admission (`CanStartGrappleAttack` `0x103285a0` step 6), with
		// a yaw of the phone's own so the 16-bit round trip is observable.
		Player->Origin = FVector::ZeroVector;
		Phone->Origin = FVector(100.0f, 0.0f, 0.0f);
		Phone->Angles = FVector(0.0f, 90.0f, 0.0f);

		// The shipped phones author `default_camera` and the arm ignores it outright.
		World.OpenDialog(PhoneEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, TEXT("DialogDefault"));

		TestFalse(TEXT("the payphone arm creates no camera at all"), World.HasScriptedCamera());
		TestFalse(TEXT("...not even an entity"), World.CineCameraEntity().IsSet());
		// With the ladder off, the port's own fallback publishes no POSE, which is retail's
		// cameraless conversation: the player's own view is what renders.
		TestFalse(TEXT("...and the port's fallback publishes no pose either"),
			Ladder.LastRequest.bOverridePose);
		TestTrue(TEXT("StartGrappleAttack(this, npc, 5) — the payphone mode"),
			Player->Grapple.Type == EElysiumGrappleType::Payphone);
		TestTrue(TEXT("...with the player as attacker and the phone as victim"),
			Player->Grapple.Role == EElysiumGrappleRole::Attacker
				&& Phone->Grapple.Role == EElysiumGrappleRole::Victim);
		TestTrue(TEXT("mode 5 holsters BOTH parties"),
			Player->Grapple.bHolsteredOnEnter && Phone->Grapple.bHolsteredOnEnter);
		// `((int)((90 + 180) * 182.04444885) & 0xFFFF) * 0.0054931640625 == 270`: the phone's own
		// yaw plus a half turn, so the player ends up facing INTO the phone — not along the
		// approach vector, which for a phone dead ahead on +X would be 0.
		TestTrue(TEXT("the facing is the phone's own angles through the 16-bit round trip"),
			FMath::IsNearlyEqual(static_cast<float>(Player->Angles.Y), 270.0f, 0.01f));
		TestFalse(TEXT("the payphone opener still immobilizes — only the camera step is replaced"),
			Player->IsMobile());

		World.CloseDialog(/*bSilent*/ false);
		TestTrue(TEXT("closing the payphone conversation mobilizes"), Player->IsMobile());
		TestTrue(TEXT("...and ends the grapple"),
			Player->Grapple.Type == EElysiumGrappleType::None);
	}

	// --- 6. No `DialogDefault` fallback on the opener; the UNCONDITIONAL remove ----------------
	{
		FLadderService Ladder;   // the profile ladder OFF: this is retail's own answer
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		FElysiumPlayer* Player = World.FindPlayer();
		if (SpeakerEnt == nullptr || Player == nullptr)
		{
			return false;
		}
		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, TEXT("NoSuchShot"));

		// **The asymmetry, asserted rather than smoothed.** `SetCamera` lands on `DialogDefault` for
		// this same name (case 1); `StartPlayerDialog` has no fallback at all and the conversation
		// runs cameraless.
		TestFalse(TEXT("a default_camera that does not load leaves the conversation cameraless"),
			World.HasScriptedCamera());
		TestFalse(TEXT("...with no camera entity either"), World.CineCameraEntity().IsSet());
		TestFalse(TEXT("the ladder publishes no pose while dialogue cameras are disabled"),
			Ladder.LastRequest.bOverridePose);
		TestFalse(TEXT("...but the opener still immobilized"), Player->IsMobile());

		// A **non-disposable** adopted camera — a map-placed `camera_cinematic` director is the
		// shipped case. `SetCineCamera`'s own destroy test (`old->+0x204 & 0x4`) would spare it;
		// `EndPlayerDialog`'s `UTIL_Remove` does not.
		const FElysiumEntityHandle NoAnchors[FElysiumShotBindings::Num] = {};
		const FElysiumEntityHandle Runtime = FElysiumCameraCinematic::CreateRuntimeCamera(World,
			TEXT("Tourette"), static_cast<int32>(EElysiumCineCamMode::NamedShot), NoAnchors);
		FElysiumEntity* RuntimeEnt = World.Resolve(Runtime);
		FElysiumCameraCinematic* Cine = RuntimeEnt ? RuntimeEnt->AsCameraCinematic() : nullptr;
		if (TestNotNull(TEXT("a camera to adopt non-disposably"), Cine))
		{
			Cine->bDisposable = false;
			World.SetCineCamera(Cine->Handle, Cine->PublishedShotId, /*bDisposable*/ false,
				Cine->ShotDef.Name);
			TestTrue(TEXT("the non-disposable camera is adopted"), World.HasScriptedCamera());

			World.CloseDialog(/*bSilent*/ false);
			TestFalse(TEXT("the dialogue end cut drops the slot the same frame"),
				World.HasScriptedCamera());
			TestNull(TEXT("...and the UNCONDITIONAL UTIL_Remove destroys even a "
				"non-disposable adopted camera"), World.Resolve(Runtime));
		}
	}

	// --- 6b. Both of the opener's failure arms run `SetCineCamera(NULL)` -----------------------
	//
	// `0x10178280`'s non-payphone arm is four statements and the last one is unconditional:
	//
	//     puVar6 = piVar5[0x1931];                       // npc->default_camera
	//     if (puVar6 == NULL) puVar6 = &DAT_106b8540;    // the empty string
	//     piVar5 = FUN_10070470(puVar6, NULL,NULL,NULL,NULL);
	//     FUN_1017cef0(this, piVar5);                    // cam may be NULL
	//
	// `FUN_1017cef0(this, NULL)` clears `m_iCameraOverrideIdx`, drops the handle and destroys the
	// outgoing camera when it is disposable. So a conversation opened while a terminal shot, a
	// `StartShot` shot or a previous conversation's camera is adopted **takes that camera down**,
	// whether or not the NPC's own `default_camera` loads.
	{
		FLadderService Ladder;   // the ladder OFF: retail's own answer is what is asserted
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		if (!TestNotNull(TEXT("the speaker exists"), SpeakerEnt))
		{
			return false;
		}

		// Arm one — a name the shot table does not hold. A DISPOSABLE camera is adopted first
		// (`FUN_10070470` marks every runtime camera `+0x204 |= 4`), which is the terminal /
		// `StartShot` / previous-conversation case.
		const FElysiumEntityHandle NoAnchors[FElysiumShotBindings::Num] = {};
		const FElysiumEntityHandle Outgoing = FElysiumCameraCinematic::CreateRuntimeCamera(World,
			TEXT("Tourette"), static_cast<int32>(EElysiumCineCamMode::NamedShot), NoAnchors);
		FElysiumEntity* OutgoingEnt = World.Resolve(Outgoing);
		if (TestNotNull(TEXT("a disposable camera to be standing in the slot"), OutgoingEnt))
		{
			FElysiumCameraCinematic* Cine = OutgoingEnt->AsCameraCinematic();
			World.SetCineCamera(Cine->Handle, Cine->PublishedShotId, Cine->bDisposable,
				Cine->ShotDef.Name);
			TestTrue(TEXT("the outgoing camera owns the slot"), World.HasScriptedCamera());

			World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
				EElysiumDialogOpenerKind::Use, 0, TEXT("NoSuchShot"));
			TestFalse(TEXT("a default_camera that does not load CLEARS the slot"),
				World.HasScriptedCamera());
			TestFalse(TEXT("...handle and all"), World.CineCameraEntity().IsSet());
			const FElysiumEntity* Dead = World.Resolve(Outgoing);
			TestTrue(TEXT("...and destroys the outgoing disposable camera with it"),
				Dead == nullptr || Dead->IsDead());
			World.CloseDialog(/*bSilent*/ false);
		}

		// Arm two — the NPC names no camera at all. Retail substitutes the empty string
		// (`&DAT_106b8540`), `FUN_10070470("")` answers NULL, and the same clear runs.
		const FElysiumEntityHandle Second = FElysiumCameraCinematic::CreateRuntimeCamera(World,
			TEXT("Tourette"), static_cast<int32>(EElysiumCineCamMode::NamedShot), NoAnchors);
		FElysiumEntity* SecondEnt = World.Resolve(Second);
		if (TestNotNull(TEXT("a second camera for the empty-name arm"), SecondEnt))
		{
			FElysiumCameraCinematic* Cine = SecondEnt->AsCameraCinematic();
			World.SetCineCamera(Cine->Handle, Cine->PublishedShotId, Cine->bDisposable,
				Cine->ShotDef.Name);
			TestTrue(TEXT("the second camera owns the slot"), World.HasScriptedCamera());

			World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
				EElysiumDialogOpenerKind::Use, 0, FString());
			TestFalse(TEXT("an NPC with no default_camera clears the slot too"),
				World.HasScriptedCamera());
			const FElysiumEntity* Dead = World.Resolve(Second);
			TestTrue(TEXT("...and takes the outgoing disposable camera down"),
				Dead == nullptr || Dead->IsDead());
			World.CloseDialog(/*bSilent*/ false);
		}
	}

	// --- 6c. `SetShot` stamps `m_nClientResetFrame`, so a per-line `SetCamera` CUTS ------------
	//
	// `FUN_1006e130`'s tail is `+0x63c = engine->GetFrameCount(); +0x638 = camMode; return 1;`, so
	// `FUN_1006e8e0` is not the only stamp site — and it is exactly the site `FUN_1017d020`'s
	// re-shot arm skips. Every one of the 115 shipped `pc.SetCamera("Shot")` calls therefore arms
	// `m_bShotStartPending` on the client and runs `FUN_10002210`, which for a `Start`-bearing shot
	// re-seeds from the **goal**: `m_flSpeed = 0`, the three turn rates 0, `m_bPositionSettled = 1`
	// and the pose on the framing. A cut, not a dolly.
	{
		// A `Start`-bearing shot: `StartsOnGoal()` is `(flags & 2) == 0 || (flags & 1) != 0`, and
		// ten shipped shots (`kilpatrick`, `tong`, `npcfollow*`, `stealth_kill`, …) have the bit.
		FElysiumCameraShotDef Kilpatrick;
		Kilpatrick.Name = TEXT("Kilpatrick");
		Kilpatrick.Start.bPresent = true;
		Kilpatrick.Start.Position = EElysiumShotPosition::Player;
		Kilpatrick.Start.AttachPos = TEXT("Origin");
		Kilpatrick.Start.AttachPoint = EElysiumShotAttachPos::Origin;
		Kilpatrick.Start.Attach = EElysiumShotAttach::Follow;
		Kilpatrick.Start.OffsetOrigin = FVector(-150.0f, 0.0f, 90.0f);
		Kilpatrick.Constraints.MoveSpeed = 150.0f * ElysiumMove::U;
		Kilpatrick.Constraints.MoveAccel = 50.0f * ElysiumMove::U;
		ElysiumCameraShots::Install(TEXT("Kilpatrick"), Kilpatrick);

		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		// The opener's camera is in the slot; the per-line `SetCamera` re-shots that same entity.
		World.SetScriptedCamera(TEXT("DialogDefault"), FElysiumEntityHandle::Invalid());
		const FElysiumEntityHandle Live = World.CineCameraEntity();
		TestTrue(TEXT("a camera is adopted"), Live.IsSet());

		const int32 StampsBefore = Services.Count(TEXT("RestartCameraShot"));
		World.SetScriptedCamera(TEXT("Kilpatrick"), FElysiumEntityHandle::Invalid());
		TestTrue(TEXT("the re-shot lands on the same camera"), World.CineCameraEntity() == Live);
		TestTrue(TEXT("and SetShot re-stamped m_nClientResetFrame, which arms shot start"),
			Services.Count(TEXT("RestartCameraShot")) > StampsBefore);

		// The re-shot arm runs no shot start, so the new record reaches the channel on the next
		// 24 Hz think — which is retail's own split, and is why the stamp above has to come from
		// `SetShot` rather than from `FUN_1006e8e0`.
		World.Tick(0.06);
		World.Tick(0.12);

		// The other half, at the value level the client works at: a new reset frame arms shot
		// start, and shot start on a `Start`-bearing shot seeds from the goal.
		FElysiumCameraShot Goal = Services.LastCameraShot;
		TestTrue(TEXT("the re-shot's goal is Start-bearing, so it starts ON the goal"),
			Goal.StartsOnGoal());

		FElysiumScriptedShotTracker Tracker;
		FElysiumShotStartEdges Edges;
		// Where the previous shot's dolly had got to: away from the framing and moving.
		FElysiumViewSetup LiveView;
		LiveView.Location = FVector(-800.0f, 300.0f, 160.0f);
		LiveView.Rotation = FRotator(5.0f, 200.0f, 0.0f);
		Tracker.Start(Goal, LiveView);
		Tracker.Location = LiveView.Location;
		Tracker.Speed = 120.0f;
		Tracker.bPositionSettled = false;

		Goal.ResetFrame += 1;   // the stamp `SetShot` just asked the embodiment for
		Edges.OnDataChanged(Goal, Tracker);
		TestTrue(TEXT("a new reset frame arms shot start"), Edges.bShotStartPending);
		Tracker.Start(Goal, LiveView);
		TestTrue(TEXT("a per-line SetCamera onto a Start-bearing shot CUTS to the goal"),
			Tracker.Location.Equals(Goal.Origin, 0.01f));
		TestEqual(TEXT("...with the speed zeroed, so nothing dollies"), Tracker.Speed, 0.0f);
		TestTrue(TEXT("...and the turn rates with it"), Tracker.TurnRate.IsNearlyZero());
		TestTrue(TEXT("...and the position marked settled"), Tracker.bPositionSettled);
	}

	// --- 7. `DialogTarget` resolves to the player's dialogue partner ---------------------------
	{
		FLadderService Ladder;
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Ladder;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		if (SpeakerEnt == nullptr)
		{
			return false;
		}
		const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(TEXT("PartnerShot"));
		if (!TestNotNull(TEXT("the DialogTarget fixture shot loads"), Def))
		{
			return false;
		}

		// No conversation: retail's `subject+0xFE8` is a dead EHANDLE and the anchor answers NULL,
		// so the resolve falls through to the world-origin tail and does not follow the NPC.
		FElysiumCameraShot Closed;
		FElysiumCameraDirector::Resolve(&World, *Def, FElysiumEntityHandle::Invalid(), Closed);
		SpeakerEnt->Origin += FVector(500.0f, 0.0f, 0.0f);
		FElysiumCameraShot ClosedMoved;
		FElysiumCameraDirector::Resolve(&World, *Def, FElysiumEntityHandle::Invalid(), ClosedMoved);
		TestTrue(TEXT("with no conversation open, DialogTarget does not follow an NPC"),
			ClosedMoved.Origin.Equals(Closed.Origin, 0.01f));
		SpeakerEnt->Origin -= FVector(500.0f, 0.0f, 0.0f);

		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, FString());
		if (!TestNotNull(TEXT("the conversation opened"), World.GetOpenDialog()))
		{
			return false;
		}
		FElysiumCameraShot Open;
		FElysiumCameraDirector::Resolve(&World, *Def, FElysiumEntityHandle::Invalid(), Open);
		SpeakerEnt->Origin += FVector(500.0f, 0.0f, 0.0f);
		FElysiumCameraShot OpenMoved;
		FElysiumCameraDirector::Resolve(&World, *Def, FElysiumEntityHandle::Invalid(), OpenMoved);
		TestTrue(TEXT("DialogTarget is the player's dialogue partner and follows where it goes"),
			!OpenMoved.Origin.Equals(Open.Origin, 1.0f));
		TestTrue(TEXT("...by the partner's own displacement"),
			FMath::Abs(static_cast<float>(OpenMoved.Origin.X - Open.Origin.X) - 500.0f) < 1.0f);
		World.CloseDialog(/*bSilent*/ true);
	}

	// --- 8. `DialogPOV`: the 30-degree head cone, the fall-through, and the hysteresis ---------
	// `FUN_10325da0` is `dot(headForward, normalize(target - headPos)) > 0.866` (a `double`
	// `0x1049e0b8`, `acos = 29.9995 degrees`) about the head bone's LIVE forward. A refusal falls to
	// the NEXT step of `MaintainEyeDirection`'s chain (`m_hTargetEnt`, enemy, navigator goal, hint,
	// scan) — never back to the player's eye, which is the arm that was just refused.
	{
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumEntity* SpeakerEnt = World.FindByName(GSpeaker);
		FElysiumCombatCharacter* Speaker = SpeakerEnt ? SpeakerEnt->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (Speaker == nullptr || Player == nullptr)
		{
			return false;
		}
		Speaker->Origin = FVector::ZeroVector;
		Speaker->NextFidgetTime = TNumericLimits<float>::Max();
		Player->Origin = FVector(500.0f, 0.0f, 0.0f);   // dead ahead, inside the cone

		World.OpenDialog(SpeakerEnt->Handle, MakeConversation(/*bBark*/ false),
			EElysiumDialogOpenerKind::Use, 0, FString());

		const FVector Head(0.0f, 0.0f, ElysiumMove::StandViewZ);
		const FVector Forward(1.0f, 0.0f, 0.0f);
		const FElysiumEyeTargetTuning Tuning;
		const FVector Ahead = Head + Forward * (500.0f * ElysiumMove::U);

		// Inside the cone: the arm aims at the camera's own origin, not the player's eye.
		const FVector CameraInside(420.0f, 60.0f, ElysiumMove::StandViewZ + 40.0f);
		Speaker->TickGaze(0.0f, 0.0f, Head, Forward, Tuning, &CameraInside);
		TestTrue(TEXT("a DialogPOV point inside the 30-degree cone is aimed at"),
			Speaker->EyeLookTarget.Equals(CameraInside, 0.1f));

		// Outside it: the chain CONTINUES. With a live dialogue partner the idle scan is suppressed
		// for the whole conversation (`CAI_BaseNPCTroika::FUN_102bff20` pushes `+0x5d6c` to
		// `curtime + 2.0`), and nothing supplies a target entity, an enemy, a goal or a heard sound,
		// so the tail's straight-ahead is the answer — NOT the player's eye.
		const FVector CameraOutside(100.0f, 500.0f, ElysiumMove::StandViewZ);
		Speaker->TickGaze(0.0f, 0.0f, Head, Forward, Tuning, &CameraOutside);
		TestFalse(TEXT("a refused DialogPOV target does NOT fall back to the partner's eye"),
			Speaker->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));
		TestTrue(TEXT("...it falls through the ordinary chain to its straight-ahead tail"),
			Speaker->EyeLookTarget.Equals(Ahead, 0.1f));

		// **Hysteretic.** The basis is the head's own live forward, so rotating the head across the
		// boundary flips the verdict on the SAME world point. `CameraOutside` sits at 78.7 degrees
		// off +X; a head turned to +Y brings it to 11.3 degrees, inside the cone.
		const FVector TurnedForward(0.0f, 1.0f, 0.0f);
		Speaker->TickGaze(0.0f, 0.0f, Head, TurnedForward, Tuning, &CameraOutside);
		TestTrue(TEXT("the gate is measured on the head's live forward, so turning the head "
			"admits the same point"), Speaker->EyeLookTarget.Equals(CameraOutside, 0.1f));
		Speaker->TickGaze(0.0f, 0.0f, Head, Forward, Tuning, &CameraOutside);
		TestFalse(TEXT("...and turning it back refuses it again"),
			Speaker->EyeLookTarget.Equals(CameraOutside, 0.1f));

		World.CloseDialog(/*bSilent*/ true);
	}

	// --- 9. The authored-profile channel's release is a CUT (M1) ------------------------------
	// The dialogue's own channel-B request, when the ladder is on, must leave zero residual weight
	// on the frame it is released — there is no blend field anywhere on `C_BaseCineCamera`, and
	// retail hands control back on the tick the camera dies.
	{
		UElysiumCameraService* Service = MakeHeadlessCameraService();
		if (TestNotNull(TEXT("a headless camera service"), Service))
		{
			Service->BeginMapEpoch(41);
			FElysiumCameraRequest Request;
			Request.Kind = EElysiumCameraRequestKind::Dialogue;
			Request.Priority = 500;
			Request.bOverridePose = true;
			Request.BlendInSeconds = 0.0f;
			Request.BlendOutSeconds = 2.0f;   // deliberately non-zero: the CUT is the channel's rule
			Request.Shot.Origin = FVector(10.0f, 20.0f, 30.0f);
			Request.Shot.bUseLookAt = false;

			const FElysiumCameraHandle Handle = Service->AcquireCamera(Request);
			++GFrameCounter;
			Service->Advance(1.0f / 60.0f);
			TestTrue(TEXT("the dialogue request is composed at full weight"),
				FMath::IsNearlyEqual(Service->ResolvedCamera().Weight, 1.0f, 0.001f));

			Service->ReleaseCamera(Handle);
			TestTrue(TEXT("the dialogue release leaves ZERO residual weight on the frame it fires"),
				Service->ResolvedCamera().Weight == 0.0f);
			TestFalse(TEXT("...and nothing is composed"), Service->ResolvedCamera().bActive);
			++GFrameCounter;
			Service->Advance(1.0f / 60.0f);
			TestTrue(TEXT("...with no blend-out tail on the next frame either"),
				Service->ResolvedCamera().Weight == 0.0f);
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// The headless dialogue witness. RC6 proved the `.dlg` line record has thirteen fields and none of
// them is a camera: every one of the 110 shipped `SetCamera` calls lives in a line's SCRIPT field,
// split on `;`/`&` and truncated at 255 characters by `CDialog::CallEventScript` (`0x100e4f30`),
// and run through `CallPyDialogFunc` — which is `ElysiumScriptNatives.cpp` here. `tourette.dlg`
// carries 96 of them (`DialogDefault`, `Tourette`, `tJeanette`, `tTherese`) against
// `npc_VVampire Tourette`'s `default_camera "vdata/CameraShots/Tourette.txt"` in `sm_asylum_1`, so
// it is the one conversation that exercises the adoption, the per-line re-shot and the end cut
// against real content.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumContentDialogueCameraChainTest,
	"Elysium.Content.DialogueCameraChain", GFlags)

bool FElysiumContentDialogueCameraChainTest::RunTest(const FString&)
{
	const FString DlgPath = FElysiumContentPaths::DlgFromDialogname(
		TEXT("dlg/Santa Monica/Tourette.dlg"));
	if (!IFileManager::Get().FileExists(*DlgPath)
		|| !IFileManager::Get().FileExists(
			*FElysiumContentPaths::VdataFile(TEXT("camerashots/tourette.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the .dlg / camerashots corpus is not mounted; the "
			"dialogue camera chain was not driven"));
		return true;
	}

	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	FElysiumDlgFile File;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("%s parses"), *DlgPath),
		FElysiumDlgFile::LoadFile(DlgPath, File, &Error)))
	{
		return false;
	}

	// Every per-line camera the file asks for, taken out of the script fields the same way
	// `CallEventScript` splits them.
	TSet<FString> Named;
	int32 Calls = 0;
	for (const FElysiumDlgLine& Line : File.Lines)
	{
		for (const FString& Field : { Line.Condition, Line.Action })
		{
			int32 At = 0;
			while ((At = Field.Find(TEXT("SetCamera(\""), ESearchCase::IgnoreCase,
				ESearchDir::FromStart, At)) != INDEX_NONE)
			{
				const int32 Open = At + 11;
				const int32 Close = Field.Find(TEXT("\""), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, Open);
				if (Close == INDEX_NONE)
				{
					break;
				}
				Named.Add(Field.Mid(Open, Close - Open));
				++Calls;
				At = Close;
			}
		}
	}
	TestTrue(TEXT("tourette.dlg carries per-line SetCamera calls in its script fields"), Calls > 0);
	AddInfo(FString::Printf(TEXT("tourette.dlg: %d SetCamera calls naming %d distinct shots"),
		Calls, Named.Num()));

	// The NPC's own `default_camera`, exactly as `sm_asylum_1` authors it.
	const FElysiumCameraShotDef* Source =
		ElysiumCameraShots::Load(TEXT("vdata/CameraShots/Tourette.txt"));
	if (!TestNotNull(TEXT("the NPC's default_camera shot loads from the corpus"), Source))
	{
		return false;
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__dialogue_camera_chain_witness__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Tourette");
		Npc.Origin = FVector(200.0f, 0.0f, 0.0f);
		Npc.Keys.Add(TEXT("default_camera"), TEXT("vdata/CameraShots/Tourette.txt"));
		Defs.Defs.Add(MoveTemp(Npc));
		World.Load(MoveTemp(Defs));
	}
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* SpeakerEnt = World.FindByName(TEXT("Tourette"));
	if (!TestNotNull(TEXT("the witness NPC spawned"), SpeakerEnt))
	{
		return false;
	}

	// The conversation's own first NPC line, driven through the port's dialogue session exactly as
	// `FElysiumNpc::OpenConversation` hands it over — un-started, so `OpenDialog` runs `Start()`
	// itself (retail runs the opening line's col-4 inside `CDialog::Acquire`).
	TSharedRef<FElysiumDlgFile> Shared = MakeShared<FElysiumDlgFile>(File);
	TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
		Shared, /*bMale*/ true, /*bMalk*/ false,
		[](const FString&) { return true; }, [](const FString&) {});
	World.OpenDialog(SpeakerEnt->Handle, Conversation, EElysiumDialogOpenerKind::Use, 0,
		TEXT("vdata/CameraShots/Tourette.txt"));

	if (!TestNotNull(TEXT("the shipped conversation opens headlessly"), World.GetOpenDialog()))
	{
		return false;
	}
	TestTrue(TEXT("the opener adopted the NPC's default_camera into the cine slot"),
		World.HasScriptedCamera());
	TestEqual(TEXT("...by the shot file's own block name"),
		World.ScriptedCameraName().ToLower(), FString(TEXT("tourette")));
	const FElysiumEntityHandle Adoptee = World.CineCameraEntity();
	TestTrue(TEXT("...as a real camera_cinematic entity"), Adoptee.IsSet());

	// A per-line `pc.SetCamera("...")` through the native, for each distinct shot the file names.
	// Retail's re-shot branch keeps the SAME camera and never re-places it.
	for (const FString& ShotName : Named)
	{
		TArray<FElysiumVariant> Args;
		Args.Add(FElysiumVariant::String(ShotName));
		ElysiumScriptNatives::CallCharacterMethod(nullptr, &World, World.PlayerHandle(),
			FName(TEXT("SetCamera")), Args);
		TestTrue(FString::Printf(
			TEXT("SetCamera(\"%s\") re-shots the adopted camera rather than stacking"), *ShotName),
			World.CineCameraEntity() == Adoptee);
		TestTrue(TEXT("...and the slot stays live"), World.HasScriptedCamera());
	}

	World.CloseDialog(/*bSilent*/ false);
	TestFalse(TEXT("the dialogue end cuts the slot on the same frame"), World.HasScriptedCamera());
	TestNull(TEXT("...and removes the camera entity unconditionally"), World.Resolve(Adoptee));

	return true;
}

}   // namespace ElysiumDialogueCameraRetailChainTests

#endif   // WITH_DEV_AUTOMATION_TESTS
