#include "ElysiumAnimGraphLibrary.h"

#if WITH_EDITOR

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimStateMachineTypes.h"
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
// A state machine cannot be hand-written as T3D with any confidence: the text has to carry the
// machine's own graph, every state's bound graph and every transition's rule graph as nested
// objects, and `UAnimGraphNode_StateMachineBase::PostPasteNode` only fixes up a bound graph the text
// already carries. So the shape is built once here, through the engine's own construction path, and
// exported.
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
		// Virtual: this is what creates a state machine's own graph and a state's bound graph.
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	// A graph a node owns, by property name — `EditorStateMachineGraph` on a machine, `BoundGraph` on
	// a state or a transition.
	UEdGraph* OwnedGraph(UEdGraphNode* Node, const TCHAR* PropertyName)
	{
		if (Node == nullptr)
		{
			return nullptr;
		}
		const FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(
			Node->GetClass()->FindPropertyByName(FName(PropertyName)));
		return Prop != nullptr
			? Cast<UEdGraph>(Prop->GetObjectPropertyValue_InContainer(Node)) : nullptr;
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

	// A plain property on the node itself rather than inside its runtime struct — a transition's
	// `LogicType` and `CrossfadeDuration` are node-level editor settings.
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

	// One `Get <variable>` feeding one pin. Every transition rule in the machine is exactly this, and
	// so is every asset pin, so the graph never computes anything: the decision happened in C++ where
	// it is asserted.
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
		// still compiles — an unwired state evaluates to the reference pose, so the failure reaches
		// the owner as a T-posed body rather than as an error. Nothing unwired becomes tracked text.
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
		// The machine owns the body, one-shots layer over it on a slot, and one inertialization node
		// carries every blend. The composition tail runs after the output pose, in the proxy, so it
		// deliberately sits OUTSIDE the inertializer: axis interpolation is a rig rule over the
		// finished pose, and inertializing it would lag the forearm twist behind the blend.
		UEdGraphNode* Machine = Place(*Graph,
			TEXT("/Script/AnimGraph.AnimGraphNode_StateMachine"), -900, 0);
		UEdGraphNode* Slot = Place(*Graph, TEXT("/Script/AnimGraph.AnimGraphNode_Slot"), -560, 0);
		UEdGraphNode* Inertia = Place(*Graph,
			TEXT("/Script/AnimGraph.AnimGraphNode_Inertialization"), -280, 0);
		if (Machine == nullptr || Slot == nullptr || Inertia == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] a spine node class was not found"));
			return;
		}
		// `DefaultSlot` belongs to `DefaultGroup` on every skeleton by construction, so no bake has
		// to register a slot for the one-shot seam to land.
		SetNodeValue<FName>(Slot, TEXT("SlotName"), FName(TEXT("DefaultSlot")));
		// Without this a one-shot freezes the gait underneath it and the return to walking pops.
		SetNodeBool(Slot, TEXT("bAlwaysUpdateSourcePose"), true);

		Wire(Link(FirstPin(Machine, EGPD_Output), FirstPin(Slot, EGPD_Input)),
			TEXT("machine -> slot"));
		Wire(Link(FirstPin(Slot, EGPD_Output), FirstPin(Inertia, EGPD_Input)),
			TEXT("slot -> inertialization"));

		// --- the reaction branch (LIFE5) -----------------------------------------------------------
		//
		// A reaction REPLACES the locomotion pose rather than riding over it, so it sits on the base
		// channel between the inertializer and the upper-body layer: `bReactionActive` picks either
		// the reaction pose or everything the machine and the one-shot slot produced.
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

			// A reaction ENDS, so neither player repeats: a looping flinch never reports complete and
			// would hold the body in its reaction forever. Written as a node value rather than exposed
			// as a pin because it is a property of the channel and not of the selection — the value
			// folds into the compiled constant at compile time.
			Wire(SetNodeBool(ReactionSpace, TEXT("bLoop"), false), TEXT("reaction blend space bLoop"));
			Wire(SetNodeBool(ReactionSequence, TEXT("bLoopAnimation"), false),
				TEXT("reaction sequence bLoopAnimation"));

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
			Wire(Link(FirstPin(Inertia, EGPD_Output), PinNamed(ReactionBlend, TEXT("BlendPose_1"))),
				TEXT("inertialization -> reaction false pose"));

			// `ChildUpateMode` stays at the engine's `Default`. Its `ResetChildOnActivate` alternative
			// looks like the retrigger fix and is not one: `FAnimNode_BlendListBase` reinitializes a
			// newly-active child only when that child's weight is still <= `ZERO_ANIMWEIGHT_THRESH`, so
			// a reaction that fires again while its own fan is still fading is never covered — and when
			// a full-weight reaction ends, the reset lands on the BASE child instead, re-initializing
			// the state machine to its entry state at elapsed zero, wiping the inertializer's pose
			// history and resetting the slot's bookkeeping. That is a hard cut on release, which is the
			// opposite of what the out-fade exists for. The restart a retrigger needs comes from
			// republishing the asset: a sequence player whose `Sequence` pin changes restarts, and so
			// does the blend space player on a new `BlendSpace`.
			//
			// The transition type and the blend curve stay at the engine's own defaults on purpose:
			// choosing a curve here would pre-empt the separate sequence-blend-fidelity call, which
			// owns what every fade in this graph is shaped like. The transition type in particular must
			// stay `StandardBlend` — the graph's inertialization node is a DESCENDANT of this branch, so
			// an `Inertialization` type here would search its ancestors for an `IInertializationRequester`,
			// find none, and log a request failure every time the reaction fires.
			Wire(SetOwnValue<FName>(ReactionBlend, TEXT("Tag"),
				FName(ElysiumAnimGraph::ReactionBranchTag)), TEXT("reaction blend tag"));
		}

		// --- the upper-body layer (CCC10) ----------------------------------------------------------
		//
		// Sits after the inertializer, outside the machine: a weapon layer rides beside the
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
				TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"), 280, 200);
			UEdGraphNode* Additive = Place(*Graph,
				TEXT("/Script/AnimGraph.AnimGraphNode_ApplyAdditive"), 520, 0);
			if (UpperBodySpace == nullptr || UpperBodySequence == nullptr || UpperBodyPick == nullptr
				|| Layer == nullptr || AdditiveSequence == nullptr || Additive == nullptr)
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
			SetOwnValue<FName>(Layer, TEXT("Tag"), FName(ElysiumAnimGraph::UpperBodyLayerTag));

			Wire(Link(FirstPin(ReactionBlend, EGPD_Output), PinNamed(Layer, TEXT("BasePose"))),
				TEXT("reaction blend -> layer base pose"));
			Wire(Link(FirstPin(UpperBodyPick, EGPD_Output), PinNamed(Layer, TEXT("BlendPoses_0"))),
				TEXT("layer pick -> layer blend pose 0"));
			Wire(DriveFromBool(*Graph, PinNamed(Layer, TEXT("BlendWeights_0")),
				TEXT("UpperBodyLayerWeight"), 40, -100), TEXT("layer weight pin"));

			// The `_delta` additive composes independently, on top — retail's own order, overlay
			// first (this node), additive second.
			ExposePin(AdditiveSequence, TEXT("Sequence"));
			Wire(DriveFromBool(*Graph, PinNamed(AdditiveSequence, TEXT("Sequence")),
				TEXT("RequestedAdditiveSequence"), 280, 260), TEXT("additive Sequence pin"));

			Wire(Link(FirstPin(Layer, EGPD_Output), PinNamed(Additive, TEXT("Base"))),
				TEXT("layer -> additive base"));
			Wire(Link(FirstPin(AdditiveSequence, EGPD_Output), PinNamed(Additive, TEXT("Additive"))),
				TEXT("additive sequence -> additive"));
			Wire(DriveFromBool(*Graph, PinNamed(Additive, TEXT("Alpha")),
				TEXT("AdditiveLayerWeight"), 520, 160), TEXT("additive Alpha pin"));
			// Additive's Pose output is left unconnected on purpose: it becomes the graph's new sole
			// terminal, the same way Inertia's was before this block, and ImportGraphFromText wires
			// whichever single output pose pin nothing else took to the schema's own output node.
		}

		UEdGraph* MachineGraph = OwnedGraph(Machine, TEXT("EditorStateMachineGraph"));
		if (MachineGraph == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] the state machine built no graph"));
			return;
		}
		// The name `UAnimInstance::GetStateMachineIndex` is asked for.
		MachineGraph->Rename(TEXT("Locomotion"), nullptr,
			REN_DontCreateRedirectors | REN_NonTransactional);

		// --- the eight states ----------------------------------------------------------------------
		//
		// Every state takes the sequence-or-blend-space pair. A grid-shaped selection whose activity
		// routes here plays the fan; a plain label plays the sequence. The graph knows only which
		// asset it was handed — Idle, Crouch, Leap, Falling and Land are not sequence-only.
		struct FStateSpec { const TCHAR* Name; const TCHAR* Wants; };
		const FStateSpec Specs[] = {
			{ TEXT("Idle"),    TEXT("bWantsIdle") },
			{ TEXT("Walk"),    TEXT("bWantsWalk") },
			{ TEXT("Run"),     TEXT("bWantsRun") },
			{ TEXT("Sneak"),   TEXT("bWantsSneak") },
			{ TEXT("Crouch"),  TEXT("bWantsCrouch") },
			{ TEXT("Leap"),    TEXT("bWantsLeap") },
			{ TEXT("Falling"), TEXT("bWantsFalling") },
			{ TEXT("Land"),    TEXT("bWantsLand") },
		};
		const int32 NumStates = UE_ARRAY_COUNT(Specs);
		UEdGraphNode* StateNodes[UE_ARRAY_COUNT(Specs)] = {};

		for (int32 i = 0; i < NumStates; ++i)
		{
			UEdGraphNode* State = Place(*MachineGraph, TEXT("/Script/AnimGraph.AnimStateNode"),
				(i % 4) * 340 - 500, (i / 4) * 340 - 300);
			StateNodes[i] = State;
			UEdGraph* Inner = OwnedGraph(State, TEXT("BoundGraph"));
			if (Inner == nullptr)
			{
				UE_LOG(LogTemp, Error, TEXT("[animbp] a state built no graph"));
				return;
			}
			// A state's name IS its bound graph's name, which is what the native side resolves by.
			Inner->Rename(Specs[i].Name, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);

			UEdGraphPin* ResultPin = nullptr;
			for (UEdGraphNode* Node : Inner->Nodes)
			{
				if (Node != nullptr && !Node->CanUserDeleteNode())
				{
					ResultPin = FirstPin(Node, EGPD_Input);
				}
			}

			// Every player takes its asset from a pin and holds none itself. That is what lets one
			// graph pose every model the resolver picks assets for, and what keeps the tracked graph
			// text free of any reference to generated content.
			UEdGraphNode* Sequence = Place(*Inner,
				TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"), -420, 0);
			ExposePin(Sequence, TEXT("Sequence"));
			ExposePin(Sequence, TEXT("bLoopAnimation"));
			Wire(DriveFromBool(*Inner, PinNamed(Sequence, TEXT("Sequence")),
				TEXT("RequestedSequence"), -720, -40), TEXT("state Sequence pin"));
			Wire(DriveFromBool(*Inner, PinNamed(Sequence, TEXT("bLoopAnimation")),
				TEXT("bRequestedLooping"), -720, 60), TEXT("state bLoopAnimation pin"));

			// A grid stands on its baked fan, steered by `move_yaw`. The sequence player beside it is
			// for a label with no baked fan: the resolver answers with the single selected cell and the
			// same state plays it.
			UEdGraphNode* Space = Place(*Inner,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendSpacePlayer"), -420, -220);
			ExposePin(Space, TEXT("BlendSpace"));
			Wire(DriveFromBool(*Inner, PinNamed(Space, TEXT("BlendSpace")),
				TEXT("RequestedBlendSpace"), -720, -260), TEXT("state BlendSpace pin"));
			Wire(DriveFromBool(*Inner, PinNamed(Space, TEXT("X")), TEXT("GridAxis0"), -720, -180),
				TEXT("state X pin"));

			UEdGraphNode* Pick = Place(*Inner,
				TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"), -160, -100);
			Wire(DriveFromBool(*Inner, PinNamed(Pick, TEXT("bActiveValue")),
				TEXT("bHasBlendSpace"), -420, 180), TEXT("state bActiveValue"));
			// `BlendPose` is an array property, so the pins are `BlendPose_<index>`; "True Pose" and
			// "False Pose" are only friendly labels and match no pin. Index 0 is the TRUE branch —
			// `UAnimGraphNode_BlendListByBool::CustomizePinData` flips the pair deliberately so that
			// true reads topmost in the editor.
			Wire(Link(FirstPin(Space, EGPD_Output), PinNamed(Pick, TEXT("BlendPose_0"))),
				TEXT("state blend space -> true pose"));
			Wire(Link(FirstPin(Sequence, EGPD_Output), PinNamed(Pick, TEXT("BlendPose_1"))),
				TEXT("state sequence -> false pose"));
			Wire(Link(FirstPin(Pick, EGPD_Output), ResultPin), TEXT("state pick -> result"));
		}

		// --- the hub -------------------------------------------------------------------------------
		//
		// Every state exits to one conduit when the request stops matching what is playing, and the
		// conduit enters whichever state does match. Sixteen transitions instead of fifty-six, with
		// every pair still reachable, because `FindValidTransition` recurses through a conduit and
		// resolves to the conduit-to-state transition's own settings.
		UEdGraphNode* Dispatch = Place(*MachineGraph,
			TEXT("/Script/AnimGraph.AnimStateConduitNode"), 60, 60);
		if (Dispatch == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[animbp] no conduit class"));
			return;
		}
		if (UEdGraph* ConduitGraph = OwnedGraph(Dispatch, TEXT("BoundGraph")))
		{
			ConduitGraph->Rename(TEXT("Dispatch"), nullptr,
				REN_DontCreateRedirectors | REN_NonTransactional);
			// A conduit carries its own rule, and an unconnected one never fires. This one is a pure
			// junction, so it always passes and the decision stays on the edges either side of it.
			for (UEdGraphNode* Node : ConduitGraph->Nodes)
			{
				if (Node != nullptr && !Node->CanUserDeleteNode())
				{
					if (UEdGraphPin* Enter = FirstPin(Node, EGPD_Input))
					{
						Enter->DefaultValue = TEXT("true");
					}
				}
			}
		}

		// Entry goes to Idle: a body that has not been told anything stands.
		for (UEdGraphNode* Node : MachineGraph->Nodes)
		{
			if (Node != nullptr && !Node->CanUserDeleteNode() && Node != Dispatch)
			{
				Wire(Link(FirstPin(Node, EGPD_Output), FirstPin(StateNodes[0], EGPD_Input)),
					TEXT("entry -> Idle"));
			}
		}

		auto MakeTransition = [&MachineGraph, &Wire, &Failures](UEdGraphNode* From, UEdGraphNode* To,
			const TCHAR* Rule, int32 X, int32 Y)
		{
			UEdGraphNode* T = Place(*MachineGraph,
				TEXT("/Script/AnimGraph.AnimStateTransitionNode"), X, Y);
			if (T == nullptr)
			{
				++Failures;
				UE_LOG(LogTemp, Error, TEXT("[animbp] no transition class"));
				return;
			}
			Wire(Link(FirstPin(From, EGPD_Output), FirstPin(T, EGPD_Input)), TEXT("-> transition"));
			Wire(Link(FirstPin(T, EGPD_Output), FirstPin(To, EGPD_Input)), TEXT("transition ->"));

			// Inertialization, at a CEILING rather than an authored value. The real duration arrives
			// at runtime through `RequestSlotGroupInertialization`, and requests merge by taking the
			// smaller, so the runtime answer always wins. The ceiling exists so a frame whose request
			// did not land blends visibly wrong rather than hard-cutting invisibly — and so the graph
			// asset encodes no game-derived timing, which the authored-content policy forbids.
			SetOwnValue<uint8>(T, TEXT("LogicType"),
				static_cast<uint8>(ETransitionLogicType::TLT_Inertialization));
			SetOwnValue<float>(T, TEXT("CrossfadeDuration"),
				ElysiumAnimGraph::TransitionCeilingSeconds);

			if (UEdGraph* Rules = OwnedGraph(T, TEXT("BoundGraph")))
			{
				// The result node is found BEFORE anything is added: `DriveFromBool` places a node
				// into this same array, and walking it while it grows is an ensure.
				UEdGraphPin* Enter = nullptr;
				for (UEdGraphNode* Node : Rules->Nodes)
				{
					if (Node != nullptr && !Node->CanUserDeleteNode())
					{
						Enter = FirstPin(Node, EGPD_Input);
					}
				}
				Wire(DriveFromBool(*Rules, Enter, Rule, -320, 0), Rule);
			}
		};

		for (int32 i = 0; i < NumStates; ++i)
		{
			MakeTransition(StateNodes[i], Dispatch, TEXT("bStateChanged"),
				(i % 4) * 200 - 420, (i / 4) * 120 - 180);
			MakeTransition(Dispatch, StateNodes[i], Specs[i].Wants,
				(i % 4) * 200 - 360, (i / 4) * 120 - 120);
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
		UE_LOG(LogTemp, Display, TEXT("[animbp] built %d states, every wire connected, wrote %d chars"),
			NumStates, Text.Len());
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
