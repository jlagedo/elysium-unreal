#include "Substrate/ElysiumCameraCinematic.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCineCam, Log, All);

// --- The cvar and the name normalization ------------------------------------------------------

TArrayView<const ElysiumCam::FCvarDef> ElysiumCineCam::CvarDefs()
{
	// `ConVar(&cvar_camera_showdebug, "camera_showdebug", "0", 0)` at `vampire.dll 0x1006d5b0`:
	// default `"0"`, no flags. It is a retail name, so it is declared into the **VtMB console
	// store** rather than as an `elysium.*` cvar (`port_harness_map.md` §10).
	static const ElysiumCam::FCvarDef Defs[] =
	{
		{ TEXT("camera_showdebug"), TEXT("0"),
		  TEXT("dev: draw the cine camera's anchors and look-at; tested == 1, not truthiness") },
	};
	return MakeArrayView(Defs);
}

FString ElysiumCineCam::NormalizeShotName(const FString& Name)
{
	// `if (_strstr(name, ".txt")) { Q_FileBase(name, buf, 0x20); name = buf; }` — a **substring**
	// test, not a suffix one, and the extension check is case-sensitive in retail's `_strstr`... but
	// the shipped corpus writes both `.txt` and `.TXT` (`CameraShots\Jack.TXT`), and `Q_FileBase`
	// strips the directory and the *last* extension either way. Folding the case of the probe is the
	// one leniency here and it only widens which spellings normalize, never which shot is chosen.
	if (Name.Contains(TEXT(".txt"), ESearchCase::IgnoreCase))
	{
		// `Q_FileBase`: drop everything up to the last separator, then the extension. VtMB authors
		// backslashes (`vdata\CameraShots\x.txt`) and the exports carry forward slashes.
		FString Base = Name;
		Base.ReplaceInline(TEXT("\\"), TEXT("/"));
		int32 Slash = INDEX_NONE;
		if (Base.FindLastChar(TEXT('/'), Slash))
		{
			Base = Base.Mid(Slash + 1);
		}
		int32 Dot = INDEX_NONE;
		if (Base.FindLastChar(TEXT('.'), Dot))
		{
			Base = Base.Left(Dot);
		}
		// Retail's buffer is 32 bytes including the NUL.
		return Base.Left(31);
	}
	return Name;
}

// The module builds with unity on, so the file's helpers take a named namespace (the project's
// convention -- see `ElysiumCameraShotsImpl`).
namespace ElysiumCineCamImpl
{
	// Retail's shot **table** (`&DAT_106c8298`) is keyed by shot name across every parsed file, so
	// `SetShot("Animated", ...)` finds a block that lives inside `special-case.txt`. The port parses
	// per file, so the lookup is "the file of that name first, then the one multi-shot file the
	// corpus has". `vdata/camerashots/` is 65 shots in 66 files and only `special-case.txt` carries
	// siblings (`DeathCam`, `Follow`, `Animated`, `Intrusion`, `Hacking`), so the two probes are the
	// whole table for the shipped corpus.
	const FElysiumCameraShotDef* FindShotByName(const FString& NormalizedName)
	{
		if (NormalizedName.IsEmpty())
		{
			return nullptr;
		}
		if (const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(NormalizedName))
		{
			return Def;
		}
		return ElysiumCameraShots::LoadNamed(TEXT("special-case"), NormalizedName);
	}

	// The player's `GetAbsAngles()` — the body's facing, which is yaw only in this runtime
	// (`ElysiumSkeletalBasis::FromSourceAngles`), not the view rotation.
	FRotator AbsAnglesOf(const FElysiumEntity& Entity)
	{
		return ElysiumSkeletalBasis::FromSourceAngles(Entity.Angles);
	}

	// `Q_snprintf(buf, 0x40, "%s_%d", baseName, i)` — `FindBestShot`'s candidate name, in retail's
	// own 64-byte buffer: 63 characters plus the NUL. No shipped 4050 `options` string comes near
	// it, but the truncated name is what the shot table is asked for.
	FString ShotNameFor(const FString& BaseName, int32 Index)
	{
		return FString::Printf(TEXT("%s_%d"), *BaseName, Index).Left(63);
	}

	// `FUN_1006f670`, the look-at solve, on its own — `FindBestShot`'s visibility predicate
	// (`FUN_1006db10`) calls it directly, ahead of and independently of the `+0xd4` angle gate that
	// `FElysiumCameraDirector::Resolve` applies. It reads the record's **presence flags**, not its
	// slots, so the order-of-presence flag bug travels with it: a `Point2`-only shot raises the
	// `Point1` bit and this reads the empty slot 2, aiming the test at `(0,0,0)`.
	FVector SolveLookAt(FElysiumEntityWorld* World, const FElysiumCameraShotDef& Def,
		FElysiumShotBindings& Bindings, bool bLatched)
	{
		// An anchor that does not resolve leaves the zero there, which is what `FUN_1006f080`'s
		// `vec3_origin` early-out writes.
		FVector Point1 = FVector::ZeroVector;
		FVector Point2 = FVector::ZeroVector;
		FElysiumCameraDirector::ResolveAnchorPoint(World, Def, 2, Bindings, bLatched, Point1);
		FElysiumCameraDirector::ResolveAnchorPoint(World, Def, 3, Bindings, bLatched, Point2);
		if (Def.bTargetPoint1Flagged && Def.bTargetPoint2Flagged)
		{
			return (Point1 + Point2) * 0.5f;
		}
		if (Def.bTargetPoint1Flagged)
		{
			return Point1;
		}
		if (Def.bTargetPoint2Flagged)
		{
			return Point2;
		}
		return FVector::ZeroVector;
	}
}

// --- Lifecycle ---------------------------------------------------------------------------------

void FElysiumCameraCinematic::Spawn()
{
	// `CBaseCineCam::Spawn` `0x1006d9a0`, in full — four instructions:
	//
	//     TEST byte ptr [ECX + 0x204],0x2
	//     JZ   ret
	//     MOV  byte ptr [ECX + 0x640],0x1
	//     RET
	//
	// It does not chain to the base, sets no model, precaches nothing and installs no think. Bit
	// `0x1` is `CCameraAnimated`'s freeze-player bit and is **dead on this class** — 50 of the 51
	// shipped directors author it and `StartShot` immobilizes unconditionally — and bit `0x4` is the
	// disposable flag the runtime factories also set.
	if ((SpawnFlags & ElysiumCineCam::SF_DrawPlayer) != 0)
	{
		bDrawPlayerBody = true;
	}
	if ((SpawnFlags & ElysiumCineCam::SF_Disposable) != 0)
	{
		bDisposable = true;
	}
	// `StartHidden` is `CBaseEntity::m_bStartHidden` (+0x0e0), not a camera key: five directors
	// author it and it does nothing on an invisible point entity. The base chain already applied it.
}

