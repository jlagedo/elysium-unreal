import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

// Every function's disassembly, as JSONL -- the fallback for everything the decompiler dropped.
//
// The C is a reconstruction, and the corpus now records where that reconstruction is damaged:
// blocks removed as unreachable, a jump table it could not follow, a frame it could not track.
// A flag that says "this C is incomplete" is only useful next to something complete, and the
// listing is that. It also answers what the C structurally cannot: the decompiler folds away
// which string constant a varargs or __thiscall call was handed, so a KeyValues accessor reads as
// a bare `GetInt()` with no key -- while the listing still shows the `PUSH <string>`.
//
// The per-instruction formatting is DumpAsm's, including the trick of appending the resolved
// string operand so `PUSH 0x201a892c` reads as the key it actually is.
//
// This lands in its own database, not the corpus. It is roughly the same order of size as every
// decompiled function put together, and every MCP query opens the corpus.
//
// Args (key=value, space separated):
//   out=<dir>    output directory
//   limit=<int>  stop after this many functions (0 = all; for a smoke run)
public class DumpListing extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outDir = a.get("out");
        if (outDir == null) {
            printerr("DumpListing: out=<dir> is required");
            return;
        }
        int limit = Integer.parseInt(a.getOrDefault("limit", "0"));
        String module = currentProgram.getName();
        new File(outDir).mkdirs();

        Listing listing = currentProgram.getListing();
        PrintWriter out = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "listing-" + module + ".jsonl")), 1 << 20));

        int functions = 0;
        long instructions = 0;
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext() && !monitor.isCancelled()) {
            Function function = all.next();
            Address entry = function.getEntryPoint();
            StringBuilder body = new StringBuilder(1024);
            for (Instruction instruction : listing.getInstructions(function.getBody(), true)) {
                body.append(instruction.getAddress()).append("  ").append(instruction);
                for (Reference reference : instruction.getReferencesFrom()) {
                    Data datum = listing.getDataAt(reference.getToAddress());
                    if (datum != null && datum.getValue() instanceof String) {
                        body.append("  \"")
                            .append(((String) datum.getValue()).replace("\n", "\\n"))
                            .append('"');
                    }
                }
                String comment = listing.getComment(CodeUnit.EOL_COMMENT,
                        instruction.getAddress());
                if (comment != null) body.append("  ; ").append(comment);
                body.append('\n');
                instructions++;
            }
            if (body.length() == 0) continue;
            out.println("{\"a\":\"" + entry + "\",\"asm\":" + json(body.toString()) + "}");
            functions++;
            if (functions % 5000 == 0) println("DumpListing: " + functions + " functions");
            if (limit > 0 && functions >= limit) break;
        }
        out.close();
        println("DumpListing: " + module + " -> " + functions + " functions, "
                + instructions + " instructions");
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
