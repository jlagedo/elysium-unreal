// `CAI_BaseNPC`'s declarations of the `Debug10` family (story 5 step 5),
// moved from `ElysiumNpcDebug10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDebug10.cpp`.

/** `CAI_BaseNPC::DrawDebugTextOverlays` (`0x102767d0`) — slot 124's BASE body, beside the Troika
 *  override that owns the slot. The Troika body calls it first for its starting line index. Returns
 *  the next free entity-text line. */
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 124); defined by hand as
// `FElysiumNpcBase::DrawDebugTextOverlays`.

/** `0x1027ef20` — append one line to the NPC's own debug ring. A null line does nothing at all.
 *  Retail `sprintf`s it at `this + 0x1b4e + cursor`, advances the cursor at `+0x5b50` by the byte
 *  count, and on a cursor past `0x3dff` zero-fills the rest of the 0x4000-byte buffer, sets the wrap
 *  latch at `+0x5b54` and resets the cursor to 0. The ring is absent; the line lands on the
 *  `DevMsg` channel and `DebugLogRingAdvance` below is the arm. */
void AppendDebugLogLine(const TCHAR* Text);

/** `0x1027ee20` — the same append against the GLOBAL trace ring slots 17 and 19 use instead of the
 *  per-entity one. Seventy-nine bytes, no verdict row of its own, and story 29c-1's slot-19 body
 *  already records it absent for the same reason. */
void AppendGlobalDebugLogLine(const TCHAR* Text) const;

/** `0x1027ef20`'s cursor arm, as a pure function so the recovered rule is exercised without the
 *  ring: given the cursor before the write and the number of bytes `sprintf` returned, answer the
 *  cursor after, and set `bOutWrapped` when the wrap latch (`+0x5b54`) was raised. The test is
 *  `> 0x3dff`, not `>=`, and the reset is to 0. */
static int32 DebugLogRingAdvance(int32 Cursor, int32 Written, bool& bOutWrapped);

/** `0x1027efb0` — the ring DUMP `NPCThinkDebugPre`'s tail runs, which walks the 16 KB buffer from
 *  the `+0x5b50` cursor in 512-byte chunks. **ABSENT**: the ring is absent (above), and the row's own
 *  verdict in band 0–4 is `mechanism → UE_LOG`. Recorded so the arm is visible. */
void DumpDebugLogRing() const;

/** The active weapon's five text-overlay words — the name slot 0x570 answers and the four ammo
 *  numbers `Weapon: %s (%d/%d) (%d/%d)` prints: `m_iClip1` (`+0x74c`), the count for
 *  `m_iPrimaryAmmoType` (`+0x744`), `m_iClip2` (`+0x750`) and the count for `m_iSecondaryAmmoType`
 *  (`+0x748`). A negative ammo TYPE answers -1 without asking for a count, which is retail's own
 *  guard. **SEAM**: answers false, and the line becomes retail's `UNARMED`. */
bool ActiveWeaponTextWords(FString& OutName, int32& OutClip1, int32& OutAmmo1, int32& OutClip2,
	int32& OutAmmo2) const;

/** `m_pSquad` (`+0x5da4`) and its name at `+0x4`, which the `0x80000` arm appends to `Squad: %c : `.
 *  Retail reads the squad OBJECT here and does NOT apply the `m_iSquadDisconnected` gate
 *  `ConnectedSquad()` applies — the gate only picks the `%c`. The port has no squad object; the
 *  recovered mapping is that `InitSquad` stands one for any NPC whose `m_SquadName` is set and that
 *  the object's `+0x4` IS that name, so a non-empty `SquadName` answers true with it. */
bool SquadObjectName(FString& OutName) const;

/** `CAI_Hint`'s two overlay words for the facing pair: `m_nHintType` (`+0x5dc`) and the yaw at
 *  `+0x454`, plus `0x102d12e0`'s own yaw and the hint's origin. **SEAM**: family Hints records that
 *  hints are node INDICES here with no type store; answers false and the arm is skipped. */
bool HintOverlayWords(int32& OutHintType, float& OutHintYawDegrees, float& OutNodeYawDegrees,
	FVector& OutOriginUnits) const;

/** `0x10278650(out, in, 0.0, 0.0)` — the head-adjusted anchor the second `NPCThinkDebugPre` ConVar
 *  block draws its ±2 box at. **SEAM**: the body is family **Motor10**'s row in this same band
 *  (`0x10278650` → `FElysiumNpc::ComputeStandoffAnchorOffset`); answers the position handed in until
 *  the two are wired. */
FVector RetailStandoffAnchorUnits(const FVector& EyeUnits) const;
