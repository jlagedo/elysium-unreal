#include "Substrate/ElysiumNpc.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **SpeciesMisc10** — the species words, the Newscaster, the Chang brothers, the
// ghoul croucher, the guard and the ManBat. The second half is in
// `ElysiumNpcKernelSpeciesMisc10_2.cpp`; the declarations and this family's five standing facts are
// in `ElysiumNpcKernelSpeciesMisc10.inl`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. Each place this family's reading corrected the checklist's one-line
// walk is marked **CORRECTION** at the arm it changes.

namespace
{
	// --- `.rdata` cells, every one read out of the pinned `vampire.dll` at file offset
	// `address - 0x10000000` (`.rdata` is identity-mapped). The mapping is validated by
	// `_DAT_104ce8c0` reading `1e-05` and `_DAT_104454c4` reading `0.0`, both of which the port
	// already records from story 29c-1.

	// `_DAT_1044fab0` — a **DOUBLE**, `0.0` (`103c1db6` is `FCOMP double ptr [0x1044fab0]`). The "no
	// slow running" sentinel the ManBat and the head claw both compare their expiry against.
	constexpr double GSlowExpireSentinel = 0.0;

	// `_DAT_104ad9f8` = **0.1**, `CNPC_VChangBros`'s teleport health-loss threshold.
	constexpr float GChangTeleportHealthLoss = 0.1f;
	// `_DAT_104ada48` = **30.0** s and `_DAT_104ada4c` = **0.5**, the united-attack cooldown and the
	// health fraction each brother is tested against.
	constexpr double GChangUnitedCooldownSeconds = 30.0;
	constexpr float GChangUnitedHealthFraction = 0.5f;

	// The ManBat cone's screen shake (`1038ea4b`..`1038ea60`) and its slow restamp (`1038eb85`).
	constexpr float GManBatShakeAmplitude = 2.5f;
	constexpr float GManBatShakeFrequency = 0.2f;    // 0x3e4ccccd
	constexpr float GManBatShakeDuration = 3.0f;     // 0x40400000
	constexpr float GManBatShakeRadius = 0.f;
	constexpr float GManBatSlowSecondsMin = 15.f;    // 0x41700000
	constexpr float GManBatSlowSecondsMax = 25.f;    // 0x41c80000
	// `1038eb5e` / `103c1dca` — `BeginSlowEntity`/`EndSlowEntity`'s only argument in this image.
	constexpr float GSlowEntityMagnitude = 500.f;    // 0x43fa0000

	// `CNPC_VGhoulCroucher::BurnPlayer`'s per-hitbox pair (`1037c12a`) and the damage its one caller
	// hands it (`1037bf3c`, `0x41200000`).
	constexpr float GBurnHitboxSeconds = 5.f;
	constexpr float GBurnHitboxInterval = 0.5f;      // 0x3f000000
	// `CTakeDamageInfo`'s `bitsDamageType` at `1037c14b` — retail's DMG_BURN.
	constexpr int32 GBurnDamageBits = 8;

	// The two relationship literals. They differ only in the case of the target word, and both are
	// handed to `InputSetRelationship` (`0x10273790`) with priority argument 0.
	const TCHAR* const GCopRelationshipLiteral = TEXT("Player D_HT 10");      // 0x106366f4
	const TCHAR* const GGuardRelationshipLiteral = TEXT("player D_HT 10");    // 0x1063bc28

	// The Newscaster's two files. `0x1064aadc` / `0x1064aac0` are FORMAT strings and `0x105a0f80` is
	// the one vararg, so `UTIL_VarArgs` (`0x101d3730`) builds `vdata\system\Newscaster_Main.txt`.
	const TCHAR* const GNewscasterDir = TEXT("system");
	const TCHAR* const GNewscasterMainFile = TEXT("Newscaster_Main.txt");
	const TCHAR* const GNewscasterSideFile = TEXT("Newscaster_Side.txt");
	// `0x1064aab8` (the `strstr` on the key name), `0x1064aab0` (the story-name default) and
	// `0x105a18d4` (the key it is read from).
	const TCHAR* const GNewscasterStoryKeyToken = TEXT("Story");
	const TCHAR* const GNewscasterDefaultStoryName = TEXT("STORY");
	// `0x103a08b5` — `if (versions >= 4) DevWarning("too many versions in %s! skipping %s")`.
	constexpr int32 GNewscasterMaxVersions = 4;

