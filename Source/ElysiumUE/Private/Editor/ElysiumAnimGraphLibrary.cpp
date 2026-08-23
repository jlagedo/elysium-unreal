#include "ElysiumAnimGraphLibrary.h"

#if WITH_EDITOR

#include "AlphaBlend.h"
#include "Animation/AnimNodeBase.h"
#include "BlendStack/AnimNode_BlendStack.h"   // EBlendStack_BlendspaceUpdateMode
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Visual/ElysiumAnimGraph.h"

namespace
{
	// A pose pin, without depending on either editor graph module for its own helper: the pin's
	// subcategory object names one of the two pose links the RUNTIME declares, which identifies it
	// on its own — the struct category the schema also checks adds nothing a caller can act on.
	bool IsPosePin(const FEdGraphPinType& PinType)
	{
		const UObject* Sub = PinType.PinSubCategoryObject.Get();
		return Sub == FPoseLink::StaticStruct() || Sub == FComponentSpacePoseLink::StaticStruct();
	}

	UEdGraph* FindGraph(const UBlueprint* Blueprint, FName GraphName)
	{
		if (Blueprint == nullptr)
		{
			return nullptr;
		}
		// An Animation Blueprint's AnimGraph is a function graph; an event graph is an ubergraph
		// page. Both are searched so this serves either without the caller having to know which.
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph != nullptr && Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph != nullptr && Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	// The graph's terminal pin — the one the schema created with it and that nothing may replace.
	// `UAnimGraphNode_Root::CanUserDeleteNode` is false, so the output node always survives a clear
	// and the imported text must never carry one of its own.
	UEdGraphPin* FindRootPosePin(UEdGraph& Graph)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node == nullptr || Node->CanUserDeleteNode())
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr && Pin->Direction == EGPD_Input
					&& IsPosePin(Pin->PinType))
				{
					return Pin;
				}
			}
		}
		return nullptr;
	}

	// The pasted node that produces the finished pose: the one output pose pin nothing else took.
	// Pasted nodes can only link to each other, never to a node that was already in the graph, so
	// this is the seam where the imported text meets the schema's own output node.
	UEdGraphPin* FindTerminalPosePin(const TSet<UEdGraphNode*>& Imported, int32& OutCandidates)
	{
		UEdGraphPin* Found = nullptr;
		OutCandidates = 0;
		for (UEdGraphNode* Node : Imported)
		{
			if (Node == nullptr)
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() == 0
					&& IsPosePin(Pin->PinType))
				{
					++OutCandidates;
					Found = Pin;
				}
			}
		}
		return OutCandidates == 1 ? Found : nullptr;
	}
}

// ------------------------------------------------------------------------------------------------
// The bootstrap that produced the tracked graph text, and what regenerates it when an engine upgrade
// changes a node's layout.
//
// A node that owns a sub-graph cannot be hand-written as T3D with any confidence: the text has to
// carry that graph and its terminal nodes as nested objects, and the node's own `PostPasteNode` only
// fixes up a bound graph the text already carries. The blend stack is exactly such a node —
// `UAnimGraphNode_BlendStack_Base::PostPlacedNewNode` builds its per-sample graph — so the shape is
// built once here, through the engine's own construction path, and exported.
//
// It goes through reflection rather than through the editor AnimGraph module's types on purpose:
// every step is a virtual on `UEdGraphNode` that the concrete node overrides, so the whole thing
// links against the modules this runtime already carries and adds no editor dependency.
namespace ElysiumAnimGraphBootstrap
{
	UClass* NodeClass(const TCHAR* Path)
	{
		return FindObject<UClass>(nullptr, Path);
	}

	// Place a node the way the schema does: add it, give it identity, let it build whatever
	// sub-objects it owns, then let it make its pins.
	UEdGraphNode* Place(UEdGraph& Graph, const TCHAR* ClassPath, int32 X, int32 Y)
	{
		UClass* Class = NodeClass(ClassPath);
		if (Class == nullptr)
		{
			return nullptr;
		}
		UEdGraphNode* Node = NewObject<UEdGraphNode>(&Graph, Class, NAME_None, RF_Transactional);
		Graph.AddNode(Node, /*bUserAction=*/false, /*bSelectNewNode=*/false);
		Node->CreateNewGuid();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		// Virtual: this is what creates a node's own sub-graph, such as the blend stack's sample graph.
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	UEdGraphPin* FirstPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (Node != nullptr)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr && Pin->Direction == Direction)
				{
					return Pin;
				}
			}
		}
		return nullptr;
	}
}

FElysiumAnimGraphImportResult UElysiumAnimGraphLibrary::ImportGraphFromText(UBlueprint* Blueprint,
	FName GraphName, const FString& T3D)
{
	FElysiumAnimGraphImportResult Result;

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (Graph == nullptr)
	{
		Result.Errors.Add(FString::Printf(TEXT("'%s' has no graph named '%s'"),
			Blueprint != nullptr ? *Blueprint->GetName() : TEXT("<null>"), *GraphName.ToString()));
		return Result;
	}
	if (T3D.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(TEXT("'%s': the graph text is empty"), *GraphName.ToString()));
		return Result;
	}
	// Refused rather than half-pasted. A text/graph mismatch — anim nodes aimed at an event graph,
	// say — answers false here, and reporting it as an error is what distinguishes it from a paste
	// that legitimately produced nothing.
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, T3D))
	{
		Result.Errors.Add(FString::Printf(
			TEXT("'%s' refuses this text -- the nodes do not belong to this graph's schema"),
			*GraphName.ToString()));
		return Result;
	}

	// Clear whatever a previous import left, keeping the nodes the schema created with the graph.
	for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
	{
		UEdGraphNode* Node = Graph->Nodes[i];
		if (Node != nullptr && Node->CanUserDeleteNode())
		{
			Graph->RemoveNode(Node);
		}
	}

	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, T3D, Imported);
	Result.Nodes = Imported.Num();
	if (Imported.Num() == 0)
	{
		Result.Errors.Add(FString::Printf(TEXT("'%s': the text parsed but produced no nodes"),
			*GraphName.ToString()));
		return Result;
	}

	// Every pasted node needs its own identity, or a second import into the same package collides
	// with the first and the compiler resolves one of them away.
	for (UEdGraphNode* Node : Imported)
	{
		if (Node != nullptr)
		{
			Node->CreateNewGuid();
		}
	}

	// Wire the imported chain into the output node the schema owns.
	if (UEdGraphPin* RootPin = FindRootPosePin(*Graph))
	{
		int32 Candidates = 0;
		if (UEdGraphPin* Terminal = FindTerminalPosePin(Imported, Candidates))
		{
			RootPin->BreakAllPinLinks();
			Terminal->MakeLinkTo(RootPin);
		}
		else
		{
			Result.Errors.Add(FString::Printf(
				TEXT("'%s': expected exactly one unconnected output pose pin to feed the output node, ")
				TEXT("found %d -- the graph text must terminate in a single pose"),
				*GraphName.ToString(), Candidates));
		}
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("'%s' has no output node to connect to"),
			*GraphName.ToString()));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Result;
}

