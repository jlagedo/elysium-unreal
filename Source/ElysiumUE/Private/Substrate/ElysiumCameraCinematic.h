#pragma once

#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "Math/RandomStream.h"   // FindBestShot's uniform pick draws from a named ElysiumRng stream
#include "Player/ElysiumCameraShots.h"

// `camera_cinematic` — VtMB's `CBaseCineCam` (`vampire.dll`, vtable `1044e67c`), the map entity that
// owns a scripted shot, and the runtime camera the same class stands in for.
//
// **Two entities, one class.** The map-placed `camera_cinematic` is the *director*: it holds the
// seven authored keys and never becomes the view. `StartShot` (`InputStartShot` `0x10070720` ->
// `FUN_10070780`) resolves the director's four `targetname` keys, then either creates a **second**
// `camera_cinematic` (`FUN_10070470`, marked *disposable*) or re-shots the one the player has
// already adopted, copies `m_bDrawPlayer` onto it, adopts it into the player's single cine slot
// (`FUN_1017cef0`) and immobilizes the player (`FUN_1015ef40`).
//
// **There is no `KeyValue` handler** (RG-A/RC2): the class overrides exactly nine vtable slots and
// the keyvalue path is not among them. All seven keys are plain datamap rows, `spawnflags` is parsed
// by the engine into `m_spawnflags`, and the only entity hook the class has is `Spawn` (slot 103),
// which is four instructions: `if (spawnflags & 2) m_bDrawPlayer = 1`.
//
// The plan of record is `docs/project/camera_scripted.md` §SC4; the recovery it rests on is
// `$ELYSIUM_WORK_ROOT/_camera_recovery/server_cine_camera.md` §1/§2/§6 and `rc_group_a.md`
// (RC2/RC3/RC4/RC12). The facts land in `docs/vtmb/camera-view-modes.md`.

class FElysiumEntityWorld;

// `CamMode` (`+0x638`, a 4-bit SendProp so the dispatcher's `default:` arm is reachable). The think
// dispatcher `FUN_1006e770` is a jump table on it; every arm is real and every arm is ported.
enum class EElysiumCineCamMode : uint8
{
	// `ThinkSet(NULL)` — a camera with no shot. `FUN_1006e0e0` (the mode clear) leaves the entity
	// here, and `SetShot` returning 0 leaves it here too.
	Idle = 0,
	// `0x1006f8f0`, the only mode shipped content reaches: the look-at solve, the origin selector,
	// `AutoPositionFromTarget`, the angle gate, the four-value publish and `point_player`. This is
	// what `FElysiumCameraShot::bTracked` means on the client side.
	NamedShot = 1,
	// `0x1006fde0` — an **empty function** on both server and client. The arm exists, publishes
	// nothing, and never expires; the client copies the last replicated pose through.
	OnRails = 2,
	// `0x1006fe00` — origin = anchor 0's world centre, angles = its abs angles. No target, no FOV.
	FollowEntity = 3,
	// `0x1006f870` — publishes only `m_flFOV`, which the client's mode-4 arm (`FUN_10002200`, a
	// confirmed `RET`) discards. Created by `CCameraAnimated::StartCamera`.
	Animated = 4,
};

namespace ElysiumCineCam
{
	// `_DAT_1044eb04 = 0.04165999963879585` — 1/24 s, read out of the image. Re-applied by
	// `FUN_1006f7d0`, `FUN_1006e770` and `CamEndThink` `FUN_1006e850`; no cvar or keyvalue touches
	// it. **The server cine camera publishes its goal at 24 Hz while the client tracker chases that
	// stepped goal every rendered frame** — that split is retail's server/client split and M2
	// (ruled 2026-09-07) reproduces it rather than collapsing it.
	inline constexpr float ThinkInterval = 0.04165999963879585f;

