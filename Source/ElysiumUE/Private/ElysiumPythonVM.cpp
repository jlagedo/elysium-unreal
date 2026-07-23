#include "ElysiumPythonVM.h"

#include "ElysiumGameStateSubsystem.h"
#include "ElysiumScriptHost.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// Python.h LAST -- after every Unreal header (see ThirdParty/ElysiumPython.h for why).
#include "ThirdParty/ElysiumPython.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPy, Log, All);

// ---------------------------------------------------------------------------------------------
// Everything from here to InitVampireModule is compiled only with the vendored CPython SDK.
// Without it the VM is an inert stub (every method reports "built without CPython").
// ---------------------------------------------------------------------------------------------
#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

namespace
{
	// --- marshaling: FElysiumVariant <-> PyObject (new reference out / borrowed in) ------------

	PyObject* VariantToPy(const FElysiumVariant& V)
	{
		if (V.IsVoid())   { Py_RETURN_NONE; }
		if (V.IsBool())   { return PyBool_FromLong(V.ToBool() ? 1 : 0); }
		if (V.IsInt())    { return PyInt_FromLong(V.ToInt()); }
		if (V.IsFloat())  { return PyFloat_FromDouble(V.ToFloat()); }
		// String / Vector / Handle all round-trip through their text form (the scripts only ever
		// store ints/strings in G; the rest is for completeness / debug echo).
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
		// Fall back to repr() so a bound entity / odd type is at least visible in G.
		PyObject* R = PyObject_Repr(O);
		FElysiumVariant Out = FElysiumVariant::String(R ? FString(UTF8_TO_TCHAR(PyString_AsString(R))) : FString(TEXT("<obj>")));
		Py_XDECREF(R);
		return Out;
	}

	// Pull the pending exception into a "Type: message" string and clear it. Never throws.
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

	// --- the `vampire.G` proxy type: attribute access <-> the C++ game-state store -------------
	// A data-less PyObject; every read/write forwards to the current UElysiumGameStateSubsystem.
	// Methods (keys/has_key/ClearAll) resolve first via the generic path, then any other name is a
	// G flag (default int 0 on a miss, delete on assign-None) -- exactly VtMB's tp_getattr/tp_setattr.

	UElysiumGameStateSubsystem* GStore()
	{
		return FElysiumPythonVM::Get().GameState();
	}

	PyObject* PyG_getattro(PyObject* Self, PyObject* NameObj)
	{
		if (PyObject* Generic = PyObject_GenericGetAttr(Self, NameObj))
		{
			return Generic; // a real attribute / bound method (keys, has_key, ClearAll, __class__...)
		}
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
		{
			return nullptr; // a genuine error, not just "not found"
		}
		PyErr_Clear();

		const char* Name = PyString_AsString(NameObj);
		if (!Name)
		{
			return nullptr;
		}
		UElysiumGameStateSubsystem* S = GStore();
		const FElysiumVariant V = S ? S->GetGlobal(FString(UTF8_TO_TCHAR(Name))) : FElysiumVariant::Void();
		return V.IsVoid() ? PyInt_FromLong(0) : VariantToPy(V); // miss -> 0 (retail default)
	}

	int PyG_setattro(PyObject* /*Self*/, PyObject* NameObj, PyObject* Value)
	{
		const char* Name = PyString_AsString(NameObj);
		if (!Name)
		{
			return -1;
		}
		UElysiumGameStateSubsystem* S = GStore();
		if (!S)
		{
			return 0; // no store bound yet -- silently drop (PoC)
		}
		const FString Key(UTF8_TO_TCHAR(Name));
		if (Value == nullptr || Value == Py_None)
		{
			S->ClearGlobal(Key); // assigning None deletes, like tp_setattr
		}
		else
		{
			S->SetGlobal(Key, PyToVariant(Value));
		}
		return 0;
	}

	PyObject* PyG_keys(PyObject* /*Self*/, PyObject* /*Args*/)
	{
		PyObject* List = PyList_New(0);
		if (UElysiumGameStateSubsystem* S = GStore())
		{
			for (const FString& K : S->GlobalKeys())
			{
				PyObject* Str = PyString_FromString(TCHAR_TO_UTF8(*K));
				PyList_Append(List, Str);
				Py_DECREF(Str);
			}
		}
		return List;
	}

