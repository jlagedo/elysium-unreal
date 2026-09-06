#pragma once

// Test-only projections from cooked native assets. Selection tests keep their existing value
// interfaces; transport parity reads the current character stage, never the retired export tree.
#include "ElysiumBodyData.h"
#include "ElysiumCastData.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumClipData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumModelCatalogues.h"
#include "Visual/ElysiumCharacterAssets.h"
#include "Visual/ElysiumSkeletalSource.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

namespace ElysiumNativeTest
{
	inline bool HasCast() { return ElysiumCharacterAssets::Cast() != nullptr; }

	inline bool Fail(FString& Error, const FString& Reason)
	{
		Error = Reason;
		// Some legacy test callers abstain on Load(false). A broken native reference
		// must still fail the active test rather than disappear from its coverage.
		if (FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest())
			Test->AddError(Error);
		return false;
	}

	inline FString OwnerName(const FString& Value)
	{
		// A few existing case fixtures name the old relative table label. It is only a
		// selector here; this function never opens a sidecar at that location.
		return Value.EndsWith(TEXT(".json")) ? FPaths::GetBaseFilename(Value) : Value;
	}

	inline FString CaptureOwner(const FString& Id)
	{
		FString Error;
		const auto* Row = ElysiumCharacterAssets::Model(Id, Error);
		if (!Row) return Id;
		return Row->Mesh.IsNull() ? FElysiumContentPaths::SafeName(Id.Mid(11)) : Row->Stem;
	}

	inline FString SourcePath(const FString& Owner)
	{
		FString Error;
		const auto* Data = ElysiumCharacterAssets::Body(Owner, Error);
		FString Id = Data ? Data->AssetId : FString();
		if (Id.IsEmpty())
			if (const auto* Model = ElysiumCharacterAssets::Model(Owner, Error)) Id = Model->AssetId;
		if (!Id.StartsWith(TEXT("vtmb:model:"))) return FString();
		FString Root;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ElysiumCharacterStage="), Root))
		{
			Root = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
			if (Root.IsEmpty()) return FString();
			Root /= TEXT("import/characters");
		}
		return Data && !Data->OwnerRoot.IsEmpty()
			? Root / Id.Mid(11) / (Data->OwnerRoot + TEXT(".skel"))
			: Root / (Id.Mid(11) + TEXT(".skel"));
	}

	inline bool Load(FElysiumNpcClipSet& Out, const FString& Name, FString& Error)
	{
		Out = {};
		const auto* Body = ElysiumCharacterAssets::Body(OwnerName(Name), Error);
		if (!Body) return Fail(Error, FString::Printf(TEXT("native vocabulary owner '%s': %s"), *Name, *Error));
		Out = Body->SelectionVocabulary(Name);
		// Retail fixtures use capture aliases; keep those identities at the test boundary.
		FElysiumClipTable Named;
		Out.Clips.ForEachClip([&Named](const FString& Label, const FElysiumNpcClip& Source)
		{
			FElysiumNpcClip Clip = Source; Clip.Owner = CaptureOwner(Clip.Owner);
			Named.Add(Label, Clip);
		});
		Out.Clips = MoveTemp(Named);
		return true;
	}

	inline bool Load(FElysiumBlendTable& Out, const FString& Name, FString& Error)
	{
		Out = {};
		const auto* Body = ElysiumCharacterAssets::Body(OwnerName(Name), Error);
		if (!Body) return Fail(Error, FString::Printf(TEXT("native blend owner '%s': %s"), *Name, *Error));
		Out.Stem = Name; Out.bMovementStated = true;
		for (const auto& Pair : Body->NativeSequences)
		{
			const UAnimSequence* Anim = Pair.Value.LoadSynchronous();
			const auto* Meta = Anim ? Anim->FindMetaDataByClass<UElysiumClipData>() : nullptr;
			if (!Meta || !Meta->bMovementStated)
			{
				Out = {};
				return Fail(Error, FString::Printf(TEXT("native owner '%s': sequence metadata missing: %s"),
					*Name, *Pair.Value.ToString()));
			}
			if (!Meta->Events.IsEmpty()) Out.Events.Add(Meta->SourceLabel, Meta->Events);
			if (!Meta->Movement.Records.IsEmpty()) Out.Movement.Add(Meta->SourceLabel, Meta->Movement);
		}
		for (const auto& Row : Body->Sequences)
			if (Row.Owner == Body->AssetId && !Row.DeclaredLayers.IsEmpty())
				Out.AutoLayers.Add(Row.Label, {Row.DeclaredLayers});
		for (const auto& Pair : Body->NativeBlendSpaces)
		{
			const UBlendSpace* Blend = Pair.Value.LoadSynchronous();
			const auto* Meta = Blend ? Blend->FindMetaDataByClass<UElysiumClipData>() : nullptr;
			if (!Meta || !Meta->bHasGrid)
			{
				Out = {};
				return Fail(Error, FString::Printf(TEXT("native owner '%s': grid metadata missing: %s"),
					*Name, *Pair.Value.ToString()));
			}
			Out.PoseParams = Meta->PoseParams;
			Out.Grids.Add(Meta->SourceLabel, Meta->Grid);
		}
		Error.Reset(); return true;
	}

