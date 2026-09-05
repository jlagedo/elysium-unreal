#include "ElysiumEffectActor.h"

#include "ElysiumEffectFamilies.h"
#include "ElysiumFog.h"
#include "ElysiumWorldServices.h"   // FElysiumWeatherEmitterState -- the leaf's publish

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEffect, Log, All);

namespace
{
	// §5.3's ramp slot table, in order: ramp `r` owns entries `r x RampSamples ..` of
	// `User.Leaf<ii>.Ramps` as a (lo, hi, 0) lookup table over normalized age; Burst keeps raw
	// (t, lo, hi) keyframes and the rate maximum in its block.
	enum class ERamp : int32
	{
		Size, Width, Height, Rotation, Red, Green, Blue, Color, Mask, Refract,
		RadiusSpeed, ThetaSpeed, PhiSpeed, XSpeed, YSpeed, ZSpeed, ElevationSpeed, ParentSpeed,
		Rate, Burst, SpawnRadius, Theta, Phi, X, Y, Z, Elevation, SpawnRotation,
		SpawnWidth, SpawnHeight, SpawnSize, SpawnRed, SpawnGreen, SpawnBlue, SpawnColor,
		SpawnMask, SpawnRefract,
		Count
	};
	static_assert(static_cast<int32>(ERamp::Count) == ElysiumEffectAssets::RampCount,
		"the ramp slot table must match the floor's 37 ramps");

	const TCHAR* const GRampNames[] =
	{
		TEXT("Size"), TEXT("Width"), TEXT("Height"), TEXT("Rotation"), TEXT("Red"), TEXT("Green"),
		TEXT("Blue"), TEXT("Color"), TEXT("Mask"), TEXT("Refract"), TEXT("RadiusSpeed"),
		TEXT("ThetaSpeed"), TEXT("PhiSpeed"), TEXT("XSpeed"), TEXT("YSpeed"), TEXT("ZSpeed"),
		TEXT("ElevationSpeed"), TEXT("ParentSpeed"), TEXT("Rate"), TEXT("Burst"),
		TEXT("SpawnRadius"), TEXT("Theta"), TEXT("Phi"), TEXT("X"), TEXT("Y"), TEXT("Z"),
		TEXT("Elevation"), TEXT("SpawnRotation"), TEXT("SpawnWidth"), TEXT("SpawnHeight"),
		TEXT("SpawnSize"), TEXT("SpawnRed"), TEXT("SpawnGreen"), TEXT("SpawnBlue"),
		TEXT("SpawnColor"), TEXT("SpawnMask"), TEXT("SpawnRefract"),
	};
	static_assert(UE_ARRAY_COUNT(GRampNames) == ElysiumEffectAssets::RampCount,
		"every ramp slot needs its pin name");

	// The ramp a slot reads for each table row: the node's own for the particle-age ramps, the
	// reaching spawn block's for the spawn-age ramps. A ramp the tree does not carry is empty and
	// packs as "unused" (t = -1 on every keyframe) -- the floor holds its unity default.
	const TArray<FElysiumRampKey>& RampOf(const FElysiumParticleNode& Node, ERamp Ramp)
	{
		static const TArray<FElysiumRampKey> Empty;
		const FElysiumParticleSpawn& S = Node.Spawn;
		switch (Ramp)
		{
		case ERamp::Size:           return Node.SizeCm;
		case ERamp::Width:          return Node.Width;
		case ERamp::Height:         return Node.Height;
		case ERamp::Rotation:       return Node.RotationDeg;
		case ERamp::Red:            return Node.Red;
		case ERamp::Green:          return Node.Green;
		case ERamp::Blue:           return Node.Blue;
		case ERamp::Color:          return Node.Color;
		case ERamp::Mask:           return Node.Mask;
		case ERamp::Refract:        return Node.Refract;
		case ERamp::RadiusSpeed:    return Node.RadiusSpeedCmS;
		case ERamp::ThetaSpeed:     return Node.ThetaSpeedDegS;
		case ERamp::PhiSpeed:       return Node.PhiSpeedDegS;
		case ERamp::XSpeed:         return Node.XSpeedCmS;
		case ERamp::YSpeed:         return Node.YSpeedCmS;
		case ERamp::ZSpeed:         return Node.ZSpeedCmS;
		case ERamp::ElevationSpeed: return Node.ElevationSpeedCmS;
		case ERamp::ParentSpeed:    return Node.ParentSpeed;
		case ERamp::Rate:           return Node.bHasSpawn ? S.Rate : Empty;
		case ERamp::Burst:          return Node.bHasSpawn ? S.Burst : Empty;
		case ERamp::SpawnRadius:    return Node.bHasSpawn ? S.RadiusCm : Empty;
		case ERamp::Theta:          return Node.bHasSpawn ? S.ThetaDeg : Empty;
		case ERamp::Phi:            return Node.bHasSpawn ? S.PhiDeg : Empty;
		case ERamp::X:              return Node.bHasSpawn ? S.XCm : Empty;
		case ERamp::Y:              return Node.bHasSpawn ? S.YCm : Empty;
		case ERamp::Z:              return Node.bHasSpawn ? S.ZCm : Empty;
		case ERamp::Elevation:      return Node.bHasSpawn ? S.ElevationCm : Empty;
		case ERamp::SpawnRotation:  return Node.bHasSpawn ? S.RotationDeg : Empty;
		case ERamp::SpawnWidth:     return Node.bHasSpawn ? S.Width : Empty;
		case ERamp::SpawnHeight:    return Node.bHasSpawn ? S.Height : Empty;
		case ERamp::SpawnSize:      return Node.bHasSpawn ? S.Size : Empty;
		case ERamp::SpawnRed:       return Node.bHasSpawn ? S.Red : Empty;
		case ERamp::SpawnGreen:     return Node.bHasSpawn ? S.Green : Empty;
		case ERamp::SpawnBlue:      return Node.bHasSpawn ? S.Blue : Empty;
		case ERamp::SpawnColor:     return Node.bHasSpawn ? S.Color : Empty;
		case ERamp::SpawnMask:      return Node.bHasSpawn ? S.Mask : Empty;
		case ERamp::SpawnRefract:   return Node.bHasSpawn ? S.Refract : Empty;
		default:                    return Empty;
		}
	}

