#pragma once

#include "CoreMinimal.h"

// The embedded VM's own filesystem namespace (docs/vtmb/python_bridge.md -> "The script filesystem").
//
// VtMB's level scripts were written to run inside the game process, whose cwd is the install folder
// and whose `sys.moddir` is "Vampire". They spell paths three ways, and only the first consults a
// function an embedder can redirect:
//
//   A  `nt.getcwd() + "\\" + moddir + "\\cfg\\config.cfg"`      (vamputil.FixKeyBindings)
//   B  `open(moddir + "/vdata/hackterminals/haven_pc.txt")`     (vamputil.setPlus)
//   C  `open("zvtool_g_dump.txt", "w")`                         (zvtool_file.zdumpg)
//
// B and C hand a relative path straight to the OS, which resolves it against the *process* cwd. The
// process is UnrealEditor.exe, so they miss. Moving the process cwd is not an option: UE resolves
// `FPaths::EngineDir()` from the literal relative string "../../../Engine/" and sets the cwd to
// BaseDir at startup for exactly that reason (GenericPlatformMisc.cpp, MakeEngineDir), and ships a
// DISABLE_CWD_CHANGES build guard that asserts on any attempt to move it. The cwd belongs to the
// engine.
//
// So the VM gets a namespace of its own instead. Every path a script hands to `open` or `nt.*` is
// rewritten here before it reaches the OS, which closes all three styles and any fourth at one
// point instead of per call site.
//
// **The shape.** A path is normalized against the virtual install root, has one leading `Vampire/`
// (the moddir) folded away, and is then either:
//
//   * **read**  -- served from the writable overlay if it holds the file, else mapped through the
//     mount table onto the read-only content mirror (out/), else left pointing at the overlay so the
//     caller's own syscall fails with the game's own error semantics (`fileutil.exists` -> 0);
//   * **written** -- always into the overlay, never the mirror, with `a`/`r+` copying the mirror's
//     copy up first so an append or a read-modify-write sees the shipped bytes.
//
// A path that escapes the sandbox is the one hard denial, and it is the only case that raises.
//
// **Why writes need an overlay at all.** Root() is game-derived pipeline output that a re-export
// regenerates, so a script write into it is state that vanishes without warning. And the scripts do
// write: `vamputil.py:588` read-modify-writes `vdata/hackterminals/haven_pc.txt` to stamp the PC's
// name into an in-game email client, `vamputil.py:889+` copies the Unofficial Patch's `- hunter`
// asset variants over the shipped ones, and `zvtool_file.py:200` opens the `.bsp` "rb+" and appends
// to it. The overlay keeps all of it, and keeps it visible.
//
// Plain C++ with no Python or UObject dependency, so the whole path policy unit-tests without a VM
// (Elysium.Substrate.ScriptFS). The interpreter side is a thin shim over `vampire._fs_*`
// (ElysiumPythonVM.cpp) that only maps paths -- real `file` objects come back, so `readlines`,
// binary mode and `seek`/`truncate` all work natively.

// What a call intends to do with the path. Picked from an `open` mode string by AccessFromMode.
enum class EElysiumFsAccess : uint8
{
	// `r`/`rb`, plus stat/listdir/access. Overlay first, then the mirror.
	Read,
	// `w`/`wb`/`w+`, plus mkdir/unlink. Straight to the overlay; the file is truncated anyway, so
	// there is nothing to carry over from the mirror.
	Write,
	// `a`/`a+`/`r+`/`rb+`. The overlay, but the existing bytes matter -- copy the mirror's copy up
	// first if the overlay has none.
	Update,
};

class FElysiumScriptFS
{
public:
	// The VM's virtual install root: what `nt.getcwd()` returns and what every relative path
	// resolves against. This is the overlay directory itself, so a path that ever escaped the shim
	// lands in the sandbox rather than in the project tree. Backslash-separated and absolute, the
	// way the real `nt.getcwd()` is on Windows -- scripts slice it back off for display
	// (`dst.replace(getcwd()+"\\"+moddir+"\\", "")`, fileutil.py:135), which only lands if the
	// separators match.
	static const FString& VirtualRoot();

	// VtMB's own `sys.moddir`. Kept at the shipped value rather than neutralized to "." so
	// fileutil's write guard -- `path.find("\\"+moddir+"\\")`, refusing any write whose path does
	// not name the mod tree (fileutil.py:81,102,113,127,158) -- passes as authored instead of by
	// the accident that a "." moddir puts `\.\` in the string.
	static const TCHAR* ModDir() { return TEXT("Vampire"); }

	// Rewrite a VM-space path to a real one. Returns false ONLY on a policy denial (the path
	// escapes the sandbox); a permitted path that does not exist still resolves, because the game's
	// scripts branch on missing files (`fileutil.isFile(...)` gating a dialogue line, vamputil.py:1039)
	// and must keep getting the OS error rather than ours.
	static bool Resolve(const FString& VirtualPath, EElysiumFsAccess Access,
		FString& OutRealPath, FString& OutError);

	// Union of the overlay's and the mirror's listings for a virtual directory, de-duplicated
	// case-insensitively (the overlay wins). Backs `nt.listdir`.
	static bool ListDir(const FString& VirtualPath, TArray<FString>& OutNames, FString& OutError);

	// --- the pieces, factored out so the policy is testable without a filesystem ----------------

	// Absolutize against VirtualRoot(), fold `\` to `/`, collapse `.` and `..`, and drop one leading
	// `Vampire/` so both spellings of a tree path land on the same sandbox-relative form. False when
	// the path leaves the sandbox (a `..` above the root, or an absolute path pointing elsewhere).
	static bool NormalizeToSandbox(const FString& VirtualPath, FString& OutRelative);

	// Map a sandbox-relative path ("cfg/config.cfg") onto the read-only content mirror. False when
	// no mount covers that tree -- `materials/`, `models/`, `maps/` and VtMB's own `scripts/` have
	// no mirror, because the runtime consumes them baked or not at all.
	static bool MapToMirror(const FString& SandboxRelative, FString& OutRealPath);

	// Classify an `open` mode string. Unknown/empty modes read.
	static EElysiumFsAccess AccessFromMode(const FString& Mode);

	// Create the overlay root. Called once at VM start; safe to repeat. The mod tree needs no entry
	// of its own -- `Vampire` alone normalizes to the empty relative path, so `fileutil.isDir` on it
	// answers against the root and reads true the way it does in the real game.
	static void EnsureOverlayRoot();
};
