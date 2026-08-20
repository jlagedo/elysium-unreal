#include "ElysiumSkeletalSource.h"

#include "Misc/FileHelper.h"

namespace
{
	constexpr uint32 EskmMagic = 'M' << 24 | 'K' << 16 | 'S' << 8 | 'E';   // "ESKM", little-endian
	// 4 -- a clip names the clip it is a difference FROM, empty for a pose of its own. An additive
	// ships once per declaring host, composed onto that host's pose, because converting VtMB's
	// post-multiplied delta into Unreal's pre-multiplied one is a conjugation by the base's
	// rotation and the answer differs across the hosts one delta serves.
	//
	// 3 -- a clip carries the index of its per-bone `weight`@0 mask, and the masks ship as a
	// de-duplicated table. Without it a bone a layer leaves at its bind pose is indistinguishable
	// from one the layer does not own, which are opposite results when the layer is composed.
	//
	// 2 -- a clip's rotations for the split bone are written pre-corrected by the exporter, so
	// ordinary inheritance reproduces the pose VtMB draws and no runtime rule is applied. A
	// version 1 container carries the same bytes meaning the opposite, and nothing in the payload
	// tells them apart, so a stale export is refused rather than posed wrongly with no error.
	// Version 5 widened the "MESH" vertex record with the authored shading normal. Version 6 made
	// every owned bone a complete donor-local pose. Version 7 also makes the host side of an
	// additive subtraction carry every bone the additive owns, including bind-only channels.
	constexpr uint32 EskmVersion = 7;

	/**
	 * A bounds-checked forward cursor over the loaded file.
	 *
	 * Every read goes through Take(), so a truncated or malformed file trips the failure flag
	 * on the first read past the end and every subsequent read is a no-op returning zeroes.
	 * The caller checks once, at the end, instead of after every field.
	 */
	class FCursor
	{
	public:
		FCursor(const uint8* InData, int64 InSize) : Data(InData), Size(InSize) {}

		bool IsValid() const { return bValid; }
		int64 Tell() const { return Offset; }
		void Seek(int64 InOffset) { Offset = InOffset; bValid &= (InOffset >= 0 && InOffset <= Size); }

		const uint8* Take(int64 Count)
		{
			if (!bValid || Count < 0 || Offset + Count > Size)
			{
				bValid = false;
				return nullptr;
			}
			const uint8* At = Data + Offset;
			Offset += Count;
			return At;
		}

		template <typename T>
		T Read()
		{
			const uint8* At = Take(sizeof(T));
			T Value{};
			if (At != nullptr)
			{
				FMemory::Memcpy(&Value, At, sizeof(T));
			}
			return Value;
		}

