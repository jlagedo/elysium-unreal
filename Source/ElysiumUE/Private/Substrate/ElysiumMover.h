#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumEntity.h"

// The brush-mover substrate. `docs/vtmb/animation_and_movers.md` Part B (decompiled `vampire.dll`:
// the Source CBaseToggle/CBaseDoor lineage, RTTI-confirmed) is the reference for everything here.
//
// FElysiumMoverBase is the CBaseToggle primitive: LinearMove/AngularMove drive the entity's brush
// body (UElysiumBrushComponent) at a CONSTANT velocity toward a target transform — no easing —
// snapping and firing MoveDone() on arrival (B.4). Motion runs on the substrate's own think clock
// (R4: one clock, one queue, no engine timers), re-arming NextThink each moving frame.
//
// FElysiumDoorBase layers the CBaseDoor 4-state machine (m_toggle_state) + the Open/Close/Toggle/
// Lock/Unlock inputs + the OnOpen/OnClose/OnFullyOpen/OnFullyClosed outputs + `wait` autoclose +
// the locked path (silent Open/Toggle inputs; +use plays only the `locked` sound, fires no output) +
// blocked-while-closing (deal `dmg`, reverse, OnBlockedClosing) +
// the full spawnflag table (B.5) + `linked_door` (the paired leaf) + the +use doorknob path. Two
// leaves derive from it: func_door_rotating (swings `distance` about the hinge) and func_door
// (slides `movedir` by its own depth). A leaf supplies only how it computes its open transform
// and which primitive it issues. Registered as the "CBaseDoor" chain node so both leaves inherit the
// inputs/fields through one case-folded chain walk (R2).

class UElysiumBrushComponent;
class FElysiumLockableEntity;

// The one log category the mover family writes on. Declared here rather than defined per-file
// because the family is split across `ElysiumMover.cpp`, `ElysiumElevator.cpp` and
// `ElysiumMoverSounds.cpp`, and one `LogElysiumMover` filter shows the whole mover story.
// `ElysiumMover.cpp` owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumMover, Log, All);

// Source keyvalues (speed, lip, distance-as-inches) are raw Source inches; the exporter emits
// geometry in cm (the UE_ convention). Linear travel must convert; angular (degrees) does not.
inline constexpr float MoverInchToCm = 2.54f;

// Source movedir from raw-Source `angles`, returned in Unreal space — the one shared SetMovedir
// derivation (sentinels + the Source→Unreal Y reflection) for every mover that slides along its
// authored angles: the sliding door and the button. Defined (with the full semantics comment) in
// ElysiumMover.cpp.
FVector SourceAnglesToUnrealDir(const FVector& AnglesDeg);

// The CBaseToggle constant-velocity mover. Not a registered class — a pure-C++ base the door/
// button/rotating families derive from. Drives the entity's Body transform; a bodiless mover is
// inert (nothing to move).
class FElysiumMoverBase : public FElysiumEntity
{
public:
	// One in-flight constant-velocity move. Linear translates the body; Angular rotates it about
	// its own pivot (= the def origin = the hinge for rotating doors). None = at rest.
	enum class EMoveKind : uint8 { None, Linear, Angular };

	// Start a constant-velocity move to a body-relative destination transform. `Speed` is in the
	// source units the keyvalue carries: cm/s-equivalent for Linear (the caller converts Source
	// inches → cm), deg/s for Angular. Schedules per-frame thinking; MoveDone() fires on arrival.
	void LinearMove(const FVector& DestRelLoc, float SpeedCmPerSec);
	void AngularMove(const FRotator& DestRelRot, float SpeedDegPerSec);

	EMoveKind MoveKind() const { return CurrentMove; }
	// Slot 153, `CBaseEntity::IsMoving`: `m_vecVelocity != vec3_origin`. A mover's
	// velocity is non-zero exactly while `LinearMove` / `AngularMove` has a move in flight (retail's
	// `CBaseToggle` sets `m_vecVelocity` / `m_vecAngVelocity` for the move and zeroes them at
	// `MoveDone`), which is what `CurrentMove` records. The slot's override on the mover line (it hid
	// the slot as a `const` method before, story 5 commit B).
	virtual bool IsMoving() override { return CurrentMove != EMoveKind::None; }

protected:
	// Slot 133 `MoveDone`: fired once when a move reaches its target (the body is already snapped to
	// the exact dest). `CBaseToggle`'s own is empty; the door overrides this to route HitTop/HitBottom.
	virtual void MoveDone() override {}

