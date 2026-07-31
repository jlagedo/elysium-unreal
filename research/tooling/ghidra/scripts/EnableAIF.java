import ghidra.app.script.GhidraScript;

// Pre-script: turn on the analyzers that find code the default set misses.
//
// Some VtMB binaries (vguimatsurface.dll) reach whole subsystems only through
// vtables and interface pointers built at runtime, so the stock analyzers never
// flow into them and leave the bytes undisassembled — `getFunctionContaining` on
// a string's referencing instruction then returns null. The Aggressive Instruction
// Finder disassembles those gaps and defines the functions.
//
// Must run as a -preScript: analysis options are read when analysis starts, so
// setting them from a -postScript has no effect.
public class EnableAIF extends GhidraScript {

    public void run() throws Exception {
        String[] on = {
            "Aggressive Instruction Finder",
            "Decompiler Parameter ID",
        };
        for (String name : on) {
            setAnalysisOption(currentProgram, name, "true");
            println("EnableAIF: enabled " + name);
        }
    }
}
