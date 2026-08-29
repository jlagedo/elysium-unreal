#include "Visual/ElysiumFacialRig.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumEyeRig.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFacial, Log, All);

namespace
{
	// Source's own stack depth for a flex rule. The deepest shipped rule uses six slots.
	constexpr int32 GFlexStackSize = 32;

	EElysiumFlexOp OpFromName(const FString& Name)
	{
		if (Name.Equals(TEXT("CONST"),  ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Const;  }
		if (Name.Equals(TEXT("FETCH1"), ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Fetch1; }
		if (Name.Equals(TEXT("FETCH2"), ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Fetch2; }
		if (Name.Equals(TEXT("ADD"),    ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Add;    }
		if (Name.Equals(TEXT("SUB"),    ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Sub;    }
		if (Name.Equals(TEXT("MUL"),    ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Mul;    }
		if (Name.Equals(TEXT("DIV"),    ESearchCase::IgnoreCase)) { return EElysiumFlexOp::Div;    }
		return EElysiumFlexOp::None;
	}
}

int32 FElysiumFacialRig::FindController(const FString& Name) const
{
	for (int32 i = 0; i < Controllers.Num(); ++i)
	{
		if (Controllers[i].Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumFacialRig::FindFlexDesc(const FString& Name) const
{
	for (int32 i = 0; i < FlexDescs.Num(); ++i)
	{
		if (FlexDescs[i].Equals(Name, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// Load.

bool FElysiumFacialRig::Load(const FString& RelPath, FString& OutError)
{
	const FString Path = FElysiumContentPaths::NpcFacial(RelPath);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	return LoadJsonText(JsonText, OutError);
}

bool FElysiumFacialRig::LoadJsonText(const FString& JsonText, FString& OutError)
{
	Stem.Reset();
	FlexDescs.Reset();
	Controllers.Reset();
	Rules.Reset();
	Morphs.Reset();
	Lids.Reset();
	Mouth = FElysiumFlexMouth();
	MouthBridge = INDEX_NONE;
	BlinkController = INDEX_NONE;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed facial JSON");
		return false;
	}
	Root->TryGetStringField(TEXT("stem"), Stem);

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

	if (Root->TryGetArrayField(TEXT("flexdescs"), Values) && Values != nullptr)
	{
		FlexDescs.Reserve(Values->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FlexDescs.Add(Value.IsValid() ? Value->AsString() : FString());
		}
	}

	if (Root->TryGetArrayField(TEXT("controllers"), Values) && Values != nullptr)
	{
		Controllers.Reserve(Values->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || Obj == nullptr)
			{
				continue;
			}
			FElysiumFlexController Controller;
			(*Obj)->TryGetStringField(TEXT("name"), Controller.Name);
			(*Obj)->TryGetStringField(TEXT("type"), Controller.Type);
			double Number = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("min"), Number)) { Controller.Min = static_cast<float>(Number); }
			if ((*Obj)->TryGetNumberField(TEXT("max"), Number)) { Controller.Max = static_cast<float>(Number); }
			Controllers.Add(MoveTemp(Controller));
		}
	}

	if (Root->TryGetArrayField(TEXT("rules"), Values) && Values != nullptr)
	{
		Rules.Reserve(Values->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || Obj == nullptr)
			{
				continue;
			}
			FElysiumFlexRule Rule;
			(*Obj)->TryGetNumberField(TEXT("flexdesc"), Rule.FlexDesc);
			const TArray<TSharedPtr<FJsonValue>>* OpRows = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("ops"), OpRows) && OpRows != nullptr)
			{
				Rule.Ops.Reserve(OpRows->Num());
				for (const TSharedPtr<FJsonValue>& OpValue : *OpRows)
				{
					// An op is `[name]`, or `[name, operand]` for CONST (a float) and FETCH1/FETCH2
					// (an index). FETCH1's operand indexes the controllers; FETCH2's indexes the
					// flexdescs, which is why the two cannot share a decode.
					const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
					if (!OpValue.IsValid() || !OpValue->TryGetArray(Row) || Row == nullptr || Row->IsEmpty())
					{
						continue;
					}
					FElysiumFlexOpCode Code;
					Code.Op = OpFromName((*Row)[0]->AsString());
					if (Code.Op == EElysiumFlexOp::None)
					{
						continue;
					}
					if (Row->Num() > 1)
					{
						const double Operand = (*Row)[1]->AsNumber();
						Code.Value = static_cast<float>(Operand);
						Code.Index = static_cast<int32>(Operand);
					}
					Rule.Ops.Add(Code);
				}
			}
			if (Rule.FlexDesc != INDEX_NONE && !Rule.Ops.IsEmpty())
			{
				Rules.Add(MoveTemp(Rule));
			}
		}
	}

	if (Root->TryGetArrayField(TEXT("morphs"), Values) && Values != nullptr)
	{
		Morphs.Reserve(Values->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || Obj == nullptr)
			{
				continue;
			}
			FElysiumFlexMorph Morph;
			(*Obj)->TryGetStringField(TEXT("name"), Morph.Name);
			(*Obj)->TryGetNumberField(TEXT("flexdesc"), Morph.FlexDesc);
			const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("targets"), Targets) && Targets != nullptr
				&& Targets->Num() == 4)
			{
				for (int32 i = 0; i < 4; ++i)
				{
					Morph.Targets[i] = static_cast<float>((*Targets)[i]->AsNumber());
				}
			}
			if (Morph.Name.IsEmpty() || !FlexDescs.IsValidIndex(Morph.FlexDesc))
			{
				continue;
			}
			Morph.Curve = FName(*Morph.Name);
			Morphs.Add(MoveTemp(Morph));
		}
	}

	if (Root->TryGetArrayField(TEXT("mouths"), Values) && Values != nullptr && !Values->IsEmpty())
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if ((*Values)[0].IsValid() && (*Values)[0]->TryGetObject(Obj) && Obj != nullptr)
		{
			(*Obj)->TryGetNumberField(TEXT("bone"), Mouth.Bone);
			(*Obj)->TryGetNumberField(TEXT("flexdesc"), Mouth.FlexDesc);
			const TArray<TSharedPtr<FJsonValue>>* Forward = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("forward"), Forward) && Forward != nullptr
				&& Forward->Num() == 3)
			{
				Mouth.Forward = FVector((*Forward)[0]->AsNumber(), (*Forward)[1]->AsNumber(),
					(*Forward)[2]->AsNumber());
			}
		}
	}

