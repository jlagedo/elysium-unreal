#include "Visual/ElysiumEyeRig.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FVector ReadVector(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || Arr == nullptr || Arr->Num() < 3)
		{
			return FVector::ZeroVector;
		}
		return FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
	}

	void ReadIntTriple(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32(&Out)[3])
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || Arr == nullptr)
		{
			return;
		}
		for (int32 i = 0; i < 3 && i < Arr->Num(); ++i)
		{
			Out[i] = static_cast<int32>((*Arr)[i]->AsNumber());
		}
	}

	void ReadFloatTriple(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float(&Out)[3])
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || Arr == nullptr)
		{
			return;
		}
		for (int32 i = 0; i < 3 && i < Arr->Num(); ++i)
		{
			Out[i] = static_cast<float>((*Arr)[i]->AsNumber());
		}
	}
}

bool FElysiumEyeSet::Load(const FString& RelPath, FString& OutError)
{
	OutError.Reset();
	const FString Path = FElysiumContentPaths::NpcEyes(RelPath);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	if (!LoadJsonText(Text, OutError))
	{
		return false;
	}
	ApplyAssetImport();
	return true;
}

bool FElysiumEyeSet::LoadJsonText(const FString& JsonText, FString& OutError)
{
	OutError.Reset();
	Eyeballs.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed eye sidecar JSON");
		return false;
	}
	Root->TryGetStringField(TEXT("stem"), Stem);

	const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
	if (!Root->TryGetArrayField(TEXT("eyeballs"), Records) || Records == nullptr)
	{
		OutError = TEXT("eye sidecar carries no `eyeballs` array");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Records)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || Obj == nullptr)
		{
			continue;
		}
		FElysiumEyeball E;
		(*Obj)->TryGetNumberField(TEXT("index"), E.Index);
		FString BoneName;
		(*Obj)->TryGetStringField(TEXT("bone"), BoneName);
		E.Bone = FName(*BoneName);
		(*Obj)->TryGetNumberField(TEXT("bone_index"), E.BoneIndex);
		E.Org = ReadVector(*Obj, TEXT("org"));
		E.Up = ReadVector(*Obj, TEXT("up"));
		E.Forward = ReadVector(*Obj, TEXT("forward"));
		double Scratch = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("zoffset"), Scratch)) { E.ZOffset = static_cast<float>(Scratch); }
		if ((*Obj)->TryGetNumberField(TEXT("radius"), Scratch)) { E.Radius = static_cast<float>(Scratch); }
		if ((*Obj)->TryGetNumberField(TEXT("iris_scale"), Scratch)) { E.IrisScale = static_cast<float>(Scratch); }
		ReadIntTriple(*Obj, TEXT("upperflexdesc"), E.UpperFlexDesc);
		ReadIntTriple(*Obj, TEXT("lowerflexdesc"), E.LowerFlexDesc);
		ReadFloatTriple(*Obj, TEXT("uppertarget"), E.UpperTarget);
		ReadFloatTriple(*Obj, TEXT("lowertarget"), E.LowerTarget);
		int32 Lid = INDEX_NONE;
		if ((*Obj)->TryGetNumberField(TEXT("upperlidflexdesc"), Lid)) { E.UpperLidFlexDesc = Lid; }
		if ((*Obj)->TryGetNumberField(TEXT("lowerlidflexdesc"), Lid)) { E.LowerLidFlexDesc = Lid; }
		(*Obj)->TryGetStringField(TEXT("material"), E.Material);
		(*Obj)->TryGetStringField(TEXT("iris"), E.IrisTexture);
		(*Obj)->TryGetBoolField(TEXT("vampire"), E.bVampire);
		Eyeballs.Add(MoveTemp(E));
	}
	if (Eyeballs.IsEmpty())
	{
		OutError = TEXT("eye sidecar parsed with no usable record");
		return false;
	}
	return true;
}

void FElysiumEyeSet::ApplyAssetImport()
{
	// The same door the procedural rule table goes through: the sidecar states its geometry in the
	// glb's own basis and metres, and glTFRuntime imports that file under a declared basis and
	// scale. Positions take both; directions take the basis alone.
	for (FElysiumEyeball& E : Eyeballs)
	{
		const FTransform Imported = ElysiumNpcVisual::ImportGlbLocal(
			FTransform(FQuat::Identity, E.Org, FVector::OneVector));
		E.Org = Imported.GetLocation();
		E.Up = ElysiumNpcVisual::ImportGlbDirection(E.Up).GetSafeNormal();
		E.Forward = ElysiumNpcVisual::ImportGlbDirection(E.Forward).GetSafeNormal();
	}
}

