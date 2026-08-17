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
	AutoLayers.Reset();
	Events.Reset();

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

	// The binding table, read before the grids because it is the half a model may carry alone. The
	// entry ORDER is the payload — appended in document order and never sorted or deduped.
	const TSharedPtr<FJsonObject>* LayerObject = nullptr;
	if (Document->TryGetObjectField(TEXT("autolayers"), LayerObject) && LayerObject != nullptr)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*LayerObject)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Entries) || Entries == nullptr)
			{
				continue;
			}
			FElysiumAutoLayerBinding Binding;
			for (const TSharedPtr<FJsonValue>& Entry : *Entries)
			{
				FString Clip;
				if (Entry.IsValid() && Entry->TryGetString(Clip) && !Clip.IsEmpty())
				{
					Clip.RemoveFromStart(TEXT("@"));
					Binding.Clips.Add(MoveTemp(Clip));
				}
			}
			if (!Binding.Clips.IsEmpty())
			{
				AutoLayers.Add(Pair.Key, MoveTemp(Binding));
			}
		}
	}

	// The timelines, read before the grids for the same reason: a model may carry these alone. The
	// options strings are interned per model, so the payload array is read first and every row
	// names one of its entries by index.
	int32 MalformedEvents = 0;
	TArray<FString> EventOptions;
	const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
	if (Document->TryGetArrayField(TEXT("event_options"), OptionValues) && OptionValues != nullptr)
	{
		EventOptions.Reserve(OptionValues->Num());
		for (const TSharedPtr<FJsonValue>& Value : *OptionValues)
		{
			FString Option;
			if (Value.IsValid())
			{
				Value->TryGetString(Option);
			}
			EventOptions.Add(MoveTemp(Option));
		}
	}

	const TSharedPtr<FJsonObject>* EventObject = nullptr;
	if (Document->TryGetObjectField(TEXT("events"), EventObject) && EventObject != nullptr)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*EventObject)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Rows) || Rows == nullptr)
			{
				++MalformedEvents;
				continue;
			}
			TArray<FElysiumAnimEvent> Timeline;
			Timeline.Reserve(Rows->Num());
			for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
			{
				const TArray<TSharedPtr<FJsonValue>>* Columns = nullptr;
				if (!RowValue.IsValid() || !RowValue->TryGetArray(Columns) || Columns == nullptr
					|| Columns->Num() < 4)
				{
					++MalformedEvents;
					continue;
				}
				FElysiumAnimEvent Record;
				Record.Cycle = static_cast<float>((*Columns)[0]->AsNumber());
				Record.Event = static_cast<int32>((*Columns)[1]->AsNumber());
				Record.Type = static_cast<int32>((*Columns)[2]->AsNumber());
				// A phase outside 0..1 can never be reached by a dispatcher scanning the cycle, so a
				// row carrying one is a decode fault rather than a record that simply never fires.
				if (!FMath::IsFinite(Record.Cycle) || Record.Cycle < 0.f || Record.Cycle > 1.f)
				{
					++MalformedEvents;
					continue;
				}
				const int32 OptionIndex = static_cast<int32>((*Columns)[3]->AsNumber());
				if (!EventOptions.IsValidIndex(OptionIndex))
				{
					// The row addresses a payload the file does not carry. Dropping it is the only
					// honest answer: an id whose handler reads its options cannot run without them.
					++MalformedEvents;
					continue;
				}
				Record.Options = EventOptions[OptionIndex];
				Timeline.Add(MoveTemp(Record));
			}
			if (!Timeline.IsEmpty())
			{
				Events.Add(Pair.Key, MoveTemp(Timeline));
			}
		}
	}

	const TSharedPtr<FJsonObject>* GridObject = nullptr;
	if (!Document->TryGetObjectField(TEXT("grids"), GridObject) || GridObject == nullptr)
	{
		if (AutoLayers.IsEmpty() && Events.IsEmpty())
		{
			OutError = TEXT("blend sidecar carries no `grids`, `autolayers` or `events`");
			return false;
		}
		if (MalformedEvents > 0)
		{
			OutError = FString::Printf(TEXT("%d malformed event row(s) skipped"), MalformedEvents);
		}
		return true;
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
				const TSharedPtr<FJsonObject>* MotionObject = nullptr;
				if ((*CellObject)->TryGetObjectField(TEXT("motion"), MotionObject)
					&& MotionObject != nullptr)
				{
					(*MotionObject)->TryGetNumberField(TEXT("cycle_seconds"),
						Cell.Motion.CycleSeconds);
					(*MotionObject)->TryGetNumberField(TEXT("ground_distance_cm"),
						Cell.Motion.GroundDistanceCm);
					(*MotionObject)->TryGetNumberField(TEXT("ground_speed_cm_s"),
						Cell.Motion.GroundSpeedCmPerSecond);
					if (!Cell.Motion.IsUsable())
					{
						Cell.Motion = FElysiumClipMotion{};
					}
				}
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

	if (Malformed > 0 || MalformedEvents > 0)
	{
		OutError = FString::Printf(TEXT("%d malformed grid(s) and %d malformed event row(s) skipped"),
			Malformed, MalformedEvents);
	}
	return IsValid();
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

bool ElysiumBlendGrids::SpeedFan(const FElysiumBlendGrid& Grid, const FElysiumBlendTable& Table,
	float Scale, FElysiumGaitSpeedTable& Out)
{
	Out = FElysiumGaitSpeedTable();

	const int32 Count = Grid.GroupSize[0];
	if (Count < 2 || Count > FElysiumGaitSpeedTable::MaxCells || Grid.GroupSize[1] > 1)
	{
		return false;
	}
	const FElysiumPoseParamDesc* Desc = Table.Param(Grid.ParamIndex[0]);
	if (Desc == nullptr)
	{
		return false;
	}

	// A gait fan is the whole circle. A grid using part of a parameter's range is a legal shape the
	// format allows and no shipped locomotion fan uses, and a speed table indexed by a wrapped angle
	// cannot answer for one.
	const float Span = Grid.ParamEnd[0] - Grid.ParamStart[0];
	if (FMath::IsNearlyZero(Desc->Loop) || !FMath::IsNearlyEqual(Span, Desc->Loop, 0.01f))
	{
		return false;
	}

	float Authored[FElysiumGaitSpeedTable::MaxCells] = {};
	bool bAuthored[FElysiumGaitSpeedTable::MaxCells] = {};
	int32 Usable = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumBlendCell* Cell = Grid.CellAt(Index, 0);
		if (Cell != nullptr && Cell->Motion.IsUsable())
		{
			Authored[Index] = Cell->Motion.GroundSpeedCmPerSecond;
			bAuthored[Index] = true;
			++Usable;
		}
	}
	if (Usable == 0)
	{
		return false;
	}

	// Fill the holes from the nearest authored cell on either side, weighted by how far each is.
	// The walk round the fan wraps, so a hole at the seam reads from both ends rather than from one.
	if (Usable < Count)
	{
		float Filled[FElysiumGaitSpeedTable::MaxCells] = {};
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (bAuthored[Index])
			{
				Filled[Index] = Authored[Index];
				continue;
			}
			int32 Back = 0;
			int32 BackIndex = Index;
			do
			{
				BackIndex = (BackIndex - 1 + Count) % Count;
				++Back;
			} while (!bAuthored[BackIndex]);

			int32 Forward = 0;
			int32 ForwardIndex = Index;
			do
			{
				ForwardIndex = (ForwardIndex + 1) % Count;
				++Forward;
			} while (!bAuthored[ForwardIndex]);

			const float Alpha = static_cast<float>(Back) / static_cast<float>(Back + Forward);
			Filled[Index] = FMath::Lerp(Authored[BackIndex], Authored[ForwardIndex], Alpha);
		}
		FMemory::Memcpy(Authored, Filled, sizeof(Authored));
	}

	FMemory::Memcpy(Out.Cells, Authored, sizeof(Authored));
	Out.Count = Count;
	Out.AxisMin = Grid.ParamStart[0];
	Out.AxisMax = Grid.ParamEnd[0];
	Out.Scale = Scale;
	return Out.IsValid();
}