	double SpeciesMisc10Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// `+0xa8 m_pPlayer`, `CBaseEntity`'s player self-downcast cache: non-null on exactly the player.
	bool SpeciesMisc10IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}

	// `0x103a07f0`, the per-`Story` parser. A file static because its retail argument is a
	// `KeyValues*` and the family's `.inl` is included inside `class FElysiumNpc`.
	//
	// Answers false for a record that must not be appended, exactly as retail's `XOR AL,AL` tail at
	// `103a09fd` does after it has released the record's strings.
	bool ParseNewscasterStoryKey(const FElysiumNpc& Npc, const FString& KeyName,
		const ElysiumKeyValues::FKvNode& Key, FElysiumNpc::FNewscasterStory& OutStory)
	{
		// `103a0803`: `strstr(key->GetName(), "Story")`. A key that does not carry the token warns
		// and falls through with a version count of zero, which the tail then refuses.
		if (!KeyName.Contains(GNewscasterStoryKeyToken))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Newscaster: invalid key! (%s)"), *KeyName);
			return false;
		}
		// `103a081f`: `GetString("Name", "STORY")` — an unnamed story is literally `STORY`.
		OutStory.Name = Key.Str(TEXT("Name"), FString(GNewscasterDefaultStoryName));
		// `103a0872`: `FindKey("Version")` then `GetNextKey()` — retail walks SIBLINGS from the first
		// `Version`, so a non-`Version` sibling after it is visited too and contributes nothing
		// (no `filename` means no increment). Walking the `Version` children answers the same set for
		// every authored file in the corpus and is what the reader below expresses.
		for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Key.Kids)
		{
			if (Child.Key != TEXT("version") || !Child.Value.IsValid())
			{
				continue;
			}
			// `103a0893` / `103a08a5`: `GetString("dependency", 0)` and `GetString("filename", 0)`.
			const FString* Dependency = Child.Value->Value(TEXT("dependency"));
			const FString* Filename = Child.Value->Value(TEXT("filename"));
			if (OutStory.Versions.Num() >= GNewscasterMaxVersions)
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("Newscaster: too many versions in %s! skipping %s"), *OutStory.Name,
					Filename != nullptr ? **Filename : TEXT(""));
				continue;
			}
			// `103a0916`: the version counter advances only inside the FILENAME block, so a version
			// with a dependency and no filename occupies no slot at all.
			if (Filename == nullptr || Filename->IsEmpty())
			{
				continue;
			}
			FElysiumNpc::FNewscasterStory::FVersion Version;
			Version.Dependency = Dependency != nullptr ? *Dependency : FString();
			Version.Filename = *Filename;
			OutStory.Versions.Add(MoveTemp(Version));
		}
		// `103a098d`..`103a0a0b`, the tail and the CORRECTION: the first version whose dependency is
		// absent, empty, or evaluates non-zero wins, and its INDEX is what `+0x24` holds. No version
		// qualifying answers false and the record is dropped.
		for (int32 Index = 0; Index < OutStory.Versions.Num(); ++Index)
		{
			const FString& Dependency = OutStory.Versions[Index].Dependency;
			if (Dependency.IsEmpty() || Npc.EvalNewscasterDependency(Dependency))
			{
				OutStory.SelectedVersion = Index;
				return true;
			}
		}
		return false;
	}
}

// =================================================================================================
// `CAI_BaseNPC::LeaveGrappleState` — `0x1026ce30`, 100 bytes.
// =================================================================================================

void FElysiumNpc::BaseLeaveGrappleState()
{
	// `1026ce30`: the `m_OnGrappleEnd` fire. The activator is the entity `+0x1538` resolves to, or
	// NULL when `+0x153c` is -1 or the handle fails its `0x1fff` index / `>>13` serial check.
	// UNCONDITIONAL — there is no grapple-type gate anywhere in this body.
	FireOutput(TEXT("OnGrappleEnd"), Grapple.Partner);
	// `1026ce78`: `CBaseCombatCharacter::LeaveGrappleState`.
	FElysiumCombatCharacter::LeaveGrappleState();
	// `1026ce82`: slot 416 (`vt+0x680`) `SetForceFrequentThink(false)`.
	SetForceFrequentThink(false);
	// `1026ce8d` -> `0x10007ea0`: `--m_iIsOblivious (+0x5bb4)`, clamped at 0, then the squad
	// reconnect `0x10009601`. Both UNCONDITIONAL, which is the arm the port was missing.
	NpcFlags.RemoveGrappleOblivious();
	ReconnectToSquad();
}