void FElysiumCameraCinematic::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	static const TCHAR* const ModeNames[] =
		{ TEXT("Idle"), TEXT("NamedShot"), TEXT("OnRails"), TEXT("FollowEntity"), TEXT("Animated") };
	Out.Emplace(TEXT("CamMode"), CamMode >= 0 && CamMode <= 4
		? FString::Printf(TEXT("%d (%s)"), CamMode, ModeNames[CamMode])
		: FString::Printf(TEXT("%d (expiring)"), CamMode));
	Out.Emplace(TEXT("Shot"), bShotLoaded ? ShotDef.Name : TEXT("<none>"));
	Out.Emplace(TEXT("Disposable"), bDisposable ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("DrawPlayer"), bDrawPlayerBody ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("ForcePlayerLook"), bForcePlayerLook ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("OriginSelector"), FString::FromInt(static_cast<int32>(OriginSelector)));
	Out.Emplace(TEXT("Expiry"), FString::Printf(TEXT("%.3f"), ExpiryTime));
	Out.Emplace(TEXT("Placement"), FString::Printf(TEXT("%s / %s"),
		*PlacementOrigin.ToCompactString(), *PlacementAngles.ToCompactString()));
	Out.Emplace(TEXT("SavedPlacement"), bSavedPlacement
		? FString::Printf(TEXT("%s / %s"), *SavedPlacementOrigin.ToCompactString(),
			*SavedPlacementAngles.ToCompactString())
		: TEXT("<none>"));
	Out.Emplace(TEXT("PublishedShot"), FString::FromInt(PublishedShotId));
}

// --- The mode, the shot and the anchors ---------------------------------------------------------

void FElysiumCameraCinematic::ClearMode()
{
	// `FUN_1006e0e0`: `for i in 0..3 { +0x610[i] = -1; +0x620[i] = -1; }`, then the shot index to
	// `-1` and the mode to 0. It **does not touch** `+0x55c` (expiry), `+0x594` (origin selector),
	// `+0x5e8` (`m_bForcePlayerLook`) or `+0x640` (`m_bDrawPlayer`) — and it does not touch the
	// shot-start anchor cache at `+0x598` either, which is what makes RC3's ordering trap visible:
	// a re-shot's placement can read the *previous* shot's cached anchor.
	for (FElysiumShotAnchorBinding& Binding : Bindings.Anchors)
	{
		Binding.Entity = FElysiumEntityHandle::Invalid();
		Binding.PointIndex = INDEX_NONE;
	}
	bShotLoaded = false;
	ShotDef = FElysiumCameraShotDef();
	CamMode = 0;
}

bool FElysiumCameraCinematic::SetShot(const FString& Name, int32 InCamMode,
	const FElysiumEntityHandle& InSubject)
{
	// `FUN_1006e130`, in its own order. The mode is cleared FIRST, so a failed lookup leaves an
	// idle camera rather than the previous shot.
	ClearMode();

	const FElysiumCameraShotDef* Found =
		ElysiumCineCamImpl::FindShotByName(ElysiumCineCam::NormalizeShotName(Name));
	if (!Found || !Found->IsValid())
	{
		// `this->m_ShotIndex = -1; return 0;` — **the mode stays 0**. `FUN_10070470` treats this as
		// its one failure path and removes the camera; `FUN_10070780`'s re-shot branch ignores the
		// return value entirely and leaves the camera idle (1.6).
		return false;
	}
	ShotDef = *Found;
	bShotLoaded = true;

	// `subject = param_3 ? param_3 : UTIL_PlayerByIndex(1); if (!subject) return 0;`
	FElysiumEntityHandle Resolved = InSubject;
	if (!Resolved.IsSet())
	{
		const FElysiumPlayer* PlayerEnt = World ? World->FindPlayer() : nullptr;
		Resolved = PlayerEnt ? PlayerEnt->Handle : FElysiumEntityHandle::Invalid();
	}
	if (!Resolved.IsSet())
	{
		ClearMode();
		return false;
	}
	Subject = Resolved;

	// The `Position` resolve loop: each anchor's keyword to an entity, then `SetShotAnchorEntity`.
	// `Named` answers NULL here and the caller fills the slot.
	FElysiumCameraDirector::BindAnchors(World, ShotDef, Subject, Bindings);

	// `m_nClientResetFrame` is stamped before the mode; the port's reset edge is the shot-change
	// edge the camera component already latches off the top shot's id, so there is no separate
	// frame counter to carry.
	//
	// **`CamMode` is written last**, which is what makes the whole update atomic for a reader that
	// only tests `IsActive()`.
	CamMode = InCamMode;
	SetCamThink();
	return true;
}

void FElysiumCameraCinematic::SetShotAnchorEntity(int32 Index, const FElysiumEntityHandle& Entity,
	bool bResolvePointIndex)
{
	if (Index < 0 || Index >= FElysiumShotBindings::Num)
	{
		return;
	}
	if (!bResolvePointIndex)
	{
		// `FUN_100705d0` (the mode-3 factory) stores the handle **raw** and leaves `+0x620` at `-1`,
		// which is consistent with the shot it names: `special-case.txt`'s `Follow` is
		// `AttachPos Center`, and `Center` needs no bone or attachment index.
		Bindings.Anchors[Index].Entity = Entity;
		Bindings.Anchors[Index].PointIndex = INDEX_NONE;
		Bindings.Anchors[Index].bCached = false;
		return;
	}
	FElysiumCameraDirector::BindAnchorEntity(World, ShotDef, Index, Entity, Bindings);
}

// --- The shot start `FUN_1006e8e0` --------------------------------------------------------------