	// A swept move hit a blocker mid-flight. Base default: nothing (the move clamps and retries
	// next frame). The door overrides to reverse + damage when closing.
	virtual void OnMoveBlocked(const FHitResult& Hit) {}

	// Advance the active move for this think frame (called from the leaf's Think while moving).
	void TickMove(double Now);

	// The current body-relative rest transforms, cached at Spawn so the state machine can move
	// between them without re-deriving (closed = spawn pose, open = leaf-computed).
	FVector  ClosedLoc = FVector::ZeroVector;
	FRotator ClosedRot = FRotator::ZeroRotator;
	FVector  OpenLoc   = FVector::ZeroVector;
	FRotator OpenRot   = FRotator::ZeroRotator;

	// Apply a body-relative transform immediately (teleport, no sweep) — used to seat the initial
	// pose (START_OPEN) before any motion.
	void SnapBody(const FVector& RelLoc, const FRotator& RelRot);

	// --- Mover sounds ---
	// VtMB resolves a mover's `soundgroup` token by directory convention (no data file): the WAVs
	// live under sound/usable/<Category>/<soundgroup>/<subkey>.wav (RE: CBaseDoor::Spawn @0x100ef060
	// reads open/close/swing/locked; CBaseButton::Spawn @0x100c8810 reads on/off). InitMoverSounds
	// reads the `soundgroup` key + the SILENT spawnflag and resolves the shipped subkeys from the
	// corpus (ElysiumSoundGroups::Resolve). Call from the leaf's Spawn(). Category is
	// "openable" (doors) or "switches" (buttons); SilentFlag is the SF bit that mutes the mover (0 = none).
	void InitMoverSounds(const TCHAR* Category, int32 SilentFlag);

	// Play a resolved subkey one-shot (open/close/on/off/locked) at the body — 3D, attached so it
	// tracks the mover, sphere attenuation. No-op if silent, the subkey isn't shipped, or bodiless.
	void PlayMoverSound(FName Sub);
	// Play an explicit WAV (sound-relative, e.g. button `unlocked_sound`) the same way. No-op if empty.
	void PlayMoverSoundRel(const FString& Rel);
	// The looping "moving" sound (door `swing`): started on motion, stopped on arrival. Tracks its
	// voice so StopMoverLoop can end it; StartMoverLoop replaces any running loop.
	void StartMoverLoop(FName Sub);
	void StopMoverLoop();

	// Append the mover-sound state (group/category/silent/resolved subkeys/last played) to a debug
	// list — the leaf's GetDebugState calls this so the Cog inspector shows what will play.
	void AppendSoundDebug(TArray<TPair<FString, FString>>& Out) const;

	// The `soundgroup` token (lower-cased; empty = none) and which usable/ subtree it draws from.
	FString SoundGroup;
	FString SoundCategory;
	bool    bMoverSilent = false;              // the SILENT spawnflag was set
	TMap<FName, FString> SoundSubs;            // resolved subkey -> sound-relative WAV (shipped only)
	FString LastMoverSound;                    // debug: the last sound played (rel), or empty

private:
	FElysiumVoiceHandle MoverLoopVoice;         // generation-safe looping travel voice


	EMoveKind CurrentMove = EMoveKind::None;
	FVector  MoveStartLoc = FVector::ZeroVector;
	FVector  MoveDestLoc  = FVector::ZeroVector;
	FRotator MoveStartRot = FRotator::ZeroRotator;
	FRotator MoveDestRot  = FRotator::ZeroRotator;
	double   MoveStartTime = 0.0;
	double   MoveDoneTime  = 0.0;

	void BeginMove(EMoveKind Kind, const FVector& DestLoc, const FRotator& DestRot, double TravelSeconds);
};

