// Headless contracts for the MDL -> glTF -> glTFRuntime skeletal-animation seam. The structural
// test reads every generated NPC/bank GLB without constructing rendering resources; the theatre
// test then exercises the real runtime loader and UAnimSequence-to-USkeleton binding on PP2's cast.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumSceneData.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"
#include "glTFRuntimeAsset.h"

static constexpr EAutomationTestFlags GElysiumSkeletalContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr uint32 GlbMagic = 0x46546C67;      // glTF
	constexpr uint32 JsonChunk = 0x4E4F534A;     // JSON
	constexpr uint32 BinChunk = 0x004E4942;      // BIN\0

	uint32 ReadU32(const TArray<uint8>& Bytes, int32 Offset)
	{
		uint32 Value = 0;
		if (Offset >= 0 && Offset + 4 <= Bytes.Num())
		{
			FMemory::Memcpy(&Value, Bytes.GetData() + Offset, 4);
		}
		return Value;
	}

	int32 JsonInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Default = INDEX_NONE)
	{
		double Value = 0.0;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value)
			? static_cast<int32>(Value) : Default;
	}

	FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Field, Value);
		}
		return Value;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray(
		const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(Field, Value) ? Value : nullptr;
	}

	TSharedPtr<FJsonObject> JsonObjectAt(
		const TArray<TSharedPtr<FJsonValue>>* Values, int32 Index)
	{
		return Values != nullptr && Values->IsValidIndex(Index) ? (*Values)[Index]->AsObject() : nullptr;
	}

	struct FGltfAccessor
	{
		const uint8* Data = nullptr;
		int32 Count = 0;
		int32 Components = 0;
		int32 ComponentType = 0;
		int32 ComponentBytes = 0;
		int32 Stride = 0;

		double Number(int32 Element, int32 Component) const
		{
			const uint8* Ptr = Data + Element * Stride + Component * ComponentBytes;
			switch (ComponentType)
			{
			case 5120: { int8 V = 0; FMemory::Memcpy(&V, Ptr, 1); return V; }
			case 5121: { uint8 V = 0; FMemory::Memcpy(&V, Ptr, 1); return V; }
			case 5122: { int16 V = 0; FMemory::Memcpy(&V, Ptr, 2); return V; }
			case 5123: { uint16 V = 0; FMemory::Memcpy(&V, Ptr, 2); return V; }
			case 5125: { uint32 V = 0; FMemory::Memcpy(&V, Ptr, 4); return V; }
			case 5126: { float V = 0.f; FMemory::Memcpy(&V, Ptr, 4); return V; }
			default: return 0.0; // rejected by Accessor() before Number() can be called
			}
		}
	};

	struct FGltfStats
	{
		int64 Files = 0;
		int64 Joints = 0;
		int64 Vertices = 0;
		int64 Clips = 0;
		int64 Channels = 0;
		int64 Samples = 0;
	};

	class FGltfContract
	{
	public:
		FGltfContract(FAutomationTestBase& InTest, FString InPath)
			: Test(InTest), Path(MoveTemp(InPath)) {}

		bool Load()
		{
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				return Fail(TEXT("cannot read file"));
			}
			if (Bytes.Num() < 20 || ReadU32(Bytes, 0) != GlbMagic || ReadU32(Bytes, 4) != 2)
			{
				return Fail(TEXT("not a glTF 2.0 binary"));
			}
			if (ReadU32(Bytes, 8) != static_cast<uint32>(Bytes.Num()))
			{
				return Fail(TEXT("header length does not match file length"));
			}

			int32 Offset = 12;
			while (Offset + 8 <= Bytes.Num())
			{
				const int32 Length = static_cast<int32>(ReadU32(Bytes, Offset));
				const uint32 Type = ReadU32(Bytes, Offset + 4);
				Offset += 8;
				if (Length < 0 || Offset + Length > Bytes.Num())
				{
					return Fail(TEXT("chunk extends past end of file"));
				}
				if (Type == JsonChunk)
				{
					FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Length);
					const FString Text(Convert.Length(), Convert.Get());
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
					if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
					{
						return Fail(TEXT("JSON chunk does not parse"));
					}
				}
				else if (Type == BinChunk)
				{
					BinOffset = Offset;
					BinLength = Length;
				}
				Offset += Length;
			}
			return Root.IsValid() && BinOffset != INDEX_NONE
				? true : Fail(TEXT("missing JSON or BIN chunk"));
		}

		bool Accessor(int32 Index, FGltfAccessor& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Accessors = JsonArray(Root, TEXT("accessors"));
			const TArray<TSharedPtr<FJsonValue>>* Views = JsonArray(Root, TEXT("bufferViews"));
			const TSharedPtr<FJsonObject> A = JsonObjectAt(Accessors, Index);
			const int32 ViewIndex = JsonInt(A, TEXT("bufferView"));
			const TSharedPtr<FJsonObject> V = JsonObjectAt(Views, ViewIndex);
			if (!A.IsValid() || !V.IsValid() || JsonInt(V, TEXT("buffer"), 0) != 0)
			{
				return Fail(FString::Printf(TEXT("accessor %d has no binary bufferView"), Index));
			}

			Out.Count = JsonInt(A, TEXT("count"), 0);
			Out.ComponentType = JsonInt(A, TEXT("componentType"), 0);
			const FString Type = JsonString(A, TEXT("type"));
			if (Type == TEXT("SCALAR")) Out.Components = 1;
			else if (Type == TEXT("VEC2")) Out.Components = 2;
			else if (Type == TEXT("VEC3")) Out.Components = 3;
			else if (Type == TEXT("VEC4")) Out.Components = 4;
			else if (Type == TEXT("MAT4")) Out.Components = 16;
			else return Fail(FString::Printf(TEXT("accessor %d has unsupported type %s"), Index, *Type));

			switch (Out.ComponentType)
			{
			case 5120: case 5121: Out.ComponentBytes = 1; break;
			case 5122: case 5123: Out.ComponentBytes = 2; break;
			case 5125: case 5126: Out.ComponentBytes = 4; break;
			default: return Fail(FString::Printf(TEXT("accessor %d has unsupported component type"), Index));
			}

			const int32 ViewOffset = JsonInt(V, TEXT("byteOffset"), 0);
			const int32 AccessorOffset = JsonInt(A, TEXT("byteOffset"), 0);
			const int32 Packed = Out.Components * Out.ComponentBytes;
			Out.Stride = JsonInt(V, TEXT("byteStride"), Packed);
			const int64 Needed = Out.Count > 0 ? int64(Out.Count - 1) * Out.Stride + Packed : 0;
			const int64 Start = int64(ViewOffset) + AccessorOffset;
			const int64 ViewLength = JsonInt(V, TEXT("byteLength"), 0);
			if (Out.Count <= 0 || Out.Stride < Packed || AccessorOffset < 0
				|| int64(AccessorOffset) + Needed > ViewLength || Start + Needed > BinLength)
			{
				return Fail(FString::Printf(TEXT("accessor %d exceeds its bufferView"), Index));
			}
			Out.Data = Bytes.GetData() + BinOffset + Start;
			return true;
		}

		bool Validate(bool bRequireSkin, FGltfStats& Stats);

	private:
		bool Fail(const FString& Message)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Path, *Message));
			return false;
		}

		FAutomationTestBase& Test;
		FString Path;
		TArray<uint8> Bytes;
		TSharedPtr<FJsonObject> Root;
		int32 BinOffset = INDEX_NONE;
		int32 BinLength = 0;
	};

	bool FGltfContract::Validate(bool bRequireSkin, FGltfStats& Stats)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = JsonArray(Root, TEXT("nodes"));
		if (Nodes == nullptr || Nodes->IsEmpty())
		{
			return Fail(TEXT("has no nodes"));
		}

		TArray<int32> JointNodes;
		const TArray<TSharedPtr<FJsonValue>>* Skins = JsonArray(Root, TEXT("skins"));
		if (bRequireSkin && (Skins == nullptr || Skins->Num() != 1))
		{
			return Fail(TEXT("NPC mesh must carry exactly one skin"));
		}
		if (Skins != nullptr && !Skins->IsEmpty())
		{
			const TSharedPtr<FJsonObject> Skin = JsonObjectAt(Skins, 0);
			const TArray<TSharedPtr<FJsonValue>>* Joints = JsonArray(Skin, TEXT("joints"));
			const int32 SkeletonRoot = JsonInt(Skin, TEXT("skeleton"));
			if (Joints == nullptr || Joints->IsEmpty() || !Nodes->IsValidIndex(SkeletonRoot))
			{
				return Fail(TEXT("skin has no joints or a bad skeleton root"));
			}

			TArray<int32> Parents;
			Parents.Init(INDEX_NONE, Nodes->Num());
			for (int32 ParentIndex = 0; ParentIndex < Nodes->Num(); ++ParentIndex)
			{
				const TArray<TSharedPtr<FJsonValue>>* Children =
					JsonArray(JsonObjectAt(Nodes, ParentIndex), TEXT("children"));
				if (Children == nullptr) continue;
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					const int32 ChildIndex = static_cast<int32>(Child->AsNumber());
					if (!Nodes->IsValidIndex(ChildIndex) || Parents[ChildIndex] != INDEX_NONE)
					{
						return Fail(TEXT("node graph has an invalid child or multiple parents"));
					}
					Parents[ChildIndex] = ParentIndex;
				}
			}

			TSet<int32> Reachable;
			TArray<int32> Stack { SkeletonRoot };
			while (!Stack.IsEmpty())
			{
				const int32 NodeIndex = Stack.Pop(EAllowShrinking::No);
				if (!Nodes->IsValidIndex(NodeIndex) || Reachable.Contains(NodeIndex))
				{
					continue;
				}
				Reachable.Add(NodeIndex);
				const TArray<TSharedPtr<FJsonValue>>* Children =
					JsonArray(JsonObjectAt(Nodes, NodeIndex), TEXT("children"));
				if (Children != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Child : *Children)
					{
						const int32 ChildIndex = static_cast<int32>(Child->AsNumber());
						if (!Nodes->IsValidIndex(ChildIndex))
						{
							return Fail(TEXT("node graph contains an invalid child"));
						}
						Stack.Add(ChildIndex);
					}
				}
			}

			TSet<FString> JointNames;
			for (const TSharedPtr<FJsonValue>& Joint : *Joints)
			{
				const int32 NodeIndex = static_cast<int32>(Joint->AsNumber());
				const TSharedPtr<FJsonObject> Node = JsonObjectAt(Nodes, NodeIndex);
				FString Name = JsonString(Node, TEXT("name"));
				Name.ToLowerInline();
				if (!Node.IsValid() || Name.IsEmpty() || JointNames.Contains(Name))
				{
					return Fail(TEXT("skin joint names are missing or duplicated"));
				}
				if (!Reachable.Contains(NodeIndex))
				{
					return Fail(FString::Printf(TEXT("joint '%s' is unreachable from skin.skeleton"), *Name));
				}
				JointNames.Add(Name);
				JointNodes.Add(NodeIndex);
			}

			FGltfAccessor InverseBinds;
			if (!Accessor(JsonInt(Skin, TEXT("inverseBindMatrices")), InverseBinds)
				|| InverseBinds.Count != JointNodes.Num() || InverseBinds.Components != 16
				|| InverseBinds.ComponentType != 5126)
			{
				return Fail(TEXT("inverse-bind matrices do not match skin joints"));
			}

			// glTF matrices are column-major. Rebuild every node's global bind from TRS and
			// prove the authored inverse really is its inverse, not merely a correctly sized array.
			auto Multiply = [](const TArray<double>& A, const TArray<double>& B)
			{
				TArray<double> C;
				C.Init(0.0, 16);
				for (int32 Column = 0; Column < 4; ++Column)
				{
					for (int32 Row = 0; Row < 4; ++Row)
					{
						for (int32 K = 0; K < 4; ++K)
						{
							C[Column * 4 + Row] += A[K * 4 + Row] * B[Column * 4 + K];
						}
					}
				}
				return C;
			};
			auto VectorField = [](const TSharedPtr<FJsonObject>& Node, const TCHAR* Field,
				const TArray<double>& Default)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = JsonArray(Node, Field);
				if (Values == nullptr || Values->Num() != Default.Num()) return Default;
				TArray<double> Out;
				for (const TSharedPtr<FJsonValue>& Value : *Values) Out.Add(Value->AsNumber());
				return Out;
			};
			TArray<TArray<double>> Globals;
			Globals.SetNum(Nodes->Num());
			TSet<int32> Building;
			TFunction<bool(int32)> BuildGlobal = [&](int32 NodeIndex)
			{
				if (Globals[NodeIndex].Num() == 16) return true;
				if (Building.Contains(NodeIndex)) return false;
				Building.Add(NodeIndex);
				const TSharedPtr<FJsonObject> Node = JsonObjectAt(Nodes, NodeIndex);
				const TArray<double> T = VectorField(Node, TEXT("translation"), { 0.0, 0.0, 0.0 });
				const TArray<double> Q = VectorField(Node, TEXT("rotation"), { 0.0, 0.0, 0.0, 1.0 });
				const TArray<double> S = VectorField(Node, TEXT("scale"), { 1.0, 1.0, 1.0 });
				const double X = Q[0], Y = Q[1], Z = Q[2], W = Q[3];
				TArray<double> Local;
				Local.Init(0.0, 16);
				Local[0] = (1.0 - 2.0 * (Y * Y + Z * Z)) * S[0];
				Local[1] = (2.0 * (X * Y + W * Z)) * S[0];
				Local[2] = (2.0 * (X * Z - W * Y)) * S[0];
				Local[4] = (2.0 * (X * Y - W * Z)) * S[1];
				Local[5] = (1.0 - 2.0 * (X * X + Z * Z)) * S[1];
				Local[6] = (2.0 * (Y * Z + W * X)) * S[1];
				Local[8] = (2.0 * (X * Z + W * Y)) * S[2];
				Local[9] = (2.0 * (Y * Z - W * X)) * S[2];
				Local[10] = (1.0 - 2.0 * (X * X + Y * Y)) * S[2];
				Local[12] = T[0]; Local[13] = T[1]; Local[14] = T[2]; Local[15] = 1.0;
				const int32 Parent = Parents[NodeIndex];
				if (Parent != INDEX_NONE)
				{
					if (!BuildGlobal(Parent)) return false;
					Globals[NodeIndex] = Multiply(Globals[Parent], Local);
				}
				else
				{
					Globals[NodeIndex] = MoveTemp(Local);
				}
				Building.Remove(NodeIndex);
				return true;
			};
			for (int32 JointIndex = 0; JointIndex < JointNodes.Num(); ++JointIndex)
			{
				if (!BuildGlobal(JointNodes[JointIndex]))
				{
					return Fail(TEXT("node graph contains a cycle"));
				}
				TArray<double> Inverse;
				for (int32 Component = 0; Component < 16; ++Component)
				{
					Inverse.Add(InverseBinds.Number(JointIndex, Component));
				}
				const TArray<double> Identity = Multiply(Globals[JointNodes[JointIndex]], Inverse);
				for (int32 Component = 0; Component < 16; ++Component)
				{
					const double Expected = Component % 5 == 0 ? 1.0 : 0.0;
					if (!FMath::IsNearlyEqual(Identity[Component], Expected, 5.e-4))
					{
						return Fail(FString::Printf(TEXT("joint %d inverse bind is numerically wrong"), JointIndex));
					}
				}
			}
			Stats.Joints += JointNodes.Num();
		}

		const TArray<TSharedPtr<FJsonValue>>* Meshes = JsonArray(Root, TEXT("meshes"));
		if (bRequireSkin && (Meshes == nullptr || Meshes->IsEmpty()))
		{
			return Fail(TEXT("NPC skin has no mesh"));
		}
		if (Meshes != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& MeshValue : *Meshes)
			{
				const TSharedPtr<FJsonObject> Mesh = MeshValue->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Primitives = JsonArray(Mesh, TEXT("primitives"));
				if (Primitives == nullptr) continue;
				for (const TSharedPtr<FJsonValue>& PrimitiveValue : *Primitives)
				{
					const TSharedPtr<FJsonObject> Primitive = PrimitiveValue->AsObject();
					const TSharedPtr<FJsonObject>* AttributesPtr = nullptr;
					if (!Primitive.IsValid() || !Primitive->TryGetObjectField(TEXT("attributes"), AttributesPtr))
					{
						return Fail(TEXT("mesh primitive has no attributes"));
					}
					const TSharedPtr<FJsonObject>& Attributes = *AttributesPtr;
					const int32 JointAccessor = JsonInt(Attributes, TEXT("JOINTS_0"));
					const int32 WeightAccessor = JsonInt(Attributes, TEXT("WEIGHTS_0"));
					FGltfAccessor Joints, Weights;
					if (!Accessor(JointAccessor, Joints) || !Accessor(WeightAccessor, Weights)
						|| Joints.Count != Weights.Count || Joints.Components != 4 || Weights.Components != 4)
					{
						return Fail(TEXT("JOINTS_0 and WEIGHTS_0 do not describe the same VEC4 vertices"));
					}
					for (int32 Vertex = 0; Vertex < Weights.Count; ++Vertex)
					{
						double Sum = 0.0;
						for (int32 Influence = 0; Influence < 4; ++Influence)
						{
							const double Weight = Weights.Number(Vertex, Influence);
							const int32 Joint = static_cast<int32>(Joints.Number(Vertex, Influence));
							if (!FMath::IsFinite(Weight) || Weight < -1.e-6
								|| (Weight > 1.e-6 && !JointNodes.IsValidIndex(Joint)))
							{
								return Fail(FString::Printf(TEXT("vertex %d has an invalid skin influence"), Vertex));
							}
							Sum += Weight;
						}
						if (!FMath::IsNearlyEqual(Sum, 1.0, 1.e-3))
						{
							return Fail(FString::Printf(TEXT("vertex %d weights sum to %.9f"), Vertex, Sum));
						}
					}
					Stats.Vertices += Weights.Count;
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Animations = JsonArray(Root, TEXT("animations"));
		TSet<FString> AnimationNames;
		if (Animations != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& AnimationValue : *Animations)
			{
				const TSharedPtr<FJsonObject> Animation = AnimationValue->AsObject();
				FString AnimName = JsonString(Animation, TEXT("name"));
				AnimName.ToLowerInline();
				if (AnimName.IsEmpty() || AnimationNames.Contains(AnimName))
				{
					return Fail(TEXT("animation names are missing or duplicated case-insensitively"));
				}
				AnimationNames.Add(AnimName);
				const TArray<TSharedPtr<FJsonValue>>* Channels = JsonArray(Animation, TEXT("channels"));
				const TArray<TSharedPtr<FJsonValue>>* Samplers = JsonArray(Animation, TEXT("samplers"));
				if (Channels == nullptr || Samplers == nullptr || Channels->IsEmpty())
				{
					return Fail(FString::Printf(TEXT("animation '%s' has no channels"), *AnimName));
				}
				TSet<FString> Targets;
				for (const TSharedPtr<FJsonValue>& ChannelValue : *Channels)
				{
					const TSharedPtr<FJsonObject> Channel = ChannelValue->AsObject();
					const TSharedPtr<FJsonObject>* TargetPtr = nullptr;
					if (!Channel.IsValid() || !Channel->TryGetObjectField(TEXT("target"), TargetPtr))
					{
						return Fail(TEXT("animation channel has no target"));
					}
					const int32 NodeIndex = JsonInt(*TargetPtr, TEXT("node"));
					const FString PathName = JsonString(*TargetPtr, TEXT("path"));
					const FString NodeName = JsonString(JsonObjectAt(Nodes, NodeIndex), TEXT("name"));
					FString TargetKey = NodeName.ToLower() + TEXT("|") + PathName.ToLower();
					if (!Nodes->IsValidIndex(NodeIndex) || NodeName.IsEmpty() || Targets.Contains(TargetKey))
					{
						return Fail(FString::Printf(TEXT("animation '%s' has an invalid/duplicate target"), *AnimName));
					}
					Targets.Add(TargetKey);

					const TSharedPtr<FJsonObject> Sampler = JsonObjectAt(Samplers, JsonInt(Channel, TEXT("sampler")));
					FGltfAccessor Times, Values;
					if (!Sampler.IsValid() || !Accessor(JsonInt(Sampler, TEXT("input")), Times)
						|| !Accessor(JsonInt(Sampler, TEXT("output")), Values)
						|| Times.Count != Values.Count || Times.Components != 1 || Times.ComponentType != 5126)
					{
						return Fail(FString::Printf(TEXT("animation '%s' sampler shape is invalid"), *AnimName));
					}
					const int32 ExpectedComponents = PathName == TEXT("rotation") ? 4 : 3;
					if ((PathName != TEXT("rotation") && PathName != TEXT("translation") && PathName != TEXT("scale"))
						|| Values.Components != ExpectedComponents || Values.ComponentType != 5126)
					{
						return Fail(FString::Printf(TEXT("animation '%s' output shape is invalid"), *AnimName));
					}

					double Previous = -1.0;
					for (int32 Sample = 0; Sample < Times.Count; ++Sample)
					{
						const double Time = Times.Number(Sample, 0);
						if (!FMath::IsFinite(Time) || (Sample == 0 && !FMath::IsNearlyZero(Time, 1.e-6))
							|| (Sample > 0 && Time <= Previous))
						{
							return Fail(FString::Printf(TEXT("animation '%s' times are not strictly increasing from zero"), *AnimName));
						}
						Previous = Time;
						double NormSq = 0.0;
						for (int32 Component = 0; Component < Values.Components; ++Component)
						{
							const double Value = Values.Number(Sample, Component);
							if (!FMath::IsFinite(Value))
							{
								return Fail(FString::Printf(TEXT("animation '%s' has a non-finite sample"), *AnimName));
							}
							NormSq += Value * Value;
						}
						if (PathName == TEXT("rotation") && !FMath::IsNearlyEqual(NormSq, 1.0, 2.e-4))
						{
							return Fail(FString::Printf(TEXT("animation '%s' has a non-unit quaternion"), *AnimName));
						}
					}
					Stats.Samples += Values.Count;
					++Stats.Channels;
				}
				++Stats.Clips;
			}
		}
		++Stats.Files;
		return true;
	}

	// The upright envelope, asserted on the BAKED clip rather than on the glb.
	//
	// A bank glb deliberately forwards VtMB's split-rotation rule instead of resolving it: it ships
	// `Bip01 Spine1`'s raw channel plus a `split_bones` inventory naming that bone, which the test
	// above asserts is present. Ordinary FK over that channel is therefore not a pose -- it bends
	// the upper body sideways on all 67 clips of the shared male stances bank, and on any other
	// clip of any rig that flags the bone. The `.eskm` -> `.uasset` bake is where the rule is
	// resolved (`UE_mdl_skeletal._split_rotation_tracks`), so the mount is the only artifact that
	// can promise an upright neutral idle, and it is the one the game plays.
	bool ValidateBakedNeutralStance(FAutomationTestBase& Test, const FElysiumNpcIndex& Index)
	{
		// The first indexed body that can actually evaluate the clip: the bank is baked once against
		// a skeleton of its own, so what makes a body eligible is the compatibility declaration plus
		// carrying the two bones this measures -- not which stem it is.
		FString Stem;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Anim = nullptr;
		TArray<FString> Stems;
		Index.Npcs.GetKeys(Stems);
		Stems.Sort([](const FString& A, const FString& B) { return A < B; });
		for (const FString& Candidate : Stems)
		{
			FString LoadError;
			UglTFRuntimeAsset* Unused = nullptr;
			USkeletalMesh* Body = ElysiumNpcVisual::LoadMesh(Candidate, Unused, LoadError);
			if (Body == nullptr || Body->GetRefSkeleton().FindBoneIndex(FName(TEXT("Bip01 Head")))
				== INDEX_NONE)
			{
				continue;
			}
			UAnimSequence* Clip = ElysiumNpcVisual::LoadBakedClip(
				Body, TEXT("character_shared_male_stances"), TEXT("Stance_Neutral_Idle_1"));
			const USkeleton* BodySkeleton = Body->GetSkeleton();
			if (Clip == nullptr || BodySkeleton == nullptr)
			{
				continue;
			}
			if (Clip->GetSkeleton() != BodySkeleton
				&& !BodySkeleton->IsCompatibleForEditor(Clip->GetSkeleton()))
			{
				continue;
			}
			Stem = Candidate;
			Mesh = Body;
			Anim = Clip;
			break;
		}
		if (Anim == nullptr)
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body can evaluate the shared male stances neutral idle"));
			return true;
		}
		const IAnimationDataModel* Model = Anim->GetDataModel();
		if (Model == nullptr)
		{
			Test.AddError(TEXT("neutral stance: the baked clip carries no animation model"));
			return false;
		}

		// Composed up the mesh's own reference skeleton. A bone the clip does not track holds its
		// bind pose, which is what the evaluator does with it too.
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		auto ComponentSpace = [&](const FName BoneName) -> FTransform
		{
			FTransform Out = FTransform::Identity;
			int32 Index = Ref.FindBoneIndex(BoneName);
			while (Index != INDEX_NONE)
			{
				const FName Name = Ref.GetBoneName(Index);
				FTransform Local = Ref.GetRefBonePose()[Index];
				if (Model->IsValidBoneTrackName(Name))
				{
					Local = Model->GetBoneTrackTransform(Name, FFrameNumber(0));
				}
				Out = Out * Local;
				Index = Ref.GetParentIndex(Index);
			}
			return Out;
		};

		const int32 PelvisIndex = Ref.FindBoneIndex(FName(TEXT("Bip01 Pelvis")));
		const int32 HeadIndex = Ref.FindBoneIndex(FName(TEXT("Bip01 Head")));
		if (PelvisIndex == INDEX_NONE || HeadIndex == INDEX_NONE)
		{
			Test.AddError(FString::Printf(
				TEXT("neutral stance: %s has no Bip01 Pelvis / Bip01 Head"), *Stem));
			return false;
		}
		const FVector Pelvis = ComponentSpace(FName(TEXT("Bip01 Pelvis"))).GetTranslation();
		const FVector Head = ComponentSpace(FName(TEXT("Bip01 Head"))).GetTranslation();

		// Unreal-native and in centimetres: Z is up, Y is lateral.
		const double Rise = FMath::Abs(Head.Z - Pelvis.Z);
		const double Lateral = FMath::Abs(Head.Y - Pelvis.Y);
		if (Rise < 1.0 || Lateral / Rise > 0.05)
		{
			Test.AddError(FString::Printf(
				TEXT("baked neutral stance leans sideways: lateral %.3f cm / rise %.3f cm"),
				Lateral, Rise));
			return false;
		}
		Test.AddInfo(FString::Printf(
			TEXT("baked neutral stance on %s: lateral %.3f cm / rise %.3f cm = %.4f"),
			*Stem, Lateral, Rise, Lateral / Rise));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalGlbContractsTest,
	"Elysium.Content.SkeletalGlbContracts", GElysiumSkeletalContentFlags)
bool FElysiumSkeletalGlbContractsTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain(s) are marked incomplete"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no NPC index (%s)"), *Error));
		return true;
	}

	FGltfStats Stats;
	bool bValid = true;
	int32 SplitBodies = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		FGltfContract Contract(*this, FElysiumContentPaths::NpcBankGlb(Pair.Value.Glb));
		bValid &= Contract.Load() && Contract.Validate(/*bRequireSkin=*/true, Stats);
		if (!Pair.Value.SplitRotationBones.IsEmpty())
		{
			++SplitBodies;
			if (Pair.Value.SplitRotationBones.Num() != 1
				|| !Pair.Value.SplitRotationBones[0].Equals(
					TEXT("Bip01 Spine1"), ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(
					TEXT("%s carries an unexpected Flags & 2 inventory: %s"),
					*Pair.Key, *FString::Join(Pair.Value.SplitRotationBones, TEXT(", "))));
				bValid = false;
			}
		}
	}
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Banks)
	{
		FGltfContract Contract(*this, FElysiumContentPaths::NpcBankGlb(Pair.Value.Glb));
		const bool bContractValid = Contract.Load()
			&& Contract.Validate(/*bRequireSkin=*/false, Stats);
		bValid &= bContractValid;
	}
	bValid &= ValidateBakedNeutralStance(*this, Index);

	AddInfo(FString::Printf(TEXT("validated %lld GLBs: %lld joints, %lld weighted vertices, "
		"%lld clips, %lld channels, %lld sampled transforms; %d target bodies carry "
		"Flags & 2 metadata"), Stats.Files, Stats.Joints, Stats.Vertices, Stats.Clips,
		Stats.Channels, Stats.Samples, SplitBodies));
	if (Index.Npcs.Num() >= 150)
	{
		TestTrue(TEXT("the complete generated cast preserves target-model Flags & 2 metadata"),
			SplitBodies > 100);
	}
	else
	{
		AddInfo(FString::Printf(TEXT("partial NPC index: validated Flags & 2 metadata on %d of %d bodies"),
			SplitBodies, Index.Npcs.Num()));
	}
	if (const FElysiumNpcIndexEntry* CourtroomBody =
		Index.Npcs.Find(TEXT("ventrue_female_armor_1")))
	{
		TestTrue(TEXT("the seated courtroom player body records Bip01 Spine1 Flags & 2"),
			CourtroomBody->SplitRotationBones.Contains(TEXT("Bip01 Spine1")));
	}
	else
	{
		AddError(TEXT("ventrue_female_armor_1 is absent from the generated target-model index"));
		bValid = false;
	}
	TestTrue(TEXT("every generated skeletal GLB satisfies the binding and animation contract"), bValid);
	return true;
}

