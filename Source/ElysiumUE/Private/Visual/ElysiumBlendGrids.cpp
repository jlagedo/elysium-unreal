#include "Visual/ElysiumBlendGrids.h"

#include "ElysiumContentPaths.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// File-prefixed for the same reason the cloth reader's helpers are: the module builds non-unity
	// adaptively, so this translation unit is regularly concatenated with its neighbours.
	void ReadBlendPair(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32(&Out)[2])
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (Object->TryGetArrayField(Field, Numbers) && Numbers != nullptr && Numbers->Num() >= 2)
		{
			Out[0] = static_cast<int32>((*Numbers)[0]->AsNumber());
			Out[1] = static_cast<int32>((*Numbers)[1]->AsNumber());
		}
	}

	void ReadBlendPair(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float(&Out)[2])
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (Object->TryGetArrayField(Field, Numbers) && Numbers != nullptr && Numbers->Num() >= 2)
		{
			Out[0] = static_cast<float>((*Numbers)[0]->AsNumber());
			Out[1] = static_cast<float>((*Numbers)[1]->AsNumber());
		}
	}
}

const FElysiumPoseParams& FElysiumPoseParams::Neutral()
{
	static const FElysiumPoseParams Empty;
	return Empty;
}

const FElysiumBlendCell* FElysiumBlendGrid::CellAt(int32 Axis0, int32 Axis1) const
{
	for (const FElysiumBlendCell& Cell : Cells)
	{
		if (Cell.Axis[0] == Axis0 && Cell.Axis[1] == Axis1)
		{
			return &Cell;
		}
	}
	return nullptr;
}

bool FElysiumBlendTable::Load(const FString& RelPath, FString& OutError)
{
	const FString Path = FElysiumContentPaths::NpcBlends(RelPath);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	return LoadJsonText(JsonText, OutError);
}

bool FElysiumBlendTable::LoadJsonText(const FString& JsonText, FString& OutError)
{
	PoseParams.Reset();
	Grids.Reset();

	TSharedPtr<FJsonObject> Document;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
	{
		OutError = TEXT("malformed blend JSON");
		return false;
	}

	FString FileStem;
	if (Document->TryGetStringField(TEXT("stem"), FileStem) && Stem.IsEmpty())
	{
		Stem = FileStem;
	}

	// Order is the binding: a grid axis names a pose parameter by INDEX into this array, so the two
	// are read together or an axis cannot be resolved at all.
	const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
	if (Document->TryGetArrayField(TEXT("pose_parameters"), Params) && Params != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Params)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr)
			{
				continue;
			}
			FElysiumPoseParamDesc Desc;
			(*Object)->TryGetStringField(TEXT("name"), Desc.Name);
			(*Object)->TryGetNumberField(TEXT("flags"), Desc.Flags);
			(*Object)->TryGetNumberField(TEXT("start"), Desc.Start);
			(*Object)->TryGetNumberField(TEXT("end"), Desc.End);
			(*Object)->TryGetNumberField(TEXT("loop"), Desc.Loop);
			PoseParams.Add(MoveTemp(Desc));
		}
	}

	const TSharedPtr<FJsonObject>* GridObject = nullptr;
	if (!Document->TryGetObjectField(TEXT("grids"), GridObject) || GridObject == nullptr)
	{
		OutError = TEXT("blend sidecar carries no `grids` object");
		return false;
	}

	int32 Malformed = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*GridObject)->Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Object) || Object == nullptr)
		{
			++Malformed;
			continue;
		}
		FElysiumBlendGrid Grid;
		Grid.Label = Pair.Key;
		ReadBlendPair(*Object, TEXT("groupsize"), Grid.GroupSize);
		ReadBlendPair(*Object, TEXT("paramindex"), Grid.ParamIndex);
		ReadBlendPair(*Object, TEXT("paramstart"), Grid.ParamStart);
		ReadBlendPair(*Object, TEXT("paramend"), Grid.ParamEnd);

		const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
		if ((*Object)->TryGetArrayField(TEXT("cells"), Cells) && Cells != nullptr)
		{
			Grid.Cells.Reserve(Cells->Num());
			for (const TSharedPtr<FJsonValue>& CellValue : *Cells)
			{
				const TSharedPtr<FJsonObject>* CellObject = nullptr;
				if (!CellValue.IsValid() || !CellValue->TryGetObject(CellObject)
					|| CellObject == nullptr)
				{
					continue;
				}
				FElysiumBlendCell Cell;
				ReadBlendPair(*CellObject, TEXT("axis"), Cell.Axis);
				(*CellObject)->TryGetNumberField(TEXT("anim"), Cell.Anim);
				// A null clip stays empty: the cell exists and addresses nothing.
				(*CellObject)->TryGetStringField(TEXT("clip"), Cell.Clip);
				// The glb spells an animation without the leading '@' a raw label can carry, and the
				// sidecar records the raw label. No shipped cell has one; stripping costs nothing and
				// a mismatch here would look like a missing animation.
				Cell.Clip.RemoveFromStart(TEXT("@"));
				Grid.Cells.Add(MoveTemp(Cell));
			}
		}

		// A grid with one cell is not a blend space, and the exporter never writes one — it drops any
		// grid left with fewer than two live cells. Skipping rather than failing keeps a damaged row
		// from costing the whole table.
		if (!Grid.IsMultiCell())
		{
			++Malformed;
			continue;
		}
		Grids.Add(Pair.Key, MoveTemp(Grid));
	}

	if (Malformed > 0)
	{
		OutError = FString::Printf(TEXT("%d malformed grid(s) skipped"), Malformed);
	}
	return !Grids.IsEmpty();
}

