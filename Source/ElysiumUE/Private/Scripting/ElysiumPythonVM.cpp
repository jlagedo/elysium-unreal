#include "Scripting/ElysiumPythonVM.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "Scripting/ElysiumPythonEntity.h"
#include "Scripting/ElysiumScriptFS.h"
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

// Everything from here to InitVampireModule is compiled only with the vendored CPython SDK.
// Without it the VM is an inert stub (every method reports "built without CPython").
#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

namespace
{
	// Marshalling + error fetch live with the entity/native bindings (ElysiumPythonEntity), since
	// a marshalled value may itself be an entity object.
	using ElysiumPy::FetchPyError;
	using ElysiumPy::PyToVariant;
	using ElysiumPy::VariantToPy;

	// The `vampire.G` proxy type: attribute access <-> the C++ game-state store.
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
			return 0; // no store bound yet -- drop the write
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

	// G's mapping protocol: `G[k]` IS `G.k`.
	// PyDataManager carries a tp_as_mapping (type object 0x1058fa08 -> 0x1058f9f8), and both slots
	// are thin adapters over the attribute path: mp_subscript (0x1019b4a0) checks the key is a
	// PyString, converts it with PyString_AsString, and TAIL-JUMPS into tp_getattr; mp_ass_subscript
	// (0x1019b720) calls tp_setattr the same way. So subscripting inherits everything the attribute
	// path has — the method table, default-on-miss integer 0 — and needs no store of its own.
	// This is load-bearing for the tutorial: DialogPostProcess's first act is saveState(), which is
	// `for k in G.keys(): G_tut[k] = G[k]`.

	PyObject* PyG_subscript(PyObject* Self, PyObject* Key)
	{
		if (!PyString_Check(Key))
		{
			return PyInt_FromLong(0);   // retail: a non-string key returns integer 0
		}
		return PyG_getattro(Self, Key);
	}

	int PyG_ass_subscript(PyObject* Self, PyObject* Key, PyObject* Value)
	{
		if (!PyString_Check(Key))
		{
			// Retail tail-calls PyDict_SetItem with the manager object in the dict slot — a latent
			// bug no shipped script reaches (every G key is a string). Raise instead of reproducing it.
			PyErr_SetString(PyExc_TypeError, "G keys must be strings");
			return -1;
		}
		return PyG_setattro(Self, Key, Value);
	}

	Py_ssize_t PyG_length(PyObject*)
	{
		UElysiumGameStateSubsystem* S = GStore();
		return S ? static_cast<Py_ssize_t>(S->GlobalKeys().Num()) : 0;
	}

	PyMappingMethods GMapping = { PyG_length, PyG_subscript, PyG_ass_subscript };

	// Head-init + the two sized fields; every other slot is zeroed and filled in InitVampireModule.
	PyTypeObject GType =
	{
		PyVarObject_HEAD_INIT(nullptr, 0)
		"vampire.G",        // tp_name
		sizeof(PyObject),   // tp_basicsize
	};

	// The `vampire` module: only _log + G are real; the natives live in the Python bootstrap.

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

	// The script filesystem natives (FElysiumScriptFS).
	// The VM's file layer is a path rewriter, not a file implementation: these hand back a real path
	// and the shim then calls the real `open`/`nt.*` on it, so script code keeps getting genuine
	// `file` objects (readlines, binary mode, seek/truncate all work unchanged).

	PyObject* Vampire_fs_resolve(PyObject* /*Self*/, PyObject* Args)
	{
		const char* Path = nullptr;
		const char* Mode = "r";
		if (!PyArg_ParseTuple(Args, "s|s", &Path, &Mode))
		{
			return nullptr;
		}
		FString Real, Err;
		const EElysiumFsAccess Access = FElysiumScriptFS::AccessFromMode(UTF8_TO_TCHAR(Mode));
		if (!FElysiumScriptFS::Resolve(UTF8_TO_TCHAR(Path), Access, Real, Err))
		{
			// Only a sandbox escape lands here. The shim re-raises this as OSError for the `nt.*`
			// callers, because fileutil catches `nt.error` and IOError is its sibling, not its base.
			PyErr_SetString(PyExc_IOError, TCHAR_TO_UTF8(*Err));
			return nullptr;
		}
		return PyString_FromString(TCHAR_TO_UTF8(*Real));
	}

