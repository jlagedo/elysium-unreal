#include "Visual/ElysiumClothRig.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// File-prefixed rather than the plain `ReadVector`/`ReadName` these would naturally be called:
// the module builds non-unity adaptively, so this translation unit is regularly concatenated with
// `ElysiumCompositionRig.cpp`, whose anonymous namespace holds a helper of exactly that name.
namespace
{
	bool ReadClothVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
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

	FName ReadClothName(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Text;
		return Object->TryGetStringField(Field, Text) && !Text.IsEmpty()
			? FName(*Text) : NAME_None;
	}
}

bool FElysiumClothRig::Load(const FString& InStem, FString& OutError)
{
	Stem = InStem;
	const FString Path = FElysiumContentPaths::NpcClothRig(InStem);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	if (!LoadJsonText(JsonText, OutError))
	{
		return false;
	}
	ApplyAssetImport();
	return true;
}

bool FElysiumClothRig::LoadJsonText(const FString& JsonText, FString& OutError)
{
	Chains.Reset();
	AnchorRow.Reset();
	Colliders.Reset();
	Solver = FElysiumClothSolver();

	TSharedPtr<FJsonObject> Document;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
	{
		OutError = TEXT("malformed cloth JSON");
		return false;
	}

	FString FileStem;
	if (Document->TryGetStringField(TEXT("stem"), FileStem) && Stem.IsEmpty())
	{
		Stem = FileStem;
	}
	Root = ReadClothName(Document, TEXT("root"));
	Document->TryGetNumberField(TEXT("rows"), Rows);
	Document->TryGetNumberField(TEXT("columns"), Columns);

	// Every solver field is optional and falls back to the struct's own default, so a sidecar
	// written before a knob existed still loads rather than zeroing that knob.
	const TSharedPtr<FJsonObject>* SolverObject = nullptr;
	if (Document->TryGetObjectField(TEXT("solver"), SolverObject) && SolverObject != nullptr)
	{
		const TSharedPtr<FJsonObject>& S = *SolverObject;
		double Number = 0.0;
		if (S->TryGetNumberField(TEXT("linear_damping"), Number)) { Solver.LinearDamping = Number; }
		if (S->TryGetNumberField(TEXT("angular_damping"), Number)) { Solver.AngularDamping = Number; }
		if (S->TryGetNumberField(TEXT("gravity_scale"), Number)) { Solver.GravityScale = Number; }
		if (S->TryGetNumberField(TEXT("component_linear_vel_scale"), Number))
		{
			Solver.ComponentLinearVelScale = Number;
		}
		if (S->TryGetNumberField(TEXT("component_linear_acc_scale"), Number))
		{
			Solver.ComponentLinearAccScale = Number;
		}
		S->TryGetNumberField(TEXT("iterations_pre"), Solver.IterationsPre);
		S->TryGetNumberField(TEXT("iterations_post"), Solver.IterationsPost);
	}

	const TArray<TSharedPtr<FJsonValue>>* ChainValues = nullptr;
	if (Document->TryGetArrayField(TEXT("chains"), ChainValues) && ChainValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChainValues)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr)
			{
				continue;
			}
			FElysiumClothChain Chain;
			(*Object)->TryGetNumberField(TEXT("column"), Chain.Column);
			Chain.Root = ReadClothName(*Object, TEXT("root"));
			Chain.End = ReadClothName(*Object, TEXT("end"));

			const TArray<TSharedPtr<FJsonValue>>* BodyValues = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("bodies"), BodyValues) && BodyValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& BodyValue : *BodyValues)
				{
					const TSharedPtr<FJsonObject>* BodyObject = nullptr;
					if (!BodyValue.IsValid() || !BodyValue->TryGetObject(BodyObject)
						|| BodyObject == nullptr)
					{
						continue;
					}
					FElysiumClothBody Body;
					Body.Bone = ReadClothName(*BodyObject, TEXT("bone"));
					(*BodyObject)->TryGetNumberField(TEXT("row"), Body.Row);
					double Number = 0.0;
					if ((*BodyObject)->TryGetNumberField(TEXT("cone_deg"), Number))
					{
						Body.ConeAngleDeg = Number;
					}
					if ((*BodyObject)->TryGetNumberField(TEXT("box_extent"), Number))
					{
						Body.BoxExtent = Number;
					}
					if (Body.Bone != NAME_None)
					{
						Chain.Bodies.Add(Body);
					}
				}
			}

			// A chain whose head and tail do not match its own body list would make
			// `FAnimNode_AnimDynamics::InitPhysics` rebuild `PhysicsBodyDefinitions` from the
			// reference skeleton and clone one prototype over every row — silently flattening the
			// cone ramp instead of failing. Drop the chain rather than install a lie.
			if (Chain.Bodies.IsEmpty() || Chain.Bodies[0].Bone != Chain.Root
				|| Chain.Bodies.Last().Bone != Chain.End)
			{
				OutError = FString::Printf(
					TEXT("chain %d does not run from its declared root to its declared end"),
					Chain.Column);
				return false;
			}
			Chains.Add(MoveTemp(Chain));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* AnchorValues = nullptr;
	if (Document->TryGetArrayField(TEXT("anchor_row"), AnchorValues) && AnchorValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AnchorValues)
		{
			FString Name;
			if (Value.IsValid() && Value->TryGetString(Name) && !Name.IsEmpty())
			{
				AnchorRow.Add(FName(*Name));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ColliderValues = nullptr;
	if (Document->TryGetArrayField(TEXT("colliders"), ColliderValues) && ColliderValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ColliderValues)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr)
			{
				continue;
			}
			FElysiumClothCollider Collider;
			Collider.Bone = ReadClothName(*Object, TEXT("bone"));
			double Number = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("radius"), Number))
			{
				Collider.Radius = Number;
			}
			ReadClothVector((*Object)->TryGetField(TEXT("offset")), Collider.Offset);
			if (Collider.Bone != NAME_None && Collider.Radius > 0.f)
			{
				Colliders.Add(Collider);
			}
		}
	}

	return true;
}

