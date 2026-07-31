import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

// Raw disassembly dump for a function. The decompiler folds away which string
// constant a call was given when the call is varargs/thiscall (Source's KeyValues
// accessors are both), so the C output shows a bare `GetInt()` with no key. The
// listing still shows the `PUSH <string>`, so dump instructions when the key name
// or default argument is the thing in question.
//
// Args (key=value, space separated):
//   funcs=<hex;...>  function entry points (or any address inside one)
//   count=<int>      max instructions per function (default 400)
//   out=<path>       output file
public class DumpAsm extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/asm.txt");
        int count = Integer.parseInt(a.getOrDefault("count", "400"));
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        for (String s : (a.getOrDefault("funcs", "")).split("[;,]")) {
            if (s.isEmpty()) continue;
            Address addr = toAddr(s);
            Function f = getFunctionAt(addr);
            if (f == null) f = getFunctionContaining(addr);

            // Code reached only through a data pointer (a qsort comparator, a
            // vtable slot) is left undisassembled by the analyzers, so there is
            // no function to walk. Disassemble at the address and dump a flat
            // instruction run instead.
            Address start = f == null ? addr : f.getEntryPoint();
            if (listing.getInstructionAt(start) == null) disassemble(start);

            out.println("\n//========================================================");
            out.println("// " + (f == null ? "<raw> " + addr : f.getName(true) + "  @ " + f.getEntryPoint()));
            out.println("//========================================================");

            Instruction ins = listing.getInstructionAt(start);
            for (int n = 0; ins != null && n < count; n++) {
                // Ghidra puts the resolved string/data operand in the EOL comment
                // it auto-generates for a reference; append it so `PUSH 0x201a892c`
                // reads as the key name it actually is.
                String cmt = listing.getComment(CodeUnit.EOL_COMMENT, ins.getAddress());
                StringBuilder refs = new StringBuilder();
                for (var r : ins.getReferencesFrom()) {
                    var d = listing.getDataAt(r.getToAddress());
                    if (d != null && d.getValue() instanceof String)
                        refs.append("  \"").append(((String) d.getValue()).replace("\n", "\\n")).append("\"");
                }
                out.printf("%s  %-40s%s%s%n", ins.getAddress(), ins,
                        refs, cmt == null ? "" : "  ; " + cmt);
                if (f != null && !f.getBody().contains(ins.getMaxAddress())) break;
                if (f == null && ins.getMnemonicString().startsWith("RET")) break;
                ins = ins.getNext();
            }
        }
        out.close();
        println("DumpAsm: wrote " + outPath);
    }
}