// =================================================================================================
// `CNPC_VChangBros` — `0x1036cab0`, `0x1036cbd0`, `0x1036cfa0`.
// =================================================================================================

bool FElysiumNpc::CheckForTeleport()
{
	// `1036cae5`: `m_ChangType != 1`. Type 1 answers false without reading anything else.
	if (ChangType == 1)
	{
		return false;
	}
	// `1036caee`: `0x103c6a20 >= _DAT_104ad9f8` (0.1). The percent RISES with damage (standing fact
	// one), so this is "lost a tenth of the bar since the mark".
	if (HealthPercentLostSinceRecord() >= GChangTeleportHealthLoss)
	{
		return true;
	}
	// `1036cb17`: the brother is resolved AFTER the health test, and the type is re-tested as 0 —
	// type 2 reaches here and is refused.
	const FElysiumNpc* Brother = GetOtherBrother();
	if (Brother == nullptr || ChangType != 0)
	{
		return false;
	}
	// `1036cb45`: `curtime - m_fFacingTime > GetFacingTimeToTeleport()`, strictly greater (the
	// decompiler's `a < b != (a == b)` idiom).
	const double Elapsed = SpeciesMisc10Now(*this) - FacingTime;
	return Elapsed > static_cast<double>(GetFacingTimeToTeleport());
}

bool FElysiumNpc::CheckForUnited()
{
	// `1036cc05`: no other brother answers false at once.
	const FElysiumNpc* Brother = GetOtherBrother();
	if (Brother == nullptr)
	{
		return false;
	}
	// `1036cc18`: `m_fLastUnitedAttackTime + _DAT_104ada48 (30.0) < curtime`, strictly.
	if (!(ChangLastUnitedAttackTime + GChangUnitedCooldownSeconds < SpeciesMisc10Now(*this)))
	{
		return false;
	}
	// `1036cc4c`: true UNLESS BOTH percents are below `_DAT_104ada4c` (0.5). The percent is
	// wounds/cap, so "below 0.5" is the HEALTHY half and two healthy brothers refuse the united
	// attack — the short-circuit means one hurt brother is enough to allow it.
	if (GetCurrHealthPercent() < GChangUnitedHealthFraction
		&& Brother->GetCurrHealthPercent() < GChangUnitedHealthFraction)
	{
		return false;
	}
	return true;
}