	// Both bounds have to be usable together: the pair is a clamp, and half of one is not a narrower
	// filter but a wrong one. An absent field and the unrigged (0, 0) therefore land on the same
	// modal-pair default rather than on a mix.
	const TArray<TSharedPtr<FJsonValue>>* Filter = nullptr;
	if (Root->TryGetArrayField(TEXT("phoneme_filter"), Filter) && Filter != nullptr
		&& Filter->Num() == 2 && (*Filter)[0].IsValid() && (*Filter)[1].IsValid())
	{
		const float Lo = static_cast<float>((*Filter)[0]->AsNumber());
		const float Hi = static_cast<float>((*Filter)[1]->AsNumber());
		if (Lo > 0.f && Hi > 0.f)
		{
			PhonemeFilterMin = FMath::Min(Lo, Hi);
			PhonemeFilterMax = FMath::Max(Lo, Hi);
		}
	}

	// A well-formed sidecar that drives nothing is a real export state, not a failure: `shovelhead`
	// carries the whole 65/44/60 rig and no flex record that deforms a mesh, and `female_raver_1`
	// carries a single flexdesc and nothing else. Both parse; `IsValid` is what says whether there
	// is a face here to move.

	// Derive the lid combine (see FElysiumFlexLid). A flexdesc qualifies when it deforms the mesh,
	// no rule computes it, its two ramps hinge on a shared value, and the rig names all three of its
	// lowerer/neutral/raiser sources — the shape only the four `$eyelid` flexdescs have.
	TSet<int32> Ruled;
	Ruled.Reserve(Rules.Num());
	for (const FElysiumFlexRule& Rule : Rules)
	{
		Ruled.Add(Rule.FlexDesc);
	}
	TMap<int32, TArray<int32>> MorphsByFlexDesc;
	for (int32 i = 0; i < Morphs.Num(); ++i)
	{
		MorphsByFlexDesc.FindOrAdd(Morphs[i].FlexDesc).Add(i);
	}
	for (const TPair<int32, TArray<int32>>& Pair : MorphsByFlexDesc)
	{
		if (Ruled.Contains(Pair.Key) || Pair.Value.Num() != 2)
		{
			continue;
		}
		const FElysiumFlexMorph& A = Morphs[Pair.Value[0]];
		const FElysiumFlexMorph& B = Morphs[Pair.Value[1]];
		const bool bAIsLow = A.Targets[0] <= B.Targets[0];
		const FElysiumFlexMorph& Low = bAIsLow ? A : B;
		const FElysiumFlexMorph& High = bAIsLow ? B : A;
		if (!FMath::IsNearlyEqual(Low.Targets[3], High.Targets[0]))
		{
			continue;   // two ramps, but not a hinge
		}
		const FString& Name = FlexDescs[Pair.Key];
		FElysiumFlexLid Lid;
		Lid.FlexDesc = Pair.Key;
		Lid.Lowerer = FindFlexDesc(Name + TEXT("_lowerer"));
		Lid.Neutral = FindFlexDesc(Name + TEXT("_neutral"));
		Lid.Raiser  = FindFlexDesc(Name + TEXT("_raiser"));
		if (Lid.Lowerer == INDEX_NONE || Lid.Neutral == INDEX_NONE || Lid.Raiser == INDEX_NONE)
		{
			UE_LOG(LogElysiumFacial, Warning,
				TEXT("facial '%s': flexdesc '%s' hinges two ramps but has no rule and no lid sources; "
					 "it stays at zero"), *Stem, *Name);
			continue;
		}
		Lid.LoweredAngle = Low.Targets[2];
		Lid.NeutralAngle = Low.Targets[3];
		Lid.RaisedAngle  = High.Targets[1];
		Lids.Add(Lid);
	}
	// Deterministic order, so a debug dump and a test read the same rows on every load.
	Lids.Sort([](const FElysiumFlexLid& A, const FElysiumFlexLid& B) { return A.FlexDesc < B.FlexDesc; });