// `GetNPCOpenData` (door slot 246, `+0x3d8`)'s out struct, 0x1c bytes: `+0 Vector StandPos`,
// `+0xc Vector FaceDir`, `+0x18 int Activity` (-1 = the door has no data for this NPC). Stated in
// the PORT's frame -- `StandPosCm` world centimetres, `FaceDir` a unit direction with retail's Y
// mirrored -- because each of the three readers hands its field straight to a port seam (the
// route goal, `RetailVecToYaw`, the activity id). Findings § 1 / `docs/vtmb/navigation-jump-links.md`.
struct FElysiumDoorNpcOpenData
{
	FVector StandPosCm = FVector::ZeroVector;   // +0x00, slot 531 arm 7's `BuildLocalRoute` goal
	FVector FaceDir = FVector::ZeroVector;      // +0x0c, alternate-AI mode 1's facing
	int32 Activity = INDEX_NONE;                // +0x18, `0x10298840`'s `RestartIdealActivity`
};

// The stale words of the door's smart link -- the retail `CAI_Link` words `0x102f1fa0` writes and
// `0x102fce80` reads: `link+0x64` bit 1 (`bStale`), `link+0x68` (`StaleUntil`, an absolute curtime)
// and `link+0` (`StaleDoor`, the blocker handle, -1 = none). The port lays ONE smart link per
// traversable door (`AElysiumNavDoorLink`, keyed by the door's lump ordinal) where retail has one
// AIN link per node pair per hull, so the words ride the door and the engine actor reads them
// through the entity world. NOT saved: `CAI_Link` has no datamap, so retail loses every stale mark
// on load (findings § 8), and so does this.
struct FElysiumDoorLinkWords
{
	bool bStale = false;
	double StaleUntil = 0.0;
	FElysiumEntityHandle StaleDoor;
};

// The CBaseDoor 4-state machine over the mover primitive. func_door_rotating (this file) and
// func_door derive from it; both register with BaseName "CBaseDoor".
class FElysiumDoorBase : public FElysiumMoverBase
{
public:
	// --- The NPC-failure words (0018/7) ---------------------------------------------------------
	// `+0x644` `m_bfNpcFailedFlags` (datamap, FIELD_INTEGER) -- the NPC-failure flag word. Bits by their writers: 1 squad-focus door (slot 531
	// arm 2), 2 `StartBlocked`'s activator, 4 slot 531 arm 4 (`0x1027f550` refused), 8 no `0xd00`
	// capability, 0x10 retry pending (`0x1027f550`), 0x40 no route (slot 531 arm 7's failed open),
	// 0x80 hit a character (`IsCloseBlocked` / `StartBlocked`). The bits' names are unrecovered. SAVE row.
	uint32 NpcFailedFlags = 0;
	// `+0x640` `m_flNpcFailedTimer` (datamap, FIELD_TIME) -- "no NPC tries me again before", an
	// absolute curtime. SAVE row. Zeroed by
	// `DoorHitTop 0x100f0860`.
	double NpcFailedTimer = 0.0;

	// `0x100f0e70`: `+0x644 = 0`.
	void ClearNpcFailedFlags() { NpcFailedFlags = 0; }
	// `0x100f0e90`: `+0x644 |= Bits`.
	void AddNpcFailedFlags(uint32 Bits) { NpcFailedFlags |= Bits; }
	// `0x100f0e30`: `if (At >= +0x640) +0x640 = At` -- a MAX write.
	void RaiseNpcFailedTimer(double At)
	{
		if (At >= NpcFailedTimer)
		{
			NpcFailedTimer = At;
		}
	}
	// `0x100f0ec0`: `(+0x644 & Mask) == Mask`.
	bool HasNpcFailedFlags(uint32 Mask) const { return (NpcFailedFlags & Mask) == Mask; }

	// The door's smart-link stale words (`FElysiumDoorLinkWords`).
	FElysiumDoorLinkWords LinkWords;
	// `0x102f1fa0`'s writes on the link: `link+0x64 |= 1`, `link+0x68 = Now + Seconds`, `link+0 =`
	// the blocker's handle or -1. The gates (`nav+0x50`, a live path, both node ids) are the
	// caller's (`FElysiumNpcBase::NavMarkLinkStale`).
	void MarkLinkStale(double Now, double Seconds, const FElysiumEntityHandle& Blocker)
	{
		LinkWords.bStale = true;
		LinkWords.StaleUntil = Now + Seconds;
		LinkWords.StaleDoor = Blocker;
	}