FElysiumClothTuning FElysiumClothTuning::FromRig(const FElysiumClothRig& Rig)
{
	FElysiumClothTuning Tuning;
	Tuning.Solver = Rig.Solver;
	return Tuning;
}

bool FElysiumClothTuning::EqualsTuning(const FElysiumClothTuning& Other) const
{
	return FMath::IsNearlyEqual(ConeScale, Other.ConeScale)
		&& FMath::IsNearlyEqual(ColliderRadiusScale, Other.ColliderRadiusScale)
		&& FMath::IsNearlyEqual(BoxExtentScale, Other.BoxExtentScale)
		&& FMath::IsNearlyEqual(Solver.LinearDamping, Other.Solver.LinearDamping)
		&& FMath::IsNearlyEqual(Solver.AngularDamping, Other.Solver.AngularDamping)
		&& FMath::IsNearlyEqual(Solver.GravityScale, Other.Solver.GravityScale)
		&& FMath::IsNearlyEqual(Solver.ComponentLinearVelScale, Other.Solver.ComponentLinearVelScale)
		&& FMath::IsNearlyEqual(Solver.ComponentLinearAccScale, Other.Solver.ComponentLinearAccScale)
		&& Solver.IterationsPre == Other.Solver.IterationsPre
		&& Solver.IterationsPost == Other.Solver.IterationsPost;
}

bool FElysiumClothTuning::NeedsReseat(const FElysiumClothTuning& Other) const
{
	return !FMath::IsNearlyEqual(Solver.LinearDamping, Other.Solver.LinearDamping)
		|| !FMath::IsNearlyEqual(Solver.AngularDamping, Other.Solver.AngularDamping)
		|| !FMath::IsNearlyEqual(BoxExtentScale, Other.BoxExtentScale);
}