	// The blink envelope's target. One of the eight `eyelid` controllers, present on every rigged
	// character; absent only on a rig with no eyelid family.
	BlinkController = FindController(TEXT("blink"));

	// The jaw bridge's target (see FElysiumFacialRig::MouthBridge). Looked up by name, like the lid
	// sources, and absent on a rig that carries no phoneme family at all.
	if (Mouth.IsValid())
	{
		MouthBridge = FindController(TEXT("jaw_drop"));
		if (MouthBridge == INDEX_NONE && !Controllers.IsEmpty())
		{
			UE_LOG(LogElysiumFacial, Verbose,
				TEXT("facial '%s': carries a mouth record but no 'jaw_drop' controller; the jaw writes "
					 "its flexdesc and moves nothing"), *Stem);
		}
	}

	return true;
}

// Evaluation.

float FElysiumFacialRig::RampWeight(const float Targets[4], float FlexWeight)
{
	if (FlexWeight <= Targets[0] || FlexWeight >= Targets[3])
	{
		return 0.f;
	}
	if (FlexWeight < Targets[1])
	{
		return (FlexWeight - Targets[0]) / (Targets[1] - Targets[0]);
	}
	if (FlexWeight > Targets[2])
	{
		return (Targets[3] - FlexWeight) / (Targets[3] - Targets[2]);
	}
	return 1.f;
}

float FElysiumFacialRig::EvalRule(const FElysiumFlexRule& Rule, TArrayView<const float> ControllerValues,
	TArrayView<const float> FlexWeights)
{
	float Stack[GFlexStackSize];
	int32 Top = 0;

	for (const FElysiumFlexOpCode& Code : Rule.Ops)
	{
		switch (Code.Op)
		{
		case EElysiumFlexOp::Const:
		case EElysiumFlexOp::Fetch1:
		case EElysiumFlexOp::Fetch2:
		{
			if (Top >= GFlexStackSize)
			{
				return 0.f;
			}
			float Value = 0.f;
			if (Code.Op == EElysiumFlexOp::Const)
			{
				Value = Code.Value;
			}
			else if (Code.Op == EElysiumFlexOp::Fetch1)
			{
				Value = ControllerValues.IsValidIndex(Code.Index) ? ControllerValues[Code.Index] : 0.f;
			}
			else
			{
				Value = FlexWeights.IsValidIndex(Code.Index) ? FlexWeights[Code.Index] : 0.f;
			}
			Stack[Top++] = Value;
			break;
		}
		default:
		{
			if (Top < 2)
			{
				return 0.f;
			}
			const float B = Stack[--Top];
			const float A = Stack[Top - 1];
			switch (Code.Op)
			{
			case EElysiumFlexOp::Add: Stack[Top - 1] = A + B; break;
			case EElysiumFlexOp::Sub: Stack[Top - 1] = A - B; break;
			case EElysiumFlexOp::Mul: Stack[Top - 1] = A * B; break;
			// Source's own guard, and load-bearing: `1 / right_open` is a shipped rule, and
			// `right_open` is zero on a closed mouth.
			case EElysiumFlexOp::Div: Stack[Top - 1] = B > 0.0001f ? A / B : 0.f; break;
			default: return 0.f;
			}
			break;
		}
		}
	}
	return Top > 0 ? Stack[0] : 0.f;
}