	// Angle ramps interpolate the shortest way round: unwrap each keyframe against the previous one
	// so the table holds a continuous angle and the floor lerps it plainly.
	bool IsAngleRamp(ERamp Ramp)
	{
		return Ramp == ERamp::Rotation || Ramp == ERamp::Theta || Ramp == ERamp::Phi || Ramp == ERamp::SpawnRotation;
	}

	// A ramp the tree does not carry holds its unity default: the multiplicative keys are 1,
	// everything else 0.
	float RampDefault(ERamp Ramp)
	{
		switch (Ramp)
		{
		case ERamp::Width: case ERamp::Height: case ERamp::Red: case ERamp::Green: case ERamp::Blue:
		case ERamp::Color: case ERamp::Mask: case ERamp::Refract: case ERamp::ParentSpeed:
		case ERamp::SpawnWidth: case ERamp::SpawnHeight: case ERamp::SpawnSize: case ERamp::SpawnRed:
		case ERamp::SpawnGreen: case ERamp::SpawnBlue: case ERamp::SpawnColor: case ERamp::SpawnMask:
		case ERamp::SpawnRefract:
			return 1.f;
		default:
			return 0.f;
		}
	}

	void SampleRamp(const TArray<FElysiumRampKey>& InKeys, ERamp Ramp, FVector* Out)
	{
		using namespace ElysiumEffectAssets;
		if (InKeys.Num() == 0)
		{
			const float D = RampDefault(Ramp);
			for (int32 J = 0; J < RampSamples; ++J)
			{
				Out[J] = FVector(D, D, 0.0);
			}
			return;
		}
		TArray<FElysiumRampKey> Keys = InKeys;
		Keys.Sort([](const FElysiumRampKey& A, const FElysiumRampKey& B) { return A.T < B.T; });
		if (IsAngleRamp(Ramp))
		{
			for (int32 K = 1; K < Keys.Num(); ++K)
			{
				Keys[K].Lo = Keys[K - 1].Lo + FMath::FindDeltaAngleDegrees(Keys[K - 1].Lo, Keys[K].Lo);
				Keys[K].Hi = Keys[K - 1].Hi + FMath::FindDeltaAngleDegrees(Keys[K - 1].Hi, Keys[K].Hi);
			}
		}
		for (int32 J = 0; J < RampSamples; ++J)
		{
			const float T = static_cast<float>(J) / static_cast<float>(RampSamples - 1);
			float Lo = Keys.Last().Lo, Hi = Keys.Last().Hi;
			if (T <= Keys[0].T)
			{
				Lo = Keys[0].Lo; Hi = Keys[0].Hi;
			}
			else
			{
				for (int32 K = 1; K < Keys.Num(); ++K)
				{
					if (T <= Keys[K].T)
					{
						const float F = (T - Keys[K - 1].T) / FMath::Max(Keys[K].T - Keys[K - 1].T, 1e-6f);
						Lo = FMath::Lerp(Keys[K - 1].Lo, Keys[K].Lo, F);
						Hi = FMath::Lerp(Keys[K - 1].Hi, Keys[K].Hi, F);
						break;
					}
				}
			}
			Out[J] = FVector(Lo, Hi, 0.0);
		}
	}