	inline const UElysiumCharacterProvenance* MeshData(const FString& Name, FString& Error)
	{
		const FString Path = ElysiumCharacterAssets::MeshPath(OwnerName(Name));
		const auto* Mesh = Path.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *Path);
		const auto* Data = Mesh ? UElysiumCharacterProvenance::Find(Mesh) : nullptr;
		if (!Data) Error = TEXT("native mesh source data missing: ") + Name;
		else Error.Reset();
		return Data;
	}
	inline bool Load(FElysiumFacialRig& Out, const FString& Name, FString& Error)
	{ const auto* Data = MeshData(Name, Error); if (!Data) return false; Out = Data->Facial; return true; }
	inline bool Load(FElysiumEyeSet& Out, const FString& Name, FString& Error)
	{ const auto* Data = MeshData(Name, Error); if (!Data) return false; Out = Data->Eyes; return true; }
	inline bool Load(FElysiumCompositionRig& Out, const FString& Name, FString& Error)
	{ const auto* Data = MeshData(Name, Error); if (!Data) return false; Out = Data->Composition; return true; }

	inline bool Load(FElysiumNpcIndex& Out, FString& Error)
	{
		Out = {};
		const auto* Cast = ElysiumCharacterAssets::Cast();
		if (!Cast) { Error = TEXT("native DA_Cast is absent"); return false; }
		Out = {}; Out.ManifestVersion = 8; // Existing value-schema assertions, not a disk manifest.
		auto AddOwner = [&](const FString& Name, const FElysiumCastModel& Model) -> bool
		{
			FElysiumNpcIndexEntry Row; Row.Model = Model.ModelPath;
			FString LoadError;
			const auto* Body = ElysiumCharacterAssets::Body(Name, LoadError);
			if (!Body) return Fail(Error, FString::Printf(TEXT("native cast owner '%s': %s"), *Name, *LoadError));
			Row.ClipCount = Body->NativeSequences.Num();
			FElysiumBlendTable Table;
			if (!Load(Table, Name, LoadError))
			{
				Error = LoadError;
				return false; // Load already records the failure on the active test.
			}
			Row.BlendGrids = Table.Grids.Num(); Row.EventSequences = Table.Events.Num();
			if (Table.IsValid()) Row.Blends = Name;
			if (USkeletalMesh* Mesh = Model.Mesh.LoadSynchronous())
			{
				Row.Bones = Mesh->GetRefSkeleton().GetNum(); Row.MorphCount = Mesh->GetMorphTargets().Num();
				if (const auto* Data = UElysiumCharacterProvenance::Find(Mesh))
				{
					if (Data->Facial.IsValid()) Row.Facial = Name;
					Row.EyeballCount = Data->Eyes.Eyeballs.Num();
					if (Row.EyeballCount) Row.Eyes = Name;
					Row.ProceduralBones = Data->Composition.AxisRules.Num();
					if (Row.ProceduralBones) Row.Procedural = Name;
					for (const auto& Bone : Data->Composition.SplitBones) Row.SplitRotationBones.Add(Bone.ToString());
				}
				Out.Npcs.Add(Name, Row);
			}
			else Out.Banks.Add(Name, Row);
			return true;
		};
		for (const auto& Pair : Cast->Models)
			if (!Pair.Value.BodyData.IsNull() && !AddOwner(CaptureOwner(Pair.Key), Pair.Value))
			{ Out = {}; return false; }
		for (const auto& Pair : Cast->Cinematics)
		{
			const auto* Model = Cast->Models.Find(Pair.Key);
			if (!Model) continue;
			FElysiumCinematicSet Set; Set.Stem = FElysiumContentPaths::SafeName(Pair.Key.Mid(11));
			for (const auto& Root : Pair.Value.Roots)
			{
				const FString Owner = Set.Stem + TEXT("__") + Root.Key.ToLower();
				Set.Roots.Add(Root.Key.ToLower(), Owner);
				if (!AddOwner(Owner, *Model)) { Out = {}; return false; }
			}
			Out.Cinematics.Add(Pair.Value.ModelPath.ToLower(), Set);
		}
		if (const auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr,
			TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels")))
			for (const auto& Pair : Placed->Data.Models)
			{
				const auto& Model = Pair.Value;
				FElysiumAnimatedPropEntry Row;
				Row.Stem = FElysiumContentPaths::SafeName(Pair.Key.Mid(11)); Row.Model = Model.ModelPath;
				Row.StaticStem = Row.Stem; Row.Eskm = Model.SkeletalMesh.ToString();
				Row.bStaticEquivalent = Model.bStaticEquivalent; Row.ClipMode = Model.bFullClipsRequired ? TEXT("full") : TEXT("rest");
				for (const auto& Clip : Model.Clips)
				{
					FElysiumPropClip C; C.Name = Clip.Label; C.Activity = Clip.Activity; C.Weight = Clip.Weight;
					C.Flags = Clip.Flags; C.Index = Clip.Index; C.Frames = Clip.Frames; C.Fps = Clip.Fps;
					C.BoundsRadiusMeters = Clip.BoundsRadiusCm / 100.f; Row.Clips.Add(C);
				}
				for (int32 Index : Model.RestCandidates) if (Row.Clips.IsValidIndex(Index)) Row.RestCandidates.Add(Row.Clips[Index].Name);
				Out.PlacedModels.Add(Row.Stem, Row);
				if (!Model.SkeletalMesh.IsNull()) Out.AnimatedProps.Add(Row.Stem, Row);
			}
		Error.Reset(); return true;
	}
}