void FElysiumFacialRig::EvalFlexWeights(TArrayView<const float> ControllerValues,
	TArray<float>& OutFlexWeights) const
{
	// A flexdesc no rule computes stays at zero, which is what Source zeroes the array for.
	OutFlexWeights.Reset(FlexDescs.Num());
	OutFlexWeights.AddZeroed(FlexDescs.Num());

	// In file order, writing as it goes: the rules are authored in dependency order and `FETCH2`
	// reads what an earlier one wrote.
	for (const FElysiumFlexRule& Rule : Rules)
	{
		if (OutFlexWeights.IsValidIndex(Rule.FlexDesc))
		{
			OutFlexWeights[Rule.FlexDesc] = EvalRule(Rule, ControllerValues, OutFlexWeights);
		}
	}

	// The lid combine stands in for the absent eyeball record: three lid-shape weights against their
	// three authored angles give the lid's position, which the hinged ramps then split.
	for (const FElysiumFlexLid& Lid : Lids)
	{
		if (!OutFlexWeights.IsValidIndex(Lid.FlexDesc))
		{
			continue;
		}
		const float Lowerer = OutFlexWeights.IsValidIndex(Lid.Lowerer) ? OutFlexWeights[Lid.Lowerer] : 0.f;
		const float Neutral = OutFlexWeights.IsValidIndex(Lid.Neutral) ? OutFlexWeights[Lid.Neutral] : 0.f;
		const float Raiser  = OutFlexWeights.IsValidIndex(Lid.Raiser)  ? OutFlexWeights[Lid.Raiser]  : 0.f;
		OutFlexWeights[Lid.FlexDesc] = Lowerer * Lid.LoweredAngle
			+ Neutral * Lid.NeutralAngle
			+ Raiser * Lid.RaisedAngle;
	}
}

void FElysiumFacialRig::EvalMorphWeights(TArrayView<const float> FlexWeights,
	TArray<float>& OutMorphWeights) const
{
	OutMorphWeights.Reset(Morphs.Num());
	OutMorphWeights.AddZeroed(Morphs.Num());
	for (int32 i = 0; i < Morphs.Num(); ++i)
	{
		const FElysiumFlexMorph& Morph = Morphs[i];
		const float Weight = FlexWeights.IsValidIndex(Morph.FlexDesc) ? FlexWeights[Morph.FlexDesc] : 0.f;
		OutMorphWeights[i] = RampWeight(Morph.Targets, Weight);
	}
}

void FElysiumFacialRig::Evaluate(TArrayView<const float> ControllerValues,
	TArray<float>& OutFlexWeights, TArray<float>& OutMorphWeights) const
{
	Evaluate(ControllerValues, FElysiumJawInput(), OutFlexWeights, OutMorphWeights);
}

void FElysiumFacialRig::ApplyJawToFlexWeights(const FElysiumJawInput& Jaw,
	TArray<float>& InOutFlexWeights) const
{
	if (Mouth.IsValid() && InOutFlexWeights.IsValidIndex(Mouth.FlexDesc))
	{
		InOutFlexWeights[Mouth.FlexDesc] = FMath::Clamp(Jaw.Open, 0.f, 1.f);
	}
}

void FElysiumEyeAim::FromRecord(const FElysiumEyeball& Eye, const FElysiumEyeState& State)
{
	UpLocal = State.UpLocal;
	ForwardLocal = State.ForwardLocal;
	// The record's own axis, bone-local — deliberately NOT `State.AuthoredUp`, which is the same
	// vector rotated into the space the basis was solved in. Mixing the two silently turns the lid
	// projection into a dot between two different spaces.
	AuthoredUp = Eye.Up;
	Radius = Eye.Radius;
	FMemory::Memcpy(UpperFlexDesc, Eye.UpperFlexDesc, sizeof(UpperFlexDesc));
	FMemory::Memcpy(LowerFlexDesc, Eye.LowerFlexDesc, sizeof(LowerFlexDesc));
	FMemory::Memcpy(UpperTarget, Eye.UpperTarget, sizeof(UpperTarget));
	FMemory::Memcpy(LowerTarget, Eye.LowerTarget, sizeof(LowerTarget));
	UpperLidFlexDesc = Eye.UpperLidFlexDesc;
	LowerLidFlexDesc = Eye.LowerLidFlexDesc;
	bValid = State.bValid && Eye.HasLids();
}