int32 FElysiumNpc::ChangBrosSelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
	const FVector& MeasureFromCm)
{
	// `1036cfe8`: the incumbent is seeded `-FLT_MAX` and replaced on a STRICTLY GREATER distance, so
	// the FARTHEST reachable ledge wins — the opposite comparison from `CNPC_VSheriffMan`'s
	// `0x103b0ab0`, which family Positions' `SelectLedgeNodeRule` carries.
	int32 Best = INDEX_NONE;
	float BestDistance = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		// `1036cffb`: `node->+0x5dc == 0x4653`, the ledge type.
		if (Nodes[Index].HintType != 0x4653)
		{
			continue;
		}
		// `1036d02b`: the full 3-D distance between the node's `GetAbsOrigin` and MY own. The
		// `CheckJumpPathToHintNode` gate at `1036d005` is applied by the caller below.
		const float Distance = static_cast<float>(FVector::Dist(MeasureFromCm,
			Nodes[Index].OriginCm));
		if (BestDistance < Distance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::ChangBrosSelectLedgeNode() const
{
	// `1036cfd8`: the global hint list `DAT_10925450`, walked through `+0x5d8`. Unlike the Sheriff's
	// twin there is NO closest-player gate in front of this one.
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	// `1036d005`: `CheckJumpPathToHintNode(this, node)` gates every candidate, in list order. It is
	// applied here rather than inside the rule because family Hints' `JumpPathSector` is a SEAM
	// answering 4, which closes the gate for every node — retail's own refusal for a brother already
	// in sector 4 — and the farthest-wins comparison has to stay readable under it.
	TArray<FHintWords> Accepted;
	TArray<int32> AcceptedIds;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (CheckJumpPathToHintNode(Nodes[Index]))
		{
			Accepted.Add(Nodes[Index]);
			AcceptedIds.Add(NodeIds[Index]);
		}
	}
	const int32 Pick = ChangBrosSelectLedgeNodeRule(Accepted, Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : AcceptedIds[Pick];
}

// =================================================================================================
// `CNPC_VCop::vfunc597` — `0x10372cc0`, slot 597's species prologue.
// =================================================================================================

void FElysiumNpc::CopSlot597Prologue(FElysiumEntity* Other)
{
	// `10372ce6`: the whole body is gated on the argument being the very entity `m_hClosestPlayer`
	// (`+0x628c`) resolves to. A stale handle resolves to null, and a null argument then matches it —
	// which is retail's own behaviour and is reproduced rather than guarded.
	const FElysiumEntity* Closest = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Other != Closest)
	{
		return;
	}
	// `10372cf8`: `m_hPursuitPlayer` (`+0x6664`) does NOT resolve to a live entity...
	const bool bPursuitLive = World != nullptr && World->Resolve(CopPursuitHandle) != nullptr;
	// `10372d20`: ...AND `GetState` (slot 464, `vt+0x740`) is 2 (COMBAT).
	if (!bPursuitLive && GetState() == EElysiumNpcState::Combat)
	{
		// `10372d2b`: CORRECTION — `param_1[0x2a]` is `+0xa8 m_pPlayer`, the player self-downcast
		// cache, not a "troika sub-object": the latch stores the PLAYER's own handle, and writes
		// `0xffffffff` when the argument carries no player record.
		CopPursuitHandle = SpeciesMisc10IsPlayer(*this, Other)
			? Other->Handle : FElysiumEntityHandle::Invalid();
	}
	// `10372d5b`: UNCONDITIONALLY for that same argument, `InputSetRelationship("Player D_HT 10", 0)`
	// — note the capital `P`, unlike `CNPC_VGuard1`'s literal.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(GCopRelationshipLiteral);
	Args.Activator = Other != nullptr ? Other->Handle : FElysiumEntityHandle::Invalid();
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
}

// =================================================================================================
// `CNPC_VGhoulCroucher::BurnPlayer` — `0x1037c090`, 228 bytes, no slot.
// =================================================================================================

void FElysiumNpc::BurnPlayer(FElysiumEntity* BurnTarget, float Damage)
{
	// `1037c0f8`: a null target does nothing at all — not even the damage.
	if (BurnTarget == nullptr)
	{
		return;
	}
	// `1037c105`: `GetModelPtr()` then `*(modelPtr + *(modelPtr + 0x104) + 4)` — the hitbox COUNT of
	// the model's hitbox set — and `BurnHitbox(target, i, 5.0, 0.5)` for each.
	//
	// SEAM: family Damage's `HitboxSetCount` answers 0 here, so the loop makes no passes. That IS
	// retail's arm for a model with no hitboxes, and the record below is what says the body ran.
	const int32 HitboxCount = HitboxSetCount();
	for (int32 Index = 0; Index < HitboxCount; ++Index)
	{
		BurnHitboxCalls.Add(FBurnHitboxCall{ BurnTarget->Handle, Index, GBurnHitboxSeconds,
			GBurnHitboxInterval });
	}
	// `1037c13d`: `CTakeDamageInfo(inflictor = GetActiveWeapon(), attacker = this, damage, bits = 8)`
	// built by `thunk_FUN_101c26d0` with the trailing `0, 0, -1`, then `TakeDamage(target, info)`.
	// The damage is the CALLER's — `0x1037bf3c` pushes `0x41200000` (10.0) — and `8` is the
	// `bitsDamageType`, not the amount, which the checklist's walk left unsaid.
	FElysiumDmg Dmg;
	Dmg.DmgMask = static_cast<uint32>(GBurnDamageBits);
	Dmg.BaseDamage = static_cast<int32>(Damage);
	Dmg.Source = Handle;
	Dmg.Inflictor = Inventory.ActiveWeapon;
	if (FElysiumCombatCharacter* Victim = BurnTarget->AsCombatCharacter())
	{
		Victim->TakeDamage(Dmg, this);
	}
}

// =================================================================================================
// `CNPC_VGuard1`'s hate latch — `0x1037e2d0`, 20 bytes, two callers.
// =================================================================================================

void FElysiumNpc::Guard1HatePlayer()
{
	// `1037e2d0`: the latch byte first...
	bGuard1HatesPlayer = true;
	// `1037e2da`: ...then `InputSetRelationship("player D_HT 10", 0)`. Lower-case `player`, which is
	// the only difference from `CNPC_VCop`'s literal and is reproduced verbatim because the input's
	// own parse is case-insensitive only for the value word.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(GGuardRelationshipLiteral);
	Args.Activator = World != nullptr ? World->PlayerHandle() : FElysiumEntityHandle::Invalid();
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
}

// =================================================================================================
// `CNPC_VManBat` — `0x1038e9c0` and `0x1038f020`, and the four seams they share.
// =================================================================================================

void FElysiumNpc::BeginSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude)
{
	// SEAM for `CBaseCombatCharacter::BeginSlowEntity`. Recorded, not applied.
	SlowEntityCalls.Add(FSlowEntityCall{ Victim, Magnitude, /*bBegin=*/true });
}