void FElysiumCameraCinematic::StartShotPlacement()
{
	// The listing's own order. `SetCamThink` runs FIRST, before the placement, which is the ordering
	// trap RC3 exposes: the `Start` arm below resolves anchor 0 through the cache-aware reader
	// **before** the cache at `+0x598` is refilled at the bottom of the same function, so a `Start`
	// block with `AttachType None` places the camera from the *previous* shot's cached anchor.
	// `special-case.txt`'s `Follow` is the only shipped shot that can see it.
	SetCamThink();

	// The one-deep previous-placement memory. The guard reads the LIVE placement, not the saved
	// slot, so the first shot on a fresh entity (`+0x564 == vec3_origin` from the constructor)
	// saves nothing and every shot after it does.
	if (!PlacementOrigin.IsNearlyZero())
	{
		SavedPlacementOrigin = PlacementOrigin;
		SavedPlacementAngles = PlacementAngles;
		bSavedPlacement = true;
	}

	const FElysiumPlayer* PlayerEnt = World ? World->FindPlayer() : nullptr;

	const bool bLatched = ElysiumCameraShots::LatchesAnchors(ShotDef);

	if (bShotLoaded && ShotDef.Start.bPresent)
	{
		// Arm A — `flags & 1`: the placement is anchor 0's position and the angles look from there
		// at the shot's look-at point (`VectorAngles(lookAt - anchor0)`).
		FElysiumCameraShot Probe;
		const bool bProbed = FElysiumCameraDirector::Resolve(World, ShotDef, Subject, Probe,
			&Bindings, EElysiumShotResolvePass::Think, OriginSelector);
		FVector StartPoint;
		if (FElysiumCameraDirector::ResolveAnchorPoint(World, ShotDef, 0, Bindings, bLatched,
			StartPoint))
		{
			PlacementOrigin = StartPoint;
		}
		if (bProbed && Probe.bUseLookAt)
		{
			PlacementAngles = (Probe.LookAt - PlacementOrigin).Rotation();
		}
	}
	else if (bShotLoaded && ShotDef.End.bPresent)
	{
		// Arm B — `flags & 2 && !(flags & 1)`: **an `End`-only shot continues from where the last
		// shot left this camera entity** instead of snapping back to the player's eye. With nothing
		// saved yet it places at the player's eye position with the player's abs angles (RC3).
		if (bSavedPlacement)
		{
			PlacementOrigin = SavedPlacementOrigin;
			PlacementAngles = SavedPlacementAngles;
		}
		else if (PlayerEnt)
		{
			PlacementOrigin = PlayerEnt->EyePosition();
			PlacementAngles = ElysiumCineCamImpl::AbsAnglesOf(*PlayerEnt);
		}
	}
	// Arm C — neither block: the placement keeps its previous values and is re-published unchanged.

	// `SetAbsOrigin` / `SetOrigin` / `SetAngles` / `SetAbsAngles` / `Relink`. Only the origin is
	// written back onto the entity: the port's `FElysiumEntity::Angles` is the authored SOURCE
	// triple (yaw-only once converted) while the placement angles are a solved world-space look
	// direction, and nothing in the port reads a cine camera's `Angles` — the pose the view uses is
	// the published goal below.
	SetRuntimeOrigin(PlacementOrigin);

	// `+0x594 = 0` and `+0x5c8 = 0` when the **End** handle is live: a shot with an `End` anchor
	// drives its origin from `End`. Otherwise the selector keeps whatever the entity was
	// constructed with, which is `2` — "leave the abs origin alone, and skip
	// `AutoPositionFromTarget`".
	if (Bindings.Anchors[1].Entity.IsSet())
	{
		OriginSelector = EElysiumShotOriginSelector::EndAnchor;
	}

	// The shot-start anchor cache at `+0x598 + i*12`, refilled last, and the publish. `Resolve`'s
	// `ShotStart` pass is exactly that fill: it resolves every anchor live and writes the cache.
	// It cannot fail: an anchor retail cannot resolve answers `vec3_origin` (`0x1006f09b`), so there
	// is no "the shot did not anchor" state to fall back from.
	FElysiumCameraShot Shot;
	FElysiumCameraDirector::Resolve(World, ShotDef, Subject, Shot, &Bindings,
		EElysiumShotResolvePass::ShotStart, OriginSelector);
	Shot.DebugName = ShotDef.Name;
	// **The shot start publishes the PLACEMENT, not the anchor.** `1006ebcb`/`1006ebe9` write
	// `m_vecCamOrigin = +0x564` and `m_angCamAngles = +0x57c` — the pose the entity was just moved
	// to — and only the 24 Hz think that follows drives the origin from the anchor. That is what
	// makes an `End`-only shot dolly *out of the player's eye* toward its `End` point rather than
	// starting on it, and it is the server half of the client's own shot-start seed.
	Shot.Origin = PlacementOrigin;
	Shot.Rotation = PlacementAngles;
	DecorateGoal(Shot);

	// `1006ebaf`: `m_nClientResetFrame = engine->GetFrameCount()`, **every shot start** — including
	// a re-shot of the camera the player has already adopted, whose id never changes. The first
	// adoption's stamp rides the push below; a re-shot needs the explicit re-stamp, and the 24 Hz
	// publish must not do it (that would re-seed the tracker every tick).
	if (PublishedShotId != 0)
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->RestartCameraShot(PublishedShotId);
		}
	}
	PublishGoal(Shot);
}

void FElysiumCameraCinematic::SetExpiry(float Seconds)
{
	// `FUN_1006e8b0`: `+0x55c = curtime + secs`.
	ExpiryTime = static_cast<float>((World ? World->NowSeconds() : 0.0)) + Seconds;
}

void FElysiumCameraCinematic::SetCamThink()
{
	// `FUN_1006e770`: mode 0 installs no think at all; every other arm ends with
	// `m_flNextThink = curtime + 1/24`. The port's think queue is deadline-driven, so the entity
	// asks to be woken now and gates the publish on its own accumulator inside `Think()` — which is
	// what lets the 24 Hz cadence be measured against the caller's delta instead of a clock read.
	const double Now = World ? World->NowSeconds() : 0.0;
	NextThink = CamMode == 0 ? ELYSIUM_NEVER_THINK : static_cast<float>(Now);
	LastThinkNow = -1.0;
	ThinkAccumulator = 0.0f;
}

bool FElysiumCameraCinematic::ShouldTransmit(const FElysiumEntityHandle& Recipient) const
{
	// Slot 86, `0x1006e6a0`. The base's force-transmit window (`+0x90 > curtime`) has no port
	// counterpart — its producer is unrecovered (RG-A "what remains unrecovered") — so the recorded
	// behaviour is the rest: **only to the subject's client, and only while `CamMode != 0`.**
	return IsActive() && Recipient.IsSet() && Recipient == Subject;
}