	// `spawnflags`, every bit pinned by RC2. The 51 shipped directors author 0, 1, 3, 5 and 7.
	//
	//   0x1 — `CCameraAnimated`'s "freeze the player" bit. **Nothing on `CBaseCineCam` reads it**:
	//         `FUN_10070780` immobilizes unconditionally. 50 of 51 directors author it and it
	//         changes nothing. Parsed and ignored, deliberately and by name.
	//   0x2 — draw the player's body. `CBaseCineCam::Spawn` `0x1006d9a0` is nothing but this test.
	//         **27 of 51 directors set it**, `sp_tutorial_1`'s `feedcamera` (`spawnflags 3`) among
	//         them, so `m_bDrawPlayer` has a shipped non-zero source in content, not only in
	//         anim event 4050.
	//   0x4 — *disposable*: the same `+0x204 & 0x4` bit the four runtime factories set. 12 shipped
	//         directors author it, and none of the twelve is ever sent `EndShot` (0 occurrences in
	//         any `.vpk` or `.ents`), so on shipped content the authored bit is inert.
	inline constexpr int32 SF_FreezePlayer = 0x1;
	inline constexpr int32 SF_DrawPlayer   = 0x2;
	inline constexpr int32 SF_Disposable   = 0x4;

	// `camera_showdebug` — the retail dev cvar registered in the same file as the entity
	// (`0x1006d5b0`, default `"0"`, no flags). Both of its readers — `CBaseCineCam`'s overlay slot
	// 123 and `CCameraAnimated`'s think — test it as `!IsCommand() && GetInt() == 1`, i.e. **exactly
	// one**, not truthiness. Declared into the VtMB console store beside `ElysiumCam::CvarDefs()`
	// (`ElysiumCommandBus::Console`), because it is a retail-named cvar.
	TArrayView<const ElysiumCam::FCvarDef> CvarDefs();

	// The `== 1` test itself, so the gate is asserted rather than re-spelled at each reader.
	inline bool ShowsDebug(int32 CvarValue) { return CvarValue == 1; }

	// `SetShot`'s name normalization, `FUN_1006e130`: *if the string contains `.txt`, run it through
	// `Q_FileBase` into a 32-byte buffer* (directory and extension stripped); anything without
	// `.txt` is passed verbatim, and the shot table's own lookup supplies case-insensitivity.
	// Load-bearing: `shotname` is authored as `vdata/CameraShots/X.txt` 43 times and bare 8 times
	// across the 51 shipped directors.
	FString NormalizeShotName(const FString& Name);
}

class FElysiumCameraCinematic : public FElysiumEntity
{
public:
	// --- The datamap, read out of `.rdata` rather than inferred (RC2, `vtmb_fields CBaseCineCam`) --

	FString ShotNameKey;    // +0x5d4 `m_sShotName`   key `shotname`   (51/51 authored)
	FString StartEntKey;    // +0x5d8 `m_sStartEnt`   key `startent`   (46/51)
	FString EndEntKey;      // +0x5dc `m_sEndEnt`     key `endent`     (49/51)
	FString Target1Key;     // +0x5e0 `m_sTarget1`    key `target1`    (51/51)
	FString Target2Key;     // +0x5e4 `m_sTarget2`    key `target2`    (42/51)

	// +0x5e8 `m_bForcePlayerLook`, key `point_player` (35/51 authored: 26 zero, 9 one).
	//
	// **The key is dead** (RG-A). The constructor `FUN_1006d620` seeds `m_bForcePlayerLook` to **1**,
	// `FUN_10070780` copies only `+0x640` onto the runtime camera, and a map-placed director is
	// never adopted, so its own byte is never read. Every director shot, every `SetCamera` shot,
	// every dialogue shot and every terminal shot therefore runs with the subject's gaze forced —
	// including the 26 that author `point_player 0`. Only `CFuncMonitor`, the `Intrusion` opener and
	// anim event 4050 clear it, on the camera they just created.
	//
	// It is parsed and stored so the content census can read it, and the runtime ignores it.
	// `docs/vtmb/retail-defects.md` §7 carries the row.
	bool bPointPlayerKey = false;

	// --- Runtime state, in retail's own offsets -------------------------------------------------

	// `CamMode` `+0x638`, held as an int because the dispatcher's `default:` (`> 4`) arm is a real
	// arm: it installs `CamEndThink` when the expiry has passed. `EElysiumCineCamMode` names 0..4.
	int32 CamMode = 0;

	// `m_ShotIndex` `+0x630`. The port holds the parsed record rather than a table index, so
	// "the index is -1" is "no shot is loaded".
	bool bShotLoaded = false;
	FElysiumCameraShotDef ShotDef;

