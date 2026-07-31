import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.*;
import java.util.*;

// Walk a CPython `PyMethodDef` table and decompile every method body behind it.
//
// This is the read side of VtMB's script API: `vampire.dll` registers module `vampire`
// via Py_InitModule4 and builds old-style classes with PyClass_New, each backed by a
// NULL-terminated table of stock 16-byte PyMethodDef records:
//
//     char*        ml_name    @0
//     PyCFunction  ml_meth    @4
//     int          ml_flags   @8      (METH_VARARGS=1, METH_KEYWORDS=2, METH_NOARGS=4)
//     char*        ml_doc     @0xC
//
// Every `ml_meth` points into an **MSVC incremental-link thunk table** at 0x1000xxxx --
// each entry is a 5-byte `JMP rel32` to the real body. Decompiling the stub reports an
// empty function, so the jump is followed before anything else happens (README: "the
// PyMethodDef function pointers are thunks -- decompile the JMP target").
//
// The doc strings are load-bearing, not decoration: they pin the contracts the shipped
// scripts rely on (FindEntityByName's "Returns None if not found" is what every `if ent:`
// guard tests). They are printed verbatim beside each body.
//
// Known tables (vampire.dll, imagebase 0x10000000 -- docs/python_bridge.md):
//   1058f7a8  11  module `vampire` globals
//   1058f868  24  Character (player + NPC)
//   1058f698  13  Entity base methods (12 distinct -- GetCenter appears twice)
//   1058f778   2  Entity.__getattr__ / __setattr__
//   1058f5d0   2  G -- ClearAll, keys
//   1058f620   2  file-like -- read, readline
//
// Args (key=value, space separated; one value each -- see README on arg splitting):
//   table=<hex>    the PyMethodDef table address                (required)
//   count=<int>    stop after N records (else the NULL terminator, cap 256)
//   depth=<int>    callee recursion depth per body (default 0 = the body alone)
//   decomp=<0|1>   decompile the bodies (default 1); 0 = table listing only
//   out=<path>     output file
public class DumpPyMethods extends GhidraScript {

    static final int REC_SIZE = 16;

    Memory mem;

    public void run() throws Exception {
        Map<String,String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        if (!a.containsKey("table")) {
            println("DumpPyMethods: need table=<hex>");
            return;
        }
        String outPath = a.getOrDefault("out",
            "E:/dev/elysium-unreal/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/pymethods.txt");
        long table  = Long.parseLong(a.get("table"), 16);
        int  cap    = Integer.parseInt(a.getOrDefault("count", "256"));
        int  depth  = Integer.parseInt(a.getOrDefault("depth", "0"));
        boolean doDecomp = !"0".equals(a.getOrDefault("decomp", "1"));
        mem = currentProgram.getMemory();

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName()
            + "  imagebase " + currentProgram.getImageBase());
        out.printf("// PyMethodDef table @ %08x%n", table);

        // --- pass 1: the table itself ---------------------------------------------------
        LinkedHashMap<Function,String> targets = new LinkedHashMap<>();
        List<String> rows = new ArrayList<>();
        Set<String> seenNames = new LinkedHashSet<>();
        int n = 0;
        for (; n < cap; n++) {
            long rec   = table + (long) n * REC_SIZE;
            long pName = u32(rec);
            long pMeth = u32(rec + 4);
            long flags = u32(rec + 8);
            long pDoc  = u32(rec + 0xC);
            if (pName == 0 && pMeth == 0) break;          // NULL sentinel

            String name = pName != 0 ? cstr(pName) : "(unnamed)";
            String doc  = pDoc  != 0 ? cstr(pDoc)  : "";
            long body   = resolveThunk(pMeth);

            rows.add(String.format("  [%2d] %-24s ml_meth=%08x%s flags=0x%x%s",
                n, name, pMeth,
                body != pMeth ? String.format(" -> body=%08x", body) : "",
                flags,
                seenNames.add(name) ? "" : "   *** DUPLICATE NAME ***"));

            if (doDecomp && body != 0) {
                Function f = functionAt(body);
                if (f != null) addWithCallees(f, depth, name, targets);
                else rows.add(String.format("       <no function at %08x>", body));
            }
            // The doc string is the contract; keep it with the row.
            if (!doc.isEmpty()) rows.add("       doc: " + doc);
        }

        out.println("// " + n + " records, " + seenNames.size() + " distinct names");
        out.println();
        out.println("======== TABLE ========");
        for (String r : rows) out.println(r);

        if (!doDecomp) {
            out.close();
            println("DumpPyMethods: wrote " + outPath + " (" + n + " records, no decompile)");
            return;
        }

        // --- pass 2: the bodies ---------------------------------------------------------
        out.println();
        out.println("// " + targets.size() + " function bodies");
        DecompInterface dec = new DecompInterface();
        dec.toggleCCode(true);
        dec.openProgram(currentProgram);
        for (Map.Entry<Function,String> e : targets.entrySet()) {
            Function f = e.getKey();
            out.println();
            out.println();
            out.println("//========================================================");
            out.println("// " + e.getValue() + "   " + f.getName(true)
                + "  @ " + f.getEntryPoint());
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
        println("DumpPyMethods: wrote " + outPath
            + " (" + n + " records, " + targets.size() + " bodies)");
    }

    // MSVC incremental-link thunk: `JMP rel32`. Follow it (once is enough in practice,
    // but chained stubs are cheap to tolerate) to the real body.
    long resolveThunk(long addr) throws Exception {
        long cur = addr;
        for (int hop = 0; hop < 4 && cur != 0; hop++) {
            if (u8(cur) != 0xE9) return cur;
            cur = cur + 5 + (int) u32(cur + 1);
        }
        return cur;
    }

    Function functionAt(long addr) throws Exception {
        Address ad = at(addr);
        Function f = getFunctionAt(ad);
        if (f == null) f = getFunctionContaining(ad);
        if (f == null) f = createFunction(ad, null);
        return f;
    }

    void addWithCallees(Function f, int depth, String why,
                        LinkedHashMap<Function,String> targets) throws Exception {
        if (f == null || targets.containsKey(f)) return;
        targets.put(f, why);
        if (depth <= 0) return;
        for (Function c : f.getCalledFunctions(monitor))
            addWithCallees(c, depth - 1, why + " <- callee", targets);
    }

    Address at(long v) { return currentProgram.getAddressFactory()
        .getDefaultAddressSpace().getAddress(v); }
    long u32(long v) throws Exception { return mem.getInt(at(v)) & 0xFFFFFFFFL; }
    int  u8 (long v) throws Exception { return mem.getByte(at(v)) & 0xFF; }

    String cstr(long v) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 512; i++) {
                int c = u8(v + i);
                if (c == 0) break;
                sb.append((char) c);
            }
        } catch (Exception e) { /* ran off the mapped image -- keep what we have */ }
        return sb.toString();
    }
}
