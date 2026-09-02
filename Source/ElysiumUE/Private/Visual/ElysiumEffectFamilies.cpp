#include "ElysiumEffectFamilies.h"

#include "Misc/Paths.h"

const FElysiumEffectFamily* UElysiumEffectFamilies::Match(const FString& RootName,
	const FElysiumParticleTree& Tree) const
{
	for (const FElysiumEffectFamily& Family : Families)
	{
		for (const FString& Name : Family.RootNames)
		{
			if (Name.Equals(RootName, ESearchCase::IgnoreCase))
			{
				return &Family;
			}
		}
	}
	for (const FElysiumEffectFamily& Family : Families)
	{
		for (const FElysiumParticleNode& Node : Tree.Nodes)
		{
			if (!Node.bDraws || !Node.Sprite.IsSet())
			{
				continue;
			}
			const FString Stem = FPaths::GetBaseFilename(Node.Sprite.Id);
			for (const FString& Name : Family.SpriteNames)
			{
				if (Name.Equals(Stem, ESearchCase::IgnoreCase)
					|| (TEXT("T_") + Name).Equals(Stem, ESearchCase::IgnoreCase))
				{
					return &Family;
				}
			}
		}
	}
	return nullptr;
}