	// `+0x610` / `+0x620` / `+0x598` — the four anchor handles, their bone/attachment indices and
	// the shot-start anchor cache `AttachType None` latches into.
	FElysiumShotBindings Bindings;

	// `m_hSubject` `+0x5d0`. `SetShot` falls back to player 1 when the caller passes nothing.
	FElysiumEntityHandle Subject;

	// `m_bDrawPlayer` `+0x640`. A `DT_BaseCineCam` SendProp with **no server reader**: written by
	// `Spawn` (from `spawnflags & 2`), copied director -> runtime camera by `FUN_10070780`, and
	// forced to 1 by anim event 4050 (SC8). SC5 consumes it as
	// `FElysiumShotPresentation::bDrawPlayerBody`.
	bool bDrawPlayerBody = false;

	// `m_bForcePlayerLook` `+0x5e8`, the RUNTIME value — **1 by construction**, see the keyvalue
	// above. Server-only (not in `DT_BaseCineCam`).
	bool bForcePlayerLook = true;

	// `+0x204 & 0x4`. Set by the four runtime factories and authorable through `spawnflags`.
	// `FUN_1017cef0` destroys the *outgoing* camera only when it carries this, which is why a
	// map-placed director survives its own `StartShot`; `FUN_10070990` removes the director itself
	// when it carries it.
	bool bDisposable = false;

	// `+0x594` — the **origin-source selector**, not a phase. `EndAnchor` (0) drives the origin from
	// anchor 1, `StartAnchor` (1) from anchor 0, `Entity` (2) leaves the entity's own abs origin
	// alone *and skips `AutoPositionFromTarget` entirely*. The constructor seeds **2**; the only
	// writer is the shot start, which sets 0 when the `End` handle is live, so every shipped shot
	// with an `End` anchor drives from `End`. The three arms live in
	// `FElysiumCameraDirector::Resolve` (SC7); this field is what selects between them.
	EElysiumShotOriginSelector OriginSelector = EElysiumShotOriginSelector::Entity;

	// `+0x55c` — the shot expiry, seeded `-1.0` (never). Only the mode-4 think arms it (with
	// `0.0`, i.e. "expire at exactly `curtime`") and the mode-3 think zeroes it, a value the
	// `> 0.0` gate rejects forever. **A mode-1 shot never expires on its own.**
	float ExpiryTime = -1.0f;

	// RC3 — `+0x564` / `+0x57c` is the **placement pose** the shot start moves the entity to and
	// publishes; `+0x570` / `+0x588` is a **one-deep memory of the previous placement**, saved on
	// every shot start after the first (the guard reads the live pose, which the constructor zeroes).
	// It is consumed by exactly one arm: an `End`-without-`Start` shot places at the saved pose when
	// there is one, and at the player's eye position with the player's abs angles when there is not.
	// Nothing in `vampire.dll` else touches the triple, and neither `SetShot` nor the mode clear
	// writes any of it — which is why a mid-conversation `SetCamera` (whose re-shot branch never
	// runs the shot start) leaves the entity where the *first* shot put it.
	FVector PlacementOrigin = FVector::ZeroVector;
	FRotator PlacementAngles = FRotator::ZeroRotator;
	FVector SavedPlacementOrigin = FVector::ZeroVector;
	FRotator SavedPlacementAngles = FRotator::ZeroRotator;
	bool bSavedPlacement = false;

	// The value-shot handle this camera publishes its goal onto. Retail replicates
	// `m_vecCamOrigin`/`m_vecCamTarget`/`m_angCamAngles`/`m_flFOV` off the entity and the client
	// pulls them; the port pushes them, which is the same channel with the arrow reversed.
	int32 PublishedShotId = 0;

	// The goal as last published, kept so a field the retail SendProp channel would have carried
	// live — `m_bDrawPlayer`, copied from the director AFTER the shot start — can reach the channel
	// on the same tick rather than at the next 24 Hz publish. See `RefreshPublishedGoal`.
	FElysiumCameraShot LastGoal;

	// The 24 Hz accumulator and the stamp it measures its delta against. `FElysiumEntityWorld::Tick`
	// hands an ABSOLUTE substrate second, so the entity keeps the last stamp and never reads a
	// clock (`.claude/rules/cpp.md`: DeltaTime arrives as a parameter).
	double LastThinkNow = -1.0;
	float ThinkAccumulator = 0.0f;

