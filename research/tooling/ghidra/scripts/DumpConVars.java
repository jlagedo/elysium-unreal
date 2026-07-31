import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// Recovers the ConVar / ConCommand registration table of a Source DLL: for every
// call to a given constructor, walks back over the call site's PUSHes to recover
// the name string and default value, and the MOV ECX,<imm> that names the ConVar
// object itself. That object address is what every reader in the decompilation
// shows (`DAT_104d205c`), so this is the lookup that turns an anonymous global in
// a decompiled function back into a cvar name.
//
// Args (key=value, space separated):
//   ctor=<hex>    the constructor's entry point (one per run)
//   out=<path>    output file
//   back=<int>    how many instructions to walk back from a call site (default 14)
public class DumpConVars extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium-unreal/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/convars.txt");
        int back = Integer.parseInt(a.getOrDefault("back", "14"));
        Address ctor = toAddr(a.get("ctor"));
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName() + "  ctor " + ctor);
        out.println("// object      name                            args (pushed, first = last PUSH before CALL)");

        int n = 0;
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(ctor);
        while (it.hasNext()) {
            Reference r = it.next();
            Instruction call = getInstructionAt(r.getFromAddress());
            if (call == null || !call.getMnemonicString().startsWith("CALL")) continue;

            String object = "?";
            List<String> args = new ArrayList<>();
            Instruction ins = call.getPrevious();
            for (int i = 0; i < back && ins != null; i++, ins = ins.getPrevious()) {
                String m = ins.getMnemonicString();
                if (m.equals("CALL")) break;                       // previous call site
                if (m.equals("MOV") && "ECX".equals(ins.getDefaultOperandRepresentation(0))) {
                    Scalar s = ins.getScalar(1);
                    if (s != null && object.equals("?")) object = hex(s.getUnsignedValue());
                }
                else if (m.equals("PUSH")) {
                    Scalar s = ins.getScalar(0);
                    args.add(s == null ? ins.getDefaultOperandRepresentation(0) : describe(s.getUnsignedValue()));
                }
            }
            String name = args.isEmpty() ? "?" : args.get(args.size() - 1);
            out.printf("%-12s %-32s %s%n", object, name, String.join("  ", args));
            n++;
        }
        out.println("// " + n + " registrations");
        out.close();
        println("DumpConVars: wrote " + outPath + " (" + n + " registrations)");
    }

    // A pushed immediate is a string pointer, a float bit pattern, or a plain int.
    private String describe(long v) {
        Address addr = null;
        try { addr = toAddr(v); } catch (Exception e) { /* not an address */ }
        if (addr != null) {
            Data d = getDataAt(addr);
            if (d != null && d.getDataType() != null) {
                StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
                if (sdi != null && sdi.getStringLength() > 0) return "\"" + sdi.getStringValue() + "\"";
            }
        }
        return hex(v);
    }

    private String hex(long v) { return "0x" + Long.toHexString(v); }
}
