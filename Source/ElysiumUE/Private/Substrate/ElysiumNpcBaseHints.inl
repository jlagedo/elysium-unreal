// `CAI_BaseNPC`'s declarations of the `Hints` family (story 5 step 5),
// moved from `ElysiumNpcHints*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseHints.cpp`.

/**
 * One `CAI_Hint`'s own words, by retail offset and datamap name (`vtmb_fields CAI_Hint`).
 *
 * This is a VIEW, not a store: `HintWords()` fills it and nothing keeps it. `bValid` is false when
 * the seam could not resolve the node, which today is always.
 */
struct FHintWords
{
	bool bValid = false;
	// The hint entity itself — retail holds the `CAI_Hint*`; this runtime holds its entity index
	// (0018 story 2). Distinct from `NodeId`, the network node the hint is bound to.
	int32 HintIndex = INDEX_NONE;
	FString Name;                     // +0x026c m_iName                 key targetname
	FString Activity;                 // +0x0450 m_strActivity
	float TargetAngleRange = 0.f;     // +0x0454 m_flTargetAngleRange    key target_angle_range
	float TargetAngleRangeDot = 0.f;  // +0x0458 m_flTargetAngleRangeDot
	float TargetDistMin = 0.f;        // +0x045c m_flTargetDistMin       key target_dist_min
	float TargetDistMax = 0.f;        // +0x0460 m_flTargetDistMax       key target_dist_max
	float HintRating = 0.f;           // +0x0464 m_flHintRating          key hint_rating
	FString TargetName;               // +0x0468 m_strTargetName         key target_name
	int32 IpPercent = 0;              // +0x046c m_iIPPercent            key ip_percent
	// +0x0470 m_iGroupID, key group_id — the 32-BIT SET `FValidateHintType` (`0x10295c20`) ANDs
	// against `FElysiumNpcScheduleHost::HintGroupMask` (`m_iHintGroups +0x62e4`), NOT a plain id.
	int32 GroupMask = 0;
	int32 HintType = 0;               // +0x05dc m_nHintType             key HintType
	FElysiumEntityHandle HintOwner;   // +0x05e0 m_hHintOwner
	int32 NodeId = INDEX_NONE;        // +0x05e4 m_nNodeID — the AI-network node, not a hint index
	int32 Disabled = 0;               // +0x05e8 m_iDisabled             key StartHintDisabled
	double NextUseTime = 0.0;         // +0x05ec m_flNextUseTime
	FString Group;                    // +0x05f0 m_strGroup              key Group
	// Retail reads these through the entity vtable, `+0x364 GetAbsOrigin` and `+0x36c GetAbsAngles`.
	// The origin is in CENTIMETRES here, as every port position is; retail's is Source units.
	FVector OriginCm = FVector::ZeroVector;
	FVector Angles = FVector::ZeroVector;
};

/** What `CAI_Hint::OnRestore` (slot 130, `0x102d3ec0`) did. `bNodeFound` false is retail's
 *  `"Warning: AI hint has incorrect origin"` arm — see the definition. */
struct FHintRestoreResult
{
	bool bNodeFound = false;
	bool bClaimedNode = false;
	FVector NodeOriginCm = FVector::ZeroVector;
};

/** Resolve `HintNode` — a hint's entity index, the form a `ScheduleHost::HintNode` holds — into the
 *  live `ai_hint`'s words (`FElysiumHint::ToWords`). Answers false and leaves `Out` untouched when
 *  the index is not a live hint. */
bool HintWords(int32 HintNode, FHintWords& Out) const;

/** SEAM for `0x102d1af0` — the task-side hint search `FindHintNode` (`0x10365780`) runs, with the
 *  hint type, the caller's flag byte and a radius in SOURCE UNITS. Answers `INDEX_NONE`. */
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;

/** SEAM for `0x102d24b0` — the same search anchored on another entity, which is what the two-group
 *  pick `SelectTzimisceHintNode` (`0x103bfa50`) calls twice. Retail walks the global list from the
 *  rotating cursor, skipping any node `IsHintUnusable` rejects, requiring `m_nHintType == HintType`
 *  (or `HintType == 0` for any), a squared-distance test against `RadiusUnits`, and slot 566
 *  `FValidateHintType` (vtable `+0x8d8`). Answers `INDEX_NONE`. */
int32 FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const;

/** `0x102d1420`, the hint release: `m_hHintOwner (+0x5e0) = -1` and
 *  `m_flNextUseTime (+0x5ec) = ReuseDelaySeconds + curtime`. SEAM on the write side only — the rule
 *  is exact, but there is no hint to write it into. */
void ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds);

/** `0x102d14c0` — is this hint unusable right now? Three arms, in retail's order: `m_iDisabled`
 *  non-zero; `curtime < m_flNextUseTime`; a live `m_hHintOwner`. Pure over the words, so this is the
 *  whole recovered rule and not a seam. `bOwnerAlive` is the `EHANDLE` validity test retail runs on
 *  `m_hHintOwner`. */
static bool IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive);

/** The node-index form of the above. Answers true — an unresolvable hint is not usable. */
bool IsHintUnusable(int32 HintNode, double Now) const;

/** SEAM for `RestartIdealActivity` (`0x10289ee0`), which every hint body calls with a retail
 *  `Activity` enum id. This runtime's activity vocabulary is NAMES (`FElysiumClipIdentity`) and
 *  carries no retail-id table — the same reason 29b left `m_IdealTranslatedActivity` (`+0x5cd0`) an
 *  opaque `int32`. The ported bodies therefore DECIDE the id (that half is tested) and hand it here,
 *  which records nothing. */
void RestartIdealActivityId(int32 RetailActivityId);

/** `CAI_Hint::OnRestore` (`0x102d3ec0`). The hint's own save-restore fixup, handed over from family
 *  Sounds. `this` is the hint, not the NPC, so it is a static helper rather than a member rule. */
static FHintRestoreResult HintOnRestore(const FHintWords& Hint);

/** SEAM for the entity vtable `+0x370` position accessor `SelectTzimisceHintNode` (`0x103bfa50`)
 *  compares through — NOT `+0x364 GetAbsOrigin`, which the rest of this family uses. Slot 220 is
 *  unidentified in the census, so this answers the entity's origin and says that is a stand-in. */
FVector HintComparePosition(const FElysiumEntity* Entity) const;

/** `0x102781e0` — `SetHintGroup(string_t)`: write `m_strHintGroup` (`+0x5db0`) and, only on a real
 *  change, dispatch slot 551 `OnChangeHintGroup(old, new)`. NAMED `SetHintGroup`, not 29c's
 *  `OnChangeHintGroup`: that name is slot 551 itself, which this body CALLS. */
void SetHintGroup(const FString& NewHintGroup);

/** SEAM for `ActivityNameToId` (`0x10412520`), the interest-place activity resolve. This runtime's
 *  activities are NAMES, so there is no retail id to answer: it answers -1, which is retail's own
 *  "not in the activity table" value and takes the `DevWarning` + `ACT_IDLE` fallback arm that both
 *  interest bodies carry. */
int32 ActivityIdForName(const FString& ActivityName) const;

void SetMotorHintYaw(float Yaw);

void ReleaseMotorHintYaw();

