#include "ElysiumGeometryVerificationLibrary.h"

#if WITH_EDITOR
#include "Animation/MorphTarget.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SkeletalMeshAttributes.h"
#include "UObject/Package.h"

namespace ElysiumGeometryVerification
{
	using FObject = TSharedRef<FJsonObject>;
	using FValues = TArray<TSharedPtr<FJsonValue>>;

	TSharedPtr<FJsonValue> Number(double Value)
	{
		// Preserve corrupt native values as explicit diagnostic tokens in valid JSON. The
		// numerical gate rejects nonfinite attributes with their vertex/channel identity.
		if (!FMath::IsFinite(Value))
			return MakeShared<FJsonValueString>(FMath::IsNaN(Value) ? TEXT("NaN") : Value < 0 ? TEXT("-Infinity") : TEXT("Infinity"));
		return MakeShared<FJsonValueNumber>(Value);
	}
	TSharedPtr<FJsonValue> Object(const FObject& Value) { return MakeShared<FJsonValueObject>(Value); }
	TSharedPtr<FJsonValue> Array(const FValues& Value) { return MakeShared<FJsonValueArray>(Value); }
	template <typename T> FValues Vector3(const T& V) { return {Number(V.X), Number(V.Y), Number(V.Z)}; }
	template <typename T> FValues Delta(int32 Index, const T& V)
	{
		return {Number(Index), Number(V.X), Number(V.Y), Number(V.Z)};
	}
}
#endif