namespace
{
	const FString* FindKeyIgnoreCase(const TMap<FString, FString>& Keys, const FString& Wanted)
	{
		for (const TPair<FString, FString>& Pair : Keys)
		{
			if (Pair.Key.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return &Pair.Value;
			}
		}
		return nullptr;
	}

	FString NormalizeModelPath(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		Path.ToLowerInline();
		if (!Path.StartsWith(TEXT("models/")))
		{
			Path = TEXT("models/") + Path;
		}
		return Path;
	}

	const FElysiumEntityDef* FindEntityByTarget(
		const FElysiumEntityDefs& Defs, const FString& Target)
	{
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.TargetName.Equals(Target, ESearchCase::IgnoreCase))
			{
				return &Def;
			}
		}
		return nullptr;
	}

	FString ResolveSceneActorTarget(const FElysiumEntityDef& SceneDef, const FString& ActorName)
	{
		if (ActorName.Equals(TEXT("Player"), ESearchCase::IgnoreCase)
			|| ActorName.Equals(TEXT("!player"), ESearchCase::IgnoreCase)
			|| ActorName.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
		{
			return TEXT("!playercontroller");
		}
		for (int32 TargetIndex = 1; TargetIndex <= 4; ++TargetIndex)
		{
			const FString Token = FString::Printf(TEXT("!target%d"), TargetIndex);
			if (ActorName.Equals(Token, ESearchCase::IgnoreCase))
			{
				const FString* Value = FindKeyIgnoreCase(
					SceneDef.Keys, FString::Printf(TEXT("target%d"), TargetIndex));
				return Value != nullptr ? *Value : FString();
			}
		}
		return ActorName;
	}

	FString StemForModel(const FElysiumNpcIndex& Index, const FString& Model)
	{
		const FString Wanted = NormalizeModelPath(Model);
		for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
		{
			if (NormalizeModelPath(Pair.Value.Model) == Wanted)
			{
				return Pair.Key;
			}
		}
		return FString();
	}
}