const FElysiumEyeball* FElysiumEyeSet::Find(int32 Index) const
{
	return Eyeballs.FindByPredicate([Index](const FElysiumEyeball& E) { return E.Index == Index; });
}

const FElysiumEyeball* FElysiumEyeSet::FindByMaterial(const FString& MaterialName) const
{
	return Eyeballs.FindByPredicate([&MaterialName](const FElysiumEyeball& E)
		{ return !E.Material.IsEmpty() && E.Material.Equals(MaterialName, ESearchCase::IgnoreCase); });
}

FString ElysiumEyes::MaterialNameFromSlot(const FName SlotName)
{
	static const FString Marker = TEXT("_Section_");
	const FString Slot = SlotName.ToString();
	const int32 SectionAt = Slot.Find(Marker);
	if (SectionAt == INDEX_NONE)
	{
		return Slot;
	}
	int32 Cursor = SectionAt + Marker.Len();
	while (Cursor < Slot.Len() && FChar::IsDigit(Slot[Cursor]))
	{
		++Cursor;
	}
	if (Cursor < Slot.Len() && Slot[Cursor] == TEXT('_'))
	{
		++Cursor;
	}
	return Slot.Mid(Cursor);
}

float ElysiumEyes::BlinkWeight(float SecondsRemaining)
{
	const float A = SecondsRemaining * ElysiumEyes::BlinkRate;
	if (A <= 0.f)
	{
		return 0.f;
	}
	const float C = FMath::Cos(A);
	if (C <= 0.f)
	{
		return 0.f;
	}
	const float W = 2.f * FMath::Sqrt(C);
	return W > 1.f ? 2.f - W : W;
}

void ElysiumEyes::BuildState(const FElysiumEyeball& Eye, const FTransform& BoneToSpace,
	const FVector& Target, const FElysiumEyeTuning& Tuning, FElysiumEyeState& Out)
{
	Out = FElysiumEyeState();

	const FQuat Rot = BoneToSpace.GetRotation();

	// The shift is applied by the sign of each component, so a mirrored pair moves apart rather
	// than both moving the same way. The shading origin deliberately does not take it.
	FVector Shifted = Eye.Org;
	Shifted.X += FMath::Sign(Shifted.X) * Tuning.EyeShift.X;
	Shifted.Y += FMath::Sign(Shifted.Y) * Tuning.EyeShift.Y;
	Shifted.Z += FMath::Sign(Shifted.Z) * Tuning.EyeShift.Z;
	Out.Org = BoneToSpace.TransformPosition(Shifted);
	Out.NormalOrg = BoneToSpace.TransformPosition(Eye.Org);
	Out.AuthoredUp = Rot.RotateVector(Eye.Up).GetSafeNormal();

	FVector Up = Out.AuthoredUp;
	// Zeroed, and the zero is load-bearing: the `bEyeMove` branch below may not assign it, and the
	// fallback to the authored resting aim is selected by testing it. `FVector`'s default constructor
	// does not initialize, so leaving it bare reads a garbage aim on exactly the path that has no
	// gaze — which still draws a round iris and still moves a lid.
	FVector Forward = FVector::ZeroVector;
	if (Tuning.bEyeMove)
	{
		Forward = (Target - Out.Org).GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		// The authored resting aim, and note the negation: the record stores it negated, so a
		// dropped sign points the eye backwards. This is also the path a body with no gaze takes.
		Forward = (-Rot.RotateVector(Eye.Forward)).GetSafeNormal();
	}
	if (Up.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return;
	}

	FVector Right = FVector::CrossProduct(Forward, Up).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		return;
	}
	// The sideways nudge the record authors, then re-orthonormalize around it.
	Forward = (Forward + Right * (2.f * Eye.ZOffset)).GetSafeNormal();
	Right = FVector::CrossProduct(Forward, Up).GetSafeNormal();
	Up = FVector::CrossProduct(Right, Forward).GetSafeNormal();

	Out.Forward = Forward;
	Out.Right = Right;
	Out.Up = Up;

	// `s` is an inverse length in eyeball units, so it converts to the geometry's centimetres by
	// DIVISION. Getting that backwards leaves a perfectly round iris of the wrong size on the whole
	// cast, which reads as an art problem rather than an arithmetic one.
	float Scale = 1.f / (Eye.IrisScale != 0.f ? Eye.IrisScale : 1.f) + Tuning.EyeSize;
	if (Scale > 0.f)
	{
		Scale = 1.f / Scale;
	}
	Scale = -Scale / ElysiumEyes::UnitsToCm;
	Out.IrisU = Right * Scale;
	Out.IrisV = Up * Scale;

	Out.UpLocal = Rot.UnrotateVector(Up);
	Out.ForwardLocal = Rot.UnrotateVector(Forward);
	Out.bValid = true;
}
