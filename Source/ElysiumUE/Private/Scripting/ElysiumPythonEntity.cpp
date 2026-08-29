#include "Scripting/ElysiumPythonEntity.h"

#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumStub.h"
#include "Scripting/ElysiumPythonVM.h"
#include "Scripting/ElysiumScriptNatives.h"

// Python.h LAST -- after every Unreal header (see ThirdParty/ElysiumPython.h for why).
#include "ThirdParty/ElysiumPython.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPyEnt, Log, All);

namespace ElysiumPy
{
namespace
{
	// The provenance the running eval is attributed to (see FScopedContext). One slot, not a
	// stack of its own: FScopedContext saves and restores, so nesting works.
	FElysiumScriptContext GContext;

	UElysiumGameStateSubsystem* State()
	{
		return FElysiumPythonVM::Get().GameState();
	}

	// vampire.Entity.
	// A handle, not a pointer: resolution is generation-checked every access, so a reference held
	// across a Kill (or a map travel) reports "game entity has been deleted" exactly as retail's
	// null `_entity_ptr_` unwrap does. `Dict` is the instance __dict__ that script-set names fall
	// through to (`ent.beam1 = ...`, oceanhouse.py's stashed refs).
	//
	// Not GC-tracked (no Py_TPFLAGS_HAVE_GC): an unreferenced object frees its dict on dealloc, but
	// a stashed one whose property bag closes a reference cycle is held until Py_Finalize. Every
	// entity object is per-map and the corpus stashes a handful, so the bound is trivial.
	struct FPyEntity
	{
		PyObject_HEAD
		FElysiumEntityHandle Handle;
		PyObject* Dict;
	};

	// There is no second player type: `FindPlayer()` returns an ordinary `vampire.Entity`
	// over the player entity's handle, so `pc.clan` is a datamap field, `pc.MoneyAdd(50)` is a
	// datamap input, and both take the same attribute walk every other entity's attributes take.

	PyTypeObject GEntityType = { PyVarObject_HEAD_INIT(nullptr, 0) "vampire.Entity", sizeof(FPyEntity) };

	bool IsEntity(PyObject* O) { return O && O->ob_type == &GEntityType; }

	// Resolve an entity object to its live entity, or set the AttributeError retail raises when
	// the boxed pointer is null ("game entity has been deleted") and return null.
	FElysiumEntity* ResolveOrRaise(PyObject* Self)
	{
		FElysiumEntityWorld* W = CurrentWorld();
		FElysiumEntity* E = W ? W->Resolve(reinterpret_cast<FPyEntity*>(Self)->Handle) : nullptr;
		if (!E)
		{
			PyErr_SetString(PyExc_AttributeError, "game entity has been deleted");
		}
		return E;
	}

	FString PyStr(PyObject* NameObj)
	{
		const char* S = PyString_AsString(NameObj);
		return S ? FString(UTF8_TO_TCHAR(S)) : FString();
	}

	// Positional call args as variants (the marshalling currency the substrate speaks).
	TArray<FElysiumVariant> ArgsToVariants(PyObject* Args)
	{
		TArray<FElysiumVariant> Out;
		const Py_ssize_t N = Args && PyTuple_Check(Args) ? PyTuple_Size(Args) : 0;
		Out.Reserve(static_cast<int32>(N));
		for (Py_ssize_t i = 0; i < N; ++i)
		{
			Out.Add(PyToVariant(PyTuple_GetItem(Args, i)));   // borrowed
		}
		return Out;
	}