void FElysiumCameraCinematic::DrawDebugGeometryOverlays(int32 ShowDebugCvarValue) const
{
	// `CBaseCineCam::vfunc123` `0x1006ff40`. The gate is `!IsCommand() && GetInt() == 1` — exactly
	// one, never truthiness.
	if (!ElysiumCineCam::ShowsDebug(ShowDebugCvarValue))
	{
		return;
	}
	// Retail draws, in this order: the forward line **only in modes 1 and 2**, 20 units long, from
	// the REPLICATED origin using the REPLICATED angles (so in mode 1 it shows the server's
	// `VectorAngles(lookAt - shotStartOrigin)` rather than what the client renders); a 2-unit box at
	// the same origin; one 2-unit box per live anchor (Start blue, End green, both target points
	// dark red); a 3-unit pulsing red box at the look-at; and the look-at printed with retail's own
	// unbalanced `"(%.1f, %.1f, %.1f"` format string.
	//
	// The substrate has no debug-draw seam (`ElysiumWorldServices` carries no line/box primitive),
	// so the hook is the gate and the diagnostic; the geometry lands when that seam exists.
	UE_LOG(LogElysiumCineCam, Verbose,
		TEXT("%s: camera_showdebug 1 — shot '%s', mode %d, origin %s"),
		*DebugString(), *ShotDef.Name, CamMode, *PlacementOrigin.ToCompactString());
}

// --- The 24 Hz think ---------------------------------------------------------------------------

void FElysiumCameraCinematic::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;
	if (CamMode == 0)
	{
		// `ThinkSet(NULL)`.
		NextThink = ELYSIUM_NEVER_THINK;
		LastThinkNow = -1.0;
		return;
	}
	// Ask to be woken on the next world tick; the cadence is the accumulator's, not the queue's.
	NextThink = static_cast<float>(Now);

	// `FElysiumEntityWorld::Tick(double Now)` hands an ABSOLUTE substrate second, so the delta is
	// measured here, between successive stamps, and no clock is read
	// (`.claude/rules/cpp.md` — DeltaTime arrives as a parameter).
	const float Delta = LastThinkNow >= 0.0
		? static_cast<float>(FMath::Max(0.0, Now - LastThinkNow))
		: 0.0f;
	LastThinkNow = Now;
	ThinkAccumulator += Delta;

	// The FIRST think after a shot start runs immediately (retail schedules `curtime + 1/24` from
	// the shot start and the first think then fires on that deadline; the port's shot start already
	// published, so the first accumulated tick is the second goal either way).
	if (ThinkAccumulator + KINDA_SMALL_NUMBER < ElysiumCineCam::ThinkInterval)
	{
		return;
	}
	// **Exactly one publish per due tick, with the remainder carried.** A 0.5 s frame publishes
	// once and keeps 0.458 s, so the goal advances at 24 Hz and never bursts — which is what retail
	// does through `m_flNextThink`, one think per frame at most.
	ThinkAccumulator -= ElysiumCineCam::ThinkInterval;
	RunCamThink();
}

bool FElysiumCameraCinematic::ThinkPrologue(double Now)
{
	// `FUN_1006f7d0`. `m_flNextThink` is re-armed by `Think` above; what is left is the expiry
	// hand-off, whose test is **strict on both sides**: `+0x55c > 0.0f && +0x55c < curtime`. The
	// constructor seeds `-1.0`, so a fresh camera never expires; the mode-3 arm stores an integer
	// `0`, which the `> 0.0` test rejects forever.
	if (ExpiryTime > 0.0f && ExpiryTime < static_cast<float>(Now))
	{
		// `CamEndThink` `FUN_1006e850`: `UTIL_Remove(this)` and keep thinking until the removal
		// lands.
		Kill();
		return true;
	}
	return false;
}

void FElysiumCameraCinematic::RunCamThink()
{
	// `FUN_1006e770`'s jump table, every arm.
	switch (CamMode)
	{
	case static_cast<int32>(EElysiumCineCamMode::NamedShot):
		ThinkNamedShot();
		return;
	case static_cast<int32>(EElysiumCineCamMode::OnRails):
		// `0x1006fde0` is an **empty function** on the server and `FUN_10002200` is a confirmed
		// `RET` on the client. Mode 2 publishes nothing, never expires, and the client copies the
		// last replicated pose through. It is a real arm of the jump table and it is ported as the
		// nothing it is — including running the prologue, which is what would expire it if anything
		// ever armed its expiry.
		ThinkPrologue(World ? World->NowSeconds() : 0.0);
		return;
	case static_cast<int32>(EElysiumCineCamMode::FollowEntity):
		ThinkFollowEntity();
		return;
	case static_cast<int32>(EElysiumCineCamMode::Animated):
		ThinkAnimated();
		return;
	default:
		// `default:` (`CamMode > 4`) — reachable because `CamMode` replicates in 4 bits. It installs
		// `CamEndThink` **only** when the expiry is armed and past, and otherwise installs no think
		// at all while still re-arming `m_flNextThink`.
		ThinkPrologue(World ? World->NowSeconds() : 0.0);
		return;
	}
}

void FElysiumCameraCinematic::ThinkNamedShot()
{
	const double Now = World ? World->NowSeconds() : 0.0;
	if (ThinkPrologue(Now))
	{
		return;
	}
	if (!bShotLoaded)
	{
		return;
	}

	// The body of `0x1006f8f0`, in its own order (M2 keeps it verbatim):
	//
	//   1. the anchor cache / live re-resolve per `AttachType` (`FUN_1006f010`),
	//   2. the look-at solve (`FUN_1006f670`),
	//   3. the `+0x594` origin selector,
	//   4. `AutoPositionFromTarget` when `flags & 0x20`,
	//   5. the `+0xd4` angle gate,
	//   6. publish origin / target / angles / FOV,
	//   7. `point_player`.
	//
	// Steps 1, 2, 4 and 5 — and arms 0/1 of step 3 — are `FElysiumCameraDirector::Resolve`'s, which
	// is the one solver both the dialogue ladder and this entity share. Arm 2 of the selector is
	// applied here, because it is the arm that says "do not touch the origin at all".
	FElysiumCameraShot Shot;
	FElysiumCameraDirector::Resolve(World, ShotDef, Subject, Shot,
		&Bindings, EElysiumShotResolvePass::Think, OriginSelector);
	if (OriginSelector == EElysiumShotOriginSelector::Entity)
	{
		// `sel == 2` — "leave the entity's own abs origin alone". `Resolve` has already suppressed
		// `AutoPositionFromTarget` for this arm; the abs-origin *source* is this entity's transform,
		// which is the pose the shot start put there (the mode-1 think never calls `SetAbsOrigin`).
		Shot.Origin = PlacementOrigin;
	}

	DecorateGoal(Shot);
	PublishGoal(Shot);

	// Step 7, last and every tick.
	ApplyForcePlayerLook(Shot.bUseLookAt ? Shot.LookAt : Shot.Origin);
}