	PyObject* Vampire_fs_getcwd(PyObject* /*Self*/, PyObject* /*Args*/)
	{
		return PyString_FromString(TCHAR_TO_UTF8(*FElysiumScriptFS::VirtualRoot()));
	}

	PyObject* Vampire_fs_listdir(PyObject* /*Self*/, PyObject* Args)
	{
		const char* Path = nullptr;
		if (!PyArg_ParseTuple(Args, "s", &Path))
		{
			return nullptr;
		}
		TArray<FString> Names;
		FString Err;
		if (!FElysiumScriptFS::ListDir(UTF8_TO_TCHAR(Path), Names, Err))
		{
			PyErr_SetString(PyExc_IOError, TCHAR_TO_UTF8(*Err));
			return nullptr;
		}
		PyObject* List = PyList_New(Names.Num());
		if (!List)
		{
			return nullptr;
		}
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			PyList_SET_ITEM(List, i, PyString_FromString(TCHAR_TO_UTF8(*Names[i]))); // steals
		}
		return List;
	}

	PyMethodDef VampireMethods[] =
	{
		{ "_log", Vampire_log, METH_VARARGS, "internal: route Python stdout/stderr to the UE log" },
		{ "_fs_resolve", Vampire_fs_resolve, METH_VARARGS,
			"internal: rewrite a VM-space path to a real one (raises IOError outside the sandbox)" },
		{ "_fs_getcwd", Vampire_fs_getcwd, METH_NOARGS,
			"internal: the VM's virtual install root -- what nt.getcwd() answers" },
		{ "_fs_listdir", Vampire_fs_listdir, METH_VARARGS,
			"internal: the overlay+mirror union listing for a virtual directory" },
		{ nullptr, nullptr, 0, nullptr }
	};

	bool InitVampireModule(FString& OutError)
	{
		GType.tp_flags      = Py_TPFLAGS_DEFAULT;
		GType.tp_getattro   = PyG_getattro;
		GType.tp_setattro   = PyG_setattro;
		GType.tp_as_mapping = &GMapping;
		GType.tp_methods    = GMethods;
		GType.tp_doc        = "VtMB global flag store (G) -- proxied onto UElysiumGameStateSubsystem";
		if (PyType_Ready(&GType) < 0)
		{
			OutError = FString::Printf(TEXT("PyType_Ready(vampire.G) failed: %s"), *FetchPyError());
			return false;
		}

		PyObject* Module = Py_InitModule3("vampire", VampireMethods,
			"Elysium VtMB bridge: the G store, the Entity object, and the 11 module globals");
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

		// The entity/native surface: vampire.Entity, ccmd/cvar, and the 11 globals.
		return ElysiumPy::InstallEntityBindings(Module, OutError);
	}

	// The file-layer shim (FElysiumScriptFS). VtMB's scripts spell paths three ways and only one of
	// them calls a function an embedder can redirect, so the interception has to sit under `open`
	// and the `nt` surface rather than over `getcwd`. Every wrapper does the same thing: rewrite the
	// path through `vampire._fs_resolve`, then call the original — script code keeps getting real
	// `file` objects, and the whole policy stays in C++.
	//
	// Installed before the main bootstrap so nothing can touch a file unshimmed, and wrapped in a
	// function because this runs in `__main__` — the bus every script name resolves against, which
	// must not collect the loop temporaries.
	const char* const FS_SHIM =
		"import sys, nt, __builtin__, vampire\n"
		"sys.moddir = 'Vampire'\n"
		"def _elysium_install_fs():\n"
		"    _resolve = vampire._fs_resolve\n"
		"    _real_open = __builtin__.open\n"
		"    def _open(name, mode='r', *rest):\n"
		"        return _real_open(_resolve(name, mode), mode, *rest)\n"
		"    __builtin__.open = _open\n"
		// _fs_resolve raises IOError; `nt.*` callers catch `nt.error` (OSError), and the two are
		// siblings under EnvironmentError, not parent and child -- so fileutil's `except nt.error`
		// would let a denial through. Re-raise per surface.
		"    def _res_nt(path, mode):\n"
		"        try:\n"
		"            return _resolve(path, mode)\n"
		"        except IOError as e:\n"
		"            raise OSError(str(e))\n"
		"    def _wrap(fn, mode):\n"
		"        def w(path, *a): return fn(_res_nt(path, mode), *a)\n"
		"        return w\n"
		"    def _wrap2(fn, m1, m2):\n"
		"        def w(src, dst, *a): return fn(_res_nt(src, m1), _res_nt(dst, m2), *a)\n"
		"        return w\n"
		// `access` resolves for read on purpose: its one caller guards it with `isFile(dst)`
		// (fileutil.py:134), so the path that reaches it already exists, and answering against the
		// mirror copy reports the writability of the write that will actually happen -- into the
		// overlay. Resolving it for write would answer about a file that is not there yet and turn
		// every hunter-mode copy into a spurious "readonly" bail.
		"    for _n, _m in (('stat','r'), ('lstat','r'), ('access','r'), ('utime','w'),\n"
		"                   ('mkdir','w'), ('rmdir','w'), ('unlink','w'), ('remove','w'), ('chmod','w')):\n"
		"        if hasattr(nt, _n): setattr(nt, _n, _wrap(getattr(nt, _n), _m))\n"
		"    if hasattr(nt, 'rename'): nt.rename = _wrap2(nt.rename, 'r', 'w')\n"
		"    nt.listdir = vampire._fs_listdir\n"
		"    nt.getcwd = vampire._fs_getcwd\n"
		"    def _chdir(path): pass\n" // the sandbox has one cwd; no shipped script calls this
		"    nt.chdir = _chdir\n"
		// `os` copies nt's names by value at import time, so a copy taken before now would keep the
		// unshimmed ones. ntpath/genericpath call through `os.` dynamically, so fixing os fixes
		// os.path.exists with it.
		"    if 'os' in sys.modules:\n"
		"        _os = sys.modules['os']\n"
		"        for _n in ('stat','lstat','access','utime','mkdir','rmdir','unlink','remove',\n"
		"                   'chmod','rename','listdir','getcwd','chdir'):\n"
		"            if hasattr(_os, _n): setattr(_os, _n, getattr(nt, _n))\n"
		"if not hasattr(nt, '_elysium_fs'):\n"
		"    _elysium_install_fs()\n"
		"    nt._elysium_fs = 1\n"
		"del _elysium_install_fs\n";

	// The bootstrap: star-import the `vampire` module into `__main__` (python_bridge.md — there is
	// no `import vampire` in any script; everything reaches the engine through `__main__`), then
	// stand up what is still missing.
	//
	// All 11 module globals, `G`, and the console objects `ccmd`/`cvar` are real
	// bindings. With `ccmd`/`cvar` bound, the REAL vamputil.py imports (its module top-level does
	// `c = __main__.ccmd; cvar = __main__.cvar`), so there is no stub vamputil module —
	// tutorial.py's `from vamputil import *` pulls in the real `unhidePlus`/`setPlus`/`IsClan`/...
	// The `IsClan` binding here is a pre-import fallback so tutorial's `if __main__.IsClan or ...`
	// guard (what triggers that import) short-circuits true before it reaches `IsIdling`; the merge
	// then overrides both with vamputil's real definitions. The FS_SHIM above has already given the
	// VM its own filesystem namespace, so vamputil's file-touching helpers (FixKeyBindings reads
	// cfg/config.cfg) resolve into the content mirror instead of raising.
	const char* const BOOTSTRAP =
		"import sys, vampire, __main__\n"
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
		"__main__.Entity = vampire.Entity\n"
		"__main__.ccmd = vampire.ccmd\n"
		"__main__.cvar = vampire.cvar\n"
		// `Character` is VtMB's player/NPC method class (python_bridge.md). Our 24 Character methods
		// dispatch off the Entity getattro (ElysiumScriptNatives::CallCharacterMethod), not a
		// shared class, so this is a compatibility shim: a mutable old-style class that lets vamputil's
		// `from __main__ import Character` resolve and its `Character.Near = _Near` monkeypatch land
		// (a C extension type would reject attribute assignment). The monkeypatched methods do not
		// reach live C entity instances — the only such patch is `Near`, used by the unused
		// AnimalRadar path — but the import completing is what unblocks the whole real vamputil.
		"class Character: pass\n"
		"__main__.Character = Character\n"
		"for _nm in ('FindPlayer','FindEntityByName','FindEntitiesByName','FindEntitiesByClass',\n"
		"            'ScheduleTask','SquadSeesPlayer','CreateEntityNoSpawn','CallEntitySpawn',\n"
		"            'ChangeMap','OneOfSet','IsPCMalk','IsClan'):\n"
		"    setattr(__main__, _nm, getattr(vampire, _nm))\n"
		"def _mk(nm):\n"
		"    def f(*a, **k): return 0\n"
		"    f.__name__ = nm; return f\n"
		"for _nm in ('IsIdling',):\n"
		"    setattr(__main__, _nm, _mk(_nm))\n"
		// `pc` = the player, the name every dialogue gate and level script reads (pc.clan, pc.base_*,
		// IsClan(pc,...)). Bound here so the name always exists (None before any map builds) and
		// **re-bound per eval** in Eval() alongside `npc`: it is an Entity over a
		// generation-checked handle, and a handle minted by one map is stale in the next.
		"__main__.pc = FindPlayer()\n";

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

