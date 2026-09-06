#include "Visual/ElysiumCompositionRig.h"

#include "Visual/ElysiumNpcVisual.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Numbers) || Numbers == nullptr
			|| Numbers->Num() < 3)
		{
			return false;
		}
		Out = FVector((*Numbers)[0]->AsNumber(), (*Numbers)[1]->AsNumber(), (*Numbers)[2]->AsNumber());
		return true;
	}

	// The sidecar writes a quaternion as the glTF component order the glb itself uses: x, y, z, w.
	bool ReadQuat(const TSharedPtr<FJsonValue>& Value, FQuat& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Numbers) || Numbers == nullptr
			|| Numbers->Num() < 4)
		{
			return false;
		}
		Out = FQuat((*Numbers)[0]->AsNumber(), (*Numbers)[1]->AsNumber(),
			(*Numbers)[2]->AsNumber(), (*Numbers)[3]->AsNumber());
		return true;
	}
}

const FElysiumAxisInterpRule* FElysiumCompositionRig::FindRule(const FName Bone) const
{
	for (const FElysiumAxisInterpRule& Rule : AxisRules)
	{
		if (Rule.Bone == Bone)
		{
			return &Rule;
		}
	}
	return nullptr;
}

// Load.

bool FElysiumCompositionRig::LoadAxisRulesJson(const FString& JsonText, FString& OutError)
{
	AxisRules.Reset();
	DriverAxes[0] = FVector(1.f, 0.f, 0.f);
	DriverAxes[1] = FVector(0.f, 1.f, 0.f);
	DriverAxes[2] = FVector(0.f, 0.f, 1.f);

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed procedural JSON");
		return false;
	}
	FString FileStem;
	if (Root->TryGetStringField(TEXT("stem"), FileStem) && Stem.IsEmpty())
	{
		Stem = FileStem;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Root->TryGetArrayField(TEXT("driver_axes"), Values) && Values != nullptr)
	{
		if (Values->Num() != 3)
		{
			OutError = FString::Printf(TEXT("driver_axes carries %d entries, not 3"), Values->Num());
			return false;
		}
		for (int32 i = 0; i < 3; ++i)
		{
			if (!ReadVector((*Values)[i], DriverAxes[i]))
			{
				OutError = FString::Printf(TEXT("driver_axes[%d] is not a 3-vector"), i);
				return false;
			}
		}
	}

	if (!Root->TryGetArrayField(TEXT("rules"), Values) || Values == nullptr)
	{
		OutError = TEXT("no `rules` array");
		return false;
	}

	AxisRules.Reserve(Values->Num());
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || Obj == nullptr)
		{
			continue;
		}
		FElysiumAxisInterpRule Rule;
		FString Name;
		if ((*Obj)->TryGetStringField(TEXT("bone"), Name)) { Rule.Bone = FName(*Name); }
		if ((*Obj)->TryGetStringField(TEXT("control"), Name)) { Rule.Control = FName(*Name); }
		(*Obj)->TryGetNumberField(TEXT("bone_index"), Rule.BoneIndex);
		(*Obj)->TryGetNumberField(TEXT("control_index"), Rule.ControlIndex);

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		bool bTable = Rule.Bone != NAME_None && Rule.Control != NAME_None
			&& (*Obj)->TryGetArrayField(TEXT("axis"), Entries) && Entries != nullptr
			&& Entries->Num() >= 3;
		if (bTable)
		{
			Rule.Axis = FVector((*Entries)[0]->AsNumber(), (*Entries)[1]->AsNumber(),
				(*Entries)[2]->AsNumber());
		}
		if (bTable && (*Obj)->TryGetArrayField(TEXT("pos"), Entries) && Entries != nullptr
			&& Entries->Num() == 6)
		{
			for (int32 i = 0; i < 6 && bTable; ++i) { bTable = ReadVector((*Entries)[i], Rule.Pos[i]); }
		}
		else
		{
			bTable = false;
		}
		if (bTable && (*Obj)->TryGetArrayField(TEXT("quat"), Entries) && Entries != nullptr
			&& Entries->Num() == 6)
		{
			for (int32 i = 0; i < 6 && bTable; ++i) { bTable = ReadQuat((*Entries)[i], Rule.Quat[i]); }
		}
		else
		{
			bTable = false;
		}
		if (!bTable)
		{
			// A malformed rule is dropped rather than half-applied: an incomplete six-entry table
			// would silently pin a limb somewhere the model never authored.
			OutError = FString::Printf(TEXT("rule '%s' is incomplete"),
				Rule.Bone == NAME_None ? TEXT("<unnamed>") : *Rule.Bone.ToString());
			return false;
		}
		AxisRules.Add(MoveTemp(Rule));
	}
	return true;
}

// Evaluation.

FTransform FElysiumCompositionRig::EvaluateRule(const FElysiumAxisInterpRule& Rule,
	const FQuat& ControlLocalRotation) const
{
	const FVector Driver = ControlLocalRotation.RotateVector(Rule.Axis);

	// The sign of each term's weight names which of the pair it uses; the magnitude weights it.
	int32 Picked[3];
	double Weights[3];
	for (int32 k = 0; k < 3; ++k)
	{
		const double Signed = FVector::DotProduct(DriverAxes[k], Driver);
		Picked[k] = Signed >= 0.0 ? 2 * k : 2 * k + 1;
		Weights[k] = FMath::Abs(Signed);
	}

	const double A1 = Weights[0];
	const double A2 = Weights[1];
	const double A3 = Weights[2];
	if (A1 + A2 > 0.0)
	{
		const double Scale = 1.0 / (A1 + A2 + A3);
		const FQuat Pair = FQuat::Slerp(Rule.Quat[Picked[1]], Rule.Quat[Picked[0]],
			static_cast<float>(A1 / (A1 + A2)));
		const FQuat Rotation = FQuat::Slerp(Pair, Rule.Quat[Picked[2]],
			static_cast<float>(A3 * Scale));
		const FVector Translation = A1 * Scale * Rule.Pos[Picked[0]]
			+ A2 * Scale * Rule.Pos[Picked[1]]
			+ A3 * Scale * Rule.Pos[Picked[2]];
		return FTransform(Rotation, Translation);
	}
	return FTransform(Rule.Quat[Picked[2]], Rule.Pos[Picked[2]]);
}
