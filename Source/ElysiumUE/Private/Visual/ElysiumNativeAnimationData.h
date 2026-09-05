#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumNativeAnimationData.generated.h"

class UElysiumCastData;
class UElysiumBodyData;
class UElysiumClipData;
class UAnimSequence;
class UBlendSpace;
class USkeletalMesh;
struct FStreamableHandle;
struct FElysiumBodyAnimationRef;
struct FElysiumAsyncModelAdmission;

/** Preload owns asset I/O. Runtime lookup only reads prepared native assets and values. */
UCLASS()
class UElysiumNativeAnimationData : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;
	void ReleasePrepared();
	void ReleaseEpoch(uint64 Epoch);
	/** Called during body/map preparation, never from a per-frame selector. */
	TSharedPtr<FStreamableHandle> Prepare(const FString& Model, FString& OutError);
	TSharedPtr<FStreamableHandle> PrepareMany(const TArray<FString>& Models, FString& OutError);
	TSharedPtr<FStreamableHandle> PrepareMapModels(const TArray<FString>& Models, FString& OutError,
		uint64 Epoch = 0, const TArray<FString>& CinematicModels = {});
	static bool FinishPreparation(const TSharedPtr<FStreamableHandle>& Handle, FString& OutError);
	/** Nonblocking runtime admission. Completion is queued; cancellation does not call it.
	 * OwnerEpoch is MapActor's native preparation epoch, not the entity world's uint32 epoch.
	 */
	uint64 AdmitModelAsync(const FString& ModelId, uint64 OwnerEpoch,
		TFunction<void(bool, const FString&)> Completion, FString& OutError);
	void CancelModelAdmission(uint64 RequestId);
	int32 NumPendingModelAdmissions() const { return ModelAdmissions.Num(); }
	bool IsModelReady(const FString& ModelId) const;
	bool OwnsPreparationEpoch(uint64 Epoch) const { return PreparedEpoch == Epoch; }
	/** Read-only presence test against the loaded cast. Call preparation before querying. */
	bool KnowsModel(const FString& Model) const;
	/** Resident cast only; this accessor never loads. */
	UElysiumCastData* PreparedCast() const { return Cast.Get(); }
	const UElysiumBodyData* Body(const FString& Model) const;
	USkeletalMesh* Mesh(const FString& Model, FString& OutError) const;
	/** An empty root is accepted only for a single-owner cinematic. Lookup never loads. */
	const UElysiumBodyData* CinematicBody(const FString& Model, const FString& Root) const;
	const FElysiumNpcClipSet* Vocabulary(const FString& Model);
	TSharedPtr<const FElysiumBlendTable> BlendTable(const FString& Owner);
	UAnimSequence* Sequence(const FString& Owner, const FString& Label) const;
	UBlendSpace* BlendSpace(const FString& Owner, const FString& Label) const;
	const UElysiumClipData* ClipData(const FString& Owner, const FString& Label) const;
	TSharedPtr<const FElysiumBlendTable> BlendTable(const UElysiumBodyData* Owner);
	UAnimSequence* Sequence(const UElysiumBodyData* Owner, const FString& Label) const;
	UBlendSpace* BlendSpace(const UElysiumBodyData* Owner, const FString& Label) const;
	const UElysiumClipData* ClipData(const UElysiumBodyData* Owner, const FString& Label) const;
	/** Binds an already-resolved reference. It does not discover names or load packages. */
	UAnimSequence* Sequence(const FElysiumBodyAnimationRef& Ref) const;
	UBlendSpace* BlendSpace(const FElysiumBodyAnimationRef& Ref) const;

private:
	void AdvanceModelAdmission(uint64 RequestId);
	void LoadAdmissionPaths(uint64 RequestId, const TArray<FSoftObjectPath>& Paths);
	void FinishModelAdmission(uint64 RequestId, const FString& Error);
	bool AdmissionAssetsCompiling(const FElysiumAsyncModelAdmission& Request) const;
	TMap<uint64, TSharedPtr<FElysiumAsyncModelAdmission>> ModelAdmissions;
	uint64 NextAdmissionId = 0;
	uint64 AdmissionGeneration = 0;
	bool LoadCast(FString& OutError);
	const UElysiumBodyData* PreparedBody(const TSoftObjectPtr<UElysiumBodyData>& Ref) const;
	UPROPERTY(Transient) TObjectPtr<UElysiumCastData> Cast;
	UPROPERTY(Transient) TMap<FString,TObjectPtr<UElysiumBodyData>> Bodies;
	TArray<TSharedPtr<FStreamableHandle>> Loads;
	uint64 PreparedEpoch = 0;
	TMap<FString,TSharedPtr<FElysiumNpcClipSet>> Vocabularies;
	TMap<FString,TSharedPtr<FElysiumBlendTable>> Tables;
};