// FElysiumPythonVM -- the same interface whether or not CPython is compiled in.

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

	// Point VtMB's file layer at the script filesystem before anything can touch a file. The VM gets
	// its own namespace -- reads served from the content mirror, writes into a Saved/ overlay -- so
	// all three of the scripts' path spellings resolve, and the UE process cwd stays where the
	// engine put it (FElysiumScriptFS explains why moving it is not on the table).
	FElysiumScriptFS::EnsureOverlayRoot();
	FString FsErr;
	if (!RunRaw(FString(ANSI_TO_TCHAR(FS_SHIM)), FsErr))
	{
		OutError = FString::Printf(TEXT("script filesystem shim failed: %s"), *FsErr);
		return false;
	}

	// Shared level-script root on sys.path (per-map dirs are added by LoadLevelScript).
	const FString ScriptsDir = FPaths::ConvertRelativePathToFull(
		FElysiumContentPaths::ScriptsDir()).Replace(TEXT("\\"), TEXT("/"));
	FString PathErr;
	RunRaw(FString::Printf(TEXT("import sys\nsys.path.insert(0, u'%s')\n"), *ScriptsDir), PathErr);

	if (!RunRaw(FString(ANSI_TO_TCHAR(BOOTSTRAP)), OutError))
	{
		OutError = FString::Printf(TEXT("bootstrap failed: %s"), *OutError);
		return false;
	}

	// Seed the console alias/cvar store from out/cfg and wire its Python fallthrough back to us. The
	// console pins Elysium's Plus profile after parsing personal cfg, so `ccmd.patchtype=""` -> alias
	// `patchtype` -> `setPlus()` -> exec in __main__.
	ConsoleStore.SetPythonSink([this](const FString& Line) { return this->ExecConsoleLine(Line); });
	ConsoleStore.LoadFromCfgDir(FElysiumContentPaths::CfgDir());

	bStarted = true;
	UE_LOG(LogElysiumPy, Display, TEXT("Embedded CPython VM started: %s"), *GetVersion());
	return true;
}