// PP2's failure-sensitive integration: parse sp_theatre's real scene entities and VCDs, map each
// `entire_scene` actor through bonerename -> cinematic bank and targetname -> character model,
// then ask glTFRuntime to bind that bank clip to the target mesh's actual USkeleton under -nullrhi.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreSkeletonBindingTest,
	"Elysium.Content.TheatreSkeletonBinding", GElysiumSkeletalContentFlags)
bool FElysiumTheatreSkeletonBindingTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("maps"))
		|| FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the maps and npc export domain(s) are marked incomplete"));
		return true;
	}
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre is not exported"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre.ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("NPC/cinematic index loads"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}

	TMap<FString, USkeletalMesh*> MeshCache;
	TMap<FString, UglTFRuntimeAsset*> BankAssetCache;
	TArray<UObject*> KeepAlive;
	int32 SceneSets = 0;
	int32 ActorRoots = 0;
	int32 RuntimeBinds = 0;
	int32 PlayerBinds = 0;
	bool bValid = true;

	for (const FElysiumEntityDef& SceneDef : Defs.Defs)
	{
		if (SceneDef.Classname != TEXT("logic_choreographed_scene"))
		{
			continue;
		}
		const FString* SceneFile = FindKeyIgnoreCase(SceneDef.Keys, TEXT("SceneFile"));
		if (SceneFile == nullptr) continue;
		const TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
		if (!Scene.IsValid())
		{
			AddError(FString::Printf(TEXT("%s: VCD does not parse"), *SceneDef.TargetName));
			bValid = false;
			continue;
		}

		TSet<int32> WholeCastActors;
		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if ((Event.Type == EElysiumChoreoEvent::Sequence || Event.Type == EElysiumChoreoEvent::Gesture)
				&& Event.Param.Equals(TEXT("entire_scene"), ESearchCase::IgnoreCase))
			{
				WholeCastActors.Add(Event.ActorIndex);
			}
		}
		if (WholeCastActors.IsEmpty()) continue;

		TArray<FString> AnimModels;
		for (const TCHAR* Key : { TEXT("BaseAnim"), TEXT("MaleAnim"), TEXT("FemaleAnim") })
		{
			if (const FString* Model = FindKeyIgnoreCase(SceneDef.Keys, Key))
			{
				if (!Model->IsEmpty()) AnimModels.AddUnique(*Model);
			}
		}

		for (const FString& AnimModel : AnimModels)
		{
			++SceneSets;
			const FElysiumCinematicSet* Set = Index.FindCinematic(AnimModel);
			if (Set == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: anim set is not indexed: %s"),
					*SceneDef.TargetName, *AnimModel));
				bValid = false;
				continue;
			}

			for (int32 ActorIndex : WholeCastActors)
			{
				if (!Scene->Actors.IsValidIndex(ActorIndex))
				{
					AddError(FString::Printf(TEXT("%s: entire_scene has invalid actor index %d"),
						*SceneDef.TargetName, ActorIndex));
					bValid = false;
					continue;
				}
				const FElysiumSceneActor& Actor = Scene->Actors[ActorIndex];
				const FString Bank = Set->BankForRoot(Actor.BoneFrom);
				++ActorRoots;
				if (Bank.IsEmpty() || !Index.Banks.Contains(Bank))
				{
					AddError(FString::Printf(TEXT("%s: %s root '%s' does not resolve exactly in %s"),
						*SceneDef.TargetName, *Actor.Name, *Actor.BoneFrom, *AnimModel));
					bValid = false;
					continue;
				}

				const FString Target = ResolveSceneActorTarget(SceneDef, Actor.Name);
				const bool bPlayerController =
					Target.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase);
				if (bPlayerController)
				{
					++PlayerBinds;
				}
				const FElysiumEntityDef* TargetDef =
					bPlayerController ? nullptr : FindEntityByTarget(Defs, Target);
				const FString* TargetModel = TargetDef != nullptr
					? FindKeyIgnoreCase(TargetDef->Keys, TEXT("model")) : nullptr;
				const FString Stem = bPlayerController
					? TEXT("brujah_male_armor_0")
					: (TargetModel != nullptr ? StemForModel(Index, *TargetModel) : FString());
				if (Stem.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s: actor %s target '%s' has no indexed skeletal model"),
						*SceneDef.TargetName, *Actor.Name, *Target));
					bValid = false;
					continue;
				}

				USkeletalMesh* Mesh = MeshCache.FindRef(Stem);
				if (Mesh == nullptr)
				{
					// The mount is the only build of a character, so a body comes back as a real
					// asset with NO parsed glb beside it -- the out-asset is null on that path by
					// design and only a bank still carries one.
					UglTFRuntimeAsset* Unused = nullptr;
					Mesh = ElysiumNpcVisual::LoadMesh(Stem, Unused, Error);
					if (Mesh == nullptr)
					{
						AddError(FString::Printf(TEXT("%s: target mesh %s failed strict load: %s"),
							*SceneDef.TargetName, *Stem, *Error));
						bValid = false;
						continue;
					}
					MeshCache.Add(Stem, Mesh);
					KeepAlive.Add(Mesh);
					Mesh->AddToRoot();
				}

				UglTFRuntimeAsset* BankAsset = BankAssetCache.FindRef(Bank);
				if (BankAsset == nullptr)
				{
					BankAsset = ElysiumNpcVisual::LoadAssetFromPath(Index.BankGlbPath(Bank), Error);
					if (BankAsset == nullptr)
					{
						AddError(FString::Printf(TEXT("%s: bank %s failed load: %s"),
							*SceneDef.TargetName, *Bank, *Error));
						bValid = false;
						continue;
					}
					BankAssetCache.Add(Bank, BankAsset);
					KeepAlive.Add(BankAsset);
					BankAsset->AddToRoot();
				}

				UAnimSequence* Anim = ElysiumNpcVisual::RetargetClip(
					BankAsset, Mesh, TEXT("entire_scene"), Error);
				if (Anim == nullptr || Anim->GetSkeleton() != Mesh->GetSkeleton()
					|| Anim->GetPlayLength() <= 0.f)
				{
					AddError(FString::Printf(TEXT("%s: %s/%s did not bind entire_scene to %s: %s"),
						*SceneDef.TargetName, *AnimModel, *Actor.BoneFrom, *Stem, *Error));
					bValid = false;
					continue;
				}