	PyObject* PyG_has_key(PyObject* /*Self*/, PyObject* Args)
	{
		const char* Name = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Name))
		{
			return nullptr;
		}
		UElysiumGameStateSubsystem* S = GStore();
		const bool bHas = S && S->HasGlobal(FString(UTF8_TO_TCHAR(Name)));
		return PyBool_FromLong(bHas ? 1 : 0);
	}

	PyObject* PyG_ClearAll(PyObject* /*Self*/, PyObject* /*Args*/)
	{
		if (UElysiumGameStateSubsystem* S = GStore())
		{
			S->ClearAllGlobals();
		}
		Py_RETURN_NONE;
	}

	PyMethodDef GMethods[] =
	{
		{ "keys",     PyG_keys,     METH_NOARGS,  "G.keys() -> list of set flag names" },
		{ "has_key",  PyG_has_key,  METH_VARARGS, "G.has_key(name) -> bool" },
		{ "ClearAll", PyG_ClearAll, METH_NOARGS,  "G.ClearAll() -> clear every flag" },
		{ nullptr, nullptr, 0, nullptr }
	};

	// Head-init + the two sized fields; every other slot is zeroed and filled in InitVampireModule.
	PyTypeObject GType =
	{
		PyVarObject_HEAD_INIT(nullptr, 0)
		"vampire.G",        // tp_name
		sizeof(PyObject),   // tp_basicsize
	};

	// --- the `vampire` module: only _log + G are real; the natives live in the Python bootstrap ---

	PyObject* Vampire_log(PyObject* /*Self*/, PyObject* Args)
	{
		int Level = 0;
		const char* Msg = nullptr;
		if (!PyArg_ParseTuple(Args, "is", &Level, &Msg))
		{
			return nullptr;
		}
		const FString M(UTF8_TO_TCHAR(Msg));
		if (Level == 0) { UE_LOG(LogElysiumPy, Log, TEXT("%s"), *M); }
		else            { UE_LOG(LogElysiumPy, Warning, TEXT("%s"), *M); }
		Py_RETURN_NONE;
	}

	PyMethodDef VampireMethods[] =
	{
		{ "_log", Vampire_log, METH_VARARGS, "internal: route Python stdout/stderr to the UE log" },
		{ nullptr, nullptr, 0, nullptr }
	};

	bool InitVampireModule(FString& OutError)
	{
		GType.tp_flags     = Py_TPFLAGS_DEFAULT;
		GType.tp_getattro  = PyG_getattro;
		GType.tp_setattro  = PyG_setattro;
		GType.tp_methods   = GMethods;
		GType.tp_doc       = "VtMB global flag store (G) -- proxied onto UElysiumGameStateSubsystem";
		if (PyType_Ready(&GType) < 0)
		{
			OutError = FString::Printf(TEXT("PyType_Ready(vampire.G) failed: %s"), *FetchPyError());
			return false;
		}

		PyObject* Module = Py_InitModule3("vampire", VampireMethods,
			"Elysium VtMB bridge (PoC: real G store; natives stubbed in the Python bootstrap)");
		if (!Module)
		{
			OutError = FString::Printf(TEXT("Py_InitModule3(vampire) failed: %s"), *FetchPyError());
			return false;
		}

		PyObject* G = PyObject_New(PyObject, &GType);
		if (!G)
		{
			OutError = TEXT("PyObject_New(vampire.G) failed");
			return false;
		}
		PyModule_AddObject(Module, "G", G); // steals the reference

		Py_INCREF(Py_None);
		PyModule_AddObject(Module, "null", Py_None); // VtMB scripts use bare `null`
		return true;
	}

	// The bootstrap: the ONE real binding (__main__.G = vampire.G) plus forgiving stubs for every
	// native 9.3 will replace with a C binding, and a stub `vamputil` so `from vamputil import *`
	// is a cheap success. This is the exact surface that let the offline harness import tutorial.py.
	const char* const BOOTSTRAP =
		"import sys, types, vampire, __main__\n"
		"class _Log(object):\n"
		"    def __init__(s, l): s.l = l; s.b = ''\n"
		"    def write(s, t):\n"
		"        s.b += t\n"
		"        while '\\n' in s.b:\n"
		"            ln, s.b = s.b.split('\\n', 1); vampire._log(s.l, ln)\n"
		"    def flush(s):\n"
		"        if s.b: vampire._log(s.l, s.b); s.b = ''\n"
		"sys.stdout = _Log(0); sys.stderr = _Log(1)\n"
		"__main__.G = vampire.G\n"
		"__main__.null = None\n"
		"class _StubEnt(object):\n"
		"    def __getattr__(s, k): return _StubEnt._c\n"
		"    @staticmethod\n"
		"    def _c(*a, **k): return _StubEnt()\n"
		"    def __nonzero__(s): return True\n"
		"    def __repr__(s): return '<stub-entity>'\n"
		"def _mk(nm):\n"
		"    def f(*a, **k): return _StubEnt()\n"
		"    f.__name__ = nm; return f\n"
		"for _nm in ('FindPlayer','FindEntityByName','FindEntitiesByName','FindEntitiesByClass',\n"
		"            'ScheduleTask','ChangeMap','OneOfSet','IsClan','IsIdling','IsPCMalk',\n"
		"            'CreateEntityNoSpawn','CallEntitySpawn','SquadSeesPlayer'):\n"
		"    setattr(__main__, _nm, _mk(_nm))\n"
		"if 'vamputil' not in sys.modules:\n"
		"    _vu = types.ModuleType('vamputil'); _vu.__all__ = []; sys.modules['vamputil'] = _vu\n";

	// Run a code block in __main__; capture any exception text. Returns true on success.
	bool RunRaw(const FString& Code, FString& OutError)
	{
		PyObject* Main = PyImport_AddModule("__main__"); // borrowed
		PyObject* Dict = Main ? PyModule_GetDict(Main) : nullptr; // borrowed
		if (!Dict)
		{
			OutError = TEXT("no __main__ dict");
			return false;
		}
		PyObject* R = PyRun_String(TCHAR_TO_UTF8(*Code), Py_file_input, Dict, Dict);
		if (!R)
		{
			OutError = FetchPyError();
			return false;
		}
		Py_DECREF(R);
		return true;
	}
}