namespace
{
	// The dict field-6 payloads, ScheduleTask sources, and callbacks evaluate in: `__main__`.
	// That is where VtMB evaluates them — its dispatch wraps the payload in the format string
	// `__main__.%s` (0x1055e370), so every leading name resolves as an attribute of `__main__`,
	// engine globals and level-script functions alike. LoadLevelScript merges the imported
	// module's public names in, which is what makes `__main__.journalPickup()` resolvable at all.
	// Borrowed.
	PyObject* EvalNamespace()
	{
		PyObject* Main = PyImport_AddModule("__main__");
		return Main ? PyModule_GetDict(Main) : nullptr;
	}

	// The loaded level module's own dict (not the merge), for listing what that script defines.
	PyObject* ModuleNamespace(const FString& LoadedModule)
	{
		if (!LoadedModule.IsEmpty())
		{
			if (PyObject* Mod = PyImport_AddModule(TCHAR_TO_UTF8(*LoadedModule))) // borrowed
			{
				return PyModule_GetDict(Mod); // borrowed
			}
		}
		return EvalNamespace();
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

FElysiumVariant FElysiumPythonVM::Eval(const FString& Source, const FElysiumScriptContext& Ctx, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return FElysiumVariant::Void();
	}
	// Bind the payload's provenance for the duration: entity lookups resolve against the world that
	// is delivering, and anything this eval defers (ScheduleTask/ChangeMap) is attributed to the
	// firing entity as `!self`. Restored on scope exit, so nesting is safe.
	const ElysiumPy::FScopedContext ScopedCtx(Ctx);

	PyObject* Ns = EvalNamespace();
	if (!Ns)
	{
		OutError = TEXT("no eval namespace");
		return FElysiumVariant::Void();
	}

	// `npc` = the firing entity (Self) for this eval — what a dialogue action (`npc.SetDisposition`,
	// `npc.times_talked`) and many level-script payloads read. Bound per-eval from the delivery
	// context (None when there is no firing entity, e.g. a hand-run eval), and left in `__main__`
	// after — harmless, and it matches VtMB keeping `npc` as the last conversation partner.
	if (Ctx.Self.IsSet())
	{
		if (PyObject* NpcObj = ElysiumPy::NewEntity(Ctx.Self))
		{
			PyDict_SetItemString(Ns, "npc", NpcObj);
			Py_DECREF(NpcObj);
		}
	}
	else
	{
		PyDict_SetItemString(Ns, "npc", Py_None);
	}

	// `pc` = the player entity. Re-bound per eval for the same reason `npc` is: it is a
	// generation-checked handle, not a sheet proxy, so the object a previous map minted would
	// raise "game entity has been deleted" on every attribute read after a travel.
	{
		// ScopedCtx is already installed, so CurrentWorld() is this delivery's world (or the current
		// map's for a hand-run eval).
		FElysiumEntityWorld* PlayerWorld = ElysiumPy::CurrentWorld();
		const FElysiumPlayer* PlayerEnt = PlayerWorld ? PlayerWorld->FindPlayer() : nullptr;
		if (PyObject* PcObj = ElysiumPy::NewEntity(PlayerEnt ? PlayerEnt->Handle : FElysiumEntityHandle::Invalid()))
		{
			PyDict_SetItemString(Ns, "pc", PcObj);   // NewEntity answers None for an unset handle
			Py_DECREF(PcObj);
		}
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

	// Take the reserved slot: drop the outgoing map's directory and any earlier copy of this one,
	// then install this one at the front. Only non-empty entries are ever filtered — `''` is Python's
	// own "current directory" entry and removing it would change import resolution. A slice
	// assignment binds no name, so nothing is left behind in the namespace this runs in.
	FString Drop = FString::Printf(TEXT("u'%s',"), *Dir);
	if (!MapScriptPath.IsEmpty() && MapScriptPath != Dir)
	{
		Drop += FString::Printf(TEXT("u'%s',"), *MapScriptPath);
	}
	FString PathErr;
	RunRaw(FString::Printf(
		TEXT("import sys\n")
		TEXT("sys.path[:] = [p for p in sys.path if p not in (%s)]\n")
		TEXT("sys.path.insert(0, u'%s')\n"),
		*Drop, *Dir), PathErr);
	MapScriptPath = Dir;

	PyObject* Mod = PyImport_ImportModule(TCHAR_TO_UTF8(*ModName));
	if (!Mod)
	{
		OutError = FetchPyError();
		return false;
	}

	// Merge the module's public top-level names into `__main__`. VtMB's field-6 dispatch wraps the
	// payload as `__main__.%s`, so a payload naming a level-script function (`journalPickup()`) only
	// resolves if that function is an attribute of `__main__` — the level script's namespace and the
	// bus are the same namespace as far as a payload can see. Merging is also what keeps the ENGINE
	// globals reachable from a payload: `hw_609_1` fires a bare `FindPlayer()`, and hollywood.py
	// (unlike tutorial.py) never aliases it, so only `__main__` can answer.
	// Underscore-prefixed names are skipped, which also leaves __name__/__builtins__/__main__ alone.
	// A function keeps its own module's globals, so a merged `DialogPostProcess` still reads its
	// script's `G_tut`/`Find`/`statemap`; only the entry-point lookup moves.
	if (PyObject* MainDict = EvalNamespace())
	{
		PyObject* ModDict = PyModule_GetDict(Mod);   // borrowed
		PyObject *Key = nullptr, *Value = nullptr;
		Py_ssize_t Pos = 0;
		int32 Merged = 0;
		while (ModDict && PyDict_Next(ModDict, &Pos, &Key, &Value))
		{
			const char* K = (Key && PyString_Check(Key)) ? PyString_AsString(Key) : nullptr;
			if (K && K[0] != '_')
			{
				PyDict_SetItem(MainDict, Key, Value);
				++Merged;
			}
		}
		UE_LOG(LogElysiumPy, Verbose, TEXT("merged %d names from '%s' into __main__"), Merged, *ModName);
	}

	Py_DECREF(Mod);
	LoadedModule   = ModName;
	OutModuleName  = ModName;
	UE_LOG(LogElysiumPy, Display, TEXT("Loaded level script: %s (%s)"), *ModName, *AbsPath);
	return true;
}

void FElysiumPythonVM::ReleaseMapScriptPath()
{
	if (!bStarted || MapScriptPath.IsEmpty())
	{
		return;
	}
	FString PathErr;
	RunRaw(FString::Printf(
		TEXT("import sys\n")
		TEXT("sys.path[:] = [p for p in sys.path if p != u'%s']\n"), *MapScriptPath), PathErr);
	MapScriptPath.Reset();
}

bool FElysiumPythonVM::FireCallback(const FString& FuncName, FString& OutError)
{
	if (!EnsureStarted(OutError))
	{
		return false;
	}
	PyObject* Ns = EvalNamespace();
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

bool FElysiumPythonVM::ExecConsoleLine(const FString& Line)
{
	FString Err;
	if (!EnsureStarted(Err))
	{
		return false;
	}
	PyObject* Ns = EvalNamespace(); // __main__
	if (!Ns)
	{
		return false;
	}
	PyObject* R = PyRun_String(TCHAR_TO_UTF8(*Line), Py_file_input, Ns, Ns);
	if (R)
	{
		Py_DECREF(R);
		return true; // parsed + ran (a level-script function like setPlus())
	}
	// A NameError/SyntaxError means the word is not Python we can run -- it is an engine
	// cvar/command we do not model; report "not Python" so the console drops it quietly.
	if (PyErr_ExceptionMatches(PyExc_NameError) || PyErr_ExceptionMatches(PyExc_SyntaxError))
	{
		PyErr_Clear();
		return false;
	}
	// It WAS Python (all names resolved) but the body raised -- e.g. setPlus()'s unbacked file I/O.
	// Error-to-false, matching every other script eval path: print the traceback and continue.
	PyErr_Print();
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
	PyObject* Ns = ModuleNamespace(LoadedModule);
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
void FElysiumPythonVM::ReleaseMapScriptPath() {}
bool FElysiumPythonVM::FireCallback(const FString&, FString& OutError) { OutError = TEXT("no cpython"); return false; }
bool FElysiumPythonVM::ExecConsoleLine(const FString&) { return false; }
FString FElysiumPythonVM::GetVersion() const { return TEXT("(no cpython)"); }
TArray<FString> FElysiumPythonVM::GetSysPath() const { return {}; }
TArray<FString> FElysiumPythonVM::GetModuleCallables(const FString&) const { return {}; }

#endif // ELYSIUM_WITH_CPYTHON

// Console verbs (dev only). These resolve the game-state store from the live world so the VM's
// G proxy is bound even when the CPython script host is not installed.
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
		// Scripts live under the $ELYSIUM_EXPORT_ROOT.
		const FString Abs = FPaths::ConvertRelativePathToFull(
			FElysiumContentPaths::ScriptModuleFile(Map));
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

// One-shot end-to-end check: proves the vendored interpreter loads + runs inside the built game,
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
		const FString Abs = FPaths::ConvertRelativePathToFull(
			FElysiumContentPaths::ScriptModuleFile(TEXT("tutorial")));
		FString ModName;
		const bool bLoad = VM.LoadLevelScript(Abs, ModName, Err);
		bAll &= bLoad;
		UE_LOG(LogElysiumPy, Display, TEXT("POC 3/5 load tutorial.py: %s  %s"),
			bLoad ? TEXT("PASS") : TEXT("FAIL"), bLoad ? *ModName : *Err);

		// 4) fire a real On* callback. Its body runs against real entity objects, so this
		// step needs sp_tutorial_1 loaded: OnKillDisc1 falls through its clan gates to
		// Find("logic_disc1_nodisc").Trigger(), and off-map that Find is None -> AttributeError.
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

// First-beat acceptance, as one token so it survives -ExecCmds: seed the tutorial's first beat, run
// the level script's own dispatcher through the INSTALLED host (the same path `elysium.exec
// DialogPostProcess()` takes), and check what the script did. The three checks are the ones that
// resolve synchronously; the warp itself is the `env_fade` chain, which runs
// off the queued Fade this reports (watch the position readout, or `elysium.ent_messages 1`).
static FAutoConsoleCommandWithWorldAndArgs GElysiumPyFirstBeat(
	TEXT("elysium.py.firstbeat"),
	TEXT("Run the B2 first-beat acceptance on sp_tutorial_1 and log a PASS/FAIL summary."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		UElysiumGameStateSubsystem* S = ResolveGameState(World);
		FElysiumEntityWorld* W = S ? S->CurrentEntityWorld() : nullptr;
		if (!S || !W)
		{
			UE_LOG(LogElysiumPy, Error, TEXT("firstbeat: needs a loaded map (sp_tutorial_1)"));
			return;
		}
		FElysiumPythonVM::Get().SetGameState(S);

		// 1) the state the beat dispatches on: Jack's dialogue has been had, the patch relocation
		// has not run yet. Seeded here rather than played so the check is one console token.
		S->SetGlobalInt(TEXT("Tut_Jack"), 1);
		S->SetGlobalInt(TEXT("Tut_Patch"), 0);
		UE_LOG(LogElysiumPy, Display, TEXT("FIRSTBEAT 1/3 seed: G.Tut_Jack=1 G.Tut_Patch=0 (host %s, script '%s')"),
			S->ScriptHost().Name(), *S->CurrentLevelScriptModule());

		// 2) tutorial.py's DialogPostProcess() — reaches saveState() (`G_tut[k] = G[k]`, the mapping
		// protocol) before it ever gets to the beat branch.
		FString Err;
		S->EvalScript(TEXT("DialogPostProcess()"), Err);
		const bool bRan = Err.IsEmpty();
		UE_LOG(LogElysiumPy, Display, TEXT("FIRSTBEAT 2/3 DialogPostProcess(): %s  %s"),
			bRan ? TEXT("PASS") : TEXT("FAIL"), bRan ? TEXT("") : *Err);

		// 3) what the branch did: set the patch flag, and fire the fade through the real chokepoint.
		const bool bPatch = S->GetGlobalInt(TEXT("Tut_Patch")) == 1;
		FString FadeLine;
		for (const FElysiumIOEvent& E : W->Queue().Pending())
		{
			if (E.Input == FName(TEXT("Fade")))
			{
				FadeLine = W->FormatEventLine(S->GameClock().GetNow(), E, E.Target, TEXT(""));
				break;
			}
		}
		const bool bQueued = !FadeLine.IsEmpty();
		UE_LOG(LogElysiumPy, Display, TEXT("FIRSTBEAT 3/3 G.Tut_Patch=%d, teleport_fade.Fade queued: %s  %s"),
			S->GetGlobalInt(TEXT("Tut_Patch")), bQueued ? TEXT("PASS") : TEXT("FAIL"), *FadeLine);

		UE_LOG(LogElysiumPy, Display, TEXT("FIRSTBEAT VERDICT: %s"),
			(bRan && bPatch && bQueued) ? TEXT("ALL PASS") : TEXT("FAILURES ABOVE"));
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