		FString ReadString()
		{
			const int32 Length = static_cast<int32>(Read<uint32>());
			const uint8* At = Take(Length);
			if (At == nullptr)
			{
				return FString();
			}
			return FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(At), Length));
		}
	private:
		const uint8* Data = nullptr;
		int64 Size = 0;
		int64 Offset = 0;
		bool bValid = true;
	};

	void ReadSkeleton(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Bones.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceBone& Bone = Out.Bones.AddDefaulted_GetRef();
			Bone.Name = FName(*Cursor.ReadString());
			Bone.Parent = Cursor.Read<int32>();
			FVector3f Translation;
			Translation.X = Cursor.Read<float>();
			Translation.Y = Cursor.Read<float>();
			Translation.Z = Cursor.Read<float>();
			FQuat4f Rotation;
			Rotation.X = Cursor.Read<float>();
			Rotation.Y = Cursor.Read<float>();
			Rotation.Z = Cursor.Read<float>();
			Rotation.W = Cursor.Read<float>();
			Rotation.Normalize();
			Bone.Local = FTransform(FQuat(Rotation), FVector(Translation));
		}
	}

	void ReadAttachments(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Attachments.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceAttachment& Attachment = Out.Attachments.AddDefaulted_GetRef();
			Attachment.Name = FName(*Cursor.ReadString());
			Attachment.Bone = static_cast<int32>(Cursor.Read<uint32>());
			FVector3f Translation;
			Translation.X = Cursor.Read<float>();
			Translation.Y = Cursor.Read<float>();
			Translation.Z = Cursor.Read<float>();
			FQuat4f Rotation;
			Rotation.X = Cursor.Read<float>();
			Rotation.Y = Cursor.Read<float>();
			Rotation.Z = Cursor.Read<float>();
			Rotation.W = Cursor.Read<float>();
			Rotation.Normalize();
			Attachment.Local = FTransform(FQuat(Rotation), FVector(Translation));
		}
	}

	void ReadMaterials(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Materials.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceMaterial& Material = Out.Materials.AddDefaulted_GetRef();
			Material.Name = Cursor.ReadString();
			Material.Albedo = Cursor.ReadString();
		}
	}

	void ReadMesh(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 VertexCount = static_cast<int32>(Cursor.Read<uint32>());
		const int32 TriangleCount = static_cast<int32>(Cursor.Read<uint32>());
		const int32 SectionCount = static_cast<int32>(Cursor.Read<uint32>());

		Out.Sections.Reserve(FMath::Max(SectionCount, 0));
		for (int32 Index = 0; Index < SectionCount && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceSection& Section = Out.Sections.AddDefaulted_GetRef();
			Section.Material = Cursor.ReadString();
			Section.FirstTriangle = static_cast<int32>(Cursor.Read<uint32>());
			Section.TriangleCount = static_cast<int32>(Cursor.Read<uint32>());
		}

		Out.Vertices.Reserve(FMath::Max(VertexCount, 0));
		for (int32 Index = 0; Index < VertexCount && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceVertex& Vertex = Out.Vertices.AddDefaulted_GetRef();
			Vertex.Position.X = Cursor.Read<float>();
			Vertex.Position.Y = Cursor.Read<float>();
			Vertex.Position.Z = Cursor.Read<float>();
			Vertex.Normal.X = Cursor.Read<float>();
			Vertex.Normal.Y = Cursor.Read<float>();
			Vertex.Normal.Z = Cursor.Read<float>();
			Vertex.UV.X = Cursor.Read<float>();
			Vertex.UV.Y = Cursor.Read<float>();
			for (uint16& Bone : Vertex.Bones)
			{
				Bone = Cursor.Read<uint16>();
			}
			for (float& Weight : Vertex.Weights)
			{
				Weight = Cursor.Read<float>();
			}
		}

		Out.Indices.Reserve(FMath::Max(TriangleCount, 0) * 3);
		for (int32 Index = 0; Index < TriangleCount * 3 && Cursor.IsValid(); ++Index)
		{
			Out.Indices.Add(Cursor.Read<uint32>());
		}
	}

	void ReadMorphs(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Morphs.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceMorph& Morph = Out.Morphs.AddDefaulted_GetRef();
			Morph.Name = Cursor.ReadString();
			const int32 DeltaCount = static_cast<int32>(Cursor.Read<uint32>());
			Morph.Deltas.Reserve(FMath::Max(DeltaCount, 0));
			for (int32 Delta = 0; Delta < DeltaCount && Cursor.IsValid(); ++Delta)
			{
				FElysiumSourceMorphDelta& Entry = Morph.Deltas.AddDefaulted_GetRef();
				Entry.Vertex = Cursor.Read<uint32>();
				Entry.Position.X = Cursor.Read<float>();
				Entry.Position.Y = Cursor.Read<float>();
				Entry.Position.Z = Cursor.Read<float>();
				Entry.Normal.X = Cursor.Read<float>();
				Entry.Normal.Y = Cursor.Read<float>();
				Entry.Normal.Z = Cursor.Read<float>();
			}
		}
	}

	void ReadMasks(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Masks.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceMask& Mask = Out.Masks.AddDefaulted_GetRef();
			const int32 BoneCount = static_cast<int32>(Cursor.Read<uint32>());
			if (const uint8* At = Cursor.Take(BoneCount))
			{
				Mask.Bones.Append(At, BoneCount);
			}
		}
	}

	void ReadClips(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.Clips.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceClip& Clip = Out.Clips.AddDefaulted_GetRef();
			Clip.Name = Cursor.ReadString();
			Clip.BaseName = Cursor.ReadString();
			Clip.FrameCount = static_cast<int32>(Cursor.Read<uint32>());
			Clip.FrameRate = Cursor.Read<float>();
			Clip.Flags = Cursor.Read<uint32>();
			Clip.Mask = Cursor.Read<int32>();
			const int32 TrackCount = static_cast<int32>(Cursor.Read<uint32>());
			Clip.Tracks.Reserve(FMath::Max(TrackCount, 0));
			for (int32 Track = 0; Track < TrackCount && Cursor.IsValid(); ++Track)
			{
				FElysiumSourceTrack& Entry = Clip.Tracks.AddDefaulted_GetRef();
				Entry.Bone = static_cast<int32>(Cursor.Read<uint32>());
				const bool bHasTranslation = Cursor.Read<uint8>() != 0;
				const bool bHasRotation = Cursor.Read<uint8>() != 0;
				if (bHasTranslation)
				{
					Entry.Translations.Reserve(Clip.FrameCount);
					for (int32 Frame = 0; Frame < Clip.FrameCount && Cursor.IsValid(); ++Frame)
					{
						FVector3f& Value = Entry.Translations.AddDefaulted_GetRef();
						Value.X = Cursor.Read<float>();
						Value.Y = Cursor.Read<float>();
						Value.Z = Cursor.Read<float>();
					}
				}
				if (bHasRotation)
				{
					Entry.Rotations.Reserve(Clip.FrameCount);
					for (int32 Frame = 0; Frame < Clip.FrameCount && Cursor.IsValid(); ++Frame)
					{
						FQuat4f& Value = Entry.Rotations.AddDefaulted_GetRef();
						Value.X = Cursor.Read<float>();
						Value.Y = Cursor.Read<float>();
						Value.Z = Cursor.Read<float>();
						Value.W = Cursor.Read<float>();
						Value.Normalize();
					}
				}
			}
		}
	}

	void ReadHairDynamics(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.HairDynamics.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceHairDynamicsChain& Chain = Out.HairDynamics.AddDefaulted_GetRef();
			Chain.BoundBone = FName(*Cursor.ReadString());
			Chain.ChainEnd = FName(*Cursor.ReadString());
			Chain.GravityScale = Cursor.Read<float>();
			Chain.Damping = Cursor.Read<float>();
			Chain.AngularSpring = Cursor.Read<float>();
			Chain.ConeAngleDegrees = Cursor.Read<float>();
		}
	}

	void ReadBreastDynamics(FCursor& Cursor, FElysiumSkeletalSource& Out)
	{
		const int32 Count = static_cast<int32>(Cursor.Read<uint32>());
		Out.BreastDynamics.Reserve(FMath::Max(Count, 0));
		for (int32 Index = 0; Index < Count && Cursor.IsValid(); ++Index)
		{
			FElysiumSourceAnimDynamicsBody& Body = Out.BreastDynamics.AddDefaulted_GetRef();
			Body.BoundBone = FName(*Cursor.ReadString());
			Body.GravityScale = Cursor.Read<float>();
			Body.Damping = Cursor.Read<float>();
			Body.AngularSpring = Cursor.Read<float>();
			Body.ConeAngleDegrees = Cursor.Read<float>();
		}
	}
}