	void PackRamps(const FElysiumParticleNode& Node, TArray<FVector>& Out)
	{
		using namespace ElysiumEffectAssets;
		Out.SetNum(RampEntries);
		for (int32 R = 0; R < RampCount; ++R)
		{
			const TArray<FElysiumRampKey>& Keys = RampOf(Node, static_cast<ERamp>(R));
			FVector* Block = &Out[R * RampSamples];
			if (R == BurstRamp)
			{
				for (int32 J = 0; J < RampSamples; ++J)
				{
					Block[J] = FVector(-1.0, 0.0, 0.0);
				}
				for (int32 K = 0; K < FMath::Min(Keys.Num(), RampKeyframes); ++K)
				{
					Block[K] = FVector(Keys[K].T, Keys[K].Lo, Keys[K].Hi);
				}
				float RateMax = 0.f;
				for (const FElysiumRampKey& Key : RampOf(Node, ERamp::Rate))
				{
					RateMax = FMath::Max3(RateMax, Key.Lo, Key.Hi);
				}
				Block[RampSamples - 1] = FVector(RateMax, 0.0, 0.0);
			}
			else
			{
				SampleRamp(Keys, static_cast<ERamp>(R), Block);
			}
		}
	}

	// The midpoint of a ramp's first keyframe -- what a family pin reads off a ramp.
	float RampScalar(const TArray<FElysiumRampKey>& Keys, float Default)
	{
		return Keys.Num() > 0 ? 0.5f * (Keys[0].Lo + Keys[0].Hi) : Default;
	}

	FName SlotName(int32 Slot, const TCHAR* Field)
	{
		return FName(*FString::Printf(TEXT("User.Leaf%02d.%s"), Slot, Field));
	}

	// The node's immediate ancestor: the non-drawing wrapper (or node 0) whose clock it runs on.
	int32 WrapperOf(const FElysiumParticleTree& Tree, const FElysiumParticleNode& Node)
	{
		return Tree.Nodes.IsValidIndex(Node.Parent) ? Node.Parent : INDEX_NONE;
	}

	// `via: collide` anywhere on the chain from the leaf up to its drawing parent (or the root):
	// VtMB's collide-spawned wrappers pass the trigger down to the leaf that draws.
	bool SpawnsOnCollision(const FElysiumParticleTree& Tree, const FElysiumParticleNode& Node)
	{
		static const FName Collide(TEXT("collide"));
		if (Node.Via == Collide)
		{
			return true;
		}
		int32 Cursor = Node.Parent;
		int32 Guard = 0;
		while (Tree.Nodes.IsValidIndex(Cursor) && Cursor > 0 && Guard++ < 64)
		{
			const FElysiumParticleNode& Ancestor = Tree.Nodes[Cursor];
			if (Ancestor.bDraws && Ancestor.bResolved)
			{
				return false;
			}
			if (Ancestor.Via == Collide)
			{
				return true;
			}
			Cursor = Ancestor.Parent;
		}
		return false;
	}

	// The nearest drawing ancestor's node index, or INDEX_NONE (the root's own clock) when the
	// parent chain reaches node 0 or a non-drawing wrapper without passing a drawing node.
	int32 DrawingParentOf(const FElysiumParticleTree& Tree, const FElysiumParticleNode& Node)
	{
		int32 Cursor = Node.Parent;
		int32 Guard = 0;
		while (Tree.Nodes.IsValidIndex(Cursor) && Cursor > 0 && Guard++ < 64)
		{
			const FElysiumParticleNode& Ancestor = Tree.Nodes[Cursor];
			if (Ancestor.bDraws && Ancestor.bResolved)
			{
				return Cursor;
			}
			Cursor = Ancestor.Parent;
		}
		return INDEX_NONE;
	}

	// The box's eight corners re-expressed in `Frame` and re-boxed; exact for an unrotated
	// frame, the enclosing box otherwise.
	FBox BoxInFrame(const FBox& WorldBox, const FTransform& Frame)
	{
		FBox Local(ForceInit);
		if (!WorldBox.IsValid)
		{
			return Local;
		}
		FVector Corners[8];
		WorldBox.GetVertices(Corners);
		for (const FVector& Corner : Corners)
		{
			Local += Frame.InverseTransformPosition(Corner);
		}
		return Local;
	}
}

AElysiumEffectActor::AElysiumEffectActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Niagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Niagara"));
	Niagara->SetAutoActivate(false);
	SetRootComponent(Niagara);
}

void AElysiumEffectActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureSystem();
}

void AElysiumEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Mode 9: the parent's render box, re-read each tick because the parent animates.
	if (UPrimitiveComponent* Parent = BoundsParent.Get())
	{
		WriteSpawnBoxFromBounds(Parent->Bounds.GetBox());
	}
	else
	{
		SetActorTickEnabled(false);
	}
}