#if WITH_EDITOR
				IAnimationDataModel* DataModel = Anim->GetDataModel();
				TArray<FName> TrackNames;
				if (DataModel != nullptr) DataModel->GetBoneTrackNames(TrackNames);
				if (TrackNames.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s: %s produced an animation with no bone tracks"),
						*SceneDef.TargetName, *Stem));
					bValid = false;
					continue;
				}
				const int32 LastFrame = DataModel->GetNumberOfFrames();
				for (const FName TrackName : TrackNames)
				{
					if (Mesh->GetRefSkeleton().FindBoneIndex(TrackName) == INDEX_NONE)
					{
						AddError(FString::Printf(TEXT("%s: retargeted track '%s' is absent from %s"),
							*SceneDef.TargetName, *TrackName.ToString(), *Stem));
						bValid = false;
						break;
					}
					for (const int32 Frame : { 0, LastFrame / 2, LastFrame })
					{
						const FTransform Pose = DataModel->GetBoneTrackTransform(TrackName, FFrameNumber(Frame));
						if (Pose.ContainsNaN() || !Pose.IsRotationNormalized())
						{
							AddError(FString::Printf(TEXT("%s: %s track '%s' has an invalid pose at frame %d"),
								*SceneDef.TargetName, *Stem, *TrackName.ToString(), Frame));
							bValid = false;
							break;
						}
					}
				}
