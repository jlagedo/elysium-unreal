#pragma once

// Isolated include of the vendored CPython 2.7 headers (qnox/python-2.7, ThirdParty/CPython27).
// ONLY .cpp files that call the Python C-API include this, and they include it LAST -- after all
// Unreal headers -- because pyconfig.h remaps a few libc names on MSVC (hypot -> _hypot, ...) and
// pulls system headers that would otherwise leak into UE code compiled in the same translation unit.
//
// When the module is built without CPython (non-Win64, or the vendored SDK is absent) the whole
// header is empty and ELYSIUM_WITH_CPYTHON is 0 -- the VM compiles to an inert stub.

#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

#include "CoreMinimal.h" // defines THIRD_PARTY_INCLUDES_START/END, and the UE macros Python won't touch

THIRD_PARTY_INCLUDES_START

// pyconfig.h keys BOTH its ABI (Py_DEBUG changes PyObject layout) and its #pragma auto-link
// (python27_d.lib) off _DEBUG. The vendored DLL is a release, non-Py_DEBUG build shipping only
// python27.lib, so a UE config that defines _DEBUG would select a missing import lib AND mismatch
// struct layouts -> crash. Undef _DEBUG strictly across the Python include, then restore it. This
// is the same shim UE's own PythonScriptPlugin uses.
#pragma push_macro("_DEBUG")
#ifdef _DEBUG
	#undef _DEBUG
#endif

// Python 2 headers still spell some slots with the old MSVC-reserved forms; keep warnings quiet.
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable: 4510 4512 4610) // no default/assignment ctor on some Py structs
	#pragma warning(disable: 5033)           // Py2 headers still spell params `register`
#endif

#include <Python.h>

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

#pragma pop_macro("_DEBUG")

THIRD_PARTY_INCLUDES_END

#endif // ELYSIUM_WITH_CPYTHON
