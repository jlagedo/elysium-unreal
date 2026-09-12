---
paths:
  - "Source/**/*.cpp"
  - "Source/**/*.h"
  - "Source/**/*.Build.cs"
---

# C++ coding policy

The baseline is Epic's
[C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)
and [Object Pointers](https://dev.epicgames.com/documentation/unreal-engine/object-pointers-in-unreal-engine)
guide. This is a review contract, not a license header: **never copy Epic's copyright notice into
project-owned source.** `.clang-format` owns C++ layout and `.editorconfig` the remaining whitespace.
Format touched code only; never mix a formatting sweep with a behavioral change.

- **Reflection and the substrate:** `UCLASS`/`USTRUCT`/`UFUNCTION`/`UPROPERTY` only where the engine
  must see the type or member; the plain-C++ substrate stays reflection-free. Reflected types cannot
  live in a namespace (UHT). Even reflection-free code uses UE containers and `FString`, never std
  containers or strings; `std::` is limited to `<atomic>`, `<type_traits>`, `<limits>`, `<cmath>`
  and `<initializer_list>`. `MoveTemp`, not `std::move`.
- **Object references:** a persistent engine-tracked `UObject` field is `UPROPERTY() TObjectPtr<T>`
  (GC-safe only with `UPROPERTY`). Locals and parameters use `T*`; pass `UObject`s by pointer,
  never by reference. Expiring non-owning references use `TWeakObjectPtr<T>`, validated before every
  use, and never as a `TMap` key or `TSet` element (`TObjectKey`). Load-on-demand assets use
  `TSoftObjectPtr<T>`. `TStrongObjectPtr<T>` only for a strong reference owned outside a `UObject`,
  never under `UPROPERTY`, never created or destroyed per frame. Worker threads never dereference a
  `TObjectPtr`; they `Pin()` a weak pointer.
- **Lambdas and `auto`:** explicit captures. Deferred work (timers, delegates, async) captures a
  `TWeakObjectPtr` or binds through `BindWeakLambda`/`CreateWeakLambda` and re-validates inside the
  body; a deferred lambda that must capture anything else says why in a comment. `auto` only for
  lambda bindings, unwieldy iterators and template cases; no structured bindings.
- **Headers and modules:** every header includes what it needs; every `.cpp` includes its own header
  first (Build.cs enforces IWYU). Forward-declare; never `Engine.h` or `UnrealEd.h`. No `using` at
  file scope, in `.cpp` files too: unity builds concatenate them. Dependencies go in the narrowest
  correct `Build.cs` list. A private header is included by its layer path
  (`#include "Visual/ElysiumLightRig.h"`) so a cross-layer dependency is visible at the top of the
  file; `Public/` stays flat because it is the module's API surface, not a layering.
- **File granularity:** one primary class, or one small tightly coupled cluster (a class plus its
  private helpers), per `.h`/`.cpp`, under the owning layer folder. A new class never lands inside
  an existing multi-class file. When a change substantially touches a class in an oversized
  multi-class file, first move that class verbatim into its own file (includes by layer path, a
  thin registration site may remain), then make the behavioral edit, as separate commits when both
  land together. Pure moves change no behavior and no names.
- **APIs:** no boolean parameters except a setter whose bool is the whole state; long parameter
  lists become a struct; no `Handle*`/`Process*` method names.