	// A 3-sequence PyObject (tuple/list, or None) -> FVector. Used for the origin/angles args of
	// CreateEntityNoSpawn. Ints coerce through nb_float. Returns false (Out untouched) on a bad arg.
	bool ParseVec3Obj(PyObject* O, FVector& Out)
	{
		if (!O || O == Py_None || !PySequence_Check(O) || PySequence_Size(O) != 3)
		{
			return false;
		}
		PyObject* Seq = PySequence_Fast(O, "vec3");
		if (!Seq)
		{
			PyErr_Clear();
			return false;
		}
		Out.X = static_cast<float>(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(Seq, 0)));
		Out.Y = static_cast<float>(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(Seq, 1)));
		Out.Z = static_cast<float>(PyFloat_AsDouble(PySequence_Fast_GET_ITEM(Seq, 2)));
		Py_DECREF(Seq);
		PyErr_Clear();   // a non-numeric element left an error; treat the vec as parsed-with-zeros
		return true;
	}

	// The call args of SetOrigin/SetAngles as a vec3, accepting both shapes scripts pass: three
	// scalars SetOrigin(x, y, z) and one 3-tuple SetOrigin((x, y, z)) (GetOrigin returns a tuple).
	bool ArgsToVec3(PyObject* Args, FVector& Out)
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f;
		if (PyArg_ParseTuple(Args, "fff", &X, &Y, &Z)) { Out = FVector(X, Y, Z); return true; }
		PyErr_Clear();
		if (PyArg_ParseTuple(Args, "(fff)", &X, &Y, &Z)) { Out = FVector(X, Y, Z); return true; }
		PyErr_Clear();
		return false;
	}

	// Bound entity input.
	// `ent.<Input>` manufactures a callable on the spot, exactly as VtMB's __getattr__ does with
	// PyCFunction_New over a generic thunk (python_bridge.md step 4). Self is the (entity, name)
	// pair; calling it delivers the input through the real chokepoint.

	PyObject* Entity_fire_input(PyObject* Bound, PyObject* Args)
	{
		PyObject* EntObj = PyTuple_GetItem(Bound, 0);   // borrowed
		PyObject* NameObj = PyTuple_GetItem(Bound, 1);  // borrowed
		FElysiumEntity* E = ResolveOrRaise(EntObj);
		if (!E)
		{
			return nullptr;
		}
		FElysiumEntityWorld* W = CurrentWorld();
		if (W)
		{
			// The call is SYNCHRONOUS (`docs/vtmb/python_bridge.md` → "Synchronous calls versus
			// queued Python"): retail's `entity_input_function` marshals the argument and calls the
			// entity's AcceptInput virtual immediately, with null activator and null caller, and the
			// input body completes before Python resumes — so a script observes its own mutation
			// mid-handler. Queuing it instead would land the mutation behind the pending equal-time
			// cohort and invent provenance retail does not pass. Outputs the body fires still go
			// through FireOutput onto the ordinary queue, i.e. behind that cohort.
			//
			// The by-handle chokepoint targets exactly the bound entity without fanning out over a
			// duplicate targetname, and still notifies the FElysiumIOSink taps.
			const TArray<FElysiumVariant> Vals = ArgsToVariants(Args);
			const FElysiumEntityHandle Target = E->Handle;
			W->AcceptInput(Target, FName(*PyStr(NameObj)),
				Vals.Num() > 0 ? Vals[0] : FElysiumVariant::Void(),
				FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		}
		Py_RETURN_NONE;   // an input call has no value
	}

	PyMethodDef GInputThunk = { "entity_input", Entity_fire_input, METH_VARARGS,
		"calls this entity input synchronously through the world's AcceptInput chokepoint" };

	PyObject* MakeBoundInput(PyObject* EntObj, PyObject* NameObj)
	{
		PyObject* Bound = PyTuple_Pack(2, EntObj, NameObj);
		if (!Bound)
		{
			return nullptr;
		}
		PyObject* Fn = PyCFunction_New(&GInputThunk, Bound);
		Py_DECREF(Bound);   // PyCFunction_New took its own reference
		return Fn;
	}

	// Bound Character method.
	// Dispatched off the player object or an NPC entity handle, through the shared native surface.

	PyObject* Char_call_method(PyObject* Bound, PyObject* Args)
	{
		PyObject* RecvObj = PyTuple_GetItem(Bound, 0);
		PyObject* NameObj = PyTuple_GetItem(Bound, 1);

		FElysiumEntityHandle Self;   // unset = the PC
		if (IsEntity(RecvObj))
		{
			FElysiumEntity* E = ResolveOrRaise(RecvObj);
			if (!E)
			{
				return nullptr;
			}
			Self = E->Handle;
		}
		const TArray<FElysiumVariant> Vals = ArgsToVariants(Args);
		const FElysiumVariant R = ElysiumScriptNatives::CallCharacterMethod(
			State(), CurrentWorld(), Self, FName(*PyStr(NameObj)), Vals);
		return VariantToPy(R);
	}

	PyMethodDef GCharThunk = { "character_method", Char_call_method, METH_VARARGS,
		"dispatches a Character method through the shared native surface" };

	PyObject* MakeBoundCharMethod(PyObject* RecvObj, PyObject* NameObj)
	{
		PyObject* Bound = PyTuple_Pack(2, RecvObj, NameObj);
		if (!Bound)
		{
			return nullptr;
		}
		PyObject* Fn = PyCFunction_New(&GCharThunk, Bound);
		Py_DECREF(Bound);
		return Fn;
	}

	// Entity base methods (the 0x1058f698 table).
	// The readers run off the live substrate. The writers that still have no backing — moving a
	// brush body, re-indexing a targetname, swapping a model — record a native-call stub, which
	// is what the Scripting window's counters are for.

	PyObject* Vec3ToPy(const FVector& V)
	{
		return Py_BuildValue("(fff)", static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}

	// Origins and centres are Unreal centimetres — the runtime reads every sidecar verbatim and
	// never converts (repo CLAUDE.md "Coordinates are read verbatim"), so the script-visible space
	// is the runtime's own. `angles` is the authored pitch/yaw/roll keyfield.
	PyObject* Entity_GetOrigin(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		return E && E->Def ? Vec3ToPy(E->Def->Origin) : nullptr;
	}

	PyObject* Entity_GetCenter(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E)
		{
			return nullptr;
		}
		// A brush entity's centre is its body's bounds; a point entity's is its origin.
		if (E->Body)
		{
			return Vec3ToPy(E->Body->Bounds.Origin);
		}
		return E->Def ? Vec3ToPy(E->Def->Origin) : Vec3ToPy(FVector::ZeroVector);
	}

	PyObject* Entity_GetAngles(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		return E ? Vec3ToPy(E->Angles) : nullptr;
	}

	PyObject* Entity_GetAngleVectors(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E)
		{
			return nullptr;
		}
		// The forward vector for the authored (pitch, yaw, roll). FRotator is (Pitch, Yaw, Roll)
		// and the sidecar's angles keyfield is written in that order.
		const FRotator R(E->Angles.X, E->Angles.Y, E->Angles.Z);
		return Vec3ToPy(R.Vector());
	}

	PyObject* Entity_GetName(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		return E ? PyString_FromString(TCHAR_TO_UTF8(*E->TargetName)) : nullptr;
	}

	PyObject* Entity_GetModelName(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		return E ? PyString_FromString(TCHAR_TO_UTF8(*E->Model)) : nullptr;
	}

	PyObject* Entity_IsAlive(PyObject* Self, PyObject*)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		return E ? PyInt_FromLong(E->IsDead() ? 0 : 1) : nullptr;
	}

	// Log a writer against the calling entity so the Scripting window still counts it (not a stub now).
	void RecordWriter(FElysiumEntity* E, const TCHAR* Method, PyObject* Args)
	{
		FElysiumEntityWorld* W = CurrentWorld();
		const FString Display = FString::Printf(TEXT("%s.%s(%s)"),
			W ? *W->DescribeHandle(E->Handle) : *E->Handle.ToString(), Method,
			*ElysiumScriptNatives::DescribeArgs(ArgsToVariants(Args)));
		ElysiumScriptNatives::Record(State(), FName(Method), Display, FElysiumVariant::Void(), /*bStub*/ false);
	}

	// The four writers, real: mutate the authoritative field, move/re-skin any body that follows,
	// re-key the name index. GetOrigin/GetAngles/GetModelName/GetName read the mutated state back.
	PyObject* Entity_SetOrigin(PyObject* Self, PyObject* Args)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E) { return nullptr; }
		FVector V;
		if (!ArgsToVec3(Args, V)) { PyErr_SetString(PyExc_TypeError, "SetOrigin expects (x, y, z)"); return nullptr; }
		E->SetRuntimeOrigin(V);
		RecordWriter(E, TEXT("SetOrigin"), Args);
		Py_RETURN_NONE;
	}

	PyObject* Entity_SetAngles(PyObject* Self, PyObject* Args)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E) { return nullptr; }
		FVector V;
		if (!ArgsToVec3(Args, V)) { PyErr_SetString(PyExc_TypeError, "SetAngles expects (pitch, yaw, roll)"); return nullptr; }
		E->SetRuntimeAngles(V);
		RecordWriter(E, TEXT("SetAngles"), Args);
		Py_RETURN_NONE;
	}

	PyObject* Entity_SetModel(PyObject* Self, PyObject* Args)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E) { return nullptr; }
		const char* Path = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Path)) { return nullptr; }
		E->SetRuntimeModel(FString(UTF8_TO_TCHAR(Path)));
		RecordWriter(E, TEXT("SetModel"), Args);
		Py_RETURN_NONE;
	}

	PyObject* Entity_SetName(PyObject* Self, PyObject* Args)
	{
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E) { return nullptr; }
		const char* Name = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Name)) { return nullptr; }
		if (FElysiumEntityWorld* W = CurrentWorld())
		{
			W->RenameEntity(*E, FString(UTF8_TO_TCHAR(Name)));
		}
		RecordWriter(E, TEXT("SetName"), Args);
		Py_RETURN_NONE;
	}

	PyMethodDef GEntityMethods[] =
	{
		{ "GetOrigin",       Entity_GetOrigin,       METH_VARARGS, "Returns the origin of this entity" },
		{ "GetAngles",       Entity_GetAngles,       METH_VARARGS, "Returns the direction this entity is facing expressed as an angle" },
		{ "GetCenter",       Entity_GetCenter,       METH_VARARGS, "Returns the center of this entity" },
		{ "GetAngleVectors", Entity_GetAngleVectors, METH_VARARGS, "Returns the direction this entity is facing expressed as a normalized vector" },
		{ "GetModelName",    Entity_GetModelName,    METH_VARARGS, "Returns the entity's model filename" },
		{ "GetName",         Entity_GetName,         METH_VARARGS, "Returns the entity's name" },
		{ "IsAlive",         Entity_IsAlive,         METH_VARARGS, "Returns 1 if the entity is alive, otherwise 0" },
		{ "SetOrigin",       Entity_SetOrigin,       METH_VARARGS, "Sets the origin of this entity" },
		{ "SetAngles",       Entity_SetAngles,       METH_VARARGS, "Sets the direction this entity is facing" },
		{ "SetModel",        Entity_SetModel,        METH_VARARGS, "Sets the entity's model to the supplied filename" },
		{ "SetName",         Entity_SetName,         METH_VARARGS, "Sets the entity's name to the supplied string" },
		{ nullptr, nullptr, 0, nullptr }
	};

	// Entity attribute protocol.

	// Read order mirrors retail: the class method table and the instance __dict__ resolve first
	// (an old-style class only calls __getattr__ once the normal lookup fails), then the datamap
	// walk — input name -> a bound callable, field name -> its marshalled value.
	PyObject* Entity_getattro(PyObject* Self, PyObject* NameObj)
	{
		if (PyObject* Generic = PyObject_GenericGetAttr(Self, NameObj))
		{
			return Generic;
		}
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
		{
			return nullptr;   // a genuine error, not just "not found"
		}
		PyErr_Clear();

		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E)
		{
			return nullptr;
		}
		const FString Attr = PyStr(NameObj);
		const FName AttrName(*Attr);
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		if (E->Class)
		{
			if (Reg.FindInput(*E->Class, AttrName) != nullptr)
			{
				return MakeBoundInput(Self, NameObj);
			}
			if (const FElysiumFieldAccessor* F = Reg.FindField(*E->Class, AttrName))
			{
				return VariantToPy(F->Get(*E));
			}
		}
		// The vdata-driven half of the character sheet: `pc.base_Celerity` has to read a
		// number, not bind as a method, and the registry's static field table cannot name it;
		// the sheet supplies those names. Consulted after the chain walk, before the method fallback.
		{
			FElysiumVariant Dynamic;
			if (E->GetDynamicField(AttrName, Dynamic))
			{
				return VariantToPy(Dynamic);
			}
		}
		// A Character method invoked on an NPC entity (Find("bob").SetExpression(...)): bind it so
		// it dispatches through the same surface the PC uses.
		if (ElysiumScriptNatives::IsCharacterMethod(Attr))
		{
			return MakeBoundCharMethod(Self, NameObj);
		}
		// Reported, then raised anyway. The raise is faithful: VtMB's datamap walk ends the same way
		// (`docs/vtmb/python_bridge.md` — a miss falls through to the instance `__dict__`, and an
		// unset name raises from there), so answering a default here would be a divergence, not a
		// stub. What the report adds is that the miss stops being silent — a name we simply have
		// not built and a name no datamap ever carried look identical from the raise alone.
		ElysiumStub::Fired(TEXT("attr"),
			FString::Printf(TEXT("%s.%s"), E->Def ? *E->Def->Classname : TEXT("?"), *Attr),
			E->DebugString(), FString(),
			TEXT("no field, input or Character method of this name — raises AttributeError"));
		PyErr_Format(PyExc_AttributeError, "entity has no attribute '%s'", TCHAR_TO_UTF8(*Attr));
		return nullptr;
	}

	// Write order is the mirror image and deliberately asymmetric (python_bridge.md "The write
	// path"): __setattr__ runs the datamap walk FIRST, and only a miss falls through to the
	// instance __dict__.
	int Entity_setattro(PyObject* Self, PyObject* NameObj, PyObject* Value)
	{
		if (Value == nullptr)
		{
			return PyObject_GenericSetAttr(Self, NameObj, Value);   // `del ent.x` -> the property bag
		}
		FElysiumEntity* E = ResolveOrRaise(Self);
		if (!E)
		{
			return -1;
		}
		const FString Attr = PyStr(NameObj);
		const FName AttrName(*Attr);
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		if (E->Class)
		{
			if (const FElysiumFieldAccessor* F = Reg.FindField(*E->Class, AttrName))
			{
				if (!F->bKeyable)
				{
					PyErr_Format(PyExc_AttributeError, "%s is read only", TCHAR_TO_UTF8(*Attr));
					return -1;
				}
				F->Set(*E, PyToVariant(Value));
				return 0;
			}
			// An input is a datamap entry too, and its record is not keyable — assigning one is the
			// same "read only" error. Letting it reach the property bag would be worse than an error:
			// the dict entry would shadow the input from then on, so `ent.Fade()` would stop firing.
			if (Reg.FindInput(*E->Class, AttrName) != nullptr)
			{
				PyErr_Format(PyExc_AttributeError, "%s is read only", TCHAR_TO_UTF8(*Attr));
				return -1;
			}
		}
		// The sheet bag, mirroring the read path: `pc.base_Celerity = 3` writes the sheet
		// rather than shadowing it in the property bag.
		if (E->SetDynamicField(AttrName, PyToVariant(Value)))
		{
			return 0;
		}
		return PyObject_GenericSetAttr(Self, NameObj, Value);
	}

	PyObject* Entity_repr(PyObject* Self)
	{
		FElysiumEntityWorld* W = CurrentWorld();
		const FElysiumEntityHandle H = reinterpret_cast<FPyEntity*>(Self)->Handle;
		const FString Desc = W ? W->DescribeHandle(H) : H.ToString();
		return PyString_FromString(TCHAR_TO_UTF8(*FString::Printf(TEXT("<entity %s>"), *Desc)));
	}

	void Entity_dealloc(PyObject* Self)
	{
		Py_XDECREF(reinterpret_cast<FPyEntity*>(Self)->Dict);
		PyObject_Del(Self);
	}

	// The 11 module globals.

	// "Find the first player entity, or NULL if there is not one spawned" — the binding's own
	// docstring, and exactly what it does: the player IS an entity, so this is the same
	// lookup FindEntityByName does, and `None` on a map built without a player (a menu backdrop) is
	// the documented answer rather than an invented one.
	PyObject* Mod_FindPlayer(PyObject*, PyObject* Args)
	{
		const TArray<FElysiumVariant> Vals = ArgsToVariants(Args);
		FElysiumEntityWorld* W = CurrentWorld();
		FElysiumEntity* P = W ? static_cast<FElysiumEntity*>(W->FindPlayer()) : nullptr;
		const FElysiumVariant R = P ? FElysiumVariant::Handle(P->Handle) : FElysiumVariant::Void();
		ElysiumScriptNatives::Record(State(), FName(TEXT("FindPlayer")),
			FString::Printf(TEXT("FindPlayer(%s)"), *ElysiumScriptNatives::DescribeArgs(Vals)),
			R, /*bStub*/ false);
		return NewEntity(P ? P->Handle : FElysiumEntityHandle::Invalid());
	}

	PyObject* Mod_FindEntityByName(PyObject*, PyObject* Args)
	{
		const char* Name = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Name))
		{
			return nullptr;
		}
		FElysiumEntityWorld* W = CurrentWorld();
		FElysiumEntity* E = W ? W->FindByName(FString(UTF8_TO_TCHAR(Name))) : nullptr;
		const FElysiumVariant R = E ? FElysiumVariant::Handle(E->Handle) : FElysiumVariant::Void();
		ElysiumScriptNatives::Record(State(), FName(TEXT("FindEntityByName")),
			FString::Printf(TEXT("FindEntityByName(\"%s\")"), UTF8_TO_TCHAR(Name)), R, /*bStub*/ false);
		// "Returns None if not found" — the binding's own docstring, and what every `if ent:` guard
		// in the shipped scripts actually tests (entity objects themselves are always truthy).
		return NewEntity(E ? E->Handle : FElysiumEntityHandle::Invalid());
	}

	PyObject* MakeEntityList(const TArray<FElysiumEntityHandle>& Handles)
	{
		PyObject* List = PyList_New(0);
		if (!List)
		{
			return nullptr;
		}
		for (const FElysiumEntityHandle& H : Handles)
		{
			PyObject* Item = NewEntity(H);
			if (Item)
			{
				PyList_Append(List, Item);
				Py_DECREF(Item);
			}
		}
		return List;
	}

	PyObject* Mod_FindEntitiesByName(PyObject*, PyObject* Args)
	{
		const char* Name = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Name))
		{
			return nullptr;
		}
		TArray<FElysiumEntityHandle> Found;
		if (FElysiumEntityWorld* W = CurrentWorld())
		{
			W->ForEachNamed(FString(UTF8_TO_TCHAR(Name)), [&Found](FElysiumEntity& E) { Found.Add(E.Handle); });
		}
		ElysiumScriptNatives::Record(State(), FName(TEXT("FindEntitiesByName")),
			FString::Printf(TEXT("FindEntitiesByName(\"%s\")"), UTF8_TO_TCHAR(Name)),
			FElysiumVariant::Int(Found.Num()), /*bStub*/ false);
		return MakeEntityList(Found);
	}

	PyObject* Mod_FindEntitiesByClass(PyObject*, PyObject* Args)
	{
		const char* Cls = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Cls))
		{
			return nullptr;
		}
		const FString Want(UTF8_TO_TCHAR(Cls));
		TArray<FElysiumEntityHandle> Found;
		if (FElysiumEntityWorld* W = CurrentWorld())
		{
			for (const TUniquePtr<FElysiumEntity>& E : W->Entities())
			{
				if (E && !E->IsDead() && E->Def && E->Def->Classname.Equals(Want, ESearchCase::IgnoreCase))
				{
					Found.Add(E->Handle);
				}
			}
		}
		ElysiumScriptNatives::Record(State(), FName(TEXT("FindEntitiesByClass")),
			FString::Printf(TEXT("FindEntitiesByClass(\"%s\")"), *Want),
			FElysiumVariant::Int(Found.Num()), /*bStub*/ false);
		return MakeEntityList(Found);
	}

	// The remaining globals have no host-specific object in their result, so they go straight
	// through the shared surface: ScheduleTask and ChangeMap are real (they use the event queue),
	// the rest log a stub and return their default.
	PyObject* CallSimple(const TCHAR* Name, PyObject* Args)
	{
		const TArray<FElysiumVariant> Vals = ArgsToVariants(Args);
		return VariantToPy(ElysiumScriptNatives::CallSimpleGlobal(
			State(), CurrentWorld(), GContext, FName(Name), Vals));
	}

	PyObject* Mod_ScheduleTask(PyObject*, PyObject* A)        { return CallSimple(TEXT("ScheduleTask"), A); }
	PyObject* Mod_ChangeMap(PyObject*, PyObject* A)           { return CallSimple(TEXT("ChangeMap"), A); }
	PyObject* Mod_SquadSeesPlayer(PyObject*, PyObject* A)     { return CallSimple(TEXT("SquadSeesPlayer"), A); }
	PyObject* Mod_OneOfSet(PyObject*, PyObject* A)            { return CallSimple(TEXT("OneOfSet"), A); }
	PyObject* Mod_IsPCMalk(PyObject*, PyObject* A)            { return CallSimple(TEXT("IsPCMalk"), A); }
	PyObject* Mod_IsClan(PyObject*, PyObject* A)              { return CallSimple(TEXT("IsClan"), A); }

	// CreateEntityNoSpawn(classname, origin, angles): build a runtime def, append a live-but-
	// unspawned entity, and return the Entity object so the script can SetModel/SetName/SetOrigin on it
	// before CallEntitySpawn. Classes with no leaf (item_*, prop_*) become logic-valid but bodiless
	// entities — findable, I/O-wired, no mesh.
	PyObject* Mod_CreateEntityNoSpawn(PyObject*, PyObject* Args)
	{
		const char* Cls = nullptr;
		PyObject* OriginObj = nullptr;
		PyObject* AnglesObj = nullptr;
		if (!PyArg_ParseTuple(Args, "s|OO", &Cls, &OriginObj, &AnglesObj))
		{
			return nullptr;
		}
		FVector Origin = FVector::ZeroVector, Angles = FVector::ZeroVector;
		ParseVec3Obj(OriginObj, Origin);
		ParseVec3Obj(AnglesObj, Angles);

		FElysiumEntityHandle H = FElysiumEntityHandle::Invalid();
		if (FElysiumEntityWorld* W = CurrentWorld())
		{
			FElysiumEntityDef Def;
			Def.Classname = FString(UTF8_TO_TCHAR(Cls));
			Def.Origin = Origin;
			H = W->CreateRuntimeEntityNoSpawn(MoveTemp(Def));
			if (FElysiumEntity* E = W->Resolve(H)) { E->Angles = Angles; }
		}
		ElysiumScriptNatives::Record(State(), FName(TEXT("CreateEntityNoSpawn")),
			FString::Printf(TEXT("CreateEntityNoSpawn(\"%s\")"), UTF8_TO_TCHAR(Cls)),
			H.IsSet() ? FElysiumVariant::Handle(H) : FElysiumVariant::Void(), /*bStub*/ false);
		return NewEntity(H);
	}

	// CallEntitySpawn(entity): run the deferred Spawn() (+ brush body) on the entity that
	// CreateEntityNoSpawn made. Forgiving on a None/non-entity arg (a create under no world), so a map
	// script never aborts on it — error-to-false's spirit.
	PyObject* Mod_CallEntitySpawn(PyObject*, PyObject* Args)
	{
		PyObject* EntObj = nullptr;
		if (!PyArg_ParseTuple(Args, "O", &EntObj))
		{
			return nullptr;
		}
		FElysiumVariant R = FElysiumVariant::Void();
		if (IsEntity(EntObj))
		{
			FElysiumEntity* E = ResolveOrRaise(EntObj);
			if (!E) { return nullptr; }   // deleted handle: ResolveOrRaise set the AttributeError
			if (FElysiumEntityWorld* W = CurrentWorld()) { W->CallEntitySpawn(*E); }
			R = FElysiumVariant::Handle(E->Handle);
		}
		ElysiumScriptNatives::Record(State(), FName(TEXT("CallEntitySpawn")),
			FString::Printf(TEXT("CallEntitySpawn(%s)"), *R.Describe()), R, /*bStub*/ false);
		Py_RETURN_NONE;
	}

	// The module table, in `vampire.dll`'s own order (python_bridge.md, table 0x1058f7a8).
	PyMethodDef GModuleGlobals[] =
	{
		{ "FindPlayer",          Mod_FindPlayer,          METH_VARARGS, "Find the first player entity, or NULL if there is not one spawned" },
		{ "FindEntityByName",    Mod_FindEntityByName,    METH_VARARGS, "Find a single entity by its targetname field. Returns None if not found." },
		{ "FindEntitiesByName",  Mod_FindEntitiesByName,  METH_VARARGS, "Returns a list of entities matching the name." },
		{ "FindEntitiesByClass", Mod_FindEntitiesByClass, METH_VARARGS, "Returns a list of entities matching the class." },
		{ "ScheduleTask",        Mod_ScheduleTask,        METH_VARARGS, "Sets up a task callback." },
		{ "SquadSeesPlayer",     Mod_SquadSeesPlayer,     METH_VARARGS, "Returns 1 if NPCs in the squad can see the player." },
		{ "CreateEntityNoSpawn", Mod_CreateEntityNoSpawn, METH_VARARGS, "Creates an entity, but does not call its spawn function" },
		{ "CallEntitySpawn",     Mod_CallEntitySpawn,     METH_VARARGS, "Dispatches the entity's spawn function" },
		{ "ChangeMap",           Mod_ChangeMap,           METH_VARARGS, "Changes to the map and sets the player at the specified landmark" },
		{ "OneOfSet",            Mod_OneOfSet,            METH_VARARGS, "" },
		{ "IsPCMalk",            Mod_IsPCMalk,            METH_VARARGS, "Returns 1 if the player is Malkavian, otherwise returns 0." },
		{ "IsClan",              Mod_IsClan,              METH_VARARGS, "Returns 1 if the character is of the named clan, otherwise 0." },
		{ nullptr, nullptr, 0, nullptr }
	};

	// vampire.ccmd : the console command object (`docs/vtmb/python_bridge.md`, the fifth surface).
	// TOUCHING an attribute executes a console command through the shared FElysiumConsole -- both
	// assigning one and merely reading one. `c.patchtype = ""` runs the alias `patchtype`, which the
	// Unofficial Patch's user.cfg defines as `setPlus()` -> the console falls through to Python ->
	// the level-script function. On assign, a non-empty value is the command's argument string (the
	// corpus only ever assigns "").
	//
	// **Read executes too**, which is what makes chargen reachable: output field 6 is wrapped as
	// `__main__.%s` and evaluated, so genesis's `ccmd.createplayer` is a bare GET with no assignment,
	// and it is the only invocation of that command in the whole shipped corpus. A get returns "" so
	// the field-6 expression still yields a value. The other field-6 use, `ccmd.wc_create`, resolves
	// as no command / no alias / no cvar / no Python name and the console drops it with a Verbose log.
	PyObject* Ccmd_getattro(PyObject* Self, PyObject* NameObj)
	{
		if (PyObject* Generic = PyObject_GenericGetAttr(Self, NameObj))
		{
			return Generic; // __class__/__doc__/... resolve normally
		}
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
		{
			return nullptr;
		}
		PyErr_Clear();
		const char* Name = PyString_AsString(NameObj);
		if (!Name)
		{
			return nullptr;
		}
		FElysiumPythonVM::Get().Console().Execute(FString(UTF8_TO_TCHAR(Name)));
		return PyString_FromString("");
	}

	int Ccmd_setattro(PyObject* /*Self*/, PyObject* NameObj, PyObject* Value)
	{
		const char* Name = PyString_AsString(NameObj);
		if (!Name)
		{
			return -1;
		}
		FString Line(UTF8_TO_TCHAR(Name));
		if (Value && Value != Py_None)
		{
			const FString Arg = PyToVariant(Value).ToString();
			if (!Arg.IsEmpty())
			{
				Line += TEXT(" ") + Arg;
			}
		}
		FElysiumPythonVM::Get().Console().Execute(Line);
		return 0;
	}

	PyTypeObject GCcmdType =
	{
		PyVarObject_HEAD_INIT(nullptr, 0)
		"vampire.ccmd",     // tp_name
		sizeof(PyObject),   // tp_basicsize
	};

	// vampire.cvar : console variables.
	// Attribute-GET reads a cvar value as a string (empty on a miss; never raises -- `cvar.name`
	// is read by setPlus's haven personalization); attribute-SET stores it.
	PyObject* Cvar_getattro(PyObject* Self, PyObject* NameObj)
	{
		if (PyObject* Generic = PyObject_GenericGetAttr(Self, NameObj))
		{
			return Generic;
		}
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
		{
			return nullptr;
		}
		PyErr_Clear();
		const char* Name = PyString_AsString(NameObj);
		const FString V = Name
			? FElysiumPythonVM::Get().Console().GetCvar(FString(UTF8_TO_TCHAR(Name)))
			: FString();
		return PyString_FromString(TCHAR_TO_UTF8(*V));
	}

	int Cvar_setattro(PyObject* /*Self*/, PyObject* NameObj, PyObject* Value)
	{
		const char* Name = PyString_AsString(NameObj);
		if (!Name)
		{
			return -1;
		}
		const FString Key(UTF8_TO_TCHAR(Name));
		const FString Val = (Value && Value != Py_None) ? PyToVariant(Value).ToString() : FString();
		FElysiumPythonVM::Get().Console().SetCvar(Key, Val);
		return 0;
	}

	PyTypeObject GCvarType =
	{
		PyVarObject_HEAD_INIT(nullptr, 0)
		"vampire.cvar",     // tp_name
		sizeof(PyObject),   // tp_basicsize
	};
}   // anonymous namespace

