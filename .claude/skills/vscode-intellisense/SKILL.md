---
name: vscode-intellisense
description: Regenerate the local VS Code IntelliSense config for the ElysiumUE C++ module. Use after adding a module dependency or plugin, or when go-to-definition / header resolution stops working in VS Code.
---

# VS Code IntelliSense

`python tools/setup_vscode.py` regenerates the local editor config — it runs UBT's
`-projectfiles -vscode` generator, then mirrors the module's include paths + forced includes into
`.vscode/settings.json` as `C_Cpp.default.*` (the fallback for every file the compile database
does not name, i.e. all headers). `settings.json` is separate because UBT overwrites
`c_cpp_properties.json` and the `.code-workspace` on every run but never touches it.

Re-run after adding a module dependency, plugin, or unresolvable source file.

Open `ElysiumUE.code-workspace`, not the bare folder — it mounts the engine tree as a second
workspace folder, which is what makes go-to-definition reach engine source.

Forced includes are build products, so the editor target must have been built at least once
(`build.bat`).