#endif
				KeepAlive.Add(Anim);
				Anim->AddToRoot();
				++RuntimeBinds;
			}
		}
	}

	for (UObject* Object : KeepAlive)
	{
		if (Object != nullptr && Object->IsRooted()) Object->RemoveFromRoot();
	}
	AddInfo(FString::Printf(TEXT("sp_theatre: %d anim sets, %d exact actor roots, %d runtime "
		"USkeleton binds; %d bind the default player body"),
		SceneSets, ActorRoots, RuntimeBinds, PlayerBinds));
	TestTrue(TEXT("every theatre cinematic clip binds to its target model's USkeleton"), bValid);
	TestTrue(TEXT("the theatre test exercised runtime bindings"), RuntimeBinds > 0);
	return true;
}

// The binding test above proves that the cinematic clip can be constructed, but not that the
// native animation host evaluates it into a changing component pose. Drive one real theatre
// `sequence` event through the same UAnimSequence and UElysiumNpcAnimInstance used by play, seek it
// to two authored scene times, and compare the bone transforms skinning consumes. This is pose data
// only: no viewport, RHI, screenshot, or image comparison.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreSequenceEvaluationTest,
	"Elysium.Content.TheatreSequenceEvaluation", GElysiumSkeletalContentFlags)
bool FElysiumTheatreSequenceEvaluationTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre is not exported"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre.ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}

	const FElysiumEntityDef* SceneDef = nullptr;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("courtroom_scene_bip4"), ESearchCase::IgnoreCase))
		{
			SceneDef = &Def;
			break;
		}
	}
	if (!TestNotNull(TEXT("Nines' courtroom scene exists"), SceneDef))
	{
		return false;
	}

	const FString* SceneFile = FindKeyIgnoreCase(SceneDef->Keys, TEXT("SceneFile"));
	const FString* AnimModel = FindKeyIgnoreCase(SceneDef->Keys, TEXT("BaseAnim"));
	if (!TestNotNull(TEXT("the scene names a VCD"), SceneFile)
		|| !TestNotNull(TEXT("the scene names a cinematic animation set"), AnimModel))
	{
		return false;
	}
	const TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
	if (!TestTrue(TEXT("Nines' courtroom VCD parses"), Scene.IsValid()))
	{
		return false;
	}

	const FElysiumSceneEvent* SequenceEvent = nullptr;
	const FElysiumSceneActor* SceneActor = nullptr;
	for (const FElysiumSceneEvent& Event : Scene->Events)
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			|| !Event.Param.Equals(TEXT("entire_scene"), ESearchCase::IgnoreCase)
			|| !Scene->Actors.IsValidIndex(Event.ActorIndex))
		{
			continue;
		}
		const FElysiumSceneActor& Candidate = Scene->Actors[Event.ActorIndex];
		if (Candidate.Name.Equals(TEXT("Nines"), ESearchCase::IgnoreCase))
		{
			SequenceEvent = &Event;
			SceneActor = &Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("the VCD carries Nines' entire_scene sequence event"), SequenceEvent)
		|| !TestNotNull(TEXT("the sequence event has an actor"), SceneActor))
	{
		return false;
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("NPC/cinematic index loads"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}
	const FElysiumCinematicSet* Set = Index.FindCinematic(*AnimModel);
	if (!TestNotNull(TEXT("the cinematic animation set is indexed"), Set))
	{
		return false;
	}
	const FString Bank = Set->BankForRoot(SceneActor->BoneFrom);
	if (!TestTrue(TEXT("Nines' bonerename root resolves a cinematic bank"),
		!Bank.IsEmpty() && Index.Banks.Contains(Bank)))
	{
		return false;
	}

	const FString Target = ResolveSceneActorTarget(*SceneDef, SceneActor->Name);
	const FElysiumEntityDef* TargetDef = FindEntityByTarget(Defs, Target);
	const FString* TargetModel = TargetDef != nullptr
		? FindKeyIgnoreCase(TargetDef->Keys, TEXT("model")) : nullptr;
	const FString Stem = TargetModel != nullptr ? StemForModel(Index, *TargetModel) : FString();
	if (!TestTrue(TEXT("the sequence actor resolves Nines' target model"), !Stem.IsEmpty()))
	{
		return false;
	}

	UglTFRuntimeAsset* MeshAsset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, MeshAsset, Error);
	if (!TestNotNull(TEXT("Nines' target mesh loads"), Mesh))
	{
		AddError(Error);
		return false;
	}
	UglTFRuntimeAsset* BankAsset = ElysiumNpcVisual::LoadAssetFromPath(
		Index.BankGlbPath(Bank), Error);
	if (!TestNotNull(TEXT("Nines' cinematic bank loads"), BankAsset))
	{
		AddError(Error);
		return false;
	}
	UAnimSequence* Anim = ElysiumNpcVisual::RetargetClip(
		BankAsset, Mesh, SequenceEvent->Param, Error);
	if (!TestNotNull(TEXT("the sequence event retargets its clip"), Anim))
	{
		AddError(Error);
		return false;
	}

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("a skeletal body owner spawned"), Owner))
	{
		return false;
	}

	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	Owner->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance());
	if (!TestNotNull(TEXT("the production NPC animation host is installed"), Inst))
	{
		return false;
	}

	const auto PoseAt = [Comp, Inst, Anim](float Seconds, TArray<FTransform>& OutPose)
	{
		Inst->PlayClip(Anim, /*bLoop=*/false, /*BlendSeconds=*/0.f);
		Inst->SeekClip(Seconds);
		// Seek is consumed by UpdateAnimationNode; refresh synchronously so the transforms copied below
		// are this authored pose rather than the component's previous evaluation.
		Comp->TickAnimation(0.f, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
		OutPose = Comp->GetComponentSpaceTransforms();
	};

	const float EventSeconds = SequenceEvent->EndTime - SequenceEvent->StartTime;
	const float LaterSeconds = FMath::Min(120.f, FMath::Max(0.f, EventSeconds - 1.f / 30.f));
	if (!TestTrue(TEXT("the real sequence spans the later sample"), LaterSeconds > 1.f))
	{
		return false;
	}
	TArray<FTransform> StartPose;
	TArray<FTransform> LaterPose;
	PoseAt(0.f, StartPose);
	PoseAt(LaterSeconds, LaterPose);
	if (!TestEqual(TEXT("both evaluations pose the complete skeleton"),
		StartPose.Num(), LaterPose.Num()) || StartPose.Num() != Mesh->GetRefSkeleton().GetNum())
	{
		return false;
	}

	int32 ChangedBones = 0;
	float MaxAngleDegrees = 0.f;
	float MaxTranslationCm = 0.f;
	FName MostChangedBone = NAME_None;
	for (int32 BoneIndex = 1; BoneIndex < StartPose.Num(); ++BoneIndex)
	{
		const float Angle = FMath::RadiansToDegrees(StartPose[BoneIndex].GetRotation().AngularDistance(
			LaterPose[BoneIndex].GetRotation()));
		const float Translation = FVector::Distance(
			StartPose[BoneIndex].GetTranslation(), LaterPose[BoneIndex].GetTranslation());
		if (Angle > 0.01f || Translation > 0.01f)
		{
			++ChangedBones;
		}
		if (Angle > MaxAngleDegrees || Translation > MaxTranslationCm)
		{
			MaxAngleDegrees = FMath::Max(MaxAngleDegrees, Angle);
			MaxTranslationCm = FMath::Max(MaxTranslationCm, Translation);
			MostChangedBone = Mesh->GetRefSkeleton().GetBoneName(BoneIndex);
		}
	}

	AddInfo(FString::Printf(TEXT("%s.%s: evaluated '%s' at 0.000s and %.3fs; %d/%d non-root "
		"bones changed (max %.2f deg, %.2f cm; representative '%s')"),
		*SceneDef->TargetName, *SceneActor->Name, *SequenceEvent->Param, LaterSeconds,
		ChangedBones, StartPose.Num() - 1, MaxAngleDegrees, MaxTranslationCm,
		*MostChangedBone.ToString()));
	TestTrue(TEXT("the cinematic sequence event produces a changing skeletal pose"), ChangedBones > 0);
	return true;
}

