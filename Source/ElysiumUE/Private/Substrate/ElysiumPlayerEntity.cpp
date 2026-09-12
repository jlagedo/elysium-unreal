// CBasePlayer / CHL2_Player: the player leaf, its recovered datamap inputs, the XP ledger and the
// session record's hydrate/dehydrate pair (S3).
//
// The public declaration stays the chain header `Public/ElysiumPlayer.h`; class registration stays
// at `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumAnimEvent.h"                // the 2050-2053 swallow reads the record's id
#include "ElysiumEntityDefs.h"
#include "ElysiumStub.h"           // the dialogue holster's unrecovered halves
#include "ElysiumEntityWorld.h"
#include "ElysiumLocomotionSample.h"         // the step clock's whole input
#include "ElysiumMoveSolve.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumRng.h"                      // EElysiumRngStream::Footsteps
#include "ElysiumSheetSlots.h"
#include "ElysiumSkeletalBasis.h"            // 4051 flattens the body's own Source angles
#include "ElysiumSurfaceSounds.h"            // the step pools the emit draws from
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumCameraCinematic.h" // anim event 4050 -> FUN_10070550 -> FindBestShot
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumFootsteps.h"      // the step clock, the landing and the hearing rules
#include "Substrate/ElysiumItemClasses.h"   // the dialogue holster switches the active weapon
#include "Substrate/ElysiumWeaponClasses.h"
#include "Substrate/ElysiumGameSound.h"      // the six PLAYER_* category names
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"      // Obf_Bumped_Object, the touch handler's first write
#include "Substrate/ElysiumNpcConditions.h"  // WeaponCapability — the block predicate's `0x18000` term
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumStealthKillRules.h"

#include "Components/SkeletalMeshComponent.h"
#include "Misc/Paths.h"

bool FElysiumPlayer::WasRecentlyObservedByHostile(double Now) const
{
	return World && World->Resolve(LastHostileAssessment) && Now < LastHostileAssessmentTime + 1.0;
}

bool FElysiumPlayer::IsInStealthPosture() const
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return IsObfuscatedForSenses()
		|| (IsGrappling() && Grapple.Type == EElysiumGrappleType::StealthKill)
		|| (Embodiment && Embodiment->IsPlayerDucking() && !WasRecentlyObservedByHostile(World->NowSeconds()));
}

bool FElysiumPlayer::CanAttemptStealthKill() const
{
	// `0x101681a0`. Other handles (`+0xfe8`, `+0x1040`, `+0x1eb8`, `+0x19c0`, `+0x19cc`,
	// menu `0x1023bd00`) and the `0x10175180` skip have no producer here and answer not-busy.
	if (!IsAlive() || IsInert())
	{
		return false;
	}
	if (World && World->CineCameraEntity().IsSet())
	{
		return false;
	}
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	const bool bPosture = IsObfuscatedForSenses()
		|| (IsGrappling() && Grapple.Type == EElysiumGrappleType::StealthKill)
		|| (Embodiment && Embodiment->IsPlayerDucking());
	// `0x101672d0` (duck interpolator vs `gpGlobals->curtime`) is a named seam: ducking is enough.
	if (!bPosture)
	{
		return false;
	}
	const FElysiumItem* Item = Inventory.Active(*this);
	const FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
	return Weapon && Weapon->CanStealthKill();
}

FElysiumNpc* FElysiumPlayer::FindStealthKillVictim()
{
	if (!World)
	{
		return nullptr;
	}
	UElysiumSessionSubsystem* GameState = World->GetGameState();
	UElysiumRulebookSubsystem* Book = GameState ? GameState->Rulebook() : nullptr;
	if (!Book)
	{
		return nullptr;
	}
	return Book->StealthKillRules().FindVictim(*this);
}

void FElysiumPlayer::Spawn()
{
	LastHostileAssessment = FElysiumEntityHandle::Invalid();
	LastHostileAssessmentTime = -1.0;
	// The body is the pawn, already standing: nothing to build, and the first SyncFromBody puts the
	// entity where the pawn is.
	//
	// The health ceiling is read, not derived: `Max_Health` is an ordinary stat with `Default 100`
	// and no formula anywhere in `vdata` — nothing derives health from Stamina (RE24). A run that
	// has been through New Game arrives with the record's seeded sheet; one that has not (a map
	// loaded straight from the console) seeds here so the damage path has a track.
	if (Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth) <= 0)
	{
		if (const FElysiumStatTable* Table = SheetRules())
		{
			Sheet.SeedFrom(*Table);
		}
	}
	// The clan is a sheet slot, so the effect layer it names can only be resolved once the sheet is
	// in place — which is here, whether it arrived from the record or was just seeded.
	RefreshClanEffects();
	SyncHealthFromSheet();
	SyncFromBody();

	if (!Model.IsEmpty())
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			if (PrepareCharacterVisual()) InstallPreparedCharacterVisual();
		}
	}

	// **The one thing that ends a death.** `CHL2_Player::Spawn` (slot 103, `0x1016d260`) is the only
	// exit retail's death sequence has — nothing in the game advances past `LIFE_RESPAWNABLE` — and
	// what it resets is `m_lifeState`, `m_fEffects`, `m_afPhysicsFlags` and `m_iFOV` (it writes the
	// same 0 `Event_Killed` does, which is why VtMB's ordinary play lens is the latched 60). Here it
	// is a load or a level change, since this runtime's saves are the only way back.
	LifeState = EElysiumLifeState::Alive;
	DeathFrames = 0.f;
	DeathAnimEndTime = -1.0;
	// The once-only latch on the death commit goes with it, for the same reason: a run that comes
	// back through a load has not reported this player's death yet.
	bDeathReported = false;
	if (IElysiumEmbodiment* Bodily = World ? World->Embodiment() : nullptr)
	{
		// The port's living lens stays `default_fov` rather than retail's latched 60: that is the
		// standing lens decision recorded in `docs/vtmb/camera-view-modes.md` -> "The lens", so the
		// override is CLEARED here (the seam's negative) rather than written to retail's 0.
		Bodily->SetPlayerFovOverride(-1);
	}

	// Arm the think for the first stealth recompute. The surface's deadline is negative
	// ("due now") on a fresh entity, and `Think` re-arms itself from it after every pass; without
	// this first arm the deadline-driven think would never start.
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

FVector FElysiumPlayer::TickGaze(float, float, const FVector& HeadPos,
	const FVector& HeadForward, const FElysiumEyeTargetTuning&, const FVector*)
{
	// `CHL2_Player`'s maintainer (`0x10350270`): `CalcLookData`, then the commanded and smoothed
	// targets both set to 300 units straight ahead of the head, then `SetViewtarget`. There is no
	// cascade on the player and no integration — a snap, every think. The tuning and the camera
	// point are accepted for the signature and ignored, because the player has no disposition
	// row driving its eyes and DialogPOV redirects NPCs at the player, never the player itself.
	EyeLookTargetHandle = FElysiumEntityHandle::Invalid();
	EyeLookTarget = HeadPos + HeadForward * (300.f * ElysiumMove::U);
	CurEyeTarget = EyeLookTarget;
	bCurEyeTargetSeeded = true;
	return CurEyeTarget;
}

void FElysiumPlayer::OnPendingEyeAnglesRaised()
{
	// `FUN_10178550` writes `m_angEyeAngles` and the movement code reads it back; the port's view
	// is the pawn's, and the seam that turns it is `IElysiumEmbodiment::SnapPlayerViewTo` — the
	// same call the terminal's near arm makes, because retail's terminal reaches the same
	// `FUN_10178590`. The pending flag stays raised: `ProcessUsercmds` is what clears it, and until
	// it does, this frame's usercmd angles are refused (`AElysiumPlayerController`'s look gate).
	if (IElysiumEmbodiment* Bodily = World ? World->Embodiment() : nullptr)
	{
		Bodily->SnapPlayerViewTo(PendingEyeLookPoint);
	}
}

void FElysiumPlayer::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// The player's only autonomous work today is the feed transaction. It runs here rather than off
	// a timer because the pulse deadline is simulation state (R4/S8): the same think that advances
	// it is the one the save's clock restores, so a load cannot duplicate or skip a pulse.
	TickFeed(Now);

	// The block input classification. Ahead of everything below it because it is an INPUT pass:
	// retail runs it out of `CPlayerMove::RunCommand` while draining the command whose button bits
	// it reads, which is the same position this think occupies.
	TickBlockIntent(Now);

	// `ShouldRemove_OnHearCombat`. The sound-event bus delivers nothing (§2.5.3), so its consumers
	// poll it during their own think; this is the discipline domain's poll.
	ElysiumDisciplines::PollHeardCombat(*this, Now);

	// The stealth target surface (`docs/vtmb/stealth.md` -> "Player target-surface update"). It
	// hangs off THIS think and nowhere else: retail's own recompute is a player-think virtual gated
	// on `m_flNextStealthUpdate`, and this think is reached only through
	// `FElysiumEntityWorld::RunPlayerThink` — never `Tick` — so the 0.1 s cadence is measured on
	// the substrate clock the save restores. One body point is sampled per due pass.
	//
	// The observer snapshot is committed straight after, in the same pass, so the HUD's view is
	// always downstream of the gameplay state it describes (§5.9).
	ElysiumStealth::TickPlayerSurface(*this, Now);

	// The law expiry pass (`docs/vtmb/player-entity.md` § "Law, Masquerade and world response"). It
	// rides the same 0.1 s heartbeat as the stealth recompute, for the same reason: retail's
	// `PlayerRuleUpdate` is the first call of `CHL2_Player::PreThink`, this think is reached only
	// through `FElysiumEntityWorld::RunPlayerThink`, and every deadline it reads is therefore
	// measured on the substrate clock the save restores rather than on a timer.
	//
	// Its four steps are retail's own order: supernatural expiry, the delayed response-cop
	// deadline, heightened-alert expiry — and the criminal expiry, which retail reaches from
	// `SetAnimation`'s law helper instead. `ElysiumLaw.cpp` states that divergence beside the code.
	ElysiumLaw::TickPlayerLaw(*this, Now);

	// The footstep hearing stimulus (`docs/vtmb/footsteps.md` §2.5). **It is not a step producer**,
	// which is what the seam that used to stand here was waiting for and never needed: retail's
	// `CBasePlayer::UpdatePlayerSound` (`0x1016b480`) runs off `PostThink`, once per think, and
	// rewrites the player's one permanently reserved `CSound` record. No footstep ever inserts a
	// sound, and the categories are chosen from locomotion state rather than from a footfall.
	UpdatePlayerSound(Now);

	// The re-arm. `RunPlayerThink` clears `NextThink` before entering here, so a think
	// that schedules nothing never runs again. Retail's player think runs every frame and gates the
	// recompute internally; ours is deadline-driven, so the surface's own 0.1 s deadline IS the
	// heartbeat. `Min` keeps whatever the feed transaction scheduled ahead of it.
	NextThink = FMath::Min(NextThink, static_cast<float>(Stealth.NextUpdateTime));
}