void FElysiumNpc::EndSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude)
{
	// SEAM for `CBaseCombatCharacter::EndSlowEntity`.
	SlowEntityCalls.Add(FSlowEntityCall{ Victim, Magnitude, /*bBegin=*/false });
}

FElysiumEntity* FElysiumNpc::PlayerInventorySlot0() const
{
	// SEAM for `0x1015d680(player, 0)` — the `+0x2308` handle array's slot 0. This runtime's
	// equivalent standing entity is the character's active weapon; a character with none answers
	// null, which is retail's own refusal for an empty slot.
	if (World == nullptr)
	{
		return nullptr;
	}
	FElysiumPlayer* Player = World->FindPlayer();
	return Player != nullptr ? World->Resolve(Player->Inventory.ActiveWeapon) : nullptr;
}

void FElysiumNpc::ManBatStartScreechCone(FElysiumEntity* ConeTarget)
{
	// `1038e9d2`: when the cone emitter at `+0x66a4` still resolves, `UTIL_Remove` it and null the
	// handle. Retail nulls it through `0x100a0ae0(handle, 0)`, so a stale handle is left alone.
	if (World != nullptr && World->Resolve(ManBatScreechCone) != nullptr)
	{
		RemoveNamedEntity(ManBatScreechCone);
		ManBatScreechCone = FElysiumEntityHandle::Invalid();
	}
	// `1038ea01`: spawn `Manbat_screechcone_emitter` at MY slot-217 origin, store the handle, attach
	// it at `Bip01 Jaw` with mode 1 and start it. The attach/start pair is guarded on the fresh
	// handle resolving, which a failed spawn does not.
	const int32 ConeIndex = CreateNamedEmitter(TEXT("Manbat_screechcone_emitter"),
		Origin / ElysiumMove::U, /*AttachMode*/ 1, Handle, TEXT("Bip01 Jaw"));
	StartNamedEmitter(ConeIndex);

	// `1038ea33`: everything below runs only for a non-null target that carries a PLAYER record.
	if (ConeTarget == nullptr || !SpeciesMisc10IsPlayer(*this, ConeTarget))
	{
		return;
	}
	// `1038ea60`: `UTIL_ScreenShake(slot 220 GetOrigin(), 2.5, 0.2, 3.0, 0.0, 0, 0)` around MY OWN
	// origin — `vt+0x370` is slot 220 on `this`, not on the target.
	ScreenShakeCalls.Add(FScreenShakeCall{ Origin / ElysiumMove::U, GManBatShakeAmplitude,
		GManBatShakeFrequency, GManBatShakeDuration, GManBatShakeRadius });

	// `1038ea69`: `param_1[0x27]` is `+0x9c`, the target's combat-character self-downcast — null for
	// anything that is not a combat character, and the whole rest of the body is gated on it.
	FElysiumCombatCharacter* Victim = ConeTarget->AsCombatCharacter();
	if (Victim == nullptr)
	{
		return;
	}
	// `1038eb2c`: `0x10344f80(victim, target.slot217 - my.slot217, 2, 0)` — the push, BEFORE the
	// slow. Both origins are slot 217.
	PushEntityCalls.Add(FPushEntityCall{ ConeTarget->Handle,
		(ConeTarget->Origin - Origin) / ElysiumMove::U, 2 });
	// `1038eb43`: `BeginSlowEntity(victim, 500.0)` only while the expiry still equals the 0.0
	// sentinel, so a second cone inside the window does not re-begin the slow.
	if (static_cast<double>(ManBatSlowedExpire) == GSlowExpireSentinel)
	{
		BeginSlowEntity(ConeTarget->Handle, GSlowEntityMagnitude);
	}
	// `1038eb85`: restamp unconditionally — `curtime + RandomFloat(15.0, 25.0)`.
	ManBatSlowedExpire = static_cast<float>(SpeciesMisc10Now(*this)
		+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(GManBatSlowSecondsMin,
			GManBatSlowSecondsMax));
	// `1038eb9a`: cache the VICTIM's handle (the combat character's `vt+4` accessor, which is the
	// entity itself — family Senses10's standing fact one).
	ManBatSlowedEntity = ConeTarget->Handle;

	// `1038ebb0`: `Manbat_player_emitter` at the TARGET's origin, `Bip01 Spine` mode 1, started —
	// only when `+0x669c` holds nothing.
	if (World == nullptr || World->Resolve(ManBatPlayerEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Manbat_player_emitter"),
			ConeTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, ConeTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// `1038ec4a`: the same shape for `Manbat_blast_player` at `+0x66a8`, at the SAME level rather
	// than nested inside the block above.
	if (World == nullptr || World->Resolve(ManBatBlastEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Manbat_blast_player"),
			ConeTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, ConeTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// `1038ed3c`: with the player's inventory slot 0 holding an entity AND `+0x66a0` stale, spawn
	// `HUD_Manbat_emitter`, attach it to THAT ITEM with mode `0xe` and an EMPTY bone name, start it,
	// and set the spawned emitter's `+0x4a1` byte to 1.
	const FElysiumEntity* Item = PlayerInventorySlot0();
	if (Item != nullptr && (World == nullptr || World->Resolve(ManBatHudEmitter) == nullptr))
	{
		const int32 Index = CreateNamedEmitter(TEXT("HUD_Manbat_emitter"), FVector::ZeroVector,
			/*AttachMode*/ 0xe, Item->Handle, TEXT(""));
		StartNamedEmitter(Index);
		// `+0x4a1` on the spawned emitter entity has no port counterpart and no reader in the
		// corpus; the emitter record carries the decision and the byte is recorded as unrecovered.
	}
	// `1038edd2`: whenever the player record stands, `player->+0x2454 |= 1`. It is inside the
	// `+0x9c` block, so a target that is the player but not a combat character never reaches it.
	bPlayerScreechConeBit = true;
}

void FElysiumNpc::ManBatReleaseSlowedEntity(bool bForce)
{
	// `1038f02c` -> `0x1038f290`: act only while `m_flSlowedExpire` is ABOVE the 0.0 sentinel. Family
	// Misc already carries that gate as `SlowedExpire()`; it is called, not restated.
	if (!SlowedExpire())
	{
		return;
	}
	// `1038f047`: forced, or the expiry has reached curtime.
	if (!bForce && !(static_cast<double>(ManBatSlowedExpire) <= SpeciesMisc10Now(*this)))
	{
		return;
	}
	// `1038f059`: zero the expiry first.
	ManBatSlowedExpire = 0.f;
	// `1038f06e`: `EndSlowEntity(victim->+0x9c, 500.0)` only while the handle resolves to a combat
	// character.
	FElysiumEntity* Victim = World != nullptr ? World->Resolve(ManBatSlowedEntity) : nullptr;
	if (Victim != nullptr && Victim->AsCombatCharacter() != nullptr)
	{
		EndSlowEntity(ManBatSlowedEntity, GSlowEntityMagnitude);
	}
	// `1038f096`, `1038f0e6`, `1038f142`: `UTIL_Remove` the three effects in THIS order — `+0x669c`,
	// `+0x66a8`, `+0x66a0` — each only while its handle resolves, and set each to -1 after.
	if (World != nullptr && World->Resolve(ManBatPlayerEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatPlayerEmitter);
	}
	ManBatPlayerEmitter = FElysiumEntityHandle::Invalid();
	if (World != nullptr && World->Resolve(ManBatBlastEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatBlastEmitter);
	}
	ManBatBlastEmitter = FElysiumEntityHandle::Invalid();
	if (World != nullptr && World->Resolve(ManBatHudEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatHudEmitter);
	}
	ManBatHudEmitter = FElysiumEntityHandle::Invalid();
	// `1038f19c`: clear bit 0 of the victim's `+0xa8` `+0x2454`, only while the handle resolves to
	// an entity that carries a player record.
	if (Victim != nullptr && SpeciesMisc10IsPlayer(*this, Victim))
	{
		bPlayerScreechConeBit = false;
	}
	// `1038f1e5`: `m_hSlowedEntity = -1`, OUTSIDE that guard — it is cleared even when the victim
	// never resolved.
	ManBatSlowedEntity = FElysiumEntityHandle::Invalid();
}

// =================================================================================================
// `CNPC_VNewscaster` — `0x103a0670` and `0x103a0ab0`.
// =================================================================================================

bool FElysiumNpc::NewscasterPlayerPresent() const
{
	// SEAM for `0x101cd9e0(1)` — `UTIL_PlayerByIndex(1)`: the index must be in `1..maxclients`, the
	// edict must not be free (`+0x4c`) and its `+0x40` unknown must resolve a base entity. This
	// runtime's one player is that entity.
	return World != nullptr && World->FindPlayer() != nullptr;
}

bool FElysiumNpc::EvalNewscasterDependency(const FString& Source) const
{
	// `0x1000134d` -> `thunk_FUN_101d2850`: `PyRun_String(src, Py_eval_input, __main__, __main__)`,
	// whose int value is compared against zero. `EvalCondition` is this runtime's same interpreter
	// and its error-to-false collapse is retail's own `PyErr_Print` + `return 0`.
	if (World == nullptr)
	{
		return false;
	}
	return const_cast<FElysiumEntityWorld*>(World)->EvalCondition(Source, Handle,
		World->PlayerHandle()).ToBool();
}

void FElysiumNpc::LoadNewscasterStories()
{
	// `103a0abd`: tear BOTH queues down first — family Species' `FUN_103a0d50`.
	FUN_103a0d50();

	// `103a0ac2` / `103a0bb8`: `UTIL_VarArgs("%sNewscaster_Main.txt", "vdata\system\")`. CORRECTION:
	// the strings are FORMATS with a `%s`, not `\s`-prefixed paths.
	const FString Dir = FElysiumContentPaths::VdataDir() / GNewscasterDir;
	auto LoadQueue = [&Dir](const TCHAR* File, TArray<FElysiumNpc::FNewscasterStory>& OutQueue,
		const FElysiumNpc& Npc)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Dir / File)))
		{
			// CRASH GUARD, named: retail's `103a0ba7` reads `[EDI+4]` with `EDI == 0` when the
			// KeyValues load fails, so a missing file faults. Nothing of retail's behaviour is
			// reachable past that point, so refusing the queue changes no observable order.
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("Newscaster: %s is missing (retail faults here)"), File);
			return;
		}
		const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
		if (!Root.IsValid())
		{
			return;
		}
		// `103a0aef`: `root->GetFirstSubKey()` then `GetNextKey()` — every child of the file's one
		// top-level block (`NewsData`), in authored order.
		const ElysiumKeyValues::FKvNode* Data = Root->Kids.Num() == 1 && Root->Kids[0].Value.IsValid()
			? Root->Kids[0].Value.Get() : Root.Get();
		for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Data->Kids)
		{
			if (!Child.Value.IsValid())
			{
				continue;
			}
			// `103a0b00`: the ten-word scratch row is zeroed per key, so nothing leaks between rows.
			FElysiumNpc::FNewscasterStory Story;
			// `103a0b15`: appended ONLY when the parser answers true.
			if (ParseNewscasterStoryKey(Npc, Child.Key, *Child.Value, Story))
			{
				OutQueue.Add(MoveTemp(Story));
			}
		}
	};
	LoadQueue(GNewscasterMainFile, NewscasterMainStories, *this);
	LoadQueue(GNewscasterSideFile, NewscasterSideStories, *this);
	// `103a0cae`: the loaded flag last.
	bNewscasterStoryActive = true;
}