void FElysiumFacialRig::ApplyEyesToFlexWeights(const FElysiumEyeInput& Eyes,
	TArray<float>& InOutFlexWeights) const
{
	if (!Eyes.bWriteLids)
	{
		return;
	}
	// The renderer's own lid solve, per eye and per lid. `Sum` is an angle in radians built from
	// the three lid-state weights the rules produced against their authored offsets; the point that
	// angle places on the eyeball, measured along the record's up axis, is the lid's weight.
	const auto Solve = [&InOutFlexWeights](const FElysiumEyeAim& Aim, const int32 (&Sources)[3],
		const float (&Targets)[3], int32 LidFlexDesc)
	{
		if (!InOutFlexWeights.IsValidIndex(LidFlexDesc) || Aim.Radius <= 0.f)
		{
			return;
		}
		float Sum = 0.f;
		for (int32 k = 0; k < 3; ++k)
		{
			if (!InOutFlexWeights.IsValidIndex(Sources[k]))
			{
				continue;
			}
			// The targets are linear offsets in eyeball units; the ratio is what makes an angle.
			const float Ratio = FMath::Clamp(Targets[k] / Aim.Radius, -1.f, 1.f);
			Sum += FMath::Asin(Ratio) * InOutFlexWeights[Sources[k]];
		}
		const FVector Point = Aim.UpLocal * (FMath::Sin(Sum) * Aim.Radius)
			+ Aim.ForwardLocal * (FMath::Cos(Sum) * Aim.Radius);
		InOutFlexWeights[LidFlexDesc] = static_cast<float>(FVector::DotProduct(Point, Aim.AuthoredUp));
	};

	for (const FElysiumEyeAim& Aim : Eyes.Eyes)
	{
		if (!Aim.bValid)
		{
			continue;
		}
		Solve(Aim, Aim.UpperFlexDesc, Aim.UpperTarget, Aim.UpperLidFlexDesc);
		Solve(Aim, Aim.LowerFlexDesc, Aim.LowerTarget, Aim.LowerLidFlexDesc);
	}
}

void FElysiumFacialRig::Evaluate(TArrayView<const float> ControllerValues, const FElysiumJawInput& Jaw,
	const FElysiumEyeInput& Eyes, TArray<float>& OutFlexWeights,
	TArray<float>& OutMorphWeights) const
{
	// 1 / 1.5 — the two writes that have to reach the rules as *controller* values. The jaw is
	// raised (an expression already opening the mouth keeps its own value); blink is assigned,
	// because retail's blink overwrites whatever the controller pass left there.
	const bool bBridge = Jaw.bBridge && Jaw.IsOpen() && Controllers.IsValidIndex(MouthBridge)
		&& ControllerValues.IsValidIndex(MouthBridge);
	const bool bBlink = Controllers.IsValidIndex(BlinkController)
		&& ControllerValues.IsValidIndex(BlinkController) && Eyes.Blink > 0.f;
	if (bBridge || bBlink)
	{
		TArray<float, TInlineAllocator<64>> Values(ControllerValues);
		if (bBridge)
		{
			Values[MouthBridge] = FMath::Max(Values[MouthBridge],
				Controllers[MouthBridge].Normalize(Jaw.Open));
		}
		if (bBlink)
		{
			// Raw: no Normalize, deliberately. The shipped controllers are all 0..1 so the remap
			// would be the identity here, but the asymmetry is authored and a modded rig would show it.
			Values[BlinkController] = Eyes.Blink;
		}
		EvalFlexWeights(Values, OutFlexWeights);
	}
	else
	{
		EvalFlexWeights(ControllerValues, OutFlexWeights);
	}
	// 2.5 — the authored eye pass, over the lid flexdescs the records name.
	ApplyEyesToFlexWeights(Eyes, OutFlexWeights);
	// 3 — the direct flexdesc write, after every rule that could have computed it.
	ApplyJawToFlexWeights(Jaw, OutFlexWeights);
	// 4 — the target ramps.
	EvalMorphWeights(OutFlexWeights, OutMorphWeights);
}

void FElysiumFacialRig::Evaluate(TArrayView<const float> ControllerValues, const FElysiumJawInput& Jaw,
	TArray<float>& OutFlexWeights, TArray<float>& OutMorphWeights) const
{
	// No eye data is the same structurally inert no-op that no jaw already is: the eye pass writes
	// nothing and the `FElysiumFlexLid` reconstruction keeps every hinged lid.
	Evaluate(ControllerValues, Jaw, FElysiumEyeInput(), OutFlexWeights, OutMorphWeights);
}
