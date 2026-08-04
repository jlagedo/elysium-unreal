#include "Visual/ElysiumClothRig.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
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

	FName ReadName(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
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
	Root = ReadName(Document, TEXT("root"));
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
			Chain.Root = ReadName(*Object, TEXT("root"));
			Chain.End = ReadName(*Object, TEXT("end"));

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
					Body.Bone = ReadName(*BodyObject, TEXT("bone"));
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
			Collider.Bone = ReadName(*Object, TEXT("bone"));
			double Number = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("radius"), Number))
			{
				Collider.Radius = Number;
			}
			ReadVector((*Object)->TryGetField(TEXT("offset")), Collider.Offset);
			if (Collider.Bone != NAME_None && Collider.Radius > 0.f)
			{
				Colliders.Add(Collider);
			}
		}
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