FString UElysiumGeometryVerificationLibrary::CaptureGeometry(const USkeletalMesh* Mesh, FString& OutJson)
{
	OutJson.Reset();
#if WITH_EDITOR
	using namespace ElysiumGeometryVerification;
	if (!IsInGameThread()) return TEXT("geometry snapshot requires the editor game thread");
	if (!Mesh || Mesh->HasAnyFlags(RF_Transient) || Mesh->GetOutermost() == GetTransientPackage())
		return TEXT("geometry snapshot requires a saved mesh");
	if (Mesh->GetOutermost()->IsDirty()) return TEXT("geometry snapshot refuses unsaved mesh edits");
	if (Mesh->IsCompiling()) return TEXT("geometry snapshot requires completed native compilation");
	const FMeshDescription* Description = Mesh->GetMeshDescription(0);
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (!Description || !Model || !Model->LODModels.IsValidIndex(0) || !RenderData || !RenderData->LODRenderData.IsValidIndex(0))
		return TEXT("geometry snapshot missing authoring/imported/render LOD0 evidence");
	const FSkeletalMeshLODModel& Imported = Model->LODModels[0];
	const FSkeletalMeshLODRenderData& Render = RenderData->LODRenderData[0];
	const auto& Positions = Render.StaticVertexBuffers.PositionVertexBuffer;
	const auto& Static = Render.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FSkinWeightVertexBuffer* Skin = Render.GetSkinWeightVertexBuffer();
	const int32 NumVertices = Positions.GetNumVertices();
	if (!NumVertices || Imported.MeshToImportVertexMap.Num() != NumVertices || Imported.NumVertices != static_cast<uint32>(NumVertices))
		return TEXT("geometry snapshot missing/incompatible native-to-source vertex map");
	if (!Positions.GetVertexData() || !Static.GetTangentData() || !Static.GetTexCoordData() || Static.GetNumVertices() != static_cast<uint32>(NumVertices)
		|| Static.GetNumTexCoords() != 1 || !Skin || Skin->GetNumVertices() != static_cast<uint32>(NumVertices)
		|| !Skin->GetDataVertexBuffer()->GetWeightData()
		|| !Render.MultiSizeIndexContainer.GetIndexBuffer())
		return TEXT("geometry snapshot requires resident CPU position/normal/UV/skin/index buffers and one UV channel");
	if (Render.MultiSizeIndexContainer.GetIndexBuffer()->GetResourceDataSize() <= 0
		|| (Skin->GetVariableBonesPerVertex() && !Skin->GetLookupVertexBuffer()->GetLookupData()))
		return TEXT("geometry snapshot index/variable-influence CPU data was discarded");
	// Bound a single response. This limit refuses evidence; it never truncates it into a pass.
	if (NumVertices > 1000000 || Description->VertexInstances().Num() > 3000000 || Mesh->GetMorphTargets().Num() > 4096)
		return TEXT("geometry snapshot exceeds per-asset verification limits");

	const FObject Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetNumberField(TEXT("tangentCaptureVersion"), 1);
	Root->SetNumberField(TEXT("lod"), 0);
	FValues Bones, Materials;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const FObject Row = MakeShared<FJsonObject>();
		const FTransform& Pose = Ref.GetRefBonePose()[I];
		Row->SetStringField(TEXT("name"), Ref.GetBoneName(I).ToString());
		Row->SetNumberField(TEXT("parent"), Ref.GetParentIndex(I));
		Row->SetArrayField(TEXT("position"), Vector3(Pose.GetTranslation()));
		Row->SetArrayField(TEXT("scale"), Vector3(Pose.GetScale3D()));
		const FQuat Q = Pose.GetRotation();
		Row->SetArrayField(TEXT("rotation"), {Number(Q.X), Number(Q.Y), Number(Q.Z), Number(Q.W)});
		Bones.Add(Object(Row));
	}
	for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("slot"), Material.MaterialSlotName.ToString());
		Row->SetStringField(TEXT("asset"), Material.MaterialInterface ? Material.MaterialInterface->GetOutermost()->GetName() : TEXT(""));
		Materials.Add(Object(Row));
	}
	Root->SetArrayField(TEXT("bones"), Bones);
	Root->SetArrayField(TEXT("materials"), Materials);

	const FSkeletalMeshConstAttributes Attributes(*Description);
	const auto AuthorPositions = Attributes.GetVertexPositions();
	const auto Normals = Attributes.GetVertexInstanceNormals();
	const auto Tangents = Attributes.GetVertexInstanceTangents();
	const auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	const auto UVs = Attributes.GetVertexInstanceUVs();
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Weights = Attributes.GetVertexSkinWeights();
	if (!AuthorPositions.IsValid() || !Normals.IsValid() || !Tangents.IsValid() || !BinormalSigns.IsValid()
		|| !UVs.IsValid() || UVs.GetNumChannels() != 1 || !Slots.IsValid() || !Weights.IsValid())
		return TEXT("geometry snapshot missing mesh-description attributes");
	FValues AuthorVertices, Instances, Groups, Triangles, AuthorMorphs;
	for (const FVertexID Id : Description->Vertices().GetElementIDs())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("id"), Id.GetValue());
		Row->SetArrayField(TEXT("position"), Vector3(AuthorPositions[Id]));
		FValues Influences;
		for (const UE::AnimationCore::FBoneWeight Weight : Weights.Get(Id))
			Influences.Add(Array({Number(Weight.GetBoneIndex()), Number(Weight.GetRawWeight())}));
		Row->SetArrayField(TEXT("influences"), Influences);
		AuthorVertices.Add(Object(Row));
	}
	for (const FVertexInstanceID Id : Description->VertexInstances().GetElementIDs())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("id"), Id.GetValue());
		Row->SetNumberField(TEXT("vertex"), Description->GetVertexInstanceVertex(Id).GetValue());
		Row->SetArrayField(TEXT("normal"), Vector3(Normals[Id]));
		Row->SetArrayField(TEXT("tangentX"), Vector3(Tangents[Id]));
		Row->SetField(TEXT("binormalSign"), Number(BinormalSigns[Id]));
		// MeshDescription stores X, Z and sign. Mark Y as reconstructed authoring
		// data; the built buffer's Y is read independently below.
		Row->SetArrayField(TEXT("tangentY"), Vector3(FVector3f::CrossProduct(Normals[Id], Tangents[Id]) * BinormalSigns[Id]));
		const FVector2f UV = UVs.Get(Id, 0);
		Row->SetArrayField(TEXT("uv"), {Number(UV.X), Number(UV.Y)});
		Instances.Add(Object(Row));
	}
	for (const FPolygonGroupID Id : Description->PolygonGroups().GetElementIDs())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("id"), Id.GetValue());
		Row->SetStringField(TEXT("slot"), Slots[Id].ToString());
		Groups.Add(Object(Row));
	}
	for (const FTriangleID Id : Description->Triangles().GetElementIDs())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("group"), Description->GetTrianglePolygonGroup(Id).GetValue());
		FValues Corners;
		for (const FVertexInstanceID Corner : Description->GetTriangleVertexInstances(Id)) Corners.Add(Number(Corner.GetValue()));
		Row->SetArrayField(TEXT("instances"), Corners);
		Triangles.Add(Object(Row));
	}
	for (const FName Name : Attributes.GetMorphTargetNames())
	{
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name.ToString());
		const auto PositionDeltas = Attributes.GetVertexMorphPositionDelta(Name);
		const auto NormalDeltas = Attributes.GetVertexInstanceMorphNormalDelta(Name);
		if (!PositionDeltas.IsValid()) return TEXT("geometry snapshot missing morph position attribute");
		Row->SetBoolField(TEXT("normalsPresent"), NormalDeltas.IsValid());
		FValues Deltas, NormalValues;
		for (const FVertexID Id : Description->Vertices().GetElementIDs())
		{
			const FVector3f D = PositionDeltas[Id];
			if (D != FVector3f::ZeroVector) Deltas.Add(Array(Delta(Id.GetValue(), D)));
		}
		if (NormalDeltas.IsValid())
			for (const FVertexInstanceID Id : Description->VertexInstances().GetElementIDs())
			{
				const FVector3f D = NormalDeltas[Id];
				if (D != FVector3f::ZeroVector) NormalValues.Add(Array(Delta(Id.GetValue(), D)));
			}
		Row->SetArrayField(TEXT("positions"), Deltas);
		Row->SetArrayField(TEXT("normals"), NormalValues);
		AuthorMorphs.Add(Object(Row));
	}
	const FObject Author = MakeShared<FJsonObject>();
	Author->SetBoolField(TEXT("tangentYDerived"), true);
	Author->SetArrayField(TEXT("vertices"), AuthorVertices);
	Author->SetArrayField(TEXT("instances"), Instances);
	Author->SetArrayField(TEXT("groups"), Groups);
	Author->SetArrayField(TEXT("triangles"), Triangles);
	Author->SetArrayField(TEXT("morphs"), AuthorMorphs);
	Root->SetObjectField(TEXT("authoring"), Author);

	FValues RenderVertices, RenderSections, RenderMorphs, RenderIndices;
	TArray<int32> SectionOfVertex;
	SectionOfVertex.Init(INDEX_NONE, NumVertices);
	for (int32 S = 0; S < Render.RenderSections.Num(); ++S)
	{
		const FSkelMeshRenderSection& Section = Render.RenderSections[S];
		if (static_cast<uint64>(Section.BaseVertexIndex) + Section.NumVertices > static_cast<uint64>(NumVertices))
			return TEXT("geometry snapshot render section vertex bounds");
		for (uint32 V = Section.BaseVertexIndex; V < Section.BaseVertexIndex + Section.NumVertices; ++V)
		{
			if (SectionOfVertex[V] != INDEX_NONE) return TEXT("geometry snapshot overlapping render sections");
			SectionOfVertex[V] = S;
		}
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("material"), Section.MaterialIndex);
		Row->SetNumberField(TEXT("baseVertex"), Section.BaseVertexIndex);
		Row->SetNumberField(TEXT("numVertices"), Section.NumVertices);
		Row->SetNumberField(TEXT("baseIndex"), Section.BaseIndex);
		Row->SetNumberField(TEXT("numTriangles"), Section.NumTriangles);
		Row->SetBoolField(TEXT("disabled"), Section.bDisabled);
		RenderSections.Add(Object(Row));
	}
	for (int32 V = 0; V < NumVertices; ++V)
	{
		if (SectionOfVertex[V] == INDEX_NONE) return TEXT("geometry snapshot unsectioned render vertex");
		const FSkelMeshRenderSection& Section = Render.RenderSections[SectionOfVertex[V]];
		const TConstArrayView<FBoneIndexType> BoneMap = Section.HasUnifiedBoneMap() ? Render.GetUnifiedBoneMap() : MakeArrayView(Section.BoneMap);
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("section"), SectionOfVertex[V]);
		// UE's map explicitly maps built vertices back to FVertexID in the saved description.
		Row->SetNumberField(TEXT("sourceVertex"), Imported.MeshToImportVertexMap[V]);
		Row->SetArrayField(TEXT("position"), Vector3(Positions.VertexPosition(V)));
		const FVector4f PackedNormal = Static.VertexTangentZ(V);
		Row->SetArrayField(TEXT("normal"), Vector3(PackedNormal));
		Row->SetArrayField(TEXT("tangentX"), Vector3(Static.VertexTangentX(V)));
		Row->SetArrayField(TEXT("tangentY"), Vector3(Static.VertexTangentY(V)));
		Row->SetField(TEXT("binormalSign"), Number(PackedNormal.W));
		const FVector2f UV = Static.GetVertexUV(V, 0);
		Row->SetArrayField(TEXT("uv"), {Number(UV.X), Number(UV.Y)});
		FValues Influences;
		for (uint32 I = 0; I < Skin->GetMaxBoneInfluences(); ++I)
		{
			const uint16 Weight = Skin->GetBoneWeight(V, I);
			if (!Weight) continue;
			const uint32 LocalBone = Skin->GetBoneIndex(V, I);
			if (LocalBone >= static_cast<uint32>(BoneMap.Num()) || BoneMap[LocalBone] >= Ref.GetNum())
				return TEXT("geometry snapshot invalid section-local bone map");
			Influences.Add(Array({Number(BoneMap[LocalBone]), Number(Weight)}));
		}
		Row->SetArrayField(TEXT("influences"), Influences);
		RenderVertices.Add(Object(Row));
	}
	TArray<uint32> Indices;
	Render.MultiSizeIndexContainer.GetIndexBuffer(Indices);
	for (uint32 Index : Indices) RenderIndices.Add(Number(Index));
	for (const UMorphTarget* Morph : Mesh->GetMorphTargets())
	{
		if (!Morph || !Morph->GetMorphLODModels().IsValidIndex(0)) return TEXT("geometry snapshot missing native morph LOD0");
		const FMorphTargetLODModel& Lod = Morph->GetMorphLODModels()[0];
		const FObject Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Morph->GetName());
		Row->SetNumberField(TEXT("baseVertices"), Lod.NumBaseMeshVerts);
		FValues Deltas, SectionIndices;
		for (const FMorphTargetDelta& D : Lod.Vertices)
			Deltas.Add(Array({Number(D.SourceIdx), Number(D.PositionDelta.X), Number(D.PositionDelta.Y), Number(D.PositionDelta.Z),
				Number(D.TangentZDelta.X), Number(D.TangentZDelta.Y), Number(D.TangentZDelta.Z)}));
		for (int32 Index : Lod.SectionIndices) SectionIndices.Add(Number(Index));
		Row->SetArrayField(TEXT("deltas"), Deltas);
		Row->SetArrayField(TEXT("sections"), SectionIndices);
		RenderMorphs.Add(Object(Row));
	}
	const FObject Built = MakeShared<FJsonObject>();
	Built->SetNumberField(TEXT("weightBits"), Skin->Use16BitBoneWeight() ? 16 : 8);
	Built->SetNumberField(TEXT("normalBits"), Static.GetUseHighPrecisionTangentBasis() ? 16 : 8);
	Built->SetBoolField(TEXT("fullPrecisionUVs"), Static.GetUseFullPrecisionUVs());
	Built->SetNumberField(TEXT("uvChannels"), Static.GetNumTexCoords());
	Built->SetArrayField(TEXT("vertices"), RenderVertices);
	Built->SetArrayField(TEXT("sections"), RenderSections);
	Built->SetArrayField(TEXT("indices"), RenderIndices);
	Built->SetArrayField(TEXT("morphs"), RenderMorphs);
	Root->SetObjectField(TEXT("render"), Built);
	if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson)))
	{
		OutJson.Reset();
		return TEXT("geometry snapshot JSON serialization failed");
	}
	return FString();
#else
	return TEXT("geometry snapshot is editor only");
#endif
}
