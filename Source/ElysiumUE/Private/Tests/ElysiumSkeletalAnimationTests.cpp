// Headless contracts for the MDL -> glTF -> glTFRuntime skeletal-animation seam. The structural
// test reads every generated NPC/bank GLB without constructing rendering resources; the theatre
// test then exercises the real runtime loader and UAnimSequence-to-USkeleton binding on PP2's cast.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumSceneData.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
		bool ValidateNeutralStanceLean();

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

	bool FGltfContract::ValidateNeutralStanceLean()
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = JsonArray(Root, TEXT("nodes"));
		const TArray<TSharedPtr<FJsonValue>>* Animations = JsonArray(Root, TEXT("animations"));
		if (Nodes == nullptr || Animations == nullptr)
		{
			return Fail(TEXT("neutral-stance regression has no nodes or animations"));
		}

		TSharedPtr<FJsonObject> Neutral;
		for (const TSharedPtr<FJsonValue>& Value : *Animations)
		{
			const TSharedPtr<FJsonObject> Animation = Value->AsObject();
			if (JsonString(Animation, TEXT("name")).Equals(
				TEXT("Stance_Neutral_Idle_1"), ESearchCase::IgnoreCase))
			{
				Neutral = Animation;
				break;
			}
		}
		if (!Neutral.IsValid())
		{
			return Fail(TEXT("shared male stances lost Stance_Neutral_Idle_1"));
		}

		auto VectorField = [](const TSharedPtr<FJsonObject>& Node, const TCHAR* Field,
			const TArray<double>& Default)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = JsonArray(Node, Field);
			if (Values == nullptr || Values->Num() != Default.Num()) return Default;
			TArray<double> Out;
			for (const TSharedPtr<FJsonValue>& Value : *Values) Out.Add(Value->AsNumber());
			return Out;
		};
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

		TArray<TArray<double>> Translations, Rotations;
		TArray<int32> Parents;
		Translations.SetNum(Nodes->Num());
		Rotations.SetNum(Nodes->Num());
		Parents.Init(INDEX_NONE, Nodes->Num());
		int32 Pelvis = INDEX_NONE, Head = INDEX_NONE;
		for (int32 NodeIndex = 0; NodeIndex < Nodes->Num(); ++NodeIndex)
		{
			const TSharedPtr<FJsonObject> Node = JsonObjectAt(Nodes, NodeIndex);
			Translations[NodeIndex] = VectorField(Node, TEXT("translation"), { 0.0, 0.0, 0.0 });
			Rotations[NodeIndex] = VectorField(Node, TEXT("rotation"), { 0.0, 0.0, 0.0, 1.0 });
			const FString Name = JsonString(Node, TEXT("name"));
			if (Name.Equals(TEXT("Bip01 Pelvis"), ESearchCase::IgnoreCase)) Pelvis = NodeIndex;
			if (Name.Equals(TEXT("Bip01 Head"), ESearchCase::IgnoreCase)) Head = NodeIndex;
			const TArray<TSharedPtr<FJsonValue>>* Children = JsonArray(Node, TEXT("children"));
			if (Children == nullptr) continue;
			for (const TSharedPtr<FJsonValue>& Child : *Children)
			{
				const int32 ChildIndex = static_cast<int32>(Child->AsNumber());
				if (Parents.IsValidIndex(ChildIndex)) Parents[ChildIndex] = NodeIndex;
			}
		}
		if (Pelvis == INDEX_NONE || Head == INDEX_NONE)
		{
			return Fail(TEXT("neutral-stance regression lost pelvis or head"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Channels = JsonArray(Neutral, TEXT("channels"));
		const TArray<TSharedPtr<FJsonValue>>* Samplers = JsonArray(Neutral, TEXT("samplers"));
		if (Channels == nullptr || Samplers == nullptr)
		{
			return Fail(TEXT("neutral stance has no channels or samplers"));
		}
		for (const TSharedPtr<FJsonValue>& ChannelValue : *Channels)
		{
			const TSharedPtr<FJsonObject> Channel = ChannelValue->AsObject();
			const TSharedPtr<FJsonObject>* TargetPtr = nullptr;
			if (!Channel.IsValid() || !Channel->TryGetObjectField(TEXT("target"), TargetPtr)) continue;
			const int32 NodeIndex = JsonInt(*TargetPtr, TEXT("node"));
			const FString PathName = JsonString(*TargetPtr, TEXT("path"));
			const TSharedPtr<FJsonObject> Sampler = JsonObjectAt(Samplers, JsonInt(Channel, TEXT("sampler")));
			FGltfAccessor Values;
			if (!Nodes->IsValidIndex(NodeIndex) || !Sampler.IsValid()
				|| !Accessor(JsonInt(Sampler, TEXT("output")), Values))
			{
				return Fail(TEXT("neutral stance has an invalid channel"));
			}
			if (PathName == TEXT("translation") && Values.Components == 3)
			{
				Translations[NodeIndex] = { Values.Number(0, 0), Values.Number(0, 1), Values.Number(0, 2) };
			}
			else if (PathName == TEXT("rotation") && Values.Components == 4)
			{
				Rotations[NodeIndex] = { Values.Number(0, 0), Values.Number(0, 1),
					Values.Number(0, 2), Values.Number(0, 3) };
			}
		}

		TArray<TArray<double>> Globals;
		Globals.SetNum(Nodes->Num());
		TFunction<bool(int32)> BuildGlobal = [&](int32 NodeIndex)
		{
			if (Globals[NodeIndex].Num() == 16) return true;
			const TArray<double>& T = Translations[NodeIndex];
			const TArray<double>& Q = Rotations[NodeIndex];
			const double X = Q[0], Y = Q[1], Z = Q[2], W = Q[3];
			TArray<double> Local;
			Local.Init(0.0, 16);
			Local[0] = 1.0 - 2.0 * (Y * Y + Z * Z);
			Local[1] = 2.0 * (X * Y + W * Z);
			Local[2] = 2.0 * (X * Z - W * Y);
			Local[4] = 2.0 * (X * Y - W * Z);
			Local[5] = 1.0 - 2.0 * (X * X + Z * Z);
			Local[6] = 2.0 * (Y * Z + W * X);
			Local[8] = 2.0 * (X * Z + W * Y);
			Local[9] = 2.0 * (Y * Z - W * X);
			Local[10] = 1.0 - 2.0 * (X * X + Y * Y);
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
			return true;
		};
		if (!BuildGlobal(Pelvis) || !BuildGlobal(Head))
		{
			return Fail(TEXT("neutral-stance hierarchy does not resolve"));
		}

		// mdl_gltf maps Source (X forward, Y left, Z up) to glTF (X forward, Y up, Z right).
		// A neutral pose may breathe and shift, but its first frame must not carry the former
		// systematic Spine1 cant. The bad quaternion order measured 0.1244; the recovered order
		// measures 0.0186, leaving generous room for source-data variation.
		const double Rise = FMath::Abs(Globals[Head][13] - Globals[Pelvis][13]);
		const double Lateral = FMath::Abs(Globals[Head][14] - Globals[Pelvis][14]);
		if (Rise < 1.e-4 || Lateral / Rise > 0.05)
		{
			return Fail(FString::Printf(TEXT("neutral stance leans sideways: lateral %.5f / rise %.5f"),
				Lateral, Rise));
		}
		Test.AddInfo(FString::Printf(TEXT("neutral stance lateral/rise %.5f / %.5f = %.4f"),
			Lateral, Rise, Lateral / Rise));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkeletalGlbContractsTest,
	"Elysium.Content.SkeletalGlbContracts", GElysiumSkeletalContentFlags)
bool FElysiumSkeletalGlbContractsTest::RunTest(const FString&)
{
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("skipping: no NPC index (%s)"), *Error));
		return true;
	}

	FGltfStats Stats;
	bool bValid = true;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		FGltfContract Contract(*this, FElysiumContentPaths::NpcBankGlb(Pair.Value.Glb));
		bValid &= Contract.Load() && Contract.Validate(/*bRequireSkin=*/true, Stats);
	}
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Banks)
	{
		FGltfContract Contract(*this, FElysiumContentPaths::NpcBankGlb(Pair.Value.Glb));
		const bool bContractValid = Contract.Load()
			&& Contract.Validate(/*bRequireSkin=*/false, Stats);
		bValid &= bContractValid;
		if (bContractValid && Pair.Key.Equals(
			TEXT("character_shared_male_stances"), ESearchCase::IgnoreCase))
		{
			bValid &= Contract.ValidateNeutralStanceLean();
		}
	}

	AddInfo(FString::Printf(TEXT("validated %lld GLBs: %lld joints, %lld weighted vertices, "
		"%lld clips, %lld channels, %lld sampled transforms"), Stats.Files, Stats.Joints,
		Stats.Vertices, Stats.Clips, Stats.Channels, Stats.Samples));
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
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(TEXT("skipping: sp_theatre is not exported"));
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
	TMap<FString, UglTFRuntimeAsset*> MeshAssetCache;
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
					UglTFRuntimeAsset* MeshAsset = nullptr;
					Mesh = ElysiumNpcVisual::LoadMesh(Stem, MeshAsset, Error);
					if (Mesh == nullptr || MeshAsset == nullptr)
					{
						AddError(FString::Printf(TEXT("%s: target mesh %s failed strict load: %s"),
							*SceneDef.TargetName, *Stem, *Error));
						bValid = false;
						continue;
					}
					MeshCache.Add(Stem, Mesh);
					MeshAssetCache.Add(Stem, MeshAsset);
					KeepAlive.Add(Mesh);
					KeepAlive.Add(MeshAsset);
					Mesh->AddToRoot();
					MeshAsset->AddToRoot();
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

#endif // WITH_DEV_AUTOMATION_TESTS