bool ElysiumClothRig::SaveTuning(const FString& Stem, const FElysiumClothTuning& Tuning,
	FString& OutError)
{
	const FString Path = FElysiumContentPaths::NpcClothRig(Stem);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> Document;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
	{
		OutError = FString::Printf(TEXT("malformed cloth JSON: %s"), *Path);
		return false;
	}

	// The solver block is written whole. A sidecar predating a knob simply gains it, which is the
	// same forgiving direction the reader takes.
	const TSharedPtr<FJsonObject> Solver = MakeShared<FJsonObject>();
	Solver->SetNumberField(TEXT("linear_damping"), Tuning.Solver.LinearDamping);
	Solver->SetNumberField(TEXT("angular_damping"), Tuning.Solver.AngularDamping);
	Solver->SetNumberField(TEXT("gravity_scale"), Tuning.Solver.GravityScale);
	Solver->SetNumberField(TEXT("component_linear_vel_scale"), Tuning.Solver.ComponentLinearVelScale);
	Solver->SetNumberField(TEXT("component_linear_acc_scale"), Tuning.Solver.ComponentLinearAccScale);
	Solver->SetNumberField(TEXT("iterations_pre"), Tuning.Solver.IterationsPre);
	Solver->SetNumberField(TEXT("iterations_post"), Tuning.Solver.IterationsPost);
	Document->SetObjectField(TEXT("solver"), Solver);

	// The three scales are folded into the authored numbers in place, so a saved file reads exactly
	// like one the exporter could have written and re-tuning starts from 1.0 again.
	const TArray<TSharedPtr<FJsonValue>>* ChainValues = nullptr;
	if (Document->TryGetArrayField(TEXT("chains"), ChainValues) && ChainValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChainValues)
		{
			const TSharedPtr<FJsonObject>* Chain = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Chain) || Chain == nullptr)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* BodyValues = nullptr;
			if (!(*Chain)->TryGetArrayField(TEXT("bodies"), BodyValues) || BodyValues == nullptr)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& BodyValue : *BodyValues)
			{
				const TSharedPtr<FJsonObject>* Body = nullptr;
				if (!BodyValue.IsValid() || !BodyValue->TryGetObject(Body) || Body == nullptr)
				{
					continue;
				}
				double Number = 0.0;
				if ((*Body)->TryGetNumberField(TEXT("cone_deg"), Number))
				{
					(*Body)->SetNumberField(TEXT("cone_deg"),
						FMath::Clamp(Number * Tuning.ConeScale, 0.0, 90.0));
				}
				if ((*Body)->TryGetNumberField(TEXT("box_extent"), Number))
				{
					(*Body)->SetNumberField(TEXT("box_extent"),
						FMath::Max(Number * Tuning.BoxExtentScale, 0.0));
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ColliderValues = nullptr;
	if (Document->TryGetArrayField(TEXT("colliders"), ColliderValues) && ColliderValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ColliderValues)
		{
			const TSharedPtr<FJsonObject>* Collider = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Collider) || Collider == nullptr)
			{
				continue;
			}
			double Number = 0.0;
			if ((*Collider)->TryGetNumberField(TEXT("radius"), Number))
			{
				(*Collider)->SetNumberField(TEXT("radius"),
					FMath::Max(Number * Tuning.ColliderRadiusScale, 0.0));
			}
		}
	}

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (!FJsonSerializer::Serialize(Document.ToSharedRef(), Writer))
	{
		OutError = TEXT("could not serialise the tuned rig");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Output, *Path))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *Path);
		return false;
	}
	return true;
}

void FElysiumClothRig::ApplyAssetImport()
{
	// The sidecar is metres in the glb's own basis, because the mesh it drives is. A collider
	// offset is a point and takes the basis change as well as the scale; a radius and a body
	// extent are plain lengths and take only the scale.
	const float Scale = ElysiumNpcVisual::ImportGlbScale();
	for (FElysiumClothCollider& Collider : Colliders)
	{
		Collider.Offset =
			ElysiumNpcVisual::ImportGlbLocal(FTransform(Collider.Offset)).GetTranslation();
		Collider.Radius *= Scale;
	}
	for (FElysiumClothChain& Chain : Chains)
	{
		for (FElysiumClothBody& Body : Chain.Bodies)
		{
			Body.BoxExtent *= Scale;
		}
	}
}