	// --- Lifecycle -----------------------------------------------------------------------------

	// Slot 103, and the ONLY virtual `CBaseCineCam` overrides that carries behaviour. `Precache`
	// (104), `Activate` (113) and `UpdateOnRemove` (180) are all inherited and empty for this class:
	// creation, adoption and teardown are free functions, never virtuals.
	virtual void Spawn() override;
	// **The tick's substrate second arrives as a parameter** and is threaded through the whole
	// think chain below (`.claude/rules/cpp.md`): the 24 Hz accumulator measures its delta between
	// successive stamps, and every arm that needs `curtime` — the expiry gate, the mode-3/4 expiry
	// arms, the next-think re-arm — reads the same one. Nothing under this entry reaches back
	// through the world for a clock.
	virtual void ThinkAt(double Now) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumCameraCinematic* AsCameraCinematic() override { return this; }

	// `ObjectCaps` (slot 117): the base's single `FCAP_ACROSS_TRANSITION` (`0x2`) is cleared, so
	// `CBaseCineCam::ObjectCaps() == 0` — **a live scripted shot does not cross a level transition**
	// and is not carried by a save. `FElysiumEntityWorld::Freeze` is the port's whole carry and
	// reads this, so the cap is wired rather than only recorded.
	virtual int32 ObjectCaps() const override { return 0; }

	// --- Inputs --------------------------------------------------------------------------------

	// `InputStartShot` `0x10070720` -> `FUN_10070780(director, player)`. The input **ignores its
	// `inputdata` entirely** — the activator is always `UTIL_PlayerByIndex(1)`.
	void InputStartShot();

	// `InputEndShot` `0x10070750` -> `FUN_10070990(director, player)`. No blend: the camera dies
	// the same tick and the client's next frame finds no override and falls back to the eye (M1).
	//
	// **No shipped map or script fires this** — 0 occurrences across every `.vpk` and every `.ents`
	// (RC2.2). Shipped shots are released by `pc.RemoveCamera()`, by dialogue end or by an
	// interaction closer, so this arm's acceptance is unit-test only.
	void InputEndShot();

	// --- Retail's free functions, as members ----------------------------------------------------

	// `FUN_1006e890` — `IsActive()`.
	bool IsActive() const { return CamMode != 0; }

	// `FUN_1006e0e0` — the mode clear: all four anchor handles and point indices to `-1`,
	// `m_ShotIndex = -1`, `CamMode = 0`. It **does not touch** the expiry, the origin selector,
	// `m_bForcePlayerLook` or `m_bDrawPlayer`.
	void ClearMode();

	// `SetShot` `FUN_1006e130(name, camMode, subject)`. Clears the mode first, normalizes the name,
	// looks it up, and **returns false with the mode still 0** when the name is unknown — which is
	// what leaves a re-shot camera idle rather than falling back.
	//
	// Its tail is two writes and nothing else:
	//
	//     uVar8 = engine->GetFrameCount();
	//     *(undefined4 *)((int)in_ECX + 0x63c) = uVar8;    // m_nClientResetFrame
	//     *(undefined4 *)((int)in_ECX + 0x638) = param_2;  // CamMode, last
	//     return 1;
	//
	// So `FUN_1006e8e0` is **not** the only reset-frame stamp site — and it is exactly the site
	// `CBasePlayer::SetCamera` `FUN_1017d020`'s re-shot arm skips. Every one of the 115 shipped
	// `pc.SetCamera("Shot")` calls therefore arms `m_bShotStartPending` on the client and runs
	// `FUN_10002210`, which for a `Start`-bearing shot re-seeds the pose **from the goal** — a cut.
	// There is no `ThinkSet` here (M4); the think is `FUN_1006e8e0`'s alone.
	bool SetShot(const FString& Name, int32 InCamMode, const FElysiumEntityHandle& InSubject);

	// `FUN_1006ef50(this, ent, i)` — bind anchor `Index` and resolve its inline `Bone:`/
	// `Attachment:` name to an index once. `bResolvePointIndex` false is the mode-3 factory's raw
	// handle store (`FUN_100705d0` writes `+0x610` directly and leaves `+0x620` at `-1`).
	void SetShotAnchorEntity(int32 Index, const FElysiumEntityHandle& Entity,
		bool bResolvePointIndex = true);