FString UElysiumAnimGraphLibrary::ExportGraphToText(const UBlueprint* Blueprint, FName GraphName)
{
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (Graph == nullptr)
	{
		return FString();
	}
	// The output node is excluded for the same reason the import never pastes one: it belongs to the
	// graph rather than to the text, so a round trip that carried it would paste a second one.
	TSet<UObject*> Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node != nullptr && Node->CanUserDeleteNode())
		{
			Nodes.Add(Node);
		}
	}
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Nodes, Text);
	return Text;
}

// ------------------------------------------------------------------------------------------------
// Reflection helpers for the bootstrap. Everything below sets a property the concrete editor node
// declares, without linking against the module that declares it.

namespace ElysiumAnimGraphBootstrap
{
	// A property inside the node's own `FAnimNode_*` struct member, which is where every runtime
	// setting on an animation node lives (`Node.SlotName`, `Node.bAlwaysUpdateSourcePose`).
	void* StructMember(UEdGraphNode* Node, const TCHAR* StructName, const TCHAR* Member,
		FProperty*& OutProperty)
	{
		OutProperty = nullptr;
		FStructProperty* Outer = CastField<FStructProperty>(
			Node->GetClass()->FindPropertyByName(FName(StructName)));
		if (Outer == nullptr)
		{
			return nullptr;
		}
		void* Container = Outer->ContainerPtrToValuePtr<void>(Node);
		OutProperty = Outer->Struct->FindPropertyByName(FName(Member));
		return OutProperty != nullptr ? Container : nullptr;
	}

	template <typename T>
	bool SetNodeValue(UEdGraphNode* Node, const TCHAR* Member, const T& Value)
	{
		FProperty* Prop = nullptr;
		void* Container = StructMember(Node, TEXT("Node"), Member, Prop);
		if (Container == nullptr)
		{
			return false;
		}
		*Prop->ContainerPtrToValuePtr<T>(Container) = Value;
		return true;
	}

	bool SetNodeBool(UEdGraphNode* Node, const TCHAR* Member, bool bValue)
	{
		FProperty* Prop = nullptr;
		void* Container = StructMember(Node, TEXT("Node"), Member, Prop);
		FBoolProperty* Bool = CastField<FBoolProperty>(Prop);
		if (Container == nullptr || Bool == nullptr)
		{
			return false;
		}
		Bool->SetPropertyValue_InContainer(Container, bValue);
		return true;
	}

	// Resize an array member inside the node's own `FAnimNode_*` struct — CCC10's
	// `FAnimNode_LayeredBoneBlend::BlendMasks`/`LayerSetup`, which a raw `BlendMode` write does not
	// resize on its own. Mirrors what the editor's `PostEditChangeProperty` handler does
	// (`SyncBlendMasksAndLayers`: `BlendMasks.SetNum(BlendPoses.Num()); LayerSetup.Reset();`), which
	// a reflection-only write never triggers.
	bool ResizeNodeArray(UEdGraphNode* Node, const TCHAR* Member, int32 Num)
	{
		FProperty* Prop = nullptr;
		void* Container = StructMember(Node, TEXT("Node"), Member, Prop);
		FArrayProperty* Array = CastField<FArrayProperty>(Prop);
		if (Container == nullptr || Array == nullptr)
		{
			return false;
		}
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Container));
		Helper.Resize(Num);
		return true;
	}

	// A plain property on the node itself rather than inside its runtime struct — `Tag` is declared
	// on `UAnimGraphNode_Base` and is what the compiled tag table is keyed by.
	template <typename T>
	bool SetOwnValue(UEdGraphNode* Node, const TCHAR* Member, const T& Value)
	{
		FProperty* Prop = Node->GetClass()->FindPropertyByName(FName(Member));
		if (Prop == nullptr)
		{
			return false;
		}
		*Prop->ContainerPtrToValuePtr<T>(Node) = Value;
		return true;
	}

	// Turn a hidden node setting into a real input pin, so the native instance can drive it. This is
	// what makes the graph parameterized: no node holds an asset, every asset arrives on a pin.
	bool ExposePin(UEdGraphNode* Node, const TCHAR* PropertyName)
	{
		FArrayProperty* Array = CastField<FArrayProperty>(
			Node->GetClass()->FindPropertyByName(TEXT("ShowPinForProperties")));
		FStructProperty* Element = Array != nullptr
			? CastField<FStructProperty>(Array->Inner) : nullptr;
		if (Element == nullptr)
		{
			return false;
		}
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Node));
		FNameProperty* NameProp = CastField<FNameProperty>(
			Element->Struct->FindPropertyByName(TEXT("PropertyName")));
		FBoolProperty* ShowProp = CastField<FBoolProperty>(
			Element->Struct->FindPropertyByName(TEXT("bShowPin")));
		if (NameProp == nullptr || ShowProp == nullptr)
		{
			return false;
		}
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			void* Entry = Helper.GetRawPtr(i);
			if (NameProp->GetPropertyValue_InContainer(Entry) == FName(PropertyName))
			{
				ShowProp->SetPropertyValue_InContainer(Entry, true);
				// The pin only exists after the node rebuilds itself around the new setting.
				Node->ReconstructNode();
				return true;
			}
		}
		return false;
	}

	UEdGraphPin* PinNamed(UEdGraphNode* Node, const TCHAR* Name)
	{
		if (Node != nullptr)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr && Pin->PinName == FName(Name))
				{
					return Pin;
				}
			}
		}
		return nullptr;
	}

	// The literal an unconnected exposed pin carries into the compiled node. The pin — not the
	// property behind it — is what the compiler folds for a shown pin, so a value meant to survive
	// compilation is written here; returns false when the pin does not exist, so a misspelled name
	// refuses the export like a dead wire does.
	bool SetPinDefault(UEdGraphNode* Node, const TCHAR* PinName, const TCHAR* Value)
	{
		UEdGraphPin* Pin = PinNamed(Node, PinName);
		if (Pin == nullptr)
		{
			return false;
		}
		Pin->DefaultValue = Value;
		return true;
	}

	// Returns whether the wire was actually made. Both pins are looked up by name, and a name that
	// does not exist yields null — so an unchecked call is a dead wire that builds, compiles and
	// exports clean, and shows up only as a reference pose on a live body. Every caller checks.
	bool Link(UEdGraphPin* From, UEdGraphPin* To)
	{
		if (From == nullptr || To == nullptr)
		{
			return false;
		}
		From->MakeLinkTo(To);
		return true;
	}

	// One `Get <variable>` feeding one pin. Every driven input in this graph is exactly this — the
	// base channel's four, every overlay asset, every blend time — so the graph never computes
	// anything: the decision happened in C++ where it is asserted.
	bool DriveFromBool(UEdGraph& Graph, UEdGraphPin* Target, const TCHAR* VariableName, int32 X, int32 Y)
	{
		if (Target == nullptr)
		{
			return false;
		}
		UEdGraphNode* Get = Place(Graph, TEXT("/Script/BlueprintGraph.K2Node_VariableGet"), X, Y);
		FStructProperty* Ref = Get != nullptr ? CastField<FStructProperty>(
			Get->GetClass()->FindPropertyByName(TEXT("VariableReference"))) : nullptr;
		if (Ref == nullptr)
		{
			return false;
		}
		void* Container = Ref->ContainerPtrToValuePtr<void>(Get);
		FNameProperty* MemberName = CastField<FNameProperty>(
			Ref->Struct->FindPropertyByName(TEXT("MemberName")));
		FBoolProperty* SelfContext = CastField<FBoolProperty>(
			Ref->Struct->FindPropertyByName(TEXT("bSelfContext")));
		if (MemberName == nullptr || SelfContext == nullptr)
		{
			return false;
		}
		MemberName->SetPropertyValue_InContainer(Container, FName(VariableName));
		// Load-bearing: without it the reference resolves against a null member parent rather than
		// against this blueprint, every variable reads as missing, and each rule silently loses its
		// pin while the build still reports success.
		SelfContext->SetPropertyValue_InContainer(Container, true);

		// Rebuild so the output pin takes the variable's own name and type.
		Get->ReconstructNode();
		UEdGraphPin* Source = PinNamed(Get, VariableName);
		Link(Source, Target);
		return Source != nullptr;
	}
}

