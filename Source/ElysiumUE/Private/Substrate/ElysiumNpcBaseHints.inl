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