void FElysiumCameraCinematic::ThinkFollowEntity()
{
	// `FUN_1006fe00`, verbatim.
	//
	//   if (!handleLive(+0x610)) this->+0x55c = 0;     // an INTEGER zero, not SetExpiry
	//   if (FUN_1006f7d0(this)) return;
	//   ent = resolve(+0x610);                          // NO null guard on this second resolve
	//   m_vecCamOrigin = ent->WorldSpaceCenter();
	//   m_angCamAngles = ent->GetAbsAngles();
	//
	// It publishes **origin and angles only** — never the target, never the FOV, never `+0x5cc` —
	// so the rendered FOV stays at whatever `m_flFOV` last held (the constructor's 75.0, since no
	// mode-3 path ever writes it).
	FElysiumEntity* Anchor = World ? World->Resolve(Bindings.Anchors[0].Entity) : nullptr;
	if (!Anchor)
	{
		// The integer `0` the `> 0.0f` gate rejects forever: **losing the anchor does not expire the
		// shot**, and retail then dereferences the dead handle and faults the server.
		ExpiryTime = 0.0f;
	}
	if (ThinkPrologue(World ? World->NowSeconds() : 0.0))
	{
		return;
	}
	if (!Anchor)
	{
		// **Named modernization (RC12): crash -> no-op.** Retail's second resolve tests only the
		// index and serial, yields NULL for `0xffffffff`, and calls through address `0x300`. The
		// only thing removed is the fault: the "does not expire" behaviour is kept, so a mode-3
		// camera whose anchor dies freezes on its last published pose — which is exactly what retail
		// renders for the one tick before it goes down. `docs/vtmb/retail-defects.md` §7.
		return;
	}

	FElysiumCameraShot Shot;
	Shot.DebugName = ShotDef.Name;
	// `WorldSpaceCenter()` (vfunc 0x300) — **the same box the anchor resolver measures**
	// (`ElysiumCameraShots::SurroundingBounds`): the skeletal body's bounds, else the embodiment's
	// use-anchor box, else VtMB's standing hull on the entity's origin. This think used to read the
	// use-body box alone and fall back to the raw origin, so a mode-3 camera on a bodied NPC sat a
	// hull-height below where a `Center` anchor on the same entity put it, and answered the feet of
	// a body the anchor arm measured whole.
	Shot.Origin = ElysiumCameraShots::SurroundingBounds(*Anchor).GetCenter();
	Shot.bUseLookAt = false;
	Shot.Rotation = ElysiumCineCamImpl::AbsAnglesOf(*Anchor);
	// No target and no FOV: `FieldOfView` 0 is the port's "keep the player's", which is the closest
	// expression of "the last value `m_flFOV` held", and the constructor's 75 is the shot record's
	// own default anyway.
	Shot.FieldOfView = 0.0f;
	DecorateGoal(Shot);
	PublishGoal(Shot);
}

void FElysiumCameraCinematic::ThinkAnimated()
{
	// `FUN_1006f870`, verbatim:
	//
	//   if (!handleLive(+0x610)) FUN_1006e8b0(this, 0.0);   // expire at EXACTLY curtime
	//   if (FUN_1006f7d0(this)) return;
	//   m_flFOV = shotRecord->FieldOfView;
	//
	// `SetExpiry(0.0)` stores `curtime` and the gate in the same call is `< curtime`, which is
	// false at equality — so the camera survives the tick that armed it. **And every tick after
	// it**: the arm is unguarded, so a still-dead anchor re-stamps the expiry to the new `curtime`
	// before the gate reads it and the equality holds again. RG-A's "removed one think later" is an
	// inference the listing does not support; mode 4 reaches the same end state mode 3 does — a
	// dead anchor freezes the shot rather than ending it — by a different mechanism. Reproduced as
	// written.
	const FElysiumEntity* Anchor = World ? World->Resolve(Bindings.Anchors[0].Entity) : nullptr;
	if (!Anchor)
	{
		SetExpiry(0.0f);
	}
	if (ThinkPrologue(World ? World->NowSeconds() : 0.0))
	{
		return;
	}
	// **Only the FOV, and the client throws it away.** `C_BaseCineCamera::Update` takes
	// `if (m_CamMode == 4) { FUN_10002200(dt); return; }` and `FUN_10002200` is a confirmed empty
	// `RET`; the copy-through that would move `m_flFOV` into `m_flCurFov` lives in the fall-through
	// arm mode 4 never reaches. So a retail mode-4 camera renders its pose and FOV frozen at shot
	// start and this whole per-tick publish is dead. Reproduced as written; the defect is recorded.
	if (PublishedShotId == 0 || !bShotLoaded)
	{
		return;
	}
	FElysiumCameraShot Shot;
	Shot.DebugName = ShotDef.Name;
	Shot.Origin = PlacementOrigin;
	Shot.bUseLookAt = false;
	Shot.Rotation = PlacementAngles;
	Shot.FieldOfView = ShotDef.Constraints.FieldOfView;
	DecorateGoal(Shot);
	PublishGoal(Shot);
}

void FElysiumCameraCinematic::DecorateGoal(FElysiumCameraShot& Shot) const
{
	// **A cine camera has no weight and no blend** (M1): `bCine` is what the channel reads, and
	// every exit is a same-tick cut. `bTracked` is retail's `CamMode == 1` — the one mode the client
	// tracker drives — and it is derived from the mode at this single place, so no reader changes.
	Shot.bCine = true;
	Shot.bTracked = CamMode == static_cast<int32>(EElysiumCineCamMode::NamedShot);
	Shot.BlendSeconds = 0.0f;
	// `m_bDrawPlayer` `+0x640`, the director's `spawnflags & 2` copied onto the runtime camera by
	// `FUN_10070780` and forced to 1 by anim event 4050. SC5 consumes it.
	Shot.Presentation.bDrawPlayerBody = bDrawPlayerBody;
}

void FElysiumCameraCinematic::ReleaseGoal()
{
	if (PublishedShotId == 0)
	{
		return;
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		// A cine release is a cut (M1); the blend argument is not consulted, and it is stated at 0
		// so the call site reads as the transition it is.
		Embodiment->PopCameraShot(PublishedShotId, /*BlendOutSeconds*/ 0.0f);
	}
	PublishedShotId = 0;
}