// =====================================================================================
// Blend-grid sidecars against the corpus they describe (CAP7.3).
//
// Two contracts. The cheap one: every cell that names a clip names an animation that is actually in
// the owning glb, because the runtime addresses cells by name and a miss would be a silent still
// body. The load-bearing one: the neutral pose selects the FORWARD cell of a locomotion fan. The
// clip baked under the label `walk` is the grid's base cell, which on a -180..180 fan is the
// backward walk — resolving the grid is the only thing that makes `walk` mean walk.
// =====================================================================================

namespace
{
	// The animation names in a glb's JSON chunk, lowercased. `FGltfContract` keeps its parsed root
	// private and validates far more than this needs — a cell check only has to know which names the
	// file carries, and reading the chunk directly keeps this out of the rendering path.
	bool ReadGlbAnimationNames(const FString& Path, TSet<FString>& Out)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 20
			|| ReadU32(Bytes, 0) != GlbMagic)
		{
			return false;
		}
		int32 Offset = 12;
		while (Offset + 8 <= Bytes.Num())
		{
			const int32 Length = static_cast<int32>(ReadU32(Bytes, Offset));
			const uint32 Type = ReadU32(Bytes, Offset + 4);
			Offset += 8;
			if (Length < 0 || Offset + Length > Bytes.Num())
			{
				return false;
			}
			if (Type == JsonChunk)
			{
				const FUTF8ToTCHAR Convert(
					reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Length);
				const FString Text(Convert.Length(), Convert.Get());
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
				if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
				{
					return false;
				}
				if (const TArray<TSharedPtr<FJsonValue>>* Animations = JsonArray(Root, TEXT("animations")))
				{
					for (const TSharedPtr<FJsonValue>& Value : *Animations)
					{
						Out.Add(JsonString(Value->AsObject(), TEXT("name")).ToLower());
					}
				}
				return true;
			}
			Offset += Length;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlendGridCorpusTest,
	"Elysium.Content.BlendGrids", GElysiumSkeletalContentFlags)
bool FElysiumBlendGridCorpusTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("npc_index parses"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}

	// Every group that can declare grids, gathered as (stem, sidecar, glb) so one loop covers
	// characters, banks and animated props rather than three near-copies.
	TArray<TTuple<FString, FString, FString>> Owners;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (!Pair.Value.Blends.IsEmpty())
		{
			Owners.Emplace(Pair.Key, Pair.Value.Blends, Pair.Value.Glb);
		}
	}
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Banks)
	{
		if (!Pair.Value.Blends.IsEmpty())
		{
			Owners.Emplace(Pair.Key, Pair.Value.Blends, Pair.Value.Glb);
		}
	}
	for (const TPair<FString, FElysiumAnimatedPropEntry>& Pair : Index.AnimatedProps)
	{
		if (!Pair.Value.Blends.IsEmpty())
		{
			Owners.Emplace(Pair.Value.Stem, Pair.Value.Blends, Pair.Value.Glb);
		}
	}
	if (Owners.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: this export declares no blend grids"));
		return true;
	}

	int32 Grids = 0, Cells = 0, NullCells = 0;
	bool bValid = true;
	for (const TTuple<FString, FString, FString>& Owner : Owners)
	{
		FElysiumBlendTable Table;
		FString TableError;
		if (!Table.Load(Owner.Get<1>(), TableError) || !Table.IsValid())
		{
			AddError(FString::Printf(TEXT("blends '%s': %s"), *Owner.Get<0>(), *TableError));
			bValid = false;
			continue;
		}

		// The owning glb's animation names, read straight out of the JSON chunk — no rendering
		// resources, which is what keeps this in the headless tier.
		TSet<FString> Baked;
		if (!ReadGlbAnimationNames(FElysiumContentPaths::NpcBankGlb(Owner.Get<2>()), Baked))
		{
			AddError(FString::Printf(TEXT("cannot read animations from %s's glb '%s'"),
				*Owner.Get<0>(), *Owner.Get<2>()));
			bValid = false;
			continue;
		}

		for (const TPair<FString, FElysiumBlendGrid>& Pair : Table.Grids)
		{
			++Grids;
			int32 Playable = 0;
			for (const FElysiumBlendCell& Cell : Pair.Value.Cells)
			{
				++Cells;
				if (Cell.Clip.IsEmpty())
				{
					++NullCells;
					continue;
				}
				++Playable;
				if (!Baked.Contains(Cell.Clip.ToLower()))
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s' cell [%d,%d] names '%s', which its glb does not carry"),
						*Owner.Get<0>(), *Pair.Key, Cell.Axis[0], Cell.Axis[1], *Cell.Clip));
					bValid = false;
				}
			}
			// The exporter drops any grid left with fewer than two live cells, so a grid that
			// reaches the runtime always has something to blend between.
			if (Playable < 2)
			{
				AddError(FString::Printf(TEXT("%s grid '%s' has %d playable cell(s)"),
					*Owner.Get<0>(), *Pair.Key, Playable));
				bValid = false;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d owner(s), %d grid(s), %d cell(s), %d unbaked"),
		Owners.Num(), Grids, Cells, NullCells));
	TestTrue(TEXT("every blend cell names an animation its glb carries"), bValid);

	// The regression that started this: the shared female locomotion fan at rest.
	FElysiumBlendTable Female;
	FString FemaleError;
	if (Female.Load(TEXT("blends/character_shared_female_move_and_ranged.json"), FemaleError))
	{
		if (const FElysiumBlendGrid* Walk = Female.Find(TEXT("walk")))
		{
			TestEqual(TEXT("`walk` is a nine-cell fan"), Walk->Cells.Num(), 9);
			const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Walk, Female,
				FElysiumPoseParams::Neutral());
			if (TestNotNull(TEXT("the neutral pose resolves a cell"), Pick.Cell))
			{
				// `walk` itself is the -180 cell's baked name. Resolving to it would be the bug.
				TestEqual(TEXT("a resting body walks FORWARD, not backward"), Pick.Cell->Clip,
					FString(TEXT("walk_0")));
			}
		}
		else
		{
			AddError(TEXT("the female locomotion bank declares no `walk` grid"));
		}

		if (const FElysiumBlendGrid* Run = Female.Find(TEXT("run")))
		{
			const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(*Run, Female,
				FElysiumPoseParams::Neutral());
			if (TestNotNull(TEXT("the run fan resolves a cell"), Pick.Cell))
			{
				TestEqual(TEXT("...and it runs forward too"), Pick.Cell->Clip,
					FString(TEXT("npc_run_0")));
			}
		}
	}
	else
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no female locomotion bank in this export (%s)"), *FemaleError));
	}

	return true;
}