void FElysiumNpc::PlayNextNewscasterStory()
{
	// `103a0678`: with the loaded flag clear, the player gate decides — and a missing player returns
	// WITHOUT loading, so the next call tries again.
	if (!bNewscasterStoryActive)
	{
		if (!NewscasterPlayerPresent())
		{
			return;
		}
		LoadNewscasterStories();
		// `103a069b`: seed BOTH cursors with `RandomInt(0, count - 1)`. An empty queue hands
		// `RandomInt(0, -1)`, which retail answers with 0.
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		NewscasterMainCursor = NewscasterMainStories.Num() > 0
			? Rng.RandRange(0, NewscasterMainStories.Num() - 1) : 0;
		NewscasterSideCursor = NewscasterSideStories.Num() > 0
			? Rng.RandRange(0, NewscasterSideStories.Num() - 1) : 0;
	}
	// `103a06c8`: `IsInDialog` (`0x102c1170`) refuses the whole rest of the body.
	if (Dialogue.bInDialog)
	{
		return;
	}
	const int32 MainCount = NewscasterMainStories.Num();
	const int32 SideCount = NewscasterSideStories.Num();
	// `103a06dd`: both queues empty does nothing.
	if (MainCount + SideCount == 0)
	{
		return;
	}
	// `103a06ef`: `RandomInt(0, count0 + count1)` — an INCLUSIVE upper bound, so the roll can equal
	// the sum and land on the side queue once more often than the counts alone would say. Retail's
	// arithmetic, reproduced.
	const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, MainCount + SideCount);
	NewscasterPlayingSide = (Roll < MainCount) ? 1 : 0;

	// `103a0719`: a zero `+0x668c` OR an empty main queue takes the SIDE cursor. Note the polarity —
	// `+0x668c` non-zero selects the MAIN queue here, which is the opposite of the name family
	// Species gave it for the debug overlay (`0x103a0ff0` highlights a MAIN row when `+0x668c == 0`).
	// Both are retail: the overlay and this body read the same word with opposite senses, and that
	// is a retail inconsistency rather than a port error.
	const TArray<FNewscasterStory>* Queue = nullptr;
	int32 Index = 0;
	if (NewscasterPlayingSide == 0 || MainCount == 0)
	{
		// `103a0730`: an empty SIDE queue returns early, without advancing anything.
		if (SideCount == 0)
		{
			return;
		}
		NewscasterSideCursor = (NewscasterSideCursor + 1 >= SideCount)
			? 0 : NewscasterSideCursor + 1;
		Index = NewscasterSideCursor;
		Queue = &NewscasterSideStories;
	}
	else
	{
		// `103a0762`: the MAIN cursor, advanced modulo its count the same way.
		NewscasterMainCursor = (NewscasterMainCursor + 1 >= MainCount)
			? 0 : NewscasterMainCursor + 1;
		Index = NewscasterMainCursor;
		Queue = &NewscasterMainStories;
	}
	// `103a0789`: `record + 8 + selected * 8` — the SELECTED VERSION's filename, and nothing when it
	// is null.
	if (!Queue->IsValidIndex(Index))
	{
		return;
	}
	const FNewscasterStory& Story = (*Queue)[Index];
	if (!Story.Versions.IsValidIndex(Story.SelectedVersion))
	{
		return;
	}
	const FString& File = Story.Versions[Story.SelectedVersion].Filename;
	if (File.IsEmpty())
	{
		return;
	}
	// `103a07a3`: `0x102c0520(this, filename, 0, 0)` — `OnDialogFilePlayed`.
	NewscasterPlayedFiles.Add(File);
	// `0x102c0520` is the spoken-line player, and the port carries its TALKING half only
	// (`OnDialogFilePlayed`, `ElysiumNpc.cpp:2177`): the `scripted_scene` create and the `sound\`
	// prefix strip have no counterpart here. The duration retail passes from this call site is 0.
	OnDialogFilePlayed(0.0);
}