	// Slot 246 `GetNPCOpenData` (`+0x3d8`). `CBaseDoor 0x100f0ef0`'s body is `out+0x18 = -1` and
	// nothing else, so a sliding door answers no data and slot 531 exits at arm 6; the rotating
	// leaf overrides (`CRotDoor::GetNPCOpenData 0x100f2bd0`). `bOpening` is `toggle_state == 2`
	// at slot 531 and `m_bOpeningDoorWait` in the two alternate-AI callers.
	virtual FElysiumDoorNpcOpenData GetNPCOpenData(const FElysiumEntity* Npc, bool bOpening) const
	{
		(void)Npc;
		(void)bOpening;
		return FElysiumDoorNpcOpenData();
	}

	// `CBaseDoor::IsCloseBlocked` (`0x100f0c00`): the closing volume (slot 245) against every live
	// client / NPC's box, the linked door first when `bAskLinked`; a hit NPC is told
	// (`+0x644 |= 0x80`, `0x1027dfb0`). True = blocked. Run by the auto-close think `0x100f09d0`.
	bool IsCloseBlocked(bool bAskLinked);

	// `CBaseDoor::StartBlocked` (`0x100f1340`, slot 177). The body is ported; its DISPATCHER is
	// unrecovered (no vampire.dll site calls `+0x2c4`; the push code calls only `Blocked` /
	// `PhysicsImpact`), so nothing calls it -- a named seam (findings § 3).
	void StartBlocked(const FElysiumEntityHandle& Blocker);

	// m_toggle_state — VtMB's exact values (dword @0x4f8, B.4).
	enum class EToggleState : uint8 { AtTop = 0, AtBottom = 1, GoingUp = 2, GoingDown = 3 };

	// --- Door keyfields (B.2) ----------------------------------------------------------
	float Speed    = 100.0f;   // deg/s (rotating) or in/s (sliding) — leaf interprets
	float Distance = 90.0f;    // arc in degrees (rotating) / lip-adjusted travel (sliding)
	float Wait     = 4.0f;     // autoclose delay; -1 = stay open (STAY_OPEN)
	float Lip      = 0.0f;     // inches subtracted from a slide's travel (sliding leaf)
	int32 Dmg      = 0;        // crush damage dealt when blocked

	bool  bLocked  = false;    // Lock/Unlock + LOCKED spawnflag (0x800)

	// `noopenwanted` (Troika's FGD: "Block if Wanted"). Refuses +use only while the player is
	// actually being hunted — the key alone is never sufficient. See DoorUse step 3.
	bool  bNoOpenWanted = false;

	// `linked_door` (B.2, 485 uses): the targetname of the paired leaf of a double door. Movement I/O
	// (Open/Close) is wired to both leaves by the map data; the runtime link is the +use doorknob —
	// a player +use on one leaf toggles both (VtMB's CBaseDoor::DoorknobUse). Resolved lazily.
	FString LinkedDoorName;
	FElysiumEntityHandle LinkedDoor;   // cached partner handle; resolved on first Use
	FString UseOverrideName;           // named entity that receives Use instead of this leaf

	// The activator that last opened the door — propagated onto its outputs (Source m_hActivator).
	FElysiumEntityHandle LastActivator;

	EToggleState State() const { return ToggleState; }

	// --- Inputs (registered on CBaseDoor; reach both leaves via the chain) --------------
	void InputOpen(const FElysiumEntityHandle& Activator);
	void InputClose(const FElysiumEntityHandle& Activator);
	void InputToggle(const FElysiumEntityHandle& Activator);
	void InputLock();
	void InputUnlock();
	void RegisterDoorknob(FElysiumLockableEntity& Doorknob);
	void UnregisterDoorknob(const FElysiumEntityHandle& Doorknob);
	// The +use doorknob path (CBaseDoor::DoorknobUse): toggle this leaf and, if a `linked_door` is
	// set, its partner too — the double-door swing. Reached by the +use look-cursor and `ent_fire Use`.
	void DoorUse(const FElysiumEntityHandle& Activator);

	// CBaseDoor::DoorknobUse (FUN_100eef50, the datamap's CBaseDoorDoorknobUse). Reached from Use
	// on the mere EXISTENCE of a knob handle, before the admission test — so a knobbed door toggles
	// without the {AtTop, AtBottom} ∪ NO_AUTO_RETURN gate a knobless one is held to. Requires a
	// character activator; anything else is a logged no-op.
	void DoorknobUse(const FElysiumEntityHandle& Activator);