// Public surface.

PyObject* VariantToPy(const FElysiumVariant& V)
{
	if (V.IsVoid())   { Py_RETURN_NONE; }
	if (V.IsBool())   { return PyBool_FromLong(V.ToBool() ? 1 : 0); }
	if (V.IsInt())    { return PyInt_FromLong(V.ToInt()); }
	if (V.IsFloat())  { return PyFloat_FromDouble(V.ToFloat()); }
	if (V.IsVector()) { return Vec3ToPy(V.AsVector); }
	if (V.IsHandle()) { return NewEntity(V.AsHandle); }
	return PyString_FromString(TCHAR_TO_UTF8(*V.ToString()));
}

FElysiumVariant PyToVariant(PyObject* O)
{
	if (!O || O == Py_None)      { return FElysiumVariant::Void(); }
	if (PyBool_Check(O))         { return FElysiumVariant::Bool(O == Py_True); }
	if (PyInt_Check(O))          { return FElysiumVariant::Int(static_cast<int32>(PyInt_AsLong(O))); }
	if (PyLong_Check(O))         { return FElysiumVariant::Int(static_cast<int32>(PyLong_AsLong(O))); }
	if (PyFloat_Check(O))        { return FElysiumVariant::Float(static_cast<float>(PyFloat_AsDouble(O))); }
	if (PyString_Check(O))       { return FElysiumVariant::String(FString(UTF8_TO_TCHAR(PyString_AsString(O)))); }
	if (IsEntity(O))             { return FElysiumVariant::Handle(reinterpret_cast<FPyEntity*>(O)->Handle); }
	// Fall back to repr() so an odd type is at least visible in G / a debug log.
	PyObject* R = PyObject_Repr(O);
	FElysiumVariant Out = FElysiumVariant::String(R ? FString(UTF8_TO_TCHAR(PyString_AsString(R))) : FString(TEXT("<obj>")));
	Py_XDECREF(R);
	return Out;
}

