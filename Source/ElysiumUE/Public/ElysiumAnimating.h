#pragma once

#include "CoreMinimal.h"
// By value: CBaseAnimating owns one event cursor per polled channel.
#include "ElysiumAnimEvent.h"
#include "ElysiumEntity.h"

class USkeletalMeshComponent;

struct FElysiumDisposition;
struct FElysiumClipPhase;      // ElysiumAnimationIntent.h — one channel's place on one clip

// FElysiumAnimating — CBaseAnimating. Everything that owns a skeletal body: standing it, moving
// it with the entity, gating it on dormancy, and playing clips on it. NPCs and the player share
// this because in VtMB they share the class.

class FElysiumAnimating : public FElysiumEntity
{
public:
	// `skin` — a material family swap. One VtMB datamap record flagged both KEY and INPUT with a
	// null inputFunc, so the keyvalue, the wire and `.skin =` are the same direct write
	// (`docs/vtmb/entity_io.md`). Carried here for the whole chain; only the prop leaf paints with it.
	int32 Skin = 0;

	// `default_disposition` — the emotional stance that selects the standing animation set through
	// `vdata/system/dispositiontable.txt`. Runtime state, not a spawn-time constant:
	// `SetDisposition` (2,510 calls, 2,467 of them a `.dlg` line's action) writes it mid-conversation.
	FString Disposition;
	// SetDisposition's second argument. `default_disposition` starts at level 1; the resolved level
	// persists separately because several names author distinct expression/stance rows.
	int32 DispositionLevel = 1;

	// The standing skeletal body, or null (a bodiless entity, `elysium.NpcBodies 0`, or a missing
	// glb). Owned by the map actor; the world tears it down. This class only gates and moves it.
	USkeletalMeshComponent* Visual = nullptr;

	// Stand this entity's body at its current origin/facing, playing the idle its disposition
	// selects. Called from the leaf's Spawn(); no-op with no embodiment, no model, or bodies off.
	void BuildBody();
	/** Admission/attachment metadata only; the source Model write has already occurred. */
	bool PrepareCharacterVisual();
	void InvalidateCharacterVisualRequest();
	void CompletePreparedCharacterVisual(uint64 Generation, const FString& ModelId);

