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
	/** Read-only presence test against the loaded cast. Call preparation before querying. */
	bool KnowsModel(const FString& Model) const;
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
	bool LoadCast(FString& OutError);
	const UElysiumBodyData* PreparedBody(const TSoftObjectPtr<UElysiumBodyData>& Ref) const;
	UPROPERTY(Transient) TObjectPtr<UElysiumCastData> Cast;
	UPROPERTY(Transient) TMap<FString,TObjectPtr<UElysiumBodyData>> Bodies;
	TArray<TSharedPtr<FStreamableHandle>> Loads;
	uint64 PreparedEpoch = 0;
	TMap<FString,TSharedPtr<FElysiumNpcClipSet>> Vocabularies;
	TMap<FString,TSharedPtr<FElysiumBlendTable>> Tables;
};
