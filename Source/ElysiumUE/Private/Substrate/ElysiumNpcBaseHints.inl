// `CAI_BaseNPC`'s declarations of the `Hints` family (story 5 step 5),
// moved from `ElysiumNpcHints*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseHints.cpp`.

/**
 * One `CAI_Hint`'s own words, by retail offset and datamap name (`vtmb_fields CAI_Hint`).
 *
 * This is a VIEW, not a store: `HintWords()` fills it from the live `ai_hint` (`FElysiumHint::ToWords`)
 * and nothing keeps it. `bValid` is false on a default-constructed view (no hint resolved).
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
	// +0x0474, the class word `CAI_Hint::Spawn` (`0x102d0b60`) derives from the type (`HintSpawn`)
	// and the class-mask search `0x102d2980` ANDs. Not in the datamap: session-only, never saved.
	int32 ClassMask = 0;              // +0x0474
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
 *  `"Warning: AI hint has incorrect origin"` arm — see the definition. `NodeIndex` is the node the
 *  hint relinked onto, `NodeOriginCm` its raw origin, where the hint is teleported. */
struct FHintRestoreResult
{
	bool bNodeFound = false;
	bool bClaimedNode = false;
	int32 NodeIndex = INDEX_NONE;
	FVector NodeOriginCm = FVector::ZeroVector;
};

/** Resolve `HintNode` — a hint's entity index, the form a `ScheduleHost::HintNode` holds — into the
 *  live `ai_hint`'s words (`FElysiumHint::ToWords`). Answers false and leaves `Out` untouched when
 *  the index is not a live hint. */
bool HintWords(int32 HintNode, FHintWords& Out) const;

/** The hint searches over the world's list (`FElysiumEntityWorld::HintList`, retail's
 *  `DAT_10925450`), `docs/vtmb/npc-ai/shape.md` § "The hint list and its four searches". Every answer
 *  is a hint ENTITY index (`FHintWords::HintIndex`), `INDEX_NONE` for retail's NULL. They are const
 *  on the NPC -- "the searches write nothing on the NPC" -- but write the WORLD's rotating cursor
 *  (`DAT_10925454`, `FElysiumEntityWorld::HintCursor`). Radii and scores are SOURCE UNITS.
 *
 *  `0x102d1af0(npc, type, flags, radius, NULL, NULL)` -- the task-side search (`FindHintNode`
 *  `0x10365780`, base `StartTask` 0x40 / 0x41, the Troika tactical and shoot-at searches). */
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;

/** `0x102d1af0`, whole: the distance is measured from `OriginCm` (the NPC's origin when null);
 *  `*OutScore` (when non-null) receives the winner's score and is NOT written on a miss. Flags: bit 0
 *  a clear eye trace, bit 1 score by squared distance, bit 3 score by distance x `m_flHintRating`
 *  (either scoring bit keeps the best, else the first admitted wins), bit 2 diverts to
 *  `FindHintRandom`. Every list element is visited once, the cursor's last. */
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits, const FVector* OriginCm,
	float* OutScore) const;

/** `0x102d24b0(npc, anchor, type, flags, radius)` -- the search measured from, and (bit 0) traced
 *  from the eye of, `Near` rather than the NPC; `SelectTzimisceHintNode` (`0x103bfa50`) calls it
 *  twice. The first admitted element wins; no scoring. Bit 2 diverts to `FindHintRandom`. Walks from
 *  the cursor's successor and stops ON the cursor, which it never examines. */
int32 FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const;

/** `0x102d2980(npc, flags, mask, radius, origin, outScore)` -- the class-word search: admits a hint
 *  whose `+0x474` (`FHintWords::ClassMask`) shares a bit with `ClassMask`, so a zero mask admits
 *  nothing. Walk as `FindHintOfTypeNear`; scoring as `FindHintNear`. With bit 0 CLEAR the first
 *  admitted element wins and `*OutScore` is `FLT_MAX`; with bit 0 set the walk runs on, the best
 *  (with no scoring bit, the LAST admitted) wins. Bit 2 does nothing here. */
int32 FindHintByClassMask(uint8 SearchFlags, int32 ClassMask, float RadiusUnits,
	const FVector* OriginCm = nullptr, float* OutScore = nullptr) const;

/** `0x102d2940` -- `0x102d2980` with mask 1 (the cover band: types 100 / 101 / 0x27d8). */
int32 FindHintByClassMask1(uint8 SearchFlags, float RadiusUnits) const;

/** The first admission gate of `FindHintByClassMask` a hint fails (`DryRunHintByClassMask`). */
enum class EHintAdmissionGate : uint8
{
	Admitted,          // passed gates 1-4 (and the bit-0 trace when the flags ask for it)
	NotLive,           // the list entry names no live `ai_hint` (the walk skips it)
	Unusable,          // gate 1, `0x102d14c0`
	ClassMask,         // gate 2, `(mask & hint+0x474) == 0`
	Distance,          // gate 3, `!(d^2 < radius^2)`
	ValidateHintType,  // gate 4, slot 566 `FValidateHintType`
	Trace,             // flags bit 0, the eye trace
};