	// The animation seam, implemented once for every character.
	virtual bool PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) override;
	virtual bool PlayAnimSegment(const struct FElysiumClipSegment& Segment,
		float* OutSeconds = nullptr) override;
	virtual void ReleaseAnimSegment() override;
	virtual bool PreloadAnimClip(const FString& ClipName) override;
	virtual bool PlayCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) override;
	virtual bool PreloadCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName) override;
	virtual bool SeekCinematicClip(float PositionSeconds) override;
	virtual void StopCinematicClip() override;
	virtual int32 SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
		TArray<FString>* OutMissing = nullptr) override;
	virtual bool SetMouthOpen(float Open) override;
	virtual bool GetPhonemeFilter(float& OutMin, float& OutMax) const override;
	virtual bool ResetAnimToIdle() override;
	virtual bool SetDispositionName(const FString& NewDisposition) override;
	virtual bool SetDisposition(const FString& NewDisposition, int32 NewLevel);

	// Dialogue owns the talking/default face switch. The current baseline is also exposed for the
	// dialogue lipsync compositor, which layers phonemes over it instead of erasing it.
	void SetDispositionTalking(bool bTalking);
	bool IsDispositionTalking() const { return bDispositionTalking; }
	void AccumulateDispositionFacialPose(TMap<FString, float>& InOutPose) const;

	// Body follow: SetOrigin/SetAngles move and re-face the component; SetModel rebuilds it.
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	// R6 — a ScriptHidden/dead character is undrawn and stops ticking its clip.
	virtual void OnDormancyChanged() override;
	// `CBaseEntity::ShouldTransmit` `0x100ab020`'s `EF_NODRAW` (`m_fEffects & 0x40`) early-out: an
	// entity carrying it is never sent to the client, so its body is never drawn — but it is still a
	// server entity, so its animation keeps advancing, its events keep firing and its hull keeps
	// moving. The base carries no effects word and always transmits; `FElysiumNpc` answers off its
	// kernel `EffectsWord`.
	virtual bool IsTransmitted() const { return true; }
	// Re-apply the draw gate after a write to what `IsTransmitted` reads.
	void RefreshVisualGate() { GateVisual(); }
	// The body a camera shot's `Bone: Bip01 Head` attach point resolves against.
	virtual USkeletalMeshComponent* GetSkeletalBody() const override { return Visual; }

	// The `parentname` attach point for a character (an ornament or an emitter worn on an NPC).
	// A point character has no brush body, so the standing skeletal body is the only primitive
	// there is; null for a bodiless character, which cannot be a parent.
	virtual UPrimitiveComponent* GetAttachBody() const override { return Visual; }

	// Historical accessor name; returns the full native model ID derived from Model.
	// The source Model spelling remains unchanged. Bare stems belong to preparation/debug only.
	FString ModelStem() const;
	// Visual metadata refresh after the map publishes prepared expression views. No entity I/O,
	// gameplay state, clock or animation-event cursor is changed by this preparation hook.
	void RefreshPreparedExpressions() { RefreshDispositionExpression(); }

	// Whether the events of ONE named clip — `(OwnerStem, Label)`, the same identity the cursor is
	// keyed by — can reach `HandleAnimEvent` on this body: true only when a channel the pass above
	// polls is publishing a phase for that clip. A producer that would otherwise stand a scheduled
	// estimate down in favour of the clip's own event has to ask, because a timeline nothing walks
	// fires nothing and would swallow the work the estimate was covering.
	//
	// It is the clip's question and not the body's. A body playing some OTHER clip on the polled
	// channel walks that clip's timeline and never reaches this one's, so "a phase is published"
	// would be the wrong answer to give a caller that named a clip.
	//
	// An ordinary negative on a bodiless character, in a headless world, on an idle channel, and on a
	// channel standing on a different clip.
	bool HasLiveAnimEventDispatch(const FString& OwnerStem, const FString& Label) const;

	// The same question, answered WITH the record: the phase a polled channel is publishing for one
	// named clip, or false when no channel stands on it. The melee swing's contact walk needs the
	// cycle itself — it walks the interval the clip advanced through, not merely whether it is on
	// screen — so the two callers share one lookup rather than one asking twice.
	bool GetLiveClipPhase(const FString& OwnerStem, const FString& Label,
		FElysiumClipPhase& Out) const;

protected:
	virtual void InstallPreparedCharacterVisual();
	virtual void OnPreparedVisualAttached() {}
	void RestoreModelChildren();
	uint64 CharacterVisualGeneration = 0;
	struct FCarriedModelChild
	{
		TWeakObjectPtr<class USceneComponent> Component;
		FTransform RelativeTransform;
		FName Socket;
	};
	TArray<FCarriedModelChild> PendingModelChildren;
	// Which of the three standing idles a disposition's stance set poses. Virtual because only the
	// NPC chain carries VtMB's stance machine — the `+0x98` self-pointer that reaches it is set in
	// `CAI_BaseNPCTroika`'s constructor, so the player has none. The base answer spreads the pick by
	// entity index, which keeps a crowd from posing identically and survives a reload.
	virtual int32 IdleVariant() const { return FMath::Max(0, Handle.Index); }
	bool CommitDisposition(const FString& NewDisposition, int32 NewLevel, bool& bOutChanged,
		FElysiumDisposition* OutOld = nullptr, FElysiumDisposition* OutNew = nullptr);
	void RefreshDispositionExpression();
	// Apply this entity's own draw gate to its body. Virtual because the player's surface is owned by
	// the pawn, not by this entity: the player override publishes the gate through the embodiment and
	// lets the pawn AND it with the camera's eligibility, so the two never race on one flag.
	virtual void GateVisual();

private:
	bool bDispositionTalking = false;
	TMap<FString, float> DispositionFacialPose;

public:
	// The generated slot surface and the hand-written slot bodies of this class's retail node
	// (0019 story 5 step 6).
	#include "ElysiumAnimatingSlots.inl"
	#include "ElysiumAnimatingSlotBodies.inl"
};
