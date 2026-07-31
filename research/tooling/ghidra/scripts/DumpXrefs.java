import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

// Lists every reference TO a set of addresses, with the containing function of
// each referencing site. Use to find the callers of a function (the fixup pass
// for a parser is usually in its caller) or every writer to a data address.
//
// Args (key=value, space separated):
//   addrs=<hex;...>  addresses to find references to
//   out=<path>       output file
public class DumpXrefs extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/xrefs.txt");
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        for (String s : (a.getOrDefault("addrs", "")).split("[;,]")) {
            if (s.isEmpty()) continue;
            Address addr = toAddr(s);
            out.println("\n//======== refs to " + addr + " ========");
            Function tf = getFunctionAt(addr);
            if (tf != null) out.println("// target function: " + tf.getName(true));
            int n = 0;
            for (Reference r : getReferencesTo(addr)) {
                Function f = getFunctionContaining(r.getFromAddress());
                out.printf("%s  %-14s  in %s%n", r.getFromAddress(), r.getReferenceType(),
                        f == null ? "<none>" : f.getName(true) + " @ " + f.getEntryPoint());
                n++;
            }
            out.println("// " + n + " references");
        }
        out.close();
        println("DumpXrefs: wrote " + outPath);
    }
}