void FElysiumPlayer::TickBlockIntent(double NowSeconds)
{
	// Retail's three-term predicate, evaluated cheapest first. The held bit is the world's retained
	// level; the capability is the same `0x18000` join every AI call site asks; ground contact is the
	// mover's own published fact and is the only term that costs an engine call — so it is asked
	// last, and a player who is not even holding the button never reaches it.
	bool bWant = World != nullptr && World->IsPlayerBlockHeld();
	if (bWant)
	{
		bWant = ElysiumNpcCond::WeaponCapability(*this) == ElysiumNpcCond::ECapability::Melee;
	}
	if (bWant)
	{
		const IElysiumEmbodiment* Embodiment = World->Embodiment();
		bWant = Embodiment != nullptr && Embodiment->IsPlayerOnGround();
	}

	if (bWant == bBlockIntentStands)
	{
		// **The predicate has not moved, but the POSE may have.** An equal or higher band takes the
		// base channel on `>=`, so the first blocked hit's own `ACT_BLOCK` displaces the block pose
		// while the button is still down — and retail has no such gap, because it re-derives the ideal
		// activity every frame and its pose and classification fall and resume together
		// (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions").
		//
		// This poll IS that re-derivation, on the think this producer already runs on: it refreshes the
		// character's answer against the seam and re-takes the pose the moment the channel comes free,
		// replaying the cell the rising edge already resolved so nothing is drawn.
		if (bWant)
		{
			TickHeldReaction();
		}
		return;
	}
	bBlockIntentStands = bWant;

	if (!bWant)
	{
		// **The falling edge releases the claim, and that is the whole release condition.** The pose is
		// held by this predicate rather than by a duration, so the button coming up is what ends it —
		// there is no clock to wait out and no second producer to agree with.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s stops blocking at %.3f"), *DebugString(),
			NowSeconds);
		ReleaseHeldReaction();
		return;
	}

	// The rising edge requests the pose ONCE, which is what compact action 13's ordinary animation
	// route does. The weapon ladder is allowed: the corpus authors `ACT_PREBLOCK_KATANA` and its
	// siblings on 155 stems against a bare `ACT_PREBLOCK` on almost none.
	//
	// **The pose stands for the whole hold, exactly as the classification does.** Retail's ideal
	// activity holds `ACT_PREBLOCK` while the predicate is true and the clip's own loop bit keeps it
	// on screen; the request below is that hold, stated as the claim's release condition
	// (`EElysiumReactionRelease::Predicate`) rather than as a duration. The claim carries no
	// `HoldSeconds` at all, the pose repeats while it stands, and the falling edge above gives it
	// back.
	//
	// **Requested ONCE, on the edge, and that is load-bearing.** Every request draws the weighted
	// variant off the Reaction stream, so re-requesting on a cadence would make that stream's
	// position a function of how long a button was held — which is why the hold is a claim rather
	// than a repeat.
	FElysiumReactionPlayRequest Preblock;
	Preblock.Activity = TEXT("ACT_PREBLOCK");
	Preblock.bAllowFallbackLadder = true;
	Preblock.Release = EElysiumReactionRelease::Predicate;
	UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s starts blocking at %.3f"), *DebugString(), NowSeconds);
	PlayReactionActivity(Preblock);
}

// --- Footsteps (B2): the step clock, the landing and the hearing stimulus ------------------------
//
// `docs/vtmb/footsteps.md` §2. The RULES are `Substrate/ElysiumFootsteps.h`; what is here is the
// sequencing retail spreads across `PlayerMove` / `UpdateStepSound` / `PlayStepSound` /
// `CheckFalling` / `PostThink`, plus the state those functions keep on `CBasePlayer`.

namespace
{
	// `FL_DUCKING` — the hull is the SMALL one. Source carries `m_bDucked` and `m_bDucking`
	// independently and the flag follows the first, so two of the sample's four stances raise it.
	bool IsPlayerDucking(const FElysiumLocomotionSample& Sample)
	{
		return Sample.Stance == EElysiumStance::Ducked || Sample.Stance == EElysiumStance::Rising;
	}

	// `CGameMovement::PlayStepSound` (`0x1011e430`, vfunc 16), the whole of it.
	//
	// `Ground` is the movement's cached `m_pSurfaceData` (`CGameMovement+0x9c`), already resolved by
	// the caller because the dry arm is the one that needs the `gamematerial` letter too; the other
	// three arms name a surfaceprop and are resolved here, exactly as retail resolves
	// `GetSurfaceIndex("ladder" | "water" | "wade")` inside the arm that chose it.
	void PlayPlayerStepSound(FElysiumPlayer& Player, const IElysiumEmbodiment& Embodiment,
		IElysiumAudio& Audio, ElysiumFootsteps::EStepPool Pool,
		const FElysiumSurfaceSounds* Ground, float Volume)
	{
		FElysiumSurfaceSounds Resolved;
		const FElysiumSurfaceSounds* Row = nullptr;
		if (Pool == ElysiumFootsteps::EStepPool::Dry)
		{
			Row = Ground;
		}
		else
		{
			const FName Surface = Pool == ElysiumFootsteps::EStepPool::Ladder
				? ElysiumFootsteps::LadderSurface()
				: (Pool == ElysiumFootsteps::EStepPool::Water
					? ElysiumFootsteps::WaterSurface() : ElysiumFootsteps::WadeSurface());
			if (Embodiment.ResolveSurfaceSounds(Surface, Resolved))
			{
				Row = &Resolved;
			}
		}
		if (Row == nullptr)
		{
			// `1011e448`: `if (!psurface) return;` — retail's null `surfacedata_t`, which is a silent
			// step and not a failure. On the dry arm it is a body standing on nothing the table
			// knows; on the other three it is a checkout whose surfaceprop bake has not run.
			return;
		}

		// `1011e4a4`: `m_nStepside` clear draws `stepright` (`+0x2e`), set draws `stepleft`
		// (`+0x2c`) — and the toggle happens BEFORE the "no sound on this side" test below, so a
		// surface that authors only one foot still alternates rather than repeating.
		const TArray<FString>& Steps = Player.bStepSide ? Row->StepLeft : Row->StepRight;
		Player.bStepSide = !Player.bStepSide;
		if (Steps.IsEmpty())
		{
			return;   // `1011e4c7`: `if (sample == 0) return;`
		}

		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::Footsteps);
		// The variation retail leaves to the sound engine: `surfaceproperties.txt` repeats the key
		// inside one entry and the engine picks between the repeats, and the bake flattened those
		// repeats into this pool. Duplicates are alternates and are legal picks.
		const FString& Rel = Steps[Stream.RandRange(0, Steps.Num() - 1)];

		FElysiumBodySound Sound;
		Sound.Rel = Rel;
		Sound.Volume = Volume;
		Sound.SoundLevelDb = ElysiumFootsteps::kPlayerSoundLevelDb;   // `1011e50b`: a literal 75
		// `1011e502`: `95 + RandomInt(0, 10)`, inclusive, as a multiplier here — 0.95 .. 1.05.
		Sound.Pitch = (ElysiumFootsteps::kPitchBase
			+ Stream.RandRange(0, ElysiumFootsteps::kPitchJitter)) / 100.f;
		Sound.Channel = EElysiumSoundChannel::Body;                   // `1011e50f`: CHAN_BODY
		Audio.PlayBodySound(Player.Handle, Sound);
	}
}