	// `FUN_1006e8e0` — the shot **start**, not a think. It is in no vtable and is never handed to
	// `ThinkSet`; all six call sites are "a shot has just been set". Saves the previous placement,
	// computes the new one, moves the entity, fills the shot-start anchor cache, sets the origin
	// selector when the `End` handle is live, and publishes the first goal.
	void StartShotPlacement();

	// `FUN_1006e8b0(this, secs)` — `+0x55c = curtime + secs`. `Now` is the tick's second, handed in.
	void SetExpiry(float Seconds, double Now);

	// `FUN_1006e770` — `SetCamThink`: the jump table on `CamMode`, which always ends by scheduling
	// the next think 1/24 s out (mode 0 schedules nothing).
	//
	// **`FUN_1006e130` does not call it.** The only caller is `FUN_1006e8e0` (`1006e8e9`, its very
	// first instruction), so a `SetShot` with no shot start behind it installs no think: retail's
	// `SetCamera` re-shot of a camera left idle by a failed `InputStartShot` sets `CamMode = 1` and
	// publishes nothing, ever. It also means the 24 Hz phase survives a re-shot, because nothing
	// re-arms `m_flNextThink` on that path.
	void SetCamThink(double Now);

	// `ShouldTransmit` (slot 86): the force-transmit window, else **only to the client whose player
	// is `m_hSubject`**, and only while `CamMode != 0`. The whole PVS/`EF_NODRAW` logic of the base
	// is replaced. One player here, so this is a fact recorded as state rather than a wire filter:
	// a shot whose subject is not the player would in retail be invisible to it.
	bool ShouldTransmit(const FElysiumEntityHandle& Recipient) const;

	// `DrawDebugGeometryOverlays` (slot 123), gated on `camera_showdebug == 1` exactly. Retail draws
	// the forward line **in modes 1 and 2 only**, from the replicated origin using the replicated
	// angles, four anchor boxes (Start blue, End green, both target points dark red) and a pulsing
	// red box at the look-at. The substrate has no debug-draw seam, so the body is the gate plus a
	// verbose log; the drawing itself belongs to whoever gains that seam.
	void DrawDebugGeometryOverlays(int32 ShowDebugCvarValue) const;

	// `FUN_10070470(name, e0..e3)` — create a disposable runtime `camera_cinematic` from a shot
	// name. A failed `SetShot` is the **only** failure path and it `UTIL_Remove`s the new entity.
	static FElysiumEntityHandle CreateRuntimeCamera(FElysiumEntityWorld& World, const FString& Name,
		int32 InCamMode, const FElysiumEntityHandle (&Anchors)[FElysiumShotBindings::Num]);

	// --- `FindBestShot` and its two predicates (SC8) ---------------------------------------------

	// `CBaseCineCam::FindBestShot` `FUN_1006e4c0(this, baseName)`.
	//
	//     CamMode = 1;                                    // DIRECTLY, before any SetShot
	//     for (i = 1; ; ++i) {
	//       Q_snprintf(buf, 0x40, "%s_%d", baseName, i);
	//       if (SetShot(buf, 1, NULL)) {
	//         FUN_1006e8e0(this);                          // place it, so the predicates can look
	//         if (FUN_1006d9d0() && FUN_1006db10()) vec.AddToTail(i);
	//       }
	//       if (m_ShotIndex == -1) break;                  // the first missing name ends the scan
	//     }
	//     if (!vec.Count()) return false;
	//     k = RandomInt(0, vec.Count()-1);
	//     SetShot("<base>_<vec[k]>", 1, NULL);
	//     Msg("CBaseCineCam::FindBestShot chose %s\n", buf);
	//
	// **There is no scoring** — the name is a misnomer for "any shot that fits, chosen at random" —
	// and it does **not** re-run the shot start after the final `SetShot`; the caller does. The draw
	// comes from `EElysiumRngStream::CameraFindBestShot`, never `FMath::Rand*`
	// (`.claude/rules/cpp.md`), which is what makes the pick assertable from a seeded stream.
	bool FindBestShot(const FString& BaseName, FRandomStream& Rng);