const TCHAR* AElysiumEffectActor::DefaultSystemPath() const
{
	return ElysiumEffectAssets::FloorSystem;
}

void AElysiumEffectActor::WarnOnce(const FString& Key, const FString& Message)
{
	if (Reported.Contains(Key))
	{
		return;
	}
	Reported.Add(Key);
	UE_LOG(LogElysiumEffect, Warning, TEXT("%s: %s"), *GetName(), *Message);
}

UNiagaraSystem* AElysiumEffectActor::LoadSystem(const TCHAR* Path)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, Path);
	if (!System)
	{
		WarnOnce(FString(TEXT("system|")) + Path,
			FString::Printf(TEXT("effect %d ('%s') cannot load its Niagara system '%s'"),
				EntityIndex, *RootName, Path));
	}
	return System;
}

FString AElysiumEffectActor::GeneratedSystemStem(const FString& RootOrName)
{
	// `NS_<root>` in the definition's authored spelling (`make_root_systems.py::_asset_name`):
	// the folded `vtmb:particle:` prefix dropped, any include path and `.txt` stripped.
	FString Stem = RootOrName;
	Stem.RemoveFromStart(TEXT("vtmb:particle:"), ESearchCase::IgnoreCase);
	Stem.ReplaceInline(TEXT("\\"), TEXT("/"));
	int32 Slash = INDEX_NONE;
	if (Stem.FindLastChar(TEXT('/'), Slash))
	{
		Stem = Stem.Mid(Slash + 1);
	}
	if (Stem.EndsWith(TEXT(".txt"), ESearchCase::IgnoreCase))
	{
		Stem.LeftChopInline(4);
	}
	return Stem;
}

bool AElysiumEffectActor::HasGeneratedSystem(const FString& RootOrName)
{
	const FString Stem = GeneratedSystemStem(RootOrName);
	return !Stem.IsEmpty()
		&& FPackageName::DoesPackageExist(
			FString::Printf(TEXT("/Game/ElysiumGenerated/VFX/NS_%s"), *Stem));
}

UNiagaraSystem* AElysiumEffectActor::LoadGeneratedSystem()
{
	// The tree's name when it has one, else the entity's -- a by-root spawn carries no tree.
	const FString Stem = GeneratedSystemStem(Tree.Name.IsEmpty() ? RootName : Tree.Name);
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString Package = FString::Printf(TEXT("/Game/ElysiumGenerated/VFX/NS_%s"), *Stem);
	if (!FPackageName::DoesPackageExist(Package))
	{
		return nullptr;
	}
	return LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("%s.NS_%s"), *Package, *Stem));
}

UMaterialInterface* AElysiumEffectActor::LoadMaterial(const TCHAR* Path)
{
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!Material)
	{
		WarnOnce(FString(TEXT("material|")) + Path,
			FString::Printf(TEXT("effect %d ('%s') cannot load the particle material '%s'; "
				"the renderer keeps the system's default"), EntityIndex, *RootName, Path));
	}
	return Material;
}

void AElysiumEffectActor::ResolveFamily(const UElysiumEffectFamilies* Families)
{
	FamilyPins.Reset();
	FamilySystem = nullptr;
	if (Families)
	{
		if (const FElysiumEffectFamily* Family = Families->Match(RootName, Tree))
		{
			FamilySystem = Family->System;
			FamilyPins = Family->Pins;
		}
	}
	bFamilyResolved = true;
	// A family swap after the first bind re-binds: the system is a different asset.
	bSystemBound = false;
	EnsureSystem();
}

void AElysiumEffectActor::EnsureSystem()
{
	if (bSystemBound || !Niagara)
	{
		return;
	}
	UNiagaraSystem* System = nullptr;
	if (!FamilySystem.IsNull())
	{
		System = FamilySystem.LoadSynchronous();
		if (!System)
		{
			WarnOnce(TEXT("family"), FString::Printf(
				TEXT("effect %d ('%s') names family system '%s' that does not load; using the floor"),
				EntityIndex, *RootName, *FamilySystem.ToString()));
		}
	}
	bGeneratedSystem = false;
	if (!System)
	{
		// B3 (revised 2026-09-03): the bake's generated `NS_<root>` when it exists, else the floor.
		System = LoadGeneratedSystem();
		bGeneratedSystem = System != nullptr;
	}
	if (!System)
	{
		System = LoadSystem(DefaultSystemPath());
	}
	bSystemBound = true;
	if (System && Niagara->GetAsset() != System)
	{
		Niagara->SetAsset(System);
	}
	// The parameters are written even without an asset: the override store keeps them, and a
	// system bound later reads them.
	WriteParameters();
	if (bOn)
	{
		Niagara->Activate(true);
	}
}