void FElysiumCameraCinematic::PublishGoal(const FElysiumCameraShot& Shot)
{
	// `ShouldTransmit` (slot 86) refuses the camera to every client while `CamMode == 0`, so an idle
	// camera's replicated pose reaches nobody and `CalcView` falls back to the player's own eye —
	// which is exactly the state `FUN_10070780`'s re-shot branch leaves behind when `SetShot` fails
	// on a name that does not exist. The port's equivalent of "not transmitted" is "not on the
	// channel", so the goal is released rather than left standing.
	if (!IsActive())
	{
		ReleaseGoal();
		return;
	}
	// `m_vecCamOrigin` / `m_vecCamTarget` / `m_angCamAngles` / `m_flFOV` are the ENTITY's own fields
	// in retail — the SendProp channel merely replicates them — and `CBaseCineCam::vfunc193`
	// (`0x1006d910`) reads `+0x5ec` off the entity whether or not any client is listening. SC9's
	// `DialogPOV` reader is exactly that call, so the goal is recorded before the channel is
	// consulted and a headless world still answers with the pose it just solved.
	LastGoal = Shot;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return;   // a headless logic world runs the whole lifecycle without a camera to point
	}
	if (PublishedShotId == 0)
	{
		PublishedShotId = Embodiment->PushCameraShotValue(Shot);
		return;
	}
	// An un-adopted camera keeps thinking in retail — it simply is not `m_iCameraOverrideIdx` any
	// more, so its replicated pose reaches no client. The port's equivalent is "stop updating the
	// value once the slot has moved on"; a disposable camera is removed by the same transition
	// anyway, and a non-disposable one has no way to be in the slot in the first place.
	if (World && World->CineCameraEntity().IsSet() && World->CineCameraEntity() != Handle)
	{
		return;
	}
	Embodiment->UpdateCameraShotValue(PublishedShotId, Shot);
}

void FElysiumCameraCinematic::RefreshPublishedGoal()
{
	if (PublishedShotId == 0 || !IsActive())
	{
		return;
	}
	FElysiumCameraShot Shot = LastGoal;
	DecorateGoal(Shot);
	PublishGoal(Shot);
}

void FElysiumCameraCinematic::ApplyForcePlayerLook(const FVector& LookAt)
{
	// `if (m_bForcePlayerLook && handleLive(m_hSubject)) FUN_10178590(subject, m_vecCamTarget)`.
	//
	// The runtime value is **1 by construction** and the director's `point_player` keyvalue is never
	// copied onto the runtime camera, so every shot turns its subject — including the 26 shipped
	// directors that author `point_player 0`. That is a retail defect, reproduced;
	// `docs/vtmb/retail-defects.md` §7 carries the row.
	if (!bForcePlayerLook || !World)
	{
		return;
	}
	FElysiumEntity* SubjectEntity = World->Resolve(Subject);
	FElysiumCombatCharacter* Character = SubjectEntity ? SubjectEntity->AsCombatCharacter() : nullptr;
	if (!Character)
	{
		return;
	}
	// `FUN_10178590`: `dir = point - EyePosition(); VectorNormalize; VectorAngles;` then
	// `FUN_10178550` writes the pending eye-angle snap. It turns the **subject**, never the camera.
	Character->LookAtWorldPoint(LookAt);
}

// --- StartShot / EndShot ------------------------------------------------------------------------

FElysiumEntityHandle FElysiumCameraCinematic::CreateRuntimeCamera(FElysiumEntityWorld& World,
	const FString& Name, int32 InCamMode,
	const FElysiumEntityHandle (&Anchors)[FElysiumShotBindings::Num])
{
	// `FUN_10070470`:
	//   this = Create("camera_cinematic", vec3_origin);
	//   this[0x81] |= 4;                                   // disposable
	//   if (!SetShot(name, camMode, NULL)) { UTIL_Remove(this); return NULL; }
	//   SetShotAnchorEntity x4; FUN_1006e8e0(this);
	FElysiumEntityDef Def;
	Def.Classname = TEXT("camera_cinematic");
	Def.Origin = FVector::ZeroVector;
	const FElysiumEntityHandle Handle = World.SpawnRuntimeEntity(MoveTemp(Def));
	FElysiumEntity* Created = World.Resolve(Handle);
	FElysiumCameraCinematic* Camera = Created ? Created->AsCameraCinematic() : nullptr;
	if (!Camera)
	{
		return FElysiumEntityHandle::Invalid();
	}
	Camera->bDisposable = true;
	if (!Camera->SetShot(Name, InCamMode, FElysiumEntityHandle::Invalid()))
	{
		// The ONLY failure path.
		Created->Kill();
		return FElysiumEntityHandle::Invalid();
	}
	for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
	{
		if (Anchors[Index].IsSet())
		{
			Camera->SetShotAnchorEntity(Index, Anchors[Index]);
		}
	}
	Camera->StartShotPlacement();
	return Handle;
}

// --- `FindBestShot` and its two predicates (SC8) -------------------------------------------------

bool FElysiumCameraCinematic::AnchorsExist() const
{
	// `FUN_1006d9d0`:
	//
	//     flags = GetFlags();                                    // the shot record's +0x20
	//     if (flags == 0xffffffff) return false;
	//     if ((flags & 1) && !handleLive(+0x610)) return false;   // Start declared, no entity
	//     if ((flags & 2) && !handleLive(+0x614)) return false;   // End
	//     if ((flags & 4) && !handleLive(+0x618)) return false;   // Point1
	//     if ((flags & 8) && !handleLive(+0x61c)) return false;   // Point2
	//     return m_ShotIndex != -1;
	//
	// The `0xffffffff` guard and the trailing `m_ShotIndex != -1` are the same question asked twice:
	// with no shot loaded there is no record to read flags out of. `bShotLoaded` is the port's
	// `m_ShotIndex != -1`, so it stands for both.
	if (!bShotLoaded || !ShotDef.IsValid())
	{
		return false;
	}
	// Bits 0 and 1 are the `Start` / `End` presence bits; bits 2 and 3 are the two `Target`
	// sub-block bits, which the parser raises **by order of presence** while writing the fixed
	// slot. The bit is what retail tests and the slot is what it indexes, so the bug travels here
	// too: a `Point2`-only shot raises the `Point1` bit and this tests anchor 2's handle.
	const bool bDeclared[FElysiumShotBindings::Num] =
	{
		ShotDef.Start.bPresent,
		ShotDef.End.bPresent,
		ShotDef.bTargetPoint1Flagged,
		ShotDef.bTargetPoint2Flagged,
	};
	for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
	{
		if (!bDeclared[Index])
		{
			continue;
		}
		// `handleLive` is an EHANDLE resolve, not a "the field was written" test — a handle whose
		// entity has since died reads dead here.
		if (!World || World->Resolve(Bindings.Anchors[Index].Entity) == nullptr)
		{
			return false;
		}
	}
	return true;
}

