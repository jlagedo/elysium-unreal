import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

// Finds every instruction whose operands contain a given structure-field
// displacement (e.g. `MOV [ESI + 0x304], EAX`). Ghidra's xref machinery only
// tracks references to ADDRESSES, so a C++ member offset has no xrefs at all --
// the only way to enumerate every reader/writer of a field is to scan the
// operands of every instruction in the program.
//
// Args (key=value, space separated):
//   disps=<hex;...>  field displacements to match (e.g. 304;308)
//   out=<path>       output file
public class DumpFieldRefs extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/fieldrefs.txt");
        Set<Long> want = new HashSet<>();
        for (String s : a.getOrDefault("disps", "").split("[;,]"))
            if (!s.isEmpty()) want.add(Long.parseLong(s.trim(), 16));

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName() + "  displacements: " + a.get("disps"));

        int hits = 0, scanned = 0;
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            scanned++;
            boolean match = false;
            for (int op = 0; op < ins.getNumOperands() && !match; op++)
                for (Object o : ins.getOpObjects(op))
                    if (o instanceof Scalar && want.contains(((Scalar) o).getUnsignedValue())) {
                        match = true; break;
                    }
            if (!match) continue;
            hits++;
            Function f = getFunctionContaining(ins.getAddress());
            out.printf("%s  %-44s  in %s%n", ins.getAddress(), ins,
                    f == null ? "<none>" : f.getName() + " @ " + f.getEntryPoint());
        }
        out.println("// " + hits + " matching instructions (" + scanned + " scanned)");
        out.close();
        println("DumpFieldRefs: wrote " + outPath + " (" + hits + " hits)");
    }
}
