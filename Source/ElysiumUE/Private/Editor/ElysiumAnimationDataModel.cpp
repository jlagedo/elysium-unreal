#include "Editor/ElysiumAnimationDataModel.h"

#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Features/IModularFeatures.h"
#include "UObject/Package.h"

namespace
{
	/** Keep source quaternion keys in the stock native model. The Sequencer model converts
	 * through float Euler channels and removes nearly-constant channels before compression. */
	class FModelForBakedAnimations final : public UE::Anim::DataModel::IAnimationDataModels
	{
		virtual UClass* GetModelClass(UAnimSequenceBase* Asset) const override
		{
			return Asset && Asset->IsA<UAnimSequence>()
				&& Asset->GetOutermost()->GetName().StartsWith(TEXT("/ElysiumBaked/Models/"))
				?UAnimDataModel::StaticClass():nullptr;
		}
	};
	FModelForBakedAnimations BakedAnimationModel;
}
#endif

void ElysiumAnimationDataModel::Register()
{
#if WITH_EDITOR
	using IModels=UE::Anim::DataModel::IAnimationDataModels;
	auto& Features=IModularFeatures::Get();
	const FName Kind=IModels::GetModularFeatureName();
	const auto Existing=Features.GetModularFeatureImplementations<IModels>(Kind);
	if (Existing.Contains(&BakedAnimationModel)) return;
	// UE's registry enumerates this feature newest-first, but its animation-model factory
	// lets the LAST non-null answer win. Preserve every existing provider's relative order
	// while placing our scoped answer last. Later generic registrations cannot displace it.
	// Outside the generated model mount we return null, preserving the original selection.
	for (auto* Provider : Existing) Features.UnregisterModularFeature(Kind,Provider);
	Features.RegisterModularFeature(Kind,&BakedAnimationModel);
	for (int32 Index=Existing.Num()-1; Index>=0; --Index) Features.RegisterModularFeature(Kind,Existing[Index]);
#endif
}

void ElysiumAnimationDataModel::Unregister()
{
#if WITH_EDITOR
	IModularFeatures::Get().UnregisterModularFeature(
		UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(),&BakedAnimationModel);
#endif
}