// The autolayer binding: which clips a host sequence is composed with, and in what order.
//
// Every assertion here crosses the table against something ANOTHER export declares — an animation
// the owner's glb carries, a flag word in the clip vocabulary — rather than against the table's own
// arithmetic. Whether the table is faithful to the `.mdl` is measured offline against the install,
// which bring-your-own-game keeps out of the repo; what this asserts is that the binding survives
// export, resolves in the namespace a standing body actually addresses, and keeps its order.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAutoLayerBindingTest,
	"Elysium.Content.AutoLayers", GElysiumSkeletalContentFlags)
bool FElysiumAutoLayerBindingTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(TEXT("npc_index parses"), Index.Load(Error)))
	{
		AddError(Error);
		return false;
	}

	// Owner -> its table, for every owner declaring a binding. Only the two shared locomotion banks
	// carry any, which is itself the census: a third carrier would be a decode fault, not content.
	TMap<FString, FElysiumBlendTable> Tables;
	TMap<FString, FString> OwnerGlb;
	int32 Hosts = 0, Entries = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Banks)
	{
		if (Pair.Value.Blends.IsEmpty())
		{
			continue;
		}
		FElysiumBlendTable Table;
		FString TableError;
		if (!Table.Load(Pair.Value.Blends, TableError) || Table.AutoLayers.IsEmpty())
		{
			continue;
		}
		Hosts += Table.AutoLayers.Num();
		for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Table.AutoLayers)
		{
			Entries += Binding.Value.Clips.Num();
		}
		OwnerGlb.Add(Pair.Key, Pair.Value.Glb);
		Tables.Add(Pair.Key, MoveTemp(Table));
	}
	if (Tables.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: this export declares no autolayer binding"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d owner(s), %d host(s), %d entr(ies)"),
		Tables.Num(), Hosts, Entries));

	// 1. Structure, against the owner's own baked animations.
	bool bStructure = true;
	for (const TPair<FString, FElysiumBlendTable>& Owner : Tables)
	{
		TSet<FString> Baked;
		if (!ReadGlbAnimationNames(FElysiumContentPaths::NpcBankGlb(OwnerGlb[Owner.Key]), Baked))
		{
			AddError(FString::Printf(TEXT("cannot read animations from %s's glb"), *Owner.Key));
			bStructure = false;
			continue;
		}
		for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Owner.Value.AutoLayers)
		{
			const TArray<FString>& Clips = Binding.Value.Clips;
			// Fan-out is 2 on shipped content and the runtime allocates for exactly that. A third
			// entry would evict one silently, so it is an error here rather than a surprise there.
			if (Clips.Num() > 2)
			{
				AddError(FString::Printf(TEXT("%s host '%s' declares %d entries"),
					*Owner.Key, *Binding.Key, Clips.Num()));
				bStructure = false;
			}
			if (!Baked.Contains(Binding.Key.ToLower()))
			{
				AddError(FString::Printf(TEXT("%s host '%s' is not an animation of its own glb"),
					*Owner.Key, *Binding.Key));
				bStructure = false;
			}
			TSet<FString> Seen;
			for (const FString& Clip : Clips)
			{
				if (!Baked.Contains(Clip.ToLower()))
				{
					AddError(FString::Printf(TEXT("%s host '%s' names layer '%s', which its glb "
						"does not carry"), *Owner.Key, *Binding.Key, *Clip));
					bStructure = false;
				}
				if (Clip.Equals(Binding.Key, ESearchCase::IgnoreCase))
				{
					AddError(FString::Printf(TEXT("%s host '%s' layers itself"),
						*Owner.Key, *Binding.Key));
					bStructure = false;
				}
				bool bDuplicate = false;
				Seen.Add(Clip.ToLower(), &bDuplicate);
				if (bDuplicate)
				{
					AddError(FString::Printf(TEXT("%s host '%s' names '%s' twice"),
						*Owner.Key, *Binding.Key, *Clip));
					bStructure = false;
				}
				// Depth 1: the dispatcher recurses, and shipped content never gives it the chance.
				// A layer that is itself a host would compose one, which nothing downstream expects.
				if (Owner.Value.FindAutoLayers(Clip) != nullptr)
				{
					AddError(FString::Printf(TEXT("%s layer '%s' is itself a host"),
						*Owner.Key, *Clip));
					bStructure = false;
				}
			}
		}
	}
	TestTrue(TEXT("every binding resolves inside its owner and stays depth 1"), bStructure);

	// 2. Kind and order, against the clip vocabulary a standing body addresses. This is the
	// resolution that matters: a body reaches its bank's clips through its own slice, so a target
	// that resolves in the bank and not here would bind to nothing at runtime.
	int32 Bodies = 0, Resolved = 0, OverlayFirst = 0, AdditiveFirst = 0, Singles = 0;
	bool bVocabulary = true;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Npc : Index.Npcs)
	{
		FElysiumNpcClipSet Set;
		FString SetError;
		if (!Set.Load(Npc.Key, SetError))
		{
			continue;
		}
		bool bCounted = false;
		for (const TPair<FString, FElysiumNpcClip>& Clip : Set.Clips)
		{
			const FElysiumBlendTable* Table = Tables.Find(Clip.Value.Owner);
			const FElysiumAutoLayerBinding* Binding =
				Table != nullptr ? Table->FindAutoLayers(Clip.Key) : nullptr;
			if (Binding == nullptr)
			{
				continue;
			}
			if (!bCounted)
			{
				++Bodies;
				bCounted = true;
			}
			++Resolved;

			TArray<bool> Additive;
			for (const FString& Layer : Binding->Clips)
			{
				const FElysiumNpcClip* Target = Set.Find(Layer);
				if (Target == nullptr)
				{
					AddError(FString::Printf(TEXT("%s: host '%s' names layer '%s', absent from "
						"this body's vocabulary"), *Npc.Key, *Clip.Key, *Layer));
					bVocabulary = false;
					continue;
				}
				// A layer is composed, never selected. Retail's own census: no target carries an
				// activity, so one that did would be reachable as a base clip and flatten the body.
				if (!Target->Activity.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s: layer '%s' carries activity '%s'"),
						*Npc.Key, *Layer, *Target->Activity));
					bVocabulary = false;
				}
				Additive.Add(Target->IsAdditive());
			}
			// Exactly one of each on a two-entry host, and an overlay alone on a one-entry host —
			// the flags say which, and the table says the order. Both are asserted; neither is
			// assumed, and the order is deliberately NOT constrained: one shipped host declares its
			// additive first, and a consumer that sorts would compose it differently from retail.
			if (Additive.Num() == 2)
			{
				if (Additive[0] == Additive[1])
				{
					AddError(FString::Printf(TEXT("%s: host '%s' declares two layers of the same "
						"kind (additive=%d)"), *Npc.Key, *Clip.Key, Additive[0] ? 1 : 0));
					bVocabulary = false;
				}
				else if (Additive[0])
				{
					++AdditiveFirst;
				}
				else
				{
					++OverlayFirst;
				}
			}
			else if (Additive.Num() == 1)
			{
				++Singles;
				if (Additive[0])
				{
					AddError(FString::Printf(TEXT("%s: host '%s' declares a lone additive"),
						*Npc.Key, *Clip.Key));
					bVocabulary = false;
				}
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("%d bod(ies) reach %d host(s): %d overlay-first, %d additive-first, %d single overlay"),
		Bodies, Resolved, OverlayFirst, AdditiveFirst, Singles));
	TestTrue(TEXT("every layer resolves in the body's own vocabulary, carries no activity, and "
		"pairs one overlay with one additive"), bVocabulary);
	TestTrue(TEXT("some body reaches a host"), Resolved > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