void AElysiumEffectActor::WriteParameters()
{
	WriteTree();
	if (FamilyPins.Num() > 0)
	{
		WriteFamilyPins();
	}
}

void AElysiumEffectActor::WriteTree()
{
	using namespace ElysiumEffectAssets;

	Niagara->SetVariableFloat(TEXT("User.RateScale"), CurrentRate);
	Niagara->SetVariableLinearColor(TEXT("User.Tint"), Tint);
	Niagara->SetVariableFloat(TEXT("User.SizeScale"), SizeScale);


	// Mode 15's box is the brush AABB the bake wrote; every other shape is set at attach.
	if (AttachType == 15 && BoundsCm.IsValid)
	{
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 1);
		WriteSpawnBoxFromBounds(BoundsCm);
	}
	else if (!bHaveDriven)
	{
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
	}

	if (bGeneratedSystem)
	{
		// The generated system carries its leaves as emitters; there are no slots to fill.
		return;
	}

	// The fixed layout (ElysiumEffectFamilies.h): roots take slots 0..RootSlots-1, the parent
	// with the most drawing children first; each child takes a free child slot whose fixed parent
	// is its parent's slot. A child of a child, a ninth root or a fourth child has no slot and is
	// not drawn.
	TMap<int32, int32> SlotByNode;
	TArray<const FElysiumParticleNode*> Roots;
	TArray<TPair<const FElysiumParticleNode*, int32>> ChildNodes;   // node, parent node index
	TMap<int32, int32> ChildCount;
	for (const FElysiumParticleNode& Node : Tree.Nodes)
	{
		if (!Node.bDraws || !Node.bResolved)
		{
			continue;
		}
		const int32 ParentNode = DrawingParentOf(Tree, Node);
		if (ParentNode == INDEX_NONE)
		{
			Roots.Add(&Node);
		}
		else
		{
			ChildNodes.Emplace(&Node, ParentNode);
			++ChildCount.FindOrAdd(ParentNode);
		}
	}
	Roots.StableSort([&ChildCount](const FElysiumParticleNode& A, const FElysiumParticleNode& B)
	{
		return ChildCount.FindRef(A.Index) > ChildCount.FindRef(B.Index);
	});
	TArray<TTuple<int32, const FElysiumParticleNode*, int32>> Placed;   // slot, node, parent slot
	for (int32 i = 0; i < Roots.Num(); ++i)
	{
		if (i >= RootSlots)
		{
			WarnOnce(TEXT("roots"), FString::Printf(
				TEXT("effect %d ('%s') has more than %d root-spawned leaves; the rest are not drawn"),
				EntityIndex, *RootName, RootSlots));
			break;
		}
		SlotByNode.Add(Roots[i]->Index, i);
		Placed.Emplace(i, Roots[i], INDEX_NONE);
	}
	TSet<int32> UsedChildSlots;
	for (const TPair<const FElysiumParticleNode*, int32>& Child : ChildNodes)
	{
		const int32* ParentSlot = SlotByNode.Find(Child.Value);
		int32 Slot = INDEX_NONE;
		if (ParentSlot && *ParentSlot < RootSlots)
		{
			for (int32 c = RootSlots; c < MaxLeafSlots; ++c)
			{
				if (ChildSlotParent[c] == *ParentSlot && !UsedChildSlots.Contains(c))
				{
					Slot = c;
					break;
				}
			}
		}
		if (Slot == INDEX_NONE)
		{
			WarnOnce(TEXT("child|") + Child.Key->Name, FString::Printf(
				TEXT("effect %d ('%s') leaf '%s' has no child slot under its parent; not drawn"),
				EntityIndex, *RootName, *Child.Key->Name));
			continue;
		}
		UsedChildSlots.Add(Slot);
		SlotByNode.Add(Child.Key->Index, Slot);
		Placed.Emplace(Slot, Child.Key, *ParentSlot);
	}
	// The root clock: every root-spawned slot's Rate / Burst run over the nearest non-drawing
	// wrapper's lifetime and loop flag (node 0 when the leaf hangs off it directly). VtMB's
	// looping wrappers restart themselves forever, which is the clock a leaf under them sees --
	// `waterdrops_timer`'s 1.7 s one-shot root holds a 0.67 s looping wrapper that drips forever.
	// One clock per instance: the first root's wrapper wins; a tree whose roots disagree is named.
	{
		float ClockLifetime = 1.f;
		bool bClockLoop = true;
		int32 ClockNode = INDEX_NONE;
		for (int32 i = 0; i < FMath::Min(Roots.Num(), RootSlots); ++i)
		{
			const int32 Wrapper = WrapperOf(Tree, *Roots[i]);
			if (i == 0)
			{
				ClockNode = Wrapper;
				if (Tree.Nodes.IsValidIndex(Wrapper))
				{
					ClockLifetime = Tree.Nodes[Wrapper].LifetimeS > 0.f ? Tree.Nodes[Wrapper].LifetimeS : 1.f;
					bClockLoop = Tree.Nodes[Wrapper].bLoop;
				}
			}
			else if (Wrapper != ClockNode && Tree.Nodes.IsValidIndex(Wrapper) && Tree.Nodes.IsValidIndex(ClockNode)
				&& (Tree.Nodes[Wrapper].LifetimeS != Tree.Nodes[ClockNode].LifetimeS || Tree.Nodes[Wrapper].bLoop != Tree.Nodes[ClockNode].bLoop))
			{
				WarnOnce(TEXT("clock|") + Roots[i]->Name, FString::Printf(
					TEXT("effect %d ('%s') leaf '%s' hangs off a wrapper with its own clock; it runs on '%s's"),
					EntityIndex, *RootName, *Roots[i]->Name, *Tree.Nodes[ClockNode].Name));
			}
		}
		Niagara->SetVariableFloat(TEXT("User.RootLifetime"), ClockLifetime);
		Niagara->SetVariableBool(TEXT("User.RootLoop"), bClockLoop);
	}
	TSet<int32> Filled;
	for (const TTuple<int32, const FElysiumParticleNode*, int32>& Entry : Placed)
	{
		const int32 Slot = Entry.Get<0>();
		const FElysiumParticleNode& Node = *Entry.Get<1>();
		const int32 ParentSlot = Entry.Get<2>();
		Filled.Add(Slot);

		Niagara->SetVariableBool(SlotName(Slot, TEXT("Active")), true);
		Niagara->SetVariableInt(SlotName(Slot, TEXT("ParentLeaf")), ParentSlot);
		Niagara->SetVariableInt(SlotName(Slot, TEXT("SpawnOn")), SpawnsOnCollision(Tree, Node) ? 1 : 0);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("Lifetime")), Node.LifetimeS);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("LifetimeMin")), Node.LifetimeMinS);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("LifetimeMax")), Node.LifetimeMaxS);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("Loop")), Node.bLoop);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("Distance")), Node.bHasSpawn && Node.Spawn.bDistance);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("DepthOffset")), Node.DepthOffsetCm);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("MoveAlign")), Node.bMoveAlign);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("Flat")), Node.bFlat);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("SortFront")), Node.bSortFront);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("NoZTest")), Node.bNoZTest);

		if (Node.Sprite.IsSet())
		{
			if (UTexture2D* Sprite = Node.Sprite.Texture.LoadSynchronous())
			{
				Niagara->SetVariableTexture(SlotName(Slot, TEXT("Sprite")), Sprite);
			}
			else
			{
				WarnOnce(TEXT("sprite|") + Node.Sprite.Id, FString::Printf(
					TEXT("effect %d ('%s') leaf '%s' sprite '%s' does not load"),
					EntityIndex, *RootName, *Node.Name, *Node.Sprite.Texture.ToString()));
			}
		}
		if (Node.Normal.IsSet())
		{
			if (UTexture2D* Normal = Node.Normal.Texture.LoadSynchronous())
			{
				Niagara->SetVariableTexture(SlotName(Slot, TEXT("Normal")), Normal);
			}
		}
		Niagara->SetVariableVec2(SlotName(Slot, TEXT("SpriteAspect")), Node.Sprite.Aspect);

		// §5.4: the material child by the flags.
		const TCHAR* MaterialPath = Node.bNoZTest ? MaterialNoZ
			: Node.Normal.IsSet() ? MaterialRefract
			: Node.bLighting ? MaterialLit
			: MaterialFloor;
		if (UMaterialInterface* Material = LoadMaterial(MaterialPath))
		{
			Niagara->SetVariableMaterial(SlotName(Slot, TEXT("Material")), Material);
		}

		Niagara->SetVariableBool(SlotName(Slot, TEXT("Collide")), Node.bHasCollide);
		Niagara->SetVariableBool(SlotName(Slot, TEXT("CollideSelf")), Node.bHasCollide && Node.Collide.bSelfCollide);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("Bounce")), Node.bHasCollide ? Node.Collide.Bounce : 1.f);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("Friction")), Node.bHasCollide ? Node.Collide.Friction : 1.f);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("Gravity")), Node.bHasCollide ? Node.Collide.Gravity : 0.f);
		Niagara->SetVariableFloat(SlotName(Slot, TEXT("Drag")), Node.bHasCollide ? Node.Collide.Drag : 1.f);

		TArray<FVector> Ramps;
		PackRamps(Node, Ramps);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Niagara,
			SlotName(Slot, TEXT("Ramps")), Ramps);

	}
	for (int32 Unused = 0; Unused < MaxLeafSlots; ++Unused)
	{
		if (!Filled.Contains(Unused))
		{
			Niagara->SetVariableBool(SlotName(Unused, TEXT("Active")), false);
		}
	}
	Niagara->SetVariableInt(TEXT("User.LeafCount"), Filled.Num());

	// A surface_color_optout leaf pins its tint white; the flag is per node, the pin per system,
	// so the actor's tint is white when any drawing leaf opts out.
	for (const FElysiumParticleNode& Node : Tree.Nodes)
	{
		if (Node.bDraws && Node.bSurfaceColorOptout)
		{
			Niagara->SetVariableLinearColor(TEXT("User.Tint"), FLinearColor::White);
			break;
		}
	}
}

