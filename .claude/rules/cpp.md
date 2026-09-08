---
paths:
  - "Source/**/*.cpp"
  - "Source/**/*.h"
  - "Source/**/*.Build.cs"
---

# C++ coding policy

The baseline for new and touched runtime code is Epic's
[C++ Coding Standard for Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine).
Use Epic's [Object Pointers](https://dev.epicgames.com/documentation/unreal-engine/object-pointers-in-unreal-engine),
[Reflection System](https://dev.epicgames.com/documentation/unreal-engine/reflection-system-in-unreal-engine)
and [Include What You Use](https://dev.epicgames.com/documentation/unreal-engine/include-what-you-use-iwyu-for-unreal-engine-programming)
guides for the corresponding engine semantics. This is a code and review contract, not a license
header: **never copy Epic's copyright notice into project-owned source.** Repository
`.clang-format` owns C++ layout and `.editorconfig` owns the remaining whitespace rules. Format
touched code only; do not mix a formatting sweep with a behavioral change.

- **Language and portability:** UE 5.8 code is C++20, constrained by Epic's cross-compiler rules.
  Prefer `nullptr`, `override`/`final`, `static_assert`, range-based loops, strongly typed enums and
  const-correct code. Keep types explicit; use `auto` only for lambdas, unwieldy iterators or
  template cases where spelling the type harms clarity. Use explicit lambda captures, especially
  for deferred work, and never capture a short-lived reference or an untracked `UObject` into it.
- **Unreal names and reflection:** use the `U`/`A`/`F`/`T`/`I`/`S`/`E` prefixes and `b` for
  booleans; boolean queries read as questions. Add `UCLASS`, `USTRUCT`, `UFUNCTION` and `UPROPERTY`
  only where the engine must see the type or member. The plain-C++ substrate remains
  reflection-free.
- **Object references:** a persistent, engine-tracked `UObject` field uses `UPROPERTY()` with
  `TObjectPtr<T>`. Short-lived locals and parameters normally use `T*`; expiring non-owning
  references use `TWeakObjectPtr<T>`; load-on-demand asset references use `TSoftObjectPtr<T>`.
  `TStrongObjectPtr<T>` is reserved for the uncommon strong reference owned outside a `UObject`.
- **Headers and modules:** every header includes what it needs; every `.cpp` includes its matching
  header first. Prefer forward declarations and fine-grained includes, never `Engine.h` or
  `UnrealEd.h`, and do not put `using` declarations in global scope. Dependencies belong in the
  narrowest correct `Build.cs` list. A private header is included by its layer path
  (`#include "Visual/ElysiumLightRig.h"`), so a cross-layer dependency is visible at the top of the
  file; `Public/` stays flat, because it is the module's API surface rather than a layering.
- **File granularity:** one primary class — or one small, tightly coupled cluster (a class plus
  its private helpers) — per `.h`/`.cpp`, under the owning layer folder. A new class never lands
  inside an existing multi-class file. When a change substantially touches a class that lives in
  an oversized multi-class file, first move that class verbatim into its own file (includes by
  layer path, a thin registration site may remain behind), then make the behavioral edit — and
  keep the verbatim move and the behavioral change reviewable as separate diffs (separate
  commits when both land together). Pure moves change no behavior and no names.
- **APIs and diagnostics:** avoid boolean flag lists and long parameter lists; use an enum or a
  parameter struct. Use `TEXT()` for Unreal string literals, sized integers for serialized or
  replicated formats, named log categories, and the appropriate `check`/`verify`/`ensure` family.
  Address compiler warnings. Comments explain intent, units, constraints and non-obvious safety,
  not a paraphrase of the implementation.