	// CBaseDoor::DoorActivate (FUN_100f0340, vtable +0x3c8): the toggle itself, with no lock or
	// admission test of its own — both are the caller's job.
	void DoorActivate(const FElysiumEntityHandle& Activator);

	// CBaseDoor::Use step 5 (vtable +0x3e0, FUN_100eff90 / CRotDoor FUN_100f2520): recompute
	// m_toggle_state from the LIVE body transform. Run UNCONDITIONALLY at the top of the +use path,
	// before the admission/locked decision. If the body sits within 0.001 per-component of the closed
	// endpoint the state becomes AtBottom; within 0.001 of the open endpoint it becomes AtTop; a body
	// genuinely mid-travel leaves the GoingUp/GoingDown state untouched. A door whose body never
	// actually moved is re-stamped to its true endpoint here, so the following activation opens it
	// again — the fix for a stale AtTop/GoingUp belief silently dropping a later +use.
	void ResolveToggleStateFromTransform();

	// --- +use / debug hooks ---
	// PUSE (0x100) is the dominant door bit (105 doors): it arms the +use look-cursor. Doors fire no
	// OnIn/OnOut (those are button-only outputs), so the cursor enter/leave stays a base no-op — only
	// activation and the reticle-arming (world-side) matter.
	virtual bool IsUsable() const override;
	// locked_icon on the reticle. The reticle is always the player's, so this asks the
	// predicate with the player as the user — otherwise a knob-gated door would draw its unlocked
	// icon and then refuse the +use behind it.
	virtual bool IsUseLocked() const override;

	// CBaseDoor::IsUseRefused (FUN_100eec70) — the real locked predicate, and the one the door's own
	// `bLocked` byte only answers when no doorknob is attached. A knob owns its lock; the door reads
	// it. Arm 1 (`0x100eef10`): `noopenwanted` refuses everyone while the player is hunted. NONPCS
	// (0x200) refuses an NPC activator ahead of the knob lookup.
	bool IsUseRefused(const FElysiumEntityHandle& Activator) const;

	// The "user" retail's door predicates actually receive: the activator's character sub-object,
	// null for any activator that is not a character (a relay, a button, a trigger).
	const class FElysiumCombatCharacter* ResolveUser(const FElysiumEntityHandle& Activator) const;

	// CBaseDoor::GetNearestDoorknob (FUN_100ee950): of the (at most two) registered knobs, the one
	// nearest the user by MANHATTAN distance between world-space centres, ties going to the
	// second-registered knob. Null when no knob is attached OR when there is no activator — retail
	// returns null for a null user, which is what lets a script-fired Open/Toggle bypass the knob.
	const FElysiumLockableEntity* FindNearestDoorknob(const FElysiumEntityHandle& Activator) const;
	FElysiumLockableEntity* FindNearestDoorknob(const FElysiumEntityHandle& Activator)
	{
		return const_cast<FElysiumLockableEntity*>(
			const_cast<const FElysiumDoorBase*>(this)->FindNearestDoorknob(Activator));
	}
	virtual void OnDormancyChanged() override;
	virtual void Use(const FElysiumEntityHandle& Activator) override { DoorUse(Activator); }
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual void OnParentAttached(const FTransform& ParentWorldTransform) override;
	virtual FElysiumDoorBase* AsDoorBase() override { return this; }

	// --- Lifecycle ---------------------------------------------------------------------
	virtual void Spawn() override;
	virtual void Think() override;

	// The door's derived state is exactly the case that carves out a leaf hook: `m_toggle_state`
	// and the lock are neither keyvalues nor registered fields, and a
	// rebuild from the def cannot re-derive them (it would put every door back at its spawn pose).
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

protected:
	// Leaf hook: compute the OPEN body-relative transform from the door's keyvalues + spawnflags.
	// Closed is always the spawn pose (identity/origin). Rotating doors rotate about the hinge;
	// sliding doors translate along `angles`.
	virtual void ComputeOpenTransform(FVector& OutOpenLoc, FRotator& OutOpenRot) const = 0;

	// Leaf hook: issue the primitive move toward Open/Closed at Speed (AngularMove vs LinearMove).
	// bResolveSwing carries retail CRotDoor::DoorGoUp's second arg: true resolves the activator-relative
	// swing (the rotating leaf), false forces the fixed-forward open pose. The sliding leaf ignores it.
	virtual void IssueMoveToOpen(bool bResolveSwing) = 0;
	virtual void IssueMoveToClosed() = 0;