FString FetchPyError()
{
	if (!PyErr_Occurred())
	{
		return FString();
	}
	PyObject *Type = nullptr, *Value = nullptr, *Traceback = nullptr;
	PyErr_Fetch(&Type, &Value, &Traceback);
	PyErr_NormalizeException(&Type, &Value, &Traceback);

	FString TypeName, Message;
	if (Type)
	{
		if (PyObject* N = PyObject_GetAttrString(Type, "__name__"))
		{
			TypeName = FString(UTF8_TO_TCHAR(PyString_AsString(N)));
			Py_DECREF(N);
		}
	}
	if (Value)
	{
		if (PyObject* S = PyObject_Str(Value))
		{
			Message = FString(UTF8_TO_TCHAR(PyString_AsString(S)));
			Py_DECREF(S);
		}
	}
	Py_XDECREF(Type);
	Py_XDECREF(Value);
	Py_XDECREF(Traceback);
	return TypeName.IsEmpty() ? Message : FString::Printf(TEXT("%s: %s"), *TypeName, *Message);
}

PyObject* NewEntity(const FElysiumEntityHandle& Handle)
{
	if (!Handle.IsSet())
	{
		Py_RETURN_NONE;
	}
	FPyEntity* E = PyObject_New(FPyEntity, &GEntityType);
	if (!E)
	{
		return nullptr;
	}
	E->Handle = Handle;
	E->Dict = nullptr;   // created on first property-bag write by PyObject_GenericSetAttr
	return reinterpret_cast<PyObject*>(E);
}