// `elysium.animbp.build <blueprint> <out-file>` — build the player locomotion graph and export it
// as T3D. This is the bootstrap the tracked graph text comes from; nothing runs it at play time.
static FAutoConsoleCommand GElysiumAnimBpBuild(
	TEXT("elysium.animbp.build"),
	TEXT("elysium.animbp.build <blueprint-object-path> <out-file> -- build the player locomotion "
	     "graph and export it as T3D."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		using namespace ElysiumAnimGraphBootstrap;

		if (Args.Num() < 2)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] usage: elysium.animbp.build <blueprint> <out-file>"));
			return;
		}
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Args[0]);
		UEdGraph* Graph = FindGraph(Blueprint, FName(TEXT("AnimGraph")));
		if (Graph == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] no AnimGraph on the named blueprint"));
			return;
		}
		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
		{
			if (Graph->Nodes[i] != nullptr && Graph->Nodes[i]->CanUserDeleteNode())
			{
				Graph->RemoveNode(Graph->Nodes[i]);
			}
		}
		// Bring the skeleton class up to the native parent's current properties before any variable
		// is referenced. Without it every `Get <bool>` resolves nothing and every rule loses its pin.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		// Every wire is checked and a single miss refuses the export. A pin looked up by a name the
		// node does not have is null, both `Link` and `DriveFromBool` no-op on null, and the graph
		// still compiles — an unwired asset pin evaluates to the reference pose, so the failure
		// reaches the owner as a T-posed body rather than as an error. Nothing unwired becomes
		// tracked text.
		int32 Failures = 0;
		auto Wire = [&Failures](bool bLinked, const TCHAR* What)
		{
			if (!bLinked)
			{
				++Failures;
				UE_LOG(LogTemp, Error, TEXT("[animbp] dead wire: %s"), What);
			}
		};

		// --- the spine ---------------------------------------------------------------------------
		//
		// The blend stack owns the body and one-shots layer over it on a slot. **There is no
		// inertialization node**, and that is a decision rather than an omission: every crossfade this
		// graph performs already belongs to a node that owns it -- the stack cross-fades its own
		// players on the authored `BlendTime`, the reaction branch takes its own two per-pose times,
		// and the slot's dynamic montage carries its own blend pair. An inertializer between them
		// serves requests nobody raises: nothing in this graph is an `IInertializationRequester`, so
		// the node was placed and unreached. The composition tail runs after the output pose, in the
		// proxy, because axis interpolation is a rig rule over the finished pose.
		//
		// **The stack IS the locomotion transitioner** (S2), which is why nothing below places a
		// state, a conduit or a transition rule. It carries no vocabulary and no edges: the
		// resolver's answer arrives whole on four pins — the asset, its loop bit, the authored fade
		// and the grid's steering pair — and `FAnimNode_BlendStack` pushes one player per request
		// and cross-fades the ones already standing on it. That is retail's own transitioner: a
		// selection names a sequence, the previous one keeps playing while it fades out, and the
		// fade duration is the authored value rather than an edge property.
		UEdGraphNode* Stack = Place(*Graph,
			TEXT("/Script/BlendStackEditor.AnimGraphNode_BlendStack"), -900, 0);
		UEdGraphNode* Slot = Place(*Graph, TEXT("/Script/AnimGraph.AnimGraphNode_Slot"), -560, 0);
		if (Stack == nullptr || Slot == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] a spine node class was not found"));
			return;
		}

		// --- the stack's own settings ---------------------------------------------------------------
		//
		// Every one of these is a HIDDEN property rather than a shown pin, so the value on the node
		// is what the compiler bakes — `SetPinDefault` above would write a pin that does not exist.
		// Each write is `Wire`-checked for the same reason every link is: a property an engine
		// upgrade renamed answers false, and the export is refused rather than shipping a node
		// silently running an engine default.
		{
			// Retail's transition curve, recorded at `0x10225158` as the constant the blend's alpha
			// is shaped by (`docs/vtmb/animation_and_movers.md`): `3t^2 - 2t^3`, which is exactly
			// `EAlphaBlendOption::HermiteCubic`. It equals the engine's own default, so it does not
			// appear in the exported text at all — the compiled-asset test is the only proof it is
			// what the node carries, and it is load-bearing because `UAnimGraphNode_BlendStack`'s
			// own `Serialize` downgrades this property to `Linear` on an old custom version.
			Wire(SetNodeValue<uint8>(Stack, TEXT("BlendOption"),
				static_cast<uint8>(EAlphaBlendOption::HermiteCubic)), TEXT("stack BlendOption"));
			// The requested start time, and the default is a trap: `-1` is not "wherever the clip
			// is", it is the value `MaxAnimationDeltaTime` would be compared against and the time a
			// new player is seeded at. Every clip this graph plays starts at its head.
			Wire(SetNodeValue<float>(Stack, TEXT("AnimationTime"), 0.f), TEXT("stack AnimationTime"));
			// **A named divergence.** Retail caps concurrent transitions at NOTHING: the append path
			// is `EnsureCapacity(1)` plus `InsertMultiple` with no bound check, and a record leaves
			// only when its own weight reaches zero (`docs/vtmb/animation_and_movers.md`). Four is
			// the deepest stack the capture ever observed (`0 -> 1 -> 2 -> 3 -> 4`), not a rule the
			// engine enforces. This node needs a number, so it takes the observed maximum — and
			// `bStoreBlendedPose` below is what keeps the divergence from being a dropped clip: a
			// fifth request accumulates the overflow into a stored pose instead of discarding a
			// player mid-fade, which would be a pop retail never produces.
			Wire(SetNodeValue<int32>(Stack, TEXT("MaxActiveBlends"), 4), TEXT("stack MaxActiveBlends"));
			Wire(SetNodeBool(Stack, TEXT("bStoreBlendedPose"), true), TEXT("stack bStoreBlendedPose"));
			// A new request never takes the most recent player's slot: retail's transitioner adds
			// the incoming clip and leaves the outgoing one fading, so overwriting a player that is
			// still blending in would drop a clip retail keeps.
			Wire(SetNodeValue<float>(Stack, TEXT("MaxBlendInTimeToOverrideAnimation"), 0.f),
				TEXT("stack MaxBlendInTimeToOverrideAnimation"));
			// Deeper players fade at their own stated rate rather than accelerated, so a nested
			// transition takes the duration `TransitionSeconds` answered for it.
			Wire(SetNodeValue<float>(Stack, TEXT("PlayerDepthBlendInTimeMultiplier"), 1.f),
				TEXT("stack PlayerDepthBlendInTimeMultiplier"));
			// The stack cross-fades its own players. True would make it RAISE an inertialization
			// request instead of blending, and this graph carries no node that answers one -- the
			// request would be logged as unserviced and the authored duration on the pin would decide
			// nothing.
			Wire(SetNodeBool(Stack, TEXT("bUseInertialBlend"), false), TEXT("stack bUseInertialBlend"));
			// **The most dangerous default in the node.** `InitialOnly` samples a blend space's xy
			// once, at `BlendTo`, and never again — so a gait fan would freeze at the steering value
			// it was entered with and `move_yaw` would stop turning the body.
			Wire(SetNodeValue<uint8>(Stack, TEXT("BlendspaceUpdateMode"),
				static_cast<uint8>(EBlendStack_BlendspaceUpdateMode::UpdateActiveOnly)),
				TEXT("stack BlendspaceUpdateMode"));
			// The other dangerous default, and it is 0. `ConditionalBlendTo` re-blends whenever the
			// requested parameters differ from the playing player's by more than this — and a
			// SEQUENCE player answers `GetBlendParameters()` with the zero vector, so a body walking
			// with a non-zero `move_yaw` on a plain clip would push a new player every single frame.
			// A threshold no steering value can reach is what turns the comparison off; the fan's
			// own steering is applied by `UpdateBlendspaceParameters` above instead.
			Wire(SetNodeValue<float>(Stack, TEXT("BlendParametersDeltaThreshold"), 1.0e6f),
				TEXT("stack BlendParametersDeltaThreshold"));
			// **False, against the engine's `true`.** The reaction branch makes this node
			// non-relevant while a full-weight flinch stands — `FAnimNode_BlendListBase` skips a
			// child under `ZERO_ANIMWEIGHT_THRESH` — so the true default would `Reset()` the stack
			// on the frame the flinch releases and restart the gait at frame 0 on every hit. False
			// keeps the frozen clip, which is what a body standing mid-gait actually looks like.
			Wire(SetNodeBool(Stack, TEXT("bResetOnBecomingRelevant"), false),
				TEXT("stack bResetOnBecomingRelevant"));
			// `MaxAnimationDeltaTime` stays at its `-1` (the desync re-blend is off, because nothing
			// drives `AnimationTime`), and the notify filter, the mirror table, the blend profile and
			// the experimental stitch fields stay at their own defaults. The sync-group fields are
			// **not written**: they are `WITH_EDITORONLY_DATA` `FoldProperty` members, so a
			// reflection write there is invisible to the runtime and breaks compile validation —
			// and `DoNotSync` is already the answer, because two clips of different lengths must not
			// be forced onto a shared normalized time.
			Wire(SetOwnValue<FName>(Stack, TEXT("Tag"),
				FName(ElysiumAnimGraph::LocomotionStackTag)), TEXT("stack tag"));
		}

		// --- the four pins the whole base channel arrives on ----------------------------------------
		//
		// Same rule as every other node in this graph: it holds no asset and decides nothing. The
		// resolver answered, the native instance projected the answer, and these carry it.
		ExposePin(Stack, TEXT("AnimationAsset"));
		Wire(DriveFromBool(*Graph, PinNamed(Stack, TEXT("AnimationAsset")),
			TEXT("RequestedAsset"), -1200, -120), TEXT("stack AnimationAsset pin"));
		ExposePin(Stack, TEXT("bLoop"));
		Wire(DriveFromBool(*Graph, PinNamed(Stack, TEXT("bLoop")),
			TEXT("bRequestedLooping"), -1200, -40), TEXT("stack bLoop pin"));
		// The authored fade, whole and unclamped. Nothing in the graph caps it, which is the point:
		// `ElysiumAnimGraph::TransitionSeconds` is the only authority on how long a transition takes.
		ExposePin(Stack, TEXT("BlendTime"));
		Wire(DriveFromBool(*Graph, PinNamed(Stack, TEXT("BlendTime")),
			TEXT("RequestedBlendSeconds"), -1200, 40), TEXT("stack BlendTime pin"));
		// The grid's steering pair as one vector, because that is the shape the node takes it in.
		// Ignored outright when the asset is a sequence.
		ExposePin(Stack, TEXT("BlendParameters"));
		Wire(DriveFromBool(*Graph, PinNamed(Stack, TEXT("BlendParameters")),
			TEXT("RequestedBlendParameters"), -1200, 120), TEXT("stack BlendParameters pin"));

		// `DefaultSlot` belongs to `DefaultGroup` on every skeleton by construction, so no bake has
		// to register a slot for the one-shot seam to land.
		SetNodeValue<FName>(Slot, TEXT("SlotName"), FName(TEXT("DefaultSlot")));
		// Without this a one-shot freezes the gait underneath it and the return to walking pops.
		SetNodeBool(Slot, TEXT("bAlwaysUpdateSourcePose"), true);

		Wire(Link(FirstPin(Stack, EGPD_Output), FirstPin(Slot, EGPD_Input)),
			TEXT("blend stack -> slot"));

		// --- the reaction branch (LIFE5) -----------------------------------------------------------
		//
		// A reaction REPLACES the locomotion pose rather than riding over it, so it sits on the base
		// channel between the one-shot slot and the upper-body layer: `bReactionActive` picks either
		// the reaction pose or everything the blend stack and the one-shot slot produced.
		//
		// The two per-pose blend times ARE the asymmetric fade. `FAnimNode_BlendListBase` takes the
		// NEWLY ACTIVE child's own time, so entering the reaction uses the in-fade and returning to
		// the base uses the out-fade, from one node and with no second blend to keep in step.
		//
		// Nothing publishes to it yet: `bReactionActive` defaults false, the branch holds full weight
		// on the base pose from its first update, and the reaction half is never evaluated.
		UEdGraphNode* ReactionBlend = Place(*Graph,
			TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"), 140, -140);
		{
			// The reaction's own pose source, the same "exactly one of these two" shape every other
			// channel in this graph takes: a directional hit fan (steered by `hit_yaw`) or a single
			// reaction clip. The resolver decides which; the graph only composes it.
			UEdGraphNode* ReactionSpace = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendSpacePlayer"), -160, -380);
			UEdGraphNode* ReactionSequence = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"), -160, -240);
			UEdGraphNode* ReactionPick = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"), -20, -300);
			if (ReactionSpace == nullptr || ReactionSequence == nullptr || ReactionPick == nullptr
				|| ReactionBlend == nullptr)
			{
				UE_LOG(LogTemp, Error, TEXT("[animbp] a reaction branch node class was not found"));
				return;
			}

			// **Whether the reaction repeats is a PIN on both players, not a folded constant** (LIFE5).
			// A struck reaction ends and must not loop — a looping flinch never reports complete and
			// would hold the body in its reaction forever — but a HELD one stands for exactly as long
			// as the predicate that asked for it, and a pose that does not repeat freezes on its
			// terminal frame instead. That is the same rule `ElysiumAnimGraph::ShouldRepeatClip`
			// states for a held stance, and it is a property of the PLAY rather than of the channel.
			//
			// Both bits are edit-time node state the compiler folds, so a branch built non-looping can
			// never be made to repeat at runtime: exposing them is the only thing that lets a held FAN
			// play here at all, where a montage cannot go — a montage plays one sequence, and a fan is
			// a blend of two cells. `bReactionLoops` is false unless the play is held, so every struck
			// reaction still runs once and reports complete.
			ExposePin(ReactionSpace, TEXT("bLoop"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionSpace, TEXT("bLoop")),
				TEXT("bReactionLoops"), -460, -460), TEXT("reaction blend space bLoop pin"));
			ExposePin(ReactionSequence, TEXT("bLoopAnimation"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionSequence, TEXT("bLoopAnimation")),
				TEXT("bReactionLoops"), -460, -300), TEXT("reaction sequence bLoopAnimation pin"));

			ExposePin(ReactionSpace, TEXT("BlendSpace"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionSpace, TEXT("BlendSpace")),
				TEXT("RequestedReactionBlendSpace"), -460, -420), TEXT("reaction BlendSpace pin"));
			// One axis only. Every hit fan VtMB ships is 9x1, so a second axis would be a steering
			// value with nothing behind it; a two-axis reaction grid is refused by name elsewhere
			// rather than half-steered here.
			Wire(DriveFromBool(*Graph, PinNamed(ReactionSpace, TEXT("X")), TEXT("ReactionAxis0"),
				-460, -360), TEXT("reaction X (ReactionAxis0) pin"));

			ExposePin(ReactionSequence, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionSequence, TEXT("Sequence")),
				TEXT("RequestedReactionSequence"), -460, -240), TEXT("reaction Sequence pin"));

			// Index 0 is the TRUE branch, the convention `UAnimGraphNode_BlendListByBool` sets and
			// every other pick in this graph follows.
			Wire(DriveFromBool(*Graph, PinNamed(ReactionPick, TEXT("bActiveValue")),
				TEXT("bReactionHasBlendSpace"), -460, -160), TEXT("reaction pick bActiveValue"));
			Wire(Link(FirstPin(ReactionSpace, EGPD_Output), PinNamed(ReactionPick, TEXT("BlendPose_0"))),
				TEXT("reaction blend space -> true pose"));
			Wire(Link(FirstPin(ReactionSequence, EGPD_Output),
				PinNamed(ReactionPick, TEXT("BlendPose_1"))), TEXT("reaction sequence -> false pose"));
			// Both halves are the SAME reaction, chosen once by what the resolver answered with — a
			// fade between them would be a fade between two answers to one question. The pin default
			// is what the compiler folds, so it is written on the pin rather than on the node.
			Wire(SetPinDefault(ReactionPick, TEXT("BlendTime_0"), TEXT("0.000000")),
				TEXT("reaction pick BlendTime_0 default"));
			Wire(SetPinDefault(ReactionPick, TEXT("BlendTime_1"), TEXT("0.000000")),
				TEXT("reaction pick BlendTime_1 default"));

			Wire(DriveFromBool(*Graph, PinNamed(ReactionBlend, TEXT("bActiveValue")),
				TEXT("bReactionActive"), -20, -60), TEXT("reaction blend bActiveValue"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionBlend, TEXT("BlendTime_0")),
				TEXT("ReactionBlendInSeconds"), -20, 0), TEXT("reaction blend in time pin"));
			Wire(DriveFromBool(*Graph, PinNamed(ReactionBlend, TEXT("BlendTime_1")),
				TEXT("ReactionBlendOutSeconds"), -20, 60), TEXT("reaction blend out time pin"));
			Wire(Link(FirstPin(ReactionPick, EGPD_Output),
				PinNamed(ReactionBlend, TEXT("BlendPose_0"))), TEXT("reaction pick -> true pose"));
			Wire(Link(FirstPin(Slot, EGPD_Output), PinNamed(ReactionBlend, TEXT("BlendPose_1"))),
				TEXT("slot -> reaction false pose"));

			// `ChildUpateMode` stays at the engine's `Default`. Its `ResetChildOnActivate` alternative
			// looks like the retrigger fix and is not one: `FAnimNode_BlendListBase` reinitializes a
			// newly-active child only when that child's weight is still <= `ZERO_ANIMWEIGHT_THRESH`, so
			// a reaction that fires again while its own fan is still fading is never covered — and when
			// a full-weight reaction ends, the reset lands on the BASE child instead, re-initializing
			// the whole base spine at elapsed zero: the blend stack starts its gait again from frame 0
			// and the slot's bookkeeping is reset. That is a
			// hard cut on release, which is the opposite of what the out-fade exists for. The restart a
			// retrigger needs comes from republishing the asset: a sequence player whose `Sequence` pin
			// changes restarts, and so does the blend space player on a new `BlendSpace`.
			//
			// **It is one half of a pair, and the other half is on the stack.** The same
			// `ZERO_ANIMWEIGHT_THRESH` skip that makes the base child un-updated also makes the blend
			// stack NON-RELEVANT for the duration of a full-weight flinch, and
			// `FAnimNode_BlendStack::NeedsReset` would then reset it on the frame it becomes relevant
			// again. `bResetOnBecomingRelevant = false` above is what refuses that; together the two
			// are what stops a flinch release restarting the gait.
			//
			// The transition type and the blend curve stay at the engine's own defaults on purpose:
			// choosing a curve here would pre-empt the separate sequence-blend-fidelity call, which
			// owns what every fade in this graph is shaped like. The transition type in particular must
			// stay `StandardBlend` — this graph carries no inertialization node at all, so an
			// `Inertialization` type here would search its ancestors for an `IInertializationRequester`,
			// find none, and log a request failure every time the reaction fires. The branch's own two
			// per-pose blend times ARE its fade, and they need no requester.
			Wire(SetOwnValue<FName>(ReactionBlend, TEXT("Tag"),
				FName(ElysiumAnimGraph::ReactionBranchTag)), TEXT("reaction blend tag"));
		}

		// --- the upper-body layer (CCC10) ----------------------------------------------------------
		//
		// Sits after the reaction branch, outside the base channel: a weapon layer rides beside the
		// locomotion state rather than through it, so it does not wait on a gait transition or
		// interrupt one. Both riders are grip-agnostic and asset-driven, matching every other node
		// in this graph — the resolver decides what plays, the graph only composes it.
		{
			// The layer's own pose source: an aim grid (BlendSpacePlayer, steered by AimYaw/AimPitch)
			// or a melee overlay (a plain sequence) — never both, exactly the base channel's own
			// "exactly one of these two" shape, picked the same way a gait state picks between its
			// fan and its single-cell fallback.
			UEdGraphNode* UpperBodySpace = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendSpacePlayer"), -160, 260);
			UEdGraphNode* UpperBodySequence = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"), -160, 420);
			UEdGraphNode* UpperBodyPick = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"), 40, 340);
			UEdGraphNode* Layer = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend"), 280, 0);
			UEdGraphNode* AdditiveSequence = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"), 280, 460);
			UEdGraphNode* Additive = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_ApplyAdditive"), 780, 0);
			// The overlay SLOT: retail's `CBaseAnimatingOverlay` slot 0, composed AFTER the host's own
			// model-declared autolayers and therefore over a second masked blend rather than through
			// the first one. Its pose comes off an evaluator pinned to an explicit time.
			UEdGraphNode* SlotEvaluator = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequenceEvaluator"), 280, 260);
			// **The slot clip's own declared layers — retail's autolayer rule, applied recursively to
			// the sequence in the overlay slot.** The slot branch is the same trio the base already
			// has: the shot motion (the evaluator above), its declared aim grid composed OVER it
			// through a masked blend of its own, and its declared `_delta` composed additively after.
			// A grid states its time as a FRACTION rather than in seconds, which is why the grid gets
			// an evaluator of its own rather than the sequence evaluator with an asset swapped in.
			UEdGraphNode* SlotGrid = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendSpaceEvaluator"), 280, 560);
			UEdGraphNode* SlotAimBlend = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend"), 400, 300);
			UEdGraphNode* SlotAdditiveEval = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequenceEvaluator"), 400, 620);
			UEdGraphNode* SlotAdditive = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_ApplyAdditive"), 460, 220);
			UEdGraphNode* SlotLayer = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend"), 520, 0);
			if (UpperBodySpace == nullptr || UpperBodySequence == nullptr || UpperBodyPick == nullptr
				|| Layer == nullptr || AdditiveSequence == nullptr || Additive == nullptr
				|| SlotEvaluator == nullptr || SlotGrid == nullptr || SlotAimBlend == nullptr
				|| SlotAdditiveEval == nullptr || SlotAdditive == nullptr || SlotLayer == nullptr)
			{
				UE_LOG(LogTemp, Error, TEXT("[animbp] a CCC10 layer node class was not found"));
				return;
			}

			ExposePin(UpperBodySpace, TEXT("BlendSpace"));
			Wire(DriveFromBool(*Graph, PinNamed(UpperBodySpace, TEXT("BlendSpace")),
				TEXT("RequestedUpperBodyBlendSpace"), -560, 220), TEXT("layer BlendSpace pin"));
			Wire(DriveFromBool(*Graph, PinNamed(UpperBodySpace, TEXT("X")), TEXT("AimYaw"), -560, 280),
				TEXT("layer X (AimYaw) pin"));
			Wire(DriveFromBool(*Graph, PinNamed(UpperBodySpace, TEXT("Y")), TEXT("AimPitch"), -560, 340),
				TEXT("layer Y (AimPitch) pin"));

			ExposePin(UpperBodySequence, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(UpperBodySequence, TEXT("Sequence")),
				TEXT("RequestedUpperBodySequence"), -560, 420), TEXT("layer melee Sequence pin"));

			Wire(DriveFromBool(*Graph, PinNamed(UpperBodyPick, TEXT("bActiveValue")),
				TEXT("bUpperBodyHasBlendSpace"), -160, 100), TEXT("layer bActiveValue"));
			// Index 0 is the TRUE branch, same convention the gait states use.
			Wire(Link(FirstPin(UpperBodySpace, EGPD_Output), PinNamed(UpperBodyPick, TEXT("BlendPose_0"))),
				TEXT("layer blend space -> true pose"));
			Wire(Link(FirstPin(UpperBodySequence, EGPD_Output), PinNamed(UpperBodyPick, TEXT("BlendPose_1"))),
				TEXT("layer sequence -> false pose"));

			// The mask lives on the blend node, not on a dedicated aim node — the roadmap's own point.
			// BlendMask mode over a single layer: one base pose, one masked rider, the mask read off
			// whichever clip the resolver selected rather than guessed from a weapon's grip.
			SetNodeValue<uint8>(Layer, TEXT("BlendMode"), 1);   // ELayeredBoneBlendMode::BlendMask
			// `BlendMode` is a plain write here rather than an editor property change, so the resize
			// the editor's own handler would do (`SyncBlendMasksAndLayers`) has to be done by hand:
			// one mask slot per blend pose, and no branch-filter setup, which is what BlendMask mode
			// means.
			ResizeNodeArray(Layer, TEXT("BlendMasks"), 1);
			ResizeNodeArray(Layer, TEXT("LayerSetup"), 0);
			// **The mask slot is left NULL, and stays null in the tracked text.** It is not a pin —
			// `BlendMasks` is edit-time state on the node — so the mask is written at runtime by
			// `UElysiumBipedAnimInstance::ApplyUpperBodyMask` through the tag below, the same door
			// Epic's own `ULayeredBoneBlendLibrary::SetBlendMask` uses. A null mask is legal here
			// precisely because this is a TEMPLATE Animation Blueprint, which the engine's own
			// `ValidateAnimNodeDuringCompilation` exempts from its null-mask error — and it is what
			// keeps a generated, game-derived profile asset out of the tracked graph.
			// **Mesh space, exactly as the slot's blends are, and for the same bone.** The layer's
			// split-bone rotation is stated against the BIND chain by the bake, so blending it in
			// mesh space writes the torso's model-space orientation absolutely — retail's own rule
			// for the flag. Blended locally the correction is only true at the host frame it was
			// computed against, and a layer over a moving gait sways with every stride: the whole
			// upper assembly, weapon at the end of the lever arm, shakes with the run.
			Wire(SetNodeBool(Layer, TEXT("bMeshSpaceRotationBlend"), true),
				TEXT("layer bMeshSpaceRotationBlend"));
			SetOwnValue<FName>(Layer, TEXT("Tag"), FName(ElysiumAnimGraph::UpperBodyLayerTag));

			Wire(Link(FirstPin(ReactionBlend, EGPD_Output), PinNamed(Layer, TEXT("BasePose"))),
				TEXT("reaction blend -> layer base pose"));
			Wire(Link(FirstPin(UpperBodyPick, EGPD_Output), PinNamed(Layer, TEXT("BlendPoses_0"))),
				TEXT("layer pick -> layer blend pose 0"));
			Wire(DriveFromBool(*Graph, PinNamed(Layer, TEXT("BlendWeights_0")),
				TEXT("UpperBodyLayerWeight"), 40, -100), TEXT("layer weight pin"));

			// --- the overlay slot, composed over everything above it ---------------------------------
			//
			// **A SECOND masked blend, and the order is retail's.** `CBaseAnimatingOverlay` accumulates
			// its slots after the host sequence's own autolayer bindings, so what the slot composes over
			// is the pose the node above already produced. Folding both into one blend would give a
			// body's carry-pose layer and its fire layer one weight and one mask between them, and only
			// one of the two could ever be on screen.
			//
			// The layer is posed by an EVALUATOR rather than a player. Retail's slot cycle is explicit
			// — the layer's phase is written each frame from its own age — and the same explicitness is
			// what this needs mechanically: a player keeps its own clock and would drift from the claim
			// that expires the layer, a re-fire of the same clip could not restart it, and it publishes
			// no phase to read back. `bTeleportToExplicitTime` stays at the node's own `true`, which is
			// what makes the time pin a pose lookup rather than an advance: no notifies fire off the
			// layer and no root motion is extracted from it, both correct for a partial-body overlay.
			// `ReinitializationBehavior` stays at `ExplicitTime` for the same reason — a slot that
			// became relevant again must resume at the phase its claim states, not at zero.
			//
			// Every setting on this node is a `WITH_EDITORONLY_DATA` `FoldProperty`, so a reflection
			// write lands somewhere the runtime cannot see; the pin is the only honest door, which is
			// why both inputs are exposed and driven rather than set.
			ExposePin(SlotEvaluator, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotEvaluator, TEXT("Sequence")),
				TEXT("RequestedSlotSequence"), 40, 200), TEXT("slot Sequence pin"));
			// `ExplicitTime` is shown by default, so it needs no `ExposePin` — only a driver.
			Wire(DriveFromBool(*Graph, PinNamed(SlotEvaluator, TEXT("ExplicitTime")),
				TEXT("SlotExplicitTime"), 40, 260), TEXT("slot ExplicitTime pin"));

			// Same node shape and the same null mask as the autolayer blend above, for the same two
			// reasons: `BlendMasks` is edit-time state with no pin, so the mask is written at runtime
			// through the tag by `UElysiumBipedAnimInstance::ApplySlotMask`; and a null mask is legal
			// only because this is a TEMPLATE Animation Blueprint, which is what keeps a generated,
			// game-derived profile asset out of the tracked graph text.
			SetNodeValue<uint8>(SlotLayer, TEXT("BlendMode"), 1);   // ELayeredBoneBlendMode::BlendMask
			ResizeNodeArray(SlotLayer, TEXT("BlendMasks"), 1);
			ResizeNodeArray(SlotLayer, TEXT("LayerSetup"), 0);
			// **Mesh space, and it is what makes a slot layer standable at all.** VtMB flags
			// `Bip01 Spine1` with `SPLIT_ROTATION`: its animated rotation is the bone's MODEL-SPACE
			// orientation, absolute and independent of the chain below it, which is what lets one
			// attack layer compose over any gait. A slot clip is masked, so the chain it would be
			// normalised against is not in it and no host is named to borrow one from; the bake
			// therefore states the bone against the bind chain and ships that chain with the clip,
			// leaving the clip's own forward kinematics to restate the absolute orientation. Blending
			// that in mesh space is what writes it onto the composed pose unchanged. Blended locally
			// it would take the host's spine rotation on top and fold the upper body about the waist.
			//
			// It is set on THIS node alone. The autolayer blend below the slot stays local, because
			// its layers are emitted per declaring host and carry an ordinary local pose resolved
			// against that host's own animated chain.
			//
			// The mask decides how far this reaches, and it reaches exactly one bone: `Bip01 Spine1`
			// is the only bone the layer masks own whose PARENT they do not, so it is the only bone
			// whose local transform the mesh-space round trip can change. Every other owned bone sits
			// under a parent that is owned too and rebuilds identically.
			Wire(SetNodeBool(SlotLayer, TEXT("bMeshSpaceRotationBlend"), true),
				TEXT("slot bMeshSpaceRotationBlend"));
			SetOwnValue<FName>(SlotLayer, TEXT("Tag"), FName(ElysiumAnimGraph::SlotLayerTag));

			Wire(Link(FirstPin(Layer, EGPD_Output), PinNamed(SlotLayer, TEXT("BasePose"))),
				TEXT("layer -> slot base pose"));
			// --- the slot's two shapes, picked by what the resolver answered with --------------------
			// A grid where the slot clip declares one and the bake composed it (every fire and
			// dry-fire layer), the plain sequence where it does not (every reload layer). Both are the
			// SAME layer, so the pick is a hard switch and not a fade — a blend between them would be
			// a blend between two answers to one question.
			ExposePin(SlotGrid, TEXT("BlendSpace"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotGrid, TEXT("BlendSpace")),
				TEXT("RequestedSlotBlendSpace"), 40, 560), TEXT("slot grid BlendSpace pin"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotGrid, TEXT("X")), TEXT("AimYaw"), 40, 620),
				TEXT("slot grid X (AimYaw) pin"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotGrid, TEXT("Y")), TEXT("AimPitch"), 40, 680),
				TEXT("slot grid Y (AimPitch) pin"));
			// The playhead, as the fraction a blend space states time in. It is the same instant the
			// sequence branch is pinned to, derived from it rather than tracked beside it.
			Wire(DriveFromBool(*Graph, PinNamed(SlotGrid, TEXT("NormalizedTime")),
				TEXT("SlotNormalizedTime"), 40, 740), TEXT("slot grid NormalizedTime pin"));

			// The aim grid rides OVER the shot through its own masked blend, at weight 1 while a grid
			// stands and 0 otherwise — retail's autolayers within a sequence are hardcoded 1.0, and
			// the slot's envelope is applied once, by the outer blend. Mesh-space for the same split
			// bone the outer blend is mesh-space for; the mask is the GRID's own, written by
			// `ApplySlotAimMask` through the tag.
			SetNodeValue<uint8>(SlotAimBlend, TEXT("BlendMode"), 1);
			ResizeNodeArray(SlotAimBlend, TEXT("BlendMasks"), 1);
			ResizeNodeArray(SlotAimBlend, TEXT("LayerSetup"), 0);
			Wire(SetNodeBool(SlotAimBlend, TEXT("bMeshSpaceRotationBlend"), true),
				TEXT("slot aim bMeshSpaceRotationBlend"));
			SetOwnValue<FName>(SlotAimBlend, TEXT("Tag"), FName(ElysiumAnimGraph::SlotAimLayerTag));
			Wire(Link(FirstPin(SlotEvaluator, EGPD_Output), PinNamed(SlotAimBlend, TEXT("BasePose"))),
				TEXT("slot evaluator -> slot aim base"));
			Wire(Link(FirstPin(SlotGrid, EGPD_Output), PinNamed(SlotAimBlend, TEXT("BlendPoses_0"))),
				TEXT("slot grid -> slot aim blend pose 0"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotAimBlend, TEXT("BlendWeights_0")),
				TEXT("SlotAimLayerWeight"), 200, 300), TEXT("slot aim weight pin"));

			// And the `_delta` the slot clip declares, additively after the grid — retail's own
			// order. The evaluator is pinned to the SAME explicit time as the shot, because the
			// delta's phase is the shot's.
			ExposePin(SlotAdditiveEval, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotAdditiveEval, TEXT("Sequence")),
				TEXT("RequestedSlotAdditive"), 200, 620), TEXT("slot additive Sequence pin"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotAdditiveEval, TEXT("ExplicitTime")),
				TEXT("SlotExplicitTime"), 200, 680), TEXT("slot additive ExplicitTime pin"));
			Wire(Link(FirstPin(SlotAimBlend, EGPD_Output), PinNamed(SlotAdditive, TEXT("Base"))),
				TEXT("slot aim -> slot additive base"));
			Wire(Link(FirstPin(SlotAdditiveEval, EGPD_Output),
				PinNamed(SlotAdditive, TEXT("Additive"))), TEXT("slot additive eval -> additive"));
			Wire(DriveFromBool(*Graph, PinNamed(SlotAdditive, TEXT("Alpha")),
				TEXT("SlotAdditiveWeight"), 260, 220), TEXT("slot additive Alpha pin"));

			Wire(Link(FirstPin(SlotAdditive, EGPD_Output), PinNamed(SlotLayer, TEXT("BlendPoses_0"))),
				TEXT("slot additive -> slot blend pose 0"));
			// The enveloped weight the record carries, never the `m_flWeightMax` ceiling: a reload
			// layer ramps over a fifth of its cycle at each end and an attack layer snaps, and both
			// answers arrive here as this one number.
			Wire(DriveFromBool(*Graph, PinNamed(SlotLayer, TEXT("BlendWeights_0")),
				TEXT("SlotLayerWeight"), 280, -100), TEXT("slot weight pin"));

			// The `_delta` additive composes independently, on top — retail's own order, overlay
			// first (this node), additive second.
			ExposePin(AdditiveSequence, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(AdditiveSequence, TEXT("Sequence")),
				TEXT("RequestedAdditiveSequence"), 280, 520), TEXT("additive Sequence pin"));

			Wire(Link(FirstPin(SlotLayer, EGPD_Output), PinNamed(Additive, TEXT("Base"))),
				TEXT("slot layer -> additive base"));
			Wire(Link(FirstPin(AdditiveSequence, EGPD_Output), PinNamed(Additive, TEXT("Additive"))),
				TEXT("additive sequence -> additive"));
			Wire(DriveFromBool(*Graph, PinNamed(Additive, TEXT("Alpha")),
				TEXT("AdditiveLayerWeight"), 780, 160), TEXT("additive Alpha pin"));
			// Additive's Pose output is left unconnected on purpose: it is the graph's sole terminal,
			// and ImportGraphFromText wires whichever single output pose pin nothing else took to the
			// schema's own output node.
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		TSet<UObject*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node != nullptr && Node->CanUserDeleteNode())
			{
				Nodes.Add(Node);
			}
		}
		if (Failures > 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] %d dead wire(s) -- refusing to export. The graph "
				"in memory is incomplete; the tracked text is untouched."), Failures);
			return;
		}

		FString Text;
		FEdGraphUtilities::ExportNodesToText(Nodes, Text);
		FFileHelper::SaveStringToFile(Text, *Args[1]);
		UE_LOG(LogTemp, Display, TEXT("[animbp] built %d node(s), every wire connected, wrote %d chars"),
			Nodes.Num(), Text.Len());
	}));

#else

FElysiumAnimGraphImportResult UElysiumAnimGraphLibrary::ImportGraphFromText(UBlueprint*, FName,
	const FString&)
{
	return FElysiumAnimGraphImportResult();
}

FString UElysiumAnimGraphLibrary::ExportGraphToText(const UBlueprint*, FName)
{
	return FString();
}

#endif   // WITH_EDITOR