bool FElysiumCameraCinematic::CanSeeTarget()
{
	// `FUN_1006db10`, in its own order: the look-at first, then the `Start` anchor, then the `End`
	// anchor. Both traces share one hull and one filter.
	if (!bShotLoaded)
	{
		return false;
	}
	const bool bLatched = ElysiumCameraShots::LatchesAnchors(ShotDef);
	const FVector LookAt = ElysiumCineCamImpl::SolveLookAt(World, ShotDef, Bindings, bLatched);

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		// A headless logic world has no geometry to refuse a candidate with, and refusing every
		// shot there would make `FindBestShot` answer false in exactly the runs meant to prove it.
		return true;
	}
	// `mins (-1,-1,-1)` / `maxs (1,1,1)` — a 2-unit hull, in Source units.
	const FVector HalfExtent(1.0f * ElysiumCam::U, 1.0f * ElysiumCam::U, 1.0f * ElysiumCam::U);

	// Anchor 0 (`Start`) then anchor 1 (`End`); the two `Target` anchors are the look-at and are
	// never traced from.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!World || World->Resolve(Bindings.Anchors[Index].Entity) == nullptr)
		{
			continue;   // `handleLive(+0x610)` / `handleLive(+0x614)` — an absent anchor is skipped
		}
		FVector From;
		if (!FElysiumCameraDirector::ResolveAnchorPoint(World, ShotDef, Index, Bindings, bLatched,
			From))
		{
			continue;
		}
		float Fraction = 1.0f;
		bool bStartSolid = false;
		// `CTraceFilterSimple(m_hSubject, 0)` — **the shot's subject never blocks its own shot**,
		// which is what stops the player's body from rejecting every candidate framed on him.
		if (!Embodiment->TraceCameraHull(From, LookAt, HalfExtent, Subject, Fraction, bStartSolid))
		{
			continue;   // no collision world answered; the candidate is admitted
		}
		if (Fraction < 1.0f || bStartSolid)
		{
			return false;
		}
	}
	return true;
}

bool FElysiumCameraCinematic::FindBestShot(const FString& BaseName, FRandomStream& Rng)
{
	// `this->CamMode (+0x638) = 1;` — written **directly**, before any `SetShot`. Every `SetShot`
	// below clears the mode and rewrites it, so on any path that reaches the loop this write is
	// overwritten; it is reproduced because it is what the listing does and because it is the state
	// a `SetShot`-less path would leave behind.
	CamMode = static_cast<int32>(EElysiumCineCamMode::NamedShot);

	TArray<int32> Candidates;
	// `for (i = 1; ; ++i)` — unbounded in retail, ended only by the first name the shot table does
	// not hold. Reproduced as written: the enumeration walks `<base>_1`, `<base>_2`, … and the gap
	// is the terminator, so a family that skips an index (`_1`, `_2`, `_4`) offers two candidates
	// and never sees the fourth.
	for (int32 Index = 1; ; ++Index)
	{
		const FString Name = ElysiumCineCamImpl::ShotNameFor(BaseName, Index);
		if (SetShot(Name, static_cast<int32>(EElysiumCineCamMode::NamedShot),
			FElysiumEntityHandle::Invalid()))
		{
			// `FUN_1006e8e0(this)` — the candidate is actually **placed** before it is judged, so
			// the anchors it declares are bound, the shot-start cache is filled and the origin
			// selector has been decided. The predicates read that state, not the record.
			StartShotPlacement();
			if (AnchorsExist() && CanSeeTarget())
			{
				Candidates.Add(Index);
			}
		}
		// `if (this->m_ShotIndex == -1) break;` — the test is on the shot index and not on
		// `SetShot`'s return, so it is the *loaded* state that ends the scan.
		if (!bShotLoaded)
		{
			break;
		}
	}

	if (Candidates.IsEmpty())
	{
		// `return false` with the camera left wherever the last failed `SetShot` put it: idle, mode
		// 0, no shot. `FUN_10070550` is what removes the entity.
		return false;
	}

	// `k = RandomInt(0, vec.Count()-1)` — a **uniform** pick with no scoring of any kind. The draw
	// is the named stream's, never `FMath::Rand*`, so a seeded session reproduces the same shot.
	const int32 Pick = Rng.RandRange(0, Candidates.Num() - 1);
	const FString Chosen = ElysiumCineCamImpl::ShotNameFor(BaseName, Candidates[Pick]);
	SetShot(Chosen, static_cast<int32>(EElysiumCineCamMode::NamedShot),
		FElysiumEntityHandle::Invalid());
	// `Msg("CBaseCineCam::FindBestShot chose %s\n", buf)` — retail's own line, verbatim.
	UE_LOG(LogElysiumCineCam, Log, TEXT("CBaseCineCam::FindBestShot chose %s"), *Chosen);
	// **No `FUN_1006e8e0` after the final `SetShot`** — the caller runs the shot start.
	return true;
}

FElysiumEntityHandle FElysiumCameraCinematic::CreateFindBestShotCamera(FElysiumEntityWorld& World,
	const FString& BaseName, FRandomStream& Rng)
{
	// `FUN_10070550(baseName)`:
	//   this = CBaseEntity::Create("camera_cinematic", vec3_origin);
	//   this[0x81] |= 4;                                   // +0x204 & 0x4 — disposable
	//   if (!FUN_1006e4c0(this, baseName)) { UTIL_Remove(this); return NULL; }
	//   FUN_1006e8e0(this);
	//
	// The disposable bit is what makes 4051's "drop and destroy" a one-liner: `SetCineCamera(NULL)`
	// removes the outgoing camera precisely because this bit is on it.
	FElysiumEntityDef Def;
	Def.Classname = TEXT("camera_cinematic");
	Def.Origin = FVector::ZeroVector;
	const FElysiumEntityHandle Handle = World.SpawnRuntimeEntity(MoveTemp(Def));
	FElysiumEntity* Created = World.Resolve(Handle);
	FElysiumCameraCinematic* Camera = Created ? Created->AsCameraCinematic() : nullptr;
	if (!Camera)
	{
		return FElysiumEntityHandle::Invalid();
	}
	Camera->bDisposable = true;
	if (!Camera->FindBestShot(BaseName, Rng))
	{
		Created->Kill();
		return FElysiumEntityHandle::Invalid();
	}
	// The shot start `FindBestShot` deliberately did not run after its final `SetShot`.
	Camera->StartShotPlacement();
	return Handle;
}