	// Leaf hook: does this door resolve its endpoint from the body's ANGLES (a rotating door, retail
	// CRotDoor comparing m_vecAngle1/m_vecAngle2) or its ORIGIN (a sliding door, retail CBaseDoor
	// comparing m_vecPosition1/m_vecPosition2)? Read only by ResolveToggleStateFromTransform.
	virtual bool ResolvesEndpointFromRotation() const = 0;

	virtual void MoveDone() override;               // HitTop / HitBottom
	virtual void OnMoveBlocked(const FHitResult& Hit) override;

	// Slot 245, the closing volume `IsCloseBlocked` scans, world centimetres. `CBaseDoor 0x100f0a40`:
	// the collision box translated back along `-movedir` by the travel (the box at the closed
	// pose); `CRotDoor 0x100f2a00`: `ComputeSwingData` at the current angles (the closed leaf's box
	// unioned with the box of its two rotated corners, about the hinge).
	virtual FBox ComputeCloseBounds() const = 0;

	// `DoorHitTop 0x100f0860`'s call into the activator's NPC: `m_hActivator (+0x53c) -> +0x94
	// m_pBaseNPC -> OnDoorFullyOpen 0x1027dd10(door)`, and the engine's link release.
	void NotifyActivatorFullyOpen();

	// bResolveSwing (retail CRotDoor::DoorGoUp's second arg): the normal player/logic-initiated open
	// path passes true so the rotating leaf resolves its activator-relative swing; the block-reverse
	// reissue (OnMoveBlocked) passes false so the leaf re-opens fixed-forward, never activator-relative.
	void DoorGoUp(const FElysiumEntityHandle& Activator, bool bResolveSwing = true);
	void DoorGoDown(const FElysiumEntityHandle& Activator);

	// Test seam: the block-reverse open (DoorGoUp with bResolveSwing == false) is only reached in-engine
	// through OnMoveBlocked, which needs a live pawn blocker in the swept arc. A content-free automation
	// test drives that exact seam through this accessor. No production code references it.
	friend struct FElysiumDoorTestAccess;
	// 0018/7's door-link suite drives a bodiless door's arrival (`MoveDone`, the HitTop / HitBottom
	// arms) directly: a Substrate-tier door has no brush body, so its move never runs to the end.
	friend struct FElysiumNpcDoorLinkTestAccess;

	// The `DOOR_NORMAL` hearing stimulus, raised beside the audio one-shot at both motion starts.
	// One helper rather than two call sites so the silence rule cannot drift between open and close.
	void EmitDoorGameSound();

	// Resolve `LinkedDoorName` to the paired door leaf through the world name index (cached, no-RTTI
	// downcast via AsDoorBase). Null when unset or the partner is missing/not a door.
	FElysiumDoorBase* ResolveLinkedDoor();
	// Drop handles whose knob no longer resolves, then re-arm the use anchor. This never writes a
	// knob's lock state — the knob owns that.
	void PruneDoorknobs();
	void RefreshUseOwner();

	// True when the door rests open with no autoclose: `wait -1` or the NO_AUTO_RETURN (0x20) flag.
	bool StaysOpen() const;

	EToggleState ToggleState = EToggleState::AtBottom;

	// START_OPEN seats the body at the open pose on the first think — the brush body is built
	// *after* Spawn() (BuildBrushBody follows Spawn in the world's load pass), so the pose can't be
	// applied in Spawn() itself.
	bool bStartOpenSeatPending = false;

	// A restored door has to be re-seated at the pose its saved state implies, and the body
	// does not exist while the snapshot is being applied. Serialize arms this and forces an
	// immediate think; the seat pass snaps the pose and hands the saved think back.
	bool  bRestoreSeatPending = false;
	float RestoreResumeThink = ELYSIUM_NEVER_THINK;
	TArray<FElysiumEntityHandle> Doorknobs;
};

// Register the shared CBaseDoor input + field tables onto a descriptor (used by the CBaseDoor
// chain-node registration). Free function so the module-static registrars reference it without a
// static-init ordering hazard, mirroring BuildCBaseTrigger in ElysiumStarterClasses.cpp.
void ElysiumBuildCBaseDoor(struct FElysiumClassDesc& D);