#endif // ELYSIUM_WITH_CPYTHON

// ---------------------------------------------------------------------------------------------
// FElysiumPythonVM -- the same interface whether or not CPython is compiled in.
// ---------------------------------------------------------------------------------------------

FElysiumPythonVM& FElysiumPythonVM::Get()
{
	static FElysiumPythonVM Instance;
	return Instance;
}

bool FElysiumPythonVM::IsAvailable()
{
#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON
	return true;
#else
	return false;
#endif
}

void FElysiumPythonVM::SetGameState(UElysiumGameStateSubsystem* InState)
{
	GameStateWeak = InState;
}

UElysiumGameStateSubsystem* FElysiumPythonVM::GameState() const
{
	return GameStateWeak.Get();
}

#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

bool FElysiumPythonVM::EnsureStarted(FString& OutError)
{
	if (bStarted)
	{
		return true;
	}

	// python27.dll is statically imported (linked via python27.lib) and already loaded by the OS
	// by the time we get here, next to the module binary -- no GetDllHandle needed. We only resolve
	// PythonHome (the vendored stdlib) so Py_Initialize can bootstrap os/string/etc.
	const FString PyRoot  = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/ElysiumUE/ThirdParty/CPython27")));
	const FString HomeDir = FPaths::Combine(PyRoot, TEXT("PythonHome"));

	// Py2 stores the home pointer verbatim -- keep a process-lifetime ANSI buffer alive.
	static TArray<char> HomeBuf;
	{
		const FTCHARToUTF8 Conv(*HomeDir);
		HomeBuf.SetNumUninitialized(Conv.Length() + 1);
		FMemory::Memcpy(HomeBuf.GetData(), Conv.Get(), Conv.Length() + 1);
	}
	static char ProgName[] = "ElysiumUE";

	Py_NoSiteFlag          = 1; // skip site.py; we import exactly what the scripts need
	Py_IgnoreEnvironmentFlag = 1; // ignore host PYTHONHOME/PYTHONPATH
	Py_NoUserSiteDirectory = 1;
	Py_SetPythonHome(HomeBuf.GetData());
	Py_SetProgramName(ProgName);
	// Py2.7 runs as its own DLL/interpreter, isolated from UE's embedded Python 3 (verified: this
	// reads already-init=0 even while PythonScriptPlugin has Py3 up).
	UE_LOG(LogElysiumPy, Verbose, TEXT("EnsureStarted: PythonHome=%s ; Py_Initialize (py2 already-init=%d)"),
		*HomeDir, Py_IsInitialized());

	Py_Initialize();
	if (!Py_IsInitialized())
	{
		OutError = TEXT("Py_Initialize failed");
		return false;
	}

	if (!InitVampireModule(OutError))
	{
		return false;
	}

	// Shared level-script root on sys.path (per-map dirs are added by LoadLevelScript).
	const FString ScriptsDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("tools/out/scripts"))).Replace(TEXT("\\"), TEXT("/"));
	FString PathErr;
	RunRaw(FString::Printf(TEXT("import sys\nsys.path.insert(0, u'%s')\n"), *ScriptsDir), PathErr);

	if (!RunRaw(FString(ANSI_TO_TCHAR(BOOTSTRAP)), OutError))
	{
		OutError = FString::Printf(TEXT("bootstrap failed: %s"), *OutError);
		return false;
	}

	bStarted = true;
	UE_LOG(LogElysiumPy, Display, TEXT("Embedded CPython VM started: %s"), *GetVersion());
	return true;
}