namespace
{
	// Both entry points below. `bBonesOnly` skips every section but SKEL, which is what makes
	// seeding a rig family's skeleton from all of its members affordable: a bank container is up
	// to 30 MB and almost all of it is clip payload the bone tree does not need. The trailing
	// validation still runs -- its mask and clip loops are simply empty on this path.
	bool LoadContainer(const FString& Path, FElysiumSkeletalSource& Out, FString& OutError,
		bool bBonesOnly)
	{
	TArray<uint8> Blob;
	if (!FFileHelper::LoadFileToArray(Blob, *Path))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *Path);
		return false;
	}

	FCursor Cursor(Blob.GetData(), Blob.Num());
	if (Cursor.Read<uint32>() != EskmMagic)
	{
		OutError = FString::Printf(TEXT("%s is not an .eskm container"), *Path);
		return false;
	}
	const uint32 Version = Cursor.Read<uint32>();
	if (Version != EskmVersion)
	{
		OutError = FString::Printf(TEXT("%s is version %u, expected %u -- re-export"),
			*Path, Version, EskmVersion);
		return false;
	}
	const int32 SectionCount = static_cast<int32>(Cursor.Read<uint32>());
	Cursor.Read<uint32>();   // reserved

	// The directory is read whole before any payload, so a section can be walked with its own
	// cursor and an unknown tag simply never gets one.
	struct FEntry { uint32 Tag; int64 Offset; int64 Size; };
	TArray<FEntry> Directory;
	Directory.Reserve(FMath::Max(SectionCount, 0));
	for (int32 Index = 0; Index < SectionCount && Cursor.IsValid(); ++Index)
	{
		FEntry& Entry = Directory.AddDefaulted_GetRef();
		Entry.Tag = Cursor.Read<uint32>();
		Entry.Offset = static_cast<int64>(Cursor.Read<uint64>());
		Entry.Size = static_cast<int64>(Cursor.Read<uint64>());
	}
	if (!Cursor.IsValid())
	{
		OutError = FString::Printf(TEXT("%s has a truncated section directory"), *Path);
		return false;
	}

	constexpr uint32 TagSkel = 'L' << 24 | 'E' << 16 | 'K' << 8 | 'S';
	constexpr uint32 TagAtch = 'H' << 24 | 'C' << 16 | 'T' << 8 | 'A';
	constexpr uint32 TagMatl = 'L' << 24 | 'T' << 16 | 'A' << 8 | 'M';
	constexpr uint32 TagMesh = 'H' << 24 | 'S' << 16 | 'E' << 8 | 'M';
	constexpr uint32 TagMorf = 'F' << 24 | 'R' << 16 | 'O' << 8 | 'M';
	constexpr uint32 TagMask = 'K' << 24 | 'S' << 16 | 'A' << 8 | 'M';
	constexpr uint32 TagAnim = 'M' << 24 | 'I' << 16 | 'N' << 8 | 'A';
	constexpr uint32 TagDynm = 'M' << 24 | 'N' << 16 | 'Y' << 8 | 'D';
	constexpr uint32 TagBdyn = 'N' << 24 | 'Y' << 16 | 'D' << 8 | 'B';

	for (const FEntry& Entry : Directory)
	{
		if (Entry.Offset < 0 || Entry.Size < 0 || Entry.Offset + Entry.Size > Blob.Num())
		{
			OutError = FString::Printf(TEXT("%s has a section running past the end of the file"),
				*Path);
			return false;
		}
		if (bBonesOnly && Entry.Tag != TagSkel)
		{
			continue;
		}
		FCursor Section(Blob.GetData() + Entry.Offset, Entry.Size);
		switch (Entry.Tag)
		{
		case TagSkel: ReadSkeleton(Section, Out); break;
		case TagAtch: ReadAttachments(Section, Out); break;
		case TagMatl: ReadMaterials(Section, Out); break;
		case TagMesh: ReadMesh(Section, Out); break;
		case TagMorf: ReadMorphs(Section, Out); break;
		case TagMask: ReadMasks(Section, Out); break;
		case TagAnim: ReadClips(Section, Out); break;
		case TagDynm: ReadHairDynamics(Section, Out); break;
		case TagBdyn: ReadBreastDynamics(Section, Out); break;
		default: continue;
		}
		if (!Section.IsValid())
		{
			OutError = FString::Printf(TEXT("%s has a truncated section"), *Path);
			return false;
		}
	}

	if (Out.Bones.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s carries no skeleton"), *Path);
		return false;
	}
	for (int32 Index = 0; Index < Out.Bones.Num(); ++Index)
	{
		// A parent declared after its child cannot be composed, and FReferenceSkeleton rejects
		// it outright -- catch it here where the file can be named.
		if (Out.Bones[Index].Parent >= Index)
		{
			OutError = FString::Printf(TEXT("%s: bone %s parents forward to %d"),
				*Path, *Out.Bones[Index].Name.ToString(), Out.Bones[Index].Parent);
			return false;
		}
	}
	for (int32 Index = 0; Index < Out.Attachments.Num(); ++Index)
	{
		const FElysiumSourceAttachment& Attachment = Out.Attachments[Index];
		if (Attachment.Name.IsNone() || !Out.Bones.IsValidIndex(Attachment.Bone)
			|| !Attachment.Local.IsValid())
		{
			OutError = FString::Printf(TEXT("%s: attachment %d ('%s') has invalid bone %d or transform"),
				*Path, Index, *Attachment.Name.ToString(), Attachment.Bone);
			return false;
		}
	}
	// A mask is read per bone by index, so a short one would silently leave the tail of the
	// skeleton outside every mask -- which is a pose, not an error, unless it is caught here.
	for (int32 Index = 0; Index < Out.Masks.Num(); ++Index)
	{
		if (Out.Masks[Index].Bones.Num() != Out.Bones.Num())
		{
			OutError = FString::Printf(TEXT("%s: mask %d covers %d of %d bones"),
				*Path, Index, Out.Masks[Index].Bones.Num(), Out.Bones.Num());
			return false;
		}
	}
	for (const FElysiumSourceClip& Clip : Out.Clips)
	{
		if (Clip.Mask != INDEX_NONE && !Out.Masks.IsValidIndex(Clip.Mask))
		{
			OutError = FString::Printf(TEXT("%s: clip %s names mask %d of %d"),
				*Path, *Clip.Name, Clip.Mask, Out.Masks.Num());
			return false;
		}
	}
	return true;
	}
}

bool FElysiumSkeletalSource::Load(const FString& Path, FElysiumSkeletalSource& Out, FString& OutError)
{
	return LoadContainer(Path, Out, OutError, /*bBonesOnly=*/false);
}

bool FElysiumSkeletalSource::LoadBones(const FString& Path, FElysiumSkeletalSource& Out,
	FString& OutError)
{
	return LoadContainer(Path, Out, OutError, /*bBonesOnly=*/true);
}