void FElysiumPlayer::TickStepClock(double NowSeconds, float DeltaSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	IElysiumAudio* Audio = World ? World->Audio() : nullptr;
	FElysiumLocomotionSample Sample;
	if (Embodiment == nullptr || Audio == nullptr || !Embodiment->SamplePlayerLocomotion(Sample))
	{
		// No published record: no mover ran, so no move happened and retail's clock — which is only
		// ever touched from inside `PlayerMove` — did not advance either. Deliberately NOT the same
		// as a zeroed sample, which would read as a body standing on the ground with no surface.
		return;
	}

	const FElysiumFootstepTuning& Tuning = World->FootstepTuning();

	ElysiumFootsteps::FStepClockIn In;
	// Source units per second. Everything in `UpdateStepSound` is authored in them.
	In.Speed2D = Sample.Speed2D() / ElysiumMove::U;
	In.Speed3D = static_cast<float>(Sample.LocalVelocity.Size()) / ElysiumMove::U;
	In.bDucked = IsPlayerDucking(Sample);
	In.bOnLadder = Sample.bOnLadder;
	In.bOnGround = Sample.bOnGround;
	In.Water = Sample.Water;
	In.PcVol = Tuning.PlayerVolume;
	In.bServerFootsteps = Tuning.bServerFootsteps;

	// The movement's cached `m_pSurfaceData`, resolved once: the dry arm needs both the
	// `gamematerial` letter it takes its volume from and the step pools it draws the wav from, and
	// asking the table twice for the same surface is how the two could disagree.
	FElysiumSurfaceSounds Ground;
	const bool bHaveGround = !Sample.GroundSurface.IsNone()
		&& Embodiment->ResolveSurfaceSounds(Sample.GroundSurface, Ground);
	In.GameMaterial = bHaveGround ? Ground.GameMaterial : FString();

	// --- The ordinary pass: `PlayerMove` -> `UpdateStepSound` -------------------------------
	//
	// `PlayerMove` (`0x101274a0`) runs `UpdateStepSound` at the TOP of the move — after the
	// `m_flFallVelocity` refresh and before `CategorizePosition`, `Duck` and the movetype switch —
	// so retail's ordinary pass reads the PREVIOUS move's ground contact, water level and velocity.
	// This port reads the post-move sample, which is that same record one frame later, except on the
	// one frame the body lands: retail's pass still sees no ground entity there and takes early
	// return #5, and the only steps that frame are `CheckFalling`'s own (below). The ordinary pass is
	// therefore handed the pre-landing premise on a landing frame — that is what keeps a landing at
	// retail's two steps rather than three, and the foot parity retail's.
	const bool bLanding = Sample.FallSpeedAtLanding > 0.f;
	ElysiumFootsteps::FStepClockIn Ordinary = In;
	Ordinary.bOnGround = In.bOnGround && !bLanding;
	ElysiumFootsteps::FStepClockOut Out;
	if (ElysiumFootsteps::AdvanceStepClock(StepSoundMs, DeltaSeconds * 1000.f, WadeStepPhase,
		Ordinary, Out))
	{
		PlayPlayerStepSound(*this, *Embodiment, *Audio, Out.Pool,
			bHaveGround ? &Ground : nullptr, Out.Volume);
	}

	// --- The landing: `CheckFalling` (`0x10125db0`), the last line of `FullWalkMove` ------------
	//
	// The sample raises `FallSpeedAtLanding` on exactly the frame the ground came back, carrying the
	// speed the body fell at — retail's `m_flFallVelocity` read the moment `CheckFalling` reads it.
	if (!bLanding)
	{
		return;
	}
	float FallSpeedUnits = Sample.FallSpeedAtLanding / ElysiumMove::U;
	// SEAM: `GetGroundEntity()->IsFloating()` (`0x1000b186`) subtracts 173 u/s before the volume is
	// chosen, and it is the ONLY way retail's 0.5 and 0.65 arms are reachable. This runtime has no
	// floating-brush concept on the ground entity — the sample publishes a surfaceprop, not the
	// entity under the foot — so the reduction is never applied and those two arms stay unreachable
	// exactly as they are on ordinary ground. The rule is written and asserted in
	// `Elysium.Substrate.Footsteps.PlayerLanding` so the day a floating platform exists there is one
	// argument to change.
	const float LandingVolume = ElysiumFootsteps::LandingStepVolume(FallSpeedUnits,
		Sample.Water != EElysiumWaterLevel::None, /*bGroundIsFloating*/ false);

	// The latch `UpdatePlayerSound` reads for its two `PLAYER_LAND_*` rows. Retail reads the player
	// animation state (`+0x1db4` == 8, or 10/11) that `CheckFalling` wrote a few instructions
	// earlier; the enum is unrecovered, so the port latches the band `CheckFalling` classified,
	// which is the split those two authored rows are named after.
	PendingLandCategory = FallSpeedUnits < ElysiumFootsteps::kMaxSafeFallSpeed
		? ElysiumGameSounds::PlayerLandSoft() : ElysiumGameSounds::PlayerLandHard();

	if (LandingVolume <= 0.f)
	{
		return;
	}
	// `1012617a`, in retail's order and with retail's consequence: the clock is zeroed, the ordinary
	// clock is run AGAIN — and with the clock at exactly 0 it fires, because its only remaining gate
	// is "moving at all" — and then the forced step plays on the other foot. **VtMB double-steps on
	// a fast landing**; that is reproduced rather than suppressed, and named here so a reviewer does
	// not read it as a bug.
	StepSoundMs = 0.f;
	ElysiumFootsteps::FStepClockOut Landing;
	if (ElysiumFootsteps::AdvanceStepClock(StepSoundMs, 0.f, WadeStepPhase, In, Landing))
	{
		PlayPlayerStepSound(*this, *Embodiment, *Audio, Landing.Pool,
			bHaveGround ? &Ground : nullptr, Landing.Volume);
	}
	// The forced step takes the movement's cached surface whatever the clock chose, and its volume
	// is `CheckFalling`'s ladder UNSCALED: no duck factor, no `footstep_pc_vol`, no clamp — those
	// belong to `UpdateStepSound`'s tail and this value never goes through it.
	PlayPlayerStepSound(*this, *Embodiment, *Audio, ElysiumFootsteps::EStepPool::Dry,
		bHaveGround ? &Ground : nullptr, LandingVolume);
}

void FElysiumPlayer::UpdatePlayerSound(double NowSeconds)
{
	if (World == nullptr)
	{
		return;
	}
	const float Dt = PlayerSoundLastTime >= 0.0
		? static_cast<float>(FMath::Clamp(NowSeconds - PlayerSoundLastTime, 0.0, 1.0)) : 0.f;
	PlayerSoundLastTime = NowSeconds;

	IElysiumEmbodiment* Embodiment = World->Embodiment();
	FElysiumLocomotionSample Sample;
	// `1016b4b8`: `FL_NOTARGET` (0x8000) writes volume 0 and returns before anything is read;
	// `1016b610`: `m_fNoPlayerSound` (`+0x22a0`) zeroes the volume after the decay. Both are the
	// `bNoPlayerSound` seam, and a world with no body takes the same arm: a retired slot rather than
	// a zero-radius stimulus nobody could act on.
	if (bNoPlayerSound || Embodiment == nullptr || !Embodiment->SamplePlayerLocomotion(Sample))
	{
		World->RefreshGameSound(PlayerSoundSlot, FVector::ZeroVector, NAME_None, 0.f, Handle);
		PlayerSoundRadiusUnits = 0.f;
		PlayerSoundCategory = NAME_None;
		return;
	}

	ElysiumFootsteps::FHearingIn In;
	// `m_nButtons & IN_JUMP`. The port's stand-in is the jump's own push window: VtMB's jump is a
	// HELD push, so the window is open exactly while the button is doing work. It closes earlier
	// than retail's button bit, which the 250 u/s decay tail covers.
	In.bJumpHeld = Sample.JumpHoldRemaining > 0.f;
	In.bOnGround = Sample.bOnGround;
	In.bLandSoft = PendingLandCategory == ElysiumGameSounds::PlayerLandSoft();
	In.bLandHard = PendingLandCategory == ElysiumGameSounds::PlayerLandHard();
	In.Speed3D = static_cast<float>(Sample.LocalVelocity.Size()) / ElysiumMove::U;
	// **Ducking is the whole of "sneaking"** on this path — not the Stealth feat, not the light
	// level, not the player's stealth surface.
	In.bDucked = IsPlayerDucking(Sample);
	if (UElysiumSessionSubsystem* GameState = World->GetGameState())
	{
		if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
		{
			// `MiscData PLAYER_RUN_SPEED` (table `+0xfc`), authored 128. The compiled-in default is
			// the same number, so a run with no `vdata` splits walk from run identically.
			In.RunSpeedUnits = Rules->SoundVolumes().MiscFloat(TEXT("PLAYER_RUN_SPEED"),
				ElysiumFootsteps::kPlayerRunSpeedUnits);
		}
	}

	const FName Category = ElysiumFootsteps::HearingCategory(In);
	// The latch is consumed by the pass that read it — retail's animation state is overwritten by
	// the next `SetAnimation`, and the port has no such publisher, so one think is the lifetime.
	PendingLandCategory = NAME_None;

	// The authored reach the slot decays TOWARDS. A silent think targets zero, which is retail's
	// `vol = 0` and the reason a sprint keeps ringing for about a second after it stops.
	const float TargetRadiusUnits = Category.IsNone() ? 0.f : World->GameSoundRadiusUnits(Category);
	PlayerSoundRadiusUnits =
		ElysiumFootsteps::DecayHearingRadiusUnits(PlayerSoundRadiusUnits, TargetRadiusUnits, Dt);
	if (!Category.IsNone())
	{
		PlayerSoundCategory = Category;
	}

	if (PlayerSoundRadiusUnits <= 0.f || PlayerSoundCategory.IsNone())
	{
		World->RefreshGameSound(PlayerSoundSlot, FVector::ZeroVector, NAME_None, 0.f, Handle);
		PlayerSoundCategory = NAME_None;
		return;
	}
	// The stealth subtraction is LISTENER-side in retail and producer-side here, and the number is
	// the same: `CBaseEntity::AdjustSoundDistForStealth` (`0x1009d850`) is called from the two NPC
	// hearing predicates (`CAI_BaseNPCTroika 0x102b35b0`, `0x1030f7b0`) on the sound's OWNER
	// entity, subtracts that owner's stealth hearing distance (vfunc `+0x78`) from the audible
	// reach and floors it at 0, and only for `m_iType == 4` (`SOUND_PLAYER`) — this slot is the
	// only type-4 producer in the game. `UpdatePlayerSound` itself writes the raw table volume; its
	// one call to the function, through `0x101b9a50`, is the rate-limited stealth debug print.
	// Retail subtracts AFTER the listener's own hearing scale, and this runtime's listeners carry
	// no such scale, so the two orders are identical here.
	World->RefreshGameSound(PlayerSoundSlot, Origin, PlayerSoundCategory,
		PlayerSoundRadiusUnits * ElysiumMove::U, Handle,
		ElysiumStealth::HearingReductionCmFor(this), ElysiumGameSounds::Player,
		/*reserved CSound freshness*/ 0.2);
}