namespace
{
	// The dict to eval field-6 / callbacks in: the loaded level module, else __main__. Borrowed.
	PyObject* EvalNamespace(const FString& LoadedModule)
	{
		if (!LoadedModule.IsEmpty())
		{
			if (PyObject* Mod = PyImport_AddModule(TCHAR_TO_UTF8(*LoadedModule))) // borrowed
			{
				return PyModule_GetDict(Mod); // borrowed
			}
		}
		PyObject* Main = PyImport_AddModule("__main__");
		return Main ? PyModule_GetDict(Main) : nullptr;
	}
}

bool FElysiumPythonVM::RunSimpleString(const FString& Code, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return false;
	}
	return RunRaw(Code, OutError);
}

FElysiumVariant FElysiumPythonVM::Eval(const FString& Source, const FElysiumScriptContext& /*Ctx*/, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return FElysiumVariant::Void();
	}
	PyObject* Ns = EvalNamespace(LoadedModule);
	if (!Ns)
	{
		OutError = TEXT("no eval namespace");
		return FElysiumVariant::Void();
	}

	// Try as an expression (pythoncheck gates); fall back to a statement block (field-6 assigns).
	PyObject* R = PyRun_String(TCHAR_TO_UTF8(*Source), Py_eval_input, Ns, Ns);
	if (!R)
	{
		if (PyErr_ExceptionMatches(PyExc_SyntaxError))
		{
			PyErr_Clear();
			PyObject* R2 = PyRun_String(TCHAR_TO_UTF8(*Source), Py_file_input, Ns, Ns);
			if (!R2)
			{
				OutError = FetchPyError();
				return FElysiumVariant::Void();
			}
			Py_DECREF(R2);
			return FElysiumVariant::Void(); // a statement has no value
		}
		OutError = FetchPyError(); // real runtime error -> error-to-false
		return FElysiumVariant::Void();
	}
	const FElysiumVariant V = PyToVariant(R);
	Py_DECREF(R);
	return V;
}

