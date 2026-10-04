# T6b — the header pass: the kernel headers' include fan-out (the owner, 2026-10-04)

Closes T6, which is unticked at an edit-mix p90 of 108 s against 90 s. Runs after V3d and before
V4. One agent holds the build, as wave 3 did: the work is measure → cut → rebuild → measure, so
there is no coder fan-out. *Size:* M. *Model:* Opus/high.

## The problem (measured in T6, `spec.md` § Step 1)

- Three headers reach almost every source file:

  | header | reaches | of which |
  |---|---|---|
  | `ElysiumNpcBase.h` | 348 `.cpp` | 114 tests |
  | `ElysiumNpc.h` | 336 `.cpp` | 318 include it directly |
  | `ElysiumEntityWorld.h` | 462 `.cpp` | 456 directly |

- An edit to any of them recompiles 42–47 unity blobs, 870–1,040 s of compile CPU. Wall time
  after the pagefile fix: 94–121 s. Target: edit-mix p90 ≤ 90 s (measured 108 s). A one-`.cpp`
  edit is already 9.3 s, so the cost is the headers only.
- Cause: large class definitions with inline bodies and generated `.inl` files included inside
  the class; most includers need only a handle, an enum or one accessor.
- The 168 default-tier test cases sit in 79 files across 18 of 21 test blobs, so every test blob
  recompiles on a kernel-header edit.

## Why after V3d

V3c and V3d delete large parts of `ElysiumNpc.h` / `ElysiumNpc.cpp` (the arbiter, the executors,
the owner enum, the scripted-move seam). An include rework in parallel collides with every wave.
After V3d the headers are smaller and stable.

## Scope, cheapest first — stop when the target is met

1. **Measure first.** For each of the three headers: which includers need the full class and
   which need only a type. Record the counts.
2. **Forward declarations and narrower includes** in the files that do not need the class.
3. **Light, rarely-changing type headers** (enums, handles, small structs) split out of the three
   headers.
4. **Inline bodies moved to `.cpp`** where they are not hot paths.
5. **Default-tier test cases gathered into fewer files**, so most test blobs leave a kernel
   header's reach.

Out of scope: splitting the classes behind interfaces; a module split. If step 5 still misses the
target, propose the module split as a separate story with its own measurement.

## Rules

- **No behaviour change**: pure include and file-layout work. The default tier, the arm tier and
  the full arena suite give the same verdicts before and after.
- **Build work**: one agent holds the build; nobody else builds while it runs.
- **Generated files** (the `.inl`s, the census, the kernel bindings) are changed through their
  generators, never by hand; `uv run elysium research kernel --check` stays clean.
- The query budget (10 s warns, 60 s stops; never read a file over ~200 KB whole), text through
  the built-in Grep / Read / Glob tools, and a build or a run waited on by blocking or by its
  completion notification, never a sleep or a polling loop.
- Commit once per measured step that holds (build green, verdicts unchanged), so a step that
  does not pay can be dropped without losing the ones before it. Do not push.

## Acceptance

- T6's eight-edit mix re-measured (touch one file, `uv run elysium build`): p90 ≤ 90 s, with the
  three header rows reported before and after.
- Includer counts for the three headers before and after.
- The same test and arena verdicts as before the pass.
- T6 ticked in `spec.md` and `TRACKER.md` with the measured numbers.