void AElysiumEffectActor::WriteFamilyPins()
{
	const FElysiumParticleNode* Leaf = nullptr;
	for (const FElysiumParticleNode& Node : Tree.Nodes)
	{
		if (Node.bDraws && Node.bResolved)
		{
			Leaf = &Node;
			break;
		}
	}
	if (!Leaf)
	{
		return;
	}
	for (const TPair<FName, FName>& Pin : FamilyPins)
	{
		const FString Field = Pin.Key.ToString();
		float Value = 0.f;
		bool bFound = true;
		if (Field == TEXT("Lifetime")) { Value = Leaf->LifetimeS; }
		else if (Field == TEXT("LifetimeMin")) { Value = Leaf->LifetimeMinS; }
		else if (Field == TEXT("LifetimeMax")) { Value = Leaf->LifetimeMaxS; }
		else
		{
			bFound = false;
			for (int32 R = 0; R < ElysiumEffectAssets::RampCount; ++R)
			{
				if (Field == GRampNames[R])
				{
					Value = RampScalar(RampOf(*Leaf, static_cast<ERamp>(R)), 0.f);
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			WarnOnce(TEXT("pin|") + Field, FString::Printf(
				TEXT("effect %d ('%s') family pin '%s' names no tree field"),
				EntityIndex, *RootName, *Field));
			continue;
		}
		Niagara->SetVariableFloat(Pin.Value, Value);
	}
}

void AElysiumEffectActor::WriteSpawnBoxFromBounds(const FBox& WorldBox)
{
	const FBox Local = BoxInFrame(WorldBox, Niagara->GetComponentTransform());
	if (!Local.IsValid)
	{
		return;
	}
	Niagara->SetVariableVec3(TEXT("User.SpawnBoxMin"), Local.Min);
	Niagara->SetVariableVec3(TEXT("User.SpawnBoxMax"), Local.Max);
}

void AElysiumEffectActor::TurnOn()
{
	if (bKilled)
	{
		return;
	}
	bOn = true;
	SetActorHiddenInGame(false);
	EnsureSystem();
	if (Niagara)
	{
		// VtMB rebuilds the emitter on every activation: a restart, not a resume.
		Niagara->Activate(/*bReset*/ true);
	}
}

void AElysiumEffectActor::TurnOff()
{
	bOn = false;
	if (Niagara)
	{
		// Deactivate (not DeactivateImmediate): spawning stops and the live particles finish.
		Niagara->Deactivate();
	}
}

void AElysiumEffectActor::Kill()
{
	bOn = false;
	bKilled = true;
	if (Niagara)
	{
		Niagara->DeactivateImmediate();
	}
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
}

void AElysiumEffectActor::SetRate(float InRateScale)
{
	CurrentRate = FMath::Max(0.f, InRateScale) * VolumeScale;
	if (Niagara)
	{
		Niagara->SetVariableFloat(TEXT("User.RateScale"), CurrentRate);
	}
}

void AElysiumEffectActor::SetTint(const FLinearColor& InTint)
{
	Tint = InTint;
	if (Niagara)
	{
		Niagara->SetVariableLinearColor(TEXT("User.Tint"), Tint);
	}
}

void AElysiumEffectActor::SetSizeScale(float InSizeScale)
{
	SizeScale = InSizeScale;
	if (Niagara)
	{
		Niagara->SetVariableFloat(TEXT("User.SizeScale"), SizeScale);
	}
}

void AElysiumEffectActor::ApplyFog(bool bEnabled, const FLinearColor& Color, float StartCm, float EndCm)
{
	if (!Niagara)
	{
		return;
	}
	TArray<float> Data;
	ElysiumFog::Pack(bEnabled, Color, StartCm, EndCm, Data);
	Niagara->SetVariableLinearColor(TEXT("User.FogColor"),
		FLinearColor(Data[ElysiumFog::SlotColor + 0], Data[ElysiumFog::SlotColor + 1],
			Data[ElysiumFog::SlotColor + 2], Data[ElysiumFog::SlotColor + 3]));
	Niagara->SetVariableFloat(TEXT("User.FogStart"), Data[ElysiumFog::SlotStart]);
	Niagara->SetVariableFloat(TEXT("User.FogInvRange"), Data[ElysiumFog::SlotInvRange]);
}

void AElysiumEffectActor::SetAttachType(int32 Mode, const FElysiumEffectAttachment& Attachment)
{
	AttachType = Mode;
	AttachNow(Mode, Attachment);
}

void AElysiumEffectActor::AttachNow(int32 Mode, const FElysiumEffectAttachment& A)
{
	if (!Niagara)
	{
		return;
	}
	BoundsParent = nullptr;
	SetActorTickEnabled(false);
	switch (Mode)
	{
	case 1:   // BoneTree
	case 3:   // BoneTreeWithColors -- the per-segment tint's source is the one open item of R-D
		if (A.ParentBody)
		{
			if (A.bHasWorldLocation)
			{
				SetActorLocation(A.WorldLocationCm);
			}
			AttachToComponent(A.ParentBody, FAttachmentTransformRules::KeepWorldTransform);
		}
		if (A.Skeletal)
		{
			Niagara->SetVariableInt(TEXT("User.SpawnShape"), 2);
			UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(Niagara,
				TEXT("User.SkeletalMesh"), A.Skeletal);
		}
		else
		{
			Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		}
		return;
	case 2:   // BoneSinglePoint
	case 6:   // ModelAttachment -- origin and basis each tick
		if (A.ParentBody)
		{
			AttachToComponent(A.ParentBody, FAttachmentTransformRules::SnapToTargetNotIncludingScale, A.Socket);
			Niagara->SetRelativeLocation(FVector::ZeroVector);
		}
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		return;
	case 17:  // ModelAttachmentNoFollow -- snap once
		if (A.ParentBody)
		{
			FTransform Snap = A.ParentBody->GetSocketTransform(A.Socket, RTS_World);
			Snap.SetScale3D(FVector::OneVector);
			SetActorTransform(Snap);
		}
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		return;
	case 9:   // EntityBox -- a random point in the parent's render bounds, refreshed per tick
		if (A.ParentBody)
		{
			AttachToComponent(A.ParentBody, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Niagara->SetRelativeLocation(FVector::ZeroVector);
			BoundsParent = Cast<UPrimitiveComponent>(A.ParentBody);
		}
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 1);
		if (UPrimitiveComponent* Parent = BoundsParent.Get())
		{
			WriteSpawnBoxFromBounds(Parent->Bounds.GetBox());
			SetActorTickEnabled(true);
		}
		return;
	case 15:  // BrushEmitter -- the brush AABB, no solid test
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 1);
		WriteSpawnBoxFromBounds(BoundsCm);
		return;
	case 4: case 8: case 12: case 13: case 18:
		WarnOnce(FString::Printf(TEXT("attach|%d"), Mode), FString::Printf(
			TEXT("effect %d ('%s') asks attach mode %d, placed nowhere and not implemented; origin"),
			EntityIndex, *RootName, Mode));
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		return;
	case 5: case 7: case 14: case 16:
		WarnOnce(FString::Printf(TEXT("attach|%d"), Mode), FString::Printf(
			TEXT("effect %d ('%s') asks screen-space attach mode %d (out of scope); origin"),
			EntityIndex, *RootName, Mode));
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		return;
	default:  // 0, -1: the entity's origin, where the bake placed the actor; 10/11 are weather's
		Niagara->SetVariableInt(TEXT("User.SpawnShape"), 0);
		return;
	}
}

void AElysiumEffectActor::Drive(const FElysiumWeatherEmitterState& Emitter,
	const FElysiumEffectAttachment* Attachment)
{
	if (bKilled)
	{
		return;
	}
	if (Emitter.bDead)
	{
		Kill();
		return;
	}
	SetRate(Emitter.RateScale);
	const bool bRestart = Emitter.bActive
		&& (!bHaveDriven || !bLastActive || Emitter.TurnOnSerial != LastTurnOnSerial);
	const bool bReattach = bRestart || (bHaveDriven && Emitter.AttachType != LastAttachType);
	if (bReattach)
	{
		const FElysiumEffectAttachment None;
		SetAttachType(Emitter.AttachType, Attachment ? *Attachment : None);
	}
	if (bRestart)
	{
		TurnOn();
	}
	else if (!Emitter.bActive && bHaveDriven && bLastActive)
	{
		TurnOff();
	}
	bHaveDriven = true;
	bLastActive = Emitter.bActive;
	LastTurnOnSerial = Emitter.TurnOnSerial;
	LastAttachType = Emitter.AttachType;
}
