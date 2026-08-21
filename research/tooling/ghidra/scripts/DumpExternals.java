import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.ExternalLocation;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

// Every function this module imports from another DLL, by the address its call edges carry.
//
// `DumpCorpus` records a callee as its entry point, and an imported function's entry point is a
// synthetic `EXTERNAL:0000001f` in Ghidra's external block. Nothing else in the corpus holds a
// row at that address, so the join in `callees` dropped 7,131 edges across the eight modules --
// every call into the CRT, Win32, or a sibling engine DLL, silently absent from "what does this
// function call".
//
// The name is in the project already; it just never left. This pass reads the external function
// list alone, so it costs seconds rather than the decompilation pass's hours.
//
// Args (key=value, space separated):
//   out=<path>    JSONL output file (one row per imported function)
public class DumpExternals extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        // Headless splits `key=value` on the '=' before the script sees it, so both forms have
        // to be accepted or every argument reads null.
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.get("out");
        if (outPath == null) {
            printerr("DumpExternals: out=<path> is required");
            return;
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new BufferedWriter(new FileWriter(outPath), 1 << 16));
        FunctionManager functions = currentProgram.getFunctionManager();
        Iterator<Function> all = functions.getExternalFunctions().iterator();
        int rows = 0, unnamed = 0;
        while (all.hasNext()) {
            Function function = all.next();
            String library = "";
            String original = "";
            ExternalLocation location = function.getExternalLocation();
            if (location != null) {
                library = location.getLibraryName() == null ? "" : location.getLibraryName();
                // The mangled or ordinal form, where the import was by ordinal and Ghidra
                // resolved a name for it from a type database. Keeping both means an ordinal
                // import is still identifiable when the resolved name is wrong.
                original = location.getOriginalImportedName() == null
                        ? "" : location.getOriginalImportedName();
            }
            String name = function.getName();
            if (name.startsWith("EXTERNAL_") || name.isEmpty()) unnamed++;
            out.println("{\"a\":\"" + function.getEntryPoint() + "\""
                    + ",\"lib\":" + json(library)
                    + ",\"name\":" + json(name)
                    + ",\"import\":" + json(original) + "}");
            rows++;
        }
        out.close();
        println("DumpExternals: " + currentProgram.getName() + " -> " + rows
                + " imported functions (" + unnamed + " with no recovered name) -> " + outPath);
    }

    private static String json(String value) {
        StringBuilder text = new StringBuilder(value.length() + 16).append('"');
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '"': text.append("\\\""); break;
                case '\\': text.append("\\\\"); break;
                case '\n': text.append("\\n"); break;
                case '\r': text.append("\\r"); break;
                case '\t': text.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7e) text.append(String.format("\\u%04x", (int) c));
                    else text.append(c);
            }
        }
        return text.append('"').toString();
    }
}