bool FElysiumPythonVM::LoadLevelScript(const FString& AbsPath, FString& OutModuleName, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return false;
	}
	const FString Dir     = FPaths::GetPath(AbsPath).Replace(TEXT("\\"), TEXT("/"));
	const FString ModName = FPaths::GetBaseFilename(AbsPath);

	FString PathErr;
	RunRaw(FString::Printf(TEXT("import sys\nsys.path.insert(0, u'%s')\n"), *Dir), PathErr);

	PyObject* Mod = PyImport_ImportModule(TCHAR_TO_UTF8(*ModName));
	if (!Mod)
	{
		OutError = FetchPyError();
		return false;
	}
	Py_DECREF(Mod);
	LoadedModule   = ModName;
	OutModuleName  = ModName;
	UE_LOG(LogElysiumPy, Display, TEXT("Loaded level script: %s (%s)"), *ModName, *AbsPath);
	return true;
}

bool FElysiumPythonVM::FireCallback(const FString& FuncName, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return false;
	}
	PyObject* Ns = EvalNamespace(LoadedModule);
	PyObject* Fn = Ns ? PyDict_GetItemString(Ns, TCHAR_TO_UTF8(*FuncName)) : nullptr; // borrowed
	if (!Fn || !PyCallable_Check(Fn))
	{
		OutError = FString::Printf(TEXT("no callable '%s' in %s"), *FuncName,
			LoadedModule.IsEmpty() ? TEXT("__main__") : *LoadedModule);
		return false;
	}
	PyObject* R = PyObject_CallObject(Fn, nullptr);
	if (!R)
	{
		OutError = FetchPyError();
		return false;
	}
	Py_DECREF(R);
	return true;
}

FString FElysiumPythonVM::GetVersion() const
{
	if (!bStarted)
	{
		return TEXT("(not started)");
	}
	const char* Ver = Py_GetVersion();
	FString V(UTF8_TO_TCHAR(Ver));
	V.ReplaceInline(TEXT("\n"), TEXT(" "));
	return V;
}

TArray<FString> FElysiumPythonVM::GetSysPath() const
{
	TArray<FString> Out;
	if (!bStarted)
	{
		return Out;
	}
	if (PyObject* Path = PySys_GetObject(const_cast<char*>("path"))) // borrowed
	{
		const Py_ssize_t N = PyList_Check(Path) ? PyList_Size(Path) : 0;
		for (Py_ssize_t i = 0; i < N; ++i)
		{
			PyObject* Item = PyList_GetItem(Path, i); // borrowed
			if (Item && PyString_Check(Item))
			{
				Out.Add(FString(UTF8_TO_TCHAR(PyString_AsString(Item))));
			}
		}
	}
	return Out;
}

TArray<FString> FElysiumPythonVM::GetModuleCallables(const FString& Filter) const
{
	TArray<FString> Out;
	if (!bStarted)
	{
		return Out;
	}
	PyObject* Ns = EvalNamespace(LoadedModule);
	if (!Ns || !PyDict_Check(Ns))
	{
		return Out;
	}
	PyObject *Key = nullptr, *Value = nullptr;
	Py_ssize_t Pos = 0;
	while (PyDict_Next(Ns, &Pos, &Key, &Value))
	{
		if (!Key || !PyString_Check(Key) || !Value || !PyFunction_Check(Value))
		{
			continue; // only top-level def'd functions
		}
		const FString Name(UTF8_TO_TCHAR(PyString_AsString(Key)));
		if (Filter.IsEmpty() || Name.Contains(Filter))
		{
			Out.Add(Name);
		}
	}
	Out.Sort();
	return Out;
}

#else // ELYSIUM_WITH_CPYTHON == 0 -- inert stub

bool FElysiumPythonVM::EnsureStarted(FString& OutError)
{
	OutError = TEXT("built without CPython (ELYSIUM_WITH_CPYTHON=0)");
	return false;
}
bool FElysiumPythonVM::RunSimpleString(const FString&, FString& OutError) { OutError = TEXT("no cpython"); return false; }
FElysiumVariant FElysiumPythonVM::Eval(const FString&, const FElysiumScriptContext&, FString& OutError) { OutError = TEXT("no cpython"); return FElysiumVariant::Void(); }
bool FElysiumPythonVM::LoadLevelScript(const FString&, FString&, FString& OutError) { OutError = TEXT("no cpython"); return false; }
bool FElysiumPythonVM::FireCallback(const FString&, FString& OutError) { OutError = TEXT("no cpython"); return false; }
FString FElysiumPythonVM::GetVersion() const { return TEXT("(no cpython)"); }
TArray<FString> FElysiumPythonVM::GetSysPath() const { return {}; }
TArray<FString> FElysiumPythonVM::GetModuleCallables(const FString&) const { return {}; }