bool InstallEntityBindings(PyObject* Module, FString& OutError)
{
	GEntityType.tp_flags      = Py_TPFLAGS_DEFAULT;
	GEntityType.tp_dealloc    = Entity_dealloc;
	GEntityType.tp_repr       = Entity_repr;
	GEntityType.tp_getattro   = Entity_getattro;
	GEntityType.tp_setattro   = Entity_setattro;
	GEntityType.tp_methods    = GEntityMethods;
	GEntityType.tp_dictoffset = offsetof(FPyEntity, Dict);
	GEntityType.tp_doc        = "A live game entity. Attribute names are its Hammer keyvalue and "
	                            "input names (one namespace); unknown names are a property bag.";
	if (PyType_Ready(&GEntityType) < 0)
	{
		OutError = FString::Printf(TEXT("PyType_Ready(vampire.Entity) failed: %s"), *FetchPyError());
		return false;
	}

	Py_INCREF(&GEntityType);
	PyModule_AddObject(Module, "Entity", reinterpret_cast<PyObject*>(&GEntityType));
	// There is no `vampire.Player`: the player is an Entity like everything else.

	// The console objects: vampire.ccmd (attribute-set executes) + vampire.cvar. Both are
	// data-less singletons forwarding to FElysiumPythonVM's FElysiumConsole.
	GCcmdType.tp_flags    = Py_TPFLAGS_DEFAULT;
	GCcmdType.tp_getattro = Ccmd_getattro;
	GCcmdType.tp_setattro = Ccmd_setattro;
	GCcmdType.tp_doc      = "VtMB console command object -- assigning an attribute runs that command.";
	GCvarType.tp_flags    = Py_TPFLAGS_DEFAULT;
	GCvarType.tp_getattro = Cvar_getattro;
	GCvarType.tp_setattro = Cvar_setattro;
	GCvarType.tp_doc      = "VtMB console variables -- attribute get/set reads/writes a cvar value.";
	if (PyType_Ready(&GCcmdType) < 0 || PyType_Ready(&GCvarType) < 0)
	{
		OutError = FString::Printf(TEXT("PyType_Ready(vampire.ccmd/cvar) failed: %s"), *FetchPyError());
		return false;
	}
	PyObject* Ccmd = PyObject_New(PyObject, &GCcmdType);
	PyObject* Cvar = PyObject_New(PyObject, &GCvarType);
	if (!Ccmd || !Cvar)
	{
		Py_XDECREF(Ccmd);
		Py_XDECREF(Cvar);
		OutError = TEXT("PyObject_New(vampire.ccmd/cvar) failed");
		return false;
	}
	PyModule_AddObject(Module, "ccmd", Ccmd); // steals the reference
	PyModule_AddObject(Module, "cvar", Cvar); // steals the reference

	// The module globals are added after Py_InitModule3 rather than through its table, so this
	// file owns the whole entity/native surface and ElysiumPythonVM.cpp owns only the VM.
	PyObject* ModName = PyString_FromString("vampire");
	for (PyMethodDef* Def = GModuleGlobals; Def->ml_name != nullptr; ++Def)
	{
		PyObject* Fn = PyCFunction_NewEx(Def, nullptr, ModName);
		if (!Fn)
		{
			Py_DECREF(ModName);
			OutError = FString::Printf(TEXT("PyCFunction_NewEx(vampire.%hs) failed: %s"),
				Def->ml_name, *FetchPyError());
			return false;
		}
		PyModule_AddObject(Module, Def->ml_name, Fn);   // steals the reference
	}
	Py_DECREF(ModName);
	return true;
}

FScopedContext::FScopedContext(const FElysiumScriptContext& Ctx)
	: Saved(GContext)
{
	GContext = Ctx;
}

FScopedContext::~FScopedContext()
{
	GContext = Saved;
}

FElysiumEntityWorld* CurrentWorld()
{
	if (GContext.World)
	{
		return GContext.World;   // the world that is delivering this payload
	}
	UElysiumGameStateSubsystem* S = State();
	return S ? S->CurrentEntityWorld() : nullptr;   // a console-run eval between deliveries
}

const FElysiumScriptContext& CurrentContext()
{
	return GContext;
}

}   // namespace ElysiumPy

#endif // ELYSIUM_WITH_CPYTHON