void FElysiumCameraCinematic::InputStartShot()
{
	if (!World)
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = World->FindPlayer();
	// `InputStartShot` `0x10070720` ignores its `inputdata` entirely — the activator is always
	// `UTIL_PlayerByIndex(1)`.
	const FElysiumEntityHandle PlayerHandle = PlayerEnt
		? PlayerEnt->Handle : FElysiumEntityHandle::Invalid();

	// 1. Four `FindEntityByName(NULL, name, 0, 0)` lookups, with the empty string substituted for a
	//    null keyvalue. A miss yields NULL and leaves that anchor to the shot file's own `Position`.
	const FString* const Keys[FElysiumShotBindings::Num] =
		{ &StartEntKey, &EndEntKey, &Target1Key, &Target2Key };
	FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
	for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
	{
		if (!Keys[Index]->IsEmpty())
		{
			if (const FElysiumEntity* Found = World->FindByName(*Keys[Index]))
			{
				Anchors[Index] = Found->Handle;
			}
		}
	}

	// 2. `cam = player->GetCineCamera()`.
	FElysiumEntity* Adopted = World->Resolve(World->CineCameraEntity());
	FElysiumCameraCinematic* Camera = Adopted ? Adopted->AsCameraCinematic() : nullptr;

	if (!Camera)
	{
		// 3. No camera yet -> create one, then `cam->m_hSubject = player`.
		const FElysiumEntityHandle Created = CreateRuntimeCamera(*World, ShotNameKey,
			static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
		FElysiumEntity* CreatedEntity = World->Resolve(Created);
		Camera = CreatedEntity ? CreatedEntity->AsCameraCinematic() : nullptr;
		if (!Camera)
		{
			// 5. `DevWarning("%s could not start shot properly\n")` and return — no adoption, no
			//    immobilize, nothing.
			UE_LOG(LogElysiumCineCam, Warning, TEXT("%s could not start shot properly"),
				*DebugString());
			return;
		}
		Camera->Subject = PlayerHandle;
	}
	else
	{
		// 4. A camera is already adopted -> re-shot it. **This branch ignores `SetShot`'s return
		//    value**, so a bad shot name leaves `CamMode 0` / no shot and the camera goes **idle**
		//    rather than falling back — the asymmetry with `SetCamera`, which does fall back to
		//    `DialogDefault`.
		Camera->SetShot(ShotNameKey, static_cast<int32>(EElysiumCineCamMode::NamedShot),
			PlayerHandle);
		for (int32 Index = 0; Index < FElysiumShotBindings::Num; ++Index)
		{
			if (Anchors[Index].IsSet())
			{
				Camera->SetShotAnchorEntity(Index, Anchors[Index]);
			}
		}
		Camera->StartShotPlacement();
	}

	// 6. `cam->m_bDrawPlayer = director->m_bDrawPlayer` — the ONLY field copied from the director,
	//    which is why `point_player` never reaches the camera that runs the shot. It lands AFTER
	//    the shot start, which in retail costs nothing because the byte is a SendProp read off the
	//    entity; here the goal already carries a stale copy, so it is re-stamped on the same tick.
	Camera->bDrawPlayerBody = bDrawPlayerBody;
	Camera->RefreshPublishedGoal();

	// 7. `FUN_1017cef0(player, cam)` — adopt, destroying the outgoing camera when it is disposable.
	World->SetCineCamera(Camera->Handle, Camera->PublishedShotId, Camera->bDisposable,
		Camera->ShotDef.Name);

	// 8. `FUN_1015ef40(player)` — `SetImmobilized(true)`: movement, jump, duck and weapon use, all
	//    frozen. It changes no view state; the view switch is entirely the client's.
	if (PlayerEnt)
	{
		PlayerEnt->SetImmobilized(true);
	}
}

void FElysiumCameraCinematic::InputEndShot()
{
	// `FUN_10070990(director, player)`, in order. **There is no blend** (M1): the camera dies this
	// tick, and the mobilize, the view-flag clear and the HUD restore land on the same frame.
	ClearMode();
	NextThink = ELYSIUM_NEVER_THINK;
	if (!World)
	{
		return;
	}
	// `SetCineCamera(player, NULL)` — drops AND destroys the runtime camera.
	World->ClearScriptedCamera();

	if (FElysiumPlayer* PlayerEnt = World->FindPlayer())
	{
		PlayerEnt->SetImmobilized(false);
		// `FUN_101815b0(player, 1)` and `(player, 8)` — `m_iVFlags &= ~mask` (RC4). **`EndShot`
		// clears locks `StartShot` never took**: it is a general release copied from the terminal /
		// sign / monitor closers, not the symmetric partner of the opener. The port must not add a
		// matching set in `StartShot`.
		PlayerEnt->RemoveViewFlags(EElysiumViewFlags::MoveAnglesFromEntity
			| EElysiumViewFlags::ViewAngleLock);
	}

	// `if (this->+0x204 & 0x4) UTIL_Remove(this);` — a DIRECTOR that authored `spawnflags & 4`
	// deletes itself. 12 shipped directors carry the bit and none of the twelve is ever sent
	// `EndShot`, so this arm has no content witness and its acceptance is a unit test.
	if (bDisposable)
	{
		Kill();
	}
}

void FElysiumCameraCinematic::TeardownShot()
{
	// `FUN_10071970`'s per-entity body: the full `EndShot` when active, then `UTIL_Remove`
	// unconditionally.
	if (IsActive())
	{
		InputEndShot();
	}
	Kill();
}

// --- Registration -------------------------------------------------------------------------------

namespace ElysiumCineCamImpl
{
	TUniquePtr<FElysiumEntity> MakeCameraCinematic()
	{
		return MakeUnique<FElysiumCameraCinematic>();
	}

	void BuildCameraCinematicClass(FElysiumClassDesc& D)
	{
		D.Input(TEXT("StartShot"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumCameraCinematic&>(E).InputStartShot(); });
		D.Input(TEXT("EndShot"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumCameraCinematic&>(E).InputEndShot(); });

		// The six datamap rows, read out of `.rdata` (RC2) rather than inferred. There is **no
		// `KeyValue` handler** on this class: every one of these is a plain `CBaseEntity` map-data
		// field, and `spawnflags` is parsed by the engine into `m_spawnflags` and read by code.
		ElysiumAddClassField(D, TEXT("shotname"), &FElysiumCameraCinematic::ShotNameKey);
		ElysiumAddClassField(D, TEXT("startent"), &FElysiumCameraCinematic::StartEntKey);
		ElysiumAddClassField(D, TEXT("endent"), &FElysiumCameraCinematic::EndEntKey);
		ElysiumAddClassField(D, TEXT("target1"), &FElysiumCameraCinematic::Target1Key);
		ElysiumAddClassField(D, TEXT("target2"), &FElysiumCameraCinematic::Target2Key);
		ElysiumAddClassField(D, TEXT("point_player"), &FElysiumCameraCinematic::bPointPlayerKey);
	}

	FElysiumClassRegistrar GRegCameraCinematic(TEXT("camera_cinematic"), ElysiumBaseClassName(),
		&MakeCameraCinematic, &BuildCameraCinematicClass);
}
