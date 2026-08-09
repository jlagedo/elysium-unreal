import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

// Targeted decompiler dump. Decompiles seed functions (+ their callees to a
// depth) and every member function of given vftables, writing C pseudocode to a
// file. Iterate by feeding vtable addresses discovered in previous dumps.
//
// Args (key=value, space separated):
//   out=<path>        output file (default E:/tmp/elysium_ghidra_out/funcs.txt)
//   funcs=<hex,...>   seed function entry points (decompiled + callees)
//   vtables=<hex,...> vftable addresses; every function pointer slot is decompiled
//   depth=<int>       callee recursion depth for seeds (default 1)
//   cap=<int>         max functions to decompile (default 80)
//   vtslots=<int>     max vtable slots to scan (default 48)
public class DumpFuncs extends GhidraScript {

    public void run() throws Exception {
        // Ghidra headless splits each "key=value" token on '=', so args arrive as
        // alternating key/value pairs. Support both that and a literal "key=value".
        Map<String,String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0,eq), raw[i].substring(eq+1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i+1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/funcs.txt");
        int depth   = Integer.parseInt(a.getOrDefault("depth", "1"));
        int cap     = Integer.parseInt(a.getOrDefault("cap", "80"));
        int vtslots = Integer.parseInt(a.getOrDefault("vtslots", "48"));
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        // ordered target set: function -> why
        LinkedHashMap<Function,String> targets = new LinkedHashMap<>();

        // --- vtables: read consecutive pointers, add each pointed function ---
        for (String v : csv(a.get("vtables"))) {
            Address va = toAddr(v);
            out.println("// vftable " + va);
            for (int i = 0; i < vtslots; i++) {
                Address slot = va.add((long)i * 4);
                long ptr;
                try { ptr = getInt(slot) & 0xffffffffL; } catch (Exception e) { break; }
                if (ptr == 0) break;
                Address tgt = toAddr(ptr);
                ghidra.program.model.mem.MemoryBlock mb = getMemoryBlock(tgt);
                if (mb == null || !mb.isExecute()) break;   // pointer left .text => vtable end
                Function f = getFunctionAt(tgt);
                if (f == null) f = getFunctionContaining(tgt);
                if (f == null) f = createFunction(tgt, null);
                if (f == null) { out.println("//   [" + i + "] " + tgt + " <no func>"); continue; }
                targets.putIfAbsent(f, "vtbl " + v + "[" + i + "]");
                out.println("//   [" + i + "] " + tgt + " " + f.getName());
            }
        }

        // --- every function whose entry is in [lo,hi]  (range=loHex-hiHex) ---
        String range = a.get("range");
        if (range != null && range.contains("-")) {
            String[] pr = range.split("-");
            Address lo = toAddr(pr[0]), hi = toAddr(pr[1]);
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(lo, true);
            while (it.hasNext()) {
                Function f = it.next();
                if (f.getEntryPoint().compareTo(hi) > 0) break;
                targets.putIfAbsent(f, "range");
            }
        }

        // --- seed functions + callees to `depth` ---
        // A seed may be any address inside the function (e.g. the instruction that
        // references a string found during recon), not just its entry point.
        for (String s : csv(a.get("funcs"))) {
            Address sa = toAddr(s);
            Function f = getFunctionAt(sa);
            if (f == null) f = getFunctionContaining(sa);
            if (f != null) addWithCallees(f, depth, "seed", targets);
            else out.println("// no function at/containing " + sa);
        }

        out.println("\n// " + targets.size() + " target functions (cap " + cap + ")");

        DecompInterface dec = new DecompInterface();
        dec.toggleCCode(true);
        dec.openProgram(currentProgram);
        int n = 0;
        for (Map.Entry<Function,String> e : targets.entrySet()) {
            if (n++ >= cap) { out.println("\n// [capped]"); break; }
            Function f = e.getKey();
            out.println("\n\n//========================================================");
            out.println("// " + f.getName(true) + "  @ " + f.getEntryPoint() + "   (" + e.getValue() + ")");
            out.println("//========================================================");
            try {
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                if (r != null && r.getDecompiledFunction() != null)
                    out.println(r.getDecompiledFunction().getC());
                else
                    out.println("// <no result>");
            } catch (Exception ex) { out.println("// <ex: " + ex + ">"); }
        }
        dec.dispose();
        out.close();
        println("DumpFuncs: wrote " + outPath + " (" + Math.min(n,cap) + " functions)");
    }

    void addWithCallees(Function f, int depth, String why,
                        LinkedHashMap<Function,String> targets) throws Exception {
        if (f == null || targets.containsKey(f)) return;
        targets.put(f, why);
        if (depth <= 0) return;
        for (Function c : f.getCalledFunctions(monitor))
            addWithCallees(c, depth - 1, "callee<-" + f.getEntryPoint(), targets);
    }

    // Ghidra headless splits script args on ',' / ';' as well as '=', so a
    // direct runner invocation can use '+' to preserve one multi-value token.
    static String[] csv(String s) {
        if (s == null || s.isEmpty()) return new String[0];
        return s.split("[;,+]");
    }
}