void ElysiumBlendGrids::ResolveAxis(const FElysiumBlendGrid& Grid, int32 Axis,
	const FElysiumPoseParamDesc* Desc, float Value, int32& OutCell, float& OutFraction)
{
	OutCell = 0;
	OutFraction = 0.f;

	if (Axis < 0 || Axis > 1)
	{
		return;
	}
	const int32 Count = Grid.GroupSize[Axis];
	// Documented: an axis with no driving parameter yields cell 0 and weight 0. Its range is a
	// degenerate 0/0, so this test has to come before any division.
	if (Count <= 1 || Grid.ParamIndex[Axis] == INDEX_NONE || Desc == nullptr)
	{
		return;
	}

	// Wrap into the loop range the descriptor declares. `move_yaw` and `hit_yaw` wrap over 360; the
	// aim parameters declare a zero loop and must not.
	float Wrapped = Value;
	if (!FMath::IsNearlyZero(Desc->Loop))
	{
		Wrapped = Desc->Start
			+ FMath::Fmod(FMath::Fmod(Value - Desc->Start, Desc->Loop) + Desc->Loop, Desc->Loop);
	}

	const float DescSpan = Desc->End - Desc->Start;
	if (FMath::IsNearlyZero(DescSpan))
	{
		return;
	}
	const float Normalized = (Wrapped - Desc->Start) / DescSpan;

	// Remap through the grid's own slice of the parameter, expressed in the same normalized space.
	// Every shipped grid spans the whole descriptor, so this is the identity on current content and
	// is here because the format allows a sequence to use part of a parameter's range.
	const float GridLow = (Grid.ParamStart[Axis] - Desc->Start) / DescSpan;
	const float GridHigh = (Grid.ParamEnd[Axis] - Desc->Start) / DescSpan;
	const float GridSpan = GridHigh - GridLow;
	if (FMath::IsNearlyZero(GridSpan))
	{
		return;
	}

	const float Alpha = FMath::Clamp((Normalized - GridLow) / GridSpan, 0.f, 1.f);
	const float Scaled = Alpha * static_cast<float>(Count - 1);
	OutCell = FMath::Clamp(FMath::FloorToInt(Scaled), 0, Count - 1);
	OutFraction = Scaled - static_cast<float>(OutCell);
}

FElysiumBlendPick ElysiumBlendGrids::SelectCell(const FElysiumBlendGrid& Grid,
	const FElysiumBlendTable& Table, const FElysiumPoseParams& Pose)
{
	FElysiumBlendPick Pick;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const FElysiumPoseParamDesc* Desc = Table.Param(Grid.ParamIndex[Axis]);
		const float Value = Desc != nullptr ? Pose.Get(Desc->Name) : 0.f;
		ResolveAxis(Grid, Axis, Desc, Value, Pick.Index[Axis], Pick.Fraction[Axis]);
	}

	auto Playable = [](const FElysiumBlendCell* Cell)
	{
		return Cell != nullptr && !Cell->Clip.IsEmpty();
	};

	const FElysiumBlendCell* Cell = Grid.CellAt(Pick.Index[0], Pick.Index[1]);
	if (!Playable(Cell))
	{
		// Step to the neighbour the fractions already point at before giving up on locality — a hole
		// should cost the nearest cell, not the first one in declaration order.
		const int32 Next0 = Pick.Index[0] + (Pick.Fraction[0] > 0.f ? 1 : -1);
		const int32 Next1 = Pick.Index[1] + (Pick.Fraction[1] > 0.f ? 1 : -1);
		const FElysiumBlendCell* Neighbour = Grid.CellAt(Next0, Pick.Index[1]);
		if (!Playable(Neighbour))
		{
			Neighbour = Grid.CellAt(Pick.Index[0], Next1);
		}
		if (Playable(Neighbour))
		{
			Cell = Neighbour;
		}
		else
		{
			Cell = nullptr;
			for (const FElysiumBlendCell& Candidate : Grid.Cells)
			{
				if (!Candidate.Clip.IsEmpty())
				{
					Cell = &Candidate;
					break;
				}
			}
		}
	}

	Pick.Cell = Cell;
	return Pick;
}