	// `FUN_1006d9d0` — "the shot's declared anchors exist". `flags == 0xffffffff` is false outright;
	// then each of `Start 0x1` / `End 0x2` / `Point1 0x4` / `Point2 0x8` **the shot declares** must
	// have a live handle at `+0x610 + i*4`; finally `m_ShotIndex != -1`. A shot that declares no
	// anchor at all passes trivially.
	bool AnchorsExist() const;

	// `FUN_1006db10` — "the camera can see its target". Solves the look-at (`FUN_1006f670`), then
	// for each **live** `Start` / `End` anchor traces a 2-unit hull from that anchor's resolved
	// world position to the look-at, ignoring the shot's subject, and fails on
	// `fraction < 1 || startsolid || allsolid`. Non-const because the port's anchor reader fills the
	// shot-start cache on a first resolve, exactly as `FUN_1006e8e0` does — which has already run by
	// the time `FindBestShot` asks.
	bool CanSeeTarget();

	// `FUN_10070550(baseName)` — create a `camera_cinematic`, flag it **disposable** (`+0x204 |= 4`),
	// `FindBestShot`, `UTIL_Remove` on failure and `FUN_1006e8e0` on success. Its only caller in the
	// whole image is `CBasePlayer::HandleAnimEvent` `0x10178a10`, anim event 4050.
	static FElysiumEntityHandle CreateFindBestShotCamera(FElysiumEntityWorld& World,
		const FString& BaseName, FRandomStream& Rng);

	// `FUN_10071970`'s per-entity body: the full `EndShot` when active, then removal. The map
	// teardown runs it over every `camera_cinematic` before it drops the adoption slot.
	void TeardownShot();

private:
	// One 1/24 s tick of the mode dispatcher's arm. Split out so the accumulator above and the
	// think body below are separately assertable.
	void RunCamThink(double Now);

	// `FUN_1006f7d0` — the prologue every cine think runs first: schedule the next think 1/24 s out,
	// then hand off to `CamEndThink` when the expiry is armed **and strictly past**. Returns true
	// when the update must abort.
	bool ThinkPrologue(double Now);

	// `m_nClientResetFrame` `+0x63c`: `engine->GetFrameCount()`, stamped by **both**
	// `FUN_1006e130` (its tail, ahead of `CamMode`) and `FUN_1006e8e0` (`1006ebaf`). The port's
	// equivalent is the camera component's shot-start edge, which the embodiment re-arms on the
	// published shot handle, so an un-adopted camera has nothing to stamp and says so.
	void RestampClientResetFrame();

	// The mode-1 body `0x1006f8f0`, in retail's verbatim order: anchor cache -> look-at ->
	// the `+0x594` origin selector -> `AutoPositionFromTarget` -> the `+0xd4` angle gate ->
	// publish origin / target / angles / FOV -> `point_player`.
	void ThinkNamedShot(double Now);
	void ThinkFollowEntity(double Now);
	void ThinkAnimated(double Now);

	// The three fields every arm stamps onto the goal before it is published: the channel
	// (`bCine`), retail's `CamMode == 1` as `bTracked`, and `m_bDrawPlayer`.
	void DecorateGoal(FElysiumCameraShot& Shot) const;

	// Push or update the value shot that carries this camera's goal.
	void PublishGoal(const FElysiumCameraShot& Shot);

	// Drop it. Retail's equivalent is `ShouldTransmit` refusing an idle camera to every client, so
	// nothing has to be destroyed for the view to fall back.
	void ReleaseGoal();

public:
	// Re-stamp the published goal from the entity's own fields without re-solving it.
	//
	// Retail replicates `m_bDrawPlayer` off the entity, so `FUN_10070780` step 6 — which copies the
	// director's byte onto the runtime camera **after** `FUN_1006e8e0` has already published the
	// pose — still reaches the client inside the same tick. The port bakes the value into the shot
	// it pushes, so the copy has to be re-published or the first frame of every draw-the-body shot
	// would render without one.
	void RefreshPublishedGoal();

private:

	// `if (m_bForcePlayerLook && handleLive(m_hSubject)) FUN_10178590(subject, m_vecCamTarget)` —
	// it turns the **subject**, never the camera, and it fires every tick of the shot.
	void ApplyForcePlayerLook(const FVector& LookAt);
};