bool FElysiumPlayer::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CBasePlayer::HandleAnimEvent` `0x10178a10`, and its gate:
	//
	//     if (!this->vfunc0x658() && event->owner == this) { ...the whole switch... }
	//     return;                                     // the base handler is INSIDE the gate
	//
	// `vfunc0x658` is `IsObserver()` (`FUN_1015ee60`, `m_bIsObserver` `+0x19f6`) — see the field's
	// comment on `FElysiumPlayer`; it has no reachable writer in the shipped image, so the gate is a
	// constant pass, and it is spelled out rather than dropped. `event->owner == this` is
	// structural here: the port's dispatcher (`FElysiumAnimating::AdvanceAnimEvents`) hands a record
	// to the entity whose clip carries it, so an event that reaches this function is always this
	// player's.
	//
	// A gated-out event reaches **no** handler at all, not even the combat-character base — so the
	// gate wraps the fall-through too.
	if (IsObserver())
	{
		return true;
	}

	// 2050-2053, **inside** the gate where the listing puts them: the switch that swallows them is
	// the same switch 4050/4051 sit in, so an observer reaches none of the four. Claimed, and
	// nothing played — the player's clips carry the same footfall records the cast's do, and the
	// player's steps come off `UpdateStepSound`'s millisecond clock instead, so letting one through
	// would double every step the moment a handler above claimed it.
	if (ElysiumFootsteps::PlayerSwallows(Event.Event))
	{
		return true;
	}

	switch (Event.Event)
	{
	case 4050:
	{
		// `0xfd2`. The `options` string is the shot **BASE NAME** — `FindBestShot` appends `_1`,
		// `_2`, … to it — not an entity classname, which is what
		// `docs/vtmb/animation_events.md`'s options table used to record.
		//
		//     name = event->options;
		//     if (name && *name && (cam = FUN_10070550(name))) {
		//        cam->m_bDrawPlayer (+0x640) = 1;
		//        FUN_1017cef0(this, cam);                 // adopt
		//        cam->m_bForcePlayerLook (+0x5e8) = 0;    // the ONLY clearer among the three
		//        return;
		//     }
		//
		// **It does not immobilize.** `FUN_1015ef40` is not on this path; only `StartShot` and
		// `StartPlayerDialog` freeze the player.
		//
		// A failed create falls out of the `if` and returns without reaching the base handler,
		// which is retail's own shape — the event is claimed either way.
		if (Event.Options.IsEmpty() || !World)
		{
			return true;
		}
		const FElysiumEntityHandle Created = FElysiumCameraCinematic::CreateFindBestShotCamera(
			*World, Event.Options, ElysiumRng::Stream(EElysiumRngStream::CameraFindBestShot));
		FElysiumEntity* CreatedEntity = World->Resolve(Created);
		FElysiumCameraCinematic* Camera =
			CreatedEntity ? CreatedEntity->AsCameraCinematic() : nullptr;
		if (!Camera)
		{
			return true;
		}
		// `m_bDrawPlayer = 1` — with `spawnflags & 2` on a map director (RC2), one of the two
		// non-zero writers in the game, and SC5's `bDrawPlayerBody` consumer is what reads it. It
		// lands **before** the adoption, so the goal already carries it; the re-stamp is what gets
		// it onto the published shot the shot start pushed.
		Camera->bDrawPlayerBody = true;
		Camera->RefreshPublishedGoal();
		World->SetCineCamera(Camera->Handle, Camera->PublishedShotId, Camera->bDisposable,
			Camera->ShotDef.Name);
		// **The only clearer of `point_player` on this camera.** The runtime default is 1 (the
		// constructor's, RG-A) and the director's keyvalue never reaches a runtime camera, so a
		// stealth-kill shot is one of exactly three paths in the game that opt the subject's gaze
		// out — the func-monitor camera and the lockpick `Intrusion` opener are the other two.
		Camera->bForcePlayerLook = false;
		return true;
	}
	case 4051:
	{
		// `0xfd3`:
		//
		//     FUN_1017cef0(this, NULL);                  // drop AND destroy — the camera 4050
		//                                                // created carries the disposable bit
		//     AngleVectors(this->GetAngles(), fwd); fwd.z = 0; VectorNormalize(fwd);
		//     FUN_10178590(this, this->EyePosition() + fwd * _DAT_10447ee0);
		//
		// `_DAT_10447ee0` is **1000.0f** (read out of `vampire.dll` at that address). The point is
		// only ever used as a direction, so the magnitude cannot change the answer; it is carried in
		// cm so the call reads as retail's.
		//
		// The net effect is a snap of the player's eye angles to pitch 0 and his own body yaw —
		// level and straight ahead — which is what returns a sane view after a shot that has been
		// driving it. Like 4050, it does not immobilize or mobilize.
		if (!World)
		{
			return true;
		}
		World->ClearScriptedCamera();
		const FRotator Facing = ElysiumSkeletalBasis::FromSourceAngles(Angles);
		FVector Forward = Facing.Vector();
		Forward.Z = 0.0f;
		if (!Forward.Normalize())
		{
			// `VectorNormalize` on a zero vector leaves it zero and the look point degenerates to
			// the eye itself; `LookAtWorldPoint` then has no direction to turn to, and retail's
			// `VectorAngles((0,0,0))` answers `(0,0,0)` — a level, zero-yaw snap.
			SetPendingEyeAngles(FRotator::ZeroRotator, EyePosition());
			return true;
		}
		// 1000 Source units.
		constexpr float ForwardReachCm = 1000.0f * ElysiumCam::U;
		LookAtWorldPoint(EyePosition() + Forward * ForwardReachCm);
		return true;
	}
	default:
		break;
	}
	return FElysiumCombatCharacter::HandleAnimEvent(Event);
}

void FElysiumPlayer::RefreshClanEffects()
{
	UElysiumSessionSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		RebuildEffects();   // no rulebook: the layer still has to match the names, which is nothing
		return;
	}
	// `clandoc000.txt` names the player templates `Player_<Clan>`, and each one names the
	// `TraitEffectGroup` carrying that clan's gift and bane — which is where every bane lives:
	// nothing about a clan is special-cased in code (`docs/vtmb/game_runtime.md` section 3).
	FString Group;
	FElysiumClanTemplate Resolved;
	if (Rules->Clans().Resolve(FString::Printf(TEXT("Player_%s"), FElysiumSheet::ClanName(Sheet.Clan())), Resolved))
	{
		Group = Resolved.GeneralStr(TEXT("ClanEffect"));
	}

	// One clan group at a time: re-running this after a clan change must replace, not accumulate.
	Effects.RemoveAll([](const FString& Name) { return Name.StartsWith(TEXT("Clan (")); });
	if (!Group.IsEmpty())
	{
		Effects.Add(Group);
	}
	RebuildEffects();
}

bool FElysiumPlayer::HasAwarded(const FString& Key) const
{
	// `Q_strnicmp` over the STORED key's length — a prefix compare, which is latent breakage VtMB
	// gets away with because no shipped key prefixes another (`Elysium.Content.Rulebook` asserts
	// the premise still holds). Reproduced as authored, not "fixed".
	for (const FElysiumXpEntry& Entry : ExperienceLog)
	{
		if (!Entry.Entry.IsEmpty() && Key.StartsWith(Entry.Entry, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

int32 FElysiumPlayer::AwardExperience(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return 0;
	}
	// 1. Give-once is the LEDGER, not an encoding: every key is give-once, unconditionally — the
	//    trailing `01` every real row carries has nothing to do with it.
	if (HasAwarded(Key))
	{
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s AwardExperience(\"%s\") — already given"),
			*DebugString(), *Key);
		return 0;
	}

	UElysiumSessionSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	const FElysiumExperienceEntry* Row = Rules ? Rules->Experience().Find(Key) : nullptr;
	if (!Row)
	{
		// 2. A key the table does not hold awards nothing AND APPENDS nothing, so it retries on
		//    every fire. That is the engine's own behaviour, not a leniency.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — no such experience_table row"),
			*DebugString(), *Key);
		return 0;
	}

	// 3. The `Experience_Modifier` bonus, above 2 XP — the file's own "the value without extra
	//    experience points". The threshold is on the RAW value, which is in hundredths.
	const int32 Value = ElysiumXp::WithModifier(Row->Value,
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::ExpModifier));

	// 4. The key joins the ledger with the amount it was worth.
	FElysiumXpEntry Entry;
	Entry.Entry = Key;
	Entry.Amount = Value;
	ExperienceLog.Add(MoveTemp(Entry));

	// `AddExperience`: the raw value accumulates untouched, the /100 keeps its remainder, and only
	// whole points reach the sheet. Every real row being `N01`, each award banks 0.01 XP of residue
	// and one bonus point falls out per 100 awards.
	const int32 Whole = ElysiumXp::Bank(Value, ExperienceRemainder, LifetimeExperience);
	if (Whole > 0)
	{
		AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, Whole);
	}
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — %d raw, +%d XP (%.0f left over)"),
		*DebugString(), *Key, Value, Whole, ExperienceRemainder);
	return Whole;
}

// The level-5 masquerade-loss transaction's map half.
// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response": "An increment whose
// resulting value is greater than four loads `sp_masquerade_1`. The retail loss boundary is
// therefore a native level-5 transaction, not only a UI convention." The increment-past-4 rule is
// applied by `FElysiumCombatCharacter::ChangeMasqueradeLevel`, which is what calls this; the map
// load is here because only the PLAYER's counter ends a run.
//
// SEAM — three things about the transition's exact form are not recovered and are not invented:
//   * whether retail's load is a plain map change or a landmark transition. No landmark is named
//     anywhere in the recovered record and `sp_masquerade_1` is an ending map with no return leg,
//     so the plain `ChangeMap` door is taken;
//   * whether it fades, holds the player, or runs any pre-travel presentation first. Nothing is
//     played here — the request goes straight to the travel seam;
//   * what the destination map's own script then does. `sp_masquerade_1` is not in the exported
//     corpus (`vamputil.py` carries only its ordinary `sp_masquerade_1_patch` hook), so nothing is
//     known about the ending it plays beyond the fact that loading it IS the loss.
// This runtime additionally keeps its own session GameOver state, which retail has no equivalent
// of — the map is retail's ending. It is a declared divergence rather than a reproduction, and it
// stays until the ending map is exported and its script owns the transition.
const FString& FElysiumPlayer::MasqueradeLossMap()
{
	static const FString Name(TEXT("sp_masquerade_1"));
	return Name;
}

void FElysiumPlayer::OnMasqueradeBreached()
{
	FElysiumCombatCharacter::OnMasqueradeBreached();

	if (IElysiumTravel* Maps = World ? World->Travel() : nullptr)
	{
		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s broke the masquerade at %d — loading %s"),
			*DebugString(), GetMasqueradeLevel(), *MasqueradeLossMap());
		Maps->ChangeMap(MasqueradeLossMap());
	}
	else
	{
		// Not a failure: a headless substrate world and the menu backdrop both carry no travel
		// seam, and the session half below still reports the loss. Stated rather than silent,
		// because the recovered transaction did not happen.
		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s broke the masquerade at %d — no travel seam, so %s is not loaded"),
			*DebugString(), GetMasqueradeLevel(), *MasqueradeLossMap());
	}

	// The second loss condition. Same shape as death: the substrate reports, and the session owns
	// what it means to the application (the GameOver state, with its own reason).
	if (UElysiumSessionSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyMasqueradeBreach();
	}
}

void FElysiumPlayer::Hydrate(const FElysiumPlayerRecord& Record)
{
	Sheet      = Record.Sheet;
	Money      = Record.Money;
	Health     = Record.Health;
	MaxHealth  = Record.MaxHealth;
	Law        = Record.Law;
	// The law/police block crosses UNSCOPED, unlike the feed, discipline and stealth blocks below.
	// Every deadline in it is on the session clock (`UElysiumSessionSubsystem`'s, which outlives
	// the map), and a wanted level with four seconds left, a Masquerade window, a pursuit count and
	// a heightened alert all mean exactly what they meant in the previous map — none of them
	// describes THIS map's geometry, light or cast. The single exception is the pending response's
	// witness, which is a map entity: that one handle is rebased, and a response whose witness does
	// not survive the boundary is dropped rather than left pointing at whatever now holds its index.
	Police     = Record.Police;
	if (Police.bResponsePending)
	{
		Police.ResponseWitness = (World && Police.ResponseWitness.IsSet())
			? World->RebaseSavedHandle(Police.ResponseWitness)
			: FElysiumEntityHandle::Invalid();
		if (!Police.ResponseWitness.IsSet())
		{
			UE_LOG(LogElysiumPlayer, Log,
				TEXT("%s pending police response (severity %d) dropped: its witness did not survive "
					"the map boundary"), *DebugString(), Police.ResponseSeverity);
			Police.bResponsePending = false;
			Police.ResponseSeverity = 0;
		}
	}
	else
	{
		Police.ResponseWitness = FElysiumEntityHandle::Invalid();
	}
	ExperienceLog = Record.ExperienceLog;
	Effects    = Record.Effects;
	GlobalEmail = Record.GlobalEmail;
	ExperienceRemainder = Record.ExperienceRemainder;
	LifetimeExperience  = Record.LifetimeExperience;
	bUnkillable = Record.bUnkillable;
	bDeathReported = false;
	// A feed only resumes into the map it was taken in, and its handles have to be re-stamped
	// against this world's epoch (the record's copy carries a dead one, exactly as a saved handle in
	// the map snapshot does). Anything else drops the pair rather than pointing it at a stranger.
	FeedState = FElysiumFeedState();
	if (World && !Record.FeedMap.IsEmpty() && Record.FeedMap == World->MapName())
	{
		FeedState = Record.Feed;
		auto Rebase = [this](FElysiumEntityHandle& H)
		{
			H = (H.IsSet() && World->Resolve(FElysiumEntityHandle(H.Index, World->GetEpoch())))
				? FElysiumEntityHandle(H.Index, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		};
		Rebase(FeedState.Target);
		Rebase(FeedState.Peer);
		if (!FeedState.IsPaired())
		{
			FeedState = FElysiumFeedState();
		}
		else if (!FeedState.bVictim)
		{
			// The player record owns the feeder half, while the map snapshot owns its victim. Preserve
			// the absolute phase/pulse deadlines and re-arm the player think at their earlier boundary;
			// otherwise a correctly restored pair can remain inert behind ELYSIUM_NEVER_THINK.
			NextThink = FeedState.PhaseDeadline;
			if (FeedState.IsTransacting())
			{
				NextThink = FMath::Min(NextThink, FeedState.NextPulse);
			}
		}
	}
	// The discipline block. Its owned expiry events ride the map's own queue and its
	// tracked effects name entities in that map, so it resumes only into the map it was taken in.
	// Any other map takes the recovered world-transition teardown instead: the sheet arrived with
	// the `Active_*` slots set, and `ClearAll` is what zeroes them and removes the groups they
	// installed. Selection and the cast counter are map-independent and always cross.
	Disciplines = FElysiumDisciplineState();
	SelectedDiscipline = Record.SelectedDiscipline;
	SelectedTier = Record.SelectedTier;
	DisciplineCastCount = Record.DisciplineCastCount;
	MiscFlags = Record.MiscFlags;
	ComfortingCount = 0;
	const bool bSameDisciplineMap =
		World && !Record.DisciplineMap.IsEmpty() && Record.DisciplineMap == World->MapName();
	if (bSameDisciplineMap)
	{
		Disciplines = Record.Disciplines;
		ComfortingCount = Record.ComfortingCount;
		for (FElysiumActiveDisciplineEffect& Effect : Disciplines.TargetEffects)
		{
			// A saved handle carries a dead epoch, exactly as one in the map snapshot does.
			Effect.Source = (Effect.Source.IsSet()
				&& World->Resolve(FElysiumEntityHandle(Effect.Source.Index, World->GetEpoch())))
				? FElysiumEntityHandle(Effect.Source.Index, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		}
	}

	// The stealth block. It is ONE generation (`docs/vtmb/stealth.md`): the three light
	// samples, the rotation index and the derived scalars either all cross or none do, and a
	// restored triplet is never combined with newly defaulted derived values. It resumes only into
	// the map it was taken in, and for a stronger reason than the two blocks above — the samples
	// measure THAT map's light at THAT position, and the raw aggregate is the sum of the
	// `trigger_stealth_mod` volumes of that map the player was standing inside.
	Stealth.Reset();
	StealthModRaw = 0;
	Observer.Reset();
	PendingObserver.Reset();
	LastHostileAssessment = FElysiumEntityHandle::Invalid();
	LastHostileAssessmentTime = -1.0;
	if (World && !Record.StealthMap.IsEmpty() && Record.StealthMap == World->MapName())
	{
		Stealth = Record.Stealth;
		StealthModRaw = Record.StealthModRaw;
	}

	// The names crossed the boundary; their resolution did not — the rulebook is re-read at load,
	// which is what lets a patched rulebook re-apply to a run that started before it. Going through
	// RefreshClanEffects rather than RebuildEffects reconciles the clan group with the clan slot
	// that just arrived, so a New Game into a different clan cannot keep the old one's bane.
	RefreshClanEffects();

	// The teardown runs AFTER the effect layer is rebuilt, so removing a group leaves a layer that
	// still matches the names on the character.
	if (!bSameDisciplineMap)
	{
		ElysiumDisciplines::ClearAll(*this);
	}
}

void FElysiumPlayer::Dehydrate(FElysiumPlayerRecord& Record) const
{
	Record.Sheet      = Sheet;
	Record.Money      = Money;
	Record.Health     = Health;
	Record.MaxHealth  = MaxHealth;
	Record.Law        = Law;
	Record.Police     = Police;   // unscoped; see Hydrate for why
	Record.ExperienceLog = ExperienceLog;
	Record.Effects    = Effects;
	Record.GlobalEmail = GlobalEmail;
	Record.ExperienceRemainder = ExperienceRemainder;
	Record.LifetimeExperience  = LifetimeExperience;
	Record.bUnkillable = bUnkillable;
	Record.Feed = FeedState;
	Record.FeedMap = (World && FeedState.IsPaired()) ? World->MapName() : FString();
	// The discipline block travels with the record, scoped to the map its owned expiry
	// events and tracked effects belong to.
	Record.Disciplines = Disciplines;
	Record.DisciplineMap = World ? World->MapName() : FString();
	Record.MiscFlags = MiscFlags;
	Record.ComfortingCount = ComfortingCount;
	Record.SelectedDiscipline = SelectedDiscipline;
	Record.SelectedTier = SelectedTier;
	Record.DisciplineCastCount = DisciplineCastCount;
	// The stealth group, whole and map-scoped. The observer snapshot is deliberately not
	// carried: it is published presentation state that the observers' own sight passes rebuild
	// within one cadence, and a restored one would name an entity of the previous world.
	Record.Stealth = Stealth;
	Record.StealthModRaw = StealthModRaw;
	Record.StealthMap = World ? World->MapName() : FString();
}

namespace
{
	// `Q_strncmp(a, b, MAX(strlen(a), strlen(b))) == 0` (`0x1016eec0` / `0x1016f19b`). Comparing
	// over the LONGER of the two lengths means the shorter string's terminator is part of the
	// comparison, so this is an exact, case-sensitive name compare — not the prefix match the
	// `strncmp` shape suggests. `haven_pc2` does not match `haven_pc`.
	bool SameTerminalName(const FString& A, const FString& B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}
}

void FElysiumPlayer::RetrieveGlobalEmailFlags(const FString& TerminalName, TArray<int32>& InOutFlags)
{
	// `CBasePlayer::RetrieveGlobalEmailFlags` `0x1016eec0`.
	FElysiumGlobalEmailRecord* Found = GlobalEmail.FindByPredicate(
		[&TerminalName](const FElysiumGlobalEmailRecord& Record)
		{ return SameTerminalName(Record.Name, TerminalName); });
	if (!Found)
	{
		// Create-on-retrieve: a zeroed `0x240` local, `Q_strncpy(local, name, 0x40)`, `AddToTail`.
		// The new record's flags are all clear, so a first visit copies nothing over the local
		// array — which is exactly what a terminal that has never been read should see.
		FElysiumGlobalEmailRecord Fresh;
		Fresh.Name = TerminalName.Left(FElysiumGlobalEmailRecord::NameMax);
		Fresh.Flags.SetNumZeroed(FElysiumGlobalEmailRecord::FlagCount);
		Found = &GlobalEmail[GlobalEmail.Add(MoveTemp(Fresh))];
	}
	// `memcpy(out, record->flags, 0x80 * 4)` — GLOBAL WINS, no merge.
	Found->Flags.SetNumZeroed(FElysiumGlobalEmailRecord::FlagCount);
	InOutFlags = Found->Flags;
}

void FElysiumPlayer::StoreGlobalEmailFlags(const FString& TerminalName, const TArray<int32>& InFlags)
{
	// `CBasePlayer::StoreGlobalEmailFlags` `0x1016f0f0`.
	FElysiumGlobalEmailRecord* Found = GlobalEmail.FindByPredicate(
		[&TerminalName](const FElysiumGlobalEmailRecord& Record)
		{ return SameTerminalName(Record.Name, TerminalName); });
	if (!Found)
	{
		// **No create-on-store.** Retail warns and writes nothing, so a `global_email` terminal
		// saved before it was ever used silently loses its state. Retrieve always runs first at
		// OnUseBegin, which is why that hole is unreachable in practice.
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("could not save email for terminal: %s"), *TerminalName);
		return;
	}
	// `memcpy(record->flags, in, 0x80 * 4)` — LOCAL WINS, no merge.
	Found->Flags = InFlags;
	Found->Flags.SetNumZeroed(FElysiumGlobalEmailRecord::FlagCount);
}

void FElysiumPlayer::PollTouchContacts(double Now)
{
	// `0x10147690`, the player's `Touch`, in its recovered order per touched character:
	//   if (other->+0x9c) {                       // the touched thing is a combat character
	//     this->AddMiscFlag(0x100);              // Obf_Bumped_Object, sticky
	//     if (this->+0xa8 && other->+0x98)       // I am a player, it is a Troika NPC
	//       if (ConditionInterruptsCurrentSchedule(npc, WAS_BUMPED)) SetCondition(npc, WAS_BUMPED);
	//   }
	// Source raises it every frame a move sweep blocks on the other entity, in either direction;
	// the embodiment's drain answers exactly those frames. Only NPC bodies are drained: a prop or
	// a non-NPC character (`+0x9c` set, `+0x98` clear) would take the first arm and not the second,
	// and this runtime has no such toucher to report yet. Whether retail's `CategorizePosition`
	// also touches a non-world ground entity (a player standing still on an NPC's head) is
	// unrecovered: the corpus names neither it nor `AddToTouched`.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return;
	}
	TArray<FElysiumEntityHandle> Contacts;
	Embodiment->DrainPlayerTouchContacts(Contacts);
	for (const FElysiumEntityHandle& Contact : Contacts)
	{
		FElysiumEntity* Other = World->Resolve(Contact);
		FElysiumNpc* Npc = Other ? Other->AsNpc() : nullptr;
		if (Npc == nullptr || Npc->IsInert())
		{
			continue;
		}
		ElysiumMiscFlags::Set(MiscFlags, ElysiumMiscFlags::ObfBumpedObject);
		// The port's reader of that bit, invoked at the producer: the discipline effects that
		// authored `ShouldRemove_OnWasBumped`. Retail's own read site is 0006's (first-disciplines) to recover.
		ElysiumDisciplines::NotifyBumped(*this);
		const bool bRecorded = Npc->OnBumped(Now);
		// The touched NPC's side of the same reader. Every authored `ShouldRemove_OnWasBumped 1`
		// row is a TARGET effect (Hysteria, Trance, Nightwisp Ravens...), so the effect that breaks
		// sits on the NPC, and it is run on the one NPC-side event retail raises here -- the
		// mask-admitted `WAS_BUMPED`. Whether retail's effect reader keys on that condition or on a
		// misc flag of the victim's own is 0006's (first-disciplines) to recover; this follows the handler.
		if (bRecorded)
		{
			ElysiumDisciplines::NotifyBumped(*Npc);
		}
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s bumped %s (misc 0x%x, WAS_BUMPED %s)"),
			*DebugString(), *Npc->DebugString(), MiscFlags,
			bRecorded ? TEXT("recorded") : TEXT("refused by the mask"));
	}
}

void FElysiumPlayer::SyncFromBody()
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FVector Feet; FRotator View = FRotator::ZeroRotator;
	if (!Embodiment || !Embodiment->GetPlayerFeetTransform(Feet, View))
	{
		return;   // no pawn (a menu backdrop, a headless world): the entity keeps its last position
	}
	// Written straight into the fields: SetRuntimeOrigin would call OnRuntimeTransformChanged, which
	// teleports the pawn — every frame, to where it already is.
	Origin = Feet;
	Angles = ElysiumPlayerView::ToSource(View);
	// `CBasePlayer+0x3d4` `m_vecVelocity`, world cm/s. The see-unknown sweep's closing speed
	// (`0x102b15c0`) and the enemy memory's observed velocity read it. The sample carries the body's
	// motion in its facing frame, so it goes back to world axes here; a body with no published
	// sample keeps its last velocity, as it keeps its last position above.
	FElysiumLocomotionSample Sample;
	if (Embodiment->SamplePlayerLocomotion(Sample))
	{
		const FVector Planar = FRotator(0.0f, Sample.FacingYaw, 0.0f)
			.RotateVector(FVector(Sample.LocalVelocity.X, Sample.LocalVelocity.Y, 0.0));
		Velocity = FVector(Planar.X, Planar.Y, Sample.LocalVelocity.Z);
	}
}

void FElysiumPlayer::OnRuntimeTransformChanged()
{
	// The skeletal surface is attached to the pawn, so moving the pawn carries it. Do not run
	// FElysiumAnimating::OnRuntimeTransformChanged, which would treat its relative transform as a
	// map-root world transform and double-apply the placement.
	FElysiumEntity::OnRuntimeTransformChanged();
	// CreateControllerNPC snapshots a scene-owned duplicate that RemoveControllerNPC later uses as
	// the player's final pose anchor. An explicit player transform (point_teleport, console teleport,
	// or script SetOrigin/SetAngles) is authoritative while that relationship exists; carry it onto
	// the duplicate so delayed teardown cannot restore the pre-teleport mark. Ordinary pawn movement
	// reaches SyncFromBody instead and deliberately leaves a scene-staged controller independent.
	if (FElysiumEntity* Controller = World ? World->FindPlayerController() : nullptr)
	{
		Controller->SetRuntimeTransform(Origin, Angles);
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->TeleportPlayer(Origin, ElysiumPlayerView::ToUnreal(Angles));
	}
}

void FElysiumPlayer::OnRuntimeModelChanged()
{
	InvalidateCharacterVisualRequest();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return; // a headless world still keeps the logical model string
	}
	// The held reaction goes back BEFORE the body it was taken on is torn down, because the
	// release is addressed by body: after the swap below, `Visual` names a component the claim was
	// never recorded against, and the old one's zero-duration claim would stand on the player driver
	// (which outlives the body) with nothing able to name it again.
	//
	// The classification is re-armed with it. Retail re-derives the ideal activity every frame, so a
	// new model resolves `ACT_PREBLOCK` through its OWN vocabulary and weapon ladder; clearing the
	// edge is what makes the next think ask for that rather than resume a cell the previous body's
	// bank named. The cached cell is not replayed across a body for the same reason.
	ReleaseHeldReaction();
	bBlockIntentStands = false;
	Embodiment->ClearPlayerVisual();
	Visual = nullptr;
	if (PrepareCharacterVisual()) InstallPreparedCharacterVisual();
}

void FElysiumPlayer::InstallPreparedCharacterVisual()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || Model.IsEmpty()) return;
	Visual = Embodiment->BuildPlayerVisual(ModelStem(), Disposition, IdleVariant());
	if (Visual)
	{
		World->RegisterNpcBody(Visual);
		GateVisual();
		RefreshPreparedExpressions();
	}
}

void FElysiumPlayer::SetHiddenByController(bool bInHidden)
{
	bHiddenByController = bInHidden;
	GateVisual();
}

void FElysiumPlayer::GateVisual()
{
	// The pawn owns the surface. Publishing the entity's hide state and letting the pawn combine it
	// with the camera's eligibility is what keeps one flag from having two writers — the defect that
	// let a scene clip un-hide a body the camera had just put away.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->SetPlayerBodyEntityHidden(IsInert() || bHiddenByController);
	}
}

// --- Death (RC14): `Event_Killed`, and the think that walks the life state --------------------
//
// **Retail has no death camera.** `vampire.dll` never sets a view entity on a player in any
// situation: `IVEngineServer::SetView` (engine slot 86) is never dispatched, `m_hViewEntity` does not
// exist as a string in any module, `point_viewcontrol` is not in the factory table, and the strings
// `observer` / `ObserverMode` / `spec_` / `DeathCam` return nothing in every module. `StartDeathCam`
// (`0x10166cc0`) and `StartObserverMode` (`0x10167030`) are complete Source ports and both are
// unreachable — their guard is `CHalfLife2::IsMultiplayer()`, whose whole body is `return 0`
// (`0x101abcc0`) — so **neither is ported**; they are recovered in full in
// `docs/vtmb/camera-view-modes.md` so nothing has to re-open them.
//
// What death actually is, is a freeze: the sign goes up, the body plays its death animation and
// becomes a client-side ragdoll, and the view stays on the player's own eye, at the world position
// and view offset he died at, with mouse look still live. The only camera-visible writes are
// `m_iFOV = 0` and the velocity the think below bleeds to zero.

void FElysiumPlayer::OnKilled()
{
	// `m_lifeState` is retail's own re-entry guard: `Event_Killed` is reached from the damage commit,
	// and a second killing blow on a body already dying must not restart the sequence.
	if (!IsAlive())
	{
		return;
	}

	// `Event_Killed` step 1 is the death **sign** (`vdata/Signs/death.txt`, `0x10586354`), raised on a
	// `CSingleUserRecipientFilter` at the very top of the function: a 2-D UI panel drawn over a world
	// that keeps running, and the only "death camera" retail has. **The port raises nothing here**,
	// and the reason is stated rather than assumed: the session's game-over screen is not that sign —
	// it is the port's expression of where the sequence ENDS (`LIFE_RESPAWNABLE`, retail's terminal
	// state, whose only exit is a load) — and raising it now pauses the substrate clock and the
	// engine with it, which would stop the death animation, the friction and
	// `interface/final_death.wav` from ever happening. It is raised from `PlayerDeathThink`'s
	// `LIFE_DEAD -> LIFE_RESPAWNABLE` step instead. A sign-shaped overlay at this instant is UI the
	// port does not have; that, and only that, is what is missing here.

	// Step 5 — `SetAnimation(4)`, the death animation (`CBasePlayer::SetAnimation` `0x10164240`,
	// slot 449). Asked for through the one Reaction-band producer, on the same ladder
	// `TASK_PLAY_DEATH_SEQUENCE` walks, so the player and the cast resolve a death performance the
	// same way. What it is worth to the view is its LENGTH: retail's think waits on
	// `m_bSequenceFinished`, and a body that plays nothing takes the transition on the first frame.
	{
		FElysiumReactionPlayRequest Die;
		Die.Activity = ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::DieSimple);
		Die.bAllowFallbackLadder = false;
		float Seconds = 0.f;
		DeathAnimEndTime = PlayReactionActivity(Die, &Seconds) && Seconds > 0.f
			? (World ? World->NowSeconds() : 0.0) + static_cast<double>(Seconds)
			: -1.0;
	}

	// Step 6 — `m_lifeState = LIFE_DYING`, and `pl.deadflag = 1` beside it.
	LifeState = EElysiumLifeState::Dying;
	DeathFrames = 0.f;

	// Step 10 — `m_iFOV (+0x1e78) = 0`, **the only camera-visible write in the whole death path**.
	// It is a cancel-the-weapon-zoom write, not a death FOV: the client's HUD think resolves the
	// zero to 60 (`FUN_100f28d0`), which is VtMB's ordinary play lens. Do not push 75 — that is only
	// the no-local-player fallback (RC14 §3c, correcting RC10).
	if (IElysiumEmbodiment* Bodily = World ? World->Embodiment() : nullptr)
	{
		Bodily->SetPlayerFovOverride(0);
	}

	// Step 11 — `CBaseCombatCharacter::Event_Killed` (`0x1032b9b0`) LAST, which is where the OnDeath
	// output and the owner notification live. Its own tail builds the ragdoll force and calls
	// `CreateCorpse` -> `BecomeClientRagdoll` (`0x10090180`): the pose freezes, the entity goes
	// `FSOLID_NOT_SOLID` and `MOVETYPE_NONE`, its bbox collapses to a point and its think stops —
	// and **its origin and view offset are never written**, which is exactly why the view does not
	// move. The port's body half of that (the ragdoll, `EF_NODRAW` on the server model) belongs to
	// the body lane and is NOT done here: hiding the player's model with no client ragdoll drawing
	// in its place would be a divergence in the other direction.
	FElysiumCombatCharacter::OnKilled();
}

void FElysiumPlayer::PlayerDeathThink()
{
	const double Now = World ? World->NowSeconds() : 0.0;

	// `SetNextThink(curtime + 0.1)` (`_DAT_104493d0`), retail's own first line. It is a leftover:
	// `PlayerDeathThink` is not a `SetThink` target — its one caller is `PreThink`, once per frame —
	// so the deadline it arms is never what runs it. Ported because it is state a load can observe.
	NextThink = static_cast<float>(Now + 0.1);

	IElysiumEmbodiment* Bodily = World ? World->Embodiment() : nullptr;

	// The ground friction. `FL_ONGROUND` only, and 20 SOURCE UNITS PER FRAME (`_DAT_1044eb0c`),
	// not per second: the think is frame-driven, so the constant is too. This is the only thing that
	// still moves in the death view — the roll (`V_CalcRoll`, and retail's `CalcBob` beside it) keeps
	// composing over a velocity that reaches zero in two or three frames.
	if (Bodily != nullptr && Bodily->IsPlayerOnGround())
	{
		Bodily->BleedPlayerBodyVelocity(20.0f * ElysiumMove::U);
	}

	// `if (HasWeapons()) PackDeadPlayerItems()` — slot `0x6e4`, UNRECOVERED. It is the next thing
	// retail does and it is deliberately not invented here; it touches the corpse's inventory rather
	// than the view, so nothing in this document depends on it.

	// The `LIFE_DYING` hold: `GetModelIndex() && !m_bSequenceFinished` -> `StudioFrameAdvance()` and
	// `if (++m_iRespawnFrames < 60.0f) return;`. **Both exits are real**: the animation finishing
	// ends it early, and 60 frames ends it whatever the animation is doing. A body that plays no
	// death performance at all (`DeathAnimEndTime < 0`) has a finished sequence by this test and
	// falls straight through, which is retail's `GetModelIndex() == 0` arm.
	if (LifeState == EElysiumLifeState::Dying && DeathAnimEndTime >= 0.0 && Now < DeathAnimEndTime)
	{
		DeathFrames += 1.0f;   // `_DAT_104454c0` = 1.0, added to a FLOAT counter
		if (DeathFrames < 60.0f)   // `_DAT_104492a4` = 60.0
		{
			return;
		}
	}

	if (LifeState == EElysiumLifeState::Dying)
	{
		// `LIFE_DYING -> LIFE_DEAD`, with the one cue the whole sequence has:
		// `interface/final_death.wav` (`0x10586440`) on channel 2, volume 1.0, attenuation 0.8,
		// pitch 100, through a `CPASAttenuationFilter` built at the player's own ear position.
		LifeState = EElysiumLifeState::Dead;
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			FElysiumAudioRequest Request;
			Request.Source = FElysiumAudioSource::Path(TEXT("interface/final_death.wav"));
			Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
			Request.Owner.StableId = TEXT("player.final_death");
			Request.Placement.bSpatialized = false;   // the filter is built AT the listener's ear
			Request.Gain = 1.0f;
			Request.Pitch = 1.0f;
			Request.Routing = EElysiumAudioRouting::NoGameplayNoise;
			Request.ConcurrencyKey = TEXT("player.final_death");
			Audio->Submit(MoveTemp(Request));
		}
	}

	// The tail retail reaches on every frame from here on: `m_fEffects |= 0x10` (`EF_NODRAW` — the
	// server model stops drawing because the CLIENT ragdoll is drawing instead) and
	// `m_flPlaybackRate = 0` (`StopAnimation`). Both are body facts with no view of their own, and
	// the port has no client ragdoll to put in the model's place, so neither is applied — see
	// `OnKilled` above.

	if (LifeState == EElysiumLifeState::Dead)
	{
		// `LIFE_DEAD -> LIFE_RESPAWNABLE` on the first frame with no button held. Retail's mask is
		// `buttons & ~IN_SCORE` (`0x10000`, the multiplayer scoreboard), and this runtime has no
		// scoreboard bit at all, so every published button counts. The published mask is the
		// controller's combat subset, which is the whole of what the substrate is told.
		if (World != nullptr && World->GetPlayerButtons() != 0)
		{
			return;
		}
		// `g_pGameRules->FPlayerCanRespawn(this)` — `CHalfLife2` slot 37 (`0x101abee0`), whose whole
		// body is `return 1`. A constant, so there is no rules object to ask.
		LifeState = EElysiumLifeState::Respawnable;

		// **The port's game-over screen goes up HERE**, on the transition into retail's terminal
		// state, because that is the state it expresses: a run that will not advance again and whose
		// only exit is a load. Retail's own UI at this instant is nothing new — the sign has been up
		// since `Event_Killed` — but retail also has a main menu behind Escape at any time, and this
		// screen is the port's load-or-quit door rather than a reproduction of the sign
		// (`menu-hud-not-vtmb-reproduction`). Raising it also pauses the clock and the engine, which
		// is why it cannot go up before the sequence has run: everything above this line would stop.
		if (UElysiumSessionSubsystem* State = World ? World->GetGameState() : nullptr)
		{
			State->NotifyPlayerKilled();
		}
		return;
	}

	// `LIFE_RESPAWNABLE`, and **this is where a shipped run stops, forever.** Everything below the
	// transition in retail is multiplayer-gated and dead in single player:
	//
	//   * `StartDeathCam` at `m_flDeathTime + 6.0` — behind `IsMultiplayer()` (`return 0`);
	//   * `mp_forcerespawn` (`0x1070a9f8`, default "1") and the `+5.0 s` forced respawn — the same;
	//   * `respawn()` (`0x10352ed0`) — a `RET` when both single-player `gpGlobals` bytes are 0.
	//
	// So the port must not auto-respawn and must not advance on its own. The death UI owns the exit,
	// and the only thing that clears the state is a load through `CHL2_Player::Spawn` (slot 103).
}

void FElysiumPlayer::InputGiveItem(const FElysiumInputArgs& Args)
{
	// STRING — the item's `vdata/items` key, 126 wires game-wide. `GiveItem` exists twice, as this
	// input and as a Character method, and the two need not share an implementation; both reach the
	// one player-only service, `GiveNamedItem`.
	const FString Classname = Args.Param.ToString();
	if (!Inventory.GiveNamedItem(*this, Classname).IsSet())
	{
		// Retail's own line for a grant that did not land.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s Could not give item (\"%s\")"), *DebugString(), *Classname);
	}
}

void FElysiumPlayer::InputAwardExperience(const FElysiumInputArgs& Args)
{
	// STRING, not an amount: it names an entry the engine looks up in the experience table. The
	// handler takes the variant's string when the field type is STRING and otherwise stringifies
	// it, which a Hammer wire's string parameter satisfies either way.
	AwardExperience(Args.Param.ToString());
}

// The three activity-level inputs (`docs/vtmb/player-entity.md` § "Law, Masquerade and world
// response"). Each takes an integer entity-input variant, clamps it to 0..5 and treats a wrong
// type or a negative value as zero. No authored wire in the corpus carries a second parameter and
// the one script call is `pc.SetCriminalLevel(1)`, so the input form never names a duration: it
// always passes `DeriveDuration`, and the finite `max(previously retained level, pl_min_act_timer)`
// deadline falls out of the rule. A native producer that DOES know its own duration (the feed
// pulse's two seconds) calls `ElysiumLaw::Set*Level` directly with it.

void FElysiumPlayer::InputSetCriminalLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetCriminalLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

void FElysiumPlayer::InputSetInvestigateLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetInvestigateLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

void FElysiumPlayer::InputSetSupernaturalLevel(const FElysiumInputArgs& Args)
{
	ElysiumLaw::SetSupernaturalLevel(*this, ElysiumLaw::SanitizeLevel(Args.Param));
}

// --- The dialogue refusal predicate and the dialogue holster ---------------------------------
//
// `FUN_10178170` is the player-side predicate `FUN_10178280` consults when `m_bForceDialogStart`
// (`npc+0x6495`) is clear: a set of combat timers on `CBasePlayer` plus a threat count and a
// partner-state check. Reproduced arm for arm; the arms with no producer in this runtime are
// named seams on the class and are read here so none of them is silently dropped.

const TCHAR* FElysiumPlayer::DialogRefusalReason() const
{
	const double Now = World ? World->NowSeconds() : 0.0;
	// `player+0x1d1c`, compared against `curtime`.
	if (NoDialogueUntil > 0.0 && Now < NoDialogueUntil)
	{
		return TEXT("the player was just in combat");
	}
	// `player+0x1dd0` / `player+0x1dd8`, the same comparison. No producer yet (seam on the class).
	if (DialogCombatStampA > 0.0 && Now < DialogCombatStampA)
	{
		return TEXT("a combat timer is running");
	}
	if (DialogCombatStampB > 0.0 && Now < DialogCombatStampB)
	{
		return TEXT("a combat timer is running");
	}
	// The threat count (`0x1017f770` / `0x1017f8b0`). Seam: answers 0.
	if (DialogThreatCount() > 0)
	{
		return TEXT("something is still hunting the player");
	}
	// `player+0x1cf8`: FLT_MAX is the CLEAR sentinel, so anything else blocks. Seam: never written.
	if (DialogRefusalFloat != TNumericLimits<float>::Max())
	{
		return TEXT("a combat timer is running");
	}
	// `0x10175180` — the partner at `player+0x1db0` is in state 3. Seam: answers false.
	if (DialogPartnerBlocks())
	{
		return TEXT("the player is already occupied");
	}
	return nullptr;
}

void FElysiumPlayer::StampDialogCombatRefusal(double Now)
{
	// Retail's own duration is unrecovered; `ElysiumDialogue::DamageRefusalSeconds` carries that
	// note. Monotonic: a second hit inside the window extends it, it never shortens it.
	NoDialogueUntil = FMath::Max(NoDialogueUntil, Now + ElysiumDialogue::DamageRefusalSeconds);
}

void FElysiumPlayer::ClearDialogCombatTimers()
{
	NoDialogueUntil = 0.0;
	DialogCombatStampA = 0.0;
	DialogCombatStampB = 0.0;
	DialogRefusalFloat = TNumericLimits<float>::Max();
	UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s ClearDialogCombatTimers"), *DebugString());
}

void FElysiumPlayer::OnDamageEntered()
{
	// `CBasePlayer::vfunc142` `0x10163020`, listing order: the two refusal early-outs
	// (`[EAX+0x658]` at `10163034`, the damage-bits filter at `10163086`), the grapple break, then
	// `10163126 CALL 0x10014a6a` -> `FUN_10167fd0(player)` — the ONE release body — and only after
	// it the damage modifiers (`[EDX+0x278]` at `101631e4`), the gamerules test and the health
	// apply (`0x1000e854` at `10163278`). So any damage the player accepts drops the terminal,
	// the container, the sign or the lockpick session first; a killing blow is not special.
	//
	// `EndPlayerUseSession` with an invalid owner is "cancel whatever is active", which is exactly
	// what retail's `resolve(player+0x1040)` does.
	if (World)
	{
		World->EndPlayerUseSession(FElysiumEntityHandle::Invalid(),
			EElysiumUseEndReason::Interrupted);
	}
}

void FElysiumPlayer::OnDamageCommitted(const FElysiumDmg& Dmg)
{
	// The one producer of `+0x1d1c` this runtime has. It sits on the COMMIT, not on the entry, so a
	// refused or fully-absorbed hit does not lock the player out of a conversation.
	StampDialogCombatRefusal(World ? World->NowSeconds() : 0.0);
}

bool FElysiumPlayer::HolsterForDialog()
{
	if (bDialogWeaponHolstered)
	{
		return true;   // already put away; a second open must not overwrite the remembered weapon
	}
	// `player+0x1e01` — whether the active weapon was drawable, remembered across the conversation.
	// SEAM: this runtime carries no per-weapon "drawable" byte, so the remembered value is simply
	// "something was in hand". TODO(dialogue-plan): recover the writer of `player+0x1e01`.
	const FElysiumEntityHandle Previous = Inventory.ActiveWeapon;
	FElysiumItem* Unarmed = Inventory.FindOrdinary(*this, TEXT("item_w_unarmed"));
	if (!Unarmed)
	{
		// The same refusal the `Holster` input makes: no carried `item_w_unarmed` to fall back to,
		// and the inventory has no representation for empty hands.
		ElysiumStub::Fired(TEXT("method"), TEXT("CBasePlayer.DialogHolster"), DebugString(),
			FString(), TEXT("dialogue open: no carried item_w_unarmed to switch to"));
		return false;
	}
	if (Previous == Unarmed->Handle)
	{
		return true;   // already empty-handed; nothing to remember and nothing to restore
	}
	if (!Inventory.SetActiveWeapon(*this, *Unarmed))
	{
		ElysiumStub::Fired(TEXT("method"), TEXT("CBasePlayer.DialogHolster"), DebugString(),
			FString(), TEXT("dialogue open: the equip funnel refused item_w_unarmed"));
		return false;
	}
	DialogHolsteredWeapon = Previous;
	bDialogWeaponWasDrawable = Previous.IsSet();
	bDialogWeaponHolstered = true;
	return true;
}

void FElysiumPlayer::RestoreDialogHolster()
{
	if (!bDialogWeaponHolstered)
	{
		return;
	}
	const FElysiumEntityHandle Wanted = DialogHolsteredWeapon;
	bDialogWeaponHolstered = false;
	DialogHolsteredWeapon = FElysiumEntityHandle::Invalid();
	if (!bDialogWeaponWasDrawable || !Wanted.IsSet() || !World)
	{
		return;   // `FUN_10178400` restores nothing when the remembered byte was clear
	}
	FElysiumEntity* Entity = World->Resolve(Wanted);
	FElysiumItem* Item = Entity ? Entity->AsItem() : nullptr;
	if (!Item)
	{
		// The weapon was dropped, destroyed or taken while the conversation ran. Retail's restore
		// is a handle read too, so a stale handle simply leaves the hands as they are.
		return;
	}
	Inventory.SetActiveWeapon(*this, *Item);
}

void FElysiumPlayer::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	// `m_lifeState`, and the death think's frame counter beside it — the two the death view is a
	// function of. Named states rather than the integer, because the integer is Source's.
	{
		static const TCHAR* const StateNames[] = { TEXT("alive"), TEXT("dying"), TEXT("dead"),
			TEXT("respawnable") };
		const int32 Index = FMath::Clamp(static_cast<int32>(LifeState), 0, 3);
		Out.Emplace(TEXT("Life state"), LifeState == EElysiumLifeState::Alive
			? FString(StateNames[Index])
			: FString::Printf(TEXT("%s (%.0f death-think frames)"), StateNames[Index], DeathFrames));
	}
	Out.Emplace(TEXT("Origin"), Origin.ToString());
	Out.Emplace(TEXT("Facing"), FString::Printf(TEXT("yaw %.0f"), -Angles.Y));
	Out.Emplace(TEXT("Law"), FString::Printf(TEXT("criminal %d / supernatural %d / investigate %d"),
		Law.Criminal, Law.Supernatural, Law.Investigate));
	// The deadlines, the act counts and the response/pursuit state beside them.
	Out.Emplace(TEXT("Law deadlines"), FString::Printf(
		TEXT("criminal %.2f / supernatural %.2f (act counts %d / %d)"),
		Law.CriminalExpiry, Law.SupernaturalExpiry, Law.CriminalCount, Law.SupernaturalCount));
	Out.Emplace(TEXT("Police"), FString::Printf(
		TEXT("%s, cops %d, hunters %d, %s, masquerade window %.2f"),
		Police.bResponsePending
			? *FString::Printf(TEXT("response severity %d due %.2f"),
				Police.ResponseSeverity, Police.ResponseDeadline)
			: TEXT("no pending response"),
		Police.CopsInPursuit, Police.HuntersInPursuit,
		Police.bHeightenedAlert
			? *FString::Printf(TEXT("heightened alert until %.2f"), Police.HeightenedAlertExpiry)
			: TEXT("no alert"),
		Police.MasqueradeTimerNext));
	Out.Emplace(TEXT("World area"), World
		? FString::FromInt(ElysiumLaw::WorldAreaType(*World))
		: FString(TEXT("(no world)")));
	Out.Emplace(TEXT("XP"), FString::Printf(TEXT("%d spent-able, %d awards, %.0f raw (%.0f pending)"),
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
		ExperienceLog.Num(), LifetimeExperience, ExperienceRemainder));
	// The selection state the two cast verbs read.
	Out.Emplace(TEXT("Discipline selection"), SelectedDiscipline == INDEX_NONE
		? FString(TEXT("(none)"))
		: FString::Printf(TEXT("%s tier %d, %d cast(s)"),
			ElysiumDisciplines::InternalName(SelectedDiscipline), SelectedTier, DisciplineCastCount));
	Out.Emplace(TEXT("Body"), (World && World->Embodiment()) ? TEXT("pawn") : TEXT("(none)"));
	// The dialogue-entry predicate and the holster, so a refused conversation is diagnosable.
	const TCHAR* Refusal = DialogRefusalReason();
	Out.Emplace(TEXT("Dialog refusal"), Refusal
		? FString::Printf(TEXT("REFUSED (%s; no-dialogue-until %.2f)"), Refusal, NoDialogueUntil)
		: FString(TEXT("clear")));
	Out.Emplace(TEXT("Dialog holster"), bDialogWeaponHolstered
		? FString::Printf(TEXT("holstered %s (drawable %s)"), *DialogHolsteredWeapon.ToString(),
			bDialogWeaponWasDrawable ? TEXT("yes") : TEXT("no"))
		: FString(TEXT("(none)")));
}