#endif // ELYSIUM_WITH_CPYTHON

// ---------------------------------------------------------------------------------------------
// Console verbs (dev only). These resolve the game-state store from the live world so the VM's
// G proxy is bound even when the CPython script host is not installed.
// ---------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING

namespace
{
	UElysiumGameStateSubsystem* ResolveGameState(UWorld* World)
	{
		if (World)
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				return GI->GetSubsystem<UElysiumGameStateSubsystem>();
			}
		}
		return nullptr;
	}

	void BindStore(UWorld* World)
	{
		if (UElysiumGameStateSubsystem* S = ResolveGameState(World))
		{
			FElysiumPythonVM::Get().SetGameState(S);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumPySmoke(
	TEXT("elysium.py.smoke"),
	TEXT("Start the embedded CPython VM and run a smoke test (version, print, stdlib import)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		BindStore(World);
		FString Err;
		if (!FElysiumPythonVM::Get().EnsureStarted(Err))
		{
			UE_LOG(LogElysiumPy, Error, TEXT("VM start failed: %s"), *Err);
			return;
		}
		FString RunErr;
		const bool bOk = FElysiumPythonVM::Get().RunSimpleString(
			TEXT("import sys, random, time, struct, string\n")
			TEXT("print 'CPython', sys.version.split()[0], 'embedded in Unreal -- stdlib OK'\n")
			TEXT("G.ElysiumPySmoke = 1\n")
			TEXT("print 'G round-trip: G.ElysiumPySmoke =', G.ElysiumPySmoke, '  keys =', G.keys()\n"),
			RunErr);
		UE_LOG(LogElysiumPy, Display, TEXT("smoke %s%s"),
			bOk ? TEXT("OK") : TEXT("FAILED: "), bOk ? TEXT("") : *RunErr);
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumPyExec(
	TEXT("elysium.py.exec"),
	TEXT("elysium.py.exec <python> -- run a statement/expression in the current script namespace."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		BindStore(World);
		const FString Code = FString::Join(Args, TEXT(" "));
		FString Err;
		const FElysiumVariant V = FElysiumPythonVM::Get().Eval(Code, FElysiumScriptContext(), Err);
		if (!Err.IsEmpty()) { UE_LOG(LogElysiumPy, Warning, TEXT("exec error: %s"), *Err); }
		else                { UE_LOG(LogElysiumPy, Display, TEXT("exec -> %s"), *V.Describe()); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumPyLoad(
	TEXT("elysium.py.load"),
	TEXT("elysium.py.load [map] -- import a real level script (default sp_tutorial_1 -> tutorial.py)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		BindStore(World);
		const FString Map = Args.Num() > 0 ? Args[0] : TEXT("tutorial");
		// scripts live at tools/out/scripts/<name>/<name>.py (tutorial, downtown, ...).
		const FString Abs = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("tools/out/scripts"), Map, Map + TEXT(".py")));
		FString ModName, Err;
		if (FElysiumPythonVM::Get().LoadLevelScript(Abs, ModName, Err))
		{
			const TArray<FString> Cbs = FElysiumPythonVM::Get().GetModuleCallables(TEXT("On"));
			UE_LOG(LogElysiumPy, Display, TEXT("loaded '%s' -- %d On* callbacks"), *ModName, Cbs.Num());
		}
		else
		{
			UE_LOG(LogElysiumPy, Error, TEXT("load '%s' failed: %s"), *Map, *Err);
		}
	}));

// One-shot end-to-end PoC: proves the vendored interpreter loads + runs inside the built game,
// the vampire/G binding round-trips with the C++ store, a real level script imports and its
// callbacks run, and a field-6 statement resolves a level-script constant. Single token (no args)
// so it survives -ExecCmds. Logs one "POC" line per step + a final verdict.
static FAutoConsoleCommandWithWorldAndArgs GElysiumPyPoc(
	TEXT("elysium.py.poc"),
	TEXT("Run the P5.5 embedded-CPython PoC end-to-end and log a PASS/FAIL summary."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		FElysiumPythonVM& VM = FElysiumPythonVM::Get();
		UElysiumGameStateSubsystem* S = ResolveGameState(World);
		VM.SetGameState(S);
		bool bAll = true;
		FString Err;

		// 1) start + version
		const bool bStart = VM.EnsureStarted(Err);
		bAll &= bStart;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 1/5 start: %s  %s"),
			bStart ? TEXT("PASS") : TEXT("FAIL"), bStart ? *VM.GetVersion() : *Err);

		// 2) stdlib + G round-trip through the C++ store
		const bool bSmoke = VM.RunSimpleString(
			TEXT("import random, time, struct, string\nG.PocFlag = 4242\n"), Err);
		const int32 Read = S ? S->GetGlobalInt(TEXT("PocFlag")) : -1;
		const bool bRoundTrip = bSmoke && Read == 4242;
		bAll &= bRoundTrip;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 2/5 stdlib+G round-trip: %s  (C++ read G.PocFlag = %d)"),
			bRoundTrip ? TEXT("PASS") : TEXT("FAIL"), Read);

		// 3) import the real tutorial.py level script
		const FString Abs = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("tools/out/scripts/tutorial/tutorial.py")));
		FString ModName;
		const bool bLoad = VM.LoadLevelScript(Abs, ModName, Err);
		bAll &= bLoad;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 3/5 load tutorial.py: %s  %s"),
			bLoad ? TEXT("PASS") : TEXT("FAIL"), bLoad ? *ModName : *Err);

		// 4) fire a real On* callback (its body runs; stub natives absorb the entity calls)
		const bool bFire = VM.FireCallback(TEXT("OnKillDisc1"), Err);
		bAll &= bFire;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 4/5 fire OnKillDisc1(): %s  %s"),
			bFire ? TEXT("PASS") : TEXT("FAIL"), bFire ? TEXT("") : *Err);

		// 5) field-6: a statement that resolves the level-script constant cCelerity (=8) into G --
		// the exact acceptance ElysiumExpr cannot meet (cCelerity is NameError there).
		if (S) { S->SetGlobalInt(TEXT("Tutorial_Discflags"), 0); }
		VM.Eval(TEXT("G.Tutorial_Discflags = G.Tutorial_Discflags | cCelerity"), FElysiumScriptContext(), Err);
		const int32 Disc = S ? S->GetGlobalInt(TEXT("Tutorial_Discflags")) : -1;
		const bool bField6 = Disc == 8;
		bAll &= bField6;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 5/5 field-6 |= cCelerity: %s  (G.Tutorial_Discflags = %d, want 8)"),
			bField6 ? TEXT("PASS") : TEXT("FAIL"), Disc);

		UE_LOG(LogElysiumPy, Display, TEXT("POC VERDICT: %s"), bAll ? TEXT("ALL PASS") : TEXT("FAILURES ABOVE"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumPyFire(
	TEXT("elysium.py.fire"),
	TEXT("elysium.py.fire <Func> -- call a top-level callback (e.g. OnMasqueradeEnd) in the loaded script."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		BindStore(World);
		if (Args.Num() == 0)
		{
			UE_LOG(LogElysiumPy, Warning, TEXT("usage: elysium.py.fire <FunctionName>"));
			return;
		}
		FString Err;
		if (FElysiumPythonVM::Get().FireCallback(Args[0], Err))
		{
			UE_LOG(LogElysiumPy, Display, TEXT("fired %s"), *Args[0]);
		}
		else
		{
			UE_LOG(LogElysiumPy, Warning, TEXT("fire %s: %s"), *Args[0], *Err);
		}
	}));

#endif // !UE_BUILD_SHIPPING