/** One hint's row of a dry run: the gate it stopped at, and what the walk would weigh it by. */
struct FHintAdmissionRow
{
	int32 HintIndex = INDEX_NONE;
	EHintAdmissionGate Gate = EHintAdmissionGate::NotLive;
	float DistanceUnits = 0.f;   // gate 3's distance from the search origin (sqrt of d^2), Source units
	bool bScored = false;        // the flags score (bit 3 before bit 1) and gates 1-4 passed
	float Score = MAX_FLT;       // the score the walk would compare
	double ScoreRated = 0.0;     // bit 3's x87-precision product the walk compares against
};

/** DEBUG ONLY (`elysium.gr_hints --validate`): a DRY RUN of `FindHintByClassMask` with the same
 *  flags, mask and radius, measured from this NPC. Every list element is put through the walk's
 *  gates in retail's order — `IsHintUnusable`, the class word, the distance, slot 566
 *  `FValidateHintType` (which routes to the cover validators by type) — then the bit-0 trace; `OnRow`
 *  receives each row, head first, straight after its gates ran (so a caller can read what the
 *  validators recorded). Answers the hint the walk would return — its start after the cursor, stop,
 *  scoring and tie rules — WITHOUT writing the cursor and without claiming. */
int32 DryRunHintByClassMask(uint8 SearchFlags, int32 ClassMask, float RadiusUnits,
	TFunctionRef<void(const FHintAdmissionRow&)> OnRow) const;

/** `0x102d1760(npc, type, flags, radius, NULL)` -- the random pick flags bit 2 diverts `0x102d1af0`
 *  and `0x102d24b0` to. Head to tail once, no cursor start: the type, distance (from the NPC),
 *  slot-566 and (bit 0) trace gates; collects every admitted hint and draws ONE `RandomInt(0,
 *  count - 1)` when there is at least one. The cursor becomes the pick, or `INDEX_NONE`; an empty
 *  list answers `INDEX_NONE` and leaves it alone. */
int32 FindHintRandom(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;

/** `0x102d1350(hint, npc)`, the claim: refused (false) only when `m_hHintOwner` resolves to a live
 *  entity that is not this NPC; otherwise (no owner, a stale owner, already mine) it writes
 *  `m_hHintOwner = my handle` and answers true. On the LIVE hint. An index that names no live hint
 *  answers false and writes nothing (a crash guard: retail dereferences the `CAI_Hint*`). */
bool ClaimHint(int32 HintNode);

/** `0x102d1450(hint, npc)`: `m_hHintOwner` resolves to this NPC. A stale or empty owner resolves to
 *  NULL and so is not mine. An index that names no live hint answers false (crash guard). */
bool OwnsHint(int32 HintNode) const;

/** `0x102d1420`, the hint release: `m_hHintOwner (+0x5e0) = -1` and
 *  `m_flNextUseTime (+0x5ec) = curtime + ReuseDelaySeconds`, the two stores and nothing else -- no
 *  owner gate (that is the caller's, `CAI_BaseNPCTroika::ClearHintNode 0x10295ab0`). On the LIVE
 *  hint; an index that names no live hint writes nothing (crash guard). */
void ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds);

/** Tests only: bit-0 line traces the hint searches asked for with no collision world behind the NPC
 *  (no embodiment, or `TraceRetail` answering false) and so read as clear -- the seam counter the
 *  motor keeps as `MotorSeams.HullTraces`. */
mutable int32 HintSearchUnansweredTraces = 0;

/** `0x102d14c0` — is this hint unusable right now? Three arms, in retail's order: `m_iDisabled`
 *  non-zero; `curtime < m_flNextUseTime`; a live `m_hHintOwner`. Pure over the words, so this is the
 *  whole recovered rule and not a seam. `bOwnerAlive` is the `EHANDLE` validity test retail runs on
 *  `m_hHintOwner`. */
static bool IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive);

/** The node-index form of the above. Answers true — an unresolvable hint is not usable. */
bool IsHintUnusable(int32 HintNode, double Now) const;

/** `CAI_BaseNPC::RestartIdealActivity` (`0x10289ee0`): `m_Activity` (`+0xfec`, `ActivityNumber`) back
 *  to 0 when it already is the id, then `SetIdealActivity(id)` (`0x10272650`). Ids are retail's
 *  `Activity` numbers. */
void RestartIdealActivityId(int32 RetailActivityId);

/** `CAI_Hint::OnRestore` (`0x102d3ec0`). The hint's own save-restore fixup, handed over from family
 *  Sounds. `this` is the hint, not the NPC, so it is a static helper rather than a member rule:
 *  `FElysiumHint::OnPostRestore` calls it with the hint's own handle and the world's place set, and
 *  applies the teleport the result names. */
static FHintRestoreResult HintOnRestore(const FHintWords& Hint, const FElysiumEntityHandle& HintHandle,
	FElysiumPlaceSet& Places);

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

